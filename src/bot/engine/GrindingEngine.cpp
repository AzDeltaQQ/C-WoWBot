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

#include <thread> // Include for std::thread
#include <chrono> // For sleep
#include <cmath>  // For std::sqrt
#include <algorithm> // For std::max

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
    if (!m_objectManager || !m_botController || !m_spellManager ) {
         LogMessage("GrindingEngine Error: Core components missing.");
         m_grindState = GrindState::ERROR_STATE;
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
    if (m_grindState != GrindState::COMBAT && m_grindState != GrindState::LOOTING) {
        if (currentTargetGuidInGame != knownTargetGuid) {
            shouldClearPointer = true;
        }
    }
    // Condition 2: We ARE in COMBAT, but the game target changed to something *else* (not just cleared to 0).
    else if (m_grindState == GrindState::COMBAT) {
        if (currentTargetGuidInGame != 0 && currentTargetGuidInGame != knownTargetGuid) {
             shouldClearPointer = true;
        }
        // NOTE: We explicitly DO NOT clear if currentTargetGuidInGame is 0 while in COMBAT,
        // as this likely means the target died, and we want handleCombat to check it.
    }
    // Condition 3: Always clear if our known pointer is non-null but the object is gone from the manager
    // (This check might be redundant if GetObjectByGUID handles it, but adds safety)
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
        if (currentTargetGuidInGame != 0) {
            std::shared_ptr<WowObject> tempTargetBase = m_objectManager->GetObjectByGUID(currentTargetGuidInGame);
            m_targetUnitPtr = std::dynamic_pointer_cast<WowUnit>(tempTargetBase);
             // Update knownTargetGuid after potential re-acquire
             knownTargetGuid = GuidToUint64(m_targetUnitPtr ? m_targetUnitPtr->GetGUID() : WGUID{0,0}); 
        }
    }
    // --- End Refined Pointer Update Logic ---

    // Store the potentially updated game target GUID for use in states
    m_currentTargetGuid = currentTargetGuidInGame; 

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
                
                // --- Calculate Effective Combat Range --- 
                const auto& rotation = m_botController->getCurrentRotation();
                float maxRange = 0.0f;
                if (!rotation.empty()) {
                    for(const auto& step : rotation) {
                        if (step.castRange > maxRange) {
                            maxRange = step.castRange;
                        }
                    }
                    // Set effective range slightly less than max, but at least melee
                    m_effectiveCombatRange = (std::max)(5.0f, maxRange - 4.0f); // Increased buffer
                } else {
                    m_effectiveCombatRange = 5.0f; // Default to melee if no rotation
                }
                LogStream ssRange; ssRange << "GrindingEngine: Setting Effective Combat Range to " << m_effectiveCombatRange << " (Max Spell Range: " << maxRange << ")";
                LogMessage(ssRange.str());
                // --- End Calculate Range ---

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
                 
                 // *** INCREASED DELAY ***
                 std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give client more time
                 // **********************

                 // --- Calculate Effective Combat Range (Moved slightly earlier) ---
                 const auto& rotation_pathing = m_botController->getCurrentRotation();
                 float maxRange_pathing = 0.0f;
                 if (!rotation_pathing.empty()) {
                     for(const auto& step : rotation_pathing) {
                         if (step.castRange > maxRange_pathing) {
                             maxRange_pathing = step.castRange;
                         }
                     }
                     m_effectiveCombatRange = (std::max)(5.0f, maxRange_pathing - 4.0f); 
                 } else {
                     m_effectiveCombatRange = 5.0f; 
                 }
                 LogStream ssRangePath; ssRangePath << "GrindingEngine: Setting Effective Combat Range to " << m_effectiveCombatRange << " (Max Spell Range: " << maxRange_pathing << ")";
                 LogMessage(ssRangePath.str());
                 // --- End Calculate Range ---
                 
                 // *** ADD FACETARGET HERE ***
                 MovementController::GetInstance().FaceTarget(GuidToUint64(m_targetUnitPtr->GetGUID()));
                 // **************************

                 m_grindState = GrindState::COMBAT;
                 m_combatStartTime = GetTickCount();
                 m_currentRotationIndex = 0;

            } else {
                // No valid target yet, continue pathing and checking
                handlePathing();    // Move along the path
                selectBestTarget(); // Check for new targets while pathing (request if found)
            }
            break;

        case GrindState::COMBAT:
            handleCombat(); // Will transition back to CHECK_STATE or LOOTING if target lost/dies
            break;

        case GrindState::LOOTING:
            handleLooting(); // Handle moving to and looting the corpse
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
        // Use MovementController to move towards the target point
        MovementController::GetInstance().ClickToMove(targetPoint, playerPos);
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

        // Refined check: Skip if friendly (NOT_ATTACKABLE_1) or explicitly non-attackable.
        // UNIT_FLAG_NOT_ATTACKABLE_1 (0x80) is often set on friendly NPCs.
        // UNIT_FLAG_NON_ATTACKABLE (0x02) means cannot be attacked at all.
        bool isFriendly = (unitFlags & UNIT_FLAG_NOT_ATTACKABLE_1) != 0;
        bool isGenerallyNonAttackable = (unitFlags & UNIT_FLAG_NON_ATTACKABLE) != 0;
        bool isImmuneToPC = (unitFlags & UNIT_FLAG_IMMUNE_PC) != 0; // Keep this check

        if (isFriendly || isGenerallyNonAttackable || isImmuneToPC) {
             // Optional: More detailed logging for skipped units
             LogStream ssSkip; 
             ssSkip << "Skipping unit GUID 0x" << std::hex << currentObjGuid 
                    << " Flags: 0x" << unitFlags 
                    << " (Friendly=" << isFriendly 
                    << ", NonAttack=" << isGenerallyNonAttackable 
                    << ", ImmunePC=" << isImmuneToPC << ")"; 
             // LogMessage(ssSkip.str()); // Uncomment for detailed debugging
            continue; 
        }
        // --- End Hostility Check ---

        // TODO: Add proper hostility check here (e.g., unit->CanAttack(player) or faction check)

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
    }

    // No suitable target found
    // LogMessage("GrindingEngine selectBestTarget: No suitable target found nearby."); // Can be noisy
    return false; 
}

