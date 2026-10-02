//=============================================================================//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
// server.cpp: implementation of the CServer class.
//
/////////////////////////////////////////////////////////////////////////////////
#include "core/stdafx.h"
#include "common/protocol.h"
#include "common/proto_oob.h"
#include "common/proto_version.h"
#include "tier0/frametask.h"
#include "tier1/cvar.h"
#include "tier1/strtools.h"
#include "engine/sys_dll2.h"
#include "engine/host.h"
#include "engine/server/sv_main.h"
#include "engine/server/server.h"
#include "engine/shared/entitlements.h"
#include "engine/shared/oob_types.h"
#include "networksystem/pylon.h"
#include "networksystem/bansystem.h"
#include "ebisusdk/EbisuSDK.h"
#include "public/edict.h"
#include "pluginsystem/pluginsystem.h"
#include "game/server/gameinterface.h"

//---------------------------------------------------------------------------------
// Console variables
//---------------------------------------------------------------------------------
ConVar sv_showconnecting("sv_showconnecting", "1", FCVAR_RELEASE, "Logs information about the connecting client to the console");

ConVar sv_globalBanlist("sv_globalBanlist", "1", FCVAR_RELEASE, "Determines whether or not to use the global banned list.", false, 0.f, false, 0.f, "0 = Disable, 1 = Enable.");
ConVar sv_banlistRefreshRate("sv_banlistRefreshRate", "30.0", FCVAR_DEVELOPMENTONLY, "Banned list refresh rate (seconds).", true, 1.f, false, 0.f);

static ConVar sv_validatePersonaName("sv_validatePersonaName", "1", FCVAR_RELEASE, "Validate the client's textual persona name on connect.");
static ConVar sv_minPersonaNameLength("sv_minPersonaNameLength", "4", FCVAR_RELEASE, "The minimum length of the client's textual persona name.", true, 0.f, false, 0.f);
static ConVar sv_maxPersonaNameLength("sv_maxPersonaNameLength", "16", FCVAR_RELEASE, "The maximum length of the client's textual persona name.", true, 0.f, false, 0.f);

//---------------------------------------------------------------------------------
// Purpose: Gets the number of human players on the server
// Output : int
//---------------------------------------------------------------------------------
int CServer::GetNumHumanPlayers(void) const
{
	int nHumans = 0;
	for (int i = 0; i < m_nMaxClients; i++)
	{
		const CClient* const pClient = g_pServer->GetClient(i);

		if (pClient->IsHumanPlayer())
			nHumans++;
	}

	return nHumans;
}

//---------------------------------------------------------------------------------
// Purpose: Gets the number of fake clients on the server
// Output : int
//---------------------------------------------------------------------------------
int CServer::GetNumFakeClients(void) const
{
	int nBots = 0;
	for (int i = 0; i < m_nMaxClients; i++)
	{
		const CClient* const pClient = g_pServer->GetClient(i);

		if (pClient->IsConnected() && pClient->IsFakeClient())
			nBots++;
	}

	return nBots;
}

//---------------------------------------------------------------------------------
// Purpose: Gets the number of clients on the server
// Output : int
//---------------------------------------------------------------------------------
int CServer::GetNumClients(void) const
{
	int nClients = 0;
	for (int i = 0; i < m_nMaxClients; i++)
	{
		const CClient* const pClient = g_pServer->GetClient(i);

		if (pClient->IsConnected())
			nClients++;
	}

	return nClients;
}

CClient* CServer::GetClientByAdr(const netadr_t& netAdr)
{
	if (m_nMaxClients <= 0)
		return nullptr;

	for (int i = 0; i < m_nMaxClients; i++)
	{
		CClient* const cl = &m_Clients[i];

		if (cl->IsFakeClient())
			continue;

		if (!cl->IsConnected())
			continue;

		CNetChan* const chan = cl->GetNetChan();
		Assert(chan);

		const netadr_t& adr = chan->GetRemoteAddress();

		if (!netAdr.ComparePort(adr))
			continue;

		if (!netAdr.CompareAdr(adr))
			continue;

		return cl;
	}

	return nullptr;
}

