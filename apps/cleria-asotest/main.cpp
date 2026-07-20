// Validation harness for the ASO animation-script VM.
//
//   cleria-asotest <asset_root>              validate every .ASO container
//   cleria-asotest <file.aso> --run <id> <n> run section <id> for n ticks, printing
//                                            the (tag, frame) the script asks for
//
// The container check reproduces the RE's stated invariants rather than trusting
// them: the section table must end exactly at codeBase, sections must tile the blob
// contiguously with no gaps, and the last must end exactly at EOF.
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "cleria/aso.hpp"

namespace fs = std::filesystem;
using namespace cleria;

static std::vector<std::uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: cleria-asotest <asset_root> | <f.aso> --run <id> <ticks>\n");
        return 2;
    }

    if (argc >= 5 && std::strcmp(argv[2], "--run") == 0) {
        Aso a = Aso::load(argv[1]);
        if (!a.ok()) { std::fprintf(stderr, "load failed\n"); return 1; }
        AsoVm vm;
        vm.bind(&a);
        const int id = std::atoi(argv[3]);
        if (!vm.set_section(id)) { std::fprintf(stderr, "no section id %d\n", id); return 1; }
        const int n = std::atoi(argv[4]);
        std::printf("section %d, %d ticks — (tag, frame) per tick:\n", id, n);
        int last_tag = -99, last_frame = -99, held = 0;
        for (int t = 0; t < n; ++t) {
            vm.tick();
            if (vm.anim_tag() != last_tag || vm.frame() != last_frame) {
                if (held) std::printf("  tag=%-4d frame=%-3d held %d ticks\n", last_tag, last_frame, held);
                last_tag = vm.anim_tag();
                last_frame = vm.frame();
                held = 1;
            } else {
                ++held;
            }
            if (vm.foot_smoke()) std::printf("  [FootSmoke] at tick %d\n", t);
            for (const AsoVm::SoundEvent& se : vm.sounds())
                std::printf("  [SePlay3D] tick %d: EFFECTDEF id %d  offset (%.2f, %.2f, %.2f)\n", t,
                            se.id, se.dx, se.dy, se.dz);
            if (vm.armed_hitbox()) {
                const AsoVm::Hitbox& h = vm.hitbox();
                std::printf("  [GenerateCollision] tick %d: radius=%.3f half_angle=%.1fdeg "
                            "lifetime=%d power=%.1f\n", t, h.radius,
                            h.half_angle * 180.0f / 3.14159265f, h.lifetime, h.power);
            }
            if (t == 0)
                std::printf("  -> anime_File(%d) = %s   (the script picks the sprite)\n",
                            vm.file_id(), aso_aia_name(vm.file_id()));
        }
        if (held) std::printf("  tag=%-4d frame=%-3d held %d ticks\n", last_tag, last_frame, held);
        return 0;
    }

    int files = 0, ok = 0, bad_tile = 0, with_id2 = 0;
    std::map<int, int> types;
    std::map<int, long> opcodes;   // vs the RE: 0x02=48580 0x01=1479 0x03=1420 0x06=786 0x09=15
    std::map<int, long> calls;     // funcid -> uses, vs funclist_map.csv
    long sections = 0, instrs = 0, decode_err = 0, desc_bad = 0;
    for (const auto& e : fs::recursive_directory_iterator(argv[1])) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::toupper((unsigned char)c));
        if (ext != ".ASO") continue;
        ++files;
        const std::vector<std::uint8_t> b = read_file(e.path());
        Aso a = Aso::parse(b);
        if (!a.ok()) { std::printf("  PARSE FAIL %s\n", e.path().filename().string().c_str()); continue; }
        ++ok;
        sections += long(a.sections().size());
        for (const Aso::Section& s : a.sections()) types[s.type]++;
        if (a.find(2)) ++with_id2;

        // Sections must tile the code blob contiguously, and the last must end at EOF.
        const std::size_t code_base = 0x10 + a.sections().size() * 0x14;
        std::size_t expect = 0;
        bool tiled = true;
        for (const Aso::Section& s : a.sections()) {
            if (std::size_t(s.code_off) != expect) tiled = false;
            expect += std::size_t(s.code_len) * 4;
        }
        if (code_base + expect != b.size()) tiled = false;
        if (!tiled) { ++bad_tile; std::printf("  TILE FAIL %s\n", e.path().filename().string().c_str()); }

        // Linear-decode every section. The encoding must tile each section exactly:
        // an instruction's length is implied by its own word, so a wrong length walks
        // off the end and the counts below will not match the RE's.
        const std::vector<std::uint32_t>& c = a.code();
        for (const Aso::Section& s : a.sections()) {
            std::size_t pc = std::size_t(s.code_off) / 4;
            const std::size_t end = pc + std::size_t(s.code_len);
            while (pc < end) {
                const std::uint32_t w = c[pc];
                const int op = int(w >> 24);
                ++opcodes[op];
                ++instrs;
                if (op == 0x02) {
                    const std::uint32_t ao = (w >> 8) & 0xF, ac = w & 0xFF;
                    ++calls[int((w >> 12) & 0xFFF)];
                    // argOffset == ceil(argCount/8): the descriptor words are 4 bits
                    // per arg, 8 args per word.
                    if (ao != (ac + 7) / 8) ++desc_bad;
                    pc += 1 + ao + ac;
                } else if (op == 0x01) {
                    ++pc;
                } else if (op == 0x03 || op == 0x06 || op == 0x09) {
                    pc += 2;
                } else {
                    ++decode_err;
                    break;
                }
            }
            if (pc != end) ++decode_err;
        }
    }
    std::printf("\nASO files: %d   parsed: %d   contiguous+EOF-exact: %d   with section id 2: %d\n",
                files, ok, ok - bad_tile, with_id2);
    std::printf("sections: %ld   types:", sections);
    for (const auto& [t, n] : types) std::printf("  type%d=%d", t, n);
    std::printf("\n\ninstructions: %ld   decode errors: %ld   argOffset != ceil(argc/8): %ld\n",
                instrs, decode_err, desc_bad);
    std::printf("opcodes:");
    for (const auto& [o, n] : opcodes) std::printf("  %#04x=%ld", o, n);
    std::printf("\ntop calls:");
    std::vector<std::pair<long, int>> byn;
    for (const auto& [f, n] : calls) byn.emplace_back(n, f);
    std::sort(byn.rbegin(), byn.rend());
    for (std::size_t i = 0; i < byn.size() && i < 6; ++i)
        std::printf("  func%d=%ld", byn[i].second, byn[i].first);
    std::printf("\n");

    // GenerateCollision (funcid 0x23): the melee hit-volume the AsoVm now surfaces.
    // Lock the verified swing: C_010 (Yunica) section 0x100 arms at frame 0, radius 0.6,
    // 30-frame window, power 8.0 (build/attack_re/ATTACK.md).
    int gencol_fail = 0;
    {
        Aso a = Aso::load(fs::path(argv[1]) / "ANI" / "C_010.ASO");
        if (a.ok()) {
            AsoVm vm; vm.bind(&a); vm.set_section(0x100);
            bool armed = false; AsoVm::Hitbox h;
            for (int t = 0; t < 4 && !armed; ++t) { vm.tick(); if (vm.armed_hitbox()) { armed = true; h = vm.hitbox(); } }
            const bool okhit = armed && std::fabs(h.radius - 0.6f) < 0.01f &&
                               h.lifetime == 30 && std::fabs(h.power - 8.0f) < 0.01f;
            std::printf("GenerateCollision C_010 sec0x100: %s (r=%.2f life=%d pow=%.1f)\n",
                        okhit ? "OK" : "MISMATCH", h.radius, h.lifetime, h.power);
            if (!okhit) gencol_fail = 1;
        }
    }
    return (ok == files && bad_tile == 0 && decode_err == 0 && !gencol_fail) ? 0 : 1;
}