// Placeholder states, logic merged into main state machine
void GrindingEngine::handleFindingTarget() {
    m_grindState = GrindState::CHECK_STATE;
}

void GrindingEngine::handleMovingToTarget() {
    m_grindState = GrindState::COMBAT; // Assume in range for now
}

void GrindingEngine::handleCombat() {
    if (!m_targetUnitPtr || !m_objectManager) {
        LogMessage("GrindingEngine COMBAT: Target pointer or ObjectManager is null. Exiting combat state.");
        m_targetUnitPtr = nullptr;
        m_grindState = GrindState::CHECK_STATE;
        return;
    }

    // Check if target is still valid and in the object manager cache
    uint64_t currentTargetGuidInGame = m_objectManager->GetCurrentTargetGUID();
    uint64_t myTargetGuid = GuidToUint64(m_targetUnitPtr->GetGUID());

    // 1. Check if target is dead
    if (m_targetUnitPtr->IsDead()) {
        LogStream ss; ss << "GrindingEngine COMBAT: Target " << myTargetGuid << " is dead.";
        LogMessage(ss.str());

        // Check if lootable AND if looting is enabled in BotController
        bool shouldLoot = m_botController->isLootingEnabled(); // Get setting
        if (shouldLoot && m_targetUnitPtr->IsLootable()) { 
            LogMessage("GrindingEngine COMBAT: Target is lootable and looting enabled. Entering LOOTING state.");
            m_grindState = GrindState::LOOTING;
            // Keep m_targetUnitPtr pointing to the corpse for looting
            return; // Exit handleCombat, proceed to LOOTING state
        } else {
             if (!shouldLoot) {
                  LogMessage("GrindingEngine COMBAT: Target is dead, but looting is disabled. Skipping loot.");
             } else { // implies !m_targetUnitPtr->IsLootable()
                  LogMessage("GrindingEngine COMBAT: Target is dead but not lootable. Skipping loot.");
             }
            TargetUnitByGuid(0); // Clear target in game
            m_targetUnitPtr = nullptr;
            m_grindState = GrindState::CHECK_STATE;
            return;
        }
    }

    // 2. Check if target GUID changed in game (e.g., player tabbed)
    if (currentTargetGuidInGame != myTargetGuid) {
        LogStream ss; ss << "GrindingEngine COMBAT: Target changed in game (Now: " << currentTargetGuidInGame << "). Resetting.";
        LogMessage(ss.str());
        // We lost our target, clear pointer and go check state
        m_targetUnitPtr = nullptr; 
        m_grindState = GrindState::CHECK_STATE;
        return;
    }

    // 3. Check if the target pointer itself became invalid somehow (e.g., object removed from manager)
    if (!m_objectManager->GetObjectByGUID(myTargetGuid)) {
        LogStream ss; ss << "GrindingEngine COMBAT: Target " << myTargetGuid << " no longer found in ObjectManager. Resetting.";
        LogMessage(ss.str());
        TargetUnitByGuid(0); // Clear target in game
        m_targetUnitPtr = nullptr;
        m_grindState = GrindState::CHECK_STATE;
        return;
    }

    // --- Target is alive and still targeted, proceed with combat --- 

    // Get player and target positions
    // Get player position directly
    std::shared_ptr<WowPlayer> player_combat = m_objectManager->GetLocalPlayer();
    Vector3 playerPos;
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
        playerPos.x = MemoryReader::Read<float>(baseAddr + 0x79C); // Use literal offset
        playerPos.y = MemoryReader::Read<float>(baseAddr + 0x798); // Use literal offset
        playerPos.z = MemoryReader::Read<float>(baseAddr + 0x7A0); // Use literal offset
    } catch (...) {
        LogMessage("GrindingEngine COMBAT: Failed to read player position. Skipping combat step.");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return;
    }
    // End get player position

    // Get target position directly (bypassing cache)
    Vector3 targetPos;
    void* targetPtr_combat = m_targetUnitPtr->GetPointer();
    if (!targetPtr_combat) {
        LogMessage("GrindingEngine COMBAT: Failed to get target pointer. Skipping combat step.");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Maybe reset state here?
        m_targetUnitPtr = nullptr;
        m_grindState = GrindState::CHECK_STATE;
        return;
    }
    try {
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(targetPtr_combat);
        targetPos.x = MemoryReader::Read<float>(baseAddr + 0x79C); // Use literal offset
        targetPos.y = MemoryReader::Read<float>(baseAddr + 0x798); // Use literal offset
        targetPos.z = MemoryReader::Read<float>(baseAddr + 0x7A0); // Use literal offset
    } catch (...) {
        LogMessage("GrindingEngine COMBAT: Failed to read target position. Skipping combat step.");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // Maybe reset state here?
        m_targetUnitPtr = nullptr;
        m_grindState = GrindState::CHECK_STATE;
        return;
    }
    // End get target position

    // Calculate 3D distance
    float dx = playerPos.x - targetPos.x;
    float dy = playerPos.y - targetPos.y;
    float dz = playerPos.z - targetPos.z;
    float distanceSq = dx*dx + dy*dy + dz*dz;
    float distance = std::sqrt(distanceSq); 

    // Ranges are now determined by m_effectiveCombatRange calculated on combat entry

    // 1. Check if target is outside effective combat range
    if (distance > m_effectiveCombatRange) { 
        LogStream ss; ss << "GrindingEngine COMBAT: Target distance (" << distance << ") > Effective Range (" << m_effectiveCombatRange << "). Moving closer.";
        LogMessage(ss.str());
        MovementController::GetInstance().ClickToMove(targetPos, playerPos); // Move towards target
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Short delay while moving
        return; // Stay in COMBAT state, but don't cast yet
    }
    // Optional: Add kiting logic here if needed later
    // else if (distance < MIN_PULL_RANGE) { ... handle kiting ... }

    // 2. Target is within effective combat range
    else {
        LogStream ss; ss << "GrindingEngine COMBAT: Target distance (" << distance << ") <= Effective Range (" << m_effectiveCombatRange << "). Stopping and Engaging.";
        LogMessage(ss.str());

        // Stop moving (Redundant, but ensures we stop if we *were* moving closer)
        MovementController::GetInstance().Stop(); 
        
        // Face the target (Convert WGUID to uint64_t)
        // MovementController::GetInstance().FaceTarget(GuidToUint64(m_targetUnitPtr->GetGUID())); // *** REMOVE THIS - MOVED TO PATHING TRANSITION ***

        // --- In range and facing, execute combat rotation --- 
        castSpellFromRotation();
    }

    // Timeout check (e.g., 60 seconds)
    if (GetTickCount() - m_combatStartTime > 60000) {
        LogMessage("GrindingEngine COMBAT: Combat timeout reached. Resetting.");
        TargetUnitByGuid(0); // Clear target
        m_targetUnitPtr = nullptr;
        m_grindState = GrindState::CHECK_STATE;
        return;
    }
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
                 m_botController->requestCastSpell(step.spellId, m_currentTargetGuid);
                 
                 spellCasted = true; // Mark as 'casted' to proceed in rotation
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
                float effectiveRange = step.castRange; // Use exact spell range without added reach
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

    // --- Final Check: Spell Cooldown/GCD ---
    if (m_spellManager) { // Ensure SpellManager is valid
        int cooldownMs = m_spellManager->GetSpellCooldownMs(step.spellId);
        if (cooldownMs > 0) {
            return false; // Spell is on cooldown or GCD
        } else if (cooldownMs < 0) {
            // Handle error case from GetSpellCooldownMs
            LogStream ssErr; ssErr << "GrindingEngine CondCheck: Error getting cooldown for spell " << step.spellId; LogMessage(ssErr.str());
            return false; // Treat error as failure
        }
    } else {
        LogMessage("GrindingEngine CondCheck: Failed - SpellManager instance is null.");
        return false; // Cannot check cooldown if SpellManager is null
    }

     return true; 
}

