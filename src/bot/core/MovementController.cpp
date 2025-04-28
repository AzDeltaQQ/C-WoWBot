#include "MovementController.h"
#include "../../utils/log.h" // For logging
#include <cstdio> // Include for snprintf
#include "../../utils/memory.h" // Include for memory writing utility
#include <chrono> // For system time
#include "../game/objectmanager.h" // Moved include here

// Define the static instance member
MovementController* MovementController::m_instance = nullptr;

MovementController::MovementController() {
    // Constructor logic (if any)
}

MovementController& MovementController::GetInstance() {
    if (!m_instance) {
        m_instance = new MovementController();
    }
    return *m_instance;
}

// Initialization function for the CTM game function pointer
bool MovementController::InitializeClickHandler(uintptr_t handlerAddress) {
    if (!handlerAddress) {
        LogMessage("MovementController Error: Click Handler Address is null.");
        return false;
    }
    m_handleClickToMoveFunc = reinterpret_cast<HandleClickToMoveFn>(handlerAddress);
    char logBuffer[150];
    snprintf(logBuffer, sizeof(logBuffer), "MovementController: Initialized with Click handler at 0x%p", (void*)handlerAddress); 
    LogMessage(logBuffer);
    return true;
}

// CTM via Direct Memory Write (More fields + Start Pos)
bool MovementController::ClickToMove(const Vector3& targetPos, const Vector3& playerPos) {
    try {
        // Log the action
        char logBuffer[250];
        snprintf(logBuffer, sizeof(logBuffer), "MovementController: Writing CTM Data (Target: %.2f, %.2f, %.2f | PlayerStart: %.2f, %.2f, %.2f | Action: %d)", 
                 targetPos.x, targetPos.y, targetPos.z, 
                 playerPos.x, playerPos.y, playerPos.z, 
                 CTMOffsets::ACTION_MOVE);
        LogMessage(logBuffer);

        // Write fields based on handlePlayerClickToMove disassembly
        const uintptr_t BASE = CTMOffsets::BASE_ADDR;
        
        // --- ADDED: Initialize first four floats ---
        MemoryWriter::WriteMemory<float>(BASE + 0x0, 6.087f);      // Offset +0x0 (Value observed after manual click)
        MemoryWriter::WriteMemory<float>(BASE + 0x4, 3.1415927f);  // Offset +0x4 (Pi, default loaded in game func)
        MemoryWriter::WriteMemory<float>(BASE + 0x8, 0.0f);        // Offset +0x8 (Interaction Distance for terrain move)
        MemoryWriter::WriteMemory<float>(BASE + 0xC, 0.0f);        // Offset +0xC (sqrt(Interaction Distance))
        // ---------------------------------------------
        
        // 1. Initialize Progress/Timer/State fields to 0
        MemoryWriter::WriteMemory<float>(BASE - 0x8, 0.0f);     // flt_CA11D0
        MemoryWriter::WriteMemory<uint32_t>(BASE - 0x4, 0);     // dword_CA11D4
        MemoryWriter::WriteMemory<uint32_t>(BASE + 0x28, 0);    // dword_CA1200

        // 2. Write Timestamp
        auto now = std::chrono::system_clock::now();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        MemoryWriter::WriteMemory<uint32_t>(BASE + 0x18, static_cast<uint32_t>(millis)); // dword_CA11F0
        
        // 3. Write Target Coordinates (SWAPPED Y, X, Z order for CTM structure)
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::X_OFFSET, targetPos.y); // Write Standard Y to CTM X (+0x8C)
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::Y_OFFSET, targetPos.x); // Write Standard X to CTM Y (+0x90)
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::Z_OFFSET, targetPos.z); // Write Standard Z to CTM Z (+0x94)

        // --- Write Player Start Position (Standard X, Y, Z order) ---
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_X_OFFSET, playerPos.x); // +0x80
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_Y_OFFSET, playerPos.y); // +0x84
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_Z_OFFSET, playerPos.z); // +0x88

        // 5. Clear the GUID in CTM structure
        MemoryWriter::WriteMemory<uint64_t>(BASE + CTMOffsets::GUID_OFFSET, 0); // +0x20
        
        // 6. Set the action type in CTM structure (Action 4)
        MemoryWriter::WriteMemory<uint32_t>(BASE + CTMOffsets::ACTION_OFFSET, 4); // +0x1C = 4 (CTMOffsets::ACTION_MOVE)

        LogMessage("MovementController: CTM data written (Action 4, GUID cleared).");
        return true;

    } catch (const std::exception& e) {
        char errorBuffer[500];
        snprintf(errorBuffer, sizeof(errorBuffer), "MovementController Error: Exception during CTM write: %s", e.what());
        LogMessage(errorBuffer);
        return false;
    } catch (...) {
        LogMessage("MovementController Error: Unknown exception during CTM write");
        return false;
    }
} 

