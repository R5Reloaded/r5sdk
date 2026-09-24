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
void ClientDataBlockReceiver::AcknowledgeTransmission()
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

	for (int i = m_nTotalBlocks; (i--) > 0;)
	{
		if (m_BlockStatus[i])
		{
			// ack the last blockNr we recv'd and processed
			buf.WriteShort(i);
			break;
		}
	}

	// send the data block ack packet
	v_NET_SendPacket(NULL,
		chan->GetSocket(),
		chan->GetRemoteAddress(),
		buf.GetData(),
		buf.GetNumBytesWritten(),
		NULL, false, NULL, true);

	m_transferAckPending = false;
}

void NetDataBlockReceiver_OnDecompressError()
{
	Assert(0);

	COM_ExplainDisconnection(true, "LZ4 error decompressing data block from server.\n");
	v_Host_Disconnect(true);
}

void VClientDataBlockReceiver::Detour(const bool /*bAttach*/) const
{
}
