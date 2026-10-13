// Lua scripting for CleriaCore mods (build/shell_re/LUA_API.md). NOT retail: CleriaCore's own mod layer.
//
// A mod opts in with `Script=main.lua` (and ApiVersion=3) in its mod.ini. Each such mod gets ONE
// lua_State of its own (Lua 5.4.9, third_party/lua, built as C++ so a Lua error unwinds C++ frames),
// sandboxed:
//   - the libraries: base (no dofile / loadfile; `load` text-only), string (no string.dump), table,
//     math, utf8, coroutine, and a safe os subset (clock, time, date, difftime). No io, no package /
//     loadlib, no debug. `require` loads only the mod's own .lua files (`a.b` -> <mod>/a/b.lua).
//   - an instruction budget per call into the mod (a runaway loop is stopped), a heap cap per mod;
//   - every Lua error is caught: it is logged with file:line, reported to the host (the Mods tab)
//     and the mod is DISABLED for the session (its handlers, layers and pages stop; its sockets
//     close). The game never sees a Lua error.
// The `cleria.*` API (events, game state, the grant filter, storage, settings, UI, net, json,
// speedrun, log) is registered by open_api (modscript_api.cpp); it reaches the game only through
// Host, which the view implements (apps/cleria-view/view_lua.inl) and the tests fake
// (apps/cleria-luatest).
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cleria/mod_events.hpp"
#include "cleria/xso.hpp"   // GrantQuery / GrantDecision

struct lua_State;

