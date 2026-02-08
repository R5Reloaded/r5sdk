//=============================================================================//
//
// Purpose: Net system utilities
//
//=============================================================================//

#include "core/stdafx.h"
#include "engine/net.h"
#ifndef _TOOLS
#include "tier1/cvar.h"
#include "tier2/cryptutils.h"
#include "mathlib/color.h"
#include "net.h"
#include "net_chan.h"
#ifndef CLIENT_DLL
#include "server/server.h"
#include "client/client.h"
#endif // !CLIENT_DLL
#endif // !_TOOLS

#define NET_HEADER_SIZE sizeof(unsigned int)
#define MAX_ROUTABLE_PAYLOAD 1200
#define MIN_USER_MAXROUTABLE_SIZE 576
#define MAX_USER_MAXROUTABLE_SIZE MAX_ROUTABLE_PAYLOAD
#define NET_COMPRESSION_STACKBUF_SIZE 4096

#define NET_HEADER_FLAG_SPLITPACKET -2
#define NET_HEADER_FLAG_COMPRESSEDPACKET -3
#define NET_HEADER_FLAG_ZSTDCOMPRESSEDPACKET -4

#ifndef _TOOLS
typedef CUtlMemoryFixedGrowable<uint8_t, NET_COMPRESSION_STACKBUF_SIZE> CUtlMemoryNetCompressionBuffer;

enum NetPacketCompressionMethod_e
{
    LZSS = 0,
};

static void NET_GetKey_f()
{
	NET_PrintKey();
}
static void NET_SetKey_f(const CCommand& args)
{
	if (args.ArgC() < 2)
	{
		return;
	}

	NET_SetKey(args.Arg(1));
}
static void NET_GenerateKey_f()
{
	NET_GenerateKey();
}

void NET_UseRandomKeyChanged_f(IConVar* pConVar, const char* pOldString, float flOldValue, ChangeUserData_t pUserData)
{
	if (ConVar* pConVarRef = g_pCVar->FindVar(pConVar->GetName()))
	{
		if (strcmp(pOldString, pConVarRef->GetString()) == NULL)
			return; // Same value.

		if (pConVarRef->GetBool())
			NET_GenerateKey();
		else
			NET_SetKey(DEFAULT_NET_ENCRYPTION_KEY);
	}
}

ConVar net_useRandomKey("net_useRandomKey", "1", FCVAR_RELEASE, "Use random AES encryption key for game packets.", false, 0.f, false, 0.f, &NET_UseRandomKeyChanged_f, nullptr);

static ConVar net_tracePayload("net_tracePayload", "0", FCVAR_DEVELOPMENTONLY, "Log the payload of the send/recv datagram to a file on the disk.");
static ConVar net_encryptionEnable("net_encryptionEnable", "1", FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED, "Use AES encryption on game packets.");
static ConVar net_compression_debug("net_compression_debug", "0", FCVAR_DEVELOPMENTONLY);
static ConVar net_maxRecvCall("net_maxRecvCall", "1000", FCVAR_DEVELOPMENTONLY);
static ConVar net_maxDatagramReceiveAttempts("net_maxDatagramReceiveAttempts", "1000", FCVAR_DEVELOPMENTONLY);

static ConCommand net_getkey("net_getkey", NET_GetKey_f, "Gets the installed base64 net key", FCVAR_RELEASE);
static ConCommand net_setkey("net_setkey", NET_SetKey_f, "Sets user specified base64 net key", FCVAR_RELEASE);
static ConCommand net_generatekey("net_generatekey", NET_GenerateKey_f, "Generates and sets a random base64 net key", FCVAR_RELEASE);

static bool NET_IsCompressedPacket(const uint8_t* const pPacketData, const unsigned int nPacketLen)
{
    if (nPacketLen > NET_HEADER_SIZE)
    {
        const int netHeader = *(int*)pPacketData;
        if (netHeader != NET_HEADER_FLAG_COMPRESSEDPACKET && netHeader != NET_HEADER_FLAG_ZSTDCOMPRESSEDPACKET)
            return false;
        else
            return true;
    }

    return false;
}

