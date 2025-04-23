#include "objectmanager.h"
#include "log.h" // Include the new log header
#include <algorithm>
#include <cmath>
#include <stdexcept> // For exception handling
#include <string>    // Needed for std::to_string
#include <sstream>   // Needed for string streams
#include <iomanip>   // For std::setw and std::setfill

// Initialize static instance
ObjectManager* ObjectManager::m_instance = nullptr;

// Constructor
ObjectManager::ObjectManager() 
    : m_enumVisibleObjects(nullptr),
      m_getObjectPtrByGuidInner(nullptr),
      m_objectManagerPtr(nullptr),
      m_isFullyInitialized(false) // Initialize flag to false
{
    m_localPlayerGuid = { 0, 0 };
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

// TryFinishInitialization attempts to read the game pointers
bool ObjectManager::TryFinishInitialization() {
    // If already initialized, don't try again
    if (m_isFullyInitialized) {
        return true;
    }

    std::stringstream ssInit;
    ssInit << "ObjectManager::TryFinishInitialization Attempting...\n";
    // Log attempt only once maybe? Or add verbosity level
    // LogMessage(ssInit.str()); 
    // ssInit.str(""); 
    
    ObjectManagerActual* tempObjMgrPtr = nullptr;
    WGUID tempLocalGuid = {0, 0};

    try {
        DWORD clientConnection = *reinterpret_cast<DWORD*>(STATIC_CLIENT_CONNECTION);
        ssInit << "  ClientConnection Addr: 0x" << std::hex << STATIC_CLIENT_CONNECTION << ", Value: 0x" << clientConnection << "\n";
        if (!clientConnection) { 
             // Don't log error every frame, just return false
             return false; 
        }
        
        tempObjMgrPtr = *reinterpret_cast<ObjectManagerActual**>(clientConnection + OBJECT_MANAGER_OFFSET);
        ssInit << "  ObjectManager Ptr Addr: 0x" << std::hex << (clientConnection + OBJECT_MANAGER_OFFSET) << ", Value: 0x" << tempObjMgrPtr << "\n";
        if (!tempObjMgrPtr) { 
            // Object Manager pointer is still NULL, wait longer
            return false; 
        }
        
        uintptr_t guidReadAddr = reinterpret_cast<uintptr_t>(tempObjMgrPtr) + LOCAL_GUID_OFFSET;
        tempLocalGuid = *reinterpret_cast<WGUID*>(guidReadAddr);
        // Format GUID as single 64-bit hex value
        ssInit << "  Local Player GUID Addr: 0x" << std::hex << guidReadAddr 
               << ", Value: 0x" << std::hex << std::setw(16) << std::setfill('0') << GuidToUint64(tempLocalGuid) << "\n";
        
        if (!tempLocalGuid.IsValid()) {
             ssInit << "  WARN: Local player GUID read as invalid/zero.\n";
        }

    } catch (const std::exception& e) {
        ssInit << "  EXCEPTION during TryFinishInitialization: " << e.what() << "\n";
        LogMessage(ssInit.str()); // Log exceptions
        return false; // Failed due to exception
    }

    // --- Success! Store pointers and set flag --- 
    m_objectManagerPtr = tempObjMgrPtr;
    m_localPlayerGuid = tempLocalGuid;
    m_isFullyInitialized = true;
                   
    ssInit << "TryFinishInitialization Succeeded! Object Manager ready.\n";
    LogMessage(ssInit.str());
    
    return true;
}

// Callback for object enumeration - Needs to check IsInitialized
int __cdecl ObjectManager::EnumObjectsCallback(uint32_t guid_low, uint32_t guid_high, int callback_arg) {
    ObjectManager* instance = reinterpret_cast<ObjectManager*>(callback_arg);
    // Check full initialization before proceeding
    if (!instance || !instance->IsInitialized() || !instance->m_objectManagerPtr || !instance->m_getObjectPtrByGuidInner) return 0; 

    WGUID guid = { guid_low, guid_high };
    void* objPtr = nullptr;
    
    std::stringstream ssCallback;
    // Format GUID as single 64-bit hex value
    ssCallback << "EnumCallback: GUID=0x" << std::hex << std::setw(16) << std::setfill('0') << GuidToUint64(guid);
    
    try {
         objPtr = instance->m_getObjectPtrByGuidInner(instance->m_objectManagerPtr, guid.guid_low, &guid);
         ssCallback << " -> Ptr=0x" << std::hex << objPtr;
    } catch (const std::exception& e) {
        ssCallback << " -> EXCEPTION calling GetObjectPtrByGuidInner: " << e.what();
        LogMessage(ssCallback.str() + "\n");
        return 1; // Continue enumeration
    }

    if (!objPtr) { 
        return 1; 
    }

    WowObjectType type = OBJECT_NONE;
    try {
        uintptr_t typeAddr = reinterpret_cast<uintptr_t>(objPtr) + OBJECT_TYPE_OFFSET;
        type = *reinterpret_cast<WowObjectType*>(typeAddr);
        ssCallback << ", TypeAddr=0x" << std::hex << typeAddr << ", TypeVal=" << std::dec << static_cast<int>(type);
        if (type < OBJECT_NONE || type > OBJECT_CORPSE) {
             ssCallback << " (Invalid Type!) -> None";
             type = OBJECT_NONE;
        }
    } catch (const std::exception& e) {
         ssCallback << " -> EXCEPTION reading Object Type: " << e.what();
         LogMessage(ssCallback.str() + "\n");
         return 1; 
    }

    if (type != OBJECT_NONE) { 
        ssCallback << " -> Caching...";
        std::shared_ptr<WowObject> obj;
        switch (type) {
            case OBJECT_PLAYER:     obj = std::make_shared<WowPlayer>(objPtr, guid); ssCallback << "(Player)"; break;
            case OBJECT_UNIT:       obj = std::make_shared<WowUnit>(objPtr, guid); ssCallback << "(Unit)"; break;
            case OBJECT_GAMEOBJECT: obj = std::make_shared<WowGameObject>(objPtr, guid); ssCallback << "(GameObject)"; break;
            default:                obj = std::make_shared<WowObject>(objPtr, guid, type); ssCallback << "(Other:" << static_cast<int>(type) << ")"; break;
        }
        if (obj) { 
             ssCallback << " OK";
             instance->m_objectCache[guid] = obj;
        } else {
             ssCallback << " FAILED (make_shared)";
        }
    } else {
        ssCallback << " -> Skipping (Type None)";
    }
    
    LogMessage(ssCallback.str() + "\n"); // Log the final result for this GUID
    
    return 1; // Continue enumeration
}

// Update object cache - Needs to check IsInitialized
void ObjectManager::Update() {
    // Don't try to update if not fully initialized
    if (!IsInitialized()) {
        LogMessage("ObjectManager::Update skipped: Not fully initialized yet.\n");
        return;
    }
    
    m_objectCache.clear();
    if (m_enumVisibleObjects) {
        try {
            m_enumVisibleObjects(EnumObjectsCallback, reinterpret_cast<int>(this));
        } catch (const std::exception& e) {
             LogMessage(std::string("ObjectManager::Update EXCEPTION during enumeration: ") + e.what() + "\n");
             m_objectCache.clear(); 
        }
    }
}

// Get object by GUID
std::shared_ptr<WowObject> ObjectManager::GetObjectByGUID(WGUID guid) {
    auto it = m_objectCache.find(guid);
    return (it != m_objectCache.end()) ? it->second : nullptr;
}

// Get objects by type
std::vector<std::shared_ptr<WowObject>> ObjectManager::GetObjectsByType(WowObjectType type) {
    std::vector<std::shared_ptr<WowObject>> results;
    for (const auto& pair : m_objectCache) {
        if (pair.second && pair.second->GetType() == type) {
            results.push_back(pair.second);
        }
    }
    return results;
}

// Get local player - Updated to use __thiscall function if needed
std::shared_ptr<WowPlayer> ObjectManager::GetLocalPlayer() {
    // Ensure fully initialized before trying to get player
    if (!IsInitialized()) { 
        return nullptr;
    }
    
    // Proceed with previous logic (cache check -> direct lookup -> fallback)
    if (m_localPlayerGuid.IsValid()) {
        // 1. Check Cache first (most efficient if Update() was called recently)
        auto objFromCache = GetObjectByGUID(m_localPlayerGuid);
        if (objFromCache && objFromCache->GetType() == OBJECT_PLAYER) {
            return std::static_pointer_cast<WowPlayer>(objFromCache);
        }
        
        // 2. If not in cache, try direct lookup using the __thiscall function
        if (m_objectManagerPtr && m_getObjectPtrByGuidInner) {
             void* playerPtr = nullptr;
             try {
                  // Create temporary GUID struct to pass its pointer
                  WGUID localGuidCopy = m_localPlayerGuid;
                  playerPtr = m_getObjectPtrByGuidInner(m_objectManagerPtr, m_localPlayerGuid.guid_low, &localGuidCopy);
             } catch (const std::exception&) { /* Call failed */ }
             
             if (playerPtr) {
                 // Verify type before returning
                 try {
                     WowObjectType type = *reinterpret_cast<WowObjectType*>(reinterpret_cast<uintptr_t>(playerPtr) + OBJECT_TYPE_OFFSET);
                     if (type == OBJECT_PLAYER) {
                         // Create a temporary shared_ptr - it won't be cached here unless Update() runs
                         return std::make_shared<WowPlayer>(playerPtr, m_localPlayerGuid);
                     }
                 } catch(const std::exception&) { /* Failed to read type */ }
            }
        }
    }
    // 3. Fallback: If direct reads failed, try searching the current cache (less reliable)
    auto players = GetObjectsByType(OBJECT_PLAYER);
    return !players.empty() ? std::static_pointer_cast<WowPlayer>(players[0]) : nullptr;
}

// Get all objects - Returns the map
std::map<WGUID, std::shared_ptr<WowObject>> ObjectManager::GetObjects() {
    return m_objectCache; 
}

// Find objects by name
std::vector<std::shared_ptr<WowObject>> ObjectManager::FindObjectsByName(const std::string& name) {
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
    auto player = GetLocalPlayer();
    if (!player) return nullptr;
    
    Vector3 playerPos = player->GetPosition(); // Assumes GetPosition is updated
    if (playerPos.x == 0 && playerPos.y == 0 && playerPos.z == 0) return nullptr; // Invalid player pos

    std::shared_ptr<WowObject> nearest = nullptr;
    float nearestDistSq = maxDistance * maxDistance; // Compare squared distances
    
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