#include "log.h"

namespace {
    // Global log storage and mutex for thread safety
    std::vector<std::string> g_logMessages;
    std::mutex g_logMutex;
    constexpr size_t MAX_LOG_MESSAGES = 500; // Limit log history
} // namespace

void LogMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logMessages.size() >= MAX_LOG_MESSAGES) {
        // Remove the oldest message to make space
        g_logMessages.erase(g_logMessages.begin());
    }
    g_logMessages.push_back(message);
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