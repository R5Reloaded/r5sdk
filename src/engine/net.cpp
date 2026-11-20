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
#include "net_chan.h"
#ifndef CLIENT_DLL
#include "server/server.h"
#include "client/client.h"
#endif // !CLIENT_DLL
#endif // !_TOOLS

#ifndef _TOOLS
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

static ConCommand net_getkey("net_getkey", NET_GetKey_f, "Gets the installed base64 net key", FCVAR_RELEASE);
static ConCommand net_setkey("net_setkey", NET_SetKey_f, "Sets user specified base64 net key", FCVAR_RELEASE);
static ConCommand net_generatekey("net_generatekey", NET_GenerateKey_f, "Generates and sets a random base64 net key", FCVAR_RELEASE);

//-----------------------------------------------------------------------------
// Purpose: hook and log the receive datagram
// Input  : iSocket - 
//			*pInpacket - 
//			bEncrypted - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool NET_ReceiveDatagram(int iSocket, netpacket_s* pInpacket, bool bEncrypted)
{
	const bool decryptPacket = (bEncrypted && net_encryptionEnable.GetBool());
	const bool result = v_NET_ReceiveDatagram(iSocket, pInpacket, decryptPacket);

	if (result && net_tracePayload.GetBool())
	{
		// Log received packet data.
		HexDump("[+] NET_ReceiveDatagram ", "net_trace",
			pInpacket->pData, size_t(pInpacket->wiresize));
	}

	return result;
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
		DevWarning(eDLL_T::ENGINE, "NET_BufferToBufferCompress: failed to compress data! ( input %p[%zu], output %p[%zu] )\n", source, sourceLen, dest, destLen);
		memcpy(dest, source, sourceLen);

		*destLen = sourceLen;
		return false;
	}

	*destLen = compLen;
	return true;
}

//-----------------------------------------------------------------------------
static unsigned int NET_GetUncompressedSize(const void* compressed, const size_t compressedLen)
{
	if (compressedLen < sizeof(lzss_header_t))
		return 0;

	const lzss_header_t* const header = (const lzss_header_t*)compressed;

	if (header->id != LZSS_ID)
		return 0;

	return LittleLong(header->actualSize);
}

