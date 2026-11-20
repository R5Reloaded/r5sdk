#ifndef CLIENT_CLIENTSIDE_PICKUPS_H
#define CLIENT_CLIENTSIDE_PICKUPS_H

// note(kawe): idk what a sane number is. The engine could support thousands
// but its really expensive. I don't see any reason to go above 100 for now,
// if more is needed feel free to increase it but don't make it too large!
#define CLIENTSIDE_PICKUP_MAX_SPAWN_COUNT 100

bool ClientSidePickup_Spawn(unsigned int pickupType, unsigned int amount, Vector3D* origin, unsigned int CSPUFLAGflags, unsigned int customFlags, unsigned int recieptID);
bool Script_GrantClientSidePickup_Internal(void* entity, int pickupType, unsigned int amount, Vector3D* origin, int CSPUFLAGflags, int customFlags, int recieptID);

inline bool (*v_ClientSidePickup_Spawn)(unsigned int pickupType, unsigned int amount, Vector3D* origin, unsigned int CSPUFLAGflags, unsigned int customFlags, unsigned int recieptID);
inline bool (*v_Script_GrantClientSidePickup_Internal)(void* entity, int pickupType, unsigned int amount, Vector3D* origin, int CSPUFLAGflags, int customFlags, int recieptID);

///////////////////////////////////////////////////////////////////////////////
class V_ClientSidePickups : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("ClientSidePickup_Spawn", v_ClientSidePickup_Spawn);
		LogFunAdr("Script_GrantClientSidePickup_Internal", v_Script_GrantClientSidePickup_Internal);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "89 4C 24 ? 4C 8B DC 55 56 57 41 56").GetPtr(v_ClientSidePickup_Spawn);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ? 48 89 6C 24 ? 48 89 74 24 ? 57 48 83 EC ? 48 63 DA 49 8B F1").GetPtr(v_Script_GrantClientSidePickup_Internal);
	}
	virtual void GetVar(void) const {}
	virtual void GetCon(void) const {}
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // CLIENT_CLIENTSIDE_PICKUPS_H
