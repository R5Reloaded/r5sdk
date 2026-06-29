#pragma once

#include <vgui/vgui.h>

FORWARD_DECLARE_HANDLE( InputCursorHandle_t );
FORWARD_DECLARE_HANDLE( InputContextHandle_t );

/* ==== CMATSYSTEMSURFACE =============================================================================================================================================== */
inline void(*CMatSystemSurface__DrawColoredText)(void* thisptr, short font, int fontHeight, int offsetX, int offsetY, int red, int green, int blue, int alpha, const char* fmt, ...);
inline void(*CMatSystemSurface__DrawColoredTextInternal)(void* thisptr, short font, int fontHeight, int offsetX, int offsetY, int red, int green, int blue, int alpha, int unk1, const char* fmt, va_list argptr);
inline void ( *CMatSystemSurface__CalculateMouseVisible )( class CMatSystemSurface* thisp );
inline bool* s_pbCursorLocked;
inline bool* s_pbCursorVisible;
inline InputCursorHandle_t* s_phCurrentCursor;
inline InputCursorHandle_t* s_hDefaultCursor;

void MatSystemSurface_DrawColoredText(void* thisptr, short font, int fontHeight, int offsetX, int offsetY, int red, int green, int blue, int alpha, const char* fmt, ...);

class CMatSystemSurface
{
public:

    inline int GetPopupCount() const { return m_PopupList.Count(); }
    inline vgui::VPANEL GetPopup(int index) const { return m_PopupList[index]; }
    
    inline InputContextHandle_t GetInputContext() const { return m_hInputContext; }

    static void _CalculateMouseVisible( CMatSystemSurface* thisp );

private:
    static inline bool IsCursorLocked() { return *s_pbCursorLocked; }

    void LockCursor( bool bLock );

    inline void LockCursor() { LockCursor( true ); }
    inline void UnlockCursor() { LockCursor( false ); }

    void SetCursor( vgui::HCursor hCursor );

	void* __vftable;

	void*						  unk1;
	int							  unk2;
	int							  field_10;
	int							  m_nTranslateX;
	int							  m_nTranslateY;
	float						  m_flAlphaMultiplier;
	_BYTE						  gap24[20];
	Color						  m_DrawColor;
	int							  field_3C;
	__int64						  field_40;
	__declspec( align( 16 ) ) int field_50;
	_BYTE						  gap54[108];
	__int64						  field_C0;
	__int64						  field_C8;
	CUtlVector<vgui::VPANEL>      m_PopupList;
	_BYTE						  gapD0[36];
	vgui::HCursor			      _currentCursor;
	InputContextHandle_t		  m_hInputContext;
	char						  m_gap0120[4];
	int							  field_124;
	_BYTE						  gap128[65560];

    bool m_unk1 : 1;
    bool m_unk2 : 1;
	bool m_unk5 : 1;
	bool m_unk6 : 1;
    bool m_bNeedsKeyboard : 1;
	bool m_bNeedsMouse : 1;
	bool m_unk7 : 1;
	bool m_unk8 : 1;

	_BYTE						  gap10141[87];
	__int64						  field_10198;
};

inline CMatSystemSurface* g_pMatSystemSurface;
inline CMatSystemSurface** g_ppVGuiSurface;

///////////////////////////////////////////////////////////////////////////////
class VMatSystemSurface : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("CMatSystemSurface::DrawColoredText", CMatSystemSurface__DrawColoredText);
		LogFunAdr("CMatSystemSurface::DrawColoredTextInternal", CMatSystemSurface__DrawColoredTextInternal);
		LogFunAdr( "CMatSystemSurface::CalculateMouseVisible", CMatSystemSurface__CalculateMouseVisible );
		LogVarAdr("g_pMatSystemSurface", g_pMatSystemSurface);
		LogVarAdr( "g_pVGuiSurface", g_ppVGuiSurface );
		LogVarAdr( "s_bCursorLocked", s_pbCursorLocked );
		LogVarAdr( "s_bCursorVisible", s_pbCursorVisible );
		LogVarAdr( "s_hCurrentCursor", s_phCurrentCursor );
		LogVarAdr( "s_hDefaultCursor", s_hDefaultCursor );
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "4C 8B DC 48 83 EC 68 49 8D 43 58 0F 57 C0").GetPtr(CMatSystemSurface__DrawColoredText);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 56 41 57 48 8D AC 24 ? ? ? ? B8 ? ? ? ? E8 ? ? ? ? 48 2B E0 48 8B F9").GetPtr(CMatSystemSurface__DrawColoredTextInternal);
		Module_FindPattern( g_GameDll, "40 57 48 83 EC ?? 80 A1" ).GetPtr( CMatSystemSurface__CalculateMouseVisible );
	}
	virtual void GetVar(void) const
	{
		g_pMatSystemSurface = Module_FindPattern(g_GameDll, "48 83 EC 28 48 83 3D ?? ?? ?? ?? ?? 48 8D 05 ?? ?? ?? ??")
			.FindPatternSelf("48 83 3D", CMemory::Direction::DOWN, 40).ResolveRelativeAddressSelf(0x3, 0x8).RCast<CMatSystemSurface*>();

		g_ppVGuiSurface = Module_FindPattern(
				g_GameDll,
				"48 8B 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC 48 8B 05 ?? ?? ?? ?? C3 CC CC CC CC CC CC CC CC 8B 81 ?? ?? ?? ??" )
			.ResolveRelativeAddressSelf(0x3, 0x7).RCast<CMatSystemSurface**>();

        Module_FindPattern( g_GameDll, "80 3D ?? ?? ?? ?? ?? 8B DA 0F 85" )
			.ResolveRelativeAddressSelf( 0x2, 0x7 )
			.GetPtr( s_pbCursorLocked );

        Module_FindPattern( g_GameDll, "C6 05 ?? ?? ?? ?? ?? 83 FB" ).ResolveRelativeAddressSelf(0x2, 0x7).GetPtr( s_pbCursorVisible );
		Module_FindPattern( g_GameDll, "4C 8B 05 ?? ?? ?? ?? FF 50" ).ResolveRelativeAddressSelf( 0x3, 0x7 ).GetPtr( s_phCurrentCursor );
		Module_FindPattern( g_GameDll, "4C 8D 05 ?? ?? ?? ?? 4D 8B 04 D8" )
			.ResolveRelativeAddressSelf( 0x3, 0x7 )
			.GetPtr( s_hDefaultCursor );
	}
	virtual void GetCon(void) const { }
	virtual void Detour( const bool bAttach ) const;
};
///////////////////////////////////////////////////////////////////////////////
