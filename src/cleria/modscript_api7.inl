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
