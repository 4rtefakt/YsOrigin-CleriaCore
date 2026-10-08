// M360 Jenocres, the floor-4 boss — brain FUN_004d2620, handler FUN_004d3f30 (AI index 29).
// Spec: build/ai_spec/M360.md (the crystals' side: build/ai_spec/M980.md, brains/m980.cpp).
//
// A hovering caster that never walks. INIT forces camera region 6 and makes the stats; 30 ticks in it
// plays the intro (300) and at its Marker 1 lays out 8 invisible crystals (cast 0xb8) on a 2x4 grid,
// two every 20 ticks, then plays 301 and switches them on (+0x730 = 10). Idle it waits (8 - d)·15 +
// R%60 ticks (each HP change takes 1, Hugo 15) and teleports (302 out, an invisible hop to a random
// point within 8 of (0, 13), 303 back in) — below half HP 50% a lightning call instead (304: a warning
// circle at the player's feet every max(30, 120·HP%) ticks and a bolt 59 - 13d ticks later), and from
// tick 450 on whenever the player is more than 11 from (0, 13). In the background it re-rolls the
// crystals' attack mode g_flags[180] every 400..1199 ticks and switches their formation (columns,
// ring, tight ring, plus) 5% a tick once a hold is over. The boss gauge (6000, emblem 6).
// The view's side of the dead path (section 5's effect loops under the linear fade, P+0x734 = 9999,
// the removal) is boss_corpse in apps/cleria-view/view_revive.inl.
// [not modelled] the keep-out box restore (FUN_0042dc90); the forced region's blend rate 0.02. [H] the pending bolts
// live in spare AI dwords (+0x7cc..+0x7f8) instead of on the warning effect's +0x1a4 countdown.
#include <algorithm>
#include <cmath>
#include <functional>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

constexpr int kCrystal = 0xb8;
constexpr float kCx = 0.0f, kCz = 13.0f;                         // [0x6bcf94], [0x6bd958]
constexpr int kBolts = 0x7cc, kNBolts = 3;                       // [H] (x, y, z, countdown) x 3

// Slot tables X / Z (0x4d38f3..0x4d3aff), by f/10 = 1..4 (anything else = 4).
constexpr float kX[4][8] = {
    {9, -9, 9, -9, 9, -9, 9, -9},
    {3.3333333f, -3.3333333f, 10, -10, 10, -10, 3.3333333f, -3.3333333f},
    {0, -2.5f, 2.5f, -2.5f, 2.5f, -2.5f, 2.5f, 0},
    {0, -10, 0, -7.5f, 7.5f, 0, 10, 0}};
constexpr float kZ[4][8] = {
    {4, 4, 10, 10, 16, 16, 22, 22},
    {3, 3, 9.6666667f, 9.6666667f, 16.333333f, 16.333333f, 23, 23},
    {10.5f, 10.5f, 10.5f, 13, 13, 15.5f, 15.5f, 15.5f},
    {3, 13, 5.5f, 13, 13, 20.5f, 13, 23}};

int hold_of(int f, std::uint32_t w) {                            // HOLD(f): one draw
    if (f % 10 == 0) {
        if (f == 10 || f == 20) return int(w % 600u) + 750;
        if (f == 30) return int(w % 900u) + 750;
        return int(w % 900u) + 900;
    }
    if (f == 11 || f == 21) return int(w % 600u) + 900;
    if (f == 31) return int(w % 600u) + 600;
    return int(w % 900u) + 900;
}

bool far_from_centre(const Vec3& t) {
    return (t.z - kCz) * (t.z - kCz) + (t.x - kCx) * (t.x - kCx) > 121.0f;   // 0x6bdb2c
}

}  // namespace

