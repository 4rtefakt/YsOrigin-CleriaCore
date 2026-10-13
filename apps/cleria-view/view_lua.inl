// cleria-view — Lua scripting for mods (build/shell_re/LUA_API.md). Textual part of main.cpp: #included
// after view_video.inl; ONE translation unit, not standalone.
//
// CleriaCore's own layer, not retail. Every Active mod whose mod.ini says `Script=<file>` (ApiVersion 3)
// gets a sandboxed Lua state (src/cleria/modscript); this file is its Host: the live game (flags, the
// player, warps), the content layer (content_give, the grant filter, the save sidecar's [mod.<id>]),
// cleria.ini [cleria.mod.<id>], the ui kit (cleria.ui.page on the mod's F1 > Mods > <mod> page, a Shell::hud layer per mod) and
// the event bus. A failing script is disabled and its error shown in the Mods tab; the game goes on.
// With no scripted mod active nothing here runs: every hook returns at once.
//
// The hook lines in view_game.inl (each a single call into this file):
//   render_window start   lua_startup             after content_startup: start the scripted mods
//   the shell's pages     lua_shell               their Mods > <mod> pages and hud layers, the notice card
//   csMgr built           lua_bind                the live game (LuaBind)
//   icons loaded          lua_seed_icons          cleria.ui.icon ids in the two icon caches; the GPU for images
//   each frame            lua_frame               "frame" handlers
//   each tick             lua_tick                "tick" handlers (after content_tick)
//   award_exp             lua_exp_factor          cleria.game.exp_factor
//   award_exp, credit_sp, the hits, the drops, a pickup's touch
//                         lua_combat_on / lua_combat   cleria.combat.add_filter (API 6); lua_sp, lua_damage
//   player_stats          lua_stat_modifiers      cleria.player.set_modifier
//   drive commands        lua_drive               lua=<command> <args> (cleria.command)
#include "cleria/modscript.hpp"

// What the view binds (lua_bind, the block after the content one in view_game.inl).
struct LuaBind {
    std::int32_t* flags = nullptr;         // game.g_flags
    std::int32_t* item_arr = nullptr;      // game.item_arr (the 54 gear-upgrade cells)
    std::uint8_t* warp_on = nullptr;       // the warp registry
    const int* character = nullptr;        // g_flags[150] as the view keeps it
    Player* player = nullptr;
    std::function<void(int)> set_level, set_weapon_tier, warp;
    std::function<void()> refresh;
    std::function<bool()> frozen;
    std::function<bool(int)> save;         // cleria.game.save (set where the game's save state is in scope)
    std::function<bool(script::PlayerInfo&)> info;                  // cleria.player.stats
    std::function<void(float)> add_exp;                             // cleria.player.add_exp
    std::function<bool(int, const Vec3&, float)> spawn_pickup;      // cleria.game.spawn_pickup
    // API 7
    std::function<const char*()> state;                            // cleria.game.state while a game is bound
    std::function<std::string()> room_title;                       // cleria.game.room_title
    std::function<void(std::vector<script::Host::ActorInfo>&)> actors;   // cleria.world.actors
    std::function<bool(const Vec3&, float*, float*)> to_screen;    // cleria.world.to_screen (fractions of the picture)
    // Host::game_op: the named writes (view_lua_ops.inl, set where the game's state is in scope)
    std::function<bool(const std::string&, const std::string&, const std::vector<double>&, const std::string&, std::vector<double>*)> op;
};

struct LuaNotice {
    std::string head, text;
    double t = 0;
};

