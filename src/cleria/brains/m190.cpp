// M190 Irebolg — brain FUN_0048f6d0 (handler FUN_00490240 = M100's minus the arena clamp,
// AI index 12). Spec: build/ai_spec/M190.md.
//
// A heavy brute. It re-decides every 20 ticks: run at the target from 4.5 u (horizontal),
// walk inside. Within 7 u, facing within +/-60 deg, grounded and not 2.5 off a grounded
// target, it attacks: within 4 u the leap (section 8, stand 95 / 115 on Easy), else the
// charge (section 7, 80 / 100); the cooldown is that + R%50, standing with the heading
// locked. On Hard and above, inside 4.5 u it first rolls 50% to hop back (section 301,
// 0.2 up, 0.5 back along its visible facing) and attacks 30 ticks later. Neither the attack
// choice nor its two follow-up ticks re-check range or facing, and no request is checked.
// Every tick in the run section it probes 1.49 ahead: a wall it can clear at +3.0 makes it
// hop (0.33) with a 0.15 forward kick.
// [not modelled] the M130-style marker launch tail
// (sections 0x13a/3 at Marker 1 — 0x13a needs FUN_00476200, which this cast's factory flags
// do not enable, and the section-3 case is likely unreachable: M190.md §6).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m190(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0: the tails too
    Actor* A = e.actor;
    const Vec3 to = a.v(0x734);
    const float dist = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);   // horizontal
    const bool can_atk = e.grounded && !(std::fabs(e.target.y - e.self.y) >= 2.5f && e.target_grounded);
    const float U = dist == 0 ? 0.0f : 1.0f / dist;
    const bool facing = dist != 0 &&
        !(0.5f > std::cos(a.heading) * to.x * U + std::sin(a.heading) * to.z * U);   // +/-60 deg
    int& sub = a.i(0x72c);
    int& step = a.i(0x730);

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.i(0x728) = 0; sub = 0; step = 0;
            break;
        case 1:
            ai::idle(a, p, e);
            break;
        case 2: {
            if (a.dist() > 20.0f) ai::disengage(a);              // the tick continues
            auto start = [&](int dur) {
                a.action() = 1;                                    // stand, heading locked
                a.dur() = dur;
                const int r = int(r15(e) % 50u);
                sub += 1;
                a.cool() = r + dur;
            };
            switch (sub) {
                case 0: sub = 0x14; step = 0; break;
                case 0x14:
                    if (a.dur() != 0) break;
                    switch (step) {
                        case 0:
                            if (dist > 7.0f || !facing || !can_atk) {
                                a.action() = dist < 4.5f ? 0x10 : 0x400;
                                a.steer() = 0;
                                a.dur() = 20;
                                break;
                            }
                            if (dist < 4.5f && difficulty(e) >= 1 && r15(e) % 100u < 50u) { step = 2; break; }
                            step += 1;
                            break;
                        case 1:
                            sub = dist <= 4.0f ? 0x6e : 0x64;
                            a.i(0x79c) = 0;
                            step = 0;
                            break;
                        case 2:                                    // hop back (no grounded test)
                            if (A) {
                                A->vert.t = 0.2f;
                                A->pos.y += 0.2f;
                                A->vert.grounded = false;
                                A->mover.k1.x -= std::cos(e.facing) * 0.5f;
                                A->mover.k1.z -= std::sin(e.facing) * 0.5f;
                            }
                            out.set_own_jump = 1;
                            out.section = 0x12d;                   // gated; result ignored
                            a.action() = 2; a.steer() = 0; a.dur() = 30;
                            step += 1;
                            break;
                        case 3: step = 1; break;
                        default: break;
                    }
                    break;
                case 0x64: case 0x6e:                              // one stop tick
                    a.action() = 2; a.steer() = 0; a.dur() = 1; sub += 1;
                    break;
                case 0x65:
                    if (a.dur() != 0) break;
                    out.section = 7; out.attack = true;            // the charge
                    start(e.diff_easy ? 100 : 80);
                    break;
                case 0x6f:
                    if (a.dur() != 0) break;
                    out.section = 8; out.attack = true;            // the leap
                    start(e.diff_easy ? 115 : 95);
                    break;
                case 0x66: case 0x70: {                            // the post-attack stand
                    const int c = a.cool();
                    if (a.dur() == 0) { a.action() = 1; a.dur() = c; }
                    if (c == 0 || (e.section == 4 && e.group_tag)) sub = a.i(0x79c);
                    break;
                }
                default: break;
            }
            break;
        }
        default: break;
    }

    // TAIL A (every phase): the step-up hop while in the run section.
    if (e.section == 1 && e.world && A) {
        const float r = e.radius > 0.5f ? e.radius + 0.5f : 1.0f;
        const Vec3 O = e.self;
        const Vec3 D{std::cos(a.heading) * r, 0, std::sin(a.heading) * r};
        if (e.world->ray(O, D).hit && !e.world->ray({O.x, O.y + a.f(0x828), O.z}, D).hit) {
            jump(*A, a.f(0x828) * 0.11f, e.jump_v0, out);
            const float k = a.f(0x818) * 5.0f;                     // not gated on grounded
            A->mover.k1.x += std::cos(a.heading) * k;
            A->mover.k1.z += std::sin(a.heading) * k;
        }
    }
    return out;
}

}  // namespace cleria::brains
