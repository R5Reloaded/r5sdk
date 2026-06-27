//=============================================================================//
// 
// Purpose: Dear ImGui engine implementation
// 
//=============================================================================//

#include "imgui/misc/imgui_snapshot.h"
#include "engine/sys_mainwind.h"
#include "windows/id3dx.h"

#include "tier1/keyvalues.h"
#include "filesystem/filesystem.h"
#include "imgui_system.h"
#include "imgui/misc/ImGuiNotify.hpp"
#include "inputsystem/inputstacksystem.h"

//-----------------------------------------------------------------------------
// Constructors/Destructors.
//-----------------------------------------------------------------------------
CImguiSystem::CImguiSystem()
	: m_enabled(false)
	, m_initialized(false)
	, m_hasNewFrame(false)
	, m_repeatFrame(false)
	, m_hImguiInputCtx(INPUT_CONTEXT_HANDLE_INVALID)
{
}

//-----------------------------------------------------------------------------
// Initializes the imgui system. If this fails, false would be returned and the
// implementation won't run.
//-----------------------------------------------------------------------------
bool CImguiSystem::Init()
{
	Assert(ThreadInMainThread(), "CImguiSystem::Init() should only be called from the main thread!");
	Assert(!IsInitialized(), "CImguiSystem::Init() called recursively?");

	Assert(IsEnabled(), "CImguiSystem::Init() called while system was disabled!");

	///////////////////////////////////////////////////////////////////////////
	IMGUI_CHECKVERSION();
	ImGuiContext* const context = ImGui::CreateContext();

	if (!context)
	{
		m_enabled = false;
		return false;
	}

	AUTO_LOCK(m_snapshotBufferMutex);
	AUTO_LOCK(m_inputEventQueueMutex);

	// This is required to disable the ctrl+tab menu as some users use this
	// shortcut for other things in-game. See: https://github.com/ocornut/imgui/issues/4828
	context->ConfigNavWindowingKeyNext = 0;
	context->ConfigNavWindowingKeyPrev = 0;

	ImGuiViewport* const vp = ImGui::GetMainViewport();
	vp->PlatformHandleRaw = g_pGame->GetWindow();

	SetupIO();

	if (!ImGui_ImplWin32_Init(g_pGame->GetWindow()) || 
		!ImGui_ImplDX11_Init(D3D11Device(), D3D11DeviceContext()))
	{
		Assert(0);

		m_enabled = false;
		return false;
	}

    m_hImguiInputCtx = g_pInputStackSystem->PushInputContext();

	m_initialized = true;
	m_hasNewFrame = false;

	return true;
}

//-----------------------------------------------------------------------------
// Shuts the imgui system down, frees all allocated buffers.
//-----------------------------------------------------------------------------
void CImguiSystem::Shutdown()
{
	Assert(ThreadInMainThread(), "CImguiSystem::Shutdown() should only be called from the main thread!");
	Assert(IsInitialized(), "CImguiSystem::Shutdown() called recursively?");

	Assert(IsEnabled(), "CImguiSystem::Shutdown() called while system was disabled!");

	AUTO_LOCK(m_snapshotBufferMutex);
	AUTO_LOCK(m_inputEventQueueMutex);

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();

	m_snapshotData.Clear();
	ImGui::DestroyContext();

    g_pInputStackSystem->PopInputContext( m_hImguiInputCtx );

	m_initialized = false;
	m_hasNewFrame = false;
    
	m_surfaceList.Purge();
}

//-----------------------------------------------------------------------------
// Sets the imgui system IO up.
//-----------------------------------------------------------------------------
void CImguiSystem::SetupIO() const
{
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_IsSRGB;

	SetupFonts();
}