//-----------------------------------------------------------------------------
// Purpose: decompresses the input buffer into the output buffer
// Input  : *source - 
//			sourceLen - 
//			*dest - 
//			*destLen - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool NET_BufferToBufferDecompress(uint8_t* const source, const size_t sourceLen, uint8_t* const dest, size_t* const destLen)
{
	const unsigned int decompressedSize = NET_GetUncompressedSize(source, sourceLen);

	if (!decompressedSize)
	{
		DevWarning(eDLL_T::ENGINE, "NET_BufferToBufferDecompress: failed to get decompress size from header!\n");
		return false;
	}

	if (decompressedSize > *destLen)
	{
		DevWarning(eDLL_T::ENGINE, "NET_BufferToBufferDecompress: improperly sized destination buffer! ( %u in, %u needed )\n", (unsigned int*)*destLen, decompressedSize);
		return false;
	}

	CLZSS lzss;
	const unsigned int actualSize = lzss.SafeUncompress(source, (unsigned int)sourceLen, dest, (unsigned int)*destLen);

	if (actualSize != decompressedSize)
	{
		DevWarning(eDLL_T::ENGINE, "NET_BufferToBufferDecompress: improperly sized decompressed data! ( %u expected, %u actual )\n", decompressedSize, actualSize);
		return false;
	}

	*destLen = actualSize;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: safely decompresses the input buffer into the output buffer
// Input  : *lzss - 
//			*pInput - 
//			*pOutput - 
//			unBufSize - 
// Output : total decompressed bytes
//-----------------------------------------------------------------------------
static unsigned int NET_BufferToBufferDecompress_OnlyUsedByRecvDatagram_TEMP(CLZSS* lzss, unsigned char* pInput, unsigned char* pOutput, unsigned int unBufSize)
{
	// todo(kawe): this is now only used by NET_ReceiveDatagram() internally,
	// and since we know the stack size (4096) I hardcoded it here. Remove
	// this entirely once that function is rebuilt in the SDK. For now that
	// function is now perfectly and secure after this input size patch, so
	// no priority to rework that. REMOVE this hook in its full once rebuilt!
	return lzss->SafeUncompress(pInput, 4096, pOutput, unBufSize);
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
// Purpose: checks whether the provided address is the local host.
// Input  : &netAdr - 
// Output : true if local, false otherwise
//-----------------------------------------------------------------------------
bool NET_IsLocalHost(const CNetAdr& netAdr)
{
	return v_NET_IsLocalHost(&netAdr);
}

typedef struct
{
	int		currentSequence;
	int		splitCount;
	int		totalSize;
	int		nExpectedSplitSize;
	char	buffer[NET_MAX_MESSAGE];	// This has to be big enough to hold the largest message
} LONGPACKET;

// Use this to pick apart the network stream, must be packed
#pragma pack(1)
typedef struct
{
	int		netID;
	int		sequenceNumber;
	int		packetID : 16;
	int		nSplitSize : 16;
} SPLITPACKET;
#pragma pack()

#define MIN_USER_MAXROUTABLE_SIZE	576  // ( X.25 Networks )
#define MAX_USER_MAXROUTABLE_SIZE	MAX_ROUTABLE_PAYLOAD

#define MIN_SPLIT_SIZE	(MIN_USER_MAXROUTABLE_SIZE - sizeof( SPLITPACKET ))
#define MAX_SPLIT_SIZE	(MAX_USER_MAXROUTABLE_SIZE - sizeof( SPLITPACKET ))

// Calculate MAX_SPLITPACKET_SPLITS according to the smallest split size
#define MAX_SPLITPACKET_SPLITS ( NET_MAX_MESSAGE / MIN_SPLIT_SIZE )
#define SPLIT_PACKET_STALE_TIME		2.0f
#define SPLIT_PACKET_TRACKING_MAX 256  // most number of outstanding split packets to allow

class CSplitPacketEntry
{
public:
	CSplitPacketEntry()
	{
		from.Clear();

		int i;
		for (i = 0; i < MAX_SPLITPACKET_SPLITS; i++)
		{
			splitflags[i] = -1;
		}

		memset(&netsplit, 0, sizeof(netsplit));
		netsplit.currentSequence = -1;
		lastactivetime = 0.0f;
	}

public:
	netadr_t		from;
	int				splitflags[MAX_SPLITPACKET_SPLITS];
	LONGPACKET		netsplit;
	// host_time the last time any entry was received for this entry
	float			lastactivetime;
};

typedef CUtlVector< CSplitPacketEntry > vecSplitPacketEntries_t;
static CUtlVector< vecSplitPacketEntries_t > *net_splitpackets;

static CUtlVector< vecSplitPacketEntries_t >& SplitPacketVec()
{
	return *net_splitpackets;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *from - 
// Output : CSplitPacketEntry
//-----------------------------------------------------------------------------
static CSplitPacketEntry* NET_FindOrCreateSplitPacketEntry(const unsigned int sock, netadr_t* from)
{
	vecSplitPacketEntries_t& splitPacketEntries = SplitPacketVec()[sock];
	const int count = splitPacketEntries.Count();

	for (int i = 0; i < count; ++i)
	{
		CSplitPacketEntry* const entry = &splitPacketEntries[i];
		Assert(entry);

		if (from->CompareAdr(entry->from))
			return entry;
	}

	const int index = splitPacketEntries.AddToTail();
	CSplitPacketEntry* const entry = &splitPacketEntries[index];

	Assert(entry);
	entry->from = *from;

	return entry;
}

//-----------------------------------------------------------------------------
// Purpose: reassembles a fragmented network packet
// Input  : sock - 
//          *packet - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
static bool NET_GetLongPacket(const netsocket_e sock, netpacket_t* const packet)
{
	if (packet->size < static_cast<int>(sizeof(SPLITPACKET)))
	{
		DevMsg(eDLL_T::ENGINE, "Invalid split packet length %i\n", packet->size);
		return false;
	}

	CSplitPacketEntry* const entry = NET_FindOrCreateSplitPacketEntry(static_cast<unsigned int>(sock), &packet->from);
	entry->lastactivetime = static_cast<float>(*g_pNetTime);
	LONGPACKET& split = entry->netsplit;

	// pHeader is network endian correct
	const SPLITPACKET* const pHeader = reinterpret_cast<const SPLITPACKET*>(packet->pData);

	const int sequenceNumber = LittleLong(pHeader->sequenceNumber);
	const unsigned short packetID = static_cast<unsigned short>(LittleShort(static_cast<short>(pHeader->packetID)));
	const int packetNumber = packetID >> 8; 	// High byte is packet number
	const int packetCount = packetID & 0xFF; 	// Low byte is number of total packets
	const int splitSize = LittleShort(static_cast<short>(pHeader->nSplitSize));
	const int fragmentSize = packet->size - static_cast<int>(sizeof(SPLITPACKET));

	if (packetCount == 0)
	{
		DevMsg(eDLL_T::ENGINE,
			__FUNC_PFX__"Invalid split packet number %i of %i from %s\n",
			packetNumber + 1,
			packetCount,
			packet->from.ToString());
		return false;
	}

	if (packetNumber < 0 || packetNumber >= packetCount)
	{
		DevMsg(eDLL_T::ENGINE,
			__FUNC_PFX__"Split packet from %s with invalid packet number (number %i/ count %i)\n",
			packet->from.ToString(),
			packetNumber,
			packetCount);
		return false;
	}

	if (splitSize < static_cast<int>(MIN_SPLIT_SIZE) || splitSize > static_cast<int>(MAX_SPLIT_SIZE))
	{
		DevMsg(eDLL_T::ENGINE,
			__FUNC_PFX__"Split packet from %s with invalid split size (number %i/ count %i) where size %i is out of valid range [%zu - %zu]\n",
			packet->from.ToString(),
			packetNumber,
			packetCount,
			splitSize,
			static_cast<size_t>(MIN_SPLIT_SIZE),
			static_cast<size_t>(MAX_SPLIT_SIZE));
		return false;
	}

	// First packet in split series?
	if (split.currentSequence == -1 || sequenceNumber != split.currentSequence)
	{
		split.currentSequence = sequenceNumber;
		split.splitCount = packetCount;
		split.nExpectedSplitSize = splitSize;
	}
	else if (split.nExpectedSplitSize != splitSize)
	{
		DevMsg(eDLL_T::ENGINE,
			__FUNC_PFX__"Split packet from %s with inconsistent split size (number %i/ count %i) where size %i not equal to initial size of %i\n",
			packet->from.ToString(),
			packetNumber,
			packetCount,
			splitSize,
			split.nExpectedSplitSize);
		return false;
	}

	if (entry->splitflags[packetNumber] == sequenceNumber)
	{
		DevMsg(eDLL_T::ENGINE,
			__FUNC_PFX__"Ignoring duplicated split packet %i of %i (%i bytes) from %s\n",
			packetNumber + 1,
			packetCount,
			fragmentSize,
			packet->from.ToString());
		return false;
	}

	const int offset = packetNumber * splitSize;
	const int totalSize = offset + fragmentSize;

	// Last packet in sequence? set size
	if (packetNumber == (packetCount-1))
		split.totalSize = totalSize;

	if (net_showsplits->GetInt() && net_showsplits->GetInt() != 3)
	{
		Msg(eDLL_T::ENGINE,
			"<-- [%u] Split packet %4i/%4i seq %5i size %4i mtu %4i from %s\n",
			static_cast<unsigned int>(sock), packetNumber + 1, packetCount,
			sequenceNumber, fragmentSize,
			splitSize + static_cast<int>(sizeof(SPLITPACKET)),
			packet->from.ToString());
	}

	if (totalSize > static_cast<int>(sizeof(split.buffer)))
	{
		DevMsg(eDLL_T::ENGINE,
			__FUNC_PFX__"Split packet from %s exceeds split buffer size (number %i/ count %i) where total size %i exceeds buffer size %zu\n",
			packet->from.ToString(),
			packetNumber,
			packetCount,
			totalSize,
			sizeof(split.buffer));
		return false;
	}

	--split.splitCount;
	entry->splitflags[packetNumber] = sequenceNumber;

	// Copy the incoming data to the appropriate place in the buffer
	memcpy(split.buffer + offset, packet->pData + sizeof(SPLITPACKET), static_cast<size_t>(fragmentSize));

	// Have we received all of the pieces to the packet?
	if (split.splitCount > 0)
		return false;

	split.currentSequence = -1;	// Clear packet

	if (split.totalSize > NET_MAX_MESSAGE)
	{
		DevMsg(eDLL_T::ENGINE,
			__FUNC_PFX__"Split packet too large! %i bytes from %s\n",
			split.totalSize,
			packet->from.ToString());
		return false;
	}

	// The packet data buffer must be large enough for the reconstructed message.
	memcpy(packet->pData, split.buffer, static_cast<size_t>(split.totalSize));
	packet->size = split.totalSize;
	packet->wiresize = split.totalSize;

	return true;
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
	DetourSetup(&v_NET_BufferToBufferDecompress_OnlyUsedByRecvDatagram_TEMP, &NET_BufferToBufferDecompress_OnlyUsedByRecvDatagram_TEMP, bAttach);
	DetourSetup(&v_NET_PrintFunc, &NET_PrintFunc, bAttach);

	DetourSetup(&v_NET_GetLongPacket, &NET_GetLongPacket, bAttach);
}

void _NET_Internal_InitStaticVars()
{
	net_splitpackets = CMemory(v_NET_GetLongPacket).FindPatternSelf("4C 03").ResolveRelativeAddressSelf(3, 7).RCast<CUtlVector< vecSplitPacketEntries_t > *>();
}

///////////////////////////////////////////////////////////////////////////////
netadr_t* g_pNetAdr = nullptr;
netkey_t* g_pNetKey = nullptr;

double* g_pNetTime = nullptr;
#endif // !_TOOLS