static bool NET_DecompressPacket(const uint8_t* const pPacketData, const unsigned int nPacketLen, 
    CUtlMemoryNetCompressionBuffer& decompressionBuffer, unsigned int nMaxDecompressedSize, unsigned int& nDecompressedSize)
{
    //Do we actually have enough bytes to decompress
    if (nPacketLen <= NET_HEADER_SIZE)
        return false;

    const uint8_t* const pCompressedData = pPacketData + NET_HEADER_SIZE;
    const int CompressionTypeFlag = *reinterpret_cast<const int*>(pPacketData);

    //const unsigned int nCompressedDataLen = nPacketLen - NET_HEADER_SIZE;

    switch (CompressionTypeFlag)
    {
    case NET_HEADER_FLAG_COMPRESSEDPACKET:
    {
        CLZSS compressor;
        if (!compressor.IsCompressed(pCompressedData))
            return false;

        const unsigned int nExpectedDecompressedSize = compressor.GetActualSize(pCompressedData);

        if (nExpectedDecompressedSize > nMaxDecompressedSize)
            return false;

        decompressionBuffer.EnsureCapacity(nExpectedDecompressedSize);

        const unsigned int nDecompressedBytes = compressor.SafeUncompress(pCompressedData, decompressionBuffer.Base(), nExpectedDecompressedSize);

        if (!nDecompressedBytes || nDecompressedBytes != nExpectedDecompressedSize)
            return false;

        nDecompressedSize = nDecompressedBytes;
        return true;
    }
    default:
        return false;
    }
}

inline static bool NET_IsSplitPacket(const uint8_t* const pData)
{
    return *reinterpret_cast<const int*>(pData) == NET_HEADER_FLAG_SPLITPACKET;
}

struct NetEncryptionHeader_t
{
    char m_IV[12];
    char m_TAG[16];
    const char* const GetData() const { return reinterpret_cast<const char* const>(this + 1); };
    char* const       GetData() { return reinterpret_cast<char* const>(this + 1); };
};
static_assert(sizeof(NetEncryptionHeader_t) == 28);

int NET_DecryptPacket(netkey_t* const pNetKey, const uint8_t* const pInputBuffer, const int nInputBufferLen, uint8_t* const pOutputBuffer, const int nOutputBufferLength)
{
    //Cant do anything with no net key
    if (!pNetKey)
        return -1;

    //Are we actually gonna have data after our header
    if (nInputBufferLen <= sizeof(NetEncryptionHeader_t))
        return -1;

    const int nDataSize = nInputBufferLen - sizeof(NetEncryptionHeader_t);

    if (nOutputBufferLength < nDataSize)
        return -1;

    const NetEncryptionHeader_t* pEncHdr = reinterpret_cast<const NetEncryptionHeader_t*>(pInputBuffer);
    return v_NET_DecryptPacket(pNetKey, pEncHdr->GetData(), nDataSize, pEncHdr->m_IV, nullptr, nullptr, nullptr, pEncHdr->m_TAG, nullptr, pOutputBuffer);
}

//-----------------------------------------------------------------------------
// Purpose: Attempts to recieve data from a socket up to net_maxRecvCall times
// Input  : *pPacket - 
//			*pSocketRecieveBuffer - 
//			nSocketRecieveBufferLen - 
//          &nBytesRecieved -
// Output : true on packet read, false on no packet read
//-----------------------------------------------------------------------------
static bool NET_TryRecieveRawDatagram(netpacket_t* const pPacket, uint8_t* const pSocketRecieveBuffer, const int nSocketRecieveBufferLen, int& nBytesRecieved)
{
    int nRecvAttempt = 0;
    const int nMaxRecvAttempts = net_maxRecvCall.GetInt();

    netsocket_t& socket = g_pNetSockets->Element(pPacket->source);
    sockaddr_storage recvAddr;

    for (;;)
    {
        socket.nLastUsed = (float)*g_pNetTime;
        int sockAddrSize = sizeof(recvAddr);
        const int res = recvfrom(socket.hUDP, reinterpret_cast<char* const>(pSocketRecieveBuffer), nSocketRecieveBufferLen, 0, reinterpret_cast<sockaddr*>(&recvAddr), &sockAddrSize);

        if (res > 0) //Has data
        {
            nBytesRecieved = res;
            break;
        }
        else if (res == 0) //Connection closed
        {
            return false;
        }
        else //Error
        {
            const int error = NET_GetLastError();
            if (error == WSAEWOULDBLOCK || error == WSAEMSGSIZE)
                return false;
        }

        if (nRecvAttempt++ >= nMaxRecvAttempts)
            return false;
    }

    if (recvAddr.ss_family == AF_INET6 || recvAddr.ss_family == AF_INET)
        pPacket->from.SetFromSockadr(&recvAddr);
    else
        pPacket->from.Clear();

    return true;
}

