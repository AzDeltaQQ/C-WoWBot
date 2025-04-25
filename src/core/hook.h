#pragma once
#include <Windows.h>

// Add the client state address definition
constexpr DWORD CLIENT_STATE_ADDR = 0x00B6AA38;

namespace Hook {
    // Initializes the D3D hooks using MinHook.
    bool Initialize();

    // Cleans up hooks, shuts down GUI, and uninitializes MinHook.
    void CleanupHook();

} // namespace Hook