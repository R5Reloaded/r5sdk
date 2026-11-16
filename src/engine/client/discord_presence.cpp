#include "core/stdafx.h"

#ifndef DEDICATED

#include "discord_presence.h"
#include "discord_rpc.h"
#include "common/global.h"
#include "engine/client/clientstate.h"
#include "engine/host_state.h"
#include "engine/server/server.h"
#include "rtech/playlists/playlists.h"
#include "tier0/commandline.h"

static ConVar discord_presence_enable("discord_presence_enable", "1", FCVAR_RELEASE | FCVAR_ARCHIVE, "Enable Discord Rich Presence updates.");

namespace
{
constexpr char DISCORD_APPLICATION_ID[] = "1364049087434850444";
constexpr char DISCORD_LARGE_IMAGE[] = "embedded_cover";
constexpr char DISCORD_LARGE_IMAGE_TEXT[] = "R5Reloaded";

bool CopyIfChanged(char* destination, size_t destinationSize, const char* source)
{
	if (!source)
		return false;

	const size_t sourceLength = strlen(source);
	const size_t copyLength = min(sourceLength, destinationSize - 1);
	if (strlen(destination) == copyLength && V_strncmp(destination, source, copyLength) == 0)
	{
		return false;
	}

	V_strncpy(destination, source, destinationSize);
	return true;
}
}

bool CDiscordPresence::s_bInitialized = false;
bool CDiscordPresence::s_bConnected = false;
bool CDiscordPresence::s_bNeedsUpdate = false;
int64_t CDiscordPresence::s_nStartTime = 0;
char CDiscordPresence::s_szState[128] = {};
char CDiscordPresence::s_szDetails[128] = {};
char CDiscordPresence::s_szMap[64] = {};
char CDiscordPresence::s_szServerName[128] = {};
char CDiscordPresence::s_szPlaylist[64] = {};
int CDiscordPresence::s_nCurrentPlayers = 0;
int CDiscordPresence::s_nMaxPlayers = 0;

void CDiscordPresence::Initialize(void)
{
	if (s_bInitialized || !IsEnabled())
		return;

	DiscordEventHandlers handlers = {};
	handlers.ready = OnReady;
	handlers.disconnected = OnDisconnected;
	handlers.errored = OnError;

	Discord_Initialize(DISCORD_APPLICATION_ID, &handlers);
	s_nStartTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	s_bInitialized = true;
	s_bNeedsUpdate = true;
}

void CDiscordPresence::Shutdown(void)
{
	if (!s_bInitialized)
		return;

	Discord_Shutdown();
	s_bInitialized = false;
	s_bConnected = false;
}

void CDiscordPresence::Update(void)
{
	if (!IsEnabled())
	{
		Shutdown();
		return;
	}

	if (!s_bInitialized)
		Initialize();
	if (!s_bInitialized)
		return;

	Discord_RunCallbacks();
	UpdateServerInfo();

	if (s_bConnected && s_bNeedsUpdate)
	{
		UpdatePresence();
		s_bNeedsUpdate = false;
	}
}

void CDiscordPresence::SetGameState(const char* state, const char* details)
{
	if (CopyIfChanged(s_szState, sizeof(s_szState), state))
		s_bNeedsUpdate = true;
	if (CopyIfChanged(s_szDetails, sizeof(s_szDetails), details))
		s_bNeedsUpdate = true;
}

void CDiscordPresence::SetServerInfo(const char* serverName, int currentPlayers, int maxPlayers, const char* playlist)
{
	if (CopyIfChanged(s_szServerName, sizeof(s_szServerName), serverName))
		s_bNeedsUpdate = true;
	if (CopyIfChanged(s_szPlaylist, sizeof(s_szPlaylist), playlist))
		s_bNeedsUpdate = true;

	if (maxPlayers >= 0 && maxPlayers <= 10000)
	{
		if (s_nMaxPlayers != maxPlayers)
		{
			s_nMaxPlayers = maxPlayers;
			s_bNeedsUpdate = true;
		}

		if (currentPlayers >= 0 && currentPlayers <= maxPlayers && s_nCurrentPlayers != currentPlayers)
		{
			s_nCurrentPlayers = currentPlayers;
			s_bNeedsUpdate = true;
		}
	}
}

void CDiscordPresence::SetMapInfo(const char* mapName)
{
	if (CopyIfChanged(s_szMap, sizeof(s_szMap), mapName))
		s_bNeedsUpdate = true;
}

