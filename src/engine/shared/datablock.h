//===========================================================================//
// 
// Purpose: data block sender & receiver
// 
//===========================================================================//
#ifndef IDATABLOCK_H
#define IDATABLOCK_H

#define DATABLOCK_SLOW_MODE_ACK_TIMEOUT 5.0
#define DATABLOCK_DEFAULT_TRANSFER_BUDGET 4096

#define DATABLOCK_DEBUG_NAME_LEN 64
#define DATABLOCK_INVALID_BLOCK_NR -1

#define DATABLOCK_INITIAL_TRANSFER_WINDOW_DBG 4

// the maximum size of a data block fragment packet (encoded header + actual data)
#define DATABLOCK_FRAGMENT_PACKET_SIZE (MAX_DATABLOCK_FRAGMENT_SIZE + 176)

#define DATABLOCK_HEADER_SIZE 1

// the maximum size of each data block fragment
#define MAX_DATABLOCK_FRAGMENT_SIZE 1024

// the maximum amount of fragments per data block transfer
#define MAX_DATABLOCK_FRAGMENTS 768

// the minimum and maximum size
#define MIN_DATABLOCK_PAYLOAD_SIZE 1
#define MAX_DATABLOCK_PAYLOAD_SIZE (MAX_DATABLOCK_TRANSFER_SIZE - DATABLOCK_HEADER_SIZE)

#define MIN_DATABLOCK_TRANSFER_SIZE (DATABLOCK_HEADER_SIZE + MIN_DATABLOCK_PAYLOAD_SIZE)
#define MAX_DATABLOCK_TRANSFER_SIZE (MAX_DATABLOCK_FRAGMENTS * MAX_DATABLOCK_FRAGMENT_SIZE)

class NetDataBlockSender
{
public:
	NetDataBlockSender();
	virtual ~NetDataBlockSender();

	virtual void SendFragment(const unsigned short blockSeqNr, const int transferSize, const unsigned short cancelSeqNr,
		const unsigned short fragId, const void* const fragData, const int fragSize) = 0;

	virtual int GetTransmissionRate() const = 0;
	virtual const char* GetReceiverName() const = 0;

	void StartBlockSender(const int transferSize, const bool doRateLimit, const char* const debugName, const int transferBudget);

	void ResetState();
	void CancelTransfer();

	void WriteDataBlock(const uint8_t* const sourceData, const int dataSize, const bool doRateLimit, const char* const debugName);
	bool SendPendingFragments(const double startTime);

	struct FragAck_s
	{
		enum
		{
			FRAG_ACKED = 0xFF
		};

		unsigned short blockSeqNr;
		unsigned short cancelSeqNr;

		u64 ackedFragments[(MAX_DATABLOCK_FRAGMENTS + 63) / 64];
	};

	void ProcessFragmentAcks(FragAck_s& ackMsg);

	inline bool IsTransfering() const { return m_transferInProgress; }
	inline int GetFragmentCount() const { return m_totalFragments; }

private:
	struct Header_s
	{
		bool isCompressed;
	};

	static_assert(sizeof(Header_s) == DATABLOCK_HEADER_SIZE);
protected:
	ALIGN64 CThreadRWLock m_Lock;

	void* m_pUserData;
	bool m_transferInProgress;

	// When current transfer time > last transfer time + DATABLOCK_SLOW_MODE_ACK_TIMEOUT,
	// see [r5apex_ds + 0x262C08] 
	bool m_slowMode;

	char m_bMultiplayer;
	char field_8B;

	unsigned short m_blockSeqNr;
	unsigned short m_cancelSeqNr;

	// the total transfer size for the data, and the number of blocks this data
	// has been carved up to
	int m_nTransferSize;
	int m_nTotalBlocks;

	// last block that has been ack'd, and the current block that is pending to
	// be sent to the receiver
	int m_nBlockAckTick;
	int m_nCurrentBlock;

	int m_transferBudget;
	int m_transferedFragments;

	// the resend rate for this connection, which depends of the stability/loss
	// and other factors computed from the netchan
	float m_flResendRate;
	char pad_00AC[4]; // padding, in case we want to stuff our own vars in here