//-----------------------------------------------------------------------------
// Retrieves glyph range by name
//-----------------------------------------------------------------------------
static const ImWchar* ImguiSystem_GetGlyphRangeForName(const char* const range, ImGuiIO& io)
{
	if (V_strcmp(range, "latin") == 0)
		return io.Fonts->GetGlyphRangesDefault();
	if (V_strcmp(range, "greek") == 0)
		return io.Fonts->GetGlyphRangesGreek();
	if (V_strcmp(range, "korean") == 0)
		return io.Fonts->GetGlyphRangesKorean();
	if (V_strcmp(range, "japanese") == 0)
		return io.Fonts->GetGlyphRangesJapanese();
	if (V_strcmp(range, "tchinese") == 0)
		return io.Fonts->GetGlyphRangesChineseFull();
	if (V_strcmp(range, "schinese") == 0)
		return io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
	if (V_strcmp(range, "cyrillic") == 0)
		return io.Fonts->GetGlyphRangesCyrillic();
	if (V_strcmp(range, "thai") == 0)
		return io.Fonts->GetGlyphRangesThai();
	if (V_strcmp(range, "vietnamese") == 0)
		return io.Fonts->GetGlyphRangesVietnamese();

	return nullptr;
}

//-----------------------------------------------------------------------------
// Parses the font configuration and sets it up for the imgui system
//-----------------------------------------------------------------------------
void CImguiSystem::SetupFonts() const
{
	static const char* const configFilePath = "resource/imgui_fonts.txt";
	KeyValues configKV("ImguiFonts");

	if (!configKV.LoadFromFile(FileSystem(), configFilePath, "GAME"))
		return;

	ImVector<ImWchar> rangesArray[IMGUI_SYSTEM_MAX_FONTS];
	ImGuiIO& io = ImGui::GetIO();

	int i = 0;
	bool ranFirst = false; // First is always the base, subsequent fonts get merged into base.

	for (KeyValues* pSubKey = configKV.GetFirstSubKey(); pSubKey != nullptr; pSubKey = pSubKey->GetNextKey())
	{
		if (i >= IMGUI_SYSTEM_MAX_FONTS)
			Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: file \"%s\" lists too many fonts; increase IMGUI_SYSTEM_MAX_FONTS!\n", __FUNCTION__);

		const char* const fontFileName = pSubKey->GetName();
		const float fontSizePixels = pSubKey->GetFloat("size", 13);

		ImVector<ImWchar>& ranges = rangesArray[i++]; bool hasRange = false;
		KeyValues* const rangesKV = pSubKey->FindKey("ranges");

		if (rangesKV)
		{
			ImFontGlyphRangesBuilder builder;

			for (KeyValues* pSubRange = rangesKV->GetFirstSubKey(); pSubRange != nullptr; pSubRange = pSubRange->GetNextKey())
			{
				const char* const rangeName = pSubRange->GetString();
				const ImWchar* const range = ImguiSystem_GetGlyphRangeForName(rangeName, io);

				if (!range)
					Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: glyph range \"%s\" doesn't exist!\n", __FUNCTION__, rangeName);

				builder.AddRanges(range);
				hasRange = true;
			}

			if (hasRange)
				builder.BuildRanges(&ranges);
		}

		LoadFont(fontFileName, ranFirst, fontSizePixels, hasRange ? ranges.Data : nullptr);
		ranFirst = true;
	}

	ImFontConfig config;
	config.MergeMode = true;
	config.OversampleH = 2;
	config.OversampleV = 2;

	io.Fonts->AddFontFromMemoryCompressedBase85TTF(Codicons_compressed_data, 14.f, &config);

	io.Fonts->Build();
}

//-----------------------------------------------------------------------------
// Loads the font to be used for the imgui system.
//-----------------------------------------------------------------------------
void CImguiSystem::LoadFont(const char* const fontPath, const bool mergeMode, const float sizePixels, ImWchar* const ranges) const
{
	FileHandle_t fontFile = FileSystem()->Open(fontPath, "rb", "GAME");

	if (fontFile == FILESYSTEM_INVALID_HANDLE)
		Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: font file \"%s\" couldn't be opened!\n", __FUNCTION__, fontPath);

	const ssize_t fontSize = FileSystem()->Size(fontFile);

	if (fontSize <= 100) // See assert in ImFontAtlas::AddFontFromMemoryTTF( ... )
		Error(eDLL_T::ENGINE, EXIT_FAILURE, "%s: font file \"%s\" appears truncated!\n", __FUNCTION__, fontPath);

	// NOTE: shouldn't be deleted! Dear ImGui needs it internally.
	u8* const fontBuf = new u8[fontSize];

	FileSystem()->Read(fontBuf, fontSize, fontFile);
	FileSystem()->Close(fontFile);

	ImFontConfig config;
	config.MergeMode = mergeMode;

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromMemoryTTF(fontBuf, (int)fontSize, sizePixels, &config, ranges);
}