static bool g_lua_info(script::PlayerInfo& out);
struct LuaHost : script::Host {
    void notice(const std::string& mod, const std::string& head, const std::string& text) override;
    void failed(const std::string& mod, const std::string& error) override;
    double now() override { return double(SDL_GetTicksNS()) / 1.0e9; }
    bool in_game() override;
    std::int32_t* flags() override;
    std::int32_t* upgrades() override;
    std::uint8_t* warps() override;
    int character() override;
    std::string room() override { return g_content.room; }
    bool frozen() override;
    bool save_ready() override { return !content_session_pending(); }   // a New Game / Load still queued (view_content.inl)
    std::map<std::string, std::pair<int, std::string>> chests() override { return g_content.chests; }
    std::string item_name(int id) override { return g_content.items.name(id); }
    int new_item_count(int id) override { return g_content.sc.item(id); }
    void give(int id, int count, const std::string& source) override { content_give(id, count, source); }
    void emit(const events::Event& e) override { content_emit(e); }
    double stat(Stat s) override;
    void set_stat(Stat s, double v) override;
    void refresh() override;
    bool player_info(script::PlayerInfo& out) override { return g_lua_info(out); }
    void add_exp(double n) override;
    void set_modifier(const std::string& mod, int stat, double add, double mul) override;
    std::pair<double, double> modifier(const std::string& mod, int stat) override;
    bool spawn_pickup(int item, float x, float y, float z, double value) override;
    void warp(int index) override;
    bool save(int slot) override;
    void unlock_character(int c) override;
    bool set_save_profile(const std::string& name) override;
    std::string save_profile() override { return g_save_profile; }
    std::map<std::string, std::string>* save_storage(const std::string& mod) override { return &g_content.sc.mod[mod]; }
    std::optional<std::string> setting(const std::string& mod, const std::string& key) override;
    bool set_setting(const std::string& mod, const std::string& key, const std::string& value) override;
    std::filesystem::path data_dir(const std::string& mod) override;
    std::map<std::string, std::uint32_t> named_colors() override;
    void draw_text(float x, float y, const std::string& t, float px, int font, std::uint32_t col) override;
    std::pair<float, float> text_size(const std::string& t, float px, int font) override;
    void draw_rect(float x0, float y0, float x1, float y1, std::uint32_t col, float rounding, float thickness) override;
    void draw_line(float x0, float y0, float x1, float y1, std::uint32_t col, float thickness) override;
    void draw_circle(float x, float y, float r, std::uint32_t col, float thickness) override;
    int load_image(const std::filesystem::path& file) override;
    void draw_image(int handle, float x0, float y0, float x1, float y1, std::uint32_t tint) override;
    void ui_section(void* list, const std::string& title) override { ui::section(*static_cast<ui::List*>(list), title.c_str()); }
    bool ui_toggle(void* list, const std::string& label, bool* v, const std::string& desc, bool enabled) override {
        return ui::toggle(*static_cast<ui::List*>(list), label.c_str(), v, desc.c_str(), enabled);
    }
    bool ui_slider(void* list, const std::string& label, float* v, float lo, float hi, float step, const std::string& fmt,
                   const std::string& desc, bool enabled) override {
        return ui::slider(*static_cast<ui::List*>(list), label.c_str(), v, lo, hi, step, fmt.c_str(), desc.c_str(), enabled);
    }
    bool ui_choice(void* list, const std::string& label, int* idx, const std::vector<std::string>& items, const std::string& desc,
                   bool enabled) override {
        std::vector<const char*> p;
        for (const std::string& s : items) p.push_back(s.c_str());
        return ui::choice(*static_cast<ui::List*>(list), label.c_str(), idx, p.data(), int(p.size()), desc.c_str(), enabled);
    }
    bool ui_button(void* list, const std::string& label, const std::string& desc, bool enabled) override {
        return ui::button(*static_cast<ui::List*>(list), label.c_str(), desc.c_str(), enabled);
    }
    bool ui_text_field(void* list, const std::string& label, std::string* v, const std::string& desc, bool secret,
                       const std::string& empty) override {
        return ui::text_field(*static_cast<ui::List*>(list), label.c_str(), v, desc.c_str(), secret, empty.c_str());
    }
    void ui_info(void* list, const std::string& label, const std::string& value, std::uint32_t col, const std::string& desc) override {
        ui::info(*static_cast<ui::List*>(list), label.c_str(), value.c_str(), col, desc.empty() ? nullptr : desc.c_str());
    }
    // ---- API 7 ----
    std::vector<ModInfo> mod_list() override;
    void log(const std::string& mod, const std::string& text) override;
    bool run_command(const std::string& name, const std::string& args) override;
    std::vector<std::string> all_commands() override;
    std::vector<std::string> log_lines(int n) override;
    bool request_reload(const std::string& mod) override;
    int bus_publish(const std::string& from, const std::string& topic, const std::string& json_text) override;
    std::optional<std::string> bus_get(const std::string& key) override;
    void bus_set(const std::string& key, const std::string& json_text) override;
    bool set_clipboard(const std::string& text) override { return SDL_SetClipboardText(text.c_str()); }
    bool open_url(const std::string& mod, const std::string& url) override;
    std::string locale() override;
    std::string game_state() override;
    std::string room_title() override;
    std::vector<ActorInfo> actors() override;
    bool to_screen(float x, float y, float z, float* sx, float* sy) override;
    bool game_op(const std::string& mod, const std::string& op, const std::vector<double>& num, const std::string& text,
                 std::vector<double>* out) override;
    void ui_text(void* list, const std::string& t, std::uint32_t col) override {
        ui::List& L = *static_cast<ui::List*>(list);
        const float px = 17.0f * ui::S, wrap = L.size.x - 26.0f * ui::S;
        const ImVec2 ts = ui::F.body->CalcTextSizeA(px, FLT_MAX, wrap, t.c_str());
        L.dl->AddText(ui::F.body, px, {L.org.x + 7.0f * ui::S, L.org.y + L.y - L.scroll + 2.0f * ui::S}, col, t.c_str(), nullptr, wrap);
        L.y += ts.y + 12.0f * ui::S;
    }
    void ui_progress(void* list, const std::string& label, double fraction, const std::string& text, std::uint32_t col) override {
        ui::List& L = *static_cast<ui::List*>(list);
        const ui::RowIo r = ui::row_frame(L, nullptr, false, /*focusable*/false);
        const float lpx = 16.5f * ui::S, cy = (r.a.y + r.b.y) * 0.5f, x0 = r.a.x + (r.b.x - r.a.x) * 0.42f, x1 = r.b.x - 13.0f * ui::S;
        ui::text(L.dl, ui::F.cond, lpx, {r.a.x + 13.0f * ui::S, cy - lpx * 0.55f}, ui::kLabel, ui::upper(label.c_str()).c_str());
        const float bh = 10.0f * ui::S;
        L.dl->AddRectFilled({x0, cy - bh * 0.5f}, {x1, cy + bh * 0.5f}, ui::kRowEdge, bh * 0.5f);
        if (fraction > 0.0) L.dl->AddRectFilled({x0, cy - bh * 0.5f}, {x0 + (x1 - x0) * float(fraction), cy + bh * 0.5f}, col, bh * 0.5f);
        if (!text.empty()) {
            const float vpx = 14.0f * ui::S;
            const ImVec2 ts = ui::text_size(ui::F.body, vpx, text.c_str());
            ui::text(L.dl, ui::F.body, vpx, {(x0 + x1 - ts.x) * 0.5f, cy - vpx * 0.6f}, ui::kValue, text.c_str());
        }
    }
    void ui_table(void* list, const std::vector<std::string>& cells, bool header, std::uint32_t col) override {
        ui::List& L = *static_cast<ui::List*>(list);
        if (cells.empty()) return;
        const float px = (header ? 14.5f : 16.0f) * ui::S, x0 = L.org.x + 13.0f * ui::S, w = L.size.x - 26.0f * ui::S;
        const float y = L.org.y + L.y - L.scroll, cw = w / float(cells.size());
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const std::string t = header ? ui::upper(cells[i].c_str()) : cells[i];
            L.dl->PushClipRect({x0 + cw * float(i), y}, {x0 + cw * float(i + 1) - 6.0f * ui::S, y + px * 1.6f}, true);
            ui::text(L.dl, header ? ui::F.cond : ui::F.body, px, {x0 + cw * float(i), y + px * 0.2f}, header ? ui::kLabel : col, t.c_str());
            L.dl->PopClipRect();
        }
        if (header) L.dl->AddLine({x0, y + px * 1.55f}, {x0 + w, y + px * 1.55f}, ui::kRowEdge, 1.0f);
        L.y += px * 1.75f;
    }
    void ui_image(void* list, int handle, float height, std::uint32_t tint) override;
    bool key_down(const std::string& name) override;
    bool pad_down(const std::string& name) override;
    bool key_known(const std::string& name, bool pad) override;
};

