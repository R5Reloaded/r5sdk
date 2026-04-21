#pragma once
#include "imgui_surface.h"
#include <mutex>

class CRUIDebugger : public CImguiSurface
{
public:
	CRUIDebugger(void);
	~CRUIDebugger(void);

	virtual bool Init(void);
	virtual void Shutdown(void);

	virtual void RunFrame(void);
	virtual bool DrawSurface(void);

	void UpdateWindowAvailability(void);

	void AppendText(const char* const text, const size_t textLen);
	void RecreateElementList();

	inline bool IsFrozen() const { return m_freezeCapture; }

	inline void RecordSource(u16 handle, const std::string& funcName)
	{
		assert((handle & 0xFFF) < 2704);
		std::lock_guard guard(sourceMutex);
		instSources[handle & 0xFFF] = funcName;
	}

	inline void RemoveSource(u16 handle)
	{
		assert((handle & 0xFFF) < 2704);
		std::lock_guard guard(sourceMutex);
		instSources[handle & 0xFFF] = "";
	}

private:
	std::string buffer;

	u32 m_numAliveInstances;

	bool m_freezeCapture;
	bool m_lastAvailability;

	std::mutex scratchMutex;
	std::mutex sourceMutex;

	std::string instSources[2704];
};

extern CRUIDebugger g_ruiDebugger;
