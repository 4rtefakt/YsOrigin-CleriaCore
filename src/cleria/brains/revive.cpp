// The revive handler family's own code (build/ai_spec/REVIVE.md; M560.md §2, M570.md §2, M780.md
// §2). See revive.hpp. Addresses are M560's (FUN_004a12a0) unless marked.
// [not modelled] FUN_004780e0's effect detach and FUN_004522b0 (attached EF2 release); the
// factory re-run's colour/render resets, the CI+0x70 / [0x76bb2c] drift and its R() draw (gated by
// the unidentified [0x75ad90] + [0x75ad94]); FUN_004202e0's re-stat (the spawn's stats are kept:
// they were made at the same ENEMYINFO default level); FUN_0046f650's render basis from +0x528;
// the GenerateDebris gate (the ASO VM, REVIVE.md §4 #3).
#include "cleria/brains/revive.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "cleria/actor.hpp"

namespace cleria::revive {
namespace {

constexpr Row kRows[] = {
    {36, 0x2f, false, 0x7c4, false, false, false},   // M430 Zeruena  FUN_004b3970
    {49, 0x3c, true, 0x7a4, true, false, false},     // M560 Karon    FUN_004a12a0
    {50, 0x3d, false, 0x7c4, false, false, false},   // M570 Zarue    FUN_004f4690
    {60, 0x47, false, 0x7c4, false, true, false},    // M670 Bazeraena FUN_004f24c0
    {71, 0x52, true, 0x7a4, false, false, true},     // M780 Zelkaron FUN_004ab280
};

std::uint8_t& byte_at(EnemyAi& a, int off) {
    return reinterpret_cast<std::uint8_t*>(a.blk.data())[off - EnemyAi::kBase];
}

}  // namespace

const Row* row(int ai_index) {
    for (const Row& r : kRows) if (r.ai_index == ai_index) return &r;
    return nullptr;
}

void sand_clamp(const Row& r, EnemyAi& a, float& y, float hp, bool grounded, bool in_4005) {
    if (r.ai_index == 50) {                                        // M570 only, the handler's end (0x4f4b7f)
        if (a.surf == 0x12 && a.water_y > y) { y = a.water_y; a.surf = 1; }   // [approx] run at the next tick's start
        return;
    }
    if (!r.skeleton || a.surf_ray != 0x12) return;
    if (r.m560 && in_4005) a.i(0x7a0) = 1;
    const float s = a.water_y;
    if (s - 1.5f > y) y = s - 1.5f;                                // 0x6bd4d4: never deeper than top - 1.5
    const float top = s + 0.65f;                                   // 0x6bd34c
    const bool emerge = a.i(0x7a0) == 1 || (hp < 1.0f && a.i(0x7a4) == 0);
    if (emerge && top > y) y += std::min(top - y, 0.06f);          // 0x6bd150
    else if (grounded && y > top) y += std::max(top - y, -0.06f);  // 0x6bdfcc
}

DeadPre dead_pre(const Row& r, Actor& A, const World& w) {
    EnemyAi& a = A.ai;
    DeadPre o;
    bool forced = false;
    if (r.m560 && w.in_4005 && w.flag179 == 1 && byte_at(a, 0x8a4) == 0) {   // 0x4a13c0
        forced = true; a.dead_t = 0; byte_at(a, 0x8a4) = 1;
    }
    if (a.dead_t == 0) {
        if (w.chimes || forced) {                                  // 0x4a13f0 / 0x4f46d1
            a.i(r.latch) = 1;
            A.flags130 ^= 9u;                                      // the drops (1) and death FX (8) come back
            if (r.m780 && w.flag265) *w.flag265 += 1;              // 0x4ab378 inc [0x76bd40]
        }
        if (r.m560 && w.in_4005) {
            if (w.flag180) *w.flag180 += 1;                        // DAT_0076bbec
            byte_at(a, 0x8a5) = 1;
            A.mover.vx = A.mover.vz = 0; A.mover.dx = A.mover.dz = 0;
            a.mode() = 0;
        }
    }
    if (a.dead_t > 0xfeu && a.i(r.latch) == 0) a.dead_t = 0xfe;    // 0x4a147a: never reaches removal
    // FUN_004780e0 (REVIVE.md §3.2)
    if (a.dead_t == 0) {
        o.first = true;
        a.dead_t = 1;
        a.hittable = false;                                        // +0x60 &= ~0x80
        A.mover.vx = A.mover.vz = 0; A.mover.dx = A.mover.dz = 0;
        a.hit_cut = true;                                          // +0x118 = 0
        a.i(0x868) = 1200;                                         // 0x4b0
    } else if (a.dead_t > 0xffu) {
        o.remove = true;                                           // FUN_004593c0 / KILLMARK
        a.hittable = false;
    } else {
        a.dead_t += 1;
        int al = 255 - 6 * int(a.dead_t);
        if (A.flags12c & 0x8000000u) al = 255 - int(a.dead_t);
        else if (A.flags12c & 0x40000000u) al = 255 - int(std::floor(float(a.dead_t) / 1.25f));
        if (al < 0) { al = 0; a.dead_t = 0x100; }
        a.alpha = float(al) * (1.0f / 255.0f);
    }
    return o;
}

DeadPost dead_post(const Row& r, Actor& A, const float ei_raw[16]) {
    EnemyAi& a = A.ai;
    DeadPost o;
    if (a.i(r.latch) == 0) {
        if (r.m780) { a.i(0x70c) = 0; a.action() = 1; a.mode() = 0; }   // 0x4ab3bc..0x4ab3d5
        a.alpha = 1.0f;                                            // the corpse stays opaque
        if (!r.skeleton) {                                         // 0x4f473b: flyer off, restores
            A.flyer = false;
            a.f(0x834) = a.f(0x7ac);
            a.f(0x814) = a.f(0x7b0);
            if (r.m670) a.f(0x818) = a.f(0x7b4);
        }
        const std::uint32_t n = std::min(std::uint32_t(a.i(0x868)), 600u) - 1u;   // cmova
        a.i(0x868) = int(n);
        if (n == 0 && (!r.skeleton || byte_at(a, 0x8a5) == 0)) {
            // REVIVE (0x4a14ef..0x4a15f0)
            const float h = a.heading;
            A.hp = A.max_hp;                                       // factory + FUN_004202e0: full HP
            A.flags12c |= 0x100010u;                               // ORed (0x459776)
            A.flags130 |= 0x2009u;                                 // case 22
            if (!r.skeleton) A.flyer = true;                       // +0x398 set (EI+0xb0 != 0)
            A.vert.grounded = true; A.vert.t = 0;                  // byte +0x388 = 1; FUN_0046f650 +0x32c..
            A.mover.vx = A.mover.vz = 0; A.mover.dx = A.mover.dz = 0;
            A.mover.k1 = {}; A.mover.k2 = {};
            a.alpha = 1.0f;
            a.dead_t = 0;                                          // FUN_0046f650 re-arms the death entry
            ai::init_block(a, {A.pos.x, A.pos.y + A.height, A.pos.z}, nullptr);   // FUN_004782f0
            for (int k = 0; k < 16; ++k) a.f(0x814 + 4 * k) = ei_raw ? ei_raw[k] : 0.0f;
            if (!r.skeleton) {
                a.f(0x7ac) = a.f(0x834); a.f(0x7b0) = a.f(0x814);
                if (r.m670) a.f(0x7b4) = a.f(0x818);
                a.i(0x878) = 0;
            }
            a.phase() = 4;
            a.hittable = false;                                    // +0x60 &= ~0x80 (after the request)
            a.heading = h;                                         // +0x314 = +0x308 = h (host: facing)
            if (r.skeleton) { a.i(0x7a0) = 1; a.i(0x7bc) = 0; }
            o.revived = true;
        }
    }
    if (r.m560 && byte_at(a, 0x8a5) == 1) o.corpse_mover = true;   // 0x4a15fa
    return o;
}

void exp_block(const Row&, Actor& A, bool chimes) {
    EnemyAi& a = A.ai;
    if (a.exp_base < 0) a.exp_base = A.exp_reward;
    A.exp_reward = chimes ? a.exp_base : 0.0f;                     // 0x4a18a1
}

}  // namespace cleria::revive

namespace cleria {

void enemy_handler_post(int ai_index, EnemyAi& a, int p) {
    // REVIVE.md §5.4 (M560 0x4a187a, M780 0x4ab785): an engage made by the action player while
    // buried (M560: or idle on sand) is cancelled in the same tick.
    const revive::Row* r = revive::row(ai_index);
    if (!r || !r->skeleton) return;
    if (p == 1 && a.phase() == 2 && (a.i(0x7a0) == 0 || (r->m560 && a.i(0x7a0) == 2))) a.phase() = 1;
}

}  // namespace cleria
