#ifndef TIER2_CRYPTUTILS_H
#define TIER2_CRYPTUTILS_H

bool Plat_GenerateRandom(unsigned char* pBuffer, const uint32_t nBufLen, const char*& errorMsg);

typedef unsigned char CryptoIV_t[16];
typedef unsigned char CryptoKey_t[16];
typedef unsigned char CryptoTag_t[16];

struct CryptoContext_s
{
	CryptoContext_s(const int setKeyBits = 128)
		: keyBits(setKeyBits)
	{
		Assert(setKeyBits == 128 || setKeyBits == 192 || setKeyBits == 256);
		memset(iv, 0, sizeof(iv));
	}

	CryptoIV_t iv;
	CryptoTag_t tag;
	int keyBits;
};

bool Crypto_GenerateIV(CryptoContext_s& ctx, const u8* const data, const size_t size);

bool Crypto_EncryptGCM(CryptoContext_s& ctx, const u8* const inBuf, u8* const outBuf,
						const CryptoKey_t key,const size_t size,
						const u8* const aad = nullptr, const size_t aadSize = 0);

bool Crypto_DecryptGCM(CryptoContext_s& ctx, const u8* const inBuf, u8* const outBuf,
						const CryptoKey_t key, const size_t size,
						const u8* const aad = nullptr, const size_t aadSize = 0);

#endif // TIER2_CRYPTUTILS_H
