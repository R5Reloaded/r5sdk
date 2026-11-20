#pragma once
#include "tier0/jobthread.h"
#include "tier1/fmtstr.h"
#include "rtech/ipakfile.h"
#include "vpklib/packedstore.h"

//-----------------------------------------------------------------------------
// Forward declarations
//-----------------------------------------------------------------------------
class KeyValues;

//-----------------------------------------------------------------------------
// this structure contains handles and names to the base pak files the engine
// loads for a level, this is used for load/unload management during level
// changes or engine shutdown
//-----------------------------------------------------------------------------
struct CommonPakData_s
{
	enum PakType_e
	{
		// the UI pak assigned to the current gamemode (range in GameMode_t)
		PAK_TYPE_UI_GM = 0,
		PAK_TYPE_COMMON,

		// the base pak assigned to the current gamemode (range in GameMode_t)
		PAK_TYPE_COMMON_GM,
		PAK_TYPE_LOBBY,

		// NOTE: this one is assigned to the name of the level, the prior ones are
		// all static!
		PAK_TYPE_LEVEL,

		// the total number of pak files to watch and manage
		PAK_TYPE_COUNT
	};

	CommonPakData_s()
	{
		pakId = PAK_INVALID_HANDLE;
		isUnloading = false;
		isCustomPakLoaded = false;
		basePakName = nullptr;

		memset(pakName, '\0', sizeof(pakName));
	}

	PakHandle_t pakId;
	bool isUnloading;

	// the pak name that's being requested to be loaded for this particular slot
	char pakName[MAX_OSPATH];

	/*-------------------
	 | 7 pad bytes here |
	 -------------------*/

	// NEW: true if we loaded the custom pak that is linked to this base pak
	// so we can initiate the LoadAsync call on the custom pak (paks from
	// CustomPakData_s). This new member uses the pad bytes.
	bool isCustomPakLoaded;

	// the actual base pak name, like "common_pve.rpak" as set when this array is
	// being initialized
	const char* basePakName;
};

//-----------------------------------------------------------------------------
// this structure contains handles and names to the custom pak files that are
// loaded with the settings KV for that level, these paks are loaded after the
// common paks are loaded, but unloaded before the common paks are unloaded
//-----------------------------------------------------------------------------
struct CustomPakData_s
{
	enum PakType_e
	{
		// the pak that loads after CommonPakData_t::PAK_TYPE_UI_GM has loaded, and
		// unloads before CommonPakData_t::PAK_TYPE_UI_GM gets unloaded
		PAK_TYPE_UI_SDK = 0,

		// the pak that loads after CommonPakData_t::PAK_TYPE_COMMON_GM has loaded,
		// and unloads before CommonPakData_t::PAK_TYPE_COMMON_GM gets unloaded
		PAK_TYPE_COMMON_SDK,

		// the total number of base SDK pak files
		PAK_TYPE_COUNT
	};

	enum
	{
		// the absolute max number of custom paks, note that the engine's limit
		// could still be reached before this number as game scripts and other
		// code still loads paks such as gladiator cards or load screens
		MAX_CUSTOM_PAKS = (PAK_MAX_LOADED_PAKS - CommonPakData_s::PAK_TYPE_COUNT)
	};

	CustomPakData_s()
	{
		for (size_t i = 0; i < V_ARRAYSIZE(handles); i++)
		{
			handles[i] = PAK_INVALID_HANDLE;
		}

		// the first # handles are reserved for base SDK paks
		numHandles = PAK_TYPE_COUNT;
		numPreload = 0;
		numMods = 0;

		inLobby = false;
		reprocessUserLevelPaks = false;
		reprocessUserLevelPaksUnloadFinished = false;
		reprocessUserLevelPaksLoadCalled = false;
	}

	PakHandle_t LoadAndAddPak(const char* const pakFile, const bool isMod);
	PakHandle_t PreloadAndAddPak(const char* const pakFile);

	bool UnloadAndRemoveNonPreloaded(const bool modsOnly);
	bool UnloadAndRemovePreloaded();

	PakHandle_t LoadBasePak(const char* const pakFile, const PakType_e type);
	bool UnloadBasePak(const PakType_e type);

private:
	bool UnloadAndRemovePak(const int index);

public:
	// Pak handles that have been loaded with the level
	// from within the level settings KV (located in
	// scripts/levels/settings/*.kv). On level unload,
	// each pak listed in this vector gets unloaded.
	PakHandle_t handles[MAX_CUSTOM_PAKS];

	int numHandles; // Total number of loaded non-base SDK paks.
	int numPreload; // preloaded paks come after base SDK paks.
	int numMods;    // level mod paks come after level core and base SDK paks.

	// True if we are currently in the lobby map.
	bool inLobby;
	bool reprocessUserLevelPaks;
	bool reprocessUserLevelPaksUnloadFinished;
	bool reprocessUserLevelPaksLoadCalled;

	CFmtStrN<MAX_MAP_NAME> lastPrecachedLevel;
	CFmtStrN<MAX_PLAYLIST_NAME> lastPlaylistUsedForPrecache;
};

// array size = CommonPakData_t::PAK_TYPE_COUNT
inline CommonPakData_s* g_commonPakData;

inline void(*v_Mod_PrecacheLevelAssets)(const char* const fullLevelFileName, const char* const levelName, const bool allowVpkLoadFail);

