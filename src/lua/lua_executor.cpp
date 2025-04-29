#include "lua_executor.h"
#include "wow_lua_api.h" // Include the WoW Lua API definitions
#include "../utils/log.h" // Assuming log utility exists
#include <sstream> // For error formatting

namespace LuaExecutor {

    // NOTE: We no longer create/manage lua_State; we use WowLua:: function pointers and WowLua::GetLuaState().

    // Helper to get string representation of Lua stack value for errors
    static std::string GetStackValueAsString(lua_State* state, int index) {
        if (!state) return "[No Lua State]";
        // Use WowLua function pointers
        if (!WowLua::lua_type || !WowLua::lua_tolstring || !WowLua::lua_toboolean || !WowLua::lua_tonumber) {
            return "[WoW Lua Type/ToString func unavailable]";
        }
        int type = WowLua::lua_type(state, index);
        switch (type) {
            // Use standard Lua type constants (LUA_TSTRING, etc.) as they are just integers
            // These need to be defined manually or included from a minimal Lua header if needed.
            // Let's define the ones we use here for simplicity:
            #define LUA_TSTRING     4
            #define LUA_TBOOLEAN    1
            #define LUA_TNUMBER     3
            #define LUA_TNIL        0

            case LUA_TSTRING:
                return WowLua::lua_tolstring(state, index, NULL); // len is optional for tolstring
            case LUA_TBOOLEAN:
                return WowLua::lua_toboolean(state, index) ? "true" : "false";
            case LUA_TNUMBER: {
                std::stringstream ss;
                ss << WowLua::lua_tonumber(state, index);
                return ss.str();
            }
            case LUA_TNIL:
                return "nil";
            default: { // Simplified typename
                 const char* name = (type == 2) ? "function" : (type == 5) ? "table" : "other";
                 return name;
            }
        }
    }

    // Helper to check Lua results - FrameScript_Execute doesn't return error codes directly
    // We rely on it potentially printing errors to chat or logs.
    // We only check if the expected number of results are on the stack.
    static void CheckLuaResult(lua_State* state, int expected_results) {
        if (!state || !WowLua::lua_gettop) return;
        int actual_results = WowLua::lua_gettop(state);
        if (actual_results != expected_results) {
             std::string errorMsg = "Lua Execution Error: Expected " + std::to_string(expected_results) 
                                  + " return values, but got " + std::to_string(actual_results) + ".";
            if (actual_results > 0) {
                 errorMsg += " Top value: " + GetStackValueAsString(state, -1);
            }
            WowLua::lua_settop(state, 0); // Clear stack on error
            LogMessage(errorMsg.c_str());
            throw LuaException(errorMsg);
        }
    }

    // Initialize now simply calls the WowLua initializer
    bool Initialize() {
        try {
            WowLua::InitializeWowLuaFunctions();

            if (!GetState()) {
                 LogMessage("LuaExecutor Error: Failed to get Lua state.");
                 return false;
            }
             // Check critical functions needed for Lua pcall path
            if (!WowLua::lua_loadbuffer || !WowLua::lua_pcall || !WowLua::lua_gettop || !WowLua::lua_settop || !WowLua::lua_tolstring) {
                 LogMessage("LuaExecutor Error: Required Lua functions (loadbuffer, pcall, etc.) not initialized.");
                 return false;
            }

            LogMessage("LuaExecutor Initialized successfully.");
            return true;
        } catch (const std::exception& e) {
            std::string errorMsg = "LuaExecutor Initialization failed: ";
            errorMsg += e.what();
            LogMessage(errorMsg);
            return false;
        }
    }

    // Shutdown is no longer needed, as we don't own the Lua state
    void Shutdown() {
         LogMessage("LuaExecutor Shutdown called (no explicit action needed for WoW Lua state).");
    }

    // GetState now returns the WoW state
    lua_State* GetState() {
        return WowLua::GetLuaState();
    }

    // Execute using FrameScript_Execute
    void ExecuteStringNoResult(const std::string& script) {
        lua_State* L = GetState();
        if (!L) {
            throw LuaException("ExecuteStringNoResult Error: WoW Lua state is not available.");
        }
        // Check required functions are available (already checked in Initialize, but good practice)
        if (!WowLua::lua_loadbuffer || !WowLua::lua_pcall || !WowLua::lua_gettop || !WowLua::lua_settop || !WowLua::lua_tolstring) {
            throw LuaException("ExecuteStringNoResult Error: Required Lua functions not initialized.");
        }

        int top_before = WowLua::lua_gettop(L);

        try {
            // 1. Load the script string onto the stack
            int load_status = WowLua::lua_loadbuffer(L, script.c_str(), script.length(), "=LuaExecutor"); // Use a source name

            if (load_status != 0) {
                // Error loading script: error message is on top of the stack
                std::string errorMsg = "Lua Load Error: ";
                size_t len;
                const char* errStr = WowLua::lua_tolstring(L, -1, &len);
                if (errStr) errorMsg += std::string(errStr, len);
                else errorMsg += "(Unknown load error, code: " + std::to_string(load_status) + ")";
                
                WowLua::lua_settop(L, top_before); // Clean stack (error message)
                throw LuaException(errorMsg);
            }

            // 2. Execute the loaded chunk using pcall
            // nargs = 0, nresults = 0 (don't expect results), errfunc = 0
            int pcall_status = WowLua::lua_pcall(L, 0, 0, 0);

            if (pcall_status != 0) {
                // Error during pcall execution: error message is on top of the stack
                std::string errorMsg = "Lua PCall Error: ";
                size_t len;
                const char* errStr = WowLua::lua_tolstring(L, -1, &len);
                if (errStr) errorMsg += std::string(errStr, len);
                else errorMsg += "(Unknown pcall error, code: " + std::to_string(pcall_status) + ")";
                
                WowLua::lua_settop(L, top_before); // Clean stack (error message)
                throw LuaException(errorMsg);
            }

            // Pcall succeeded. Stack should be back to top_before as we requested 0 results.
            // Add a check/cleanup just in case.
            int top_after = WowLua::lua_gettop(L);
            if (top_after != top_before) {
                 LogMessage("LuaExecutor Warning: Stack not clean after pcall in ExecuteStringNoResult. Cleaning.");
                 WowLua::lua_settop(L, top_before);
            }

        } catch (...) {
            // Ensure stack is cleaned up even if error handling above failed or another exception occurred
            WowLua::lua_settop(L, top_before);
            throw; // Re-throw the exception
        }
    }

} // namespace LuaExecutor 