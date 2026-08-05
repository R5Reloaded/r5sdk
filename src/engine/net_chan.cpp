//=============================================================================//
//
// Purpose: Netchannel system utilities
//
//=============================================================================//

#include "core/stdafx.h"
#include "tier0/frametask.h"
#include "tier1/cvar.h"
#include "tier1/keyvalues.h"
#include "common/callback.h"
#include "engine/net.h"
#include "engine/net_chan.h"
#ifndef CLIENT_DLL
#include "engine/server/server.h"
#include "engine/client/client.h"
#include "server/vengineserver_impl.h"
#endif // !CLIENT_DLL

//-----------------------------------------------------------------------------
// Console variables
//-----------------------------------------------------------------------------
static ConVar net_processTimeBudget("net_processTimeBudget", "200", FCVAR_RELEASE, "Net message process time budget in milliseconds (removing netchannel if exceeded).", true, 0.f, false, 0.f, "0 = disabled");

extern ConVar net_compression_method;
extern ConVar net_compression_debug;

//-----------------------------------------------------------------------------
// Purpose: gets the netchannel resend rate
// Output : float
//-----------------------------------------------------------------------------
float CNetChan::GetResendRate() const
{
	const int64_t totalupdates = this->m_DataFlow[FLOW_INCOMING].totalupdates;

	if (!totalupdates && !this->m_nSequencesSkipped)
		return 0.0f;

	float lossRate = (float)(totalupdates + m_nSequencesSkipped);

	if (totalupdates + m_nSequencesSkipped < 0.0f)
		lossRate += float(2 ^ 64);

	return m_nSequencesSkipped / lossRate;
}

