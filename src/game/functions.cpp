#include "functions.h"
#include <windows.h> // Keep for potential future use?
#include "log.h" // Include for logging
#include "../utils/memory.h" // Include for MemoryReader

// Define the function pointers
CastLocalPlayerSpellFn CastLocalPlayerSpell = nullptr;
GetLocalPlayerGuidFn GetLocalPlayerGuid = nullptr;

// Function to initialize all function pointers
void InitializeFunctions() {
    LogMessage("InitializeFunctions: Using provided offsets as ABSOLUTE addresses.");

    CastLocalPlayerSpell = (CastLocalPlayerSpellFn)OFF_CastLocalPlayerSpell;
    GetLocalPlayerGuid = (GetLocalPlayerGuidFn)OFF_GetLocalPlayerGuid;

    LogStream ss;
    ss << "InitializeFunctions Pointers (Absolute Addresses):" 
       << "\n  CastLocalPlayerSpell: 0x" << std::hex << (uintptr_t)CastLocalPlayerSpell
       << "\n  GetLocalPlayerGuid: 0x" << std::hex << (uintptr_t)GetLocalPlayerGuid;
    LogMessage(ss.str());

    if (!CastLocalPlayerSpell || !GetLocalPlayerGuid) {
        LogMessage("InitializeFunctions Error: One or more function pointers are NULL after assignment!");
        // Handle error appropriately, e.g., return false or throw an exception
    } else {
        LogMessage("InitializeFunctions complete.");
    }
} 