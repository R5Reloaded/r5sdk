//===========================================================================//
// 
// Purpose: Base rcon implementation.
// 
//===========================================================================//
#include "core/stdafx.h"
#include "tier2/cryptutils.h"
#include "base_rcon.h"
#include "engine/net.h"
#include "shared_rcon.h"
#include "protoc/netcon.pb.h"
#include "mbedtls/base64.h"

//-----------------------------------------------------------------------------
// Purpose: sets the encryption key, a key will always be set, either random or
//			the default key on failure
// Input  : *pBase64NetKey - 
//			bUseDefaultOnFailure - 
//-----------------------------------------------------------------------------
void CNetConBase::SetKey(const char* pBase64NetKey, const bool bUseDefaultOnFailure/* = false*/)
{
	// Drop all connections as they would be unable to decipher the message
	// frames once the key has been swapped.
	m_Socket.CloseAllAcceptedSockets();

	bool parseInput = pBase64NetKey && *pBase64NetKey;
	bool genRandom = !parseInput;

	bool failure = false;

	if (parseInput)
	{
		const size_t keyLen = strlen(pBase64NetKey);
		string tokenizedKey;

		if (keyLen != AES_128_B64_ENCODED_SIZE || !IsValidBase64(pBase64NetKey, &tokenizedKey))
		{
			Error(eDLL_T::ENGINE, NO_ERROR, "RCON Key: invalid key (%s)\n", pBase64NetKey);
			failure = true;
		}
		else
		{
			size_t numBytesDecoded = 0;

			const int decodeRet = mbedtls_base64_decode(m_NetKey, sizeof(m_NetKey), &numBytesDecoded,
				reinterpret_cast<const unsigned char*>(tokenizedKey.c_str()), tokenizedKey.length());

			if (decodeRet != 0)
			{
				Error(eDLL_T::ENGINE, NO_ERROR, "RCON Key: decode error (%d)\n", decodeRet);
				failure = true;
			}
			else if (numBytesDecoded != sizeof(m_NetKey))
			{
				Error(eDLL_T::ENGINE, NO_ERROR, "RCON Key: read error (%zu != %zu)\n", numBytesDecoded, sizeof(m_NetKey));
				failure = true;
			}
			else
			{
				m_Base64NetKey.SetDirect(tokenizedKey.c_str(), ssize_t(tokenizedKey.length()));
			}
		}
	}

	bool useDefaultKey = false; // Last resort

	if (genRandom || failure) // Generate random key
	{
		if (failure && bUseDefaultOnFailure)
		{
			useDefaultKey = true;
		}
		else
		{
			const char* errorMsg = nullptr;

			if (!Plat_GenerateRandom(m_NetKey, sizeof(m_NetKey), errorMsg))
			{
				Error(eDLL_T::ENGINE, NO_ERROR, "RCON Key: generate error (%s)\n", errorMsg);
				useDefaultKey = true;
			}
			else // Try to encode it
			{
				char encodedKey[AES_128_B64_ENCODED_SIZE+1]; // +1 for null terminator.
				size_t numBytesEncoded = 0;

				const int encodeRet = mbedtls_base64_encode(reinterpret_cast<unsigned char*>(&encodedKey),
					sizeof(encodedKey), &numBytesEncoded, m_NetKey, sizeof(m_NetKey));

				if (encodeRet != 0)
				{
					Error(eDLL_T::ENGINE, NO_ERROR, "RCON Key: encode error (%d)\n", encodeRet);
					useDefaultKey = true;
				}
				else if (numBytesEncoded != AES_128_B64_ENCODED_SIZE)
				{
					Error(eDLL_T::ENGINE, NO_ERROR, "RCON Key: write error (%zu != %zu)\n", numBytesEncoded, AES_128_B64_ENCODED_SIZE);
					failure = true;
				}
				else
				{
					m_Base64NetKey.SetDirect(encodedKey, numBytesEncoded);
				}
			}
		}
	}

	if (useDefaultKey) // Use the default key if everything failed (unlikely)
	{
		size_t numBytesDecoded = 0;
		mbedtls_base64_decode(m_NetKey, sizeof(m_NetKey), &numBytesDecoded,
			reinterpret_cast<const unsigned char*>(DEFAULT_NET_ENCRYPTION_KEY), AES_128_B64_ENCODED_SIZE);

		m_Base64NetKey.SetDirect(DEFAULT_NET_ENCRYPTION_KEY, sizeof(DEFAULT_NET_ENCRYPTION_KEY) - 1);
	}
}

