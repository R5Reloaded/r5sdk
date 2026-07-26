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
static ConVar rpakwav_spatial_min_distance("rpakwav_spatial_min_distance", "96", FCVAR_RELEASE, "Packed WAV spatial audio full-volume distance in game units");
static ConVar rpakwav_spatial_max_distance("rpakwav_spatial_max_distance", "12000", FCVAR_RELEASE, "Packed WAV spatial audio fade distance in game units");
static ConVar rpakwav_spatial_min_volume("rpakwav_spatial_min_volume", "0", FCVAR_RELEASE, "Minimum packed WAV spatial attenuation multiplier");
static ConVar rpakwav_spatial_pan_strength("rpakwav_spatial_pan_strength", "0.85", FCVAR_RELEASE, "Packed WAV stereo pan strength for queued 3D positions");

namespace
{
	constexpr uint32_t RPAK_WAV_SOURCE_MAGIC = ('R' | ('S' << 8) | ('A' << 16) | ('S' << 24));
	constexpr uint32_t RPAK_WAV_AEVT_MAGIC = ('R' | ('S' << 8) | ('A' << 16) | ('E' << 24));
	constexpr uint32_t RPAK_WAV_AWSR_ASSET_TYPE = ('a' | ('w' << 8) | ('s' << 16) | ('r' << 24));
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
	constexpr uint32_t RPAK_WAV_MAX_REGISTERED_EVENTS = 256;
	constexpr uint32_t RPAK_WAV_MAX_REGISTERED_SOURCES = 1024;
	constexpr size_t RPAK_WAV_SPATIAL_BUFFER_COUNT = 3;
	constexpr uint32_t RPAK_WAV_SPATIAL_CHUNK_MS = 40;
	constexpr uint32_t RPAK_WAV_MAX_SOURCE_CACHE_ENTRIES = 1024;
	constexpr size_t RPAK_WAV_MAX_PLAY_REQUESTS = 256;
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
		PakGuid_t eventGuid;
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
		float eventVolume;
		std::atomic_bool done;
		std::atomic_uint32_t spatialActiveChunks;
		std::atomic_bool spatialFinishedSubmitting;
		bool prepared;
		bool closed;
		bool countedMusic;
		bool looping;
		bool spatialStreaming;

