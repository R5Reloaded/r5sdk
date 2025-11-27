#pragma once
#include "tier0/threadtools.h"
#include "rtech/rstdlib.h"

// IMPORTANT: do not change these as these are used by GfxCbufCollection_s
// which maps directly to the instance in the data segment of the image,
// change the NEW ones instead!
#define CBUF_MAX_ITEMS_OLD 3072
#define CBUF_BUCKET_SIZE_OLD 4096

// IMPORTANT: if you reached this limit, and you can't optimize resource
// usage any further, increase these. Keep in mind that the bucket size
// must be a power of two because RHashMap uses mod masks!
#define CBUF_MAX_ITEMS_NEW 9920
#define CBUF_BUCKET_SIZE_NEW 16383

struct GfxCbufItem_s
{
	u32 handle;
	u32 pad;
	u32 refCount;
	u32 hash;
};

// nb(kawe): this struct must be left unchanged as it maps directly
// with the data in the executable.
struct GfxCbufCollection_s
{
	CThreadMutexRW mutex;
	RHashMap<GfxCbufItem_s, CBUF_MAX_ITEMS_OLD> insertMap;
	int insertBuckets[CBUF_BUCKET_SIZE_OLD];
	GfxCbufItem_s insertItems[CBUF_MAX_ITEMS_OLD];
	RHashMap<u32, CBUF_MAX_ITEMS_OLD> eraseMap;
	u32 eraseItems[CBUF_MAX_ITEMS_OLD];
	int eraseBuckets[CBUF_BUCKET_SIZE_OLD];
};

extern GfxCbufCollection_s* g_constBufferCollection;
inline void (*v_Gfx_InitShared)(void);

///////////////////////////////////////////////////////////////////////////////
class VConstBuffer : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("Gfx_InitShared", v_Gfx_InitShared);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 48 83 EC ? E8 ? ? ? ? 48 8D 0D ? ? ? ? FF 15").GetPtr(v_Gfx_InitShared);
	}
	virtual void GetVar(void) const
	{
		CMemory(v_Gfx_InitShared).Offset(0x1FC).FindPatternSelf("48 8D", CMemory::Direction::DOWN).ResolveRelativeAddressSelf(0x3, 0x7).GetPtr(g_constBufferCollection);
	}
	virtual void GetCon(void) const
	{ }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
