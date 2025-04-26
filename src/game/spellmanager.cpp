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

// --- CORRECTED GetSpellNameByID Implementation --- (Replacing with user-provided code)
std::string SpellManager::GetSpellNameByID(uint32_t spellId) {
    LogStream ssTrace; // For detailed tracing
    ssTrace << "GetSpellNameByID Enter: SpellID=" << spellId;
    LogMessage(ssTrace.str());

    try {
        // --- CORRECTED LOGIC ---
        // Addresses relative to the static pointer location ADDR_SpellDBContextPtr (0xAD49D0)
        constexpr uintptr_t ADDR_MinID = ADDR_SpellDBContextPtr + 0x10; // 0xAD49E0
        constexpr uintptr_t ADDR_MaxID = ADDR_SpellDBContextPtr + 0x0C; // 0xAD49DC
        constexpr uintptr_t ADDR_IndexOfTablePtrValue = ADDR_SpellDBContextPtr + 0x20; // 0xAD49F0

        // 1. Read MinID, MaxID, and the pointer to the Index Table directly
        uint32_t minID = 0;
        uint32_t maxID = 0;
        uintptr_t indexTablePtr = 0; // This will hold the POINTER to the array of record pointers
        try {
            minID = MemoryReader::Read<uint32_t>(ADDR_MinID);
            maxID = MemoryReader::Read<uint32_t>(ADDR_MaxID);
            indexTablePtr = MemoryReader::Read<uintptr_t>(ADDR_IndexOfTablePtrValue); // Read the pointer value

            ssTrace = LogStream(); // Reuse stream
            ssTrace << "  Read MinID from 0x" << std::hex << ADDR_MinID << " = " << std::dec << minID; LogMessage(ssTrace.str());
            ssTrace = LogStream(); ssTrace << "  Read MaxID from 0x" << std::hex << ADDR_MaxID << " = " << std::dec << maxID; LogMessage(ssTrace.str());
            ssTrace = LogStream(); ssTrace << "  Read IndexTablePtr Value from 0x" << std::hex << ADDR_IndexOfTablePtrValue << " = 0x" << std::hex << indexTablePtr; LogMessage(ssTrace.str());

        } catch (const std::runtime_error& readErr) {
            LogStream ssErr;
            ssErr << "GetSpellNameByID Error: Exception reading Min/Max/TablePtrValue: " << readErr.what();
            LogMessage(ssErr.str());
            return "[Context Data Read Error]";
        }
        // --- END CORRECTED LOGIC ---


        // 2. Validate Spell ID range and IndexTablePtr value
        if (spellId < minID || spellId > maxID) {
            ssTrace = LogStream(); ssTrace << "  Result: ID " << std::dec << spellId << " out of range (" << minID << "-" << maxID << ")"; LogMessage(ssTrace.str());
            return "[ID Out of Range]";
        }
        if (indexTablePtr == 0) {
            LogMessage("GetSpellNameByID Error: IndexTablePtr read is NULL.");
            return "[Null Index Tbl]";
        }
        // Check if the INDEX TABLE POINTER points to valid memory
        if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(indexTablePtr))) {
             LogStream ssErr; ssErr << "GetSpellNameByID Error: IndexTablePtr 0x" << std::hex << indexTablePtr << " points to invalid memory."; LogMessage(ssErr.str());
            return "[Invalid Index Tbl Ptr]";
        }

        // 3. Calculate index and get Record Pointer address (within the index table)
        uint32_t index = spellId - minID;
        uintptr_t pRecordPtrAddr = indexTablePtr + (index * sizeof(uintptr_t)); // Address of the pointer *within* the index table
        ssTrace = LogStream(); ssTrace << "  Calculated Index=" << std::dec << index << ", Addr of Record Ptr = 0x" << std::hex << pRecordPtrAddr; LogMessage(ssTrace.str());

        // Check if the address *within* the index table is readable
        if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(pRecordPtrAddr))) {
             LogStream ssErr; ssErr << "GetSpellNameByID Error: Cannot read from index table at address 0x" << std::hex << pRecordPtrAddr; LogMessage(ssErr.str());
            return "[Invalid Index Entry Addr]";
        }

        // 4. Get the actual Record Pointer value from the index table
        uintptr_t recordPtr = MemoryReader::Read<uintptr_t>(pRecordPtrAddr);
        ssTrace = LogStream(); ssTrace << "  Read Record Ptr from Index Table = 0x" << std::hex << recordPtr; LogMessage(ssTrace.str());

        if (recordPtr == 0) {
             // This might be normal for unused spell IDs within the range
             // LogStream ssWarn; ssWarn << "GetSpellNameByID: Null record pointer for Spell ID " << spellId << "."; LogMessage(ssWarn.str());
            return "[Null Record Ptr]";
        }
        // Check if the record pointer points to valid memory
         if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(recordPtr))) {
             LogStream ssErr; ssErr << "GetSpellNameByID Error: RecordPtr 0x" << std::hex << recordPtr << " points to invalid memory."; LogMessage(ssErr.str());
            return "[Invalid Record Ptr]";
        }

        // 5. Check for compression
        uint8_t isCompressed = MemoryReader::Read<uint8_t>(ADDR_CompressionFlag);
        if (isCompressed) return "[Compressed Record]";

        // 6. Get the address where the name pointer is stored within the record
        uintptr_t namePtrAddr = recordPtr + OFFSET_DBC_NAME_PTR; // Ensure this uses 0x220
        ssTrace = LogStream(); ssTrace << "  Address of Name Ptr within record = 0x" << std::hex << namePtrAddr; LogMessage(ssTrace.str());

         // Check if the address *within* the record holding the name pointer is readable
         if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(namePtrAddr), sizeof(char*))) {
             LogStream ssErr; ssErr << "GetSpellNameByID Error: Cannot read name pointer address from record at 0x" << std::hex << namePtrAddr; LogMessage(ssErr.str());
            return "[Invalid Record Data @ Name]";
        }

        // 7. Read the name pointer from the record
        char* pszName = MemoryReader::Read<char*>(namePtrAddr); // Read the pointer to the string
        ssTrace = LogStream(); ssTrace << "  Read Name Ptr Value = 0x" << std::hex << reinterpret_cast<uintptr_t>(pszName); LogMessage(ssTrace.str());


        // 8. Read the string safely using the helper function
        // The helper checks pszName validity
        std::string resultName = SafeReadStringHelper(pszName);
        ssTrace = LogStream(); ssTrace << "  SafeReadString Result: \"" << resultName << "\""; LogMessage(ssTrace.str());
        return resultName;

    } catch (const std::runtime_error& e) {
        LogStream ssErr;
        ssErr << "GetSpellNameByID: Runtime error for spell ID " << spellId << " - " << e.what();
        LogMessage(ssErr.str());
        return "[Read Error]";
    } catch (...) {
        LogStream ssErr;
        ssErr << "GetSpellNameByID: Unknown exception for spell ID " << spellId;
        LogMessage(ssErr.str());
        return "[Unknown Error]";
    }
}
// --- End GetSpellNameByID ---

