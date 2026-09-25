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
	virtual void SendFragment(const unsigned short blockSeqNr, const int transferSize, const unsigned short cancelSeqNr,
		const unsigned short fragId, const void* const fragData, const int fragSize) override;

	virtual int GetTransmissionRate() const override;
	virtual const char* GetReceiverName() const override;

protected:
	friend class VServerDataBlockSender; // Statics needed for VFTable hook!
	static void SendFragmentGuts(ServerDataBlockSender* const sender, const unsigned short blockSeqNr, const int transferSize,
		const unsigned short cancelSeqNr, const unsigned short fragId, const void* const fragData, const int fragSize);
	static int GetTransmissionRateGuts(const ServerDataBlockSender* const sender);

private:
	inline CClient* GetClient() { Assert(m_pUserData); return reinterpret_cast<CClient*>(m_pUserData); };
	inline const CClient* GetClient() const { Assert(m_pUserData); return reinterpret_cast<const CClient*>(m_pUserData); };
};

inline bool (*NetDataBlockSender__SendPendingFragments)(NetDataBlockSender* sender, const double currTime);
inline void (*NetDataBlockSender__SendFragment)(ServerDataBlockSender* thisptr,
	const short transferId, const int transferSize, const short transferNr,
	const short blockNr, const uint8_t* const blockData, const int blockSize);
inline void (*NetDataBlockSender__CancelTransfer)(NetDataBlockSender* sender);

inline void* g_pServerDataBlockSender_VFTable;

///////////////////////////////////////////////////////////////////////////////
class VServerDataBlockSender : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("ServerDataBlockSender::SendPendingFragments", NetDataBlockSender__SendPendingFragments);
		LogFunAdr("ServerDataBlockSender::SendFragment", NetDataBlockSender__SendFragment);
		LogFunAdr("ServerDataBlockSender::CancelTransfer", NetDataBlockSender__CancelTransfer);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 8B C4 53 48 83 EC ? 0F 28 C1").GetPtr(NetDataBlockSender__SendPendingFragments);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 55 48 8D AC 24 ?? ?? ?? ?? 48 81 EC ?? ?? ?? ?? 48 8B 99 ?? ?? ?? ??").GetPtr(NetDataBlockSender__SendFragment);
		Module_FindPattern(g_GameDll, "40 57 48 83 EC ? 80 B9 ? ? ? ? ? 48 8B F9 0F 84 ? ? ? ? 33 C0").GetPtr(NetDataBlockSender__CancelTransfer);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const
	{
		g_GameDll.GetVirtualMethodTable(".?AVServerDataBlockSender@@").GetPtr(g_pServerDataBlockSender_VFTable);
	}
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // DATABLOCK_SENDER_H
