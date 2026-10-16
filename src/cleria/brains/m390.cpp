// M390 Baocar — brain FUN_0047f6a0 (handler FUN_00480c60 = M100's minus the arena clamp,
// AI index 32). Spec: build/ai_spec/M390.md.
//
// A warp mage. It spawns invisible (alpha +0x4d8 = 0) and is unhittable while alpha <= 0.5
// (+0x60 & 0x80). Engaged and invisible within 15 planar u it dashes (speed x4, turn 1 rad/
// tick) to 4..7.5 u in front of the player, then fades in over 60 ticks (effects 270 + 269,
// SE 464) and holds (4-d)*15. Visible, it backs away or (coin flip, or P >= 5) winds up 30
// ticks and casts section 7 (one disc bullet). It vanishes and re-warps after 10-3d damaging
// hits, after a forced pause following 3 casts (coin flip), when |dy| > 3, or when an elite
// (+0x130 & 0x700) is hit (40%). A vanish: 60-tick fade-out, 30-tick hold, an invisible dash
// to a random point 4..7.5 u round the player, a 10-tick stop, fade-in.
// [not modelled] the flyer altitude controller
// FUN_00476960 (+0x834 hover 1.2, climb/descend +0x838/+0x83c) — the view keeps enemies on
// the floor; the effects' attachment to the actor (FUN_00451c20) and z_warp's 1.3 scale.
#include <cmath>

