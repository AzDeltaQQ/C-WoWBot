#include "log.h" // Include the new log header
#include "../utils/memory.h" // Added include for MemoryReader
#include <algorithm>
#include <cmath>
#include <stdexcept> // For exception handling
#include <string>    // Needed for std::to_string
#include <sstream>   // Needed for string streams
#include <iomanip>   // For std::setw and std::setfill
#include <mutex>     // Added for std::mutex
#include "wowobject.h" // Then the definition of Vec3
#include "objectmanager.h" // Then the header using Vec3
#include "functions.h" // Include for GetLocalPlayerGuid function pointer

// Initialize static instance
ObjectManager* ObjectManager::m_instance = nullptr;

// Constructor
ObjectManager::ObjectManager() 
    : m_enumVisibleObjects(nullptr),
      m_getObjectPtrByGuidInner(nullptr),
      m_objectManagerPtr(nullptr),
      m_isFullyInitialized(false), // Initialize flag to false
      m_cachedLocalPlayer(nullptr) // Initialize cached player pointer
{
    m_localPlayerGuid = { 0, 0 }; // Initialize to 0, but don't rely on it
}

// Destructor
ObjectManager::~ObjectManager() {
    m_objectCache.clear();
}

// Get singleton instance
ObjectManager* ObjectManager::GetInstance() {
    if (!m_instance) {
        m_instance = new ObjectManager();
    }
    return m_instance;
}

// Initialize just stores function pointers now
bool ObjectManager::Initialize(DWORD enumVisibleObjectsAddr, DWORD getObjectPtrByGuidInnerAddr) {
    m_enumVisibleObjects = reinterpret_cast<EnumVisibleObjectsFn>(enumVisibleObjectsAddr);
    m_getObjectPtrByGuidInner = reinterpret_cast<GetObjectPtrByGuidInnerFn>(getObjectPtrByGuidInnerAddr);
    
    // Basic check if pointers are non-null
    bool success = m_enumVisibleObjects != nullptr && m_getObjectPtrByGuidInner != nullptr;
    if (!success) {
         LogMessage("ObjectManager::Initialize ERROR: Invalid function addresses provided.\n");
    }
    return success;
}

// Static Shutdown method implementation
void ObjectManager::Shutdown() {
    if (m_instance) {
        LogMessage("ObjectManager::Shutdown() called.");
        // Clear cache explicitly (destructor will also do this, but good practice)
        m_instance->m_objectCache.clear(); 
        // Nullify potentially dangerous pointers
        m_instance->m_objectManagerPtr = nullptr;
        m_instance->m_enumVisibleObjects = nullptr;
        m_instance->m_getObjectPtrByGuidInner = nullptr;
        m_instance->m_isFullyInitialized = false;
        m_instance->m_cachedLocalPlayer = nullptr; // Clear cached player
        
        // Delete the singleton instance
        delete m_instance;
        m_instance = nullptr;
        LogMessage("ObjectManager::Shutdown() completed.");
    }
}