//---------------------------------------------------------------------------------
// Purpose: Rejects connection request and sends back a message
// Input  : iSocket - 
//			*pNetAdr - 
//			*pFmt - 
//			... - 
//---------------------------------------------------------------------------------
void CServer::RejectConnection(const int iSocket, const netadr_t* const pNetAdr, const char* const pFmt, ...) const
{
	va_list		argptr;
	char	text[1024];

	va_start(argptr, pFmt);
	Q_vsnprintf(text, sizeof(text), pFmt, argptr);
	va_end(argptr);

	v_NET_OutOfBandPrintf(m_Socket, pNetAdr, "%c%s", S2C_CONNREJECT, text);
}

//---------------------------------------------------------------------------------
// Purpose: Initializes a CSVClient for a new net connection. This will only be called
//			once for a player each game, not once for each level change.
// Input  : *pServer - 
//			*pChallenge - 
// Output : pointer to client instance on success, nullptr on failure
//---------------------------------------------------------------------------------
CClient* CServer::ConnectClient(CServer* pServer, user_creds_s* pChallenge)
{
	if (pServer->m_State < server_state_t::ss_active)
		return nullptr;

	char* pszPersonaName = pChallenge->personaName;
	NucleusID_t nNucleusID = pChallenge->personaId;

	const bool bEnableLogging = sv_showconnecting.GetBool();
	const int nPort = int(ntohs(pChallenge->netAdr.GetPort()));

	char szAddresBuffer[128];
	const char* pszAddresBuffer = nullptr;

	if (bEnableLogging)
	{
		// Render the client address once.
		pChallenge->netAdr.ToString(szAddresBuffer, sizeof(szAddresBuffer), true);
		pszAddresBuffer = szAddresBuffer;

		Msg(eDLL_T::SERVER, "Processing connectionless challenge for '[%s]:%i' ('%llu')\n",
			pszAddresBuffer, nPort, nNucleusID);
	}

	bool bValidName = false;

	if (VALID_CHARSTAR(pszPersonaName) &&
		V_IsValidUTF8(pszPersonaName))
	{
		if (sv_validatePersonaName.GetBool() && 
			!IsValidPersonaName(pszPersonaName, sv_minPersonaNameLength.GetInt(), sv_maxPersonaNameLength.GetInt()))
		{
			bValidName = false;
		}
		else
		{
			bValidName = true;
		}
	}

	// Only proceed connection if the client's name is valid and UTF-8 encoded.
	if (!bValidName)
	{
		pServer->RejectConnection(pServer->m_Socket, &pChallenge->netAdr, "#Valve_Reject_Invalid_Name");

		if (bEnableLogging)
		{
			Warning(eDLL_T::SERVER, "Connection rejected for '[%s]:%i' ('%llu' has an invalid name!)\n",
				pszAddresBuffer, nPort, nNucleusID);
		}

		return nullptr;
	}

	if (g_BanSystem.IsBanned(&pChallenge->netAdr, nNucleusID))
	{
		pServer->RejectConnection(pServer->m_Socket, &pChallenge->netAdr, "#Valve_Reject_Banned");

		if (bEnableLogging)
		{
			Warning(eDLL_T::SERVER, "Connection rejected for '[%s]:%i' ('%llu' is banned from this server!)\n",
				pszAddresBuffer, nPort, nNucleusID);
		}

		return nullptr;
	}

	CClient* const pClient = CServer__ConnectClient(pServer, pChallenge);

	for (auto& callback : !PluginSystem()->GetConnectClientCallbacks())
	{
		if (!callback.Function()(pServer, pClient, pChallenge))
		{
			pClient->Disconnect(REP_MARK_BAD, "#Valve_Reject_Banned");
			return nullptr;
		}
	}

	if (pClient && sv_globalBanlist.GetBool())
	{
		if (!pClient->GetNetChan()->GetRemoteAddress().IsLoopback())
		{
			if (!pszAddresBuffer)
			{
				pChallenge->netAdr.ToString(szAddresBuffer, sizeof(szAddresBuffer), true);
				pszAddresBuffer = szAddresBuffer;
			}

			const string addressBufferCopy(pszAddresBuffer);
			const string personaNameCopy(pszPersonaName);

			std::thread th(SV_CheckForBanAndDisconnect, pClient, addressBufferCopy, nNucleusID, personaNameCopy, nPort);
			th.detach();
		}
	}

	return pClient;
}

