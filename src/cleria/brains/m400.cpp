// M400 Tolpi — brain FUN_004ae8c0 (handler FUN_004af320 = M100's minus the arena clamp, AI index 33).
// Spec: build/ai_spec/M400.md.
//
// A slow walker. Beyond 10 + 0.5d u (horizontal) it walks at the target in 15-tick steps; inside, level
// with a grounded target (|dy| < 2.5), it stands, turns at (0.3 + 0.09d) x its rate and sprays (section 7,
// 179 ticks): while the section's Marker is 1 it fires a pollen bullet every 2nd tick from 1 u ahead of
// its visible facing — kind 1, EF2 303 (m_400kafun01, two hit emitters), +-15 deg random yaw about the
// facing and +-15 deg absolute pitch, at 0.16 + 0.015d a tick slowing 1% a tick. Cooldown 209 - 10d +
// R%30, cut to 90 by a hit reaction.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m400(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4ae9ce
    const int d = difficulty(e);
    const Vec3 to = a.v(0x734);
    const float lsq = to.x * to.x + 0.0f + to.z * to.z;
    const float pd = std::sqrt(lsq);                               // horizontal
    const bool can_atk = !(std::fabs(e.target.y - e.self.y) >= 2.5f && e.target_grounded);   // 0x6bd624
    float dot;
    {
        float fx = std::cos(a.heading), fz = std::sin(a.heading);
        const float fl = std::sqrt(fx * fx + 0.0f + fz * fz);
        if (fl != 0.0f) { fx /= fl; fz /= fl; } else { fx = fz = 0.0f; }
        const float vx = lsq == 0.0f ? 0.0f : to.x / pd, vz = lsq == 0.0f ? 0.0f : to.z / pd;
        dot = (0.0f + fx * vx) + vz * fz;
    }
    int& sub = a.i(0x72c);
    int& latch = a.i(0x730);
    switch (a.phase()) {
        case 0:                                                    // INIT (0x4af20c)
            ai::init(a, p, e);
            a.i(0x7a0) = 0;
            a.f(0x7ac) = a.f(0x814);
            sub = 0; latch = 0;
            return out;
        case 1:                                                    // 0x4af1de
            ai::idle(a, p, e);
            sub = 0; latch = 0;
            a.f(0x814) = a.f(0x7ac);
            return out;
        case 2: break;
        default: return out;
    }
    if (a.dist() > 20.0f) ai::disengage(a);                        // the tick continues
    if (std::uint32_t(sub) > 0x66u) return out;
    switch (sub) {
        case 0:
            latch = 0; sub = 0x14; a.f(0x814) = a.f(0x7ac);
            return out;
        case 0x14:                                                 // decide
            if (a.dur() != 0) return out;
            if (latch == 1) { sub = 100; latch = 0; a.i(0x79c) = 0; return out; }
            if (latch != 0) return out;
            if (pd > float(d) * 0.5f + 10.0f || dot == 0.0f) {     // 0x6bd304, 0x6bd928
                a.action() = 0x10; a.steer() = 0; a.dur() = 15;    // walk at it
                return out;
            }
            if (!can_atk) return out;
            latch = 1;
            return out;
        case 100:                                                  // aim
            a.action() = 2; a.steer() = 0; a.dur() = 0;
            sub = 0x65;
            a.f(0x814) = (float(d) * 0.09f + 0.3f) * a.f(0x7ac);   // 0x6bd190, 0x6bd27c
            return out;
        case 0x65: {                                               // the spray
            if (a.dur() != 0) return out;
            out.section = 7; out.attack = true;                    // gated; result ignored
            a.action() = 2; a.steer() = 0; a.dur() = 179;
            const int r = int(r15(e) % 30u);
            a.cool() = r + 10 * (-d + 3) + a.dur();                // 209 - 10d + r
            a.i(0x7a0) = 0;
            sub = 0x66;
            return out;
        }
        case 0x66: {                                               // spraying / resting
            const int c = a.cool();
            if (a.dur() == 0) { a.action() = 2; a.dur() = c; a.f(0x814) = a.f(0x7ac); }
            if (c == 0) { sub = a.i(0x79c); return out; }
            if (e.section == 4) {                                  // a hit reaction
                if (std::uint32_t(c) > 90u) a.cool() = 90;
                return out;
            }
            if (e.aso_marker == 1 && a.i(0x7a0) == 0) {            // FIRE (0x4aef33)
                const std::uint32_t w1 = e.rng(), w2 = e.rng();
                const int r1 = int((w1 >> 17) % 30u), r2 = int((w2 >> 17) % 30u);
                const float F = e.facing;                          // +0x314
                const float a1 = float(r1 - 15) * 3.14159f / 180.0f + F;   // 0x6bd6a8, 0x6bdba0
                const float a2 = float(r2 - 15) * 3.14159f / 180.0f;
                Vec3 dir{float(std::cos(double(a1))), float(std::sin(double(a2))), float(std::sin(double(a1)))};
                const float l = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);   // FUN_0042cc50
                dir = l == 0.0f ? Vec3{} : Vec3{dir.x * (1.0f / l), dir.y * (1.0f / l), dir.z * (1.0f / l)};
                const float k = float(d) * 0.015f + 0.16f;         // 0x6bd0c0, 0x6bd1dc
                AiBullet b;
                b.kind = 1; b.effect = 303;                        // enemy\m_400kafun01.eft
                b.pos = {e.self.x + float(std::cos(double(F))), e.self.y + 0.0f, e.self.z + float(std::sin(double(F)))};
                b.dir = dir;
                b.speed = k;
                b.accel = float(double(-k) * 0.01);                // 0x6bd448 (f64)
                b.gravity = 0.0f; b.power = 0.0f; b.knock = 1.0f;
                b.f168 = 1.0f;                                     // the kind-1 terrain-probe flag
                out.bullets.push_back(b);
                a.i(0x7a0) = 2;
            }
            if (a.i(0x7a0) != 0) a.i(0x7a0) -= 1;                  // every 2nd tick of the window
            return out;
        }
        default: return out;
    }
}

}  // namespace cleria::brains
