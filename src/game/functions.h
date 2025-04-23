#pragma once

#include <windows.h>
#include <cstdint> // Required for uint64_t

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

// We should have an initialization function declaration
void InitializeFunctions(); 