#include "PathRecorder.h"
#include "../../utils/log.h" // Assuming log utility exists
#include "../../game/objectmanager.h" // Need definition for ObjectManager

#include <thread> // Include for std::thread
#include <chrono> // For sleep

// Define offsets here or include a common header where they are defined
// REVERTED to standard X, Y, Z order
constexpr DWORD OBJECT_POS_X_OFFSET = 0x79C; // Standard X offset
constexpr DWORD OBJECT_POS_Y_OFFSET = 0x798; // Standard Y offset
constexpr DWORD OBJECT_POS_Z_OFFSET = 0x7A0;

// Helper template from WowObject (or move to a common utils header)
// Ensure this helper is robust
namespace {
    template <typename T> 
    T ReadMemorySafe(uintptr_t address) {
        if (address == 0) return T{}; // Check null address
        try {
            // Basic check if address looks somewhat valid (optional)
            // if (IsBadReadPtr((const void*)address, sizeof(T))) { return T{}; }
            return *reinterpret_cast<T*>(address);
        } catch (const std::exception& /*e*/) {
             // Optionally log the exception
             // LogMessageV("ReadMemorySafe Exception: %s at 0x%p", e.what(), address);
             return T{}; // Return default value on memory access error
        } catch (...) {
             // LogMessageV("ReadMemorySafe Unknown Exception at 0x%p", address);
             return T{}; // Catch any other exceptions
        }
    }
}

PathRecorder::PathRecorder(PathManager& pathManager, ObjectManager* objectManager)
    : m_pathManager(pathManager), m_objectManager(objectManager) {
    LogMessage("PathRecorder: Instance created.");
}

PathRecorder::~PathRecorder() {
    stopRecording(); // Ensure thread is stopped and joined
    LogMessage("PathRecorder: Instance destroyed.");
}

bool PathRecorder::startRecording(int intervalMilliseconds) {
    if (m_isRecording) {
        LogMessage("PathRecorder: Start requested but already recording.");
        return false;
    }
     if (!m_objectManager) {
        LogMessage("PathRecorder Error: Cannot start recording, ObjectManager is null.");
        return false;
    }
    // Prevent starting if stop was already requested and thread hasn't finished
    if (m_recordingThread.joinable()) {
         LogMessage("PathRecorder: Cannot start, previous stop request still processing.");
         return false;
    }

    LogMessage("PathRecorder: Starting recording...");
    m_intervalMs = intervalMilliseconds;
    m_recordedPath.clear(); // Clear previous recording session data
    m_stopRequested = false;
    m_isRecording = true; // Set flag before starting thread

    // Start recordingLoop in a separate thread
    m_recordingThread = std::thread(&PathRecorder::recordingLoop, this);
    LogMessage("PathRecorder: Started recordingLoop thread.");
    return true;
}

void PathRecorder::stopRecording() {
    if (!m_isRecording && !m_recordingThread.joinable()) { // Only stop if running or thread exists
        return;
    }
    LogMessage("PathRecorder: Stopping recording...");
    m_stopRequested = true;
    m_isRecording = false; // Indicate stop is in progress

    // Join the thread if it exists and is joinable
    if (m_recordingThread.joinable()) {
         LogMessage("PathRecorder: Joining recording thread...");
         try {
            m_recordingThread.join();
            LogMessage("PathRecorder: Recording thread joined.");
         } catch (const std::system_error& e) {
            char errorMsg[256];
            snprintf(errorMsg, sizeof(errorMsg), "PathRecorder Error: Failed to join thread (%s)", e.what());
            LogMessage(errorMsg);
         }
    }

    // State is fully stopped only after joining
    m_isRecording = false; // Explicitly set false after join

    // Log size before setting
    char setPathMsg[100];
    snprintf(setPathMsg, sizeof(setPathMsg), "PathRecorder: Pushing %zu recorded points to PathManager.", m_recordedPath.size());
    LogMessage(setPathMsg);

    // Push the completed path to the PathManager (moved from loop end)
    if (!m_recordedPath.empty()) {
        m_pathManager.setPath(m_recordedPath);
        LogMessage("PathRecorder: Final path pushed to PathManager.");
    } else {
         LogMessage("PathRecorder: Recording stopped with empty path.");
    }
    LogMessage("PathRecorder: Stopped.");
}

