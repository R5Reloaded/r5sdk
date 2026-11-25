//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Thread tools
//
// $Workfile: $
// $NoKeywords: $
//===========================================================================//

#include "tier0/threadtools.h"
#include "tier0/threadtools_iface.h"
#include "tier0/jobthread.h"

bool ThreadInMainThread()
{
	return (ThreadGetCurrentId() == (*g_ThreadMainThreadID));
}

bool ThreadInServerFrameThread()
{
	return (ThreadGetCurrentId() == (*g_ThreadServerFrameThreadID) 
		&& JT_GetCurrentJob() == (*g_CurrentServerFrameJobID));
}

bool ThreadInMainOrServerFrameThread()
{
	return (ThreadInMainThread() || ThreadInServerFrameThread());
}

bool ThreadCouldDoServerWork()
{
	if (*g_ThreadServerFrameThreadID == -1)
		return ThreadInMainThread();

	return ThreadInServerFrameThread();
}

void ThreadJoinServerJob()
{
	if (ThreadCouldDoServerWork())
		return; // No job to join

	if (*g_AllocatedServerFrameJobID)
	{
		JT_WaitForJobAndOnlyHelpWithJobTypes(*g_AllocatedServerFrameJobID, NULL, 0xFFFFFFFFFFFFFFFF);
		*g_AllocatedServerFrameJobID = 0;
	}
}

// NOTE: originally the game exported 'ThreadInMainThread()' and ThreadInServerFrameThread(),
// but since the game is built static, and all instances of said functions are inline, we had
// to export the variable symbols instead and get them here to reimplement said functions.
ThreadId_t* g_ThreadMainThreadID = CModule::GetExportedSymbol(CModule::GetProcessEnvironmentBlock()->ImageBaseAddress, "g_ThreadMainThreadID").RCast<ThreadId_t*>();
ThreadId_t* g_ThreadServerFrameThreadID = CModule::GetExportedSymbol(CModule::GetProcessEnvironmentBlock()->ImageBaseAddress, "g_ThreadServerFrameThreadID").RCast<ThreadId_t*>();
