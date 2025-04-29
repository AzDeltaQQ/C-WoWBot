#include "PathManager.h"
#include "../../utils/log.h" // Assuming log utility exists
#include <Windows.h>
#include <Shlwapi.h> // For PathRemoveFileSpecA, PathAppendA
#include <direct.h> // For _mkdir

// Include headers for file I/O
#include <fstream>
#include <sstream>
#include <filesystem> // Requires C++17
#include <algorithm> // For std::sort
#include <stdexcept> // For runtime_error in loadPath

// Link Shlwapi.lib for path functions
#pragma comment(lib, "Shlwapi.lib")

namespace {
    // Helper function to get the directory containing the current DLL
    std::string GetDllDirectoryInternal() { // Renamed slightly
        char dllPath[MAX_PATH] = {0};
        HMODULE hModule = NULL;

        // Get handle to *this* DLL module
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCSTR)&GetDllDirectoryInternal, // Use new name
                              &hModule)) {
            LogMessage("GetDllDirectoryInternal Error: GetModuleHandleExA failed.");
            return "."; // Return current directory as fallback
        }

        if (GetModuleFileNameA(hModule, dllPath, MAX_PATH) == 0) {
            LogMessage("GetDllDirectoryInternal Error: GetModuleFileNameA failed.");
            return ".";
        }

        // Remove the filename part to get the directory
        PathRemoveFileSpecA(dllPath);
        return std::string(dllPath);
    }
}

// --- Private Helpers ---

std::string PathManager::getPathDirectory() const {
    // This could be cached if needed
    std::string dllDir = GetDllDirectoryInternal();
    std::filesystem::path pathsDir = std::filesystem::path(dllDir) / "Paths";
    return pathsDir.string();
}

std::string PathManager::getFileExtension(PathType type) const {
    switch (type) {
        case PathType::GRIND: return ".path";
        case PathType::VENDOR: return ".vendorpath";
        default: return ".unknownpath";
    }
}

// --- Constructor / Destructor ---

PathManager::PathManager() {
    LogMessage("PathManager: Instance created.");
}

PathManager::~PathManager() {
    LogMessage("PathManager: Instance destroyed.");
}

// --- Core Path Methods (Modified for PathType) ---

std::string PathManager::getCurrentPathName(PathType type) const {
    switch(type) {
        case PathType::GRIND: return m_currentGrindPathName;
        case PathType::VENDOR: return m_currentVendorPathName;
        default: return "";
    }
}

std::string PathManager::getCurrentVendorName() const {
    return m_currentVendorName;
}

// --- ADDED Setter --- 
void PathManager::setCurrentVendorName(const std::string& name) {
    m_currentVendorName = name;
    // Optional: Log here if desired
}
// ---------------------

std::vector<std::string> PathManager::ListAvailablePaths(PathType type) const {
    std::vector<std::string> pathFiles;
    std::string targetExtension = getFileExtension(type);
    std::string pathsDirStr = getPathDirectory();
    std::filesystem::path pathsDir(pathsDirStr);

    char logBuffer[MAX_PATH + 200]; // Buffer for logging

    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Listing paths of type '%s' (Ext: '%s') in '%s'", 
              (type == PathType::GRIND ? "GRIND" : "VENDOR"), targetExtension.c_str(), pathsDirStr.c_str());
    LogMessage(logBuffer);

    if (!std::filesystem::exists(pathsDir)) {
        snprintf(logBuffer, sizeof(logBuffer), "PathManager: Paths directory '%s' does not exist.", pathsDirStr.c_str());
        LogMessage(logBuffer);
        return pathFiles;
    }
    if (!std::filesystem::is_directory(pathsDir)) {
        snprintf(logBuffer, sizeof(logBuffer), "PathManager: '%s' exists but is not a directory.", pathsDirStr.c_str());
        LogMessage(logBuffer);
        return pathFiles;
    }

    try {
        int count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(pathsDir)) {
            count++;
            if (entry.is_regular_file() && entry.path().extension() == targetExtension) {
                std::string stem = entry.path().stem().string();
                pathFiles.push_back(stem);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        snprintf(logBuffer, sizeof(logBuffer), "PathManager Error listing paths: %s", e.what());
        LogMessage(logBuffer);
    } catch (const std::exception& e) {
        snprintf(logBuffer, sizeof(logBuffer), "PathManager Error listing paths (std::exception): %s", e.what());
        LogMessage(logBuffer);
    } catch (...) {
        LogMessage("PathManager Error listing paths (Unknown exception)");
    }
    
    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Found %zu paths of type '%s'.", pathFiles.size(), (type == PathType::GRIND ? "GRIND" : "VENDOR"));
    LogMessage(logBuffer);

    std::sort(pathFiles.begin(), pathFiles.end());
    return pathFiles;
}

