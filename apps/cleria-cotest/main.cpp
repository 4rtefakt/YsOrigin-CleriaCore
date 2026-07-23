// Headless test for the XSO coroutine VM (CUTSCENE_COROUTINE.md): verifies run()
// is unchanged, tick() parks/advances on Wait_Prompt, and the ScriptManager spawns +
// ticks concurrent StartScript children. Usage: cleria-cotest <assets>
#include "cleria/sev.hpp"
#include "cleria/xso.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <fstream>
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
        for (bh.frame = 1; bh.frame < 600 && mgr.any_live(); ++bh.frame) mgr.tick_all();
        // Order: move first (frame 0), kill after the 180f pan, return after the 60f hold,
        // and the whole thing ends near frame 360 (180+60+120). Allow small slack.
        const bool ordered = bh.fMove == 0 && bh.fKill > bh.fMove && bh.fReturn > bh.fKill;
        const bool timed = bh.fKill >= 175 && bh.fKill <= 185 && bh.fReturn >= 235 && bh.fReturn <= 245;
        const bool ended = !mgr.any_live() && bh.frame >= 350 && bh.frame <= 370;
        const bool ok = ordered && timed && ended;
        std::printf("[brk]  KEKKAIBREAK: move@%d kill@%d return@%d end@%d %s\n",
                    bh.fMove, bh.fKill, bh.fReturn, bh.frame, ok ? "OK" : "FAIL");
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
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
