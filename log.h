#pragma once

#include <vector>
#include <string>
#include <mutex>

// Function to add a message to the log buffer
void LogMessage(const std::string& message);

// Function to retrieve all log messages (thread-safe copy)
std::vector<std::string> GetLogMessages();

// Function to clear the log buffer
void ClearLogMessages(); 