struct LuaRt {
    LuaHost host;
    LuaBind b;
    std::vector<std::unique_ptr<script::Mod>> mods;
    bool in_game = false;                  // a NewGame / Load was seen
    ImDrawList* dl = nullptr;              // the list a hud layer draws into (only while it runs)
    Gpu* gpu = nullptr;                    // for cleria hud images (set by lua_seed_icons)
    std::vector<SpriteTex> images;         // handle = index + 1
    std::deque<LuaNotice> notices;
    std::map<std::string, std::string> mem_settings;   // a run without a settings file
    double last_frame = -1;
    // cleria.player.set_modifier: per mod {add, mul} of max HP / ATK / DEF, and their totals (kept for the process)
    std::map<std::string, std::array<std::pair<double, double>, 3>> stat_mods;
    bool stat_any = false;
    double stat_add[3] = {0, 0, 0}, stat_mul[3] = {1, 1, 1};
    std::map<std::string, std::string> bus;            // cleria.bus.get / set: JSON text by key (the process)
    std::string last_state;                            // state_changed / floor_changed: what the last tick saw
    std::map<std::string, double> speed;               // cleria.game.set_speed: each mod's factor (their product applies)
    // cleria.player.tune: each mod's factors (0 speed, 1 jump, 2 mp_cost, 3 mp_regen, 4 boost_gain) and extra air jumps (5)
    std::map<std::string, std::array<double, 6>> tune;
    struct Hit { int id; int dmg; float knock; int cls; };
    std::vector<Hit> hits;                             // cleria.combat.damage: landed at the next hit scan
    int forced_dmg = -1;                               // >= 0 while one of them lands: the damage, as given
    std::map<std::string, unsigned> hud_hidden;        // cleria.hud.show: each mod's hidden parts (1 status, 2 boss, 4 messages)
    std::set<std::string> pause;                       // cleria.game.pause: the mods holding the game
    float look_scale = 1.0f;                           // cleria.player.set_look: the player's sprite scale and glow (look_tint)
    std::uint32_t look_tint = 0xffffffffu;
    bool look_on = false;
    std::string spawn_by;                              // the mod whose world.spawn / world.apply is building a monster now
    int newgame_char = 0, newgame_diff = 2;            // cleria.game.new_game: asked for (0 = no), started by the title
    std::deque<std::string> log_tail;                  // the mods' last log lines (the console)
    std::set<std::string> reload;                      // mods whose script starts afresh at the next frame
    std::map<std::string, std::int64_t> watch;         // CLERIA_MOD_WATCH: each script file's last write time
    int watch_in = 0;
    std::map<std::string, ui::List> win_lists;         // cleria.ui.window: the list state of each window (mod id + window id)
    ui::Nav drive_nav{};                               // a drive run's keys for the open window (modwin=ok / back / up / down)
    std::string win_shown;                             // the window drawn last frame (mod id + its open serial)
    int last_floor = -2;
};
static LuaRt g_lua;
static std::set<int> g_lua_puppets;                    // cleria.world.puppet: the actor ids the mods own in this room
static bool g_lua_info(script::PlayerInfo& out) { return g_lua.b.info && g_lua.b.info(out); }

static mods::Mod* lua_mods_entry(const std::string& id) {
    for (mods::Mod& m : g_mods_ui.list)
        if (m.m.id == id) return &m;
    return nullptr;
}

// ---- the host ------------------------------------------------------------------------------------------
void LuaHost::notice(const std::string& mod, const std::string& head, const std::string& text) {
    (void)mod;
    g_lua.notices.push_back({head, text, now()});
    while (g_lua.notices.size() > 4) g_lua.notices.pop_front();
    std::printf("  [lua] notice %s: %s\n", head.c_str(), text.c_str());
}
void LuaHost::failed(const std::string& mod, const std::string& error) {
    if (mods::Mod* m = lua_mods_entry(mod)) {   // the Mods tab: "Stopped" and the error (still loaded: no restart pending)
        m->errors.push_back("script stopped: " + error);
        m->stopped = true;
    }
    g_lua.notices.push_back({"Mod stopped", mod + ": " + error, now()});
}
bool LuaHost::in_game() { return g_lua.in_game && g_lua.b.flags; }
std::int32_t* LuaHost::flags() { return g_lua.b.flags; }
std::int32_t* LuaHost::upgrades() { return g_lua.b.item_arr; }
std::uint8_t* LuaHost::warps() { return g_lua.b.warp_on; }
int LuaHost::character() { return g_lua.b.character ? *g_lua.b.character : 1; }
bool LuaHost::frozen() { return g_lua.b.frozen && g_lua.b.frozen(); }
double LuaHost::stat(Stat s) {
    const Player* p = g_lua.b.player;
    if (!p) return 0;
    switch (s) {
        case Stat::Hp: return p->hp;
        case Stat::MaxHp: return p->max_hp;
        case Stat::Sp: return p->sp;
        case Stat::Level: return p->level;
        case Stat::Exp: return p->exp;
        case Stat::WeaponTier: return g_lua.b.flags ? g_lua.b.flags[148] : 0;
    }
    return 0;
}
void LuaHost::set_stat(Stat s, double v) {
    Player* p = g_lua.b.player;
    if (!p) return;
    const int i = int(std::lround(v));
    switch (s) {
        case Stat::Hp: p->hp = std::clamp(i, 0, p->max_hp); break;
        case Stat::Sp: p->sp = std::clamp(i, 0, 999999); break;            // the live wallet (as content_give / AP)
        case Stat::Level: if (g_lua.b.set_level) g_lua.b.set_level(i); break;
        case Stat::WeaponTier: if (g_lua.b.set_weapon_tier) g_lua.b.set_weapon_tier(std::clamp(i, 0, 8)); break;
        default: break;                                                    // MaxHp / Exp follow the stat model
    }
}
void LuaHost::refresh() { if (g_lua.b.refresh) g_lua.b.refresh(); }
void LuaHost::add_exp(double n) { if (g_lua.b.add_exp) g_lua.b.add_exp(float(n)); }
void LuaHost::set_modifier(const std::string& mod, int stat, double add, double mul) {
    LuaRt& r = g_lua;
    if (stat < 0 || stat > 2) return;
    if (!r.stat_mods.count(mod)) r.stat_mods[mod] = {{{0.0, 1.0}, {0.0, 1.0}, {0.0, 1.0}}};   // none (API 7: x0 is a modifier)
    r.stat_mods[mod][std::size_t(stat)] = {add, mul};
    r.stat_any = false;
    for (int k = 0; k < 3; ++k) { r.stat_add[k] = 0; r.stat_mul[k] = 1; }
    for (auto& [id, a] : r.stat_mods)
        for (int k = 0; k < 3; ++k) {
            r.stat_add[k] += a[std::size_t(k)].first;
            r.stat_mul[k] *= a[std::size_t(k)].second;
            r.stat_any = r.stat_any || a[std::size_t(k)].first != 0 || a[std::size_t(k)].second != 1;
        }
    if (r.b.refresh) r.b.refresh();                        // max HP follows now; ATK / DEF are read at each hit
    if (r.b.player) r.b.player->hp = std::min(r.b.player->hp, r.b.player->max_hp);
}
std::pair<double, double> LuaHost::modifier(const std::string& mod, int stat) {
    const auto it = g_lua.stat_mods.find(mod);
    if (it == g_lua.stat_mods.end() || stat < 0 || stat > 2) return {0.0, 1.0};
    return it->second[std::size_t(stat)];
}
bool LuaHost::spawn_pickup(int item, float x, float y, float z, double value) {
    return in_game() && g_lua.b.spawn_pickup && g_lua.b.spawn_pickup(item, Vec3{x, y, z}, float(value));
}
void LuaHost::warp(int index) { if (g_lua.b.warp && g_lua.b.warp_on) g_lua.b.warp(index); }
bool LuaHost::save(int slot) { return in_game() && g_lua.b.save && g_lua.b.save(slot); }
static bool g_lua_open_toal = false;   // cleria.game.unlock_character(3): read by the title's Character Select
void LuaHost::unlock_character(int c) { if (c == 3) g_lua_open_toal = true; }
// cleria.save.set_profile: only with no game bound (the launcher, the title), so a running game's saves never
// change folder; the notice card tells the player where the saves now go.
bool LuaHost::set_save_profile(const std::string& name) {
    const std::string was = g_save_profile;
    if (g_lua.b.flags || !save_set_profile(name)) return false;
    if (g_save_profile != was)
        g_lua.notices.push_back({"Save profile", g_save_profile.empty() ? "The normal saves" : g_save_profile, now()});
    return true;
}

