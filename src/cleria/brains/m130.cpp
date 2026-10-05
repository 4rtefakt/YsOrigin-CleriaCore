// M130 Selnarg — brain FUN_0047bae0 (handler FUN_0047c2e0 = M100's minus the arena
// clamp, AI index 6). Spec: build/ai_spec/M130.md.
//
// The sub-state lives at +0x72c (not M100's +0x728). Each plan is one roll: R%100 < 30
// -> shield charge (sub 10: hold 3.0..3.8 planar units, backing off at most 4 bursts,
// face within +/-60 deg for up to 30 ticks, ASO section 7), else sword (sub 20: close to
// <= 2.5, face for up to 10 ticks, section 8). A strike holds action 1 for 130/105
// (charge) or 100/80 (sword) by difficulty index >= 1, +20 on Easy; the cooldown is that
// + 10 + R%30. [not modelled] the post-switch jump launch (section 0x13a + marker 1 +
// grounded): it only fires after the shared jump helper FUN_00476200 (reached from the
// action dispatcher for +0x12c & 0x400000 casts) enters section 0x13a, which the
// view does not run yet — so Selnarg never gap-jumps or climbs here.
#include <cmath>

#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m130(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0
    const Vec3 to = a.v(0x734);
    const float D = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);   // planar
    const Vec3 n = D == 0 ? Vec3{} : Vec3{to.x / D, 0, to.z / D};
    const bool F = !(0.5f > std::cos(a.heading) * n.x + std::sin(a.heading) * n.z);  // +/-60 deg

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.i(0x7bc) = 0; a.i(0x730) = 0; a.i(0x72c) = 0; a.i(0x728) = 0;
            return out;
        case 1:
            ai::idle(a, p, e);
            a.i(0x730) = 0; a.i(0x72c) = 0; a.i(0x728) = 0;
            return out;
        case 2: break;
        default: return out;
    }

    if (a.dist() > 20.0f) ai::disengage(a);                       // the switch still runs
    int& sub = a.i(0x72c);
    auto strike = [&](int base_normal, int base_hard) {            // 0x47c007
        a.action() = 1;
        const int dur = (e.diff_index >= 1 ? base_hard : base_normal) + (e.diff_easy ? 20 : 0);
        a.dur() = dur;
        const int r = int(r15(e) % 30u);
        sub += 1;
        a.cool() = dur + 10 + r;
    };
    switch (sub) {
        case 0:
            sub = (r15(e) % 100u >= 30u) ? 20 : 10;               // 70% sword, 30% charge
            break;
        case 10:                                                   // charge: get to 3.0..3.8
            if (a.dur() != 0) break;
            if (D < 3.0f && std::uint32_t(a.i(0x7bc)) < 4u) {
                a.action() = 0x400; a.steer() = 3; a.dur() = 10;   // back away
                a.i(0x7bc) += 1;
            } else if (D > 3.8f) {
                a.action() = 0x400; a.steer() = 0; a.dur() = 10;   // approach
            } else {
                sub = 100; a.i(0x79c) = 0; a.i(0x7bc) = 0;
            }
            break;
        case 20:                                                   // sword: close to <= 2.5
            if (a.dur() != 0) break;
            if (D > 2.5f) {
                a.action() = 0x400; a.steer() = 0; a.dur() = 10;
            } else {
                sub = 0x6e + (F ? 1 : 0);
                a.i(0x79c) = 0;
            }
            break;
        case 100:                                                  // charge: face it
            if (!F) { a.action() = 2; a.steer() = 0; a.dur() = 30; }
            sub = 101;
            break;
        case 101:
            if (a.dur() != 0) break;
            out.section = 7;                                       // shield charge
            out.attack = true;
            strike(130, 105);
            break;
        case 102: case 112:                                        // recovery
            if (a.cool() != 0) break;
            sub = a.i(0x79c);
            break;
        case 110:                                                  // sword: face it
            if (!F) { a.action() = 2; a.steer() = 0; a.dur() = 10; }
            sub = 111;
            break;
        case 111:
            if (a.dur() != 0) break;
            out.section = 8;                                       // sword swing
            out.attack = true;
            strike(100, 80);
            break;
        default: break;
    }
    return out;
}

}  // namespace cleria::brains
