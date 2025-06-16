#ifndef HUD_BASECHAT_H
#define HUD_BASECHAT_H

#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/TextEntry.h"
#include "vgui_controls/RichText.h"

class CBaseHudChatInputLine;
class CHudChatHistory;

class CBaseHudChat : public vgui::EditablePanel
{
public:
	void PrintSystemMsg(const char* const pszPrefixStr, const char* const pszMsgText, const bool bAdminMsg);

private:
	char m_gap02C8[8];
	unsigned int m_hChatFont;
	int16_t m_hRuiFont;
	int16_t m_hRuiAsianFont;
	int m_RuiFontHeight;
	int m_RuiMinFontHeight;
	char m_gap2D4[8];
	int m_dword2E8;
	char m_gap2EC[12];
	Color m_TeamColors[2];
	Color m_clrText;
	char m_gap308[20];
	CBaseHudChatInputLine* m_pInputLine;
	CHudChatHistory* m_pChatHistory;
	char m_gap328[16];
};

static_assert(sizeof(CBaseHudChat) == 0x340);

class CBaseHudChatEntry : public vgui::TextEntry
{
	char m_gap000[12];
	CBaseHudChat* m_pHudChat;
};

class CBaseHudChatInputLine : public vgui::Panel
{
private:
	CBaseHudChat* m_pHudChat;
	class Label* m_pPrompt;
	CBaseHudChatEntry* m_pInput;
};

static_assert(sizeof(CBaseHudChatInputLine) == 0x290);

class CHudChatHistory : public vgui::RichText
{
	char m_gap000[8];
};

static_assert(sizeof(CHudChatHistory) == 0x3D0);

inline CBaseHudChat** g_ppHudChat;

class VHudChat : public IDetour
{
	virtual void GetAdr(void) const 
	{
		LogVarAdr("g_pHudChat", g_ppHudChat);
	}

	virtual void GetFun(void) const {}
	virtual void GetVar(void) const 
	{
		Module_FindPattern(g_GameDll, "48 8B 1D ?? ?? ?? ?? 8B 70").ResolveRelativeAddressSelf(0x3, 0x7).GetPtr(g_ppHudChat);
	}
	virtual void GetCon(void) const {}
	virtual void Detour(const bool bAttach) const {};
};

#endif HUD_BASECHAT_H