// Headless test for the XSO coroutine VM (CUTSCENE_COROUTINE.md): verifies run()
// is unchanged, tick() parks/advances on Wait_Prompt, and the ScriptManager spawns +
// ticks concurrent StartScript children. Usage: cleria-cotest <assets>
#include "cleria/xso.hpp"
#include <algorithm>
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
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
