#include "wowobject.h"
#include <Windows.h>
#include <stdexcept> // For exception handling
#include <chrono> // For basic throttling timestamp
#include <sstream>
#include "log.h" // Include for LogStream and LogMessage
#include "objectmanager.h" // Include ObjectManager to get local player GUID
#include "functions.h" // Include for GetLocalPlayerGuid

// --- Offsets (Assuming these are defined correctly elsewhere or here) ---
// Object Base Relative
constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C; // Standard X offset
constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798; // Standard Y offset
constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0; // Z seems correct
constexpr DWORD OBJECT_ROTATION_OFFSET = 0x7A8;
constexpr DWORD OBJECT_UNIT_FIELDS_PTR_OFFSET = 0x8;
constexpr DWORD OBJECT_DESCRIPTOR_PTR_OFFSET = 0x8; // Same as UnitFields for descriptor
constexpr DWORD OBJECT_CASTING_ID_OFFSET = 0xC08; // Use verified offset
constexpr DWORD OBJECT_CHANNEL_ID_OFFSET = 0xC20; // Use verified offset
// Unit Fields Relative (Multiply index by 4 is already done in defines)
constexpr DWORD UNIT_FIELD_HEALTH_OFFSET = 0x18 * 4;
constexpr DWORD UNIT_FIELD_MAXHEALTH_OFFSET = 0x20 * 4;
constexpr DWORD UNIT_FIELD_LEVEL_OFFSET = 0x36 * 4;
constexpr DWORD UNIT_FIELD_FLAGS_OFFSET = 0x3B * 4;
constexpr DWORD UNIT_FIELD_POWTYPE_OFFSET = 0x5F; // Offset of Power Type within UNIT_FIELD_BYTES_0 (0x17*4 + 3)
// Add Dynamic Flags offset (WotLK 3.3.5)
constexpr DWORD UNIT_FIELD_DYNAMIC_FLAGS_OFFSET = 0x13C; // 0x4F*4 = 316 = 0x13C
// Power offsets (relative to UnitFields base)
constexpr DWORD UNIT_FIELD_POWER_BASE = 0x19 * 4; // Was 0x4C, enum UNIT_FIELD_POWER1 = 0x19
constexpr DWORD UNIT_FIELD_MAXPOWER_BASE = 0x21 * 4; // Was 0x6C, enum UNIT_FIELD_MAXPOWER1 = 0x21

// VFTable indices (keep as before)
// Note: These are only used for GetName, GetScale, Interact, GetQuestStatus currently
enum VFTableIndex {
    VF_GetBagPtr = 10,
    VF_GetPosition = 12, // Not used directly anymore
    VF_GetFacing = 14,   // Not used directly anymore
    VF_GetScale = 15,
    VF_GetQuestStatus = 22,
    VF_GetModel = 24,
    VF_Interact = 44,
    VF_GetName = 54
};

// --- Base WowObject Implementation ---

// Constructor
WowObject::WowObject(void* ptr, WGUID guid, WowObjectType type)
    : m_pointer(ptr), m_guid(guid), m_type(type) {
    // Initialize cached values to defaults
    m_cachedRotation = 0.0f;
    m_cachedScale = 1.0f;
    m_lastUpdateTime = 0;
    // Call initial update for basic info like name/scale if pointer is valid
    if (m_pointer) {
        // Read name immediately if possible
        m_cachedName = ReadNameFromVTable();
        // Read initial scale via VTable
        try {
            typedef float (__thiscall* GetScaleFunc)(void* thisptr);
            void** vftable = MemoryReader::Read<void**>(reinterpret_cast<uintptr_t>(m_pointer));
            if (vftable) {
                 // Read function pointer from VTable address
                 uintptr_t funcAddr = MemoryReader::Read<uintptr_t>(reinterpret_cast<uintptr_t>(&vftable[VFTableIndex::VF_GetScale]));
                 if(funcAddr) {
                    GetScaleFunc func = reinterpret_cast<GetScaleFunc>(funcAddr);
                    m_cachedScale = func(m_pointer);
                 }
            }
        } catch (...) { /* Ignore scale read error */ }
    }
}