// TryFinishInitialization attempts to read the game pointers
bool ObjectManager::TryFinishInitialization() {
    // If already initialized, don't try again
    if (m_isFullyInitialized) {
        return true;
    }

    ObjectManagerActual* tempObjMgrPtr = nullptr;
    // WGUID tempLocalGuid = {0, 0}; // No longer read GUID here

    try {
        DWORD clientConnection = *reinterpret_cast<DWORD*>(STATIC_CLIENT_CONNECTION);
        if (!clientConnection) { 
             // Don't log error every frame, just return false
             return false; 
        }
        
        tempObjMgrPtr = *reinterpret_cast<ObjectManagerActual**>(clientConnection + OBJECT_MANAGER_OFFSET);
        if (!tempObjMgrPtr) { 
            // Object Manager pointer is still NULL, wait longer
            return false; 
        }
        
        // uintptr_t guidReadAddr = reinterpret_cast<uintptr_t>(tempObjMgrPtr) + LOCAL_GUID_OFFSET;
        // tempLocalGuid = *reinterpret_cast<WGUID*>(guidReadAddr);
        // 
        // if (!tempLocalGuid.IsValid()) {
        //      return false; // Indicate initialization is not yet complete
        // }

    } catch (const std::exception& e) {
        // Log the exception minimally if needed
        LogStream errLog; errLog << "[TryFinishInitialization] EXCEPTION: " << e.what(); LogMessage(errLog.str());
        return false; // Failed due to exception
    }

    // --- Success! Store ObjMgr pointer and set flag --- 
    // Only reach here if ClientConnection AND ObjMgrPtr are valid
    m_objectManagerPtr = tempObjMgrPtr;
    // m_localPlayerGuid = tempLocalGuid; // Don't store GUID from potentially early read
    m_isFullyInitialized = true;

    // Log the successfully read GUID - REMOVED
    // LogStream initLog;
    // initLog << "ObjectManager::TryFinishInitialization Succeeded! Object Manager ready. Stored Player GUID: 0x"
    //         << std::hex << GuidToUint64(m_localPlayerGuid);
    // LogMessage(initLog.str());
    LogMessage("ObjectManager::TryFinishInitialization Succeeded! Object Manager pointer acquired.");

    return true;
}

// Callback for object enumeration - Needs to check IsInitialized
int __cdecl ObjectManager::EnumObjectsCallback(uint32_t guid_low, uint32_t guid_high, int callback_arg) {
    ObjectManager* instance = reinterpret_cast<ObjectManager*>(callback_arg);
    // Check full initialization before proceeding
    if (!instance || !instance->IsInitialized() || !instance->m_objectManagerPtr || !instance->m_getObjectPtrByGuidInner) return 0; 

    WGUID guid = { guid_low, guid_high };
    void* objPtr = nullptr;
    
    try {
         // Use temporary struct copy for safety with the inner function call
         WGUID guidCopy = guid;
         objPtr = instance->m_getObjectPtrByGuidInner(instance->m_objectManagerPtr, guid.guid_low, &guidCopy);
    } catch (const std::exception& e) {
        LogStream errLog; errLog << "[EnumObjectsCallback] EXCEPTION calling GetObjectPtrByGuidInner: " << e.what(); LogMessage(errLog.str());
        return 1; // Continue enumeration
    }

    if (!objPtr) { 
        return 1; 
    }

    WowObjectType type = OBJECT_NONE;
    try {
        uintptr_t typeAddr = reinterpret_cast<uintptr_t>(objPtr) + OBJECT_TYPE_OFFSET;
        type = *reinterpret_cast<WowObjectType*>(typeAddr);
        if (type < OBJECT_NONE || type > OBJECT_CORPSE) {
             type = OBJECT_NONE;
        }
    } catch (const std::exception& e) {
         LogStream errLog; errLog << "[EnumObjectsCallback] EXCEPTION reading Object Type: " << e.what(); LogMessage(errLog.str());
         return 1; 
    }

    if (type != OBJECT_NONE) { 
        std::shared_ptr<WowObject> obj;
        switch (type) {
            case OBJECT_PLAYER:     obj = std::make_shared<WowPlayer>(objPtr, guid); break;
            case OBJECT_UNIT:       obj = std::make_shared<WowUnit>(objPtr, guid); break;
            case OBJECT_GAMEOBJECT: obj = std::make_shared<WowGameObject>(objPtr, guid); break;
            default:                obj = std::make_shared<WowObject>(objPtr, guid, type); break;
        }
        if (obj) { 
            // No lock needed here as Update() holds the lock
            instance->m_objectCache[guid] = obj; 
        } else {
             LogStream errLog; errLog << "[EnumObjectsCallback] FAILED make_shared for GUID 0x" << std::hex << GuidToUint64(guid);
             LogMessage(errLog.str());
        }
    }
    
    return 1; // Continue enumeration
}