// Stop any current movement by issuing a 'Face Target' CTM action with current position
void MovementController::Stop() {
    LogMessage("MovementController: Issuing Stop() command...");
    try {
        Vector3 currentPos = {0,0,0};
        
        // Get player position - REQUIRES ACCESS TO OBJECT MANAGER DIRECTLY
        ObjectManager* objMgr = ObjectManager::GetInstance();
        if (objMgr && objMgr->IsInitialized()) {
            auto player = objMgr->GetLocalPlayer();
            if (player) {
                void* playerPtr = player->GetPointer();
                if (playerPtr) {
                    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(playerPtr);
                    constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C;
                    constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798;
                    constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0;
                    try {
                         currentPos.x = MemoryReader::Read<float>(baseAddr + OBJECT_POS_X_OFFSET);
                         currentPos.y = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Y_OFFSET);
                         currentPos.z = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Z_OFFSET);
                    } catch (...) { /* Failed to read pos */ }
                }
            }
        }
        
        if (currentPos.x == 0.0f && currentPos.y == 0.0f) { // Check if read failed
            LogMessage("MovementController Stop(): WARNING - Could not get player position. Stop command might fail.");
            // Don't write if position is invalid
            return;
        }
        
        // Write CTM Action 1 (Face Target/Stop) with current position
        const uintptr_t BASE = CTMOffsets::BASE_ADDR;
        
        // Initialize base floats (same as ClickToMove)
        MemoryWriter::WriteMemory<float>(BASE + 0x0, 6.087f);      
        MemoryWriter::WriteMemory<float>(BASE + 0x4, 3.1415927f);  
        MemoryWriter::WriteMemory<float>(BASE + 0x8, 0.0f);        
        MemoryWriter::WriteMemory<float>(BASE + 0xC, 0.0f);        
        
        // Reset progress fields
        MemoryWriter::WriteMemory<float>(BASE - 0x8, 0.0f);     
        MemoryWriter::WriteMemory<uint32_t>(BASE - 0x4, 0);     
        MemoryWriter::WriteMemory<uint32_t>(BASE + 0x28, 0);    

        // Write Timestamp
        auto now = std::chrono::system_clock::now();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        MemoryWriter::WriteMemory<uint32_t>(BASE + 0x18, static_cast<uint32_t>(millis)); 
        
        // Write Target Coordinates (using current position)
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::X_OFFSET, currentPos.y); // CTM X = Player Y
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::Y_OFFSET, currentPos.x); // CTM Y = Player X
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::Z_OFFSET, currentPos.z); // CTM Z = Player Z

        // Write Player Start Position (also current position for stop)
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_X_OFFSET, currentPos.x);
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_Y_OFFSET, currentPos.y);
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_Z_OFFSET, currentPos.z);

        // Clear the GUID 
        MemoryWriter::WriteMemory<uint64_t>(BASE + CTMOffsets::GUID_OFFSET, 0); 
        
        // Set the action type to 3 (Stop)
        const uint32_t ACTION_STOP = 3; // Use the correct action code for stopping
        MemoryWriter::WriteMemory<uint32_t>(BASE + CTMOffsets::ACTION_OFFSET, ACTION_STOP); 

        LogMessage("MovementController: Stop CTM action (3) written.");
        
    } catch (const std::exception& e) {
        LogStream ssErr; ssErr << "MovementController Stop() EXCEPTION: " << e.what(); LogMessage(ssErr.str());
    } catch (...) {
        LogMessage("MovementController Stop(): Unknown exception.");
    }
}

