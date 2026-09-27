//=============================================================================//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "core/stdafx.h"
#include "tier0/memstd.h"
#include "tier0/jobthread.h"
#include "tier1/fmtstr.h"
#include "tier1/keyvalues.h"
#include "tier2/fileutils.h"
#include "engine/sys_dll2.h"
#include "engine/host_cmd.h"
#include "engine/host_state.h"
#include "engine/cmodel_bsp.h"

#include "rtech/rson.h"
#include "rtech/pak/pakstate.h"
#include "rtech/pak/pakparse.h"
#include "rtech/pak/paktools.h"
#include "rtech/pak/pakstream.h"
#include "rtech/playlists/playlists.h"

#include "vpklib/packedstore.h"
#include "datacache/mdlcache.h"
#include "filesystem/filesystem.h"
#include "pluginsystem/modsystem.h"
#ifndef DEDICATED
#include "client/clientstate.h"
#include "client/community_party.h"
#endif // !DEDICATED

CUtlVector<CUtlString> g_InstalledMaps;
static CustomPakData_s s_customPakData;

//-----------------------------------------------------------------------------
// Purpose: load a custom pak and add it to the list
// Input  : *pakFile - 
// Output : pak handle, PAK_INVALID_HANDLE on failure
//-----------------------------------------------------------------------------
PakHandle_t CustomPakData_s::LoadAndAddPak(const char* const pakFile, const bool isMod)
{
    if (numHandles >= MAX_CUSTOM_PAKS)
    {
        Error(eDLL_T::ENGINE, NO_ERROR, "Tried to load pak '%s', but already reached the limit of %d!\n", pakFile, MAX_CUSTOM_PAKS);
        return PAK_INVALID_HANDLE;
    }

    const PakHandle_t pakId = g_pakLoadApi->LoadAsync(pakFile, AlignedMemAlloc(), 4, 0);

    // failure, don't add; return the invalid handle.
    if (pakId == PAK_INVALID_HANDLE)
        return pakId;

    handles[numHandles++] = pakId;

    if (isMod)
        numMods++;

    return pakId;
}

//-----------------------------------------------------------------------------
// Purpose: unload the SDK pak file by index
// Input  : index - index into `handles`
// Output : true if the given pak is unloaded
//-----------------------------------------------------------------------------
bool CustomPakData_s::UnloadAndRemovePak(const int index)
{
    const PakHandle_t pakId = handles[index];

    // Only unload if it was actually successfully loaded
    if (pakId == PAK_INVALID_HANDLE)
        return true;

    const PakLoadedInfo_s* const pakInfo = Pak_GetPakInfo(pakId);

    if (pakInfo->status == PAK_STATUS_LOADED)
        g_pakLoadApi->UnloadAsync(pakId);

    if (pakInfo->status != PAK_STATUS_FREED &&
        pakInfo->status != PAK_STATUS_ERROR &&
        pakInfo->status != PAK_STATUS_INVALID_PAKHANDLE)
    {
        // Unload is still pending.
        return false;
    }

    // Pak is unloaded, clear and return.
    handles[index] = PAK_INVALID_HANDLE;
    return true;
}
//-----------------------------------------------------------------------------
// Purpose: preload a custom pak; this keeps it available throughout the
//          duration of the process, unless manually removed by user.
// Input  : *pakFile - 
// Output : pak handle, PAK_INVALID_HANDLE on failure
//-----------------------------------------------------------------------------
PakHandle_t CustomPakData_s::PreloadAndAddPak(const char* const pakFile)
{
    // this must never be called after a non-preloaded pak has been added!
    // preloaded paks must always appear before custom user requested paks
    // due to the unload order: user-requested -> preloaded -> sdk -> core.
    assert(handles[CustomPakData_s::PAK_TYPE_COUNT+numPreload] == PAK_INVALID_HANDLE);

    const PakHandle_t pakId = LoadAndAddPak(pakFile, false);

    if (pakId != PAK_INVALID_HANDLE)
        numPreload++;

    return pakId;
}

