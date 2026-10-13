// The cleria.* API added at level 7 (build/shell_re/API7.md, LUA_API.md §12). Textual part of
// modscript_api.cpp (inside its anonymous namespace, so it uses that file's helpers: M, str_arg, to_json,
// push_json, set_funcs ...). Kept apart so the level's additions read as one piece.

namespace {

// ---- timers: cleria.after / every / cancel -----------------------------------------------------------
// `play = true` counts play time (ticks while a game runs and the world is not frozen); otherwise real time.
int add_timer(lua_State* L, bool repeat) {
    Mod& m = M(L);
    const double s = luaL_checknumber(L, 1);
    if (!std::isfinite(s) || s < 0.0 || s > 86400.0 * 365.0) return luaL_argerror(L, 1, "seconds must be within 0..one year");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    bool play = false;
    if (lua_istable(L, 3)) {
        lua_getfield(L, 3, "play");
        play = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    if (m.timers_.size() >= 4096) return luaL_error(L, "too many timers (4096)");
    lua_pushvalue(L, 2);
    Mod::Timer t;
    t.id = m.next_timer_++;
    t.period = repeat ? std::max(s, 1.0 / 60.0) : 0.0;
    t.due = (play ? m.play_clock_ : m.host().now()) + (repeat ? t.period : s);
    t.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    t.play = play;
    m.timers_.push_back(t);
    lua_pushinteger(L, t.id);
    return 1;
}
int l_after(lua_State* L) { return add_timer(L, false); }
int l_every(lua_State* L) { return add_timer(L, true); }
int l_cancel(lua_State* L) {
    Mod& m = M(L);
    const lua_Integer id = luaL_checkinteger(L, 1);
    for (std::size_t i = 0; i < m.timers_.size(); ++i)
        if (m.timers_[i].id == id) {
            m.timers_[i].dead = true;                    // unref'd by run_timers (it may be the one running)
            lua_pushboolean(L, 1);
            return 1;
        }
    lua_pushboolean(L, 0);
    return 1;
}

// ---- cleria.rng(seed): a generator of the mod's own (xoshiro256**, seeded by splitmix64) --------------
const char* kRngMt = "cleria.rng";
struct Rng {
    std::uint64_t s[4];
    std::uint64_t next() {
        auto rotl = [](std::uint64_t x, int k) { return (x << k) | (x >> (64 - k)); };
        const std::uint64_t r = rotl(s[1] * 5, 7) * 9, t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 45);
        return r;
    }
    std::uint64_t below(std::uint64_t n) {               // unbiased, n >= 1
        const std::uint64_t lim = ~std::uint64_t(0) - (~std::uint64_t(0) % n);
        std::uint64_t v = next();
        while (v >= lim) v = next();
        return v % n;
    }
};
int l_rng(lua_State* L) {
    std::uint64_t seed = 0;
    if (lua_type(L, 1) == LUA_TSTRING) {                 // FNV-1a 64 of the text
        std::size_t n = 0;
        const char* p = lua_tolstring(L, 1, &n);
        seed = 1469598103934665603ull;
        for (std::size_t i = 0; i < n; ++i) seed = (seed ^ static_cast<unsigned char>(p[i])) * 1099511628211ull;
    } else {
        seed = std::uint64_t(luaL_checkinteger(L, 1));
    }
    Rng* r = static_cast<Rng*>(lua_newuserdatauv(L, sizeof(Rng), 0));
    for (std::uint64_t& x : r->s) {                      // splitmix64
        seed += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = seed;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        x = z ^ (z >> 31);
    }
    luaL_setmetatable(L, kRngMt);
    return 1;
}
Rng& rng_arg(lua_State* L) { return *static_cast<Rng*>(luaL_checkudata(L, 1, kRngMt)); }
int l_rng_int(lua_State* L) {                            // r:int(n) -> 1..n ; r:int(a, b) -> a..b
    Rng& r = rng_arg(L);
    lua_Integer a = 1, b = luaL_checkinteger(L, 2);
    if (!lua_isnoneornil(L, 3)) { a = b; b = luaL_checkinteger(L, 3); }
    if (b < a) return luaL_argerror(L, 2, "the range is empty");
    lua_pushinteger(L, a + lua_Integer(r.below(std::uint64_t(b - a) + 1)));
    return 1;
}
int l_rng_float(lua_State* L) {                          // [0, 1)
    lua_pushnumber(L, double(rng_arg(L).next() >> 11) * (1.0 / 9007199254740992.0));
    return 1;
}
int l_rng_shuffle(lua_State* L) {                        // in place (Fisher-Yates), returns the table
    Rng& r = rng_arg(L);
    luaL_checktype(L, 2, LUA_TTABLE);
    const lua_Integer n = lua_Integer(lua_rawlen(L, 2));
    for (lua_Integer i = n; i >= 2; --i) {
        const lua_Integer j = 1 + lua_Integer(r.below(std::uint64_t(i)));
        lua_rawgeti(L, 2, i);
        lua_rawgeti(L, 2, j);
        lua_rawseti(L, 2, i);
        lua_rawseti(L, 2, j);
    }
    lua_pushvalue(L, 2);
    return 1;
}
int l_rng_pick(lua_State* L) {                           // one element of an array (nil when it is empty)
    Rng& r = rng_arg(L);
    luaL_checktype(L, 2, LUA_TTABLE);
    const lua_Integer n = lua_Integer(lua_rawlen(L, 2));
    if (n < 1) { lua_pushnil(L); return 1; }
    lua_rawgeti(L, 2, 1 + lua_Integer(r.below(std::uint64_t(n))));
    return 1;
}

// ---- cleria.mods / cleria.bus: the mods see each other ------------------------------------------------
int l_mods_list(lua_State* L) {
    const std::vector<Host::ModInfo> v = M(L).host().mod_list();
    lua_createtable(L, int(v.size()), 0);
    for (std::size_t i = 0; i < v.size(); ++i) {
        lua_createtable(L, 0, 5);
        lua_pushlstring(L, v[i].id.data(), v[i].id.size()); lua_setfield(L, -2, "id");
        lua_pushlstring(L, v[i].name.data(), v[i].name.size()); lua_setfield(L, -2, "name");
        lua_pushlstring(L, v[i].version.data(), v[i].version.size()); lua_setfield(L, -2, "version");
        lua_pushboolean(L, v[i].active); lua_setfield(L, -2, "active");
        lua_pushboolean(L, v[i].scripted); lua_setfield(L, -2, "scripted");
        lua_rawseti(L, -2, lua_Integer(i + 1));
    }
    return 1;
}
int l_mods_is_active(lua_State* L) {
    const std::string id = str_arg(L, 1);
    bool on = false;
    for (const Host::ModInfo& m : M(L).host().mod_list()) on = on || (m.active && m.id == id);
    lua_pushboolean(L, on);
    return 1;
}
std::string topic_arg(lua_State* L, int i) {
    std::string k = str_arg(L, i);
    bool ok = !k.empty() && k.size() <= 64;
    for (char c : k) ok = ok && (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '-' || c == ':');
    if (!ok) luaL_argerror(L, i, "a topic is 1-64 characters of A-Z a-z 0-9 _ . - :");
    return k;
}
int l_bus_publish(lua_State* L) {                        // -> how many handlers of other mods got it
    Mod& m = M(L);
    const std::string topic = topic_arg(L, 1);
    const std::string text = json::write(lua_isnone(L, 2) ? json::Value() : to_json(L, 2, 0));
    if (text.size() > 1u << 20) return luaL_error(L, "a bus message is at most 1 MB of JSON");
    lua_pushinteger(L, m.host().bus_publish(m.id(), topic, text));
    return 1;
}
int l_bus_subscribe(lua_State* L) {
    Mod& m = M(L);
    const std::string topic = topic_arg(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    lua_pushvalue(L, 2);
    m.bus_subs_[topic].push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}
int l_bus_set(lua_State* L) {                            // a value every mod can read (kept for the process, not saved)
    Mod& m = M(L);
    const std::string key = topic_arg(L, 1);
    if (lua_isnoneornil(L, 2)) { m.host().bus_set(key, {}); return 0; }
    const std::string text = json::write(to_json(L, 2, 0));
    if (text.size() > 1u << 20) return luaL_error(L, "a bus value is at most 1 MB of JSON");
    m.host().bus_set(key, text);
    return 0;
}
int l_bus_get(lua_State* L) {
    const std::optional<std::string> v = M(L).host().bus_get(topic_arg(L, 1));
    if (!v || v->empty()) { lua_settop(L, 2); return 1; }                    // the default (or nil)
    if (const auto j = json::parse(*v)) push_json(L, *j);
    else lua_settop(L, 2);
    return 1;
}

// ---- small conveniences -------------------------------------------------------------------------------
int l_clipboard(lua_State* L) {
    const std::string t = str_arg(L, 1);
    if (t.size() > 1u << 16) return luaL_argerror(L, 1, "at most 64 KB of text");
    lua_pushboolean(L, M(L).host().set_clipboard(t));
    return 1;
}
int l_open_url(lua_State* L) {                           // http(s) only; the host asks the player first
    Mod& m = M(L);
    if (!m.list_) return luaL_error(L, "cleria.open_url works only from a page's button (the player asked for it)");
    const std::string u = str_arg(L, 1);
    const bool web = u.rfind("https://", 0) == 0 || u.rfind("http://", 0) == 0;
    bool clean = u.size() <= 2048;
    for (char c : u) clean = clean && static_cast<unsigned char>(c) > 0x20 && c != '"' && c != '<' && c != '>' && c != '\\' && c != '^' && c != '`';
    if (!web || !clean) return luaL_argerror(L, 1, "an http:// or https:// address");
    lua_pushboolean(L, m.host().open_url(m.id(), u));
    return 1;
}
int l_locale(lua_State* L) {
    const std::string s = M(L).host().locale();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

void open_api7(lua_State* L) {                           // the cleria table is on top of the stack
    static const luaL_Reg top[] = {{"after", l_after}, {"every", l_every}, {"cancel", l_cancel}, {"rng", l_rng},
                                   {"clipboard", l_clipboard}, {"open_url", l_open_url}, {"locale", l_locale}, {nullptr, nullptr}};
    set_funcs(L, top);
    static const luaL_Reg rm[] = {{"int", l_rng_int}, {"float", l_rng_float}, {"shuffle", l_rng_shuffle}, {"pick", l_rng_pick},
                                  {nullptr, nullptr}};
    luaL_newmetatable(L, kRngMt);
    lua_newtable(L);
    set_funcs(L, rm);
    lua_setfield(L, -2, "__index");
    lua_pushstring(L, kRngMt);
    lua_setfield(L, -2, "__name");
    lua_pop(L, 1);

    static const luaL_Reg mods[] = {{"list", l_mods_list}, {"is_active", l_mods_is_active}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, mods);
    lua_setfield(L, -2, "mods");
    static const luaL_Reg bus[] = {{"publish", l_bus_publish}, {"subscribe", l_bus_subscribe}, {"get", l_bus_get}, {"set", l_bus_set},
                                   {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, bus);
    lua_setfield(L, -2, "bus");
}

}  // namespace

namespace {

// ---- what the game is doing: cleria.game.state / room_title / floor / zone / difficulty / play_time -----------
int l_game_state(lua_State* L) {
    const std::string s = M(L).host().game_state();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}
int l_room_title(lua_State* L) {
    const std::string s = M(L).host().room_title();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}
// The floor a room's name starts with (" 4F - Wailing Blue", "12F Path 2"): 1..25, or -1 when it names none
// (the outer corridors, Rado's Annex, the summit).
int floor_of_title(const std::string& t) {
    std::size_t i = 0;
    while (i < t.size() && t[i] == ' ') ++i;
    int n = 0;
    std::size_t d = i;
    while (d < t.size() && std::isdigit(static_cast<unsigned char>(t[d])) && d - i < 2) n = n * 10 + (t[d++] - '0');
    if (d == i || d >= t.size() || t[d] != 'F' || n < 1 || n > 25) return -1;
    return n;
}
int l_game_floor(lua_State* L) {
    Mod& m = M(L);
    if (!m.host().in_game()) { lua_pushnil(L); return 1; }
    const int f = floor_of_title(m.host().room_title());
    if (f < 0) lua_pushnil(L); else lua_pushinteger(L, f);
    return 1;
}
int scene_of_room(const std::string& r) {                // "S_1001" -> 1001, anything else -> 0
    if (r.size() < 3 || (r[0] != 'S' && r[0] != 's') || r[1] != '_') return 0;
    int n = 0;
    for (std::size_t i = 2; i < r.size() && std::isdigit(static_cast<unsigned char>(r[i])); ++i) n = n * 10 + (r[i] - '0');
    return n;
}
int l_zone(lua_State* L) {                               // -> 1..7 and its name; nil outside the tower's rooms
    static const char* const kZone[] = {"Wailing Blue", "Flooded Prison", "Guilty Fire", "Silent Sands", "Blighted Blood",
                                        "Demonic Core", "Tower Summit"};
    Mod& m = M(L);
    const int z = m.host().in_game() ? scene_of_room(m.host().room()) / 1000 : 0;
    if (z < 1 || z > 7) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, z);
    lua_pushstring(L, kZone[z - 1]);
    return 2;
}
int l_difficulty(lua_State* L) {                         // -> 0..4 and its name (flags 203 / 143 / 144)
    static const char* const kDiff[] = {"Very Easy", "Easy", "Normal", "Hard", "Nightmare"};
    const std::int32_t* f = M(L).host().flags();
    if (!f) { lua_pushnil(L); return 1; }
    const int d = f[203] ? 0 : f[143] ? 1 : 2 + std::clamp(int(f[144]), 0, 2);
    lua_pushinteger(L, d);
    lua_pushstring(L, kDiff[d]);
    return 2;
}
int l_play_time(lua_State* L) {                          // seconds: the game's own counter (flag 199, ticks)
    const std::int32_t* f = M(L).host().flags();
    if (!f) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, double(f[199]) / 60.0);
    return 1;
}

void open_api7_game(lua_State* L) {                      // the cleria table is on top of the stack
    static const luaL_Reg game[] = {{"state", l_game_state}, {"room_title", l_room_title}, {"floor", l_game_floor}, {"zone", l_zone},
                                    {"difficulty", l_difficulty}, {"play_time", l_play_time}, {nullptr, nullptr}};
    lua_getfield(L, -1, "game");
    set_funcs(L, game);
    lua_pop(L, 1);
}

}  // namespace

namespace {

// ---- cleria.world: the monsters of the room, and where a world point is on the screen ------------------------
void push_actor(lua_State* L, const Host::ActorInfo& a) {
    lua_createtable(L, 0, 14);
    lua_pushinteger(L, a.id); lua_setfield(L, -2, "id");
    lua_pushinteger(L, a.cast); lua_setfield(L, -2, "cast");
    lua_pushinteger(L, a.level); lua_setfield(L, -2, "level");
    lua_pushlstring(L, a.name.data(), a.name.size()); lua_setfield(L, -2, "name");
    lua_pushnumber(L, a.hp); lua_setfield(L, -2, "hp");
    lua_pushnumber(L, a.max_hp); lua_setfield(L, -2, "max_hp");
    lua_pushnumber(L, a.atk); lua_setfield(L, -2, "atk");
    lua_pushnumber(L, a.def); lua_setfield(L, -2, "def");
    lua_pushnumber(L, a.pos[0]); lua_setfield(L, -2, "x");
    lua_pushnumber(L, a.pos[1]); lua_setfield(L, -2, "y");
    lua_pushnumber(L, a.pos[2]); lua_setfield(L, -2, "z");
    lua_pushnumber(L, a.height); lua_setfield(L, -2, "height");
    lua_pushnumber(L, a.yaw); lua_setfield(L, -2, "yaw");
    lua_pushboolean(L, a.hittable); lua_setfield(L, -2, "hittable");
}
int l_world_actors(lua_State* L) {
    const std::vector<Host::ActorInfo> v = M(L).host().actors();
    lua_createtable(L, int(v.size()), 0);
    for (std::size_t i = 0; i < v.size(); ++i) {
        push_actor(L, v[i]);
        lua_rawseti(L, -2, lua_Integer(i + 1));
    }
    return 1;
}
int l_world_actor(lua_State* L) {
    const lua_Integer id = luaL_checkinteger(L, 1);
    for (const Host::ActorInfo& a : M(L).host().actors())
        if (a.id == id) { push_actor(L, a); return 1; }
    lua_pushnil(L);
    return 1;
}
int l_world_to_screen(lua_State* L) {                    // -> fx, fy (0..1 of the game picture), or nil behind the camera
    float sx = 0, sy = 0;
    if (!M(L).host().to_screen(float(luaL_checknumber(L, 1)), float(luaL_checknumber(L, 2)), float(luaL_checknumber(L, 3)), &sx, &sy)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, sx);
    lua_pushnumber(L, sy);
    return 2;
}

void open_api7_world(lua_State* L) {                     // the cleria table is on top of the stack
    static const luaL_Reg world[] = {{"actors", l_world_actors}, {"actor", l_world_actor}, {"to_screen", l_world_to_screen},
                                     {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, world);
    lua_setfield(L, -2, "world");
}

}  // namespace

namespace {

// ---- writes of the player and the flow (Host::game_op) ---------------------------------------------------------
bool op(lua_State* L, const char* name, std::vector<double> num = {}, const std::string& text = {}, std::vector<double>* out = nullptr) {
    Mod& m = M(L);
    return m.host().game_op(m.id(), name, num, text, out);
}
double num_arg(lua_State* L, int i, double lo, double hi) {
    const double v = luaL_checknumber(L, i);
    if (!std::isfinite(v) || v < lo || v > hi) luaL_argerror(L, i, "out of range");
    return v;
}
int l_set_position(lua_State* L) {                       // (x, y, z [, facing in degrees]) -> placed on the floor under it
    std::vector<double> v = {num_arg(L, 1, -1e6, 1e6), num_arg(L, 2, -1e6, 1e6), num_arg(L, 3, -1e6, 1e6)};
    if (!lua_isnoneornil(L, 4)) v.push_back(num_arg(L, 4, -36000, 36000));
    lua_pushboolean(L, op(L, "set_position", v));
    return 1;
}
int l_set_mp(lua_State* L) { lua_pushboolean(L, op(L, "set_mp", {num_arg(L, 1, 0, 1e6)})); return 1; }
int l_set_boost(lua_State* L) { lua_pushboolean(L, op(L, "set_boost", {num_arg(L, 1, 0, 1e6)})); return 1; }
int l_set_exp(lua_State* L) { lua_pushboolean(L, op(L, "set_exp", {num_arg(L, 1, 0, 1e9)})); return 1; }
int l_set_invulnerable(lua_State* L) { lua_pushboolean(L, op(L, "invulnerable", {num_arg(L, 1, 0, 36000)})); return 1; }

const char* const kSlots[] = {"weapon", "armor", "leggings", "accessory", "magic", "held"};
int l_equipment(lua_State* L) {                          // -> {weapon =, armor =, leggings =, accessory =, magic =, held =} (item ids, -1 = none)
    std::vector<double> out;
    if (!op(L, "equipment", {}, {}, &out) || out.size() < 6) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 6);
    for (int i = 0; i < 6; ++i) { lua_pushinteger(L, lua_Integer(out[std::size_t(i)])); lua_setfield(L, -2, kSlots[i]); }
    return 1;
}
int l_equip(lua_State* L) {                              // (slot, item id | nil) -> true when it is now equipped
    const int slot = luaL_checkoption(L, 1, nullptr, std::array<const char*, 7>{"weapon", "armor", "leggings", "accessory", "magic", "held", nullptr}.data());
    const double item = lua_isnoneornil(L, 2) ? -1.0 : double(luaL_checkinteger(L, 2));
    lua_pushboolean(L, op(L, "equip", {double(slot), item}));
    return 1;
}
const char* const kStatus[] = {"poison", "heavy", "confuse", "curse"};
int l_status(lua_State* L) {                             // -> {poison = ticks left, ...} (0 = not on)
    std::vector<double> out;
    if (!op(L, "status", {}, {}, &out) || out.size() < 4) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 4);
    for (int i = 0; i < 4; ++i) { lua_pushinteger(L, lua_Integer(out[std::size_t(i)])); lua_setfield(L, -2, kStatus[i]); }
    return 1;
}
int l_set_status(lua_State* L) {                         // (name, on) : inflict it as a hit would, or end it
    const int which = luaL_checkoption(L, 1, nullptr, std::array<const char*, 5>{"poison", "heavy", "confuse", "curse", nullptr}.data());
    luaL_checkany(L, 2);
    lua_pushboolean(L, op(L, "set_status", {double(which), lua_toboolean(L, 2) ? 1.0 : 0.0}));
    return 1;
}
// The abilities the game keeps as flags: the double jump (163, the Gold Bracelet's script sets it) and Boost (187).
struct AbilityFlag { const char* name; int flag; };
const AbilityFlag kAbilities[] = {{"double_jump", 163}, {"boost", 187}};
int l_abilities(lua_State* L) {
    const std::int32_t* f = M(L).host().flags();
    if (!f) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 2);
    for (const AbilityFlag& a : kAbilities) { lua_pushboolean(L, f[a.flag] != 0); lua_setfield(L, -2, a.name); }
    return 1;
}
int l_set_ability(lua_State* L) {
    const std::string name = str_arg(L, 1);
    luaL_checkany(L, 2);
    std::int32_t* f = M(L).host().flags();
    for (const AbilityFlag& a : kAbilities)
        if (name == a.name) {
            if (f) f[a.flag] = lua_toboolean(L, 2) ? 1 : 0;
            lua_pushboolean(L, f != nullptr);
            return 1;
        }
    return luaL_argerror(L, 1, "an ability is \"double_jump\" or \"boost\"");
}

