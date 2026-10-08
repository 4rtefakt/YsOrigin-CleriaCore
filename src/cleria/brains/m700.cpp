// M700 Fliogan — brain FUN_004bbf90 (AI index 63; handler FUN_004bce40 = M100's minus the arena clamp, plus a
// dead path that releases its attached effect: the view, tag kDash). Spec: build/ai_spec/M700.md.
//
// A hopping fencer. Engaged it runs at the target and stands inside 3.5 u or while its lockout +0x7a0 runs; every
// 10th tick it may hop forward (beyond 6 u, 25 %: 0.2 up, 0.6 along the heading +-54 deg) or step back (inside
// 3.5 u, 10 %: -0.2 along +-36 deg). Each time its 15..44-tick action runs out, off cooldown and grounded: inside
// 4.5 u 55 % forces section 8 (a backflip, then a dash wrapped in the attached looping hit effect of the
// section's Marker(496): power 5, rtype 2, status 0x10, released at Marker(1)); else inside 6 u (and 45 deg when
// beyond 4.5) 35 % forces section 7, a slash with shots that restarts itself at Pattern frame 5, d + 2 times.
// Cooldowns 270 - 30d + R%60 / 410 - 25d + R%60; the lockout is a third of the cooldown.
// [not modelled] the keep-out box restore (FUN_0042dc90). [approx] FUN_0047a880 un-loops the effect's particles
// through the SHARED emitter descriptor (M700.md §6 #2: every later spawn would lose the loop unless the EF2 is
// reloaded, not traced); the port un-loops this instance only.
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

namespace {
constexpr int kDash = 1;                                           // the owned effect's tag (+0x88c)
}

