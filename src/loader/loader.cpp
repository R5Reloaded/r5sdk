//===========================================================================//
// 
// Purpose: SDK loader stub
// 
// --------------------------------------------------------------------------
// The game module cannot be imported directly by the executable, as the
// internal memalloc system is not initialized before the entry point is
// called. So we need to load the SDK dll after the entry point is called,
// but before LauncherMain is called.
// 
// The executable exports table has been restructured; the exported function
// 'GetDenuvoTimeTicketRequest' has been swapped with 'CreateGlobalMemAlloc',
// the exported 'IEngineAPI' interface accessor has been replaced with
// 'g_pMemAllocSingleton', so we can obtain the addresses without hardcoding.
// 
// These changes allow us to load the SDK in the following order:
// - Create game process
// - Import this loader stub by its dummy export.
// - Immediately hook 'LauncherMain' by getting the address from the exports.
// - Determine if, and which SDK module to load.
// 
// Since LauncherMain is called before anything of the game is, we can still
// hook and modify anything of the game before it starts. With this order of
// initialization, we can now replace the standard memalloc system with that
// of the game, by:
// 
// - Redefining the standard C functions to use the internal memalloc system.
// - Checking if the memalloc system has been initialized, and create if not.
//===========================================================================//
#include "loader.h"
#include "tier0/module.h"

//-----------------------------------------------------------------------------
// SDK image statics
//-----------------------------------------------------------------------------
static HMODULE s_sdkModuleHandle = NULL;

typedef void (*InitFunc)(void);
static InitFunc s_sdkInitFunc = NULL;
static InitFunc s_sdkShutdownFunc = NULL;

//-----------------------------------------------------------------------------
// LauncherMain function pointer
//-----------------------------------------------------------------------------
static int (*v_LauncherMain)(HINSTANCE, HINSTANCE, LPSTR, int) = nullptr;

//-----------------------------------------------------------------------------
// Purpose: Terminates the process with an error when called
//-----------------------------------------------------------------------------
static void Loader_FatalError(const char* const fmt, ...)
{
	Assert(0);

	va_list vArgs;
	va_start(vArgs, fmt);

	char errorBuf[1024];
	const int ret = V_vsnprintf(errorBuf, sizeof(errorBuf), fmt, vArgs);

	if (ret < 0)
		errorBuf[0] = '\0';

	va_end(vArgs);

	MessageBoxA(NULL, errorBuf, "Loader Error", MB_ICONERROR | MB_OK);
	TerminateProcess(GetCurrentProcess(), 0xBAD0C0DE);
}

//-----------------------------------------------------------------------------
// Purpose: Loads the SDK module
//-----------------------------------------------------------------------------
static void Loader_InitGameSDK(const LPSTR lpCmdLine)
{
	if (V_strstr(lpCmdLine, "-noworkerdll"))
		return;

	char moduleName[MAX_OSPATH];

	if (!GetModuleFileNameA((HMODULE)NtCurrentPeb()->ImageBaseAddress,
		moduleName, sizeof(moduleName)))
	{
		Loader_FatalError("Failed to retrieve process module name: error code = %08x\n", GetLastError());
		return;
	}

	// Prune the path.
	const char* const pModuleName = strrchr(moduleName, '\\') + 1;
	const bool bDedicated = V_stricmp(pModuleName, SERVER_GAME_DLL) == NULL;

	// The dedicated server has its own SDK module,
	// so we need to check whether we are running
	// the base game or the dedicated server.
	if (!bDedicated)
	{
		// Load the client dll if '-noserverdll' is passed,
		// as this command lime parameter prevents the
		// server dll from initializing in the engine.
		if (V_strstr(lpCmdLine, "-noserverdll"))
			s_sdkModuleHandle = LoadLibraryA(CLIENT_WORKER_DLL);
		else
			s_sdkModuleHandle = LoadLibraryA(MAIN_WORKER_DLL);
	}
	else
		s_sdkModuleHandle = LoadLibraryA(SERVER_WORKER_DLL);

	if (!s_sdkModuleHandle)
	{
		Loader_FatalError("Failed to load SDK module: error code = %08x\n", GetLastError());
		return;
	}

	s_sdkInitFunc = (InitFunc)GetProcAddress(s_sdkModuleHandle, "SDK_Init");

	if (s_sdkInitFunc)
		s_sdkShutdownFunc = (InitFunc)GetProcAddress(s_sdkModuleHandle, "SDK_Shutdown");

	if (!s_sdkInitFunc || !s_sdkShutdownFunc)
	{
		Loader_FatalError("Loaded SDK module is invalid: error code = %08x\n", GetLastError());
		return;
	}

	s_sdkInitFunc();
}

//-----------------------------------------------------------------------------
// Purpose: Unloads the SDK module
//-----------------------------------------------------------------------------
static void Loader_ShutdownGameSDK()
{
	if (s_sdkModuleHandle)
	{
		s_sdkShutdownFunc();

		FreeLibrary(s_sdkModuleHandle);
		s_sdkModuleHandle = NULL;
	}
}

//-----------------------------------------------------------------------------
// Purpose: LauncherMain hook; loads the SDK before the game inits
//-----------------------------------------------------------------------------
static int WINAPI hkLauncherMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	Loader_InitGameSDK(lpCmdLine); // Init GameSDK, internal function calls LauncherMain.
	const int ret = v_LauncherMain(hInstance, hPrevInstance, lpCmdLine, nShowCmd);
	Loader_ShutdownGameSDK();

	return ret;
}

//-----------------------------------------------------------------------------
// Purpose: hooks the entry point
//-----------------------------------------------------------------------------
static void Loader_AttachToEntryPoint()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	DetourAttach(&v_LauncherMain, &hkLauncherMain);
	const HRESULT hr = DetourTransactionCommit();

	if (hr != NO_ERROR) // Failed to hook into the process, terminate...
		Loader_FatalError("Failed to detour process: error code = %08x\n", hr);
}

//-----------------------------------------------------------------------------
// Purpose: unhooks the entry point
//-----------------------------------------------------------------------------
static void Loader_DetachFromEntryPoint()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	DetourDetach(&v_LauncherMain, &hkLauncherMain);
	const HRESULT hr = DetourTransactionCommit();

	Assert(hr == S_OK);
	NOTE_UNUSED(hr);
}

//-----------------------------------------------------------------------------
// Purpose: APIENTRY
//-----------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
	{
		v_LauncherMain = CModule::GetExportedSymbol(NtCurrentPeb()->ImageBaseAddress, "LauncherMain")
			.RCast<int (*)(HINSTANCE, HINSTANCE, LPSTR, int)>();

		Loader_AttachToEntryPoint();
		break;
	}

	case DLL_PROCESS_DETACH:
	{
		Loader_DetachFromEntryPoint();
		break;
	}
	}

	return TRUE;
}