		RPakWavVoice_t()
			: eventGuid(0),
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
	std::atomic_uint32_t s_rpakWavActiveMusicVoiceCount{ 0 };

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
		std::shared_ptr<const RPakWavCachedWave_t> wave;
		uint64_t intervalMs;
		uint64_t nextPlayMs;
		uint64_t lastRefreshMs;
		RPakWavSpatialInfo_t spatial;
		float eventVolume;
	};

	std::mutex s_rpakWavWeaponLoopEventCacheMutex;
	std::vector<RPakWavWeaponLoopEvent_t> s_rpakWavWeaponLoopEvents;
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
#pragma pack(pop)

	bool RPakWav_DebugEnabled()
	{
		return miles_debug.GetBool() || rpakwav_debug.GetBool();
	}

	float RPakWav_ClampFiniteFloat(const float value, const float fallback, const float minValue, const float maxValue)
	{
		if (!std::isfinite(value))
			return fallback;

		return std::clamp(value, minValue, maxValue);
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
		return std::clamp(eventGain * menuGain * RPAK_WAV_GLOBAL_GAIN, 0.0f, 8.0f);
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
		if (!g_milesGlobals)
			return RPakWavSpatialInfo_t();

		const Vector3D position = g_milesGlobals->queuedSoundPosition;
		if (!RPakWav_IsFiniteVector(position) || position.IsZero(0.01f))
			return RPakWavSpatialInfo_t();

		return RPakWavSpatialInfo_t(position);
	}

	bool RPakWav_CalculateSpatialGains(const RPakWavSpatialInfo_t& spatial, float& outLeftGain, float& outRightGain)
	{
		outLeftGain = 1.0f;
		outRightGain = 1.0f;

		if (!rpakwav_spatial_enable.GetBool() || !spatial.valid || !g_vecRenderOrigin || !g_vecRenderAngles)
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
			return std::isfinite(sample) ? RPakWav_ClampSample(sample) : 0.0f;
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
			const int32_t quantized = static_cast<int32_t>(std::clamp(sample * 2147483647.0f, -2147483648.0f, 2147483647.0f));
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
		if (!RPakWav_CalculateSpatialGains(voice.spatial, leftGain, rightGain))
		{
			leftGain = 1.0f;
			rightGain = 1.0f;
		}
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

		float leftGain = 1.0f;
		float rightGain = 1.0f;
		RPakWav_GetSpatialChunkGains(voice, leftGain, rightGain);
		const float voiceGain = RPakWav_GetVoiceSampleGain(voice);
		leftGain *= voiceGain;
		rightGain *= voiceGain;

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
			return false;

		chunk.prepared = true;
		chunk.queued.store(true, std::memory_order_release);
		voice.spatialActiveChunks.fetch_add(1, std::memory_order_relaxed);

		result = waveOutWrite(voice.waveOut, &chunk.header, sizeof(chunk.header));
		if (result != MMSYSERR_NOERROR)
		{
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
			Msg(eDLL_T::AUDIO, "Packed WAV chunked spatial source=(%.1f %.1f %.1f) bus=%s gain=%.2f L=%.2f R=%.2f\n",
				voice.spatial.position.x, voice.spatial.position.y, voice.spatial.position.z,
				RPakWav_AudioBusDebugName(voice.audioBus), voiceGain, leftGain * voiceGain, rightGain * voiceGain);
		}

		return submittedCount != 0;
	}

	void RPakWav_UpdateSpatialVoices()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (const std::unique_ptr<RPakWavVoice_t>& voice : s_rpakWavVoices)
		{
			if (!voice || voice->done || voice->closed || !voice->spatialStreaming)
				continue;

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
			}

			if (voice->spatialFinishedSubmitting.load(std::memory_order_acquire) &&
				voice->spatialActiveChunks.load(std::memory_order_acquire) == 0)
			{
				voice->done.store(true, std::memory_order_release);
			}
		}
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
		return source && source->magic == RPAK_WAV_SOURCE_MAGIC && source->version == RPAK_WAV_SOURCE_VERSION;
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
			event->version == RPAK_WAV_EVENT_VERSION &&
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
		std::vector<RPakWavWeaponLoopEvent_t>& loopEvents, std::vector<RPakWavWeaponLoopStop_t>& loopStops)
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

		std::error_code ec;
		for (std::filesystem::directory_iterator it(weaponDir, ec), end; !ec && it != end; it.increment(ec))
		{
			if (it->is_regular_file(ec) && it->path().extension() == ".txt")
				RPakWav_ParseWeaponLoopFile(it->path(), loopEvents, loopStops);
		}

		const size_t loopEventCount = loopEvents.size();
		const size_t loopStopCount = loopStops.size();
		{
			std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopEventCacheMutex);
			s_rpakWavWeaponLoopEvents = std::move(loopEvents);
			s_rpakWavWeaponLoopStops = std::move(loopStops);
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

	void RPakWav_StopVoicesByEvent(const PakGuid_t eventGuid);
	size_t RPakWav_StopLoopingVoicesByEvent(const PakGuid_t eventGuid);
	size_t RPakWav_StopWeaponLoopRepeatersByEvent(const PakGuid_t eventGuid);

	size_t RPakWav_RemoveQueuedPlayRequestsByEvent(const PakGuid_t eventGuid)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavPlayQueueMutex);
		const size_t oldSize = s_rpakWavPlayQueue.size();
		s_rpakWavPlayQueue.erase(
			std::remove_if(s_rpakWavPlayQueue.begin(), s_rpakWavPlayQueue.end(),
				[eventGuid](const RPakWavPlayRequest_t& request)
				{
					return Pak_StringToGuid(request.eventName.c_str()) == eventGuid;
				}),
			s_rpakWavPlayQueue.end());

		return oldSize - s_rpakWavPlayQueue.size();
	}

	size_t RPakWav_RemovePendingEventsByEvent(const PakGuid_t eventGuid)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavPendingEventsMutex);
		const size_t oldSize = s_rpakWavPendingEvents.size();
		s_rpakWavPendingEvents.erase(
			std::remove_if(s_rpakWavPendingEvents.begin(), s_rpakWavPendingEvents.end(),
				[eventGuid](const RPakWavPendingEvent_t& pending)
				{
					return Pak_StringToGuid(pending.eventName.c_str()) == eventGuid;
				}),
			s_rpakWavPendingEvents.end());

		return oldSize - s_rpakWavPendingEvents.size();
	}

	void RPakWav_StopWeaponLoopVoicesForStopEvent(const char* const eventName)
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

			const size_t pendingCount = RPakWav_RemovePendingEventsByEvent(stop.loopEventGuid);
			const size_t queuedCount = RPakWav_RemoveQueuedPlayRequestsByEvent(stop.loopEventGuid);
			const size_t repeaterCount = RPakWav_StopWeaponLoopRepeatersByEvent(stop.loopEventGuid);
			const size_t loopingVoiceCount = RPakWav_StopLoopingVoicesByEvent(stop.loopEventGuid);
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

				if (channels == 0 || sampleRate == 0 || avgBytesPerSecond == 0 || blockAlign == 0 || bitsPerSample == 0)
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

				chunk.queued.store(false, std::memory_order_release);
				RPakWav_DecrementSpatialActiveChunks(*voice);
				break;
			}

			if (voice->spatialFinishedSubmitting.load(std::memory_order_acquire) &&
				voice->spatialActiveChunks.load(std::memory_order_acquire) == 0)
			{
				voice->done.store(true, std::memory_order_release);
			}

			return;
		}

		voice->done = true;
	}

	void RPakWav_CloseVoice(RPakWavVoice_t& voice, const bool reset)
	{
		if (!voice.waveOut || voice.closed)
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
	}

	void RPakWav_CleanupDoneVoices()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (auto it = s_rpakWavVoices.begin(); it != s_rpakWavVoices.end();)
		{
			RPakWavVoice_t& voice = **it;
			if (voice.done)
			{
				RPakWav_CloseVoice(voice, false);
				it = s_rpakWavVoices.erase(it);
				continue;
			}

			++it;
		}
	}

	void RPakWav_StopVoicesByEvent(const PakGuid_t eventGuid)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (auto it = s_rpakWavVoices.begin(); it != s_rpakWavVoices.end();)
		{
			RPakWavVoice_t& voice = **it;
			if (voice.eventGuid == eventGuid)
			{
				RPakWav_CloseVoice(voice, true);
				it = s_rpakWavVoices.erase(it);
				continue;
			}

			++it;
		}
	}

	size_t RPakWav_StopLoopingVoicesByEvent(const PakGuid_t eventGuid)
	{
		size_t stoppedCount = 0;
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (auto it = s_rpakWavVoices.begin(); it != s_rpakWavVoices.end();)
		{
			RPakWavVoice_t& voice = **it;
			if (voice.eventGuid == eventGuid && voice.looping)
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
	}

	bool RPakWav_HasActiveVoiceForEvent(const PakGuid_t eventGuid)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (const std::unique_ptr<RPakWavVoice_t>& voice : s_rpakWavVoices)
		{
			if (voice && voice->eventGuid == eventGuid && !voice->closed)
				return true;
		}

		return false;
	}

	bool RPakWav_RefreshActiveVoiceForEvent(const PakGuid_t eventGuid, const RPakWavSpatialInfo_t* const spatialInfo)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavVoicesMutex);
		for (const std::unique_ptr<RPakWavVoice_t>& voice : s_rpakWavVoices)
		{
			if (!voice || voice->eventGuid != eventGuid || voice->closed)
				continue;

			if (voice->spatialStreaming && spatialInfo && spatialInfo->valid)
				voice->spatial = *spatialInfo;

			return true;
		}

		return false;
	}

	bool RPakWav_PlayCachedWave(const PakGuid_t eventGuid, const uint32_t eventFlags, const char* const eventName,
		const std::shared_ptr<const RPakWavCachedWave_t>& wave, const float eventVolume, const bool forceLoop,
		const RPakWavSpatialInfo_t* const spatialInfo = nullptr)
	{
		if (!wave || wave->pcmBytes.empty())
			return false;

		RPakWav_CleanupDoneVoices();
		const bool loopEvent = forceLoop || ((eventFlags & RPAK_WAV_EVENT_FLAG_LOOP) != 0);
		if (loopEvent && RPakWav_RefreshActiveVoiceForEvent(eventGuid, spatialInfo))
			return true;

		if ((eventFlags & RPAK_WAV_EVENT_FLAG_IGNORE_WHILE_PLAYING) && RPakWav_HasActiveVoiceForEvent(eventGuid))
		{
			if (RPakWav_DebugEnabled())
				Msg(eDLL_T::AUDIO, "Ignoring duplicate packed WAV event '%s' while its voice is active\n",
					eventName ? eventName : "<null>");

			return true;
		}

		if ((eventFlags & RPAK_WAV_EVENT_FLAG_REPLACE_SAME_EVENT) || (eventFlags & RPAK_WAV_EVENT_FLAG_MUSIC) || RPakWav_EventNameLooksLikeMusic(eventName))
			RPakWav_StopVoicesByEvent(eventGuid);

		std::unique_ptr<RPakWavVoice_t> voice(new RPakWavVoice_t());
		voice->eventGuid = eventGuid;
		voice->flags = eventFlags | RPAK_WAV_EVENT_FLAG_MANAGED;
		voice->looping = loopEvent;
		if (loopEvent)
			voice->flags |= RPAK_WAV_EVENT_FLAG_LOOP;
		if (RPakWav_EventNameLooksLikeMusic(eventName))
			voice->flags |= RPAK_WAV_EVENT_FLAG_MUSIC;

		voice->audioBus = RPakWav_GetAudioBusForEvent(eventName, voice->flags);
		voice->eventVolume = RPakWav_NormalizeEventVolume(eventVolume);
		voice->wave = wave;
		voice->playbackFormat = voice->wave->format;
		if (!(voice->flags & RPAK_WAV_EVENT_FLAG_MUSIC) && spatialInfo && spatialInfo->valid)
		{
			voice->spatial = *spatialInfo;
			voice->spatialStreaming = RPakWav_PrepareSpatialPlaybackFormat(*voice->wave, voice->playbackFormat);
		}

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

		MMRESULT result = waveOutOpen(&voice->waveOut, WAVE_MAPPER, &voice->playbackFormat,
			reinterpret_cast<DWORD_PTR>(&RPakWav_WaveOutProc), reinterpret_cast<DWORD_PTR>(voice.get()), CALLBACK_FUNCTION);
		if (result != MMSYSERR_NOERROR)
			return false;

		if (voice->spatialStreaming)
		{
			if (!RPakWav_StartSpatialVoice(*voice))
			{
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
			return true;
		}

		result = waveOutPrepareHeader(voice->waveOut, &voice->header, sizeof(voice->header));
		if (result != MMSYSERR_NOERROR)
		{
			RPakWav_CloseVoice(*voice, true);
			return false;
		}

		voice->prepared = true;
		result = waveOutWrite(voice->waveOut, &voice->header, sizeof(voice->header));
		if (result != MMSYSERR_NOERROR)
		{
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
		return true;
	}

	bool RPakWav_PlayWeaponLoopShot(const PakGuid_t eventGuid, const uint32_t eventFlags, const char* const eventName,
		const std::shared_ptr<const RPakWavCachedWave_t>& wave, const float eventVolume, const RPakWavSpatialInfo_t& spatial)
	{
		return RPakWav_PlayCachedWave(eventGuid, eventFlags & ~RPAK_WAV_EVENT_FLAG_LOOP, eventName, wave, eventVolume, false, &spatial);
	}

	void RPakWav_StartOrRefreshWeaponLoopRepeater(const PakGuid_t eventGuid, const uint32_t eventFlags,
		const char* const eventName, const std::shared_ptr<const RPakWavCachedWave_t>& wave,
		const float eventVolume, const RPakWavSpatialInfo_t& spatial, uint64_t intervalMs)
	{
		if (!eventName || !*eventName || !wave)
			return;

		if (intervalMs == 0)
			intervalMs = RPakWav_FireRateToIntervalMs(RPAK_WAV_DEFAULT_WEAPON_FIRE_RATE);

		const uint64_t nowMs = GetTickCount64();
		std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopRepeatersMutex);
		for (RPakWavWeaponLoopRepeater_t& repeater : s_rpakWavWeaponLoopRepeaters)
		{
			if (repeater.eventGuid != eventGuid)
				continue;

			repeater.eventFlags = eventFlags & ~RPAK_WAV_EVENT_FLAG_LOOP;
			repeater.eventName = eventName;
			repeater.wave = wave;
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
			wave,
			intervalMs,
			nowMs,
			nowMs,
			spatial,
			RPakWav_NormalizeEventVolume(eventVolume)
		});
	}

	size_t RPakWav_StopWeaponLoopRepeatersByEvent(const PakGuid_t eventGuid)
	{
		std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopRepeatersMutex);
		const size_t oldSize = s_rpakWavWeaponLoopRepeaters.size();
		s_rpakWavWeaponLoopRepeaters.erase(
			std::remove_if(s_rpakWavWeaponLoopRepeaters.begin(), s_rpakWavWeaponLoopRepeaters.end(),
				[eventGuid](const RPakWavWeaponLoopRepeater_t& repeater)
				{
					return repeater.eventGuid == eventGuid;
				}),
			s_rpakWavWeaponLoopRepeaters.end());

		return oldSize - s_rpakWavWeaponLoopRepeaters.size();
	}

	void RPakWav_ClearWeaponLoopRepeaters()
	{
		std::lock_guard<std::mutex> lock(s_rpakWavWeaponLoopRepeatersMutex);
		s_rpakWavWeaponLoopRepeaters.clear();
	}

	void RPakWav_RunWeaponLoopRepeaters()
	{
		struct ShotRequest_t
		{
			PakGuid_t eventGuid;
			uint32_t eventFlags;
			std::string eventName;
			std::shared_ptr<const RPakWavCachedWave_t> wave;
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
						shots.push_back({ it->eventGuid, it->eventFlags, it->eventName, it->wave, it->spatial, it->eventVolume });
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

		for (const ShotRequest_t& shot : shots)
			RPakWav_PlayWeaponLoopShot(shot.eventGuid, shot.eventFlags, shot.eventName.c_str(), shot.wave, shot.eventVolume, shot.spatial);
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
			warnFailed("event asset header did not match aevt v4");
			return false;
		}

		if (outRecognizedPackedEvent)
			*outRecognizedPackedEvent = true;

		if (RPakWav_HandleControlEvent(eventName, event, loadedEventPak, eventPakFile))
			return true;

		const bool loopForWeaponText = !suppressWeaponLoop && RPakWav_IsWeaponLoopEventName(eventName);

		std::shared_ptr<const RPakWavCachedWave_t> wave;
		RPakWavSelectedSource_t selectedSource;
		if (!RPakWav_ReadEventSourceWave(event, loadedEventPak, eventPakFile, wave, &selectedSource))
		{
			warnFailed("failed to read or cache source WAV");
			return false;
		}

		if (RPakWav_DebugEnabled())
		{
			if (loopForWeaponText)
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
			RPakWav_StartOrRefreshWeaponLoopRepeater(event->eventGuid, event->flags, eventName, wave, event->volume, spatial,
				RPakWav_GetWeaponLoopIntervalMs(event->eventGuid));
			return true;
		}

		if (!RPakWav_PlayCachedWave(event->eventGuid, event->flags, eventName, wave, event->volume, false, &spatial))
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
			if (pending.eventName == eventName)
			{
				pending.framesRemaining = RPAK_WAV_PENDING_EVENT_FRAMES;
				pending.suppressWeaponLoop = pending.suppressWeaponLoop || suppressWeaponLoop;
				if (spatial.valid)
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

	if (asset->magic == RPAK_WAV_AWSR_ASSET_TYPE)
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
void MilesQueueEventRun(Miles::Queue* queue, const char* eventName)
{
	if(miles_debug.GetBool())
		Msg(eDLL_T::AUDIO, "%s: running event: '%s'\n", __FUNCTION__, eventName);

	v_MilesQueueEventRun(queue, eventName);
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
	RPakWav_StopWeaponLoopVoicesForStopEvent(eventName);

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

	const RPakWavSpatialInfo_t spatial = RPakWav_CaptureQueuedSpatialInfo();
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
	if (rpakwav_enable.GetBool())
	{
		RPakWav_CleanupDoneVoices();
		RPakWav_RunPendingEvents();
		RPakWav_RunQueuedPlayRequests();
		RPakWav_RunWeaponLoopRepeaters();
		RPakWav_UpdateSpatialVoices();
	}

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
		RPakWav_ClearPlayQueue();
		RPakWav_StopAllVoices();
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
