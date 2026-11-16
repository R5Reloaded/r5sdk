#pragma once

#ifndef DEDICATED

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus

typedef struct DiscordRichPresence
{
	const char* state;
	const char* details;
	int64_t startTimestamp;
	int64_t endTimestamp;
	const char* largeImageKey;
	const char* largeImageText;
	const char* smallImageKey;
	const char* smallImageText;
	const char* partyId;
	int partySize;
	int partyMax;
	const char* matchSecret;
	const char* joinSecret;
	const char* spectateSecret;
	int8_t instance;
} DiscordRichPresence;

typedef struct DiscordRPCUser
{
	const char* userId;
	const char* username;
	const char* discriminator;
	const char* avatar;
} DiscordRPCUser;

typedef void (*DiscordReadyCallback)(const DiscordRPCUser* user);
typedef void (*DiscordDisconnectedCallback)(int errorCode, const char* message);
typedef void (*DiscordErrorCallback)(int errorCode, const char* message);
typedef void (*DiscordJoinGameCallback)(const char* joinSecret);
typedef void (*DiscordSpectateGameCallback)(const char* spectateSecret);
typedef void (*DiscordJoinRequestCallback)(const DiscordRPCUser* user);

typedef struct DiscordEventHandlers
{
	DiscordReadyCallback ready;
	DiscordDisconnectedCallback disconnected;
	DiscordErrorCallback errored;
	DiscordJoinGameCallback joinGame;
	DiscordSpectateGameCallback spectateGame;
	DiscordJoinRequestCallback joinRequest;
} DiscordEventHandlers;

#define DISCORD_REPLY_NO 0
#define DISCORD_REPLY_YES 1
#define DISCORD_REPLY_IGNORE 2

void Discord_Initialize(const char* applicationId, DiscordEventHandlers* handlers);
void Discord_Shutdown(void);
void Discord_RunCallbacks(void);
void Discord_UpdatePresence(const DiscordRichPresence* presence);
void Discord_ClearPresence(void);
void Discord_Respond(const char* userId, int reply);
void Discord_UpdateHandlers(DiscordEventHandlers* handlers);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // !DEDICATED
