#pragma once
#include "wowobject.h"
#include <Windows.h>
#include <functional>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept> // Include for exception

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
    DWORD m_baseObjectManagerAddr; // Store the calculated Object Manager address
    
    // Cache of objects
    std::map<WGUID, std::shared_ptr<WowObject>> m_objectCache;
    
    // Local player GUID
    WGUID m_localPlayerGuid; // Will be read directly now
    
    // Callback for enumeration
    static int __cdecl EnumObjectsCallback(uint32_t guid_low, uint32_t guid_high, int callback_arg);
    
    // Ensure this member is declared
    ObjectManagerActual* m_objectManagerPtr; 
    
    bool m_isFullyInitialized; // New flag
    
    // Private constructor for singleton
    ObjectManager();

public:
    ~ObjectManager();
    
    // Get singleton instance
    static ObjectManager* GetInstance();
    
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
    
    // Get all objects
    std::map<WGUID, std::shared_ptr<WowObject>> GetObjects(); // Return map for easier access in GUI
    
    // Find objects by name
    std::vector<std::shared_ptr<WowObject>> FindObjectsByName(const std::string& name);
    
    // Get nearest object of a type
    std::shared_ptr<WowObject> GetNearestObject(WowObjectType type, float maxDistance = 9999.0f);
}; 