AiOut m700(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4bc09e
    Actor* A = e.actor;
    const int d = difficulty(e);
    const int S = e.section;
    const int PS = e.target_section;
    const float h = a.heading;
    bool gnd = e.grounded;
    const float pd = float(std::sqrt(double(a.f(0x734) * a.f(0x734) + a.f(0x73c) * a.f(0x73c))));
    auto release = [&] {                                           // FUN_0047a830 + FUN_0047a880; +0x88c = 0
        if (a.i(0x88c) == 0) return;
        AiEffect r; r.release_tag = kDash; out.effects.push_back(r);
        a.i(0x88c) = 0;
    };
    auto hop = [&](float dh, float sp) {
        if (A) push1(*A, h + dh, sp);
        if (gnd && A) { launch(*A, 0.2f, out); gnd = false; }
    };
    auto combo_vec = [&] { a.set_v(0x748, a.ncrumb() > 0 ? a.v(0x758) - e.self : a.v(0x734)); };
    auto end_attack = [&] {                                        // 0x4bc2a7
        a.i(0x72c) = 0;
        a.dur() = 0;
        a.i(0x7a0) = int(std::uint32_t(a.cool()) / 3u);
    };

    switch (a.phase()) {
        case 0:                                                    // INIT (0x4bcd2b)
            ai::init(a, p, e);
            a.i(0x79c) = 0; a.i(0x7a0) = 0; a.i(0x88c) = 0;
            break;
        case 1:                                                    // UNAWARE (0x4bccd2)
            a.i(0x70c) = 0; a.cool() = 0; a.dur() = 0;
            ai::idle(a, p, e);
            release();
            a.i(0x79c) = 0;
            break;
        case 2: {                                                  // ENGAGED (0x4bc149)
            if (a.dist() > 20.0f) ai::disengage(a);                // the tick continues
            const int sub = a.i(0x72c);
            if (sub == 20) {                                       // COMBO7 (0x4bc185)
                a.action() = 1;
                if (S != 7) end_attack();
                else if (std::uint32_t(e.aso_frame) >= 4u && std::uint32_t(a.i(0x79c)) < std::uint32_t(d + 2)) {
                    combo_vec();
                    ai::turn(a, a.f(0x814) * 20.0f);
                    if (std::uint32_t(e.aso_frame) >= 5u) {
                        out.section = 7; out.section_force = true; out.attack = true;   // restart from its first word
                        a.i(0x79c) += 1;
                    }
                }
            } else if (sub == 10) {                                // COMBO8 (0x4bc2d3)
                a.action() = 1;
                if (S != 8) end_attack();
                else {
                    int m = e.aso_marker;
                    if (std::uint32_t(m) > 10u) {                  // Marker(496): the dash effect
                        out.set_marker = 0;
                        if (a.i(0x88c) != 0) { AiEffect k; k.dying_tag = kDash; out.effects.push_back(k); }
                        AiEffect fx;
                        fx.id = m; fx.pos = e.self; fx.attach = true; fx.attach_full = true;
                        fx.power = 5.0f; fx.knock = 10.0f; fx.rtype = 2; fx.status = 0x10; fx.tag = kDash;
                        out.effects.push_back(fx);
                        a.i(0x88c) = 1;
                        m = 0;
                    }
                    combo_vec();
                    ai::turn(a, a.f(0x814));
                    if (std::uint32_t(m) > 0u) {                   // Marker(1): release
                        out.set_marker = 0;
                        release();
                        a.i(0x88c) = 0;
                    }
                }
            } else if (sub == 0) {                                 // DECIDE (0x4bc4c0)
                a.action() = 0x800;
                a.steer() = 0;
                const bool tick10 = std::uint32_t(a.i(0x87c)) % 10u == 0;
                if (3.5f > pd || a.i(0x7a0) > 0) {                 // NEAR (0x4bc67a)
                    a.action() = 2;
                    if (a.i(0x7a0) == 0 && gnd && tick10 && r15(e) % 1000u < 100u)
                        hop(unit(e.rng()) * 1.2566371f - 0.62831855f, -0.2f);
                } else if (pd > 6.0f && gnd && tick10) {           // FAR (0x4bc4fd)
                    if (r15(e) % 1000u < 250u) hop(unit(e.rng()) * 1.8849558f - 0.9424779f, 0.6f);
                }
                // MID (0x4bc7c7): Hard+, the player in an attack section — a roll that only spends a draw
                if (a.i(0x7a0) == 0 && d > 0 && gnd && ((0x100 <= PS && PS <= 0x109) || (0x10b <= PS && PS <= 0x112)) &&
                    tick10) {
                    if (r15(e) % 1000u < (d == 1 ? 150u : 350u)) e.rng();
                }
                if (a.dur() != 0 || a.i(0x7a0) != 0 || !gnd) break;   // 0x4bc8c9
                a.dur() = int(r15(e) % 30u) + 15;
                if (a.cool() != 0) break;
                const float an = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
                const bool in = 0.7853982f > float(std::fabs(ai::angle_diff(h, an)));
                bool start8 = false;
                if (4.5f > pd) start8 = r15(e) % 1000u < 550u;
                else if (!in) break;
                if (start8) {                                      // START8 (0x4bca30)
                    a.i(0x72c) = 10;
                    a.i(0x87c) = 0;
                    const int r2 = int(r15(e) % 60u);
                    a.dur() = 90;
                    a.cool() = r2 - 30 * d + 270;
                    out.section = 8; out.section_force = true; out.attack = true;
                } else {
                    if (!(6.0f > pd)) break;
                    if (r15(e) % 1000u >= 350u) break;
                    a.i(0x72c) = 20;                               // START7 (0x4bcb8d)
                    a.i(0x87c) = 0;
                    const int r2 = int(r15(e) % 60u);
                    a.cool() = r2 - 25 * d + 410;
                    a.dur() = 20 * d + 150;
                    out.section = 7; out.section_force = true; out.attack = true;
                    a.i(0x79c) = 0;
                }
                a.action() = 1;                                    // ARM (0x4bcc73)
                a.i(0x7a0) = int(std::uint32_t(a.cool()) / 3u);
            }
            // JUMPCLR (0x4bcc90)
            if (a.i(0x728) == 100 && (S != 0x13a || std::uint32_t(e.aso_marker) >= 2u) && gnd) a.i(0x728) = 0;
            break;
        }
        default: break;
    }
    // TAIL (0x4bcda3)
    if (a.i(0x7a0) != 0) a.i(0x7a0) -= 1;
    if (S != 8) release();                                         // outside section 8
    if (S == 0x13a && gnd && e.aso_marker == 1 && A) launch(*A, a.f(0x7dc) * 0.1f, out);
    return out;
}

}  // namespace cleria::brains
