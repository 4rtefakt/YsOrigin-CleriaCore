// M810 — Hugo, a class-4 duel opponent (Toal's S_2080 duel, `Battle_2`). Handler
// FUN_004da070, brain FUN_004d7510 (AI index 74). Spec: build/ai_spec/M810.md.
//
// The handler runs the brain only while g_flags[130] ("boss AI live") is set; otherwise
// Hugo is a script puppet (FUN_0045f250) that only tops up / dismisses his Eyes of Fact.
// g_flags[131] freezes the AI. The brain keeps 5.5..9 u from the player, strafing on
// steer 5 and re-rolling the strafe heading every 30 ticks; when its cooldown is out it
// draws up to five moves from FUN_004d7420's HP-tiered table: a section-302 volley (sub
// 100: a 0xc6 shot, three at d >= 2, on each of Marker(1..5)), a section-274 homing fan
// (sub 200: 0xc7 kind-4 shots on Marker(1)), an eye command (sub 300), the eye recall
// (sub 600, section 313; forced once below 25% HP), a hop (sub 400) or, on a 15-tick
// cadence when close (sub 0x14), the barrier (sub 500, section 314: EF2 0x86, guard byte
// +0x399 for 240 ticks; hits shorten it). Taking hits fills +0x7ac and forces the barrier.
// Every idle tick it may warp (section 317, sub 1000) and pick an eye formation. Below 50%
// HP: an aura (EF2 39) and ATK + max(d,1)/2 + 1. The gauge is pushed with lenScale 4000
// (8500 when g_flags[231]).
// When its HP falls below 1 with the player alive the handler (m810_dead) sets HP to 1,
// clears flags 156 and 130 (ending the duel's Wait_FlagEqualInt(156, 0)), hides the gauge
// and runs the generic first-death step (unhittable).
// The Eyes of Fact (cast 0xb0, brain m900.cpp) are driven through their own fields: one is
// spawned per tick up to (231 ? 8 : 4) - (HP >= 50% ? 2 : 0) (+0x72c = Hugo's +0x7a0, +0x730 =
// a free slot); the eye command writes every Eye's +0x72c (the formation), launches one (3)
// or sets the recall +0x7c0 (1000); each shot order fires from the Eyes too (fire % 10), or
// sets their +0x7a4 / +0x7a8 shot timers. As the puppet with g_flags[214] clear he dismisses
// them.
// [not modelled] the roster's sweep of orphaned EF2 0x95 and the live 0xc7 count the 0x112
// picks are capped by (taken as 0) — both read the effect pool; the puppet path's count test
// and top-up (its Eyes run FUN_00470210); the keep-out box restore; the specular flash of sub
// 300; the barrier-expiry voice; sound preloads; +0x64 / +0x4be.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "cleria/actor.hpp"
#include "cleria/brains/brains.hpp"

