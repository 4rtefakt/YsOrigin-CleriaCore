// M610 Gunghogan — brain FUN_004ac940 (handler FUN_004ad690 = M100's minus the arena clamp, AI index
// 54). Spec: build/ai_spec/M610.md (§3.6: the block diff against M190, "a greater form of Irebolg").
//
// It always runs at the target, re-deciding every 20 ticks. Within 9 u (horizontal), facing within
// +-60 deg, grounded and within 2.5 u of the target's height it attacks: the leap (section 8, stand
// 200 - 15d) when the target is 1.5 above it or 1.5 < dist < 5, else the charge — section 7 at x2 run
// speed (0.12 a tick) straight along its locked heading for 77 ticks, arming a contact sphere every
// tick while the section's Marker is 1 (FUN_00477f00: r 1.0, 0.5 ahead, 0.55 down, rtype 2, a fresh
// serial each tick) with one push of 0.7 + 0.15d. A tagged one inside 5 u hops back first (section
// 301); on Hard and up, inside 1.5 u, 50%. The cooldowns: 77 + 20 + R%20 (+40 on Easy) / 200 - 15d +
// 20 + R%20. A grounded one in its walk / run section hops a wall it can clear at +3.0 (0.33, kick 0.15).
// [not modelled] the keep-out box restore (FUN_0042dc90); the dead 0x13a launch tail (no 0x13a section
// in M_610.ASO, no jump flag).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m610(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4aca52: the tails do not run
    Actor* A = e.actor;
    const int d = difficulty(e);
    const Vec3 to = a.v(0x734);
    const float dsq = to.x * to.x + 0.0f + to.z * to.z;
    const float dist = std::sqrt(dsq);                             // horizontal, the previous tick's
    const bool can_atk = !(std::fabs(e.target.y - e.self.y) >= 2.5f) && e.grounded;   // 0x6bd624
    const bool tagged = e.group_tag;
    bool facing;
    {
        float hx = std::cos(a.heading), hz = std::sin(a.heading);
        const float hl = std::sqrt(hx * hx + 0.0f + hz * hz);
        hx = hl == 0.0f ? 0.0f : hx / hl; hz = hl == 0.0f ? 0.0f : hz / hl;
        const float ux = dist == 0.0f ? 0.0f : to.x / dist, uz = dist == 0.0f ? 0.0f : to.z / dist;
        const float dot = (0.0f + hx * ux) + uz * hz;
        facing = !(0.5f > dot);                                    // 0x6bd304: +-60 deg (NaN passes)
    }
    int& sub = a.i(0x72c);
    int& step = a.i(0x730);
    std::uint8_t& latch = reinterpret_cast<std::uint8_t*>(a.blk.data())[0x8a4 - EnemyAi::kBase];

    switch (a.phase()) {
        case 0:                                                    // INIT (0x4ad2a9)
            ai::init(a, p, e);
            a.f(0x7ac) = a.f(0x820);                               // the run multiplier (2.0)
            sub = 0; step = 0;
            break;
        case 1:                                                    // 0x4ad280
            ai::idle(a, p, e);
            sub = 0; step = 0;
            a.f(0x820) = a.f(0x7ac);
            break;
        case 2: {
            if (a.dist() > 20.0f) ai::disengage(a);                // the tick continues
            if (std::uint32_t(sub) > 0x70u) break;
            switch (sub) {
                case 0:
                    step = 0; sub = 0x14;
                    a.f(0x820) = a.f(0x7ac);
                    break;
                case 0x14:
                    if (a.dur() != 0) break;
                    switch (step) {
                        case 0:
                            if (tagged && 5.0f > dist) { step = 2; break; }     // 0x6bd868
                            if (dist > 9.0f || !facing || !can_atk) {           // 0x6bd900
                                a.action() = 0x400; a.steer() = 0; a.dur() = 20;
                                break;
                            }
                            if (1.5f > dist && d >= 1 && r15(e) % 100u < 50u) { step = 2; break; }
                            step += 1;
                            break;
                        case 1:                                    // choose (no re-check)
                            if (e.target.y > e.self.y + 1.5f || (5.0f > dist && dist > 1.5f)) sub = 0x6e;
                            else sub = 0x64;
                            a.i(0x79c) = 0; step = 0;
                            break;
                        case 2:                                    // hop back (M190's)
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
                case 0x64:                                         // charge: a stop tick, x2 run
                    a.action() = 2; a.steer() = 0; a.dur() = 1;
                    sub = 0x65;
                    a.f(0x820) = a.f(0x7ac) + a.f(0x7ac);
                    break;
                case 0x65: {
                    if (a.dur() != 0) break;
                    out.section = 7; out.attack = true;            // gated; result ignored
                    a.dur() = 77;
                    a.action() = 0x800;                            // run, no distance stop
                    a.steer() = 1;                                 // the own heading: no turning
                    const int r = int(r15(e) % 20u);
                    const int ez = e.diff_easy ? 40 : 0;           // DAT_0076bb58 == 1 (easy or very easy)
                    latch = 0;
                    sub += 1;
                    a.cool() = a.dur() + 20 + r + ez;
                    break;
                }
                case 0x66: {                                       // the charge
                    const int c = a.cool();
                    if (a.dur() == 0) { a.action() = 1; a.dur() = c; }
                    if (c == 0) { sub = a.i(0x79c); break; }
                    if (e.section == 4 && tagged) { sub = a.i(0x79c); break; }
                    if (e.aso_marker == 1) {
                        AiHitbox h;                                // FUN_00477f00
                        h.shape = 0; h.radius = 1.0f; h.fwd = 0.5f; h.angle_deg = 0.0f; h.voff = -0.55f;
                        h.half_h = 0; h.life = 2; h.rehit = 200; h.one_shot = 0; h.rtype = 2; h.status = 0;
                        h.power = 0.0f; h.knock = 0.0f;
                        h.new_serial = true;
                        out.hitboxes.push_back(h);
                        if (latch == 0) {                          // FUN_0047aa00: one push
                            if (A) push1(*A, a.heading, float(d) * 0.15f + 0.7f);
                            latch = 1;
                        }
                    }
                    break;
                }
                case 0x6e:                                         // leap: a stop tick
                    a.action() = 2; a.steer() = 0; a.dur() = 1; sub = 0x6f;
                    break;
                case 0x6f: {
                    if (a.dur() != 0) break;
                    out.section = 8; out.attack = true;            // gated; result ignored
                    a.action() = 1;
                    a.dur() = 200 - 15 * d;
                    const int r = int(r15(e) % 20u);
                    sub += 1;
                    a.cool() = a.dur() + 20 + r;
                    break;
                }
                case 0x70: {                                       // the post-leap stand
                    const int c = a.cool();
                    if (a.dur() == 0) { a.action() = 1; a.dur() = c; }
                    if (c == 0 || (e.section == 4 && tagged)) sub = a.i(0x79c);
                    break;
                }
                default: break;
            }
            break;
        }
        default: break;
    }

    // TAIL A (0x4ad32d): the step-up hop, grounded in the walk / run section.
    if (e.grounded && (e.section == 0 || e.section == 1) && e.world && A) {
        const float r = e.radius > 0.5f ? e.radius + 0.5f : 1.0f;
        const Vec3 O = e.self;
        const Vec3 D{std::cos(a.heading) * r, 0, std::sin(a.heading) * r};
        if (e.world->ray(O, D).hit && !e.world->ray({O.x, O.y + a.f(0x828), O.z}, D).hit) {
            jump(*A, a.f(0x828) * 0.11f, e.jump_v0, out);         // 0x6bd1a4
            const float k = a.f(0x818) * 5.0f;
            A->mover.k1.x += std::cos(a.heading) * k;
            A->mover.k1.z += std::sin(a.heading) * k;
        }
    }
    return out;
}

}  // namespace cleria::brains
