#pragma once

#include <cstdint> // For uint64_t
#include <iomanip> // For std::hex potentially used with LogStream
#include <vector>  // Added for std::vector return type
#include "functions.h" // For CastLocalPlayerSpellFn
#include "log.h"
#include <string>
// REMOVED: #include "spell_info.h" // Will be simplified later

class SpellManager {
private:
    // Singleton instance
    static SpellManager* m_instance;

    // Private constructor for singleton
    SpellManager();

public:
    // Delete copy constructor and assignment operator
    SpellManager(const SpellManager&) = delete;
    SpellManager& operator=(const SpellManager&) = delete;

    // Get singleton instance
    static SpellManager& GetInstance();

    // Function to cast a spell using the local player
    // targetGuid = 0 means self-cast or no target needed
    // unknownIntArg1 and unknownCharArg based on assembly, default to 0
    bool CastSpell(int spellId, uint64_t targetGuid = 0, int unknownIntArg1 = 0, char unknownCharArg = 0);

    // Function to read the spellbook from game memory
    static std::vector<uint32_t> ReadSpellbook();

    // Initialization (if needed in the future)
    // static bool Initialize();
    // static void Shutdown();
    
    // --- REMOVED Spell Info Methods ---
    // static SpellInfo GetSpellInfo(uint32_t spellId);
    // static bool GetSpellInfoRaw(uint32_t spellId, char* outputBuffer);
    // static std::vector<SpellInfo> GetPlayerSpells(); // Changed to GetSpellbookIDs
    static std::vector<uint32_t> GetSpellbookIDs(); // Renamed for clarity
    // --------------------------------
    
    // --- NEW ---
    /**
     * @brief Retrieves the name of a spell using its ID via direct memory access.
     * Uses DBC offset 0x220.
     */
    static std::string GetSpellNameByID(uint32_t spellId);

    /**
     * @brief Retrieves the primary description of a spell using its ID via direct memory access.
     * Uses DBC offset 0x2A8 (relative to string table base). May contain format codes ($s1 etc).
     */
    static std::string GetSpellDescriptionByID(uint32_t spellId);

    /**
     * @brief Retrieves the tooltip description of a spell using its ID via direct memory access.
     * Uses DBC offset 0x2EC (relative to string table base). May contain format codes ($s1 etc). May be empty.
     */
    static std::string GetSpellTooltipByID(uint32_t spellId);

    // Remove or comment out the old GetSpellDescriptionByID and GetSpellTooltipByID
    // static std::string GetSpellDescriptionByID(uint32_t spellId); // Uses offset 0x2A8 - unreliable
    // static std::string GetSpellTooltipByID(uint32_t spellId); // Uses offset 0x2EC - usually empty
    // ---------
    
    // REMOVED: static bool CastSpellByID(uint32_t spellId);
    // REMOVED: static bool CastSpellByName(const std::string& spellName);

    // Cooldown Checks
    /**
     * @brief Gets the remaining cooldown for a player spell in milliseconds.
     *
     * Calls the game's internal get_spell_cooldown_proxy function (0x809000).
     *
     * @param spellId The ID of the spell to check.
     * @return The remaining cooldown in milliseconds, or 0 if the spell is ready.
     *         Returns -1 if an error occurs during the function call.
     */
    static int GetSpellCooldownMs(int spellId);

    /**
     * @brief Gets the remaining cooldown for a pet spell in milliseconds.
     *
     * Calls the game's internal get_spell_cooldown_proxy function (0x809000).
     *
     * @param spellId The ID of the pet spell to check.
     * @return The remaining cooldown in milliseconds, or 0 if the spell is ready.
     *         Returns -1 if an error occurs during the function call.
     */
    static int GetPetSpellCooldownMs(int spellId);

    // Function to apply the memory patch for the cooldown display bug.
    // Should be called once during initialization.
    static void PatchCooldownBug_Final();

// private:
    // static bool m_initialized; // Uncomment if Initialize/Shutdown are used
};