//=============================================================================//
// 
// Purpose: Expose native code to VScript API
// 
//-----------------------------------------------------------------------------
// 
// Read the documentation in 'game/shared/vscript_shared.cpp' before modifying
// existing code or adding new code!
// 
// To create client script bindings:
// - use the DEFINE_CLIENT_SCRIPTFUNC_NAMED() macro.
// - prefix your function with "ClientScript_" i.e.: "ClientScript_GetVersion".
// 
// To create ui script bindings:
// - use the DEFINE_UI_SCRIPTFUNC_NAMED() macro.
// - prefix your function with "UIScript_" i.e.: "UIScript_GetVersion".
// 
//=============================================================================//

#include "core/stdafx.h"
#include "tier0/frametask.h"
#include "tier1/keyvalues.h"
#include "engine/cmodel_bsp.h"
#include "engine/host_state.h"
#include "engine/debugoverlay.h"
#include "pluginsystem/pluginsystem.h"
#include "networksystem/pylon.h"
#include "networksystem/listmanager.h"
#include "networksystem/hostmanager.h"

#include "vscript/vscript.h"
#include "vscript/languages/squirrel_re/include/sqvm.h"

#include "game/shared/vscript_gamedll_defs.h"

#include "game/shared/vscript_shared.h"
#include "game/shared/vscript_debug_overlay_shared.h"

#include "vscript_client.h"

/*
=====================
SQVM_ClientScript_f

  Executes input on the
  VM in CLIENT context.
=====================
*/
static void SQVM_ClientScript_f(const CCommand& args)
{
    if (args.ArgC() >= 2)
    {
        Script_Execute(args.ArgS(), SQCONTEXT::CLIENT);
    }
}

/*
=====================
SQVM_UIScript_f

  Executes input on the
  VM in UI context.
=====================
*/
static void SQVM_UIScript_f(const CCommand& args)
{
    if (args.ArgC() >= 2)
    {
        Script_Execute(args.ArgS(), SQCONTEXT::UI);
    }
}

static ConCommand script_client("script_client", SQVM_ClientScript_f, "Run input code as CLIENT script on the VM", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL | FCVAR_CHEAT);
static ConCommand script_ui("script_ui", SQVM_UIScript_f, "Run input code as UI script on the VM", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL | FCVAR_CHEAT);

//-----------------------------------------------------------------------------
// Purpose: client NDebugOverlay proxies
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_DebugDrawSolidBox(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSolidBox(v);
}
static SQRESULT ClientScript_DebugDrawSweptBox(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSweptBox(v);
}
static SQRESULT ClientScript_DebugDrawTriangle(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawTriangle(v);
}
static SQRESULT ClientScript_DebugDrawSolidSphere(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawSolidSphere(v);
}
static SQRESULT ClientScript_DebugDrawCapsule(HSQUIRRELVM v)
{
    return SharedScript_DebugDrawCapsule(v);
}

//-----------------------------------------------------------------------------
// Purpose: internal handler for adding debug texts on screen through scripts
//-----------------------------------------------------------------------------
static void ClientScript_Internal_DebugScreenTextWithColor(HSQUIRRELVM v, const float posX, const float posY, const Color color, const char* const text)
{
    g_pDebugOverlay->AddScreenTextOverlay(posX, posY, NDEBUG_PERSIST_TILL_NEXT_CLIENT, color.r(), color.g(), color.b(), color.a(), text);
}

