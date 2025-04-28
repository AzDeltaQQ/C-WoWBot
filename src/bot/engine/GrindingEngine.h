#pragma once

#include "../core/BotController.h" // Include BotController for state/access
#include "../../game/ObjectManager.h" // Need ObjectManager
#include "../../game/SpellManager.h"  // Need SpellManager
#include "../core/RotationStep.h" // Include RotationStep
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <chrono> // For time tracking
#include <windows.h> // For GetTickCount
#include <memory>

// Forward declarations
class PathManager;
class MovementController;

class GrindingEngine {
public:
    // Define internal states for the Grinding Engine
    enum class GrindState {
        IDLE,           // Doing nothing (shouldn't stay here long if started)
        CHECK_STATE,    // Deciding what to do next (pathing, finding target, combat)
        PATHING,        // Following the loaded path
        FINDING_TARGET, // Looking for nearby suitable enemies
        MOVING_TO_TARGET, // Moving towards the selected target if out of range/LOS
        COMBAT,         // Actively fighting the target
        LOOTING,        // Moving to and interacting with a corpse
        RECOVERING,     // Eating/Drinking/Waiting after combat
        ERROR_STATE     // An unrecoverable error occurred
    };

    // Constructor now takes BotController and ObjectManager
    GrindingEngine(BotController* controller, ObjectManager* objectManager);
    ~GrindingEngine();

    // Start the grinding logic (potentially in a new thread)
    void start();

    // Request the grinding logic to stop
    void stop();

    // Check if the engine is currently running
    bool isRunning() const;

private:
    void run(); // Main loop executed in the thread
    void updateState(); // State machine logic

    // State-specific handlers
    void handlePathing();
    void handleFindingTarget();
    void handleMovingToTarget();
    void handleCombat();
    void handleLooting();
    void handleRecovering();

    bool selectBestTarget(); // Logic to find and target an enemy
    bool checkRotationCondition(const RotationStep& step); // Check spell conditions
    void castSpellFromRotation(); // Find and cast next available spell

    // Non-owning pointer to the core BotController
    BotController* m_botController = nullptr;
    // Non-owning pointer to the ObjectManager (needed for player position)
    ObjectManager* m_objectManager = nullptr;
    // Non-owning pointer to the SpellManager (Needs to be passed or accessed via BotController)
    SpellManager* m_spellManager = nullptr;

    // State
    std::atomic<bool> m_isRunning = {false};
    std::atomic<bool> m_stopRequested = {false};

    // Engine-specific state
    GrindState m_grindState = GrindState::IDLE;
    uint64_t m_currentTargetGuid = 0;
    std::shared_ptr<WowUnit> m_targetUnitPtr = nullptr; // Store the pointer too
    int m_currentRotationIndex = 0; // Index for iterating through rotation
    uint64_t m_lastFailedTargetGuid = 0; // Track last GUID that TargetUnitByGuid failed on
    DWORD m_combatStartTime = 0; // Track combat duration
    float m_effectiveCombatRange = 5.0f; // Calculated based on rotation spells

    // Pathing state
    int m_currentPathIndex = -1; // -1 indicates no active path segment

    // Thread for the engine's main loop
    std::thread m_engineThread;
}; 