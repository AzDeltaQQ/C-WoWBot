#include "GrindingEngine.h"
#include "../../utils/log.h"
#include "../../utils/memory.h" // Include MemoryReader/Writer
#include "../../game/wowobject.h" // Use correct path for WowObject definition
#include "functions.h"   // Use path relative to src include dir
#include "../../game/ObjectManager.h" // Need ObjectManager full def
#include "../../game/SpellManager.h" // Need SpellManager full def
#include "../core/BotController.h" // Include BotController
#include "../pathing/PathManager.h" // Need full definition
#include "../core/MovementController.h" // Include for movement
#include "lua_executor.h" // <<< ADD INCLUDE FOR LUA EXECUTOR

#include <thread> // Include for std::thread
#include <chrono> // For sleep
#include <cmath>  // For std::sqrt
#include <algorithm> // For std::max

// Typedef for convenience (assuming this is defined elsewhere or here)
// using GrindState = GrindingEngine::EngineState; 
// ^^^ IMPORTANT: Ensure this alias or direct use of GrindingEngine::EngineState is consistent

// Constructor - Get necessary pointers
GrindingEngine::GrindingEngine(BotController* controller, ObjectManager* objectManager)
    : m_botController(controller),
      m_objectManager(objectManager), // Assign ObjectManager passed in
      m_spellManager(&SpellManager::GetInstance()), // Use GetInstance() for SpellManager
      m_currentState(EngineState::IDLE) // Use EngineState directly
{
    LogMessage("GrindingEngine: Created.");
}

GrindingEngine::~GrindingEngine() {
    stop(); // Ensure thread is stopped
    LogMessage("GrindingEngine: Destroyed.");
}

void GrindingEngine::start() {
    if (m_isRunning) return;
    LogMessage("GrindingEngine: Starting...");
    m_stopRequested = false;
    m_isRunning = true;
    // --- Initialize state --- 
    m_currentPathIndex = -1; // Reset grind path index
    m_currentVendorPathIndex = -1; // Reset vendor path index
    m_targetUnitPtr = nullptr;
    m_currentState = EngineState::IDLE; // Start idle, will transition quickly
    // ----------------------
    m_engineThread = std::thread(&GrindingEngine::run, this);
    m_engineThread.detach(); // Or join in destructor/stop
}

void GrindingEngine::stop() {
    if (!m_isRunning) return;
    LogMessage("GrindingEngine: Stopping...");
    m_stopRequested = true;
    // Wait for the thread to acknowledge the stop request briefly
    if (m_engineThread.joinable()) {
        // Give it a short time to finish its current loop iteration
        std::this_thread::sleep_for(std::chrono::milliseconds(150)); 
    }
    m_isRunning = false; // Set flag after potential wait
    m_currentState = EngineState::IDLE;
    LogMessage("GrindingEngine: Stopped.");
}

// --- Get Current State --- 
GrindingEngine::EngineState GrindingEngine::getCurrentState() const {
    return m_currentState;
}
// -------------------------

bool GrindingEngine::isRunning() const {
    return m_isRunning && !m_stopRequested;
}

// Main loop running in its own thread
void GrindingEngine::run() {
    LogMessage("GrindingEngine: Run loop started.");

    // --- Initial Wait for Valid Player --- 
    LogMessage("GrindingEngine: Performing initial check for valid player object...");
    const int MAX_INITIAL_WAIT_ATTEMPTS = 20; // e.g., 20 attempts * 100ms = 2 seconds max wait
    bool playerInitiallyValid = false;
    for (int attempt = 0; attempt < MAX_INITIAL_WAIT_ATTEMPTS; ++attempt) {
        if (m_stopRequested) { // Check if stop was requested during wait
            LogMessage("GrindingEngine: Stop requested during initial player wait.");
            m_isRunning = false;
            return;
        }
        if (m_objectManager && m_objectManager->GetLocalPlayer() != nullptr) {
            LogMessage("GrindingEngine: Initial player object check successful.");
            playerInitiallyValid = true;
            break;
        }
        // LogMessage("GrindingEngine: Initial player check failed, waiting..."); // Can be noisy
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Wait before next check
    }

    if (!playerInitiallyValid) {
        LogMessage("GrindingEngine Error: Failed to get valid player object after initial wait. Stopping engine.");
        m_isRunning = false;
        m_currentState = EngineState::ERROR; // Indicate an error state
        // Potentially signal BotController about the failure?
        return; // Exit the run loop
    }
    // --- End Initial Wait --- 

    while (!m_stopRequested) {
        try {
            // Get state before update for logging/debugging if needed
            // EngineState stateBeforeUpdate = m_currentState;

            updateState(); // Execute state machine logic

            // Optional: Log state transition if it changed
            // if (m_currentState != stateBeforeUpdate) {
            //     LogStream ssState; ssState << "GrindingEngine: State changed from " << static_cast<int>(stateBeforeUpdate) << " to " << static_cast<int>(m_currentState);
            //     LogMessage(ssState.str());
            // }

        } catch (const std::exception& e) {
             LogStream ssErr;
             ssErr << "GrindingEngine: Exception in run loop: " << e.what();
             LogMessage(ssErr.str());
             m_currentState = EngineState::ERROR; // Enter error state
        } catch (...) {
             LogMessage("GrindingEngine: Unknown exception in run loop.");
             m_currentState = EngineState::ERROR;
        }

        // Prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Adjust sleep time
    }
    m_isRunning = false; // Ensure flag is updated on exit
    LogMessage("GrindingEngine: Run loop finished.");
}

// Central state machine logic
void GrindingEngine::updateState() {
    if (!m_objectManager || !m_botController || !m_spellManager ) {
         LogMessage("GrindingEngine Error: Core components missing.");
         m_currentState = EngineState::ERROR;
         return;
    }

     // --- Global Checks --- 
     // TODO: Add recovery check based on player health/mana

    // Get current target GUID from the game state
    uint64_t currentTargetGuidInGame = m_objectManager->GetCurrentTargetGUID(); 
    uint64_t knownTargetGuid = GuidToUint64(m_targetUnitPtr ? m_targetUnitPtr->GetGUID() : WGUID{0,0});
    
    // --- Refined Pointer Update Logic --- 
    bool shouldClearPointer = false;

    // Condition 1: We are NOT in COMBAT or LOOTING, and the game target differs from our known target.
    // ADDITION: Also clear if we are returning from vendor (to avoid stale vendor target)
    if (m_currentState != EngineState::COMBAT && 
        m_currentState != EngineState::LOOTING && 
        m_currentState != EngineState::VENDERING && 
        m_currentState != EngineState::MOVING_TO_VENDOR) // Don't clear while moving TO vendor
    {
        if (currentTargetGuidInGame != knownTargetGuid) {
            // LogStream ssClear; ssClear << "Clearing pointer (Not Combat/Looting/Vendering, Game GUID changed)"; LogMessage(ssClear.str());
            shouldClearPointer = true;
        }
    }
    // Condition 2: We ARE in COMBAT, but the game target changed to something *else* (not just cleared to 0).
    else if (m_currentState == EngineState::COMBAT) {
        if (currentTargetGuidInGame != 0 && currentTargetGuidInGame != knownTargetGuid) {
            // LogStream ssClear; ssClear << "Clearing pointer (Combat, Game GUID changed to non-zero)"; LogMessage(ssClear.str());
             shouldClearPointer = true;
        }
        // NOTE: We explicitly DO NOT clear if currentTargetGuidInGame is 0 while in COMBAT,
        // as this likely means the target died, and we want handleCombat to check it.
    }
    // Condition 3: Always clear if our known pointer is non-null but the object is gone from the manager
    if (m_targetUnitPtr && !m_objectManager->GetObjectByGUID(knownTargetGuid)) {
        LogStream ssInvalid; ssInvalid << "GrindingEngine UpdateState: Known target pointer (GUID " << std::hex << knownTargetGuid << ") seems invalid/gone from manager. Clearing pointer.";
        LogMessage(ssInvalid.str());
        shouldClearPointer = true; 
    }

    // Apply pointer update if needed
    if (shouldClearPointer) {
        m_targetUnitPtr = nullptr;
        knownTargetGuid = 0; // Update known GUID since pointer is cleared
        // If pointer was cleared, try to re-acquire based on current game target if it's valid
        // Avoid re-acquiring if just finished vendoring or returning
        if (currentTargetGuidInGame != 0 && 
            m_currentState != EngineState::MOVING_TO_GRIND_SPOT) 
        { 
            // LogStream ssReacquire; ssReacquire << "Pointer cleared, attempting re-acquire for GUID " << std::hex << currentTargetGuidInGame;
            // LogMessage(ssReacquire.str());
            std::shared_ptr<WowObject> tempTargetBase = m_objectManager->GetObjectByGUID(currentTargetGuidInGame);
            m_targetUnitPtr = std::dynamic_pointer_cast<WowUnit>(tempTargetBase);
             // Update knownTargetGuid after potential re-acquire
             knownTargetGuid = GuidToUint64(m_targetUnitPtr ? m_targetUnitPtr->GetGUID() : WGUID{0,0}); 
             // LogStream ssReacquired; ssReacquired << "Re-acquired pointer is now: " << (m_targetUnitPtr ? "Valid" : "Invalid/Null");
             // LogMessage(ssReacquired.str());
        }
    }
    // --- End Refined Pointer Update Logic ---

    // Store the potentially updated game target GUID for use in states
    m_currentTargetGuid = currentTargetGuidInGame; 

    // --- Main State Machine --- 
    switch (m_currentState) {
        case EngineState::IDLE:
            processStateIdle(); // Will transition to CHECK_STATE
            break;

        case EngineState::FINDING_TARGET: // Renamed from CHECK_STATE conceptually
            processStateFindingTarget();
            break;

        case EngineState::MOVING_TO_TARGET: // Renamed from PATHING conceptually
            processStateMovingToTarget();
            break;

        case EngineState::COMBAT:
            processStateCombat(); // Will transition back to FINDING_TARGET or LOOTING if target lost/dies
            break;

        case EngineState::LOOTING:
            processStateLooting(); // Handle moving to and looting the corpse
            break;

        case EngineState::MOVING_TO_CORPSE: // Was RECOVERING, assuming corpse run
            processStateMovingToCorpse();
            break;
        
        // --- NEW VENDOR STATES --- 
        case EngineState::MOVING_TO_VENDOR:
            processStateMovingToVendor();
            break;
        
        case EngineState::VENDERING:
            processStateVendering();
            break;

        case EngineState::MOVING_TO_GRIND_SPOT:
            processStateMovingToGrindSpot();
            break;

        case EngineState::RESTING:
            processStateResting();
            break;

        case EngineState::ERROR:
            LogMessage("GrindingEngine: In ERROR state. Stopping.");
            stop(); // Stop the engine on error
            // Consider signaling BotController too if needed
            break;
        
        default: // Catch unexpected states
             LogStream ssErr; ssErr << "GrindingEngine: Encountered unexpected state: " << static_cast<int>(m_currentState) << ". Resetting to IDLE.";
             LogMessage(ssErr.str());
             m_currentState = EngineState::IDLE;
             break;
    }
}

