#include "spells_tab.h"
#include "imgui.h"
#include "objectmanager.h" // For IsInitialized()
#include "spellmanager.h"  // Updated for new SpellManager functions
#include "log.h"
#include "../utils/memory.h" // Needed for MemoryReader
#include <cstdint>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip> // For std::fixed, std::setprecision
#include <windows.h> // For GetTickCount
#include <fstream> // Added for rotation save/load later
#include <filesystem> // For directory creation
#include "../bot/core/RotationStep.h" // Include the dedicated header

// Helper function to get the directory where the DLL is running
// This helps ensure Rotations folder is created in the right place (e.g., Debug/Release folder)
std::string GetDllDirectory() {
    char path[MAX_PATH];
    HMODULE hModule = GetModuleHandleA("WoWDX9Hook.dll"); // Use your DLL name
    if (hModule != NULL && GetModuleFileNameA(hModule, path, MAX_PATH) > 0) {
        std::string fullPath(path);
        size_t lastSlash = fullPath.find_last_of("\\/");
        if (lastSlash != std::string::npos) {
            return fullPath.substr(0, lastSlash);
        }
    }
    return "."; // Fallback to current directory
}

namespace { // Anonymous namespace for constants and static data
    constexpr DWORD ADDR_CurrentTargetGUID = 0x00BD07B0; 
    
    // State for the Spells tab
    static int spellIdToCast = 0; 

    // --- Rotation Editor State ---
    static std::vector<RotationStep> currentRotation; // Use the type from BotController.h
    static int selectedRotationIndex = -1; 
    static std::vector<uint32_t> availableSpellIds; 
    static bool spells_dumped_for_rotation = false; // Flag for rotation spell list
    static char rotationFilename[128] = "MyRotation"; // Buffer for filename input
} // namespace

