#include "core/stdafx.h"

#ifndef DEDICATED

#include "discord_ipc.h"
#include "tier0/platform.h"

namespace
{
constexpr uint32_t DISCORD_IPC_VERSION = 1;
constexpr uint32_t DISCORD_IPC_HANDSHAKE = 0;
constexpr uint32_t DISCORD_IPC_FRAME = 1;
constexpr uint32_t DISCORD_IPC_CLOSE = 2;
constexpr uint32_t DISCORD_IPC_PING = 3;
constexpr uint32_t DISCORD_IPC_PONG = 4;
constexpr uint32_t DISCORD_IPC_MAX_PAYLOAD = 16 * 1024;
constexpr double DISCORD_IPC_RECONNECT_DELAY = 5.0;

struct DiscordIpcHeader_s
{
	uint32_t opcode;
	uint32_t length;
};

static_assert(sizeof(DiscordIpcHeader_s) == 8, "Discord IPC header has an invalid size");

std::string SerializeJson(const rapidjson::Document& document)
{
	rapidjson::StringBuffer buffer;
	rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
	document.Accept(writer);

	return std::string(buffer.GetString(), buffer.GetSize());
}

const rapidjson::Value* GetObjectMember(const rapidjson::Value& object, const char* name)
{
	const rapidjson::Value::ConstMemberIterator it = object.FindMember(name);
	return it != object.MemberEnd() && it->value.IsObject() ? &it->value : nullptr;
}

const char* GetStringMember(const rapidjson::Value& object, const char* name, const char* defaultValue = "")
{
	const rapidjson::Value::ConstMemberIterator it = object.FindMember(name);
	return it != object.MemberEnd() && it->value.IsString() ? it->value.GetString() : defaultValue;
}

int GetIntMember(const rapidjson::Value& object, const char* name, int defaultValue = 0)
{
	const rapidjson::Value::ConstMemberIterator it = object.FindMember(name);
	return it != object.MemberEnd() && it->value.IsInt() ? it->value.GetInt() : defaultValue;
}

void AddOptionalString(rapidjson::Value& object, const char* name, const char* value, size_t maxLength, rapidjson::Document::AllocatorType& allocator)
{
	if (!value)
		return;

	const size_t length = strlen(value);
	if (length == 0 || length > maxLength)
		return;

	object.AddMember(rapidjson::Value(name, allocator), rapidjson::Value(value, length, allocator), allocator);
}

bool ReadPipeExact(HANDLE pipe, void* output, DWORD length)
{
	byte* cursor = static_cast<byte*>(output);
	DWORD totalRead = 0;

	while (totalRead < length)
	{
		DWORD bytesRead = 0;
		if (!ReadFile(pipe, cursor + totalRead, length - totalRead, &bytesRead, nullptr) || bytesRead == 0)
			return false;

		totalRead += bytesRead;
	}

	return true;
}
}

CDiscordIpc::CDiscordIpc(void) : m_hPipe(INVALID_HANDLE_VALUE), m_bInitialized(false), m_bConnected(false), m_bReady(false), m_flNextConnectAttempt(0.0), m_nNonce(0), m_bPresencePending(false), m_Handlers({})
{
}

CDiscordIpc::~CDiscordIpc(void)
{
	Shutdown();
}

bool CDiscordIpc::Initialize(const char* applicationId, const DiscordEventHandlers* handlers)
{
	if (m_bInitialized)
	{
		UpdateHandlers(handlers);
		return true;
	}

	if (!applicationId)
		return false;

	const size_t applicationIdLength = strlen(applicationId);
	if (applicationIdLength == 0 || applicationIdLength > 32)
		return false;

	for (const char* cursor = applicationId; *cursor; ++cursor)
	{
		if (*cursor < '0' || *cursor > '9')
			return false;
	}

	m_ApplicationId.assign(applicationId, applicationIdLength);
	m_Handlers = handlers ? *handlers : DiscordEventHandlers{};
	m_bInitialized = true;
	m_flNextConnectAttempt = 0.0;

	Connect();
	return true;
}

