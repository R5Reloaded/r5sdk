#ifndef RTECH_RUI_H
#define RTECH_RUI_H

struct RuiInstance_s;

enum class RuiArgumentType_e : u8
{
	TYPE_STRING = 0x1,
	TYPE_ASSET = 0x2,
	TYPE_BOOL = 0x3,
	TYPE_INT = 0x4,
	TYPE_FLOAT = 0x5,
	TYPE_FLOAT2 = 0x6,
	TYPE_FLOAT3 = 0x7,
	TYPE_COLOR_ALPHA = 0x8,
	TYPE_GAMETIME = 0x9,
	TYPE_WALLTIME = 0xA,
	TYPE_UIHANDLE = 0xB,
	TYPE_IMAGE = 0xC,
	TYPE_FONT_FACE = 0xD,
	TYPE_FONT_HASH = 0xE,
	TYPE_ARRAY = 0xF,
};

// must match game dll
enum RuiDrawGroup_e : int
{
	RUI_DRAW_NATIVE = 0,
	RUI_DRAW_UNK_1 = 1,
	RUI_DRAW_WORLD = 2,
	RUI_DRAW_VIEW_MODEL = 3,
	RUI_DRAW_COCKPIT = 5,
	RUI_DRAW_HUD = 6,
	RUI_DRAW_POSTEFFECTS = 7,
};

// must match the above enum
static const char* s_RuiDrawGroupNames[] = {
	"NATIVE", // seems to be used for any rui elements created from native/vgui
	"INVALID_1",
	"RUI_DRAW_WORLD",
	"RUI_DRAW_VIEW_MODEL",
	"INVALID_4",
	"RUI_DRAW_COCKPIT",
	"RUI_DRAW_HUD",
	"RUI_DRAW_POSTEFFECTS",
};

struct RuiArg_s
{
	RuiArgumentType_e type;
	char unk1;
	i16 valueOffset;
	i16 nameOffset;
	i16 shortHash;
};

struct RuiArgCluster_s
{
	uint16_t argIndex;
	uint16_t argCount;
	char byte1;
	char byte2;
	i16 unk_6;
	i16 unk_8;
	i16 unk_A;
	char unk_C[4];
	i16 unk_10;
};

struct RuiFuncs_s
{
	void (*SetHidden)(RuiInstance_s* const rui);
	void (*SetNoRender)(RuiInstance_s* const rui);
	void (*SetNoRenderAndAssert)(RuiInstance_s* const rui, const char* const errorMsg);

	float (*GetTransformSize)(RuiInstance_s* const rui);
	void (*GetTextSize)(RuiInstance_s* const rui);

	void* unk1;

	void (*ExecuteTransform)(RuiInstance_s* const rui, const int index);
	void (*ExecuteTransformAndResize)(RuiInstance_s* const rui, const int index);

	const char* (*SNPrintF)(RuiInstance_s* const rui, const char* const fmt, ...);
	const char* (*Localize)(RuiInstance_s* const rui, const char* const key, __int64 a3, __int64 a4, __int64 a5, __int64 a6, __int64 a7);

	const char* (*ToUpper)(RuiInstance_s* const rui, char* const text);
	const char* (*FormatNumber)(RuiInstance_s* const rui, const char* fmt, const float number);

	void* unk2;
	void* unk3;
	void* unk4;
	void* unk5;
	void* unk6;
	void* unk7;

	// Returns -1 on failure.
	u32 (*FindAsset)(RuiInstance_s* const rui, const char* const assetName);

	void* unk8;
	void* unk9;
	void* unk10;
	void* unk11;
	void* unk12;
	void* unk13;

	u32(*StringToHash)(RuiInstance_s* const rui, const char* const assetName);
	void* (*GetActiveColorBlindPaletteColor)(RuiInstance_s* const rui, const int a2, const int a3);
	
	void* unk14;
};

struct RuiGlobalState_s
{

};

struct RuiHeader_s
{
	char* name;
	void* defaultValues;
	char* transformData;
	float elementWidth;
	float elementHeight;
	float elementWidthRatio;
	float elementHeightRatio;
	char* argNames;
	RuiArgCluster_s* argClusters;
	RuiArg_s* args;
	u16 argCount;
	u16 short42;
	u16 ruiDataStructSize;
	u16 defaultValuesSize;
	u16 styleDescriptorCount;
	u16 unk_4A;
	u16 renderJobCount;
	u16 argClusterCount;
	void* styleDescriptors;
	void* renderJobs;
	void* mappingData;
	void (*ruiFunc)(RuiFuncs_s* const api, RuiGlobalState_s* const state, RuiInstance_s* const rui, char* const values);
	void (*ruiHiddenFunc)(RuiFuncs_s* const api, RuiGlobalState_s* const state, RuiInstance_s* const rui, char* const values);
};


struct RuiInstance_s
{
	RuiHeader_s* header;
	float elementWidth;
	float elementHeight;
	float elementWidthRatio;
	float elementHeightRatio;
	void* str_v1;
	__m128 m128_20;
	_BYTE gap_30[8];
	i64 createTimeStamp;
	_QWORD qword_40;
	bool isHidden;
	bool hasError;
	_BYTE gap_4A[14];
	void* pointer_58;
	_BYTE data[1];
};

