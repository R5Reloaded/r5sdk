//===========================================================================//
// 
// Purpose: Implementation of the rcon client.
// 
//===========================================================================//

#include "core/stdafx.h"
#include "tier1/cmd.h"
#include "tier1/cvar.h"
#include "protoc/netcon.pb.h"
#include "engine/client/cl_rcon.h"
#include "engine/shared/shared_rcon.h"
#include "engine/net.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "common/igameserverdata.h"


//-----------------------------------------------------------------------------
// Purpose: console variables
//-----------------------------------------------------------------------------
static void RCON_AddressChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData);
static void RCON_InputOnlyChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData);

static ConVar cl_rcon_address("cl_rcon_address", "", FCVAR_SERVER_CANNOT_QUERY | FCVAR_DONTRECORD | FCVAR_RELEASE, "Remote server access address (rcon client is disabled if empty)", &RCON_AddressChanged_f);
static ConVar cl_rcon_inputonly("cl_rcon_inputonly", "0", FCVAR_RELEASE, "Tells the rcon server whether or not we are input only.", RCON_InputOnlyChanged_f);

//-----------------------------------------------------------------------------
// Purpose: console commands
//-----------------------------------------------------------------------------
static void RCON_CmdQuery_f(const CCommand& args);

static ConCommand rcon("rcon", RCON_CmdQuery_f, "Forward RCON message to remote server", FCVAR_CLIENTDLL | FCVAR_RELEASE, nullptr, "rcon \"<message>\"");

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CRConClient::CRConClient() : CNetConBase(false)
	, m_bInitialized(false)
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CRConClient::~CRConClient(void)
{
	// NOTE: do not call Shutdown() from the destructor as the OS's socket
	// system would be shutdown by now, call Shutdown() in application
	// shutdown code instead
}

//-----------------------------------------------------------------------------
// Purpose: NETCON systems init
//-----------------------------------------------------------------------------
void CRConClient::Init(const char* pNetKey)
{
	SetKey(pNetKey);
	m_bInitialized = true;
}

//-----------------------------------------------------------------------------
// Purpose: NETCON systems shutdown
//-----------------------------------------------------------------------------
void CRConClient::Shutdown(void)
{
	Disconnect("shutdown");
}