// Update object cache - Needs to check IsInitialized
void ObjectManager::Update() {
    // Don't try to update if not fully initialized
    if (!IsInitialized()) {
        return;
    }
    
    // Lock the mutex for the duration of this function
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    
    m_objectCache.clear(); // Clear cache before enumeration
    if (m_enumVisibleObjects) {
        try {
            m_enumVisibleObjects(EnumObjectsCallback, reinterpret_cast<int>(this));
        } catch (const std::exception& e) {
             LogMessage(std::string("ObjectManager::Update EXCEPTION during enumeration: ") + e.what() + "\n");
             m_objectCache.clear(); // Clear again on error
        }
    }
    // NOTE: RefreshLocalPlayerCache is called separately in HookedEndScene AFTER Update()
}

// Get object by GUID (WGUID version)
std::shared_ptr<WowObject> ObjectManager::GetObjectByGUID(WGUID guid) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_objectCache.find(guid);
    return (it != m_objectCache.end()) ? it->second : nullptr;
}

// Add the missing implementation for the uint64_t version
std::shared_ptr<WowObject> ObjectManager::GetObjectByGUID(uint64_t guid64) {
    // Convert uint64_t to WGUID
    WGUID guid;
    guid.guid_low = static_cast<uint32_t>(guid64 & 0xFFFFFFFF);
    guid.guid_high = static_cast<uint32_t>((guid64 >> 32) & 0xFFFFFFFF);
    
    // Call the WGUID version
    return GetObjectByGUID(guid); 
}

// Get objects by type
std::vector<std::shared_ptr<WowObject>> ObjectManager::GetObjectsByType(WowObjectType type) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    std::vector<std::shared_ptr<WowObject>> results;
    for (const auto& pair : m_objectCache) {
        if (pair.second && pair.second->GetType() == type) {
            results.push_back(pair.second);
        }
    }
    return results;
}

