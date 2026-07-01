//===========================================================================//
//
// Purpose: 
//
//===========================================================================//

#include "core/stdafx.h"
#include "vpc/IAppSystem.h"
#include "windows/id3dx.h"
#include "geforce/reflex.h"
#include "inputsystem/inputsystem.h"
#include <materialsystem/cmaterialsystem.h>
#include <hidusage.h>
#include <engine/sys_mainwind.h>
#include <materialsystem/cmatqueuedrendercontext.h>
#include <tier0/frametask.h>
#include <gameui/imgui_system.h>

HANDLE CInputSystem::s_hRawInputThread = NULL;
HANDLE CInputSystem::s_hRawInputShutdownEvent = NULL;

//-----------------------------------------------------------------------------
// Returns the currently attached window
//-----------------------------------------------------------------------------
PlatWindow_t CInputSystem::GetAttachedWindow() const
{
	return (PlatWindow_t)m_hAttachedHWnd;
}

void CInputSystem::Shutdown(CInputSystem* thisp)
{
    if (s_hRawInputThread && s_hRawInputShutdownEvent)
    {
		SetEvent(s_hRawInputShutdownEvent);
		WaitForSingleObject( s_hRawInputThread, INFINITE );
		CloseHandle( s_hRawInputThread );
		CloseHandle( s_hRawInputShutdownEvent );

		s_hRawInputThread = NULL;
		s_hRawInputShutdownEvent = NULL;
    }

    CInputSystem__Shutdown(thisp);
}

unsigned int __stdcall CInputSystem::RawInputCaptureThread( LPVOID lpThreadParam )
{
	UNUSED_ATTR( lpThreadParam );
	
	WNDCLASSEX wndClass{ 0 };
	wndClass.cbSize		   = sizeof( wndClass );
	wndClass.lpfnWndProc   = DefWindowProc;
	wndClass.lpszClassName = TEXT("GameMessages");
	wndClass.hInstance	   = g_pGame->GetInstance();

	RegisterClassEx( &wndClass );
	HWND messageWnd = CreateWindowEx( 0, wndClass.lpszClassName, TEXT("Messages"), 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, g_pGame->GetInstance(), NULL );

	RAWINPUTDEVICE dev = { 0 };
	dev.usUsagePage	   = HID_USAGE_PAGE_GENERIC;
	dev.usUsage		   = HID_USAGE_GENERIC_MOUSE;
	dev.dwFlags		   = RIDEV_INPUTSINK;
	dev.hwndTarget	   = messageWnd;

	RegisterRawInputDevices( &dev, 1, sizeof( RAWINPUTDEVICE ) );

	RAWINPUT rawInputEvents[512];
	for ( ;; )
	{
		const DWORD result = MsgWaitForMultipleObjectsEx(1, &s_hRawInputShutdownEvent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);

        switch ( result )
		{
		case WAIT_OBJECT_0: //Exit event
        {
			dev.dwFlags = RIDEV_REMOVE;
			RegisterRawInputDevices( &dev, 1, sizeof( RAWINPUTDEVICE ) );
			DestroyWindow( messageWnd );
			UnregisterClass( wndClass.lpszClassName, wndClass.hInstance );
			return 0;
        }
		case WAIT_OBJECT_0 + 1:
        {
			UINT nEvents;
			do
			{
				UINT bufferSize = sizeof( rawInputEvents );
				nEvents		= GetRawInputBuffer( rawInputEvents, &bufferSize, sizeof( RAWINPUTHEADER ) );
				if ( nEvents == 0 || nEvents == (UINT)-1 )
					break;

				if ( g_pInputSystem->m_bEnabled )
					g_pInputSystem->RecordRawInputMouseMove( rawInputEvents, nEvents );

			} while ( nEvents > 0 );

			MSG msg;
			while ( PeekMessageW( &msg, 0, 0, 0, PM_REMOVE ) )
			{
				TranslateMessage( &msg );
				DispatchMessageW( &msg );
			}

			break;
        }
		case WAIT_FAILED:
        {
			g_TaskQueue.Dispatch([](){
                const DWORD code = GetLastError();
                Error( eDLL_T::ENGINE, EXIT_FAILURE, "CInputSystem::RawInputCaptureThread MsgWaitForMultipleObjectsEx Failed Code: %d\n", code);
            }, 0);

			return EXIT_FAILURE;
        }
		default: 
            break;
		}
	}
}

