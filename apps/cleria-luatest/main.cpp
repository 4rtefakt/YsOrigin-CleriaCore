// cleria-luatest — Lua scripting for mods (src/cleria/modscript, build/shell_re/LUA_API.md): the sandbox
// refuses escapes, a runaway loop / a heap bomb / an error disables only that mod, and the cleria.* API
// round-trips through a fake host (events, flags, the grant filter, storage, settings, data files,
// json, the hud and page calls, commands, websockets). Also: the save sidecar's [mod.<id>] storage,
// the Script= manifest key, and the sample mods (hello, archipelago_lua) starting cleanly.
// Usage: cleria-luatest [asset_root]   (writes only under %TEMP%\cleria-luatest; run from the repo root)
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <cassert>

#include "cleria/json.hpp"
#include "cleria/modscript.hpp"
#include "cleria/mods.hpp"
#include "cleria/sidecar.hpp"

using namespace cleria;
namespace fs = std::filesystem;

static int fails = 0;
static void check(bool ok, const std::string& what) {
    std::printf("[lua] %-92s %s\n", what.c_str(), ok ? "OK" : "FAIL");
    fails += !ok;
}

static void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// A fake game: flags, a sidecar map, settings, a data folder; it records what scripts do.
struct FakeHost : script::Host {
    std::int32_t fl[0x200] = {};
    std::int32_t up[54] = {};
    std::uint8_t wp[24] = {};
    std::vector<std::string> logs, fails_seen, notices, draws, widgets;
    std::map<std::string, std::map<std::string, std::string>> store;
    std::map<std::string, std::string> settings;
    fs::path data;
    std::vector<events::Event> emitted;
    std::vector<std::pair<int, int>> given;
    double hp = 100, max_hp = 100, sp = 50, level = 3, tier = 1;
    bool game = true;
    int warped = -1, saved = -1;
    bool toggle_flip = false, press = false;

    void log(const std::string& mod, const std::string& text) override {
        logs.push_back(text);
        std::printf("    [lua:%s] %s\n", mod.c_str(), text.c_str());
    }
    void notice(const std::string&, const std::string& head, const std::string& text) override { notices.push_back(head + "|" + text); }
    void failed(const std::string&, const std::string& error) override { fails_seen.push_back(error); }
    bool in_game() override { return game; }
    std::int32_t* flags() override { return game ? fl : nullptr; }
    std::int32_t* upgrades() override { return up; }
    std::uint8_t* warps() override { return wp; }
    std::string room() override { return "S_1001"; }
    std::map<std::string, std::pair<int, std::string>> chests() override { return {{"s_box01", {302, "box_01"}}}; }
    std::string item_name(int id) override { return id == 0x59 ? "Celcetan Panacea" : "?"; }
    void give(int id, int count, const std::string&) override { given.push_back({id, count}); }
    void emit(const events::Event& e) override { emitted.push_back(e); }
    double stat(Stat s) override {
        switch (s) {
            case Stat::Hp: return hp;
            case Stat::MaxHp: return max_hp;
            case Stat::Sp: return sp;
            case Stat::Level: return level;
            case Stat::WeaponTier: return tier;
            default: return 0;
        }
    }
    void set_stat(Stat s, double v) override {
        if (s == Stat::Hp) hp = v;
        if (s == Stat::Sp) sp = v;
        if (s == Stat::Level) level = v;
        if (s == Stat::WeaponTier) tier = v;
    }
    void warp(int i) override { warped = i; }
    // API 6
    double exp_added = 0;
    std::map<int, std::pair<double, double>> mods6;
    std::vector<std::string> spawned;
    bool player_info(script::PlayerInfo& o) override {
        if (!game) return false;
        o.level = int(level); o.hp = int(hp); o.max_hp = int(max_hp); o.atk = 46; o.base_atk = 40; o.mp = 1500; o.max_mp = 3000;
        o.buff[0] = 0.5; o.buff_timer[0] = 1800; o.pos[0] = 1; o.pos[1] = 2; o.pos[2] = 3;
        return true;
    }
    void add_exp(double n) override { exp_added += n; }
    void set_modifier(const std::string&, int stat, double add, double mul) override { mods6[stat] = {add, mul}; }
    std::pair<double, double> modifier(const std::string&, int stat) override { return mods6.count(stat) ? mods6[stat] : std::make_pair(0.0, 1.0); }
    bool spawn_pickup(int item, float x, float y, float z, double value) override {
        char b[96];
        std::snprintf(b, sizeof b, "%d at %.0f,%.0f,%.0f value %.0f", item, x, y, z, value);
        spawned.push_back(b);
        return true;
    }
    bool save(int slot) override { if (slot == 9) return false; saved = slot; return true; }   // 9: "not now"
    // API 7
    double clock = -1;                                         // >= 0: the test's clock
    double now() override { return clock >= 0 ? clock : script::Host::now(); }
    std::vector<script::Mod*> peers;
    std::map<std::string, std::string> busv;
    std::string clip;
    std::vector<std::string> urls;
    std::vector<ModInfo> mod_list() override { return {{"api7a", "A", "1.0", true, true}, {"off", "Off", "2.0", false, false}}; }
    int bus_publish(const std::string& from, const std::string& topic, const std::string& text) override {
        int n = 0;
        for (script::Mod* p : peers) if (p->id() != from) n += p->on_bus(topic, text, from);
        return n;
    }
    std::optional<std::string> bus_get(const std::string& k) override { return busv.count(k) ? std::optional<std::string>(busv[k]) : std::nullopt; }
    void bus_set(const std::string& k, const std::string& v) override { if (v.empty()) busv.erase(k); else busv[k] = v; }
    bool set_clipboard(const std::string& t) override { clip = t; return true; }
    bool open_url(const std::string&, const std::string& u) override { urls.push_back(u); return true; }
    std::string locale() override { return "fr"; }
    std::string state7 = "play", title7 = " 4F - Wailing Blue";
    std::string game_state() override { return state7; }
    std::string room_title() override { return title7; }
    std::vector<ActorInfo> actors() override {
        ActorInfo a;
        a.id = 17; a.cast = 17; a.level = 3; a.name = "Selnarg"; a.hp = 54; a.max_hp = 74; a.pos[0] = 1; a.pos[1] = 2; a.pos[2] = 3; a.height = 1.5f;
        return {a};
    }
    bool to_screen(float x, float, float z, float* sx, float* sy) override { if (z < 0) return false; *sx = x / 10; *sy = 0.5f; return true; }
    void ui_text(void*, const std::string& t, std::uint32_t) override { widgets.push_back("text " + t); }
    void ui_progress(void*, const std::string& label, double f, const std::string& t, std::uint32_t) override {
        char b[64];
        std::snprintf(b, sizeof b, " %.2f ", f);
        widgets.push_back("progress " + label + b + t);
    }
    std::string held7;                                          // the one key / button held
    bool key_down(const std::string& n) override { return n == held7; }
    bool pad_down(const std::string& n) override { return n == held7; }
    bool key_known(const std::string& n, bool pad) override { return pad ? n == "back" || n == "a" : n != "NoSuchKey"; }
    std::vector<std::string> ops7;                              // game_op: "mod op n0,n1 [text]"
    bool game_op(const std::string& mod, const std::string& op, const std::vector<double>& num, const std::string& text,
                 std::vector<double>* out) override {
        std::string s = mod + " " + op;
        for (double v : num) { char b[32]; std::snprintf(b, sizeof b, " %g", v); s += b; }
        if (!text.empty()) s += " [" + text + "]";
        ops7.push_back(s);
        if (op == "equipment" && out) *out = {0, 6, 30, -1, 116, -1};
        if (op == "status" && out) *out = {900, 0, 0, 12};
        if (op == "exits" && out) *out = {1003, 1009};
        if (op == "world_snapshot" && out) *out = {21, 43, 5, 1, 2, 3, 90, 50, 80, 301, 4, 15};
        if (op == "world_apply" && out) *out = {1};
        if (op == "spawn_actor" && out && !num.empty() && num[0] == 43) *out = {0};   // cast 43 is built (id 0); any other is refused
        return op != "retry";                                   // "not now"
    }
    std::string profile;
    bool set_save_profile(const std::string& n) override { if (n == "no") return false; profile = n; return true; }   // "no": refused
    std::string save_profile() override { return profile; }
    std::map<std::string, std::string>* save_storage(const std::string& mod) override { return &store[mod]; }
    std::optional<std::string> setting(const std::string& mod, const std::string& key) override {
        const auto it = settings.find(mod + "/" + key);
        if (it == settings.end()) return std::nullopt;
        return it->second;
    }
    bool set_setting(const std::string& mod, const std::string& key, const std::string& value) override {
        settings[mod + "/" + key] = value;
        return true;
    }
    fs::path data_dir(const std::string& mod) override { return data / mod; }
    std::map<std::string, std::uint32_t> named_colors() override { return {{"gold", 0xFF3EB0D6u}}; }
    void draw_text(float x, float y, const std::string& t, float px, int font, std::uint32_t col) override {
        char b[160];
        std::snprintf(b, sizeof b, "text %.0f,%.0f %s %.0f f%d %08X", x, y, t.c_str(), px, font, col);
        draws.push_back(b);
    }
    void draw_rect(float x0, float y0, float x1, float y1, std::uint32_t col, float, float th) override {
        char b[120];
        std::snprintf(b, sizeof b, "rect %.0f,%.0f-%.0f,%.0f %08X %s", x0, y0, x1, y1, col, th == 0 ? "fill" : "line");
        draws.push_back(b);
    }
    void draw_circle(float x, float y, float r, std::uint32_t col, float) override {
        char b[96];
        std::snprintf(b, sizeof b, "circle %.0f,%.0f r%.0f %08X", x, y, r, col);
        draws.push_back(b);
    }
    int load_image(const fs::path& f) override { return fs::exists(f) ? 7 : 0; }
    void draw_image(int h, float, float, float, float, std::uint32_t) override { draws.push_back("image " + std::to_string(h)); }
    void ui_section(void*, const std::string& t) override { widgets.push_back("section " + t); }
    bool ui_toggle(void*, const std::string& l, bool* v, const std::string&, bool) override {
        widgets.push_back("toggle " + l);
        if (toggle_flip) { *v = !*v; return true; }
        return false;
    }
    bool ui_button(void*, const std::string& l, const std::string&, bool) override { widgets.push_back("button " + l); return press; }
    bool ui_text_field(void*, const std::string& l, std::string* v, const std::string&, bool secret, const std::string&) override {
        widgets.push_back("field " + l + (secret ? " (secret)" : "") + " = " + *v);
        if (press) { *v = "typed"; return true; }
        return false;
    }
    void ui_info(void*, const std::string& l, const std::string& v, std::uint32_t, const std::string&) override {
        widgets.push_back("info " + l + " = " + v);
    }
};

