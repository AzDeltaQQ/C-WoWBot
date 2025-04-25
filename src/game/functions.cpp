#include "functions.h"
#include <windows.h> // Keep for potential future use?
#include "log.h" // Include for logging
#include "../utils/memory.h" // Include for MemoryReader

// Define the actual function pointer
CastLocalPlayerSpellFn CastLocalPlayerSpell = nullptr;
GetLocalPlayerGuidFn GetLocalPlayerGuid = nullptr;
RetrieveInfoBlockFn RetrieveInfoBlock = nullptr;

// Function to initialize all function pointers
void InitializeFunctions() {
    LogMessage("InitializeFunctions: Using provided offsets as ABSOLUTE addresses.");

    // Initialize existing functions using OFF_ constants directly as absolute addresses
    CastLocalPlayerSpell = (CastLocalPlayerSpellFn)OFF_CastLocalPlayerSpell; 
    GetLocalPlayerGuid = (GetLocalPlayerGuidFn)OFF_GetLocalPlayerGuid; 
    RetrieveInfoBlock = (RetrieveInfoBlockFn)OFF_RetrieveInfoBlock; 

    // Initialize new cooldown functions using OFF_ constants directly as absolute addresses
    // get_spell_cooldown_with_retries = (GetSpellCooldownWithRetriesFn)OFF_GetSpellCooldownWithRetries;
    // checkPlayerSpellCooldown = (CheckPlayerSpellCooldownFn)OFF_CheckPlayerSpellCooldown;

    // Log function pointer addresses (and check for null)
    LogStream ssFuncs;
    ssFuncs << "InitializeFunctions Pointers (Absolute Addresses):\n"
            << "  CastLocalPlayerSpell: 0x" << std::hex << (uintptr_t)CastLocalPlayerSpell << "\n"
            << "  GetLocalPlayerGuid: 0x" << std::hex << (uintptr_t)GetLocalPlayerGuid << "\n"
            << "  RetrieveInfoBlock: 0x" << std::hex << (uintptr_t)RetrieveInfoBlock;
    // << "  get_spell_cooldown_with_retries: 0x" << std::hex << (uintptr_t)get_spell_cooldown_with_retries << "\n"
    // << "  checkPlayerSpellCooldown: 0x" << std::hex << (uintptr_t)checkPlayerSpellCooldown;
    LogMessage(ssFuncs.str());

    // Optional: Add null checks here if desired, though later calls will check
    if (!CastLocalPlayerSpell || !GetLocalPlayerGuid || !RetrieveInfoBlock) {
         LogMessage("InitializeFunctions Warning: One or more function pointers failed to initialize!");
    }

    LogMessage("InitializeFunctions complete.");
} 