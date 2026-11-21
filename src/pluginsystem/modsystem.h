#pragma once

#include "tier1/utlhash.h"
#include "tier1/keyvalues.h"
#include "rtech/rson.h"
#include "filesystem/filesystem.h"
#include "vscript/ivscript.h"

#define MOD_BASE_DIRECTORY "mods"
#define MOD_STATUS_LIST_FILE MOD_BASE_DIRECTORY"/mods.vdf"
#define MOD_SETTINGS_FILE "mod.vdf"
#define MAX_MODS_TO_LOAD 1024

class CModAppSystemGroup;

class CModSystem
{
public:
	enum eModState : int8_t
	{
		UNLOADED = -1, // loading was unsuccessful (error occurred)
		LOADING,       // if mod is being loaded
		LOADED,        // if a mod has been loaded
		DISABLED,      // if disabled by user
		ENABLED,       // if enabled by user and loaded properly
	};

	struct ModInstance_t
	{
		ModInstance_t(CModSystem* const _parentClass, const CUtlString& basePath);
		~ModInstance_t();

		bool ParseSettings();
		void ParseConVars();
		void ParseLocalizationFiles();

		inline void SetState(const eModState newState) { state = newState; };

		inline bool IsLoaded() const { return state == eModState::LOADED; };
		inline bool IsEnabled() const { return state == eModState::ENABLED; };

		bool ShouldLoadPaks(const char* const targetPlaylist) const;

		inline const CUtlString& GetBasePath() const { return basePath; };
		inline CUtlString GetScriptCompileListPath() const { return basePath + GAME_SCRIPT_COMPILELIST; };

		KeyValues* GetSettingsKeyRequired(const char* settingsPath, const char* key) const;

		inline RSON::Node_t* LoadScriptCompileList(bool* const parseFailure) const
		{
			return RSON::LoadFromFile(GetScriptCompileListPath().Get(), "GAME", parseFailure);
		};

		CModSystem* parentClass;
		KeyValues* settingsKV;

		UtlHashHandle_t idHashHandle;

		eModState state = eModState::UNLOADED;
		bool hasSearchPath;
		bool hasPrecompiledScripts;

		CUtlVector<CUtlString> localizationFiles;
		CUtlVector<ConVar*> conVars;

		CUtlString author;
		CUtlString name;
		CUtlString id;
		CUtlString description;
		CUtlString version;

		CUtlString basePath;
	};

	CModSystem();
	~CModSystem();

	void Init();
	void Shutdown();

	// load mod enabled/disabled status from file on disk
	void UpdateModStatusList();
	void LoadModStatusList(CUtlMap<CUtlString, bool>& enabledList);
	void WriteModStatusList();

	bool IsEnabled() const;

	const inline CUtlVector<ModInstance_t*>& GetModList() { return m_ModList; };
	const void LockModList() { m_ModListMutex.Lock(); }
	const void UnlockModList() { m_ModListMutex.Unlock(); }

	const CUtlString& GetNormalizedModID(const ModInstance_t* const mod) const;

private:
	static unsigned int ModSystem_HashModId( const CUtlString &s )
	{
		return HashStringCaseless( s.String() );
	}

	CUtlVector<ModInstance_t*> m_ModList;
	CUtlHash<CUtlString, &UtlStringCompareFunc, &CModSystem::ModSystem_HashModId> m_ModIdHashMap;
	CThreadMutex m_ModListMutex;
};

extern CModSystem g_ModSystem;

FORCEINLINE CModSystem* ModSystem()
{
	return &g_ModSystem;
}
