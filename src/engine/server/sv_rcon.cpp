//===========================================================================//
// 
// Purpose: Implementation of the rcon server.
// 
//===========================================================================//

#include "core/stdafx.h"
#include "tier0/commandline.h"
#include "tier1/cvar.h"
#include "tier1/NetAdr.h"
#include "tier2/socketcreator.h"
#include "engine/cmd.h"
#include "engine/net.h"
#include "engine/shared/shared_rcon.h"
#include "engine/server/sv_rcon.h"
#include "protoc/netcon.pb.h"
#include "common/igameserverdata.h"
#include "mbedtls/include/mbedtls/sha512.h"
#include "mbedtls/aes.h"
#include "mbedtls/ctr_drbg.h"

//-----------------------------------------------------------------------------
// Purpose: constants
//-----------------------------------------------------------------------------
static const char s_NoAuthMessage[]  = "This server is password protected for console access; authenticate with 'PASS <password>' command.\n";
static const char s_WrongPwMessage[] = "Admin password incorrect.\n";
static const char s_AuthMessage[]    = "Authentication successful.\n";
static const char s_BannedMessage[]  = "Go away.\n";

//-----------------------------------------------------------------------------
// Purpose: console variables
//-----------------------------------------------------------------------------
static void RCON_PasswordChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData);
static void RCON_WhiteListAddresChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData);
static void RCON_ConnectionCountChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData);
static void RCON_UseLoopbackSocketChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData);

static ConVar sv_rcon_password("sv_rcon_password", "", FCVAR_RELEASE, "Remote server access password (rcon server is disabled if empty)", &RCON_PasswordChanged_f);
static ConVar sv_rcon_sendlogs("sv_rcon_sendlogs", "0", FCVAR_RELEASE, "Network console logs to connected and authenticated sockets");
//static ConVar sv_rcon_banpenalty("sv_rcon_banpenalty" , "10", FCVAR_RELEASE, "Number of minutes to ban users who fail rcon authentication");

static ConVar sv_rcon_maxfailures("sv_rcon_maxfailures", "10", FCVAR_RELEASE, "Max number of times an user can fail rcon authentication before being banned", true, 1.f, false, 0.f);
static ConVar sv_rcon_maxignores("sv_rcon_maxignores", "15", FCVAR_RELEASE, "Max number of times an user can ignore the instruction message before being banned", true, 1.f, false, 0.f);
static ConVar sv_rcon_maxsockets("sv_rcon_maxsockets", "32", FCVAR_RELEASE, "Max number of accepted sockets before the server starts closing redundant sockets", true, 1.f, true, MAX_PLAYERS);

static ConVar sv_rcon_maxconnections("sv_rcon_maxconnections", "1", FCVAR_RELEASE, "Max number of authenticated connections before the server closes the listen socket", true, 1.f, true, MAX_PLAYERS, &RCON_ConnectionCountChanged_f);
static ConVar sv_rcon_whitelistaddress("sv_rcon_whitelistaddress", "", FCVAR_RELEASE, "This address is not considered a 'redundant' socket and will never be banned for failed authentication attempts", &RCON_WhiteListAddresChanged_f, "Format: '::ffff:127.0.0.1'");

