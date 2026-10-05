// M300 Hubamobre — brain FUN_00491060 (handler FUN_00491910 = M100's minus the arena clamp,
// AI index 23). Spec: build/ai_spec/M300.md.
//
// A fire-breather. It runs at the target (0x400 re-issued every tick, steer never written).
// Within 2.25 u, grounded, level and facing within 45 deg, off cooldown: a body slam
// (section 8, stand 50 / 65 on Easy). Within 5.25 u (else): 15% a tick, facing within 60 deg:
// the fire fan (section 7, 110 / 125; on Hard+ action 2 so it keeps turning): every 4th
// marker-1 tick a bullet of effect 302 at heading - 40 + 2n deg, 0.75 ahead and 0.35 down,
// speed 0.08, accel -0.0008. Cooldown = hold + 20 + R%20. After an attack it stands 40 ticks
// at double turn rate. Durable in sections 7/8. Requests are gated and unchecked.
// The jump helper's jumps: sub-state 100 and the section-0x13a launch tail (K 0.11).
// [not modelled] the keep-out box restore (FUN_0042dc90); bullet block field +0x150 (-2.0).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m300(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    const bool can_atk = !(e.target_grounded && std::fabs(e.target.y - e.self.y) >= 2.5f);

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.f(0x7ac) = a.f(0x814);                               // base turn rate
            return out;
        case 1:
            ai::idle(a, p, e);
            return out;
        case 2: break;
        default: return out;
    }
    const float dist = a.dist();
    const int S = e.section;
    if (dist > 20.0f) ai::disengage(a);                           // the tick continues
    if (e.diff_index >= 0 && (S == 7 || S == 8)) out.set_durable = 1;
    int& sub = a.i(0x728);
    if (a.action() == 0) {                                         // C: re-issue
        if (sub != 100) {
            sub = 0;
            if (a.i(0x7bc) == 1) { a.action() = 2; a.dur() = 40; a.f(0x814) = a.f(0x7ac) + a.f(0x7ac); }
            else { a.action() = 0x400; a.f(0x814) = a.f(0x7ac); }
        }
        a.i(0x7bc) = 0;
    }
    const Vec3 to = a.v(0x734);
    const float a_T = float(std::atan2(double(to.z), double(to.x)));
    auto adiff = [&] { return float(std::fabs(ai::angle_diff(a.heading, a_T))); };
    switch (sub) {
        case 0:
            if (dist < 2.25f && S != 7 && S != 8) {                // body slam
                if (a.cool() != 0 || !e.grounded || !can_atk) break;
                if (adiff() >= 0.785398f) break;
                out.section = 8; out.attack = true;                // gated; unchecked
                a.action() = 1;
                a.dur() = e.diff_easy ? 65 : 50;
                a.cool() = a.dur() + 20 + int(r15(e) % 20u);
                a.i(0x7bc) = 1;
                break;
            }
            if (!(dist < 5.25f)) break;                            // the fire fan
            if (S == 7 || S == 8 || a.cool() != 0 || !e.grounded || !can_atk) break;
            if (r15(e) % 1000u >= 150u) break;
            if (adiff() >= 1.0472f) break;
            out.section = 7; out.attack = true;
            a.action() = e.diff_index >= 1 ? 2 : 1;
            a.dur() = e.diff_easy ? 125 : 110;
            a.cool() = a.dur() + 20 + int(r15(e) % 20u);
            a.i(0x7bc) = 1; a.i(0x7c0) = 0; sub = 1;
            break;
        case 1: {
            if (e.aso_marker != 1) break;
            const std::uint32_t n = std::uint32_t(a.i(0x7c0));
            if ((n & 3u) == 0) {
                const float nf = float(n);
                const float th = (nf + nf) * 0.0174533f + (a.heading - 0.698132f);
                const float c = float(std::cos(double(th))), s = float(std::sin(double(th)));
                AiBullet b;
                b.effect = 302;                                    // enemy\m_300_fire01.eft
                b.pos = {e.self.x + c * 0.75f, (e.self.y - 0.35f) + 0.0f, e.self.z + s * 0.75f};
                b.dir = {c, 0, s};
                b.speed = 0.08f; b.accel = -0.0008f; b.gravity = 0; b.scale = 1.0f;
                out.bullets.push_back(b);
            }
            a.i(0x7c0) = int(n + 1);
            break;
        }
        case 100:                                                  // the jump helper's state
            if ((S != 0x13a || std::uint32_t(e.aso_marker) >= 2u) && e.grounded) { a.action() = 0; sub = 0; }
            break;
        default: break;
    }
    // TAIL: the jump helper's launch, K 0.11, push +0x7e0 (no fallback).
    if (Actor* A = e.actor; A && launch_gate(e)) {
        launch(*A, a.f(0x7dc) * 0.11f, out);
        push1(*A, a.heading, a.f(0x7e0));
    }
    return out;
}

}  // namespace cleria::brains
