#ifndef CL_ENTS_PARSE_H
#define CL_ENTS_PARSE_H
#include "engine/shared/ents_shared.h"
#include "engine/framesnapshot.h"

inline void(*v_CL_ProcessSnapshot_WT)(SnapshotData_Async* const asyncSnap);
inline bool(*v_CL_CopyNewEntity)(CEntityReadInfo* const u, unsigned int* const iClass, const int iSerialNum, bool* const pbError);
inline bool(*v_CL_CopyExistingEntity)(CEntityReadInfo* const u, unsigned int* const iClass, bool* const pbError);

bool CL_CopyNewEntity(CEntityReadInfo* const u, unsigned int* const iClass, const int iSerialNum, bool* const pbError);
bool CL_CopyExistingEntity(CEntityReadInfo* const u, unsigned int* const iClass, bool* const pbError);
///////////////////////////////////////////////////////////////////////////////
class V_CL_Ents_Parse : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("CL_ProcessSnapshot_WT", v_CL_ProcessSnapshot_WT);
		LogFunAdr("CL_CopyNewEntity", v_CL_CopyNewEntity);
		LogFunAdr("CL_CopyExistingEntity", v_CL_CopyExistingEntity);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 74 24 ?? 41 56 48 83 EC ?? 45 33 F6").GetPtr(v_CL_ProcessSnapshot_WT);
		Module_FindPattern(g_GameDll, "40 55 53 41 54 41 55 41 57 48 8D AC 24").GetPtr(v_CL_CopyNewEntity);
		Module_FindPattern(g_GameDll, "40 53 48 83 EC 70 4C 63 51 28").GetPtr(v_CL_CopyExistingEntity);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // !CL_ENTS_PARSE_H
