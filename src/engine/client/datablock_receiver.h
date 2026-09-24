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
	virtual void AcknowledgeTransmission() override;

	bool ProcessFragment(const double currTime, const short blockSeqNr, const int transferSize,
		const short cancelSeqNr, const short fragId, const void* const fragData, const int fragSize);
};

struct ClientDataBlockHeader_s
{
	char reserved[3]; // unused in retail
	bool isCompressed;
};

// virtual methods
inline void*(*ClientDataBlockReceiver__AcknowledgeTransmission)(ClientDataBlockReceiver* thisptr);

// non-virtual methods
inline bool (*ClientDataBlockReceiver__ProcessFragment)(ClientDataBlockReceiver* thisptr, const double startTime,
	const short blockSeqNr, const int transferSize,const short cancelSeqNr, const short fragId,
	const void* const fragData, const int fragSize);

///////////////////////////////////////////////////////////////////////////////
class VClientDataBlockReceiver : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("ClientDataBlockReceiver::AcknowledgeTransmission", ClientDataBlockReceiver__AcknowledgeTransmission);
		LogFunAdr("ClientDataBlockReceiver::ProcessFragment", ClientDataBlockReceiver__ProcessFragment);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 48 81 EC ?? ?? ?? ?? 4C 8B 51 08").GetPtr(ClientDataBlockReceiver__AcknowledgeTransmission);

		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 41 56 48 83 EC 30 0F B7 44 24 ??")
			.GetPtr(ClientDataBlockReceiver__ProcessFragment);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // DATABLOCK_RECEIVER_H
