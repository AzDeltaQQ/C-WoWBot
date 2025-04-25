#include "gui.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "log.h"

// Include headers for individual tabs
#include "main_tab.h"
#include "objects_tab.h"
#include "spells_tab.h"
#include "log_tab.h"

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {
    bool show_gui = true;
    HWND game_hwnd = nullptr;
    WNDPROC original_wndproc = nullptr;
} // namespace

namespace GUI {
    bool g_ShowGui = true; // Default to showing the GUI
    bool g_isGuiInitialized = false; // Initialize flag to false
    HWND g_hWnd = nullptr;
    WNDPROC oWndProc = nullptr;
    LPDIRECT3DDEVICE9 g_pd3dDevice = nullptr; // Add device pointer

    void Initialize(HWND hwnd, LPDIRECT3DDEVICE9 pDevice) {
        game_hwnd = hwnd;
        g_pd3dDevice = pDevice; // Store the device pointer
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = false;
        io.IniFilename = NULL;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        ImGui::StyleColorsDark();
        ImGui::GetStyle().WindowMinSize = ImVec2(400, 300); // Set a minimum window size

        LogMessage("GUI::Initialize: Setting up backends...");
        // Initialize Win32 and DX9 backends
        if (!ImGui_ImplWin32_Init(hwnd)) {
            LogMessage("GUI::Initialize Error: ImGui_ImplWin32_Init failed!");
            ImGui::DestroyContext();
            return;
        }
        if (!ImGui_ImplDX9_Init(pDevice)) {
            LogMessage("GUI::Initialize Error: ImGui_ImplDX9_Init failed!");
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            return;
        }

        LogMessage("GUI::Initialize: Hooking WndProc...");
        // Hook the window procedure
        g_hWnd = hwnd;
        oWndProc = (WNDPROC)SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
        if (!oWndProc) {
            LogMessage("GUI::Initialize Error: Failed to hook WndProc!");
            ImGui_ImplDX9_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            return;
        }

        LogMessage("GUI::Initialize: Initialization successful.");
        g_isGuiInitialized = true; // Set flag on successful initialization
    }

