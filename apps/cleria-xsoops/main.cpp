// cleria-xsoops — the XSO VM's op coverage probe for tools/xso_floor_ops.py (FLOOR13_OPS.md).
// Prints "0x<sub> vm|unhandled" for every sub-op 0..0x18f.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "cleria/xso.hpp"

using namespace cleria;

// --op-status: run every sub-op 0..0x18f once through XsoVm (coroutine mode, a do-nothing host,
// 22 int operands of value 1) and print whether it reached the VM's unhandled fallthrough.
// tools/xso_floor_ops.py reads this to classify the floor-1..3 census (FLOOR13_OPS.md).
static int cmd_op_status() {
    struct ProbeHost : XsoHost {
        void spawn(int, int, float, float, float, float, int) override {}
        bool is_alive(int) const override { return false; }
    } host;
    for (std::uint32_t sub = 0; sub < 0x190; ++sub) {
        const std::uint32_t nargs = 22, skip = (nargs + 7) / 8;
        std::vector<std::int32_t> code;
        code.push_back(std::int32_t((2u << 24) | (sub << 12) | (skip << 8) | nargs));
        for (std::uint32_t k = 0; k < skip; ++k) code.push_back(0);         // all operands int
        for (std::uint32_t k = 0; k < nargs; ++k) code.push_back(1);
        code.push_back(std::int32_t(1u << 24));                               // end
        std::vector<std::uint8_t> b(0x24 + code.size() * 4, 0);
        std::memcpy(b.data(), "XSR\0", 4);
        const std::uint32_t words = std::uint32_t(code.size());
        std::memcpy(b.data() + 0x1c, &words, 4);
        std::memcpy(b.data() + 0x24, code.data(), code.size() * 4);
        const Xso x = Xso::parse(b);
        XsoVm vm;
        vm.host = &host;
        bool unhandled = false;
        vm.on_unhandled = [&](std::uint32_t) { unhandled = true; };
        vm.on_start_script = [](const std::string&, const XsoVm::Vars&, int) {};
        vm.on_fix = [](int) {};
        vm.on_unfix = [] {};
        vm.on_window_fix = [] {};
        vm.co_begin(x);
        vm.tick();
        std::printf("0x%03x %s\n", sub, unhandled ? "unhandled" : "vm");
    }
    return 0;
}

int main() { return cmd_op_status(); }
