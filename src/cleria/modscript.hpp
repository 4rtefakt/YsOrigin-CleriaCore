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

inline constexpr int kApiVersion = 4;   // cleria.api_version (= mods::kApiVersion; LUA_API.md §11 lists what each level added)
inline constexpr int kScriptApi = 3;    // the manifest ApiVersion a Script= needs (when the Lua API arrived)

struct Limits {
    std::uint64_t instructions = 20'000'000;        // [H] per call into the mod (an event, a frame, a filter)
    std::uint64_t main_instructions = 400'000'000;  // [H] the main chunk (it may build big tables)
    std::size_t memory = 96u << 20;                 // [H] the Lua heap of one mod
    int websockets = 4;                             // [H] sockets open at once per mod
    std::size_t file_bytes = 16u << 20;             // [H] largest file a mod reads / writes
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
    virtual void warp(int index) { (void)index; }           // 0xa6 GoWarpPoint to a statue of the registry
    virtual bool save(int slot) { (void)slot; return false; }   // the statue save into slot 0..63; false = refused
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
    std::map<std::string, int> layers_;                         // hud layer name -> ref (drawn in name order)
    std::vector<std::pair<std::string, int>> pages_;            // title -> ref
    std::map<std::string, int> commands_;
    int exp_ref_ = 0, speedrun_ref_ = 0;
    std::vector<Icon> icons_;
    std::map<std::string, std::string> fields_;                 // text_field storage, by page + label
    std::vector<void*> sockets_;                                // live websocket userdata (closed on failure)
    std::map<std::string, int> images_;                         // path -> host image handle
    void* list_ = nullptr;                                      // the F1 page being drawn (draw_page)
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

}  // namespace cleria::script
