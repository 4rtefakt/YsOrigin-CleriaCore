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
    int warped = -1;
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
            local tid = cleria.events.on("tick", function() TICKS = (TICKS or 0) + 1 end)
            cleria.events.on("frame", function(dt) DT = dt end)
            cleria.command("probe", function(args)
                cleria.log("probe " .. args .. " flag " .. cleria.game.flag(0x59) .. " room " .. cleria.game.room() .. " scene " .. cleria.game.scene())
                cleria.game.set_flag(0x63, 1)
                cleria.player.add_sp(25)
                cleria.player.set_level(7)
                cleria.game.set_warp_unlocked(3, true)
                cleria.game.warp_to(3)
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
        m->on_tick();
        m->on_frame(0.016);
        m->command("probe", "hello");
        m->on_tick();   // the tick handler was removed by events.off
        m->command("store", "");
        m->command("load", "");
        m->command("json", "");
        check(m->ok(), "api: every call ran (" + m->error() + ")");
        check(has(h.logs, "probe hello flag -1 room S_1001 scene 1001"), "api: game.flag / room / scene, commands with arguments");
        check(h.fl[0x63] == 1 && h.sp == 75 && h.level == 7 && h.wp[3] == 1 && h.warped == 3,
              "api: set_flag, player.add_sp / set_level, warps");
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
        check(errs.empty() && mf.script == "main.lua" && mf.api == 3 && mods::kApiVersion == 3, "manifest: Script= and ApiVersion 3");
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
                check(slurp(root / "data" / "ys_origin.json") == slurp(fs::path("res") / "ap" / "ys_origin.json"),
                      "samples: archipelago_lua's data/ys_origin.json is res/ap/ys_origin.json");
                check(mod.pages().size() == 1 && mod.has_filter() && mod.icons().size() == 1, "samples: archipelago_lua has its tab, filter and icon");
            }
        }
    }

    fs::remove_all(tmp, ec);
    std::printf("[lua] %s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
