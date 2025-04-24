#include "hook.h"
#include <d3d9.h>
#include "imgui_impl_dx9.h" // Add this include for Reset hook functions
// #include "imgui.h"           // No longer directly needed here
// #include "imgui_impl_win32.h" // No longer directly needed here
#include <MinHook.h>
#include <cstdio> // Include for snprintf
#include "objectmanager.h"
#include "log.h"
#include "functions.h"
#include "gui.h" // Include the main GUI header

#pragma comment(lib, "d3d9.lib")

// Function pointer for the original EndScene
typedef HRESULT(APIENTRY* EndScene_t)(LPDIRECT3DDEVICE9 pDevice);
EndScene_t oEndScene = nullptr;

// Add type and variable for Reset hook
typedef HRESULT(APIENTRY* Reset_t)(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
Reset_t oReset = nullptr;

// Re-add type and variable for GameUISystemShutdown hook
typedef void (__cdecl* GameUISystemShutdown_t)();
GameUISystemShutdown_t oGameUISystemShutdown = nullptr;

// Store function addresses for cleanup
LPVOID EndSceneFunc = nullptr;
LPVOID ResetFunc = nullptr;
LPVOID WorldFrame__RenderFunc = nullptr;
LPVOID GameUISystemShutdownFunc = nullptr; // Re-add address storage

// Hook status and delay logic
bool is_d3d_hooked = false;
bool is_gui_initialized = false;

// Re-add Flag to prevent double cleanup
static bool g_cleanupCalled = false;

// Game addresses (ensure these are correct for your WoW version)
constexpr DWORD ENUM_VISIBLE_OBJECTS_ADDR = 0x004D4B30;
constexpr DWORD GET_OBJECT_PTR_BY_GUID_INNER_ADDR = 0x004D4BB0;

// The hooked EndScene function
HRESULT APIENTRY HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    // If the hook isn't properly set up yet, just call the original (if possible)
    if (!oEndScene) {
        // This state should ideally not be reached if initialization is correct
        LogMessage("HookedEndScene Warning: Called before oEndScene was captured!");
        // Cannot call original, maybe return D3D_OK or an error?
        return D3D_OK; 
    }

    // Perform one-time initialization on the first call after hook is ready
    if (!is_gui_initialized) {
        LogMessage("HookedEndScene: Performing one-time initialization...");
        is_gui_initialized = true; // Set flag immediately to prevent re-entry

        D3DDEVICE_CREATION_PARAMETERS params;
        if (SUCCEEDED(pDevice->GetCreationParameters(&params))) {
            LogMessage("HookedEndScene: Initializing GUI...");
            GUI::Initialize(params.hFocusWindow, pDevice);
            // Check if GUI initialization actually succeeded
            if (GUI::IsInitialized()) { 
                 LogMessage("HookedEndScene: GUI Initialized Successfully.");
                 // Initialize Object Manager only after GUI is confirmed
                 ObjectManager* objMgr = ObjectManager::GetInstance();
                 LogMessage("HookedEndScene: Initializing ObjectManager...");
                 objMgr->Initialize(ENUM_VISIBLE_OBJECTS_ADDR, GET_OBJECT_PTR_BY_GUID_INNER_ADDR);

                 LogMessage("HookedEndScene: Initializing Game Functions...");
                 InitializeFunctions();
                 
                 LogMessage("HookedEndScene: One-time initialization complete.");
            } else {
                LogMessage("HookedEndScene Error: GUI::Initialize failed!");
                // If GUI fails, reset the flag so we might retry (or handle error differently)
                is_gui_initialized = false; 
            }
        } else {
            LogMessage("HookedEndScene Error: GetCreationParameters failed!");
            // If GetCreationParameters fails, reset the flag so we might retry
            is_gui_initialized = false; 
        }
    }

    // Only proceed with per-frame logic if GUI is fully initialized
    if (GUI::IsInitialized()) { // Check the actual GUI state, not our local flag
        // Attempt to finish Object Manager initialization if needed
        ObjectManager* objMgr = ObjectManager::GetInstance();
        if (!objMgr->IsInitialized()) {
            objMgr->TryFinishInitialization(); // This will log success/failure internally
        } else { 
            // Ensure player data is updated frequently if OM is ready
            // Check if player GUID is valid *and* game is in the world state (10)
            uint64_t playerGuid = objMgr->GetLocalPlayerGUID(); // Get the GUID
            DWORD clientState = 0;
            try { // Read client state safely
                clientState = *reinterpret_cast<DWORD*>(CLIENT_STATE_ADDR);
            } catch (...) {
                // LogMessage("[HookedEndScene] EXCEPTION reading client state!"); // Optional log
                clientState = 0; // Assume invalid state on error
            }

            if (playerGuid != 0 && clientState == 10) { // Check BOTH conditions
                // LogMessage("[HookedEndScene] Player GUID valid and ClientState is 10. Attempting update..."); // Optional log
                auto player = objMgr->GetLocalPlayer();
                if (player) {
                    // LogMessage("[HookedEndScene] Player object obtained. Calling UpdateDynamicData..."); // Keep existing log
                    try { // Add try-catch
                        player->UpdateDynamicData(); // Force update every frame for player
                        // LogMessage("[HookedEndScene] UpdateDynamicData called successfully."); // Keep existing log
                    } catch (const std::exception& e) {
                        LogStream errLog;
                        errLog << "[HookedEndScene] EXCEPTION calling player->UpdateDynamicData(): " << e.what();
                        LogMessage(errLog.str());
                    } catch (...) {
                        LogMessage("[HookedEndScene] UNKNOWN EXCEPTION calling player->UpdateDynamicData()");
                    }
                } else {
                    // LogMessage("[HookedEndScene] GetLocalPlayer() returned nullptr even with valid GUID/State."); // Optional log
                }
            } else {
                 // LogMessage("[HookedEndScene] Skipping player update (GUID=" + std::to_string(playerGuid) + ", State=" + std::to_string(clientState) + ")"); // Optional log
            }
        }

        // Toggle GUI visibility
        if (GetAsyncKeyState(VK_INSERT) & 1) {
            GUI::ToggleVisibility();
        }

        // Render the GUI if it's visible
        GUI::Render(); // Render handles visibility check internally now
    }

    // ALWAYS call the original EndScene function at the end
    return oEndScene(pDevice);
}

