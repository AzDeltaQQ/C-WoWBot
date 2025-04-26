#pragma once
#include "wowobject.h"
#include <Windows.h>
#include <functional>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept> // Include for exception
#include <mutex>      // <-- Include mutex header

// Provided Offsets
constexpr DWORD STATIC_CLIENT_CONNECTION = 0x00C79CE0;
constexpr DWORD OBJECT_MANAGER_OFFSET = 0x2ED0;
constexpr DWORD LOCAL_GUID_OFFSET = 0xC0;
constexpr DWORD OBJECT_TYPE_OFFSET = 0x14; // Offset relative to object base

// Define ObjectManagerActual structure based on usage
struct ObjectManagerActual { // Placeholder, actual layout unknown beyond offsets used
    uint8_t padding[0x1C];
    void* hashTableBase; // Offset 0x1C based on findObjectByIdAndData
    uint8_t padding2[0x24 - 0x1C - 4];
    uint32_t hashTableMask; // Offset 0x24 based on findObjectByIdAndData
    // ... other unknown fields ...
};

// Function pointer types
typedef int(__cdecl* EnumVisibleObjectsCallback)(uint32_t guid_low, uint32_t guid_high, int callback_arg);
typedef int(__cdecl* EnumVisibleObjectsFn)(EnumVisibleObjectsCallback callback, int callback_arg);

// Function pointer type for the INNER GetObjectPtrByGuid (findObjectByIdAndData)
// Convention: __thiscall (this in ECX, args on stack, callee cleans stack)
// Arguments: this (ObjectManagerActual*), guid_low (uint32_t), pGuidStruct (WGUID*)
typedef void* (__thiscall* GetObjectPtrByGuidInnerFn)(void* thisptr, uint32_t guid_low, WGUID* pGuidStruct);

// Main object manager class
class ObjectManager {
private:
    // Singleton instance
    static ObjectManager* m_instance;
    
    // Pointers to WoW memory
    EnumVisibleObjectsFn m_enumVisibleObjects;
    GetObjectPtrByGuidInnerFn m_getObjectPtrByGuidInner; // Renamed and changed type
    
    // Added base addresses needed for direct reads
    // DWORD m_baseObjectManagerAddr; // Store the calculated Object Manager address // Removed as not used currently

    // Cache of objects
    std::map<WGUID, std::shared_ptr<WowObject>> m_objectCache;
    std::mutex m_cacheMutex; // <-- Add mutex member
    
    // Local player GUID
    WGUID m_localPlayerGuid; // Will be initialized in constructor
    
    // Callback for enumeration
    static int __cdecl EnumObjectsCallback(uint32_t guid_low, uint32_t guid_high, int callback_arg);
    
    // Ensure this member is declared
    ObjectManagerActual* m_objectManagerPtr; 
    
    bool m_isFullyInitialized; // New flag
    
    // Private constructor for singleton
    ObjectManager();

    // Helper for GetLocalPlayer to call GetObjectByGUID without re-locking
    std::shared_ptr<WowObject> GetObjectByGUID_locked(WGUID guid);

public:
    ~ObjectManager();
    
    // Get singleton instance
    static ObjectManager* GetInstance();
    
    // Add a static shutdown method
    static void Shutdown(); 

    // Initialize only stores function pointers now
    bool Initialize(DWORD enumVisibleObjectsAddr, DWORD getObjectPtrByGuidInnerAddr);
    // New method to attempt pointer/GUID reading
    bool TryFinishInitialization(); 
    // Check if fully initialized
    bool IsInitialized() const { return m_isFullyInitialized; }
    
    // Update cache of objects
    void Update();
    
    // Get objects
    std::shared_ptr<WowObject> GetObjectByGUID(WGUID guid);
    std::vector<std::shared_ptr<WowObject>> GetObjectsByType(WowObjectType type);
    std::shared_ptr<WowPlayer> GetLocalPlayer(); // Still returns WowPlayer
    
    // Get all objects (non-const version, might update)
    std::map<WGUID, std::shared_ptr<WowObject>> GetObjects(); 
    // Get all objects (const version, returns a copy for thread safety)
    std::map<WGUID, std::shared_ptr<WowObject>> GetObjects() const;
    
    // Find objects by name
    std::vector<std::shared_ptr<WowObject>> FindObjectsByName(const std::string& name);
    
    // Get nearest object of a type
    std::shared_ptr<WowObject> GetNearestObject(WowObjectType type, float maxDistance = 9999.0f);

    // Get objects within distance (declaration added)
    std::vector<std::shared_ptr<WowObject>> GetObjectsWithinDistance(const Vector3& center, float distance);

    std::shared_ptr<WowObject> GetTarget();
    std::shared_ptr<WowObject> GetObjectByGUID(uint64_t guid);

    WGUID GetLocalPlayerGUID() const; // Add this getter (WGUID version)
    
    // Get the GUID of the currently targeted unit (from global variable)
    uint64_t GetCurrentTargetGUID() const;

    // Returns the underlying pointer to the actual game object manager (declaration added)
    ObjectManagerActual* GetInternalObjectManagerPtr() const;

// Removed duplicate/old private members and static declarations below:
// private:
//    void EnumerateVisibleObjects(); // Not implemented
//    void ReadObjectDescriptors(std::shared_ptr<WowObject> obj); // Not implemented
//    bool m_fullyInitialized = false; // Duplicate
//    std::map<WGUID, std::shared_ptr<WowObject>> m_objectCache; // Duplicate
//    WGUID m_localPlayerGuid = WGUID(0); // Duplicate and invalid initializer
//    std::mutex m_cacheMutex; // Duplicate
//
//    // Helper for GetLocalPlayer to call GetObjectByGUID without re-locking
//    std::shared_ptr<WowObject> GetObjectByGUID_locked(WGUID guid); // Duplicate
//
//    // Static function pointers
//    static EnumerateVisibleObjectsFn m_enumerateVisibleObjectsFn; // Duplicate and incorrect
}; 