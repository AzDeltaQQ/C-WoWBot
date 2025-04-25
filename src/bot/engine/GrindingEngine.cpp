#include "GrindingEngine.h"
#include "../../utils/log.h" // Assuming log utility exists
#include "../../utils/memory.h" // Include MemoryReader/Writer
#include "../game/wowobject.h" // Corrected include for Vector3
#include "../core/BotController.h" // Include BotController
#include "../pathing/PathManager.h" // Need full definition
#include "../core/MovementController.h" // Need full definition and GetInstance()
#include "../game/ObjectManager.h" // Need for player position
// #include "WowPlayer.h"   // Removed incorrect include

// Include other necessary headers e.g., ObjectManager, SpellManager, etc.
// #include "../../game/ObjectManager.h"
// #include "../../game/SpellManager.h"
// #include "../pathing/PathManager.h"

#include <thread> // Include for std::thread
#include <chrono> // For sleep
#include <cmath>  // For std::sqrt
// #include <cmath>  // For std::atan2 - REMOVED

// --- TEST: Define address suspected to hold the Local Player Pointer ---
// const uintptr_t GLOBAL_PLAYER_PTR_ADDR = 0xBD08F4;
// ----------------------------------------------------------------------

GrindingEngine::GrindingEngine(BotController* botController, ObjectManager* objectManager)
    : m_botController(botController), m_objectManager(objectManager)
{
    LogMessage("GrindingEngine: Instance created.");
}

GrindingEngine::~GrindingEngine() {
    stop(); // Ensure thread is stopped and joined
    LogMessage("GrindingEngine: Instance destroyed.");
}

void GrindingEngine::start() {
    if (m_isRunning) {
        LogMessage("GrindingEngine: Start requested but already running.");
        return;
    }
     // Prevent starting if stop was already requested and thread hasn't finished
    if (m_engineThread.joinable()) {
         LogMessage("GrindingEngine: Cannot start, previous stop request still processing.");
         return;
    }

    LogMessage("GrindingEngine: Starting...");
    m_stopRequested = false;
    m_isRunning = true; // Set running before starting thread
    // Start the runLoop in a separate thread
    m_engineThread = std::thread(&GrindingEngine::runLoop, this);
    // m_engineThread.detach(); // Alternatively, detach if you don't need to join explicitly on stop
    LogMessage("GrindingEngine: Started runLoop thread."); 
}

void GrindingEngine::stop() {
    if (!m_isRunning && !m_engineThread.joinable()) { // Only stop if running or thread exists
        return;
    }
    
    LogMessage("GrindingEngine: Stopping...");
    m_stopRequested = true; // Signal the loop to stop
    // m_isRunning = false; // REMOVED: Don't set running false prematurely
    
    // Join the thread if it exists and is joinable
    if (m_engineThread.joinable()) {
        LogMessage("GrindingEngine: Joining engine thread...");
        try {
            m_engineThread.join(); // Wait for runLoop to finish
            LogMessage("GrindingEngine: Engine thread joined.");
        } catch (const std::system_error& e) {
            char errorMsg[256];
            snprintf(errorMsg, sizeof(errorMsg), "GrindingEngine Error: Failed to join thread (%s)", e.what());
            LogMessage(errorMsg);
            // If join fails, the state might be inconsistent. 
            // Consider how to handle this - maybe force m_isRunning false?
        }
    }
    
    // State is fully stopped only after joining (or if join fails maybe?)
    m_isRunning = false; // Set running false AFTER thread is joined
    LogMessage("GrindingEngine: Stopped.");
}

bool GrindingEngine::isRunning() const {
    // Consider checking thread completion if needed, but atomic flag is usually sufficient for GUI
    return m_isRunning;
}

// // Helper function to calculate facing angle (radians) - REMOVED
// inline float CalculateFacing(const Vector3& currentPos, const Vector3& targetPos) {
//     // Use standard X and Y coordinates for atan2
//     return std::atan2(targetPos.y - currentPos.y, targetPos.x - currentPos.x);
// }

