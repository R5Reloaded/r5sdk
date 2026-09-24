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
