#pragma once

constexpr unsigned int AES_128_KEY_SIZE = 16;
constexpr unsigned int AES_128_B64_ENCODED_SIZE = 24;
constexpr const char DEFAULT_NET_ENCRYPTION_KEY[AES_128_B64_ENCODED_SIZE+1] = "WDNWLmJYQ2ZlM0VoTid3Yg==";

#ifndef _TOOLS
#include "engine/net_chan.h"
#include "tier1/lzss.h"

// Flow control bytes per second limits
#define MIN_RATE		1000
#define MAX_RATE		1000000000

#define MAX_STREAMS         2

// user message
#define MAX_USER_MSG_DATA 511 // <-- 255 in Valve Source.

#define NETMSG_TYPE_BITS	7	// must be 2^NETMSG_TYPE_BITS > SVC_LASTMSG (6 in Valve Source).
#define NETMSG_LENGTH_BITS	12	// 512 bytes (11 in Valve Source, 256 bytes).

#define NET_MIN_MESSAGE 5 // Even connectionless packets require int32 value (-1) + 1 byte content
#define NET_MAX_PAYLOAD	262144 // <-- 288000 in Valve Source.

/* ==== CNETCHAN ======================================================================================================================================================== */
inline void*(*v_NET_Init)(bool bDeveloper);
inline void(*v_NET_Config)(void);

inline void(*v_NET_SetKey)(netkey_t* pKey, const char* szHash);
inline CNetChan* (*v_NET_CreateNetChannel)(int socket, netadr_t* adr, const char* name, INetChannelHandler* handler, char bForceNewChannel);

inline int(*v_NET_GetPacket)(int iSocket, uint8_t* pScratch, bool bEncrypted);
inline int(*v_NET_SendPacket)(CNetChan* pChan, int iSocket, const netadr_t& toAdr, const uint8_t* pData, unsigned int nLen, void* unused0, bool bCompress, void* unused1, bool bEncrypt);

inline bool(*v_NET_ReceiveDatagram)(int iSocket, netpacket_s* pInpacket, bool bRaw);
inline int(*v_NET_SendDatagram)(SOCKET s, void* pPayload, int iLenght, netadr_t* pAdr, bool bEncrypted);

inline bool(*v_NET_BufferToBufferCompress)(uint8_t* const dest, size_t* const destLen, uint8_t* const source, const size_t sourceLen);
inline unsigned int(*v_NET_BufferToBufferDecompress_OnlyUsedByRecvDatagram_TEMP)(CLZSS* lzss, unsigned char* pInput, unsigned char* pOutput, unsigned int unBufSize);

inline bool (*v_NET_IsLocalHost)(const CNetAdr* const adr);

inline void (*v_NET_OutOfBandPrintf)(int sock, const CNetAdr* adr, const char* format, ...);
inline void(*v_NET_PrintFunc)(const char* fmt, ...);


///////////////////////////////////////////////////////////////////////////////
bool NET_ReceiveDatagram(int iSocket, netpacket_s* pInpacket, bool bRaw);
int  NET_SendDatagram(SOCKET s, void* pPayload, int iLenght, netadr_t* pAdr, bool bEncrypted);
void NET_PrintKey();
void NET_SetKey(const string& svNetKey);
void NET_GenerateKey();
void NET_PrintFunc(const char* fmt, ...);
void NET_RemoveChannel(CClient* pClient, int nIndex, const char* szReason, uint8_t bBadRep, bool bRemoveNow);

bool NET_BufferToBufferCompress(uint8_t* const dest, size_t* const destLen, uint8_t* const source, const size_t sourceLen);
bool NET_BufferToBufferDecompress(uint8_t* const source, const size_t sourceLen, uint8_t* const dest, size_t* const destLen);

bool NET_ReadMessageType(int* outType, bf_read* buffer);
bool NET_IsLocalHost(const CNetAdr& netAdr);

///////////////////////////////////////////////////////////////////////////////
extern netadr_t* g_pNetAdr;
extern netkey_t* g_pNetKey;

extern double* g_pNetTime;

extern ConVar net_useRandomKey;

