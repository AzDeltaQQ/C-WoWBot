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

namespace { // Anonymous namespace for constants
    // Address for current target GUID - Should be defined centrally if used elsewhere
    // For now, keeping it local to where it's used.
    constexpr DWORD ADDR_CurrentTargetGUID = 0x00BD07B0; 
    
    // State for the Spells tab
    static int spellIdToCast = 0; 
    static std::vector<uint32_t> spellbook_ids; // Correct type
    static bool spellbook_dumped = false; // Flag to check if dumped
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
        SpellManager::GetInstance().CastSpell(spellIdToCast, targetGuid);
    }
    ImGui::Separator();

    // --- Spellbook Dump Section ---
    if (ImGui::Button("Dump Spellbook IDs")) { 
        LogMessage("GUI::SpellsTab: Dump Spellbook IDs button clicked.");
        try {
            // Use GetSpellbookIDs 
            spellbook_ids = SpellManager::GetInstance().GetSpellbookIDs(); 
            spellbook_dumped = true; 
            LogStream ss;
            ss << "GUI::SpellsTab: Read " << spellbook_ids.size() << " spell IDs from spellbook.";
            LogMessage(ss.str());
        } catch (const std::exception& e) {
             LogStream ssErr;
             ssErr << "GUI::SpellsTab: Exception dumping spellbook IDs: " << e.what();
             LogMessage(ssErr.str());
             spellbook_dumped = false; // Reset flag on error
        } catch (...) {
            LogMessage("GUI::SpellsTab: Unknown exception dumping spellbook IDs.");
            spellbook_dumped = false; // Reset flag on error
        }
    }

    // Display the dumped spellbook if available
    if (spellbook_dumped) {
        ImGui::BeginChild("SpellbookDisplay", ImVec2(0, 200), true); 
        ImGui::Text("Spellbook Contents (%zu entries):", spellbook_ids.size());
        ImGui::Separator();
        // Iterate through the spell IDs vector
        for (const uint32_t& spellId : spellbook_ids) { 
            // Call GetSpellNameByID to get the name
            std::string spellName = SpellManager::GetSpellNameByID(spellId);
            
            // Format the output string to include ID and Name
            ImGui::Text("ID: %u - Name: %s", spellId, spellName.c_str());
        }
        ImGui::EndChild();
    } else {
         ImGui::TextUnformatted("Click 'Dump Spellbook IDs' to view spells.");
    }
} 