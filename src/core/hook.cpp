#include "hook.h"
#include <d3d9.h>
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <MinHook.h>
#include <cstdio>
#include "objectmanager.h"
#include "log.h"
#include "functions.h"
#include "gui.h"
#include "wowobject.h"
#include "BotController.h"

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
LPVOID GameUISystemShutdownFunc = nullptr;

// Hook status and delay logic
bool is_d3d_hooked = false;
bool is_gui_initialized = false;

// Re-add Flag to prevent double cleanup
static bool g_cleanupCalled = false;

// Flag to signal Win32 backend re-initialization is needed after reset
static bool g_needsWin32Reinit = false;

// Game addresses
constexpr DWORD ENUM_VISIBLE_OBJECTS_ADDR = 0x004D4B30;
constexpr DWORD GET_OBJECT_PTR_BY_GUID_INNER_ADDR = 0x004D4BB0;

// Change the extern declaration to specify the GUI namespace
namespace GUI {
    extern BotController* g_BotController; // Declare it within the GUI namespace
}

// The hooked EndScene function
HRESULT APIENTRY HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    if (!oEndScene) {
        LogMessage("HookedEndScene Warning: Called before oEndScene was captured!");
        return D3D_OK;
    }

    if (!is_gui_initialized) {
        LogMessage("HookedEndScene: Performing one-time initialization...");
        is_gui_initialized = true;

        D3DDEVICE_CREATION_PARAMETERS params;
        if (SUCCEEDED(pDevice->GetCreationParameters(&params))) {
            LogMessage("HookedEndScene: Initializing GUI...");
            GUI::Initialize(params.hFocusWindow, pDevice);
            if (GUI::IsInitialized()) {
                 LogMessage("HookedEndScene: GUI Initialized Successfully.");
                 ObjectManager* objMgr = ObjectManager::GetInstance();
                 LogMessage("HookedEndScene: Initializing ObjectManager...");
                 objMgr->Initialize(ENUM_VISIBLE_OBJECTS_ADDR, GET_OBJECT_PTR_BY_GUID_INNER_ADDR);

                 LogMessage("HookedEndScene: Initializing Game Functions...");
                 InitializeFunctions();

                 LogMessage("HookedEndScene: One-time initialization complete.");
            } else {
                LogMessage("HookedEndScene Error: GUI::Initialize failed!");
                is_gui_initialized = false;
            }
        } else {
            LogMessage("HookedEndScene Error: GetCreationParameters failed!");
            is_gui_initialized = false;
        }
    }

    if (GUI::IsInitialized()) {
        ObjectManager* objMgr = ObjectManager::GetInstance();

        HRESULT coopLevel = pDevice->TestCooperativeLevel();
        if (coopLevel != D3D_OK) {
            if (coopLevel == D3DERR_DEVICELOST) {
                // Device lost, needs reset. Don't render anything.
                return oEndScene(pDevice);
            } else if (coopLevel == D3DERR_DEVICENOTRESET) {
                // Device needs reset, signal for re-initialization after reset.
                g_needsWin32Reinit = true;
                // Still call original EndScene? Yes, likely needed for game loop.
                 return oEndScene(pDevice);
            } else {
                // Some other error, maybe try calling original EndScene.
                 return oEndScene(pDevice);
            }
        } else if (g_needsWin32Reinit) {
            // Device is OK now, and we previously needed re-initialization (after D3DERR_DEVICENOTRESET)
            LogMessage("HookedEndScene: Device OK. Performing delayed Win32 re-initialization...");

            HWND currentHwnd = nullptr;
            D3DDEVICE_CREATION_PARAMETERS params;
            if (SUCCEEDED(pDevice->GetCreationParameters(&params))) {
                currentHwnd = params.hFocusWindow;
            }

            if (currentHwnd) {
                LogMessage("HookedEndScene: Re-initializing Win32 backend and WndProc hook...");
                ImGui_ImplWin32_Shutdown();
                ImGui_ImplWin32_Init(currentHwnd);
                GUI::oWndProc = (WNDPROC)SetWindowLongPtr(currentHwnd, GWLP_WNDPROC, (LONG_PTR)GUI::WndProc);
                 if (!GUI::oWndProc) {
                    LogMessage("HookedEndScene Error: Failed to re-hook WndProc after reset!");
                 } else {
                    LogMessage("HookedEndScene: Successfully re-initialized Win32 and re-hooked WndProc.");
                    GUI::g_hWnd = currentHwnd;
                 }
            } else {
                LogMessage("HookedEndScene Error: Failed to get current HWND for re-initialization.");
            }

            g_needsWin32Reinit = false;
        }

        // --- Restore Player Update Logic --- 
        if (!objMgr->IsInitialized()) {
            objMgr->TryFinishInitialization(); // Keep trying to initialize if not ready
        } else {
            // ObjectManager is initialized, update the entire cache first
            try {
                objMgr->Update(); 
            } catch (const std::exception& e) {
                LogStream errLog;
                errLog << "[HookedEndScene] EXCEPTION calling objMgr->Update(): " << e.what();
                LogMessage(errLog.str());
            } catch (...) {
                 LogMessage("[HookedEndScene] UNKNOWN EXCEPTION calling objMgr->Update()");
            }

            // --- ADD BotController::run() CALL HERE ---
            if (GUI::g_BotController) {
                try {
                    GUI::g_BotController->run(); // Run the bot controller's main thread logic
                } catch (const std::exception& e) {
                    LogStream errLog;
                    errLog << "[HookedEndScene] EXCEPTION calling GUI::g_BotController->run(): " << e.what();
                    LogMessage(errLog.str());
                } catch (...) {
                    LogMessage("[HookedEndScene] UNKNOWN EXCEPTION calling GUI::g_BotController->run()");
                }
            }
            // --- END BotController::run() CALL ---

            // Now proceed with player-specific updates if needed (or remove if Update handles it)
            uint64_t playerGuid = objMgr->GetLocalPlayerGUID();
            DWORD clientState = 0;
            try {
                clientState = *reinterpret_cast<DWORD*>(CLIENT_STATE_ADDR);
            } catch (...) {
                clientState = 0; // Default or handle error
            }

            // Check if player is valid and in the world (clientState == 10 typically means 'loaded into world')
            if (playerGuid != 0 && clientState == 10) { 
                auto player = objMgr->GetLocalPlayer(); // Get player object again
                if (player) {
                    try {
                        player->UpdateDynamicData(); // Update player data like position, health etc.
                    } catch (const std::exception& e) {
                        LogStream errLog;
                        errLog << "[HookedEndScene] EXCEPTION calling player->UpdateDynamicData(): " << e.what();
                        LogMessage(errLog.str());
                    } catch (...) {
                        LogMessage("[HookedEndScene] UNKNOWN EXCEPTION calling player->UpdateDynamicData()");
                    }
                }
            }
        }
        // --- End Player Update Logic ---

        if (GetAsyncKeyState(VK_INSERT) & 1) {
            GUI::ToggleVisibility();
        }

        GUI::Render();
    }

    return oEndScene(pDevice);
}

