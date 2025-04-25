#include "spellmanager.h"
#include "functions.h" // Required for the actual CastLocalPlayerSpell function pointer
#include "log.h"
#include <stdexcept> // For std::runtime_error
#include <vector>    // Added for std::vector
#include <cstdint>   // Added for uint32_t
#include <iomanip>   // Added for std::setw and std::setfill
#include <stdio.h>   // For snprintf
#include <windows.h>
#include <cstdio> // For printf/logging
#include <cstring>      // For memcpy

// Include the actual memory reader utility
#include "../utils/memory.h" 

namespace { // Anonymous namespace for constants
    constexpr uintptr_t SpellCountAddr = 0x00BE8D9C;
    constexpr uintptr_t SpellBookAddr = 0x00BE5D88;
    constexpr size_t MaxSpellbookSize = 1023; // Based on disassembly comment
} // namespace

// Initialize singleton instance pointer
SpellManager* SpellManager::m_instance = nullptr;

// Private constructor
SpellManager::SpellManager() {
    // Initialization logic for SpellManager, if any, goes here.
    // Currently, it doesn't need much, relies on Functions::InitializeFunctions being called elsewhere.
}

// Get singleton instance
SpellManager& SpellManager::GetInstance() {
    if (m_instance == nullptr) {
        m_instance = new SpellManager();
    }
    return *m_instance;
}

// CastSpell implementation
bool SpellManager::CastSpell(int spellId, uint64_t targetGuid, int unknownIntArg1, char unknownCharArg) {
    // Ensure the function pointer is initialized
    if (::CastLocalPlayerSpell == nullptr) {
        LogMessage("SpellManager::CastSpell Error: CastLocalPlayerSpell function pointer is not initialized!");
        LogMessage("Ensure InitializeFunctions() has been called successfully.");
        return false;
    }

    LogStream ss;
    ss << "SpellManager::CastSpell: Attempting to cast SpellID=" << spellId 
       << " on TargetGUID=0x" << std::hex << targetGuid 
       << " (Args: " << unknownIntArg1 << ", '" << unknownCharArg << "')";
    LogMessage(ss.str());

    try {
        // Call the actual game function
        // The return value is 'char' according to the disassembly, potentially indicating success/failure or some state.
        char result = ::CastLocalPlayerSpell(spellId, unknownIntArg1, targetGuid, unknownCharArg);
        
        LogStream ssResult;
        ssResult << "SpellManager::CastSpell: CastLocalPlayerSpell returned '" << result << "' (char code: " << static_cast<int>(result) << ")";
        LogMessage(ssResult.str());
        
        // We might need to interpret the result code based on observation
        // For now, let's assume non-zero might mean success or queued, zero might mean failure.
        // This is a guess and might need refinement.
        return result != 0; 
    } catch (const std::exception& e) {
        LogStream ssErr;
        ssErr << "SpellManager::CastSpell: Exception caught while calling CastLocalPlayerSpell - " << e.what();
        LogMessage(ssErr.str());
        return false;
    } catch (...) {
        LogMessage("SpellManager::CastSpell: Unknown exception caught while calling CastLocalPlayerSpell.");
        return false;
    }
}

