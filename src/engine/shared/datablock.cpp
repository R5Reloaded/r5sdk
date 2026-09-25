//===========================================================================//
// 
// Purpose: data block sender & receiver
// 
//===========================================================================//
#include "datablock.h"
#include "datablock_events.h"

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
NetDataBlockSender::NetDataBlockSender()
{
	m_scratchBuffer = new u8[MAX_DATABLOCK_TRANSFER_SIZE + sizeof(Header_s)];
	m_pUserData = nullptr;
	m_flResendRate = 0.0;
	ResetState();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
NetDataBlockSender::~NetDataBlockSender()
{
	ResetState();

	delete[] m_pScratchBuffer;
	m_pScratchBuffer = nullptr;

	m_pUserData = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: reset the data block sender context
//-----------------------------------------------------------------------------
void NetDataBlockSender::ResetState(void)
{
	m_bInitialized = false;
	m_bStartedTransfer = false;

	m_blockSeqNr = 0;
	m_cancelSeqNr = 0;

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

//-----------------------------------------------------------------------------
// Purpose: cancel the data block transfer
//-----------------------------------------------------------------------------
void NetDataBlockSender::CancelTransfer()
{
	if (!m_transferInProgress)
		return;

	const unsigned short nextCancelSeqNr = m_cancelSeqNr + 1;

	ResetState();
	m_cancelSeqNr = nextCancelSeqNr;
}

static int NetDataBlock_CalcFragCount(const int transferSize)
{
	Assert(transferSize > 0 && transferSize <= MAX_DATABLOCK_TRANSFER_SIZE);
	return transferSize / MAX_DATABLOCK_FRAGMENT_SIZE + (transferSize % MAX_DATABLOCK_FRAGMENT_SIZE != 0);
}

//-----------------------------------------------------------------------------
// Purpose: initialize the data block sender context
//-----------------------------------------------------------------------------
void NetDataBlockSender::StartBlockSender(const int transferSize, const bool doRateLimit, 
	const char* const debugName, const int transferBudget)
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

	m_transferBudget = transferBudget;
	m_transferedFragments = 0;
}

static ConVar net_compressDataBlockLzAcceleration("net_compressDataBlockLzAcceleration", "1", FCVAR_DEVELOPMENTONLY, "The acceleration value for LZ4 data block compression");

//-----------------------------------------------------------------------------
// Purpose: write the whole data in the data block scratch buffer
//-----------------------------------------------------------------------------
void NetDataBlockSender::WriteDataBlock(const uint8_t* const sourceData, const int dataSize,
	const bool doRateLimit, const char* const debugName)
{
	if (dataSize < MIN_DATABLOCK_PAYLOAD_SIZE ||
		dataSize > MAX_DATABLOCK_PAYLOAD_SIZE)
	{
		Assert(0, __FUNC_PFX__"Invalid data block payload size!");
		return;
	}

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
			NetDataBlockSender_OnCompressError();

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
	StartBlockSender(actualDataSize + sizeof(Header_s), doRateLimit,
		debugName, DATABLOCK_DEFAULT_TRANSFER_BUDGET);
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

		Assert(m_totalFragments <= MAX_DATABLOCK_FRAGMENTS);

		for (int i = 0; i < m_totalFragments; ++i)
		{
			const unsigned short fragId = static_cast<unsigned short>((i + nextFrag) % m_totalFragments);

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

			SendFragment(m_blockSeqNr, m_nTransferSize, m_cancelSeqNr, fragId, &m_scratchBuffer[fragOffset], fragSize);

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
// Purpose: process fragment acknowledgements from the receiver
//-----------------------------------------------------------------------------
void NetDataBlockSender::ProcessFragmentAcks(FragAck_s& ackMsg)
{
	if (!m_transferInProgress)
		return;

	if (ackMsg.cancelSeqNr != m_cancelSeqNr)
		return;

	// Receiver is still responding.
	m_timeLastAck = m_timeCurrentTransfer;

	if (m_slowMode)
		m_slowMode = false;

	Assert(m_totalFragments > 0);
	Assert(m_totalFragments <= MAX_DATABLOCK_FRAGMENTS);

	// Receiver already advanced, so the current block is fully ack'd.
	if (ackMsg.blockSeqNr == m_blockSeqNr + 1)
	{
		const int ackBytes = (m_totalFragments + 7) >> 3;
		memset(ackMsg.ackedFragments, FragAck_s::FRAG_ACKED, ackBytes);
	}
	else if (ackMsg.blockSeqNr != m_blockSeqNr)
		return;

	for (int fragId = 0; fragId < m_totalFragments; ++fragId)
	{
		const u64 ackBits = ackMsg.ackedFragments[fragId >> 6];
		const u64 ackMask = 1ull << (fragId & 63);

		if (!(ackBits & ackMask))
			continue;

		if (m_fragmentsAcked[fragId])
			continue;

		++m_ackedFragmentsCount;
		m_fragmentsAcked[fragId] = true;
	}

	Assert(m_ackedFragmentsCount >= 0);
	Assert(m_ackedFragmentsCount <= m_totalFragments);

	if (m_ackedFragmentsCount != m_totalFragments)
		return;

	++m_blockSeqNr;
	m_transferInProgress = false;

	m_flResendRate = static_cast<float>(m_transferedFragments) / static_cast<float>(m_totalFragments);
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
NetDataBlockReceiver::NetDataBlockReceiver()
{
	m_scratchBuffer = new u8[MAX_DATABLOCK_TRANSFER_SIZE + sizeof(Header_s)];
	m_pUserData = nullptr;
	ResetState();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
NetDataBlockReceiver::~NetDataBlockReceiver()
{
	ResetState();

	delete[] m_scratchBuffer;
	m_scratchBuffer = nullptr;

	m_pUserData = nullptr;
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

	ClearCopyStatArray();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: reset the data block receiver context
//-----------------------------------------------------------------------------
void NetDataBlockReceiver::ResetState()
{
	m_copyingFragments = false;
	m_transferFinished = false;

	m_blockSeqNr = 0;
	m_cancelSeqNr = 0;

	m_transferAckPending = false;
	m_nTransferSize = 0;
	m_totalFragments = 0;

	m_copiedFragmentsCount = 0;
	m_timeStartTransfer = 0.0;

	ClearCopyStatArray();
}

//-----------------------------------------------------------------------------
// Purpose: cancel the data block transfer
//-----------------------------------------------------------------------------
void NetDataBlockReceiver::CancelTransfer(const unsigned short cancelSeqNr)
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
	ClearCopyStatArray();
}

//-----------------------------------------------------------------------------
// Purpose: gets information for the provided fragment ID
//-----------------------------------------------------------------------------
bool NetDataBlockReceiver::GetFragmentInfo(const unsigned short fragId, FragInfo_s& info)
{
	if (!IsFragmentIdValid(fragId))
		return false;

	info.scratchOffset = fragId * MAX_DATABLOCK_FRAGMENT_SIZE;
	const int bytesRemaining = m_nTransferSize - info.scratchOffset;

	if (bytesRemaining <= 0)
	{
		Assert(0); // Bad bug.
		return false;
	}

	info.expectedSize = Min(bytesRemaining, MAX_DATABLOCK_FRAGMENT_SIZE);
	info.SetStatusHandle(&m_copiedFragments[fragId]);

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: check whether the transfer size can be accepted by the system
//-----------------------------------------------------------------------------
bool NetDataBlockReceiver::IsTransferSizeValid(const int transferSize) const
{
	if (transferSize < MIN_DATABLOCK_TRANSFER_SIZE)
		return false;

	if (transferSize > MAX_DATABLOCK_TRANSFER_SIZE)
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: check whether the fragment ID can be accepted by the system
//-----------------------------------------------------------------------------
bool NetDataBlockReceiver::IsFragmentIdValid(const unsigned short fragId) const
{
	Assert(m_totalFragments <= MAX_DATABLOCK_FRAGMENTS);

	if (fragId >= m_totalFragments)
		return false;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: process the recv'd fragment and reconstruct the data block
//-----------------------------------------------------------------------------
bool NetDataBlockReceiver::ProcessFragment(const double currTime, const unsigned short blockSeqNr, const int transferSize,
	const unsigned short cancelSeqNr, const unsigned short fragId, const void* const fragData, const int fragSize)
{
	if (!fragData)
	{
		Assert(0, __FUNC_PFX__"Null fragment buffer!");
		return false;
	}

	// should we cancel the transfer?
	if (cancelSeqNr != m_cancelSeqNr)
	{
		CancelTransfer(cancelSeqNr);
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

	// only copy if we haven't already (sender can send duplicates!)
	if (!frag.IsCopied())
	{
		constexpr int dataOffset = sizeof(Header_s) - 1;
		memcpy(&m_scratchBuffer[frag.scratchOffset + dataOffset], fragData, frag.expectedSize);

		++m_copiedFragmentsCount;
		frag.MarkCopied();
	}

	if (m_copiedFragmentsCount != m_totalFragments)
		return true; // Not complete yet.

	SendFragmentAcks();

	m_transferAckPending = false;
	m_transferFinished = true;

	const Header_s* const pHeader = reinterpret_cast<Header_s*>(m_scratchBuffer);

	if (pHeader->isCompressed)
	{
		// NOTE: the engine's implementation of this function does NOT free
		// this buffer when a malformed/corrupt LZ4 packet is sent to the
		// receiver; wrapped buffer in unique_ptr to make sure it never leaks!
		std::unique_ptr<unsigned char[]> encodedDataBuf(new unsigned char[MAX_DATABLOCK_TRANSFER_SIZE]);

		unsigned char* const pEncodedDataBuf = encodedDataBuf.get();
		unsigned char* const dataLocation = m_scratchBuffer + sizeof(Header_s);

		// copy the encoded data in the newly allocated buffer so we can decode back
		// into the data block buffer we copied the encoded data from
		const int compressedSize = m_nTransferSize - DATABLOCK_HEADER_SIZE;

		memcpy(pEncodedDataBuf, dataLocation, compressedSize);
		const int numDecode = LZ4_decompress_safe((const char*)pEncodedDataBuf, (char*)dataLocation, compressedSize, MAX_DATABLOCK_TRANSFER_SIZE);

		if (numDecode < 0)
		{
			NetDataBlockReceiver_OnDecompressError();
			return false;
		}

		m_nTransferSize = numDecode;
	}
	else
	{
		// truncate the byte that determines whether the data was compressed
		m_nTransferSize -= DATABLOCK_HEADER_SIZE;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: prepare state for next transfer
//-----------------------------------------------------------------------------
void NetDataBlockReceiver::NextTransfer()
{
	if (net_debugDataBlockReceiver->GetBool())
		LogState();

	++m_blockSeqNr;
	m_copyingFragments = false;
	m_transferFinished = false;
	m_totalFragments = 0;

	m_copiedFragmentsCount = 0;
	m_timeStartTransfer = 0.0;

	ClearCopyStatArray();
}
