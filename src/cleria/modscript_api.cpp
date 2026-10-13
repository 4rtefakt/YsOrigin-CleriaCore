// Lua scripting for CleriaCore mods: the sandbox and the cleria.* API (build/shell_re/LUA_API.md is
// the reference for modders; keep the two in step). Every function reaches the game through Host;
// nothing here duplicates a C++ layer: json is src/cleria/json, the socket src/cleria/net/websocket,
// the treasure text src/cleria/content, the events src/cleria/mod_events.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <new>

#include "cleria/net/websocket.hpp"
#include "cleria/net/socket.hpp"
#include "cleria/content.hpp"
#include "cleria/json.hpp"
#include "cleria/modscript.hpp"

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

namespace cleria::script {

namespace fs = std::filesystem;

namespace {

Mod& M(lua_State* L) { return **static_cast<Mod**>(lua_getextraspace(L)); }

char g_null_tag;              // cleria.json.null (a light userdata)
const char* kArrayMt = "cleria.json.array";
const char* kWsMt = "cleria.net.websocket";

// ---- small helpers ---------------------------------------------------------------------------------

std::string str_arg(lua_State* L, int i) {
    std::size_t n = 0;
    const char* s = luaL_checklstring(L, i, &n);
    return std::string(s, n);
}

// A name a script may use as a storage / setting key: [A-Za-z0-9_.-], 1..64.
std::string key_arg(lua_State* L, int i) {
    std::string k = str_arg(L, i);
    bool ok = !k.empty() && k.size() <= 64;
    for (char c : k) ok = ok && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-');
    if (!ok) luaL_argerror(L, i, "a key is 1-64 characters of A-Z a-z 0-9 _ . -");
    return k;
}

// The optional last argument of the widgets / draw calls: a string (the description) or a table.
std::string opt_field_s(lua_State* L, int t, const char* k, const std::string& def) {
    if (!lua_istable(L, t)) return def;
    lua_getfield(L, lua_absindex(L, t), k);
    std::string v = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : def;
    lua_pop(L, 1);
    return v;
}
double opt_field_n(lua_State* L, int t, const char* k, double def) {
    if (!lua_istable(L, t)) return def;
    lua_getfield(L, lua_absindex(L, t), k);
    double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}
bool opt_field_b(lua_State* L, int t, const char* k, bool def) {
    if (!lua_istable(L, t)) return def;
    lua_getfield(L, lua_absindex(L, t), k);
    bool v = lua_isnil(L, -1) ? def : lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return v;
}
std::string desc_of(lua_State* L, int i) {
    if (lua_type(L, i) == LUA_TSTRING) return lua_tostring(L, i);
    return opt_field_s(L, i, "desc", "");
}

std::uint32_t pack(std::uint32_t rgb, double alpha) {
    const std::uint32_t a = std::uint32_t(std::lround(std::clamp(alpha, 0.0, 1.0) * 255.0));
    return ((rgb >> 16) & 0xFF) | (((rgb >> 8) & 0xFF) << 8) | ((rgb & 0xFF) << 16) | (a << 24);
}
// A colour from opts: `color` = 0xRRGGBB or a kit name ("gold", "warn", ...), `alpha` 0..1.
std::uint32_t color_of(lua_State* L, int t, std::uint32_t def_rgb) {
    std::uint32_t rgb = def_rgb;
    t = lua_absindex(L, t);
    if (lua_istable(L, t)) {
        lua_getfield(L, t, "color");
        if (lua_isinteger(L, -1)) {
            rgb = std::uint32_t(lua_tointeger(L, -1)) & 0xFFFFFFu;
        } else if (lua_type(L, -1) == LUA_TSTRING) {
            const auto named = M(L).host().named_colors();
            const auto it = named.find(lua_tostring(L, -1));
            if (it != named.end()) rgb = (it->second & 0xFF) << 16 | (it->second & 0xFF00) | ((it->second >> 16) & 0xFF);
        }
        lua_pop(L, 1);
    }
    return pack(rgb, opt_field_n(L, t, "alpha", 1.0));
}
int font_of(lua_State* L, int t) {
    const std::string f = opt_field_s(L, t, "font", "body");
    if (f == "medium") return 1;
    if (f == "bold") return 2;
    if (f == "cond") return 3;
    if (f == "cond_medium") return 4;
    return 0;
}

// ---- json <-> Lua ------------------------------------------------------------------------------------

void push_json(lua_State* L, const json::Value& v, int depth = 0) {
    luaL_checkstack(L, 4, "json too deep");   // the parser bounds the depth (256)
    switch (v.type()) {
        case json::Type::Null: lua_pushlightuserdata(L, &g_null_tag); break;
        case json::Type::Bool: lua_pushboolean(L, v.as_bool()); break;
        case json::Type::Int: lua_pushinteger(L, lua_Integer(v.as_int())); break;
        case json::Type::Double: lua_pushnumber(L, v.as_double()); break;
        case json::Type::String: lua_pushlstring(L, v.as_string().data(), v.as_string().size()); break;
        case json::Type::Array: {
            const json::Array& a = v.as_array();
            lua_createtable(L, int(a.size()), 0);
            for (std::size_t i = 0; i < a.size(); ++i) {
                push_json(L, a[i], depth + 1);
                lua_rawseti(L, -2, lua_Integer(i + 1));
            }
            luaL_setmetatable(L, kArrayMt);
            break;
        }
        case json::Type::Object: {
            const json::Object& o = v.as_object();
            lua_createtable(L, 0, int(o.size()));
            for (const auto& [k, x] : o) {
                lua_pushlstring(L, k.data(), k.size());
                push_json(L, x, depth + 1);
                lua_rawset(L, -3);
            }
            break;
        }
    }
}

bool is_array_table(lua_State* L, int i, lua_Integer* n_out) {
    if (lua_getmetatable(L, i)) {
        luaL_getmetatable(L, kArrayMt);
        const bool marked = lua_rawequal(L, -1, -2);
        lua_pop(L, 2);
        if (marked) { *n_out = lua_Integer(lua_rawlen(L, i)); return true; }
    }
    // unmarked: an array when its keys are exactly 1..n (n > 0)
    lua_Integer count = 0, max = 0;
    bool only_ints = true;
    lua_pushnil(L);
    while (lua_next(L, i) != 0) {
        lua_pop(L, 1);
        if (lua_isinteger(L, -1) && lua_tointeger(L, -1) >= 1) { ++count; max = std::max(max, lua_tointeger(L, -1)); }
        else only_ints = false;
    }
    *n_out = max;
    return only_ints && count > 0 && count == max;
}

json::Value to_json(lua_State* L, int i, int depth) {
    i = lua_absindex(L, i);
    if (depth > 128) luaL_error(L, "json.encode: nested too deep (a cycle?)");
    switch (lua_type(L, i)) {
        case LUA_TNIL: return json::Value();
        case LUA_TBOOLEAN: return json::Value(lua_toboolean(L, i) != 0);
        case LUA_TNUMBER:
            if (lua_isinteger(L, i)) return json::Value(static_cast<long long>(lua_tointeger(L, i)));
            return json::Value(double(lua_tonumber(L, i)));
        case LUA_TSTRING: {
            std::size_t n = 0;
            const char* s = lua_tolstring(L, i, &n);
            return json::Value(std::string(s, n));
        }
        case LUA_TLIGHTUSERDATA:
            if (lua_touserdata(L, i) == &g_null_tag) return json::Value();
            break;
        case LUA_TTABLE: {
            luaL_checkstack(L, 6, "json.encode: too deep");
            lua_Integer n = 0;
            if (is_array_table(L, i, &n)) {
                json::Value a = json::Value::array();
                for (lua_Integer k = 1; k <= n; ++k) {
                    lua_rawgeti(L, i, k);
                    a.push_back(to_json(L, -1, depth + 1));
                    lua_pop(L, 1);
                }
                return a;
            }
            json::Value o = json::Value::object();
            lua_pushnil(L);
            while (lua_next(L, i) != 0) {
                std::string key;
                if (lua_type(L, -2) == LUA_TSTRING) key = lua_tostring(L, -2);
                else if (lua_isinteger(L, -2)) key = std::to_string(lua_tointeger(L, -2));   // {[5857605]=x} -> "5857605"
                else luaL_error(L, "json.encode: an object key must be a string or an integer");
                o.obj()[key] = to_json(L, -1, depth + 1);
                lua_pop(L, 1);
            }
            return o;
        }
        default: break;
    }
    luaL_error(L, "json.encode: cannot encode a %s", luaL_typename(L, i));
    return {};
}

int l_json_encode(lua_State* L) {
    luaL_checkany(L, 1);
    json::WriteOptions wo;
    wo.indent = int(opt_field_n(L, 2, "indent", -1));
    const std::string s = json::write(to_json(L, 1, 0), wo);
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}
int l_json_decode(lua_State* L) {
    const std::string s = str_arg(L, 1);
    json::ParseOptions po;
    po.allow_nan = true;
    json::ParseError pe;
    const auto v = json::parse(s, &pe, po);
    if (!v) {
        lua_pushnil(L);
        lua_pushstring(L, pe.message.c_str());
        return 2;
    }
    push_json(L, *v);
    return 1;
}
int l_json_array(lua_State* L) {
    if (lua_isnoneornil(L, 1)) {
        lua_settop(L, 0);
        lua_newtable(L);
    } else {
        luaL_checktype(L, 1, LUA_TTABLE);
        lua_settop(L, 1);
    }
    luaL_setmetatable(L, kArrayMt);
    return 1;
}
int l_json_is_array(lua_State* L) {
    if (!lua_istable(L, 1)) { lua_pushboolean(L, 0); return 1; }
    lua_Integer n = 0;
    lua_pushboolean(L, is_array_table(L, 1, &n));
    return 1;
}

// ---- the sandbox -------------------------------------------------------------------------------------

int l_print(lua_State* L) {
    std::string line;
    const int n = lua_gettop(L);
    for (int i = 1; i <= n; ++i) {
        if (i > 1) line += "\t";
        line += luaL_tolstring(L, i, nullptr);
        lua_pop(L, 1);
    }
    Mod& m = M(L);
    m.host().log(m.id(), line);
    m.add_log(line);
    return 0;
}

// load(chunk [, chunkname [, mode [, env]]]): text chunks only (a binary chunk can crash the VM).
int l_load(lua_State* L) {
    const int env = lua_isnone(L, 4) ? 0 : 4;
    std::size_t n = 0;
    const char* s = nullptr;
    std::string buf;
    if (lua_type(L, 1) == LUA_TSTRING) {
        s = lua_tolstring(L, 1, &n);
    } else {
        luaL_checktype(L, 1, LUA_TFUNCTION);   // a reader function: concatenate its pieces
        for (;;) {
            lua_pushvalue(L, 1);
            lua_call(L, 0, 1);
            if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
            if (lua_type(L, -1) != LUA_TSTRING) luaL_error(L, "reader function must return a string");
            std::size_t k = 0;
            const char* p = lua_tolstring(L, -1, &k);
            buf.append(p, k);
            lua_pop(L, 1);
        }
        s = buf.data();
        n = buf.size();
    }
    const char* name = luaL_optstring(L, 2, "=(load)");
    if (luaL_loadbufferx(L, s, n, name, "t") != LUA_OK) {
        lua_pushnil(L);
        lua_insert(L, -2);
        return 2;
    }
    if (env) {
        lua_pushvalue(L, env);
        if (!lua_setupvalue(L, -2, 1)) lua_pop(L, 1);
    }
    return 1;
}

int l_collectgarbage(lua_State* L) {
    const std::string opt = luaL_optstring(L, 1, "collect");
    if (opt == "count") {
        const int k = lua_gc(L, LUA_GCCOUNT), b = lua_gc(L, LUA_GCCOUNTB);
        lua_pushnumber(L, double(k) + double(b) / 1024.0);
        return 1;
    }
    if (opt == "collect") { lua_gc(L, LUA_GCCOLLECT); lua_pushinteger(L, 0); return 1; }
    if (opt == "step") { lua_pushboolean(L, lua_gc(L, LUA_GCSTEP, int(luaL_optinteger(L, 2, 0)))); return 1; }
    return luaL_error(L, "collectgarbage(\"%s\") is not available to mods", opt.c_str());
}

// require("a.b"): <mod>/a/b.lua, run once, its result cached; nothing outside the mod folder.
int l_require(lua_State* L) {
    const std::string name = str_arg(L, 1);
    bool ok = !name.empty() && name.size() < 128 && name.front() != '.' && name.back() != '.';
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        ok = ok && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || (c == '.' && name[i - 1] != '.'));
    }
    if (!ok) return luaL_error(L, "require: bad module name '%s' (letters, digits, _ - and dots)", name.c_str());
    lua_getfield(L, LUA_REGISTRYINDEX, "cleria.loaded");
    lua_getfield(L, -1, name.c_str());
    if (!lua_isnil(L, -1)) {
        if (lua_type(L, -1) == LUA_TLIGHTUSERDATA) return luaL_error(L, "require: '%s' requires itself (a loop)", name.c_str());
        return 1;
    }
    lua_pop(L, 1);
    std::string rel = name;
    for (char& c : rel) if (c == '.') c = '/';
    rel += ".lua";
    std::string err;
    const auto text = M(L).read_file(rel, &err);
    if (!text) return luaL_error(L, "require: module '%s' not found in the mod folder (%s)", name.c_str(), err.c_str());
    const std::string chunk = "@" + rel;
    if (luaL_loadbufferx(L, text->data(), text->size(), chunk.c_str(), "t") != LUA_OK) return lua_error(L);
    lua_pushlightuserdata(L, &g_null_tag);   // "loading" (a loop guard)
    lua_setfield(L, -3, name.c_str());
    lua_pushstring(L, name.c_str());
    lua_call(L, 1, 1);
    if (lua_isnil(L, -1)) { lua_pop(L, 1); lua_pushboolean(L, 1); }
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, name.c_str());
    return 1;
}

