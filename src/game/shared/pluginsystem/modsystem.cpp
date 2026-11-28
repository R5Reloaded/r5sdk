//=============================================================================//
// 
// Purpose: Script-side ModSystem implementation
// 
//=============================================================================//
#include "pluginsystem/modsystem.h"
#include "game/shared/vscript_gamedll_defs.h"
#include "game/shared/vscript_shared.h"

static SQRESULT RunModCallbacks(HSQUIRRELVM v, const char* callbackSuffix, bool optional)
{
	if (!ModSystem()->IsEnabled())
		SCRIPT_CHECK_AND_RETURN(v, SQ_OK);

	CSquirrelVM* const s = v->GetScriptVM();
	ModSystem()->LockModList();

	FOR_EACH_VEC(ModSystem()->GetModList(), i)
	{
		const CModSystem::ModInstance_t* const mod = ModSystem()->GetModList()[i];

		if (!mod->IsEnabled())
			continue;

		if (!mod->hasPrecompiledScripts)
			continue;

		const CUtlString& modIdNormalized = ModSystem()->GetNormalizedModID(mod);
		CUtlString modCodeCBName;

		modCodeCBName.Format("%s_%s_%s", Script_GetCodeCallbackPrefixForContext(v->GetContext()), modIdNormalized.String(), callbackSuffix);
		HSCRIPT modCodeCB = s->FindFunction(modCodeCBName.String(), "void functionref()", NULL);

		if (!modCodeCB)
		{
			if (!optional)
				Warning(v->GetNativeContext(), "Mod '%s'(\"%s\") has precompiled scripts, but entry point \"%s()\" was not found!\n",
				mod->name.String(), mod->id.String(), modCodeCBName.String());

			continue;
		}

		s->ExecuteFunction(modCodeCB, nullptr, 0, nullptr, NULL);
		free(modCodeCB);
	}

	ModSystem()->UnlockModList();
	SCRIPT_CHECK_AND_RETURN(v, SQ_OK);
}

static SQRESULT SharedScript_ModSystem_RunCallbacks(HSQUIRRELVM v)
{
	return RunModCallbacks(v, "ModInit", false);
}

static SQRESULT SharedScript_ModSystem_RunPreInitCallbacks(HSQUIRRELVM v)
{
	return RunModCallbacks(v, "ModPreInit", true);
}

void Script_RegisterModSystemFunctions(CSquirrelVM* const s)
{
	DEFINE_SHARED_SCRIPTFUNC_NAMED(s, ModSystem_RunCallbacks, "Initiates the code callbacks for all mods registered by the modsystem", "void", "", false);
	DEFINE_SHARED_SCRIPTFUNC_NAMED(s, ModSystem_RunPreInitCallbacks, "Runs optional early mod initialization callbacks", "void", "", false);
}
