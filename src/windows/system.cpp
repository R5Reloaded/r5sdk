#include "core/stdafx.h"
#include "core/init.h"
#include "windows/system.h"
#include "engine/host_state.h"
#include "engine/sys_mainwind.h"
#include "tier0/frametask.h"

#pragma warning(push)
#pragma warning( disable : 4005 )
#include <ntstatus.h>
#pragma warning(pop)

typedef struct _UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR  Buffer;
} UNICODE_STRING, * PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
	ULONG           Length;
	HANDLE          RootDirectory;
	PUNICODE_STRING ObjectName;
	ULONG           Attributes;
	PVOID           SecurityDescriptor;
	PVOID           SecurityQualityOfService;
} OBJECT_ATTRIBUTES, * POBJECT_ATTRIBUTES;

///////////////////////////////////////////////////////////////////////////////
typedef BOOL(WINAPI* IGetVersionExA)(
	_Inout_ LPOSVERSIONINFOA lpVersionInformation);

typedef BOOL(WINAPI* IPeekMessage)(
	_Out_ LPMSG lpMsg,
	_In_opt_ HWND hWnd,
	_In_ UINT wMsgFilterMin,
	_In_ UINT wMsgFilterMax,
	_In_ UINT wRemoveMsg);

typedef HMODULE(WINAPI* ILoadLibraryA)(
	_In_ LPCSTR lpLibFileName
);

typedef HMODULE(WINAPI* ILoadLibraryW)(
	_In_ LPCWSTR lpLibFileName
);

typedef HMODULE (WINAPI* ILoadLibraryExA)(
	_In_ LPCSTR lpLibFileName,
	_Reserved_ HANDLE hFile,
	_In_ DWORD dwFlags
);

typedef HMODULE(WINAPI* ILoadLibraryExW)(
	_In_ LPCWSTR lpLibFileName,
	_Reserved_ HANDLE hFile,
	_In_ DWORD dwFlags
	);

typedef NTSTATUS (NTAPI* INtOpenFile)(
	OUT PHANDLE FileHandle,
	IN ACCESS_MASK DesiredAccess,
	IN POBJECT_ATTRIBUTES ObjectAttributes,
	OUT struct IO_STATUS_BLOCK* IoStatusBlock,
	IN ULONG ShareAccess,
	IN ULONG OpenOptions
);

static IGetVersionExA									   VGetVersionExA = nullptr;
static IPeekMessage                                      VPeekMessageA  = nullptr;
static IPeekMessage                                      VPeekMessageW  = nullptr;

static ILoadLibraryA									   VLoadLibraryA = nullptr;
static ILoadLibraryW									   VLoadLibraryW = nullptr;

static ILoadLibraryExA								   VLoadLibraryExA = nullptr;
static ILoadLibraryExW								   VLoadLibraryExW = nullptr;
static INtOpenFile										   VNtOpenFile = nullptr;

enum ModuleBlockAction_e
{
	WARN,		//This should be used for modules that may cause issues but often work fine
	BLOCK,		//This should be used for modules that are completely incompatible with the game
	ERROR,	//This should be used when a BLOCK module doesnt behave well with being blocked, e.g blitz
};

struct DllBlock_t
{
	const wchar_t* m_pwszModuleName;
	const char* m_pszFriendlyName;
	ModuleBlockAction_e m_eAction;
	std::atomic_bool m_bNotified{ false };
};

static DllBlock_t s_DllBlockList[] = {
	{L"apex-internal",  "blitz.gg",  ERROR}
};

