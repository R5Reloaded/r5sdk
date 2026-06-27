//===========================================================================//
//
// Purpose: 
//
//===========================================================================//
#pragma once
#include "tier1/cmd.h"
#include "protoc/netcon.pb.h"
#include "engine/shared/base_rcon.h"

constexpr const char* NETCON_VERSION = "2.0.0.1";
constexpr const u32 NETCON_MAX_FRAME_SIZE = 4096;

class CNetCon : public CNetConBase
{
public:
	CNetCon(void);
	~CNetCon(void);

	bool Init(const bool bAnsiColor, const char* pAdr = nullptr, const char* pKey = nullptr);
	bool Shutdown(void);
	void TermSetup(const bool bAnsiColor);

	void RunInput(const string& lineInput);
	bool RunFrame(void);

	bool GetQuitting(void) const;
	void SetQuitting(const bool bQuit);

	bool GetPrompting(void) const;
	void SetPrompting(const bool bPrompt);

	inline float GetTickInterval() const { return m_flTickInterval; }
	static BOOL WINAPI CloseHandler(DWORD eventCode);

	virtual bool Connect(const char* pHostName, const int nHostPort = SOCKET_ERROR) override;
	virtual void Disconnect(const char* szReason = nullptr) override;

	virtual bool ProcessMessage(ConnectedNetConsoleData_s& data, const u32 nMaxLen) override;

	void TrySetKey(const char* const pKey);
	bool Serialize(ConnectedNetConsoleData_s& data, vector<byte>& vecBuf, const char* szReqBuf, const size_t nReqMsgLen,
		const char* szReqVal, const size_t nReqValLen, const netcon::request_e requestType) const;

	SocketHandle_t GetSocket(void);
	ConnectedNetConsoleData_s* GetData(void);

	bool IsInitialized(void) const;
	bool IsConnected(void);

private:
	bool m_bInitialized;
	bool m_bQuitting;
	bool m_bPromptConnect;
	bool m_bEncryptFrames;
	float m_flTickInterval;

	characterset_t m_CharacterSet;
	mutable CThreadMutex m_Mutex;
};

//-----------------------------------------------------------------------------
// singleton
//-----------------------------------------------------------------------------
extern CNetCon* NetConsole();
