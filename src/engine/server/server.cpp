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
#include "tier0/frametask.h"
#include "tier1/cvar.h"
#include "tier1/strtools.h"
#include "engine/host.h"
#include "engine/server/sv_main.h"
#include "engine/server/server.h"
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

CClient* CServer::GetClientByAdr(const netadr_t* const netAdr)
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

		if (!netAdr->ComparePort(adr))
			continue;

		if (!netAdr->CompareAdr(adr))
			continue;

		return cl;
	}

	return nullptr;
}

//---------------------------------------------------------------------------------
// Purpose: Rejects connection request and sends back a message
// Input  : iSocket - 
//			*pChallenge - 
//			*szMessage - 
//---------------------------------------------------------------------------------
void CServer::RejectConnection(int iSocket, netadr_t* pNetAdr, const char* szMessage)
{
	CServer__RejectConnection(this, iSocket, pNetAdr, szMessage);
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
	const char id = static_cast<char>(msg.ReadByte());

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
		CServer__ProcessC2S_Connect(this, &msg, &packet->from);
		break;
	}

	case C2S_CHALLENGE:
	{
		CServer__ProcessC2S_Challenge(this, packet->source,	packet,	&msg, packet->source != NS_RESERVED);
		break;
	}

	case S2C_CHALLENGE:
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
		const u16 blockSeqNr = static_cast<u16>(msg.ReadWord());
		const u16 cancelSeqNr = static_cast<u16>(msg.ReadWord());

		CClient* const cl = GetClientByAdr(&packet->from);

		if (!cl)
			break;

		if (!cl->IsConnected())
			break;

		NetDataBlockSender* const sender = cl->GetDataBlockSender();

		if (!sender->IsTransfering())
			break;

		Assert(sender->m_totalFragments > 0);
		Assert(sender->m_totalFragments <= MAX_DATABLOCK_FRAGMENTS);

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
			break;

		chan->SetLastReceivedTime(*g_pNetTime);
		sender->ProcessFragmentAcks(ackMsg);

		break;
	}

	default:
		break;
	}

	return true;
}

static bool Server_ProcessConnectionlessPacket(CServer* const server, netpacket_t* const packet)
{
	return server->ProcessConnectionlessPacketGuts(packet);
}

///////////////////////////////////////////////////////////////////////////////
void VServer::Detour(const bool bAttach) const
{
	DetourSetup(&CServer__SpawnServer, &CServer::SpawnServer, bAttach);
	DetourSetup(&CServer__RunFrame, &CServer::RunFrame, bAttach);
	DetourSetup(&CServer__ConnectClient, &CServer::ConnectClient, bAttach);
	DetourSetup(&CServer__ProcessConnectionlessPacket, &Server_ProcessConnectionlessPacket, bAttach);
}

///////////////////////////////////////////////////////////////////////////////
CServer* g_pServer = nullptr;
CClientExtended CServer::sm_ClientsExtended[MAX_PLAYERS];

