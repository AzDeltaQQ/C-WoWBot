#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <sstream>
#include <iomanip>

// Helper class for easy stream-based logging
class LogStream {
public:
    template<typename T>
    LogStream& operator<<(const T& value) {
        m_stream << value;
        return *this;
    }
    // Handle stream manipulators like std::hex, std::endl
    typedef std::ostream& (*Manipulator)(std::ostream&);
    LogStream& operator<<(Manipulator manip) {
        manip(m_stream);
        return *this;
    }
    // Get the string content
    std::string str() const {
        return m_stream.str();
    }
private:
    std::stringstream m_stream;
};

// Function to initialize the log file
void InitializeLogFile();

// Function to shutdown the log file
void ShutdownLogFile();

// Function to add a message to the log buffer and file
void LogMessage(const std::string& message);

// Function to retrieve all log messages (thread-safe copy)
std::vector<std::string> GetLogMessages();

// Function to clear the log buffer
void ClearLogMessages(); 