void set_nil(lua_State* L, const char* tbl, const char* k) {
    lua_getglobal(L, tbl);
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        lua_setfield(L, -2, k);
    }
    lua_pop(L, 1);
}

// ---- cleria.* --------------------------------------------------------------------------------------

int l_log(lua_State* L) { return l_print(L); }

int l_notice(lua_State* L) {
    Mod& m = M(L);
    const std::string text = luaL_tolstring(L, 1, nullptr);
    lua_pop(L, 1);
    const std::string head = lua_isnoneornil(L, 2) ? m.name() : str_arg(L, 2);
    m.host().notice(m.id(), head, text);
    return 0;
}

int l_time(lua_State* L) { lua_pushnumber(L, M(L).host().now()); return 1; }
int l_unix_time(lua_State* L) {
    using namespace std::chrono;
    lua_pushnumber(L, duration<double>(system_clock::now().time_since_epoch()).count());
    return 1;
}

int l_read(lua_State* L) {
    std::string err;
    const auto t = M(L).read_file(str_arg(L, 1), &err);
    if (!t) { lua_pushnil(L); lua_pushstring(L, err.c_str()); return 2; }
    lua_pushlstring(L, t->data(), t->size());
    return 1;
}
int l_exists(lua_State* L) {
    Mod& m = M(L);
    const fs::path p = m.safe_path(m.root(), str_arg(L, 1), nullptr);
    std::error_code ec;
    lua_pushboolean(L, !p.empty() && fs::is_regular_file(p, ec));
    return 1;
}

