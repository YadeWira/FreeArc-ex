#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fa {

// FreeArc's Lua event hooks (legacy/haskell/Options.hs): a Lua 5.1 state that
// loads arc.*.lua from the configuration directory and calls the handlers
// registered for Program/Command/Archive Start/Done, Warning and Error.
bool lua_init(const std::string& config_dir, std::string& error);
void lua_shutdown();
bool lua_active();

using LuaParams = std::vector<std::pair<std::string, std::string>>;
void lua_event(const std::string& event, const LuaParams& params);

}  // namespace fa
