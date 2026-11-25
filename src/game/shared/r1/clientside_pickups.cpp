/*
example usage (scripts):
script        ClientSidePickups_InitType( 1, $"mdl/weapons_r5/loot/w_loot_wep_iso_phoenix_kit_v1.rmdl", $"P_LL_med_drone_jet_ctr_loop" )
script_client ClientSidePickups_InitType( 1, $"mdl/weapons_r5/loot/w_loot_wep_iso_phoenix_kit_v1.rmdl", $"P_LL_med_drone_jet_ctr_loop" )
script        GrantClientSidePickup( gp()[ 0 ], 1, 20, gp()[ 0 ].GetOrigin(), 100, 0, 1 )
*/
#include "vscript/languages/squirrel_re/include/sqvm.h"
#include "clientside_pickups.h"

bool ClientSidePickup_Spawn(unsigned int pickupType, unsigned int amount, Vector3D* const origin, unsigned int CSPUFLAGflags, unsigned int customFlags, unsigned int recieptID)
{
	// security_audit(kawe): my fuzzer locked the client up on svc_GrantClientSidePickup,
	// added this here to make sure this can't be exploited.
	if (amount > CLIENTSIDE_PICKUP_MAX_SPAWN_COUNT)
	{
		Warning(eDLL_T::CLIENT, __FUNC_PFX__"tried to spawn %u items, max allowed is %u.\n", amount, CLIENTSIDE_PICKUP_MAX_SPAWN_COUNT);
		return false;
	}

	return v_ClientSidePickup_Spawn(pickupType, amount, origin, CSPUFLAGflags, customFlags, recieptID);
}

bool Script_GrantClientSidePickup_Internal(void* entity, int pickupType, unsigned int amount, Vector3D* origin, int CSPUFLAGflags, int customFlags, int recieptID)
{
	// also added this here to let scripters know there's a client side limit now.
	if (amount > CLIENTSIDE_PICKUP_MAX_SPAWN_COUNT)
	{
		v_SQVM_ScriptError("Tried to spawn %u items, max allowed is %u.\n", amount, CLIENTSIDE_PICKUP_MAX_SPAWN_COUNT);
		return false;
	}

	return v_Script_GrantClientSidePickup_Internal(entity, pickupType, amount, origin, CSPUFLAGflags, customFlags, recieptID);
}

void V_ClientSidePickups::Detour(const bool bAttach) const
{
	DetourSetup(&v_ClientSidePickup_Spawn, ClientSidePickup_Spawn, bAttach);
	DetourSetup(&v_Script_GrantClientSidePickup_Internal, Script_GrantClientSidePickup_Internal, bAttach);
}
