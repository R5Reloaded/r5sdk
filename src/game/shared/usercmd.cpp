//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "usercmd.h"
#include "worldsize.h"
#include "tier1/utlbuffer.h"
#include "game/shared/in_buttons.h"
#include "game/shared/shared_activity.h"

ConVar usercmd_frametime_max("usercmd_frametime_max", "0.100"   , FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY, "The largest amount of simulation seconds a UserCmd can have." );
ConVar usercmd_frametime_min("usercmd_frametime_min", "0.002857", FCVAR_REPLICATED | FCVAR_DEVELOPMENTONLY, "The smallest amount of simulation seconds a UserCmd can have.");

ConVar usercmd_dualwield_enable("usercmd_dualwield_enable", "0", FCVAR_REPLICATED | FCVAR_RELEASE, "Allows setting dual wield cycle slots, and activating multiple inventory weapons from UserCmd.");

template <typename TViewState>
static void UserCmd_ClampSmartAmmoTargets(CBaseUserCmd<TViewState>* const ucmd)
{
	for (int i = 0; i < V_ARRAYSIZE(ucmd->smartAmmoUnlag); i++)
	{
		SmartAmmo_UnlagData& data = ucmd->smartAmmoUnlag[i];

		// Smart ammo can lock up to SMART_AMMO_TRACKER_MAX_TARGETS targets at
		// a time, the server will iterate targetCount times over the networked
		// array to calculate lag compensation for all targeted entities. But
		// since this was never clamped, a client sending an INT_MAX targetCount
		// value will cause the server to implode!
		if (data.targetCount < 0 ||
			data.targetCount > V_ARRAYSIZE(data.targetEntHandles))
		{
			data.Reset();
			continue;
		}

		// note(kawe): no need to check ehandles as these are always clamped
		// to MAX_EDICTS anyways.
	}
}

static void UserCmd_RestoreNonFiniteReal(float_t* const value, const float_t fallback)
{
	if (!IsFinite(*value))
		*value = fallback;
}

template <typename TVector>
static void UserCmd_RestoreNonFiniteVector(TVector* const value, const TVector* const from)
{
	UserCmd_RestoreNonFiniteReal(&value->x, from->x);
	UserCmd_RestoreNonFiniteReal(&value->y, from->y);
	UserCmd_RestoreNonFiniteReal(&value->z, from->z);
}

static void UserCmd_RestoreNonFiniteQuaternion(Quaternion* const value, const Quaternion* const from)
{
	UserCmd_RestoreNonFiniteReal(&value->x, from->x);
	UserCmd_RestoreNonFiniteReal(&value->y, from->y);
	UserCmd_RestoreNonFiniteReal(&value->z, from->z);
	UserCmd_RestoreNonFiniteReal(&value->w, from->w);
}

static void UserCmd_ClampAbsoluteCoord(Vector3D* const coord)
{
	coord->x = Clamp(coord->x, MIN_COORD_FLOAT, MAX_COORD_FLOAT);
	coord->y = Clamp(coord->y, MIN_COORD_FLOAT, MAX_COORD_FLOAT);
	coord->z = Clamp(coord->z, MIN_COORD_FLOAT, MAX_COORD_FLOAT);
}

