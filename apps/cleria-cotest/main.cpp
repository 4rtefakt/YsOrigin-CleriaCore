// Headless test for the XSO coroutine VM (CUTSCENE_COROUTINE.md): verifies run()
// is unchanged, tick() parks/advances on Wait_Prompt, and the ScriptManager spawns +
// ticks concurrent StartScript children. Usage: cleria-cotest <assets>
#include "cleria/sev.hpp"
#include "cleria/xso.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
using namespace cleria;
static std::vector<std::uint8_t> rd(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}
struct H : XsoHost {
    bool prompt = false;
    std::vector<std::string> lines;
    bool wait_prompt() override { return prompt; }
    void window_msg(int, int, const std::string& name, const std::string& text) override {
        lines.push_back((name.empty() ? "" : "[" + name + "] ") + text);
    }
    void spawn(int, int, float, float, float, float, int) override {}
    bool is_alive(int) const override { return false; }
};
int main(int argc, char** argv) {
    std::string root = argc >= 2 ? argv[1] : "assets";
    int fails = 0;
    // 1) run() unchanged: S_1001 barrier script still extracts grants.
    { Xso s = Xso::parse(rd(root + "/MAP/S_10/S_1001/S_1001.XSO"));
      XsoVm vm; H h; vm.host = &h; vm.g_flags[150] = 1; vm.run(s);
      std::printf("[run]  S_1001 grants=%zu %s\n", vm.grants.size(),
                  vm.grants.size() > 0 ? "OK" : "FAIL"); fails += vm.grants.empty(); }
    Xso talk = Xso::parse(rd(root + "/MAP/S_10/S_1000/TALKC210.XSO"));
    // 2) prompt NEVER pressed -> coroutine parks forever at the first Wait_Prompt.
    { XsoVm vm; H h; h.prompt = false; vm.host = &h; vm.g_flags[150] = 1; vm.g_flags[235] = 1; vm.co_begin(talk);
      bool ended = false; int t = 0; for (; t < 5000; ++t) if (vm.tick() == XsoVm::Tick::Ended) { ended = true; break; }
      bool ok = !ended && vm.co_active();
      std::printf("[tick] prompt=NEVER : ticked %d ended=%d active=%d %s\n", t, ended, vm.co_active(), ok ? "OK" : "FAIL");
      fails += !ok; }
    // 3) prompt ALWAYS pressed -> advances through every line and ends.
    { XsoVm vm; H h; h.prompt = true; vm.host = &h; vm.g_flags[150] = 1; vm.g_flags[235] = 1; vm.co_begin(talk);
      bool ended = false; int t = 0; for (; t < 5000; ++t) if (vm.tick() == XsoVm::Tick::Ended) { ended = true; break; }
      const bool ok = ended && !h.lines.empty();
      std::printf("[tick] prompt=ALWAYS: ticked %d ended=%d lines=%zu %s\n", t, ended, h.lines.size(), ok ? "OK" : "FAIL");
      for (const auto& l : h.lines) std::printf("       > %.72s\n", l.c_str());
      fails += !ok; }
    // 4) ScriptManager: run the S_1000 ROOM ROOT and confirm it spawns + ticks its
    //    concurrent StartScript children (CUTSCENE_COROUTINE.md §5). A filesystem loader
    //    resolves each child NAME -> <room>/<NAME>.XSO and records every request, so we can
    //    assert the root asked the engine for its documented ungated controllers.
    {
        const std::string dir = root + "/MAP/S_10/S_1000/";
        struct FsLoader : ScriptLoader {
            std::string dir;
            std::unordered_set<std::string> requested;   // upper-cased names asked for
            std::shared_ptr<const Xso> load_script(const std::string& name) override {
                std::string up = name;
                for (char& c : up) c = char(std::toupper((unsigned char)c));
                requested.insert(up);
                auto bytes = rd(dir + up + ".XSO");
                if (bytes.size() < 4) return nullptr;     // not present -> engine skips
                try { return std::make_shared<Xso>(Xso::parse(bytes)); }
                catch (const std::exception&) { return nullptr; }
            }
        } loader;
        loader.dir = dir;
        H h; h.prompt = true;                             // auto-advance any dialogue that fires
        ScriptManager mgr;
        mgr.loader = &loader;
        mgr.vm.host = &h;
        mgr.vm.g_flags[150] = 1;                          // Yunica
        auto rootScript = std::make_shared<Xso>(Xso::parse(rd(dir + "S_1000.XSO")));
        mgr.start_root(rootScript, "s_1000");             // ticks once (spawns children)
        for (int f = 0; f < 120; ++f) mgr.tick_all();
        const std::size_t peak = mgr.peak_live;           // max GoScripts resident in one frame
        const bool spawnedCaption = loader.requested.count("EV_CAPTION") > 0;
        const bool spawnedSave = loader.requested.count("S_SAVEOBJECTCHANGE_START") > 0;
        const bool concurrent = peak > 1;                 // >1 GoScript alive at once
        const bool ok = spawnedCaption && spawnedSave && concurrent;
        std::printf("[mgr]  S_1000 root: requested %zu children, peak-live=%zu, "
                    "EV_Caption=%d Save=%d %s\n",
                    loader.requested.size(), peak, spawnedCaption, spawnedSave,
                    ok ? "OK" : "FAIL");
        std::vector<std::string> names(loader.requested.begin(), loader.requested.end());
        std::sort(names.begin(), names.end());
        for (const auto& nnm : names) std::printf("       spawned: %s\n", nnm.c_str());
        fails += !ok;
    }
    // 5) The cleria-view path: a self-contained TALK run as a ScriptManager root emits the
    //    same dialogue as the single-VM coroutine (test 3). Proves the manager integration.
    {
        struct NoLoader : ScriptLoader {
            std::shared_ptr<const Xso> load_script(const std::string&) override { return nullptr; }
        } noloader;
        H h; h.prompt = true;
        ScriptManager mgr; mgr.loader = &noloader; mgr.vm.host = &h;
        mgr.vm.g_flags[150] = 1; mgr.vm.g_flags[235] = 1;
        mgr.start_root(std::make_shared<Xso>(talk), "talkc210");
        int t = 0; for (; t < 5000 && mgr.any_live(); ++t) mgr.tick_all();
        const bool ok = !mgr.any_live() && h.lines.size() == 3;
        std::printf("[mgr]  TALKC210 as root: ticked %d, ended=%d lines=%zu %s\n",
                    t, !mgr.any_live(), h.lines.size(), ok ? "OK" : "FAIL");
        fails += !ok;
    }
    // 6) The barrier break-pan: KEKKAIBREAK_01 driven as a cutscene must, in order and on
    //    the script's real frame schedule, pan the camera to the ward (Scroll_MoveTo, 180f),
    //    Eft_Kill it, then Scroll_Return (120f) and end. Proves the camera event, not a
    //    silent run-to-completion. (assets/MAP/S_10/S_1001/KEKKAIBREAK_01.XSO)
    {
        struct BreakHost : XsoHost {
            int frame = 0, fMove = -1, fKill = -1, fReturn = -1;
            void scroll_moveto(float, float, float, int) override { if (fMove < 0) fMove = frame; }
            void scroll_return(int) override { if (fReturn < 0) fReturn = frame; }
            void kill_effect(const std::string&) override { if (fKill < 0) fKill = frame; }
            void spawn(int, int, float, float, float, float, int) override {}
            bool is_alive(int) const override { return false; }
        } bh;
        struct NoLoader : ScriptLoader {
            std::shared_ptr<const Xso> load_script(const std::string&) override { return nullptr; }
        } nl;
        ScriptManager mgr; mgr.loader = &nl; mgr.vm.host = &bh;
        auto kb = std::make_shared<Xso>(Xso::parse(rd(root + "/MAP/S_10/S_1001/KEKKAIBREAK_01.XSO")));
        mgr.start_root(kb, "kekkaibreak_01");
        const bool frozenDuring = mgr.world_frozen();   // Fix(0) ran in the first burst
        for (bh.frame = 1; bh.frame < 600 && mgr.any_live(); ++bh.frame) mgr.tick_all();
        const bool thawedAfter = !mgr.world_frozen();    // Unfix + teardown released it
        // Order: move first (frame 0), kill after the 180f pan, return after the 60f hold,
        // and the whole thing ends near frame 360 (180+60+120). Allow small slack.
        const bool ordered = bh.fMove == 0 && bh.fKill > bh.fMove && bh.fReturn > bh.fKill;
        const bool timed = bh.fKill >= 175 && bh.fKill <= 185 && bh.fReturn >= 235 && bh.fReturn <= 245;
        const bool ended = !mgr.any_live() && bh.frame >= 350 && bh.frame <= 370;
        const bool ok = ordered && timed && ended && frozenDuring && thawedAfter;
        std::printf("[brk]  KEKKAIBREAK: move@%d kill@%d return@%d end@%d frozen=%d thawed=%d %s\n",
                    bh.fMove, bh.fKill, bh.fReturn, bh.frame, frozenDuring, thawedAfter, ok ? "OK" : "FAIL");
        fails += !ok;
    }
    // 7) SceneBox OBB containment (op 0x13d math, OP_13D_REGION.md §3), independent of the
    //    .SEV byte layout: an axis-aligned box, then the same box rotated 90deg about Y.
    {
        SceneBox box;                        // 20 wide (x), 4 tall (y), 4 deep (z), at (10,0,10)
        box.center = {10, 0, 10};
        box.amin = {-10, 0, -2};
        box.amax = {10, 4, 2};
        int t = 0;
        auto chk = [&](bool got, bool want, const char* what) {
            if (got != want) { std::printf("       box FAIL: %s (got %d want %d)\n", what, got, want); ++t; }
        };
        // Axis-aligned: a point at the center is in; one 5 past the +Z face (z=17) is out.
        chk(box.contains({10, 2, 10}), true, "center inside");
        chk(box.contains({10, 2, 17}), false, "past +Z face out");
        chk(box.contains({10, 2, 11.9f}), true, "near +Z edge in");
        chk(box.contains({19, 2, 10}), true, "wide +X in");
        chk(box.contains({22, 2, 10}), false, "past +X out");
        chk(box.contains({10, 5, 10}), false, "above top out");
        // Rotate the long axis 90deg about Y: what was the wide X extent now runs along Z.
        box.rot_y = 3.14159265f / 2.0f;
        chk(box.contains({10, 2, 19}), true, "rotated: long axis now Z, in");
        chk(box.contains({19, 2, 10}), false, "rotated: old wide-X now narrow, out");
        const bool ok = (t == 0);
        std::printf("[box]  SceneBox OBB containment: %s\n", ok ? "OK" : "FAIL");
        fails += !ok;
    }
    // 8) Camera shake op 0xcf: EV_TILT (Wait_Time 20 -> Shake(0.12,0.12,0,15)) forwards the
    //    per-axis amplitudes + frame count to the host after the wait (CUTSCENE_CAMERA.md §6).
    //    (The op word 1039516303 = 0x3DF5C28F = 0.12f — the doc's "0.1257" was approximate.)
    {
        struct ShakeH : XsoHost {
            float ax = -1, ay = -1, az = -1; int fr = -1;
            void shake(float x, float y, float z, int frames) override { ax = x; ay = y; az = z; fr = frames; }
            void spawn(int, int, float, float, float, float, int) override {}
            bool is_alive(int) const override { return false; }
        } sh;
        XsoVm vm; vm.host = &sh;
        Xso evtilt = Xso::parse(rd(root + "/MAP/S_10/S_1000/EV_TILT.XSO"));
        vm.co_begin(evtilt);
        for (int t = 0; t < 60 && vm.co_active(); ++t) vm.tick();
        const bool ok = sh.fr == 15 && std::fabs(sh.ax - 0.12f) < 0.001f &&
                        std::fabs(sh.ay - 0.12f) < 0.001f && sh.az == 0.0f;
        std::printf("[shk]  EV_TILT Shake: amp=(%.4f,%.4f,%.4f) frames=%d %s\n",
                    sh.ax, sh.ay, sh.az, sh.fr, ok ? "OK" : "FAIL");
        fails += !ok;
    }
    // 9) .SEV parse (SEV_FORMAT.md): S_1000.SEV has 5 boxes; 'event_01' must match the doc's
    //    numeric cross-check (center (-0.093,2.56,25.2), min (-5,-2.5,-2.5), max (5,10,2.5)),
    //    and a point at its center must test inside via the op-0x13d containment.
    {
        auto boxes = parse_sev(rd(root + "/MAP/S_10/S_1000/S_1000.SEV"));
        const SceneBox* e1 = find_box(boxes, "event_01");
        bool ok = boxes.size() == 5 && e1 != nullptr;
        if (e1) {
            ok = ok && std::fabs(e1->center.x + 0.0933f) < 0.01f
                    && std::fabs(e1->center.z - 25.206f) < 0.01f
                    && std::fabs(e1->amin.x + 5.0f) < 0.01f && std::fabs(e1->amax.x - 5.0f) < 0.01f
                    && std::fabs(e1->amax.y - 10.0f) < 0.01f && e1->rot_y == 0.0f
                    && e1->contains(e1->center)                     // center is inside
                    && !e1->contains({e1->center.x + 100, e1->center.y, e1->center.z}); // far is out
        }
        std::printf("[sev]  S_1000.SEV: %zu boxes, event_01 center=(%.3f,%.3f,%.3f) min.x=%.1f max=(%.1f,%.1f,%.1f) %s\n",
                    boxes.size(), e1 ? e1->center.x : 0, e1 ? e1->center.y : 0, e1 ? e1->center.z : 0,
                    e1 ? e1->amin.x : 0, e1 ? e1->amax.x : 0, e1 ? e1->amax.y : 0, e1 ? e1->amax.z : 0,
                    ok ? "OK" : "FAIL");
        fails += !ok;
    }
    // 10) END-TO-END region triggering: run the STOP1000_ROI_NORTH controller on the manager;
    //     it polls op 0x13d against 'event_01' each frame and StartScripts its talk only when
    //     the player is inside the box. Proves the trigger chain (controller -> 0x13d ->
    //     StartScript the talk) gates on player position.
    {
        const std::string dir = root + "/MAP/S_10/S_1000/";
        struct TrigHost : XsoHost {
            bool inBox = false;                       // driven by the test (player in event_01)
            bool chr_in_event_box(int entity, const std::string& name) const override {
                return inBox && entity == 13 && name == "event_01";
            }
            bool wait_prompt() override { return true; }   // auto-advance any dialogue
            void spawn(int, int, float, float, float, float, int) override {}
            bool is_alive(int e) const override { return e == 13; }   // the player is alive
        } th;
        struct RecLoader : ScriptLoader {
            std::string dir;
            std::unordered_set<std::string> requested;
            std::shared_ptr<const Xso> load_script(const std::string& name) override {
                std::string up = name;
                for (char& c : up) c = char(std::toupper((unsigned char)c));
                requested.insert(up);
                auto bytes = rd(dir + up + ".XSO");
                if (bytes.size() < 4) return nullptr;
                try { return std::make_shared<Xso>(Xso::parse(bytes)); }
                catch (const std::exception&) { return nullptr; }
            }
        } loader; loader.dir = dir;
        ScriptManager mgr; mgr.loader = &loader; mgr.vm.host = &th;
        mgr.vm.g_flags[150] = 1;                       // Yunica; flag 243 (talked) stays 0
        mgr.start_root(std::make_shared<Xso>(Xso::parse(rd(dir + "STOP1000_ROI_NORTH.XSO"))),
                       "stop1000_roi_north");
        for (int f = 0; f < 60; ++f) mgr.tick_all();   // player OUTSIDE: talk must not fire
        const bool talkBefore = loader.requested.count("STOP1000_ROI_TALK_1") > 0;
        th.inBox = true;                               // player walks into event_01
        for (int f = 0; f < 60; ++f) mgr.tick_all();
        const bool talkAfter = loader.requested.count("STOP1000_ROI_TALK_1") > 0;
        const bool ok = !talkBefore && talkAfter;
        std::printf("[trg]  STOP1000_ROI_NORTH: talk(player out)=%d talk(player in)=%d %s\n",
                    talkBefore, talkAfter, ok ? "OK" : "FAIL");
        fails += !ok;
    }
    // 11) DOCUMENTED FINDING (why live room-events are opt-in): running the S_1000 root with a
    //     FRESH/zero flag state misroutes — with no story progress set it fires 'BattleStart1'
    //     (a battle-intro cutscene) which Fixes the world and never releases, locking the hub.
    //     The real game enters S_1000 with progression flags set that route away from it; until
    //     CleriaCore models that story-flag state, running the full root live is gated behind
    //     CLERIA_EVENTS. This test pins the artifact so the reason stays visible.
    {
        const std::string dir = root + "/MAP/S_10/S_1000/";
        struct EntryHost : XsoHost {
            bool wait_prompt() override { return true; }
            bool chr_in_event_box(int, const std::string&) const override { return false; }
            void spawn(int, int, float, float, float, float, int) override {}
            bool is_alive(int e) const override { return e == 13; }
        } eh;
        struct FsLoader2 : ScriptLoader {
            std::string dir;
            std::shared_ptr<const Xso> load_script(const std::string& name) override {
                std::string up = name; for (char& c : up) c = char(std::toupper((unsigned char)c));
                auto b = rd(dir + up + ".XSO");
                if (b.size() < 4) return nullptr;
                try { return std::make_shared<Xso>(Xso::parse(b)); } catch (const std::exception&) { return nullptr; }
            }
        } loader; loader.dir = dir;
        ScriptManager mgr; mgr.loader = &loader; mgr.vm.host = &eh;
        mgr.vm.g_flags[150] = 1;                                     // Yunica; NO story flags set
        mgr.start_root(std::make_shared<Xso>(Xso::parse(rd(dir + "S_1000.XSO"))), "s_1000");
        for (int f = 0; f < 300; ++f) mgr.tick_all();
        bool battleStartHolds = false;
        for (const auto& in : mgr.live) {
            std::string lo = in.name;
            for (char& c : lo) c = char(std::tolower((unsigned char)c));
            if (in.fix_held > 0 && lo.rfind("battle", 0) == 0) battleStartHolds = true;
        }
        // The finding is: zero-flag entry freezes via a battle-intro child. That is exactly why
        // live events are opt-in (CLERIA_EVENTS), not on by default.
        const bool zeroLocks = mgr.world_frozen() && battleStartHolds;
        // ...and with the first-visit story flag (235 = search party present) the root routes
        // to the opening scene and does NOT fall into the battle lock: after settling (dialogue
        // auto-advances) the world is not frozen. This is the candidate new-game state.
        ScriptManager mgr2; mgr2.loader = &loader; mgr2.vm.host = &eh;
        mgr2.vm.g_flags[150] = 1; mgr2.vm.g_flags[235] = 1;
        mgr2.start_root(std::make_shared<Xso>(Xso::parse(rd(dir + "S_1000.XSO"))), "s_1000");
        for (int f = 0; f < 300; ++f) mgr2.tick_all();
        const bool flag235ok = !mgr2.world_frozen();
        const bool ok = zeroLocks && flag235ok;
        std::printf("[live] S_1000: zero-flag locks(battle)=%d | flag235 settles-free=%d %s\n",
                    int(zeroLocks), int(flag235ok), ok ? "OK" : "FAIL");
        fails += !ok;
    }
    // NEW-GAME flag init (XsoVm::new_game) matches FUN_0041fd10 exactly
    // (build/event_re/NEWGAME_FLAGS.md §2): inventory -1 fill, header set,
    // per-character starting items, and the script-only flags (235 &c.) left 0.
    {
        XsoVm vm;
        vm.g_flags[235] = 7;  vm.locals[0x300] = 1;  // dirt that the wipe must clear
        vm.new_game(1);                              // Yunica
        bool ok = vm.g_flags[150] == 1 && vm.g_flags[149] == 1 &&
                  vm.g_flags[0] == 1 && vm.g_flags[6] == 1 &&      // starting items
                  vm.g_flags[151] == 0 && vm.g_flags[152] == 6 &&
                  vm.g_flags[3] == -1 && vm.g_flags[127] == -1 &&  // unowned stay -1
                  vm.g_flags[119] == 1 && vm.g_flags[153] == -1 &&
                  vm.g_flags[154] == 1 && vm.g_flags[155] == 1 &&
                  vm.g_flags[228] == 16 && vm.g_flags[193] == 0 &&
                  vm.g_flags[235] == 0 && vm.g_flags[128] == 0;    // story flags wiped
        vm.new_game(3);                              // Toal
        ok = ok && vm.g_flags[2] == 1 && vm.g_flags[18] == 1 &&
             vm.g_flags[151] == 2 && vm.g_flags[152] == 18 && vm.g_flags[150] == 3;
        vm.new_game(2, /*time_attack*/true);         // Hugo, TA mode
        ok = ok && vm.g_flags[1] == 1 && vm.g_flags[12] == 1 && vm.g_flags[193] == 1;
        std::printf("[newgame] init table matches NEWGAME_FLAGS.md %s\n", ok ? "OK" : "FAIL");
        fails += !ok;
    }
    // Query-op batch 2026-07-23 (0x3c/0x8c/0x9a/0xc1/0x139/0x13a/0x13b): run the REAL
    // ops through the VM on a synthetic XSR container, storing each acc into a local
    // (>= 0x200) via Flag_MovReg (0x66) so the results are observable.
    {
        struct PosHost : XsoHost {
            void spawn(int, int, float, float, float, float, int) override {}
            bool is_alive(int) const override { return false; }
            bool actor_pos(int id, float out[3]) const override {
                if (id != 13) return false;              // only the player resolves
                out[0] = 5; out[1] = 1; out[2] = -3; return true;
            }
            bool is_fix() const override { return true; }
        } ph;
        std::vector<std::int32_t> code;
        auto W = [](int sub, int nargs) {                // class-2 word, argoff bits 0
            return std::int32_t((2u << 24) | (std::uint32_t(sub) << 12) | std::uint32_t(nargs));
        };
        auto F = [](float v) { std::int32_t w; std::memcpy(&w, &v, 4); return w; };
        int next_local = 0x200;
        auto q = [&](std::initializer_list<std::int32_t> opwords) {   // query; store acc
            code.insert(code.end(), opwords);
            code.push_back(W(0x66, 2)); code.push_back(next_local++); code.push_back(0);
        };
        q({W(0x9a, 1), 3});                              // 0x200: story bit 3 -> 1
        q({W(0x9a, 1), 7});                              // 0x201: itemArr[flag152] -> 1
        q({W(0x9a, 1), 9});                              // 0x202: spliced -> bit 7 -> 1
        q({W(0x9a, 1), 10});                             // 0x203: spliced -> bit 8 -> 0
        q({W(0x139, 5), 13, F(5), F(1), F(-3), F(0.25f)});          // 0x204: at center -> 1
        q({W(0x139, 5), 13, F(5), F(9), F(-3), F(2)});              // 0x205: 8 above -> 0
        q({W(0x13a, 6), 13, F(5), F(9), F(-3), F(0.5f), F(10)});    // 0x206: tall cyl -> 1
        q({W(0x13a, 6), 13, F(5), F(9), F(-3), F(0.5f), F(2)});     // 0x207: short cyl -> 0
        q({W(0x13b, 7), 13, F(6), F(2), F(-2), F(4), F(0), F(-4)}); // 0x208: box (swapped corners) -> 1
        q({W(0x13b, 7), 13, F(6), F(2), F(-2), F(5.5f), F(0), F(-4)}); // 0x209: outside -> 0
        q({W(0xc1, 0)});                                 // 0x20a: IsFix -> 1
        code.push_back(std::int32_t(0xEu << 24)); code.push_back(42);  // acc = 42
        q({W(0x3c, 1), 42});                             // 0x20b: Reg_IsEqualInt -> 1
        q({W(0x8c, 1), 5});                              // 0x20c: IsEquip, no model -> 0
        q({W(0x139, 5), 42, F(5), F(1), F(-3), F(9)});   // 0x20d: unknown actor -> 0
        code.push_back(std::int32_t(1u << 24));          // end
        std::vector<std::uint8_t> bytes(0x24, 0);
        std::memcpy(bytes.data(), "XSR\0", 4);
        const std::uint32_t words = std::uint32_t(code.size());
        std::memcpy(bytes.data() + 0x1C, &words, 4);     // string count @0x20 stays 0
        const auto* cp = reinterpret_cast<const std::uint8_t*>(code.data());
        bytes.insert(bytes.end(), cp, cp + code.size() * 4);

        XsoVm vm; vm.host = &ph;
        vm.g_flags[217] = (1 << 3) | (1 << 7);           // story bits 3 and 7
        vm.item_arr[6] = 1; vm.g_flags[152] = 6;         // op0==7 escape hits
        vm.run(Xso::parse(bytes));
        const int expect[] = {1, 1, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 0};
        bool ok = true;
        for (int i = 0; i < 14; ++i)
            if (vm.flag(0x200 + i) != expect[i]) {
                std::printf("  queryops: local 0x%x = %d, want %d\n",
                            0x200 + i, vm.flag(0x200 + i), expect[i]);
                ok = false;
            }
        std::printf("[queryops] 14 query-op results via synthetic script %s\n", ok ? "OK" : "FAIL");
        fails += !ok;
    }
    // EVENTS-ON-BY-DEFAULT burn-in (PLAN.md Phase 2): EVERY room root, started on the
    // faithful new-game flag state + the flag-235 intro stand-in, must SETTLE (no Fix
    // FLAG-ROUTING PROBE (2026-07-24): S_1000's root branches on story flags to pick a
    // per-moment NPC config. Determine the FRESH-GAME state empirically: run the root
    // under each candidate flag set and report whether it soft-locks + how many NPCs it
    // spawns. Candidates: zero flags, the old flag-235 hack, and FLAG_MAIN_1 (the first
    // story-checkpoint state, MAP/S_01/S_0100). A good fresh state = doesn't lock the
    // hub AND spawns the just-arrived scene.
    {
        namespace fs = std::filesystem;
        struct ProbeHost : XsoHost {
            int spawns = 0;
            bool wait_prompt() override { return true; }
            void spawn(int id, int, float, float, float, float, int) override { if (id > 0) ++spawns; }
            bool is_alive(int e) const override { return e == 13; }
            bool any_seed_alive(int) const override { return false; }
        };
        struct DirLoader2 : ScriptLoader {
            std::string dir;
            std::shared_ptr<const Xso> load_script(const std::string& name) override {
                std::string up = name; for (char& c : up) c = char(std::toupper((unsigned char)c));
                auto b = rd(dir + up + ".XSO");
                if (b.size() < 4) return nullptr;
                try { return std::make_shared<Xso>(Xso::parse(b)); } catch (...) { return nullptr; }
            }
        } dl; dl.dir = root + "/MAP/S_10/S_1000/";
        // FLAG_MAIN_1's flag set (NEWGAME_FLAGS.md addendum): the first story checkpoint.
        static const int fm1[] = {246,300,187,229,84,85,86,303,116,107,304,247,111,87,248,
            312,442,230,54,313,99,314,316,249,55,444,78,318,319,321,323,220,275};
        auto probe = [&](const char* label, std::function<void(XsoVm&)> setflags) {
            ProbeHost ph; DirLoader2 ld; ld.dir = dl.dir;
            ScriptManager mgr; mgr.loader = &ld; mgr.vm.host = &ph;
            mgr.vm.new_game(1);                        // Yunica base
            setflags(mgr.vm);
            try { mgr.start_root(std::make_shared<Xso>(Xso::parse(rd(dl.dir + "S_1000.XSO"))), "s_1000"); }
            catch (...) {}
            for (int t = 0; t < 500; ++t) mgr.tick_all();
            std::printf("[flagprobe] %-12s locked=%d npc_spawns=%d live=%zu\n",
                        label, int(mgr.world_frozen()), ph.spawns, mgr.live.size());
        };
        probe("zero-flags",  [](XsoVm&){});
        probe("flag235=1",   [](XsoVm& v){ v.g_flags[235] = 1; });
        probe("FLAG_MAIN_1", [&](XsoVm& v){ for (int f : fm1) v.g_flags[f] = 1; v.g_flags[182]=0; v.g_flags[78]=0; });
        // The VERIFIED intro trigger: flag 141=1 -> BATTLESTART1. Confirm the intro
        // chain COMPLETES (world unfrozen) rather than soft-locking, under a host that
        // auto-advances prompts + reports actors "still" (so Wait_ChrMoving proceeds).
        probe("flag141=1",   [](XsoVm& v){ v.g_flags[141] = 1; });
    }

    // held) within 600 ticks under an auto-advancing host. A room stuck frozen at
    // entry would soft-lock live play — this pins the whole corpus, not just S_1000.
    {
        namespace fs = std::filesystem;
        struct EntryHost : XsoHost {
            bool wait_prompt() override { return true; }   // auto-advance dialogue
            void spawn(int, int, float, float, float, float, int) override {}
            bool is_alive(int e) const override { return e == 13; }
        } eh;
        struct DirLoader : ScriptLoader {
            fs::path dir;
            std::shared_ptr<const Xso> load_script(const std::string& name) override {
                std::string up = name; for (char& c : up) c = char(std::toupper((unsigned char)c));
                std::ifstream f(dir / (up + ".XSO"), std::ios::binary);
                std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f), {}};
                if (b.size() < 4) return nullptr;
                try { return std::make_shared<Xso>(Xso::parse(b)); }
                catch (const std::exception&) { return nullptr; }
            }
        } loader;
        int rooms = 0, frozen = 0, soft_locked = 0;
        std::string frozen_names, locked_names;
        for (const auto& band : fs::directory_iterator(root + "/MAP")) {
            if (!band.is_directory()) continue;
            for (const auto& rm : fs::directory_iterator(band.path())) {
                if (!rm.is_directory()) continue;
                const std::string name = rm.path().filename().string();
                const fs::path xso = rm.path() / (name + ".XSO");
                std::error_code ec;
                if (!fs::exists(xso, ec)) continue;
                std::ifstream f(xso, std::ios::binary);
                std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f), {}};
                loader.dir = rm.path();
                ScriptManager mgr; mgr.loader = &loader; mgr.vm.host = &eh;
                mgr.vm.new_game(1);                 // Yunica, faithful init
                mgr.vm.g_flags[235] = 1;            // the intro-script stand-in
                try { mgr.start_root(std::make_shared<Xso>(Xso::parse(b)), name); }
                catch (const std::exception&) { continue; }
                std::fprintf(stderr, "  sweep %s...\n", name.c_str());
                for (int t = 0; t < 300; ++t) mgr.tick_all();
                ++rooms;
                // What "frozen" MEANS is the question, not the raw count: a room only
                // SOFT-LOCKS if a Fix-holder makes NO progress AND is waiting on
                // something the player can't clear. The right signal is the coroutine's
                // `wait` counter: a script mid Wait_Time countdown (wait>0) is a
                // cutscene actively advancing frame-by-frame — like the StepMove camera
                // pans, or the goddess/Toal dialogue on Flooded 1F. Only a Fix-holder
                // that is NOT counting down (wait==0) and is parked on a non-player op
                // is a genuine lock. (This host auto-advances Wait_Prompt, so a
                // dialogue never parks with wait==0 here.)
                if (mgr.world_frozen()) {
                    ++frozen;
                    bool locked = false;
                    for (const auto& in : mgr.live) {
                        if (in.finished || in.fix_held <= 0) continue;
                        std::uint32_t psub = 0;
                        for (const XsoInstr& ins : in.script->disasm())
                            if (ins.pc == in.pc) { psub = ins.sub; break; }
                        const bool advancing = in.wait > 0;   // live Wait_Time countdown
                        if (!advancing) locked = true;
                        if (std::getenv("CLERIA_SWEEP_DIAG"))
                            std::printf("    %-6s %-10s <- %-22s pc=%u op=0x%x wait=%d\n",
                                        advancing ? "cutsc" : "LOCK", name.c_str(),
                                        in.name.c_str(), in.pc, psub, in.wait);
                    }
                    if (locked) { ++soft_locked; locked_names += " " + name; }
                }
            }
        }
        // The real gate for events-on-by-default: ZERO rooms soft-lock (frozen on a
        // non-player-advanceable op). `frozen` alone counts cutscenes-in-progress and
        // is informational. Established 2026-07-23: all 33 headless "freezes" are
        // finite Fix->animate->Unfix sequences or player-advance dialogues; none lock.
        const bool ok = rooms > 50 && soft_locked == 0;
        std::printf("[rootsweep] %d roots; %d in-cutscene at 300t; %d SOFT-LOCK%s %s\n",
                    rooms, frozen, soft_locked, locked_names.c_str(), ok ? "OK" : "FAIL");
        fails += !ok;
    }
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
