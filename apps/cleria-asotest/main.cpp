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
#include "cleria/ef2.hpp"
#include "cleria/hit_feedback.hpp"

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
            {   // capability gates — the windows that actually define an attack's length
                static int pa = -1, pc2 = -1, pr = -1, pt = -1;
                static float pm = -1;
                if (int(vm.actable()) != pa || int(vm.cancelable()) != pc2 ||
                    int(vm.attack_reserve()) != pr || int(vm.turnable()) != pt ||
                    vm.movable() != pm) {
                    pa = vm.actable(); pc2 = vm.cancelable(); pr = vm.attack_reserve();
                    pt = vm.turnable(); pm = vm.movable();
                    std::printf("  [gates] tick %-3d actable=%d cancelable=%d reserve=%d "
                                "turnable=%d movable=%.2f\n", t, pa, pc2, pr, pt, pm);
                }
            }
            if (vm.section_ended())
                std::printf("  [section ended] tick %d — chained to section %d\n", t, vm.section());
            for (const AsoVm::BulletEvent& b : vm.bullets()) {
                std::printf("  [FireBullet] tick %d: kind=%d id=%d  f=", t, b.kind, b.id);
                for (int i = 0; i < 8; ++i) std::printf("%g ", b.f[i]);
                std::printf("\n");
            }
            for (const AsoVm::SoundEvent& se : vm.sounds())
                std::printf("  [SePlay3D] tick %d: EFFECTDEF id %d  offset (%.2f, %.2f, %.2f)\n", t,
                            se.id, se.dx, se.dy, se.dz);
            if (vm.armed_hitbox()) {
                const AsoVm::Hitbox& h = vm.hitbox();
                std::printf("  [GenerateCollision] tick %d: shape=%d radius=%.3f fwd=%.3f angle=%.1fdeg "
                            "voff=%.3f half_h=%.3f life=%d rehit=%d one_shot=%d rtype=%d status=%d power=%.1f knock=%.2f\n",
                            t, h.shape, h.radius, h.fwd, h.angle * 180.0f / 3.14159265f, h.voff, h.half_h,
                            h.life, h.rehit, int(h.one_shot), h.rtype, h.status, h.power, h.knock);
            }
            if (t == 0)
                std::printf("  -> anime_File(%d) = %s   (the script picks the sprite)\n",
                            vm.file_id(), aso_aia_name(vm.file_id()));
        }
        if (held) std::printf("  tag=%-4d frame=%-3d held %d ticks\n", last_tag, last_frame, held);
        return 0;
    }

    // Resolve every projectile the scripts fire, end to end: ASO anime_FireBullet
    // arg[1] -> eftlist -> .EF2 -> its emitter's texture id -> the .DDS on disk. This
    // is the chain that decides whether a bullet can be drawn at all, so it is worth
    // being able to check in one command.
    //   cleria-asotest <asset_root> --bullets
    if (argc >= 3 && std::strcmp(argv[2], "--bullets") == 0) {
        const fs::path root = argv[1];
        EftList efl;
        EftTex eft;
        if (!efl.load(root) || !eft.load(root)) {
            std::fprintf(stderr, "eftlist/efttex load failed under %s\n", argv[1]);
            return 1;
        }
        std::printf("eftlist: %zu ids, efttex: %zu textures\n", efl.size(), eft.size());
        std::map<int, int> uses;      // effect id -> how many FireBullet calls
        std::map<int, std::string> from;
        for (const auto& e : fs::recursive_directory_iterator(root)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = char(std::toupper((unsigned char)c));
            if (ext != ".ASO") continue;
            Aso a = Aso::parse(read_file(e.path()));
            if (!a.ok()) continue;
            AsoVm vm;
            vm.bind(&a);
            for (const Aso::Section& s : a.sections()) {
                if (!vm.set_section(s.id, true)) continue;
                for (int t = 0; t < 240; ++t) {
                    vm.tick();
                    for (const AsoVm::BulletEvent& b : vm.bullets()) {
                        if (!uses.count(b.id)) from[b.id] = e.path().stem().string();
                        ++uses[b.id];
                    }
                }
            }
        }
        int drawable = 0;
        for (const auto& [id, n] : uses) {
            const fs::path ef2p = efl.path(root, id);
            const Ef2 ef = ef2p.empty() ? Ef2{} : Ef2::load(ef2p);
            const Ef2Emitter* em = ef.ok() ? ef.primary() : nullptr;
            const fs::path tex = em ? eft.path(root, int(em->frames[0].tex_id)) : fs::path();
            if (!tex.empty()) ++drawable;
            std::printf("  id %-4d x%-4d %-28s %-34s %s\n", id, n, from[id].c_str(),
                        efl.name(id).c_str(),
                        tex.empty() ? "** UNRESOLVED **" : tex.filename().string().c_str());
        }
        std::printf("\n%zu distinct projectile effects, %d resolve to a texture on disk\n",
                    uses.size(), drawable);
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
            // build/ai_spec/HITVOLUME.md §6: sphere r 0.5, 0.6 ahead, 0.5 down, life 30, power 8.
            const bool okhit = armed && h.shape == 0 && std::fabs(h.radius - 0.5f) < 0.01f &&
                               std::fabs(h.fwd - 0.6f) < 0.01f && std::fabs(h.voff + 0.5f) < 0.01f &&
                               h.life == 30 && std::fabs(h.power - 8.0f) < 0.01f;
            std::printf("GenerateCollision C_010 sec0x100: %s (r=%.2f fwd=%.2f voff=%.2f life=%d pow=%.1f)\n",
                        okhit ? "OK" : "MISMATCH", h.radius, h.fwd, h.voff, h.life, h.power);
            if (!okhit) gencol_fail = 1;
        }
        Aso m = Aso::load(fs::path(argv[1]) / "ANI" / "M_180.ASO");
        if (m.ok()) {   // M_180 section 7, first hit: r 0.7, 1.5 ahead, 0.5 up, life 3, re-hit 43, one-shot
            AsoVm vm; vm.bind(&m); vm.set_section(7);
            bool armed = false; AsoVm::Hitbox h;
            for (int t = 0; t < 200 && !armed; ++t) { vm.tick(); if (vm.armed_hitbox()) { armed = true; h = vm.hitbox(); } }
            const bool ok = armed && std::fabs(h.radius - 0.7f) < 0.01f && std::fabs(h.fwd - 1.5f) < 0.01f &&
                            std::fabs(h.voff - 0.5f) < 0.01f && h.life == 3 && h.rehit == 43 && h.one_shot &&
                            h.rtype == 1 && std::fabs(h.knock - 1.25f) < 0.01f;
            std::printf("GenerateCollision M_180 sec7: %s (r=%.2f fwd=%.2f voff=%.2f life=%d rehit=%d one=%d)\n",
                        ok ? "OK" : "MISMATCH", h.radius, h.fwd, h.voff, h.life, h.rehit, int(h.one_shot));
            if (!ok) gencol_fail = 1;
        }
        // anime_GenerateEffect (0x25): Perrie's run section 1 lays its fire puff,
        // GenerateEffect(251, 0, 0, -0.25, 1, 0, rtype 1, status 0, power 0, knock 1) (EFFECTHIT.md §7).
        Aso pe = Aso::load(fs::path(argv[1]) / "ANI" / "M_540.ASO");
        if (pe.ok()) {
            AsoVm vm; vm.bind(&pe); vm.set_section(1);
            bool got = false; AsoVm::EffectEvent ev;
            for (int t = 0; t < 200 && !got; ++t) { vm.tick(); if (!vm.effects().empty()) { got = true; ev = vm.effects()[0]; } }
            const bool ok = got && ev.id == 251 && std::fabs(ev.voff + 0.25f) < 1e-5f && ev.rtype == 1 &&
                            std::fabs(ev.knock - 1.0f) < 1e-5f && ev.power == 0.0f;
            std::printf("GenerateEffect M_540 sec1: %s (id=%d voff=%.2f rtype=%d power=%.1f knock=%.1f)\n",
                        ok ? "OK" : "MISMATCH", ev.id, ev.voff, ev.rtype, ev.power, ev.knock);
            if (!ok) gencol_fail = 1;
        }
    }

    // Section chaining on `end` (FUN_004472e0): Yunica's hurt section 4 is two frames
    // (Wait 5 + Wait 4) then `end`. The engine consumes `end`, yields, and on the next
    // tick chains to the queued follow-on (idle, 2). It must NOT hold forever — that
    // left the player stuck in a hurt section with its i-frames frozen (2026-10-05).
    int chain_fail = 0;
    {
        Aso a = Aso::load(fs::path(argv[1]) / "ANI" / "C_010.ASO");
        if (a.ok()) {
            AsoVm vm; vm.bind(&a); vm.set_section(4, /*force*/true);
            int back = -1;
            for (int t = 1; t <= 60 && back < 0; ++t) { vm.tick(); if (vm.section() == 2) back = t; }
            const bool okc = back > 0 && back <= 14;
            std::printf("section 4 -> 2 on `end`: %s (after %d ticks)\n", okc ? "OK" : "FAIL", back);
            if (!okc) chain_fail = 1;
        }
    }
    // The Flag_ family (FUN_004472e0 0x448468..0x4486d8): indices >= 0x200 are the VM's own registers
    // (LocalInt idx-0x200). Yunica's jump section 3 counts Flag 512 to 128 in its falling hold before
    // Marker(3); without Flag_SetInt / AddInt the landing skip never reached the marker (2026-10-06).
    int flag_fail = 0;
    {
        Aso a = Aso::load(fs::path(argv[1]) / "ANI" / "C_010.ASO");
        if (a.ok()) {
            std::int32_t flags[0x200] = {};
            flags[149] = 1;
            AsoVm vm; vm.bind(&a); vm.set_flags(flags, 0x200); vm.set_section(3, /*force*/true);
            vm.tick();
            vm.fast_forward_to_marker(2);
            vm.set_marker_block(3);
            for (int t = 0; t < 20; ++t) vm.tick();                // in the falling hold
            const bool held = vm.section() == 3 && vm.marker() == 2;
            vm.fast_forward_to_marker(3);                            // the landing
            vm.set_marker_block(-1);
            const bool landed = vm.marker() == 3;
            int back = -1;
            for (int t = 1; t <= 30 && back < 0; ++t) { vm.tick(); if (vm.section() != 3) back = t; }
            const bool okf = held && landed && back > 0 && back <= 12;
            std::printf("jump hold -> Marker(3) via Flag 512 -> end: %s (held %d landed %d, out after %d)\n",
                        okf ? "OK" : "FAIL", int(held), int(landed), back);
            if (!okf) flag_fail = 1;
        }
    }
    // anime_ChangeSpecular (0x1f), anime_AttackBreak (0x14) — build/mech_re/ASO_NOOPS.md §1, §6.
    int spec_fail = 0;
    {
        auto say = [&](bool c, const char* what) { std::printf("%s: %s\n", what, c ? "OK" : "FAIL"); if (!c) spec_fail = 1; };
        Aso m = Aso::load(fs::path(argv[1]) / "ANI" / "M_100.ASO");
        if (m.ok()) {   // M_100 section 7: the wind-up flash, on the section's first tick
            AsoVm vm; vm.bind(&m); vm.set_section(7, /*force*/true);
            vm.tick();
            const AsoVm::SpecularEvent* s = vm.specular();
            say(s && s->b == 0 && s->a == 0xc0c0c0 && std::fabs(s->step - 0.075f) < 1e-6f && s->count == 0,
                "ChangeSpecular M_100 sec7 tick 0 = {b 0, a c0c0c0, 0.075, 0}");
            vm.tick();
            say(vm.specular() == nullptr, "ChangeSpecular: an event lasts the tick that issued it");
        }
        Aso m2 = Aso::load(fs::path(argv[1]) / "ANI" / "M_360.ASO");
        if (m2.ok()) {  // M_360 section 300: the flash up, then (32 ticks on) the fade back from 192 grey
            AsoVm vm; vm.bind(&m2); vm.set_section(300, /*force*/true);
            std::vector<std::pair<int, AsoVm::SpecularEvent>> ev;
            for (int t = 0; t < 120 && ev.size() < 2; ++t) { vm.tick(); if (vm.specular()) ev.push_back({t, *vm.specular()}); }
            say(ev.size() == 2 && ev[1].first - ev[0].first == 32 && ev[1].second.b == 0xc0c0c0 && ev[1].second.a == 0,
                "ChangeSpecular M_360 sec300: two events 32 ticks apart, the second c0c0c0 -> 0");
        }
        {   // The struct's timeline (hitfx::Tint = FUN_0045a3a0 / FUN_00473b70): stepped before the VM, so the op's
            // tick shows t = 0, tick k shows k steps.
            auto run = [](float step, int count, int ticks) {
                hitfx::Tint t; t.a = 0xc0c0c0; t.b = 0; t.step = step; t.count = count;
                std::vector<std::uint32_t> c{t.colour() & 0xff};
                for (int k = 1; k <= ticks; ++k) { t.tick(); c.push_back(t.colour() & 0xff); }
                return c;
            };
            const auto a = run(0.075f, 0, 15);
            bool ramp = a[0] == 0 && a[1] == 14 && a[2] == 28 && a[13] == 187 && a[14] == 0 && a[15] == 0;
            static const std::uint32_t kRamp[14] = {0, 14, 28, 43, 57, 72, 86, 100, 115, 129, 143, 158, 172, 187};
            for (int k = 0; k <= 13; ++k) ramp = ramp && a[std::size_t(k)] == kRamp[k];
            say(ramp, "Tint 0 -> c0 step 0.075 count 0: 0, 14, 28 .. 187 on ticks 0..13, off on tick 14 (the end colour never shows)");
            const auto b = run(0.05f, 30, 51);
            say(b[19] < 192 && b[20] == 192 && b[49] == 192 && b[50] == 0, "Tint step 0.05 count 30: c0 on ticks 20..49, off on 50");
            const auto c = run(0.025f, 5, 42);
            say(c[40] < 192 && c[41] == 192, "Tint step 0.025: the float32 sum reaches 1.0 on tick 41, not 40");
        }
        Aso ad = Aso::load(fs::path(argv[1]) / "ANI" / "C_990.ASO");
        if (ad.ok()) {  // C_990 section 0x116 (a charged swing): AttackBreak(1) from about tick 6; section 2 starts clear
            AsoVm vm; vm.bind(&ad); vm.set_section(0x116, /*force*/true);
            int on = -1;
            for (int t = 0; t < 40 && on < 0; ++t) { vm.tick(); if (vm.attack_break()) on = t; }
            const bool was = on >= 0 && on <= 12;
            vm.set_section(2, /*force*/true); vm.tick();
            say(was && !vm.attack_break(), "AttackBreak C_990 sec0x116: on within 12 ticks; a new section clears vm+0xe5");
        }
        // anime_CameraDistanceOffset (0x0f) — ASO_NOOPS.md §2.3: 0.3 in nearly every section of C_010, none in
        // section 3; Hugo's death goes 0 -> 0.45 -> 0.65. A new section starts at 0 again.
        {
            Aso c10 = Aso::load(fs::path(argv[1]) / "ANI" / "C_010.ASO"), c20 = Aso::load(fs::path(argv[1]) / "ANI" / "C_020.ASO");
            AsoVm vm; vm.bind(&c10); vm.set_section(2, /*force*/true);
            vm.tick();
            const float s2 = vm.sprite_depth_offset();
            vm.set_section(3, /*force*/true);
            float s3 = 0.0f;
            for (int t = 0; t < 10; ++t) { vm.tick(); s3 = std::max(s3, vm.sprite_depth_offset()); }
            say(c10.ok() && s2 == 0.3f && s3 == 0.0f, "CameraDistanceOffset C_010: section 2 = 0.3 on its first tick, section 3 stays 0");
            AsoVm vh; vh.bind(&c20); vh.set_section(5, /*force*/true);
            std::vector<float> seen;
            for (int t = 0; t < 120; ++t) {
                vh.tick();
                if (seen.empty() || seen.back() != vh.sprite_depth_offset()) seen.push_back(vh.sprite_depth_offset());
            }
            say(c20.ok() && seen == std::vector<float>{0.0f, 0.45f, 0.65f}, "CameraDistanceOffset C_020 section 5: 0 -> 0.45 -> 0.65");
        }
        // anime_GenerateDebris (0x26) — ASO_NOOPS.md §3.3: the death section's op.
        auto debris_of = [&](const char* file, int* tick) {
            AsoVm::DebrisEvent ev;
            Aso d = Aso::load(fs::path(argv[1]) / "ANI" / file);
            AsoVm vm;
            *tick = -1;
            if (d.ok() && (vm.bind(&d), vm.set_section(5, /*force*/true)))
                for (int t = 0; t < 60 && *tick < 0; ++t) { vm.tick(); if (vm.debris()) { ev = *vm.debris(); *tick = t; } }
            return ev;
        };
        int t100 = 0, t900 = 0;
        const AsoVm::DebrisEvent d100 = debris_of("M_100.ASO", &t100), d900 = debris_of("M_900.ASO", &t900);
        say(t100 == 0 && d100.count == 5 && d100.spin && !d100.keep && !d100.fixed &&
            d100.bag == std::vector<std::int32_t>{370, 371, 372, 373, 374},
            "GenerateDebris M_100 sec5 tick 0 = {5, spin, bag 370..374}");
        say(t900 >= 0 && d900.count == 8 && d900.bag == std::vector<std::int32_t>{8, 340, 341, 342, 343, 344, 343, 344},
            "GenerateDebris M_900 sec5 = 8 pieces, bag {8, 340..344, 343, 344}");
    }
    return (ok == files && bad_tile == 0 && decode_err == 0 && !gencol_fail && !chain_fail && !flag_fail && !spec_fail) ? 0 : 1;
}
