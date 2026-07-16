// cleria-xso: XSO event-script tool.
//   cleria-xso <file.xso>                 disassemble
//   cleria-xso --run <file.xso>           execute; print grants + items
//   cleria-xso --grants <dir> <out.csv>   static grant scan over a tree
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cleria/xso.hpp"

namespace fs = std::filesystem;
using namespace cleria;

static std::vector<std::uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static int cmd_disasm(const fs::path& p) {
    Xso x = Xso::parse(read_file(p));
    std::printf("%s: %u code words\n", p.filename().string().c_str(), x.code_words());
    for (const XsoInstr& in : x.disasm()) {
        if (in.unknown) { std::printf("  %4u: UNKNOWN class %u\n", in.pc, in.cls); break; }
        if (in.cls == 2) {
            std::printf("  %4u: call sub=0x%-3X (%u args:", in.pc, in.sub,
                        unsigned(in.ops.size()));
            for (std::int32_t o : in.ops) std::printf(" %d", o);
            std::printf(")\n");
        } else {
            std::printf("  %4u: class 0x%X\n", in.pc, in.cls);
        }
    }
    return 0;
}

static int cmd_run(const fs::path& p, int character) {
    Xso x = Xso::parse(read_file(p));
    XsoVm vm;
    if (character) vm.g_flags[150] = character;  // 1 Yunica / 2 Hugo / 3 Toal
    const int steps = vm.run(x);
    std::printf("%s: ran %d steps (char %d)\n", p.filename().string().c_str(), steps, character);
    if (!vm.grants.empty()) {
        std::printf("  grants (%zu):\n", vm.grants.size());
        for (const Grant& g : vm.grants)
            std::printf("    g_flags[0x%X] %s %d\n", g.index, g.op.c_str(), g.value);
    }
    if (!vm.items.empty()) { std::printf("  give-item:"); for (int it : vm.items) std::printf(" 0x%X", it); std::printf("\n"); }
    if (!vm.acquires.empty()) { std::printf("  acquire-box:"); for (int it : vm.acquires) std::printf(" 0x%X", it); std::printf("\n"); }
    if (!vm.obj_enable.empty()) { std::printf("  enable:"); for (auto& s : vm.obj_enable) std::printf(" %s", s.c_str()); std::printf("\n"); }
    if (!vm.obj_opened.empty()) { std::printf("  set-opened:"); for (auto& s : vm.obj_opened) std::printf(" %s", s.c_str()); std::printf("\n"); }
    return 0;
}

static int cmd_grants(const fs::path& root, const fs::path& out_csv) {
    std::ofstream csv(out_csv);
    csv << "file,flag_idx,op,value\n";
    std::size_t total = 0, scripts = 0, bad = 0;
    std::vector<fs::path> files;
    for (auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) {
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = char(std::tolower((unsigned char)c));
            if (ext == ".xso") files.push_back(e.path());
        }
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files) {
        try {
            Xso x = Xso::parse(read_file(f));
            ++scripts;
            for (const Grant& g : x.grants()) {
                ++total;
                csv << f.filename().string() << "," << g.index << "," << g.op << ","
                    << (g.has_value ? std::to_string(g.value) : "") << "\n";
            }
        } catch (const std::exception&) { ++bad; }
    }
    std::printf("scanned %zu scripts (%zu unreadable) -> %zu grants -> %s\n",
                scripts, bad, total, out_csv.string().c_str());
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: cleria-xso <file.xso> | --run <file> | --grants <dir> <csv>\n");
        return 2;
    }
    try {
        if (std::strcmp(argv[1], "--grants") == 0 && argc >= 4)
            return cmd_grants(argv[2], argv[3]);
        if (std::strcmp(argv[1], "--run") == 0 && argc >= 3)
            return cmd_run(argv[2], argc >= 4 ? std::atoi(argv[3]) : 0);
        return cmd_disasm(argv[1]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
