#include "stdafx.h"

#include "xrSimScript.h"

#include "xrSim/xrSimBridge.h"

#include "xrScriptEngine/script_space.hpp"

#include <string>

namespace
{
int push_reply(lua_State* luaState, bool ok, const std::string& text)
{
    lua_pushboolean(luaState, ok ? 1 : 0);
    lua_pushlstring(luaState, text.data(), text.size());
    return 2;
}

// xrsim_verb(verb, payload) -> ok, text. Scripts reach the freeplay runtime only.
int xrsim_verb(lua_State* luaState)
{
    if (lua_type(luaState, 1) != LUA_TSTRING || lua_type(luaState, 2) != LUA_TSTRING)
        return push_reply(luaState, false, "usage");

    size_t verbSize = 0;
    size_t payloadSize = 0;
    const char* verbText = lua_tolstring(luaState, 1, &verbSize);
    const char* payloadText = lua_tolstring(luaState, 2, &payloadSize);
    const std::string verb(verbText, verbSize);
    if (verb.rfind("ai.freeplay.", 0) != 0)
        return push_reply(luaState, false, "forbidden");

    bool ok = false;
    const std::string text = xrSim::HandleBridgeVerb(verb, std::string(payloadText, payloadSize), ok);
    return push_reply(luaState, ok, text);
}
} // namespace

void xrsim_script_registrator::script_register(lua_State* luaState)
{
    lua_pushcfunction(luaState, xrsim_verb);
    lua_setglobal(luaState, "xrsim_verb");
}
