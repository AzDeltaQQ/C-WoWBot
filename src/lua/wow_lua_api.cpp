#include "wow_lua_api.h"
#include <stdexcept> // For runtime_error
#include <string>    // For error messages

// Define the global function pointers
namespace WowLua {
    tLua_GetTop                  lua_gettop = nullptr;
    tLua_SetTop                  lua_settop = nullptr;
    tLua_PushString              lua_pushstring = nullptr;
    tLua_PushInteger             lua_pushinteger = nullptr;
    tLua_PushNumber              lua_pushnumber = nullptr;
    tLua_ToLString               lua_tolstring = nullptr;
    tLua_ToNumber                lua_tonumber = nullptr;
    tLua_ToInteger               lua_tointeger = nullptr;
    tLua_Type                    lua_type = nullptr;
    tLua_PCall                   lua_pcall = nullptr;
    tLua_PushBoolean             lua_pushboolean = nullptr;
    tLua_PushCClosure            lua_pushcclosure = nullptr;
    tLua_ToBoolean               lua_toboolean = nullptr;
    tLua_ToCFunction             lua_tocfunction = nullptr;
    tLua_LoadBuffer              lua_loadbuffer = nullptr;

    // WoW Specific
    tLua_GetField_WoW            lua_getfield_wow = nullptr;
    tFrameScript_Execute         FrameScript_Execute = nullptr;
    tLua_SetField_WoW            lua_setfield_wow = nullptr;
    // Initialize others as nullptr if added

    void InitializeWowLuaFunctions() {
        // We are using absolute addresses provided by user
        
        lua_gettop = (tLua_GetTop)0x0084DBD0;
        lua_settop = (tLua_SetTop)0x0084DBF0;
        lua_pushstring = (tLua_PushString)0x0084E350;
        lua_pushinteger = (tLua_PushInteger)0x0084E2D0;
        lua_pushnumber = (tLua_PushNumber)0x0084E2A0;
        lua_tolstring = (tLua_ToLString)0x0084E0E0;
        lua_tonumber = (tLua_ToNumber)0x0084E030;
        lua_tointeger = (tLua_ToInteger)0x0084E070;
        lua_type = (tLua_Type)0x0084DEB0;
        lua_pcall = (tLua_PCall)0x0084EC50;
        lua_loadbuffer = (tLua_LoadBuffer)0x0084F860;
        lua_pushboolean = (tLua_PushBoolean)0x0084E4D0;
        lua_pushcclosure = (tLua_PushCClosure)0x0084E400;
        lua_toboolean = (tLua_ToBoolean)0x0044E2C0;
        lua_tocfunction = (tLua_ToCFunction)0x0084E1C0;

        // WoW Specific
        lua_getfield_wow = (tLua_GetField_WoW)0x0084F3B0;
        FrameScript_Execute = (tFrameScript_Execute)0x00819210;
        lua_setfield_wow = (tLua_SetField_WoW)0x0084E900;

        // Basic check
        if (!lua_gettop || !FrameScript_Execute || !lua_loadbuffer || !lua_pcall) { // Check critical ones
            throw std::runtime_error("Failed to initialize WoW Lua function pointers!");
        }
    }

} // namespace WowLua 