// --- Bag Check Logic --- 

bool GrindingEngine::checkBagsAndTransition() {
    int freeSlots = GetFreeBagSlots(); // Assumes this function exists and works
    LogStream ssBag; ssBag << "GrindingEngine: Checking bags. Free slots: " << freeSlots;
    LogMessage(ssBag.str());

    if (freeSlots <= BAG_FULL_THRESHOLD) {
        // Bags are full enough to warrant a vendor run
        LogMessage("GrindingEngine: Bags are full. Initiating vendor run.");

        // --- ADDED: Load vendor name --- 
        if (m_botController && m_botController->getPathManager()) {
            m_targetVendorName = m_botController->getPathManager()->getCurrentVendorName();
        } else {
            LogMessage("GrindingEngine Error: Cannot get vendor name, BotController or PathManager missing.");
            m_targetVendorName = ""; // Reset name if unable to load
        }
        // --- END ADDED --- 

        // Store current state to return later
        m_preVendorState = m_currentState; 
        // Store current location if we are pathing
        if (m_preVendorState == EngineState::MOVING_TO_TARGET || m_preVendorState == EngineState::FINDING_TARGET) {
            // Try to get player position reliably with retries
            bool locationStored = false;
            const int MAX_POS_RETRIES = 3;
            for (int retry = 0; retry < MAX_POS_RETRIES; ++retry) {
                std::shared_ptr<WowPlayer> player = m_objectManager->GetLocalPlayer();
                if (player) {
                    void* playerPtr = player->GetPointer();
                    if (playerPtr) {
                        try {
                            uintptr_t baseAddr = reinterpret_cast<uintptr_t>(playerPtr);
                            constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C;
                            constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798;
                            constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0;
                            m_grindSpotLocation.x = MemoryReader::Read<float>(baseAddr + OBJECT_POS_X_OFFSET);
                            m_grindSpotLocation.y = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Y_OFFSET);
                            m_grindSpotLocation.z = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Z_OFFSET);
                            locationStored = true;
                            LogStream ssLoc; ssLoc << "GrindingEngine: Stored grind spot location: (" << m_grindSpotLocation.x << ", " << m_grindSpotLocation.y << ", " << m_grindSpotLocation.z << ")";
                            LogMessage(ssLoc.str());
                            break; // Success
                        } catch (...) {
                            // Log exception on last retry
                            if (retry == MAX_POS_RETRIES - 1) {
                                LogMessage("GrindingEngine Warning: Failed to read player position to store grind spot location (Exception).");
                            }
                        }
                    } else {
                         // Log pointer null on last retry
                         if (retry == MAX_POS_RETRIES - 1) {
                             LogMessage("GrindingEngine Warning: Player pointer null, cannot store exact grind spot location.");
                         }
                    }
                } else {
                    // Log object null on last retry
                    if (retry == MAX_POS_RETRIES - 1) {
                         LogMessage("GrindingEngine Warning: Player object null, cannot store exact grind spot location.");
                    }
                }
                // If not successful, wait before retrying
                if (!locationStored) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }

            // Fallback if location couldn't be stored via direct read
            if (!locationStored) {
                const PathManager* pm = m_botController->getPathManager();
                if (pm && pm->hasPath(PathManager::PathType::GRIND)) {
                    const auto& grindPath = pm->getPath(PathManager::PathType::GRIND);
                    if (!grindPath.empty()) {
                        m_grindSpotLocation = grindPath.back(); // Store last point as fallback
                        LogMessage("GrindingEngine: Using last grind path point as fallback location.");
                    }
                }
            }
        }
        // Make sure we aren't targeting anything before starting vendor run
        if (m_currentTargetGuid != 0) { TargetUnitByGuid(0); }
        m_targetUnitPtr = nullptr;
        m_currentTargetGuid = 0;
        MovementController::GetInstance().Stop(); // Stop any current movement

        // Transition to vendor state
        m_currentState = EngineState::MOVING_TO_VENDOR;
        m_currentVendorPathIndex = -1; // Reset vendor path index
        m_vendorPathLoaded = false; // Reset vendor path loaded flag
        return true; // Transitioned to vendor run
    }
    return false; // Bags not full, no transition needed
}


// --- State Handler Implementations (Renamed) ---

void GrindingEngine::processStateIdle() {
    LogMessage("GrindingEngine IDLE: Transitioning to FINDING_TARGET.");
    m_currentState = EngineState::FINDING_TARGET;
}

