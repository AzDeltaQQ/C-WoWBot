#include "functions.h" // Include definitions for types and offsets
#include <windows.h>
#include <vector>
#include <memory> // For std::shared_ptr, std::dynamic_pointer_cast
#include <stdexcept> // For std::runtime_error potentially
#include <utility> // For std::pair
#include "../utils/log.h" // Corrected path
#include "../utils/memory.h" // Corrected path
#include "../bot/core/MovementController.h" // Corrected path
#include "wowobject.h"       // Path is fine (same directory)
#include "objectmanager.h" // Path is fine (same directory)
#include "../lua/lua_executor.h" // Added for Lua execution

// Local offsets needed in this file (taken from wowobject.cpp or C# MemoryAddresses)
constexpr DWORD OBJECT_DESCRIPTOR_PTR_OFFSET = 0x8;
constexpr uintptr_t OFF_BackpackStartOffset = 0x5C8; // Byte offset from player base for backpack items (WotLK)
constexpr uintptr_t OFF_ContainerSlotsStart = 0x108; // Byte offset from container base for slot items (WotLK ItemCache)
constexpr uintptr_t OFF_NumSlots = 0x760;           // Byte offset from container base for num slots (WotLK ItemCache)
constexpr uintptr_t OFF_PlayerBagGuidsStart = 0x1E68; // Byte offset for equipped bag GUIDs (WotLK EquipmentFirstItemOffset)


// Define the function pointers (declared extern in functions.h)
CastLocalPlayerSpellFn CastLocalPlayerSpell = nullptr;
GetLocalPlayerGuidFn GetLocalPlayerGuid = nullptr;
GetItemCacheEntryFn GetItemCacheEntry = nullptr;
SellItemByGuidFn SellItemByGuid = nullptr;
HandleTerrainClickFn HandleTerrainClick = nullptr;
RetrieveAndProcessClientObjectFn RetrieveAndProcessClientObject = nullptr;

// Function to initialize all function pointers
void InitializeFunctions() {
    LogMessage("InitializeFunctions: Using provided offsets as ABSOLUTE addresses.");

    uintptr_t baseAddress = 0; // Not needed if using absolute addresses

    // Initialize pointers with ABSOLUTE addresses from log/IDA
    CastLocalPlayerSpell = (CastLocalPlayerSpellFn)0x80da40;
    GetLocalPlayerGuid = (GetLocalPlayerGuidFn)0x4d3790;
    GetItemCacheEntry = (GetItemCacheEntryFn)0x67ca30;
    SellItemByGuid = (SellItemByGuidFn)0x6d2d40;
    HandleTerrainClick = (HandleTerrainClickFn)0x727400;
    RetrieveAndProcessClientObject = (RetrieveAndProcessClientObjectFn)0x00513740;

    // Initialize MovementController with the CTM function address (CAST to uintptr_t)
    if (!MovementController::GetInstance().InitializeClickHandler(reinterpret_cast<uintptr_t>(HandleTerrainClick))) {
         LogMessage("InitializeFunctions Error: Failed to initialize MovementController Click Handler!");
    }

    LogStream ss;
    ss << "InitializeFunctions Pointers (Absolute Addresses):\n"
       << "  CastLocalPlayerSpell: 0x" << std::hex << (uintptr_t)CastLocalPlayerSpell << "\n"
       << "  GetLocalPlayerGuid: 0x" << std::hex << (uintptr_t)GetLocalPlayerGuid << "\n"
       << "  GetItemCacheEntry: 0x" << std::hex << (uintptr_t)GetItemCacheEntry << "\n"
       << "  SellItemByGuid: 0x" << std::hex << (uintptr_t)SellItemByGuid << "\n"
       << "  OFF_HandleTerrainClick: 0x" << std::hex << (uintptr_t)HandleTerrainClick << "\n"
       << "  RetrieveAndProcessClientObject: 0x" << std::hex << (uintptr_t)RetrieveAndProcessClientObject;
    LogMessage(ss.str());

    if (!CastLocalPlayerSpell || !GetLocalPlayerGuid || !GetItemCacheEntry || !SellItemByGuid || !RetrieveAndProcessClientObject) {
        LogMessage("InitializeFunctions Error: One or more required function pointers are NULL!");
        // Handle error appropriately
    } else {
        LogMessage("InitializeFunctions complete.");
    }
}

