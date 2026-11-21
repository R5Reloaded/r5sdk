#include "core/stdafx.h"
#include "tier0/commandline.h"
#include "ebisusdk/EbisuSDK.h"
#include "engine/server/sv_main.h"
#include <engine/common.h>

//-----------------------------------------------------------------------------
// Purpose: initialize the EbisuSDK
//-----------------------------------------------------------------------------
void HEbisuSDK_Init()
{
	const bool isDedicated = IsDedicated();
	const bool noOrigin = IsOriginDisabled();

	// Fill with default data if this is a dedicated server, or if the game was
	// launched with the platform system disabled. Engine code requires these
	// to be set for the game to function, else stuff like the "map" command
	// won't run as 'IsOriginInitialized()' returns false (which got inlined in
	// every place this was called in the game's executable).
	if (isDedicated || noOrigin)
	{
		*g_EbisuSDKInit = true;
		*g_EbisuProfileInit = true;
		*g_NucleusID = FAKE_BASE_NUCLEUD_ID;

		Q_snprintf(g_OriginAuthCode, 256, "%s", "INVALID_OAUTH_CODE");
		Q_snprintf(g_NucleusToken, 1024, "%s", "INVALID_NUCLEUS_TOKEN");

		if (!isDedicated)
		{
			platform_user_id->SetValue(FAKE_BASE_NUCLEUD_ID);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: runs the EbisuSDK state machine
//-----------------------------------------------------------------------------
void HEbisuSDK_RunFrame()
{
	if (IsOriginDisabled())
	{
		return;
	}

	EbisuSDK_RunFrame();
}

//-----------------------------------------------------------------------------
// Purpose: returns the currently set language
//-----------------------------------------------------------------------------
const char* HEbisuSDK_GetLanguage()
{
	static bool initialized = false;
	static char languageName[32];

	if (initialized)
	{
		return languageName;
	}

	const char* value = nullptr;
	bool useDefault = true;

	if (CommandLine()->CheckParm("-language", &value))
	{
		if (V_LocaleNameExists(value))
		{
			strncpy(languageName, value, sizeof(languageName));
			useDefault = false;
		}
	}

	if (useDefault)
	{
		strncpy(languageName, g_LanguageNames[0], sizeof(languageName));
	}

	languageName[sizeof(languageName) - 1] = '\0';
	initialized = true;

	return languageName;
}

//-----------------------------------------------------------------------------
// Purpose: checks if the EbisuSDK is disabled
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool IsOriginDisabled()
{
	const static bool isDisabled = CommandLine()->CheckParm("-noorigin");
	return isDisabled;
}

//-----------------------------------------------------------------------------
// Purpose: checks if the EbisuSDK is initialized
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool IsOriginInitialized()
{
	if (IsDedicated())
	{
		return true;
	}
	else if ((!(*g_OriginErrorLevel)
		&& (*g_EbisuSDKInit)
		&& (*g_NucleusID)
		&& (*g_EbisuProfileInit)))
	// Note(amos): checks on these are disabled because we should be able to
	// load into the game without an origin or nucleus token. There won't be
	// a token if the game is launched with -offline for example, these are
	// only used for the platform system and online server authentication.
	//	&& (*g_OriginAuthCode)
	//	&& (g_NucleusToken[0])))
	{
		return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: validates if client's persona name meets EA's criteria
// Input  : *pszName -
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool IsValidPersonaName(const char* const pszName, const int nMinLen, const int nMaxLen)
{
	const size_t len = strlen(pszName);

	if (len < nMinLen ||
		len > nMaxLen)
	{
		return false;
	}

	// Check if the name contains any special characters.
	const size_t pos = strspn(pszName, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_");
	return pszName[pos] == '\0';
}

static const char* GetContentID()
{
	static CUtlString contentID;

	if ( contentID.IsEmpty() )
	{
		const char* pszEnvContentID = std::getenv( "ContentId" );
		if ( !pszEnvContentID )
			contentID = "194908";
		else
			contentID = pszEnvContentID;
    }

    return contentID.Get();
}

static void OriginFriendsCache_UpdateFriend( const OriginFriendT *pFriend, FriendCacheUtlHash* pFriendCache )
{
	FriendCacheEntry friendcopy = {};

	if ( !pFriend || !pFriendCache )
		return;

    friendcopy.m_UserId = pFriend->UserId;

    if (pFriend->FriendState == 1) // Not Friends
	{
		friendcopy.m_UserId = pFriend->UserId;
		const UtlHashHandle_t hEntry = pFriendCache->Find( friendcopy );
		if ( hEntry != pFriendCache->InvalidHandle() )
		    pFriendCache->Remove( hEntry );
		return;
    }

    if ( pFriend->FriendState != 2 ) // Mutual friends
		return;

    if ( VALID_CHARSTAR(pFriend->Persona) )
	{
		V_strncpy( friendcopy.m_szPersonaName, pFriend->Persona, sizeof( friendcopy.m_szPersonaName ) );
		friendcopy.m_szPersonaName[sizeof( friendcopy.m_szPersonaName ) - 1] = '\0';
    }

    friendcopy.m_bIsOnline = pFriend->PresenceState > 1; // Any online state
	friendcopy.m_bIsIdle   = pFriend->PresenceState == 5; // Are we idle
    
    const bool bPlayingSameGame = VALID_CHARSTAR(pFriend->MultiplayerId) ? 
            V_strcmp( GetContentID(), pFriend->MultiplayerId ) == 0 : false;

    friendcopy.m_bIsPlayingApex = friendcopy.m_bIsOnline && bPlayingSameGame;

    if ( VALID_CHARSTAR(pFriend->Title) )
    {
		V_strncpy( friendcopy.m_szGameTitle, pFriend->Title, sizeof( friendcopy.m_szGameTitle ) );
		friendcopy.m_szGameTitle[sizeof( friendcopy.m_szGameTitle ) - 1] = '\0';
    }

    bool inserted;
	const UtlHashHandle_t hashHandle = pFriendCache->Insert( friendcopy, &inserted );
	if ( !inserted )
		pFriendCache->Element( hashHandle ) = friendcopy;

    Assert(pFriendCache->Find( friendcopy ) != pFriendCache->InvalidHandle(), "Failed to add origin friend");
}

static int32_t OriginFriendsEnumerationCallback(void* pCtx, intptr_t hHandle, size_t total, size_t avaliable, int32_t err)
{
	NOTE_UNUSED( pCtx );
	NOTE_UNUSED( avaliable );

    if (err != 0)
    {
		v_OriginDestroyHandle( hHandle );
		return 0;
    }

    FriendCacheUtlHash* const pNewOriginFriendsCache = new FriendCacheUtlHash(256);
	OriginFriendT*					 pFriendReadBuffer		= nullptr;

    do
	{
		if ( total == 0 )
			break;

        pFriendReadBuffer = new OriginFriendT[total];
		size_t				    nFriendsRead = 0;

        if ( v_OriginReadEnumerationSync( hHandle, pFriendReadBuffer, sizeof( OriginFriendT ) * total, 0, total, &nFriendsRead ) != 0 || nFriendsRead == 0 )
			break;

        for ( size_t i = 0; i < nFriendsRead; i++ )
		{
			const OriginFriendT *pFriend = &pFriendReadBuffer[i];

			if ( pFriend->FriendState == 2 ) // ORIGIN_FRIENDSTATE_MUTUAL
				OriginFriendsCache_UpdateFriend( pFriend, pNewOriginFriendsCache );
		}

	} while (false);

    if ( pFriendReadBuffer )
		delete[] pFriendReadBuffer;

	{
		AUTO_LOCK( *g_pOriginFriendsCacheLock );
		if ( ( *g_ppCachedFriendsList ) )
			delete ( *g_ppCachedFriendsList );
		( *g_ppCachedFriendsList ) = pNewOriginFriendsCache;
	}

    v_OriginDestroyHandle( hHandle );
	return 0;
}

static CachedFriendState OrignFriendGetCachedState( uint64_t userID )
{
	AUTO_LOCK( *g_pOriginFriendsCacheLock );
	FriendCacheUtlHash *pFriendCache = *g_ppCachedFriendsList;

    if ( pFriendCache == nullptr )
		return NO_CACHE;

	FriendCacheEntry temp = {};
	temp.m_UserId		  = userID;

	const UtlHashHandle_t hHandle = ( *g_ppCachedFriendsList )->Find( temp );
	return hHandle != ( *g_ppCachedFriendsList )->InvalidHandle() ? FOUND : NOT_FOUND;
}

static bool GetFriendsListFromCache(CUtlVector<CUtlString>& vPersonaNames, CUtlVector<CUtlString>& vUserIDs, CUtlVector<CUtlString>& vPlatformTags)
{
    vPersonaNames.Purge();
	vUserIDs.Purge();
	vPlatformTags.Purge();

    AUTO_LOCK( *g_pOriginFriendsCacheLock );
	FriendCacheUtlHash *pFriendsCache = *g_ppCachedFriendsList;

    if ( pFriendsCache == nullptr )
		return false;
	
    const int nFriendsInCache = pFriendsCache->Count();

    vPersonaNames.EnsureCapacity( nFriendsInCache );
	vUserIDs.EnsureCapacity( nFriendsInCache );
	vPlatformTags.EnsureCapacity( nFriendsInCache );

    FOR_EACH_HASH(*pFriendsCache, itr)
    {
		const FriendCacheEntry &friendEntry = pFriendsCache->Element( itr );
		vPersonaNames.AddToTailGetPtr()->Set(friendEntry.m_szPersonaName);
		vUserIDs.AddToTailGetPtr()->Format( "%llu", friendEntry.m_UserId );
		vPlatformTags.AddToTailGetPtr()->SetDirect("PC", 2);
    }

    for (size_t i = 0; i < 200; i++)
    {
		SteamFriend *pSteamFriend = &g_pSteamFriendsList[i];

        if ( pSteamFriend->m_uid == 0 )
			break;

		char fmtBuffer[256];
		uint32_t charCount = 0;
		char	*fmtArgs[] = { pSteamFriend->m_szName };
		if ( v_COM_GetLocalisedStringFormatted( "STEAM_ACCOUNT_PREFIX", fmtArgs, SDK_ARRAYSIZE(fmtArgs), fmtBuffer, sizeof( fmtBuffer ), &charCount ) )
        {
			vPersonaNames.AddToTailGetPtr()->SetDirect( fmtBuffer, ssize_t(charCount) - 1 );
			vUserIDs.AddToTailGetPtr()->Format( "%llu", pSteamFriend->m_uid );
			
            if ( pSteamFriend->m_HardwareType < 8 )
				vPlatformTags.AddToTailGetPtr()->Set( g_ppszPlatformNames[pSteamFriend->m_HardwareType] );
			else
				vPlatformTags.AddToTailGetPtr()->SetDirect( "UNKNOWN", 7 );
        }
    }

	return true;
}

static bool GetFriendsListFromCacheWithPresence( CUtlVector<CUtlString>& vPersonaNames, CUtlVector<CUtlString>& vUserIDs, 
    CUtlVector<CUtlString>& vPlatformTags, CUtlVector<bool>& vOnlineState, 
    CUtlVector<bool>& vIsPlayingApexStates, CUtlVector<CUtlString>& vGameTitles, CUtlVector<bool>& vIdleStates)
{
	vPersonaNames.Purge();
	vUserIDs.Purge();
	vPlatformTags.Purge();
	vOnlineState.Purge();
	vIsPlayingApexStates.Purge();
	vGameTitles.Purge();
	vIdleStates.Purge();

    AUTO_LOCK( *g_pOriginFriendsCacheLock );
	FriendCacheUtlHash *pFriendsCache = *g_ppCachedFriendsList;

	if ( pFriendsCache == nullptr )
		return false;

    const int nFriendsInCache = pFriendsCache->Count();

    vPersonaNames.EnsureCapacity( nFriendsInCache );
	vUserIDs.EnsureCapacity( nFriendsInCache );
	vPlatformTags.EnsureCapacity( nFriendsInCache );
	vOnlineState.EnsureCapacity( nFriendsInCache );
	vIsPlayingApexStates.EnsureCapacity( nFriendsInCache );
	vGameTitles.EnsureCapacity( nFriendsInCache );
	vIdleStates.EnsureCapacity( nFriendsInCache );

    FOR_EACH_HASH( *pFriendsCache, itr )
	{
		const FriendCacheEntry &friendEntry = pFriendsCache->Element( itr );
		vPersonaNames.AddToTailGetPtr()->Set( friendEntry.m_szPersonaName );
		vUserIDs.AddToTailGetPtr()->Format( "%llu", friendEntry.m_UserId );
		vPlatformTags.AddToTailGetPtr()->SetDirect( "PC", 2 );
		vOnlineState.AddToTail( friendEntry.m_bIsOnline );
		vIsPlayingApexStates.AddToTail( friendEntry.m_bIsPlayingApex );
		vGameTitles.AddToTailGetPtr()->Set( friendEntry.m_szGameTitle );
		vIdleStates.AddToTail( friendEntry.m_bIsIdle );
	}

    char szSteamGameTitle[512];
	uint32_t nSteamGameTitleLen = 0;
    if (!v_COM_GetLocalisedStringFormatted("RICHPRESENCE_INGAME", nullptr, 0, szSteamGameTitle, sizeof(szSteamGameTitle), &nSteamGameTitleLen))
    {
		szSteamGameTitle[0] = '\0';
		nSteamGameTitleLen = 0;
    }

    for (size_t i = 0; i < 200; i++)
    {
		SteamFriend *pSteamFriend = &g_pSteamFriendsList[i];

        if ( pSteamFriend->m_uid == 0 )
			break;

        char	 fmtBuffer[256];
		uint32_t charCount = 0;
		char	*fmtArgs[] = { pSteamFriend->m_szName };
		if ( v_COM_GetLocalisedStringFormatted( "STEAM_ACCOUNT_PREFIX", fmtArgs, SDK_ARRAYSIZE( fmtArgs ), fmtBuffer, sizeof( fmtBuffer ), &charCount ) )
		{
			vPersonaNames.AddToTailGetPtr()->SetDirect( fmtBuffer, charCount );
			vUserIDs.AddToTailGetPtr()->Format( "%llu", pSteamFriend->m_uid );

			if ( pSteamFriend->m_HardwareType < 8 )
				vPlatformTags.AddToTailGetPtr()->Set( g_ppszPlatformNames[pSteamFriend->m_HardwareType] );
			else
				vPlatformTags.AddToTailGetPtr()->SetDirect( "UNKNOWN", 7 );

            vOnlineState.AddToTail( pSteamFriend->m_bOnline );
			vIsPlayingApexStates.AddToTail( pSteamFriend->m_bOnline );

            if ( pSteamFriend->m_bOnline )
				vGameTitles.AddToTailGetPtr()->SetDirect(szSteamGameTitle, ssize_t(nSteamGameTitleLen) - 1 );
			else
				vGameTitles.AddToTailGetPtr()->Clear();

            vIdleStates.AddToTail( false );
        }
    }

	return true;
}

static int32_t OriginEventCallback(int32_t eventID, void* pCtx, void* pEventData, size_t count)
{
    switch (eventID)
    {
	case ORIGIN_EVENT_PRESENCE:
    {
		FriendCacheEntry temp = {};
		OriginPresenceT *pPresence = reinterpret_cast<OriginPresenceT *>( pEventData );
		temp.m_UserId				 = pPresence->UserId;

        AUTO_LOCK( *g_pOriginFriendsCacheLock );
		FriendCacheUtlHash *pCachedFriendsList = ( *g_ppCachedFriendsList );
		if ( pCachedFriendsList == nullptr )
            return 0;
        
		const UtlHashHandle_t hHandle	   = pCachedFriendsList->Find( temp );
        if (hHandle == pCachedFriendsList->InvalidHandle())
			return 0;

        FriendCacheEntry &cachedFriend = pCachedFriendsList->Element( hHandle );
		cachedFriend.m_bIsOnline		 = pPresence->PresenceState > 1; // <=1 is offline
		
        cachedFriend.m_bIsPlayingApex = cachedFriend.m_bIsOnline 
            && VALID_CHARSTAR( pPresence->MultiplayerId ) && V_strcmp( GetContentID(), pPresence->MultiplayerId ) == 0;
        
        if ( VALID_CHARSTAR( pPresence->Title ) )
		{
			V_strncpy( cachedFriend.m_szGameTitle, pPresence->Title, sizeof( cachedFriend.m_szGameTitle ) );
			cachedFriend.m_szGameTitle[sizeof( cachedFriend.m_szGameTitle ) - 1] = '\0';
		}
		else
			cachedFriend.m_szGameTitle[0] = '\0';

        cachedFriend.m_bIsIdle = pPresence->PresenceState == 5;
		return 0;
    }
	case ORIGIN_EVENT_FRIENDS:
    {
		FriendCacheUtlHash* pNewCachedFriends = new FriendCacheUtlHash( 256 );
		const OriginFriendT*      pFriends  = reinterpret_cast<const OriginFriendT*>( pEventData );

		for ( size_t i = 0; i < count; i++ )
		{
			const OriginFriendT *pFriend = &pFriends[i];
			if ( pFriend->FriendState == 2 ) // ORIGIN_FRIENDSTATE_MUTUAL
				OriginFriendsCache_UpdateFriend( pFriend, pNewCachedFriends );
		}

        {
			AUTO_LOCK( *g_pOriginFriendsCacheLock );
			if ( ( *g_ppCachedFriendsList ) != nullptr )
				delete ( *g_ppCachedFriendsList );
			( *g_ppCachedFriendsList ) = pNewCachedFriends;
        }

		return 0;
    }
	default:
		break;
    }

    return v_OriginEventCallback( eventID, pCtx, pEventData, count );
}

void VEbisuSDK::Detour(const bool bAttach) const
{
	DetourSetup(&EbisuSDK_RunFrame, &HEbisuSDK_RunFrame, bAttach);
	DetourSetup(&EbisuSDK_GetLanguage, &HEbisuSDK_GetLanguage, bAttach);

	DetourSetup( &v_OriginFriendsEnumerationCallback, &OriginFriendsEnumerationCallback, bAttach );
	DetourSetup( &v_GetCachedFriendState, &OrignFriendGetCachedState, bAttach );
	DetourSetup( &v_OriginEventCallback, &OriginEventCallback, bAttach );
	DetourSetup( &v_GetFriendsListFromCache, &GetFriendsListFromCache, bAttach );
	DetourSetup( &v_GetFriendsListFromCacheWithPresence, &GetFriendsListFromCacheWithPresence, bAttach );
}
