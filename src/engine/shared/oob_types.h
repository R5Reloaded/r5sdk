#ifndef OOB_TYPES_H
#define OOB_TYPES_H
#include "tier1/netadr.h"
#include "ebisusdk/EbisuTypes.h"

struct user_creds_s
{
	netadr_t netAdr;
	int32_t  protocolVer;
	int32_t  challenge;
	uint32_t reservation;
	NucleusID_t personaId;
	char* personaName;

	const char* serverFilter;

	u64 entitlements;
	CUtlVector<u16>* balances;

	bool hasOriginToken;
	bool spectatorClient;
	bool lowViolence;
	bool trial;
	bool forceEAAccess;
	bool originTokenUnknown;

	HardwareType_e platform;
	u8 pad57;

	u64 playlistCRC;
	u64 persistenceVersion;
	u32 persistenceCRC;
	u32 reserved6C;

	NET_SetConVar::Vec_t* userInfo;
};

inline constexpr const char* g_userInfoConVarNames[] =
{
	"",											// #0
	"match_partySize",							// #1
	"match_myDatacenter",						// #2
	"match_myRankedDatacenter",					// #3
	"match_myBestDatacenter",					// #4
	"match_playlist",							// #5
	"match_partyChangeNum",						// #6
	"cl_machineName",							// #7
	"cl_fovScale",								// #8
	"stryder_security",							// #9
	"hasMic",									// #10
	"hasPartialInstall",						// #11
	"hasAnyAssetsWithDiscardedStreamableData",	// #12
	"cl_isUnderAge",							// #13
	"nucleus_id",								// #14
	"nucleus_pid",								// #15
	"pin_sid",									// #16
	"match_myTeam",								// #17
	"cl_predict",								// #18
	"inPartyChat",								// #19
	"match_searching",							// #20
	"match_teamNoFill",							// #21
	"voice_loopback",							// #22
	"cl_lagcompensation",						// #23
	"cl_updaterate_mp",							// #24
	"pin_plat_id",								// #25
	"community",								// #26
	"net_maxroutable",							// #27
	"match_goodReputation",						// #28
	"match_partySub",							// #29
	"cl_predictweapons",						// #30
	"devnet_client_log",						// #31
	"locationInfo",								// #32
	"locationInfo_nucleus",						// #33
	"net_wifi",									// #34
	"net_minimumPacketLossDC",					// #35
};

#endif // OOB_TYPES_H
