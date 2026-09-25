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
	const CClientState* const cl = GetLocalClient();

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

	char dataBuf[DATABLOCK_FRAGMENT_PACKET_SIZE];
	bf_write buf(&dataBuf, sizeof(dataBuf));

	buf.WriteLong(CONNECTIONLESS_HEADER);
	buf.WriteByte(C2S_DATABLOCK_ACK);

	buf.WriteWord(m_blockSeqNr);
	buf.WriteWord(m_cancelSeqNr);

	// Fragment ACK bitmap follows.
	buf.WriteOneBit(1);

	for (int fragId = 0; fragId < receiver->m_totalFragments; ++fragId)
		buf.WriteOneBit(receiver->m_copiedFragments[fragId]);

	// send the data block ack packet
	v_NET_SendPacket(NULL,
		chan->GetSocket(),
		chan->GetRemoteAddress(),
		buf.GetData(),
		buf.GetNumBytesWritten(),
		NULL, false, NULL, true);
}

//-----------------------------------------------------------------------------
// NOTE: detoured for 5 reasons:
// 1: when a corrupt or malformed compress packet is sent, the code never freed
//    the temporary copy buffer it made to decode the data into the scratch buf
// 2: to guard against negative or overflowing fragment ID's, transfer sizes
//    and scratch buffer offsets
// 3: to make sure we only acknowledge successfull recv's AFTER LZ4 decoding!
// 4: to return out early when we receive a new cancel request
// 5: exploring other compression algorithms for potential optimizations
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
	DetourSetup(&NetDataBlockReceiver__ProcessFragment, NetDataBlockReceiver_ProcessFragment, bAttach);
}
