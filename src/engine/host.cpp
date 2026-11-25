//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ========//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "core/stdafx.h"
#include "tier0/frametask.h"
#include "engine/cmd.h"
#include "engine/host.h"
#include "engine/debugoverlay.h"
#ifndef CLIENT_DLL
#include "server/server.h"
#endif // !CLIENT_DLL
#ifndef DEDICATED
#include "client/clientstate.h"
#include "windows/id3dx.h"
#include "geforce/reflex.h"
#include "vgui/vgui_debugpanel.h"
#include "materialsystem/cmaterialsystem.h"
#endif // !DEDICATED

CCommonHostState* g_pCommonHostState = nullptr;

void CCommonHostState::SetWorldModel(model_t* pModel)
{
	if (worldmodel == pModel)
		return;

	worldmodel = pModel;
	if (pModel)
	{
		worldbrush = pModel->brush.pShared;
	}
	else
	{
		worldbrush = NULL;
	}
}

/*
==================
Host_CountRealTimePackets

Counts the number of
packets in non-prescaled
clock frames (does not
count for bots or Terminal
Services environments)
==================
*/
void Host_CountRealTimePackets()
{
	v_Host_CountRealTimePackets();
#ifndef DEDICATED
	GeForce_SetLatencyMarker(D3D11Device(), SIMULATION_START, MaterialSystem()->GetCurrentFrameCount());
#endif // !DEDICATED
}

/*
==================
_Host_RunFrame

Runs all active servers
==================
*/
void _Host_RunFrame(void* unused, const float deltaTime)
{
	for (IFrameTask* const& task : g_TaskQueueList)
	{
		task->RunFrame();
	}

	g_TaskQueueList.erase(std::remove_if(g_TaskQueueList.begin(), g_TaskQueueList.end(), [](const IFrameTask* task)
		{
			return task->IsFinished();
		}), g_TaskQueueList.end());

#ifndef DEDICATED
	g_TextOverlay.ShouldDraw(deltaTime);
#endif // !DEDICATED

#ifdef DEDICATED
	DebugOverlay_HandleDecayed();
#endif // DEDICATED

	v_Host_RunFrame(unused, deltaTime);
}

void Host_Error(const char* const error, ...)
{
	char buf[1024];
	{/////////////////////////////
		va_list args{};
		va_start(args, error);

		const int ret = V_vsnprintf(buf, sizeof(buf), error, args);

		if (ret < 0)
			buf[0] = '\0';

		va_end(args);
	}/////////////////////////////

	Error(eDLL_T::ENGINE, NO_ERROR, "Host_Error: %s", buf);
	v_Host_Error(buf);
}

void Host_ReparseAllScripts()
{
	// NOTE: the following are already called during "reload" or "reconnect".
	//"aisettings_reparse"
	//"aisettings_reparse_client"

	//"damagedefs_reparse"
	//"damagedefs_reparse_client"

	//"playerSettings_reparse"
	//"fx_impact_reparse"

#ifndef DEDICATED
	// Reparse banks.rson
	Cbuf_AddText(Cbuf_GetCurrentPlayer(), "miles_reboot", cmd_source_t::kCommandSrcCode);
#endif // !DEDICATED

	Cbuf_AddText(Cbuf_GetCurrentPlayer(), "downloadPlaylists", cmd_source_t::kCommandSrcCode);
	Cbuf_AddText(Cbuf_GetCurrentPlayer(), "banlist_reload", cmd_source_t::kCommandSrcCode);

	Cbuf_AddText(Cbuf_GetCurrentPlayer(), "ReloadAimAssistSettings", cmd_source_t::kCommandSrcCode);
	Cbuf_AddText(Cbuf_GetCurrentPlayer(), "reload_localization", cmd_source_t::kCommandSrcCode);

	Cbuf_AddText(Cbuf_GetCurrentPlayer(), "weapon_reparse", cmd_source_t::kCommandSrcCode);

#ifndef DEDICATED
	// Recompile all UI scripts
	Cbuf_AddText(Cbuf_GetCurrentPlayer(), "uiscript_reset", cmd_source_t::kCommandSrcCode);
#endif // !DEDICATED

	bool serverActive = false;

#ifndef CLIENT_DLL
	if (g_pServer->IsActive())
	{
		// If we hit this code path, we are the server (or the listen server),
		// reload it to recompile all scripts.
		Cbuf_AddText(Cbuf_GetCurrentPlayer(), "reload", cmd_source_t::kCommandSrcCode);
		serverActive = true;
	}
#endif // !CLIENT_DLL
#ifndef DEDICATED
	if (!serverActive && g_pClientState->IsActive())
	{
		// If we hit this code path, we are connected to a remote server,
		// reconnect to it to recompile all client side scripts.
		Cbuf_AddText(Cbuf_GetCurrentPlayer(), "reconnect", cmd_source_t::kCommandSrcCode);
	}
#endif // !DEDICATED

	Cbuf_Execute();
}