// --- GetSpellDescriptionByID Implementation (Direct Pointer Logic) ---
std::string SpellManager::GetSpellDescriptionByID(uint32_t spellId) {
    // LogStream ssTrace; ssTrace << "GetSpellDescriptionByID Enter: SpellID=" << spellId; LogMessage(ssTrace.str());
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
        // ssTrace = LogStream(); ssTrace << "  Address of Desc Ptr = 0x" << std::hex << descPtrAddr; LogMessage(ssTrace.str());
        if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(descPtrAddr), sizeof(char*))) return "";

        // 6. Read Description Pointer Value
        char* pszDesc = MemoryReader::Read<char*>(descPtrAddr);
        // ssTrace = LogStream(); ssTrace << "  Read Desc Ptr Value = 0x" << std::hex << reinterpret_cast<uintptr_t>(pszDesc); LogMessage(ssTrace.str());

        // 7. Read String (Helper checks pointer validity)
        return SafeReadStringHelper(pszDesc);

    } catch (...) { /* Log error maybe? */ return "[Desc Read Error]"; }
}

// --- GetSpellTooltipByID Implementation (Direct Pointer Logic) ---
std::string SpellManager::GetSpellTooltipByID(uint32_t spellId) {
    // LogStream ssTrace; ssTrace << "GetSpellTooltipByID Enter: SpellID=" << spellId; LogMessage(ssTrace.str());
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
        // ssTrace = LogStream(); ssTrace << "  Address of Tooltip Ptr = 0x" << std::hex << tooltipPtrAddr; LogMessage(ssTrace.str());
        if (!IsValidReadPtrHelper(reinterpret_cast<const void*>(tooltipPtrAddr), sizeof(char*))) return "";

        // 6. Read Tooltip Pointer Value
        char* pszTooltip = MemoryReader::Read<char*>(tooltipPtrAddr);
        // ssTrace = LogStream(); ssTrace << "  Read Tooltip Ptr Value = 0x" << std::hex << reinterpret_cast<uintptr_t>(pszTooltip); LogMessage(ssTrace.str());

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

/**
 * @brief Gets the spell IDs from the player's spellbook.
 *
 * Just calls ReadSpellbook.
 *
 * @return A vector of spell IDs.
 */
std::vector<uint32_t> SpellManager::GetSpellbookIDs() {
    return ReadSpellbook(); // Simply return the result of ReadSpellbook
}

// --- End of Simplified Methods ---

// Initialization (if needed in the future)
// ... rest of existing code ...