// M540 Perrie — handler FUN_00499c90 (M100's minus the arena clamp, AI index 47), which runs
// brain FUN_00497bb0 while the boss-fight flag DAT_0076bb8c == 1, else FUN_00498830.
// Spec: build/ai_spec/M540.md.
//
// Normal brain: Rezalior's fledgling, Celny's (M370) design with a fuse. Birth (phase 4) as
// Celny's; engaged it reads where the target faces: faced within 5 u it flees (walk x1.5) or,
// walled in, runs; back turned within 4 u (lockout clear) it runs AWAY (toward when tagged)
// for 68 / 128 / 188 ticks; back turned beyond 2 u it sneaks up; else it stands or wanders.
// The run section 1 is a hop loop laying fire puffs (EF2 251). A hit of >= 1 damage lights the
// fuse (any phase): it chases at walk x2 (stopping for the last 30 / 15 / 0 counts), blinks
// faster, glows (EF2 321) 19 ticks before the end, and at 360 explodes (EF2 267, SE 437) and
// removes itself. A wall it can clear at +H makes it hop (FUN_004773f0(H x 0.11), push 0.2).
// Boss brain (the Gelaldy fight's summons): HP 350, DEF 50; homes within 15 u, else walks a
// radius-20 ring about the world origin toward the target; runs when close and level; a fuse
// of 800 - R%400 ticks (DEF 999 and the bomb EF2 410 from 660).
// [not modelled] the effects' particle-borne hits (EFFECTHIT.md: the explosion's power 15
// reaction 2, the fire puffs, the bomb — there is no EF2 particle simulation yet); the blink
// (+0x264..+0x274); the glow / bomb attachment; the keep-out box restore (FUN_0042dc90); the
// x1.6 surface launch factor; the boss brain's lava hop (surface byte +0x4ba).
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

