//=============================================================================//
//
// Purpose: increases weapon blast pattern and projectile limits.
//
//=============================================================================//
#include "core/stdafx.h"
#include "game/shared/r1/blast_pattern.h"

template <size_t BULLET_COUNT>
struct BlastPattern_t
{
	char m_szName[32];
	int m_nBulletCount;
	float m_BulletData[BULLET_COUNT][4];
};

struct MemoryPatch_t
{
	void* m_pAddress;
	size_t m_nSize;
	uint32_t m_nOriginalValue;
};

struct PatchStats_t
{
	int m_nArrayReferences;
	int m_nEntryStrides;
	int m_nProjectileLimits;
};

static constexpr int MAX_BLAST_PATTERNS = 128;
static constexpr int MAX_BLAST_PATTERN_BULLETS = 64;
static constexpr int MAX_PROJECTILES_PER_SHOT = 64;

using OriginalBlastPattern_t = BlastPattern_t<16>;
using ExtendedBlastPattern_t = BlastPattern_t<MAX_BLAST_PATTERN_BULLETS>;
using ExtendedBlastPatternArray_t = ExtendedBlastPattern_t[MAX_BLAST_PATTERNS];

static_assert(sizeof(OriginalBlastPattern_t) == 0x124);
static_assert(sizeof(ExtendedBlastPattern_t) == 0x424);

static CMemory s_BlastPatternLimitCheck;
static uint8_t* s_pBlastPatternArray = nullptr;
static uint8_t* s_pExtendedBlastPatternArray = nullptr;
static uint8_t* s_pPatternCountCompare = nullptr;
static uint32_t* s_pPatternCountError = nullptr;
static uint8_t* s_pBulletCountCompare = nullptr;
static uint32_t* s_pBulletCountError = nullptr;

static vector<MemoryPatch_t> s_MemoryPatches;

template <typename T>
static T ReadUnaligned(const uint8_t* const pAddress)
{
	T value;
	memcpy(&value, pAddress, sizeof(value));
	return value;
}

template <typename T>
static bool ApplyMemoryPatch(T* const pAddress, const T value)
{
	static_assert(sizeof(T) <= sizeof(MemoryPatch_t::m_nOriginalValue), "MemoryPatch_t cannot store the original value");

	if (!pAddress)
		return false;

	DWORD nOldProtection = 0;
	if (!VirtualProtect(pAddress, sizeof(value), PAGE_EXECUTE_READWRITE, &nOldProtection))
	{
		Warning(eDLL_T::ENGINE, "VBlastPattern: VirtualProtect failed with error %u\n", GetLastError());
		return false;
	}

	MemoryPatch_t patch = {};
	patch.m_pAddress = pAddress;
	patch.m_nSize = sizeof(value);
	memcpy(&patch.m_nOriginalValue, pAddress, sizeof(value));
	s_MemoryPatches.emplace_back(patch);

	memcpy(pAddress, &value, sizeof(value));
	FlushInstructionCache(GetCurrentProcess(), pAddress, sizeof(value));

	DWORD nUnusedProtection = 0;
	if (!VirtualProtect(pAddress, sizeof(value), nOldProtection, &nUnusedProtection))
		Warning(eDLL_T::ENGINE, "VBlastPattern: failed to restore memory protection with error %u\n", GetLastError());

	return true;
}

static void RestoreMemoryPatches()
{
	for (auto it = s_MemoryPatches.rbegin(); it != s_MemoryPatches.rend(); ++it)
	{
		const MemoryPatch_t& patch = *it;
		DWORD nOldProtection = 0;

		if (!VirtualProtect(patch.m_pAddress, patch.m_nSize, PAGE_EXECUTE_READWRITE, &nOldProtection))
		{
			Warning(eDLL_T::ENGINE, "VBlastPattern: VirtualProtect failed with error %u\n", GetLastError());
			continue;
		}

		memcpy(patch.m_pAddress, &patch.m_nOriginalValue, patch.m_nSize);
		FlushInstructionCache(GetCurrentProcess(), patch.m_pAddress, patch.m_nSize);

		DWORD nUnusedProtection = 0;
		if (!VirtualProtect(patch.m_pAddress, patch.m_nSize, nOldProtection, &nUnusedProtection))
			Warning(eDLL_T::ENGINE, "VBlastPattern: failed to restore memory protection with error %u\n", GetLastError());
	}

	s_MemoryPatches.clear();
}

static void FreeExtendedBlastPatternArray()
{
	if (!s_pExtendedBlastPatternArray)
		return;

	VirtualFree(s_pExtendedBlastPatternArray, 0, MEM_RELEASE);
	s_pExtendedBlastPatternArray = nullptr;
}