int l_command(lua_State* L) {
    Mod& m = M(L);
    std::string name = str_arg(L, 1);
    bool ok = !name.empty() && name.size() <= 32;
    for (char c : name) ok = ok && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
    if (!ok) return luaL_argerror(L, 1, "a command name is 1-32 characters of A-Z a-z 0-9 _");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (auto it = m.commands_.find(name); it != m.commands_.end()) luaL_unref(L, LUA_REGISTRYINDEX, it->second);
    lua_pushvalue(L, 2);
    m.commands_[name] = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

// ---- cleria.events ---------------------------------------------------------------------------------

const char* const kKinds[] = {"room_enter", "flag_changed", "chest_opened", "item_obtained", "death",
                              "new_game", "save", "load", "tick", "frame", "boss_defeated", "item_used",
                              "door_opened", "blessing_bought", "enemy_killed", "pickup_collected",
                              // API 7 (events::Kind::Named)
                              "retry", "level_up", "state_changed", "floor_changed", "player_hit", "enemy_hit", "skill_used",
                              "boost", "statue_used", "attack", "jump", "land", "enemy_spawned", "script_started", "input", "interact"};

int l_on(lua_State* L) {
    Mod& m = M(L);
    const std::string kind = str_arg(L, 1);
    bool known = false;
    for (const char* k : kKinds) known = known || kind == k;
    if (!known) return luaL_argerror(L, 1, ("unknown event '" + kind + "' (see LUA_API.md: room_enter, flag_changed, chest_opened, "
                                             "item_obtained, death, new_game, save, load, tick, frame, boss_defeated, item_used, door_opened, blessing_bought, "
                                             "enemy_killed, pickup_collected; API 7: retry, level_up, state_changed, floor_changed, player_hit, "
                                             "enemy_hit, skill_used, boost, statue_used, attack, jump, land, enemy_spawned, script_started, input)").c_str());
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    const int id = m.next_handler_++;
    m.handlers_.push_back({id, kind, ref});
    lua_pushinteger(L, id);
    return 1;
}
int l_off(lua_State* L) {
    Mod& m = M(L);
    const lua_Integer id = luaL_checkinteger(L, 1);
    for (auto it = m.handlers_.begin(); it != m.handlers_.end(); ++it)
        if (it->id == id) {
            luaL_unref(L, LUA_REGISTRYINDEX, it->ref);
            m.handlers_.erase(it);
            lua_pushboolean(L, 1);
            return 1;
        }
    lua_pushboolean(L, 0);
    return 1;
}
// emit("item_obtained", {item=, count=, source=}): report a grant the mod made itself.
int l_emit(lua_State* L) {
    const std::string kind = str_arg(L, 1);
    if (kind != "item_obtained") return luaL_argerror(L, 1, "only \"item_obtained\" may be emitted by a mod");
    luaL_checktype(L, 2, LUA_TTABLE);
    events::Event e;
    e.kind = events::Kind::ItemObtained;
    e.item = int(opt_field_n(L, 2, "item", -1));
    e.count = int(opt_field_n(L, 2, "count", 1));
    e.source = opt_field_s(L, 2, "source", M(L).id());
    e.script = opt_field_s(L, 2, "script", "");
    if (e.item < 0) return luaL_argerror(L, 2, "item_obtained needs item");
    M(L).host().emit(e);
    return 0;
}

// ---- cleria.game / cleria.player -----------------------------------------------------------------------

int checked_index(lua_State* L, int arg, int n, const char* what) {
    const lua_Integer i = luaL_checkinteger(L, arg);
    if (i < 0 || i >= n) luaL_error(L, "%s index %d out of range (0..%d)", what, int(i), n - 1);
    return int(i);
}

int l_flag(lua_State* L) {
    const int i = checked_index(L, 1, 0x200, "flag");
    const std::int32_t* f = M(L).host().flags();
    if (!f) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, f[i]);
    return 1;
}
int l_set_flag(lua_State* L) {
    const int i = checked_index(L, 1, 0x200, "flag");
    const lua_Integer v = luaL_checkinteger(L, 2);
    std::int32_t* f = M(L).host().flags();
    if (f) f[i] = std::int32_t(v);
    lua_pushboolean(L, f != nullptr);
    return 1;
}
int l_item(lua_State* L) {
    const lua_Integer id = luaL_checkinteger(L, 1);
    Host& h = M(L).host();
    if (id >= 0 && id < content::kRetailItems) {
        const std::int32_t* f = h.flags();
        if (!f) { lua_pushnil(L); return 1; }
        lua_pushinteger(L, f[id]);
    } else if (id >= content::kNewItemFirst && id <= content::kNewItemLast) {
        lua_pushinteger(L, h.new_item_count(int(id)));
    } else {
        return luaL_error(L, "item id %d: retail items are 0..0x7F, mod items 0x100..0x1FF", int(id));
    }
    return 1;
}
int l_item_name(lua_State* L) {
    const std::string n = M(L).host().item_name(int(luaL_checkinteger(L, 1)));
    lua_pushstring(L, n.c_str());
    return 1;
}
int l_upgrade(lua_State* L) {
    const int i = checked_index(L, 1, 54, "upgrade");
    const std::int32_t* a = M(L).host().upgrades();
    if (!a) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, a[i]);
    return 1;
}
int l_set_upgrade(lua_State* L) {
    const int i = checked_index(L, 1, 54, "upgrade");
    const lua_Integer v = luaL_checkinteger(L, 2);
    std::int32_t* a = M(L).host().upgrades();
    if (a) a[i] = std::int32_t(v);
    return 0;
}
int l_warp_unlocked(lua_State* L) {
    const int i = checked_index(L, 1, 24, "warp");
    const std::uint8_t* w = M(L).host().warps();
    if (!w) { lua_pushnil(L); return 1; }
    lua_pushboolean(L, w[i] != 0);
    return 1;
}
int l_set_warp_unlocked(lua_State* L) {
    const int i = checked_index(L, 1, 24, "warp");
    std::uint8_t* w = M(L).host().warps();
    if (w) w[i] = lua_toboolean(L, 2) ? 1 : 0;
    return 0;
}
int l_warp_to(lua_State* L) {
    const int i = checked_index(L, 1, 24, "warp");
    M(L).host().warp(i);
    return 0;
}
// save(slot) -> boolean: the goddess statue's save into slot 0..63, when the game can be saved now.
int l_game_save(lua_State* L) {
    const lua_Integer s = luaL_checkinteger(L, 1);
    lua_pushboolean(L, s >= 0 && s < 64 && M(L).host().save(int(s)));
    return 1;
}
// unlock_character(c): Character Select offers character c for the rest of this session, whatever the clear
// data says (1 Yunica, 2 Hugo, 3 Toal; only Toal is ever locked on a New Game). Not saved.
int l_unlock_character(lua_State* L) { M(L).host().unlock_character(int(luaL_checkinteger(L, 1))); return 0; }
int l_room(lua_State* L) { lua_pushstring(L, M(L).host().room().c_str()); return 1; }
int l_scene(lua_State* L) {
    const std::string r = M(L).host().room();
    int n = 0;
    if (r.size() > 2 && (r[0] == 'S' || r[0] == 's') && r[1] == '_')
        for (std::size_t i = 2; i < r.size() && r[i] >= '0' && r[i] <= '9'; ++i) n = n * 10 + (r[i] - '0');
    lua_pushinteger(L, n);
    return 1;
}
int l_character(lua_State* L) { lua_pushinteger(L, M(L).host().character()); return 1; }
int l_frozen(lua_State* L) { lua_pushboolean(L, M(L).host().frozen()); return 1; }
int l_in_game(lua_State* L) { lua_pushboolean(L, M(L).host().in_game()); return 1; }
int l_chests(lua_State* L) {
    lua_newtable(L);
    for (const auto& [script, c] : M(L).host().chests()) {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, c.first);
        lua_setfield(L, -2, "flag");
        lua_pushstring(L, c.second.c_str());
        lua_setfield(L, -2, "object");
        lua_setfield(L, -2, script.c_str());
    }
    return 1;
}
int l_exp_factor(lua_State* L) {
    Mod& m = M(L);
    if (m.exp_ref_) luaL_unref(L, LUA_REGISTRYINDEX, m.exp_ref_);
    m.exp_ref_ = 0;
    if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_pushvalue(L, 1);
        m.exp_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    return 0;
}

