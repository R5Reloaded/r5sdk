//=============================================================================//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
// clientstate.cpp: implementation of the CClientState class.
//
/////////////////////////////////////////////////////////////////////////////////
#include "core/stdafx.h"
#include "common/protocol.h"
#include "common/proto_oob.h"
#include "mathlib/bitvec.h"
#include "tier0/frametask.h"
#include "tier1/checksum_crc.h"
#include "ebisusdk/EbisuSDK.h"
#include "engine/common.h"
#include "engine/host.h"
#include "engine/host_cmd.h"
#include "engine/cmd.h"
#ifndef CLIENT_DLL
#include "engine/server/server.h"
#endif // !CLIENT_DLL
#include "engine/cmodel_bsp.h"
#include "engine/sys_dll2.h"
#include "engine/shared/oob_types.h"
#include "engine/shared/entitlements.h"
#include "networksystem/persistence.h"
#include "networksystem/datacenters.h"
#include "networksystem/matchmaking.h"
#include "clientstate.h"
#include "common/callback.h"
#include "cdll_engine_int.h"
#include "vgui/vgui_baseui_interface.h"
#include "rtech/playlists/playlists.h"

//------------------------------------------------------------------------------
// Purpose: console command callbacks
//------------------------------------------------------------------------------
static void SetName_f(const CCommand& args)
{
    if (args.ArgC() < 2)
        return;

    if (!IsOriginDisabled())
        return;

    const char* pszName = args.Arg(1);

    if (!pszName[0])
        pszName = "unnamed";

    const size_t nLen = strlen(pszName);

    if (nLen >= ORIGIN_MAX_USERNAME_LENGTH)
        return;

    // Update nucleus name.
    strncpy(g_originState->userName, pszName, nLen+1);
    name_cvar->SetValue(pszName);
}
static void Reconnect_f(const CCommand& args)
{
    g_pClientState->Reconnect();
}

//------------------------------------------------------------------------------
// Purpose: console commands
//------------------------------------------------------------------------------
static ConCommand cl_setname("cl_setname", SetName_f, "Sets the client's persona name", FCVAR_RELEASE);
static ConCommand reconnect("reconnect", Reconnect_f, "Reconnect to current server.", FCVAR_DONTRECORD | FCVAR_RELEASE);

//------------------------------------------------------------------------------
// Purpose: returns true if client simulation is paused
//------------------------------------------------------------------------------
bool CClientState::IsPaused() const
{
    return m_bPaused || !*host_initialized || g_pEngineVGui->ShouldPause();
}

//------------------------------------------------------------------------------
// Purpose: returns true if client is fully connected and active
//------------------------------------------------------------------------------
bool CClientState::IsActive(void) const
{
    return m_nSignonState == SIGNONSTATE::SIGNONSTATE_FULL;
};

//------------------------------------------------------------------------------
// Purpose: returns true if client connected but not active
//------------------------------------------------------------------------------
bool CClientState::IsConnected(void) const
{
    return m_nSignonState >= SIGNONSTATE::SIGNONSTATE_CONNECTED;
};

//------------------------------------------------------------------------------
// Purpose: returns true if client is still connecting
//------------------------------------------------------------------------------
bool CClientState::IsConnecting(void) const
{
    return m_nSignonState >= SIGNONSTATE::SIGNONSTATE_NONE;
}

//------------------------------------------------------------------------------
// Purpose: returns true if client is still challenging
//------------------------------------------------------------------------------
bool CClientState::IsChallenging(void) const
{
	return m_nSignonState == SIGNONSTATE::SIGNONSTATE_CHALLENGE;
}

//------------------------------------------------------------------------------
// Purpose: gets the client time
// Technically doesn't belong here
//------------------------------------------------------------------------------
float CClientState::GetClientTime() const
{
    if (m_bClockCorrectionEnabled)
    {
        return (float)m_ClockDriftMgr.m_nClientTick * g_pCommonHostState->interval_per_tick;
    }
    else
    {
        return m_flClockDriftFrameTime;
    }
}

//------------------------------------------------------------------------------
// Purpose: gets the simulation tick count
//------------------------------------------------------------------------------
int CClientState::GetTick() const
{
    return m_ClockDriftMgr.m_nSimulationTick;
}

//------------------------------------------------------------------------------
// Purpose: gets the last-received server tick count
//------------------------------------------------------------------------------
int CClientState::GetServerTickCount() const
{
    return m_ClockDriftMgr.m_nServerTick;
}

