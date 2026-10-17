// M850 Epona — the duel opponent (S_2080 for Yunica, S_1080 for Hugo). Handler FUN_004d1c90,
// brain FUN_004cc3d0 (AI index 78). Spec: build/ai_spec/M850.md.
//
// A swordswoman on a move machine (+0x72c): the picker (state 0) chooses by range among the
// lunge (section 8), the slash (7), the two-part cut (0x12f -> 0x133), the heavy cut (0x130),
// three jump attacks (0x139 / 0x132), the guard with its counter (6 -> 0x138), the anti-air
// (0x131) and a reposition run (section 1); five combos chain those with gaps (+0x7a4). A
// streak of hits in her hurt section makes her retreat into a combo or the guard; a player
// attack in reach makes her jump away. She writes flag 130 herself (1, or 2 inside a combo:
// her sections 7 and 8 read it for their faster timing).
// The handler's parts live elsewhere: its 130 / 131 gates (enemy_handler_puppet / _held,
// consulted by enemy_ai_tick), its arena clamp (enemy_handler_clamp) and its dead path
// (m850_dead below: 156 and 130 clear on the 6th dead tick, which ends the duel scripts'
// Wait_FlagEqualInt(156, 0)).
// [not modelled] the 231 = 1 rematch mode beyond its stats and HP link (the clone summons, the
// joint attack 0x44c.., the death swap, the aura); the lunge's after-images, its d > 0 bullets
// 0x12e and its Marker(298) slash effects;
// the handler's knock cap in the attack sections;
// the dead path's player i-frames (9999) and double-KO revive.
#include <algorithm>
#include <cmath>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

constexpr int kSelf = 0xab;
constexpr float kPi = 3.1415927f, k2Pi = 6.2831855f;

// The bytes +0x8a4 (repeat flag) / +0x8a5 (this one is a clone) inside the raw block.
std::uint8_t& b8(EnemyAi& a, int off) {
    return reinterpret_cast<std::uint8_t*>(a.blk.data())[off - EnemyAi::kBase];
}

}  // namespace

void m850_dead(Actor& A, AiWorld& w) {                              // FUN_004d1c90, HP < 1
    EnemyAi& a = A.ai;
    const bool rematch = w.flag(231, 0) != 0;
    const bool clone = b8(a, 0x8a5) != 0;
    if (w.flag(130, 1) == 0) {                                      // 0x4d1cac: the puppet's dead tick
        if (rematch && clone) return;
        a.alpha = 1.0f;                                             // 0x4d1d0b
        if (a.dead_t > 1u) a.dead_t = 1;                            // 0x4d1d04: pinned, never removed
        return;
    }
    // [not modelled] 0x4d1d5c: player alive -> its i-frames (+0x734) = 9999; player dead -> HP 1.1
    // and FUN_0047aa80 (she revives). [not modelled] 231: the clone death swap (+0x3fc == 0).
    if (a.dead_t == 0) { a.dead_t = 1; a.hittable = false; }        // FUN_004780e0: the death start
    else if (a.dead_t < 0x100u) ++a.dead_t;                         // ... +1 a tick (its fade is undone below)
    if (!rematch || !clone) {
        a.alpha = 1.0f;                                             // 0x4d1f56
        if (a.dead_t > 5u) {                                        // cmp [+0x3fc], 5; jbe (0x4d1f4f)
            a.dead_t = 1;
            // DAT_006d5fd8 = 0 hides the boss bar: the host hides it when its owner is dead.
            w.set_flag(156, 0);                                     // 0x4d1f7a
            w.set_flag(130, 0);                                     // 0x4d1f84
        }
    }
}

