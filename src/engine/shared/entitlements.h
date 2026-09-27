#ifndef ENTITLEMENTS_H
#define ENTITLEMENTS_H

struct Entitlements_s // Maps to entitlements.rson and script getters/setters.
{
	CUtlVector<char const*> enums;
	CUtlVector<char const*> names;

	u8 namesMD5[16];

	CUtlVector<char const*> offerIDs;
	CUtlVector<char*> prices_String;
	CUtlVector<unsigned int> prices_Int;
	CUtlVector<char*> originalPrices_String;
	CUtlVector<unsigned short> balances;

	u64 dataCRC;
	char unused[8]; // 8 bytes here but never used at all.

	CThreadMutex mutex;
};

inline void(*v_Entitlements_Destroy)();
inline Entitlements_s* g_hostEntitlements;

///////////////////////////////////////////////////////////////////////////////
class VEntitlements : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("Entitlements_Destroy", v_Entitlements_Destroy);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 48 83 EC ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 48 8B 05").GetPtr(v_Entitlements_Destroy);
	}
	virtual void GetVar(void) const
	{
		u8* mtxBase = CMemory(v_Entitlements_Destroy).FindPatternSelf("48 8D").ResolveRelativeAddressSelf(0x3, 0x7).RCast<u8*>();
		g_hostEntitlements = reinterpret_cast<Entitlements_s*>(mtxBase - offsetof(Entitlements_s, mutex));
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { };
};
///////////////////////////////////////////////////////////////////////////////

#endif // ENTITLEMENTS_H