///////////////////////////////////////////////////////////////////////////////
class VNet : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("NET_Init", v_NET_Init);
		LogFunAdr("NET_Config", v_NET_Config);

		LogFunAdr("NET_SetKey", v_NET_SetKey);
		LogFunAdr("NET_CreateNetChannel", v_NET_CreateNetChannel);

		LogFunAdr("NET_GetPacket", v_NET_GetPacket);
		LogFunAdr("NET_SendPacket", v_NET_SendPacket);

		LogFunAdr("NET_ReceiveDatagram", v_NET_ReceiveDatagram);
		LogFunAdr("NET_SendDatagram", v_NET_SendDatagram);

		LogFunAdr("NET_BufferToBufferCompress", v_NET_BufferToBufferCompress);
		LogFunAdr("NET_BufferToBufferDecompress_LZSS", v_NET_BufferToBufferDecompress_OnlyUsedByRecvDatagram_TEMP);

		LogFunAdr("NET_IsLocalHost", v_NET_IsLocalHost);

		LogFunAdr("NET_PrintFunc", v_NET_PrintFunc);
		LogFunAdr("NET_OutOfBandPrintf", v_NET_OutOfBandPrintf);

		LogVarAdr("g_NetAdr", g_pNetAdr);
		LogVarAdr("g_NetKey", g_pNetKey);
		LogVarAdr("g_NetTime", g_pNetTime);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 54 41 56 41 57 48 81 EC F0 01 ??").GetPtr(v_NET_Init);
		Module_FindPattern(g_GameDll, "48 81 EC ?? ?? ?? ?? E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F 57 C0").GetPtr(v_NET_Config);

		Module_FindPattern(g_GameDll, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F9 41 B8").GetPtr(v_NET_SetKey);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 6C 24 ?? 57 41 54 41 57 48 83 EC ?? 80 7C 24").GetPtr(v_NET_CreateNetChannel);


		Module_FindPattern(g_GameDll, "48 8B C4 44 88 40 18 48 89 50 10 41 55").GetPtr(v_NET_GetPacket);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 74 24 ?? 55 57 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 2B E0 4C 63 F2").GetPtr(v_NET_SendPacket);

		Module_FindPattern(g_GameDll, "48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D AC 24 50 EB").GetPtr(v_NET_ReceiveDatagram);
		Module_FindPattern(g_GameDll, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 81 EC ?? 05 ?? ??").GetPtr(v_NET_SendDatagram);

		Module_FindPattern(g_GameDll, "48 89 6C 24 ?? 48 89 74 24 ?? 57 41 56 41 57 48 83 EC 50 48 8B 05 ?? ?? ?? ?? 49 8B E9").GetPtr(v_NET_BufferToBufferCompress);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 6C 24 ?? 48 89 74 24 ?? 41 56 45 33 F6").GetPtr(v_NET_BufferToBufferDecompress_OnlyUsedByRecvDatagram_TEMP);

		Module_FindPattern(g_GameDll, "83 39 ?? 74 ?? 48 8B 05 ?? ?? ?? ?? 48 3B 41").GetPtr(v_NET_IsLocalHost);

		Module_FindPattern(g_GameDll, "48 89 54 24 10 4C 89 44 24 18 4C 89 4C 24 20 C3 48").GetPtr(v_NET_PrintFunc);
		Module_FindPattern(g_GameDll, "4C 89 44 24 ?? 4C 89 4C 24 ?? 53 55 56 57 41 56 48 81 EC").GetPtr(v_NET_OutOfBandPrintf);
	}
	virtual void GetVar(void) const
	{
		g_pNetAdr = CMemory(v_NET_Config).FindPatternSelf("89 05 AC").ResolveRelativeAddressSelf(2, 6).RCast<netadr_t*>();
		g_pNetKey = CMemory(v_NET_Init).OffsetSelf(0x300).FindPatternSelf("48 8D 0D").ResolveRelativeAddressSelf(3, 7).RCast<CNetKey*>();
		g_pNetTime = CMemory(v_NET_Config).FindPatternSelf("F2 0F").ResolveRelativeAddressSelf(4, 8).RCast<double*>();
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
#endif // !_TOOLS

const char* NET_ErrorString(int iCode);