// Helper to read name via VTable
std::string WowObject::ReadNameFromVTable() {
    if (!m_pointer) return "";
    try {
        typedef char* (__thiscall* GetNameFunc)(void* thisptr);
        // Read VTable pointer from object base
        void** vftable = MemoryReader::Read<void**>(reinterpret_cast<uintptr_t>(m_pointer));
        if (!vftable) return "[Error VTable Null]";
        
        // Read function pointer from VTable address
        uintptr_t funcAddr = MemoryReader::Read<uintptr_t>(reinterpret_cast<uintptr_t>(&vftable[VFTableIndex::VF_GetName]));
        if (!funcAddr) return "[Error Func Null]";
        
        GetNameFunc func = reinterpret_cast<GetNameFunc>(funcAddr);
        
        // Read the pointer returned by the game function
        char* namePtr = func(m_pointer); 
        if (!namePtr) return ""; // Empty name is valid

        // Read the string content from the pointer received
        std::string nameStr;
        uintptr_t currentAddr = reinterpret_cast<uintptr_t>(namePtr);
        // Protect against reading invalid memory returned by GetName
        // Check if the pointer is within a reasonable range if possible,
        // or just limit length and catch exceptions.
        size_t count = 0;
        constexpr size_t MAX_NAME_LEN = 100; 
        while(count < static_cast<size_t>(MAX_NAME_LEN)) {
            char c = MemoryReader::Read<char>(currentAddr + count);
            if (c == '\0') break;
            nameStr += c;
            count++;
        }
        // If loop finished due to length, the name might be truncated or invalid
        if (count == MAX_NAME_LEN) return "[Error Name Too Long/Invalid]"; 

        return nameStr;        

    } catch (...) {
        return "[Error Reading Name]";
    }
}

// Update dynamic data for base object
void WowObject::UpdateDynamicData() {
    if (!m_pointer) return;

    // --- Throttling ---
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    // Allow update if never updated, or enough time passed
    if (m_lastUpdateTime != 0 && static_cast<uint64_t>(now_ms) < (m_lastUpdateTime + 100LL)) { // 100ms throttle (use LL for literal type match)
        return; 
    }

    // --- Read Data --- 
    try {
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(m_pointer);

        // Revert back to reading position via direct offsets
        m_cachedPosition.x = MemoryReader::Read<float>(baseAddr + OBJECT_POS_X_OFFSET);
        m_cachedPosition.y = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Y_OFFSET);
        m_cachedPosition.z = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Z_OFFSET);

        // Keep reading rotation directly via offset
        m_cachedRotation = MemoryReader::Read<float>(baseAddr + OBJECT_ROTATION_OFFSET);
        
        // Potentially update Scale here too if needed, maybe less frequently
    } catch (...) {
        // Handle potential exceptions during memory read
        m_cachedPosition = {0,0,0};
        m_cachedRotation = 0;
    }
    m_lastUpdateTime = now_ms; // Update timestamp *after* successful read attempt (or after catch)
}

// GetName returns cached name, attempts read if cache is empty
std::string WowObject::GetName() {
    // Name is read once in constructor and cached.
    // If reading failed initially, it will remain as error/empty.
    // We don't re-attempt reading name here frequently as it involves VTable calls.
    if (!m_pointer) return "";
    return m_cachedName; 
}

// Interact still likely needs direct VTable call
void WowObject::Interact() {
    if (!m_pointer) return;
    try {
        typedef void (__thiscall* InteractFunc)(void* thisptr);
        void** vftable = MemoryReader::Read<void**>(reinterpret_cast<uintptr_t>(m_pointer));
        if (!vftable) return;
        
        uintptr_t funcAddr = MemoryReader::Read<uintptr_t>(reinterpret_cast<uintptr_t>(&vftable[VFTableIndex::VF_Interact]));
        if (!funcAddr) return;

        InteractFunc func = reinterpret_cast<InteractFunc>(funcAddr);
        func(m_pointer); 
    } catch (...) {
        return;
    }
}


// --- WowUnit Implementation ---

// Constructor
WowUnit::WowUnit(void* ptr, WGUID guid) : WowObject(ptr, guid, OBJECT_UNIT) {}

// Implement IsLootable
bool WowUnit::IsLootable() {
    if (!m_pointer) return false;
    try {
        uintptr_t unitFieldsPtrAddr = reinterpret_cast<uintptr_t>(m_pointer) + OBJECT_UNIT_FIELDS_PTR_OFFSET;
        uintptr_t unitFieldsPtr = MemoryReader::Read<uintptr_t>(unitFieldsPtrAddr);
        if (!unitFieldsPtr) return false;

        uint32_t dynamicFlags = MemoryReader::Read<uint32_t>(unitFieldsPtr + UNIT_FIELD_DYNAMIC_FLAGS_OFFSET);
        constexpr uint32_t UNIT_DYNFLAG_LOOTABLE = 0x8;
        return (dynamicFlags & UNIT_DYNFLAG_LOOTABLE) != 0;
    } catch (...) {
        LogStream ssErr; ssErr << "!!! IsLootable FAILED !!!"; LogMessage(ssErr.str());
        return false;
    }
}

