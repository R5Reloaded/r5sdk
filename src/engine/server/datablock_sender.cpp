//===========================================================================//
// 
// Purpose: server side data block sender
// 
//===========================================================================//
#include "engine/client/client.h"
#include "engine/host_cmd.h"
#include "common/proto_oob.h"
#include "datablock_sender.h"

//-----------------------------------------------------------------------------
// Purpose: sends the data block
//-----------------------------------------------------------------------------
void ServerDataBlockSender::SendFragment(const unsigned short blockSeqNr, const int transferSize,
	const unsigned short cancelSeqNr, const unsigned short fragId, const void* const fragData, const int fragSize)
{
	SendFragmentGuts(this, blockSeqNr, transferSize, cancelSeqNr, fragId, fragData, fragSize);
}

void ServerDataBlockSender::SendFragmentGuts(ServerDataBlockSender* const sender, const unsigned short blockSeqNr, const int transferSize,
	const unsigned short cancelSeqNr, const unsigned short fragId, const void* const fragData, const int fragSize)
{
	const CClient* const cl = sender->GetClient();

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

	// msg data (gets processed on client's out of band packet handler)
	buf.WriteULong(CONNECTIONLESS_HEADER);
	buf.WriteByte(S2C_DATABLOCK_FRAGMENT);

	// transfer info
	buf.WriteWord(blockSeqNr);
	buf.WriteLong(transferSize);
	buf.WriteWord(cancelSeqNr);

	// block info
	buf.WriteWord(fragId);
	buf.WriteLong(fragSize);

	// block data
	buf.WriteBytes(fragData, fragSize);

	if (net_debugDataBlockSender->GetInt() >= 2)
	{
		Msg(eDLL_T::ENGINE,
			"[DataBlock:Tx] sending fragment #%hu of block #%hu to \"%s\": %d bytes "
			"(transfer %d bytes, cancel sequence %hu)\n",
			fragId,
			blockSeqNr,
			sender->GetReceiverName(),
			fragSize,
			transferSize,
			cancelSeqNr);
	}

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
	return GetTransmissionRateGuts(this);
}

int ServerDataBlockSender::GetTransmissionRateGuts(const ServerDataBlockSender* const sender)
{
	const CClient* const cl = sender->GetClient();

	if (!cl)
		return net_datablock_fastRate->GetInt();

	const CNetChan* const chan = cl->GetNetChan();

	if (!chan)
	{
		Assert(0, __FUNC_PFX__"Called without a valid net channel!");
		return net_datablock_fastRate->GetInt();
	}

	if (sender->m_slowMode)
		return net_datablock_slowRate->GetInt();

	const float networkLoss = chan->GetTotalNetworkLoss();

	if (networkLoss >= net_datablock_networkLossForSlowSpeed->GetFloat())
		return net_datablock_slowRate->GetInt();

	if (sender->m_flResendRate > net_datablock_resendRateForSlowSpeed->GetFloat())
		return net_datablock_slowRate->GetInt();

	return net_datablock_fastRate->GetInt();
}

//-----------------------------------------------------------------------------
// Purpose: gets the receiver name (client name as registered on the server)
//-----------------------------------------------------------------------------
const char* ServerDataBlockSender::GetReceiverName() const
{
	const CClient* const cl = GetClient();

	if (!cl)
		return "<<null>>";

	return cl->GetServerName();
}

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
static bool NetDataBlockSender_SendPendingFragments(NetDataBlockSender* sender, const double currTime)
{
	return sender->SendPendingFragments(currTime);
}

static void NetDataBlockSender_CancelTransfer(NetDataBlockSender* sender)
{
	sender->CancelTransfer();
}

void VServerDataBlockSender::Detour(const bool bAttach) const
{
	void* null;

	DetourSetup(&NetDataBlockSender__SendPendingFragments, NetDataBlockSender_SendPendingFragments, bAttach);
	DetourSetup(&NetDataBlockSender__CancelTransfer, NetDataBlockSender_CancelTransfer, bAttach);
	CMemory::HookVirtualMethod((uintptr_t)g_pServerDataBlockSender_VFTable, ServerDataBlockSender::SendFragmentGuts, 1, &null);
	CMemory::HookVirtualMethod((uintptr_t)g_pServerDataBlockSender_VFTable, ServerDataBlockSender::GetTransmissionRateGuts, 2, &null);
}
