// M650 Tenat-Baocar — brain FUN_004b4890 (handler FUN_004b5360 = M100's minus the arena clamp, plus
// `+0x3c0 &= ~1` on the alive paths; AI index 58). Spec: build/ai_spec/M650.md.
//
// A flyer (hover 3.5) that never walks: engaged it stands facing the target and drifts on knock
// channel 2 along a direction re-picked every 30..59 ticks (+-54 deg about its heading; away inside
// 4 u), now and then with a hop on channel 1 along the mirrored pick. Inside 7.5 u, (40 + 10d)% at a
// re-pick with the target within 45 deg, it forces section 7: each of its two Marker(310)s fires 2 kind-2
// shots (4 on d >= 2) of EF2 310 (the marker value), 0.25 ahead and 0.5 to each side, 26.6 deg down, at
// 0.2 a tick, homing from age 15 to 30 at (6 + 2.25d) deg a tick. The base angle is the sprite's
// direction row x 45 deg (+0x9b4: the facing snapped to 45 deg plus the camera yaw), literal.
// [not modelled] the handler's +0x3c0 &= ~1 (landing puff).
// M680 Hekat-Baocar — brain FUN_004c4100 (AI 61, handler FUN_004c4df0 = M650's + the 25-u far freeze, which the
// view applies: enemy_handler_far_freeze). Spec: build/ai_spec/M680.md. M650 with other hop odds (5 % inside 4 u,
// 1 in 5 or always beyond 7.5 at 4 u and more; impulse 0.25..0.45), a hover-height roll at every decision (1 in 5:
// 3.5 / 1.5 / 5.5 at 1/2, 1/4, 1/4; 3.5 again when unaware), cooldown R%60 - 15d + 60, and slower long-homing
// shots: 0.15 + (0.0015 + 0.0005d) a tick, level below H 2 else 16.7 deg down, homing 10..180 at (1.85 + 0.45d) deg.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

