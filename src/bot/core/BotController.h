#pragma once

#include <memory> // For std::unique_ptr
#include <atomic> // For std::atomic_bool
#include <string> // <-- Add this include
#include <vector> // Add for vector return type
#include <cstdint> // For uint64_t
#include "RotationStep.h" // Include the new struct definition

// Forward declarations to reduce header dependencies
class ObjectManager;
class SpellManager;
// class MovementController; // Example
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
        /* MovementController* moveController */ // Pass other necessary managers
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

    // --- Target Request --- 
    void requestTarget(uint64_t guid); // Request main thread to target
    uint64_t getAndClearRequestedTarget(); // Main thread gets request
    
    // --- Main Loop (Add declaration) ---
    void run(); 

    // Accessors for components (optional, provide as needed)
    const PathManager* getPathManager() const; 
    const PathRecorder* getPathRecorder() const;

private:
    // Core systems (non-owning pointers)
    ObjectManager* m_objectManager = nullptr;
    SpellManager* m_spellManager = nullptr;
    // MovementController* m_movementController = nullptr;

    // Bot components (owned or managed)
    std::unique_ptr<PathManager> m_pathManager;
    std::unique_ptr<PathRecorder> m_pathRecorder;
    std::unique_ptr<GrindingEngine> m_currentEngine; // Example for grinding

    // Bot state
    std::atomic<State> m_currentState = {State::IDLE};
    EngineType m_currentEngineType = EngineType::NONE;
    std::atomic<bool> m_stopRequested = {false};
    std::atomic<uint64_t> m_requestedTargetGuid{0}; // For target requests

    // --- Rotation State (New) ---
    std::vector<RotationStep> m_currentRotation;
    std::string m_currentRotationName;
    std::string m_rotationsDirectory; // Store path
    // --------------------------

    // Thread for the engine's main loop (if applicable)
    // std::thread m_engineThread;

    // Private helper methods
    void runGrindingEngine(); // Example task for the thread
    void initializeRotationsDirectory(); // New helper
}; 