// Face a specific target by issuing CTM Action 1 with the target's GUID
void MovementController::FaceTarget(uint64_t targetGuid) {
    LogStream ssLog; ssLog << "MovementController: Issuing FaceTarget() command for GUID 0x" << std::hex << targetGuid; LogMessage(ssLog.str());
    if (targetGuid == 0) {
        LogMessage("MovementController FaceTarget(): Warning - Provided GUID is 0. Cannot face null target.");
        return;
    }
    
    try {
        // Get Player Position (Needs refinement)
        Vector3 currentPos = {0,0,0};
        ObjectManager* objMgr = ObjectManager::GetInstance();
        if (objMgr && objMgr->IsInitialized()) {
            auto player = objMgr->GetLocalPlayer();
            if (player) {
                void* playerPtr = player->GetPointer();
                if (playerPtr) {
                    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(playerPtr);
                    constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C;
                    constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798;
                    constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0;
                    try {
                         currentPos.x = MemoryReader::Read<float>(baseAddr + OBJECT_POS_X_OFFSET);
                         currentPos.y = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Y_OFFSET);
                         currentPos.z = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Z_OFFSET);
                    } catch (...) { /* Failed to read pos */ }
                }
            }
        }
        if (currentPos.x == 0.0f && currentPos.y == 0.0f) { 
            LogMessage("MovementController FaceTarget(): WARNING - Could not get player position. Face command might fail.");
            return;
        }
        
        // Write CTM Action 1 (Face Target) with target GUID and current position
        const uintptr_t BASE = CTMOffsets::BASE_ADDR;
        
        // Initialize base floats 
        MemoryWriter::WriteMemory<float>(BASE + 0x0, 6.087f);      
        MemoryWriter::WriteMemory<float>(BASE + 0x4, 3.1415927f);  
        MemoryWriter::WriteMemory<float>(BASE + 0x8, 0.0f);        
        MemoryWriter::WriteMemory<float>(BASE + 0xC, 0.0f);        
        
        // Reset progress fields
        MemoryWriter::WriteMemory<float>(BASE - 0x8, 0.0f);     
        MemoryWriter::WriteMemory<uint32_t>(BASE - 0x4, 0);     
        MemoryWriter::WriteMemory<uint32_t>(BASE + 0x28, 0);    

        // Write Timestamp
        auto now = std::chrono::system_clock::now();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        MemoryWriter::WriteMemory<uint32_t>(BASE + 0x18, static_cast<uint32_t>(millis)); 
        
        // Write Target Coordinates (using current position for facing action)
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::X_OFFSET, currentPos.y); // CTM X = Player Y
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::Y_OFFSET, currentPos.x); // CTM Y = Player X
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::Z_OFFSET, currentPos.z); // CTM Z = Player Z

        // Write Player Start Position (also current position)
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_X_OFFSET, currentPos.x);
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_Y_OFFSET, currentPos.y);
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_Z_OFFSET, currentPos.z);

        // Write the Target GUID 
        MemoryWriter::WriteMemory<uint64_t>(BASE + CTMOffsets::GUID_OFFSET, targetGuid); 
        
        // Set the action type to 1 (Face Target)
        const uint32_t ACTION_FACE_TARGET = 1; 
        MemoryWriter::WriteMemory<uint32_t>(BASE + CTMOffsets::ACTION_OFFSET, ACTION_FACE_TARGET); 

        LogMessage("MovementController: Face Target CTM action (1) written.");
        
    } catch (const std::exception& e) {
        LogStream ssErr; ssErr << "MovementController FaceTarget() EXCEPTION: " << e.what(); LogMessage(ssErr.str());
    } catch (...) {
        LogMessage("MovementController FaceTarget(): Unknown exception.");
    }
}

