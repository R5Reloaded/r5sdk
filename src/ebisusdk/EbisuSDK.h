#pragma once
#include "ebisusdk/EbisuTypes.h"

#define ORIGIN_MAX_USERNAME_LENGTH 64
#define ORIGIN_MAX_COUNTRY_LENGTH 64

#define ORIGIN_MAX_AUTH_CODE_LENGTH 256
#define ORIGIN_MAX_TOKEN_LENGTH 1024

#define ORIGIN_MAX_ENVIRONMENT_LENGTH 64
#define ORIGIN_MAX_STORE_URI_LENGTH 2048

#define FAKE_BASE_NUCLEUD_ID 9990000

struct OriginQuery_s
{
	unsigned int requestHandle;
	char queryPath[1024];
	CUtlBuffer queryParams;
};

struct OriginTokenRequest_s
{
	OriginQuery_s requestTokenData;
	char refreshTokenHttpData[4096]; // Some internal HTTP struct, needs further reversal.
	OriginQuery_s refreshTokenData;
};

struct OriginState_s
{
	bool isInitialized;
	bool isOriginChatActive;

	bool isOnline;
	bool onlineStale;

	bool requestingProfile;
	bool hasProfile;

	bool hasOnlineAccess;
	bool isUpToDate;
	bool isTrialVersion;

	// IGO stuff
	bool isOverlayAvailable;
	bool isOverlayEnabled;
	bool isOverlayDisplayed;

	bool requestingAuthCode;
	bool requestingToken;
	bool refreshingToken;

	bool unkF;

	bool isProduction;
	bool ugcRestriction;
	bool isSubscriber;
	bool gamePurchased;
	bool isUnderAge;
	bool entitlementsChanged; // [r5apex + 0x32DFF0]

	bool errorHandled;
	int errorLevel;

	_BYTE gap1C[4];

	NucleusID_t userId;
	u64 personaId;
	u64 unk30;

	double unk38; // I really don't know.

	double tokenTime;
	int tokenExpiry;

	void* unk50;
	void* somethingWithEntitlements;
	void* unk60[4];

	OriginTokenRequest_s tokenRequest;

	char unk1940[256];
	u64 unk1A40;

	char authCode[ORIGIN_MAX_AUTH_CODE_LENGTH];
	char token[ORIGIN_MAX_TOKEN_LENGTH];
	char userName[ORIGIN_MAX_USERNAME_LENGTH];
	char country[ORIGIN_MAX_COUNTRY_LENGTH];
	char avatarId[MAX_OSPATH];
	char environment[ORIGIN_MAX_ENVIRONMENT_LENGTH];
	char storeUri[ORIGIN_MAX_STORE_URI_LENGTH];

	// todo: reverse
	char precenseStuff[780];
};

inline OriginState_s* g_originState;

inline void(*EbisuSDK_Tier0_Init)(void);
inline void(*EbisuSDK_CVar_Init)(void);
inline void(*EbisuSDK_RunFrame)(void);
inline const char*(*EbisuSDK_GetLanguage)(void);

///////////////////////////////////////////////////////////////////////////////
void HEbisuSDK_Init();
const char* HEbisuSDK_GetLanguage();

bool IsOriginDisabled();
bool IsOriginInitialized();

bool IsValidPersonaName(const char* const pszName, const int nMinLen, const int nMaxLen);

///////////////////////////////////////////////////////////////////////////////
class VEbisuSDK : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("EbisuSDK_Tier0_Init", EbisuSDK_Tier0_Init);
		LogFunAdr("EbisuSDK_CVar_Init", EbisuSDK_CVar_Init);
		LogFunAdr("EbisuSDK_RunFrame", EbisuSDK_RunFrame);
		LogFunAdr("EbisuSDK_GetLanguage", EbisuSDK_GetLanguage);
		LogVarAdr("g_originState", g_originState);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 83 EC 28 80 3D ?? ?? ?? ?? ?? 0F 85 ?? 02 ?? ?? 48 89 5C 24 20").GetPtr(EbisuSDK_Tier0_Init);
		Module_FindPattern(g_GameDll, "40 57 48 83 EC 40 83 3D").GetPtr(EbisuSDK_CVar_Init);
		Module_FindPattern(g_GameDll, "48 81 EC ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 74 5B").GetPtr(EbisuSDK_RunFrame);
		Module_FindPattern(g_GameDll, "48 8B C4 48 81 EC ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F 85 ?? ?? ?? ??").GetPtr(EbisuSDK_GetLanguage);
	}
	virtual void GetVar(void) const
	{
		g_originState = CMemory(EbisuSDK_Tier0_Init).Offset(0x0).FindPatternSelf("80 3D", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x2, 0x7).RCast<OriginState_s*>();
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
