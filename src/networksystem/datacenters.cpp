#include "tier1/bitbuf.h"
#include "tier1/fmtstr.h"
#include "datacenters.h"

static ConVar datacenterDebug("datacenterDebug", "0");

static bool Datacenters_IsIndexValid(const int recordIdx)
{
	const bool isValid = recordIdx >= 0 &&
		recordIdx < g_datacenterQoS->numRecords &&
		recordIdx < MAX_DATACENTER_RECORDS;

	Assert(isValid, "Got moGus-sized record index!");
	return isValid;
}

static const char* Datacenters_GetDatacenterName(const int recordIdx)
{
	if (!Datacenters_IsIndexValid(recordIdx))
		return "-<[InVaLiD]>-";

	return g_datacenterQoS->qosRecords[recordIdx].datacenterId;
}

static void Datacenters_SetRankedDatacenter(const int recordIdx)
{
	if (!Datacenters_IsIndexValid(recordIdx))
		return;

	g_datacenterQoS->selectedRankedDatacenterIndex = recordIdx;
	match_myRankedDatacenter->SetValue(g_datacenterQoS->qosRecords[recordIdx].datacenterId);
}

static float Datacenters_GetPacketLoss(const int recordIdx)
{
	Assert(recordIdx >= 0);
	Assert(recordIdx < g_datacenterQoS->numRecords);

	const DatacenterQoSRecord_s& datacenter = g_datacenterQoS->qosRecords[recordIdx];
	const float currentTime = static_cast<float>(Plat_FloatTime());

	int pingsSent = datacenter.pingsSent;
	const int pingsReceived = datacenter.pingsReceived;

	if (datacenter.lastPingSendTime + match_pingWaveInterval->GetFloat() >= currentTime &&
		--pingsSent <= pingsReceived)
	{
		pingsSent = pingsReceived;
	}

	return 1.0f - (static_cast<float>(pingsReceived) / static_cast<float>(pingsSent));
}

static int Datacenters_SelectBestDatacenter(const int recordIdxA, const int recordIdxB, const bool doDebug)
{
	Assert(recordIdxA >= 0);
	Assert(recordIdxA < MAX_DATACENTER_RECORDS);

	// note(kawe): this func was rebuilt so we can enable debug again, and
	// even though they stripped the msg emitter, they still construct the
	// string unconditionally. This rebuild will only format the string if
	// the debug var is set and therefore not burn CPU unnecessarily.

	if (recordIdxA == recordIdxB)
		return recordIdxA;

	if (recordIdxB < 0)
	{
		g_datacenterQoS->selectionCriterion = DSC_INIT;
		return recordIdxA;
	}

	Assert(recordIdxB < MAX_DATACENTER_RECORDS);

	const DatacenterQoSRecord_s& datacenterA = g_datacenterQoS->qosRecords[recordIdxA];
	const DatacenterQoSRecord_s& datacenterB = g_datacenterQoS->qosRecords[recordIdxB];

	CFmtStr1024 dbgMsg;

	if (doDebug)
	{
		dbgMsg.AppendFormat("%s [%d] '%s' vs [%d] '%s'",
			__FUNC_PFX__,
			recordIdxA,
			datacenterA.datacenterId,
			recordIdxB,
			datacenterB.datacenterId);
	}

	const int averagePingA = datacenterA.averagePingMs;
	const int averagePingB = datacenterB.averagePingMs;

	const int qosUnits = ping_qos_units->GetInt();
	Assert(qosUnits > 0);

	const u32 averagePingBucketA = static_cast<u32>(averagePingA >= 0 ? averagePingA : -1) / static_cast<u32>(qosUnits);
	const u32 averagePingBucketB = static_cast<u32>(averagePingB >= 0 ? averagePingB : -1) / static_cast<u32>(qosUnits);

	if (doDebug)
	{
		dbgMsg.AppendFormat(" avg: %d (%u) > %d (%u)",
			averagePingA,
			averagePingBucketA,
			averagePingB,
			averagePingBucketB);
	}

	if (averagePingBucketA > averagePingBucketB)
	{
		if (doDebug)
			Msg(eDLL_T::ENGINE, "%s\n", dbgMsg.String());

		g_datacenterQoS->selectionCriterion = DSC_AVG_LATENCY;
		return recordIdxB;
	}

	const float packetLossA = Datacenters_GetPacketLoss(recordIdxA);
	const float packetLossB = Datacenters_GetPacketLoss(recordIdxB);

	if (doDebug)
		dbgMsg.AppendFormat(" loss: %f < %f", packetLossA, packetLossB);

	if (ping_usePacketLoss->GetBool() && packetLossA > packetLossB)
	{
		if (doDebug)
			Msg(eDLL_T::ENGINE, "%s\n", dbgMsg.String());

		g_datacenterQoS->selectionCriterion = DSC_PACKET_LOSS;
		return recordIdxB;
	}

	if (doDebug)
	{
		dbgMsg.AppendFormat(" min: %d < %d",
			datacenterA.minPingMs,
			datacenterB.minPingMs);
	}

	if (datacenterA.minPingMs < 0 ||
		datacenterA.minPingMs >= datacenterB.minPingMs)
	{
		return recordIdxB;
	}

	if (doDebug)
		Msg(eDLL_T::ENGINE, "%s\n", dbgMsg.String());

	g_datacenterQoS->selectionCriterion = DSC_MIN_LATENCY;
	return recordIdxA;
}

