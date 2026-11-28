//===============================================================================//
//
// Purpose: Client Sound Miles implementation
//
//===============================================================================//
#include "core/stdafx.h"
#include "tier0/fasttimer.h"
#include "tier0/commandline.h"
#include "tier1/cvar.h"
#include "rtech/core/strutils.h"
#include "rtech/async/asyncio.h"
#include "rtech/pak/pakstate.h"
#include "rtech/pak/pakparse.h"
#include "rtech/pak/paktools.h"
#include "filesystem/filesystem.h"
#include "pluginsystem/modsystem.h"
#include "ebisusdk/EbisuSDK.h"
#include "game/client/viewrender.h"
#include "game/client/cliententitylist.h"
#include "mathlib/mathlib.h"
#include "miles_impl.h"
#include "miles/src/sdk/shared/rrthreads2.h"
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "winmm.lib")

//-----------------------------------------------------------------------------
// Console variables
//-----------------------------------------------------------------------------
static ConVar miles_debug("miles_debug", "0", FCVAR_DEVELOPMENTONLY, "Enables debug prints for the Miles Sound System", "1 = print; 0 (zero) = no print");
static ConVar miles_warnings("miles_warnings", "0", FCVAR_RELEASE, "Enables warning prints for the Miles Sound System", "1 = print; 0 (zero) = no print");
static ConVar rpakwav_enable("rpakwav_enable", "1", FCVAR_RELEASE, "Enables packed WAV RPAK audio event handling", "1 = enabled; 0 = native Miles only");
static ConVar rpakwav_debug("rpakwav_debug", "0", FCVAR_RELEASE, "Enables packed WAV RPAK audio debug prints", "1 = print; 0 (zero) = no print");
static ConVar rpakwav_use_menu_volume("rpakwav_use_menu_volume", "1", FCVAR_RELEASE, "Scales packed WAV playback by the Audio menu volume sliders", "1 = enabled; 0 = ignore menu sliders");
static ConVar rpakwav_spatial_enable("rpakwav_spatial_enable", "1", FCVAR_RELEASE, "Applies queued 3D Miles positions to packed WAV playback", "1 = enabled; 0 = centered playback");
static ConVar rpakwav_shared_mixer("rpakwav_shared_mixer", "1", FCVAR_RELEASE, "Mixes custom sound effects through one output with overlap limiting", "0 = legacy independent outputs for comparison");
static ConVar rpakwav_spatial_min_distance("rpakwav_spatial_min_distance", "96", FCVAR_RELEASE, "Packed WAV spatial audio full-volume distance in game units");
static ConVar rpakwav_spatial_max_distance("rpakwav_spatial_max_distance", "12000", FCVAR_RELEASE, "Packed WAV spatial audio fade distance in game units");
static ConVar rpakwav_spatial_min_volume("rpakwav_spatial_min_volume", "0", FCVAR_RELEASE, "Minimum packed WAV spatial attenuation multiplier");
static ConVar rpakwav_spatial_pan_strength("rpakwav_spatial_pan_strength", "0.85", FCVAR_RELEASE, "Packed WAV stereo pan strength for queued 3D positions");

namespace
{
	constexpr uint32_t RPAK_WAV_ASRC_MAGIC = ('R' | ('S' << 8) | ('A' << 16) | ('S' << 24));
	constexpr uint32_t RPAK_WAV_AEVT_MAGIC = ('R' | ('S' << 8) | ('A' << 16) | ('E' << 24));
	constexpr uint32_t RPAK_WAV_ASRC_ASSET_TYPE = ('a' | ('w' << 8) | ('s' << 16) | ('r' << 24));
	constexpr uint32_t RPAK_WAV_AEVT_ASSET_TYPE = ('a' | ('e' << 8) | ('v' << 16) | ('t' << 24));
	constexpr uint32_t RPAK_WAV_RIFF_MAGIC = ('R' | ('I' << 8) | ('F' << 16) | ('F' << 24));
	constexpr uint32_t RPAK_WAV_WAVE_MAGIC = ('W' | ('A' << 8) | ('V' << 16) | ('E' << 24));
	constexpr uint32_t RPAK_WAV_FMT_MAGIC = ('f' | ('m' << 8) | ('t' << 16) | (' ' << 24));
	constexpr uint32_t RPAK_WAV_DATA_MAGIC = ('d' | ('a' << 8) | ('t' << 16) | ('a' << 24));
	constexpr WORD RPAK_WAV_FORMAT_IEEE_FLOAT = 3;
	constexpr WORD RPAK_WAV_FORMAT_EXTENSIBLE = 0xFFFE;
	constexpr DWORD RPAK_WAV_SPEAKER_FRONT_LEFT = 0x00000001;
	constexpr DWORD RPAK_WAV_SPEAKER_FRONT_RIGHT = 0x00000002;
	constexpr DWORD RPAK_WAV_SPEAKER_FRONT_CENTER = 0x00000004;
	constexpr DWORD RPAK_WAV_SPEAKER_LOW_FREQUENCY = 0x00000008;
	constexpr DWORD RPAK_WAV_SPEAKER_BACK_LEFT = 0x00000010;
	constexpr DWORD RPAK_WAV_SPEAKER_BACK_RIGHT = 0x00000020;
	constexpr DWORD RPAK_WAV_SPEAKER_FRONT_LEFT_OF_CENTER = 0x00000040;
	constexpr DWORD RPAK_WAV_SPEAKER_FRONT_RIGHT_OF_CENTER = 0x00000080;
	constexpr DWORD RPAK_WAV_SPEAKER_BACK_CENTER = 0x00000100;
	constexpr DWORD RPAK_WAV_SPEAKER_SIDE_LEFT = 0x00000200;
	constexpr DWORD RPAK_WAV_SPEAKER_SIDE_RIGHT = 0x00000400;
	constexpr DWORD RPAK_WAV_SPEAKER_TOP_CENTER = 0x00000800;
	constexpr DWORD RPAK_WAV_SPEAKER_TOP_FRONT_LEFT = 0x00001000;
	constexpr DWORD RPAK_WAV_SPEAKER_TOP_FRONT_CENTER = 0x00002000;
	constexpr DWORD RPAK_WAV_SPEAKER_TOP_FRONT_RIGHT = 0x00004000;
	constexpr DWORD RPAK_WAV_SPEAKER_TOP_BACK_LEFT = 0x00008000;
	constexpr DWORD RPAK_WAV_SPEAKER_TOP_BACK_CENTER = 0x00010000;
	constexpr DWORD RPAK_WAV_SPEAKER_TOP_BACK_RIGHT = 0x00020000;
	constexpr uint32_t RPAK_WAV_SOURCE_VERSION = 1;
	constexpr uint32_t RPAK_WAV_EVENT_VERSION = 4;
	constexpr uint32_t RPAK_WAV_EVENT_GROUP_VERSION = 5;
	constexpr uint32_t RPAK_WAV_MAX_SOURCE_GROUPS = 16;
	constexpr size_t RPAK_WAV_EVENT_STREAM_PATH_MAX = 260;
	constexpr int RPAK_WAV_PENDING_EVENT_FRAMES = 900;
	constexpr uint32_t RPAK_WAV_EVENT_MODE_PLAY = 0;
	constexpr uint32_t RPAK_WAV_EVENT_MODE_STOP_EVENTS = 1;
	constexpr uint32_t RPAK_WAV_EVENT_MODE_STOP_MUSIC = 2;
	constexpr uint32_t RPAK_WAV_EVENT_MODE_STOP_ALL = 3;
	constexpr uint32_t RPAK_WAV_EVENT_MODE_STOP_MANAGED = 4;
	constexpr uint32_t RPAK_WAV_EVENT_FLAG_MANAGED = (1u << 0);
	constexpr uint32_t RPAK_WAV_EVENT_FLAG_MUSIC = (1u << 1);
	constexpr uint32_t RPAK_WAV_EVENT_FLAG_REPLACE_SAME_EVENT = (1u << 2);
	constexpr uint32_t RPAK_WAV_EVENT_FLAG_LOOP = (1u << 3);
	constexpr uint32_t RPAK_WAV_EVENT_FLAG_IGNORE_WHILE_PLAYING = (1u << 4);
	constexpr uint32_t RPAK_WAV_EVENT_FLAG_FINISH_RETRIGGER_GRACE = (1u << 5);
	constexpr uint64_t RPAK_WAV_FINISH_RETRIGGER_GRACE_MS = 300;
	constexpr uint32_t RPAK_WAV_MAX_REGISTERED_EVENTS = 256;
	constexpr uint32_t RPAK_WAV_MAX_REGISTERED_SOURCES = 1024;
	constexpr size_t RPAK_WAV_SPATIAL_BUFFER_COUNT = 4;
	constexpr uint32_t RPAK_WAV_SPATIAL_CHUNK_MS = 20;
	constexpr uint32_t RPAK_WAV_SPATIAL_GAIN_RAMP_MS = 10;
	constexpr uint32_t RPAK_WAV_MAX_SOURCE_CACHE_ENTRIES = 1024;
	constexpr size_t RPAK_WAV_MAX_PLAY_REQUESTS = 256;
	constexpr size_t AUDIO_SCRIPT_MAX_PLAY_REQUESTS = 256;
	constexpr size_t AUDIO_SCRIPT_MAX_VOICES = 4096;
	constexpr float AUDIO_SCRIPT_MIN_VOLUME = 0.0f;
	constexpr float AUDIO_SCRIPT_MAX_VOLUME = 1.0f;
	constexpr float AUDIO_SCRIPT_MIN_PITCH = 0.25f;
	constexpr float AUDIO_SCRIPT_MAX_PITCH = 4.0f;
	// Keep custom packed-WAV assets at half gain independent of their JSON
	// event volume. Native Miles events do not pass through this mixer.
	constexpr float RPAK_WAV_GLOBAL_GAIN = 0.5f;
	constexpr float RPAK_WAV_DEFAULT_WEAPON_FIRE_RATE = 10.0f;
	constexpr uint64_t RPAK_WAV_WEAPON_LOOP_CACHE_REFRESH_INTERVAL_MS = 1000;
	constexpr uint64_t RPAK_WAV_WEAPON_LOOP_IDLE_TIMEOUT_MS = 15000;
	constexpr size_t RPAK_WAV_WEAPON_LOOP_MAX_CATCHUP_SHOTS = 8;

	struct RPakWavSpatialInfo_t
	{
		bool valid;
		Vector3D position;
		uint32_t emitterEntity = UINT32_MAX;

		RPakWavSpatialInfo_t()
			: valid(false),
			  position(0.0f, 0.0f, 0.0f)
		{
		}

		explicit RPakWavSpatialInfo_t(const Vector3D& sourcePosition)
			: valid(true),
			  position(sourcePosition)
		{
		}
	};

	enum class RPakWavAudioBus_e
	{
		Sfx,
		Dialogue,
		MusicGame,
		MusicLobby
	};
	constexpr size_t RPAK_WAV_MIXER_GROUP_COUNT = 5;
	enum class RPakWavMixerGroup_e : uint8_t { Weapons, Abilities, Critical, World, FirstPersonWeapons };

	bool RPakWav_IsWeaponMixerGroup(const RPakWavMixerGroup_e group)
	{
		return group == RPakWavMixerGroup_e::Weapons || group == RPakWavMixerGroup_e::FirstPersonWeapons;
	}

	struct RPakWavPendingEvent_t
	{
		std::string eventName;
		int framesRemaining;
		bool suppressWeaponLoop;
		RPakWavSpatialInfo_t spatial;
	};

	std::mutex s_rpakWavPendingEventsMutex;
	std::vector<RPakWavPendingEvent_t> s_rpakWavPendingEvents;
	thread_local int s_rpakWavManualMilesPlayDepth = 0;

	struct RPakWavParsedWave_t
	{
		WAVEFORMATEX format;
		DWORD channelMask;
		const uint8_t* data;
		uint32_t dataSize;
	};

	struct RPakWavCachedWave_t
	{
		PakGuid_t sourceGuid;
		WAVEFORMATEX format;
		DWORD channelMask;
		std::vector<uint8_t> pcmBytes;
		uint32_t sampleRate;
		uint16_t channels;

		RPakWavCachedWave_t()
			: sourceGuid(0),
			  format{},
			  channelMask(0),
			  pcmBytes(),
			  sampleRate(0),
			  channels(0)
		{
		}
	};

	struct RPakWavSourceCacheEntry_t
	{
		PakGuid_t sourceGuid;
		std::shared_ptr<const RPakWavCachedWave_t> wave;
	};

	struct RPakWavSpatialChunk_t
	{
		WAVEHDR header;
		std::vector<uint8_t> bytes;
		std::atomic_bool queued;
		bool prepared;

		RPakWavSpatialChunk_t()
			: header{},
			  bytes(),
			  queued(false),
			  prepared(false)
		{
		}
	};

	struct RPakWavVoice_t
	{
		uint32_t scriptVoiceId;
		uint64_t layerBatchId = 0;
		uint32_t sourceLayerCount = 1;
		bool sharedMixer = false;
		bool mixerPending = false;
		bool mixerFading = false;
		size_t mixerFadeRemaining = 0;
		double mixerCursor = 0.0;
		uint64_t mixerFinishFrame = 0;
		RPakWavMixerGroup_e mixerGroup = RPakWavMixerGroup_e::World;
		bool mixerFirstPerson = false;
		bool firstPersonPlayback = false;
		float mixerPriority = 0.0f;
		PakGuid_t eventGuid;
		std::string eventName;
		uint32_t flags;
		HWAVEOUT waveOut;
		WAVEHDR header;
		std::shared_ptr<const RPakWavCachedWave_t> wave;
		WAVEFORMATEX playbackFormat;
		std::vector<uint8_t> playbackBytes;
		std::array<RPakWavSpatialChunk_t, RPAK_WAV_SPATIAL_BUFFER_COUNT> spatialChunks;
		RPakWavSpatialInfo_t spatial;
		RPakWavAudioBus_e audioBus;
		size_t spatialReadOffset;
		float spatialTargetLeftGain = 1.0f;
		float spatialTargetRightGain = 1.0f;
		float spatialPreviousLeftGain = 1.0f;
		float spatialPreviousRightGain = 1.0f;
		uint32_t spatialUnderruns = 0;
		float eventVolume;
		float scriptVolume;
		float openedPitch;
		float scriptPitch;
		std::atomic_bool done;
		std::atomic_uint32_t spatialActiveChunks;
		std::atomic_bool spatialFinishedSubmitting;
		bool prepared;
		bool closed;
		bool countedMusic;
		bool looping;
		bool spatialStreaming;

		RPakWavVoice_t()
			: scriptVoiceId(0),
			  eventGuid(0),
			  eventName(),
			  flags(0),
			  waveOut(nullptr),
			  header{},
			  wave(),
			  playbackFormat{},
			  playbackBytes(),
			  spatialChunks(),
			  spatial(),
			  audioBus(RPakWavAudioBus_e::Sfx),
			  spatialReadOffset(0),
			  eventVolume(1.0f),
			  scriptVolume(1.0f),
			  openedPitch(1.0f),
			  scriptPitch(1.0f),
			  done(false),
			  spatialActiveChunks(0),
			  spatialFinishedSubmitting(false),
			  prepared(false),
			  closed(false),
			  countedMusic(false),
			  looping(false),
			  spatialStreaming(false)
		{
		}
	};

	struct RPakWavSelectedSource_t
	{
		PakGuid_t sourceGuid;
		uint32_t sampleRate;
		uint16_t channels;

		RPakWavSelectedSource_t()
			: sourceGuid(0),
			  sampleRate(0),
			  channels(0)
		{
		}
	};

	std::mutex s_rpakWavVoicesMutex;
	std::vector<std::unique_ptr<RPakWavVoice_t>> s_rpakWavVoices;
	// The callback only publishes completion and signals this event. WinMM
	// submission happens on a separate service thread, never in the callback.
	HANDLE s_rpakWavSpatialWakeEvent = nullptr;
	HANDLE s_rpakWavSpatialThread = nullptr;
	std::mutex s_rpakWavSpatialServiceMutex;
	std::atomic_bool s_rpakWavSpatialServiceStopping{ false };
	bool RPakWav_SameEmitter(const RPakWavSpatialInfo_t& a, const RPakWavSpatialInfo_t& b, const bool matchUnknown = true);

	struct RPakWavNaturalFinish_t
	{
		PakGuid_t eventGuid;
		RPakWavSpatialInfo_t spatial;
		uint64_t timeMs;
	};
	std::vector<RPakWavNaturalFinish_t> s_rpakWavNaturalFinishTimes;

	void RPakWav_ClearFinishGraceLocked(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t& spatial)
	{
		const uint64_t now = GetTickCount64();
		s_rpakWavNaturalFinishTimes.erase(std::remove_if(s_rpakWavNaturalFinishTimes.begin(),
			s_rpakWavNaturalFinishTimes.end(), [&](const auto& finish)
			{
				return now - finish.timeMs >= RPAK_WAV_FINISH_RETRIGGER_GRACE_MS ||
					(finish.eventGuid == eventGuid && RPakWav_SameEmitter(finish.spatial, spatial));
			}), s_rpakWavNaturalFinishTimes.end());
	}
	std::atomic_uint32_t s_rpakWavActiveMusicVoiceCount{ 0 };

	constexpr size_t RPAK_WAV_MIXER_RATE = 48000;
	constexpr size_t RPAK_WAV_MIXER_FRAMES = RPAK_WAV_MIXER_RATE * 10 / 1000;
	constexpr size_t RPAK_WAV_MIXER_LOOKAHEAD = RPAK_WAV_MIXER_RATE * 5 / 1000;
	constexpr size_t RPAK_WAV_MIXER_MAX_VOICES = 128;
	constexpr size_t RPAK_WAV_MIXER_MAX_EMITTER_BATCHES = 16;
	constexpr size_t RPAK_WAV_MIXER_RESERVED_WEAPON_VOICES = 32;
	constexpr size_t RPAK_WAV_MIXER_FADE_FRAMES = RPAK_WAV_MIXER_RATE * 10 / 1000;
	struct RPakWavMixerChunk_t
	{
		WAVEHDR header{};
		std::array<float, RPAK_WAV_MIXER_FRAMES * 2> samples{};
		std::atomic_bool queued{ false };
		bool prepared = false;
		uint64_t endFrame = 0;
	};
	// Device/ring/DSP state is protected by the voices mutex. The callback
	// only publishes completion; it never references individual voice objects.
	HWAVEOUT s_rpakWavMixerOutput = nullptr;
	std::array<RPakWavMixerChunk_t, RPAK_WAV_SPATIAL_BUFFER_COUNT> s_rpakWavMixerChunks;
	std::atomic_uint64_t s_rpakWavMixerPlayedFrames{ 0 };
	uint64_t s_rpakWavMixerWrittenFrames = 0;
	size_t s_rpakWavMixerNextChunk = 0;
	bool s_rpakWavMixerPaused = false;
	bool s_rpakWavMixerIdle = true;
	struct RPakWavMixerGroupState_t
	{
		std::array<float, RPAK_WAV_MIXER_FRAMES * 2> samples{};
		std::array<float, RPAK_WAV_MIXER_LOOKAHEAD * 2> delay{};
		std::deque<std::pair<uint64_t, float>> peaks;
		float gain = 1.0f;
		float inputPeak = 0.0f;
		uint64_t limitedFrames = 0;
	};
	std::array<RPakWavMixerGroupState_t, RPAK_WAV_MIXER_GROUP_COUNT> s_rpakWavMixerGroups;
	uint32_t s_rpakWavMixerUnderruns = 0;
	uint32_t s_rpakWavMixerStolenVoices = 0;
	uint32_t s_rpakWavMixerRejectedVoices = 0;
	std::atomic_bool s_rpakWavAudioFrameSubmitting{ false };

	enum class AudioScriptBackend_e : uint8_t
	{
		NativeMiles,
		CustomRPak
	};

	enum class AudioScriptVoiceState_e : uint8_t
	{
		Pending,
		Starting,
		Active,
		Failed,
		Stopped
	};

	struct AudioScriptVoiceEntry_t
	{
		uint32_t voiceId;
		uint32_t generation;
		AudioScriptBackend_e backend;
		AudioScriptVoiceState_e state;
		PakGuid_t eventGuid;
		PakGuid_t sourceGuid;
		uint32_t sourceIndex;
		std::string eventName;
		void* nativeEventId;
		float volume;
		float pitch;
		bool forceLoop;
		bool cancelled;
		uint32_t remainingCustomLayers;
	};

	struct AudioScriptPlayRequest_t
	{
		uint32_t voiceId;
	};

	std::atomic_uint32_t s_audioScriptGeneration{ 1 };
	std::atomic_uint32_t s_audioScriptNextVoiceId{ 1 };
	std::mutex s_audioScriptVoicesMutex;
	std::deque<AudioScriptVoiceEntry_t> s_audioScriptVoices;
	std::mutex s_audioScriptPlayQueueMutex;
	std::deque<AudioScriptPlayRequest_t> s_audioScriptPlayQueue;

	struct RPakWavPlayRequest_t
	{
		std::string eventName;
		bool warnOnFailure;
		bool allowLoadedPakFallback;
		bool suppressWeaponLoop;
		RPakWavSpatialInfo_t spatial;
	};

	std::mutex s_rpakWavPlayQueueMutex;
	std::deque<RPakWavPlayRequest_t> s_rpakWavPlayQueue;
	std::atomic_bool s_rpakWavPlayQueueOverflowWarned{ false };

	struct RPakWavWeaponLoopStop_t
	{
		PakGuid_t stopEventGuid;
		PakGuid_t loopEventGuid;
		std::string stopEventName;
		std::string loopEventName;
	};

	struct RPakWavWeaponLoopEvent_t
	{
		PakGuid_t loopEventGuid;
		std::string loopEventName;
		float fireRate;
		uint64_t intervalMs;
		int blockDepth;
	};

	struct RPakWavWeaponLoopRepeater_t
	{
		PakGuid_t eventGuid;
		uint32_t eventFlags;
		std::string eventName;
		std::vector<std::shared_ptr<const RPakWavCachedWave_t>> layers;
		uint64_t intervalMs;
		uint64_t nextPlayMs;
		uint64_t lastRefreshMs;
		RPakWavSpatialInfo_t spatial;
		float eventVolume;
	};

	std::mutex s_rpakWavWeaponLoopEventCacheMutex;
	std::vector<RPakWavWeaponLoopEvent_t> s_rpakWavWeaponLoopEvents;
	std::unordered_set<PakGuid_t> s_rpakWavWeaponSoundEvents;
	std::vector<RPakWavWeaponLoopStop_t> s_rpakWavWeaponLoopStops;
	bool s_rpakWavWeaponLoopEventCacheBuilt = false;
	uint64_t s_rpakWavWeaponLoopEventCacheNextCheckMs = 0;
	std::filesystem::file_time_type s_rpakWavWeaponLoopEventCacheNewestWriteTime{};
	std::mutex s_rpakWavWeaponLoopRepeatersMutex;
	std::vector<RPakWavWeaponLoopRepeater_t> s_rpakWavWeaponLoopRepeaters;

	std::mutex s_rpakWavSourceCacheMutex;
	std::vector<RPakWavSourceCacheEntry_t> s_rpakWavSourceCache;
	std::atomic_bool s_rpakWavSourceCacheOverflowWarned{ false };

	struct RPakWavAudioSourceHeader_v1_t;
	struct RPakWavAudioEventHeader_v2_t;

	struct RPakWavSourceLookup_t
	{
		PakGuid_t sourceGuid;
		const RPakWavAudioSourceHeader_v1_t* source;
		PakLoadedInfo_s* loadedPak;
		PakFile_s* pakFile;
	};

	struct RPakWavEventLookup_t
	{
		PakGuid_t eventGuid;
		const RPakWavAudioEventHeader_v2_t* event;
		PakLoadedInfo_s* loadedPak;
		PakFile_s* pakFile;
	};

	struct RPakWavEventRegistrySnapshot_t
	{
		uint32_t count;
		std::array<RPakWavEventLookup_t, RPAK_WAV_MAX_REGISTERED_EVENTS> entries;
	};

	RPakWavEventRegistrySnapshot_t s_rpakWavEmptyEventRegistry{};
	std::atomic<const RPakWavEventRegistrySnapshot_t*> s_rpakWavEventRegistry{ &s_rpakWavEmptyEventRegistry };
	std::mutex s_rpakWavEventRegistryWriteMutex;
	std::vector<std::unique_ptr<RPakWavEventRegistrySnapshot_t>> s_rpakWavEventRegistrySnapshots;
	std::atomic_bool s_rpakWavEventRegistryOverflowWarned{ false };
	std::atomic_uint32_t s_rpakWavRandomState{ 0 };

	struct RPakWavSourceRegistrySnapshot_t
	{
		uint32_t count;
		std::array<RPakWavSourceLookup_t, RPAK_WAV_MAX_REGISTERED_SOURCES> entries;
	};

	RPakWavSourceRegistrySnapshot_t s_rpakWavEmptySourceRegistry{};
	std::atomic<const RPakWavSourceRegistrySnapshot_t*> s_rpakWavSourceRegistry{ &s_rpakWavEmptySourceRegistry };
	std::mutex s_rpakWavSourceRegistryWriteMutex;
	std::vector<std::unique_ptr<RPakWavSourceRegistrySnapshot_t>> s_rpakWavSourceRegistrySnapshots;
	std::atomic_bool s_rpakWavSourceRegistryOverflowWarned{ false };

#pragma pack(push, 1)
	struct RPakWavAudioSourceHeader_v1_t
	{
		uint32_t magic;
		uint32_t version;
		PakGuid_t sourceGuid;
		PakPage_u name;
		PakPage_u virtualName;
		int64_t streamOffset : 52;
		int64_t streamIndex : 12;
		uint64_t streamSize;
		uint32_t sampleRate;
		uint32_t sampleCount;
		uint16_t channels;
		uint16_t bitsPerSample;
		uint16_t formatTag;
		uint16_t blockAlign;
		uint32_t averageBytesPerSecond;
		uint32_t flags;
	};
	static_assert(sizeof(RPakWavAudioSourceHeader_v1_t) == 72);

	struct RPakWavAudioEventHeader_v2_t
	{
		uint32_t magic;
		uint32_t version;
		PakGuid_t eventGuid;
		PakPage_u eventName;
		PakPage_u sourceGuids;
		uint32_t sourceCount;
		float volume;
		float pitch;
		uint32_t mode;
		uint32_t flags;

		PakGuid_t firstSourceGuid;
		int64_t firstSourceStreamOffset : 52;
		int64_t firstSourceStreamIndex : 12;
		uint64_t firstSourceStreamSize;
		uint32_t firstSourceSampleRate;
		uint32_t firstSourceSampleCount;
		uint16_t firstSourceChannels;
		uint16_t firstSourceBitsPerSample;
		uint16_t firstSourceFormatTag;
		uint16_t firstSourceBlockAlign;
		uint32_t firstSourceAverageBytesPerSecond;
		uint32_t firstSourceFlags;
		char firstSourceStreamPath[RPAK_WAV_EVENT_STREAM_PATH_MAX];
	};
	static_assert(sizeof(RPakWavAudioEventHeader_v2_t) == 360);
	struct RPakWavAudioSourceGroup_v1_t
	{
		uint32_t firstSourceIndex;
		uint32_t sourceCount;
	};
	static_assert(sizeof(RPakWavAudioSourceGroup_v1_t) == 8);

	struct RPakWavAudioEventHeader_v3_t
	{
		RPakWavAudioEventHeader_v2_t base;
		PakPage_u sourceGroups;
		uint32_t groupCount;
		uint32_t reserved;
		PakPage_u sourceWeights;
	};
	static_assert(sizeof(RPakWavAudioEventHeader_v3_t) == RPAK_WAV_EVENT_HEADER_STORAGE_SIZE);
#pragma pack(pop)

	bool RPakWav_DebugEnabled()
	{
		return miles_debug.GetBool() || rpakwav_debug.GetBool();
	}

	const ConVar* RPakWav_RegisteredSpatialSetting()
	{
		return g_pCVar ? g_pCVar->FindVar("rpakwav_spatial_enable") : nullptr;
	}

	bool RPakWav_SpatialEnabled()
	{
		// Read the same registered variable the console changes. An SDK module
		// can have a local static of the same name that is not the active root.
		const ConVar* const registered = RPakWav_RegisteredSpatialSetting();
		return registered ? registered->GetBool() : rpakwav_spatial_enable.GetBool();
	}

	bool RPakWav_SharedMixerEnabled()
	{
		const ConVar* const registered = g_pCVar ? g_pCVar->FindVar("rpakwav_shared_mixer") : nullptr;
		return registered ? registered->GetBool() : rpakwav_shared_mixer.GetBool();
	}

	void RPakWav_ReportSpatialSetting(const bool force)
	{
		if (!force && !RPakWav_DebugEnabled())
			return;
		const ConVar* const registered = RPakWav_RegisteredSpatialSetting();
		const int consoleValue = registered ? registered->GetInt() : -1;
		const int localValue = rpakwav_spatial_enable.GetInt();
		const int effective = (registered ? registered->GetBool() : rpakwav_spatial_enable.GetBool()) ? 1 : 0;
		static std::atomic_uint64_t lastSetting{ UINT64_MAX };
		const uint64_t snapshot = (static_cast<uint64_t>(static_cast<uint32_t>(consoleValue)) << 32) |
			static_cast<uint32_t>(localValue);
		const uint64_t previous = lastSetting.exchange(snapshot, std::memory_order_relaxed);
		if (force || snapshot != previous)
			Msg(eDLL_T::AUDIO, "Packed WAV spatial setting registered=%d local=%d effective=%d listenerReady=%u\n",
				consoleValue, localValue, effective, g_vecRenderOrigin && g_vecRenderAngles ? 1u : 0u);
	}

	bool RPakWav_NormalizeLinearWaveFormat(WAVEFORMATEX& format, bool& outChanged)
	{
		outChanged = false;
		if (format.wFormatTag != WAVE_FORMAT_PCM && format.wFormatTag != RPAK_WAV_FORMAT_IEEE_FLOAT)
			return false;
		if (format.nChannels == 0 || format.nSamplesPerSec == 0 ||
			format.wBitsPerSample == 0 || (format.wBitsPerSample % 8) != 0)
		{
			return false;
		}
		if (format.wFormatTag == RPAK_WAV_FORMAT_IEEE_FLOAT && format.wBitsPerSample != 32)
			return false;
		if (format.wFormatTag == WAVE_FORMAT_PCM && format.wBitsPerSample != 8 &&
			format.wBitsPerSample != 16 && format.wBitsPerSample != 24 &&
			format.wBitsPerSample != 32)
		{
			return false;
		}

		const uint64_t bytesPerSample = static_cast<uint64_t>(format.wBitsPerSample / 8);
		const uint64_t expectedBlockAlign = static_cast<uint64_t>(format.nChannels) * bytesPerSample;
		const uint64_t expectedAverageBytes = static_cast<uint64_t>(format.nSamplesPerSec) * expectedBlockAlign;
		if (expectedBlockAlign == 0 || expectedBlockAlign > UINT16_MAX || expectedAverageBytes > UINT32_MAX)
			return false;

		const WORD normalizedBlockAlign = static_cast<WORD>(expectedBlockAlign);
		const DWORD normalizedAverageBytes = static_cast<DWORD>(expectedAverageBytes);
		outChanged = format.nBlockAlign != normalizedBlockAlign ||
			format.nAvgBytesPerSec != normalizedAverageBytes ||
			format.cbSize != 0;
		format.nBlockAlign = normalizedBlockAlign;
		format.nAvgBytesPerSec = normalizedAverageBytes;
		format.cbSize = 0;
		return true;
	}

