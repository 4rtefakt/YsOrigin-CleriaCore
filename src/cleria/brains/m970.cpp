// M970 Vagully — handler FUN_004cc070 (AI index 90) dispatches on phase: 4 -> brain B
// FUN_004ca830 (a swarm bat owned by an M460 leader), anything else -> brain A
// FUN_004ca1a0 (a lone, room-placed bat). Spec: build/ai_spec/M970.md.
//
// Brain A: always runs (0x800) at 0.04 x 2.0. Under 2 u it steers away; from 2 to 4 u, with
// no flutter timer, it re-rolls its steer vector every tick (+/-135 deg random walk, 1 in 5
// "away"); beyond 4 it chases. Every 15th tick, off cooldown and level with the target
// (|dy| < 1, not flying high): 30% to request section 7 if within 6 u and +/-54 deg. Every
// (e+16)*15 ticks (e = -M100's d) outside section 7 it flips a coin between hover 1.0 and
// flying high at 3.0.
// [not ported] brain B — phase 4 only comes from the M460 boss's spawner, which is not
// ported yet, so a phase-4 bat does nothing here.
// [not modelled] the keep-out box restore (FUN_0042dc90); the hover controller FUN_00476960
// (+0x834 is written for it; the view keeps enemies on the floor); H1/H3/H4 handler extras
// (the phase-4 room-count bit, the dust-puff clear, the phase-4 box clamp).
#include <cmath>
#include <cstring>

#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

// unit(w) = bitcast<float>((w & 0x7fffff) | 0x3f800000) - 1.0f: the whole word, in [0, 1).
float unit(std::uint32_t w) {
    const std::uint32_t bits = (w & 0x7fffffu) | 0x3f800000u;
    float f;
    std::memcpy(&f, &bits, 4);
    return f - 1.0f;
}

AiOut brain_a(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0: nothing, not even the tail
    const int ev = -difficulty(e);
    a.i(0x7a0) += 1;
    if (a.i(0x7a8) != 0) a.i(0x7a8) -= 1;
    const bool level = std::fabs(double(e.self.y) - double(e.target.y)) < 1.0 && a.i(0x79c) == 0;

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.i(0x79c) = 0; a.i(0x7a0) = 0; a.i(0x7a4) = 0; a.i(0x7a8) = 0;
            a.f(0x7ac) = a.f(0x834);                               // EI hover altitude (1.0)
            a.i(0x70c) = 0;
            break;
        case 1:
            ai::idle(a, p, e);
            break;
        case 2: {
            if (a.dist() > 20.0f) { a.steer() = 0; ai::disengage(a); break; }
            a.action() = 0x800;                                    // always run
            if (e.section == 7) {
                a.steer() = 0;
                a.action() = e.turnable ? 0x800 : 1;
                break;                                             // no attack roll
            }
            if (a.steer() == 0 && a.dist() < 2.0f) {               // too close: back off
                a.steer() = 3;
                a.i(0x7a8) = int(r15(e) % 30u) + 15;
            } else if (a.dist() > 4.0f) {
                a.steer() = 0; a.i(0x7a8) = 0;
            } else if (a.i(0x7a8) == 0) {                          // flutter
                const std::uint32_t k = r15(e) % 5u;
                a.steer() = k != 0 ? 5 : 3;
                if (k != 0) {
                    const float a0 = float(std::atan2(double(a.f(0x750)), double(a.f(0x748))));
                    const float u = unit(e.rng());
                    const float ang = u * 4.712389f - 2.3561945f + a0;
                    a.f(0x748) = std::cos(ang); a.f(0x74c) = 0; a.f(0x750) = std::sin(ang);
                }
            }
            if (a.cool() == 0 && level && std::uint32_t(a.i(0x87c)) % 15u == 0) {
                if (r15(e) % 1000u < 300u && a.dist() < 6.0f) {
                    const Vec3 to = a.v(0x734);
                    const float ang = float(std::atan2(double(to.z), double(to.x)));
                    if (std::fabs(ai::angle_diff(a.heading, ang)) < 0.9424778) {
                        out.section = 7;                           // gated request
                        out.attack = true;
                    }
                }
            }
            break;
        }
        default:
            break;
    }
    // Tail, every phase.
    if (e.section != 7 && std::uint32_t(a.i(0x7a0)) > std::uint32_t((ev + 16) * 15)) {
        a.i(0x79c) = int(r15(e) & 1u);                             // fly high?
        a.i(0x7a0) = 0;
    }
    a.f(0x834) = a.i(0x79c) != 0 ? 3.0f : a.f(0x7ac);
    return out;
}

}  // namespace

AiOut m970(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    if (a.phase() == 4) return {};                                 // brain B: not ported
    return brain_a(a, p, e);
}

}  // namespace cleria::brains
