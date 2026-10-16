// M490 Sopus-Baocar — brain FUN_00481940 (handler FUN_00482f10 = M100's minus the arena
// clamp, AI index 42). Spec: build/ai_spec/M490.md.
//
// M390's warp-mage frame (invisible spawn, invisible dash to 5..7 u in front of the player,
// 60-tick fades with effects 270 + 269 and SE 464, unhittable while alpha <= 0.5 except
// during the fade-in) with two attacks picked by planar distance: under 4 u, section 8 —
// it rises 1.5 and, while the section's marker is 1, drops NRING rings of bombs (effect
// 267 m_bom01, SE 437 per bomb) every 30 (45 easy) ticks, ring k at radius 2 + 0.75k; from 4
// to d+8 u, section 7 (disc shots) or a 40-tick back-off; two shots, then a stand. It warps
// out after 4-d damaging hits, on |dy| > 3, on a coin flip after standing, or (tagged
// spawns) 40% on a hit once an attack has ended.
// [not modelled] the bombs' damage — they are GoXx type-1 objects (FUN_00593b90, the
// FireBullet struct) whose damage path is untraced, so here they are effects + sound only;
// the flyer altitude controller FUN_00476960 (the
// +0x834/+0x838 hover writes are kept for it); the effects' attachment (FUN_00451c20).
#include <cmath>

#include "cleria/brains/brains.hpp"

