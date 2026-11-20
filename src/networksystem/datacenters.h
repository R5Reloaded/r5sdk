#ifndef RTECH_DATACENTERS_H
#define RTECH_DATACENTERS_H

#include "tier1/NetAdr.h"

#define MAX_DATACENTER_RECORDS 128
#define MAX_DATACENTER_REGIONS 16
#define MAX_DATACENTER_NAME_LEN 64

struct DatacenterQoSRecord_s
{
	char displayName[MAX_DATACENTER_NAME_LEN];
	char datacenterId[MAX_DATACENTER_NAME_LEN];
	int groupIndex;
	netadr_t address;
	int minPingMs;
	int accumulatedPingMs;
	int averagePingMs;
	int pingsSent;
	int pingsReceived;
	float lastPingSendTime;
};

enum DatacenterSelectionCriterion_e : u8
{
	DSC_NONE,

	// Set by SelectBestDatacenter()
	DSC_INIT,
	DSC_AVG_LATENCY,
	DSC_MIN_LATENCY,
	DSC_PACKET_LOSS,

	// Set by SetDatacenter()
	DSC_USER,

	// Not a criterion!
	DSC_COUNT,
};

struct DatacenterQoSState_s
{
	int numRecords;
	DatacenterQoSRecord_s qosRecords[MAX_DATACENTER_RECORDS];
	int numRegions;
	int regionIndices[MAX_DATACENTER_REGIONS];
	CThreadMutex mutex;
	int unused_5A70;
	short unused_5A74;
	bool unused_5A76;
	bool unused_5A77;
	bool pingInProgress;
	DatacenterSelectionCriterion_e selectionCriterion;
	int selectedDatacenterIndex;
	int selectedRankedDatacenterIndex;
	bool selectionReady;
	int pingGeneration;
};

inline DatacenterQoSState_s* g_datacenterQoS;

void Datacenters_ProcessPingReply(bf_read& reply);

///////////////////////////////////////////////////////////////////////////////
inline float (*v_Datacenters_GetPacketLoss)(const int recordIdx);
inline void (*v_Datacenters_UpdateSelection)(const int recordIdx);
inline int (*v_Datacenters_SelectBestDatacenter_QSort)(const void* recordIdxA, const void* recordIdxB);

///////////////////////////////////////////////////////////////////////////////
class VDatacenters : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("Datacenters_GetPacketLoss", v_Datacenters_GetPacketLoss);
		LogFunAdr("Datacenters_UpdateSelection", v_Datacenters_UpdateSelection);
		LogFunAdr("Datacenters_SelectBestDatacenter_QSort", v_Datacenters_SelectBestDatacenter_QSort);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 48 83 EC ? 48 63 C1 48 69 D8").GetPtr(v_Datacenters_GetPacketLoss);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 63 D9 48 8D 35").GetPtr(v_Datacenters_UpdateSelection);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC ? 8B 19").GetPtr(v_Datacenters_SelectBestDatacenter_QSort);
	}
	virtual void GetVar(void) const
	{
		g_datacenterQoS = CMemory(v_Datacenters_GetPacketLoss)
			.FindPatternSelf("48 8D", CMemory::Direction::DOWN, 40).ResolveRelativeAddressSelf(3, 7).RCast<DatacenterQoSState_s*>();
	}
	virtual void GetCon(void) const {}
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // RTECH_DATACENTERS_H