	void RPakWav_ReportWaveOutFailure(const char* const operation, const MMRESULT result,
		const RPakWavVoice_t& voice)
	{
		char errorText[256]{};
		if (waveOutGetErrorTextA(result, errorText, static_cast<UINT>(sizeof(errorText))) != MMSYSERR_NOERROR)
			V_snprintf(errorText, sizeof(errorText), "WinMM error %u", static_cast<unsigned int>(result));

		const WAVEFORMATEX& format = voice.playbackFormat;
		Warning(eDLL_T::AUDIO,
			"Packed WAV %s failed for event '%s': %s (%u); format=%u ch=%u rate=%lu bits=%u align=%u avg=%lu\n",
			operation ? operation : "waveOut operation",
			voice.eventName.empty() ? "<unnamed>" : voice.eventName.c_str(),
			errorText,
			static_cast<unsigned int>(result),
			static_cast<unsigned int>(format.wFormatTag),
			static_cast<unsigned int>(format.nChannels),
			static_cast<unsigned long>(format.nSamplesPerSec),
			static_cast<unsigned int>(format.wBitsPerSample),
			static_cast<unsigned int>(format.nBlockAlign),
			static_cast<unsigned long>(format.nAvgBytesPerSec));
	}

	float RPakWav_ClampFiniteFloat(const float value, const float fallback, const float minValue, const float maxValue)
	{
		if (!std::isfinite(value))
			return fallback;

		return std::clamp(value, minValue, maxValue);
	}

	float AudioScript_ClampVolume(const float volume)
	{
		return RPakWav_ClampFiniteFloat(volume, 1.0f, AUDIO_SCRIPT_MIN_VOLUME, AUDIO_SCRIPT_MAX_VOLUME);
	}

	float AudioScript_ClampPitch(const float pitch)
	{
		return RPakWav_ClampFiniteFloat(pitch, 1.0f, AUDIO_SCRIPT_MIN_PITCH, AUDIO_SCRIPT_MAX_PITCH);
	}

	AudioScriptVoiceEntry_t* AudioScript_FindVoiceLocked(const uint32_t voiceId)
	{
		for (AudioScriptVoiceEntry_t& voice : s_audioScriptVoices)
		{
			if (voice.voiceId == voiceId)
				return &voice;
		}

		return nullptr;
	}

	void AudioScript_ResetState()
	{
		s_audioScriptGeneration.fetch_add(1, std::memory_order_acq_rel);

		{
			std::lock_guard<std::mutex> lock(s_audioScriptPlayQueueMutex);
			s_audioScriptPlayQueue.clear();
		}

		{
			std::lock_guard<std::mutex> lock(s_audioScriptVoicesMutex);
			s_audioScriptVoices.clear();
		}
	}

	void AudioScript_RetireVoice(const uint32_t voiceId, const uint32_t completedLayers)
	{
		if (!voiceId)
			return;

		std::lock_guard<std::mutex> lock(s_audioScriptVoicesMutex);
		AudioScriptVoiceEntry_t* const voice = AudioScript_FindVoiceLocked(voiceId);
		if (!voice)
			return;
		if (voice->remainingCustomLayers > completedLayers)
		{
			voice->remainingCustomLayers -= completedLayers;
			return;
		}

		voice->remainingCustomLayers = 0;
		voice->state = AudioScriptVoiceState_e::Stopped;
		voice->nativeEventId = nullptr;
	}

	float RPakWav_GetMenuConVarFloat(const char* const cvarName, const float fallback)
	{
		if (!g_pCVar || !cvarName || !*cvarName)
			return fallback;

		ConVar* const cvar = g_pCVar->FindVar(cvarName);
		if (!cvar)
			return fallback;

		return RPakWav_ClampFiniteFloat(cvar->GetFloat(), fallback, 0.0f, 1.0f);
	}

	const char* RPakWav_AudioBusVolumeCVar(const RPakWavAudioBus_e bus)
	{
		switch (bus)
		{
		case RPakWavAudioBus_e::Dialogue:
			return "sound_volume_dialogue";
		case RPakWavAudioBus_e::MusicGame:
			return "sound_volume_music_game";
		case RPakWavAudioBus_e::MusicLobby:
			return "sound_volume_music_lobby";
		case RPakWavAudioBus_e::Sfx:
		default:
			return "sound_volume_sfx";
		}
	}

	const char* RPakWav_AudioBusDebugName(const RPakWavAudioBus_e bus)
	{
		switch (bus)
		{
		case RPakWavAudioBus_e::Dialogue:
			return "dialogue";
		case RPakWavAudioBus_e::MusicGame:
			return "music_game";
		case RPakWavAudioBus_e::MusicLobby:
			return "music_lobby";
		case RPakWavAudioBus_e::Sfx:
		default:
			return "sfx";
		}
	}

	float RPakWav_GetMenuVolumeForBus(const RPakWavAudioBus_e bus)
	{
		if (!rpakwav_use_menu_volume.GetBool())
			return 1.0f;

		const float masterVolume = RPakWav_GetMenuConVarFloat("sound_volume", 1.0f);
		const float busVolume = RPakWav_GetMenuConVarFloat(RPakWav_AudioBusVolumeCVar(bus), 1.0f);
		return std::clamp(masterVolume * busVolume, 0.0f, 1.0f);
	}

	float RPakWav_NormalizeEventVolume(const float eventVolume)
	{
		return RPakWav_ClampFiniteFloat(eventVolume, 1.0f, 0.0f, 8.0f);
	}

	float RPakWav_GetVoiceSampleGain(const RPakWavVoice_t& voice)
	{
		const float eventGain = RPakWav_NormalizeEventVolume(voice.eventVolume);
		const float menuGain = RPakWav_GetMenuVolumeForBus(voice.audioBus);
		return std::clamp(eventGain * menuGain * RPAK_WAV_GLOBAL_GAIN * voice.scriptVolume, 0.0f, 8.0f);
	}

	bool RPakWav_IsFiniteVector(const Vector3D& vec)
	{
		return std::isfinite(vec.x) && std::isfinite(vec.y) && std::isfinite(vec.z) &&
			vec.x > -1000000.0f && vec.x < 1000000.0f &&
			vec.y > -1000000.0f && vec.y < 1000000.0f &&
			vec.z > -1000000.0f && vec.z < 1000000.0f;
	}

	RPakWavSpatialInfo_t RPakWav_CaptureQueuedSpatialInfo()
	{
		if (!g_milesGlobals || s_rpakWavManualMilesPlayDepth > 0)
			return RPakWavSpatialInfo_t();
		if (g_milesGlobals->queuedSoundPositionType == 3)
		{
			RPakWavSpatialInfo_t spatial;
			memcpy(&spatial.emitterEntity, &g_milesGlobals->queuedSoundPosition, sizeof(spatial.emitterEntity));
			return spatial; // Preserve identity without interpreting the integer as XYZ.
		}

		// Non-spatial requests can retain old coordinates. Entity requests
		// store an integer entity number here, not an XYZ position.
		if (g_milesGlobals->queuedSoundPositionType != 1 && g_milesGlobals->queuedSoundPositionType != 4)
			return RPakWavSpatialInfo_t();

		const Vector3D position = g_milesGlobals->queuedSoundPosition;
		if (!RPakWav_IsFiniteVector(position))
			return RPakWavSpatialInfo_t();

		return RPakWavSpatialInfo_t(position);
	}

	bool RPakWav_SameEmitter(const RPakWavSpatialInfo_t& a, const RPakWavSpatialInfo_t& b,
		const bool matchUnknown)
	{
		if (a.emitterEntity != UINT32_MAX || b.emitterEntity != UINT32_MAX)
			return a.emitterEntity != UINT32_MAX && a.emitterEntity == b.emitterEntity;
		if (a.valid || b.valid)
			return a.valid && b.valid && (a.position - b.position).LengthSqr() <= 64.0f * 64.0f;
		return matchUnknown;
	}

	bool RPakWav_CalculateSpatialGains(const RPakWavSpatialInfo_t& spatial, float& outLeftGain, float& outRightGain)
	{
		outLeftGain = 1.0f;
		outRightGain = 1.0f;

		if (!RPakWav_SpatialEnabled() || !spatial.valid || !g_vecRenderOrigin || !g_vecRenderAngles)
			return false;

		const Vector3D listenerPosition = MainViewOrigin();
		const QAngle listenerAngles = MainViewAngles();
		if (!RPakWav_IsFiniteVector(listenerPosition) || !listenerAngles.IsValid())
			return false;

		Vector3D delta = spatial.position - listenerPosition;
		if (!RPakWav_IsFiniteVector(delta))
			return false;

		const float distanceSqr = delta.LengthSqr();
		if (distanceSqr <= 1.0f)
			return false;

		const float distance = std::sqrt(distanceSqr);
		Vector3D direction = delta * (1.0f / distance);

		Vector3D forward;
		Vector3D right;
		Vector3D up;
		AngleVectors(listenerAngles, &forward, &right, &up);

		const float panStrength = std::clamp(rpakwav_spatial_pan_strength.GetFloat(), 0.0f, 1.0f);
		const float pan = std::clamp(DotProduct(direction, right), -1.0f, 1.0f) * panStrength;
		const float leftPan = std::sqrt((std::max)(0.0f, 1.0f - pan));
		const float rightPan = std::sqrt((std::max)(0.0f, 1.0f + pan));
		const float panNormalize = 1.0f / (std::max)(1.0f, (std::max)(leftPan, rightPan));

		const float minDistance = (std::max)(0.0f, rpakwav_spatial_min_distance.GetFloat());
		const float maxDistance = (std::max)(minDistance + 1.0f, rpakwav_spatial_max_distance.GetFloat());
		const float minVolume = std::clamp(rpakwav_spatial_min_volume.GetFloat(), 0.0f, 1.0f);
		const float distanceT = std::clamp((distance - minDistance) / (maxDistance - minDistance), 0.0f, 1.0f);
		const float attenuation = minVolume + ((1.0f - minVolume) * (1.0f - distanceT));

		outLeftGain = std::clamp(leftPan * panNormalize * attenuation, 0.0f, 1.0f);
		outRightGain = std::clamp(rightPan * panNormalize * attenuation, 0.0f, 1.0f);

		return true;
	}

	bool RPakWav_IsSpatialFormatSupported(const WAVEFORMATEX& format)
	{
		if ((format.wFormatTag != WAVE_FORMAT_PCM && format.wFormatTag != RPAK_WAV_FORMAT_IEEE_FLOAT) ||
			format.nChannels == 0 || format.nChannels > 2 || format.wBitsPerSample == 0 ||
			format.nBlockAlign == 0 || (format.wBitsPerSample % 8) != 0)
		{
			return false;
		}

		if (format.wFormatTag == RPAK_WAV_FORMAT_IEEE_FLOAT)
			return format.wBitsPerSample == 32;

		return format.wBitsPerSample == 8 || format.wBitsPerSample == 16 ||
			format.wBitsPerSample == 24 || format.wBitsPerSample == 32;
	}

	float RPakWav_ClampSample(const float sample)
	{
		if (!std::isfinite(sample))
			return 0.0f;
		if (sample < -1.0f)
			return -1.0f;
		if (sample > 1.0f)
			return 1.0f;

		return sample;
	}

	float RPakWav_ReadNormalizedSample(const uint8_t* const sampleBytes, const WAVEFORMATEX& format)
	{
		if (format.wFormatTag == RPAK_WAV_FORMAT_IEEE_FLOAT)
		{
			float sample = 0.0f;
			memcpy(&sample, sampleBytes, sizeof(sample));
			// RSX exports decoder float values verbatim. Peaks above 1.0 are
			// valid input headroom: apply mixing/volume before output clipping.
			return std::isfinite(sample) ? sample : 0.0f;
		}

		switch (format.wBitsPerSample)
		{
		case 8:
			return (static_cast<int>(sampleBytes[0]) - 128) / 128.0f;

		case 16:
		{
			int16_t sample = 0;
			memcpy(&sample, sampleBytes, sizeof(sample));
			return static_cast<float>(sample) / 32768.0f;
		}

		case 24:
		{
			int32_t sample = static_cast<int32_t>(sampleBytes[0]) |
				(static_cast<int32_t>(sampleBytes[1]) << 8) |
				(static_cast<int32_t>(sampleBytes[2]) << 16);
			if (sample & 0x00800000)
				sample |= static_cast<int32_t>(0xFF000000);

			return static_cast<float>(sample) / 8388608.0f;
		}

		case 32:
		{
			int32_t sample = 0;
			memcpy(&sample, sampleBytes, sizeof(sample));
			return static_cast<float>(sample) / 2147483648.0f;
		}

		default:
			return 0.0f;
		}
	}

	void RPakWav_WriteNormalizedSample(uint8_t* const sampleBytes, const WAVEFORMATEX& format, const float sampleValue)
	{
		const float sample = RPakWav_ClampSample(sampleValue);

		if (format.wFormatTag == RPAK_WAV_FORMAT_IEEE_FLOAT)
		{
			memcpy(sampleBytes, &sample, sizeof(sample));
			return;
		}

		switch (format.wBitsPerSample)
		{
		case 8:
			sampleBytes[0] = static_cast<uint8_t>(std::clamp((sample * 127.0f) + 128.0f, 0.0f, 255.0f));
			break;

		case 16:
		{
			const int16_t quantized = static_cast<int16_t>(std::clamp(sample * 32767.0f, -32768.0f, 32767.0f));
			memcpy(sampleBytes, &quantized, sizeof(quantized));
			break;
		}

		case 24:
		{
			const int32_t quantized = static_cast<int32_t>(std::clamp(sample * 8388607.0f, -8388608.0f, 8388607.0f));
			sampleBytes[0] = static_cast<uint8_t>(quantized & 0xFF);
			sampleBytes[1] = static_cast<uint8_t>((quantized >> 8) & 0xFF);
			sampleBytes[2] = static_cast<uint8_t>((quantized >> 16) & 0xFF);
			break;
		}

		case 32:
		{
			// float rounds INT32_MAX to 2147483648; bound in double before casting.
			const int32_t quantized = static_cast<int32_t>(std::clamp(
				static_cast<double>(sample) * 2147483647.0, -2147483648.0, 2147483647.0));
			memcpy(sampleBytes, &quantized, sizeof(quantized));
			break;
		}
		}
	}

	struct RPakWavStereoWeights_t
	{
		float left;
		float right;
	};

	RPakWavStereoWeights_t RPakWav_GetSpeakerStereoWeights(const DWORD speaker)
	{
		switch (speaker)
		{
		case RPAK_WAV_SPEAKER_FRONT_LEFT:
			return { 1.0f, 0.0f };
		case RPAK_WAV_SPEAKER_FRONT_RIGHT:
			return { 0.0f, 1.0f };
		case RPAK_WAV_SPEAKER_FRONT_CENTER:
			return { 0.70710678f, 0.70710678f };
		case RPAK_WAV_SPEAKER_LOW_FREQUENCY:
			return { 0.5f, 0.5f };
		case RPAK_WAV_SPEAKER_BACK_LEFT:
		case RPAK_WAV_SPEAKER_SIDE_LEFT:
		case RPAK_WAV_SPEAKER_FRONT_LEFT_OF_CENTER:
			return { 0.70710678f, 0.0f };
		case RPAK_WAV_SPEAKER_BACK_RIGHT:
		case RPAK_WAV_SPEAKER_SIDE_RIGHT:
		case RPAK_WAV_SPEAKER_FRONT_RIGHT_OF_CENTER:
			return { 0.0f, 0.70710678f };
		case RPAK_WAV_SPEAKER_BACK_CENTER:
		case RPAK_WAV_SPEAKER_TOP_CENTER:
			return { 0.5f, 0.5f };
		case RPAK_WAV_SPEAKER_TOP_FRONT_LEFT:
		case RPAK_WAV_SPEAKER_TOP_BACK_LEFT:
			return { 0.5f, 0.0f };
		case RPAK_WAV_SPEAKER_TOP_FRONT_RIGHT:
		case RPAK_WAV_SPEAKER_TOP_BACK_RIGHT:
			return { 0.0f, 0.5f };
		case RPAK_WAV_SPEAKER_TOP_FRONT_CENTER:
		case RPAK_WAV_SPEAKER_TOP_BACK_CENTER:
			return { 0.35355339f, 0.35355339f };
		default:
			return { 0.0f, 0.0f };
		}
	}

