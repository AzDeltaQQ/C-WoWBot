#pragma once
#include <Windows.h>
// #include <d3d9.h> // No longer needed here as HookedEndScene is internal

// Add the client state address definition
constexpr DWORD CLIENT_STATE_ADDR = 0x00B6AA38;

namespace Hook {
    // Initializes the D3D hooks using MinHook.
    // Returns true on success, false on failure.
    bool Initialize();

    // Cleans up hooks, shuts down GUI, and uninitializes MinHook.
    void CleanupHook();

} // namespace Hook

// Main entry point to initialize the hooking mechanism
// void InitializeHook(); // Removed, initialization moved to HookedEndScene

// Removed: HRESULT APIENTRY HookedEndScene(LPDIRECT3DDEVICE9 pDevice); 