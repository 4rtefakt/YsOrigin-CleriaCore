// M110 Genoid — brain FUN_00489970 (handler FUN_0048a370, AI index 4). Spec:
// build/ai_spec/M110.md.
//
// Runs (0x800, run section = a series of leaps) beyond 7 u, walk-approaches inside 7, stands
// inside 3.5 while cooling down. Within 4.5 u, off cooldown and grounded it rolls every tick:
// chance = R%90 - max(0, 15d) + (tagged ? 45 : 90) per mille, +/-45 deg, for section 7 (8 once
// enraged); while attacking it re-rolls the cooldown nearly every tick and, before the
// section's Marker(1), turns twice per tick. The first time HP < 50% it enrages: an ungated
// section 313 in which it grows to x1.30 (scale/radius by marker/100), DEF +15 while growing,
// tints at marker 30, then base speed x1.3, STR x1.075 and permanent Durable. Tagged spawns
// get speed/walk x1.1, max HP x1.35 (refilled), STR x1.05.
// Handler: no arena clamp; it overrides Durable after the VM step (durable_rule).
// [not modelled] the keep-out box restore (FUN_0042dc90).
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m110(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    Actor* A = e.actor;
    const bool tag = e.group_tag;
    out.durable_rule = (a.i(0x79c) != 0 || tag) ? 1 : 0;           // handler diff #4/#5
    if (e.launched) return out;
    const int d = difficulty(e);
    a.f(0x81c) = a.f(0x7ac);                                       // walk multiplier, every tick
    const int S = e.section;
    const int m = e.aso_marker;
    const int k = std::max(0, 15 * d);
    const int base = tag ? 45 : 90;
    // The doubly-expanded max(0, x): one draw for the sign test, one for the value.
    auto cl0 = [&] {
        const int v1 = int(r15(e) % 90u) - k + base;
        return v1 < 0 ? 0 : int(r15(e) % 90u) - k + base;
    };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            if (tag) {
                a.f(0x818) *= 1.1f; a.f(0x81c) *= 1.1f;
                if (A) { A->max_hp *= 1.35f; A->hp = A->max_hp; A->atk *= 1.05f; }
            }
            a.i(0x79c) = 0;
            a.f(0x7ac) = a.f(0x81c);
            if (A) {
                a.f(0x7b0) = A->scale; a.f(0x7b4) = A->radius; a.f(0x7cc) = A->height;
                a.f(0x7d8) = A->def;
            }
            a.i(0x70c) = 0;
            break;
        case 1:
            ai::idle(a, p, e);
            break;
        case 2: {
            if (a.dist() > 20.0f) ai::disengage(a);               // the tick continues
            if (S == 7 || S == 8) {                                // ATTACKING
                const int c = cl0();
                if (std::uint32_t(a.cool()) <= std::uint32_t(c)) a.cool() = cl0();
                a.action() = 1;
                if (m == 0) {                                      // wind-up: turn (twice a tick)
                    a.action() = 2;
                    ai::steer(a, e);
                    ai::turn(a, a.f(0x814));
                }
                break;
            }
            if (a.dist() > 7.0f) {
                a.action() = 0x800;
            } else {
                a.action() = (a.dist() < 3.5f && a.cool() != 0) ? 2 : 0x10;
                if (S == 1 && !e.grounded) a.action() = 0x800;     // mid-leap: keep running
            }
            if (a.dist() < 4.5f && a.cool() == 0 && e.grounded) {
                const int chance = cl0();
                const int r = int(r15(e) % 1000u);
                if (r < chance) {
                    const Vec3 to = a.v(0x734);
                    const float ang = float(std::atan2(double(to.z), double(to.x)));
                    if (float(std::fabs(ai::angle_diff(a.heading, ang))) < 0.7853982f) {
                        out.section = a.i(0x79c) == 0 ? 7 : 8;     // gated; result ignored
                        out.attack = true;
                    }
                }
            }
            if (a.i(0x728) == 100 && (S != 0x13a || std::uint32_t(m) >= 2u) && e.grounded) a.i(0x728) = 0;
            break;
        }
        default:
            break;
    }

    // Tail.
    if (A) A->def = a.f(0x7d8);
    int S2 = S;
    if (a.i(0x79c) == 0 && e.max_hp * 0.5f > e.hp && (a.phase() == 1 || a.phase() == 2) &&
        (S == 2 || S == 0 || S == 1)) {                            // E: enrage, once
        out.section = 0x139; out.section_force = true; out.attack = false;
        S2 = 0x139;
        a.i(0x79c) = 1;
        a.f(0x818) *= 1.3f;
        if (A) A->atk *= 1.075f;
    }
    if (S2 == 0x139 && A) {                                        // F: growth
        a.action() = 1;
        A->def = a.f(0x7d8) + 15.0f;
        const float mf = float(double(std::uint32_t(S2 == S ? m : 0)));   // the trigger tick reads 0
        const float s = mf * 0.01f + 1.0f;
        A->scale = a.f(0x7b0) * s;
        const float old = A->radius;
        A->radius = a.f(0x7b4) * s;
        const float dl = A->radius - old;                          // THIS tick's change (literal)
        const float oy = A->pos.y + A->height;
        A->height = a.f(0x7cc) + dl;
        A->pos.y = oy - A->height;                                 // keep the origin
        std::uint32_t col = 0xffffffffu;
        if (std::uint32_t(S2 == S ? m : 0) >= 30u) {
            const int v = std::max(0, 255 - int(mf * 3.0f));
            col = 0xff000000u | std::uint32_t(v) << 16 | std::uint32_t(v) << 8 |
                  std::uint32_t(255 - ((255 - v) >> 2));
        }
        A->tint = col;
        a.i(0x79c) = 2;
    }
    // G: section 314, the jump helper's jump: walk x2 for the whole section; at Marker 1,
    // grounded, the launch (K 0.1, no horizontal push — +0x12c & 0x40 ignores channel 1 anyway).
    if (S == 0x13a) {
        a.f(0x81c) = a.f(0x7ac) + a.f(0x7ac);
        if (A && e.grounded && m == 1) launch(*A, a.f(0x7dc) * 0.1f, out);
    }
    out.durable_rule = (a.i(0x79c) != 0 || tag) ? 1 : 0;
    return out;
}

}  // namespace cleria::brains
