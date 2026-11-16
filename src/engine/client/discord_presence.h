#pragma once

#ifndef DEDICATED

struct DiscordRPCUser;

class CDiscordPresence
{
public:
	static void Initialize(void);
	static void Shutdown(void);
	static void Update(void);

	static void SetGameState(const char* state, const char* details = nullptr);
	static void SetServerInfo(const char* serverName, int currentPlayers, int maxPlayers, const char* playlist = nullptr);
	static void SetMapInfo(const char* mapName);
	static void ClearServerInfo(void);
	static void ClearPresence(void);

	static bool IsEnabled(void);
	static bool IsConnected(void);

private:
	static void UpdatePresence(void);
	static void UpdateServerInfo(void);
	static void OnReady(const DiscordRPCUser* user);
	static void OnDisconnected(int errorCode, const char* message);
	static void OnError(int errorCode, const char* message);

	static bool s_bInitialized;
	static bool s_bConnected;
	static bool s_bNeedsUpdate;
	static int64_t s_nStartTime;
	static char s_szState[128];
	static char s_szDetails[128];
	static char s_szMap[64];
	static char s_szServerName[128];
	static char s_szPlaylist[64];
	static int s_nCurrentPlayers;
	static int s_nMaxPlayers;
};

#endif // !DEDICATED
