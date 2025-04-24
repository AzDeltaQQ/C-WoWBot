#include "log_tab.h"
#include "imgui.h"
#include "log.h"      // For GetLogMessages, ClearLogMessages
#include <vector>
#include <string>

// Need to store the combined log string persistently for InputTextMultiline
// Using a static variable within the function scope is one way.
// Alternatively, make it a static member of a class or global in an anonymous namespace.
// For simplicity here, static local variable.
void RenderLogTab() {
    // Button to clear the log
    if (ImGui::Button("Clear Log")) {
        ClearLogMessages();
    }
    ImGui::Separator();
    
    // --- Display Log using InputTextMultiline for Copy/Paste --- 
    // Get the log messages (returns a copy)
    auto logs = GetLogMessages(); 

    // Combine logs into a single string - do this every frame
    static std::string log_combined; // Static to persist buffer
    log_combined.clear(); // Clear before rebuilding
    for (const auto& msg : logs) {
        log_combined += msg; 
        log_combined += "\n"; // Add newline after each message
    }

    // Ensure the string is null-terminated for ImGui::InputTextMultiline
    // ImGui expects a writable buffer, even though it's read-only.
    // We resize to ensure there's space for the null terminator. 
    // Directly using log_combined.c_str() might be problematic if the buffer isn't writable.
    // Using a temporary buffer or ensuring the static string has capacity is safer.
    // A simple approach is to copy to a temporary buffer, but that's inefficient.
    // Let's try resizing the static string and passing its data pointer.
    log_combined.resize(log_combined.length() + 1); // Ensure space for null terminator
    
    // Use InputTextMultiline in ReadOnly mode
    // Calculate size to fill available space
    ImVec2 available_size = ImGui::GetContentRegionAvail();
    // Use -FLT_MIN for height to automatically use available vertical space is better
    ImGui::InputTextMultiline("##LogView", 
                                &log_combined[0], // Pass pointer to the buffer
                                log_combined.size(), // Pass the buffer size (including null terminator space)
                                ImVec2(available_size.x, available_size.y > 0 ? available_size.y : -FLT_MIN), // Use available height
                                ImGuiInputTextFlags_ReadOnly);
    // --- End Log Display --- 
} 