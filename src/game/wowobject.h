#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <iomanip>
#include "../../utils/memory.h" // Include the robust MemoryReader
#include "../../utils/log.h" // Include for LogStream and LogMessage

// WoW GUID structure (64-bit identifier for game objects)
struct WGUID {
    uint32_t guid_low;
    uint32_t guid_high;

    bool operator==(const WGUID& other) const {
        return guid_low == other.guid_low && guid_high == other.guid_high;
    }

    bool operator!=(const WGUID& other) const {
        return !(*this == other);
    }

    // Less-than operator for std::map compatibility
    bool operator<(const WGUID& other) const {
        // Compare high bits first, then low bits
        if (guid_high != other.guid_high)
            return guid_high < other.guid_high;
        return guid_low < other.guid_low;
    }

    bool IsValid() const {
        return guid_low != 0 || guid_high != 0;
    }
};

// Helper to convert WGUID to uint64_t
inline uint64_t GuidToUint64(const WGUID& guid) {
    return (static_cast<uint64_t>(guid.guid_high) << 32) | guid.guid_low;
}

// 3D Vector for positions
struct Vector3 {
    float x = 0.0f, y = 0.0f, z = 0.0f; // Initialize members
};

// Object Types
enum WowObjectType {
    OBJECT_NONE = 0,
    OBJECT_ITEM = 1,
    OBJECT_CONTAINER = 2,
    OBJECT_UNIT = 3,
    OBJECT_PLAYER = 4,
    OBJECT_GAMEOBJECT = 5,
    OBJECT_DYNAMICOBJECT = 6,
    OBJECT_CORPSE = 7
};

// Base WoW object class
class WowObject {
protected:
    WGUID m_guid;
    void* m_pointer; // Pointer to the object in game memory
    WowObjectType m_type;

    // --- Cached Dynamic Data ---
    std::string m_cachedName; // Cache name as it might involve function calls
    Vector3 m_cachedPosition;
    float m_cachedRotation;
    float m_cachedScale; 
    uint64_t m_lastUpdateTime = 0; // Basic throttling mechanism

    // Helper to read string from vtable function (for GetName)
    std::string ReadNameFromVTable();

public:
    WowObject(void* ptr, WGUID guid, WowObjectType type);
    virtual ~WowObject() = default; // Use default destructor

    // --- Core Accessors ---
    WGUID GetGUID() const { return m_guid; }
    void* GetPointer() const { return m_pointer; }
    WowObjectType GetType() const { return m_type; } // Type doesn't change

    // --- Virtual method to update dynamic data ---
    // Reads from memory into cached members
    virtual void UpdateDynamicData();

    // --- Accessors for Cached Data ---
    // Note: These now return the cached values!
    std::string GetName(); // Will update cache if empty
    Vector3 GetPosition() const { return m_cachedPosition; }
    float GetFacing() const { return m_cachedRotation; }
    float GetScale() const { return m_cachedScale; }

    // VFTable function indexes (from user's data) - renamed with VF_ prefix to avoid conflicts
    enum VFTableIndex {
        VF_GetBagPtr = 10,
        VF_GetPosition = 12,
        VF_GetFacing = 14,
        VF_GetScale = 15,
        VF_GetQuestStatus = 22,
        VF_GetModel = 24,
        VF_Interact = 44,
        VF_GetName = 54
    };

    // Virtual function table helpers
    template<typename T>
    T CallVFunc(int index) {
        if (!m_pointer) return T{};
        
        void** vftable = *(void***)m_pointer;
        typedef T (*FuncType)();
        FuncType func = (FuncType)vftable[index];
        return func();
    }

    template<typename T, typename A1>
    T CallVFunc1(int index, A1 arg1) {
        if (!m_pointer) return T{};
        
        void** vftable = *(void***)m_pointer;
        typedef T (*FuncType)(A1);
        FuncType func = (FuncType)vftable[index];
        return func(arg1);
    }

    // Common methods (Interact might still call VTable directly or be updated)
    bool Interact();
};

