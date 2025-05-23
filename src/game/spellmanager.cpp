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
#include <string>      // For std::string
#include "ObjectManager.h" // Includes wowobject.h definitions indirectly
#include "wowobject.h" // Explicitly include WowObject definitions
#include <cmath> // For sqrt

// Include the actual memory reader utility
#include "../utils/memory.h" 

// --- REMOVED: Define function pointer types --- 
// -------------------------------------------

namespace { // Anonymous namespace for constants and helpers
    constexpr uintptr_t SpellCountAddr = 0x00BE8D9C;
    constexpr uintptr_t SpellBookAddr = 0x00BE5D88;
    constexpr size_t MaxSpellbookSize = 1023; // Based on disassembly comment

    // --- REMOVED: Addresses related to DBC access --- 

    // --- Spell Info Constants ---
    constexpr uintptr_t ADDR_SpellDBContextPtr = 0x00AD49D0;
    constexpr uintptr_t ADDR_CompressionFlag   = 0x00C5DEA0;
    constexpr size_t    SPELL_RECORD_SIZE      = 0x2A8; // 680 bytes

    // Offsets within SpellDBContext structure (pointed to by ADDR_SpellDBContextPtr)
    constexpr uint32_t OFFSET_CONTEXT_MAX_ID = 0x0C;
    constexpr uint32_t OFFSET_CONTEXT_MIN_ID = 0x10;
    constexpr uint32_t OFFSET_CONTEXT_INDEX_TABLE_PTR = 0x20; // Points to array of pointers to spell records

    // Offsets within the Spell Record (Relative to RecordPtr) - FINAL-FINAL VERIFIED
    constexpr uint32_t OFFSET_DBC_NAME_PTR = 0x220;        // Field 137 (SpellName[0]) - Direct Pointer
    // constexpr uint32_t OFFSET_DBC_RANK_PTR = 0x264;     // Field 154 (SpellSubtext[0]) - Probably Direct Pointer
    constexpr uint32_t OFFSET_DBC_DESC_PTR = 0x228;        // Field ??? (~171?) - Direct Pointer to Description string (with format codes)
    constexpr uint32_t OFFSET_DBC_TOOLTIP_PTR = 0x22C;     // Field ??? (~188?) - Direct Pointer to Tooltip string (with format codes, often NULL/empty)
    constexpr uint32_t OFFSET_DBC_ICON_ID = 0x218;        // Field 134 (SpellIconID)
    constexpr uint32_t OFFSET_DBC_POWER_TYPE = 0xA4;         // Field 42 (powerType)
    // Add other offsets from DBC as needed...

    // REMOVED: Base address for the spell string table - Not needed for direct pointers
    // constexpr uintptr_t ADDR_SpellStringTableBase = 0x13C155D4; 

    // ---------------------------

    // --- Memory Reading Helpers ---

    // Basic memory validity check (use with caution, IsBadReadPtr has limitations)
    // A more robust solution might involve VirtualQuery or SEH
    bool IsValidReadPtrHelper(const void* ptr, size_t size = 1) {
        if (ptr == nullptr) return false;
        // IsBadReadPtr is generally discouraged, but simple for in-process checks
        // It returns TRUE if the pointer is BAD, so we negate it.
        // Requires <windows.h>
        return !IsBadReadPtr(ptr, size);
    }

    // Reads a null-terminated string from a potentially unsafe pointer
    // Stops after maxLen characters to prevent reading too far on unterminated strings
    std::string SafeReadStringHelper(const char* ptrToString, size_t maxLen = 256) {
        if (!IsValidReadPtrHelper(ptrToString)) {
            return "[Invalid Str Ptr]";
        }

        std::vector<char> buffer;
        buffer.reserve(maxLen + 1); // Reserve space

        try {
            for (size_t i = 0; i < maxLen; ++i) {
                // Check validity of pointer for the current character
                 if (!IsValidReadPtrHelper(ptrToString + i)) {
                     // Hit invalid memory before null terminator or maxLen
                     buffer.push_back('\0'); // Null-terminate what we have
                     LogMessage("SafeReadStringHelper: Hit invalid memory reading string."); // Optional log
                     break;
                 }

                // Read character using MemoryReader (catches exceptions)
                char current_char = MemoryReader::Read<char>(reinterpret_cast<uintptr_t>(ptrToString + i));
                if (current_char == '\0') {
                    break; // End of string
                }
                buffer.push_back(current_char);
            }
            // Ensure null termination if loop finished without finding null char (maxLen reached)
            buffer.push_back('\0');
            return std::string(buffer.data());

        } catch (const std::runtime_error& e) {
            LogStream ssErr;
            ssErr << "SafeReadStringHelper: Exception reading string - " << e.what();
            LogMessage(ssErr.str());
            return "[String Read Error]";
        } catch (...) {
            LogMessage("SafeReadStringHelper: Unknown exception reading string.");
            return "[String Read Unknown Error]";
        }
    }
    // --- End Memory Reading Helpers ---