void GrindingEngine::runLoop() {
    LogMessage("GrindingEngine: runLoop started.");

    // Check BotController
    if (!m_botController) {
        LogMessage("GrindingEngine Error: BotController is null, stopping loop.");
        m_isRunning = false;
        return;
    }
    // Check ObjectManager
    if (!m_objectManager) { 
        LogMessage("GrindingEngine Error: ObjectManager is null, stopping loop.");
        m_isRunning = false;
        return;
    }
    
    // Get PathManager & MovementController
    const PathManager* pathManager = m_botController->getPathManager();
    MovementController& movementController = MovementController::GetInstance(); 
    if (!pathManager) {
        LogMessage("GrindingEngine Error: PathManager is null, stopping loop.");
        m_isRunning = false;
        return;
    }

    // Get Path
    const std::vector<Vector3>& path = pathManager->getPath();
    if (path.empty()) {
        LogMessage("GrindingEngine: No path loaded, stopping engine.");
        m_isRunning = false; // No path, nothing to do.
        return;
    }

    LogMessage("GrindingEngine: Starting path following.");
    size_t currentPathIndex = 0;
    // bool needsFacingAndFirstMove = true; // REMOVED Flag 
    const float reach_threshold = 3.0f; 
    const float reach_threshold_sq = reach_threshold * reach_threshold;

    while (!m_stopRequested) {
        if (currentPathIndex >= path.size()) {
             LogMessage("GrindingEngine: Reached end of path. Looping..."); // Simplified log
             currentPathIndex = 0; // Loop back
             // needsFacingAndFirstMove = true; // REMOVED 
             if (path.empty()) { 
                 LogMessage("GrindingEngine: Path is empty after loop, stopping.");
                 break; 
             }
        }

        Vector3 currentTarget = path[currentPathIndex];
        
        // --- Get Player Pointer and Position --- 
        Vector3 playerPosition = {0, 0, 0};
        WowPlayer* playerPtr = nullptr;
        if (m_objectManager) {
            auto player = m_objectManager->GetLocalPlayer(); 
            if (player) {
                player->UpdateDynamicData();
                playerPosition = player->GetPosition(); 
                playerPtr = dynamic_cast<WowPlayer*>(player.get()); 
                if (!playerPtr) {
                     LogMessage("GrindingEngine Warning: Failed to cast player object to WowPlayer*."); // Simplified log
                }
            } else {
                LogMessage("GrindingEngine Warning: GetLocalPlayer returned null. Cannot get position."); // Simplified log
                std::this_thread::sleep_for(std::chrono::milliseconds(1000)); 
                continue;
            }
        } else {
             LogMessage("GrindingEngine Error: ObjectManager is null");
             std::this_thread::sleep_for(std::chrono::milliseconds(1000)); 
             continue;
        }
        // -----------------------------------------

        // Calculate squared distance
        float dx = playerPosition.x - currentTarget.x;
        float dy = playerPosition.y - currentTarget.y;
        float distSq = dx * dx + dy * dy;

        // Check if we are close enough to the current target point
        if (distSq < reach_threshold_sq) {
            char logBufferReach[150];
            snprintf(logBufferReach, sizeof(logBufferReach), "GrindingEngine: Reached point %zu (Pos: %.2f, %.2f). Moving to next.", 
                     currentPathIndex + 1, playerPosition.x, playerPosition.y);
            LogMessage(logBufferReach);
            
            currentPathIndex++;
            // needsFacingAndFirstMove = true; // REMOVED
            continue; // Immediately check next point or loop condition
        }

        // --- Movement Logic (Restored to original) ---
        // Just continue moving towards the current target via memory write
        char logBufferCTM[250];
        snprintf(logBufferCTM, sizeof(logBufferCTM), "GrindingEngine: Triggering CTM towards point %zu (Target: %.2f, %.2f, %.2f, Player: %.2f, %.2f, %.2f)",
                    currentPathIndex + 1,
                    currentTarget.x, currentTarget.y, currentTarget.z,
                    playerPosition.x, playerPosition.y, playerPosition.z);
        LogMessage(logBufferCTM);
        movementController.ClickToMove(currentTarget, playerPosition);
        // --- End Movement Logic ---

        // Sleep 
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    m_isRunning = false;
    LogMessage("GrindingEngine: runLoop finished.");
} 