void m360_dead(Actor& A, AiWorld& w) {
    EnemyAi& a = A.ai;
    // Pre-block (0x4d3f50): a boss that died while the player is dead comes back at 1.1 HP.
    if (Actor* P = w.player(); P && P->hp < 1.0f) {
        A.hp = 1.1f;
        a.hittable = false; A.def = 999.0f;                        // FUN_0047aa80(A, 1)
        return;
    }
    // FUN_004780e0's counter with +0x12c & 0x8000000: +1 a tick, the linear fade (255 - n) / 255.
    if (a.dead_t < 0x100u) a.dead_t += 1;
    a.alpha = float(std::max(0, 255 - int(a.dead_t))) / 255.0f;
    a.hittable = false;
    if (a.dead_t == 1) {                                           // 0x4d4000: the death-entry tick
        w.force_camera(0, 0.02f);
        w.for_each([&](Actor& c) {
            if (c.cast_id != kCrystal || c.destroy_requested() || c.hp < 1.0f) return;
            c.hp = 0.0f;
            w.sound_at(449, c.pos);                                // zako\z_dead11
        });
    }
    if (a.dead_t == 0xff) {                                        // 0x4d4076: the fade's end
        w.hide_boss_gauge();
        w.set_flag(156, 0);
        w.set_flag(130, 0);
    }
}

