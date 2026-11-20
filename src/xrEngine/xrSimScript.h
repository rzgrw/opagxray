#pragma once

#include "xrScriptEngine/ScriptExporter.hpp"

// Lua: local ok, text = xrsim_verb(verb, payload) -- ai.freeplay.* verbs only.
struct ENGINE_API xrsim_script_registrator
{
    DECLARE_SCRIPT_REGISTER_FUNCTION();
};