// ---- API 7: the mods see each other ----
std::vector<script::Host::ModInfo> LuaHost::mod_list() {
    std::vector<ModInfo> v;
    for (const mods::Mod& m : g_mods_ui.list)
        v.push_back({m.m.id, m.label(), m.m.version, m.state == mods::State::Active, !m.m.script.empty()});
    return v;
}
int LuaHost::bus_publish(const std::string& from, const std::string& topic, const std::string& json_text) {
    int n = 0;
    for (std::size_t i = 0; i < g_lua.mods.size(); ++i)        // by index: a handler may not add mods, but may fail one
        if (g_lua.mods[i]->id() != from) n += g_lua.mods[i]->on_bus(topic, json_text, from);
    return n;
}
std::optional<std::string> LuaHost::bus_get(const std::string& key) {
    const auto it = g_lua.bus.find(key);
    if (it == g_lua.bus.end()) return std::nullopt;
    return it->second;
}
void LuaHost::bus_set(const std::string& key, const std::string& json_text) {
    if (json_text.empty()) g_lua.bus.erase(key);
    else if (g_lua.bus.size() < 4096 || g_lua.bus.count(key)) g_lua.bus[key] = json_text;
}
// Only reached from a page's button (modscript_api7.inl checks it): the player pressed it.
bool LuaHost::open_url(const std::string& mod, const std::string& url) {
    std::printf("  [lua] %s opens %s\n", mod.c_str(), url.c_str());
    if (std::getenv("CLERIA_DRIVE")) return true;               // a headless run opens nothing
    return SDL_OpenURL(url.c_str());
}
std::string LuaHost::room_title() { return g_lua.b.room_title ? g_lua.b.room_title() : std::string(); }
std::vector<script::Host::ActorInfo> LuaHost::actors() {
    std::vector<ActorInfo> v;
    if (in_game() && g_lua.b.actors) g_lua.b.actors(v);
    return v;
}
bool LuaHost::to_screen(float x, float y, float z, float* sx, float* sy) {
    return in_game() && g_lua.b.to_screen && g_lua.b.to_screen(Vec3{x, y, z}, sx, sy);
}
bool LuaHost::game_op(const std::string& mod, const std::string& op, const std::vector<double>& num, const std::string& text,
                      std::vector<double>* out) {
    return in_game() && g_lua.b.op && g_lua.b.op(mod, op, num, text, out);
}
// ---- cleria.input (API 7): SDL's names for keys ("F5", "Left Shift") and pad buttons ("a", "back", "dpup") ----
bool LuaHost::key_known(const std::string& name, bool pad) {
    return pad ? SDL_GetGamepadButtonFromString(name.c_str()) != SDL_GAMEPAD_BUTTON_INVALID
               : SDL_GetKeyFromName(name.c_str()) != SDLK_UNKNOWN;
}
bool LuaHost::key_down(const std::string& name) {
    const SDL_Keycode k = SDL_GetKeyFromName(name.c_str());
    if (k == SDLK_UNKNOWN) return false;
    const SDL_Scancode sc = SDL_GetScancodeFromKey(k, nullptr);
    int n = 0;
    const bool* st = SDL_GetKeyboardState(&n);
    return st && int(sc) > 0 && int(sc) < n && st[sc];
}
bool LuaHost::pad_down(const std::string& name) {
    const SDL_GamepadButton b = SDL_GetGamepadButtonFromString(name.c_str());
    return b != SDL_GAMEPAD_BUTTON_INVALID && g_sdl_in.pad && SDL_GetGamepadButton(g_sdl_in.pad, b);
}
// A key or a pad button went down while the shell does not hold the input: the mods' actions bound to it run, then
// the "input" event (key = / pad = its name). `name` is SDL's.
static void lua_input_press(const std::string& name, bool pad) {
    if (g_lua.mods.empty() || name.empty()) return;
    for (std::size_t mi = 0; mi < g_lua.mods.size(); ++mi) {
        script::Mod& m = *g_lua.mods[mi];
        for (std::size_t i = 0; i < m.actions_.size(); ++i) {
            const std::string& bound = pad ? m.actions_[i].pad : m.actions_[i].key;
            if (bound.empty()) continue;
            const bool hit = pad ? SDL_GetGamepadButtonFromString(bound.c_str()) == SDL_GetGamepadButtonFromString(name.c_str())
                                 : SDL_GetKeyFromName(bound.c_str()) == SDL_GetKeyFromName(name.c_str());
            if (hit) m.run_action(i);
        }
    }
    content_emit(events::Event::named("input").with(pad ? "pad" : "key", name));
}
static void lua_input_event(const SDL_Event& e) {
    if (g_lua.mods.empty()) return;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) lua_input_press(SDL_GetKeyName(e.key.key), false);
    else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        const char* n = SDL_GetGamepadStringForButton(SDL_GamepadButton(e.gbutton.button));
        if (n) lua_input_press(n, true);
    }
}

// cleria.player.tune: the product of the mods' factors (k 0..4), the sum of their extra air jumps (k 5).
static double lua_tune(int k) {
    double f = k == 5 ? 0.0 : 1.0;
    for (const auto& [id, a] : g_lua.tune) { if (k == 5) f += a[5]; else f *= a[std::size_t(k)]; }
    return f;
}

// The game speed the mods ask for: the product of their factors, within the shell's own limits.
static float lua_game_speed() {
    if (g_lua.speed.empty()) return 1.0f;
    double f = 1.0;
    for (const auto& [id, x] : g_lua.speed) f *= x;
    return float(std::clamp(f, 0.1, 4.0));
}
std::string LuaHost::game_state() {
    if (!in_game() || !g_lua.b.state) return "title";
    for (auto& m : g_lua.mods)
        if (m->ok() && m->window() && m->window()->pause) return "window";   // a mod's own window holds the game
    if (!g_lua.pause.empty()) return "held";                                 // a mod holds it (game.pause)
    return g_lua.b.state();
}
std::string LuaHost::locale() {
    static const char* const k[] = {"ja", "en", "es", "fr", "de", "it"};
    const int l = int(settings().s.game.language);
    return l >= 0 && l < 6 ? k[l] : "en";
}