bool PathManager::loadPath(const std::string& filename, PathType type) {
    std::string fileExtension = getFileExtension(type);
    std::string pathsDirStr = getPathDirectory();
    std::filesystem::path fullPath = std::filesystem::path(pathsDirStr) / (filename + fileExtension);

    char logBuffer[MAX_PATH + 150]; 

    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Attempting to load %s path from '%s'...",
               (type == PathType::GRIND ? "GRIND" : "VENDOR"), fullPath.string().c_str());
    LogMessage(logBuffer);

    std::ifstream infile(fullPath);
    if (!infile.is_open()) {
        snprintf(logBuffer, sizeof(logBuffer), "PathManager Error: Could not open file '%s' for loading.", fullPath.string().c_str());
        LogMessage(logBuffer);
        return false;
    }

    std::vector<Vector3> loadedPath;
    std::string line;
    int lineNum = 0;
    std::string loadedVendorName = ""; // Temporary variable for vendor name

    // --- MODIFIED VENDOR PATH LOADING ---
    if (type == PathType::VENDOR) {
        if (std::getline(infile, line)) {
            // First line is vendor name
            loadedVendorName = line;
            lineNum++;
        } else {
            snprintf(logBuffer, sizeof(logBuffer), "PathManager Warning: Vendor path file '%s' is empty or couldn't read vendor name.", fullPath.string().c_str());
            LogMessage(logBuffer);
            // Decide if this is an error - for now, allow empty vendor name
        }
    }
    // --- END MODIFICATION ---

    while (std::getline(infile, line)) {
        lineNum++;
        std::stringstream ss(line);
        std::string segment;
        Vector3 point;
        
        try {
            if (!std::getline(ss, segment, ',')) throw std::runtime_error("Bad X");
            point.x = std::stof(segment);
            if (!std::getline(ss, segment, ',')) throw std::runtime_error("Bad Y");
            point.y = std::stof(segment);
            if (!std::getline(ss, segment)) throw std::runtime_error("Bad Z");
            point.z = std::stof(segment);
            loadedPath.push_back(point);
        } catch (const std::exception& e) {
             snprintf(logBuffer, sizeof(logBuffer), "PathManager Error: Failed to parse line %d ('%s') during load (%s). Skipping.", lineNum, line.c_str(), e.what());
             LogMessage(logBuffer);
             // Optional: consider returning false on parse error?
        }
    }
    infile.close();

    if (type == PathType::GRIND) {
        m_grindPath = loadedPath;
        m_currentGrindPathName = filename;
        snprintf(logBuffer, sizeof(logBuffer), "PathManager: Successfully loaded %zu GRIND points from '%s'. Path name set to '%s'.", m_grindPath.size(), fullPath.string().c_str(), m_currentGrindPathName.c_str());
    } else { // VENDOR
        m_vendorPath = loadedPath;
        m_currentVendorPathName = filename;
        m_currentVendorName = loadedVendorName; // <<< ADDED: Assign loaded name
        snprintf(logBuffer, sizeof(logBuffer), "PathManager: Successfully loaded %zu VENDOR points (Vendor: '%s') from '%s'. Path name set to '%s'.", 
                 m_vendorPath.size(), m_currentVendorName.c_str(), fullPath.string().c_str(), m_currentVendorPathName.c_str());
    }
    LogMessage(logBuffer);
    return true;
}

