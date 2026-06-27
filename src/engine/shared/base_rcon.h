#ifndef BASE_RCON_H
#define BASE_RCON_H

#include "netcon/INetCon.h"
#include "tier1/NetAdr.h"
#include "tier2/cryptutils.h"
#include "tier2/socketcreator.h"
#include "protobuf/message_lite.h"

class CNetConBase
{
public:
	CNetConBase(const bool isServer) : m_Socket(isServer)
	{
		memset(m_NetKey, 0, sizeof(m_NetKey));
	}

	void SetKey(const char* pBase64NetKey, const bool bUseDefaultOnFailure = false);
	const char* GetKey(void) const;

	virtual bool Connect(const char* pHostName, const int nHostPort = SOCKET_ERROR);
	virtual void Disconnect(const char* szReason = nullptr) { NOTE_UNUSED(szReason); };

	virtual bool ProcessBuffer(ConnectedNetConsoleData_s& data, const byte* pRecvBuf, u32 nRecvLen, const u32 nMaxLen);
	virtual bool ProcessMessage(ConnectedNetConsoleData_s& /*data*/, const u32 /*nMaxLen*/) { return true; };

	virtual bool Encrypt(CryptoContext_s& ctx, const byte* pInBuf, byte* pOutBuf, const u32 nDataLen, const u8* pAad, const size_t nAadLen) const;
	virtual bool Decrypt(CryptoContext_s& ctx, const byte* pInBuf, byte* pOutBuf, const u32 nDataLen, const u8* pAad, const size_t nAadLen) const;

	virtual bool Encode(google::protobuf::MessageLite* pMsg, byte* pMsgBuf, const u32 nMsgLen) const;
	virtual bool Decode(google::protobuf::MessageLite* pMsg, const byte* pMsgBuf, const u32 nMsgLen) const;

	virtual bool Send(const SocketHandle_t hSocket, const byte* pMsgBuf, const u32 nMsgLen) const;
	virtual void Recv(ConnectedNetConsoleData_s& data, const u32 nMaxLen);

	CSocketCreator* GetSocketCreator(void) { return &m_Socket; }
	netadr_t* GetNetAddress(void) { return &m_Address; }

protected:
	CSocketCreator m_Socket;
	netadr_t m_Address;
	CryptoKey_t m_NetKey;
	CUtlString m_Base64NetKey;
};

#endif // BASE_RCON_H