AiOut m360(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    AiOut out;
    Actor* Ap = e.actor;
    AiWorld* w = e.world;
    if (!Ap || !w) return out;
    Actor& A = *Ap;
    if (e.launched) return out;                                    // +0x4d0
    const int d = difficulty(e);
    const int ch = e.character;
    const Vec3 T = e.target;                                       // the player's origin
    a.i(0x7c0) += 1;
    int S = e.section;
    auto force = [&](int s) { out.section = s; out.section_force = true; S = s; };
    auto gated = [&](int s) {                                      // FUN_00446550(s, 0): +0x994 at once if accepted
        if (ai::request(S, s, false, e)) { out.section = s; out.section_force = false; }
    };
    auto crystals = [&](const std::function<void(Actor&)>& f) {
        w->for_each([&](Actor& c) { if (c.cast_id == kCrystal) f(c); });
    };
    auto idle_wait = [&] {                                         // max(0, R%60 + base), evaluated twice
        const int base = (8 - d) * 15;
        const int r1 = int(r15(e) % 60u) + base;
        return r1 < 0 ? 0 : int(r15(e) % 60u) + base;
    };
    auto teleport = [&] { force(302); a.i(0x72c) = 20; };

    bool main = false;
    switch (a.phase()) {
        case 0: {                                                  // INIT (0x4d27b8)
            ai::init(a, p, e);
            w->force_camera(6, 0.02f);
            // 0x4d282d: FUN_004202e0(EI[26], level +0xc8). The battle scripts' 0x160 Chr_SetStatus(168, 29,
            // 200, 70, 70, 5, 6, ...) lands first (g_flags[131] holds the brain until BATTLE_* clears it at
            // pc 47), so its HP / STR / DEF / EXP are overwritten here; only its level 29 survives.
            w->restat(A, A.cast_id, A.level);
            A.atk += 1.5f;
            A.hp *= 1.03f;
            if (ch == 2) {
                if (d > 0) { A.atk *= 0.98f; A.hp = A.max_hp; A.def *= 0.965f; }
            } else {
                A.resist[1] *= 0.7f;                               // +0xb8
                A.def *= 0.99f; A.atk *= 0.99f;
                A.hp *= 0.98f; A.max_hp = A.hp;
                if (d > 0) A.atk *= 1.02f;
            }
            A.flags12c |= 0x0a080040u;
            a.i(0x79c) = 10;
            a.i(0x7a0) = 0; a.i(0x7a4) = 1200;
            a.i(0x7a8) = idle_wait();
            a.i(0x7bc) = a.i(0x7c0) = a.i(0x7c4) = a.i(0x7c8) = 0;
            a.i(0x72c) = 0; a.cool() = 0; a.i(0x87c) = 0;
            a.phase() = 4;
            break;
        }
        case 1: case 2: case 4: main = true; break;
        default: break;
    }

    if (main) {                                                    // MAIN (0x4d29f5)
        a.phase() = 4; a.steer() = 0; a.action() = 2;
        int& sub = a.i(0x72c);
        switch (sub) {
            case 0:                                                // the opening hold
                a.action() = 1;
                if (a.i(0x87c) == 30) { force(300); sub = 1; a.i(0x87c) = 4000; }
                break;
            case 1: {                                              // the intro marker, the crystals
                if (S == 300 && e.aso_marker != 0) {
                    a.i(0x87c) = 0; out.set_marker = 0;
                    w->set_flag(180, 0); w->set_flag(179, 0);
                }
                const std::uint32_t t = std::uint32_t(a.i(0x87c));
                if (t >= 4000u) break;
                if (t % 20u == 0) {
                    const int k = int(t / 10u);
                    for (int s = k; s <= k + 1; ++s) {
                        Actor* c = w->spawn_child(A, kCrystal, {0, 0, 0}, A.level, true);
                        if (!c) continue;
                        static constexpr float kGz[4] = {4.0f, 10.0f, 16.0f, 22.0f};
                        c->pos = {(s & 1) ? -9.0f : 9.0f, 0.81f - c->height, kGz[std::min(s / 2, 3)]};
                        c->ai.i(0x79c) = 10;
                        c->ai.i(0x7a0) = s;
                        c->ai.f(0x7ac) = (s & 1) ? 0.0f : 180.0f;
                        c->ai.set_v(0x748, {(s & 1) ? -1.0f : 1.0f, 0.0f, 0.0f});
                        c->ai.alpha = 0.0f;
                        c->ai.i(0x854) = A.instance_id;
                        c->atk = A.atk;
                        w->sound_at(535, c->pos);                  // zako\z_360_01
                    }
                }
                if (std::uint32_t(a.i(0x87c)) > 60u) { sub = 5; a.cool() = 5; }
                break;
            }
            case 5:
                if (a.cool() == 0) { gated(301); sub = 6; a.i(0x7a8) = 0; }
                break;
            case 6:
                if (S == 301) break;
                crystals([&](Actor& c) { if (std::uint32_t(c.ai.i(0x730)) < 10u) c.ai.i(0x730) = 10; });
                sub = 10;
                a.cool() = int(r15(e) % 800u) + 400;
                break;
            case 10: {                                             // idle: the next own action
                if (std::uint32_t(a.i(0x7c0)) > 450u && far_from_centre(T) && (S == 2 || S == 4)) {
                    force(304); sub = 40; a.i(0x7c4) = 0; break;   // LIGHTNING
                }
                if (a.i(0x7a8) != 0) { a.i(0x7a8) -= 1; break; }
                if (S != 2 && S != 4) break;
                if (A.max_hp * 0.5f > A.hp && (e.rng() & 0x20000u) == 0) {   // RAW bit 17: 50%
                    force(304); sub = 40; a.i(0x7c4) = 0; break;
                }
                teleport();
                break;
            }
            case 20:                                               // the teleport fades
                if (S == 302) {
                    if (e.aso_marker != 0 && a.alpha > 0.0f) a.alpha = std::max(a.alpha - 0.05f, 0.0f);
                    break;
                }
                if (S == 303) {
                    if (e.aso_marker != 0 && 1.0f > a.alpha) a.alpha = std::min(a.alpha + 0.05f, 1.0f);
                    break;
                }
                if (a.alpha > 0.5f) {                              // came back in
                    a.alpha = 1.0f; sub = 10;
                    a.i(0x7a8) = idle_wait();
                } else {                                           // gone: where to reappear
                    a.alpha = 0.0f;
                    const std::uint32_t w1 = e.rng(), w2 = e.rng();
                    const float rad = unit(w2) * 8.0f + 0.0f;
                    const float ang = unit(w1) * 6.2831855f - 3.1415927f;
                    a.f(0x894) = float(std::cos(double(ang))) * rad + kCx;
                    a.f(0x898) = -0.19f;
                    a.f(0x89c) = float(std::sin(double(ang))) * rad + kCz;
                    a.i(0x7bc) = int(r15(e) % 60u) + 60;
                    sub = 30;
                }
                break;
            case 30: {                                             // invisible travel
                a.alpha = 0.0f;
                const float vx = a.f(0x894) - e.self.x, vz = a.f(0x89c) - e.self.z;
                const float l = std::sqrt(vx * vx + vz * vz);
                const float sx = l == 0.0f ? 0.0f : vx * (1.0f / l) * 0.1f;
                const float sz = l == 0.0f ? 0.0f : vz * (1.0f / l) * 0.1f;
                const bool near_ = std::fabs(vx) < 0.05f && std::fabs(vz) < 0.05f;
                if (a.i(0x7bc) != 0) a.i(0x7bc) -= 1;
                if (near_) {
                    if (a.i(0x7bc) != 0) break;
                    if (S == 2 || S == 4) { force(303); sub = 20; }
                    break;
                }
                float dx, dz;                                      // the per-axis step, as coded
                if (std::fabs(vx) > std::fabs(sx)) { dx = vx; dz = sz; }
                else if (std::fabs(vz) > std::fabs(sz)) { dx = sx; dz = vz; }
                else { dx = sx; dz = sz; }
                A.pos.x += dx; A.pos.z += dz;
                break;
            }
            case 40: {                                             // the lightning call, section 304
                if (S != 304) { teleport(); break; }
                const std::uint32_t m = std::uint32_t(e.aso_marker);
                if (m != 0) {
                    const int id = int(m / 10000u), rest = int(m % 10000u);
                    float h = A.hp;
                    if (ch == 2) h *= 0.5f;
                    const int per = int(std::max(30.0f, h / A.max_hp * 120.0f));
                    if (per != 0 && std::uint32_t(a.i(0x7c0)) % std::uint32_t(per) == 0 && id > 0 && rest > 0) {
                        a.i(0x7c4) += 1;
                        const Vec3 pos{T.x, e.target_floor + 0.5f, T.z};
                        AiEffect fx;                               // bos\jeno_m_zin: the warning circle
                        fx.id = id; fx.pos = pos; fx.scale = 3.0f; fx.power = 0.0f; fx.knock = 0.0f;
                        out.effects.push_back(fx);
                        w->sound_at(541, {pos.x, pos.y, pos.z});   // zako\z_360_07
                        for (int k = 0; k < kNBolts; ++k) {        // the strike pending on it
                            const int o = kBolts + 16 * k;
                            if (a.i(o + 12) != 0) continue;
                            a.set_v(o, pos);
                            a.i(o + 12) = rest * 1000 - d * 13 + 60;
                            break;
                        }
                    }
                }
                if ((!far_from_centre(T) && std::uint32_t(a.i(0x7c4)) > 0u) || std::uint32_t(a.i(0x7c4)) > 9u)
                    force(2);                                      // end the cast
                break;
            }
            default: break;
        }
    }

    // ---- COMMON (0x4d340a) ----
    const int f180 = w->flag(180, 0);
    if (a.i(0x7a4) != -1 && f180 == 0 && a.i(0x7c8) != 0) a.i(0x7c8) -= 1;          // (a)
    {                                                                                 // (b) mode reroll
        const int f = a.i(0x79c), m = f180;
        bool reroll = false;
        if (std::uint32_t(a.i(0x72c)) >= 10u) {
            if (S != 301 && a.cool() == 0) reroll = true;
            else if (d < 1 && m == 0) reroll = f == 40 || f % 10 == 1;
            else if (m == 3 && f != 40 && f % 10 == 0) reroll = true;
        }
        if (reroll) {
            if (a.i(0x72c) == 10) gated(301);
            a.cool() = int(r15(e) % 800u) + 400;
            int arr[6] = {1, 2, 3, 4, 1, 4};
            if (f != 40 && f % 10 == 0) { arr[3] = 0; arr[5] = 0; }
            if (m >= 0 && m < 6) arr[m] = 0;
            int n = 0;
            for (int v : arr) n += v != 0;
            const int r = n > 0 ? int(r15(e) % std::uint32_t(n)) : 0;
            int v = 0;
            for (int i = 0, k = 0; i < 6; ++i) if (arr[i] != 0) { if (k == r) { v = arr[i]; break; } ++k; }
            const int nm = std::max(v - 1, 0);
            w->set_flag(180, nm);
            if (nm != w->flag(179, 0)) {
                const float delta = (e.rng() & 0x20000u) ? -120.0f : 120.0f;   // RAW
                crystals([&](Actor& c) { c.ai.f(0x7b0) += delta; });
            }
            a.cool() = int(r15(e) % 800u) + 400;
        }
    }
    {                                                                                 // (c) formations
        bool arrival = true;
        if (!(S == 301 || std::uint32_t(a.i(0x7a0)) <= std::uint32_t(a.i(0x7a4))) && r15(e) % 1000u < 50u) {
            arrival = false;
            if (a.i(0x72c) == 10) gated(301);
            a.i(0x7a0) = 0; a.i(0x7a4) = -1;
            const int thr = A.max_hp * 0.5f > A.hp ? (d + 3) * 100 : 0;
            if (int(r15(e) % 1000u) < thr) {                       // ADVANCE
                const int f = ++a.i(0x79c);
                a.i(0x7a4) = hold_of(f, r15(e));
                if (f != 41) a.i(0x87c) = 0;
                crystals([&](Actor& c) { c.ai.i(0x72c) = f; c.ai.i(0x87c) = 0; });
            } else {                                               // NEW FORMATION
                int f;
                do {
                    int k = int(r15(e) % (A.hp > A.max_hp * 0.5f ? 3u : 7u));
                    if (k == 4) k = 1; else if (k == 5 || k == 6) k = 3;
                    f = (k + 1) * 10;
                } while (f == a.i(0x79c));
                a.i(0x79c) = f;
                const int row = std::clamp(f / 10, 1, 4) - 1;
                crystals([&](Actor& c) {
                    const int s = c.ai.i(0x7a0) & 7;
                    c.ai.i(0x79c) = f; c.ai.i(0x72c) = 0x32;
                    c.ai.f(0x894) = kX[row][s]; c.ai.f(0x89c) = kZ[row][s];
                });
            }
        }
        if (arrival && a.i(0x7a4) == -1) {                        // ARRIVAL (0x4d3b1d)
            bool travelling = false;
            crystals([&](Actor& c) { travelling |= c.hp >= 1.0f && c.ai.i(0x72c) == 0x32; });
            if (!travelling) { a.i(0x7a4) = hold_of(a.i(0x79c), r15(e)); a.i(0x87c) = 0; }
        }
        a.i(0x7a0) += 1;                                           // (d)
    }
    a.hittable = !(0.5f > a.alpha);                                // (e)
    if (a.f(0x880) != A.hp || std::isnan(A.hp)) {
        a.f(0x880) = A.hp;
        out.gauge_hp = A.hp; out.gauge_len = 6000.0f; out.gauge_emblem = 6;
        if (a.i(0x7a8) != 0) a.i(0x7a8) = std::max(a.i(0x7a8) - (ch == 2 ? 15 : 1), 0);
    }
    for (int k = 0; k < kNBolts; ++k) {                            // (f) the delayed strikes
        const int o = kBolts + 16 * k;
        if (a.i(o + 12) == 0) continue;
        const int v = --a.i(o + 12);
        if (v % 1000 != 0) continue;
        a.i(o + 12) = 0;
        AiEffect fx;                                               // bos\jeno_kaminari
        fx.id = v / 1000; fx.pos = a.v(o); fx.scale = 1.0f;
        fx.power = 10.0f; fx.knock = 10.0f; fx.rtype = 2;
        fx.status = (d >= 1 || far_from_centre(T)) ? 8 : 0;
        out.effects.push_back(fx);
    }
    // (g) the standard tail: the brain steers, turns, hovers and acts itself.
    a.dur() = -1;
    ai::steer(a, e);
    ai::turn(a, a.f(0x814) * (a.i(0x728) == 0xb ? 100.0f : 1.0f));
    if (A.flyer) ai::hover(a, A, e, nullptr, a.i(0x720));
    ai::act(a, p, e, &out);
    if (!e.target_alive) { a.hittable = false; A.def = 999.0f; }  // handler #6: FUN_0047aa80(A, 1)
    return out;
}

}  // namespace cleria::brains
