//===========================================================================//
// 
// Purpose: server side data block sender
// 
//===========================================================================//
#include "engine/client/client.h"
#include "common/proto_oob.h"
#include "datablock_sender.h"

static ConVar net_compressDataBlockLzAcceleration("net_compressDataBlockLzAcceleration", "1", FCVAR_DEVELOPMENTONLY, "The acceleration value for LZ4 data block compression");

//-----------------------------------------------------------------------------
// Purpose: sends the data block
//-----------------------------------------------------------------------------
void ServerDataBlockSender::SendDataBlock(const short transferId, const int transferSize,
	const short transferNr, const short blockNr, const uint8_t* const blockData, const int blockSize)
{
	const CClient* const cl = m_pClient;

	if (!cl)
	{
		Assert(0, __FUNC_PFX__"Called without a valid client handle!");
		return;
	}

	const CNetChan* const chan = cl->m_NetChannel;

	if (!chan)
	{
		Assert(0, __FUNC_PFX__"Called without a valid net channel!");
		return;
	}

	char dataBuf[DATABLOCK_FRAGMENT_PACKET_SIZE];
	bf_write buf(&dataBuf, sizeof(dataBuf));

	// msg data (gets processed on client's out of band packet handler)
	buf.WriteLong(CONNECTIONLESS_HEADER);
	buf.WriteByte(S2C_DATABLOCK_FRAGMENT);

	// transfer info
	buf.WriteShort(blockSeqNr);
	buf.WriteLong(transferSize);
	buf.WriteShort(cancelSeqNr);

	// block info
	buf.WriteShort(fragId);
	buf.WriteLong(fragSize);

	// block data
	buf.WriteBytes(blockData, blockSize);

	// send the data block packet
	v_NET_SendPacket(NULL, 
		chan->GetSocket(), 
		chan->GetRemoteAddress(),
		buf.GetData(),
		buf.GetNumBytesWritten(), 
		NULL, false, NULL, true);
}

//-----------------------------------------------------------------------------
// Purpose: gets the resend rate
//-----------------------------------------------------------------------------
int ServerDataBlockSender::GetTransmissionRate() const
{
	if (!m_pClient)
		return net_datablock_fastRate->GetInt();

	const CNetChan* const chan = m_pClient->GetNetChan();

	if (!chan)
		return net_datablock_fastRate->GetInt();

	if (m_slowMode)
		return net_datablock_slowRate->GetInt();

	const float networkLoss = chan->GetTotalNetworkLoss();

	if (networkLoss >= net_datablock_networkLossForSlowSpeed->GetFloat())
		return net_datablock_slowRate->GetInt();

	if (m_flResendRate > net_datablock_resendRateForSlowSpeed->GetFloat())
		return net_datablock_slowRate->GetInt();

	return net_datablock_fastRate->GetInt();
}

//-----------------------------------------------------------------------------
// Purpose: gets the receiver name (client name as registered on the server)
//-----------------------------------------------------------------------------
const char* ServerDataBlockSender::GetReceiverName() const
{
    return m_pClient->m_szServerName;
}

