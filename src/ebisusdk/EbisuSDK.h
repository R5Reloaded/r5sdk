#pragma once

#define MAX_PERSONA_NAME_LEN 64 // sizeof( g_PersonaName )
#define FAKE_BASE_NUCLEUD_ID 9990000
#include <tier1/utlhash.h>

#define ORIGIN_EVENT_PRESENCE 4
#define ORIGIN_EVENT_FRIENDS 5

struct OriginPresenceT
{
	uint64_t    UserId;
	uint32_t    PresenceState;
	const char *TitleId;
	const char *MultiplayerId;
	const char *Title;
	const char *Presence;
	const char *GamePresence;
	const char *Group;
	const char *GroupId;
};

struct OriginFriendT
{
	uint64_t	UserId;
	uint64_t	PersonaId;
	const char *Persona;
	const char *AvatarId;
	const char *Group;
	const char *GroupId;
	int			PresenceState;
	int			FriendState;
	const char *TitleId;
	const char *Title;
	const char *MultiplayerId;
	const char *Presence;
	const char *GamePresence;
};

struct FriendCacheEntry
{
	uint64_t m_UserId;
	char	 m_szPersonaName[64];
	char	 m_szGameTitle[128];
	bool	 m_bIsOnline;
	bool	 m_bIsPlayingApex;
	bool	 m_bIsIdle;
};

struct SteamFriend
{
    uint8_t	 m_HardwareType;
    bool	 m_bOnline;
    uint64_t m_uid;
    char	 m_szName[32];	
};

inline bool OriginFriendRecordCompareFunc( const FriendCacheEntry &lhs, const FriendCacheEntry &rhs )
{
	return lhs.m_UserId == rhs.m_UserId;
}

inline unsigned int OriginFriendRecordHashFunc( const FriendCacheEntry &data )
{
	return Hash8( &data.m_UserId );
}

typedef CUtlHash<FriendCacheEntry, &OriginFriendRecordCompareFunc, &OriginFriendRecordHashFunc> FriendCacheUtlHash;
inline FriendCacheUtlHash **g_ppCachedFriendsList;
inline CThreadMutex*               g_pOriginFriendsCacheLock;
inline const char *const *         g_ppszPlatformNames;
inline SteamFriend *g_pSteamFriendsList;

inline void(*EbisuSDK_Tier0_Init)(void);
inline void(*EbisuSDK_CVar_Init)(void);
inline void(*EbisuSDK_RunFrame)(void);
inline const char*(*EbisuSDK_GetLanguage)(void);

inline uint64_t* g_NucleusID = nullptr;
inline char* g_NucleusToken = nullptr; /*SIZE = 1024*/
inline char* g_OriginAuthCode = nullptr; /*SIZE = 256*/
inline char* g_PersonaName = nullptr; /*SIZE = 64*/
inline int* g_OriginErrorLevel = nullptr;
inline bool* g_EbisuSDKInit = nullptr;
inline bool* g_EbisuProfileInit = nullptr;

///////////////////////////////////////////////////////////////////////////////
void HEbisuSDK_Init();
const char* HEbisuSDK_GetLanguage();

bool IsOriginDisabled();
bool IsOriginInitialized();

bool IsValidPersonaName(const char* const pszName, const int nMinLen, const int nMaxLen);

enum CachedFriendState : int32_t
{
    NO_CACHE,
    FOUND,
    NOT_FOUND
};

inline CachedFriendState ( *v_GetCachedFriendState )( uint64_t userID );
inline bool ( *v_GetFriendsListFromCache )( CUtlVector<CUtlString>& pvPersonaNames, CUtlVector<CUtlString>& pvUserIDs, CUtlVector<CUtlString>& pvPlatformTags );
inline bool ( *v_GetFriendsListFromCacheWithPresence )( CUtlVector<CUtlString> &vPersonaNames, CUtlVector<CUtlString> &vUserIDs,
														CUtlVector<CUtlString> &vPlatformTags, CUtlVector<bool> &vOnlineState,
														CUtlVector<bool> &vIsPlayingApexStates, CUtlVector<CUtlString> &vGameTitles, CUtlVector<bool> &vIdleStates );