static bool AllocateExtendedBlastPatternArray()
{
	const uintptr_t nModuleEnd = g_GameDll.GetModuleBase() + g_GameDll.GetModuleSize();
	const uintptr_t nFirstAddress = (nModuleEnd + 0x10000 - 1) & ~(0x10000 - 1);
	const uintptr_t nLastAddress = nModuleEnd + 0x40000000;

	for (uintptr_t nAddress = nFirstAddress; nAddress < nLastAddress; nAddress += 0x10000)
	{
		s_pExtendedBlastPatternArray = reinterpret_cast<uint8_t*>(VirtualAlloc(
			reinterpret_cast<void*>(nAddress),
			sizeof(ExtendedBlastPatternArray_t),
			MEM_COMMIT | MEM_RESERVE,
			PAGE_READWRITE));

		if (s_pExtendedBlastPatternArray)
			return true;
	}

	Warning(eDLL_T::ENGINE, "VBlastPattern: failed to allocate the extended blast pattern array\n");
	return false;
}

static bool PatchBlastPatternArrayReferences(const CModule::ModuleSections_t& textSection, PatchStats_t& stats)
{
	uint8_t* const pText = reinterpret_cast<uint8_t*>(textSection.m_pSectionBase);
	const size_t nTextSize = textSection.m_nSectionSize;
	bool bSuccess = true;

	for (size_t i = 0; i + 7 <= nTextSize; ++i)
	{
		const uint8_t nRexPrefix = pText[i];

		// LEA reg, [RIP+disp32]
		if ((nRexPrefix == 0x48 || nRexPrefix == 0x4C) && pText[i + 1] == 0x8D && (pText[i + 2] & 0xC7) == 0x05)
		{
			const int32_t nDisplacement = ReadUnaligned<int32_t>(&pText[i + 3]);
			const uintptr_t nNextInstruction = reinterpret_cast<uintptr_t>(&pText[i + 7]);
			const uintptr_t nTarget = static_cast<uintptr_t>(static_cast<intptr_t>(nNextInstruction) + nDisplacement);

			if (nTarget == reinterpret_cast<uintptr_t>(s_pBlastPatternArray))
			{
				const int64_t nNewDisplacement = reinterpret_cast<intptr_t>(s_pExtendedBlastPatternArray) - static_cast<intptr_t>(nNextInstruction);

				if (nNewDisplacement < INT32_MIN || nNewDisplacement > INT32_MAX)
				{
					Warning(eDLL_T::ENGINE, "VBlastPattern: extended array is outside the relative address range\n");
					bSuccess = false;
				}
				else if (ApplyMemoryPatch(reinterpret_cast<int32_t*>(&pText[i + 3]), static_cast<int32_t>(nNewDisplacement)))
				{
					++stats.m_nArrayReferences;
				}
				else
				{
					bSuccess = false;
				}
			}
		}

		// IMUL reg, reg/mem, imm32
		if ((nRexPrefix == 0x48 || nRexPrefix == 0x4C)
			&& pText[i + 1] == 0x69
			&& ReadUnaligned<uint32_t>(&pText[i + 3]) == sizeof(OriginalBlastPattern_t))
		{
			if (ApplyMemoryPatch(
				reinterpret_cast<uint32_t*>(&pText[i + 3]),
				static_cast<uint32_t>(sizeof(ExtendedBlastPattern_t))))
			{
				++stats.m_nEntryStrides;
			}
			else
			{
				bSuccess = false;
			}
		}
	}

	return bSuccess;
}

static bool PatchProjectileLimits(const CModule::ModuleSections_t& textSection, PatchStats_t& stats)
{
	uint8_t* const pText = reinterpret_cast<uint8_t*>(textSection.m_pSectionBase);
	const size_t nTextSize = textSection.m_nSectionSize;
	bool bSuccess = true;

	for (size_t i = 0; i + 19 <= nTextSize; ++i)
	{
		const bool bLimitCheck =
			pText[i] == 0x45 && pText[i + 1] == 0x8B && pText[i + 2] == 0x86
			&& pText[i + 3] == 0xD0 && pText[i + 4] == 0x02
			&& pText[i + 5] == 0x00 && pText[i + 6] == 0x00
			&& pText[i + 7] == 0x41 && pText[i + 8] == 0x83 && pText[i + 9] == 0xF8
			&& pText[i + 10] == 12
			&& pText[i + 13] == 0x41 && pText[i + 14] == 0xB9
			&& ReadUnaligned<uint32_t>(&pText[i + 15]) == 12;

		if (!bLimitCheck)
			continue;

		const bool bComparePatched = ApplyMemoryPatch(
			&pText[i + 10], static_cast<uint8_t>(MAX_PROJECTILES_PER_SHOT));
		const bool bErrorPatched = ApplyMemoryPatch(
			reinterpret_cast<uint32_t*>(&pText[i + 15]),
			static_cast<uint32_t>(MAX_PROJECTILES_PER_SHOT));

		if (bComparePatched && bErrorPatched)
			++stats.m_nProjectileLimits;
		else
			bSuccess = false;
	}

	return bSuccess;
}
//-----------------------------------------------------------------------------
void VBlastPattern::GetAdr(void) const
{
	LogFunAdr("LoadWeaponBlastPatterns_Client (limit check)", s_BlastPatternLimitCheck.RCast<void*>());
	LogVarAdr("g_blastPatterns", s_pBlastPatternArray);
}