// Update dynamic data for Unit
void WowUnit::UpdateDynamicData() {
    // Call base class update first
    WowObject::UpdateDynamicData(); 

    // Ensure pointer is still valid
    if (!m_pointer) return; 

    // Get the actual local player GUID (still useful for rage division later)
    ObjectManager* objMgr = ObjectManager::GetInstance();
    // WGUID localPlayerGuid = objMgr->IsInitialized() ? objMgr->GetLocalPlayerGUID() : WGUID{0, 0}; // OLD DEPRECATED
    uint64_t localPlayerGuid64 = 0;
    if (objMgr->IsInitialized() && GetLocalPlayerGuid) {
         localPlayerGuid64 = GetLocalPlayerGuid(); // Use global func ptr
    }

    // bool isPlayerObject = (GuidToUint64(m_guid) == localPlayerGuid64 && localPlayerGuid64 != 0);

    // --- Read Unit Specific Data --- 
    try {
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(m_pointer);
        uintptr_t unitFieldsPtrAddr = baseAddr + OBJECT_UNIT_FIELDS_PTR_OFFSET;
        uintptr_t unitFieldsPtr = MemoryReader::Read<uintptr_t>(unitFieldsPtrAddr);
        uintptr_t descriptorPtrAddr = baseAddr + OBJECT_DESCRIPTOR_PTR_OFFSET;
        uintptr_t descriptorPtr = MemoryReader::Read<uintptr_t>(descriptorPtrAddr);

        // Try reading UnitFields data IF the pointer is valid
        if (unitFieldsPtr) { 
            // Read core unit fields using UnitFields offsets
            m_cachedHealth = 0; m_cachedMaxHealth = 0; m_cachedLevel = 0; m_cachedUnitFlags = 0; // Reset before reading
            try {
                m_cachedHealth = MemoryReader::Read<int>(unitFieldsPtr + UNIT_FIELD_HEALTH_OFFSET);
                m_cachedMaxHealth = MemoryReader::Read<int>(unitFieldsPtr + UNIT_FIELD_MAXHEALTH_OFFSET);
                m_cachedLevel = MemoryReader::Read<int>(unitFieldsPtr + UNIT_FIELD_LEVEL_OFFSET);
                m_cachedUnitFlags = MemoryReader::Read<uint32_t>(unitFieldsPtr + UNIT_FIELD_FLAGS_OFFSET);
            } catch (const std::exception& /*e*/) {
            } catch (...) {
            }

            // --- Read Power Type (Combined Logic with Debugging) ---

            uint8_t rawPowerType = 0xFF; // Default to invalid
            try { // Try reading power type from UNIT_FIELD_BYTES_0 (Byte 3)
                 uintptr_t bytes0Addr = unitFieldsPtr + (0x17*4); 
                 uint32_t bytes0Val = MemoryReader::Read<uint32_t>(bytes0Addr);
                 rawPowerType = (bytes0Val >> 24) & 0xFF; 
            } catch (...) {
            }

            // Fallback to descriptor if Bytes0 read failed or seems invalid
            if (rawPowerType > 6) {
                if (descriptorPtr) {
                     try { 
                          uintptr_t descriptorPowerTypeAddr = descriptorPtr + 0x47; 
                          rawPowerType = MemoryReader::Read<uint8_t>(descriptorPowerTypeAddr);
                     } catch (...) {
                          rawPowerType = 0xFF;
                     }
                } else {
                     rawPowerType = 0xFF;
                }
            }
            // Validate final rawPowerType and cache it
            if (rawPowerType <= 6) { m_cachedPowerType = rawPowerType; }
            else { 
                     m_cachedPowerType = 0; 
            }
            

            // Read Power/MaxPower based on determined type using standard array offsets
            DWORD powerOffset = UNIT_FIELD_POWER_BASE + (m_cachedPowerType * 4);
            DWORD maxPowerOffset = UNIT_FIELD_MAXPOWER_BASE + (m_cachedPowerType * 4);
            

            int rawPower = 0; int rawMaxPower = 0;
            try { 
                rawPower = MemoryReader::Read<int>(unitFieldsPtr + powerOffset);
                rawMaxPower = MemoryReader::Read<int>(unitFieldsPtr + maxPowerOffset);
            } catch(...) { 
            }
            m_cachedPower = rawPower; m_cachedMaxPower = rawMaxPower;


            // Read casting/channeling from object base offsets (independent of UnitFields)
            try { 
                 m_cachedCastingSpellId = MemoryReader::Read<uint32_t>(baseAddr + OBJECT_CASTING_ID_OFFSET);
                 m_cachedChannelSpellId = MemoryReader::Read<uint32_t>(baseAddr + OBJECT_CHANNEL_ID_OFFSET);
            } catch (...) {
                m_cachedCastingSpellId = 0; m_cachedChannelSpellId = 0;
            }

        } else { // Failed to read UnitFields pointer
            // Clear relevant data if UF pointer is bad
            m_cachedHealth = 0; m_cachedMaxHealth = 0; m_cachedLevel = 0;
            m_cachedUnitFlags = 0; m_cachedPower = 0; m_cachedMaxPower = 0;
            m_cachedPowerType = 0; m_cachedCastingSpellId = 0; m_cachedChannelSpellId = 0;
        }

    } catch (const std::exception& /*e*/) {
        // Clear all fields on outer exception
        m_cachedHealth = 0; m_cachedMaxHealth = 0; m_cachedLevel = 0; m_cachedUnitFlags = 0;
        m_cachedPower = 0; m_cachedMaxPower = 0; m_cachedPowerType = 0;
        m_cachedCastingSpellId = 0; m_cachedChannelSpellId = 0;
    } catch (...) {
        // Clear all fields
         m_cachedHealth = 0; m_cachedMaxHealth = 0; m_cachedLevel = 0; m_cachedUnitFlags = 0;
        m_cachedPower = 0; m_cachedMaxPower = 0; m_cachedPowerType = 0;
        m_cachedCastingSpellId = 0; m_cachedChannelSpellId = 0;
    }
}

