#include "constbuffer.h"
#include "rtech/rstdlib.h"
#include "windows/id3dx.h"

GfxCbufCollection_s* g_constBufferCollection;

static struct GfxCbufRemap_s
{
	GfxCbufRemap_s()
	{
		memset(insertBuckets, RSTD_HASHMAP_FREE_BUCKET, sizeof(insertBuckets));
		memset(eraseBuckets, RSTD_HASHMAP_FREE_BUCKET, sizeof(eraseBuckets));
	}

	GfxCbufItem_s insertItems[CBUF_MAX_ITEMS_NEW];
	int insertBuckets[CBUF_BUCKET_SIZE_NEW];
	u32 eraseItems[CBUF_MAX_ITEMS_NEW];
	int eraseBuckets[CBUF_BUCKET_SIZE_NEW];
} s_constBufferRemap;

static void Gfx_RemapCbufCollection()
{
	// Remap insertion data to our new static arrays to increase size.
	// Engine uses these to access the buffers.
	g_constBufferCollection->insertMap.capacity = CBUF_MAX_ITEMS_NEW;
	g_constBufferCollection->insertMap.items = s_constBufferRemap.insertItems;
	g_constBufferCollection->insertMap.buckets = s_constBufferRemap.insertBuckets;
	g_constBufferCollection->insertMap.bucketMask = CBUF_BUCKET_SIZE_NEW;

	// NOTE: this is the only array that is used directly in some functions.
	// The trick is to set the first item in the array to the address of our
	// new array, and switching up the opcodes at function sites that load
	// this member from LEA to MOV to make it deref it. See r5apex.patch for
	// more details regarding all required patches applied to the image.
	*reinterpret_cast<GfxCbufItem_s**>(g_constBufferCollection->insertItems) = s_constBufferRemap.insertItems;

	// Remap erasion data to our new static arrays to increase size.
	// Engine uses these to access the buffers.
	g_constBufferCollection->eraseMap.capacity = CBUF_MAX_ITEMS_NEW;
	g_constBufferCollection->eraseMap.items = s_constBufferRemap.eraseItems;
	g_constBufferCollection->eraseMap.buckets = s_constBufferRemap.eraseBuckets;
	g_constBufferCollection->eraseMap.bucketMask = CBUF_BUCKET_SIZE_NEW;
}

static void Gfx_InitShared()
{
	v_Gfx_InitShared();
	Gfx_RemapCbufCollection();
}

void VConstBuffer::Detour(const bool bAttach) const
{
	DetourSetup(&v_Gfx_InitShared, &Gfx_InitShared, bAttach);
}
