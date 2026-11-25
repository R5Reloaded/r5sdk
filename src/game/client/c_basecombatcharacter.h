//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Defines the client-side representation of CBaseCombatCharacter.
//
// $NoKeywords: $
//===========================================================================//
#ifndef C_BASECOMBATCHARACTER_H
#define C_BASECOMBATCHARACTER_H

#include "c_baseanimatingoverlay.h"
#include "game/shared/r1/weapon_inventory.h"


class C_BaseCombatCharacter : public C_BaseAnimatingOverlay
{
protected:
	float m_flNextAttack;
	float m_lastFiredTime;
	int m_lastFiredWeapon;
	float m_raiseFromMeleeEndTime;
	int m_sharedEnergyCount;
	int m_sharedEnergyTotal;
	int m_sharedEnergyLockoutThreshold;
	float m_lastSharedEnergyRegenTime;
	float m_sharedEnergyRegenRate;
	float m_sharedEnergyRegenDelay;
	float m_lastSharedEnergyTakeTime;
	char gap_16ac[4];
	WeaponInventory_Client m_inventory;
	char m_selectedWeapons[MAX_ACTIVE_WEAPONS];
	char gap_1702[2];
	int m_latestPrimaryWeapons[MAX_ACTIVE_WEAPONS];
	int m_latestPrimaryWeaponsIndexZeroOrOne[MAX_ACTIVE_WEAPONS];
	char m_latestNonOffhandWeapons[MAX_ACTIVE_WEAPONS];
	char m_selectedOffhands[3];
	char m_selectedOffhandsPendingHybridAction[3];
	char m_lastCycleSlot;
	char gap_171d[3];
	EHANDLE m_latestMeleeWeapon;
	int m_weaponPermission;
	float m_weaponDelayEnableTime;
	bool m_weaponDisabledInScript;
	char gap_172d[36];
	char m_weaponDisabledFlags;
	bool m_hudInfo_visibilityTestAlwaysPasses;
	char gap_1753[17];
	int m_contextAction;
	char gap_1768[40];
	float m_phaseShiftTimeStart;
	float m_phaseShiftTimeEnd;
	char gap_1798[228];
};

static_assert(sizeof(C_BaseCombatCharacter) == 0x1880);

#endif // C_BASECOMBATCHARACTER_H