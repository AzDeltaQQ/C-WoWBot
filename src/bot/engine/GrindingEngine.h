#pragma once

#include <atomic>
#include <memory> // For std::thread if used
#include <thread>

// Forward declarations
class ObjectManager;
class SpellManager;
class PathManager;
class MovementController; // Uncommented and needed
class BotController; // Added forward declaration
// class MovementController; // If you have a dedicated movement system

class GrindingEngine {
public:
    // Constructor now takes BotController and ObjectManager
    GrindingEngine(BotController* botController, ObjectManager* objectManager);
    ~GrindingEngine();

    // Start the grinding logic (potentially in a new thread)
    void start();

    // Request the grinding logic to stop
    void stop();

    // Check if the engine is currently running
    bool isRunning() const;

private:
    // The main loop executed by the engine's thread
    void runLoop();

    // --- Helper methods for grinding logic --- 
    // bool findTarget(TargetInfo& outTarget);
    // bool navigateTo(const Point3D& destination);
    // bool combatRotation(uint64_t targetGuid);
    // bool lootTarget(uint64_t targetGuid);
    // bool followPath();
    // ------------------------------------------

    // Non-owning pointer to the core BotController
    BotController* m_botController = nullptr;
    // Non-owning pointer to the ObjectManager (needed for player position)
    ObjectManager* m_objectManager = nullptr;
    // Removed individual manager pointers:
    // ObjectManager* m_objectManager = nullptr;
    // SpellManager* m_spellManager = nullptr;
    // PathManager* m_pathManager = nullptr;
    // MovementController* m_movementController = nullptr;

    // State
    std::atomic<bool> m_isRunning = {false};
    std::atomic<bool> m_stopRequested = {false};

    // Thread for the engine's main loop
    std::thread m_engineThread;
}; 