template <typename TUserCmd>
static void UserCmd_ClampBaseFields(TUserCmd* const ucmd, const TUserCmd* const from, bool tmp)
{
	if (tmp)
	{
		// Bad view vectors crashes clients in PVS code!
		UserCmd_RestoreNonFiniteVector(&ucmd->viewangles, &from->viewangles);
		ucmd->viewangles.Normalize();

		// Bad view vectors crashes clients in PVS code!
		UserCmd_RestoreNonFiniteVector(&ucmd->weaponPuchViewCorrection, &from->weaponPuchViewCorrection);
		ucmd->weaponPuchViewCorrection.Normalize();
	}

	UserCmd_RestoreNonFiniteReal(&ucmd->forwardmove, from->forwardmove);
	UserCmd_RestoreNonFiniteReal(&ucmd->sidemove, from->sidemove);
	UserCmd_RestoreNonFiniteReal(&ucmd->upmove, from->upmove);

	if (ucmd->hasZiplineCorrection)
	{
		UserCmd_RestoreNonFiniteVector(&ucmd->ziplineCorrectionOrigin, &from->ziplineCorrectionOrigin);
		UserCmd_RestoreNonFiniteVector(&ucmd->ziplineCorrectionVelocity, &from->ziplineCorrectionVelocity);
		UserCmd_RestoreNonFiniteVector(&ucmd->ziplineHitPosition, &from->ziplineHitPosition);
		UserCmd_RestoreNonFiniteVector(&ucmd->ziplineHitNormal, &from->ziplineHitNormal);
	}

	// Some players abused a feature of the engine which allows you to perform
	// custom weapon activities. After some research, it appears that only the
	// 'ACT_VM_WEAPON_INSPECT' was supposed to work for standard games, all the
	// other activities appears to be for development or testing, therefore, we
	// should only allow 'ACT_VM_WEAPON_INSPECT' if cheats are disabled.
	if (!sv_cheats->GetBool() && ucmd->weaponactivity != ACT_VM_WEAPON_INSPECT)
		ucmd->weaponactivity = ACT_NONE;

	// security_audit(kawe): the server never validates any of the client's
	// smart ammo inputs after this stage, so we need to do it here!
	UserCmd_ClampSmartAmmoTargets(ucmd);

	const bool dualWieldEnabled = usercmd_dualwield_enable.GetBool();

	// The normal weapon selection code only accepts actual inventory slots.
	// 'WEAPON_INVENTORY_SLOT_EMPTY' is handled specially by the caller, but
	// 'WEAPON_INVENTORY_SLOT_ANY' and values below 'EMPTY' would otherwise
	// pass the signed '< MAX_INVENTORY_WEAPONS' check and index before the
	// 'WeaponInventory::weapons' array.
	if (ucmd->weaponSelect < WEAPON_INVENTORY_SLOT_EMPTY
		|| ucmd->weaponSelect == WEAPON_INVENTORY_SLOT_ANY
		|| ucmd->weaponSelect >= MAX_INVENTORY_WEAPONS)
	{
		ucmd->weaponSelect = WEAPON_INVENTORY_SLOT_INVALID;
	}

	// Client could instruct the server to switch cycle slots for inventory
	// weapons, however, the client could also cycle to the dual wield slots.
	// dual wield weapons should be explicitly enabled to prevent exploitation.
	if (!dualWieldEnabled && ucmd->weaponSelect > WEAPON_INVENTORY_SLOT_ANTI_TITAN)
		ucmd->weaponSelect = WEAPON_INVENTORY_SLOT_ANTI_TITAN;

	// 'weaponSelectType' is an active inventory slot. Unlike
	// 'CPlayer::m_selectedWeapons', most active weapon state has three
	// entries: mainHand, altHand and utility. Values outside this range
	// would result in arbitrary memory reads/writes on the server.
	if (ucmd->weaponSelectType < mainHand || ucmd->weaponSelectType > utility)
	{
		ucmd->weaponSelectType = mainHand;
		ucmd->weaponSelect = WEAPON_INVENTORY_SLOT_INVALID;
	}

	// altHand is used for dual wielding and must not be client selectable
	// while dual wield support is disabled. utility is a separate active
	// inventory slot and should not be folded into the dual wield check.
	if (!dualWieldEnabled && ucmd->weaponSelectType == altHand)
		ucmd->weaponSelectType = mainHand;

	// 'CPlayer::m_selectedWeapons' only contains mainHand and altHand.
	// 'WEAPON_INVENTORY_SLOT_EMPTY' writes to this array directly without
	// the 'activeSlot < utility' guard used by Weapon_UpdateSelection_Internal.
	// utility therefore cannot be used together with EMPTY. Drop the command
	// instead of redirecting it to another active inventory slot.
	if (ucmd->weaponSelect == WEAPON_INVENTORY_SLOT_EMPTY && ucmd->weaponSelectType == utility)
		ucmd->weaponSelect = WEAPON_INVENTORY_SLOT_INVALID;

	UserCmd_RestoreNonFiniteReal(&ucmd->frametime, from->frametime);

	// On the client, the frame time must be within 'usercmd_frametime_min'
	// and 'usercmd_frametime_max'. Testing revealed that speed hacking could
	// be achieved by sending bogus frame times. Clamp the networked frame
	// time to the exact values that the client should be using to make sure
	// it couldn't be circumvented by busting out the client side clamps.
	if (host_timescale->GetFloat() == 1.0f)
	{
		ucmd->frametime = Clamp(ucmd->frametime,
			usercmd_frametime_min.GetFloat(),
			usercmd_frametime_max.GetFloat());
	}
}

