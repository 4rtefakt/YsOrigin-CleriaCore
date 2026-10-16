// M270 Moz — brain FUN_004a1940 (handler FUN_004a1e30 = M100's minus the arena clamp,
// AI index 20). Spec: build/ai_spec/M270.md.
//
// A small charger with no attack section: engaged it runs (0x800) for 31..120 ticks at a
// time, 8% of picks standing and turning instead, straight at the target at 0.05 rad/tick,
// so it charges in wide arcs and runs through you. While its run section 1 plays it arms its
// own contact sphere every 8th tick (r 0.45, 0.5 ahead, life 8, re-hit 16, knock 1, its spawn
// serial) unless the player is in section 0x10b. Every phase: in the hit reaction (section 4)
// it stands heading-locked for 30 ticks. It sets +0x12c |= 0x71c802 itself (jump-capable but
// 0x100000 kept: hops and climbs, never gap jumps); its launch tail is the standard one.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m270(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            if (A) A->flags12c |= 0x71c802u;
            a.i(0x7a4) = 0;
            break;
        case 1:
            ai::idle(a, p, e);
            a.i(0x79c) = 0; a.i(0x7a0) = 0;
            if (A) A->flags12c |= 0x71c802u;
            break;
        case 2:
            if (a.action() == 0) {
                a.action() = 0x800;
                a.dur() = int(r15(e) % 90u) + 30;
                if (r15(e) % 1000u < 80u && a.dist() > 0.0f) { a.action() = 2; a.dur() = int(r15(e) % 90u) + 30; }
            }
            if (a.dist() > 20.0f) ai::disengage(a);               // the tick continues
            break;
        default: break;
    }
    // COMMON TAIL (every phase).
    const int S = e.section;
    if (a.i(0x728) == 100 && S != 0x13a && e.grounded) { a.action() = 0; a.i(0x728) = 0; }   // form (b)
    if (S == 4) { a.action() = 1; a.dur() = 30; }                  // the hurt hold
    if (A && launch_gate(e)) {                                     // K 0.1, push +0x7e0 (always 0)
        launch(*A, a.f(0x7dc) * 0.1f, out);
        push1(*A, a.heading, a.f(0x7e0));
    }
    const std::uint32_t n = std::uint32_t(++a.i(0x7a4));
    if (S == 1 && (n & 7u) == 0 && e.target_section != 0x10b) {    // the contact sphere
        AiHitbox h;
        h.shape = 0; h.radius = 0.45f; h.fwd = 0.5f; h.angle_deg = 0; h.voff = 0; h.half_h = 0;
        h.life = 8; h.rehit = 16; h.one_shot = 0; h.rtype = 0; h.status = 0; h.power = 0; h.knock = 1.0f;
        out.hitboxes.push_back(h);
    }
    return out;
}

}  // namespace cleria::brains
