//===========================================================================//
//
// Purpose: Implements all the functions exported by the GameUI dll.
//
// $NoKeywords: $
//===========================================================================//

#include <core/stdafx.h>
#include <tier1/cvar.h>
#include <engine/sys_utils.h>
#include <vgui_controls/RichText.h>
#include <vguimatsurface/MatSystemSurface.h>

void RichTextCommon_SetText(vgui::RichText* thisptr, const char* text)
{
	thisptr->SetText(text);
}

void vgui::RichTextCommon::SetText(const char* text)
{
    // Originally 4096, increased to 8192
    WCHAR unicode[VGUI_RICHTEXT_MAX_LEN];

	if (text && *text)
	{
        if (text[0] == '#')
        {
            this->ResolveLocalizedTextAndVariables(text, unicode, sizeof(unicode));
            this->SetText(unicode);
        }
        else
        {
            unicode[0] = 0;
            MultiByteToWideChar(CP_UTF8, 0, text, -1, unicode, VGUI_RICHTEXT_MAX_LEN);
            unicode[VGUI_RICHTEXT_MAX_LEN - 1] = 0;
            this->SetText(unicode);
        }
	}
	else
	{
		this->SetText((const char*)nullptr);
	}
}

///////////////////////////////////////////////////////////////////////////////
void VVGUIRichTextCommon::Detour(const bool bAttach) const
{
	DetourSetup(&vgui__RichTextCommon__SetText, &RichTextCommon_SetText, bAttach);
}

///////////////////////////////////////////////////////////////////////////////