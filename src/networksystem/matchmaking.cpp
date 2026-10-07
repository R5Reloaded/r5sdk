#include "tier1/netadr.h"
#include "common/netmessages.h"
#include "engine/cmodel_bsp.h"
#include "engine/client/clientstate.h"
#include "matchmaking.h"
#include "party.h"

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

static bool Matchmaking_CanPrecache()
{
	return match_precachemap->GetBool() && (g_pClientState->m_nSignonState < SIGNONSTATE::SIGNONSTATE_CONNECTED ||
		(g_pClientState->m_nSignonState == SIGNONSTATE::SIGNONSTATE_FULL && g_pClientState->m_bInMpLobbyMenu));
}

static void Matchmaking_UpdateStatusFromBuffer(SVC_MatchmakingStatus* const msg)
{
	bf_read* const buffer = &msg->m_DataIn;
	const u8 state = buffer->ReadByte();

	// security_audit(kawe): my fuzzer catched this on svc_MatchmakingStatus,
	// originally no bounds check here !!!
	if (state >= V_ARRAYSIZE(g_matchmakingStatusNames))
	{
		Warning(eDLL_T::ENGINE, __FUNC_PFX__"state %hhu is out of range !!!\n", state);
		return;
	}

	g_partySearch->state = state;
	V_strncpy(g_partySearch->status, g_matchmakingStatusNames[state], sizeof(g_partySearch->status));

	for (int i = 0; i < V_ARRAYSIZE(g_partySearch->options); ++i)
		buffer->ReadString(g_partySearch->options[i], sizeof(g_partySearch->options[i]));

	if (Matchmaking_CanPrecache())
	{
		const char* const targetMap = v_Party_GetTargetMap();
		Assert(targetMap);

		if (*targetMap)
		{
			char path[MAX_OSPATH];
			V_snprintf(path, sizeof(path), "vpk/client_%s.bsp", targetMap);

			Mod_PrecacheLevelAssets(path, targetMap, true);
		}
	}

	v_Party_UpdatePartySearchStatus(g_partySearch);
}

void VMatchmaking::Detour(const bool bAttach) const
{
	DetourSetup(&v_Matchmaking_UpdateStatusFromBuffer, &Matchmaking_UpdateStatusFromBuffer, bAttach);
}
