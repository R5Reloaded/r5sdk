//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#ifndef NETWORKSTRINGTABLE_H
#define NETWORKSTRINGTABLE_H
#include "tier0/fasttimer.h"
#include "tier1/utlvector.h"
#include "tier1/utlhashtable.h"
#include "tier1/bitbuf.h"
#include "public/networkstringtabledefs.h"
#include "client/client.h"

// security_audit(kawe): currently, the game never uses more than 8192 string
// table entries, so I put this here to make sure nothing externally (or bad)
// sends more as that ultimately just burns CPU. NOTE that this is not a hard
// cap!! If you increase the number of entries inside the existing tables, or
// create a new one that requires more, increase this value to the next pow2.
#define MAX_STRINGTABLE_ENTRIES ( 1<<13 )

// security_audit(kawe): this used to be hardcoded everywhere, this has now
// been placed here so if you need to send string/int tables up, use this as
// the actual maximum for the bitbuf as the receiver will reject everything
// exceeding it! NOTE that this being twice the max reliable payload size is
// fine as this is nearly always send over the data blocks protocol anyways.
#define MAX_STRINGTABLE_PAYLOAD ( 2 * NET_MAX_PAYLOAD )

class CNetworkStringTable : public INetworkStringTable
{
public:
	// TODO[ AMOS ]: there are a few more entries below in the vftable that
	// need to be mapped out, most of them set/get bit fields;
	// see [ r5apex_ds + 0x1329888 ]

	TABLEID GetTableId(void) const;
	int GetMaxStrings(void) const;
	const char* GetTableName(void) const;
	int GetEntryBits(void) const;
	void SetTick(int tick_count);
	bool Lock(bool bLock);

private:
	TABLEID         m_id;
	bool            m_bLocked; // Might be wrong!
	char* m_pszTableName;
	int             m_nMaxEntries;
	int             m_nEntryBits;
	int             m_nTickCount;
	int             m_nLastChangedTick;
	uint32_t        m_nFlags;
	// !TODO
};

class CNetworkStringTableContainer : public INetworkStringTableContainer
{
public:
	static void WriteUpdateMessage(CNetworkStringTableContainer* thisp, CClient* client, unsigned int tick_ack, bf_write* msg);

	// Guards so game .dll can't create tables at the wrong time
	inline void AllowCreation(bool state) { m_bAllowCreation = state; }

private:
	bool        m_bAllowCreation;  // create guard
	int         m_nTickCount;      // current tick
	bool        m_bLocked;         // currently locked?
	bool        m_bEnableRollback; // enables rollback feature

	CUtlVector < CNetworkStringTable* > m_Tables; // the string tables
};

inline void (*CNetworkStringTableContainer__WriteUpdateMessage)(CNetworkStringTableContainer* thisp, CClient* client, unsigned int tick_ack, bf_write* msg);

class VNetworkStringTableContainer : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("CNetworkStringTableContainer::WriteUpdateMessage", CNetworkStringTableContainer__WriteUpdateMessage);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "48 89 74 24 ?? 48 89 7C 24 ?? 55 41 54 41 55 41 56 41 57 48 8D AC 24 ?? ?? ?? ?? B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 2B E0 45 33 ED")
			.GetPtr(CNetworkStringTableContainer__WriteUpdateMessage);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // NETWORKSTRINGTABLE_H