static ConVar sv_rcon_useloopbacksocket("sv_rcon_useloopbacksocket", "0", FCVAR_RELEASE, "Whether to bind rcon server to the loopback socket", &RCON_UseLoopbackSocketChanged_f);

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CRConServer::CRConServer(void) : CNetConBase(true)
	, m_nConnIndex(0)
	, m_nAuthConnections(0)
	, m_bInitialized(false)
	, m_bSocketFailure(false)
{
	memset(m_PasswordHash, 0, sizeof(m_PasswordHash));
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CRConServer::~CRConServer(void)
{
	// NOTE: do not call Shutdown() from the destructor as the OS's socket
	// system would be shutdown by then, call Shutdown() in application
	// shutdown code instead
}

//-----------------------------------------------------------------------------
// Purpose: NETCON systems init
//-----------------------------------------------------------------------------
void CRConServer::Init(const char* pPassword, const char* pNetKey)
{
	if (!m_bInitialized)
	{
		SetKey(pNetKey);

		if (!SetPassword(pPassword))
		{
			return;
		}
	}
	else
	{
		// Already initialized.
		return;
	}

	const char* pszAddress = sv_rcon_useloopbacksocket.GetBool() ? NET_IPV6_LOOPBACK : NET_IPV6_UNSPEC;
	const string addressFormatted = Format("[%s]:%i", pszAddress, hostport->GetInt());

	if (!m_Address.SetFromString(addressFormatted.c_str(), true))
	{
		Error(eDLL_T::SERVER, 0, "Internal failure while initializing remote server access address ('%s')\n", addressFormatted.c_str());
		return;
	}
	
	m_bSocketFailure = !m_Socket.CreateListenSocket(m_Address);

	if (!m_bSocketFailure)
	{
		Msg(eDLL_T::SERVER, "Remote server access initialized ('%s') with key %s'%s%s%s'\n",
			m_Address.ToString(), g_svReset.c_str(), g_svGreyB.c_str(), GetKey(), g_svReset.c_str());
	}

	m_bInitialized = true;
}

//-----------------------------------------------------------------------------
// Purpose: NETCON systems shutdown
//-----------------------------------------------------------------------------
void CRConServer::Shutdown(void)
{
	if (!m_bInitialized)
	{
		// If we aren't initialized, we shouldn't have any connections at all.
		Assert(!m_Socket.GetAcceptedSocketCount(), "Accepted connections while RCON server isn't initialized!");
		Assert(!m_Socket.IsListening(), "Listen socket active while RCON server isn't initialized!");

		return;
	}

	const int nConnCount = m_Socket.GetAcceptedSocketCount();
	m_Socket.CloseAllAcceptedSockets();

	if (m_Socket.IsListening())
	{
		m_Socket.CloseListenSocket();
	}

	m_BannedList.clear();

	Msg(eDLL_T::SERVER, "Remote server access deinitialized (%i accepted sockets closed)\n", nConnCount);
	m_bInitialized = false;
}

//-----------------------------------------------------------------------------
// Purpose: reboots the RCON server if initialized
//-----------------------------------------------------------------------------
void CRConServer::Reboot(void)
{
	if (RCONServer()->IsInitialized())
	{
		Msg(eDLL_T::SERVER, "Rebooting RCON server...\n");
		RCONServer()->Shutdown();
		RCONServer()->Init(sv_rcon_password.GetString(), RCONServer()->GetKey());
	}
}

//-----------------------------------------------------------------------------
// Purpose: run tasks for the RCON server
//-----------------------------------------------------------------------------
void CRConServer::Think(void)
{
	const int nCount = m_Socket.GetAcceptedSocketCount();

	// Close redundant sockets if there are too many except for whitelisted and authenticated.
	if (nCount > sv_rcon_maxsockets.GetInt())
	{
		for (m_nConnIndex = nCount - 1; m_nConnIndex >= 0; m_nConnIndex--)
		{
			const netadr_t& netAdr = m_Socket.GetAcceptedSocketAddress(m_nConnIndex);
			if (!netAdr.CompareAdr(m_WhiteListAddress))
			{
				const ConnectedNetConsoleData_s& data = m_Socket.GetAcceptedSocketData(m_nConnIndex);
				if (!data.authorized)
				{
					Disconnect("redundant");
				}
			}
		}
	}

	// Create a new listen socket if authenticated connection is closed.
	if (nCount == 0 && !m_bSocketFailure)
	{
		if (!m_Socket.IsListening())
		{
			m_bSocketFailure = !m_Socket.CreateListenSocket(m_Address);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: hashes provided password using sha-512
// Input  : *password - 
//			length - 
//			*hashOut - buf must be as large as RCON_SHA512_HASH_SIZE
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
static bool RCONServer_HashPassword(const char* const password, const size_t length, u8* const hashOut)
{
	const int nHashRet = mbedtls_sha512(reinterpret_cast<const u8*>(password), length, hashOut, NULL);

	if (nHashRet == 0)
	{
		return true;
	}

	if (rcon_debug.GetBool())
	{
		Error(eDLL_T::SERVER, 0, "SHA-512 algorithm failed on RCON password \"%s\" [%i]\n", password, nHashRet);
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: changes the password
// Input  : *pszPassword - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CRConServer::SetPassword(const char* pszPassword)
{
	const size_t nLen = strlen(pszPassword);
	if (nLen < RCON_MIN_PASSWORD_LEN)
	{
		if (nLen > NULL)
		{
			Warning(eDLL_T::SERVER, "Remote server access requires a password of at least %i characters\n",
				RCON_MIN_PASSWORD_LEN);
		}

		Shutdown();
		return false;
	}

	// This is here so we only print the confirmation message if the user
	// actually requested to change the password rather than initializing
	// the RCON server
	const bool wasInitialized = m_bInitialized;

	m_bInitialized = false;
	m_Socket.CloseAllAcceptedSockets();

	if (!RCONServer_HashPassword(pszPassword, nLen, m_PasswordHash))
	{
		// Note: inverted check because if this is cvar is set, the func
		// RCONServer_HashPassword will print out a more detailed error.
		// We want to inform the user either way that the hashing of the
		// provided password has failed.
		if (!rcon_debug.GetBool())
		{
			Error(eDLL_T::SERVER, 0, "Failed to hash RCON server password\n");
		}

		if (m_Socket.IsListening())
		{
			m_Socket.CloseListenSocket();
		}

		return false;
	}

	if (wasInitialized)
	{
		Msg(eDLL_T::SERVER, "Successfully changed RCON server password\n");
	}

	m_bInitialized = true;
	m_bSocketFailure = false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: sets the white list address
// Input  : *pszAddress - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CRConServer::SetWhiteListAddress(const char* pszAddress)
{
	return m_WhiteListAddress.SetFromString(pszAddress, true);
}

//-----------------------------------------------------------------------------
// Purpose: server RCON main loop (run this every frame)
//-----------------------------------------------------------------------------
void CRConServer::RunFrame(void)
{
	if (m_bInitialized)
	{
		m_Socket.RunFrame();
		Think();

		const int nCount = m_Socket.GetAcceptedSocketCount();
		for (m_nConnIndex = nCount - 1; m_nConnIndex >= 0; m_nConnIndex--)
		{
			ConnectedNetConsoleData_s& data = m_Socket.GetAcceptedSocketData(m_nConnIndex);

			if (CheckForBan(data))
			{
				SendEncoded(data, s_BannedMessage, sizeof(s_BannedMessage)-1, "", 0,
					netcon::response_e::SERVERDATA_RESPONSE_AUTH, int(eDLL_T::NETCON));

				Disconnect("banned");
				continue;
			}

			Recv(data, (u32)rcon_maxframesize.GetInt());
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: send message to all connected sockets
// Input  : *pMsgBuf - 
//			nMsgLen - 
// Output: true on success, false otherwise
//-----------------------------------------------------------------------------
bool CRConServer::SendToAll(const byte* pMsgBuf, const u32 nMsgLen) const
{
	const int nCount = m_Socket.GetAcceptedSocketCount();
	bool bSuccess = true;

	for (int i = nCount - 1; i >= 0; i--)
	{
		const ConnectedNetConsoleData_s& data = m_Socket.GetAcceptedSocketData(i);

		if (data.authorized && !data.inputOnly)
		{
			const int ret = ::send(data.socket, (const char*)pMsgBuf, (i32)nMsgLen, MSG_NOSIGNAL);

			if (ret == SOCKET_ERROR)
			{
				if (!bSuccess)
				{
					bSuccess = false;
				}
			}
		}
	}

	return bSuccess;
}

//-----------------------------------------------------------------------------
// Purpose: encode and send message to all connected sockets
// Input  : *pResponseMsg - 
//			nResponseMsgLen - 
//			*pResponseVal - 
//			nResponseValLen - 
//			responseType - 
//			nMessageId - 
//			nMessageType - 
// Output: true on success, false otherwise
//-----------------------------------------------------------------------------
bool CRConServer::SendEncoded(const char* pResponseMsg, const size_t nResponseMsgLen, const char* pResponseVal, const size_t nResponseValLen,
	const netcon::response_e responseType, const int nMessageId, const int nMessageType)
{
	const int nCount = m_Socket.GetAcceptedSocketCount();
	bool bSuccess = true;

	for (int i = nCount - 1; i >= 0; i--)
	{
		ConnectedNetConsoleData_s& data = m_Socket.GetAcceptedSocketData(i);

		if (!data.authorized || data.inputOnly)
			continue;

		vector<byte> vecMsg;

		if (!Serialize(data, vecMsg, pResponseMsg, nResponseMsgLen, pResponseVal, nResponseValLen,
			responseType, nMessageId, nMessageType))
		{
			Error(eDLL_T::SERVER, NO_ERROR, "Failed to serialize RCON message for socket #i\n", i);
			continue;
		}

		const int ret = ::send(data.socket, (const char*)vecMsg.data(), (i32)vecMsg.size(), MSG_NOSIGNAL);

		if (ret == SOCKET_ERROR)
		{
			Error(eDLL_T::SERVER, NO_ERROR, "Failed to send RCON message to socket #i: (%s)\n", i, "SOCKET_ERROR");

			if (!bSuccess)
			{
				bSuccess = false;
			}
		}
	}

	return bSuccess;
}

//-----------------------------------------------------------------------------
// Purpose: encode and send message to specific socket
// Input  : &data - 
//			*pResponseMsg - 
//			nResponseMsgLen - 
//			*pResponseVal - 
//			nResponseValLen - 
//			responseType - 
//			nMessageId - 
//			nMessageType - 
// Output: true on success, false otherwise
//-----------------------------------------------------------------------------
bool CRConServer::SendEncoded(ConnectedNetConsoleData_s& data,
	const char* pResponseMsg, const size_t nResponseMsgLen, const char* pResponseVal, const size_t nResponseValLen,
	const netcon::response_e responseType, const int nMessageId, const int nMessageType) const
{
	vector<byte> vecMsg;
	if (!Serialize(data, vecMsg, pResponseMsg, nResponseMsgLen, pResponseVal, nResponseValLen,
		responseType, nMessageId, nMessageType))
	{
		return false;
	}
	if (!Send(data.socket, vecMsg.data(), u32(vecMsg.size())))
	{
		Error(eDLL_T::SERVER, NO_ERROR, "Failed to send RCON message: (%s)\n", "SOCKET_ERROR");
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: serializes input
// Input  : &data - 
//			&vecBuf - 
//			*responseMsg - 
//			nResponseMsgLen - 
//			*responseVal - 
//			nResponseValLen - 
//			responseType - 
//			nMessageId - 
//			nMessageType - 
// Output : serialized results as string
//-----------------------------------------------------------------------------
bool CRConServer::Serialize(ConnectedNetConsoleData_s& data, vector<byte>& vecBuf,
	const char* pResponseMsg, const size_t nResponseMsgLen, const char* pResponseVal, const size_t nResponseValLen,
	const netcon::response_e responseType, const int nMessageId, const int nMessageType) const
{
	return NetconServer_Serialize(this, data, vecBuf, pResponseMsg, nResponseMsgLen, pResponseVal, nResponseValLen, responseType, nMessageId, nMessageType,
		rcon_encryptframes.GetBool(), rcon_debug.GetBool());
}

//-----------------------------------------------------------------------------
// Purpose: authenticate new connections
// Input  : &request - 
//			&data - 
//-----------------------------------------------------------------------------
void CRConServer::Authenticate(const netcon::request& request, ConnectedNetConsoleData_s& data)
{
	if (data.authorized)
	{
		return;
	}

	// Authorize.
	if (Comparator(request.requestmsg()))
	{
		data.authorized = true;
		if (++m_nAuthConnections >= sv_rcon_maxconnections.GetInt())
		{
			m_Socket.CloseListenSocket();
			CloseNonAuthConnection();
		}

		const char* const pSendLogs = (!sv_rcon_sendlogs.GetBool() || data.inputOnly) ? "0" : "1";

		SendEncoded(data, s_AuthMessage, sizeof(s_AuthMessage)-1, pSendLogs, 1,
			netcon::response_e::SERVERDATA_RESPONSE_AUTH, static_cast<int>(eDLL_T::NETCON));
	}
	else // Bad password.
	{
		const netadr_t& netAdr = m_Socket.GetAcceptedSocketAddress(m_nConnIndex);
		if (rcon_debug.GetBool())
		{
			Msg(eDLL_T::SERVER, "Bad RCON password attempt from '%s'\n", netAdr.ToString());
		}

		SendEncoded(data, s_WrongPwMessage, sizeof(s_WrongPwMessage)-1, "", 0,
			netcon::response_e::SERVERDATA_RESPONSE_AUTH, static_cast<int>(eDLL_T::NETCON));

		data.authorized = false;
		data.validated = false;
		data.numFailedAttempts++;
	}
}

//-----------------------------------------------------------------------------
// Purpose: sha512 hashed password comparison
// Input  : &svPassword - 
// Output : true if matches, false otherwise
//-----------------------------------------------------------------------------
bool CRConServer::Comparator(const string& svPassword) const
{
	u8 clientPasswordHash[RCON_SHA512_HASH_SIZE];

	if (!RCONServer_HashPassword(svPassword.c_str(), svPassword.length(), clientPasswordHash))
	{
		return false;
	}

	if (memcmp(clientPasswordHash, m_PasswordHash, RCON_SHA512_HASH_SIZE) == 0)
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: processes received message
// Input  : &data - 
//			nMaxLen - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CRConServer::ProcessMessage(ConnectedNetConsoleData_s& data, const u32 nMaxLen)
{
	netcon::request request;

	if (!NetconShared_UnpackEnvelope(this, data, nMaxLen, &request, rcon_encryptframes.GetBool(), rcon_debug.GetBool()))
	{
		Disconnect("received invalid message");
		return false;
	}

	if (!data.authorized &&
		request.requesttype() != netcon::request_e::SERVERDATA_REQUEST_AUTH)
	{
		// Notify netconsole that authentication is required.
		SendEncoded(data, s_NoAuthMessage, sizeof(s_NoAuthMessage)-1, "", 0,
			netcon::response_e::SERVERDATA_RESPONSE_AUTH, static_cast<int>(eDLL_T::NETCON));

		data.validated = false;
		data.numIgnoredMessage++;
		return true;
	}
	switch (request.requesttype())
	{
		case netcon::request_e::SERVERDATA_REQUEST_AUTH:
		{
			Authenticate(request, data);
			break;
		}
		case netcon::request_e::SERVERDATA_REQUEST_EXECCOMMAND:
		{
			if (data.authorized) // Only execute if auth was successful.
			{
				Execute(request);
			}
			break;
		}
		case netcon::request_e::SERVERDATA_REQUEST_SEND_CONSOLE_LOG:
		{
			if (data.authorized)
			{
				// request value "0" means the netconsole is input only.
				const bool bWantLog = atoi(request.requestval().c_str()) != NULL;

				data.inputOnly = !bWantLog;
				if (bWantLog && !sv_rcon_sendlogs.GetBool())
				{
					// Toggle it on since there's at least 1 netconsole that
					// wants to receive logs.
					sv_rcon_sendlogs.SetValue(bWantLog);
				}
			}
			break;
		}
		default:
		{
			break;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: execute commands issued from netconsole (ignores all protection flags)
// Input  : &request - 
//-----------------------------------------------------------------------------
void CRConServer::Execute(const netcon::request& request) const
{
	Cmd_ExecuteUnrestricted(request.requestmsg().c_str(), request.requestval().c_str());
}

//-----------------------------------------------------------------------------
// Purpose: checks for amount of failed attempts and bans netconsole accordingly
// Input  : &data - 
//-----------------------------------------------------------------------------
bool CRConServer::CheckForBan(ConnectedNetConsoleData_s& data)
{
	if (data.validated)
	{
		return false;
	}

	const netadr_t& netAdr = m_Socket.GetAcceptedSocketAddress(m_nConnIndex);

	if (m_BannedList.size() >= RCON_MAX_BANNEDLIST_SIZE)
	{
		const char* pszWhiteListAddress = sv_rcon_whitelistaddress.GetString();
		if (!pszWhiteListAddress[0])
		{
			Warning(eDLL_T::SERVER, "Banned list overflowed, please use a whitelist address; remote server access shutting down...\n");
			Shutdown();

			return true;
		}

		// Only allow whitelisted at this point.
		if (!netAdr.CompareAdr(m_WhiteListAddress))
		{
			if (rcon_debug.GetBool())
			{
				Warning(eDLL_T::SERVER, "Banned list is full, dropping '%s'\n", netAdr.ToString(true));
			}

			return true;
		}
	}

	data.validated = true;

	// Check if IP is in the banned list.
	if (m_BannedList.find(netAdr.GetIP()) != m_BannedList.end())
	{
		return true;
	}

	// Check if netconsole has reached maximum number of attempts > add to banned list.
	if (data.numFailedAttempts >= sv_rcon_maxfailures.GetInt()
		|| data.numIgnoredMessage >= sv_rcon_maxignores.GetInt())
	{
		// Don't add white listed address to banned list.
		if (netAdr.CompareAdr(m_WhiteListAddress))
		{
			data.numFailedAttempts = 0;
			data.numIgnoredMessage = 0;

			return false;
		}

		m_BannedList.insert(netAdr.GetIP());

		if (rcon_debug.GetBool())
		{
			Warning(eDLL_T::SERVER, "Banned '%s' for RCON hacking attempts\n", netAdr.ToString(true));
		}

		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: close connection on current index
//-----------------------------------------------------------------------------
void CRConServer::Disconnect(const char* szReason) // NETMGR
{
	Disconnect(m_nConnIndex, szReason);
}

//-----------------------------------------------------------------------------
// Purpose: close specific connection by index
//-----------------------------------------------------------------------------
void CRConServer::Disconnect(const int nIndex, const char* szReason) // NETMGR
{
	ConnectedNetConsoleData_s& data = m_Socket.GetAcceptedSocketData(nIndex);
	if (data.authorized)
	{
		// Inform server owner when authenticated connection has been closed.
		const netadr_t& netAdr = m_Socket.GetAcceptedSocketAddress(nIndex);
		if (!szReason)
		{
			szReason = "unknown reason";
		}

		Msg(eDLL_T::SERVER, "Connection to '%s' lost (%s)\n", netAdr.ToString(), szReason);

		if (--m_nAuthConnections < sv_rcon_maxconnections.GetInt())
		{
			if (!m_Socket.IsListening())
			{
				m_Socket.CreateListenSocket(m_Address);
			}
		}
	}

	m_Socket.CloseAcceptedSocket(nIndex);
}

//-----------------------------------------------------------------------------
// Purpose: close all connections except for authenticated
//-----------------------------------------------------------------------------
void CRConServer::CloseNonAuthConnection(void)
{
	const int nCount = m_Socket.GetAcceptedSocketCount();

	for (int i = nCount - 1; i >= 0; i--)
	{
		const ConnectedNetConsoleData_s& data = m_Socket.GetAcceptedSocketData(i);

		if (!data.authorized)
		{
			m_Socket.CloseAcceptedSocket(i);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: checks if this message should be send or not
// Input  : responseType -
// Output : true if it should send, false otherwise
//-----------------------------------------------------------------------------
bool CRConServer::ShouldSend(const netcon::response_e responseType) const
{
	if (!IsInitialized() || !m_Socket.GetAcceptedSocketCount())
	{
		// Not initialized or no sockets...
		return false;
	}

	if (responseType == netcon::response_e::SERVERDATA_RESPONSE_CONSOLE_LOG)
	{
		if (!sv_rcon_sendlogs.GetBool() || !m_Socket.GetAuthorizedSocketCount())
		{
			// Disabled or no authorized clients to send to...
			return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: returns whether the rcon server is initialized
//-----------------------------------------------------------------------------
bool CRConServer::IsInitialized(void) const
{
	return m_bInitialized;
}

//-----------------------------------------------------------------------------
// Purpose: returns the number of authenticated connections
//-----------------------------------------------------------------------------
int CRConServer::GetAuthenticatedCount(void) const
{
	return m_nAuthConnections;
}

//-----------------------------------------------------------------------------
// Purpose: change RCON password on server and drop all connections
//-----------------------------------------------------------------------------
static void RCON_PasswordChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData)
{
	if (ConVar* const pConVarRef = g_pCVar->FindVar(pConVar->GetName()))
	{
		const char* const pNewString = pConVarRef->GetString();

		if (strcmp(pOldString, pNewString) == NULL)
			return; // Same password.

		if (RCONServer()->IsInitialized())
		{
			RCONServer()->SetPassword(pNewString);
		}
		else // Initialize first
		{
			RCON_InitServerAndTrySyncKeys(pNewString);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: change whitelist address on RCON server
//-----------------------------------------------------------------------------
static void RCON_WhiteListAddresChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData)
{
	if (ConVar* const pConVarRef = g_pCVar->FindVar(pConVar->GetName()))
	{
		if (strcmp(pOldString, pConVarRef->GetString()) == NULL)
			return; // Same address.

		if (!RCONServer()->SetWhiteListAddress(pConVarRef->GetString()))
		{
			Warning(eDLL_T::SERVER, "Failed to set RCON whitelist address: %s\n", pConVarRef->GetString());
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: change max connection count on RCON server
//-----------------------------------------------------------------------------
static void RCON_ConnectionCountChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData)
{
	if (!RCONServer()->IsInitialized())
		return; // Not initialized; no sockets at this point.

	if (ConVar* const pConVarRef = g_pCVar->FindVar(pConVar->GetName()))
	{
		if (strcmp(pOldString, pConVarRef->GetString()) == NULL)
			return; // Same count.

		const int maxCount = pConVarRef->GetInt();
		const int count = RCONServer()->GetAuthenticatedCount();

		CSocketCreator* const pCreator = RCONServer()->GetSocketCreator();

		if (count < maxCount)
		{
			if (!pCreator->IsListening())
			{
				pCreator->CreateListenSocket(*RCONServer()->GetNetAddress());
			}
		}
		else
		{
			int currCount = count;

			while (currCount > maxCount)
			{
				RCONServer()->Disconnect(currCount - 1, "too many authenticated sockets");
				currCount = RCONServer()->GetAuthenticatedCount();
			}

			pCreator->CloseListenSocket();
			RCONServer()->CloseNonAuthConnection();
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: change whether to bind on loopback socket
//-----------------------------------------------------------------------------
static void RCON_UseLoopbackSocketChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData)
{
	if (ConVar* const pConVarRef = g_pCVar->FindVar(pConVar->GetName()))
	{
		if (strcmp(pOldString, pConVarRef->GetString()) == NULL)
			return; // Same value.

#ifndef CLIENT_DLL
		// Reboot the RCON server to switch address type.
		RCONServer()->Reboot();
#endif // !CLIENT_DLL
	}
}

///////////////////////////////////////////////////////////////////////////////
static CRConServer s_RCONServer;
CRConServer* RCONServer() // Singleton RCON Server.
{
	return &s_RCONServer;
}
