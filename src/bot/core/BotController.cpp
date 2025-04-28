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
#include <filesystem> // Added for rotation methods
#include <fstream>    // Added for rotation methods
#include <sstream>    // Added for rotation methods
#include <windows.h>  // Added for GetModuleFileNameA
#include <algorithm> // Added for std::transform
#include <cctype>    // Added for ::tolower
#include <mutex>    // Added for mutex

// Helper function (if not already available globally)
// Consider moving to a shared utility header if used elsewhere
std::string GetDllDirectory_BC_Internal() { // Renamed slightly 
    char path[MAX_PATH];
    // IMPORTANT: Ensure "WoWDX9Hook.dll" matches your actual output DLL name
    HMODULE hModule = GetModuleHandleA("WoWDX9Hook.dll"); 
    if (hModule != NULL && GetModuleFileNameA(hModule, path, MAX_PATH) > 0) {
        std::string fullPath(path);
        size_t lastSlash = fullPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            return fullPath.substr(0, lastSlash);
        }
    }
    return "."; // Fallback
}

BotController::BotController() {
    // Initialize owned components
    m_pathManager = std::make_unique<PathManager>();
    // PathRecorder initialization moved to initialize() as it needs ObjectManager
    
    initializeRotationsDirectory(); // Initialize rotation directory path

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
        return; 
    }

    LogMessage("BotController: Stopping bot...");
    m_stopRequested = true;
    
    if (m_currentEngine) {
        m_currentEngine->stop(); // Signal engine thread to stop
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
    if (m_currentState == State::RUNNING && m_currentEngine && !m_currentEngine->isRunning()) {
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

// --- New Helper and Rotation Method Implementations ---

void BotController::initializeRotationsDirectory() {
    if (m_rotationsDirectory.empty()) {
        std::string dllDir = GetDllDirectory_BC_Internal();
        std::filesystem::path dirPath = std::filesystem::path(dllDir) / "Rotations";
        m_rotationsDirectory = dirPath.string();
        // Ensure the directory exists (create if not)
        try {
            if (!std::filesystem::exists(dirPath)) {
                if (std::filesystem::create_directories(dirPath)) {
                     LogMessage(("BotController: Created rotations directory: " + m_rotationsDirectory).c_str());
                } else {
                     LogMessage(("BotController Warning: Failed to create rotations directory: " + m_rotationsDirectory).c_str());
                }
            }
        } catch (const std::filesystem::filesystem_error& e) {
            LogStream ssErr;
            ssErr << "BotController: Filesystem error checking/creating rotations directory: " << e.what();
            LogMessage(ssErr.str());
        }
        LogMessage(("BotController: Rotations directory set to: " + m_rotationsDirectory).c_str());
    }
}

std::vector<std::string> BotController::getAvailableRotationNames() {
    // No need to call initializeRotationsDirectory() here, constructor handles it.
    std::vector<std::string> names;
    try {
        if (!m_rotationsDirectory.empty() && std::filesystem::exists(m_rotationsDirectory) && std::filesystem::is_directory(m_rotationsDirectory)) {
            for (const auto& entry : std::filesystem::directory_iterator(m_rotationsDirectory)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    names.push_back(entry.path().stem().string()); // Get filename without extension
                }
            }
        } else {
             LogMessage(("BotController Warning: Rotations directory not found or invalid: " + m_rotationsDirectory).c_str());
        }
    } catch (const std::filesystem::filesystem_error& e) {
        LogStream ssErr;
        ssErr << "BotController: Filesystem error reading rotations directory: " << e.what();
        LogMessage(ssErr.str());
    } catch (const std::exception& e) {
         LogMessage(("BotController: Error reading rotations directory: " + std::string(e.what())).c_str());
    }
    return names;
}

bool BotController::loadRotationByName(const std::string& name) {
    // No need to call initializeRotationsDirectory() here.
    if (m_rotationsDirectory.empty()) {
        LogMessage("BotController Error: Rotations directory path is not set.");
        return false;
    }
    std::filesystem::path filePath = std::filesystem::path(m_rotationsDirectory) / (name + ".json");

    LogMessage(("BotController: Attempting to load rotation: " + filePath.string()).c_str());

    std::ifstream inFile(filePath);
    if (!inFile.is_open()) {
        LogMessage(("BotController Error: Failed to open rotation file: " + filePath.string()).c_str());
        return false;
    }

    // --- Manual JSON Parsing (Fragile - Recommend a Library) ---
    std::vector<RotationStep> loadedRotation;
    std::string line;
    std::stringstream currentObject;
    int braceLevel = 0;
    bool inString = false;
    bool escapeNext = false;

    try {
        while (std::getline(inFile, line)) {
            for (char c : line) {
                if (escapeNext) {
                    if (braceLevel > 1) currentObject << c;
                    escapeNext = false;
                    continue;
                }
                if (c == '\\') {
                    if (inString && braceLevel > 1) currentObject << c;
                    escapeNext = true;
                    continue;
                }
                if (c == '"') {
                    inString = !inString;
                    // Always append quotes if within the main object structure (braceLevel > 0)
                    if (braceLevel > 0) currentObject << c; 
                } else if (!inString) {
                    if (c == '{') {
                        if (braceLevel == 1) currentObject.str(std::string()); // Clear stream for new object
                        braceLevel++;
                        currentObject << c; // ALWAYS append '{'
                    } else if (c == '}') {
                        currentObject << c; // ALWAYS append '}'
                        braceLevel--;
                        // Check if we just finished an object INSIDE the main array (level is back to 0)
                        if (braceLevel == 0 && !currentObject.str().empty()) { // Finished an object, ensure stream wasn't just cleared
                            std::string objStr = currentObject.str();
                            
                            // Initialize with defaults
                            RotationStep currentStep;
                            currentStep.spellId = 0;
                            currentStep.triggersGCD = true;
                            currentStep.requiresTarget = true;
                            currentStep.castRange = 30.0f;
                            currentStep.minPlayerHealthPercent = 0.0f;
                            currentStep.maxPlayerHealthPercent = 100.0f;
                            currentStep.minTargetHealthPercent = 0.0f;
                            currentStep.maxTargetHealthPercent = 100.0f;
                            currentStep.minPlayerManaPercent = 0.0f;
                            currentStep.maxPlayerManaPercent = 100.0f;
                            
                            // --- Parse the object string (Updated) --- 
                            size_t currentPos = 0;
                            while((currentPos = objStr.find('"', currentPos)) != std::string::npos) {
                                size_t keyEndPos = objStr.find('"', currentPos + 1);
                                if (keyEndPos == std::string::npos) break; 
                                std::string key = objStr.substr(currentPos + 1, keyEndPos - currentPos - 1);
                                
                                size_t colonPos = objStr.find(':', keyEndPos);
                                if (colonPos == std::string::npos) break;
                                
                                size_t valueStartPos = objStr.find_first_not_of(" \t\n\r", colonPos + 1);
                                if (valueStartPos == std::string::npos) break;
                                
                                size_t valueEndPos = std::string::npos;
                                std::string valueStr;
                                if (objStr[valueStartPos] == '"') { // String value
                                    valueEndPos = objStr.find('"', valueStartPos + 1);
                                     while (valueEndPos != std::string::npos && objStr[valueEndPos-1] == '\\') {
                                         valueEndPos = objStr.find('"', valueEndPos + 1);
                                     }
                                     if (valueEndPos != std::string::npos) {
                                          valueStr = objStr.substr(valueStartPos + 1, valueEndPos - valueStartPos - 1);
                                          size_t found = valueStr.find("\\\"");
                                          while (found != std::string::npos) {
                                              valueStr.replace(found, 2, "\"");
                                              found = valueStr.find("\\\"", found + 1);
                                          }
                                     } else { valueEndPos = objStr.length(); } // Malformed?
                                } else { // Numeric or boolean value
                                    valueEndPos = objStr.find_first_of(",}" , valueStartPos);
                                    if (valueEndPos == std::string::npos) valueEndPos = objStr.length();
                                    valueStr = objStr.substr(valueStartPos, valueEndPos - valueStartPos);
                                    valueStr.erase(valueStr.find_last_not_of(" \t\n\r") + 1);
                                }
                                
                                // --- Assign based on key (Updated) --- 
                                try {
                                    if (key == "spellId") {
                                        currentStep.spellId = std::stoul(valueStr);
                                    } else if (key == "spellName") {
                                        currentStep.spellName = valueStr;
                                    } else if (key == "triggersGCD") {
                                        currentStep.triggersGCD = (valueStr == "true");
                                    } else if (key == "requiresTarget") {
                                        currentStep.requiresTarget = (valueStr == "true");
                                    } else if (key == "castRange") {
                                        currentStep.castRange = std::stof(valueStr);
                                    } else if (key == "minPlayerHealthPercent") {
                                        currentStep.minPlayerHealthPercent = std::stof(valueStr);
                                    } else if (key == "maxPlayerHealthPercent") {
                                        currentStep.maxPlayerHealthPercent = std::stof(valueStr);
                                    } else if (key == "minTargetHealthPercent") {
                                        currentStep.minTargetHealthPercent = std::stof(valueStr);
                                    } else if (key == "maxTargetHealthPercent") {
                                        currentStep.maxTargetHealthPercent = std::stof(valueStr);
                                    } else if (key == "minPlayerManaPercent") {
                                        currentStep.minPlayerManaPercent = std::stof(valueStr);
                                    } else if (key == "maxPlayerManaPercent") {
                                        currentStep.maxPlayerManaPercent = std::stof(valueStr);
                                    }
                                } catch (const std::invalid_argument& /*ia*/) {
                                     LogMessage(("BotController Warning: Invalid value '" + valueStr + "' for key '" + key + "' in rotation '" + name + "'. Skipping key.").c_str());
                                } catch (const std::out_of_range& /*oor*/) {
                                     LogMessage(("BotController Warning: Value '" + valueStr + "' out of range for key '" + key + "' in rotation '" + name + "'. Skipping key.").c_str());
                                }
                                // -----------------------------------
                                
                                currentPos = valueEndPos;
                                if (currentPos == std::string::npos) break;
                            }
                            // Add the parsed step
                            loadedRotation.push_back(currentStep);
                        } // End object parsing
                    } else if (braceLevel > 0 && !std::isspace(c)) { // Append other non-brace, non-whitespace characters ONLY if we are inside braces (level > 0)
                        currentObject << c;
                    }
                } else { // Inside string
                     // Append character if inside braces (level > 0)
                     if (braceLevel > 0) currentObject << c; 
                }
            } // End char loop
        } // End line loop
         // --- End Manual Parsing --- 

        m_currentRotation = std::move(loadedRotation); // Use move assignment
        m_currentRotationName = name;
        LogMessage(("BotController: Successfully loaded rotation '" + name + "' with " + std::to_string(m_currentRotation.size()) + " steps.").c_str());
        inFile.close();
        return true;

    } catch (const std::exception& e) { // Catch outer errors 
        LogMessage(("BotController Error: Exception parsing rotation file '" + name + "': " + std::string(e.what())).c_str());
        m_currentRotation.clear(); m_currentRotationName = ""; inFile.close(); return false;
    }
}

const std::string& BotController::getCurrentRotationName() const {
    return m_currentRotationName;
}

const std::vector<RotationStep>& BotController::getCurrentRotation() const {
    return m_currentRotation;
}

// --- Request Handling --- 
// Called by engines to queue actions for the main thread

// Request setting target GUID
void BotController::requestTarget(uint64_t guid) {
    // Simple queueing - store the requested GUID
    // The main update loop will process this
    std::lock_guard<std::mutex> lock(m_requestMutex); 
    m_targetRequest = guid;
    m_hasTargetRequest = true;
}

// Request casting a spell
void BotController::requestCastSpell(uint32_t spellId) {
    // Queue the spell cast request
    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_castRequest = spellId;
    m_hasCastRequest = true;
}

// Request interaction with a GUID
void BotController::requestInteract(uint64_t guid) {
    std::lock_guard<std::mutex> lock(m_requestMutex);
    m_interactRequest = guid;
    m_hasInteractRequest = true;
}

// --- Main Thread Processing --- 
// Called periodically from the main game thread (e.g., EndScene hook)
void BotController::processRequests() {
    std::lock_guard<std::mutex> lock(m_requestMutex); // Lock for accessing request flags/data

    if (m_hasTargetRequest) {
         LogStream ss; ss << "BotController: Processing target request for GUID 0x" << std::hex << m_targetRequest;
         LogMessage(ss.str());
        // Execute the actual targeting function (must be safe for main thread)
        TargetUnitByGuid(m_targetRequest);
        m_hasTargetRequest = false; // Reset flag
        m_targetRequest = 0;
    }

    if (m_hasCastRequest) {
         LogStream ss; ss << "BotController: Processing cast request for SpellID " << m_castRequest;
         LogMessage(ss.str());
        // Assuming SpellManager::CastSpell is safe to call from main thread
        // or it handles its own threading/queuing internally.
        if (m_spellManager) { 
             m_spellManager->CastSpell(m_castRequest); // Call the actual cast function
        }
        m_hasCastRequest = false;
        m_castRequest = 0;
    }

    if (m_hasInteractRequest) {
        LogStream ss; ss << "BotController: Processing interact request for GUID 0x" << std::hex << m_interactRequest;
        LogMessage(ss.str());
        // Find the WowObject and call its Interact method
        if (m_objectManager) {
             std::shared_ptr<WowObject> obj = m_objectManager->GetObjectByGUID(m_interactRequest);
             if (obj) {
                 obj->Interact(); // Call Interact from main thread
                 LogStream ssDone; ssDone << "BotController: Executed Interact() on GUID 0x" << std::hex << m_interactRequest;
                 LogMessage(ssDone.str());
             } else {
                 LogStream ssErr; ssErr << "BotController Error: Could not find object with GUID 0x" << std::hex << m_interactRequest << " to interact.";
                 LogMessage(ssErr.str());
             }
        } else {
             LogMessage("BotController Error: ObjectManager is null, cannot process interact request.");
        }
        m_hasInteractRequest = false;
        m_interactRequest = 0;
    }
}

// --- Looting Setting Implementation ---
void BotController::setLootingEnabled(bool enabled) {
    m_isLootingEnabled.store(enabled);
    LogStream ss; ss << "BotController: Looting Enabled set to " << (enabled ? "true" : "false");
    LogMessage(ss.str());
}

bool BotController::isLootingEnabled() const {
    return m_isLootingEnabled.load();
}
// --- End Looting Setting ---