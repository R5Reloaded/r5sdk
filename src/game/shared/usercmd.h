//====== Copyright © 1996-2005, Valve Corporation, All rights reserved. =======//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#if !defined( USERCMD_H )
#define USERCMD_H
#ifdef _WIN32
#pragma once
#endif
#include "tier1/bitbuf.h"
#include "mathlib/vector.h"

#include "pingcmd.h"

#include "r1/weapon_playerdata.h"
#include "r1/smartammo.h"
#include "r1/weapon_inventory.h"

//-------------------------------------------------------------------------------------
// Console variables
//-------------------------------------------------------------------------------------
extern ConVar usercmd_frametime_max;
extern ConVar usercmd_frametime_min;

extern ConVar usercmd_dualwield_enable;

//-------------------------------------------------------------------------------------
// Forward declarations
//-------------------------------------------------------------------------------------
class CUserCmd;
class C_UserCmd;

inline CUserCmd*(*CUserCmd__CUserCmd)(CUserCmd* pUserCmd);
inline void(*CUserCmd__Reset)(CUserCmd* pUserCmd);
inline CUserCmd*(*CUserCmd__Copy)(CUserCmd* pDest, CUserCmd* pSource);
inline int(*v_UserCmdServer_ReadFromBuf)(bf_read* buf, CUserCmd* move, CUserCmd* from);
inline int(*v_UserCmdClient_ReadFromBuf)(bf_read* buf, C_UserCmd* move, C_UserCmd* from);
inline bool (*v_UserCmdClient_WriteToBuf)(bf_write* buf, C_UserCmd* from, C_UserCmd* to);

struct UserCmdServerViewState_s
{
	void Reset()
	{
		cameraPos.Init();
		cameraAngles.Init();

		hasThirdPersonAttackFocus = false;
		memset(pad_19, 0, sizeof(pad_19));

		thirdPersonAttackFocus.Init();
	}

	Vector3D cameraPos;                    // +0x00
	QAngle   cameraAngles;                 // +0x0C

	bool     hasThirdPersonAttackFocus;    // +0x18
	char     pad_19[3];

	Vector3D thirdPersonAttackFocus;       // +0x1C
};

static_assert(sizeof(UserCmdServerViewState_s) == 0x28);

struct UserCmdClientPredictionState_s
{
	void Reset()
	{
		predictedEyePosition.Init();
		predictedFieldOfView = 0.0f;

		predictedGravityOrientation.Init(0.0f, 0.0f, 0.0f, 1.0f);

		predictedViewPitch = 0.0f;
		predictedViewYaw = 0.0f;
	}

	Vector3D predictedEyePosition; // +0x00
	float    predictedFieldOfView;         // +0x0C

	Quaternion predictedGravityOrientation; // +0x10

	float predictedViewPitch;          // +0x20
	float predictedViewYaw;            // +0x24
};

static_assert(sizeof(UserCmdClientPredictionState_s) == 0x28);

template <typename TViewState>
class CBaseUserCmd
{
public:
	CBaseUserCmd()
	{
		Reset();
	}

	void Reset()
	{
		command_number = -1;
		tick_count = 0;
		command_time = 0.0f;

		viewangles.Init();
		weaponPuchViewCorrection.Init();

		forwardmove = 0.0f;
		sidemove = 0.0f;
		upmove = 0.0f;

		buttons = 0;

		impulse = 0;
		weaponSelect = WEAPON_INVENTORY_SLOT_INVALID;
		weaponSelectType = -1;

		unk1 = 0;
		unk2 = -1;

		weaponactivity = -1;
		nUnk3C = -1;

		controllermode = false;
		fixangles = false;
		setlastcycleslot = false;

		pad_0x0043[0] = 0;
		pad_0x0043[1] = 0;
		pad_0x0043[2] = 0;

		for (int i = 0; i < MAX_ACTIVE_WEAPONS; i++)
			smartAmmoUnlag[i].Reset();

		hasZiplineCorrection = false;
		ziplineEntityHandle = -1;

		ziplineCorrectionOrigin.Init();
		ziplineCorrectionVelocity.Init();

		ziplineHitTime = -1.0f;

		ziplineHitPosition.Init();
		ziplineHitNormal.Init();

		for (int i = 0; i < NUM_PING_COMMANDS; ++i)
			m_pingCommands[i].Reset();

		randomseed = 0;

		bUnk188 = false;
		pendingPrimaryAttack = false;
		doCameraOverride = false;

		linkedButtonPairPress = false;
		gap18C = 0;

		didPredict = false;

		memset(pad_0x018E, 0, sizeof(pad_0x018E));
		viewState.Reset();

		doKnockBack = false;
		memset(pad_0x01B9, 0, sizeof(pad_0x01B9));

		knockBackEyePos.Init();

		world_angle_tick = 0;
		nUnk1D4 = 0;
		predicted_server_event_ack = 0;
		nUnk1D8 = 0;

		frametime = 0.0f;
	}