// Function to get the number of free bag slots
int GetFreeBagSlots() {
    auto player = ObjectManager::GetInstance()->GetLocalPlayer();
    if (!player) {
        LogMessage("GetFreeBagSlots Error: Could not get local player object.");
        return 0;
    }
    uintptr_t playerBase = reinterpret_cast<uintptr_t>(player->GetPointer());
    if (!playerBase) {
        LogMessage("GetFreeBagSlots Error: Player object pointer is null.");
        return 0;
    }

    int totalFreeSlots = 0;

    // --- Backpack Calculation (Bag 0) ---
    try {
        int backpackSlots = 16; // Default backpack size
        for (int slot = 0; slot < backpackSlots; ++slot) {
            // Use the byte offset for backpack items relative to player base
            uintptr_t itemGuidOffset = playerBase + OFF_BackpackStartOffset + (slot * sizeof(uint64_t)); 
            uint64_t itemGuid64 = MemoryReader::Read<uint64_t>(itemGuidOffset);
            if (itemGuid64 == 0) {
                totalFreeSlots++;
            }
        }
    } catch (const std::exception& e) {
        LogStream ss;
        ss << "GetFreeBagSlots Error reading backpack slots: " << e.what();
        LogMessage(ss.str());
        // Continue to check other bags even if backpack fails
    } catch (...) {
        LogMessage("GetFreeBagSlots Unknown Error reading backpack slots.");
    }

    {
        LogStream ss;
        ss << "GetFreeBagSlots: Backpack check completed. Free slots so far: " << totalFreeSlots;
        LogMessage(ss.str());
    }

    // --- Equipped Bags Calculation (Bags 1-4) ---
    for (int bagIndex = 0; bagIndex < 4; ++bagIndex) { // Check bag slots 0, 1, 2, 3 (map to Bag 1, 2, 3, 4)
        try {
            // Read the GUID of the container item equipped in the corresponding slot
            uintptr_t bagItemGuidOffset = playerBase + OFF_PlayerBagGuidsStart + (bagIndex * sizeof(uint64_t));
            uint64_t containerGuid64 = MemoryReader::Read<uint64_t>(bagItemGuidOffset); 
            WGUID containerGuid;
            containerGuid.guid_low = (uint32_t)containerGuid64;
            containerGuid.guid_high = (uint32_t)(containerGuid64 >> 32);

            if (!containerGuid.IsValid()) {
                // This is normal, no bag equipped
                continue; 
            }

            // Get the container object from the ObjectManager
            auto containerObj = ObjectManager::GetInstance()->GetObjectByGUID(containerGuid);
            if (!containerObj || containerObj->GetType() != WowObjectType::OBJECT_CONTAINER) {
                LogStream ss;
                ss << "GetFreeBagSlots Warning: Could not find valid container object for GUID 0x" 
                   << std::hex << containerGuid64 << std::dec << " in bag equipment slot index " << bagIndex << ".";
                LogMessage(ss.str());
                continue;
            }

            auto container = std::dynamic_pointer_cast<WowContainer>(containerObj); 
            if (!container) {
                 LogStream ss;
                 ss << "GetFreeBagSlots Warning: Failed to cast object GUID 0x" << std::hex << containerGuid64 
                    << std::dec << " to WowContainer.";
                 LogMessage(ss.str());
                 continue;
            }

            uintptr_t containerBase = reinterpret_cast<uintptr_t>(container->GetPointer());
            if (!containerBase) {
                LogStream ss;
                ss << "GetFreeBagSlots Warning: Container GUID 0x" << std::hex << containerGuid64 
                   << std::dec << " has a null pointer.";
                LogMessage(ss.str());
                continue;
            }

            int numSlots = MemoryReader::Read<int>(containerBase + OFF_NumSlots);
            if (numSlots <= 0) {
                 LogStream ss;
                 ss << "GetFreeBagSlots Warning: Container GUID 0x" << std::hex << containerGuid64 
                    << std::dec << " reports " << numSlots << " slots.";
                 LogMessage(ss.str());
                 continue; 
            }

            int usedSlots = 0;
            uintptr_t slotArrayBase = containerBase + OFF_ContainerSlotsStart;
            for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex) {
                uint64_t itemGuidInSlot = MemoryReader::Read<uint64_t>(slotArrayBase + (slotIndex * sizeof(uint64_t)));
                if (itemGuidInSlot != 0) {
                    usedSlots++;
                }
            }
            int freeInThisBag = numSlots - usedSlots;
            {
                LogStream ss;
                ss << "GetFreeBagSlots: Bag " << (bagIndex + 1) << " (GUID 0x" << std::hex << containerGuid64 << std::dec << "): " 
                   << freeInThisBag << " free slots (" << usedSlots << "/" << numSlots << ")";
                LogMessage(ss.str());
            }
            totalFreeSlots += freeInThisBag;

        } catch (const std::exception& e) {
             LogStream ss;
             ss << "GetFreeBagSlots Error reading equipped bag index " << bagIndex << ": " << e.what();
             LogMessage(ss.str());
             // Continue to next bag
        } catch (...) {
             LogStream ss;
             ss << "GetFreeBagSlots Unknown Error reading equipped bag index " << bagIndex;
             LogMessage(ss.str());
        }
    }

    {
        LogStream ss;
        ss << "GetFreeBagSlots: Final total free slots calculated: " << totalFreeSlots;
        LogMessage(ss.str());
    }
    return totalFreeSlots;
}


// Function to check if a specific bag slot is empty
// bagIndex: 0 = backpack, 1-4 = equipped bags
// slotIndex: 0-based index within the bag/backpack
bool IsBagSlotEmpty(int bagIndex, int slotIndex) {
    try {
        auto player = ObjectManager::GetInstance()->GetLocalPlayer();
        if (!player) return true; // Assume empty if no player
        uintptr_t playerBase = reinterpret_cast<uintptr_t>(player->GetPointer());
        if (!playerBase) return true;

        uint64_t itemGuid64 = 0;

        if (bagIndex == 0) { // Backpack
            int backpackSlots = 16;
            if (slotIndex < 0 || slotIndex >= backpackSlots) return true; // Invalid slot
            uintptr_t itemGuidOffset = playerBase + OFF_BackpackStartOffset + (slotIndex * sizeof(uint64_t));
            itemGuid64 = MemoryReader::Read<uint64_t>(itemGuidOffset);

        } else if (bagIndex >= 1 && bagIndex <= 4) { // Equipped Bags
            // Get the GUID of the container item in the specified bag slot (index 0-3)
            uintptr_t bagItemGuidOffset = playerBase + OFF_PlayerBagGuidsStart + ((bagIndex - 1) * sizeof(uint64_t)); 
            uint64_t containerGuid64 = MemoryReader::Read<uint64_t>(bagItemGuidOffset); 
            WGUID containerGuid;
            containerGuid.guid_low = (uint32_t)containerGuid64;
            containerGuid.guid_high = (uint32_t)(containerGuid64 >> 32);

            if (!containerGuid.IsValid()) return true; // No bag equipped, slot non-existent

            auto containerObj = ObjectManager::GetInstance()->GetObjectByGUID(containerGuid);
            if (!containerObj || containerObj->GetType() != WowObjectType::OBJECT_CONTAINER) return true; 
            auto container = std::dynamic_pointer_cast<WowContainer>(containerObj); 
            if (!container) return true;

            uintptr_t containerBase = reinterpret_cast<uintptr_t>(container->GetPointer());
            if (!containerBase) return true;

            int numSlots = MemoryReader::Read<int>(containerBase + OFF_NumSlots);
            if (slotIndex < 0 || slotIndex >= numSlots) return true; // Invalid slot

            uintptr_t slotArrayBase = containerBase + OFF_ContainerSlotsStart;
            itemGuid64 = MemoryReader::Read<uint64_t>(slotArrayBase + (slotIndex * sizeof(uint64_t)));

        } else {
            return true; // Invalid bag index
        }

        return itemGuid64 == 0;

    } catch (const std::exception& e) {
        LogStream ss;
        ss << "IsBagSlotEmpty Error checking bag " << bagIndex << ", slot " << slotIndex << ": " << e.what();
        LogMessage(ss.str());
        return true; // Assume empty on error
    } catch (...) {
        LogStream ss;
        ss << "IsBagSlotEmpty Unknown Error checking bag " << bagIndex << ", slot " << slotIndex;
        LogMessage(ss.str());
        return true; // Assume empty on error
    }
}

