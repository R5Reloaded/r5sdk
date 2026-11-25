#ifndef THREADTOOLS_IFACE_H
#define THREADTOOLS_IFACE_H
#include "threadtools.h"
#include "jobthread.h"

extern ThreadId_t* g_ThreadMainThreadID;
extern ThreadId_t* g_ThreadServerFrameThreadID;
inline JobID_t* g_CurrentServerFrameJobID;
inline JobID_t* g_AllocatedServerFrameJobID;

PLATFORM_INTERFACE bool ThreadInMainThread();
PLATFORM_INTERFACE bool ThreadInServerFrameThread();
PLATFORM_INTERFACE bool ThreadInMainOrServerFrameThread();
PLATFORM_INTERFACE bool ThreadCouldDoServerWork();
PLATFORM_INTERFACE void ThreadJoinServerJob();

///////////////////////////////////////////////////////////////////////////////
class VThreadTools : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogVarAdr("g_ThreadMainThreadID", g_ThreadMainThreadID);
		LogVarAdr("g_ThreadServerFrameThreadID", g_ThreadServerFrameThreadID);
		LogVarAdr("g_CurrentServerFrameJobID", g_CurrentServerFrameJobID);
		LogVarAdr("g_AllocatedServerFrameJobID", g_AllocatedServerFrameJobID);
	}
	virtual void GetFun(void) const { }
	virtual void GetVar(void) const
	{
		Module_FindPattern(g_GameDll, "66 89 54 24 ?? 53 55 56 57 41 54 48 81 EC ?? ?? ?? ??")
			.FindPatternSelf("39 05").ResolveRelativeAddressSelf(2, 6).GetPtr(g_CurrentServerFrameJobID);

		Module_FindPattern(g_GameDll, "48 83 EC 28 FF 15 ?? ?? ?? ?? 8B 0D ?? ?? ?? ??")
			.FindPatternSelf("8B 0D").ResolveRelativeAddressSelf(2, 6).GetPtr(g_AllocatedServerFrameJobID);
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool /*bAttach*/) const { }
};
///////////////////////////////////////////////////////////////////////////////

#endif // THREADTOOLS_IFACE_H