void CDiscordIpc::Shutdown(void)
{
	if (!m_bInitialized)
		return;

	Disconnect(0, nullptr, false);
	m_bInitialized = false;
	m_nNonce = 0;
	m_ApplicationId.clear();
	m_PendingPresence.clear();
	m_bPresencePending = false;
	m_Handlers = {};
}

void CDiscordIpc::RunCallbacks(void)
{
	if (!m_bInitialized)
		return;

	if (!m_bConnected)
	{
		const double currentTime = Plat_FloatTime();
		if (currentTime >= m_flNextConnectAttempt)
		{
			if (!Connect())
				m_flNextConnectAttempt = currentTime + DISCORD_IPC_RECONNECT_DELAY;
		}
		return;
	}

	// Bound the work per call in case Discord has queued a large message burst.
	for (int i = 0; i < 64 && ReadFrame(); ++i)
	{
	}
}

void CDiscordIpc::UpdatePresence(const DiscordRichPresence* presence)
{
	if (!m_bInitialized)
		return;

	m_PendingPresence = BuildPresencePayload(presence);
	m_bPresencePending = !m_PendingPresence.empty();
	SendPendingPresence();
}

void CDiscordIpc::ClearPresence(void)
{
	UpdatePresence(nullptr);
}

void CDiscordIpc::Respond(const char* userId, int reply)
{
	if (!m_bReady || !userId || !*userId || reply == DISCORD_REPLY_IGNORE)
		return;

	if (reply != DISCORD_REPLY_NO && reply != DISCORD_REPLY_YES)
		return;

	SendJson(BuildJoinReplyPayload(userId, reply));
}

void CDiscordIpc::UpdateHandlers(const DiscordEventHandlers* handlers)
{
	const DiscordEventHandlers oldHandlers = m_Handlers;
	const DiscordEventHandlers newHandlers = handlers ? *handlers : DiscordEventHandlers{};

	if (m_bReady)
	{
		if (!!oldHandlers.joinGame != !!newHandlers.joinGame)
			SetEventSubscription("ACTIVITY_JOIN", !!newHandlers.joinGame);
		if (!!oldHandlers.spectateGame != !!newHandlers.spectateGame)
			SetEventSubscription("ACTIVITY_SPECTATE", !!newHandlers.spectateGame);
		if (!!oldHandlers.joinRequest != !!newHandlers.joinRequest)
			SetEventSubscription("ACTIVITY_JOIN_REQUEST", !!newHandlers.joinRequest);
	}

	m_Handlers = newHandlers;
}

bool CDiscordIpc::IsConnected(void) const
{
	return m_bReady;
}

bool CDiscordIpc::Connect(void)
{
	if (m_bConnected)
		return true;

	for (int i = 0; i < 10; ++i)
	{
		char pipeName[64];
		V_snprintf(pipeName, sizeof(pipeName), "\\\\.\\pipe\\discord-ipc-%d", i);

		m_hPipe = CreateFileA(pipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
		if (m_hPipe == INVALID_HANDLE_VALUE)
			continue;

		DWORD readMode = PIPE_READMODE_BYTE;
		if (!SetNamedPipeHandleState(m_hPipe, &readMode, nullptr, nullptr))
		{
			CloseHandle(m_hPipe);
			m_hPipe = INVALID_HANDLE_VALUE;
			continue;
		}

		rapidjson::Document handshake;
		handshake.SetObject();
		rapidjson::Document::AllocatorType& allocator = handshake.GetAllocator();
		handshake.AddMember("v", DISCORD_IPC_VERSION, allocator);
		handshake.AddMember("client_id", rapidjson::Value(m_ApplicationId.c_str(), m_ApplicationId.length(), allocator), allocator);

		const std::string payload = SerializeJson(handshake);
		if (!SendFrame(DISCORD_IPC_HANDSHAKE, payload.c_str(), payload.length()))
		{
			CloseHandle(m_hPipe);
			m_hPipe = INVALID_HANDLE_VALUE;
			continue;
		}

		m_bConnected = true;
		m_bReady = false;
		DevMsg(eDLL_T::ENGINE, "Discord IPC connected to '%s'\n", pipeName);
		return true;
	}

	return false;
}

void CDiscordIpc::Disconnect(int errorCode, const char* message, bool notify)
{
	const bool wasConnected = m_bConnected;

	if (m_hPipe != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_hPipe);
		m_hPipe = INVALID_HANDLE_VALUE;
	}

	m_bConnected = false;
	m_bReady = false;
	m_flNextConnectAttempt = Plat_FloatTime() + DISCORD_IPC_RECONNECT_DELAY;
	m_bPresencePending = !m_PendingPresence.empty();

	if (notify && wasConnected && m_Handlers.disconnected)
		m_Handlers.disconnected(errorCode, message ? message : "");
}

