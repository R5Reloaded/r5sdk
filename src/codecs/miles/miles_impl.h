#pragma once
#include "miles_types.h"
#include "miles/include/mss.h"

#define CSOM_MAX_ASYNC_FILE_HANDLES 128
#define CSOM_MAX_ASYNC_FILE_HANDLES_MASK (CSOM_MAX_ASYNC_FILE_HANDLES-1)
#define CSOM_MAX_FILE_NAME 64
#define CSOM_MAX_LOADED_BANKS 16

struct PakAsset_s;
struct PakFile_s;

constexpr size_t AUDIO_SCRIPT_NAME_MAX = 260;
constexpr uint32_t AUDIO_SCRIPT_RANDOM_SOURCE = UINT32_MAX;
// A single RPak type binding must reserve enough storage for both flat v4
// (360-byte) and grouped v5 (384-byte) event headers, regardless of load order.
constexpr uint32_t RPAK_WAV_EVENT_HEADER_STORAGE_SIZE = 384;

struct AudioScriptEventInfo_s
{
	uint64_t eventGuid;
	uint32_t generation;
	uint32_t sourceCount;
	char name[AUDIO_SCRIPT_NAME_MAX];
};

struct AudioScriptSourceInfo_s
{
	uint64_t eventGuid;
	uint64_t sourceGuid;
	uint32_t generation;
	uint32_t sourceIndex;
	float duration;
	char name[AUDIO_SCRIPT_NAME_MAX];
};

struct CSOM_AsyncFile_s
{
	u64 asyncRequestId;
	char fileName[CSOM_MAX_FILE_NAME];
	int fileHandle;
	size_t readOffset;
	size_t readStart;
	size_t fileSize;
};

struct CSOM_BankList_s
{
	char banks[CSOM_MAX_LOADED_BANKS][CSOM_MAX_FILE_NAME];
	int bankCount;
};

/* ==== WASAPI THREAD SERVICE =========================================================================================================================================== */
inline void*(*v_MilesAllocEx)(const size_t size, const u8 flags, void* const driver, const char* const allocSrcFileName, const int allocSrcFileLine);
inline void*(*v_MilesQueueEventRun)(Miles::Queue*, const char*);
inline int(*v_MilesQueueEventVolume)(Miles::Queue*, float);
inline int(*v_MilesQueueEventRateFactor)(Miles::Queue*, float);
inline int(*v_MilesQueueUpdateEventVolume)(Miles::Queue*, void*, float);
inline int(*v_MilesQueueUpdateEventRateFactor)(Miles::Queue*, void*, float);
inline int(*v_MilesQueueEventSignal)(Miles::Queue*, void*, const char*);
inline int(*v_MilesBankGetEventCount)(Miles::Bank*);
inline const char*(*v_MilesBankGetEventName)(Miles::Bank*, int);
inline void(*v_MilesBankPatch)(Miles::Bank*, char*, char*);
inline unsigned int (*v_MilesSampleSetSourceRaw)(__int64 a1, __int64 a2, unsigned int a3, int a4, unsigned __int16 a5, bool a6);
inline unsigned int (*v_MilesEventGetDetails)(__int64 a1, __int64 a2, __int64 a3, __int64 a4, __int64 a5, __int64 a6, void* const releaseList);

inline bool(*v_CSOM_Initialize)();
inline void(*v_CSOM_InitializeBankList)(CSOM_BankList_s* const bankList);
inline void(*v_CSOM_LogFunc)(int64_t nLogLevel, const char* pszMessage);
inline void(*v_CSOM_RunFrame)(char a1, char a2, float a3, float a4);

inline s32(*v_CSOM_MilesAsync_FileRead)(MilesAsyncRead* const request);
inline s32(*v_CSOM_MilesAsync_FileStatus)(MilesAsyncRead* const request, const u32 i_MS);
inline s32(*v_CSOM_MilesAsync_FileCancel)(MilesAsyncRead* const request);

inline void(*v_CSOM_AddEventToQueue)(const char* eventName);

