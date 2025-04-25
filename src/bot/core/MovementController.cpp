#include "MovementController.h"
#include "../../utils/log.h" // For logging
#include <cstdio> // Include for snprintf
#include "../../utils/memory.h" // Include for memory writing utility
#include <chrono> // For system time

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

        // --- NEW: Write Player Start Position (Standard X, Y, Z order) ---
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_X_OFFSET, playerPos.x); // +0x80
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_Y_OFFSET, playerPos.y); // +0x84
        MemoryWriter::WriteMemory<float>(BASE + CTMOffsets::START_Z_OFFSET, playerPos.z); // +0x88
        // ------------------------------------------------------------------

        // 5. Clear the GUID in CTM structure - RE-ENABLED
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