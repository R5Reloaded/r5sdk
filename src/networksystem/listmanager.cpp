//=============================================================================//
// 
// Purpose: server list manager
// 
//-----------------------------------------------------------------------------
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier0/threadtools.h"
#include "tier0/frametask.h"
#include "tier1/cvar.h"
#include "engine/cmd.h"
#include "engine/net.h"
#include "engine/host_state.h"
#include "engine/server/server.h"
#include "rtech/playlists/playlists.h"
#include "pylon.h"
#include "listmanager.h"
#include <ebisusdk/EbisuSDK.h>
#include <misc/ImGuiNotify.hpp>

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CServerListManager::CServerListManager(void)
{
}

//-----------------------------------------------------------------------------
// Purpose: get server list from pylon
// Input  : &outMessage - 
//          &numServers - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CServerListManager::RefreshServerList(string& outMessage, size_t& numServers)
{
    ClearServerList();

    vector<NetGameServer_t> serverList;
    const bool success = g_MasterServer.GetServerList(serverList, outMessage);

    if (!success)
        return false;

    AUTO_LOCK(m_Mutex);
    m_vServerList = std::move(serverList);

    numServers = m_vServerList.size();
    return true;
}

//-----------------------------------------------------------------------------
// Purpose: clears the server list
//-----------------------------------------------------------------------------
void CServerListManager::ClearServerList(void)
{
    AUTO_LOCK(m_Mutex);
    m_vServerList.clear();
}

//-----------------------------------------------------------------------------
// Purpose: connects to specified server
// Input  : &svIp - 
//          nPort - 
//          &svNetKey - 
//-----------------------------------------------------------------------------
void CServerListManager::ConnectToServer(const string& svIp, const int nPort, const string& svNetKey) const
{
    if (!ThreadInMainThread())
    {
        g_TaskQueue.Dispatch([this, svIp, nPort, svNetKey]()
            {
                this->ConnectToServer(svIp, nPort, svNetKey);
            }, 0);
        return;
    }

    if (!svNetKey.empty())
    {
        NET_SetKey(svNetKey);
    }

    const string command = Format("%s \"[%s]:%i\"", "connect", svIp.c_str(), nPort);
    Cbuf_AddText(Cbuf_GetCurrentPlayer(), command.c_str(), cmd_source_t::kCommandSrcCode);
}

//-----------------------------------------------------------------------------
// Purpose: connects to specified server
// Input  : &svServer - 
//          &svNetKey - 
//-----------------------------------------------------------------------------
void CServerListManager::ConnectToServer(const string& svServer, const string& svNetKey) const
{
    if (!ThreadInMainThread())
    {
        g_TaskQueue.Dispatch([this, svServer, svNetKey]()
            {
                this->ConnectToServer(svServer, svNetKey);
            }, 0);
        return;
    }

    if (!svNetKey.empty())
    {
        NET_SetKey(svNetKey);
    }

    const string command = Format("%s \"%s\"", "connect", svServer.c_str()).c_str();
    Cbuf_AddText(Cbuf_GetCurrentPlayer(), command.c_str(), cmd_source_t::kCommandSrcCode);
}


static ConVar cl_onlineAuthEnable("cl_onlineAuthEnable", "1", FCVAR_RELEASE, "Enables the client-side online authentication system");

static ConVar cl_onlineAuthToken("cl_onlineAuthToken", "", FCVAR_USERINFO | FCVAR_DONTRECORD | FCVAR_SERVER_CANNOT_QUERY | FCVAR_PLATFORM_SYSTEM, "The client's online authentication token");
static ConVar cl_onlineAuthTokenSignature1("cl_onlineAuthTokenSignature1", "", FCVAR_USERINFO | FCVAR_DONTRECORD | FCVAR_SERVER_CANNOT_QUERY | FCVAR_PLATFORM_SYSTEM, "The client's online authentication token signature", false, 0.f, false, 0.f, "Primary");
static ConVar cl_onlineAuthTokenSignature2("cl_onlineAuthTokenSignature2", "", FCVAR_USERINFO | FCVAR_DONTRECORD | FCVAR_SERVER_CANNOT_QUERY | FCVAR_PLATFORM_SYSTEM, "The client's online authentication token signature", false, 0.f, false, 0.f, "Secondary");


bool ServerList_SetTokenCVars(const string& msToken)
{
    // get full token
    const char* token = msToken.c_str();

    // get a pointer to the delimiter that begins the token's signature
    const char* tokenSignatureDelim = strrchr(token, '.');

    if (!tokenSignatureDelim)
    {
        Warning(eDLL_T::ENGINE, "ServerList_SetTokenCVars: Invalid token returned by MS");
        //FORMAT_ERROR_REASON("Invalid token returned by MS");
        return false;
    }

    const size_t sigLength = strlen(tokenSignatureDelim + 1);
    // replace the delimiter with a null char so the first cvar only takes the header and payload data
    *(char*)tokenSignatureDelim = '\0';

    cl_onlineAuthToken.SetValue(token);

    if (sigLength > 0)
    {
        // get a pointer to the first part of the token signature to store in cl_onlineAuthTokenSignature1
        const char* tokenSignaturePart1 = tokenSignatureDelim + 1;

        cl_onlineAuthTokenSignature1.SetValue(tokenSignaturePart1);

        if (sigLength > 255)
        {
            // get a pointer to the rest of the token signature to store in cl_onlineAuthTokenSignature2
            const char* tokenSignaturePart2 = tokenSignaturePart1 + 255;

            cl_onlineAuthTokenSignature2.SetValue(tokenSignaturePart2);
        }
    }

    return true;
}

void CServerListManager::ConnectToServerById(string svId) const
{
    ImGui::InsertNotification({ ImGuiToastType::Info, 3000, "Connecting..." });

    std::thread request([this, svId = std::move(svId)] {
        string msToken;
        string message;
        MSConnectionInfo_t connInfo;

        const string authCode = cl_onlineAuthEnable.GetBool() ? g_OriginAuthCode : "";
        const bool bSuccess = g_MasterServer.AuthForConnection(*g_NucleusID, svId, authCode.c_str(), msToken, connInfo, message);

        g_TaskQueue.Dispatch([this, bSuccess, message = std::move(message), connInfo = std::move(connInfo), msToken = std::move(msToken)] {
            
            ServerList_SetTokenCVars(msToken);

            if (!bSuccess)
            {
                Error(eDLL_T::MS, ERROR_SUCCESS, "ConnectToServer: %s\n", message.c_str());
                ImGui::InsertNotification({ ImGuiToastType::Error, 5000, "Failed to connect!\n%s", message.c_str() });
                return;
            }

            this->ConnectToServer(connInfo.addr, connInfo.port, connInfo.key);
        
        }, 0);
    });

    request.detach();
}

CServerListManager g_ServerListManager;
