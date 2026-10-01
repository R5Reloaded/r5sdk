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

//-----------------------------------------------------------------------------
// Purpose: gets the netchannel resend rate
// Output : float
//-----------------------------------------------------------------------------
float CNetChan::GetTotalNetworkLoss() const
{
	const uint64_t totalUpdates = m_DataFlow[FLOW_INCOMING].totalupdates;

	if (!totalUpdates && !m_nSequencesSkipped)
		return 0.0f;

	return static_cast<float>(m_nSequencesSkipped) /
		   static_cast<float>(totalUpdates + m_nSequencesSkipped);
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

bool CNetChan::_SendSubChannelData(CNetChan* pChan, bf_write* buf)
{
	return pChan->SendSubChannelData(*buf);
}

bool CNetChan::_ReadSubChannelData(CNetChan* pChan, bf_read* buf)
{
	return pChan->ReadSubChannelData(*buf);
}

//-----------------------------------------------------------------------------
// Purpose: writes pending reliable fragment data to the subchannel
// Input  : &buf -
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CNetChan::SendSubChannelData(bf_write& buf)
{
	if (m_bShuttingDown)
		return false;

	int fragmentCount = m_WaitingList.Count();

	if (!fragmentCount)
		return false;

	//-------------------------------------------------------------------------
	// Subchannel header
	//-------------------------------------------------------------------------
	buf.WriteULong(NET_RELIABLE_HEADER_MAGIC);
    buf.WriteULong(m_nSubOutFragmentsAck);

	const unsigned int fragmentAck = static_cast<u32>(m_nSubOutFragmentsAck) & NET_RELIABLE_FRAGMENT_SERIAL_MASK;
	buf.WriteUBitLong(fragmentAck, NET_RELIABLE_FRAGMENT_SERIAL_BITS);

	if (!fragmentAck) // Also used to carry the initial nonce.
	{
		const bool hasNonce = m_nSubOutFragmentsAck == 0;
		buf.WriteOneBit(hasNonce);

		if (hasNonce)
			buf.WriteULong(m_nNonceHost);
	}

	m_nSubOutSequenceNr = m_nOutSequenceNr;

	if (fragmentCount > NET_RELIABLE_FRAGMENT_SERIAL_MASK)
		fragmentCount = NET_RELIABLE_FRAGMENT_SERIAL_MASK;

	//-------------------------------------------------------------------------
	// Fragment data
	//-------------------------------------------------------------------------
	for (int i = 0; i < fragmentCount; ++i)
	{
		dataFragments_t* const fragment = m_WaitingList[i];

		// Every fragment after the first is preceded by a continuation bit
		if (i)
			buf.WriteOneBit(true);

        if (net_showfragments->GetBool())
        {
            const char* state = "";

            if (fragment->firstFragment && fragment->lastFragment)
                state = ", single";
            else if (fragment->firstFragment)
                state = ", first";
            else if (fragment->lastFragment)
                state = ", last";

            Msg(eDLL_T::ENGINE, "%s(%s): Reliable fragment #%u (%i bytes, %i/%i, seq %i%s%s)\n",
                __FUNCTION__, GetAddress(),
                static_cast<u32>(m_nSubOutFragmentsAck + i),
                static_cast<int>(fragment->size),
                i + 1,
                fragmentCount,
                m_nOutSequenceNr,
                state,
                fragment->isCompressed ? ", compressed" : "");
        }

		buf.WriteULong(static_cast<u32>(m_nSubOutFragmentsAck + i));
		buf.WriteOneBit(fragment->firstFragment);

		if (fragment->firstFragment)
		{
			// First fragment describes the complete transfer
			buf.WriteUBitLong(static_cast<u32>(fragment->transferSize),	NET_RELIABLE_TRANSFER_SIZE_BITS);
			buf.WriteOneBit(fragment->isCompressed);

			if (fragment->isCompressed)
				buf.WriteUBitLong(static_cast<u32>(fragment->uncompressedSize),	NET_RELIABLE_UNCOMPRESSED_SIZE_BITS);
		}
		else
		{
			buf.WriteOneBit(fragment->lastFragment);

            // Only last fragment can transmit its actual size.
			if (fragment->lastFragment)
				buf.WriteUBitLong(static_cast<u32>(fragment->size), NET_RELIABLE_FRAGMENT_SIZE_BITS);
		}

		const int bitPos = buf.GetNumBitsWritten();

		if (!buf.WriteBytes(fragment->buffer, static_cast<int>(fragment->size)))
		{
			buf.SeekToBit(bitPos);
			m_MessageHandler->ChannelDisconnect("#DISCONNECT_SEND_RELIABLE_OVERFLOW");

			return false;
		}

		if (!fragment->isSent)
		{
			fragment->isSent = true;
			fragment->sendSeqNr = m_nOutSequenceNr;
		}
	}

	// No more fragments follow
	buf.WriteOneBit(false);
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: reads reliable fragment data from the subchannel
// Input  : &buf -
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CNetChan::ReadSubChannelData(bf_read& buf)
{
	// security_audit(kawe): check for stream overflow more often as packet
	// truncation is possible!
#define CHECK_BUF_OVERFLOW(component) \
	if (buf.IsOverflowed()) \
	{ \
		DevWarning(eDLL_T::ENGINE, "%s(%s): Overflow while reading component \"%s\" of reliable message !!!\n", \
					__FUNCTION__, GetAddress(), component); \
		return false; \
	}

#define ERROR_AND_RETURN(reason) \
	do \
	{ \
		DevWarning(eDLL_T::ENGINE, "%s(%s): Failed to read reliable message: %s !!!\n", \
					__FUNCTION__, GetAddress(), reason); \
		return false; \
	} while (0)
	//-------------------------------------------------------------------------

	m_bInReliableState = true;

	//-------------------------------------------------------------------------
	// Subchannel header
	//-------------------------------------------------------------------------
	const u32 headerMagic = buf.ReadULong();
	const u32 remoteFragmentAck = buf.ReadULong();
	const unsigned int fragmentAck = buf.ReadUBitLong(NET_RELIABLE_FRAGMENT_SERIAL_BITS);

	CHECK_BUF_OVERFLOW("header"); // trunc!

	if (headerMagic != NET_RELIABLE_HEADER_MAGIC)
		ERROR_AND_RETURN("unrecognized header");

	// security_audit(kawe): both values originate from the same remote
	// fragment counter, so mismatching low bits indicate malformed hdr.
	if ((remoteFragmentAck & NET_RELIABLE_FRAGMENT_SERIAL_MASK) != fragmentAck)
		ERROR_AND_RETURN("fragment ack mismatch");

	// Nonce sync.
	if (!fragmentAck)
	{
		const bool hasNonce = buf.ReadOneBit() != 0;
		CHECK_BUF_OVERFLOW("hasNonce");

		// security_audit(kawe): a nonce is only sent when the full fragment
		// counter is 0; low-bit wraps must not start another nonce exchange.
		if (hasNonce != (remoteFragmentAck == 0))
			ERROR_AND_RETURN("invalid nonce state");

		if (hasNonce)
		{
			const u32 nonceRemote = buf.ReadULong();
			CHECK_BUF_OVERFLOW("nonceRemote");

			if (nonceRemote != m_nNonceRemote)
			{
				m_nNonceRemote = nonceRemote;
				m_bReceivedRemoteNonce = true;
				m_bInReliableState = false;
				m_nSubInFragments = 0;

				m_bPendingRemoteNonceAck = true;
				FreeReceiveList();
			}
		}
		else
		{
			m_bPendingRemoteNonceAck = false;
		}
	}
	else
	{
		m_bPendingRemoteNonceAck = false;
	}

	// security_audit(kawe): a resident transfer requires at least one
	// fragment accepted since the current nonce.
	if (m_bReceivedRemoteNonce && !m_nSubInFragments && m_ReceiveList.buffer)
		ERROR_AND_RETURN("invalid receive state");

	// Count retransmitted fragments. Until the remote nonce is known,
	// fragments are only consumed and discarded
	int fragmentsRemaining = INT_MAX;

	if (m_bReceivedRemoteNonce)
	{
		fragmentsRemaining = (m_nSubInFragments & NET_RELIABLE_FRAGMENT_SERIAL_MASK) - static_cast<int>(fragmentAck);

		if (fragmentsRemaining < 0)
			fragmentsRemaining += NET_RELIABLE_FRAGMENT_SERIAL_COUNT;

		// security_audit(kawe): retransmits cannot exceed accepted fragments;
		// reject before full counter reconstruction to avoid unsigned wrap.
		if (fragmentsRemaining > m_nSubInFragments)
			ERROR_AND_RETURN("invalid retransmit count");

		// security_audit(kawe): reconstruct the full fragment counter from the
		// low-bit ack and local receive state, anf require the same generation.
		const u32 expectedRemoteFragmentAck = static_cast<u32>(m_nSubInFragments) - static_cast<u32>(fragmentsRemaining);

		if (remoteFragmentAck != expectedRemoteFragmentAck)
			ERROR_AND_RETURN("fragment ack generation mismatch");
	}

	unsigned int fragmentCount = 0;

	for (;;)
	{
		// security_audit(kawe): SendSubChannelData(...) cannot place more than
		// NET_RELIABLE_FRAGMENT_SERIAL_MASK fragments in one subchannel packet.
		if (fragmentCount >= NET_RELIABLE_FRAGMENT_SERIAL_MASK)
			ERROR_AND_RETURN("fragment serial overflow");

		const u32 fragmentSerial = buf.ReadULong();
		CHECK_BUF_OVERFLOW("fragmentSerial");

		// security_audit(kawe): fragment serials must be contiguous, the expected
		// serial is derived from the ack'd fragment serial + this fragment's index
		// within subchannel payload.
		const u32 expectedFragmentSerial = remoteFragmentAck + static_cast<u32>(fragmentCount);

		if (fragmentSerial != expectedFragmentSerial)
			ERROR_AND_RETURN("fragment serial mismatch");

		++fragmentCount;

		int fragmentBytes = NET_MAX_RELIABLE_FRAGMENT_SIZE;
		const bool firstFragment = buf.ReadOneBit() != 0;

		bool isCompressed = false;
		bool lastFragment = false;
		int uncompressedSize = -1;
		int transferSize = -1;

		if (firstFragment)
		{
			// A new transfer cannot begin while the previous one is still pending.
			if (!fragmentsRemaining && m_ReceiveList.buffer)
				ERROR_AND_RETURN("new transfer while previous transfer pending");

			transferSize = static_cast<int>(buf.ReadUBitLong(NET_RELIABLE_TRANSFER_SIZE_BITS));

			// security_audit(kawe): a 0 sized transfer cannot contain a
			// fragment and would create inconsistent receive state.
			if (transferSize <= 0 || transferSize > NET_MAX_PAYLOAD)
				ERROR_AND_RETURN("invalid transfer size");

			lastFragment = transferSize <= NET_MAX_RELIABLE_FRAGMENT_SIZE;

			if (transferSize < fragmentBytes)
				fragmentBytes = transferSize;

			isCompressed = buf.ReadOneBit() != 0;

			if (isCompressed)
			{
				uncompressedSize = static_cast<int>(buf.ReadUBitLong(NET_RELIABLE_UNCOMPRESSED_SIZE_BITS));

				// security_audit(kawe): compressed size metadata is entirely
				// network controlled and is later used as an allocation size!
				if (uncompressedSize <= 0 || uncompressedSize > NET_MAX_PAYLOAD)
					ERROR_AND_RETURN("invalid uncompressed size");
			}
		}
		else
		{
			lastFragment = buf.ReadOneBit() != 0;

			if (lastFragment)
				fragmentBytes = static_cast<int>(buf.ReadUBitLong(NET_RELIABLE_FRAGMENT_SIZE_BITS));
		}

		// security_audit(kawe): the networked last fragment size is wider
		// than the maximum reliable fragment size (stack BOF).
		if (fragmentBytes <= 0 || fragmentBytes > NET_MAX_RELIABLE_FRAGMENT_SIZE)
			ERROR_AND_RETURN("invalid fragment size");

		CHECK_BUF_OVERFLOW("fragmentDescriptor");

		if (net_showfragments->GetBool())
		{
			const char* state = "";

			if (firstFragment && lastFragment)
				state = ", first, last";
			else if (firstFragment)
				state = ", first";
			else if (lastFragment)
				state = ", last";

			Msg(eDLL_T::ENGINE, "%s(%s): Reliable fragment #%u (%i bytes%s%s%s)\n",
				__FUNCTION__, GetAddress(),
				fragmentSerial,
				fragmentBytes,
				state,
				isCompressed ? ", compressed" : "",
				fragmentsRemaining ? ", retransmitted" : "");
		}

		//---------------------------------------------------------------------
		// Retransmitted fragment
		//---------------------------------------------------------------------
		if (fragmentsRemaining)
		{
			char packetBuffer[NET_MAX_RELIABLE_FRAGMENT_SIZE];
			buf.ReadBytes(packetBuffer, fragmentBytes);

			CHECK_BUF_OVERFLOW("discardedFragmentData");
			--fragmentsRemaining;
		}
		else // Begin a new transfer
		{
			// security_audit(kawe): continuation fragments are valid only for
			// an active transfer established by a preceding first fragment.
			if (!firstFragment && !m_ReceiveList.buffer)
				ERROR_AND_RETURN("continuation without active transfer");

			const int currentOffset = firstFragment ? 0 : m_ReceiveList.currentOffset;
			const int currentTransferSize = firstFragment ? transferSize : m_ReceiveList.transferSize;

			// security_audit(kawe): avoid relying on addition of networked
			// lengths when checking destination buffer bounds.
			if (currentOffset < 0 ||
				currentOffset > currentTransferSize ||
				fragmentBytes > currentTransferSize - currentOffset)
			{
				ERROR_AND_RETURN("fragment exceeds transfer bounds");
			}

			if (!firstFragment)
			{
				const int bytesRemaining = currentTransferSize - currentOffset;

				// security_audit(kawe): the sender marks the fragment as last
				// exactly when it carries all remaining transfer bytes.
				if (lastFragment)
				{
					if (fragmentBytes != bytesRemaining)
						ERROR_AND_RETURN("invalid last fragment size");
				}
				else if (bytesRemaining <= NET_MAX_RELIABLE_FRAGMENT_SIZE)
				{
					ERROR_AND_RETURN("missing last fragment marker");
				}
			}

			// security_audit(kawe): stage the fragment before modifying receive
			// state so truncation cannot leave a partial transfer or advance state.
			char packetBuffer[NET_MAX_RELIABLE_FRAGMENT_SIZE];
			buf.ReadBytes(packetBuffer, fragmentBytes);

			CHECK_BUF_OVERFLOW("fragmentData");

			if (firstFragment)
			{
				m_ReceiveList.size = transferSize;
				m_ReceiveList.transferSize = transferSize;
				m_ReceiveList.currentOffset = 0;

				m_ReceiveList.buffer = new char[static_cast<size_t>(transferSize)];

				m_ReceiveList.uncompressedSize = uncompressedSize;
				m_ReceiveList.isCompressed = isCompressed;
			}

			memcpy(&m_ReceiveList.buffer[currentOffset], packetBuffer, static_cast<size_t>(fragmentBytes));

			m_ReceiveList.currentOffset += fragmentBytes;
			++m_nSubInFragments;

			// Complete transfer.
			if (m_ReceiveList.currentOffset == m_ReceiveList.transferSize)
			{
				size_t dataSize;

				if (m_ReceiveList.isCompressed)
				{
					// security_audit(kawe): NET_BufferToBufferCompress only selects
					// compression when the compressed transfer fits within original
					// reliable payload.
					if (m_ReceiveList.size > m_ReceiveList.uncompressedSize)
						ERROR_AND_RETURN("compressed transfer larger than original");

					size_t dataSizeExpected = static_cast<size_t>(m_ReceiveList.uncompressedSize);

					const size_t allocationSize = PAD_NUMBER(dataSizeExpected, 4);
					char* const uncompressedData = new char[allocationSize];

					size_t decompressedSize = dataSizeExpected;

					if (!NET_BufferToBufferDecompress(
						reinterpret_cast<uint8_t*>(m_ReceiveList.buffer),
						static_cast<size_t>(m_ReceiveList.transferSize),
						reinterpret_cast<uint8_t*>(uncompressedData),
						&decompressedSize))
					{
						delete[] uncompressedData;
						ERROR_AND_RETURN("decompress failed");
					}

					if (decompressedSize != dataSizeExpected)
					{
						delete[] uncompressedData;
						ERROR_AND_RETURN("decompressed size mismatch");
					}

					dataSize = decompressedSize;
					delete[] m_ReceiveList.buffer;

					m_ReceiveList.buffer = uncompressedData;
					m_ReceiveList.size = dataSize;
					m_ReceiveList.isCompressed = false;
				}
				else
				{
					dataSize = m_ReceiveList.size;
				}

				bf_read messageBuffer(m_ReceiveList.buffer, static_cast<int>(dataSize));
				
				if (!ProcessMessages(&messageBuffer))
					ERROR_AND_RETURN("reliable message processing failed");

				FreeReceiveList();
			}
		}

		// Another fragment follows?
		const bool hasMoreFragments = buf.ReadOneBit() != 0;
		CHECK_BUF_OVERFLOW("hasMoreFragments");

		if (!hasMoreFragments)
			return true;
	}

#undef ERROR_AND_RETURN
#undef CHECK_BUF_OVERFLOW
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

    bool ret;
    {
        AUTO_LOCK(m_Lock);

        pStream->WriteUBitLong(msg.GetType(), NETMSG_TYPE_BITS);
        ret = msg.WriteToBuffer(pStream);
    }

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
    m_ReceiveList.size = NULL;
    m_ReceiveList.transferSize = NULL;
    if (m_ReceiveList.buffer)
    {
        delete [] m_ReceiveList.buffer;
        m_ReceiveList.buffer = nullptr;
    }
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNetChan::SetDataRate(const int rate)
{
    m_Rate = Clamp(rate, MIN_RATE, MAX_RATE);
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
	DetourSetup(&CNetChan__SendSubChannelData, &CNetChan::_SendSubChannelData, bAttach);
	DetourSetup(&CNetChan__ReadSubChannelData, &CNetChan::_ReadSubChannelData, bAttach);
}
