#include "log.h"
#include <fstream> // For file stream
#include <windows.h> // For GetModuleFileNameA, GetModuleHandleA
// #include <filesystem> // Removed C++17 dependency
#include <string> // Required for std::string
#include <Shlwapi.h> // For PathRemoveFileSpecA
#pragma comment(lib, "Shlwapi.lib") // Link against Shlwapi

namespace {
    // Global log storage and mutex for thread safety
    std::vector<std::string> g_logMessages;
    std::mutex g_logMutex;
    constexpr size_t MAX_LOG_MESSAGES = 500; // Limit log history

    // Global log file stream
    std::ofstream g_logFile;
    std::string g_logFilePath; // Keep path for potential debugging
    bool g_logFileOpened = false; // Track if file is open

    // Helper to get DLL directory using WinAPI
    std::string GetDllDirectory() {
        char buffer[MAX_PATH] = { 0 };
        HMODULE hModule = GetModuleHandleA("WoWDX9Hook.dll"); 
        if (hModule == NULL) { 
             OutputDebugStringA("GetDllDirectory: GetModuleHandleA failed!\n");
             return "."; // Fallback to current directory
        }
        if (GetModuleFileNameA(hModule, buffer, MAX_PATH) == 0) {
            OutputDebugStringA("GetDllDirectory: GetModuleFileNameA failed!\n");
            return "."; // Fallback
        }
        // Use Shlwapi to remove the filename part
        if (!PathRemoveFileSpecA(buffer)) {
             OutputDebugStringA("GetDllDirectory: PathRemoveFileSpecA failed!\n");
             return "."; // Fallback
        }
        return std::string(buffer);
    }
} // namespace

void InitializeLogFile() {
    // No mutex lock here to avoid potential deadlocks with DllMain/Cleanup
    if (g_logFileOpened) return;

    try {
        std::string dir = GetDllDirectory();
        g_logFilePath = dir + "\\WoWDX9Hook.log";
        // Open in write/truncate mode to clear the file on each start
        // g_logFile.open(g_logFilePath, std::ios::app);
        g_logFile.open(g_logFilePath, std::ios::out | std::ios::trunc); 
        
        if (g_logFile.is_open()) {
            g_logFileOpened = true;
            // Avoid calling LogMessage here during initialization
            std::string initMsg = "Log file initialized: " + g_logFilePath + "\n";
            g_logFile << initMsg << std::flush; // Write directly and flush
            OutputDebugStringA(initMsg.c_str()); // Also send to debug output
        } else {
            g_logFileOpened = false;
            OutputDebugStringA(("Failed to open log file: " + g_logFilePath + "\n").c_str());
        }
    } catch (const std::exception& e) {
        g_logFileOpened = false;
        OutputDebugStringA(("Exception initializing log file: " + std::string(e.what()) + "\n").c_str());
    } catch (...) {
        g_logFileOpened = false;
         OutputDebugStringA("Unknown exception initializing log file.\n");
    }
}

void ShutdownLogFile() {
    // No mutex lock here
    if (g_logFileOpened && g_logFile.is_open()) {
        // Log directly to file before closing
        g_logFile << "Shutting down log file.\n" << std::flush;
        g_logFile.close();
    }
    g_logFileOpened = false;
}

void LogMessage(const std::string& message) {
    // No lazy initialization here - assume InitializeLogFile was called

    // Lock only for accessing the shared g_logMessages buffer
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_logMessages.size() >= MAX_LOG_MESSAGES) {
            g_logMessages.erase(g_logMessages.begin());
        }
        g_logMessages.push_back(message);
    }

    // Write to file (outside the mutex lock for g_logMessages)
    // Check file status without lock - minor race condition is acceptable here
    // compared to potential deadlock.
    if (g_logFileOpened && g_logFile.is_open()) { 
        g_logFile << message << std::endl; // std::endl flushes implicitly
    }
}

std::vector<std::string> GetLogMessages() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    // Return a copy to avoid holding the lock while ImGui iterates
    return g_logMessages;
}

void ClearLogMessages() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logMessages.clear();
} 