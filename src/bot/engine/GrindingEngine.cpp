#include "GrindingEngine.h"
#include "../../utils/log.h"
#include "../../utils/memory.h" // Include MemoryReader/Writer
#include "../../game/wowobject.h" // Use correct path for WowObject definition
#include "../../game/functions.h"   // For TargetUnitByGuid
#include "../../game/ObjectManager.h" // Need ObjectManager full def
#include "../../game/SpellManager.h" // Need SpellManager full def
#include "../core/BotController.h" // Include BotController
#include "../pathing/PathManager.h" // Need full definition
#include "../core/MovementController.h" // Include for movement
// #include "../core/MovementController.h" // Assuming this is not needed for now

#include <thread> // Include for std::thread
#include <chrono> // For sleep
#include <cmath>  // For std::sqrt

// Constructor - Get necessary pointers
GrindingEngine::GrindingEngine(BotController* controller, ObjectManager* objectManager)
    : m_botController(controller), 
      m_objectManager(objectManager), // Assign ObjectManager passed in
      m_spellManager(&SpellManager::GetInstance()), // Use GetInstance() for SpellManager
      m_grindState(GrindState::IDLE)
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
    m_grindState = GrindState::CHECK_STATE; // Start by checking what to do
    m_engineThread = std::thread(&GrindingEngine::run, this);
    m_engineThread.detach(); // Or join in destructor/stop
}

void GrindingEngine::stop() {
    if (!m_isRunning) return;
    LogMessage("GrindingEngine: Stopping...");
    m_stopRequested = true;
    m_isRunning = false;
    m_grindState = GrindState::IDLE;
    LogMessage("GrindingEngine: Stopped.");
}

bool GrindingEngine::isRunning() const {
    return m_isRunning && !m_stopRequested;
}

// Main loop running in its own thread
void GrindingEngine::run() {
    LogMessage("GrindingEngine: Run loop started.");
    while (!m_stopRequested) {
        try {
             // Update ObjectManager cache once per loop iteration -> REMOVED (MUST BE CALLED FROM MAIN THREAD)
             /*
             if (m_objectManager) { 
                 m_objectManager->Update(); 
             } else {
                 LogMessage("GrindingEngine Error: ObjectManager is null in run() loop. Stopping.");
                 m_grindState = GrindState::ERROR_STATE; 
                 // Maybe directly call stop() or break here?
             }
             */

             updateState(); // Execute state machine logic
        } catch (const std::exception& e) {
             LogStream ssErr;
             ssErr << "GrindingEngine: Exception in run loop: " << e.what();
             LogMessage(ssErr.str());
             m_grindState = GrindState::ERROR_STATE; // Enter error state
        } catch (...) {
             LogMessage("GrindingEngine: Unknown exception in run loop.");
             m_grindState = GrindState::ERROR_STATE;
        }

        // Prevent busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Adjust sleep time
    }
    m_isRunning = false; // Ensure flag is updated on exit
    LogMessage("GrindingEngine: Run loop finished.");
}

