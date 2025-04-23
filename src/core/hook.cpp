#include "hook.h"
#include <d3d9.h>
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include <MinHook.h>
#include "objectmanager.h"
#include "log.h"
#include "functions.h"
#include "spellmanager.h"
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "d3d9.lib")

typedef HRESULT(APIENTRY* EndScene)(LPDIRECT3DDEVICE9 pDevice);
EndScene oEndScene = nullptr;

typedef HRESULT(APIENTRY* Reset)(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
Reset oReset = nullptr;

bool is_hook_initialized = false;
bool show_demo_window = true;
bool show_gui = true;

// Object manager address - this should be the address in WoW.exe
// In this example, we're using the address provided in the assembly dump
constexpr DWORD ENUM_VISIBLE_OBJECTS_ADDR = 0x004D4B30;
// Use the address of findObjectByIdAndData for the inner lookup
constexpr DWORD GET_OBJECT_PTR_BY_GUID_INNER_ADDR = 0x004D4BB0;
constexpr DWORD ADDR_CurrentTargetGUID = 0x00BD07B0; // Address for current target GUID

// Additional GUI tabs and state
bool show_objects_tab = true;
std::vector<std::string> object_list_strings;
int selected_object_index = -1;
static int spellIdToCast = 0; // Spell ID input field

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

WNDPROC oWndProc;
LRESULT APIENTRY WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    // Process ImGui window procedure first
    if (show_gui) {
        ImGuiIO& io = ImGui::GetIO();
        bool processed = ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam);
        
        // Block ALL mouse messages if ImGui wants to capture mouse
        if (io.WantCaptureMouse && (uMsg >= WM_MOUSEFIRST && uMsg <= WM_MOUSELAST)) {
            // If ImGui processed this mouse message, don't pass it to the game
            return processed ? 1 : 0;
        }
        
        // Block keyboard messages if ImGui wants the keyboard
        if (io.WantCaptureKeyboard && (uMsg >= WM_KEYFIRST && uMsg <= WM_KEYLAST)) {
            // If ImGui processed this keyboard message, don't pass it to the game
            return processed ? 1 : 0;
        }
        
        // If ImGui processed it and we got here, it was a non-mouse/keyboard message
        if (processed) {
            return 1;
        }
    }

    // If not handled by ImGui, pass to the original WndProc
    return CallWindowProc(oWndProc, hwnd, uMsg, wParam, lParam);
}

// Function to update the object list for the GUI
void UpdateObjectListForGUI() {
    object_list_strings.clear();
    selected_object_index = -1;

    LogMessage("UpdateObjectListForGUI: Starting update...");
    ObjectManager* objMgr = ObjectManager::GetInstance();
    objMgr->Update();
    LogMessage("UpdateObjectListForGUI: ObjectManager::Update() finished.");

    auto objects_map = objMgr->GetObjects();
    LogMessage("UpdateObjectListForGUI: Retrieved " + std::to_string(objects_map.size()) + " objects from cache.");

    for (const auto& pair : objects_map) {
        const auto& obj = pair.second;
        if (!obj) {
            LogMessage("UpdateObjectListForGUI: Skipped null object pointer in cache.");
            continue;
        }
        
        std::stringstream ssEntry;
        WGUID guid = obj->GetGUID();
        ssEntry << "GUID: 0x" << std::hex << std::setw(16) << std::setfill('0') << GuidToUint64(guid) << " | ";

        std::string name = "[Error]";
        try { name = obj->GetName(); } catch(...) { /* Ignore name read error */ }
        if (name.empty() || name == "[Error Reading Name]") {
            name = "[Unnamed/Error]";
        }
        ssEntry << "Name: '" << name << "' | ";

        std::string typeStr = "(Unknown)";
        WowObjectType type = OBJECT_NONE;
        try { type = obj->GetType(); } catch(...) { /* Ignore type read error */ }
        
        ssEntry << "Type: " << static_cast<int>(type);
        switch (type) { 
            case OBJECT_PLAYER: typeStr = "(Player)"; break;
            case OBJECT_UNIT: typeStr = "(Unit)"; break;
            case OBJECT_GAMEOBJECT: typeStr = "(GameObject)"; break;
            case OBJECT_ITEM: typeStr = "(Item)"; break;
            case OBJECT_CONTAINER: typeStr = "(Container)"; break;
            case OBJECT_DYNAMICOBJECT: typeStr = "(DynObject)"; break;
            case OBJECT_CORPSE: typeStr = "(Corpse)"; break;
            case OBJECT_NONE: typeStr = "(None?)"; break;
            default: break; 
        }
        ssEntry << typeStr;

        object_list_strings.push_back(ssEntry.str());
    }
    LogMessage("UpdateObjectListForGUI: Finished creating display list.");
}

