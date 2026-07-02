//======== Copyright (c) Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//===========================================================================//
#include "core/stdafx.h"
#include "common/protocol.h"
#include "game/shared/shareddefs.h"
#include "game/shared/usercmd.h"
#include "game/server/movehelper_server.h"
#include "gameinterface.h"
#include "player.h"

#include "engine/server/server.h"

// NOTE[ AMOS ]: default tick interval (0.05) * default cvar value (10) = total time buffer of 0.5, which is the default of cvar 'sv_maxunlag'.
static ConVar sv_maxUserCmdProcessTicks("sv_maxUserCmdProcessTicks", "10", FCVAR_NONE, "Maximum number of client-issued UserCmd ticks that can be replayed in packet loss conditions, 0 to allow no restrictions.");

//------------------------------------------------------------------------------
// Purpose: executes a null command for this player
//------------------------------------------------------------------------------
void CPlayer::RunNullCommand(void)
{
	CUserCmd cmd;

	float flOldFrameTime = gpGlobals->frameTime;
	float flOldCurTime = gpGlobals->curTime;

	cmd.frametime = flOldFrameTime;
	cmd.command_time = flOldCurTime;

	pl.fixangle = FIXANGLE_NONE;
	EyeAngles(&cmd.viewangles);

	SetTimeBase(gpGlobals->curTime);
	MoveHelperServer()->SetHost(this);

	PlayerRunCommand(&cmd, MoveHelperServer());
	SetLastUserCommand(&cmd);

	gpGlobals->frameTime = flOldFrameTime;
	gpGlobals->curTime = flOldCurTime;

	MoveHelperServer()->SetHost(NULL);
}

//------------------------------------------------------------------------------
// Purpose: gets the eye angles of this player
// Input  : *pAngles - 
// Output : QAngle*
//------------------------------------------------------------------------------
QAngle* CPlayer::EyeAngles(QAngle* pAngles)
{
	return CPlayer__EyeAngles(this, pAngles);
}

//------------------------------------------------------------------------------
// Purpose: sets the time base for this player
// Input  : flTimeBase - 
//------------------------------------------------------------------------------
inline void CPlayer::SetTimeBase(float flTimeBase)
{
	const float fRemainderTime = Max((float)TIME_TO_TICKS(flTimeBase), 0.0f);
	SetLastUCmdSimulationRemainderTime(fRemainderTime);

	const float flAttemptedTime = Max(flTimeBase - (m_lastUCmdSimulationRemainderTime * TICK_INTERVAL), 0.0f);
	SetTotalExtraClientCmdTimeAttempted(flAttemptedTime);
}

//------------------------------------------------------------------------------
// Purpose: gets the time base for this player
//------------------------------------------------------------------------------
float CPlayer::GetTimeBase() const
{
	return TICKS_TO_TIME(m_lastUCmdSimulationTicks) + m_lastUCmdSimulationRemainderTime;
}

//------------------------------------------------------------------------------
// Purpose: sets the last user cmd simulation remainder time
// Input  : nRemainderTime - 
//------------------------------------------------------------------------------
void CPlayer::SetLastUCmdSimulationRemainderTime(float fRemainderTime)
{
	if (m_lastUCmdSimulationRemainderTime != fRemainderTime)
	{
		const edict_t nEdict = NetworkProp()->GetEdict();

		if (nEdict != FL_EDICT_INVALID)
		{
			_InterlockedOr16((SHORT*)gpGlobals->m_pEdicts + nEdict + 32, 0x200u);
		}

		m_lastUCmdSimulationRemainderTime = fRemainderTime;
	}
}

//------------------------------------------------------------------------------
// Purpose: sets the total extra client cmd time attempted
// Input  : flAttemptedTime - 
//------------------------------------------------------------------------------
void CPlayer::SetTotalExtraClientCmdTimeAttempted(float flAttemptedTime)
{
	if (m_totalExtraClientCmdTimeAttempted != flAttemptedTime)
	{
		const edict_t nEdict = NetworkProp()->GetEdict();

		if (nEdict != FL_EDICT_INVALID)
		{
			_InterlockedOr16((SHORT*)gpGlobals->m_pEdicts + nEdict + 32, 0x200u);
		}

		m_totalExtraClientCmdTimeAttempted = flAttemptedTime;
	}
}

