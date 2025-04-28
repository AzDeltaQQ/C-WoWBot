#pragma once

#include <memory> // For std::unique_ptr
#include <atomic> // For std::atomic_bool
#include <string> // <-- Add this include
#include <vector> // Add for vector return type
#include <cstdint> // For uint64_t
#include <filesystem> // Add for directory iteration
#include <mutex>      // <-- Add this include
#include "RotationStep.h" // Include the new struct definition

// Forward declarations to reduce header dependencies
class ObjectManager;
class SpellManager;
class GrindingEngine;
class PathManager;
class PathRecorder;

class BotController {
public:
    enum class State {
        IDLE,
        RUNNING,
        PATH_RECORDING
    };

    enum class EngineType {
        NONE,
        GRINDING
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
    void startPathRecording(int intervalMs);
    void stopPathRecording();
    void clearCurrentPath();
    
    // Save/Load (delegates to PathManager)
    bool saveCurrentPath(const std::string& filename);
    bool loadPathByName(const std::string& pathName); // New: Load path
    std::vector<std::string> getAvailablePathNames() const; // New: List paths
    std::string getCurrentPathName() const; // New: Get loaded path name
    
    // --- Rotation Methods (New) ---
    std::vector<std::string> getAvailableRotationNames();
    bool loadRotationByName(const std::string& name);
    const std::string& getCurrentRotationName() const;
    const std::vector<RotationStep>& getCurrentRotation() const;
    // -----------------------------

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
    // void requestCastSpell(uint32_t spellId); // Modify declaration
    void requestCastSpell(uint32_t spellId, uint64_t targetGuid); // Add targetGuid parameter
    void requestInteract(uint64_t targetGuid);

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
    // Remove old atomic request variables:
    // std::atomic<uint64_t> m_requestedTargetGuid{0}; // For target requests
    // std::atomic<uint32_t> m_requestedSpellId{0};    // For spell cast requests

    // --- New Request Queueing Members --- 
    std::mutex m_requestMutex;           // Mutex to protect request variables
    uint64_t m_targetRequest = 0;        // Stores requested target GUID
    bool m_hasTargetRequest = false;     // Flag indicating a target request
    uint32_t m_castRequest_SpellId = 0;      // Stores requested spell ID (rename)
    uint64_t m_castRequest_TargetGuid = 0;   // Stores requested spell target GUID (add)
    bool m_hasCastRequest = false;           // Flag indicating a cast request
    uint64_t m_interactRequest = 0;          // Stores requested interact GUID
    bool m_hasInteractRequest = false;       // Flag indicating an interact request
    // -------------------------------------

    // --- Rotation State (New) ---
    std::vector<RotationStep> m_currentRotation;
    std::string m_currentRotationName;
    std::string m_rotationsDirectory; // Store path
    // --------------------------

    // --- Looting Setting --- 
    std::atomic<bool> m_isLootingEnabled{true}; // Default to true

    // Private helper methods
    void runGrindingEngine(); // Example task for the thread
    void initializeRotationsDirectory(); // New helper
}; 