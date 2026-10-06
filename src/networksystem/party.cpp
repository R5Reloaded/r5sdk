//=============================================================================//
//
// Purpose: Community parties implenentation
//
//=============================================================================//
#include "rtech/playlists/playlists.h"
#include "party.h"

//-----------------------------------------------------------------------------
// Purpose: returns the target playlist the party is looking for
//-----------------------------------------------------------------------------
const char* Party_GetTargetPlaylist()
{
	// Reconstruction of Party_GetTargetMap(), except we return the playlist
	// that is used to retrieve the map in Party_GetTargetMap().

	if (g_partySearch->status == 3)
	{
		if ((atoi(g_partySearch->playlists[3]) >= 0) && (atoi(g_partySearch->playlists[4]) >= 0))
		{
			const char* const toRet = g_partySearch->playlists[1];

			if (*toRet)
				return toRet;
		}
	}

	if (*g_localTargetPartyPlaylist)
		return g_localTargetPartyPlaylist;

	const char* const matchPlaylist = match_playlist->GetString();

	if (*matchPlaylist)
		return matchPlaylist;

	return v_Playlists_GetCurrent();
}