bool PathRecorder::isRecording() const {
    return m_isRecording;
}

const std::vector<Vector3>& PathRecorder::getRecordedPath() const {
    // This might be less useful now that the final path is pushed on stop
    // Maybe return m_pathManager.getPath() instead, or keep this for live preview?
    return m_recordedPath; 
}

void PathRecorder::recordingLoop() {
    LogMessage("PathRecorder: recordingLoop started.");
    size_t pointsRecordedThisSession = 0;
    while (!m_stopRequested) {
        Vector3 currentPos = getCurrentPlayerPosition();
        
        bool shouldRecord = true;
        if (!m_recordedPath.empty()) {
             const Vector3& lastPos = m_recordedPath.back();
             float distSq = (currentPos.x - lastPos.x) * (currentPos.x - lastPos.x) +
                            (currentPos.y - lastPos.y) * (currentPos.y - lastPos.y) +
                            (currentPos.z - lastPos.z) * (currentPos.z - lastPos.z);
             if (distSq < 0.01f) { 
                 shouldRecord = false;
             }
        }

        if (shouldRecord) {
            m_recordedPath.push_back(currentPos);
            pointsRecordedThisSession++;
            // Log every 10 points added to avoid spamming logs too much
            if (pointsRecordedThisSession % 10 == 1 || pointsRecordedThisSession <= 1) {
                 char msg[150];
                 snprintf(msg, sizeof(msg), "PathRecorder: Added point #%zu (%.2f, %.2f, %.2f) to internal path (total %zu)", 
                          pointsRecordedThisSession, currentPos.x, currentPos.y, currentPos.z, m_recordedPath.size());
                 LogMessage(msg);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(m_intervalMs));
    }
    m_isRecording = false; 
     LogMessage("PathRecorder: recordingLoop finished.");
}

Vector3 PathRecorder::getCurrentPlayerPosition() {
    if (!m_objectManager) {
        LogMessage("PathRecorder Error: ObjectManager is null in getCurrentPlayerPosition.");
        return {0.0f, 0.0f, 0.0f};
    }
    
    std::shared_ptr<WowPlayer> player = m_objectManager->GetLocalPlayer(); 
    
    if (player) {
         void* playerPtr = player->GetPointer(); // Get the raw pointer to the player object in memory
        if (!playerPtr) {
             LogMessage("PathRecorder Error: Player object pointer is null.");
             return {0.0f, 0.0f, 0.0f};
        }
        
        // --- Direct Memory Read --- 
        // Read directly from memory, bypassing the cache timing issue.
        uintptr_t baseAddr = reinterpret_cast<uintptr_t>(playerPtr);
        Vector3 directPos;
        directPos.x = ReadMemorySafe<float>(baseAddr + OBJECT_POS_X_OFFSET);
        directPos.y = ReadMemorySafe<float>(baseAddr + OBJECT_POS_Y_OFFSET);
        directPos.z = ReadMemorySafe<float>(baseAddr + OBJECT_POS_Z_OFFSET);

        // Log the directly read position (can be commented out later)
        // char msg[100];
        // snprintf(msg, sizeof(msg), "PathRecorder: DIRECTLY READ player pos (%.2f, %.2f, %.2f)", directPos.x, directPos.y, directPos.z);
        // LogMessage(msg); 
        
        return directPos;
        // --------------------------

    } else {
        LogMessage("PathRecorder Error: Could not get player object from ObjectManager in getCurrentPlayerPosition.");
        return {0.0f, 0.0f, 0.0f};
    }
} 