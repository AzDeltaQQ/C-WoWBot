#pragma once

#include <cstdint> // For uint64_t
#include <iomanip> // For std::hex potentially used with LogStream
#include <vector>  // Added for std::vector return type
#include "functions.h" // For CastLocalPlayerSpellFn
#include "log.h"
#include <string>

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
    std::vector<uint32_t> ReadSpellbook();

    // Internal WoW data structures
    struct SpellInfo {
        uint32_t id;
        char* name;
        char* rank;
        // ... other fields
    };

    // Initialization (if needed in the future)
    static bool Initialize();
    static void Shutdown();
    
    // Main functionality
    static SpellInfo* GetSpellInfo(uint32_t spellId);
    static std::vector<SpellInfo*> GetPlayerSpells();
    static bool CastSpellByID(uint32_t spellId);
    static bool CastSpellByName(const std::string& spellName);

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

private:
    static bool m_initialized;
};