int l_goto_room(lua_State* L) {                          // ("S_1003") -> true when the change is staged (the room exists)
    const std::string room = str_arg(L, 1);
    bool ok = room.size() >= 3 && room.size() <= 16;
    for (char c : room) ok = ok && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
    if (!ok) return luaL_argerror(L, 1, "a room name like \"S_1003\"");
    lua_pushboolean(L, op(L, "goto_room", {}, room));
    return 1;
}
int l_exits(lua_State* L) {                              // -> {{index =, room =}, ...}: the current room's exits
    std::vector<double> out;
    lua_newtable(L);
    if (!op(L, "exits", {}, {}, &out)) return 1;
    for (std::size_t i = 0; i < out.size(); ++i) {
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, lua_Integer(i)); lua_setfield(L, -2, "index");
        char b[16];
        std::snprintf(b, sizeof b, "S_%04d", int(out[i]));
        lua_pushstring(L, b); lua_setfield(L, -2, "room");
        lua_rawseti(L, -2, lua_Integer(i + 1));
    }
    return 1;
}
int l_take_exit(lua_State* L) { lua_pushboolean(L, op(L, "take_exit", {double(luaL_checkinteger(L, 1))})); return 1; }
int l_set_speed(lua_State* L) { lua_pushboolean(L, op(L, "speed", {num_arg(L, 1, 0.1, 4.0)})); return 1; }   // this mod's factor
int l_game_retry(lua_State* L) { lua_pushboolean(L, op(L, "retry")); return 1; }
int l_to_title(lua_State* L) { lua_pushboolean(L, op(L, "to_title")); return 1; }
int l_game_load(lua_State* L) { lua_pushboolean(L, op(L, "load", {double(luaL_checkinteger(L, 1))})); return 1; }
int l_seed_random(lua_State* L) { lua_pushboolean(L, op(L, "seed_random", {double(luaL_checkinteger(L, 1) & 0xFFFFFFFF)})); return 1; }