//-----------------------------------------------------------------------------
// Purpose: Handles packet data setup from a raw data buffer
// Input  : iSocket - 
//			*pRawDataBuffer - 
//			nRawDataLen - 
//          *pPacket -
//          bEncrypted -
// Output : true on packet successfully created, false on failure
//-----------------------------------------------------------------------------
static bool NET_ProcessRawRecievedPacket(const int iSocket, const uint8_t* const pRawDataBuffer, const int nRawDataLen, netpacket_s* pPacket, bool bEncrypted)
{
    unsigned int nDecompressedSize = 0;
    CUtlMemoryNetCompressionBuffer decompressionBuffer;
    int nDataSize = nRawDataLen;
    pPacket->wiresize = nRawDataLen;

    if(bEncrypted)
    {
        netkey_t* const pNetKey = v_NET_GetKeyForAdr(&pPacket->from);
        const int res = NET_DecryptPacket(pNetKey, pRawDataBuffer, nRawDataLen, pPacket->pData, NET_MAX_MESSAGE);

        if (res < 0)
            return false;

        nDataSize = res;
    }

    pPacket->size = nDataSize;

    //If there is no chance we can have a header we are done with this packet
    if (nDataSize <= NET_HEADER_SIZE)
        return true;

    //If the packet is split and we havent got the long packet yet we need to loop again to get more data
    if (NET_IsSplitPacket(pPacket->pData) && !v_NET_GetLongPacket(iSocket, pPacket))
        return false;

    //If the packet isnt compressed we are done
    if (!NET_IsCompressedPacket(pPacket->pData, pPacket->size))
        return true;

    //TODO: The decompression buffer should be allocated in here to support decompression algs that can do decompression on overlapped buffers
    if (!NET_DecompressPacket(pPacket->pData, pPacket->size, decompressionBuffer, NET_MAX_PAYLOAD, nDecompressedSize))
        return false;

    pPacket->size = nDecompressedSize;
    memcpy(pPacket->pData, decompressionBuffer.Base(), nDecompressedSize);
    return true;
}

//-----------------------------------------------------------------------------
// Purpose: Forms a completed net packet from data on a raw socket
// Input  : iSocket - 
//			*pInpacket - 
//			bEncrypted - 
// Output : true on success, false on no data on socket
//-----------------------------------------------------------------------------
bool NET_ReceiveDatagram(int iSocket, netpacket_s* pInpacket, bool bEncrypted)
{
    //This buffer should never be larger than NET_MAX_MESSAGE or it will exceed the size of the allocated scratch buffer
    //which would make copying this buffer into the scratch buffer unsafe
    //This exact size is probably based off MAX_ROUTABLE_PAYLOAD with some extra padding im not sure of
    uint8_t sockReadBuff[1264];
    static_assert(sizeof(sockReadBuff) <= NET_MAX_MESSAGE);

    const int nMaxAttempts = net_maxDatagramReceiveAttempts.GetInt();

    for (int iAttempt = 0; iAttempt < nMaxAttempts; iAttempt++)
    {
        int nBytesRecieved = 0;
        if (!NET_TryRecieveRawDatagram(pInpacket, sockReadBuff, sizeof(sockReadBuff), nBytesRecieved))
            return false;

        //Did we successfully decode a packet, if yes we are done, if no we will keep looping till we have no data in the socket or we get a good packet
        if (NET_ProcessRawRecievedPacket(iSocket, (const uint8_t*)sockReadBuff, nBytesRecieved, pInpacket, bEncrypted))
            return true;
    }

    return false;
}