//-----------------------------------------------------------------------------
void VBlastPattern::GetFun(void) const
{
	// The function prologue is shared with LoadWeaponViewkickPatterns_Client.
	// Anchor on the unique count check and entry stride instead.
	s_BlastPatternLimitCheck = Module_FindPattern(g_GameDll, "83 F9 07 0F 84 ?? ?? ?? ?? 4C 69 F9 24 01 00 00");
}

//-----------------------------------------------------------------------------
void VBlastPattern::GetVar(void) const
{
	if (!s_BlastPatternLimitCheck)
		return;

	s_pPatternCountCompare = s_BlastPatternLimitCheck.Offset(0x2).RCast<uint8_t*>();
	s_pBlastPatternArray = s_BlastPatternLimitCheck.Offset(-0x7).ResolveRelativeAddress(0x3, 0x7).RCast<uint8_t*>();

	const CMemory patternCountError = s_BlastPatternLimitCheck.FindPattern("41 B8 08 00 00 00 48 8D 15", CMemory::Direction::DOWN, 512);
	const CMemory bulletCountCompare = s_BlastPatternLimitCheck.FindPattern("48 83 FD 10", CMemory::Direction::DOWN, 512);
	const CMemory bulletCountError = s_BlastPatternLimitCheck.FindPattern("41 B8 10 00 00 00 48 8D 0D", CMemory::Direction::DOWN, 512);

	if (patternCountError)
		s_pPatternCountError = patternCountError.Offset(0x2).RCast<uint32_t*>();
	if (bulletCountCompare)
		s_pBulletCountCompare = bulletCountCompare.Offset(0x3).RCast<uint8_t*>();
	if (bulletCountError)
		s_pBulletCountError = bulletCountError.Offset(0x2).RCast<uint32_t*>();
}

//-----------------------------------------------------------------------------
void VBlastPattern::Detour(const bool bAttach) const
{
	if (!bAttach)
	{
		RestoreMemoryPatches();
		FreeExtendedBlastPatternArray();
		return;
	}

	if (s_pExtendedBlastPatternArray)
		return;

	if (!s_pBlastPatternArray || !s_pPatternCountCompare || !s_pPatternCountError || !s_pBulletCountCompare || !s_pBulletCountError)
	{
		Warning(eDLL_T::ENGINE, "VBlastPattern: failed to resolve one or more patch addresses\n");
		return;
	}

	const CModule::ModuleSections_t& textSection = g_GameDll.GetSectionByName(".text");
	if (!textSection.IsSectionValid() || !AllocateExtendedBlastPatternArray())
		return;

	s_MemoryPatches.clear();
	PatchStats_t stats = {};

	bool bSuccess = PatchBlastPatternArrayReferences(textSection, stats);
	bSuccess &= ApplyMemoryPatch(s_pPatternCountCompare, static_cast<uint8_t>(MAX_BLAST_PATTERNS - 1));
	bSuccess &= ApplyMemoryPatch(s_pPatternCountError, static_cast<uint32_t>(MAX_BLAST_PATTERNS));
	bSuccess &= ApplyMemoryPatch(s_pBulletCountCompare, static_cast<uint8_t>(MAX_BLAST_PATTERN_BULLETS));
	bSuccess &= ApplyMemoryPatch(s_pBulletCountError, static_cast<uint32_t>(MAX_BLAST_PATTERN_BULLETS));
	bSuccess &= PatchProjectileLimits(textSection, stats);

	if (!bSuccess || stats.m_nArrayReferences == 0 || stats.m_nEntryStrides == 0 || stats.m_nProjectileLimits == 0)
	{
		RestoreMemoryPatches();
		FreeExtendedBlastPatternArray();
		Warning(eDLL_T::ENGINE, "VBlastPattern: failed to apply all required patches\n");
		return;
	}
}
