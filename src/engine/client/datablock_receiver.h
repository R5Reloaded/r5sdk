//===========================================================================//
// 
// Purpose: client side data block receiver
// 
//===========================================================================//
#ifndef DATABLOCK_RECEIVER_H
#define DATABLOCK_RECEIVER_H
#include "engine/shared/datablock.h"

class CClientState;

class ClientDataBlockReceiver : public NetDataBlockReceiver
{
public:
	virtual void SendFragmentAcks() override;

private:
	inline CClientState* GetLocalClient() { Assert(m_pUserData); return reinterpret_cast<CClientState*>(m_pUserData); };
	inline const CClientState* GetLocalClient() const { Assert(m_pUserData); return reinterpret_cast<const CClientState*>(m_pUserData); };
};

// virtual methods
inline void*(*ClientDataBlockReceiver__SendFragmentAcks)(ClientDataBlockReceiver* thisptr);

// non-virtual methods
inline bool (*NetDataBlockReceiver__ProcessFragment)(NetDataBlockReceiver* thisptr, const double startTime,
	const unsigned short blockSeqNr, const int transferSize, const unsigned short cancelSeqNr,
	const unsigned short fragId, const void* const fragData, const int fragSize);

///////////////////////////////////////////////////////////////////////////////
class VClientDataBlockReceiver : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("NetDataBlockReceiver::ProcessFragment", NetDataBlockReceiver__ProcessFragment);
		LogFunAdr("ClientDataBlockReceiver::SendFragmentAcks", ClientDataBlockReceiver__SendFragmentAcks);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 41 56 48 83 EC 30 0F B7 44 24 ??").GetPtr(NetDataBlockReceiver__ProcessFragment);
		Module_FindPattern(g_GameDll, "40 53 48 81 EC ?? ?? ?? ?? 4C 8B 51 08").GetPtr(ClientDataBlockReceiver__SendFragmentAcks);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // DATABLOCK_RECEIVER_H