void GrindingEngine::handleRecovering() {
    // TODO: Implement recovery logic
    // - Check if health/mana are below desired threshold
    // - Use food/drink items (requires inventory/item interaction logic)
    // - Wait until recovered
    // - Transition back to CHECK_STATE when done
    LogMessage("GrindingEngine: Recovering (Not Implemented). Will transition back to check state.");
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Short delay before checking again
    m_grindState = GrindState::CHECK_STATE;
}

// --- New State Handler Implementation ---

void GrindingEngine::handleLooting() {
    const float LOOT_DISTANCE_THRESHOLD = 4.0f; // How close to get before interacting
    const float LOOT_DISTANCE_THRESHOLD_SQ = LOOT_DISTANCE_THRESHOLD * LOOT_DISTANCE_THRESHOLD;

    if (!m_targetUnitPtr || !m_objectManager) {
        LogMessage("GrindingEngine LOOTING: Corpse pointer or ObjectManager is null. Back to CHECK_STATE.");
        m_targetUnitPtr = nullptr;
        m_grindState = GrindState::CHECK_STATE;
        return;
    }

    // Ensure the object is still targetable (corpses are often still WowUnit)
    // We keep the pointer from combat, so it should still be a WowUnit initially.
    // A check like IsDead should have already happened in combat handler.

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
    // End get player position

    // Get corpse position DIRECTLY from memory
    Vector3 corpsePos = {0,0,0};
    void* corpsePtr = m_targetUnitPtr->GetPointer();
    if (!corpsePtr) {
        LogMessage("GrindingEngine LOOTING: Failed to get corpse pointer. Aborting loot.");
        m_targetUnitPtr = nullptr;
        m_grindState = GrindState::CHECK_STATE;
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
        // Basic validation: Check if position seems reasonable (not exactly 0,0,0 unless it's truly there)
        if (corpsePos.x == 0.0f && corpsePos.y == 0.0f && corpsePos.z == 0.0f) {
             LogMessage("GrindingEngine LOOTING: Warning - Read corpse position as (0,0,0). Might be invalid. Proceeding cautiously.");
             // Optionally, abort if 0,0,0 is always invalid in your context
             // m_targetUnitPtr = nullptr;
             // m_grindState = GrindState::CHECK_STATE;
             // return;
        }
    } catch (...) {
        LogMessage("GrindingEngine LOOTING: Failed to read corpse position. Aborting loot.");
        m_targetUnitPtr = nullptr;
        m_grindState = GrindState::CHECK_STATE;
        return;
    }
    // End get corpse position

    // Calculate distance squared using DIRECTLY READ positions
    float dx = playerPos.x - corpsePos.x;
    float dy = playerPos.y - corpsePos.y;
    float dz = playerPos.z - corpsePos.z;
    float distanceSq = dx*dx + dy*dy + dz*dz;

    if (distanceSq > LOOT_DISTANCE_THRESHOLD_SQ) {
        // Too far, move closer using the DIRECTLY READ corpse position
        LogStream ss; ss << "GrindingEngine LOOTING: Moving to corpse. Distance: " << std::sqrt(distanceSq);
        LogMessage(ss.str());
        MovementController::GetInstance().ClickToMove(corpsePos, playerPos); // Use the freshly read corpsePos
        // Stay in LOOTING state
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Short delay while moving
    } else {
        // Close enough, stop and interact
        LogMessage("GrindingEngine LOOTING: Reached corpse. Stopping and Interacting.");
        MovementController::GetInstance().Stop();
        
        // --- Re-target the corpse before interacting ---
        uint64_t corpseGuid = GuidToUint64(m_targetUnitPtr->GetGUID());
        LogStream ssTarget; ssTarget << "GrindingEngine LOOTING: Requesting target corpse GUID 0x" << std::hex << corpseGuid;
        LogMessage(ssTarget.str());
        if (m_botController) {
             m_botController->requestTarget(corpseGuid); // Request targeting via BotController
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Shorter initial delay
        
        // --- Wait for target to update in-game (with timeout) ---
        bool targetSet = false;
        const int maxWaitMs = 1000; // Max time to wait for target update (1 second)
        const int checkIntervalMs = 100; // Check every 100ms
        int waitedMs = 0;
        while (waitedMs < maxWaitMs) {
             uint64_t currentGuidInGame = m_objectManager->GetCurrentTargetGUID();
             if (currentGuidInGame == corpseGuid) {
                  LogMessage("GrindingEngine LOOTING: Confirmed target set in game.");
                  targetSet = true;
                  break;
             }
             std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
             waitedMs += checkIntervalMs;
        }
        if (!targetSet) {
            LogMessage("GrindingEngine LOOTING: Timed out waiting for target to be set in game. Aborting loot.");
            m_targetUnitPtr = nullptr; // Clear pointer if we couldn't target
            m_grindState = GrindState::CHECK_STATE;
            return; // Exit looting state
        }
        // --- End Wait ---

        // Attempt interaction (only if target was confirmed)
        // bool interacted = m_targetUnitPtr->Interact(); 
        // LogStream ss; ss << "GrindingEngine LOOTING: Interact() call returned: " << (interacted ? "true" : "false");
        // LogMessage(ss.str());
        if (m_botController) {
            LogStream ssI; ssI << "GrindingEngine LOOTING: Requesting interaction with GUID 0x" << std::hex << corpseGuid;
            LogMessage(ssI.str());
            m_botController->requestInteract(corpseGuid);
        } else {
            LogMessage("GrindingEngine LOOTING Error: BotController null, cannot request interaction.");
        }

        // Wait a bit for auto-loot to happen (adjust as needed)
        std::this_thread::sleep_for(std::chrono::milliseconds(750));

        // Loot attempt finished, clear pointer and go back to checking
        LogMessage("GrindingEngine LOOTING: Loot attempt finished. Back to CHECK_STATE.");
        m_targetUnitPtr = nullptr;
        m_grindState = GrindState::CHECK_STATE;
    }
} 