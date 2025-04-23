#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <iomanip>

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
    float x, y, z;
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
    void* m_pointer;
    WowObjectType m_type;

public:
    WowObject(void* ptr, WGUID guid, WowObjectType type) 
        : m_pointer(ptr), m_guid(guid), m_type(type) {}

    virtual ~WowObject() {}

    // Base properties
    WGUID GetGUID() const { return m_guid; }
    void* GetPointer() const { return m_pointer; }
    WowObjectType GetType() const { return m_type; }

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

    // Common methods for all objects
    std::string GetName();
    Vector3 GetPosition();
    float GetFacing();
    float GetScale();
    bool Interact();
};

// Specialized Unit class (for players, NPCs, etc.)
class WowUnit : public WowObject {
public:
    WowUnit(void* ptr, WGUID guid) : WowObject(ptr, guid, OBJECT_UNIT) {}

    // Unit-specific methods
    int GetHealth();
    int GetMaxHealth();
    int GetLevel();
    bool IsDead();
};

// Specialized Player class
class WowPlayer : public WowUnit {
public:
    WowPlayer(void* ptr, WGUID guid) : WowUnit(ptr, guid) {
        m_type = OBJECT_PLAYER;
    }

    // Player-specific methods
    std::string GetClass();
    int GetMana();
    int GetMaxMana();
};

// GameObject class (for doors, chests, etc.)
class WowGameObject : public WowObject {
public:
    WowGameObject(void* ptr, WGUID guid) : WowObject(ptr, guid, OBJECT_GAMEOBJECT) {}

    // GameObject-specific methods
    int GetQuestStatus();
}; 