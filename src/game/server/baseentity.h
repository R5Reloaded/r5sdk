//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#ifndef BASEENTITY_H
#define BASEENTITY_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"
#include "tier1/string_t.h"
#include "game/server/iservernetworkable.h"
#include "game/server/iserverentity.h"
#include "engine/gl_model_private.h"
#include "game/shared/collisionproperty.h"
#include "game/shared/shareddefs.h"
#include "networkproperty.h"
//#include "entitylist.h"
#include "entityoutput.h"
#include "vscript/ivscript.h"

//-----------------------------------------------------------------------------

typedef void (CBaseEntity::* BASEPTR)(void);
typedef void (CBaseEntity::* ENTITYFUNCPTR)(CBaseEntity* pOther);
typedef void (CBaseEntity::* USEPTR)(CBaseEntity* pActivator, CBaseEntity* pCaller, USE_TYPE useType, float value);

//-----------------------------------------------------------------------------
// Purpose: think contexts
//-----------------------------------------------------------------------------
struct thinkfunc_t
{
	BASEPTR		m_pfnThink;
	bool		m_fireBeforeBaseThink;
	string_t	m_iszContext;
	int			m_nNextThinkTick;
	int			m_nLastThinkTick;
};

//-----------------------------------------------------------------------------
// Purpose: debug overlay bits
//-----------------------------------------------------------------------------
enum DebugOverlayBits_e
{
	OVERLAY_TEXT_BIT			=	1<<0,		// show text debug overlay for this entity
	OVERLAY_NAME_BIT			=	1<<1,		// show name debug overlay for this entity
	OVERLAY_BBOX_BIT			=	1<<2,		// show bounding box overlay for this entity
	OVERLAY_PIVOT_BIT			=	1<<3,		// show pivot for this entity
	OVERLAY_MESSAGE_BIT			=	1<<4,		// show messages for this entity
	OVERLAY_ABSBOX_BIT			=	1<<5,		// show abs bounding box overlay
	OVERLAY_RBOX_BIT			=   1<<6,		// show the rbox overlay
	OVERLAY_SHOW_BLOCKSLOS		=	1<<7,		// show entities that block NPC LOS
	OVERLAY_ATTACHMENTS_BIT		=	1<<8,		// show attachment points
	OVERLAY_AUTOAIM_BIT			=	1<<9,		// Display autoaim radius
	OVERLAY_NPC_ASSAULT_VOLUME	=	1<<11,		// show the npc's assault volume
	OVERLAY_NPC_SELECTED_BIT	=	1<<12,		// the npc is current selected
	OVERLAY_NPC_NODE_BIT		=	1<<13,		// show the nearest and squad assigned node of this npc
	OVERLAY_NPC_ROUTE_BIT		=	1<<14,		// draw the route for this npc
	OVERLAY_NPC_TRIANGULATE_BIT =	1<<15,		// draw the triangulation for this npc
	OVERLAY_NPC_ZAP_BIT			=	1<<16,		// destroy the NPC
	OVERLAY_NPC_ENEMIES_BIT		=	1<<17,		// show npc's enemies
	OVERLAY_NPC_CONDITIONS_BIT	=	1<<18,		// show NPC's current conditions
	OVERLAY_NPC_SQUAD_BIT		=	1<<19,		// show npc squads
	OVERLAY_NPC_TASK_BIT		=	1<<20,		// show npc task details
	OVERLAY_NPC_FOCUS_BIT		=	1<<21,		// show line to npc's enemy and target
	OVERLAY_NPC_SIGHT_BIT		=	1<<22,		// show npc's sight volume
	OVERLAY_NPC_KILL_BIT		=	1<<23,		// kill the NPC, running all appropriate AI.

	OVERLAY_WC_CHANGE_ENTITY	=	1<<24,		// object changed during WC edit
	OVERLAY_BUDDHA_MODE			=	1<<25,		// take damage but don't die

	OVERLAY_NPC_STEERING_REGULATIONS	=	1<<26,	// Show the steering regulations associated with the NPC

	OVERLAY_TASK_TEXT_BIT		=	1<<27,		// show task and schedule names when they start

	OVERLAY_PROP_DEBUG			=	1<<28,

	OVERLAY_NPC_RELATION_BIT	=	1<<29,		// show relationships between target and all children

	OVERLAY_VIEWOFFSET			=	1<<30,		// show view offset
};

//-----------------------------------------------------------------------------
// Purpose: timed overlays
//-----------------------------------------------------------------------------
struct TimedOverlay_t
{
	char* msg;
	int				msgEndTime;
	int				msgStartTime;
	TimedOverlay_t* pNextTimedOverlay;
};

class CBaseEntity : public IServerEntity
{
public:
	void DrawDebugGeometryOverlays(void)
	{
		const int index = 34;
		CallVFunc<void>(index, this);
	}
	void DrawDebugTextOverlays(void)
	{
		const int index = 35;
		CallVFunc<void>(index, this);
	}