//-----------------------------------------------------------------------------
// Purpose: client rcon main processing loop
//-----------------------------------------------------------------------------
void CRConClient::RunFrame(void)
{
	if (IsInitialized() && IsConnected())
	{
		ConnectedNetConsoleData_s* const pData = GetData();
		Assert(pData != nullptr);

		if (pData)
		{
			Recv(*pData, (u32)rcon_maxframesize.GetInt());
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: disconnect from current session
//-----------------------------------------------------------------------------
void CRConClient::Disconnect(const char* szReason)
{
	if (IsConnected())
	{
		if (!szReason)
		{
			szReason = "unknown reason";
		}

		Msg(eDLL_T::CLIENT, "RCON disconnect: (%s)\n", szReason);
		m_Socket.CloseAcceptedSocket(0);
	}
}

//-----------------------------------------------------------------------------
// Purpose: processes received message
// Input  : &data - 
//			nMaxLen - 
//-----------------------------------------------------------------------------
bool CRConClient::ProcessMessage(ConnectedNetConsoleData_s& data, const u32 nMaxLen)
{
	netcon::response response;

	if (!NetconShared_UnpackEnvelope(this, data, nMaxLen, &response, rcon_encryptframes.GetBool(), rcon_debug.GetBool()))
	{
		Disconnect("received invalid message");
		return false;
	}

	switch (response.responsetype())
	{
	case netcon::response_e::SERVERDATA_RESPONSE_AUTH:
	{
		if (!response.responseval().empty())
		{
			const int i = atoi(response.responseval().c_str());

			// '!i' means we are marked 'input only' on the rcon server.
			if (!i && ShouldReceive())
			{
				RequestConsoleLog(true);
			}
		}

		Msg(eDLL_T::NETCON, "%s", response.responsemsg().c_str());
		break;
	}
	case netcon::response_e::SERVERDATA_RESPONSE_CONSOLE_LOG:
	{
		NetMsg(static_cast<LogType_t>(response.messagetype()), 
			static_cast<eDLL_T>(response.messageid()),
			response.responseval().c_str(), "%s", response.responsemsg().c_str());
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
// Purpose: request the rcon server to enable/disable sending logs to us 
// Input  : bWantLog - 
//-----------------------------------------------------------------------------
void CRConClient::RequestConsoleLog(const bool bWantLog)
{
	// If 'IsRemoteLocal()' returns true, and you called this with 'bWantLog'
	// true, you caused a bug! It means the server address and port are equal
	// to the global netadr singleton, which ultimately means we are running on
	// a listen server. Listen server's already log to the same console,
	// sending logs will cause the print func to get called recursively forever.
	Assert(!(bWantLog && IsRemoteLocal()));
	ConnectedNetConsoleData_s* pData = GetData();

	if (!pData)
	{
		Assert(0);
		return;
	}

	const char* const szEnable = bWantLog ? "1" : "0";

	vector<byte> vecMsg;
	const bool ret = Serialize(*pData, vecMsg, "", 0, szEnable, 1, netcon::request_e::SERVERDATA_REQUEST_SEND_CONSOLE_LOG);

	if (ret && !Send(pData->socket, vecMsg.data(), (u32)vecMsg.size()))
	{
		Error(eDLL_T::CLIENT, NO_ERROR, "Failed to send RCON message: (%s)\n", "SOCKET_ERROR");
	}
}

//-----------------------------------------------------------------------------
// Purpose: serializes input
// Input  : &data - 
//			*svReqBuf - 
//			nReqMsgLen - 
//			*svReqVal - 
//			nReqValLen -
//			request_t - 
// Output : serialized results as string
//-----------------------------------------------------------------------------
bool CRConClient::Serialize(ConnectedNetConsoleData_s& data, vector<byte>& vecBuf, const char* szReqBuf, const size_t nReqMsgLen,
	const char* szReqVal, const size_t nReqValLen, const netcon::request_e requestType) const
{
	return NetconClient_Serialize(this, data, vecBuf, szReqBuf, nReqMsgLen, szReqVal, nReqValLen, requestType,
		rcon_encryptframes.GetBool(), rcon_debug.GetBool());
}

//-----------------------------------------------------------------------------
// Purpose: retrieves the remote socket
// Output : SOCKET_ERROR (-1) on failure
//-----------------------------------------------------------------------------
ConnectedNetConsoleData_s* CRConClient::GetData(void)
{
	return NetconShared_GetConnData(this, 0);
}

//-----------------------------------------------------------------------------
// Purpose: retrieves the remote socket
// Output : SOCKET_ERROR (-1) on failure
//-----------------------------------------------------------------------------
SocketHandle_t CRConClient::GetSocket(void)
{
	return NetconShared_GetSocketHandle(this, 0);
}

//-----------------------------------------------------------------------------
// Purpose: returns whether or not we should receive logs from the server
//-----------------------------------------------------------------------------
bool CRConClient::ShouldReceive(void)
{
	return (!IsRemoteLocal() && !cl_rcon_inputonly.GetBool());
}

//-----------------------------------------------------------------------------
// Purpose: returns whether the rcon server is actually our own listen server
//-----------------------------------------------------------------------------
bool CRConClient::IsRemoteLocal(void)
{
	return NET_IsRemoteLocal(m_Address);
}

//-----------------------------------------------------------------------------
// Purpose: checks if client rcon is initialized
//-----------------------------------------------------------------------------
bool CRConClient::IsInitialized(void) const
{
	return m_bInitialized;
}

//-----------------------------------------------------------------------------
// Purpose: returns whether the rcon client is connected
//-----------------------------------------------------------------------------
bool CRConClient::IsConnected(void)
{
	return (GetSocket() != SOCKET_ERROR);
}

///////////////////////////////////////////////////////////////////////////////
static CRConClient s_RCONClient;
CRConClient* RCONClient() // Singleton RCON Client.
{
	return &s_RCONClient;
}

/*
=====================
RCON_AddressChanged_f

  changes the address of the rcon
  server and attempts to connect
  to it
=====================
*/
static void RCON_AddressChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData)
{
	if (ConVar* pConVarRef = g_pCVar->FindVar(pConVar->GetName()))
	{
		const char* pNewString = pConVarRef->GetString();

		if (!*pNewString) // Empty address means nothing to network to; shutdown client...
		{
			RCONClient()->Shutdown();
		}
		else
		{
			RCON_InitClientAndTrySyncKeys();

			if (RCONClient()->IsInitialized() && !RCONClient()->IsConnected())
			{
				if (RCONClient()->IsConnected())
				{
					RCONClient()->Disconnect("address change requested");
				}

				RCONClient()->Connect(pNewString);
			}
		}
	}
}

/*
=====================
RCON_InputOnlyChanged_f

  request whether to recv logs
  from RCON server when cvar
  changes
=====================
*/
static void RCON_InputOnlyChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData)
{
	RCONClient()->RequestConsoleLog(RCONClient()->ShouldReceive());
}

/*
=====================
RCON_CmdQuery_f

  Issues an RCON command to the
  RCON server.
=====================
*/
static void RCON_CmdQuery_f(const CCommand& args)
{
	const int64_t argCount = args.ArgC();

	if (argCount < 2)
	{
		Warning(eDLL_T::CLIENT, "Failed to issue command to RCON server: %s\n", "no command provided");
		return;
	}
	else
	{
		if (!RCONClient()->IsInitialized())
		{
			Warning(eDLL_T::CLIENT, "Failed to issue command to RCON server: %s\n", "uninitialized");
			return;
		}
		else if (RCONClient()->IsConnected())
		{
			vector<byte> vecMsg;
			bool bSuccess = false;
			ConnectedNetConsoleData_s* pData = RCONClient()->GetData();

			if (!pData)
			{
				Assert(0);
				return;
			}

			if (strcmp(args.Arg(1), "PASS") == 0)
			{
				if (argCount > 2)
				{
					const char* const pass = args.Arg(2);
					const size_t passLen = strlen(pass);

					bSuccess = RCONClient()->Serialize(*pData, vecMsg, pass, passLen, "", 0, netcon::request_e::SERVERDATA_REQUEST_AUTH);
				}
				else // Need at least 3 arguments for a password in PASS command (rcon PASS <password>)
				{
					Warning(eDLL_T::CLIENT, "Failed to issue command to RCON server: %s\n", "no password provided");
					return;
				}

				if (bSuccess)
				{
					RCONClient()->Send(pData->socket, vecMsg.data(), (u32)vecMsg.size());
				}

				return;
			}
			else if (strcmp(args.Arg(1), "disconnect") == 0) // Disconnect from RCON server.
			{
				RCONClient()->Disconnect("ordered by user");
				return;
			}

			const char* const request = args.Arg(1);
			const size_t requestLen = strlen(request);

			const char* const value = args.ArgS();
			const size_t valueLen = strlen(value);

			bSuccess = RCONClient()->Serialize(*pData, vecMsg, request, requestLen, value, valueLen, netcon::request_e::SERVERDATA_REQUEST_EXECCOMMAND);
			if (bSuccess)
			{
				RCONClient()->Send(pData->socket, vecMsg.data(), (u32)vecMsg.size());
			}
			return;
		}
		else
		{
			Warning(eDLL_T::CLIENT, "Failed to issue command to RCON server: %s\n", "unconnected");
			return;
		}
	}
}
