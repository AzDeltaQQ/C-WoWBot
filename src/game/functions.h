#pragma once

#include <windows.h>
#include <cstdint> // Required for uint64_t
#include <vector> // Added for std::vector

// Function pointer types and extern declarations will go here

// Example:
// typedef void (*SomeFunctionType)(int arg1, bool arg2);
// extern SomeFunctionType SomeFunction;

// Function Offsets (relative to base address)
// Example:
// const DWORD OFF_SomeFunction = 0x12345;

// CastLocalPlayerSpell Function
typedef char (__cdecl* CastLocalPlayerSpellFn)(int spellId, int unknownIntArg1, uint64_t targetGuid, char unknownCharArg);
extern CastLocalPlayerSpellFn CastLocalPlayerSpell;
const DWORD OFF_CastLocalPlayerSpell = 0x0080DA40; // Address from disassembly

// GetLocalPlayerGuid Function
typedef uint64_t (__cdecl* GetLocalPlayerGuidFn)(); // Assuming cdecl, returns uint64 in edx:eax
extern GetLocalPlayerGuidFn GetLocalPlayerGuid;
const DWORD OFF_GetLocalPlayerGuid = 0x004D3790;

// RetrieveInfoBlock Function
// Returns a pointer to an info structure.
struct PlayerGuidStruct { uint32_t low; uint32_t high; }; // Helper
// Special "__thiscall" (using ECX for 'this' with stack args)
typedef void* (__thiscall* RetrieveInfoBlockFn)(
    void* cachePtr,         // 'this' pointer in ECX
    int spellId,            // Stack arg 2
    PlayerGuidStruct* guidPtr, // Stack arg 3
    void* callbackPtr,      // Stack arg 4
    int unknown1,           // Stack arg 5
    int unknown2            // Stack arg 6
);
extern RetrieveInfoBlockFn RetrieveInfoBlock;
const DWORD OFF_RetrieveInfoBlock = 0x0067CA30;

// ApplySpellEffectsIfReady Offset (Used as callback argument)
const DWORD OFF_ApplySpellEffects = 0x0080ABE0;

// We should have an initialization function declaration
void InitializeFunctions();

void Initialize(uintptr_t clientConnectionAddr, uintptr_t funcAddr);
void ReadSpellbook();
const std::vector<int>& GetSpellbook();

// Add other non-cooldown related function declarations here if any