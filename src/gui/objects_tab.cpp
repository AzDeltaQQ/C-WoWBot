#include "objects_tab.h"
#include "imgui.h"
#include "objectmanager.h"
#include "log.h"
#include "wowobject.h"
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <memory>

namespace {
    // State specific to the Objects tab
    std::vector<std::shared_ptr<WowObject>> object_list_pointers;
    int selected_object_index = -1;
    std::shared_ptr<WowObject> selected_object_ptr = nullptr;

    // Helper function to calculate distance between two points
    inline float CalculateDistance(const Vector3& p1, const Vector3& p2) {
        float dx = p1.x - p2.x;
        float dy = p1.y - p2.y;
        float dz = p1.z - p2.z;
        return sqrtf(dx*dx + dy*dy + dz*dz);
    }

    // Function to update the object list pointers for the GUI
    void UpdateObjectPointerList() {
        object_list_pointers.clear();
        selected_object_index = -1;
        selected_object_ptr = nullptr; // Clear selected pointer too

        LogMessage("GUI::UpdateObjectPointerList: Starting update...");
        ObjectManager* objMgr = ObjectManager::GetInstance();
        if (!objMgr || !objMgr->IsInitialized()) {
            LogMessage("GUI::UpdateObjectPointerList: Aborted - ObjectManager not ready.");
            return;
        }

        objMgr->Update(); // Refresh the ObjectManager's internal cache
        LogMessage("GUI::UpdateObjectPointerList: ObjectManager::Update() finished.");

        // Get local player position for distance check
        Vector3 playerPos = {0, 0, 0};
        bool playerPosValid = false;
        auto player = objMgr->GetLocalPlayer();
        if (player) {
            player->UpdateDynamicData(); // Ensure player position is fresh
            playerPos = player->GetPosition();
            if (playerPos.x != 0.0f || playerPos.y != 0.0f || playerPos.z != 0.0f) {
                playerPosValid = true;
            }
        }
        if (!playerPosValid) {
            LogMessage("GUI::UpdateObjectPointerList: Warning - Could not get valid player position for distance filtering.");
        }

        auto objects_map = objMgr->GetObjects(); // Get the map of shared_ptrs
        LogMessage("GUI::UpdateObjectPointerList: Retrieved " + std::to_string(objects_map.size()) + " raw objects from cache.");

        object_list_pointers.reserve(objects_map.size()); // Reserve potential max space
        
        constexpr float MAX_DISTANCE_FILTER = 1000.0f;
        
        for (const auto& pair : objects_map) {
            if (!pair.second) continue; // Skip null pointers
            
            auto& objPtr = pair.second;
            WowObjectType objType = objPtr->GetType();

            // Always include items, regardless of distance
            if (objType == OBJECT_ITEM) {
                object_list_pointers.push_back(objPtr);
                continue; 
            }

            // For non-items, filter by distance if player position is valid
            if (playerPosValid) {
                objPtr->UpdateDynamicData(); // Ensure object position is updated before checking distance
                Vector3 objPos = objPtr->GetPosition();
                float distance = CalculateDistance(playerPos, objPos);

                if (distance <= MAX_DISTANCE_FILTER) {
                    object_list_pointers.push_back(objPtr);
                }
            } else {
                // If player position is invalid, maybe include all non-items?
                // Or exclude them? Let's include them for now.
                object_list_pointers.push_back(objPtr); 
            }
        }
        LogMessage("GUI::UpdateObjectPointerList: Finished filtering. Added " + std::to_string(object_list_pointers.size()) + " objects to GUI list.");
        
        // Optional: Sort the list here if desired (e.g., by distance or name)
        // std::sort(object_list_pointers.begin(), ...);
    }

} // namespace

