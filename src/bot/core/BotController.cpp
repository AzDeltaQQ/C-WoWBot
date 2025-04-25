#include "BotController.h"
#include "../pathing/PathManager.h"
#include "../pathing/PathRecorder.h"
#include "../engine/GrindingEngine.h"
#include "../../utils/log.h" // Assuming log utility exists

// Include headers for systems passed in initialize, e.g.:
// #include "../../game/ObjectManager.h"
// #include "../../game/SpellManager.h"

#include <thread> // Include if using threads
#include <vector> // Include vector

BotController::BotController() {
    // Initialize owned components
    m_pathManager = std::make_unique<PathManager>();
    // PathRecorder might need dependencies passed here or in initialize
    // m_pathRecorder = std::make_unique<PathRecorder>(*m_pathManager, ???);
    LogMessage("BotController: Instance created.");
}

BotController::~BotController() {
    // Ensure engine thread is stopped if running
    stop(); 
    LogMessage("BotController: Instance destroyed.");
}

void BotController::initialize(
    ObjectManager* objManager,
    SpellManager* spellManager) {
    m_objectManager = objManager;
    m_spellManager = spellManager;
    
    // Now that we have ObjectManager, initialize PathRecorder
    if (m_pathManager && m_objectManager) { // Check dependencies
         m_pathRecorder = std::make_unique<PathRecorder>(*m_pathManager, m_objectManager);
         LogMessage("BotController: PathRecorder initialized.");
    } else {
         LogMessage("BotController Error: Failed to initialize PathRecorder (missing dependencies).");
    }

    // Set default engine type to GRINDING
    if (m_currentEngineType == EngineType::NONE) {
        LogMessage("BotController: Setting default engine type to GRINDING.");
        setEngine(EngineType::GRINDING);
    }

    LogMessage("BotController: Initialized with core systems.");
}

void BotController::start() {
    if (m_currentState == State::RUNNING) {
        LogMessage("BotController: Start requested but already running.");
        return;
    }
    if (m_currentEngineType == EngineType::NONE) {
        LogMessage("BotController: Cannot start, no engine selected.");
        return;
    }
    if (m_currentState == State::PATH_RECORDING) {
        LogMessage("BotController: Cannot start engine while recording path.");
        return;
    }

    LogMessage("BotController: Starting bot...");
    m_stopRequested = false;
    m_currentState = State::RUNNING;

    // Create and start the appropriate engine
    // Example for GrindingEngine:
    if (m_currentEngineType == EngineType::GRINDING) {
        if (!m_currentEngine) { // Create if it doesn't exist
             m_currentEngine = std::make_unique<GrindingEngine>(this, m_objectManager);
        }
        if (m_currentEngine) {
             LogMessage("BotController: Starting GrindingEngine...");
             m_currentEngine->start(); // Assuming engine runs in its own thread or manages its loop
        } else {
             LogMessage("BotController Error: Failed to create GrindingEngine.");
             m_currentState = State::IDLE;
        }
    }
    // Add else if for other engine types later
    else {
         LogMessage("BotController Error: Selected engine type not implemented for starting.");
         m_currentState = State::IDLE;
    }
}

void BotController::stop() {
    if (m_currentState != State::RUNNING) {
        // LogMessage("BotController: Stop requested but not running."); // Maybe too noisy
        return; 
    }

    LogMessage("BotController: Stopping bot...");
    m_stopRequested = true;
    
    if (m_currentEngine) {
        m_currentEngine->stop(); // Signal engine thread to stop
        // Consider joining the thread here if the engine uses one
        // m_currentEngine.reset(); // Optional: destroy engine instance on stop
         LogMessage("BotController: Stop signal sent to engine.");
    }
    
    m_currentState = State::IDLE;
     LogMessage("BotController: Bot stopped.");
}

void BotController::setEngine(EngineType type) {
    if (m_currentState == State::RUNNING) {
        LogMessage("BotController: Cannot change engine while running. Stop the bot first.");
        return;
    }
    if (m_currentEngineType == type) return; // No change

    LogMessage("BotController: Setting engine type...");
    m_currentEngine.reset(); // Destroy previous engine instance
    m_currentEngineType = type;

    // Pre-create engine instance (optional, could also be done in start())
    // if (m_currentEngineType == EngineType::GRINDING) {
    //     m_currentEngine = std::make_unique<GrindingEngine>(m_objectManager, m_spellManager, m_pathManager.get());
    // }
    LogMessage("BotController: Engine type set.");
}

BotController::EngineType BotController::getCurrentEngineType() const {
    return m_currentEngineType;
}

