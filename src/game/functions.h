#pragma once

#include <windows.h>
#include <cstdint> // Required for uint64_t
#include <vector> // Added for std::vector

// Function pointer types and extern declarations will go here

// CastLocalPlayerSpell Function
typedef char (__cdecl* CastLocalPlayerSpellFn)(int spellId, int unknownIntArg1, uint64_t targetGuid, char unknownCharArg);
extern CastLocalPlayerSpellFn CastLocalPlayerSpell;
const DWORD OFF_CastLocalPlayerSpell = 0x0080DA40; // Address from disassembly

// GetLocalPlayerGuid Function
typedef uint64_t(__cdecl* GetLocalPlayerGuidFn)();
extern GetLocalPlayerGuidFn GetLocalPlayerGuid;
const DWORD OFF_GetLocalPlayerGuid = 0x004D3790;

// ApplySpellEffectsIfReady Offset (Used as callback argument)
const DWORD OFF_ApplySpellEffects = 0x0080ABE0;

// We should have an initialization function declaration
void InitializeFunctions();

void Initialize(uintptr_t clientConnectionAddr, uintptr_t funcAddr);
void ReadSpellbook();
const std::vector<int>& GetSpellbook();

// Add other non-cooldown related function declarations here if any