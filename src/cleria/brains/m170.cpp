// M170 Vilvan — brain FUN_0048a6c0 (handler FUN_0048b170 = M100's minus the arena clamp,
// AI index 10). Spec: build/ai_spec/M170.md.
//
// It circles the player. Engaged, it sets action -1 (the action player then does nothing for
// it) and each orbit tick stops, steers and turns itself at the full rate, plays the walk
// section with zero mover speed, and adds an impulse of magnitude exactly 1/60 to knock
// channel 2: a sidestep at heading +/-1.5 rad blended with a push out (inside 4 u) or pull in
// (beyond 4 u). The mover damps channel 2 by 0.325 a tick, so it drifts at ~0.0247 round a
// ring of radius ~4. After 100 orbit ticks it may switch side (2%/tick). Within 4.5 u, facing
// within 45 deg: max(1, 4-dist)*3% a tick to strike (section 7, hold 80, cooldown 130..159
// or 100..129 above Normal).
// Dead for this cast (kept out):
// the 0x13a jump block (no section 314 in M_170.ASO) and the unreachable FUN_004773f0 hop.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m170(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // first, before the snapshot
    bool can_atk = true;
    if (e.target_grounded && std::fabs(e.target.y - e.self.y) >= 2.5f) can_atk = false;
    const int d = difficulty(e);
    const int S = e.section;
    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0;   // only 32 bytes copied
            return out;
        case 1:
            ai::idle(a, p, e);
            return out;
        case 2: break;
        default: return out;
    }
    const float dist = a.dist();
    if (dist > 20.0f) ai::disengage(a);                           // the tick continues
    if (a.action() == 0) {
        if (a.i(0x7bc) != 1) {
            a.action() = -1;
            a.i(0x728) = 0;
            a.i(0x7a0) = int(r15(e) & 1u);                         // the side
            ai::stop(a, 0);
        }
        a.i(0x7bc) = 0;
    }
    bool to_tail = a.i(0x728) != 0 || a.action() != -1;
    if (!to_tail) {                                                // D: orbit
        a.i(0x79c) += 1;
        ai::stop(a, 0);
        ai::steer(a, e);
        ai::turn(a, a.f(0x814));
        if (r15(e) % 1000u < 20u && std::uint32_t(a.i(0x79c)) > 100u) {
            a.i(0x7a0) = int(r15(e) & 1u);
            a.i(0x79c) = 0;
        }
        a.mode() = 1;                                              // 0x48aa90: walk section
        const float s = a.speed310 * a.f(0x818);
        const float ang = a.i(0x7a0) != 0 ? a.heading + 1.5f : a.heading - 1.5f;
        const float sx = s * float(std::cos(double(ang))), sz = s * float(std::sin(double(ang)));
        const Vec3 to = a.v(0x734);
        float vx = 0, vz = 0;
        if (4.0f > dist) {                                         // too close: push away
            float nx = -to.x, nz = -to.z;
            const float l = std::sqrt(nx * nx + nz * nz);
            if (l > 0) { nx /= l; nz /= l; } else { nx = nz = 0; }
            const float k = 4.0f - dist;
            vx = ((k * nx) * a.f(0x818) * a.f(0x81c)) * 2;
            vz = ((nz * k) * a.f(0x818) * a.f(0x81c)) * 2;
        } else if (dist > 4.0f) {                                  // too far: pull in
            float nx = to.x, nz = to.z;
            const float l = std::sqrt(nx * nx + nz * nz);
            if (l > 0) { nx /= l; nz /= l; } else { nx = nz = 0; }
            const float k = dist - 4.0f;
            vx = (a.f(0x818) * nx) * a.f(0x820);
            vz = (a.f(0x818) * nz) * a.f(0x820);
            if (0.25f > k) { vx *= k; vz *= k; }
            vx = vx + vx; vz = vz + vz;
            if (e.grounded) e.rng();                               // the wasted hop roll
        }
        const float fx = sx + vx, fz = vz + sz;
        const float L = float(std::sqrt(double(fx * fx + fz * fz)));
        const float inv = 1.0f / (L * 60.0f);
        if (e.actor) { e.actor->mover.k2.x += inv * fx; e.actor->mover.k2.z += inv * fz; }   // +0x344/+0x34c
        a.speed = 0;
        // E: the strike.
        if (4.5f > dist && S != 7 && a.cool() == 0 && e.grounded && can_atk) {
            const float ang2 = float(std::atan2(double(to.z), double(to.x)));
            if (0.7853982f > float(std::fabs(ai::angle_diff(a.heading, ang2)))) {
                const std::uint32_t r = r15(e) % 1000u;
                const float pr = std::max(1.0f, 4.0f - dist) * 30.0f;
                if (pr > float(r)) {
                    out.section = 7; out.attack = true;            // gated; result ignored
                    a.dur() = 80;
                    a.action() = 1;
                    a.cool() = int(r15(e) % 30u) + (d > 0 ? 20 : 50) + a.dur();
                }
            }
        }
    } else if (a.i(0x728) != 0) {
        if (a.i(0x728) == 100 && S != 0x13a && e.grounded) { a.action() = 0; a.i(0x728) = 0; }
    }
    // Tail: during section 7 before its marker, a half-rate turn.
    if (S == 7) {
        if (e.aso_marker != 0) return out;
        ai::steer(a, e);
        ai::turn(a, a.f(0x814) * 0.5f);
    }
    return out;
}

}  // namespace cleria::brains