//-----------------------------------------------------------------------------
// Purpose: write the whole data in the data block scratch buffer
//-----------------------------------------------------------------------------
void ServerDataBlockSender::WriteDataBlock(const uint8_t* const sourceData, const int dataSize,
	const bool isMultiplayer, const char* const debugName)
{
	AUTO_LOCK(m_Lock);

	ServerDataBlockHeader_s* const pHeader = reinterpret_cast<ServerDataBlockHeader_s*>(m_pScratchBuffer);
	bool copyRaw = true;

	int actualDataSize = dataSize;

	if (net_compressDataBlock->GetBool())
	{
		const int encodedSize = LZ4_compress_fast((const char*)sourceData, (char*)m_scratchBuffer + sizeof(ServerDataBlockHeader_s),
			dataSize, MAX_DATABLOCK_TRANSFER_SIZE, net_compressDataBlockLzAcceleration.GetInt());

		// this shouldn't happen at all
		if (!encodedSize)
		{
			Assert(0);
			Error(eDLL_T::SERVER, 0, "LZ4 error compressing data block for client.\n");
		}

		// make sure the encoded data is smaller than the raw data, in some cases
		// this might turn larger which means we should just send raw data
		else if (encodedSize < dataSize)
		{
			actualDataSize = encodedSize;

			pHeader->isCompressed = true;
			copyRaw = false;
		}
	}

	// in case no compression was performed, we send the raw data
	if (copyRaw)
	{
		// this should equal the dataSize at this point, even if compression failed
		Assert(actualDataSize == dataSize);

		pHeader->isCompressed = false;
		memcpy(m_pScratchBuffer + sizeof(ServerDataBlockHeader_s), sourceData, actualDataSize);
	}

	// NOTE: we copy data in the scratch buffer with an offset of
	// sizeof(ServerDataBlockHeader_s), the header gets send up as well so we
	// have to take this into account !!!
	StartBlockSender(actualDataSize + sizeof(ServerDataBlockHeader_s), isMultiplayer, debugName);
}

//-----------------------------------------------------------------------------
// Purpose: send any pending data block fragments
//-----------------------------------------------------------------------------
bool ServerDataBlockSender::SendPendingFragments(const double currTime)
{
	const float deltaTime = static_cast<float>(currTime - m_timeCurrentTransfer);
	m_timeCurrentTransfer = currTime;

	if (deltaTime < 0.0f)
		return false;

	if (m_transferInProgress)
	{
		// If we haven't received an ack for too long, fall back to the
		// slower transmission rate.
		if (!m_slowMode && currTime > (m_timeLastAck + DATABLOCK_SLOW_MODE_ACK_TIMEOUT))
		{
			m_slowMode = true;
			m_transferBudget = 0;
		}

		// Accumulate the amount of data we're allowed to send this frame
		const float transferRate = static_cast<float>(GetTransmissionRate());
		m_transferBudget += static_cast<int>(transferRate * deltaTime);

		AUTO_LOCK(m_Lock);
		const int nextFrag = m_lastTransferedFragmentId + 1;

		Assert(m_totalFragments < MAX_DATABLOCK_FRAGMENTS);

		for (int i = 0; i < m_totalFragments; ++i)
		{
			const short fragId = static_cast<short>((i + nextFrag) % m_totalFragments);

			// Don't send more data until enough bandwidth has accumulated
			if (m_rateLimited && m_transferBudget < MAX_DATABLOCK_FRAGMENT_SIZE)
				break;

			if (m_fragmentsAcked[fragId])
				continue; // Already ack'd.

			// Don't resend this fragment until the minimum resend interval
			// has elapsed.
			if (m_timeCurrentTransfer < m_lastFragmentTransferTimes[fragId] + net_datablock_minResendInterval->GetFloat())
				continue;

			const int fragOffset = fragId * MAX_DATABLOCK_FRAGMENT_SIZE;
			int fragSize = MAX_DATABLOCK_FRAGMENT_SIZE;

			// Last fragment can be smaller than the maximum fragment size
			if (fragOffset + MAX_DATABLOCK_FRAGMENT_SIZE > m_nTransferSize)
				fragSize = m_nTransferSize - fragOffset;

			++m_transferedFragments;
			m_lastFragmentTransferTimes[fragId] = m_timeCurrentTransfer;

			SendDataBlock(m_blockSeqNr, m_nTransferSize, m_cancelSeqNr, fragId, &m_scratchBuffer[fragOffset], fragSize);

			m_transferBudget -= fragSize;
			m_lastTransferedFragmentId = fragId;
		}
	}

	if (net_debugDataBlockSender->GetBool() || m_slowMode
		|| m_timeCurrentTransfer - m_timeStartTransfer > net_datablock_longSendTime->GetFloat()
		|| m_blockSeqNr < DATABLOCK_INITIAL_TRANSFER_WINDOW_DBG)
	{
		m_abnormalTransfer = true;
		return true;
	}

	m_abnormalTransfer = false;
	return false;
}