//-----------------------------------------------------------------------------
// Add an imgui surface.
//-----------------------------------------------------------------------------
void CImguiSystem::AddSurface(CImguiSurface* const surface)
{
	Assert(IsInitialized());
	m_surfaceList.AddToTail(surface);
}

//-----------------------------------------------------------------------------
// Remove an imgui surface.
//-----------------------------------------------------------------------------
void CImguiSystem::RemoveSurface(CImguiSurface* const surface)
{
	Assert(!IsInitialized());
	m_surfaceList.FindAndRemove(surface);
}

inline void ImGuiSystem_RenderNotifications()
{
	// Notifications style setup
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f); // Disable round borders
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f); // Disable borders

	// Notifications color setup
	//ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 1.00f)); // Background color

	ImGui::PushFont(NULL, 21.f);
	// Main rendering function
	ImGui::RenderNotifications();
	ImGui::PopFont();

	//——————————————————————————————— WARNING ———————————————————————————————
	// Argument MUST match the amount of ImGui::PushStyleVar() calls 
	ImGui::PopStyleVar(2);
	// Argument MUST match the amount of ImGui::PushStyleColor() calls 
	//ImGui::PopStyleColor(1);
}

//-----------------------------------------------------------------------------
// Draws the ImGui panels and applies all queued input events.
//-----------------------------------------------------------------------------
void CImguiSystem::SampleFrame()
{
	Assert(ThreadInMainThread(), "CImguiSystem::SampleFrame() should only be called from the main thread!");
	Assert(IsInitialized());

	AUTO_LOCK(m_inputEventQueueMutex);

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();

	ImGui::NewFrame();

	FOR_EACH_VEC(m_surfaceList, i)
	{
		CImguiSurface* const surface = m_surfaceList[i];
		surface->RunFrame();
	}
	
    const bool bSurfaceActive = IsSurfaceActive();
    g_pInputStackSystem->SetCursorVisible( m_hImguiInputCtx, bSurfaceActive );
	g_pInputStackSystem->EnableInputContext( m_hImguiInputCtx, bSurfaceActive );

    ImGuiSystem_RenderNotifications();

	ImGui::EndFrame();
	ImGui::Render();
}

//-----------------------------------------------------------------------------
// Copies currently drawn data into the snapshot buffer which is queued to be
// rendered in the render thread. This should only be called from the same
// thread SampleFrame() is being called from.
//-----------------------------------------------------------------------------
void CImguiSystem::SwapBuffers()
{
	Assert(ThreadInMainThread(), "CImguiSystem::SwapBuffers() should only be called from the main thread!");
	Assert(IsInitialized());

	ImDrawData* const drawData = ImGui::GetDrawData();
	Assert(drawData);

	// Nothing has been drawn, nothing to swap.
	if (!drawData->CmdListsCount)
		return;

	AUTO_LOCK(m_snapshotBufferMutex);

	m_snapshotData.SnapUsingSwap(drawData, ImGui::GetTime());

	m_hasNewFrame = true;
	m_repeatFrame = true;
}

//-----------------------------------------------------------------------------
// Renders the drawn frame out which has been swapped to the snapshot buffer.
//-----------------------------------------------------------------------------
void CImguiSystem::RenderFrame()
{
	Assert(IsInitialized());

	if (!m_hasNewFrame.exchange(false) && !m_repeatFrame.exchange(false))
		return;

	AUTO_LOCK(m_snapshotBufferMutex);
	ImGui_ImplDX11_RenderDrawData(&m_snapshotData.DrawData);
}

//-----------------------------------------------------------------------------
// Checks whether we have an active surface.
//-----------------------------------------------------------------------------
bool CImguiSystem::IsSurfaceActive() const
{
	FOR_EACH_VEC(m_surfaceList, i)
	{
		if (m_surfaceList[i]->IsActivated())
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// Window procedure handler.
//-----------------------------------------------------------------------------
LRESULT CImguiSystem::MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (!ImguiSystem()->IsInitialized())
		return NULL;

	AUTO_LOCK(ImguiSystem()->m_inputEventQueueMutex);

	extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
}

static CImguiSystem s_imguiSystem;

CImguiSystem* ImguiSystem()
{
	return &s_imguiSystem;
}