//------------------------------------------------------------------------------
// Purpose: sets the server tick count
//------------------------------------------------------------------------------
void CClientState::SetServerTickCount(int tick)
{
    m_ClockDriftMgr.m_nServerTick = tick;
}

//------------------------------------------------------------------------------
// Purpose: gets the client tick count
//------------------------------------------------------------------------------
int CClientState::GetClientTickCount() const
{
    return m_ClockDriftMgr.m_nClientTick;
}

//------------------------------------------------------------------------------
// Purpose: sets the client tick count
//------------------------------------------------------------------------------
void CClientState::SetClientTickCount(int tick)
{
    m_ClockDriftMgr.m_nClientTick = tick;
}

//------------------------------------------------------------------------------
// Purpose: gets the client frame time
//------------------------------------------------------------------------------
float CClientState::GetFrameTime() const
{
    if (IsPaused())
    {
        return 0.0f;
    }

    return m_flFrameTime;
}

//---------------------------------------------------------------------------------
// Purpose: registers net messages
// Input  : *pClient - 
//			*pChan - 
// Output : true if setup was successful, false otherwise
//---------------------------------------------------------------------------------
bool CClientState::VConnectionStart(CClientState* pClient, CNetChan* pChan)
{
    pClient->RegisterNetMsgs(pChan);
    return CClientState__ConnectionStart(pClient, pChan);
}

//------------------------------------------------------------------------------
// Purpose: called when connection to the server has been closed
//------------------------------------------------------------------------------
void CClientState::VConnectionClosing(CClientState* thisptr, const char* szReason)
{
    CClientState__ConnectionClosing(thisptr, szReason);

    // Delay execution to the next frame; this is required to avoid a rare crash.
    // Cannot reload playlists while still disconnecting.
    g_TaskQueue.Dispatch([]()
        {
            // Reload the local playlist to override the cached
            // one from the server we got disconnected from.
            v_Playlists_Download_f();
            Playlists_SDKInit();
        }, 0);
}

//------------------------------------------------------------------------------
// Purpose: called when a SVC_ServerTick messages comes in.
// This function has an additional check for the command tick against '-1',
// if it is '-1', we process statistics only. This is required as the game
// no longer can process server ticks every frame unlike previous games.
// Without this, the server CPU and frame time don't get updated to the client.
//------------------------------------------------------------------------------
bool CClientState::VProcessServerTick(CClientState* thisptr, SVC_ServerTick* msg)
{
    if (msg->m_NetTick.m_nCommandTick != -1)
    {
        // Updates statistics and updates clockdrift.
        return CClientState__ProcessServerTick(thisptr, msg);
    }
    else // Statistics only.
    {
        CClientState* const thisptr_ADJ = thisptr->GetShiftedBasePointer();

        if (thisptr_ADJ->IsConnected())
        {
            CNetChan* const pChan = thisptr_ADJ->m_NetChannel;

            pChan->SetRemoteFramerate(msg->m_NetTick.m_flHostFrameTime, msg->m_NetTick.m_flHostFrameTimeStdDeviation);
            pChan->SetRemoteCPUStatistics(msg->m_NetTick.m_nServerCPU);
        }

        return true;
    }
}

//------------------------------------------------------------------------------
// Purpose: processes string commands sent from server
// Input  : *thisptr - 
//          *msg     - 
// Output : true on success, false otherwise
//------------------------------------------------------------------------------
bool CClientState::_ProcessStringCmd(CClientState* thisptr, NET_StringCmd* msg)
{
    CClientState* const thisptr_ADJ = thisptr->GetShiftedBasePointer();

    if (thisptr_ADJ->m_bRestrictServerCommands
#ifndef CLIENT_DLL
        // Don't restrict commands if we are on our own listen server
        && !g_pServer->IsActive()
#endif // !CLIENT_DLL
        )
    {
        CCommand args;
        args.Tokenize(msg->cmd, cmd_source_t::kCommandSrcInvalid);

        if (args.ArgC() > 0)
        {
            if (!Cbuf_AddTextWithMarkers(msg->cmd,
                eCmdExecutionMarker_Enable_FCVAR_SERVER_CAN_EXECUTE,
                eCmdExecutionMarker_Disable_FCVAR_SERVER_CAN_EXECUTE))
            {
                DevWarning(eDLL_T::CLIENT, "%s: No room for %i execution markers; command \"%s\" ignored\n",
                    __FUNCTION__, 2, msg->cmd);
            }

            return true;
        }
    }
    else
    {
        Cbuf_AddText(Cbuf_GetCurrentPlayer(), msg->cmd, cmd_source_t::kCommandSrcCode);
    }

    return true;
}

