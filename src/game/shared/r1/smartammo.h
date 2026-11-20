#ifndef SHARED_SMARTAMMO_H
#define SHARED_SMARTAMMO_H
#include "../predictioncopy.h"

#define SMART_AMMO_TRACKER_MAX_TARGETS 8

class SmartAmmo_WeaponData
{
	void* __vftable;
	int numTargetEntities;
	EHANDLE targetEntities[SMART_AMMO_TRACKER_MAX_TARGETS];
	float fractions[SMART_AMMO_TRACKER_MAX_TARGETS];
	float previousFractions[SMART_AMMO_TRACKER_MAX_TARGETS];
	float currentFractions[SMART_AMMO_TRACKER_MAX_TARGETS];
	int visiblePoints[SMART_AMMO_TRACKER_MAX_TARGETS];
	float lastVisibleTimes[SMART_AMMO_TRACKER_MAX_TARGETS];
	float lastFullLockTimes[SMART_AMMO_TRACKER_MAX_TARGETS];
	EHANDLE storedTargets[SMART_AMMO_TRACKER_MAX_TARGETS];
	float lastNewTargetTime;
	int trackerCount;
	EHANDLE trackerEntities[SMART_AMMO_TRACKER_MAX_TARGETS];
	int trackerLocks[SMART_AMMO_TRACKER_MAX_TARGETS];
	float trackerTimes[SMART_AMMO_TRACKER_MAX_TARGETS];
	char gap_174[4];
	EHANDLE previousTargetEntities[10];
	EHANDLE previousTrackerEntities[10];
	int previousTrackerLocks[10];
};

// Client's class is identical.
typedef SmartAmmo_WeaponData SmartAmmo_WeaponData_Client;

struct SmartAmmo_UnlagData
{
	void Reset()
	{
		sourceEntHandle.Term();
		targetCount = 0;

		for (int i = 0; i < SMART_AMMO_TRACKER_MAX_TARGETS; ++i)
		{
			targetEntHandles[i].Term();
			targetFlags[i] = 0;
		}
	}

	EHANDLE sourceEntHandle;
	int targetCount;

	EHANDLE targetEntHandles[SMART_AMMO_TRACKER_MAX_TARGETS];
	u32 targetFlags[SMART_AMMO_TRACKER_MAX_TARGETS];
};

#endif // SHARED_SMARTAMMO_H
