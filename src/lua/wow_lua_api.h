#pragma once

#include <cstdint>

// Define lua_State as an opaque struct pointer
struct lua_State;

// Define types used by WoW Lua functions (match standard Lua types where possible)
typedef double lua_Number;
typedef int lua_Integer; // WoW 3.3.5a seems to use standard int for integers
typedef int (*lua_CFunction) (lua_State *L);

// Define constants based on provided list
#define LUA_GLOBALSINDEX (-10002)

// Define function pointer types based on provided list and standard Lua signatures

// Basic Stack Operations
typedef int         (__cdecl* tLua_GetTop)(lua_State *L);
typedef void        (__cdecl* tLua_SetTop)(lua_State *L, int idx);
typedef int         (__cdecl* tLua_Type)(lua_State *L, int idx);

// Push Operations
typedef void        (__cdecl* tLua_PushString)(lua_State *L, const char *s);
typedef void        (__cdecl* tLua_PushInteger)(lua_State *L, lua_Integer n);
typedef void        (__cdecl* tLua_PushNumber)(lua_State *L, lua_Number n);
typedef void        (__cdecl* tLua_PushBoolean)(lua_State *L, int b);
// Note: Assuming standard C calling convention (__cdecl). Adjust if needed.
typedef void        (__cdecl* tLua_PushCClosure)(lua_State *L, lua_CFunction fn, int n);

// Get Operations
typedef const char* (__cdecl* tLua_ToLString)(lua_State *L, int idx, size_t *len);
typedef lua_Number  (__cdecl* tLua_ToNumber)(lua_State *L, int idx);
typedef lua_Integer (__cdecl* tLua_ToInteger)(lua_State *L, int idx);
typedef int         (__cdecl* tLua_ToBoolean)(lua_State *L, int idx); // Returns int (0 or 1)
typedef lua_CFunction (__cdecl* tLua_ToCFunction)(lua_State *L, int idx);

// Execution
typedef int         (__cdecl* tLua_PCall)(lua_State *L, int nargs, int nresults, int errfunc);
// Loading
typedef int         (__cdecl* tLua_LoadBuffer)(lua_State *L, const char *buff, size_t sz, const char *name);

// WoW Specific Functions
typedef int         (__cdecl* tFrameScript_Execute)(const char *code, const char *sourceName, int zero);
typedef void        (__cdecl* tLua_GetField_WoW)(lua_State* L, int index); // Key is pushed onto Lua stack before call
typedef void        (__cdecl* tLua_SetField_WoW)(lua_State *L, int index, const char *k); // Value is pushed before call?
// typedef bool        (__cdecl* tWow_GetGlobalStringVariable)(lua_State *L, const char *name, char** result); // Signature needs verification

// Define pointers for the Lua functions using the provided offsets
// These need to be defined and initialized in a .cpp file
namespace WowLua { 
    // Note: We get the Lua State directly from the provided address
    inline lua_State* GetLuaState() { 
        return *reinterpret_cast<lua_State**>(0x00D3F78C); 
    } 

    extern tLua_GetTop                  lua_gettop;
    extern tLua_SetTop                  lua_settop;
    extern tLua_PushString              lua_pushstring;
    extern tLua_PushInteger             lua_pushinteger;
    extern tLua_PushNumber              lua_pushnumber;
    extern tLua_ToLString               lua_tolstring;
    extern tLua_ToNumber                lua_tonumber;
    extern tLua_ToInteger               lua_tointeger;
    extern tLua_Type                    lua_type;
    extern tLua_PCall                   lua_pcall;
    extern tLua_PushBoolean             lua_pushboolean;
    extern tLua_PushCClosure            lua_pushcclosure;
    extern tLua_ToBoolean               lua_toboolean;
    extern tLua_ToCFunction             lua_tocfunction;
    extern tLua_LoadBuffer              lua_loadbuffer;

    // WoW Specific Wrappers/Functions
    extern tLua_GetField_WoW            lua_getfield_wow;
    extern tFrameScript_Execute         FrameScript_Execute;
    extern tLua_SetField_WoW            lua_setfield_wow;
    // Add others like RawGet helper or GetGlobalString if needed later

    // Initialization function to be called once
    void InitializeWowLuaFunctions();

} // namespace WowLua 