bool CDiscordIpc::SendFrame(uint32_t opcode, const char* payload, size_t payloadLength)
{
	if (m_hPipe == INVALID_HANDLE_VALUE || payloadLength > DISCORD_IPC_MAX_PAYLOAD)
		return false;

	const DiscordIpcHeader_s header = { opcode, static_cast<uint32_t>(payloadLength) };
	vector<byte> frame(sizeof(header) + payloadLength);
	memcpy(frame.data(), &header, sizeof(header));
	if (payloadLength > 0)
		memcpy(frame.data() + sizeof(header), payload, payloadLength);

	DWORD totalWritten = 0;
	while (totalWritten < frame.size())
	{
		DWORD bytesWritten = 0;
		const DWORD bytesRemaining = static_cast<DWORD>(frame.size() - totalWritten);
		if (!WriteFile(m_hPipe, frame.data() + totalWritten, bytesRemaining, &bytesWritten, nullptr) || bytesWritten == 0)
		{
			return false;
		}

		totalWritten += bytesWritten;
	}

	return true;
}

bool CDiscordIpc::SendJson(const std::string& payload)
{
	if (!m_bConnected || payload.empty())
		return false;

	if (SendFrame(DISCORD_IPC_FRAME, payload.c_str(), payload.length()))
		return true;

	const DWORD errorCode = GetLastError();
	Disconnect(static_cast<int>(errorCode), "Failed to write to the Discord IPC pipe", true);
	return false;
}

bool CDiscordIpc::ReadFrame(void)
{
	DWORD bytesAvailable = 0;
	if (!PeekNamedPipe(m_hPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr))
	{
		const DWORD errorCode = GetLastError();
		Disconnect(static_cast<int>(errorCode), "Failed to read from the Discord IPC pipe", true);
		return false;
	}

	if (bytesAvailable < sizeof(DiscordIpcHeader_s))
		return false;

	DiscordIpcHeader_s header = {};
	DWORD headerBytes = 0;
	if (!PeekNamedPipe(m_hPipe, &header, sizeof(header), &headerBytes, &bytesAvailable, nullptr) || headerBytes != sizeof(header))
	{
		const DWORD errorCode = GetLastError();
		Disconnect(static_cast<int>(errorCode), "Failed to inspect the Discord IPC frame", true);
		return false;
	}

	if (header.length > DISCORD_IPC_MAX_PAYLOAD)
	{
		Disconnect(ERROR_INVALID_DATA, "Discord IPC frame exceeds the maximum size", true);
		return false;
	}

	if (bytesAvailable < sizeof(header) + header.length)
		return false;

	vector<char> payload(header.length + 1, '\0');
	if (!ReadPipeExact(m_hPipe, &header, sizeof(header)) || (header.length > 0 && !ReadPipeExact(m_hPipe, payload.data(), header.length)))
	{
		const DWORD errorCode = GetLastError();
		Disconnect(static_cast<int>(errorCode), "Failed to read the Discord IPC frame", true);
		return false;
	}

	switch (header.opcode)
	{
	case DISCORD_IPC_FRAME:
		ProcessMessage(payload.data(), header.length);
		break;
	case DISCORD_IPC_CLOSE:
	{
		rapidjson::Document closeMessage;
		closeMessage.Parse(payload.data(), header.length);
		const int errorCode = closeMessage.IsObject() ? GetIntMember(closeMessage, "code") : 0;
		const char* message = closeMessage.IsObject() ? GetStringMember(closeMessage, "message", "Discord closed the IPC connection") : "Discord closed the IPC connection";
		Disconnect(errorCode, message, true);
		break;
	}
	case DISCORD_IPC_PING:
		if (!SendFrame(DISCORD_IPC_PONG, payload.data(), header.length))
			Disconnect(static_cast<int>(GetLastError()), "Failed to reply to Discord IPC ping", true);
		break;
	default:
		break;
	}

	return m_bConnected;
}

