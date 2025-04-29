#pragma once

#include <vector>
#include <string>
#include "../../game/wowobject.h" // Corrected include path for Vector3

class PathManager {
public:
    // Define path types
    enum class PathType {
        GRIND,
        VENDOR
    };

    PathManager();
    ~PathManager();

    // Load a path from a file (now takes type)
    bool loadPath(const std::string& filename, PathType type);

    // Save the current path to a file (now takes type)
    bool savePath(const std::string& filename, PathType type) const;

    // Set path data (distinguish between types)
    void setPath(const std::vector<Vector3>& path, PathType type);

    // Get path data (distinguish between types)
    const std::vector<Vector3>& getPath(PathType type) const;

    // Clear path data (distinguish between types)
    void clearPath(PathType type);

    // Check if a path is loaded/exists (distinguish between types)
    bool hasPath(PathType type) const;

    // List available path files (now takes type)
    std::vector<std::string> ListAvailablePaths(PathType type) const;

    // Get the name of the currently loaded path (distinguish between types)
    std::string getCurrentPathName(PathType type) const;

    // Get the name of the currently loaded vendor path
    std::string getCurrentVendorName() const;

    // Set the name for the current vendor path
    void setCurrentVendorName(const std::string& name);

    // --- Vendor-specific aliases (optional convenience) ---
    // const std::vector<Vector3>& getVendorPath() const { return getPath(PathType::VENDOR); }
    // bool hasVendorPath() const { return hasPath(PathType::VENDOR); }
    // std::string getCurrentVendorPathName() const { return getCurrentPathName(PathType::VENDOR); }
    // void clearVendorPath() { clearPath(PathType::VENDOR); }
    // bool loadVendorPath(const std::string& filename) { return loadPath(filename, PathType::VENDOR); }
    // bool saveVendorPath(const std::string& filename) const { return savePath(filename, PathType::VENDOR); }
    // std::vector<std::string> ListAvailableVendorPaths() const { return ListAvailablePaths(PathType::VENDOR); }
    // void setVendorPath(const std::vector<Vector3>& path) { setPath(path, PathType::VENDOR); }

private:
    std::string getPathDirectory() const; // Helper for path directory
    std::string getFileExtension(PathType type) const; // Helper for file extension

    std::vector<Vector3> m_grindPath; // Renamed from m_currentPath
    std::string m_currentGrindPathName; // Renamed from m_currentPathName

    // New members for vendor path
    std::vector<Vector3> m_vendorPath;
    std::string m_currentVendorPathName;
    std::string m_currentVendorName;
}; 