#ifndef COMMUNITY_PARTY_H
#define COMMUNITY_PARTY_H

struct PartySearch_s
{
	int status;
	char playlists[7][64];
};

extern const char* Party_GetTargetPlaylist();

/* ==== PARTY ================================================================================================================================================== */
inline const char* (*v_Party_GetTargetMap)();

inline PartySearch_s* g_partySearch;
inline const char* g_localTargetPartyPlaylist; // size = MAX_PLAYLIST_NAME

///////////////////////////////////////////////////////////////////////////////
class VCommunityParty : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("Party_GetTargetMap", v_Party_GetTargetMap);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 89 5C 24 ? 57 48 83 EC ? 83 3D ? ? ? ? ? 48 8D 3D").GetPtr(v_Party_GetTargetMap);
	}
	virtual void GetVar(void) const
	{
		CMemory(v_Party_GetTargetMap).OffsetSelf(0xA).FindPatternSelf("83 3D").ResolveRelativeAddressSelf(2, 7).GetPtr(g_partySearch);
		CMemory(v_Party_GetTargetMap).OffsetSelf(0x80).FindPatternSelf("48 8D").ResolveRelativeAddressSelf(3, 7).GetPtr(g_localTargetPartyPlaylist);
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { }
};
///////////////////////////////////////////////////////////////////////////////

#endif // COMMUNITY_PARTY_H