static std::string lua_setting_sec(const std::string& mod) { return "cleria.mod." + mod; }
std::optional<std::string> LuaHost::setting(const std::string& mod, const std::string& key) {
    if (settings().memory_only()) {
        const auto it = g_lua.mem_settings.find(mod + "/" + key);
        if (it == g_lua.mem_settings.end()) return std::nullopt;
        return it->second;
    }
    auto v = settings().cleria_doc().raw(lua_setting_sec(mod), key);
    if (v && v->size() >= 2 && v->front() == '"' && v->back() == '"') v = v->substr(1, v->size() - 2);
    return v;
}
bool LuaHost::set_setting(const std::string& mod, const std::string& key, const std::string& value) {
    if (settings().memory_only()) {
        g_lua.mem_settings[mod + "/" + key] = value;
        return true;
    }
    settings().cleria_doc_mut().set_string(lua_setting_sec(mod), key, value);
    settings().touch();
    return settings().save();
}
std::filesystem::path LuaHost::data_dir(const std::string& mod) {
    if (settings().memory_only() || settings().path().empty()) return {};
    return settings().path().parent_path() / "modsdata" / mod;
}
std::map<std::string, std::uint32_t> LuaHost::named_colors() {
    return {{"gold", ui::kGold}, {"label", ui::kLabel}, {"value", ui::kValue}, {"muted", ui::kMuted},
            {"desc", ui::kDesc}, {"good", ui::kGood}, {"warn", ui::kWarn}, {"section", ui::kSection},
            {"panel", ui::kPanel & 0x00FFFFFFu}, {"edge", ui::kPanelEdge & 0x00FFFFFFu}, {"row", ui::kRow},
            {"white", IM_COL32(255, 255, 255, 255)}, {"black", IM_COL32(0, 0, 0, 255)}};
}
static ImFont* lua_font(int f) {
    switch (f) {
        case 1: return ui::F.medium;
        case 2: return ui::F.bold;
        case 3: return ui::F.cond;
        case 4: return ui::F.cond_med;
        default: return ui::F.body;
    }
}
void LuaHost::draw_text(float x, float y, const std::string& t, float px, int font, std::uint32_t col) {
    if (g_lua.dl && px > 0) ui::text(g_lua.dl, lua_font(font), px, {x, y}, col, t.c_str());
}
std::pair<float, float> LuaHost::text_size(const std::string& t, float px, int font) {
    if (!lua_font(font)) return {0.0f, 0.0f};
    const ImVec2 s = ui::text_size(lua_font(font), px, t.c_str());
    return {s.x, s.y};
}
void LuaHost::draw_rect(float x0, float y0, float x1, float y1, std::uint32_t col, float rounding, float thickness) {
    if (!g_lua.dl) return;
    if (thickness <= 0) g_lua.dl->AddRectFilled({x0, y0}, {x1, y1}, col, rounding);
    else g_lua.dl->AddRect({x0, y0}, {x1, y1}, col, rounding, 0, thickness);
}
void LuaHost::draw_line(float x0, float y0, float x1, float y1, std::uint32_t col, float thickness) {
    if (g_lua.dl) g_lua.dl->AddLine({x0, y0}, {x1, y1}, col, thickness);
}
void LuaHost::draw_circle(float x, float y, float r, std::uint32_t col, float thickness) {
    if (!g_lua.dl) return;
    if (thickness <= 0) g_lua.dl->AddCircleFilled({x, y}, r, col, 40);
    else g_lua.dl->AddCircle({x, y}, r, col, 40, thickness);
}
int LuaHost::load_image(const std::filesystem::path& file) {
    if (!g_lua.gpu) return 0;
    Dds d = load_dds(file);
    if (!d.ok()) return 0;
    g_lua.images.push_back(upload_rgba(*g_lua.gpu, d.width, d.height, d.data.data()));
    return int(g_lua.images.size());
}
void LuaHost::ui_image(void* list, int handle, float height, std::uint32_t tint) {
    ui::List& L = *static_cast<ui::List*>(list);
    if (handle < 1 || handle > int(g_lua.images.size())) return;
    const SpriteTex& t = g_lua.images[std::size_t(handle - 1)];
    const float h = std::clamp(height, 16.0f, 600.0f) * ui::S, w = t.h > 0 ? h * float(t.w) / float(t.h) : h;
    const float x0 = L.org.x + 13.0f * ui::S, y = L.org.y + L.y - L.scroll + 4.0f * ui::S;
    L.dl->AddImage(ImTextureID(reinterpret_cast<std::intptr_t>(t.view)), {x0, y}, {x0 + std::min(w, L.size.x - 26.0f * ui::S), y + h}, {0, 0}, {1, 1}, tint);
    L.y += h + 12.0f * ui::S;
}
void LuaHost::draw_image(int handle, float x0, float y0, float x1, float y1, std::uint32_t tint) {
    if (!g_lua.dl || handle < 1 || handle > int(g_lua.images.size())) return;
    g_lua.dl->AddImage(ImTextureID(reinterpret_cast<std::intptr_t>(g_lua.images[std::size_t(handle - 1)].view)), {x0, y0}, {x1, y1},
                       {0, 0}, {1, 1}, tint);
}

// ---- the hooks -------------------------------------------------------------------------------------------
static void lua_startup() {
    LuaRt& r = g_lua;
    for (mods::Mod& m : g_mods_ui.list) {
        if (m.state != mods::State::Active || m.m.script.empty()) continue;
        if (m.m.api < script::kScriptApi) {
            m.notes.push_back("Script= ignored: a Lua script needs ApiVersion=3");
            continue;
        }
        auto sm = std::make_unique<script::Mod>(m.m.id, m.label(), m.m.version, m.root, r.host);
        const bool ok = sm->start(m.m.script);
        std::printf("[lua] %s: %s\n", m.label().c_str(), ok ? ("started (" + m.m.script + ")").c_str() : ("disabled: " + sm->error()).c_str());
        r.mods.push_back(std::move(sm));
    }
    if (r.mods.empty()) return;
    events::bus().subscribe([](const events::Event& e) {
        if (e.kind == events::Kind::NewGame || e.kind == events::Kind::Load) g_lua.in_game = true;
        for (auto& m : g_lua.mods) m->on_event(e);
    });
    content_add_filter([](const GrantQuery& q, const std::string& room) -> GrantDecision {
        for (auto& m : g_lua.mods)
            if (m->has_filter() && m->ok()) {
                const GrantDecision d = m->filter(q, room);
                if (d.action != GrantDecision::Action::Pass) return d;
            }
        return GrantDecision{};
    });
    content_add_shop_filter([](const ShopQuery& q, const std::string& room) -> GrantDecision {
        for (auto& m : g_lua.mods)
            if (m->has_shop_filter() && m->ok()) {
                const GrantDecision d = m->shop_filter(q, room);
                if (d.action != GrantDecision::Action::Pass) return d;
            }
        return GrantDecision{};
    });
    speedrun::set_provider("lua", [](std::vector<std::string>& out) {
        for (auto& m : g_lua.mods)
            for (std::string& s : m->modifications()) out.push_back(std::move(s));
    });
}

