#ifndef TSLIST_H
#define TSLIST_H
#include <tier0/imemalloc.h>

#define TSLIST_HEAD_ALIGNMENT 16
#define TSLIST_NODE_ALIGNMENT 16

#define TSLIST_HEAD_ALIGN DECL_ALIGN(TSLIST_HEAD_ALIGNMENT)
#define TSLIST_NODE_ALIGN DECL_ALIGN(TSLIST_NODE_ALIGNMENT)

typedef SLIST_HEADER TSLHead_t;

//-----------------------------------------------------------------------------
// 
//-----------------------------------------------------------------------------
class CAlignedMemAlloc
{
public:
	// Passed explicit parameters for 'this' pointer; the game expects one,
	// albeit unused. Do NOT optimize this away!
	typedef void* (*FnAlloc_t)(CAlignedMemAlloc* thisptr, size_t nSize, size_t nAlignment);
	typedef void (*FnFree_t)(CAlignedMemAlloc* thisptr, void* pMem);

	CAlignedMemAlloc(FnAlloc_t pAllocCallback, FnFree_t pFreeCallback);

	inline void* Alloc(size_t nSize, size_t nAlign = 0)
	{
		return m_pAllocCallback(this, nSize, nAlign);
	}
	inline void Free(void* pMem)
	{
		m_pFreeCallback(this, pMem);
	}

private:
	FnAlloc_t m_pAllocCallback;
	FnFree_t m_pFreeCallback;
};

extern CAlignedMemAlloc* g_pAlignedMemAlloc;

//-----------------------------------------------------------------------------
// Singleton aligned memalloc
//-----------------------------------------------------------------------------
inline CAlignedMemAlloc* AlignedMemAlloc()
{
	return g_pAlignedMemAlloc;
}

struct CTSListBase
{
    TSLHead_t m_Head;
};

template<typename T>
class CTSQueue
{
public:
    // override new/delete so we can guarantee 8-byte aligned allocs
    static void* operator new(size_t size)
    {
        CTSQueue* pNode = (CTSQueue*)MemAlloc_AllocAlignedFileLine(size, TSLIST_HEAD_ALIGNMENT, __FILE__, __LINE__);
        return pNode;
    }

    static void operator delete(void* p)
    {
        MemAlloc_FreeAligned(p);
    }

    static void* operator new[](size_t size) = delete;
    static void operator delete[](void* p) = delete;

    struct TSLIST_NODE_ALIGN Node_t
    {
        Node_t* pNext;
        T elem;
    };

    struct TSLIST_HEAD_ALIGN NodeLink_t
    {
        Node_t* pNode;
        uint64_t sequence;
    };

    void FinishPush(Node_t* pNode, NodeLink_t& oldTail)
    {
        AUTO_LOCK(m_Mutex);
        if (memcmp(&m_Tail, &oldTail, sizeof(NodeLink_t)))
            return;

        m_Tail.pNode = pNode;
        m_Tail.sequence++;
    }

    Node_t* End() { return (Node_t*)this; };

    Node_t* Pop()
    {
        NodeLink_t* volatile pHead = &m_Head;
        NodeLink_t* volatile pTail = &m_Tail;

        Node_t* volatile* pHeadNode = &m_Head.pNode;
        volatile uint64_t* volatile pHeadSequence = &m_Head.sequence;

        Node_t* volatile* pTailNode = &pTail->pNode;

        NodeLink_t head;
        Node_t* pNext;
        uint64_t tailSequence;
        T elem;

        for (;;)
        {
            head.sequence = *pHeadSequence;
            head.pNode = *pHeadNode;
            tailSequence = pTail->sequence;
            pNext = head.pNode->pNext;

            if (!pNext || head.sequence != *pHeadSequence)
                continue;

            if (head.pNode == *pTailNode)
            {
                if (pNext == End())
                    return nullptr;

                NodeLink_t& oldTail = head;
                oldTail.sequence = tailSequence;
                FinishPush(pNext, oldTail);
                continue;
            }

            if (pNext != End())
            {
                //Why are respawn using a lock here instead of atomic ops?
                AUTO_LOCK(m_Mutex);
                if (pHead->pNode == head.pNode && pHead->sequence == head.sequence)
                {
                    NodeLink_t newHead;
                    newHead.pNode = pNext;
                    newHead.sequence = head.sequence + 1;

                    *pHead = newHead;
                    elem = pNext->elem;
                    break;
                }
            }
        }

        head.pNode->elem = elem;
        InterlockedDecrement((volatile LONG*)&m_Count);
        return head.pNode;
    }

    NodeLink_t m_Head;
    NodeLink_t m_Tail;
    volatile int m_Count;
    char m_gap024[12];
    CTSListBase m_FreeNodes;
    CThreadMutex m_Mutex;
    char m_gap040[8];
};

///////////////////////////////////////////////////////////////////////////////
class VTSListBase : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogVarAdr("g_AlignedMemAlloc", g_pAlignedMemAlloc);
	}
	virtual void GetFun(void) const { }
	virtual void GetVar(void) const
	{
		g_pAlignedMemAlloc = Module_FindPattern(g_GameDll, "48 89 5C 24 ?? 48 89 74 24 ?? 48 89 7C 24 ?? 55 41 54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 2B E0 48 8B D9")
			.Offset(0x130).FindPatternSelf("48 8D 15 ?? ?? ?? 01", CMemory::Direction::DOWN, 100).ResolveRelativeAddressSelf(0x3, 0x7).RCast<CAlignedMemAlloc*>();
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const { }
};
///////////////////////////////////////////////////////////////////////////////

#endif // TSLIST_H