	RPakWavStereoWeights_t RPakWav_GetDefaultChannelStereoWeights(const uint16_t channels, const uint16_t channel)
	{
		// WAVEFORMATEXTENSIBLE speaker order is preferred. These defaults cover
		// the standard interleaving used by plain PCM WAV files with no mask.
		if (channel == 0)
			return { 1.0f, 0.0f };
		if (channel == 1)
			return { 0.0f, 1.0f };

		switch (channels)
		{
		case 3: // FL, FR, FC
			return channel == 2 ? RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_FRONT_CENTER) : RPakWavStereoWeights_t{};
		case 4: // FL, FR, BL, BR
			return channel == 2 ? RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_BACK_LEFT) :
				RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_BACK_RIGHT);
		case 5: // FL, FR, FC, BL, BR
			if (channel == 2)
				return RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_FRONT_CENTER);
			return channel == 3 ? RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_BACK_LEFT) :
				RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_BACK_RIGHT);
		case 6: // FL, FR, FC, LFE, BL, BR
			if (channel == 2)
				return RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_FRONT_CENTER);
			if (channel == 3)
				return RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_LOW_FREQUENCY);
			return channel == 4 ? RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_BACK_LEFT) :
				RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_BACK_RIGHT);
		case 7: // FL, FR, FC, LFE, BC, SL, SR
			if (channel == 2)
				return RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_FRONT_CENTER);
			if (channel == 3)
				return RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_LOW_FREQUENCY);
			if (channel == 4)
				return RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_BACK_CENTER);
			return channel == 5 ? RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_SIDE_LEFT) :
				RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_SIDE_RIGHT);
		case 8: // FL, FR, FC, LFE, BL, BR, SL, SR
			if (channel == 2)
				return RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_FRONT_CENTER);
			if (channel == 3)
				return RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_LOW_FREQUENCY);
			if (channel == 4)
				return RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_BACK_LEFT);
			if (channel == 5)
				return RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_BACK_RIGHT);
			return channel == 6 ? RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_SIDE_LEFT) :
				RPakWav_GetSpeakerStereoWeights(RPAK_WAV_SPEAKER_SIDE_RIGHT);
		default:
			return (channel & 1) ? RPakWavStereoWeights_t{ 0.0f, 0.5f } :
				RPakWavStereoWeights_t{ 0.5f, 0.0f };
		}
	}

	bool RPakWav_BuildStereoChannelWeights(const uint16_t channels, const DWORD channelMask,
		std::vector<RPakWavStereoWeights_t>& outWeights)
	{
		if (channels <= 2)
			return false;

		outWeights.clear();
		outWeights.reserve(channels);

		uint16_t maskChannelCount = 0;
		for (uint32_t bit = 0; bit < 32; ++bit)
		{
			if (channelMask & (1u << bit))
				++maskChannelCount;
		}

		if (channelMask != 0 && maskChannelCount == channels)
		{
			for (uint32_t bit = 0; bit < 32 && outWeights.size() < channels; ++bit)
			{
				const DWORD speaker = (1u << bit);
				if (channelMask & speaker)
					outWeights.push_back(RPakWav_GetSpeakerStereoWeights(speaker));
			}
		}
		else
		{
			for (uint16_t channel = 0; channel < channels; ++channel)
				outWeights.push_back(RPakWav_GetDefaultChannelStereoWeights(channels, channel));
		}

		return outWeights.size() == channels;
	}

	bool RPakWav_DownmixToStereo(const WAVEFORMATEX& sourceFormat, const DWORD channelMask,
		const std::vector<uint8_t>& sourceBytes, WAVEFORMATEX& outFormat, std::vector<uint8_t>& outBytes)
	{
		if ((sourceFormat.wFormatTag != WAVE_FORMAT_PCM && sourceFormat.wFormatTag != RPAK_WAV_FORMAT_IEEE_FLOAT) ||
			sourceFormat.nChannels <= 2 || sourceFormat.wBitsPerSample == 0 ||
			(sourceFormat.wBitsPerSample % 8) != 0 || sourceFormat.nBlockAlign == 0)
		{
			return false;
		}

		if (sourceFormat.wFormatTag == RPAK_WAV_FORMAT_IEEE_FLOAT && sourceFormat.wBitsPerSample != 32)
			return false;
		if (sourceFormat.wFormatTag == WAVE_FORMAT_PCM && sourceFormat.wBitsPerSample != 8 &&
			sourceFormat.wBitsPerSample != 16 && sourceFormat.wBitsPerSample != 24 &&
			sourceFormat.wBitsPerSample != 32)
		{
			return false;
		}

		const size_t bytesPerSample = static_cast<size_t>(sourceFormat.wBitsPerSample / 8);
		const size_t sourceChannelBytes = static_cast<size_t>(sourceFormat.nChannels) * bytesPerSample;
		const size_t sourceFrameSize = static_cast<size_t>(sourceFormat.nBlockAlign);
		if (sourceFrameSize < sourceChannelBytes || sourceBytes.size() < sourceFrameSize)
			return false;

		std::vector<RPakWavStereoWeights_t> weights;
		if (!RPakWav_BuildStereoChannelWeights(sourceFormat.nChannels, channelMask, weights))
			return false;

		const size_t frameCount = sourceBytes.size() / sourceFrameSize;
		const size_t outputFrameSize = bytesPerSample * 2;
		outBytes.assign(frameCount * outputFrameSize, 0);
		std::vector<float> stereoSamples(frameCount * 2, 0.0f);
		float peakMagnitude = 0.0f;

		outFormat = sourceFormat;
		outFormat.nChannels = 2;
		outFormat.nBlockAlign = static_cast<WORD>(outputFrameSize);
		outFormat.nAvgBytesPerSec = outFormat.nSamplesPerSec * outFormat.nBlockAlign;
		outFormat.cbSize = 0;

		for (size_t frame = 0; frame < frameCount; ++frame)
		{
			const uint8_t* const sourceFrame = sourceBytes.data() + (frame * sourceFrameSize);
			float left = 0.0f;
			float right = 0.0f;
			for (uint16_t channel = 0; channel < sourceFormat.nChannels; ++channel)
			{
				const uint8_t* const sampleBytes = sourceFrame + (static_cast<size_t>(channel) * bytesPerSample);
				const float sample = RPakWav_ReadNormalizedSample(sampleBytes, sourceFormat);
				left += sample * weights[channel].left;
				right += sample * weights[channel].right;
			}

			stereoSamples[frame * 2] = left;
			stereoSamples[(frame * 2) + 1] = right;
			peakMagnitude = (std::max)(peakMagnitude, (std::max)(std::abs(left), std::abs(right)));
		}

		// Surround folds can legitimately sum above full scale even when every
		// source channel is valid. Normalize the cached mix once instead of
		// hard-clipping each gunshot transient during sample conversion.
		constexpr float RPAK_WAV_DOWNMIX_PEAK = 0.98f;
		const float normalizationGain = peakMagnitude > RPAK_WAV_DOWNMIX_PEAK ?
			RPAK_WAV_DOWNMIX_PEAK / peakMagnitude : 1.0f;
		for (size_t frame = 0; frame < frameCount; ++frame)
		{
			uint8_t* const outputFrame = outBytes.data() + (frame * outputFrameSize);
			RPakWav_WriteNormalizedSample(outputFrame, outFormat, stereoSamples[frame * 2] * normalizationGain);
			RPakWav_WriteNormalizedSample(outputFrame + bytesPerSample, outFormat,
				stereoSamples[(frame * 2) + 1] * normalizationGain);
		}

		return true;
	}

	std::shared_ptr<const RPakWavCachedWave_t> RPakWav_MixOneShotLayers(
		const std::vector<std::shared_ptr<const RPakWavCachedWave_t>>& layers, const char* const eventName)
	{
		if (layers.size() < 2 || !layers[0])
			return nullptr;

		const DWORD sampleRate = layers[0]->format.nSamplesPerSec;
		size_t frameCount = 0;
		for (const auto& layer : layers)
		{
			if (!layer || !RPakWav_IsSpatialFormatSupported(layer->format) ||
				layer->format.nSamplesPerSec != sampleRate || layer->format.nBlockAlign == 0 ||
				layer->format.nBlockAlign < layer->format.nChannels * (layer->format.wBitsPerSample / 8))
				return nullptr;
			frameCount = (std::max)(frameCount, layer->pcmBytes.size() / layer->format.nBlockAlign);
		}
		// Bound transient allocations. Different sample rates and long or looped
		// sources retain the existing independent-voice path.
		constexpr size_t maxMixBytes = 64u * 1024u * 1024u;
		constexpr size_t outputFrameSize = 2 * sizeof(float);
		if (frameCount == 0 || frameCount > maxMixBytes / outputFrameSize || sampleRate > UINT32_MAX / outputFrameSize)
			return nullptr;

		std::vector<float> mixedSamples(frameCount * 2, 0.0f);
		for (const auto& layer : layers)
		{
			const WAVEFORMATEX& format = layer->format;
			const size_t layerFrames = layer->pcmBytes.size() / format.nBlockAlign;
			const size_t sampleBytes = format.wBitsPerSample / 8;
			for (size_t frame = 0; frame < layerFrames; ++frame)
			{
				const uint8_t* const sourceFrame = layer->pcmBytes.data() + frame * format.nBlockAlign;
				const float left = RPakWav_ReadNormalizedSample(sourceFrame, format);
				const float right = format.nChannels == 1 ? left :
					RPakWav_ReadNormalizedSample(sourceFrame + sampleBytes, format);
				mixedSamples[frame * 2] += left;
				mixedSamples[frame * 2 + 1] += right;
			}
		}

		float peak = 0.0f;
		for (const float sample : mixedSamples)
		{
			if (!std::isfinite(sample))
				return nullptr;
			peak = (std::max)(peak, std::abs(sample));
		}
		// One constant gain preserves the balance and transient shape of all
		// selected lists. The usual event/menu/global gains are applied later.
		// At unit event volume and global half gain, two portal-end mixes peak
		// at <= 0.98 combined, before any other game audio is added.
		constexpr float mixPeak = 0.98f;
		const float headroomGain = peak > mixPeak ? mixPeak / peak : 1.0f;
		std::shared_ptr<RPakWavCachedWave_t> mix = std::make_shared<RPakWavCachedWave_t>();
		mix->format.wFormatTag = RPAK_WAV_FORMAT_IEEE_FLOAT;
		mix->format.nChannels = 2;
		mix->format.nSamplesPerSec = sampleRate;
		mix->format.nAvgBytesPerSec = sampleRate * static_cast<DWORD>(outputFrameSize);
		mix->format.nBlockAlign = static_cast<WORD>(outputFrameSize);
		mix->format.wBitsPerSample = 32;
		mix->channelMask = RPAK_WAV_SPEAKER_FRONT_LEFT | RPAK_WAV_SPEAKER_FRONT_RIGHT;
		mix->sampleRate = sampleRate;
		mix->channels = 2;
		mix->pcmBytes.resize(frameCount * outputFrameSize);
		for (size_t sample = 0; sample < mixedSamples.size(); ++sample)
		{
			const float value = mixedSamples[sample] * headroomGain;
			memcpy(mix->pcmBytes.data() + sample * sizeof(float), &value, sizeof(value));
		}
		if (RPakWav_DebugEnabled())
			Msg(eDLL_T::AUDIO, "Packed WAV layer mix event='%s' layers=%zu inputPeak=%.4f headroomGain=%.4f frames=%zu rate=%lu\n",
				eventName ? eventName : "<null>", layers.size(), peak, headroomGain, frameCount,
				static_cast<unsigned long>(sampleRate));
		return mix;
	}

	bool RPakWav_BuildVolumePlaybackBuffer(const RPakWavCachedWave_t& wave, const float gain,
		std::vector<uint8_t>& outBytes)
	{
		if (!RPakWav_IsSpatialFormatSupported(wave.format))
			return false;

		const uint16_t channels = wave.format.nChannels;
		const size_t bytesPerSample = static_cast<size_t>(wave.format.wBitsPerSample / 8);
		const size_t sourceBlockAlign = static_cast<size_t>(wave.format.nBlockAlign);
		const size_t channelBytes = static_cast<size_t>(channels) * bytesPerSample;
		if (channels == 0 || channels > 2 || bytesPerSample == 0 || sourceBlockAlign == 0 ||
			sourceBlockAlign < channelBytes || wave.pcmBytes.size() < sourceBlockAlign)
		{
			return false;
		}

		outBytes = wave.pcmBytes;
		const size_t frameLimit = (outBytes.size() / sourceBlockAlign) * sourceBlockAlign;
		for (size_t frameOffset = 0; frameOffset < frameLimit; frameOffset += sourceBlockAlign)
		{
			for (uint16_t channel = 0; channel < channels; ++channel)
			{
				uint8_t* const sampleBytes = outBytes.data() + frameOffset + (static_cast<size_t>(channel) * bytesPerSample);
				const float sample = RPakWav_ReadNormalizedSample(sampleBytes, wave.format);
				RPakWav_WriteNormalizedSample(sampleBytes, wave.format, sample * gain);
			}
		}

		return true;
	}

	bool RPakWav_PrepareSpatialPlaybackFormat(const RPakWavCachedWave_t& wave, WAVEFORMATEX& outFormat)
	{
		if (!RPakWav_IsSpatialFormatSupported(wave.format))
			return false;

		const size_t bytesPerSample = static_cast<size_t>(wave.format.wBitsPerSample / 8);
		const size_t sourceBlockAlign = static_cast<size_t>(wave.format.nBlockAlign);
		if (wave.format.nChannels == 0 || wave.format.nChannels > 2 || bytesPerSample == 0 ||
			sourceBlockAlign == 0 || wave.pcmBytes.size() < sourceBlockAlign)
		{
			return false;
		}

		if ((wave.pcmBytes.size() / sourceBlockAlign) == 0)
			return false;

		outFormat = wave.format;
		if (wave.format.nChannels == 1)
		{
			outFormat.nChannels = 2;
			outFormat.nBlockAlign = static_cast<WORD>(bytesPerSample * 2);
			outFormat.nAvgBytesPerSec = outFormat.nSamplesPerSec * outFormat.nBlockAlign;
		}

		return true;
	}

	size_t RPakWav_SpatialSourceFrameLimit(const RPakWavVoice_t& voice)
	{
		if (!voice.wave || voice.wave->format.nBlockAlign == 0)
			return 0;

		const size_t sourceBlockAlign = static_cast<size_t>(voice.wave->format.nBlockAlign);
		return (voice.wave->pcmBytes.size() / sourceBlockAlign) * sourceBlockAlign;
	}

	void RPakWav_GetSpatialChunkGains(const RPakWavVoice_t& voice, float& leftGain, float& rightGain)
	{
		leftGain = 1.0f;
		rightGain = 1.0f;
		if (voice.firstPersonPlayback)
			return;
		if (!RPakWav_CalculateSpatialGains(voice.spatial, leftGain, rightGain))
		{
			leftGain = 1.0f;
			rightGain = 1.0f;
		}
	}

	void RPakWav_ResolveEmitterPosition(RPakWavVoice_t& voice)
	{
		if (voice.mixerFirstPerson)
		{
			voice.spatial.valid = false;
			return;
		}
		if (voice.spatial.emitterEntity == UINT32_MAX) return;
		// Keep the last resolved position if the entity disappears during a
		// one-shot tail, rather than suddenly returning it to full center gain.
		const int entityNumber = static_cast<int32_t>(voice.spatial.emitterEntity);
		// Match the bounds used by the SDK's client-entity-list detour, including
		// negative client-only entity numbers. Never resolve on the audio worker.
		if (!g_pClientEntityList || entityNumber < -(MAX_EDICTS - 2) || entityNumber >= NUM_ENT_ENTRIES) return;
		const IClientEntity* const entity = g_pClientEntityList->GetClientEntity(entityNumber);
		if (!entity) return;
		const Vector3D origin = entity->GetAbsOrigin();
		if (!RPakWav_IsFiniteVector(origin)) return;
		voice.spatial.position = origin;
		voice.spatial.valid = true;
	}

	void RPakWav_CacheSpatialVoiceGains(RPakWavVoice_t& voice)
	{
		// Called on the engine thread. The service thread consumes these values
		// under the voices mutex without reading camera state or menu CVars.
		RPakWav_ResolveEmitterPosition(voice);
		RPakWav_GetSpatialChunkGains(voice, voice.spatialTargetLeftGain, voice.spatialTargetRightGain);
		const float voiceGain = RPakWav_GetVoiceSampleGain(voice);
		voice.spatialTargetLeftGain *= voiceGain;
		voice.spatialTargetRightGain *= voiceGain;
		float proximity = 10.0f;
		if (voice.spatial.valid && g_vecRenderOrigin && RPakWav_IsFiniteVector(MainViewOrigin()))
		{
			const float distance = std::sqrt((voice.spatial.position - MainViewOrigin()).LengthSqr());
			proximity = 20.0f / (1.0f + distance / 512.0f);
		}
		const float base = RPakWav_IsWeaponMixerGroup(voice.mixerGroup) ? 60.0f :
			(voice.mixerGroup == RPakWavMixerGroup_e::Critical ? 50.0f :
			(voice.mixerGroup == RPakWavMixerGroup_e::Abilities ? 30.0f : 20.0f));
		voice.mixerPriority = base + proximity + (voice.mixerFirstPerson ? 50.0f : 0.0f);
	}

	size_t RPakWav_FillSpatialChunk(RPakWavVoice_t& voice, RPakWavSpatialChunk_t& chunk)
	{
		if (!voice.wave || voice.spatialFinishedSubmitting.load(std::memory_order_acquire))
			return 0;

		const WAVEFORMATEX& sourceFormat = voice.wave->format;
		const size_t sourceFrameSize = static_cast<size_t>(sourceFormat.nBlockAlign);
		const size_t outputFrameSize = static_cast<size_t>(voice.playbackFormat.nBlockAlign);
		const size_t bytesPerSample = static_cast<size_t>(sourceFormat.wBitsPerSample / 8);
		const size_t sourceFrameLimit = RPakWav_SpatialSourceFrameLimit(voice);
		if (sourceFrameSize == 0 || outputFrameSize == 0 || bytesPerSample == 0 || sourceFrameLimit == 0)
		{
			voice.spatialFinishedSubmitting.store(true, std::memory_order_release);
			return 0;
		}

		const float startLeftGain = voice.spatialPreviousLeftGain;
		const float startRightGain = voice.spatialPreviousRightGain;
		const size_t rampFrames = (std::max)(static_cast<size_t>(1),
			(static_cast<size_t>(voice.playbackFormat.nSamplesPerSec) * RPAK_WAV_SPATIAL_GAIN_RAMP_MS) / 1000);

		const size_t targetFrames = (std::max)(static_cast<size_t>(1),
			(static_cast<size_t>(voice.playbackFormat.nSamplesPerSec) * RPAK_WAV_SPATIAL_CHUNK_MS) / 1000);
		chunk.bytes.assign(targetFrames * outputFrameSize, 0);

		size_t framesWritten = 0;
		while (framesWritten < targetFrames)
		{
			if (voice.spatialReadOffset >= sourceFrameLimit)
			{
				if (!voice.looping)
					break;

				voice.spatialReadOffset = 0;
			}

			const uint8_t* const inFrame = voice.wave->pcmBytes.data() + voice.spatialReadOffset;
			uint8_t* const outFrame = chunk.bytes.data() + (framesWritten * outputFrameSize);
			const float ramp = (std::min)(1.0f, static_cast<float>(framesWritten + 1) / static_cast<float>(rampFrames));
			const float leftGain = startLeftGain + (voice.spatialTargetLeftGain - startLeftGain) * ramp;
			const float rightGain = startRightGain + (voice.spatialTargetRightGain - startRightGain) * ramp;
			voice.spatialPreviousLeftGain = leftGain;
			voice.spatialPreviousRightGain = rightGain;

			if (sourceFormat.nChannels == 1)
			{
				const float sample = RPakWav_ReadNormalizedSample(inFrame, sourceFormat);
				RPakWav_WriteNormalizedSample(outFrame, voice.playbackFormat, sample * leftGain);
				RPakWav_WriteNormalizedSample(outFrame + bytesPerSample, voice.playbackFormat, sample * rightGain);
			}
			else
			{
				const float leftSample = RPakWav_ReadNormalizedSample(inFrame, sourceFormat);
				const float rightSample = RPakWav_ReadNormalizedSample(inFrame + bytesPerSample, sourceFormat);
				RPakWav_WriteNormalizedSample(outFrame, voice.playbackFormat, leftSample * leftGain);
				RPakWav_WriteNormalizedSample(outFrame + bytesPerSample, voice.playbackFormat, rightSample * rightGain);
			}

			voice.spatialReadOffset += sourceFrameSize;
			++framesWritten;
		}

		if (framesWritten == 0)
		{
			voice.spatialFinishedSubmitting.store(true, std::memory_order_release);
			chunk.bytes.clear();
			return 0;
		}

		chunk.bytes.resize(framesWritten * outputFrameSize);
		if (!voice.looping && voice.spatialReadOffset >= sourceFrameLimit)
			voice.spatialFinishedSubmitting.store(true, std::memory_order_release);

		return chunk.bytes.size();
	}

	void RPakWav_DecrementSpatialActiveChunks(RPakWavVoice_t& voice)
	{
		uint32_t currentCount = voice.spatialActiveChunks.load(std::memory_order_relaxed);
		while (currentCount != 0 &&
			!voice.spatialActiveChunks.compare_exchange_weak(currentCount, currentCount - 1, std::memory_order_relaxed))
		{
		}
	}

	bool RPakWav_SubmitSpatialChunk(RPakWavVoice_t& voice, RPakWavSpatialChunk_t& chunk)
	{
		if (!voice.waveOut || voice.closed || chunk.bytes.empty())
			return false;

		chunk.header = WAVEHDR{};
		chunk.header.lpData = reinterpret_cast<LPSTR>(chunk.bytes.data());
		chunk.header.dwBufferLength = static_cast<DWORD>(chunk.bytes.size());

		MMRESULT result = waveOutPrepareHeader(voice.waveOut, &chunk.header, sizeof(chunk.header));
		if (result != MMSYSERR_NOERROR)
		{
			RPakWav_ReportWaveOutFailure("spatial waveOutPrepareHeader", result, voice);
			return false;
		}

		chunk.prepared = true;
		chunk.queued.store(true, std::memory_order_release);
		voice.spatialActiveChunks.fetch_add(1, std::memory_order_relaxed);

		result = waveOutWrite(voice.waveOut, &chunk.header, sizeof(chunk.header));
		if (result != MMSYSERR_NOERROR)
		{
			RPakWav_ReportWaveOutFailure("spatial waveOutWrite", result, voice);
			RPakWav_DecrementSpatialActiveChunks(voice);
			chunk.queued.store(false, std::memory_order_release);
			waveOutUnprepareHeader(voice.waveOut, &chunk.header, sizeof(chunk.header));
			chunk.prepared = false;
			return false;
		}

		return true;
	}

	bool RPakWav_StartSpatialVoice(RPakWavVoice_t& voice)
	{
		RPakWav_CacheSpatialVoiceGains(voice);
		voice.spatialPreviousLeftGain = voice.spatialTargetLeftGain;
		voice.spatialPreviousRightGain = voice.spatialTargetRightGain;
		size_t submittedCount = 0;
		for (RPakWavSpatialChunk_t& chunk : voice.spatialChunks)
		{
			if (RPakWav_FillSpatialChunk(voice, chunk) == 0)
				break;

			if (!RPakWav_SubmitSpatialChunk(voice, chunk))
				return false;

			++submittedCount;
			if (voice.spatialFinishedSubmitting.load(std::memory_order_acquire))
				break;
		}

		if (submittedCount != 0 && RPakWav_DebugEnabled())
		{
			float leftGain = 1.0f;
			float rightGain = 1.0f;
			RPakWav_GetSpatialChunkGains(voice, leftGain, rightGain);
			const float voiceGain = RPakWav_GetVoiceSampleGain(voice);
			Msg(eDLL_T::AUDIO, "Packed WAV spatial event='%s' batch=%llu source=(%.1f %.1f %.1f) bus=%s gain=%.2f L=%.2f R=%.2f buffers=%zu chunkMs=%u\n",
				voice.eventName.c_str(), static_cast<unsigned long long>(voice.layerBatchId),
				voice.spatial.position.x, voice.spatial.position.y, voice.spatial.position.z,
				RPakWav_AudioBusDebugName(voice.audioBus), voiceGain, leftGain * voiceGain, rightGain * voiceGain,
				RPAK_WAV_SPATIAL_BUFFER_COUNT, RPAK_WAV_SPATIAL_CHUNK_MS);
		}

		return submittedCount != 0;
	}

	void RPakWav_UpdateSpatialVoiceGains()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (const auto& voice : s_rpakWavVoices)
			if (voice && !voice->done && !voice->closed && (voice->spatialStreaming || voice->sharedMixer))
				RPakWav_CacheSpatialVoiceGains(*voice);
	}

	void CALLBACK RPakWav_MixerCallback(HWAVEOUT, UINT message, DWORD_PTR, DWORD_PTR param1, DWORD_PTR)
	{
		if (message != WOM_DONE || !param1)
			return;
		WAVEHDR* const header = reinterpret_cast<WAVEHDR*>(param1);
		RPakWavMixerChunk_t* const chunk = reinterpret_cast<RPakWavMixerChunk_t*>(header->dwUser);
		if (!chunk)
			return;
		uint64_t completed = s_rpakWavMixerPlayedFrames.load(std::memory_order_relaxed);
		while (completed < chunk->endFrame && !s_rpakWavMixerPlayedFrames.compare_exchange_weak(
			completed, chunk->endFrame, std::memory_order_release, std::memory_order_relaxed)) {}
		if (s_rpakWavSpatialWakeEvent)
			SetEvent(s_rpakWavSpatialWakeEvent);
		chunk->queued.store(false, std::memory_order_release); // Last access before the header can be recycled.
	}

	bool RPakWav_OpenMixerLocked()
	{
		if (s_rpakWavMixerOutput)
			return true;
		WAVEFORMATEX format{};
		format.wFormatTag = RPAK_WAV_FORMAT_IEEE_FLOAT;
		format.nChannels = 2;
		format.nSamplesPerSec = static_cast<DWORD>(RPAK_WAV_MIXER_RATE);
		format.wBitsPerSample = 32;
		format.nBlockAlign = 2 * sizeof(float);
		format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
		MMRESULT result = waveOutOpen(&s_rpakWavMixerOutput, WAVE_MAPPER, &format,
			reinterpret_cast<DWORD_PTR>(&RPakWav_MixerCallback), 0, CALLBACK_FUNCTION);
		if (result != MMSYSERR_NOERROR)
		{
			s_rpakWavMixerOutput = nullptr;
			Warning(eDLL_T::AUDIO, "Packed WAV shared mixer could not open output (%u)\n", result);
			return false;
		}
		s_rpakWavMixerPaused = waveOutPause(s_rpakWavMixerOutput) == MMSYSERR_NOERROR;
		return true;
	}

	void RPakWav_ResetMixerDspLocked()
	{
		for (auto& group : s_rpakWavMixerGroups)
		{
			group.delay.fill(0.0f);
			group.peaks.clear();
			group.gain = 1.0f;
		}
	}

	void RPakWav_CloseMixerLocked()
	{
		if (!s_rpakWavMixerOutput)
			return;
		waveOutReset(s_rpakWavMixerOutput);
		for (auto& chunk : s_rpakWavMixerChunks)
		{
			if (chunk.prepared)
				waveOutUnprepareHeader(s_rpakWavMixerOutput, &chunk.header, sizeof(chunk.header));
			chunk.prepared = false;
			chunk.queued.store(false, std::memory_order_release);
		}
		waveOutClose(s_rpakWavMixerOutput);
		s_rpakWavMixerOutput = nullptr;
		s_rpakWavMixerPlayedFrames.store(0, std::memory_order_release);
		s_rpakWavMixerWrittenFrames = 0;
		s_rpakWavMixerNextChunk = 0;
		s_rpakWavMixerPaused = false;
		s_rpakWavMixerIdle = true;
		RPakWav_ResetMixerDspLocked();
	}

	void RPakWav_FillMixerChunkLocked(RPakWavMixerChunk_t& chunk)
	{
		chunk.samples.fill(0.0f);
		for (auto& group : s_rpakWavMixerGroups)
			group.samples.fill(0.0f);
		for (const auto& voicePtr : s_rpakWavVoices)
		{
			RPakWavVoice_t& voice = *voicePtr;
			if (!voice.sharedMixer || voice.mixerPending || voice.done || voice.closed || voice.mixerFinishFrame != 0)
				continue;
			const auto& format = voice.wave->format;
			const size_t sourceFrames = voice.wave->pcmBytes.size() / format.nBlockAlign;
			const size_t sampleBytes = format.wBitsPerSample / 8;
			const double step = static_cast<double>(format.nSamplesPerSec) * voice.scriptPitch / RPAK_WAV_MIXER_RATE;
			const float startLeft = voice.spatialPreviousLeftGain;
			const float startRight = voice.spatialPreviousRightGain;
			auto& samples = s_rpakWavMixerGroups[static_cast<size_t>(voice.mixerGroup)].samples;
			for (size_t frame = 0; frame < RPAK_WAV_MIXER_FRAMES; ++frame)
			{
				if (voice.mixerFading && voice.mixerFadeRemaining == 0)
				{
					voice.mixerFinishFrame = s_rpakWavMixerWrittenFrames + frame + RPAK_WAV_MIXER_LOOKAHEAD;
					break;
				}
				if (voice.mixerCursor >= static_cast<double>(sourceFrames))
				{
					if (!voice.looping)
					{
						voice.mixerFinishFrame = s_rpakWavMixerWrittenFrames + frame + RPAK_WAV_MIXER_LOOKAHEAD;
						break;
					}
					voice.mixerCursor = std::fmod(voice.mixerCursor, static_cast<double>(sourceFrames));
				}
				const size_t first = static_cast<size_t>(voice.mixerCursor);
				const size_t next = first + 1 < sourceFrames ? first + 1 : (voice.looping ? 0 : first);
				const float fraction = static_cast<float>(voice.mixerCursor - first);
				const uint8_t* const a = voice.wave->pcmBytes.data() + first * format.nBlockAlign;
				const uint8_t* const b = voice.wave->pcmBytes.data() + next * format.nBlockAlign;
				const float aLeft = RPakWav_ReadNormalizedSample(a, format);
				const float bLeft = RPakWav_ReadNormalizedSample(b, format);
				const float aRight = format.nChannels == 1 ? aLeft : RPakWav_ReadNormalizedSample(a + sampleBytes, format);
				const float bRight = format.nChannels == 1 ? bLeft : RPakWav_ReadNormalizedSample(b + sampleBytes, format);
				const float ramp = (std::min)(1.0f, static_cast<float>(frame + 1) /
					static_cast<float>(RPAK_WAV_MIXER_RATE * RPAK_WAV_SPATIAL_GAIN_RAMP_MS / 1000));
				voice.spatialPreviousLeftGain = startLeft + (voice.spatialTargetLeftGain - startLeft) * ramp;
				voice.spatialPreviousRightGain = startRight + (voice.spatialTargetRightGain - startRight) * ramp;
				const float fade = voice.mixerFading ? static_cast<float>(--voice.mixerFadeRemaining) /
					static_cast<float>(RPAK_WAV_MIXER_FADE_FRAMES) : 1.0f;
				samples[frame * 2] += (aLeft + (bLeft - aLeft) * fraction) * voice.spatialPreviousLeftGain * fade;
				samples[frame * 2 + 1] += (aRight + (bRight - aRight) * fraction) * voice.spatialPreviousRightGain * fade;
				voice.mixerCursor += step;
			}
		}

		// First-person weapons/dialogue/footsteps receive headroom first, then
		// remote weapons, then abilities/world. Remote gunfire cannot duck the
		// first-person weapon group, and ability spam cannot duck either group.
		// All groups share the same clock and 5 ms delay, retaining layer timing.
		constexpr float ceiling = 0.95f;
		const float release = 1.0f - std::exp(-1.0f / (0.1f * RPAK_WAV_MIXER_RATE));
		for (size_t frame = 0; frame < RPAK_WAV_MIXER_FRAMES; ++frame)
		{
			const uint64_t absoluteFrame = s_rpakWavMixerWrittenFrames + frame;
			const size_t delayIndex = static_cast<size_t>(absoluteFrame % RPAK_WAV_MIXER_LOOKAHEAD) * 2;
			std::array<float, RPAK_WAV_MIXER_GROUP_COUNT> peaks{};
			std::array<float, RPAK_WAV_MIXER_GROUP_COUNT * 2> delayed{};
			for (size_t index = 0; index < RPAK_WAV_MIXER_GROUP_COUNT; ++index)
			{
				auto& group = s_rpakWavMixerGroups[index];
				float left = group.samples[frame * 2], right = group.samples[frame * 2 + 1];
				if (!std::isfinite(left)) left = 0.0f;
				if (!std::isfinite(right)) right = 0.0f;
				const float peak = (std::max)(std::abs(left), std::abs(right));
				group.inputPeak = (std::max)(group.inputPeak, peak);
				while (!group.peaks.empty() && group.peaks.back().second <= peak)
					group.peaks.pop_back();
				group.peaks.emplace_back(absoluteFrame, peak);
				while (group.peaks.front().first + RPAK_WAV_MIXER_LOOKAHEAD < absoluteFrame)
					group.peaks.pop_front();
				peaks[index] = group.peaks.front().second;
				delayed[index * 2] = group.delay[delayIndex];
				delayed[index * 2 + 1] = group.delay[delayIndex + 1];
				group.delay[delayIndex] = left;
				group.delay[delayIndex + 1] = right;
			}
			const auto setGain = [&](const size_t index, const float target)
			{
				auto& group = s_rpakWavMixerGroups[index];
				group.gain = target < group.gain ? target : group.gain + (target - group.gain) * release;
				if (group.gain < 0.999f) ++group.limitedFrames;
			};
			const float priorityPeak = peaks[4] + peaks[2]; // First-person weapons + Critical.
			const float priorityTarget = priorityPeak > ceiling ? ceiling / priorityPeak : 1.0f;
			setGain(4, priorityTarget);
			setGain(2, priorityTarget);
			float remaining = (std::max)(0.0f, ceiling -
				peaks[4] * s_rpakWavMixerGroups[4].gain - peaks[2] * s_rpakWavMixerGroups[2].gain);
			const float weaponTarget = peaks[0] > remaining ? remaining / peaks[0] : 1.0f;
			setGain(0, weaponTarget);
			remaining = (std::max)(0.0f, remaining - peaks[0] * s_rpakWavMixerGroups[0].gain);
			const float otherPeak = peaks[1] + peaks[3];
			const float otherTarget = otherPeak > remaining ? remaining / otherPeak : 1.0f;
			setGain(1, otherTarget);
			setGain(3, otherTarget);
			for (size_t index = 0; index < RPAK_WAV_MIXER_GROUP_COUNT; ++index)
			{
				chunk.samples[frame * 2] += delayed[index * 2] * s_rpakWavMixerGroups[index].gain;
				chunk.samples[frame * 2 + 1] += delayed[index * 2 + 1] * s_rpakWavMixerGroups[index].gain;
			}
		}
		chunk.endFrame = s_rpakWavMixerWrittenFrames + RPAK_WAV_MIXER_FRAMES;
	}

	void RPakWav_RefillMixer()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		if (!s_rpakWavMixerOutput)
			return;
		const uint64_t played = s_rpakWavMixerPlayedFrames.load(std::memory_order_acquire);
		for (const auto& voice : s_rpakWavVoices)
			if (voice->sharedMixer && voice->mixerFinishFrame != 0 && played >= voice->mixerFinishFrame)
				voice->done.store(true, std::memory_order_release);
		const bool active = std::any_of(s_rpakWavVoices.begin(), s_rpakWavVoices.end(),
			[](const auto& voice) { return voice->sharedMixer && !voice->mixerPending && !voice->done && !voice->closed; });
		const bool drained = std::none_of(s_rpakWavMixerChunks.begin(), s_rpakWavMixerChunks.end(),
			[](const auto& chunk) { return chunk.queued.load(std::memory_order_acquire); });
		if (!active)
		{
			if (drained)
			{
				RPakWav_ResetMixerDspLocked();
				s_rpakWavMixerIdle = true;
			}
			return;
		}
		if (drained && !s_rpakWavMixerIdle && !s_rpakWavMixerPaused)
			++s_rpakWavMixerUnderruns;
		s_rpakWavMixerIdle = false;
		if (drained)
			s_rpakWavMixerPaused = waveOutPause(s_rpakWavMixerOutput) == MMSYSERR_NOERROR;
		for (size_t i = 0; i < s_rpakWavMixerChunks.size(); ++i)
		{
			auto& chunk = s_rpakWavMixerChunks[s_rpakWavMixerNextChunk];
			if (chunk.queued.load(std::memory_order_acquire))
				break;
			RPakWav_FillMixerChunkLocked(chunk);
			if (!chunk.prepared)
			{
				chunk.header = WAVEHDR{};
				chunk.header.lpData = reinterpret_cast<LPSTR>(chunk.samples.data());
				chunk.header.dwBufferLength = static_cast<DWORD>(sizeof(chunk.samples));
				chunk.header.dwUser = reinterpret_cast<DWORD_PTR>(&chunk);
				chunk.prepared = waveOutPrepareHeader(s_rpakWavMixerOutput, &chunk.header, sizeof(chunk.header)) == MMSYSERR_NOERROR;
			}
			chunk.queued.store(true, std::memory_order_release);
			if (!chunk.prepared || waveOutWrite(s_rpakWavMixerOutput, &chunk.header, sizeof(chunk.header)) != MMSYSERR_NOERROR)
			{
				chunk.queued.store(false, std::memory_order_release);
				for (const auto& voice : s_rpakWavVoices)
					if (voice->sharedMixer) voice->done.store(true, std::memory_order_release);
				Warning(eDLL_T::AUDIO, "Packed WAV shared mixer submission failed; retiring active mixer voices\n");
				RPakWav_CloseMixerLocked();
				return;
			}
			s_rpakWavMixerWrittenFrames = chunk.endFrame;
			s_rpakWavMixerNextChunk = (s_rpakWavMixerNextChunk + 1) % s_rpakWavMixerChunks.size();
		}
		if (s_rpakWavMixerPaused)
		{
			if (waveOutRestart(s_rpakWavMixerOutput) == MMSYSERR_NOERROR)
				s_rpakWavMixerPaused = false;
			else
			{
				for (const auto& voice : s_rpakWavVoices)
					if (voice->sharedMixer) voice->done.store(true, std::memory_order_release);
				RPakWav_CloseMixerLocked();
			}
		}
	}

	void RPakWav_RefillSpatialVoices()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (const std::unique_ptr<RPakWavVoice_t>& voice : s_rpakWavVoices)
		{
			if (!voice || voice->done || voice->closed || !voice->spatialStreaming)
				continue;

			const bool drained = !voice->spatialFinishedSubmitting.load(std::memory_order_acquire) &&
				voice->spatialActiveChunks.load(std::memory_order_acquire) == 0;
			bool submitted = false;
			for (RPakWavSpatialChunk_t& chunk : voice->spatialChunks)
			{
				if (chunk.queued.load(std::memory_order_acquire))
					continue;

				if (chunk.prepared)
				{
					if (waveOutUnprepareHeader(voice->waveOut, &chunk.header, sizeof(chunk.header)) != MMSYSERR_NOERROR)
						continue;

					chunk.prepared = false;
				}

				if (voice->spatialFinishedSubmitting.load(std::memory_order_acquire))
					continue;

				if (RPakWav_FillSpatialChunk(*voice, chunk) == 0)
					break;

				if (!RPakWav_SubmitSpatialChunk(*voice, chunk))
				{
					voice->done.store(true, std::memory_order_release);
					break;
				}
				submitted = true;
			}

			if (drained && submitted)
			{
				++voice->spatialUnderruns;
				if (RPakWav_DebugEnabled())
					Warning(eDLL_T::AUDIO, "Packed WAV spatial underrun event='%s' recovery=%u\n",
						voice->eventName.c_str(), voice->spatialUnderruns);
			}

			// Only the service thread decides completion after submitting all
			// prepared data. A callback must not finish a voice while its final
			// chunk is still being filled or submitted.
			if (voice->spatialFinishedSubmitting.load(std::memory_order_acquire) &&
				voice->spatialActiveChunks.load(std::memory_order_acquire) == 0 &&
				std::none_of(voice->spatialChunks.begin(), voice->spatialChunks.end(),
					[](const RPakWavSpatialChunk_t& chunk) { return chunk.queued.load(std::memory_order_acquire); }))
			{
				voice->done.store(true, std::memory_order_release);
			}
		}
	}

	DWORD WINAPI RPakWav_SpatialServiceThread(LPVOID)
	{
		while (!s_rpakWavSpatialServiceStopping.load(std::memory_order_acquire))
		{
			WaitForSingleObject(s_rpakWavSpatialWakeEvent, 5);
			if (!s_rpakWavSpatialServiceStopping.load(std::memory_order_acquire))
			{
				RPakWav_RefillMixer();
				RPakWav_RefillSpatialVoices();
			}
		}
		return 0;
	}

	bool RPakWav_StartSpatialService()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavSpatialServiceMutex);
		if (s_rpakWavSpatialThread)
			return true;
		s_rpakWavSpatialWakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!s_rpakWavSpatialWakeEvent)
			return false;
		s_rpakWavSpatialServiceStopping.store(false, std::memory_order_release);
		s_rpakWavSpatialThread = CreateThread(nullptr, 0, &RPakWav_SpatialServiceThread, nullptr, 0, nullptr);
		if (!s_rpakWavSpatialThread)
		{
			CloseHandle(s_rpakWavSpatialWakeEvent);
			s_rpakWavSpatialWakeEvent = nullptr;
			return false;
		}
		SetThreadPriority(s_rpakWavSpatialThread, THREAD_PRIORITY_ABOVE_NORMAL);
		return true;
	}

	void RPakWav_StopSpatialService()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavSpatialServiceMutex);
		if (!s_rpakWavSpatialThread)
			return;
		s_rpakWavSpatialServiceStopping.store(true, std::memory_order_release);
		SetEvent(s_rpakWavSpatialWakeEvent);
		WaitForSingleObject(s_rpakWavSpatialThread, INFINITE);
		CloseHandle(s_rpakWavSpatialThread);
		s_rpakWavSpatialThread = nullptr;
		// The event remains valid until every WinMM voice has been closed.
	}

	bool RPakWav_FindLoadedAssetByHeader(const void* const header, PakAssetShort_s*& outLoadedAsset, PakLoadedInfo_s*& outLoadedPak)
	{
		if (!g_pakGlobals || !header)
			return false;

		for (int i = 0; i < PAK_MAX_LOADED_ASSETS; ++i)
		{
			PakAssetShort_s& loadedAsset = g_pakGlobals->loadedAssets[i];
			if (loadedAsset.head != header)
				continue;

			if (loadedAsset.trackerIndex >= PAK_MAX_TRACKED_ASSETS)
				return false;

			const PakAssetTracker_s& tracker = g_pakGlobals->trackedAssets[loadedAsset.trackerIndex];
			if (tracker.loadedPakIndex < 0 || tracker.loadedPakIndex >= PAK_MAX_LOADED_PAKS)
				return false;

			PakLoadedInfo_s& loadedPak = g_pakGlobals->loadedPaks[tracker.loadedPakIndex];
			if (loadedPak.status != PAK_STATUS_LOADED || !loadedPak.pakFile)
				return false;

			outLoadedAsset = &loadedAsset;
			outLoadedPak = &loadedPak;
			return true;
		}

		return false;
	}

	bool RPakWav_IsSourceHeader(const RPakWavAudioSourceHeader_v1_t* const source)
	{
		return source && source->magic == RPAK_WAV_ASRC_MAGIC && source->version == RPAK_WAV_SOURCE_VERSION;
	}

	bool RPakWav_IsControlMode(const uint32_t mode)
	{
		return mode == RPAK_WAV_EVENT_MODE_STOP_EVENTS ||
			mode == RPAK_WAV_EVENT_MODE_STOP_MUSIC ||
			mode == RPAK_WAV_EVENT_MODE_STOP_ALL ||
			mode == RPAK_WAV_EVENT_MODE_STOP_MANAGED;
	}

	bool RPakWav_IsEventHeader(const RPakWavAudioEventHeader_v2_t* const event)
	{
		return event &&
			event->magic == RPAK_WAV_AEVT_MAGIC &&
			(event->version == RPAK_WAV_EVENT_VERSION || event->version == RPAK_WAV_EVENT_GROUP_VERSION) &&
			(event->sourceCount != 0 || RPakWav_IsControlMode(event->mode));
	}

	bool RPakWav_LoadedPakIsUsable(PakLoadedInfo_s* const loadedPak)
	{
		if (!loadedPak)
			return false;

		switch (loadedPak->status)
		{
		case PAK_STATUS_FREED:
		case PAK_STATUS_UNLOAD_PENDING:
		case PAK_STATUS_FREE_PENDING:
		case PAK_STATUS_CANCELING:
		case PAK_STATUS_ERROR:
		case PAK_STATUS_INVALID_PAKHANDLE:
			return false;
		default:
			return true;
		}
	}

	uint32_t RPakWav_NextRandom()
	{
		uint32_t state = s_rpakWavRandomState.load(std::memory_order_relaxed);
		if (state == 0)
		{
			uint32_t seed = GetTickCount() ^ 0xA511E9B3u;
			if (seed == 0)
				seed = 0xA511E9B3u;

			uint32_t expected = 0;
			s_rpakWavRandomState.compare_exchange_strong(expected, seed, std::memory_order_relaxed);
			state = s_rpakWavRandomState.load(std::memory_order_relaxed);
		}

		for (;;)
		{
			uint32_t next = state;
			next ^= next << 13;
			next ^= next >> 17;
			next ^= next << 5;
			if (next == 0)
				next = 0xA511E9B3u;

			if (s_rpakWavRandomState.compare_exchange_weak(state, next, std::memory_order_relaxed))
				return next;
		}
	}

	std::string RPakWav_LowerEventName(const char* const eventName)
	{
		std::string lowerName = eventName ? eventName : "";
		std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](const unsigned char c)
		{
			return static_cast<char>(std::tolower(c));
		});

		return lowerName;
	}

	bool RPakWav_NameContainsAny(const std::string& name, const std::initializer_list<const char*> tokens)
	{
		for (const char* const token : tokens)
		{
			if (name.find(token) != std::string::npos)
				return true;
		}

		return false;
	}

	bool RPakWav_ParseQuotedWeaponSettingLine(const std::string& line, std::string& outKey, std::string& outValue)
	{
		const size_t firstNonSpace = line.find_first_not_of(" \t\r\n");
		if (firstNonSpace == std::string::npos || line.compare(firstNonSpace, 2, "//") == 0)
			return false;

		const size_t keyStart = line.find('"', firstNonSpace);
		if (keyStart == std::string::npos)
			return false;

		const size_t keyEnd = line.find('"', keyStart + 1);
		if (keyEnd == std::string::npos)
			return false;

		const size_t valueStart = line.find('"', keyEnd + 1);
		if (valueStart == std::string::npos)
			return false;

		const size_t valueEnd = line.find('"', valueStart + 1);
		if (valueEnd == std::string::npos)
			return false;

		outKey.assign(line.data() + keyStart + 1, keyEnd - keyStart - 1);
		outValue.assign(line.data() + valueStart + 1, valueEnd - valueStart - 1);
		return true;
	}

	int RPakWav_WeaponLoopChannelIndex(const std::string& suffix)
	{
		if (suffix == "1p")
			return 0;
		if (suffix == "3p")
			return 1;
		if (suffix == "npc")
			return 2;

		return -1;
	}

	bool RPakWav_IsWeaponLoopMiddleKey(const std::string& key, std::string& outSuffix)
	{
		constexpr char prefix[] = "burst_or_looping_fire_sound_middle_";
		constexpr size_t prefixLen = sizeof(prefix) - 1;
		if (key.compare(0, prefixLen, prefix) != 0)
			return false;

		outSuffix = key.substr(prefixLen);
		return true;
	}

	bool RPakWav_IsWeaponLoopEndKey(const std::string& key, std::string& outSuffix)
	{
		constexpr char prefix[] = "burst_or_looping_fire_sound_end_";
		constexpr size_t prefixLen = sizeof(prefix) - 1;
		if (key.compare(0, prefixLen, prefix) != 0)
			return false;

		outSuffix = key.substr(prefixLen);
		return true;
	}

	uint64_t RPakWav_FireRateToIntervalMs(const float fireRate)
	{
		const float clampedFireRate = (fireRate > 0.01f && fireRate < 1000.0f) ? fireRate : RPAK_WAV_DEFAULT_WEAPON_FIRE_RATE;
		uint64_t intervalMs = static_cast<uint64_t>((1000.0f / clampedFireRate) + 0.5f);
		if (intervalMs < 15)
			intervalMs = 15;

		return intervalMs;
	}

	float RPakWav_ParseWeaponFireRateValue(const std::string& value, const float currentFireRate)
	{
		if (value.empty())
			return currentFireRate;

		const char* const text = value.c_str();
		char* end = nullptr;
		if (text[0] == '*')
		{
			const float multiplier = std::strtof(text + 1, &end);
			if (end != text + 1 && multiplier > 0.0f)
				return currentFireRate * multiplier;

			return currentFireRate;
		}

		const float parsed = std::strtof(text, &end);
		if (end != text && parsed > 0.0f)
			return parsed;

		return currentFireRate;
	}

	struct RPakWavWeaponLoopBlockState_t
	{
		std::array<std::string, 3> currentMiddleEvents;
		std::array<std::string, 3> currentEndEvents;
		std::array<bool, 3> explicitMiddleEvents;
		float currentFireRate;

		RPakWavWeaponLoopBlockState_t()
			: currentMiddleEvents(),
			  currentEndEvents(),
			  explicitMiddleEvents(),
			  currentFireRate(RPAK_WAV_DEFAULT_WEAPON_FIRE_RATE)
		{
		}
	};

	void RPakWav_PushWeaponLoopBlock(std::vector<RPakWavWeaponLoopBlockState_t>& blockStack)
	{
		RPakWavWeaponLoopBlockState_t childBlock = blockStack.back();
		childBlock.explicitMiddleEvents.fill(false);
		blockStack.push_back(childBlock);
	}

	void RPakWav_CountWeaponSettingBraces(const std::string& line, int& outOpenBraces, int& outCloseBraces)
	{
		outOpenBraces = 0;
		outCloseBraces = 0;

		bool inQuote = false;
		for (size_t i = 0; i < line.size(); ++i)
		{
			const char ch = line[i];
			if (!inQuote && ch == '/' && i + 1 < line.size() && line[i + 1] == '/')
				break;

			if (ch == '"' && (i == 0 || line[i - 1] != '\\'))
			{
				inQuote = !inQuote;
				continue;
			}

			if (inQuote)
				continue;

			if (ch == '{')
				++outOpenBraces;
			else if (ch == '}')
				++outCloseBraces;
		}
	}

	void RPakWav_AddWeaponLoopEvent(std::vector<RPakWavWeaponLoopEvent_t>& loopEvents,
		const std::string& eventName, const float fireRate, const int blockDepth)
	{
		if (eventName.empty())
			return;

		const PakGuid_t eventGuid = Pak_StringToGuid(eventName.c_str());
		for (RPakWavWeaponLoopEvent_t& existingEvent : loopEvents)
		{
			if (existingEvent.loopEventGuid == eventGuid)
			{
				if (blockDepth >= existingEvent.blockDepth)
				{
					existingEvent.loopEventName = eventName;
					existingEvent.fireRate = fireRate;
					existingEvent.intervalMs = RPakWav_FireRateToIntervalMs(fireRate);
					existingEvent.blockDepth = blockDepth;
				}
				return;
			}
		}

		loopEvents.push_back({ eventGuid, eventName, fireRate, RPakWav_FireRateToIntervalMs(fireRate), blockDepth });
	}

	void RPakWav_AddWeaponLoopStop(std::vector<RPakWavWeaponLoopStop_t>& loopStops,
		const std::string& stopEventName, const std::string& loopEventName)
	{
		if (stopEventName.empty() || loopEventName.empty())
			return;

		const PakGuid_t stopEventGuid = Pak_StringToGuid(stopEventName.c_str());
		const PakGuid_t loopEventGuid = Pak_StringToGuid(loopEventName.c_str());

		for (const RPakWavWeaponLoopStop_t& existingStop : loopStops)
		{
			if (existingStop.stopEventGuid == stopEventGuid && existingStop.loopEventGuid == loopEventGuid)
				return;
		}

		loopStops.push_back({ stopEventGuid, loopEventGuid, stopEventName, loopEventName });
	}

	void RPakWav_RegisterWeaponLoopBlock(const RPakWavWeaponLoopBlockState_t& block, const int blockDepth,
		std::vector<RPakWavWeaponLoopEvent_t>& loopEvents, std::vector<RPakWavWeaponLoopStop_t>& loopStops)
	{
		for (size_t channelIndex = 0; channelIndex < block.currentMiddleEvents.size(); ++channelIndex)
		{
			if (!block.explicitMiddleEvents[channelIndex])
				continue;

			const std::string& middleEvent = block.currentMiddleEvents[channelIndex];
			if (middleEvent.empty())
				continue;

			RPakWav_AddWeaponLoopEvent(loopEvents, middleEvent, block.currentFireRate, blockDepth);
			if (!block.currentEndEvents[channelIndex].empty())
				RPakWav_AddWeaponLoopStop(loopStops, block.currentEndEvents[channelIndex], middleEvent);
		}
	}

	void RPakWav_ParseWeaponLoopFile(const std::filesystem::path& path,
		std::vector<RPakWavWeaponLoopEvent_t>& loopEvents, std::vector<RPakWavWeaponLoopStop_t>& loopStops,
		std::unordered_set<PakGuid_t>& weaponSounds)
	{
		std::ifstream file(path);
		if (!file)
			return;

		std::vector<RPakWavWeaponLoopBlockState_t> blockStack;
		blockStack.emplace_back();

		std::string line;
		std::string key;
		std::string value;
		std::string suffix;
		const bool weaponFile = path.filename().string().rfind("mp_weapon_", 0) == 0;
		while (std::getline(file, line))
		{
			int openBraces = 0;
			int closeBraces = 0;
			RPakWav_CountWeaponSettingBraces(line, openBraces, closeBraces);
			for (int i = 0; i < closeBraces && blockStack.size() > 1; ++i)
			{
				const int blockDepth = static_cast<int>(blockStack.size()) - 1;
				RPakWav_RegisterWeaponLoopBlock(blockStack.back(), blockDepth, loopEvents, loopStops);
				blockStack.pop_back();
			}

			if (!RPakWav_ParseQuotedWeaponSettingLine(line, key, value))
			{
				for (int i = 0; i < openBraces; ++i)
					RPakWav_PushWeaponLoopBlock(blockStack);

				continue;
			}

			RPakWavWeaponLoopBlockState_t& currentBlock = blockStack.back();
			if (weaponFile && key.find("sound") != std::string::npos && !value.empty())
				weaponSounds.insert(Pak_StringToGuid(value.c_str()));

			if (key == "fire_rate")
			{
				currentBlock.currentFireRate = RPakWav_ParseWeaponFireRateValue(value, currentBlock.currentFireRate);
				for (int i = 0; i < openBraces; ++i)
					RPakWav_PushWeaponLoopBlock(blockStack);

				continue;
			}

			if (RPakWav_IsWeaponLoopMiddleKey(key, suffix))
			{
				const int channelIndex = RPakWav_WeaponLoopChannelIndex(suffix);
				if (channelIndex < 0)
					continue;

				currentBlock.currentMiddleEvents[static_cast<size_t>(channelIndex)] = value;
				currentBlock.explicitMiddleEvents[static_cast<size_t>(channelIndex)] = !value.empty();
				for (int i = 0; i < openBraces; ++i)
					RPakWav_PushWeaponLoopBlock(blockStack);

				continue;
			}

			if (RPakWav_IsWeaponLoopEndKey(key, suffix))
			{
				const int channelIndex = RPakWav_WeaponLoopChannelIndex(suffix);
				if (channelIndex < 0)
					continue;

				currentBlock.currentEndEvents[static_cast<size_t>(channelIndex)] = value;
				for (int i = 0; i < openBraces; ++i)
					RPakWav_PushWeaponLoopBlock(blockStack);

				continue;
			}

			for (int i = 0; i < openBraces; ++i)
				RPakWav_PushWeaponLoopBlock(blockStack);
		}

		while (!blockStack.empty())
		{
			const int blockDepth = static_cast<int>(blockStack.size()) - 1;
			RPakWav_RegisterWeaponLoopBlock(blockStack.back(), blockDepth, loopEvents, loopStops);
			blockStack.pop_back();
		}
	}

	bool RPakWav_FindWeaponLoopDirectory(std::filesystem::path& outWeaponDir)
	{
		const std::array<std::filesystem::path, 2> candidateDirs = {
			std::filesystem::path("platform") / "scripts" / "weapons",
			std::filesystem::path("game") / "platform" / "scripts" / "weapons"
		};

		for (const std::filesystem::path& weaponDir : candidateDirs)
		{
			std::error_code ec;
			if (!std::filesystem::is_directory(weaponDir, ec))
				continue;

			outWeaponDir = weaponDir;
			return true;
		}

		return false;
	}

	std::filesystem::file_time_type RPakWav_GetNewestWeaponLoopWriteTime(const std::filesystem::path& weaponDir)
	{
		std::filesystem::file_time_type newestWriteTime{};
		std::error_code ec;
		for (std::filesystem::directory_iterator it(weaponDir, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_regular_file(ec) || it->path().extension() != ".txt")
				continue;

			std::error_code timeEc;
			const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(it->path(), timeEc);
			if (!timeEc && writeTime > newestWriteTime)
				newestWriteTime = writeTime;
		}

		return newestWriteTime;
	}

	void RPakWav_RebuildWeaponLoopEventCache(const std::filesystem::path& weaponDir,
		const std::filesystem::file_time_type newestWriteTime)
	{
		std::vector<RPakWavWeaponLoopEvent_t> loopEvents;
		std::vector<RPakWavWeaponLoopStop_t> loopStops;
		std::unordered_set<PakGuid_t> weaponSounds;

		std::error_code ec;
		for (std::filesystem::directory_iterator it(weaponDir, ec), end; !ec && it != end; it.increment(ec))
		{
			if (it->is_regular_file(ec) && it->path().extension() == ".txt")
				RPakWav_ParseWeaponLoopFile(it->path(), loopEvents, loopStops, weaponSounds);
		}

		const size_t loopEventCount = loopEvents.size();
		const size_t loopStopCount = loopStops.size();
		{
			std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopEventCacheMutex);
			s_rpakWavWeaponLoopEvents = std::move(loopEvents);
			s_rpakWavWeaponLoopStops = std::move(loopStops);
			s_rpakWavWeaponSoundEvents = std::move(weaponSounds);
			s_rpakWavWeaponLoopEventCacheBuilt = true;
			s_rpakWavWeaponLoopEventCacheNewestWriteTime = newestWriteTime;
			s_rpakWavWeaponLoopEventCacheNextCheckMs = GetTickCount64() + RPAK_WAV_WEAPON_LOOP_CACHE_REFRESH_INTERVAL_MS;
		}

		if (RPakWav_DebugEnabled())
		{
			Msg(eDLL_T::AUDIO, "Registered %zu packed WAV weapon loop aliases and %zu loop stop aliases from weapon txt\n",
				loopEventCount, loopStopCount);
		}
	}

	void RPakWav_EnsureWeaponLoopEventCache()
	{
		const uint64_t nowMs = GetTickCount64();
		{
			std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopEventCacheMutex);
			if (s_rpakWavWeaponLoopEventCacheBuilt && nowMs < s_rpakWavWeaponLoopEventCacheNextCheckMs)
				return;

			s_rpakWavWeaponLoopEventCacheNextCheckMs = nowMs + RPAK_WAV_WEAPON_LOOP_CACHE_REFRESH_INTERVAL_MS;
		}

		std::filesystem::path weaponDir;
		if (!RPakWav_FindWeaponLoopDirectory(weaponDir))
			return;

		const std::filesystem::file_time_type newestWriteTime = RPakWav_GetNewestWeaponLoopWriteTime(weaponDir);
		{
			std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopEventCacheMutex);
			if (s_rpakWavWeaponLoopEventCacheBuilt && newestWriteTime <= s_rpakWavWeaponLoopEventCacheNewestWriteTime)
				return;
		}

		RPakWav_RebuildWeaponLoopEventCache(weaponDir, newestWriteTime);
	}

	bool RPakWav_IsWeaponLoopEventName(const char* const eventName)
	{
		if (!eventName || !*eventName)
			return false;

		RPakWav_EnsureWeaponLoopEventCache();

		const PakGuid_t eventGuid = Pak_StringToGuid(eventName);
		std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopEventCacheMutex);
		for (const RPakWavWeaponLoopEvent_t& loopEvent : s_rpakWavWeaponLoopEvents)
		{
			if (loopEvent.loopEventGuid == eventGuid)
				return true;
		}

		return false;
	}

	uint64_t RPakWav_GetWeaponLoopIntervalMs(const PakGuid_t eventGuid)
	{
		RPakWav_EnsureWeaponLoopEventCache();

		std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopEventCacheMutex);
		for (const RPakWavWeaponLoopEvent_t& loopEvent : s_rpakWavWeaponLoopEvents)
		{
			if (loopEvent.loopEventGuid == eventGuid)
				return loopEvent.intervalMs;
		}

		return RPakWav_FireRateToIntervalMs(RPAK_WAV_DEFAULT_WEAPON_FIRE_RATE);
	}

	bool RPakWav_IsManualMilesPlayActive()
	{
		return s_rpakWavManualMilesPlayDepth > 0;
	}

	void RPakWav_StopVoicesByEvent(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* spatial = nullptr);
	size_t RPakWav_StopLoopingVoicesByEvent(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* spatial = nullptr);
	size_t RPakWav_StopWeaponLoopRepeatersByEvent(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* spatial = nullptr);

	size_t RPakWav_RemoveQueuedPlayRequestsByEvent(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* spatial = nullptr)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavPlayQueueMutex);
		const size_t oldSize = s_rpakWavPlayQueue.size();
		s_rpakWavPlayQueue.erase(
			std::remove_if(s_rpakWavPlayQueue.begin(), s_rpakWavPlayQueue.end(),
				[eventGuid, spatial](const RPakWavPlayRequest_t& request)
				{
					return Pak_StringToGuid(request.eventName.c_str()) == eventGuid &&
						(!spatial || RPakWav_SameEmitter(request.spatial, *spatial));
				}),
			s_rpakWavPlayQueue.end());

		return oldSize - s_rpakWavPlayQueue.size();
	}

	size_t RPakWav_RemovePendingEventsByEvent(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* spatial = nullptr)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavPendingEventsMutex);
		const size_t oldSize = s_rpakWavPendingEvents.size();
		s_rpakWavPendingEvents.erase(
			std::remove_if(s_rpakWavPendingEvents.begin(), s_rpakWavPendingEvents.end(),
				[eventGuid, spatial](const RPakWavPendingEvent_t& pending)
				{
					return Pak_StringToGuid(pending.eventName.c_str()) == eventGuid &&
						(!spatial || RPakWav_SameEmitter(pending.spatial, *spatial));
				}),
			s_rpakWavPendingEvents.end());

		return oldSize - s_rpakWavPendingEvents.size();
	}

	void RPakWav_StopWeaponLoopVoicesForStopEvent(const char* const eventName, const RPakWavSpatialInfo_t& spatial)
	{
		if (!eventName || !*eventName)
			return;

		RPakWav_EnsureWeaponLoopEventCache();

		const PakGuid_t stopEventGuid = Pak_StringToGuid(eventName);
		std::vector<RPakWavWeaponLoopStop_t> matchingStops;
		{
			std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopEventCacheMutex);
			for (const RPakWavWeaponLoopStop_t& stop : s_rpakWavWeaponLoopStops)
			{
				if (stop.stopEventGuid == stopEventGuid)
					matchingStops.push_back(stop);
			}
		}

		for (const RPakWavWeaponLoopStop_t& stop : matchingStops)
		{
			if (RPakWav_DebugEnabled())
				Msg(eDLL_T::AUDIO, "Stopping packed WAV weapon loop '%s' from stop event '%s'\n",
					stop.loopEventName.c_str(), eventName);

			const size_t pendingCount = RPakWav_RemovePendingEventsByEvent(stop.loopEventGuid, &spatial);
			const size_t queuedCount = RPakWav_RemoveQueuedPlayRequestsByEvent(stop.loopEventGuid, &spatial);
			const size_t repeaterCount = RPakWav_StopWeaponLoopRepeatersByEvent(stop.loopEventGuid, &spatial);
			const size_t loopingVoiceCount = RPakWav_StopLoopingVoicesByEvent(stop.loopEventGuid, &spatial);
			if (RPakWav_DebugEnabled() && (queuedCount != 0 || pendingCount != 0 || repeaterCount != 0 || loopingVoiceCount != 0))
			{
				Msg(eDLL_T::AUDIO, "Canceled %zu repeaters, %zu queued, %zu pending and %zu looping packed WAV voices for '%s'; active one-shots will drain\n",
					repeaterCount, queuedCount, pendingCount, loopingVoiceCount, stop.loopEventName.c_str());
			}
		}
	}

	bool RPakWav_EventNameLooksLikeMusic(const char* const eventName)
	{
		const std::string lowerName = RPakWav_LowerEventName(eventName);
		return RPakWav_NameContainsAny(lowerName, { "music", "musicpack", "mus_" });
	}

	bool RPakWav_EventNameLooksLikeLobbyMusic(const char* const eventName)
	{
		const std::string lowerName = RPakWav_LowerEventName(eventName);
		return RPakWav_NameContainsAny(lowerName, { "lobby", "menu_music", "mainmenu", "main_menu" });
	}

	bool RPakWav_EventNameLooksLikeDialogue(const char* const eventName)
	{
		const std::string lowerName = RPakWav_LowerEventName(eventName);
		return RPakWav_NameContainsAny(lowerName, { "diag_", "dialog", "dialogue", "announcer", "voice_line", "vo_" });
	}

	RPakWavAudioBus_e RPakWav_GetAudioBusForEvent(const char* const eventName, const uint32_t eventFlags)
	{
		if ((eventFlags & RPAK_WAV_EVENT_FLAG_MUSIC) || RPakWav_EventNameLooksLikeMusic(eventName))
		{
			if (RPakWav_EventNameLooksLikeLobbyMusic(eventName))
				return RPakWavAudioBus_e::MusicLobby;

			return RPakWavAudioBus_e::MusicGame;
		}

		if (RPakWav_EventNameLooksLikeDialogue(eventName))
			return RPakWavAudioBus_e::Dialogue;

		return RPakWavAudioBus_e::Sfx;
	}

	const char* RPakWav_MixerGroupName(const RPakWavMixerGroup_e group)
	{
		switch (group)
		{
		case RPakWavMixerGroup_e::Weapons: return "weapons";
		case RPakWavMixerGroup_e::FirstPersonWeapons: return "first-person-weapons";
		case RPakWavMixerGroup_e::Abilities: return "abilities";
		case RPakWavMixerGroup_e::Critical: return "critical";
		default: return "world";
		}
	}

	RPakWavMixerGroup_e RPakWav_GetMixerGroup(const char* const eventName, const RPakWavAudioBus_e bus)
	{
		if (!eventName || !*eventName) return RPakWavMixerGroup_e::World;
		const std::string name = RPakWav_LowerEventName(eventName);
		if (bus == RPakWavAudioBus_e::Dialogue || RPakWav_NameContainsAny(name, { "footstep", "foot_step" }))
			return RPakWavMixerGroup_e::Critical;
		if (RPakWav_NameContainsAny(name, { "_tac_", "_ult_", "ability", "portal", "healing", "syringe", "medkit", "shieldcell", "shieldbattery" }))
			return RPakWavMixerGroup_e::Abilities;
		RPakWav_EnsureWeaponLoopEventCache();
		std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopEventCacheMutex);
		if (name.rfind("weapon_", 0) == 0 || s_rpakWavWeaponSoundEvents.count(Pak_StringToGuid(eventName)))
			return RPakWavMixerGroup_e::Weapons;
		return RPakWavMixerGroup_e::World;
	}

	bool RPakWav_EventIsFirstPerson(const std::string& lowerName)
	{
		// The engine routes these aliases to the first-person listener. An
		// attached entity identifies the emitter; it must not turn the alias
		// into a world-positioned sound. Keep this separate from weapon priority.
		for (const char* const token : { "_1p", "_firstperson", "_first_person" })
		{
			const size_t tokenLength = strlen(token);
			for (size_t offset = lowerName.find(token); offset != std::string::npos;
				offset = lowerName.find(token, offset + tokenLength))
			{
				const size_t end = offset + tokenLength;
				if (end == lowerName.size() || lowerName[end] == '_')
					return true;
			}
		}
		return false;
	}

	bool RPakWav_EventNameLooksLikeMusicStop(const char* const eventName)
	{
		const std::string lowerName = RPakWav_LowerEventName(eventName);
		return RPakWav_NameContainsAny(lowerName, { "music", "musicpack", "mus_" }) &&
			RPakWav_NameContainsAny(lowerName, { "stop", "end", "fadeout", "fade_out", "silence", "mute", "kill" });
	}

	bool RPakWav_ShouldUseLoadedPakFallback(const char* const eventName)
	{
		if (!eventName || !*eventName)
			return false;

		const std::string lowerName = RPakWav_LowerEventName(eventName);
		if (lowerName.find('/') != std::string::npos || lowerName.find('\\') != std::string::npos ||
			RPakWav_NameContainsAny(lowerName, { "rpakwav_", "custom_", "_magic" }))
			return true;

		// Keep project-local music tokens like "schoolhouse" working without
		// letting native-style aliases such as Weapon_* or *_loop hit the pak scanner.
		for (const char* it = eventName; *it; ++it)
		{
			const unsigned char ch = static_cast<unsigned char>(*it);
			if (!std::islower(ch) && !std::isdigit(ch) && ch != '-')
				return false;
		}

		return true;
	}

	void* RPakWav_FindLoadedHeaderByGuid(const PakGuid_t guid, const uint32_t expectedAssetType, PakLoadedInfo_s*& outLoadedPak)
	{
		if (!g_pakGlobals)
			return nullptr;

		for (int i = 0; i < PAK_MAX_LOADED_PAKS; ++i)
		{
			PakLoadedInfo_s& loadedPak = g_pakGlobals->loadedPaks[i];
			PakFile_s* const pak = loadedPak.pakFile;

			if (loadedPak.status != PAK_STATUS_LOADED || !pak)
				continue;

			for (uint32_t assetIndex = 0; assetIndex < pak->GetAssetCount(); ++assetIndex)
			{
				const PakAsset_s& asset = pak->memoryData.assetEntries[assetIndex];
				if (asset.guid != guid)
					continue;

				if (expectedAssetType != 0 && asset.magic != expectedAssetType)
					continue;

				outLoadedPak = &loadedPak;
				return pak->GetPointerForPageOffset(asset.headPtr);
			}
		}

		return nullptr;
	}

	bool RPakWav_HasRegisteredEvents()
	{
		const RPakWavEventRegistrySnapshot_t* const snapshot =
			s_rpakWavEventRegistry.load(std::memory_order_acquire);

		return snapshot && snapshot->count != 0;
	}

	bool RPakWav_FindRegisteredSource(const PakGuid_t sourceGuid, const RPakWavAudioSourceHeader_v1_t*& outSource, PakLoadedInfo_s*& outLoadedPak)
	{
		const RPakWavSourceRegistrySnapshot_t* const snapshot =
			s_rpakWavSourceRegistry.load(std::memory_order_acquire);

		if (!snapshot)
			return false;

		for (uint32_t i = 0; i < snapshot->count; ++i)
		{
			const RPakWavSourceLookup_t& lookup = snapshot->entries[i];
			if (lookup.sourceGuid != sourceGuid)
				continue;

			if (!RPakWav_LoadedPakIsUsable(lookup.loadedPak))
				return false;

			if (lookup.loadedPak->pakFile && lookup.loadedPak->pakFile != lookup.pakFile)
				return false;

			if (!RPakWav_IsSourceHeader(lookup.source))
				return false;

			outSource = lookup.source;
			outLoadedPak = lookup.loadedPak;
			return true;
		}

		return false;
	}

	bool RPakWav_FindRegisteredEvent(const PakGuid_t eventGuid, const RPakWavAudioEventHeader_v2_t*& outEvent,
		PakLoadedInfo_s*& outLoadedPak, PakFile_s*& outPakFile)
	{
		const RPakWavEventRegistrySnapshot_t* const snapshot =
			s_rpakWavEventRegistry.load(std::memory_order_acquire);

		if (!snapshot)
			return false;

		for (uint32_t i = 0; i < snapshot->count; ++i)
		{
			const RPakWavEventLookup_t& lookup = snapshot->entries[i];
			if (lookup.eventGuid != eventGuid)
				continue;

			if (!RPakWav_LoadedPakIsUsable(lookup.loadedPak))
				return false;

			if (lookup.loadedPak->pakFile && lookup.loadedPak->pakFile != lookup.pakFile)
				return false;

			if (!RPakWav_IsEventHeader(lookup.event))
				return false;

			outEvent = lookup.event;
			outLoadedPak = lookup.loadedPak;
			outPakFile = lookup.pakFile;
			return true;
		}

		return false;
	}

	void RPakWav_ResetEventRegistry()
	{
		AudioScript_ResetState();

		std::lock_guard<std::mutex> lock(s_rpakWavEventRegistryWriteMutex);

		s_rpakWavEventRegistry.store(&s_rpakWavEmptyEventRegistry, std::memory_order_release);
		s_rpakWavEventRegistrySnapshots.clear();
		s_rpakWavEventRegistryOverflowWarned.store(false, std::memory_order_release);

		std::lock_guard<std::mutex> sourceLock(s_rpakWavSourceRegistryWriteMutex);
		s_rpakWavSourceRegistry.store(&s_rpakWavEmptySourceRegistry, std::memory_order_release);
		s_rpakWavSourceRegistrySnapshots.clear();
		s_rpakWavSourceRegistryOverflowWarned.store(false, std::memory_order_release);
	}

	const PakGuid_t* RPakWav_GetEventGuidList(const RPakWavAudioEventHeader_v2_t* const event,
		PakLoadedInfo_s* loadedEventPak, PakFile_s* eventPakFile);

	bool RPakWav_ParseWaveBytes(const std::vector<uint8_t>& wavBytes, RPakWavParsedWave_t& outWave);

	void RPakWav_ClearSourceCache()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavSourceCacheMutex);
		s_rpakWavSourceCache.clear();
		s_rpakWavSourceCacheOverflowWarned.store(false, std::memory_order_release);
	}

	bool RPakWav_FindCachedSourceWave(const PakGuid_t sourceGuid, std::shared_ptr<const RPakWavCachedWave_t>& outWave)
	{
		if (sourceGuid == 0)
			return false;

		std::lock_guard<std::mutex> lock(s_rpakWavSourceCacheMutex);
		for (const RPakWavSourceCacheEntry_t& entry : s_rpakWavSourceCache)
		{
			if (entry.sourceGuid != sourceGuid || !entry.wave)
				continue;

			outWave = entry.wave;
			return true;
		}

		return false;
	}

	bool RPakWav_BuildCachedWave(const PakGuid_t sourceGuid, const std::vector<uint8_t>& wavBytes,
		std::shared_ptr<const RPakWavCachedWave_t>& outWave)
	{
		RPakWavParsedWave_t parsedWave{};
		if (!RPakWav_ParseWaveBytes(wavBytes, parsedWave))
			return false;

		std::unique_ptr<RPakWavCachedWave_t> wave(new RPakWavCachedWave_t());
		wave->sourceGuid = sourceGuid;
		wave->format = parsedWave.format;
		wave->channelMask = parsedWave.channelMask;
		wave->pcmBytes.assign(parsedWave.data, parsedWave.data + parsedWave.dataSize);

		const WORD authoredBlockAlign = wave->format.nBlockAlign;
		const DWORD authoredAverageBytes = wave->format.nAvgBytesPerSec;
		bool formatChanged = false;
		if (!RPakWav_NormalizeLinearWaveFormat(wave->format, formatChanged))
			return false;

		const size_t alignedDataSize =
			(wave->pcmBytes.size() / static_cast<size_t>(wave->format.nBlockAlign)) *
			static_cast<size_t>(wave->format.nBlockAlign);
		if (alignedDataSize == 0)
			return false;

		const bool dataTrimmed = alignedDataSize != wave->pcmBytes.size();
		if (dataTrimmed)
			wave->pcmBytes.resize(alignedDataSize);

		if (formatChanged || dataTrimmed)
		{
			Warning(eDLL_T::AUDIO,
				"Normalized packed WAV source 0x%llX metadata: align %u->%u avg %lu->%lu data %zu->%zu\n",
				sourceGuid,
				static_cast<unsigned int>(authoredBlockAlign),
				static_cast<unsigned int>(wave->format.nBlockAlign),
				static_cast<unsigned long>(authoredAverageBytes),
				static_cast<unsigned long>(wave->format.nAvgBytesPerSec),
				static_cast<size_t>(parsedWave.dataSize),
				wave->pcmBytes.size());
		}

		if (wave->format.nChannels > 2)
		{
			const uint16_t sourceChannels = wave->format.nChannels;
			WAVEFORMATEX stereoFormat{};
			std::vector<uint8_t> stereoBytes;
			if (!RPakWav_DownmixToStereo(wave->format, wave->channelMask, wave->pcmBytes, stereoFormat, stereoBytes))
				return false;

			wave->format = stereoFormat;
			wave->channelMask = RPAK_WAV_SPEAKER_FRONT_LEFT | RPAK_WAV_SPEAKER_FRONT_RIGHT;
			wave->pcmBytes = std::move(stereoBytes);

			if (RPakWav_DebugEnabled())
			{
				Msg(eDLL_T::AUDIO, "Downmixed packed WAV source 0x%llX from %u channels to stereo\n",
					sourceGuid, sourceChannels);
			}
		}

		wave->sampleRate = wave->format.nSamplesPerSec;
		wave->channels = wave->format.nChannels;

		outWave.reset(wave.release());
		return true;
	}

	bool RPakWav_StoreCachedSourceWave(const PakGuid_t sourceGuid, const std::shared_ptr<const RPakWavCachedWave_t>& wave,
		std::shared_ptr<const RPakWavCachedWave_t>& outWave)
	{
		if (sourceGuid == 0 || !wave)
		{
			outWave = wave;
			return wave != nullptr;
		}

		std::lock_guard<std::mutex> lock(s_rpakWavSourceCacheMutex);
		for (const RPakWavSourceCacheEntry_t& entry : s_rpakWavSourceCache)
		{
			if (entry.sourceGuid != sourceGuid || !entry.wave)
				continue;

			outWave = entry.wave;
			return true;
		}

		if (s_rpakWavSourceCache.size() >= RPAK_WAV_MAX_SOURCE_CACHE_ENTRIES)
		{
			if (!s_rpakWavSourceCacheOverflowWarned.exchange(true, std::memory_order_acq_rel))
				Warning(eDLL_T::AUDIO, "Packed WAV source cache is full; future uncached sources will still play without being retained\n");

			outWave = wave;
			return true;
		}

		s_rpakWavSourceCache.push_back({ sourceGuid, wave });
		outWave = wave;
		return true;
	}

	bool RPakWav_ReadSourceBytes(PakLoadedInfo_s* const loadedPak, const RPakWavAudioSourceHeader_v1_t* const source, std::vector<uint8_t>& outBytes)
	{
		if (!g_pakLoadApi || !RPakWav_LoadedPakIsUsable(loadedPak) || !source || source->streamSize == 0 || source->streamOffset < 0)
			return false;

		if (source->streamIndex < 0 || source->streamIndex >= PAK_MAX_STREAMING_FILE_HANDLES_PER_SET)
			return false;

		const int fileHandle = loadedPak->streamInfo[STREAMING_SET_MANDATORY].streamFileNumber[source->streamIndex];
		if (fileHandle == FS_ASYNC_FILE_INVALID)
			return false;

		outBytes.resize(static_cast<size_t>(source->streamSize));

		const int asyncRequest = g_pakLoadApi->ReadAsyncFile(fileHandle, static_cast<size_t>(source->streamOffset),
			static_cast<size_t>(source->streamSize), outBytes.data(), 1);

		const char* statusMsg = "(no reason)";
		size_t bytesProcessed = 0;
		const AsyncHandleStatus_s::Status_e status = g_pakLoadApi->WaitAndCheckAsyncRequest(asyncRequest, &bytesProcessed, &statusMsg);

		if (status == AsyncHandleStatus_s::Status_e::FS_ASYNC_ERROR || bytesProcessed != source->streamSize)
		{
			Warning(eDLL_T::AUDIO, "Packed WAV read failed -- %s\n", statusMsg);
			outBytes.clear();
			return false;
		}

		return true;
	}

	bool RPakWav_GetCachedSourceWave(PakLoadedInfo_s* const loadedPak, const RPakWavAudioSourceHeader_v1_t* const source,
		std::shared_ptr<const RPakWavCachedWave_t>& outWave)
	{
		if (!source)
			return false;

		if (RPakWav_FindCachedSourceWave(source->sourceGuid, outWave))
			return true;

		std::vector<uint8_t> wavBytes;
		if (!RPakWav_ReadSourceBytes(loadedPak, source, wavBytes))
			return false;

		std::shared_ptr<const RPakWavCachedWave_t> wave;
		if (!RPakWav_BuildCachedWave(source->sourceGuid, wavBytes, wave))
			return false;

		return RPakWav_StoreCachedSourceWave(source->sourceGuid, wave, outWave);
	}

	bool RPakWav_ReadFirstEventSourceWave(const RPakWavAudioEventHeader_v2_t* const event,
		std::shared_ptr<const RPakWavCachedWave_t>& outWave,
		RPakWavSelectedSource_t* const outSelectedSource)
	{
		if (event && RPakWav_FindCachedSourceWave(event->firstSourceGuid, outWave))
		{
			if (outSelectedSource)
			{
				outSelectedSource->sourceGuid = outWave->sourceGuid;
				outSelectedSource->sampleRate = outWave->sampleRate;
				outSelectedSource->channels = outWave->channels;
			}

			return true;
		}

		if (!event || !event->firstSourceStreamPath[0] || event->firstSourceStreamSize == 0 || event->firstSourceStreamOffset < 0)
			return false;

		const char* const streamPath = event->firstSourceStreamPath;
		std::vector<std::filesystem::path> candidates;
		const std::filesystem::path rawPath(streamPath);
		candidates.push_back(rawPath);

		if (rawPath.is_relative())
		{
			char exePath[MAX_PATH];
			if (GetModuleFileNameA(nullptr, exePath, static_cast<DWORD>(sizeof(exePath))) != 0)
				candidates.push_back(std::filesystem::path(exePath).parent_path() / rawPath);

			const char* const readPath = Pak_GetReadPath();
			if (readPath && *readPath)
				candidates.push_back(std::filesystem::path(readPath) / rawPath);

			const char* const writePath = Pak_GetWritePath();
			if (writePath && *writePath)
				candidates.push_back(std::filesystem::path(writePath) / rawPath);
		}

		std::ifstream stream;
		std::filesystem::path openedPath;
		for (const std::filesystem::path& candidate : candidates)
		{
			stream.open(candidate, std::ios::binary);
			if (stream.is_open())
			{
				openedPath = candidate;
				break;
			}
		}

		if (!stream.is_open())
		{
			Warning(eDLL_T::AUDIO, "Packed WAV read failed -- could not open stream '%s'\n", streamPath);
			return false;
		}

		std::vector<uint8_t> wavBytes;
		wavBytes.resize(static_cast<size_t>(event->firstSourceStreamSize));
		stream.seekg(static_cast<std::streamoff>(event->firstSourceStreamOffset), std::ios::beg);
		stream.read(reinterpret_cast<char*>(wavBytes.data()), static_cast<std::streamsize>(wavBytes.size()));
		if (!stream || static_cast<uint64_t>(stream.gcount()) != event->firstSourceStreamSize)
		{
			Warning(eDLL_T::AUDIO, "Packed WAV read failed -- '%s' offset %lld size %llu read %lld bytes\n",
				openedPath.string().c_str(),
				static_cast<long long>(event->firstSourceStreamOffset),
				static_cast<unsigned long long>(event->firstSourceStreamSize),
				static_cast<long long>(stream.gcount()));
			return false;
		}

		std::shared_ptr<const RPakWavCachedWave_t> wave;
		if (!RPakWav_BuildCachedWave(event->firstSourceGuid, wavBytes, wave))
			return false;

		if (!RPakWav_StoreCachedSourceWave(event->firstSourceGuid, wave, outWave))
			return false;

		if (outSelectedSource)
		{
			outSelectedSource->sourceGuid = outWave->sourceGuid;
			outSelectedSource->sampleRate = outWave->sampleRate ? outWave->sampleRate : event->firstSourceSampleRate;
			outSelectedSource->channels = outWave->channels ? outWave->channels : event->firstSourceChannels;
		}

		return true;
	}

	bool RPakWav_ReadEventSourceWave(const RPakWavAudioEventHeader_v2_t* const event,
		PakLoadedInfo_s* const loadedEventPak, PakFile_s* const eventPakFile,
		std::shared_ptr<const RPakWavCachedWave_t>& outWave,
		RPakWavSelectedSource_t* const outSelectedSource)
	{
		if (!event)
			return false;

		const PakGuid_t* const sourceGuids = RPakWav_GetEventGuidList(event, loadedEventPak, eventPakFile);
		if (sourceGuids && event->sourceCount != 0)
		{
			const uint32_t startIndex = event->sourceCount > 1 ? (RPakWav_NextRandom() % event->sourceCount) : 0;
			for (uint32_t attempt = 0; attempt < event->sourceCount; ++attempt)
			{
				const uint32_t sourceIndex = (startIndex + attempt) % event->sourceCount;
				const PakGuid_t sourceGuid = sourceGuids[sourceIndex];
				const RPakWavAudioSourceHeader_v1_t* source = nullptr;
				PakLoadedInfo_s* loadedSourcePak = nullptr;
				if (!RPakWav_FindRegisteredSource(sourceGuid, source, loadedSourcePak))
					continue;

				if (!RPakWav_GetCachedSourceWave(loadedSourcePak, source, outWave))
					continue;

				if (outSelectedSource)
				{
					outSelectedSource->sourceGuid = outWave->sourceGuid ? outWave->sourceGuid : sourceGuid;
					outSelectedSource->sampleRate = outWave->sampleRate ? outWave->sampleRate : source->sampleRate;
					outSelectedSource->channels = outWave->channels ? outWave->channels : source->channels;
				}

				return true;
			}
		}

		return RPakWav_ReadFirstEventSourceWave(event, outWave, outSelectedSource);
	}

	bool RPakWav_ReadSpecificEventSourceWave(const RPakWavAudioEventHeader_v2_t* const event,
		PakLoadedInfo_s* const loadedEventPak, PakFile_s* const eventPakFile,
		const uint32_t sourceIndex, const PakGuid_t expectedSourceGuid,
		std::shared_ptr<const RPakWavCachedWave_t>& outWave,
		RPakWavSelectedSource_t* const outSelectedSource)
	{
		if (!event || sourceIndex >= event->sourceCount)
			return false;

		const PakGuid_t* const sourceGuids = RPakWav_GetEventGuidList(event, loadedEventPak, eventPakFile);
		PakGuid_t sourceGuid = sourceGuids ? sourceGuids[sourceIndex] : 0;
		if (!sourceGuid && sourceIndex == 0)
			sourceGuid = event->firstSourceGuid;

		if (!sourceGuid || (expectedSourceGuid && expectedSourceGuid != sourceGuid))
			return false;

		const RPakWavAudioSourceHeader_v1_t* source = nullptr;
		PakLoadedInfo_s* loadedSourcePak = nullptr;
		if (!RPakWav_FindRegisteredSource(sourceGuid, source, loadedSourcePak))
			return false;

		if (!RPakWav_GetCachedSourceWave(loadedSourcePak, source, outWave))
			return false;

		if (outSelectedSource)
		{
			outSelectedSource->sourceGuid = outWave->sourceGuid ? outWave->sourceGuid : sourceGuid;
			outSelectedSource->sampleRate = outWave->sampleRate ? outWave->sampleRate : source->sampleRate;
			outSelectedSource->channels = outWave->channels ? outWave->channels : source->channels;
		}

		return true;
	}

	template <typename T>
	bool RPakWav_ReadLE(const uint8_t* const bytes, const size_t size, const size_t offset, T& outValue)
	{
		if (offset > size || sizeof(T) > size - offset)
			return false;

		memcpy(&outValue, bytes + offset, sizeof(T));
		return true;
	}

	bool RPakWav_ParseWaveBytes(const std::vector<uint8_t>& wavBytes, RPakWavParsedWave_t& outWave)
	{
		if (wavBytes.size() < 12)
			return false;

		uint32_t riffMagic = 0;
		uint32_t waveMagic = 0;
		if (!RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), 0, riffMagic) ||
			!RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), 8, waveMagic))
		{
			return false;
		}

		if (riffMagic != RPAK_WAV_RIFF_MAGIC || waveMagic != RPAK_WAV_WAVE_MAGIC)
			return false;

		bool foundFormat = false;
		bool foundData = false;
		WAVEFORMATEX parsedFormat{};
		DWORD parsedChannelMask = 0;
		size_t cursor = 12;

		while (cursor + 8 <= wavBytes.size())
		{
			uint32_t chunkMagic = 0;
			uint32_t chunkSize = 0;
			if (!RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), cursor, chunkMagic) ||
				!RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), cursor + 4, chunkSize))
			{
				return false;
			}

			const size_t chunkDataOffset = cursor + 8;
			if (chunkDataOffset > wavBytes.size() || chunkSize > wavBytes.size() - chunkDataOffset)
				return false;

			if (chunkMagic == RPAK_WAV_FMT_MAGIC)
			{
				if (chunkSize < 16)
					return false;

				WORD formatTag = 0;
				WORD channels = 0;
				DWORD sampleRate = 0;
				DWORD avgBytesPerSecond = 0;
				WORD blockAlign = 0;
				WORD bitsPerSample = 0;

				RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), chunkDataOffset + 0, formatTag);
				RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), chunkDataOffset + 2, channels);
				RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), chunkDataOffset + 4, sampleRate);
				RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), chunkDataOffset + 8, avgBytesPerSecond);
				RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), chunkDataOffset + 12, blockAlign);
				RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), chunkDataOffset + 14, bitsPerSample);

				if (formatTag == RPAK_WAV_FORMAT_EXTENSIBLE)
				{
					if (chunkSize < 40)
						return false;

					WORD cbSize = 0;
					DWORD channelMask = 0;
					DWORD subFormatTag = 0;
					if (!RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), chunkDataOffset + 16, cbSize) ||
						!RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), chunkDataOffset + 20, channelMask) ||
						!RPakWav_ReadLE(wavBytes.data(), wavBytes.size(), chunkDataOffset + 24, subFormatTag))
					{
						return false;
					}
					if (cbSize < 22 || (subFormatTag != WAVE_FORMAT_PCM && subFormatTag != RPAK_WAV_FORMAT_IEEE_FLOAT))
						return false;

					formatTag = static_cast<WORD>(subFormatTag);
					parsedChannelMask = channelMask;
				}
				else if (formatTag != WAVE_FORMAT_PCM && formatTag != RPAK_WAV_FORMAT_IEEE_FLOAT)
				{
					return false;
				}

				if (channels == 0 || sampleRate == 0 || bitsPerSample == 0)
					return false;

				parsedFormat.wFormatTag = formatTag;
				parsedFormat.nChannels = channels;
				parsedFormat.nSamplesPerSec = sampleRate;
				parsedFormat.nAvgBytesPerSec = avgBytesPerSecond;
				parsedFormat.nBlockAlign = blockAlign;
				parsedFormat.wBitsPerSample = bitsPerSample;
				parsedFormat.cbSize = 0;
				foundFormat = true;
			}
			else if (chunkMagic == RPAK_WAV_DATA_MAGIC)
			{
				if (chunkSize == 0)
					return false;

				outWave.data = wavBytes.data() + chunkDataOffset;
				outWave.dataSize = chunkSize;
				foundData = true;
			}

			const size_t paddedChunkSize = static_cast<size_t>(chunkSize) + (chunkSize & 1);
			if (paddedChunkSize > wavBytes.size() - chunkDataOffset)
				break;

			cursor = chunkDataOffset + paddedChunkSize;
		}

		if (!foundFormat || !foundData)
			return false;

		outWave.format = parsedFormat;
		outWave.channelMask = parsedChannelMask;
		return true;
	}

	void CALLBACK RPakWav_WaveOutProc(HWAVEOUT, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR)
	{
		if (message != WOM_DONE || !instance)
			return;

		RPakWavVoice_t* const voice = reinterpret_cast<RPakWavVoice_t*>(instance);
		if (voice->spatialStreaming)
		{
			WAVEHDR* const doneHeader = reinterpret_cast<WAVEHDR*>(param1);
			for (RPakWavSpatialChunk_t& chunk : voice->spatialChunks)
			{
				if (&chunk.header != doneHeader)
					continue;

				RPakWav_DecrementSpatialActiveChunks(*voice);
				if (s_rpakWavSpatialWakeEvent)
					SetEvent(s_rpakWavSpatialWakeEvent);
				// Publish completion last: the service thread can now recycle the
				// header or retire the voice, so do not touch it again afterward.
				chunk.queued.store(false, std::memory_order_release);
				break;
			}

			return;
		}

		voice->done = true;
	}

	void RPakWav_CloseVoice(RPakWavVoice_t& voice, const bool reset)
	{
		if (voice.closed)
			return;
		if (voice.sharedMixer)
		{
			// Output buffers reference only the global mix, never this object.
			voice.done = true;
			voice.closed = true;
			AudioScript_RetireVoice(voice.scriptVoiceId, voice.sourceLayerCount);
			return;
		}
		if (!voice.waveOut)
			return;

		if (voice.countedMusic)
		{
			uint32_t currentCount = s_rpakWavActiveMusicVoiceCount.load(std::memory_order_relaxed);
			while (currentCount != 0 &&
				!s_rpakWavActiveMusicVoiceCount.compare_exchange_weak(currentCount, currentCount - 1, std::memory_order_relaxed))
			{
			}
		}

		if (reset && !voice.done)
			waveOutReset(voice.waveOut);

		if (voice.spatialStreaming)
		{
			for (RPakWavSpatialChunk_t& chunk : voice.spatialChunks)
			{
				if (!chunk.prepared)
					continue;

				waveOutUnprepareHeader(voice.waveOut, &chunk.header, sizeof(chunk.header));
				chunk.prepared = false;
				chunk.queued.store(false, std::memory_order_release);
			}

			voice.spatialActiveChunks.store(0, std::memory_order_release);
		}
		else if (voice.prepared)
		{
			waveOutUnprepareHeader(voice.waveOut, &voice.header, sizeof(voice.header));
			voice.prepared = false;
		}

		waveOutClose(voice.waveOut);
		voice.waveOut = nullptr;
		voice.done = true;
		voice.closed = true;
		AudioScript_RetireVoice(voice.scriptVoiceId, voice.sourceLayerCount);
	}

	void RPakWav_CleanupDoneVoices()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (auto it = s_rpakWavVoices.begin(); it != s_rpakWavVoices.end();)
		{
			RPakWavVoice_t& voice = **it;
			if (voice.done)
			{
				if (voice.scriptVoiceId == 0 &&
					(voice.flags & RPAK_WAV_EVENT_FLAG_FINISH_RETRIGGER_GRACE))
				{
					const bool anotherLayerPlaying = std::any_of(s_rpakWavVoices.begin(), s_rpakWavVoices.end(),
						[&voice](const std::unique_ptr<RPakWavVoice_t>& other)
						{
							return other && other.get() != &voice && other->eventGuid == voice.eventGuid &&
								!other->closed && !other->done && RPakWav_SameEmitter(other->spatial, voice.spatial);
						});
					if (!anotherLayerPlaying && !voice.mixerFading)
					{
						RPakWav_ClearFinishGraceLocked(voice.eventGuid, voice.spatial);
						s_rpakWavNaturalFinishTimes.push_back({ voice.eventGuid, voice.spatial, GetTickCount64() });
					}
				}
				RPakWav_CloseVoice(voice, false);
				it = s_rpakWavVoices.erase(it);
				continue;
			}

			++it;
		}
	}

	bool RPakWav_SetScriptVoiceVolume(const uint32_t scriptVoiceId, const float volume)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		bool found = false;
		bool allUpdated = true;
		for (const std::unique_ptr<RPakWavVoice_t>& voicePtr : s_rpakWavVoices)
		{
			RPakWavVoice_t& voice = *voicePtr;
			if (voice.scriptVoiceId != scriptVoiceId || voice.closed || voice.done)
				continue;
			found = true;

			const float previousVolume = voice.scriptVolume;
			voice.scriptVolume = AudioScript_ClampVolume(volume);
			if (voice.sharedMixer)
			{
				RPakWav_CacheSpatialVoiceGains(voice);
				continue;
			}
			if (voice.spatialStreaming)
				continue;

			if (!voice.waveOut || voice.playbackBytes.empty())
			{
				voice.scriptVolume = previousVolume;
				allUpdated = false;
				continue;
			}

			if (waveOutPause(voice.waveOut) != MMSYSERR_NOERROR)
			{
				voice.scriptVolume = previousVolume;
				allUpdated = false;
				continue;
			}

			std::vector<uint8_t> updatedBytes;
			const bool rebuilt = RPakWav_BuildVolumePlaybackBuffer(
				*voice.wave, RPakWav_GetVoiceSampleGain(voice), updatedBytes);
			if (!rebuilt || updatedBytes.size() != voice.playbackBytes.size())
			{
				voice.scriptVolume = previousVolume;
				waveOutRestart(voice.waveOut);
				allUpdated = false;
				continue;
			}

			memcpy(voice.playbackBytes.data(), updatedBytes.data(), updatedBytes.size());
			if (waveOutRestart(voice.waveOut) != MMSYSERR_NOERROR)
				allUpdated = false;
		}

		return found && allUpdated;
	}

	bool RPakWav_SetScriptVoicePitch(const uint32_t scriptVoiceId, const float pitch)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		bool found = false;
		bool allUpdated = true;
		for (const std::unique_ptr<RPakWavVoice_t>& voicePtr : s_rpakWavVoices)
		{
			RPakWavVoice_t& voice = *voicePtr;
			if (voice.scriptVoiceId != scriptVoiceId || voice.closed || voice.done || (!voice.waveOut && !voice.sharedMixer))
				continue;
			found = true;

			const float requestedPitch = AudioScript_ClampPitch(pitch);
			if (voice.sharedMixer)
			{
				voice.scriptPitch = requestedPitch;
				continue;
			}
			const double relativeRate = static_cast<double>(requestedPitch) /
				static_cast<double>(voice.openedPitch);
			const DWORD fixedRate = static_cast<DWORD>(std::clamp(
				relativeRate * 65536.0, 1.0, static_cast<double>(UINT32_MAX)));
			if (waveOutSetPlaybackRate(voice.waveOut, fixedRate) != MMSYSERR_NOERROR)
			{
				allUpdated = false;
				continue;
			}

			voice.scriptPitch = requestedPitch;
		}

		return found && allUpdated;
	}

	bool RPakWav_StopScriptVoice(const uint32_t scriptVoiceId)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		bool stopped = false;
		for (auto it = s_rpakWavVoices.begin(); it != s_rpakWavVoices.end();)
		{
			RPakWavVoice_t& voice = **it;
			if (voice.scriptVoiceId != scriptVoiceId || voice.closed)
			{
				++it;
				continue;
			}

			RPakWav_CloseVoice(voice, true);
			it = s_rpakWavVoices.erase(it);
			stopped = true;
		}

		return stopped;
	}

	void RPakWav_StopVoicesByEvent(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* spatial)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (auto it = s_rpakWavVoices.begin(); it != s_rpakWavVoices.end();)
		{
			RPakWavVoice_t& voice = **it;
			if (voice.eventGuid == eventGuid && (!spatial || RPakWav_SameEmitter(voice.spatial, *spatial)))
			{
				RPakWav_CloseVoice(voice, true);
				it = s_rpakWavVoices.erase(it);
				continue;
			}

			++it;
		}
	}

	size_t RPakWav_StopLoopingVoicesByEvent(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* spatial)
	{
		size_t stoppedCount = 0;
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (auto it = s_rpakWavVoices.begin(); it != s_rpakWavVoices.end();)
		{
			RPakWavVoice_t& voice = **it;
			if (voice.eventGuid == eventGuid && voice.looping && (!spatial || RPakWav_SameEmitter(voice.spatial, *spatial)))
			{
				RPakWav_CloseVoice(voice, true);
				it = s_rpakWavVoices.erase(it);
				++stoppedCount;
				continue;
			}

			++it;
		}

		return stoppedCount;
	}

	void RPakWav_StopVoicesByFlags(const uint32_t requiredFlags)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (auto it = s_rpakWavVoices.begin(); it != s_rpakWavVoices.end();)
		{
			RPakWavVoice_t& voice = **it;
			if ((voice.flags & requiredFlags) == requiredFlags)
			{
				RPakWav_CloseVoice(voice, true);
				it = s_rpakWavVoices.erase(it);
				continue;
			}

			++it;
		}
	}

	void RPakWav_StopAllVoices()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (std::unique_ptr<RPakWavVoice_t>& voice : s_rpakWavVoices)
			RPakWav_CloseVoice(*voice, true);

		s_rpakWavVoices.clear();
		s_rpakWavNaturalFinishTimes.clear();
		RPakWav_CloseMixerLocked();
	}

	bool RPakWav_HasActiveVoiceForEvent(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* spatial)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (const std::unique_ptr<RPakWavVoice_t>& voice : s_rpakWavVoices)
		{
			if (voice && voice->eventGuid == eventGuid && !voice->closed && (!spatial || RPakWav_SameEmitter(voice->spatial, *spatial)))
				return true;
		}

		return false;
	}

	bool RPakWav_FinishedRecently(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* spatial)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		const uint64_t now = GetTickCount64();
		s_rpakWavNaturalFinishTimes.erase(std::remove_if(s_rpakWavNaturalFinishTimes.begin(),
			s_rpakWavNaturalFinishTimes.end(), [&](const auto& finish)
			{ return now - finish.timeMs >= RPAK_WAV_FINISH_RETRIGGER_GRACE_MS; }), s_rpakWavNaturalFinishTimes.end());
		return std::any_of(s_rpakWavNaturalFinishTimes.begin(), s_rpakWavNaturalFinishTimes.end(),
			[&](const auto& finish) { return finish.eventGuid == eventGuid &&
				(!spatial || RPakWav_SameEmitter(finish.spatial, *spatial)); });
	}

	bool RPakWav_RefreshActiveVoiceForEvent(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* const spatialInfo)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		bool found = false;
		for (const std::unique_ptr<RPakWavVoice_t>& voice : s_rpakWavVoices)
		{
			if (!voice || voice->eventGuid != eventGuid || voice->closed ||
				(spatialInfo && !RPakWav_SameEmitter(voice->spatial, *spatialInfo)))
				continue;

			if ((voice->spatialStreaming || voice->sharedMixer) && spatialInfo)
				voice->spatial = *spatialInfo;

			found = true;
		}

		return found;
	}

	bool RPakWav_PlayCachedWave(const PakGuid_t eventGuid, const uint32_t eventFlags, const char* const eventName,
		const std::shared_ptr<const RPakWavCachedWave_t>& wave, const float eventVolume, const bool forceLoop,
		const RPakWavSpatialInfo_t* const spatialInfo = nullptr, const uint32_t scriptVoiceId = 0,
		const float scriptVolume = 1.0f, const float scriptPitch = 1.0f,
		const bool additionalLayer = false, bool* const outStarted = nullptr, const uint64_t layerBatchId = 0,
		const uint32_t sourceLayerCount = 1, const uint32_t batchVoiceCount = 1)
	{
		if (outStarted)
			*outStarted = false;
		if (!wave || wave->pcmBytes.empty())
			return false;

		RPakWav_CleanupDoneVoices();
		const bool loopEvent = forceLoop || ((eventFlags & RPAK_WAV_EVENT_FLAG_LOOP) != 0);
		if (!additionalLayer && scriptVoiceId == 0 && loopEvent && RPakWav_RefreshActiveVoiceForEvent(eventGuid, spatialInfo))
			return true;

		if (!additionalLayer && scriptVoiceId == 0 && (eventFlags & RPAK_WAV_EVENT_FLAG_IGNORE_WHILE_PLAYING) &&
			RPakWav_HasActiveVoiceForEvent(eventGuid, spatialInfo))
		{
			if (RPakWav_DebugEnabled())
				Msg(eDLL_T::AUDIO, "Ignoring duplicate packed WAV event '%s' while its voice is active\n",
					eventName ? eventName : "<null>");

			return true;
		}
		if (!additionalLayer && scriptVoiceId == 0 && (eventFlags & RPAK_WAV_EVENT_FLAG_FINISH_RETRIGGER_GRACE) &&
			RPakWav_FinishedRecently(eventGuid, spatialInfo))
		{
			if (RPakWav_DebugEnabled())
				Msg(eDLL_T::AUDIO, "Ignoring immediate replay of finished packed WAV event '%s'\n",
					eventName ? eventName : "<null>");
			return true;
		}

		if (!additionalLayer && ((eventFlags & RPAK_WAV_EVENT_FLAG_REPLACE_SAME_EVENT) ||
			(eventFlags & RPAK_WAV_EVENT_FLAG_MUSIC) || RPakWav_EventNameLooksLikeMusic(eventName)))
			RPakWav_StopVoicesByEvent(eventGuid, (eventFlags & RPAK_WAV_EVENT_FLAG_MUSIC) ||
				RPakWav_EventNameLooksLikeMusic(eventName) ? nullptr : spatialInfo);

		std::unique_ptr<RPakWavVoice_t> voice(new RPakWavVoice_t());
		voice->scriptVoiceId = scriptVoiceId;
		voice->layerBatchId = layerBatchId;
		voice->sourceLayerCount = sourceLayerCount;
		voice->eventGuid = eventGuid;
		voice->eventName = eventName ? eventName : "";
		voice->flags = eventFlags | RPAK_WAV_EVENT_FLAG_MANAGED;
		voice->looping = loopEvent;
		if (loopEvent)
			voice->flags |= RPAK_WAV_EVENT_FLAG_LOOP;
		if (RPakWav_EventNameLooksLikeMusic(eventName))
			voice->flags |= RPAK_WAV_EVENT_FLAG_MUSIC;

		voice->audioBus = RPakWav_GetAudioBusForEvent(eventName, voice->flags);
		if (spatialInfo) voice->spatial = *spatialInfo;
		voice->mixerGroup = RPakWav_GetMixerGroup(eventName, voice->audioBus);
		const std::string lowerEventName = RPakWav_LowerEventName(eventName);
		voice->firstPersonPlayback = RPakWav_EventIsFirstPerson(lowerEventName);
		voice->mixerFirstPerson = voice->mixerGroup == RPakWavMixerGroup_e::Weapons &&
			RPakWav_NameContainsAny(lowerEventName, { "_1p", "_firstperson", "_first_person" }) && !voice->spatial.valid;
		if (voice->mixerFirstPerson) voice->mixerGroup = RPakWavMixerGroup_e::FirstPersonWeapons;
		voice->eventVolume = RPakWav_NormalizeEventVolume(eventVolume);
		voice->scriptVolume = AudioScript_ClampVolume(scriptVolume);
		voice->openedPitch = AudioScript_ClampPitch(scriptPitch);
		voice->scriptPitch = voice->openedPitch;
		voice->wave = wave;
		voice->playbackFormat = voice->wave->format;
		if (RPakWav_SharedMixerEnabled() && !(voice->flags & RPAK_WAV_EVENT_FLAG_MUSIC) &&
			RPakWav_IsSpatialFormatSupported(wave->format) &&
			wave->pcmBytes.size() >= wave->format.nBlockAlign)
		{
			if (spatialInfo && spatialInfo->valid)
				voice->spatial = *spatialInfo;
			voice->sharedMixer = true;
			voice->mixerPending = s_rpakWavAudioFrameSubmitting.load(std::memory_order_acquire);
			RPakWav_CacheSpatialVoiceGains(*voice);
			voice->spatialPreviousLeftGain = voice->spatialTargetLeftGain;
			voice->spatialPreviousRightGain = voice->spatialTargetRightGain;
			voice->playbackFormat.wFormatTag = RPAK_WAV_FORMAT_IEEE_FLOAT;
			voice->playbackFormat.nSamplesPerSec = static_cast<DWORD>(RPAK_WAV_MIXER_RATE);
			voice->playbackFormat.nChannels = 2;
			voice->playbackFormat.wBitsPerSample = 32;
			voice->playbackFormat.nBlockAlign = 2 * sizeof(float);
			voice->playbackFormat.nAvgBytesPerSec = voice->playbackFormat.nSamplesPerSec * voice->playbackFormat.nBlockAlign;
			voice->playbackFormat.cbSize = 0;
			if (!RPakWav_StartSpatialService())
				return false;
			std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
			if (!RPakWav_OpenMixerLocked())
				return false;
			const size_t requested = additionalLayer ? 1u : batchVoiceCount;
			// Unknown third-person emitters do not share a per-event quota.
			const bool knownEmitter = voice->spatial.valid || voice->spatial.emitterEntity != UINT32_MAX || voice->mixerFirstPerson;
			std::unordered_set<uint64_t> victimBatches;
			for (;;)
			{
				size_t active = 0, nonWeapons = 0;
				std::unordered_set<uint64_t> emitterBatches;
				for (const auto& current : s_rpakWavVoices)
				{
					if (!current->sharedMixer || current->mixerFading || current->done || current->closed ||
						victimBatches.count(current->layerBatchId)) continue;
					++active;
					if (!RPakWav_IsWeaponMixerGroup(current->mixerGroup)) ++nonWeapons;
					if (knownEmitter && current->eventGuid == eventGuid &&
						RPakWav_SameEmitter(current->spatial, voice->spatial))
						emitterBatches.insert(current->layerBatchId);
				}
				const bool emitterFull = !additionalLayer && emitterBatches.size() >= RPAK_WAV_MIXER_MAX_EMITTER_BATCHES;
				const bool nonWeaponFull = !RPakWav_IsWeaponMixerGroup(voice->mixerGroup) &&
					nonWeapons + requested > RPAK_WAV_MIXER_MAX_VOICES - RPAK_WAV_MIXER_RESERVED_WEAPON_VOICES;
				if (!emitterFull && !nonWeaponFull && active + requested <= RPAK_WAV_MIXER_MAX_VOICES) break;
				auto victim = s_rpakWavVoices.end();
				for (auto it = s_rpakWavVoices.begin(); it != s_rpakWavVoices.end(); ++it)
				{
					const auto& current = *it;
					if (!current->sharedMixer || current->mixerFading || current->looping || current->scriptVoiceId ||
						current->done || current->closed || current->layerBatchId == layerBatchId ||
						victimBatches.count(current->layerBatchId) ||
						current->mixerPriority > voice->mixerPriority + 0.01f) continue;
					if (emitterFull && (current->eventGuid != eventGuid || !RPakWav_SameEmitter(current->spatial, voice->spatial))) continue;
					if (nonWeaponFull && RPakWav_IsWeaponMixerGroup(current->mixerGroup)) continue;
					// A batch is indivisible: do not evict its protected siblings.
					if (std::any_of(s_rpakWavVoices.begin(), s_rpakWavVoices.end(), [&](const auto& sibling)
						{ return sibling->layerBatchId == current->layerBatchId && (sibling->looping || sibling->scriptVoiceId); })) continue;
					if (victim == s_rpakWavVoices.end() || current->mixerPriority < (*victim)->mixerPriority) victim = it;
				}
				if (victim == s_rpakWavVoices.end())
				{
					++s_rpakWavMixerRejectedVoices;
					return scriptVoiceId == 0 && !additionalLayer; // Automatic culling is not a missing asset.
				}
				victimBatches.insert((*victim)->layerBatchId);
			}
			// Commit only after the entire incoming batch can fit. A rejected
			// lower-priority request must not silence any existing batches.
			for (const auto& current : s_rpakWavVoices)
				if (victimBatches.count(current->layerBatchId) && !current->mixerFading && !current->done && !current->closed)
				{
					current->mixerFading = true;
					current->mixerFadeRemaining = RPAK_WAV_MIXER_FADE_FRAMES;
					++s_rpakWavMixerStolenVoices;
				}
			// Bound retiring cursors even when producers outrun the fade interval.
			size_t fading = std::count_if(s_rpakWavVoices.begin(), s_rpakWavVoices.end(),
				[](const auto& current) { return current->sharedMixer && current->mixerFading; });
			for (auto it = s_rpakWavVoices.begin(); fading > RPAK_WAV_MIXER_MAX_VOICES && it != s_rpakWavVoices.end();)
			{
				if ((*it)->sharedMixer && (*it)->mixerFading)
				{
					RPakWav_CloseVoice(**it, true);
					it = s_rpakWavVoices.erase(it);
					--fading;
				}
				else ++it;
			}
			if (RPakWav_DebugEnabled())
				Msg(eDLL_T::AUDIO, "Packed WAV shared voice event='%s' batch=%llu layers=%u positional=%u emitter=%u group=%s priority=%.1f firstPerson=%u centered1p=%u\n",
					voice->eventName.c_str(), static_cast<unsigned long long>(layerBatchId), sourceLayerCount,
					!voice->firstPersonPlayback && voice->spatial.valid && RPakWav_SpatialEnabled() ? 1u : 0u, voice->spatial.emitterEntity,
					RPakWav_MixerGroupName(voice->mixerGroup), voice->mixerPriority, voice->mixerFirstPerson ? 1u : 0u,
					voice->firstPersonPlayback ? 1u : 0u);
			s_rpakWavVoices.push_back(std::move(voice));
			RPakWav_ClearFinishGraceLocked(eventGuid, s_rpakWavVoices.back()->spatial);
			if (outStarted) *outStarted = true;
			SetEvent(s_rpakWavSpatialWakeEvent);
			return true;
		}
		if (RPakWav_SpatialEnabled() && !(voice->flags & RPAK_WAV_EVENT_FLAG_MUSIC) &&
			!voice->firstPersonPlayback && spatialInfo && spatialInfo->valid)
		{
			voice->spatial = *spatialInfo;
			voice->spatialStreaming = RPakWav_PrepareSpatialPlaybackFormat(*voice->wave, voice->playbackFormat);
		}

		const double pitchedSampleRate = static_cast<double>(voice->playbackFormat.nSamplesPerSec) *
			static_cast<double>(voice->openedPitch);
		voice->playbackFormat.nSamplesPerSec = static_cast<DWORD>(std::clamp(
			pitchedSampleRate, 1000.0, static_cast<double>(UINT32_MAX)));
		voice->playbackFormat.nAvgBytesPerSec =
			voice->playbackFormat.nSamplesPerSec * voice->playbackFormat.nBlockAlign;

		if (!voice->spatialStreaming)
		{
			const float voiceGain = RPakWav_GetVoiceSampleGain(*voice);
			if (std::abs(voiceGain - 1.0f) > 0.001f)
			{
				if (!RPakWav_BuildVolumePlaybackBuffer(*voice->wave, voiceGain, voice->playbackBytes) &&
					RPakWav_DebugEnabled())
				{
					Warning(eDLL_T::AUDIO, "Packed WAV event '%s' could not apply menu volume to unsupported format\n",
						eventName ? eventName : "<null>");
				}
			}

			if (RPakWav_DebugEnabled())
			{
				Msg(eDLL_T::AUDIO, "Packed WAV voice bus=%s gain=%.2f eventVol=%.2f\n",
					RPakWav_AudioBusDebugName(voice->audioBus), voiceGain, voice->eventVolume);
			}
		}

		if (!voice->spatialStreaming && !voice->playbackBytes.empty())
		{
			voice->header.lpData = reinterpret_cast<LPSTR>(voice->playbackBytes.data());
			voice->header.dwBufferLength = static_cast<DWORD>(voice->playbackBytes.size());
		}
		else if (!voice->spatialStreaming)
		{
			voice->header.lpData = const_cast<LPSTR>(reinterpret_cast<const char*>(voice->wave->pcmBytes.data()));
			voice->header.dwBufferLength = static_cast<DWORD>(voice->wave->pcmBytes.size());
		}

		if (!voice->spatialStreaming && voice->looping)
		{
			voice->header.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
			voice->header.dwLoops = 0xFFFFFFFFu;
		}

		if (voice->spatialStreaming && !RPakWav_StartSpatialService())
		{
			Warning(eDLL_T::AUDIO, "Packed WAV spatial service could not start for event '%s'\n", voice->eventName.c_str());
			return false;
		}

		MMRESULT result = waveOutOpen(&voice->waveOut, WAVE_MAPPER, &voice->playbackFormat,
			reinterpret_cast<DWORD_PTR>(&RPakWav_WaveOutProc), reinterpret_cast<DWORD_PTR>(voice.get()), CALLBACK_FUNCTION);
		if (result != MMSYSERR_NOERROR)
		{
			RPakWav_ReportWaveOutFailure("waveOutOpen", result, *voice);
			return false;
		}

		if (voice->spatialStreaming)
		{
			// Queue the initial ring before allowing playback to consume it.
			result = waveOutPause(voice->waveOut);
			if (result != MMSYSERR_NOERROR)
			{
				RPakWav_ReportWaveOutFailure("spatial waveOutPause", result, *voice);
				RPakWav_CloseVoice(*voice, true);
				return false;
			}
			if (!RPakWav_StartSpatialVoice(*voice))
			{
				RPakWav_CloseVoice(*voice, true);
				return false;
			}
			result = waveOutRestart(voice->waveOut);
			if (result != MMSYSERR_NOERROR)
			{
				RPakWav_ReportWaveOutFailure("spatial waveOutRestart", result, *voice);
				RPakWav_CloseVoice(*voice, true);
				return false;
			}

			if (voice->flags & RPAK_WAV_EVENT_FLAG_MUSIC)
			{
				voice->countedMusic = true;
				s_rpakWavActiveMusicVoiceCount.fetch_add(1, std::memory_order_relaxed);
			}

			std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
			s_rpakWavVoices.push_back(std::move(voice));
			RPakWav_ClearFinishGraceLocked(eventGuid, s_rpakWavVoices.back()->spatial);
			if (outStarted)
				*outStarted = true;
			return true;
		}

		result = waveOutPrepareHeader(voice->waveOut, &voice->header, sizeof(voice->header));
		if (result != MMSYSERR_NOERROR)
		{
			RPakWav_ReportWaveOutFailure("waveOutPrepareHeader", result, *voice);
			RPakWav_CloseVoice(*voice, true);
			return false;
		}

		voice->prepared = true;
		result = waveOutWrite(voice->waveOut, &voice->header, sizeof(voice->header));
		if (result != MMSYSERR_NOERROR)
		{
			RPakWav_ReportWaveOutFailure("waveOutWrite", result, *voice);
			RPakWav_CloseVoice(*voice, true);
			return false;
		}

		if (voice->flags & RPAK_WAV_EVENT_FLAG_MUSIC)
		{
			voice->countedMusic = true;
			s_rpakWavActiveMusicVoiceCount.fetch_add(1, std::memory_order_relaxed);
		}

		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		s_rpakWavVoices.push_back(std::move(voice));
		RPakWav_ClearFinishGraceLocked(eventGuid, s_rpakWavVoices.back()->spatial);
		if (outStarted)
			*outStarted = true;
		return true;
	}

	bool RPakWav_PlaySelectedLayers(const PakGuid_t eventGuid, const uint32_t eventFlags, const char* const eventName,
		const std::vector<std::shared_ptr<const RPakWavCachedWave_t>>& layers, const float eventVolume,
		const bool forceLoop, const RPakWavSpatialInfo_t* const spatialInfo,
		const uint32_t scriptVoiceId = 0, const float scriptVolume = 1.0f, const float scriptPitch = 1.0f)
	{
		if (layers.empty())
			return false;

		static std::atomic_uint64_t nextBatchId{ 1 };
		const uint64_t batchId = nextBatchId.fetch_add(1, std::memory_order_relaxed);
		// Keep loop lengths independent. One-shots at a shared rate can start
		// every chosen source at the same sample on one output stream.
		const auto mix = !forceLoop && !(eventFlags & RPAK_WAV_EVENT_FLAG_LOOP)
			? RPakWav_MixOneShotLayers(layers, eventName) : nullptr;
		bool started = false;
		if (!RPakWav_PlayCachedWave(eventGuid, eventFlags, eventName, mix ? mix : layers[0], eventVolume,
			forceLoop, spatialInfo, scriptVoiceId, scriptVolume, scriptPitch, false, &started, batchId,
			mix ? static_cast<uint32_t>(layers.size()) : 1u, mix ? 1u : static_cast<uint32_t>(layers.size())))
		{
			return false;
		}
		if (!started)
			return true; // The complete event was ignored or an existing loop was refreshed.

		for (size_t layerIndex = mix ? layers.size() : 1; layerIndex < layers.size(); ++layerIndex)
		{
			if (!RPakWav_PlayCachedWave(eventGuid, eventFlags, eventName, layers[layerIndex], eventVolume,
				forceLoop, spatialInfo, scriptVoiceId, scriptVolume, scriptPitch, true, nullptr, batchId))
			{
				Warning(eDLL_T::AUDIO, "Packed WAV event '%s' could not start layer %zu of %zu\n",
					eventName ? eventName : "<null>", layerIndex + 1, layers.size());
				std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
				for (auto it = s_rpakWavVoices.begin(); it != s_rpakWavVoices.end();)
				{
					if ((*it)->layerBatchId == batchId)
					{
						RPakWav_CloseVoice(**it, true);
						it = s_rpakWavVoices.erase(it);
					}
					else
						++it;
				}
				return false;
			}
		}
		if (RPakWav_DebugEnabled())
		{
			std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
			size_t activeLayers = 0, spatialLayers = 0, batchOutputs = 0;
			bool sharedBatchOutput = false;
			for (const auto& voice : s_rpakWavVoices)
			{
				if (voice->done || voice->closed)
					continue;
				if (voice->eventGuid == eventGuid)
					activeLayers += voice->sourceLayerCount;
				if (voice->layerBatchId == batchId)
				{
					if (voice->sharedMixer) sharedBatchOutput = true;
					else ++batchOutputs;
					if (voice->spatialStreaming || (voice->sharedMixer && voice->spatial.valid && RPakWav_SpatialEnabled()))
						spatialLayers += voice->sourceLayerCount;
				}
			}
			if (sharedBatchOutput) ++batchOutputs;
			Msg(eDLL_T::AUDIO, "Packed WAV trigger event='%s' batch=%llu selectedLayers=%zu activeLayersForEvent=%zu spatial=%u spatialLayers=%zu effectiveSpatial=%u outputs=%zu mixed=%u\n",
				eventName ? eventName : "<null>", static_cast<unsigned long long>(batchId), layers.size(), activeLayers,
				spatialLayers != 0 ? 1u : 0u, spatialLayers, RPakWav_SpatialEnabled() ? 1u : 0u, batchOutputs, mix ? 1u : 0u);
		}
		return true;
	}

	bool RPakWav_PlayWeaponLoopShot(const PakGuid_t eventGuid, const uint32_t eventFlags, const char* const eventName,
		const std::vector<std::shared_ptr<const RPakWavCachedWave_t>>& layers,
		const float eventVolume, const RPakWavSpatialInfo_t& spatial)
	{
		return RPakWav_PlaySelectedLayers(eventGuid, eventFlags & ~RPAK_WAV_EVENT_FLAG_LOOP,
			eventName, layers, eventVolume, false, &spatial);
	}

	void RPakWav_StartOrRefreshWeaponLoopRepeater(const PakGuid_t eventGuid, const uint32_t eventFlags,
		const char* const eventName, const std::vector<std::shared_ptr<const RPakWavCachedWave_t>>& layers,
		const float eventVolume, const RPakWavSpatialInfo_t& spatial, uint64_t intervalMs)
	{
		if (!eventName || !*eventName || layers.empty())
			return;

		if (intervalMs == 0)
			intervalMs = RPakWav_FireRateToIntervalMs(RPAK_WAV_DEFAULT_WEAPON_FIRE_RATE);

		const uint64_t nowMs = GetTickCount64();
		std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopRepeatersMutex);
		for (RPakWavWeaponLoopRepeater_t& repeater : s_rpakWavWeaponLoopRepeaters)
		{
			if (repeater.eventGuid != eventGuid || !RPakWav_SameEmitter(repeater.spatial, spatial))
				continue;

			repeater.eventFlags = eventFlags & ~RPAK_WAV_EVENT_FLAG_LOOP;
			repeater.eventName = eventName;
			repeater.layers = layers;
			repeater.intervalMs = intervalMs;
			repeater.lastRefreshMs = nowMs;
			repeater.spatial = spatial;
			repeater.eventVolume = RPakWav_NormalizeEventVolume(eventVolume);
			if (repeater.nextPlayMs > nowMs + intervalMs)
				repeater.nextPlayMs = nowMs;

			return;
		}

		s_rpakWavWeaponLoopRepeaters.push_back({
			eventGuid,
			eventFlags & ~RPAK_WAV_EVENT_FLAG_LOOP,
			eventName,
			layers,
			intervalMs,
			nowMs,
			nowMs,
			spatial,
			RPakWav_NormalizeEventVolume(eventVolume)
		});
	}

	size_t RPakWav_StopWeaponLoopRepeatersByEvent(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* spatial)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopRepeatersMutex);
		const size_t oldSize = s_rpakWavWeaponLoopRepeaters.size();
		s_rpakWavWeaponLoopRepeaters.erase(
			std::remove_if(s_rpakWavWeaponLoopRepeaters.begin(), s_rpakWavWeaponLoopRepeaters.end(),
				[eventGuid, spatial](const RPakWavWeaponLoopRepeater_t& repeater)
				{
					return repeater.eventGuid == eventGuid && (!spatial || RPakWav_SameEmitter(repeater.spatial, *spatial));
				}),
			s_rpakWavWeaponLoopRepeaters.end());

		return oldSize - s_rpakWavWeaponLoopRepeaters.size();
	}

	void RPakWav_ClearWeaponLoopRepeaters()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopRepeatersMutex);
		s_rpakWavWeaponLoopRepeaters.clear();
	}

	bool RPakWav_ReadGroupedEventWaves(const RPakWavAudioEventHeader_v2_t* event,
		PakLoadedInfo_s* loadedEventPak, PakFile_s* eventPakFile,
		std::vector<std::shared_ptr<const RPakWavCachedWave_t>>& outLayers);

	void RPakWav_RunWeaponLoopRepeaters()
	{
		struct ShotRequest_t
		{
			PakGuid_t eventGuid;
			uint32_t eventFlags;
			std::string eventName;
			std::vector<std::shared_ptr<const RPakWavCachedWave_t>> layers;
			RPakWavSpatialInfo_t spatial;
			float eventVolume;
		};

		std::vector<ShotRequest_t> shots;
		const uint64_t nowMs = GetTickCount64();
		{
			std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopRepeatersMutex);
			for (auto it = s_rpakWavWeaponLoopRepeaters.begin(); it != s_rpakWavWeaponLoopRepeaters.end();)
			{
				if (nowMs - it->lastRefreshMs > RPAK_WAV_WEAPON_LOOP_IDLE_TIMEOUT_MS)
				{
					if (RPakWav_DebugEnabled())
						Msg(eDLL_T::AUDIO, "Packed WAV weapon loop '%s' expired after missing its stop event\n", it->eventName.c_str());

					it = s_rpakWavWeaponLoopRepeaters.erase(it);
					continue;
				}

				if (nowMs >= it->nextPlayMs)
				{
					const uint64_t intervalMs = it->intervalMs ? it->intervalMs : RPakWav_FireRateToIntervalMs(RPAK_WAV_DEFAULT_WEAPON_FIRE_RATE);
					size_t catchupShots = 0;
					while (nowMs >= it->nextPlayMs && catchupShots < RPAK_WAV_WEAPON_LOOP_MAX_CATCHUP_SHOTS)
					{
						shots.push_back({ it->eventGuid, it->eventFlags, it->eventName, it->layers, it->spatial, it->eventVolume });
						it->nextPlayMs += intervalMs;
						++catchupShots;
					}

					if (nowMs >= it->nextPlayMs)
					{
						if (RPakWav_DebugEnabled())
						{
							Msg(eDLL_T::AUDIO, "Packed WAV weapon loop '%s' dropped delayed catch-up shots after %zu emits\n",
								it->eventName.c_str(), catchupShots);
						}

						it->nextPlayMs = nowMs + intervalMs;
					}
				}

				++it;
			}
		}

		for (ShotRequest_t& shot : shots)
		{
			const RPakWavAudioEventHeader_v2_t* event = nullptr;
			PakLoadedInfo_s* loadedPak = nullptr;
			PakFile_s* pakFile = nullptr;
			if (!RPakWav_FindRegisteredEvent(shot.eventGuid, event, loadedPak, pakFile))
				continue;
			if (event->version == RPAK_WAV_EVENT_GROUP_VERSION &&
				!RPakWav_ReadGroupedEventWaves(event, loadedPak, pakFile, shot.layers))
				continue;
			RPakWav_PlayWeaponLoopShot(shot.eventGuid, shot.eventFlags, shot.eventName.c_str(), shot.layers, shot.eventVolume, shot.spatial);
		}
	}

	const PakGuid_t* RPakWav_GetEventGuidList(const RPakWavAudioEventHeader_v2_t* const event,
		PakLoadedInfo_s* loadedEventPak, PakFile_s* eventPakFile)
	{
		if (!event || event->sourceCount == 0)
			return nullptr;

		PakFile_s* pakFile = eventPakFile;
		if (!pakFile && loadedEventPak)
			pakFile = loadedEventPak->pakFile;

		if (!pakFile)
		{
			PakAssetShort_s* loadedAsset = nullptr;
			RPakWav_FindLoadedAssetByHeader(event, loadedAsset, loadedEventPak);
			if (loadedEventPak)
				pakFile = loadedEventPak->pakFile;
		}

		if (!pakFile)
			return nullptr;

		if (pakFile->IsPageOffsetValid(event->sourceGuids.index, event->sourceGuids.offset))
			return reinterpret_cast<const PakGuid_t*>(pakFile->GetPointerForPageOffset(event->sourceGuids));

		return reinterpret_cast<const PakGuid_t*>(event->sourceGuids.ptr);
	}

	const RPakWavAudioSourceGroup_v1_t* RPakWav_GetEventSourceGroups(
		const RPakWavAudioEventHeader_v2_t* const event,
		PakLoadedInfo_s* const loadedEventPak, PakFile_s* const eventPakFile)
	{
		if (!event || event->version != RPAK_WAV_EVENT_GROUP_VERSION)
			return nullptr;

		const RPakWavAudioEventHeader_v3_t* const grouped =
			reinterpret_cast<const RPakWavAudioEventHeader_v3_t*>(event);
		if (grouped->groupCount == 0 || grouped->groupCount > RPAK_WAV_MAX_SOURCE_GROUPS)
			return nullptr;

		PakFile_s* const pakFile = eventPakFile ? eventPakFile : (loadedEventPak ? loadedEventPak->pakFile : nullptr);
		if (!pakFile)
			return nullptr;

		if (pakFile->IsPageOffsetValid(grouped->sourceGroups.index, grouped->sourceGroups.offset))
			return reinterpret_cast<const RPakWavAudioSourceGroup_v1_t*>(
				pakFile->GetPointerForPageOffset(grouped->sourceGroups));
		return reinterpret_cast<const RPakWavAudioSourceGroup_v1_t*>(grouped->sourceGroups.ptr);
	}

	bool RPakWav_ReadGroupedEventWaves(const RPakWavAudioEventHeader_v2_t* const event,
		PakLoadedInfo_s* const loadedEventPak, PakFile_s* const eventPakFile,
		std::vector<std::shared_ptr<const RPakWavCachedWave_t>>& outLayers)
	{
		outLayers.clear();
		const RPakWavAudioSourceGroup_v1_t* const groups =
			RPakWav_GetEventSourceGroups(event, loadedEventPak, eventPakFile);
		if (!groups || event->sourceCount == 0 || event->sourceCount > 256)
			return false;

		const RPakWavAudioEventHeader_v3_t* const grouped =
			reinterpret_cast<const RPakWavAudioEventHeader_v3_t*>(event);
		PakFile_s* const pakFile = eventPakFile ? eventPakFile : (loadedEventPak ? loadedEventPak->pakFile : nullptr);
		if (!pakFile)
			return false;
		const float* const weights = pakFile->IsPageOffsetValid(grouped->sourceWeights.index, grouped->sourceWeights.offset)
			? reinterpret_cast<const float*>(pakFile->GetPointerForPageOffset(grouped->sourceWeights))
			: reinterpret_cast<const float*>(grouped->sourceWeights.ptr);
		if (!weights)
			return false;
		uint32_t expectedIndex = 0;
		for (uint32_t groupIndex = 0; groupIndex < grouped->groupCount; ++groupIndex)
		{
			const RPakWavAudioSourceGroup_v1_t& group = groups[groupIndex];
			if (group.firstSourceIndex != expectedIndex || group.sourceCount == 0 ||
				group.sourceCount > event->sourceCount - expectedIndex)
			{
				outLayers.clear();
				return false;
			}
			expectedIndex += group.sourceCount;
		}
		if (expectedIndex != event->sourceCount)
			return false;
		for (uint32_t i = 0; i < event->sourceCount; ++i)
		{
			if (!std::isfinite(weights[i]) || weights[i] < 0.0f || weights[i] > 1000000.0f)
				return false;
		}

		outLayers.reserve(grouped->groupCount);
		for (uint32_t groupIndex = 0; groupIndex < grouped->groupCount; ++groupIndex)
		{
			const RPakWavAudioSourceGroup_v1_t& group = groups[groupIndex];
			double totalWeight = 0.0;
			for (uint32_t i = 0; i < group.sourceCount; ++i)
				totalWeight += weights[group.firstSourceIndex + i];
			if (totalWeight <= 0.0)
			{
				outLayers.clear();
				return false;
			}
			double selection = (static_cast<double>(RPakWav_NextRandom()) / 4294967296.0) * totalWeight;
			uint32_t randomOffset = 0;
			for (; randomOffset + 1 < group.sourceCount; ++randomOffset)
			{
				selection -= weights[group.firstSourceIndex + randomOffset];
				if (selection < 0.0)
					break;
			}
			bool loaded = false;
			for (uint32_t attempt = 0; attempt < group.sourceCount; ++attempt)
			{
				const uint32_t sourceIndex = group.firstSourceIndex + ((randomOffset + attempt) % group.sourceCount);
				if (weights[sourceIndex] == 0.0f)
					continue;
				std::shared_ptr<const RPakWavCachedWave_t> wave;
				if (!RPakWav_ReadSpecificEventSourceWave(event, loadedEventPak, eventPakFile,
					sourceIndex, 0, wave, nullptr))
					continue;

				outLayers.push_back(std::move(wave));
				loaded = true;
				break;
			}
			if (!loaded)
			{
				outLayers.clear();
				return false;
			}
		}

		return true;
	}

	bool RPakWav_HandleControlEvent(const char* const eventName, const RPakWavAudioEventHeader_v2_t* const event,
		PakLoadedInfo_s* const loadedEventPak, PakFile_s* const eventPakFile)
	{
		if (!event || !RPakWav_IsControlMode(event->mode))
			return false;

		switch (event->mode)
		{
		case RPAK_WAV_EVENT_MODE_STOP_EVENTS:
		{
			const PakGuid_t* const targetGuids = RPakWav_GetEventGuidList(event, loadedEventPak, eventPakFile);
			if (!targetGuids)
				return false;

			for (uint32_t i = 0; i < event->sourceCount; ++i)
			{
				RPakWav_RemovePendingEventsByEvent(targetGuids[i]);
				RPakWav_RemoveQueuedPlayRequestsByEvent(targetGuids[i]);
				RPakWav_StopWeaponLoopRepeatersByEvent(targetGuids[i]);
				RPakWav_StopVoicesByEvent(targetGuids[i]);
			}

			break;
		}
		case RPAK_WAV_EVENT_MODE_STOP_MUSIC:
			RPakWav_StopVoicesByFlags(RPAK_WAV_EVENT_FLAG_MUSIC);
			break;
		case RPAK_WAV_EVENT_MODE_STOP_ALL:
			RPakWav_StopAllVoices();
			break;
		case RPAK_WAV_EVENT_MODE_STOP_MANAGED:
			RPakWav_StopVoicesByFlags(RPAK_WAV_EVENT_FLAG_MANAGED);
			break;
		default:
			return false;
		}

		if (RPakWav_DebugEnabled())
			Msg(eDLL_T::AUDIO, "Handled packed WAV control event '%s' (mode %u)\n", eventName, event->mode);

		return true;
	}


	bool RPakWav_FindEventForName(const char* const eventName, const bool allowLoadedPakFallback,
		const RPakWavAudioEventHeader_v2_t*& outEvent, PakLoadedInfo_s*& outLoadedPak, PakFile_s*& outPakFile)
	{
		outEvent = nullptr;
		outLoadedPak = nullptr;
		outPakFile = nullptr;

		if (!eventName || !*eventName)
			return false;

		if (!allowLoadedPakFallback && !RPakWav_HasRegisteredEvents())
			return false;

		const PakGuid_t eventGuid = Pak_StringToGuid(eventName);
		if (RPakWav_FindRegisteredEvent(eventGuid, outEvent, outLoadedPak, outPakFile))
			return true;

		if (!allowLoadedPakFallback || !RPakWav_ShouldUseLoadedPakFallback(eventName))
			return false;

		void* const eventAsset = RPakWav_FindLoadedHeaderByGuid(eventGuid, RPAK_WAV_AEVT_ASSET_TYPE, outLoadedPak);
		outEvent = reinterpret_cast<const RPakWavAudioEventHeader_v2_t*>(eventAsset);
		outPakFile = outLoadedPak ? outLoadedPak->pakFile : nullptr;
		return outEvent != nullptr;
	}

	bool RPakWav_TryPlayEvent(const char* const eventName, const bool warnOnFailure = true,
		bool* const outRecognizedPackedEvent = nullptr, const bool allowLoadedPakFallback = false,
		const bool suppressWeaponLoop = false, const RPakWavSpatialInfo_t* const spatialInfo = nullptr)
	{
		if (outRecognizedPackedEvent)
			*outRecognizedPackedEvent = false;

		if (!eventName || !*eventName || !g_pakLoadApi)
			return false;

		const auto warnFailed = [eventName, warnOnFailure](const char* const reason)
		{
			if (warnOnFailure && (miles_debug.GetBool() || miles_warnings.GetBool()))
				Warning(eDLL_T::AUDIO, "Packed WAV event '%s' not handled: %s\n", eventName, reason);
		};

		PakLoadedInfo_s* loadedEventPak = nullptr;
		PakFile_s* eventPakFile = nullptr;
		const RPakWavAudioEventHeader_v2_t* event = nullptr;
		if (!RPakWav_FindEventForName(eventName, allowLoadedPakFallback, event, loadedEventPak, eventPakFile))
			return false;

		if (!RPakWav_IsEventHeader(event))
		{
			warnFailed("event asset header did not match aevt v4/v5");
			return false;
		}

		if (outRecognizedPackedEvent)
			*outRecognizedPackedEvent = true;

		if (RPakWav_HandleControlEvent(eventName, event, loadedEventPak, eventPakFile))
			return true;

		const bool loopForWeaponText = !suppressWeaponLoop && RPakWav_IsWeaponLoopEventName(eventName);

		std::vector<std::shared_ptr<const RPakWavCachedWave_t>> layers;
		RPakWavSelectedSource_t selectedSource;
		bool sourcesRead = false;
		if (event->version == RPAK_WAV_EVENT_GROUP_VERSION)
		{
			sourcesRead = RPakWav_ReadGroupedEventWaves(event, loadedEventPak, eventPakFile, layers);
		}
		else
		{
			std::shared_ptr<const RPakWavCachedWave_t> wave;
			sourcesRead = RPakWav_ReadEventSourceWave(event, loadedEventPak, eventPakFile, wave, &selectedSource);
			if (sourcesRead)
				layers.push_back(std::move(wave));
		}
		if (!sourcesRead)
		{
			warnFailed("failed to read or cache one or more source WAVs");
			return false;
		}

		if (RPakWav_DebugEnabled())
		{
			if (event->version == RPAK_WAV_EVENT_GROUP_VERSION)
			{
				Msg(eDLL_T::AUDIO, "Playing packed WAV event '%s' with %zu independently selected layers\n",
					eventName, layers.size());
			}
			else if (loopForWeaponText)
			{
				Msg(eDLL_T::AUDIO, "Repeating packed WAV weapon event '%s' every %llums from source 0x%llX/%u (%u Hz, %u ch)\n",
					eventName,
					RPakWav_GetWeaponLoopIntervalMs(event->eventGuid),
					selectedSource.sourceGuid,
					event->sourceCount,
					selectedSource.sampleRate,
					selectedSource.channels);
			}
			else
			{
				Msg(eDLL_T::AUDIO, "Playing packed WAV event '%s' source 0x%llX/%u (%u Hz, %u ch)\n",
					eventName,
					selectedSource.sourceGuid,
					event->sourceCount,
					selectedSource.sampleRate,
					selectedSource.channels);
			}
		}

		const RPakWavSpatialInfo_t spatial = spatialInfo ? *spatialInfo : RPakWav_CaptureQueuedSpatialInfo();
		if (loopForWeaponText)
		{
			RPakWav_StartOrRefreshWeaponLoopRepeater(event->eventGuid, event->flags, eventName, layers, event->volume, spatial,
				RPakWav_GetWeaponLoopIntervalMs(event->eventGuid));
			return true;
		}

		if (!RPakWav_PlaySelectedLayers(event->eventGuid, event->flags, eventName,
			layers, event->volume, false, &spatial))
		{
			warnFailed("failed to submit WAV bytes to waveOut");
			return false;
		}

		return true;
	}

	void RPakWav_ClearPlayQueue()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavPlayQueueMutex);
		s_rpakWavPlayQueue.clear();
		s_rpakWavPlayQueueOverflowWarned.store(false, std::memory_order_release);
	}

	bool RPakWav_QueuePlayRequest(const char* const eventName, const bool warnOnFailure,
		const bool allowLoadedPakFallback, const bool suppressWeaponLoop, const RPakWavSpatialInfo_t& spatial)
	{
		if (!eventName || !*eventName)
			return false;

		{
			std::lock_guard<std::mutex> lock(s_rpakWavPlayQueueMutex);
			if (s_rpakWavPlayQueue.size() >= RPAK_WAV_MAX_PLAY_REQUESTS)
			{
				if (!s_rpakWavPlayQueueOverflowWarned.exchange(true, std::memory_order_acq_rel))
					Warning(eDLL_T::AUDIO, "Packed WAV playback queue is full; dropping oldest queued request\n");

				s_rpakWavPlayQueue.pop_front();
			}

			s_rpakWavPlayQueue.push_back({ eventName, warnOnFailure, allowLoadedPakFallback, suppressWeaponLoop, spatial });
		}

		return true;
	}

	bool RPakWav_TryScheduleEvent(const char* const eventName, const bool warnOnFailure = true,
		bool* const outRecognizedPackedEvent = nullptr, const bool allowLoadedPakFallback = false,
		const bool suppressWeaponLoop = false, const RPakWavSpatialInfo_t* const spatialInfo = nullptr)
	{
		if (outRecognizedPackedEvent)
			*outRecognizedPackedEvent = false;

		if (!eventName || !*eventName || !g_pakLoadApi)
			return false;

		const auto warnFailed = [eventName, warnOnFailure](const char* const reason)
		{
			if (warnOnFailure && (miles_debug.GetBool() || miles_warnings.GetBool()))
				Warning(eDLL_T::AUDIO, "Packed WAV event '%s' not scheduled: %s\n", eventName, reason);
		};

		PakLoadedInfo_s* loadedEventPak = nullptr;
		PakFile_s* eventPakFile = nullptr;
		const RPakWavAudioEventHeader_v2_t* event = nullptr;
		if (!RPakWav_FindEventForName(eventName, allowLoadedPakFallback, event, loadedEventPak, eventPakFile))
			return false;

		if (outRecognizedPackedEvent)
			*outRecognizedPackedEvent = true;

		if (!RPakWav_IsEventHeader(event))
		{
			warnFailed("event asset header did not match aevt v4");
			return false;
		}

		if (RPakWav_HandleControlEvent(eventName, event, loadedEventPak, eventPakFile))
			return true;

		if (RPakWav_DebugEnabled())
			Msg(eDLL_T::AUDIO, "Scheduling packed WAV event '%s'\n", eventName);

		const RPakWavSpatialInfo_t spatial = spatialInfo ? *spatialInfo : RPakWav_CaptureQueuedSpatialInfo();
		return RPakWav_QueuePlayRequest(eventName, warnOnFailure, allowLoadedPakFallback, suppressWeaponLoop, spatial);
	}

	void RPakWav_RunQueuedPlayRequests()
	{
		for (size_t requestIndex = 0; requestIndex < RPAK_WAV_MAX_PLAY_REQUESTS; ++requestIndex)
		{
			RPakWavPlayRequest_t request;
			{
				std::lock_guard<std::mutex> lock(s_rpakWavPlayQueueMutex);
				if (s_rpakWavPlayQueue.empty())
					break;

				request = std::move(s_rpakWavPlayQueue.front());
				s_rpakWavPlayQueue.pop_front();
			}

			RPakWav_TryPlayEvent(request.eventName.c_str(), request.warnOnFailure, nullptr,
				request.allowLoadedPakFallback, request.suppressWeaponLoop, &request.spatial);
		}
	}

	void RPakWav_QueuePendingEvent(const char* const eventName, const bool suppressWeaponLoop,
		const RPakWavSpatialInfo_t& spatial)
	{
		if (!eventName || !*eventName)
			return;

		std::lock_guard<std::mutex> lock(s_rpakWavPendingEventsMutex);
		for (RPakWavPendingEvent_t& pending : s_rpakWavPendingEvents)
		{
			if (pending.eventName == eventName && RPakWav_SameEmitter(pending.spatial, spatial))
			{
				pending.framesRemaining = RPAK_WAV_PENDING_EVENT_FRAMES;
				pending.suppressWeaponLoop = pending.suppressWeaponLoop || suppressWeaponLoop;
				pending.spatial = spatial;
				return;
			}
		}

		s_rpakWavPendingEvents.push_back({ eventName, RPAK_WAV_PENDING_EVENT_FRAMES, suppressWeaponLoop, spatial });

		if (RPakWav_DebugEnabled())
			Msg(eDLL_T::AUDIO, "Queued packed WAV event '%s' until its audio pak is loaded\n", eventName);
	}

	void RPakWav_RunPendingEvents()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavPendingEventsMutex);
		for (auto it = s_rpakWavPendingEvents.begin(); it != s_rpakWavPendingEvents.end();)
		{
			if (RPakWav_TryScheduleEvent(it->eventName.c_str(), false, nullptr, true, it->suppressWeaponLoop, &it->spatial))
			{
				it = s_rpakWavPendingEvents.erase(it);
				continue;
			}

			if (--it->framesRemaining <= 0)
			{
				it = s_rpakWavPendingEvents.erase(it);
				continue;
			}

			++it;
		}
	}

	void RPakWav_ClearPendingEvents()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavPendingEventsMutex);
		s_rpakWavPendingEvents.clear();
	}

	const char* AudioScript_GetPakString(const PakPage_u& page, PakFile_s* const pakFile)
	{
		if (pakFile && pakFile->IsPageOffsetValid(page.index, page.offset))
			return reinterpret_cast<const char*>(pakFile->GetPointerForPageOffset(page));

		return reinterpret_cast<const char*>(page.ptr);
	}

	uint32_t AudioScript_ReserveVoice(const AudioScriptBackend_e backend, const char* const eventName,
		const PakGuid_t eventGuid, const uint32_t sourceIndex, const PakGuid_t sourceGuid,
		const float volume, const float pitch, const bool forceLoop)
	{
		std::lock_guard<std::mutex> queueLock(s_audioScriptPlayQueueMutex);
		if (s_audioScriptPlayQueue.size() >= AUDIO_SCRIPT_MAX_PLAY_REQUESTS)
			return 0;

		std::lock_guard<std::mutex> voiceLock(s_audioScriptVoicesMutex);
		for (auto it = s_audioScriptVoices.begin();
			s_audioScriptVoices.size() >= AUDIO_SCRIPT_MAX_VOICES && it != s_audioScriptVoices.end();)
		{
			if (it->state == AudioScriptVoiceState_e::Failed || it->state == AudioScriptVoiceState_e::Stopped)
				it = s_audioScriptVoices.erase(it);
			else
				++it;
		}

		if (s_audioScriptVoices.size() >= AUDIO_SCRIPT_MAX_VOICES)
			return 0;

		uint32_t voiceId = s_audioScriptNextVoiceId.fetch_add(1, std::memory_order_acq_rel);
		if (voiceId == 0)
			voiceId = s_audioScriptNextVoiceId.fetch_add(1, std::memory_order_acq_rel);

		s_audioScriptVoices.push_back({
			voiceId,
			s_audioScriptGeneration.load(std::memory_order_acquire),
			backend,
			AudioScriptVoiceState_e::Pending,
			eventGuid,
			sourceGuid,
			sourceIndex,
			eventName ? eventName : "",
			nullptr,
			AudioScript_ClampVolume(volume),
			AudioScript_ClampPitch(pitch),
			forceLoop,
			false,
			0
			});
		s_audioScriptPlayQueue.push_back({ voiceId });
		return static_cast<int>(voiceId);
	}

	void AudioScript_MarkVoiceFailed(const uint32_t voiceId, const char* const reason)
	{
		{
			std::lock_guard<std::mutex> lock(s_audioScriptVoicesMutex);
			AudioScriptVoiceEntry_t* const voice = AudioScript_FindVoiceLocked(voiceId);
			if (voice)
			{
				voice->state = AudioScriptVoiceState_e::Failed;
				voice->remainingCustomLayers = 0;
			}
		}

		Warning(eDLL_T::AUDIO, "PlayAudio voice %u failed: %s\n", voiceId, reason);
	}

	void AudioScript_RunCustomRequest(const AudioScriptVoiceEntry_t& request)
	{
		if (!rpakwav_enable.GetBool())
		{
			AudioScript_MarkVoiceFailed(request.voiceId, "custom RPAK audio is disabled by rpakwav_enable");
			return;
		}

		const RPakWavAudioEventHeader_v2_t* event = nullptr;
		PakLoadedInfo_s* loadedEventPak = nullptr;
		PakFile_s* eventPakFile = nullptr;
		if (!RPakWav_FindRegisteredEvent(request.eventGuid, event, loadedEventPak, eventPakFile))
		{
			AudioScript_MarkVoiceFailed(request.voiceId, "custom aevt is no longer loaded");
			return;
		}

		const char* eventName = AudioScript_GetPakString(event->eventName, eventPakFile);
		if (!eventName || !*eventName)
			eventName = request.eventName.c_str();

		if (RPakWav_HandleControlEvent(eventName, event, loadedEventPak, eventPakFile))
		{
			std::lock_guard<std::mutex> lock(s_audioScriptVoicesMutex);
			if (AudioScriptVoiceEntry_t* const voice = AudioScript_FindVoiceLocked(request.voiceId))
				voice->state = AudioScriptVoiceState_e::Stopped;
			return;
		}

		std::vector<std::shared_ptr<const RPakWavCachedWave_t>> layers;
		bool sourceRead = false;
		if (request.sourceIndex == AUDIO_SCRIPT_RANDOM_SOURCE && event->version == RPAK_WAV_EVENT_GROUP_VERSION)
		{
			sourceRead = RPakWav_ReadGroupedEventWaves(event, loadedEventPak, eventPakFile, layers);
		}
		else
		{
			std::shared_ptr<const RPakWavCachedWave_t> wave;
			sourceRead = request.sourceIndex == AUDIO_SCRIPT_RANDOM_SOURCE
				? RPakWav_ReadEventSourceWave(event, loadedEventPak, eventPakFile, wave, nullptr)
				: RPakWav_ReadSpecificEventSourceWave(event, loadedEventPak, eventPakFile,
					request.sourceIndex, request.sourceGuid, wave, nullptr);
			if (sourceRead)
				layers.push_back(std::move(wave));
		}
		if (!sourceRead)
		{
			AudioScript_MarkVoiceFailed(request.voiceId, "failed to read the requested custom source");
			return;
		}

		const float eventPitch = RPakWav_ClampFiniteFloat(event->pitch, 1.0f,
			AUDIO_SCRIPT_MIN_PITCH, AUDIO_SCRIPT_MAX_PITCH);
		const float effectivePitch = AudioScript_ClampPitch(eventPitch * request.pitch);
		{
			std::lock_guard<std::mutex> lock(s_audioScriptVoicesMutex);
			AudioScriptVoiceEntry_t* const voice = AudioScript_FindVoiceLocked(request.voiceId);
			if (!voice || voice->cancelled)
				return;
			voice->remainingCustomLayers = static_cast<uint32_t>(layers.size());
		}
		if (!RPakWav_PlaySelectedLayers(event->eventGuid, event->flags, eventName, layers,
			event->volume, request.forceLoop, nullptr, request.voiceId, request.volume, effectivePitch))
		{
			RPakWav_StopScriptVoice(request.voiceId);
			AudioScript_MarkVoiceFailed(request.voiceId, "waveOut rejected one or more custom layers");
			return;
		}

		bool cancelled = false;
		{
			std::lock_guard<std::mutex> lock(s_audioScriptVoicesMutex);
			if (AudioScriptVoiceEntry_t* const voice = AudioScript_FindVoiceLocked(request.voiceId))
			{
				cancelled = voice->cancelled;
				voice->state = cancelled ? AudioScriptVoiceState_e::Stopped : AudioScriptVoiceState_e::Active;
			}
		}

		if (cancelled)
			RPakWav_StopScriptVoice(request.voiceId);
	}

	void AudioScript_RunNativeRequest(const AudioScriptVoiceEntry_t& request)
	{
		if (!g_milesGlobals || !g_milesGlobals->queue || !v_MilesQueueEventRun ||
			!v_MilesQueueEventVolume || !v_MilesQueueEventRateFactor)
		{
			AudioScript_MarkVoiceFailed(request.voiceId, "Miles queue is unavailable");
			return;
		}

		Miles::Queue* const queue = reinterpret_cast<Miles::Queue*>(g_milesGlobals->queue);
		v_MilesQueueEventVolume(queue, request.volume);
		v_MilesQueueEventRateFactor(queue, request.pitch);
		void* const nativeEventId = v_MilesQueueEventRun(queue, request.eventName.c_str());
		if (!nativeEventId)
		{
			AudioScript_MarkVoiceFailed(request.voiceId, "Miles rejected the native event");
			return;
		}

		bool cancelled = false;
		{
			std::lock_guard<std::mutex> lock(s_audioScriptVoicesMutex);
			if (AudioScriptVoiceEntry_t* const voice = AudioScript_FindVoiceLocked(request.voiceId))
			{
				voice->nativeEventId = nativeEventId;
				cancelled = voice->cancelled;
				voice->state = cancelled ? AudioScriptVoiceState_e::Stopped : AudioScriptVoiceState_e::Active;
			}
		}

		if (cancelled && v_MilesQueueEventSignal)
			v_MilesQueueEventSignal(queue, nativeEventId, "stop");
	}

	void AudioScript_RunQueuedPlayRequests()
	{
		for (size_t requestIndex = 0; requestIndex < AUDIO_SCRIPT_MAX_PLAY_REQUESTS; ++requestIndex)
		{
			AudioScriptPlayRequest_t queuedRequest{};
			{
				std::lock_guard<std::mutex> lock(s_audioScriptPlayQueueMutex);
				if (s_audioScriptPlayQueue.empty())
					break;

				queuedRequest = s_audioScriptPlayQueue.front();
				s_audioScriptPlayQueue.pop_front();
			}

			AudioScriptVoiceEntry_t request{};
			{
				std::lock_guard<std::mutex> lock(s_audioScriptVoicesMutex);
				AudioScriptVoiceEntry_t* const voice = AudioScript_FindVoiceLocked(queuedRequest.voiceId);
				if (!voice || voice->cancelled || voice->generation != s_audioScriptGeneration.load(std::memory_order_acquire))
					continue;

				voice->state = AudioScriptVoiceState_e::Starting;
				request = *voice;
			}

			if (request.backend == AudioScriptBackend_e::NativeMiles)
				AudioScript_RunNativeRequest(request);
			else
				AudioScript_RunCustomRequest(request);
		}
	}
}