	int command_number;
	int tick_count;
	float_t command_time;

	QAngle viewangles;
	QAngle weaponPuchViewCorrection; // Looks like Z is never used?

	float_t forwardmove;
	float_t sidemove;
	float_t upmove;

	int buttons;

	byte impulse;

	WeaponInventorySlot_t weaponSelect;
	char weaponSelectType;

	char unk1;
	char unk2;

	bool bUnk39;
	short weaponactivity;
	int nUnk3C;

	bool controllermode;
	bool fixangles;
	bool setlastcycleslot;

	char pad_0x0043[5];

	SmartAmmo_UnlagData smartAmmoUnlag[MAX_ACTIVE_WEAPONS];

	bool hasZiplineCorrection;
	char pad_0x00D9[3];

	int ziplineEntityHandle;

	Vector3D ziplineCorrectionOrigin;
	Vector3D ziplineCorrectionVelocity;

	float_t ziplineHitTime;

	Vector3D ziplineHitPosition;
	Vector3D ziplineHitNormal;

	PingCommand_s m_pingCommands[NUM_PING_COMMANDS];

	int randomseed;

	bool bUnk188; // something with debouncing?
	bool pendingPrimaryAttack;
	bool doCameraOverride;

	bool linkedButtonPairPress;
	byte gap18C;

	bool didPredict;

	char pad_0x018E[2];

	TViewState viewState;

	bool doKnockBack;
	char pad_0x01B9[3];

	Vector3D knockBackEyePos;

	int world_angle_tick;
	int nUnk1D4;
	int predicted_server_event_ack;
	int nUnk1D8;

	float_t frametime;
};

class CUserCmd : public CBaseUserCmd<UserCmdServerViewState_s>
{
public:
	using BaseClass = CBaseUserCmd<UserCmdServerViewState_s>;

	CUserCmd() : BaseClass()
	{
	}

	void Reset()
	{
		BaseClass::Reset();
	}
};

class C_UserCmd : public CBaseUserCmd<UserCmdClientPredictionState_s>
{
public:
	using BaseClass = CBaseUserCmd<UserCmdClientPredictionState_s>;

	C_UserCmd() : BaseClass()
	{
		timeStamp = 0;
		mouseInput.Init();

		// TODO: !!! REVERSE ENGINEER ME !!!
		memset(unknown_extended, 0, sizeof(unknown_extended));
	}

	void Reset()
	{
		BaseClass::Reset();

		timeStamp = 0;
		mouseInput.Init();

		// TODO: !!! REVERSE ENGINEER ME !!!
		memset(unknown_extended, 0, sizeof(unknown_extended));
	}

	__int64 timeStamp; // time when this ucmd was created.
	Vector2D mouseInput;
	char unknown_extended[144]; // TODO: !!! REVERSE ENGINEER ME !!!
};

static_assert(sizeof(CUserCmd) == 0x1DC);
static_assert(sizeof(C_UserCmd) == 0x280);

int UserCmdServer_ReadFromBuf(bf_read* buf, CUserCmd* move, CUserCmd* from);
int UserCmdClient_ReadFromBuf(bf_read* buf, C_UserCmd* move, C_UserCmd* from);

bool UserCmdClient_WriteToBuf(bf_write* buf, C_UserCmd* from, C_UserCmd* to);

///////////////////////////////////////////////////////////////////////////////
class VUserCmd : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("CUserCmd::CUserCmd", CUserCmd__CUserCmd);
		LogFunAdr("CUserCmd::Reset", CUserCmd__Reset);
		LogFunAdr("CUserCmd::Copy", CUserCmd__Copy);
		LogFunAdr("UserCmdServer_ReadFromBuf", v_UserCmdServer_ReadFromBuf);
		LogFunAdr("UserCmdClient_ReadFromBuf", v_UserCmdClient_ReadFromBuf);
		LogFunAdr("UserCmdClient_WriteToBuf", v_UserCmdClient_WriteToBuf);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? 48 81 C3 ?? ?? ?? ?? 48 83 EF 01 75 EB 33 C0").FollowNearCallSelf().GetPtr(CUserCmd__CUserCmd);
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? 48 8B DF 66 83 FE FF").FollowNearCallSelf().GetPtr(CUserCmd__Reset);
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? 4C 8B 9B ?? ?? ?? ??").FollowNearCallSelf().GetPtr(CUserCmd__Copy);
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? 4C 8B C6 48 81 C6 ?? ?? ?? ??").FollowNearCallSelf().GetPtr(v_UserCmdServer_ReadFromBuf);
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? 8B 4B ?? 4C 8D 35 ?? ?? ?? ?? 8B 53").FollowNearCallSelf().GetPtr(v_UserCmdClient_ReadFromBuf);
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? 41 80 7F ?? ?? 4C 8D 9C 24").FollowNearCallSelf().GetPtr(v_UserCmdClient_WriteToBuf);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // USERCMD_H
