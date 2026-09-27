#pragma once
#include "vpc/interfaces.h"
#include "common/engine_launcher_api.h"

class CEngineAPI : public CTier1AppSystem<IEngineAPI>
{
public:
	// This function must be called before init
	virtual bool SetStartupInfo(StartupInfo_t& info) = 0;

	virtual int Run() = 0;

	// Posts a console command
	virtual void PostConsoleCommand(const char* pConsoleCommand) = 0;

	// Are we running the simulation?
	virtual bool IsRunningSimulation() const = 0;

	// Start/stop running the simulation
	virtual void ActivateSimulation(bool bActive) = 0;

	// Reset the map we're on
	virtual void SetMap(const char* pMapName) = 0;

	static InitReturnVal_t VInit(CEngineAPI* thisp);
	static bool VModInit(CEngineAPI* pEngineAPI, const char* pModName, const char* pGameDir);
	static bool OnStartup(CEngineAPI* pEngineAPI, void* pInstance, const char* pStartupModName);
	static void VSetStartupInfo(CEngineAPI* pEngineAPI, StartupInfo_t* pStartupInfo);

	static void PumpMessages();
	static void RunLowLatencyFrame();
	static void UpdateLowLatencyParameters();

	static bool MainLoop();
private:
	void* m_hEditorHWnd;
	bool m_bRunningSimulation;
	StartupInfo_t m_StartupInfo;
};

inline InitReturnVal_t(*CEngineAPI__Init)(CEngineAPI* thisp);
inline void(*CEngineAPI__Shutdown)(void);
inline bool(*CEngineAPI__Connect)(CEngineAPI* thisptr, CreateInterfaceFn factory);
inline bool(*CEngineAPI__ModInit)(CEngineAPI* pEngineAPI, const char* pModName, const char* pGameDir);
inline bool(*CEngineAPI__OnStartup)(CEngineAPI* pEngineAPI, void* pInstance, const char* pStartupModName);
inline bool(*CEngineAPI__MainLoop)(void);
inline void(*CEngineAPI__PumpMessages)(void);
inline void(*CEngineAPI__SetStartupInfo)(CEngineAPI* pEngineAPI, StartupInfo_t* pStartupInfo);
inline void(*v_PakFile_Init)(char* buffer, char* source, char vpk_file);

inline int(*v_Sys_GetHostVersion)();

inline bool* g_bStartupInfoSet = nullptr;
inline char* g_szBaseDir = nullptr; // static size = MAX_OSPATH

///////////////////////////////////////////////////////////////////////////////
class VSys_Dll2 : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("CEngineAPI::Init", CEngineAPI__Init);
		LogFunAdr("CEngineAPI::Shutdown", CEngineAPI__Shutdown);
		LogFunAdr("CEngineAPI::Connect", CEngineAPI__Connect);
		LogFunAdr("CEngineAPI::ModInit", CEngineAPI__ModInit);
		LogFunAdr("CEngineAPI::OnStartup", CEngineAPI__OnStartup);
		LogFunAdr("CEngineAPI::MainLoop", CEngineAPI__MainLoop);
		LogFunAdr("CEngineAPI::PumpMessages", CEngineAPI__PumpMessages);
		LogFunAdr("CEngineAPI::SetStartupInfo", CEngineAPI__SetStartupInfo);
		LogFunAdr("PakFile_Init", v_PakFile_Init);
		LogFunAdr("Sys_GetHostVersion", v_Sys_GetHostVersion);
		LogVarAdr("g_bStartupInfoSet", g_bStartupInfoSet);
		LogVarAdr("g_szBaseDir", g_szBaseDir);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC 20 48 8B F1 48 8D 3D ?? ?? ?? ?? 33 DB 48 8D 15 ?? ?? ?? ??").GetPtr(CEngineAPI__Init);
		Module_FindPattern(g_GameDll, "48 83 EC 28 48 8B 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 85 C0 48 89 15").GetPtr(CEngineAPI__Connect);
		Module_FindPattern(g_GameDll, "48 83 EC 28 48 8B 0D ?? ?? ?? ?? 33 D2 48 8B 01 FF 90 ?? ?? ?? ?? B1 01").GetPtr(CEngineAPI__Shutdown);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 4C 24 ?? 55 56 57 41 54 41 55 41 56 41 57 48 81 EC ?? ?? ?? ?? 4D 8B F8").GetPtr(CEngineAPI__ModInit);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 81 EC ?? ?? ?? ?? 49 8B F8 48 8B DA").GetPtr(CEngineAPI__OnStartup);
		Module_FindPattern(g_GameDll, "4C 8B DC 49 89 4B 08 48 81 EC ?? ?? ?? ?? 8B 05 ?? ?? ?? ??").GetPtr(CEngineAPI__MainLoop);
		Module_FindPattern(g_GameDll, "44 88 44 24 ?? 53 55 56 57").GetPtr(v_PakFile_Init);
		Module_FindPattern(g_GameDll, "8B 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC CC 48 83 3D").GetPtr(v_Sys_GetHostVersion);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 55 48 81 EC ?? ?? ?? ?? 45 33 C9").GetPtr(CEngineAPI__PumpMessages);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? ?? 48 81 EC ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 48 8B DA").GetPtr(CEngineAPI__SetStartupInfo);
	}
	virtual void GetVar(void) const
	{
		g_bStartupInfoSet = CMemory(CEngineAPI__SetStartupInfo).FindPattern("80 3D", CMemory::Direction::DOWN, 250).ResolveRelativeAddressSelf(0x2, 0x7).RCast<bool*>();
		g_szBaseDir = CMemory(CEngineAPI__SetStartupInfo).FindPattern("48 8D", CMemory::Direction::DOWN, 250).ResolveRelativeAddressSelf(0x3, 0x7).RCast<char*>();
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
