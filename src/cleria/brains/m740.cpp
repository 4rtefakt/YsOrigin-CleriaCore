// M740 Drefflum — brain FUN_004c5140 (AI index 67; handler FUN_004c59d0 = M680's far-freeze handler without the
// +0x3c0 clear: enemy_handler_far_freeze). Spec: build/ai_spec/M740.md.
//
// A stationary caster: at spawn it snaps to a random facing and never moves or turns (action 1 every tick).
// Unaware it also acquires the player within 30 u when the sight test passes (21.2 u, +-45 deg). Engaged
// (disengage beyond 40 u) it decides on every 30th tick (the phase rolled at spawn) when its cooldown is 0 or its
// last coin was 0: inside 7 u (30 + 15 max(d, 0))% forces section 7 (three attached needle bursts, EF2 265,
// status 57, from the ASO); otherwise (or failing that) the same odds force section 8, whose Marker 1 fires a fan of
// 5 (7 on d >= 1) kind-7 weaving bubbles of EF2 491 at the player, 0.4 rad apart. Each attack flips a coin into
// +0x79c (1 = wait for the cooldown R%90 - 30d + 120, rolled every tick of section 7).
// [not modelled] the keep-out box restore (FUN_0042dc90); the player status bits 3 / 4 / 5 of the needles.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m740(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4c5248
    const int d = difficulty(e);
    switch (a.phase()) {
        case 0: {                                                  // INIT (0x4c5811)
            ai::init(a, p, e);
            const float an = unit(e.rng()) * 6.2831855f + 0.0f;    // D1 (0x6bd898, 0x6bcf94)
            a.f(0x748) = float(std::cos(double(an)));
            a.f(0x750) = float(std::sin(double(an)));
            a.action() = 1; a.steer() = 1;
            if (a.turn_hold() == 0)                                // FUN_00427410 + FUN_005eecc0(rate 1000): a snap
                a.heading = rotate_toward(a.heading, float(std::atan2(double(a.f(0x750)), double(a.f(0x748)))), 1000.0f);
            else
                --a.turn_hold();
            a.i(0x87c) = int(r15(e) % 30u);                        // D2: the decision gate's phase
            a.i(0x79c) = 0;
            return out;
        }
        case 1:                                                    // UNAWARE (0x4c577b)
            a.i(0x70c) = 2; a.action() = 1; a.cool() = 0;
            ai::idle(a, p, e);
            if (30.0f > a.dist() && ai::sees(a, e, a.f(0x834) * 0.5f + 10.0f, 450.0f, 0.7853982f))   // 0x6bd9e4, 0x6bdcc8
                ai::acquire(a);
            return out;
        case 2: break;
        default: return out;
    }
    if (a.dist() > 40.0f) ai::disengage(a);                        // 0x6bda34; the tick continues
    a.action() = 1;                                                // stop + a 1-tick heading hold: it never turns
    if (e.section == 7) {                                          // 0x4c52e3
        a.cool() = int(r15(e) % 90u) - 30 * d + 120;
        return out;
    }
    if (e.section == 8) {                                          // 0x4c5355: the bubble fan
        if (e.aso_marker != 1) return out;
        out.set_marker = 0;
        const int hi = 2 + (d > 0 ? 1 : 0);
        for (int i = (d <= 0 ? 1 : 0) - 3; i <= hi; ++i) {
            const float base = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
            const float an = base + float(i) * 0.4f;               // 0x6bd2d4
            Vec3 v{float(std::cos(double(an))), 0.0f, float(std::sin(double(an)))};
            const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);   // FUN_0042cc50
            v = l == 0.0f ? Vec3{} : Vec3{v.x * (1.0f / l), v.y * (1.0f / l), v.z * (1.0f / l)};
            AiBullet b;
            b.kind = 7; b.effect = 0x1eb;
            b.pos = {e.self.x, e.self.y + 0.5f, e.self.z};
            b.dir = v; b.speed = 0.15f; b.accel = 0; b.gravity = 0; b.power = 1.5f; b.knock = 20.0f;
            b.f168 = 3.5f; b.f16c = 5.0f;
            out.bullets.push_back(b);
        }
        return out;
    }
    if (std::uint32_t(a.i(0x87c)) % 30u != 0u) return out;         // 0x4c54d4: the decision gate
    const bool gate = a.cool() == 0 || a.i(0x79c) == 0;
    const int thr = std::max(150 * d, 0) + 300;
    auto attack = [&](int s) {
        a.i(0x79c) = int(r15(e) & 1u);                             // the coin
        out.section = s; out.section_force = true; out.attack = true;
    };
    if (gate && 7.0f > a.dist() && int(r15(e) % 1000u) < thr) { attack(7); return out; }   // 0x6bd8b8
    if (gate && int(r15(e) % 1000u) < thr) { attack(8); return out; }
    a.i(0x79c) = 1;
    return out;
}

}  // namespace cleria::brains
