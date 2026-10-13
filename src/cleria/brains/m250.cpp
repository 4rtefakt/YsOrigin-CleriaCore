// M250 Nipper — brain FUN_0048e190 (handler FUN_0048f3e0 = M100's minus the arena clamp,
// AI index 18). Spec: build/ai_spec/M250.md.
//
// On land it walks at the player (0x20) and, from 3 u, now and then hops forward (vy 0.25 +
// a 0.25 kick along its heading; 0.0015*dist per tick). Within 3 u it runs away (0x800, steer
// away) in 30..59-tick bursts while the player stays within 6 (a 50% roll at a burst's last
// tick may switch back to the 3 u test). Attack A, section 7 (aim 40, hold, Dash -0.15):
// within 6 u, |dy| < 2, cooldown +0x7bc, +/-45 deg; R%1000 < R%max(10d,1) + 10; cooldown
// 300 - 40d. Attack B, section 8, the jump-shot: the target between 0.5 below and 6 above,
// within 7 u; R%1000 < R%max(20d,1) + 30; it aims 22 ticks, then at Marker(1) jumps 0.11 /
// 0.22 / 0.33 by how far the target is above, and the script's bullets fire mid-air; it holds
// at Marker(5) until it lands. Cooldown 75..104.
// In water (+0x389 / +0x38a; M250.md §3.1, §3.7; ENEMY_WATER.md W7) it floats at the surface + 0.7,
// the action is -1 every tick, and it moves only by a drift on knock channel 2 (x 0.975 a tick)
// that the strokes of section 300 feed: a 25% roll every 75..134 ticks, 15 ticks of turning, then
// a kick of 2 x base x run along the heading at the section's Marker(2).
// [not modelled] the ground puff (+0x3c0 & 1).
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

void jump(Actor& a, float v, float cast_jump_v0, AiOut& out) {   // FUN_004773f0
    if (!a.vert.grounded) return;
    const float t = v == 0.0f ? cast_jump_v0 : v;
    a.vert.t = t;
    a.pos.y += t;
    a.vert.grounded = false;
    out.set_own_jump = 1;                                          // +0x9d6
}

void launch(Actor& a, float v, AiOut& out) {
    a.vert.t = v;
    a.pos.y += v;
    a.vert.grounded = false;
    out.set_own_jump = 1;
}

void push1(Actor& a, float heading, float sp) {
    a.mover.k1.x += float(std::cos(double(heading))) * sp;
    a.mover.k1.y += 0.0f;
    a.mover.k1.z += float(std::sin(double(heading))) * sp;
}

