#ifndef VPANEL_H
#define VPANEL_H

namespace vgui
{
class VPanel
{
public:
	virtual void sub_14052A490();		   // 0 0x0
	virtual void sub_14052A560();		   // 1 0x8
	virtual void unk_0x10();			   // 2 0x10
	virtual void sub_14052C070();		   // 3 0x18
	virtual void sub_14025C5A0();		   // 4 0x20
	virtual void unk_0x28();			   // 5 0x28
	virtual void sub_14052C080();		   // 6 0x30
	virtual void sub_14052C090();		   // 7 0x38
	virtual void sub_14052C0E0();		   // 8 0x40
	virtual void sub_14052C100();		   // 9 0x48
	virtual void sub_14052C110();		   // 10 0x50
	virtual void sub_14052AA30();		   // 11 0x58
	virtual void sub_14052AAC0();		   // 12 0x60
	virtual void sub_14052AAD0();		   // 13 0x68
	virtual void sub_14052AAE0();		   // 14 0x70
	virtual void sub_14052AAF0();		   // 15 0x78
	virtual void sub_14052AB00();		   // 16 0x80
	virtual void sub_14052AB10();		   // 17 0x88
	virtual void sub_14052AB80();		   // 18 0x90
	virtual void sub_14052AB90();		   // 19 0x98
	virtual void sub_14052ABA0();		   // 20 0xA0
	virtual void sub_14052ABB0();		   // 21 0xA8
	virtual void sub_14052ABF0();		   // 22 0xB0
	virtual void sub_14052B6B0();		   // 23 0xB8
	virtual void sub_14052BAE0();		   // 24 0xC0
	virtual void sub_14052ADF0();		   // 25 0xC8
	virtual void sub_14052AE10();		   // 26 0xD0
	virtual void sub_14052AE80();		   // 27 0xD8
	virtual void sub_14052AEE0();		   // 28 0xE0
	virtual void sub_14052AF50();		   // 29 0xE8
	virtual void sub_14052AF70();		   // 30 0xF0
	virtual void sub_14052B0A0();		   // 31 0xF8
	virtual void sub_14052AF90();		   // 32 0x100
	virtual void sub_14052B090();		   // 33 0x108
	virtual void sub_14052B1E0();		   // 34 0x110
	virtual void sub_14052B0E0();		   // 35 0x118
	virtual void sub_14052A570();		   // 36 0x120
	virtual void sub_14052AC00();		   // 37 0x128
	virtual bool IsVisible();			   // 38 0x130
	virtual void sub_14052B3E0();		   // 39 0x138
	virtual void sub_14052ADA0();		   // 40 0x140
	virtual void sub_14052ADE0();		   // 41 0x148
	virtual void sub_1403D2220();		   // 42 0x150
	virtual void sub_14052B690();		   // 43 0x158
	virtual void unk_0x160();			   // 44 0x160
	virtual VPanel* GetParent();			   // 45 0x168
	virtual void sub_14052BAF0();		   // 46 0x170
	virtual void sub_14052BDA0();		   // 47 0x178
	virtual bool		HasParent( VPanel* potentialParent ); // 48 0x180
	virtual const char* GetName();				   // 49 0x188
	virtual void sub_14052C130();		   // 50 0x190
	virtual void sub_14052C140();		   // 51 0x198
	virtual void sub_14052C150();		   // 52 0x1A0
	virtual void sub_14052C160();		   // 53 0x1A8
	virtual void sub_14052C180();		   // 54 0x1B0
	virtual bool IsKeyBoardInputEnabled(); // 55 0x1B8
	virtual bool IsMouseInputEnabled();	   // 56 0x1C0
	virtual void sub_14052C1E0();		   // 57 0x1C8
	virtual void sub_14052C1D0();		   // 58 0x1D0
	virtual void sub_14052C1C0();		   // 59 0x1D8
	virtual void sub_14052C0B0();		   // 60 0x1E0
	virtual void sub_14052C0C0();		   // 61 0x1E8
	virtual void sub_14052C1F0();		   // 62 0x1F0
	virtual void sub_14052C200();		   // 63 0x1F8
	virtual void sub_14052C210();		   // 64 0x200
	virtual void sub_14052AE70();		   // 65 0x208

private:

};
}

#endif