namespace cleria::script {

inline constexpr int kApiVersion = 7;   // cleria.api_version (= mods::kApiVersion; LUA_API.md §11 lists what each level added)
inline constexpr int kScriptApi = 3;    // the manifest ApiVersion a Script= needs (when the Lua API arrived)

struct Limits {
    std::uint64_t instructions = 20'000'000;        // [H] per call into the mod (an event, a frame, a filter)
    std::uint64_t main_instructions = 400'000'000;  // [H] the main chunk (it may build big tables)
    std::size_t memory = 96u << 20;                 // [H] the Lua heap of one mod
    int websockets = 4;                             // [H] sockets open at once per mod
    std::size_t file_bytes = 16u << 20;             // [H] largest file a mod reads / writes
};

// cleria.combat.add_filter (API 6, LUA_API.md §5.2): one number of the player's progression, combat or pickups
// that the game is about to apply. The game fills it (only when a mod has a filter of that kind); a filter
// may answer another value.
struct CombatQuery {
    enum class Kind { Exp, Sp, Damage, Drop, Pickup };
    Kind kind = Kind::Exp;
    double value = 0;                 // what the game would apply: EXP, SP, damage, a pickup's amount
    int item = -1;                    // Drop / Pickup: the item id
    int timer = 0;                    // Pickup: a buff drop's duration in ticks
    std::vector<std::pair<const char*, double>> num;        // the query table's other fields
    std::vector<std::pair<const char*, std::string>> str;
};
inline constexpr int kCombatKinds = 5;
struct CombatDecision {
    enum class Action { Pass, Suppress, Replace } action = Action::Pass;
    double value = 0;                 // Replace: the query's value unless the filter gave one (always finite)
    int item = -1;
    int timer = 0;
};

// cleria.player.stats(): the player's numbers as the game holds them (inventory.hpp PlayerStats, the view's Player).
struct PlayerInfo {
    int level = 1, hp = 0, max_hp = 0, sp = 0, weapon_tier = 0;
    double exp = 0, exp_next = 0;     // the running total; the total the next level needs (0 at the cap)
    double atk = 0, def = 0, luck = 0;                      // FUN_00420c40's results, a mod's modifiers applied
    double base_max_hp = 0, base_atk = 0, base_def = 0;     // ... and without them
    double mp = 0, max_mp = 0, boost = 0;                   // the skill gauge 0x76a738 / 0x76a734; the Boost gauge (30000 = full)
    bool boosting = false;
    double buff[3] = {0, 0, 0};       // the Strength / Defense / MP Drop buffs 0x76b904 / 0x76b908 / 0x76b900
    int buff_timer[3] = {0, 0, 0};    // ... and their ticks left 0x76c19c / 0x76c1a0 / 0x76c1a4
    float pos[3] = {0, 0, 0};         // the feet (world, Y up)
};

// cleria.world.add_filter (API 7, LUA_API.md §12.11): something the world is about to do ("spawn", "exit"), with free
// fields both ways. The game fills the query (only when a mod has a filter of that kind); the answer's numbers and
// strings are what the kind documents.
struct WorldQuery {
    std::string kind;
    std::vector<std::pair<std::string, double>> num;
    std::vector<std::pair<std::string, std::string>> str;
};
struct WorldDecision {
    enum class Action { Pass, Suppress, Replace } action = Action::Pass;
    std::map<std::string, double> num;
    std::map<std::string, std::string> str;
    double n(const std::string& k, double def) const { const auto it = num.find(k); return it == num.end() ? def : it->second; }
};

// What a script can reach. Every method has a harmless default, so a host provides only what it has.
struct Host {
    virtual ~Host() = default;
    // ---- console / Mods tab ----
    virtual void log(const std::string& mod, const std::string& text);            // default: stdout
    virtual void notice(const std::string& mod, const std::string& head, const std::string& text) { (void)mod; (void)head; (void)text; }
    virtual void failed(const std::string& mod, const std::string& error) { (void)mod; (void)error; }   // the mod was disabled
    virtual double now();                                                         // seconds, monotonic
    // ---- the game (null / 0 when no game runs) ----
    virtual bool in_game() { return false; }
    virtual std::int32_t* flags() { return nullptr; }       // g_flags[0x200]
    virtual std::int32_t* upgrades() { return nullptr; }    // the 54 gear-upgrade cells (XsoVm::item_arr)
    virtual std::uint8_t* warps() { return nullptr; }       // the warp registry [24]
    virtual int character() { return 1; }                   // g_flags[150]: 1 Yunica, 2 Hugo, 3 Toal
    virtual std::string room() { return {}; }               // "S_1001" (upper-case), "" before the first room
    virtual bool frozen() { return false; }                 // a cutscene / box holds the world
    virtual bool save_ready() { return true; }              // false while a New Game / Load is still queued
    virtual std::map<std::string, std::pair<int, std::string>> chests() { return {}; }   // script -> {box flag, object}
    virtual std::string item_name(int id) { (void)id; return {}; }
    virtual int new_item_count(int id) { (void)id; return -1; }   // ids 0x100..0x1FF (the sidecar)
    virtual void give(int id, int count, const std::string& source) { (void)id; (void)count; (void)source; }
    virtual void emit(const events::Event& e) { (void)e; }
    enum class Stat { Hp, MaxHp, Sp, Level, Exp, WeaponTier };
    virtual double stat(Stat s) { (void)s; return 0; }
    virtual void set_stat(Stat s, double v) { (void)s; (void)v; }
    virtual void refresh() {}                               // the stat recompute after a grant
    virtual bool player_info(PlayerInfo& out) { (void)out; return false; }   // false = no game
    virtual void add_exp(double n) { (void)n; }             // EXP through the game's own level-up loop
    // A mod's modifier of a derived stat (0 max HP, 1 ATK, 2 DEF): stat = (the game's + add) x mul, at every recompute.
    virtual void set_modifier(const std::string& mod, int stat, double add, double mul) { (void)mod; (void)stat; (void)add; (void)mul; }
    virtual std::pair<double, double> modifier(const std::string& mod, int stat) { (void)mod; (void)stat; return {0.0, 1.0}; }
    // A collectable pickup of `item` thrown up from (x, y, z) as an enemy drop is; value >= 0 = what it applies.
    virtual bool spawn_pickup(int item, float x, float y, float z, double value) { (void)item; (void)x; (void)y; (void)z; (void)value; return false; }
    virtual void warp(int index) { (void)index; }           // 0xa6 GoWarpPoint to a statue of the registry
    virtual bool save(int slot) { (void)slot; return false; }   // the statue save into slot 0..63; false = refused
    virtual void unlock_character(int c) { (void)c; }       // Character Select offers character c (3 = Toal) this session
    // The save folder becomes <save dir>/profiles/<name>/ ("" = the normal one); false = refused (a game runs, a bad name).
    virtual bool set_save_profile(const std::string& name) { (void)name; return false; }
    virtual std::string save_profile() { return {}; }
    // ---- storage ----
    virtual std::map<std::string, std::string>* save_storage(const std::string& mod) { (void)mod; return nullptr; }
    virtual std::optional<std::string> setting(const std::string& mod, const std::string& key) { (void)mod; (void)key; return std::nullopt; }
    virtual bool set_setting(const std::string& mod, const std::string& key, const std::string& value) { (void)mod; (void)key; (void)value; return false; }
    virtual std::filesystem::path data_dir(const std::string& mod) { (void)mod; return {}; }   // "" = no data folder
    // ---- drawing (hud layers). Colours are packed RGBA as ImU32: r | g<<8 | b<<16 | a<<24 ----
    virtual std::map<std::string, std::uint32_t> named_colors() { return {}; }
    virtual void draw_text(float x, float y, const std::string& t, float px, int font, std::uint32_t col) { (void)x; (void)y; (void)t; (void)px; (void)font; (void)col; }
    virtual std::pair<float, float> text_size(const std::string& t, float px, int font) { (void)font; return {px * 0.5f * float(t.size()), px}; }
    virtual void draw_rect(float x0, float y0, float x1, float y1, std::uint32_t col, float rounding, float thickness) { (void)x0; (void)y0; (void)x1; (void)y1; (void)col; (void)rounding; (void)thickness; }   // thickness 0 = filled
    virtual void draw_line(float x0, float y0, float x1, float y1, std::uint32_t col, float thickness) { (void)x0; (void)y0; (void)x1; (void)y1; (void)col; (void)thickness; }
    virtual void draw_circle(float x, float y, float r, std::uint32_t col, float thickness) { (void)x; (void)y; (void)r; (void)col; (void)thickness; }   // 0 = filled
    virtual int load_image(const std::filesystem::path& file) { (void)file; return 0; }      // a handle, 0 = failed
    virtual void draw_image(int handle, float x0, float y0, float x1, float y1, std::uint32_t tint) { (void)handle; (void)x0; (void)y0; (void)x1; (void)y1; (void)tint; }
    virtual void register_icon(int id, const std::filesystem::path& file) { (void)id; (void)file; }
    // ---- the F1 page widgets (the ui kit); `list` is the page being drawn ----
    virtual void ui_section(void* list, const std::string& title) { (void)list; (void)title; }
    virtual bool ui_toggle(void* list, const std::string& label, bool* v, const std::string& desc, bool enabled) { (void)list; (void)label; (void)v; (void)desc; (void)enabled; return false; }
    virtual bool ui_slider(void* list, const std::string& label, float* v, float lo, float hi, float step, const std::string& fmt, const std::string& desc, bool enabled) { (void)list; (void)label; (void)v; (void)lo; (void)hi; (void)step; (void)fmt; (void)desc; (void)enabled; return false; }
    virtual bool ui_choice(void* list, const std::string& label, int* idx, const std::vector<std::string>& items, const std::string& desc, bool enabled) { (void)list; (void)label; (void)idx; (void)items; (void)desc; (void)enabled; return false; }
    virtual bool ui_button(void* list, const std::string& label, const std::string& desc, bool enabled) { (void)list; (void)label; (void)desc; (void)enabled; return false; }
    virtual bool ui_text_field(void* list, const std::string& label, std::string* v, const std::string& desc, bool secret, const std::string& empty) { (void)list; (void)label; (void)v; (void)desc; (void)secret; (void)empty; return false; }
    virtual void ui_info(void* list, const std::string& label, const std::string& value, std::uint32_t col, const std::string& desc) { (void)list; (void)label; (void)value; (void)col; (void)desc; }
    // ---- API 7 ----
    struct ModInfo { std::string id, name, version; bool active = false, scripted = false; };
    virtual std::vector<ModInfo> mod_list() { return {}; }      // every installed mod, in load order
    // cleria.bus: a message to the other scripted mods (-> how many handlers ran), and a process-wide key / value.
    virtual int bus_publish(const std::string& from, const std::string& topic, const std::string& json_text) { (void)from; (void)topic; (void)json_text; return 0; }
    virtual std::optional<std::string> bus_get(const std::string& key) { (void)key; return std::nullopt; }
    virtual void bus_set(const std::string& key, const std::string& json_text) { (void)key; (void)json_text; }
    virtual bool set_clipboard(const std::string& text) { (void)text; return false; }
    virtual bool open_url(const std::string& mod, const std::string& url) { (void)mod; (void)url; return false; }
    virtual std::string locale() { return "en"; }               // the game's text language: "en", "fr", "ja" ...
    // What the game is doing: "title", "play", "cutscene", "dialog", "menu", "book", "panel", "gameover".
    virtual std::string game_state() { return in_game() ? "play" : "title"; }
    virtual std::string room_title() { return {}; }             // the room's own name ("2F Path 1"), "" when it has none
    // cleria.world: the room's monsters (living enemy actors; never the player, an NPC or a boss module's parts).
    struct ActorInfo {
        int id = 0, cast = 0, level = 0;
        std::string name;                                       // CASTINFO's display name
        float hp = 0, max_hp = 0, atk = 0, def = 0;
        float pos[3] = {0, 0, 0}, height = 0, yaw = 0;          // the feet (world, Y up), the collision height, the heading
        bool hittable = true;
        std::string kind;                                       // a mod's variant ("<mod>:<name>"), else empty
        bool boss = false, ai = true;                           // the boss gauge is its own; its handler runs
    };
    virtual std::vector<ActorInfo> actors() { return {}; }
    // A write (or a small read) of the live game by name: the view implements each next to the state it touches
    // (apps/cleria-view/view_lua_ops.inl). False = no game, an unknown op, or the game refused; `out` gets an op's numbers.
    // More page widgets (API 7): a wrapped paragraph; a bar (fraction 0..1) with its text.
    virtual void ui_text(void* list, const std::string& text, std::uint32_t col) { (void)list; (void)text; (void)col; }
    virtual void ui_progress(void* list, const std::string& label, double fraction, const std::string& text, std::uint32_t col) { (void)list; (void)label; (void)fraction; (void)text; (void)col; }
    // cleria.input: is this key / pad button held now; is the name one the host knows (SDL's names: "F5", "Left Shift", "A";
    // pad: "a", "b", "x", "y", "back", "start", "leftshoulder", "dpup" ...).
    virtual bool key_down(const std::string& name) { (void)name; return false; }
    virtual bool pad_down(const std::string& name) { (void)name; return false; }
    virtual bool key_known(const std::string& name, bool pad) { (void)name; (void)pad; return true; }
    virtual bool game_op(const std::string& mod, const std::string& op, const std::vector<double>& num, const std::string& text,
                         std::vector<double>* out) { (void)mod; (void)op; (void)num; (void)text; (void)out; return false; }
    // A world point on the screen as fractions of the game picture (0..1 from its top left); false = behind the camera.
    virtual bool to_screen(float x, float y, float z, float* sx, float* sy) { (void)x; (void)y; (void)z; (void)sx; (void)sy; return false; }
};

// One scripted mod: its Lua state, sandbox, budget, handlers, and the calls the game makes into it.
class Mod {
public:
    Mod(std::string id, std::string name, std::string version, std::filesystem::path root, Host& host, Limits lim = {});
    ~Mod();
    Mod(const Mod&) = delete;
    Mod& operator=(const Mod&) = delete;