namespace {

// The phase switch (M250.md §3.2..§3.6); every return is the engine's `goto TAIL`. true = phase 1 out of the
// water, which enters the tail past its stroke roll.
bool m250_phases(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, AiOut& out, bool gnd, bool inwater) {
    using ai::r15;
    Actor* A = e.actor;
    const int d = difficulty(e);
    int& sub = a.i(0x72c);
    const int mk = e.aso_marker;
    auto aim = [&](float k) {                                      // inlined steer 0 + turn
        if (a.ncrumb() != 0) a.set_v(0x748, a.v(0x758) - e.self);
        else a.set_v(0x748, a.v(0x734));
        ai::turn(a, a.f(0x814) * k);
    };
    auto common = [&](int sec) {
        a.action() = -1;
        a.speed = 0; a.mode() = 0;
        if (A) { A->mover.vx = A->mover.vz = 0; A->mover.dx = A->mover.dz = 0; }
        if (e.section != sec || a.dur() == 0) sub = 0;
    };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0;   // only 32 bytes copied
            a.i(0x728) = 0;
            a.i(0x79c) = a.i(0x7a0) = 0; a.f(0x7ac) = a.f(0x7b0) = 0; a.i(0x7bc) = 0;
            return false;
        case 1:
            a.steer() = 0; a.action() = 0x20;
            ai::idle(a, p, e);
            sub = 0;
            if (inwater) a.action() = 2;
            return !inwater;
        case 2: break;
        default: return false;
    }
    const float dist = a.dist();
    if (dist > 20.0f) ai::disengage(a);
    if (a.i(0x728) == 100) {
        if ((e.section != 0x13a || std::uint32_t(mk) >= 2u) && gnd) a.i(0x728) = 0;
        return false;
    }
    if (sub == 0) {
        const bool close = a.steer() == 3 ? dist < 6.0f : dist < 3.0f;
        if (!close) {
            a.steer() = 0;
            a.action() = inwater ? 1 : 0x20;                       // walk along the heading
            if (!gnd) return false;
            if (dist > 3.0f) {
                const std::uint32_t r = r15(e) % 1000u;
                if (float(r) < float(2 + (inwater ? 0 : 1)) * (dist * 0.5f)) {   // land 3, water 2
                    a.dur() = 0;
                    if (A && !A->vert.grounded) {                  // gnd came from the surface snap: no puff, +0x9d6 = 0
                        A->vert.grounded = true;
                        jump(*A, 0.25f, e.jump_v0, out);
                        out.set_own_jump = 0;
                    } else if (A) jump(*A, 0.25f, e.jump_v0, out);
                    if (A) { A->mover.k1.x += std::cos(a.heading) * 0.25f; A->mover.k1.z += std::sin(a.heading) * 0.25f; }
                }
            }
        } else {
            a.steer() = 3;
            a.action() = inwater ? 2 : 0x800;                      // run, steered away
            if (a.dur() == 0) a.dur() = int(r15(e) % 30u) + 30;
            else if (a.dur() == 1) { if (r15(e) % 1000u < 500u) a.steer() = 0; }
        }
        if (!gnd) return false;
        // Attack A: section 7.
        if (a.i(0x7bc) == 0 && dist < 6.0f && 2.0f > float(std::fabs(double(e.self.y - e.target.y)))) {
            const std::uint32_t m = std::uint32_t(std::max(10 * d, 1));
            const std::uint32_t w1 = e.rng(), w2 = e.rng();
            if ((w1 >> 17) % 1000u < (w2 >> 17) % m + 10u) {
                const Vec3 to = a.v(0x734);
                const float ang = float(std::atan2(double(to.z), double(to.x)));
                if (float(std::fabs(ai::angle_diff(a.heading, ang))) < 0.7853982f) {
                    sub = 1;
                    out.section = 7; out.attack = true;
                    a.action() = -1; a.dur() = 100;
                    a.i(0x7bc) = 300 - 40 * d;
                    if (A) A->mover.vx = A->mover.vz = 0;
                    return false;
                }
            }
        }
        // Attack B: section 8, the jump-shot.
        if (a.cool() == 0 && e.target.y > e.self.y - 0.5f && e.self.y + 6.0f > e.target.y && dist < 7.0f) {
            const std::uint32_t m = std::uint32_t(std::max(20 * d, 1));
            const std::uint32_t w1 = e.rng(), w2 = e.rng();
            if ((w1 >> 17) % 1000u < (w2 >> 17) % m + 30u) {
                const Vec3 to = a.v(0x734);
                const float ang = float(std::atan2(double(to.z), double(to.x)));
                if (float(std::fabs(ai::angle_diff(a.heading, ang))) < 0.7853982f) {
                    sub = 2;
                    out.section = 8; out.attack = true;
                    out.set_marker_block = 3;                      // wiped by the section start
                    a.action() = -1; a.dur() = 45;
                    a.cool() = int(r15(e) % 30u) + 75;
                    a.speed = 0; a.mode() = 0;
                    if (A) A->mover.vx = A->mover.vz = 0;
                }
            }
        }
        return false;
    }
    if (sub == 1) {                                                // section 7 running
        if (mk == 0) aim(0.5f);
        else if (mk == 2) {
            if (inwater && A) {                                    // the Dash(-0.15) folded into the drift (0x48eb26)
                const float vx = float(std::cos(double(a.f(0x7b0)))) * a.f(0x7ac) + A->mover.k1.x * 0.5f;
                const float vz = float(std::sin(double(a.f(0x7b0)))) * a.f(0x7ac) + A->mover.k1.z * 0.5f;
                a.f(0x7ac) = float(std::sqrt(double(vz * vz + vx * vx)));
                a.f(0x7b0) = float(std::atan2(double(vz), double(vx)));
            }
            out.set_marker = 3;
        }
        else if (mk == 3 && d >= 2) aim(0.2f);
        common(7);
        return false;
    }
    if (sub == 2) {                                                // section 8 running
        out.set_own_jump = 1;
        if (mk == 0) aim(0.5f);
        int m2 = mk;
        if (m2 == 1) {
            out.set_marker = 2; m2 = 2;
            const float dy = e.target.y - e.self.y;
            const float v = 1.0f > dy ? 0.11f : 3.0f > dy ? 0.22f : 0.33f;
            if (A) { A->vert.grounded = true; jump(*A, v, e.jump_v0, out); }
        }
        if (m2 == 3) {
            if (0.1f > (A ? A->vert.t : 0.0f)) { out.set_marker = 4; out.set_marker_block = 5; }
        } else if (m2 == 5 && gnd) {
            out.set_marker_block = -1;                             // landed: release Marker(5)
        }
        common(8);
        return false;
    }
    return false;
}

}  // namespace