// Renamed from handlePathing
void GrindingEngine::processStateMovingToTarget() { 
    const PathManager* pathManager = m_botController->getPathManager();
    if (!pathManager || !pathManager->hasPath(PathManager::PathType::GRIND)) {
        LogMessage("GrindingEngine MOVING_TO_TARGET: No grind path loaded or PathManager missing. Back to FINDING_TARGET.");
        m_currentPathIndex = -1; // Reset path index
        m_currentState = EngineState::FINDING_TARGET;
        return;
    }

    const auto& path = pathManager->getPath(PathManager::PathType::GRIND);
    if (path.empty()) {
        LogMessage("GrindingEngine MOVING_TO_TARGET: Path is empty. Back to FINDING_TARGET.");
        m_currentPathIndex = -1;
        m_currentState = EngineState::FINDING_TARGET;
        return;
    }

    // Check for bags before continuing pathing
    if (checkBagsAndTransition()) {
        return; // Switched to vendor state
    }

    // Initialize or validate path index
    if (m_currentPathIndex < 0 || m_currentPathIndex >= static_cast<int>(path.size())) {
        m_currentPathIndex = 0; // Start from the beginning
        LogMessage("GrindingEngine MOVING_TO_TARGET: Starting path navigation at index 0.");
    }

    // Get player and target point positions
    std::shared_ptr<WowPlayer> player = m_objectManager->GetLocalPlayer();
    if (!player) {
        LogMessage("GrindingEngine MOVING_TO_TARGET: Cannot get player object. Skipping path step.");
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Wait longer if player invalid
        return;
    }
    // Read player position DIRECTLY, bypassing cache issues
    void* playerPtr = player->GetPointer();
    Vector3 playerPos = {0,0,0};
    if (!playerPtr) {
        LogMessage("GrindingEngine MOVING_TO_TARGET: Player object pointer is null. Skipping path step.");
         std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
         return;
    } else {
         try {
             uintptr_t baseAddr = reinterpret_cast<uintptr_t>(playerPtr);
             constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C;
             constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798;
             constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0;
             playerPos.x = MemoryReader::Read<float>(baseAddr + OBJECT_POS_X_OFFSET);
             playerPos.y = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Y_OFFSET);
             playerPos.z = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Z_OFFSET);
         } catch (...) {
              LogMessage("GrindingEngine MOVING_TO_TARGET: EXCEPTION reading player position directly. Skipping path step.");
              std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
              return;
         }
    }
    const Vector3& targetPoint = path[m_currentPathIndex];

    // Calculate distance (squared for efficiency)
    float dx = playerPos.x - targetPoint.x;
    float dy = playerPos.y - targetPoint.y;
    float dz = playerPos.z - targetPoint.z;
    float distanceSq = dx*dx + dy*dy + dz*dz;

    const float DISTANCE_THRESHOLD_SQ = 2.0f * 2.0f; // e.g., 2 yards squared

    if (distanceSq < DISTANCE_THRESHOLD_SQ) {
        // Reached the current point, move to the next
        m_currentPathIndex++;
        LogStream ss; ss << "GrindingEngine MOVING_TO_TARGET: Reached point, advancing to index " << m_currentPathIndex;
        LogMessage(ss.str());

        if (m_currentPathIndex >= static_cast<int>(path.size())) {
            // Reached the end of the path
            LogMessage("GrindingEngine MOVING_TO_TARGET: Path finished. Looping or stopping logic here? Back to FINDING_TARGET for now.");
            m_currentPathIndex = 0; // Loop path for now
            // m_currentState = EngineState::FINDING_TARGET; // Or switch state if path is done
            return;
        }
        // If not finished, loop will handle moving to the new targetPoint in the next iteration

    } else {
        // Not close enough, need to move towards targetPoint
        MovementController::GetInstance().ClickToMove(targetPoint, playerPos);
    }
    
    // --- Check for targets while moving --- 
    if (selectBestTarget()) { 
        // selectBestTarget now just requests targeting via BotController.
        // It returns true IF IT FOUND A TARGET, but the actual targeting happens async.
        // We need to wait until m_targetUnitPtr becomes valid in the FINDING_TARGET state.
        LogMessage("GrindingEngine MOVING_TO_TARGET: Target requested while pathing. Switching to FINDING_TARGET to wait/confirm.");
        MovementController::GetInstance().Stop(); // Stop pathing movement
        m_currentState = EngineState::FINDING_TARGET;
        return;
    }
    // -------------------------------------
}

// Renamed from handleFindingTarget & part of CHECK_STATE
void GrindingEngine::processStateFindingTarget() {
    // --- Bag Check --- 
    // Only check bags if we don't currently have a target GUID or a target pointer
    // This prevents immediate re-vendoring after returning to grind spot
    if (!m_targetUnitPtr && m_currentTargetGuid == 0) {
        if (checkBagsAndTransition()) {
            return; // Switched to vendor state
        }
    }
    // -----------------

    if (m_targetUnitPtr) { // Check if we have a valid target pointer (set by BotController's processRequests)
        LogMessage("GrindingEngine FINDING_TARGET: Valid target pointer found. Entering COMBAT.");
        
        // --- Calculate Effective Combat Range --- 
        const auto& rotation = m_botController->getCurrentRotation();
        float maxRange = 0.0f;
        if (!rotation.empty()) {
            for(const auto& step : rotation) {
                if (step.castRange > maxRange) {
                    maxRange = step.castRange;
                }
            }
            m_effectiveCombatRange = (std::max)(5.0f, maxRange - 4.0f); 
        } else {
            m_effectiveCombatRange = 5.0f; 
        }
        LogStream ssRange; ssRange << "GrindingEngine: Setting Effective Combat Range to " << m_effectiveCombatRange << " (Max Spell Range: " << maxRange << ")";
        LogMessage(ssRange.str());
        // --- End Calculate Range ---

        MovementController::GetInstance().FaceTarget(GuidToUint64(m_targetUnitPtr->GetGUID()));
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Small delay after facing

        m_currentState = EngineState::COMBAT;
        m_combatStartTime = GetTickCount();
        m_currentRotationIndex = 0; 
        m_lastFailedTargetGuid = 0; // Reset failed target since we have a new one

    } else if (m_currentTargetGuid != 0) {
         // We have a GUID targeted in game (maybe manually by player, or requested but pointer not yet valid),
         // but the object ptr isn't valid/cached yet. Wait here.
         LogMessage("GrindingEngine FINDING_TARGET: Have target GUID in game, but object ptr invalid. Waiting for cache/update...");
         // Stay in FINDING_TARGET state
         std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Short wait
    } else {
         // No target GUID exists in the game.
         LogMessage("GrindingEngine FINDING_TARGET: No target. Requesting new one via selectBestTarget()...");
         if (!selectBestTarget()) { // Returns true if it *requested* a target
            // No suitable target found nearby, continue pathing
            LogMessage("GrindingEngine FINDING_TARGET: No target found nearby. Entering MOVING_TO_TARGET.");
            m_currentState = EngineState::MOVING_TO_TARGET; 
            m_lastFailedTargetGuid = 0; // Reset failed target
         } else {
             // Target was requested, stay in FINDING_TARGET to wait for confirmation
             LogMessage("GrindingEngine FINDING_TARGET: Target requested. Waiting for pointer update...");
         }
    }
}

// Renamed from handleCombat
void GrindingEngine::processStateCombat() {
    if (!m_targetUnitPtr || !m_objectManager) {
        LogMessage("GrindingEngine COMBAT: Target pointer or ObjectManager is null. Exiting combat state to FINDING_TARGET.");
        m_targetUnitPtr = nullptr;
        m_currentState = EngineState::FINDING_TARGET;
        return;
    }

    // Check if target is still valid and in the object manager cache
    uint64_t currentTargetGuidInGame = m_objectManager->GetCurrentTargetGUID();
    uint64_t myTargetGuid = GuidToUint64(m_targetUnitPtr->GetGUID());

    // 1. Check if target is dead
    if (m_targetUnitPtr->IsDead()) {
        LogStream ss; ss << "GrindingEngine COMBAT: Target " << std::hex << myTargetGuid << " is dead.";
        LogMessage(ss.str());

        bool shouldLoot = m_botController->isLootingEnabled(); 
        if (shouldLoot && m_targetUnitPtr->IsLootable()) { 
            LogMessage("GrindingEngine COMBAT: Target is lootable and looting enabled. Entering LOOTING state.");
            m_currentState = EngineState::LOOTING;
            // Keep m_targetUnitPtr pointing to the corpse for looting
            return; 
        } else {
             if (!shouldLoot) {
                  LogMessage("GrindingEngine COMBAT: Target is dead, but looting is disabled. Skipping loot.");
             } else { 
                  LogMessage("GrindingEngine COMBAT: Target is dead but not lootable. Skipping loot.");
             }
            // Target is dead, clear target in game IF it's our target
            if (currentTargetGuidInGame == myTargetGuid) {
                 LogMessage("GrindingEngine COMBAT: Clearing dead target in game.");
                 // Request clear via BotController
                 m_botController->requestTarget(0); 
            }
            m_targetUnitPtr = nullptr;
            m_currentState = EngineState::FINDING_TARGET; // Go look for a new target
            return;
        }
    }

    // 2. Check if target GUID changed in game (e.g., player tabbed)
    // Allow target to be 0 (target died/despawned) - handled by IsDead check above
    if (currentTargetGuidInGame != 0 && currentTargetGuidInGame != myTargetGuid) {
        LogStream ss; ss << "GrindingEngine COMBAT: Target changed in game (Now: " << std::hex << currentTargetGuidInGame << "). Resetting to FINDING_TARGET.";
        LogMessage(ss.str());
        // We lost our target, clear pointer and go check state
        m_targetUnitPtr = nullptr; 
        m_currentState = EngineState::FINDING_TARGET;
        return;
    }

    // 3. Check if the target pointer itself became invalid somehow (e.g., object removed from manager)
    // This check is important as the object might despawn even if targeted
    if (!m_objectManager->GetObjectByGUID(myTargetGuid)) {
        LogStream ss; ss << "GrindingEngine COMBAT: Target " << std::hex << myTargetGuid << " no longer found in ObjectManager. Resetting to FINDING_TARGET.";
        LogMessage(ss.str());
        // Clear target in game if it was still set to the despawned GUID
        if (currentTargetGuidInGame == myTargetGuid) {
            m_botController->requestTarget(0); 
        }
        m_targetUnitPtr = nullptr;
        m_currentState = EngineState::FINDING_TARGET;
        return;
    }

    // --- Target is alive and still targeted, proceed with combat --- 

    // Get player and target positions (Direct reads for robustness)
    std::shared_ptr<WowPlayer> player_combat = m_objectManager->GetLocalPlayer();
    Vector3 playerPos = {0,0,0};
    if (!player_combat) {
        LogMessage("GrindingEngine COMBAT: Failed to get player object. Skipping combat step.");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }
    void* playerPtr_combat = player_combat->GetPointer();
    if (!playerPtr_combat) {
        LogMessage("GrindingEngine COMBAT: Failed to get player pointer. Skipping combat step.");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }
    try {
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(playerPtr_combat);
        playerPos.x = MemoryReader::Read<float>(baseAddr + 0x79C); 
        playerPos.y = MemoryReader::Read<float>(baseAddr + 0x798); 
        playerPos.z = MemoryReader::Read<float>(baseAddr + 0x7A0); 
    } catch (...) {
        LogMessage("GrindingEngine COMBAT: Failed to read player position. Skipping combat step.");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }

    Vector3 targetPos = {0,0,0};
    void* targetPtr_combat = m_targetUnitPtr->GetPointer();
    if (!targetPtr_combat) {
        LogMessage("GrindingEngine COMBAT: Failed to get target pointer. Resetting to FINDING_TARGET.");
        m_targetUnitPtr = nullptr;
        m_currentState = EngineState::FINDING_TARGET;
        return;
    }
    try {
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(targetPtr_combat);
        targetPos.x = MemoryReader::Read<float>(baseAddr + 0x79C); 
        targetPos.y = MemoryReader::Read<float>(baseAddr + 0x798); 
        targetPos.z = MemoryReader::Read<float>(baseAddr + 0x7A0); 
    } catch (...) {
        LogMessage("GrindingEngine COMBAT: Failed to read target position. Resetting to FINDING_TARGET.");
        m_targetUnitPtr = nullptr;
        m_currentState = EngineState::FINDING_TARGET;
        return;
    }

    // Calculate 3D distance
    float dx = playerPos.x - targetPos.x;
    float dy = playerPos.y - targetPos.y;
    float dz = playerPos.z - targetPos.z;
    float distanceSq = dx*dx + dy*dy + dz*dz;
    float distance = std::sqrt(distanceSq); 

    // 1. Check if target is outside effective combat range
    if (distance > m_effectiveCombatRange) { 
        LogStream ss; ss << "GrindingEngine COMBAT: Target distance (" << distance << ") > Effective Range (" << m_effectiveCombatRange << "). Moving closer.";
        LogMessage(ss.str());
        MovementController::GetInstance().ClickToMove(targetPos, playerPos); 
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); 
        return; // Stay in COMBAT state, but don't cast yet
    }
    
    // 2. Target is within effective combat range
    else {
        // LogStream ss; ss << "GrindingEngine COMBAT: Target distance (" << distance << ") <= Effective Range (" << m_effectiveCombatRange << "). Stopping and Engaging.";
        // LogMessage(ss.str()); // Can be noisy

        // Stop moving if we were approaching
        MovementController::GetInstance().Stop(); 
        
        // Ensure facing target (might drift slightly)
        MovementController::GetInstance().FaceTarget(myTargetGuid); 

        // Execute combat rotation
        castSpellFromRotation();
    }

    // Timeout check (e.g., 60 seconds)
    if (GetTickCount() - m_combatStartTime > 60000) {
        LogMessage("GrindingEngine COMBAT: Combat timeout reached. Resetting to FINDING_TARGET.");
        if (currentTargetGuidInGame == myTargetGuid) { m_botController->requestTarget(0); } 
        m_targetUnitPtr = nullptr;
        m_currentState = EngineState::FINDING_TARGET;
        return;
    }
}

