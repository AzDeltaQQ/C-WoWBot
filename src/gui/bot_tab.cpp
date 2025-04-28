#include "bot_tab.h"
#include "imgui.h" // Assuming ImGui is used

// Include BotController header for interactions
#include "../bot/core/BotController.h" 
#include "../bot/pathing/PathManager.h" // Include full definition for PathManager
#include "../bot/pathing/PathRecorder.h" // Include full definition for PathRecorder

#include <vector>
#include <string>
#include "../utils/log.h" // Include log header for LogMessage

namespace GUI {

    static bool show_path_creator_window = false; // Flag to control visibility
    static char path_filename_buffer[128] = "MyPath"; // Buffer for filename input
    
    // State for Path Loading UI
    static std::vector<std::string> available_paths; // Cache for path names
    static int selected_path_index = -1; // Index of the path selected in the combo box
    static std::string loaded_path_name = ""; // Name of the currently loaded path

    // State for Rotation Loading UI
    static std::vector<std::string> available_rotations;
    static int selected_rotation_index = -1;
    static std::string loaded_rotation_name = "";

    void render_bot_tab(BotController* botController) { // Updated signature
        if (!botController) {
            ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "BotController not initialized!");
            return;
        }

        // Always ensure engine is set to a valid type (moved outside IsWindowAppearing)
        if (botController->getCurrentEngineType() == BotController::EngineType::NONE) {
            LogMessage("GUI: BotController engine is NONE, setting default (Grinding).");
            botController->setEngine(BotController::EngineType::GRINDING); // Set to the first available engine
        }

        // Refresh path list when tab becomes visible (simple refresh strategy)
        if (ImGui::IsWindowAppearing()) { // Or use a dedicated refresh button
             // Refresh Paths
             available_paths = botController->getAvailablePathNames();
             loaded_path_name = botController->getCurrentPathName();
             selected_path_index = -1; 
             for (size_t i = 0; i < available_paths.size(); ++i) {
                 if (available_paths[i] == loaded_path_name) {
                     selected_path_index = static_cast<int>(i);
                     break;
                 }
             }
             // Refresh Rotations
             available_rotations = botController->getAvailableRotationNames(); // Assumes this method exists
             loaded_rotation_name = botController->getCurrentRotationName(); // Assumes this method exists
             selected_rotation_index = -1;
              for (size_t i = 0; i < available_rotations.size(); ++i) {
                 if (available_rotations[i] == loaded_rotation_name) {
                     selected_rotation_index = static_cast<int>(i);
                     break;
                 }
             }
        }

        ImGui::Text("Bot Controls");

        // --- Engine Selection --- 
        ImGui::Separator();
        ImGui::Text("Engine:");
        ImGui::SameLine();

        const char* engine_items[] = { "Grinding" /*, "Questing", "Gathering" */ };
        // Reflect the actual current engine from BotController
        int current_engine_index = static_cast<int>(botController->getCurrentEngineType()) -1; // Assuming EngineType::NONE = 0, GRINDING = 1 etc.
        if (current_engine_index < 0) current_engine_index = 0; // Handle NONE case

        if (ImGui::Combo("##EngineCombo", &current_engine_index, engine_items, IM_ARRAYSIZE(engine_items))) {
            // Update BotController when selection changes
            botController->setEngine(static_cast<BotController::EngineType>(current_engine_index + 1)); // +1 to map back to enum value
        }
        ImGui::Separator();

        // --- Path Loading Controls ---
        ImGui::Text("Load Path:");
        ImGui::SameLine();
        
        // Convert vector<string> to const char* for ImGui Combo
        std::vector<const char*> path_items;
        for (const auto& p : available_paths) {
            path_items.push_back(p.c_str());
        }

