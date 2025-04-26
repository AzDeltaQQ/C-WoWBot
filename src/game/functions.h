#pragma once

#include <windows.h>
#include <cstdint> // Required for uint64_t
#include <vector> // Added for std::vector

// Function pointer types and extern declarations will go here

// ClickToMove (CTM) - Higher Level Handlers
// Signature defined in MovementController.h (HandleTerrainClickFn)
const DWORD OFF_HandleTerrainClick = 0x00727400; // Address for CORE CTM function (handlePlayerClickToMove)

// CastLocalPlayerSpell Function
// Reverting to 4-argument signature based on disassembly
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

// Function to select a target by GUID (Placeholder address: 0x520190)
// Actual parameters and return type might differ - this is based on limited context.
// The disassembly suggests it might *do more* than just select.
// Research or reverse engineer the exact call needed for CLickToMove_SetTarget or similar.
// UPDATE: Based on disassembly, 0x00524BF0 (handleTargetAcquisition) seems more likely.
// UPDATE 2: Disassembly hints at __usercall (int@<edi>, __int64), trying __stdcall first.
// UPDATE 3: Trying __thiscall based on user request.
// UPDATE 4: Reverting to __cdecl for logging test.
typedef void(__cdecl* TargetUnitByGuidFn)(uint64_t guid);
const TargetUnitByGuidFn TargetUnitByGuid = (TargetUnitByGuidFn)0x00524BF0; // Updated Address

// Alternative based on enumVisibleObjects:
// typedef int (__cdecl *EnumVisibleObjectsFn)(int (__cdecl *callback)(uint64_t, void*), void* filter);
// const EnumVisibleObjectsFn EnumVisibleObjects = (EnumVisibleObjectsFn)0x005203EE;
// Need to define the callback function select_nearest_suitable_target separately if using this.