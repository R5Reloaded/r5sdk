//=============================================================================//
//
// Purpose: Manage loading mods
//
//-----------------------------------------------------------------------------
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier0/commandline.h"
#include "tier1/cvar.h"
#include "tier2/fileutils.h"
#include "engine/host.h"
#include "engine/cmodel_bsp.h"
#include "rtech/rson.h"
#include "localize/localize.h"
#include "modsystem.h"

// NOTE: code using this should be development only; if the convars are used by
// VGUI (i.e. attached to a slider or button), and we free them, but no longer
// add them back in, the VGUI system will crash. This is therefore considered
// an advanced function -- Possible fix: shutdown VGUI, reload mods, init VGUI?
// Currently we shutdown the mod system, init it again and then shutdown VGUI
// and all other systems.
static void ModSystem_Reload_f()
{
	ModSystem()->Shutdown();
	ModSystem()->Init();

	Mod_InitiateUserLevelModPaksReprocess(); // Make sure our level mod rpaks reload.
	Host_ReparseAllScripts(); // Reparse every script of the game.
}

static void ModSystem_EnableChanged_f(IConVar* var, const char* pOldValue, float flOldValue, ChangeUserData_t pUserData)
{
	NOTE_UNUSED(var);
	NOTE_UNUSED(pOldValue);
	NOTE_UNUSED(flOldValue);
	NOTE_UNUSED(pUserData);
	ModSystem_Reload_f();
}

//-----------------------------------------------------------------------------
// Console variables
//-----------------------------------------------------------------------------
static ConVar modsystem_enable("modsystem_enable", "1", FCVAR_DEVELOPMENTONLY, "Enable the modsystem", ModSystem_EnableChanged_f);
static ConVar modsystem_debug("modsystem_debug", "0", FCVAR_RELEASE, "Debug the modsystem");

//-----------------------------------------------------------------------------
// Purpose: returns the mod state as string
//-----------------------------------------------------------------------------
static const char* ModSystem_StateToString(const CModSystem::eModState state)
{
	switch (state)
	{
	case CModSystem::eModState::UNLOADED: return "unloaded";
	case CModSystem::eModState::LOADING: return "loading";
	case CModSystem::eModState::LOADED: return "loaded";
	case CModSystem::eModState::DISABLED: return "disabled";
	case CModSystem::eModState::ENABLED: return "enabled";
	default: Assert(0); return "unknown";
	}
}

static void ModSystem_List_f()
{
	if (!ModSystem()->IsEnabled())
	{
		Msg(eDLL_T::ENGINE, "*** modsystem is disabled ***\n");
		return;
	}

	Msg(eDLL_T::ENGINE, "*** loaded mods ***\n");
	Msg(eDLL_T::ENGINE, "-------------------------------------------------------------------------------\n");
	ModSystem()->LockModList();

	FOR_EACH_VEC(ModSystem()->GetModList(), i)
	{
		const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

		Msg(eDLL_T::ENGINE, "author: %s\n", mod->author.String());
		Msg(eDLL_T::ENGINE, "name: %s\n", mod->name.String());
		Msg(eDLL_T::ENGINE, "id: %s\n", mod->id.String());
		Msg(eDLL_T::ENGINE, "description: %s\n", mod->description.String());
		Msg(eDLL_T::ENGINE, "version: %s\n", mod->version.String());
		Msg(eDLL_T::ENGINE, "path: %s\n", mod->basePath.String());
		Msg(eDLL_T::ENGINE, "state: %s\n", ModSystem_StateToString(mod->state));
		Msg(eDLL_T::ENGINE, "-------------------------------------------------------------------------------\n");
	}

	ModSystem()->UnlockModList();
}

//-----------------------------------------------------------------------------
// Console commands
//-----------------------------------------------------------------------------
static ConCommand modsystem_reload("modsystem_reload", ModSystem_Reload_f, "Reload the modsystem", FCVAR_DEVELOPMENTONLY);
static ConCommand modsystem_list("modsystem_list", ModSystem_List_f, "Show list of mods", FCVAR_RELEASE);

