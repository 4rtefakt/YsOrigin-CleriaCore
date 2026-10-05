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

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
