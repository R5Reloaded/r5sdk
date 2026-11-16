#include "core/stdafx.h"

#ifndef DEDICATED

#include "discord_ipc.h"
#include "discord_rpc.h"

static CDiscordIpc s_DiscordIpc;

void Discord_Initialize(const char* applicationId, DiscordEventHandlers* handlers)
{
	s_DiscordIpc.Initialize(applicationId, handlers);
}

void Discord_Shutdown(void)
{
	s_DiscordIpc.Shutdown();
}

void Discord_RunCallbacks(void)
{
	s_DiscordIpc.RunCallbacks();
}

void Discord_UpdatePresence(const DiscordRichPresence* presence)
{
	s_DiscordIpc.UpdatePresence(presence);
}

void Discord_ClearPresence(void)
{
	s_DiscordIpc.ClearPresence();
}

void Discord_Respond(const char* userId, int reply)
{
	s_DiscordIpc.Respond(userId, reply);
}

void Discord_UpdateHandlers(DiscordEventHandlers* handlers)
{
	s_DiscordIpc.UpdateHandlers(handlers);
}

#endif // !DEDICATED