void open_api7_writes(lua_State* L) {                    // the cleria table is on top of the stack
    static const luaL_Reg player[] = {{"set_position", l_set_position}, {"set_mp", l_set_mp}, {"set_boost", l_set_boost},
                                      {"set_exp", l_set_exp}, {"set_invulnerable", l_set_invulnerable}, {"equipment", l_equipment},
                                      {"equip", l_equip}, {"status", l_status}, {"set_status", l_set_status},
                                      {"abilities", l_abilities}, {"set_ability", l_set_ability}, {nullptr, nullptr}};
    lua_getfield(L, -1, "player");
    set_funcs(L, player);
    lua_pop(L, 1);
    static const luaL_Reg game[] = {{"goto_room", l_goto_room}, {"exits", l_exits}, {"take_exit", l_take_exit}, {"set_speed", l_set_speed},
                                    {"retry", l_game_retry}, {"to_title", l_to_title}, {"load", l_game_load},
                                    {"seed_random", l_seed_random}, {nullptr, nullptr}};
    lua_getfield(L, -1, "game");
    set_funcs(L, game);
    lua_pop(L, 1);
}

}  // namespace

namespace {

// ---- cleria.input: actions on a key or a pad button ------------------------------------------------------------
std::string action_id_arg(lua_State* L, int i) {
    std::string k = str_arg(L, i);
    bool ok = !k.empty() && k.size() <= 32;
    for (char c : k) ok = ok && (std::isalnum(static_cast<unsigned char>(c)) || c == '_');
    if (!ok) luaL_argerror(L, i, "an action id is 1-32 characters of A-Z a-z 0-9 _");
    return k;
}
Mod::Action* find_action(Mod& m, const std::string& id) {
    for (Mod::Action& a : m.actions_) if (a.id == id) return &a;
    return nullptr;
}
// cleria.input.action(id, {label =, key =, pad =} [, fn]): declares the action (again: replaces it). The player's own
// key, when they set one, wins over `key`.
int l_input_action(lua_State* L) {
    Mod& m = M(L);
    const std::string id = action_id_arg(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    Mod::Action a;
    a.id = id;
    a.label = opt_field_s(L, 2, "label", id);
    a.key = opt_field_s(L, 2, "key", "");
    a.pad = opt_field_s(L, 2, "pad", "");
    if (!a.key.empty() && !m.host().key_known(a.key, false)) return luaL_argerror(L, 2, ("unknown key '" + a.key + "'").c_str());
    if (!a.pad.empty() && !m.host().key_known(a.pad, true)) return luaL_argerror(L, 2, ("unknown pad button '" + a.pad + "'").c_str());
    if (const auto k = m.host().setting(m.id(), "Key." + id)) a.key = *k;       // the player's choice ("" = unbound)
    if (const auto p = m.host().setting(m.id(), "Pad." + id)) a.pad = *p;
    if (!lua_isnoneornil(L, 3)) {
        luaL_checktype(L, 3, LUA_TFUNCTION);
        lua_pushvalue(L, 3);
        a.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    if (Mod::Action* old = find_action(m, id)) {
        if (old->ref) luaL_unref(L, LUA_REGISTRYINDEX, old->ref);
        *old = a;
    } else {
        if (m.actions_.size() >= 64) return luaL_error(L, "too many actions (64)");
        m.actions_.push_back(a);
    }
    return 0;
}
int l_input_down(lua_State* L) {                         // is the action's key or pad button held now
    Mod& m = M(L);
    const Mod::Action* a = find_action(m, action_id_arg(L, 1));
    if (!a) return luaL_argerror(L, 1, "no such action (declare it with cleria.input.action)");
    lua_pushboolean(L, (!a->key.empty() && m.host().key_down(a->key)) || (!a->pad.empty() && m.host().pad_down(a->pad)));
    return 1;
}
int l_input_binding(lua_State* L) {                      // -> key, pad (the names in use, "" = none)
    Mod& m = M(L);
    const Mod::Action* a = find_action(m, action_id_arg(L, 1));
    if (!a) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, a->key.data(), a->key.size());
    lua_pushlstring(L, a->pad.data(), a->pad.size());
    return 2;
}
int l_input_bind(lua_State* L) {                         // (id, key [, pad]): the player's choice, kept in the settings
    Mod& m = M(L);
    Mod::Action* a = find_action(m, action_id_arg(L, 1));
    if (!a) return luaL_argerror(L, 1, "no such action (declare it with cleria.input.action)");
    const std::string key = luaL_optstring(L, 2, "");
    if (!key.empty() && !m.host().key_known(key, false)) { lua_pushboolean(L, 0); return 1; }
    a->key = key;
    m.host().set_setting(m.id(), "Key." + a->id, key);
    if (!lua_isnoneornil(L, 3)) {
        const std::string pad = luaL_checkstring(L, 3);
        if (!pad.empty() && !m.host().key_known(pad, true)) { lua_pushboolean(L, 0); return 1; }
        a->pad = pad;
        m.host().set_setting(m.id(), "Pad." + a->id, pad);
    }
    lua_pushboolean(L, 1);
    return 1;
}
int l_input_key(lua_State* L) { lua_pushboolean(L, M(L).host().key_down(str_arg(L, 1))); return 1; }   // a key by its name
int l_input_pad(lua_State* L) { lua_pushboolean(L, M(L).host().pad_down(str_arg(L, 1))); return 1; }

void open_api7_input(lua_State* L) {                     // the cleria table is on top of the stack
    static const luaL_Reg in[] = {{"action", l_input_action}, {"down", l_input_down}, {"binding", l_input_binding}, {"bind", l_input_bind},
                                  {"key", l_input_key}, {"pad", l_input_pad}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, in);
    lua_setfield(L, -2, "input");
}

}  // namespace

namespace {

// ---- cleria.ui.window / open / close, cleria.ui.message / choose ------------------------------------------------
const char* const kDialogId = "\x01" "dialog";
int l_ui_window(lua_State* L) {                          // (id, {title =, width =, pause =}, fn(p))
    Mod& m = M(L);
    const std::string id = action_id_arg(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    Mod::Window w;
    w.id = id;
    w.title = opt_field_s(L, 2, "title", m.name());
    w.width = float(std::clamp(opt_field_n(L, 2, "width", 0.42), 0.2, 0.9));
    lua_getfield(L, 2, "pause");
    w.pause = lua_isnil(L, -1) || lua_toboolean(L, -1);
    lua_pop(L, 1);
    lua_pushvalue(L, 3);
    w.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    for (Mod::Window& o : m.windows_)
        if (o.id == id) { luaL_unref(L, LUA_REGISTRYINDEX, o.ref); o = w; return 0; }
    if (m.windows_.size() >= 32) return luaL_error(L, "too many windows (32)");
    m.windows_.push_back(w);
    return 0;
}
int l_ui_open(lua_State* L) {                            // (id) -> true when it is now the open window
    Mod& m = M(L);
    const std::string id = action_id_arg(L, 1);
    for (const Mod::Window& w : m.windows_)
        if (w.id == id) { m.open_window_ = id; ++m.window_serial_; lua_pushboolean(L, 1); return 1; }
    return luaL_argerror(L, 1, "no such window (declare it with cleria.ui.window)");
}
int l_ui_close(lua_State* L) {
    Mod& m = M(L);
    if (m.open_window_ == kDialogId && m.dialog_.ref) { luaL_unref(L, LUA_REGISTRYINDEX, m.dialog_.ref); m.dialog_.ref = 0; }
    m.open_window_.clear();
    return 0;
}
int l_ui_is_open(lua_State* L) {                         // () -> the open window's id or nil; (id) -> boolean
    Mod& m = M(L);
    if (lua_isnoneornil(L, 1)) {
        if (m.open_window_.empty()) lua_pushnil(L);
        else if (m.open_window_ == kDialogId) lua_pushliteral(L, "dialog");
        else lua_pushlstring(L, m.open_window_.data(), m.open_window_.size());
        return 1;
    }
    lua_pushboolean(L, m.open_window_ == str_arg(L, 1));
    return 1;
}
// cleria.ui.choose(text, {options}, fn(index, option) [, {title =, cancel =}]): the game waits; fn gets the pick, or nil
// when the player backs out (cancel = false: they must choose). cleria.ui.message(text [, fn [, {title =, ok =}]]).
int open_dialog(lua_State* L, std::vector<std::string> options, int fn_index, int opt_index, bool cancel_default) {
    Mod& m = M(L);
    if (m.dialog_.ref) { luaL_unref(L, LUA_REGISTRYINDEX, m.dialog_.ref); m.dialog_.ref = 0; }
    m.dialog_ = Mod::Dialog{};
    m.dialog_.text = str_arg(L, 1);
    if (m.dialog_.text.size() > 4000) return luaL_argerror(L, 1, "at most 4000 characters");
    m.dialog_.options = std::move(options);
    m.dialog_.title = opt_field_s(L, opt_index, "title", m.name());
    m.dialog_.cancel = cancel_default;
    if (lua_istable(L, opt_index)) {
        lua_getfield(L, opt_index, "cancel");
        if (!lua_isnil(L, -1)) m.dialog_.cancel = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    if (lua_isfunction(L, fn_index)) {
        lua_pushvalue(L, fn_index);
        m.dialog_.ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    m.open_window_ = kDialogId;
    ++m.window_serial_;
    return 0;
}
int l_ui_choose(lua_State* L) {
    luaL_checktype(L, 2, LUA_TTABLE);
    luaL_checktype(L, 3, LUA_TFUNCTION);
    std::vector<std::string> options;
    const lua_Integer n = lua_Integer(lua_rawlen(L, 2));
    if (n < 1 || n > 12) return luaL_argerror(L, 2, "1 to 12 options");
    for (lua_Integer i = 1; i <= n; ++i) {
        lua_rawgeti(L, 2, i);
        options.push_back(luaL_tolstring(L, -1, nullptr));
        lua_pop(L, 2);
    }
    return open_dialog(L, std::move(options), 3, 4, true);
}
int l_ui_message(lua_State* L) {
    const std::string ok = opt_field_s(L, 3, "ok", "OK");
    return open_dialog(L, {ok}, 2, 3, true);
}
int l_p_text(lua_State* L) {                             // p:text(str [, {color =}]): a wrapped paragraph
    Mod& m = paging(L);
    m.host().ui_text(m.list_, str_arg(L, 2), color_of(L, 3, 0xCFCABD));
    return 0;
}
int l_p_progress(lua_State* L) {                         // p:progress(label, fraction [, text [, {color =}]])
    Mod& m = paging(L);
    const double f = std::clamp(luaL_checknumber(L, 3), 0.0, 1.0);
    const std::string t = lua_isnoneornil(L, 4) ? std::string() : std::string(luaL_tolstring(L, 4, nullptr));
    if (!lua_isnoneornil(L, 4)) lua_pop(L, 1);
    m.host().ui_progress(m.list_, str_arg(L, 2), std::isfinite(f) ? f : 0.0, t, color_of(L, 5, 0xD6B03E));
    return 0;
}
int l_p_close(lua_State* L) {                            // p:close() inside a window's function
    Mod& m = M(L);
    m.open_window_.clear();
    return 0;
}

void open_api7_windows(lua_State* L) {                   // the cleria table is on top of the stack
    static const luaL_Reg ui[] = {{"window", l_ui_window}, {"open", l_ui_open}, {"close", l_ui_close}, {"is_open", l_ui_is_open},
                                  {"choose", l_ui_choose}, {"message", l_ui_message}, {nullptr, nullptr}};
    lua_getfield(L, -1, "ui");
    set_funcs(L, ui);
    lua_pop(L, 1);
}

}  // namespace

const Mod::Window* Mod::window() const {
    if (open_window_.empty()) return nullptr;
    if (open_window_ == kDialogId) {
        static Window d;
        d.id = kDialogId; d.title = dialog_.title; d.width = 0.42f; d.pause = true;
        return &d;
    }
    for (const Window& w : windows_) if (w.id == open_window_) return &w;
    return nullptr;
}

// The open window's rows. A dialog: its text, one line a row, then a button per option.
void Mod::draw_window(void* list) {
    if (!ok() || open_window_.empty()) return;
    lua_State* L = state();
    if (open_window_ == kDialogId) {
        host_.ui_text(list, dialog_.text, 0xFFE8E4D8u);
        int picked = -1;
        for (std::size_t i = 0; i < dialog_.options.size(); ++i)
            if (host_.ui_button(list, dialog_.options[i], {}, true)) picked = int(i);
        if (picked < 0) return;
        const int ref = dialog_.ref;
        const std::string option = dialog_.options[std::size_t(picked)];
        dialog_.ref = 0;
        open_window_.clear();                              // before the function: it may open the next one
        if (ref) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
            lua_pushinteger(L, picked + 1);
            lua_pushlstring(L, option.data(), option.size());
            pcall(2, 0, "dialog");
            if (L_) luaL_unref(L_, LUA_REGISTRYINDEX, ref);
        }
        return;
    }
    const Window* w = window();
    if (!w) { open_window_.clear(); return; }
    static const luaL_Reg pm[] = {{"section", l_p_section}, {"toggle", l_p_toggle}, {"slider", l_p_slider},
                                  {"choice", l_p_choice}, {"button", l_p_button}, {"text_field", l_p_text_field},
                                  {"info", l_p_info}, {"text", l_p_text}, {"progress", l_p_progress}, {"close", l_p_close},
                                  {nullptr, nullptr}};
    lua_createtable(L, 0, 11);
    luaL_setfuncs(L, pm, 0);
    lua_rawgeti(L, LUA_REGISTRYINDEX, w->ref);
    lua_insert(L, -2);
    list_ = list;
    pcall(1, 0, "window");
    list_ = nullptr;
}

// Back / Esc on the open window. A dialog that may be cancelled calls its function with nil.
void Mod::close_window(bool cancelled) {
    if (open_window_.empty() || !ok()) { open_window_.clear(); return; }
    if (open_window_ == kDialogId) {
        if (cancelled && !dialog_.cancel) return;          // the player must choose
        const int ref = dialog_.ref;
        dialog_.ref = 0;
        open_window_.clear();
        if (ref) {
            lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
            pcall(0, 0, "dialog");
            if (L_) luaL_unref(L_, LUA_REGISTRYINDEX, ref);
        }
        return;
    }
    open_window_.clear();
}

namespace {
}  // namespace

namespace {

// ---- cleria.world.add_filter("spawn" | "exit", fn), world.spawn / remove / set_actor ---------------------------
int l_world_add_filter(lua_State* L) {
    const std::string kind = str_arg(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (kind != "spawn" && kind != "exit") return luaL_argerror(L, 1, "a world filter is of \"spawn\" or \"exit\"");
    lua_pushvalue(L, 2);
    M(L).world_filters_[kind].push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}
int l_world_spawn(lua_State* L) {                        // (cast, x, y, z [, {level =, yaw =, group =}]) -> the actor's id, or nil
    std::vector<double> v = {double(luaL_checkinteger(L, 1)), num_arg(L, 2, -1e6, 1e6), num_arg(L, 3, -1e6, 1e6), num_arg(L, 4, -1e6, 1e6),
                             std::clamp(opt_field_n(L, 5, "level", 0), 0.0, 60.0), opt_field_n(L, 5, "yaw", 0),
                             std::clamp(opt_field_n(L, 5, "group", 0), 0.0, 4.0)};
    std::vector<double> out;
    if (!op(L, "spawn_actor", v, {}, &out) || out.empty()) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, lua_Integer(out[0]));
    return 1;
}
int l_world_remove(lua_State* L) { lua_pushboolean(L, op(L, "remove_actor", {double(luaL_checkinteger(L, 1))})); return 1; }
int l_world_set_actor(lua_State* L) {                    // (id, {hp =, x =, y =, z =, yaw =}) -> true when it lives
    luaL_checktype(L, 2, LUA_TTABLE);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    lua_pushboolean(L, op(L, "set_actor", {double(luaL_checkinteger(L, 1)), opt_field_n(L, 2, "hp", nan), opt_field_n(L, 2, "x", nan),
                                           opt_field_n(L, 2, "y", nan), opt_field_n(L, 2, "z", nan), opt_field_n(L, 2, "yaw", nan)}));
    return 1;
}

void open_api7_world2(lua_State* L) {                    // the cleria table is on top of the stack
    static const luaL_Reg world[] = {{"add_filter", l_world_add_filter}, {"spawn", l_world_spawn}, {"remove", l_world_remove},
                                     {"set_actor", l_world_set_actor}, {nullptr, nullptr}};
    lua_getfield(L, -1, "world");
    set_funcs(L, world);
    lua_pop(L, 1);
}

}  // namespace

// The first answer that is not "pass" wins. The answer: nil / "pass", "suppress", or a table with `action = "replace"`
// (the default when a table has no action) whose number and string fields are the decision's.
WorldDecision Mod::world_filter(const WorldQuery& q, const std::string& room) {
    const auto it = world_filters_.find(q.kind);
    if (it == world_filters_.end()) return {};
    const std::vector<int> fs = it->second;
    for (std::size_t i = 0; i < fs.size() && ok(); ++i) {
        lua_State* L = state();
        lua_createtable(L, 0, int(q.num.size() + q.str.size()) + 2);
        lua_pushlstring(L, q.kind.data(), q.kind.size()); lua_setfield(L, -2, "kind");
        for (const auto& [k, v] : q.num) {
            if (v == std::floor(v) && std::fabs(v) < 9.0e15) lua_pushinteger(L, lua_Integer(v)); else lua_pushnumber(L, v);
            lua_setfield(L, -2, k.c_str());
        }
        for (const auto& [k, v] : q.str) { lua_pushlstring(L, v.data(), v.size()); lua_setfield(L, -2, k.c_str()); }
        lua_pushlstring(L, room.data(), room.size()); lua_setfield(L, -2, "room");
        lua_rawgeti(L, LUA_REGISTRYINDEX, fs[i]);
        lua_insert(L, -2);
        if (!pcall(1, 1, "world filter")) return {};
        WorldDecision d;
        if (lua_type(L, -1) == LUA_TSTRING) {
            if (std::string(lua_tostring(L, -1)) == "suppress") d.action = WorldDecision::Action::Suppress;
        } else if (lua_istable(L, -1)) {
            const std::string action = opt_field_s(L, -1, "action", "replace");
            if (action == "suppress") d.action = WorldDecision::Action::Suppress;
            else if (action == "replace") d.action = WorldDecision::Action::Replace;
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {
                if (lua_type(L, -2) == LUA_TSTRING) {
                    const std::string k = lua_tostring(L, -2);
                    if (lua_type(L, -1) == LUA_TNUMBER) { const double v = lua_tonumber(L, -1); if (std::isfinite(v)) d.num[k] = v; }
                    else if (lua_type(L, -1) == LUA_TSTRING) d.str[k] = lua_tostring(L, -1);
                    else if (lua_isboolean(L, -1)) d.num[k] = lua_toboolean(L, -1) ? 1.0 : 0.0;
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
        if (d.action != WorldDecision::Action::Pass) return d;
    }
    return {};
}

namespace {
}  // namespace

namespace {

// ---- cleria.audio (the game's own sounds and music), cleria.screen (the fader) ---------------------------------
int l_audio_se(lua_State* L) {                           // (id [, volume 0..1]) : a sound of EFFECTDEF
    lua_pushboolean(L, op(L, "se", {double(luaL_checkinteger(L, 1)), std::clamp(luaL_optnumber(L, 2, 1.0), 0.0, 1.0)}));
    return 1;
}
int l_audio_music(lua_State* L) { lua_pushboolean(L, op(L, "bgm", {double(luaL_checkinteger(L, 1))})); return 1; }   // a BGM id (bgmdef)
int l_audio_stop_music(lua_State* L) {                   // ([fade in seconds])
    lua_pushboolean(L, op(L, "bgm_stop", {std::clamp(luaL_optnumber(L, 1, 1.0), 0.0, 30.0) * 1000.0}));
    return 1;
}
double rgb_arg(lua_State* L, int i) { return double(std::uint32_t(luaL_optinteger(L, i, 0)) & 0xFFFFFFu); }
int l_screen_fade(lua_State* L) {                        // (color 0xRRGGBB, from 0..1, to 0..1 [, seconds]) : the game's own fader
    const double sec = std::clamp(luaL_optnumber(L, 4, 0.33), 0.0, 60.0);
    lua_pushboolean(L, op(L, "fade", {rgb_arg(L, 1), num_arg(L, 2, 0, 1), num_arg(L, 3, 0, 1), std::max(1.0, sec * 60.0)}));
    return 1;
}
int l_screen_flash(lua_State* L) {                       // (color [, seconds [, strength 0..1]]) : full of that colour, then clear
    const double sec = std::clamp(luaL_optnumber(L, 2, 0.25), 0.0, 60.0), a = std::clamp(luaL_optnumber(L, 3, 0.6), 0.0, 1.0);
    lua_pushboolean(L, op(L, "fade", {rgb_arg(L, 1), a, 0.0, std::max(1.0, sec * 60.0)}));
    return 1;
}

void open_api7_av(lua_State* L) {                        // the cleria table is on top of the stack
    static const luaL_Reg audio[] = {{"se", l_audio_se}, {"music", l_audio_music}, {"stop_music", l_audio_stop_music}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, audio);
    lua_setfield(L, -2, "audio");
    static const luaL_Reg screen[] = {{"fade", l_screen_fade}, {"flash", l_screen_flash}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, screen);
    lua_setfield(L, -2, "screen");
}

}  // namespace

namespace {

// ---- cleria.world.puppet: one of the three characters as an actor the mod moves (another player, a ghost) -------
int l_world_puppet(lua_State* L) {                       // (character 1..3, x, y, z [, degrees]) -> its id, or nil
    const lua_Integer c = luaL_checkinteger(L, 1);
    if (c < 1 || c > 3) return luaL_argerror(L, 1, "1 Yunica, 2 Hugo or 3 Toal");
    std::vector<double> out;
    if (!op(L, "puppet_spawn", {double(c), num_arg(L, 2, -1e6, 1e6), num_arg(L, 3, -1e6, 1e6), num_arg(L, 4, -1e6, 1e6), luaL_optnumber(L, 5, 0.0)}, {}, &out) ||
        out.empty()) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, lua_Integer(out[0]));
    return 1;
}
int l_world_set_puppet(lua_State* L) {                   // (id, {x =, y =, z =, yaw =, tag =, frame =, sheet =}) -> true while it exists
    luaL_checktype(L, 2, LUA_TTABLE);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    lua_pushboolean(L, op(L, "puppet_set", {double(luaL_checkinteger(L, 1)), opt_field_n(L, 2, "x", nan), opt_field_n(L, 2, "y", nan),
                                            opt_field_n(L, 2, "z", nan), opt_field_n(L, 2, "yaw", nan), opt_field_n(L, 2, "tag", nan),
                                            opt_field_n(L, 2, "frame", nan), opt_field_n(L, 2, "sheet", nan)}));
    return 1;
}
int l_world_remove_puppet(lua_State* L) { lua_pushboolean(L, op(L, "puppet_remove", {double(luaL_checkinteger(L, 1))})); return 1; }

int l_player_pose(lua_State* L) {                        // -> {x, y, z, yaw (degrees), tag, frame, sheet}: what a puppet needs to copy the player
    std::vector<double> out;
    if (!op(L, "pose", {}, {}, &out) || out.size() < 7) { lua_pushnil(L); return 1; }
    static const char* const k[] = {"x", "y", "z", "yaw", "tag", "frame", "sheet"};
    lua_createtable(L, 0, 7);
    for (int i = 0; i < 7; ++i) {
        if (i >= 4) lua_pushinteger(L, lua_Integer(out[std::size_t(i)])); else lua_pushnumber(L, out[std::size_t(i)]);
        lua_setfield(L, -2, k[i]);
    }
    return 1;
}

void open_api7_puppet(lua_State* L) {                    // the cleria table is on top of the stack
    static const luaL_Reg world[] = {{"puppet", l_world_puppet}, {"set_puppet", l_world_set_puppet}, {"remove_puppet", l_world_remove_puppet},
                                     {nullptr, nullptr}};
    lua_getfield(L, -1, "world");
    set_funcs(L, world);
    lua_pop(L, 1);
    static const luaL_Reg player[] = {{"pose", l_player_pose}, {nullptr, nullptr}};
    lua_getfield(L, -1, "player");
    set_funcs(L, player);
    lua_pop(L, 1);
}

}  // namespace

namespace {

// ---- cleria.script: the game's own scripts (XSO) ----------------------------------------------------------------
int l_script_run(lua_State* L) { lua_pushboolean(L, op(L, "script_run", {}, str_arg(L, 1))); return 1; }   // (name) -> started
int l_script_add_filter(lua_State* L) {                  // (fn(q)) : q = {name, room, owner}; "suppress" refuses the start
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    M(L).world_filters_["script"].push_back(luaL_ref(L, LUA_REGISTRYINDEX));
    return 0;
}

void open_api7_script(lua_State* L) {                    // the cleria table is on top of the stack
    static const luaL_Reg fns[] = {{"run", l_script_run}, {"add_filter", l_script_add_filter}, {nullptr, nullptr}};
    lua_newtable(L);
    set_funcs(L, fns);
    lua_setfield(L, -2, "script");
}

}  // namespace

namespace {

// ---- cleria.combat.damage / heal / hurt, cleria.player.tune -----------------------------------------------------
int l_combat_damage(lua_State* L) {                      // (actor, n [, {knock =, class =}]) -> queued
    lua_pushboolean(L, op(L, "hit", {double(luaL_checkinteger(L, 1)), num_arg(L, 2, 0, 1e7), std::clamp(opt_field_n(L, 3, "knock", 0), 0.0, 10.0),
                                     opt_field_n(L, 3, "class", 3)}));
    return 1;
}
int l_combat_heal(lua_State* L) { lua_pushboolean(L, op(L, "heal", {num_arg(L, 1, 0, 1e7)})); return 1; }
int l_combat_hurt(lua_State* L) { lua_pushboolean(L, op(L, "hurt", {num_arg(L, 1, 0, 1e7)})); return 1; }
int l_player_tune(lua_State* L) {                        // ({speed =, jump =, mp_cost =, mp_regen =, boost_gain =, air_jumps =})
    luaL_checktype(L, 1, LUA_TTABLE);
    static const char* const k[] = {"speed", "jump", "mp_cost", "mp_regen", "boost_gain", "air_jumps"};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    bool ok = true;
    for (int i = 0; i < 6; ++i) {
        const double v = opt_field_n(L, 1, k[i], nan);
        if (std::isfinite(v)) ok = op(L, "tune", {double(i), v}) && ok;
    }
    lua_pushboolean(L, ok);
    return 1;
}

void open_api7_combat(lua_State* L) {                    // the cleria table is on top of the stack
    static const luaL_Reg combat[] = {{"damage", l_combat_damage}, {"heal", l_combat_heal}, {"hurt", l_combat_hurt}, {nullptr, nullptr}};
    lua_getfield(L, -1, "combat");
    set_funcs(L, combat);
    lua_pop(L, 1);
    static const luaL_Reg player[] = {{"tune", l_player_tune}, {nullptr, nullptr}};
    lua_getfield(L, -1, "player");
    set_funcs(L, player);
    lua_pop(L, 1);
}

}  // namespace

namespace {

// ---- cleria.net.udp / tcp / http (src/cleria/net/socket) --------------------------------------------------------
const char* const kUdpMt = "cleria.net.udp";
const char* const kTcpMt = "cleria.net.tcp";
constexpr int kRawSockets = 16;                          // [H] udp + tcp sockets alive at once per mod
struct UdpUd { net::UdpSocket* s = nullptr; Mod* mod = nullptr; };
struct TcpUd { net::TcpSocket* s = nullptr; Mod* mod = nullptr; };

template <class Ud, class Sock>
Ud* new_sock(lua_State* L, const char* mt, Sock* adopt = nullptr) {
    Mod& m = M(L);
    if (m.raw_sockets_ >= kRawSockets) {
        delete adopt;
        luaL_error(L, "a mod may keep at most %d sockets", kRawSockets);
    }
    Ud* u = static_cast<Ud*>(lua_newuserdatauv(L, sizeof(Ud), 0));
    new (u) Ud{};
    u->s = adopt ? adopt : new Sock();
    u->mod = &m;
    ++m.raw_sockets_;
    luaL_setmetatable(L, mt);
    return u;
}
template <class Ud>
int sock_gc(lua_State* L, const char* mt) {
    Ud* u = static_cast<Ud*>(luaL_checkudata(L, 1, mt));
    if (u->s && u->mod) --u->mod->raw_sockets_;
    delete u->s;
    u->s = nullptr;
    return 0;
}
net::UdpSocket& udp_check(lua_State* L) {
    UdpUd* u = static_cast<UdpUd*>(luaL_checkudata(L, 1, kUdpMt));
    if (!u->s) luaL_error(L, "the socket was closed for good");
    return *u->s;
}
net::TcpSocket& tcp_check(lua_State* L) {
    TcpUd* u = static_cast<TcpUd*>(luaL_checkudata(L, 1, kTcpMt));
    if (!u->s) luaL_error(L, "the socket was closed for good");
    return *u->s;
}
int push_fail(lua_State* L, const std::string& err) { lua_pushboolean(L, 0); lua_pushstring(L, err.c_str()); return 2; }
std::string bytes_arg(lua_State* L, int i) { std::size_t n = 0; const char* p = luaL_checklstring(L, i, &n); return std::string(p, n); }

int l_udp_new(lua_State* L) { new_sock<UdpUd, net::UdpSocket>(L, kUdpMt); return 1; }
int l_udp_gc(lua_State* L) { return sock_gc<UdpUd>(L, kUdpMt); }
int l_udp_bind(lua_State* L) {                           // ([port [, any]]) -> the port, or false, why
    std::string err;
    net::UdpSocket& s = udp_check(L);
    if (!s.bind(int(luaL_optinteger(L, 2, 0)), lua_isnoneornil(L, 3) || lua_toboolean(L, 3), &err)) return push_fail(L, err);
    lua_pushinteger(L, s.port());
    return 1;
}
int l_udp_send(lua_State* L) {                           // (host, port, data) -> true, or false, why
    std::string err;
    if (!udp_check(L).send_to(str_arg(L, 2), int(luaL_checkinteger(L, 3)), bytes_arg(L, 4), &err)) return push_fail(L, err);
    lua_pushboolean(L, 1);
    return 1;
}
int l_udp_poll(lua_State* L) {                           // -> {{host =, port =, data =}, ...}
    lua_newtable(L);
    lua_Integer i = 0;
    for (const net::UdpSocket::Datagram& d : udp_check(L).poll()) {
        lua_createtable(L, 0, 3);
        lua_pushstring(L, d.host.c_str()); lua_setfield(L, -2, "host");
        lua_pushinteger(L, d.port); lua_setfield(L, -2, "port");
        lua_pushlstring(L, d.data.data(), d.data.size()); lua_setfield(L, -2, "data");
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}
int l_udp_port(lua_State* L) { lua_pushinteger(L, udp_check(L).port()); return 1; }
int l_udp_close(lua_State* L) { udp_check(L).close(); return 0; }

int l_tcp_new(lua_State* L) { new_sock<TcpUd, net::TcpSocket>(L, kTcpMt); return 1; }
int l_tcp_gc(lua_State* L) { return sock_gc<TcpUd>(L, kTcpMt); }
int l_tcp_connect(lua_State* L) {                        // (host, port [, timeout seconds]) : "open" or "close" follows in poll()
    const lua_Integer port = luaL_checkinteger(L, 3);
    if (port < 1 || port > 65535) return luaL_argerror(L, 3, "a port is 1..65535");
    tcp_check(L).connect(str_arg(L, 2), int(port), int(std::clamp(luaL_optnumber(L, 4, 10.0), 1.0, 60.0) * 1000.0));
    return 0;
}
int l_tcp_listen(lua_State* L) {                         // ([port [, any]]) -> the port, or false, why
    std::string err;
    net::TcpSocket& s = tcp_check(L);
    if (!s.listen(int(luaL_optinteger(L, 2, 0)), lua_isnoneornil(L, 3) || lua_toboolean(L, 3), &err)) return push_fail(L, err);
    lua_pushinteger(L, s.port());
    return 1;
}
int l_tcp_send(lua_State* L) { lua_pushboolean(L, tcp_check(L).send(bytes_arg(L, 2))); return 1; }
int l_tcp_poll(lua_State* L) {                           // -> {{kind = "open" | "data" | "close" | "accept", text =, error =, socket =}, ...}
    std::vector<net::TcpSocket::Event> ev = tcp_check(L).poll();
    lua_newtable(L);
    lua_Integer i = 0;
    for (net::TcpSocket::Event& e : ev) {
        using K = net::TcpSocket::Event::Kind;
        lua_createtable(L, 0, 4);
        lua_pushstring(L, e.kind == K::Opened ? "open" : e.kind == K::Data ? "data" : e.kind == K::Closed ? "close" : "accept");
        lua_setfield(L, -2, "kind");
        lua_pushlstring(L, e.text.data(), e.text.size()); lua_setfield(L, -2, "text");
        lua_pushboolean(L, e.error); lua_setfield(L, -2, "error");
        if (e.kind == K::Accepted && e.socket) {
            if (M(L).raw_sockets_ >= kRawSockets) { lua_pop(L, 1); continue; }   // over the limit: the connection is dropped
            new_sock<TcpUd, net::TcpSocket>(L, kTcpMt, e.socket.release());
            lua_setfield(L, -2, "socket");
        }
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}
int l_tcp_state(lua_State* L) {
    static const char* const kName[] = {"idle", "connecting", "open", "listening", "closed"};
    lua_pushstring(L, kName[int(tcp_check(L).state())]);
    return 1;
}
int l_tcp_port(lua_State* L) { lua_pushinteger(L, tcp_check(L).port()); return 1; }
int l_tcp_close(lua_State* L) { tcp_check(L).close(); return 0; }

struct HttpJob {
    std::mutex mu;
    bool done = false;
    net::HttpResponse rs;
};
int l_net_http(lua_State* L) {                           // ({url =, method =, body =, headers = {}, timeout =}, fn(res))
    Mod& m = M(L);
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (m.http_jobs_.size() >= 8) return luaL_error(L, "a mod may have at most 8 HTTP requests running");
    net::HttpRequest rq;
    lua_getfield(L, 1, "url");
    rq.url = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    if (rq.url.rfind("http://", 0) != 0 && rq.url.rfind("https://", 0) != 0) return luaL_argerror(L, 1, "url must start with http:// or https://");
    lua_getfield(L, 1, "method");
    if (lua_isstring(L, -1)) rq.method = lua_tostring(L, -1);
    lua_pop(L, 1);
    for (char& c : rq.method) c = char(std::toupper(static_cast<unsigned char>(c)));
    if (rq.method != "GET" && rq.method != "POST" && rq.method != "PUT" && rq.method != "PATCH" && rq.method != "DELETE" && rq.method != "HEAD")
        return luaL_argerror(L, 1, "method is GET, POST, PUT, PATCH, DELETE or HEAD");
    lua_getfield(L, 1, "body");
    if (lua_isstring(L, -1)) { std::size_t n = 0; const char* p = lua_tolstring(L, -1, &n); rq.body.assign(p, n); }
    lua_pop(L, 1);
    rq.timeout_ms = int(std::clamp(opt_field_n(L, 1, "timeout", 15.0), 1.0, 120.0) * 1000.0);
    lua_getfield(L, 1, "headers");
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING && lua_isstring(L, -1) && rq.headers.size() < 64) rq.headers.emplace_back(lua_tostring(L, -2), lua_tostring(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
    auto job = std::make_shared<HttpJob>();
    lua_pushvalue(L, 2);
    m.http_jobs_.emplace_back(job, luaL_ref(L, LUA_REGISTRYINDEX));
    std::thread([job, rq] {
        net::HttpResponse rs = net::http_request(rq);
        std::lock_guard<std::mutex> lk(job->mu);
        job->rs = std::move(rs);
        job->done = true;
    }).detach();
    return 0;
}

void open_api7_net(lua_State* L) {                       // the cleria table is on top of the stack
    static const luaL_Reg udp[] = {{"bind", l_udp_bind}, {"send", l_udp_send}, {"poll", l_udp_poll}, {"port", l_udp_port}, {"close", l_udp_close},
                                   {"__gc", l_udp_gc}, {nullptr, nullptr}};
    static const luaL_Reg tcp[] = {{"connect", l_tcp_connect}, {"listen", l_tcp_listen}, {"send", l_tcp_send}, {"poll", l_tcp_poll},
                                   {"state", l_tcp_state}, {"port", l_tcp_port}, {"close", l_tcp_close}, {"__gc", l_tcp_gc}, {nullptr, nullptr}};
    for (const auto& [mt, fns] : {std::pair<const char*, const luaL_Reg*>{kUdpMt, udp}, {kTcpMt, tcp}}) {
        luaL_newmetatable(L, mt);
        lua_pushvalue(L, -1);
        lua_setfield(L, -2, "__index");
        luaL_setfuncs(L, fns, 0);
        lua_pushstring(L, "socket");
        lua_setfield(L, -2, "__metatable");
        lua_pop(L, 1);
    }
    static const luaL_Reg net[] = {{"udp", l_udp_new}, {"tcp", l_tcp_new}, {"http", l_net_http}, {nullptr, nullptr}};
    lua_getfield(L, -1, "net");
    set_funcs(L, net);
    lua_pop(L, 1);
}

}  // namespace

void Mod::run_http() {
    for (std::size_t i = 0; i < http_jobs_.size() && ok();) {
        HttpJob& j = *std::static_pointer_cast<HttpJob>(http_jobs_[i].first);
        net::HttpResponse rs;
        {
            std::lock_guard<std::mutex> lk(j.mu);
            if (!j.done) { ++i; continue; }
            rs = std::move(j.rs);
        }
        const int ref = http_jobs_[i].second;
        http_jobs_.erase(http_jobs_.begin() + std::ptrdiff_t(i));
        lua_State* L = L_;
        lua_createtable(L, 0, 5);
        lua_pushboolean(L, rs.ok); lua_setfield(L, -2, "ok");
        lua_pushinteger(L, rs.status); lua_setfield(L, -2, "status");
        lua_pushlstring(L, rs.body.data(), rs.body.size()); lua_setfield(L, -2, "body");
        lua_pushstring(L, rs.error.c_str()); lua_setfield(L, -2, "error");
        lua_pushboolean(L, rs.truncated); lua_setfield(L, -2, "truncated");
        call_ref(ref, 1, 0, "http");
        if (L_) luaL_unref(L_, LUA_REGISTRYINDEX, ref);
    }
}