//-----------------------------------------------------------------------------
// Purpose: counts ConVars matching the specified flags
//-----------------------------------------------------------------------------
int Host_CountVariablesWithFlags(const int flags, const bool nonDefault)
{
	int count = 0;

	ICvar::Iterator iter(g_pCVar);

	for (iter.SetFirst(); iter.IsValid(); iter.Next())
	{
		ConCommandBase* const commandBase = iter.Get();

		if (commandBase->IsCommand())
			continue;

		const ConVar* const var = static_cast<const ConVar*>(commandBase);

		if (!var->IsFlagSet(flags))
			continue;

		if (nonDefault && !V_stricmp(var->GetDefault(), var->GetString()))
			continue;

		++count;
	}

	return count;
}

static bool AppearsNumeric(char const* in)
{
	char const* p = in;
	int special[3];
	memset(special, 0, sizeof(special));

	for (; *p; p++)
	{
		if (*p == '-')
		{
			special[0]++;
			continue;
		}

		if (*p == '+')
		{
			special[1]++;
			continue;
		}

		if (*p >= '0' && *p <= '9')
		{
			continue;
		}

		if (*p == '.')
		{
			special[2]++;
			continue;
		}

		return false;
	}

	// Can't have multiple +, -, or decimals
	for (int i = 0; i < 3; i++)
	{
		if (special[i] > 1)
			return false;
	}

	// can't be + and - at same time
	if (special[0] && special[1])
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: if the value is numeric, remove unnecessary trailing zeros
// Input  : *inValue -
// Output : const char*
//-----------------------------------------------------------------------------
const char* Host_CleanupConVarStringValue(const char* const inValue)
{
	static char clean[256];
	V_snprintf(clean, sizeof(clean), "%s", inValue);

	// Don't mess with empty string
	// Otherwise, if it appears numeric and has a decimal, try to strip all zeroes after decimal
	if (V_strlen(clean) >= 1 && AppearsNumeric(clean) && V_strstr(clean, "."))
	{
		char* end = clean + V_strlen(clean) - 1;

		while (*end && end >= clean)
		{
			// Removing trailing zeros
			if (*end != '0')
			{
				// Remove decimal, zoo
				if (*end == '.')
					*end = end == clean ? '0' : '\0';

				break;
			}

			*end-- = '\0';
		}
	}

	return clean;
}

//-----------------------------------------------------------------------------
// Purpose: builds a list of userinfo or platform ConVars
// Input  : splitScreenSlot - 
//          *cvars          - 
//          nonDefault      - 
//          platformSystem  - build platform ConVars instead of userinfo
//-----------------------------------------------------------------------------
void Host_BuildUserInfoUpdateMessage(const int splitScreenSlot,
									 NET_SetConVar::Vec_t* const cvars,
									 const bool nonDefault,
									 const bool platformSystem)
{
	// Slot 0 does the easy version, all userinfo/platform system, except _ADDED ones
	int requiredFlags = platformSystem ? FCVAR_PLATFORM_SYSTEM : FCVAR_USERINFO;
	int disallowFlag = FCVAR_SS_ADDED;

	if (splitScreenSlot != 0)
	{
		// Slot one only looks at added ones
		disallowFlag = FCVAR_SS;
		requiredFlags = FCVAR_USERINFO | FCVAR_SS_ADDED;
	}

	// Nothing to send.
	if (Host_CountVariablesWithFlags(requiredFlags, nonDefault) <= 0)
		return;

	ICvar::Iterator iter(g_pCVar);

	for (iter.SetFirst(); iter.IsValid(); iter.Next())
	{
		ConCommandBase* const commandBase = iter.Get();

		if (commandBase->IsCommand())
			continue;

		const ConVar* const var = static_cast<const ConVar*>(commandBase);

		if (!var->IsFlagSet(requiredFlags))
			continue;

		if (var->IsFlagSet(disallowFlag))
			continue;

		// It's == to the default value, don't count
		if (nonDefault && !Q_strcasecmp(var->GetDefault(), var->GetString()))
			continue;

		if (var->GetSplitScreenPlayerSlot() != splitScreenSlot)
			continue;

		const int idx = cvars->AddToTail();
		NET_SetConVar::Cvar_s& entry = cvars->Element(idx);

		V_strncpy(entry.name, var->GetBaseName(), sizeof(entry.name));
		entry.name[sizeof(entry.name) - 1] = '\0';

		const char* const value = Host_CleanupConVarStringValue(var->GetString());

		V_strncpy(entry.value, value, sizeof(entry.value));
		entry.value[sizeof(entry.value) - 1] = '\0';
	}

	// Too many to send, error out and have mod author get a clue.
	if (cvars->Count() > 255)
	{
		Error(eDLL_T::ENGINE, EXIT_FAILURE, "Engine only supports 255 ConVars marked %i\n", FCVAR_USERINFO);
	}
}

///////////////////////////////////////////////////////////////////////////////
void VHost::Detour(const bool bAttach) const
{
	DetourSetup(&v_Host_RunFrame, &_Host_RunFrame, bAttach);
	DetourSetup(&v_Host_CountRealTimePackets, &Host_CountRealTimePackets, bAttach);

#ifndef DEDICATED // Dedicated already logs this!
	DetourSetup(&v_Host_Error, &Host_Error, bAttach);
#endif // !DEDICATED
}
