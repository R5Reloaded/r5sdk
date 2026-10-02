#ifndef RTECH_MATCHMAKING_H
#define RTECH_MATCHMAKING_H

#define MAX_MATCHMAKING_SEARCHES 130
#define MAX_MATCHMAKING_SERVERS 10

struct MatchmakingServerCandidate_s
{
	bool active;				// 0x00
	u8 pad_0001[0x4F];			// 0x01
};
static_assert(sizeof(MatchmakingServerCandidate_s) == 0x50);

struct MatchmakingSearchState_s
{
	bool active;				// 0x0000
	bool aborting;			// 0x0001
	u8 pad_0002[0xED0E];			// 0x0002

	int searchHandle;			// 0xED10
	u8 pad_ED14[0x800];			// 0xED14

	MatchmakingServerCandidate_s servers[MAX_MATCHMAKING_SERVERS]; // 0xF514
	int numServers;				// 0xF834

	bool canRetryServer;			// 0xF838
	u8 pad_F839[7];			// 0xF839
};

MatchmakingSearchState_s* Matchmaking_GetActiveSearch();
void Matchmaking_AbortAllSearches();

inline bool (*v_Matchmaking_RemoveRejectedServer)(netadr_t* const adr);
inline void (*v_Matchmaking_AbortSearchHandle)(const int handle);
inline bool (*v_Matchmaking_UpdateChatroomState)();

// Size = MAX_MATCHMAKING_SEARCHES
inline MatchmakingSearchState_s* g_matchmakingSearches;

///////////////////////////////////////////////////////////////////////////////
class VMatchmaking : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("Matchmaking_RemoveRejectedServer", v_Matchmaking_RemoveRejectedServer);
		LogFunAdr("Matchmaking_AbortSearchHandle", v_Matchmaking_AbortSearchHandle);
		LogFunAdr("Matchmaking_UpdateChatroomState", v_Matchmaking_UpdateChatroomState);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 48 83 EC ? 4C 8B C1 33 DB").GetPtr(v_Matchmaking_RemoveRejectedServer);
		Module_FindPattern(g_GameDll, "0F B7 C1 3D ? ? ? ? 77 ? 8B D0 4C 8D 05 ? ? ? ? 48 69 C0 ? ? ? ? 42 39 0C 00 75 ? 48 C1 E2 ? 48 8D 05 ? ? ? ? 48 03 D0 74 ? 48 8B 0A").GetPtr(v_Matchmaking_AbortSearchHandle);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ? 57 48 81 EC ? ? ? ? 32 DB").GetPtr(v_Matchmaking_UpdateChatroomState);
	}
	virtual void GetVar(void) const
	{
		g_matchmakingSearches = CMemory(v_Matchmaking_RemoveRejectedServer)
			.FindPatternSelf("48 8D", CMemory::Direction::DOWN, 40).ResolveRelativeAddressSelf(3, 7).RCast<MatchmakingSearchState_s*>();
	}
	virtual void GetCon(void) const {}
	virtual void Detour(const bool bAttach) const {};
};
///////////////////////////////////////////////////////////////////////////////

#endif // RTECH_MATCHMAKING_H