// Central state machine logic
void GrindingEngine::updateState() {
    // Ensure ObjectManager's cache is up-to-date before state logic -> MOVED TO run() loop
    /*
    if (m_objectManager) { 
        m_objectManager->Update(); 
    } else {
        LogMessage("GrindingEngine Error: ObjectManager is null in updateState.");
        m_grindState = GrindState::ERROR_STATE;
        return;
    }
    */

    if (!m_objectManager || !m_botController || !m_spellManager ) { // Check SpellManager too
         LogMessage("GrindingEngine Error: Core components missing.");
         m_grindState = GrindState::ERROR_STATE;
         return;
    }

     // --- Global Checks --- 
     // TODO: Add recovery check based on player health/mana

    // Get current target GUID from the game state FIRST
    m_currentTargetGuid = m_objectManager->GetCurrentTargetGUID(); 
    
    // --- Pointer Update Logic --- 
    // Only update the pointer if the state *isn't* COMBAT, 
    // or if the game target changed unexpectedly while in combat.
    // COMBAT state will manage its own pointer check.
    if (m_grindState != GrindState::COMBAT || m_currentTargetGuid != GuidToUint64(m_targetUnitPtr ? m_targetUnitPtr->GetGUID() : WGUID{0,0})) {
        m_targetUnitPtr = nullptr; // Reset pointer if not in combat or target changed
        if (m_currentTargetGuid != 0) {
            std::shared_ptr<WowObject> tempTargetBase = m_objectManager->GetObjectByGUID(m_currentTargetGuid);
            m_targetUnitPtr = std::dynamic_pointer_cast<WowUnit>(tempTargetBase);
        }
    }
    // --- End Pointer Update Logic ---

    switch (m_grindState) {
        case GrindState::IDLE:
            m_grindState = GrindState::CHECK_STATE;
            break;

        case GrindState::CHECK_STATE:
            if (m_targetUnitPtr) { // Check if we have a valid target pointer
                LogMessage("GrindingEngine CHECK_STATE: Valid target found. Entering COMBAT.");
                m_grindState = GrindState::COMBAT;
                m_combatStartTime = GetTickCount();
                m_currentRotationIndex = 0; 
            } else if (m_currentTargetGuid != 0) {
                 // We have a GUID, but the object isn't in cache or isn't a WowUnit yet.
                 // Stay in CHECK_STATE to wait for ObjectManager update.
                 LogMessage("GrindingEngine CHECK_STATE: Have target GUID, but object ptr invalid. Waiting for cache...");
                 // Stay in CHECK_STATE
            } else {
                 // No target GUID exists in the game.
                 LogMessage("GrindingEngine CHECK_STATE: No target. Requesting new one and entering PATHING.");
                 selectBestTarget(); // Request a target (ignore return value)
                 m_grindState = GrindState::PATHING; 
                 m_lastFailedTargetGuid = 0; // Reset failed target
            }
            break;

        case GrindState::PATHING:
            if (m_targetUnitPtr) { // Check if a target has been acquired and is valid
                 LogMessage("GrindingEngine PATHING: Target acquired! Stopping movement and entering COMBAT.");
                 MovementController::GetInstance().Stop(); // Stop CTM
                 m_grindState = GrindState::COMBAT;
                 m_combatStartTime = GetTickCount();
                 m_currentRotationIndex = 0;
            } else {
                // No valid target yet, continue pathing and checking
                handlePathing();    // Move along the path
                selectBestTarget(); // Check for new targets while pathing (request if found)
            }
            break;

        // FINDING_TARGET and MOVING_TO_TARGET are effectively handled by PATHING/COMBAT checks now
        // case GrindState::FINDING_TARGET: 
        //     handleFindingTarget();
        //     break;
        // case GrindState::MOVING_TO_TARGET:
        //      handleMovingToTarget();
        //      break;

        case GrindState::COMBAT:
            handleCombat(); // Will transition back to CHECK_STATE if target lost/dies
            break;

        case GrindState::RECOVERING:
            handleRecovering();
            break;

        case GrindState::ERROR_STATE:
            LogMessage("GrindingEngine: In ERROR_STATE. Stopping.");
            stop(); // Stop the engine on error
            // Consider signaling BotController too if needed
            break;
    }
}

// --- State Handler Implementations ---

