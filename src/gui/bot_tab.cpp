#include "bot_tab.h"
#include "imgui.h" // Assuming ImGui is used

// Include BotController header for interactions
#include "../bot/core/BotController.h" 
#include "../bot/pathing/PathManager.h" // Include full definition for PathManager
#include "../bot/pathing/PathRecorder.h" // Include full definition for PathRecorder
#include "../game/objectmanager.h" // Added: To use ObjectManager::GetInstance()
#include "../game/wowobject.h"     // Added: To use Vector3 and player object methods

#include <vector>
#include <string>
#include "../utils/log.h" // Include log header for LogMessage

namespace GUI {

    // --- Grind Path State ---
    static bool show_grind_path_creator_window = false; // Renamed flag
    static char grind_path_filename_buffer[128] = "MyGrindPath"; // Renamed buffer
    static std::vector<std::string> available_grind_paths; // Renamed cache
    static int selected_grind_path_index = -1; // Renamed index
    static std::string loaded_grind_path_name = ""; // Renamed loaded name

    // --- Vendor Path State (NEW) ---
    static bool show_vendor_path_creator_window = false;
    static char vendor_path_filename_buffer[128] = "MyVendorPath";
    static std::vector<std::string> available_vendor_paths;
    static int selected_vendor_path_index = -1;
    static std::string loaded_vendor_path_name = "";

    // State for Rotation Loading UI
    static std::vector<std::string> available_rotations;
    static int selected_rotation_index = -1;
    static std::string loaded_rotation_name = "";