void RPakWav_StopAllPackedVoices();
void RPakWav_StopPackedMusicVoices();
void RPakWav_StopPackedVoiceByEventName(const char* eventName);
void RPakWav_BeginManualMilesPlay();
void RPakWav_EndManualMilesPlay();
void RPakWav_RegisterPakAsset(PakFile_s* pak, const PakAsset_s* asset);

uint32_t AudioScript_GetGeneration();
size_t AudioScript_ListCustomEvents(AudioScriptEventInfo_s* events, size_t capacity);
size_t AudioScript_ListCustomSources(uint64_t eventGuid, AudioScriptSourceInfo_s* sources, size_t capacity);
bool AudioScript_FindCustomEvent(const char* eventName, AudioScriptEventInfo_s& event);
bool AudioScript_FindCustomSource(const char* sourceName, AudioScriptSourceInfo_s& source);
bool AudioScript_IsNativeEvent(const char* eventName);
int AudioScript_PlayNative(const char* eventName, float volume, float pitch, bool forceLoop = false);
int AudioScript_PlayCustom(uint64_t eventGuid, uint32_t sourceIndex, uint64_t sourceGuid, float volume, float pitch, bool forceLoop = false);
bool AudioScript_SetPitch(int voiceId, float pitch);
bool AudioScript_SetVolume(int voiceId, float volume);
bool AudioScript_Stop(int voiceId);

struct CSOM_GlobalState_s
{
	char gap0[24];
	bool mismatchedBuildTag;
	char gap19[43];
	// Engine request kind: 1 = position, 2 = non-spatial, 3 = entity,
	// 4 = position/direction. Entity requests reuse the position's first word.
	uint32_t queuedSoundPositionType;
	char gap48[16];
	uintptr_t queuedEventHash;
	char gap60[4];
	Vector3D queuedSoundPosition;
	char gap70[24];
	float soundMasterVolume;
	char gap8c[28];
	void* samplesXlogType;
	char gapB0[8];
	void* dumpXlogType;
	char gapC0[48];
	void* driver;
	void* queue;
	char gap100[40];
	CSOM_BankList_s bankList;
	char gap52c[4];
	void* loadedBanks[CSOM_MAX_LOADED_BANKS];
	char gap5b0[273016];
	CSOM_AsyncFile_s asyncFiles[CSOM_MAX_ASYNC_FILE_HANDLES];
	size_t asyncRequestIdGen;
	char gap46430[4110];
	HANDLE milesInitializedEvent;
	HANDLE milesThread;
	char gap47450[272];
	char milesOutputBuffer[1024];
	char unk[96];
};

static_assert(offsetof(CSOM_GlobalState_s, queuedSoundPositionType) == 0x44);
static_assert(offsetof(CSOM_GlobalState_s, queuedEventHash) == 0x58);
static_assert(offsetof(CSOM_GlobalState_s, queuedSoundPosition) == 0x64);

inline CSOM_GlobalState_s* g_milesGlobals;

