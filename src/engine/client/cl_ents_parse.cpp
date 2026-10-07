//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: Parsing of entity network packets.
//
// $NoKeywords: $
//=============================================================================//
#include "core/stdafx.h"
#include "public/const.h"
#include "engine/host.h"
#include "engine/client/cl_ents_parse.h"
#include "clientstate.h"

static void CL_ProcessSnapshot_WT(SnapshotData_Async* const asyncSnap)
{
	v_CL_ProcessSnapshot_WT(asyncSnap);

	CNetChan* const chan = g_pClientState->GetNetChannel();
	Assert(chan);

	const nettick_t& netTick = asyncSnap->record.tick_update;

	chan->SetRemoteFramerate(netTick.m_flHostFrameTime, netTick.m_flHostFrameTimeStdDeviation);
	chan->SetRemoteCPUStatistics(netTick.m_nServerCPU);
}

bool CL_CopyNewEntity(CEntityReadInfo* const u, unsigned int* const iClass, const int iSerialNum, bool* const pbError)
{
	// Similar to the issue in CL_CopyExistingEntity,
	// except, only the lower bounds check was missing.
	if (u->m_nNewEntity < NULL || u->m_nNewEntity >= MAX_EDICTS)
	{
		Host_Error("CL_CopyNewEntity: u.m_nNewEntity < 0 || u.m_nNewEntity >= MAX_EDICTS");
		*pbError = true;

		return false;
	}

	return v_CL_CopyNewEntity(u, iClass, iSerialNum, pbError);
}

bool CL_CopyExistingEntity(CEntityReadInfo* const u, unsigned int* const iClass, bool* const pbError)
{
	if (u->m_nNewEntity < NULL || u->m_nNewEntity >= MAX_EDICTS)
	{
		// Value isn't sanitized in release builds for
		// every game powered by the Source Engine 1
		// causing read/write outside of array bounds.
		// This defect has let to the achievement of a
		// full-chain RCE exploit. We hook and perform
		// sanity checks for the value of m_nNewEntity
		// here to prevent this behavior from happening.
		Host_Error("CL_CopyExistingEntity: u.m_nNewEntity < 0 || u.m_nNewEntity >= MAX_EDICTS");
		*pbError = true;

		return false;
	}

	return v_CL_CopyExistingEntity(u, iClass, pbError);
}

void V_CL_Ents_Parse::Detour(const bool bAttach) const
{
	DetourSetup(&v_CL_ProcessSnapshot_WT, &CL_ProcessSnapshot_WT, bAttach);
	DetourSetup(&v_CL_CopyNewEntity, &CL_CopyNewEntity, bAttach);
	DetourSetup(&v_CL_CopyExistingEntity, &CL_CopyExistingEntity, bAttach);
}