//------------------------------------------------------------------------------
// Purpose: create's string tables from string table data sent from server
// Input  : *thisptr - 
//          *msg     - 
// Output : true on success, false otherwise
//------------------------------------------------------------------------------
bool CClientState::_ProcessCreateStringTable(CClientState* thisptr, SVC_CreateStringTable* msg)
{
    CClientState* const cl = thisptr->GetShiftedBasePointer();

    if (!cl->IsConnected())
        return false;

    CNetworkStringTableContainer* const container = cl->m_StringTableContainer;

    // Must have a string table container at this point!
    if (!container)
    {
        Assert(0);

        COM_ExplainDisconnection(true, "String table container missing.\n");
        v_Host_Disconnect(true);

        return false;
    }

    container->AllowCreation(true);
    const ssize_t startbit = msg->m_DataIn.GetNumBitsRead();

    CNetworkStringTable* const table = (CNetworkStringTable*)container->CreateStringTable(false, msg->m_szTableName,
        msg->m_nMaxEntries, msg->m_nUserDataSize, msg->m_nUserDataSizeBits, msg->m_nDictFlags);

    table->SetTick(cl->GetServerTickCount());
    CClientState__HookClientStringTable(cl, msg->m_szTableName);

    if (msg->m_bDataCompressed)
    {
        // TODO[ AMOS ]: check sizes before proceeding to decode
        // the string tables
        unsigned int msgUncompressedSize = msg->m_DataIn.ReadLong();
        unsigned int msgCompressedSize = msg->m_DataIn.ReadLong();

        size_t uncompressedSize = msgUncompressedSize;
        size_t compressedSize = msgCompressedSize;

        bool bSuccess = false;

        // TODO[ AMOS ]: this could do better. The engine does UINT_MAX-3
        // which doesn't look very great. Clamp to more reasonable values
        // than UINT_MAX-3 or UINT_MAX/2? The largest string tables sent
        // are settings layout string tables which are roughly 256KiB
        // compressed with LZSS. perhaps clamp this to something like 16MiB?
        if (msg->m_DataIn.TotalBytesAvailable() > 0 &&
            msgCompressedSize <= (unsigned int)msg->m_DataIn.TotalBytesAvailable() &&
            msgCompressedSize < UINT_MAX / 2 && msgUncompressedSize < UINT_MAX / 2)
        {
            // allocate buffer for uncompressed data, align to 4 bytes boundary
            uint8_t* const uncompressedBuffer = new uint8_t[PAD_NUMBER(msgUncompressedSize, 4)];
            uint8_t* const compressedBuffer = new uint8_t[PAD_NUMBER(msgCompressedSize, 4)];

            msg->m_DataIn.ReadBytes(compressedBuffer, msgCompressedSize);

            // uncompress data
            bSuccess = NET_BufferToBufferDecompress(compressedBuffer, compressedSize, uncompressedBuffer, &uncompressedSize);
            bSuccess &= (uncompressedSize == msgUncompressedSize);

            if (bSuccess)
            {
                bf_read data(uncompressedBuffer, (int)uncompressedSize);
                table->ParseUpdate(data, msg->m_nNumEntries);
            }

            delete[] uncompressedBuffer;
            delete[] compressedBuffer;
        }

        if (!bSuccess)
        {
            Assert(false);
            DevWarning(eDLL_T::CLIENT, "%s: Received malformed string table message!\n", __FUNCTION__);
        }
    }
    else
    {
        table->ParseUpdate(msg->m_DataIn, msg->m_nNumEntries);
    }

    container->AllowCreation(false);
    const ssize_t endbit = msg->m_DataIn.GetNumBitsRead();

    return (endbit - startbit) == msg->m_nLength;
}

//------------------------------------------------------------------------------
// Purpose: processes user message data
// Input  : *thisptr - 
//          *msg     - 
// Output : true on success, false otherwise
//------------------------------------------------------------------------------
bool CClientState::_ProcessUserMessage(CClientState* thisptr, SVC_UserMessage* msg)
{
    CClientState* const cl = thisptr->GetShiftedBasePointer();

    if (!cl->IsConnected())
        return false;

    // buffer for incoming user message
    ALIGN4 byte userdata[MAX_USER_MSG_DATA] ALIGN4_POST = { 0 };
    bf_read userMsg("UserMessage(read)", userdata, sizeof(userdata));

    int bitsRead = msg->m_DataIn.ReadBitsClamped(userdata, msg->m_nLength);
    userMsg.StartReading(userdata, Bits2Bytes(bitsRead));

    // dispatch message to client.dll
    if (!g_pHLClient->DispatchUserMessage(msg->m_nMsgType, &userMsg))
    {
        Warning(eDLL_T::CLIENT, "Couldn't dispatch user message (%i)\n", msg->m_nMsgType);
        return false;
    }

    return true;
}

