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
};
static LuaRt g_lua;
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
    r.stat_mods[mod][std::size_t(stat)] = {add, mul};      // a new entry is value-initialised: {0, 0} ...
    r.stat_any = false;
    for (int k = 0; k < 3; ++k) { r.stat_add[k] = 0; r.stat_mul[k] = 1; }
    for (auto& [id, a] : r.stat_mods)
        for (int k = 0; k < 3; ++k) {
            if (a[std::size_t(k)].first == 0 && a[std::size_t(k)].second == 0) a[std::size_t(k)].second = 1;   // ... = none
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
            });
        shell.hud["lua." + m->id()] = [m](ImDrawList* dl, float w, float h) {
            if (!m->ok()) return;
            g_lua.dl = dl;
            m->draw_hud(w, h, ui::S);
            g_lua.dl = nullptr;
        };
    }
    shell.hud["lua~notice"] = lua_hud_notice;   // sorts after the mods' layers
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

static void lua_frame() {
    LuaRt& r = g_lua;
    if (r.mods.empty()) return;
    const double now = r.host.now();
    const double dt = r.last_frame < 0 ? 0.0 : now - r.last_frame;
    r.last_frame = now;
    for (auto& m : r.mods) m->on_frame(dt);
}

static void lua_tick() {
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
static int lua_damage(bool dealt, int value, int formula, float atk, float power, float def, int cls, int cast, int level) {
    if (!lua_combat_on(script::CombatQuery::Kind::Damage)) return value;
    script::CombatQuery q;
    q.kind = script::CombatQuery::Kind::Damage;
    q.value = value;
    q.num = {{"formula", double(formula)}, {"atk", double(atk)}, {"power", double(power)}, {"def", double(def)}};
    if (cls >= 0) q.num.push_back({"class", double(cls)});
    if (cast >= 0) { q.num.push_back({"cast", double(cast)}); q.num.push_back({"level", double(level)}); }
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
