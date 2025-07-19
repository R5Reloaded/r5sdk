#pragma once
#include "Panel.h"
#include <engine/server/sv_main.h>
#include <tier1/utlvector.h>
#include <vguimatsurface/MatSystemSurface.h>

#define VGUI_RICHTEXT_MAX_LEN 8192

namespace vgui
{
	class RichTextCommon : public Panel
	{
	public:
		void SetText(const char* text);
		
		void SetText(wchar_t* pwszStr) 
		{
			CallVFunc<void>(239, this, pwszStr);
		}

		const wchar_t* ResolveLocalizedTextAndVariables(const char* pchLookup, wchar_t* outbuf, size_t outbufsizeinbytes) 
		{
			return CallVFunc<const wchar_t*>(283, this, pchLookup, outbuf, outbufsizeinbytes);
		}

		void InsertChar(wchar_t ch)
		{
			return CallVFunc<void>(250, this, ch);
		}

		void InsertColorChange(Color col)
		{
			return CallVFunc<void>(268, this, col);
		}

		void InsertText(const wchar_t* pwszText)
		{
			return CallVFunc<void>(251, this, pwszText);
		}

		void InsertText(const char* pszText)
		{
			return CallVFunc<void>(252, this, pszText);
		}

		void InsertFade(float flSustain, float flLength)
		{
			return CallVFunc<void>(270, this, flSustain, flLength);
		}

	private:
		struct TFade
		{
			float flFadeStartTime;
			float flFadeLength;
			float flFadeSustain;
			int iOriginalAlpha;
		};

		struct TFormatStream
		{
			Color m_color;
			int pixelsIndent;
			TFade fade;
			int m_textStreamIndex;
		};

		char m_gap0270[8];
		class ScrollBar* _vertScrollBar;
		bool m_bResetFades;
		char m_gap281[2];
		bool m_bUnusedScrollbarInvis;
		char m_gap283[4];
		CUtlVector<TFormatStream> m_TextStream;
		CUtlVector<int> m_LineBreaks;
		CUtlVector<TFormatStream> m_FormatStream;
		bool m_bRecalcLineBreaks;
		int _recalculateBreaksIndex;
		bool _invalidateVerticalScrollbarSlider;
		int _cursorPos;
		char m_gap300[4];
		int _select[2];
		char _pixelsIndent[4];
		int _maxCharCount;
		unsigned int _font;
		char m_gap310[4];
		uint16_t m_hRuiFont;
		char m_gap316[2];
		int m_SomeFontHeight;
		int m_RuiFontMinHeight;
		char m_gap0320[48];
		CUtlVector<class ClickPanel*> _clickableTextPanels;
		char m_gap0370[8];
		int _drawOffsetX;
		int _drawOffsetY;
		char m_gap0384[8];
		class Menu* m_pEditMenu;
		char m_gap0390[8];
		bool _recalcSavedRenderState;
		char m_gap399[11];
		int m_unk;
		char m_gap03A8[16];
	};

	class RichText : public RichTextCommon
	{
	private:
		//[ROBOTIC]: Probably part of common, but not 100% sure
		char m_gap3B8[8];
	};
};

/* ==== RICHTEXT ===================================================================================================================================================== */
inline void(*vgui__RichTextCommon__SetText)(vgui::RichText* thisptr, const char* text);

///////////////////////////////////////////////////////////////////////////////
class VVGUIRichTextCommon : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("vgui::RichTextCommon::SetText", vgui__RichTextCommon__SetText);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 B8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 2B E0 48 8B D9").GetPtr(vgui__RichTextCommon__SetText);
	}
	virtual void GetVar(void) const { }
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////