static bool has(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& x : v) if (x.find(s) != std::string::npos) return true;
    return false;
}

// A mod folder with `main` as main.lua (+ extra files); started on `h`.
static std::unique_ptr<script::Mod> make(FakeHost& h, const fs::path& root, const std::string& id, const std::string& main,
                                         const std::map<std::string, std::string>& extra = {}, script::Limits lim = {}) {
    fs::remove_all(root / id);
    put(root / id / "main.lua", main);
    for (const auto& [f, t] : extra) put(root / id / f, t);
    auto m = std::make_unique<script::Mod>(id, id + " mod", "1.0", root / id, h, lim);
    m->start("main.lua");
    return m;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    const fs::path tmp = fs::temp_directory_path() / "cleria-luatest";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp);
    put(tmp / "secret.txt", "outside the mod");
    FakeHost h;
    h.data = tmp / "modsdata";

    // ---- 1. the sandbox ----
    {
        auto m = make(h, tmp, "sandbox", R"LUA(
            local r = {}
            r.io = io == nil
            r.package = package == nil
            r.debug = debug == nil
            r.dofile = dofile == nil and loadfile == nil
            r.exec = os.execute == nil and os.getenv == nil and os.remove == nil and os.exit == nil
            r.clock = type(os.clock()) == "number" and type(os.time()) == "number" and type(os.date("%Y")) == "string"
            r.dump = string.dump == nil
            r.libs = type(string.format) == "function" and type(table.concat) == "function" and type(math.floor) == "function"
                     and type(utf8.char) == "function" and type(coroutine.wrap) == "function"
            r.bytecode = select(2, load("\27Lua\84\0garbage")) ~= nil
            r.textload = load("return 1 + 1")() == 2
            r.req_up = not pcall(require, "..secret")
            r.req_missing = not pcall(require, "nothere")
            r.read_up = cleria.read("../secret.txt") == nil
            r.read_abs = cleria.read("C:/Windows/win.ini") == nil
            r.read_own = cleria.read("lib/util.lua") ~= nil
            r.req_ok = require("lib.util").twice(21) == 42 and require("lib.util") == require("lib.util")
            r.gc_stop = not pcall(collectgarbage, "stop")
            r.data_up = not pcall(cleria.data.write, "../../escape.txt", "x")
            r.print = (print("printed via the mod log") == nil)
            for k, v in pairs(r) do if not v then error("sandbox check failed: " .. k) end end
            SANDBOX_OK = true
            cleria.log("sandbox ok")
        )LUA", {{"lib/util.lua", "return { twice = function(x) return 2 * x end }"}});
        check(m->ok(), "sandbox: no io/package/debug/dofile, os = clock/time/date only, no string.dump");
        check(m->ok() && has(h.logs, "sandbox ok"), "sandbox: bytecode refused, require / read / data stay in their folder");
        check(!fs::exists(tmp / "escape.txt") && !fs::exists(h.data / "escape.txt"), "sandbox: nothing written outside the data folder");
        check(has(h.logs, "printed via the mod log"), "sandbox: print goes to the mod's log");
    }

    // ---- 2. a runaway loop, a heap bomb, an error: only that mod stops ----
    {
        script::Limits lim;
        lim.instructions = 2'000'000;
        lim.main_instructions = 5'000'000;
        auto loop = make(h, tmp, "loop", "cleria.events.on('tick', function() while true do end end)", {}, lim);
        check(loop->ok(), "runaway: the mod starts");
        const auto t0 = std::chrono::steady_clock::now();
        loop->on_tick();
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        check(!loop->ok() && loop->error().find("ran too long") != std::string::npos, "runaway: an endless loop in a handler is stopped");
        check(secs < 5.0, "runaway: stopped quickly (" + std::to_string(secs) + " s)");
        check(has(h.fails_seen, "tick"), "runaway: the host is told (the Mods tab)");

        auto sneaky = make(h, tmp, "sneaky",
                           "cleria.events.on('tick', function() while true do pcall(function() while true do end end) end end)", {}, lim);
        sneaky->on_tick();
        check(!sneaky->ok(), "runaway: catching the limit with pcall does not save the loop");

        auto top = make(h, tmp, "toploop", "local n = 0 while true do n = n + 1 end", {}, lim);
        check(!top->ok(), "runaway: an endless main chunk is stopped at start");

        script::Limits small;
        small.memory = 8u << 20;
        auto bomb = make(h, tmp, "bomb", "cleria.events.on('tick', function() local t = {} for i = 1, 1e9 do t[i] = string.rep('x', 64) .. i end end)",
                         {}, small);
        bomb->on_tick();
        check(!bomb->ok() && bomb->error().find("memory") != std::string::npos, "heap: a heap bomb is refused (" + bomb->error() + ")");

        auto err = make(h, tmp, "boom", "local n = 0\ncleria.events.on('tick', function()\n  n = n + 1\n  if n == 2 then error('boom') end\nend)\n");
        err->on_tick();
        check(err->ok(), "error: a good call keeps the mod on");
        err->on_tick();
        check(!err->ok() && err->error() == "main.lua:4: boom", "error: file:line in the message (" + err->error() + ")");
        const std::size_t before = h.logs.size();
        err->on_tick();
        check(h.logs.size() == before, "error: a disabled mod gets no more calls");
        auto syn = make(h, tmp, "syntax", "this is not lua");
        check(!syn->ok() && syn->error().rfind("main.lua:1:", 0) == 0, "error: a syntax error disables the mod at start");
        auto bad_req = make(h, tmp, "badreq", "require('lib.broken')", {{"lib/broken.lua", "local x = nil\nreturn x.y"}});
        check(!bad_req->ok() && bad_req->error().rfind("lib/broken.lua:2:", 0) == 0, "error: a module's error names its file (" + bad_req->error() + ")");
        // another mod is unaffected
        auto fine = make(h, tmp, "fine", "N = 0 cleria.events.on('tick', function() N = N + 1 end)");
        fine->on_tick();
        check(fine->ok(), "error: other mods keep running");
    }

    // ---- 3. the API ----
    {
        h.fl[0x59] = -1;
        auto m = make(h, tmp, "api", R"LUA(
            local J = cleria.json
            seen = {}
            cleria.events.on("flag_changed", function(e) seen[#seen + 1] = e.kind .. " " .. e.index .. " " .. e.old .. "->" .. e.new .. " " .. e.room end)
            cleria.events.on("chest_opened", function(e) seen[#seen + 1] = "chest " .. e.script .. " " .. e.object .. " " .. e.index end)
            cleria.events.on("boss_defeated", function(e) cleria.log("boss " .. e.room .. " " .. tostring(e.flag) .. " api " .. cleria.api_version) end)
            cleria.events.on("item_used", function(e) cleria.log("used " .. e.item .. " in " .. e.room) end)
            cleria.events.on("door_opened", function(e) cleria.log("door " .. e.room .. " " .. e.flag .. " " .. e.script) end)
            cleria.events.on("blessing_bought", function(e)
                cleria.log("bought " .. e.index .. " for " .. e.price .. " suppressed " .. tostring(e.suppressed) .. " in " .. e.room)
            end)
            cleria.shop.add_filter(function(q)
                if q.kind == "price" and q.index == 3 then return {action = "replace", value = 777} end
                if q.kind == "row" and q.index == 3 then return {action = "replace", text = "Mystery - [SP:]" .. q.value .. " (" .. q.script .. "/" .. q.room .. ")"} end
                if q.kind == "owned" and q.index == 3 then return {action = "replace", value = 1} end
                if q.kind == "buy" and q.index == 3 then return "suppress" end
            end)
            local tid = cleria.events.on("tick", function() TICKS = (TICKS or 0) + 1 end)
            cleria.events.on("frame", function(dt) DT = dt end)
            cleria.command("probe", function(args)
                cleria.log("probe " .. args .. " flag " .. cleria.game.flag(0x59) .. " room " .. cleria.game.room() .. " scene " .. cleria.game.scene())
                cleria.game.set_flag(0x63, 1)
                cleria.player.add_sp(25)
                cleria.player.set_level(7)
                cleria.game.set_warp_unlocked(3, true)
                cleria.game.warp_to(3)
                cleria.log("save " .. tostring(cleria.game.save(8)) .. " " .. tostring(cleria.game.save(9)) .. " " ..
                           tostring(cleria.game.save(64)) .. " " .. tostring(cleria.game.save(-1)))
                local sp = cleria.save.set_profile
                cleria.log("profile " .. tostring(sp("AP_1")) .. " " .. cleria.save.profile() .. " " .. tostring(sp("no")) .. " " ..
                           cleria.save.profile() .. " " .. tostring(sp(nil)) .. " [" .. cleria.save.profile() .. "] " .. tostring(sp("x")) .. " " .. tostring(sp()))
                cleria.content.give(0x59, 2, "test")
                cleria.events.emit("item_obtained", {item = 0x59, count = 1, source = "test"})
                cleria.log("chests " .. cleria.game.chests().s_box01.flag .. " name " .. cleria.game.item_name(0x59))
                cleria.log("text " .. cleria.content.treasure_text("Acquired\\n<color:0xfff2ea8c>Celcetan Panacea<color:>.", "Bronze Key", 1))
                cleria.events.off(tid)
            end)
            cleria.command("store", function()
                cleria.save.set("state", {applied = 3, checks = J.array({5857605, 5857606}), name = 'q"uo\\te\n%'})
                cleria.save.set("n", 12)
                cleria.settings.set("Server", "127.0.0.1:1234")
                cleria.settings.set("DeathLink", true)
                cleria.data.write("cache/a.json", J.encode({x = 1}))
            end)
            cleria.command("load", function()
                local s = cleria.save.get("state")
                cleria.log("state " .. s.applied .. " " .. #s.checks .. " " .. s.checks[2] .. " " .. s.name)
                cleria.log("default " .. cleria.save.get("missing", "dflt") .. " n " .. cleria.save.get("n"))
                cleria.log("settings " .. cleria.settings.get("Server", "") .. " " .. tostring(cleria.settings.get("DeathLink", false)) ..
                           " " .. cleria.settings.get("Port", 38281))
                cleria.log("data " .. J.decode(cleria.data.read("cache/a.json")).x .. " " .. tostring(cleria.data.exists("nope")))
            end)
            cleria.command("json", function()
                local v = J.decode('{"a":[1,2.5,"x",null,true],"b":{},"c":[],"big":9007199254740993}')
                assert(v.a[4] == J.null and v.a[1] == 1 and math.type(v.a[1]) == "integer" and v.a[2] == 2.5)
                assert(J.is_array(v.c) and not J.is_array(v.b) and v.big == 9007199254740993)
                cleria.log("json " .. J.encode(v))
                cleria.log("json2 " .. J.encode({cmd = "Connect", tags = J.array(), slot_data = true, ids = {3, 4}}))
                local ok = pcall(J.encode, {f = function() end})
                local bad, msg = J.decode("{oops")
                cleria.log("json3 " .. tostring(ok) .. " " .. tostring(bad) .. " " .. msg)
            end)
            cleria.content.add_filter(function(q)
                if q.kind == "store" and q.index == 0x59 and q.value > math.max(q.old, 0) then return "suppress" end
                if q.kind == "window" then return {action = "replace", index = 0x2A1, text = "Sent X to Y. (" .. q.script .. "/" .. q.room .. ")"} end
                if q.kind == "icon" then return nil end
            end)
            cleria.ui.icon(0x2A1, "icon.dds")
            cleria.hud.layer("b.second", function(d) d:text(10, 20, "hello", {size = 14 * d.scale, color = "gold"}) end)
            cleria.hud.layer("a.first", function(d)
                d:rect(0, 0, d.w, 10, {color = 0x102030, alpha = 0.5})
                d:circle(5, 5, 3, {color = 0xFF0000})
                d:image("icon.dds", 0, 0, 16, 16)
            end)
            cleria.ui.page("Test Tab", function(p)
                p:section("Connection")
                DL = p:toggle("DeathLink", DL, "kills")
                SERVER = p:text_field("Server", SERVER or "host:1", {desc = "the room", secret = false})
                if p:button("Connect") then PRESSED = (PRESSED or 0) + 1 end
                p:info("Status", "not connected", {color = "gold"})
            end)
            cleria.game.exp_factor(function(level) return level < 10 and 3 or 1 end)
            cleria.speedrun.modification(function() return "Test mod run" end)
        )LUA", {{"icon.dds", "DDS "}});
        check(m->ok(), "api: the test mod starts");
        events::Event fe;
        fe.kind = events::Kind::FlagChanged;
        fe.index = 480; fe.old_value = 0; fe.new_value = 1; fe.room = "S_1009";
        m->on_event(fe);
        events::Event ce;
        ce.kind = events::Kind::ChestOpened;
        ce.index = 302; ce.script = "s_box01"; ce.object = "box_01"; ce.room = "S_1001";
        m->on_event(ce);
        events::Event be;
        be.kind = events::Kind::BossDefeated;
        be.index = 226; be.room = "S_7080";
        m->on_event(be);
        be.index = -1; be.room = "S_1080";
        m->on_event(be);
        events::Event ue;
        ue.kind = events::Kind::ItemUsed;
        ue.item = 0x59; ue.room = "S_1001";
        m->on_event(ue);
        events::Event de;
        de.kind = events::Kind::DoorOpened;
        de.index = 315; de.script = "usekey"; de.room = "S_1005";
        m->on_event(de);
        m->on_tick();
        m->on_frame(0.016);
        m->command("probe", "hello");
        m->on_tick();   // the tick handler was removed by events.off
        m->command("store", "");
        m->command("load", "");
        m->command("json", "");
        check(m->ok(), "api: every call ran (" + m->error() + ")");
        check(has(h.logs, "probe hello flag -1 room S_1001 scene 1001"), "api: game.flag / room / scene, commands with arguments");
        check(has(h.logs, "boss S_7080 226 api 7") && has(h.logs, "boss S_1080 nil api 7"),
              "api: boss_defeated carries the room and the boss flag (nil for a duel); api_version 6");
        check(has(h.logs, "used 89 in S_1001") && has(h.logs, "door S_1005 315 usekey"), "api: item_used (item), door_opened (flag, script)");
        check(h.fl[0x63] == 1 && h.sp == 75 && h.level == 7 && h.wp[3] == 1 && h.warped == 3,
              "api: set_flag, player.add_sp / set_level, warps");
        check(has(h.logs, "save true false false false") && h.saved == 8,
              "api: game.save returns the host's answer; a slot outside 0..63 is false without asking");
        check(has(h.logs, "profile true AP_1 false AP_1 true [] true true") && h.profile.empty(),
              "api: save.set_profile returns the host's answer (nil / nothing = \"\"), save.profile reads it back");
        check(h.given.size() == 1 && h.given[0] == std::make_pair(0x59, 2), "api: content.give goes to the host");
        check(h.emitted.size() == 1 && h.emitted[0].kind == events::Kind::ItemObtained && h.emitted[0].source == "test",
              "api: events.emit item_obtained");
        check(has(h.logs, "chests 302 name Celcetan Panacea"), "api: game.chests / item_name");
        check(has(h.logs, "text Acquired\\n<color:0xfff2ea8c>Bronze Key<color:>."), "api: content.treasure_text is content's own");
        check(has(h.logs, "state 3 2 5857606 q\"uo\\te\n%"), "api: save.set / save.get round trip (a table, any text)");
        check(has(h.logs, "default dflt n 12"), "api: save.get default, numbers");
        check(has(h.logs, "settings 127.0.0.1:1234 true 38281"), "api: settings typed by their default");
        check(h.settings["api/DeathLink"] == "1", "api: settings.set(true) stores 1");
        check(has(h.logs, "data 1 false") && fs::exists(h.data / "api" / "cache" / "a.json"), "api: data.write / read in the mod's data folder");
        check(has(h.logs, "json {\"a\":[1,2.5,\"x\",null,true],\"b\":{},\"big\":9007199254740993,\"c\":[]}"),
              "api: json decode / encode keeps arrays, null, int64 and empty arrays");
        check(has(h.logs, "json2 {\"cmd\":\"Connect\",\"ids\":[3,4],\"slot_data\":true,\"tags\":[]}"), "api: json.array() marks an empty array");
        check(has(h.logs, "json3 false nil line 1, column 2"), "api: json errors are values, encode errors raise");
        // the save storage in a real sidecar: the text survives the INI
        {
            Sidecar sc;
            sc.mod["api"] = h.store["api"];
            const auto back = Sidecar::parse(sc.serialize());
            check(back && back->mod.count("api") && back->mod.at("api") == h.store["api"] && !back->empty(),
                  "sidecar: [mod.<id>] storage round trip (quotes, backslash, newline, %)");
            Sidecar e;
            e.mod["x"];   // an empty map
            check(e.empty() && Sidecar::parse(e.serialize()).has_value(), "sidecar: an empty mod block keeps the sidecar empty");
        }
        // events
        std::string seen;
        {
            m->command("probe", "x");   // keeps running
        }
        // the grant filter
        GrantQuery q;
        q.kind = GrantQuery::Kind::Store;
        q.index = 0x59; q.old_value = -1; q.value = 1; q.script = "s_box01";
        check(m->filter(q, "S_1001").action == GrantDecision::Action::Suppress, "api: filter -> suppress (a store)");
        q.old_value = 2; q.value = 1;
        check(m->filter(q, "S_1001").action == GrantDecision::Action::Pass, "api: filter -> pass (nil)");
        GrantQuery w;
        w.kind = GrantQuery::Kind::Window;
        w.index = 0x59; w.text = "Acquired"; w.script = "s_box01";
        const GrantDecision d = m->filter(w, "S_1001");
        check(d.action == GrantDecision::Action::Replace && d.index == 0x2A1 && d.text == "Sent X to Y. (s_box01/S_1001)",
              "api: filter -> replace (the treasure box: icon + text)");
        check(m->icons().size() == 1 && m->icons()[0].id == 0x2A1, "api: ui.icon registers a mod icon id");
        {   // the blessing shop (API 5): cleria.shop.add_filter and blessing_bought
            ShopQuery s;
            s.kind = ShopQuery::Kind::Price; s.sub = 0x61; s.index = 3; s.value = 24000; s.script = "@grow03";
            const GrantDecision p = m->shop_filter(s, "S_1009");
            s.index = 4;
            const GrantDecision p4 = m->shop_filter(s, "S_1009");
            ShopQuery r;
            r.kind = ShopQuery::Kind::Row; r.sub = 0xdd; r.index = 3; r.value = 777; r.text = "Increase item drop rate - [SP:]777";
            r.script = "@growmenu";
            const GrantDecision rd = m->shop_filter(r, "S_1009");
            ShopQuery o;
            o.kind = ShopQuery::Kind::Owned; o.sub = 0x9a; o.index = 3; o.value = 0;
            const GrantDecision od = m->shop_filter(o, "S_1009");
            ShopQuery b;
            b.kind = ShopQuery::Kind::Buy; b.sub = 0xaf; b.index = 3; b.value = 777;
            const GrantDecision bd = m->shop_filter(b, "S_1009");
            b.index = 5;
            const GrantDecision b5 = m->shop_filter(b, "S_1009");
            check(m->has_shop_filter() && p.action == GrantDecision::Action::Replace && p.value == 777 &&
                  p4.action == GrantDecision::Action::Pass && rd.action == GrantDecision::Action::Replace &&
                  rd.text == "Mystery - [SP:]777 (@growmenu/S_1009)" && od.action == GrantDecision::Action::Replace &&
                  od.value == 1 && bd.action == GrantDecision::Action::Suppress && b5.action == GrantDecision::Action::Pass,
                  "api: shop.add_filter answers price / row / owned / buy (replace, suppress, pass)");
            events::Event se;
            se.kind = events::Kind::BlessingBought;
            se.index = 3; se.new_value = 777; se.old_value = 1; se.room = "S_1009";
            m->on_event(se);
            check(has(h.logs, "bought 3 for 777 suppressed true in S_1009"), "api: blessing_bought (index, price, suppressed)");
        }
        {   // API 6: cleria.combat.add_filter, the stats, the modifiers, add_exp, spawn_pickup, the two events
            using script::CombatQuery;
            using script::CombatDecision;
            auto c = make(h, tmp, "api6", R"LUA(
                local C = cleria.combat
                C.add_filter("exp", function(q)
                    cleria.log("exp " .. q.kind .. " " .. q.source .. " " .. q.value .. " cast " .. q.cast .. " scaled " .. q.scaled .. " in " .. q.room)
                    if q.cast == 17 then return math.max(q.value, 50) end       -- a number: that value
                    if q.cast == 18 then return 0 / 0 end                       -- not finite: ignored
                    if q.cast == 19 then return {value = 5} end                 -- no action: pass
                end)
                C.add_filter("sp", function(q) if q.source == "kill" then return "suppress" end return "pass" end)
                C.add_filter("damage", function(q) if q.dir == "taken" then return {action = "replace", value = q.value / 2} end end)
                C.add_filter("drop", function(q) return {action = "replace", item = 0x42, value = 7} end)
                C.add_filter("pickup", function(q) return {action = "replace", ticks = q.ticks * 2} end)
                C.add_filter("pickup", function(q) error("never reached") end)
                cleria.events.on("enemy_killed", function(e)
                    cleria.log("killed " .. e.cast .. " lv " .. e.level .. " exp " .. e.exp .. " sp " .. e.sp .. " at " .. e.x .. "," .. e.y .. "," .. e.z .. " in " .. e.room)
                end)
                cleria.events.on("pickup_collected", function(e)
                    cleria.log("collected " .. e.item .. " " .. e.what .. " " .. e.value .. " ticks " .. e.ticks .. " " .. tostring(e.suppressed))
                end)
                cleria.command("six", function()
                    local s = cleria.player.stats()
                    cleria.log("stats lv " .. s.level .. " atk " .. s.atk .. "/" .. s.base_atk .. " mp " .. s.mp .. "/" .. s.max_mp .. " buff " ..
                               s.atk_buff .. ":" .. s.atk_buff_ticks .. " pos " .. s.x .. "," .. s.y .. "," .. s.z)
                    cleria.player.set_modifier("atk", 10, 2)
                    cleria.player.set_modifier("max_hp", 50)
                    local a, mu = cleria.player.modifier("atk")
                    local d0, d1 = cleria.player.modifier("def")
                    cleria.log("mods " .. a .. " " .. mu .. " " .. d0 .. " " .. d1)
                    cleria.log("bad stat " .. tostring(pcall(cleria.player.set_modifier, "luck", 1)) .. " bad mul " ..
                               tostring(pcall(cleria.player.set_modifier, "atk", 0, -1)) .. " bad kind " ..
                               tostring(pcall(C.add_filter, "loot", print)))
                    cleria.player.add_exp(120.5)
                    cleria.player.add_exp(-5)
                    cleria.log("spawn " .. tostring(cleria.game.spawn_pickup(0x78, 1, 2, 3, {value = 500})) .. " " ..
                               tostring(cleria.game.spawn_pickup(0x42, 4, 5, 6)) .. " " .. tostring(pcall(cleria.game.spawn_pickup, 0x90, 0, 0, 0)))
                end)
            )LUA");
            CombatQuery e;
            e.kind = CombatQuery::Kind::Exp; e.value = 14.14; e.num = {{"cast", 17.0}, {"scaled", 0.147}}; e.str = {{"source", "kill"}};
            const CombatDecision e17 = c->combat_filter(e, "S_1002");
            e.num[0].second = 18.0;
            const CombatDecision e18 = c->combat_filter(e, "S_1002");
            e.num[0].second = 19.0;
            const CombatDecision e19 = c->combat_filter(e, "S_1002");
            check(c->has_combat_filter(CombatQuery::Kind::Exp) && e17.action == CombatDecision::Action::Replace && e17.value == 50 &&
                  e18.action == CombatDecision::Action::Pass && e19.action == CombatDecision::Action::Pass &&
                  has(h.logs, "exp exp kill 14.14 cast 17 scaled 0.147 in S_1002"),
                  "api 6: the exp filter (a number replaces; NaN and a table without action pass)");
            CombatQuery s;
            s.kind = CombatQuery::Kind::Sp; s.value = 4; s.str = {{"source", "kill"}};
            const CombatDecision sk = c->combat_filter(s, "S_1002");
            s.str[0].second = "pickup";
            const CombatDecision sp = c->combat_filter(s, "S_1002");
            CombatQuery d;
            d.kind = CombatQuery::Kind::Damage; d.value = 22; d.str = {{"dir", "taken"}};
            const CombatDecision dt = c->combat_filter(d, "S_1002");
            d.str[0].second = "dealt";
            const CombatDecision dd = c->combat_filter(d, "S_1002");
            check(sk.action == CombatDecision::Action::Suppress && sp.action == CombatDecision::Action::Pass &&
                  dt.action == CombatDecision::Action::Replace && dt.value == 11 && dd.action == CombatDecision::Action::Pass,
                  "api 6: the sp and damage filters (suppress, pass, replace)");
            CombatQuery dr;
            dr.kind = CombatQuery::Kind::Drop; dr.item = 0x78; dr.value = -1;
            const CombatDecision drd = c->combat_filter(dr, "S_1002");
            CombatQuery pk;
            pk.kind = CombatQuery::Kind::Pickup; pk.item = 0x48; pk.value = 0.5; pk.timer = 1800;
            const CombatDecision pkd = c->combat_filter(pk, "S_1002");
            check(drd.action == CombatDecision::Action::Replace && drd.item == 0x42 && drd.value == 7 &&
                  pkd.action == CombatDecision::Action::Replace && pkd.value == 0.5 && pkd.timer == 3600 && pkd.item == 0x48 && c->ok(),
                  "api 6: the drop and pickup filters (item, value, ticks; the first decision wins)");
            events::Event ke;
            ke.kind = events::Kind::EnemyKilled; ke.room = "S_1002";
            ke.cast = 17; ke.level = 3; ke.amount = 50; ke.amount2 = 4; ke.pos[0] = 1; ke.pos[1] = 2; ke.pos[2] = 3;
            c->on_event(ke);
            events::Event pe;
            pe.kind = events::Kind::PickupCollected; pe.item = 0x48; pe.source = "atk_buff"; pe.amount = 0.5; pe.count = 1800;
            c->on_event(pe);
            check(has(h.logs, "killed 17 lv 3 exp 50 sp 4 at 1.0,2.0,3.0 in S_1002") && has(h.logs, "collected 72 atk_buff 0.5 ticks 1800 false"),
                  "api 6: enemy_killed and pickup_collected");
            c->command("six", "");
            check(c->ok() && has(h.logs, "stats lv 7 atk 46.0/40.0 mp 1500.0/3000.0 buff 0.5:1800 pos 1.0,2.0,3.0") &&
                  has(h.logs, "mods 10.0 2.0 0.0 1.0") && h.mods6[1] == std::make_pair(10.0, 2.0) && h.mods6[0] == std::make_pair(50.0, 1.0),
                  "api 6: player.stats, set_modifier / modifier (" + c->error() + ")");
            check(has(h.logs, "bad stat false bad mul false bad kind false") && h.exp_added == 120.5 && h.spawned.size() == 2 &&
                  h.spawned[0] == "120 at 1,2,3 value 500" && h.spawned[1] == "66 at 4,5,6 value -1" && has(h.logs, "spawn true true false"),
                  "api 6: bad arguments are errors; add_exp; spawn_pickup");
            auto bad = make(h, tmp, "api6bad", "cleria.combat.add_filter('exp', function(q) error('boom') end)");
            CombatQuery be;
            be.kind = CombatQuery::Kind::Exp; be.value = 3;
            const CombatDecision bd6 = bad->combat_filter(be, "S_1002");
            check(bd6.action == CombatDecision::Action::Pass && !bad->ok(), "api 6: a filter that fails passes and stops its mod");
        }
        {   // API 7, the runtime batch (build/shell_re/API7.md A): timers, cleria.rng, the mods and the bus, named events
            h.clock = 100.0;
            auto a = make(h, tmp, "api7a", R"LUA(
                cleria.after(1, function() cleria.log("t one") end)
                local n, id = 0
                id = cleria.every(2, function() n = n + 1 cleria.log("t every " .. n) if n == 3 then cleria.cancel(id) end end)
                cleria.after(0.05, function() cleria.log("t play") end, {play = true})
                cleria.log("timer args " .. tostring(pcall(cleria.after, -1, print)) .. " " .. tostring(pcall(cleria.after, 1)) ..
                           " cancel " .. tostring(cleria.cancel(9999)))
                local r1, r2, s1, s2 = cleria.rng("abc"), cleria.rng("abc"), {}, {}
                for i = 1, 8 do s1[i] = r1:int(1000) s2[i] = r2:int(1000) end
                local lo, hi = 99, 0
                for i = 1, 200 do local v = r1:int(3, 5) lo = math.min(lo, v) hi = math.max(hi, v) end
                local f = r1:float()
                cleria.log("rng same " .. tostring(table.concat(s1, ",") == table.concat(s2, ",")) .. " other " ..
                           tostring(cleria.rng("abd"):int(1000000) ~= cleria.rng("abc"):int(1000000)) .. " range " .. lo .. ".." .. hi ..
                           " float " .. tostring(f >= 0 and f < 1) .. " shuffle " .. #cleria.rng(1):shuffle({1, 2, 3, 4}) ..
                           " pick " .. tostring(cleria.rng(1):pick({})) .. " empty " .. tostring(pcall(r1.int, r1, 5, 4)))
                local ids = {}
                for _, m in ipairs(cleria.mods.list()) do ids[#ids + 1] = m.id .. (m.active and "+" or "-") .. (m.scripted and "s" or "") end
                cleria.log("mods " .. table.concat(ids, " ") .. " active " .. tostring(cleria.mods.is_active("api7a")) .. " " ..
                           tostring(cleria.mods.is_active("off")) .. " locale " .. cleria.locale() .. " clip " .. tostring(cleria.clipboard("hi")) ..
                           " url " .. tostring(pcall(cleria.open_url, "https://example.org/")))
                cleria.bus.set("k", {a = 1})
                cleria.log("bus get " .. cleria.bus.get("k").a .. " " .. tostring(cleria.bus.get("none")) .. " " .. cleria.bus.get("none", 7))
                cleria.command("pub", function() cleria.log("published to " .. cleria.bus.publish("topic.x", {v = 5, list = {1, 2}})) end)
                cleria.events.on("level_up", function(e) cleria.log("ev " .. e.kind .. " " .. e.level .. " " .. e.room .. " " .. e.who) end)
                cleria.events.on("load", function(e) cleria.log("ev load retry " .. tostring(e.retry)) end)
                cleria.log("bad event " .. tostring(pcall(cleria.events.on, "nope", print)))
            )LUA");
            auto b = make(h, tmp, "api7b", R"LUA(
                cleria.bus.subscribe("topic.x", function(v, from) cleria.log("got " .. v.v .. " " .. #v.list .. " from " .. from .. " k " .. cleria.bus.get("k").a) end)
                cleria.bus.subscribe("topic.x", function(v) cleria.log("got twice") end)
            )LUA");
            h.peers = {a.get(), b.get()};
            check(a->ok() && b->ok() && has(h.logs, "timer args false false cancel false") &&
                  has(h.logs, "rng same true other true range 3..5 float true shuffle 4 pick nil empty false"), "api 7: timers' arguments, cleria.rng (" + a->error() + ")");
            check(has(h.logs, "mods api7a+s off- active true false locale fr clip true url false") && h.clip == "hi" && h.urls.empty() &&
                  has(h.logs, "bus get 1 nil 7"), "api 7: cleria.mods, locale, clipboard, open_url only from a page, bus.get / set");
            a->command("pub", "");
            check(has(h.logs, "got 5 2 from api7a k 1") && has(h.logs, "got twice") && has(h.logs, "published to 2"), "api 7: bus.publish reaches the other mods' handlers");
            a->on_frame(0.016);
            const bool none = !has(h.logs, "t one");
            h.clock = 101.1; a->on_frame(0.016);
            const bool one = has(h.logs, "t one") && !has(h.logs, "t every 1");
            h.clock = 102.1; a->on_frame(0.016);
            h.clock = 150.0; a->on_frame(0.016);                   // far behind: one call, no burst
            const bool two = has(h.logs, "t every 2") && !has(h.logs, "t every 3");
            h.clock = 153.0; a->on_frame(0.016);
            h.clock = 160.0; a->on_frame(0.016);                   // cancelled by its third run
            check(none && one && two && has(h.logs, "t every 3") && !has(h.logs, "t every 4") && a->timers_.size() == 1,
                  "api 7: after / every in real time, no burst, cancel from inside");
            a->on_tick(); a->on_tick();
            const bool not_yet = !has(h.logs, "t play");
            a->on_tick(); a->on_tick();
            check(not_yet && has(h.logs, "t play") && a->timers_.empty(), "api 7: a play-time timer counts ticks");
            events::Event lv = events::Event::named("level_up").with("level", 5).with("who", "you");
            lv.room = "S_1002";
            a->on_event(lv);
            events::Event ld;
            ld.kind = events::Kind::Load; ld.retry = true;
            a->on_event(ld);
            check(has(h.logs, "ev level_up 5 S_1002 you") && has(h.logs, "ev load retry true") && has(h.logs, "bad event false"),
                  "api 7: named events with their fields; load.retry");
        }
        {   // API 7, batch B: what the game is doing, the room's monsters, a world point on the screen
            h.fl[199] = 600; h.fl[144] = 1;
            auto g = make(h, tmp, "api7g", R"LUA(
                local z, zn = cleria.game.zone()
                local d, dn = cleria.game.difficulty()
                cleria.log("where " .. cleria.game.state() .. " [" .. cleria.game.room_title() .. "] floor " .. tostring(cleria.game.floor()) ..
                           " zone " .. z .. " " .. zn .. " diff " .. d .. " " .. dn .. " time " .. cleria.game.play_time())
                local list = cleria.world.actors()
                local a = list[1]
                local fx, fy = cleria.world.to_screen(a.x, a.y, a.z)
                cleria.log("actors " .. #list .. " " .. a.id .. " " .. a.name .. " lv " .. a.level .. " hp " .. a.hp .. "/" .. a.max_hp ..
                           " h " .. a.height .. " screen " .. string.format("%.1f", fx) .. " " .. fy .. " behind " .. tostring(cleria.world.to_screen(0, 0, -1)) ..
                           " one " .. cleria.world.actor(17).cast .. " none " .. tostring(cleria.world.actor(5)))
                cleria.events.on("state_changed", function(e) cleria.log("state " .. e.old .. " -> " .. e.state) end)
                cleria.events.on("enemy_hit", function(e) cleria.log("hit " .. e.actor .. " " .. e.damage .. " " .. e.killed) end)
            )LUA");
            g->on_event(events::Event::named("state_changed").with("state", "book").with("old", "play"));
            g->on_event(events::Event::named("enemy_hit").with("actor", 17).with("damage", 20).with("killed", 0));
            h.title7 = "Outer Corridor 1";
            auto g2 = make(h, tmp, "api7g2", "cleria.log('nofloor ' .. tostring(cleria.game.floor()))");
            check(g->ok() && has(h.logs, "where play [ 4F - Wailing Blue] floor 4 zone 1 Wailing Blue diff 3 Hard time 10.0") &&
                  has(h.logs, "actors 1 17 Selnarg lv 3 hp 54.0/74.0 h 1.5 screen 0.1 0.5 behind nil one 17 none nil") &&
                  has(h.logs, "state play -> book") && has(h.logs, "hit 17 20 0") && has(h.logs, "nofloor nil"),
                  "api 7: game.state / room_title / floor / zone / difficulty / play_time, world.actors / actor / to_screen (" + g->error() + ")");
        }
        {   // API 7, batch C: the writes go to the host by name with their numbers; arguments are checked first
            h.fl[163] = 0;
            auto w = make(h, tmp, "api7w", R"LUA(
                local P, G = cleria.player, cleria.game
                local r = {P.set_position(1, 2, 3), P.set_position(1, 2, 3, 90), P.set_mp(1500), P.set_boost(30000), P.set_exp(500),
                           P.set_invulnerable(60), P.equip("armor", 7), P.equip("accessory"), P.set_status("poison", true),
                           P.set_status("curse", false), G.goto_room("S_1003"), G.take_exit(1), G.set_speed(0.5), G.retry(), G.to_title(),
                           G.load(5), G.seed_random(42)}
                local t = {}
                for i, v in ipairs(r) do t[i] = v and "1" or "0" end
                local e, s, x = P.equipment(), P.status(), G.exits()
                cleria.log("writes " .. table.concat(t) .. " eq " .. e.weapon .. " " .. e.armor .. " " .. e.leggings .. " " .. e.accessory .. " " .. e.magic ..
                           " st " .. s.poison .. " " .. s.curse .. " exits " .. #x .. " " .. x[2].index .. " " .. x[2].room)
                cleria.log("ability " .. tostring(P.abilities().double_jump) .. " " .. tostring(P.set_ability("double_jump", true)) .. " " ..
                           tostring(P.abilities().double_jump) .. " " .. tostring(pcall(P.set_ability, "fly", true)))
                cleria.log("bad " .. tostring(pcall(P.set_position, 1, 2)) .. tostring(pcall(P.set_mp, -1)) .. tostring(pcall(P.equip, "hat", 1)) ..
                           tostring(pcall(P.set_status, "sleep", true)) .. tostring(pcall(G.goto_room, "../x")) .. tostring(pcall(G.set_speed, 9)))
            )LUA");
            const std::vector<std::string> want = {"api7w set_position 1 2 3", "api7w set_position 1 2 3 90", "api7w set_mp 1500", "api7w set_boost 30000",
                "api7w set_exp 500", "api7w invulnerable 60", "api7w equip 1 7", "api7w equip 3 -1", "api7w set_status 0 1", "api7w set_status 3 0",
                "api7w goto_room [S_1003]", "api7w take_exit 1", "api7w speed 0.5", "api7w retry", "api7w to_title", "api7w load 5", "api7w seed_random 42",
                "api7w equipment", "api7w status", "api7w exits"};
            check(w->ok() && h.ops7 == want && has(h.logs, "writes 11111111111110111 eq 0 6 30 -1 116 st 900 12 exits 2 1 S_1009") &&
                  has(h.logs, "ability false true true false") && h.fl[163] == 1 && has(h.logs, "bad falsefalsefalsefalsefalsefalse"),
                  "api 7: the player's and the flow's writes reach the host; bad arguments are errors (" + w->error() + ")");
        }
        {   // API 7, cleria.input: actions with a default key, the player's setting over it, held states, bind
            h.settings["api7i/Key.jump"] = "F8";
            auto in = make(h, tmp, "api7i", R"LUA(
                local I = cleria.input
                I.action("open", {label = "Open", key = "F5", pad = "back"}, function() cleria.log("ran open") end)
                I.action("jump", {key = "F6"}, function() cleria.log("ran jump") end)
                I.action("quiet", {key = "F9"})
                local k, p = I.binding("open")
                local k2 = I.binding("jump")
                cleria.log("bindings " .. k .. " " .. p .. " " .. k2 .. " none " .. tostring(I.binding("nope")) .. " down " .. tostring(I.down("open")) ..
                           " key " .. tostring(I.key("F5")) .. " bad " .. tostring(pcall(I.action, "x", {key = "NoSuchKey"})) ..
                           tostring(pcall(I.action, "bad id", {})) .. tostring(pcall(I.down, "nope")))
                cleria.command("rebind", function() cleria.log("bind " .. tostring(I.bind("open", "F7", "a")) .. tostring(I.bind("open", "NoSuchKey")) ..
                                                               " now " .. I.binding("open") .. " down " .. tostring(I.down("open"))) end)
            )LUA");
            check(in->ok() && in->actions_.size() == 3 && has(h.logs, "bindings F5 back F8 none nil down false key false bad falsefalsefalse"),
                  "api 7: input.action, binding, the player's key from the settings (" + in->error() + ")");
            in->run_action(0); in->run_action(1); in->run_action(2); in->run_action(9);
            h.held7 = "F7";
            in->command("rebind", "");
            check(has(h.logs, "ran open") && has(h.logs, "ran jump") && has(h.logs, "bind truefalse now F7 down true") &&
                  h.settings["api7i/Key.open"] == "F7" && h.settings["api7i/Pad.open"] == "a" && in->ok(),
                  "api 7: an action's function runs; input.bind is kept in the settings; input.down");
        }
        {   // API 7: a window of the mod's own (the page widgets + text / progress / close), and the dialogs
            h.widgets.clear();
            h.press = false; h.toggle_flip = false;
            auto u = make(h, tmp, "api7u", R"LUA(
                local U = cleria.ui
                U.window("tools", {title = "Tools", width = 0.5, pause = false}, function(p)
                    p:section("S")
                    p:text("a paragraph")
                    p:progress("HP", 0.5, "30 / 60")
                    if p:button("Close") then p:close() end
                end)
                cleria.log("open " .. tostring(U.is_open()) .. " " .. tostring(U.open("tools")) .. " " .. tostring(U.is_open()) .. " " ..
                           tostring(U.is_open("tools")) .. " bad " .. tostring(pcall(U.open, "nope")))
                cleria.command("ask", function(a)
                    if a == "msg" then U.message("Hello", function(i) cleria.log("msg done " .. tostring(i)) end, {ok = "Fine"})
                    else U.choose("Pick one\nof two", {"A", "B"}, function(i, o) cleria.log("picked " .. tostring(i) .. " " .. tostring(o)) end,
                                  {title = "T", cancel = a ~= "must"}) end
                    cleria.log("asked " .. tostring(U.is_open()))
                end)
            )LUA");
            const script::Mod::Window* w = u->window();
            const bool opened = w && w->id == "tools" && w->title == "Tools" && w->width == 0.5f && !w->pause && has(h.logs, "open nil true tools true bad false");
            int dummy = 0;
            u->draw_window(&dummy);
            const bool rows = h.widgets.size() == 4 && h.widgets[1] == "text a paragraph" && h.widgets[2] == "progress HP 0.50 30 / 60";
            h.press = true;                                         // the button: p:close()
            u->draw_window(&dummy);
            h.press = false;
            check(opened && rows && u->window() == nullptr, "api 7: ui.window / open / is_open, p:text / p:progress / p:close (" + u->error() + ")");
            u->command("ask", "x");
            const bool dlg = u->window() && u->window()->title == "T" && u->window()->pause && has(h.logs, "asked dialog");
            h.widgets.clear();
            u->draw_window(&dummy);                                 // the text and a button per option, nothing pressed
            const bool drawn = h.widgets.size() == 3 && h.widgets[0] == "text Pick one\nof two" && u->window() != nullptr;
            u->close_window(true);                                  // Back: the function gets nil
            const bool cancelled = u->window() == nullptr && has(h.logs, "picked nil nil");
            u->command("ask", "must");
            u->close_window(true);                                  // cancel = false: it stays
            const bool stays = u->window() != nullptr;
            h.press = true;                                         // every button "pressed": the last one wins
            u->draw_window(&dummy);
            h.press = false;
            u->command("ask", "msg");
            h.press = true;
            u->draw_window(&dummy);
            h.press = false;
            check(dlg && drawn && cancelled && stays && has(h.logs, "picked 2 B") && has(h.logs, "msg done 1") && u->window() == nullptr && u->ok(),
                  "api 7: ui.choose / ui.message: the pick, Back as nil, cancel = false");
        }
        {   // API 7: the world filters (free fields both ways; the first answer wins), world.spawn / remove / set_actor
            h.ops7.clear();
            auto f = make(h, tmp, "api7f", R"LUA(
                local W = cleria.world
                W.add_filter("spawn", function(q)
                    cleria.log("spawn q " .. q.kind .. " " .. q.cast .. " " .. q.name .. " lv " .. q.level .. " in " .. q.room)
                    if q.cast == 17 then return {cast = 16, level = 5, hp = 2} end
                    if q.cast == 43 then return "suppress" end
                    if q.cast == 50 then return {action = "pass", cast = 1} end
                end)
                W.add_filter("spawn", function(q) return {cast = 99} end)
                W.add_filter("exit", function(q) if q.to == "S_1003" then return {to = "S_1009"} end end)
                cleria.log("bad kind " .. tostring(pcall(W.add_filter, "loot", print)))
                cleria.log("ops " .. tostring(W.spawn(17, 1, 2, 3, {level = 9})) .. " " .. tostring(W.remove(17)) .. " " .. tostring(W.set_actor(17, {hp = 5})))
            )LUA");
            script::WorldQuery q;
            q.kind = "spawn"; q.num = {{"cast", 17.0}, {"level", 3.0}}; q.str = {{"name", "Selnarg"}};
            const script::WorldDecision d17 = f->world_filter(q, "S_1002");
            q.num[0].second = 43.0;
            const script::WorldDecision d43 = f->world_filter(q, "S_1002");
            q.num[0].second = 50.0;
            const script::WorldDecision d50 = f->world_filter(q, "S_1002");       // the first passes, the second answers
            script::WorldQuery x;
            x.kind = "exit"; x.str = {{"to", "S_1003"}, {"from", "S_1002"}};
            const script::WorldDecision dx = f->world_filter(x, "S_1002");
            x.str[0].second = "S_1009";
            const script::WorldDecision dp = f->world_filter(x, "S_1002");
            check(f->ok() && f->has_world_filter("spawn") && f->has_world_filter("exit") && !f->has_world_filter("loot") &&
                  d17.action == script::WorldDecision::Action::Replace && d17.n("cast", 0) == 16 && d17.n("level", 0) == 5 && d17.n("hp", 1) == 2 &&
                  d43.action == script::WorldDecision::Action::Suppress && d50.action == script::WorldDecision::Action::Replace && d50.n("cast", 0) == 99 &&
                  dx.action == script::WorldDecision::Action::Replace && dx.str.at("to") == "S_1009" && dp.action == script::WorldDecision::Action::Pass &&
                  has(h.logs, "spawn q spawn 17 Selnarg lv 3 in S_1002") && has(h.logs, "bad kind false"),
                  "api 7: world.add_filter (spawn, exit): replace with free fields, suppress, pass on to the next (" + f->error() + ")");
            const std::vector<std::string> want = {"api7f spawn_actor 17 1 2 3 9 0 0", "api7f remove_actor 17", "api7f set_actor 17 5 nan nan nan nan"};
            check(h.ops7 == want && has(h.logs, "ops nil true true"), "api 7: world.spawn / remove / set_actor reach the host");
        }
        {   // API 7: cleria.audio and cleria.screen are named writes too
            h.ops7.clear();
            auto v = make(h, tmp, "api7v", R"LUA(
                local A, S = cleria.audio, cleria.screen
                cleria.log("av " .. tostring(A.se(5)) .. tostring(A.se(5, 0.25)) .. tostring(A.music(18)) .. tostring(A.stop_music(0.5)) ..
                           tostring(S.fade(0x102030, 0, 1, 0.5)) .. tostring(S.flash(0xFF0000)) .. " bad " .. tostring(pcall(S.fade, 0, 2, 0)))
            )LUA");
            auto ends = [](const std::string& x, const char* tail) { const std::string t = tail; return x.size() >= t.size() && x.compare(x.size() - t.size(), t.size(), t) == 0; };
            check(v->ok() && h.ops7.size() == 6 && h.ops7[0] == "api7v se 5 1" && h.ops7[1] == "api7v se 5 0.25" && h.ops7[2] == "api7v bgm 18" &&
                  h.ops7[3] == "api7v bgm_stop 500" && ends(h.ops7[4], " 0 1 30") && ends(h.ops7[5], " 0.6 0 15") &&
                  has(h.logs, "av truetruetruetruetruetrue bad false"), "api 7: audio.se / music / stop_music, screen.fade / flash (" + v->error() + ")");
        }
        {   // API 7: the puppet and the player's pose are named writes / reads
            h.ops7.clear();
            auto p = make(h, tmp, "api7p", R"LUA(
                local W = cleria.world
                cleria.log("puppet " .. tostring(W.puppet(2, 1, 2, 3, 90)) .. " " .. tostring(W.set_puppet(7, {x = 4, yaw = 180, tag = 1, frame = 2, sheet = 15})) .. " " ..
                           tostring(W.remove_puppet(7)) .. " pose " .. tostring(cleria.player.pose()) .. " bad " .. tostring(pcall(W.puppet, 4, 0, 0, 0)))
            )LUA");
            check(p->ok() && h.ops7.size() == 4 && h.ops7[0] == "api7p puppet_spawn 2 1 2 3 90" && h.ops7[1] == "api7p puppet_set 7 4 nan nan 180 1 2 15 nan" &&
                  h.ops7[2] == "api7p puppet_remove 7" && h.ops7[3] == "api7p pose" && has(h.logs, "puppet nil true true pose nil bad false"),
                  "api 7: world.puppet / set_puppet / remove_puppet, player.pose (" + p->error() + ")");
        }
        {   // API 7: cleria.script.run is a named write, script.add_filter a world filter of the kind "script"
            h.ops7.clear();
            auto s = make(h, tmp, "api7s", R"LUA(
                cleria.script.add_filter(function(q) if q.name == "no" then return "suppress" end end)
                cleria.log("script " .. tostring(cleria.script.run("@Save")))
            )LUA");
            script::WorldQuery q;
            q.kind = "script";
            q.str = {{"name", "no"}};
            const bool refused = s->world_filter(q, "S_1002").action == script::WorldDecision::Action::Suppress;
            q.str = {{"name", "yes"}};
            const bool passed = s->world_filter(q, "S_1002").action == script::WorldDecision::Action::Pass;
            check(s->ok() && refused && passed && h.ops7.size() == 1 && h.ops7[0] == "api7s script_run [@Save]" && has(h.logs, "script true"),
                  "api 7: script.run, script.add_filter (" + s->error() + ")");
        }
        {   // API 7: combat.damage / heal / hurt and player.tune are named writes
            h.ops7.clear();
            auto c = make(h, tmp, "api7c", R"LUA(
                local C = cleria.combat
                cleria.log("combat " .. tostring(C.damage(7, 12)) .. tostring(C.damage(7, 5, {knock = 2, class = 1})) .. tostring(C.heal(3)) ..
                           tostring(C.hurt(4)) .. tostring(cleria.player.tune({speed = 1.5, air_jumps = 2})) .. " bad " .. tostring(pcall(C.damage, 7, -1)))
            )LUA");
            const std::vector<std::string> want = {"api7c hit 7 12 0 3", "api7c hit 7 5 2 1", "api7c heal 3", "api7c hurt 4", "api7c tune 0 1.5",
                                                   "api7c tune 5 2"};
            check(c->ok() && h.ops7 == want && has(h.logs, "combat truetruetruetruetrue bad false"),
                  "api 7: combat.damage / heal / hurt, player.tune (" + c->error() + ")");
        }
        {   // API 7: cleria.net.udp / tcp / http on the loopback: a datagram, a listener and its client, and an HTTP
            // request answered by the mod's own TCP listener
            auto n = make(h, tmp, "api7n", R"LUA(
                local N = cleria.net
                local a, b = N.udp(), N.udp()
                local port = a:bind(0, false)
                b:send("127.0.0.1", port, "ping")
                local srv, cli, web = N.tcp(), N.tcp(), N.tcp()
                srv:listen(0, false)
                cli:connect("127.0.0.1", srv:port())
                web:listen(0, false)
                local got_udp, got_tcp, got_http, opened, hc, answered, done
                local req = ""
                N.http({url = "http://127.0.0.1:" .. web:port() .. "/hi?x=1", method = "post", body = "abc", headers = {["X-T"] = "1"}}, function(r)
                    got_http = r.status .. " " .. r.body .. " " .. tostring(r.ok)
                end)
                cleria.events.on("frame", function()
                    for _, d in ipairs(a:poll()) do got_udp = d.data .. " from " .. d.host end
                    for _, e in ipairs(srv:poll()) do if e.kind == "accept" then e.socket:send("hello") srv_conn = e.socket end end
                    for _, e in ipairs(cli:poll()) do
                        if e.kind == "open" then opened = true end
                        if e.kind == "data" then got_tcp = e.text end
                    end
                    for _, e in ipairs(web:poll()) do if e.kind == "accept" then hc = e.socket end end
                    if hc then
                        for _, e in ipairs(hc:poll()) do if e.kind == "data" then req = req .. e.text end end
                        if not answered and req:find("abc", 1, true) then
                            answered = true
                            hc:send("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nConnection: close\r\n\r\nworld")
                        end
                    end
                    if got_udp and got_tcp and got_http and opened and not done then
                        done = true
                        cleria.log("net " .. got_udp .. " | " .. got_tcp .. " | " .. got_http .. " | " ..
                                   tostring(req:find("POST /hi?x=1 HTTP", 1, true) == 1) .. tostring(req:find("X-T: 1", 1, true) ~= nil) ..
                                   " " .. cli:state() .. " " .. srv:state())
                    end
                end)
                cleria.log("bad " .. tostring(pcall(N.http, {url = "file:///x"}, print)) .. tostring(pcall(N.http, {url = "http://x", method = "BREW"}, print)))
            )LUA");
            for (int i = 0; i < 400 && n->ok() && !has(h.logs, "net ping"); ++i) {
                n->on_frame(0.016);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            check(n->ok() && has(h.logs, "net ping from 127.0.0.1 | hello | 200 world true | truetrue open listening") && has(h.logs, "bad falsefalse"),
                  "api 7: net.udp / tcp / http on the loopback (" + n->error() + ")");
        }
        {   // API 7: cleria.camera, hud.show, game.pause and world.boss are named writes / reads
            h.ops7.clear();
            auto c = make(h, tmp, "api7k", R"LUA(
                local C = cleria.camera
                cleria.log("cam " .. tostring(C.get()) .. tostring(C.set({distance = 1.5, y = 2})) .. tostring(C.shake(0.2, 0.5)) ..
                           tostring(C.look_at(1, 2, 3, 1)) .. tostring(C.look_at()) .. tostring(C.reset()) .. tostring(cleria.hud.show("boss", false)) ..
                           tostring(cleria.game.pause(true)) .. tostring(cleria.world.boss()) .. " bad " .. tostring(pcall(cleria.hud.show, "nose", true)))
            )LUA");
            const std::vector<std::string> want = {"api7k cam_get", "api7k cam_set 1.5 nan nan 2 nan", "api7k cam_shake 0.2 0.2 0.2 30",
                                                   "api7k cam_look 1 2 3 60", "api7k cam_look nan nan nan 30", "api7k cam_reset",
                                                   "api7k hud_show 2 0", "api7k pause 1", "api7k boss"};
            check(c->ok() && h.ops7 == want && has(h.logs, "cam niltruetruetruetruetruetruetruenil bad false"),
                  "api 7: camera.get / set / shake / look_at / reset, hud.show, game.pause, world.boss (" + c->error() + ")");
        }
        {   // API 7: monster variants: define, spawn by name (the base cast, then the form), the callbacks and the phases
            h.ops7.clear();
            auto m = make(h, tmp, "api7m", R"LUA(
                local C = cleria.content
                C.define_monster("brute", {base = 43, name = "Brute", level = 20, hp = 3, atk = 2, scale = 1.5, tint = 0xFF4040,
                    on_spawn = function(id) cleria.log("born " .. id) end,
                    on_hit = function(e) cleria.log("ouch " .. e.damage) end,
                    on_death = function(e) cleria.log("dead " .. e.cast .. " " .. tostring(e.variant)) end})
                C.define_boss("king", {base = 43, ai = false,
                    phases = {{at = 0.25, enter = function(id, i) cleria.log("phase " .. i .. " of " .. id) end},
                              {at = 0.75, enter = function(id, i) cleria.log("phase " .. i .. " of " .. id) end}}})
                cleria.log("spawn " .. tostring(cleria.world.spawn("brute", 1, 2, 3)) .. " morph " .. tostring(C.morph(9, "king")) .. " form " ..
                           tostring(cleria.world.form(9, {hp = 2, boss = false, name = "X"})) .. " bad " .. tostring(pcall(cleria.world.spawn, "nobody", 0, 0, 0)) ..
                           tostring(pcall(C.define_monster, "x", {})))
            )LUA");
            const std::vector<std::string> want = {"api7m spawn_actor 43 1 2 3 20 0 0", "api7m actor_form 0 3 2 1 1 1 1.5 1.67281e+07 1 0 [api7m:brute\tBrute]",
                                                   "api7m actor_form 9 1 1 1 1 1 -1 -1 0 1 [api7m:king\t]",
                                                   "api7m actor_form 9 2 nan nan nan nan -1 -1 nan 0 [\tX]"};
            auto hit = [&](int actor, double hp, int dmg = 7) {
                events::Event e = events::Event::named("enemy_hit");
                e.with("actor", actor).with("damage", dmg).with("hp", hp).with("max_hp", 100);
                m->on_event(e);
            };
            hit(0, 90);                                             // the brute (the fake host's spawn answers id 0): on_hit
            hit(9, 80); hit(9, 20);                                 // the king: nothing at 80 %, both phases at 20 %
            events::Event k;
            k.kind = events::Kind::EnemyKilled;
            k.cast = 43;
            k.with("actor", 0).with("variant", std::string("api7m:brute"));
            m->on_event(k);
            hit(0, 50, 8);                                          // gone: no callback
            check(m->ok() && h.ops7 == want && has(h.logs, "spawn 0 morph true form true bad falsefalse") && has(h.logs, "born 0") &&
                  has(h.logs, "ouch 7") && !has(h.logs, "ouch 8") && has(h.logs, "phase 1 of 9") && has(h.logs, "phase 2 of 9") && has(h.logs, "dead 43 api7m:brute"),
                  "api 7: content.define_monster / define_boss / morph, world.form, callbacks and phases (" + m->error() + ")");
        }
        {   // API 7: the multiplayer pieces: a hittable puppet, combat.strike, world.snapshot / apply
            h.ops7.clear();
            auto s = make(h, tmp, "api7y", R"LUA(
                local W = cleria.world
                local snap = W.snapshot()
                cleria.log("sync " .. #snap .. " " .. snap[1].id .. " " .. snap[1].cast .. " " .. snap[1].hp .. " " .. snap[1].tag .. " " ..
                           tostring(W.set_puppet(7, {hittable = true})) .. tostring(cleria.combat.strike(40)) .. " " ..
                           tostring(W.apply(snap)) .. tostring(W.apply({{id = 5, cast = 43, hp = 9}}, {ai = true, remove = false})))
            )LUA");
            const std::vector<std::string> want = {"api7y world_snapshot", "api7y puppet_set 7 nan nan nan nan nan nan nan 1", "api7y strike 40",
                                                   "api7y world_apply 3 21 43 5 1 2 3 90 50 80 301 4 15",
                                                   "api7y world_apply 0 5 43 0 0 0 0 0 9 0 -1 -1 -1"};
            check(s->ok() && h.ops7 == want && has(h.logs, "sync 1 21 43 50.0 301 truetrue 11"),
                  "api 7: world.set_puppet{hittable}, combat.strike, world.snapshot / apply (" + s->error() + ")");
        }
        {   // API 7: the Lua layer (rooms, skills, characters) reaches the host through the same writes; the layer's
            // extras draw; the skill filter refuses a cast and runs the mod's own
            h.ops7.clear();
            auto o = make(h, tmp, "api7o", R"LUA(
                local C = cleria.content
                C.define_room("arena", {base = "S_1002", monsters = {{43, 1, 2, 3, level = 9}}, exits = {[0] = false, default = "S_1009"}})
                cleria.log("enter " .. tostring(C.enter_room("arena")) .. " bad " .. tostring(pcall(C.enter_room, "nowhere")))
                C.define_skill(2, {mp = 50, cooldown = 5, cast = function(c) cleria.log("cast slot " .. c.slot .. " cost " .. c.cost) end})
                C.define_character("giant", {hp = 2, speed = 1.5, air_jumps = 1, scale = 1.5, glow = 0x80FF80})
                cleria.log("char " .. tostring(C.set_character("giant")) .. " " .. tostring(C.character()) .. " look " .. tostring(cleria.player.set_look({scale = 1})))
                cleria.hud.layer("w", function(d)
                    if not shown then shown = true
                        cleria.log("draw " .. tostring(d:world_text(5, 0, 1, "hi")) .. tostring(d:world_text(5, 0, -1, "behind")) ..
                                   tostring(d:world_line(0, 0, 1, 5, 0, 1)) .. tostring(d:world_circle(5, 0, 5, 2)))
                    end
                end)
                cleria.ui.window("t", {title = "T"}, function(p) p:table({{"a", "b"}, {1, 2}}) p:list({"x", "y"}) cleria.log("page ok") end)
                cleria.ui.open("t")
            )LUA");
            o->draw_hud(1000, 500, 1);
            int dummy = 0;
            o->draw_window(&dummy);
            script::WorldQuery q;
            q.kind = "skill";
            q.num = {{"slot", 2}, {"item", 117}, {"level", 1}, {"cost", 400}, {"mp", 300}, {"air", 0}};
            const bool cast = o->world_filter(q, "S_1002").action == script::WorldDecision::Action::Suppress;
            const bool cooling = o->world_filter(q, "S_1002").action == script::WorldDecision::Action::Suppress;   // refused: 5 s
            q.num[0].second = 3;
            const bool other = o->world_filter(q, "S_1002").action == script::WorldDecision::Action::Pass;         // slot 3 is the game's
            script::WorldQuery x;
            x.kind = "exit";
            x.num = {{"index", 0}};
            x.str = {{"to", "S_1003"}, {"door", "d0"}};
            const bool free_exit = o->world_filter(x, "S_1009").action == script::WorldDecision::Action::Pass;     // not in the room yet
            events::Event re;
            re.kind = events::Kind::RoomEnter;
            re.room = "S_1002";
            o->on_event(re);                                        // arrived: the variant is the room now
            const bool closed = o->world_filter(x, "S_1002").action == script::WorldDecision::Action::Suppress;
            x.num[0].second = 1;
            const script::WorldDecision away = o->world_filter(x, "S_1002");
            script::WorldQuery sp;
            sp.kind = "spawn";
            sp.str = {{"by", ""}};
            const bool own = o->world_filter(sp, "S_1002").action == script::WorldDecision::Action::Suppress;      // the room's own monsters
            sp.str = {{"by", "api7o"}};
            const bool ours = o->world_filter(sp, "S_1002").action == script::WorldDecision::Action::Pass;
            auto opn = [&](const std::string& s) { return std::count(h.ops7.begin(), h.ops7.end(), s); };
            check(o->ok() && has(h.logs, "enter true bad false") && has(h.logs, "char true giant look true") && has(h.logs, "draw truefalsetruetrue") &&
                  has(h.logs, "page ok") && has(h.logs, "cast slot 2 cost 50") && cast && cooling && other && free_exit && closed &&
                  away.action == script::WorldDecision::Action::Replace && away.str.count("to") && away.str.at("to") == "S_1009" && own && ours &&
                  opn("api7o goto_room [S_1002]") == 1 && opn("api7o set_mp 250") == 1 && opn("api7o tune 0 1.5") == 1 && opn("api7o tune 5 1") == 1 &&
                  opn("api7o player_look 1.5 8.45402e+06") == 1 && opn("api7o player_look 1 nan") == 1,
                  "api 7: define_room / define_skill / define_character, set_look, d:world_*, p:table / p:list (" + o->error() + ")");
            h.draws.clear();                                        // this block's layer drew: the hud test below counts its own
        }
        // hud / pages
        m->draw_hud(1280, 800, 0.8f);
        check(h.draws.size() == 4 && h.draws[0].rfind("rect 0,0-1280,10 80302010 fill", 0) == 0 && h.draws[1].rfind("circle 5,5 r3 FF0000FF", 0) == 0 &&
                  h.draws[2] == "image 7" && h.draws[3].rfind("text 10,20 hello 11 f0 FF3EB0D6", 0) == 0,
              "api: hud layers in name order; colours, alpha, kit names, images");
        h.toggle_flip = true;
        h.press = true;
        m->draw_page(0, &h);
        m->draw_page(0, &h);
        check(m->pages().size() == 1 && m->pages()[0] == "Test Tab", "api: ui.page registers a tab");
        check(has(h.widgets, "section Connection") && has(h.widgets, "toggle DeathLink") && has(h.widgets, "field Server = host:1") &&
                  has(h.widgets, "field Server = typed") && has(h.widgets, "info Status = not connected"),
              "api: page widgets (section, toggle, text_field, button, info)");
        m->command("probe", "y");
        check(m->exp_factor(5) == 3.0f && m->exp_factor(20) == 1.0f, "api: game.exp_factor");
        check(m->modifications() == std::vector<std::string>{"Test mod run"}, "api: speedrun.modification");
        check(m->has_command("probe") && !m->has_command("nope"), "api: commands are listed");
        check(m->ok(), "api: still on after everything (" + m->error() + ")");
        // outside a hud layer, draw calls refuse
        auto stray = make(h, tmp, "stray", "local keep\ncleria.hud.layer('x', function(d) keep = d end)\ncleria.events.on('tick', function() keep:text(1, 1, 'x') end)");
        stray->draw_hud(100, 100, 1);
        stray->on_tick();
        check(!stray->ok() && stray->error().find("only work inside a hud layer") != std::string::npos, "api: draw calls outside a layer are an error");
    }

    // ---- 4. websockets ----
    {
        auto m = make(h, tmp, "net", R"LUA(
            local socks = {}
            for i = 1, 4 do socks[i] = cleria.net.websocket() end
            local fifth = pcall(cleria.net.websocket)
            local ok, why = socks[1]:open("not a :: valid address ::")
            cleria.log("net " .. tostring(fifth) .. " " .. tostring(ok) .. " " .. socks[2]:state())
            assert(socks[2]:open("127.0.0.1:1"))
            S = socks[2]
            cleria.events.on("tick", function()
                for _, e in ipairs(S:poll()) do cleria.log("ws " .. e.kind .. " " .. tostring(e.error)) end
            end)
        )LUA");
        check(m->ok() && has(h.logs, "net false false idle"), "net: 4 sockets per mod, a bad address refused, state idle");
        bool closed = false;
        for (int i = 0; i < 400 && !closed; ++i) {
            m->on_tick();
            closed = has(h.logs, "ws close true");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        check(closed, "net: nothing listening -> a close event with error (non-blocking poll)");
    }

    // ---- 5. the manifest and the samples ----
    {
        std::vector<std::string> errs;
        const mods::Manifest mf = mods::parse_manifest("[mod]\r\nId=a.b\r\nApiVersion=3\r\nScript=main.lua\r\n", errs);
        check(errs.empty() && mf.script == "main.lua" && mf.api == 3 && mods::kApiVersion == 7, "manifest: Script= and ApiVersion 3");
        for (const char* s : {"hello", "archipelago_lua"}) {
            const fs::path root = fs::path("samples") / "mods" / s;
            std::vector<std::string> e2;
            const mods::Manifest sm = mods::parse_manifest(slurp(root / "mod.ini"), e2);
            FakeHost fh;
            fh.data = tmp / "modsdata";
            fh.game = false;
            script::Mod mod(sm.id, sm.name, sm.version, root, fh);
            mod.start(sm.script);
            check(e2.empty() && mod.ok(), std::string("samples: ") + s + " starts (" + mod.error() + ")");
            if (std::string(s) == "archipelago_lua") {
                // its table is its own (tools/ap_import.py writes it there): the apworld's whole id space
                const auto t = cleria::json::parse(slurp(root / "data" / "ys_origin.json"));
                const cleria::json::Value* tv = t ? &*t : nullptr;
                check(tv && (*tv)["locations"].size() == 336 && (*tv)["items"].size() == 147 && (*tv)["game"].as_string() == "Ys Origin",
                      "samples: archipelago_lua's data/ys_origin.json: 336 locations, 147 items");
                check(mod.pages().size() == 1 && mod.pages()[0] == "Archipelago" && mod.has_filter() && mod.icons().size() == 1,
                      "samples: archipelago_lua has its page (Mods > Archipelago), filter and icon");
                check(fs::exists(root / "README.md") && fs::exists(root / "icons" / "AP_ITEM.DDS"),
                      "samples: archipelago_lua is self-contained (README.md, its icon)");
            }
        }
    }

    fs::remove_all(tmp, ec);
    std::printf("[lua] %s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