//------------------------------------------------------------------------------
// Purpose: processes user cmd's for this player
// Input  : *cmds - 
//			numCmds - 
//			totalCmds - 
//			droppedPackets - 
//			paused - 
//------------------------------------------------------------------------------
// TODO: this code is experimental and has reported problems from players with
// high latency, needs to be debugged or a different approach needs to be taken!
// Defaulted to OFF for now
static ConVar sv_unlag_clamp("sv_unlag_clamp", "1", FCVAR_RELEASE, "Clamp the difference between player's time base and received command time to sv_maxunlag.");

void CPlayer::ProcessUserCmds(CUserCmd* cmds, int numCmds, int totalCmds,
	int droppedPackets, bool paused)
{
	if (totalCmds <= 0)
		return;

	CUserCmd* lastCmd = &m_Commands[MAX_QUEUED_COMMANDS_PROCESS];
	const float maxUnlag = sv_maxunlag->GetFloat();
	const float timeBase = GetTimeBase();

	for (int i = totalCmds - 1; i >= 0; i--)
	{
		CUserCmd* cmd = &cmds[i];
		const int commandNumber = cmd->command_number;

		if (commandNumber <= m_latestCommandQueued)
			continue;

		m_latestCommandQueued = commandNumber;
		const int lastCommandNumber = lastCmd->command_number;

		if (lastCommandNumber == MAX_QUEUED_COMMANDS_PROCESS)
			return;

		// TODO: why are grenades not clamped to sv_maxunlag ???
		// TODO: the command_time is set from the client itself in CInput::CreateMove
		// to gpGlobals->curtime in the ucmd packet, perhaps just calculate it from
		// the server based on ucmd ticks ???
		// 
		// Possible solutions that need to be explored and worked out further:
		// 
		// cmd->command_time = TICKS_TO_TIME(cmd->command_number + cmd->tick_count) // seems to be the closest, but also still manipulatable from the client.
		// cmd->command_time = TICKS_TO_TIME(client->GetDeltaTick() + cmd->command_number) // delta tick is not necessarily the same as actual ucmd tick, and will be -1 on baseline request.
		// cmd->command_time = TICKS_TO_TIME(m_lastUCmdSimulationRemainderTime) + m_totalExtraClientCmdTimeAttempted; // player timebase; also up to 100ms difference between orig sent value.
		// 
		// ... reverse more ticks and floats in CClient since there seem to be a
		// bunch still in the padded bytes, possibly one of them is what we could
		// and should actually use to get the remote client time since ucmd was sent.
		if (sv_unlag_clamp.GetBool())
			cmd->command_time = Min(Max(cmd->command_time, Max(timeBase - maxUnlag, 0.0f)), timeBase + maxUnlag);

		CUserCmd* queuedCmd = &m_Commands[lastCommandNumber];
		queuedCmd->Copy(cmd);

		if (++lastCmd->command_number > player_userCmdsQueueWarning->GetInt())
		{
			const float curTime = float(Plat_FloatTime());

			if ((curTime - m_lastCommandCountWarnTime) > 0.5f)
				m_lastCommandCountWarnTime = curTime;
		}
	}

	lastCmd->tick_count += droppedPackets;
	m_bGamePaused = paused;
}

//------------------------------------------------------------------------------
// Purpose: runs user command for this player
// Input  : *pUserCmd - 
//			*pMover - 
//------------------------------------------------------------------------------
void CPlayer::PlayerRunCommand(CUserCmd* pUserCmd, IMoveHelper* pMover)
{
	CPlayer__PlayerRunCommand(this, pUserCmd, pMover);
}

//------------------------------------------------------------------------------
// Purpose: stores off a user command
// Input  : *pUserCmd - 
//------------------------------------------------------------------------------
void CPlayer::SetLastUserCommand(CUserCmd* pUserCmd)
{
	m_LastCmd.Copy(pUserCmd);
}

static ConVar player_applyViewPunch("player_applyViewPunch", "0", FCVAR_RELEASE, "Whether to apply view punch from damage.");
static ConVar player_applyViewPunchDuringAim("player_applyViewPunchDuringAim", "0", FCVAR_RELEASE, "Whether to apply view punch from damage while aiming.");