// Helper to get power type as string
std::string WowUnit::GetPowerTypeString() const {
    switch (m_cachedPowerType) {
        case 0: return "Mana";
        case 1: return "Rage";
        case 2: return "Focus";
        case 3: return "Energy";
        case 4: return "Happiness"; // Pet specific
        case 5: return "Runes"; // DK Resource - needs special handling usually
        case 6: return "Runic Power"; // DK Power Bar
        default: return "Unknown";
    }
}

// --- WowPlayer Implementation ---

WowPlayer::WowPlayer(void* ptr, WGUID guid) : WowUnit(ptr, guid) {
    m_type = OBJECT_PLAYER;
}

std::string WowPlayer::GetClass() {
    // Needs implementation using ReadUnitField and UNIT_FIELD_BYTES_0
    return "UnknownClass";
}

// --- WowGameObject Implementation ---

WowGameObject::WowGameObject(void* ptr, WGUID guid) : WowObject(ptr, guid, OBJECT_GAMEOBJECT) {}

// Override UpdateDynamicData for GameObjects to read position from raw offsets
void WowGameObject::UpdateDynamicData() {
    // Call base first for throttling and common data (e.g., rotation from offset)
    WowObject::UpdateDynamicData(); 

    // Check if base class actually updated (based on timestamp comparison)
    auto now_ms_check = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    bool base_updated = (m_lastUpdateTime == now_ms_check);

    if (!base_updated) {
       return; // Base class was throttled or failed, so we skip too
    }
    // Ensure pointer is still valid 
    if (!m_pointer) return; 

    // --- Read GameObject Position from raw offsets 0xE8, 0xEC, 0xF0 --- 
    try {
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(m_pointer);
        constexpr DWORD GO_RAW_POS_Y_OFFSET = 0xE8; // Swapped: Offset 0xE8 seems to hold Y
        constexpr DWORD GO_RAW_POS_X_OFFSET = 0xEC; // Swapped: Offset 0xEC seems to hold X
        constexpr DWORD GO_RAW_POS_Z_OFFSET = 0xF0;

        // Assign to correct members based on observed order
        m_cachedPosition.x = MemoryReader::Read<float>(baseAddr + GO_RAW_POS_X_OFFSET); // Read X from 0xEC
        m_cachedPosition.y = MemoryReader::Read<float>(baseAddr + GO_RAW_POS_Y_OFFSET); // Read Y from 0xE8
        m_cachedPosition.z = MemoryReader::Read<float>(baseAddr + GO_RAW_POS_Z_OFFSET); // Z remains 0xF0
    } catch (...) {
    }
}

// Example of keeping VTable call for specific functions
int WowGameObject::GetQuestStatus() {
    if (!m_pointer) return 0;
    try {
        typedef int (__thiscall* GetQuestStatusFunc)(void* thisptr);
        void** vftable = MemoryReader::Read<void**>(reinterpret_cast<uintptr_t>(m_pointer));
         if (!vftable) return 0;

        // Use the ENUM index for GetQuestStatus
        uintptr_t funcAddr = MemoryReader::Read<uintptr_t>(reinterpret_cast<uintptr_t>(&vftable[VFTableIndex::VF_GetQuestStatus])); 
         if (!funcAddr) return 0;

        GetQuestStatusFunc func = reinterpret_cast<GetQuestStatusFunc>(funcAddr);
        return func(m_pointer);
    } catch (...) {
        return 0;
    }
}

// --- WowContainer Implementation ---

WowContainer::WowContainer(void* ptr, WGUID guid)
    : WowObject(ptr, guid, OBJECT_CONTAINER) {
    // Optional: Add any container-specific initialization here
    // For example, reading initial slot count or item data if desired
} 