        ImGui::PushItemWidth(150); // Adjust width as needed
        if (available_paths.empty()) {
            ImGui::TextUnformatted("(No paths found)");
        } else {
            if (ImGui::Combo("##PathSelectCombo", &selected_path_index, path_items.data(), path_items.size())) {
                // Selection changed - update internal state if needed, but loading happens on button press
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Load##LoadPathButton")) {
            if (selected_path_index >= 0 && static_cast<size_t>(selected_path_index) < available_paths.size()) {
                const std::string& nameToLoad = available_paths[selected_path_index];
                if (botController->loadPathByName(nameToLoad)) {
                    loaded_path_name = nameToLoad; // Update displayed loaded path on success
                } else {
                    // Optionally show an error popup to the user
                }
            } else {
                 LogMessage("GUI: No path selected to load.");
            }
        }
        ImGui::SameLine(); // Add refresh button
        if (ImGui::Button("Refresh##RefreshPathButton")) {
            LogMessage("GUI: Refresh button clicked, refreshing path list...");
            available_paths = botController->getAvailablePathNames();
            loaded_path_name = botController->getCurrentPathName();
            selected_path_index = -1; // Reset selection
            // Try to find the loaded path in the available list and set the index
            for (size_t i = 0; i < available_paths.size(); ++i) {
                 if (available_paths[i] == loaded_path_name) {
                     selected_path_index = static_cast<int>(i);
                     break;
                 }
            }
        }

        // Display currently loaded path
        if (!loaded_path_name.empty()) {
             ImGui::SameLine();
             ImGui::TextDisabled("(Loaded: %s)", loaded_path_name.c_str());
        }
        ImGui::Separator();

        // --- Rotation Loading Controls (New Section) ---
        ImGui::Text("Load Rotation:");
        ImGui::SameLine();

        // Convert vector<string> to const char* for ImGui Combo
        std::vector<const char*> rotation_items;
        for (const auto& r : available_rotations) {
            rotation_items.push_back(r.c_str());
        }

        ImGui::PushItemWidth(150); // Adjust width as needed
        if (available_rotations.empty()) {
            ImGui::TextUnformatted("(No rotations found)");
        } else {
            if (ImGui::Combo("##RotationSelectCombo", &selected_rotation_index, rotation_items.data(), rotation_items.size())) {
                // Selection changed - loading happens on button press
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Load##LoadRotationButton")) {
            if (selected_rotation_index >= 0 && static_cast<size_t>(selected_rotation_index) < available_rotations.size()) {
                const std::string& nameToLoad = available_rotations[selected_rotation_index];
                // Assumes BotController has this method
                if (botController->loadRotationByName(nameToLoad)) { 
                    loaded_rotation_name = nameToLoad; // Update displayed loaded rotation on success
                     LogMessage(("GUI: Rotation loaded: " + nameToLoad).c_str());
                } else {
                    LogMessage(("GUI Error: Failed to load rotation: " + nameToLoad).c_str());
                    // Optionally show an error popup
                }
            } else {
                 LogMessage("GUI: No rotation selected to load.");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh##RefreshRotationButton")) {
            LogMessage("GUI: Refresh button clicked, refreshing rotation list...");
            // Assumes these methods exist in BotController
            available_rotations = botController->getAvailableRotationNames(); 
            loaded_rotation_name = botController->getCurrentRotationName(); 
            selected_rotation_index = -1; // Reset selection
            // Try to find the loaded rotation in the available list
             for (size_t i = 0; i < available_rotations.size(); ++i) {
                 if (available_rotations[i] == loaded_rotation_name) {
                     selected_rotation_index = static_cast<int>(i);
                     break;
                 }
             }
        }

        // Display currently loaded rotation
        if (!loaded_rotation_name.empty()) {
             ImGui::SameLine();
             ImGui::TextDisabled("(Loaded: %s)", loaded_rotation_name.c_str());
        }
        ImGui::Separator(); // Add separator after rotation loading

        // --- Bot Settings --- 
        bool loot_enabled = botController->isLootingEnabled();
        if (ImGui::Checkbox("Enable Looting", &loot_enabled)) {
            botController->setLootingEnabled(loot_enabled);
        }
        // Add other settings like Skinning, Mining etc. here later

        ImGui::Separator(); // Add separator after settings

        // --- Start/Stop Button --- 
        // Get running state from BotController
        bool is_running = botController->isRunning(); 
        const char* start_stop_label = is_running ? "Stop Bot" : "Start Bot";
        if (ImGui::Button(start_stop_label, ImVec2(-FLT_MIN, 0))) { // Full width button
            // Call start/stop on BotController
            if (is_running) {
                LogMessage("GUI: Stop button clicked, calling botController->stop()");
                botController->stop();
            }
            else {
                // Log current engine type for debugging
                BotController::EngineType currentEngine = botController->getCurrentEngineType();
                char debugMsg[128];
                snprintf(debugMsg, sizeof(debugMsg), "GUI: Start button clicked with engine type: %d", 
                         static_cast<int>(currentEngine));
                LogMessage(debugMsg);
                
                botController->start();
            }
        }

        // --- Pathing Creation Controls (Button) --- 
        ImGui::Separator();
        ImGui::Text("Pathing Creation:"); // Renamed section slightly
        bool is_recording = botController->getCurrentState() == BotController::State::PATH_RECORDING;
        if (ImGui::Button("Create/Edit Path##BotTab", ImVec2(-FLT_MIN, 0))) { // Renamed button
            show_path_creator_window = true; // Set flag instead of opening popup
        }
        ImGui::SameLine();
        ImGui::Text(is_recording ? "(Recording...)" : "(Not Recording)");

        // --- Path Creator Window (replaces popup) --- 
        if (show_path_creator_window) {
            // Use Begin/End for a standard window. Pass the boolean by reference to get the close button behavior.
            ImGui::Begin("Path Creator", &show_path_creator_window, ImGuiWindowFlags_AlwaysAutoResize);
            
            ImGui::Text("Path Recording Controls");
            ImGui::Separator();
            
            static int path_interval_ms = 1000;
            ImGui::SliderInt("Record Interval (ms)", &path_interval_ms, 100, 5000); 

            const char* record_button_label = is_recording ? "Stop Recording" : "Start Recording";
            if (ImGui::Button(record_button_label)) {
                if (is_recording) {
                    botController->stopPathRecording();
                }
                else {
                    botController->startPathRecording(path_interval_ms);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear Path")) {
                botController->clearCurrentPath();
            }
            
            // --- Add Save Controls ---
            ImGui::PushItemWidth(150); // Limit width of input text
            ImGui::InputText("##PathFilename", path_filename_buffer, IM_ARRAYSIZE(path_filename_buffer));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Save Path")) {
                if (strlen(path_filename_buffer) > 0) {
                    // Call BotController to save the path
                    std::string filenameToSave = path_filename_buffer;
                    if (botController->saveCurrentPath(filenameToSave)) {
                        // --- Refresh the path list after successful save ---
                        LogMessage("GUI: Path saved successfully, refreshing available paths list...");
                        available_paths = botController->getAvailablePathNames(); // Update the static list
                        // Set the newly saved path as the currently loaded/selected one
                        loaded_path_name = filenameToSave;
                        selected_path_index = -1; 
                        for (size_t i = 0; i < available_paths.size(); ++i) {
                             if (available_paths[i] == loaded_path_name) {
                                 selected_path_index = static_cast<int>(i);
                                 break;
                             }
                        }
                        // -----------------------------------------------------
                    } else {
                         LogMessage("GUI Error: BotController failed to save path.");
                    }
                } else {
                    // TODO: Optionally show an error message if filename is empty
                    LogMessage("GUI Error: Path filename cannot be empty.");
                }
            }
            // -------------------------

            ImGui::Separator();
            ImGui::Text("Recorded Points:");
            
            ImGui::BeginChild("PathPointsList", ImVec2(350, 150), true, ImGuiWindowFlags_HorizontalScrollbar);
            
            const std::vector<Vector3>* path_to_display = nullptr; 
            bool is_rec = botController->getCurrentState() == BotController::State::PATH_RECORDING;
            
            if (is_rec) {
                // If recording, get the live path from the recorder
                const PathRecorder* recorder = botController->getPathRecorder();
                if (recorder) {
                    path_to_display = &recorder->getRecordedPath();
                }
            } else {
                // If not recording, get the completed path from the manager
                const PathManager* pathMgr = botController->getPathManager(); 
                if (pathMgr) { 
                   path_to_display = &pathMgr->getPath();
                }
            }
            
            // Display the selected path (if found)
            if (path_to_display) {
                if (path_to_display->empty()) {
                   ImGui::TextUnformatted("(No points recorded)");
                }
                for (size_t i = 0; i < path_to_display->size(); ++i) {
                   const auto& pt = (*path_to_display)[i]; 
                   ImGui::Text("%zu: (%.2f, %.2f, %.2f)", i + 1, pt.x, pt.y, pt.z);
                }
                // Auto-scroll to bottom if path is long and currently recording
                if (is_rec && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                   ImGui::SetScrollHereY(1.0f);
                }
            } else {
                 // Handle case where neither recorder nor manager was available
                 ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Path source not available.");
            }
            
            ImGui::EndChild();
            
            ImGui::Separator();
            // No explicit close button needed, the 'X' on the window works via the boolean flag
            // if (ImGui::Button("Close")) {
            //     show_path_creator_window = false;
            // }
            ImGui::End(); // End the Path Creator window
        }

    }

} // namespace GUI 