//------------------------------------------------------------------------------
// Purpose: updates the collision bounds if the edited hull is currently active
//------------------------------------------------------------------------------
static void Player_UpdateCollisionBoundsForHull(CPlayer* const player, const bool duckHull)
{
	const bool isDuckHullActive = player->IsDucked();

	if (duckHull != isDuckHullActive)
		return;

	CCollisionProperty* const collision = player->CollisionProp();
	Vector3D& collisionMins = const_cast<Vector3D&>(collision->OBBMins());
	Vector3D& collisionMaxs = const_cast<Vector3D&>(collision->OBBMaxs());

	if (duckHull)
	{
		collisionMins = player->GetDuckHullMin();
		collisionMaxs = player->GetDuckHullMax();
	}
	else
	{
		collisionMins = player->GetStandHullMin();
		collisionMaxs = player->GetStandHullMax();
	}
}

void CPlayer::SetStandHullMin(const Vector3D& mins)
{
	m_StandHullMin = mins;
	Player_UpdateCollisionBoundsForHull(this, false);
}

void CPlayer::SetStandHullMax(const Vector3D& maxs)
{
	m_StandHullMax = maxs;
	Player_UpdateCollisionBoundsForHull(this, false);
}

void CPlayer::SetDuckHullMin(const Vector3D& mins)
{
	m_DuckHullMin = mins;
	Player_UpdateCollisionBoundsForHull(this, true);
}

void CPlayer::SetDuckHullMax(const Vector3D& maxs)
{
	m_DuckHullMax = maxs;
	Player_UpdateCollisionBoundsForHull(this, true);
}

//------------------------------------------------------------------------------
// Purpose: applies view punch to player view angles when taking damage
// Input  : *player (this) - 
//			inputInfo - 
//------------------------------------------------------------------------------
void Player_ApplyViewPunch(CPlayer* thisptr, const CTakeDamageInfo* inputInfo)
{
	if (!player_applyViewPunch.GetBool())
		return;

	if (thisptr->IsZooming() && !player_applyViewPunchDuringAim.GetBool())
		return;

	CPlayer__ApplyViewPunch(thisptr, inputInfo);
}

//------------------------------------------------------------------------------
// Purpose: run physics simulation for player
// Input  : *player (this) - 
//			numPerIteration - 
//			adjustTimeBase - 
//------------------------------------------------------------------------------
bool Player_PhysicsSimulate(CPlayer* player, int numPerIteration, bool adjustTimeBase)
{
	CClientExtended* const cle = g_pServer->GetClientExtended(player->GetEdict() - 1);
	const int numUserCmdProcessTicksMax = sv_maxUserCmdProcessTicks.GetInt();

	if (numUserCmdProcessTicksMax && gpGlobals->gameMode != GameMode_t::SP_MODE) // don't apply this filter in SP games
		cle->InitializeMovementTimeForUserCmdProcessing(numUserCmdProcessTicksMax, TICK_INTERVAL);
	else // Otherwise we don't care to track time
		cle->SetRemainingMovementTimeForUserCmdProcessing(FLT_MAX);

	return CPlayer__PhysicsSimulate(player, numPerIteration, adjustTimeBase);
}

/*
=====================
CC_CreateFakePlayer_f

  Creates a fake player
  on the server
=====================
*/
static void CC_CreateFakePlayer_f(const CCommand& args)
{
	if (!g_pServer->IsActive())
		return;

	if (args.ArgC() < 3)
	{
		Msg(eDLL_T::SERVER, "usage 'sv_addbot': name(string) teamid(int)\n");
		return;
	}

	const int numPlayers = g_pServer->GetNumClients();

	// Already at max, don't create.
	if (numPlayers >= g_ServerGlobalVariables->maxClients)
		return;

	const char* const playerName = args.Arg(1);
	const int teamNum = atoi(args.Arg(2));

	// The following code must either run inside the server frame thread, or
	// after it has finished. Lock here and help with other jobs until the
	// server has finished running the frame.
	ThreadJoinServerJob();

	// note(amos): if you call CServer::CreateFakeClient() directly, you also
	// need to lock the string tables with LockNetworkStringTables. The 
	// CVEngineServer method automatically locks and unlocks the string tables.
	const edict_t nHandle = g_pEngineServer->CreateFakeClient(playerName, teamNum);
	g_pServerGameClients->ClientFullyConnect(nHandle, false);
}