int push_stat(lua_State* L, Host::Stat s, bool integer = true) {
    const double v = M(L).host().stat(s);
    if (integer) lua_pushinteger(L, lua_Integer(std::llround(v)));
    else lua_pushnumber(L, v);
    return 1;
}
int l_hp(lua_State* L) { return push_stat(L, Host::Stat::Hp); }
int l_max_hp(lua_State* L) { return push_stat(L, Host::Stat::MaxHp); }
int l_sp(lua_State* L) { return push_stat(L, Host::Stat::Sp); }
int l_level(lua_State* L) { return push_stat(L, Host::Stat::Level); }
int l_exp(lua_State* L) { return push_stat(L, Host::Stat::Exp, false); }
int l_weapon_tier(lua_State* L) { return push_stat(L, Host::Stat::WeaponTier); }
int l_set_hp(lua_State* L) { M(L).host().set_stat(Host::Stat::Hp, double(luaL_checkinteger(L, 1))); return 0; }
int l_add_sp(lua_State* L) {
    Host& h = M(L).host();
    h.set_stat(Host::Stat::Sp, h.stat(Host::Stat::Sp) + double(luaL_checkinteger(L, 1)));
    return 0;
}
int l_set_level(lua_State* L) { M(L).host().set_stat(Host::Stat::Level, double(luaL_checkinteger(L, 1))); return 0; }
int l_set_weapon_tier(lua_State* L) { M(L).host().set_stat(Host::Stat::WeaponTier, double(luaL_checkinteger(L, 1))); return 0; }
int l_refresh(lua_State* L) { M(L).host().refresh(); return 0; }
// add_exp(n): EXP as a kill credits it (the level-up loop runs); no filter and no factor is applied.
int l_add_exp(lua_State* L) {
    const double n = luaL_checknumber(L, 1);
    if (std::isfinite(n) && n > 0) M(L).host().add_exp(std::min(n, 9999999.0));
    return 0;
}
// stats() -> table | nil: everything the game holds of the player's numbers (LUA_API.md §4.1).
int l_stats(lua_State* L) {
    PlayerInfo p;
    if (!M(L).host().player_info(p)) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 24);
    auto n = [&](const char* k, double v) { lua_pushnumber(L, v); lua_setfield(L, -2, k); };
    auto i = [&](const char* k, lua_Integer v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); };
    i("level", p.level); i("hp", p.hp); i("max_hp", p.max_hp); i("sp", p.sp); i("weapon_tier", p.weapon_tier);
    n("exp", p.exp); n("exp_next", p.exp_next); n("atk", p.atk); n("def", p.def); n("luck", p.luck);
    n("base_max_hp", p.base_max_hp); n("base_atk", p.base_atk); n("base_def", p.base_def);
    n("mp", p.mp); n("max_mp", p.max_mp); n("boost", p.boost);
    lua_pushboolean(L, p.boosting); lua_setfield(L, -2, "boosting");
    n("atk_buff", p.buff[0]); n("def_buff", p.buff[1]); n("mp_buff", p.buff[2]);
    i("atk_buff_ticks", p.buff_timer[0]); i("def_buff_ticks", p.buff_timer[1]); i("mp_buff_ticks", p.buff_timer[2]);
    n("x", p.pos[0]); n("y", p.pos[1]); n("z", p.pos[2]);
    return 1;
}
const char* const kModStats[] = {"max_hp", "atk", "def"};
int mod_stat_arg(lua_State* L) {
    const std::string s = str_arg(L, 1);
    for (int k = 0; k < 3; ++k)
        if (s == kModStats[k]) return k;
    return luaL_argerror(L, 1, "a modifier is of \"max_hp\", \"atk\" or \"def\"");
}
// set_modifier(stat, add [, mul]): this mod's modifier of a derived stat, kept until it is set again;
// (0, 1) or nil removes it. A value that is not finite, or a mul outside 0..100, raises an error.
int l_set_modifier(lua_State* L) {
    const int k = mod_stat_arg(L);
    const double add = luaL_optnumber(L, 2, 0.0), mul = luaL_optnumber(L, 3, 1.0);
    if (!std::isfinite(add) || std::fabs(add) > 9999.0) return luaL_argerror(L, 2, "add must be within -9999..9999");
    if (!std::isfinite(mul) || mul < 0.0 || mul > 100.0) return luaL_argerror(L, 3, "mul must be within 0..100");
    Mod& m = M(L);
    m.host().set_modifier(m.id(), k, add, mul);
    return 0;
}
int l_modifier(lua_State* L) {
    Mod& m = M(L);
    const auto [add, mul] = m.host().modifier(m.id(), mod_stat_arg(L));
    lua_pushnumber(L, add);
    lua_pushnumber(L, mul);
    return 2;
}
// spawn_pickup(item, x, y, z [, {value = n}]) -> boolean: a collectable pickup thrown up from that point.
int l_spawn_pickup(lua_State* L) {
    const lua_Integer id = luaL_checkinteger(L, 1);
    const double x = luaL_checknumber(L, 2), y = luaL_checknumber(L, 3), z = luaL_checknumber(L, 4);
    const double v = lua_istable(L, 5) ? opt_field_n(L, 5, "value", -1.0) : -1.0;
    if (!((id >= 0 && id < content::kRetailItems) || (id >= content::kNewItemFirst && id <= content::kNewItemLast)))
        return luaL_error(L, "spawn_pickup: item id %d: retail items are 0..0x7F, mod items 0x100..0x1FF", int(id));
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return luaL_error(L, "spawn_pickup: the position is not finite");
    lua_pushboolean(L, M(L).host().spawn_pickup(int(id), float(x), float(y), float(z),
                                                 std::isfinite(v) && v >= 0 ? std::min(v, 999999.0) : -1.0));
    return 1;
}

// ---- cleria.content --------------------------------------------------------------------------------

int l_add_filter(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    M(L).filters_.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}
// cleria.shop.add_filter(fn): the blessing shop's queries (xso.hpp ShopQuery; LUA_API.md §5.1).
int l_shop_add_filter(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    M(L).shop_filters_.push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}
// cleria.combat.add_filter(kind, fn): the game's EXP / SP / damage / drop / pickup numbers (CombatQuery; LUA_API.md §5.2).
const char* const kCombatKindNames[kCombatKinds] = {"exp", "sp", "damage", "drop", "pickup"};
int l_combat_add_filter(lua_State* L) {
    const std::string kind = str_arg(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    for (int k = 0; k < kCombatKinds; ++k)
        if (kind == kCombatKindNames[k]) {
            lua_pushvalue(L, 2);
            M(L).combat_filters_[k].push_back(luaL_ref(L, LUA_REGISTRYINDEX));
            return 0;
        }
    return luaL_argerror(L, 1, "a combat filter is of \"exp\", \"sp\", \"damage\", \"drop\" or \"pickup\"");
}
int l_give(lua_State* L) {
    const lua_Integer id = luaL_checkinteger(L, 1);
    const lua_Integer n = luaL_optinteger(L, 2, 1);
    Mod& m = M(L);
    const std::string src = luaL_optstring(L, 3, m.id().c_str());
    if (!((id >= 0 && id < content::kRetailItems) || (id >= content::kNewItemFirst && id <= content::kNewItemLast)))
        return luaL_error(L, "give: item id %d: retail items are 0..0x7F, mod items 0x100..0x1FF", int(id));
    m.host().give(int(id), int(std::clamp<lua_Integer>(n, 1, 99)), src);
    return 0;
}
int l_treasure_text(lua_State* L) {
    const std::string t = content::treasure_text(str_arg(L, 1), str_arg(L, 2), int(luaL_optinteger(L, 3, 1)));
    lua_pushlstring(L, t.data(), t.size());
    return 1;
}

// ---- cleria.save / cleria.settings / cleria.data --------------------------------------------------------

int l_save_get(lua_State* L) {
    Mod& m = M(L);
    const std::string k = key_arg(L, 1);
    auto* st = m.host().save_storage(m.id());
    if (st) {
        const auto it = st->find(k);
        if (it != st->end()) {
            json::ParseOptions po;
            po.allow_nan = true;
            if (const auto v = json::parse(it->second, nullptr, po)) { push_json(L, *v); return 1; }
        }
    }
    lua_settop(L, 2);   // the default (or nil)
    return 1;
}
int l_save_set(lua_State* L) {
    Mod& m = M(L);
    const std::string k = key_arg(L, 1);
    auto* st = m.host().save_storage(m.id());
    if (!st) { lua_pushboolean(L, 0); return 1; }
    if (lua_isnoneornil(L, 2)) st->erase(k);
    else (*st)[k] = json::write(to_json(L, 2, 0));
    lua_pushboolean(L, 1);
    return 1;
}
int l_save_keys(lua_State* L) {
    Mod& m = M(L);
    lua_newtable(L);
    if (auto* st = m.host().save_storage(m.id())) {
        lua_Integer i = 0;
        for (const auto& [k, v] : *st) {
            lua_pushstring(L, k.c_str());
            lua_rawseti(L, -2, ++i);
        }
    }
    return 1;
}
int l_save_ready(lua_State* L) {
    Host& h = M(L).host();
    lua_pushboolean(L, h.in_game() && h.save_ready());
    return 1;
}
// set_profile(name): this run's saves go to <save dir>/profiles/<name>/ ("" / nil = the normal folder).
int l_save_set_profile(lua_State* L) {
    lua_pushboolean(L, M(L).host().set_save_profile(lua_isnoneornil(L, 1) ? std::string() : str_arg(L, 1)));
    return 1;
}
int l_save_profile(lua_State* L) { lua_pushstring(L, M(L).host().save_profile().c_str()); return 1; }

int l_setting_get(lua_State* L) {
    Mod& m = M(L);
    const std::string k = key_arg(L, 1);
    const auto v = m.host().setting(m.id(), k);
    if (!v) { lua_settop(L, 2); return 1; }
    switch (lua_type(L, 2)) {
        case LUA_TBOOLEAN: lua_pushboolean(L, *v == "1" || *v == "true" || *v == "on" || *v == "yes"); break;
        case LUA_TNUMBER: {
            lua_pushlstring(L, v->data(), v->size());
            if (!lua_stringtonumber(L, lua_tostring(L, -1))) { lua_pop(L, 1); lua_pushvalue(L, 2); }
            else lua_remove(L, -2);
            break;
        }
        default: lua_pushlstring(L, v->data(), v->size()); break;
    }
    return 1;
}
int l_setting_set(lua_State* L) {
    Mod& m = M(L);
    const std::string k = key_arg(L, 1);
    std::string v;
    switch (lua_type(L, 2)) {
        case LUA_TBOOLEAN: v = lua_toboolean(L, 2) ? "1" : "0"; break;
        case LUA_TNUMBER: case LUA_TSTRING: v = lua_tostring(L, 2); break;
        default: return luaL_argerror(L, 2, "a setting is a string, a number or a boolean");
    }
    for (char c : v) if (static_cast<unsigned char>(c) < 0x20) return luaL_argerror(L, 2, "a setting is one line of text");
    lua_pushboolean(L, m.host().set_setting(m.id(), k, v));
    return 1;
}

fs::path data_file(lua_State* L, int arg) {
    Mod& m = M(L);
    const fs::path dir = m.host().data_dir(m.id());
    if (dir.empty()) return {};
    std::string err;
    const fs::path p = m.safe_path(dir, str_arg(L, arg), &err);
    if (p.empty()) luaL_error(L, "data: %s", err.c_str());
    return p;
}
int l_data_read(lua_State* L) {
    const fs::path p = data_file(L, 1);
    std::error_code ec;
    if (p.empty() || !fs::is_regular_file(p, ec)) { lua_pushnil(L); return 1; }
    if (fs::file_size(p, ec) > M(L).limits().file_bytes) return luaL_error(L, "data: file too big");
    std::ifstream f(p, std::ios::binary);
    const std::string s{std::istreambuf_iterator<char>(f), {}};
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}
int l_data_write(lua_State* L) {
    const fs::path p = data_file(L, 1);
    const std::string s = str_arg(L, 2);
    if (p.empty()) { lua_pushboolean(L, 0); return 1; }
    if (s.size() > M(L).limits().file_bytes) return luaL_error(L, "data: file too big");
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(s.data(), std::streamsize(s.size()));
        if (!f) { lua_pushboolean(L, 0); return 1; }
    }
    fs::rename(tmp, p, ec);
    if (ec) fs::remove(tmp, ec);
    lua_pushboolean(L, !ec);
    return 1;
}
int l_data_exists(lua_State* L) {
    const fs::path p = data_file(L, 1);
    std::error_code ec;
    lua_pushboolean(L, !p.empty() && fs::is_regular_file(p, ec));
    return 1;
}
int l_data_remove(lua_State* L) {
    const fs::path p = data_file(L, 1);
    std::error_code ec;
    lua_pushboolean(L, !p.empty() && fs::remove(p, ec));
    return 1;
}