    // --- REMOVED: ReadFromBuffer / ReadFromRawPtr helpers --- 

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

// CastSpell implementation - Reverted to 4-arg signature
bool SpellManager::CastSpell(int spellId, uint64_t targetGuid, int unknownIntArg1, char unknownCharArg) {
    // Ensure the function pointer is initialized
    if (::CastLocalPlayerSpell == nullptr) {
        LogMessage("SpellManager::CastSpell Error: CastLocalPlayerSpell function pointer is not initialized!");
        LogMessage("Ensure InitializeFunctions() has been called successfully.");
        return false;
    }

    LogStream ss;
    // Log with all 4 arguments
    ss << "SpellManager::CastSpell: Attempting to cast SpellID=" << spellId 
       << " on TargetGUID=0x" << std::hex << targetGuid 
       << " (Args: " << unknownIntArg1 << ", " << static_cast<int>(unknownCharArg) << ")"; // Log char as int
    LogMessage(ss.str());

    try {
        // Call the actual game function with 4 arguments
        char result = ::CastLocalPlayerSpell(spellId, unknownIntArg1, targetGuid, unknownCharArg);
        
        LogStream ssResult;
        // Log the result char and its code
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

// --- CORRECTED GetSpellNameByID Implementation --- (Replacing with user-provided code)
std::string SpellManager::GetSpellNameByID(uint32_t spellId) {
    try {
        // --- Steps 1-4: Get RecordPtr (Simplified) ---
        constexpr uintptr_t ADDR_MinID = ADDR_SpellDBContextPtr + 0x10;
        constexpr uintptr_t ADDR_MaxID = ADDR_SpellDBContextPtr + 0x0C;
        constexpr uintptr_t ADDR_IndexOfTablePtrValue = ADDR_SpellDBContextPtr + 0x20;

        uint32_t minID = MemoryReader::Read<uint32_t>(ADDR_MinID);
        uint32_t maxID = MemoryReader::Read<uint32_t>(ADDR_MaxID);
        if (spellId < minID || spellId > maxID) return ""; // Handle invalid spell ID early

        uintptr_t indexTablePtr = MemoryReader::Read<uintptr_t>(ADDR_IndexOfTablePtrValue);
        if (indexTablePtr == 0) return "";

        uint32_t index = spellId - minID;
        uintptr_t pRecordPtrAddr = indexTablePtr + (index * sizeof(uintptr_t));
        if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(pRecordPtrAddr))) return "";
        uintptr_t recordPtr = MemoryReader::Read<uintptr_t>(pRecordPtrAddr);
        if (recordPtr == 0 || !IsValidReadPtrHelper(reinterpret_cast<const void*>(recordPtr))) return "";

        uint8_t isCompressed = MemoryReader::Read<uint8_t>(ADDR_CompressionFlag);
        if (isCompressed) return "[Compressed Record]"; // Handle compressed records
        // --- End Steps 1-4 ---

        // 5. Get Name Pointer Address
        uintptr_t namePtrAddr = recordPtr + OFFSET_DBC_NAME_PTR; // Use 0x220
        if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(namePtrAddr), sizeof(char*))) return "";

        // 6. Read Name Pointer Value
        char* pszName = MemoryReader::Read<char*>(namePtrAddr);

        // 7. Read String (Helper checks pointer validity)
        std::string result = SafeReadStringHelper(pszName);
        return result;

    } catch (...) { /* Log error maybe? */ return "[Name Read Error]"; }
}

