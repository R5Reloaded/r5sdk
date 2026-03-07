#pragma once

class RFixedArray;

/* ==== RTECH =========================================================================================================================================================== */
// [ PIXIE ]: I'm very unsure about this, but it really seems like it
inline int(*v_RFixedArray_FindSlot)(RFixedArray* const thisptr);
inline void(*v_RFixedArray_FreeSlot)(RFixedArray* const thisptr, const int slotNum);


class RFixedArray
{
public:
	inline int FindSlot(void)
	{
		return v_RFixedArray_FindSlot(this);
	}

	inline void FreeSlot(const unsigned int slotNum)
	{
		v_RFixedArray_FreeSlot(this, slotNum);
	}

private:
	int index;
	int slotsLeft;
	int structSize;
	int modMask;
	void* buffer;
	int slotsUsed;
	int padding_perhaps;
};

class RFixedArrayMT
{
public:
	inline int FindSlot(void)
	{
		AcquireSRWLockExclusive(&lock);
		const int slot = array.FindSlot();
		ReleaseSRWLockExclusive(&lock);

		return slot;
	}

	inline void FreeSlot(const unsigned int slotNum)
	{
		AcquireSRWLockExclusive(&lock);
		array.FreeSlot(slotNum);
		ReleaseSRWLockExclusive(&lock);
	}

private:
	RFixedArray array;
	SRWLOCK lock;
};

#define RSTD_HASHMAP_FREE_BUCKET -1

template <typename T, unsigned int N>
struct RHashMap
{
	constexpr RHashMap(T(&itemArray)[N], int(&bucketArray)[N])
	{
		count = 0;
		capacity = N;
		items = itemArray;
		buckets = bucketArray;
		firstReusable = 0;
		firstUnused = 0;
		itemToAdd = 0;
		bucketToAdd = 0;
		const u32 bucketBufSz = SmallestPowerOfTwoGreaterOrEqual(N);
		bucketMask = bucketBufSz - 1;
		memset(buckets, RSTD_HASHMAP_FREE_BUCKET, bucketBufSz);
	}

	unsigned int count;
	unsigned int capacity;
	T* items;
	int* buckets;
	unsigned int firstReusable;
	unsigned int firstUnused;
	int itemToAdd;
	unsigned int bucketToAdd;
	unsigned int bucketMask;
};

#pragma pack(push, 4)

struct RBitRead
{
	unsigned __int64 m_dataBuf;
	unsigned int m_bitsAvailable;

	RBitRead() : m_dataBuf(0), m_bitsAvailable(64) {};

	FORCEINLINE void ConsumeData(unsigned __int64 input, unsigned int numBits = 64)
	{
		if (numBits > m_bitsAvailable)
		{
			assert(false && "RBitRead::ConsumeData: numBits must be less than or equal to m_bitsAvailable.");
			return;
		}

		m_dataBuf |= input << (64 - numBits);
	}

	FORCEINLINE void ConsumeData(void* input, unsigned int numBits = 64)
	{
		if (numBits > m_bitsAvailable)
		{
			assert(false && "RBitRead::ConsumeData: numBits must be less than or equal to m_bitsAvailable.");
			return;
		}

		m_dataBuf |= *reinterpret_cast<unsigned __int64*>(input) << (64 - numBits);
	}

	FORCEINLINE int BitsAvailable() const { return m_bitsAvailable; };

	FORCEINLINE unsigned __int64 ReadBits(unsigned int numBits)
	{
		assert(numBits <= 64 && "RBitRead::ReadBits: numBits must be less than or equal to 64.");
		return m_dataBuf & ((1ull << numBits) - 1);
	}

	FORCEINLINE void DiscardBits(unsigned int numBits)
	{
		assert(numBits <= 64 && "RBitRead::DiscardBits: numBits must be less than or equal to 64.");
		this->m_dataBuf >>= numBits;
		this->m_bitsAvailable += numBits;
	}
};
#pragma pack(pop)

///////////////////////////////////////////////////////////////////////////////
class V_ReSTD : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("RHashMap::FindSlot", v_RFixedArray_FindSlot);
		LogFunAdr("RHashMap::FreeSlot", v_RFixedArray_FreeSlot);
	}
	virtual void GetFun(void) const 
	{
		Module_FindPattern(g_GameDll, "44 8B 51 0C 4C 8B C1").GetPtr(v_RFixedArray_FindSlot);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 44 8B 59 0C").GetPtr(v_RFixedArray_FreeSlot);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { };
};
///////////////////////////////////////////////////////////////////////////////