// Function to find the first empty bag slot
std::pair<int, int> FindFirstEmptyBagSlot() {
    // Iterate backpack first (usually fills first)
    for (int slotIndex = 0; slotIndex < 16; ++slotIndex) {
        if (IsBagSlotEmpty(0, slotIndex)) {
            return {0, slotIndex};
        }
    }
    // Then iterate equipped bags
    for (int bagIndex = 1; bagIndex <= 4; ++bagIndex) {
         try {
             auto player = ObjectManager::GetInstance()->GetLocalPlayer();
             if (!player) continue; 
             uintptr_t playerBase = reinterpret_cast<uintptr_t>(player->GetPointer());
             if (!playerBase) continue;

             uintptr_t bagItemGuidOffset = playerBase + OFF_PlayerBagGuidsStart + ((bagIndex - 1) * sizeof(uint64_t));
             uint64_t containerGuid64 = MemoryReader::Read<uint64_t>(bagItemGuidOffset); 
             WGUID containerGuid;
             containerGuid.guid_low = (uint32_t)containerGuid64;
             containerGuid.guid_high = (uint32_t)(containerGuid64 >> 32);
             if (!containerGuid.IsValid()) continue; // No bag

             auto containerObj = ObjectManager::GetInstance()->GetObjectByGUID(containerGuid);
             if (!containerObj || containerObj->GetType() != WowObjectType::OBJECT_CONTAINER) continue;
             auto container = std::dynamic_pointer_cast<WowContainer>(containerObj);
             if (!container) continue;

             uintptr_t containerBase = reinterpret_cast<uintptr_t>(container->GetPointer());
             if (!containerBase) continue;
             int numSlots = MemoryReader::Read<int>(containerBase + OFF_NumSlots);
             if (numSlots <= 0) continue;

            for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex) {
                if (IsBagSlotEmpty(bagIndex, slotIndex)) { 
                    return {bagIndex, slotIndex};
                }
            }
         } catch (const std::exception& e) {
            LogStream ss;
            ss << "FindFirstEmptyBagSlot Error checking bag " << bagIndex << ": " << e.what();
            LogMessage(ss.str());
             // Continue to next bag
         } catch (...) {
             LogStream ss;
             ss << "FindFirstEmptyBagSlot Unknown Error checking bag " << bagIndex;
             LogMessage(ss.str());
         }
    }
    return {-1, -1}; // No empty slot found
}

// --- NEW Implementation: GetItemQuality --- (Using Lua GetContainerItemInfo)
ItemQuality GetItemQuality(int bagIndex, int slotIndex) {
    // Note: bagIndex is 0-4 (0=Backpack), slotIndex is 0-based (C++)

    try {
        // --- 1. Get Item Quality using Lua GetContainerItemInfo --- 
        // Lua expects bagID 0 for backpack, 1-4 for bags. This matches our C++ bagIndex.
        // Lua expects slotID 1-based.
        std::string luaScript = "local _, _, _, q = GetContainerItemInfo(" 
                                + std::to_string(bagIndex) + ", " 
                                + std::to_string(slotIndex + 1) + "); return q";
        
        // ExecuteString<int> already handles nil returns as 0
        int qualityInt = LuaExecutor::ExecuteString<int>(luaScript); 

        // --- 2. Cast to Enum --- 
        if (qualityInt >= 0 && qualityInt <= static_cast<int>(ItemQuality::ARTIFACT)) {
            return static_cast<ItemQuality>(qualityInt);
        } else {
            // This case should ideally not be hit if Lua returns nil (handled as 0/POOR) 
            // or a valid quality number. Log if it somehow returns an unexpected number.
            LogStream ss; ss << "GetItemQuality Warning: Lua returned unexpected quality value " << qualityInt 
                           << " for Bag " << bagIndex << ", Slot " << slotIndex;
            LogMessage(ss.str());
            return ItemQuality::UNKNOWN;
        }

    } catch (const LuaExecutor::LuaException& e) {
        // Don't spam logs for errors on potentially empty slots, return UNKNOWN silently
        // LogStream ssErr; ssErr << "GetItemQuality Lua Error checking Bag " << bagIndex << ", Slot " << slotIndex << ": " << e.what(); LogMessage(ssErr.str());
        (void)e; // Explicitly mark 'e' as unused if not logging
        return ItemQuality::UNKNOWN;
    } catch (const std::exception& e) {
        LogStream errLog;
        errLog << "GetItemQuality EXCEPTION: Failed to read item quality for Bag " << bagIndex << ", Slot " << slotIndex << ": " << e.what();
        LogMessage(errLog.str());
        return ItemQuality::UNKNOWN;
    } catch (...) {
        LogStream ssErr; ssErr << "GetItemQuality Unknown Error checking bag " << bagIndex << ", slot " << slotIndex; LogMessage(ssErr.str());
        return ItemQuality::UNKNOWN; // Return UNKNOWN on error
    }
}