void CDiscordIpc::ProcessMessage(const char* payload, size_t payloadLength)
{
	rapidjson::Document message;
	message.Parse(payload, payloadLength);
	if (message.HasParseError() || !message.IsObject())
	{
		if (m_Handlers.errored)
			m_Handlers.errored(ERROR_INVALID_DATA, "Discord returned malformed JSON");
		return;
	}

	const char* eventName = GetStringMember(message, "evt", nullptr);
	if (!eventName)
		return;

	const rapidjson::Value* data = GetObjectMember(message, "data");
	if (strcmp(eventName, "READY") == 0)
	{
		m_bReady = true;

		if (m_Handlers.joinGame)
			SetEventSubscription("ACTIVITY_JOIN", true);
		if (m_Handlers.spectateGame)
			SetEventSubscription("ACTIVITY_SPECTATE", true);
		if (m_Handlers.joinRequest)
			SetEventSubscription("ACTIVITY_JOIN_REQUEST", true);

		SendPendingPresence();

		if (m_Handlers.ready)
		{
			const rapidjson::Value* user = data ? GetObjectMember(*data, "user") : nullptr;
			DiscordRPCUser discordUser = {};
			discordUser.userId = user ? GetStringMember(*user, "id") : "";
			discordUser.username = user ? GetStringMember(*user, "username") : "";
			discordUser.discriminator = user ? GetStringMember(*user, "discriminator") : "";
			discordUser.avatar = user ? GetStringMember(*user, "avatar") : "";
			m_Handlers.ready(&discordUser);
		}
	}
	else if (strcmp(eventName, "ERROR") == 0)
	{
		if (m_Handlers.errored)
		{
			const int errorCode = data ? GetIntMember(*data, "code", -1) : -1;
			const char* errorMessage = data ? GetStringMember(*data, "message", "Discord RPC error") : "Discord RPC error";
			m_Handlers.errored(errorCode, errorMessage);
		}
	}
	else if (data && strcmp(eventName, "ACTIVITY_JOIN") == 0 && m_Handlers.joinGame)
	{
		m_Handlers.joinGame(GetStringMember(*data, "secret"));
	}
	else if (data && strcmp(eventName, "ACTIVITY_SPECTATE") == 0 && m_Handlers.spectateGame)
	{
		m_Handlers.spectateGame(GetStringMember(*data, "secret"));
	}
	else if (data && strcmp(eventName, "ACTIVITY_JOIN_REQUEST") == 0 && m_Handlers.joinRequest)
	{
		const rapidjson::Value* user = GetObjectMember(*data, "user");
		if (user)
		{
			DiscordRPCUser discordUser = {};
			discordUser.userId = GetStringMember(*user, "id");
			discordUser.username = GetStringMember(*user, "username");
			discordUser.discriminator = GetStringMember(*user, "discriminator");
			discordUser.avatar = GetStringMember(*user, "avatar");
			m_Handlers.joinRequest(&discordUser);
		}
	}
}

void CDiscordIpc::SetEventSubscription(const char* eventName, bool subscribe)
{
	SendJson(BuildEventPayload(subscribe ? "SUBSCRIBE" : "UNSUBSCRIBE", eventName));
}

void CDiscordIpc::SendPendingPresence(void)
{
	if (!m_bReady || !m_bPresencePending)
		return;

	if (SendJson(m_PendingPresence))
		m_bPresencePending = false;
}