bool IsLocalHost(connectparams_t* connectParams)
{
    return (strstr(connectParams->netAdr, "localhost") || strstr(connectParams->netAdr, "127.0.0.1"));
}

void CClientState::VConnect(CClientState* thisptr, connectparams_t* connectParams)
{
    // rexx: this hook is no longer needed at the moment, but I'm keeping it around because it might be needed in the future
    CClientState__Connect(thisptr, connectParams);
}

//------------------------------------------------------------------------------
// Purpose: reconnects to currently connected server
//------------------------------------------------------------------------------
void CClientState::Reconnect()
{
    if (!IsConnected())
    {
        Warning(eDLL_T::CLIENT, "Attempted to reconnect while unconnected, please defer it.\n");
        return;
    }

    const netadr_t& remoteAdr = m_NetChannel->GetRemoteAddress();

    // NOTE: technically the engine supports running "connect localhost" to
    // reconnect to the listen server without killing it, however when running
    // this, an engine error occurs "Couldn't create a world static vertex buffer\n"
    // Needs investigation.
    if (remoteAdr.IsLoopback() || NET_IsLocalHost(remoteAdr))
    {
        Warning(eDLL_T::CLIENT, "Reconnecting to a listen server isn't supported, use \"reload\" instead.\n");
        return;
    }

    char buf[1024];
    V_snprintf(buf, sizeof(buf), "connect \"%s\"", remoteAdr.ToString());

    Cbuf_AddText(ECommandTarget_t::CBUF_FIRST_PLAYER, buf, cmd_source_t::kCommandSrcCode);
}

void CClientState::Disconnect(const bool bShowMainMenu)
{
	CClientState__Disconnect(this, bShowMainMenu);
}

//---------------------------------------------------------------------------------
// Purpose: registers net messages
// Input  : *chan
//---------------------------------------------------------------------------------
void CClientState::RegisterNetMsgs(CNetChan* chan)
{
    REGISTER_SVC_MSG(SetClassVar);
    REGISTER_SVC_MSG(SystemSayText);
}

//-----------------------------------------------------------------------------
// Purpose: handles a connection rejection while connecting through matchmaking
//-----------------------------------------------------------------------------
static void ClientState_HandleConnectionRejectMatchmaking(CClientState* const client)
{
	MatchmakingSearchState_s* matchmaking = Matchmaking_GetActiveSearch();

	if (!matchmaking || !matchmaking->canRetryServer)
	{
		if (client->m_bSendChallengeRequest)
			client->m_bSendChallengeRequest = false;
		else
			client->Disconnect(true);

		return;
	}

	v_Matchmaking_RemoveRejectedServer(&client->m_RemoteAddress);
	// Removing the rejected server can change
	// or clear the active matchmaking state!
	matchmaking = Matchmaking_GetActiveSearch();

	if (!matchmaking)
	{
		client->m_bSendChallengeRequest = false;
		return;
	}

	int serverIndex = -1;

	for (int i = 0; i < matchmaking->numServers; ++i)
	{
		if (matchmaking->servers[i].active)
		{
			serverIndex = i;
			break;
		}
	}

	client->m_bSendChallengeRequest = serverIndex >= 0;
}

bool CClientState::ProcessDisconnect(const netadr_t& from, bf_read& msg)
{
	char reason[1200];
	msg.ReadString(reason, sizeof(reason));

	return true;
}

