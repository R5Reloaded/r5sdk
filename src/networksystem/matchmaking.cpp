#include "tier1/netadr.h"
#include "matchmaking.h"

MatchmakingSearchState_s* Matchmaking_GetActiveSearch()
{
	for (int i = 0; i < MAX_MATCHMAKING_SEARCHES; ++i)
	{
		MatchmakingSearchState_s& search = g_matchmakingSearches[i];

		if (search.active)
			return &search;
	}

	return nullptr;
}

void Matchmaking_AbortAllSearches()
{
	for (int i = 0; i < MAX_MATCHMAKING_SEARCHES; ++i)
	{
		MatchmakingSearchState_s& search = g_matchmakingSearches[i];

		if (!search.active)
			continue;

		search.aborting = true;
		v_Matchmaking_AbortSearchHandle(search.searchHandle);
		search.active = false;
	}

	v_Matchmaking_UpdateChatroomState();
}