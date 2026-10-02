#ifndef PERSISTENCE_H
#define PERSISTENCE_H

struct PDefItem
{
	const char* name;
	int type; // todo: reverse enum
	u64 structOffset;
	u64 arraySizeOrIndex;
	u64 itemSize;
	u64 stringSizeOrStructID;
};

struct PDefStruct
{
	const char* name;
	u64 index;
	u64 size;
	PDefItem* items;
	u64 allocationCount;
	u64 itemCount;
};

struct PDefEnum
{
	const char* name;
	u64 index;
	const char** fields;
	u64 fieldCount;
	u64 allocationCount;
};

struct PDefIndex
{
	PDefItem* item;
	u64 offset;
	u64 index;
	u64 flags; // todo: reverse flags
};

struct PersistentDataDef_s
{
	PDefItem items[2200];
	u64 itemCount;
	const char* enumFields[3409];
	u64 enumFieldCount;
	u64 scriptVersion;
	u64 dataSize;
	PDefEnum enumDefs[191];
	u64 enumDefCount;
	char pad[40];
	PDefStruct structDefs[40];
	u64 structDefCount;
	PDefIndex itemIndices[16000];
	u64 itemIndexCount;

	CUtlDict<u8, int> enumDict[192];
	CUtlDict<u64, int> structDict[40];

	// todo: reverse the rest.
};

inline PersistentDataDef_s* g_persistentDataDef;

class VPersistence : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogVarAdr("g_persistentDataDef", g_persistentDataDef);
	}
	virtual void GetFun(void) const {}
	virtual void GetVar(void) const
	{
		g_persistentDataDef = Module_FindPattern(g_GameDll, "48 8D 15 ? ? ? ? 49 C1 E0 ? 49 8B 84 10").ResolveRelativeAddressSelf(0x3, 0x7).RCast<PersistentDataDef_s*>();
	}
	virtual void GetCon(void) const {}
	virtual void Detour(const bool bAttach) const {};
};

#endif // PERSISTENCE_H
