//===========================================================================//
// 
// Purpose: client side data block receiver
// 
//===========================================================================//
#include "engine/client/clientstate.h"
#include "engine/host_cmd.h"
#include "datablock_receiver.h"
#include "common/proto_oob.h"

//-----------------------------------------------------------------------------
// Purpose: send an ack back to the server to let them know
// we received the data block
//-----------------------------------------------------------------------------
void ClientDataBlockReceiver::SendFragmentAcks()
{
	SendFragmentAcksGuts(this);
}

void ClientDataBlockReceiver::SendFragmentAcksGuts(ClientDataBlockReceiver* const receiver)
{
	const CClientState* const cl = receiver->GetLocalClient();

	if (!cl)
	{
		Assert(0, __FUNC_PFX__"Null client handle!");
		return;
	}

	const CNetChan* const chan = cl->m_NetChannel;

	if (!chan)
	{
		Assert(0, __FUNC_PFX__"Null net channel!");
		return;
	}

	Assert(receiver->m_totalFragments > 0);
	Assert(receiver->m_totalFragments <= MAX_DATABLOCK_FRAGMENTS);

	char dataBuf[DATABLOCK_FRAGMENT_PACKET_SIZE];
	bf_write buf(&dataBuf, sizeof(dataBuf));

	buf.WriteLong(CONNECTIONLESS_HEADER);
	buf.WriteByte(C2S_DATABLOCK_ACK);

	buf.WriteWord(receiver->m_blockSeqNr);
	buf.WriteWord(receiver->m_cancelSeqNr);

	// Fragment ACK bitmap follows.
	buf.WriteOneBit(1);

	for (int fragId = 0; fragId < receiver->m_totalFragments; ++fragId)
		buf.WriteOneBit(receiver->m_copiedFragments[fragId]);

	if (net_debugDataBlockReceiver->GetInt() >= 2)
	{
		Msg(eDLL_T::ENGINE,
			"[DataBlock:Rx] sending ACK for block #%hu: %d/%d fragments copied "
			"(cancel sequence %hu)\n",
			receiver->m_blockSeqNr,
			receiver->m_copiedFragmentsCount,
			receiver->m_totalFragments,
			receiver->m_cancelSeqNr);
	}

	v_NET_SendPacket(NULL,
		chan->GetSocket(),
		chan->GetRemoteAddress(),
		buf.GetData(),
		buf.GetNumBytesWritten(),
		NULL, false, NULL, true);

	receiver->m_transferAckPending = false;
}

//-----------------------------------------------------------------------------
// Purpose: logs the current state to the console
//-----------------------------------------------------------------------------
void ClientDataBlockReceiver::LogState()
{
	LogStateGuts(this);
}

void ClientDataBlockReceiver::LogStateGuts(ClientDataBlockReceiver* const receiver)
{
	const double transferTime = Plat_FloatTime() - receiver->m_timeStartTransfer;

	Msg(eDLL_T::ENGINE,
		"[DataBlock:Rx] completed block #%hu\n"
		"\t- Cancel sequence: %hu\n"
		"\t- Payload size:    %d bytes\n" // Excludes header!
		"\t- Fragments:       %d/%d\n"
		"\t- Compressed:      %s\n"
		"\t- Transfer time:   %.3lf sec\n"
		"*******************************************************************\n",
		receiver->m_blockSeqNr,
		receiver->m_cancelSeqNr,
		receiver->m_nTransferSize,
		receiver->m_copiedFragmentsCount,
		receiver->m_totalFragments,
		receiver->m_usesCompression ? "yes" : "no",
		transferTime);
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
static bool NetDataBlockReceiver_ProcessFragment(NetDataBlockReceiver* receiver, const double startTime,
	const unsigned short blockSeqNr, const int transferSize, const unsigned short cancelSeqNr,
	const unsigned short fragId, const void* const fragData, const int fragSize)
{
	return receiver->ProcessFragment(startTime, blockSeqNr, transferSize, cancelSeqNr,
		fragId, fragData, fragSize);
}

void VClientDataBlockReceiver::Detour(const bool bAttach) const
{
	void* null;
	DetourSetup(&NetDataBlockReceiver__ProcessFragment, NetDataBlockReceiver_ProcessFragment, bAttach);
	CMemory::HookVirtualMethod((uintptr_t)g_pClientDataBlockReceiver_VFTable, ClientDataBlockReceiver::LogStateGuts, 1, &null);
	CMemory::HookVirtualMethod((uintptr_t)g_pClientDataBlockReceiver_VFTable, ClientDataBlockReceiver::SendFragmentAcksGuts, 2, &null);
}