//-----------------------------------------------------------------------------
// Purpose: gets the netchannel sequence number
// Input  : flow - 
// Output : int
//-----------------------------------------------------------------------------
int CNetChan::GetSequenceNr(int flow) const
{
	if (flow == FLOW_OUTGOING)
	{
		return m_nOutSequenceNr;
	}
	else if (flow == FLOW_INCOMING)
	{
		return m_nInSequenceNr;
	}

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: gets the netchannel connect time
// Output : double
//-----------------------------------------------------------------------------
double CNetChan::GetTimeConnected(void) const
{
	double t = *g_pNetTime - connect_time;
	return (t > 0.0) ? t : 0.0;
}

//-----------------------------------------------------------------------------
// Purpose: gets the number of bits written in selected stream
//-----------------------------------------------------------------------------
int CNetChan::GetNumBitsWritten(const bool bReliable)
{
    bf_write* pStream = &m_StreamUnreliable;

    if (bReliable)
    {
        pStream = &m_StreamReliable;
    }

    return pStream->GetNumBitsWritten();
}

//-----------------------------------------------------------------------------
// Purpose: gets the number of bits written in selected stream
//-----------------------------------------------------------------------------
int CNetChan::GetNumBitsLeft(const bool bReliable)
{
    bf_write* pStream = &m_StreamUnreliable;

    if (bReliable)
    {
        pStream = &m_StreamReliable;
    }

    return pStream->GetNumBitsLeft();
}

//-----------------------------------------------------------------------------
// Purpose: flows a new packet
// Input  : *pChan   - 
//          outSeqNr - 
//          inSeqNr  - 
//          nChoked  - 
//          nDropped - 
//          nSize    - 
//-----------------------------------------------------------------------------
void CNetChan::_FlowNewPacket(CNetChan* const pChan, const int flow, const int outSeqNr, const int inSeqNr, const int nChoked, const int nDropped, const int nSize)
{
    netflow_t* const pflow = &pChan->m_DataFlow[flow];

    netframe_header_t* frameheader = nullptr;
    netframe_t* frame = nullptr;

    const int currentindex = pflow->currentindex;
    const float netTime = (float)*g_pNetTime;

    if (outSeqNr > currentindex)
    {
        // If client sends a malformed packet with an 'outSeqNr' that differs
        // greatly from our current index, the loop will hang. Make sure we
        // never execute more than NET_FRAMES_BACKUP iterations as that is the
        // total storage we have in the frames and headers. If we receive a
        // packet with a greater delta, we clear all the frames to reset the
        // statistics as they have then been invalidated.
        if (outSeqNr - currentindex > NET_FRAMES_BACKUP)
        {
            memset(pflow->frame_headers, 0, sizeof(pflow->frame_headers));
            netframe_header_t* const frameHeader = &pflow->frame_headers[outSeqNr & NET_FRAMES_MASK];

            frameHeader->time = netTime;
            frameHeader->latency = -1.0f;
        }
        else
        {
            for (int i = currentindex + 1; (i <= outSeqNr); ++i)
            {
                const int frameIndex = i & NET_FRAMES_MASK;

                frameheader = &pflow->frame_headers[frameIndex];

                frameheader->time = netTime; // Now.
                frameheader->size = 0;
                frameheader->choked = 0; // Not acknowledged yet.
                frameheader->valid = false;
                frameheader->latency = -1.0f; // Not acknowledged yet.

                frame = &pflow->frames[frameIndex];

                frame->dropped = 0;
                frame->avg_latency = pChan->GetAvgLatency(FLOW_OUTGOING);

                const int backTrack = outSeqNr - i;

                if (backTrack < (nChoked + nDropped))
                {
                    if (backTrack < nChoked)
                    {
                        frameheader->choked = 1;
                    }
                    else
                    {
                        frame->dropped = 1;
                    }
                }
            }

            frameheader->size = nSize;
            frameheader->choked = (short)nChoked;
            frameheader->valid = true;
            frame->dropped = nDropped;
            frame->avg_latency = pChan->GetAvgLatency(FLOW_OUTGOING);
        }
    }

    pflow->totalpackets++;
    pflow->currentindex = outSeqNr;
    pflow->currentframe = frame;

    // Update ping for acknowledged packet.
    const int aflowIndex = (flow == FLOW_OUTGOING) ? FLOW_INCOMING : FLOW_OUTGOING;
    netflow_t* const aflow = &pChan->m_DataFlow[aflowIndex];

    if (inSeqNr > (aflow->currentindex - NET_FRAMES_BACKUP))
    {
        netframe_header_t* const aframe = &aflow->frame_headers[inSeqNr & NET_FRAMES_MASK];

        if (aframe->valid && aframe->latency == -1.0f)
        {
            const float latency = Max(0.0f, netTime - aframe->time);
            aframe->latency = latency;

            pflow->latency += latency;
            pflow->maxlatency = Max(pflow->maxlatency, latency);

            pflow->totalupdates++;
        }
    }
    else // Acknowledged packet isn't in backup buffer anymore.
    {
        netframe_header_t* const aframe = &aflow->frame_headers[aflow->currentindex & NET_FRAMES_MASK];
        netframe_header_t* const nframe = &aflow->frame_headers[aflow->currentindex + 1 & NET_FRAMES_MASK];

        static const float DELTA_INTERP = 127.0f;

        const float delta = (aframe->time - nframe->time) / DELTA_INTERP;
        const int backTrack = aflow->currentindex - inSeqNr;

        const float latency = (delta * backTrack) + netTime - aframe->time;

        pflow->latency += latency;
        pflow->maxlatency = Max(pflow->maxlatency, latency);

        pflow->totalupdates++;
    }
}

//-----------------------------------------------------------------------------
// Purpose: shutdown netchannel
// Input  : *this - 
//			*szReason - 
//			bBadRep - 
//			bRemoveNow - 
//-----------------------------------------------------------------------------
void CNetChan::_Shutdown(CNetChan* pChan, const char* szReason, uint8_t bBadRep, bool bRemoveNow)
{
	CNetChan__Shutdown(pChan, szReason, bBadRep, bRemoveNow);
}

//-----------------------------------------------------------------------------
// Purpose: process message
// Input  : *pChan - 
//			*pMsg - 
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool CNetChan::_ProcessMessages(CNetChan* pChan, bf_read* pBuf)
{
#ifndef CLIENT_DLL
    if (!net_processTimeBudget.GetInt() || !ThreadInServerFrameThread())
        return pChan->ProcessMessages(pBuf);

    const double flStartTime = Plat_FloatTime();
    const bool bResult = pChan->ProcessMessages(pBuf);

    if (!pChan->m_MessageHandler) // NetChannel removed?
        return bResult;

    CClient* const pClient = reinterpret_cast<CClient*>(pChan->m_MessageHandler);
    CClientExtended* const pExtended = pClient->GetClientExtended();

    // Reset every second.
    if ((flStartTime - pExtended->GetNetProcessingTimeBase()) > 1.0)
    {
        pExtended->SetNetProcessingTimeBase(flStartTime);
        pExtended->SetNetProcessingTimeMsecs(0.0, 0.0);
    }

    const double flCurrentTime = Plat_FloatTime();
    pExtended->SetNetProcessingTimeMsecs(flStartTime, flCurrentTime);

    if (pExtended->GetNetProcessingTimeMsecs() > net_processTimeBudget.GetFloat())
    {
        Warning(eDLL_T::SERVER, "Removing netchannel %s(%s) (exceeded time budget by %3.1fms!)\n",
            pChan->GetName(), pChan->GetAddress(), (pExtended->GetNetProcessingTimeMsecs() - net_processTimeBudget.GetFloat()));
        pClient->Disconnect(Reputation_t::REP_MARK_BAD, "#DISCONNECT_NETCHAN_OVERFLOW");

        return false;
    }

    return bResult;
#else // !CLIENT_DLL
    return pChan->ProcessMessages(pBuf);
#endif
}

//-----------------------------------------------------------------------------
// Purpose: process message
// Input  : *buf - 
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool CNetChan::ProcessMessages(bf_read* buf)
{
    m_bStopProcessing = false;

    const char* showMsgName = net_showmsg->GetString();
    const char* blockMsgName = net_blockmsg->GetString();
    const int netPeak = net_showpeaks->GetInt();

    if (*showMsgName == '0')
    {
        showMsgName = NULL; // dont do strcmp all the time
    }

    if (*blockMsgName == '0')
    {
        blockMsgName = NULL; // dont do strcmp all the time
    }

    if (netPeak > 0 && netPeak < buf->GetNumBytesLeft())
    {
        showMsgName = "1"; // show messages for this packet only
    }

    while (true)
    {
        int cmd = net_NOP;

        while (true)
        {
            if (buf->GetNumBitsLeft() < NETMSG_TYPE_BITS)
                return true; // Reached the end.

            if (!NET_ReadMessageType(&cmd, buf) && buf->m_bOverflow)
            {
                Error(eDLL_T::ENGINE, 0, "%s(%s): Incoming buffer overflow!\n", __FUNCTION__, GetAddress());
                m_MessageHandler->ConnectionCrashed("Buffer overflow in net message");

                return false;
            }

            if (cmd <= net_Disconnect)
                break; // Either a Disconnect or NOP packet; process it below.

            INetMessage* netMsg = FindMessage(cmd);

            if (!netMsg)
            {
                DevWarning(eDLL_T::ENGINE, "%s(%s): Received unknown net message (%i)!\n",
                    __FUNCTION__, GetAddress(), cmd);
                Assert(0);
                return false;
            }

            if (!netMsg->ReadFromBuffer(buf))
            {
                DevWarning(eDLL_T::ENGINE, "%s(%s): Failed reading message '%s'!\n",
                    __FUNCTION__, GetAddress(), netMsg->GetName());
                Assert(0);
                return false;
            }

            if (showMsgName)
            {
                if ((*showMsgName == '1') || !Q_stricmp(showMsgName, netMsg->GetName()))
                {
                    Msg(eDLL_T::ENGINE, "%s(%s): Received: %s\n",
                        __FUNCTION__, GetAddress(), netMsg->ToString());
                }
            }

            if (blockMsgName)
            {
                if ((*blockMsgName == '1') || !Q_stricmp(blockMsgName, netMsg->GetName()))
                {
                    Msg(eDLL_T::ENGINE, "%s(%s): Blocked: %s\n",
                        __FUNCTION__, GetAddress(), netMsg->ToString());

                    continue;
                }
            }

            // Netmessage calls the Process function that was registered by
            // it's MessageHandler.
            m_bProcessingMessages = true;
            const bool bRet = netMsg->Process();
            m_bProcessingMessages = false;

            // This means we were deleted during the processing of that message.
            if (m_bShouldDelete)
            {
                delete this;
                return false;
            }

            // This means our message buffer was freed or invalidated during
            // the processing of that message.
            if (m_bStopProcessing)
                return false;

            if (!bRet)
            {
                DevWarning(eDLL_T::ENGINE, "%s(%s): Failed processing message '%s'!\n",
                    __FUNCTION__, GetAddress(), netMsg->GetName());
                Assert(0);
                return false;
            }

            if (IsOverflowed())
                return false;
        }

        m_bProcessingMessages = true;

        if (cmd == net_NOP) // NOP; continue to next packet.
        {
            m_bProcessingMessages = false;
            continue;
        }
        else if (cmd == net_Disconnect) // Disconnect request.
        {
            char reason[1024];
            buf->ReadString(reason, sizeof(reason), false);

            m_MessageHandler->ConnectionClosing(reason, 1);
            m_bProcessingMessages = false;
        }

        m_bProcessingMessages = false;

        if (m_bShouldDelete)
            delete this;

        return false;
    }
}

void CNetChan::CreateFragmentsFromBuffer(bf_write* pBuff)
{
    _CreateFragmentsFromBuffer(this, pBuff);
}

void CNetChan::_CreateFragmentsFromBuffer(CNetChan* thisp, bf_write* pBuff)
{
    //byte align
    const int nBitsWritten = pBuff->GetNumBitsWritten();
    if (8 - nBitsWritten % 8 < 8)
    {
        pBuff->WriteUBitLong(0, 8 - nBitsWritten % 8);
    }

    size_t nBytes = pBuff->GetNumBytesWritten();
    const size_t nUncompressedSize = nBytes;
    const uint8_t* pData = pBuff->GetData();
    bool bIsCompressed = false;

    if (nBytes > 512)
    {
        const NetPacketCompressionMethod_e eCompressionMethod = static_cast<NetPacketCompressionMethod_e>(net_compression_method.GetInt());

        size_t nNeededCompressionBufferLen = 0;
        if (!NET_BufferToBufferCompressMultiMethodNeededCapactity(nBytes, nNeededCompressionBufferLen, eCompressionMethod))
            return;

        unsigned char* pCompressedBuffer = new unsigned char[nNeededCompressionBufferLen];
        if (!NET_BufferToBufferCompressMultiMethod(pCompressedBuffer, nNeededCompressionBufferLen, pData, nBytes, eCompressionMethod))
        {
            delete[] pCompressedBuffer;
            return;
        }

        nBytes = nNeededCompressionBufferLen;
        pData = pCompressedBuffer;
        bIsCompressed = true;
    }

    if (nBytes)
    {
        size_t nBytesLeft = nBytes;
        while (nBytesLeft > 0)
        {
            dataFragments_t* pFragment = new dataFragments_t;
            const size_t nFragmentBlockSize = min(nBytesLeft, 560);
            char* pFragmentBlock = new char[nFragmentBlockSize];

            pFragment->blockSize = nFragmentBlockSize;
            pFragment->buffer = pFragmentBlock;
            pFragment->firstFragment = nBytesLeft == nBytes;
            pFragment->isCompressed = bIsCompressed;
            pFragment->transferSize = (int)nBytes;
            pFragment->uncompressedSize = nUncompressedSize;
            pFragment->transferID = -1;

            memcpy(pFragment->buffer, &pData[nBytes - nBytesLeft], nFragmentBlockSize);

            nBytesLeft -= nFragmentBlockSize;
            pFragment->lastFragment = nBytesLeft == 0;

            thisp->m_WaitingList.AddToTail(pFragment);
        }
    }

    thisp->m_nSubOutSequenceNr = thisp->m_nOutSequenceNrAck;

    if (bIsCompressed)
        delete[] pData;
}

bool CNetChan::ReadSubChannelData(bf_read* pBuff)
{
    return _ReadSubChannelData(this, pBuff);
}

bool CNetChan::SendSubChannelData(bf_write* pBuff)
{
    return _SendSubChannelData(this, pBuff);
}

bool CNetChan::_SendSubChannelData(CNetChan* thisp, bf_write* pBuff)
{
    if (thisp->m_bShuttingDown)
        return false;

    if (thisp->m_WaitingList.IsEmpty())
        return false;

    const int subOutFragmentsAckClamped = thisp->m_nSubOutFragmentsAck & 1023;

    pBuff->WriteUBitLong(0xABCDEF01, 32);
    pBuff->WriteUBitLong(thisp->m_nSubOutFragmentsAck, 32);
    pBuff->WriteUBitLong(subOutFragmentsAckClamped, 10);

    if (!subOutFragmentsAckClamped)
    {
        if (thisp->m_nSubOutFragmentsAck)
        {
            pBuff->WriteOneBit(false);
        }
        else
        {
            pBuff->WriteOneBit(true);
            pBuff->WriteUBitLong(thisp->m_nNonceHost, 32);
        }
    }

    thisp->m_nSubOutSequenceNr = thisp->m_nOutSequenceNr;
    const int nFragmentsToCreate = min(1023, thisp->m_WaitingList.Count());
    for (int iFragment = 0; iFragment < nFragmentsToCreate; iFragment++)
    {
        dataFragments_t* pFragment = thisp->m_WaitingList.Element(iFragment);
        
        if(iFragment)
            pBuff->WriteOneBit(true);

        const int nFragmentOffset = iFragment + thisp->m_nSubOutFragmentsAck;
        pBuff->WriteUBitLong(nFragmentOffset, 32);
        pBuff->WriteOneBit(pFragment->firstFragment);

        if (pFragment->firstFragment)
        {
            pBuff->WriteUBitLong(pFragment->transferSize, 19);
            pBuff->WriteOneBit(pFragment->isCompressed);
            if (pFragment->isCompressed)
            {
                pBuff->WriteUBitLong(static_cast<NetPacketCompressionMethod_e>(net_compression_method.GetInt()), 8);
                pBuff->WriteUBitLong(static_cast<unsigned int>(pFragment->uncompressedSize), 22);
            }
        }
        else
        {
            if (pFragment->lastFragment)
            {
                pBuff->WriteOneBit(true);
                pBuff->WriteUBitLong(static_cast<unsigned int>(pFragment->blockSize), 10);
            }
            else
            {
                pBuff->WriteOneBit(false);
            }
        }

        pBuff->WriteBytes(pFragment->buffer, static_cast<unsigned int>(pFragment->blockSize));
        if (!pFragment->isOutbound)
        {
            pFragment->isOutbound = true;
            pFragment->transferID = thisp->m_nOutSequenceNr;
        }
    }

    pBuff->WriteOneBit(false);
    return true;
}

bool CNetChan::ProcessSubChannelBuffer()
{
    if (m_ReceiveList.isCompressed)
    {
        const NetPacketCompressionMethod_e compressionMethod = m_ReceiveList.eCompressionMethod;
        const size_t nCompressedDataLen = m_ReceiveList.transferSize;
        const uint8_t* const pCompressedBuffer = reinterpret_cast<const uint8_t* const>(m_ReceiveList.buffer);

        size_t nNeededDecompressionBufferSize = 0;
        if (!NET_BufferToBufferDecompressGetNeededDecompressionBufferSize(pCompressedBuffer,
            nCompressedDataLen, nNeededDecompressionBufferSize, compressionMethod))
            return false;
        
        uint8_t* const pDecompressionBuffer = new uint8_t[nNeededDecompressionBufferSize];
        if (!NET_BufferToBufferDecompressMultiMode(pCompressedBuffer, nCompressedDataLen, pDecompressionBuffer, nNeededDecompressionBufferSize, compressionMethod))
        {
            delete[] pDecompressionBuffer;
            return false;
        }

        if (nNeededDecompressionBufferSize != static_cast<size_t>(m_ReceiveList.uncompressedSize))
        {
            delete[] pDecompressionBuffer;
            return false;
        }

        delete[] m_ReceiveList.buffer;
        m_ReceiveList.buffer = reinterpret_cast<char*>(pDecompressionBuffer);
        m_ReceiveList.blockSize = nNeededDecompressionBufferSize;
        m_ReceiveList.isCompressed = false;
    }
  
    bf_read messageBuffer(m_ReceiveList.buffer, static_cast<int>(m_ReceiveList.uncompressedSize));

    if (!ProcessMessages(&messageBuffer))
        return false;

    return true;
}

bool CNetChan::_ReadSubChannelData(CNetChan* thisp, bf_read* pBuff)
{
    thisp->m_bInReliableState = true;
    NetPacketCompressionMethod_e compressionMethod = INVALID;
    const unsigned int nAlignmentCheck = pBuff->ReadUBitLong(32);
    if (nAlignmentCheck != 0xABCDEF01)
        return false;

    const unsigned int nSubOutFragmentsAck = pBuff->ReadUBitLong(32);
    const unsigned int nSubOutFragmentsAckClamped = pBuff->ReadUBitLong(10);

    if (thisp->m_bReceivedRemoteNonce && nSubOutFragmentsAck > (unsigned int)thisp->m_nSubInFragments)
    {
        Assert(0);
        return false;
    }

    if (nSubOutFragmentsAckClamped || !pBuff->ReadOneBit())
    {
        thisp->m_bPendingRemoteNonceAck = false;
    }
    else
    {
        const uint32_t nNonce = pBuff->ReadUBitLong(32);
        if (nNonce != thisp->m_nNonceRemote)
        {
            thisp->m_nNonceRemote = nNonce;
            thisp->m_bReceivedRemoteNonce = true;
            thisp->m_bInReliableState = false;
            thisp->m_nSubInFragments = 0;
            thisp->FreeReceiveList();
            thisp->m_bPendingRemoteNonceAck = true;
        }
    }

    int nFragmentCatchUp = INT32_MAX;
    if (thisp->m_bReceivedRemoteNonce)
    {
        nFragmentCatchUp = (thisp->m_nSubInFragments & 1023) - nSubOutFragmentsAckClamped;
        if (nFragmentCatchUp < 0)
        {
            nFragmentCatchUp += 1024;
        }
    }
 
    do
    {
        int nBlockSize = 560;
        int nUncompressedSize = 0;
        int nTransferSize = 0;
        bool bCompressed = false;

        const int nFragment = pBuff->ReadUBitLong(32);
        if (thisp->m_bReceivedRemoteNonce && nFragment + nFragmentCatchUp != thisp->m_nSubInFragments)
        {
            Assert(0);
            return false;
        }

        const bool bFirstFragment = pBuff->ReadOneBit();
        if (bFirstFragment)
        {
            if (nFragmentCatchUp <= 0 && thisp->m_ReceiveList.buffer)
            {
                Assert(0);
                return false;
            }

            nTransferSize = pBuff->ReadUBitLong(19);

            if (nTransferSize > NET_MAX_PAYLOAD)
                return false;

            bCompressed = pBuff->ReadOneBit();
            if (bCompressed)
            {
                compressionMethod = static_cast<NetPacketCompressionMethod_e>(pBuff->ReadUBitLong(8));
                nUncompressedSize = pBuff->ReadUBitLong(22);
            }
            else
                nUncompressedSize = nTransferSize;

            nBlockSize = min(nTransferSize, 560);
        }
        else
        {
            const bool bIsLastFragment = pBuff->ReadOneBit();
            if (bIsLastFragment)
            {
                nBlockSize = pBuff->ReadUBitLong(10);
                thisp->m_ReceiveList.blockSize = nBlockSize;
            }
        }

        if (nFragmentCatchUp > 0)
        {
            pBuff->SeekRelative(nBlockSize * 8);
            nFragmentCatchUp--;
        }
        else
        {
            if (nFragment != thisp->m_nSubInFragments)
            {
                Assert(0);
                return false;
            }

            if (bFirstFragment)
            {
                thisp->FreeReceiveList();
                thisp->m_ReceiveList.blockSize = nBlockSize;
                thisp->m_ReceiveList.uncompressedSize = nUncompressedSize;
                thisp->m_ReceiveList.transferSize = nTransferSize;
                thisp->m_ReceiveList.isCompressed = bCompressed;
                thisp->m_ReceiveList.eCompressionMethod = compressionMethod;
                thisp->m_ReceiveList.currentOffset = 0;
                thisp->m_ReceiveList.buffer = new char[nTransferSize];
            }

            if (!thisp->m_ReceiveList.buffer)
                return false;

            if (thisp->m_ReceiveList.currentOffset + nBlockSize > thisp->m_ReceiveList.transferSize)
            {
                thisp->FreeReceiveList();
                return false;
            }

            pBuff->ReadBytes(&thisp->m_ReceiveList.buffer[thisp->m_ReceiveList.currentOffset], nBlockSize);
            thisp->m_ReceiveList.currentOffset += nBlockSize;
            thisp->m_nSubInFragments++;

            if (thisp->m_ReceiveList.currentOffset == thisp->m_ReceiveList.transferSize)
            {
                if (!thisp->ProcessSubChannelBuffer())
                    return false;

                thisp->FreeReceiveList();
            }
        }

    } while (pBuff->ReadOneBit());

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: send message
// Input  : &msg - 
//			bForceReliable - 
//			bVoice - 
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool CNetChan::SendNetMsg(INetMessage& msg, const bool bForceReliable, const bool bVoice)
{
	if (remote_address.GetType() == netadrtype_t::NA_NULL)
		return true;

	bf_write* pStream = &m_StreamUnreliable;

	if (msg.IsReliable() || bForceReliable)
		pStream = &m_StreamReliable;

	if (bVoice)
		pStream = &m_StreamVoice;

	if (pStream == &m_StreamUnreliable && pStream->GetNumBytesLeft() < NET_UNRELIABLE_STREAM_MINSIZE)
		return true;

	AcquireSRWLockExclusive(&m_Lock);

	pStream->WriteUBitLong(msg.GetType(), NETMSG_TYPE_BITS);
	const bool ret = msg.WriteToBuffer(pStream);

	ReleaseSRWLockExclusive(&m_Lock);

	return !pStream->IsOverflowed() && ret;
}

//-----------------------------------------------------------------------------
// Purpose: send data
// Input  : &msg - 
//			bReliable - 
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool CNetChan::SendData(bf_write& msg, const bool bReliable)
{
    // Always queue any pending reliable data ahead of the fragmentation buffer

    if (remote_address.GetType() == netadrtype_t::NA_NULL)
        return true;

    if (msg.GetNumBitsWritten() <= 0)
        return true;

    if (msg.IsOverflowed() && !bReliable)
        return true;

    bf_write& buf = bReliable
        ? m_StreamReliable
        : m_StreamUnreliable;

    const int dataBits = msg.GetNumBitsWritten();
    const int bitsLeft = buf.GetNumBitsLeft();

    if (dataBits > bitsLeft)
    {
        if (bReliable)
        {
            Error(eDLL_T::ENGINE, 0, "%s(%s): Data too large for reliable buffer (%i > %i)!\n", 
                __FUNCTION__, GetAddress(), msg.GetNumBytesWritten(), buf.GetNumBytesLeft());

            m_MessageHandler->ChannelDisconnect("reliable buffer is full");
        }

        return false;
    }

    return buf.WriteBits(msg.GetData(), dataBits);
}

//-----------------------------------------------------------------------------
// Purpose: finds a registered net message by type
// Input  : type - 
// Output : net message pointer on success, NULL otherwise
//-----------------------------------------------------------------------------
INetMessage* CNetChan::FindMessage(const int type)
{
    const int numtypes = m_NetMessages.Count();

    for (int i = 0; i < numtypes; i++)
    {
        INetMessage* const message = m_NetMessages[i];

        if (message->GetType() == type)
            return message;
    }

    return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: registers a net message
// Input  : *msg
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CNetChan::RegisterMessage(INetMessage* msg)
{
    Assert(msg);

    if (FindMessage(msg->GetType()))
    {
        Assert(0); // Duplicate registration!
        return false;
    }

    m_NetMessages.AddToTail(msg);
    msg->SetNetChannel(this);

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: free's the receive data fragment list
//-----------------------------------------------------------------------------
void CNetChan::FreeReceiveList()
{
    m_ReceiveList.blockSize = NULL;
    m_ReceiveList.transferSize = NULL;
    if (m_ReceiveList.buffer)
    {
        delete m_ReceiveList.buffer;
        m_ReceiveList.buffer = nullptr;
    }
}

//-----------------------------------------------------------------------------
// Purpose: check if there is still data in the reliable waiting buffers
//-----------------------------------------------------------------------------
bool CNetChan::HasPendingReliableData(void)
{
	return (m_StreamReliable.GetNumBitsWritten() > 0)
		|| (m_WaitingList.Count() > 0);
}

///////////////////////////////////////////////////////////////////////////////
void VNetChan::Detour(const bool bAttach) const
{
	DetourSetup(&CNetChan__Shutdown, &CNetChan::_Shutdown, bAttach);
	DetourSetup(&CNetChan__FlowNewPacket, &CNetChan::_FlowNewPacket, bAttach);
	DetourSetup(&CNetChan__ProcessMessages, &CNetChan::_ProcessMessages, bAttach);
    DetourSetup(&CNetChan__CreateFragmentsFromBuffer, &CNetChan::_CreateFragmentsFromBuffer, bAttach);
    DetourSetup(&CNetChan__SendSubChannelData, &CNetChan::_SendSubChannelData, bAttach);
    DetourSetup(&CNetChan__ReadSubChannelData, &CNetChan::_ReadSubChannelData, bAttach);
}
