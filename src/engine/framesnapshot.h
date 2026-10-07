#if !defined( FRAMESNAPSHOT_H )
#define FRAMESNAPSHOT_H
#include "public/inetchannel.h"
#include "shared/datablock.h"

struct SnapshotFrame // todo: reverse engineer more of this
{
	bool isValid;

	int last_entity;
	int view_entity;

	int snap_dwordC;
	bool snap_byte10;
	_BYTE padding11[3];
	int snap_dword14;
	int snap_dword18;
	bool snap_byte1C;
	_BYTE padding1D[3];
	int snap_dword20;
	bool snap_byte24;
	bool snap_byte25;

	bool doPrediction;
	bool shouldDiscard;

	bool snap_byte28;
	bool padding29[3];

	nettick_t snapTick;

	_BYTE padding44[4];

	SnapshotFrame* next;

	i64 qword50;
	i64 buffer58[16384];

	u8 buffer20058[2048];
	u8 buffer20858[4096];
};

static_assert(sizeof(SnapshotFrame) == 137304);

struct SnapshotRecord // todo: reverse engineer more of this
{
	int serverCount;
	int maxEntities;
	int changedEntities;
	bool isDelta;
	bool doPrediction;
	int deltaFrom;
	int tickStart;
	int viewEntity;
	int m_nUnk1;
	bool m_bUnk2;
	bool field_41;
	int m_nUnk_14bits_2;
	int m_nUnk_32bits;
	bool m_bUnk3;
	int m_nUnk_32bits_2;
	int deltaBits;
	bool m_bUnk4;
	bool m_bUnk5;
	nettick_t tick_update;
};

static_assert(sizeof(SnapshotRecord) == 84);

struct SnapshotData_Async // Mostly used by CL_ProcessSnapshot_WT
{
	SnapshotRecord record;
	bf_read dataIn;
	u8 buffer[MAX_DATABLOCK_TRANSFER_SIZE];
	int oldestActiveSnapshotTick;
};

static_assert(sizeof(SnapshotData_Async) == 786592);

#endif // FRAMESNAPSHOT_H