bool ValidateModuleLoadAllowedAndNotify(const wchar_t* const pwszModulePath)
{
	wchar_t wszModuleNameLower[MAX_OSPATH];
	char szModuleName[MAX_OSPATH];

	const wchar_t* const pwszFileName = V_UnqualifiedFileName(pwszModulePath);
	const wchar_t* const pwszExtensionStart = V_GetFileExtension(pwszFileName);

	const size_t nStringLength = pwszExtensionStart - pwszFileName;

	if (!nStringLength)
		return true;

	const size_t nCopyLen = min(nStringLength, SDK_ARRAYSIZE(wszModuleNameLower));
	memcpy(wszModuleNameLower, pwszFileName, nCopyLen * sizeof(wchar_t));
	wszModuleNameLower[nCopyLen - 1] = L'\0';

	DllBlock_t* pDllBlock = nullptr;
	for (size_t i = 0; i < SDK_ARRAYSIZE(s_DllBlockList); i++)
	{
		if (wcscmp(wszModuleNameLower, s_DllBlockList[i].m_pwszModuleName) == 0)
		{
			pDllBlock = &s_DllBlockList[i];
			break;
		}
	}
	
	if (!pDllBlock)
		return true;

	V_UnicodeToUTF8(pwszFileName, szModuleName, sizeof(szModuleName));

	switch (pDllBlock->m_eAction)
	{
	case ModuleBlockAction_e::ERROR:
	{
		if (!pDllBlock->m_bNotified)
		{
			std::string moduleName = szModuleName;
			std::string friendlyName = pDllBlock->m_pszFriendlyName;

			g_TaskQueue.Dispatch([moduleName = std::move(moduleName), friendlyName = std::move(friendlyName)] {
				Error(eDLL_T::SYSTEM_ERROR, EXIT_FAILURE, "Unsupported module loaded: %s (%s)\nPlease exit this program and restart the game.\n",
					moduleName.c_str(), friendlyName.c_str());
				}, 0);

			pDllBlock->m_bNotified = true;
		}

		return false;
	}
	case ModuleBlockAction_e::WARN:
	{
		if (!pDllBlock->m_bNotified)
		{
			std::string moduleName = szModuleName;
			std::string friendlyName = pDllBlock->m_pszFriendlyName;

			g_TaskQueue.Dispatch([moduleName  = std::move(moduleName), friendlyName = std::move(friendlyName)] {
				MessageBoxA(NULL, Format("Known problematic module loaded, you may encounter issues\n%s (%s)", moduleName.c_str(),
					friendlyName.c_str()).c_str(), "Problematic module", MB_OK | MB_ICONEXCLAMATION);
			}, 0);

			Warning(eDLL_T::SYSTEM_WARNING, "Known problematic module %s (%s)\n", szModuleName, pDllBlock->m_pszFriendlyName);
			pDllBlock->m_bNotified = true;
		}

		return true;
	}
	case ModuleBlockAction_e::BLOCK:
	{
		Warning(eDLL_T::SYSTEM_WARNING, "Module '%s' load blocked\n", szModuleName);
		return false;
	}
	default:
		return true;
	}
}

//#############################################################################
// SYSTEM HOOKS
//#############################################################################

BOOL
WINAPI
HGetVersionExA(
	_Inout_ LPOSVERSIONINFOA lpVersionInformation)
{
#ifdef DEDICATED
	// Return false for dedicated to skip 'SetProcessDpiAwareness' in 'CEngineAPI:OnStartup()'.
	return NULL;
#else
	return VGetVersionExA(lpVersionInformation);
#endif // DEDICATED
}