// The notice card (cleria.notice and a stopped mod), bottom centre, 5 s [H].
static void lua_hud_notice(ImDrawList* dl, float w, float h) {
    LuaRt& r = g_lua;
    const double now = r.host.now();
    while (!r.notices.empty() && now - r.notices.front().t > 5.0) r.notices.pop_front();
    if (r.notices.empty()) return;
    const LuaNotice& n = r.notices.back();
    const float fade = std::clamp(float(5.0 - (now - n.t)) * 2.0f, 0.0f, 1.0f);
    auto A = [&](ImU32 c) { return (c & 0x00FFFFFFu) | (ImU32(float((c >> 24) & 0xFF) * fade) << 24); };
    const float S = ui::S, hpx = 13.5f * S, bpx = 17.0f * S;
    const std::string head = ui::upper(n.head.c_str());
    const ImVec2 ts = ui::text_size(ui::F.body, bpx, n.text.c_str());
    const float bw = std::max(340.0f * S, ts.x + 40.0f * S), bh = 76.0f * S;
    const ImVec2 a{(w - bw) * 0.5f, h - 290.0f * S}, b{a.x + bw, a.y + bh};
    dl->AddRectFilled(a, b, A(IM_COL32(26, 24, 16, 235)), 6.0f * S);
    dl->AddRect(a, b, A(ui::kPanelEdge), 6.0f * S, 0, 1.2f * S);
    ui::text(dl, ui::F.cond, hpx, {a.x + 20.0f * S, a.y + 15.0f * S}, A(ui::kSection), head.c_str());
    ui::text(dl, ui::F.body, bpx, {a.x + 20.0f * S, a.y + 40.0f * S}, A(ui::kDesc), n.text.c_str());
}

// ---- cleria.ui.window / message / choose (API 7): one window of a mod over the game, in the shell's look ----
static script::Mod* lua_window_mod() {                // the first mod with a window open
    for (auto& m : g_lua.mods)
        if (m->ok() && m->window()) return m.get();
    return nullptr;
}
static bool lua_modal_pauses() {                      // the game waits under it (the default), or a mod holds it (game.pause)
    if (!g_lua.pause.empty()) return true;
    const script::Mod* m = lua_window_mod();
    return m && m->window()->pause;
}
static unsigned lua_hud_hidden() {                    // the parts of the game's HUD the mods hide
    unsigned v = 0;
    for (const auto& [id, bits] : g_lua.hud_hidden) v |= bits;
    return v;
}
static void lua_draw_window(ImDrawList* dl, float w, float h, const ui::Nav& nav_in) {
    script::Mod* m = lua_window_mod();
    if (!m) return;
    const script::Mod::Window win = *m->window();
    ui::Nav nav = nav_in;
    {   // a drive run's presses
        ui::Nav& d = g_lua.drive_nav;
        nav.up = nav.up || d.up; nav.down = nav.down || d.down; nav.ok = nav.ok || d.ok; nav.back = nav.back || d.back;
        nav.left = nav.left || d.left; nav.right = nav.right || d.right;
        d = ui::Nav{};
    }
    using namespace ui;
    if (win.pause) dl->AddRectFilled({0, 0}, {w, h}, IM_COL32(0, 0, 0, 110));
    List& L0 = g_lua.win_lists[m->id() + "/" + win.id];
    const float head = 46.0f * S, bw = w * win.width;      // as tall as its rows were last frame, within the screen
    const float bh = std::clamp(L0.content_h > 0 ? head + L0.content_h + 28.0f * S : h * 0.3f, 110.0f * S, h * 0.8f);
    const ImVec2 a{(w - bw) * 0.5f, (h - bh) * 0.5f}, b{a.x + bw, a.y + bh};
    dl->AddRectFilled(a, b, kPanel, 8.0f * S);
    dl->AddRect(a, b, kPanelEdge, 8.0f * S, 0, 1.5f * S);
    text(dl, F.cond, 20.0f * S, {a.x + 20.0f * S, a.y + 13.0f * S}, kGold, upper(win.title.c_str()).c_str());
    dl->AddLine({a.x, a.y + head}, {b.x, a.y + head}, kRule, 1.0f * S);
    List& L = g_lua.win_lists[m->id() + "/" + win.id];
    const std::string shown = m->id() + "#" + std::to_string(m->window_serial_);
    if (shown != g_lua.win_shown) { L = List{}; g_lua.win_shown = shown; }   // a newly opened window starts on its first row
    list_begin(L, dl, {a.x + 14.0f * S, a.y + head + 10.0f * S}, {bw - 28.0f * S, bh - head - 20.0f * S}, nav, true);
    m->draw_window(&L);
    list_end(L);
    if (nav.back && m->window()) m->close_window(true);
}

// The keys of a mod's actions (cleria.input.action), on its own page: a text field per action with SDL's key name.
static void lua_page_keys(script::Mod& m, ui::List& L) {
    if (m.actions_.empty()) return;
    ui::section(L, "Keys");
    for (script::Mod::Action& a : m.actions_) {
        std::string k = a.key;
        if (ui::text_field(L, a.label.c_str(), &k, "The key for this action, by its name: F5, Tab, Left Shift, A ... Empty = no key.", false, "(none)")) {
            if (k.empty() || SDL_GetKeyFromName(k.c_str()) != SDLK_UNKNOWN) {
                a.key = k;
                g_lua.host.set_setting(m.id(), "Key." + a.id, k);
            }
        }
    }
}

static void lua_shell(Shell& shell) {
    LuaRt& r = g_lua;
    if (r.mods.empty()) return;
    for (std::size_t i = 0; i < r.mods.size(); ++i) {
        script::Mod* m = r.mods[i].get();
        const std::vector<std::string> pages = m->pages();
        for (std::size_t j = 0; j < pages.size(); ++j)       // on its Mods > <mod> page (view_mods.inl), not a tab
            mods_add_page(m->id(), pages[j], [m, j](ui::List& L) {
                if (!m->ok()) {
                    if (j > 0) return;
                    ui::section(L, "Settings");
                    ui::info(L, "Status", "Stopped by an error", ui::kWarn,
                             ("This mod's script failed and was turned off until CleriaCore restarts (the error is "
                              "listed below).\n\n" + m->error()).c_str());
                    return;
                }
                m->draw_page(j, &L);
                if (j + 1 == m->pages().size()) lua_page_keys(*m, L);     // API 7: its actions' keys, after its last page
            });
        shell.hud["lua." + m->id()] = [m](ImDrawList* dl, float w, float h) {
            if (!m->ok()) return;
            g_lua.dl = dl;
            m->draw_hud(w, h, ui::S);
            g_lua.dl = nullptr;
        };
    }
    shell.hud["lua~notice"] = lua_hud_notice;   // sorts after the mods' layers
    shell.modal_on = [] { return lua_window_mod() != nullptr; };
    shell.modal = lua_draw_window;
}

