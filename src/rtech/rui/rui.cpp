//=============================================================================//
//
// Purpose: rUI Utilities
//
//=============================================================================//

#include "core/stdafx.h"

#include "rui.h"
#include "tier1/cvar.h"
#include <gameui/IRUIDebugger.h>
#include <vscript/languages/squirrel_re/include/squirrel.h>
#include <vscript/languages/squirrel_re/include/sqvm.h>
#include <vscript/languages/squirrel_re/vsquirrel.h>

static ConVar rui_drawEnable("rui_drawEnable", "1", FCVAR_RELEASE, "Draws the RUI if set", false, 0.f, false, 0.f, "1 = draw; 0 (zero) = no draw");
static ConVar rui_codeAsserts("rui_codeAsserts", "0", FCVAR_RELEASE, "Prints the RUI code assertions that fail to the console if set", false, 0.f, false, 0.f, "1 = print; 0 (zero) = no print");

//-----------------------------------------------------------------------------
// Purpose: draw RUI frame
//-----------------------------------------------------------------------------
static bool Rui_Draw(__int64* a1, __m128* a2, const __m128i* a3, __int64 a4, __m128* a5)
{
	if (!rui_drawEnable.GetBool())
		return false;

	return v_Rui_Draw(a1, a2, a3, a4, a5);
}

static void Rui_CodeAssert(RuiInstance_s* const ruiInstance, const char* const errorMsg)
{
	if (rui_codeAsserts.GetBool())
		Error(eDLL_T::UI, 0, "%s", errorMsg);

	Assert(0);
	ruiInstance->hasError = true;
}

static u32 Rui_CreateClientScriptInstance(RuiHeader_s* asset, RuiDrawGroup_e drawGroup, u16 hash, i32 a4, i32 a5)
{
	const u32 returnValue = v_Rui_CreateClientScriptInstance(asset, drawGroup, hash, a4, a5);
	const u16 handle = static_cast<u16>(returnValue);

	if (returnValue != 0xFFFFFFFF)
	{
		// exclude all the draw groups that i know are not intended for script
		if (drawGroup != 0 && drawGroup != 1 && drawGroup != 4)
		{
			CSquirrelVM* clientVm = g_pClientScript;
			HSQUIRRELVM vm = clientVm->GetVM();

			// hacky sure but it works!
			if (vm && vm->_stacklevel != 0)
			{
				SQStackInfos si;
				if (SQ_FAILED(v_sq_stackinfos(vm, 1, &si, vm->_stacklevel)))
					si.funcname = "(unk)";

				g_ruiDebugger.RecordSource(handle, si.funcname);
			}
			else
				g_ruiDebugger.RecordSource(handle, "(stacklevel == 0)");
		}
		else
			g_ruiDebugger.RecordSource(handle, "NATIVE");
	}

	return returnValue;
}

static void Rui_Destroy(u16 handle)
{
	//const RuiScriptInstance_s* scriptInstance = &s_ruiTracker->scriptInstances[handle & 0xFFF];

	//if (scriptInstance->instance)
	//	Msg(eDLL_T::UI, "Destroying client script RUI instance: \"%s\"\n", scriptInstance->instance->header->name);
	//else
	//	Msg(eDLL_T::UI, "Destroying client script RUI instance: handle %u\n", handle);

	g_ruiDebugger.RemoveSource(handle);

	return v_Rui_Destroy(handle);
}

static bool Rui_LoadAssetForVGuiPanel(void* a1, void* panel)
{
	const int v2 = *reinterpret_cast<int*>((char*)a1 + 4);

	// idk what any of these numbers mean
	if (v2 == 1)
		return true;

	if (v2 == 0)
	{
		RuiHeader_s* asset = *reinterpret_cast<RuiHeader_s**>((char*)a1 + 8);

		if (asset)
		{
			const u32 handle = Rui_CreateClientScriptInstance(asset, RUI_DRAW_NATIVE, 0, -1, 0);

			*reinterpret_cast<int*>(a1) = handle;

			if (handle != UINT32_MAX)
			{
				*reinterpret_cast<int*>((char*)a1 + 4) = 1;

				const char* panelName = (const char*)(*(__int64(__fastcall**)(void*))(*(_QWORD*)panel + 192LL))(panel);

				g_ruiDebugger.RecordSource(static_cast<u16>(handle), panelName);

				v_sub_140937E40(s_ruiTracker->scriptInstances[handle & 0xFFF].instance, panel);
				return true;
			}
		}
		*reinterpret_cast<int*>((char*)a1 + 4) = 2;
	}

	return false;
}

void V_Rui::Detour(const bool bAttach) const
{
	DetourSetup(&v_Rui_Draw, &Rui_Draw, bAttach);

	void* orgCodeAssertMethod;
	CMemory::HookVirtualMethod((uintptr_t)s_ruiApi, Rui_CodeAssert, 2, &orgCodeAssertMethod);

	DetourSetup(&v_Rui_LoadAssetForVGuiPanel, &Rui_LoadAssetForVGuiPanel, bAttach);
	DetourSetup(&v_Rui_CreateClientScriptInstance, &Rui_CreateClientScriptInstance, bAttach);
	DetourSetup(&v_Rui_Destroy, &Rui_Destroy, bAttach);
}