// Re-add Detour function for GameUISystemShutdown (Order will be changed later)
void __cdecl HookedGameUISystemShutdown() {
    LogMessage("[Hook] HookedGameUISystemShutdown called.");
    
    // ---- CORRECTED ORDER ----
    // Call the original game function FIRST
    LogMessage("[Hook] Calling original GameUISystemShutdown...");
    if (oGameUISystemShutdown) {
        try {
             oGameUISystemShutdown();
             LogMessage("[Hook] Original GameUISystemShutdown finished.");
        } catch (const std::exception& e) {
             LogMessage(std::string("[Hook] EXCEPTION during original GameUISystemShutdown: ") + e.what());
        } catch (...) {
             LogMessage("[Hook] UNKNOWN EXCEPTION during original GameUISystemShutdown.");
        }
    } else {
        LogMessage("[Hook] Error: oGameUISystemShutdown is NULL! Cannot call original.");
    }

    // Call our cleanup routine AFTER the original function has executed
    // CleanupHook itself will check g_cleanupCalled
    LogMessage("[Hook] Proceeding to Hook::CleanupHook...");
    Hook::CleanupHook(); 
    LogMessage("[Hook] Hook::CleanupHook finished.");
    // ---- END CORRECTED ORDER ----
}

// Uncomment and implement the Reset hook handler
HRESULT APIENTRY HookedReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    // Include header here just in case
    #include "imgui_impl_dx9.h"
    LogMessage("HookedReset: Called."); // Log entry

    // Invalidate ImGui device objects BEFORE calling the original Reset
    if (GUI::IsInitialized()) { // Check if GUI was ever initialized
        LogMessage("HookedReset: Invalidating ImGui device objects...");
        ImGui_ImplDX9_InvalidateDeviceObjects();
    } else {
         LogMessage("HookedReset: GUI not initialized, skipping Invalidate.");
    }
    
    // Call the original Reset function
    HRESULT result = E_FAIL; // Default to failure
    if (oReset) {
        LogMessage("HookedReset: Calling original Reset...");
        result = oReset(pDevice, pPresentationParameters);
        char resetResultMsg[128];
        snprintf(resetResultMsg, sizeof(resetResultMsg), "HookedReset: Original Reset returned 0x%lX", result);
        LogMessage(resetResultMsg);
    } else {
         LogMessage("HookedReset Error: Original Reset function pointer (oReset) is null!");
    }
    
    // Recreate ImGui device objects AFTER the original Reset succeeds
    if (SUCCEEDED(result)) {
        if (GUI::IsInitialized()) { // Check again in case shutdown occurred
             LogMessage("HookedReset: Reset succeeded. Recreating ImGui device objects...");
             ImGui_ImplDX9_CreateDeviceObjects();
        } else {
             LogMessage("HookedReset: Reset succeeded, but GUI not initialized, skipping Create.");
        }
    } else {
         LogMessage("HookedReset: Reset failed, ImGui objects not recreated.");
    }

    return result;
}

