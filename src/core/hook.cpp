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
#include <cmath> // Include for sqrtf
#include <memory> // Include for std::shared_ptr

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
// Store shared_ptrs to WowObjects instead of just strings
std::vector<std::shared_ptr<WowObject>> object_list_pointers;
int selected_object_index = -1;
// Keep track of the actual selected object pointer for easy access
std::shared_ptr<WowObject> selected_object_ptr = nullptr; 

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

// Helper function to calculate distance between two points
inline float CalculateDistance(const Vector3& p1, const Vector3& p2) {
    float dx = p1.x - p2.x;
    float dy = p1.y - p2.y;
    float dz = p1.z - p2.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

// Function to update the object list pointers (formerly UpdateObjectListForGUI)
void UpdateObjectPointerList() {
    object_list_pointers.clear();
    selected_object_index = -1;
    selected_object_ptr = nullptr; // Clear selected pointer too

    LogMessage("UpdateObjectPointerList: Starting update...");
    ObjectManager* objMgr = ObjectManager::GetInstance();
    if (!objMgr || !objMgr->IsInitialized()) {
        LogMessage("UpdateObjectPointerList: Aborted - ObjectManager not ready.");
        return;
    }

    objMgr->Update(); // Refresh the ObjectManager's internal cache
    LogMessage("UpdateObjectPointerList: ObjectManager::Update() finished.");

    // Get local player position for distance check
    Vector3 playerPos = {0, 0, 0};
    bool playerPosValid = false;
    auto player = objMgr->GetLocalPlayer();
    if (player) {
        player->UpdateDynamicData(); // Ensure player position is fresh
        playerPos = player->GetPosition();
        if (playerPos.x != 0.0f || playerPos.y != 0.0f || playerPos.z != 0.0f) {
            playerPosValid = true;
        }
    }
    if (!playerPosValid) {
         LogMessage("UpdateObjectPointerList: Warning - Could not get valid player position for distance filtering.");
    }

    auto objects_map = objMgr->GetObjects(); // Get the map of shared_ptrs
    LogMessage("UpdateObjectPointerList: Retrieved " + std::to_string(objects_map.size()) + " raw objects from cache.");

    object_list_pointers.reserve(objects_map.size()); // Reserve potential max space
    
    constexpr float MAX_DISTANCE_FILTER = 1000.0f;
    
    for (const auto& pair : objects_map) {
        if (!pair.second) continue; // Skip null pointers
        
        auto& objPtr = pair.second;
        WowObjectType objType = objPtr->GetType();

        // Always include items, regardless of distance
        if (objType == OBJECT_ITEM) {
            object_list_pointers.push_back(objPtr);
            continue; 
        }

        // For non-items, filter by distance if player position is valid
        if (playerPosValid) {
            objPtr->UpdateDynamicData(); // Ensure object position is updated before checking distance
            Vector3 objPos = objPtr->GetPosition();
            float distance = CalculateDistance(playerPos, objPos);

            if (distance <= MAX_DISTANCE_FILTER) {
                object_list_pointers.push_back(objPtr);
            }
        } else {
            // If player position is invalid, maybe include all non-items?
            // Or exclude them? Let's include them for now.
            object_list_pointers.push_back(objPtr); 
        }
    }
    LogMessage("UpdateObjectPointerList: Finished filtering. Added " + std::to_string(object_list_pointers.size()) + " objects to GUI list.");
    
    // Optional: Sort the list here if desired (e.g., by distance or name)
    // std::sort(object_list_pointers.begin(), ...);
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
                       player->UpdateDynamicData(); // Update player data every frame

                       ImGui::Text("Local Player: %s", player->GetName().c_str());
                       Vector3 pos = player->GetPosition();
                       ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
                       ImGui::Text("Facing: %.2f", player->GetFacing());
                       // Cast to WowUnit to access unit-specific data
                       auto playerUnit = std::dynamic_pointer_cast<WowUnit>(player);
                       if (playerUnit) {
                           ImGui::Text("Health: %d / %d", playerUnit->GetHealth(), playerUnit->GetMaxHealth());
                           ImGui::Text("%s: %d / %d", playerUnit->GetPowerTypeString().c_str(), playerUnit->GetPower(), playerUnit->GetMaxPower());
                           ImGui::Text("Level: %d", playerUnit->GetLevel());
                           ImGui::Text("Flags: 0x%X", playerUnit->GetUnitFlags());
                           ImGui::Text("Casting: %d", playerUnit->GetCastingSpellId());
                           ImGui::Text("Channeling: %d", playerUnit->GetChannelSpellId());
                       }
                    } else {
                        ImGui::Text("Local player object not found in cache/lookup.");
                    }
                } else {
                    ImGui::Text("Object Manager initializing...");
                }
                ImGui::EndTabItem();
            }
            
            // Objects tab - Updated Drawing Logic
            if (ImGui::BeginTabItem("Objects")) {
                // Refresh button now calls UpdateObjectPointerList
                if (!objMgr->IsInitialized()) {
                    ImGui::TextDisabled("Refresh Objects (Initializing...)");
                } else {
                    if (ImGui::Button("Refresh Objects")) {
                        UpdateObjectPointerList(); // Update the pointer list
                    }
                }
                ImGui::SameLine();
                ImGui::Text("%zu objects found", object_list_pointers.size());
                
                // Get local player pos once for distance calculation - Moved outside list child
                Vector3 playerPos = {0, 0, 0};
                bool playerPosValid = false;
                if (objMgr->IsInitialized()) { // Check again in case it changed
                    auto player = objMgr->GetLocalPlayer();
                    if (player) {
                        // Ensure player data is reasonably fresh for position calculation
                        player->UpdateDynamicData(); 
                        playerPos = player->GetPosition(); // Use cached position
                        if (playerPos.x != 0.0f || playerPos.y != 0.0f || playerPos.z != 0.0f) {
                             playerPosValid = true;
                        }
                    }
                }

                // Dynamic list rendering
                float listHeight = ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 4; // Leave more space for details
                ImGui::BeginChild("ObjectList", ImVec2(0, listHeight > 0 ? listHeight : 100), true);
                
                if (object_list_pointers.empty()) {
                    ImGui::Text(objMgr->IsInitialized() ? "No objects found or list not refreshed." : "Object Manager not initialized.");
                } else {
                    // Player pos is calculated above now
                    for (size_t i = 0; i < object_list_pointers.size(); ++i) {
                        const auto& objPtr = object_list_pointers[i];
                        if (!objPtr) continue;

                        WowObjectType currentObjType = objPtr->GetType();

                        // Generate display string on the fly
                        std::stringstream ssLabel;
                        ssLabel << "GUID: 0x" << std::hex << std::setw(16) << std::setfill('0') << GuidToUint64(objPtr->GetGUID()) << " | ";
                        ssLabel << "Name: '" << objPtr->GetName() << "' | "; 
                        ssLabel << "Type: " << objPtr->GetType();

                        // Add distance, handling OBJECT_ITEM specifically
                        if (currentObjType == OBJECT_ITEM) {
                            ssLabel << " | Dist: N/A";
                        } else if (playerPosValid) {
                            // Only calculate/show distance for non-items if player pos is valid
                            objPtr->UpdateDynamicData(); // Ensure position is current for list display
                            float distance = CalculateDistance(playerPos, objPtr->GetPosition());
                            ssLabel << " | Dist: " << std::fixed << std::setprecision(1) << distance;
                        } else {
                             ssLabel << " | Dist: ?"; // Indicate unknown distance if player pos invalid
                        }

                        std::string label = ssLabel.str();
                        if (ImGui::Selectable(label.c_str(), selected_object_index == i)) {
                            selected_object_index = i;
                            selected_object_ptr = objPtr; // Store the shared_ptr
                        }
                    }
                }
                ImGui::EndChild(); // End ObjectList

                // Display details for the selected object
                ImGui::Separator();
                ImGui::Text("Selected Object Details:");
                if (selected_object_ptr) {
                     // Update dynamic data just before displaying details
                    selected_object_ptr->UpdateDynamicData();
                    
                    ImGui::Text("GUID: 0x%llX", GuidToUint64(selected_object_ptr->GetGUID()));
                    ImGui::Text("Name: %s", selected_object_ptr->GetName().c_str());
                    ImGui::Text("Type: %d", selected_object_ptr->GetType());
                    Vector3 pos = selected_object_ptr->GetPosition();
                    ImGui::Text("Pos: (%.1f, %.1f, %.1f)", pos.x, pos.y, pos.z);
                    
                    // Display distance, handling OBJECT_ITEM
                    if (selected_object_ptr->GetType() == OBJECT_ITEM) {
                        ImGui::Text("Dist: N/A");
                    } else if (playerPosValid) {
                         float distance = CalculateDistance(playerPos, pos);
                         ImGui::Text("Dist: %.1f", distance);
                    } else {
                        ImGui::Text("Dist: ?");
                    }
                    
                    // Display Unit-specific data if applicable
                    auto selectedUnit = std::dynamic_pointer_cast<WowUnit>(selected_object_ptr);
                    if (selectedUnit) {
                        ImGui::Text("Health: %d / %d", selectedUnit->GetHealth(), selectedUnit->GetMaxHealth());
                        ImGui::Text("%s: %d / %d", selectedUnit->GetPowerTypeString().c_str(), selectedUnit->GetPower(), selectedUnit->GetMaxPower());
                        ImGui::Text("Level: %d", selectedUnit->GetLevel());
                        ImGui::Text("Flags: 0x%X", selectedUnit->GetUnitFlags());
                        ImGui::Text("Casting: %d", selectedUnit->GetCastingSpellId());
                        ImGui::Text("Channeling: %d", selectedUnit->GetChannelSpellId());
                        ImGui::Text("Is Dead: %s", selectedUnit->IsDead() ? "Yes" : "No");
                    }
                     // Add GameObject specific details if needed
                     auto selectedGameObject = std::dynamic_pointer_cast<WowGameObject>(selected_object_ptr);
                     if (selectedGameObject) {
                         // Example: Display quest status if relevant
                         // ImGui::Text("Quest Status: %d", selectedGameObject->GetQuestStatus());
                     }

                } else {
                    ImGui::Text("No object selected.");
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
                
                // Create a scrollable child window for the log messages
                // Use -FLT_MIN to automatically size to fill available space
                ImGui::BeginChild("LogScrollingRegion", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing()), true, ImGuiWindowFlags_HorizontalScrollbar);

                // Get the log messages (returns a copy)
                auto logs = GetLogMessages(); 

                // Display each log message on a new line
                for (const auto& msg : logs) {
                    // TextUnformatted is slightly faster than Text if no formatting is needed
                    ImGui::TextUnformatted(msg.c_str()); // Assuming messages already contain newline characters if desired
                }

                // Auto-scroll to the bottom if the scroll bar is near the end
                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                    ImGui::SetScrollHereY(1.0f);
                }

                ImGui::EndChild();
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