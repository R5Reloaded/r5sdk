#include "core/stdafx.h"
#include "engine/client/vengineclient_impl.h"
#include "game/client/cliententitylist.h"
#include "hud_basechat.h"
#include "edict.h"
#include "engine/client/cl_main.h"
#include "localize/localize.h"

static ConVar hudchat_ignore_server_messages("hudchat_ignore_server_messages", "0", FCVAR_RELEASE | FCVAR_ARCHIVE, "Disables server messages appearing in the chat box");

void CBaseHudChat::PrintSystemMsg(const char* const pszPrefixStr, const char* const pszMsgText, const bool bAdminMsg)
{
	if (!bAdminMsg && (!hudchat_visibility->GetBool() || hudchat_ignore_server_messages.GetBool()))
		return;

	static Color adminColour(186, 13, 13, 255);
	const float flMessageShowDuration = hudchat_new_message_shown_duration->GetFloat();
	const float flMessageFadeDuration = hudchat_new_message_fade_duration->GetFloat();

	m_pChatHistory->InsertChar('\n');

	m_pChatHistory->InsertColorChange(bAdminMsg ? adminColour : m_clrText);

	m_pChatHistory->InsertText(pszPrefixStr);
	m_pChatHistory->InsertText(L": ");

	m_pChatHistory->InsertFade(flMessageShowDuration, flMessageFadeDuration);

	m_pChatHistory->InsertColorChange(m_clrText);

	m_pChatHistory->InsertText(pszMsgText);

	//Set the fade for our text
	m_pChatHistory->InsertFade(flMessageShowDuration, flMessageFadeDuration);
}
