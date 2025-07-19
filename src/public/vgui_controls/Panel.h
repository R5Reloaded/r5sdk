//===== Copyright 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#ifndef PANEL_H
#define PANEL_H

#include "vgui/vgui.h"
#include "vgui/IClientPanel.h"

namespace vgui
{
	class IBorder;

	struct DragDrop_t
	{
		bool m_bDragEnabled;
		char m_gap001[119];
		bool m_bPreventChaining;
	};

	class Panel : public IClientPanel
	{
	private:
		char m_gap000[40];
		DragDrop_t* m_pDragDrop;
		char m_gap030[48];
		void* _vpanel;
		char m_gap068[32];
		IBorder* _border;
		int _flags;
		char m_gap090[476];
	};
}


#endif PANEL_H