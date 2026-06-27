//===========================================================================//
//
// Purpose: A set of utilities to perform encryption/decryption
//
//===========================================================================//
#include "mbedtls/aes.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/gcm.h"

#include "tier2/cryptutils.h"

static BCRYPT_ALG_HANDLE s_algorithmProvider;
bool Plat_GenerateRandom(unsigned char* buffer, const uint32_t bufferSize, const char*& errorMsg)
{
	if (!s_algorithmProvider && (BCryptOpenAlgorithmProvider(&s_algorithmProvider, L"RNG", 0, 0) < 0))
	{
		errorMsg = "Failed to open rng algorithm";
		return false;
	}

	if (BCryptGenRandom(s_algorithmProvider, buffer, bufferSize, 0) < 0)
	{
		errorMsg = "Failed to generate random data";
		return false;
	}

	return true;
}

bool Crypto_GenerateIV(CryptoContext_s& ctx, const u8* const data, const size_t size)
{
	mbedtls_entropy_context entropy;
	mbedtls_entropy_init(&entropy);

	mbedtls_ctr_drbg_context drbg;
	mbedtls_ctr_drbg_init(&drbg);

	//mbedtls_ctr_drbg_seed will only accept a maximum of MBEDTLS_CTR_DRBG_MAX_SEED_INPUT - MBEDTLS_CTR_DRBG_ENTROPY_LEN as custom data for seed generation
	//make sure to clamp the number of bytes we ask it to read
	const size_t customDataLen = Min<size_t>(size, MBEDTLS_CTR_DRBG_MAX_SEED_INPUT - MBEDTLS_CTR_DRBG_ENTROPY_LEN);

	const int seedRet = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, data, customDataLen);
	int randRet = MBEDTLS_ERR_ERROR_GENERIC_ERROR;

	if (seedRet == 0)
	{
		randRet = mbedtls_ctr_drbg_random(&drbg, ctx.iv, sizeof(ctx.iv));
	}

	mbedtls_ctr_drbg_free(&drbg);
	mbedtls_entropy_free(&entropy);

	return randRet == 0;
}

bool Crypto_EncryptGCM(CryptoContext_s& ctx, const u8* const inBuf, u8* const outBuf,
						const CryptoKey_t key, const size_t size,
						const u8* const aad, const size_t aadSize)
{
	mbedtls_gcm_context gcm;
	mbedtls_gcm_init(&gcm);

	const int setRet =
		mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, ctx.keyBits);

	int cryptRet = MBEDTLS_ERR_ERROR_GENERIC_ERROR;

	if (setRet == 0)
	{
		cryptRet = mbedtls_gcm_crypt_and_tag(
			&gcm,
			MBEDTLS_GCM_ENCRYPT,
			size,
			ctx.iv,
			sizeof(ctx.iv),
			aad,
			aadSize,
			inBuf,
			outBuf,
			sizeof(CryptoTag_t),
			ctx.tag);
	}

	mbedtls_gcm_free(&gcm);
	return cryptRet == 0;
}

bool Crypto_DecryptGCM(CryptoContext_s& ctx, const u8* const inBuf, u8* const outBuf,
						const CryptoKey_t key, const size_t size,
						const u8* const aad, const size_t aadSize)
{
	mbedtls_gcm_context gcm;
	mbedtls_gcm_init(&gcm);

	const int setRet =
		mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, ctx.keyBits);

	int cryptRet = MBEDTLS_ERR_ERROR_GENERIC_ERROR;

	if (setRet == 0)
	{
		cryptRet = mbedtls_gcm_auth_decrypt(
			&gcm,
			size,
			ctx.iv,
			sizeof(ctx.iv),
			aad,
			aadSize,
			ctx.tag,
			sizeof(CryptoTag_t),
			inBuf,
			outBuf);
	}

	mbedtls_gcm_free(&gcm);
	return cryptRet == 0;
}
