#include <Windows.h>
#include "hook.h"
#include "log.h"
#include "SpellManager.h"

// Thread function for initialization
DWORD WINAPI MainThread(LPVOID lpParam) {
    InitializeLogFile();
    LogMessage("MainThread: Starting initialization...");

    // Apply necessary memory patches early
    SpellManager::PatchCooldownBug_Final();

    if (Hook::Initialize()) {
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
        DisableThreadLibraryCalls(hModule);
        // Create a new thread to run our initialization code
        CloseHandle(CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr)); 
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        Hook::CleanupHook();
        break;
    }
    return TRUE;
} 