#pragma once

#include <string>
#include <stdexcept> // For exceptions
#include <sstream> // For error formatting
#include <type_traits> // For std::is_same_v
#include "wow_lua_api.h" // Include the WoW Lua API definitions
#include "../utils/log.h" // Assuming log utility exists

// Forward declare lua_State
struct lua_State; 

namespace LuaExecutor {

    // Exception class for Lua errors
    class LuaException : public std::runtime_error {
    public:
        LuaException(const std::string& message) : std::runtime_error(message) {}
    };

    // Initialize the Lua state and register basic libraries
    bool Initialize();

    // Shutdown and close the Lua state
    void Shutdown();

    // Execute a Lua string, expecting a specific return type T
    // Throws LuaException on error or if the return type doesn't match
    template<typename T>
    T ExecuteString(const std::string& script);

    // Execute using direct Lua API calls (loadbuffer, pcall)
    template<typename T>
    T ExecuteString(const std::string& script) {
        lua_State* L = GetState();
        // Check required functions for THIS implementation
        // Assuming lua_isstring exists or lua_tolstring handles type errors gracefully
        if (!L || !WowLua::lua_loadbuffer || !WowLua::lua_pcall || !WowLua::lua_gettop || !WowLua::lua_settop || !WowLua::lua_type || !WowLua::lua_tolstring) { 
            throw LuaException("WoW Lua state or required functions (loadbuffer, pcall, type, tolstring etc.) not available for direct execution.");
        }

        int top_before = WowLua::lua_gettop(L);

        // 1. Load the script string onto the stack as a function chunk
        // Use lua_loadbuffer as seen in FrameScript_Execute disassembly
        int load_status = WowLua::lua_loadbuffer(L, script.c_str(), script.length(), script.c_str());

        if (load_status != 0) {
            // Error loading script: error message is on top of the stack
            std::string errorMsg = "Lua Load Error: ";
             size_t len;
             // Use lua_tolstring which might work on the error object
             const char* errStr = WowLua::lua_tolstring(L, -1, &len);
             if (errStr) errorMsg += std::string(errStr, len);
             else errorMsg += "(Unknown load error, code: " + std::to_string(load_status) + ")";
            
            WowLua::lua_settop(L, top_before); // Clear stack (error message)
            throw LuaException(errorMsg);
        }
        // If load succeeds, the compiled chunk is now on top of the stack (at index top_before + 1)

        // 2. Execute the loaded chunk using pcall
        // lua_pcall(lua_State *L, int nargs, int nresults, int errfunc);
        // nargs = 0 (no arguments for the chunk)
        // nresults = 1 (Expect exactly one result for non-void, or handle cleanup for void)
        // errfunc = 0 (no message handler)
        int pcall_nresults = std::is_void_v<T> ? 0 : 1; // Request 0 results for void, 1 otherwise
        int pcall_status = WowLua::lua_pcall(L, 0, pcall_nresults, 0); 

        if (pcall_status != 0) {
            // Error during pcall execution: error message is on top of the stack
             std::string errorMsg = "Lua PCall Error: ";
             size_t len;
             const char* errStr = WowLua::lua_tolstring(L, -1, &len);
             if (errStr) errorMsg += std::string(errStr, len);
             else errorMsg += "(Unknown pcall error, code: " + std::to_string(pcall_status) + ")";
             
             WowLua::lua_settop(L, top_before); // Clear stack (error message)
             throw LuaException(errorMsg);
        }

        // Pcall succeeded. The result(s) (if any requested) are now on the stack.
        int actual_nresults = WowLua::lua_gettop(L) - top_before;

        // --- Handling based on template type T ---
        if constexpr (std::is_void_v<T>) {
            // For void, pcall requested 0 results, stack should be clean.
            // If somehow results were left (e.g. pcall implementation detail), clean them.
            if (actual_nresults != 0) {
                LogMessage("LuaExecutor Warning: Stack not clean after void pcall. Clearing."); // Optional log
                 WowLua::lua_settop(L, top_before);
            }
            // No return value needed.
        } else {
            // Expect exactly one result since we requested nresults = 1.
            if (actual_nresults != 1) {
                std::string errorMsg = "Lua Execution Error: Expected 1 return value after pcall, found " + std::to_string(actual_nresults);
                 // If results > 0, maybe log the top one, but the primary error is the count mismatch.
                if (actual_nresults > 0) {
                     WowLua::lua_settop(L, top_before); // Clean stack
                }
                 throw LuaException(errorMsg);
            }

            // Retrieve the single result
             try {
                 T returnValue;
                 
                #define LUA_TBOOLEAN_LOCAL    1 
                #define LUA_TNUMBER_LOCAL     3
                #define LUA_TSTRING_LOCAL     4
                #define LUA_TNIL_LOCAL        0 

                 int resultType = WowLua::lua_type(L, -1); // Get type of the single result

                 // Specialize based on T
                 if constexpr (std::is_same_v<T, bool>) {
                     // We now expect a number (1 or 0) instead of a boolean
                     if (!WowLua::lua_tointeger && !WowLua::lua_tonumber) throw LuaException("lua_tointeger/lua_tonumber unavailable for boolean retrieval");
                     if (resultType != LUA_TNUMBER_LOCAL) {
                         std::string typeErr = "Expected number result (1 or 0) for bool, got type " + std::to_string(resultType);
                         WowLua::lua_settop(L, top_before); // Clean before throw
                         throw LuaException(typeErr);
                     }
                     // Use lua_tointeger if available, otherwise lua_tonumber
                     int numResult = 0;
                     if (WowLua::lua_tointeger) {
                        numResult = WowLua::lua_tointeger(L, -1); 
                     } else {
                        numResult = static_cast<int>(WowLua::lua_tonumber(L, -1));
                     }
                     returnValue = (numResult != 0);
                 } else if constexpr (std::is_same_v<T, double> || std::is_same_v<T, float>) {
                     if (!WowLua::lua_tonumber) throw LuaException("lua_tonumber unavailable");
                     if (resultType != LUA_TNUMBER_LOCAL) {
                         std::string typeErr = "Expected number result, got type " + std::to_string(resultType);
                         WowLua::lua_settop(L, top_before); // Clean before throw
                         throw LuaException(typeErr);
                     }
                     if constexpr (std::is_same_v<T, float>) {
                         returnValue = static_cast<float>(WowLua::lua_tonumber(L, -1));
                     } else {
                         returnValue = WowLua::lua_tonumber(L, -1);
                     }
                 } else if constexpr (std::is_same_v<T, int>) {
                     // Use lua_tointeger if available, otherwise lua_tonumber
                     if (!WowLua::lua_tointeger && !WowLua::lua_tonumber) throw LuaException("lua_tointeger/lua_tonumber unavailable");

                     // Check the type returned by Lua
                     if (resultType == LUA_TNUMBER_LOCAL) {
                         if (WowLua::lua_tointeger) {
                             returnValue = WowLua::lua_tointeger(L, -1); 
                         } else {
                             returnValue = static_cast<int>(WowLua::lua_tonumber(L, -1));
                         }
                     } else if (resultType == LUA_TNIL_LOCAL) {
                         // If Lua returns nil, treat it as 0 (e.g., empty slot ID)
                         returnValue = 0;
                     } else {
                         // If it's neither number nor nil, it's an error
                         std::string typeErr = "Expected number or nil result for int conversion, got type " + std::to_string(resultType);
                         WowLua::lua_settop(L, top_before); // Clean before throw
                         throw LuaException(typeErr);
                     }
                 } else if constexpr (std::is_same_v<T, std::string>) {
                     if (!WowLua::lua_tolstring) throw LuaException("lua_tolstring unavailable");
                     if (resultType != LUA_TSTRING_LOCAL) {
                         std::string typeErr = "Expected string result, got type " + std::to_string(resultType);
                         WowLua::lua_settop(L, top_before); // Clean before throw
                         throw LuaException(typeErr);
                     }
                     size_t len = 0;
                     const char* str = WowLua::lua_tolstring(L, -1, &len);
                     returnValue = std::string(str ? str : "", len);
                 } else {
                     WowLua::lua_settop(L, top_before); // Clean before static_assert/error
                     static_assert(!sizeof(T), "Unsupported return type for ExecuteString<T>");
                 }

                 WowLua::lua_settop(L, top_before); // Pop the result by restoring the original top
                 return returnValue;

             } catch (...) {
                  WowLua::lua_settop(L, top_before); // Ensure stack is restored on exception during retrieval
                  throw;
             }
        } // End of else block for non-void types
    }

    // Execute a Lua string without expecting a return value
    // Throws LuaException on error
    void ExecuteStringNoResult(const std::string& script);

    // Get the raw Lua state (use with caution)
    lua_State* GetState();

} // namespace LuaExecutor