#pragma once

#include <memory> // For std::unique_ptr
#include <atomic> // For std::atomic_bool
#include <string> // <-- Add this include
#include <vector> // Add for vector return type
#include <cstdint> // For uint64_t
#include <filesystem> // Add for directory iteration
#include <mutex>      // <-- Add this include
#include "RotationStep.h" // Include the new struct definition
#include "../../game/wowobject.h" // <-- ADDED: Correct include for Vector3 definition
#include <queue> // Added for request queue
#include <functional> // Added for std::function
#include <deque> // Use deque for efficient front removal

// Forward declarations to reduce header dependencies
class ObjectManager;
class SpellManager;
class GrindingEngine;
class PathManager;
class PathRecorder;

// Define function pointer type for SellItemByGuid
// typedef void (__cdecl* tSellItemByGuid)(uint64_t itemGuid, uint64_t vendorGuid); // <<< REMOVE

class BotController {
public:
    enum class State {
        IDLE,
        RUNNING,
        PATH_RECORDING
    };

    enum class EngineType {
        NONE,
        GRINDING,
        FISHING
        // Add other engine types like QUESTING, GATHERING later
    };

    BotController();
    ~BotController();

    // Initialization with essential systems
    void initialize(
        ObjectManager* objManager,
        SpellManager* spellManager
    );

    // Main bot control
    void start();
    void stop();

    // Engine management
    void setEngine(EngineType type);
    EngineType getCurrentEngineType() const;

    // Path recording control (delegates to PathRecorder/PathManager)
    void startGrindPathRecording(int intervalMs);
    void stopGrindPathRecording();
    void clearCurrentGrindPath();
    bool saveCurrentGrindPath(const std::string& filename);
    bool loadGrindPathByName(const std::string& pathName);
    std::vector<std::string> getAvailableGrindPathNames() const;
    std::string getCurrentGrindPathName() const;
    
    // --- Vendor Path recording/management (NEW) ---
    void startVendorPathRecording(int intervalMs, const std::string& vendorName);
    void stopVendorPathRecording();
    void clearCurrentVendorPath();
    bool saveCurrentVendorPath(const std::string& filename, const std::string& vendorName);
    bool loadVendorPathByName(const std::string& pathName);
    std::vector<std::string> getAvailableVendorPathNames() const;
    std::string getCurrentVendorPathName() const;
    std::string getCurrentVendorName() const;
    // --------------------------------------------

    // --- Rotation Methods (New) ---
    std::vector<std::string> getAvailableRotationNames();
    bool loadRotationByName(const std::string& name);
    const std::string& getCurrentRotationName() const;
    const std::vector<RotationStep>& getCurrentRotation() const;
    void loadRotation(const std::string& filename);
    // -----------------------------

    // --- Add method to get current vendor path points ---
    const std::vector<Vector3>& getLoadedVendorPathPoints() const;

    // State query
    State getCurrentState() const;
    bool isRunning() const;

    // --- Main Loop / Request Processing ---
    // void run(); // Previous run method, replaced by processRequests
    void processRequests(); // Add declaration for the processing function

    // Accessors for components (optional, provide as needed)
    const PathManager* getPathManager() const; 
    const PathRecorder* getPathRecorder() const;

    // --- Looting Setting --- 
    void setLootingEnabled(bool enabled);
    bool isLootingEnabled() const;

    // --- Request Handling --- 
    void requestTarget(uint64_t guid);      // Add back declaration
    void requestCastSpell(uint32_t spellId, uint64_t targetGuid); // Add targetGuid parameter
    void requestInteract(uint64_t targetGuid);
    void requestSellItem(int bagIndex, int slotIndex); // NEW
    void requestCloseVendorWindow();
    void requestLuaTestScript(const std::string& script); // <<< ADDED

    // --- UI State Accessors ---
    bool getIsVendorWindowVisible() const;
    void setIsVendorWindowVisible(bool isVisible);

private:
    // Core systems (non-owning pointers)
    ObjectManager* m_objectManager = nullptr;
    SpellManager* m_spellManager = nullptr;

    // Bot components (owned or managed)
    std::unique_ptr<PathManager> m_pathManager;
    std::unique_ptr<PathRecorder> m_pathRecorder;
    std::unique_ptr<GrindingEngine> m_currentEngine; // Example for grinding

    // Bot state
    std::atomic<State> m_currentState = {State::IDLE};
    EngineType m_currentEngineType = EngineType::NONE;
    std::atomic<bool> m_stopRequested = {false};
    std::string m_currentVendorName;

    // --- Request Queueing Members --- 
    std::mutex m_requestMutex;           // Mutex to protect request variables
    
    // Target request (only store latest)
    uint64_t m_targetRequest = 0;        
    bool m_hasTargetRequest = false;     // Flag indicating a target request is pending
    
    // Interact request (only store latest)
    uint64_t m_interactRequest = 0;          
    bool m_hasInteractRequest = false;       // Flag indicating an interact request is pending
    
    // Cast requests (queue)
    std::deque<std::pair<uint32_t, uint64_t>> m_castRequests; // SpellId, TargetGuid
    
    // Sell requests (queue)
    std::deque<std::pair<int, int>> m_sellRequests; // Queue of {bagIndex, slotIndex} pairs to sell - NEW
    
    // Close vendor request (flag)
    bool m_hasCloseVendorRequest = false;    // Flag indicating a close request is pending
    
    // Lua test request (store latest script)
    std::string m_luaTestScriptRequest; 
    bool m_hasLuaTestRequest = false;   // Flag indicating a lua test request is pending
    // -------------------------------------

    // --- Rotation State (New) ---
    std::vector<RotationStep> m_currentRotation;
    std::string m_currentRotationName;
    std::string m_rotationsDirectory; // Store path
    // --------------------------

    // --- UI State Cache ---
    std::atomic<bool> m_isVendorWindowVisible{false};

    // --- Looting Setting --- 
    std::atomic<bool> m_isLootingEnabled{true}; // Default to true

    // Function pointers (To be initialized)
    // tSellItemByGuid SellItemByGuid = nullptr; // <<< REMOVE THIS FUNCTION POINTER MEMBER

    // Private helper methods
    void runGrindingEngine(); // Example task for the thread
    void initializeRotationsDirectory(); // New helper
}; 