void RenderSpellsTab() {
    ImGui::TextUnformatted("Player Spell Management");

    // --- Spell Casting Section ---
    ImGui::Separator();
    ImGui::Text("Cast Spell:");
    ImGui::InputInt("Spell ID", &spellIdToCast); 
    if (ImGui::Button("Cast")) {
        uint64_t targetGuid = 0;
        try { // Safely read target GUID
            targetGuid = MemoryReader::Read<uint64_t>(ADDR_CurrentTargetGUID);
        } catch (const std::runtime_error& e) {
             LogStream ssErr;
             ssErr << "GUI::SpellsTab: Error reading target GUID from 0x" << std::hex << ADDR_CurrentTargetGUID << ": " << e.what();
             LogMessage(ssErr.str());
             targetGuid = 0; // Default to 0 on error
        }
        // CastSpell is an instance method
        SpellManager::GetInstance().CastSpell(spellIdToCast, targetGuid); 
    }
    ImGui::Separator();

    // --- Rotation Editor Section ---
    ImGui::Text("Rotation Editor");

    if (ImGui::Button("Load Known Spells")) { // Renamed button
        LogMessage("GUI::SpellsTab: Load Known Spells button clicked for rotation editor.");
        try {
            // Use static GetSpellbookIDs() which returns vector<uint32_t>
            availableSpellIds = SpellManager::GetSpellbookIDs(); 
            spells_dumped_for_rotation = true; 
            LogStream ss;
            ss << "GUI::SpellsTab: Loaded " << availableSpellIds.size() << " known spell IDs for rotation editor."; // Updated log message
            LogMessage(ss.str());
        } catch (const std::exception& e) {
             LogStream ssErr;
             ssErr << "GUI::SpellsTab: Exception loading known spell IDs: " << e.what(); // Updated log message
             LogMessage(ssErr.str());
             spells_dumped_for_rotation = false; // Reset flag on error
        } catch (...) {
            LogMessage("GUI::SpellsTab: Unknown exception loading known spell IDs."); // Updated log message
            spells_dumped_for_rotation = false; // Reset flag on error
        }
    }

    if (!spells_dumped_for_rotation) {
        ImGui::Text("Click 'Load Known Spells' to populate the list below.");
    } else {
        ImGui::Columns(2, "RotationColumns"); 
        ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.4f); // Adjust column width ratio if needed

        // --- Column 1: Available Spells --- 
        ImGui::BeginChild("AvailableSpellsRotation", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 15), true); // Fixed height or use -ve for dynamic
        ImGui::Text("Available Spells (%zu)", availableSpellIds.size()); // Use availableSpellIds
        ImGui::Separator();

        static char spellFilter[128] = "";
        ImGui::InputText("Filter##RotationFilter", spellFilter, IM_ARRAYSIZE(spellFilter));

        if (ImGui::BeginListBox("##SpellListBoxRotation", ImVec2(-FLT_MIN, -FLT_MIN))) { 
            // Convert filter to lowercase once before the loop if it's not empty
            std::string filterLower;
            if (spellFilter[0] != '\0') {
                filterLower = spellFilter;
                std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(), ::tolower);
            }

            for (const uint32_t& spellId : availableSpellIds) { 
                std::string spellName = SpellManager::GetSpellNameByID(spellId);
                std::string spellIdStr = std::to_string(spellId);
                
                // Corrected Filtering Logic (Case-Insensitive Name Check):
                bool nameMatches = false;
                if (!filterLower.empty() && !spellName.empty()) {
                    std::string nameLower = spellName;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                    nameMatches = (strstr(nameLower.c_str(), filterLower.c_str()) != nullptr);
                }
                
                bool idMatches = !filterLower.empty() && (strstr(spellIdStr.c_str(), filterLower.c_str()) != nullptr);

                // Skip if filter is active and neither name (case-insensitive) nor ID matches
                if (!filterLower.empty() && !nameMatches && !idMatches) {
                    continue;
                }
                
                std::string spellLabel = spellName.empty() ? ("ID: " + spellIdStr) : (spellName + " (" + spellIdStr + ")"); 
                
                if (ImGui::Selectable(spellLabel.c_str())) {
                    // Add to rotation using the ID and retrieved name
                    currentRotation.push_back({spellId, spellName, true}); 
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to add to rotation sequence.");
            }
            ImGui::EndListBox();
        }
        ImGui::EndChild();

        ImGui::NextColumn();

        // --- Column 2: Current Rotation Steps --- 
        ImGui::BeginChild("RotationSteps", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 15), true); // Match height
        ImGui::Text("Rotation Sequence (%zu)", currentRotation.size());
        ImGui::Separator();
        
        int action = -1; // 0=remove, 1=move up, 2=move down
        int actionIndex = -1;

        for (size_t i = 0; i < currentRotation.size(); ++i) {
            ImGui::PushID(static_cast<int>(i)); 
            RotationStep& step = currentRotation[i];
            std::string label = std::to_string(i + 1) + ". " + step.spellName + " (" + std::to_string(step.spellId) + ")";
            
            bool is_selected = (selectedRotationIndex == static_cast<int>(i));
            if (ImGui::Selectable(label.c_str(), is_selected, ImGuiSelectableFlags_AllowItemOverlap)) {
                selectedRotationIndex = static_cast<int>(i);
            }
            
            // Indent controls if the item is selected for better visibility
            if (is_selected) {
                ImGui::Indent();
                ImGui::PushItemWidth(80); // Adjust width for compact layout
                
                // --- Row 1: GCD, Target Req, Range --- 
                ImGui::Checkbox("GCD", &step.triggersGCD);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Does this spell trigger the 1.5s Global Cooldown?");
                ImGui::SameLine();
                ImGui::Checkbox("Needs Target", &step.requiresTarget);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Does this spell require an active target selected?");
                ImGui::SameLine();
                ImGui::InputFloat("Range", &step.castRange, 1.0f, 5.0f, "%.1f yd");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Maximum distance to attempt casting.");

                // --- Row 2: Player HP/MP --- 
                ImGui::InputFloat("Min Player HP%%", &step.minPlayerHealthPercent, 1.0f, 10.0f, "%.0f%%");
                ImGui::SameLine();
                ImGui::InputFloat("Max Player HP%%", &step.maxPlayerHealthPercent, 1.0f, 10.0f, "%.0f%%");
                ImGui::SameLine();
                ImGui::InputFloat("Min Player MP%%", &step.minPlayerManaPercent, 1.0f, 10.0f, "%.0f%%");
                 if (ImGui::IsItemHovered()) ImGui::SetTooltip("Min/Max Player Mana/Resource Percent");
                ImGui::SameLine(); // Keep Max MP on same line for compactness maybe?
                ImGui::InputFloat("Max Player MP%%", &step.maxPlayerManaPercent, 1.0f, 10.0f, "%.0f%%");

                // --- Row 3: Target HP --- 
                ImGui::InputFloat("Min Target HP%%", &step.minTargetHealthPercent, 1.0f, 10.0f, "%.0f%%");
                ImGui::SameLine();
                ImGui::InputFloat("Max Target HP%%", &step.maxTargetHealthPercent, 1.0f, 10.0f, "%.0f%%");
                
                ImGui::PopItemWidth();
                ImGui::Unindent();
                ImGui::Separator(); // Separate selected item's details
            }
            ImGui::PopID();
        }

        ImGui::EndChild();

        // Buttons below the rotation steps column
        if (ImGui::Button("Remove") && selectedRotationIndex >= 0 && selectedRotationIndex < static_cast<int>(currentRotation.size())) {
            action = 0; actionIndex = selectedRotationIndex;
        }
        ImGui::SameLine();
        if (ImGui::Button("Up") && selectedRotationIndex > 0) {
             action = 1; actionIndex = selectedRotationIndex;
        }
        ImGui::SameLine();
        if (ImGui::Button("Down") && selectedRotationIndex >= 0 && selectedRotationIndex < static_cast<int>(currentRotation.size()) - 1) {
            action = 2; actionIndex = selectedRotationIndex;
        }

        // Apply actions after the loop to avoid iterator invalidation issues
        if (action != -1) {
            if (action == 0) { // Remove
                 currentRotation.erase(currentRotation.begin() + actionIndex);
                 selectedRotationIndex = -1; // Reset selection
            } else if (action == 1) { // Move Up
                std::swap(currentRotation[actionIndex], currentRotation[actionIndex - 1]);
                selectedRotationIndex--;
            } else if (action == 2) { // Move Down
                 std::swap(currentRotation[actionIndex], currentRotation[actionIndex + 1]);
                selectedRotationIndex++;
            }
        }

        ImGui::Columns(1); 
        ImGui::Separator();

        // --- Save Rotation --- 
        ImGui::Text("Save Rotation:");
        ImGui::InputText("Filename##RotationFilename", rotationFilename, IM_ARRAYSIZE(rotationFilename));
        ImGui::SameLine();
        if (ImGui::Button("Save##RotationSave")) {
            if (rotationFilename[0] != '\0' && !currentRotation.empty()) {
                std::string dllDir = GetDllDirectory();
                std::filesystem::path rotationsDir = std::filesystem::path(dllDir) / "Rotations";
                std::filesystem::path filePath = rotationsDir / (std::string(rotationFilename) + ".json"); 

                try {
                    if (!std::filesystem::exists(rotationsDir)) {
                        std::filesystem::create_directories(rotationsDir);
                        LogMessage(("GUI::SpellsTab: Created directory: " + rotationsDir.string()).c_str());
                    }

                    std::ofstream outFile(filePath);
                    if (outFile.is_open()) {
                        outFile << "[\n"; // Start JSON array
                        for (size_t i = 0; i < currentRotation.size(); ++i) {
                            const auto& step = currentRotation[i];
                            outFile << "  {\n"; // Start JSON object
                            outFile << "    \"spellId\": " << step.spellId << ",\n";
                            outFile << "    \"spellName\": \"" << step.spellName << "\",\n"; // Ensure spellName is properly escaped if needed later
                            outFile << "    \"triggersGCD\": " << (step.triggersGCD ? "true" : "false") << ",\n"; // Output JSON boolean
                            outFile << "    \"requiresTarget\": " << (step.requiresTarget ? "true" : "false") << ",\n"; // Output JSON boolean
                            outFile << "    \"castRange\": " << std::fixed << std::setprecision(1) << step.castRange << ",\n";
                            outFile << "    \"minPlayerHealthPercent\": " << std::fixed << std::setprecision(1) << step.minPlayerHealthPercent << ",\n";
                            outFile << "    \"maxPlayerHealthPercent\": " << std::fixed << std::setprecision(1) << step.maxPlayerHealthPercent << ",\n";
                            outFile << "    \"minTargetHealthPercent\": " << std::fixed << std::setprecision(1) << step.minTargetHealthPercent << ",\n";
                            outFile << "    \"maxTargetHealthPercent\": " << std::fixed << std::setprecision(1) << step.maxTargetHealthPercent << ",\n";
                            outFile << "    \"minPlayerManaPercent\": " << std::fixed << std::setprecision(1) << step.minPlayerManaPercent << ",\n";
                            outFile << "    \"maxPlayerManaPercent\": " << std::fixed << std::setprecision(1) << step.maxPlayerManaPercent << "\n"; 
                            outFile << "  }"; // End JSON object
                            if (i < currentRotation.size() - 1) {
                                outFile << ","; 
                            }
                            outFile << "\n";
                        }
                        outFile << "]\n"; // End JSON array
                        
                        outFile.close();
                        LogStream ss;
                        ss << "GUI::SpellsTab: Rotation saved to " << filePath.string();
                        LogMessage(ss.str());
                    } else {
                         LogStream ssErr;
                         ssErr << "GUI::SpellsTab: Failed to open file for saving: " << filePath.string();
                         LogMessage(ssErr.str());
                    }
                } catch (const std::filesystem::filesystem_error& fs_err) {
                    LogStream ssErr;
                    ssErr << "GUI::SpellsTab: Filesystem error saving rotation: " << fs_err.what() << " Path: " << filePath.string();
                    LogMessage(ssErr.str());
                } catch (const std::exception& e) {
                    LogStream ssErr;
                    ssErr << "GUI::SpellsTab: Error saving rotation: " << e.what();
                    LogMessage(ssErr.str());
                }
            } else if (currentRotation.empty()) {
                 LogMessage("GUI::SpellsTab: Cannot save empty rotation.");
            } else {
                 LogMessage("GUI::SpellsTab: Please enter a filename to save the rotation.");
            }
        }
    } // End of if(spells_dumped_for_rotation) block

    // --- Remove old Spellbook Dump Section ---
    // The section previously starting with if (ImGui::Button("Dump Spellbook IDs")) 
    // and containing BeginChild("SpellbookDisplay") has been removed.
} 