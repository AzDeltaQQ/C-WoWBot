#pragma once

#include <cstdint> // For uint64_t
#include "../game/wowobject.h" // For Vector3, WowPlayer

// Forward declare WowPlayer for function signature
class WowPlayer;

// Function signature for the game's core CTM function
typedef char (__thiscall* HandleClickToMoveFn)(WowPlayer* player, int type, uint64_t* guidPtr, float* positionPtr, float facing);

// Constants for Click-to-Move system memory writing
namespace CTMOffsets {
    const uintptr_t BASE_ADDR = 0x00CA11D8;         // CTM_Base
    // Offsets for WRITING coordinates
    const uint32_t X_OFFSET = 0x8C;                 // CTM_X 
    const uint32_t Y_OFFSET = 0x90;                 // CTM_Y
    const uint32_t Z_OFFSET = 0x94;                 // CTM_Z
    // Offsets for WRITING player start position?
    const uint32_t START_X_OFFSET = 0x80;           // dword_CA1258
    const uint32_t START_Y_OFFSET = 0x84;           // dword_CA125C
    const uint32_t START_Z_OFFSET = 0x88;           // dword_CA1260
    // Other offsets
    const uint32_t GUID_OFFSET = 0x20;              // CTM_GUID
    // Offset for the Action Code
    const uint32_t ACTION_OFFSET = 0x1C;            // CTM_Action
    // Activation Pointer & Offset
    const uintptr_t ACTIVATE_PTR = 0xBD08F4;        // CTM_Activate_Pointer
    const uint32_t ACTIVATE_OFFSET = 0x30;          // CTM_Activate_Offset
        
    // CTM Action types
    const uint32_t ACTION_MOVE = 4; // Action type 4 seems correct for initiating terrain click move
}

class MovementController {
private:
    HandleClickToMoveFn m_handleClickToMoveFunc = nullptr; // Pointer for calling the game function
    
    // Private constructor (singleton pattern)
    MovementController(); 
    
    static MovementController* m_instance; // Singleton instance

public:
    // Delete copy/assignment for singleton
    MovementController(const MovementController&) = delete;
    MovementController& operator=(const MovementController&) = delete;
    
    // Get the singleton instance
    static MovementController& GetInstance();

    // Initialize the pointer to the game's CTM handler function
    bool InitializeClickHandler(uintptr_t handlerAddress);
    
    // CTM via Direct Memory Write (primary method for pathing)
    bool ClickToMove(const Vector3& targetPos, const Vector3& playerPos);

    // Simulate Right-Clicking on a point (e.g., for interacting with objects)
    void RightClickAt(const Vector3& targetPos);

    // Stop any current CTM movement
    void Stop();

    // Face a specific target using CTM Action 1
    void FaceTarget(uint64_t targetGuid);

    // Destructor (if needed for cleanup, e.g., if m_instance was allocated)
    // ~MovementController(); 
};