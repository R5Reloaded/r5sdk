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
inline void*(*ClientDataBlockReceiver__AcknowledgeTransmission)(ClientDataBlockReceiver* thisptr);

///////////////////////////////////////////////////////////////////////////////
class VClientDataBlockReceiver : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("ClientDataBlockReceiver::AcknowledgeTransmission", ClientDataBlockReceiver__AcknowledgeTransmission);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 48 81 EC ?? ?? ?? ?? 4C 8B 51 08").GetPtr(ClientDataBlockReceiver__AcknowledgeTransmission);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // DATABLOCK_RECEIVER_H
