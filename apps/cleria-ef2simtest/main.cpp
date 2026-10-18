// cleria-ef2simtest — the EF2 particle simulation (src/cleria/ef2_sim.hpp) against the worked
// examples of build/ai_spec/EF2SIM.md §5: effect 267 (enemy\m_bom01) and 251 (enemy\m_540_fire),
// identity effect matrix, the RNG fixture of §5 (the .data xorshift words, nothing else drawing).
// Usage: cleria-ef2simtest <assets dir>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include "cleria/ef2.hpp"
#include "cleria/ef2_sim.hpp"

using namespace cleria;
using namespace cleria::ef2sim;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[ef2] %-66s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float tol = 2e-4f) { return std::fabs(a - b) <= tol; }
static bool near3(const Vec3& v, float x, float y, float z, float tol = 2e-4f) {
    return near(v.x, x, tol) && near(v.y, y, tol) && near(v.z, z, tol);
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: cleria-ef2simtest <assets>\n"); return 2; }
    const std::filesystem::path root = argv[1];

    // ---- the render word's blend (B2+0x08, FUN_005eb5b0): effect 504, enemy\m_970_attack01 (the bat's dive) ----
    {
        const Ef2 ef = Ef2::load(root / "EFFECT" / "EFT" / "ENEMY" / "M_970_ATTACK01.EF2");
        const auto& em = ef.emitters();
        check(ef.ok() && em.size() == 5, "M_970_ATTACK01.EF2 loads: 5 emitters");
        check(em.size() == 5 && em[0].additive && em[2].additive && em[3].subtractive && !em[3].additive &&
              !em[4].additive && !em[4].subtractive && !em[4].premult,
              "blend: em0 / em2 additive (0x46c1 / 0xc6c1), em3 subtractive (0x204641), em4 alpha (0xc641)");
    }

    // ---- 5.1 effect 267, enemy\m_bom01 ----
    {
        const Ef2 ef = Ef2::load(root / "EFFECT" / "EFT" / "ENEMY" / "M_BOM01.EF2");
        check(ef.ok() && ef.emitters().size() == 9, "M_BOM01.EF2 loads: 9 emitters");
        Rng rng;
        System sys(&rng);
        sys.spawn(267, ef, SpawnParams{});
        check(rng.calls == 194, "spawn: 194 draws (1 + 1 + 24 x 8)");
        check(sys.particles().size() == 26, "spawn: em0, em3 and 24 em5 sparks");
        sys.step();
        std::printf("[ef2]   after step 1: %zu particles, %llu draws\n", sys.particles().size(), (unsigned long long)rng.calls);
        check(rng.calls == 259 && sys.particles().size() == 35, "step 1: em0 fires em2 x4 + em8, em3 fires em4 x2 + em7 x2 (259 draws)");
        const Particle& p0 = *sys.particles()[0];
        const Particle& p2 = *sys.particles()[2];
        const Vec3 s0{p0.scale.x * p0.mult.x, p0.scale.y * p0.mult.y, p0.scale.z * p0.mult.z};
        std::printf("[ef2]   p0 rem %d scale*mult %.4f %.4f %.4f alpha %u; p2 at %.4f %.4f %.4f\n", p0.remaining,
                    s0.x, s0.y, s0.z, p0.diffuse >> 24, p2.world_pos().x, p2.world_pos().y, p2.world_pos().z);
        check(p0.remaining == 19 && near3(s0, 1.0681f, 1.5054f, 1.0860f) && (p0.diffuse >> 24) == 136,
              "step 1: em0 p0 rem 19, scale*mult (1.0681 1.5054 1.0860), alpha 136");
        check(near3(p2.world_pos(), 0.0165f, -0.0047f, 0.0082f), "step 1: em5 p2 (first spark) at (0.0165 -0.0047 0.0082)");
        check(p2.life == 42 && near(p2.speed, 0.973083f, 1e-5f) && near3(p2.vel, 0.046614f, -0.011789f, 0.023198f, 1e-5f),
              "p2: life 42, speed 0.973083, velocity (0.046614 -0.011789 0.023198)");
        sys.step();
        const Particle& p30 = *sys.particles()[30];
        const Particle& p31 = *sys.particles()[31];
        const Vec3 s30{p30.scale.x * p30.mult.x, p30.scale.y * p30.mult.y, p30.scale.z * p30.mult.z};
        std::printf("[ef2]   step 2: p30 rem %d scale*mult %.4f %.4f %.4f r %.4f; p31 rem %d at %.4f %.4f %.4f frame %d alpha %u\n",
                    p30.remaining, s30.x, s30.y, s30.z, p30.hit.radius, p31.remaining, p31.world_pos().x,
                    p31.world_pos().y, p31.world_pos().z, p31.frame, p31.diffuse >> 24);
        check(p30.em == 8 && p30.remaining == 19 && near3(s30, 0.6885f, 0.3543f, 0.6875f) && near(p30.hit.radius, 1.0423f),
              "step 2: em8 p30 (the hit) rem 19, scale*mult (0.6885 0.3543 0.6875), radius 1.0423");
        check(p31.em == 4 && p31.remaining == 14 && near3(p31.world_pos(), -0.0237f, 0.1155f, 0.1394f) &&
              p31.frame == 0 && (p31.diffuse >> 24) == 179,
              "step 2: em4 p31 rem 14 at (-0.0237 0.1155 0.1394), frame 0, alpha 179");
        int s = 2;
        for (; s < 5; ++s) sys.step();
        check(rng.calls == 311, "step 5: em3 fires em4 x2 + em7 x2 again (311 draws)");
        for (; s < 19; ++s) sys.step();
        const Particle& q0 = *sys.particles()[0];
        check(q0.remaining == 1 && (q0.diffuse >> 24) == 25 && near(q0.scale.x * q0.mult.x, 4.2335f),
              "step 19: em0 rem 1, alpha 25, scale x 4.2335");
        sys.step();                                                // step 20: p0 dies
        check(sys.particles()[0]->dead, "step 20: em0 p0 reaches 0 and dies");
        check(near(sys.particles()[30]->hit.radius, 1.3215f), "step 20: em8 hit radius 1.3215");
    }

    // ---- 5.2 effect 251, enemy\m_540_fire ----
    {
        const Ef2 ef = Ef2::load(root / "EFFECT" / "EFT" / "ENEMY" / "M_540_FIRE.EF2");
        check(ef.ok(), "M_540_FIRE.EF2 loads");
        Rng rng;
        System sys(&rng);
        sys.spawn(251, ef, SpawnParams{});
        check(rng.calls == 7, "spawn: 7 draws");
        sys.step();
        check(rng.calls == 21, "step 1: 21 draws");
        sys.step();
        check(rng.calls == 31, "step 2: 31 draws");
        // em8's flame (delay 0) moves +0.1 x a step from (0.5, -0.6, 0).
        const Particle* p7 = nullptr;
        int seen = 0;
        for (const auto& p : sys.particles()) if (p->em == 8 && seen++ == 0) p7 = p.get();
        check(p7 && near3(p7->world_pos(), 0.6f, -0.6f, 0.0f), "step 2: em8 p7 at (0.6 -0.6 0)");
        for (int s = 2; s < 10; ++s) sys.step();
        std::printf("[ef2]   251 step 10: %llu draws, p7 at %.4f %.4f %.4f\n", (unsigned long long)rng.calls,
                    p7->world_pos().x, p7->world_pos().y, p7->world_pos().z);
        check(rng.calls == 130 && near3(p7->world_pos(), 1.3647f, -0.6f, -0.1413f), "step 10: 130 draws; p7 (turbulence-turned) at (1.3647 -0.6 -0.1413)");
        int puffs = 0;
        for (int s = 10; s < 30; ++s) sys.step();
        for (const auto& p : sys.particles()) puffs += p->em >= 12 && p->em <= 15;
        std::printf("[ef2]   251: %d hit puffs spawned\n", puffs);
        check(puffs == 16, "16 hit puffs (em12..15) per effect");
    }

    // ---- the draw depth bias fx+0x1f0 (emitter 0's B2+0x0c) and FUN_00451e60's fx+0x1f4 ----
    {
        struct Want { const char* file; float bias; };
        const Want ws[] = {{"PC/ADOL/ADOL_S_MODE.EF2", 2.0f},     // 39, the Boost aura
                           {"PC/ADOL/ADOL_S_MODE_F.EF2", 0.0f},   // 50, the Boost entry
                           {"COMMON/CURE.EF2", 5.0f},             // 63, the heal / buff pickup
                           {"COMMON/Z_YARARE01.EF2", 1.0f},       // 0x41, the hit spark
                           {"COMMON/M_ZAKOYARARE.EF2", 2.0f}};    // 0x56, the death puff
        for (const Want& w : ws) {
            const Ef2 ef = Ef2::load(root / "EFFECT" / "EFT" / w.file);
            Rng rng;
            System sys(&rng);
            const Effect* fx = ef.ok() ? sys.spawn(1, ef, SpawnParams{}) : nullptr;
            char what[128];
            std::snprintf(what, sizeof what, "%s: depth bias %.1f", w.file, w.bias);
            check(fx && fx->depth_bias == w.bias, what);
        }
        const Vec3 o = depth_bias_offset({0, 0, 10}, {0, 0, 0}, 2.0f);
        check(near3(o, 0, 0, 2.0f), "fx+0x1f4 = normalize(fx - eye) x bias (drawn at p - it: toward the eye)");
        check(near3(depth_bias_offset({3, 4, 0}, {0, 0, 0}, 0.0f), 0, 0, 0) &&
              near3(depth_bias_offset({1, 1, 1}, {1, 1, 1}, 5.0f), 0, 0, 0), "no bias, or the eye at the effect: none");
    }

    // ---- a kill's death effects (FUN_00478510; item/PICKUPS.md §12 item 10, counted by a Python EF2SIM over 200
    // seeds): the draws at creation, and in all over the effect's life (the creation's included), which keep the
    // shared xorshift in step. An ordinary kill: 1 (FUN_00478510's entry) + 91 (the puff) + 57..128 (the spark).
    // z_yarare03 (0x43, the revivers' puff) is checked below: its turbulence varies the count.
    {
        struct Want { const char* file; int id; unsigned create, life; };
        const Want ws[] = {{"COMMON/M_ZAKOYARARE.EF2", 0x56, 1, 91},        // the puff
                           {"COMMON/Z_YARARE01.EF2", 0x41, 1, 90},          // the spark, Adol and Hugo
                           {"PC/PC01/PC1_KEN_HIT.EF2", 0x7e, 80, 128},      // Yunica's sword
                           {"PC/PC01/PC1_ONO_HIT.EF2", 0x7f, 80, 120},      // Yunica's axe
                           {"PC/PC03/PC3_HIT.EF2", 0xb6, 1, 57}};           // Toal
        for (const Want& w : ws) {
            const Ef2 ef = Ef2::load(root / "EFFECT" / "EFT" / w.file);
            for (std::uint32_t seed = 1; seed <= 4; ++seed) {
                Rng rng;
                rng.w ^= seed * 0x9e3779b9u;                       // a few states, not only the .data words
                System sys(&rng);
                const bool ok = ef.ok() && sys.spawn(w.id, ef, SpawnParams{}) != nullptr;
                const std::uint64_t c0 = rng.calls;
                int s = 0;
                for (; ok && s < 600 && !sys.particles().empty(); ++s) { sys.step(); sys.sweep(); }
                char what[160];
                std::snprintf(what, sizeof what, "%s seed %u: %llu draws (%llu at creation) in %d steps", w.file,
                              seed, (unsigned long long)rng.calls, (unsigned long long)c0, s);
                check(ok && c0 == w.create && rng.calls == w.life && sys.particles().empty(), what);
            }
        }
    }

    // ---- z_yarare03 (0x43): 1630 fixed draws + 3 per turbulence pass. Root em0 (1 at creation: the shadow phase) emits
    // 4 em1 at its first step and 3 em2 at age 20; each em2 emits an em3 at its first step and at remaining 76, 72 .. 4
    // (mode 3, period 4): 60 em3. Spawn draws: em1 shadow + delay (D 15) + roll jitter = 3, em2 / em3 shadow + delay
    // (D 5) + 3 rotation jitters = 5; 1 + 12 + 15 + 300 = 328. Turbulence (B2+0x8a2 = 30, 20/256) tests every motion
    // step whose remaining & 30 != 0 [BYTES 0x44d6f9..0x44d774: `test byte [sub+0x24], al`, after the decrement]: em2 80
    // steps (remaining 79..0) -> 74 tests, em3 20 -> 18: 222 + 1080 = 1302; a pass takes 3 more. So the count is
    // 1630 + 3k, k ~ Binomial(1302, 20/256) (mean 101.7, sd 9.7): about 1935 +- 29, as this sim gives. The "about
    // 1860..1910" of item/PICKUPS.md §12 item 10 (a Python sim) is below that mean.
    {
        const Ef2 ef = Ef2::load(root / "EFFECT" / "EFT" / "COMMON" / "Z_YARARE03.EF2");
        for (std::uint32_t seed = 1; seed <= 16; ++seed) {
            Rng rng;
            rng.w ^= seed * 0x9e3779b9u;
            System sys(&rng);
            const bool ok = ef.ok() && sys.spawn(0x43, ef, SpawnParams{}) != nullptr;
            const std::uint64_t c0 = rng.calls;
            for (int s = 0; ok && s < 600 && !sys.particles().empty(); ++s) { sys.step(); sys.sweep(); }
            const std::uint64_t k3 = rng.calls - 1630;
            char what[160];
            std::snprintf(what, sizeof what, "Z_YARARE03 seed %u: %llu draws (%llu at creation) = 1630 + 3 x %llu passes",
                          seed, (unsigned long long)rng.calls, (unsigned long long)c0, (unsigned long long)(k3 / 3));
            check(ok && c0 == 1 && rng.calls >= 1630 && k3 % 3 == 0 && k3 / 3 >= 60 && k3 / 3 <= 145 &&
                      sys.particles().empty(), what);
        }
    }

    // ---- M860's pillar break (build/mech_re/KISHGAL_SHIELD.md §3.2): EF2 290 against Toal's Ignis 162 ----
    {
        const Ef2 pillar = Ef2::load(root / "EFFECT" / "EFT" / "ENEMY" / "M_860_HYOUTYUU.EF2");
        const Ef2 ignis = Ef2::load(root / "EFFECT" / "EFT" / "PC" / "PC03" / "PC3_S_B.EF2");
        check(pillar.ok() && ignis.ok(), "M_860_HYOUTYUU.EF2 (290) and PC3_S_B.EF2 (162) load");
        static const int kIds[2] = {101, 162};
        // The first tick (of 40) on which a 162 at `dist` along x breaks a pillar at the origin; -1 = never.
        auto broke_at = [&](float dist, int skill_id, bool marked, int* mode, bool* kill) {
            Rng rng;
            System sys(&rng);
            Effect* s = sys.spawn(290, pillar, SpawnParams{});
            for (int i = 0; i < 5; ++i) { sys.step(); sys.sweep(); }
            if (marked) s->attach_mode = 0xff;
            SpawnParams sp; sp.pos = {dist, 0.0f, 0.0f}; sp.side = 0;
            sys.spawn(skill_id, ignis, sp);
            int at = -1;
            for (int i = 0; i < 40 && at < 0; ++i) {
                sys.step(); sys.sweep();
                const std::vector<Vec3> b = sys.break_overlapping(290, kIds, 2);
                if (!b.empty()) at = i;
            }
            if (mode) *mode = s->attach_mode;
            if (kill) *kill = s->kill;
            return at;
        };
        int mode = 0; bool kill = false;
        const int near_t = broke_at(1.0f, 162, false, &mode, &kill);
        std::printf("[ef2]   162 at 1.0 from a 290: broken on step %d (attach %d kill %d)\n", near_t, mode, int(kill));
        check(near_t >= 0 && mode == 0xff && kill, "a 162 hit particle 1.0 from a pillar breaks it: +0x1d5 = 0xff, kill flag");
        check(broke_at(12.0f, 162, false, &mode, &kill) < 0 && mode == 0 && !kill, "12.0 away: no break, the pillar untouched");
        check(broke_at(1.0f, 162, true, &mode, nullptr) < 0, "a pillar already marked 0xff is skipped");
        check(broke_at(1.0f, 163, false, &mode, &kill) < 0 && mode == 0, "an effect id outside the list breaks nothing");
        float edge = 0.0f;
        for (float d = 0.5f; d < 8.0f; d += 0.25f) if (broke_at(d, 162, false, nullptr, nullptr) >= 0) edge = d;
        std::printf("[ef2]   162 breaks a 290 out to %.2f along x (the sum of the two scaled radii)\n", edge);
    }

    std::printf(fails ? "SOME TESTS FAILED\n" : "ALL PASS\n");
    return fails ? 1 : 0;
}
