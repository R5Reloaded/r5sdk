#pragma once
#include "common/netmessages.h"
#include "engine/gl_model_private.h"

inline void(*v_Host_RunFrame)(void* unused, float time);
inline void(*v_Host_RunFrame_Render)(void);
inline void(*v_Host_CountRealTimePackets)(void);
inline bool(*v_Host_IsPersistenceDataDefValid)(void);
inline bool(*v_Host_IsInLowViolenceMode)(void);
inline bool(*v_Host_ShouldRun)();
inline void(*v_Host_Error)(const char* error, ...);
//inline void(*v_VCR_EnterPausedState)(void);

inline bool* g_bAbortServerSet = nullptr;
inline bool* g_bDedicatedServerBenchmarkMode = nullptr;

inline jmp_buf* host_abortserver = nullptr;
inline bool* host_initialized = nullptr;
inline float* host_remainder = nullptr;
inline float* host_frametime = nullptr;
inline float* host_frametime_unbounded = nullptr;
inline float* host_frametime_unscaled = nullptr;
inline float* host_frametime_stddeviation = nullptr;

// PERFORMANCE INFO
#define MIN_FPS         0.1f         // Host minimum fps value for maxfps.
#define MAX_FPS         300.0f       // Upper limit for maxfps.
#define MIN_FRAMETIME   0.001
#define MAX_FRAMETIME   0.1

void Host_Error(const char* const error, ...);
void Host_ReparseAllScripts();

class CCommonHostState
{
public:
	CCommonHostState()
		: worldmodel(NULL)
		, worldbrush(NULL)
		, interval_per_tick(0.0f)
		, max_splitscreen_players(1)
		, max_splitscreen_players_clientdll(1)
	{}

	// cl_entitites[0].model
	model_t* worldmodel;
	struct worldbrushdata_t* worldbrush;
	// Tick interval for game
	float					interval_per_tick;
	// 1, unless a game supports split screen, then probably 2 or 4 (4 is the max allowable)
	int						max_splitscreen_players;
	// This is the # the client .dll thinks is the max, it might be > max_splitscreen_players in -tools mode, etc.
	int						max_splitscreen_players_clientdll;
	void					SetWorldModel(model_t* pModel);
};

extern CCommonHostState* g_pCommonHostState;

#define HOST_TIME_TO_TICKS( dt )		( (int)( 0.5f + (float)(dt) / g_pCommonHostState->interval_per_tick ) )
#define HOST_TICKS_TO_TIME( dt )		( g_pCommonHostState->interval_per_tick * (float)(dt) )

int Host_CountVariablesWithFlags(const int flags, const bool nonDefault);
const char* Host_CleanupConVarStringValue(const char* const inValue);
void Host_BuildUserInfoUpdateMessage(const int splitScreenSlot,
									 NET_SetConVar::Vec_t* const cvars,
									 const bool nonDefault,
									 const bool platformSystem);