	// times used to determine when a data block has been sent, and how long it
	// took to get this out and acknowledged
	double m_TimeCurrentSend;
	double m_TimeFirstSend;
	double m_TimeLastSend;

	// the last time we attempted to send this block, this gets updated when
	// a data block hasn't been acknowledged in time and is being resent
	double m_flBlockSendTimes[MAX_DATABLOCK_FRAGMENTS];

	// the debug name used when details get dumped to the console
	char m_szDebugName[DATABLOCK_DEBUG_NAME_LEN];

	// if a fragment has been acknowledged by the receiver, we mark that
	// particular fragment as acknowledged
	bool m_fragmentsAcked[MAX_DATABLOCK_FRAGMENTS];
	unsigned char* m_scratchBuffer; // size = sizeof(Header_s) + MAX_DATABLOCK_TRANSFER_SIZE

	// this member is true when:
	// net_debugDataBlockSender.GetBool() == true, or:
	// m_bStartedTransfer == true, or:
	// ( m_TimeCurrentSend - m_TimeFirstSend ) > net_datablock_longSendTime.GetFloat(), or:
	// m_nTransferId < 4
	//
	// if the above condition is true, function ServerDataBlockSender::SendPendingDataBlocks()
	// also returns true, else this function always returns false
	bool m_bAbnormalSend;
};

class NetDataBlockReceiver
{
public:
	NetDataBlockReceiver();
	virtual ~NetDataBlockReceiver();

	virtual void LogState() {}; // Called when cvar 'net_debugDataBlockReceiver' is set;
	virtual void SendFragmentAcks() = 0;

	bool StartBlockReceiver(const int transferSize, const double startTime);
	void ResetState();
	void CancelTransfer(const unsigned short cancelSeqNr);

	struct FragInfo_s
	{
		inline bool IsCopied() const { return *copyStatus; };
		inline void MarkCopied() { *copyStatus = true; }

		inline void SetStatusHandle(bool* const hnd)
		{
			Assert(hnd);
			copyStatus = hnd;
		}

		int scratchOffset;
		int expectedSize;

	private:
		// note(kawe): private to prevent mistakes of checking
		// ptr instead of dereferencing it. Don't change.
		bool* copyStatus;
	};

	bool GetFragmentInfo(const unsigned short fragId, FragInfo_s& info);

	bool IsTransferSizeValid(const int transferSize) const;
	bool IsFragmentIdValid(const unsigned short fragId) const;

	bool ProcessFragment(const double currTime, const unsigned short blockSeqNr, const int transferSize,
		const unsigned short cancelSeqNr, const unsigned short fragId, const void* const fragData, const int fragSize);

	void NextTransfer();

	inline bool TransferFinished() const { return m_transferFinished; }

	inline u8* GetPayloadBuffer() { return m_scratchBuffer + sizeof(Header_s); }
	inline int GetPayloadSize() const { return m_nTransferSize; }

private:
	inline void ClearCopyStatArray() { memset(m_copiedFragments, 0, sizeof(m_copiedFragments)); };

	struct Header_s
	{
		char reserved[3]; // unused in retail
		bool isCompressed;
	};

protected:
	void* m_pUserData;

	// whether the transfer has been started and completed
	bool m_bStartedRecv;
	bool m_bCompletedRecv;

	bool m_usesCompression; // DBG padding var!

	// These get incremented each time we start a new datablock transfer,
	// or cancel the operation.
	unsigned short m_blockSeqNr;
	unsigned short m_cancelSeqNr;

	bool m_transferAckPending;

	// the total transfer size, and the # amount of blocks the data has been
	// carved into
	int m_nTransferSize;
	int m_nTotalBlocks;

	int m_nBlockAckTick;

	// the time the data block receiver was started for this transfer
	double m_flStartTime;

	// Marked if fragment copy was successfull.
	bool m_copiedFragments[MAX_DATABLOCK_FRAGMENTS];

	// buf size = MAX_DATABLOCK_TRANSFER_SIZE + sizeof(Header_s).
	// The start of the buffer is Header_s, for actual block writes, always offset this #!
	unsigned char* m_scratchBuffer;
};

#endif // IDATABLOCK_H
