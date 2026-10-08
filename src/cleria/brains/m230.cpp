// M230 Euchreas — brain FUN_004c1260 (AI index 16; handler FUN_004c2fb0 = M100's minus the arena clamp, plus the
// 25-u far freeze: enemy_handler_far_freeze). Spec: build/ai_spec/M230.md.
//
// A walking, teleporting mage, stun-immune and Durable once engaged; every section request is gated. State
// +0x728: 0 MAIN — it walks at the target in 0..39-tick bursts or (7 %, beyond 5 u) stands 30..59 ticks; within
// 11.5 u, grounded, level with a grounded target (2.5) and off cooldown it rolls EVERY tick: 9 % the staff shot
// (section 7, the script's bolt) inside 60 deg; then 9 % the triple lob (section 305, state 6: at each
// Marker(272) the brain fires a kind-2 mortar bolt at 85 / 70 / 55 deg) once three staff shots have passed; then
// (7..20) % a teleport if it has attacked since the last one. After an attack it chants (section 300, state 4);
// off cooldown 11 % a tick (always beyond 20 u) plays the wind-down 301 and returns to MAIN; the chant can also
// teleport (section 304). The teleport: a 60-tick fade-out (1; effects 270 + 269 attached, SE 464), up to 5 tries
// for a point 4.5..6 u from the target with a clear ray, 30..149 ticks running unseen toward that point as it
// follows the target (2; 0.21 a tick, turn x10), a vertical snap, a 60-tick fade-in (3). Unhittable under alpha 0.5.
// [not modelled] the keep-out box restore (FUN_0042dc90); state 5 (the cut scripted vanish: nothing writes it);
// byte +0x85c (the attached effects' release on death).
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m230(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // 0x4c136e: +0x60 untouched
    Actor* A = e.actor;
    const Actor* Pa = e.world ? e.world->player() : nullptr;
    const Vec3 T = e.target;                                       // +0x888, else the player
    const Vec3 P = Pa ? Vec3{Pa->pos.x, Pa->pos.y + Pa->height, Pa->pos.z} : e.target;
    const Vec3 S0 = e.self;
    bool C = true;                                                 // level with a grounded target
    if (e.target_grounded && float(std::fabs(double(T.y - S0.y))) >= 2.5f) C = false;
    if (0.1f > a.alpha) a.i(0x79c) += 1; else a.i(0x79c) = 0;      // the invisible-tick counter (never read)
    int S = e.section;                                             // live: an accepted request writes +0x994
    const bool gnd = e.grounded;
    auto req = [&](int id) { if (ai::request(S, id, false, e)) { out.section = id; out.section_force = false; } };
    auto in_cone = [&] {
        const float an = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
        return 1.0471976f > float(std::fabs(ai::angle_diff(a.heading, an)));
    };
    auto tele_roll = [&] {                                         // 70 per mille beyond 7.5 u, 201 at 0
        const float t = std::max(1.0f, (11.5f - a.dist()) * 0.25f) * 70.0f;
        return t > float(r15(e) % 1000u);
    };
    auto warp_fx = [&] {                                           // 0x4c218e / 0x4c28e6
        const Vec3 at{S0.x, S0.y - 1.0f, S0.z};
        AiEffect f1; f1.id = 0x10e; f1.pos = at; f1.attach = true; f1.attach_full = true;                    // m_230_taiki
        AiEffect f2; f2.id = 0x10d; f2.pos = at; f2.scale = 1.3f; f2.attach = true; f2.attach_full = true;   // z_warp
        out.effects.push_back(f1); out.effects.push_back(f2);
        out.sounds.push_back(0x1d0);                               // SE 464 zako\z_390_run
    };

    switch (a.phase()) {
        case 0:                                                    // INIT (0x4c1446)
            ai::init(a, p, e);
            for (int i = 0; i < 8; ++i) a.f(0x834 + 4 * i) = 0.0f; // only EI+0x90..+0xaf are copied
            a.f(0x7ac) = a.f(0x820);
            a.f(0x7b0) = a.f(0x814);
            a.i(0x7a0) = 0; a.i(0x72c) = 1; a.i(0x730) = 1; a.i(0x88c) = 0;
            break;
        case 1: {                                                  // UNAWARE (0x4c14dd)
            const int st = a.i(0x728);
            if (st == 1) { a.i(0x728) = 3; a.dur() = 60 - a.dur(); }   // a fade-out reverses into a fade-in
            else if (st == 2) { a.i(0x728) = 3; a.dur() = 60; }        // unseen: fade back in where it stands
            else if (st != 3) ai::idle(a, p, e);
            break;
        }
        case 3:                                                    // the stun hold (0x4c2152)
            if (a.i(0x728) == 1 || a.i(0x728) == 2) { a.i(0x728) = 0; a.alpha = 1.0f; }
            break;
        case 2: {                                                  // ENGAGED (0x4c152e)
            int st = a.i(0x728);
            if (a.dist() > 20.0f && st != 4 && st != 5 && st != 6) ai::disengage(a);   // the tick continues
            if (A) A->flags12c |= 0x80000u;                        // stun-immune from here on
            out.set_durable = 1;
            if (st == 0 && a.i(0x72c) == 0 && (S != 7 || std::uint32_t(a.dur()) <= 1u)) {
                a.action() = 2;
                req(300);
                st = a.i(0x728) = 4;                               // the chant
            }
            if (st == 4) {
                if (a.i(0x72c) != 0 && S != 0x12d) { a.action() = 0; st = a.i(0x728) = 0; }   // 301 is over
            } else if (std::uint32_t(st) > 6u) break;
            switch (st) {
                case 0: {                                          // MAIN (0x4c1604)
                    const int was = a.action();
                    a.steer() = 0;
                    a.f(0x820) = a.f(0x7ac); a.f(0x814) = a.f(0x7b0);
                    if (was == 0) {
                        a.action() = 0x10;
                        a.dur() = int(r15(e) % 40u);               // D1
                        if (r15(e) % 1000u < 70u && a.dist() > 5.0f) {   // D2
                            a.action() = 2;
                            a.dur() = int(r15(e) % 30u) + 30;      // D3
                        }
                    }
                    if (a.i(0x72c) == 0) break;
                    auto G = [&] { return 11.5f > a.dist() && S != 7 && a.action() != 0x800 && a.cool() == 0 && gnd && C; };
                    if (G() && r15(e) % 1000u < 90u && in_cone()) {            // A: the staff shot (D4)
                        req(7);
                        a.action() = 2; a.dur() = 87; a.cool() = 87;
                        a.i(0x730) = 1; a.i(0x72c) = 0;
                        if (a.i(0x7a0) != 0) a.i(0x7a0) -= 1;
                        out.attack = true;
                    }
                    if (G() && a.i(0x7a0) == 0 && r15(e) % 1000u < 90u && in_cone()) {   // B: the triple lob (D5)
                        req(305);
                        a.action() = 2; a.dur() = 180; a.cool() = 300;
                        a.i(0x730) = 1; a.i(0x72c) = 0; a.i(0x728) = 6;
                        a.i(0x7a0) = 3; a.i(0x7a4) = 0; a.i(0x87c) = 0;
                        out.attack = true;
                    }
                    if (G() && a.i(0x730) != 0 && tele_roll()) {   // C: the teleport (D6)
                        a.action() = 0x800; a.dur() = 60; a.cool() = 60;
                        a.i(0x728) = 1;
                        a.i(0x730) = 0;
                    }
                    break;
                }
                case 1: case 3:                                    // FADING (0x4c1b93)
                    a.steer() = 1;
                    a.f(0x820) = a.f(0x7ac) * 0.0f;
                    break;
                case 2: {                                          // RELOCATE (0x4c1bb6)
                    a.action() = 0x800; a.steer() = 4;
                    a.f(0x820) = a.f(0x7ac) + a.f(0x7ac);
                    a.f(0x814) = a.f(0x7b0) * 10.0f;
                    if (a.dur() != 0) break;
                    if (float(std::fabs(double(S0.y - P.y))) > 1.0f && e.world) {   // the vertical snap
                        const AiWorld::Ray r1 = e.world->ray({S0.x, P.y + 0.5f, S0.z}, {0.0f, -3.0f, 0.0f});
                        if (r1.hit) {
                            const float Hy = r1.point.y;
                            if (!e.world->ray({S0.x, Hy + 0.1f, S0.z}, {P.x - S0.x, P.y - Hy, P.z - S0.z}).hit && A)
                                A->pos.y = (Hy + 0.1f) - A->height;   // the origin's y
                        }
                    }
                    a.i(0x728) = 3; a.dur() = 60;
                    a.heading = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
                    a.steer() = 0;
                    break;
                }
                case 4: {                                          // CHANT (0x4c1d6f)
                    a.action() = 2;
                    auto H = [&] { return S != 7 && S != 0x12d && a.cool() == 0 && gnd && C; };
                    if (a.i(0x72c) == 0 && H()) {
                        const bool roll = r15(e) % 1000u < 110u;   // D7
                        if (roll || a.dist() > 20.0f) {
                            req(301);
                            a.action() = 1; a.dur() = 90; a.cool() = 90; a.i(0x72c) = 1;
                        }
                    }
                    if (11.5f > a.dist() && H() && a.i(0x730) != 0 && tele_roll()) {   // D8
                        req(304);
                        a.action() = 2; a.dur() = 60; a.cool() = 60;
                        a.i(0x728) = 1; a.i(0x730) = 0;
                    }
                    break;
                }
                case 6: {                                          // LOB (0x4c1f87)
                    if (S != 305) { a.i(0x728) = 0; break; }
                    const int m = e.aso_marker;
                    if (std::uint32_t(m) <= 1u) break;
                    const int k = a.i(0x7a4);
                    const float An = float(double(std::uint32_t(85 - 15 * k))) * 0.0174533f;   // 85 / 70 / 55 deg
                    const float sA = float(std::sin(double(An))), cA = float(std::cos(double(An)));
                    const float sH = float(std::sin(double(a.heading))), cH = float(std::cos(double(a.heading)));
                    out.set_marker = 0;
                    AiBullet b;
                    b.kind = 2; b.effect = m;                      // 272 m_230_tue
                    b.pos = {S0.x + 2.0f * cA * cH, S0.y + (sA - 0.5f), S0.z + 2.0f * cA * sH};
                    b.dir = {cH * cA, sA, sH * cA};
                    b.speed = 0.4f; b.gravity = k == 1 ? 0.02f : k == 2 ? 0.0175f : 0.015f;
                    b.power = 0.0f; b.knock = 0.0f;
                    b.trail_effect = 78; b.trail_period = 1; b.home_period = 1; b.home_rate = 0.0174533f;
                    out.bullets.push_back(b);
                    a.i(0x7a4) += 1;
                    break;
                }
                default: break;                                    // 5: unreachable
            }
            break;
        }
        default: break;
    }

    // FADE (0x4c14c6), every phase
    switch (a.i(0x728)) {
        case 1: {                                                  // FADE_OUT (0x4c217f)
            if (a.dur() == 60) warp_fx();
            a.alpha = float(std::uint32_t(a.dur())) / 60.0f;
            if (a.dur() != 0) break;
            a.i(0x728) = 2; a.alpha = 0.0f;
            const float Dy = (T.y + 0.0f) - S0.y;
            int n = 0;
            bool hit = false;
            do {
                const std::uint32_t w1 = e.rng(), w2 = e.rng();    // D9, D10
                const float r = unit(w1) * 1.5f + 4.5f;
                const float an = float((w2 >> 17) % 360u) * 0.0174533f;
                const float ox = float(std::cos(double(an))) * r, oz = float(std::sin(double(an))) * r;
                a.set_v(0x808, {ox, 0.0f, oz});                    // the relocation offset
                ++n;
                hit = e.world && e.world->ray(S0, {ox + T.x - S0.x, Dy, oz + T.z - S0.z}).hit;
            } while (hit && n < 5);                                // all 5 blocked: the 5th is kept
            a.set_v(0x894, {a.f(0x808) + T.x, a.f(0x80c) + T.y, a.f(0x810) + T.z});
            a.dur() = int(r15(e) % 120u) + 30;                     // D11
            break;
        }
        case 2:                                                    // TRACK (0x4c2893): the point follows T
            a.set_v(0x894, {a.f(0x808) + T.x, a.f(0x80c) + T.y, a.f(0x810) + T.z});
            break;
        case 3: {                                                  // FADE_IN (0x4c28d7)
            if (a.dur() == 60) warp_fx();
            a.alpha = 1.0f - float(std::uint32_t(a.dur())) / 60.0f;
            if (a.dur() != 0) break;
            if (a.i(0x72c) != 0) { a.action() = 0; a.alpha = 1.0f; a.i(0x728) = 0; }
            else { a.action() = 2; a.alpha = 1.0f; req(300); a.i(0x728) = 4; }
            const int v = e.diff_index;                            // DAT_0076bb5c
            if (v <= -1) a.cool() = 90;
            else if (v == 0) a.cool() = int(r15(e) % 30u) + 30;    // D12
            else a.cool() = int(r15(e) % 30u);
            break;
        }
        default: break;
    }
    a.hittable = !(0.5f > a.alpha);                                // +0x60 & 0x80
    return out;
}

}  // namespace cleria::brains