///////////////////////////////////////////////////////////////////////////////
class VHost : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("_Host_RunFrame", v_Host_RunFrame);
		LogFunAdr("_Host_RunFrame_Render", v_Host_RunFrame_Render);
		LogFunAdr("Host_CountRealTimePackets", v_Host_CountRealTimePackets);
		LogFunAdr("Host_IsPersistenceDataDefValid", v_Host_IsPersistenceDataDefValid);
		LogFunAdr("Host_IsInLowViolenceMode", v_Host_IsInLowViolenceMode);
		LogFunAdr("Host_ShouldRun", v_Host_ShouldRun);
		LogFunAdr("Host_Error", v_Host_Error);
		//LogFunAdr("VCR_EnterPausedState", v_VCR_EnterPausedState);
		LogVarAdr("g_CommonHostState", g_pCommonHostState);
		LogVarAdr("g_bAbortServerSet", g_bAbortServerSet);
		LogVarAdr("g_bDedicatedServerBenchmarkMode", g_bDedicatedServerBenchmarkMode);
		LogVarAdr("host_abortserver", host_abortserver);
		LogVarAdr("host_initialized", host_initialized);
		LogVarAdr("host_remainder", host_remainder);
		LogVarAdr("host_frametime", host_frametime);
		LogVarAdr("host_frametime_unbounded", host_frametime_unbounded);
		LogVarAdr("host_frametime_unscaled", host_frametime_unscaled);
		LogVarAdr("host_frametime_stddeviation", host_frametime_stddeviation);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 8B C4 48 89 58 18 48 89 70 20 F3 0F 11 48 ??").GetPtr(v_Host_RunFrame);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC 20 48 8B 0D ?? ?? ?? ?? 48 85 C9 75 34").GetPtr(v_Host_RunFrame_Render);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC 30 65 48 8B 04 25 ?? ?? ?? ?? 33 DB").GetPtr(v_Host_CountRealTimePackets);
		Module_FindPattern(g_GameDll, "0F B6 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC 40 53 48 83 EC ?? 48 8B CA").GetPtr(v_Host_IsPersistenceDataDefValid);
		Module_FindPattern(g_GameDll, "0F B6 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC 48 8B 05 ?? ?? ?? ?? 48 8D 0D").GetPtr(v_Host_IsInLowViolenceMode);
		Module_FindPattern(g_GameDll, "48 83 EC 28 48 8B 05 ?? ?? ?? ?? 83 78 6C 00 75 07 B0 01").GetPtr(v_Host_ShouldRun);
		Module_FindPattern(g_GameDll, "48 89 4C 24 ?? 48 89 54 24 ?? 4C 89 44 24 ?? 4C 89 4C 24 ?? 53 57 48 81 EC ?? ?? ?? ??").GetPtr(v_Host_Error);
		//Module_FindPattern(g_GameDll, "40 53 48 83 EC 20 65 48 8B 04 25 ?? ?? ?? ?? BB ?? ?? ?? ?? C6 05 ?? ?? ?? ?? ??").GetPtr(v_VCR_EnterPausedState);
	}
	virtual void GetVar(void) const
	{
		g_pCommonHostState = Module_FindPattern(g_GameDll, "48 83 EC 28 84 C9 75 0B")
			.FindPatternSelf("48 8B 15").ResolveRelativeAddressSelf(0x3, 0x7).RCast<CCommonHostState*>();

		const CMemory hostErrorBase(v_Host_Error);

		g_bAbortServerSet = hostErrorBase.FindPattern("40 38 3D", CMemory::Direction::DOWN, 512, 4).ResolveRelativeAddress(3, 7).RCast<bool*>();
		host_abortserver = hostErrorBase.FindPattern("48 8D 0D", CMemory::Direction::DOWN, 512, 5).ResolveRelativeAddress(3, 7).RCast<jmp_buf*>();

		const CMemory hostRunFrameBase(v_Host_RunFrame);

		host_initialized = hostRunFrameBase.Offset(0x500).FindPatternSelf("44 38").ResolveRelativeAddressSelf(0x3, 0x7).RCast<bool*>();
		host_remainder = hostRunFrameBase.Offset(0x33F).FindPatternSelf("F3 0F 10").ResolveRelativeAddressSelf(0x4, 0x8).RCast<float*>();
		host_frametime = hostRunFrameBase.Offset(0x1F4).FindPatternSelf("F3 0F 11").ResolveRelativeAddressSelf(0x4, 0x8).RCast<float*>();
		host_frametime_unbounded = hostRunFrameBase.Offset(0x330).FindPatternSelf("F3 0F 11").ResolveRelativeAddressSelf(0x4, 0x8).RCast<float*>();
		host_frametime_unscaled = hostRunFrameBase.Offset(0x2F4).FindPatternSelf("F3 0F 11").ResolveRelativeAddressSelf(0x4, 0x8).RCast<float*>();
		host_frametime_stddeviation = hostRunFrameBase.Offset(0xFAA).FindPatternSelf("F3 0F 11").ResolveRelativeAddressSelf(0x4, 0x8).RCast<float*>();
		g_bDedicatedServerBenchmarkMode = hostRunFrameBase.Offset(0x1C4).FindPatternSelf("38 05").ResolveRelativeAddressSelf(0x2, 0x6).RCast<bool*>();
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