// do not change! this is used for the size of a static struct in the exe
constexpr int MAX_RUI_SCRIPT_INSTANCES = 2704;

constexpr const char* s_ruiStateNames[] = {
	"UNK_0",
	"ALIVE",
	"FREEING", // RuiDestroy sets this, but then elements end up with "DEAD" later on?
	"DEAD",
};

struct RuiScriptInstance_s // sizeof=0x28
{
	char unk[6];
	u8 drawGroup;
	char unk_8;
	RuiInstance_s* instance; // actual native rui instance
	char unk_10[8];

	uint8_t state;
    char unk_1c[15];
};
static_assert(sizeof(RuiScriptInstance_s) == 40);

struct RuiTracker_s
{
	RuiScriptInstance_s scriptInstances[MAX_RUI_SCRIPT_INSTANCES];
};

/* ==== RUI ====================================================================================================================================================== */
inline bool(*v_Rui_Draw)(__int64* a1, __m128* a2, const __m128i* a3, __int64 a4, __m128* a5);
inline void*(*v_Rui_LoadAsset)(const char* szRuiAssetName);
inline int16_t(*v_Rui_GetFontFace)(void);
inline u32(*v_Rui_CreateClientScriptInstance)(RuiHeader_s* asset, RuiDrawGroup_e drawGroup, u16 hash, i32 a4, i32 a5);
inline int(*v_Script_Rui_Create)(void* sqvm); // i know this is in the wrong place but idc rn
inline void(*v_Rui_Destroy)(u16 handle);

inline bool(*v_Rui_LoadAssetForVGuiPanel)(void* a1, void* panel);
inline void(*v_sub_140937E40)(void* a1, void* panel);

inline RuiFuncs_s* s_ruiApi = nullptr;
inline RuiTracker_s* s_ruiTracker = nullptr;

///////////////////////////////////////////////////////////////////////////////
class V_Rui : public IDetour
{
	virtual void GetAdr(void) const
	{
		LogFunAdr("Rui_Draw", v_Rui_Draw);
		LogFunAdr("Rui_LoadAsset", v_Rui_LoadAsset);
		LogFunAdr("Rui_GetFontFace", v_Rui_GetFontFace);
		LogFunAdr("Rui_CreateClientScriptInstance", v_Rui_CreateClientScriptInstance);
		LogFunAdr("Rui_Destroy", v_Rui_Destroy);
		LogFunAdr("Script_Rui_Create", v_Script_Rui_Create);

		LogVarAdr("s_ruiApi", s_ruiApi);
		LogVarAdr("s_ruiTracker", s_ruiTracker);
	}
	virtual void GetFun(void) const
	{
		Module_FindPattern(g_GameDll, "40 53 48 83 EC 40 4C 8B 5A 18").GetPtr(v_Rui_Draw);
		Module_FindPattern(g_GameDll, "E8 ?? ?? ?? ?? EB 03 49 8B C6 48 89 86 ?? ?? ?? ?? 8B 86 ?? ?? ?? ??").FollowNearCallSelf().GetPtr(v_Rui_LoadAsset);
		Module_FindPattern(g_GameDll, "F7 05 ?? ?? ?? ?? ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 74 05 49 8B D1 EB 19 48 8B 05 ?? ?? ?? ?? 48 8D 15 ?? ?? ?? ?? 48 8B 48 58 48 85 C9 48 0F 45 D1 F7 05 ?? ?? ?? ?? ?? ?? ?? ?? 75 19 48 8B 05 ?? ?? ?? ?? 4C 8D 0D ?? ?? ?? ?? 4C 8B 40 58 4D 85 C0 4D 0F 45 C8 49 8B C9 48 FF 25 ?? ?? ?? ??").GetPtr(v_Rui_GetFontFace);
		Module_FindPattern(g_GameDll, "44 89 4C 24 ?? 53 57").GetPtr(v_Rui_CreateClientScriptInstance);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ? 57 48 83 EC ? 8B F9 48 8D 05").GetPtr(v_Rui_Destroy);
		Module_FindPattern(g_GameDll, "48 89 5C 24 ? 48 89 6C 24 ? 41 56 48 83 EC ? 48 8B 41").GetPtr(v_Script_Rui_Create);

		Module_FindPattern(g_GameDll, "48 89 5C 24 ? 57 48 83 EC ? 8B 41 ? 48 8B FA 48 8B D9 83 F8 ? 74").GetPtr(v_Rui_LoadAssetForVGuiPanel);
		Module_FindPattern(g_GameDll, "40 53 56 57 48 83 EC ? 48 8B 1D ? ? ? ? 48 8B F1 48 8B 02").GetPtr(v_sub_140937E40);
	}
	virtual void GetVar(void) const
	{
		CMemory(v_Rui_Draw).Offset(0x16B).FindPatternSelf("48 8D").ResolveRelativeAddressSelf(3, 7).GetPtr(s_ruiApi);
		CMemory(v_Rui_Destroy).Offset(0x10).FindPatternSelf("48 8D").ResolveRelativeAddressSelf(3, 7).GetPtr(s_ruiTracker); // lea rcx, s_ruiTracker
	}
	virtual void GetCon(void) const { }
	virtual void Detour(const bool bAttach) const;
};
///////////////////////////////////////////////////////////////////////////////

#endif // RTECH_RUI_H