// --- Placeholder Implementations ---

// --- Implementation using Lua --- 
bool IsVendorWindowOpen() {
    try {
        // Execute the Lua code to check MerchantFrame visibility
        // Use 'and' for short-circuiting: only call IsVisible if MerchantFrame exists.
        bool isVisible = LuaExecutor::ExecuteString<bool>("return MerchantFrame and MerchantFrame:IsVisible()");
        return isVisible;
    } catch (const LuaExecutor::LuaException& e) {
        // Log the Lua error
        LogStream ss;
        ss << "IsVendorWindowOpen Lua Error: " << e.what();
        LogMessage(ss.str());
        return false; // Assume closed on error
    } catch (const std::exception& e) {
        // Catch other potential standard exceptions
         LogStream ss;
        ss << "IsVendorWindowOpen Standard Error: " << e.what();
        LogMessage(ss.str());
        return false;
    } catch (...) {
        // Catch any other unknown errors
        LogMessage("IsVendorWindowOpen Unknown Error occurred.");
        return false;
    }
}

void SellItem(uint64_t vendorGuid, int bagIndex, int slotIndex) {
    LogStream log;
    log << "SellItem called: VendorGUID=0x" << std::hex << vendorGuid
        << ", Bag=" << std::dec << bagIndex << ", Slot=" << slotIndex;
    LogMessage(log.str());

    if (!SellItemByGuid) {
        LogMessage("SellItem Error: SellItemByGuid function pointer is null.");
        return;
    }

    if (!IsVendorWindowOpen()) {
        LogMessage("SellItem Error: Vendor window is not open.");
        return;
    }

    auto player = ObjectManager::GetInstance()->GetLocalPlayer();
    if (!player) {
        LogMessage("SellItem Error: Could not get local player object.");
        return;
    }
    uintptr_t playerBase = reinterpret_cast<uintptr_t>(player->GetPointer());
     if (!playerBase) {
        LogMessage("SellItem Error: Player object pointer is null.");
        return;
    }

    uint64_t itemGuid64 = 0;

    try {
        // Get Item GUID based on bag/slot (using logic similar to IsBagSlotEmpty, no WowContainer)
        if (bagIndex == 0) { // Backpack
            int backpackSlots = 16;
            if (slotIndex < 0 || slotIndex >= backpackSlots) {
                LogMessage("SellItem Error: Invalid slot index for backpack.");
                return;
            }
            uintptr_t itemGuidOffset = playerBase + OFF_BackpackStartOffset + (slotIndex * sizeof(uint64_t));
            itemGuid64 = MemoryReader::Read<uint64_t>(itemGuidOffset);

        } else if (bagIndex >= 1 && bagIndex <= 4) { // Equipped Bags
            uintptr_t bagItemGuidOffset = playerBase + OFF_PlayerBagGuidsStart + ((bagIndex - 1) * sizeof(uint64_t)); 
            uint64_t containerGuid64 = MemoryReader::Read<uint64_t>(bagItemGuidOffset); 
            WGUID containerGuid;
            containerGuid.guid_low = (uint32_t)containerGuid64;
            containerGuid.guid_high = (uint32_t)(containerGuid64 >> 32);

            if (!containerGuid.IsValid()) {
                 LogMessage("SellItem Error: No container equipped in that bag slot.");
                 return;
            }

            auto containerObj = ObjectManager::GetInstance()->GetObjectByGUID(containerGuid);
            if (!containerObj || containerObj->GetType() != WowObjectType::OBJECT_CONTAINER) {
                 LogMessage("SellItem Error: Could not find valid container object.");
                 return;
            }

            uintptr_t containerBase = reinterpret_cast<uintptr_t>(containerObj->GetPointer());
            if (!containerBase) {
                 LogMessage("SellItem Error: Container has a null pointer.");
                 return;
            }

            int numSlots = MemoryReader::Read<int>(containerBase + OFF_NumSlots);
            if (slotIndex < 0 || slotIndex >= numSlots) {
                LogMessage("SellItem Error: Invalid slot index for this container.");
                return;
            }

            uintptr_t slotArrayBase = containerBase + OFF_ContainerSlotsStart;
            itemGuid64 = MemoryReader::Read<uint64_t>(slotArrayBase + (slotIndex * sizeof(uint64_t)));

        } else {
             LogMessage("SellItem Error: Invalid bag index.");
             return;
        }

        if (itemGuid64 == 0) {
            LogMessage("SellItem Error: No item found in the specified slot to sell.");
            return;
        }

        // Call the internal sell function (using absolute address from OFF_SellItemByGuid)
        LogStream callLog;
        callLog << "SellItem: Calling SellItemByGuid(0x" << std::hex << vendorGuid << ", 0x" << itemGuid64 << ", 1) at address 0x" << (uintptr_t)SellItemByGuid;
        LogMessage(callLog.str());

        // Extract low and high 32-bit parts of the GUIDs
        int itemGuidLow = (int)(itemGuid64 & 0xFFFFFFFF);
        int itemGuidHigh = (int)(itemGuid64 >> 32);
        int vendorGuidLow = (int)(vendorGuid & 0xFFFFFFFF);
        int vendorGuidHigh = (int)(vendorGuid >> 32);
        int count = 1; // The 5th parameter is likely the count

        // Call the function pointer with the correct 5-integer signature
        SellItemByGuid(itemGuidLow, itemGuidHigh, vendorGuidLow, vendorGuidHigh, count);

        LogMessage("SellItem: SellItemByGuid called successfully.");

    } catch (const std::exception& e) {
        LogStream errLog;
        errLog << "SellItem EXCEPTION: " << e.what();
        LogMessage(errLog.str());
    } catch (...) {
        LogMessage("SellItem UNKNOWN EXCEPTION occurred.");
    }
}

