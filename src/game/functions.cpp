#include "functions.h"
#include <windows.h> // Keep for potential future use?
#include "log.h" // Include for logging
#include "../utils/memory.h" // Include for MemoryReader

// Define the function pointers
CastLocalPlayerSpellFn CastLocalPlayerSpell = nullptr;
GetLocalPlayerGuidFn GetLocalPlayerGuid = nullptr;
// RetrieveInfoBlockFn RetrieveInfoBlock = nullptr; // REMOVED

// Function to initialize all function pointers
void InitializeFunctions() {
    // Assuming GameBaseAddr is 0 and these are absolute offsets for now
    // Adjust if you have a dynamic base address calculation
    LogMessage("InitializeFunctions: Using provided offsets as ABSOLUTE addresses.");

    CastLocalPlayerSpell = (CastLocalPlayerSpellFn)OFF_CastLocalPlayerSpell;
    GetLocalPlayerGuid = (GetLocalPlayerGuidFn)OFF_GetLocalPlayerGuid;
    // RetrieveInfoBlock = (RetrieveInfoBlockFn)OFF_RetrieveInfoBlock; // REMOVED

    // Initialize new cooldown functions using OFF_ constants directly as absolute addresses
    // get_spell_cooldown_with_retries = (GetSpellCooldownWithRetriesFn)OFF_GetSpellCooldownWithRetries;
    // checkPlayerSpellCooldown = (CheckPlayerSpellCooldownFn)OFF_CheckPlayerSpellCooldown;

    // Log the initialized function pointers (optional but good for debugging)
    LogStream ss;
    ss << "InitializeFunctions Pointers (Absolute Addresses):" 
       << "\n  CastLocalPlayerSpell: 0x" << std::hex << (uintptr_t)CastLocalPlayerSpell
       << "\n  GetLocalPlayerGuid: 0x" << std::hex << (uintptr_t)GetLocalPlayerGuid;
       // << "\n  RetrieveInfoBlock: 0x" << std::hex << (uintptr_t)RetrieveInfoBlock; // REMOVED
    LogMessage(ss.str());

    // Check if any pointers are null after assignment
    if (!CastLocalPlayerSpell || !GetLocalPlayerGuid /* || !RetrieveInfoBlock */) { // REMOVED check
        LogMessage("InitializeFunctions Error: One or more function pointers are NULL after assignment!");
        // Handle error appropriately, e.g., return false or throw an exception
    } else {
        LogMessage("InitializeFunctions complete.");
    }
} 