//---------------------------------------------------------------------------------
// Purpose: Sends netmessage to all active clients
// Input  : *msg       -
//          onlyActive - 
//          reliable   - 
//---------------------------------------------------------------------------------
void CServer::BroadcastMessage(CNetMessage* const msg, const bool onlyActive, const bool reliable)
{
	CServer__BroadcastMessage(this, msg, onlyActive, reliable);
}

//---------------------------------------------------------------------------------
// Purpose: Runs the server frame
// Input  : *pServer - 
//---------------------------------------------------------------------------------
void CServer::RunFrame(CServer* pServer)
{
	CServer__RunFrame(pServer);

	for (auto& callback : !PluginSystem()->GetServerFrameCallbacks())
		callback.Function()(pServer);
}

bool CServer::SpawnServer(CServer* pServer, const char* pszMapName, const char* pszMapGroupName)
{
	const bool bSpawnResult = CServer__SpawnServer(pServer, pszMapName, pszMapGroupName);
	if (bSpawnResult)
	{
		if (::IsDedicated() || (V_strcmp(pszMapName, "mp_lobby") != 0 && V_strcmp(pszMapName, "mp_npe") != 0))
		{
			CClient::CheckMSForNewAuthKey();
		}
	}
	return bSpawnResult;
}

// todo(kawe): use V_StrToNum_Fast here?
static NucleusID_t Server_ParseNucleusID(const char* const value)
{
	Assert(value);

	if (!value)
		return 0;

	NucleusID_t result = 0;

	// Hex value
	if (value[0] == '0' &&	(value[1] == 'x' || value[1] == 'X'))
	{
		const char* current = value + 2;

		for (;; ++current)
		{
			const unsigned char c =	static_cast<unsigned char>(*current);

			if (c >= '0' && c <= '9')
			{
				result = (result * 16) + (c - '0');
			}
			else if (c >= 'a' && c <= 'f')
			{
				result = (result * 16) + (c - 'a' + 10);
			}
			else if (c >= 'A' && c <= 'F')
			{
				result = (result * 16) + (c - 'A' + 10);
			}
			else
			{
				break;
			}
		}

		return result;
	}

	if (value[0] == '\'')
	{
		// Return byte value directly.
		return static_cast<unsigned char>(value[1]);
	}

	// Dec value
	for (const char* current = value; *current >= '0' && *current <= '9'; ++current)
	{
		result = (result * 10) + (*current - '0');
	}

	return result;
}

// security_audit(kawe): helpers here since we now check far more often
// for truncation and desync.

static void Server_RejectTruncConnectPacket(CServer* const sv, const netadr_t& adr)
{
	sv->RejectConnection(sv->GetSocket(), &adr, "Truncated packet in C2S_CONNECT message\n");
}

static void Server_RejectDesyncConnectPacket(CServer* const sv, const netadr_t& adr)
{
	sv->RejectConnection(sv->GetSocket(), &adr, "C2S_CONNECT desync\n");
}