static unsigned int ModSystem_HashModId(const CUtlString& s)
{
	return HashStringCaseless(s.String());
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CModSystem::CModSystem()
	: m_ModIdHashMap(MAX_MODS_TO_LOAD, 0, 0, UtlStringCompareFunc, ModSystem_HashModId)
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CModSystem::~CModSystem()
{
	Shutdown();
}

//-----------------------------------------------------------------------------
// Purpose: initialize the mod system
// Input  :
//-----------------------------------------------------------------------------
void CModSystem::Init()
{
	if (CommandLine()->CheckParm("-modsystem_disable"))
		return;

	if (!modsystem_enable.GetBool())
		return;

	// no mods installed, no point in initializing.
	if (!FileSystem()->IsDirectory(MOD_BASE_DIRECTORY, "GAME"))
		return;

	// mod system initializes before the first Cbuf_Execute call, which
	// executes commands/convars over the command line. we check for an
	// explicit modsystem debug flag, and set the convar from here.
	if (CommandLine()->CheckParm("-modsystem_debug"))
		modsystem_debug.SetValue(true);

	CUtlVector<CUtlString> modFileList;
	RecursiveFindFilesMatchingName(modFileList,
		MOD_BASE_DIRECTORY, MOD_SETTINGS_FILE, "GAME", '/');

	LockModList();

	FOR_EACH_VEC(modFileList, i)
	{
		if (i == MAX_MODS_TO_LOAD)
		{
			Error(eDLL_T::ENGINE, NO_ERROR, "Exceeded MAX_MODS_TO_LOAD; only %d out of %d mods are loaded.\n",
				MAX_MODS_TO_LOAD, modFileList.Count());
			break;
		}

		// allocate dynamically, so less memory/resources are required when
		// the vector has to grow and reallocate everything. we also got
		// a vector member in the modinstance struct, which would ultimately
		// lead into each item getting copy constructed into the mod list
		// by the 'move' constructor (CUtlVector also doesn't support being
		// nested unless its a pointer).
		CModSystem::ModInstance_t* mod = 
			new CModSystem::ModInstance_t(this, modFileList.Element(i).DirName());

		if (!mod->IsLoaded())
		{
			delete mod;
			continue;
		}

		// The mod ID gets normalized here ('SDK.BaseMod' gets turned into
		// 'SDK_BaseMod'. This ensures that we can generate unique script code
		// callbacks and anything else that might be of interest in the future.
		bool didInsert; // Mod ID's must be unique!
		const UtlHashHandle_t idHandle = m_ModIdHashMap.Insert(mod->id.Replace('.', '_'), &didInsert);

		if (!didInsert)
		{
			Error(eDLL_T::ENGINE, NO_ERROR,
				"Mod \"%s\" has ID \"%s\" that was already used by another mod; skipping...\n",
				mod->name.String(), mod->id.String());

			delete mod;
			continue;
		}

		mod->idHashHandle = idHandle;
		m_ModList.AddToTail(mod);
	}

	UpdateModStatusList();
	UnlockModList(); // Unlock after to make sure nothing uses it during init.
}

//-----------------------------------------------------------------------------
// Purpose: shutdown the mod system
// Input  :
//-----------------------------------------------------------------------------
void CModSystem::Shutdown()
{
	AUTO_LOCK(m_ModListMutex);
	m_ModList.PurgeAndDeleteElements(); // clear all allocated mod instances.
	m_ModIdHashMap.Purge();
}

//-----------------------------------------------------------------------------
// Purpose: returns whether the mod system is enabled
//-----------------------------------------------------------------------------
bool CModSystem::IsEnabled() const
{
	return modsystem_enable.GetBool();
}

//-----------------------------------------------------------------------------
// Purpose: loads, updates, enforces and writes the mod status list
//-----------------------------------------------------------------------------
void CModSystem::UpdateModStatusList()
{
	CUtlMap<CUtlString, bool> enabledList(UtlStringLessFunc);
	LoadModStatusList(enabledList);
	
	LockModList();

	// from here, we determine whether or not to enable the loaded mod.
	FOR_EACH_VEC(m_ModList, i)
	{
		ModInstance_t* mod = m_ModList[i];
		Assert(mod->IsLoaded());

		if (!enabledList.HasElement(mod->id))
		{
			if (modsystem_debug.GetBool())
			{
				Msg(eDLL_T::ENGINE, "Mod '%s'(\"%s\") does not exist in '%s'; enabling...\n",
					mod->name.String(), mod->id.String(), MOD_STATUS_LIST_FILE);
			}

			mod->SetState(eModState::ENABLED);
		}
		else
		{
			const bool bEnable = enabledList.FindElement(mod->id, false);
			mod->SetState(bEnable ? eModState::ENABLED : eModState::DISABLED);

			if (modsystem_debug.GetBool())
			{
				Msg(eDLL_T::ENGINE, "Mod '%s'(\"%s\") exists in '%s' and is %s.\n",
					mod->name.String(), mod->id.String(), MOD_STATUS_LIST_FILE, ModSystem_StateToString(mod->state));
			}
		}
	}

	WriteModStatusList();
	UnlockModList(); // Unlock after to make sure nothing uses it during init.
}

//-----------------------------------------------------------------------------
// Purpose: loads the mod status file from the disk
// Input  : &enabledList - 
//-----------------------------------------------------------------------------
void CModSystem::LoadModStatusList(CUtlMap<CUtlString, bool>& enabledList)
{
    KeyValues* const pModList = FileSystem()->LoadKeyValues(
        IFileSystem::TYPE_COMMON, MOD_STATUS_LIST_FILE, "GAME");

    if (!pModList)
        return; // Does not exist, or failed to parse (check console).

    for (KeyValues* pSubKey = pModList->GetFirstSubKey();
        pSubKey != nullptr; pSubKey = pSubKey->GetNextKey())
    {
        enabledList.Insert(pSubKey->GetName(), pSubKey->GetBool());
    }

    pModList->DeleteThis();
}

//-----------------------------------------------------------------------------
// Purpose: writes the mod status file to the disk
//-----------------------------------------------------------------------------
void CModSystem::WriteModStatusList()
{
	KeyValues kv("ModList");
	LockModList();

	FOR_EACH_VEC(m_ModList, i)
	{
		const ModInstance_t* mod = m_ModList[i];
		bool enabled = false;

		if (mod->state == eModState::ENABLED)
			enabled = true;

		kv.SetBool(mod->id.String(), enabled);
	}

	UnlockModList();

	CUtlBuffer buf = CUtlBuffer(ssize_t(0), 0, CUtlBuffer::TEXT_BUFFER);
	kv.RecursiveSaveToFile(buf, 0);

	if (!FileSystem()->WriteFile(MOD_STATUS_LIST_FILE, "GAME", buf))
		Error(eDLL_T::ENGINE, NO_ERROR, "Failed to write mod status list '%s'.\n", MOD_STATUS_LIST_FILE);
}

//-----------------------------------------------------------------------------
// Purpose: returns the normalized mod ID for the given mod instance
// Input  : *mod - 
// Output : normalized mod ID (i.e. SDK.BaseMod gets returned as SDK_BaseMod)
//-----------------------------------------------------------------------------
const CUtlString& CModSystem::GetNormalizedModID(const ModInstance_t* const mod) const
{
	return m_ModIdHashMap[mod->idHashHandle];
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *_parentClass - 
//          &_basePath    - 
//-----------------------------------------------------------------------------
CModSystem::ModInstance_t::ModInstance_t(CModSystem* const _parentClass, const CUtlString& _basePath)
{
	parentClass = _parentClass;
	settingsKV = nullptr;

	hasSearchPath = false;
	hasPrecompiledScripts = false;

	basePath = _basePath;
	basePath.AppendSlash('/');

	SetState(eModState::LOADING);

	if (!ParseSettings())
	{
		SetState(eModState::UNLOADED);
		return;
	}

	// parse any additional info from mod.vdf
	ParseConVars();
	ParseLocalizationFiles();

	// add mod folder to search paths so files can be easily loaded from here
	// [rexx]: maybe this isn't ideal as the only way of finding the mod's files,
	//         as there may be name clashes in files where the engine
	//         won't really care about the input file name. it may be better to,
	//         where possible, request files by file path relative to root
	//         (i.e. including platform/mods/{mod}/)
	// [amos]: it might be better to pack core files into the VPK, and disable
	//         the filesystem cache to disk reroute to avoid the file name
	//         clashing problems, research required.
	FileSystem()->AddSearchPath(basePath.String(), "GAME", SearchPathAdd_t::PATH_ADD_TO_TAIL);
	hasSearchPath = true;

	const CUtlString scriptsRsonPath = basePath + GAME_SCRIPT_COMPILELIST;
	SetState(eModState::LOADED);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CModSystem::ModInstance_t::~ModInstance_t()
{
	if (settingsKV)
		delete settingsKV;

	if (hasSearchPath)
		FileSystem()->RemoveSearchPath(basePath.String(), "GAME");

	FOR_EACH_VEC(conVars, i)
	{
		ConVar* const cvar = conVars.Element(i);
		cvar->Shutdown(); // Removes it from the linked list.

		delete cvar;
	}
}

//-----------------------------------------------------------------------------
// Purpose: returns whether pak loading should happen for current playlist
// Input  : *targetPlaylist - 
// Output : true if we should load, false otherwise
//-----------------------------------------------------------------------------
bool CModSystem::ModInstance_t::ShouldLoadPaks(const char* const targetPlaylist) const
{
	Assert(settingsKV);
	const KeyValues* const pPlaylistsKV = settingsKV->FindKey("PakLoadOnPlaylists");

	// If the pak load filter is empty or absent, return true as that means no
	// filter should be applied here.
	if (!pPlaylistsKV || !pPlaylistsKV->GetFirstSubKey())
		return true;

	for (KeyValues* pSubKey = pPlaylistsKV->GetFirstSubKey();
		pSubKey != nullptr; pSubKey = pSubKey->GetNextKey())
	{
		if (!pSubKey->GetBool())
			continue; // Pak load is disabled on this mode.

		if (V_strcmp(pSubKey->GetName(), targetPlaylist) == 0)
			return true;
	}

	// Current playlist not found in filter, paks shouldn't be loaded.
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: gets a keyvalue from settings KV, and logs an error on failure
// Input  : *settingsPath - 
//          *key          - 
// Output : pointer to KeyValues object
//-----------------------------------------------------------------------------
KeyValues* CModSystem::ModInstance_t::GetSettingsKeyRequired(
	const char* settingsPath, const char* key) const
{
	KeyValues* const pKeyValue = settingsKV->FindKey(key);

	if (!pKeyValue)
	{
		Error(eDLL_T::ENGINE, NO_ERROR,
			"Mod settings \"%s\" is missing key \"%s\" which is required; skipping...\n",
			settingsPath, key);

		return nullptr;
	}

	return pKeyValue;
}

//-----------------------------------------------------------------------------
// Purpose: gets a string value from settings KV, and logs an error on failure
// Input  : *mod          - 
//          *settingsPath - 
//          *key          - 
//          &out          - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
static bool ModSystem_GetSettingsKeyValueString(CModSystem::ModInstance_t* const mod, const char* settingsPath, const char* const key, CUtlString& out)
{
	KeyValues* const pKeyValue = mod->GetSettingsKeyRequired(settingsPath, key);

	if (!pKeyValue)
		return false;

	const char* const value = pKeyValue->GetString();

	if (!value[0])
	{
		Error(eDLL_T::ENGINE, NO_ERROR,
			"Mod settings \"%s\" contains key \"%s\" with no value; skipping...\n",
			settingsPath, key);

		return false;
	}

	out = value;
	return true;
}

#define MIN_MOD_ID_LENGTH 4

//-----------------------------------------------------------------------------
// Purpose: make sure mod ID's are valid (version numbers for example shouldn't
//          be used inside mod ID's since we have a dedicated "version" field
//          for this. Mod ID's are also used to generate the script entry point
//          callback names, and in the future they can be used for even more
//          automation to significantly reduce the surface area for user error.
//-----------------------------------------------------------------------------
static bool ModSystem_ValidateModID(const CUtlString& modId, const char* const settingsPath)
{
	const ssize_t modIdLength = modId.Length();
	const char* const pModId = modId.String();

	if (modIdLength < MIN_MOD_ID_LENGTH)
	{
		Error(eDLL_T::ENGINE, NO_ERROR, "Mod settings \"%s\" has ID \"%s\" with length %zd, however the minimum length is %zd; skipping...\n",
			settingsPath, pModId, modIdLength, (ssize_t)MIN_MOD_ID_LENGTH);

		return false;
	}

	const size_t pos = strspn(pModId, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz._");
	const bool hasInvalidChars = pModId[pos] != '\0';

	if (hasInvalidChars)
	{
		Error(eDLL_T::ENGINE, NO_ERROR, "Mod settings \"%s\" has ID \"%s\" containing disallowed characters. ID's can only contain letters, periods (.) and underscores (_); skipping...\n",
			settingsPath, pModId);

		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: loads the settings KV and parses the main values
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CModSystem::ModInstance_t::ParseSettings()
{
	const CUtlString settingsPath = basePath + MOD_SETTINGS_FILE;
	const char* const pSettingsPath = settingsPath.String();

	settingsKV = FileSystem()->LoadKeyValues(
		IFileSystem::TYPE_COMMON, pSettingsPath, "GAME");

	if (!settingsKV)
	{
		Error(eDLL_T::ENGINE, NO_ERROR,
			"Failed to parse mod settings \"%s\"; skipping...\n", pSettingsPath);
		return false;
	}

	// "author" "MyName"
	if (!ModSystem_GetSettingsKeyValueString(this, pSettingsPath, "author", author))
		return false;

	// "name" "An R5Reloaded Mod"
	if (!ModSystem_GetSettingsKeyValueString(this, pSettingsPath, "name", name))
		return false;

	// "id" "r5reloaded.TestMod"
	if (!ModSystem_GetSettingsKeyValueString(this, pSettingsPath, "id", id))
		return false;

	if (!ModSystem_ValidateModID(id, pSettingsPath))
		return false;

	// "description" "This mod does X and Y using Z"
	if (!ModSystem_GetSettingsKeyValueString(this, pSettingsPath, "description", description))
		return false;

	// "version" "1.0.0"
	if (!ModSystem_GetSettingsKeyValueString(this, pSettingsPath, "version", version))
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: parses and registers convars listed in settings KV
//-----------------------------------------------------------------------------
void CModSystem::ModInstance_t::ParseConVars()
{
	Assert(settingsKV);
	const KeyValues* pConVars = settingsKV->FindKey("ConVars");

	if (!pConVars)
		return;

	for (KeyValues* pSubKey = pConVars->GetFirstSubKey();
		pSubKey != nullptr; pSubKey = pSubKey->GetNextKey())
	{
		const char* pszName = pSubKey->GetName();
		const char* pszFlagsString = pSubKey->GetString("flags", "NONE");
		const char* pszHelpString = pSubKey->GetString("helpText");
		const char* pszUsageString = pSubKey->GetString("usageText");

		KeyValues* pValues = pSubKey->FindKey("Values");

		const char* pszDefaultValue = "0";
		bool bMin = false;
		bool bMax = false;
		float fMin = 0.f;
		float fMax = 0.f;

		if (pValues)
		{
			pszDefaultValue = pValues->GetString("default", "0");

			// minimum cvar value
			if (pValues->FindKey("min"))
			{
				bMin = true; // has min value
				fMin = pValues->GetFloat("min", 0.f);
			}

			// maximum cvar value
			if (pValues->FindKey("max"))
			{
				bMax = true; // has max value
				fMax = pValues->GetFloat("max", 1.f);
			}
		}

		int flags;
		if (ConVar_ParseFlagString(pszFlagsString, flags, pszName))
		{
			if (g_pCVar->FindCommandBase(pszName) != nullptr)
			{
				Warning(eDLL_T::ENGINE, "Failed to register ConVar \"%s\" for mod '%s'(\"%s\"); already registered.\n",
					pszName, name.String(), id.String());

				continue;
			}

			ConVar* cvar = new ConVar(pszName, pszDefaultValue, flags, pszHelpString, bMin, fMin, bMax, fMax, nullptr, pszUsageString);

			if (!cvar)
			{
				// Quit as we ran out of memory.
				Error(eDLL_T::ENGINE, EXIT_FAILURE, "Failed to register ConVar \"%s\" for mod '%s'(\"%s\"); allocation failure.\n",
					pszName, name.String(), id.String());

				return;
			}

			conVars.AddToTail(cvar);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: parses and stores localization file paths in a vector
//-----------------------------------------------------------------------------
void CModSystem::ModInstance_t::ParseLocalizationFiles()
{
	Assert(settingsKV);
	const KeyValues* pLocalizationFiles = settingsKV->FindKey("LocalizationFiles");

	if (!pLocalizationFiles)
		return;

	for (KeyValues* pSubKey = pLocalizationFiles->GetFirstSubKey();
		pSubKey != nullptr; pSubKey = pSubKey->GetNextKey())
	{
		localizationFiles.AddToTail(basePath + pSubKey->GetName());
	}
}

CModSystem g_ModSystem;