    // Create the state, open the sandbox and the API, run `main_file` (relative to the mod folder).
    // False = the mod is disabled (error() says why).
    bool start(const std::string& main_file);
    bool ok() const { return L_ != nullptr && !failed_; }
    const std::string& error() const { return error_; }      // "main.lua:12: attempt to ..." (first line)
    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& version() const { return version_; }
    const std::filesystem::path& root() const { return root_; }
    Host& host() { return host_; }
    const Limits& limits() const { return lim_; }

    // ---- the game calls into the mod (each a protected call; nothing happens once disabled) ----
    void on_event(const events::Event& e);
    void on_tick();
    void on_frame(double dt);
    bool has_filter() const { return !filters_.empty(); }
    GrantDecision filter(const GrantQuery& q, const std::string& room);
    bool has_shop_filter() const { return !shop_filters_.empty(); }
    GrantDecision shop_filter(const ShopQuery& q, const std::string& room);   // cleria.shop.add_filter
    bool has_combat_filter(CombatQuery::Kind k) const { return !combat_filters_[int(k)].empty(); }
    CombatDecision combat_filter(const CombatQuery& q, const std::string& room);   // cleria.combat.add_filter
    void draw_hud(float w, float h, float scale);
    std::vector<std::string> pages() const;                    // titles, in registration order
    void draw_page(std::size_t i, void* list);
    bool has_command(const std::string& name) const;
    void command(const std::string& name, const std::string& args);
    std::vector<std::string> command_names() const;
    float exp_factor(int level);                               // 1 when the mod sets none
    std::vector<std::string> modifications();                  // the speedrun "modified run" lines
    struct Icon { int id; std::filesystem::path file; };
    const std::vector<Icon>& icons() const { return icons_; }
    std::vector<std::string> recent;                            // the last log lines (the Mods tab)