void CInputSystem::RecordRawInputMouseMove( const PRAWINPUT pInputEvents, const size_t nEvents )
{
	PRAWINPUT pCurrentEvent = pInputEvents;

    int xAccum = 0;
	int yAccum = 0;

    for (size_t i = 0; i < nEvents; i++)
    {
        if (pCurrentEvent->header.dwType == RIM_TYPEMOUSE)
        {
			xAccum += pCurrentEvent->data.mouse.lLastX;
			yAccum += pCurrentEvent->data.mouse.lLastY;
        }

        pCurrentEvent = NEXTRAWINPUTBLOCK( pCurrentEvent );
    }

    m_MouseAccum.Accumulate( xAccum, yAccum );
}

LRESULT CInputSystem::WindowProc(void* unused, HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (g_pInputSystem->m_bEnabled &&
		((hwnd == g_pInputSystem->m_hAttachedHWnd) || (uMsg == WM_ACTIVATEAPP)) &&
		(uMsg != WM_CLOSE))
	{
		if (PCLSTATS_IS_PING_MSG_ID(uMsg))
		{
			GeForce_SetLatencyMarker(D3D11Device(), PC_LATENCY_PING, MaterialSystem()->GetCurrentFrameCount());
		}
	}

    if (!g_pInputSystem->m_bMouseCursorVisible && uMsg == WM_MOUSEMOVE)
    {
        return 0;
    }
    
	return CInputSystem__WindowProc( unused, hwnd, uMsg, wParam, lParam );
}

struct RenderThreadFunctorItem
{
	void* m_pFunc;
	HWND m_Arg;
};

static void SetCaptureThreadAwareFunctor( CallQueue_s* const queue )
{
	RenderThreadFunctorItem* const task	= (RenderThreadFunctorItem*)queue->GetCurrentCallItem();
	HWND	targetWnd		= task->m_Arg;
	reinterpret_cast<HWND ( * )( HWND )>( task->m_pFunc )( targetWnd );
	queue->currentCallIndex += sizeof( RenderThreadFunctorItem );
}

static void SetCaptureThreadAware(HWND hWnd)
{
    if ( (*g_fnHasRenderCallQueue)() )
    {
		CallQueue_s* const queue = ( *g_fnAddRenderCallQueueItem )( SetCaptureThreadAwareFunctor, sizeof( RenderThreadFunctorItem ), 7 );
		RenderThreadFunctorItem* const task	= (RenderThreadFunctorItem*)queue->GetCurrentAllocatedItem();
		task->m_pFunc =             (void*)SetCaptureThreadAware;
		task->m_Arg = hWnd;
		( *g_fnAdvanceRenderCallQueue )( sizeof( RenderThreadFunctorItem ) );

		return;
    }
    else
    {
		SetCapture( hWnd );
    }
}

static void ReleaseCaptureThreadAwareFunctor( CallQueue_s* const queue )
{
	RenderThreadFunctorItem* const task = (RenderThreadFunctorItem*)queue->GetCurrentCallItem();
	reinterpret_cast<void ( * )( void )>( task->m_pFunc )();
	queue->currentCallIndex += sizeof( RenderThreadFunctorItem );
}

static void ReleaseCaptureThreadAware()
{
	if ( ( *g_fnHasRenderCallQueue )() )
	{
		CallQueue_s* const queue = ( *g_fnAddRenderCallQueueItem )( ReleaseCaptureThreadAwareFunctor, sizeof( RenderThreadFunctorItem ), 7 );
		RenderThreadFunctorItem* const task = (RenderThreadFunctorItem*)queue->GetCurrentAllocatedItem();
		task->m_pFunc						 = (void*)ReleaseCaptureThreadAware;
		( *g_fnAdvanceRenderCallQueue )( sizeof( RenderThreadFunctorItem ) );
		return;
    }
    else
    {
		ReleaseCapture();
    }
}

static void SetCursorThreadAwareFunctor( CallQueue_s* const queue )
{
	RenderThreadFunctorItem* const task = (RenderThreadFunctorItem*)queue->GetCurrentCallItem();
	reinterpret_cast<HCURSOR ( * )( HCURSOR )>( task->m_pFunc )( (HCURSOR)task->m_Arg );
	queue->currentCallIndex += sizeof( RenderThreadFunctorItem );
}