// NEW: Refreshes the internally cached local player pointer
void ObjectManager::RefreshLocalPlayerCache() {
    // Ensure ObjectManager base pointers are initialized first
    if (!IsInitialized()) { 
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cachedLocalPlayer = nullptr;
        return;
    }

    // Call the native function to get the current player GUID
    if (!GetLocalPlayerGuid) {
        // LogMessage("ObjectManager::RefreshLocalPlayerCache Error: Global GetLocalPlayerGuid function pointer is null!");
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cachedLocalPlayer = nullptr;
        return;
    }
    uint64_t playerGuid64 = GetLocalPlayerGuid();
    if (playerGuid64 == 0) {
        // LogMessage("ObjectManager::RefreshLocalPlayerCache Warning: GetLocalPlayerGuid() returned 0."); // Don't spam log
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_cachedLocalPlayer = nullptr;
        return; // Not logged in or player not valid yet?
    }

    WGUID currentLocalPlayerGuid;
    currentLocalPlayerGuid.guid_low = (uint32_t)playerGuid64;
    currentLocalPlayerGuid.guid_high = (uint32_t)(playerGuid64 >> 32);

    std::shared_ptr<WowPlayer> foundPlayer = nullptr;

    // Now use this potentially updated GUID to find the object
    
    // 1. Check Cache first 
    {
         std::lock_guard<std::mutex> lock(m_cacheMutex);
         auto objFromCache = GetObjectByGUID_locked(currentLocalPlayerGuid); // Use helper
         if (objFromCache && objFromCache->GetType() == OBJECT_PLAYER) {
             foundPlayer = std::static_pointer_cast<WowPlayer>(objFromCache);
         }
    } // Mutex unlocked here
    
    // 2. If not in cache, try direct lookup using the __thiscall function
    if (!foundPlayer && m_objectManagerPtr && m_getObjectPtrByGuidInner) {
         void* playerPtr = nullptr;
         try {
              // Create temporary GUID struct to pass its pointer
              WGUID localGuidCopy = currentLocalPlayerGuid;
              playerPtr = m_getObjectPtrByGuidInner(m_objectManagerPtr, currentLocalPlayerGuid.guid_low, &localGuidCopy);
         } catch (const std::exception&) { /* Call failed */ }
         
         if (playerPtr) {
             // Verify type before returning
             try {
                 WowObjectType type = *reinterpret_cast<WowObjectType*>(reinterpret_cast<uintptr_t>(playerPtr) + OBJECT_TYPE_OFFSET);
                 if (type == OBJECT_PLAYER) {
                     // Found via direct lookup, create the object
                     foundPlayer = std::make_shared<WowPlayer>(playerPtr, currentLocalPlayerGuid); 
                     // Immediately add/update it in the cache
                     {
                         std::lock_guard<std::mutex> lock(m_cacheMutex);
                         m_objectCache[currentLocalPlayerGuid] = foundPlayer; 
                     }
                 }
             } catch(const std::exception&) { /* Failed to read type */ }
        }
    }
    
    // 3. Fallback: Search cache again (maybe Update() just ran between steps 1 and 2? Unlikely but possible)
    if (!foundPlayer) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto objFromCache = GetObjectByGUID_locked(currentLocalPlayerGuid);
        if (objFromCache && objFromCache->GetType() == OBJECT_PLAYER) {
            foundPlayer = std::static_pointer_cast<WowPlayer>(objFromCache);
        }
    }
    
    // 4. Final fallback: Iterate cache (less efficient) - Removed this as it's unreliable for finding the *local* player
    /*
    if (!foundPlayer) {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        for (const auto& pair : m_objectCache) {
            if (pair.second && pair.second->GetType() == OBJECT_PLAYER) {
                 LogMessage("ObjectManager::RefreshLocalPlayerCache Warning: Found player via cache iteration fallback.");
                 foundPlayer = std::static_pointer_cast<WowPlayer>(pair.second);
                 break; // Found one, stop iterating
            }
        }
    }
    */

    // Update the cached member pointer
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        // Log if the player pointer changed (or became null/valid)
        // if (m_cachedLocalPlayer != foundPlayer) {
        //     LogStream ss; ss << "RefreshLocalPlayerCache: Player pointer updated. Was " 
        //                    << (m_cachedLocalPlayer ? "Valid" : "Null") << ", Is now " << (foundPlayer ? "Valid" : "Null")
        //                    << " (GUID: 0x" << std::hex << playerGuid64 << ")";
        //     LogMessage(ss.str());
        // }
        m_cachedLocalPlayer = foundPlayer;
    }
}


// Get local player - SIMPLIFIED: Returns the pointer cached by RefreshLocalPlayerCache
std::shared_ptr<WowPlayer> ObjectManager::GetLocalPlayer() {
    // Still check if OM is initialized in general
    if (!IsInitialized()) { 
        // LogMessage("GetLocalPlayer DEBUG: Returning null because !IsInitialized()"); 
        return nullptr;
    }
    // Return the cached pointer (thread-safe read due to mutex)
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    // Debug logging removed, rely on RefreshLocalPlayerCache logs if needed
    return m_cachedLocalPlayer;
}

// Helper for GetLocalPlayer to avoid re-locking mutex
std::shared_ptr<WowObject> ObjectManager::GetObjectByGUID_locked(WGUID guid) {
    // Assumes m_cacheMutex is ALREADY locked by the caller
    auto it = m_objectCache.find(guid);
    return (it != m_objectCache.end()) ? it->second : nullptr;
}

// Get all objects (non-const version)
std::map<WGUID, std::shared_ptr<WowObject>> ObjectManager::GetObjects() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    return m_objectCache; // Return a copy
}

// Get all objects (const version, returns a copy for thread safety)
std::map<WGUID, std::shared_ptr<WowObject>> ObjectManager::GetObjects() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_cacheMutex)); // Need const_cast for const method
    return m_objectCache; // Return a copy
}

