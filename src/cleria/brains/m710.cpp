// M710 Astyle — brain FUN_004c3300 (AI index 64; handler FUN_004c3d50 = M100's minus the arena clamp, plus the
// 25-u far freeze (enemy_handler_far_freeze) and, on the first dead tick, the end of its attached suction effects
// (the view's dead path)). Spec: build/ai_spec/M710.md.
//
// A rooted plant: a random fixed facing at spawn, it never moves or turns. Every 15..44 ticks off cooldown it rolls
// 35 % and forces section 7 (the ~576-tick suction) when the target is within d + 20 (d + 25 while still unaware)
// on a y-halved distance and within 5.5 vertically. On the section's loop frames 5..8 it pulls the target toward
// itself on knock channel 2 (a per-character base + 0.005d, x1.1 airborne, x (1 - k)^4 between 5 u and d + 20),
// none while the player holds a burst section or is within 1.5 horizontally; it fast-forwards to the burst
// (Marker 1, EF2 234 from the ASO) when the target is within 2.5 or 5.5+ off vertically after 80 ticks, and keeps at
// most one of its attached suction effects (246) alive.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/body_push.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m710(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4c3407
    Actor* A = e.actor;
    const int d = difficulty(e);
    const Vec3 v = a.v(0x734);
    const float D3 = float(std::sqrt(double((v.y * v.y * 0.25f) + (v.x * v.x + v.z * v.z))));   // 0x6bd244
    const float Dp = float(std::sqrt(double(v.x * v.x + v.z * v.z)));
    const float DY = float(std::fabs(double(e.self.y - e.target_floor)));   // self origin vs T's floor (+0x318)
    float pull = e.character == 1 ? 0.085f : e.character == 2 ? 0.07f : e.character == 3 ? 0.105f : 0.09f;
    if (bodypush::player_holds(e.character, e.target_section)) pull = 0.0f;   // 0x139 / 0x13d
    int S = e.section;
    auto force7 = [&] { out.section = 7; out.section_force = true; out.attack = true; S = 7; a.i(0x87c) = 0; };
    switch (a.phase()) {
        case 0: {                                                  // INIT (0x4c3a0d)
            ai::init(a, p, e);
            a.steer() = 1; a.action() = 1;
            const float an = unit(e.rng()) * 6.2831855f + 0.0f;    // D0 (0x6bd898)
            const float c = float(std::cos(double(an))), s = float(std::sin(double(an)));
            a.f(0x748) = c; a.f(0x750) = s;
            if (a.turn_hold() == 0) a.heading = rotate_toward(a.heading, float(std::atan2(double(s), double(c))), 1000.0f);
            else --a.turn_hold();
            a.cool() = 0; a.dur() = 0; a.i(0x87c) = 0;
            break;                                                 // -> COMMON
        }
        case 1:                                                    // UNAWARE (0x4c388f)
            a.i(0x72c) = 0; a.i(0x70c) = 2;
            ai::idle(a, p, e);
            if (a.cool() == 0 && a.dur() == 0) {
                a.dur() = int(r15(e) % 30u) + 15;                  // D1
                if (int(r15(e) % 1000u) < 350 && float(d) + 25.0f > D3 && 5.5f > DY) force7();   // D2 (0x6bd9c0)
            }
            break;
        case 2:                                                    // ENGAGED (0x4c3590)
            if (a.dist() > (float(d) + 20.0f) + 1.0f) ai::disengage(a);   // continues
            if (a.cool() == 0 && a.dur() == 0) {
                a.dur() = int(r15(e) % 30u) + 15;                  // D1
                if (int(r15(e) % 1000u) < 350 && float(d) + 20.0f > D3 && 5.5f > DY) force7();   // D2 (0x6bd994)
            }
            a.action() = 1; a.steer() = 1;
            break;
        default: break;
    }
    // COMMON (0x4c370f). A: the suction.
    int marker = e.aso_marker;
    if (S == 7 && pull > 0.0f) {
        a.cool() = int(r15(e) % 60u) - 15 * d + 90;                // D3'
        a.dur() = int(r15(e) % 30u) + 15;                          // D4
        const int f = e.aso_frame;
        if (std::uint32_t(f - 5) <= 3u && float(d) + 20.0f > D3 && Dp > 1.5f) {
            float k = std::max(0.0f, D3 - 5.0f) / (float(d) + 15.0f);   // maxss (0x6bd868)
            if (!(1.0f > k)) k = 1.0f;
            const float fall = float(std::pow(double(1.0f - k), 4.0));  // FUN_006701e0 [H pow]
            const float g = e.target_grounded ? 1.0f : 1.1f;            // 0x6bd464
            const float s = ((float(d) * 0.005f) + pull) * g * fall;
            const double an = double(float(std::atan2(double(-v.z), double(-v.x))));   // toward self
            if (Actor* P = e.world ? e.world->player() : nullptr) {     // T (+0x888 is never set for M710)
                P->mover.k2.x += float(std::cos(an)) * s;
                P->mover.k2.z += float(std::sin(an)) * s;
            }
        }
        // B: fast-forward to the burst (0x4c3c51)
        if (marker == 0 && f == 8 && std::uint32_t(a.i(0x87c)) > 80u && (2.5f > D3 || !(DY < 5.5f))) {
            out.fast_forward = 1;                                  // FUN_00446680(1)
            marker = 1;                                            // the cull below sees +0x9bc = 1
        }
    }
    // C: the suction-effect cull (0x4c3c98)
    const int keep = (S == 7 && marker == 0) ? 1 : 0;
    if (std::uint32_t(a.i(0x87c)) % (keep ? 30u : 10u) == 0u && e.world && A) e.world->cull_attached_fx(*A, 246, keep);
    return out;
}

}  // namespace cleria::brains