static void RPakWav_Status_f(const CCommand& args)
{
	RPakWav_ReportSpatialSetting(true);
	std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
	size_t active = 0, spatial = 0, outputs = 0, mixerVoices = 0;
	for (const auto& voice : s_rpakWavVoices)
	{
		if (!voice || voice->done || voice->closed ||
			(args.ArgC() > 1 && V_stricmp(voice->eventName.c_str(), args.Arg(1))))
			continue;
		if (voice->sharedMixer) ++mixerVoices;
		else ++outputs;
		active += voice->sourceLayerCount;
		spatial += (voice->spatialStreaming || (voice->sharedMixer && !voice->firstPersonPlayback &&
			voice->spatial.valid && RPakWav_SpatialEnabled())) ? voice->sourceLayerCount : 0;
		Msg(eDLL_T::AUDIO, "Packed WAV active event='%s' batch=%llu backend=%s rate=%lu ch=%u bits=%u underruns=%u layers=%u group=%s emitter=%u priority=%.1f firstPerson=%u centered1p=%u\n",
			voice->eventName.c_str(), static_cast<unsigned long long>(voice->layerBatchId),
			voice->sharedMixer ? "shared-mixer" : (voice->spatialStreaming ? "spatial-stream" : "whole-clip"),
			static_cast<unsigned long>(voice->playbackFormat.nSamplesPerSec),
			static_cast<unsigned int>(voice->playbackFormat.nChannels),
			static_cast<unsigned int>(voice->playbackFormat.wBitsPerSample), voice->spatialUnderruns, voice->sourceLayerCount,
			RPakWav_MixerGroupName(voice->mixerGroup), voice->spatial.emitterEntity,
			voice->mixerPriority, voice->mixerFirstPerson ? 1u : 0u, voice->firstPersonPlayback ? 1u : 0u);
	}
	if (mixerVoices) ++outputs;
	Msg(eDLL_T::AUDIO, "Packed WAV status activeLayers=%zu spatialLayers=%zu outputs=%zu mixerVoices=%zu\n",
		active, spatial, outputs, mixerVoices);
	Msg(eDLL_T::AUDIO, "Packed WAV mixer enabled=%u device=%u underruns=%u voiceSteals=%u rejectedVoices=%u maxVoices=%zu emitterBatches=%zu reservedWeaponVoices=%zu\n",
		RPakWav_SharedMixerEnabled() ? 1u : 0u, s_rpakWavMixerOutput ? 1u : 0u,
		s_rpakWavMixerUnderruns, s_rpakWavMixerStolenVoices, s_rpakWavMixerRejectedVoices,
		RPAK_WAV_MIXER_MAX_VOICES, RPAK_WAV_MIXER_MAX_EMITTER_BATCHES, RPAK_WAV_MIXER_RESERVED_WEAPON_VOICES);
	for (size_t index = 0; index < RPAK_WAV_MIXER_GROUP_COUNT; ++index)
	{
		const auto& group = s_rpakWavMixerGroups[index];
		Msg(eDLL_T::AUDIO, "Packed WAV mixer group=%s inputPeak=%.4f limiterGain=%.4f limitedFrames=%llu\n",
			RPakWav_MixerGroupName(static_cast<RPakWavMixerGroup_e>(index)), group.inputPeak,
			group.gain, static_cast<unsigned long long>(group.limitedFrames));
	}
}