// --- GetSpellDescriptionByID Implementation (Direct Pointer Logic) ---
std::string SpellManager::GetSpellDescriptionByID(uint32_t spellId) {
    try {
        // --- Steps 1-4: Get RecordPtr (Simplified) ---
        constexpr uintptr_t ADDR_MinID = ADDR_SpellDBContextPtr + 0x10;
        constexpr uintptr_t ADDR_MaxID = ADDR_SpellDBContextPtr + 0x0C;
        constexpr uintptr_t ADDR_IndexOfTablePtrValue = ADDR_SpellDBContextPtr + 0x20;

        uint32_t minID = MemoryReader::Read<uint32_t>(ADDR_MinID);
        uint32_t maxID = MemoryReader::Read<uint32_t>(ADDR_MaxID);
        if (spellId < minID || spellId > maxID) return ""; 

        uintptr_t indexTablePtr = MemoryReader::Read<uintptr_t>(ADDR_IndexOfTablePtrValue);
        if (indexTablePtr == 0) return "";

        uint32_t index = spellId - minID;
        uintptr_t pRecordPtrAddr = indexTablePtr + (index * sizeof(uintptr_t));
        if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(pRecordPtrAddr))) return "";
        uintptr_t recordPtr = MemoryReader::Read<uintptr_t>(pRecordPtrAddr);
        if (recordPtr == 0 || !IsValidReadPtrHelper(reinterpret_cast<const void*>(recordPtr))) return "";

        uint8_t isCompressed = MemoryReader::Read<uint8_t>(ADDR_CompressionFlag);
        if (isCompressed) return "[Compressed Record]";
        // --- End Steps 1-4 ---

        // 5. Get Description Pointer Address
        uintptr_t descPtrAddr = recordPtr + OFFSET_DBC_DESC_PTR; // Use 0x228
        if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(descPtrAddr), sizeof(char*))) return "";

        // 6. Read Description Pointer Value
        char* pszDesc = MemoryReader::Read<char*>(descPtrAddr);

        // 7. Read String (Helper checks pointer validity)
        return SafeReadStringHelper(pszDesc);

    } catch (...) { /* Log error maybe? */ return "[Description Read Error]"; }
}

