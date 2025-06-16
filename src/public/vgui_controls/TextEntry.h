//========= Copyright (c) 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: A Class to create a window that you can type and edit text in.
//          Window can hold single line or multiline text. 
//          If it is single it can scroll horizontally in response to 
//          key input and mouse selection.
//
// $NoKeywords: $
//=============================================================================//

#ifndef TEXTENTRY_H
#define TEXTENTRY_H

#include "vgui/vgui.h"
#include "Panel.h"
#include "tier1/utlvector.h"

namespace vgui
{
	class TextEntry : public Panel
	{

	private:
		char m_gap270[424];
		bool m_bAllowNonAsciiCharacters;
		char m_gap419[7];
		CUtlVector<wchar_t> m_TextStream;
		CUtlVector<wchar_t> m_UndoTextStream;
		CUtlVector<int> m_LineBreaks;
		int _cursorPos;
		bool _cursorIsAtEnd;
		bool _putCursorAtEnd;
		char m_gap486[2];
		int _undoCursorPos;
		bool _cursorBlink;
		bool _hideText;
		bool _editable;
		char m_gap48F[1];
		bool _mouseDragSelection;
		char m_gap491[3];
		int _cursorNextBlinkTime;
		int _cursorBlinkRate;
		int _select[2];
		int _pixelsIndent;
		int _charCount;
		int _maxCharCount;
		int _font;
		char m_gap4B4[20];
		bool _dataChanged;
		bool _multiline;
		char m_gap4C9[6];
		class ScrollBar* _vertScrollBar;
		char m_gap4D8[4];
		Color _disabledFgColor;
		Color _disabledBgColor;
		Color _focusedBgColor;
		Color _selectionColor;
		Color _selectionTextColor;
		Color _defaultSelectionBG2Color;
		char m_gap4E0[4];
		int _currentStartLine;
		int _currentStartIndex;
		bool _horizScrollingAllowed;
		char m_gap4FD[5];
		bool _catchTabKey;
		char m_gap503[1];
		bool _sendNewLines;
		char m_gap505[7];
		int _tabSpaces;
		char m_gap510[16];
		int _recalculateBreaksIndex;
		char m_gap524[2];
		bool m_bAutoProgressOnHittingCharLimit;
		char m_gap526[521];
		int m_hPreviousIME;
		char m_gap730[32];
	};
}



#endif TEXTENTRY_H