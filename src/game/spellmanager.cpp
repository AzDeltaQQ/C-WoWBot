#include "spellmanager.h"
#include "functions.h" // Required for the actual CastLocalPlayerSpell function pointer
#include "log.h"
#include <stdexcept> // For std::runtime_error

// Initialize singleton instance pointer
SpellManager* SpellManager::m_instance = nullptr;

// Private constructor
SpellManager::SpellManager() {
    // Initialization logic for SpellManager, if any, goes here.
    // Currently, it doesn't need much, relies on Functions::InitializeFunctions being called elsewhere.
}

// Get singleton instance
SpellManager& SpellManager::GetInstance() {
    if (m_instance == nullptr) {
        m_instance = new SpellManager();
    }
    return *m_instance;
}

// CastSpell implementation
bool SpellManager::CastSpell(int spellId, uint64_t targetGuid, int unknownIntArg1, char unknownCharArg) {
    // Ensure the function pointer is initialized
    if (::CastLocalPlayerSpell == nullptr) {
        LogMessage("SpellManager::CastSpell Error: CastLocalPlayerSpell function pointer is not initialized!");
        LogMessage("Ensure InitializeFunctions() has been called successfully.");
        return false;
    }

    LogStream ss;
    ss << "SpellManager::CastSpell: Attempting to cast SpellID=" << spellId 
       << " on TargetGUID=0x" << std::hex << targetGuid 
       << " (Args: " << unknownIntArg1 << ", '" << unknownCharArg << "')";
    LogMessage(ss.str());

    try {
        // Call the actual game function
        // The return value is 'char' according to the disassembly, potentially indicating success/failure or some state.
        char result = ::CastLocalPlayerSpell(spellId, unknownIntArg1, targetGuid, unknownCharArg);
        
        LogStream ssResult;
        ssResult << "SpellManager::CastSpell: CastLocalPlayerSpell returned '" << result << "' (char code: " << static_cast<int>(result) << ")";
        LogMessage(ssResult.str());
        
        // We might need to interpret the result code based on observation
        // For now, let's assume non-zero might mean success or queued, zero might mean failure.
        // This is a guess and might need refinement.
        return result != 0; 
    } catch (const std::exception& e) {
        LogStream ssErr;
        ssErr << "SpellManager::CastSpell: Exception caught while calling CastLocalPlayerSpell - " << e.what();
        LogMessage(ssErr.str());
        return false;
    } catch (...) {
        LogMessage("SpellManager::CastSpell: Unknown exception caught while calling CastLocalPlayerSpell.");
        return false;
    }
} 