//-----------------------------------------------------------------------------
// Purpose: adds a debug text on the screen at given position
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_DebugScreenText(HSQUIRRELVM v)
{
    if (g_pDebugOverlay)
    {
        SQFloat posX;
        SQFloat posY;
        const SQChar* text = nullptr;

        sq_getfloat(v, 2, &posX);
        sq_getfloat(v, 3, &posY);
        sq_getstring(v, 4, &text);

        const Color color(255, 255, 255, 255);

        ClientScript_Internal_DebugScreenTextWithColor(v, posX, posY, color, text);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: adds a debug text on the screen at given position with color
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_DebugScreenTextWithColor(HSQUIRRELVM v)
{
    if (g_pDebugOverlay)
    {
        SQFloat posX;
        SQFloat posY;
        const SQChar* text = nullptr;
        const SQVector3D* colorVec = nullptr;

        sq_getfloat(v, 2, &posX);
        sq_getfloat(v, 3, &posY);
        sq_getstring(v, 4, &text);
        sq_getvector(v, 5, &colorVec);

        const Color color = Script_VectorToColor(colorVec, 1.0f);
        ClientScript_Internal_DebugScreenTextWithColor(v, posX, posY, color, text);
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the most recent server id
//-----------------------------------------------------------------------------
static SQRESULT ClientScript_GetServerID(HSQUIRRELVM v)
{
    sq_pushstring(v, Host_GetSessionID(), -1);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: checks if the server index is valid, raises an error if not
//-----------------------------------------------------------------------------
static SQBool Script_CheckServerIndexAndFailure(HSQUIRRELVM v, SQInteger iServer)
{
    SQInteger iCount = static_cast<SQInteger>(g_ServerListManager.m_vServerList.size());

    if (iServer >= iCount)
    {
        v_SQVM_RaiseError(v, "Index must be less than %i.\n", iCount);
        return false;
    }
    else if (iServer == -1) // If its still -1, then 'sq_getinteger' failed
    {
        v_SQVM_RaiseError(v, "Invalid argument type provided.\n");
        return false;
    }

    return true;
}

static void Internal_UIScript_RequestForServerBrowserListThreaded()
{
    string responseMsg;
    size_t serverCount;

    const bool success = g_ServerListManager.RefreshServerList(responseMsg, serverCount);

    g_TaskQueue.Dispatch([success, errorMsg = std::move(responseMsg), serverCount]
        {
            if (!g_pUIScript)
                return;

            HSCRIPT onRequestComplete = g_pUIScript->FindFunction("UICodeCallback_OnServerListRequestCompleted",
                "void functionref( bool success, string errorMsg, int serverCount )", nullptr);

            if (!onRequestComplete)
                return;

            ScriptVariant_t args[3] = { success, errorMsg.c_str(), (int)serverCount };
            g_pUIScript->ExecuteFunction(onRequestComplete, args, SDK_ARRAYSIZE(args), nullptr, 0);

            free(onRequestComplete);
        }, 0);
}

//-----------------------------------------------------------------------------
// Purpose: refreshes the server list
//-----------------------------------------------------------------------------
static SQRESULT UIScript_RequestServerList(HSQUIRRELVM v)
{
    std::thread(Internal_UIScript_RequestForServerBrowserListThreaded).detach();
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get current server count from pylon
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerCount(HSQUIRRELVM v)
{
    size_t iCount = g_ServerListManager.m_vServerList.size();
    sq_pushinteger(v, static_cast<SQInteger>(iCount));

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get response from private server request
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetHiddenServerName(HSQUIRRELVM v)
{
    const SQChar* privateToken = nullptr;

    if (SQ_FAILED(sq_getstring(v, 2, &privateToken)) || !VALID_CHARSTAR(privateToken))
    {
        v_SQVM_ScriptError("Empty or null private token");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    string hiddenServerRequestMessage;
    NetGameServer_t serverListing;

    bool result = g_MasterServer.GetServerByToken(serverListing, hiddenServerRequestMessage, privateToken); // Send token connect request.
    if (!result)
    {
        if (hiddenServerRequestMessage.empty())
            sq_pushstring(v, "Request failed", -1);
        else
        {
            hiddenServerRequestMessage = Format("Request failed: %s", hiddenServerRequestMessage.c_str());
            sq_pushstring(v, hiddenServerRequestMessage.c_str(), (SQInteger)hiddenServerRequestMessage.length());
        }

        SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
    }

    if (serverListing.name.empty())
    {
        if (hiddenServerRequestMessage.empty())
            hiddenServerRequestMessage = Format("Server listing empty");
        else
            hiddenServerRequestMessage = Format("Server listing empty: %s", hiddenServerRequestMessage.c_str());

        sq_pushstring(v, hiddenServerRequestMessage.c_str(), (SQInteger)hiddenServerRequestMessage.length());
    }
    else
    {
        hiddenServerRequestMessage = Format("Found server: %s", serverListing.name.c_str());
        sq_pushstring(v, hiddenServerRequestMessage.c_str(), (SQInteger)hiddenServerRequestMessage.length());
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current name from server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerName(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const string& serverName = g_ServerListManager.m_vServerList[iServer].name;
    sq_pushstring(v, serverName.c_str(), (SQInteger)serverName.length());

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current description from server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerDescription(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const string& serverDescription = g_ServerListManager.m_vServerList[iServer].description;
    sq_pushstring(v, serverDescription.c_str(), (SQInteger)serverDescription.length());

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current map via server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerMap(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const string& serverMapName = g_ServerListManager.m_vServerList[iServer].map;
    sq_pushstring(v, serverMapName.c_str(), (SQInteger)serverMapName.length());

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current playlist via server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerPlaylist(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const string& serverPlaylist = g_ServerListManager.m_vServerList[iServer].playlist;
    sq_pushstring(v, serverPlaylist.c_str(), (SQInteger)serverPlaylist.length());

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current player count via server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerCurrentPlayers(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const SQInteger playerCount = g_ServerListManager.m_vServerList[iServer].numPlayers;
    sq_pushinteger(v, playerCount);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get server's current player count via server list index
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerMaxPlayers(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);

    const SQInteger maxPlayers = g_ServerListManager.m_vServerList[iServer].maxPlayers;
    sq_pushinteger(v, maxPlayers);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: gets the most recent server id
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetServerID(HSQUIRRELVM v)
{
    sq_pushstring(v, Host_GetSessionID(), -1);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: get promo data for server browser panels
//-----------------------------------------------------------------------------
static SQRESULT UIScript_GetPromoData(HSQUIRRELVM v)
{
    enum class R5RPromoData : SQInteger
    {
        PromoLargeTitle,
        PromoLargeDesc,
        PromoLeftTitle,
        PromoLeftDesc,
        PromoRightTitle,
        PromoRightDesc
    };

    SQInteger idx = 0;
    sq_getinteger(v, 2, &idx);

    R5RPromoData ePromoIndex = static_cast<R5RPromoData>(idx);
    const char* pszPromoKey;

    switch (ePromoIndex)
    {
    case R5RPromoData::PromoLargeTitle:
    {
        pszPromoKey = "#PROMO_LARGE_TITLE";
        break;
    }
    case R5RPromoData::PromoLargeDesc:
    {
        pszPromoKey = "#PROMO_LARGE_DESCRIPTION";
        break;
    }
    case R5RPromoData::PromoLeftTitle:
    {
        pszPromoKey = "#PROMO_LEFT_TITLE";
        break;
    }
    case R5RPromoData::PromoLeftDesc:
    {
        pszPromoKey = "#PROMO_LEFT_DESCRIPTION";
        break;
    }
    case R5RPromoData::PromoRightTitle:
    {
        pszPromoKey = "#PROMO_RIGHT_TITLE";
        break;
    }
    case R5RPromoData::PromoRightDesc:
    {
        pszPromoKey = "#PROMO_RIGHT_DESCRIPTION";
        break;
    }
    default:
    {
        pszPromoKey = "#PROMO_SDK_ERROR";
        break;
    }
    }

    sq_pushstring(v, pszPromoKey, -1);
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static void Internal_UIScript_RequestEULAThreaded()
{
    MSEulaData_t eulaDataMs;
    string responseMsg;

    const bool success = g_MasterServer.GetEULA(eulaDataMs, responseMsg);

    g_TaskQueue.Dispatch([success, errorMsg = std::move(responseMsg), eulaData = std::move(eulaDataMs)]
        {
            if (success)
            {
                // set EULA version cvar to the newly fetched EULA version
                eula_version->SetValue(eulaData.version);
            }

            if (!g_pUIScript)
                return;

            HSCRIPT onRequestComplete = g_pUIScript->FindFunction("UICodeCallback_OnEULARequestCompleted",
                "void functionref( bool success, string errorMsg, string language, string eulaData )", nullptr);

            if (!onRequestComplete)
                return;

            ScriptVariant_t args[4] = { success, errorMsg.c_str(), eulaData.language.c_str(), eulaData.contents.c_str() };
            g_pUIScript->ExecuteFunction(onRequestComplete, args, SDK_ARRAYSIZE(args), nullptr, 0);

            free(onRequestComplete);
        }, 0);
}

static SQRESULT UIScript_RequestEULAContents(HSQUIRRELVM v)
{
    std::thread(Internal_UIScript_RequestEULAThreaded).detach();
    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: connect to server from native server browser entries
//-----------------------------------------------------------------------------
static SQRESULT UIScript_ConnectToServer(HSQUIRRELVM v)
{
    const SQChar* ipAddress = nullptr;
    if (SQ_FAILED(sq_getstring(v, 2, &ipAddress)))
    {
        v_SQVM_ScriptError("Missing ip address");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const SQChar* cryptoKey = nullptr;
    if (SQ_FAILED(sq_getstring(v, 3, &cryptoKey)))
    {
        v_SQVM_ScriptError("Missing encryption key");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    Msg(eDLL_T::UI, "Connecting to server with ip address '%s' and encryption key '%s'\n", ipAddress, cryptoKey);
    g_ServerListManager.ConnectToServer(ipAddress, cryptoKey);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: set netchannel encryption key and connect to server
//-----------------------------------------------------------------------------
static SQRESULT UIScript_ConnectToListedServer(HSQUIRRELVM v)
{
    AUTO_LOCK(g_ServerListManager.m_Mutex);

    SQInteger iServer = -1;
    sq_getinteger(v, 2, &iServer);

    if (!Script_CheckServerIndexAndFailure(v, iServer))
    {
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    const NetGameServer_t& gameServer = g_ServerListManager.m_vServerList[iServer];

    g_ServerListManager.ConnectToServerById(gameServer.serverId);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: request token from pylon and join server with result.
//-----------------------------------------------------------------------------
static SQRESULT UIScript_ConnectToHiddenServer(HSQUIRRELVM v)
{
    const SQChar* privateToken = nullptr;
    const SQRESULT strRet = sq_getstring(v, 2, &privateToken);

    if (SQ_FAILED(strRet) || !VALID_CHARSTAR(privateToken))
    {
        v_SQVM_ScriptError("Empty or null private token");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    string hiddenServerRequestMessage;
    NetGameServer_t netListing;

    const bool result = g_MasterServer.GetServerByToken(netListing, hiddenServerRequestMessage, privateToken); // Send token connect request.
    if (result)
    {
        g_ServerListManager.ConnectToServerById(netListing.serverId);
    }
    else
    {
        Warning(eDLL_T::UI, "Failed to connect to private server: %s\n", hiddenServerRequestMessage.c_str());
    }

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: create server via native server browser entries
// TODO: return a boolean on failure instead of raising an error, so we could
// determine from scripts whether or not to spin a local server, or connect
// to a dedicated server (for disconnecting and loading the lobby, for example)
//-----------------------------------------------------------------------------
static SQRESULT UIScript_CreateServer(HSQUIRRELVM v)
{
    const SQChar* serverName = nullptr;
    const SQChar* serverDescription = nullptr;
    const SQChar* serverMapName = nullptr;
    const SQChar* serverPlaylist = nullptr;

    sq_getstring(v, 2, &serverName);
    sq_getstring(v, 3, &serverDescription);
    sq_getstring(v, 4, &serverMapName);
    sq_getstring(v, 5, &serverPlaylist);

    SQInteger serverVisibility = 0;
    sq_getinteger(v, 6, &serverVisibility);

    if (!VALID_CHARSTAR(serverName) ||
        !VALID_CHARSTAR(serverMapName) ||
        !VALID_CHARSTAR(serverPlaylist))
    {
        v_SQVM_ScriptError("Empty or null server criteria");
        SCRIPT_CHECK_AND_RETURN(v, SQ_ERROR);
    }

    hostname->SetValue(serverName);
    hostdesc.SetValue(serverDescription);

    pylon_host_visibility.SetValue((int)serverVisibility);

    // Launch server.
    g_ServerHostManager.LaunchServer(serverMapName, serverPlaylist);

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//-----------------------------------------------------------------------------
// Purpose: shuts the server down and disconnects all clients
//-----------------------------------------------------------------------------
static SQRESULT UIScript_DestroyServer(HSQUIRRELVM v)
{
    if (g_pHostState->m_bActiveGame)
        g_pHostState->m_iNextState = HostStates_t::HS_GAME_SHUTDOWN;

    SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

//---------------------------------------------------------------------------------
// Purpose: registers script functions in CLIENT context
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterClientFunctions(CSquirrelVM* s)
{
    Script_RegisterCommonAbstractions(s);
    Script_RegisterCoreClientFunctions(s);

    // NOTE: plugin functions must always come after SDK functions!
    for (auto& callback : !PluginSystem()->GetRegisterClientScriptFuncsCallbacks())
    {
        // Register script functions inside plugins.
        callback.Function()(s);
    }
}

//---------------------------------------------------------------------------------
// Purpose: core client script functions
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterCoreClientFunctions(CSquirrelVM* s)
{
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, DebugDrawSolidBox, "Draw a debug overlay solid box", "void", "vector origin, vector mins, vector maxs, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, DebugDrawSweptBox, "Draw a debug overlay swept box", "void", "vector start, vector end, vector mins, vector maxs, vector angles, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, DebugDrawTriangle, "Draw a debug overlay triangle", "void", "vector p1, vector p2, vector p3, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, DebugDrawSolidSphere, "Draw a debug overlay solid sphere", "void", "vector origin, float radius, int theta, int phi, vector color, float alpha, bool drawThroughWorld, float duration", false);
    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, DebugDrawCapsule, "Draw a debug overlay capsule", "void", "vector start, vector end, float radius, vector color, float alpha, bool drawThroughWorld, float duration", false);

    DEFINE_CLIENT_SCRIPTFUNC_NAMED(s, GetServerID, "Gets the ID of the most recent server", "string", "", false);
}

//---------------------------------------------------------------------------------
// Purpose: registers script functions in UI context
// Input  : *s - 
//---------------------------------------------------------------------------------
void Script_RegisterUIFunctions(CSquirrelVM* s)
{
    Script_RegisterCommonAbstractions(s);

    DEFINE_UI_SCRIPTFUNC_NAMED(s, RequestServerList, "Requests the latest public server list, calls UICodeCallback_OnServerListRequestCompleted on completion", "void", "", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerCount, "Gets the number of public servers", "int", "", false);

    // Functions for retrieving server browser data
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetHiddenServerName, "Gets hidden server name by token", "string", "string token", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerName, "Gets the name of the server at the specified index of the server list", "string", "int index", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerDescription, "Gets the description of the server at the specified index of the server list", "string", "int index", false);

    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerMap, "Gets the map of the server at the specified index of the server list", "string", "int index", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerPlaylist, "Gets the playlist of the server at the specified index of the server list", "string", "int index", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerCurrentPlayers, "Gets the current player count of the server at the specified index of the server list", "int", "int index", false);

    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerMaxPlayers, "Gets the max player count of the server at the specified index of the server list", "int", "int index", false);

    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetServerID, "Gets the ID of the most recent server", "string", "", false);

    // Misc main menu functions
    DEFINE_UI_SCRIPTFUNC_NAMED(s, RequestEULAContents, "Requests the latest online EULA contents, calls UICodeCallback_OnEULARequestCompleted on completion", "void", "", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, GetPromoData, "Gets promo data for specified slot type", "string", "int slotType", false);

    // Functions for connecting to servers
    DEFINE_UI_SCRIPTFUNC_NAMED(s, ConnectToServer, "Joins server by ip address and encryption key", "void", "string address, string key", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, ConnectToListedServer, "Joins listed server by index", "void", "int index", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, ConnectToHiddenServer, "Joins hidden server by token", "void", "string token", false);

    // NOTE: plugin functions must always come after SDK functions!
    for (auto& callback : !PluginSystem()->GetRegisterUIScriptFuncsCallbacks())
    {
        // Register script functions inside plugins.
        callback.Function()(s);
    }
}

void Script_RegisterUIServerFunctions(CSquirrelVM* s)
{
    DEFINE_UI_SCRIPTFUNC_NAMED(s, CreateServer, "Starts server with the specified settings", "void", "string name, string description, string levelName, string playlistName, int visibilityMode", false);
    DEFINE_UI_SCRIPTFUNC_NAMED(s, DestroyServer, "Shuts the local server down", "void", "", false);
}

//---------------------------------------------------------------------------------
// Purpose: console variables for scripts, these should not be used in engine/sdk code !!!
//---------------------------------------------------------------------------------
static ConVar settings_reflex("settings_reflex", "1", FCVAR_RELEASE, "Selected NVIDIA Reflex mode.", "0 = Off. 1 = On. 2 = On + Boost.");
static ConVar settings_antilag("settings_antilag", "1", FCVAR_RELEASE, "Selected AMD Anti-Lag mode.", "0 = Off. 1 = On.");

// NOTE: if we want to make a certain promo only show once, add the playerprofile flag to the cvar below. Current behavior = always show after game restart.
static ConVar promo_version_accepted("promo_version_accepted", "0", FCVAR_RELEASE, "The accepted promo version.");

//---------------------------------------------------------------------------------
// Purpose: script code class function registration
//---------------------------------------------------------------------------------
static void Script_RegisterClientEntityClassFuncs()
{
    v_Script_RegisterClientEntityClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientPlayerClassFuncs()
{
    v_Script_RegisterClientPlayerClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientAIClassFuncs()
{
    v_Script_RegisterClientAIClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientWeaponClassFuncs()
{
    v_Script_RegisterClientWeaponClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientProjectileClassFuncs()
{
    v_Script_RegisterClientProjectileClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientTitanSoulClassFuncs()
{
    v_Script_RegisterClientTitanSoulClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientPlayerDecoyClassFuncs()
{
    v_Script_RegisterClientPlayerDecoyClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}
//---------------------------------------------------------------------------------
static void Script_RegisterClientFirstPersonProxyClassFuncs()
{
    v_Script_RegisterClientFirstPersonProxyClassFuncs();
    static bool initialized = false;

    if (initialized)
        return;

    initialized = true;
}

void VScriptClient::Detour(const bool bAttach) const
{
    DetourSetup(&v_ClientScript_DebugScreenText, &ClientScript_DebugScreenText, bAttach);
    DetourSetup(&v_ClientScript_DebugScreenTextWithColor, &ClientScript_DebugScreenTextWithColor, bAttach);

    DetourSetup(&v_Script_RegisterClientEntityClassFuncs, &Script_RegisterClientEntityClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientPlayerClassFuncs, &Script_RegisterClientPlayerClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientAIClassFuncs, &Script_RegisterClientAIClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientWeaponClassFuncs, &Script_RegisterClientWeaponClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientProjectileClassFuncs, &Script_RegisterClientProjectileClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientTitanSoulClassFuncs, &Script_RegisterClientTitanSoulClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientPlayerDecoyClassFuncs, &Script_RegisterClientPlayerDecoyClassFuncs, bAttach);
    DetourSetup(&v_Script_RegisterClientFirstPersonProxyClassFuncs, &Script_RegisterClientFirstPersonProxyClassFuncs, bAttach);
}
