#pragma once
#include "inputsystem/iinputsystem.h"
#include "mathlib/bitvec.h"
#include "tier1/utlstringmap.h"
#include <Xinput.h>

//-----------------------------------------------------------------------------
// Implementation of the input system
//-----------------------------------------------------------------------------
class CInputSystem : public CTier1AppSystem< IInputSystem >
{
public:
	// !!!interface implemented in engine!!!
public:
	PlatWindow_t GetAttachedWindow() const;
    void RecordRawInputMouseMove( const PRAWINPUT pInputEvents, const size_t nEvents );

public:
	// Hook statics:
	static void			Shutdown( CInputSystem* thisp );
	static LRESULT WindowProc(void* unused, HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	static unsigned int RawInputCaptureThread( LPVOID lpThreadParam );
	static bool			Connect_( CInputSystem* thisp, const CreateInterfaceFn factory );
    static void         GetRawMouseAccumulators_( CInputSystem* thisp, int& accumX, int& accumY );
	static void			EnableMouseCaptureExH( CInputSystem* thisp, HWND hWnd );
	static void			DisableMouseCaptureH( CInputSystem* thisp );
    
    static HANDLE s_hRawInputThread;
	static HANDLE s_hRawInputShutdownEvent;

    struct MouseAccumulator_t
    {
        void Accumulate(const int x, const int y)
		{
			volatile LONG64*    pData   = reinterpret_cast<volatile LONG64*>( &m_Accumulator );
			LONG64		        expected;

            ALIGN8 CombinedAccumulator current; ALIGN8_POST
			ALIGN8 CombinedAccumulator next; ALIGN8_POST
			current.m_Combined = *pData;
    
            do
			{   
                next.m_Split.x = current.m_Split.x + x;
				next.m_Split.y = current.m_Split.y + y;

                expected = current.m_Combined;
				current.m_Combined = ThreadInterlockedCompareExchange64( pData, next.m_Combined, current.m_Combined );
            } while ( current.m_Combined != expected );
        }

        void Set(const int x, const int y)
        { 
            ALIGN8 CombinedAccumulator points ALIGN8_POST;
			points.m_Split.x = x;
			points.m_Split.y = y;

            ThreadInterlockedExchange64( reinterpret_cast<volatile int64*>( &m_Accumulator ), points.m_Combined );
        }

        void Zero() { Set( 0, 0 ); }

        void Consume(int& x, int& y)    
        { 
           ALIGN8 CombinedAccumulator old ALIGN8_POST;
			old.m_Combined = ThreadInterlockedExchange64( reinterpret_cast<volatile int64*>( &m_Accumulator ), 0 );
		   x			  = old.m_Split.x;
		   y			  = old.m_Split.y;
        }

        ALIGN8 union CombinedAccumulator
        {
			LONG64 m_Combined;
			struct
			{
				int x;
				int y;
			} m_Split;
		} m_Accumulator ALIGN8_POST;
    };

private:
	enum
	{
		INPUT_STATE_QUEUED = 0,
		INPUT_STATE_CURRENT,

		INPUT_STATE_COUNT,

		BUTTON_EVENT_COUNT = 128
	};

	struct xdevice_t
	{
		struct xvibration_t
		{
			float leftMainMotor;
			float rightMainMotor;
			float leftTriggerMotor;
			float rightTriggerMotor;
		};

		struct unkownhiddevice_t
		{
			struct state_t
			{
				SRWLOCK lock;
				char unk0[56];
				xvibration_t vibration;
				char unk1[48];
			};

			// Name might be incorrect!
			state_t states[INPUT_STATE_COUNT];
			HANDLE hThread0;
			HANDLE hthread1;
		};

		int userId;
		char active;
		XINPUT_STATE states[INPUT_STATE_COUNT];
		int newState;
		xKey_t lastStickKeys[MAX_JOYSTICK_AXES-2]; // -2 as U and V aren't polled.
		int unk0;
		bool pendingRumbleUpdate;
		_BYTE gap41[3];
		xvibration_t vibration;
		bool isXbox360Gamepad;
		bool nonXboxDevice; // uses unknownHidDevice when set
		_BYTE gap56[42];
		unkownhiddevice_t unknownHidDevice;
		_BYTE gap190[42];
	};
	static_assert(sizeof(xdevice_t) == 0x1C0);

	struct appKey_t
	{
		int repeats;
		int	sample;
	};

	struct InputState_t
	{
		// Analog states
		CBitVec<BUTTON_CODE_LAST> m_ButtonState;
		int m_pAnalogValue[ANALOG_CODE_LAST];
	};


	HWND m_ChainedWndProc;
	HWND m_hAttachedHWnd;
	bool m_bEnabled;
	bool m_bPumpEnabled;
	bool m_bIsPolling;
	bool m_bIMEComposing;
	bool m_bMouseCursorVisible;
	bool m_bJoystickCursorVisible;
	bool m_bIsInGame; // Delay joystick polling if in-game.

	// Current button state
	InputState_t m_InputState[INPUT_STATE_COUNT];

	// Current button state mutex
	CThreadMutex m_InputStateMutex;
	int unknown0;
	short unknown1;
	bool unknown2;

	// Analog event mutex
	CThreadMutex m_AnalogEventMutex;
	int unknown3;
	short unknown4;
	bool unknown5;

	// Analog events
	InputEvent_t m_AnalogEvents[JOYSTICK_AXIS_BUTTON_COUNT];
	int m_AnalogEventTypes[JOYSTICK_AXIS_BUTTON_COUNT];

	// Button events
	InputEvent_t m_Events[BUTTON_EVENT_COUNT];

	// Current event
	InputEvent_t m_CurrentEvent;

	DWORD m_StartupTimeTick;
	int m_nLastPollTick;
	int m_nLastSampleTick;
	int m_nLastAnalogPollTick;
	int m_nLastAnalogSampleTick;

	// Mouse wheel hack
	UINT m_uiMouseWheel;

	// Xbox controller info
	int m_nJoystickCount;
	appKey_t m_appXKeys[XUSER_MAX_COUNT][XK_MAX_KEYS];
	char pad_unk[16];
	xdevice_t m_XDevices[XUSER_MAX_COUNT];

	// Used to determine whether to generate UI events
	int m_nUIEventClientCount;

	// Raw mouse input
	bool m_bRawInputSupported;
	CThreadMutex m_MouseAccumMutex;
	
    MouseAccumulator_t m_MouseAccum;
    
    //int m_mouseRawAccumX;
	//int m_mouseRawAccumY;

	_BYTE gap1785[8];

	// Current mouse capture window
	PlatWindow_t m_hCurrentCaptureWnd;

	// For the 'SleepUntilInput' feature
	HANDLE m_hEvent;

	InputCursorHandle_t m_pDefaultCursors[INPUT_CURSOR_COUNT];
	CUtlStringMap<InputCursorHandle_t> m_UserCursors;

	CSysModule* m_pXInputDLL;
	CSysModule* m_pRawInputDLL;

	// NVNT falcon module
	CSysModule* m_pNovintDLL; // Unused in R5?

	bool m_bIgnoreLocalJoystick;
	InputCursorHandle_t m_hCursor;
};
static_assert(sizeof(CInputSystem) == 0x18E8);

///////////////////////////////////////////////////////////////////////////////
inline LRESULT (*CInputSystem__WindowProc)(void* thisptr, HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
inline bool ( *CInputSystem__Connect )( CInputSystem* thisp, const CreateInterfaceFn factory );
inline void ( *CInputSystem__AttachToWindow )( CInputSystem* thisp, HWND hWnd );
inline void ( *CInputSystem__GetRawMouseAccumulators )( CInputSystem* thisp, int& accumX, int& accumY );
inline void ( *CInputSystem__EnableMouseCapture )( CInputSystem* thisp, HWND hWnd );
inline void ( *CInputSystem__DisableMouseCapture )( CInputSystem* thisp );
inline void ( *CInputSystem__Shutdown )( CInputSystem* thisp );
inline void ( *CInputSystem__SetMouseCursorVisible )( CInputSystem* thisp, bool bVisible );
inline HCURSOR ( *v_SetCursor )( HCURSOR hCursor );

inline HWND* g_phCurrentForegroundWindow;

extern CInputSystem* g_pInputSystem;
extern bool(**g_fnSyncRTWithIn)(void); // Belongs to an array of functions, see CMaterialSystem::MatsysMode_Init().

///////////////////////////////////////////////////////////////////////////////
class VInputSystem : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr( "CInputSystem::Connect", CInputSystem__Connect );
		LogFunAdr("CInputSystem::WindowProc", CInputSystem__WindowProc);
		LogFunAdr( "CInputSystem::AttachToWindow", CInputSystem__AttachToWindow );
		LogFunAdr( "CInputSystem::GetRawMouseAccumulators", CInputSystem__GetRawMouseAccumulators );
		LogFunAdr( "CInputSystem::EnableMouseCapture", CInputSystem__EnableMouseCapture );
		LogFunAdr( "CInputSystem::DisableMouseCapture", CInputSystem__DisableMouseCapture );
		LogFunAdr( "CInputSystem::Shutdown", CInputSystem__Shutdown );
		LogVarAdr("g_pInputSystem", g_pInputSystem);
		LogVarAdr("g_fnSyncRTWithIn", g_fnSyncRTWithIn);
		LogVarAdr( "g_phCurrentForegroundWindow", g_phCurrentForegroundWindow );
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 89 4C 24 ?? 55 56 41 54 41 55 48 83 EC 48").GetPtr(CInputSystem__WindowProc);
		Module_FindPattern( g_GameDll, "48 89 5C 24 ?? 57 48 83 EC ?? 48 83 79 ?? ?? 48 8B FA 48 8B D9 0F 85" )
			.GetPtr( CInputSystem__AttachToWindow );

        Module_FindPattern( g_GameDll, "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 48 8B E9 49 8B F0 48 8B 0D" )
			.GetPtr( CInputSystem__GetRawMouseAccumulators );

        Module_FindPattern( g_GameDll, "48 89 5C 24 ?? 56 48 83 EC ?? 48 8B 81 ?? ?? ?? ?? 48 8B DA" )
			.GetPtr( CInputSystem__EnableMouseCapture );

        Module_FindPattern( g_GameDll, "40 53 48 83 EC ?? 48 83 B9 ?? ?? ?? ?? ?? 48 8B D9 74 ?? 33 C9" )
            .GetPtr( CInputSystem__DisableMouseCapture );

        Module_FindPattern( g_GameDll, "48 83 EC ?? 48 89 5C 24 ?? 48 8D 99" ).GetPtr( CInputSystem__Shutdown );
		Module_FindPattern( g_GameDll, "88 51 1C" ).GetPtr( CInputSystem__SetMouseCursorVisible );
		Module_FindPattern( g_GameDll,
							"48 8B 05 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? 48 85 C0 48 0F 45 C8 FF 05 ?? ?? ?? ?? 48 89 0D ?? ?? ?? ?? B0" )
			.GetPtr( CInputSystem__Connect );

        g_GameDll.GetImportedSymbol( "user32.dll", "SetCursor", false ).GetPtr( v_SetCursor );
		
        CMemory( CInputSystem__AttachToWindow )
			.OffsetSelf( 0xA7 )
			.FindPatternSelf( "FF 15 12" )
			.Patch( { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } );
    }
	virtual void GetVar(void) const
	{
		g_pInputSystem = Module_FindPattern(g_GameDll, "48 83 EC 28 48 8B 0D ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 48 85 C9 74 11")
			.FindPatternSelf("48 89 05", CMemory::Direction::DOWN, 40).ResolveRelativeAddressSelf(0x3, 0x7).RCast<CInputSystem*>();

		const CMemory l_EngineApi_PumpMessages = Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 55 48 81 EC ?? ?? ?? ?? 45 33 C9");
		g_fnSyncRTWithIn = l_EngineApi_PumpMessages.FindPattern("74 06").FindPatternSelf("FF 15").ResolveRelativeAddressSelf(2, 6).RCast<bool(**)(void)>();

        Module_FindPattern( g_GameDll, "48 89 05 ?? ?? ?? ?? 75 ?? B9" ).ResolveRelativeAddressSelf(0x3, 0x7).GetPtr( g_phCurrentForegroundWindow );
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
