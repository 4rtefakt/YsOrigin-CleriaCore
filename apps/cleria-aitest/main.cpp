// Headless tests for src/cleria/enemy_ai.hpp — the shared enemy machinery and the M100
// brain, driven through scripted situations with a deterministic xorshift128.
#include <cmath>
#include <cstdio>
#include <utility>

#include "cleria/enemy_ai.hpp"

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

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
