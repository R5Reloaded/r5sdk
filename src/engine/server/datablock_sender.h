//===========================================================================//
// 
// Purpose: server side data block sender
// 
//===========================================================================//
#ifndef DATABLOCK_SENDER_H
#define DATABLOCK_SENDER_H
#include "engine/shared/datablock.h"

class CClient;

class ServerDataBlockSender : public NetDataBlockSender
{
public:
	virtual void SendFragment(const short blockSeqNr, const int transferSize, const short cancelSeqNr,
		const short fragId, const void* const fragData, const int fragSize) override;

	virtual int GetTransmissionRate() const override;
	virtual const char* GetReceiverName() const override;

private:
	inline CClient* GetClient() { Assert(m_pUserData); return reinterpret_cast<CClient*>(m_pUserData); };
	inline const CClient* GetClient() const { Assert(m_pUserData); return reinterpret_cast<const CClient*>(m_pUserData); };
};

inline void* (*ServerDataBlockSender__SendDataBlock)(ServerDataBlockSender* thisptr,
	const short transferId, const int transferSize, const short transferNr,
	const short blockNr, const uint8_t* const blockData, const int blockSize);

///////////////////////////////////////////////////////////////////////////////
class VServerDataBlockSender : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("ServerDataBlockSender::SendDataBlock", ServerDataBlockSender__SendDataBlock);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 55 48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 48 8B 99 ?? ?? ?? ??").GetPtr(ServerDataBlockSender__SendDataBlock);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { }
};
///////////////////////////////////////////////////////////////////////////////

#endif // DATABLOCK_SENDER_H