    // ---- for the API (modscript_api.cpp) ----
    struct Handler { int id; std::string kind; int ref; };
    std::vector<Handler> handlers_;
    int next_handler_ = 1;
    std::vector<int> filters_;                                  // registry refs
    std::vector<int> shop_filters_;                             // cleria.shop.add_filter
    std::vector<int> combat_filters_[kCombatKinds];             // cleria.combat.add_filter, by CombatQuery::Kind
    std::map<std::string, int> layers_;                         // hud layer name -> ref (drawn in name order)
    std::vector<std::pair<std::string, int>> pages_;            // title -> ref
    std::map<std::string, int> commands_;
    int exp_ref_ = 0, speedrun_ref_ = 0;
    std::vector<Icon> icons_;
    std::map<std::string, std::string> fields_;                 // text_field storage, by page + label
    std::vector<void*> sockets_;                                // live websocket userdata (closed on failure)
    std::map<std::string, int> images_;                         // path -> host image handle
    void* list_ = nullptr;                                      // the F1 page being drawn (draw_page)
    // ---- API 7 (modscript_api7.inl) ----
    struct Timer { int id = 0; double due = 0, period = 0; int ref = 0; bool play = false, dead = false; };
    std::vector<Timer> timers_;                                 // cleria.after / every
    int next_timer_ = 1;
    double play_clock_ = 0;                                     // seconds of play: ticks / 60 while a game runs unfrozen
    std::map<std::string, std::vector<int>> bus_subs_;          // cleria.bus.subscribe: topic -> refs
    void run_timers(bool play);
    int raw_sockets_ = 0;                                       // cleria.net.udp / tcp userdata alive
    // cleria.content.define_monster / define_boss: a variant of an enemy cast, and the ones alive in this room.
    struct MonsterDef {
        int base = 0;
        double level = 0, hp = 1, atk = 1, def = 1, exp = 1, sp = 1, scale = -1, tint = -1;
        bool ai = true, boss = false;
        std::string name;
        int on_spawn = 0, on_hit = 0, on_death = 0;             // refs (0 = none)
        std::vector<std::pair<double, int>> phases;             // HP fraction (falling) -> ref
    };
    std::map<std::string, MonsterDef> monsters_;
    struct LiveMonster { std::string kind; std::size_t phase = 0; };
    std::map<int, LiveMonster> live_monsters_;                  // actor id -> its variant
    void monster_event(const events::Event& e);                 // on_hit / phases / on_death; a room change forgets them
    std::vector<std::pair<std::shared_ptr<void>, int>> http_jobs_;   // cleria.net.http: the request's state, its callback ref
    void run_http();                                            // the finished ones' callbacks (each frame)
    // cleria.input.action: a named action of the mod with its key and pad button (the player's choice is a setting,
    // "Key.<id>" / "Pad.<id>"; "" = none). The view matches key and button presses against these.
    struct Action { std::string id, label, key, pad; int ref = 0; };
    std::vector<Action> actions_;
    void run_action(std::size_t i);                             // its function, if it has one
    // cleria.ui.window: a panel of the mod's own over the game (the page widgets), and cleria.ui.message / choose,
    // a built-in one. One is open at a time per mod; the host draws it and gives it the input.
    struct Window { std::string id, title; int ref = 0; float width = 0.42f; bool pause = true; };
    std::vector<Window> windows_;
    std::string open_window_;                                   // "" = none; "\x01dialog" = the dialog below
    struct Dialog { std::string title, text; std::vector<std::string> options; int ref = 0; bool cancel = true; };
    Dialog dialog_;
    int window_serial_ = 0;                                     // + 1 at each open: the host starts its list afresh
    std::map<std::string, std::vector<int>> world_filters_;     // cleria.world.add_filter: kind -> refs
    bool has_world_filter(const std::string& kind) const { const auto it = world_filters_.find(kind); return it != world_filters_.end() && !it->second.empty(); }
    WorldDecision world_filter(const WorldQuery& q, const std::string& room);
    const Window* window() const;                               // the open one (the dialog as a Window), or null
    void draw_window(void* list);                               // its rows, into the host's list
    void close_window(bool cancelled);                          // Back / Esc, or ui.close()
    int on_bus(const std::string& topic, const std::string& json_text, const std::string& from);   // -> handlers called
    std::size_t page_ = 0;
    bool drawing_ = false;                                      // inside draw_hud (the d:* calls draw)
    void add_log(const std::string& line);
    // Read a file of the mod folder: `rel` must stay inside it (no absolute path, drive or "..").
    std::optional<std::string> read_file(const std::string& rel, std::string* err);
    std::filesystem::path safe_path(const std::filesystem::path& base, const std::string& rel, std::string* err) const;
    // Protected call of the function (and `nargs` args) on the stack; false = it failed (mod disabled).
    bool pcall(int nargs, int nresults, const char* where);
    void fail(const std::string& where, const std::string& msg);
    lua_State* state() { return L_; }

    // the budget / heap (the hook and the allocator)
    std::uint64_t count_ = 0, budget_ = 0;
    std::size_t heap_ = 0;
    std::string killed_;                                        // set when the budget / heap ran out
    int depth_ = 0;

private:
    std::string id_, name_, version_;
    std::filesystem::path root_;
    Host& host_;
    Limits lim_;
    lua_State* L_ = nullptr;
    bool failed_ = false;
    std::string error_;
    bool call_ref(int ref, int nargs, int nresults, const char* where);   // pushes the ref'd function under the args
};

// The cleria.* API and the sandbox (modscript_api.cpp).
void open_sandbox(lua_State* L, Mod& m);
void open_api(lua_State* L, Mod& m);
void close_socket(void* ud);   // a cleria.net websocket userdata (Mod::sockets_)
// events::Event -> a Lua table (pushed); the kind names of LUA_API.md ("room_enter", ...).
void push_event(lua_State* L, const events::Event& e);
const char* event_name(events::Kind k);
void push_json_text(lua_State* L, const std::string& json_text);   // a JSON text as a Lua value (nil when it does not parse)

}  // namespace cleria::script