static ConCommand rpakwav_status("rpakwav_status", RPakWav_Status_f,
	"Prints the effective spatial setting and active custom voices; optional event name filter", FCVAR_RELEASE);

uint32_t AudioScript_GetGeneration()
{
	return s_audioScriptGeneration.load(std::memory_order_acquire);
}

static void AudioScript_CopyDisplayName(char* const destination, const PakGuid_t guid, const char* const name)
{
	if (name && *name)
	{
		V_strncpy(destination, name, AUDIO_SCRIPT_NAME_MAX);
		return;
	}

	V_snprintf(destination, AUDIO_SCRIPT_NAME_MAX, "0x%016llX", guid);
}

size_t AudioScript_ListCustomEvents(AudioScriptEventInfo_s* const events, const size_t capacity)
{
	const RPakWavEventRegistrySnapshot_t* const snapshot =
		s_rpakWavEventRegistry.load(std::memory_order_acquire);
	if (!snapshot)
		return 0;

	size_t validCount = 0;
	for (uint32_t i = 0; i < snapshot->count; ++i)
	{
		const RPakWavEventLookup_t& lookup = snapshot->entries[i];
		if (!RPakWav_LoadedPakIsUsable(lookup.loadedPak) ||
			(lookup.loadedPak->pakFile && lookup.loadedPak->pakFile != lookup.pakFile) ||
			!RPakWav_IsEventHeader(lookup.event))
		{
			continue;
		}

		if (events && validCount < capacity)
		{
			AudioScriptEventInfo_s& info = events[validCount];
			info.eventGuid = lookup.eventGuid;
			info.generation = AudioScript_GetGeneration();
			info.sourceCount = lookup.event->sourceCount;
			AudioScript_CopyDisplayName(info.name, info.eventGuid,
				AudioScript_GetPakString(lookup.event->eventName, lookup.pakFile));
		}

		++validCount;
	}

	return validCount;
}