// ---- cleria.hud / cleria.ui ----------------------------------------------------------------------------

Mod& drawing(lua_State* L) {
    Mod& m = M(L);
    if (!m.drawing_) luaL_error(L, "draw calls only work inside a hud layer function");
    return m;
}
int l_d_text(lua_State* L) {
    Mod& m = drawing(L);
    const float x = float(luaL_checknumber(L, 2)), y = float(luaL_checknumber(L, 3));
    const std::string t = luaL_tolstring(L, 4, nullptr);
    lua_pop(L, 1);
    m.host().draw_text(x, y, t, float(opt_field_n(L, 5, "size", 16.0)), font_of(L, 5), color_of(L, 5, 0xEAE6DA));
    return 0;
}
int l_d_text_size(lua_State* L) {
    Mod& m = M(L);
    const std::string t = luaL_tolstring(L, 2, nullptr);
    lua_pop(L, 1);
    const auto [w, h] = m.host().text_size(t, float(opt_field_n(L, 3, "size", 16.0)), font_of(L, 3));
    lua_pushnumber(L, w);
    lua_pushnumber(L, h);
    return 2;
}
int l_d_rect(lua_State* L) {
    Mod& m = drawing(L);
    const float x = float(luaL_checknumber(L, 2)), y = float(luaL_checknumber(L, 3));
    const float w = float(luaL_checknumber(L, 4)), h = float(luaL_checknumber(L, 5));
    const bool fill = opt_field_b(L, 6, "fill", true);
    m.host().draw_rect(x, y, x + w, y + h, color_of(L, 6, 0x16150F), float(opt_field_n(L, 6, "rounding", 0)),
                       fill ? 0.0f : float(opt_field_n(L, 6, "thickness", 1.0)));
    return 0;
}
int l_d_line(lua_State* L) {
    Mod& m = drawing(L);
    m.host().draw_line(float(luaL_checknumber(L, 2)), float(luaL_checknumber(L, 3)), float(luaL_checknumber(L, 4)),
                       float(luaL_checknumber(L, 5)), color_of(L, 6, 0xEAE6DA), float(opt_field_n(L, 6, "thickness", 1.0)));
    return 0;
}
int l_d_circle(lua_State* L) {
    Mod& m = drawing(L);
    const bool fill = opt_field_b(L, 5, "fill", true);
    m.host().draw_circle(float(luaL_checknumber(L, 2)), float(luaL_checknumber(L, 3)), float(luaL_checknumber(L, 4)),
                         color_of(L, 5, 0xEAE6DA), fill ? 0.0f : float(opt_field_n(L, 5, "thickness", 1.0)));
    return 0;
}
int l_d_image(lua_State* L) {
    Mod& m = drawing(L);
    const std::string path = str_arg(L, 2);
    int& h = m.images_[path];
    if (h == 0) {
        std::string err;
        const fs::path p = m.safe_path(m.root(), path, &err);
        h = p.empty() ? -1 : m.host().load_image(p);
        if (h == 0) h = -1;
        if (h < 0) m.host().log(m.id(), "image " + path + " could not be loaded" + (err.empty() ? "" : ": " + err));
    }
    if (h < 0) return 0;
    const float x = float(luaL_checknumber(L, 3)), y = float(luaL_checknumber(L, 4));
    const float w = float(luaL_checknumber(L, 5)), hh = float(luaL_checknumber(L, 6));
    m.host().draw_image(h, x, y, x + w, y + hh, pack(0xFFFFFF, opt_field_n(L, 7, "alpha", 1.0)));
    return 0;
}

