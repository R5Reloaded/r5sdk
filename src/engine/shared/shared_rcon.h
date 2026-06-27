#ifndef SHARED_RCON_H
#define SHARED_RCON_H
#include "base_rcon.h"
#include "protoc/netcon.pb.h"

#ifndef _TOOLS
extern ConVar rcon_debug;
extern ConVar rcon_encryptframes;
extern ConVar rcon_key;
extern ConVar rcon_maxframesize;

#ifndef CLIENT_DLL
extern void RCON_InitServerAndTrySyncKeys(const char* pPassword);
#endif // !CLIENT_DLL
#ifndef DEDICATED
extern void RCON_InitClientAndTrySyncKeys();
#endif // !DEDICATED
#endif // _TOOLS

bool NetconServer_Serialize(const CNetConBase* pBase, ConnectedNetConsoleData_s& data, vector<byte>& vecBuf,
	const char* pResponseMsg, const size_t nResponseMsgLen, const char* pResponseVal, const size_t nResponseValLen,
	const netcon::response_e responseType, const int nMessageId, const int nMessageType, const bool bEncrypt, const bool bDebug);

bool NetconClient_Serialize(const CNetConBase* pBase, ConnectedNetConsoleData_s& data, vector<byte>& vecBuf, const char* szReqBuf, const size_t nReqMsgLen,
	const char* szReqVal, const size_t nReqValLen, const netcon::request_e requestType, const bool bEncrypt, const bool bDebug);
bool NetconClient_Connect(CNetConBase* pBase, const char* pHostAdr, const int nHostPort);

bool NetconShared_PackEnvelope(const CNetConBase* pBase, ConnectedNetConsoleData_s& data, vector<byte>& outMsgBuf, const u32 nMsgLen, google::protobuf::MessageLite* const inMsg, const bool bEncrypt, const bool bDebug);
bool NetconShared_UnpackEnvelope(const CNetConBase* pBase, ConnectedNetConsoleData_s& data, const u32 nMaxLen, google::protobuf::MessageLite* const outMsg, const bool bDecrypt, const bool bDebug);

ConnectedNetConsoleData_s* NetconShared_GetConnData(CNetConBase* pBase, const int iSocket);
SocketHandle_t NetconShared_GetSocketHandle(CNetConBase* pBase, const int iSocket);

#endif // SHARED_RCON_H
