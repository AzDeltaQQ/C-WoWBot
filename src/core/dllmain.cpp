#include <Windows.h>
#include "hook.h"
#include "log.h"
#include "SpellManager.h"
#include "ObjectManager.h"
#include "../bot/core/BotController.h"
#include "../gui/gui.h"
#include <memory>

// Global unique_ptr for the BotController instance
std::unique_ptr<BotController> g_botControllerInstance;

// Thread function for initialization
DWORD WINAPI MainThread(LPVOID lpParam) {
    InitializeLogFile();
    LogMessage("MainThread: Starting initialization...");

    // Apply necessary memory patches early
    SpellManager::PatchCooldownBug_Final();

    if (Hook::Initialize()) {
        LogMessage("MainThread: Hook::Initialize succeeded.");

        // --- BotController Initialization ---
        LogMessage("MainThread: Initializing BotController...");
        g_botControllerInstance = std::make_unique<BotController>();
        GUI::g_BotController = g_botControllerInstance.get(); // Assign to GUI pointer

        // Get singleton instances of managers
        ObjectManager* objMgr = ObjectManager::GetInstance(); // Use singleton getter
        SpellManager* spellMgr = &SpellManager::GetInstance(); // Use singleton getter, take address for pointer

        if (GUI::g_BotController && objMgr && spellMgr) {
            // Ensure managers are initialized (optional check, depends on your design)
            // if (objMgr->IsInitialized() && spellMgr->IsInitialized()) { 
            //     GUI::g_BotController->initialize(objMgr, spellMgr);
            //     LogMessage("MainThread: BotController initialized successfully.");
            // } else {
            //     LogMessage("MainThread Error: Failed to initialize BotController (managers not ready).");
            //     GUI::g_BotController = nullptr;
            //     g_botControllerInstance.reset();
            // }
            
            // Assuming managers are ready after Hook::Initialize()
            GUI::g_BotController->initialize(objMgr, spellMgr);
            LogMessage("MainThread: BotController initialized successfully.");

        } else {
             LogMessage("MainThread Error: Failed to initialize BotController (GetInstance returned null?).");
             GUI::g_BotController = nullptr; // Ensure GUI knows it's not valid
             g_botControllerInstance.reset(); // Clean up partial instance
        }
        // ------------------------------------

    } else {
        LogMessage("MainThread Error: Hook::Initialize failed.");
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        CloseHandle(CreateThread(nullptr, 0, MainThread, hModule, 0, nullptr)); 
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    case DLL_PROCESS_DETACH:
        LogMessage("DllMain: DLL_PROCESS_DETACH received.");
        Hook::CleanupHook();
        g_botControllerInstance.reset();
        LogMessage("DllMain: BotController instance reset.");
        break;
    }
    return TRUE;
} 