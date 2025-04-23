#pragma once

#include <cstdint> // For uint64_t
#include <iomanip> // For std::hex potentially used with LogStream
#include "functions.h" // For CastLocalPlayerSpellFn
#include "log.h"

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

    // Initialization (if needed in the future)
    // bool Initialize(); 
}; 