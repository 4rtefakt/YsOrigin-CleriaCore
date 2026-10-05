// Headless tests for src/cleria/enemy_ai.hpp — the shared enemy machinery and the M100
// brain, driven through scripted situations with a deterministic xorshift128.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cleria/actor.hpp"
#include "cleria/enemy_ai.hpp"
#include "cleria/enemy_mover.hpp"
#include "cleria/factory_flags.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[ai] %-62s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

// The engine's xorshift128 (0x74622c), seeded deterministically.
struct XorShift {
    std::uint32_t x = 123456789, y = 362436069, z = 521288629, w = 88675123;
    std::uint32_t operator()() {
        const std::uint32_t t = x ^ (x << 11);
        x = y; y = z; z = w;
        w = w ^ (w >> 19) ^ t ^ (t >> 8);
        return w;
    }
};

// M100 Riffligan's ENEMYINFO movement row (turn, base, walk, run) and a sight value.
// The machinery reads the actor's copy (+0x814..) of raw = EI+0x90..+0xcc, as the engine does.
static EnemyAiParams params(float turn, float base, float walk, float run, float sight) {
    EnemyAiParams p{turn, base, walk, run, sight, {}};
    p.raw[0] = turn; p.raw[1] = base; p.raw[2] = walk; p.raw[3] = run; p.raw[8] = sight;
    return p;
}
static EnemyAiParams m100() { return params(0.1f, 0.0325f, 1.0f, 1.5f, 1.0f); }

struct Rig {
    XorShift rng;
    bool wall = false;      // block every segment
    AiEnv env;
    Rig() {
        env.self = {0, 1.075f, 0};
        env.rng = [this] { return rng(); };
        env.blocked = [this](const Vec3&, const Vec3&) { return wall; };
        env.has_floor = [](float, float, float, float) { return true; };
    }
};