inline void(*v_Mod_RunPakJobFrame)(void);
inline void(*v_Mod_LoadLoadscreenPakForLevel)(const char* const levelName);

inline int32_t * g_pNumPrecacheItemsMTVTF;
inline bool* g_pPakPrecacheJobFinished;

inline void(*v_Mod_UnloadPendingAndPrecacheRequestedPaks)(void);

inline void(*v_Mod_UnloadCurrentLevelVPK)(void);

inline CPackedStore** g_currentLevelVPK = nullptr;
inline char* g_currentLevelVPKName = nullptr;

extern CUtlVector<CUtlString> g_InstalledMaps;
extern CThreadMutex g_InstalledMapsMutex;

extern void Mod_GetAllInstalledMaps();

extern void Mod_InitiateUserLevelModPaksReprocess();
extern void Mod_CancelUserLevelModPaksReprocess();

extern void Mod_SetPrecacheLevelName(const char* const levelName);
extern void Mod_SetPrecachePlaylistName(const char* const playlistName);
extern KeyValues* Mod_GetLevelCoreSettings(const char* pszLevelName);

extern void Mod_PrecacheLevelAssets(const char* const fullLevelFileName, const char* const levelName, const bool allowVpkLoadFail);
extern void Mod_LoadLoadscreenPakForLevel(const char* const levelName);

///////////////////////////////////////////////////////////////////////////////
class VModel_BSP : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("Mod_PrecacheLevelAssets", v_Mod_PrecacheLevelAssets);

		LogFunAdr("Mod_RunPakJobFrame", v_Mod_RunPakJobFrame);
		LogFunAdr("Mod_LoadLoadscreenPakForMap", v_Mod_LoadLoadscreenPakForLevel);

		LogFunAdr("Mod_UnloadPendingAndPrecacheRequestedPaks", v_Mod_UnloadPendingAndPrecacheRequestedPaks);

		LogFunAdr("Mod_UnloadCurrentLevelVPK", v_Mod_UnloadCurrentLevelVPK);

		LogVarAdr("g_numPrecacheItemsMTVTF", g_pNumPrecacheItemsMTVTF);
		LogVarAdr("g_pakPrecacheJobFinished", g_pPakPrecacheJobFinished);

		LogVarAdr("g_currentLevelVPK", g_currentLevelVPK);
		LogVarAdr("g_currentLevelVPKName", g_currentLevelVPKName);

		LogVarAdr("g_commonPakData", g_commonPakData);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "44 88 44 24 ?? 53 55 56 57").GetPtr(v_Mod_PrecacheLevelAssets);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC ?? F3 0F 10 05 ?? ?? ?? ?? 32 DB").GetPtr(v_Mod_RunPakJobFrame);
		Module_FindPattern(g_GameDll, "48 81 EC ?? ?? ?? ?? 0F B6 05 ?? ?? ?? ?? 4C 8D 05 ?? ?? ?? ?? 84 C0").GetPtr(v_Mod_LoadLoadscreenPakForLevel);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 33 ED 48 8D 35 ?? ?? ?? ?? 48 39 2D ?? ?? ?? ??").GetPtr(v_Mod_UnloadPendingAndPrecacheRequestedPaks);
		Module_FindPattern(g_GameDll, "48 83 EC 28 48 8B 15 ?? ?? ?? ?? 48 85 D2 0F 84 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8B 01 FF 90 ?? ?? ?? ?? 33 C9 E8 ?? ?? ?? ?? 0F 28 05 ?? ?? ?? ?? 0F 28 0D ?? ?? ?? ?? 0F 11 05 ?? ?? ?? ?? 0F 28 05 ?? ?? ?? ?? 0F 11 0D ?? ?? ?? ?? 0F 28 0D ?? ?? ?? ?? 0F 11 05 ?? ?? ?? ?? 0F 11 0D ?? ?? ?? ?? 48 C7 05 ?? ?? ?? ?? ?? ?? ?? ?? FF 15 ?? ?? ?? ??").GetPtr(v_Mod_UnloadCurrentLevelVPK);
	}
	virtual void GetVar(void) const
	{
		g_pNumPrecacheItemsMTVTF = CMemory(v_Mod_RunPakJobFrame).FindPattern("8B 05").ResolveRelativeAddressSelf(0x2, 0x6).RCast<int32_t*>();
		g_pPakPrecacheJobFinished = CMemory(v_Mod_RunPakJobFrame).Offset(0x20).FindPatternSelf("88 1D").ResolveRelativeAddressSelf(0x2, 0x6).RCast<bool*>();

		g_currentLevelVPK = CMemory(v_Mod_UnloadCurrentLevelVPK).FindPattern("48 8B", CMemory::Direction::DOWN, 250).ResolveRelativeAddressSelf(0x3, 0x7).RCast<CPackedStore**>();
		g_currentLevelVPKName = CMemory(v_Mod_UnloadCurrentLevelVPK).FindPattern("C6 05", CMemory::Direction::DOWN, 250).ResolveRelativeAddressSelf(0x2, 0x7).RCast<char*>();

		CMemory(v_Mod_RunPakJobFrame).Offset(0xA0).FindPatternSelf("48 8D 2D").ResolveRelativeAddressSelf(0x3, 0x7).GetPtr(g_commonPakData);
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