//-----------------------------------------------------------------------------
// Purpose: hook and log the send datagram
// Input  : s - 
//			*pPayload - 
//			iLenght - 
//			*pAdr - 
//			bEncrypt - 
// Output : outgoing sequence number for this packet
//-----------------------------------------------------------------------------
int NET_SendDatagram(SOCKET s, void* pPayload, int iLenght, netadr_t* pAdr, bool bEncrypt)
{
	const bool encryptPacket = (bEncrypt && net_encryptionEnable.GetBool());
	const int result = v_NET_SendDatagram(s, pPayload, iLenght, pAdr, encryptPacket);

	if (result && net_tracePayload.GetBool())
	{
		// Log transmitted packet data.
		HexDump("[+] NET_SendDatagram ", "net_trace", pPayload, size_t(iLenght));
	}

	return result;
}

constexpr NetPacketCompressionMethod_e compressionMethod = LZSS;

static bool NET_CompressPacket(const uint8_t* pRawData, unsigned int nLen, CUtlMemoryNetCompressionBuffer& compressionBuffer, unsigned int& nCompressedSize)
{
    //Make sure we have at a minimum enough data for the uncompressed data and the header
    compressionBuffer.EnsureCapacity(nLen + NET_HEADER_SIZE);

    const bool           bCompressionDebug = net_compression_debug.GetBool();

    int* const           pCompressionHeader = reinterpret_cast<int*>(compressionBuffer.Base());
    unsigned char* const pCompressionBuffer = reinterpret_cast<unsigned char* const>(compressionBuffer.Base() + NET_HEADER_SIZE);

    switch (compressionMethod)
    {
    case LZSS:
    {
        *pCompressionHeader = LittleLong(NET_HEADER_FLAG_COMPRESSEDPACKET);

        CLZSS compressor;
        const uint8_t* const pCompressedBytes = compressor.CompressNoAlloc(pRawData, nLen, 
            pCompressionBuffer, &nCompressedSize);

        if (!pCompressedBytes)
        {
            if (bCompressionDebug)
                DevMsg(eDLL_T::ENGINE,"Packet failed to compress with lzss\n");
            return false;
        }
        
        if(bCompressionDebug)
        {
            DevMsg(eDLL_T::ENGINE, 
                "Packet compressed with LZSS, original size '%lu' bytes, compressed size '%lu' bytes\n", 
                nLen, nCompressedSize);
        }

        nCompressedSize += NET_HEADER_SIZE;
        return true;
    }
    default:
        Assert("Invalid compression mode");
        return false;
    }
}

static void NET_SendLoopPacket(const int iSocket, const unsigned int nLen, const uint8_t* const pData)
{
    if (nLen > NET_MAX_PAYLOAD)
        return;

    if (iSocket != NS_CLIENT && iSocket != NS_SERVER)
        return;

    loopback_t* const loop = static_cast<loopback_t*>(loopback_t::s_pAllocator->Alloc());
    
    if (nLen > sizeof(loopback_t::m_FixedBuffer))
        loop->m_pData = new char[nLen];
    else
        loop->m_pData = loop->m_FixedBuffer;

    loop->m_nDataLen = nLen;
    memcpy(loop->m_pData, pData, nLen);

    g_pLoopBacks[!iSocket].PushItem(loop);
}

