#include "main_tab.h"
#include "imgui.h"
#include "objectmanager.h" // Include necessary headers
#include "wowobject.h"     // For WowUnit, WowPlayer, Vector3 etc.
#include <memory>          // For std::dynamic_pointer_cast

void RenderMainTab() {
    ObjectManager* objMgr = ObjectManager::GetInstance();

    // Check if ObjMgr is initialized before accessing player data
    if (objMgr->IsInitialized()) {
        auto player = objMgr->GetLocalPlayer();
        if (player) {
            player->UpdateDynamicData(); // Update player data every frame

            ImGui::Text("Local Player: %s", player->GetName().c_str());
            Vector3 pos = player->GetPosition();
            ImGui::Text("Position: X: %.2f, Y: %.2f, Z: %.2f", pos.x, pos.y, pos.z);
            ImGui::Text("Facing: %.2f", player->GetFacing());
            // Cast to WowUnit to access unit-specific data
            auto playerUnit = std::dynamic_pointer_cast<WowUnit>(player);
            if (playerUnit) {
                ImGui::Text("Health: %d / %d", playerUnit->GetHealth(), playerUnit->GetMaxHealth());
                
                // Get power values
                int currentPower = playerUnit->GetPower();
                int maxPower = playerUnit->GetMaxPower();
                auto powerType = playerUnit->GetPowerType();

                // Adjust Rage values (WoW 3.3.5 stores rage * 10)
                // Power Type Indices: 0=Mana, 1=Rage, 2=Focus, 3=Energy, etc.
                if (powerType == 1) { // Use literal 1 for Rage
                    currentPower /= 10;
                    maxPower /= 10; // Max rage is 100 (1000/10)
                }

                ImGui::Text("%s: %d / %d", playerUnit->GetPowerTypeString().c_str(), currentPower, maxPower);
                ImGui::Text("Level: %d", playerUnit->GetLevel());
                ImGui::Text("Flags: 0x%X", playerUnit->GetUnitFlags());
                ImGui::Text("Casting: %d", playerUnit->GetCastingSpellId());
                ImGui::Text("Channeling: %d", playerUnit->GetChannelSpellId());
            }
        } else {
            ImGui::Text("Local player object not found in cache/lookup.");
        }
    } else {
        ImGui::Text("Object Manager initializing...");
    }
} 