int l_layer(lua_State* L) {
    Mod& m = M(L);
    const std::string name = str_arg(L, 1);
    if (auto it = m.layers_.find(name); it != m.layers_.end()) {
        luaL_unref(L, LUA_REGISTRYINDEX, it->second);
        m.layers_.erase(it);
    }
    if (!lua_isnoneornil(L, 2)) {
        luaL_checktype(L, 2, LUA_TFUNCTION);
        lua_pushvalue(L, 2);
        m.layers_[name] = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    return 0;
}

int l_page(lua_State* L) {
    Mod& m = M(L);
    const std::string title = str_arg(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    m.pages_.push_back({title, luaL_ref(L, LUA_REGISTRYINDEX)});
    return 0;
}

int l_icon(lua_State* L) {
    Mod& m = M(L);
    const lua_Integer id = luaL_checkinteger(L, 1);
    if (id < 0x200 || id > 0x3FF) return luaL_argerror(L, 1, "an icon id of a mod is 0x200..0x3FF (outside every item and flag)");
    std::string err;
    const fs::path p = m.safe_path(m.root(), str_arg(L, 2), &err);
    std::error_code ec;
    if (p.empty() || !fs::is_regular_file(p, ec)) return luaL_error(L, "icon: %s", err.empty() ? "no such file" : err.c_str());
    m.icons_.push_back({int(id), p});
    m.host().register_icon(int(id), p);
    return 0;
}

// The page widgets: valid only while the page function runs.
Mod& paging(lua_State* L) {
    Mod& m = M(L);
    if (!m.list_) luaL_error(L, "page widgets only work inside the page function");
    return m;
}
bool enabled_of(lua_State* L, int i) { return opt_field_b(L, i, "enabled", true); }

int l_p_section(lua_State* L) {
    Mod& m = paging(L);
    m.host().ui_section(m.list_, str_arg(L, 2));
    return 0;
}
int l_p_toggle(lua_State* L) {
    Mod& m = paging(L);
    bool v = lua_toboolean(L, 3) != 0;
    const bool ch = m.host().ui_toggle(m.list_, str_arg(L, 2), &v, desc_of(L, 4), enabled_of(L, 4));
    lua_pushboolean(L, v);
    lua_pushboolean(L, ch);
    return 2;
}
// A printf format with ONE float conversion (ui::slider formats the value with it).
bool safe_float_fmt(const std::string& f) {
    int conv = 0;
    for (std::size_t i = 0; i < f.size(); ++i) {
        if (f[i] != '%') continue;
        if (i + 1 < f.size() && f[i + 1] == '%') { ++i; continue; }
        std::size_t j = i + 1;
        while (j < f.size() && std::strchr("-+ #0", f[j])) ++j;
        while (j < f.size() && std::isdigit(static_cast<unsigned char>(f[j]))) ++j;
        if (j < f.size() && f[j] == '.') { ++j; while (j < f.size() && std::isdigit(static_cast<unsigned char>(f[j]))) ++j; }
        if (j >= f.size() || !std::strchr("fFgGeE", f[j])) return false;
        ++conv;
        i = j;
    }
    return conv == 1 && f.size() < 40;
}
int l_p_slider(lua_State* L) {
    Mod& m = paging(L);
    float v = float(luaL_checknumber(L, 3));
    const float lo = float(luaL_checknumber(L, 4)), hi = float(luaL_checknumber(L, 5));
    const float step = float(luaL_optnumber(L, 6, 1.0));
    std::string fmt = luaL_optstring(L, 7, "%g");
    if (!safe_float_fmt(fmt)) fmt = "%g";
    const bool ch = m.host().ui_slider(m.list_, str_arg(L, 2), &v, lo, hi, step, fmt, desc_of(L, 8), enabled_of(L, 8));
    lua_pushnumber(L, v);
    lua_pushboolean(L, ch);
    return 2;
}
int l_p_choice(lua_State* L) {
    Mod& m = paging(L);
    int idx = int(luaL_checkinteger(L, 3)) - 1;
    luaL_checktype(L, 4, LUA_TTABLE);
    std::vector<std::string> items;
    const lua_Integer n = luaL_len(L, 4);
    for (lua_Integer i = 1; i <= n && i <= 64; ++i) {
        lua_rawgeti(L, 4, i);
        items.push_back(luaL_tolstring(L, -1, nullptr));
        lua_pop(L, 2);
    }
    if (!items.empty()) idx = std::clamp(idx, 0, int(items.size()) - 1);
    const bool ch = m.host().ui_choice(m.list_, str_arg(L, 2), &idx, items, desc_of(L, 5), enabled_of(L, 5));
    lua_pushinteger(L, idx + 1);
    lua_pushboolean(L, ch);
    return 2;
}
int l_p_button(lua_State* L) {
    Mod& m = paging(L);
    lua_pushboolean(L, m.host().ui_button(m.list_, str_arg(L, 2), desc_of(L, 3), enabled_of(L, 3)));
    return 1;
}
int l_p_text_field(lua_State* L) {
    Mod& m = paging(L);
    const std::string label = str_arg(L, 2);
    const std::string value = lua_isnoneornil(L, 3) ? std::string() : str_arg(L, 3);
    // the kit edits a string by address: one stable slot per page + label, synced while not edited
    std::string& slot = m.fields_[std::to_string(m.page_) + "\x1f" + label];
    slot = value;   // the kit edits its own buffer and writes the slot only when an edit is kept
    const bool kept = m.host().ui_text_field(m.list_, label, &slot, desc_of(L, 4), opt_field_b(L, 4, "secret", false),
                                             opt_field_s(L, 4, "empty", "(not set)"));
    lua_pushlstring(L, slot.data(), slot.size());
    lua_pushboolean(L, kept);
    return 2;
}
int l_p_info(lua_State* L) {
    Mod& m = paging(L);
    const std::string value = lua_isnoneornil(L, 3) ? std::string() : std::string(luaL_tolstring(L, 3, nullptr));
    if (!lua_isnoneornil(L, 3)) lua_pop(L, 1);
    m.host().ui_info(m.list_, str_arg(L, 2), value, color_of(L, 4, 0xCFCABD), desc_of(L, 4));
    return 0;
}

// ---- cleria.net.websocket --------------------------------------------------------------------------------

struct WsUd {
    net::WebSocket* ws = nullptr;
    Mod* mod = nullptr;
};

WsUd& ws_check(lua_State* L) {
    WsUd* u = static_cast<WsUd*>(luaL_checkudata(L, 1, kWsMt));
    if (!u->ws) luaL_error(L, "the websocket was closed for good");
    return *u;
}
int l_ws_new(lua_State* L) {
    Mod& m = M(L);
    if (int(m.sockets_.size()) >= m.limits().websockets)
        return luaL_error(L, "a mod may keep at most %d websockets", m.limits().websockets);
    WsUd* u = static_cast<WsUd*>(lua_newuserdatauv(L, sizeof(WsUd), 0));
    new (u) WsUd{};
    u->ws = new net::WebSocket();
    u->mod = &m;
    m.sockets_.push_back(u);
    luaL_setmetatable(L, kWsMt);
    return 1;
}
int l_ws_gc(lua_State* L) {
    WsUd* u = static_cast<WsUd*>(luaL_checkudata(L, 1, kWsMt));
    if (u->mod) {
        auto& v = u->mod->sockets_;
        v.erase(std::remove(v.begin(), v.end(), static_cast<void*>(u)), v.end());
    }
    delete u->ws;   // closes; joins its threads (at most ~2.5 s)
    u->ws = nullptr;
    u->mod = nullptr;
    return 0;
}
int l_ws_open(lua_State* L) {
    WsUd& u = ws_check(L);
    const std::string addr = str_arg(L, 2);
    if (net::candidate_urls(addr).empty()) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "not a websocket address");
        return 2;
    }
    u.ws->open(addr);
    lua_pushboolean(L, 1);
    return 1;
}
int l_ws_send(lua_State* L) {
    WsUd& u = ws_check(L);
    lua_pushboolean(L, u.ws->send(str_arg(L, 2)));
    return 1;
}
int l_ws_close(lua_State* L) {
    ws_check(L).ws->close();
    return 0;
}
int l_ws_poll(lua_State* L) {
    WsUd& u = ws_check(L);
    lua_newtable(L);
    lua_Integer i = 0;
    for (const net::WebSocket::Event& e : u.ws->poll()) {
        lua_createtable(L, 0, 3);
        using K = net::WebSocket::Event::Kind;
        lua_pushstring(L, e.kind == K::Opened ? "open" : e.kind == K::Message ? "message" : "close");
        lua_setfield(L, -2, "kind");
        lua_pushlstring(L, e.text.data(), e.text.size());
        lua_setfield(L, -2, "text");
        lua_pushboolean(L, e.error);
        lua_setfield(L, -2, "error");
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}
int l_ws_state(lua_State* L) {
    using S = net::WebSocket::State;
    static const char* kName[] = {"idle", "connecting", "open", "closing", "closed"};
    const S s = ws_check(L).ws->state();
    lua_pushstring(L, kName[int(s)]);
    return 1;
}
int l_ws_url(lua_State* L) {
    lua_pushstring(L, ws_check(L).ws->url().c_str());
    return 1;
}

// ---- cleria.speedrun -----------------------------------------------------------------------------------

int l_modification(lua_State* L) {
    Mod& m = M(L);
    if (m.speedrun_ref_) luaL_unref(L, LUA_REGISTRYINDEX, m.speedrun_ref_);
    m.speedrun_ref_ = 0;
    if (lua_type(L, 1) == LUA_TSTRING) {   // a fixed line: wrap it
        lua_pushvalue(L, 1);
        lua_pushcclosure(L, [](lua_State* L2) -> int { lua_pushvalue(L2, lua_upvalueindex(1)); return 1; }, 1);
        m.speedrun_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
    } else if (!lua_isnoneornil(L, 1)) {
        luaL_checktype(L, 1, LUA_TFUNCTION);
        lua_pushvalue(L, 1);
        m.speedrun_ref_ = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    return 0;
}

void set_funcs(lua_State* L, const luaL_Reg* r) { luaL_setfuncs(L, r, 0); }

}  // namespace

// ---- registration ------------------------------------------------------------------------------------

void open_sandbox(lua_State* L, Mod& m) {
    (void)m;
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(L, 1);
    luaL_requiref(L, LUA_COLIBNAME, luaopen_coroutine, 1);
    lua_pop(L, 1);
    // os: only the clock and the calendar (no execute / exit / getenv / remove / rename / tmpname)
    luaL_requiref(L, "cleria.os_full", luaopen_os, 0);
    lua_newtable(L);
    for (const char* k : {"clock", "time", "date", "difftime"}) {
        lua_getfield(L, -2, k);
        lua_setfield(L, -2, k);
    }
    lua_setglobal(L, "os");
    lua_pop(L, 1);
    // the base library without file access; load is text-only; print goes to the mod's log
    for (const char* k : {"dofile", "loadfile"}) {
        lua_pushnil(L);
        lua_setglobal(L, k);
    }
    lua_register(L, "load", l_load);
    lua_register(L, "print", l_print);
    lua_register(L, "collectgarbage", l_collectgarbage);
    set_nil(L, "string", "dump");
    // require: the mod's own modules
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "cleria.loaded");
    lua_register(L, "require", l_require);
    // the json array marker
    luaL_newmetatable(L, kArrayMt);
    lua_pushstring(L, "array");
    lua_setfield(L, -2, "__name");
    lua_pop(L, 1);
}

#include "modscript_api7.inl"

void push_json_text(lua_State* L, const std::string& json_text) {
    if (const auto j = json::parse(json_text)) push_json(L, *j);
    else lua_pushnil(L);
}