	// non-virtual methods. Don't override these!
public:
	CCollisionProperty* CollisionProp();
	const CCollisionProperty* CollisionProp() const;
	CServerNetworkProperty* NetworkProp();
	const CServerNetworkProperty* NetworkProp() const;

	model_t*		GetModel(void);
	int				GetModelIndex(void) const; // Virtual in-engine!
	string_t		GetModelName(void) const;  // Virtual in-engine!
	const Vector3D& GetViewOffset(void) const { return m_vecViewOffset; }
	void SetViewOffset(const Vector3D& viewOffset);
	const Vector3D& GetVecPrevAbsOrigin(void) const { return m_vecPrevAbsOrigin; }

	inline edict_t GetEdict(void) const { return NetworkProp()->GetEdict(); }
	inline string_t GetEntityName(void) const { return m_iName; }

	inline int		GetFlags(void) const { return m_fFlags; }

	const HSCRIPT GetScriptInstance();

	inline int GetDebugOverlays() const { return m_debugOverlays; }
	inline const TimedOverlay_t* GetTimedOverlay() const { return m_pTimedOverlay; }

protected:
	void* m_collideable;
	void* m_networkable;
	int genericKeyValueCount;
	char gap_24[4];
	void* genericKeyValues;
	void* m_pfnMoveDone;
	void* m_pfnThink;
	CServerNetworkProperty m_Network;
	char padding_or_unknown[8];
	string_t m_ModelName;
	int m_entIndex;
	char gap_74[4];
	string_t* m_iClassname;
	float m_flAnimTime;
	float m_flSimulationTime;
	int m_creationTick;
	int m_nLastThinkTick;
	int m_PredictableID;
	int touchStamp;
	CUtlVector<thinkfunc_t> m_aThinkFunctions;
	float m_entitySpawnTime;
	EHANDLE m_spawner;
	bool m_wantsDamageCallbacks;
	bool m_wantsDeathCallbacks;
	char gap_c2[2];
	int m_nNextThinkTick;
	int m_fEffects;
	bool m_thinkNextFrame;
	char gap_cd[3];
	string_t m_target;
	int m_networkedFlags;
	char m_nRenderFX;
	char m_nRenderMode;
	short m_nModelIndex;
	color32 m_clrRender;
	char m_clIntensity;
	char gap_e5[3];
	int m_desiredHibernationType;
	int m_scriptMinHibernationType;
	int m_minSelfAndDescendantHibernationType;
	int m_actualHibernationType;
	int m_hibernationQueueIndex;
	bool m_bRenderWithViewModels;
	char gap_fd[3];
	int m_nameVisibilityFlags;
	float m_cloakEndTime;
	float m_cloakFadeInEndTime;
	float m_cloakFadeOutStartTime;
	float m_cloakFadeInDuration;
	float m_cloakFlickerAmount;
	float m_cloakFlickerEndTime;
	float m_cloakFadeOutDuration;
	Vector3D m_highlightParams[16];
	int m_highlightFunctionBits[8];
	float m_highlightServerFadeBases[2];
	float m_highlightServerFadeStartTimes[2];
	float m_highlightServerFadeEndTimes[2];
	int m_highlightServerContextID;
	int m_highlightTeamBits;
	float m_nextGrenadeTargetTime;
	float m_grenadeTargetDebounce;
	int m_nSimulationTick;
	int m_fDataObjectTypes;
	int m_iEFlags;
	int m_fFlags;
	string_t m_iName;
	int m_scriptNameIndex;
	int m_instanceNameIndex;
	char m_scriptName[64];
	char m_instanceName[64];
	string_t m_holdUsePrompt;
	string_t m_pressUsePrompt;
	float m_attachmentLerpStartTime;
	float m_attachmentLerpEndTime;
	Vector3D m_attachmentLerpStartOrigin;
	Vector3D m_attachmentLerpStartAngles;
	int m_parentAttachmentType;
	int m_parentAttachmentIndex;
	int m_parentAttachmentHitbox;
	int m_parentAttachmentModel;
	char m_MoveType;
	char m_MoveCollide;
	char gap_30a[2];
	int m_RestoreMoveTypeOnDetach;
	EHANDLE m_hMoveParent;
	EHANDLE m_hMoveChild;
	EHANDLE m_hMovePeer;
	bool m_bIsActiveChild;
	bool m_bPrevAbsOriginValid;
	char gap_31e[2];
	int m_descendantZiplineCount;
	char gap_324[4];
	CCollisionProperty m_Collision;
	EHANDLE m_hOwnerEntity;
	int m_CollisionGroup;
	int m_contents;
	bool m_collideWithOwner;
	char gap_3ad[3];
	int m_baseSolidType;
	char gap_3b4[4];
	void* m_pPhysicsObject;
	float m_flNavIgnoreUntilTime;
	EHANDLE m_hGroundEntity;
	float m_flGroundChangeTime;
	Vector3D m_vecBaseVelocity;
	EHANDLE m_baseVelocityEnt;
	Vector3D m_vecAbsVelocity;
	Vector3D m_vecAngVelocity;
	char gap_3f4[12];
	matrix3x4_t m_rgflCoordinateFrame;
	float m_flFriction;
	float m_flLocalTime;
	float m_flVPhysicsUpdateLocalTime;
	float m_flMoveDoneTime;
	int m_nPushEnumCount;
	Vector3D m_vecPrevAbsOrigin;
	Vector3D m_vecAbsOrigin;
	Vector3D m_angAbsRotation;
	Vector3D m_vecVelocity;
	char gap_474[4];
	string_t m_iParent;
	int m_iHammerID;
	float m_flSpeed;
	int m_iMaxHealth;
	int m_iHealth;
	void* m_pfnTouch;
	bool m_bClientSideRagdoll;
	char m_lifeState;
	char gap_49a[2];
	EHANDLE m_scriptNetData;
	int m_phaseShiftFlags;
	char m_baseTakeDamage;
	char gap_4a5[3];
	int m_invulnerableToDamageCount;
	char m_passDamageToParent;
	char gap_4ad[3];
	Vector3D m_deathVelocity;
	float m_lastTitanFootstepDamageTime;
	float m_flMaxspeed;
	int m_visibilityFlags;
	COutputEvent m_OnUser1;
	COutputEvent m_OnDeath;
	COutputEvent m_OnDestroy;
	int m_cellWidth;
	int m_cellBits;
	int m_cellX;
	int m_cellY;
	int m_cellZ;
	Vector3D m_localOrigin;
	Vector3D m_localAngles;
	Vector3D m_vecViewOffset;
	int m_ListByClass;
	char gap_57c[4];
	CBaseEntity* m_pPrevByClass;
	CBaseEntity* m_pNextByClass;
	int m_iInitialTeamNum;
	int m_iTeamNum;
	int m_teamMemberIndex;
	int m_squadID;
	int m_grade;
	int m_ignorePredictedTriggerFlags;
	int m_passThroughFlags;
	int m_passThroughThickness;
	float m_passThroughDirection;
	int m_spawnflags;
	float m_flGravity;
	float m_entityFadeDist;
	EHANDLE m_dissolveEffectEntityHandle;
	float m_fadeDist;
	string_t m_iSignifierName;
	int m_collectedInvalidateFlags;
	bool m_collectingInvalidateFlags;
	char gap_5d5[3];
	int m_lagCompensationCounter;
	bool m_bLagCompensate;
	bool m_bNetworkQuantizeOriginAndAngles;
	bool m_bForcePurgeFixedupStrings;
	char gap_5df[1];
	int m_debugOverlays;
	char gap_5e4[4];
	TimedOverlay_t* m_pTimedOverlay;
	char m_ScriptScope[32];
	char m_hScriptInstance[8];
	string_t m_iszScriptId;
	EHANDLE m_bossPlayer;
	int m_usableType;
	int m_usablePriority;
	float m_usableDistanceOverride;
	float m_usableFOV;
	float m_usePromptSize;
	bool m_hasDispatchedSpawn;
	bool m_bDoDestroyCallback;
	bool m_bDoPusherCallback;
	bool m_bDoPreSpawnCallback;
	bool m_bDoOnSpawnedCallback;
	char gap_63d[3];
	float m_spottedBeginTimes[128];
	float m_spottedLatestTimes[128];
	i64 m_spottedByTeams[4]; // TODO: team handles are 64bit, create type in SDK
	char m_minimapData[88];
	int m_shieldHealth;
	int m_shieldHealthMax;
	int m_firstChildEntityLink;
	int m_firstParentEntityLink;
	bool m_bIsSoundCodeControllerValueSet;
	char gap_ac9[3];
	float m_flSoundCodeControllerValue;
	float m_pusherWithChildrenRadius;
	int m_childPusherMoveHandlerCount;
	bool m_inWater;
	char gap_ad9[7];
	void* m_statusEffectPlugin;
	i64 m_realmsBitMask;
	char m_realmsTransmitMaskCached[16];
	int m_realmsTransmitMaskCachedSerialNumber;
};
static_assert(sizeof(CBaseEntity) == 0xB08);

inline const HSCRIPT(*v_CBaseEntity__GetScriptInstance)(CBaseEntity* thisp);

///////////////////////////////////////////////////////////////////////////////
class VCBaseEntity : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("CBaseEntity::GetScriptInstance", v_CBaseEntity__GetScriptInstance);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 8B C4 56 41 56 48 81 EC ?? ?? ?? ?? 48 83 B9").GetPtr(v_CBaseEntity__GetScriptInstance);
	}
	virtual void GetVar(void) const {}
	virtual void GetCon(void) const {}
	virtual void Detour(const bool bAttach) const {};
};
///////////////////////////////////////////////////////////////////////////////

#endif // BASEENTITY_H