//-----------------------------------------------------------------------------
// Purpose: unloads all non-preloaded custom pak handles, keep calling this
//          over time until it returns true
// Output : true if the non-preloaded paks are unloaded
//-----------------------------------------------------------------------------
bool CustomPakData_s::UnloadAndRemoveNonPreloaded(const bool modsOnly)
{
    if (modsOnly && numMods == 0)
        return true;

    // Preloaded paks should not be unloaded here, but only right before sdk /
    // engine paks are unloaded. Only unload user requested and level settings
    // paks from here. Unload them in reverse order, the last pak loaded should
    // be the first one to be unloaded.
    for (int n = (numHandles-1); n >= CustomPakData_s::PAK_TYPE_COUNT + numPreload; n--)
    {
        if (!UnloadAndRemovePak(n))
            return false;

        numHandles--;

        if (numMods > 0)
        {
            numMods--;

            if (modsOnly && numMods == 0)
                return true;
        }
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: unloads all preloaded custom pak handles, keep calling this
//          over time until it returns true
// Output : true if the preloaded paks are unloaded
//-----------------------------------------------------------------------------
bool CustomPakData_s::UnloadAndRemovePreloaded()
{
    // Unload them in reverse order, the last pak loaded should be the first
    // one to be unloaded.
    for (; numPreload > 0; numPreload--)
    {
        if (!UnloadAndRemovePak(CustomPakData_s::PAK_TYPE_COUNT + (numPreload-1)))
            return false;

        numHandles--;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: loads the base SDK pak file by type
// Input  : *pakFile - 
//          type     - 
// Output : pak handle, PAK_INVALID_HANDLE on failure
//-----------------------------------------------------------------------------
PakHandle_t CustomPakData_s::LoadBasePak(const char* const pakFile, const PakType_e type)
{
    const PakHandle_t pakId = g_pakLoadApi->LoadAsync(pakFile, AlignedMemAlloc(), 4, 0);

    // the file is most likely missing
    assert(pakId != PAK_INVALID_HANDLE);
    handles[type] = pakId;

    return pakId;
}

//-----------------------------------------------------------------------------
// Purpose: unload the SDK base pak file by type, keep calling this
//          over time until it returns true
// Input  : type - 
// Output : true if the given pak is unloaded
//-----------------------------------------------------------------------------
bool CustomPakData_s::UnloadBasePak(const PakType_e type)
{
    return UnloadAndRemovePak(type);
}

//-----------------------------------------------------------------------------
// Purpose: checks if level has changed
// Input  : *levelName - 
// Output : true if level name deviates from previous level
//-----------------------------------------------------------------------------
static bool Mod_LevelHasChanged(const char* const levelName)
{
    return (V_strcmp(levelName, s_customPakData.lastPrecachedLevel) != NULL);
}

//-----------------------------------------------------------------------------
// Purpose: checks if playlist has changed
// Input  : *playlistName - 
// Output : true if playlist name deviates from previous playlist
//-----------------------------------------------------------------------------
static bool Mod_PlaylistHasChanged(const char* const playlistName)
{
    return (V_strcmp(playlistName, s_customPakData.lastPlaylistUsedForPrecache) != NULL);
}

//-----------------------------------------------------------------------------
// Purpose: gets all installed maps
//-----------------------------------------------------------------------------
void Mod_GetAllInstalledMaps()
{
    CUtlVector<CUtlString> fileList;
    AddFilesToList(fileList, "vpk", "vpk", "GAME", '/');

    boost::cmatch regexMatches;
    AUTO_LOCK(g_InstalledMapsMutex);

    g_InstalledMaps.Purge(); // Clear current list.

    FOR_EACH_VEC(fileList, i)
    {
        const CUtlString& filePath = fileList[i];

        const char* const pathBase = filePath.String();
        const ssize_t pathLength = filePath.Length();

        ssize_t fileNameStart = pathLength;

        // Get the unqualified file name.
        while (fileNameStart)
        {
            if (pathBase[fileNameStart] == '/')
            {
                fileNameStart++; // Skip the '/'.
                break;
            }

            fileNameStart--;
        }

        const bool result = boost::regex_match(&pathBase[fileNameStart], &pathBase[pathLength], regexMatches, g_VpkDirFileRegex);

        if (!result || regexMatches.empty())
            continue;

        const boost::csub_match& match = regexMatches[2];
        const std::string mapName = match.str();

        if (mapName.compare("frontend") == 0)
            continue; // Frontend contains no BSP's.

        else if (mapName.compare("mp_common") == 0)
        {
            if (!g_InstalledMaps.HasElement("mp_lobby"))
                g_InstalledMaps.AddToTail("mp_lobby");

            continue; // Common contains mp_lobby.
        }
        else
        {
            bool found = false;

            FOR_EACH_VEC(g_InstalledMaps, j)
            {
                const CUtlString& installedMap = g_InstalledMaps[j];

                if (installedMap.IsEqual_CaseSensitive(mapName.c_str()))
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                const int index = g_InstalledMaps.AddToTail();
                CUtlString& entry = g_InstalledMaps.Element(index);

                entry.SetDirect(mapName.c_str(), (ssize_t)mapName.length());
            }
        }
    }
}

//-----------------------------------------------------------------------------
// Purpose: returns whether the load job for given pak id is finished
// Input  : pakId - 
// Output : true if the load job is finished
//-----------------------------------------------------------------------------
static bool Mod_IsPakLoadFinished(const PakHandle_t pakId)
{
    if (pakId == PAK_INVALID_HANDLE)
        return true;

    const PakLoadedInfo_s* const pli = Pak_GetPakInfo(pakId);

    if (pli->handle != pakId)
        return false;

    const PakStatus_e stat = pli->status;

    if (stat != PakStatus_e::PAK_STATUS_LOADED && 
        stat != PakStatus_e::PAK_STATUS_ERROR)
        return false;

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: returns whether the load job for custom pak batch for given common
//          pak is finished
//-----------------------------------------------------------------------------
static bool Mod_IsCustomPakLoadFinished(const int commonType)
{
    switch (commonType)
    {
    case CommonPakData_s::PakType_e::PAK_TYPE_UI_GM:
#ifndef DEDICATED
        return Mod_IsPakLoadFinished(s_customPakData.handles[CustomPakData_s::PakType_e::PAK_TYPE_UI_SDK]);
#else // Dedicated doesn't load UI paks.
        return true;
#endif // DEDICATED
    case CommonPakData_s::PakType_e::PAK_TYPE_COMMON:
        return true;
    case CommonPakData_s::PakType_e::PAK_TYPE_COMMON_GM:
        return Mod_IsPakLoadFinished(s_customPakData.handles[CustomPakData_s::PakType_e::PAK_TYPE_COMMON_SDK]);
    case CommonPakData_s::PakType_e::PAK_TYPE_LOBBY:
        // Check for preloaded paks at this stage (loaded from preload.rson).
        for (int i = 0, n = s_customPakData.numPreload; i < n; i++)
        {
            if (!Mod_IsPakLoadFinished(s_customPakData.handles[CustomPakData_s::PAK_TYPE_COUNT + i]))
                return false;
        }
        break;
    case CommonPakData_s::PakType_e::PAK_TYPE_LEVEL:
        // Check for extra level paks at this stage (loaded from <levelname>.kv).
        for (int i = CustomPakData_s::PAK_TYPE_COUNT + s_customPakData.numPreload, n = s_customPakData.numHandles; i < n; i++)
        {
            if (!Mod_IsPakLoadFinished(s_customPakData.handles[i]))
                return false;
        }
        break;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: formats the path to a file residing inside the paks directory
// Input  : (&pOut)   - 
//          *rootPath - 
//          *fileName - 
//-----------------------------------------------------------------------------
template <typename T, int N>
static void Mod_FormatPakPath(T(&pOut)[N], const char* const rootPath, const char* const fileName)
{
    const int ret = V_snprintf(pOut, N, "%s%s%s", rootPath, Pak_GetReadPath(), fileName);

    if (ret < 0 || ret >= N)
        Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: failure encoding path for file \"%s\" in root \"%s\"\n", __FUNCTION__, fileName, rootPath);

    V_FixSlashes(pOut);
}

//-----------------------------------------------------------------------------
// Purpose: preload paks in list and keeps them active throughout level changes
//-----------------------------------------------------------------------------
static void Mod_PreloadPaks(const char* const rootPath)
{
    char preloadFileBuf[MAX_OSPATH];
    Mod_FormatPakPath(preloadFileBuf, rootPath, "preload.rson");

    bool parseFailure;
    RSON::Node_t* const rson = RSON::LoadFromFile(preloadFileBuf, nullptr, &parseFailure);

    if (!rson)
    {
        if (parseFailure)
            Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: failure parsing file \"%s\"\n", __FUNCTION__, preloadFileBuf);

        return; // No pak preload file, just return out.
    }

    static const char* const arrayName = "Paks";
    const RSON::Field_t* const key = rson->FindKey(arrayName);

    if (!key)
        Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: missing array key \"%s\" in file \"%s\"\n", __FUNCTION__, arrayName, preloadFileBuf);

    if ((key->node.type != (RSON::eFieldType::RSON_ARRAY | RSON::eFieldType::RSON_STRING)) &&
        (key->node.type != (RSON::eFieldType::RSON_ARRAY | RSON::eFieldType::RSON_VALUE)))
    {
        Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: expected an array of strings in file \"%s\"\n", __FUNCTION__, preloadFileBuf);
    }

    for (int i = 0; i < key->node.valueCount; i++)
    {
        const RSON::Value_t* const value = key->node.GetArrayValue(i);
        const char* pakPath;

        if (*rootPath)
        {
            Mod_FormatPakPath(preloadFileBuf, rootPath, value->stringValue);
            pakPath = preloadFileBuf;
        }
        else
        {
            // For core paks, we shouldn't prepend the path.
            pakPath = value->stringValue;
        }

        s_customPakData.PreloadAndAddPak(pakPath);
    }

    RSON_Free(rson, AlignedMemAlloc());
    AlignedMemAlloc()->Free(rson);
}

//-----------------------------------------------------------------------------
// Purpose: preloads all mod pak files
//-----------------------------------------------------------------------------
static void Mod_PreloadAllPaks()
{
    // Preload core paks.
    Mod_PreloadPaks("");

    if (ModSystem()->IsEnabled())
    {
        ModSystem()->LockModList();

        // Preload mod paks.
        FOR_EACH_VEC(ModSystem()->GetModList(), i)
        {
            const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

            if (!mod->IsEnabled())
                continue;

            Mod_PreloadPaks(mod->GetBasePath().String());
        }

        ModSystem()->UnlockModList();
    }
}

//-----------------------------------------------------------------------------
// Purpose: unloads all preloaded paks
// Output : true if the preloaded paks are unloaded
//-----------------------------------------------------------------------------
static bool Mod_UnloadPreloadedPaks()
{
    if (!s_customPakData.UnloadAndRemovePreloaded())
        return false;

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: initiates the reprocess of all user level mod paks, call this when
//          the playlist changes as we need to re-evaluate which paks need to
//          be unloaded and loaded as some mod paks are necessary on certain
//          playlists while others aren't
//-----------------------------------------------------------------------------
void Mod_InitiateUserLevelModPaksReprocess()
{
    s_customPakData.reprocessUserLevelPaks = true;
    s_customPakData.reprocessUserLevelPaksUnloadFinished = false;
    s_customPakData.reprocessUserLevelPaksLoadCalled = false;

    *g_pPakPrecacheJobFinished = false;
}

//-----------------------------------------------------------------------------
// Purpose: cancel the user level mod paks reprocess request
//-----------------------------------------------------------------------------
void Mod_CancelUserLevelModPaksReprocess()
{
    s_customPakData.reprocessUserLevelPaks = false;
    s_customPakData.reprocessUserLevelPaksUnloadFinished = true;
    s_customPakData.reprocessUserLevelPaksLoadCalled = true;
}

//-----------------------------------------------------------------------------
// Purpose: gets the playlist we are currently looking for
//-----------------------------------------------------------------------------
static const char* Mod_GetTargetPlaylistForPreload()
{
#ifndef DEDICATED
    // For client builds, we need to be aware of our party because the engine
    // uses this to precache level assets; it calls Party_GetTargetMap() which
    // internally does the same thing as Party_GetTargetPlaylist(), except it
    // retrieves the target map from the given 'target' playlist. We need to
    // resolve the playlist that was used to retrieve the target level so we
    // can load the correct level mod paks.
    return Party_GetTargetPlaylist();
#else
    // For server builds, life's a lot easier; we just take whatever our
    // current playlist is and return that.
    return v_Playlists_GetCurrent();
#endif
}

//-----------------------------------------------------------------------------
// Purpose: handles the level change, update's SDK's internal state and loads
//          the level's load screen
// Input  : *levelName - 
//-----------------------------------------------------------------------------
static void Mod_HandleLevelChanged(const char* const levelName)
{
    const bool levelChanged = Mod_LevelHasChanged(levelName);

    if (levelChanged)
    {
        // Lobby should be handled specially, as changing level to lobby will
        // retain all currently loaded paks as an optimization since its likely
        // that we will be loading the previous level again. However we still
        // want to load/unload our level mod paks based on the map and mode we
        // are on. Detect the changing to/from lobby level here and handle it.
        s_customPakData.inLobby = V_strcmp(levelName, "mp_lobby") == NULL;

        // note(kawe): corner case; if we are remounting paks (either through
        // the concommand `pak_emulateremount` or an actual scenario, and we
        // load the lobby directly after while its still reloading the paks,
        // the system will call `Mod_LoadAllLevelPaks` while we are loading the
        // lobby. We should retain the last precached level here and not update
        // it when we go into the lobby to account for this scenario. This is
        // technically also the correct behavior since the lobby isn't a level
        // in the context of this system (lobby has its own slot in the main
        // `CommonPakData_s` structure, with a higher priority than the level
        // paks. The lobby paks are also always loaded and persistent accross
        // all level changes).
        if (!s_customPakData.inLobby)
            s_customPakData.lastPrecachedLevel = levelName;
    }

    // We should retain all paks in lobby, do not initiate a reprocess unless
    // we are precaching assets for a new level. (this is for SetLevelNameForLoading,
    // and possibly PreCacheLevelDuringVideo, test it for this script func). If we
    // precache, `inLobby` will be set to false in `Mod_SetPrecacheLevelName` and
    // `Mod_SetPrecachePlaylistName`.
    if (!s_customPakData.inLobby)
    {
        const char* const playlistName = Mod_GetTargetPlaylistForPreload();

        if (Mod_PlaylistHasChanged(playlistName))
        {
            Mod_InitiateUserLevelModPaksReprocess();
            s_customPakData.lastPlaylistUsedForPrecache = playlistName;
        }
    }
}

#define MOD_LEVEL_SETTINGS_PATH "scripts/levels/settings/"

//-----------------------------------------------------------------------------
// Purpose: loads the level settings file relative from provided root
// Input  : *levelName - 
//          *rootPath - 
// Output : KeyValues*, nullptr on failure
//-----------------------------------------------------------------------------
static KeyValues* Mod_GetLevelSettings(const char* const levelName, const char* const rootPath)
{
    char pathBuf[MAX_OSPATH];
    snprintf(pathBuf, sizeof(pathBuf), "%s%s%s.kv", rootPath, MOD_LEVEL_SETTINGS_PATH, levelName);

    return FileSystem()->LoadKeyValues(IFileSystem::TYPE_LEVELSETTINGS, pathBuf, "GAME");
}

//-----------------------------------------------------------------------------
// Purpose: loads the level core settings file.
// Input  : *levelName - 
// Output : KeyValues*, nullptr on failure
//-----------------------------------------------------------------------------
KeyValues* Mod_GetLevelCoreSettings(const char* const levelName)
{
    return Mod_GetLevelSettings(levelName, "");
}

enum PakLoadContext_e
{
    PLC_SERVER = 0, // Load server-side only.
    PLC_CLIENT,     // Load client-side only.
    PLC_BOTH        // Load on both.
};

//-----------------------------------------------------------------------------
// Purpose: determines if the pak should be loaded in the current context
// Input  : mode - 
// Output : true if we should load it, false otherwise
//-----------------------------------------------------------------------------
static bool Mod_ShouldLoadPakInCurrentContext(const int mode)
{
#ifdef DEDICATED
    return (mode == PLC_SERVER || mode == PLC_BOTH);
#elif defined CLIENT_DLL
    return (mode == PLC_CLIENT || mode == PLC_BOTH);
#else
    if (HostState_IsTransitioningToLoad()) // If we are the host, load everything.
        return (mode == PLC_SERVER || mode == PLC_CLIENT || mode == PLC_BOTH);
    else // We are just a client connecting to a remote server.
        return (mode == PLC_CLIENT || mode == PLC_BOTH);
#endif
}

//-----------------------------------------------------------------------------
// Purpose: loads paks specified inside the level settings file
// Input  : *settingsKV - 
//          *rootPath   - 
//-----------------------------------------------------------------------------
static void Mod_LoadLevelPaks(KeyValues* const settingsKV, const char* const rootPath)
{
    Assert(settingsKV);
    KeyValues* const pakListKV = settingsKV->FindKey("PakList");

    if (!pakListKV)
        return;

    char pathBuf[MAX_OSPATH];

    for (KeyValues* subKey = pakListKV->GetFirstSubKey(); subKey != nullptr; subKey = subKey->GetNextKey())
    {
        const int mode = subKey->GetInt(nullptr, -1);

        if (!Mod_ShouldLoadPakInCurrentContext(mode))
            continue; // This pak should not be loaded in our current context.

        const char* pakToLoad;
        bool isMod;

        if (*rootPath)
        {
            Mod_FormatPakPath(pathBuf, rootPath, subKey->GetName());

            pakToLoad = pathBuf;
            isMod = true;
        }
        else
        {
            pakToLoad = subKey->GetName();
            isMod = false;
        }

        const PakHandle_t pakId = s_customPakData.LoadAndAddPak(pakToLoad, isMod);

        if (pakId == PAK_INVALID_HANDLE)
            Error(eDLL_T::ENGINE, NO_ERROR, "%s: unable to load pak '%s'\n", __FUNCTION__, pathBuf);
    }
}

//-----------------------------------------------------------------------------
// Purpose: load core mod paks for this level
// Input  : *levelName - 
//-----------------------------------------------------------------------------
static void Mod_LoadLevelCorePaks(const char* const levelName)
{
    KeyValues* const coreSettingsKV = Mod_GetLevelCoreSettings(levelName);

    if (coreSettingsKV)
    {
        Mod_LoadLevelPaks(coreSettingsKV, "");
        coreSettingsKV->DeleteThis();
    }
}

//-----------------------------------------------------------------------------
// Purpose: load user mod paks for this level
// Input  : *levelName - 
//-----------------------------------------------------------------------------
static void Mod_LoadLevelModPaks(const char* const levelName)
{
    if (!ModSystem()->IsEnabled())
        return;

    const char* const targetPlaylist = s_customPakData.lastPlaylistUsedForPrecache;

    if (!*targetPlaylist)
        return;

    ModSystem()->LockModList();

    FOR_EACH_VEC(ModSystem()->GetModList(), i)
    {
        const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

        if (!mod->IsEnabled())
            continue;

        if (!mod->ShouldLoadPaks(targetPlaylist))
            continue;

        const char* const rootPath = mod->GetBasePath().String();
        KeyValues* const modSettingsKV = Mod_GetLevelSettings(levelName, rootPath);

        if (modSettingsKV)
        {
            Mod_LoadLevelPaks(modSettingsKV, rootPath);
            modSettingsKV->DeleteThis();
        }
    }

    ModSystem()->UnlockModList();
}

//-----------------------------------------------------------------------------
// Purpose: load all mod paks for this level
// Input  : modsOnly - 
//-----------------------------------------------------------------------------
static void Mod_LoadAllLevelPaks(const bool modsOnly)
{
    const char* const levelToUse = s_customPakData.lastPrecachedLevel;

    if (!modsOnly)
        Mod_LoadLevelCorePaks(levelToUse); // Load level core paks.

    Mod_LoadLevelModPaks(levelToUse); // Load level mod paks.
}

//-----------------------------------------------------------------------------
// Purpose: unloads all paks loaded by the level settings file
// Input  : modsOnly - 
//-----------------------------------------------------------------------------
static bool Mod_UnloadLevelPaks(const bool modsOnly)
{
    if (!s_customPakData.UnloadAndRemoveNonPreloaded(modsOnly))
        return false;

    // NOTE: if we only unload level mod paks, we shouldn't
    // clear the bad model handles because the actual level
    // resources are still loaded, just mods that are gone.
    if (!modsOnly)
    {
        g_StudioMdlFallbackHandler.ClearBadModelHandleCache();
        g_StudioMdlFallbackHandler.ClearSuppresionList();

        // The old gather props is set if a model couldn't be
        // loaded properly. If we unload level assets, we just
        // enable the new implementation again and re-evaluate
        // on the next level load. If we load a missing/bad
        // model again, we toggle the old implementation as
        // otherwise the fallback models won't render; the new
        // gather props solution does not attempt to obtain
        // studio hardware data on bad mdl handles. See
        // 'GatherStaticPropsSecondPass_PreInit()' for details.
        g_StudioMdlFallbackHandler.DisableLegacyGatherProps();
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpuse: scans the list of loaded common paks and returns the type we should
//          unload; all paks starting from the tail until (and including) the
//          returned type should be unloaded.
// Output : int, maps to CommonPakData_s::PakType_e
//-----------------------------------------------------------------------------
static int Mod_GetTargetPakToUnloadType()
{
    int endIndex;
    for (endIndex = 0; endIndex < CommonPakData_s::PAK_TYPE_COUNT; ++endIndex)
    {
        const CommonPakData_s& cpd = g_commonPakData[endIndex];

        if (cpd.isUnloading)
            break; // This pak is currently unloading, break and return idx.

        if (V_strcmp(cpd.pakName, cpd.basePakName))
            break; // Different pak pending to load, break and return idx.
    }

    return endIndex;
}

//-----------------------------------------------------------------------------
// Purpose: handles custom pak unload for type
// Output : true if the custom pak(s) unload jobs are finished
//-----------------------------------------------------------------------------
static bool Mod_HandleCustomPakUnloadForType(const int type)
{
    // SDK pak files must be unloaded before the engine pak files,
    // as we use assets within engine pak files.
    switch (type)
    {
#ifndef DEDICATED
    case CommonPakData_s::PakType_e::PAK_TYPE_UI_GM:
    {
        if (!s_customPakData.UnloadBasePak(CustomPakData_s::PakType_e::PAK_TYPE_UI_SDK))
            return false;

        break;
    }
#endif // !DEDICATED
    case CommonPakData_s::PakType_e::PAK_TYPE_COMMON:
    {
        g_StudioMdlFallbackHandler.Clear();
        break;
    }
    case CommonPakData_s::PakType_e::PAK_TYPE_COMMON_GM:
    {
        if (!s_customPakData.UnloadBasePak(CustomPakData_s::PakType_e::PAK_TYPE_COMMON_SDK))
            return false;

        break;
    }
    case CommonPakData_s::PakType_e::PAK_TYPE_LOBBY:
    {
        if (!Mod_UnloadPreloadedPaks())
            return false;

        break;
    }
    case CommonPakData_s::PakType_e::PAK_TYPE_LEVEL:
    {
        if (!Mod_UnloadLevelPaks(false)) // Unload extra level pak files.
            return false;

        // If this was initiated, cancel it because they will be re-initiated
        // when we load our level paks again. Else we will end up loading them
        // up twice again.
        Mod_CancelUserLevelModPaksReprocess();
        break;
    }
    default:
        break;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: unloads all paks until and including the given pak type
// Input  : pakType - maps to CommonPakData_s::PakType_e
// Output : true if all paks have been successfully loaded, false otherwise
//-----------------------------------------------------------------------------
static bool Mod_UnloadPaksUntilType(const int pakType)
{
    for (int i = (CommonPakData_s::PAK_TYPE_COUNT-1); i >= pakType; --i)
    {
        CommonPakData_s& cpd = g_commonPakData[i];

        if (!cpd.pakName[0])
            continue; // Already unloaded.

        PakLoadedInfo_s* const pakInfo = Pak_GetPakInfo(cpd.pakId);
        PakStatus_e status = PAK_STATUS_INVALID_PAKHANDLE;

        if (pakInfo->handle == cpd.pakId)
            status = pakInfo->status;

        if (!cpd.isUnloading || status == PAK_STATUS_LOADED)
        {
            cpd.isUnloading = true;

            if (cpd.isCustomPakLoaded)
            {
                // Make sure the custom pak is unloaded first.
                if (!Mod_HandleCustomPakUnloadForType(i))
                    return false;

                cpd.isCustomPakLoaded = false;
            }

            g_pakLoadApi->UnloadAsync(cpd.pakId);
        }

        if (status != PAK_STATUS_FREED &&
            status != PAK_STATUS_ERROR &&
            status != PAK_STATUS_INVALID_PAKHANDLE)
        {
            // Unload is still pending.
            return false;
        }

        cpd.isUnloading = false;
        cpd.pakName[0] = '\0';

        cpd.pakId = PAK_INVALID_HANDLE;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: loads and unloads pending paks with fifo lock
//-----------------------------------------------------------------------------
static void Mod_LoadAndUnloadPaksWithLock()
{
    if (!*g_pPakPrecacheJobFinished)
        return; // Not finished yet.

    const CPackedStore* const vpk = *g_currentLevelVPK;

    if (!vpk) // VPK not loaded yet.
        return;

    if (vpk->GetStatus() != 0)
        return; // VPK not ready yet.

    if (g_pakGlobals->hasPendingUnloadJobs || g_pakGlobals->loadedPakCount != g_pakGlobals->requestedPakCount)
    {
        JobFifoLock_s* const pakFifoLock = &g_pakGlobals->fifoLock;

        if (!JT_AcquireFifoLock(pakFifoLock)
            && !JT_HelpWithJobTypes(g_pPakFifoLockWrapper, pakFifoLock, -1, 0))
        {
            // Help with other jobs until we can acquire the lock.
            JT_HelpWithJobTypesOrSleep(g_pPakFifoLockWrapper, pakFifoLock, -1, 0, 0, 1);
        }

        v_Mod_UnloadPendingAndPrecacheRequestedPaks();

        if (ThreadInMainThread() && (*g_bPakFifoLockAcquiredInMainThread))
        {
            *g_bPakFifoLockAcquiredInMainThread = false;
            JT_ReleaseFifoLock(pakFifoLock);
        }

        JT_ReleaseFifoLock(pakFifoLock);
    }

    FileSystem()->ResetItemCacheSize(256);
    FileSystem()->PrecacheTaskItem((void*)vpk);
}

//-----------------------------------------------------------------------------
// Purpose: handle load of custom paks based on current common pak
// Input  : type - maps to CommonPakData_s::PakType_e
//-----------------------------------------------------------------------------
static void Mod_HandleCustomPakLoadForType(const int type)
{
    switch (type)
    {
#ifndef DEDICATED
    case CommonPakData_s::PakType_e::PAK_TYPE_UI_GM:
    {
        s_customPakData.LoadBasePak("ui_sdk.rpak", CustomPakData_s::PakType_e::PAK_TYPE_UI_SDK);
        break;
    }
#endif // !DEDICATED
    case CommonPakData_s::PakType_e::PAK_TYPE_COMMON_GM:
    {
        s_customPakData.LoadBasePak("common_sdk.rpak", CustomPakData_s::PakType_e::PAK_TYPE_COMMON_SDK);
        break;
    }
    case CommonPakData_s::PakType_e::PAK_TYPE_LOBBY:
    {
        Mod_PreloadAllPaks();
        break;
    }
    case CommonPakData_s::PakType_e::PAK_TYPE_LEVEL:
    {
        Mod_LoadAllLevelPaks(false);
        // If this was initiated, cancel it because they have already been
        // reprocessed at this point.
        Mod_CancelUserLevelModPaksReprocess();
        break;
    }
    default:
        break;
    }
}

//-----------------------------------------------------------------------------
// Purpose: loads the main pak and forces the global state to unfinished and
//          returns false if its still in progress. If the main pak finished
//          loading, the custom pak linked to this pak will start loading and
//          code will force the global state to unfinished and return false
//          for this pak as well if its still in progress.
// Input  : &cpd - 
//          index - 
// Output : true if all load jobs have finished, false otherwise
//-----------------------------------------------------------------------------
static bool Mod_HandlePakLoadJobStateUpdate(CommonPakData_s& cpd, const int index)
{
    if (!Mod_IsPakLoadFinished(cpd.pakId))
    {
        *g_pPakPrecacheJobFinished = false;
        return false;
    }

    if (!cpd.isCustomPakLoaded && cpd.pakId != PAK_INVALID_HANDLE)
    {
        Mod_HandleCustomPakLoadForType(index);
        cpd.isCustomPakLoaded = true;
    }

    if (!Mod_IsCustomPakLoadFinished(index))
    {
        *g_pPakPrecacheJobFinished = false;
        return false;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: handles the unload of user level mod paks if the reprocess flag is
//          set, this will be set if we change the playlist
//-----------------------------------------------------------------------------
static bool Mod_HandleUserLevelModPaksUnload()
{
    if (s_customPakData.reprocessUserLevelPaks && !s_customPakData.reprocessUserLevelPaksUnloadFinished)
    {
        // note(kawe): unlike engine and sdk paks (which follow a fixed loading
        // and unloading order) we couldn't reliably unload disabled mod paks
        // and keep enabled mod paks loaded, because if we happen to unload a
        // pak that another one that we keep relies on, we will have dangling
        // references which will yield undefined behavior (typically crashes).
        // We have to bite the bullet and reload all level mod paks to avoid
        // this behavior which instead will show the mod author an actual asset
        // dependency error may they happen to load their linked mod paks in
        // the wrong order, or unload a pak while loading a pak that relies on
        // the now unloaded pak.
        if (!Mod_UnloadLevelPaks(true))
            return false;

        s_customPakData.reprocessUserLevelPaksUnloadFinished = true;
    }

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: handles the load of user level mod paks if the reprocess flag is
//          set, this will be set if we change the playlist
//-----------------------------------------------------------------------------
static void Mod_HandleUserLevelModPaksLoad()
{
    if (!s_customPakData.reprocessUserLevelPaks)
        return; // Nothing to reprocess.

    CommonPakData_s& cpd = g_commonPakData[CommonPakData_s::PAK_TYPE_LEVEL];

    // If we launch a level and the vpk couldn't be loaded, we will be hitting
    // this code. Make sure our level paks are loaded, else defer loading our
    // custom paks.
    if (cpd.pakId == PAK_INVALID_HANDLE)
        return;

    if (!s_customPakData.reprocessUserLevelPaksLoadCalled)
    {
        // Only mods, we only reload level core paks if we actually change
        // levels, in that case this code shouldn't be fired.
        Mod_LoadAllLevelPaks(true);
        s_customPakData.reprocessUserLevelPaksLoadCalled = true;
    }

    if (!Mod_IsCustomPakLoadFinished(CommonPakData_s::PAK_TYPE_LEVEL))
        *g_pPakPrecacheJobFinished = false;

    if (*g_pPakPrecacheJobFinished)
        s_customPakData.reprocessUserLevelPaks = false;
}

// Streaming data is client only, remounting does not apply for the dedicated
// server.
#ifndef DEDICATED
static bool s_emulatePakRemount = false;

static void Pak_EmulateRemount_f()
{
    s_emulatePakRemount = true;
}

static ConCommand pak_emulateremount("pak_emulateremount", Pak_EmulateRemount_f,
    "Remount all paks once we are disconnected and no longer active, as if we had assets with discarded streaming data loaded.", FCVAR_DEVELOPMENTONLY);

//-----------------------------------------------------------------------------
// Purpose: returns whether we should remount all paks, for example, when we
//          have assets loaded with discarded streaming data.
//-----------------------------------------------------------------------------
static bool Mod_ShouldRemountPaks()
{
    return s_emulatePakRemount || // Emulation override for debugging.
        (Pak_StreamingDownloadFinished() && Pak_HasNonFullyInstalledAssetsLoaded());
}
#endif // !DEDICATED

//-----------------------------------------------------------------------------
// Purpose: pak loading and unloading state machine
//-----------------------------------------------------------------------------
static void Mod_RunPakJobFrame()
{
    bool unloadAll = false;

#ifndef DEDICATED
    // Reload all paks if all optional streaming files are finished downloading
    // and we are no longer connected to a server. They need to be reloaded as
    // the stream file handles are only opened during the load of a given rpak.
    // For host, we also need to check if the game is active and only continue
    // if it isn't because the assets are otherwise still in use by the server.
    if (Mod_ShouldRemountPaks() && !g_pHostState->IsActiveGame() && !g_pClientState->IsConnected())
    {
        *g_pPakPrecacheJobFinished = false;
        unloadAll = true;
    }
    else 
#endif // !DEDICATED
    if (*g_pPakPrecacheJobFinished)
        return;

    if (!FileSystem()->ResetItemCache() || *g_pNumPrecacheItemsMTVTF)
        return;

    // Check if we have a pak with a different name now, if so,
    // drop any pak from the tail until and including this pak
    // and load them. Paks are always loaded in order; if we
    // drop pak of type 2, then type 3 needs to unload as well
    // because type 3 can have assets that rely on type 2.
    const int endIndex = unloadAll ? 0 : Mod_GetTargetPakToUnloadType();

    if (!Mod_UnloadPaksUntilType(endIndex))
        return; // Not finished unloading yet.

#ifndef DEDICATED
    s_emulatePakRemount = false;
#endif // !DEDICATED

    // If we change to the lobby, we should retain all the currently loaded
    // paks, including mod paks as its likely we will be going back to the
    // same level and same playlist again. If we do actually change to a
    // different level than the previous one from the lobby, the paks will
    // all be unloaded in the above `Mod_UnloadPaksUntilType` call and all
    // the level mod paks will be re-evaluated anyways.
    if (!s_customPakData.inLobby)
    {
        if (!Mod_HandleUserLevelModPaksUnload())
            return;
    }

    *g_pPakPrecacheJobFinished = true;

    for (int i = 0; i < CommonPakData_s::PAK_TYPE_COUNT; i++)
    {
        CommonPakData_s& cpd = g_commonPakData[i];

        if (V_strcmp(cpd.pakName, cpd.basePakName) == 0)
        {
            if (!Mod_HandlePakLoadJobStateUpdate(cpd, i))
                return; // Current pak hasn't finished loading yet.

            continue; // Pak file didn't change, no mutation needed.
        }

        // Copy the new name over and load the pak.
        V_strncpy(cpd.pakName, cpd.basePakName, MAX_OSPATH);
        cpd.pakId = g_pakLoadApi->LoadAsync(cpd.pakName, AlignedMemAlloc(), 4, 0);

        if (!Mod_HandlePakLoadJobStateUpdate(cpd, i))
            return; // Current pak hasn't finished loading yet.
    }

    // See comment on the `Mod_HandleUserLevelModPaksUnload` guard above.
    if (!s_customPakData.inLobby)
        Mod_HandleUserLevelModPaksLoad();

    Mod_LoadAndUnloadPaksWithLock();
}

//-----------------------------------------------------------------------------
// Purpose: initiates asset precache for the given level
// Input  : *fullLevelFileName - is vpk/<target>_<levelName>.bsp, so it can be:
//                               vpk/server_mp_lobby.bsp
//          *levelName         - is mp_lobby, mp_rr_box, or whatever map we are
//                               precaching. However, if the to-precache VPK 
//                               doesn't have a map by design, such as the VPK
//                               vpk/client_mp_common.bsp for example, then the
//                               levelName parameter will be nullptr!
//          allowVpkLoadFail   - whether to error or not when the VPK for the
//                               given level failed to load. NOTE that we will
//                               always error when a VPK without a BSP level,
//                               such as vpk/client_frontend.bsp, fails to load
//-----------------------------------------------------------------------------
void Mod_PrecacheLevelAssets(const char* const fullLevelFileName, const char* const levelName, const bool allowVpkLoadFail)
{
    if (levelName)
        Mod_HandleLevelChanged(levelName);

    v_Mod_PrecacheLevelAssets(fullLevelFileName, levelName, allowVpkLoadFail);
}

//-----------------------------------------------------------------------------
// Purpose: loads the load screen pak for the given level
// Input  : *levelName
//-----------------------------------------------------------------------------
void Mod_LoadLoadscreenPakForLevel(const char* const levelName)
{
    // On the dedicated server, we should do nothing here since load screens
    // are not used and shipped on dedicated servers for good reasons! These
    // are client-only.
#ifndef DEDICATED
    v_Mod_LoadLoadscreenPakForLevel(levelName);
#endif // !DEDICATED
}

///////////////////////////////////////////////////////////////////////////////
void VModel_BSP::Detour(const bool bAttach) const
{
	DetourSetup(&v_Mod_RunPakJobFrame, &Mod_RunPakJobFrame, bAttach);
	DetourSetup(&v_Mod_PrecacheLevelAssets, &Mod_PrecacheLevelAssets, bAttach);
	DetourSetup(&v_Mod_LoadLoadscreenPakForLevel, &Mod_LoadLoadscreenPakForLevel, bAttach);
}