int NET_SendPacket(CNetChan* pChan, int iSocket, const netadr_t& toAdr, const uint8_t* pData, unsigned int nLen, void* unused0, bool bCompress, void* unused1, bool bEncrypt)
{
    if (toAdr.IsLoopback() || (toAdr.IsLocalhost() && !net_usesocketsforloopback->GetBool()))
    {
        NET_SendLoopPacket(iSocket, nLen, pData);
        return static_cast<int>(nLen);
    }

    if (toAdr.GetType() != netadrtype_t::NA_IP)
        return nLen;

    const netsocket_t& netsocket = g_pNetSockets->Element(iSocket);
    const int hUDP = netsocket.hUDP;

    if (!hUDP)
        return nLen;

    if (iSocket == NS_CLIENT && net_droppackets->GetInt() < 0)
    {
        net_droppackets->SetValue(net_droppackets->GetInt() + 1);
        return nLen;
    }

    const uint8_t* pDataToSend = pData;
    unsigned int nBytesToSend = nLen;
    CUtlMemoryNetCompressionBuffer compressionBuffer;

    if (bCompress)
    {
        unsigned int nCompressedSize = 0;
        if (NET_CompressPacket(pData, nLen, compressionBuffer, nCompressedSize))
        {
            pDataToSend = compressionBuffer.Base();
            nBytesToSend = nCompressedSize;
        }
    }

    //On the server always clamp the max routable to whatever the sv var is set to
#ifndef CLIENT_DLL
    const unsigned int nMaxRoutable = pChan
        ? clamp(pChan->GetMaxRoutablePayloadSize(), MIN_USER_MAXROUTABLE_SIZE, min(sv_maxroutable->GetInt(), MAX_USER_MAXROUTABLE_SIZE))
        : MAX_USER_MAXROUTABLE_SIZE;
#else
    const unsigned int nMaxRoutable = pChan
        ? clamp(pChan->GetMaxRoutablePayloadSize(), MIN_USER_MAXROUTABLE_SIZE, MAX_USER_MAXROUTABLE_SIZE)
        : MAX_USER_MAXROUTABLE_SIZE;
#endif

    int nBytesProcessed = 0;

    if (nBytesToSend > nMaxRoutable || (net_queued_packet_thread->GetInt() == 0x8DEB8 && pChan))
        nBytesProcessed = v_NET_SendLong(pChan, iSocket, hUDP, pDataToSend, nBytesToSend, &toAdr, nMaxRoutable, bEncrypt);
    else
        nBytesProcessed = v_NET_SendTo_Async(nullptr, hUDP, pDataToSend, nBytesToSend, &toAdr, bEncrypt);
    
    if (nBytesProcessed == -1)
    {
        const int err = NET_GetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAECONNRESET)
            return 0;
    }

    return nBytesToSend;
}