#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m390(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0
    const int d = difficulty(e);
    const Vec3 to = a.v(0x734);
    const float P = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);   // planar, last tick's
    const float Th = e.target_heading;
    const Vec3 Tp = e.target;
    const float DY = std::fabs(Tp.y - e.self.y);
    const bool C = !(DY >= 2.0f && e.target_grounded);
    const bool E = e.group_tag;                                    // +0x130 & 0x700
    const std::uint32_t F = (d >= -2 && d <= 1 && !E) ? 1u : 0u;
    if (e.took_damage) a.i(0x7a0) += 1;                            // +0x3bc & 2
    constexpr float kPi390 = 3.1415901184082031f;                  // 0x40490FD0, not exact pi
    int& sub = a.i(0x72c);
    auto saved_restore = [&] {
        a.f(0x838) = a.f(0x7ac); a.f(0x83c) = a.f(0x7b0); a.f(0x818) = a.f(0x7b4); a.f(0x814) = a.f(0x7b8);
    };
    auto vanish = [&] {
        sub = 10; a.i(0x7a0) = 0; a.i(0x7c0) = 0;
        a.i(0x79c) = int(r15(e) % 3u) + 1;
    };
    const std::uint32_t hit_thr = std::uint32_t(10 - 3 * d);

    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.i(0x7bc) = 0; a.i(0x7a0) = 0; a.i(0x7a4) = 0; a.i(0x7c4) = 1;
            a.f(0x7ac) = a.f(0x838); a.f(0x7b0) = a.f(0x83c); a.f(0x7b4) = a.f(0x818);
            a.f(0x7b8) = a.f(0x814); a.f(0x7cc) = a.f(0x834);
            a.i(0x728) = 0; sub = 0; a.i(0x730) = 0;
            a.alpha = 0.0f;                                        // spawns invisible
            break;
        case 1:
            ai::idle(a, p, e);
            a.i(0x7bc) = 0; a.i(0x728) = 0; sub = 0;
            saved_restore();
            a.f(0x834) = a.f(0x7cc);
            break;
        case 3:
            sub = 0; a.alpha = 1.0f; a.i(0x7bc) = 0;
            break;
        case 2: {
            if (a.dist() > 20.0f) ai::disengage(a);               // falls into the switch
            switch (sub) {
                case 0:
                    if (a.alpha == 0.0f && P < 15.0f) {            // warp in front of the target
                        sub = 12;
                        const std::uint32_t w = e.rng();
                        a.i(0x7c4) = 1; a.i(0x79c) = int((w >> 17) & 1u) + 2;
                    } else if (DY > 3.0f) {
                        sub = 10;
                        const std::uint32_t w = e.rng();
                        a.i(0x7a0) = 0; a.i(0x7c4) = 0; a.i(0x79c) = int((w >> 17) % 3u) + 1;
                    } else {
                        sub = 1; a.i(0x7c0) = 0; a.i(0x7c4) = 0;
                    }
                    break;
                case 1: {
                    if (a.dur() != 0) break;
                    const bool ok = C && P < float(d) + 8.0f;
                    if (std::uint32_t(a.i(0x7c0)) >= 3u) {         // 3 casts: forced pause
                        a.action() = 2; a.steer() = 0;
                        a.dur() = (15 - 5 * d) * 8;
                        a.i(0x7c0) = 0; sub = 2;
                    } else if (!ok) {
                        sub = 0;
                    } else {
                        if (std::uint32_t(a.i(0x7a0)) >= hit_thr && std::uint32_t(a.i(0x7c0)) >= F) {
                            vanish();
                            break;
                        }
                        const std::uint32_t w = e.rng();
                        if ((w & 0x20000u) != 0 || P >= 5.0f) {
                            a.i(0x7bc) = 1; sub = 20; a.i(0x7c0) += 1;
                        } else {                                   // back off (walk away)
                            a.action() = 0x10; a.steer() = 3; a.dur() = 40;
                        }
                    }
                    break;
                }
                case 2: {
                    if (a.dur() != 0) break;
                    const std::uint32_t w = e.rng();
                    if ((w & 0x20000u) == 0) {
                        sub = 1;
                    } else {
                        sub = 10;
                        const std::uint32_t w2 = e.rng();
                        a.i(0x7a0) = 0; a.i(0x79c) = int((w2 >> 17) % 3u) + 1;
                    }
                    break;
                }
                case 10:                                           // wait fully visible, fade out
                    if (a.alpha == 1.0f && a.i(0x730) == 0) { a.i(0x730) = 100; sub = 11; }
                    break;
                case 11:
                    if (a.i(0x730) == 0) sub = 12;
                    break;
                case 12: {                                         // set up the invisible dash
                    a.f(0x818) = a.f(0x818) * 4.0f;
                    a.f(0x838) = 1.0f; a.f(0x83c) = 1.0f; a.f(0x814) = 1.0f;
                    a.action() = 0x400;
                    const float base = a.i(0x79c) == 1 ? 0.5f * float(d) + 4.0f
                                     : a.i(0x79c) == 2 ? 0.5f * float(d) + 5.0f
                                                       : 0.5f * float(d) + 6.0f;
                    const std::uint32_t w1 = e.rng(), w2 = e.rng();
                    const float r = float((w1 >> 17) % 1000u) * 0.001f + base;
                    float ang = (float((w2 >> 17) % 360u) * kPi390) / 180.0f;
                    if (a.i(0x7c4) != 0) {
                        const std::uint32_t w3 = e.rng();
                        ang = (float(int((w3 >> 17) % 60u) - 30) * kPi390) / 180.0f - Th;
                    }
                    const float s = float(std::sin(double(ang))), c = float(std::cos(double(ang)));
                    sub = 13; a.steer() = 4; a.dur() = 20;
                    a.set_v(0x894, {Tp.x + c * r, Tp.y, Tp.z - s * r});
                    break;
                }
                case 13:
                    if (a.dur() == 0) { a.action() = 2; a.steer() = 0; a.dur() = 10; sub = 14; }
                    break;
                case 14:
                    if (a.dur() == 0) { saved_restore(); a.i(0x730) = 200; sub = 15; }
                    break;
                case 15:
                    if (a.i(0x730) == 0) sub = 0;
                    break;
                case 20:                                           // wind-up, falls into 21
                    a.action() = 2; a.steer() = 0; a.dur() = 30; sub = 21;
                    [[fallthrough]];
                case 21: {
                    if (a.dur() != 0) break;
                    out.section = 7;                               // the disc; result ignored
                    out.attack = true;
                    a.action() = 1;
                    a.dur() = 80 + (e.diff_easy ? 30 : (e.diff_index == 0 ? 10 : 0));
                    const int c = a.dur() + 30 + int(r15(e) % 20u);
                    a.cool() = c; a.i(0x7a4) = c;
                    sub = 22;
                    break;
                }
                case 22:
                    if (a.i(0x7a4) == 0) {
                        sub = a.i(0x7bc); a.i(0x7a0) = 0;
                    } else if (e.section != 7) {                   // the cast has ended
                        if (std::uint32_t(a.i(0x7a0)) >= hit_thr && std::uint32_t(a.i(0x7c0)) >= F) {
                            vanish();
                        } else if (E && e.took_damage && r15(e) % 100u < 40u) {
                            vanish();
                        }
                    }
                    break;
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }

    // The fade machine on +0x730 (every phase).
    int f = a.i(0x730);
    if (f == 100 || (f == 200 && !(P < 1.3f))) {
        a.action() = 1; a.dur() = 60; a.steer() = 1; a.cool() = 60;
        const Vec3 at{e.self.x, e.self.y - e.height, e.self.z};
        out.effects.push_back({0x10e, at, 1.0f, true});            // enemy\m_230_taiki
        out.effects.push_back({0x10d, at, 1.3f, true});            // enemy\z_warp
        out.sounds.push_back(0x1d0);                               // zako\z_390_run (warp)
        f = ++a.i(0x730);
    }
    if (f == 101 || f == 201) {
        const float k = float(std::uint32_t(a.dur())) / 60.0f;
        a.alpha = f == 101 ? k : 1.0f - k;
        if (a.dur() == 0) {
            int n;
            if (a.i(0x730) == 101) { a.alpha = 0.0f; n = 30; }
            else { a.alpha = 1.0f; n = (4 - d) * 15; }
            a.dur() = n; a.i(0x730) += 1; a.action() = 1; a.steer() = 1; a.cool() = n;
            f = a.i(0x730);
        }
    }
    if (f == 102 || f == 202) {
        if (a.dur() == 0) a.i(0x730) = 0;
    }
    a.hittable = a.alpha > 0.5f || a.i(0x730) == 201;
    if (a.i(0x7a4) != 0) a.i(0x7a4) -= 1;
    return out;
}

}  // namespace cleria::brains