void open_api(lua_State* L, Mod& m) {
    lua_newtable(L);   // cleria
    lua_pushinteger(L, kApiVersion);
    lua_setfield(L, -2, "api_version");
    lua_createtable(L, 0, 4);
    lua_pushstring(L, m.id().c_str());
    lua_setfield(L, -2, "id");
    lua_pushstring(L, m.name().c_str());
    lua_setfield(L, -2, "name");
    lua_pushstring(L, m.version().c_str());
    lua_setfield(L, -2, "version");
    lua_setfield(L, -2, "mod");
    static const luaL_Reg top[] = {{"log", l_log}, {"notice", l_notice}, {"time", l_time}, {"unix_time", l_unix_time},
                                   {"read", l_read}, {"exists", l_exists}, {"command", l_command}, {nullptr, nullptr}};
    set_funcs(L, top);

    static const luaL_Reg ev[] = {{"on", l_on}, {"off", l_off}, {"emit", l_emit}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, ev);
    lua_setfield(L, -2, "events");

    static const luaL_Reg game[] = {{"flag", l_flag}, {"set_flag", l_set_flag}, {"item", l_item}, {"item_name", l_item_name},
                                    {"upgrade", l_upgrade}, {"set_upgrade", l_set_upgrade}, {"warp_unlocked", l_warp_unlocked},
                                    {"set_warp_unlocked", l_set_warp_unlocked}, {"warp_to", l_warp_to}, {"room", l_room},
                                    {"scene", l_scene}, {"character", l_character}, {"frozen", l_frozen},
                                    {"in_game", l_in_game}, {"chests", l_chests}, {"exp_factor", l_exp_factor},
                                    {"save", l_game_save}, {"unlock_character", l_unlock_character},
                                    {"spawn_pickup", l_spawn_pickup},
                                    {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, game);
    lua_setfield(L, -2, "game");

    static const luaL_Reg player[] = {{"hp", l_hp}, {"max_hp", l_max_hp}, {"set_hp", l_set_hp}, {"sp", l_sp},
                                      {"add_sp", l_add_sp}, {"level", l_level}, {"set_level", l_set_level}, {"exp", l_exp},
                                      {"weapon_tier", l_weapon_tier}, {"set_weapon_tier", l_set_weapon_tier},
                                      {"refresh", l_refresh}, {"add_exp", l_add_exp}, {"stats", l_stats},
                                      {"set_modifier", l_set_modifier}, {"modifier", l_modifier}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, player);
    lua_setfield(L, -2, "player");

    static const luaL_Reg cont[] = {{"add_filter", l_add_filter}, {"give", l_give}, {"treasure_text", l_treasure_text},
                                    {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, cont);
    lua_setfield(L, -2, "content");

    static const luaL_Reg shop[] = {{"add_filter", l_shop_add_filter}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, shop);
    lua_setfield(L, -2, "shop");

    static const luaL_Reg combat[] = {{"add_filter", l_combat_add_filter}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, combat);
    lua_setfield(L, -2, "combat");

    static const luaL_Reg save[] = {{"get", l_save_get}, {"set", l_save_set}, {"keys", l_save_keys}, {"ready", l_save_ready},
                                    {"set_profile", l_save_set_profile}, {"profile", l_save_profile},
                                    {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, save);
    lua_setfield(L, -2, "save");

    static const luaL_Reg sett[] = {{"get", l_setting_get}, {"set", l_setting_set}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, sett);
    lua_setfield(L, -2, "settings");

    static const luaL_Reg data[] = {{"read", l_data_read}, {"write", l_data_write}, {"exists", l_data_exists},
                                    {"remove", l_data_remove}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, data);
    lua_setfield(L, -2, "data");

    static const luaL_Reg hud[] = {{"layer", l_layer}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, hud);
    lua_setfield(L, -2, "hud");

    static const luaL_Reg ui[] = {{"page", l_page}, {"icon", l_icon}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, ui);
    lua_newtable(L);   // ui.colors: the kit's named colours as 0xRRGGBB
    for (const auto& [name, c] : m.host().named_colors()) {
        lua_pushinteger(L, lua_Integer(((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF)));
        lua_setfield(L, -2, name.c_str());
    }
    lua_setfield(L, -2, "colors");
    lua_setfield(L, -2, "ui");

    static const luaL_Reg js[] = {{"encode", l_json_encode}, {"decode", l_json_decode}, {"array", l_json_array},
                                  {"is_array", l_json_is_array}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, js);
    lua_pushlightuserdata(L, &g_null_tag);
    lua_setfield(L, -2, "null");
    lua_setfield(L, -2, "json");

    // net: the websocket type
    luaL_newmetatable(L, kWsMt);
    static const luaL_Reg wsm[] = {{"open", l_ws_open}, {"send", l_ws_send}, {"close", l_ws_close}, {"poll", l_ws_poll},
                                   {"state", l_ws_state}, {"url", l_ws_url}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, wsm);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, l_ws_gc);
    lua_setfield(L, -2, "__gc");
    lua_pushstring(L, "websocket");
    lua_setfield(L, -2, "__name");
    lua_pop(L, 1);
    static const luaL_Reg net[] = {{"websocket", l_ws_new}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, net);
    lua_setfield(L, -2, "net");

    static const luaL_Reg sr[] = {{"modification", l_modification}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, sr);
    lua_setfield(L, -2, "speedrun");
    open_api7(L);
    open_api7_game(L);
    open_api7_world(L);
    open_api7_writes(L);
    open_api7_input(L);
    open_api7_windows(L);
    open_api7_world2(L);
    open_api7_av(L);
    open_api7_puppet(L);
    open_api7_script(L);
    open_api7_combat(L);
    open_api7_net(L);

    lua_setglobal(L, "cleria");
}

void close_socket(void* ud) {
    WsUd* u = static_cast<WsUd*>(ud);
    if (u && u->ws) u->ws->close();
}

const char* event_name(events::Kind k) {
    switch (k) {
        case events::Kind::RoomEnter: return "room_enter";
        case events::Kind::FlagChanged: return "flag_changed";
        case events::Kind::ChestOpened: return "chest_opened";
        case events::Kind::ItemObtained: return "item_obtained";
        case events::Kind::PlayerDeath: return "death";
        case events::Kind::NewGame: return "new_game";
        case events::Kind::Save: return "save";
        case events::Kind::Load: return "load";
        case events::Kind::BossDefeated: return "boss_defeated";
        case events::Kind::ItemUsed: return "item_used";
        case events::Kind::DoorOpened: return "door_opened";
        case events::Kind::BlessingBought: return "blessing_bought";
        case events::Kind::EnemyKilled: return "enemy_killed";
        case events::Kind::PickupCollected: return "pickup_collected";
        case events::Kind::Named: return "named";
    }
    return "?";
}

void push_event(lua_State* L, const events::Event& e) {
    lua_createtable(L, 0, 6);
    auto s = [&](const char* k, const std::string& v) { lua_pushlstring(L, v.data(), v.size()); lua_setfield(L, -2, k); };
    auto n = [&](const char* k, lua_Integer v) { lua_pushinteger(L, v); lua_setfield(L, -2, k); };
    auto r = [&](const char* k, double v) {                    // an amount: a whole one is a Lua integer
        if (v == std::floor(v) && std::fabs(v) < 9.0e15) lua_pushinteger(L, lua_Integer(v));
        else lua_pushnumber(L, v);
        lua_setfield(L, -2, k);
    };
    s("kind", e.kind == events::Kind::Named ? e.name : std::string(event_name(e.kind)));
    s("room", e.room);
    switch (e.kind) {
        case events::Kind::RoomEnter: s("prev_room", e.prev_room); break;
        case events::Kind::FlagChanged: n("index", e.index); n("old", e.old_value); n("new", e.new_value); break;
        case events::Kind::ChestOpened: n("index", e.index); s("script", e.script); s("object", e.object); break;
        case events::Kind::ItemObtained: n("item", e.item); n("count", e.count); s("source", e.source); s("script", e.script); break;
        case events::Kind::NewGame: n("character", e.character); break;
        case events::Kind::Save: n("slot", e.slot); break;
        case events::Kind::Load: n("slot", e.slot); lua_pushboolean(L, e.retry); lua_setfield(L, -2, "retry"); break;
        case events::Kind::Named:
            for (const auto& [k, v] : e.num) r(k.c_str(), v);
            for (const auto& [k, v] : e.str) s(k.c_str(), v);
            break;
        case events::Kind::PlayerDeath: break;
        case events::Kind::BossDefeated: if (e.index >= 0) n("flag", e.index); break;
        case events::Kind::ItemUsed: n("item", e.item); break;
        case events::Kind::DoorOpened: n("flag", e.index); s("script", e.script); break;
        case events::Kind::BlessingBought:
            n("index", e.index); n("price", e.new_value);
            lua_pushboolean(L, e.old_value != 0); lua_setfield(L, -2, "suppressed");
            break;
        case events::Kind::EnemyKilled:
            n("cast", e.cast); n("level", e.level);
            r("exp", e.amount); n("sp", lua_Integer(e.amount2));
            lua_pushnumber(L, e.pos[0]); lua_setfield(L, -2, "x");
            lua_pushnumber(L, e.pos[1]); lua_setfield(L, -2, "y");
            lua_pushnumber(L, e.pos[2]); lua_setfield(L, -2, "z");
            break;
        case events::Kind::PickupCollected:
            n("item", e.item); s("what", e.source); n("ticks", e.count);
            r("value", e.amount);
            lua_pushboolean(L, e.old_value != 0); lua_setfield(L, -2, "suppressed");
            lua_pushnumber(L, e.pos[0]); lua_setfield(L, -2, "x");
            lua_pushnumber(L, e.pos[1]); lua_setfield(L, -2, "y");
            lua_pushnumber(L, e.pos[2]); lua_setfield(L, -2, "z");
            break;
    }
}

// ---- the game's calls that build API objects --------------------------------------------------------------

GrantDecision Mod::filter(const GrantQuery& q, const std::string& room) {
    GrantDecision d;
    for (std::size_t i = 0; i < filters_.size() && ok(); ++i) {
        lua_State* L = state();
        lua_createtable(L, 0, 10);
        static const char* kKind[] = {"store", "icon", "window"};
        lua_pushstring(L, kKind[int(q.kind)]);
        lua_setfield(L, -2, "kind");
        lua_pushinteger(L, q.sub);
        lua_setfield(L, -2, "sub");
        lua_pushinteger(L, q.index);
        lua_setfield(L, -2, "index");
        lua_pushinteger(L, q.old_value);
        lua_setfield(L, -2, "old");
        lua_pushinteger(L, q.value);
        lua_setfield(L, -2, "value");
        lua_pushlstring(L, q.text.data(), q.text.size());
        lua_setfield(L, -2, "text");
        lua_pushstring(L, q.script.c_str());
        lua_setfield(L, -2, "script");
        lua_pushstring(L, q.object.c_str());
        lua_setfield(L, -2, "object");
        lua_pushinteger(L, q.owner);
        lua_setfield(L, -2, "owner");
        lua_pushstring(L, room.c_str());
        lua_setfield(L, -2, "room");
        lua_rawgeti(L, LUA_REGISTRYINDEX, filters_[i]);
        lua_insert(L, -2);
        if (!pcall(1, 1, "filter")) return GrantDecision{};
        std::string action;
        if (lua_type(L, -1) == LUA_TSTRING) action = lua_tostring(L, -1);
        else if (lua_istable(L, -1)) action = opt_field_s(L, -1, "action", "pass");
        if (action == "suppress") {
            d.action = GrantDecision::Action::Suppress;
        } else if (action == "replace") {
            d.action = GrantDecision::Action::Replace;
            d.index = int(opt_field_n(L, -1, "index", q.index));
            d.value = std::int32_t(opt_field_n(L, -1, "value", q.value));
            d.text = opt_field_s(L, -1, "text", q.text);
        }
        lua_pop(L, 1);
        if (d.action != GrantDecision::Action::Pass) return d;
    }
    return d;
}

GrantDecision Mod::shop_filter(const ShopQuery& q, const std::string& room) {
    GrantDecision d;
    for (std::size_t i = 0; i < shop_filters_.size() && ok(); ++i) {
        lua_State* L = state();
        lua_createtable(L, 0, 8);
        static const char* kKind[] = {"price", "row", "owned", "buy"};
        lua_pushstring(L, kKind[int(q.kind)]);
        lua_setfield(L, -2, "kind");
        lua_pushinteger(L, q.sub);
        lua_setfield(L, -2, "sub");
        lua_pushinteger(L, q.index);
        lua_setfield(L, -2, "index");
        lua_pushinteger(L, q.value);
        lua_setfield(L, -2, "value");
        lua_pushlstring(L, q.text.data(), q.text.size());
        lua_setfield(L, -2, "text");
        lua_pushstring(L, q.script.c_str());
        lua_setfield(L, -2, "script");
        lua_pushstring(L, room.c_str());
        lua_setfield(L, -2, "room");
        lua_rawgeti(L, LUA_REGISTRYINDEX, shop_filters_[i]);
        lua_insert(L, -2);
        if (!pcall(1, 1, "shop filter")) return GrantDecision{};
        std::string action;
        if (lua_type(L, -1) == LUA_TSTRING) action = lua_tostring(L, -1);
        else if (lua_istable(L, -1)) action = opt_field_s(L, -1, "action", "pass");
        if (action == "suppress") {
            d.action = GrantDecision::Action::Suppress;
        } else if (action == "replace") {
            d.action = GrantDecision::Action::Replace;
            d.index = int(opt_field_n(L, -1, "index", q.index));
            d.value = std::int32_t(opt_field_n(L, -1, "value", q.value));
            d.text = opt_field_s(L, -1, "text", q.text);
        }
        lua_pop(L, 1);
        if (d.action != GrantDecision::Action::Pass) return d;
    }
    return d;
}

CombatDecision Mod::combat_filter(const CombatQuery& q, const std::string& room) {
    CombatDecision d;
    const std::vector<int>& fs = combat_filters_[int(q.kind)];
    for (std::size_t i = 0; i < fs.size() && ok(); ++i) {
        lua_State* L = state();
        lua_createtable(L, 0, int(q.num.size() + q.str.size()) + 5);
        lua_pushstring(L, kCombatKindNames[int(q.kind)]);
        lua_setfield(L, -2, "kind");
        auto num = [&](const char* k, double v) {              // a whole number is a Lua integer ("17", not "17.0")
            if (v == std::floor(v) && std::fabs(v) < 9.0e15) lua_pushinteger(L, lua_Integer(v));
            else lua_pushnumber(L, v);
            lua_setfield(L, -2, k);
        };
        num("value", q.value);
        if (q.item >= 0) num("item", q.item);
        if (q.kind == CombatQuery::Kind::Pickup) num("ticks", q.timer);
        for (const auto& [k, v] : q.num) num(k, v);
        for (const auto& [k, v] : q.str) { lua_pushlstring(L, v.data(), v.size()); lua_setfield(L, -2, k); }
        lua_pushstring(L, room.c_str());
        lua_setfield(L, -2, "room");
        lua_rawgeti(L, LUA_REGISTRYINDEX, fs[i]);
        lua_insert(L, -2);
        if (!pcall(1, 1, "combat filter")) return CombatDecision{};
        d.value = q.value; d.item = q.item; d.timer = q.timer;
        std::string action;
        if (lua_type(L, -1) == LUA_TNUMBER) {                  // a number: that value instead
            action = "replace";
            d.value = lua_tonumber(L, -1);
        } else if (lua_type(L, -1) == LUA_TSTRING) {
            action = lua_tostring(L, -1);
        } else if (lua_istable(L, -1)) {
            action = opt_field_s(L, -1, "action", "pass");
            d.value = opt_field_n(L, -1, "value", q.value);
            d.item = int(opt_field_n(L, -1, "item", q.item));
            d.timer = int(std::clamp(opt_field_n(L, -1, "ticks", q.timer), 0.0, 999999.0));
        }
        lua_pop(L, 1);
        if (!std::isfinite(d.value)) action = "pass";          // NaN / inf: the game's own value stands
        if (action == "suppress") d.action = CombatDecision::Action::Suppress;
        else if (action == "replace") d.action = CombatDecision::Action::Replace;
        if (d.action != CombatDecision::Action::Pass) return d;
    }
    return CombatDecision{};
}

void Mod::draw_hud(float w, float h, float scale) {
    if (!ok() || layers_.empty()) return;
    lua_State* L = state();
    static const luaL_Reg dm[] = {{"text", l_d_text}, {"text_size", l_d_text_size}, {"rect", l_d_rect}, {"line", l_d_line},
                                  {"circle", l_d_circle}, {"image", l_d_image}, {nullptr, nullptr}};
    std::vector<int> refs;
    for (const auto& [name, ref] : layers_) refs.push_back(ref);   // a layer may add / remove layers
    drawing_ = true;
    for (int ref : refs) {
        if (!ok()) break;
        lua_createtable(L, 0, 10);
        luaL_setfuncs(L, dm, 0);
        lua_pushnumber(L, w);
        lua_setfield(L, -2, "w");
        lua_pushnumber(L, h);
        lua_setfield(L, -2, "h");
        lua_pushnumber(L, scale);
        lua_setfield(L, -2, "scale");
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        lua_insert(L, -2);
        pcall(1, 0, "hud");
    }
    drawing_ = false;
}

void Mod::draw_page(std::size_t i, void* list) {
    if (!ok() || i >= pages_.size()) return;
    lua_State* L = state();
    static const luaL_Reg pm[] = {{"section", l_p_section}, {"toggle", l_p_toggle}, {"slider", l_p_slider},
                                  {"choice", l_p_choice}, {"button", l_p_button}, {"text_field", l_p_text_field},
                                  {"info", l_p_info}, {"text", l_p_text}, {"progress", l_p_progress}, {nullptr, nullptr}};
    lua_createtable(L, 0, 10);
    luaL_setfuncs(L, pm, 0);
    lua_rawgeti(L, LUA_REGISTRYINDEX, pages_[i].second);
    lua_insert(L, -2);
    list_ = list;
    page_ = i;
    pcall(1, 0, "page");
    list_ = nullptr;
}

}  // namespace cleria::script