void BotController::startPathRecording(int intervalMs) {
     if (m_currentState != State::IDLE) {
         LogMessage("BotController: Cannot start path recording unless IDLE.");
         return;
     }
     if (m_pathRecorder) {
         LogMessage("BotController: Starting path recording...");
         if (m_pathRecorder->startRecording(intervalMs)) {
             m_currentState = State::PATH_RECORDING;
         } else {
             LogMessage("BotController: Failed to start path recording (already recording?).");
         }
     } else {
         LogMessage("BotController Error: PathRecorder not initialized.");
     }
}

void BotController::stopPathRecording() {
    if (m_currentState != State::PATH_RECORDING) {
        // LogMessage("BotController: Stop path recording requested but not recording."); // Maybe too noisy
        return;
    }
     if (m_pathRecorder) {
          LogMessage("BotController: Stopping path recording...");
         m_pathRecorder->stopRecording();
         // PathRecorder should push final path to PathManager
         m_currentState = State::IDLE;
          LogMessage("BotController: Path recording stopped.");
     } else {
         LogMessage("BotController Error: PathRecorder not initialized.");
         m_currentState = State::IDLE; // Recover state
     }
}

void BotController::clearCurrentPath() {
     if (m_currentState == State::PATH_RECORDING) {
         LogMessage("BotController: Cannot clear path while recording.");
         return;
     }
    if (m_pathManager) {
         LogMessage("BotController: Clearing current path.");
        m_pathManager->clearPath();
    } else {
        LogMessage("BotController Error: PathManager not initialized.");
    }
}

bool BotController::saveCurrentPath(const std::string& filename) {
    if (m_currentState.load() == State::PATH_RECORDING) {
         LogMessage("BotController: Cannot save path while recording.");
         return false;
     }
     if (m_pathManager) {
        // Format log message manually
        char logBuffer[256];
        snprintf(logBuffer, sizeof(logBuffer), "BotController: Requesting PathManager to save path as '%s'.", filename.c_str());
        LogMessage(logBuffer);
        return m_pathManager->savePath(filename);
     } else {
        LogMessage("BotController Error: PathManager not initialized, cannot save path.");
        return false;
     }
}

bool BotController::loadPathByName(const std::string& pathName) {
    if (m_currentState != State::IDLE) {
         LogMessage("BotController: Cannot load path unless IDLE.");
         return false;
     }
    if (m_pathManager) {
        // Format log message
        char logBuffer[256];
        snprintf(logBuffer, sizeof(logBuffer), "BotController: Requesting PathManager to load path '%s'.", pathName.c_str());
        LogMessage(logBuffer);
        
        bool result = m_pathManager->loadPath(pathName);
        if (!result) {
             // Format log message
             snprintf(logBuffer, sizeof(logBuffer), "BotController: PathManager failed to load path '%s'.", pathName.c_str());
             LogMessage(logBuffer);
        }
        return result;
     } else {
        LogMessage("BotController Error: PathManager not initialized, cannot load path.");
        return false;
     }
}

std::vector<std::string> BotController::getAvailablePathNames() const {
    if (m_pathManager) {
        return m_pathManager->ListAvailablePaths();
    } else {
        LogMessage("BotController Error: PathManager not initialized, cannot list paths.");
        return {}; // Return empty vector
    }
}

std::string BotController::getCurrentPathName() const {
    if (m_pathManager) {
        return m_pathManager->GetCurrentPathName();
    } else {
        LogMessage("BotController Error: PathManager not initialized, cannot get current path name.");
        return ""; // Return empty string
    }
}

BotController::State BotController::getCurrentState() const {
    // If engine exists and says it's stopped running (e.g., completed task or error), update state
    // This prevents the GUI showing RUNNING if the engine thread terminated itself.
    if (m_currentState == State::RUNNING && m_currentEngine && !m_currentEngine->isRunning()) {
       // Potentially log this state change
       // LogMessage("BotController: Engine stopped unexpectedly. Updating state to IDLE.");
       const_cast<BotController*>(this)->m_currentState = State::IDLE;
    }
     // Same for recorder?
    if (m_currentState == State::PATH_RECORDING && m_pathRecorder && !m_pathRecorder->isRecording()) {
        const_cast<BotController*>(this)->m_currentState = State::IDLE;
    }
    
    return m_currentState;
}

bool BotController::isRunning() const {
    return getCurrentState() == State::RUNNING;
}

// --- Accessors --- 
const PathManager* BotController::getPathManager() const {
    return m_pathManager.get();
}

const PathRecorder* BotController::getPathRecorder() const {
    return m_pathRecorder.get();
}

// Private helper method implementation (if needed)
void BotController::runGrindingEngine() {
    // This function might be the entry point for m_engineThread
    // if m_currentEngine->start() doesn't create its own thread.
} 