// --- GetSpellTooltipByID Implementation (Direct Pointer Logic) ---
std::string SpellManager::GetSpellTooltipByID(uint32_t spellId) {
    try {
        // --- Steps 1-4: Get RecordPtr (Simplified) ---
        constexpr uintptr_t ADDR_MinID = ADDR_SpellDBContextPtr + 0x10;
        constexpr uintptr_t ADDR_MaxID = ADDR_SpellDBContextPtr + 0x0C;
        constexpr uintptr_t ADDR_IndexOfTablePtrValue = ADDR_SpellDBContextPtr + 0x20;

        uint32_t minID = MemoryReader::Read<uint32_t>(ADDR_MinID);
        uint32_t maxID = MemoryReader::Read<uint32_t>(ADDR_MaxID);
        if (spellId < minID || spellId > maxID) return ""; 

        uintptr_t indexTablePtr = MemoryReader::Read<uintptr_t>(ADDR_IndexOfTablePtrValue);
        if (indexTablePtr == 0) return "";

        uint32_t index = spellId - minID;
        uintptr_t pRecordPtrAddr = indexTablePtr + (index * sizeof(uintptr_t));
        if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(pRecordPtrAddr))) return "";
        uintptr_t recordPtr = MemoryReader::Read<uintptr_t>(pRecordPtrAddr);
        if (recordPtr == 0 || !IsValidReadPtrHelper(reinterpret_cast<const void*>(recordPtr))) return "";

        uint8_t isCompressed = MemoryReader::Read<uint8_t>(ADDR_CompressionFlag);
        if (isCompressed) return "[Compressed Record]";
        // --- End Steps 1-4 ---

        // 5. Get Tooltip Pointer Address
        uintptr_t tooltipPtrAddr = recordPtr + OFFSET_DBC_TOOLTIP_PTR; // Use 0x22C
        if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(tooltipPtrAddr), sizeof(char*))) return "";

        // 6. Read Tooltip Pointer Value
        char* pszTooltip = MemoryReader::Read<char*>(tooltipPtrAddr);

        // 7. Read String (Helper checks pointer validity)
        return SafeReadStringHelper(pszTooltip);

    } catch (...) { /* Log error maybe? */ return "[Tooltip Read Error]"; }
}

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

    DWORD oldProtect;
    // 1. Change memory protection
    if (!VirtualProtect((LPVOID)address, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        LogStream ssErr;
        ssErr << "Error: Failed to change memory protection for patch at 0x" << std::hex << address << ". Error code: " << GetLastError();
        LogMessage(ssErr.str());
        return false;
    }

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

// --- REMOVED GetSpellInfoRaw implementation ---

// --- REMOVED GetSpellInfo implementation ---

std::vector<uint32_t> SpellManager::GetSpellbookIDs() {
    return ReadSpellbook(); // Simply return the result of ReadSpellbook
}

// --- End of Simplified Methods ---

// Initialization (if needed in the future)
// ... rest of existing code ...

// Helper function to convert uint64_t to WGUID
inline WGUID GuidFromUint64(uint64_t guid64) {
    WGUID guid;
    guid.guid_low = static_cast<uint32_t>(guid64 & 0xFFFFFFFF);
    guid.guid_high = static_cast<uint32_t>((guid64 >> 32) & 0xFFFFFFFF);
    return guid;
}

bool SpellManager::IsSpellInRange(uint32_t spellId, uint64_t targetGuid, ObjectManager* objManager) {
    if (!objManager) {
        LogMessage("IsSpellInRange Error: ObjectManager is null.");
        return false; // Cannot determine range without ObjectManager
    }

    // 1. Get Player and Target Objects
    std::shared_ptr<WowPlayer> player = objManager->GetLocalPlayer();
    std::shared_ptr<WowObject> targetObj = objManager->GetObjectByGUID(targetGuid);

    if (!player) {
        LogMessage("IsSpellInRange Error: Could not get player object.");
        return false;
    }
    if (!targetObj) {
        LogMessage("IsSpellInRange Warning: Target object not found (invalid GUID?). Assuming out of range.");
        return false;
    }

    // 2. Get Spell Range from DBC (Using a simplified placeholder for now)
    //    TODO: Implement a proper way to read SpellRange.dbc or cache ranges
    float spellMaxRange = 30.0f; // Placeholder max range (e.g., default attack/spell range)
    float spellMinRange = 0.0f;  // Placeholder min range
    
    // Example of how you might get specific ranges (needs DBC reading)
    // if (spellId == 123) { spellMaxRange = 40.0f; } // Frostbolt
    // else if (spellId == 456) { spellMaxRange = 5.0f; } // Melee attack
    // ... etc ...

    // 3. Read Positions Directly
    Vector3 playerPos = {0,0,0};
    Vector3 targetPos = {0,0,0};
    bool posReadSuccess = true;
    try {
        // Read Player Pos
        void* playerPtr = player->GetPointer();
        if (playerPtr) {
            uintptr_t pBase = reinterpret_cast<uintptr_t>(playerPtr);
            playerPos.x = MemoryReader::Read<float>(pBase + 0x79C);
            playerPos.y = MemoryReader::Read<float>(pBase + 0x798);
            playerPos.z = MemoryReader::Read<float>(pBase + 0x7A0);
        } else { throw std::runtime_error("Player pointer null"); }
        
        // Read Target Pos
        void* targetPtr = targetObj->GetPointer();
        if (targetPtr) {
            uintptr_t tBase = reinterpret_cast<uintptr_t>(targetPtr);
            // Read GameObject position differently if applicable
            if (targetObj->GetType() == WowObjectType::OBJECT_GAMEOBJECT) {
                targetPos.x = MemoryReader::Read<float>(tBase + 0xEC); // GO X
                targetPos.y = MemoryReader::Read<float>(tBase + 0xE8); // GO Y
                targetPos.z = MemoryReader::Read<float>(tBase + 0xF0); // GO Z
            } else { // Assume Unit or other standard object
                targetPos.x = MemoryReader::Read<float>(tBase + 0x79C); // Unit X
                targetPos.y = MemoryReader::Read<float>(tBase + 0x798); // Unit Y
                targetPos.z = MemoryReader::Read<float>(tBase + 0x7A0); // Unit Z
            }
        } else { throw std::runtime_error("Target pointer null"); }

    } catch(const std::exception& e) { 
        LogStream ssErr; ssErr << "IsSpellInRange Error: Exception reading positions: " << e.what(); LogMessage(ssErr.str());
        posReadSuccess = false;
    } 

    if (!posReadSuccess) {
        return false; // Cannot determine range if position read failed
    }

    // 4. Calculate Distance
    float dx = playerPos.x - targetPos.x;
    float dy = playerPos.y - targetPos.y;
    float dz = playerPos.z - targetPos.z;
    float distance = std::sqrt(dx*dx + dy*dy + dz*dz);

    // 5. Check Against Spell Range (incorporating combat reach)
    //    Combat reach values vary by race/model, using placeholders
    float playerCombatReach = 2.5f; 
    float targetCombatReach = 2.5f; 

    // Distance check: Actual distance must be <= spell's max range + target's reach
    //               Actual distance must be >= spell's min range - target's reach (if min range > 0)
    bool withinMaxRange = distance <= (spellMaxRange + targetCombatReach);
    bool beyondMinRange = (spellMinRange == 0) || (distance >= (spellMinRange - targetCombatReach));

    // Log range details for debugging
    // LogStream ssRange; ssRange << "IsSpellInRange: Spell=" << spellId << ", Target=" << std::hex << targetGuid << ", Dist=" << distance 
    //                             << ", MaxRange=" << spellMaxRange << "(+Reach=" << targetCombatReach << "), MinRange=" << spellMinRange << "(-Reach=" << targetCombatReach << ")"
    //                             << " | Result: " << (withinMaxRange && beyondMinRange);
    // LogMessage(ssRange.str());

    return withinMaxRange && beyondMinRange;
}