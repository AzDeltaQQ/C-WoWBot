#include <Windows.h>
#include "hook.h"
#include "log.h" // Include for logging
#include "SpellManager.h" // Include SpellManager for the patch function

// Thread function for initialization
DWORD WINAPI MainThread(LPVOID lpParam) {
    InitializeLogFile(); // Initialize logging first
    LogMessage("MainThread: Starting initialization...");

    // Apply necessary memory patches early
    SpellManager::PatchCooldownBug_Final(); // Call the final patch function

    if (Hook::Initialize()) { // Call namespaced Initialize
        LogMessage("MainThread: Hook::Initialize succeeded.");
    } else {
        LogMessage("MainThread Error: Hook::Initialize failed.");
        // Consider how to handle failure - maybe unload the DLL?
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule); // Optional: Optimize DLL loading
        // Create a new thread to run our initialization code
        CloseHandle(CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr)); 
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break; // Typically do nothing for thread attach/detach
    case DLL_PROCESS_DETACH:
        Hook::CleanupHook(); // Call namespaced CleanupHook
        break;
    }
    return TRUE;
} 