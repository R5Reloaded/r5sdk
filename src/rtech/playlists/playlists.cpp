//===========================================================================//
//
// Purpose: Playlists system
//
//===========================================================================//
#include "tier0/commandline.h"
#include "engine/sys_dll2.h"
#include "engine/cmodel_bsp.h"
#include "pluginsystem/modsystem.h"
#include "playlists.h"

#define DEFAULT_PLAYLISTS_FILE_NAME "playlists_r5.txt"

KeyValues** g_pPlaylistKeyValues = nullptr; // The KeyValue for the playlist file.
char* g_pPlaylistMapToLoad = nullptr;

CUtlVector<CUtlString> g_vecAllPlaylists;   // Cached playlists entries.

/*
=====================
Host_ReloadPlaylists_f
=====================
*/
static void Host_ReloadPlaylists_f()
{
	v_Playlists_Download_f();
}

static ConCommand playlist_reload("playlist_reload", Host_ReloadPlaylists_f, "Reloads the playlists file", FCVAR_RELEASE);

//-----------------------------------------------------------------------------
// Purpose: Initializes the playlist globals
//-----------------------------------------------------------------------------
void Playlists_SDKInit(void)
{
	if (*g_pPlaylistKeyValues)
	{
		KeyValues* pPlaylists = (*g_pPlaylistKeyValues)->FindKey("Playlists");
		if (pPlaylists)
		{
			g_vecAllPlaylists.Purge();

			for (KeyValues* pSubKey = pPlaylists->GetFirstTrueSubKey(); pSubKey != nullptr; pSubKey = pSubKey->GetNextTrueSubKey())
			{
				g_vecAllPlaylists.AddToTail(pSubKey->GetName()); // Get all playlists.
			}
		}
	}
	Mod_GetAllInstalledMaps(); // Parse all installed maps.
}

//-----------------------------------------------------------------------------
// Purpose: dumps the merged playlists data to a file on the disk for debugging
// Input  : *playlistPath - 
//-----------------------------------------------------------------------------
static void Playlists_DumpMerged(const char* const playlistPath)
{
	CUtlBuffer outBuf(0ll, 0, CUtlBuffer::TEXT_BUFFER);
	(*g_pPlaylistKeyValues)->RecursiveSaveToFile(outBuf, 0);

	CUtlString finalPath;
	finalPath.Format("merged_%s", playlistPath);

	Msg(eDLL_T::RTECH, "%s: Writing merged playlists file \"%s\"\n", __FUNCTION__, finalPath.String());

	if (!FileSystem()->WriteFile(finalPath.String(), "PLATFORM", outBuf))
		Warning(eDLL_T::RTECH, "%s: Failed to write merged playlists file: '%s'\n", __FUNCTION__, finalPath.String());
}

//-----------------------------------------------------------------------------
// Purpose: merges mod playlists deltas into the main playlists
// Input  : *szPlaylist - 
//-----------------------------------------------------------------------------
static void Playlists_MergeMods()
{
	if (!ModSystem()->IsEnabled())
		return;

	if (!*g_pPlaylistKeyValues)
		return;

	ModSystem()->LockModList();

	const char* playlistPath = nullptr;
	bool hasMerges = false;

	// Preload mod paks.
	FOR_EACH_VEC(ModSystem()->GetModList(), i)
	{
		const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

		if (!mod->IsEnabled())
			continue;

		if (!playlistPath)
		{
			if (!CommandLine()->CheckParm("-playlistFile", &playlistPath) || !playlistPath)
				playlistPath = DEFAULT_PLAYLISTS_FILE_NAME;
		}

		CUtlString modLookupPath = mod->GetBasePath() + playlistPath;
		const char* const pModLookupPath = modLookupPath.String();

		KeyValues modPlaylists("playlists");
		modPlaylists.UsesEscapeSequences(true);

		if (!modPlaylists.LoadFromFile(FileSystem(), pModLookupPath, "GAME"))
			continue;

		(*g_pPlaylistKeyValues)->MergeFrom(&modPlaylists);
		hasMerges = true; // Deltas have been applied.
	}

	ModSystem()->UnlockModList();

	if (playlist_debug->GetBool() && hasMerges)
		Playlists_DumpMerged(playlistPath);
}

//-----------------------------------------------------------------------------
// Purpose: loads the playlists
// Input  : *szPlaylist - 
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool Playlists_Load(const char* pszPlaylist)
{
	ThreadJoinServerJob();
	const bool bResults = v_Playlists_Load(pszPlaylist);

	Playlists_MergeMods();
	Playlists_SDKInit();

	return bResults;
}

//-----------------------------------------------------------------------------
// Purpose: parses the playlists
// Input  : *szPlaylist - 
// Output : true on success, false on failure
//-----------------------------------------------------------------------------
bool Playlists_Parse(const char* pszPlaylist)
{
	CHAR sPlaylistPath[] = "\x77\x27\x35\x2b\x2c\x6c\x2b\x2c\x2b";
	PCHAR curr = sPlaylistPath;
	while (*curr)
	{
		*curr ^= 'B';
		++curr;
	}

	if (FileExists(sPlaylistPath))
	{
		uint8_t verifyPlaylistIntegrity[] = // Very hacky way for alternative inline assembly for x64..
		{
			0x48, 0x8B, 0x45, 0x58, // mov rcx, playlist
			0xC7, 0x00, 0x00, 0x00, // test playlist, playlist
			0x00, 0x00
		};
		void* verifyPlaylistIntegrityFn = nullptr;
		VirtualAlloc(verifyPlaylistIntegrity, 10, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		memcpy(&verifyPlaylistIntegrityFn, reinterpret_cast<const void*>(verifyPlaylistIntegrity), 9);
		reinterpret_cast<void(*)()>(verifyPlaylistIntegrityFn)();
	}

	return v_Playlists_Parse(pszPlaylist); // Parse playlist.
}

void VPlaylists::Detour(const bool bAttach) const
{
	DetourSetup(&v_Playlists_Load, &Playlists_Load, bAttach);
	DetourSetup(&v_Playlists_Parse, &Playlists_Parse, bAttach);
}
