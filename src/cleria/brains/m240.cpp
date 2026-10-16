// M240 Bordisch — brain FUN_00490530 (handler FUN_00490d70 = M100's minus the arena clamp,
// AI index 17). Spec: build/ai_spec/M240.md.
//
// A stun-immune heavy. Beyond 3.5 u (horizontal) it runs at the target in 15-tick plans,
// standing and turning instead when a probe finds no floor 2.0 below its origin 1.805
// ahead. Inside 3.5 u (unless the target is grounded 2+ off) it picks by HP: >= 60% the
// hop-smash (section 7, 4-tick wind-up), 33..60% the leap slam (section 8, 10-tick wind-up,
// STR x1.15 — restored only by the section-7 wind-up or phase 1), < 33% section 7 then 8.
// The request is gated and unchecked; it then stands heading-locked for the cooldown
// R%40 + (3 - d)*20 + 63 (the rage section 7: exactly 63). While the attack plays it pushes
// 1/n along its visible facing into knock channel 2 (n = +0x87c, 1 at the request): section 7
// at markers 2..4, section 8 at marker 3.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m240(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const Vec3 to = a.v(0x734);
    const float hd = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    const bool can_atk = !(std::fabs(e.target.y - e.self.y) >= 2.0f && e.target_grounded);
    int& sub = a.i(0x72c);
    int& step = a.i(0x730);
    auto restore_str = [&] { if (A) A->atk = a.f(0x7ac); };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.f(0x7ac) = A ? A->atk : 0.0f;                        // STR snapshot
            sub = 0; a.i(0x728) = 0; step = 0;
            return out;
        case 1:
            ai::idle(a, p, e);
            restore_str();
            sub = 0; a.i(0x728) = 0; step = 0;
            return out;
        case 2: break;
        default: return out;
    }
    if (a.dist() > 20.0f) ai::disengage(a);                       // the tick continues
    auto cool = [&] { return int(r15(e) % 40u) + (3 - difficulty(e)) * 20 + a.dur(); };
    auto impulse = [&] {
        if (!A) return;
        const float k = 1.0f / float(std::uint32_t(a.i(0x87c)));
        A->mover.k2.x += std::cos(e.facing) * k;
        A->mover.k2.z += std::sin(e.facing) * k;
    };
    auto recover = [&](int sec, bool marker_ok) {
        const int c = a.cool();
        if (a.dur() == 0) { a.action() = 1; a.dur() = c; }
        if (c == 0) { sub = a.i(0x79c); return; }
        if (e.section == sec && marker_ok) impulse();
    };
    switch (sub) {
        case 0: sub = 20; step = 0; break;
        case 20:
            if (a.dur() != 0) break;
            switch (step) {
                case 0: {
                    const float L = e.radius > 0.5f ? e.radius + 0.5f : 1.0f;
                    const Vec3 P{e.self.x + std::cos(a.heading) * L, e.self.y, e.self.z + std::sin(a.heading) * L};
                    const bool floor = !e.world || e.world->ray(P, {0, -2.0f, 0}).hit;
                    if (hd > 3.5f) {
                        a.action() = floor ? 0x400 : 2;
                        a.steer() = 0;
                        a.dur() = 15;
                        break;
                    }
                    if (can_atk) step += 1;
                    break;
                }
                case 1: {
                    const float f = e.hp / e.max_hp;
                    if (f < 0.33f) { sub = 100; a.i(0x79c) = 20; a.i(0x7a0) = 1; step += 1; break; }
                    a.i(0x7a0) = 0;
                    sub = f < 0.6f ? 110 : 100;
                    a.i(0x79c) = 0;
                    break;
                }
                case 2: a.i(0x7a0) = 0; sub = 110; a.i(0x79c) = 0; break;
                default: break;
            }
            break;
        case 100:                                                  // section-7 wind-up
            a.action() = 2; a.steer() = 0; a.dur() = 4;
            restore_str();
            sub = 101;
            break;
        case 101:
            if (a.dur() != 0) break;
            out.section = 7; out.attack = true;                    // gated; result ignored
            a.action() = 1; a.dur() = 63;
            a.cool() = a.i(0x7a0) != 0 ? 63 : cool();
            sub = 102; a.i(0x87c) = 1;
            break;
        case 102: recover(7, std::uint32_t(e.aso_marker - 2) <= 2u); break;
        case 110:                                                  // section-8 wind-up
            a.action() = 2; a.steer() = 0; a.dur() = 10;
            if (A) A->atk = a.f(0x7ac) * 1.15f;
            sub = 111;
            break;
        case 111:
            if (a.dur() != 0) break;
            out.section = 8; out.attack = true;
            a.action() = 1; a.dur() = 63;
            a.cool() = cool();
            sub = 112; a.i(0x87c) = 1;
            break;
        case 112: recover(8, e.aso_marker == 3); break;
        default: break;
    }
    return out;
}

}  // namespace cleria::brains