//-----------------------------------------------------------------------------
// Purpose: processes a C2S_CONNECT connectionless packet
//-----------------------------------------------------------------------------
bool CServer::ProcessConnect(const netadr_t& from, bf_read& msg)
{
	Assert(g_hostEntitlements);

	//-------------------------------------------------------------------------
	// Connection protocol.
	//-------------------------------------------------------------------------
	const int clientProtVer = msg.ReadLong();

	if (msg.IsOverflowed())
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	if (clientProtVer != PROTOCOL_VERSION)
	{
		RejectConnection(m_Socket, &from, "Client has network protocol %i, server has network protocol %i\n",
			clientProtVer, PROTOCOL_VERSION);

		return false;
	}

	if (!m_nMaxPlayers)
	{
		RejectConnection(m_Socket, &from, "Server is not running\n");
		return false;
	}

	const i32 protocolVersion = msg.ReadLong();
	const i32 challenge = msg.ReadLong();
	const u32 reservation = msg.ReadULong();

	const HardwareType_e platform = static_cast<HardwareType_e>(msg.ReadByte());

	if (msg.IsOverflowed())
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	if (platform >= HardwareType_e::PLAT_UNSPECIFIED)
	{
		RejectConnection(m_Socket, &from, "Invalid platform.\n");
		return false;
	}

	//-------------------------------------------------------------------------
	// Client identity and server filter.
	//-------------------------------------------------------------------------
	const NucleusID_t personaId = static_cast<NucleusID_t>(msg.ReadLongLong());

	if (msg.IsOverflowed())
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	if (!personaId)
	{
		RejectConnection(m_Socket, &from, "Null client user ID.\n");
		return false;
	}

	char personaName[256];

	if (!msg.ReadString(personaName, sizeof(personaName), false))
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	char serverFilter[256];

	if (!msg.ReadString(serverFilter, sizeof(serverFilter), false))
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	//-------------------------------------------------------------------------
	// Playlist / persistence information.
	//-------------------------------------------------------------------------
	const u64 playlistCRC = msg.ReadULongLong();
	const u64 persistenceVersion = msg.ReadULongLong();
	const u32 persistenceCRC = msg.ReadULong();

	if (msg.IsOverflowed())
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	//-------------------------------------------------------------------------
	// Entitlement state.
	//-------------------------------------------------------------------------
	const u64 entitlementsCRC = static_cast<u64>(msg.ReadLongLong());

	if (msg.IsOverflowed())
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	if (entitlementsCRC != g_hostEntitlements->dataCRC)
	{
		Server_RejectDesyncConnectPacket(this, from);
		return false;
	}

	u64 entitlements = 0;
	msg.ReadBits(&entitlements, 64);

	const int expectedBalanceCount = g_hostEntitlements->balances.Count();

	Assert(expectedBalanceCount >= 0);
	Assert(expectedBalanceCount <= UINT8_MAX);

	if (expectedBalanceCount < 0 ||
		expectedBalanceCount > UINT8_MAX)
	{
		Server_RejectDesyncConnectPacket(this, from);
		return false;
	}

	const unsigned int clientBalanceCount = msg.ReadUBitLong(8);

	if (msg.IsOverflowed())
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	if (clientBalanceCount != static_cast<unsigned int>(expectedBalanceCount))
	{
		Server_RejectDesyncConnectPacket(this, from);
		return false;
	}

	CUtlVector<u16> balances;
	balances.SetCount(expectedBalanceCount);

	for (int i = 0; i < expectedBalanceCount; ++i)
	{
		balances[i] = msg.ReadWord();
	}

	if (msg.IsOverflowed())
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	//-------------------------------------------------------------------------
	// Miscellaneous connection flags.
	//-------------------------------------------------------------------------
	const bool requestedSpectator = msg.ReadOneBit() != 0;
	const bool lowViolence = msg.ReadOneBit() != 0;
	const bool trial = msg.ReadOneBit() != 0;
	const bool forceEAAccess = msg.ReadOneBit() != 0;
	const bool hasOriginToken = msg.ReadOneBit() != 0;

	if (msg.IsOverflowed())
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	//-------------------------------------------------------------------------
	// note(kawe): the original consumes the Origin token here, but does not
	// use it. Should we just drop it here since we send auth token and sig?
	//-------------------------------------------------------------------------
	char originToken[1024] = {};

	if (hasOriginToken)
	{
		if (!msg.ReadString(originToken, sizeof(originToken), false))
		{
			Server_RejectTruncConnectPacket(this, from);
			return false;
		}
	}

	//-------------------------------------------------------------------------
	// Read/write synchronization marker.
	//-------------------------------------------------------------------------
	const u32 sentinel = msg.ReadULong();

	if (msg.IsOverflowed() || sentinel != CONNECTIONLESS_SENTINEL)
	{
		RejectConnection(m_Socket, &from, "Read/write desync in C2S_CONNECT message\n");
		return false;
	}

	//-------------------------------------------------------------------------
	// Client userinfo ConVars.
	//-------------------------------------------------------------------------

	// note(kawe): this can be at most 255. If you ever increase it, you need
	// to check for sizes before EnsureCapacity() !!!
	const u8 userInfoCount = msg.ReadByte();
	// I will come after you if you don't.

	if (msg.IsOverflowed())
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	NET_SetConVar::Vec_t userInfo;
	userInfo.EnsureCapacity(userInfoCount);

	for (unsigned int i = 0; i < userInfoCount; ++i)
	{
		NET_SetConVar::Cvar_s entry = {};
		const unsigned int nameIndex = msg.ReadUBitLong(6);

		if (msg.IsOverflowed())
		{
			Server_RejectTruncConnectPacket(this, from);
			return false;
		}

		if (nameIndex)
		{
			// security_audit(kawe): the original indexed the fixed userinfo
			// name table without validating the networked index.
			if (nameIndex >= V_ARRAYSIZE(g_userInfoConVarNames))
			{
				Server_RejectDesyncConnectPacket(this, from);
				return false;
			}

			strncpy(entry.name, g_userInfoConVarNames[nameIndex], sizeof(entry.name));
			entry.name[sizeof(entry.name) - 1] = '\0';
		}
		else
		{
			if (!msg.ReadString(entry.name, sizeof(entry.name), false))
			{
				Server_RejectTruncConnectPacket(this, from);
				return false;
			}
		}

		if (!msg.ReadString(entry.value, sizeof(entry.value), false))
		{
			Server_RejectTruncConnectPacket(this, from);
			return false;
		}

		userInfo.AddToTail(entry);
	}

	if (msg.IsOverflowed())
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	//-------------------------------------------------------------------------
	// Origin token connections require the generic userinfo block to contain
	// nucleus_id and nucleus_pid values matching the token-side identity.
	//
	// In this protocol revision the recovered expected values are both zero.
	//-------------------------------------------------------------------------
	if (hasOriginToken)
	{
		bool foundNucleusId = false;
		bool foundNucleusPid = false;
		bool invalidNucleusCvars = false;

		for (int i = 0; i < userInfo.Count(); ++i)
		{
			NET_SetConVar::Cvar_s& entry = userInfo[i];

			if (!foundNucleusId && V_strcmp(entry.name, "nucleus_id") == 0)
			{
				foundNucleusId = true;

				if (Server_ParseNucleusID(entry.value) != 0)
				{
					invalidNucleusCvars = true;
					break;
				}
			}
			else if (!foundNucleusPid && V_strcmp(entry.name, "nucleus_pid") == 0)
			{
				foundNucleusPid = true;

				if (Server_ParseNucleusID(entry.value) != 0)
				{
					invalidNucleusCvars = true;
					break;
				}
			}
		}

		if (invalidNucleusCvars || !foundNucleusId || !foundNucleusPid)
		{
			//SendStats("originToken.connectReject.invalidNucleusCvars:1|c\n");
			RejectConnection(m_Socket, &from, "Origin token: invalid_nucleus_cvars\n");

			return false;
		}

		// security_audit(kawe): the original uses an uninitialized balance vector
		// for the Origin token SVC_DLCNotifyOwnership path, so zero it out here.
		entitlements = 0;

		if (balances.Count() > 0)
		{
			memset(balances.Base(),	0, balances.Count() * sizeof(u16));
		}
	}

	if (msg.IsOverflowed())
	{
		Server_RejectTruncConnectPacket(this, from);
		return false;
	}

	//-------------------------------------------------------------------------
	// Construct the extended credentials blob expected by ConnectClient().
	//-------------------------------------------------------------------------
	user_creds_s creds = {};

	creds.netAdr = from;
	creds.protocolVer = protocolVersion;
	creds.challenge = challenge;
	creds.reservation = reservation;
	creds.personaId = personaId;
	creds.personaName = personaName;

	creds.serverFilter = serverFilter;
	creds.entitlements = entitlements;
	creds.balances = &balances;

	creds.hasOriginToken = hasOriginToken;

	creds.spectatorClient = sv_allowSpectatorClients->GetBool() &&
		!m_bResetMaxTeams &&
		requestedSpectator;

	creds.lowViolence = lowViolence;
	creds.trial = trial;
	creds.forceEAAccess = forceEAAccess;

	creds.platform = platform;

	creds.playlistCRC = playlistCRC;
	creds.persistenceVersion = persistenceVersion;
	creds.persistenceCRC = persistenceCRC;

	creds.userInfo = &userInfo;

	if (!ConnectClient(this, &creds))
	{
		//SendStats("dedi.connect.fail.total:1|c\n");
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: returns an existing challenge for a client or creates a new one
//-----------------------------------------------------------------------------
int CServer::GetOrCreateChallenge(const netpacket_t* const packet, const NucleusID_t nucleusId)
{
	Assert(packet);

	const netadr_t& address = packet->from;
	const int challengeCount =	m_ConnectionChallenges.Count();

	int oldestChallengeIndex = 0;
	float oldestChallengeTime = FLT_MAX;

	for (int i = 0; i < challengeCount; ++i)
	{
		ServerChallenge_s& entry = m_ConnectionChallenges[i];

		if (entry.nucleusId != nucleusId)
			continue;

		const bool addressMatches = address == entry.address;

		if (addressMatches)
		{
			entry.lastRequestTime = static_cast<float>(*g_pNetTime);
			return static_cast<int>(entry.challenge);
		}

		// Only consider challenges for this nucleus ID.
		if (entry.lastRequestTime < oldestChallengeTime)
		{
			oldestChallengeTime = entry.lastRequestTime;
			oldestChallengeIndex = i;
		}
	}

	if (challengeCount >= MAX_SERVER_CONNECT_CHALLENGES)
		m_ConnectionChallenges.FastRemove(oldestChallengeIndex);

	const int newIndex = m_ConnectionChallenges.AddToTail();
	ServerChallenge_s& newEntry = m_ConnectionChallenges[newIndex];

	const u32 challengeHigh = static_cast<u32>(RandomInt(0, 4095)) << 16;
	const u32 challengeLow = static_cast<u32>(RandomInt(0, 65535));

	newEntry.challenge = challengeHigh | challengeLow;

	newEntry.nucleusId = nucleusId;
	newEntry.address = address;

	newEntry.lastRequestTime = static_cast<float>(*g_pNetTime);
	return static_cast<int>(newEntry.challenge);
}

//-----------------------------------------------------------------------------
// Purpose: processes a C2S challenge request and sends the challenge response
//-----------------------------------------------------------------------------
bool CServer::ProcessChallenge(netpacket_t* const packet, bf_read& msg)
{
	Assert(packet);
	Assert(msg);

	char commandName[16] = {};
	msg.ReadString(commandName, sizeof(commandName));

	const NucleusID_t nucleusId = static_cast<NucleusID_t>(msg.ReadULongLong());
	const int challenge = GetOrCreateChallenge(packet, nucleusId);

	ALIGN4 u8 challengeBuffer[512];
	bf_write challengeMsg(challengeBuffer, sizeof(challengeBuffer));

	challengeMsg.WriteULong(CONNECTIONLESS_HEADER);
	challengeMsg.WriteByte(S2C_CHALLENGE);
	challengeMsg.WriteLong(challenge);
	challengeMsg.WriteULongLong(nucleusId);
	challengeMsg.WriteString(commandName);

	if (strcmp(commandName, "connect") == 0)
		challengeMsg.WriteLong(v_Sys_GetHostVersion());

	Assert(!challengeMsg.IsOverflowed());
	v_NET_SendPacket(
		nullptr,
		packet->source,
		packet->from,
		challengeMsg.GetData(),
		static_cast<unsigned int>(challengeMsg.GetNumBytesWritten()),
		nullptr,
		false,
		nullptr,
		packet->source != NS_RESERVED);

	return true;
}

bool CServer::ProcessDataBlockAck(const netadr_t& from, bf_read& msg)
{
	const u16 blockSeqNr = msg.ReadWord();
	const u16 cancelSeqNr = msg.ReadWord();

	CClient* const cl = GetClientByAdr(from);

	if (!cl)
		return false;

	if (!cl->IsConnected())
		return false;

	NetDataBlockSender* const sender = cl->GetDataBlockSender();

	if (!sender->IsTransfering())
		return false;

	Assert(sender->GetFragmentCount() > 0);
	Assert(sender->GetFragmentCount() <= MAX_DATABLOCK_FRAGMENTS);

	NetDataBlockSender::FragAck_s ackMsg{};

	ackMsg.blockSeqNr = blockSeqNr;
	ackMsg.cancelSeqNr = cancelSeqNr;

	// Zero here means no fragment bitmap follows.
	const bool hasFragmentAcks = msg.ReadOneBit() != 0;

	if (hasFragmentAcks)
	{
		const int numFrags = sender->GetFragmentCount();

		for (int fragId = 0; fragId < numFrags; ++fragId)
		{
			if (!msg.ReadOneBit())
				continue;

			ackMsg.ackedFragments[fragId >> 6] |= 1ull << (fragId & 63);
		}
	}

	CNetChan* const chan = cl->GetNetChan();

	if (!chan)
		return false;

	chan->SetLastReceivedTime(*g_pNetTime);
	sender->ProcessFragmentAcks(ackMsg);

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: process an out-of-band packet received by the server
//-----------------------------------------------------------------------------
bool CServer::ProcessConnectionlessPacketGuts(netpacket_t* const packet)
{
	// NOTE: msg is copy-constructed from packet->message, so reading
	// from "msg" will not advance read-pointer in "packet->message"!!!
	// ... and vice-versa, hence passing "packet" to a nested function
	// will make that function receive the original unprocessed message.
	// [ this differs from client-side connectionless packet processing ]
	bf_read msg = packet->message;
	const u8 id = msg.ReadByte();

	if (!IsActive() || !v_Host_ShouldRun())
	{
		// Server should not be processing most of the traffic in this
		// state except client LAN searches

		if (IsDedicated())
			return true;

		if (id != 0)
			return true;
	}

	switch (id)
	{
	case C2S_CONNECT:
	{
		return ProcessConnect(packet->from, msg);
	}

	case C2S_CHALLENGE:
	{
		return ProcessChallenge(packet, msg);
	}

	case S2C_CHALLENGE: // todo(kawe): why is this a thing?
	{
		msg.ReadLong();
		msg.ReadLongLong();

		char challengeData[256];
		msg.ReadString(challengeData, sizeof(challengeData));

		if (msg.IsOverflowed())
			RejectConnection(packet->source, &packet->from, "Invalid challenge packet");

		break;
	}

	case C2S_DATABLOCK_ACK:
	{
		return ProcessDataBlockAck(packet->from, msg);
	}

	default:
		break;
	}

	return true;
}

bool CServer::_ProcessConnectionlessPacket(CServer* const server, netpacket_t* const packet)
{
	return server->ProcessConnectionlessPacketGuts(packet);
}

///////////////////////////////////////////////////////////////////////////////
void VServer::Detour(const bool bAttach) const
{
	DetourSetup(&CServer__SpawnServer, &CServer::SpawnServer, bAttach);
	DetourSetup(&CServer__RunFrame, &CServer::RunFrame, bAttach);
	DetourSetup(&CServer__ConnectClient, &CServer::ConnectClient, bAttach);
	DetourSetup(&CServer__ProcessConnectionlessPacket, &CServer::_ProcessConnectionlessPacket, bAttach);
}

///////////////////////////////////////////////////////////////////////////////
CServer* g_pServer = nullptr;
CClientExtended CServer::sm_ClientsExtended[MAX_PLAYERS];

