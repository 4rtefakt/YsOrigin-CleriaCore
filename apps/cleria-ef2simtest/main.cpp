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

    std::printf(fails ? "SOME TESTS FAILED\n" : "ALL PASS\n");
    return fails ? 1 : 0;
}