bool AudioScript_FindCustomEvent(const char* const eventName, AudioScriptEventInfo_s& info)
{
	if (!eventName || !*eventName)
		return false;

	const RPakWavAudioEventHeader_v2_t* event = nullptr;
	PakLoadedInfo_s* loadedPak = nullptr;
	PakFile_s* pakFile = nullptr;
	if (!RPakWav_FindRegisteredEvent(Pak_StringToGuid(eventName), event, loadedPak, pakFile))
		return false;

	info.eventGuid = event->eventGuid;
	info.generation = AudioScript_GetGeneration();
	info.sourceCount = event->sourceCount;
	AudioScript_CopyDisplayName(info.name, info.eventGuid,
		AudioScript_GetPakString(event->eventName, pakFile));
	return true;
}

bool AudioScript_FindCustomSource(const char* const sourceName, AudioScriptSourceInfo_s& info)
{
	if (!sourceName || !*sourceName)
		return false;

	const RPakWavAudioSourceHeader_v1_t* source = nullptr;
	PakLoadedInfo_s* loadedPak = nullptr;
	if (!RPakWav_FindRegisteredSource(Pak_StringToGuid(sourceName), source, loadedPak))
		return false;

	info.eventGuid = 0;
	info.sourceGuid = source->sourceGuid;
	info.generation = AudioScript_GetGeneration();
	info.sourceIndex = AUDIO_SCRIPT_RANDOM_SOURCE;
	info.duration = source->sampleRate
		? static_cast<float>(source->sampleCount) / static_cast<float>(source->sampleRate)
		: 0.0f;
	AudioScript_CopyDisplayName(info.name, info.sourceGuid,
		AudioScript_GetPakString(source->name, loadedPak ? loadedPak->pakFile : nullptr));
	return true;
}

