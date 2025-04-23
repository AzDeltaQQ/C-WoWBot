#include "wowobject.h"
#include <Windows.h>
#include <stdexcept> // For exception handling

// --- Offsets from User List --- 
// Object Base Relative
constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C;
constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798;
constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0;
constexpr DWORD OBJECT_ROTATION_OFFSET = 0x7A8;
constexpr DWORD OBJECT_UNIT_FIELDS_PTR_OFFSET = 0x8;
// Unit Fields Relative (Multiply index by 4 is already done in defines)
constexpr DWORD UNIT_FIELD_HEALTH_OFFSET = 0x18 * 4;
constexpr DWORD UNIT_FIELD_MAXHEALTH_OFFSET = 0x20 * 4;
constexpr DWORD UNIT_FIELD_LEVEL_OFFSET = 0x36 * 4;
// Add other unit field offsets if needed (Mana, Energy, etc.)

// Structure for Position (not needed for direct read)
// #pragma pack(push, 1) ... #pragma pack(pop)

// Base WowObject methods
std::string WowObject::GetName() {
    // Keep using VTable for GetName for now, but add null check
    if (!m_pointer) return "";
    try {
        typedef char* (__thiscall* GetNameFunc)(void* thisptr);
        void** vftable = *(void***)m_pointer;
        if (!vftable) return ""; // Check if vtable is null
        GetNameFunc func = (GetNameFunc)vftable[VFTableIndex::VF_GetName];
        if (!func) return ""; // Check if function pointer is null
        
        char* name = func(m_pointer);
        return name ? std::string(name) : "";
    } catch (const std::exception&) {
        // Catch memory access violation if m_pointer is invalid
        return "[Error Reading Name]";
    }
}

Vector3 WowObject::GetPosition() {
    // Read position using direct offsets
    if (!m_pointer) return {0, 0, 0};
    try {
        float x = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(m_pointer) + OBJECT_POS_X_OFFSET);
        float y = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(m_pointer) + OBJECT_POS_Y_OFFSET);
        float z = *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(m_pointer) + OBJECT_POS_Z_OFFSET);
        return {x, y, z};
    } catch (const std::exception&) {
        return {0, 0, 0}; // Return zero vector on error
    }
}

float WowObject::GetFacing() {
    // Read facing/rotation using direct offset
    if (!m_pointer) return 0.0f;
    try {
        return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(m_pointer) + OBJECT_ROTATION_OFFSET);
    } catch (const std::exception&) {
        return 0.0f; // Return 0 on error
    }
}

float WowObject::GetScale() {
    // Keep using VTable for GetScale (offset not provided)
    if (!m_pointer) return 1.0f;
     try {
        typedef float (__thiscall* GetScaleFunc)(void* thisptr);
        void** vftable = *(void***)m_pointer;
        if (!vftable) return 1.0f;
        GetScaleFunc func = (GetScaleFunc)vftable[VFTableIndex::VF_GetScale];
         if (!func) return 1.0f;
        return func(m_pointer);
    } catch (const std::exception&) {
        return 1.0f;
    }
}

bool WowObject::Interact() {
    // Keep using VTable for Interact
    if (!m_pointer) return false;
    try {
        typedef bool (__thiscall* InteractFunc)(void* thisptr);
        void** vftable = *(void***)m_pointer;
        if (!vftable) return false;
        InteractFunc func = (InteractFunc)vftable[VFTableIndex::VF_Interact];
        if (!func) return false;
        return func(m_pointer);
    } catch (const std::exception&) {
        return false;
    }
}

// Helper function to safely read from UnitFields
template <typename T> 
T ReadUnitField(void* objectPtr, DWORD unitFieldsOffset, DWORD valueOffset) {
    if (!objectPtr) return T{};
    try {
        uintptr_t unitFieldsPtrAddr = reinterpret_cast<uintptr_t>(objectPtr) + unitFieldsOffset;
        void* unitFieldsPtr = *reinterpret_cast<void**>(unitFieldsPtrAddr);
        if (!unitFieldsPtr) return T{};
        return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(unitFieldsPtr) + valueOffset);
    } catch (const std::exception&) {
        return T{}; // Return default value on error
    }
}

// WowUnit methods
int WowUnit::GetHealth() {
    return ReadUnitField<int>(m_pointer, OBJECT_UNIT_FIELDS_PTR_OFFSET, UNIT_FIELD_HEALTH_OFFSET);
}

int WowUnit::GetMaxHealth() {
    return ReadUnitField<int>(m_pointer, OBJECT_UNIT_FIELDS_PTR_OFFSET, UNIT_FIELD_MAXHEALTH_OFFSET);
}

int WowUnit::GetLevel() {
    return ReadUnitField<int>(m_pointer, OBJECT_UNIT_FIELDS_PTR_OFFSET, UNIT_FIELD_LEVEL_OFFSET);
}

bool WowUnit::IsDead() {
    // Check health directly, avoid recursive call if GetHealth fails
    int health = ReadUnitField<int>(m_pointer, OBJECT_UNIT_FIELDS_PTR_OFFSET, UNIT_FIELD_HEALTH_OFFSET);
    return health <= 0;
}

// WowPlayer methods
std::string WowPlayer::GetClass() {
    // Placeholder - Reading class byte requires OBJECT_DESCRIPTOR_OFFSET + UNIT_FIELD_BYTES_0 etc.
    // Add implementation if needed, using ReadUnitField or similar logic.
    return "Unknown";
}

int WowPlayer::GetMana() {
    // Placeholder - Requires specific UNIT_FIELD_ENERGY offset and check for POWER_MANA
    // Add implementation if needed.
    return 0;
}

int WowPlayer::GetMaxMana() {
    // Placeholder - Requires specific UNIT_FIELD_MAXENERGY offset and check for POWER_MANA
    // Add implementation if needed.
    return 100;
}

// WowGameObject methods
int WowGameObject::GetQuestStatus() {
    // Keep using VTable for GetQuestStatus (offset not provided)
    if (!m_pointer) return 0;
    try {
        typedef int (__thiscall* GetQuestStatusFunc)(void* thisptr);
        void** vftable = *(void***)m_pointer;
         if (!vftable) return 0;
        GetQuestStatusFunc func = (GetQuestStatusFunc)vftable[VFTableIndex::VF_GetQuestStatus];
         if (!func) return 0;
        return func(m_pointer);
    } catch (const std::exception&) {
        return 0;
    }
} 