// Renamed from handleLooting
void GrindingEngine::processStateLooting() {
    const float LOOT_DISTANCE_THRESHOLD = 4.0f; 
    const float LOOT_DISTANCE_THRESHOLD_SQ = LOOT_DISTANCE_THRESHOLD * LOOT_DISTANCE_THRESHOLD;

    if (!m_targetUnitPtr || !m_objectManager) {
        LogMessage("GrindingEngine LOOTING: Corpse pointer or ObjectManager is null. Back to FINDING_TARGET.");
        m_targetUnitPtr = nullptr;
        m_currentState = EngineState::FINDING_TARGET;
        return;
    }

    // Re-check if the corpse is still lootable (maybe someone else looted it, or it despawned)
    if (!m_targetUnitPtr->IsLootable()) {
        LogMessage("GrindingEngine LOOTING: Corpse is no longer lootable. Back to FINDING_TARGET.");
         if (m_objectManager->GetCurrentTargetGUID() == GuidToUint64(m_targetUnitPtr->GetGUID())) {
            m_botController->requestTarget(0); // Clear target if it was the corpse
         }
        m_targetUnitPtr = nullptr;
        m_currentState = EngineState::FINDING_TARGET;
        // Check bags *after* attempting loot (or failing)
        checkBagsAndTransition(); 
        return;
    }

    // Get player position directly
    std::shared_ptr<WowPlayer> player_loot = m_objectManager->GetLocalPlayer();
    Vector3 playerPos = {0,0,0};
     if (!player_loot) {
        LogMessage("GrindingEngine LOOTING: Failed to get player object. Skipping loot step.");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }
    void* playerPtr_loot = player_loot->GetPointer();
    if (!playerPtr_loot) {
        LogMessage("GrindingEngine LOOTING: Failed to get player pointer. Skipping loot step.");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }
    try {
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(playerPtr_loot);
        constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C;
        constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798;
        constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0;
        playerPos.x = MemoryReader::Read<float>(baseAddr + OBJECT_POS_X_OFFSET); 
        playerPos.y = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Y_OFFSET); 
        playerPos.z = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Z_OFFSET); 
    } catch (...) {
        LogMessage("GrindingEngine LOOTING: Failed to read player position. Skipping loot step.");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }

    // Get corpse position DIRECTLY from memory
    Vector3 corpsePos = {0,0,0};
    void* corpsePtr = m_targetUnitPtr->GetPointer();
    if (!corpsePtr) {
        LogMessage("GrindingEngine LOOTING: Failed to get corpse pointer. Aborting loot, back to FINDING_TARGET.");
        m_targetUnitPtr = nullptr;
        m_currentState = EngineState::FINDING_TARGET;
        checkBagsAndTransition(); 
        return;
    }
    try {
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(corpsePtr);
        constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C;
        constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798;
        constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0;
        corpsePos.x = MemoryReader::Read<float>(baseAddr + OBJECT_POS_X_OFFSET); 
        corpsePos.y = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Y_OFFSET); 
        corpsePos.z = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Z_OFFSET); 
        if (corpsePos.x == 0.0f && corpsePos.y == 0.0f && corpsePos.z == 0.0f) {
             LogMessage("GrindingEngine LOOTING: Warning - Read corpse position as (0,0,0). Aborting loot.");
             m_targetUnitPtr = nullptr;
             m_currentState = EngineState::FINDING_TARGET;
             checkBagsAndTransition();
             return;
        }
    } catch (...) {
        LogMessage("GrindingEngine LOOTING: Failed to read corpse position. Aborting loot, back to FINDING_TARGET.");
        m_targetUnitPtr = nullptr;
        m_currentState = EngineState::FINDING_TARGET;
        checkBagsAndTransition();
        return;
    }

    // Calculate distance squared using DIRECTLY READ positions
    float dx = playerPos.x - corpsePos.x;
    float dy = playerPos.y - corpsePos.y;
    float dz = playerPos.z - corpsePos.z;
    float distanceSq = dx*dx + dy*dy + dz*dz;

    if (distanceSq > LOOT_DISTANCE_THRESHOLD_SQ) {
        // Too far, move closer using the DIRECTLY READ corpse position
        LogStream ss; ss << "GrindingEngine LOOTING: Moving to corpse. Distance: " << std::sqrt(distanceSq);
        LogMessage(ss.str());
        MovementController::GetInstance().ClickToMove(corpsePos, playerPos); 
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    } else {
        // Close enough, stop and interact
        LogMessage("GrindingEngine LOOTING: Reached corpse. Stopping and Interacting.");
        MovementController::GetInstance().Stop();
        
        uint64_t corpseGuid = GuidToUint64(m_targetUnitPtr->GetGUID());
        uint64_t currentGuidInGame = m_objectManager->GetCurrentTargetGUID();

        // Target the corpse if not already targeted
        if (currentGuidInGame != corpseGuid) {
             LogStream ssTarget; ssTarget << "GrindingEngine LOOTING: Requesting target corpse GUID 0x" << std::hex << corpseGuid;
             LogMessage(ssTarget.str());
             if (m_botController) {
                 m_botController->requestTarget(corpseGuid);
             }
            // Wait briefly for target to potentially update (Interact might fail otherwise)
            std::this_thread::sleep_for(std::chrono::milliseconds(150)); 
        } 
        
        // Now request interaction
        if (m_botController) {
            LogStream ssI; ssI << "GrindingEngine LOOTING: Requesting interaction with GUID 0x" << std::hex << corpseGuid;
            LogMessage(ssI.str());
            m_botController->requestInteract(corpseGuid);
        } else {
            LogMessage("GrindingEngine LOOTING Error: BotController null, cannot request interaction.");
        }

        // Wait a bit for auto-loot to happen (adjust as needed)
        std::this_thread::sleep_for(std::chrono::milliseconds(750));

        // Loot attempt finished, clear pointer and go back to finding target
        LogMessage("GrindingEngine LOOTING: Loot attempt finished. Back to FINDING_TARGET.");
        m_targetUnitPtr = nullptr;
        m_currentState = EngineState::FINDING_TARGET;
        // Check bags AFTER looting attempt
        checkBagsAndTransition(); 
    }
} 

