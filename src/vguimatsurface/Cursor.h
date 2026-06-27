//========= Copyright � 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Methods associated with the cursor
//
// $Revision: $
// $NoKeywords: $
//=============================================================================//

#ifndef MATSURFACE_CURSOR_H
#define MATSURFACE_CURSOR_H

FORWARD_DECLARE_HANDLE( InputContextHandle_t );

//-----------------------------------------------------------------------------
// Selects a cursor
//-----------------------------------------------------------------------------
void CursorSelect( InputContextHandle_t hContext, vgui::HCursor hCursor );

#endif MATSURFACE_CURSOR_H