AiOut perrie_boss(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    Actor* A = e.actor;
    if (A) { A->flags12c |= 0x4000000u; A->flags130 |= 0x4001u; }
    const bool can = !(e.target_grounded && std::fabs(e.target.y - e.self.y) >= 0.5f);
    const Vec3 dT{e.target.x - e.self.x, e.target.y - e.self.y, e.target.z - e.self.z};
    const float d2 = dT.x * dT.x + dT.y * dT.y + dT.z * dT.z;
    if (225.0f > d2) a.steer() = 2;
    else {
        auto deg = [](float z, float x) {
            float v = (float(std::atan2(double(z), double(x))) - 1.5707964f) * 57.295776f;
            if (0 > v) v += 360.0f; else if (v >= 360.0f) v -= 360.0f;
            return v;
        };
        const float aT = deg(e.target.z, e.target.x), aS = deg(e.self.z, e.self.x);
        float mid = (aS + aT) * 0.5f;
        if (std::fabs(aS - mid) > 90.0f && std::fabs(aT - mid) > 90.0f) { mid += 180.0f; if (mid >= 360.0f) mid -= 360.0f; }
        const float yaw = -(mid * 0.017453292f);
        a.set_v(0x894, {20.0f * std::sin(yaw), 0.5f, 20.0f * std::cos(yaw)});   // D3DX yaw of (0, 0, 20)
        a.steer() = 4;
    }
    if (A) A->flags12c &= ~0x800000u;
    const bool E = e.diff_easy, N = !E && e.diff_index == 0;
    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.phase() = 4;
            a.alpha = 0.5f; if (A) A->scale = 0.5f;
            a.f(0x7ac) = a.f(0x81c) * 3.0f;
            a.f(0x7b0) = a.f(0x814) * 3.0f;
            a.steer() = 0;
            a.i(0x7a8) = int(r15(e) % 400u);
            a.i(0x8a4) = 0x100;                                    // byte +0x8a5 = 1: the bomb pending
            if (A) { A->hp = 350.0f; A->def = 50.0f; }
            return out;
        case 4: {
            out.set_durable = 1;
            if (a.action() == 0 && std::uint32_t(a.i(0x79c)) >= 1u) { a.phase() = 2; a.action() = 0; a.i(0x728) = 0; }
            if (e.section != 8 && a.i(0x79c) == 0) {
                out.section = 8;
                a.action() = 1; a.dur() = 80; a.cool() = 80; a.i(0x79c) = 1;
                a.hittable = true;
                return out;
            }
            const float s = float(std::uint32_t(a.i(0x7a0))) / 80.0f + 0.5f;
            a.alpha = s; if (A) A->scale = s;
            if (std::uint32_t(a.i(0x7a0)) <= 40u) a.i(0x7a0) += 1;
            a.hittable = true;
            return out;
        }
        case 1:
            ai::idle(a, p, e);
            return out;
        case 2: break;
        default: return out;
    }
    if (std::uint32_t(a.i(0x7a8)) >= 660u) {
        a.f(0x7ac) = 4.0f > d2 ? 5.5f : 7.0f;
        if (A) A->def = 999.0f;
        if ((a.i(0x8a4) >> 8 & 0xff) == 1) {
            a.i(0x8a4) &= ~0xff00;
            AiEffect bb{410, e.self, 1.0f, true};                   // bos\bs_03_bomb1.eft, attached
            bb.power = 10.0f;
            out.effects.push_back(bb);
        }
    }
    if (std::uint32_t(a.i(0x7a8)) >= 800u) {
        out.sounds.push_back(0x1b5);                               // SE 437 zako\z_bomb
        a.i(0x7a8) = 0;
        if (e.world && A) e.world->remove(*A);
    } else a.i(0x7a8) += 1;
    bool walk = false;
    if (a.action() == 0) { a.action() = 0x10; a.i(0x728) = 0; walk = true; }
    else if (a.i(0x728) == 0) walk = true;
    else if (a.i(0x728) == 100) {
        if ((e.section != 0x13a || std::uint32_t(e.aso_marker) >= 2u) && e.grounded) { a.action() = 0; a.i(0x728) = 0; }
    }
    if (walk) {
        a.f(0x81c) = a.f(0x7ac) * 0.8f;
        a.f(0x7ac) = 225.0f > d2 ? 5.5f : 7.0f;
        a.f(0x814) = e.section == 1 ? a.f(0x7b0) * 0.5f : a.f(0x7b0);
        if (e.section != 4 && std::uint32_t(a.i(0x7a8)) <= 400u && 5.0f > a.dist() && e.section != 1 &&
            a.action() == 0x10 && a.cool() == 0 && e.grounded && can) {
            a.action() = 0x400;
            a.dur() = E ? 112 : N ? 164 : 216;
            a.cool() = a.dur() + (E ? 250 : N ? 200 : 150);
        }
        // [not modelled] the lava hop (surface +0x4ba == 0x0d and the preamble's floor segment).
    }
    if (A && launch_gate(e)) {                                     // JUMP: K 0.15, push +0x7e0
        launch(*A, a.f(0x7dc) * 0.15f, out);
        push1(*A, a.heading, a.f(0x7e0));
    }
    return out;
}

