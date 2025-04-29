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

// Undefine the ERROR macro from windows.h to avoid conflict with enum member
#ifdef ERROR
#undef ERROR
#endif

#include <memory>
#include "../../game/wowobject.h"

// Forward declarations
class PathManager;
class MovementController;
class BotController;
class SpellManager;
class WowUnit;

class GrindingEngine {
public:
    // Engine State Definition
    enum class EngineState {
        IDLE,
        FINDING_TARGET,
        MOVING_TO_TARGET,
        COMBAT,
        LOOTING,
        MOVING_TO_CORPSE,   // Assuming this exists for corpse runs
        MOVING_TO_VENDOR,   // <-- NEW
        VENDERING,          // <-- NEW
        MOVING_TO_GRIND_SPOT, // <<< ADDED
        RESTING,
        RETURNING_TO_GRINDSPOT,
        ERROR               // <-- Should now be safe
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

    // Get the current state of the engine
    EngineState getCurrentState() const;

    // Get the current vendor GUID
    uint64_t getCurrentVendorGuid() const { return m_vendorGuid; }

    // Public configuration methods if needed (e.g., set pull distance?)

private:
    void run(); // Main loop executed in the thread
    void updateState(); // State machine logic

    // State-specific handlers (OLD - REMOVE THESE)
    // void handlePathing();
    // void handleFindingTarget();
    // void handleMovingToTarget();
    // void handleCombat();
    // void handleLooting();
    // void handleRecovering();

    // --- Correct Helper Function Declarations --- 
    bool selectBestTarget(); // Logic to find and target an enemy - Returns true if target requested
    bool checkRotationCondition(const RotationStep& step); // Check spell conditions
    void castSpellFromRotation(); // Find and cast next available spell
    // --------------------------------------------

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
    EngineState m_currentState = EngineState::IDLE;
    uint64_t m_currentTargetGuid = 0;
    std::shared_ptr<WowUnit> m_targetUnitPtr = nullptr; // Store the pointer too
    int m_currentRotationIndex = 0; // Index for iterating through rotation
    uint64_t m_lastFailedTargetGuid = 0; // Track last GUID that TargetUnitByGuid failed on
    DWORD m_combatStartTime = 0; // Track combat duration
    float m_effectiveCombatRange = 5.0f; // Calculated based on rotation spells

    // Pathing state
    int m_currentPathIndex = -1; // -1 indicates no active path segment

    // State for vendor runs
    EngineState m_preVendorState = EngineState::IDLE; // State before starting vendor run
    Vector3 m_grindSpotLocation; // Store location to return to
    uint64_t m_vendorGuid = 0; // Store the target vendor's GUID
    std::string m_vendorName = "Repair"; // TODO: Make this configurable
    int m_currentVendorPathIndex = -1; // Track progress along vendor path
    std::vector<Vector3> m_currentVendorPathPoints; // Store the vendor path points locally
    bool m_vendorPathLoaded = false; // Flag if vendor path was successfully loaded
    int m_sellAttemptCounter = 0; // Counter for selling attempts/delays
    DWORD m_vendorInteractionTimer = 0; // Timer for waiting for vendor window

    // Pathing
    std::vector<Vector3> m_currentPath; // Holds either grind or vendor path temporarily
    Vector3 m_storedGrindLocation; // Store location before vendor run
    std::string m_targetVendorName; // <<< ADDED: Store vendor name

    // Thread for the engine's main loop
    std::thread m_engineThread;

    // --- State Handlers (Declarations) ---
    void processStateIdle();
    void processStateFindingTarget();
    void processStateMovingToTarget();
    void processStateCombat();
    void processStateLooting();
    void processStateMovingToCorpse();
    void processStateMovingToVendor();
    void processStateVendering();
    void processStateMovingToGrindSpot();
    void processStateReturningToGrindspot();
    void processStateResting();
    // -------------------------------------

    bool checkBagsAndTransition(); // Helper to check bags

    // Constants
    const float PULL_DISTANCE = 30.0f;
    const float LOOT_DISTANCE = 5.0f;
    const float VENDOR_INTERACT_DISTANCE = 5.0f;
    const int BAG_FULL_THRESHOLD = 1; // Trigger vendor run if <= 1 free slot
    const int VENDORING_TIMEOUT_MS = 10000; // Max time to wait for vendor window
    const int SELL_DELAY_MS = 200; // Delay between selling items

    // --- NEW: Vendor Interaction State ---
    enum class VendorSubState {
        FINDING_VENDOR,
        APPROACHING_VENDOR,
        REQUESTING_INTERACT,
        WAITING_FOR_WINDOW_OPEN,
        SELLING_ITEMS,
        SELLING_ITEM_LOOP,
        REQUESTING_CLOSE,
        WAITING_FOR_WINDOW_CLOSE,
        FINISHED
    };
    VendorSubState m_vendorSubState = VendorSubState::FINDING_VENDOR;
    std::chrono::steady_clock::time_point m_lastVendorActionTime;
    const std::chrono::milliseconds VENDOR_ACTION_TIMEOUT{5000}; // Timeout for waiting for window open/close
    const std::chrono::milliseconds SELL_DELAY{250}; // Delay between sell requests
    std::vector<uint64_t> m_itemsToSellGuids;
    // std::vector<int> m_itemsToSellItemIDs; // REMOVED - Selling handled by Lua script
    // size_t m_currentItemSellIndex = 0; // REMOVED - Selling handled by Lua script
    // --- END NEW ---

    // --- Helper Functions (REMOVE DUPLICATE SECTION) ---
    // std::shared_ptr<WowUnit> selectBestTarget(); 
    // void castSpellFromRotation();
    // bool checkRotationCondition(const RotationStep& step);
    // bool checkBagsAndTransition();
    // --- NEW Vendor/Utility Helpers ---
    std::shared_ptr<WowUnit> findVendorUnitByName(const std::string& targetName);
    bool ReadUnitPosition(std::shared_ptr<WowObject> unit, Vector3& outPosition);
    float CalculateDistanceSq(const Vector3& p1, const Vector3& p2);
    // std::vector<int> getItemsToSell(); // REMOVED - Selling handled by Lua script
    // -----------------------------
}; 