void RenderObjectsTab() {
    ObjectManager* objMgr = ObjectManager::GetInstance();

    // Refresh button now calls UpdateObjectPointerList
    if (!objMgr->IsInitialized()) {
        ImGui::TextDisabled("Refresh Objects (Initializing...)");
    } else {
        if (ImGui::Button("Refresh Objects")) {
            UpdateObjectPointerList(); // Update the pointer list
        }
    }
    ImGui::SameLine();
    ImGui::Text("%zu objects found", object_list_pointers.size());
    
    // Get local player pos once for distance calculation
    Vector3 playerPos = {0, 0, 0};
    bool playerPosValid = false;
    if (objMgr->IsInitialized()) { // Check again in case it changed
        auto player = objMgr->GetLocalPlayer();
        if (player) {
            // Use cached position after ensuring it's reasonably fresh
            player->UpdateDynamicData(); 
            playerPos = player->GetPosition(); 
            if (playerPos.x != 0.0f || playerPos.y != 0.0f || playerPos.z != 0.0f) {
                    playerPosValid = true;
            }
        }
    }

    // Dynamic list rendering
    float listHeight = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 4; // Leave space for details
    ImGui::BeginChild("ObjectList", ImVec2(0, listHeight > 0 ? listHeight : 100), true);
    
    if (object_list_pointers.empty()) {
        ImGui::Text(objMgr->IsInitialized() ? "No objects found or list not refreshed." : "Object Manager not initialized.");
    } else {
        for (size_t i = 0; i < object_list_pointers.size(); ++i) {
            const auto& objPtr = object_list_pointers[i];
            if (!objPtr) continue;

            WowObjectType currentObjType = objPtr->GetType();

            // Generate display string on the fly
            std::stringstream ssLabel;
            ssLabel << "GUID: 0x" << std::hex << std::setw(16) << std::setfill('0') << GuidToUint64(objPtr->GetGUID()) << " | ";
            ssLabel << "Name: '" << objPtr->GetName() << "' | "; 
            ssLabel << "Type: " << objPtr->GetType();

            // Add distance, handling OBJECT_ITEM specifically
            if (currentObjType == OBJECT_ITEM) {
                ssLabel << " | Dist: N/A";
            } else if (playerPosValid) {
                // Only calculate/show distance for non-items if player pos is valid
                objPtr->UpdateDynamicData(); // Ensure position is current for list display
                float distance = CalculateDistance(playerPos, objPtr->GetPosition());
                ssLabel << " | Dist: " << std::fixed << std::setprecision(1) << distance;
            } else {
                    ssLabel << " | Dist: ?"; // Indicate unknown distance if player pos invalid
            }

            std::string label = ssLabel.str();
            if (ImGui::Selectable(label.c_str(), selected_object_index == (int)i)) {
                selected_object_index = (int)i;
                selected_object_ptr = objPtr; // Store the shared_ptr
            }
        }
    }
    ImGui::EndChild(); // End ObjectList

    // Display details for the selected object
    ImGui::Separator();
    ImGui::Text("Selected Object Details:");
    if (selected_object_ptr) {
            // Update dynamic data just before displaying details
        selected_object_ptr->UpdateDynamicData();
        
        ImGui::Text("GUID: 0x%llX", GuidToUint64(selected_object_ptr->GetGUID()));
        ImGui::Text("Name: %s", selected_object_ptr->GetName().c_str());
        ImGui::Text("Type: %d", selected_object_ptr->GetType());
        Vector3 pos = selected_object_ptr->GetPosition();
        ImGui::Text("Pos: X: %.1f, Y: %.1f, Z: %.1f", pos.x, pos.y, pos.z);
        
        // Display distance, handling OBJECT_ITEM
        if (selected_object_ptr->GetType() == OBJECT_ITEM) {
            ImGui::Text("Dist: N/A");
        } else if (playerPosValid) {
                float distance = CalculateDistance(playerPos, pos);
                ImGui::Text("Dist: %.1f", distance);
        } else {
            ImGui::Text("Dist: ?");
        }
        
        // Display Unit-specific data if applicable
        auto selectedUnit = std::dynamic_pointer_cast<WowUnit>(selected_object_ptr);
        if (selectedUnit) {
            ImGui::Text("Health: %d / %d", selectedUnit->GetHealth(), selectedUnit->GetMaxHealth());
            ImGui::Text("%s: %d / %d", selectedUnit->GetPowerTypeString().c_str(), selectedUnit->GetPower(), selectedUnit->GetMaxPower());
            ImGui::Text("Level: %d", selectedUnit->GetLevel());
            ImGui::Text("Flags: 0x%X", selectedUnit->GetUnitFlags());
            ImGui::Text("Casting: %d", selectedUnit->GetCastingSpellId());
            ImGui::Text("Channeling: %d", selectedUnit->GetChannelSpellId());
            ImGui::Text("Is Dead: %s", selectedUnit->IsDead() ? "Yes" : "No");
        }
            // Add GameObject specific details if needed
            auto selectedGameObject = std::dynamic_pointer_cast<WowGameObject>(selected_object_ptr);
            if (selectedGameObject) {
                // Example: Display quest status if relevant
                // ImGui::Text("Quest Status: %d", selectedGameObject->GetQuestStatus());
            }

    } else {
        ImGui::Text("No object selected.");
    }
} 