namespace cleria::brains {

AiOut m490(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    if (e.launched) return out;                                    // +0x4d0
    const int d = difficulty(e);
    const Vec3 to = a.v(0x734);
    const float dist = std::sqrt(to.x * to.x + 0.0f + to.z * to.z);  // planar, last tick's
    const Vec3 Tp = e.target;
    const float Th = e.target_heading;
    const float dyT = Tp.y - e.self.y;
    const bool can_attack = !(std::fabs(dyT) >= 2.0f && e.target_grounded);
    const bool tagged = e.group_tag;                               // +0x130 & 0x700
    const std::uint32_t FLAG = (d >= -2 && d <= 1 && !tagged) ? 1u : 0u;
    const std::uint32_t NRING = (d == 3 || d == 2) ? 4u : (d == -2 || d == -1) ? 2u : 3u;
    const std::uint32_t RING_INTERVAL = (d == -2 || d == -1) ? 45u : 30u;
    const int SHOT_LOCK = 100 + (e.diff_easy ? 30 : e.diff_index == 0 ? 10 : 0);
    const int RING_LOCK = 0x112 + (e.diff_easy ? 0x50 : e.diff_index == 0 ? 0x28 : 0x14);
    const std::uint32_t hit_thr = std::uint32_t(4 - d);
    constexpr float kPi490 = 3.1415901184082031f;                  // 0x40490FD0
    if (e.took_damage) a.i(0x7a0) += 1;

    int& sub = a.i(0x72c);
    int& fade = a.i(0x730);
    auto restore = [&](bool hover) {
        a.f(0x838) = a.f(0x7ac); a.f(0x83c) = a.f(0x7b0); a.f(0x818) = a.f(0x7b4); a.f(0x814) = a.f(0x7b8);
        if (hover) a.f(0x834) = a.f(0x7cc);
    };
    auto unhover = [&] { a.f(0x834) = a.f(0x7cc); a.f(0x838) = a.f(0x7ac); };
    auto warp = [&] {                                              // WARP
        sub = 10; a.i(0x7a0) = 0; a.i(0x7c0) = 0;
        a.i(0x79c) = int(r15(e) % 3u) + 1;
    };
    auto interrupt = [&] {                                         // INTERRUPT
        if (std::uint32_t(a.i(0x7a0)) >= hit_thr && std::uint32_t(a.i(0x7c0)) >= FLAG) { warp(); return; }
        if (!tagged || !e.took_damage) return;
        if (r15(e) % 100u >= 40u) return;
        warp();
    };

    bool run_sub = false;
    switch (a.phase()) {
        case 0:
            ai::init(a, p, e);
            a.i(0x7bc) = 0; a.i(0x7a0) = 0; a.i(0x7a4) = 0; a.i(0x7c4) = 1;
            a.f(0x7ac) = a.f(0x838); a.f(0x7b0) = a.f(0x83c); a.f(0x7b4) = a.f(0x818);
            a.f(0x7b8) = a.f(0x814); a.f(0x7cc) = a.f(0x834);
            a.i(0x728) = 0; sub = 0; fade = 0;
            a.alpha = 0.0f;
            break;
        case 1:
            ai::idle(a, p, e);
            a.i(0x7bc) = 0; a.i(0x728) = 0; sub = 0;
            restore(true);
            break;
        case 3:
            sub = 0; fade = 0; a.alpha = 1.0f; a.i(0x7bc) = 0;
            break;
        case 2:
            if (a.dist() > 20.0f) ai::disengage(a);               // the sub-switch still runs
            run_sub = true;
            break;
        default:
            break;
    }

    if (run_sub) switch (sub) {
        case 0:
            restore(true);
            if (a.alpha == 0.0f && dist < 15.0f) {
                sub = 12;
                a.i(0x79c) = int(r15(e) & 1u) + 2;
                a.i(0x7c4) = 1;
            } else if (std::fabs(dyT) > 3.0f) {
                sub = 10;
                a.i(0x79c) = int(r15(e) % 3u) + 1;
                a.i(0x7a0) = 0; a.i(0x7c4) = 0;
            } else if (dist < 4.0f) {
                a.i(0x7bc) = 0; sub = 30;                          // bomb rings
            } else {
                sub = 1; a.i(0x7c0) = 0; a.i(0x7c4) = 0;
            }
            break;
        case 1: {
            if (a.dur() != 0) break;
            const bool cond = can_attack && !(dist >= float(d) + 8.0f);
            if (std::uint32_t(a.i(0x7c0)) >= 2u) {
                a.action() = 2; a.steer() = 0;
                a.dur() = (-d * 5 + 15) * 8;
                a.i(0x7c0) = 0; sub = 2;
                break;
            }
            if (!cond) { sub = 0; break; }
            if (std::uint32_t(a.i(0x7a0)) >= hit_thr && std::uint32_t(a.i(0x7c0)) >= FLAG) {
                sub = 10; a.i(0x7a0) = 0; a.i(0x7c0) = 0;
                a.i(0x79c) = int(r15(e) % 3u) + 1;
                break;
            }
            const std::uint32_t w = e.rng();
            if ((w & 0x20000u) != 0 || dist >= 5.0f) {
                a.i(0x7bc) = 1; sub = 20; a.i(0x7c0) += 1;        // shoot
            } else {
                a.action() = 0x10; a.steer() = 3; a.dur() = 40;    // back off
            }
            break;
        }
        case 2:
            if (a.dur() == 0) {
                if ((e.rng() & 0x20000u) == 0) sub = 1;
                else { sub = 10; a.i(0x79c) = int(r15(e) % 3u) + 1; a.i(0x7a0) = 0; }
            }
            break;
        case 10:
            if (a.alpha != 1.0f) break;
            if (fade == 0) { fade = 100; sub = 11; }
            break;
        case 11:
            if (fade == 0) sub = 12;
            break;
        case 12: {                                                 // invisible dash
            a.f(0x818) = a.f(0x818) * 4.0f;
            a.f(0x838) = 1.0f; a.f(0x83c) = 1.0f;
            a.f(0x814) = 1.0f;
            a.action() = 0x400;
            const float base = a.i(0x79c) == 1 ? 4.0f : a.i(0x79c) == 2 ? 5.0f : 6.0f;
            const std::uint32_t w1 = e.rng();
            const float rad = (float(d) * 0.5f + base) + float((w1 >> 17) % 1000u) * 0.001f;
            float ang = float(r15(e) % 360u) * kPi490 / 180.0f;
            if (a.i(0x7c4) != 0)
                ang = float(int(r15(e) % 60u) - 30) * kPi490 / 180.0f - Th;
            a.steer() = 4;
            Vec3 wp = Tp;
            wp.x += float(std::cos(double(ang))) * rad;
            sub = 13;
            a.dur() = 20;
            wp.z += -(float(std::sin(double(ang))) * rad);
            a.set_v(0x894, wp);
            break;
        }
        case 13:
            if (a.dur() == 0) { a.action() = 2; a.steer() = 0; a.dur() = 10; sub = 14; }
            break;
        case 14:
            if (a.dur() == 0) { restore(false); fade = 200; sub = 15; }
            break;
        case 15:
            if (fade == 0) sub = 0;
            break;
        case 20:
            a.action() = 2; a.steer() = 0; a.dur() = 30; sub = 21;
            [[fallthrough]];
        case 21: {
            if (a.dur() != 0) break;
            out.section = 7; out.attack = true;                    // disc shots
            a.action() = 1;
            a.dur() = SHOT_LOCK;
            const int lock = a.dur() + 30 + int(r15(e) % 20u);
            a.cool() = lock; a.i(0x7a4) = lock;
            sub = 22;
            break;
        }
        case 22:
            if (a.i(0x7a4) == 0) { sub = a.i(0x7bc); a.i(0x7a0) = 0; break; }
            if (e.section == 7) break;
            interrupt();
            break;
        case 30:
            a.action() = 2; a.steer() = 0; a.dur() = 30; sub = 31;
            [[fallthrough]];
        case 31: {
            if (a.dur() != 0) break;
            out.section = 8; out.attack = true;                    // bomb dance
            a.action() = 1;
            a.dur() = RING_LOCK;
            const int lock = a.dur() + 30 + int(r15(e) % 20u);
            a.cool() = lock; a.i(0x7a4) = lock;
            a.i(0x7c8) = 0;
            sub = 32;
            a.i(0x87c) = 0;
            a.f(0x834) = a.f(0x834) + 1.5f;
            a.f(0x838) = a.f(0x838) * 6.0f;
            break;
        }
        case 32: {
            if (a.i(0x7a4) == 0) { sub = a.i(0x7bc); a.i(0x7a0) = 0; unhover(); break; }
            if (e.section != 8) { unhover(); interrupt(); break; }
            if (e.aso_marker == 2) { unhover(); break; }
            if (e.aso_marker != 1) break;
            if (std::uint32_t(a.i(0x7c8)) >= NRING) break;
            if (std::uint32_t(a.i(0x87c)) < RING_INTERVAL) break;
            std::uint32_t deg = r15(e) % 360u;
            const std::uint32_t step = 360u / NRING;
            for (std::uint32_t i = 0; i < NRING; ++i) {
                const float rad = float(double(a.i(0x7c8))) * 0.75f + 2.0f;
                const float th = float(deg) * kPi490 / 180.0f;
                const Vec3 bp{e.self.x + float(std::cos(double(th))) * rad, e.self.y - 2.0f,
                              e.self.z - float(std::sin(double(th))) * rad};
                out.effects.push_back({0x10b, bp, 1.0f, false});   // enemy\m_bom01
                out.sounds.push_back(0x1b5);                       // zako\z_bomb
                deg += step;
            }
            a.i(0x7c8) += 1;
            a.i(0x87c) = 0;
            break;
        }
        default:
            break;
    }

    // FADE
    if (fade == 100 || fade == 200) {
        if (!(fade == 200 && dist < 1.3f)) {
            a.action() = 1; a.dur() = 60; a.steer() = 1; a.cool() = 60;
            const Vec3 at{e.self.x, e.self.y - e.height, e.self.z};
            out.effects.push_back({0x10e, at, 1.0f, true});        // enemy\m_230_taiki
            out.effects.push_back({0x10d, at, 1.3f, true});        // enemy\z_warp
            out.sounds.push_back(0x1d0);                           // zako\z_390_run (warp)
            ++fade;
        }
    }
    if (fade == 101 || fade == 201) {
        const float f = float(double(std::uint32_t(a.dur()))) / 60.0f;
        a.alpha = fade == 101 ? f : 1.0f - f;
        if (a.dur() == 0) {
            int n;
            if (fade == 101) { a.alpha = 0.0f; n = 30; }
            else { a.alpha = 1.0f; n = (-d + 4) * 15; }
            a.dur() = n;
            ++fade;
            a.action() = 1; a.steer() = 1; a.cool() = n;
        }
    }
    if (fade == 102 || fade == 202) {
        if (a.dur() == 0) fade = 0;
    }
    a.hittable = a.alpha > 0.5f || fade == 201;
    if (a.i(0x7a4) != 0) a.i(0x7a4) -= 1;
    return out;
}

}  // namespace cleria::brains