static HCURSOR SetCursorThreadAware( HCURSOR newCursor )
{
	if ( ( *g_fnHasRenderCallQueue )() )
	{
		CallQueue_s* const queue = ( *g_fnAddRenderCallQueueItem )( SetCursorThreadAwareFunctor, sizeof( RenderThreadFunctorItem ), 7 );
		RenderThreadFunctorItem* const task = (RenderThreadFunctorItem*)queue->GetCurrentAllocatedItem();
		task->m_pFunc						 = (void*)SetCursorThreadAware;
		task->m_Arg							 = (HWND)newCursor;
		( *g_fnAdvanceRenderCallQueue )( sizeof( RenderThreadFunctorItem ) );
		return NULL;
	}
	else
	{
		return v_SetCursor( newCursor );
	}
}

void CInputSystem::EnableMouseCaptureExH( CInputSystem* thisp, HWND hWnd )
{	
    if ( (HWND)thisp->m_hCurrentCaptureWnd == hWnd )
		return;
	
	const bool bActiveWindow = hWnd == *g_phCurrentForegroundWindow;

    if (thisp->m_hCurrentCaptureWnd != PLAT_WINDOW_INVALID || !bActiveWindow)
    {
		ClipCursor( 0 );
		ReleaseCaptureThreadAware();
    }

    thisp->m_hCurrentCaptureWnd = (PlatWindow_t)hWnd;

    if ( !hWnd )
		return;

    if (hWnd && bActiveWindow)
    {
		RECT rect;
		GetWindowRect( hWnd, &rect );
		ClipCursor( &rect );
		SetCaptureThreadAware( hWnd );
    }
}

void CInputSystem::DisableMouseCaptureH( CInputSystem* thisp )
{
	if ( thisp->m_hCurrentCaptureWnd == PLAT_WINDOW_INVALID )
		return;
    
    ClipCursor(0);
	ReleaseCaptureThreadAware();
	thisp->m_hCurrentCaptureWnd = PLAT_WINDOW_INVALID;
}

bool CInputSystem::Connect_( CInputSystem* thisp, const CreateInterfaceFn factory )
{
    s_hRawInputShutdownEvent = CreateEvent( nullptr, TRUE, FALSE, TEXT( "RawInputShutdown" ) );
	s_hRawInputThread = CreateThread( NULL, 0x200000, (LPTHREAD_START_ROUTINE)CInputSystem::RawInputCaptureThread, NULL,
									  STACK_SIZE_PARAM_IS_A_RESERVATION, 0 );

    SetThreadPriority( s_hRawInputThread, THREAD_PRIORITY_HIGHEST );

	return CInputSystem__Connect( thisp, factory );
}

void CInputSystem::GetRawMouseAccumulators_(CInputSystem* thisp, int& accumX, int& accumY)
{
	CMaterialSystem__SyncToMessagePump( g_pMaterialSystem );
    thisp->m_MouseAccum.Consume( accumX, accumY );
}

void CInputSystem__SetMouseCursorVisibleHk(CInputSystem* thisp, bool bVisible)
{
    if (!bVisible && ImguiSystem()->IsSurfaceActive())
    {
		CInputSystem__SetMouseCursorVisible( thisp, true );
		return;
    }

    CInputSystem__SetMouseCursorVisible( thisp, bVisible );
}

void VInputSystem::Detour(const bool bAttach) const
{
    DetourSetup( &CInputSystem__Connect, &CInputSystem::Connect_, bAttach );
	DetourSetup( &CInputSystem__WindowProc, &CInputSystem::WindowProc, bAttach);
	DetourSetup( &CInputSystem__GetRawMouseAccumulators, &CInputSystem::GetRawMouseAccumulators_, bAttach );
	DetourSetup( &CInputSystem__Shutdown, &CInputSystem::Shutdown, bAttach );

    DetourSetup( &CInputSystem__EnableMouseCapture, &CInputSystem::EnableMouseCaptureExH, bAttach );
	DetourSetup( &CInputSystem__DisableMouseCapture, &CInputSystem::DisableMouseCaptureH, bAttach );
	DetourSetup( &CInputSystem__SetMouseCursorVisible, &CInputSystem__SetMouseCursorVisibleHk, bAttach );

	DetourSetup( &v_SetCursor, &SetCursorThreadAware, bAttach );
}

///////////////////////////////////////////////////////////////////////////////
CInputSystem* g_pInputSystem = nullptr;
bool(**g_fnSyncRTWithIn)(void) = nullptr;