// note(kawe): the game's qsort callback returned a bool instead of a proper
// negative/zero/positive comparison result. This rebuild fixes the comparator
// while reusing SelectBestDatacenter() without its debug formatting overhead.
static int Datacenters_SelectBestDatacenter_QSort(const void* recordIdxA, const void* recordIdxB)
{
	const int indexA = *static_cast<const int*>(recordIdxA);
	const int indexB = *static_cast<const int*>(recordIdxB);

	if (indexA == indexB)
		return 0;

	const DatacenterSelectionCriterion_e selectionCriterion = g_datacenterQoS->selectionCriterion;

	const bool aIsBetter = Datacenters_SelectBestDatacenter(indexA, indexB, false) == indexA;
	const bool bIsBetter = Datacenters_SelectBestDatacenter(indexB, indexA, false) == indexB;

	g_datacenterQoS->selectionCriterion = selectionCriterion;

	if (aIsBetter != bIsBetter)
		return aIsBetter ? -1 : 1;

	// Neither record strictly beats the other
	return indexA < indexB ? -1 : 1;
}

static void Datacenters_UpdateSelection(const int recordIdx)
{
	Assert(recordIdx >= 0);
	Assert(recordIdx < g_datacenterQoS->numRecords);

	const DatacenterQoSRecord_s& datacenter = g_datacenterQoS->qosRecords[recordIdx];

	Assert(datacenter.groupIndex >= 0);
	Assert(datacenter.groupIndex < g_datacenterQoS->numRegions);

	int& regionRecordIdx = g_datacenterQoS->regionIndices[datacenter.groupIndex];

	if (Datacenters_SelectBestDatacenter(recordIdx, regionRecordIdx, false) == recordIdx)
	{
		regionRecordIdx = recordIdx;
	}

	if (Datacenters_SelectBestDatacenter(recordIdx,
		g_datacenterQoS->selectedDatacenterIndex,
		datacenterDebug.GetBool()) != recordIdx)
	{
		return;
	}

	if (g_datacenterQoS->selectedDatacenterIndex != recordIdx)
	{
		const float packetLoss = Datacenters_GetPacketLoss(recordIdx);
		const char* const datacenterName = Datacenters_GetDatacenterName(recordIdx);

		Msg(eDLL_T::ENGINE,
			"%s: datacenter %d -> %d '%s' [ping: min %d ms, avg %d ms, total %d ms; loss: %.2f%%]\n",
			__FUNC__,
			g_datacenterQoS->selectedDatacenterIndex,
			recordIdx,
			datacenterName,
			datacenter.minPingMs,
			datacenter.averagePingMs,
			datacenter.accumulatedPingMs,
			packetLoss * 100.0f);

		Assert(datacenter.averagePingMs > 0);
		Assert(packetLoss < 100.0f);

		g_datacenterQoS->selectedDatacenterIndex = recordIdx;

		Datacenters_SetRankedDatacenter(recordIdx);

		match_myDatacenter->SetValue(datacenterName);
		match_myBestDatacenter->SetValue(datacenterName);
	}

	if (!g_datacenterQoS->selectionReady && datacenter.pingsSent >= ping_minSentForChoice->GetInt())
		g_datacenterQoS->selectionReady = true;
}

void Datacenters_ProcessPingReply(bf_read& reply)
{
	const u8 recordIdx = static_cast<u8>(reply.ReadByte());
	const float sendTime = reply.ReadBitFloat();

	const int pingGeneration = reply.ReadLong();
	const float latency = static_cast<float>(Plat_FloatTime()) - sendTime;

	if (pingGeneration != g_datacenterQoS->pingGeneration || latency < 0.0f)
		return;

	{
		AUTO_LOCK(g_datacenterQoS->mutex);

		Assert(g_datacenterQoS->numRecords >= 0);
		Assert(g_datacenterQoS->numRecords <= MAX_DATACENTER_RECORDS);

		if (recordIdx >= g_datacenterQoS->numRecords)
			return;

		DatacenterQoSRecord_s& datacenter = g_datacenterQoS->qosRecords[recordIdx];

		const int latencyMs = static_cast<int>(latency * 1000.0f);
		const int pingsReceived = Min(datacenter.pingsReceived + 1, datacenter.pingsSent);

		datacenter.pingsReceived = pingsReceived;
		datacenter.accumulatedPingMs += latencyMs;

		if (datacenter.minPingMs < 0 ||
			datacenter.minPingMs >= latencyMs)
		{
			datacenter.minPingMs = latencyMs;
		}

		Assert(pingsReceived > 0);

		if (pingsReceived > 0)
		{
			datacenter.averagePingMs = static_cast<int>(
				static_cast<float>(datacenter.accumulatedPingMs) /
				static_cast<float>(pingsReceived));
		}
	}

	Datacenters_UpdateSelection(recordIdx);
}

///////////////////////////////////////////////////////////////////////////////
void VDatacenters::Detour(const bool bAttach) const
{
	DetourSetup(&v_Datacenters_GetPacketLoss, &Datacenters_GetPacketLoss, bAttach);
	DetourSetup(&v_Datacenters_UpdateSelection, &Datacenters_UpdateSelection, bAttach);
	DetourSetup(&v_Datacenters_SelectBestDatacenter_QSort, &Datacenters_SelectBestDatacenter_QSort, bAttach);
}