static void lua_bind(LuaBind b) { g_lua.b = std::move(b); }
// The bound game lives in render_window's per-game scope, which the game over's Return to Title destroys
// (goto title_again) while the shell's layers keep drawing: scripts then see no game until the next bind.
struct LuaUnbind {
    ~LuaUnbind() {
        g_lua.b = LuaBind{};
        g_lua.in_game = false;
    }
};

// cleria.ui.icon ids into the two icon caches (the rising chest sprite, the treasure box).
static void lua_seed_icons(Gpu& g, std::unordered_map<int, SpriteTex>& sprite_cache, std::unordered_map<int, SpriteTex>& book_cache,
                           const std::function<SpriteTex(const fs::path&)>& book_load) {
    g_lua.gpu = &g;
    for (auto& m : g_lua.mods)
        for (const script::Mod::Icon& ic : m->icons()) {
            Dds d = load_dds(ic.file);
            if (!d.ok()) { std::printf("[lua] %s: icon 0x%X: %s is not a readable DDS\n", m->id().c_str(), ic.id, ic.file.string().c_str()); continue; }
            sprite_cache[ic.id] = upload_rgba(g, d.width, d.height, d.data.data());
            book_cache[ic.id] = book_load(ic.file);
        }
}

void LuaHost::log(const std::string& mod, const std::string& text) {
    script::Host::log(mod, text);
    g_lua.log_tail.push_back("[" + mod + "] " + text);
    while (g_lua.log_tail.size() > 200) g_lua.log_tail.pop_front();
}
bool LuaHost::run_command(const std::string& name, const std::string& args) {
    for (auto& m : g_lua.mods)
        if (m->ok() && m->has_command(name)) { m->command(name, args); return true; }
    return false;
}
std::vector<std::string> LuaHost::all_commands() {
    std::vector<std::string> v;
    for (auto& m : g_lua.mods)
        if (m->ok())
            for (const std::string& c : m->command_names()) v.push_back(c);
    std::sort(v.begin(), v.end());
    return v;
}
std::vector<std::string> LuaHost::log_lines(int n) {
    const std::size_t k = std::min<std::size_t>(std::size_t(std::max(n, 0)), g_lua.log_tail.size());
    return std::vector<std::string>(g_lua.log_tail.end() - std::ptrdiff_t(k), g_lua.log_tail.end());
}
bool LuaHost::request_reload(const std::string& mod) { g_lua.reload.insert(mod); return true; }

// A mod's script started afresh: its Lua state is closed (its sockets, timers, filters and handlers go with it),
// what it left with the game is dropped (its stat modifiers, tuning, game speed, hold, hidden HUD parts), and
// its main file runs again. What it built in the room (monsters, puppets) stays until the room changes.
static bool lua_reload(const std::string& id) {
    LuaRt& r = g_lua;
    for (auto& up : r.mods) {
        if (up->id() != id) continue;
        const mods::Mod* def = nullptr;
        for (const mods::Mod& m : g_mods_ui.list) if (m.m.id == id) def = &m;
        if (!def) return false;
        for (int k = 0; k < 3; ++k) r.host.set_modifier(id, k, 0.0, 1.0);
        r.stat_mods.erase(id);
        if (r.b.op && r.in_game) {
            for (int k = 0; k < 5; ++k) r.b.op(id, "tune", {double(k), 1.0}, "", nullptr);
            r.b.op(id, "tune", {5.0, 0.0}, "", nullptr);
        }
        r.tune.erase(id); r.speed.erase(id); r.pause.erase(id); r.hud_hidden.erase(id);
        up.reset();
        up = std::make_unique<script::Mod>(def->m.id, def->label(), def->m.version, def->root, r.host);
        const bool ok = up->start(def->m.script);
        std::printf("[lua] %s: %s\n", def->label().c_str(), ok ? "reloaded" : ("reload failed: " + up->error()).c_str());
        return ok;
    }
    return false;
}
// CLERIA_MOD_WATCH=1 (development): a mod whose main script file changed on disk is reloaded, checked once a second.
static void lua_watch_files() {
    LuaRt& r = g_lua;
    static const bool on = std::getenv("CLERIA_MOD_WATCH") != nullptr;
    if (!on || ++r.watch_in < 60) return;
    r.watch_in = 0;
    for (const mods::Mod& m : g_mods_ui.list) {
        if (m.m.script.empty()) continue;
        std::error_code ec;
        const auto t = fs::last_write_time(m.root / m.m.script, ec);
        if (ec) continue;
        const std::int64_t stamp = std::int64_t(t.time_since_epoch().count());
        const auto it = r.watch.find(m.m.id);
        if (it != r.watch.end() && it->second != stamp) r.reload.insert(m.m.id);
        r.watch[m.m.id] = stamp;
    }
}

static void lua_frame() {
    LuaRt& r = g_lua;
    if (r.mods.empty()) return;
    lua_watch_files();
    if (!r.reload.empty()) {
        const std::set<std::string> ids = std::move(r.reload);
        r.reload.clear();
        for (const std::string& id : ids) lua_reload(id);
    }
    const double now = r.host.now();
    const double dt = r.last_frame < 0 ? 0.0 : now - r.last_frame;
    r.last_frame = now;
    for (auto& m : r.mods) m->on_frame(dt);
}

// API 7: state_changed and floor_changed, from what this tick sees against the last one.
static int lua_floor_of(const std::string& t) {
    std::size_t i = 0;
    while (i < t.size() && t[i] == ' ') ++i;
    int n = 0;
    std::size_t d = i;
    while (d < t.size() && t[d] >= '0' && t[d] <= '9' && d - i < 2) n = n * 10 + (t[d++] - '0');
    return (d == i || d >= t.size() || t[d] != 'F' || n < 1 || n > 25) ? -1 : n;
}
static void lua_watch_state() {
    LuaRt& r = g_lua;
    const std::string st = r.host.game_state();
    if (st != r.last_state) {
        if (!r.last_state.empty()) content_emit(events::Event::named("state_changed").with("state", st).with("old", r.last_state));
        r.last_state = st;
    }
    if (!r.host.in_game()) { r.last_floor = -2; return; }
    const int fl = lua_floor_of(r.host.room_title());
    if (fl != r.last_floor) {
        if (r.last_floor != -2 && fl > 0) content_emit(events::Event::named("floor_changed").with("floor", fl).with("old", r.last_floor > 0 ? r.last_floor : 0));
        if (fl > 0 || r.last_floor == -2) r.last_floor = fl;      // a room with no floor in its name keeps the last one
    }
}

