//=============================================================================//
//
// Purpose: Implement things from GameInterface.cpp. Mostly the engine interfaces.
//
// $NoKeywords: $
//=============================================================================//

#include "core/stdafx.h"
#include "tier1/cvar.h"
#include "public/server_class.h"
#include "public/eiface.h"
#include "public/const.h"
#include "common/protocol.h"
#include "common/callback.h"
#include "rtech/liveapi/liveapi.h"
#include "engine/server/sv_main.h"
#include "gameinterface.h"
#include "entitylist.h"
#include "baseanimating.h"
#include "engine/server/server.h"
#include "game/shared/usercmd.h"
#include "game/server/util_server.h"
#include "pluginsystem/pluginsystem.h"
#include "game/server/recipientfilter.h"

//-----------------------------------------------------------------------------
// Purpose: retrieves the index of the client that issued the last command
// Output : int
//-----------------------------------------------------------------------------
int UTIL_GetCommandClientIndex(void)
{
	// -1 == unknown,dedicated server console
	// 0  == player 1

	// Convert to 1 based offset
	return (*g_nCommandClientIndex)+1;
}

//-----------------------------------------------------------------------------
// Purpose: retrieves the player of the client that issued the last command
// Output : CPlayer*
//-----------------------------------------------------------------------------
CPlayer* UTIL_GetCommandClient(void)
{
	const int idx = UTIL_GetCommandClientIndex();
	if (idx > 0)
	{
		CPlayer* const player = UTIL_PlayerByIndex(idx);

		if (!player || !player->IsConnected())
			return NULL;

		return player;
	}

	// HLDS console issued command
	return NULL;
}

bool CServerGameDLL::DLLInit(CServerGameDLL* thisptr, CreateInterfaceFn appSystemFactory, CreateInterfaceFn physicsFactory,
	CreateInterfaceFn fileSystemFactory, CGlobalVars* pGlobals)
{
	gpGlobals = pGlobals;
	return CServerGameDLL__DLLInit(thisptr, appSystemFactory, physicsFactory, fileSystemFactory, pGlobals);
}

//-----------------------------------------------------------------------------
// This is called when a new game is started. (restart, map)
//-----------------------------------------------------------------------------
bool CServerGameDLL::GameInit(void)
{
	const static int index = 1;
	return CallVFunc<bool>(index, this);
}

//-----------------------------------------------------------------------------
// This is called when scripts are getting recompiled. (restart, map, changelevel)
//-----------------------------------------------------------------------------
void CServerGameDLL::PrecompileScriptsJob(void)
{
	const static int index = 2;
	CallVFunc<void>(index, this);
}

//-----------------------------------------------------------------------------
// Called when a level is shutdown (including changing levels)
//-----------------------------------------------------------------------------
void CServerGameDLL::LevelShutdown(void)
{
	const static int index = 8;
	CallVFunc<void>(index, this);
}

//-----------------------------------------------------------------------------
// This is called when a game ends (server disconnect, death, restart, load)
// NOT on level transitions within a game
//-----------------------------------------------------------------------------
void CServerGameDLL::GameShutdown(void)
{
	// Game just calls a nullsub for GameShutdown lol.
	const static int index = 9;
	CallVFunc<void>(index, this);
}

//-----------------------------------------------------------------------------
// Purpose: Gets the simulation tick interval
// Output : float
//-----------------------------------------------------------------------------
float CServerGameDLL::GetTickInterval(void)
{
	const static int index = 11;
	return CallVFunc<float>(index, this);
}

//-----------------------------------------------------------------------------
// Purpose: get all server classes
// Output : ServerClass*
//-----------------------------------------------------------------------------
ServerClass* CServerGameDLL::GetAllServerClasses(void)
{
	const static int index = 12;
	return CallVFunc<ServerClass*>(index, this);
}

static ConVar chat_debug("chat_debug", "0", FCVAR_RELEASE, "Enables chat-related debug printing.");
static ConVar sv_overrideTeamChatRestriction("sv_overrideTeamChatRestriction", "0", FCVAR_RELEASE,
	"When enabled this allows sv_forceChatToTeamOnly to take control of the team chat restriction.",
	"0: Default, 1: Forces the value from sv_forceChatToTeamOnly."
);

