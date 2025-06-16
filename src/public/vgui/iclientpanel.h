//========= Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef ICLIENTPANEL_H
#define ICLIENTPANEL_H

#include "vgui.h"

namespace vgui
{
	class IClientPanel
	{
	public:
		virtual VPANEL GetVPanel() = 0;
	};
}

#endif // ICLIENTPANEL_H