AiOut m850(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    Actor* Ap = e.actor;
    AiWorld* w = e.world;
    if (!Ap || !w) return out;
    Actor& A = *Ap;
    const int d = difficulty(e);
    const float fd = float(d);
    const int ch = e.character;
    const bool f231 = w->flag(231, 0) != 0;
    const float k = f231 ? 1.25f : 1.0f;                           // the gap divisor (0x4cd8c7)
    const Vec3 pos = e.self;                                       // the origin, +0x50
    std::uint8_t& rep = b8(a, 0x8a4);
    const bool clone = b8(a, 0x8a5) != 0;
    int sec = e.section, marker = e.aso_marker, block = e.aso_marker_block;
    auto force = [&](int s) { out.section = s; out.section_force = true; sec = s; };   // FUN_00458e40
    auto set_block = [&](int n) { out.set_marker_block = n; block = n; };            // FUN_00458ef0
    auto steer_to = [&](int mode) { const int s = a.steer(); a.steer() = mode; ai::steer(a, e); a.steer() = s; };
    auto rnd = [&](float lo, float hi) { return unit(e.rng()) * (hi - lo) + lo; };   // FUN_0047ab00
    auto P = [&] { return int(r15(e) % 1000u); };
    auto half = [&] { return A.hp < A.max_hp * 0.5f; };
    auto to2 = [&] { return a.f(0x734) * a.f(0x734) + a.f(0x73c) * a.f(0x73c); };

    // ---- handler, alive path: with 231 the 0xab actors share the lowest HP (0x4d1f93) ----
    if (f231 && a.phase() != 0)
        w->for_each([&](Actor& o) {
            if (&o == &A || o.cast_id != kSelf || o.ai.phase() == 0) return;
            if (A.hp > o.hp) A.hp = o.hp; else if (o.hp > A.hp) o.hp = A.hp;
        });

    // ---- preamble ----
    if (e.launched) return out;                                    // +0x4d0
    a.dur() = -1;
    bool hit_on = true;                                            // local_e4
    if (a.i(0x7a4) != 0) --a.i(0x7a4);                             // 0x4cc568..
    if (a.i(0x7a8) != 0) --a.i(0x7a8);
    if (a.i(0x7c0) == 0) a.i(0x7bc) = 0; else --a.i(0x7c0);
    if (a.i(0x7c8) != 0) --a.i(0x7c8);
    if (ch == 2) out.set_durable = (a.cool() == 0 || (sec != 0 && sec != 1 && sec != 2)) ? 1 : 0;
    if (sec == 4) {                                                // hurt: the hit-streak retreat
        if (ch != 2 && std::uint32_t(a.i(0x72c)) < 1000u) a.i(0x7c8) = 10;
        const int thr = half() ? 6 - int(fd * 0.8f) : 10 - int(fd * 1.7f);
        if (std::uint32_t(a.i(0x7bc)) >= std::uint32_t(thr) && std::uint32_t(a.i(0x72c)) < 1000u) {
            const int v = int(r15(e) % 3u + 1u) * 30;
            a.i(0x72c) = v == 90 ? 0x82 : v;
            a.i(0x7bc) = 0; a.i(0x7c0) = 0; a.cool() = 0; a.i(0x7c8) = 11;
        }
    }
    // [not modelled] 231 below half HP: the aura (+0x88c) and the clone bookkeeping (+0x7d4 = 1000).
    A.flags12c &= ~0x40u;

    switch (a.phase()) {
    case 0: {                                                      // ---- INIT
        // The handler, phase 0, before the brain (0x4d2253..0x4d226d): FUN_004202e0(CI[171]+0x60 - 0xe,
        // level = +0xc8): the script's Chr_SetStatus level.
        w->restat(A, 171, A.level);
        ai::init(a, p, e);                                         // +0x814.. = EI[75] = p.raw
        a.set_v(0x808, pos);
        A.flags12c &= 0xfbffffbfu;
        a.hittable = true;                                         // +0x60 = 0x88
        a.i(0x72c) = 0;
        a.cool() = 90 - 20 * d;
        a.i(0x7ac) = a.i(0x7b0) = a.i(0x7b4) = a.i(0x7b8) = 0;
        a.i(0x7d0) = a.i(0x7d4) = a.i(0x7d8) = 0;
        a.f(0x7cc) = to2();
        a.i(0x858) = 0; a.i(0x8a4) &= ~0xffff;                     // +0x8a4/+0x8a5 (a short)
        for (int off = 0x79c; off <= 0x7c8; off += 4) a.i(off) = 0;
        a.i(0x88c) = 0;
        a.set_v(0x7f0, A.mover.k1);
        a.steer() = 0; a.action() = 2; a.i(0x70c) = 0;
        a.phase() = 4;
        if (ch == 2) {
            if (d < 0) A.atk *= 1.075f;
            if (d > 0) A.atk *= 0.95f;
        }
        if (!f231) {                                               // the duels (S_2080, S_1080)
            const float df = A.def * 0.985f;
            float hp = A.hp * 0.95f;
            A.def = df; A.hp = hp;
            if (d > 0) { hp *= 0.95f; A.atk *= 0.99f; A.def = df * 0.95f; A.hp = hp; }
            A.max_hp = hp;
        } else {                                                   // the rematch
            float atk = fd * 0.125f + 3.0f;
            float def = fd * 0.1625f + 14.0f + A.def;
            A.hp = A.max_hp;
            atk += A.atk;
            A.def = def; A.atk = atk;
            if (d == -2) A.atk = atk + 0.75f;
            else if (d == -1) A.atk = atk + 1.5f;
            else if (d > 0) {
                const float mx = A.max_hp * 0.98f;
                A.atk = atk * 0.985f; A.hp = mx; A.def = def * 0.98f; A.max_hp = mx;
            }
        }
        bool found = false;                                        // another 0xab below half: a clone
        w->for_each([&](Actor& o) {
            if (found || o.cast_id != kSelf || &o == &A || !(o.hp < o.max_hp * 0.5f)) return;
            found = true;
            b8(a, 0x8a5) = 1;
            A.hp = o.hp; A.vert.grounded = false;
            a.i(0x72c) = 0x3ea;
            // [not modelled] its aura effect (FUN_00450410, attached); the stat bump assumes it spawned.
            const int dc = std::max(d, 0), d2 = std::min(std::max(d, 0), 2);
            float f = float(dc) * 1.5f;
            if (4.5f <= f) f = 4.5f;
            A.atk -= 4.5f - f;
            A.def = float(d2) * 3.0f + 1.5f + A.def;
            a.f(0x818) = fd * 0.01f + 0.05f + a.f(0x818);
            a.f(0x814) = fd * 0.02f + 0.1f + a.f(0x814);
        });
        break;
    }
    case 1: ai::idle(a, p, e); break;
    case 2: if (a.dist() > 20.0f) ai::disengage(a); break;
    case 4: {                                                      // ---- MAIN
        const std::uint32_t s72 = std::uint32_t(a.i(0x72c));
        int& v = a.i(0x72c);
        // The combo gap (+0x7a4): W() one draw, x3/4 below half HP; D = 3 or 4 (EXPORT :937-1028).
        auto W = [&](int m, int c, int b) {
            int x = int(float(int(r15(e) % std::uint32_t(m)) - c * d + b) / k);
            if (half()) x = (x * 3) / 4;
            return x;
        };
        auto combo_gap = [&](int m, int c, int b, std::uint32_t D) {
            const std::uint32_t n = std::uint32_t(a.i(0x7c4));
            std::uint32_t u = 1;
            if (n * std::uint32_t(W(m, c, b)) / D < 2u) u = n * std::uint32_t(W(m, c, b)) / D;
            std::uint32_t g = std::uint32_t(a.i(0x7a4));
            if (g <= u) {
                if (n * std::uint32_t(W(m, c, b)) / D >= 2u) { a.i(0x7a4) = 1; return; }
                g = n * std::uint32_t(W(m, c, b)) / D;
            }
            a.i(0x7a4) = int(g);
        };
        // The post-move gap the moves top up each tick: /2 (or x3/4) below half HP.
        auto Wm = [&](int m, int c, int b, bool by2) {
            int x = int(float(int(r15(e) % std::uint32_t(m)) - c * d + b) / k);
            if (half()) x = by2 ? x / 2 : (x * 3) / 4;
            return x;
        };
        auto move_gap = [&](int m, int c, int b, bool by2) {
            const std::uint32_t u = std::uint32_t(Wm(m, c, b, by2));
            if (std::uint32_t(a.i(0x7a4)) <= u) a.i(0x7a4) = Wm(m, c, b, by2);
        };
        auto reroute = [&] {                                       // too far for the move: 0x5a / 0x96
            v = (ch != 2 && P() < 300) ? 0x96 : 0x5a;
            if (a.i(0x79c) != 0) a.i(0x7a0) -= 1;                  // LAB_004cec3d: replay this step
        };
        auto jump_start = [&](int section, int next, float ang, float jv, float sp) {
            force(section); out.vm_step = true; set_block(1);
            a.steer() = 0; a.action() = 2; v = next;
            a.f(0x7ac) = ang; a.f(0x7b0) = jv; a.f(0x7b4) = sp;
        };
        auto end_combo = [&] { v = 0; a.i(0x79c) = 0; a.i(0x7a0) = 0; };

        if (s72 >= 1000u) {
            if (s72 == 0x3ea) {                                    // a clone landing
                a.i(0x7d4) = 0;
                if (A.vert.grounded) { v = 0; a.i(0x7d4) = 0; }
            }
            // [not modelled] 1000 / 0x3e9 (the clone summons) and 0x44c..0x452 (the joint attack).
            break;
        }
        switch (s72) {
        case 0: {                                                  // the picker (0x4cd0db)
            if (a.i(0x7d4) == 0 && ch == 1 && e.target_section == 0x139) {
                v = int((r15(e) & 1u) + 11u) * 10;                 // jump away from Yunica's jump attack
                break;
            }
            if (clone) break;
            if (a.i(0x79c) != 0) {                                 // resume the combo
                v = a.i(0x79c) * 10 + 1;
                if (a.i(0x7a0) != 0) w->set_flag(130, 2);
            } else if (a.cool() == 0 && sec != 7 && sec != 8 && sec != 0x12f && sec != 0x131 && sec != 3 &&
                       sec != 0x139 && sec != 0x13a && sec != 0x132) {
                w->set_flag(130, 1);
                const float dist = a.dist();
                auto mid = [&] {                                   // LAB_004cd52a
                    const int lim = (ch != 2 || dist < 8.0f) ? 300 : 50;
                    if (P() < lim && P() >= 50) {
                        if (dist < 8.0f && P() >= 50) v = 0x5a;
                    } else {
                        const int lim2 = ch == 2 ? 50 : 300;
                        if ((P() >= lim2 && (dist >= 3.5f || P() >= lim2 * 2)) || P() < 50) {
                            if (P() >= 50) v = 0x6e;
                        } else {
                            v = 0x78;
                        }
                    }
                };
                if (dist <= 8.0f || P() < 50) {
                    if (dist >= 3.5f || P() < 50) {                // mid range
                        if (P() < 100 && P() >= 50) mid();
                        else if (a.i(0x7a4) == 0) {
                            if (P() < 700 && P() >= 50) v = int(r15(e) % 5u + 1u) * 10;
                            else if (a.i(0x7a4) == 0) {
                                if (P() < 300 && P() >= 50) v = 0x3c;
                                else if (a.i(0x7a4) == 0) {
                                    if (P() < 300 && P() >= 50) v = 0x46;
                                    else if (a.i(0x7a4) == 0 && P() >= 50) v = 0x50;
                                }
                            }
                        }
                    } else if (a.i(0x7a4) == 0 && P() < 750 && P() >= 50) {   // close
                        if (P() < 800 && P() >= 50) {
                            const int x = int((r15(e) & 3u) + 1u) * 10;
                            v = x > 29 ? x + 10 : x;               // 10 / 20 / 40 / 50
                        } else if (P() >= 50) {
                            v = 0x46;
                        }
                    } else if (P() >= 50) {
                        mid();
                    }
                } else if (a.i(0x7a4) == 0 && P() < 200 && P() >= 50) {        // far
                    if (P() < 700 && P() >= 50) {
                        const int x = int(r15(e) % 3u + 2u) * 10;
                        v = x == 40 ? 50 : x;                      // 20 / 30 / 50
                    } else if (P() >= 50) {
                        v = 0x8c;
                    }
                } else if (P() < 400 && P() >= 50) {
                    if (P() < (ch == 2 ? 300 : 250) && P() >= 50) v = 0x96;
                    else if (P() >= 50) v = 0x5a;
                } else if (P() < 550 && P() >= 50) {
                    v = 0x6e;
                } else if (P() >= 50) {
                    v = 100;
                }
                if (f231 && P() < 150 && a.i(0x858) != 0) { v = 0x44c; w->set_flag(130, 2); }   // [not modelled] target
            } else {
                w->set_flag(130, 1);
            }
            if (a.i(0x7d4) != 0) {
                v = a.i(0x7d4); a.i(0x7d4) = 0;
                if (v == 1000) a.i(0x87c) = 0;
            }
            break;
        }
        case 10: case 0x14: case 0x1e: case 0x28: case 0x32:     // the combo starters
            if (a.i(0x7c8) == 0) { a.i(0x7a0) = 0; a.i(0x79c) = int(s72 / 10u); v += 1; a.i(0x7c4) = 0; }
            else v = 0x82;
            break;
        case 0xb:                                                  // combo 1
            ai::turn(a, a.f(0x814) * 100.0f);
            switch (a.i(0x7a0)) {
                case 0: case 2: v = 0x50; steer_to(0); break;
                case 1: case 3: v = 0x6e; break;
                case 4: v = 0x3c; break;
                case 5: end_combo(); break;
                default: break;
            }
            a.i(0x7a0) += 1;
            combo_gap(46, 46, 174, 3);
            break;
        case 0x15:                                                 // combo 2
            steer_to(0); ai::turn(a, a.f(0x814) * 100.0f); steer_to(0);
            switch (a.i(0x7a0)) {
                case 0: v = 0x50; break;
                case 1: v = a.dist() <= 8.0f ? int((r15(e) & 1u) + 10u) * 10 : 0x5a; break;
                case 2: v = 0x46; break;
                case 3: v = int((r15(e) & 1u) + 11u) * 10; break;
                case 4: v = 0x3c; break;
                case 5: end_combo(); break;
                default: break;
            }
            a.i(0x7a0) += 1;
            combo_gap(58, 58, 222, 3);
            break;
        case 0x1f:                                                 // combo 3
            steer_to(0); ai::turn(a, a.f(0x814) * 100.0f);
            switch (a.i(0x7a0)) {
                case 0: case 4: v = 0x8c; break;
                case 1: case 5: v = 0x5a; break;
                case 2: v = 0x46; break;
                case 3: v = a.dist() <= 8.0f ? 0x78 : 0x6e; break;
                case 6: v = 0x3c; break;
                case 7: end_combo(); break;
                default: break;
            }
            a.i(0x7a0) += 1;
            combo_gap(70, 77, 300, 4);
            break;
        case 0x29:                                                 // combo 4
            steer_to(0); ai::turn(a, a.f(0x814) * 100.0f);
            switch (a.i(0x7a0)) {
                case 0: case 2: v = 0x46; break;
                case 1: v = 0x5a; break;
                case 3: case 5: v = 0x6e; break;
                case 4: v = 0x50; break;
                case 6: v = 0x3c; break;
                case 7: end_combo(); break;
                default: break;
            }
            a.i(0x7a0) += 1;
            combo_gap(67, 67, 260, 3);
            break;
        case 0x33:                                                 // combo 5
            steer_to(0); ai::turn(a, a.f(0x814) * 100.0f);
            switch (a.i(0x7a0)) {
                case 0: v = a.dist() <= 8.0f ? 0x78 : 0x6e; break;
                case 1: case 3: case 5: v = 0x8c; break;
                case 2: case 4: v = 0x6e; break;
                case 6: v = (r15(e) & 1u) != 0 ? 0x64 : 0x5a; break;
                case 7: v = 0x3c; break;
                case 8: end_combo(); break;
                default: break;
            }
            a.i(0x7a0) += 1;
            combo_gap(50, 49, 190, 4);
            break;
        case 0x3c: {                                               // the lunge (section 8)
            const bool go = a.i(0x7c8) == 0 || (a.i(0x79c) != 0 && (r15(e) & 1u) == 0);
            if (!go) { v = 0x82; rep = 0; break; }
            if (a.dist() >= 16.0f && (a.i(0x79c) == 0 || a.i(0x7a0) == 0)) {
                v = (ch == 2 || P() > 299) ? 0x5a : 0x96;
                rep = 0;
                if (a.i(0x79c) != 0) a.i(0x7a0) -= 1;
                break;
            }
            a.steer() = 0; a.action() = 2; v = 0x3d;
            if (!clone) force(8);
            out.set_durable = 1;
            a.i(0x7c4) += 1;
            break;
        }
        case 0x3d: {
            a.steer() = 0; a.action() = 2;
            if (sec == 8) {
                if (d > 0) {                                       // bend knock 1 toward the target (0x4cece3)
                    Vec3& kn = A.mover.k1;
                    float ang = std::atan2(kn.z, kn.x);
                    const float spd = std::sqrt(kn.x * kn.x + kn.z * kn.z);
                    float df = std::atan2(a.f(0x73c), a.f(0x734)) - ang;
                    while (df > kPi) df -= k2Pi;
                    while (-kPi >= df) df += k2Pi;
                    if (df > 0.0f) ang += std::min(df, fd * 0.015f);
                    else if (df < 0.0f) ang += std::max(fd * -0.015f, df);
                    kn.x = std::cos(ang) * spd;
                    kn.z = std::sin(ang) * spd;
                }
                if (std::uint32_t(e.aso_frame) > std::uint32_t((d > 0) + 4)) { a.action() = 1; rep = 0; }
                else { ai::steer(a, e); ai::turn(a, a.f(0x814) * 3.0f); }
                if (f231 && std::uint32_t(e.aso_frame) > 12u) rep = 1;
                // [not modelled] |knock 1|^2 > 0.01, every 3rd tick: after-images, the d > 1 effect, two
                // side effects and (d > 0) two bullets 0x12e.
                if (f231) hit_on = A.mover.k1.z * A.mover.k1.z + A.mover.k1.x * A.mover.k1.x <= 0.01f;
                if (marker != 0) { out.set_marker = 0; marker = 0; }   // [not modelled] the slash effect
            } else if (!f231 || a.i(0x7d0) != 0) {
                rep = 0; a.i(0x7d0) = 0; v = 0;
            } else {                                               // 231: one repeat
                a.i(0x7d0) = 1; steer_to(0); ai::turn(a, a.f(0x814) * 50.0f); v = 0x3c; rep = 1;
            }
            move_gap(30, 40, 180, false);
            break;
        }
        case 0x46: {                                               // the two-part cut (0x12f -> 0x133)
            const bool go = a.i(0x7c8) == 0 || (a.i(0x79c) != 0 && (r15(e) & 1u) == 0);
            if (!go) { v = 0x82; break; }
            if (a.dist() < 10.0f || (a.i(0x79c) != 0 && a.i(0x7a0) != 0)) {
                v = 0x47; a.action() = 2; a.steer() = 0; force(0x12f); a.i(0x7c4) += 1;
                break;
            }
            reroute();
            break;
        }
        case 0x47:
            a.steer() = 0; a.action() = 2;
            if (sec == 0x12f) {
                if (std::uint32_t(e.aso_frame) % 3u != 0) a.action() = 1;
                if (marker == 1) {
                    bool hold = false;                             // facing and close: no follow-up
                    if (P() > 0x4a) {
                        const float off = float(std::fabs(ai::angle_diff(a.heading, std::atan2(a.f(0x73c), a.f(0x734)))));
                        hold = off <= 0.39269909f && a.dist() <= 6.875f;
                    }
                    if (!hold) {
                        force(0x133); ai::steer(a, e); a.turn_hold() = 0; ai::turn(a, a.f(0x814) * 100.0f);
                    }
                }
            } else if (sec == 0x133) {
                a.action() = 1;
            } else {
                v = 0;
            }
            move_gap(30, 30, 90, true);
            break;
        case 0x50:                                                 // the slash (section 7)
            if (a.i(0x7c8) != 0 && (a.i(0x79c) == 0 || (r15(e) & 1u) != 0)) { v = 0x82; break; }
            if (a.dist() >= 11.0f && (a.i(0x79c) == 0 || a.i(0x7a0) == 0)) { reroute(); break; }
            v = 0x51; a.action() = 2; a.steer() = 0; force(7); a.i(0x7c4) += 1;
            break;
        case 0x51:
            a.steer() = 0; a.action() = 2;
            if (sec == 7) {
                if (e.aso_frame == 0) { ai::steer(a, e); ai::turn(a, a.f(0x814) * 3.0f); }
                else a.action() = 1;
            } else {
                v = 0;
            }
            move_gap(10, 12, 40, true);
            break;
        case 0x5a:                                                 // the jump attacks
            a.i(0x7c8) = 0;
            jump_start(0x139, 0x5b, rnd(-0.2617994f, 0.2617994f), 0.275f, 1.5f);
            break;
        case 100:
            a.i(0x7c8) = 0;
            jump_start(0x139, 0x5c, rnd(-0.43633232f, 0.43633232f), 0.15f, 0.5f);
            break;
        case 0x78:
            a.i(0x7c8) = 0;
            jump_start(0x132, 0x5c, rnd(-0.43633232f, 0.43633232f) + kPi, 0.15f, 0.5f);
            break;
        case 0x6e: {                                               // the side hop
            a.i(0x87c) = 0; a.i(0x7c8) = 0;
            float m = 1.0f, add = 0.0f;
            if ((r15(e) & 1u) != 0) add = kPi; else m = -1.0f;
            float x = rnd(-0.08726646f, 0.43633232f) * m + 1.5707964f + add;
            while (x > kPi) x -= k2Pi;
            while (kPi >= x) x += k2Pi;                            // [BYTES 0x4d024f] both bounds +pi: (pi, 3pi]
            a.f(0x7ac) = x; a.f(0x7b0) = 0.15f; a.f(0x7b4) = 0.5f;
            a.steer() = 0; a.action() = 2;
            force(std::fabs(x) > 0.76794487f ? 0x132 : 0x139);     // so always 0x132
            out.vm_step = true; set_block(1);
            v = 0x5c;
            break;
        }
        case 0x5b: case 0x5c: {
            a.action() = 1; a.steer() = 0;
            if (sec != 0x139 && sec != 0x132) { a.action() = 2; v = 0; break; }
            if (marker != 1 && block == 1) {                       // wind-up: face the target
                a.action() = 2; steer_to(2); ai::turn(a, a.f(0x814) * 3.0f);
            } else if (marker == 1 && block == 1 && A.vert.grounded) {
                launch(A, a.f(0x7b0), out);                        // FUN_0047a9c0(0, +0x7b0)
                push1(A, a.heading + a.f(0x7ac), a.f(0x7b4));      // FUN_0047aa00(+0x7b4, +0x7ac)
                set_block(2);
            } else if (marker == 2 && block == 2 && A.vert.t < 0.0f) {
                set_block(3);
                if (f231 && (r15(e) & 1u) != 0) push1(A, a.heading + rnd(-kPi, kPi), 0.5f);
            } else if (marker != 0 && A.vert.grounded) {
                set_block(-1);
            }
            if (a.f(0x7b0) > 0.2f && to2() > a.f(0x7cc)) {         // getting away: damp knock 1
                A.mover.k1.x *= 0.95f; A.mover.k1.y *= 0.95f; A.mover.k1.z *= 0.95f;
            }
            if (half() && a.i(0x87c) == 15 && !f231) {             // the low-HP fan (0x4cff57)
                if (d >= 0) out.sounds.push_back(376);             // FUN_0043a080(0x178)
                static constexpr float kFan[5] = {0.0f, -0.2618f, 0.2618f, -0.5236f, 0.5236f};   // 0x6bef70
                const int n = std::min(2 * d + 1, 5);
                for (int i = 0; i < n; ++i) {
                    const float ang = kFan[i] + a.heading;
                    const float c = std::cos(ang), s = std::sin(ang);
                    AiBullet b;
                    b.kind = 1; b.effect = 0xee;
                    b.pos = {c * 0.5f + pos.x, (pos.y - 0.5f) + 0.0f, s * 0.5f + pos.z};
                    b.dir = {c, 0.0f, s};
                    b.speed = 0.3f; b.power = -3.0f; b.knock = 5.0f;
                    out.bullets.push_back(b);
                }
            }
            if (std::uint32_t(marker) > 1u) a.action() = 2;
            break;
        }
        case 0x82:                                                 // the guard (section 6)
            a.i(0x7c8) = 0; force(6); v = 0x83;
            a.i(0x730) = 0; a.i(0x87c) = 0; a.i(0x878) = 0;
            a.steer() = 0; a.action() = 1;
            steer_to(0); ai::turn(a, a.f(0x814) * 50.0f);
            a.f(0x7b8) = 60.0f; a.i(0x87c) = 0;
            break;
        case 0x83: {
            a.action() = 1;
            const float off = float(std::fabs(ai::angle_diff(a.heading, std::atan2(a.f(0x73c), a.f(0x734)))));
            if (off > 1.0471976f) a.i(0x730) += 1;                 // facing away (> 60 deg)
            auto blocks = [&] {
                return std::uint32_t(f231 ? (half() ? 4 * d + 12 : 3 * d + 8) : (half() ? (d + 2) * 3 : 2 * d + 4));
            };
            auto gap = [&] {
                return f231 ? int(r15(e) % 45u) - 20 * d + 70 : int(r15(e) % 90u) - 40 * d + 150;
            };
            if (sec == 6) {
                // Every 45 ticks (+0x87c % 45 == 1) the guard effect: the ASO marker's id, FUN_00450410 at the
                // origin (power -7, knock 10, rtype 2), then FUN_00451c20 mode 1 at (0, 0, -1) [BYTES 0x4d0439..0x4d0547].
                if (std::uint32_t(a.i(0x87c)) % 45u == 1u) {
                    AiEffect fx;
                    fx.id = e.aso_marker; fx.pos = {e.self.x, e.self.y, e.self.z - 1.0f};
                    fx.attach = true; fx.attach_full = true;
                    fx.power = -7.0f; fx.knock = 10.0f; fx.rtype = 2;
                    out.effects.push_back(fx);
                }
                float t;
                if (e.was_hit && !e.took_damage) {                 // +0x3bc & 6 == 4: a blocked hit
                    a.i(0x878) += 1; a.f(0x7b8) = 30.0f; t = 30.0f;
                    if (d > 1 && std::uint32_t(a.i(0x878)) > 2u) { v = 0x8c; a.i(0x7a8) = gap() / 3; break; }
                } else {
                    a.f(0x7b8) -= 1.0f; t = a.f(0x7b8);
                }
                if (std::uint32_t(a.i(0x878)) > blocks() || std::uint32_t(a.i(0x87c)) > std::uint32_t(250 - 15 * d) ||
                    std::uint32_t(a.i(0x730)) > std::uint32_t(30 - 10 * d) || !(0.0f < t))
                    force(0x138);                                  // release
            } else if (sec != 0x138) {
                const bool keep = a.i(0x7c8) != 11 && a.i(0x7c8) != 0 &&
                                  std::uint32_t(a.i(0x87c)) <= std::uint32_t(250 - 15 * d) &&
                                  std::uint32_t(a.i(0x878)) <= blocks();
                if (!keep) { a.action() = 2; v = 0; }
            }
            a.i(0x7a8) = gap();
            break;
        }
        case 0x8c:                                                 // the heavy cut (0x130)
            a.steer() = 0; a.i(0x7c8) = 0; a.action() = 2; v = 0x8d;
            if (!clone || r15(e) % 5u == 0) force(0x130);
            a.i(0x7c4) += 1;
            break;
        case 0x8d:
            a.steer() = 0; a.action() = 2;
            if (sec == 0x130) {
                if (e.aso_frame == 0) { ai::steer(a, e); ai::turn(a, a.f(0x814) * 3.0f); }
                else a.action() = 1;
            } else {
                v = 0;
            }
            move_gap(20, 26, 80, true);
            break;
        case 0x96: {                                               // the reposition run
            a.i(0x7c8) = 0; a.steer() = 4; a.action() = 0x800; a.i(0x87c) = 0;
            const float x = rnd(-1.0f, 1.0f);
            const float z = rnd(-1.0f, 1.0f);
            a.set_v(0x894, {x + pos.x, 0.0f + pos.y, z + pos.z});
            v = 0x97;
            if (sec != 1) force(1);
            break;
        }
        case 0x97: {
            a.steer() = 4; a.action() = 0x800;
            bool stop = true;
            if (sec == 1) {
                if (std::uint32_t(a.i(0x87c)) > 30u && (r15(e) & 1u) != 0) { v = 0x96; break; }
                if (a.dist() >= 6.2f) stop = std::uint32_t(a.i(0x87c)) > 30u && (r15(e) & 1u) != 0;
            }
            if (stop) { a.steer() = 0; a.action() = 2; v = 0; }
            break;
        }
        case 200:                                                  // the anti-air (0x131)
            a.i(0x7c8) = 0; force(0x131); v = 0xc9; a.steer() = 0; a.action() = 2;
            break;
        case 0xc9:
            a.steer() = 0; a.action() = 2;
            if (sec == 0x131) {
                if (std::uint32_t(e.aso_frame) < 2u) { ai::steer(a, e); ai::turn(a, a.f(0x814) * 2.0f); }
                else a.action() = 1;
            } else if (!f231) {
                v = 0;
            } else {
                v = 0x8c; push1(A, a.heading + kPi, 0.2f);
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

    // ---- TAIL (LAB_004d127d, every phase) ----
    {
        const std::uint32_t s = std::uint32_t(a.i(0x72c));
        if (!clone && s % 10u == 0 && s != 0 && s > 0x32u && (s < 1000u || s > 1099u))
            w->for_each([&](Actor& o) { if (&o != &A && o.cast_id == kSelf) o.ai.i(0x72c) = int(s); });
        if ((s > 9u && s < 0x5au) || (s > 0x8bu && s < 0x96u)) {
            a.cool() = f231 ? (3 - d) * 5 + int(r15(e) % 30u) : (3 - d) * 15 + int(r15(e) % 45u);
            if (s > 9u && s < 0x3cu)
                a.cool() = f231 ? int(r15(e) % 60u) - 25 * d + 90 : int(r15(e) % 75u) + (4 - d) * 30;
        }
        // The evade (0x4d13d0): a player attack in reach and a lucky draw -> hop away or guard.
        if (a.i(0x79c) == 0 && std::uint32_t(a.i(0x7a4)) < 60u && std::uint32_t(a.cool()) < 15u &&
            A.vert.grounded && s < 1000u && (s == 0 || sec == 1 || sec == 0 || sec == 2 || sec == 6)) {
            const float dist = a.dist();
            if (e.target.y > pos.y + 0.8f && dist < 7.1f && P() > 50) {
                a.i(0x72c) = 200;                                  // the target above: anti-air
            } else if (ch == 2 || dist < 8.0f) {
                const std::uint32_t ps = std::uint32_t(e.target_section);
                if (P() < (f231 ? 250 : 50) && sec != 6 &&
                    (e.took_damage || (ps > 0xffu && ps < 0x106u) || ps - 0x10cu < 7u)) {
                    if ((P() < 300 && P() > 50) || P() < 50) a.i(0x72c) = 0x6e;
                    else if ((P() < (ch == 2 ? 50 : 300) && P() > 50) || P() < 50) a.i(0x72c) = 0x78;
                    else if ((P() < ((ch != 2 || dist < 8.0f) ? 300 : 50) && P() > 50) || P() < 50) a.i(0x72c) = 0x5a;
                    else if (a.i(0x7a8) == 0 || P() < 5 || (ch == 2 && P() < 50)) a.i(0x72c) = 0x82;
                }
            }
        }
    }
    if (a.i(0x728) == 100 && ((sec == 0x13a && std::uint32_t(marker) >= 2u) || sec != 0x13a) && A.vert.grounded)
        a.i(0x728) = 0;
    if (sec == 0x13a && std::uint32_t(a.i(0x72c)) < 1000u && A.vert.grounded && marker == 1)
        launch(A, a.f(0x7dc) * 0.1f, out);                         // the jump helper's lift
    if (a.f(0x880) != A.hp) {                                      // the boss gauge (0x4d1593)
        a.f(0x880) = A.hp;
        out.gauge_hp = A.hp;
        out.gauge_len = ch == 2 ? 3000.0f : (f231 ? 7500.0f : 4000.0f);
        if (a.i(0x72c) != 0x3c && a.i(0x72c) != 0x3d) a.i(0x7bc) += 1;   // hits in a row
        a.i(0x7c0) = 20;
    }
    if (ch != 2) {                                                 // a fresh knock adds the last one
        Vec3& kn = A.mover.k1;
        const float now = std::sqrt(kn.x * kn.x + kn.z * kn.z);
        const float was = std::sqrt(a.f(0x7f0) * a.f(0x7f0) + a.f(0x7f8) * a.f(0x7f8));
        if (was < now) { kn.x += a.f(0x7f0); kn.y = a.f(0x7f4) + kn.y; kn.z = a.f(0x7f8) + kn.z; }
        a.set_v(0x7f0, kn);
    }
    if (!A.vert.grounded && (a.i(0x87c) & 1) == 0)                 // airborne: the trail puff
        out.effects.push_back({72, {pos.x, rnd(-1.0f, -0.5f) + pos.y, pos.z}, 1.0f, false});
    a.hittable = hit_on;
    a.dur() = -1;
    ai::steer(a, e);
    ai::turn(a, a.f(0x814));
    if (A.flyer) ai::hover(a, A, e, nullptr, a.i(0x720));
    ai::act(a, p, e, &out);
    if (a.i(0x72c) == 0x96 || a.i(0x72c) == 0x97) a.action() = -1;
    a.f(0x7cc) = to2();
    if (!e.target_alive) { a.hittable = false; A.def = 999.0f; }   // the handler: FUN_0047aa80
    return out;
}

}  // namespace cleria::brains