// Re-add Detour function for GameUISystemShutdown
void __cdecl HookedGameUISystemShutdown() {
    LogMessage("[Hook] HookedGameUISystemShutdown called.");

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

    LogMessage("[Hook] Proceeding to Hook::CleanupHook...");
    Hook::CleanupHook();
    LogMessage("[Hook] Hook::CleanupHook finished.");
}

HRESULT APIENTRY HookedReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    if (GUI::IsInitialized()) {
        LogMessage("HookedReset: Invalidating ImGui device objects...");
        ImGui_ImplDX9_InvalidateDeviceObjects();
    } else {
         LogMessage("HookedReset: GUI not initialized, skipping Invalidate.");
    }

    HRESULT result = E_FAIL;
    if (oReset) {
        LogMessage("HookedReset: Calling original Reset...");
        result = oReset(pDevice, pPresentationParameters);
        char resetResultMsg[128];
        snprintf(resetResultMsg, sizeof(resetResultMsg), "HookedReset: Original Reset returned 0x%lX", result);
        LogMessage(resetResultMsg);
    } else {
         LogMessage("HookedReset Error: Original Reset function pointer (oReset) is null!");
    }

    if (SUCCEEDED(result)) {
        if (GUI::IsInitialized()) {
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

        D3DPRESENT_PARAMETERS pp = {};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = tempHwnd;
        pp.BackBufferFormat = D3DFMT_UNKNOWN;

        LogMessage("Hook::Initialize: Attempting to create dummy D3D device...");
        IDirect3DDevice9* pDummyDevice = nullptr;
        HRESULT hr = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, tempHwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &pDummyDevice);

        LogMessage("Hook::Initialize: Releasing D3D9 object...");
        pD3D->Release();
        LogMessage("Hook::Initialize: Destroying temporary window...");
        DestroyWindow(tempHwnd);
        UnregisterClass("TempD3DWindowClass", GetModuleHandle(NULL));
        LogMessage("Hook::Initialize: Temporary window destroyed.");

        if (FAILED(hr)) {
            char errorMsg[256];
            snprintf(errorMsg, sizeof(errorMsg), "Hook::Initialize Error: CreateDevice failed! HRESULT: 0x%lX", hr);
            LogMessage(errorMsg);
            return false;
        }
        LogMessage("Hook::Initialize: Dummy device created successfully.");

        LogMessage("Hook::Initialize: Getting VTable...");
        DWORD* pVTable = *(DWORD**)pDummyDevice;
        if (!pVTable) {
             LogMessage("Hook::Initialize Error: Failed to get VTable pointer!");
             pDummyDevice->Release();
             return false;
        }
        LogMessage("Hook::Initialize: VTable pointer obtained.");

        EndSceneFunc = (LPVOID)pVTable[42];
        ResetFunc = (LPVOID)pVTable[16];

        if (!EndSceneFunc) {
            LogMessage("Hook::Initialize Error: VTable entry 42 (EndScene) is NULL!");
            pDummyDevice->Release();
            return false;
        }
        LogMessage("Hook::Initialize: VTable entry 42 seems valid.");
        char endSceneMsg[64];
        snprintf(endSceneMsg, sizeof(endSceneMsg), "Hook::Initialize: Found EndScene at address 0x%p", EndSceneFunc);
        LogMessage(endSceneMsg);

        if (!ResetFunc) {
            LogMessage("Hook::Initialize Error: VTable entry 16 (Reset) is NULL!");
            pDummyDevice->Release();
            return false;
        }
        LogMessage("Hook::Initialize: VTable entry 16 seems valid.");
        char resetMsg[64];
        snprintf(resetMsg, sizeof(resetMsg), "Hook::Initialize: Found Reset at address 0x%p", ResetFunc);
        LogMessage(resetMsg);

        LogMessage("Hook::Initialize: Releasing dummy device...");
        pDummyDevice->Release();
        LogMessage("Hook::Initialize: Dummy device released.");

        LogMessage("Hook::Initialize: Creating Reset hook...");
        if (MH_CreateHook(ResetFunc, &HookedReset, reinterpret_cast<LPVOID*>(&oReset)) != MH_OK) {
            LogMessage("Hook::Initialize Error: MH_CreateHook for Reset failed!");
            return false;
        }
        LogMessage("Hook::Initialize: Reset Hook Created.");

        LogMessage("Hook::Initialize: Creating EndScene hook...");
        if (MH_CreateHook(EndSceneFunc, &HookedEndScene, reinterpret_cast<LPVOID*>(&oEndScene)) != MH_OK) {
            LogMessage("Hook::Initialize Error: MH_CreateHook for EndScene failed!");
            MH_RemoveHook(ResetFunc);
            return false;
        }
        LogMessage("Hook::Initialize: EndScene Hook Created.");

        LogMessage("Hook::Initialize: Enabling EndScene hook...");
        if (MH_EnableHook(EndSceneFunc) != MH_OK) {
            LogMessage("Hook::Initialize Error: MH_EnableHook for EndScene failed!");
            MH_RemoveHook(EndSceneFunc);
            MH_RemoveHook(ResetFunc);
            return false;
        }
        LogMessage("Hook::Initialize: EndScene Hook Enabled.");

        LogMessage("Hook::Initialize: Enabling Reset hook...");
        if (MH_EnableHook(ResetFunc) != MH_OK) {
            LogMessage("Hook::Initialize Error: MH_EnableHook for Reset failed!");
            MH_RemoveHook(EndSceneFunc);
            MH_RemoveHook(ResetFunc);
            MH_DisableHook(EndSceneFunc);
            return false;
        }
        LogMessage("Hook::Initialize: Reset Hook Enabled.");

        GameUISystemShutdownFunc = (LPVOID)0x00529160;
        LogMessage("Hook::Initialize: Creating GameUISystemShutdown hook (Address: 0x00529160)...");
        if (MH_CreateHook(GameUISystemShutdownFunc, &HookedGameUISystemShutdown, reinterpret_cast<LPVOID*>(&oGameUISystemShutdown)) != MH_OK) {
            LogMessage("Hook::Initialize Error: MH_CreateHook for GameUISystemShutdown failed!");
            MH_RemoveHook(EndSceneFunc);
            MH_RemoveHook(ResetFunc);
            MH_DisableHook(EndSceneFunc);
            MH_DisableHook(ResetFunc);
            return false;
        }
        LogMessage("Hook::Initialize: GameUISystemShutdown Hook Created.");

        LogMessage("Hook::Initialize: Enabling GameUISystemShutdown hook...");
        if (MH_EnableHook(GameUISystemShutdownFunc) != MH_OK) {
             LogMessage("Hook::Initialize Error: MH_EnableHook for GameUISystemShutdown failed!");
             MH_RemoveHook(GameUISystemShutdownFunc);
             MH_RemoveHook(EndSceneFunc);
             MH_RemoveHook(ResetFunc);
             MH_DisableHook(EndSceneFunc);
             MH_DisableHook(ResetFunc);
             return false;
        }
        LogMessage("Hook::Initialize: GameUISystemShutdown Hook Enabled.");


        is_d3d_hooked = true;
        LogMessage("Hook::Initialize: D3D Hook Initialization Successful.");
        return true;
    }

    void CleanupHook() {
        if (g_cleanupCalled) {
            LogMessage("[Hook] CleanupHook: Already called, skipping duplicate run.");
            return;
        }
        g_cleanupCalled = true;

        LogMessage("[Hook] CleanupHook: Starting cleanup process...");

        LogMessage("[Hook] CleanupHook: Shutting down GUI...");
        GUI::Shutdown();
        LogMessage("[Hook] CleanupHook: GUI shutdown completed.");

        LogMessage("[Hook] CleanupHook: Shutting down ObjectManager...");
        ObjectManager::Shutdown();
        LogMessage("[Hook] CleanupHook: ObjectManager shutdown completed.");

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

        LogMessage("[Hook] CleanupHook: Disabling GameUISystemShutdown hook...");
        if (GameUISystemShutdownFunc) {
            MH_DisableHook(GameUISystemShutdownFunc);
            MH_RemoveHook(GameUISystemShutdownFunc);
        }
        LogMessage("[Hook] CleanupHook: GameUISystemShutdown hook disabled.");

        oEndScene = nullptr;
        oReset = nullptr;
        is_d3d_hooked = false;
        is_gui_initialized = false;

        LogMessage("[Hook] CleanupHook: Uninitializing MinHook...");
        MH_Uninitialize();
        LogMessage("[Hook] CleanupHook: MinHook uninitialized.");

        LogMessage("[Hook] CleanupHook: Cleanup complete. Process should terminate normally now.");

        ShutdownLogFile();
    }

} // namespace Hook 