static void UserCmd_ClampServerFields(CUserCmd* const ucmd, const CUserCmd* const from)
{
	if (ucmd->doCameraOverride)
	{
		UserCmd_RestoreNonFiniteVector(&ucmd->viewState.cameraPos, &from->viewState.cameraPos);
		UserCmd_ClampAbsoluteCoord(&ucmd->viewState.cameraPos);
		UserCmd_RestoreNonFiniteVector(&ucmd->viewState.cameraAngles, &from->viewState.cameraAngles);
	}

	if (ucmd->viewState.hasThirdPersonAttackFocus)
	{
		UserCmd_RestoreNonFiniteVector(&ucmd->viewState.thirdPersonAttackFocus, &from->viewState.thirdPersonAttackFocus);
	}

	if (ucmd->doKnockBack)
	{
		UserCmd_ClampAbsoluteCoord(&ucmd->knockBackEyePos);
	}
}

static void UserCmd_ClampClientFields(C_UserCmd* const ucmd, const C_UserCmd* const from)
{
	UserCmd_RestoreNonFiniteVector(&ucmd->viewState.predictedEyePosition, &from->viewState.predictedEyePosition);
	UserCmd_RestoreNonFiniteReal(&ucmd->viewState.predictedFieldOfView, ucmd->viewState.predictedFieldOfView);
	UserCmd_RestoreNonFiniteQuaternion(&ucmd->viewState.predictedGravityOrientation, &ucmd->viewState.predictedGravityOrientation);
	UserCmd_RestoreNonFiniteReal(&ucmd->viewState.predictedViewPitch, ucmd->viewState.predictedViewPitch);
	UserCmd_RestoreNonFiniteReal(&ucmd->viewState.predictedViewYaw, ucmd->viewState.predictedViewYaw);
}

static void UserCmd_ClampServer(CUserCmd* const ucmd, const CUserCmd* const from)
{
	UserCmd_ClampBaseFields(ucmd, from, false);
	UserCmd_ClampServerFields(ucmd, from);
}

static void UserCmd_ClampClient(C_UserCmd* const ucmd, const C_UserCmd* const from)
{
	UserCmd_ClampBaseFields(ucmd, from, true);
	UserCmd_ClampClientFields(ucmd, from);
}

//-----------------------------------------------------------------------------
// Purpose: Read in a delta compressed usercommand.
// Input  : *buf - 
//			*move - 
//			*from - 
// Output : random seed
//-----------------------------------------------------------------------------
int UserCmdServer_ReadFromBuf(bf_read* buf, CUserCmd* move, CUserCmd* from)
{
	const int seed = v_UserCmdServer_ReadFromBuf(buf, move, from);
	UserCmd_ClampServer(move, from);

	return seed;
}

int UserCmdClient_ReadFromBuf(bf_read* buf, C_UserCmd* move, C_UserCmd* from)
{
	const int seed = v_UserCmdClient_ReadFromBuf(buf, move, from);
	UserCmd_ClampClient(move, from);

	return seed;
}

bool UserCmdClient_WriteToBuf(bf_write* buf, C_UserCmd* from, C_UserCmd* to)
{
	UserCmd_ClampClient(to, from);
	return v_UserCmdClient_WriteToBuf(buf, from, to);
}

//-----------------------------------------------------------------------------
void VUserCmd::Detour(const bool bAttach) const
{
	DetourSetup(&v_UserCmdServer_ReadFromBuf, &UserCmdServer_ReadFromBuf, bAttach);
	DetourSetup(&v_UserCmdClient_ReadFromBuf, &UserCmdClient_ReadFromBuf, bAttach);
	DetourSetup(&v_UserCmdClient_WriteToBuf, &UserCmdClient_WriteToBuf, bAttach);
}