size_t AudioScript_ListCustomSources(const PakGuid_t eventGuid, AudioScriptSourceInfo_s* const sources,
	const size_t capacity)
{
	const RPakWavAudioEventHeader_v2_t* event = nullptr;
	PakLoadedInfo_s* loadedEventPak = nullptr;
	PakFile_s* eventPakFile = nullptr;
	if (!RPakWav_FindRegisteredEvent(eventGuid, event, loadedEventPak, eventPakFile))
		return 0;

	const PakGuid_t* const sourceGuids = RPakWav_GetEventGuidList(event, loadedEventPak, eventPakFile);
	size_t validCount = 0;
	for (uint32_t sourceIndex = 0; sourceIndex < event->sourceCount; ++sourceIndex)
	{
		PakGuid_t sourceGuid = sourceGuids ? sourceGuids[sourceIndex] : 0;
		if (!sourceGuid && sourceIndex == 0)
			sourceGuid = event->firstSourceGuid;

		const RPakWavAudioSourceHeader_v1_t* source = nullptr;
		PakLoadedInfo_s* loadedSourcePak = nullptr;
		if (!sourceGuid || !RPakWav_FindRegisteredSource(sourceGuid, source, loadedSourcePak))
			continue;

		if (sources && validCount < capacity)
		{
			AudioScriptSourceInfo_s& info = sources[validCount];
			info.eventGuid = eventGuid;
			info.sourceGuid = sourceGuid;
			info.generation = AudioScript_GetGeneration();
			info.sourceIndex = sourceIndex;
			info.duration = source->sampleRate
				? static_cast<float>(source->sampleCount) / static_cast<float>(source->sampleRate)
				: 0.0f;
			AudioScript_CopyDisplayName(info.name, sourceGuid,
				AudioScript_GetPakString(source->name, loadedSourcePak ? loadedSourcePak->pakFile : nullptr));
		}

		++validCount;
	}

	return validCount;
}

bool AudioScript_IsNativeEvent(const char* const eventName)
{
	if (!eventName || !*eventName || !g_milesGlobals)
		return false;

	for (int bankIndex = 0; bankIndex < CSOM_MAX_LOADED_BANKS; ++bankIndex)
	{
		Miles::Bank* const bank = reinterpret_cast<Miles::Bank*>(g_milesGlobals->loadedBanks[bankIndex]);
		if (!bank)
			continue;

		if (v_MilesBankGetEventCount && v_MilesBankGetEventName)
		{
			const int eventCount = v_MilesBankGetEventCount(bank);
			if (eventCount < 0 || eventCount > 1000000)
				continue;

			for (int eventIndex = 0; eventIndex < eventCount; ++eventIndex)
			{
				const char* const candidate = v_MilesBankGetEventName(bank, eventIndex);
				if (candidate && V_stricmp(candidate, eventName) == 0)
					return true;
			}

			continue;
		}

		const Miles::BankHeader_t* const header = bank->GetHeader();
		if (!header || !header->events || !header->stringTable || header->eventCount > 1000000)
			continue;

		for (uint32_t eventIndex = 0; eventIndex < header->eventCount; ++eventIndex)
		{
			const char* const candidate = header->stringTable + header->events[eventIndex].nameOffset;
			if (candidate && V_stricmp(candidate, eventName) == 0)
				return true;
		}
	}

	return false;
}

int AudioScript_PlayNative(const char* const eventName, const float volume, const float pitch, const bool forceLoop)
{
	if (forceLoop || !AudioScript_IsNativeEvent(eventName))
		return 0;

	return AudioScript_ReserveVoice(AudioScriptBackend_e::NativeMiles, eventName, 0,
		AUDIO_SCRIPT_RANDOM_SOURCE, 0, volume, pitch, false);
}

int AudioScript_PlayCustom(const PakGuid_t eventGuid, const uint32_t sourceIndex, const PakGuid_t sourceGuid,
	const float volume, const float pitch, const bool forceLoop)
{
	const RPakWavAudioEventHeader_v2_t* event = nullptr;
	PakLoadedInfo_s* loadedEventPak = nullptr;
	PakFile_s* eventPakFile = nullptr;
	if (!RPakWav_FindRegisteredEvent(eventGuid, event, loadedEventPak, eventPakFile))
		return 0;

	if (sourceIndex != AUDIO_SCRIPT_RANDOM_SOURCE)
	{
		if (sourceIndex >= event->sourceCount)
			return 0;

		const PakGuid_t* const sourceGuids = RPakWav_GetEventGuidList(event, loadedEventPak, eventPakFile);
		PakGuid_t currentSourceGuid = sourceGuids ? sourceGuids[sourceIndex] : 0;
		if (!currentSourceGuid && sourceIndex == 0)
			currentSourceGuid = event->firstSourceGuid;
		if (!currentSourceGuid || currentSourceGuid != sourceGuid)
			return 0;
	}

	const char* const eventName = AudioScript_GetPakString(event->eventName, eventPakFile);
	return AudioScript_ReserveVoice(AudioScriptBackend_e::CustomRPak, eventName, eventGuid,
		sourceIndex, sourceGuid, volume, pitch, forceLoop);
}

bool AudioScript_SetPitch(const int voiceId, const float pitch)
{
	if (voiceId <= 0)
		return false;

	AudioScriptBackend_e backend = AudioScriptBackend_e::CustomRPak;
	AudioScriptVoiceState_e state = AudioScriptVoiceState_e::Failed;
	void* nativeEventId = nullptr;
	{
		std::lock_guard<std::mutex> lock(s_audioScriptVoicesMutex);
		AudioScriptVoiceEntry_t* const voice = AudioScript_FindVoiceLocked(static_cast<uint32_t>(voiceId));
		if (!voice || voice->generation != AudioScript_GetGeneration())
			return false;
		if (voice->state == AudioScriptVoiceState_e::Failed || voice->state == AudioScriptVoiceState_e::Stopped)
			return true;

		voice->pitch = AudioScript_ClampPitch(pitch);
		backend = voice->backend;
		state = voice->state;
		nativeEventId = voice->nativeEventId;
	}

	if (state == AudioScriptVoiceState_e::Pending || state == AudioScriptVoiceState_e::Starting)
		return true;

	if (backend == AudioScriptBackend_e::CustomRPak)
		return RPakWav_SetScriptVoicePitch(static_cast<uint32_t>(voiceId), pitch);

	return g_milesGlobals && g_milesGlobals->queue && nativeEventId && v_MilesQueueUpdateEventRateFactor &&
		v_MilesQueueUpdateEventRateFactor(reinterpret_cast<Miles::Queue*>(g_milesGlobals->queue),
			nativeEventId, AudioScript_ClampPitch(pitch)) != 0;
}

bool AudioScript_SetVolume(const int voiceId, const float volume)
{
	if (voiceId <= 0)
		return false;

	AudioScriptBackend_e backend = AudioScriptBackend_e::CustomRPak;
	AudioScriptVoiceState_e state = AudioScriptVoiceState_e::Failed;
	void* nativeEventId = nullptr;
	{
		std::lock_guard<std::mutex> lock(s_audioScriptVoicesMutex);
		AudioScriptVoiceEntry_t* const voice = AudioScript_FindVoiceLocked(static_cast<uint32_t>(voiceId));
		if (!voice || voice->generation != AudioScript_GetGeneration() ||
			voice->state == AudioScriptVoiceState_e::Failed || voice->state == AudioScriptVoiceState_e::Stopped)
		{
			return false;
		}

		voice->volume = AudioScript_ClampVolume(volume);
		backend = voice->backend;
		state = voice->state;
		nativeEventId = voice->nativeEventId;
	}

	if (state == AudioScriptVoiceState_e::Pending || state == AudioScriptVoiceState_e::Starting)
		return true;

	if (backend == AudioScriptBackend_e::CustomRPak)
		return RPakWav_SetScriptVoiceVolume(static_cast<uint32_t>(voiceId), volume);

	return g_milesGlobals && g_milesGlobals->queue && nativeEventId && v_MilesQueueUpdateEventVolume &&
		v_MilesQueueUpdateEventVolume(reinterpret_cast<Miles::Queue*>(g_milesGlobals->queue),
			nativeEventId, AudioScript_ClampVolume(volume)) != 0;
}

bool AudioScript_Stop(const int voiceId)
{
	if (voiceId <= 0)
		return false;

	AudioScriptBackend_e backend = AudioScriptBackend_e::CustomRPak;
	AudioScriptVoiceState_e state = AudioScriptVoiceState_e::Failed;
	void* nativeEventId = nullptr;
	{
		std::lock_guard<std::mutex> lock(s_audioScriptVoicesMutex);
		AudioScriptVoiceEntry_t* const voice = AudioScript_FindVoiceLocked(static_cast<uint32_t>(voiceId));
		if (!voice || voice->generation != AudioScript_GetGeneration() ||
			voice->state == AudioScriptVoiceState_e::Failed || voice->state == AudioScriptVoiceState_e::Stopped)
		{
			return false;
		}

		voice->cancelled = true;
		backend = voice->backend;
		state = voice->state;
		nativeEventId = voice->nativeEventId;
		voice->state = AudioScriptVoiceState_e::Stopped;
	}

	if (state == AudioScriptVoiceState_e::Pending || state == AudioScriptVoiceState_e::Starting)
		return true;

	if (backend == AudioScriptBackend_e::CustomRPak)
		return RPakWav_StopScriptVoice(static_cast<uint32_t>(voiceId));

	if (!g_milesGlobals || !g_milesGlobals->queue || !nativeEventId || !v_MilesQueueEventSignal)
		return false;

	return v_MilesQueueEventSignal(reinterpret_cast<Miles::Queue*>(g_milesGlobals->queue),
		nativeEventId, "stop") != 0;
}

void RPakWav_StopAllPackedVoices()
{
	RPakWav_ClearPlayQueue();
	RPakWav_ClearPendingEvents();
	RPakWav_ClearWeaponLoopRepeaters();
	RPakWav_StopAllVoices();
}

void RPakWav_StopPackedMusicVoices()
{
	RPakWav_StopVoicesByFlags(RPAK_WAV_EVENT_FLAG_MUSIC);
}

void RPakWav_StopPackedVoiceByEventName(const char* const eventName)
{
	if (!VALID_CHARSTAR(eventName))
		return;

	const PakGuid_t eventGuid = Pak_StringToGuid(eventName);
	RPakWav_RemovePendingEventsByEvent(eventGuid);
	RPakWav_RemoveQueuedPlayRequestsByEvent(eventGuid);
	RPakWav_StopWeaponLoopRepeatersByEvent(eventGuid);
	RPakWav_StopVoicesByEvent(eventGuid);
}