// Specialized Unit class (for players, NPCs, etc.)
class WowUnit : public WowObject {
protected:
    // --- Cached Unit-Specific Data ---
    int m_cachedHealth = 0;
    int m_cachedMaxHealth = 0;
    int m_cachedLevel = 0;
    int m_cachedPower = 0;
    int m_cachedMaxPower = 0;
    uint8_t m_cachedPowerType = 0; // Store the raw power type byte
    uint32_t m_cachedUnitFlags = 0;
    uint32_t m_cachedCastingSpellId = 0;
    uint32_t m_cachedChannelSpellId = 0;

    // Helper function to safely read from UnitFields - USES MemoryReader::Read
    template <typename T>
    T ReadUnitField(DWORD valueOffset) {
        // This helper reads relative to the UnitFields pointer
        if (!m_pointer) return T{};
        // Define OBJECT_UNIT_FIELDS_PTR_OFFSET here or ensure it's included
        constexpr DWORD OBJECT_UNIT_FIELDS_PTR_OFFSET = 0x8;
        try {
            uintptr_t unitFieldsPtrAddr = reinterpret_cast<uintptr_t>(m_pointer) + OBJECT_UNIT_FIELDS_PTR_OFFSET;
            // Use MemoryReader for safety
            uintptr_t unitFieldsPtr = MemoryReader::Read<uintptr_t>(unitFieldsPtrAddr);
            if (!unitFieldsPtr) return T{};
            // Use MemoryReader for safety
            return MemoryReader::Read<T>(unitFieldsPtr + valueOffset);
        } catch (...) {
             // Log potential error here if needed
            LogStream ssErr; ssErr << "!!! ReadUnitField FAILED for offset 0x" << std::hex << valueOffset << " !!!"; LogMessage(ssErr.str());
            return T{}; // Return default value on error
        }
    }

public:
    WowUnit(void* ptr, WGUID guid);

    // Override to update unit-specific data
    void UpdateDynamicData() override;

    // --- Accessors for Cached Unit Data ---
    int GetHealth() const { return m_cachedHealth; }
    int GetMaxHealth() const { return m_cachedMaxHealth; }
    int GetLevel() const { return m_cachedLevel; }
    int GetPower() const { return m_cachedPower; }
    int GetMaxPower() const { return m_cachedMaxPower; }
    uint8_t GetPowerType() const { return m_cachedPowerType; }
    uint32_t GetUnitFlags() const { return m_cachedUnitFlags; }
    uint32_t GetCastingSpellId() const { return m_cachedCastingSpellId; }
    uint32_t GetChannelSpellId() const { return m_cachedChannelSpellId; }
    
    bool HasFlag(uint32_t flag) const { return (m_cachedUnitFlags & flag) != 0; }
    
    // Modified IsDead to read directly from memory for robustness
    bool IsDead() {
        // Read health directly using the helper (which now uses MemoryReader)
        int currentHealth = ReadUnitField<int>(/*UNIT_FIELD_HEALTH_OFFSET*/ 0x18 * 4); // Use literal offset
        // Read flags directly
        uint32_t currentFlags = ReadUnitField<uint32_t>(/*UNIT_FIELD_FLAGS_OFFSET*/ 0x3B * 4); // Use literal offset

        // Log the values read for debugging
        // LogStream ss; ss << "IsDead Check: Health=" << currentHealth << ", Flags=0x" << std::hex << currentFlags; LogMessage(ss.str());

        // Check health first, then specific flags if needed
        return currentHealth <= 0 || (currentFlags & 0x04000000 /*UNIT_FLAG_SKINNABLE*/) != 0;
    }

    bool IsCasting() const { return m_cachedCastingSpellId != 0; }
    bool IsChanneling() const { return m_cachedChannelSpellId != 0; }
    std::string GetPowerTypeString() const; // Helper to convert type byte to string
};

// Specialized Player class
class WowPlayer : public WowUnit {
public:
    WowPlayer(void* ptr, WGUID guid);

    // Player-specific methods
    std::string GetClass();
};

// GameObject class (for doors, chests, etc.)
class WowGameObject : public WowObject {
public:
    WowGameObject(void* ptr, WGUID guid);

    // Override to use specific GameObject position logic (Reading Raw Offsets)
    virtual void UpdateDynamicData() override;

    // Example of keeping VTable call for specific functions
    int GetQuestStatus(); // Still uses VTable
}; 