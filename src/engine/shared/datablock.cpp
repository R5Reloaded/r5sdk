//===========================================================================//
// 
// Purpose: data block sender & receiver
// 
//===========================================================================//
#include "datablock.h"

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
NetDataBlockSender::~NetDataBlockSender()
{
	ResetBlockSender();

	delete[] m_pScratchBuffer;
	m_pScratchBuffer = nullptr;

	m_pClient = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: reset the data block sender context
//-----------------------------------------------------------------------------
void NetDataBlockSender::ResetBlockSender(void)
{
	m_bInitialized = false;
	m_bStartedTransfer = false;

	m_nTransferId = 0;
	m_nTransferSize = 0;
	m_nTotalBlocks = 0;
	m_nBlockAckTick = 0;

	m_TimeCurrentSend = 0.0;
	m_TimeFirstSend = 0.0;

	m_nTotalSizeRemaining = 0;

	m_TimeLastSend = 0.0;
	m_szDebugName[0] = '\0';
	m_abnormalTransfer = false;
	m_lastTransferedFragmentId = DATABLOCK_INVALID_FRAGMENT_NR;
	m_transferedFragments = 0;

	memset(m_bBlockAckStatus, 0, sizeof(m_bBlockAckStatus));
	memset(m_flBlockSendTimes, 0, sizeof(m_flBlockSendTimes));
}

static int NetDataBlock_CalcFragCount(const int transferSize)
{
	Assert(transferSize > 0 && transferSize <= MAX_DATABLOCK_TRANSFER_SIZE);
	return transferSize / MAX_DATABLOCK_FRAGMENT_SIZE + (transferSize % MAX_DATABLOCK_FRAGMENT_SIZE != 0);
}

//-----------------------------------------------------------------------------
// Purpose: initialize the data block sender context
//-----------------------------------------------------------------------------
void NetDataBlockSender::StartBlockSender(const int transferSize, const bool isMultiplayer, const char* const debugName)
{
	m_bMultiplayer = isMultiplayer;
	m_nBlockAckTick = 0;
	m_nTransferSize = transferSize;

	m_totalFragments = NetDataBlock_CalcFragCount(m_nTransferSize);

	strncpy(m_szDebugName, debugName, sizeof(m_szDebugName));
	m_szDebugName[sizeof(m_szDebugName) - 1] = '\0';

	// null status memory
	memset(m_bBlockAckStatus, 0, sizeof(m_bBlockAckStatus));
	memset(m_flBlockSendTimes, 0, sizeof(m_flBlockSendTimes));

	m_bInitialized = true;
	m_bStartedTransfer = false;

	const double currentTime = Plat_FloatTime();

	m_TimeLastSend = currentTime;
	m_TimeCurrentSend = currentTime;
	m_TimeFirstSend = currentTime;

	m_transferBudget = 4096;
	m_transferedFragments = 0;
}

static ConVar net_compressDataBlockLzAcceleration("net_compressDataBlockLzAcceleration", "1", FCVAR_DEVELOPMENTONLY, "The acceleration value for LZ4 data block compression");

//-----------------------------------------------------------------------------
// Purpose: write the whole data in the data block scratch buffer
//-----------------------------------------------------------------------------
void NetDataBlockSender::WriteDataBlock(const uint8_t* const sourceData, const int dataSize,
	const bool doRateLimit, const char* const debugName)
{
	AUTO_LOCK(m_Lock);

	Header_s* const pHeader = reinterpret_cast<Header_s*>(m_scratchBuffer);
	bool copyRaw = true;

	int actualDataSize = dataSize;

	if (net_compressDataBlock->GetBool())
	{
		const int encodedSize = LZ4_compress_fast((const char*)sourceData, (char*)m_scratchBuffer + sizeof(Header_s),
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
		memcpy(m_scratchBuffer + sizeof(Header_s), sourceData, actualDataSize);
	}

	// NOTE: we copy data in the scratch buffer with an offset of
	// sizeof(Header_s), the header gets send up as well so we
	// have to take this into account !!!
	StartBlockSender(actualDataSize + sizeof(Header_s), doRateLimit, debugName);
}

//-----------------------------------------------------------------------------
// Purpose: send any pending data block fragments
//-----------------------------------------------------------------------------
bool NetDataBlockSender::SendPendingFragments(const double currTime)
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

//-----------------------------------------------------------------------------
// Purpose: initialize the data block receiver context
//-----------------------------------------------------------------------------
bool NetDataBlockReceiver::StartBlockReceiver(const int transferSize, const double startTime)
{
	if (!IsTransferSizeValid(transferSize))
		return false;

	m_copyingFragments = true;
	m_nTransferSize = transferSize;
	m_totalFragments = NetDataBlock_CalcFragCount(transferSize);
	m_copiedFragmentsCount = 0;
	m_timeStartTransfer = startTime;

	memset(m_copiedFragments, 0, sizeof(m_copiedFragments));
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: reset the data block receiver context
//-----------------------------------------------------------------------------
void NetDataBlockReceiver::ResetBlockReceiver(const short transferNr)
{
	m_nTransferNr = transferNr;

	m_copyingFragments = false;
	m_transferFinished = false;

	m_transferAckPending = false;

	m_blockSeqNr = 0;
	m_totalFragments = 0;
	m_nTransferSize = 0;
	m_copiedFragmentsCount = 0;

	m_timeStartTransfer = 0.0;
	memset(m_copiedFragments, 0, sizeof(m_copiedFragments));
}

//-----------------------------------------------------------------------------
// Purpose: gets information for the provided fragment ID
//-----------------------------------------------------------------------------
bool NetDataBlockReceiver::GetFragmentInfo(const short fragId, FragInfo_s& info)
{
	if (!IsFragmentIdValid(fragId))
		return false;

	info.scratchOffset = fragId * MAX_DATABLOCK_FRAGMENT_SIZE;
	const int bytesRemaining = m_nTransferSize - info.scratchOffset;

	if (bytesRemaining <= 0)
		return false;

	info.expectedSize = Min(bytesRemaining, MAX_DATABLOCK_FRAGMENT_SIZE);
	info.SetStatusHandle(&m_copiedFragments[fragId]);

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: check whether the transfer size can be accepted by the system
//-----------------------------------------------------------------------------
bool NetDataBlockReceiver::IsTransferSizeValid(const int transferSize) const
{
	if (transferSize <= 0)
		return false;

	if (transferSize > MAX_DATABLOCK_TRANSFER_SIZE)
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: check whether the fragment ID can be accepted by the system
//-----------------------------------------------------------------------------
bool NetDataBlockReceiver::IsFragmentIdValid(const short fragmentId) const
{
	if (fragmentId < 0)
		return false;

	if (fragmentId >= m_totalFragments)
		return false;

	return true;
}
