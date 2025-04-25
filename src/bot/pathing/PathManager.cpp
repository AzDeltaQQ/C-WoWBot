#include "PathManager.h"
#include "../../utils/log.h" // Assuming log utility exists
#include <Windows.h>
#include <Shlwapi.h> // For PathRemoveFileSpecA, PathAppendA
#include <direct.h> // For _mkdir

// Include headers for file I/O
#include <fstream>
#include <sstream>
#include <filesystem> // Requires C++17

// Link Shlwapi.lib for path functions
#pragma comment(lib, "Shlwapi.lib")

namespace {
    // Helper function to get the directory containing the current DLL
    std::string GetDllDirectory() {
        char dllPath[MAX_PATH] = {0};
        HMODULE hModule = NULL;

        // Get handle to *this* DLL module
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCSTR)&GetDllDirectory, // Address within this module
                              &hModule)) {
            LogMessage("GetDllDirectory Error: GetModuleHandleExA failed.");
            return "."; // Return current directory as fallback
        }

        if (GetModuleFileNameA(hModule, dllPath, MAX_PATH) == 0) {
            LogMessage("GetDllDirectory Error: GetModuleFileNameA failed.");
            return ".";
        }

        // Remove the filename part to get the directory
        PathRemoveFileSpecA(dllPath);
        return std::string(dllPath);
    }
}

PathManager::PathManager() {
    LogMessage("PathManager: Instance created.");
}

PathManager::~PathManager() {
    LogMessage("PathManager: Instance destroyed.");
}

// Get the name of the currently loaded path
std::string PathManager::GetCurrentPathName() const {
    return m_currentPathName;
}

// List available .path files
std::vector<std::string> PathManager::ListAvailablePaths() const {
    std::vector<std::string> pathFiles;
    std::string dllDir = GetDllDirectory();
    char logBuffer[MAX_PATH + 200]; // Increased buffer size for detailed logs

    snprintf(logBuffer, sizeof(logBuffer), "PathManager: DLL Directory determined as '%s'", dllDir.c_str());
    LogMessage(logBuffer);

    std::filesystem::path pathsDir = std::filesystem::path(dllDir) / "Paths";
    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Looking for paths in '%s'", pathsDir.string().c_str());
    LogMessage(logBuffer);

    if (!std::filesystem::exists(pathsDir)) {
        snprintf(logBuffer, sizeof(logBuffer), "PathManager: Paths directory '%s' does not exist.", pathsDir.string().c_str());
        LogMessage(logBuffer);
        return pathFiles;
    }
    if (!std::filesystem::is_directory(pathsDir)) {
        snprintf(logBuffer, sizeof(logBuffer), "PathManager: '%s' exists but is not a directory.", pathsDir.string().c_str());
        LogMessage(logBuffer);
        return pathFiles;
    }

    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Paths directory '%s' exists and is a directory. Iterating...", pathsDir.string().c_str());
    LogMessage(logBuffer);

    try {
        int count = 0;
        for (const auto& entry : std::filesystem::directory_iterator(pathsDir)) {
            count++;
            std::string entryPathStr = entry.path().string();
            std::string entryExtStr = entry.path().extension().string();
            bool isFile = entry.is_regular_file();
            bool isCorrectExt = entryExtStr == ".path";

            snprintf(logBuffer, sizeof(logBuffer), "PathManager: Found entry '%s' [File: %d, Ext: '%s', Match: %d]",
                       entryPathStr.c_str(), isFile, entryExtStr.c_str(), isCorrectExt);
            LogMessage(logBuffer);

            if (isFile && isCorrectExt) {
                std::string stem = entry.path().stem().string();
                snprintf(logBuffer, sizeof(logBuffer), "PathManager: Adding path '%s'", stem.c_str());
                LogMessage(logBuffer);
                pathFiles.push_back(stem);
            }
        }
        if (count == 0) {
             snprintf(logBuffer, sizeof(logBuffer), "PathManager: No entries found within directory '%s'.", pathsDir.string().c_str());
             LogMessage(logBuffer);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        // Buffer already declared above
        snprintf(logBuffer, sizeof(logBuffer), "PathManager Error listing paths: %s", e.what());
        LogMessage(logBuffer);
    } catch (const std::exception& e) { // Catch other potential exceptions
        snprintf(logBuffer, sizeof(logBuffer), "PathManager Error listing paths (std::exception): %s", e.what());
        LogMessage(logBuffer);
    } catch (...) { // Catch anything else
        LogMessage("PathManager Error listing paths (Unknown exception)");
    }
    
    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Found %zu paths.", pathFiles.size());
    LogMessage(logBuffer);

    std::sort(pathFiles.begin(), pathFiles.end());
    return pathFiles;
}

bool PathManager::loadPath(const std::string& filename) {
    std::string dllDir = GetDllDirectory();
    std::filesystem::path fullPath = std::filesystem::path(dllDir) / "Paths" / (filename + ".path");

    // Declare buffer ONCE at the start of the function scope, large enough for all messages
    char logBuffer[MAX_PATH + 150]; 

    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Attempting to load path from '%s'...", fullPath.string().c_str());
    LogMessage(logBuffer);

    std::ifstream infile(fullPath);
    if (!infile.is_open()) {
        // Use the existing logBuffer
        snprintf(logBuffer, sizeof(logBuffer), "PathManager Error: Could not open file '%s' for loading.", fullPath.string().c_str());
        LogMessage(logBuffer);
        return false;
    }

    std::vector<Vector3> loadedPath;
    std::string line;
    int lineNum = 0;
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
             // Use the existing logBuffer
             snprintf(logBuffer, sizeof(logBuffer), "PathManager Error: Failed to parse line %d ('%s') during load (%s). Skipping.", lineNum, line.c_str(), e.what());
             LogMessage(logBuffer);
        }
    }
    infile.close();

    m_currentPath = loadedPath;
    m_currentPathName = filename;
    // Use the existing logBuffer (size MAX_PATH + 150 is sufficient)
    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Successfully loaded %zu points from '%s'. Path name set to '%s'.", m_currentPath.size(), fullPath.string().c_str(), m_currentPathName.c_str());
    LogMessage(logBuffer);
    return true;
}

