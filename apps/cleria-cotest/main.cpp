// Headless test for the XSO coroutine VM (CUTSCENE_COROUTINE.md): verifies run()
// is unchanged and tick() parks/advances on Wait_Prompt. Usage: cleria-cotest <assets>
#include "cleria/xso.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
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
    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
