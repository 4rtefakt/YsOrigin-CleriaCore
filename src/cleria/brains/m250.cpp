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
// [not modelled] water (+0x389/+0x38a: the surface snap, the swim section 300, the drift on
// knock channel 2 — the room collision has no water set, so it is always on land); the
// keep-out box restore (FUN_0042dc90); the ground puff (+0x3c0 & 1).
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

AiOut m250(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const int d = difficulty(e);
    const bool gnd = e.grounded;
    if (a.i(0x7bc) != 0) a.i(0x7bc) -= 1;
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
            return out;
        case 1:
            a.steer() = 0; a.action() = 0x20;
            ai::idle(a, p, e);
            sub = 0;
            return out;
        case 2: break;
        default: return out;
    }
    const float dist = a.dist();
    if (dist > 20.0f) ai::disengage(a);
    if (a.i(0x728) == 100) {
        if ((e.section != 0x13a || std::uint32_t(mk) >= 2u) && gnd) a.i(0x728) = 0;
        return out;
    }
    if (sub == 0) {
        const bool close = a.steer() == 3 ? dist < 6.0f : dist < 3.0f;
        if (!close) {
            a.steer() = 0;
            a.action() = 0x20;                                     // walk along the heading
            if (!gnd) return out;
            if (dist > 3.0f) {
                const std::uint32_t r = r15(e) % 1000u;
                if (float(r) < float(2 + 1) * (dist * 0.5f)) {     // land: k = 3
                    a.dur() = 0;
                    if (A) jump(*A, 0.25f, e.jump_v0, out);
                    if (A) { A->mover.k1.x += std::cos(a.heading) * 0.25f; A->mover.k1.z += std::sin(a.heading) * 0.25f; }
                }
            }
        } else {
            a.steer() = 3;
            a.action() = 0x800;                                    // run, steered away
            if (a.dur() == 0) a.dur() = int(r15(e) % 30u) + 30;
            else if (a.dur() == 1) { if (r15(e) % 1000u < 500u) a.steer() = 0; }
        }
        if (!gnd) return out;
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
                    return out;
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
        return out;
    }
    if (sub == 1) {                                                // section 7 running
        if (mk == 0) aim(0.5f);
        else if (mk == 2) out.set_marker = 3;
        else if (mk == 3 && d >= 2) aim(0.2f);
        common(7);
        return out;
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
        return out;
    }
    return out;
}

}  // namespace cleria::brains