bool PathManager::savePath(const std::string& filename) const {
    if (m_currentPath.empty()) {
        LogMessage("PathManager Warning: Attempted to save an empty path.");
        return false; 
    }

    std::string dllDir = GetDllDirectory();
    std::filesystem::path pathsDir = std::filesystem::path(dllDir) / "Paths";
    std::filesystem::path fullPath = pathsDir / (filename + ".path");
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
    
    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Attempting to save %zu points to '%s'...", m_currentPath.size(), fullPath.string().c_str());
    LogMessage(logBuffer);
    std::ofstream outfile(fullPath);
    if (!outfile.is_open()) {
        snprintf(logBuffer, sizeof(logBuffer), "PathManager Error: Could not open file '%s' for saving.", fullPath.string().c_str());
        LogMessage(logBuffer);
        return false;
    }

    for (const auto& p : m_currentPath) {
        outfile << p.x << "," << p.y << "," << p.z << "\n";
    }

    outfile.close();

    if (outfile.fail()) { 
         snprintf(logBuffer, sizeof(logBuffer), "PathManager Error: Failed to write data correctly to '%s'.", fullPath.string().c_str());
         LogMessage(logBuffer);
         return false;
    }

    snprintf(logBuffer, sizeof(logBuffer), "PathManager: Successfully saved path to '%s'.", fullPath.string().c_str());
    LogMessage(logBuffer);
    return true;
}

void PathManager::setPath(const std::vector<Vector3>& path) {
    m_currentPath = path;
    char msg[100];
    snprintf(msg, sizeof(msg), "PathManager: Path set with %zu points.", path.size());
    LogMessage(msg);
}

const std::vector<Vector3>& PathManager::getPath() const {
    return m_currentPath;
}

void PathManager::clearPath() {
    if (!m_currentPath.empty()) {
        m_currentPath.clear();
        m_currentPathName.clear(); // Clear the name too
        LogMessage("PathManager: Path cleared.");
    } else {
        // LogMessage("PathManager: ClearPath called on empty path."); // Optional
    }
}

bool PathManager::hasPath() const {
    return !m_currentPath.empty();
} 