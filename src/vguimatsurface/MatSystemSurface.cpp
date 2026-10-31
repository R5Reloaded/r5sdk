#include "core/stdafx.h"
#include "vguimatsurface/MatSystemSurface.h"
#include <gameui/imgui_system.h>
#include <inputsystem/inputstacksystem.h>
#include <vgui/input/inputwin32.h>
#include <vgui/VPanel.h>
#include <vgui/cursor.h>
#include <vguimatsurface/Cursor.h>

void MatSystemSurface_DrawColoredText(void* thisptr, short font, int fontHeight, int offsetX, int offsetY, int red, int green, int blue, int alpha, const char* fmt, ...)
{
	va_list argptr;
	va_start(argptr, fmt);
	CMatSystemSurface__DrawColoredTextInternal(thisptr, font, fontHeight, offsetX, offsetY, red, green, blue, alpha, 0, fmt, argptr);
	va_end(argptr);
}

void CMatSystemSurface::SetCursor( vgui::HCursor hCursor )
{
	if ( IsCursorLocked() )
		return;

	if ( _currentCursor == hCursor )
		return;

	_currentCursor = hCursor;
	CursorSelect( GetInputContext(), hCursor );
}

void CMatSystemSurface::LockCursor( bool bLock )
{
	*s_pbCursorLocked			  = bLock;
	InputCursorHandle_t newCursor = INPUT_CURSOR_HANDLE_INVALID;
	if (( *s_pbCursorVisible ))
		newCursor = *s_phCurrentCursor;

	g_pInputStackSystem->SetCursorIcon( GetInputContext(), newCursor );
}

static bool IsChildOfModalSubTree(vgui::VPANEL panel)
{
	if ( !panel )
		return false;

    vgui::VPANEL modalSubTree = g_pInputWin32->GetModalSubTree();
	if ( !modalSubTree )
		return true;

    const bool bRestrictMessages = g_pInputWin32->ShouldModalSubTreeReceiveMessages();
	const bool isChildOfModal	= reinterpret_cast<vgui::VPanel*>( panel )->HasParent( (vgui::VPanel*)modalSubTree );
    if (isChildOfModal)
    {
		return bRestrictMessages;
    }
    else
    {
		return !bRestrictMessages;
    }
}

void CMatSystemSurface::_CalculateMouseVisible( CMatSystemSurface* thisp )
{
	thisp->m_bNeedsMouse = false;
	thisp->m_bNeedsKeyboard = false;

    if (g_pInputWin32->GetMouseCapture() != NULL)
		return;

    int c = (*g_ppVGuiSurface)->GetPopupCount();
	vgui::VPANEL modalSubTree = g_pInputWin32->GetModalSubTree();

    if (modalSubTree)
		thisp->m_bNeedsMouse = g_pInputWin32->ShouldModalSubTreeShowMouse();

    for (int i = 0; i < c; i++)
    {
		vgui::VPanel* popup = (vgui::VPanel*)( *g_ppVGuiSurface )->GetPopup( i );

		if ( modalSubTree && !IsChildOfModalSubTree( popup ) )
			continue;

		bool isVisible = popup->IsVisible();
		for ( vgui::VPanel* parent = popup->GetParent(); parent && isVisible; parent = parent->GetParent() )
			isVisible = parent->IsVisible();

		if ( isVisible )
		{
			thisp->m_bNeedsMouse |= popup->IsMouseInputEnabled();
			thisp->m_bNeedsKeyboard |= popup->IsKeyBoardInputEnabled();

			if ( thisp->m_bNeedsMouse && thisp->m_bNeedsKeyboard )
				break;
		}
    }

    g_pInputStackSystem->EnableInputContext( thisp->GetInputContext(), thisp->m_bNeedsMouse );

    if ( thisp->m_bNeedsMouse )
    {
	    thisp->UnlockCursor();
		thisp->SetCursor( vgui::dc_arrow );
    }
    else
    {
	    if ( ImguiSystem()->IsSurfaceActive() )
	    {
			thisp->UnlockCursor();
        }
        else
        {
			thisp->SetCursor( vgui::dc_none );
			thisp->LockCursor();
        }
    }
}

void VMatSystemSurface::Detour(const bool bAttach) const
{
	DetourSetup( &CMatSystemSurface__CalculateMouseVisible, &CMatSystemSurface::_CalculateMouseVisible, bAttach );
}