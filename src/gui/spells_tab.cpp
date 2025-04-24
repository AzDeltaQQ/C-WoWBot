#include "spells_tab.h"
#include "imgui.h"
#include "objectmanager.h" // For IsInitialized()
#include "spellmanager.h"  // For CastSpell
#include "log.h"
#include <cstdint>

namespace {
    // Address for current target GUID - Should be defined centrally if used elsewhere
    // For now, keeping it local to where it's used.
    constexpr DWORD ADDR_CurrentTargetGUID = 0x00BD07B0; 
    
    // State for the Spells tab
    static int spellIdToCast = 0; 
} // namespace

void RenderSpellsTab() {
    ObjectManager* objMgr = ObjectManager::GetInstance(); // Needed for IsInitialized check

    ImGui::InputInt("Spell ID", &spellIdToCast);
    
    // Disable button if ObjMgr isn't ready or function ptr invalid (checked inside CastSpell)
    bool canCast = objMgr->IsInitialized(); 
    if (!canCast) {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("Cast Spell on Target")) {
        uint64_t currentTargetGuid = 0;
        try {
            // Read the target GUID directly from the address
            // WARNING: Reading directly like this can be unsafe if the address is invalid!
            // A safer approach might involve game functions if available.
            currentTargetGuid = *(uint64_t*)ADDR_CurrentTargetGUID;
            
            LogStream ss;
            ss << "GUI::SpellsTab: Attempting to cast SpellID: " << spellIdToCast 
                << " on TargetGUID: 0x" << std::hex << currentTargetGuid;
            LogMessage(ss.str());
            
            if (currentTargetGuid != 0) {
                bool success = SpellManager::GetInstance().CastSpell(spellIdToCast, currentTargetGuid);
                LogMessage(success ? "GUI::SpellsTab: CastSpell call succeeded (returned true)." : "GUI::SpellsTab: CastSpell call failed (returned false).");
            } else {
                LogMessage("GUI::SpellsTab: No target selected (GUID is 0).");
            }
            
        } catch (const std::exception& e) {
            LogStream ssErr;
            ssErr << "GUI::SpellsTab: Exception reading target GUID or casting spell: " << e.what();
            LogMessage(ssErr.str());
        } catch (...) {
            LogMessage("GUI::SpellsTab: Unknown exception reading target GUID or casting spell.");
        }
    }
    
    if (!canCast) {
        ImGui::EndDisabled();
    }
} 