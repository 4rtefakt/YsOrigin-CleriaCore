// M200 Venueba — brain FUN_0047db90 (handler FUN_0047f3b0 = M100's minus the arena clamp,
// AI index 13). Spec: build/ai_spec/M200.md.
//
// A slime. Engaging, it rolls aggressive (d*16+50 %, chase in 30-tick run legs while over
// 4.5 u away, up to d*5+15 legs, then lob) or wary (a side-step whose floor probe is a
// straight-down ray from its own origin — on solid ground it always hits, so the wary path
// falls back: 50% the splash, else 60 ticks turning toward the ABSOLUTE world point
// (cos a, 0, sin a)). The lob (section 7): a 15-tick turn, then at marker 2 one glob (three
// when d > 1, effect 315, speed 0.12, gravity 0.004) tilted 0.125*dist rad from vertical.
// The splash (section 8): at marker 3 the poison-hit effect 255 one unit ahead. Every tick
// in its hit reaction refills an "aggression credit" (+0x7c4 = d+2) spent on chase-and-lob.
// Its slime puddles (FUN_0058a5f0, flag B; 6 draws each of the shared xorshift, taken by the host): six in an arc
// ahead at the splash, one under it every 20th tick while it walks or runs, one near it every 10th tick of its hit
// reaction (AiOut::splats; M200.md §3.7b, §3.8).
// [not modelled] the globs' damage (the GoXx path is untraced).
#include <cmath>

