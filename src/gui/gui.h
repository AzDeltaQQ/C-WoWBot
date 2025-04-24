#pragma once
#include <Windows.h>
#include <d3d9.h>
#include "imgui.h"

namespace GUI {
    extern bool g_ShowGui; // Keep track of GUI visibility
    extern bool g_isGuiInitialized; // Track if GUI was successfully initialized

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

    // For handling Reset
    // void InvalidateDeviceObjects(); // Removed as Reset is no longer hooked
    // void CreateDeviceObjects(); // Removed as Reset is no longer hooked

} // namespace GUI 