void CDiscordPresence::ClearServerInfo(void)
{
	bool changed = false;
	changed |= CopyIfChanged(s_szServerName, sizeof(s_szServerName), "");
	changed |= CopyIfChanged(s_szPlaylist, sizeof(s_szPlaylist), "");
	changed |= CopyIfChanged(s_szMap, sizeof(s_szMap), "");

	if (s_nCurrentPlayers != 0 || s_nMaxPlayers != 0)
	{
		s_nCurrentPlayers = 0;
		s_nMaxPlayers = 0;
		changed = true;
	}

	s_bNeedsUpdate |= changed;
}

void CDiscordPresence::ClearPresence(void)
{
	if (s_bInitialized)
		Discord_ClearPresence();

	s_szState[0] = '\0';
	s_szDetails[0] = '\0';
	ClearServerInfo();
	s_bNeedsUpdate = false;
}

bool CDiscordPresence::IsEnabled(void)
{
	return discord_presence_enable.GetBool() && !CommandLine()->CheckParm("-nodiscord");
}

bool CDiscordPresence::IsConnected(void)
{
	return s_bConnected;
}

void CDiscordPresence::UpdatePresence(void)
{
	DiscordRichPresence presence = {};
	char details[128] = {};

	if (s_szServerName[0] && s_nMaxPlayers > 0)
	{
		presence.state = s_szServerName;

		if (s_szMap[0] && s_szPlaylist[0])
			V_snprintf(details, sizeof(details), "%s (%s)", s_szMap, s_szPlaylist);
		else if (s_szMap[0])
			V_strncpy(details, s_szMap, sizeof(details));
		else if (s_szPlaylist[0])
			V_snprintf(details, sizeof(details), "Playing %s", s_szPlaylist);
		else
			V_strncpy(details, "In game", sizeof(details));

		presence.details = details;
		presence.partySize = s_nCurrentPlayers;
		presence.partyMax = s_nMaxPlayers;
	}
	else
	{
		presence.state = s_szState[0] ? s_szState : nullptr;
		presence.details = s_szDetails[0] ? s_szDetails : nullptr;
	}

	presence.startTimestamp = s_nStartTime;
	presence.largeImageKey = DISCORD_LARGE_IMAGE;
	presence.largeImageText = DISCORD_LARGE_IMAGE_TEXT;
	Discord_UpdatePresence(&presence);
}

void CDiscordPresence::UpdateServerInfo(void)
{
	if (!g_pClientState || !g_pClientState->IsConnected())
	{
		ClearServerInfo();
		return;
	}

	const char* serverName = hostname && hostname->GetString()[0] ? hostname->GetString() : "Unknown Server";
	const char* playlist = v_Playlists_GetCurrent();
	if (!playlist || !playlist[0])
		playlist = "Unknown";

	int currentPlayers = 1;
	int maxPlayers = g_pClientState->m_nMaxClients;

#ifndef CLIENT_DLL
	if (g_pServer && g_pServer->IsActive())
	{
		currentPlayers = g_pServer->GetNumClients();
		maxPlayers = g_pServer->GetMaxClients();
	}
#endif // !CLIENT_DLL

	SetServerInfo(serverName, currentPlayers, maxPlayers, playlist);
	if (g_pHostState && g_pHostState->m_levelName[0])
		SetMapInfo(g_pHostState->m_levelName);
}

void CDiscordPresence::OnReady(const DiscordRPCUser* user)
{
	s_bConnected = true;
	s_bNeedsUpdate = true;

	if (!s_szState[0])
		SetGameState("In menu", "R5Reloaded");

	if (user && user->username && user->username[0])
		DevMsg(eDLL_T::ENGINE, "Discord RPC ready for user '%s'\n", user->username);
	else
		DevMsg(eDLL_T::ENGINE, "Discord RPC ready\n");
}

void CDiscordPresence::OnDisconnected(int errorCode, const char* message)
{
	s_bConnected = false;
	DevMsg(eDLL_T::ENGINE, "Discord RPC disconnected (%d): %s\n", errorCode, message ? message : "");
}

void CDiscordPresence::OnError(int errorCode, const char* message)
{
	DevWarning(eDLL_T::ENGINE, "Discord RPC error (%d): %s\n", errorCode, message ? message : "");
}

#endif // !DEDICATED