#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m200(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;
    const int d = difficulty(e);
    const Vec3 to = a.v(0x734);
    const float hdist = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);
    const bool vert_ok = !(std::fabs(e.target.y - e.self.y) >= 2.0f && e.target_grounded);
    const bool tagged = e.group_tag;
    int S = e.section;
    const int M = e.aso_marker;
    int& sub = a.i(0x72c);
    auto common_reset = [&] { sub = 0; a.i(0x728) = 0; a.i(0x730) = 0; };
    auto arm = [&] {                                               // 0x47e505
        const int r = int(r15(e) % 15u);
        sub += 1;
        a.cool() = a.dur() + r;
    };
    auto exit_attack = [&] { sub = a.i(0x79c); a.i(0x7bc) = 0; };

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.i(0x7bc) = 0;
            common_reset();
            break;
        case 1:
            ai::idle(a, p, e);
            common_reset();
            break;
        case 2:
            if (a.dist() > 20.0f) ai::disengage(a);
            if (S == 4) a.i(0x7c4) = d + 2;                        // the aggression credit
            switch (sub) {
                case 0:                                            // decide
                    a.i(0x730) = 0;
                    if (std::uint32_t(a.i(0x7c4)) > 0u || tagged) { sub = 20; a.i(0x7c4) -= 1; break; }
                    if (a.dur() != 0) break;
                    sub = int(r15(e) % 100u) < d * 16 + 50 ? 20 : 10;
                    break;
                case 10:                                           // reposition, maybe attack
                    if (a.dur() != 0) break;
                    if (a.i(0x730) == 1) {
                        if (std::uint32_t(a.i(0x7c4)) > 0u) { sub = 20; a.i(0x7c4) -= 1; a.i(0x730) = 0; break; }
                        if (!(4.5f < hdist)) {
                            if (int(r15(e) % 100u) < d * 16 + 50) { sub = 100; a.i(0x79c) = 10; a.i(0x730) = 0; break; }
                        }
                        if (r15(e) % 10u == 0) { sub = 111; a.i(0x79c) = 0; }
                        a.i(0x730) = 0;
                    } else if (a.i(0x730) == 0) {
                        int off = 0;
                        for (int k = 0; k < 2; ++k) {
                            const int r = int(r15(e) % 180u);
                            const float ang = float(off - 90 + r) * 3.14159f / 180.0f + a.heading;
                            const float s2 = 2 * float(std::sin(double(ang))), c2 = 2 * float(std::cos(double(ang)));
                            const Vec3 down{e.self.x, e.self.y + e.height * -1.1f, e.self.z};
                            if (!(e.blocked && e.blocked(e.self, down))) {   // nothing below: accept
                                a.i(0x730) += 1;
                                a.action() = 0x10; a.steer() = 4;
                                a.set_v(0x894, {e.self.x + c2, e.self.y + 0.0f, e.self.z + s2});
                                a.dur() = 30;
                                break;
                            }
                            off = 180;
                        }
                        if (a.i(0x730) != 0) break;
                        if ((e.rng() & 0x20000u) != 0) { a.i(0x730) = 0; sub = 111; a.i(0x79c) = 0; break; }
                        const float ang = float(r15(e) % 360u) * 3.14159f / 180.0f;
                        a.steer() = 4; a.dur() = 60;
                        a.set_v(0x894, {std::cos(ang), 0.0f, std::sin(ang)});   // ABSOLUTE (literal)
                        a.i(0x730) = 0;
                    }
                    break;
                case 20:                                           // chase, then lob
                    if (a.dur() != 0) break;
                    if (a.i(0x730) == 0) {
                        if (hdist > 4.5f && std::uint32_t(a.i(0x7c0)) < std::uint32_t(d * 5 + 15)) {
                            a.action() = 0x400; a.steer() = 0; a.dur() = 30; a.i(0x7c0) += 1;
                            break;
                        }
                        a.i(0x730) = 1; a.i(0x7c0) = 0;
                    } else if (a.i(0x730) == 1) {
                        if (vert_ok) { sub = 100; a.i(0x79c) = 0; } else sub = 0;
                    }
                    break;
                case 100:
                    a.action() = 2; a.steer() = 0; a.dur() = 15; sub = 101;
                    break;
                case 101:
                    if (a.dur() != 0) break;
                    if (ai::request(S, 7, false, e)) out.section = 7;
                    out.attack = true;
                    a.action() = 1;
                    a.dur() = d < 0 ? 96 : 76 - 10 * d;
                    arm();
                    break;
                case 102: {
                    if (a.cool() == 0) { exit_attack(); break; }
                    if (S != 7 || M != 2 || a.i(0x7bc) != 0) break;
                    a.i(0x7bc) = 1;
                    float sp = a.dist() * 0.125f;
                    if (sp > 1.0f) sp = 0.7854f;
                    const int n = d > 1 ? 3 : 1;
                    const float ss = float(std::sin(double(sp))), cs = float(std::cos(double(sp)));
                    for (int k = 1; k <= n; ++k) {
                        const float f = float(k) * 0.1f;
                        const float u1 = unit(e.rng()), u2 = unit(e.rng()), u3 = unit(e.rng()), u4 = unit(e.rng());
                        const float yaw = (-100.0f + (100.0f - -100.0f) * u1) * 0.0174533f + a.heading;
                        AiBullet b;
                        b.effect = 0x13b;                          // enemy\m_200_attack01
                        b.dir = {float(std::cos(double(yaw))) * ss + (-0.75f * f + 1.5f * f * u3),
                                 cs + (-f + 2.0f * f * u2),
                                 float(std::sin(double(yaw))) * ss + (-0.75f * f + 1.5f * f * u4)};
                        b.pos = {e.self.x + float(std::cos(double(a.heading))) * 0.2f, e.self.y + 0.6f,
                                 e.self.z + float(std::sin(double(a.heading))) * 0.2f};
                        b.kind = 0x29;
                        b.speed = 0.12f; b.gravity = 0.004f; b.knock = 1.0f;
                        b.f168 = 131172.0f; b.f16c = 1.2f; b.f170 = 1.2f; b.f174 = 2.4f;   // raw 0x48001900: the burst
                        out.bullets.push_back(b);
                    }
                    break;
                }
                case 111:
                    if (a.dur() != 0) break;
                    if (ai::request(S, 8, false, e)) out.section = 8;
                    out.attack = true;
                    a.action() = 1;
                    a.dur() = 120 - 5 * d;
                    arm();
                    break;
                case 112:
                    if (a.cool() == 0) { exit_attack(); break; }
                    if (S != 8) { a.dur() = 0; exit_attack(); break; }
                    if (M != 3 || a.i(0x7bc) != 0) break;
                    a.i(0x7bc) = 1;
                    // The poison hit one unit ahead along the visible facing (+0x314).
                    out.effects.push_back({0xff, {e.self.x + std::cos(e.facing), e.self.y - 1.0f,
                                                  e.self.z + std::sin(e.facing)}, 2.3f, false});
                    for (int k = 0; k < 6; ++k) {                  // six puddles in a +-0.7 rad arc ahead of the HEADING
                        const std::uint32_t w1 = e.rng(), w2 = e.rng();
                        const float an = unit(w1) * 1.4f - 0.7f + a.heading;       // 0x6bd4b4, 0x6bd36c
                        const float rad = unit(w2) * 1.5f + 1.5f;                  // 0x6bd4d4: 1.5 .. 3.0
                        AiSplat sp;
                        sp.life_range = 30; sp.size0 = 1.6f; sp.size1 = 2.6f;
                        sp.pos = {e.self.x + float(std::cos(double(an))) * rad, e.self.y, e.self.z + float(std::sin(double(an))) * rad};
                        out.splats.push_back(sp);
                    }
                    break;
                default:
                    break;
            }
            break;
        default:
            break;
    }
    // Tail: the hit-reaction poison puff every 10 ticks.
    const std::uint32_t t = std::uint32_t(a.i(0x8a0));
    if (t % 20u == 0 && (S == 0 || S == 1)) {                      // walking or running: the slime trail
        AiSplat sp;
        sp.size0 = 0.8f; sp.size1 = 1.6f; sp.pos = e.self;
        out.splats.push_back(sp);
    }
    if (t % 10u == 0 && S == 4) {
        out.effects.push_back({0xff, {e.self.x, e.self.y, e.self.z}, 1.3f, false});
        const std::uint32_t w1 = e.rng(), w2 = e.rng();            // whether or not the effect allocated
        const float an = unit(w1) * 6.28318f + 0.0f;               // 0x6bd894
        const float rad = unit(w2) * 1.75f + 0.25f;                // 0x6bd510, 0x6bd244: 0.25 .. 2.0
        AiSplat sp;
        sp.size0 = 1.5f; sp.size1 = 2.5f;
        sp.pos = {e.self.x + float(std::cos(double(an))) * rad, e.self.y, e.self.z + float(std::sin(double(an))) * rad};
        out.splats.push_back(sp);
    }
    return out;
}

}  // namespace cleria::brains
