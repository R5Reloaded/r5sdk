/******************************************************************************
-------------------------------------------------------------------------------
File   : IParticleOverlay.cpp
Date   : 20:03:2025
Author : Kawe Mazidjatari
Purpose: Implements the in-game particle debug overlay
-------------------------------------------------------------------------------
History:
- 20:03:2025 | 09:07 : Created by Kawe Mazidjatari

******************************************************************************/

#include "windows/id3dx.h"
#include "IParticleOverlay.h"

//-----------------------------------------------------------------------------
// Console variables
//-----------------------------------------------------------------------------
static ConVar particle_overlay_list_memory("particle_overlay_list_memory", "262144", FCVAR_DEVELOPMENTONLY, "Total string memory to allocate for the particle overlay list.", true, 1.f, false, 0.0f);
extern ConVar particle_overlay_list_enable;

//-----------------------------------------------------------------------------
// Purpose: constructor/destructor
//-----------------------------------------------------------------------------
CParticleOverlay::CParticleOverlay(void)
{
	m_surfaceLabel = "Particle Overlay";
	m_scratchBuffer = nullptr;
	m_scratchBufferSize = 0;
	m_bufferCursor = 0;
	m_freezeCapture = false;
	m_lastAvailability = false;
}
CParticleOverlay::~CParticleOverlay(void)
{
	Shutdown();
}

//-----------------------------------------------------------------------------
// Purpose: particle overlay initialization
//-----------------------------------------------------------------------------
bool CParticleOverlay::Init(void)
{
	SetStyleVar();
	m_initialized = true;

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: particle overlay shutdown
//-----------------------------------------------------------------------------
void CParticleOverlay::Shutdown(void)
{
	FreeScratchBuffer();
	m_initialized = false;
}

//-----------------------------------------------------------------------------
// Purpose: check value of cvars and determine availability of window
//-----------------------------------------------------------------------------
void CParticleOverlay::UpdateWindowAvailability(void)
{
	const bool enabled = particle_overlay->GetBool() && particle_overlay_list_enable.GetBool() && !particle_overlay_old->GetBool();

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
void CParticleOverlay::RunFrame(void)
{
	if (!m_initialized)
		Init();

	Animate();

	int baseWindowStyleVars = 0;
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_fadeAlpha); baseWindowStyleVars++;

	const bool drawn = DrawSurface();
	ImGui::PopStyleVar(baseWindowStyleVars);

	if (!drawn)
		FreeScratchBuffer();

	UpdateWindowAvailability();
}

//-----------------------------------------------------------------------------
// Purpose: syncs the cvar and updates the availability of mouse/key inputs
//-----------------------------------------------------------------------------
static void ParticleOverlay_HandleClose(void)
{
	particle_overlay_list_enable.SetValue(false);
	ResetInput();
}

//-----------------------------------------------------------------------------
// Purpose: draw particle overlay
//-----------------------------------------------------------------------------
bool CParticleOverlay::DrawSurface(void)
{
	if (!IsVisible())
		return false;

	if (!ImGui::Begin(m_surfaceLabel, &m_activated, ImGuiWindowFlags_None, &ParticleOverlay_HandleClose))
	{
		ImGui::End();
		return false;
	}

	SetRect(567, 367, 10, 10);

	ImGui::Checkbox("Freeze##ParticleOverlay_ParticleReport", &m_freezeCapture);

	if (ImGui::BeginChild("##ParticleOverlay_ParticleReport", ImVec2(-1, -1), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::TextUnformatted(m_scratchBuffer, &m_scratchBuffer[m_bufferCursor]);
	}

	ImGui::EndChild();
	ImGui::End();

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: dynamically scale the scratch buffer if it became smaller or larger
//-----------------------------------------------------------------------------
bool CParticleOverlay::ResizeScratchBuffer(const size_t newSize)
{
	Assert(newSize > 0);

	if (newSize == m_scratchBufferSize)
		return false; // Same size.

	if (m_scratchBuffer)
		delete[] m_scratchBuffer;

	m_scratchBuffer = new char[newSize];
	m_scratchBufferSize = newSize;

	m_scratchBuffer[0] = '\0';
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: free the scratch buffer if we have one
//-----------------------------------------------------------------------------
void CParticleOverlay::FreeScratchBuffer(void)
{
	if (m_scratchBuffer)
	{
		delete[] m_scratchBuffer;

		m_scratchBuffer = nullptr;
		m_scratchBufferSize = 0;
		m_bufferCursor = 0;
	}
	else
		Assert(m_scratchBufferSize == 0);
}

//-----------------------------------------------------------------------------
// Purpose: begin the frame
//-----------------------------------------------------------------------------
void CParticleOverlay::Begin()
{
	if (!m_freezeCapture)
		m_bufferCursor = 0;
}

//-----------------------------------------------------------------------------
// Purpose: end the frame
//-----------------------------------------------------------------------------
void CParticleOverlay::End()
{
	if (m_scratchBuffer)
		m_scratchBuffer[m_bufferCursor] = '\0';
}

//-----------------------------------------------------------------------------
// Purpose: appends text to the scratch buffer
//-----------------------------------------------------------------------------
void CParticleOverlay::AppendText(const char* const text, const size_t textLen)
{
	if (!m_activated || m_freezeCapture)
		return;

	ResizeScratchBuffer(particle_overlay_list_memory.GetInt());

	if (m_bufferCursor >= m_scratchBufferSize)
		return; // Out of room, increase `particle_overlay_list_memory`.

	const size_t roomLeft = m_scratchBufferSize - m_bufferCursor;
	const size_t numBytesToCopy = Min(textLen, roomLeft);

	strncpy(&m_scratchBuffer[m_bufferCursor], text, numBytesToCopy);
	m_bufferCursor += numBytesToCopy;
}

CParticleOverlay g_particleOverlay;