std::string CDiscordIpc::BuildPresencePayload(const DiscordRichPresence* presence)
{
	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
	const std::string nonce = NextNonce();

	document.AddMember("nonce", rapidjson::Value(nonce.c_str(), nonce.length(), allocator), allocator);
	document.AddMember("cmd", "SET_ACTIVITY", allocator);

	rapidjson::Value args(rapidjson::kObjectType);
	args.AddMember("pid", static_cast<uint32_t>(GetCurrentProcessId()), allocator);

	if (presence)
	{
		rapidjson::Value activity(rapidjson::kObjectType);
		AddOptionalString(activity, "state", presence->state, 128, allocator);
		AddOptionalString(activity, "details", presence->details, 128, allocator);

		if (presence->startTimestamp || presence->endTimestamp)
		{
			rapidjson::Value timestamps(rapidjson::kObjectType);
			if (presence->startTimestamp)
				timestamps.AddMember("start", presence->startTimestamp, allocator);
			if (presence->endTimestamp)
				timestamps.AddMember("end", presence->endTimestamp, allocator);
			activity.AddMember("timestamps", timestamps, allocator);
		}

		rapidjson::Value assets(rapidjson::kObjectType);
		AddOptionalString(assets, "large_image", presence->largeImageKey, 32, allocator);
		AddOptionalString(assets, "large_text", presence->largeImageText, 128, allocator);
		AddOptionalString(assets, "small_image", presence->smallImageKey, 32, allocator);
		AddOptionalString(assets, "small_text", presence->smallImageText, 128, allocator);
		if (!assets.ObjectEmpty())
			activity.AddMember("assets", assets, allocator);

		rapidjson::Value party(rapidjson::kObjectType);
		AddOptionalString(party, "id", presence->partyId, 128, allocator);
		if (presence->partySize > 0 && presence->partyMax > 0 && presence->partySize <= presence->partyMax)
		{
			rapidjson::Value size(rapidjson::kArrayType);
			size.PushBack(presence->partySize, allocator);
			size.PushBack(presence->partyMax, allocator);
			party.AddMember("size", size, allocator);
		}
		if (!party.ObjectEmpty())
			activity.AddMember("party", party, allocator);

		rapidjson::Value secrets(rapidjson::kObjectType);
		AddOptionalString(secrets, "match", presence->matchSecret, 128, allocator);
		AddOptionalString(secrets, "join", presence->joinSecret, 128, allocator);
		AddOptionalString(secrets, "spectate", presence->spectateSecret, 128, allocator);
		if (!secrets.ObjectEmpty())
			activity.AddMember("secrets", secrets, allocator);

		activity.AddMember("instance", presence->instance != 0, allocator);
		args.AddMember("activity", activity, allocator);
	}

	document.AddMember("args", args, allocator);
	return SerializeJson(document);
}

std::string CDiscordIpc::BuildEventPayload(const char* command, const char* eventName)
{
	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
	const std::string nonce = NextNonce();

	document.AddMember("nonce", rapidjson::Value(nonce.c_str(), nonce.length(), allocator), allocator);
	document.AddMember("cmd", rapidjson::Value(command, allocator), allocator);
	document.AddMember("evt", rapidjson::Value(eventName, allocator), allocator);
	return SerializeJson(document);
}

std::string CDiscordIpc::BuildJoinReplyPayload(const char* userId, int reply)
{
	rapidjson::Document document;
	document.SetObject();
	rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
	const std::string nonce = NextNonce();
	const char* command = reply == DISCORD_REPLY_YES ? "SEND_ACTIVITY_JOIN_INVITE" : "CLOSE_ACTIVITY_JOIN_REQUEST";

	document.AddMember("cmd", rapidjson::Value(command, allocator), allocator);
	rapidjson::Value args(rapidjson::kObjectType);
	args.AddMember("user_id", rapidjson::Value(userId, allocator), allocator);
	document.AddMember("args", args, allocator);
	document.AddMember("nonce", rapidjson::Value(nonce.c_str(), nonce.length(), allocator), allocator);
	return SerializeJson(document);
}

std::string CDiscordIpc::NextNonce(void)
{
	char nonce[16];
	V_snprintf(nonce, sizeof(nonce), "%u", ++m_nNonce);
	return nonce;
}

#endif // !DEDICATED