///////////////////////////////////////////////////////////////////////////////
class MilesCore : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("MilesAllocEx", v_MilesAllocEx);
		LogFunAdr("MilesQueueEventRun", v_MilesQueueEventRun);
		LogFunAdr("MilesQueueEventVolume", v_MilesQueueEventVolume);
		LogFunAdr("MilesQueueEventRateFactor", v_MilesQueueEventRateFactor);
		LogFunAdr("MilesQueueUpdateEventVolume", v_MilesQueueUpdateEventVolume);
		LogFunAdr("MilesQueueUpdateEventRateFactor", v_MilesQueueUpdateEventRateFactor);
		LogFunAdr("MilesQueueEventSignal", v_MilesQueueEventSignal);
		LogFunAdr("MilesBankGetEventCount", v_MilesBankGetEventCount);
		LogFunAdr("MilesBankGetEventName", v_MilesBankGetEventName);
		LogFunAdr("MilesBankPatch", v_MilesBankPatch);
		LogFunAdr("MilesSampleSetSourceRaw", v_MilesSampleSetSourceRaw);
		LogFunAdr("MilesEventGetDetails", v_MilesEventGetDetails);
		LogFunAdr("CSOM_Initialize", v_CSOM_Initialize);
		LogFunAdr("CSOM_InitializeBankList", v_CSOM_InitializeBankList);
		LogFunAdr("CSOM_LogFunc", v_CSOM_LogFunc);
		LogFunAdr("CSOM_RunFrame", v_CSOM_RunFrame);
		LogFunAdr("CSOM_MilesAsync_FileRead", v_CSOM_MilesAsync_FileRead);
		LogFunAdr("CSOM_MilesAsync_FileStatus", v_CSOM_MilesAsync_FileStatus);
		LogFunAdr("CSOM_MilesAsync_FileCancel", v_CSOM_MilesAsync_FileCancel);
		LogFunAdr("CSOM_AddEventToQueue", v_CSOM_AddEventToQueue);
		LogVarAdr("g_milesGlobals", g_milesGlobals);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 56 57 41 54 41 55 41 56 41 57 48 81 EC ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ??").GetPtr(v_CSOM_Initialize);
		Module_FindPattern(g_GameDll, "40 55 41 54 41 55 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 83 65").GetPtr(v_CSOM_InitializeBankList);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC 20 48 8B DA 48 8D 15 ?? ?? ?? ??").GetPtr(v_CSOM_LogFunc);
		Module_FindPattern(g_GameDll, "48 8B C4 F3 0F 11 50 ?? 88 50").GetPtr(v_CSOM_RunFrame);
		Module_FindPattern(g_GameDll, "4C 8B DC 53 55 56 57 48 81 EC").GetPtr(v_CSOM_MilesAsync_FileRead);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 57 48 83 EC ?? 8B B9 ?? ?? ?? ?? 48 8B D9 83 FF").GetPtr(v_CSOM_MilesAsync_FileStatus);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC ?? 8B 81 ?? ?? ?? ?? 48 8B D9 83 F8 ?? 75 ?? B8").GetPtr(v_CSOM_MilesAsync_FileCancel);
		Module_FindPattern(g_GameDll, "0F B6 11 4C 8B C1").GetPtr(v_CSOM_AddEventToQueue);
		
		g_RadAudioSystemDll.GetExportedSymbol("MilesAllocEx").GetPtr(v_MilesAllocEx);
		g_RadAudioSystemDll.GetExportedSymbol("MilesQueueEventRun").GetPtr(v_MilesQueueEventRun);
		g_RadAudioSystemDll.GetExportedSymbol("MilesQueueEventVolume").GetPtr(v_MilesQueueEventVolume);
		g_RadAudioSystemDll.GetExportedSymbol("MilesQueueEventRateFactor").GetPtr(v_MilesQueueEventRateFactor);
		g_RadAudioSystemDll.GetExportedSymbol("MilesQueueUpdateEventVolume").GetPtr(v_MilesQueueUpdateEventVolume);
		g_RadAudioSystemDll.GetExportedSymbol("MilesQueueUpdateEventRateFactor").GetPtr(v_MilesQueueUpdateEventRateFactor);
		g_RadAudioSystemDll.GetExportedSymbol("MilesQueueEventSignal").GetPtr(v_MilesQueueEventSignal);
		g_RadAudioSystemDll.GetExportedSymbol("MilesBankGetEventCount").GetPtr(v_MilesBankGetEventCount);
		g_RadAudioSystemDll.GetExportedSymbol("MilesBankGetEventName").GetPtr(v_MilesBankGetEventName);
		g_RadAudioSystemDll.GetExportedSymbol("MilesBankPatch").GetPtr(v_MilesBankPatch);
		g_RadAudioSystemDll.GetExportedSymbol("MilesSampleSetSourceRaw").GetPtr(v_MilesSampleSetSourceRaw);
		g_RadAudioSystemDll.GetExportedSymbol("MilesEventGetDetails").GetPtr(v_MilesEventGetDetails);
	}
	virtual void GetVar(void) const
	{
		g_milesGlobals = CMemory(v_CSOM_Initialize).FindPatternSelf("48 8D", CMemory::Direction::DOWN, 0x50).ResolveRelativeAddressSelf(0x3, 0x7).RCast<CSOM_GlobalState_s*>();
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
