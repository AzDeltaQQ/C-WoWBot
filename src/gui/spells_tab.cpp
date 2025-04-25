#include "spells_tab.h"
#include "imgui.h"
#include "objectmanager.h" // For IsInitialized()
#include "spellmanager.h"  // Updated for new SpellManager functions
#include "log.h"
#include <cstdint>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip> // For std::fixed, std::setprecision
#include <windows.h> // For GetTickCount

namespace { // Anonymous namespace for constants
    // Address for current target GUID - Should be defined centrally if used elsewhere
    // For now, keeping it local to where it's used.
    constexpr DWORD ADDR_CurrentTargetGUID = 0x00BD07B0; 
    
    // State for the Spells tab
    static int spellIdToCast = 0; 
    static std::vector<uint32_t> spellbook_ids; // To store dumped spell IDs
    static bool spellbook_dumped = false; // Flag to check if dumped
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
                // Assuming SpellManager is the right place for ReadSpellbook
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

    ImGui::Separator(); // Add a separator

    // --- Spellbook Dump Section ---
    if (ImGui::Button("Dump Spellbook")) {
        LogMessage("GUI::SpellsTab: Dump Spellbook button clicked.");
        try {
            // Placeholder: Call the function to read the spellbook
            // We assume this function exists in SpellManager for now.
            // It should handle reading SpellCount and the SpellBook array.
            spellbook_ids = SpellManager::GetInstance().ReadSpellbook(); 
            spellbook_dumped = true; 
            LogStream ss;
            ss << "GUI::SpellsTab: Read " << spellbook_ids.size() << " spell IDs from spellbook.";
            LogMessage(ss.str());
        } catch (const std::exception& e) {
             LogStream ssErr;
             ssErr << "GUI::SpellsTab: Exception dumping spellbook: " << e.what();
             LogMessage(ssErr.str());
             spellbook_dumped = false; // Reset flag on error
        } catch (...) {
            LogMessage("GUI::SpellsTab: Unknown exception dumping spellbook.");
            spellbook_dumped = false; // Reset flag on error
        }
    }

    // Display the dumped spellbook if available
    if (spellbook_dumped) {
        ImGui::BeginChild("SpellbookDisplay", ImVec2(0, 200), true); // Child window with border
        ImGui::Text("Spellbook Contents (%zu entries):", spellbook_ids.size());
        ImGui::Separator();
        for (uint32_t spell_id : spellbook_ids) {
            std::ostringstream oss;
            // TODO: Map spell_id to name if you have DBC access
            // oss << "ID: " << spell_id;
            
            // Check cooldown
            int cooldownMs = SpellManager::GetSpellCooldownMs(spell_id);
            
            oss << "ID: " << spell_id;
            if (cooldownMs > 0) {
                oss << " (CD: " << std::fixed << std::setprecision(1) << (cooldownMs / 1000.0) << "s)";
            } else if (cooldownMs == 0) {
                oss << " (Ready)";
            } else { // cooldownMs < 0
                oss << " (Error)";
            }

            ImGui::TextUnformatted(oss.str().c_str());
        }
        ImGui::EndChild();
    } else {
         ImGui::TextUnformatted("Click 'Dump Spellbook' to view spells.");
    }
} 