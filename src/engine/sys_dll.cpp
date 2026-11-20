//=============================================================================//
//
// Purpose: IApplication methods
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier0/frametask.h"
#include "tier0/commandline.h"
#include "tier1/cvar.h"
#include "vpc/interfaces.h"
#include "common/engine_launcher_api.h"
#include "pluginsystem/pluginsystem.h"
#include "pluginsystem/modsystem.h"
#include "ebisusdk/EbisuSDK.h"
#include "engine/cmodel_bsp.h"
#include "engine/sys_engine.h"
#include "engine/sys_dll.h"
#include "engine/sys_dll2.h"
#include "engine/sdk_dll.h"
#include "engine/host_cmd.h"
#include "engine/enginetrace.h"
#include "engine/debugoverlay.h"
#ifndef CLIENT_DLL
#include "engine/server/server.h"
#include "engine/server/sv_main.h"
#include "server/vengineserver_impl.h"
#include "game/server/gameinterface.h"
#endif // !CLIENT_DLL
#ifndef DEDICATED
#include "game/client/cliententitylist.h"
#include "gameui/IConsole.h"
#include "windows/id3dx.h"
#include "windows/input.h"
#endif // !DEDICATED
#include "vscript/languages/squirrel_re/vsquirrel_bridge.h"
#include "vstdlib/keyvaluessystem.h"

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CSourceAppSystemGroup::StaticPreInit(CSourceAppSystemGroup* pSourceAppSystemGroup)
{
	return CSourceAppSystemGroup__PreInit(pSourceAppSystemGroup);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CSourceAppSystemGroup::StaticCreate(CSourceAppSystemGroup* pSourceAppSystemGroup)
{
	return CSourceAppSystemGroup__Create(pSourceAppSystemGroup);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
int CModAppSystemGroup::StaticMain(CModAppSystemGroup* pModAppSystemGroup)
{
	int nRunResult = RUN_OK;
	HEbisuSDK_Init(); // Not here in retail. We init EbisuSDK here though.

	g_pEngine->SetQuitting(IEngine::QUIT_NOTQUITTING);
	if (g_pEngine->Load(pModAppSystemGroup->IsServerOnly(), g_pEngineParms->baseDirectory))
	{
		if (CEngineAPI::MainLoop())
		{
			nRunResult = RUN_RESTART;
		}
		g_pEngine->Unload();

#ifndef CLIENT_DLL
		SV_ShutdownGameDLL();
#endif // !CLIENT_DLL
	}
	return nRunResult;
}

//-----------------------------------------------------------------------------
// Purpose: Initialize plugin system
//-----------------------------------------------------------------------------
static void PluginSystem_Init(CModAppSystemGroup* const pModAppSystemGroup)
{
	PluginSystem()->Init();

	CALL_PLUGIN_CALLBACKS(PluginSystem()->GetCreateCallbacks(), pModAppSystemGroup);
	ModSystem()->Init();
}

//-----------------------------------------------------------------------------
// Purpose: Shutdown plugin system
//-----------------------------------------------------------------------------
static void PluginSystem_Shutdown(CModAppSystemGroup* const pModAppSystemGroup)
{
	ModSystem()->Shutdown();
	CALL_PLUGIN_CALLBACKS(PluginSystem()->GetDestroyCallbacks(), pModAppSystemGroup);

	PluginSystem()->Shutdown();
}

//-----------------------------------------------------------------------------
// Purpose: Instantiate all main libraries
//-----------------------------------------------------------------------------
bool CModAppSystemGroup::StaticCreate(CModAppSystemGroup* pModAppSystemGroup)
{
#ifdef DEDICATED
	pModAppSystemGroup->SetServerOnly();
	g_pServer->SetDedicated(true);
#endif // DEDICATED

	EXPOSE_INTERFACE_FN((InstantiateInterfaceFn)EngineCVar, CCvar, CVAR_INTERFACE_VERSION);
	EXPOSE_INTERFACE_FN((InstantiateInterfaceFn)FileSystem, CFileSystem_Stdio, BASEFILESYSTEM_INTERFACE_VERSION);
	EXPOSE_INTERFACE_FN((InstantiateInterfaceFn)FileSystem, CFileSystem_Stdio, FILESYSTEM_INTERFACE_VERSION);
	EXPOSE_INTERFACE_FN((InstantiateInterfaceFn)KeyValuesSystem, CKeyValuesSystem, KEYVALUESSYSTEM_INTERFACE_VERSION);
	EXPOSE_INTERFACE_FN((InstantiateInterfaceFn)SquirrelVMBridge, CSquirrelVMBridge, SQUIRRELVM_BRIDGE_INTERFACE_VERSION);
	EXPOSE_INTERFACE_FN((InstantiateInterfaceFn)PluginSystem, CPluginSystem, INTERFACEVERSION_PLUGINSYSTEM);

	PluginSystem_Init(pModAppSystemGroup);

	g_pDebugOverlay = (CIVDebugOverlay*)g_FactorySystem.GetFactory(VDEBUG_OVERLAY_INTERFACE_VERSION);
#ifndef CLIENT_DLL
	g_pServerGameDLL = (CServerGameDLL*)g_FactorySystem.GetFactory(INTERFACEVERSION_SERVERGAMEDLL);
	g_pServerGameClients = (CServerGameClients*)g_FactorySystem.GetFactory(INTERFACEVERSION_SERVERGAMECLIENTS_NEW);
	if (!g_pServerGameClients)
		g_pServerGameClients = (CServerGameClients*)g_FactorySystem.GetFactory(INTERFACEVERSION_SERVERGAMECLIENTS);
	g_pServerGameEntities = (CServerGameEnts*)g_FactorySystem.GetFactory(INTERFACEVERSION_SERVERGAMEENTS);

#endif // !CLIENT_DLL
#ifndef DEDICATED
	g_pClientEntityList = (IClientEntityList*)g_FactorySystem.GetFactory(VCLIENTENTITYLIST_INTERFACE_VERSION);
	g_pEngineTraceClient = (CEngineTraceClient*)g_FactorySystem.GetFactory(INTERFACEVERSION_ENGINETRACE_CLIENT);

	g_ImGuiConfig.Load(); // Load ImGui configs.
	DirectX_Init();

#endif // !DEDICATED
	if (CommandLine()->CheckParm("-devsdk"))
	{
		cv->EnableDevCvars();
	}

	g_TaskQueueList.push_back(&g_TaskQueue);
	g_bAppSystemInit = true;

	return CModAppSystemGroup__Create(pModAppSystemGroup);
}

//-----------------------------------------------------------------------------
// Purpose: Destroy all main libraries
//-----------------------------------------------------------------------------
void CModAppSystemGroup::StaticDestroy(CModAppSystemGroup* pModAppSystemGroup)
{
	CModAppSystemGroup__Destroy(pModAppSystemGroup);
	PluginSystem_Shutdown(pModAppSystemGroup);
}

//-----------------------------------------------------------------------------
//	Sys_Error_Internal
//
//-----------------------------------------------------------------------------
int HSys_Error_Internal(char* fmt, va_list args)
{
	char buffer[2048];
	Error(eDLL_T::ENGINE, NO_ERROR, "_______________________________________________________________\n");
	Error(eDLL_T::ENGINE, NO_ERROR, "] ENGINE ERROR ################################################\n");

	int nLen = vsprintf(buffer, fmt, args);
	bool shouldNewline = true;
	
	if (nLen > 0)
		shouldNewline = buffer[nLen - 1] != '\n';

	Error(eDLL_T::ENGINE, NO_ERROR, shouldNewline ? "%s\n" : "%s", buffer);

	///////////////////////////////////////////////////////////////////////////
	return Sys_Error_Internal(fmt, args);
}

void VSys_Dll::Detour(const bool bAttach) const
{
	DetourSetup(&CModAppSystemGroup__Main, &CModAppSystemGroup::StaticMain, bAttach);
	DetourSetup(&CModAppSystemGroup__Create, &CModAppSystemGroup::StaticCreate, bAttach);
	DetourSetup(&CModAppSystemGroup__Destroy, &CModAppSystemGroup::StaticDestroy, bAttach);

	DetourSetup(&CSourceAppSystemGroup__PreInit, &CSourceAppSystemGroup::StaticPreInit, bAttach);
	DetourSetup(&CSourceAppSystemGroup__Create, &CSourceAppSystemGroup::StaticCreate, bAttach);

	DetourSetup(&Sys_Error_Internal, &HSys_Error_Internal, bAttach);
}