namespace cleria::brains {
namespace {

constexpr float kDeg = 0.01745329238474369f;      // DAT_006bd0dc

int gflag(const AiEnv& e, int i, int unset) { return e.world ? e.world->flag(i, unset) : unset; }

AiOut still(EnemyAi& a) {                          // puppet / frozen: see m830.cpp
    AiOut out;
    out.skip_action_player = true;
    a.mode() = 0; a.speed = 0;
    return out;
}

// FUN_004d7420: one draw from the HP-tiered move table {1,1,2,3,3,1,4,2}.
int pick_move(const Actor& A, int arg, const AiEnv& e) {
    static constexpr int T[8] = {1, 1, 2, 3, 3, 1, 4, 2};           // 0x6be330, 0x6be310
    const int v = 1 + (A.hp < A.max_hp * 0.75f) + (A.hp < A.max_hp * 0.3f) + (arg > 4);
    int n = 0;                                                      // == the inlined count for v 1..5
    for (int t : T) n += t <= v;
    const int r = int(ai::r15(e)) % n;
    for (int k = 0, c = 0; k < 8; ++k) {
        if (T[k] > v) continue;
        if (r == c) return k;
        ++c;
    }
    return 0;
}

}  // namespace

void m810_dead(Actor& A, AiWorld& w) {
    EnemyAi& a = A.ai;
    if (w.flag(130, 1) == 0) return;                                // puppet: no dead branch
    // The live path with HP < 1 (0x4da5fe).
    if (Actor* P = w.player(); P && !P->alive()) {                  // the player lost
        A.hp = 1.1f;
        a.hittable = false; A.def = 999.0f;                         // FUN_0047aa80(edx = 1)
        return;
    } else if (P) {
        P->i_frames = 9999;                                         // a store, not a grant
    }
    w.set_flag(174, 1);                                             // DAT_0076bbd4
    w.set_flag(156, 0);                                             // DAT_0076bb8c: the duel is over
    w.set_flag(130, 0);                                             // DAT_0076bb24: back to a puppet
    A.hp = 1.0f;
    if (a.i(0x88c) != 0) { a.hook_kill_tags |= 1u; a.i(0x88c) = 0; }            // the barrier
    if (a.i(0x7bc) != 0 && a.i(0x7bc) != 1) a.hook_kill_tags |= 2u;              // the aura
    a.i(0x7bc) = 0;
    if (a.i(0x7c4) != 0) { a.hook_kill_tags |= 4u; a.i(0x7c4) = 0; }            // the 231 aura
    // [not modelled] DAT_0076a554 = -0.025 (reader FUN_00427c90 untraced)
    w.hide_boss_gauge();                                            // the push, then DAT_006d5fd8 = 0
    a.hittable = false;                                             // FUN_004780e0's first-death step
    // [not modelled] the rest of FUN_004780e0 (its EXP / drops are the host's kill path) and
    // the FUN_004472e0 VM step
    a.alpha = 1.0f;                                                 // +0x4d8
}

AiOut m810(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e) {
    using ai::r15;
    if (!e.actor) return {};
    Actor& A = *e.actor;
    // ---- the handler FUN_004da070
    if (gflag(e, 130, 1) == 0) {                                    // the puppet: the eye upkeep
        // g_flags[214] (DAT_0076bc74) == 0 dismisses every owned Eye (0x4da53d..0x4da5d4): HP 0,
        // +0x12c |= 0x80000000, +0x130 |= 0x1008, and their EF2 0x95 is killed (outside section
        // 5 [H: the Eye's section is not seen; killed always]). [not modelled] the rest: the count
        // test against (211 ? 0 : 210 ? 6 : DAT_0076c198 & 0x20 ? 4 : 2) and the top-up spawn,
        // whose Eyes run FUN_00470210's chain (+0x7e4..+0x804, +0x8a4).
        if (e.world && gflag(e, 214, 1) == 0) {
            e.world->for_each([&](Actor& o) {
                if (o.cast_id != 0xb0 || o.ai.i(0x854) != A.instance_id) return;
                o.hp = 0.0f;
                o.flags12c |= 0x80000000u;
                o.flags130 |= 0x1000u | 8u;
                if (o.ai.i(0x88c) != 0) { o.ai.hook_kill_tags |= 1u; o.ai.i(0x88c) = 0; }
            });
        }
        return still(a);
    }
    if (gflag(e, 131, 0) != 0) return still(a);                     // FUN_0045f470 only
    AiOut out;
    AiWorld* w = e.world;
    const int d = difficulty(e);
    const float fd = float(d);
    const bool f231 = gflag(e, 231, 0) != 0;                        // DAT_0076bcb8

    // ---- the brain FUN_004d7510
    a.set_v(0x808, e.self);                                         // [not modelled] the keep-out restore
    if (e.launched) return out;
    auto half = [&] { return A.max_hp * 0.5f > A.hp; };
    const int eyes_want = (f231 ? 8 : 4) - (half() ? 0 : 2);       // local_9c
    const float dz = e.self.z - e.target.z, dx = e.self.x - e.target.x;   // +0x888 = 0: the player
    const float dist = float(std::sqrt(double(float(double(dz) * double(dz)) + float(double(dx) * double(dx)))));
    int cmd = 0;                                                    // local_d0: the eye command
    int fire = 0;                                                   // local_d4
    const int shots_live = 0;                                       // local_10c [not modelled]
    bool warp = false;                                              // local_f0
    int S = e.section;
    auto req = [&](int id) { if (ai::request(S, id, false, e)) { out.section = id; out.section_force = false; } };
    auto force = [&](int id) { S = id; out.section = id; out.section_force = true; };
    auto steer_to = [&](int mode) { const int s = a.steer(); a.steer() = mode; ai::steer(a, e); a.steer() = s; };
    auto away_heading = [&](float span, float lo) {                 // 0x748 = dir(away + U*span - lo)
        const float th = float(std::atan2(double(-a.f(0x73c)), double(-a.f(0x734))));
        const float g = unit(e.rng()) * span - lo + th;
        a.f(0x748) = float(std::cos(double(g)));
        a.f(0x750) = float(std::sin(double(g)));
    };

    if (half() && a.i(0x7bc) == 0) {                                // the half-HP aura (0x4d77cb)
        AiEffect aura{39, e.self, 1.0f, true};
        aura.tag = 1;
        out.effects.push_back(aura);
        a.i(0x7bc) = 2;                                             // [H] stands in for the effect pointer
        if (f231) {
            AiEffect k; k.kill_tag = 1; out.effects.push_back(k);
            a.i(0x7bc) = 1;
        }
        A.atk += float(std::max(d, 1)) * 0.5f + 1.0f;
        out.sounds.push_back(0xb4);
        out.effects.push_back({50, e.self, 1.0f, true});            // [H] its -0.5 z offset not applied
    }
    // [not modelled] the roster (the first 8 live owned Eyes) and its sweep of EF2 0x95 effects
    // not held by an Eye in recall, and the live 0xc7 count: both read the effect pool
    // (0x4d7a59..0x4d7c25)
    if (a.i(0x7a4) != 0) a.i(0x7a4) -= 1;
    if (A.guard399 && e.was_hit && !e.took_damage) {                // a blocked hit shortens the barrier
        const int k = std::max(0, (d < 0 ? 25 * d * d : 0) - 30 * d + 15);
        a.i(0x79c) = a.i(0x79c) - k < 1 ? 1 : a.i(0x79c) - k;
        const int c = a.cool() - (d > 0 ? 30 * d : 0) - (d < 1 ? 30 : 60);
        a.cool() = c < 1 ? 1 : c;
    }
    if (a.i(0x79c) != 0) {
        A.guard399 = true;
        if (--a.i(0x79c) == 0 && a.i(0x88c) != 0) {                 // the barrier expires
            AiEffect k; k.kill_tag = 0; out.effects.push_back(k);
            a.i(0x88c) = 0;
            AiEffect pop{140, e.self, 1.0f, false};
            pop.knock = 0.0f;
            out.effects.push_back(pop);
            out.sounds.push_back(0x99);                             // [not modelled] the voice
        }
    } else {
        A.guard399 = false;
    }

    int& sub = a.i(0x728);
    switch (a.phase()) {
        case 0: {                                                   // ---- INIT (then the post-switch)
            ai::init(a, p, e);                                      // FUN_004782f0 + EI[71] +0x90..
            a.f(0x814) = 0.05f;
            a.set_v(0x808, e.self);
            // FUN_004202e0(level) [not modelled: the spawn computed it]; then:
            A.atk -= 1.5f;
            if (f231) {
                A.max_hp *= 1.25f; A.hp = A.max_hp;
                A.def = fd * 0.32499998807907104f + 19.0f + A.def;
                A.atk = fd * 0.5f + 31.0f + A.atk;
                A.exp_reward *= 4.0f;
                if (d == -2) A.atk += 1.5f;
                else if (d == -1) A.atk += 3.0f;
                else if (d > 0) A.atk *= 0.995f;
            } else {
                A.max_hp *= 1.0499999523162842f; A.hp = A.max_hp;
                A.atk += 5.0f;
                A.def += 4.0f;
                if (d == -2) A.atk += 1.5f;
                else if (d == -1) A.atk += 3.0f;
                else if (d > 0) { A.def *= 0.99f; A.atk *= 0.995f; }
            }
            a.i(0x79c) = 0; a.i(0x7a0) = 0; a.i(0x7a4) = 320; a.i(0x7a8) = 0;
            a.i(0x7bc) = 0; a.i(0x7c0) = 0; a.i(0x7c4) = 0; a.i(0x7ac) = 0;
            sub = 0;
            a.cool() = 120;
            if (w) {                                                // every Eye in the pool joins him
                int n = 0;
                w->for_each([&](Actor& o) {
                    if (o.cast_id != 0xb0) return;
                    o.ai.i(0x854) = A.instance_id; o.ai.i(0x730) = n++; o.ai.i(0x72c) = 0; o.ai.phase() = 0;
                });
            }
            A.flags12c = 0x1190000u;                                // = 0x1190000 | 0x1090000
            a.phase() = 4; a.steer() = 4; a.action() = 2;
            A.flags130 |= 9u;
            A.kind = 0x88; a.hittable = true;                       // +0x60 = 0x88
            break;                                                  // [not modelled] the sound preloads
        }
        case 1: case 2: case 4: {                                   // ---- MAIN
            const float th = float(std::atan2(double(a.f(0x73c)), double(a.f(0x734))));
            const float ang = float(std::fabs(ai::angle_diff(a.heading, th)));
            switch (sub) {
                case 0: {                                           // keep 5.5..9 u, strafe
                    int st = 4;
                    bool strafe = false;
                    if (dist > 9.0f) { a.steer() = 4; a.action() = 0x800; st = 4; }
                    else if (!(5.5f > dist)) {
                        if (a.steer() == 5 && 7.0f > dist) strafe = true;
                        else { a.steer() = 4; a.action() = 2; st = 4; }
                    } else {
                        if (a.steer() != 5) away_heading(0.6283185482025146f, 0.3141592741012573f);
                        strafe = true;
                    }
                    if (strafe) {
                        a.steer() = 5; a.action() = 0x800; st = 5;
                        if (std::uint32_t(a.i(0x87c)) % 30u == 0) {
                            const std::uint32_t R = e.rng();
                            const float g0 = float(std::atan2(double(a.f(0x750)), double(a.f(0x748))));
                            const float g = g0 + (unit(R) * 6.2831854820251465f + 0.0f);
                            a.f(0x748) = float(std::cos(double(g)));
                            a.f(0x750) = float(std::sin(double(g)));
                        }
                    }
                    if (a.cool() != 0) break;
                    if (st != 4) {
                        if (7.0f > dist) sub = 0x14;
                        break;
                    }
                    for (int t = 0; t < 5 && sub == 0; ++t) {       // up to five draws
                        const int arg = (f231 ? 8 : 4) - (half() ? 0 : 2);
                        switch (pick_move(A, arg, e)) {
                            case 0:                                 // the volley (section 302)
                                if (int(r15(e) % 1000u) < (f231 ? 500 : 300) &&
                                    (0.7853981852531433f > ang || (3.1415927410125732f > ang && 3.0f > dist))) {
                                    req(0x12e);
                                    out.vm_step = true;
                                    out.set_marker_block = 1;
                                    sub = 100;
                                }
                                break;
                            case 1:                                 // the homing fan (section 274)
                                if (int(r15(e) % 1000u) < (f231 ? 300 : 200) && shots_live < 3 * d + 10 && 6.0f > dist) {
                                    req(0x112); sub = 200;
                                }
                                break;
                            case 2:
                                if (int(r15(e) % 1000u) < (f231 ? 450 : 250)) { a.i(0x87c) = 0; sub = 300; }
                                break;
                            case 3:
                                if (int(r15(e) % 1000u) < (f231 ? 200 : 100)) { a.i(0x87c) = 0; sub = 600; }
                                break;
                            case 4: fire = 4; break;                // the eyes' timing only
                            case 5:
                                if (int(r15(e) % 1000u) < (f231 ? 150 : 100) && shots_live < 3 * d + 10) {
                                    req(0x112); sub = 200;
                                }
                                break;
                            case 7:
                                if (int(r15(e) % 1000u) < (f231 ? 325 : 200)) sub = 400;
                                break;
                            default: break;
                        }
                    }
                    break;
                }
                case 0x14:                                          // close: every 15 ticks
                    if (std::uint32_t(a.i(0x87c)) % 15u != 0) break;
                    if (int(r15(e) % 1000u) < (f231 ? 300 : 200) && shots_live < 3 * d + 10) {
                        req(0x112); sub = 200;
                    } else if (int(r15(e) % 1000u) < (f231 ? 450 : 250)) {
                        a.i(0x87c) = 0; sub = 300;
                    } else if (int(r15(e) % 1000u) < (f231 ? 650 : 400)) {
                        sub = 400;
                    } else if (int(r15(e) % 1000u) < (f231 ? 700 : 400) && a.i(0x79c) == 0) {
                        sub = 500; a.i(0x87c) = 0;
                    } else {
                        away_heading(3.769911527633667f, 1.8849557638168335f);
                        a.steer() = 5; a.action() = 0x800;
                        if (dist > 7.0f) sub = 0;
                    }
                    break;
                case 100:                                           // the volley
                    if (S == 0x12e) {
                        int mb = e.aso_marker_block;
                        for (int i = 0; i < 6; ++i) {
                            if (e.aso_marker != i || mb != i) continue;
                            fire = 0xb;
                            mb = i + 1; out.set_marker_block = mb;
                            if (i == 5 || int(r15(e) % 1000u) < 100) out.fast_forward = 6;
                        }
                    } else {
                        sub = 0; a.cool() = 180 - 25 * d;
                    }
                    break;
                case 200:                                           // the homing fan
                    a.steer() = 4; a.action() = 1;
                    if (S == 0x112) {
                        if (half()) out.vm_step = true;
                        if (std::uint32_t(e.aso_marker) < 1u && std::uint32_t(e.aso_frame) < 4u) {
                            a.action() = 2;                         // aim during the wind-up
                            steer_to(2);
                            ai::turn(a, a.f(0x814));
                        }
                        if (e.aso_marker == 1) { out.set_marker = 2; fire = 0x18; }
                    } else {
                        sub = 0; a.cool() = 210 - 25 * d;
                    }
                    break;
                case 300:                                           // command the eyes
                    if (std::uint32_t(a.i(0x87c)) >= 15u) {
                        sub = 0; cmd = 3; a.cool() = 60 - 10 * d;   // [not modelled] the flash
                    }
                    break;
                case 400:                                           // the hop
                    sub = 0;
                    if (S == 2 || S == 1 || S == 0) {
                        if (e.grounded) launch(A, 0.3f, out);       // FUN_0047a9c0(edx = 0)
                        out.sounds.push_back(0x1af);
                        if (int(r15(e) % 1000u) < 350) {
                            const float off = unit(e.rng()) * 1.600000023841858f - 0.800000011920929f;
                            push1(A, a.heading + off, 0.6000000238418579f);   // FUN_0047aa00
                        }
                    }
                    break;
                case 500:                                           // the barrier (section 314)
                    if (std::uint32_t(a.i(0x87c)) < 10u) {
                        force(0x13a);
                        out.vm_step = true;
                        out.set_marker_block = 1;
                        a.i(0x87c) = 10;
                        out.sounds.push_back(0x1a6);
                    } else if (e.aso_marker == 1) {
                        out.set_marker = 0; out.set_marker_block = 2;
                        if (a.i(0x88c) != 0) { AiEffect k; k.kill_tag = 0; out.effects.push_back(k); }
                        AiEffect bar{f231 ? 0x87 : 0x86, e.self, 1.0f, true};
                        bar.tag = 0;
                        bar.power = -5.0f; bar.knock = 10.0f; bar.rtype = 2; bar.status = 8;
                        out.effects.push_back(bar);
                        a.i(0x88c) = 1;                             // [H] stands in for the effect pointer
                        a.i(0x79c) = f231 ? 480 : 240;
                        out.sounds.push_back(f231 ? 0x9d : 0x1ab);
                        a.f(0x7ac) = 0.0f;
                    } else if (S != 0x13a) {
                        sub = 0; a.cool() = 150 - 30 * d;
                    }
                    break;
                case 600:                                           // recall the eyes (section 313)
                    a.steer() = 1; a.action() = 2;
                    if (std::uint32_t(a.i(0x87c)) < 10u) {
                        force(0x139);
                        out.sounds.push_back(0x1a7);
                        a.i(0x87c) = 10;
                    } else if (S == 0x139) {
                        if (e.aso_marker == 1) {
                            out.set_marker = 2;
                            cmd = 1000;
                            a.i(0x7c0) = 1;
                            out.sounds.push_back(0x9c);
                        }
                    } else {
                        const int T = 30 * (d + 7);
                        const int thr = std::max(0, (T >> 3) + 210 + (30 - T / 3) * d + (T >> 2));
                        if (std::uint32_t(a.i(0x87c)) > std::uint32_t(thr)) {
                            sub = 0;
                            a.cool() = std::max(0, (1 - d) * (T >> 2) + T + (T >> 3));
                        }
                    }
                    steer_to(0);
                    ai::turn(a, (fd * 0.2f + 0.25f) * a.f(0x814));
                    if (a.i(0x7a0) == 6) cmd = 1;
                    break;
                case 1000:                                          // after the warp (section 317)
                    a.steer() = 1; a.action() = 2;
                    if (a.i(0x7a8) != 0) a.i(0x7a8) -= 1;
                    else { sub = 0; force(2); }
                    break;
                default: break;
            }
            break;
        }
        default: break;
    }

    // ---- the post-switch (0x4d911c)
    if (S == 2 || S == 0 || S == 1) {
        if (a.i(0x7a4) == 0 && int(r15(e) % 1000u) < 450) {         // the warp
            warp = true;
            force(0x13d);
            sub = 1000; a.i(0x7a8) = 20;
        }
        if (a.i(0x7c0) == 0 && A.max_hp * 0.25f > A.hp) { a.i(0x87c) = 0; sub = 600; }
    }
    if (fire != 0) {                                                // the shots (0x4d91c4)
        // The shooters: Hugo (index 0), then every live Eye in the pool (any owner, +0x11 & 2
        // clear, +0x72c != 2), at most 8. Hugo uses fire / 10, each Eye fire % 10.
        std::vector<Actor*> pool;                                   // DAT_00730098, engine order
        std::vector<Actor*> sh{&A};
        if (w) w->for_each([&](Actor& o) {
            pool.push_back(&o);
            if (sh.size() < 9 && o.cast_id == 0xb0 && !o.destroy_requested() && o.ai.i(0x72c) != 2) sh.push_back(&o);
        });
        const int ns = int(sh.size());
        for (int k = 0; k < ns; ++k) {
            Actor& X = *sh[k];
            const bool hugo = X.cast_id == 0xa7;
            const float h = &X == &A ? a.heading : X.ai.heading;
            Vec3 base = &X == &A ? e.self : Vec3{X.pos.x, X.pos.y + X.height, X.pos.z};
            if (hugo) base = {float(std::cos(double(h))) + base.x, base.y + 0.0f, float(std::sin(double(h))) + base.z};
            // The bytes test POOL entry k here, not Eye k (0x4d930f: [esp+0x88] is the pool array).
            else if (k < int(pool.size()) && (pool[k]->ai.i(0x72c) == 3 || pool[k]->ai.i(0x7c0) != 0)) continue;
            const int mode = k == 0 ? fire / 10 : fire % 10;
            auto volley = [&] {                                     // 0xc6, kind 0x1b (0x4d9789)
                const int n = d > 1 && hugo ? 3 : 1;
                for (int i = 0; i < n; ++i) {
                    float off = float((i + 1) >> 1) * 0.26179999113082886f;
                    if (i & 1) off *= -1.0f;
                    const double g = double(h + off);
                    AiBullet b;
                    b.kind = 0x1b; b.effect = 0xc6;
                    b.pos = base; b.dir = {float(std::cos(g)), 0.0f, float(std::sin(g))};
                    b.speed = fd * 0.019999999552965164f + (f231 ? 0.30000001192092896f : 0.22499999403953552f);
                    b.power = 0.0f; b.knock = 1.0f;
                    out.bullets.push_back(b);
                }
                out.sounds.push_back(0x94);                         // [H] at the actor, not the shooter
            };
            auto timers = [&] {                                     // the Eye fires later (0x4d96dd)
                if (k == 0) return;
                const float f = f231 ? 1.0f : 1.5f;
                const int nshots = int(f * float(d + 5));
                X.ai.i(0x7a4) = nshots < 1 ? 1 : nshots;
                const int den = ns - 1 < 1 ? 1 : ns - 1;
                X.ai.i(0x7a8) = int(15.0f / f) * k / den;            // staggered: k / (shooters - 1)
            };
            if (mode == 1) {
                if (hugo) { volley(); continue; }
                X.ai.i(0x7a0) = 5; X.ai.f(0x7cc) = 0.2f;            // hold the facing, recoil
                if (X.ai.i(0x72c) != 4) volley();
                else timers();
            } else if (mode == 4) {
                timers();
            } else if (mode == 2) {                                 // 0xc7, kind 4 (homing)
                if (!hugo) { X.ai.i(0x7a0) = 15; X.ai.f(0x7cc) = 0.5f; }
                const int m = d < 1 ? 1 : d;
                const int kk = f231 ? 2 : 1;
                auto count = [&] { return (half() ? 2 * m : m) * kk; };
                const int N0 = count();
                const int q = (N0 & ~1) < 2 ? 1 : N0 >> 1;
                const float step = float(N0) * 20.0f / float(q) * kDeg;
                int hm = f231 ? 2 * d + 3 : d + 2;
                if (hm < 1) hm = 1;
                for (int i = 0; i < count(); ++i) {
                    const float sgn = (i & 1) ? -1.0f : 1.0f;
                    float g;
                    if (count() & 1) g = float((i + 1) >> 1) * step * sgn + h;
                    else g = float((i >> 1) + 1) * step * sgn + (step * 0.5f + h);
                    AiBullet b;
                    b.kind = 4; b.effect = 0xc7;
                    b.pos = base; b.dir = {float(std::cos(double(g))), 0.0f, float(std::sin(double(g)))};
                    b.speed = 0.18999999761581421f; b.accel = -0.0013749999925494194f;
                    b.power = 5.0f; b.knock = 10.0f;
                    b.home_period = 1; b.home_rate = float(hm) * kDeg;
                    b.f16c = 10.0f; b.f170 = 60.0f;                 // homing from age 10 to 60
                    out.bullets.push_back(b);
                    // [not modelled] fx +0x1ec = 2, and the re-clock of the effect's first particle
                    // (FUN_00564a20 = the first p with p+0x1a0 == fx): remaining p+0x1c4 and life
                    // p+0x1c0 = (d + 15) * 30 - R % 200 each (the fields: BULLETS.md §3.12) [H: a
                    // particle is found, so both draws happen]
                    (void)r15(e); (void)r15(e);
                }
                out.sounds.push_back(0x95);                         // [H] at the actor, not the shooter
            }
        }
    }
    if (warp) {                                                     // the eye formation (0x4d990e)
        int T[7] = {2, 2, 2, 8, 8, 4, 4};
        int n = 3 + 2 * (eyes_want >= 8) + 2 * (eyes_want >= 4);
        int r = int(r15(e)) % n;
        for (int k = 0, c = 0; k < 7; ++k) {
            if (eyes_want < T[k]) continue;
            if (r != c) { ++c; continue; }
            bool take = k != a.i(0x7a0);
            if (take && k == 2) take = int(r15(e) % 1000u) <= (f231 ? 450 : 250);
            if (take) { cmd = k + 1; break; }
            T[k] = 999999;                                          // excluded: draw again
            --n;
            r = int(r15(e)) % n;
            c = 0; k = -1;
        }
        a.i(0x7a4) = 300;
    }
    if (cmd != 0) {                                                 // the eye orders (0x4d9a16)
        if (cmd != 3 && cmd < 1000) a.i(0x7a0) = cmd - 1;
        // Every live Eye in the pool (any owner): 1000 = the recall, +0x7c0 = 30(d + 7); else
        // the formation +0x72c = cmd - 1 (not for a dashing Eye) and +0x79c = 0; 3 = launch ONE
        // Eye (mode 2), SE 0x9a.
        if (w) {
            bool done = false;
            w->for_each([&](Actor& o) {
                if (done || o.cast_id != 0xb0 || o.destroy_requested()) return;
                if (cmd == 1000) { o.ai.i(0x7c0) = 30 * (d + 7); return; }
                if (o.ai.i(0x72c) == 2) return;
                o.ai.i(0x72c) = cmd - 1;
                o.ai.i(0x79c) = 0;
                if (cmd == 3) { out.sounds.push_back(0x9a); done = true; }
            });
        }
    }
    if (w) {                                                        // the top-up spawn (0x4d9ad6)
        // Count every live Eye (any owner); the slot = the first free one above the slots seen.
        int occ[8] = {};
        int n = 0, slot = 0;
        w->for_each([&](Actor& o) {
            if (o.cast_id != 0xb0 || o.destroy_requested()) return;
            const int s = o.ai.i(0x730);
            ++n;
            if (s >= 8 || s < 0) return;                            // [H] s < 0 never occurs
            occ[s] = 1;
            if (slot != s) return;
            slot = 8;
            for (int k = s + 1; k < 8; ++k)
                if (occ[k] == 0) { slot = k; break; }
        });
        if (n < eyes_want) {                                        // one a tick
            if (Actor* c = w->spawn_child(A, 0xb0, {0.0f, 0.0f, 0.0f}, A.level, true)) {   // FUN_00476d70
                c->ai.i(0x72c) = a.i(0x7a0);
                c->ai.i(0x730) = slot;
                c->ai.i(0x854) = A.instance_id;
            }
        }
    }
    if (f231 && a.i(0x7c4) == 0) {                                  // the 231 aura
        AiEffect au{0x21c, e.self, 1.0f, true};
        au.tag = 2;
        out.effects.push_back(au);
        a.i(0x7c4) = 1;                                             // [H] stands in for the effect pointer
        A.atk += float(std::max(d, 1)) * 1.5f + 1.0f;
    }
    if (a.f(0x880) != A.hp) {                                       // the gauge; each change fills +0x7ac
        a.f(0x880) = A.hp;
        out.gauge_hp = A.hp; out.gauge_len = f231 ? 8500.0f : 4000.0f;
        a.f(0x7ac) += 1.0f;
    }
    if (a.f(0x7ac) > 0.0f) {                                        // ... which drains
        const float dec = (d < 0 ? float(-d) - 1.0f : 1.0f) * 0.05000000074505806f;
        a.f(0x7ac) = 0.0f > a.f(0x7ac) - dec ? 0.0f : a.f(0x7ac) - dec;
    }
    if (a.f(0x7ac) > 4.0f - fd * 0.5f && sub != 500 && a.i(0x79c) == 0 && sub != 600) {
        sub = 500; a.i(0x87c) = 0;                                  // hit too often: the barrier
    }
    {                                                               // lead the player (+0x894)
        const float k = float(std::clamp(d + 1, 0, 3));
        a.set_v(0x894, {e.target_vel.x * 10.0f * k + e.target.x, e.target_vel.y * 10.0f * k + e.target.y,
                        e.target_vel.z * 10.0f * k + e.target.z});
    }
    a.dur() = -1;
    ai::steer(a, e);
    ai::turn(a, a.f(0x814) * 1000.0f);                              // DAT_006bde6c: he snaps round
    if (A.flyer) ai::hover(a, A, e, nullptr, a.i(0x720));
    ai::act(a, p, e, &out);

    if (!e.target_alive) { a.hittable = false; A.def = 999.0f; }    // the handler's FUN_0047aa80
    return out;
}

}  // namespace cleria::brains