namespace {

AiOut baocar(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, bool m680) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4b4997
    Actor* A = e.actor;
    const int d = difficulty(e);
    const float pd2 = a.f(0x734) * a.f(0x734) + a.f(0x73c) * a.f(0x73c);
    switch (a.phase()) {
        case 0:                                                    // INIT (0x4b52f9)
            ai::init(a, p, e);
            return out;
        case 1:                                                    // 0x4b52a3
            a.f(0x7e4) = a.f(0x7e8) = a.f(0x7ec) = 0.0f;
            a.i(0x72c) = 0; a.cool() = 0; a.dur() = 0; a.i(0x70c) = 0;
            if (m680) a.f(0x834) = 3.5f;                           // M680 0x4c4d21: the hover height back to 3.5
            ai::idle(a, p, e);
            return out;
        case 2: break;
        default: return out;
    }
    if (a.dist() > 20.0f) ai::disengage(a);                        // the tick continues
    a.action() = 2; a.steer() = 0;
    const float h = a.heading;
    if (a.i(0x72c) == 0) {                                         // DRIFT_AND_DECIDE (0x4b4e22)
        if (A) {
            A->mover.k2.x += ((a.f(0x7e4) * a.f(0x818)) * a.f(0x820)) * 0.5f;
            A->mover.k2.y += ((a.f(0x818) * a.f(0x7e8)) * a.f(0x820)) * 0.5f;
            A->mover.k2.z += ((a.f(0x818) * a.f(0x7ec)) * a.f(0x820)) * 0.5f;
        }
        if (a.dur() != 0) return out;
        const float u1 = unit(e.rng());                            // D1 (RAW)
        float th = ((u1 * 1.8849558f) - 0.9424779f) + h;           // 0x6bd54c, 0x6bd3f4
        if (16.0f > pd2) th += 3.1415927f;                         // closer than 4: away
        float dh = h - th;
        while (dh > 3.1415927f) dh -= 6.2831855f;
        while (-3.1415927f >= dh) dh += 6.2831855f;
        a.f(0x7e4) = float(std::cos(double(th)));
        a.f(0x7ec) = float(std::sin(double(th)));
        bool hop;
        if (!m680) {
            const std::uint32_t r2 = r15(e) % 3u;                  // D2
            hop = r2 == 0 || 16.0f > pd2 || pd2 > 56.25f;
        } else if (16.0f > pd2) {
            hop = r15(e) % 1000u < 50u;                            // M680 D2: 5 % inside 4
        } else if (pd2 >= 16.0f) {
            hop = r15(e) % 5u == 0u || pd2 > 56.25f;               // M680 D2 (NaN: no draw, no hop)
        } else {
            hop = false;
        }
        if (hop) {
            const float u3 = unit(e.rng());                        // D3 (RAW)
            const float sp = u3 * 0.19999999f + (m680 ? 0.25f : 0.15f);   // 0x6bd20c, 0x6bd244 / 0x6bd1d8
            if (A) {                                               // FUN_0047aa00(sp, dh)
                A->mover.k1.x += float(std::cos(double(h + dh))) * sp;
                A->mover.k1.z += float(std::sin(double(h + dh))) * sp;
            }
        }
        a.dur() = int(r15(e) % 30u) + 30;                          // D4
        if (56.25f > pd2) {                                        // within 7.5
            const std::uint32_t r5 = r15(e) % 1000u;               // D5
            if (int(r5) < std::max(100 * d, 0) + 400) {
                const float an = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
                if (0.7853982f > float(std::fabs(ai::angle_diff(h, an)))) {
                    out.section = 7; out.section_force = true;     // inline FUN_00446620: ungated
                    out.attack = true;
                    if (A) { A->mover.k1 = {}; A->mover.k2 = {}; }
                    a.i(0x72c) = 10;
                }
            }
        }
        if (m680 && r15(e) % 5u == 0u) {                           // M680 D6..D8 (0x4c4c14): the height roll
            if ((r15(e) & 1u) == 0u) a.f(0x834) = 3.5f;
            else a.f(0x834) = (r15(e) & 1u) ? 5.5f : 1.5f;
        }
        return out;
    }
    if (a.i(0x72c) != 10) return out;
    // CAST (0x4b4a6a)
    const int S = e.section;
    if (S != 7) {
        a.i(0x72c) = 0;
        if (S == 4) return out;                                    // a hit reaction cut it
        a.cool() = m680 ? int(r15(e) % 60u) - 15 * d + 60 : int(r15(e) % 60u) - 25 * d + 90;
        return out;
    }
    a.action() = 1;
    if (std::uint32_t(e.aso_frame) >= 2u)                         // every tick (literal)
        a.cool() = m680 ? int(r15(e) % 60u) - 15 * d + 60 : int(r15(e) % 60u) - 25 * d + 90;
    const int m = e.aso_marker;
    if (m == 0) return out;
    out.set_marker = 0;                                            // consume the marker
    const int n = 1 + (d > 1 ? 1 : 0);
    const float a0 = float(double(std::uint32_t(e.sprite_dir))) * 0.7853982f;
    const float c0 = float(std::cos(double(a0))), s0 = float(std::sin(double(a0)));
    const float turn = m680 ? (float(d) * 0.45f + 1.85f) * 0.0174533f : (float(d) * 2.25f + 6.0f) * 0.0174533f;
    const float acc = float(d) * 0.0005f + 0.0015f;                // M680 f1 (0x6bcfd8, 0x6bd000)
    for (int side = 0; side < 2; ++side) {
        const float k = side ? -0.5f : 0.5f;
        for (int i = 0; i < n; ++i) {
            AiBullet b;
            b.kind = 2; b.effect = m;
            b.pos = {(c0 * 0.25f - k * s0) + e.self.x, e.self.y + 0.0f, (k * c0 + s0 * 0.25f) + e.self.z};
            const float off = i == 0 ? 0.0f : (side ? 0.40840703f : -0.40840703f);
            const float th = off + a0;
            const float c = float(std::cos(double(th))), s = float(std::sin(double(th)));
            if (!m680) {
                const float L = std::sqrt((c * c + 0.25f) + s * s);
                b.dir = L == 0.0f ? Vec3{} : Vec3{c * (1.0f / L), (1.0f / L) * -0.5f, s * (1.0f / L)};
                b.speed = 0.2f; b.accel = 0;
                b.f16c = 15.0f; b.f170 = 30.0f;
            } else {                                               // M680 0x4c44f0..: level below H 2 (0x6bd580)
                const float dy = 2.0f > a.f(0x834) ? 0.0f : -0.3f;  // 0x6be000
                const float L = std::sqrt((dy * dy + c * c) + s * s);
                const float inv = 1.0f / L;
                b.dir = L == 0.0f ? Vec3{} : Vec3{inv * c, inv * dy, inv * s};
                b.speed = 0.15f; b.accel = acc;
                b.f16c = 10.0f; b.f170 = 180.0f;
            }
            b.gravity = 0; b.power = 0; b.knock = 15.0f;
            b.home_period = 1; b.home_rate = turn;
            b.f168 = 0.0f; b.f174 = 0.0f;
            out.bullets.push_back(b);
        }
    }
    return out;
}

}  // namespace

AiOut m650(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return baocar(a, p, e, false); }
AiOut m680(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) { return baocar(a, p, e, true); }

}  // namespace cleria::brains
