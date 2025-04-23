#include "wowobject.h"
#include <Windows.h>
#include <stdexcept> // For exception handling
#include <chrono> // For basic throttling timestamp

// --- Offsets (Assuming these are defined correctly elsewhere or here) ---
// Object Base Relative
constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C;
constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798;
constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0;
constexpr DWORD OBJECT_ROTATION_OFFSET = 0x7A8;
constexpr DWORD OBJECT_UNIT_FIELDS_PTR_OFFSET = 0x8;
constexpr DWORD OBJECT_DESCRIPTOR_PTR_OFFSET = 0x8; // Same as UnitFields for descriptor
constexpr DWORD OBJECT_CASTING_ID_OFFSET = 0xC08; // Use verified offset
constexpr DWORD OBJECT_CHANNEL_ID_OFFSET = 0xC20; // Use verified offset
// Unit Fields Relative (Multiply index by 4 is already done in defines)
constexpr DWORD UNIT_FIELD_HEALTH_OFFSET = 0x18 * 4;
constexpr DWORD UNIT_FIELD_MAXHEALTH_OFFSET = 0x20 * 4;
constexpr DWORD UNIT_FIELD_LEVEL_OFFSET = 0x36 * 4;
constexpr DWORD UNIT_FIELD_FLAGS_OFFSET = 0xEC;
constexpr DWORD UNIT_FIELD_POWER_TYPE_FROM_DESCRIPTOR = 0x47;
// Power offsets (relative to UnitFields base)
constexpr DWORD UNIT_FIELD_POWER_BASE = 0x4C; // UNIT_FIELD_POWERS start
constexpr DWORD UNIT_FIELD_MAXPOWER_BASE = 0x6C; // UNIT_FIELD_MAXPOWERS start

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
            void** vftable = ReadMemory<void**>(reinterpret_cast<uintptr_t>(m_pointer));
            if (vftable) {
                 // Read function pointer from VTable address
                 uintptr_t funcAddr = ReadMemory<uintptr_t>(reinterpret_cast<uintptr_t>(&vftable[VFTableIndex::VF_GetScale]));
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
        void** vftable = ReadMemory<void**>(reinterpret_cast<uintptr_t>(m_pointer));
        if (!vftable) return "[Error VTable Null]";
        
        // Read function pointer from VTable address
        uintptr_t funcAddr = ReadMemory<uintptr_t>(reinterpret_cast<uintptr_t>(&vftable[VFTableIndex::VF_GetName]));
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
        while(count < MAX_NAME_LEN) {
            char c = ReadMemory<char>(currentAddr + count);
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
    if (m_lastUpdateTime != 0 && now_ms < m_lastUpdateTime + 100) { // 100ms throttle
        return; 
    }

    // --- Read Data --- 
    try {
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(m_pointer);

        // Revert back to reading position via direct offsets
        m_cachedPosition.x = ReadMemory<float>(baseAddr + OBJECT_POS_X_OFFSET);
        m_cachedPosition.y = ReadMemory<float>(baseAddr + OBJECT_POS_Y_OFFSET);
        m_cachedPosition.z = ReadMemory<float>(baseAddr + OBJECT_POS_Z_OFFSET);

        // Keep reading rotation directly via offset
        m_cachedRotation = ReadMemory<float>(baseAddr + OBJECT_ROTATION_OFFSET);
        
        // Potentially update Scale here too if needed, maybe less frequently
        // m_cachedScale = ReadMemory<float>(baseAddr + ...); 
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
bool WowObject::Interact() {
    if (!m_pointer) return false;
    try {
        typedef bool (__thiscall* InteractFunc)(void* thisptr);
        void** vftable = ReadMemory<void**>(reinterpret_cast<uintptr_t>(m_pointer));
        if (!vftable) return false;
        
        uintptr_t funcAddr = ReadMemory<uintptr_t>(reinterpret_cast<uintptr_t>(&vftable[VFTableIndex::VF_Interact]));
        if (!funcAddr) return false;

        InteractFunc func = reinterpret_cast<InteractFunc>(funcAddr);
        return func(m_pointer);
    } catch (...) {
        return false;
    }
}


// --- WowUnit Implementation ---

// Constructor
WowUnit::WowUnit(void* ptr, WGUID guid) : WowObject(ptr, guid, OBJECT_UNIT) {}

// Helper to read from UnitFields safely
template <typename T>
T WowUnit::ReadUnitField(DWORD valueOffset) {
    // This helper reads relative to the UnitFields pointer
    if (!m_pointer) return T{};
    try {
        uintptr_t unitFieldsPtrAddr = reinterpret_cast<uintptr_t>(m_pointer) + OBJECT_UNIT_FIELDS_PTR_OFFSET;
        uintptr_t unitFieldsPtr = ReadMemory<uintptr_t>(unitFieldsPtrAddr);
        if (!unitFieldsPtr) return T{};
        return ReadMemory<T>(unitFieldsPtr + valueOffset);
    } catch (...) {
        return T{}; // Return default value on error
    }
}

// Update dynamic data for Unit
void WowUnit::UpdateDynamicData() {
    // Call base class update first for Pos/Rot etc. and throttling
    WowObject::UpdateDynamicData(); 

    // Check if base class actually updated (based on timestamp)
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (m_lastUpdateTime != now_ms) { 
       return; // Base class was throttled, so we skip too
    }
    // Ensure pointer is still valid after base call might have returned early
    if (!m_pointer) return; 

    // --- Read Unit Specific Data --- 
    try {
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(m_pointer);
        uintptr_t unitFieldsPtrAddr = baseAddr + OBJECT_UNIT_FIELDS_PTR_OFFSET;
        uintptr_t unitFieldsPtr = ReadMemory<uintptr_t>(unitFieldsPtrAddr);
        uintptr_t descriptorPtrAddr = baseAddr + OBJECT_DESCRIPTOR_PTR_OFFSET;
        uintptr_t descriptorPtr = ReadMemory<uintptr_t>(descriptorPtrAddr);

        if (unitFieldsPtr) {
            // Read core unit fields
            m_cachedHealth = ReadMemory<int>(unitFieldsPtr + UNIT_FIELD_HEALTH_OFFSET);
            m_cachedMaxHealth = ReadMemory<int>(unitFieldsPtr + UNIT_FIELD_MAXHEALTH_OFFSET);
            m_cachedLevel = ReadMemory<int>(unitFieldsPtr + UNIT_FIELD_LEVEL_OFFSET);
            m_cachedUnitFlags = ReadMemory<uint32_t>(unitFieldsPtr + UNIT_FIELD_FLAGS_OFFSET);

            // Read Power Type (prefer descriptor)
            if (descriptorPtr) {
                 m_cachedPowerType = ReadMemory<uint8_t>(descriptorPtr + UNIT_FIELD_POWER_TYPE_FROM_DESCRIPTOR);
                 // Basic sanity check on power type (0-6 are known valid types)
                 if (m_cachedPowerType > 6) m_cachedPowerType = 0; 
            } else {
                 m_cachedPowerType = 0; // Default to Mana if no descriptor
            }

            // Read Power/MaxPower based on type using array offsets
            DWORD powerOffset = UNIT_FIELD_POWER_BASE + (m_cachedPowerType * 4);
            DWORD maxPowerOffset = UNIT_FIELD_MAXPOWER_BASE + (m_cachedPowerType * 4);

            m_cachedPower = ReadMemory<int>(unitFieldsPtr + powerOffset);
            m_cachedMaxPower = ReadMemory<int>(unitFieldsPtr + maxPowerOffset);
            
        } else {
            // Failed to read UnitFields pointer, clear related data
            m_cachedHealth = 0; m_cachedMaxHealth = 0; m_cachedLevel = 0;
            m_cachedUnitFlags = 0; m_cachedPower = 0; m_cachedMaxPower = 0;
            m_cachedPowerType = 0;
        }

        // Read casting/channeling from object base offsets (independent of UnitFields)
        m_cachedCastingSpellId = ReadMemory<uint32_t>(baseAddr + OBJECT_CASTING_ID_OFFSET);
        m_cachedChannelSpellId = ReadMemory<uint32_t>(baseAddr + OBJECT_CHANNEL_ID_OFFSET);

    } catch (...) {
         // Clear data on exception
         m_cachedHealth = 0; m_cachedMaxHealth = 0; m_cachedLevel = 0; m_cachedUnitFlags = 0;
         m_cachedPower = 0; m_cachedMaxPower = 0; m_cachedPowerType = 0;
         m_cachedCastingSpellId = 0; m_cachedChannelSpellId = 0;
    }
    // Note: m_lastUpdateTime was already set by the base class call
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
    // Placeholder - Needs implementation using ReadUnitField and UNIT_FIELD_BYTES_0
    return "UnknownClass";
}

// --- WowGameObject Implementation ---

WowGameObject::WowGameObject(void* ptr, WGUID guid) : WowObject(ptr, guid, OBJECT_GAMEOBJECT) {}

// Override UpdateDynamicData for GameObjects to read position from raw offsets
void WowGameObject::UpdateDynamicData() {
    // Call base first for throttling and common data (e.g., rotation from offset)
    // Note: Base UpdateDynamicData currently reads pos from 0x798/C/0 which is likely wrong for GOs,
    // so we will overwrite m_cachedPosition below.
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
    // WARNING: These might be local coordinates, not world coordinates.
    try {
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(m_pointer);
        constexpr DWORD GO_RAW_POS_Y_OFFSET = 0xE8; // Swapped: Offset 0xE8 seems to hold Y
        constexpr DWORD GO_RAW_POS_X_OFFSET = 0xEC; // Swapped: Offset 0xEC seems to hold X
        constexpr DWORD GO_RAW_POS_Z_OFFSET = 0xF0;

        // Assign to correct members based on observed order
        m_cachedPosition.x = ReadMemory<float>(baseAddr + GO_RAW_POS_X_OFFSET); // Read X from 0xEC
        m_cachedPosition.y = ReadMemory<float>(baseAddr + GO_RAW_POS_Y_OFFSET); // Read Y from 0xE8
        m_cachedPosition.z = ReadMemory<float>(baseAddr + GO_RAW_POS_Z_OFFSET); // Z remains 0xF0
        
    } catch (...) {
         // Exception during memory read, keep potentially incorrect position from base class
         // or consider setting to {0,0,0} here?
         // For now, we keep whatever base class read.
    }
    // Note: m_lastUpdateTime was already set by the base class call
}

// Example of keeping VTable call for specific functions
int WowGameObject::GetQuestStatus() {
    if (!m_pointer) return 0;
    try {
        typedef int (__thiscall* GetQuestStatusFunc)(void* thisptr);
        void** vftable = ReadMemory<void**>(reinterpret_cast<uintptr_t>(m_pointer));
         if (!vftable) return 0;

        // Use the ENUM index for GetQuestStatus
        uintptr_t funcAddr = ReadMemory<uintptr_t>(reinterpret_cast<uintptr_t>(&vftable[VFTableIndex::VF_GetQuestStatus])); 
         if (!funcAddr) return 0;

        GetQuestStatusFunc func = reinterpret_cast<GetQuestStatusFunc>(funcAddr);
        return func(m_pointer);
    } catch (...) {
        return 0;
    }
} 