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
    const uint32_t X_OFFSET = 0x8C;                 // CTM_X 
    const uint32_t Y_OFFSET = 0x90;                 // CTM_Y
    const uint32_t Z_OFFSET = 0x94;                 // CTM_Z
    const uint32_t START_X_OFFSET = 0x80;           // dword_CA1258
    const uint32_t START_Y_OFFSET = 0x84;           // dword_CA125C
    const uint32_t START_Z_OFFSET = 0x88;           // dword_CA1260
    const uint32_t GUID_OFFSET = 0x20;              // CTM_GUID
    const uint32_t ACTION_OFFSET = 0x1C;            // CTM_Action
    const uintptr_t ACTIVATE_PTR = 0xBD08F4;        // CTM_Activate_Pointer
    const uint32_t ACTIVATE_OFFSET = 0x30;          // CTM_Activate_Offset
        
    const uint32_t ACTION_MOVE = 4; // Action type 4 seems correct for initiating terrain click move
}

class MovementController {
private:
    HandleClickToMoveFn m_handleClickToMoveFunc = nullptr;
    
    MovementController(); 
    
    static MovementController* m_instance;

public:
    MovementController(const MovementController&) = delete;
    MovementController& operator=(const MovementController&) = delete;
    
    static MovementController& GetInstance();

    bool InitializeClickHandler(uintptr_t handlerAddress);
    
    bool ClickToMove(const Vector3& targetPos, const Vector3& playerPos);

    void RightClickAt(const Vector3& targetPos);

    void Stop();

    void FaceTarget(uint64_t targetGuid);

};