static ConCommand sv_addbot("sv_addbot", CC_CreateFakePlayer_f, "Creates a bot on the server", FCVAR_RELEASE);

//------------------------------------------------------------------------------
// Purpose: parses a vector from a console command
//------------------------------------------------------------------------------
static bool Player_ParseHullVectorArgs(const CCommand& args, Vector3D* const out)
{
	if (args.ArgC() < 4)
		return false;

	out->x = float(atof(args.Arg(1)));
	out->y = float(atof(args.Arg(2)));
	out->z = float(atof(args.Arg(3)));
	return true;
}

//------------------------------------------------------------------------------
// Purpose: updates a hull vector on the command client
//------------------------------------------------------------------------------
static void Player_HullVectorCommand(const CCommand& args, const char* const usage, void(CPlayer::*setter)(const Vector3D&), const Vector3D&(CPlayer::*getter)() const)
{
	CPlayer* const player = UTIL_GetCommandClient();

	if (!player)
	{
		Msg(eDLL_T::SERVER, "%s can only be used by an in game player\n", args.Arg(0));
		return;
	}

	if (args.ArgC() < 4)
	{
		const Vector3D& current = (player->*getter)();
		Msg(eDLL_T::SERVER, "%s = <%f, %f, %f>\n", args.Arg(0), current.x, current.y, current.z);
		Msg(eDLL_T::SERVER, "usage: %s\n", usage);
		return;
	}

	Vector3D value;
	if (!Player_ParseHullVectorArgs(args, &value))
		return;

	(player->*setter)(value);
}

static void CC_m_standhullmin_f(const CCommand& args)
{
	Player_HullVectorCommand(args, "m_standhullmin <x> <y> <z>", &CPlayer::SetStandHullMin, &CPlayer::GetStandHullMin);
}

static void CC_m_standhullmax_f(const CCommand& args)
{
	Player_HullVectorCommand(args, "m_standhullmax <x> <y> <z>", &CPlayer::SetStandHullMax, &CPlayer::GetStandHullMax);
}

static void CC_m_crouchhullmin_f(const CCommand& args)
{
	Player_HullVectorCommand(args, "m_crouchhullmin <x> <y> <z>", &CPlayer::SetDuckHullMin, &CPlayer::GetDuckHullMin);
}

static void CC_m_crouchhullmax_f(const CCommand& args)
{
	Player_HullVectorCommand(args, "m_crouchhullmax <x> <y> <z>", &CPlayer::SetDuckHullMax, &CPlayer::GetDuckHullMax);
}

static void CC_m_viewoffset_f(const CCommand& args)
{
	Player_HullVectorCommand(args, "m_vecviewoffset <x> <y> <z>", &CPlayer::SetViewOffset, &CPlayer::GetViewOffset);
}

static ConCommand m_standhullmin("m_standhullmin", CC_m_standhullmin_f, "Sets the players standing hull mins", FCVAR_GAMEDLL | FCVAR_CHEAT);
static ConCommand m_standhullmax("m_standhullmax", CC_m_standhullmax_f, "Sets the players standing hull maxs", FCVAR_GAMEDLL | FCVAR_CHEAT);
static ConCommand m_crouchhullmin("m_crouchhullmin", CC_m_crouchhullmin_f, "Sets the players crouch hull mins", FCVAR_GAMEDLL | FCVAR_CHEAT);
static ConCommand m_crouchhullmax("m_crouchhullmax", CC_m_crouchhullmax_f, "Sets players crouch hull maxs", FCVAR_GAMEDLL | FCVAR_CHEAT);
static ConCommand m_vecviewoffset("m_vecviewoffset", CC_m_viewoffset_f, "Sets players view offset", FCVAR_GAMEDLL | FCVAR_CHEAT);

void VPlayer::Detour(const bool bAttach) const
{
	DetourSetup(&CPlayer__PhysicsSimulate, &Player_PhysicsSimulate, bAttach);
	DetourSetup(&CPlayer__ApplyViewPunch, &Player_ApplyViewPunch, bAttach);
}