// CTM via Direct Memory Write for Right Clicking
void MovementController::RightClickAt(const Vector3& targetPos) {
    LogStream ssLog; ssLog << "MovementController: Issuing RightClickAt() command for Target: " 
                           << targetPos.x << ", " << targetPos.y << ", " << targetPos.z;
    LogMessage(ssLog.str());
    
    try {
        // 1. Get Player Position (Same HACK as Stop()/FaceTarget() - needs refinement)
        Vector3 currentPos = {0,0,0};
        ObjectManager* objMgr = ObjectManager::GetInstance(); // Assumes include is at top
        if (objMgr && objMgr->IsInitialized()) {
            auto player = objMgr->GetLocalPlayer();
            if (player) {
                void* playerPtr = player->GetPointer();
                if (playerPtr) {
                    uintptr_t baseAddr = reinterpret_cast<uintptr_t>(playerPtr);
                    constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C;
                    constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798;
                    constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0;
                    try {
                         currentPos.x = MemoryReader::Read<float>(baseAddr + OBJECT_POS_X_OFFSET);
                         currentPos.y = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Y_OFFSET);
                         currentPos.z = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Z_OFFSET);
                    } catch (...) { /* Failed to read pos */ }
                }
            }
        }
        if (currentPos.x == 0.0f && currentPos.y == 0.0f) { 
            LogMessage("MovementController RightClickAt(): WARNING - Could not get player position. Right click might fail.");
            return;
        }

        // 2. Write CTM Action 6 (Interact Target) 
        const uintptr_t BASE = CTMOffsets::BASE_ADDR;
        
        // Initialize base floats
        MemoryWriter::WriteMemory<float>(BASE + 0x0, 6.087f);      
        MemoryWriter::WriteMemory<float>(BASE + 0x4, 3.1415927f);  
        MemoryWriter::WriteMemory<float>(BASE + 0x8, 0.0f);        // Interaction distance for right-click?
        MemoryWriter::WriteMemory<float>(BASE + 0xC, 0.0f);        
        
        // Reset progress fields
        MemoryWriter::WriteMemory<float>(BASE - 0x8, 0.0f);     
        MemoryWriter::WriteMemory<uint32_t>(BASE - 0x4, 0);     
        MemoryWriter::WriteMemory<uint32_t>(BASE + 0x28, 0);    

        // Write Timestamp
        auto now = std::chrono::system_clock::now();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        MemoryWriter::WriteMemory<uint32_t>(BASE + 0x18, static_cast<uint32_t>(millis)); 
        
        // Write Target Coordinates (where to right-click)
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::X_OFFSET, targetPos.y); // CTM X = Target Y
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::Y_OFFSET, targetPos.x); // CTM Y = Target X
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::Z_OFFSET, targetPos.z); // CTM Z = Target Z

        // Write Player Start Position (current position)
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_X_OFFSET, currentPos.x);
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_Y_OFFSET, currentPos.y);
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_Z_OFFSET, currentPos.z);

        // Clear the GUID (since we are clicking a position, not a unit)
        MemoryWriter::WriteMemory<uint64_t>(BASE + CTMOffsets::GUID_OFFSET, 0); 
        
        // Set the action type to 6 (Interact Target? Need to confirm this is correct for right-click)
        const uint32_t ACTION_INTERACT = 6; 
        MemoryWriter::WriteMemory<uint32_t>(BASE + CTMOffsets::ACTION_OFFSET, ACTION_INTERACT); 

        LogMessage("MovementController: Right Click CTM action (6) written.");

    } catch (const std::exception& e) {
        LogStream ssErr; ssErr << "MovementController RightClickAt() EXCEPTION: " << e.what(); LogMessage(ssErr.str());
    } catch (...) {
        LogMessage("MovementController RightClickAt(): Unknown exception.");
    }
}