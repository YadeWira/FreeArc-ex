#include "fa/lua.h"

#include <algorithm>
#include <filesystem>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

namespace fa {
namespace {

lua_State* g_state = nullptr;

const char* const kEvents[] = {"ProgramStart", "ProgramDone", "CommandStart",
                               "CommandDone",  "ArchiveStart", "ArchiveDone",
                               "Error",        "Warning"};

// Same thin dispatcher the original builds for every event: on<Event>(handler)
// appends to a list, and calling the event function invokes every handler.
void register_event(lua_State* state, const std::string& event)
{
    const std::string handlers = "on" + event + "Handlers";
    const std::string code = handlers + " = {}\n" + "function on" + event +
                             "(handler) table.insert(" + handlers + ", handler) end\n" +
                             "function " + event + "(params) for _, handler in ipairs(" +
                             handlers + ") do handler(params) end end\n";
    if (luaL_dostring(state, code.c_str()) != 0) {
        const char* message = lua_tostring(state, -1);
        lua_pop(state, 1);
        (void)message;
    }
}

}  // namespace

bool lua_init(const std::string& config_dir, std::string& error)
{
    if (g_state != nullptr) return true;
    g_state = luaL_newstate();
    if (g_state == nullptr) {
        error = "cannot create the Lua state";
        return false;
    }
    luaL_openlibs(g_state);
    for (const char* event : kEvents) register_event(g_state, event);

    // Configuration scripts, in name order, like findFiles(configFilePlaces, "arc.*.lua").
    std::error_code ec;
    std::vector<std::filesystem::path> scripts;
    for (const auto& entry : std::filesystem::directory_iterator(config_dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind("arc.", 0) == 0 && name.size() > 4 &&
            name.substr(name.size() - 4) == ".lua") {
            scripts.push_back(entry.path());
        }
    }
    std::sort(scripts.begin(), scripts.end());
    for (const auto& script : scripts) {
        if (luaL_dofile(g_state, script.string().c_str()) != 0) {
            const char* message = lua_tostring(g_state, -1);
            error = "Lua error in '" + script.string() + "': " + (message ? message : "?");
            lua_pop(g_state, 1);
            return false;
        }
    }
    return true;
}

void lua_shutdown()
{
    if (g_state != nullptr) {
        lua_close(g_state);
        g_state = nullptr;
    }
}

bool lua_active()
{
    return g_state != nullptr;
}

void lua_event(const std::string& event, const LuaParams& params)
{
    if (g_state == nullptr) return;
    lua_getglobal(g_state, event.c_str());
    if (!lua_isfunction(g_state, -1)) {
        lua_pop(g_state, 1);
        return;
    }
    lua_newtable(g_state);
    for (const auto& [key, value] : params) {
        lua_pushstring(g_state, value.c_str());
        lua_setfield(g_state, -2, key.c_str());
    }
    if (lua_pcall(g_state, 1, 0, 0) != 0) {
        lua_pop(g_state, 1);  // a failing handler must not abort the archiver
    }
}

}  // namespace fa