    void Shutdown() {
        LogMessage("GUI::Shutdown: Starting shutdown...");
        if (!g_isGuiInitialized) {
            LogMessage("GUI::Shutdown: Skipped full shutdown (GUI not initialized).");
            // Still attempt to destroy context if it exists
            LogMessage("GUI::Shutdown: Checking for existing ImGui context...");
            if (ImGui::GetCurrentContext()) {
                LogMessage("GUI::Shutdown: Destroying lingering ImGui context...");
                ImGui::DestroyContext();
                LogMessage("GUI::Shutdown: Lingering ImGui context destroyed.");
            } else {
                LogMessage("GUI::Shutdown: No lingering ImGui context found.");
            }
            return; // Exit early if GUI wasn't initialized
        }

        LogMessage("GUI::Shutdown: Attempting to restore WndProc...");
        if (oWndProc && g_hWnd) {
            // Check if the window handle is still valid before trying to modify it
            if (IsWindow(g_hWnd)) { 
                char restoreMsg[256];
                snprintf(restoreMsg, sizeof(restoreMsg), "GUI::Shutdown: Restoring WndProc (Original: 0x%p, Target HWND: 0x%p)...", oWndProc, g_hWnd);
                LogMessage(restoreMsg);
                
                // Store previous value (optional, but good practice)
                LONG_PTR prevWndProc = SetWindowLongPtr(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
                
                // Check for errors after setting
                if (prevWndProc == 0) { // SetWindowLongPtr returns 0 on failure IF the previous value was also 0
                    DWORD error = GetLastError();
                    // Check if the error is non-zero (0 means success even if prevWndProc was 0)
                    if (error != 0) {
                        char errorMsg[256];
                        snprintf(errorMsg, sizeof(errorMsg), "GUI::Shutdown Error: Failed to restore WndProc! GetLastError() = %lu", error);
                        LogMessage(errorMsg); 
                        // Decide how to proceed - maybe still nullify pointers?
                    } else {
                        // Success, previous value was genuinely 0
                        LogMessage("GUI::Shutdown: WndProc restored successfully (Previous value was 0).");
                    }
                } else {
                    LogMessage("GUI::Shutdown: WndProc restored successfully.");
                }
            } else {
                 LogMessage("GUI::Shutdown: Skipped restoring WndProc because g_hWnd is no longer a valid window handle.");
            }
            
            // Always nullify our pointers regardless of whether restore happened/succeeded
            oWndProc = nullptr;
            g_hWnd = nullptr;
        } else {
            char skipMsg[256];
            snprintf(skipMsg, sizeof(skipMsg), "GUI::Shutdown: Skipped restoring WndProc (oWndProc=0x%p, g_hWnd=0x%p).", oWndProc, g_hWnd);
            LogMessage(skipMsg);
        }

        LogMessage("GUI::Shutdown: Shutting down DX9 backend...");
        ImGui_ImplDX9_Shutdown();
        LogMessage("GUI::Shutdown: DX9 backend shutdown complete.");
        
        LogMessage("GUI::Shutdown: Shutting down Win32 backend...");
        ImGui_ImplWin32_Shutdown();
        LogMessage("GUI::Shutdown: Win32 backend shutdown complete.");
        
        LogMessage("GUI::Shutdown: Destroying ImGui context...");
        ImGui::DestroyContext();
        LogMessage("GUI::Shutdown: ImGui context destroyed.");

        // Explicitly release device if we still have it
        if (g_pd3dDevice != nullptr) {
            LogMessage("GUI::Shutdown: Releasing D3D9 Device...");
            ULONG refCount = g_pd3dDevice->Release();
            char deviceReleaseMsg[256];
            snprintf(deviceReleaseMsg, sizeof(deviceReleaseMsg), "GUI::Shutdown: D3D9 Device released. Reference count: %lu", refCount);
            LogMessage(deviceReleaseMsg);
            g_pd3dDevice = nullptr;
        } else {
            LogMessage("GUI::Shutdown: D3D9 Device already null, skipping release.");
        }

        // Force log file flush
        LogMessage("GUI::Shutdown: Forcing log file flush...");
        
        // Reset initialization flag
        g_isGuiInitialized = false;
        
        LogMessage("GUI::Shutdown: GUI shutdown complete.");
    }

    void ToggleVisibility() {
        show_gui = !show_gui;
    }

    bool IsVisible() {
        return show_gui;
    }

    bool IsInitialized() {
        return g_isGuiInitialized;
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        if (show_gui) {
            ImGuiIO& io = ImGui::GetIO();
            // Let ImGui handle its events first
            bool processed = ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam);
            
            // Block game input if ImGui wants focus
            if (io.WantCaptureMouse && (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)) {
                return processed ? 1 : 0;
            }
            if (io.WantCaptureKeyboard && (uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST)) {
                return processed ? 1 : 0;
            }
            // If ImGui processed it, don't pass to game (unless it wasn't mouse/kb)
            if (processed) {
                 return 1;
            }
        }

        // Pass messages to the original game WndProc
        return CallWindowProc(GUI::oWndProc, hwnd, uMsg, wParam, lParam);
    }

    void Render() {
        // Prevent rendering if GUI is hidden or not initialized/already shut down
        if (!g_isGuiInitialized || !show_gui) { 
            return;
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Set initial window size/pos
        static bool first_frame = true;
        if (first_frame) {
            ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
            first_frame = false;
        }

        // Main window
        ImGui::Begin("WoW Hook", &show_gui); 

        if (ImGui::BeginTabBar("MainTabs")) {
            // Render each tab
            if (ImGui::BeginTabItem("Main")) {
                RenderMainTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Objects")) {
                RenderObjectsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Spells")) {
                RenderSpellsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Log")) {
                RenderLogTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End(); // End main window

        ImGui::EndFrame();
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

} // namespace GUI 