//------------------------------------------------------------------------------
// Purpose: writes the client connection request payload
// Input  : *buf      - 
//          challenge - 
//------------------------------------------------------------------------------
void CClientState::CreateConnectMessage(bf_write* const buf, const int challenge) const
{
	buf->SetAssertOnOverflow(false);

	//-------------------------------------------------------------------------
	// Connection protocol
	//-------------------------------------------------------------------------
	buf->WriteLong(PROTOCOL_VERSION);
	buf->WriteLong(v_Sys_GetHostVersion());
	buf->WriteLong(challenge);
	buf->WriteLong(reconnect_unk);

	//-------------------------------------------------------------------------
	// Client plat and id
	//-------------------------------------------------------------------------
	const NucleusID_t userId = strtoull(platform_user_id->GetString(), nullptr, 0); // todo(kawe): inconsistency with server parser!!!

	buf->WriteByte(HardwareType_e::PLAT_PC);
	buf->WriteULongLong(userId);

	buf->WriteString(name_cvar->GetString()); // originally vtable call to get name!
	buf->WriteString(serverFilter_cvar->GetString());

	//-------------------------------------------------------------------------
	// Playlist / persistence info
	//-------------------------------------------------------------------------
	buf->WriteLongLong(*g_pPlaylistCRC);
	buf->WriteLongLong(g_persistentDataDef->scriptVersion);

	u32 persistenceCRC = 0;

	if (m_bPersistenceBaselineRecvd && v_Host_IsPersistenceDataDefValid())
	{
		persistenceCRC = UINT32_MAX;
		CRC32_ProcessBuffer(&persistenceCRC, m_PersistenceData, g_persistentDataDef->dataSize);
		persistenceCRC = ~persistenceCRC;
	}

	buf->WriteULong(persistenceCRC);

	//-------------------------------------------------------------------------
	// Entitlement stuff
	//-------------------------------------------------------------------------
	{
		AUTO_LOCK(g_hostEntitlements->mutex);

		buf->WriteULongLong(g_hostEntitlements->dataCRC);
		buf->WriteBits(&m_entitlementBits, 64);
		buf->WriteByte(static_cast<u8>(g_hostEntitlements->balances.Count()));

		for (int i = 0; i < g_hostEntitlements->balances.Count(); ++i)
		{
			buf->WriteBits(&g_hostEntitlements->balances[i], 16);
		}
	}

	//-------------------------------------------------------------------------
	// Misc connection flags
	//-------------------------------------------------------------------------
	buf->WriteOneBit(m_bUnk_used_during_auth);
	buf->WriteOneBit(v_Host_IsInLowViolenceMode());
	buf->WriteOneBit(false);

	const int forceEAAccessValue = force_EAAccess->GetInt();
	const bool forceEAAccess = forceEAAccessValue < 0
		? g_originState->isSubscriber
		: forceEAAccessValue != 0;

	buf->WriteOneBit(forceEAAccess);

	const bool hasOriginToken = g_originState->token[0] != '\0';
	buf->WriteOneBit(hasOriginToken);

	if (hasOriginToken)
		buf->WriteString(g_originState->token);

	// Read/write sync marker
	buf->WriteULong(CONNECTIONLESS_SENTINEL);

	//-------------------------------------------------------------------------
	// Client userinfo cvars
	//-------------------------------------------------------------------------
	NET_SetConVar::Vec_t userInfo;
	Host_BuildUserInfoUpdateMessage(0, &userInfo, false, true);

	buf->WriteByte(static_cast<u8>(userInfo.Count()));

	for (int i = 0; i < userInfo.Count(); ++i)
	{
		const NET_SetConVar::Cvar_s& entry = userInfo[i];
		unsigned int nameIndex = 0;

		for (unsigned int j = 0; j < V_ARRAYSIZE(g_userInfoConVarNames); ++j)
		{
			if (strcmp(entry.name, g_userInfoConVarNames[j]) == 0)
			{
				nameIndex = j;
				break;
			}
		}

		buf->WriteBits(&nameIndex, 6);

		if (!nameIndex)
			buf->WriteString(entry.name);

		buf->WriteString(entry.value);
	}
}

bool CClientState::ProcessChallenge(const netadr_t& from, bf_read& msg)
{
	if (!NET_IsLocalHost(from) && cl_failremoteconnections->GetBool())
	{
		return true;
	}

	m_bSignonChallengeReceived = false;
	m_challengeAddr = from;

	m_challenge = msg.ReadLong();

	// Unknown and unused
	msg.ReadLongLong();

	if (msg.IsOverflowed())
	{
		Disconnect(true);
		return false;
	}

	char commandName[256] = {};
	msg.ReadString(commandName, sizeof(commandName));

	if (strcmp(commandName, "connect") != 0)
		return true;

	if (!IsChallenging() && !m_bSendChallengeRequest)
		return false;

	m_bSignonChallengeReceived = true;

	const int hostVersion = msg.ReadLong();

	if (hostVersion != v_Sys_GetHostVersion())
	{
		Disconnect(true);
		return false;
	}

	if (!m_bSignonChallengeReceived)
		return true;

	m_bSignonChallengeReceived = false;

	ALIGN4 u8 connectBuffer[1200];
	bf_write connectMsg(connectBuffer, sizeof(connectBuffer));

	connectMsg.WriteULong(CONNECTIONLESS_HEADER);
	connectMsg.WriteByte(C2S_CONNECT);

	CreateConnectMessage(&connectMsg, m_challenge);

	if (connectMsg.IsOverflowed())
	{
		Assert(0, __FUNC_PFX__"C2S connect message overflowed!");
		Disconnect(true);

		return false;
	}

	m_flConnectTime = *g_pNetTime;

	v_NET_SendPacket(
		nullptr,
		m_Socket,
		m_challengeAddr,
		connectMsg.GetData(),
		static_cast<unsigned int>(connectMsg.GetNumBytesWritten()),
		nullptr,
		false,
		nullptr,
		true);

	return true;
}