//-----------------------------------------------------------------------------
// Purpose: gets the encryption key as a base64 encoded string
//-----------------------------------------------------------------------------
const char* CNetConBase::GetKey(void) const
{
	return m_Base64NetKey.String();
}

//-----------------------------------------------------------------------------
// Purpose: connect to remote
// Input  : *pHostName - 
//			nPort - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CNetConBase::Connect(const char* pHostName, const int nPort)
{
	return NetconClient_Connect(this, pHostName, nPort);
}

//-----------------------------------------------------------------------------
// Purpose: parses input response buffer using length-prefix framing
// Input  : &data - 
//			*pRecvBuf - 
//			nRecvLen - 
//			nMaxLen - 
// Output: true on success, false otherwise
//-----------------------------------------------------------------------------
bool CNetConBase::ProcessBuffer(ConnectedNetConsoleData_s& data, const byte* pRecvBuf, u32 nRecvLen, const u32 nMaxLen)
{
	while (nRecvLen > 0)
	{
		// Read payload if it's already in progress.
		if (data.payloadLen)
		{
			const u32 bytesToCopy = Min(nRecvLen, data.payloadLen - data.payloadRead);
			memcpy(&data.recvBuffer[data.payloadRead], pRecvBuf, bytesToCopy);

			data.payloadRead += bytesToCopy;

			pRecvBuf += bytesToCopy;
			nRecvLen -= bytesToCopy;

			if (data.payloadRead == data.payloadLen)
			{
				if (!ProcessMessage(data.recvBuffer.data(), data.payloadLen, nMaxLen))
					return false;

				// Reset state.
				data.payloadLen = 0;
				data.payloadRead = 0;
			}
		}
		else if (data.payloadRead < sizeof(NetConFrameHeader_s)) // Read the header if we haven't fully recv'd it.
		{
			const u32 bytesToCopy = Min(nRecvLen, int(sizeof(NetConFrameHeader_s)) - data.payloadRead);
			memcpy(reinterpret_cast<char*>(&data.frameHeader) + data.payloadRead, pRecvBuf, bytesToCopy);

			data.payloadRead += bytesToCopy;

			pRecvBuf += bytesToCopy;
			nRecvLen -= bytesToCopy;

			if (data.payloadRead == sizeof(NetConFrameHeader_s))
			{
				NetConFrameHeader_s& header = data.frameHeader;

				// Convert byte order and check for desync.
				header.magic = ntohl(header.magic);
				const char* desyncReason = nullptr;

				if (header.magic != RCON_FRAME_MAGIC)
				{
					desyncReason = "invalid magic";
				}

				if (!desyncReason)
				{
					header.length = ntohl(header.length);

					if (header.length == 0)
					{
						desyncReason = "empty frame";
					}
				}

				if (desyncReason)
				{
					Error(eDLL_T::ENGINE, NO_ERROR, "RCON Cmd: sync error (%s)\n", desyncReason);
					Disconnect("desync");

					return false;
				}

				if (header.length > nMaxLen)
				{
					Disconnect("overflow");
					return false;
				}

				data.payloadLen = header.length;
				data.payloadRead = 0;

				data.recvBuffer.resize(header.length);
			}
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: encrypt message to buffer
// Input  : &ctx - 
//			*pInBuf - 
//			*pOutBuf - 
//			nDataLen - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CNetConBase::Encrypt(CryptoContext_s& ctx, const byte* pInBuf, byte* pOutBuf, const u32 nDataLen) const
{
	if (Crypto_GenerateIV(ctx, pInBuf, nDataLen))
		return Crypto_CTREncrypt(ctx, pInBuf, pOutBuf, m_NetKey, nDataLen);

	Assert(0);
	return false; // failure
}

//-----------------------------------------------------------------------------
// Purpose: decrypt message to buffer
// Input  : &ctx - 
//			*pInBuf - 
//			*pOutBuf - 
//			nDataLen - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CNetConBase::Decrypt(CryptoContext_s& ctx, const byte* pInBuf, byte* pOutBuf, const u32 nDataLen) const
{
	return Crypto_CTRDecrypt(ctx, pInBuf, pOutBuf, m_NetKey, nDataLen);
}

//-----------------------------------------------------------------------------
// Purpose: encode message to buffer
// Input  : *pMsg - 
//			*pMsgBuf - 
//			nMsgLen - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CNetConBase::Encode(google::protobuf::MessageLite* pMsg, byte* pMsgBuf, const u32 nMsgLen) const
{
	return pMsg->SerializeToArray(pMsgBuf, (i32)nMsgLen);
}

//-----------------------------------------------------------------------------
// Purpose: decode message from buffer
// Input  : *pMsg - 
//			*pMsgBuf - 
//			nMsgLen - 
// Output : true on success, false otherwise
//-----------------------------------------------------------------------------
bool CNetConBase::Decode(google::protobuf::MessageLite* pMsg, const byte* pMsgBuf, const u32 nMsgLen) const
{
	return pMsg->ParseFromArray(pMsgBuf, (i32)nMsgLen);
}

//-----------------------------------------------------------------------------
// Purpose: send message to specific connected socket
// Input  : hSocket - 
//			*pMsgBuf - 
//			nMsgLen - 
// Output: true on success, false otherwise
//-----------------------------------------------------------------------------
bool CNetConBase::Send(const SocketHandle_t hSocket, const byte* pMsgBuf, const u32 nMsgLen) const
{
	const int ret = ::send(hSocket, (char*)pMsgBuf, (i32)nMsgLen, MSG_NOSIGNAL);
	return (ret != SOCKET_ERROR);
}

//-----------------------------------------------------------------------------
// Purpose: receive message
// Input  : &data - 
//			nMaxLen - 
// Output: true on success, false otherwise
//-----------------------------------------------------------------------------
void CNetConBase::Recv(ConnectedNetConsoleData_s& data, const u32 nMaxLen)
{
	static char szRecvBuf[1024];

	{//////////////////////////////////////////////
		const int nPendingLen = ::recv(data.socket, szRecvBuf, sizeof(szRecvBuf), MSG_PEEK);
		if (nPendingLen == SOCKET_ERROR && m_Socket.IsSocketBlocking())
		{
			return;
		}
		else if (nPendingLen == 0) // Socket was closed.
		{
			Disconnect("socket closed prematurely");
			return;
		}
		else if (nPendingLen < 0)
		{
			Disconnect("socket closed unexpectedly");
			return;
		}
	}//////////////////////////////////////////////

	u_long nReadLen = 0; // Find out how much we have to read.
	const int iResult = ::ioctlsocket(data.socket, FIONREAD, &nReadLen);

	if (iResult == SOCKET_ERROR)
	{
		Error(eDLL_T::ENGINE, NO_ERROR, "RCON Cmd: ioctl(%s) error (%s)\n", "FIONREAD", NET_ErrorString(WSAGetLastError()));
		return;
	}

	while (nReadLen > 0)
	{
		const int nRecvLen = ::recv(data.socket, szRecvBuf, MIN(sizeof(szRecvBuf), nReadLen), MSG_NOSIGNAL);
		if (nRecvLen == 0) // Socket was closed.
		{
			Disconnect("socket closed");
			break;
		}
		if (nRecvLen < 0 && !m_Socket.IsSocketBlocking())
		{
			Error(eDLL_T::ENGINE, NO_ERROR, "RCON Cmd: recv error (%s)\n", NET_ErrorString(WSAGetLastError()));
			break;
		}

		nReadLen -= static_cast<u_long>(nRecvLen); // Process what we've got.

		if (!ProcessBuffer(data, reinterpret_cast<byte*>(&szRecvBuf), static_cast<u32>(nRecvLen), nMaxLen))
			break;
	}

	return;
}
