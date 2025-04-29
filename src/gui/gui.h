#pragma once
#include <Windows.h>
#include <d3d9.h>
#include "imgui.h"
#include "log_tab.h"
#include "objects_tab.h"
#include "spells_tab.h"
#include "bot_tab.h"

// Forward declarations
class BotController; // Forward declare BotController

namespace GUI {
    extern bool g_ShowGui; // Keep track of GUI visibility
    extern bool g_isGuiInitialized; // Track if GUI was successfully initialized
    extern HWND g_hWnd; // Declare the game window handle
    extern WNDPROC oWndProc; // Declare the original window procedure pointer
    extern LPDIRECT3DDEVICE9 g_pd3dDevice;
    extern BotController* g_BotController; // Declare global BotController pointer

    // Initialize ImGui and Win32/DX9 backends
    void Initialize(HWND hwnd, LPDIRECT3DDEVICE9 pDevice);

    // Shutdown ImGui
    void Shutdown();

    // Render the main ImGui frame and all tabs
    void Render();

    // Handle window messages for ImGui
    LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

    // Toggles the GUI visibility
    void ToggleVisibility();
    bool IsVisible();

    // Checks if the GUI has been successfully initialized.
    bool IsInitialized();

} // namespace GUI