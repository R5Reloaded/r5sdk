#pragma once
#include "tier1/NetAdr.h"
#include "tier2/socketcreator.h"
#include "tier2/netadrutils.h"
#include "protoc/netcon.pb.h"
#include "engine/shared/base_rcon.h"

#define RCON_MIN_PASSWORD_LEN 8
#define RCON_MAX_BANNEDLIST_SIZE 512
#define RCON_SHA512_HASH_SIZE 64

class CRConServer : public CNetConBase
{
public:
	CRConServer(void);
	~CRConServer(void);

	void Init(const char* pPassword, const char* pNetKey = nullptr);
	void Shutdown(void);

	void Reboot(void);

	bool SetPassword(const char* pszPassword);
	bool SetWhiteListAddress(const char* pszAddress);

	void Think(void);
	void RunFrame(void);

	bool SendEncoded(const char* pResponseMsg, const size_t nResponseMsgLen, const char* pResponseVal, const size_t nResponseValLen,
		const netcon::response_e responseType,
		const int nMessageId = static_cast<int>(eDLL_T::NETCON),
		const int nMessageType = static_cast<int>(LogType_t::LOG_NET));

	bool SendEncoded(ConnectedNetConsoleData_s& data, const char* pResponseMsg, const size_t nResponseMsgLen,
		const char* pResponseVal, const size_t nResponseValLen, const netcon::response_e responseType,
		const int nMessageId = static_cast<int>(eDLL_T::NETCON),
		const int nMessageType = static_cast<int>(LogType_t::LOG_NET)) const;

	bool SendToAll(const byte* pMsgBuf, const u32 nMsgLen) const;
	bool Serialize(ConnectedNetConsoleData_s& data, vector<byte>& vecBuf, const char* pResponseMsg, const size_t nResponseMsgLen, const char* pResponseVal, const size_t nResponseValLen,
		const netcon::response_e responseType, const int nMessageId = static_cast<int>(eDLL_T::NETCON), const int nMessageType = static_cast<int>(LogType_t::LOG_NET)) const;

	void Authenticate(const netcon::request& request, ConnectedNetConsoleData_s& data);
	bool Comparator(const string& svPassword) const;

	virtual bool ProcessMessage(ConnectedNetConsoleData_s& data, const u32 nMaxLen) override;

	void Execute(const netcon::request& request) const;
	bool CheckForBan(ConnectedNetConsoleData_s& data);

	virtual void Disconnect(const char* szReason = nullptr) override;
	void Disconnect(const int nIndex, const char* szReason = nullptr);
	void CloseNonAuthConnection(void);

	bool ShouldSend(const netcon::response_e responseType) const;
	bool IsInitialized(void) const;

	int GetAuthenticatedCount(void) const;
	void CloseAllSockets() { m_Socket.CloseAllAcceptedSockets(); }

private:
	int                      m_nConnIndex;
	int                      m_nAuthConnections;
	bool                     m_bInitialized;
	bool                     m_bSocketFailure;
	std::unordered_set<IPv6Wrapper_s, IPv6Hasher_s> m_BannedList;
	uint8_t                  m_PasswordHash[RCON_SHA512_HASH_SIZE];
	netadr_t                 m_WhiteListAddress;
};

CRConServer* RCONServer();