void CServerGameDLL::OnReceivedSayTextMessage(CServerGameDLL* thisptr, int senderId, const char* text, bool isTeamChat)
{
	CPlayer* const pSenderPlayer = UTIL_PlayerByIndex(senderId);
	CClient* const pSenderClient = g_pServer->GetClient(senderId - 1);

	if (!pSenderPlayer || !pSenderClient ||  !pSenderPlayer->IsConnected())
		return;

	const bool bIsTeamChat = sv_overrideTeamChatRestriction.GetBool() ? sv_forceChatToTeamOnly->GetBool()  : isTeamChat;
	const int nMaxClients = gpGlobals->maxClients;
	const bool bShouldApplyGlobalCommsMutes = SV_ShouldApplyTextChatGlobalMutes();

	pSenderPlayer->UpdateLastActiveTime(gpGlobals->curTime);
	
	const bool bSenderIsCommsBanned = pSenderClient->GetClientExtended()->IsClientCommsBanned();

	if (bShouldApplyGlobalCommsMutes && bSenderIsCommsBanned)
	{
		if (chat_debug.GetBool())
			Msg(eDLL_T::SERVER, "Dropping chat message from '%s' (%llu) User is globally muted", 
				pSenderPlayer->GetNetName(), pSenderPlayer->GetPlatformUserId());

		CSingleUserRecipientFilter filter(pSenderPlayer);
		filter.MakeReliable();

		v_UserMessageBegin(&filter, "SayText", 2);

		MessageWriteByte(pSenderPlayer->GetEdict());
		MessageWriteString(pSenderClient->GetClientExtended()->GetCommsMuteDisplayMessage());
		MessageWriteBool(bIsTeamChat);

		MessageEnd();
		return;
	}

	for (auto& cb : !PluginSystem()->GetChatMessageCallbacks())
	{
		if (!cb.Function()(pSenderPlayer, text, sv_forceChatToTeamOnly->GetBool()))
		{
			if (chat_debug.GetBool())
			{
				char moduleName[MAX_PATH] = {};

				V_UnicodeToUTF8(V_UnqualifiedFileName(cb.ModuleName()), moduleName, MAX_PATH);

				Msg(eDLL_T::SERVER, "[%s] Plugin blocked chat message from '%s' (%llu): \"%s\"\n", moduleName, pSenderPlayer->GetNetName(), pSenderPlayer->GetPlatformUserId(), text);
			}

			return;
		}
	}

	const bool bSenderDeadAndCanOnlyTalkToDead = hudchat_dead_can_only_talk_to_other_dead->GetBool() && pSenderPlayer->GetLifeState();

	for (int nRecipientIndex = 1; nRecipientIndex <= nMaxClients; nRecipientIndex++)
	{
		const CPlayer* const pRecipientPlayer = UTIL_PlayerByIndex(nRecipientIndex);
		const CClient* const pRecipientClient = g_pServer->GetClient(nRecipientIndex - 1);

		//Are we all there
		if (!pRecipientPlayer || !pRecipientClient || !pRecipientPlayer->IsConnected())
			continue;

		//If our recipient is banned and the host doesnt want banned people to see others chat skip them
		if (bShouldApplyGlobalCommsMutes && pRecipientClient->GetClientExtended()->IsClientCommsBanned() && !sv_commsBannedClientsCanReceiveComms.GetBool())
			continue;

		//If we are only allowed to talk to the dead make sure the recipient is dead
		if (bSenderDeadAndCanOnlyTalkToDead == !pRecipientPlayer->GetLifeState())
			continue;

		//If we arent the recipient
		if (pRecipientPlayer != pSenderPlayer &&
			//If the chat is limited to one team we must check the sender and recipient are on the same team
			bIsTeamChat && pSenderPlayer->GetTeamNum() != pRecipientPlayer->GetTeamNum()
		)
			continue;

		CSingleUserRecipientFilter filter(pRecipientPlayer);
		filter.MakeReliable();

		v_UserMessageBegin(&filter, "SayText", 2);

		MessageWriteByte(pSenderPlayer->GetEdict());
		MessageWriteString(text);
		MessageWriteBool(bIsTeamChat);

		MessageEnd();
	}
}

static void DrawServerHitbox(int iEntity)
{
	const CEntInfo* const pInfo = g_serverEntityList->GetEntInfoPtrByIndex(iEntity);
	CBaseAnimating* const pAnimating = dynamic_cast<CBaseAnimating*>(pInfo->m_pEntity);

	if (pAnimating)
	{
		pAnimating->DrawServerHitboxes();
	}
}

static void DrawServerHitboxes()
{
	const int nVal = sv_showhitboxes->GetInt();
	Assert(nVal < NUM_ENT_ENTRIES);

	if (nVal == -1)
		return;

	if (nVal == 0)
	{
		for (int i = 0; i < NUM_ENT_ENTRIES; i++)
		{
			DrawServerHitbox(i);
		}
	}
	else // Lookup entity manually by index from 'sv_showhitboxes'.
	{
		DrawServerHitbox(nVal);
	}
}

static void DrawGeometryOverlays()
{
	const CEntInfo* pInfo = g_serverEntityList->FirstEntInfo();

	for (; pInfo; pInfo = pInfo->m_pNext)
	{
		CBaseEntity* const ent = (CBaseEntity*)pInfo->m_pEntity;

		if (ent->GetDebugOverlays() || ent->GetTimedOverlay())
		{
			ent->DrawDebugGeometryOverlays();
		}
	}
}

static void DrawAllDebugOverlays()
{
	if (!developer->GetBool())
		return;

	DrawServerHitboxes();
	DrawGeometryOverlays();
}

