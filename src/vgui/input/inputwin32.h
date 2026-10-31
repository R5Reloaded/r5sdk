#ifndef INPUTWIN32_H
#define INPUTWIN32_H
#include <vgui/vgui.h>

class IInputInternal
{
public:
	virtual void sub_14051B7F0();					  // 0 0x0
	virtual void SetMouseFocus();					  // 1 0x8
	virtual void SetMouseCapture();					  // 2 0x10
	virtual void sub_14051DFD0();					  // 3 0x18
	virtual void sub_14051D9D0();					  // 4 0x20
	virtual void sub_14051DA00();					  // 5 0x28
	virtual void sub_14051DA10();					  // 6 0x30
	virtual void sub_14051DD10();					  // 7 0x38
	virtual void sub_14051DD60();					  // 8 0x40
	virtual void sub_14051DF90();					  // 9 0x48
	virtual void sub_14051DF40();					  // 10 0x50
	virtual void sub_14051DA70();					  // 11 0x58
	virtual void sub_14051DAB0();					  // 12 0x60
	virtual void sub_14051DAF0();					  // 13 0x68
	virtual void sub_14051E070();					  // 14 0x70
	virtual void GetState();						  // 15 0x78
	virtual void sub_14051DB30();					  // 16 0x80
	virtual void sub_14051DB70();					  // 17 0x88
	virtual void sub_14051DBB0();					  // 18 0x90
	virtual void sub_14051DBF0();					  // 19 0x98
	virtual void sub_14051DC30();					  // 20 0xA0
	virtual void sub_14051FD70();					  // 21 0xA8
	virtual void sub_14051FDA0();					  // 22 0xB0
	virtual void sub_14051FDD0();					  // 23 0xB8
	virtual void sub_14051DEB0();					  // 24 0xC0
	virtual void sub_14051FE10();					  // 25 0xC8
	virtual void sub_14051FE20();					  // 26 0xD0
	virtual void sub_14051FEB0();					  // 27 0xD8
	virtual void sub_140520150();					  // 28 0xE0
	virtual void sub_140520160();					  // 29 0xE8
	virtual void sub_1405201A0();					  // 30 0xF0
	virtual void sub_140520220();					  // 31 0xF8
	virtual void sub_1405202C0();					  // 32 0x100
	virtual void GetIMEConversionModes();			  // 33 0x108
	virtual void GetIMESentenceModes();				  // 34 0x110
	virtual void sub_140520170();					  // 35 0x118
	virtual void sub_140520D30();					  // 36 0x120
	virtual void unk_1();							  // 37 0x128
	virtual void unk_2();							  // 38 0x130
	virtual void unk_3();							  // 39 0x138
	virtual void sub_140520DE0();					  // 40 0x140
	virtual void sub_140520E90();					  // 41 0x148
	virtual void sub_140520F70();					  // 42 0x150
	virtual void sub_1405211B0();					  // 43 0x158
	virtual void sub_140521090();					  // 44 0x160
	virtual void unk_4();							  // 45 0x168
	virtual void sub_140521430();					  // 46 0x170
	virtual void sub_140521510();					  // 47 0x178
	virtual void sub_1405215F0();					  // 48 0x180
	virtual void sub_1405212D0();					  // 49 0x188
	virtual void sub_140521300();					  // 50 0x190
	virtual void sub_1405213A0();					  // 51 0x198
	virtual void sub_1405213D0();					  // 52 0x1A0
	virtual void sub_140521400();					  // 53 0x1A8
	virtual void sub_1405216D0();					  // 54 0x1B0
	virtual void sub_140521750();					  // 55 0x1B8
	virtual void unk_5();							  // 56 0x1C0
	virtual void unk_6();							  // 57 0x1C8
	virtual void sub_14051D690();					  // 58 0x1D0
	virtual void sub_1405217A0();					  // 59 0x1D8
	virtual void sub_140521840();					  // 60 0x1E0
	virtual void sub_140521900();					  // 61 0x1E8
	virtual void sub_140521B50();					  // 62 0x1F0
	virtual void sub_140521C40();					  // 63 0x1F8
	virtual vgui::VPANEL GetModalSubTree();			  // 64 0x200
	virtual void sub_140521CE0();					  // 65 0x208
	virtual bool ShouldModalSubTreeReceiveMessages(); // 66 0x210
	virtual vgui::VPANEL GetMouseCapture();			  // 67 0x218
	virtual void sub_14051DA40();					  // 68 0x220
	virtual void sub_140521D60();					  // 69 0x228
	virtual bool ShouldModalSubTreeShowMouse();		  // 70 0x230
	virtual void sub_14051E080();					  // 71 0x238
	virtual void sub_140521DE0();					  // 72 0x240
	virtual void sub_140521E10();					  // 73 0x248
	virtual void sub_14051BE70();					  // 74 0x250
	virtual void sub_14051D1B0();					  // 75 0x258
	virtual void UpdateMouseFocus();				  // 76 0x260
	virtual void sub_14051CA10();					  // 77 0x268
	virtual void InternalCursorMoved();				  // 78 0x270
	virtual void InternalMousePressed();			  // 79 0x278
	virtual void sub_14051ED50();					  // 80 0x280
	virtual void sub_14051F070();					  // 81 0x288
	virtual void InternalMouseWheeled();			  // 82 0x290
	virtual void sub_14051F750();					  // 83 0x298
	virtual void sub_14051F910();					  // 84 0x2A0
	virtual void sub_14051F9B0();					  // 85 0x2A8
	virtual void sub_14051FA40();					  // 86 0x2B0
	virtual void sub_14051FAF0();					  // 87 0x2B8
	virtual void sub_14051E240();					  // 88 0x2C0
	virtual void sub_14051E470();					  // 89 0x2C8
	virtual void sub_14051BBB0();					  // 90 0x2D0
	virtual void sub_14051BCB0();					  // 91 0x2D8
	virtual void sub_14051BDC0();					  // 92 0x2E0
	virtual void sub_14051BE60();					  // 93 0x2E8
	virtual void sub_14051E740();					  // 94 0x2F0
	virtual void sub_14051DC70();					  // 95 0x2F8
	virtual void sub_14051E6A0();					  // 96 0x300
	virtual void sub_14051F5E0();					  // 97 0x308
	virtual void SetMouseCodeState();				  // 98 0x310
	virtual void sub_14051F640();					  // 99 0x318
	virtual void sub_14051BB80();					  // 100 0x320
};

class CInputWin32 : public IInputInternal
{
};

inline CInputWin32* g_pInputWin32;

class VInputWin32 : public IDetour
{
	virtual void GetAdr( void ) const 
    { 
        LogVarAdr( "g_InputWin32", g_pInputWin32 );
	}
	virtual void GetFun( void ) const
	{}
	virtual void GetVar( void ) const
	{
		Module_FindPattern( g_GameDll, "48 8B 05 ?? ?? ?? ?? 0F B7 F9" ).ResolveRelativeAddressSelf( 0x3, 0x7 ).GetPtr( g_pInputWin32 );
	}
	virtual void GetCon( void ) const {}
	virtual void Detour( const bool bAttach ) const {};
};


#endif INPUTWIN32_H