// --- Hook Management --- 

namespace Hook {

    // Helper function to create a minimal temporary window
    HWND CreateTemporaryWindow() {
        WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, DefWindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, "TempD3DWindowClass", NULL };
        RegisterClassEx(&wc);
        HWND hwnd = CreateWindow("TempD3DWindowClass", NULL, WS_OVERLAPPEDWINDOW, 100, 100, 300, 300, GetDesktopWindow(), NULL, wc.hInstance, NULL);
        // Don't ShowWindow(hwnd, SW_HIDE); // Keep it hidden by default
        return hwnd;
    }

    bool Initialize() {
        LogMessage("Hook::Initialize: Initializing MinHook...");
        if (MH_Initialize() != MH_OK) {
            LogMessage("Hook::Initialize Error: MH_Initialize failed!");
            return false;
        }
        LogMessage("Hook::Initialize: MinHook Initialized.");

        LogMessage("Hook::Initialize: Finding EndScene address...");
        // Find the address of EndScene dynamically
        // Create a dummy device to get the VTable
        LogMessage("Hook::Initialize: Creating D3D9 object...");
        IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
        if (!pD3D) {
            LogMessage("Hook::Initialize Error: Direct3DCreate9 failed!");
            MH_Uninitialize();
            return false;
        }
        LogMessage("Hook::Initialize: D3D9 object created.");

        LogMessage("Hook::Initialize: Creating temporary window...");
        HWND tempHwnd = CreateTemporaryWindow();
        if (!tempHwnd) {
             LogMessage("Hook::Initialize Error: Failed to create temporary window!");
             pD3D->Release();
             MH_Uninitialize();
             return false;
        }
        LogMessage("Hook::Initialize: Temporary window created.");

        D3DPRESENT_PARAMETERS d3dpp = {};
        d3dpp.Windowed = TRUE;
        d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        d3dpp.hDeviceWindow = tempHwnd; 
        d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; // Avoid vsync issues

        LogMessage("Hook::Initialize: Attempting to create dummy D3D device...");
        IDirect3DDevice9* pDummyDevice = nullptr;
        HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow,
                                    D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDummyDevice);
        
        LogMessage("Hook::Initialize: Releasing D3D9 object...");
        pD3D->Release(); // Release the D3D object
        LogMessage("Hook::Initialize: Destroying temporary window...");
        DestroyWindow(tempHwnd); // Clean up the temporary window
        UnregisterClass("TempD3DWindowClass", GetModuleHandle(NULL));
        LogMessage("Hook::Initialize: Temporary window destroyed.");

        if (FAILED(hr)) { // Check HRESULT first
            char errorMsg[256];
            snprintf(errorMsg, sizeof(errorMsg), "Hook::Initialize Error: CreateDevice failed! HRESULT: 0x%lX", hr);
            LogMessage(errorMsg);
            MH_Uninitialize();
            return false;
        }
        
        if (!pDummyDevice) { // Explicitly check if the device pointer is NULL
             char errorMsg[256];
             snprintf(errorMsg, sizeof(errorMsg), "Hook::Initialize Error: CreateDevice succeeded (hr=0x%lX) but returned NULL device pointer!", hr);
             LogMessage(errorMsg); 
             MH_Uninitialize();
             return false;
        }
        LogMessage("Hook::Initialize: Dummy device created successfully.");

        // Get the VTable (adjust index if needed - 42 is common for EndScene)
        LogMessage("Hook::Initialize: Getting VTable...");
        void** vTable = *reinterpret_cast<void***>(pDummyDevice);
        LogMessage("Hook::Initialize: Releasing dummy device...");
        pDummyDevice->Release(); // Release the dummy device
        LogMessage("Hook::Initialize: Dummy device released.");

        if (!vTable) { // Check if vTable pointer itself is null
            LogMessage("Hook::Initialize Error: Failed to get VTable (NULL pointer)!");
            MH_Uninitialize();
            return false;
        }
        LogMessage("Hook::Initialize: VTable pointer obtained.");

        if (!vTable[42]) { // Check if the specific VTable entry is valid
            LogMessage("Hook::Initialize Error: VTable entry for EndScene (index 42) is NULL!");
            MH_Uninitialize();
            return false;
        }
        LogMessage("Hook::Initialize: VTable entry 42 seems valid.");
        
        LPVOID endSceneAddress = vTable[42];
        char addrMsg[128];
        snprintf(addrMsg, sizeof(addrMsg), "Hook::Initialize: Found EndScene at address 0x%p", endSceneAddress);
        LogMessage(addrMsg);
        
        // Store the function address for later cleanup
        EndSceneFunc = endSceneAddress;

        // --- Add Reset Hook ---
        LogMessage("Hook::Initialize: Getting Reset address (VTable index 16)...");
        if (!vTable[16]) { // Check VTable entry for Reset (index 16)
            LogMessage("Hook::Initialize Error: VTable entry for Reset (index 16) is NULL!");
            // Cleanup EndScene hook if Reset fails
            MH_RemoveHook(EndSceneFunc); 
            MH_Uninitialize();
            return false;
        }
        LogMessage("Hook::Initialize: VTable entry 16 seems valid.");

        LPVOID resetAddress = vTable[16];
        snprintf(addrMsg, sizeof(addrMsg), "Hook::Initialize: Found Reset at address 0x%p", resetAddress);
        LogMessage(addrMsg);
        ResetFunc = resetAddress; // Store for cleanup

        LogMessage("Hook::Initialize: Creating Reset hook...");
        // Create the hook for Reset
        if (MH_CreateHook(ResetFunc, &HookedReset, reinterpret_cast<LPVOID*>(&oReset)) != MH_OK) {
            LogMessage("Hook::Initialize Error: MH_CreateHook for Reset failed!");
            MH_RemoveHook(EndSceneFunc); // Cleanup EndScene hook
            MH_Uninitialize();
            return false;
        }
        LogMessage("Hook::Initialize: Reset Hook Created.");
        // --- End Reset Hook ---

        LogMessage("Hook::Initialize: Creating EndScene hook...");
        // Create the hook for EndScene (Target, Detour, Original)
        if (MH_CreateHook(endSceneAddress, &HookedEndScene, reinterpret_cast<LPVOID*>(&oEndScene)) != MH_OK) {
            LogMessage("Hook::Initialize Error: MH_CreateHook failed!");
            MH_Uninitialize();
            return false;
        }
        LogMessage("Hook::Initialize: EndScene Hook Created.");

        LogMessage("Hook::Initialize: Enabling EndScene hook...");
        // Enable the hook
        if (MH_EnableHook(endSceneAddress) != MH_OK) { // Use the specific address for enabling
            LogMessage("Hook::Initialize Error: MH_EnableHook failed!");
            MH_RemoveHook(endSceneAddress); // Attempt to remove the hook on failure
            MH_RemoveHook(ResetFunc); // Attempt to remove Reset hook too
            MH_Uninitialize();
            return false;
        }
        is_d3d_hooked = true;
        LogMessage("Hook::Initialize: EndScene Hook Enabled.");

        // --- Enable Reset Hook ---
        LogMessage("Hook::Initialize: Enabling Reset hook...");
        if (MH_EnableHook(ResetFunc) != MH_OK) {
            LogMessage("Hook::Initialize Error: MH_EnableHook for Reset failed!");
            MH_DisableHook(EndSceneFunc); // Disable EndScene hook if Reset enable fails
            MH_RemoveHook(EndSceneFunc);
            MH_RemoveHook(ResetFunc);
            MH_Uninitialize();
            return false;
        }
        LogMessage("Hook::Initialize: Reset Hook Enabled.");
        // --- End Enable Reset Hook ---

        // --- Re-add GameUISystemShutdown Hook ---
        LogMessage("Hook::Initialize: Creating GameUISystemShutdown hook (Address: 0x00529160)...");
        GameUISystemShutdownFunc = (LPVOID)0x00529160;
        if (MH_CreateHook(GameUISystemShutdownFunc, &HookedGameUISystemShutdown, reinterpret_cast<LPVOID*>(&oGameUISystemShutdown)) != MH_OK) {
            LogMessage("Hook::Initialize Error: MH_CreateHook for GameUISystemShutdown failed!");
            // Cleanup other hooks
            MH_DisableHook(EndSceneFunc);
            MH_DisableHook(ResetFunc);
            MH_RemoveHook(EndSceneFunc);
            MH_RemoveHook(ResetFunc);
            MH_Uninitialize();
            return false;
        }
        LogMessage("Hook::Initialize: GameUISystemShutdown Hook Created.");

        LogMessage("Hook::Initialize: Enabling GameUISystemShutdown hook...");
        if (MH_EnableHook(GameUISystemShutdownFunc) != MH_OK) {
            LogMessage("Hook::Initialize Error: MH_EnableHook for GameUISystemShutdown failed!");
            // Cleanup other hooks
            MH_DisableHook(EndSceneFunc);
            MH_DisableHook(ResetFunc);
            MH_RemoveHook(EndSceneFunc);
            MH_RemoveHook(ResetFunc);
            MH_RemoveHook(GameUISystemShutdownFunc); // Remove this one too
            MH_Uninitialize();
            return false;
        }
        LogMessage("Hook::Initialize: GameUISystemShutdown Hook Enabled.");
        // --- End GameUISystemShutdown Hook ---

        LogMessage("Hook::Initialize: D3D Hook Initialization Successful.");
        return true;
    }

    void CleanupHook() {
        // Re-add cleanup flag check 
        if (g_cleanupCalled) {
            LogMessage("[Hook] CleanupHook: Already called, skipping duplicate run.");
            return;
        }
        g_cleanupCalled = true; // Set flag immediately
        
        LogMessage("[Hook] CleanupHook: Starting cleanup process...");

        // Shutdown GUI first
        LogMessage("[Hook] CleanupHook: Shutting down GUI...");
        GUI::Shutdown();
        LogMessage("[Hook] CleanupHook: GUI shutdown completed.");

        // --- Add ObjectManager Shutdown --- 
        LogMessage("[Hook] CleanupHook: Shutting down ObjectManager...");
        ObjectManager::Shutdown(); // Call the static shutdown method
        LogMessage("[Hook] CleanupHook: ObjectManager shutdown completed.");
        // --- End ObjectManager Shutdown ---

        // Disable hooks
        LogMessage("[Hook] CleanupHook: Disabling WorldFrame::Render hook...");
        if (WorldFrame__RenderFunc) {
            MH_DisableHook(WorldFrame__RenderFunc);
            MH_RemoveHook(WorldFrame__RenderFunc);
        }
        LogMessage("[Hook] CleanupHook: WorldFrame::Render hook disabled.");

        LogMessage("[Hook] CleanupHook: Disabling EndScene hook...");
        if (EndSceneFunc) {
            MH_DisableHook(EndSceneFunc);
            MH_RemoveHook(EndSceneFunc);
        }
        LogMessage("[Hook] CleanupHook: EndScene hook disabled.");

        LogMessage("[Hook] CleanupHook: Disabling Reset hook...");
        if (ResetFunc) {
            MH_DisableHook(ResetFunc);
            MH_RemoveHook(ResetFunc);
        }
        LogMessage("[Hook] CleanupHook: Reset hook disabled.");

        // --- Re-add Disable GameUISystemShutdown Hook --- 
        LogMessage("[Hook] CleanupHook: Disabling GameUISystemShutdown hook...");
        if (GameUISystemShutdownFunc) {
            MH_DisableHook(GameUISystemShutdownFunc);
            MH_RemoveHook(GameUISystemShutdownFunc); // Also remove it
        }
        LogMessage("[Hook] CleanupHook: GameUISystemShutdown hook disabled.");
        // --- End Disable --- 

        // Reset global states
        oEndScene = nullptr;
        oReset = nullptr; // Reset the Reset pointer too
        is_d3d_hooked = false;
        is_gui_initialized = false;

        // Uninitialize MinHook
        LogMessage("[Hook] CleanupHook: Uninitializing MinHook...");
        MH_Uninitialize();
        LogMessage("[Hook] CleanupHook: MinHook uninitialized.");

        // Log final cleanup message BEFORE closing the log file
        LogMessage("[Hook] CleanupHook: Cleanup complete. Process should terminate normally now.");

        // Ensure all log messages are flushed to disk and close the file LAST
        ShutdownLogFile(); 
    }

} // namespace Hook 