BOOL
WINAPI
HPeekMessage(
	_Out_ LPMSG lpMsg,
	_In_opt_ HWND hWnd,
	_In_ UINT wMsgFilterMin,
	_In_ UINT wMsgFilterMax,
	_In_ UINT wRemoveMsg)
{
#ifdef DEDICATED
	// Return false for dedicated to reduce unnecessary overhead when calling 'PeekMessageA/W()' every frame.
	return NULL;
#else
	return VPeekMessageA(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
#endif // DEDICATED
}

HMODULE WINAPI HLoadLibraryExA(
	_In_ LPCSTR lpLibFileName,
	_Reserved_ HANDLE hFile,
	_In_ DWORD dwFlags
)
{
	wchar_t wszModulePath[MAX_OSPATH];
	V_UTF8ToUnicode(lpLibFileName, wszModulePath, sizeof(wszModulePath));
	if (!ValidateModuleLoadAllowedAndNotify(wszModulePath))
	{
		return NULL;
	}

	return VLoadLibraryExA(lpLibFileName, hFile, dwFlags);
}

HMODULE WINAPI HLoadLibraryExW(
	_In_ LPCWSTR lpLibFileName,
	_Reserved_ HANDLE hFile,
	_In_ DWORD dwFlags
)
{
	if (!ValidateModuleLoadAllowedAndNotify(lpLibFileName))
	{
		return NULL;
	}

	return VLoadLibraryExW(lpLibFileName, hFile, dwFlags);
}

HMODULE WINAPI HLoadLibraryA(
	_In_ LPCSTR lpLibFileName
)
{
	return HLoadLibraryExA(lpLibFileName, NULL, 0);
}

HMODULE WINAPI HLoadLibraryW(
	_In_ LPCWSTR lpLibFileName
)
{
	return HLoadLibraryExW(lpLibFileName, NULL, 0);
}

NTSTATUS NTAPI HNtOpenFile(
	OUT PHANDLE FileHandle,
	IN ACCESS_MASK DesiredAccess,
	IN OBJECT_ATTRIBUTES* ObjectAttributes,
	OUT struct IO_STATUS_BLOCK* IoStatusBlock,
	IN ULONG ShareAccess,
	IN ULONG OpenOptions
)
{
	if (ObjectAttributes->ObjectName->Buffer)
	{
		if ((DesiredAccess & (FILE_EXECUTE | FILE_READ_DATA)) == (FILE_EXECUTE | FILE_READ_DATA))
		{
			if (!ValidateModuleLoadAllowedAndNotify(ObjectAttributes->ObjectName->Buffer))
			{
				return STATUS_OBJECT_NAME_NOT_FOUND;
			}
		}
	}

	return VNtOpenFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
}

BOOL
WINAPI
ConsoleHandlerRoutine(
	DWORD eventCode)
{
	switch (eventCode)
	{
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		
		if (!g_bSdkShutdownInitiatedFromConsoleHandler)
			g_bSdkShutdownInitiatedFromConsoleHandler = true;

		if (g_pHostState) // This tells the engine to gracefully shutdown on the next frame.
			g_pHostState->m_iNextState = HostStates_t::HS_SHUTDOWN;

		// Give it time to shutdown properly, this loop waits for max time
		// of SPI_GETWAITTOKILLSERVICETIMEOUT, which is 20000ms by default.
		while (g_bSdkInitialized)
			Sleep(50);

		return TRUE;
	}

	return FALSE;
}

//#############################################################################
// MANAGEMENT
//#############################################################################

void WinSys_Init()
{
	VLoadLibraryExA = (ILoadLibraryExA)DetourFindFunction("KERNEL32.dll", "LoadLibraryExA");
	VLoadLibraryExW = (ILoadLibraryExW)DetourFindFunction("KERNEL32.dll", "LoadLibraryExW");
	VLoadLibraryA = (ILoadLibraryA)DetourFindFunction("KERNEL32.dll", "LoadLibraryA");
	VLoadLibraryW = (ILoadLibraryW)DetourFindFunction("KERNEL32.dll", "LoadLibraryW");
	VNtOpenFile = (INtOpenFile)DetourFindFunction("ntdll.dll", "NtOpenFile");

	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	DetourAttach(&(LPVOID&)VLoadLibraryExA, (PBYTE)HLoadLibraryExA);
	DetourAttach(&(LPVOID&)VLoadLibraryExW, (PBYTE)HLoadLibraryExW);
	DetourAttach(&(LPVOID&)VLoadLibraryA, (PBYTE)HLoadLibraryA);
	DetourAttach(&(LPVOID&)VLoadLibraryW, (PBYTE)HLoadLibraryW);
	DetourAttach(&(LPVOID&)VNtOpenFile, (PBYTE)HNtOpenFile);

#ifdef DEDICATED
	VGetVersionExA = (IGetVersionExA)DetourFindFunction("KERNEL32.dll", "GetVersionExA");
	VPeekMessageA = (IPeekMessage)DetourFindFunction("USER32.dll", "PeekMessageA");
	VPeekMessageW = (IPeekMessage)DetourFindFunction("USER32.dll", "PeekMessageW");

	///////////////////////////////////////////////////////////////////////////
	DetourAttach(&(LPVOID&)VGetVersionExA, (PBYTE)HGetVersionExA);
	DetourAttach(&(LPVOID&)VPeekMessageA, (PBYTE)HPeekMessage);
	//DetourAttach(&(LPVOID&)VPeekMessageW, (PBYTE)HPeekMessage);	
#endif // DEDICATED

	HRESULT hr = DetourTransactionCommit();
	if (hr != NO_ERROR)
	{
		// Failed to hook into the process, terminate
		Assert(0);
		Error(eDLL_T::COMMON, 0xBAD0C0DE, "Failed to detour process: error code = %08x\n", hr);
	}
}

void WinSys_Shutdown()
{
	///////////////////////////////////////////////////////////////////////////
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

#ifdef DEDICATED
	///////////////////////////////////////////////////////////////////////////
	DetourDetach(&(LPVOID&)VGetVersionExA, (PBYTE)HGetVersionExA);
	DetourDetach(&(LPVOID&)VPeekMessageA, (PBYTE)HPeekMessage);
	//DetourDetach(&(LPVOID&)VPeekMessageW, (PBYTE)HPeekMessage);
#endif // DEDICATED
	DetourDetach(&(LPVOID&)VLoadLibraryExA, (PBYTE)HLoadLibraryExA);
	DetourDetach(&(LPVOID&)VLoadLibraryExW, (PBYTE)HLoadLibraryExW);
	DetourDetach(&(LPVOID&)VLoadLibraryA, (PBYTE)HLoadLibraryA);
	DetourDetach(&(LPVOID&)VLoadLibraryW, (PBYTE)HLoadLibraryW);
	DetourDetach(&(LPVOID&)VNtOpenFile, (PBYTE)HNtOpenFile);
	///////////////////////////////////////////////////////////////////////////

	DetourTransactionCommit();
}