AiOut m250(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    bool gnd = e.grounded, inwater = false;
    int S = e.section;
    if (a.i(0x7bc) != 0) a.i(0x7bc) -= 1;
    // 0x48e337: wading or submerged (last tick's +0x389 / +0x38a), at or under the surface + 0.8 it floats at
    // the surface + 0.7 with the timer at 0 and counts as grounded for this call.
    if (a.in_water || a.wading) {
        inwater = true;
        if (A && a.w320() + 0.8f >= e.self.y) {
            A->pos.y = a.w320() + 0.7f - A->height;
            A->vert.t = 0.0f;
            if (S == 3 && ai::request(S, 2, true, e)) out.section = 2;
            out.set_own_jump = 1;
            gnd = true;
        }
    }
    const bool no_roll = m250_phases(a, p, e, out, gnd, inwater);
    if (out.section >= 0 && out.section != 2) ai::request(S, out.section, false, e);   // the attack's 7 / 8
    // TAIL (M250.md §3.7 [BYTES 0x48ef8b..]), every phase.
    int mk = out.set_marker > -1000 ? out.set_marker : e.aso_marker;
    if (!no_roll && inwater && a.i(0x79c) == 0) {                  // the swim-stroke roll
        if (r15(e) % 1000u < 250u && S != 7 && S != 8) {
            out.set_marker = mk = 4;
            a.i(0x7a0) = 0;
            a.i(0x79c) = int(r15(e) % 60u) + 75;
        }
    }
    if (S == 300) {
        if (mk == 4) {                                             // 15 ticks of turning at twice the rate
            ai::steer(a, e);
            ai::turn(a, a.f(0x814) + a.f(0x814));
            a.i(0x7a0) += 1;
            if (std::uint32_t(a.i(0x7a0)) >= 15u) out.set_marker = mk = 5;
        } else if (mk == 5) {                                      // restart the section, on to its Marker(1)
            out.section = 300; out.fast_forward = 1;
        }
        if (mk == 2) {                                             // the stroke's kick
            const float s = (a.f(0x818) + a.f(0x818)) * a.f(0x820);
            const float vx = float(std::cos(double(a.f(0x7b0)))) * a.f(0x7ac) + float(std::cos(double(a.heading))) * s;
            const float vz = float(std::sin(double(a.heading))) * s + float(std::sin(double(a.f(0x7b0)))) * a.f(0x7ac);
            a.f(0x7ac) = float(std::sqrt(double(vz * vz + vx * vx)));
            a.f(0x7b0) = float(std::atan2(double(vz), double(vx)));
            out.set_marker = 3;
        }
    }
    if (!inwater) {
        a.i(0x79c) = 0;
    } else {                                                       // the drift, on knock channel 2
        if (a.i(0x79c) != 0) a.i(0x79c) -= 1;
        if (S != 300 && S != 3 && S != 2 && S != 7 && S != 8) {
            // FUN_00446550(300, 1) then FUN_00446680(1): the skip runs on whatever section is live, also one
            // the request could not replace (a hit reaction 4 ends within the tick) — as written, not seen [H].
            if (ai::request(S, 300, true, e)) out.section = 300;
            out.fast_forward = 1;
        }
        a.action() = -1;
        const float v = a.f(0x7ac) * 0.975f;
        a.f(0x7ac) = v;
        if (A) {
            A->mover.k2.x += float(std::cos(double(a.f(0x7b0)))) * v;
            A->mover.k2.z += float(std::sin(double(a.f(0x7b0)))) * v;
            A->mover.vx = A->mover.vz = 0; A->mover.dx = A->mover.dz = 0;
        }
        a.speed = 0; a.mode() = 0;
    }
    if (A && S == 0x13a && A->vert.grounded && mk == 1) launch(*A, a.f(0x7dc) * 0.1f, out);
    if (inwater && gnd) out.set_own_jump = 1;
    return out;
}

}  // namespace cleria::brains
