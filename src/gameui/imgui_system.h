//=============================================================================//
// 
// Purpose: Dear ImGui engine implementation
// 
//=============================================================================//
#ifndef IMGUI_SYSTEM_H
#define IMGUI_SYSTEM_H
#include "imgui/misc/imgui_snapshot.h"
#include "imgui_surface.h"

// Max number of fonts to be loaded and merged.
#define IMGUI_SYSTEM_MAX_FONTS 16

class CImguiSystem
{
public:
	CImguiSystem();

	bool Init();
	void Shutdown();

	void SetupIO() const;
	void SetupFonts() const;

	void LoadFont(const char* const fontPath, const bool mergeMode, const float sizePixels, ImWchar* const ranges) const;

	void AddSurface(CImguiSurface* const surface);
	void RemoveSurface(CImguiSurface* const surface);

	void SwapBuffers();

	void SampleFrame();
	void RenderFrame();

	bool IsSurfaceActive() const;

	bool IsOccluded() const;
	void UpdateOcclusionStatus();

	void ClampActiveWindowToScreenRect() const;

	// statics:
	static LRESULT MessageHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	// inlines:
	inline bool IsEnabled() const { return m_enabled; };
	inline bool IsInitialized() const { return m_initialized; };

	// when explicitly disabled, surfaces such as the console could query
	// whether to run code that isn't directly tied to rendering, i.e. to
	// check if we should store logs for rendering.
	inline void SetEnabled(const bool enable) { Assert(!m_initialized); m_enabled = enable; }

private:
	ImDrawDataSnapshot m_snapshotData;
	CUtlVector<CImguiSurface*> m_surfaceList;

	// Mutex used during swapping and rendering, we draw the windows in the
	// main thread, and render it in the render thread. The only place this
	// mutex is used is during snapshot swapping and during rendering
	mutable CThreadMutex m_snapshotBufferMutex;

	// Mutex used between ImGui window procedure handling and drawing, see
	// https://github.com/ocornut/imgui/issues/6895. In this engine the window
	// is ran in thread separate from the main thread, therefore it needs a
	// lock to control access as main calls SampleFrame().
	mutable CThreadMutex m_inputEventQueueMutex;

	bool m_enabled;
	bool m_initialized;
	bool m_isOccluded;
	bool m_hasActiveSurfacesThisFrame;

	std::atomic_bool m_hasNewFrame;
	std::atomic_bool m_repeatFrame;
};

CImguiSystem* ImguiSystem();

#endif // IMGUI_SYSTEM_H
