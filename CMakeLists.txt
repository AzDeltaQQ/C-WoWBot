cmake_minimum_required(VERSION 3.10)
project(WoWDX9Hook)

# Force 32-bit build
set(CMAKE_GENERATOR_PLATFORM Win32)
set(CMAKE_VS_PLATFORM_NAME "Win32")
set(CMAKE_VS_PLATFORM_TOOLSET_HOST_ARCHITECTURE "x86")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Add project source files using new paths
set(SOURCE_FILES
    src/core/dllmain.cpp
    src/core/hook.cpp
    src/game/objectmanager.cpp
    src/game/wowobject.cpp
    src/utils/log.cpp
    src/game/functions.cpp
    src/game/spellmanager.cpp
    # Add GUI source files
    src/gui/gui.cpp
    src/gui/main_tab.cpp
    src/gui/objects_tab.cpp
    src/gui/spells_tab.cpp
    src/gui/log_tab.cpp
    src/gui/bot_tab.cpp
    # src/gui/testing_tab.cpp # Removed testing tab source
    # Add Bot source files
    src/bot/core/BotController.cpp
    src/bot/core/MovementController.cpp
    src/bot/engine/GrindingEngine.cpp
    src/bot/pathing/PathManager.cpp
    src/bot/pathing/PathRecorder.cpp
    # Add Lua source files (NEW)
    src/lua/lua_executor.cpp
    src/lua/wow_lua_api.cpp
)

# ImGui source files using new paths
set(IMGUI_FILES
    dependencies/ImGui/imgui.cpp
    dependencies/ImGui/imgui_demo.cpp
    dependencies/ImGui/imgui_draw.cpp
    dependencies/ImGui/imgui_tables.cpp
    dependencies/ImGui/imgui_widgets.cpp
    dependencies/ImGui/backends/imgui_impl_dx9.cpp
    dependencies/ImGui/backends/imgui_impl_win32.cpp
)

# Create shared library
add_library(WoWDX9Hook SHARED ${SOURCE_FILES} ${IMGUI_FILES})

# MinHook library using new paths
add_library(minhook STATIC
    dependencies/MinHook/src/buffer.c
    dependencies/MinHook/src/hook.c
    dependencies/MinHook/src/trampoline.c
    dependencies/MinHook/src/hde/hde32.c
)

# Update MinHook public include directory path
target_include_directories(minhook PUBLIC dependencies/MinHook/include)

# Update Include directories for the main library
target_include_directories(WoWDX9Hook PRIVATE
    # ${CMAKE_SOURCE_DIR}/src          # Add the main source directory (Reverting this)
    ${CMAKE_SOURCE_DIR}/src/core       # Re-add individual subdirectories
    ${CMAKE_SOURCE_DIR}/src/game
    ${CMAKE_SOURCE_DIR}/src/utils
    ${CMAKE_SOURCE_DIR}/src/gui
    ${CMAKE_SOURCE_DIR}/src/bot/core    # Add bot include dirs
    ${CMAKE_SOURCE_DIR}/src/bot/engine
    ${CMAKE_SOURCE_DIR}/src/bot/pathing
    ${CMAKE_SOURCE_DIR}/src/lua         # Add Lua executor include dir (NEW)
    ${CMAKE_SOURCE_DIR}/dependencies/ImGui
    ${CMAKE_SOURCE_DIR}/dependencies/ImGui/backends
    ${CMAKE_SOURCE_DIR}/dependencies/MinHook/include # MinHook include is already absolute if add_library uses PUBLIC
    "$ENV{DXSDK_DIR}/Include" # Keep DX SDK include
)

# Link directories
target_link_directories(WoWDX9Hook PRIVATE
    "$ENV{DXSDK_DIR}/Lib/x86"
)

# Link libraries
target_link_libraries(WoWDX9Hook PRIVATE
    minhook
    d3d9
    d3dx9
) 