// ReadSpellbook implementation
std::vector<uint32_t> SpellManager::ReadSpellbook() {
    LogMessage("SpellManager::ReadSpellbook: Attempting to read spellbook.");
    std::vector<uint32_t> spell_ids;

    try {
        // 1. Read SpellCount
        uint32_t spellCount = MemoryReader::Read<uint32_t>(SpellCountAddr);
        
        LogStream ssCount;
        ssCount << "SpellManager::ReadSpellbook: Read SpellCount = " << spellCount;
        LogMessage(ssCount.str());

        // 2. Validate SpellCount
        if (spellCount == 0) {
            LogMessage("SpellManager::ReadSpellbook: SpellCount is 0, returning empty spellbook.");
            return spell_ids; // Return empty vector
        }
        if (spellCount > MaxSpellbookSize) {
            LogStream ssWarn;
            ssWarn << "SpellManager::ReadSpellbook: Warning - SpellCount (" << spellCount 
                   << ") exceeds MaxSpellbookSize (" << MaxSpellbookSize 
                   << "). Clamping to max size.";
            LogMessage(ssWarn.str());
            spellCount = MaxSpellbookSize; // Clamp to avoid excessive reading
        }

        // 3. Read SpellBook array
        spell_ids.reserve(spellCount); // Reserve space for efficiency
        for (size_t i = 0; i < spellCount; ++i) {
            uintptr_t currentSpellAddr = SpellBookAddr + (i * sizeof(uint32_t));
            uint32_t spellId = MemoryReader::Read<uint32_t>(currentSpellAddr);
             if (spellId != 0) { // Often spellbooks have trailing zeros
                 spell_ids.push_back(spellId);
            }
        }

        LogStream ssDone;
        ssDone << "SpellManager::ReadSpellbook: Successfully read " << spell_ids.size() << " non-zero spell IDs.";
        LogMessage(ssDone.str());

    } catch (const std::runtime_error& e) {
        // Catch potential errors from MemoryReader::Read
        LogStream ssErr;
        ssErr << "SpellManager::ReadSpellbook: Runtime error reading spellbook memory - " << e.what();
        LogMessage(ssErr.str());
        spell_ids.clear(); // Clear potentially partial results on error
    } catch (...) {
        LogMessage("SpellManager::ReadSpellbook: Unknown exception caught while reading spellbook memory.");
        spell_ids.clear(); // Clear potentially partial results on error
    }

    return spell_ids;
}

// GetSpellCooldownTimes implementation REMOVED
// bool SpellManager::GetSpellCooldownTimes(...) { ... function body removed ... } // REMOVED

// IsSpellUsable implementation REMOVED
// bool SpellManager::IsSpellUsable(...) { ... function body removed ... } // REMOVED

// Define the function pointer type for get_spell_cooldown_proxy (0x809000)
// Ensure DWORD is defined appropriately (e.g., via <windows.h> or manually)
#ifndef DWORD
typedef unsigned long DWORD;
#endif
typedef bool(__cdecl* GetSpellCooldownProxyFn)(int spellId, int playerOrPetFlag, int* ptr_remainingDuration, int* ptr_startTime, DWORD* ptr_isActive);

// The address of the target function in the game's memory space
const uintptr_t GET_SPELL_COOLDOWN_PROXY_ADDR = 0x809000;

// --- Cooldown Patch Logic --- 

// Structure to hold patch information (optional but good practice)
struct MemoryPatch {
    uintptr_t address;
    std::vector<unsigned char> originalBytes;
    std::vector<unsigned char> patchBytes;
};

// Store patches to potentially revert them later if needed
// static std::vector<MemoryPatch> appliedPatches; // If reversion needed

