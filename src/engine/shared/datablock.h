//===========================================================================//
// 
// Purpose: data block sender & receiver
// 
//===========================================================================//
#ifndef IDATABLOCK_H
#define IDATABLOCK_H

// the maximum size of each data block fragment
#define MAX_DATABLOCK_FRAGMENT_SIZE 1024

// the maximum amount of fragments per data block transfer
#define MAX_DATABLOCK_FRAGMENTS 768

#define DATABLOCK_SLOW_MODE_ACK_TIMEOUT 5.0

#define DATABLOCK_DEBUG_NAME_LEN 64
#define DATABLOCK_INVALID_BLOCK_NR -1

#define DATABLOCK_INITIAL_TRANSFER_WINDOW_DBG 4

// the maximum size of a data block fragment packet (encoded header + actual data)
#define DATABLOCK_FRAGMENT_PACKET_SIZE (MAX_DATABLOCK_FRAGMENT_SIZE + 176)

// the maximum total transfer size
#define MAX_DATABLOCK_TRANSFER_SIZE (MAX_DATABLOCK_FRAGMENTS * MAX_DATABLOCK_FRAGMENT_SIZE)

//-----------------------------------------------------------------------------
// Forward decelerations
//-----------------------------------------------------------------------------
class CClient;
class CClientState;

abstract_class NetDataBlockSender
{
public:
	virtual ~NetDataBlockSender();


	virtual void SendDataBlock(const short blockSeqNr, const int transferSize,
		const short cancelSeqNr, const short fragId, const void* const fragData, const int fragSize) = 0;

	virtual int GetTransmissionRate() const = 0;
	virtual const char* GetReceiverName() const = 0;

	void StartBlockSender(const int transferSize, const bool isMultiplayer, const char* const debugName);
	void ResetBlockSender();

protected:
	ALIGN64 CThreadRWLock m_Lock;

	// the server side client handle that is our 'receiving' end
	CClient* m_pClient;

	bool m_transferInProgress;

	// When current transfer time > last transfer time + DATABLOCK_SLOW_MODE_ACK_TIMEOUT,
	// see [r5apex_ds + 0x262C08] 
	bool m_slowMode;

	char m_bMultiplayer;
	char field_8B;

	// the current transfer id, and the global transfer count for this
	// particular client. the transfer nr keeps getting incremented on
	// each new context setup
	short m_nTransferId;
	short m_nTransferNr;

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
	unsigned char* m_scratchBuffer; // size = sizeof(ServerDataBlockHeader_s) + MAX_DATABLOCK_TRANSFER_SIZE

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

abstract_class NetDataBlockReceiver
{
public:
	virtual ~NetDataBlockReceiver() {};
	// Called when cvar 'net_debugDataBlockReceiver' is set;
	// currently a nullsub in the engine.
	virtual void DebugDataBlockReceiver() {};
	virtual void AcknowledgeTransmission() = 0;

	bool StartBlockReceiver(const int transferSize, const double startTime);
	void ResetBlockReceiver(const short cancelSeqNr);

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

	bool GetFragmentInfo(const short fragmentId, FragInfo_s& info);

	bool IsTransferSizeValid(const int transferSize) const;
	bool IsFragmentIdValid(const short fragmentId) const;

protected:
	// the client side 'client' handle
	CClientState* m_pClientState;

	// whether the transfer has been started and completed
	bool m_bStartedRecv;
	bool m_bCompletedRecv;

	bool byte12;

	// the current transfer id, and the global transfer count for this
	// particular client. the transfer nr keeps getting incremented on
	// each new context setup
	short m_TransferId;
	short m_nTransferNr;

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

	// buf size = MAX_DATABLOCK_TRANSFER_SIZE + sizeof(ClientDataBlockHeader_s).
	// The start of the buffer is ClientDataBlockHeader_s, for actual block writes, always offset this #!
	unsigned char* m_scratchBuffer;
};

inline void* (*NetDataBlockSender__Destructor)(NetDataBlockSender* thisptr);
inline void* (*NetDataBlockReceiver__Destructor)(NetDataBlockReceiver* thisptr);

///////////////////////////////////////////////////////////////////////////////
class VNetDataBlock : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("NetDataBlockSender::~NetDataBlockSender", NetDataBlockSender__Destructor);
		LogFunAdr("NetDataBlockReceiver::~NetDataBlockReceiver", NetDataBlockReceiver__Destructor);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 57 48 83 EC 20 8B DA 48 8B F9 E8 ?? ?? ?? ?? F6 C3 01 74"
			" 0D BA ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 48 8B C7 48 8B 5C 24 ?? 48 83 C4 20 5F C3 CC CC CC CC CC CC CC CC CC CC CC CC 48 89 5C 24"
			" ?? 48 89 74 24 ?? 57 48 83 EC 20 33 F6 66 C7 81 ?? ?? ?? ?? ?? ??").GetPtr(NetDataBlockSender__Destructor);

		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 57 48 83 EC 20 8B DA 48 8B F9 E8 ?? ?? ?? ?? F6 C3 01 74"
			" 0D BA ?? ?? ?? ?? 48 8B CF E8 ?? ?? ?? ?? 48 8B C7 48 8B 5C 24 ?? 48 83 C4 20 5F C3 CC CC CC CC CC CC CC CC CC CC CC CC 48 89 5C 24"
			" ?? 48 89 74 24 ?? 57 48 83 EC 20 48 8D 05 ?? ?? ?? ?? C6 41 12 00").GetPtr(NetDataBlockReceiver__Destructor);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { }
};
///////////////////////////////////////////////////////////////////////////////

#endif // IDATABLOCK_H
