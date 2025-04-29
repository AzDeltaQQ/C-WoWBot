#pragma once

#include <windows.h>
#include <cstdint> // Required for uint64_t
#include <vector> // Added for std::vector
#include "wowobject.h"       // Include WGUID and Vector3 definition

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
extern GetLocalPlayerGuidFn GetLocalPlayerGuid; // This is the pointer variable
const DWORD OFF_GetLocalPlayerGuid = 0x004D3790;

// ApplySpellEffectsIfReady Offset (Used as callback argument)
const DWORD OFF_ApplySpellEffects = 0x0080ABE0;

// Initialization function declarations
void InitializeFunctions(); // Removed baseAddress parameter
void Initialize(uintptr_t clientConnectionAddr, uintptr_t funcAddr);
void ReadSpellbook();
const std::vector<int>& GetSpellbook();

// Function to select a target by GUID
typedef void(__cdecl* TargetUnitByGuidFn)(uint64_t guid);
const TargetUnitByGuidFn TargetUnitByGuid = (TargetUnitByGuidFn)0x00524BF0; // Updated Address

// Inventory/Bag Functions
int GetFreeBagSlots();
bool IsBagSlotEmpty(int bagIndex, int slotIndex);
int FindFirstEmptyBagSlot(int* bagIndex, int* slotIndex);

// --- Item Quality Enum ---
enum class ItemQuality {
    POOR = 0,      // Grey
    COMMON,        // White
    UNCOMMON,      // Green
    RARE,          // Blue
    EPIC,          // Purple
    LEGENDARY,     // Orange
    ARTIFACT,      // Heirloom/Gold
    UNKNOWN = -1   // Error or not found
};

// --- Item Cache Interaction ---
// Signature adapted from C# delegate: ItemCacheGetRowDelegate(IntPtr ptr, int itemId, IntPtr unknown, int unused1, int unused2, char unused3)
typedef uintptr_t (__cdecl* GetItemCacheEntryFn)(uintptr_t basePtr, int itemId, void* unknownPtr, int unused1, int unused2, char unused3);
extern GetItemCacheEntryFn GetItemCacheEntry;
const DWORD OFF_GetItemCacheEntry = 0x0067CA30; // Corrected Address from BloogBot (WotLK)
const uintptr_t BASE_ItemCache = 0x00C5D828; // Address from C# MemoryAddresses (WotLK)

// --- Selling Items ---
// Updated signature to match disassembly AND call order from handleContainerItemUse:
// Vendor GUID first, then Item GUID, then the final (likely count or flag) parameter.
typedef void(__cdecl* SellItemByGuidFn)(int vendorGuidLow, int vendorGuidHigh, int itemGuidLow, int itemGuidHigh, int unknownParam);
extern SellItemByGuidFn SellItemByGuid;
#define OFF_SellItemByGuid 0x006D2D40 // From IDA

// --- Client-Side Item Processing ---
// Called after item actions (sell, use, equip) to update client state/UI
// Address found via handleContainerItemUse analysis
typedef int(__cdecl* RetrieveAndProcessClientObjectFn)(int itemGuidLow, int itemGuidHigh);
extern RetrieveAndProcessClientObjectFn RetrieveAndProcessClientObject;
#define OFF_RetrieveAndProcessClientObject 0x00513740 

// --- UI Interaction ---
// Placeholder for Lua execution or memory check
#define OFF_MerchantFrame_IsVisible 0xDEADBEEF // Placeholder - Needs actual address or Lua implementation

// --- Function Declarations ---
ItemQuality GetItemQuality(int bagIndex, int slotIndex);
int GetContainerNumSlots(int bagIndex);
uint64_t GetItemGuidInSlot(int bagIndex, int slotIndex);
bool IsVendorWindowOpen();
void SellItem(uint64_t vendorGuid, int bagIndex, int slotIndex);

// --- Offsets ---
// Offset for item ID within item object struct (WoWObject + offset)
constexpr uintptr_t OFF_ItemId = 0xC; // Verified via IDA getItemId
// Offset for quality within ItemCache entry (ItemCacheEntry + offset)
constexpr uintptr_t OFF_ItemQuality = 0x4C; // Updated based on BloogBot C# ItemCacheEntryOffsets.Quality

// Add other non-cooldown related function declarations here if any

// Alternative based on enumVisibleObjects:
// typedef int (__cdecl *EnumVisibleObjectsFn)(int (__cdecl *callback)(uint64_t, void*), void* filter);
// const EnumVisibleObjectsFn EnumVisibleObjects = (EnumVisibleObjectsFn)0x005203EE;
// Need to define the callback function select_nearest_suitable_target separately if using this.

// --- Function Pointer Typedefs ---
typedef void (__stdcall* HandleTerrainClickFn)(int actionType, Vector3* position, uint64_t guid);

// --- Global Function Pointers ---
extern CastLocalPlayerSpellFn CastLocalPlayerSpell;
extern GetLocalPlayerGuidFn GetLocalPlayerGuid;
extern GetItemCacheEntryFn GetItemCacheEntry;
extern SellItemByGuidFn SellItemByGuid; // Declaration using the updated signature
extern RetrieveAndProcessClientObjectFn RetrieveAndProcessClientObject;
extern HandleTerrainClickFn HandleTerrainClick;

// --- Game Functions ---
// ... existing functions ...
ItemQuality GetItemQuality(int bagIndex, int slotIndex);
WGUID FindUnitByGuid(uint64_t guid);
bool IsVendorWindowOpen(); 
void CloseVendorWindow(); 
void RepairAllItems(); 
// Add declaration for SellItemByGuid (using the pointer directly is also an option)
// void SellItemByGuid_Wrapper(uint64_t itemGuid, uint64_t vendorGuid, int count); 

// --- Item Cache Offsets ---