void RPakWav_BeginManualMilesPlay()
{
	++s_rpakWavManualMilesPlayDepth;
}

void RPakWav_EndManualMilesPlay()
{
	if (s_rpakWavManualMilesPlayDepth > 0)
		--s_rpakWavManualMilesPlayDepth;
}

void RPakWav_RegisterPakAsset(PakFile_s* const pak, const PakAsset_s* const asset)
{
	if (!g_pakGlobals || !pak || !asset)
		return;

	PakLoadedInfo_s* const loadedPak = &g_pakGlobals->loadedPaks[pak->memoryData.pakId & PAK_MAX_LOADED_PAKS_MASK];

	if (asset->magic == RPAK_WAV_ASRC_ASSET_TYPE)
	{
		const RPakWavAudioSourceHeader_v1_t* const source =
			reinterpret_cast<const RPakWavAudioSourceHeader_v1_t*>(pak->GetPointerForPageOffset(asset->headPtr));
		if (!RPakWav_IsSourceHeader(source))
			return;

		if (source->sourceGuid != asset->guid)
			return;

		const RPakWavSourceLookup_t lookup = { source->sourceGuid, source, loadedPak, pak };

		std::lock_guard<std::mutex> lock(s_rpakWavSourceRegistryWriteMutex);

		const RPakWavSourceRegistrySnapshot_t* const currentSnapshot =
			s_rpakWavSourceRegistry.load(std::memory_order_acquire);
		std::unique_ptr<RPakWavSourceRegistrySnapshot_t> nextSnapshot(
			new RPakWavSourceRegistrySnapshot_t(currentSnapshot ? *currentSnapshot : s_rpakWavEmptySourceRegistry));

		bool updatedExistingSource = false;
		for (uint32_t i = 0; i < nextSnapshot->count; ++i)
		{
			if (nextSnapshot->entries[i].sourceGuid != source->sourceGuid)
				continue;

			nextSnapshot->entries[i] = lookup;
			updatedExistingSource = true;
			break;
		}

		if (!updatedExistingSource)
		{
			if (nextSnapshot->count >= RPAK_WAV_MAX_REGISTERED_SOURCES)
			{
				if (!s_rpakWavSourceRegistryOverflowWarned.exchange(true) && (RPakWav_DebugEnabled() || miles_warnings.GetBool()))
					Warning(eDLL_T::AUDIO, "Packed WAV source registry is full; skipping source guid 0x%llX\n", source->sourceGuid);

				return;
			}

			nextSnapshot->entries[nextSnapshot->count++] = lookup;
		}

		const RPakWavSourceRegistrySnapshot_t* const publishedSnapshot = nextSnapshot.get();
		s_rpakWavSourceRegistrySnapshots.push_back(std::move(nextSnapshot));
		s_rpakWavSourceRegistry.store(publishedSnapshot, std::memory_order_release);

		if (RPakWav_DebugEnabled())
		{
			Msg(eDLL_T::AUDIO, "Registered packed WAV source guid 0x%llX from pak '%s'\n",
				source->sourceGuid,
				pak->memoryData.fileName);
		}

		return;
	}

	if (asset->magic != RPAK_WAV_AEVT_ASSET_TYPE)
		return;

	const RPakWavAudioEventHeader_v2_t* const event =
		reinterpret_cast<const RPakWavAudioEventHeader_v2_t*>(pak->GetPointerForPageOffset(asset->headPtr));
	if (!RPakWav_IsEventHeader(event))
		return;

	if (event->eventGuid != asset->guid)
		return;

	const RPakWavEventLookup_t lookup = { event->eventGuid, event, loadedPak, pak };

	std::lock_guard<std::mutex> lock(s_rpakWavEventRegistryWriteMutex);

	const RPakWavEventRegistrySnapshot_t* const currentSnapshot =
		s_rpakWavEventRegistry.load(std::memory_order_acquire);
	std::unique_ptr<RPakWavEventRegistrySnapshot_t> nextSnapshot(
		new RPakWavEventRegistrySnapshot_t(currentSnapshot ? *currentSnapshot : s_rpakWavEmptyEventRegistry));

	bool updatedExistingEvent = false;
	for (uint32_t i = 0; i < nextSnapshot->count; ++i)
	{
		if (nextSnapshot->entries[i].eventGuid != event->eventGuid)
			continue;

		nextSnapshot->entries[i] = lookup;
		updatedExistingEvent = true;
		break;
	}

	if (!updatedExistingEvent)
	{
		if (nextSnapshot->count >= RPAK_WAV_MAX_REGISTERED_EVENTS)
		{
			if (!s_rpakWavEventRegistryOverflowWarned.exchange(true) && (RPakWav_DebugEnabled() || miles_warnings.GetBool()))
				Warning(eDLL_T::AUDIO, "Packed WAV event registry is full; skipping event guid 0x%llX\n", event->eventGuid);

			return;
		}

		nextSnapshot->entries[nextSnapshot->count++] = lookup;
	}

	const RPakWavEventRegistrySnapshot_t* const publishedSnapshot = nextSnapshot.get();
	s_rpakWavEventRegistrySnapshots.push_back(std::move(nextSnapshot));
	s_rpakWavEventRegistry.store(publishedSnapshot, std::memory_order_release);

	if (RPakWav_DebugEnabled())
	{
		Msg(eDLL_T::AUDIO, "Registered packed WAV event guid 0x%llX from pak '%s'\n",
			event->eventGuid,
			pak->memoryData.fileName);
	}
}

//-----------------------------------------------------------------------------
// Purpose: initializes the miles sound system
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
static bool CSOM_Initialize()
{
	const char* pszLanguage = HEbisuSDK_GetLanguage();
	const bool isDefaultLanguage = V_stricmp(pszLanguage, MILES_DEFAULT_LANGUAGE) == 0;

	if (!isDefaultLanguage)
	{
		if ((V_stricmp(pszLanguage, "schinese") == 0) || (V_stricmp(pszLanguage, "tchinese") == 0))
			pszLanguage = "mandarin"; // schinese and tchinese use the mandarin bank.

		const bool useShipSound = !CommandLine()->FindParm("-devsound") || CommandLine()->FindParm("-shipsound");
		char baseStreamFilePath[MAX_OSPATH];

		V_snprintf(baseStreamFilePath, sizeof(baseStreamFilePath), "%s\\general_%s.mstr", useShipSound ? "audio\\ship" : "audio\\dev", pszLanguage);
		bool found = FileExists(baseStreamFilePath);

		if (!found && ModSystem()->IsEnabled())
		{
			ModSystem()->LockModList();

			// Check for it in our mods.
			FOR_EACH_VEC(ModSystem()->GetModList(), i)
			{
				const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

				if (!mod->IsEnabled())
					continue;

				const CUtlString modLookupPath = mod->GetBasePath() + baseStreamFilePath;
				const char* const pModLookupPath = modLookupPath.String();

				found = FileExists(pModLookupPath);

				if (found)
					break;
			}

			ModSystem()->UnlockModList();
		}

		if (!found)
		{
			// if the requested language for miles does not have a MSTR file present,
			// throw a non-fatal error and force MILES_DEFAULT_LANGUAGE as a fallback if
			// we are loading MILES_DEFAULT_LANGUAGE and the file is still not found, we
			// can let it hit the regular engine error, since that is not recoverable.
			Error(eDLL_T::AUDIO, NO_ERROR, "%s: attempted to load language '%s' but the required streaming source file (%s) was not found, falling back to '%s'...\n",
				__FUNCTION__, pszLanguage, baseStreamFilePath, MILES_DEFAULT_LANGUAGE);

			pszLanguage = MILES_DEFAULT_LANGUAGE;
		}

		miles_language->SetValue(pszLanguage);
	}

	Msg(eDLL_T::AUDIO, "%s: initializing MSS with language: '%s'\n", __FUNCTION__, pszLanguage);
	CFastTimer initTimer;

	RPakWav_StopAllPackedVoices();

	initTimer.Start();
	const bool bResult = v_CSOM_Initialize();
	initTimer.End();

	Msg(eDLL_T::AUDIO, "%s: %s (%f seconds)\n", __FUNCTION__, bResult ? "success" : "failure", initTimer.GetDuration().GetSeconds());
	return bResult;
}

//-----------------------------------------------------------------------------
// Purpose: appends banks from list to be loaded
//-----------------------------------------------------------------------------
static void CSOM_AppendBanksFromList(CSOM_BankList_s* const bankList, const char* const filePath, const bool mandatory)
{
	const int errorCode = mandatory ? EXIT_FAILURE : 0;
	RSON::Node_t* root = nullptr;

#define ERROR_AND_RETURN(fmt, ...) \
		do {\
			Error(eDLL_T::AUDIO, errorCode, "Error loading Miles Bank list from '%s': "##fmt, filePath, ##__VA_ARGS__); \
			if (root) {\
				RSON_Free(root, AlignedMemAlloc()); \
				AlignedMemAlloc()->Free(root); \
			}\
			return; \
		} while(0)\

	if (bankList->bankCount == CSOM_MAX_LOADED_BANKS)
	{
		ERROR_AND_RETURN("Out of room -- already reached code limit of %d.\n", CSOM_MAX_LOADED_BANKS);
		return;
	}

	CUtlBuffer buf;

	if (!FileSystem()->ReadFile(filePath, nullptr, buf))
	{
		if (mandatory) // Only exit if the main file doesn't exist.
			ERROR_AND_RETURN("Could not load file.\n");

		return;
	}

	const RSON::eFieldType rootType = (RSON::eFieldType)(RSON::eFieldType::RSON_ARRAY | RSON::eFieldType::RSON_VALUE);
	root = RSON::LoadFromBuffer(filePath, (char*)buf.Base(), rootType);

	const RSON::eFieldType expectType = (RSON::eFieldType)(RSON::eFieldType::RSON_ARRAY | RSON::eFieldType::RSON_OBJECT);

	if (!root || root->type != expectType)
		ERROR_AND_RETURN("Data should be an array of objects.\n");

	const int numSlotsLeft = (CSOM_MAX_LOADED_BANKS - bankList->bankCount);

	if (root->valueCount > numSlotsLeft)
		ERROR_AND_RETURN("Too many banks -- code limit is %d.\n", CSOM_MAX_LOADED_BANKS);

	bool nameSetForBank = false;

	for (int i = 0; i < root->valueCount; i++)
	{
		const RSON::Field_t* const key = root->GetArrayValue(i)->GetSubKey();

		if (!key)
			continue;

		if (V_strcmp(key->name, "name") != 0)
			ERROR_AND_RETURN("Only valid key is 'name', not '%s'.\n", key->name);

		if (nameSetForBank)
			ERROR_AND_RETURN("Each bank must have exactly one name.\n");

		nameSetForBank = true;

		if (key->node.type != RSON::eFieldType::RSON_STRING)
			ERROR_AND_RETURN("'name' must be a single string.\n");

		const char* const bankToAdd = key->GetString();

		// Make sure this bank wasn't already added.
		for (int j = 0; j < bankList->bankCount; j++)
		{
			if (V_stricmp(bankList->banks[j], bankToAdd) == 0)
				ERROR_AND_RETURN("Each bank must be unique; '%s' was already listed.\n", bankToAdd);
		}

		V_strncpy(bankList->banks[bankList->bankCount++], bankToAdd, CSOM_MAX_FILE_NAME);
	}

	RSON_Free(root, AlignedMemAlloc());
	AlignedMemAlloc()->Free(root);

#undef ERROR_AND_RETURN
}

#define CSOM_BANK_LIST_FILE "scripts/audio/banks.rson"

//-----------------------------------------------------------------------------
// Purpose: initializes the bank list object dictating which banks to load
//-----------------------------------------------------------------------------
static void CSOM_InitializeBankList(CSOM_BankList_s* const bankList)
{
	bankList->bankCount = 0;
	CSOM_AppendBanksFromList(bankList, CSOM_BANK_LIST_FILE, true);

	if (ModSystem()->IsEnabled())
	{
		ModSystem()->LockModList();

		// Add banks from our mods.
		FOR_EACH_VEC(ModSystem()->GetModList(), i)
		{
			const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

			if (!mod->IsEnabled())
				continue;

			const CUtlString lookupPath = mod->GetBasePath() + CSOM_BANK_LIST_FILE;
			CSOM_AppendBanksFromList(bankList, lookupPath.String(), false);
		}

		ModSystem()->UnlockModList();
	}
}

//-----------------------------------------------------------------------------
// Purpose: logs debug output emitted from the Miles Sound System
// Input  : nLogLevel - 
//          pszMessage - 
//-----------------------------------------------------------------------------
static void CSOM_LogFunc(int64_t nLogLevel, const char* pszMessage)
{
	Msg(eDLL_T::AUDIO, "%s\n", pszMessage);
	v_CSOM_LogFunc(nLogLevel, pszMessage);
}

//-----------------------------------------------------------------------------
// Purpose: runs the event queue
//-----------------------------------------------------------------------------
void* MilesQueueEventRun(Miles::Queue* queue, const char* eventName)
{
	if(miles_debug.GetBool())
		Msg(eDLL_T::AUDIO, "%s: running event: '%s'\n", __FUNCTION__, eventName);

	return v_MilesQueueEventRun(queue, eventName);
}

//-----------------------------------------------------------------------------
// Purpose: patches miles banks
//-----------------------------------------------------------------------------
void MilesBankPatch(Miles::Bank* bank, char* streamPatch, char* localizedStreamPatch)
{
	if (miles_debug.GetBool())
	{
		Msg(eDLL_T::AUDIO,
			"%s: patching bank \"%s\". stream patches: \"%s\", \"%s\"\n",
			__FUNCTION__,
			bank->GetBankName(),
			V_UnqualifiedFileName(streamPatch), V_UnqualifiedFileName(localizedStreamPatch)
		);
	}

	const Miles::BankHeader_t* header = bank->GetHeader();

	if (header->bankIndex >= header->project->bankCount)
		Error(eDLL_T::AUDIO, EXIT_FAILURE,
			"%s: attempted to patch bank \"%s\" that identified itself as bank #%i, project expects a highest index of #%i\n",
			__FUNCTION__,
			bank->GetBankName(),
			header->bankIndex,
			header->project->bankCount - 1
		);

	v_MilesBankPatch(bank, streamPatch, localizedStreamPatch);
}

//-----------------------------------------------------------------------------
// Purpose: adds an audio event to the queue
//-----------------------------------------------------------------------------
static void CSOM_AddEventToQueue(const char* eventName)
{
	if (miles_debug.GetBool())
		Msg(eDLL_T::AUDIO, "%s: queuing audio event '%s'\n", __FUNCTION__, eventName);

	if (!rpakwav_enable.GetBool())
	{
		v_CSOM_AddEventToQueue(eventName);
		return;
	}

	v_CSOM_AddEventToQueue(eventName);
	std::atomic_thread_fence(std::memory_order_seq_cst);
	const RPakWavSpatialInfo_t spatial = RPakWav_CaptureQueuedSpatialInfo();
	RPakWav_StopWeaponLoopVoicesForStopEvent(eventName, spatial);

	if (!g_milesGlobals)
		return;

	if (g_milesGlobals->queuedEventHash != 2)
	{
		if (s_rpakWavActiveMusicVoiceCount.load(std::memory_order_relaxed) != 0 &&
			RPakWav_EventNameLooksLikeMusicStop(eventName))
		{
			RPakWav_StopVoicesByFlags(RPAK_WAV_EVENT_FLAG_MUSIC);
		}

		if (miles_warnings.GetBool() && g_milesGlobals->queuedEventHash == 1)
			Warning(eDLL_T::AUDIO, "%s: failed to add event to queue; invalid event name '%s'\n", __FUNCTION__, eventName);

		return;
	}

	bool recognizedPackedWavEvent = false;
	const bool allowLoadedPakFallback = RPakWav_ShouldUseLoadedPakFallback(eventName);
	const bool suppressWeaponLoop = RPakWav_IsManualMilesPlayActive();
	RPakWav_TryScheduleEvent(eventName, true, &recognizedPackedWavEvent, allowLoadedPakFallback, suppressWeaponLoop, &spatial);
	if (recognizedPackedWavEvent)
		return;

	if (allowLoadedPakFallback)
		RPakWav_QueuePendingEvent(eventName, suppressWeaponLoop, spatial);

	if (s_rpakWavActiveMusicVoiceCount.load(std::memory_order_relaxed) != 0 &&
		RPakWav_EventNameLooksLikeMusicStop(eventName))
	{
		RPakWav_StopVoicesByFlags(RPAK_WAV_EVENT_FLAG_MUSIC);
	}

	if (miles_warnings.GetBool() && g_milesGlobals)
	{
		Warning(eDLL_T::AUDIO, "%s: failed to add event to queue; event '%s' not found.\n", __FUNCTION__, eventName);
	}
};

//-----------------------------------------------------------------------------
// Purpose: runs per-frame Miles maintenance without adding work to every event
//-----------------------------------------------------------------------------
static void CSOM_RunFrame(char a1, char a2, float a3, float a4)
{
	RPakWav_ReportSpatialSetting(false);
	s_rpakWavAudioFrameSubmitting.store(true, std::memory_order_release);
	if (rpakwav_enable.GetBool())
	{
		RPakWav_CleanupDoneVoices();
		RPakWav_RunPendingEvents();
		RPakWav_RunQueuedPlayRequests();
		RPakWav_RunWeaponLoopRepeaters();
		RPakWav_UpdateSpatialVoiceGains();
	}

	AudioScript_RunQueuedPlayRequests();
	{
		// Publish this frame's new voices together, while existing voices keep
		// streaming during file reads and source preparation on the main thread.
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (const auto& voice : s_rpakWavVoices)
			voice->mixerPending = false;
	}
	s_rpakWavAudioFrameSubmitting.store(false, std::memory_order_release);
	if (s_rpakWavSpatialWakeEvent) SetEvent(s_rpakWavSpatialWakeEvent);
	v_CSOM_RunFrame(a1, a2, a3, a4);
}

//-----------------------------------------------------------------------------
// Purpose: close and reset the CSOM async file instance
//-----------------------------------------------------------------------------
static void CSOM_CloseAsyncFile(CSOM_AsyncFile_s* const asyncFile)
{
	asyncFile->asyncRequestId = 0;
	asyncFile->fileName[0] = '\0';
	FS_CloseAsyncFile(asyncFile->fileHandle);
	asyncFile->fileHandle = FS_ASYNC_FILE_INVALID;
	asyncFile->readOffset = 0;
	asyncFile->fileSize = 0;
}

//-----------------------------------------------------------------------------
// Structure for each live file instance
//-----------------------------------------------------------------------------
struct CSOM_FileInfo_s
{
	char fileName[256];
	size_t fileSize;
	int fileHandle;
};

#define CSOM_MAX_OPENED_FILES 32

static CSOM_FileInfo_s s_milesFileInfos[CSOM_MAX_OPENED_FILES];
static size_t s_numMilesFilesOpened = 0;

//-----------------------------------------------------------------------------
// Purpose: finds the file handle for given name, opens it if not found
//-----------------------------------------------------------------------------
static int CSOM_MilesAsync_OpenOrFindFile(const char* const fileName, size_t& outFileSize)
{
	if (s_numMilesFilesOpened)
	{
		// Find the file.
		CSOM_FileInfo_s* infoIt = s_milesFileInfos;
		size_t currIdx = 0;
		bool notFound = false; // If true, will try and open the file.

		while (V_strcmp(fileName, infoIt->fileName))
		{
			++currIdx;
			++infoIt;

			if (currIdx == s_numMilesFilesOpened)
			{
				notFound = true;
				break;
			}
		}

		if (!notFound)
		{
			outFileSize = infoIt->fileSize;
			g_pakLoadApi->IncrementAsyncFileRefCount(infoIt->fileHandle);

			return infoIt->fileHandle;
		}
	}

	if (s_numMilesFilesOpened == CSOM_MAX_OPENED_FILES)
		return FS_ASYNC_FILE_INVALID; // Max opened files reached.

	// Open the file.
	CSOM_FileInfo_s* const info = &s_milesFileInfos[s_numMilesFilesOpened++];
	V_strncpy(info->fileName, fileName, sizeof(info->fileName));

	info->fileHandle = FS_OpenAsyncFile(fileName, 4, &info->fileSize);

	if (info->fileHandle == FS_ASYNC_FILE_INVALID)
		Error(eDLL_T::AUDIO, EXIT_FAILURE, "%s( \"%s\" ) failed to open file; try resyncing\n", __FUNCTION__, fileName);

	outFileSize = info->fileSize;
	g_pakLoadApi->IncrementAsyncFileRefCount(info->fileHandle);

	return info->fileHandle;
}

//-----------------------------------------------------------------------------
// Purpose: returns the first free file slot index
//-----------------------------------------------------------------------------
static inline size_t CSOM_MilesAsync_GetFirstFreeFileSlot()
{
	size_t index = 0;

	// Scan the list.
	while (g_milesGlobals->asyncFiles[index].asyncRequestId)
		index++;

	return index;
}

//-----------------------------------------------------------------------------
// User structure for MilesAsyncRead
//-----------------------------------------------------------------------------
struct CSOM_AsyncRead_s
{
	int asyncFileHandle;
	bool shouldCloseFile;
	bool readFinished;
};

//-----------------------------------------------------------------------------
// Purpose: Miles async file read request handler; maps to internal callback of
//          MilesAsyncFileRead, set through API MilesAsyncSetCallbacks.
//-----------------------------------------------------------------------------
static s32 CSOM_MilesAsync_FileRead(MilesAsyncRead* const request)
{
	CSOM_AsyncRead_s* const user = (CSOM_AsyncRead_s*)request->Internal;
	CSOM_AsyncFile_s* asyncFile;

	if (request->RequestId)
	{
		asyncFile = &g_milesGlobals->asyncFiles[request->RequestId & CSOM_MAX_ASYNC_FILE_HANDLES_MASK];
	}
	else // New request, open the file.
	{
		const size_t asyncFileIdx = CSOM_MilesAsync_GetFirstFreeFileSlot();
		asyncFile = &g_milesGlobals->asyncFiles[asyncFileIdx];

		MilesSubFileInfo_s sfi; char fileNameStack[512];
		MilesGetSubFileInfo(fileNameStack, request->FileName, &sfi);

		R_UTF8_strncpy(asyncFile->fileName, sfi.filename, sizeof(asyncFile->fileName));

		asyncFile->fileSize = 0;
		asyncFile->fileHandle = CSOM_MilesAsync_OpenOrFindFile(sfi.filename, asyncFile->fileSize);

		asyncFile->readOffset = 0;
		asyncFile->readStart = sfi.start;

		if (sfi.size)
		{
			const size_t subFileSize = sfi.size + sfi.start;

			if (subFileSize < asyncFile->fileSize)
				asyncFile->fileSize = subFileSize;
		}

		// Give the request an unique ID with its slot index packed into it.
		const u64 asyncRequestId = asyncFileIdx + (++g_milesGlobals->asyncRequestIdGen * CSOM_MAX_ASYNC_FILE_HANDLES);

		asyncFile->asyncRequestId = asyncRequestId;
		request->RequestId = asyncRequestId;
	}

	user->shouldCloseFile = (request->Flags & MSSIO_FLAGS_DONT_CLOSE_HANDLE) == 0;

	if ((request->Flags & (MSSIO_FLAGS_QUERY_START_ONLY|MSSIO_FLAGS_QUERY_SIZE_ONLY)) != 0)
		request->Start = asyncFile->fileSize - asyncFile->readStart;

	size_t readCount = request->Count;

	if ((request->Flags & MSSIO_FLAGS_QUERY_SIZE_ONLY) != 0 || readCount == 0)
	{
		user->asyncFileHandle = FS_ASYNC_FILE_INVALID;
		request->Status = MSSIO_STATUS_COMPLETE;

		if (user->shouldCloseFile)
			CSOM_CloseAsyncFile(asyncFile);

		return 1;
	}

	size_t readOffset = 0;

	if ((request->Flags & MSSIO_FLAGS_DONT_USE_OFFSET) == 0)
	{
		readOffset = request->Offset;
		asyncFile->readOffset = readOffset;
	}

	if (readCount < 0)
	{
		readCount = asyncFile->fileSize - asyncFile->readStart - readOffset;
		request->Count = readCount;
	}

	size_t numBytesLeft = asyncFile->fileSize - asyncFile->readStart - readOffset;

	if (readCount < numBytesLeft)
		numBytesLeft = readCount;

	request->Count = numBytesLeft;

	if (!request->Buffer)
	{
		// Allocate a read buffer.
		const size_t readBufSize = numBytesLeft + request->ReadAmt;
		void* const readBuffer = v_MilesAllocEx(readBufSize, 0, g_milesGlobals->driver, request->LastAllocSrcFileName, request->LastAllocSrcFileLine);

		if (!readBuffer)
		{
			Error(eDLL_T::AUDIO, EXIT_FAILURE, "Miles async failed malloc for '%s' size %zu\n", asyncFile->fileName, readBufSize);

			user->asyncFileHandle = FS_ASYNC_FILE_INVALID;
			request->Status = MSSIO_STATUS_ERROR_MEMORY_ALLOC_FAIL;

			if (user->shouldCloseFile)
				CSOM_CloseAsyncFile(asyncFile);

			return 0;
		}

		request->Buffer = readBuffer;
	}

	if (request->Count)
	{
		// Read data into the buffer.
		user->readFinished = false;
		user->asyncFileHandle = g_pakLoadApi->ReadAsyncFile(asyncFile->fileHandle, asyncFile->readStart + asyncFile->readOffset, request->Count, request->Buffer, 1);

		if (user->asyncFileHandle == FS_ASYNC_FILE_INVALID)
		{
			Error(eDLL_T::AUDIO, EXIT_FAILURE, "Miles async failed read for '%s' offset %zu count %zu\n", asyncFile->fileName, request->Offset, request->Count);
			request->Status = MSSIO_STATUS_ERROR_FAILED_OPEN;

			if (user->shouldCloseFile)
				CSOM_CloseAsyncFile(asyncFile);

			return 0;
		}

		request->Status = MSSIO_STATUS_COMPLETE_NOP;
	}
	else
	{
		request->Status = MSSIO_STATUS_COMPLETE_NOP;
		user->readFinished = true;
		user->asyncFileHandle = FS_ASYNC_REQ_INVALID;
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: Miles async file status request handler; maps to internal callback
//          of MilesAsyncFileStatus, set through API MilesAsyncSetCallbacks.
//-----------------------------------------------------------------------------
static s32 CSOM_MilesAsync_FileStatus(MilesAsyncRead* const request, const u32 i_MS)
{
	CSOM_AsyncRead_s* const user = (CSOM_AsyncRead_s*)request->Internal;

	if (user->asyncFileHandle == FS_ASYNC_FILE_INVALID)
		return request->Status;

	AsyncHandleStatus_s::Status_e currentStatus;

	if (user->readFinished)
	{
		currentStatus = AsyncHandleStatus_s::Status_e::FS_ASYNC_READY;
	}
	else
	{
		if (i_MS)
			g_pakLoadApi->WaitForAsyncRequest(user->asyncFileHandle);

		currentStatus = g_pakLoadApi->CheckAsyncRequest(user->asyncFileHandle, nullptr, nullptr);

		if (currentStatus == AsyncHandleStatus_s::Status_e::FS_ASYNC_PENDING)
			return 0;
	}

	user->asyncFileHandle = FS_ASYNC_FILE_INVALID;
	CSOM_AsyncFile_s* const asyncFile = &g_milesGlobals->asyncFiles[request->RequestId & CSOM_MAX_ASYNC_FILE_HANDLES_MASK];

	if (user->shouldCloseFile)
		CSOM_CloseAsyncFile(asyncFile);

	if (currentStatus == AsyncHandleStatus_s::Status_e::FS_ASYNC_READY)
	{
		request->LastCount = request->Count;
		asyncFile->readOffset += request->Count;
		request->Status = MSSIO_STATUS_COMPLETE;
	}
	else // Failure or canceled.
	{
		request->LastCount = 0;

		if (currentStatus == AsyncHandleStatus_s::Status_e::FS_ASYNC_CANCELLED)
			request->Status = MSSIO_STATUS_ERROR_CANCELLED;
		else
			request->Status = MSSIO_STATUS_ERROR_FAILED_READ;
	}

	return request->Status;
}

//-----------------------------------------------------------------------------
// Purpose: Miles async file cancel request handler; maps to internal callback
//          of MilesAsyncFileCancel, set through API MilesAsyncSetCallbacks.
//-----------------------------------------------------------------------------
static s32 CSOM_MilesAsync_FileCancel(MilesAsyncRead* const request)
{
	CSOM_AsyncRead_s* const user = (CSOM_AsyncRead_s*)request->Internal;

	if (user->asyncFileHandle == FS_ASYNC_FILE_INVALID)
		return 1; // Nothing to cancel.

	if (!user->readFinished)
		g_pakLoadApi->CancelAsyncRequest(user->asyncFileHandle);

	return CSOM_MilesAsync_FileStatus(request, RR_WAIT_INFINITE);
}

///////////////////////////////////////////////////////////////////////////////
void MilesCore::Detour(const bool bAttach) const
{
	if (!bAttach)
	{
		RPakWav_StopSpatialService();
		AudioScript_ResetState();
		RPakWav_ClearPlayQueue();
		RPakWav_StopAllVoices();
		if (s_rpakWavSpatialWakeEvent)
		{
			CloseHandle(s_rpakWavSpatialWakeEvent);
			s_rpakWavSpatialWakeEvent = nullptr;
		}
		RPakWav_ClearPendingEvents();
		RPakWav_ClearWeaponLoopRepeaters();
		RPakWav_ClearSourceCache();
	}
	else
	{
		RPakWav_ResetEventRegistry();
		RPakWav_ClearPlayQueue();
		RPakWav_ClearWeaponLoopRepeaters();
		RPakWav_ClearSourceCache();
	}

	Pak_SetAssetProcessedCallback(bAttach ? &RPakWav_RegisterPakAsset : nullptr);

	DetourSetup(&v_MilesQueueEventRun, &MilesQueueEventRun, bAttach);
	//DetourSetup(&v_MilesBankPatch, &MilesBankPatch, bAttach);
	DetourSetup(&v_CSOM_Initialize, &CSOM_Initialize, bAttach);
	DetourSetup(&v_CSOM_InitializeBankList, &CSOM_InitializeBankList, bAttach);
	DetourSetup(&v_CSOM_LogFunc, &CSOM_LogFunc, bAttach);
	DetourSetup(&v_CSOM_MilesAsync_FileRead, &CSOM_MilesAsync_FileRead, bAttach);
	DetourSetup(&v_CSOM_MilesAsync_FileStatus, &CSOM_MilesAsync_FileStatus, bAttach);
	DetourSetup(&v_CSOM_MilesAsync_FileCancel, &CSOM_MilesAsync_FileCancel, bAttach);
	DetourSetup(&v_CSOM_AddEventToQueue, &CSOM_AddEventToQueue, bAttach);

	if (bAttach)
	{
		CMemory mem(v_CSOM_RunFrame);

		// Between Miles version 10.0.48 and 10.0.50, they swapped locations of
		// 2 members in a struct returned by MilesEventInfoQueueEnum on type 4.
		// This change breaks closed captions (sub-titles). The fix is to apply
		// the swap in the assembly code as well so the engine retrieves the
		// values correctly from the new locations again. The structure layout
		// on all other enums are still identical and do not need to be fixes.
		mem.Offset(0x762).Patch({ 0x4 });
		mem.Offset(0x78B).Patch({ 0xC });
	}

	DetourSetup(&v_CSOM_RunFrame, &CSOM_RunFrame, bAttach);
}