void CServerGameClients::_ProcessUserCmds(CServerGameClients* thisp, edict_t edict,
	bf_read* buf, int numCmds, int totalCmds, int droppedPackets, bool ignore, bool paused)
{
	int i;
	CUserCmd* from, * to;

	// We track last three command in case we drop some
	// packets but get them back.
	CUserCmd cmds[MAX_BACKUP_COMMANDS_PROCESS];
	CUserCmd cmdNull;  // For delta compression

	Assert(numCmds >= 0);
	Assert((totalCmds - numCmds) >= 0);

	// Too many commands?
	if (totalCmds < 0 || totalCmds >= (MAX_BACKUP_COMMANDS_PROCESS - 1) ||
		numCmds < 0 || numCmds > totalCmds)
	{
		const CClient* const pClient = g_pServer->GetClient(edict-1);

		Warning(eDLL_T::SERVER, "%s: Player '%s' sent too many cmds (%i)\n", __FUNCTION__, pClient->GetServerName(), totalCmds);
		buf->SetOverflowFlag();

		return;
	}

	from = &cmdNull;
	for (i = totalCmds - 1; i >= 0; i--)
	{
		to = &cmds[i];
		ReadUserCmd(buf, to, from);
		from = to;
	}

	// Server has gone inactive, just ignore.
	if (ignore)
	{
		return;
	}

	CPlayer* const pPlayer = UTIL_PlayerByIndex(edict);

	// Client not fully connected, just ignore.
	if (!pPlayer)
	{
		return;
	}

	pPlayer->ProcessUserCmds(cmds, numCmds, totalCmds, droppedPackets, paused);
}

//---------------------------------------------------------------------------------
// Purpose: dispatches the server frame job, this calls ExecuteFrameServerJob(),
//          anything you add in this function will either be before, or after the
//          server frame job has ran, so ThreadInServerFrameThread() will always
//          return false here. If you need to run code in the server frame thread,
//          consider adding your code in ExecuteFrameServerJob().
// Input  : flFrameTime - 
//			bRunOverlays - 
//			bUpdateFrame - 
//---------------------------------------------------------------------------------
static void DispatchFrameServerJob(double flFrameTime, bool bRunOverlays, bool bUniformUpdate)
{
	v_DispatchFrameServerJob(flFrameTime, bRunOverlays, bUniformUpdate);
}

//---------------------------------------------------------------------------------
// Purpose: executes the server frame job
// Input  : flFrameTime - 
//			bRunOverlays - 
//			bUpdateFrame - 
//---------------------------------------------------------------------------------
static void ExecuteFrameServerJob(double flFrameTime, bool bRunOverlays, bool bUpdateFrame)
{
	v_ExecuteFrameServerJob(flFrameTime, bRunOverlays, bUpdateFrame);

	LiveAPISystem()->RunFrame();
	DrawAllDebugOverlays();
}

void MessageEnd(void)
{
	Assert(*g_ppUsrMessageBuffer);

	g_pEngineServer->MessageEnd();

	(*g_ppUsrMessageBuffer) = nullptr;
}

void MessageWriteByte(int iValue)
{
	if (!*g_ppUsrMessageBuffer)
		Error(eDLL_T::ENGINE, EXIT_FAILURE, "WRITE_BYTE called with no active message\n");

	(*g_ppUsrMessageBuffer)->WriteByte((unsigned char)iValue);
}

void MessageWriteString(const char* pszString)
{
	if (!*g_ppUsrMessageBuffer)
		Error(eDLL_T::ENGINE, EXIT_FAILURE, "WriteString called with no active message\n");

	(*g_ppUsrMessageBuffer)->WriteString(pszString);
}

void MessageWriteBool(bool bValue)
{
	if (!*g_ppUsrMessageBuffer)
		Error(eDLL_T::ENGINE, EXIT_FAILURE, "WriteBool called with no active message\n");

	(*g_ppUsrMessageBuffer)->WriteOneBit(static_cast<int>(bValue));
}

void VServerGameDLL::Detour(const bool bAttach) const
{
	DetourSetup(&CServerGameDLL__DLLInit, &CServerGameDLL::DLLInit, bAttach);
	DetourSetup(&CServerGameDLL__OnReceivedSayTextMessage, &CServerGameDLL::OnReceivedSayTextMessage, bAttach);
	DetourSetup(&CServerGameClients__ProcessUserCmds, CServerGameClients::_ProcessUserCmds, bAttach);
	DetourSetup(&v_DispatchFrameServerJob, &DispatchFrameServerJob, bAttach);
	DetourSetup(&v_ExecuteFrameServerJob, &ExecuteFrameServerJob, bAttach);
}

CThreadMutex* g_serverFrameMutex;

CServerGameDLL* g_pServerGameDLL = nullptr;
CServerGameClients* g_pServerGameClients = nullptr;
CServerGameEnts* g_pServerGameEntities = nullptr;
CServerRandomStream* g_randomStream = nullptr;

// Holds global variables shared between engine and game.
CGlobalVars* gpGlobals = nullptr;