bool CClientState::ProcessConnectionAccept(const netadr_t& from, bf_read& msg)
{
	if (!IsChallenging() && !m_bSendChallengeRequest)
		return true;

	if (!from.IsLoopback() && m_RemoteAddress != from)
		return true;

	if (m_NetChannel && m_NetChannel->GetRemoteAddress() != from)
		Disconnect(false);

	CNetChan* const newNetChannel = v_NET_CreateNetChannel(m_Socket, &from, "CLIENT", this, true);
	Assert(newNetChannel);

	if (!newNetChannel)
		return false;

	m_NetChannel = newNetChannel;
	const double currentTime = *g_pNetTime;

	m_flConnectTime = currentTime;
	m_flNextCmdTime = currentTime;

	m_nDeltaTick = -1;
	m_nStringTableAckTick = -1;

	m_bSendChallengeRequest = false;
	CClientState__SetSignonState(this, SIGNONSTATE::SIGNONSTATE_CONNECTED, -1, nullptr);

	Matchmaking_AbortAllSearches();
	m_NetChannel->SetDataRate(rate_cvar->GetInt());

	dword245F8 = -1;
	m_nOutgoingCommandNr = -1;
	dword18D2C = -1;
	field_18D30.Init(0, 0, 0);
	dword18D3C = 7;
	dword18D40 = -1;

	from.ToString(false);

	char levelName[256];
	msg.ReadString(levelName, sizeof(levelName), false);

	char gameModeName[256];
	msg.ReadString(gameModeName, sizeof(gameModeName));

	m_bInMpLobbyMenu = strcmp(levelName, "mp_lobby") == 0;
	const int len = V_snprintf(m_szLevelFileName, sizeof(m_szLevelFileName), "maps/%s%s.bsp", levelName, "");

	if (len < 0 || len >= sizeof(m_szLevelFileName))
		return false;

	strncpy(m_szLevelBaseName, levelName, sizeof(m_szLevelBaseName));
	m_szLevelBaseName[sizeof(m_szLevelBaseName) - 1] = '\0';

	Mod_LoadLoadscreenPakForLevel(levelName);

	g_pEngineVGui->SetProgressLevelName(levelName, gameModeName);
	g_pEngineVGui->UpdateProgressBar(PROGRESS_SIGNONCONNECTED);

	if (!m_NetChannel || m_NetChannel->GetRemoteAddress().IsLoopback())
		return true;

	NET_PrintFunc("start caching level assets %s", levelName);

	char levelAssetPath[MAX_OSPATH];
	V_snprintf(levelAssetPath, sizeof(levelAssetPath), "vpk/client_%s.bsp", levelName);

	Mod_PrecacheLevelAssets(levelAssetPath, levelName, true);
	return true;
}