// Helper to apply a single patch
bool ApplyPatch(uintptr_t address, const std::vector<unsigned char>& patchBytes) {
    size_t patchSize = patchBytes.size();
    // std::vector<unsigned char> originalBytes(patchSize); // For reverting
    
    DWORD oldProtect;
    // 1. Change memory protection
    if (!VirtualProtect((LPVOID)address, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogStream ssErr;
        ssErr << "Error: Failed to change memory protection for patch at 0x" << std::hex << address << ". Error code: " << GetLastError();
        LogMessage(ssErr.str());
        return false;
    }

    // 2. Read original bytes (optional, if reverting needed)
    // memcpy(originalBytes.data(), (void*)address, patchSize);

    // 3. Write the patch
    memcpy((void*)address, patchBytes.data(), patchSize);

    // 4. Restore original protection
    DWORD temp;
    VirtualProtect((LPVOID)address, patchSize, oldProtect, &temp);

    // Store patch info for potential reverting
    // appliedPatches.push_back({address, originalBytes, patchBytes});

    LogStream ssSuccess;
    ssSuccess << "Successfully applied " << patchSize << "-byte patch at 0x" << std::hex << address;
    LogMessage(ssSuccess.str());
    return true;
}

// Definition for PatchCooldownBug_Final
void SpellManager::PatchCooldownBug_Final() {
    LogMessage("Applying final cooldown display patches...");
    bool success = true;

    // --- GCD Block Patches (Addresses: 0x807BD4, 0x807BD7, 0x807BDB) ---
    LogMessage("Applying GCD block patches...");
    // Patch 1: 0x807BD4 (Change mov edx,[ebp+10h] to mov eax,[ebp+10h])
    success &= ApplyPatch(0x807BD4, {0x8B, 0x45, 0x10}); 

    // Patch 2: 0x807BD7 (Change test edx,edx to test eax,eax)
    success &= ApplyPatch(0x807BD7, {0x85, 0xC0});

    // Patch 3: 0x807BDB (Change mov [edx],eax to mov [eax],edx)
    success &= ApplyPatch(0x807BDB, {0x89, 0x10});

    // --- Category Block Patches (Addresses: 0x807B84, 0x807B87, 0x807B8B) ---
    LogMessage("Applying Category block patches...");
    // Patch 4: 0x807B84 (Change mov edx,[ebp+10h] to mov eax,[ebp+10h])
    success &= ApplyPatch(0x807B84, {0x8B, 0x45, 0x10});

    // Patch 5: 0x807B87 (Change test edx,edx to test eax,eax)
    success &= ApplyPatch(0x807B87, {0x85, 0xC0});

    // Patch 6: 0x807B8B (Change mov [edx],eax to mov [eax],edx)
    success &= ApplyPatch(0x807B8B, {0x89, 0x10});

    if (success) {
        LogMessage("All cooldown display patches applied successfully.");
    } else {
        LogMessage("Error: One or more cooldown display patches failed. Check logs.");
        // Consider reverting applied patches if one fails?
    }
}

// --- End Cooldown Patch Logic ---

namespace { // Anonymous namespace for helper

int GetSpellCooldownInternal(int spellId, int playerOrPetFlag) {
    GetSpellCooldownProxyFn get_spell_cooldown_proxy = reinterpret_cast<GetSpellCooldownProxyFn>(GET_SPELL_COOLDOWN_PROXY_ADDR);

    int remainingDuration = 0;
    int startTime = 0;
    DWORD isActive = 0; // Using DWORD as per analysis

    try {
        // Important: Ensure the pointers passed are valid memory locations
        bool isOnCooldown = get_spell_cooldown_proxy(
            spellId,
            playerOrPetFlag,
            &remainingDuration,
            &startTime,
            &isActive);

        if (isOnCooldown) {
            // Ensure duration is non-negative
            return (remainingDuration > 0) ? remainingDuration : 0;
        } else {
            return 0; // Spell is ready
        }
    } catch (...) {
        // TODO: Replace with proper logging using LogMessage/LogStream if available
        //printf("SpellManager: Exception calling get_spell_cooldown_proxy at address 0x%p\n", (void*)GET_SPELL_COOLDOWN_PROXY_ADDR);
        LogStream ssErr;
        ssErr << "SpellManager: Exception calling get_spell_cooldown_proxy at address 0x" 
              << std::hex << GET_SPELL_COOLDOWN_PROXY_ADDR;
        LogMessage(ssErr.str());
        return -1; // Indicate an error
    }
}

} // Anonymous namespace

int SpellManager::GetSpellCooldownMs(int spellId) {
    return GetSpellCooldownInternal(spellId, 0); // 0 for player
}

int SpellManager::GetPetSpellCooldownMs(int spellId) {
    return GetSpellCooldownInternal(spellId, 1); // 1 for pet
}