    // Helper function to render path loading UI (avoids code duplication)
    void render_path_loader(const char* section_label, const char* id_suffix, 
                             BotController* botController, PathManager::PathType pathType,
                             std::vector<std::string>& available_paths_cache,
                             int& selected_path_index_cache,
                             std::string& loaded_path_name_cache) {
        ImGui::Text(section_label);
        ImGui::SameLine();
        
        std::vector<const char*> path_items;
        for (const auto& p : available_paths_cache) {
            path_items.push_back(p.c_str());
        }

        std::string combo_id = std::string("##PathSelectCombo") + id_suffix;
        std::string load_button_id = std::string("Load##LoadPathButton") + id_suffix;
        std::string refresh_button_id = std::string("Refresh##RefreshPathButton") + id_suffix;

        ImGui::PushItemWidth(150); 
        if (available_paths_cache.empty()) {
            ImGui::TextUnformatted("(No paths found)");
        } else {
            if (ImGui::Combo(combo_id.c_str(), &selected_path_index_cache, path_items.data(), path_items.size())) {
                // Selection changed
            }
        }
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button(load_button_id.c_str())) {
            if (selected_path_index_cache >= 0 && static_cast<size_t>(selected_path_index_cache) < available_paths_cache.size()) {
                const std::string& nameToLoad = available_paths_cache[selected_path_index_cache];
                bool success = false;
                if (pathType == PathManager::PathType::GRIND) {
                    success = botController->loadGrindPathByName(nameToLoad);
                } else {
                    success = botController->loadVendorPathByName(nameToLoad);
                }
                if (success) {
                    loaded_path_name_cache = nameToLoad; 
                }
            }
        }
        ImGui::SameLine(); 
        if (ImGui::Button(refresh_button_id.c_str())) {
            LogMessage("GUI: Refresh button clicked...");
            if (pathType == PathManager::PathType::GRIND) {
                 available_paths_cache = botController->getAvailableGrindPathNames();
                 loaded_path_name_cache = botController->getCurrentGrindPathName();
            } else {
                 available_paths_cache = botController->getAvailableVendorPathNames();
                 loaded_path_name_cache = botController->getCurrentVendorPathName();
            }
            selected_path_index_cache = -1; 
            for (size_t i = 0; i < available_paths_cache.size(); ++i) {
                 if (available_paths_cache[i] == loaded_path_name_cache) {
                     selected_path_index_cache = static_cast<int>(i);
                     break;
                 }
            }
        }
        if (!loaded_path_name_cache.empty()) {
             ImGui::SameLine();
             ImGui::TextDisabled("(Loaded: %s)", loaded_path_name_cache.c_str());
        }
    }

    // Helper function to render path creator window
    void render_path_creator_window(const char* window_title, const char* id_suffix, bool& show_window_flag,
                                    BotController* botController, PathManager::PathType pathType,
                                    char* filename_buffer) {
        if (show_window_flag) {
            ImGui::Begin(window_title, &show_window_flag, ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::Text("Path Recording Controls (%s)", (pathType == PathManager::PathType::GRIND ? "Grind" : "Vendor"));
            ImGui::Separator();
            
            // --- ADDED: Vendor Name Input (only for vendor paths) ---
            if (pathType == PathManager::PathType::VENDOR) {
                ImGui::InputText("Vendor Name", vendorNameBuffer, IM_ARRAYSIZE(vendorNameBuffer));
            }
            // --------------------------------------------------------

            static int path_interval_ms = 1000;
            ImGui::PushID(id_suffix); // Ensure unique IDs for interval slider if windows open simultaneously
            ImGui::SliderInt("Record Interval (ms)", &path_interval_ms, 100, 5000); 
            ImGui::PopID();

            bool is_recording = botController->getCurrentState() == BotController::State::PATH_RECORDING;
            const char* record_button_label = is_recording ? "Stop Recording" : "Start Recording";
            if (ImGui::Button(record_button_label)) {
                if (is_recording) {
                    // Stop whichever type is currently recording
                    if (pathType == PathManager::PathType::GRIND) botController->stopGrindPathRecording();
                    else botController->stopVendorPathRecording();
                } else {
                    if (pathType == PathManager::PathType::GRIND) {
                         botController->startGrindPathRecording(path_interval_ms);
                    } else {
                        // Pass vendor name when starting vendor path recording
                        botController->startVendorPathRecording(path_interval_ms, vendorNameBuffer); 
                    }
                }
            }

            // Display current coordinates while recording
            if (is_recording) {
                 // Ensure ObjectManager and WowObject headers are included at the top of the file
                 auto player = ObjectManager::GetInstance()->GetLocalPlayer();
                 if (player) {
                    player->UpdateDynamicData(); // Ensure position is fresh
                    Vector3 pos = player->GetPosition();
                    ImGui::Text("Recording at: X: %.2f, Y: %.2f, Z: %.2f", pos.x, pos.y, pos.z);
                 } else {
                    ImGui::Text("Recording... (Player not found?)");
                 }
            }

            // --- BEGIN ADDED CODE for Recorded Points List ---
            ImGui::Separator(); // Separate from recording controls
            ImGui::Text("Recorded Points:");

            // Create a scrollable child window for the points list
            float list_height = ImGui::GetTextLineHeightWithSpacing() * 8; // Adjust height (e.g., 8 lines high)
            ImGui::BeginChild("RecordedPointsList", ImVec2(0, list_height), true, ImGuiWindowFlags_HorizontalScrollbar);

            // Get the path recorder instance
            const PathRecorder* recorder = botController->getPathRecorder(); // Use accessor
            if (recorder) {
                 // Get the points from the recorder
                 const std::vector<Vector3>& current_path_points = recorder->getRecordedPath(); // Corrected method name

                 if (!current_path_points.empty()) {
                    for (size_t i = 0; i < current_path_points.size(); ++i) {
                        const auto& pt = current_path_points[i];
                        ImGui::Text("%zu: X: %.2f, Y: %.2f, Z: %.2f", i + 1, pt.x, pt.y, pt.z);
                    }
                 } else {
                    ImGui::TextUnformatted("(No points recorded yet)");
                 }
            } else {
                 ImGui::TextUnformatted("(Path Recorder unavailable)");
            }

            ImGui::EndChild(); // End RecordedPointsList
            // --- END ADDED CODE ---

            ImGui::Separator(); // Separator before Path Management
            ImGui::Text("Path Management");
            ImGui::InputText("Filename##Path", filename_buffer, IM_ARRAYSIZE(filename_buffer));
            ImGui::SameLine();
            if (ImGui::Button("Save##Path")) {
                if (strlen(filename_buffer) > 0) {
                    if (pathType == PathManager::PathType::GRIND) botController->saveCurrentGrindPath(filename_buffer);
                    else {
                         // Pass vendor name when saving vendor path
                        botController->saveCurrentVendorPath(filename_buffer, vendorNameBuffer); 
                    }
                } else {
                    LogMessage("GUI Error: Cannot save path with empty filename.");
                }
            }
            if (ImGui::Button("Clear Path##Path")) {
                 if (pathType == PathManager::PathType::GRIND) botController->clearCurrentGrindPath();
                 else botController->clearCurrentVendorPath();
                 // Optionally clear buffer: filename_buffer[0] = '\0';
            }
            
            ImGui::End();
        }
    }

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

        // Refresh path/rotation lists when tab appears
        if (ImGui::IsWindowAppearing()) { 
             // Refresh Grind Paths
             available_grind_paths = botController->getAvailableGrindPathNames();
             loaded_grind_path_name = botController->getCurrentGrindPathName();
             selected_grind_path_index = -1; 
             for (size_t i = 0; i < available_grind_paths.size(); ++i) {
                 if (available_grind_paths[i] == loaded_grind_path_name) {
                     selected_grind_path_index = static_cast<int>(i);
                     break;
                 }
             }
             // Refresh Vendor Paths
             available_vendor_paths = botController->getAvailableVendorPathNames();
             loaded_vendor_path_name = botController->getCurrentVendorPathName();
             selected_vendor_path_index = -1; 
             for (size_t i = 0; i < available_vendor_paths.size(); ++i) {
                 if (available_vendor_paths[i] == loaded_vendor_path_name) {
                     selected_vendor_path_index = static_cast<int>(i);
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

        // --- Grind Path Loading --- 
        render_path_loader("Grind Path:", "Grind", botController, PathManager::PathType::GRIND, 
                           available_grind_paths, selected_grind_path_index, loaded_grind_path_name);
        ImGui::Separator();

        // --- Vendor Path Loading (NEW) --- 
        render_path_loader("Vendor Path:", "Vendor", botController, PathManager::PathType::VENDOR,
                           available_vendor_paths, selected_vendor_path_index, loaded_vendor_path_name);
        ImGui::Separator();

        // --- Rotation Loading Controls ---
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

        // --- Pathing Creation Controls (Buttons for both types) --- 
        ImGui::Separator();
        ImGui::Text("Pathing Creation:"); 
        bool is_recording = botController->getCurrentState() == BotController::State::PATH_RECORDING;
        const char* recording_status_text = is_recording ? "(Recording...)" : "";
        
        if (ImGui::Button("Create/Edit Grind Path##BotTab")) { 
            show_grind_path_creator_window = true; 
        }
        ImGui::SameLine();
         if (ImGui::Button("Create/Edit Vendor Path##BotTab")) { 
            show_vendor_path_creator_window = true; 
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(recording_status_text);

        // --- Render Path Creator Windows --- 
        render_path_creator_window("Grind Path Creator", "Grind", show_grind_path_creator_window, 
                                   botController, PathManager::PathType::GRIND, grind_path_filename_buffer);
        render_path_creator_window("Vendor Path Creator", "Vendor", show_vendor_path_creator_window, 
                                   botController, PathManager::PathType::VENDOR, vendor_path_filename_buffer);

    }

} // namespace GUI 