//-----------------------------------------------------------------------------
// Purpose: compresses the input buffer into the output buffer
// Input  : *dest - 
//			*destLen - 
//			*source - 
//			sourceLen - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool NET_BufferToBufferCompress(uint8_t* const dest, size_t* const destLen, uint8_t* const source, const size_t sourceLen)
{
	CLZSS lzss;
	uint32_t compLen = (uint32_t)sourceLen;

	if (!lzss.CompressNoAlloc(source, (uint32_t)sourceLen, dest, &compLen))
	{
		memcpy(dest, source, sourceLen);

		*destLen = sourceLen;
		return false;
	}

	*destLen = compLen;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: decompresses the input buffer into the output buffer
// Input  : *source - 
//			&sourceLen - 
//			*dest - 
//			destLen - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
unsigned int NET_BufferToBufferDecompress(uint8_t* const source, size_t& sourceLen, uint8_t* const dest, const size_t destLen)
{
	Assert(source);
	Assert(sourceLen);

	CLZSS lzss;

	if (lzss.IsCompressed(source))
	{
		return lzss.SafeUncompress(source, dest, (unsigned int)destLen);
	}

	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: safely decompresses the input buffer into the output buffer
// Input  : *lzss - 
//			*pInput - 
//			*pOutput - 
//			unBufSize - 
// Output : total decompressed bytes
//-----------------------------------------------------------------------------
unsigned int NET_BufferToBufferDecompress_LZSS(CLZSS* lzss, unsigned char* pInput, unsigned char* pOutput, unsigned int unBufSize)
{
	return lzss->SafeUncompress(pInput, pOutput, unBufSize);
}

//-----------------------------------------------------------------------------
// Purpose: configures the network system
//-----------------------------------------------------------------------------
void NET_Config()
{
	v_NET_Config();
	g_pNetAdr->SetPort(htons(u_short(hostport->GetInt())));
}

//-----------------------------------------------------------------------------
// Purpose: prints the currently installed encryption key
//-----------------------------------------------------------------------------
void NET_PrintKey()
{
	Msg(eDLL_T::ENGINE, "Installed NetKey: %s'%s%s%s'\n",
		g_svReset.c_str(), g_svGreyB.c_str(), g_pNetKey->GetBase64NetKey(), g_svReset.c_str());
}

//-----------------------------------------------------------------------------
// Purpose: sets the user specified encryption key
// Input  : svNetKey - 
//-----------------------------------------------------------------------------
void NET_SetKey(const string& svNetKey)
{
	string svTokenizedKey;

	if (svNetKey.size() == AES_128_B64_ENCODED_SIZE &&
		IsValidBase64(svNetKey, &svTokenizedKey)) // Results are tokenized by 'IsValidBase64()'.
	{
		v_NET_SetKey(g_pNetKey, svTokenizedKey.c_str());
		NET_PrintKey();
	}
	else
	{
		Error(eDLL_T::ENGINE, NO_ERROR, "AES-128 key not encoded or invalid\n");
	}
}

//-----------------------------------------------------------------------------
// Purpose: calculates and sets the encryption key
//-----------------------------------------------------------------------------
void NET_GenerateKey()
{
	if (!net_useRandomKey.GetBool())
	{
		net_useRandomKey.SetValue(1);
		return; // Change callback will handle this.
	}

	uint8_t keyBuf[AES_128_KEY_SIZE];
	const char* errorMsg = nullptr;

	if (!Plat_GenerateRandom(keyBuf, sizeof(keyBuf), errorMsg))
	{
		Error(eDLL_T::ENGINE, NO_ERROR, "%s\n", errorMsg);
		return;
	}

	NET_SetKey(Base64Encode(string(reinterpret_cast<char*>(&keyBuf), AES_128_KEY_SIZE)));
}

//-----------------------------------------------------------------------------
// Purpose: hook and log the client's signonstate to the console
// Input  : *fmt - 
//			... - 
//-----------------------------------------------------------------------------
void NET_PrintFunc(const char* fmt, ...)
{
#ifndef DEDICATED
	const static eDLL_T context = eDLL_T::CLIENT;
#else // !DEDICATED
	const static eDLL_T context = eDLL_T::SERVER;
#endif

	string result;

	va_list args;
	va_start(args, fmt);
	result = FormatV(fmt, args);
	va_end(args);

	Msg(context, result.back() == '\n' ? "%s" : "%s\n", result.c_str());
}

//-----------------------------------------------------------------------------
// Purpose: disconnect the client and shutdown netchannel
// Input  : *pClient - 
//			nIndex - 
//			*szReason - 
//			bBadRep - 
//			bRemoveNow - 
//-----------------------------------------------------------------------------
void NET_RemoveChannel(CClient* pClient, int nIndex, const char* szReason, uint8_t bBadRep, bool bRemoveNow)
{
#ifndef CLIENT_DLL
	if (!pClient || std::strlen(szReason) == NULL || !pClient->GetNetChan())
	{
		return;
	}

	pClient->GetNetChan()->Shutdown(szReason, bBadRep, bRemoveNow); // Shutdown NetChannel.
	pClient->Clear();                                               // Reset CClient slot.
#endif // !CLIENT_DLL
}

//-----------------------------------------------------------------------------
// Purpose: reads the net message type from buffer
// Input  : &outType - 
//			&buffer - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool NET_ReadMessageType(int* outType, bf_read* buffer)
{
	*outType = buffer->ReadUBitLong(NETMSG_TYPE_BITS);
	return !buffer->IsOverflowed();
}

//-----------------------------------------------------------------------------
// Purpose: checks whether the provided address is the local server.
// Input  : &netAdr - 
// Output : true if equal, false otherwise
//-----------------------------------------------------------------------------
bool NET_IsRemoteLocal(const CNetAdr& netAdr)
{
	return (g_pNetAdr->ComparePort(netAdr) && g_pNetAdr->CompareAdr(netAdr));
}

int NET_GetLastError()
{
    const int error = WSAGetLastError();
    *g_pNetError = error;
    return error;
}

#endif // !_TOOLS

//-----------------------------------------------------------------------------
// Purpose: returns the WSA error code
//-----------------------------------------------------------------------------
const char* NET_ErrorString(int iCode)
{
	switch (iCode)
	{
		case WSAEINTR                   : return "WSAEINTR";
		case WSAEBADF                   : return "WSAEBADF";
		case WSAEACCES                  : return "WSAEACCES";
		case WSAEFAULT                  : return "WSAEFAULT";
		case WSAEINVAL                  : return "WSAEINVAL";
		case WSAEMFILE                  : return "WSAEMFILE";
		case WSAEWOULDBLOCK             : return "WSAEWOULDBLOCK";
		case WSAEINPROGRESS             : return "WSAEINPROGRESS";
		case WSAEALREADY                : return "WSAEALREADY";
		case WSAENOTSOCK                : return "WSAENOTSOCK";
		case WSAEDESTADDRREQ            : return "WSAEDESTADDRREQ";
		case WSAEMSGSIZE                : return "WSAEMSGSIZE";
		case WSAEPROTOTYPE              : return "WSAEPROTOTYPE";
		case WSAENOPROTOOPT             : return "WSAENOPROTOOPT";
		case WSAEPROTONOSUPPORT         : return "WSAEPROTONOSUPPORT";
		case WSAESOCKTNOSUPPORT         : return "WSAESOCKTNOSUPPORT";
		case WSAEOPNOTSUPP              : return "WSAEOPNOTSUPP";
		case WSAEPFNOSUPPORT            : return "WSAEPFNOSUPPORT";
		case WSAEAFNOSUPPORT            : return "WSAEAFNOSUPPORT";
		case WSAEADDRINUSE              : return "WSAEADDRINUSE";
		case WSAEADDRNOTAVAIL           : return "WSAEADDRNOTAVAIL";
		case WSAENETDOWN                : return "WSAENETDOWN";
		case WSAENETUNREACH             : return "WSAENETUNREACH";
		case WSAENETRESET               : return "WSAENETRESET";
		case WSAECONNABORTED            : return "WSAECONNABORTED";
		case WSAECONNRESET              : return "WSAECONNRESET";
		case WSAENOBUFS                 : return "WSAENOBUFS";
		case WSAEISCONN                 : return "WSAEISCONN";
		case WSAENOTCONN                : return "WSAENOTCONN";
		case WSAESHUTDOWN               : return "WSAESHUTDOWN";
		case WSAETOOMANYREFS            : return "WSAETOOMANYREFS";
		case WSAETIMEDOUT               : return "WSAETIMEDOUT";
		case WSAECONNREFUSED            : return "WSAECONNREFUSED";
		case WSAELOOP                   : return "WSAELOOP";
		case WSAENAMETOOLONG            : return "WSAENAMETOOLONG";
		case WSAEHOSTDOWN               : return "WSAEHOSTDOWN";
		case WSAEHOSTUNREACH            : return "WSAEHOSTUNREACH";
		case WSAENOTEMPTY               : return "WSAENOTEMPTY";
		case WSAEPROCLIM                : return "WSAEPROCLIM";
		case WSAEUSERS                  : return "WSAEUSERS";
		case WSAEDQUOT                  : return "WSAEDQUOT";
		case WSAESTALE                  : return "WSAESTALE";
		case WSAEREMOTE                 : return "WSAEREMOTE";
		case WSASYSNOTREADY             : return "WSASYSNOTREADY";
		case WSAVERNOTSUPPORTED         : return "WSAVERNOTSUPPORTED";
		case WSANOTINITIALISED          : return "WSANOTINITIALISED";
		case WSAEDISCON                 : return "WSAEDISCON";
		case WSAENOMORE                 : return "WSAENOMORE";
		case WSAECANCELLED              : return "WSAECANCELLED";
		case WSAEINVALIDPROCTABLE       : return "WSAEINVALIDPROCTABLE";
		case WSAEINVALIDPROVIDER        : return "WSAEINVALIDPROVIDER";
		case WSAEPROVIDERFAILEDINIT     : return "WSAEPROVIDERFAILEDINIT";
		case WSASYSCALLFAILURE          : return "WSASYSCALLFAILURE";
		case WSASERVICE_NOT_FOUND       : return "WSASERVICE_NOT_FOUND";
		case WSATYPE_NOT_FOUND          : return "WSATYPE_NOT_FOUND";
		case WSA_E_NO_MORE              : return "WSA_E_NO_MORE";
		case WSA_E_CANCELLED            : return "WSA_E_CANCELLED";
		case WSAEREFUSED                : return "WSAEREFUSED";
		case WSAHOST_NOT_FOUND          : return "WSAHOST_NOT_FOUND";
		case WSATRY_AGAIN               : return "WSATRY_AGAIN";
		case WSANO_RECOVERY             : return "WSANO_RECOVERY";
		case WSANO_DATA                 : return "WSANO_DATA";
		case WSA_QOS_RECEIVERS          : return "WSA_QOS_RECEIVERS";
		case WSA_QOS_SENDERS            : return "WSA_QOS_SENDERS";
		case WSA_QOS_NO_SENDERS         : return "WSA_QOS_NO_SENDERS";
		case WSA_QOS_NO_RECEIVERS       : return "WSA_QOS_NO_RECEIVERS";
		case WSA_QOS_REQUEST_CONFIRMED  : return "WSA_QOS_REQUEST_CONFIRMED";
		case WSA_QOS_ADMISSION_FAILURE  : return "WSA_QOS_ADMISSION_FAILURE";
		case WSA_QOS_POLICY_FAILURE     : return "WSA_QOS_POLICY_FAILURE";
		case WSA_QOS_BAD_STYLE          : return "WSA_QOS_BAD_STYLE";
		case WSA_QOS_BAD_OBJECT         : return "WSA_QOS_BAD_OBJECT";
		case WSA_QOS_TRAFFIC_CTRL_ERROR : return "WSA_QOS_TRAFFIC_CTRL_ERROR";
		case WSA_QOS_GENERIC_ERROR      : return "WSA_QOS_GENERIC_ERROR";
		case WSA_QOS_ESERVICETYPE       : return "WSA_QOS_ESERVICETYPE";
		case WSA_QOS_EFLOWSPEC          : return "WSA_QOS_EFLOWSPEC";
		case WSA_QOS_EPROVSPECBUF       : return "WSA_QOS_EPROVSPECBUF";
		case WSA_QOS_EFILTERSTYLE       : return "WSA_QOS_EFILTERSTYLE";
		case WSA_QOS_EFILTERTYPE        : return "WSA_QOS_EFILTERTYPE";
		case WSA_QOS_EFILTERCOUNT       : return "WSA_QOS_EFILTERCOUNT";
		case WSA_QOS_EOBJLENGTH         : return "WSA_QOS_EOBJLENGTH";
		case WSA_QOS_EFLOWCOUNT         : return "WSA_QOS_EFLOWCOUNT";
		case WSA_QOS_EUNKOWNPSOBJ       : return "WSA_QOS_EUNKNOWNPSOBJ";
		case WSA_QOS_EPOLICYOBJ         : return "WSA_QOS_EPOLICYOBJ";
		case WSA_QOS_EFLOWDESC          : return "WSA_QOS_EFLOWDESC";
		case WSA_QOS_EPSFLOWSPEC        : return "WSA_QOS_EPSFLOWSPEC";
		case WSA_QOS_EPSFILTERSPEC      : return "WSA_QOS_EPSFILTERSPEC";
		case WSA_QOS_ESDMODEOBJ         : return "WSA_QOS_ESDMODEOBJ";
		case WSA_QOS_ESHAPERATEOBJ      : return "WSA_QOS_ESHAPERATEOBJ";
		case WSA_QOS_RESERVED_PETYPE    : return "WSA_QOS_RESERVED_PETYPE";
		case WSA_SECURE_HOST_NOT_FOUND  : return "WSA_SECURE_HOST_NOT_FOUND";
		case WSA_IPSEC_NAME_POLICY_ERROR: return "WSA_IPSEC_NAME_POLICY_ERROR";
	default                    : return "UNKNOWN_ERROR";
	}
}

#ifndef _TOOLS
///////////////////////////////////////////////////////////////////////////////
void VNet::Detour(const bool bAttach) const
{
	DetourSetup(&v_NET_Config, &NET_Config, bAttach);
	DetourSetup(&v_NET_ReceiveDatagram, &NET_ReceiveDatagram, bAttach);
	DetourSetup(&v_NET_SendDatagram, &NET_SendDatagram, bAttach);

	DetourSetup(&v_NET_BufferToBufferCompress, &NET_BufferToBufferCompress, bAttach);
	DetourSetup(&v_NET_BufferToBufferDecompress_LZSS, &NET_BufferToBufferDecompress_LZSS, bAttach);
	DetourSetup(&v_NET_PrintFunc, &NET_PrintFunc, bAttach);

    DetourSetup(&v_NET_SendPacket, &NET_SendPacket, bAttach);
}

///////////////////////////////////////////////////////////////////////////////
netadr_t* g_pNetAdr = nullptr;
netkey_t* g_pNetKey = nullptr;

double* g_pNetTime = nullptr;
#endif // !_TOOLS