bool CClientState::ProcessConnectionReject(const netadr_t& from, bf_read& msg)
{
	if (!IsChallenging() && !m_bSendChallengeRequest)
		return true;

	char reason[1200];
	msg.ReadString(reason, sizeof(reason));

	if (!m_bSendChallengeRequest)
		COM_ExplainDisconnection(true, reason);

	ClientState_HandleConnectionRejectMatchmaking(this);
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: processes an S2C MatchInfo update
// TODO: dump r2 packets and figure out all field names?
//-----------------------------------------------------------------------------
i64 CClientState::ProcessMatchInfoUpdate(const netadr_t& from, bf_read& msg)
{
	memset(&m_remoteMatchInfo, 0, sizeof(m_remoteMatchInfo));
	const char* const addressString = from.ToString(false);

	strncpy(m_remoteMatchInfo.serverAddress, addressString, sizeof(m_remoteMatchInfo.serverAddress));
	m_remoteMatchInfo.serverAddress[sizeof(m_remoteMatchInfo.serverAddress) - 1] = '\0';

	const u8 version = static_cast<u8>(msg.ReadByte());

	//-------------------------------------------------------------------------
	// Version 2+
	//-------------------------------------------------------------------------
	if (version > 1)
	{
		m_remoteMatchInfo.field_084 = msg.ReadByte() != 0;
		m_remoteMatchInfo.field_080 = static_cast<u8>(msg.ReadByte());
		m_remoteMatchInfo.field_088 = static_cast<u32>(msg.ReadLong());
		m_remoteMatchInfo.field_08C = static_cast<i16>(msg.ReadShort());
		m_remoteMatchInfo.field_090 = msg.ReadLongLong();

		msg.ReadString(m_remoteMatchInfo.field_098, sizeof(m_remoteMatchInfo.field_098));
		msg.ReadString(m_remoteMatchInfo.datacenter, sizeof(m_remoteMatchInfo.datacenter));
		msg.ReadString(m_remoteMatchInfo.gamemode, sizeof(m_remoteMatchInfo.gamemode));
	}

	//-------------------------------------------------------------------------
	// Fields present in every packet revision
	//-------------------------------------------------------------------------
	m_remoteMatchInfo.field_1D8 = static_cast<u16>(msg.ReadWord());
	msg.ReadString(m_remoteMatchInfo.field_178, sizeof(m_remoteMatchInfo.field_178));

	char unusedString[16];
	msg.ReadString(unusedString, sizeof(unusedString));

	m_remoteMatchInfo.field_1DC = static_cast<u32>(msg.ReadLong());
	msg.ReadString(m_remoteMatchInfo.playlist, sizeof(m_remoteMatchInfo.playlist));

	m_remoteMatchInfo.numClients = static_cast<u8>(msg.ReadByte());
	m_remoteMatchInfo.maxClients = static_cast<u8>(msg.ReadByte());

	msg.ReadString(m_remoteMatchInfo.map, sizeof(m_remoteMatchInfo.map));

	//-------------------------------------------------------------------------
	// Version 5+
	//-------------------------------------------------------------------------
	if (version > 4)
	{
		m_remoteMatchInfo.field_208 = static_cast<u32>(msg.ReadLong());
		m_remoteMatchInfo.field_20C = static_cast<u32>(msg.ReadLong());
		m_remoteMatchInfo.field_210 = static_cast<u32>(msg.ReadLong());
		m_remoteMatchInfo.field_214 = static_cast<u32>(msg.ReadLong());
	}

	//-------------------------------------------------------------------------
	// Version 3+
	//-------------------------------------------------------------------------
	if (version > 2)
	{
		m_remoteMatchInfo.field_1E8 = static_cast<u8>(msg.ReadByte());
		m_remoteMatchInfo.field_1EC = static_cast<u8>(msg.ReadByte());
		m_remoteMatchInfo.field_1F0 = static_cast<u8>(msg.ReadByte());
		m_remoteMatchInfo.field_1F4 = static_cast<u8>(msg.ReadByte());
		m_remoteMatchInfo.field_1F8 = static_cast<i16>(msg.ReadShort());
		m_remoteMatchInfo.field_1FC = static_cast<i16>(msg.ReadShort());
		m_remoteMatchInfo.field_200 = static_cast<i16>(msg.ReadShort());

		// Version 6+
		if (version > 5)
		{
			m_remoteMatchInfo.field_204 = static_cast<i16>(msg.ReadShort());
		}

		//---------------------------------------------------------------------
		// Team scores
		//---------------------------------------------------------------------
		u8 index = static_cast<u8>(msg.ReadByte());

		while (index != 0xFF)
		{
			if (msg.IsOverflowed())
				break;

			const i16 value = static_cast<i16>(msg.ReadShort());

			if (index < ARRAYSIZE(m_remoteMatchInfo.teamScores))
			{
				m_remoteMatchInfo.teamScores[index] = value;
			}

			index = static_cast<u8>(msg.ReadByte());
		}
	}

	//-------------------------------------------------------------------------
	//-------------------------------------------------------------------------
	const i64 result = msg.ReadLongLong();

	if (result != 0 || m_remoteMatchInfo.field_080 != 2)
	{
		return result;
	}

	g_pHLClient->RemoteMatchInfoUpdated();
	return 0;
}

bool CClientState::ProcessDataBlockFragment(const netadr_t& from, bf_read& msg)
{
	if (!IsConnected())
		return true;

	if (!m_NetChannel)
	{
		DevWarning(eDLL_T::ENGINE, __FUNC__"(%s): got datablock packet without installed netchannel !!!\n", from.ToString());
		return true;
	}

	const u16 blockSeqNr = msg.ReadWord();
	const int transferSize = msg.ReadLong();
	const u16 cancelSeqNr = msg.ReadWord();

	const u16 fragId = msg.ReadWord();
	const int fragSize = msg.ReadLong();

	if (fragSize < 1 || fragSize > MAX_DATABLOCK_FRAGMENT_SIZE)
		return true;

	ALIGN4 u8 fragmentData[MAX_DATABLOCK_FRAGMENT_SIZE];

	if (!msg.ReadBytes(fragmentData, fragSize))
	{
		COM_ExplainDisconnection(true, "DataBlockFragment overflowed [%zu][%zu]",
			msg.TotalBytesAvailable(), static_cast<size_t>(fragSize));

		Disconnect(true);
		return false;
	}

	ClientDataBlockReceiver* const receiver = GetDataBlockReceiver();

	const bool wasTransferFinished = receiver->TransferFinished();
	const bool fragmentAccepted = receiver->ProcessFragment(
		Plat_FloatTime(),
		blockSeqNr,
		transferSize,
		cancelSeqNr,
		fragId,
		fragmentData,
		fragSize);

	const double receiveTime = *g_pNetTime;

	if (fragmentAccepted)
	{
		Assert(m_NetChannel);
		m_NetChannel->SetLastReceivedTime(receiveTime);
	}

	if (!wasTransferFinished && receiver->TransferFinished())
	{
		bool processMessagesFailed = false;

		if (fragmentAccepted)
		{
			if (m_NetChannel)
			{
				u8* const payload = receiver->GetPayloadBuffer();
				bf_read dataBlock("ProcessDataBlock", payload, receiver->GetPayloadSize());

				m_NetChannel->SetLastReceivedTime(receiveTime);
				processMessagesFailed = !m_NetChannel->ProcessMessages(&dataBlock);
			}
		}

		receiver->NextTransfer();

		if (!fragmentAccepted)
			return false;

		if (processMessagesFailed)
		{
			if (m_NetChannel)
				m_NetChannel->FreeReceiveList();

			return false;
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: process an out-of-band packet received by the client
//-----------------------------------------------------------------------------
bool CClientState::ProcessConnectionlessPacketGuts(netpacket_t* const packet)
{
	Assert(packet);

	// NOTE: msg is a reference to packet->message, so reading
	// from "msg" will also advance read-pointer in "packet->message"!!!
	// ... and vice-versa, hence passing "packet" to a nested function
	// will make that function receive a modified message with advanced
	// read pointer.
	// [ this differs from server-side connectionless packet processing ]

	bf_read& msg = packet->message;
	const u8 id = msg.ReadByte();

	switch (id)
	{
	case S2C_DISCONNECT:
	{
		return ProcessDisconnect(packet->from, msg);
	}

	case S2C_CHALLENGE:
	{
		return ProcessChallenge(packet->from, msg);
	}

	case S2C_CONNACCEPT:
	{
		return ProcessConnectionAccept(packet->from, msg);
	}

	case S2C_CONNREJECT:
	{
		return ProcessConnectionReject(packet->from, msg);
	}

	case A2A_PING:
	{
		Datacenters_ProcessPingReply(msg);
		return true;
	}

	case S2C_MATCHINFO_UPDATE:
	{
		ProcessMatchInfoUpdate(packet->from, msg);
		return true;
	}

	case S2C_DATABLOCK_FRAGMENT:
	{
		return ProcessDataBlockFragment(packet->from, msg);
	}

	default:
		return false;
	}
}

void VClientState::Detour(const bool bAttach) const
{
    DetourSetup(&CClientState__ConnectionStart, &CClientState::VConnectionStart, bAttach);
    DetourSetup(&CClientState__ConnectionClosing, &CClientState::VConnectionClosing, bAttach);
    DetourSetup(&CClientState__ProcessStringCmd, &CClientState::_ProcessStringCmd, bAttach);
    DetourSetup(&CClientState__ProcessServerTick, &CClientState::VProcessServerTick, bAttach);
    DetourSetup(&CClientState__ProcessCreateStringTable, &CClientState::_ProcessCreateStringTable, bAttach);
    DetourSetup(&CClientState__ProcessUserMessage, &CClientState::_ProcessUserMessage, bAttach);
    DetourSetup(&CClientState__ProcessConnectionlessPacket, &CClientState::_ProcessConnectionlessPacket, bAttach);
    DetourSetup(&CClientState__Connect, &CClientState::VConnect, bAttach);
}

/////////////////////////////////////////////////////////////////////////////////
CClientState* g_pClientState = nullptr;
CClientState** g_pClientState_Shifted = nullptr;
