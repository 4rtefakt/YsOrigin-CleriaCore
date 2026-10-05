// M140 Twarth — brain FUN_0049f370 (handler FUN_0049fef0 = M100's minus the arena clamp,
// its dead path killing the owned effect; AI index 7). Spec: build/ai_spec/M140.md.
//
// A gauntlet brute. Engaged it walks one tick at a time (re-deciding every tick). Within 5 u,
// off cooldown, 5% a tick it starts a run (0x800 for 71 / 113 / 176 ticks): past the run
// section's Marker 1 it spawns the charge effect (EF2 268, attached 0.25 up — a cylinder hit
// r 1.5, re-hit 5) once, is Durable and turns at x1.5. When the run ends it forces the
// recovery section 0x139 (50 ticks), kills the effect and sets a cooldown of 350 / 275 / 170.
// Walking within 10 u, level, lockout clear: 2% a tick, the target within 45 deg, the
// gauntlet volley (section 7, stand 80, lockout 80 + 75 / 56 / 30). The jump helper's jumps
// end with form (b); the standard launch tail (K 0.1, push +0x7e0).
// [not modelled] the effect's particle-borne hit (EFFECTHIT.md); its attachment (re-placed on
// the actor every tick instead); the KILLFX 16-step fade, and the kill on the handler's dead
// path; the keep-out box restore (FUN_0042dc90).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m140(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    constexpr int kFx = 0x88c;                                     // the owned effect's slot / tag
    bool can = true;
    if (std::fabs(e.target.y - e.self.y) >= 3.0f) can = false;
    else if (!(std::fabs(e.target_floor - e.self.y) < 3.0f)) can = false;
    const int dp = -difficulty(e);                                 // d' = -d
    const bool E = e.diff_easy, N = !E && e.diff_index == 0;
    if (a.i(0x79c) != 0) a.i(0x79c) -= 1;
    auto killfx = [&] {
        if (a.i(kFx) != 0) { AiEffect k; k.kill_tag = kFx; out.effects.push_back(k); a.i(kFx) = 0; }
    };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0;   // only 32 bytes copied
            a.f(0x7ac) = a.f(0x814);
            a.i(0x79c) = 0;
            return out;
        case 1:
            killfx();
            ai::idle(a, p, e);
            return out;
        case 3:
            killfx();
            a.i(0x7c4) = 0;                                        // a stun cancels the charge
            return out;
        case 2: break;
        default: return out;
    }
    int S = e.section;
    if (a.dist() > 20.0f) { a.action() = 0x10; a.i(0x728) = 1; ai::disengage(a); }   // the tick continues
    if (a.action() == 0) {                                         // B: the decision
        if (a.i(0x728) != 100) {
            if (a.i(0x7c4) == 1) {                                 // the run ended
                out.section = 0x139; out.section_force = true;   // inlined: +0x994 = 0x139 at once
                S = 0x139;
                a.action() = 1; a.dur() = 50;
                a.cool() = 50 + (E ? 300 : N ? 225 : 120);
                a.i(0x7c4) = 0;
                killfx();
            } else {
                bool run = false;
                if (a.cool() == 0 && 5.0f > a.dist() && r15(e) % 1000u < 50u) {
                    a.action() = 0x800; a.i(0x728) = 0;
                    a.dur() = a.cool() = E ? 71 : N ? 113 : 176;
                    a.i(0x7c0) = 0; a.i(0x7c4) = 1;
                    run = true;
                }
                if (!run) { a.action() = 0x10; a.i(0x728) = 1; }
            }
        }
        a.i(0x7bc) = 0;
    }
    a.f(0x814) = a.f(0x7ac);
    bool launch_now = false;
    switch (a.i(0x728)) {
        case 100:
            if (S == 0x13a) { launch_now = true; break; }
            if (e.grounded) { a.action() = 0; a.i(0x728) = 0; }
            break;
        case 1:                                                    // the gauntlet volley
            if (10.0f > a.dist() && S != 7 && a.i(0x79c) == 0 && e.grounded && can &&
                r15(e) % 1000u < 20u) {
                const Vec3 to = a.v(0x734);
                const float an = float(std::atan2(double(to.z), double(to.x)));
                if (float(std::fabs(ai::angle_diff(a.heading, an))) < 0.785398f) {
                    out.section = 7; out.attack = true;            // gated; result ignored
                    a.action() = 1; a.dur() = 80;
                    a.i(0x79c) = 80 + (E ? 75 : N ? 56 : 30);
                    a.i(0x7bc) = 1;
                }
            }
            break;
        case 0:                                                    // charging
            if (S == 1 && e.aso_marker == 1) {
                if (a.i(0x7c0) == 0 || a.i(kFx) == 0) {
                    AiEffect fx;
                    fx.id = 268;                                   // enemy\m_140_attack01.eft
                    fx.pos = {e.self.x, e.self.y + 0.25f, e.self.z};
                    fx.tag = kFx;
                    fx.rtype = 2;
                    out.effects.push_back(fx);
                    a.i(kFx) = 1;
                    a.i(0x7c0) = 1;
                }
                out.set_durable = 1;
                a.f(0x814) = a.f(0x7ac) * 1.5f;
                a.cool() = (dp * 5 + 30) * 4;
            }
            break;
        default: break;
    }
    if (a.i(kFx) != 0) {                                           // the attachment (re-placed)
        AiEffect mv; mv.move_tag = kFx; mv.pos = {e.self.x, e.self.y + 0.25f, e.self.z};
        out.effects.push_back(mv);
    }
    if ((launch_now || S == 0x13a) && A && e.grounded && e.aso_marker == 1) {   // TAIL / LAUNCH
        launch(*A, a.f(0x7dc) * 0.1f, out);
        push1(*A, a.heading, a.f(0x7e0));
    }
    return out;
}

}  // namespace cleria::brains