void CloseVendorWindow() {
    try {
        LuaExecutor::ExecuteString<void>("CloseMerchant()"); // Use ExecuteString
    } catch (const LuaExecutor::LuaException& e) {
        LogStream ss; ss << "CloseVendorWindow Lua Error: " << e.what(); LogMessage(ss.str());
    } catch (const std::exception& e) {
        LogStream ss; ss << "CloseVendorWindow Standard Error: " << e.what(); LogMessage(ss.str());
    } catch (...) {
        LogMessage("CloseVendorWindow Unknown Error occurred.");
    }
}

void RepairAllItems() {
    // TODO: Implement repair logic
    LogMessage("RepairAllItems() - NOT IMPLEMENTED");
}

// Optional Wrapper if needed, but direct call is fine if pointer is checked
// void SellItemByGuid_Wrapper(uint64_t itemGuid, uint64_t vendorGuid, int count) {
//     if (SellItemByGuid) {
//         SellItemByGuid(itemGuid, vendorGuid, count);
//     } else {
//         LogMessage("Error: SellItemByGuid function pointer is null!");
//     }
// }

// --- Implementations for missing functions needed by GrindingEngine ---

int GetContainerNumSlots(int bagIndex) {
    // bagIndex: 0 = backpack, 1-4 = equipped bags
    try {
        auto player = ObjectManager::GetInstance()->GetLocalPlayer();
        if (!player) return 0;
        uintptr_t playerBase = reinterpret_cast<uintptr_t>(player->GetPointer());
        if (!playerBase) return 0;

        if (bagIndex == 0) {
            return 16; // Backpack always has 16 slots
        } else if (bagIndex >= 1 && bagIndex <= 4) {
            // Get container GUID from player equipment slots
            uintptr_t bagItemGuidOffset = playerBase + OFF_PlayerBagGuidsStart + ((bagIndex - 1) * sizeof(uint64_t));
            uint64_t containerGuid64 = MemoryReader::Read<uint64_t>(bagItemGuidOffset);
            WGUID containerGuid;
            containerGuid.guid_low = (uint32_t)containerGuid64;
            containerGuid.guid_high = (uint32_t)(containerGuid64 >> 32);

            // --- Added Logging ---
            LogStream ssLog; 
            ssLog << "GetContainerNumSlots(Bag " << bagIndex << "): Read Container GUID 0x" << std::hex << containerGuid64 << std::dec;

            if (!containerGuid.IsValid()) {
                ssLog << ". GUID is invalid.";
                LogMessage(ssLog.str());
                return 0; // No bag equipped
            }

            // Get container object
            auto containerObj = ObjectManager::GetInstance()->GetObjectByGUID(containerGuid);
            if (!containerObj || containerObj->GetType() != WowObjectType::OBJECT_CONTAINER) {
                ssLog << ". ObjectManager found no valid CONTAINER object.";
                LogMessage(ssLog.str());
                return 0;
            }
            ssLog << ". Found container object (Type: " << static_cast<int>(containerObj->GetType()) << ")";

            // Get container base pointer
            uintptr_t containerBase = reinterpret_cast<uintptr_t>(containerObj->GetPointer());
            if (!containerBase) {
                ssLog << ". Container object pointer is NULL.";
                LogMessage(ssLog.str());
                return 0;
            }
            ssLog << ". ContainerBase: 0x" << std::hex << containerBase << std::dec;

            // Read number of slots from container object
            int numSlots = MemoryReader::Read<int>(containerBase + OFF_NumSlots);
            ssLog << ". Read NumSlots: " << numSlots << " (from offset 0x" << std::hex << OFF_NumSlots << std::dec << ")";
            LogMessage(ssLog.str()); // Log everything gathered

            return (numSlots > 0) ? numSlots : 0;
        } else {
            LogStream ss; ss << "GetContainerNumSlots Warning: Invalid bag index requested: " << bagIndex; LogMessage(ss.str());
            return 0; // Invalid bag index
        }
    } catch (const std::exception& e) {
        LogStream ss;
        ss << "GetContainerNumSlots Error checking bag " << bagIndex << ": " << e.what();
        LogMessage(ss.str());
        return 0;
    } catch (...) {
        LogStream ss;
        ss << "GetContainerNumSlots Unknown Error checking bag " << bagIndex;
        LogMessage(ss.str());
        return 0;
    }
}

// Define types for the necessary game functions based on reverse engineering
// getBagItem(BagID) -> returns uint64_t BagGUID
typedef uint64_t(__cdecl* GetBagItemFn)(int bagId);
const GetBagItemFn GetBagItem = (GetBagItemFn)0x5D6F20; // Address from IDA

// findObjectByGuidAndFlags(GuidLow, GuidHigh, TypeMask, ...) -> returns ObjectPtr (e.g., BagObjectPtr or ItemStructPtr)
// Note: Simplified signature, assuming only GUID and TypeMask are primary args needed here. Adjust if other args are crucial.
// TypeMask: 4 = Container, 2 = Item
typedef void* (__cdecl* FindObjectByGuidAndFlagsFn)(uint32_t guidLow, uint32_t guidHigh, uint32_t typeMask);
const FindObjectByGuidAndFlagsFn FindObjectByGuidAndFlags = (FindObjectByGuidAndFlagsFn)0x4D4DB0; // Correct Address from IDA