bool PathManager::savePath(const std::string& filename, PathType type) const {
    const std::vector<Vector3>* path_to_save = nullptr;
    const std::string* vendor_name_to_save = nullptr; // Pointer for vendor name

    if (type == PathType::GRIND) {
        path_to_save = &m_grindPath;
    } else { // VENDOR
        path_to_save = &m_vendorPath;
        vendor_name_to_save = &m_currentVendorName; // <<< ADDED: Get vendor name pointer
    }

    if (!path_to_save || path_to_save->empty()) {
        LogMessage("PathManager Warning: Attempted to save an empty or invalid path.");
        return false; 
    }

    std::string fileExtension = getFileExtension(type);
    std::string pathsDirStr = getPathDirectory();
    std::filesystem::path pathsDir(pathsDirStr);
    std::filesystem::path fullPath = pathsDir / (filename + fileExtension);
    char logBuffer[MAX_PATH + 100]; // Buffer for formatted logs

    try {
        if (!std::filesystem::exists(pathsDir)) {
            if (std::filesystem::create_directory(pathsDir)) {
                 snprintf(logBuffer, sizeof(logBuffer), "PathManager: Created directory '%s'.", pathsDir.string().c_str());
                 LogMessage(logBuffer);
            } else {
                 snprintf(logBuffer, sizeof(logBuffer), "PathManager Error: Failed to create directory '%s'. Cannot save path.", pathsDir.string().c_str());
                 LogMessage(logBuffer);
                return false;
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        snprintf(logBuffer, sizeof(logBuffer), "PathManager Error: Filesystem error checking/creating directory (%s). Cannot save path.", e.what());
        LogMessage(logBuffer);
        return false;
    }
    
    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Attempting to save %zu %s points to '%s'...", 
               path_to_save->size(), (type == PathType::GRIND ? "GRIND" : "VENDOR"), fullPath.string().c_str());
    LogMessage(logBuffer);
    std::ofstream outfile(fullPath);
    if (!outfile.is_open()) {
        snprintf(logBuffer, sizeof(logBuffer), "PathManager Error: Could not open file '%s' for saving.", fullPath.string().c_str());
        LogMessage(logBuffer);
        return false;
    }

    // --- MODIFIED VENDOR PATH SAVING ---
    if (type == PathType::VENDOR && vendor_name_to_save) {
        outfile << *vendor_name_to_save << "\n"; // Write vendor name first
    }
    // --- END MODIFICATION ---

    for (const auto& p : *path_to_save) {
        outfile << p.x << "," << p.y << "," << p.z << "\n";
    }

    outfile.close();

    if (outfile.fail()) { 
         snprintf(logBuffer, sizeof(logBuffer), "PathManager Error: Failed to write data correctly to '%s'.", fullPath.string().c_str());
         LogMessage(logBuffer);
         return false;
    }

    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Successfully saved %s path to '%s'.", (type == PathType::GRIND ? "GRIND" : "VENDOR"), fullPath.string().c_str());
    LogMessage(logBuffer);
    return true;
}

void PathManager::setPath(const std::vector<Vector3>& path, PathType type) {
    char msg[100];
    if (type == PathType::GRIND) {
        m_grindPath = path;
        // Don't clear name here, recorder doesn't know the name
        snprintf(msg, sizeof(msg), "PathManager: GRIND Path set with %zu points.", path.size());
    } else { // VENDOR
        m_vendorPath = path;
        m_currentVendorName.clear(); // <<< ADDED: Clear vendor name when path is set directly
        // Don't clear path name here
        snprintf(msg, sizeof(msg), "PathManager: VENDOR Path set with %zu points.", path.size());
    }
    LogMessage(msg);
}

const std::vector<Vector3>& PathManager::getPath(PathType type) const {
    if (type == PathType::GRIND) {
        return m_grindPath;
    } else { // VENDOR
        return m_vendorPath;
    }
}

void PathManager::clearPath(PathType type) {
    if (type == PathType::GRIND) {
        if (!m_grindPath.empty()) {
            m_grindPath.clear();
            m_currentGrindPathName.clear();
            LogMessage("PathManager: GRIND Path cleared.");
        }
    } else { // VENDOR
        if (!m_vendorPath.empty()) {
            m_vendorPath.clear();
            m_currentVendorPathName.clear();
            m_currentVendorName.clear(); // <<< ADDED: Clear vendor name too
            LogMessage("PathManager: VENDOR Path cleared.");
        }
    }
}

bool PathManager::hasPath(PathType type) const {
     if (type == PathType::GRIND) {
        return !m_grindPath.empty();
    } else { // VENDOR
        return !m_vendorPath.empty();
    }
} 