// Renamed from handleRecovering
void GrindingEngine::processStateMovingToCorpse() { 
    // TODO: Implement corpse run logic
    // - Find corpse location (requires death handling/tracking)
    // - Path back to corpse
    // - Resurrect
    // - Transition back to finding target or maybe recovery state
    LogMessage("GrindingEngine MOVING_TO_CORPSE: (Not Implemented). Transitioning back to finding target.");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    m_currentState = EngineState::FINDING_TARGET;
}


// --- Vendor State Handlers (Placeholders) ---

void GrindingEngine::processStateMovingToVendor() {
    LogMessage("GrindingEngine MOVING_TO_VENDOR: State handler entered.");

    // 1. Load Vendor Path if not already loaded
    if (!m_vendorPathLoaded) {
        LogMessage("GrindingEngine MOVING_TO_VENDOR: Loading vendor path points...");
        if (!m_botController) { 
            LogMessage("GrindingEngine Error: BotController null, cannot load vendor path. Aborting vendor run.");
            m_currentState = m_preVendorState; 
            return;
        }
        m_currentVendorPathPoints = m_botController->getLoadedVendorPathPoints(); // Get copy
        if (m_currentVendorPathPoints.empty()) {
            LogMessage("GrindingEngine Error: Vendor path is empty or not loaded in BotController. Cannot proceed. Returning to previous state.");
            m_currentState = m_preVendorState; // Go back to where we were
            return;
        }
        m_vendorPathLoaded = true;
        m_currentVendorPathIndex = 0; // Start at the beginning of the path
        LogStream ssPath; ssPath << "GrindingEngine MOVING_TO_VENDOR: Loaded " << m_currentVendorPathPoints.size() << " vendor path points. Starting navigation.";
        LogMessage(ssPath.str());
    }

    // 2. Check if path navigation is complete
    if (m_currentVendorPathIndex < 0 || m_currentVendorPathIndex >= static_cast<int>(m_currentVendorPathPoints.size())) {
        LogMessage("GrindingEngine MOVING_TO_VENDOR: Reached end of vendor path. Transitioning to VENDERING.");
        m_currentState = EngineState::VENDERING;
        m_vendorSubState = VendorSubState::FINDING_VENDOR; // <<< Reset sub-state when entering VENDERING
        m_vendorGuid = 0; // Ensure vendor GUID is reset
        MovementController::GetInstance().Stop(); // Stop movement
        return;
    }

    // 3. Navigate along the path
    const Vector3& targetPoint = m_currentVendorPathPoints[m_currentVendorPathIndex];
    
    // Get player position with retries
    Vector3 playerPos = {0,0,0};
    std::shared_ptr<WowPlayer> player = nullptr;
    bool positionReadSuccess = false;
    const int MAX_RETRIES = 3;
    for (int retry = 0; retry < MAX_RETRIES; ++retry) {
        player = m_objectManager->GetLocalPlayer();
        if (player && ReadUnitPosition(player, playerPos)) {
            positionReadSuccess = true;
            break; // Success!
        }
        // Log only on the last attempt if still failing
        if (retry == MAX_RETRIES - 1) {
             if (!player) {
                 LogMessage("GrindingEngine MOVING_TO_VENDOR: Player object null after retries, skipping move.");
             } else {
                 LogMessage("GrindingEngine MOVING_TO_VENDOR: Failed to read player position after retries, skipping move.");
             }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Wait briefly before retrying
    }

    if (!positionReadSuccess) {
        return; // Skip this movement update if position couldn't be read
    }

    // Calculate distance (squared for efficiency)
    float distanceSq = CalculateDistanceSq(playerPos, targetPoint);
    const float DISTANCE_THRESHOLD_SQ = 2.0f * 2.0f; 

    if (distanceSq < DISTANCE_THRESHOLD_SQ) {
        // Reached the current point, move to the next
        m_currentVendorPathIndex++;
        LogStream ss; ss << "GrindingEngine MOVING_TO_VENDOR: Reached point, advancing to index " << m_currentVendorPathIndex;
        LogMessage(ss.str());
        // Loop will handle moving to the new point or transitioning if done
    } else {
        // Not close enough, move towards targetPoint
        MovementController::GetInstance().ClickToMove(targetPoint, playerPos);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Small delay during pathing
}

void GrindingEngine::processStateVendering() {
    // LogMessage("GrindingEngine VENDERING: State handler entered."); // Can be noisy
    if (!m_botController) { 
        LogMessage("GrindingEngine Error: BotController is null in VENDERING state. Aborting.");
        m_currentState = m_preVendorState; // Go back to previous state
        m_vendorSubState = VendorSubState::FINDING_VENDOR; // Reset sub-state
        return; 
    }

    auto now = std::chrono::steady_clock::now();
    bool isVendorWindowVisible = m_botController->getIsVendorWindowVisible();

    // --- NEW VENDOR SUB-STATE MACHINE --- 

    switch (m_vendorSubState) {
        case VendorSubState::FINDING_VENDOR: {
            LogMessage("GrindingEngine VENDERING: SubState FINDING_VENDOR");
            m_vendorGuid = 0; // Reset vendor GUID

            std::string targetVendorName = m_targetVendorName;
            if (targetVendorName.empty()) {
                LogMessage("GrindingEngine Error: Target vendor name is empty. Aborting vendor run.");
                m_vendorSubState = VendorSubState::FINISHED; // Set to finished to exit vendering
                break;
            }

            LogStream ssFind; ssFind << "GrindingEngine VENDERING: Searching for vendor NPC named '" << targetVendorName << "'.";
            LogMessage(ssFind.str());

            std::shared_ptr<WowUnit> foundVendor = findVendorUnitByName(targetVendorName);

            if (foundVendor) {
                m_vendorGuid = GuidToUint64(foundVendor->GetGUID());
                LogStream ssFound; ssFound << "GrindingEngine VENDERING: Found vendor '" << targetVendorName << "' with GUID 0x" << std::hex << m_vendorGuid << ". Moving to APPROACHING_VENDOR.";
                LogMessage(ssFound.str());
                m_vendorSubState = VendorSubState::APPROACHING_VENDOR;
            } else {
                LogStream ssNotFound; ssNotFound << "GrindingEngine VENDERING: Vendor '" << targetVendorName << "' not found nearby. Retrying search...";
                LogMessage(ssNotFound.str());
                std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Wait before retrying search
            }
            break;
        }

        case VendorSubState::APPROACHING_VENDOR: {
            // LogMessage("GrindingEngine VENDERING: SubState APPROACHING_VENDOR"); // Noisy
            if (m_vendorGuid == 0) { 
                LogMessage("GrindingEngine Error: Vendor GUID is 0 in APPROACHING state. Resetting to FINDING.");
                m_vendorSubState = VendorSubState::FINDING_VENDOR;
                break;
            }

            // Use GetObjectByGUID and cast to WowUnit
            std::shared_ptr<WowObject> vendorObject = m_objectManager->GetObjectByGUID(m_vendorGuid);
            std::shared_ptr<WowUnit> vendorUnit = std::dynamic_pointer_cast<WowUnit>(vendorObject);

            if (!vendorUnit || vendorUnit->IsDead()) { // Check if null after cast or dead
                LogMessage("GrindingEngine Error: Vendor unit not found (or not a unit) or is dead. Resetting to FINDING.");
                m_vendorGuid = 0;
                m_vendorSubState = VendorSubState::FINDING_VENDOR;
                break;
            }

            Vector3 vendorPos = {0,0,0};
            if (!ReadUnitPosition(vendorUnit, vendorPos)) {
                 LogMessage("GrindingEngine Warning: Could not read vendor position. Resetting to FINDING.");
                 m_vendorGuid = 0;
                 m_vendorSubState = VendorSubState::FINDING_VENDOR;
                 break;
            }

            Vector3 playerPos = {0,0,0};
            std::shared_ptr<WowPlayer> player = m_objectManager->GetLocalPlayer();
            if (!player || !ReadUnitPosition(player, playerPos)) {
                 LogMessage("GrindingEngine Warning: Could not read player position for approach.");
                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
                 break;
            }
            
            float distanceSq = CalculateDistanceSq(playerPos, vendorPos);
            const float VENDOR_INTERACT_DISTANCE_SQ = VENDOR_INTERACT_DISTANCE * VENDOR_INTERACT_DISTANCE;

            if (distanceSq <= VENDOR_INTERACT_DISTANCE_SQ) {
                LogMessage("GrindingEngine VENDERING: Reached vendor range. Stopping movement. Moving to REQUESTING_INTERACT.");
                MovementController::GetInstance().Stop();
                m_vendorSubState = VendorSubState::REQUESTING_INTERACT;
            } else {
                // LogMessage("GrindingEngine VENDERING: Moving towards vendor."); // Noisy
                MovementController::GetInstance().ClickToMove(vendorPos, playerPos);
                std::this_thread::sleep_for(std::chrono::milliseconds(50)); 
            }
            break;
        }

        case VendorSubState::REQUESTING_INTERACT: {
            LogMessage("GrindingEngine VENDERING: SubState REQUESTING_INTERACT");
            if (m_vendorGuid != 0) { 
                LogStream ssLog; ssLog << "GrindingEngine VENDERING: Requesting interaction with vendor GUID 0x" << std::hex << m_vendorGuid;
                LogMessage(ssLog.str()); 
                m_botController->requestInteract(m_vendorGuid);
                m_vendorSubState = VendorSubState::WAITING_FOR_WINDOW_OPEN;
                LogMessage("GrindingEngine VENDERING: Interaction requested. Moving to WAITING_FOR_WINDOW_OPEN.");
            } else {
                LogMessage("GrindingEngine VENDERING Error: Vendor GUID is 0 in REQUESTING_INTERACT. Resetting.");
                m_vendorSubState = VendorSubState::FINDING_VENDOR; // Go back to finding if GUID is somehow 0
            }
            break;
        }

        case VendorSubState::WAITING_FOR_WINDOW_OPEN: {
            // LogMessage("GrindingEngine VENDERING: SubState WAITING_FOR_WINDOW_OPEN"); // Noisy
            try {
                const char* luaScript = R"lua(
                    local frame = MerchantFrame;
                    if frame and frame:IsVisible() then
                        return 1; -- Return 1 for true
                    else
                        return 0; -- Return 0 for false
                    end
                )lua";
                bool isVisible = LuaExecutor::ExecuteString<bool>(luaScript);
                
                if (isVisible) {
                    LogMessage("GrindingEngine VENDERING: Vendor window detected. Adding short delay before selling...");
                    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // <-- ADD DELAY HERE (e.g., 500ms)
                    LogMessage("GrindingEngine VENDERING: Delay finished. Moving to SELLING_ITEMS.");
                    m_vendorSubState = VendorSubState::SELLING_ITEMS;
                    // Reset sell timer/state if needed
                } else {
                    // Still waiting for window, do nothing this tick
                }
            } catch (const LuaExecutor::LuaException& e) {
                LogStream ssErr; ssErr << "[GrindingEngine] Lua Error checking vendor visibility: " << e.what(); LogMessage(ssErr.str());
                // Stay in this state, maybe add a timeout later?
            } catch (...) {
                LogMessage("[GrindingEngine] Unknown error checking vendor visibility.");
                 // Stay in this state, maybe add a timeout later?
            }
            break;
        }

        case VendorSubState::SELLING_ITEMS: {
            LogMessage("GrindingEngine VENDERING: SubState SELLING_ITEMS - Selling grey items via Lua UseContainerItem."); // Updated Log
            
            if (!m_botController || !m_objectManager) {
                LogMessage("GrindingEngine Error: BotController or ObjectManager null in SELLING_ITEMS. Aborting sell.");
                m_vendorSubState = VendorSubState::FINISHED;
                break;
            }

            /* // Vendor GUID check not needed for Lua UseContainerItem
            if (m_vendorGuid == 0) {
                LogMessage("GrindingEngine Error: Vendor GUID is 0 in SELLING_ITEMS. Aborting sell.");
                m_vendorSubState = VendorSubState::FINISHED;
                break;
            }
            */

            LogMessage("GrindingEngine: Scanning bags for grey items to sell...");
            int itemsRequestedToSell = 0;

            // Iterate through bags (0 = backpack, 1-4 = equipped)
            for (int bagIndex = 0; bagIndex <= 4; ++bagIndex) {
                int numSlots = GetContainerNumSlots(bagIndex);
                if (numSlots <= 0) continue; // Skip if bag doesn't exist or has no slots

                for (int slotIndex = 0; slotIndex < numSlots; ++slotIndex) {
                    // Check if item is grey (quality == 0)
                    if (GetItemQuality(bagIndex, slotIndex) == ItemQuality::POOR) {
                        // uint64_t itemGuid = GetItemGuidInSlot(bagIndex, slotIndex); // Don't need GUID anymore
                        // if (itemGuid != 0) { // Check removed, Lua handles empty slots fine
                            LogStream ssReq;
                            ssReq << "GrindingEngine: Found grey item in Bag " 
                                  << std::dec << bagIndex << ", Slot " << slotIndex << ". Requesting sell via UseContainerItem.";
                            LogMessage(ssReq.str());
                            
                            m_botController->requestSellItem(bagIndex, slotIndex); // Pass bag/slot index
                            itemsRequestedToSell++;

                            // Add a delay between requesting sells to avoid flooding the server/controller
                            std::this_thread::sleep_for(SELL_DELAY); 
                        // }
                    }
                }
            }

            LogStream ssDone; ssDone << "GrindingEngine: Finished scanning bags. Requested to sell " << itemsRequestedToSell << " grey items.";
            LogMessage(ssDone.str());
            
            // Add a small delay after requesting all sells, before closing the window
            LogMessage("GrindingEngine VENDERING: Finished requesting sells. Adding delay...");
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Slightly longer delay after all requests
            LogMessage("GrindingEngine VENDERING: Delay finished. Transitioning to FINISHED.");
            m_vendorSubState = VendorSubState::FINISHED;
            break;
        }

        case VendorSubState::SELLING_ITEM_LOOP: { 
            // THIS ENTIRE CASE REMAINS DEPRECATED
            LogMessage("GrindingEngine VENDERING Error: Entered deprecated SELLING_ITEM_LOOP state. Resetting to FINISHED.");
            m_vendorSubState = VendorSubState::FINISHED;
            break;
        }

        case VendorSubState::FINISHED: { // Changed from DONE_VENDERING to match existing enum value
            LogMessage("GrindingEngine VENDERING: SubState FINISHED. Vendor run complete."); // Changed log message
            
            // --- Request window close --- 
            if (m_botController) {
                LogMessage("GrindingEngine VENDERING: Requesting vendor window close.");
                m_botController->requestCloseVendorWindow();
            } else {
                LogMessage("GrindingEngine Error: BotController null, cannot request vendor close.");
            }
            // Add a small delay to allow the request to be processed potentially
            std::this_thread::sleep_for(std::chrono::milliseconds(150)); 
            // --------------------------- 

            LogMessage("GrindingEngine VENDERING: Transitioning back to previous state or MOVING_TO_GRIND_SPOT.");

            // Reset vendor state for next run
            m_vendorGuid = 0;
            m_vendorSubState = VendorSubState::FINDING_VENDOR; // Reset sub-state
            // No item list to clear anymore

            // Decide where to go next
            if (m_preVendorState == EngineState::MOVING_TO_TARGET || m_preVendorState == EngineState::FINDING_TARGET || m_preVendorState == EngineState::COMBAT || m_preVendorState == EngineState::LOOTING) {
                LogMessage("GrindingEngine VENDERING: Initiating move back to grind spot.");
                m_currentState = EngineState::MOVING_TO_GRIND_SPOT;
                // m_currentReturnPathIndex = -1; // Reset return path index (If using pathing)
            } else {
                // If we weren't grinding/pathing (e.g., IDLE), just return to the state we were in
                LogMessage("GrindingEngine VENDERING: Returning to previous non-grinding state.");
                m_currentState = m_preVendorState;
            }
            break;
        }

        default: {
            LogStream ssErr; ssErr << "GrindingEngine VENDERING: Encountered unknown VendorSubState: " << static_cast<int>(m_vendorSubState) << ". Resetting to FINDING_VENDOR.";
            LogMessage(ssErr.str());
            m_vendorSubState = VendorSubState::FINDING_VENDOR;
            break;
        }
    }
    // --- END NEW VENDOR SUB-STATE MACHINE ---
}

// --- ADDED: Helper Functions --- 

std::shared_ptr<WowUnit> GrindingEngine::findVendorUnitByName(const std::string& targetName) {
     if (!m_objectManager) return nullptr;

     // Define search distance within the function or as a class constant
     constexpr float VENDOR_SEARCH_DISTANCE = 20.0f; // e.g., 20 yards
     constexpr float VENDOR_SEARCH_DISTANCE_SQ = VENDOR_SEARCH_DISTANCE * VENDOR_SEARCH_DISTANCE;

     std::shared_ptr<WowUnit> foundVendor = nullptr;
     float minDistanceSq = VENDOR_SEARCH_DISTANCE_SQ; // Use a reasonable search distance squared
     Vector3 playerPos = {0,0,0};
     std::shared_ptr<WowPlayer> player = m_objectManager->GetLocalPlayer();
     // Use helper for position read
     if (player) ReadUnitPosition(player, playerPos);

     // Prepare the target name for comparison (lowercase, trimmed)
     std::string lowerTargetName = targetName;
     std::transform(lowerTargetName.begin(), lowerTargetName.end(), lowerTargetName.begin(), ::tolower);
     lowerTargetName.erase(0, lowerTargetName.find_first_not_of(" \t\n\r"));
     lowerTargetName.erase(lowerTargetName.find_last_not_of(" \t\n\r") + 1);
     if (lowerTargetName.empty()) return nullptr; // Don't search for empty names

     auto objectMap = m_objectManager->GetObjects();
     for (const auto& pair : objectMap) {
         std::shared_ptr<WowUnit> unit = std::dynamic_pointer_cast<WowUnit>(pair.second);
         if (!unit || unit->IsDead()) continue; // Skip dead units

         std::string unitName = unit->GetName(); 
         std::string lowerUnitName = unitName;
         std::transform(lowerUnitName.begin(), lowerUnitName.end(), lowerUnitName.begin(), ::tolower);
         lowerUnitName.erase(0, lowerUnitName.find_first_not_of(" \t\n\r"));
         lowerUnitName.erase(lowerUnitName.find_last_not_of(" \t\n\r") + 1);

         if (lowerUnitName == lowerTargetName) {
             Vector3 vendorPos = {0,0,0};
             // Use helper for position read
             if (ReadUnitPosition(unit, vendorPos)) {
                 // Use helper for distance calculation
                 float distSq = CalculateDistanceSq(playerPos, vendorPos);
                 if (distSq < minDistanceSq) {
                     minDistanceSq = distSq;
                     foundVendor = unit;
                 }
             }
         }
     }
     return foundVendor;
}

bool GrindingEngine::ReadUnitPosition(std::shared_ptr<WowObject> unit, Vector3& outPosition) {
     if (!unit) return false;
     void* ptr = unit->GetPointer();
     if (!ptr) return false;
     try {
         uintptr_t baseAddr = reinterpret_cast<uintptr_t>(ptr);
         // Make sure these offsets are correct for your version/client
         outPosition.x = MemoryReader::Read<float>(baseAddr + 0x79C); 
         outPosition.y = MemoryReader::Read<float>(baseAddr + 0x798); 
         outPosition.z = MemoryReader::Read<float>(baseAddr + 0x7A0); 
         return true;
     } catch (const std::exception& e) { // Catch standard exception
         LogStream ssErr; ssErr << "GrindingEngine: std::exception reading position for GUID 0x" << std::hex << GuidToUint64(unit->GetGUID()) << ": " << e.what(); LogMessage(ssErr.str());
         return false;
     } catch (...) {
         LogStream ssErr; ssErr << "GrindingEngine: UNKNOWN EXCEPTION reading position for GUID 0x" << std::hex << GuidToUint64(unit->GetGUID()); LogMessage(ssErr.str());
         return false;
     }
}

float GrindingEngine::CalculateDistanceSq(const Vector3& p1, const Vector3& p2) {
    float dx = p1.x - p2.x;
    float dy = p1.y - p2.y;
    float dz = p1.z - p2.z;
    // Avoid NaN issues if positions are identical or extremely close in float precision
    if (std::isnan(dx) || std::isnan(dy) || std::isnan(dz)) return 0.0f;
    return dx*dx + dy*dy + dz*dz;
}

// --- ADDED: New State Handler --- 
void GrindingEngine::processStateMovingToGrindSpot() {
    LogMessage("GrindingEngine MOVING_TO_GRIND_SPOT: State handler entered.");

    // 1. Ensure we have a stored grind spot location
    if (m_grindSpotLocation.x == 0 && m_grindSpotLocation.y == 0 && m_grindSpotLocation.z == 0) {
        LogMessage("GrindingEngine Error: No grind spot location stored. Cannot return. Switching to FINDING_TARGET.");
        m_currentState = EngineState::FINDING_TARGET;
        return;
    }

    // 2. (Optional but Recommended) Use the Vendor Path in Reverse or a Dedicated Return Path?
    // For simplicity now, we'll just ClickToMove directly to the stored grind spot.
    // A better approach would involve pathfinding back along the vendor path or a separate return path.
    // TODO: Implement path following for return journey?

    // 3. Get player position using helper
    Vector3 playerPos = {0,0,0};
    std::shared_ptr<WowPlayer> player = m_objectManager->GetLocalPlayer();
    if (!player || !ReadUnitPosition(player, playerPos)) {
         LogMessage("GrindingEngine Warning: Could not read player position for return move.");
         std::this_thread::sleep_for(std::chrono::milliseconds(100));
         return;
    }

    // 4. Calculate distance to grind spot using helper
    float distanceSq = CalculateDistanceSq(playerPos, m_grindSpotLocation);
    const float GRIND_SPOT_ARRIVAL_THRESHOLD_SQ = 5.0f * 5.0f; // e.g., 5 yards squared

    if (distanceSq <= GRIND_SPOT_ARRIVAL_THRESHOLD_SQ) {
        // Reached the grind spot (or close enough)
        LogMessage("GrindingEngine MOVING_TO_GRIND_SPOT: Reached grind spot area. Stopping movement.");
        MovementController::GetInstance().Stop();
        LogMessage("GrindingEngine MOVING_TO_GRIND_SPOT: Transitioning back to FINDING_TARGET.");
        m_currentState = EngineState::FINDING_TARGET; // Or maybe m_preVendorState if it wasn't FINDING/MOVING?
    } else {
        // Need to move closer
        // LogMessage("GrindingEngine MOVING_TO_GRIND_SPOT: Moving towards stored grind spot location."); // Noisy
        MovementController::GetInstance().ClickToMove(m_grindSpotLocation, playerPos);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void GrindingEngine::processStateResting() {
    // ... existing code ...
}

bool GrindingEngine::selectBestTarget() {
    std::shared_ptr<WowPlayer> player = m_objectManager->GetLocalPlayer();
    if (!player) {
        LogMessage("GrindingEngine selectBestTarget: Cannot get player object.");
        return false;
    }

    Vector3 playerPos = {0,0,0};
    // Use helper function
    if (!ReadUnitPosition(player, playerPos)) {
         LogMessage("GrindingEngine selectBestTarget: Failed to read player position.");
         return false;
    }

    uint64_t playerGuid = GuidToUint64(player->GetGUID());
    std::shared_ptr<WowUnit> closestUnit = nullptr;
    // Use the PULL_DISTANCE constant defined in the header
    float minDistanceSq = PULL_DISTANCE * PULL_DISTANCE; 

    // Define common unit flags relevant for attackability (adjust values if needed for 3.3.5)
    const uint32_t UNIT_FLAG_NON_ATTACKABLE = 0x00000002;
    const uint32_t UNIT_FLAG_NOT_ATTACKABLE_1 = 0x00000080; // Often used for friendly/non-attackable NPCs
    const uint32_t UNIT_FLAG_IMMUNE_PC = 0x00000100;      // Immune to player attacks

    auto objectMap = m_objectManager->GetObjects(); 
    for (const auto& pair : objectMap) {
        const auto& obj = pair.second;
        uint64_t currentObjGuid = GuidToUint64(obj->GetGUID());
        // Skip self, last failed target
        if (!obj || currentObjGuid == playerGuid || currentObjGuid == m_lastFailedTargetGuid) continue; 

        std::shared_ptr<WowUnit> unit = std::dynamic_pointer_cast<WowUnit>(obj);
        if (!unit || unit->IsDead()) continue; // Skip non-units and dead units

        // Read flags dynamically if possible, otherwise rely on cached flags?
        // unit->UpdateDynamicData(); // Potentially expensive, consider if needed
        uint32_t unitFlags = unit->GetUnitFlags();

        bool isFriendly = (unitFlags & UNIT_FLAG_NOT_ATTACKABLE_1) != 0;
        bool isGenerallyNonAttackable = (unitFlags & UNIT_FLAG_NON_ATTACKABLE) != 0;
        bool isImmuneToPC = (unitFlags & UNIT_FLAG_IMMUNE_PC) != 0;
        // TODO: Add check for Tapped status if needed (unit->IsTapped())

        if (isFriendly || isGenerallyNonAttackable || isImmuneToPC) {
            continue; 
        }

        // Check distance
        Vector3 targetPos = {0,0,0};
        if (ReadUnitPosition(unit, targetPos)) {
            float distanceSq = CalculateDistanceSq(playerPos, targetPos);

            if (distanceSq < minDistanceSq) {
                minDistanceSq = distanceSq;
                closestUnit = unit;
            }
        }
    }

    if (closestUnit) {
        uint64_t targetGuid = GuidToUint64(closestUnit->GetGUID());
        LogStream ssTarget; ssTarget << "GrindingEngine selectBestTarget: Found closest valid target GUID 0x" << std::hex << targetGuid << " at distance sqrt(" << minDistanceSq << "). Requesting target.";
        LogMessage(ssTarget.str());
        
        if (m_botController) {
            m_botController->requestTarget(targetGuid); // Signal main thread
            return true; // Indicate that a target request was made
        }
        return false; // Bot controller missing
    }

    return false; // No suitable target found
}

void GrindingEngine::castSpellFromRotation() {
    if (!m_botController || !m_spellManager) {
        LogMessage("GrindingEngine Error: BotController or SpellManager is null in castSpellFromRotation.");
        return;
    }
    const auto& rotation = m_botController->getCurrentRotation();
    if (rotation.empty()) {
        // LogMessage("GrindingEngine Warning: Rotation is empty, cannot cast."); // Can be noisy
        return; 
    }

    int checkedSpells = 0;
    int maxChecks = rotation.size(); 
    bool spellRequested = false;

    // Use a separate index to avoid modifying m_currentRotationIndex directly within the loop
    int startIndex = m_currentRotationIndex;
    int currentIndex = startIndex;

    while(checkedSpells < maxChecks && !spellRequested) {
        const RotationStep& step = rotation[currentIndex];

        if (checkRotationCondition(step)) { // Check conditions first
             int cooldownMs = m_spellManager->GetSpellCooldownMs(step.spellId); 
             if (cooldownMs == 0) { // Ready to cast
                 uint64_t targetGuid = (step.requiresTarget) ? m_currentTargetGuid : 0;
                 // Re-check if target is required but current target GUID is 0
                 if (step.requiresTarget && targetGuid == 0) {
                     LogMessage("GrindingEngine Warning: Rotation step requires target, but m_currentTargetGuid is 0. Skipping cast.");
                 } else {
                     LogStream ss;
                     ss << "GrindingEngine: Attempting to cast spell ID: " << step.spellId
                        << " (" << step.spellName << ") on Target: 0x" << std::hex << targetGuid;
                     LogMessage(ss.str());
                     m_botController->requestCastSpell(step.spellId, targetGuid); // Request cast
                     spellRequested = true; // Mark as 'requested' to proceed in rotation
                     m_currentRotationIndex = (currentIndex + 1) % rotation.size(); // Update main index only after successful cast request
                 }
             } else if (cooldownMs > 0) {
                  // Optional: Log cooldown active
                  // LogStream ssCD; ssCD << "GrindingEngine: Spell " << step.spellId << " on cooldown (" << cooldownMs << "ms)"; LogMessage(ssCD.str());
             } else { // cooldownMs < 0 indicates an error
                  LogMessage(("GrindingEngine Warning: GetSpellCooldownMs returned error for spell " + std::to_string(step.spellId)).c_str());
             }
        } 

        // Move to next spell in rotation for checking
        currentIndex = (currentIndex + 1) % rotation.size();
        checkedSpells++;
    } 
    // If no spell was requested after checking all, m_currentRotationIndex remains unchanged until next cycle
}

// --- ADD THIS MISSING FUNCTION DEFINITION ---

bool GrindingEngine::checkRotationCondition(const RotationStep& step) {
    std::shared_ptr<WowPlayer> player = m_objectManager->GetLocalPlayer(); 
    std::shared_ptr<WowUnit> target = m_targetUnitPtr; 

    if (!player) {
        // LogMessage("GrindingEngine CondCheck: Failed - Player object is null.");
        return false; // Cannot check conditions without player
    }

    // 1. Target Requirement Check
    if (step.requiresTarget && !target) { 
        // LogMessage("GrindingEngine CondCheck: Failed - Step requires target, but m_targetUnitPtr is NULL");
        return false; 
    }

    // 2. Range Check (only if target is required and exists)
    if (step.requiresTarget && target) {
        Vector3 playerPos = {0,0,0};
        Vector3 targetPos = {0,0,0};
        if (ReadUnitPosition(player, playerPos) && ReadUnitPosition(target, targetPos)) {
            float distanceSq = CalculateDistanceSq(playerPos, targetPos);
            // Use the range specified in the rotation step
            float maxRangeSq = step.castRange * step.castRange; 
            if (distanceSq > maxRangeSq) {
                // LogStream ssRange; ssRange << "GrindingEngine CondCheck: Failed - Out of Range. DistSq=" << distanceSq << ", MaxRangeSq=" << maxRangeSq << " (SpellRange=" << step.castRange << ")"; LogMessage(ssRange.str());
                return false; // Target is out of range for this specific spell
            }
        } else {
             LogMessage("GrindingEngine CondCheck: Failed - Could not read positions for range check.");
             return false; // Cannot check range if positions are unreadable
        }
    }

    // 3. Player Health/Mana Check
    try {
        float playerHealthPct = 0.0f;
        int playerMaxHealth = player->GetMaxHealth();
        if (playerMaxHealth > 0) {
            playerHealthPct = (static_cast<float>(player->GetHealth()) * 100.0f) / playerMaxHealth;
        }
        
        float playerManaPct = 0.0f;
        // Check if the player uses Mana (PowerType 0) before calculating mana percentage
        if (player->GetPowerType() == 0) { 
            int playerMaxMana = player->GetMaxPower();
            if (playerMaxMana > 0) {
                playerManaPct = (static_cast<float>(player->GetPower()) * 100.0f) / playerMaxMana;
            }
        } else {
             playerManaPct = 100.0f; // Assume full "mana" if not a mana user for condition checks
        }

        if (playerHealthPct < step.minPlayerHealthPercent || playerHealthPct > step.maxPlayerHealthPercent) {
             // LogStream ssPH; ssPH << "GrindingEngine CondCheck: Failed - Player Health% " << playerHealthPct << " out of range [" << step.minPlayerHealthPercent << ", " << step.maxPlayerHealthPercent << "]"; LogMessage(ssPH.str());
             return false;
        }
        // Only check mana if the step has mana conditions (avoid checking for non-mana classes if condition is 0-100)
        if (step.minPlayerManaPercent > 0.0f || step.maxPlayerManaPercent < 100.0f) {
             if (playerManaPct < step.minPlayerManaPercent || playerManaPct > step.maxPlayerManaPercent) {
                  // LogStream ssPM; ssPM << "GrindingEngine CondCheck: Failed - Player Mana% " << playerManaPct << " out of range [" << step.minPlayerManaPercent << ", " << step.maxPlayerManaPercent << "]"; LogMessage(ssPM.str());
                  return false;
             }
        }
     } catch (const std::exception& e) { // Catch potential read errors
         LogMessage(("GrindingEngine CondCheck: Exception reading player stats: " + std::string(e.what())).c_str());
         return false; // Fail condition check on error
     }

     // 4. Target Health Check (only if target is required and exists)
     if (step.requiresTarget && target) {
         try {
             float targetHealthPct = 0.0f;
             int targetMaxHealth = target->GetMaxHealth();
             if (targetMaxHealth > 0) {
                  targetHealthPct = (static_cast<float>(target->GetHealth()) * 100.0f) / targetMaxHealth;
             }

             if (targetHealthPct < step.minTargetHealthPercent || targetHealthPct > step.maxTargetHealthPercent) {
                 // LogStream ssTH; ssTH << "GrindingEngine CondCheck: Failed - Target Health% " << targetHealthPct << " out of range [" << step.minTargetHealthPercent << ", " << step.maxTargetHealthPercent << "]"; LogMessage(ssTH.str());
                 return false;
             }
         } catch (const std::exception& e) { // Catch potential read errors
             LogMessage(("GrindingEngine CondCheck: Exception reading target stats: " + std::string(e.what())).c_str());
             return false; // Fail condition check on error
         }
     }

     // 5. Spell Cooldown Check (moved this later, less frequent check)
     if (m_spellManager) { 
         int cooldownMs = m_spellManager->GetSpellCooldownMs(step.spellId);
         if (cooldownMs > 0) { // Spell is on cooldown
             // LogStream ssCD; ssCD << "GrindingEngine CondCheck: Failed - Spell " << step.spellId << " on cooldown (" << cooldownMs << "ms)"; LogMessage(ssCD.str());
             return false; 
         } else if (cooldownMs < 0) { // Error checking cooldown
             LogStream ssErr; ssErr << "GrindingEngine CondCheck: Error getting cooldown for spell " << step.spellId; LogMessage(ssErr.str());
             return false; // Fail condition check on error
         }
         // If cooldownMs == 0, the spell is ready
     } else {
         LogMessage("GrindingEngine CondCheck: Failed - SpellManager instance is null.");
         return false; 
     }

     // If all checks passed
     // LogStream ssPass; ssPass << "GrindingEngine CondCheck: PASSED for Spell " << step.spellId; LogMessage(ssPass.str());
     return true; 
}

// --- END MISSING FUNCTION DEFINITION ---

// ... (rest of the original file content remains unchanged) 