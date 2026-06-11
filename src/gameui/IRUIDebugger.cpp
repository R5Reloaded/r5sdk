/******************************************************************************
-------------------------------------------------------------------------------
File   : IRUIDebugger.cpp
Date   : 20:04:2026
Author : rexx
Purpose: Implements a debugger overlay for Respawn UI (RUI) elements
-------------------------------------------------------------------------------
History:
- 20:04:2026 | 16:04 : Created by rexx

******************************************************************************/

#include "windows/id3dx.h"
#include "IRUIDebugger.h"
#include <tier1/convar.h>
#include <imgui.h>
#include <rtech/rui/rui.h>
#include <tier0/utility.h>

//-----------------------------------------------------------------------------
// Console variables
//-----------------------------------------------------------------------------
static ConVar rui_debugger("rui_debugger", "0", FCVAR_RELEASE, "Enables the RUI Debugger overlay", false, 0.f, false, 0.f);

//-----------------------------------------------------------------------------
// Purpose: constructor/destructor
//-----------------------------------------------------------------------------
CRUIDebugger::CRUIDebugger(void)
{
	m_surfaceLabel = "RUI Debugger";
	m_freezeCapture = false;
	m_lastAvailability = false;
	m_numAliveInstances = 0;
}
CRUIDebugger::~CRUIDebugger(void)
{
	Shutdown();
}

//-----------------------------------------------------------------------------
// Purpose: particle overlay initialization
//-----------------------------------------------------------------------------
bool CRUIDebugger::Init(void)
{
	SetStyleVar();
	m_initialized = true;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: particle overlay shutdown
//-----------------------------------------------------------------------------
void CRUIDebugger::Shutdown(void)
{
	//FreeScratchBuffer();
	m_initialized = false;
}

//-----------------------------------------------------------------------------
// Purpose: check value of cvars and determine availability of window
//-----------------------------------------------------------------------------
void CRUIDebugger::UpdateWindowAvailability(void)
{
	const bool enabled = rui_debugger.GetBool();

	if (enabled == m_lastAvailability)
		return;

	if (!enabled && m_activated)
	{
		m_activated = false;
		ResetInput();
	}

	else if (enabled && !m_activated)
		m_activated = true;

	m_lastAvailability = enabled;
}

//-----------------------------------------------------------------------------
// Purpose: run particle overlay frame
//-----------------------------------------------------------------------------
void CRUIDebugger::RunFrame(void)
{
	if (!m_initialized)
		Init();

	Animate();

	int baseWindowStyleVars = 0;
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_fadeAlpha); baseWindowStyleVars++;

	/*const bool drawn =*/ DrawSurface();
	ImGui::PopStyleVar(baseWindowStyleVars);

	//if (!drawn)
	//	FreeScratchBuffer();

	UpdateWindowAvailability();
}

//-----------------------------------------------------------------------------
// Purpose: syncs the cvar and updates the availability of mouse/key inputs
//-----------------------------------------------------------------------------
static void RUIDebugger_HandleClose(void)
{
	rui_debugger.SetValue(false);
	ResetInput();
}

//-----------------------------------------------------------------------------
// Purpose: draw particle overlay
//-----------------------------------------------------------------------------
bool CRUIDebugger::DrawSurface(void)
{
	if (!IsVisible())
		return false;

	if (!ImGui::Begin(m_surfaceLabel, &m_activated, ImGuiWindowFlags_None, &RUIDebugger_HandleClose))
	{
		ImGui::End();
		return false;
	}

	SetRect(567, 367, 10, 10);

	ImGui::Checkbox("Freeze##RUIDebugger_DebugOut", &m_freezeCapture);

	if(ImGui::Button("Capture##RUIDebugger_DebugOut"))
		RecreateElementList();

	if (ImGui::Button("Print##RUIDebugger_DebugOut"))
	{
		std::lock_guard guard(this->scratchMutex);
		Msg(eDLL_T::UI, "%s\n", buffer.c_str());
	}

	ImGui::Text("%u/%u alive", m_numAliveInstances, MAX_RUI_SCRIPT_INSTANCES);

	if (ImGui::BeginChild("##RUIDebugger_DebugOut", ImVec2(-1, -1), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
	{
		std::lock_guard guard(this->scratchMutex);
		ImGui::TextUnformatted(buffer.c_str());
	}

	ImGui::EndChild();
	ImGui::End();

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: appends text to the scratch buffer
//-----------------------------------------------------------------------------
void CRUIDebugger::AppendText(const char* const text, const size_t textLen)
{
	if (!m_activated || m_freezeCapture)
		return;

	std::lock_guard guard(this->scratchMutex);
	buffer = text;
}

void CRUIDebugger::RecreateElementList()
{
	if (m_freezeCapture)
		return;

	std::string buf;

	const u32 oldAliveCount = m_numAliveInstances;

	m_numAliveInstances = 0;

	for (u32 i = 0; i < std::size(s_ruiTracker->scriptInstances); ++i)
	{
		RuiScriptInstance_s* inst = &s_ruiTracker->scriptInstances[i];

		// dunno
		if (inst->state >= 3 || inst->state < 0)
			continue;

		const char* name = "(unknown)";
		if (!inst->instance || !inst->instance->header)
			continue;

		name = inst->instance->header->name;

		const char* changeIndicator = "    ";
		if (i > oldAliveCount)
			changeIndicator = "  + ";

		buf += Format("%s%i: %s (%s) %s\n", changeIndicator, i, name, s_ruiStateNames[inst->state], instSources[i].c_str());

		m_numAliveInstances++;
	}

	this->AppendText(buf.c_str(), buf.length());
}

CRUIDebugger g_ruiDebugger;
