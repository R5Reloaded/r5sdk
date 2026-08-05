//===== Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: Methods associated with the cursor
//
// $Revision: $
// $NoKeywords: $
//===========================================================================//
#include <vgui/vgui.h>
#include <vgui/cursor.h>
#include <vguimatsurface/Cursor.h>
#include <inputsystem/inputstacksystem.h>
#include <vguimatsurface/MatSystemSurface.h>
#include <inputsystem/inputsystem.h>

using namespace vgui;

void CursorSelect( InputContextHandle_t hContext, HCursor hCursor )
{
	*s_pbCursorVisible = true;

    switch (hCursor)
	{
	case dc_none: 
        *s_pbCursorVisible = false; 
        break;

    case dc_arrow:
	case dc_ibeam:
	case dc_hourglass:
	case dc_waitarrow:
	case dc_crosshair:
	case dc_up:
	case dc_sizenwse:
	case dc_sizenesw:
	case dc_sizewe:
	case dc_sizens:
	case dc_sizeall:
	case dc_no:
	case dc_hand:	   
        *s_phCurrentCursor = s_hDefaultCursor[hCursor]; 
        break;

	default:
	{
		Assert( 0 );

        //TODO: Nonstandard cursor handling
        //currently this function is not used for anything that would require this
		break;
	}
	}

    if ( *s_pbCursorVisible )
		g_pInputStackSystem->SetCursorIcon( hContext, *s_phCurrentCursor );
    else 
        g_pInputStackSystem->SetCursorIcon(hContext, INPUT_CURSOR_HANDLE_INVALID);

    g_pInputSystem->SetMouseCursorVisible( *s_pbCursorVisible );
}