// M690 Glaregrave — brain FUN_004c5d20 (AI index 62; handler FUN_004c6cd0 = M100's minus the arena clamp, plus
// the 25-u far freeze: enemy_handler_far_freeze). Spec: build/ai_spec/M690.md.
//
// A sword-and-shield knight. It runs at the target and stands inside 4.5 u. There, grounded, off cooldown and
// within 72 deg: 30 % a tick forces section 7 (a two-hit combo; the brain's lunge +0x7ac = 0.4 on knock channel
// 2 at Marker 1, x0.9 a tick). Below half HP section 7's Marker(11) snap-turns it and chains into section 8 (the
// leaping lightning strike). After an attack it stands locked for 3/4 of a R%60 - 20d + 90 cooldown. Section 6 is
// its all-round guard (src/cleria/hit.hpp): started by a damage accumulator (+2 per HP change, -0.075 a tick,
// over 3 - d: 4 in 6) or, every 10th tick, by a player attack within 36 deg of its heading (30 %, 1 in 3); it
// lasts 60..119 ticks, each blocked hit refreshes it, and it breaks after d + 9 blocks. The same rolls (and 1 %
// of quiet ticks) give a backstep (0.15 up, -0.35) or a forward leap (0.275 up, +0.55). Every tick: STR =
// base (1 + 0.025 (1 - (HP/max)^2)) and a blue tint from the same factor.
// [not modelled] the keep-out box restore (FUN_0042dc90). The EX-element bits (+0x3bc & 0x1200) come from
// AiEnv::events, which the view does not fill yet: every blocked hit counts.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m690(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4c5e2e
    Actor* A = e.actor;
    const int d = difficulty(e);
    const int PS = e.target_section;
    int S = e.section;                                             // live: a force writes +0x994 at once
    bool gnd = e.grounded;
    int marker = e.aso_marker;
    auto force = [&](int n) { out.section = n; out.section_force = true; S = n; };
    auto turn_vec = [&] { a.set_v(0x748, a.ncrumb() > 0 ? a.v(0x758) - e.self : a.v(0x734)); };
    auto adiff_target = [&] {
        const float an = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
        return float(std::fabs(ai::angle_diff(a.heading, an)));
    };

    switch (a.phase()) {
        case 0:                                                    // INIT (0x4c634b)
            ai::init(a, p, e);
            a.i(0x70c) = 0; a.action() = 0x800; a.steer() = 0; a.dur() = 0; a.cool() = 0; a.i(0x72c) = 0;
            a.f(0x7ac) = 0.0f;
            a.f(0x7b0) = A ? A->atk : 0.0f;                        // the STR snapshot
            a.f(0x7b4) = 0.0f;
            break;
        case 1:                                                    // UNAWARE (0x4c633f)
            ai::idle(a, p, e);
            break;
        case 2: {                                                  // ENGAGED (0x4c5ec9)
            if (a.dist() > 20.0f) ai::disengage(a);                // the tick continues
            if (S == 7 || S == 8) {                                // ATTACKING (0x4c6069)
                if (std::uint32_t(e.aso_frame) < 1u) {
                    turn_vec();
                    ai::turn(a, a.f(0x814) + a.f(0x814));
                }
                if (marker != 0) {
                    if (e.max_hp * 0.5f > e.hp && S == 7 && std::uint32_t(marker) > 10u) {   // the chain
                        turn_vec();
                        a.turn_hold() = 0;
                        ai::turn(a, a.f(0x814) * 200.0f);
                        force(8);
                        out.attack = true;
                    }
                    if (marker == 1) a.f(0x7ac) = 0.4f;
                    else if (marker == 2) a.f(0x7ac) = 0.05f;
                    else if (marker == 3) a.f(0x7ac) = 0.28f;
                    marker = 0; out.set_marker = 0;
                }
                const int c = int(r15(e) % 60u) - 20 * d + 90;     // D2, every tick
                a.i(0x72c) = 2;
                a.cool() = c;
                a.dur() = int((std::uint32_t(c) >> 2) + (std::uint32_t(c) >> 1));
            } else if (a.dur() == 0) {                             // DECIDE (0x4c5f06)
                if (a.cool() == 0 && gnd && 4.5f > a.dist()) {
                    if (r15(e) % 1000u < 300u && 1.2566371f > adiff_target()) { force(7); out.attack = true; }
                }
                a.i(0x72c) = 4.5f > a.dist() ? 1 : 0;
            }
            // ACT (0x4c62b9)
            a.steer() = 0;
            a.action() = a.i(0x72c) == 0 ? 0x800 : a.i(0x72c) == 1 ? 2 : 1;
            if (!gnd) a.action() = 1;
            if (a.i(0x728) == 100 && (S != 0x13a || std::uint32_t(marker) >= 2u) && gnd) a.i(0x728) = 0;
            break;
        }
        default: break;
    }

    // COMMON (0x4c6404), every phase
    if (S == 6) {                                                  // GUARDING (0x4c6411)
        turn_vec();
        a.turn_hold() = 0;
        const float an = float(std::atan2(double(a.f(0x750)), double(a.f(0x748))));
        a.heading = rotate_toward(a.heading, an, a.f(0x814) * 5.0f);
        if (a.dur() == 0 || std::uint32_t(a.i(0x878)) > std::uint32_t(d + 8)) {   // timer out, or the guard broken
            force(2);
            a.i(0x878) = 0; a.dur() = 0;
        } else if (e.was_hit && !e.took_damage) {                  // a blocked hit
            if (!(e.events & 0x1200u) || (e.rng() & 0x20000u) != 0) a.i(0x878) += 1;   // D3
            a.dur() = int(r15(e) % 60u) + 60;                      // D4
        }
    } else {                                                       // NOT GUARDING (0x4c6619)
        int pick = 0;
        a.i(0x878) = 0;
        if (!(a.f(0x880) == e.hp)) { a.f(0x880) = e.hp; a.f(0x7b4) += 2.0f; }
        if (S != 7 && S != 8 && gnd) {
            if (a.f(0x7b4) > float(3 - d)) {
                pick = int(r15(e) % 6u) + 1;                       // D5
                if (pick > 3) pick = 1;
            }
            if (a.f(0x7b4) == 0.0f && std::uint32_t(a.i(0x87c)) % 10u == 0 &&
                ((0x100 <= PS && PS <= 0x109) || (0x10b <= PS && PS <= 0x112))) {
                if (r15(e) % 1000u < 300u && 0.62831855f > adiff_target()) pick = int(r15(e) % 3u) + 1;   // D6, D7
            }
            bool go = true;
            if (pick == 0) {                                       // the idle hop roll
                if (r15(e) % 1000u >= 10u) go = false;             // D8
                else pick = int((e.rng() >> 17) & 1u) + 2;         // D9
            }
            if (go && pick == 1) {                                 // start a guard (0x4c694a)
                if (!e.group_tag || r15(e) % 1000u < 100u) {       // D10
                    force(6);
                    a.dur() = int(r15(e) % 60u) + 60;              // D11
                    a.f(0x7b4) = 0.0f;
                }
            } else if (go && pick == 2) {                          // backstep (0x4c6a80)
                if (gnd && A) { launch(*A, 0.15f, out); gnd = false; }
                a.f(0x7ac) = a.f(0x7ac) - 0.35f;
            } else if (go && pick == 3) {                          // forward leap (0x4c6aca)
                if (gnd && A) { launch(*A, 0.275f, out); gnd = false; }
                a.f(0x7ac) = a.f(0x7ac) + 0.55f;
            }
        }
    }

    // TAIL (0x4c6b15), every phase
    if (a.f(0x7b4) > 0.0f) a.f(0x7b4) = std::max(0.0f, a.f(0x7b4) - 0.075f);
    if (S == 0x13a && gnd && marker == 1 && A) launch(*A, std::min(a.f(0x7dc) * 0.1f, 0.2f), out);
    if (a.f(0x7ac) > 1e-05f || -1e-05f > a.f(0x7ac)) {             // the impulse, on knock channel 2
        if (A) {
            A->mover.k2.x += float(std::cos(double(a.heading))) * a.f(0x7ac);
            A->mover.k2.z += float(std::sin(double(a.heading))) * a.f(0x7ac);
        }
        a.f(0x7ac) = a.f(0x7ac) * 0.9f;
        if (1e-05f > a.f(0x7ac) && a.f(0x7ac) > -1e-05f) a.f(0x7ac) = 0.0f;
    }
    const float r = e.hp / e.max_hp;
    const float k1 = 1.0f - r * r;
    if (A) {
        A->atk = ((a.f(0x7b0) * k1) * 0.025f) + a.f(0x7b0);
        const int k = int((k1 * 255.0f) * 0.35f);
        const std::uint32_t g = std::uint32_t(255 - k) & 0xffu, b = std::uint32_t(255 - (k >> 2)) & 0xffu;
        A->tint = 0xFF000000u | g << 16 | g << 8 | b;
    }
    return out;
}

}  // namespace cleria::brains
