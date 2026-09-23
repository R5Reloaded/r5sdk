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
	m_bAbnormalSend = false;
	m_nCurrentBlock = DATABLOCK_INVALID_BLOCK_NR;
	m_nBlockSendsAttempted = 0;

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

	m_nTotalSizeRemaining = 4096;
	m_nBlockSendsAttempted = 0;
}

//-----------------------------------------------------------------------------
// Purpose: initialize the data block receiver context
//-----------------------------------------------------------------------------
void NetDataBlockReceiver::StartBlockReceiver(const int transferSize, const double startTime)
{
	m_bStartedRecv = true;
	m_nTransferSize = transferSize;
	m_totalFragments = NetDataBlock_CalcFragCount(transferSize);
	m_copiedFragmentsCount = 0;
	m_timeStartTransfer = startTime;

	memset(m_BlockStatus, 0, sizeof(m_BlockStatus));
}

//-----------------------------------------------------------------------------
// Purpose: reset the data block receiver context
//-----------------------------------------------------------------------------
void NetDataBlockReceiver::ResetBlockReceiver(const short transferNr)
{
	m_nTransferNr = transferNr;

	m_copyingFragments = false;
	m_transferInProgress = false;

	m_TransferId = 0;
	m_nTotalBlocks = 0;
	m_nBlockAckTick = 0;
	m_flStartTime = 0.0;

	memset(m_BlockStatus, 0, sizeof(m_BlockStatus));
}
