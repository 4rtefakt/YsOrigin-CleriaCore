// Lua scripting for CleriaCore mods: the runtime (state, budget, protected calls, the game's calls
// into a mod). The sandbox and the cleria.* API are in modscript_api.cpp. See modscript.hpp and
// build/shell_re/LUA_API.md.
#include "cleria/modscript.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

// Lua is compiled as C++ (CMakeLists: lua54), so its headers are included WITHOUT extern "C" and a
// Lua error is a C++ throw that unwinds the C++ frames of the API functions.
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

namespace cleria::script {

namespace fs = std::filesystem;

namespace {
constexpr int kHookStep = 1000;   // the count hook runs every 1000 VM instructions

Mod* mod_of(lua_State* L) { return *static_cast<Mod**>(lua_getextraspace(L)); }

// The allocator: realloc / free with a cap on the mod's heap (Limits::memory). A refused block makes
// Lua run a full collection and retry, then raise "not enough memory" (caught like any error).
void* l_alloc(void* ud, void* ptr, std::size_t osize, std::size_t nsize) {
    Mod* m = static_cast<Mod*>(ud);
    const std::size_t old = ptr ? osize : 0;
    if (nsize == 0) {
        std::free(ptr);
        m->heap_ -= std::min(m->heap_, old);
        return nullptr;
    }
    if (nsize > old && m->heap_ + (nsize - old) > m->limits().memory) return nullptr;
    void* p = std::realloc(ptr, nsize);
    if (p) m->heap_ = m->heap_ - std::min(m->heap_, old) + nsize;
    return p;
}

// The instruction budget: once spent, the hook runs on EVERY instruction and raises each time, so a
// script that catches the error with pcall is stopped at its very next instruction outside that pcall
// (with the 1000-step period, `while true do pcall(loop) end` would always be hit inside `loop`), and
// the mod is disabled when the call returns.
void count_hook(lua_State* L, lua_Debug*) {
    Mod* m = mod_of(L);
    m->count_ += m->killed_.empty() ? kHookStep : 1;
    if (m->count_ > m->budget_) {
        if (m->killed_.empty()) {
            m->killed_ = "the script ran too long (more than " + std::to_string(m->budget_) +
                         " instructions in one call; an endless loop?)";
            lua_sethook(L, count_hook, LUA_MASKCOUNT, 1);
            if (m->state() && m->state() != L) lua_sethook(m->state(), count_hook, LUA_MASKCOUNT, 1);
        }
        luaL_error(L, "%s", m->killed_.c_str());
    }
}

// The message handler of every protected call: the error and a traceback.
int msgh(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    if (!msg) {
        if (luaL_callmeta(L, 1, "__tostring") && lua_type(L, -1) == LUA_TSTRING) msg = lua_tostring(L, -1);
        else msg = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    }
    luaL_traceback(L, L, msg, 1);
    return 1;
}
}  // namespace

void Host::log(const std::string& mod, const std::string& text) {
    std::printf("  [lua:%s] %s\n", mod.c_str(), text.c_str());
    std::fflush(stdout);
}

double Host::now() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

Mod::Mod(std::string id, std::string name, std::string version, fs::path root, Host& host, Limits lim)
    : id_(std::move(id)), name_(std::move(name)), version_(std::move(version)), root_(std::move(root)), host_(host),
      lim_(lim) {}

Mod::~Mod() {
    if (L_) lua_close(L_);   // __gc closes the sockets (their threads are joined)
    L_ = nullptr;
}

void Mod::add_log(const std::string& line) {
    recent.push_back(line);
    while (recent.size() > 40) recent.erase(recent.begin());
}

fs::path Mod::safe_path(const fs::path& base, const std::string& rel, std::string* err) const {
    auto bad = [&](const char* why) {
        if (err) *err = std::string(why) + ": " + rel;
        return fs::path();
    };
    if (rel.empty()) return bad("empty path");
    if (rel.find(':') != std::string::npos || rel.front() == '/' || rel.front() == '\\') return bad("not a relative path");
    if (rel.find('\0') != std::string::npos) return bad("bad path");
    std::size_t i = 0;
    while (i <= rel.size()) {
        std::size_t j = rel.find_first_of("/\\", i);
        if (j == std::string::npos) j = rel.size();
        const std::string part = rel.substr(i, j - i);
        if (part == "..") return bad("path leaves the mod folder");
        i = j + 1;
    }
    const fs::path p = base / fs::path(std::u8string(rel.begin(), rel.end()));
    // Also through links: the resolved file must stay under the resolved base.
    std::error_code ec;
    const fs::path cb = fs::weakly_canonical(base, ec);
    const fs::path cp = fs::weakly_canonical(p, ec);
    if (!ec) {
        const std::u8string b = cb.u8string(), q = cp.u8string();
        if (q.size() < b.size() || q.compare(0, b.size(), b) != 0) return bad("path leaves the mod folder");
    }
    return p;
}

std::optional<std::string> Mod::read_file(const std::string& rel, std::string* err) {
    const fs::path p = safe_path(root_, rel, err);
    if (p.empty()) return std::nullopt;
    std::error_code ec;
    const auto size = fs::file_size(p, ec);
    if (ec) { if (err) *err = "no file " + rel; return std::nullopt; }
    if (size > lim_.file_bytes) { if (err) *err = rel + " is too big"; return std::nullopt; }
    std::ifstream f(p, std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + rel; return std::nullopt; }
    std::string s{std::istreambuf_iterator<char>(f), {}};
    return s;
}

void Mod::fail(const std::string& where, const std::string& msg) {
    if (failed_) return;
    failed_ = true;
    std::string first = msg.substr(0, msg.find('\n'));
    error_ = first;
    host_.log(id_, "error in " + where + ": " + msg);
    add_log("error in " + where + ": " + first);
    host_.log(id_, "the mod is disabled until the game restarts");
    host_.failed(id_, where + ": " + first);
    for (void* s : sockets_) close_socket(s);   // its sockets close now (the userdata lives until lua_close)
}

bool Mod::pcall(int nargs, int nresults, const char* where) {
    if (!L_) return false;
    if (failed_) { lua_pop(L_, nargs + 1); return false; }
    const int base = lua_gettop(L_) - nargs;   // the function's slot
    lua_pushcfunction(L_, msgh);
    lua_insert(L_, base);
    const bool outer = depth_ == 0;
    if (outer) {
        count_ = 0;
        killed_.clear();
    }
    ++depth_;
    const int st = lua_pcall(L_, nargs, nresults, base);
    --depth_;
    if (st != LUA_OK) {
        std::string msg = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "unknown error";
        if (st == LUA_ERRMEM) msg = "out of memory (the mod's Lua heap is capped at " + std::to_string(lim_.memory >> 20) + " MB)";
        lua_settop(L_, base - 1);
        fail(where, msg);
        return false;
    }
    lua_remove(L_, base);   // the handler
    if (outer && !killed_.empty()) {   // the script caught the budget error itself: still stopped
        lua_settop(L_, base - 1);
        fail(where, killed_);
        return false;
    }
    return true;
}

bool Mod::call_ref(int ref, int nargs, int nresults, const char* where) {
    lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
    lua_insert(L_, -nargs - 1);
    return pcall(nargs, nresults, where);
}

bool Mod::start(const std::string& main_file) {
    L_ = lua_newstate(l_alloc, this);
    if (!L_) { fail("start", "cannot create a Lua state"); return false; }
    *static_cast<Mod**>(lua_getextraspace(L_)) = this;
    lua_sethook(L_, count_hook, LUA_MASKCOUNT, kHookStep);
    budget_ = lim_.main_instructions;
    // the sandbox and the API, then main_file, all under the main budget
    lua_pushcfunction(L_, [](lua_State* L) -> int {
        Mod& m = *mod_of(L);
        open_sandbox(L, m);
        open_api(L, m);
        return 0;
    });
    if (!pcall(0, 0, "start")) return false;
    std::string err;
    const auto text = read_file(main_file, &err);
    if (!text) { fail("start", err); return false; }
    std::string chunk = "@" + main_file;
    for (char& c : chunk) if (c == '\\') c = '/';
    if (luaL_loadbufferx(L_, text->data(), text->size(), chunk.c_str(), "t") != LUA_OK) {
        const std::string msg = lua_tostring(L_, -1) ? lua_tostring(L_, -1) : "cannot load";
        lua_settop(L_, 0);
        fail("start", msg);
        return false;
    }
    const bool ok_run = pcall(0, 0, main_file.c_str());
    budget_ = lim_.instructions;
    return ok_run;
}

// ---- the game's calls ----------------------------------------------------------------------------

void Mod::on_event(const events::Event& e) {
    if (!ok()) return;
    const char* kind = e.kind == events::Kind::Named ? e.name.c_str() : event_name(e.kind);
    for (std::size_t i = 0; i < handlers_.size() && ok(); ++i) {
        if (handlers_[i].kind != kind) continue;
        push_event(L_, e);
        call_ref(handlers_[i].ref, 1, 0, kind);
    }
}

// cleria.after / every (API 7). A timer that is due runs once per call at most; a repeating one that fell
// behind skips to the next period (no burst). `dead` timers (cancelled, or a finished one-shot) are dropped here.
void Mod::run_timers(bool play) {
    if (timers_.empty() || !ok()) return;
    const double now = play ? play_clock_ : host_.now();
    const std::size_t n = timers_.size();                      // timers added by a callback wait for the next call
    for (std::size_t i = 0; i < n && ok(); ++i) {
        if (timers_[i].dead || timers_[i].play != play || timers_[i].due > now) continue;
        const int ref = timers_[i].ref;
        if (timers_[i].period > 0) {
            timers_[i].due += timers_[i].period;
            if (timers_[i].due <= now) timers_[i].due = now + timers_[i].period;
        } else {
            timers_[i].dead = true;
        }
        call_ref(ref, 0, 0, "timer");
    }
    if (!L_) return;
    for (std::size_t i = timers_.size(); i-- > 0;)
        if (timers_[i].dead) {
            luaL_unref(L_, LUA_REGISTRYINDEX, timers_[i].ref);
            timers_.erase(timers_.begin() + std::ptrdiff_t(i));
        }
}

// cleria.bus.subscribe handlers of `topic`: fn(value, from).
int Mod::on_bus(const std::string& topic, const std::string& json_text, const std::string& from) {
    if (!ok()) return 0;
    const auto it = bus_subs_.find(topic);
    if (it == bus_subs_.end()) return 0;
    int n = 0;
    const std::vector<int> refs = it->second;                  // a handler may subscribe again
    for (int ref : refs) {
        if (!ok()) break;
        push_json_text(L_, json_text);
        lua_pushlstring(L_, from.data(), from.size());
        call_ref(ref, 2, 0, "bus");
        ++n;
    }
    return n;
}

void Mod::on_tick() {
    if (!ok()) return;
    if (host_.in_game() && !host_.frozen()) play_clock_ += 1.0 / 60.0;
    run_timers(true);
    for (std::size_t i = 0; i < handlers_.size() && ok(); ++i)
        if (handlers_[i].kind == "tick") call_ref(handlers_[i].ref, 0, 0, "tick");
}

void Mod::on_frame(double dt) {
    if (!ok()) return;
    run_timers(false);
    for (std::size_t i = 0; i < handlers_.size() && ok(); ++i) {
        if (handlers_[i].kind != "frame") continue;
        lua_pushnumber(L_, dt);
        call_ref(handlers_[i].ref, 1, 0, "frame");
    }
}

std::vector<std::string> Mod::pages() const {
    std::vector<std::string> v;
    for (const auto& [t, r] : pages_) v.push_back(t);
    return v;
}

bool Mod::has_command(const std::string& name) const { return commands_.count(name) != 0; }

std::vector<std::string> Mod::command_names() const {
    std::vector<std::string> v;
    for (const auto& [n, r] : commands_) v.push_back(n);
    return v;
}

void Mod::command(const std::string& name, const std::string& args) {
    if (!ok()) return;
    const auto it = commands_.find(name);
    if (it == commands_.end()) return;
    lua_pushlstring(L_, args.data(), args.size());
    call_ref(it->second, 1, 0, ("command " + name).c_str());
}

float Mod::exp_factor(int level) {
    if (!ok() || !exp_ref_) return 1.0f;
    lua_pushinteger(L_, level);
    if (!call_ref(exp_ref_, 1, 1, "exp_factor")) return 1.0f;
    float f = 1.0f;
    if (lua_isnumber(L_, -1)) f = float(lua_tonumber(L_, -1));
    lua_pop(L_, 1);
    return f > 0.0f && f < 1000.0f ? f : 1.0f;
}

std::vector<std::string> Mod::modifications() {
    std::vector<std::string> out;
    if (!ok() || !speedrun_ref_) return out;
    if (!call_ref(speedrun_ref_, 0, 1, "speedrun")) return out;
    if (lua_type(L_, -1) == LUA_TSTRING) {
        out.push_back(lua_tostring(L_, -1));
    } else if (lua_istable(L_, -1)) {
        const lua_Integer n = luaL_len(L_, -1);
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L_, -1, i);
            if (lua_type(L_, -1) == LUA_TSTRING) out.push_back(lua_tostring(L_, -1));
            lua_pop(L_, 1);
        }
    }
    lua_pop(L_, 1);
    return out;
}

}  // namespace cleria::script
