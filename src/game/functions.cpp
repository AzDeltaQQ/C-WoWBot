#include "functions.h"
#include <windows.h> // Keep for potential future use?
#include "log.h" // Include for logging

// Define the actual function pointer
CastLocalPlayerSpellFn CastLocalPlayerSpell = nullptr;

// Function to initialize all function pointers
void InitializeFunctions() {
    // User instructed to use the offset as the ABSOLUTE address
    DWORD absoluteAddr = OFF_CastLocalPlayerSpell; 
    
    // Log the address being used
    LogStream ssInit;
    ssInit << "InitializeFunctions: Using ABSOLUTE address for CastLocalPlayerSpell: 0x" << std::hex << absoluteAddr;
    LogMessage(ssInit.str());

    // Initialize CastLocalPlayerSpell with the absolute address
    CastLocalPlayerSpell = (CastLocalPlayerSpellFn)absoluteAddr;

    // Log the final pointer value (should match the absolute address)
    LogStream ssFunc;
    ssFunc << "  CastLocalPlayerSpell pointer set to: 0x" << std::hex << (uintptr_t)CastLocalPlayerSpell;
    LogMessage(ssFunc.str());

    // Warn about potential issues based on previous crash
    LogMessage("Warning: Using absolute address based on user instruction. Previous crash indicated this might be incorrect.");

} 