HRESULT APIENTRY HookedEndScene(LPDIRECT3DDEVICE9 pDevice) {
    // One-time hook setup (DX hook, ImGui context, WndProc)
    if (!is_hook_initialized) {
        LogMessage("HookedEndScene: Performing one-time setup...");
        D3DDEVICE_CREATION_PARAMETERS params;
        pDevice->GetCreationParameters(&params);

        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.MouseDrawCursor = false;
        io.IniFilename = NULL;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        ImGui_ImplWin32_Init(params.hFocusWindow);
        ImGui_ImplDX9_Init(pDevice);

        oWndProc = (WNDPROC)SetWindowLongPtr(params.hFocusWindow, GWLP_WNDPROC, (LONG_PTR)WndProc);
        
        // Call basic Initialize for Object Manager (only sets func ptrs)
        ObjectManager* objMgr = ObjectManager::GetInstance();
        objMgr->Initialize(ENUM_VISIBLE_OBJECTS_ADDR, GET_OBJECT_PTR_BY_GUID_INNER_ADDR);
        // Don't log failure here, as TryFinish handles it

        // Initialize game function pointers
        InitializeFunctions();

        is_hook_initialized = true;
        LogMessage("HookedEndScene: One-time setup complete.");
    }

    // Attempt to finish Object Manager initialization if not already done
    ObjectManager* objMgr = ObjectManager::GetInstance(); // Get instance again
    if (!objMgr->IsInitialized()) {
        objMgr->TryFinishInitialization();
        // Optional: Add a small delay or limit frequency of attempts?
    }

    // --- Per Frame Logic --- 
    if (GetAsyncKeyState(VK_INSERT) & 1) {
        show_gui = !show_gui;
    }

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (show_gui) {
        // Set initial position just once on the first frame
        static bool first_frame = true;
        if (first_frame) {
            ImGui::SetNextWindowPos(ImVec2(50, 50), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
            first_frame = false;
        }
        
        // Create a window with tabs
        ImGui::Begin("WoW Hook", &show_gui);
        
        if (ImGui::BeginTabBar("MainTabs")) {
            // Main tab
            if (ImGui::BeginTabItem("Main")) {
                // Check if ObjMgr is initialized before accessing player data
                if (objMgr->IsInitialized()) {
                    auto player = objMgr->GetLocalPlayer();
                    if (player) {
                       ImGui::Text("Local Player: %s", player->GetName().c_str());
                       Vector3 pos = player->GetPosition();
                       ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
                       ImGui::Text("Facing: %.2f", player->GetFacing());
                       ImGui::Text("Health: %d / %d", player->GetHealth(), player->GetMaxHealth());
                       ImGui::Text("Level: %d", player->GetLevel());
                    } else {
                        ImGui::Text("Local player object not found in cache/lookup.");
                    }
                } else {
                    ImGui::Text("Object Manager initializing...");
                }
                ImGui::EndTabItem();
            }
            
            // Objects tab
            if (ImGui::BeginTabItem("Objects")) {
                // Only allow refresh if initialized
                if (!objMgr->IsInitialized()) {
                    ImGui::TextDisabled("Refresh Objects (Initializing...)");
                } else {
                    if (ImGui::Button("Refresh Objects")) {
                        UpdateObjectListForGUI();
                    }
                }
                ImGui::SameLine();
                ImGui::Text("%zu objects found", object_list_strings.size());
                
                ImGui::BeginChild("ObjectList", ImVec2(0, 150), true);
                if (object_list_strings.empty()) {
                    ImGui::Text(objMgr->IsInitialized() ? "No objects found or list not refreshed." : "Object Manager not initialized.");
                } else {
                    for (size_t i = 0; i < object_list_strings.size(); ++i) {
                        if (ImGui::Selectable(object_list_strings[i].c_str(), selected_object_index == static_cast<int>(i))) {
                            selected_object_index = static_cast<int>(i);
                        }
                    }
                }
                ImGui::EndChild();
                
                // Display selected object details
                if (objMgr->IsInitialized() && selected_object_index >= 0 && static_cast<size_t>(selected_object_index) < object_list_strings.size()) {
                    ImGui::Separator();
                    ImGui::Text("Selected Object Info (from list string):");
                    ImGui::TextWrapped("%s", object_list_strings[selected_object_index].c_str());
                } else if (selected_object_index >= 0) {
                     ImGui::Text("Object details unavailable (Manager not ready?).");
                }
                
                ImGui::EndTabItem();
            }
            
            // Spells Tab
            if (ImGui::BeginTabItem("Spells")) {
                ImGui::InputInt("Spell ID", &spellIdToCast);
                
                // Disable button if ObjMgr isn't ready (needed for local player context eventually)
                // Or if the function pointer isn't set (checked inside CastSpell)
                bool canCast = objMgr->IsInitialized(); 
                if (!canCast) {
                    ImGui::BeginDisabled();
                }
                
                if (ImGui::Button("Cast Spell on Target")) {
                    uint64_t currentTargetGuid = 0;
                    try {
                        // Read the target GUID directly from the address
                        // Ensure the pointer is valid before dereferencing if possible,
                        // but for a static global, direct read is common.
                        currentTargetGuid = *(uint64_t*)ADDR_CurrentTargetGUID;
                        
                        LogStream ss;
                        ss << "Attempting to cast SpellID: " << spellIdToCast 
                           << " on TargetGUID: 0x" << std::hex << currentTargetGuid;
                        LogMessage(ss.str());
                        
                        if (currentTargetGuid != 0) {
                            bool success = SpellManager::GetInstance().CastSpell(spellIdToCast, currentTargetGuid);
                            LogMessage(success ? "CastSpell call succeeded (returned true)." : "CastSpell call failed (returned false).");
                        } else {
                            LogMessage("No target selected (GUID is 0).");
                        }
                        
                    } catch (const std::exception& e) {
                        LogStream ssErr;
                        ssErr << "Exception reading target GUID or casting spell: " << e.what();
                        LogMessage(ssErr.str());
                    } catch (...) {
                         LogMessage("Unknown exception reading target GUID or casting spell.");
                    }
                }
                
                if (!canCast) {
                    ImGui::EndDisabled();
                }

                ImGui::EndTabItem();
            }
            
            // Log tab
            if (ImGui::BeginTabItem("Log")) {
                // Button to clear the log
                if (ImGui::Button("Clear Log")) {
                    ClearLogMessages();
                }
                ImGui::Separator();
                
                // Combine log messages into a single string
                auto logs = GetLogMessages(); 
                std::stringstream log_ss;
                for (const auto& msg : logs) {
                    log_ss << msg; // Messages should already end with \n
                }
                static std::string log_buffer; // Make static to avoid reallocation every frame
                log_buffer = log_ss.str();

                // Use InputTextMultiline with corrected size
                ImGui::InputTextMultiline("##LogView", 
                                          (char*)log_buffer.c_str(),        // Pass C-style string 
                                          log_buffer.length() + 1,        // Pass length + 1 for null terminator
                                          ImVec2(-FLT_MIN, -FLT_MIN),       // Fill available space
                                          ImGuiInputTextFlags_ReadOnly);    // Make it read-only
                
                ImGui::EndTabItem();
            }
            
            ImGui::EndTabBar();
        }
        
        ImGui::End();
    }

    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    return oEndScene(pDevice);
}

HRESULT APIENTRY HookedReset(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT result = oReset(pDevice, pPresentationParameters);
    if (SUCCEEDED(result)) {
        ImGui_ImplDX9_CreateDeviceObjects();
    }
    return result;
}

void InitializeHook() {
    // Initialize MinHook
    if (MH_Initialize() != MH_OK) {
        return;
    }

    // Get Direct3D device
    IDirect3D9* pD3D = Direct3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) return;

    D3DPRESENT_PARAMETERS d3dpp = { 0 };
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = GetForegroundWindow();
    d3dpp.Windowed = TRUE;

    IDirect3DDevice9* pDevice = nullptr;
    HRESULT result = pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, d3dpp.hDeviceWindow,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDevice);

    if (FAILED(result) || !pDevice) {
        pD3D->Release();
        return;
    }

    void** vTable = *reinterpret_cast<void***>(pDevice);
    
    // Hook EndScene (index 42 in the vtable)
    if (MH_CreateHook(vTable[42], &HookedEndScene, reinterpret_cast<LPVOID*>(&oEndScene)) != MH_OK) {
        pDevice->Release();
        pD3D->Release();
        return;
    }

    // Hook Reset (index 16)
    if (MH_CreateHook(vTable[16], &HookedReset, reinterpret_cast<LPVOID*>(&oReset)) != MH_OK) {
        pDevice->Release();
        pD3D->Release();
        return;
    }

    // Enable both hooks
    if (MH_EnableHook(vTable[42]) != MH_OK || MH_EnableHook(vTable[16]) != MH_OK) {
        pDevice->Release();
        pD3D->Release();
        return;
    }

    pDevice->Release();
    pD3D->Release();
}

void CleanupHook() {
    if (is_hook_initialized) {
        ImGui_ImplDX9_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }

    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
} 