int main() {
    const EnemyAiParams p = m100();

    {   // rotate_toward
        check(std::fabs(rotate_toward(0.0f, 1.0f, 0.1f) - 0.1f) < 1e-6f, "rotate_toward steps by the rate");
        check(rotate_toward(0.0f, 0.05f, 0.1f) == 0.05f, "rotate_toward snaps when within the rate");
        check(std::fabs(rotate_toward(3.0f, -3.0f, 0.1f) - 3.1f) < 1e-5f, "rotate_toward goes the short way round");
    }
    {   // Init, then an unseen target: wander, no acquisition.
        Rig r; EnemyAi ai;
        r.env.target = {-12.0f, 1.075f, 0};   // behind (heading 0 faces +X), beyond 8u
        enemy_ai_tick_m100(ai, p, r.env);
        check(ai.phase() == 1 && ai.initialised, "init -> unaware");
        for (int i = 0; i < 30; ++i) enemy_ai_tick_m100(ai, p, r.env);
        check(ai.phase() == 1, "target behind and > 8u: not acquired");
        check(ai.mode() == 1 && std::fabs(ai.speed - 0.0325f) < 1e-6f, "unaware: wanders at WALK speed (EI 0x94*0x98)");
    }
    {   // In the +/-45 deg cone within 15u: acquired.
        Rig r; EnemyAi ai;
        r.env.target = {12.0f, 1.075f, 2.0f};
        enemy_ai_tick_m100(ai, p, r.env);
        enemy_ai_tick_m100(ai, p, r.env);
        check(ai.phase() == 2, "in front within 15u with LOS: acquired");
    }
    {   // Same, but a wall in between.
        Rig r; EnemyAi ai; r.wall = true;
        r.env.target = {12.0f, 1.075f, 2.0f};
        for (int i = 0; i < 5; ++i) enemy_ai_tick_m100(ai, p, r.env);
        check(ai.phase() == 1, "no line of sight: not acquired");
    }
    {   // Within 8u, any direction.
        Rig r; EnemyAi ai;
        r.env.target = {-6.0f, 1.075f, 0};
        for (int i = 0; i < 2; ++i) enemy_ai_tick_m100(ai, p, r.env);
        check(ai.phase() == 2, "within 8u: acquired from behind");
    }
    {   // Hit from behind far away: acquired.
        Rig r; EnemyAi ai;
        r.env.target = {-14.0f, 1.075f, 0};
        enemy_ai_tick_m100(ai, p, r.env);
        r.env.was_hit = true;
        enemy_ai_tick_m100(ai, p, r.env);
        check(ai.phase() == 2, "being hit acquires the attacker");
    }
    {   // Off-screen and unaware: frozen.
        Rig r; EnemyAi ai; r.env.culled = true;
        r.env.target = {5.0f, 1.075f, 0};
        for (int i = 0; i < 10; ++i) enemy_ai_tick_m100(ai, p, r.env);
        check(ai.phase() == 0 && ai.mode() == 0, "off-screen and unaware: no AI at all");
    }
    {   // Engaged, target in front 6u away: approach at RUN speed.
        Rig r; EnemyAi ai;
        r.env.target = {6.0f, 1.075f, 0};
        for (int i = 0; i < 4; ++i) enemy_ai_tick_m100(ai, p, r.env);
        check(ai.phase() == 2 && ai.action() == 0x400, "in front, out of reach: approach (0x400)");
        check(ai.mode() == 2 && std::fabs(ai.speed - 0.0325f * 1.5f) < 1e-6f, "approach RUNS (EI 0x94*0x9c), not walks");
        r.env.target = {0.5f, 1.075f, 0};      // inside the 0.75 stop
        enemy_ai_tick_m100(ai, p, r.env);
        check(ai.mode() == 0, "approach stops inside 0.75");
    }
    {   // Turning: heading moves toward the target at EI+0x90 per tick.
        Rig r; EnemyAi ai;
        r.env.target = {0.0f, 1.075f, 6.0f};   // 90 deg left of heading 0, within 8u
        for (int i = 0; i < 2; ++i) enemy_ai_tick_m100(ai, p, r.env);   // init + acquire
        const float h0 = ai.heading;
        enemy_ai_tick_m100(ai, p, r.env);
        check(std::fabs((ai.heading - h0) - p.turn) < 1e-5f || ai.action() == 1,
              "heading turns at the EI+0x90 rate (or is locked by a hold)");
    }
    {   // The attack cadence: in reach and in front.
        Rig r; EnemyAi ai;
        r.env.target = {1.2f, 1.075f, 0};      // planar dist^2 1.44 <= 2.5, dead ahead
        for (int i = 0; i < 2; ++i) enemy_ai_tick_m100(ai, p, r.env);
        int first_attack = -1, second_attack = -1;
        for (int t = 0; t < 400; ++t) {
            const AiOut o = enemy_ai_tick_m100(ai, p, r.env);
            if (o.attack) { if (first_attack < 0) first_attack = t; else if (second_attack < 0) second_attack = t; }
        }
        std::printf("[ai]   attack ticks: first %d, second %d\n", first_attack, second_attack);
        check(first_attack >= 10 && first_attack <= 40, "first strike after the 10-tick hold + 30-tick aim");
        const int gap = second_attack - first_attack;
        check(gap >= 100 + 10 && gap <= 139 + 45, "cooldown 100..139 ticks (+ the next pre-attack)");
    }
    {   // Difficulty: Nightmare (d=2) shortens the cooldown by 20.
        Rig r; EnemyAi ai; r.env.difficulty = 2;
        r.env.target = {1.2f, 1.075f, 0};
        for (int i = 0; i < 2; ++i) enemy_ai_tick_m100(ai, p, r.env);
        int a = -1;
        for (int t = 0; t < 200 && a < 0; ++t) if (enemy_ai_tick_m100(ai, p, r.env).attack) a = t;
        check(a >= 0 && ai.cool() >= 80 - 1 && ai.cool() <= 119, "d=2: cooldown 80..119");
    }
    {   // A target on another floor (|dy| >= 2, grounded): never attacked.
        Rig r; EnemyAi ai;
        r.env.target = {1.2f, 3.5f, 0};
        for (int i = 0; i < 2; ++i) enemy_ai_tick_m100(ai, p, r.env);
        bool attacked = false;
        for (int t = 0; t < 200; ++t) attacked |= enemy_ai_tick_m100(ai, p, r.env).attack;
        check(!attacked, "target 2+ above and grounded: no attack");
        r.env.target_grounded = false;
        for (int t = 0; t < 200; ++t) attacked |= enemy_ai_tick_m100(ai, p, r.env).attack;
        check(attacked, "...but a jumping target can be hit");
    }
    {   // Losing sight: breadcrumbs, then disengage.
        Rig r; EnemyAi ai;
        r.env.target = {6.0f, 1.075f, 0};
        for (int i = 0; i < 3; ++i) enemy_ai_tick_m100(ai, p, r.env);
        check(ai.phase() == 2, "engaged before the wall");
        r.wall = true;
        int lost_at = -1;
        for (int t = 0; t < 200 && lost_at < 0; ++t) {
            enemy_ai_tick_m100(ai, p, r.env);
            if (ai.phase() == 1) lost_at = t;
        }
        check(lost_at > 0, "line of sight lost through the crumb trail -> disengage");
    }

    {   // The stun (build/ai_spec/REACT.md §2): +0x3bc & 0x4000 rolls 60%; a 99-tick hold.
        int stunned = 0, held = -1;
        bool exit_ok = false, entry_ok = false;
        for (int s = 0; s < 40; ++s) {
            Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
            r.env.target = {3.0f, 1.075f, 0};
            for (int i = 0; i < 3; ++i) enemy_ai_tick_m100(ai, p, r.env);
            r.env.events = 0x4000;
            AiOut o = enemy_ai_tick_m100(ai, p, r.env);
            r.env.events = 0;
            if (ai.phase() != 3) continue;
            ++stunned;
            entry_ok = o.section == 0x13b && o.sounds.size() == 1 && o.sounds[0] == 0xb8 && ai.dur() == 98;
            r.env.section = 0x13b;
            for (int t = 1; t <= 120; ++t) {
                o = enemy_ai_tick_m100(ai, p, r.env);
                if (ai.phase() == 2) { held = t; exit_ok = o.section == 4 && o.section_force; break; }
            }
        }
        std::printf("[ai]   stun: %d / 40 rolls stunned; held %d ticks\n", stunned, held);
        check(stunned > 15 && stunned < 35, "0x4000 hit: ~60% stun (61/100 incl. 0)");
        check(entry_ok, "stun entry: section 0x13B, sound 0xB8, hold 99");
        check(held == 99 && exit_ok, "stun exit after 99 ticks: phase 2, section 4 forced");
        Rig h; EnemyAi ai; h.env.target = {3.0f, 1.075f, 0}; h.env.character = 2;
        for (int i = 0; i < 3; ++i) enemy_ai_tick_m100(ai, p, h.env);
        bool any = false;
        for (int k = 0; k < 20; ++k) { h.env.events = 0x4000; enemy_ai_tick_m100(ai, p, h.env); any |= ai.phase() == 3; }
        check(!any, "Hugo never stuns");
    }

    // ---- M150 Normous / M480 Nustarl: the turrets (build/ai_spec/M150.md, M480.md) ----
    {
        const EnemyBrain m150 = enemy_brain_for(8), m480 = enemy_brain_for(41);
        check(m150 != nullptr && m480 != nullptr, "turret brains registered (AI index 8, 41)");
        const EnemyAiParams tp = params(0.135f, 0.0f, 0.0f, 0.0f, 0.0f);   // base speed 0, turn 0.135
        Rig r; EnemyAi ai;
        r.env.target = {6.0f, 1.075f, 0};          // within 8 u: acquired at once
        int section = -1, sec_tick = -1;
        for (int t = 0; t < 40 && section < 0; ++t) {
            const AiOut o = enemy_ai_tick(ai, tp, r.env, m150);
            if (o.section >= 0) { section = o.section; sec_tick = t; }
        }
        check(section == 7 && ai.dur() == 129, "M150 in range: volley = section 7, 130 ticks");
        check(ai.mode() == 0, "M150 never moves (base speed 0)");
        int fired_again = -1;
        for (int t = sec_tick + 1; t < sec_tick + 400 && fired_again < 0; ++t)
            if (enemy_ai_tick(ai, tp, r.env, m150).section == 7) fired_again = t;
        // d=0: volley 130 + 2 transition ticks + rest 120 (+ first-rest jitter < 60) + 1.
        const int gap = fired_again - sec_tick;
        std::printf("[ai]   M150 volley gap %d ticks\n", gap);
        check(gap >= 130 + 120 && gap <= 130 + 120 + 60 + 6, "M150 rest = 40*(3-d) between volleys");
        const float h = ai.heading;
        check(std::fabs(h) < 1e-3f || std::fabs(h - 0.785398f) < 1e-3f,
              "M150 heading settles on a fixed world angle (0 or pi/4)");

        Rig r2; EnemyAi far;
        r2.env.target = {10.5f, 1.075f, 0};        // > d+10 for M150, <= d+11 for M480 (d=0)
        r2.env.was_hit = true;                     // wake it from out of the sight cone
        bool m150_fired = false, m480_fired = false;
        for (int t = 0; t < 20; ++t) m150_fired |= enemy_ai_tick(far, tp, r2.env, m150).section == 7;
        EnemyAi far2;
        for (int t = 0; t < 20; ++t) m480_fired |= enemy_ai_tick(far2, tp, r2.env, m480).section == 7;
        check(!m150_fired && m480_fired, "range: M150 d+10 (silent at 10.5), M480 d+11 (fires)");
        check(far2.dur() == 99 || far2.action() == 2, "M480 volley lasts 100 ticks");
    }

    // ---- M130 Selnarg (build/ai_spec/M130.md) ----
    {
        const EnemyBrain m130 = enemy_brain_for(6);
        check(m130 != nullptr, "Selnarg brain registered (AI index 6)");
        // Run a fresh Selnarg 2u in front until its first strike; report section + hold.
        auto first_strike = [&](int skip, int diff_index, bool easy, int* backoffs) {
            Rig r; EnemyAi ai;
            for (int i = 0; i < skip; ++i) r.rng();
            r.env.target = {2.0f, 1.075f, 0};
            r.env.diff_index = diff_index; r.env.diff_easy = easy;
            int max_back = 0;
            for (int t = 0; t < 300; ++t) {
                const AiOut o = enemy_ai_tick(ai, p, r.env, m130);
                if (ai.i(0x7bc) > max_back) max_back = ai.i(0x7bc);
                if (o.section >= 0) { if (backoffs) *backoffs = max_back; return std::pair{o.section, ai.dur()}; }
            }
            return std::pair{-1, 0};
        };
        int charges = 0, swords = 0;
        bool holds_ok = true, backoff_ok = true;
        for (int s = 0; s < 200; ++s) {
            int back = 0;
            const auto [sec, dur] = first_strike(s, 0, false, &back);
            if (sec == 7) { ++charges; holds_ok &= dur == 129; backoff_ok &= back <= 4; }
            else if (sec == 8) { ++swords; holds_ok &= dur == 99; }
            else holds_ok = false;
        }
        std::printf("[ai]   M130 plans over 200 rolls: charge %d, sword %d\n", charges, swords);
        check(charges + swords == 200 && charges > 40 && charges < 80, "plan roll ~30% shield charge (7) / ~70% sword (8)");
        check(holds_ok, "Normal: charge holds 130, sword 100");
        check(backoff_ok, "charge backs off at most 4 bursts to reach 3.0..3.8");
        bool hard_ok = true, easy_ok = true;
        for (int s = 0; s < 40; ++s) {
            const auto [sh, dh] = first_strike(s, 1, false, nullptr);
            hard_ok &= (sh == 7 && dh == 104) || (sh == 8 && dh == 79);
            const auto [se, de] = first_strike(s, 0, true, nullptr);
            easy_ok &= (se == 7 && de == 149) || (se == 8 && de == 119);
        }
        check(hard_ok, "index >= 1: holds 105 / 80");
        check(easy_ok, "Easy: holds +20 (150 / 120)");
        {   // Cool-down = hold + 10 + R%30, then a fresh roll.
            Rig r; EnemyAi ai; r.env.target = {2.0f, 1.075f, 0};
            int a1 = -1, a2 = -1, dur1 = 0;
            for (int t = 0; t < 600 && a2 < 0; ++t) {
                const AiOut o = enemy_ai_tick(ai, p, r.env, m130);
                if (o.section >= 0) { if (a1 < 0) { a1 = t; dur1 = ai.dur() + 1; } else a2 = t; }
            }
            std::printf("[ai]   M130 strikes at %d, %d (hold %d)\n", a1, a2, dur1);
            check(a2 - a1 >= dur1 + 10 && a2 - a1 <= dur1 + 39 + 60, "next strike after hold + 10..39 + re-plan");
        }
    }

    // ---- M910 Nooze (build/ai_spec/M910.md) ----
    {
        const EnemyBrain m910 = enemy_brain_for(84);
        check(m910 != nullptr, "Nooze brain registered (AI index 84)");
        EnemyAiParams np = params(0.03f, 0.015f, 1.0f, 3.0f, 0.0f);
        np.raw[6] = 0.1f; np.raw[7] = 0.15f;                    // EI+0xa8 up / +0xac down
        {   // The pulse: run section 1 ramps 0.1/tick to 3.0 in Marker(0), down 0.15 otherwise.
            Rig r; EnemyAi ai; r.env.target = {-14.0f, 1.075f, 0}; r.env.section = 1;
            enemy_ai_tick(ai, np, r.env, m910);                    // init
            // Init zeroes it, then falls through into the ramp on the same tick.
            check(std::fabs(ai.f(0x820) - 0.1f) < 1e-6f && ai.f(0x7d0) == 3.0f,
                  "init: run target 3.0 saved; zeroed, then the same tick's ramp step");
            for (int t = 0; t < 40; ++t) enemy_ai_tick(ai, np, r.env, m910);
            check(std::fabs(ai.f(0x820) - 3.0f) < 1e-4f, "marker 0 in run: ramps up to 3.0");
            r.env.aso_marker = 1;
            for (int t = 0; t < 4; ++t) enemy_ai_tick(ai, np, r.env, m910);
            check(std::fabs(ai.f(0x820) - 2.4f) < 1e-4f && ai.f(0x81c) == ai.f(0x820),
                  "marker != 0: ramps down 0.15/tick (both multipliers)");
            r.env.section = 0; r.env.aso_marker = 0;
            for (int t = 0; t < 40; ++t) enemy_ai_tick(ai, np, r.env, m910);
            check(std::fabs(ai.f(0x81c) - 1.0f) < 1e-4f, "walk section 0: target 1.0");
        }
        // Splits on the hit reaction's marker 9 (15%), given >= 5 HP lost since init.
        auto split_rate = [&](float hp_now, int pool, bool* child_ok, float max_hp = 100) {
            int n = 0;
            for (int s = 0; s < 400; ++s) {
                Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
                r.env.target = {6.0f, 1.075f, 0};
                r.env.hp = 100; r.env.max_hp = max_hp;
                enemy_ai_tick(ai, np, r.env, m910);                // init: snapshot 100
                r.env.hp = hp_now; r.env.section = 4; r.env.aso_marker = 9; r.env.pool_count = pool;
                const AiOut o = enemy_ai_tick(ai, np, r.env, m910);
                if (!o.spawns.empty()) {
                    ++n;
                    // chunk = max(0.31*100, 0.5*80) = 40 -> both 40; EXP x0.5025
                    if (child_ok) *child_ok &= o.spawns[0].cast == 0xb1 && o.spawns[0].hp == 40.0f &&
                                               o.set_hp == 40.0f && o.exp_scale == 0.5025f &&
                                               o.spawns[0].exp_copy;
                    // The latch: a second marker-9 tick cannot split again.
                    const AiOut o2 = enemy_ai_tick(ai, np, r.env, m910);
                    if (child_ok) *child_ok &= o2.spawns.empty();
                }
            }
            return n;
        };
        bool child_ok = true;
        const int hits = split_rate(80, 0, &child_ok);
        std::printf("[ai]   M910 hit-reaction splits: %d / 400\n", hits);
        check(hits > 35 && hits < 90, "marker 9 in section 4: splits ~15%");
        check(child_ok, "split: HP 80 -> both 40 (max(0.31 max, HP/2)), EXP x0.5025, latched");
        // (max 1000 so the stand clause's HP >= 10% max cannot fire either)
        check(split_rate(97, 0, nullptr, 1000) == 0, "under 5 HP lost: no hit split");
        check(split_rate(80, 24, nullptr) == 0, "actor pool at 24: no split");
        {   // The floor: a tiny parent leaves both at 1.1.
            int ok = 0;
            for (int s = 0; s < 400 && !ok; ++s) {
                Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
                r.env.target = {6.0f, 1.075f, 0}; r.env.hp = 100; r.env.max_hp = 100;
                enemy_ai_tick(ai, np, r.env, m910);
                r.env.hp = 20; r.env.section = 4; r.env.aso_marker = 9;
                const AiOut o = enemy_ai_tick(ai, np, r.env, m910);
                if (!o.spawns.empty()) ok = (o.set_hp == 1.1f && o.spawns[0].hp == 1.1f) ? 1 : -1;
            }
            check(ok == 1, "split below 1 HP floors both halves to 1.1");
        }
        {   // The lunge: close, in front, grounded; cooldown 120 + 10d + 0..59 (d = -M100's d).
            auto lunge_cool = [&](int m100_d) {
                for (int s = 0; s < 200; ++s) {
                    Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
                    r.env.target = {2.0f, 1.075f, 0}; r.env.hp = 50; r.env.max_hp = 50;
                    r.env.section = 2; r.env.difficulty = m100_d;   // section 2: always busy
                    for (int t = 0; t < 400; ++t)
                        if (enemy_ai_tick(ai, np, r.env, m910).section == 7) return ai.cool() + 1;
                }
                return -1;
            };
            const int c0 = lunge_cool(0), c2 = lunge_cool(2);
            std::printf("[ai]   M910 lunge cooldown: normal %d, nightmare %d\n", c0, c2);
            check(c0 >= 120 && c0 <= 179, "lunge (section 7): Normal cooldown 120..179");
            check(c2 >= 100 && c2 <= 159, "Nightmare (M100 d=2 -> d=-2): 100..159");
        }
    }

    // ---- M180 Wilewarer (build/ai_spec/M180.md) ----
    {
        const EnemyBrain m180 = enemy_brain_for(11);
        check(m180 != nullptr, "Wilewarer brain registered (AI index 11)");
        const EnemyAiParams wp = params(0.11f, 0.042f, 1.0f, 2.0f, 0.0f);
        // M_180.ASO priorities: 0..3 = 2, 4/5 = 4, 6 = 2, 7 = 3, 300 = 2, 315 = 4.
        auto prio = [](int id) {
            switch (id) { case 0: case 1: case 2: case 3: case 6: case 300: return 2;
                          case 7: return 3; case 4: case 5: case 315: return 4; default: return -1; }
        };
        struct W { Rig r; EnemyAi ai; AiOut o; };
        // Engaged 3u in front (+X), facing the player; the player faces back (-X).
        auto setup = [&](W& w, int psec, float pheading) {
            w.r.env.section_type = prio;
            w.r.env.section = 2;
            w.r.env.target = {3.0f, 1.075f, 0};
            w.r.env.target_section = psec;
            w.r.env.target_heading = pheading;
            for (int i = 0; i < 2; ++i) enemy_ai_tick(w.ai, wp, w.r.env, m180);   // init, acquire
        };
        auto tick = [&](W& w) {
            w.o = enemy_ai_tick(w.ai, wp, w.r.env, m180);
            if (w.o.section >= 0) w.r.env.section = w.o.section;      // the request lands
            w.r.env.was_hit = w.r.env.took_damage = false;
        };
        const float pi = 3.14159265f;
        {
            W w; setup(w, 0x100, pi);
            tick(w);
            check(w.o.section == 6 && w.ai.action() == 2 && w.ai.dur() == 59,
                  "player swinging (0x100) in 5u, face to face: guard (section 6, 60 ticks)");
        }
        {
            W w; setup(w, 0x100, 0.0f);       // the player faces away from it
            bool g = false;
            for (int t = 0; t < 60; ++t) { tick(w); g |= w.o.section == 6; }
            check(!g, "player facing away: never guards (needs it within +/-45 deg)");
        }
        {   // An excluded player section (0x109): only the 3% random guard remains.
            int guards = 0;
            for (int s = 0; s < 200; ++s) {
                W w; for (int i = 0; i < s; ++i) w.r.rng();
                setup(w, 0x109, pi);
                tick(w);
                guards += w.o.section == 6;
            }
            std::printf("[ai]   M180 guards vs section 0x109: %d / 200\n", guards);
            check(guards < 20, "excluded player section 0x109: no reactive guard (random ~3% only)");
        }
        {   // Blocked hits: 3 on Normal (bb5c 0) break the guard -> 300, lockout 60.
            W w; setup(w, 0x100, pi);
            tick(w);
            int broke = -1;
            for (int t = 0; t < 3 && broke < 0; ++t) {
                w.r.env.was_hit = true;                                   // landed, no damage
                tick(w);
                if (w.o.section == 300) broke = t;
            }
            check(broke == 2 && w.ai.i(0x72c) == 59 && w.ai.i(0x878) == 0,
                  "3 blocked hits (Normal) break the guard: section 300, 60-tick lockout");
        }
        {   // Hard (bb5c 1): 4 blocks; lockout 40.
            W w; w.r.env.diff_index = 1; setup(w, 0x100, pi);
            tick(w);
            int n = 0, broke = -1;
            for (int t = 0; t < 5 && broke < 0; ++t) { w.r.env.was_hit = true; tick(w); ++n; if (w.o.section == 300) broke = n; }
            check(broke == 4 && w.ai.i(0x72c) == 39, "Hard: 4 blocks, 40-tick lockout");
        }
        {   // The guard times out 60 ticks after the player stops qualifying.
            W w; setup(w, 0x100, pi);
            tick(w);
            w.r.env.target_section = 2;       // stops swinging
            w.r.env.target_heading = 0.0f;    // and turns away (no random guard either)
            int broke = -1;
            for (int t = 0; t < 80 && broke < 0; ++t) { tick(w); if (w.o.section == 300) broke = t; }
            std::printf("[ai]   M180 guard timeout after %d ticks\n", broke);
            check(broke >= 58 && broke <= 61, "guard breaks on its 60-tick timer");
        }
        {   // The strike: 22% a tick in 4u, +/-45 deg; hold 132 (142 Easy), cool + 20..39.
            W w; setup(w, 2, 0.0f);
            int at = -1;
            for (int t = 0; t < 200 && at < 0; ++t) { tick(w); if (w.o.section == 7) at = t; }
            check(at >= 0 && w.o.attack && w.ai.dur() == 131 && w.ai.cool() >= 151 && w.ai.cool() <= 170,
                  "strike: section 7, hold 132, cooldown 152..171");
            W e; e.r.env.diff_easy = true; setup(e, 2, 0.0f);
            int ate = -1;
            for (int t = 0; t < 200 && ate < 0; ++t) { tick(e); if (e.o.section == 7) ate = t; }
            check(ate >= 0 && e.ai.dur() == 141, "Easy: hold 142");
            W far; setup(far, 2, 0.0f); far.r.env.target = {3.0f, 3.5f, 0};   // 2.4 above, grounded
            bool hit = false;
            for (int t = 0; t < 200; ++t) { tick(far); hit |= far.o.section == 7; }
            check(!hit, "player 2+ above and grounded: no strike");
        }
    }

    // ---- M390 Baocar (build/ai_spec/M390.md) ----
    {
        const EnemyBrain m390 = enemy_brain_for(32);
        check(m390 != nullptr, "Baocar brain registered (AI index 32)");
        EnemyAiParams bp = params(0.095f, 0.0335f, 2.0f, 4.0f, 1.2f);
        bp.raw[9] = 0.005f; bp.raw[10] = 0.02f;                 // EI+0xb4/+0xb8 climb/descend
        Rig r; EnemyAi ai;
        r.env.target = {6.0f, 1.075f, 0};                       // within 8u: acquired at once
        r.env.target_heading = 3.14159265f;                     // the player faces -X (toward it)
        enemy_ai_tick(ai, bp, r.env, m390);
        check(ai.alpha == 0.0f && !ai.hittable, "spawns invisible and unhittable");
        int dash_tick = -1, appear = -1, visible = -1, cast = -1, fx = 0, snd = 0;
        float dash_speed = 0, wp_ahead = 0;
        for (int t = 0; t < 600 && cast < 0; ++t) {
            const AiOut o = enemy_ai_tick(ai, bp, r.env, m390);
            fx += int(o.effects.size()); snd += int(o.sounds.size());
            if (dash_tick < 0 && ai.i(0x72c) == 13) {
                dash_tick = t; dash_speed = ai.f(0x818);
                wp_ahead = r.env.target.x - ai.v(0x894).x;      // in front of a -X-facing player
            }
            if (appear < 0 && ai.i(0x730) == 201) appear = t;
            if (visible < 0 && ai.alpha == 1.0f) visible = t;
            if (o.section == 7) cast = t;
        }
        std::printf("[ai]   M390 dash@%d appear@%d visible@%d cast@%d fx %d snd %d\n",
                    dash_tick, appear, visible, cast, fx, snd);
        check(dash_tick >= 0 && std::fabs(dash_speed - 0.0335f * 4.0f) < 1e-6f,
              "invisible within 15u: dash at base x4");
        check(wp_ahead >= 4.0f && wp_ahead <= 6.0f + 1.0f, "dash target 4..7 u IN FRONT of the player");
        check(appear > dash_tick + 25 && visible - appear >= 58 && visible - appear <= 61,
              "after the 20-tick dash + 10-tick stop: a 60-tick fade-in");
        check(fx == 2 && snd == 1, "fade-in start: effects 270 + 269 and SE 464, once");
        check(cast > visible + 59, "holds (4-d)*15 = 60 after appearing, then may cast");
        check(cast < 0 || (ai.dur() == 89 && ai.cool() >= 119 && ai.cool() <= 138),
              "cast: section 7, action 90 (Normal), cooldown +30..49");
    }

    // ---- M490 Sopus-Baocar (build/ai_spec/M490.md) ----
    {
        const EnemyBrain m490 = enemy_brain_for(42);
        check(m490 != nullptr, "Sopus-Baocar brain registered (AI index 42)");
        EnemyAiParams sp = params(0.095f, 0.0335f, 2.0f, 4.0f, 1.2f);
        sp.raw[9] = 0.005f; sp.raw[10] = 0.02f;
        // Close (3u): appears, then the bomb dance (section 8) with rings in its marker-1 window.
        Rig r; EnemyAi ai;
        r.env.target = {3.0f, 1.075f, 0};
        r.env.target_heading = 3.14159265f;
        int sec8 = -1, rings = 0, bombs = 0, ring_ticks[8] = {}, snd = 0;
        float hover_up = 0;
        for (int t = 0; t < 700; ++t) {
            const AiOut o = enemy_ai_tick(ai, sp, r.env, m490);
            if (o.section == 8 && sec8 < 0) { sec8 = t; r.env.section = 8; hover_up = ai.f(0x834); }
            if (sec8 >= 0 && t >= sec8 + 84) r.env.aso_marker = 1;        // the section's window
            if (sec8 >= 0 && t >= sec8 + 180) r.env.aso_marker = 2;
            int b = 0;
            for (const AiEffect& f : o.effects) b += f.id == 0x10b;
            if (b) { if (rings < 8) ring_ticks[rings] = t; ++rings; bombs += b; }
            for (int s : o.sounds) snd += s == 0x1b5;
        }
        std::printf("[ai]   M490 section 8 @%d, rings %d (bombs %d) at %d, %d, %d\n",
                    sec8, rings, bombs, ring_ticks[0], ring_ticks[1], ring_ticks[2]);
        check(sec8 >= 0 && std::fabs(hover_up - 2.7f) < 1e-5f, "under 4u: bomb dance (section 8), hover 1.2 + 1.5");
        check(rings == 3 && bombs == 9 && snd == 9, "Normal: 3 rings x 3 bombs, SE 437 per bomb");
        check(ring_ticks[1] - ring_ticks[0] == 30 && ring_ticks[2] - ring_ticks[1] == 30,
              "rings every 30 ticks inside the marker-1 window");
        check(std::fabs(ai.f(0x834) - 1.2f) < 1e-5f, "marker 2 restores the hover height");

        Rig r2; EnemyAi far;                                     // 6u: disc shots (section 7)
        r2.env.target = {6.0f, 1.075f, 0};
        r2.env.target_heading = 3.14159265f;
        int sec7 = -1;
        for (int t = 0; t < 600 && sec7 < 0; ++t)
            if (enemy_ai_tick(far, sp, r2.env, m490).section == 7) sec7 = t;
        check(sec7 >= 0 && far.dur() == 109, "4..d+8 u: disc shot (section 7), lock 110 on Normal");
    }

    // ---- M970 Vagully, brain A (build/ai_spec/M970.md §2A) ----
    {
        const EnemyBrain m970 = enemy_brain_for(90);
        check(m970 != nullptr, "Vagully brain registered (AI index 90)");
        const EnemyAiParams vp = params(0.05f, 0.04f, 1.0f, 2.0f, 1.0f);
        {   // Close (1.5u): always runs (0x800) and steers AWAY.
            Rig r; EnemyAi ai; r.env.target = {1.5f, 1.075f, 0};
            for (int t = 0; t < 4; ++t) enemy_ai_tick(ai, vp, r.env, m970);
            const Vec3 sv = ai.v(0x748);
            check(ai.mode() == 2 && std::fabs(ai.speed - 0.08f) < 1e-6f, "engaged: always runs at 0.04 x 2.0");
            check(sv.x < 0, "under 2u: steers away from the target");
        }
        {   // 5u, level, in front: a strike every so often; never when not level.
            Rig r; EnemyAi ai; r.env.target = {5.0f, 1.075f, 0};
            int strikes = 0;
            for (int t = 0; t < 300; ++t) strikes += enemy_ai_tick(ai, vp, r.env, m970).section == 7;
            Rig r2; EnemyAi hi; r2.env.target = {5.0f, 2.2f, 0};          // 1.1 above: not level
            int strikes2 = 0;
            for (int t = 0; t < 300; ++t) strikes2 += enemy_ai_tick(hi, vp, r2.env, m970).section == 7;
            std::printf("[ai]   M970 strike requests in 300 ticks: level %d, 1.1 above %d\n", strikes, strikes2);
            check(strikes > 0 && strikes <= 20, "level, within 6u, +/-54 deg: 30% every 15th tick");
            check(strikes2 == 0, "|dy| >= 1: never strikes");
        }
        {   // The high-flight coin flip every (e+16)*15 = 240 ticks on Normal.
            Rig r; EnemyAi ai; r.env.target = {-14.0f, 1.075f, 0};      // unaware; tail still runs
            bool high = false, reset = false;
            for (int t = 0; t < 2000; ++t) {
                enemy_ai_tick(ai, vp, r.env, m970);
                high |= ai.f(0x834) == 3.0f;
                if (t == 241) reset = ai.i(0x7a0) < 5;
            }
            check(reset && high, "every 240 ticks: 50% to fly high (hover 3.0)");
        }
    }

    // ---- M930 Nooze (Red): the same routine; splits on its own, rarely when hit (M930.md) ----
    {
        const EnemyBrain m930 = enemy_brain_for(86);
        check(m930 != nullptr, "Nooze (Red) brain registered (AI index 86)");
        EnemyAiParams np = params(0.04f, 0.05f, 1.0f, 3.25f, 0.0f);
        np.raw[6] = 0.125f; np.raw[7] = 0.15f;
        int hit_splits = 0, free_splits = 0;
        for (int s = 0; s < 1000; ++s) {
            for (int sec : {4, 0}) {
                Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
                r.env.target = {6.0f, 1.075f, 0}; r.env.hp = 100; r.env.max_hp = 100;
                enemy_ai_tick(ai, np, r.env, m930);
                r.env.hp = 80; r.env.section = sec; r.env.aso_marker = 9;
                const AiOut o = enemy_ai_tick(ai, np, r.env, m930);
                if (!o.spawns.empty()) {
                    (sec == 4 ? hit_splits : free_splits) += 1;
                    check(o.spawns[0].cast == 0xb3 && std::fabs(o.set_hp - 44.0f) < 1e-4f, "splits into 0xb3; chunk max(31, 80 - 44) = 36 -> 44 each");
                }
            }
        }
        std::printf("[ai]   M930 splits over 1000 seeds: hit %d, otherwise %d\n", hit_splits, free_splits);
        check(hit_splits > 25 && hit_splits < 80 && free_splits > 190 && free_splits < 270,
              "split odds: 5% in the hit reaction, 23% otherwise (the reverse of M910/M920)");
        int c3 = -1;
        for (int s = 0; s < 200 && c3 < 0; ++s) {
            Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
            r.env.target = {4.8f, 1.075f, 0}; r.env.section = 2; r.env.difficulty = 2;
            r.env.hp = 50; r.env.max_hp = 50;
            for (int t = 0; t < 400 && c3 < 0; ++t)
                if (enemy_ai_tick(ai, np, r.env, m930).section == 7) c3 = ai.cool() + 1;
        }
        check(c3 >= 75 && c3 <= 119, "lunge at 4.8u (< 5.0); Nightmare (d = -2): cooldown (d + 3)*15 + hold 60 + 0..44");
    }

    // ---- M940 Nooze (Yellow): splits ~only when hit (35%) (build/ai_spec/M940.md) ----
    {
        const EnemyBrain m940 = enemy_brain_for(87);
        check(m940 != nullptr, "Nooze (Yellow) brain registered (AI index 87)");
        EnemyAiParams np = params(0.03f, 0.02f, 1.0f, 3.0f, 0.0f);
        np.raw[6] = 0.1f; np.raw[7] = 0.15f;
        int hit_splits = 0, free_splits = 0;
        for (int s = 0; s < 1000; ++s)
            for (int sec : {4, 0}) {
                Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
                r.env.target = {6.0f, 1.075f, 0}; r.env.hp = 100; r.env.max_hp = 100;
                enemy_ai_tick(ai, np, r.env, m940);
                r.env.hp = 80; r.env.section = sec; r.env.aso_marker = 9;
                const AiOut o = enemy_ai_tick(ai, np, r.env, m940);
                if (!o.spawns.empty()) (sec == 4 ? hit_splits : free_splits) += 1;
            }
        std::printf("[ai]   M940 splits over 1000 seeds: hit %d, otherwise %d\n", hit_splits, free_splits);
        check(hit_splits > 310 && hit_splits < 390 && free_splits < 6, "split odds: 35% in the hit reaction, 0.1% otherwise");
    }

    // ---- M920 Nooze (Blue): M910 with other constants (build/ai_spec/M920.md) ----
    {
        const EnemyBrain m920 = enemy_brain_for(85);
        check(m920 != nullptr, "Nooze (Blue) brain registered (AI index 85)");
        EnemyAiParams np = params(0.03f, 0.02f, 1.0f, 3.0f, 0.0f);
        np.raw[6] = 0.1f; np.raw[7] = 0.15f;
        bool split_at_24 = false, chunk_ok = false;
        for (int s = 0; s < 400 && !split_at_24; ++s) {
            Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
            r.env.target = {6.0f, 1.075f, 0}; r.env.hp = 100; r.env.max_hp = 100;
            enemy_ai_tick(ai, np, r.env, m920);
            r.env.hp = 80; r.env.section = 4; r.env.aso_marker = 9; r.env.pool_count = 24;
            const AiOut o = enemy_ai_tick(ai, np, r.env, m920);
            if (!o.spawns.empty()) {
                split_at_24 = true;
                // chunk = max(0.31*100, 80 - 0.525*80 = 38) = 38 -> both 42; the child is 0xb2
                chunk_ok = o.set_hp == 42.0f && o.spawns[0].hp == 42.0f && o.spawns[0].cast == 0xb2;
            }
        }
        check(split_at_24, "pool cap 26: still splits with 24 actors (M910 would not)");
        check(chunk_ok, "HP chunk max(0.31 max, h - 0.525h): 80 -> 42 each; splits into 0xb2");
        int c2 = -1;
        for (int s = 0; s < 200 && c2 < 0; ++s) {
            Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
            r.env.target = {4.2f, 1.075f, 0}; r.env.section = 2; r.env.difficulty = 2;
            r.env.hp = 50; r.env.max_hp = 50;
            for (int t = 0; t < 400 && c2 < 0; ++t)
                if (enemy_ai_tick(ai, np, r.env, m920).section == 7) c2 = ai.cool() + 1;
        }
        check(c2 >= 90 && c2 <= 149, "lunge at 4.2u (< 4.5); Nightmare cooldown 120 - 15*2 + 0..59");
    }

    // ---- M110 Genoid (build/ai_spec/M110.md) ----
    {
        const EnemyBrain m110 = enemy_brain_for(4);
        check(m110 != nullptr, "Genoid brain registered (AI index 4)");
        const EnemyAiParams gp = params(0.05f, 0.03f, 1.0f, 6.0f, 0.0f);
        auto mode_at = [&](float x) {
            Rig r; EnemyAi ai; r.env.target = {x, 1.075f, 0}; r.env.section = 2;
            for (int t = 0; t < 4; ++t) enemy_ai_tick(ai, gp, r.env, m110);
            return std::pair{ai.mode(), ai.speed};
        };
        const auto far = mode_at(10.0f), near = mode_at(5.0f);
        check(far.first == 2 && std::fabs(far.second - 0.18f) < 1e-5f, "beyond 7u: runs (0x800) at 0.03 x 6.0");
        check(near.first == 1 && std::fabs(near.second - 0.03f) < 1e-5f, "inside 7u: walk-approach (0x10)");
        {   // The attack roll and, once attacking, the cooldown re-roll: 90..179 (Normal, untagged).
            Rig r; EnemyAi ai; r.env.target = {2.0f, 1.075f, 0}; r.env.section = 2;
            int at = -1;
            for (int t = 0; t < 400 && at < 0; ++t) if (enemy_ai_tick(ai, gp, r.env, m110).section == 7) at = t;
            r.env.section = 7; r.env.aso_marker = 1;
            int lo = 1 << 30, hi = -1;
            for (int t = 0; t < 60; ++t) { enemy_ai_tick(ai, gp, r.env, m110); lo = std::min(lo, ai.cool()); hi = std::max(hi, ai.cool()); }
            std::printf("[ai]   M110 attack @%d, cooldown while attacking %d..%d\n", at, lo, hi);
            check(at >= 0, "within 4.5u, facing: requests section 7");
            check(lo >= 89 && hi <= 179, "attacking: cooldown re-rolled into 90..179 (- the tick)");
        }
        {   // Enrage under half HP: ungated 313, speed x1.3, then growth to x1.30 and the tint.
            Actor act; act.hp = 40; act.max_hp = 100; act.def = 10; act.atk = 20; act.radius = 0.99f; act.height = 1.776f;
            Rig r; EnemyAi& ai = act.ai; r.env.actor = &act;
            r.env.target = {6.0f, 1.075f, 0}; r.env.section = 2; r.env.hp = 40; r.env.max_hp = 100;
            AiOut o = enemy_ai_tick(ai, gp, r.env, m110);                   // init + tail: enrage
            check(o.section == 0x139 && o.section_force && ai.i(0x79c) == 2 &&
                  std::fabs(ai.f(0x818) - 0.039f) < 1e-6f && std::fabs(act.atk - 21.5f) < 1e-4f,
                  "HP < 50%: ungated section 313, base speed x1.3, STR x1.075");
            r.env.section = 0x139; r.env.aso_marker = 30;
            o = enemy_ai_tick(ai, gp, r.env, m110);
            check(std::fabs(act.scale - 1.3f) < 1e-5f && act.tint == 0xffa5a5e9u && std::fabs(act.def - 25.0f) < 1e-4f,
                  "section 313 at marker 30: scale x1.30, tint 0xFFA5A5E9, DEF +15");
            check(o.durable_rule == 1, "enraged: Durable forced on (the handler)");
            r.env.section = 2; r.env.aso_marker = 0;
            enemy_ai_tick(ai, gp, r.env, m110);
            check(std::fabs(act.def - 10.0f) < 1e-4f && std::fabs(act.scale - 1.3f) < 1e-5f,
                  "after 313: the DEF bonus ends, the size stays");
        }
    }

    // ---- M200 Venueba (build/ai_spec/M200.md) ----
    {
        const EnemyBrain m200 = enemy_brain_for(13);
        check(m200 != nullptr, "Venueba brain registered (AI index 13)");
        const EnemyAiParams vp = params(0.05f, 0.03f, 1.0f, 2.0f, 0.0f);
        int lobs = 0, splashes = 0, glob_ok = 0, splash_ok = 0;
        for (int s = 0; s < 60; ++s) {
            Rig r; EnemyAi ai; for (int i = 0; i < s; ++i) r.rng();
            // Solid floor under it: only straight-down segments are blocked (LOS stays clear).
            r.env.blocked = [](const Vec3& a0, const Vec3& b0) { return a0.x == b0.x && a0.z == b0.z && b0.y < a0.y; };
            r.env.target = {3.0f, 1.075f, 0}; r.env.section = 2;
            for (int t = 0; t < 600; ++t) {
                const AiOut o = enemy_ai_tick(ai, vp, r.env, m200);
                if (o.section == 7) { ++lobs; r.env.section = 7; r.env.aso_marker = 0; glob_ok += ai.dur() == 75; }
                if (o.section == 8) { ++splashes; r.env.section = 8; r.env.aso_marker = 0; splash_ok += ai.dur() == 119; }
                if (r.env.section == 7 && ai.i(0x72c) == 102) {
                    r.env.aso_marker = 2;
                    if (!o.bullets.empty()) glob_ok += o.bullets.size() == 1 && o.bullets[0].effect == 0x13b ? 100 : 0;
                }
                if (r.env.section == 8 && ai.i(0x72c) == 112) {
                    r.env.aso_marker = 3;
                    for (const AiEffect& f : o.effects) splash_ok += f.id == 0xff ? 100 : 0;
                }
                if (lobs + splashes > 0 && ai.i(0x72c) < 100 && t > 300) break;
            }
            r.env.section = 2;
        }
        std::printf("[ai]   M200 first attacks over 60 seeds: lob %d, splash %d\n", lobs, splashes);
        check(lobs > 0 && splashes > 0, "both attacks occur: the lob (section 7) and the splash (section 8)");
        check(glob_ok >= 101 && splash_ok >= 101, "lob: hold 76 (Normal), one glob at marker 2; splash: hold 120, poison hit at marker 3");
    }

    // ---- M170 Vilvan, the orbiter (build/ai_spec/M170.md) ----
    {
        const EnemyBrain m170 = enemy_brain_for(10);
        check(m170 != nullptr, "Vilvan brain registered (AI index 10)");
        const EnemyAiParams vp = params(0.22f, 0.045f, 0.75f, 1.5f, 0.0f);
        Actor v; v.height = 1.0f; v.ai.speed310 = 0.03375f;
        Rig r; r.env.actor = &v; r.env.target = {6.0f, 1.075f, 0};
        float speed = 0; bool neg1 = true; int strikes = 0;
        for (int t = 0; t < 400; ++t) {
            r.env.self = {v.pos.x, v.pos.y + v.height, v.pos.z};
            const AiOut o = enemy_ai_tick(v.ai, vp, r.env, m170);
            if (t > 2 && t < 60) neg1 &= v.ai.action() == -1;
            strikes += o.section == 7;
            mover_post(v.mover, 1, v.ai.speed, v.ai.heading, true, false, true, 1.0f, 30.0f);
            v.pos.x += v.mover.dx; v.pos.z += v.mover.dz;
            if (t > 100 && v.ai.action() == -1 && v.ai.i(0x79c) > 20)   // settled orbit ticks only
                speed = std::sqrt(v.mover.dx * v.mover.dx + v.mover.dz * v.mover.dz);
        }
        const float ring = std::sqrt((v.pos.x - 6.0f) * (v.pos.x - 6.0f) + v.pos.z * v.pos.z);
        std::printf("[ai]   M170 orbit: drift %.4f/tick, %.2f from the target, %d strikes\n", speed, ring, strikes);
        check(neg1, "engaged: action -1 (the brain drives; the action player stands aside)");
        check(speed > 0.015f && speed <= 0.0248f, "orbit drift below the 1/60 steady state (the impulse turns each tick)");
        {   // A fixed-direction 1/60 impulse into channel 2 settles at (1/60)/0.675 = 0.0247.
            MoverState m;
            for (int t = 0; t < 60; ++t) { m.k2.x += 1.0f / 60.0f; mover_post(m, 1, 0, 0, true, false, true, 1.0f, 30.0f); }
            check(std::fabs(m.dx - 0.02469f) < 2e-4f, "channel 2 steady state: (1/60)/0.675 = 0.0247/tick");
        }
        check(ring > 2.5f && ring < 5.5f, "it circles at about 4 u from the target");
        check(strikes > 0, "inside 4.5u and facing: it strikes (section 7)");
    }

    // ---- M250 Nipper (build/ai_spec/M250.md) ----
    {
        const EnemyBrain m250 = enemy_brain_for(18);
        check(m250 != nullptr, "Nipper brain registered (AI index 18)");
        const EnemyAiParams np = params(0.075f, 0.025f, 1.1f, 1.8f, 0.0f);
        {   // Far (10u): walks; now and then a forward hop (vy 0.25 + a 0.25 kick).
            Actor n; n.height = 0.8f;
            Rig r; r.env.actor = &n; r.env.target = {10.0f, 1.075f, 0};
            int hops = 0; bool walked = false;
            for (int t = 0; t < 600; ++t) {
                r.env.grounded = n.vert.grounded;
                enemy_ai_tick(n.ai, np, r.env, m250);
                walked |= n.ai.mode() == 1;
                if (!n.vert.grounded) { ++hops; check(std::fabs(n.vert.t - 0.25f) < 1e-6f && n.mover.k1.x > 0.2f, "hop: vy 0.25 + forward kick 0.25"); n.vert.grounded = true; n.mover.k1 = {}; }
            }
            std::printf("[ai]   M250 hops in 600 far ticks: %d\n", hops);
            check(walked && hops > 0 && hops < 60, "far: walks at the target and hops ~1.5%/tick at 10u");
        }
        {   // Close (2u): runs away.
            Rig r; EnemyAi ai; r.env.target = {2.0f, 1.075f, 0};
            for (int t = 0; t < 4; ++t) enemy_ai_tick(ai, np, r.env, m250);
            check(ai.steer() == 3 || ai.mode() == 2, "within 3u: flees (run, steered away)");
        }
        {   // The jump-shot: at Marker(1) it jumps 0.22 when the target is 1..3 above.
            Actor n; n.height = 0.8f;
            Rig r; r.env.actor = &n; r.env.target = {5.0f, 1.075f + 2.0f, 0};
            r.env.target_grounded = false;
            int sec8 = -1;
            for (int t = 0; t < 2000 && sec8 < 0; ++t) {
                r.env.grounded = true;
                if (enemy_ai_tick(n.ai, np, r.env, m250).section == 8) sec8 = t;
            }
            r.env.section = 8; r.env.aso_marker = 1;
            const AiOut o = enemy_ai_tick(n.ai, np, r.env, m250);
            check(sec8 >= 0 && !n.vert.grounded && std::fabs(n.vert.t - 0.22f) < 1e-6f && o.set_marker == 2,
                  "jump-shot: section 8, at Marker(1) a 0.22 jump (target 2 above)");
        }
    }

    // ---- M510 Mobre (build/ai_spec/M510.md) ----
    {
        const EnemyBrain m510 = enemy_brain_for(44);
        check(m510 != nullptr, "Mobre brain registered (AI index 44)");
        const EnemyAiParams mp = params(0.06f, 0.04f, 1.0f, 2.0f, 0.0f);
        Rig r; EnemyAi ai; r.env.target = {3.0f, 1.075f, 0};
        int sec7 = -1;
        for (int t = 0; t < 600 && sec7 < 0; ++t) if (enemy_ai_tick(ai, mp, r.env, m510).section == 7) sec7 = t;
        check(sec7 >= 0 && ai.dur() == 109, "within 4.25u: turn, then 15%/tick for section 7, hold 110");
        r.env.section = 7; r.env.aso_marker = 1;
        int discs = 0; float first_deg = 0, last_deg = 0;
        for (int t = 0; t < 51; ++t) {
            const AiOut o = enemy_ai_tick(ai, mp, r.env, m510);
            for (const AiBullet& b : o.bullets) {
                const float dg = (std::atan2(b.dir.z, b.dir.x) - ai.heading) * 57.29578f;
                if (discs == 0) first_deg = dg;
                last_deg = dg; ++discs;
                check(b.effect == 0x13d && std::fabs(b.speed - 0.06f) < 1e-6f, "disc: effect 317 at 0.06 (Normal)");
            }
        }
        std::printf("[ai]   M510 discs %d, sweep %.1f .. %.1f deg\n", discs, first_deg, last_deg);
        check(discs == 8 && std::fabs(first_deg + 38.0f) < 0.1f && std::fabs(last_deg - 60.0f) < 0.1f,
              "51 marker-1 ticks: 8 discs sweeping -38 .. +60 deg");
    }

    // ---- M190 Irebolg (build/ai_spec/M190.md) ----
    {
        const EnemyBrain m190 = enemy_brain_for(12);
        check(m190 != nullptr, "Irebolg brain registered (AI index 12)");
        const EnemyAiParams ip = params(0.05f, 0.03f, 1.0f, 2.0f, 0.0f);
        auto first = [&](float x, int& dur, int& cool) {
            Rig r; EnemyAi ai; r.env.target = {x, 1.075f, 0};
            for (int t = 0; t < 400; ++t) {
                const AiOut o = enemy_ai_tick(ai, ip, r.env, m190);
                if (o.section == 7 || o.section == 8) { dur = ai.dur() + 1; cool = ai.cool() + 1; return o.section; }
            }
            return -1;
        };
        int d8 = 0, c8 = 0, d7 = 0, c7 = 0;
        const int s3 = first(3.0f, d8, c8), s6 = first(6.0f, d7, c7);
        check(s3 == 8 && d8 == 95 && c8 >= 95 && c8 < 145, "within 4u: the leap (section 8), stand 95, cooldown + R%50");
        check(s6 == 7 && d7 == 80 && c7 >= 80 && c7 < 130, "4..7u: the charge (section 7), stand 80");
        {   // Hard, inside 4.5: a 50% back-hop (section 301: 0.2 up, 0.5 back along the facing).
            int hops = 0, bad = 0;
            for (int s = 0; s < 40; ++s) {
                Actor A; A.height = 1.075f;
                Rig r; for (int i = 0; i < s; ++i) r.rng();
                r.env.actor = &A; r.env.target = {3.0f, 1.075f, 0}; r.env.difficulty = 1; r.env.diff_index = 1;
                for (int t = 0; t < 200; ++t) {
                    const AiOut o = enemy_ai_tick(A.ai, ip, r.env, m190);
                    if (o.section == 0x12d) {
                        ++hops;
                        bad += !(std::fabs(A.vert.t - 0.2f) < 1e-6f && std::fabs(A.mover.k1.x + 0.5f) < 1e-5f && !A.vert.grounded);
                        break;
                    }
                    if (o.section == 7 || o.section == 8) break;
                }
            }
            std::printf("[ai]   M190 Hard back-hops over 40 seeds: %d\n", hops);
            check(hops > 8 && hops < 32, "Hard, inside 4.5u: about half the attacks start with the back-hop");
            check(bad == 0, "back-hop: t 0.2, channel 1 -0.5 along the facing, airborne");
        }
    }

    // ---- M240 Bordisch (build/ai_spec/M240.md) ----
    {
        const EnemyBrain m240 = enemy_brain_for(17);
        check(m240 != nullptr, "Bordisch brain registered (AI index 17)");
        const EnemyAiParams bp = params(0.05f, 0.03f, 1.0f, 2.0f, 0.0f);
        auto run = [&](float hp, int& sec, int& cool, Actor& A) {
            Rig r; r.env.actor = &A; r.env.target = {2.0f, 1.075f, 0}; r.env.hp = hp; r.env.max_hp = 100;
            for (int t = 0; t < 300; ++t) {
                const AiOut o = enemy_ai_tick(A.ai, bp, r.env, m240);
                if (o.section == 7 || o.section == 8) { sec = o.section; cool = A.ai.cool() + 1; return; }
            }
        };
        Actor a1; a1.atk = 40; int s1 = -1, c1 = 0; run(100, s1, c1, a1);
        check(s1 == 7 && c1 >= 123 && c1 <= 162, "HP >= 60%: hop-smash (section 7), cooldown 63 + 60 + R%40 (Normal)");
        Actor a2; a2.atk = 40; int s2 = -1, c2 = 0; run(50, s2, c2, a2);
        check(s2 == 8 && std::fabs(a2.atk - 46.0f) < 1e-4f, "33..60%: leap slam (section 8), STR x1.15");
        Actor a3; a3.atk = 40; int s3 = -1, c3 = 0; run(20, s3, c3, a3);
        check(s3 == 7 && c3 == 63, "rage (< 33%): section 7 with a fixed 63-tick lock");
        {   // The push: 1/n into channel 2 along the facing at markers 2..4 of section 7.
            Actor A; A.atk = 40;
            Rig r; r.env.actor = &A; r.env.target = {2.0f, 1.075f, 0}; r.env.hp = 100; r.env.max_hp = 100;
            for (int t = 0; t < 300 && A.ai.i(0x72c) != 102; ++t) enemy_ai_tick(A.ai, bp, r.env, m240);
            r.env.section = 7; r.env.aso_marker = 2; r.env.facing = 0;
            enemy_ai_tick(A.ai, bp, r.env, m240);
            check(std::fabs(A.mover.k2.x - 0.5f) < 1e-5f, "smash push: channel 2 += 1/n (n = 2 on the tick after the request)");
        }
    }

    // ---- M760 Raldisch (build/ai_spec/M760.md) ----
    {
        const EnemyBrain m760 = enemy_brain_for(69);
        check(m760 != nullptr, "Raldisch brain registered (AI index 69)");
        const EnemyAiParams rp = params(0.0724f, 0.03f, 1.0f, 2.3f, 0.0f);
        auto secs = [&](float x, float hp, std::vector<int>& got, Actor& A) {
            Rig r; r.env.actor = &A; r.env.target = {x, 1.075f, 0}; r.env.hp = hp; r.env.max_hp = 100;
            for (int t = 0; t < 600 && got.size() < 2; ++t) {
                const AiOut o = enemy_ai_tick(A.ai, rp, r.env, m760);
                if (o.section == 7 || o.section == 8) got.push_back(o.section * 1000 + A.ai.cool() + 1);
            }
        };
        { Actor A; A.atk = 50; std::vector<int> g; secs(4.0f, 100, g, A);
          check(!g.empty() && g[0] / 1000 == 7 && g[0] % 1000 >= 123 && g[0] % 1000 <= 162, "within 5u: the jump press (section 7)"); }
        { Actor A; A.atk = 50; std::vector<int> g; secs(5.5f, 100, g, A);
          check(!g.empty() && g[0] / 1000 == 8, "5..6u: the charge (section 8)"); }
        { Actor A; A.atk = 50; std::vector<int> g; secs(5.5f, 40, g, A);
          check(g.size() == 2 && g[0] == 7063 && g[1] / 1000 == 8, "below half HP beyond 5u: press (cool 63) then charge");
          check(std::fabs(A.atk - 57.5f) < 1e-4f && std::fabs(A.ai.f(0x820) - 2.3f * 1.5f) < 1e-4f, "enrage: STR x1.15, run x1.5"); }
        {   // The charge push: ((40 + 0)/n)^2 at marker 3 on Normal; smoke every 3rd tick.
            Actor A; A.atk = 50;
            Rig r; r.env.actor = &A; r.env.target = {5.5f, 1.075f, 0}; r.env.hp = 100; r.env.max_hp = 100;
            for (int t = 0; t < 600 && A.ai.i(0x72c) != 112; ++t) enemy_ai_tick(A.ai, rp, r.env, m760);
            for (int t = 0; t < 63; ++t) enemy_ai_tick(A.ai, rp, r.env, m760);   // n = 64
            r.env.section = 8; r.env.aso_marker = 3; r.env.facing = 0;
            const int n = A.ai.i(0x87c);
            A.mover.k2 = {};
            enemy_ai_tick(A.ai, rp, r.env, m760);
            const float want = (40.0f / float(n)) * (40.0f / float(n));
            check(std::fabs(A.mover.k2.x - want) < 1e-5f, "charge push: channel 2 += (40/n)^2 along the facing");
        }
    }

    // ---- M300 Hubamobre (build/ai_spec/M300.md) ----
    {
        const EnemyBrain m300 = enemy_brain_for(23);
        check(m300 != nullptr, "Hubamobre brain registered (AI index 23)");
        const EnemyAiParams hp = params(0.06f, 0.034f, 1.0f, 2.0f, 0.0f);
        Rig r; EnemyAi ai; r.env.target = {4.0f, 1.075f, 0};
        int sec7 = -1;
        for (int t = 0; t < 600 && sec7 < 0; ++t) if (enemy_ai_tick(ai, hp, r.env, m300).section == 7) sec7 = t;
        check(sec7 >= 0 && ai.dur() == 109 && ai.cool() >= 129 && ai.cool() <= 148, "2.25..5.25u: fire fan (section 7), hold 110, cool + 20 + R%20");
        r.env.section = 7; r.env.aso_marker = 1;
        int shots = 0; float first = 0, last = 0;
        for (int t = 0; t < 51; ++t) {
            const AiOut o = enemy_ai_tick(ai, hp, r.env, m300);
            check(o.set_durable == 1 || t > 0, "Durable in section 7");
            for (const AiBullet& b : o.bullets) {
                const float dg = (std::atan2(b.dir.z, b.dir.x) - ai.heading) * 57.29578f;
                if (!shots) first = dg;
                last = dg; ++shots;
            }
        }
        std::printf("[ai]   M300 fan: %d bullets, %.1f .. %.1f deg\n", shots, first, last);
        check(shots == 13 && std::fabs(first + 40.0f) < 0.1f && std::fabs(last - 56.0f) < 0.1f, "51 marker ticks: 13 fire bullets, -40 .. +56 deg");
        Rig r2; EnemyAi a2; r2.env.target = {1.5f, 1.075f, 0};
        int sec8 = -1;
        for (int t = 0; t < 400 && sec8 < 0; ++t) if (enemy_ai_tick(a2, hp, r2.env, m300).section == 8) sec8 = t;
        check(sec8 >= 0 && a2.dur() == 49, "inside 2.25u facing: body slam (section 8), hold 50");
    }

    // ---- M770 Hubamobrun (build/ai_spec/M770.md) ----
    {
        const EnemyBrain m770 = enemy_brain_for(70);
        check(m770 != nullptr, "Hubamobrun brain registered (AI index 70)");
        const EnemyAiParams hp = params(0.06f, 0.034f, 1.0f, 2.0f, 0.0f);
        {
            Rig r; EnemyAi ai; r.env.target = {5.0f, 1.075f, 0}; r.env.hp = 100; r.env.max_hp = 100;
            int sec = -1;
            for (int t = 0; t < 400 && sec < 0; ++t) sec = enemy_ai_tick(ai, hp, r.env, m770).section;
            check(sec == 7 && ai.cool() + 1 >= 170 && ai.cool() + 1 <= 199, "3..7.5u: fire fan (section 7), cool 110 + 60 + R%30");
            r.env.section = 7; r.env.aso_marker = 1;
            int shots = 0; float last = 0;
            for (int t = 0; t < 51; ++t)
                for (const AiBullet& b : enemy_ai_tick(ai, hp, r.env, m770).bullets) {
                    last = (std::atan2(b.dir.z, b.dir.x) - ai.heading) * 57.29578f; ++shots;
                    check(std::fabs(b.speed - 0.15f) < 1e-6f && b.effect == 302, "bullet: effect 302 at 0.15");
                }
            check(shots == 13 && std::fabs(last - 94.0f) < 0.1f, "section 7: 13 bullets up to +94 deg");
        }
        {   // Half HP: section 301; its completion doubles the turn rate for good (source bug).
            Rig r; EnemyAi ai; r.env.target = {5.0f, 1.075f, 0}; r.env.hp = 40; r.env.max_hp = 100;
            int sec = -1;
            for (int t = 0; t < 400 && sec < 0; ++t) sec = enemy_ai_tick(ai, hp, r.env, m770).section;
            check(sec == 0x12d && ai.dur() + 1 == 155, "at or below half HP: the double fan (section 301), hold 155");
            r.env.section = 0x12d;
            for (int t = 0; t < 400 && ai.i(0x72c) == 122; ++t) enemy_ai_tick(ai, hp, r.env, m770);
            check(std::fabs(ai.f(0x7ac) - 0.12f) < 1e-6f && std::fabs(ai.f(0x814) - 0.12f) < 1e-6f,
                  "after a full 301: turn rate doubled for good (0.06 -> 0.12)");
        }
    }

    // ---- M380 Chelasken / M340 Rezalior, the flying breeders, and M370 Celny ----
    {
        struct Brood : AiWorld {       // open space; children are kept in a list
            std::list<Actor> actors;
            int next_id = 300;
            void for_each(const std::function<void(Actor&)>& f) override { for (Actor& a : actors) f(a); }
            Actor* find(int id) override { for (Actor& a : actors) if (a.instance_id == id) return &a; return nullptr; }
            Actor* spawn_child(Actor& p, int cast, const Vec3& d, int lv, bool keep0) override {
                actors.emplace_back();
                Actor& c = actors.back();
                c.instance_id = next_id++; c.cast_id = cast; c.level = lv; c.hp = c.max_hp = 20;
                c.pos = {p.pos.x + d.x, p.pos.y + d.y, p.pos.z + d.z};
                c.ai.heading = p.ai.heading; c.ai.i(0x854) = p.instance_id; c.ai.phase() = keep0 ? 0 : 4;
                return &c;
            }
            void remove(Actor&) override {}
            Ray ray(const Vec3&, const Vec3&) override { return {}; }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
        };
        const EnemyBrain m380 = enemy_brain_for(31), m340 = enemy_brain_for(27), m370 = enemy_brain_for(30);
        check(m380 && m340 && m370, "Chelasken / Rezalior / Celny brains registered (AI 31 / 27 / 30)");
        EnemyAiParams cp = params(0.0275f, 0.03f, 1.0f, 1.7f, 0.0f);
        cp.raw[8] = 2.5f; cp.raw[9] = 0.03f; cp.raw[10] = 0.03f;
        for (int which = 0; which < 2; ++which) {
            Brood w; Actor& A = (w.actors.emplace_back(), w.actors.back());
            A.instance_id = 1; A.hp = A.max_hp = 50; A.flyer = true; A.height = 1.0f; A.level = 7;
            Rig r; r.env.actor = &A; r.env.world = &w; r.env.target = {3.0f, 1.075f, 0}; r.env.self = {0, 3.5f, 0};
            r.env.target_grounded = true;
            const EnemyBrain br = which ? m340 : m380;
            int sec7 = -1;
            bool sting = false, stung_fresh = true;
            for (int t = 0; t < 2000 && sec7 < 0; ++t) {
                const AiOut o = enemy_ai_tick(A.ai, cp, r.env, br);
                for (const AiHitbox& h : o.hitboxes) { sting = true; stung_fresh &= h.new_serial && h.radius == 0.3f && h.fwd == -0.5f; }
                if (o.section == 7) sec7 = t;
            }
            check(sec7 >= 0 && A.ai.dur() == 49 && A.ai.cool() == 149, which ? "Rezalior: breed (section 7), hold 50, cooldown 150"
                                                                           : "Chelasken: breed (section 7), hold 50, cooldown 150");
            r.env.section = 7; r.env.aso_marker = 1;
            for (int t = 0; t < 3; ++t) enemy_ai_tick(A.ai, cp, r.env, br);
            int kids = 0; bool kid_ok = false;
            for (Actor& c : w.actors) if (&c != &A) {
                ++kids;
                kid_ok = c.cast_id == (which ? 0x3a : 0x29) && c.ai.phase() == 0 && c.ai.i(0x854) == 1 &&
                         std::fabs(c.ai.f(0x7f0) - A.ai.heading) < 1e-6f;
            }
            check(kids == 1 && kid_ok, which ? "at Marker 1: ONE Perrie (0x3a), phase 0, parent + heading set"
                                             : "at Marker 1: ONE Celny (0x29), phase 0, parent + heading set");
            // The drip: force a burst and run until a bullet drops.
            A.ai.i(0x7c0) = 150; A.ai.i(0x7bc) = 0;
            r.env.section = 0; r.env.aso_marker = 0;
            int drips = 0; bool drip_ok = true;
            for (int t = 0; t < 140; ++t) {
                const AiOut o = enemy_ai_tick(A.ai, cp, r.env, br);
                for (const AiHitbox& h : o.hitboxes) { sting = true; stung_fresh &= h.new_serial && h.radius == 0.3f && h.fwd == -0.5f; }
                for (const AiBullet& b : o.bullets) {
                    ++drips;
                    drip_ok &= b.effect == (which ? 500 : 264) && b.dir.y == -1.0f && b.speed == 0.09f && b.kind == 0x10 && b.fx_scale >= 1.1f && b.fx_scale < 2.3f;
                }
            }
            std::printf("[ai]   breeder drip: %d bullets, ok %d, sting seen %d\n", drips, int(drip_ok), int(sting));
            // +0x8a0 % P with P re-rolled at each drip: the spacing is irregular (can be < 60).
            check(drips >= 1 && drips <= 6 && drip_ok, "drip burst: bullets on +0x8a0 % (60..99), falling at 0.09, scale 1.1..2.3");
            check(sting && stung_fresh, "from the 61st undamaged tick: the 0.3 sting 0.5 behind, fresh serial");
        }
        {   // Celny born of a parent: phase 4, section 8, unhittable, grows 0.5 -> 1, engages ~tick 102.
            Brood w;
            Actor& P = (w.actors.emplace_back(), w.actors.back()); P.instance_id = 1;
            Actor* C = w.spawn_child(P, 0x29, {0, 0, 0}, 1, true);
            C->ai.f(0x7f0) = 0.0f;
            const EnemyAiParams lp = params(0.035f, 0.04f, 1.5f, 1.0f, 0.0f);
            Rig r; r.env.actor = C; r.env.world = &w; r.env.target = {6.0f, 1.075f, 0};
            enemy_ai_tick(C->ai, lp, r.env, m370);
            check(C->ai.phase() == 4 && C->scale == 0.5f && C->ai.alpha == 0.5f, "init: phase 4 (birth), alpha and scale 0.5");
            const AiOut o = enemy_ai_tick(C->ai, lp, r.env, m370);
            check(o.section == 8 && o.set_durable == 1 && !C->ai.hittable && C->mover.k2.x > 0.07f,
                  "birth: section 8, Durable, unhittable, slid along the parent's heading");
            r.env.section = 8;
            int engaged = -1;
            for (int t = 0; t < 200 && engaged < 0; ++t) {
                enemy_ai_tick(C->ai, lp, r.env, m370);
                if (C->ai.phase() == 2) engaged = t;
            }
            std::printf("[ai]   M370 born child engages after %d ticks\n", engaged + 2);
            check(engaged > 90 && engaged < 110 && C->scale == 1.0f && C->ai.hittable, "engages after the 100-tick hold: full size, hittable");
        }
        {   // A placed Celny engages at once; back turned within 4u it charges (0x400, 164 on Normal).
            Actor C; C.instance_id = 9;
            const EnemyAiParams lp = params(0.035f, 0.04f, 1.5f, 1.0f, 0.0f);
            Rig r; r.env.actor = &C; r.env.target = {3.0f, 1.075f, 0}; r.env.target_heading = 0.0f;   // looking away (+x)
            enemy_ai_tick(C.ai, lp, r.env, m370);
            enemy_ai_tick(C.ai, lp, r.env, m370);
            check(C.ai.phase() == 2 && C.scale == 1.0f, "placed: engaged at once at full size");
            for (int t = 0; t < 4; ++t) enemy_ai_tick(C.ai, lp, r.env, m370);
            check(C.ai.action() == 0x400 && C.ai.i(0x730) == 2, "back turned within 4u: the hop charge (run section)");
            Actor F; F.instance_id = 10;
            Rig r2; r2.env.actor = &F; r2.env.target = {3.0f, 1.075f, 0}; r2.env.target_heading = 3.14159f;   // looking at it (-x)
            for (int t = 0; t < 4; ++t) enemy_ai_tick(F.ai, lp, r2.env, m370);
            check(F.ai.action() == 0x10 && F.ai.steer() == 3, "faced within 5u: flees (walk, steered away)");
        }
    }

    // ---- M310 Jiamobre (build/ai_spec/M310.md) ----
    {
        struct Floor : AiWorld {       // an endless floor at y = 0, no walls
            void for_each(const std::function<void(Actor&)>&) override {}
            Actor* find(int) override { return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor&) override {}
            Ray ray(const Vec3& o, const Vec3& dl) override {
                Ray r;
                if (dl.y < 0 && o.y + dl.y <= 0 && o.y >= 0) { r.hit = true; r.point = {o.x, 0, o.z}; r.normal = {0, 1, 0}; }
                return r;
            }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
        } fl;
        const EnemyBrain m310 = enemy_brain_for(24);
        check(m310 != nullptr && enemy_handler_clamps(24) && !enemy_handler_clamps(5), "Jiamobre brain registered (AI 24); its handler keeps the arena clamp");
        EnemyAiParams jp = params(0.0888f, 0.03f, 1.0f, 2.0f, 0.0f);
        jp.raw[5] = 3.5f;                                              // +0x828 = H
        {   // Far: the hop search picks the farthest spot (r up to 6) toward the target, section 314.
            Actor J; J.height = 1.075f;
            Rig r; r.env.actor = &J; r.env.world = &fl; r.env.target = {12.0f, 1.075f, 0};
            int sec = -1;
            for (int t = 0; t < 10 && sec < 0; ++t) { const AiOut o = enemy_ai_tick(J.ai, jp, r.env, m310); if (o.section == 0x13a) sec = t; }
            check(sec >= 0 && J.ai.i(0x7c8) == 1 && std::fabs(J.ai.f(0x7b8) - 6.0f) < 1e-5f && J.ai.dur() == 31,
                  "far: hop to the farthest clear spot (6u), section 314, hold 32");
            const float ang = J.ai.f(0x7b4);
            check(std::fabs(ang) <= 0.53f, "the hop angle is within +/-30 deg of the target");
            for (int t = 0; t < 20; ++t) enemy_ai_tick(J.ai, jp, r.env, m310);   // past the 17-tick launch gate
            r.env.section = 0x13a; r.env.aso_marker = 1; r.env.grounded = true;
            const float y0 = J.pos.y;
            enemy_ai_tick(J.ai, jp, r.env, m310);
            check(std::fabs(J.pos.y - y0 - 0.7f) < 1e-5f && std::fabs(J.mover.k1.x - std::cos(ang) * 0.6f) < 1e-5f && !J.vert.grounded,
                  "launch (mode 1): y += 0.7, channel 1 += dist*0.1 along the hop angle");
        }
        {   // Close: back-hop, then section 7 (hold 67), then a section 8 at its Marker 4 (Normal: N = 2).
            Actor J; J.height = 1.075f;
            Rig r; r.env.actor = &J; r.env.world = &fl; r.env.target = {3.0f, 1.075f, 0};
            std::vector<int> secs;
            for (int t = 0; t < 200 && secs.size() < 2; ++t) {
                const AiOut o = enemy_ai_tick(J.ai, jp, r.env, m310);
                if (o.section >= 0) { secs.push_back(o.section); if (o.section == 0x13a) r.env.section = 0; }
            }
            check(secs.size() == 2 && secs[0] == 0x13a && secs[1] == 7 && J.ai.dur() == 66, "close: a back-hop (314), then the throw (section 7), hold 67");
            const float bang = J.ai.f(0x7b4);
            check(std::fabs(std::fabs(bang) - 3.14159f) <= 0.53f, "the back-hop heads away from the target");
            r.env.section = 7; r.env.aso_marker = 1;
            enemy_ai_tick(J.ai, jp, r.env, m310);
            check(std::fabs(J.ai.f(0x814) - 0.0888f * 3.0f) < 1e-6f && J.ai.action() == 2, "Marker 1: tracks at x3 (Normal)");
            r.env.aso_marker = 4;
            enemy_ai_tick(J.ai, jp, r.env, m310);
            const AiOut o8 = enemy_ai_tick(J.ai, jp, r.env, m310);
            check(o8.section == 8, "Normal: N = 2, so a second throw (section 8) at Marker 4");
        }
    }

    // ---- The factory's flag words and the jump helper (FACTORY.md, JUMPHELPER.md) ----
    {
        check(factory_flags(0x1c).f12c == 0x00490050u && factory_flags(0x0e).f12c == 0x00910010u &&
              factory_flags(0xb3).f12c == 0x2041c012u && factory_flags(0x17).f130 == 0x4000u,
              "factory flags: M240 / M100 / Nooze (Red) / M190 as FACTORY.md §5");
        {   // 0x40 sums channel 2 only; 0x40 | 0x2000000 moves not at all.
            MoverState m1; m1.k1 = {0.2f, 0, 0}; m1.k2 = {0, 0, 0.1f};
            MoverState m2 = m1, m3 = m1;
            mover_post(m1, 0, 0, 0, true, false, true, 1.0f, 30.0f, 0x00110010u);
            mover_post(m2, 0, 0, 0, true, false, true, 1.0f, 30.0f, 0x00490050u);
            mover_post(m3, 0, 0, 0, true, false, true, 1.0f, 30.0f, 0x02394052u);
            check(std::fabs(m1.dx - 0.2f) < 1e-6f && std::fabs(m1.dz - 0.1f) < 1e-6f &&
                  m2.dx == 0.0f && std::fabs(m2.dz - 0.1f) < 1e-6f && m3.dx == 0.0f && m3.dz == 0.0f,
                  "knock: both channels / channel 2 only (0x40) / none (0x40 | 0x2000000)");
        }
        // A world of axis-aligned boxes; the floor is boxes too (top at y = 0).
        struct Boxes : AiWorld {
            struct Box { Vec3 lo, hi; };
            std::vector<Box> boxes;
            void for_each(const std::function<void(Actor&)>&) override {}
            Actor* find(int) override { return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor&) override {}
            Ray ray(const Vec3& o, const Vec3& d) override {
                Ray r; float best = 2.0f;
                for (const Box& b : boxes) {
                    float t0 = 0, t1 = 1; bool ok = true;
                    const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
                    const float lo[3] = {b.lo.x, b.lo.y, b.lo.z}, hi[3] = {b.hi.x, b.hi.y, b.hi.z};
                    for (int k = 0; k < 3 && ok; ++k) {
                        if (dd[k] == 0) { if (oo[k] < lo[k] || oo[k] > hi[k]) ok = false; continue; }
                        float ta = (lo[k] - oo[k]) / dd[k], tb = (hi[k] - oo[k]) / dd[k];
                        if (ta > tb) std::swap(ta, tb);
                        t0 = std::max(t0, ta); t1 = std::min(t1, tb);
                        if (t0 > t1) ok = false;
                    }
                    // Faces only, two-sided like physics.cpp's segment_hit: a ray that starts
                    // inside a box hits the face it leaves through.
                    if (ok && t0 == 0.0f) t0 = t1 < 1.0f || t1 == 1.0f ? t1 : 2.0f;
                    if (ok && t0 <= 1.0f && t0 < best) { best = t0; r.hit = true; r.point = {o.x + d.x * t0, o.y + d.y * t0, o.z + d.z * t0}; }
                }
                return r;
            }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
        };
        EnemyAiParams hp = params(0.06f, 0.034f, 1.0f, 2.0f, 0.0f);
        hp.raw[5] = 3.2f;                                              // +0x828 = H
        const EnemyBrain m300 = enemy_brain_for(23);
        auto run_toward = [&](Boxes& w, Actor& A, int ticks, int* sec_out) {
            Rig r; r.env.actor = &A; r.env.world = &w; r.env.target = {12.0f, 1.075f, 0};
            r.env.radius = 0.675f; r.env.height = 1.075f;
            for (int t = 0; t < ticks; ++t) {
                r.env.self = {A.pos.x, A.pos.y + 1.075f, A.pos.z};
                r.env.grounded = A.vert.grounded;
                const AiOut o = enemy_ai_tick(A.ai, hp, r.env, m300);
                if (o.section == 0x13a) { *sec_out = t; return; }
            }
        };
        {   // A 2-wide gap at x 2..4: the gap jump at d = 3.0, +0x7e0 = 0.45, +0x7dc = 2.1.
            Boxes w; w.boxes = {{{-20, -1, -20}, {2, 0, 20}}, {{4, -1, -20}, {20, 0, 20}}};
            Actor A; A.flags12c = factory_flags(0x22).f12c; A.pos = {1.2f, 0, 0}; A.floor_y = 0;
            int sec = -1; run_toward(w, A, 5, &sec);
            check(sec >= 0 && A.ai.i(0x728) == 100 &&   // (action 1, then cleared: dur is 0)
                 
                  std::fabs(A.ai.f(0x7e0) - 0.45f) < 1e-5f && std::fabs(A.ai.f(0x7dc) - 2.1f) < 1e-5f,
                  "gap ahead: the jump helper's gap jump (d 3.0): 0x7e0 0.45, 0x7dc 2.1, section 314");
            // The brain's tail then launches at section 314's Marker 1 (M300: K 0.11).
            Rig r; r.env.actor = &A; r.env.world = &w; r.env.target = {12.0f, 1.075f, 0};
            r.env.self = {A.pos.x, 1.075f, 0}; r.env.section = 0x13a; r.env.aso_marker = 1;
            enemy_ai_tick(A.ai, hp, r.env, m300);
            check(std::fabs(A.vert.t - 2.1f * 0.11f) < 1e-5f && !A.vert.grounded && std::fabs(A.mover.k1.x - 0.45f) < 1e-5f,
                  "M300's tail at Marker 1: t = 0.231, airborne, channel 1 += 0.45");
        }
        {   // The same gap with the probe flag 0x100000 set (an M100-style cast): no jump.
            Boxes w; w.boxes = {{{-20, -1, -20}, {2, 0, 20}}, {{4, -1, -20}, {20, 0, 20}}};
            Actor A; A.flags12c = 0x00510010u; A.pos = {1.2f, 0, 0};
            int sec = -1; run_toward(w, A, 5, &sec);
            check(sec < 0, "0x100000 set: the probe is off and no jump arms (0x400000 alone is not enough)");
        }
        {   // A knee-high block (0.8) ahead: the step-up hop (t 0.1, y + 0.1).
            Boxes w; w.boxes = {{{-20, -1, -20}, {20, 0, 20}}, {{1.8f, 0, -2}, {3, 0.8f, 2}}};
            Actor A; A.flags12c = factory_flags(0x22).f12c; A.pos = {1.0f, 0, 0};
            int sec = -1;
            Rig r; r.env.actor = &A; r.env.world = &w; r.env.target = {12.0f, 1.075f, 0};
            r.env.self = {1.0f, 1.075f, 0}; r.env.radius = 0.675f; r.env.height = 1.075f;
            enemy_ai_tick(A.ai, hp, r.env, m300);
            enemy_ai_tick(A.ai, hp, r.env, m300);
            (void)sec;
            check(!A.vert.grounded && std::fabs(A.vert.t - 0.1f) < 1e-6f && std::fabs(A.pos.y - 0.1f) < 1e-6f,
                  "knee-high block: the step-up hop (t 0.1, y + 0.1)");
        }
        {   // A 2.5-high wall: the climb (FUN_00475fd0): h = 2.5 - (1.075 - 1.275) = 2.7 -> capped
            // at H - 1 = 2.2; +0x7dc = 2.8, +0x7e0 = 0.
            Boxes w; w.boxes = {{{-20, -1, -20}, {20, 0, 20}}, {{1.8f, 0, -2}, {3, 2.5f, 2}}};
            Actor A; A.flags12c = factory_flags(0x22).f12c; A.pos = {1.0f, 0, 0};
            int sec = -1; run_toward(w, A, 3, &sec);
            std::printf("[ai]   climb: sec %d sub %d 7dc %.3f 7e0 %.3f\n", sec, A.ai.i(0x728), A.ai.f(0x7dc), A.ai.f(0x7e0));
            check(sec >= 0 && A.ai.i(0x728) == 100 && std::fabs(A.ai.f(0x7dc) - 2.8f) < 1e-5f && A.ai.f(0x7e0) == 0.0f,
                  "a 2.5 wall: the climb (h capped at H - 1 = 2.2): 0x7dc 2.8, no push");
        }
    }

    // ---- M540 Perrie and M530 Sepast (build/ai_spec/M540.md, M530.md) ----
    {
        struct Pen : AiWorld {          // open space; remembers removals
            std::list<Actor> actors;
            int removed = 0;
            void for_each(const std::function<void(Actor&)>& f) override { for (Actor& a : actors) f(a); }
            Actor* find(int id) override { for (Actor& a : actors) if (a.instance_id == id) return &a; return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor& a) override { ++removed; a.hp = 0; }
            Ray ray(const Vec3& o, const Vec3& d) override {       // a floor at y = 0, no walls
                Ray r;
                if (d.y < 0 && o.y >= 0 && o.y + d.y <= 0) { r.hit = true; r.point = {o.x, 0, o.z}; }
                return r;
            }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
        };
        const EnemyBrain m540 = enemy_brain_for(47), m530 = enemy_brain_for(46);
        check(m540 && m530, "Perrie / Sepast brains registered (AI 47 / 46)");
        const EnemyAiParams pp = params(0.035f, 0.04f, 1.5f, 1.75f, 0.0f);
        {   // A placed Perrie: phase 2 at once; a damaging hit lights the fuse; 358 ticks later it explodes.
            Pen w; Actor& P = (w.actors.emplace_back(), w.actors.back()); P.instance_id = 5; P.hp = P.max_hp = 20;
            Rig r; r.env.actor = &P; r.env.world = &w; r.env.target = {8.0f, 1.075f, 0};
            enemy_ai_tick(P.ai, pp, r.env, m540);
            enemy_ai_tick(P.ai, pp, r.env, m540);
            check(P.ai.phase() == 2 && (P.flags12c & 0x80000u) != 0, "placed Perrie: engaged at once; init made it stun-immune");
            r.env.took_damage = true;
            enemy_ai_tick(P.ai, pp, r.env, m540);
            r.env.took_damage = false;
            check(P.ai.i(0x730) == 10 && P.ai.i(0x7c4) == 2, "a damaging hit lights the fuse (count 2), sub 10 (chase)");
            enemy_ai_tick(P.ai, pp, r.env, m540);
            check(P.ai.action() == 0x10 && P.ai.steer() == 0 && std::fabs(P.ai.f(0x81c) - 3.0f) < 1e-5f,
                  "lit: walks AT the target at walk x2");
            int glow = -1, boom = -1;
            for (int t = 0; t < 400 && boom < 0; ++t) {
                const AiOut o = enemy_ai_tick(P.ai, pp, r.env, m540);
                for (const AiEffect& f : o.effects) { if (f.id == 321) glow = t; if (f.id == 267) boom = t; }
            }
            std::printf("[ai]   M540 fuse: glow %d, explosion %d (ticks after the first chase tick)\n", glow, boom);
            check(boom - glow == 19 && w.removed == 1, "the glow 19 ticks before the explosion (EF2 267 + SE 437), then removed");
        }
        {   // The boss-fight brain (DAT_0076bb8c == 1): HP 350, DEF 50, a fuse start of 0..399.
            Actor P; P.instance_id = 6; P.hp = P.max_hp = 20;
            Rig r; r.env.actor = &P; r.env.boss_fight = true; r.env.target = {8.0f, 1.075f, 0};
            enemy_ai_tick(P.ai, pp, r.env, m540);
            check(P.hp == 350.0f && P.def == 50.0f && P.ai.phase() == 4 && P.ai.i(0x7a8) < 400, "boss Perrie: HP 350, DEF 50, birth");
        }
        {   // Sepast: one leg, the glow leg, then the attack: frame 0 -> stand 224 (Normal), volleys.
            const EnemyAiParams sp = params(0.03f, 0.0425f, 1.0f, 2.0f, 0.0f);
            Pen w; Actor& S = (w.actors.emplace_back(), w.actors.back()); S.instance_id = 7; S.cast_id = 57; S.hp = S.max_hp = 40;
            Rig r; r.env.actor = &S; r.env.world = &w; r.env.target = {6.0f, 1.075f, 0}; r.env.radius = 0.9f;
            int t10 = -1;
            for (int t = 0; t < 600 && t10 < 0; ++t) { enemy_ai_tick(S.ai, sp, r.env, m530); if (S.ai.i(0x72c) >= 10) t10 = t; }
            std::printf("[ai]   M530 attack after %d ticks\n", t10);
            // Acquired on the init tick (no phase-1 tick): the leg count starts at 0, so 6 legs
            // + the glow leg (60 ticks each), then the attack.
            check(t10 > 410 && t10 < 430, "engaged at once: 6 legs + the glow leg (60 ticks each), then the attack");
            r.env.aso_frame = 0; r.env.aso_marker = 1;
            const AiOut o = enemy_ai_tick(S.ai, sp, r.env, m530);
            int beams = 0, moves = 0;
            for (const AiEffect& f : o.effects) { beams += f.id == 235 && f.tag >= 0xfd; moves += f.move_tag >= 0xfd; }
            // (frame 0: sub 10 runs and falls through into 11 this tick; 224 - this tick's decrement)
            check(S.ai.i(0x72c) == 11 && S.ai.dur() == 223 && beams == 3 && moves == 3 && o.sounds.size() == 1 && o.sounds[0] == 477,
                  "attack: stand 224 (Normal); a volley of 3 owned beams (EF2 235, SE 477), all re-placed");
            int volleys = 1;
            for (int t = 0; t < 56; ++t) for (int s2 : enemy_ai_tick(S.ai, sp, r.env, m530).sounds) volleys += s2 == 477;
            check(volleys == 3, "a volley every 28 ticks while the marker is up");
            // The bounce: another Sepast 1.5 away pushes this one off (channel 1 += 0.25 along the push).
            Actor& O = (w.actors.emplace_back(), w.actors.back()); O.instance_id = 8; O.cast_id = 57; O.hp = 40; O.ai.phase() = 2;
            O.pos = {1.5f, 0, 0};
            S.mover.k1 = {};
            const AiOut ob = enemy_ai_tick(S.ai, sp, r.env, m530);
            bool spark = false; for (const AiEffect& f : ob.effects) spark |= f.id == 65;
            check(std::fabs(S.mover.k1.x + 0.25f) < 1e-5f && spark && std::fabs(O.ai.f(0x7cc) - 1.0f) < 1e-5f,
                  "bounce: pushed off its neighbour (k1 -0.25), spark EF2 65, the neighbour's accumulator pushed");
        }
    }

    // ---- M580 Sevnam: M500's teleporter with the bubble attack (build/ai_spec/M580.md) ----
    {
        const EnemyBrain m580 = enemy_brain_for(51);
        check(m580 != nullptr, "Sevnam brain registered (AI index 51)");
        const EnemyAiParams sp = params(0.1f, 0.0f, 0.0f, 0.0f, 0.0f);
        Rig r; EnemyAi ai; r.env.target = {4.0f, 1.075f, 0};
        enemy_ai_tick(ai, sp, r.env, m580);                            // init: fade out (302)
        r.env.section = 0x12e; r.env.aso_frame = 7;
        enemy_ai_tick(ai, sp, r.env, m580);                            // vanished: hidden, timer
        check(ai.i(0x728) == 3 && ai.cool() >= 90 - 1 && ai.cool() <= 209, "hidden: timer R%120 - 20d + 90 (Normal 90..209)");
        ai.cool() = 0;
        r.env.aso_frame = 0;
        int trig = -1;
        for (int t = 0; t < 300 && trig < 0; ++t) {
            const AiOut o = enemy_ai_tick(ai, sp, r.env, m580);
            if (ai.i(0x728) == 0 && r.env.section == 0x12e) r.env.section = 0x12d;   // the appear plays
            if (o.section == 7) trig = t;
        }
        check(trig >= 0 && ai.i(0x728) == 4, "appeared and near: the attack (section 7 requested at once)");
        r.env.section = 7; r.env.aso_marker = 4910491;
        const AiOut o = enemy_ai_tick(ai, sp, r.env, m580);
        int spits = 0, drops = 0;
        for (const AiBullet& b : o.bullets) {
            if (b.effect != 491) continue;
            if (b.dir.y > 0.8f && std::fabs(b.speed - 0.5f) < 1e-6f) ++spits;
            if (b.dir.y == -1.0f && std::fabs(b.speed - 0.15f) < 1e-6f && b.pos.y > 21.0f) ++drops;
        }
        check(spits == 1 && drops == 2 && o.set_marker == 0, "marker 4910491: one spit up (491) + two drops from 21+ (Normal)");
        r.env.section = 2; r.env.aso_marker = 0;
        enemy_ai_tick(ai, sp, r.env, m580);
        check(ai.i(0x728) == 1 && ai.cool() + 1 >= 180 && ai.cool() + 1 <= 267, "section 7 over: the attack ends, cooldown (R%30 + 60) x 3");
    }

    // ---- M140 Twarth and M270 Moz (build/ai_spec/M140.md, M270.md) ----
    {
        const EnemyBrain m140 = enemy_brain_for(7), m270 = enemy_brain_for(20);
        check(m140 && m270, "Twarth / Moz brains registered (AI 7 / 20)");
        const EnemyAiParams tp = params(0.12f, 0.03f, 1.2f, 2.8f, 0.0f);
        {   // The charge: within 5u a 5% roll starts a 113-tick run (Normal); effect 268 at Marker 1.
            Actor A; Rig r; r.env.actor = &A; r.env.target = {3.0f, 1.075f, 0};
            int run = -1;
            for (int t = 0; t < 400 && run < 0; ++t) { enemy_ai_tick(A.ai, tp, r.env, m140); if (A.ai.action() == 0x800) run = t; }
            check(run >= 0 && A.ai.dur() == 112 && A.ai.i(0x7c4) == 1, "within 5u: the run (0x800, 113 ticks on Normal)");
            r.env.section = 1; r.env.aso_marker = 1;
            const AiOut o = enemy_ai_tick(A.ai, tp, r.env, m140);
            bool fx = false; for (const AiEffect& f : o.effects) fx |= f.id == 268 && f.tag == 0x88c;
            check(fx && o.set_durable == 1 && std::fabs(A.ai.f(0x814) - 0.18f) < 1e-6f,
                  "run past Marker 1: the charge effect (EF2 268, owned), Durable, turn x1.5");
            for (int t = 0; t < 200 && A.ai.action() != 0; ++t) enemy_ai_tick(A.ai, tp, r.env, m140);
            r.env.section = 1;
            const AiOut e2 = enemy_ai_tick(A.ai, tp, r.env, m140);
            bool killed = false; for (const AiEffect& f : e2.effects) killed |= f.kill_tag == 0x88c;
            check(e2.section == 0x139 && e2.section_force && killed && A.ai.cool() + 1 == 275,
                  "run over: section 0x139 forced, the effect killed, cooldown 275 (Normal)");
        }
        {   // Moz: arms its contact sphere every 8th tick of the run section; the hurt hold.
            Actor M; M.flags12c = 0x00110010u;
            const EnemyAiParams mp = params(0.05f, 0.0335f, 1.0f, 3.5f, 0.0f);
            Rig r; r.env.actor = &M; r.env.target = {4.0f, 1.075f, 0}; r.env.section = 1;
            int arms = 0;
            for (int t = 0; t < 64; ++t)
                for (const AiHitbox& h : enemy_ai_tick(M.ai, mp, r.env, m270).hitboxes)
                    arms += h.radius == 0.45f && h.fwd == 0.5f && h.life == 8 && h.rehit == 16;
            check(arms == 8 && (M.flags12c & 0x71c802u) == 0x71c802u, "Moz: the contact sphere every 8 ticks in section 1; +0x12c |= 0x71c802");
            r.env.section = 4;
            enemy_ai_tick(M.ai, mp, r.env, m270);
            check(M.ai.action() == 1 && M.ai.dur() == 29, "in the hit reaction: stand heading-locked 30 ticks");
        }
    }

    // ---- M550 Arte-Baocar (build/ai_spec/M550.md) ----
    {
        struct Field : AiWorld {        // a floor at y = 0, open sky; holds a wounded ally
            std::list<Actor> actors;
            void for_each(const std::function<void(Actor&)>& f) override { for (Actor& a : actors) f(a); }
            Actor* find(int id) override { for (Actor& a : actors) if (a.instance_id == id) return &a; return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor&) override {}
            Ray ray(const Vec3& o, const Vec3& d) override {
                Ray r;
                if (d.y < 0 && o.y >= 0 && o.y + d.y <= 0) { r.hit = true; r.point = {o.x, 0, o.z}; }
                return r;
            }
            bool sphere_hits(const Vec3& c, float rr) override { return c.y - rr < 0; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
        };
        const EnemyBrain m550 = enemy_brain_for(48);
        check(m550 != nullptr, "Arte-Baocar brain registered (AI index 48)");
        EnemyAiParams bp = params(0.05f, 0.03f, 1.0f, 2.0f, 0.0f);
        bp.raw[8] = 1.2f;                                              // +0x834 hover
        {   // The warp-attack chain: 3 teleports near the target, then the shot cast (section 8).
            Field w; Actor& B = (w.actors.emplace_back(), w.actors.back()); B.instance_id = 3; B.cast_id = 0x3b; B.hp = B.max_hp = 50;
            B.height = 1.0f; B.flyer = true;
            Rig r; r.env.actor = &B; r.env.world = &w; r.env.target = {6.0f, 1.075f, 0};
            int warps = 0, cast = -1;
            for (int t = 0; t < 3000 && cast < 0; ++t) {
                r.env.self = {B.pos.x, B.pos.y + B.height, B.pos.z};
                const AiOut o = enemy_ai_tick(B.ai, bp, r.env, m550);
                for (const AiEffect& f : o.effects) warps += f.id == 0x10d;
                if (o.section == 8) cast = t;
            }
            std::printf("[ai]   M550: %d warp effects before the cast (tick %d)\n", warps, cast);
            check(cast > 0 && warps == 6, "chain: 3 warps (out + in effects each), then the shot cast (section 8)");
            check(B.ai.dur() == 149 && B.ai.cool() + 1 == 330, "shot cast: 150 ticks, cooldown 330 (Normal)");
            r.env.section = 8; r.env.aso_marker = 310;
            const AiOut o = enemy_ai_tick(B.ai, bp, r.env, m550);
            check(o.bullets.size() == 1 && o.bullets[0].kind == 2 && o.bullets[0].effect == 310 &&
                  o.bullets[0].speed == 0.2f && o.set_marker == 0, "Marker(310): one homing shot (kind 2, effect 310, 0.2), marker consumed");
        }
        {   // The heal cast: a wounded ally (< 30%) -> section 7; the heal shot is owned by the ally.
            Field w; Actor& B = (w.actors.emplace_back(), w.actors.back()); B.instance_id = 3; B.cast_id = 0x3b; B.hp = B.max_hp = 50;
            Actor& F = (w.actors.emplace_back(), w.actors.back()); F.instance_id = 9; F.cast_id = 0x20; F.max_hp = 100; F.hp = 20;
            B.height = 1.0f; B.flyer = true;
            Rig r; r.env.actor = &B; r.env.world = &w; r.env.target = {6.0f, 1.075f, 0};
            int heal = -1;
            for (int t = 0; t < 3000 && heal < 0; ++t) {
                r.env.self = {B.pos.x, B.pos.y + B.height, B.pos.z};
                const AiOut o = enemy_ai_tick(B.ai, bp, r.env, m550);
                if (o.section == 7) heal = t;
                if (o.section == 8) { B.ai.i(0x730) = 0; B.ai.cool() = 0; }   // skip the chain for this test
            }
            r.env.section = 7; r.env.aso_marker = 314;
            const AiOut o = enemy_ai_tick(B.ai, bp, r.env, m550);
            check(heal >= 0 && o.bullets.size() == 1 && o.bullets[0].kind == 0xb && o.bullets[0].owner == 9 &&
                  o.bullets[0].f16c == 50.0f && o.set_marker == 4,
                  "heal cast: section 7; Marker(314) a heal shot (kind 0xb) owned by the ally, heal 50 (Normal)");
        }
    }

    // ---- M600 Titamonus and M350 Diphtheloyd (build/ai_spec/M600.md, M350.md) ----
    {
        const EnemyBrain m600 = enemy_brain_for(53), m350 = enemy_brain_for(28);
        check(m600 && m350, "Titamonus / Diphtheloyd brains registered (AI 53 / 28)");
        {   // M600: 4.5..5.5u -> the 312 leap with an immediate VM step and the Marker(1) hold.
            EnemyAiParams tp = params(0.09f, 0.05f, 1.0f, 2.6f, 0.0f);
            Actor A; A.height = 1.78f;
            Rig r; r.env.actor = &A; r.env.target = {5.0f, 1.075f, 0};
            r.env.section_type = [](int id) { return id == 0x138 || id == 7 || id == 8 ? 3 : 2; };
            AiOut req;
            for (int t = 0; t < 2000; ++t) { req = enemy_ai_tick(A.ai, tp, r.env, m600); if (req.section == 0x138) break; }
            // (the action / steer the 312 block sets are run and cleared by the action player: dur 0)
            check(req.section == 0x138 && req.vm_step && req.set_marker_block == 1,
                  "4.5..5.5u: the 312 leap: gated request, immediate VM step, blk 1");
            r.env.section = 0x138; r.env.aso_marker = 1; r.env.aso_marker_block = 1;
            AiOut o = enemy_ai_tick(A.ai, tp, r.env, m600);
            check(std::fabs(A.vert.t - 0.45f) < 1e-6f && !A.vert.grounded && o.set_marker_block == 2,
                  "Marker 1: the brain launches 0.45 (its jump did not), releases to Marker 2");
            r.env.aso_marker = 2; r.env.aso_marker_block = 2; A.vert.t = -0.01f; r.env.grounded = false;
            o = enemy_ai_tick(A.ai, tp, r.env, m600);
            check(o.set_marker_block == 3, "Marker 2, past the apex: release to the dive (blk 3)");
            r.env.aso_marker = 3; r.env.aso_marker_block = 3; A.floor_y = -10.0f;
            int fx = 0, ticks = 0;
            for (; ticks < 9; ++ticks) {
                o = enemy_ai_tick(A.ai, tp, r.env, m600);
                for (const AiEffect& f : o.effects) fx += f.id == 193;
            }
            check(fx == 3, "the dive: EF2 193 every third tick");
            r.env.grounded = true; A.vert.grounded = true;
            o = enemy_ai_tick(A.ai, tp, r.env, m600);
            check(o.set_marker_block == 4 && std::fabs(A.vert.t - 0.2f) < 1e-6f, "landed: release to Marker 4 with a 0.2 hop");
            r.env.aso_marker = 4; r.env.aso_marker_block = 4;
            o = enemy_ai_tick(A.ai, tp, r.env, m600);
            check(A.ai.f(0x818) < 0 && A.ai.speed < 0 && o.set_marker_block == -1, "Marker 4: a backwards run (negative speed), released on the ground");
        }
        {   // M350: strafes on the 6-unit ring; the breath window fires every third tick.
            EnemyAiParams dp = params(0.05f, 0.1f, 1.0f, 2.0f, 0.0f);
            dp.raw[8] = 5.2f; dp.raw[9] = 0.03f; dp.raw[10] = 0.04f;
            Actor A; A.flyer = true;
            Rig r; r.env.actor = &A; r.env.target = {5.0f, 1.075f, 0};
            int sec7 = -1;
            for (int t = 0; t < 3000 && sec7 < 0; ++t) if (enemy_ai_tick(A.ai, dp, r.env, m350).section == 7) sec7 = t;
            check(sec7 >= 0 && A.ai.dur() == 141 && A.ai.i(0x728) == 2, "4.5..6.5u facing: the breath (section 7, 142 ticks)");
            r.env.section = 7; r.env.aso_marker = 302;
            AiOut o = enemy_ai_tick(A.ai, dp, r.env, m350);
            check(A.ai.i(0x7c0) == 302 && o.set_marker == 0, "Marker(302): the bullet window opens (marker consumed)");
            r.env.aso_marker = 0;
            int shots = 0;
            for (int t = 0; t < 45; ++t) for (const AiBullet& b : enemy_ai_tick(A.ai, dp, r.env, m350).bullets) shots += b.effect == 302 && b.kind == 2;
            std::printf("[ai]   M350 breath: %d bullets in 45 ticks\n", shots);
            check(shots == 15, "the window: a fire bullet (kind 2, effect 302) every third tick");
        }
    }

    // ---- M520 Menoak (build/ai_spec/M520.md) ----
    {
        const EnemyBrain m520 = enemy_brain_for(45);
        check(m520 != nullptr, "Menoak brain registered (AI index 45)");
        const EnemyAiParams mp = params(0.06f, 0.018f, 1.1f, 1.8f, 0.0f);
        {   // Far (10u), in the hop section at its first Marker(1): k = 0.018*1.8*20 = 0.648.
            Actor m; m.height = 0.775f;
            Rig r; r.env.actor = &m; r.env.target = {10.0f, 1.075f, 0}; r.env.section = 0;
            for (int t = 0; t < 3; ++t) enemy_ai_tick(m.ai, mp, r.env, m520);
            check(m.ai.f(0x820) == 0.0f && m.ai.mode() == 2, "run multiplier zeroed: 0x800 = the hop section at speed 0");
            r.env.section = 1; r.env.aso_marker = 1;
            const AiOut o = enemy_ai_tick(m.ai, mp, r.env, m520);
            check(std::fabs(m.ai.f(0x7ac) - 0.648f * 0.99f) < 1e-3f || std::fabs(m.ai.f(0x7ac) - 0.648f) < 1e-3f,
                  "hop marker (first after engaging): leap strength 0.648");
            check(o.set_marker == 2 && !m.vert.grounded && m.mover.vx > 0.05f,
                  "hopping: airborne, velocity blended toward k along the heading (x0.15 first)");
        }
        {   // Close: the spin (section 7, 150..209, cooldown + 240), then the spin-down (8, ungated).
            Actor m; m.height = 0.775f;
            Rig r; r.env.actor = &m; r.env.target = {2.0f, 1.075f, 0}; r.env.section = 0;
            int sec7 = -1, dur7 = 0, cool7 = 0, sec8 = -1;
            for (int t = 0; t < 1200 && sec8 < 0; ++t) {
                r.env.grounded = true; m.vert.grounded = true;
                const AiOut o = enemy_ai_tick(m.ai, mp, r.env, m520);
                if (o.section == 7 && sec7 < 0) { sec7 = t; dur7 = m.ai.dur() + 1; cool7 = m.ai.cool() + 1; r.env.section = 7; }
                if (o.section == 8 && o.section_force && sec8 < 0) sec8 = t;
            }
            std::printf("[ai]   M520 spin @%d (dur %d cool %d), spin-down @%d\n", sec7, dur7, cool7, sec8);
            check(sec7 >= 0 && dur7 >= 150 && dur7 <= 209 && cool7 == dur7 + 240, "spin: section 7, 150..209 ticks, cooldown + 240");
            check(sec8 - sec7 >= dur7 && sec8 - sec7 <= dur7 + 2, "spin-down: section 8 (ungated) when the spin runs out");
        }
    }

    // ---- M210 Kelemare (build/ai_spec/M210.md) ----
    {
        const EnemyBrain m210 = enemy_brain_for(14);
        check(m210 != nullptr, "Kelemare brain registered (AI index 14)");
        const EnemyAiParams kp = params(0.08f, 0.03f, 1.0f, 2.0f, 0.0f);
        {   // 7u, level: it stands and spits (section 8), hold 60 on Normal; then the lockout.
            Actor k; k.height = 1.0f;
            Rig r; r.env.actor = &k; r.env.target = {7.0f, 1.075f, 0};
            int spit = -1;
            for (int t = 0; t < 200 && spit < 0; ++t) if (enemy_ai_tick(k.ai, kp, r.env, m210).section == 8) spit = t;
            check(spit >= 0 && k.ai.dur() == 59 && k.ai.i(0x7a8) >= 120 && k.ai.i(0x7a8) < 240,
                  "within 9.5u, level: spit (section 8), hold 60, lockout 180 + R%60 (- the tick)");
        }
        {   // 2u: the 21% sting (section 7); afterwards it flees (sub 2, run x2.5).
            Actor k; k.height = 1.0f;
            Rig r; r.env.actor = &k; r.env.target = {2.0f, 1.075f, 0};
            int sting = -1;
            for (int t = 0; t < 400 && sting < 0; ++t) if (enemy_ai_tick(k.ai, kp, r.env, m210).section == 7) sting = t;
            check(sting >= 0, "close: the sting (section 7)");
            r.env.section = 7;
            for (int t = 0; t < 3; ++t) enemy_ai_tick(k.ai, kp, r.env, m210);
            r.env.section = 2;
            for (int t = 0; t < 70; ++t) enemy_ai_tick(k.ai, kp, r.env, m210);
            std::printf("[ai]   M210 after sting: sub %d act 0x%x steer %d run %.2f dur %d 7c0 %d 79c %d\n",
                        k.ai.i(0x728), k.ai.action(), k.ai.steer(), k.ai.f(0x820), k.ai.dur(), k.ai.i(0x7c0), k.ai.i(0x79c));
            check(k.ai.i(0x728) == 2 && std::fabs(k.ai.f(0x820) - 2.0f * 2.5f) < 1e-5f,
                  "after the sting: flees (sub 2, steer away, run x2.5)");
        }
    }

    // ---- M500 Oggnome, the teleporter (build/ai_spec/M500.md) ----
    {
        const EnemyBrain m500 = enemy_brain_for(43);
        check(m500 != nullptr, "Oggnome brain registered (AI index 43)");
        struct Flat : AiWorld {       // an open floor at y = 0, no walls
            void for_each(const std::function<void(Actor&)>&) override {}
            Actor* find(int) override { return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor&) override {}
            Ray ray(const Vec3& o, const Vec3& dl) override {
                Ray r;
                if (dl.y < 0 && o.y > 0 && o.y + dl.y <= 0) { r.hit = true; r.point = {o.x, 0, o.z}; r.normal = {0, 1, 0}; }
                return r;
            }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
        } flat;
        const EnemyAiParams op = params(1.0f, 0.035f, 1.0f, 2.0f, 0.0f);
        Actor og; og.height = 1.0f; og.pos = {0, 0, 0};
        Rig r; r.env.actor = &og; r.env.world = &flat;
        r.env.target = {5.0f, 1.075f, 0};
        r.env.section_type = [](int id) { return id == 0x12d || id == 0x12e || id == 7 ? 3 : id <= 3 ? 2 : 4; };
        AiOut o = enemy_ai_tick(og.ai, op, r.env, m500);
        check(o.section == 0x12e && o.section_force && og.ai.i(0x728) == 2, "init: forced fade-out (302), sub 2");
        r.env.section = 0x12e; r.env.aso_frame = 5;
        enemy_ai_tick(og.ai, op, r.env, m500);
        check(og.ai.f(0x818) == 0 && og.ai.speed == 0, "never walks: base speed zeroed (from tick 2)");
        check(std::fabs(og.ai.alpha - 0.5f) < 1e-6f && !og.ai.hittable, "fading (frame 5): alpha 0.5, unhittable");
        r.env.aso_frame = 7;
        enemy_ai_tick(og.ai, op, r.env, m500);
        check(og.ai.i(0x728) == 3 && og.ai.cool() < 120, "frame 7: hidden (sub 3), timer 0..119");
        // Hidden and engaged: it relocates 1.5..8 u from the target, 0.5 above the floor.
        bool moved = false;
        for (int t = 0; t < 400 && !moved; ++t) {
            r.env.self = {og.pos.x, og.pos.y + og.height, og.pos.z};
            enemy_ai_tick(og.ai, op, r.env, m500);
            moved = og.pos.x != 0 || og.pos.z != 0;
        }
        const float tdx = og.pos.x - 5.0f, tdz = og.pos.z;
        const float td = std::sqrt(tdx * tdx + tdz * tdz);
        std::printf("[ai]   M500 relocated to (%.2f, %.2f, %.2f), %.2f from the target\n", og.pos.x, og.pos.y, og.pos.z, td);
        check(moved && td >= 1.5f && td < 8.0f && std::fabs(og.pos.y + og.height - 0.5f) < 1e-5f,
              "relocates 1.5..8 u from the target, origin 0.5 above the floor");
        // Appear, ready, attack: section 7, Marker(1) -> N = 2 balls on Normal.
        r.env.self = {og.pos.x, og.pos.y + og.height, og.pos.z};
        int sec7 = -1, balls = 0;
        for (int t = 0; t < 2000 && balls == 0; ++t) {
            o = enemy_ai_tick(og.ai, op, r.env, m500);
            if (o.section >= 0) { r.env.section = o.section; r.env.aso_frame = 0; }
            if (o.section == 7 && sec7 < 0) { sec7 = t; r.env.aso_marker = 1; }
            balls = int(o.bullets.size());
        }
        check(sec7 >= 0 && balls == 2 && o.bullets[0].effect == 0x1c0 && o.set_marker == 2 && og.ai.cool() == 37,
              "attack: section 7, at Marker(1) two m_dokutama balls (Normal), cooldown 38");
    }

    // ---- M120 Jestona (build/ai_spec/M120.md) ----
    {
        const EnemyBrain m120 = enemy_brain_for(5);
        check(m120 != nullptr, "Jestona brain registered (AI index 5)");
        struct Open : AiWorld {       // a flat floor at 0, no walls
            void for_each(const std::function<void(Actor&)>&) override {}
            Actor* find(int) override { return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor&) override {}
            Ray ray(const Vec3& o, const Vec3& dl) override {
                Ray r; if (dl.y < 0 && o.y > 0 && o.y + dl.y <= 0) { r.hit = true; r.point = {o.x, 0, o.z}; } return r;
            }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
        } open;
        const EnemyAiParams jp = params(0.1f, 0.03f, 1.0f, 3.0f, 0.0f);
        Actor j; j.height = 1.0f;
        Rig r; r.env.actor = &j; r.env.world = &open; r.env.target = {2.0f, 1.075f, 0};
        int slash = -1;
        for (int t = 0; t < 400 && slash < 0; ++t) if (enemy_ai_tick(j.ai, jp, r.env, m120).section == 7) slash = t;
        check(slash >= 0 && j.ai.dur() == 59 && j.ai.cool() >= 74 && j.ai.cool() <= 103,
              "within 3.5u facing: the slash (section 7), hold 60, cooldown hold + 15..44");
        r.env.section = 7;
        int back = -1;
        for (int t = 0; t < 80 && back < 0; ++t) if (enemy_ai_tick(j.ai, jp, r.env, m120).section == 3) back = t;
        check(back >= 0 && j.ai.i(0x728) == 1 && j.ai.i(0x7a0) == 1, "after the slash (within 5.5u): backstep, section 3, hop kind 1 (back)");
        r.env.section = 3; r.env.aso_marker = 1; j.vert.grounded = true; r.env.grounded = true;
        enemy_ai_tick(j.ai, jp, r.env, m120);
        check(!j.vert.grounded && std::fabs(j.vert.t - 0.17f) < 1e-6f && j.mover.k1.x < 0,
              "at Marker(1): a 0.17 hop pushed AWAY from the target (x0.8 on Normal)");
    }

    // ---- M460 the bat-swarm boss + M970 brain B (build/ai_spec/M460.md, M970.md §2B) ----
    {
        // A 24 x 24 walled room, floor y = 0; the player stands at (0, 0, -6).
        struct Room : AiWorld {
            std::list<Actor> actors;
            int next_id = 200;
            float raw87[16] = {0.05f, 0.04f, 1.0f, 2.0f, 0, 0, 0, 0, 1.0f, 0.03f, 0.03f, 0.3f, 10.0f, 0, 49.5f, 52.0f};
            void for_each(const std::function<void(Actor&)>& f) override {
                for (Actor& a : actors) if (!a.destroy_requested()) f(a);
            }
            Actor* find(int id) override {
                for (Actor& a : actors) if (a.instance_id == id && !a.destroy_requested()) return &a;
                return nullptr;
            }
            Actor* spawn_child(Actor& p, int cast, const Vec3& d, int lv, bool keep0) override {
                actors.emplace_back();
                Actor& c = actors.back();
                c.instance_id = next_id++; c.cast_id = cast; c.level = lv;
                c.pos = {p.pos.x + d.x, p.pos.y + d.y, p.pos.z + d.z};
                c.height = 0.5f; c.hp = c.max_hp = 30; c.flyer = true; c.floor_y = 0;
                c.ai.heading = p.ai.heading; c.ai.i(0x854) = p.instance_id; c.ai.phase() = keep0 ? 0 : 4;
                return &c;
            }
            void remove(Actor& a) override { a.header_flags |= kDestroyRequested; a.hp = 0; }
            Ray ray(const Vec3& o, const Vec3& dl) override {   // the walls at |x|, |z| = 12
                Ray r; float best = 2;
                auto tryp = [&](float t, Vec3 n) { if (t > 1e-4f && t < best) { best = t; r.normal = n; } };
                if (dl.x != 0) { tryp((12 - o.x) / dl.x, {-1, 0, 0}); tryp((-12 - o.x) / dl.x, {1, 0, 0}); }
                if (dl.z != 0) { tryp((12 - o.z) / dl.z, {0, 0, -1}); tryp((-12 - o.z) / dl.z, {0, 0, 1}); }
                if (best > 1) return r;
                r.hit = true; r.point = {o.x + dl.x * best, o.y + dl.y * best, o.z + dl.z * best};
                return r;
            }
            bool sphere_hits(const Vec3& c, float rr) override {
                return std::fabs(c.x) > 12 - rr || std::fabs(c.z) > 12 - rr;
            }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int cast) override { return cast == 0xb7 ? raw87 : nullptr; }
        } room;
        XorShift rng;
        // M460: EI[36] +0x90..+0xcc.
        EnemyAiParams bp = params(0.16f, 0.12f, 1.0f, 2.0f, 2.5f);
        bp.raw[4] = 1.0f; bp.raw[5] = 0.1f; bp.raw[6] = 0.01f; bp.raw[7] = 0.01f;
        bp.raw[9] = 0.1f; bp.raw[10] = 0.1f; bp.raw[11] = 0.3f; bp.raw[12] = 3.0f;
        const EnemyAiParams batp = params(0.05f, 0.04f, 1.0f, 2.0f, 1.0f);
        room.actors.emplace_back();
        Actor& boss = room.actors.back();
        boss.instance_id = 50; boss.cast_id = 0x32; boss.hp = boss.max_hp = 600; boss.def = 20;
        boss.height = 1.5f; boss.radius = 1.0f; boss.flyer = true; boss.ai.alpha = 1.0f;
        const EnemyBrain m460 = enemy_brain_for(39), m970 = enemy_brain_for(90);
        check(m460 != nullptr, "M460 brain registered (AI index 39)");
        std::unordered_map<int, int> sec;                  // per-actor ASO section stand-in
        std::unordered_map<int, int> mark;
        auto tick_actor = [&](Actor& a, const EnemyAiParams& pp, EnemyBrain br) {
            AiEnv e;
            e.actor = &a; e.world = &room;
            e.self = {a.pos.x, a.pos.y + a.height, a.pos.z};
            e.height = a.height; e.radius = a.radius;
            e.target = {0, 1.075f, -6}; e.target_floor = 0;
            e.hp = a.hp; e.max_hp = a.max_hp;
            e.section = sec[a.instance_id]; e.aso_marker = mark[a.instance_id];
            e.rng = [&] { return rng(); };
            const AiOut o = enemy_ai_tick(a.ai, pp, e, br);
            if (o.section >= 0) sec[a.instance_id] = o.section;
            if (o.set_marker > -1000) mark[a.instance_id] = o.set_marker;
            // the swarm moves: speed along the heading (no ASO Movable here)
            a.pos.x += std::cos(a.ai.heading) * a.ai.speed;
            a.pos.z += std::sin(a.ai.heading) * a.ai.speed;
            return o;
        };
        int t = 0, sec8_at = -1, spawned_at = -1, gone_at = -1;
        for (; t < 400 && gone_at < 0; ++t) {
            const AiOut o = tick_actor(boss, bp, m460);
            if (o.section == 8 && sec8_at < 0) sec8_at = t;
            if (sec8_at >= 0 && t == sec8_at + 36) mark[50] = 1;   // section 8 reaches Marker(1)
            int bats = 0;
            for (Actor& b : room.actors) if (b.cast_id == 0xb7) { ++bats; tick_actor(b, batp, m970); }
            if (bats > 0 && spawned_at < 0) spawned_at = t;
            if (spawned_at >= 0 && boss.ai.alpha == 0.0f && gone_at < 0) gone_at = t;
        }
        int nbat = 0, owned = 0, phase4 = 0, invis = 0;
        float minx = 1e9f, maxx = -1e9f;
        for (Actor& b : room.actors) if (b.cast_id == 0xb7) {
            ++nbat; owned += b.ai.i(0x854) == 50; phase4 += b.ai.phase() == 4;
            invis += b.def == 999.0f && b.no_min_damage;
            minx = std::min(minx, b.ai.f(0x7f0)); maxx = std::max(maxx, b.ai.f(0x7f0));
        }
        std::printf("[ai]   M460 section 8 @%d, bats @%d (%d), faded out @%d; slot x %.2f..%.2f\n",
                    sec8_at, spawned_at, nbat, gone_at, minx, maxx);
        check(sec8_at == 90, "hold 90 - 30d ticks (Normal: 90), then dissolve (section 8)");
        check(nbat == 32 && owned == 32 && phase4 == 32, "Marker(1): 32 Vagully, owned, phase 4");
        check(invis == 32, "bats: DEF 999, no min-1 chip");
        check(std::fabs(minx + 1.125f) < 1e-4f && std::fabs(maxx - 1.125f) < 1e-4f,
              "4 x 4 x 2 formation: slot x = (q%4 - 1.5) * 0.75");
        check(gone_at - spawned_at >= 39 && gone_at - spawned_at <= 41, "the body fades out over 40 ticks");
        // Fly + swarm for a while: the bats fade in and swarm; the core heads for waypoints.
        int bat_vis = 0, wp_set = 0;
        for (int k = 0; k < 120; ++k) {
            tick_actor(boss, bp, m460);
            for (Actor& b : room.actors) if (b.cast_id == 0xb7) tick_actor(b, batp, m970);
            wp_set += boss.ai.i(0x728) == 0xb;
        }
        for (Actor& b : room.actors) if (b.cast_id == 0xb7) bat_vis += b.ai.alpha == 1.0f && b.ai.i(0x728) == 10;
        check(bat_vis == 32, "bats fade in (+0.04/tick) and swarm (sub 10)");
        check(wp_set > 0, "the invisible core picks wall-side waypoints and flies (sub 0xb)");
        check(!boss.ai.hittable, "the core is unhittable while invisible");
        // Recall after 400 ticks of flight.
        bool recalled = false, bats_return = false;
        for (int k = 0; k < 600 && !bats_return; ++k) {
            tick_actor(boss, bp, m460);
            for (Actor& b : room.actors) if (b.cast_id == 0xb7) tick_actor(b, batp, m970);
            recalled |= boss.ai.i(0x728) == 0x14;
            int ret = 0;
            for (Actor& b : room.actors) if (b.cast_id == 0xb7) ret += b.ai.i(0x728) == 0x14;
            bats_return = ret == 32;
        }
        check(recalled && bats_return, "after 400 ticks: recall (0x14); every bat heads back (0x14)");
        // Dock -> (re-scatter at most twice) -> reform (section 21) -> fade in -> attack choice.
        int reform_at = -1, attack_sub = -1, k = 0, scatters = 0, prev_sub = boss.ai.i(0x728);
        for (; k < 6000 && attack_sub < 0; ++k) {
            const AiOut o = tick_actor(boss, bp, m460);
            const int s = boss.ai.i(0x728);
            if (prev_sub == 0x14 && s == 10) ++scatters;
            prev_sub = s;
            if (o.section == 0x15 && reform_at < 0) reform_at = k;
            if (s == 0x32 || s == 0x3c || s == 0x46) attack_sub = s;
            if (s == 0x1e && reform_at >= 0) sec[50] = 0x15;   // section 21 is playing
            for (Actor& b : room.actors) if (b.cast_id == 0xb7 && !b.destroy_requested()) tick_actor(b, batp, m970);
        }
        int left = 0;
        for (Actor& b : room.actors) left += b.cast_id == 0xb7 && !b.destroy_requested();
        std::printf("[ai]   M460 re-scatters %d, reform @%d, attack sub 0x%x @%d, bats left %d\n",
                    scatters, reform_at, attack_sub, k, left);
        check(scatters <= 2, "re-scatters at most twice per cycle (+0x7a0 < 2)");
        check(reform_at >= 0 && left == 0, "reform: section 21, the flock dissolves into the body");
        check(attack_sub > 0 && boss.ai.alpha == 1.0f, "visible again, it picks an attack (rush / shots / gas)");
    }

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
