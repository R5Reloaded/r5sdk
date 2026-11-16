#pragma once

#ifndef DEDICATED

#include "discord_rpc.h"

class CDiscordIpc
{
public:
	CDiscordIpc(void);
	~CDiscordIpc(void);

	bool Initialize(const char* applicationId, const DiscordEventHandlers* handlers);
	void Shutdown(void);
	void RunCallbacks(void);

	void UpdatePresence(const DiscordRichPresence* presence);
	void ClearPresence(void);
	void Respond(const char* userId, int reply);
	void UpdateHandlers(const DiscordEventHandlers* handlers);

	bool IsConnected(void) const;

private:
	bool Connect(void);
	void Disconnect(int errorCode, const char* message, bool notify);
	bool SendFrame(uint32_t opcode, const char* payload, size_t payloadLength);
	bool SendJson(const std::string& payload);
	bool ReadFrame(void);
	void ProcessMessage(const char* payload, size_t payloadLength);
	void SetEventSubscription(const char* eventName, bool subscribe);
	void SendPendingPresence(void);

	std::string BuildPresencePayload(const DiscordRichPresence* presence);
	std::string BuildEventPayload(const char* command, const char* eventName);
	std::string BuildJoinReplyPayload(const char* userId, int reply);
	std::string NextNonce(void);

	HANDLE m_hPipe;
	bool m_bInitialized;
	bool m_bConnected;
	bool m_bReady;
	double m_flNextConnectAttempt;
	uint32_t m_nNonce;
	std::string m_ApplicationId;
	std::string m_PendingPresence;
	bool m_bPresencePending;
	DiscordEventHandlers m_Handlers;
};

#endif // !DEDICATED