void GrindingEngine::handlePathing() {
    const PathManager* pathManager = m_botController->getPathManager();
    if (!pathManager || !pathManager->hasPath()) {
        LogMessage("GrindingEngine handlePathing: No path loaded or PathManager missing. Back to CHECK_STATE.");
        m_currentPathIndex = -1; // Reset path index
        m_grindState = GrindState::CHECK_STATE;
        return;
    }

    const auto& path = pathManager->getPath();
    if (path.empty()) {
        LogMessage("GrindingEngine handlePathing: Path is empty. Back to CHECK_STATE.");
        m_currentPathIndex = -1;
        m_grindState = GrindState::CHECK_STATE;
        return;
    }

    // Initialize or validate path index
    if (m_currentPathIndex < 0 || m_currentPathIndex >= static_cast<int>(path.size())) {
        m_currentPathIndex = 0; // Start from the beginning
        LogMessage("GrindingEngine handlePathing: Starting path navigation at index 0.");
    }

    // Get player and target point positions
    std::shared_ptr<WowPlayer> player = m_objectManager->GetLocalPlayer();
    if (!player) {
        LogMessage("GrindingEngine handlePathing: Cannot get player object. Skipping path step.");
        std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Wait longer if player invalid
        return;
    }
    // Read player position DIRECTLY, bypassing cache issues
    void* playerPtr = player->GetPointer();
    Vector3 playerPos = {0,0,0};
    if (!playerPtr) {
        LogMessage("GrindingEngine handlePathing: Player object pointer is null. Skipping path step.");
         std::this_thread::sleep_for(std::chrono::milliseconds(200)); 
         return;
    } else {
         try {
             uintptr_t baseAddr = reinterpret_cast<uintptr_t>(playerPtr);
             // Define offsets locally or include a common header
             constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C;
             constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798;
             constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0;
             playerPos.x = MemoryReader::Read<float>(baseAddr + OBJECT_POS_X_OFFSET);
             playerPos.y = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Y_OFFSET);
             playerPos.z = MemoryReader::Read<float>(baseAddr + OBJECT_POS_Z_OFFSET);
         } catch (...) {
              LogMessage("GrindingEngine handlePathing: EXCEPTION reading player position directly. Skipping path step.");
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
        LogStream ss; ss << "GrindingEngine handlePathing: Reached point, advancing to index " << m_currentPathIndex;
        LogMessage(ss.str());

        if (m_currentPathIndex >= static_cast<int>(path.size())) {
            // Reached the end of the path
            LogMessage("GrindingEngine handlePathing: Path finished. Back to CHECK_STATE.");
            m_currentPathIndex = -1; // Reset index
            m_grindState = GrindState::CHECK_STATE;
            return;
        }
        // If not finished, loop will handle moving to the new targetPoint in the next iteration

    } else {
        // Not close enough, need to move towards targetPoint
        // LogStream ssMove; ssMove << "GrindingEngine handlePathing: Moving towards point " << m_currentPathIndex 
        //                           << " (" << targetPoint.x << ", " << targetPoint.y << ", " << targetPoint.z << ")"
        //                           << ", DistSq: " << distanceSq;
        // LogMessage(ssMove.str());

        // **** MOVEMENT LOGIC NEEDED HERE ****
        // Use MovementController to move towards the target point
        MovementController::GetInstance().ClickToMove(targetPoint, playerPos);

        // Placeholder sleep until movement implemented -> REMOVED
         // std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    }
}

bool GrindingEngine::selectBestTarget() {
    std::shared_ptr<WowPlayer> player = m_objectManager->GetLocalPlayer();
    if (!player) {
        LogMessage("GrindingEngine selectBestTarget: Cannot get player object.");
        return false;
    }

    Vector3 playerPos = {0,0,0};
    void* playerPtr = player->GetPointer();
    if (!playerPtr) {
        LogMessage("GrindingEngine selectBestTarget: Player pointer is null.");
        return false;
    }
    try {
        uintptr_t pBase = reinterpret_cast<uintptr_t>(playerPtr);
        playerPos.x = MemoryReader::Read<float>(pBase + 0x79C);
        playerPos.y = MemoryReader::Read<float>(pBase + 0x798);
        playerPos.z = MemoryReader::Read<float>(pBase + 0x7A0);
    } catch (...) {
        LogMessage("GrindingEngine selectBestTarget: EXCEPTION reading player position.");
        return false;
    }

    uint64_t playerGuid = GuidToUint64(player->GetGUID());
    std::shared_ptr<WowUnit> closestUnit = nullptr;
    float minDistanceSq = 40.0f * 40.0f; // Max targeting range (squared)

    // Define common unit flags relevant for attackability (adjust values if needed for 3.3.5)
    const uint32_t UNIT_FLAG_NON_ATTACKABLE = 0x00000002;
    const uint32_t UNIT_FLAG_NOT_ATTACKABLE_1 = 0x00000080; // Often used for friendly/non-attackable NPCs
    const uint32_t UNIT_FLAG_IMMUNE_PC = 0x00000100;      // Immune to player attacks
    // const uint32_t UNIT_FLAG_PVP = 0x00001000;          // Optional: Consider if bot should engage PvP flagged targets
    // const uint32_t UNIT_FLAG_IN_COMBAT = 0x00080000;    // Optional: Consider if bot should prioritize targets already in combat

    // Get the object cache map (returns a copy)
    auto objectMap = m_objectManager->GetObjects(); 
    for (const auto& pair : objectMap) {
        // pair.second is std::shared_ptr<WowObject>
        const auto& obj = pair.second;
        uint64_t currentObjGuid = GuidToUint64(obj->GetGUID());
        if (!obj || currentObjGuid == playerGuid) continue; // Skip null or self
        if (currentObjGuid == m_lastFailedTargetGuid) continue; // Skip last failed target

        // Attempt cast to WowUnit
        std::shared_ptr<WowUnit> unit = std::dynamic_pointer_cast<WowUnit>(obj);
        if (!unit) continue; // Skip non-units

        if (unit->IsDead()) continue; // Skip dead units

        // --- Hostility Check ---
        unit->UpdateDynamicData(); // Ensure flags are up-to-date before checking
        uint32_t unitFlags = unit->GetUnitFlags();

        // Check if the unit is generally attackable
        bool isAttackable = true;
        if (unitFlags & UNIT_FLAG_NON_ATTACKABLE) isAttackable = false;
        if (unitFlags & UNIT_FLAG_NOT_ATTACKABLE_1) isAttackable = false; 
        if (unitFlags & UNIT_FLAG_IMMUNE_PC) isAttackable = false;

        if (!isAttackable) {
             // LogStream ssSkip; ssSkip << "Skipping non-attackable unit GUID 0x" << std::hex << currentObjGuid << " Flags: 0x" << unitFlags; LogMessage(ssSkip.str());
            continue; // Skip non-attackable units (friendly NPCs, immune units, etc.)
        }
        // --- End Hostility Check ---

        // TODO: Add proper hostility check here (e.g., unit->CanAttack(player) or faction check)
        // For now, attacking any living non-player unit in range

        try {
            // Read target position directly
            Vector3 targetPos = {0,0,0};
            void* targetPtr = unit->GetPointer();
            if (targetPtr) {
                 uintptr_t tBase = reinterpret_cast<uintptr_t>(targetPtr);
                 targetPos.x = MemoryReader::Read<float>(tBase + 0x79C);
                 targetPos.y = MemoryReader::Read<float>(tBase + 0x798);
                 targetPos.z = MemoryReader::Read<float>(tBase + 0x7A0);
            } else { continue; } // Skip if pointer invalid

            float dx = playerPos.x - targetPos.x;
            float dy = playerPos.y - targetPos.y;
            float dz = playerPos.z - targetPos.z;
            float distanceSq = dx*dx + dy*dy + dz*dz;

            if (distanceSq < minDistanceSq) {
                minDistanceSq = distanceSq;
                closestUnit = unit;
            }
        } catch (...) {
            // LogStream ssErr; ssErr << "GrindingEngine selectBestTarget: EXCEPTION reading position for GUID 0x" << std::hex << GuidToUint64(unit->GetGUID()); LogMessage(ssErr.str());
            continue; // Skip unit if position read fails
        }
    }

    // If we found a suitable target
    if (closestUnit) {
        uint64_t targetGuid = GuidToUint64(closestUnit->GetGUID());
        LogStream ssTarget; ssTarget << "GrindingEngine selectBestTarget: Found closest valid target GUID 0x" << std::hex << targetGuid << " at distance sqrt(" << minDistanceSq << "). Requesting target.";
        LogMessage(ssTarget.str());
        
        // --- CHANGE: Request target via BotController instead of calling directly --- 
        if (m_botController) {
            m_botController->requestTarget(targetGuid); // Signal main thread
        }
        // --- END CHANGE ---

        // We no longer call TargetUnitByGuid here, so we cannot immediately verify.
        // We also cannot store the pointer/GUID here, as the main thread handles actual targeting.
        // Return false because targeting is requested, not confirmed.
        return false; 
        
        /* --- OLD LOGIC REMOVED ---
        // Call the targeting function (Address 0x00524BF0)
        LogMessage("GrindingEngine selectBestTarget: >>> Calling TargetUnitByGuid NOW <<< ");
        TargetUnitByGuid(targetGuid); 
        LogMessage("GrindingEngine selectBestTarget: <<< TargetUnitByGuid call returned >>>");
        
        // Short delay to allow game state update
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); 

        // Verify target was set
        uint64_t actualTargetGuid = m_objectManager->GetCurrentTargetGUID();
        if (actualTargetGuid == targetGuid) {
            LogMessage("GrindingEngine selectBestTarget: Target set successfully in game.");
            // Store the pointer and GUID
            m_targetUnitPtr = closestUnit; 
            m_currentTargetGuid = targetGuid;
            m_lastFailedTargetGuid = 0; // Clear last failed target on success
            return true;
        } else {
             LogStream ssFail; ssFail << "GrindingEngine Warning: TargetUnitByGuid failed or target changed. Expected 0x" << std::hex << targetGuid << " but got 0x" << actualTargetGuid;
             LogMessage(ssFail.str());
             m_targetUnitPtr = nullptr;
             m_currentTargetGuid = 0;
             m_lastFailedTargetGuid = targetGuid; // Remember this GUID failed
             return false;
        }
        */
    }

    // No suitable target found
    // LogMessage("GrindingEngine selectBestTarget: No suitable target found nearby."); // Can be noisy
    return false; 
}

void GrindingEngine::handleFindingTarget() {
    // This state might be simplified and merged into CHECK_STATE
    if (selectBestTarget()) {
        m_grindState = GrindState::COMBAT; // Or MOVING_TO_TARGET if range check needed first
    } else {
        m_grindState = GrindState::PATHING; // No target, go back to pathing
    }
}

void GrindingEngine::handleMovingToTarget() {
     // TODO: If target selected but out of range/LOS
     // - Get target position
     // - Use ClickToMove/movement towards target
     // - Check distance/LOS periodically
     // - If in range, switch to COMBAT
     // - If target lost/dies, switch back to CHECK_STATE
     LogMessage("GrindingEngine: Moving to target (Not Implemented).");
     m_grindState = GrindState::COMBAT; // Placeholder: Assume in range for now
}

void GrindingEngine::handleCombat() {
    // --- Check Game Target GUID --- 
    uint64_t gameCurrentTargetGuid = m_objectManager->GetCurrentTargetGUID();
    
    // If game has no target, or our initial GUID doesn't match, exit combat.
    if (gameCurrentTargetGuid == 0 || gameCurrentTargetGuid != m_currentTargetGuid) {
        LogMessage("GrindingEngine handleCombat: Target lost or changed externally (GUID mismatch). Exiting Combat.");
        m_targetUnitPtr = nullptr; // Clear pointer just in case
        m_currentTargetGuid = 0;
        m_lastFailedTargetGuid = 0; // Reset failed target
        m_grindState = GrindState::CHECK_STATE;
        return;
    }
    // --- GUID is still valid in game --- 

    // --- Face the Target (ensure we do this early) --- 
    MovementController::GetInstance().FaceTarget(m_currentTargetGuid);
    // --- End Face Target ---

    // --- Re-fetch and Validate Pointer BEFORE IsDead Check --- 
    std::shared_ptr<WowObject> tempTargetBase_DeadCheck = m_objectManager->GetObjectByGUID(gameCurrentTargetGuid);
    std::shared_ptr<WowUnit> targetUnit_DeadCheck = std::dynamic_pointer_cast<WowUnit>(tempTargetBase_DeadCheck);
    if (!targetUnit_DeadCheck) {
        LogMessage("GrindingEngine handleCombat: ERROR - Failed to get valid target pointer before IsDead check. Exiting Combat.");
        m_targetUnitPtr = nullptr; 
        m_currentTargetGuid = 0; 
        m_lastFailedTargetGuid = gameCurrentTargetGuid; 
        m_grindState = GrindState::CHECK_STATE;
        return;
    }
    // Use the freshly fetched pointer for the check
    if (targetUnit_DeadCheck->IsDead()) { 
        LogMessage("GrindingEngine handleCombat: Target died or invalid (using IsDead). Exiting Combat.");
        m_targetUnitPtr = nullptr; 
        m_currentTargetGuid = 0;
        m_lastFailedTargetGuid = 0; 
        m_grindState = GrindState::CHECK_STATE; 
        return;
    }
    // --- Target is Alive --- 

    // Check if GCD is active
    DWORD currentTime = GetTickCount();
    if (currentTime < m_lastGcdTriggerTime + GCD_DURATION) {
        return; // Still on GCD
    }

    // --- Re-fetch and Validate Pointer BEFORE Casting --- 
    // (Seems redundant, but belts and suspenders approach for timing issues)
    std::shared_ptr<WowObject> tempTargetBase_CastCheck = m_objectManager->GetObjectByGUID(gameCurrentTargetGuid);
    m_targetUnitPtr = std::dynamic_pointer_cast<WowUnit>(tempTargetBase_CastCheck); // Update the member pointer here
    if (!m_targetUnitPtr) {
        LogMessage("GrindingEngine handleCombat: ERROR - Failed to get valid target pointer before casting. Exiting Combat.");
        m_currentTargetGuid = 0; 
        m_lastFailedTargetGuid = gameCurrentTargetGuid; 
        m_grindState = GrindState::CHECK_STATE;
        return;
    }
    // --- Pointer is valid for casting --- 

    // --- Execute Rotation --- (Will still warn if empty)
    castSpellFromRotation(); 

    // --- Timeout/Stuck Check ---
    // if (currentTime > m_combatStartTime + 60000) { // e.g., 1 minute timeout
    //     LogMessage("GrindingEngine Warning: Combat lasted over 60 seconds. Exiting.");
    //     m_currentTargetGuid = 0; // Drop target
    //     m_grindState = GrindState::CHECK_STATE;
    //     // TODO: Blacklist target?
    // }
}

void GrindingEngine::castSpellFromRotation() {
    const auto& rotation = m_botController->getCurrentRotation();
    if (rotation.empty()) {
        LogMessage("GrindingEngine Warning: Rotation is empty, cannot cast.");
        return; // No rotation loaded/defined
    }

    int checkedSpells = 0;
    int maxChecks = rotation.size(); 
    bool spellCasted = false;

    while(checkedSpells < maxChecks && !spellCasted) {
        const RotationStep& step = rotation[m_currentRotationIndex];

        if (checkRotationCondition(step)) {
             int cooldownMs = SpellManager::GetSpellCooldownMs(step.spellId); 
             if (cooldownMs == 0) { 
                 LogStream ss;
                 ss << "GrindingEngine: Attempting to cast spell ID: " << step.spellId
                    << " (" << step.spellName << ") on Target: " << m_currentTargetGuid;
                 LogMessage(ss.str());

                 // Request the cast via BotController (runs on main thread)
                 m_botController->requestCastSpell(step.spellId);
                 
                 // We assume the cast will succeed for GCD purposes here.
                 // A more advanced system could wait for confirmation from the main thread.
                 if (step.triggersGCD) {
                     m_lastGcdTriggerTime = GetTickCount();
                     LogMessage("GrindingEngine: GCD Triggered (assumed after cast request).");
                 }
                 spellCasted = true; // Mark as 'casted' to proceed in rotation
                 
                 /* --- OLD DIRECT CALL REMOVED ---
                 bool success = m_spellManager->CastSpell(step.spellId, m_currentTargetGuid, 0, 0);
                 if (success) {
                    if (step.triggersGCD) {
                        m_lastGcdTriggerTime = GetTickCount();
                         LogMessage("GrindingEngine: GCD Triggered.");
                    }
                    spellCasted = true; 
                 } else {
                     LogMessage("GrindingEngine Warning: CastSpell failed.");
                 }
                 */
             } else if (cooldownMs > 0) {
                  // Cooldown active
             } else {
                   LogMessage(("GrindingEngine Warning: GetSpellCooldownMs returned error for spell " + std::to_string(step.spellId)).c_str());
             }
        } 

        m_currentRotationIndex = (m_currentRotationIndex + 1) % rotation.size();
        checkedSpells++;

        if (checkedSpells >= maxChecks && !spellCasted) {
             break;
        }
    } 
}

bool GrindingEngine::checkRotationCondition(const RotationStep& step) {
     // Use GetLocalPlayer() and shared_ptr
     std::shared_ptr<WowPlayer> player = m_objectManager->GetLocalPlayer(); 
     // Use the stored target pointer directly
     std::shared_ptr<WowUnit> target = m_targetUnitPtr; 

     if (!player) return false;

     // Check requiresTarget using the stored pointer
     if (step.requiresTarget && !target) { 
         LogMessage("GrindingEngine CondCheck: Failed - Step requires target, but m_targetUnitPtr is NULL");
         return false; 
     }

     // Check range etc using the stored pointer
     if (step.requiresTarget && target) {
          // Placeholder check using step.castRange
          try {
                // Read positions directly for robustness
                Vector3 playerPos = {0,0,0};
                Vector3 targetPos = {0,0,0};
                // Read Player Pos
                void* playerPtr = player->GetPointer();
                if (playerPtr) {
                    uintptr_t pBase = reinterpret_cast<uintptr_t>(playerPtr);
                    playerPos.x = MemoryReader::Read<float>(pBase + 0x79C);
                    playerPos.y = MemoryReader::Read<float>(pBase + 0x798);
                    playerPos.z = MemoryReader::Read<float>(pBase + 0x7A0);
                } else { throw std::runtime_error("Player pointer null"); }
                // Read Target Pos
                void* targetPtr = target->GetPointer();
                if (targetPtr) {
                    uintptr_t tBase = reinterpret_cast<uintptr_t>(targetPtr);
                    targetPos.x = MemoryReader::Read<float>(tBase + 0x79C);
                    targetPos.y = MemoryReader::Read<float>(tBase + 0x798);
                    targetPos.z = MemoryReader::Read<float>(tBase + 0x7A0);
                } else { throw std::runtime_error("Target pointer null"); }
                
                float dx = playerPos.x - targetPos.x;
                float dy = playerPos.y - targetPos.y;
                float dz = playerPos.z - targetPos.z;
                float dist = std::sqrt(dx*dx + dy*dy + dz*dz); 

                float playerCombatReach = 2.5f;
                float targetCombatReach = 2.5f;
                float effectiveRange = step.castRange + playerCombatReach + targetCombatReach;
                 if (dist > effectiveRange) {
                     // Uncomment the log message to check if range is the issue
                     LogStream ssRange; ssRange << "GrindingEngine CondCheck: Failed - Out of Range. Dist=" << dist << ", MaxEffRange=" << effectiveRange << " (SpellRange=" << step.castRange << ")"; LogMessage(ssRange.str());
                     return false;
                 }
          } catch(const std::exception& e) { 
              LogStream ssErr; ssErr << "GrindingEngine CondCheck: EXCEPTION reading positions for range check: " << e.what(); LogMessage(ssErr.str());
              return false; 
          } 
     }

     // --- Player Health/Mana Checks (using cached values is likely fine here) ---
     try {
        float playerHealthPct = 0.0f;
        int playerMaxHealth = player->GetMaxHealth();
        if (playerMaxHealth > 0) {
            playerHealthPct = (static_cast<float>(player->GetHealth()) * 100.0f) / playerMaxHealth;
        }
        
        float playerManaPct = 0.0f;
        if (player->GetPowerType() == 0) { // Check if power type is Mana
            int playerMaxMana = player->GetMaxPower();
            if (playerMaxMana > 0) {
                playerManaPct = (static_cast<float>(player->GetPower()) * 100.0f) / playerMaxMana;
            }
        }

        if (playerHealthPct < step.minPlayerHealthPercent || playerHealthPct > step.maxPlayerHealthPercent) {
             return false;
        }
        if (playerManaPct < step.minPlayerManaPercent || playerManaPct > step.maxPlayerManaPercent) {
             return false;
        }
     } catch (const std::runtime_error& e) {
         LogMessage(("GrindingEngine CondCheck: Error reading player stats: " + std::string(e.what())).c_str());
         return false; 
     }

     // --- Target Health Check (using cached values is likely fine here) ---
     if (step.requiresTarget && target) {
         try {
             float targetHealthPct = 0.0f;
             int targetMaxHealth = target->GetMaxHealth();
             if (targetMaxHealth > 0) {
                  targetHealthPct = (static_cast<float>(target->GetHealth()) * 100.0f) / targetMaxHealth;
             }

             if (targetHealthPct < step.minTargetHealthPercent || targetHealthPct > step.maxTargetHealthPercent) {
                 return false;
             }
         } catch (const std::runtime_error& e) {
             LogMessage(("GrindingEngine CondCheck: Error reading target stats: " + std::string(e.what())).c_str());
             return false; 
         }
     }

     return true; 
}

void GrindingEngine::handleRecovering() {
    // TODO: Implement recovery logic
    // - Check if health/mana are below desired threshold
    // - Use food/drink items (requires inventory/item interaction logic)
    // - Wait until recovered
    // - Transition back to CHECK_STATE when done
    LogMessage("GrindingEngine: Recovering (Not Implemented).");
    std::this_thread::sleep_for(std::chrono::seconds(5)); // Placeholder wait
    m_grindState = GrindState::CHECK_STATE;
} 