inline void ( *v_CacheOriginFriend )( OriginFriendT *pOriginFriend, FriendCacheUtlHash *pFriendCache );
inline int32_t ( *v_OriginReadEnumerationSync )( intptr_t hHandle, void *pBuffer, size_t nBuffSize, size_t startIndex, size_t count, size_t *countRead );
inline int32_t ( *v_OriginFriendsEnumerationCallback )( void *pCtx, intptr_t hHandle, size_t total, size_t avaliable, int32_t err );
inline int32_t ( *v_OriginEventCallback )( int32_t eventID, void *pCtx, void *pEventData, size_t count );
inline int32_t ( *v_OriginDestroyHandle )( intptr_t hHandle );

///////////////////////////////////////////////////////////////////////////////
class VEbisuSDK : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("EbisuSDK_Tier0_Init", EbisuSDK_Tier0_Init);
		LogFunAdr("EbisuSDK_CVar_Init", EbisuSDK_CVar_Init);
		LogFunAdr("EbisuSDK_RunFrame", EbisuSDK_RunFrame);
		LogFunAdr("EbisuSDK_GetLanguage", EbisuSDK_GetLanguage);
		LogFunAdr( "OriginDestroyHandle", v_OriginDestroyHandle );
		LogFunAdr( "OriginFriendsEnumerationCallback", v_OriginFriendsEnumerationCallback );
		LogFunAdr( "OriginEventCallback", v_OriginEventCallback );
		LogFunAdr( "OriginReadEnumerationSync", v_OriginReadEnumerationSync );
		LogFunAdr( "OrignFriendGetCachedState", v_GetCachedFriendState );
		LogFunAdr( "GetFriendsListFromCache", v_GetFriendsListFromCache );
		LogFunAdr( "GetFriendsListFromCacheWithPresence", v_GetFriendsListFromCacheWithPresence );
		LogVarAdr("g_NucleusID", g_NucleusID);
		LogVarAdr("g_NucleusToken", g_NucleusToken);
		LogVarAdr("g_OriginAuthCode", g_OriginAuthCode);
		LogVarAdr("g_PersonaName", g_PersonaName);
		LogVarAdr("g_OriginErrorLevel", g_OriginErrorLevel);
		LogVarAdr("g_EbisuProfileInit", g_EbisuProfileInit);
		LogVarAdr("g_EbisuSDKInit", g_EbisuSDKInit);
		LogVarAdr( "g_ppCachedFriendsList", g_ppCachedFriendsList );
		LogVarAdr( "g_OriginFriendsCacheLock", g_pOriginFriendsCacheLock );
		LogVarAdr( "g_pSteamFriendsList", g_pSteamFriendsList );
		LogVarAdr( "g_ppszPlatformNames", g_ppszPlatformNames );
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 83 EC 28 80 3D ?? ?? ?? ?? ?? 0F 85 ?? 02 ?? ?? 48 89 5C 24 20").GetPtr(EbisuSDK_Tier0_Init);
		Module_FindPattern(g_GameDll, "40 57 48 83 EC 40 83 3D").GetPtr(EbisuSDK_CVar_Init);
		Module_FindPattern(g_GameDll, "48 81 EC ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 74 5B").GetPtr(EbisuSDK_RunFrame);
		Module_FindPattern(g_GameDll, "48 8B C4 48 81 EC ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F 85 ?? ?? ?? ??").GetPtr(EbisuSDK_GetLanguage);
		Module_FindPattern( g_GameDll, "48 85 D2 0F 84 ?? ?? ?? ?? 48 89 5C 24 ?? 48 89 7C 24" ).GetPtr( v_CacheOriginFriend );

        Module_FindPattern( g_GameDll, "41 56 41 57 48 83 EC ?? 83 BC 24" ).GetPtr( v_OriginFriendsEnumerationCallback );
		Module_FindPattern( g_GameDll, "48 89 5C 24 18 55 48 81 EC 40 01 00 00" ).GetPtr( v_OriginEventCallback );
		Module_FindPattern( g_GameDll, "40 53 48 83 EC 30 48 8B D9 48 8D 15 D0 2C 20 00" ).GetPtr( v_OriginDestroyHandle );
		Module_FindPattern( g_GameDll, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 40 48 8B F2 48 8B E9 48 8D 15 4F 23 20 00" ).GetPtr( v_OriginReadEnumerationSync );
		Module_FindPattern( g_GameDll, "40 53 48 81 EC B0 00 00 00 48 8B D9 48 8D 0D 35 49 1C 23" ).GetPtr( v_GetCachedFriendState );
		Module_FindPattern( g_GameDll, "40 55 53 41 54 41 55 41 57 48 8D 6C 24 80" ).GetPtr( v_GetFriendsListFromCache );
		Module_FindPattern( g_GameDll, "4C 89 4C 24 20 4C 89 44 24 18 48 89 54 24 10 55 53 57" ).GetPtr( v_GetFriendsListFromCacheWithPresence );
	}
	virtual void GetVar(void) const
	{
		g_NucleusID = CMemory(EbisuSDK_CVar_Init).Offset(0x20).FindPatternSelf("4C 89 05", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x3, 0x7).RCast<uint64_t*>();
		g_NucleusToken = CMemory(EbisuSDK_RunFrame).Offset(0x1EF).FindPatternSelf("80 3D", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x2, 0x7).RCast<char*>();
		g_OriginAuthCode = CMemory(EbisuSDK_RunFrame).Offset(0x1BF).FindPatternSelf("0F B6", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x3, 0x7).RCast<char*>();
		g_PersonaName = CMemory(EbisuSDK_CVar_Init).Offset(0x120).FindPatternSelf("48 8D 0D", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x3, 0x7).RCast<char*>();
		g_OriginErrorLevel = CMemory(EbisuSDK_RunFrame).Offset(0x20).FindPatternSelf("89 05", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x2, 0x6).RCast<int*>();
		g_EbisuProfileInit = CMemory(EbisuSDK_CVar_Init).Offset(0x12A).FindPatternSelf("C6 05", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x2, 0x7).RCast<bool*>();
		g_EbisuSDKInit = CMemory(EbisuSDK_Tier0_Init).Offset(0x0).FindPatternSelf("80 3D", CMemory::Direction::DOWN, 150).ResolveRelativeAddressSelf(0x2, 0x7).RCast<bool*>();

        Module_FindPattern( g_GameDll, "40 55 53 41 54 41 55 41 57 48 8D 6C 24 ?? 48 81 EC ?? ?? ?? ?? 4D 8B F8" )
            .OffsetSelf( 0x35A ).FindPatternSelf( "4C 8B 1D F7" ).ResolveRelativeAddressSelf( 0x3, 0x7 ).GetPtr( g_ppCachedFriendsList );

        CMemory( v_OriginFriendsEnumerationCallback ).OffsetSelf( 0x170 ).FindPatternSelf( "48 8D 0D A9" )
            .ResolveRelativeAddressSelf( 0x3, 0x7 ).GetPtr(g_pOriginFriendsCacheLock);

        Module_FindPattern( g_GameDll, "40 55 41 55 41 57 48 83 EC 40 48 8B E9" )
			.FindPatternSelf( "4C 8D 2D 5C EA DE 0B" )
			.ResolveRelativeAddressSelf( 0x3, 0x7 )
			.GetPtr( g_pSteamFriendsList );

        CMemory( v_GetFriendsListFromCache ).OffsetSelf( 0x5F5 ).FindPatternSelf( "48 8D 0D 6D" )
            .ResolveRelativeAddressSelf( 0x3, 0x7 ).GetPtr( g_ppszPlatformNames );
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