// retrieveBagItemData(BagObjectPtr ecx, SlotID stack) -> returns ItemStructPtr (eax)
// We will call this using its address. The signature helps understand its purpose.
// The calling convention requires setting ECX manually before the call.
const uintptr_t retrieveBagItemDataAddr = 0x754390; // Address from IDA

// retrievePlayerInventoryItemData(int *outAdjustedSlotID eax, int luaArg [ebp+8], _DWORD *outBagObjectPtr [ebp+0Ch], _DWORD *outIsBag [ebp+10h]) -> returns success/fail (eax)
// This function is complex to call directly due to its custom calling convention.
// Instead of calling it, we will *replicate* its logic for the specific case of BagID 0 (Backpack).
// const uintptr_t retrievePlayerInventoryItemDataAddr = 0x5D7380; // Address from IDA - Not directly called for now


// Structure to hold WoW container field indices (adjust if using a proper enum header)
struct WoWContainerFields {
    static const int CONTAINER_FIELD_NUM_SLOTS = 6; // Index 6 * 4 = 0x18
    static const int CONTAINER_FIELD_SLOT_1 = 8;    // Index 8 * 4 = 0x20
};

// Item Structure Offsets (Deduced from retrieveBagItemData callers)
constexpr uintptr_t OFF_ItemStruct_DescriptorPtr = 0x8;
constexpr uintptr_t OFF_ItemDescriptor_GuidLow = 0x0;
constexpr uintptr_t OFF_ItemDescriptor_GuidHigh = 0xC;