// Find objects by name
std::vector<std::shared_ptr<WowObject>> ObjectManager::FindObjectsByName(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    std::vector<std::shared_ptr<WowObject>> results;
    if (name.empty()) return results;

    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

    for (const auto& pair : m_objectCache) {
        if (!pair.second) continue; // Null check
        std::string objName = pair.second->GetName(); // Assumes GetName() adds null check
        if (!objName.empty()) {
             std::string lowerObjName = objName;
             std::transform(lowerObjName.begin(), lowerObjName.end(), lowerObjName.begin(), ::tolower);
            if (lowerObjName.find(lowerName) != std::string::npos) {
                results.push_back(pair.second);
            }
        }
    }
    return results;
}

// Get nearest object of a type
std::shared_ptr<WowObject> ObjectManager::GetNearestObject(WowObjectType type, float maxDistance) {
    // GetLocalPlayer now uses the cached pointer
    auto player = GetLocalPlayer();
    if (!player) return nullptr;
    
    Vector3 playerPos = player->GetPosition(); // Assumes GetPosition is updated
    if (playerPos.x == 0 && playerPos.y == 0 && playerPos.z == 0) return nullptr; // Invalid player pos

    std::shared_ptr<WowObject> nearest = nullptr;
    float nearestDistSq = maxDistance * maxDistance; // Compare squared distances
    
    // Need to lock here to iterate over the cache
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    for (const auto& pair : m_objectCache) {
        if (!pair.second || pair.second->GetType() != type) continue;
        
        Vector3 objPos = pair.second->GetPosition();
        // Calculate squared distance
        float dx = playerPos.x - objPos.x;
        float dy = playerPos.y - objPos.y;
        float dz = playerPos.z - objPos.z;
        float distSq = dx*dx + dy*dy + dz*dz;
        
        if (distSq < nearestDistSq) {
            nearestDistSq = distSq;
            nearest = pair.second;
        }
    }
    
    return nearest;
}

// Returns the underlying pointer to the actual game object manager
ObjectManagerActual* ObjectManager::GetInternalObjectManagerPtr() const {
    // Add a check for initialization? Or assume caller checks IsInitialized?
    // For now, just return the pointer. Caller should check IsInitialized() first.
    return m_objectManagerPtr;
}

// Add implementation for GetCurrentTargetGUID
uint64_t ObjectManager::GetCurrentTargetGUID() const {
    constexpr uintptr_t ADDR_CurrentTargetGUID = 0x00BD07B0; // Address found in spells_tab.cpp
    try {
        // Use MemoryReader to read the GUID directly from the game memory
        return MemoryReader::Read<uint64_t>(ADDR_CurrentTargetGUID);
    } catch (const std::exception& e) {
        // Log the error if the read fails
        LogStream ssErr; ssErr << "ObjectManager::GetCurrentTargetGUID EXCEPTION reading 0x" << std::hex << ADDR_CurrentTargetGUID << ": " << e.what();
        LogMessage(ssErr.str());
        return 0; // Return 0 if read fails
    } catch (...) {
        LogStream ssErr; ssErr << "ObjectManager::GetCurrentTargetGUID Unknown EXCEPTION reading 0x" << std::hex << ADDR_CurrentTargetGUID;
        LogMessage(ssErr.str());
        return 0; // Return 0 on unknown exception
    }
}

// Get objects within a certain distance
std::vector<std::shared_ptr<WowObject>> ObjectManager::GetObjectsWithinDistance(const Vector3& center, float distance) {
    std::vector<std::shared_ptr<WowObject>> results; // Declare results
    float distSqThreshold = distance * distance;
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    for (const auto& pair : m_objectCache) {
        if (pair.second) { // Check if object exists
            Vector3 objPos = pair.second->GetPosition(); // Assuming GetPosition is available and reads directly
            float dx = center.x - objPos.x;
            float dy = center.y - objPos.y;
            float dz = center.z - objPos.z;
            float distSq = dx*dx + dy*dy + dz*dz;
            if (distSq <= distSqThreshold) {
                results.push_back(pair.second);
            }
        }
    }
    return results;
}