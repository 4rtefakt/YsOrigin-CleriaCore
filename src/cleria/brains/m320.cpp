// M320 Pikkarton — brain FUN_004e9d00 (handler FUN_004ea450 = M910's with this brain, AI index 25).
// Spec: build/ai_spec/M320.md (thresholds re-read: 0x6bd85c 4.75, 0x6bd7c0 4.0, 0x6bd3a0 pi/4, 0x6bd994 20,
// 0x6bd210 0.2, 0x6bd19c 0.1).
//
// It runs at its target (action 0x400, re-issued every tick: no dur) and has two ASO-driven jumping attacks,
// rolled only on the ground, at the target's height (|dy| < 0.5 or the target airborne), inside +/-45 deg:
// section 7, hop and dash (inside 4.75 u, 101 per mille a tick) and section 8, a higher hop with a landing puff
// (inside 4.0 u, 61 per mille). On Hard / Nightmare it cannot be staggered out of either (Durable forced) and
// turns a fifth as fast during section 7. The "guard" sub-state 1 (stand until the parent +0x854 dies) is in the
// bytes but nothing arms it (M320.md §3.4).
// [not modelled] the keep-out box restore (FUN_0042dc90 over 0x76a2b4).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m320(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0
    // 0x4e9e14..0x4e9e61: a grounded target half a unit above or below = no attack.
    const bool vok = !(e.target_grounded && std::fabs(e.target.y - e.self.y) >= 0.5f);

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            for (int o = 0x834; o <= 0x850; o += 4) a.f(o) = 0;    // only EI+0x90..+0xaf are copied (32 bytes)
            a.f(0x7b8) = a.f(0x814);                               // the base turn rate (0.115)
            return out;
        case 1:
            ai::idle(a, p, e);
            return out;
        case 2:
            break;
        default:
            return out;
    }

    if (a.dist() > 20.0f) ai::disengage(a);                        // not an early out
    a.f(0x814) = a.f(0x7b8);
    const int sec = e.section;
    if (e.diff_index >= 1) {                                       // Hard / Nightmare
        if (sec == 7) a.f(0x814) = a.f(0x7b8) * 0.2f;
        if (sec == 7 || sec == 8) out.set_durable = 1;
    }
    if (a.action() == 0) {
        if (a.i(0x728) != 100) {
            if (a.i(0x7a8) == 1) { a.action() = 2; a.i(0x728) = 1; }       // the guard (never armed)
            else { a.action() = 0x400; a.i(0x728) = 0; }                   // run at the target; no dur
        }
        a.i(0x7bc) = 0;
    }

    auto in_cone = [&] {
        const Vec3 to = a.v(0x734);
        const float ang = float(std::atan2(double(to.z), double(to.x)));
        return 0.785398185 > std::fabs(ai::angle_diff(a.heading, ang));
    };
    switch (a.i(0x728)) {
        case 0:
            // Attack A: section 7.
            if (4.75f > a.dist() && sec != 7 && sec != 8 && a.cool() == 0 && e.grounded && vok && in_cone() &&
                r15(e) % 1000u <= 100u) {
                out.section = 7;
                out.attack = true;
                a.action() = e.diff_index >= 1 ? 2 : 1;
                a.dur() = 55 + (e.diff_index <= -1 ? 30 : e.diff_index == 0 ? 15 : 0);
                a.cool() = a.dur() + 5 + int(r15(e) % 15u);
            }
            // Attack B: section 8, tested in the same tick (A's cooldown blocks it).
            if (4.0f > a.dist() && sec != 7 && sec != 8 && a.cool() == 0 && e.grounded && vok && in_cone() &&
                r15(e) % 1000u <= 60u) {
                out.section = 8;
                out.attack = true;
                a.dur() = 70;
                a.action() = 1;
                a.cool() = a.dur() + 5 + int(r15(e) % 15u);
            }
            break;
        case 1: {                                                  // 0x4e9f81: the guard
            const Actor* par = e.world ? e.world->find(a.i(0x854)) : nullptr;
            if (!par || 1.0f > par->hp) { a.i(0x7a8) = 0; a.action() = 0; }
            break;
        }
        case 100:                                                  // set by the jump helper
            if (sec != 0x13a && e.grounded) { a.action() = 0; a.i(0x728) = 0; }
            break;
        default:
            break;
    }

    // Section 0x13a, the jump helper's jump: at Marker 1 on the ground the launch (K 0.1) and the helper's own
    // forward push +0x7e0 along the heading.
    if (sec == 0x13a && e.actor && e.grounded && e.aso_marker == 1) {
        launch(*e.actor, a.f(0x7dc) * 0.1f, out);
        push1(*e.actor, a.heading, a.f(0x7e0));
    }
    return out;
}

}  // namespace cleria::brains
