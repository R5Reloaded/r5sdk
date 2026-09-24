//===========================================================================//
// 
// Purpose: client side data block receiver
// 
//===========================================================================//
#include "engine/client/clientstate.h"
#include "datablock_receiver.h"
#include "common/proto_oob.h"
#include "engine/common.h"
#include "engine/host_cmd.h"

//-----------------------------------------------------------------------------
// Purpose: send an ack back to the server to let them know
// we received the data block
//-----------------------------------------------------------------------------
void ClientDataBlockReceiver::AcknowledgeTransmission()
{
	const CClientState* const cl = m_pClientState;

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

	buf.WriteShort(m_TransferId);
	buf.WriteShort(m_nTransferNr);

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

//-----------------------------------------------------------------------------
// Purpose: process the recv'd data block and reconstruct the fragmented data
//-----------------------------------------------------------------------------
bool ClientDataBlockReceiver::ProcessFragment(const double currTime, const short blockSeqNr, const int transferSize,
	const short cancelSeqNr, const short fragId, const void* const fragData, const int fragSize)
{
	if (!fragData)
	{
		Assert(0, __FUNC_PFX__"Null fragment buffer!");
		return false;
	}

	// should we cancel the transfer?
	if (cancelSeqNr != m_cancelSeqNr)
	{
		ResetBlockReceiver(cancelSeqNr);
		return false;
	}

	m_transferAckPending = true;

	// make sure we always receive fragments in order
	if (blockSeqNr != m_blockSeqNr)
	{
		Assert(0, __FUNC_PFX__"Block sequence number changed!");
		return false;
	}

	if (m_transferFinished)
		return false; // Drop duplicates that may arive late.

	// initialize the receiver if this is the first fragment
	if (!m_copyingFragments)
	{
		if (!StartBlockReceiver(transferSize, currTime))
		{
			Assert(0, __FUNC_PFX__"this->StartBlockReceiver() failed!");
			return false; // moGus transferSize, nice try!
		}
	}
	else if (transferSize != m_nTransferSize)
	{
		// This gets resend each time. There is no reason for it, but
		// rewriting that code will be a very big undertaking (needs
		// the connectionless handler to be rebuild!). So just use it
		// as a validator here. If it mismatches something is broken.
		Assert(0, __FUNC_PFX__"Transfer size changed!");
		return false;
	}

	FragInfo_s frag;

	if (!GetFragmentInfo(fragId, frag))
	{
		Assert(0, __FUNC_PFX__"this->GetFragmentInfo() failed!");
		return false; // moGus fragId, nice try!
	}

	// Gaps, overflow or negative value?
	if (fragSize != frag.expectedSize)
	{
		Assert(0, __FUNC_PFX__"Fragment stride desync!");
		return false;
	}

	// only cpoy if we haven't already (sender can send duplicates!)
	if (!frag.IsCopied())
	{
		constexpr int dataOffset = sizeof(ClientDataBlockHeader_s) - 1;
		memcpy(&m_scratchBuffer[frag.scratchOffset + dataOffset], fragData, frag.expectedSize);

		++m_copiedFragmentsCount;
		frag.MarkCopied();
	}

	if (m_copiedFragmentsCount != m_totalFragments)
		return true; // Not complete yet.

	const ClientDataBlockHeader_s* const pHeader = reinterpret_cast<ClientDataBlockHeader_s*>(m_scratchBuffer);

	if (pHeader->isCompressed)
	{
		// NOTE: the engine's implementation of this function does NOT free
		// this buffer when a malformed/corrupt LZ4 packet is sent to the
		// receiver; wrapped buffer in unique_ptr to make sure it never leaks!
		std::unique_ptr<unsigned char[]> encodedDataBuf(new unsigned char[MAX_DATABLOCK_TRANSFER_SIZE]);

		char* const pEncodedDataBuf = encodedDataBuf.get();
		char* const dataLocation = m_pScratchBuffer + sizeof(ClientDataBlockHeader_s);

		// copy the encoded data in the newly allocated buffer so we can decode back
		// into the data block buffer we copied the encoded data from
		const int compressedSize = m_nTransferSize -1;

		memcpy(pEncodedDataBuf, dataLocation, compressedSize);
		const int numDecode = LZ4_decompress_safe((const char*)pEncodedDataBuf, (char*)dataLocation, compressedSize, MAX_DATABLOCK_TRANSFER_SIZE);

		if (numDecode < 0)
		{
			Assert(0);

			COM_ExplainDisconnection(true, "LZ4 error decompressing data block from server.\n");
			v_Host_Disconnect(true);

			return false;
		}

		m_nTransferSize = numDecode;
	}
	else
	{
		// truncate the byte that determines whether the data was compressed
		m_nTransferSize--;
	}

	AcknowledgeTransmission();
	m_transferFinished = true;

	return true;
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
static bool HK_ProcessFragment(ClientDataBlockReceiver* receiver, const double startTime,
	const short blockSeqNr, const int transferSize, const short cancelSeqNr,
	const short fragId, const void* const fragData, const int fragSize)
{
	return receiver->ProcessFragment(startTime, blockSeqNr, transferSize, cancelSeqNr, 
		fragId, fragData, fragSize);
}

void VClientDataBlockReceiver::Detour(const bool bAttach) const
{
	DetourSetup(&ClientDataBlockReceiver__ProcessFragment, HK_ProcessFragment, bAttach);
}