uint64_t GetItemGuidInSlot(int bagIndex, int slotIndex) {
    LogStream ssEntry; ssEntry << "GetItemGuidInSlot(Bag: " << bagIndex << ", Slot: " << slotIndex << ")"; LogMessage(ssEntry.str());

    if (bagIndex < 0 || bagIndex > 4) {
         LogStream ss; ss << "  Error: Invalid bag index " << bagIndex;
         LogMessage(ss.str());
         return 0;
    }
    if (slotIndex < 0) {
         LogStream ss; ss << "  Error: Invalid negative slot index " << slotIndex;
         LogMessage(ss.str());
        return 0;
    }

    uint64_t itemGuid = 0;
    try {
        // --- Logic based on reverse engineering retrievePlayerInventoryItemData & retrieveBagItemData ---

        void* containerPtr = nullptr; // This will hold BagObjectPtr or PlayerEquipmentAreaPtr
        int adjustedSlotIndex = slotIndex; // For bags 1-4, slotIndex is already 0-based. For backpack, it's 0-15.

        // Get Player Object (Needed for both paths)
        auto player = ObjectManager::GetInstance()->GetLocalPlayer();
        if (!player) {
            LogMessage("  Error: Could not get local player object.");
            return 0;
        }
        uintptr_t playerBase = reinterpret_cast<uintptr_t>(player->GetPointer());
        if (!playerBase) {
            LogMessage("  Error: Player object pointer is null.");
            return 0;
        }
        // LogStream ssPBase; ssPBase << "  Player Base: 0x" << std::hex << playerBase; LogMessage(ssPBase.str()); // Keep commented unless debugging

        // --- Start More Debug --- REMOVED GUID COMPARISON
        // ObjectManager* objMgr = ObjectManager::GetInstance();
        // WGUID guidFromMemory = {0, 0};
        // if (objMgr && objMgr->IsInitialized()) { // Check if ObjMgr is initialized
        //     ObjectManagerActual* gameObjMgrPtr = objMgr->GetInternalObjectManagerPtr();
        //     uintptr_t gameObjMgrPtrAddr = reinterpret_cast<uintptr_t>(gameObjMgrPtr);
        //     if (gameObjMgrPtrAddr) {
        //         try {
        //             // Read GUID directly from Game Object Manager structure memory
        //             guidFromMemory = MemoryReader::Read<WGUID>(gameObjMgrPtrAddr + LOCAL_GUID_OFFSET); // LOCAL_GUID_OFFSET = 0xC0
        //         } catch (const std::exception& e) { 
        //             LogStream ssMemErr; ssMemErr << "  GUID DEBUG: EXCEPTION reading GUID from memory: " << e.what(); 
        //             LogMessage(ssMemErr.str());
        //         } catch (...) { 
        //             LogMessage("  GUID DEBUG: UNKNOWN EXCEPTION reading GUID from memory.");
        //         }
        //     } else {
        //          LogMessage("  GUID DEBUG: Got NULL Internal Object Manager Pointer!");
        //     }
        // } else {
        //     LogMessage("  GUID DEBUG: ObjectManager instance or initialization check failed!");
        // }
        // 
        // WGUID guidFromObject = player->GetGUID(); // Get GUID from the WowPlayer object instance returned by GetLocalPlayer()
        //
        // LogStream ssGuidDebug;
        // ssGuidDebug << "  GUID DEBUG: From Mem (ObjMgrPtr+0xC0): 0x" << std::hex << GuidToUint64(guidFromMemory)
        //             << ", From WowPlayer Obj: 0x" << std::hex << GuidToUint64(guidFromObject);
        // if (guidFromMemory != guidFromObject) {
        //      ssGuidDebug << " <-- MISMATCH!";
        // }
        // LogMessage(ssGuidDebug.str());
        // --- End More Debug ---

        // --- Determine Container Pointer and Adjusted Slot ---
        if (bagIndex == 0) { // Backpack (Replicates retrievePlayerInventoryItemData logic for BagID 0)
            // For the main backpack (BagID 0), retrievePlayerInventoryItemData doesn't use PlayerObjectPtr + 0x18F0.
            // It calls getBagItem(0), findObjectByGuidAndFlags(bagGuid, 4), then the virtual call +0x28.
            
            // --- REVISED LOGIC: Read backpack slot directly from player base offset ---
            constexpr int BACKPACK_SLOTS = 16;
            if (adjustedSlotIndex < 0 || adjustedSlotIndex >= BACKPACK_SLOTS) {
                LogStream ss; ss << "  Error: Invalid backpack slot index " << adjustedSlotIndex;
                LogMessage(ss.str());
                return 0;
            }
            
            // Calculate offset for the specific slot
            uintptr_t itemGuidOffset = playerBase + OFF_BackpackStartOffset + (adjustedSlotIndex * sizeof(uint64_t)); 
            itemGuid = MemoryReader::Read<uint64_t>(itemGuidOffset);
            // LogStream ssItem; ssItem << "  Read backpack item GUID 0x" << std::hex << itemGuid << " from address 0x" << itemGuidOffset; LogMessage(ssItem.str());
            return itemGuid; // Return the directly read GUID
            // --- END REVISED LOGIC ---

            /* --- OLD LOGIC using FindObjectByGuidAndFlags (REMOVED) ---
            if (!GetBagItem || !FindObjectByGuidAndFlags) {
                LogMessage("  Error: GetBagItem or FindObjectByGuidAndFlags function pointer is null!");
                return 0;
            }

            uint64_t backpackContainerGuid64 = GetBagItem(0); // BagID 0 for backpack container itself
            if (backpackContainerGuid64 == 0) {
                LogMessage("  Error: GetBagItem(0) returned null GUID for backpack container.");
                return 0;
            }
             WGUID backpackContainerGuid;
             backpackContainerGuid.guid_low = (uint32_t)backpackContainerGuid64;
             backpackContainerGuid.guid_high = (uint32_t)(backpackContainerGuid64 >> 32);


            // --- DEBUG TEST --- // Test FindObjectByGuidAndFlags with GUID from GetLocalPlayer()
            WGUID playerGuidStruct = player->GetGUID(); // Get GUID from the player object (now sourced from native func)
            uint64_t playerGuid64 = GuidToUint64(playerGuidStruct);
            void* testPlayerObj = FindObjectByGuidAndFlags(playerGuidStruct.guid_low, playerGuidStruct.guid_high, 16); // Type 16 = Player
            if (testPlayerObj) {
                 LogStream ssTest; ssTest << "  DEBUG: FindObjectByGuidAndFlags successfully found player object (Obj GUID 0x" << std::hex << playerGuid64 << ") at address 0x" << reinterpret_cast<uintptr_t>(testPlayerObj);
                 LogMessage(ssTest.str());
                 // Compare testPlayerObj with player->GetPointer() - they should match if successful
                 if (reinterpret_cast<uintptr_t>(testPlayerObj) == playerBase) {
                     LogMessage("  DEBUG: Found player object address matches expected player base.");
                 } else {
                      LogMessage("  DEBUG WARNING: Found player object address MISMATCH!");
                 }
            } else {
                 LogStream ssTest; ssTest << "  DEBUG ERROR: FindObjectByGuidAndFlags FAILED to find player object (Obj GUID 0x" << std::hex << playerGuid64 << ", Type 16).";
                 LogMessage(ssTest.str());
            }
            // } // End of commented out outer else block from previous debug step
            // --- END DEBUG TEST ---


            void* bagObjectPtr = FindObjectByGuidAndFlags(backpackContainerGuid.guid_low, backpackContainerGuid.guid_high, 4); // Type 4 = Container
            if (!bagObjectPtr) {
                LogStream ss; ss << "  Error: FindObjectByGuidAndFlags failed to find backpack container object (GUID 0x" << std::hex << backpackContainerGuid64 << ").";
                LogMessage(ss.str());
                return 0;
            }

            // Replicate the virtual function call: result = (*(int (__thiscall **)(int))(*(_DWORD *)bagObjectPtr + 40))(bagObjectPtr);
            // This gets the pointer to the internal structure needed by retrieveBagItemData.
            uintptr_t bagObjectAddr = reinterpret_cast<uintptr_t>(bagObjectPtr);
            uintptr_t vtablePtr = MemoryReader::Read<uintptr_t>(bagObjectAddr);
            if (!vtablePtr) {
                LogMessage("  Error: Backpack container object has null vtable pointer.");
                return 0;
            }
            uintptr_t funcAddr = MemoryReader::Read<uintptr_t>(vtablePtr + 0x28); // Offset 0x28 for the virtual function
            if (!funcAddr) {
                 LogMessage("  Error: Backpack container vtable missing function at offset 0x28.");
                 return 0;
            }

            // Define the virtual function type and call it (__thiscall passes 'this' in ecx)
            typedef void* (__thiscall* GetInternalBagStructFn)(void* thisPtr);
            GetInternalBagStructFn getInternalStruct = reinterpret_cast<GetInternalBagStructFn>(funcAddr);

            // Call the function - the result is the pointer we need
            containerPtr = getInternalStruct(bagObjectPtr);

            if (!containerPtr) {
                 LogMessage("  Error: Failed to get internal bag structure pointer for backpack via virtual call.");
                 return 0;
            }
             // Backpack slot index is already 0-based (0-15), no adjustment needed here.
             if (adjustedSlotIndex >= 16) {
                 LogStream ss; ss << "  Error: Invalid backpack slot index " << adjustedSlotIndex;
                 LogMessage(ss.str());
                 return 0;
             }
            --- END OLD LOGIC --- */

        } else { // Equipped Bags (1-4) (Replicates logic from old function, but using FindObjectByGuidAndFlags)
            uintptr_t bagGuidAddress = playerBase + OFF_PlayerBagGuidsStart + ((bagIndex - 1) * 8);
            uint64_t equippedContainerGuid64 = MemoryReader::Read<uint64_t>(bagGuidAddress);
            if (equippedContainerGuid64 == 0) {
                LogStream ss; ss << "  Warning: No container GUID found for bag index " << bagIndex;
                LogMessage(ss.str());
                return 0; // No bag equipped
            }

             WGUID equippedContainerGuid;
             equippedContainerGuid.guid_low = (uint32_t)equippedContainerGuid64;
             equippedContainerGuid.guid_high = (uint32_t)(equippedContainerGuid64 >> 32);

            void* bagObjectPtr = FindObjectByGuidAndFlags(equippedContainerGuid.guid_low, equippedContainerGuid.guid_high, 4); // Type 4 = Container
             if (!bagObjectPtr) {
                 LogStream ss; ss << "  Error: FindObjectByGuidAndFlags failed to find equipped bag object (GUID 0x" << std::hex << equippedContainerGuid64 << ", Bag " << std::dec << bagIndex << ").";
                 LogMessage(ss.str());
                 return 0;
             }

             // Get the internal structure pointer via the virtual function call
             uintptr_t bagObjectAddr = reinterpret_cast<uintptr_t>(bagObjectPtr);
             uintptr_t vtablePtr = MemoryReader::Read<uintptr_t>(bagObjectAddr);
             if (!vtablePtr) {
                 LogStream ss; ss << "  Error: Equipped bag (GUID 0x" << std::hex << equippedContainerGuid64 << ") has null vtable pointer.";
                 LogMessage(ss.str());
                 return 0;
             }
             uintptr_t funcAddr = MemoryReader::Read<uintptr_t>(vtablePtr + 0x28);
             if (!funcAddr) {
                 LogStream ss; ss << "  Error: Equipped bag (GUID 0x" << std::hex << equippedContainerGuid64 << ") vtable missing function at offset 0x28.";
                 LogMessage(ss.str());
                 return 0;
             }
             typedef void* (__thiscall* GetInternalBagStructFn)(void* thisPtr);
             GetInternalBagStructFn getInternalStruct = reinterpret_cast<GetInternalBagStructFn>(funcAddr);
             containerPtr = getInternalStruct(bagObjectPtr);

             if (!containerPtr) {
                  LogStream ss; ss << "  Error: Failed to get internal bag structure pointer for equipped bag (GUID 0x" << std::hex << equippedContainerGuid64 << ") via virtual call.";
                  LogMessage(ss.str());
                  return 0;
             }
             // Equipped bag slot index is already 0-based, no adjustment needed.
        }

        // --- At this point, containerPtr should point to the internal Bag structure ---
        if (!containerPtr) {
            LogMessage("  Error: Failed to determine valid container pointer.");
            return 0;
        }

        // --- Call retrieveBagItemData ---
        // retrieveBagItemData(BagObjectPtr ecx, SlotID stack) -> returns ItemStructPtr (eax)
        // We need to use assembly to call it correctly, setting ecx and pushing the arg.

        void* itemStructPtr = nullptr;
        uintptr_t contPtrAddr = reinterpret_cast<uintptr_t>(containerPtr); // The pointer to pass in ECX
        int slotIdArg = adjustedSlotIndex; // The argument to push on the stack

        // Use naked function or inline assembly to call retrieveBagItemData
        __asm {
            mov ecx, contPtrAddr     // Load containerPtr into ECX
            push slotIdArg           // Push slotIndex onto the stack
            call retrieveBagItemDataAddr // Call the function
            add esp, 4               // Clean up stack argument
            mov itemStructPtr, eax   // Store the returned ItemStructPtr in our variable
        }

        if (!itemStructPtr) {
            // This is normal for an empty slot, don't log error unless debugging
            // LogStream ss; ss << "  Info: retrieveBagItemData returned null for Bag " << bagIndex << ", Slot " << adjustedSlotIndex << ". Slot is likely empty."; LogMessage(ss.str());
            return 0; // Slot is empty
        }

        // --- Read GUID from ItemStructPtr ---
        // ItemStructPtr -> +0x8 = ItemDescriptorPtr
        // ItemDescriptorPtr -> +0x0 = GuidLow
        // ItemDescriptorPtr -> +0xC = GuidHigh

        uintptr_t itemStructAddr = reinterpret_cast<uintptr_t>(itemStructPtr);
        uintptr_t descriptorPtr = MemoryReader::Read<uintptr_t>(itemStructAddr + OFF_ItemStruct_DescriptorPtr);
        if (!descriptorPtr) {
            LogStream ss; ss << "  Error: ItemStruct at 0x" << std::hex << itemStructAddr << " has null descriptor pointer (offset 0x8). Bag " << std::dec << bagIndex << ", Slot " << adjustedSlotIndex;
            LogMessage(ss.str());
            return 0;
        }

        uint32_t guidLow = MemoryReader::Read<uint32_t>(descriptorPtr + OFF_ItemDescriptor_GuidLow);
        uint32_t guidHigh = MemoryReader::Read<uint32_t>(descriptorPtr + OFF_ItemDescriptor_GuidHigh);

        itemGuid = ((uint64_t)guidHigh << 32) | guidLow;

    } catch (const std::exception& e) {
        LogStream ssErr; ssErr << "GetItemGuidInSlot EXCEPTION for Bag " << bagIndex << ", Slot " << slotIndex << ": " << e.what();
        LogMessage(ssErr.str());
        return 0;
    } catch (...) {
        LogStream ssErr; ssErr << "GetItemGuidInSlot UNKNOWN EXCEPTION for Bag " << bagIndex << ", Slot " << slotIndex;
        LogMessage(ssErr.str());
        return 0;
    }

    // LogStream ssResult; ssResult << std::hex << std::setw(16) << std::setfill('0') << itemGuid; // Keep commented unless debugging
    // LogMessage(std::string("  Returned GUID: 0x") + ssResult.str());

    return itemGuid;
}