// cleria.world.add_filter (API 7): the first mod, in load order, whose filter of that kind answers.
static bool lua_world_on(const char* kind) {
    for (auto& m : g_lua.mods)
        if (m->ok() && m->has_world_filter(kind)) return true;
    return false;
}
static script::WorldDecision lua_world(const script::WorldQuery& q) {
    for (auto& m : g_lua.mods)
        if (m->ok() && m->has_world_filter(q.kind)) {
            const script::WorldDecision d = m->world_filter(q, g_content.room);
            if (d.action != script::WorldDecision::Action::Pass) return d;
        }
    return {};
}

// A named event of API 7 raised from the game's own code: built only when a scripted mod runs.
static bool lua_events_on() { return !g_lua.mods.empty(); }

static void lua_tick() {
    if (g_lua.mods.empty()) return;
    lua_watch_state();
    for (auto& m : g_lua.mods) m->on_tick();
}

static float lua_exp_factor(int level) {
    float f = 1.0f;
    for (auto& m : g_lua.mods) f *= m->exp_factor(level);
    return f;
}

// ---- cleria.combat.add_filter (API 6, LUA_API.md §5.2) ---------------------------------------------------
// The game asks lua_combat_on(kind) first and builds a query only when some mod has a filter of that kind.
static bool lua_combat_on(script::CombatQuery::Kind k) {
    for (auto& m : g_lua.mods)
        if (m->has_combat_filter(k) && m->ok()) return true;
    return false;
}
// The mods in load order; the first decision that is not Pass wins.
static script::CombatDecision lua_combat(const script::CombatQuery& q) {
    for (auto& m : g_lua.mods)
        if (m->has_combat_filter(q.kind) && m->ok()) {
            const script::CombatDecision d = m->combat_filter(q, g_content.room);
            if (d.action != script::CombatDecision::Action::Pass) return d;
        }
    return script::CombatDecision{};
}
// An SP gain of `value` (the game's own, from `base` before its scale): "kill", "boss", "pickup", "give", "script".
static int lua_sp(int value, int base, const char* source, int cast = -1) {
    if (value <= 0 || !lua_combat_on(script::CombatQuery::Kind::Sp)) return value;
    script::CombatQuery q;
    q.kind = script::CombatQuery::Kind::Sp;
    q.value = value;
    q.num = {{"base", double(base)}};
    if (cast >= 0) q.num.push_back({"cast", double(cast)});
    q.str = {{"source", source}};
    const script::CombatDecision d = lua_combat(q);
    if (d.action == script::CombatDecision::Action::Suppress) return 0;
    if (d.action == script::CombatDecision::Action::Replace) return int(std::clamp(d.value, 0.0, 999999.0));
    return value;
}
// A hit's final damage. `dealt`: the player's hit on a monster (`cast` / `level`: the victim's) or a boss part
// (cast -1); else a hit the player takes (`cast`: the attacker's, -1 = a boss, a trap, a shot). `formula` is the
// damage function's own result, `value` what is about to leave the HP.
// API 7: `actor` (the monster's id), `hp` / `max_hp` (the victim's, before the hit) when the caller knows them.
static int lua_damage(bool dealt, int value, int formula, float atk, float power, float def, int cls, int cast, int level,
                      int actor = -1, float hp = -1.0f, float max_hp = -1.0f) {
    if (!lua_combat_on(script::CombatQuery::Kind::Damage)) return value;
    script::CombatQuery q;
    q.kind = script::CombatQuery::Kind::Damage;
    q.value = value;
    q.num = {{"formula", double(formula)}, {"atk", double(atk)}, {"power", double(power)}, {"def", double(def)}};
    if (cls >= 0) q.num.push_back({"class", double(cls)});
    if (cast >= 0) { q.num.push_back({"cast", double(cast)}); q.num.push_back({"level", double(level)}); }
    if (actor >= 0) q.num.push_back({"actor", double(actor)});
    if (hp >= 0.0f) { q.num.push_back({"hp", double(hp)}); q.num.push_back({"max_hp", double(max_hp)}); }
    q.str = {{"dir", dealt ? "dealt" : "taken"}};
    const script::CombatDecision d = lua_combat(q);
    if (d.action == script::CombatDecision::Action::Suppress) return 0;
    if (d.action == script::CombatDecision::Action::Replace) return int(std::clamp(d.value, 0.0, 999999.0));
    return value;
}
// A hit of the player's on a boss part (FUN_004fa550's result `formula`): the shell's Damage-dealt cheat, then the
// "damage" filter (no cast).
static int lua_part_damage(int formula, const AttackStats& a, float def, int cls) {
    return lua_damage(true, access::damage_dealt(formula, settings().s.access.damage_dealt), formula, a.atk, a.bonus, def,
                      cls == kDamageClassNone ? -1 : cls, -1, 0);
}
// cleria.player.set_modifier on FUN_00420c40's results: (the game's + add) x mul; max HP stays within 1..999
// (the game's own cap), ATK is not negative (the damage function raises it to a power).
static void lua_stat_modifiers(PlayerStats& s) {
    const LuaRt& r = g_lua;
    if (!r.stat_any) return;
    s.max_hp = std::clamp(float((double(s.max_hp) + r.stat_add[0]) * r.stat_mul[0]), 1.0f, 999.0f);
    s.atk = std::max(0.0f, float((double(s.atk) + r.stat_add[1]) * r.stat_mul[1]));
    s.def = float((double(s.def) + r.stat_add[2]) * r.stat_mul[2]);
}

// CLERIA_DRIVE: lua=<command> <args>   (a cleria.command of an active scripted mod)
static bool lua_drive(const std::string& cmd) {
    if (cmd.rfind("modkey=", 0) == 0) { lua_input_press(cmd.substr(7), false); return true; }   // a key press for the mods (API 7)
    if (cmd.rfind("modpad=", 0) == 0) { lua_input_press(cmd.substr(7), true); return true; }
    if (cmd.rfind("modwin=", 0) == 0) {                   // a press for the mods' open window: ok / back / up / down
        const std::string k = cmd.substr(7);
        ui::Nav& d = g_lua.drive_nav;
        d.ok = k == "ok"; d.back = k == "back"; d.up = k == "up"; d.down = k == "down"; d.left = k == "left"; d.right = k == "right";
        return true;
    }
    if (cmd.rfind("modreload=", 0) == 0) { g_lua.reload.insert(cmd.substr(10)); return true; }   // a mod's script afresh (next frame)
    if (cmd.rfind("lua=", 0) != 0) return false;
    const std::string rest = cmd.substr(4);
    const std::size_t sp = rest.find(' ');
    const std::string name = rest.substr(0, sp), args = sp == std::string::npos ? std::string() : rest.substr(sp + 1);
    for (auto& m : g_lua.mods)
        if (m->has_command(name)) {
            m->command(name, args);
            return true;
        }
    std::printf("  [lua] no command '%s'\n", name.c_str());
    return true;
}