AiOut perrie(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const Vec3 to = a.v(0x734);
    const float distH = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    const bool tag = e.group_tag;
    const int self_id = A ? A->instance_id : -1;
    if (a.i(0x854) == 0) a.i(0x854) = self_id;
    const bool E = e.diff_easy, N = !E && e.diff_index == 0;
    auto ray = [&](const Vec3& o, const Vec3& d) { return e.world && e.world->ray(o, d).hit; };
    auto set_scale = [&](float s) { a.alpha = s; if (A) A->scale = s; };

    // FUSE (every phase).
    auto fuse = [&]() -> AiOut {
        std::uint32_t c = std::uint32_t(a.i(0x7c4));
        if (c == 0) {
            if (!e.took_damage) return out;
            a.i(0x7c4) = 1; a.i(0x730) = 10;                       // light: force sub 10
        }
        a.i(0x7c8) += 1;
        c = std::uint32_t(a.i(0x7c4)) + 1; a.i(0x7c4) = int(c);
        if (c >= 360u && (a.i(0x8a4) & 0xff) == 0) {               // EXPLODE
            AiEffect ex{267, e.self, 1.0f, false};                  // enemy\m_bom01.eft
            ex.power = 15.0f; ex.knock = 0.0f; ex.rtype = 2;
            out.effects.push_back(ex);
            out.sounds.push_back(0x1b5);                           // SE 437 zako\z_bomb
            if (e.world && A) e.world->remove(*A);
            a.i(0x8a4) |= 1; c = 0; a.i(0x7c4) = 0;
        }
        if ((a.i(0x8a4) & 0xff00) == 0 && 360u - c < 20u) {       // GLOW (attached)
            AiEffect gl{321, e.self, 1.0f, true};                   // bos\bs_01_leser_tame.eft
            gl.power = 10.0f;
            out.effects.push_back(gl);
            a.i(0x8a4) |= 0x100;
            c = std::uint32_t(a.i(0x7c4));
        }
        if (std::uint32_t(a.i(0x7c8)) > ((360u - c) >> 4) + 2u) a.i(0x7c8) = 0;   // BLINK (not drawn)
        return out;
    };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.phase() = 4;
            set_scale(0.5f);
            a.f(0x7ac) = a.f(0x81c);
            a.f(0x7b0) = a.f(0x814);
            a.i(0x7bc) = 0; a.i(0x7a0) = 0; a.i(0x7a4) = 0; a.i(0x8a4) = 0;
            if (A) A->flags12c |= 0x80000u;                        // stun-immune
            a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x730) = 0;
            return fuse();
        case 4: {
            if (a.i(0x854) == self_id) {
                a.hittable = true;
                set_scale(1.0f);
                a.phase() = 2; a.action() = 0; a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x730) = 0;
                return fuse();
            }
            out.set_durable = 1;
            if (a.action() == 0 && std::uint32_t(a.i(0x7bc)) >= 1u) {
                a.phase() = 2; a.action() = 0; a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x730) = 0;
            }
            std::uint32_t n;
            if (e.section != 8 && a.i(0x7bc) == 0) {
                out.section = 8;
                a.action() = 1; a.dur() = 100; a.cool() = 100;
                a.i(0x7bc) = 1; a.f(0x7b4) = 0.18f; a.i(0x87c) = 0;
                n = 0;
            } else {
                n = std::uint32_t(a.i(0x87c));
                set_scale(std::min(60.0f, float(n)) / 120.0f + 0.5f);
            }
            const float k = std::max(0.0f, (60.0f - float(n)) / 60.0f) * 0.075f;
            if (e.aso_marker == 1 && e.grounded && A) {            // dead with the shipped ASO (no Marker)
                if (a.i(0x7bc) == 2) { A->vert.t += a.f(0x7b4); A->pos.y += A->vert.t; a.f(0x7b4) *= 0.5f; }
                else a.i(0x7bc) += 1;
                A->vert.grounded = false; out.set_own_jump = 1;
            }
            if (A) { A->mover.k2.x += std::cos(a.f(0x7f0)) * k; A->mover.k2.z += std::sin(a.f(0x7f0)) * k; }
            a.hittable = std::uint32_t(a.i(0x87c)) >= 0x40u;
            return fuse();
        }
        case 1:
            a.f(0x81c) = a.f(0x7ac); a.f(0x814) = a.f(0x7b0);
            ai::idle(a, p, e);
            a.i(0x728) = 0; a.i(0x72c) = 0; a.i(0x730) = 0;
            return fuse();
        case 2: break;
        default: return fuse();
    }
    if (a.dist() > 20.0f) ai::disengage(a);                       // the tick continues
    int& sub = a.i(0x730);
    switch (a.i(0x72c) != 0 ? -1 : sub) {                          // +0x72c != 0: dead
        case 0: {
            if (a.i(0x7a0) != 0) a.i(0x7a0) -= 1;
            if (a.i(0x7a4) != 0) a.i(0x7a4) -= 1;
            if (a.dur() != 0 && a.steer() != 4) break;
            a.f(0x81c) = a.f(0x7ac); a.f(0x814) = a.f(0x7b0);
            auto norm = [](float x, float z) {
                const float l = std::sqrt(x * x + z * z);
                return l == 0 ? Vec3{} : Vec3{x * (1.0f / l), 0, z * (1.0f / l)};
            };
            const Vec3 f = norm(std::cos(e.target_heading), std::sin(e.target_heading));
            const Vec3 r = norm(e.self.x - e.target.x, e.self.z - e.target.z);
            const float dot = (r.x * f.x + 0.0f) + r.z * f.z;
            const bool fA = (dot >= 0.25f && 5.0f > distH) || a.i(0x7a0) != 0;
            const bool fC = 0.25f > dot && distH > 2.0f;
            const bool fB = a.i(0x7a4) == 0 && 0.25f > dot && 4.0f > distH;
            if (fA) {
                if (!ray(e.self, {std::cos(a.heading), 0, std::sin(a.heading)})) {
                    a.action() = 0x10; a.steer() = 3;
                    const int rr = int(r15(e) % 15u) + 20;
                    a.dur() = rr; a.cool() = rr;
                    a.f(0x81c) = a.f(0x7ac) * 1.5f; a.f(0x814) = a.f(0x7b0) * 1.5f;
                } else sub += 1;
            } else if (fB) {
                sub += 1;
            } else if (fC) {
                a.action() = 0x10; a.steer() = 0; a.dur() = 15; a.cool() = 15;
            } else if (a.dur() == 0) {
                if (r15(e) % 10u >= 6u) { a.action() = 1; a.steer() = 1; a.dur() = 60; }
                else {
                    for (int k = 0, tries = 0;;) {
                        const int off = int(r15(e) % 120u);
                        const float an = float(k - 60 + off) * 3.14159012f / 180.0f + a.heading;
                        const Vec3 D{2.0f * std::cos(an), 0, 2.0f * std::sin(an)};
                        if (!ray(e.self, D)) {
                            a.action() = 0x10; a.steer() = 4;
                            a.set_v(0x894, {e.self.x + D.x, e.self.y + 0.0f, e.self.z + D.z});
                            a.cool() = 0;
                            a.dur() = int(r15(e) % 15u) + 85;
                            a.f(0x814) = a.f(0x7b0);
                            a.f(0x81c) = a.f(0x7ac) * 0.8f;
                            break;
                        }
                        k += 120;
                        if (++tries >= 3) break;
                    }
                }
            }
            break;
        }
        case 1:                                                    // run (the hop-run section)
            a.action() = 0x400;
            a.steer() = tag ? 0 : 3;
            a.dur() = E ? 68 : N ? 128 : 188;
            a.cool() = a.dur() + (E ? 250 : N ? 200 : 150);
            sub = 2;
            a.f(0x814) = a.f(0x7b0) * 0.5f;
            break;
        case 2:
            if (a.dur() == 0) { sub = 0; a.f(0x814) = a.f(0x7b0); a.i(0x7a4) = E ? 120 : N ? 90 : 60; }
            break;
        case 10: {                                                 // the fuse is lit: chase
            out.set_durable = 1;
            a.steer() = 0;
            const int d = difficulty(e);
            const std::uint32_t L = d < 0 ? 30u : d == 0 ? 15u : 0u;
            a.action() = L < 360u - std::uint32_t(a.i(0x7c4)) ? 0x10 : 1;
            a.f(0x81c) = a.f(0x7ac) * 2.0f; a.f(0x814) = a.f(0x7b0) * 2.0f;
            a.dur() = 60;
            break;
        }
        default: break;
    }
    // TAIL (phase 2): A. the wall hop; B. the jump helper's launch (K 0.11, push +0x7e0).
    if (A && e.grounded && (e.section == 0 || e.section == 1)) {
        const float L = e.radius > 0.5f ? e.radius + 0.5f : 1.0f;
        const Vec3 D{std::cos(a.heading) * L, 0, std::sin(a.heading) * L};
        if (ray(e.self, D) && !ray({e.self.x, e.self.y + a.f(0x828), e.self.z}, D)) {
            jump(*A, a.f(0x828) * 0.11f, e.jump_v0, out);
            push1(*A, a.heading, a.f(0x818) * 5.0f);
        }
    }
    if (A && launch_gate(e)) {
        launch(*A, a.f(0x7dc) * 0.11f, out);
        push1(*A, a.heading, a.f(0x7e0));
    }
    return fuse();
}

}  // namespace

AiOut m540(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    return e.boss_fight ? perrie_boss(a, p, e) : perrie(a, p, e);
}

}  // namespace cleria::brains
