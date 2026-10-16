// M760 Raldisch — brain FUN_00499f90 (handler FUN_0049ad70 = M100's minus the arena clamp,
// AI index 69). Spec: build/ai_spec/M760.md. Bordisch's (M240) design with a charge.
//
// Beyond 6 u (horizontal) it approaches in 30-tick plans (run, or walk when a probe finds no
// floor 3.0 below its origin 1.535 ahead). Inside 6 u (unless the target is grounded 4+ off)
// it picks: within 5 u the jump press (section 7, 4-tick wind-up); beyond, the charge
// (section 8, 10-tick wind-up); beyond 5 u below half HP, the press then the charge. The
// request is gated and unchecked; it then stands heading-locked for the cooldown
// 63 + R%40 + (3 - d)*20 (the combo's press: exactly 63). The press pushes 1/n along its
// facing into knock channel 2 at markers 2..4; the charge pushes ((40 | 60 low) + bonus)/n,
// SQUARED, at marker 3 while a floor is ahead (else it flickers frames 7/8), with smoke
// (effect 232) every third tick. Below half HP: STR x1.15, run and turn x1.5.
// [not modelled] the enrage's white <-> 0xffa00000
// diffuse pulse (+0x27c..+0x28c, FUN_0045a3a0 — no diffuse tween in the view yet); the
// smoke's rotation (-pi/2 - facing).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m760(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const Vec3 to = a.v(0x734);
    const float hd = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    const bool can_atk = !(double(std::fabs(e.target.y - e.self.y)) >= 4.0 && e.target_grounded);
    const bool low = 0.5f > e.hp / e.max_hp;
    int& sub = a.i(0x72c);
    int& step = a.i(0x730);
    const float L = e.radius > 0.5f ? e.radius + 0.5f : 1.0f;
    auto floor_ahead = [&] {
        const Vec3 P{e.self.x + std::cos(a.heading) * L, e.self.y, e.self.z + std::sin(a.heading) * L};
        return !e.world || e.world->ray(P, {0, -3.0f, 0}).hit;
    };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.f(0x7ac) = A ? A->atk : 0.0f;                        // base STR
            a.f(0x7b0) = a.f(0x820);                               // base run multiplier
            a.f(0x7b4) = a.f(0x814);                               // base turn rate
            a.i(0x7a4) = 100;
            a.i(0x728) = 0; sub = 0; step = 0;
            break;
        case 1:
            a.i(0x728) = 0; sub = 0; step = 0;
            ai::idle(a, p, e);
            if (A) A->atk = a.f(0x7ac);
            a.f(0x820) = a.f(0x7b0);
            a.f(0x814) = a.f(0x7b4);
            break;
        case 2: {
            if (a.dist() > 20.0f) ai::disengage(a);               // the tick continues
            auto cooldown = [&] { return int(r15(e) % 40u) + 20 * (3 - difficulty(e)) + a.dur(); };   // dur = 63
            auto hold = [&] {                                      // returns false when the cooldown is over
                const int c = a.cool();
                if (a.dur() == 0) { a.action() = 1; a.dur() = c; }
                if (c == 0) { sub = a.i(0x79c); return false; }
                return true;
            };
            switch (sub) {
                case 0: step = 0; sub = 20; break;
                case 20:
                    if (a.dur() != 0) break;
                    switch (step) {
                        case 0: {
                            const bool fl = floor_ahead();         // cast even when hd <= 6
                            if (hd > 6.0f) { a.steer() = 0; a.dur() = 30; a.action() = fl ? 0x400 : 0x10; }
                            else if (can_atk) step = 1;
                            break;
                        }
                        case 1:
                            e.rng();                               // drawn and discarded
                            if (hd > 5.0f) {
                                if (low) { sub = 100; a.i(0x79c) = 20; a.i(0x7a0) = 1; step = 2; }
                                else { a.i(0x7a0) = 0; sub = 110; a.i(0x79c) = 0; }
                            } else { a.i(0x7a0) = 0; sub = 100; a.i(0x79c) = 0; }
                            break;
                        case 2: a.i(0x7a0) = 0; sub = 110; a.i(0x79c) = 0; break;
                        default: break;
                    }
                    break;
                case 100:                                          // press wind-up
                    a.action() = 2; a.steer() = 0; a.dur() = 4;
                    if (A) A->atk = a.f(0x7ac);
                    sub = 101;
                    break;
                case 101:
                    if (a.dur() != 0) break;
                    out.section = 7; out.attack = true;            // gated; result ignored
                    a.action() = 1; a.dur() = 63;
                    a.cool() = a.i(0x7a0) != 0 ? 63 : cooldown();
                    sub = 102; a.i(0x87c) = 1;
                    break;
                case 102:
                    if (hold() && std::uint32_t(e.aso_marker - 2) <= 2u && A) {
                        const float f = 1.0f / float(std::uint32_t(a.i(0x87c)));
                        A->mover.k2.x += std::cos(e.facing) * f;
                        A->mover.k2.z += std::sin(e.facing) * f;
                    }
                    break;
                case 110:                                          // charge wind-up
                    a.action() = 2; a.steer() = 0; a.dur() = 10; sub = 111;
                    break;
                case 111:
                    if (a.dur() != 0) break;
                    out.section = 8; out.attack = true;
                    a.action() = 1; a.dur() = 63;
                    a.cool() = cooldown();
                    sub = 112; a.i(0x87c) = 1;
                    break;
                case 112: {
                    if (!hold() || e.aso_marker != 3) break;
                    if (floor_ahead()) {
                        const int d = difficulty(e);
                        float B = low ? 60.0f : 40.0f;
                        B += float(d >= 1 ? 30 : d == 0 ? 0 : -10);
                        float s = B / float(std::uint32_t(a.i(0x87c)));
                        s = s * s;
                        if (A) { A->mover.k2.x += std::cos(e.facing) * s; A->mover.k2.z += std::sin(e.facing) * s; }
                    } else {
                        out.set_frame = (std::uint32_t(a.i(0x8a0)) & 3u) < 2u ? 7 : 8;
                    }
                    if (std::uint32_t(a.i(0x8a0)) % 3u == 0) {
                        AiEffect fx;
                        fx.id = 232;                               // m_760_tossinkemuri01 (charge smoke)
                        fx.pos = {e.self.x, e.self.y - 0.75f, e.self.z};
                        fx.rot = {0.0f, -1.5707964f - e.facing, 0.0f};
                        out.effects.push_back(fx);
                    }
                    break;
                }
                default: break;
            }
            break;
        }
        default: break;
    }

    // TAIL (every phase once past init): the enrage below half HP.
    if (a.phase() != 0) {
        if (!low) { if (A) A->atk = a.f(0x7ac); }
        else {
            if (A) A->atk = a.f(0x7ac) * 1.15f;
            a.f(0x820) = a.f(0x7b0) * 1.5f;
            a.f(0x814) = a.f(0x7b4) * 1.5f;
            if (std::uint32_t(++a.i(0x7a4)) >= 25u) a.i(0x7a4) = 0;   // the pulse restart (tween not modelled)
        }
    }
    return out;
}

}  // namespace cleria::brains
