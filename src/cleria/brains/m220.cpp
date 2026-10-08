// M220 Chryolos — the normal brain FUN_004b5650 (handler FUN_004ac470, AI index 15). Spec:
// build/ai_spec/M220.md (§3; §3B is the boss-fight brain FUN_004ab840 the handler runs while
// DAT_0076bb8c != 0 — Pictimos's summons).
//
// A fast runner (base speed doubled at init) re-issuing a 1-tick run at the target. Inside 3.75 u, facing it
// within 45 deg, grounded, level, off cooldown: the slash (section 7: a 0.3 / 0.2 hop at Marker 1, two hits),
// 63.5% a tick when the target's back is turned, else 24%; a group-tagged one re-slashes at once with odds
// 1 / max(4 - d, 2). Only while the target faces it (and not every (15 - 3d)-th tick): inside 2.55 a backstep
// (within 50 ticks of a forward hop) or a forward hop, and inside 5 a strafe — standing, turning to face it and
// backpedalling on knock channel 2 at walk speed, with 10% side hops.
// [not modelled] the keep-out box restore (FUN_0042dc90); the handler's boss death burst (SE 11 + EF2 67 when
// +0x130 & 0x2000). The boss-fight brain FUN_004ab840 is m220b.cpp (m220_boss). [approx] the side hop's unreachable "stale" side takes the target vector.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m220(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    if (e.boss_fight) return m220_boss(a, p, e);                   // 0x4ac8cc: g_flags[156] != 0 (m220b.cpp)
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    Actor* A = e.actor;
    const float Th = e.target_heading;
    const int Psec = e.target_section;
    const Vec3 v{a.f(0x734), 0.0f, a.f(0x73c)};
    const float pd = std::sqrt(v.x * v.x + 0.0f + v.z * v.z);
    bool can_atk = true;
    if (std::fabs(double(e.target.y - e.self.y)) >= 2.0 && A && std::fabs(double(e.target_floor - A->floor_y)) >= 2.0)
        can_atk = false;
    const int d = difficulty(e);
    const bool tag = e.group_tag;
    const int S = e.section;
    const int marker = e.aso_marker;
    const bool gnd = e.grounded;
    auto ray_hit = [&](const Vec3& o, const Vec3& dl) { return e.world && e.world->ray(o, dl).hit; };
    auto adiff = [](float h, float an) { return float(std::fabs(ai::angle_diff(h, an))); };
    const int k = d < 0 ? 12 : 120;

    switch (a.phase()) {
        case 0:                                                    // INIT (0x4b73d0): 8 floats
            ai::init(a, p, e);
            for (int off = 0x834; off <= 0x850; off += 4) a.f(off) = 0.0f;
            a.f(0x818) = a.f(0x818) + a.f(0x818);                  // base speed doubled
            return out;
        case 1:
            ai::idle(a, p, e);
            return out;
        case 2: break;
        default: return out;
    }
    if (a.dist() > 20.0f) ai::disengage(a);
    if (a.action() == 0) {
        if (a.i(0x728) != 100 && a.i(0x7bc) != 1) { a.action() = 0x400; a.i(0x728) = 0; }
        a.i(0x7bc) = 0;
    }
    const float h = a.heading;
    const float a_to = float(std::atan2(double(v.z), double(v.x)));
    // HOP(window): the backstep within `window` counter ticks of a forward hop, else a forward hop.
    auto hop = [&](std::uint32_t window, bool case3) {
        Vec3 B{-v.x, -0.0f, -v.z};
        const float bl = std::sqrt(B.x * B.x + B.y * B.y + B.z * B.z);
        B = bl == 0.0f ? Vec3{} : Vec3{B.x / bl, B.y / bl, B.z / bl};
        const float ang = unit(e.rng()) - 0.5f;                    // RAW
        const float bx = 0.5f * B.x, by = 0.5f * B.y, bz = 0.5f * B.z;
        const float c = float(std::cos(double(ang))), s = float(std::sin(double(ang)));
        const Vec3 V{s * bz + c * bx, by, c * bz - s * bx};
        a.set_v(0x7f0, V);
        const Vec3 O{e.self.x, e.self.y + 1.0f, e.self.z};
        if (std::uint32_t(a.i(0x72c) - 1) < window && !ray_hit(O, {4 * V.x, 4 * V.y, 4 * V.z}) &&
            ray_hit({O.x + 9 * V.x, O.y + 9 * V.y, O.z + 9 * V.z}, {0, -3.5f, 0})) {
            out.section = 3;                                       // gated
            a.i(0x728) = 2; a.i(0x72c) = 0; a.action() = 2; a.dur() = 20;   // BACKSTEP
            return;
        }
        const Vec3 F{0.9f * float(std::cos(double(h))), 0, 0.9f * float(std::sin(double(h)))};   // 0x6bd3e0
        if (!ray_hit(O, {4 * F.x, 0, 4 * F.z}) && ray_hit({O.x + 8 * F.x, O.y, O.z + 8 * F.z}, {0, -3.5f, 0})) {
            out.section = 3;
            a.i(0x728) = 1; a.i(0x72c) = 1; a.action() = 2; a.dur() = 20;   // FORWARD HOP
        } else if (case3) { a.action() = 0; a.i(0x728) = 0; }
    };
    auto slash_dur = [&] { return 80 + 20 * (3 - d) / 5; };      // 92 - 4d

    switch (a.i(0x728)) {
        case 0: {                                                  // DECIDE (0x4b5938)
            if (a.i(0x72c) != 0) a.i(0x72c) += 1;
            if (3.75f > pd && S != 7 && a.cool() == 0 && gnd && can_atk && adiff(h, a_to) < 0.785398f) {
                bool hit;
                if (adiff(Th, h) < 1.0472f) hit = r15(e) % 1000u < 520u || r15(e) % 1000u < 240u;
                else hit = r15(e) % 1000u < 240u;
                if (hit) {                                         // SLASH (0x4b5b07)
                    out.section = 7; out.attack = true;            // gated
                    a.action() = 1;
                    a.dur() = slash_dur();
                    a.cool() = a.dur() + 60 + int(r15(e) % 30u) - 20 * d;
                    a.i(0x7bc) = 1; a.i(0x728) = 4;
                }
            }
            if (a.i(0x728) != 0) break;
            if (std::uint32_t(a.i(0x87c)) % std::uint32_t(15 - 3 * d) == 0u) break;
            if (adiff(h, a_to) >= 0.785398f) break;
            if (adiff(Th, h) <= 1.0472f) break;                    // only while the target faces it
            if (2.55f > pd && int(r15(e) % 1000u) < k * d + 40) hop(50u, false);
            if (5.0f > pd && Psec != 9 && int(r15(e) % 1000u) < 2 * k * d + 80) {
                a.action() = -1; a.i(0x728) = 3;                   // STRAFE (overrides a hop)
            }
            break;
        }
        case 1:                                                    // FORWARD HOP
            if (marker == 1) {
                if (!gnd) break;
                if (S == 3 && A) { push1(*A, h, 0.9f); jump(*A, 0.27f, e.jump_v0, out); a.cool() = 20; }
            }
            if (gnd && S != 3 && a.cool() == 0) { a.action() = 0; a.i(0x728) = 0; a.i(0x7bc) = 1; }
            break;
        case 2:                                                    // BACKSTEP
            if (marker == 1) {
                if (!gnd) break;
                if (S == 3 && A) {
                    A->mover.k1.x += a.f(0x7f0); A->mover.k1.y += a.f(0x7f4); A->mover.k1.z += a.f(0x7f8);
                    jump(*A, 0.17f, e.jump_v0, out); a.cool() = 20;
                }
            }
            if (gnd && S != 3 && a.cool() == 0) { a.action() = 0; a.i(0x728) = 0; }
            break;
        case 3: {                                                  // STRAFE (action -1)
            if (a.i(0x72c) != 0) a.i(0x72c) += 1;
            if (adiff(h, a_to) >= 0.785398f || adiff(Th, h) <= 1.0472f || pd >= 6.0f) {
                a.action() = 0; a.i(0x728) = 0; break;
            }
            if (2.55f > pd && std::uint32_t(a.i(0x87c)) % std::uint32_t(15 - 3 * d) != 0u &&
                int(r15(e) % 1000u) < k * d + 40) {
                hop(40u, true);
                break;
            }
            ai::stop(a, 0); ai::steer(a, e); ai::turn(a, a.f(0x814));
            const float s = a.f(0x81c) * a.f(0x818);
            a.mode() = 1;
            const double hb = double(a.heading + 3.1415927f);
            if (A) { A->mover.k2.x += float(std::cos(hb)) * s; A->mover.k2.z += float(std::sin(hb)) * s; }
            if (std::uint32_t(a.i(0x87c)) % 10u != 0u && d <= 0) break;
            if (r15(e) % 1000u >= 100u) break;
            a.i(0x7a4) = 0;
            const float hh = a.heading;
            const Vec3 L{0.6f * float(std::cos(double(hh + 1.3f))), 0, 0.6f * float(std::sin(double(hh + 1.3f)))};
            const Vec3 R{0.6f * float(std::cos(double(hh - 1.3f))), 0, 0.6f * float(std::sin(double(hh - 1.3f)))};
            const Vec3 Lf{e.self.x + 8 * L.x, e.self.y, e.self.z + 8 * L.z};
            if (!ray_hit(e.self, {3 * L.x, 0, 3 * L.z}) && ray_hit(Lf, {0, -3.5f, 0})) a.i(0x7a4) += 1;
            if (!ray_hit(e.self, {3 * R.x, 0, 3 * R.z}) && ray_hit(Lf, {0, -3.5f, 0})) a.i(0x7a4) += 10;   // LEFT floor (literal)
            if (a.i(0x7a4) == 0) {                                 // the forward-hop commit, no probes
                out.section = 3; a.i(0x728) = 1; a.i(0x72c) = 1; a.action() = 2; a.dur() = 20;
                break;
            }
            a.i(0x7a0) = int((e.rng() >> 17) & 1u);
            a.action() = 2; out.section = 3; a.dur() = 20;
            a.i(0x728) = 5; a.i(0x72c) = 0;
            break;
        }
        case 4: {                                                  // SLASH in progress
            if (marker == 1) {
                if (!gnd) break;
                if (S == 7 && A) {
                    push1(*A, h, 0.3f);
                    jump(*A, 0.2f, e.jump_v0, out);
                    a.cool() = (int(r15(e) % 30u) + 20 * (3 - d)) / 4 + a.dur();
                }
            }
            if (!gnd || S == 7) break;
            if (tag) {
                if (r15(e) % std::uint32_t(std::max(4 - d, 2)) == 0u) {
                    a.set_v(0x748, a.ncrumb() != 0 ? a.v(0x758) - e.self : a.v(0x734));
                    a.heading = rotate_toward(a.heading, float(std::atan2(double(a.f(0x750)), double(a.f(0x748)))),
                                              float(std::max(2 * d, 0) + 10) * a.f(0x814));
                    out.section = 7; out.attack = true;
                    a.action() = 1; a.dur() = slash_dur();
                    a.cool() = a.dur() + 60 + int(r15(e) % 30u) + int(std::uint32_t(a.cool()) / 3u) - 20 * d;
                    a.i(0x7bc) = 1; a.i(0x728) = 4;
                } else { a.action() = 0; a.i(0x728) = 0; }
            }
            if (a.cool() == 0) { a.action() = 0; a.i(0x728) = 0; }
            break;
        }
        case 5: {                                                  // SIDE HOP
            if (marker == 1 && !gnd) break;
            if (marker == 1 && S == 3 && A) {
                const int m = a.i(0x7a4);
                Vec3 J;
                if (m == 1 || m == 10 || m == 11) {
                    const float side = m == 1 ? a.heading + 1.3f : m == 10 ? a.heading - 1.3f
                                     : (a.i(0x7a0) == 0 ? a.heading - 1.3f : a.heading + 1.3f);
                    J = {0.6f * float(std::cos(double(side))), 0, 0.6f * float(std::sin(double(side)))};
                } else {
                    J = {0.6f * v.x, 0, 0.6f * v.z};               // [approx] the stale target vector
                }
                A->mover.k1.x += J.x; A->mover.k1.z += J.z;
                jump(*A, 0.17f, e.jump_v0, out); a.cool() = 20;
            }
            if (gnd && S != 3 && a.cool() == 0) { a.action() = 0; a.i(0x728) = 0; }
            break;
        }
        case 100:
            if (S == 0x13a) break;
            if (gnd) { a.action() = 0x400; a.i(0x728) = 0; }
            break;
        default: break;
    }
    // TAIL (0x4b72e3): the jump helper's launch, K 0.1, push +0x7e0
    if (S == 0x13a && gnd && marker == 1 && A) {
        launch(*A, a.f(0x7dc) * 0.1f, out);
        push1(*A, a.heading, a.f(0x7e0));
    }
    return out;
}

}  // namespace cleria::brains
