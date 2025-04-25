#pragma once

#include <vector>
#include <atomic>
#include "PathManager.h" // Includes Vector3 definition
#include <thread>

// Forward declarations
class ObjectManager;
struct Vector3; // Forward declare Vector3 just in case PathManager.h changes

class PathRecorder {
public:
    // Constructor might take dependencies like ObjectManager and PathManager
    PathRecorder(PathManager& pathManager, ObjectManager* objectManager /* or Player* */);
    ~PathRecorder();

    // Start recording player position at specified intervals
    // Returns true if starting was successful, false otherwise (e.g., already recording)
    bool startRecording(int intervalMilliseconds);

    // Stop recording
    void stopRecording();

    // Check if currently recording
    bool isRecording() const;

    // Get the path recorded so far during the current session
    const std::vector<Vector3>& getRecordedPath() const;

private:
    void recordingLoop(); // The function running in the recording thread
    Vector3 getCurrentPlayerPosition(); // Changed return type to Vector3

    PathManager& m_pathManager; // Reference to store the final path
    ObjectManager* m_objectManager; // Pointer to get player position

    std::vector<Vector3> m_recordedPath; // Changed to Vector3
    std::atomic<bool> m_isRecording = {false};
    std::atomic<bool> m_stopRequested = {false};
    int m_intervalMs = 1000; // Default interval

    // Thread for the recording loop
    std::thread m_recordingThread;
}; 