#pragma once

#include <vector>
#include <string>
#include "../game/wowobject.h" // Include wowobject.h for Vector3

class PathManager {
public:
    PathManager();
    ~PathManager();

    // Load a path from a file
    bool loadPath(const std::string& filename);

    // Save the current path to a file
    bool savePath(const std::string& filename) const;

    // Set the path data directly (e.g., from PathRecorder)
    void setPath(const std::vector<Vector3>& path);

    // Get the current path data
    const std::vector<Vector3>& getPath() const;

    // Clear the current path
    void clearPath();

    // Check if a path is loaded/exists
    bool hasPath() const;

    // List available path files in the standard path directory
    std::vector<std::string> ListAvailablePaths() const;

    // Get the name of the currently loaded path (if loaded from file)
    std::string GetCurrentPathName() const;

private:
    std::vector<Vector3> m_currentPath;
    std::string m_currentPathName; // Store the name when loaded
    // Potentially add filename tracking, dirty flags, etc.
}; 