// Headless tests for src/cleria/enemy_ai.hpp — the shared enemy machinery and the M100
// brain, driven through scripted situations with a deterministic xorshift128.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cleria/actor.hpp"
#include "cleria/enemy_ai.hpp"
#include "cleria/enemy_mover.hpp"
#include "cleria/factory_flags.hpp"
#include "cleria/hugo_eyes.hpp"

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

#include "floor4.inl"   // the floor-4 brains (S_40)
#include "floor5.inl"   // the floor-5 brains (S_50)
#include "floor6.inl"   // the floor-6 brains (S_60)

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
    {   // build/audit/REACQUIRE.md §3.2: D4 (acquire), D7 (a dead target), D3 (M100's leash).
        {   // D4: FUN_004758a0 zeroes +0x118 as FUN_00475860 does; nothing while +0x884 runs.
            EnemyAi ai; ai.phase() = 1; ai::acquire(ai);
            check(ai.phase() == 2 && ai.hit_cut, "acquire: phase 2, the armed hit cut (+0x118 = 0)");
            EnemyAi held; held.phase() = 1; held.leash() = 5; ai::acquire(held);
            check(held.phase() == 1 && !held.hit_cut, "acquire while +0x884 != 0: nothing");
        }
        // D7 (0x47a472 / 0x47a353): `bloody` = byte 0x6d5d92 > 1; a cast with +0x12c & 0x2000.
        auto dead_run = [&](bool bloody, std::uint32_t f12c, bool engaged_first) {
            Actor act; act.flags12c |= f12c;
            Rig r; EnemyAi& ai = act.ai; r.env.actor = &act; r.env.bloody = bloody;
            r.env.target = {3.0f, 1.075f, 0};
            r.env.target_alive = engaged_first;
            for (int i = 0; i < 3; ++i) enemy_ai_tick_m100(ai, p, r.env);
            const int before = ai.phase();
            r.env.target_alive = false;
            int low = ai.phase();
            for (int t = 0; t < 400; ++t) { enemy_ai_tick_m100(ai, p, r.env); low = std::min(low, int(ai.phase())); }
            return std::pair<int, int>(before, low);
        };
        check(dead_run(false, 0, true) == std::pair<int, int>(2, 1), "a dead target, BloodyEffectLevel <= 1: phase 2 -> 1");
        check(dead_run(true, 0, true) == std::pair<int, int>(2, 2), "a dead target, BloodyEffectLevel > 1: still hunted");
        check(dead_run(false, 0x2000u, true) == std::pair<int, int>(2, 2), "a dead target, a +0x12c & 0x2000 cast: still hunted");
        check(dead_run(false, 0, false).first == 1, "unaware and the target dead: not acquired");
        check(dead_run(true, 0, false).first == 2, "...acquired with BloodyEffectLevel > 1");
        {   // D3 (0x47a39e): +0x12c & 0x800000, > 12.5 from the anchor +0x51c and > 4.0 from the target.
            Actor act; act.flags12c = 0x00910010;                  // factory_flags.hpp, cast 0x0e M100
            Rig r; EnemyAi& ai = act.ai; r.env.actor = &act;
            r.env.target = {6.0f, 1.075f, 0};
            for (int i = 0; i < 4; ++i) enemy_ai_tick_m100(ai, p, r.env);
            check(ai.phase() == 2 && ai.anchor.x == 0.0f, "leash: engaged, the anchor is the origin of the phase change");
            r.env.self = {12.0f, 1.075f, 0}; r.env.target = {18.0f, 1.075f, 0};
            enemy_ai_tick_m100(ai, p, r.env);
            check(ai.phase() == 2, "leash: 12.0 from the anchor holds");
            r.env.self = {13.0f, 1.075f, 0}; r.env.target = {16.5f, 1.075f, 0};
            enemy_ai_tick_m100(ai, p, r.env);
            check(ai.phase() == 2, "leash: 13 from the anchor but 3.5 from the target holds");
            r.env.target = {19.0f, 1.075f, 0};
            enemy_ai_tick_m100(ai, p, r.env);
            check(ai.phase() == 1 && ai.leash() == 19, "leash: > 12.5 and > 4.0 -> disengage, +0x884 = 20 (- the tick)");
            int back = -1;
            for (int t = 1; t <= 20 && back < 0; ++t) { enemy_ai_tick_m100(ai, p, r.env); if (ai.phase() == 2) back = t; }
            std::printf("[ai]   leash: re-acquired %d ticks after the drop\n", back);
            check(back >= 4 && back <= 6 && ai.anchor.x == 13.0f, "leash: +0x884 >> 2, then re-acquired from the new anchor");
            Actor other;                                           // no 0x800000: no leash
            Rig q; EnemyAi& bi = other.ai; q.env.actor = &other; q.env.target = {6.0f, 1.075f, 0};
            for (int i = 0; i < 4; ++i) enemy_ai_tick_m100(bi, p, q.env);
            q.env.self = {13.0f, 1.075f, 0}; q.env.target = {19.0f, 1.075f, 0};
            enemy_ai_tick_m100(bi, p, q.env);
            check(bi.phase() == 2, "no +0x12c & 0x800000: no leash");
        }
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
        {   // M970's handler tail (0x4cc375): a swarm bat (phase 4) stays in the box, whatever the mode.
            const float rect[4] = {-12.396f, -22.525f, 12.983f, -0.7f};   // S_1080's EnclosePlayer
            Vec3 o{-6.5f, -15.0f, -23.9f}, q{-6.5f, -15.0f, -23.9f}, m{-6.5f, -15.0f, -23.9f};
            enemy_handler_clamp(90, o, 0, false, rect, 0, 4);
            enemy_handler_clamp(90, q, 0, false, rect, 0, 0);
            enemy_handler_clamp(90, m, 2, true, rect, 0, 4);
            check(o.z == -22.525f && o.x == -6.5f && q.z == -23.9f && m.z == -22.525f,
                  "M970 clamp: a phase-4 bat is held in the box (mode-blind); a lone bat is not");
        }
    }

    // ---- M590 Sepaswait (build/ai_spec/M590.md) ----
    {
        const EnemyBrain m590 = enemy_brain_for(52);
        check(m590 != nullptr, "Sepaswait brain registered (AI index 52)");
        const EnemyAiParams sp = params(0.075f, 0.03f, 1.0f, 2.0f, 0.0f);
        {   // 4u away: legs away from it, a laser roll; the hold fires 3 lasers every 25 ticks.
            Actor A; A.cast_id = 63; A.flags12c = 0x00098050u;
            Rig r; r.env.actor = &A; r.env.target = {4.0f, 1.075f, 0};
            int t = 0;
            for (; t < 4000 && A.ai.i(0x72c) != 101; ++t) {
                enemy_ai_tick(A.ai, sp, r.env, m590);
                if (A.ai.i(0x72c) == 200) { A.ai.i(0x72c) = 20; A.ai.cool() = 0; }   // laser only here
            }
            const int hold = A.ai.dur(), win = A.ai.i(0x7a8);
            std::printf("[ai]   M590 laser at tick %d: dur %d, window %d, lasers since dash %d\n", t, hold, win, A.ai.i(0x7c4));
            check(A.ai.i(0x72c) == 101 && hold - win >= 29 && hold - win <= 30 && hold >= 359 && hold <= 389 &&
                  A.ai.i(0x7c4) == 1, "the laser: hold 360 + R%30, window opens 30 ticks in");
            r.env.section = 2; r.env.aso_marker = 9025446;
            int volleys = 0, lasers = 0, se = 0, moves = 0, durable_ok = 1, k = 0;
            for (; k < 500 && A.ai.i(0x72c) == 101; ++k) {
                r.env.aso_frame = (k / 9) % 8;
                const AiOut o = enemy_ai_tick(A.ai, sp, r.env, m590);
                int n = 0;
                for (const AiEffect& f : o.effects) {
                    if (f.id == 446 && f.rtype == 1 && f.tag >= 0xfd) ++n;
                    moves += f.move_tag >= 0xfd;
                }
                lasers += n; volleys += n == 3;
                if (n) durable_ok &= o.set_durable == 1;
                for (int s : o.sounds) se += s == 523;
            }
            std::printf("[ai]   M590 hold: %d volleys (%d lasers), SE 523 x%d, cool %d\n", volleys, lasers, se, A.ai.cool());
            check(volleys >= 9 && volleys <= 12 && lasers == volleys * 3 && durable_ok,
                  "the window: 3 lasers (EF2 446, rtype 1, owned) per 25-tick volley, Durable");
            check(se >= 4 && se <= 6 && moves == k * 3 && A.ai.i(0x72c) == 0 && A.ai.cool() == 149,
                  "SE 523 every ~58 ticks; the ring re-placed every tick; cool 150 - 20d (- the tick)");
        }
        {   // The dash: section 7, rooted, then the ramp at a tenth of the turn rate; sparkles; 202.
            Actor A; A.cast_id = 63; A.flags12c = 0x00198050u;      // probe off for the test
            Rig r; r.env.actor = &A; r.env.target = {4.0f, 1.075f, 0};
            enemy_ai_tick(A.ai, sp, r.env, m590);
            enemy_ai_tick(A.ai, sp, r.env, m590);
            A.ai.i(0x72c) = 200;
            const AiOut o = enemy_ai_tick(A.ai, sp, r.env, m590);
            check(o.section == 7 && A.ai.i(0x72c) == 201 && o.set_durable == 1, "dash wind-up at frame 0: section 7 (gated), Durable");
            r.env.section = 7; r.env.movable = 0.0f; r.env.aso_marker = 72;
            enemy_ai_tick(A.ai, sp, r.env, m590);
            check(A.ai.f(0x78c) == 0.0f && std::fabs(A.ai.f(0x814) - 0.075f) < 1e-7f, "rooted (Movable 0): no ramp, full turn rate");
            r.env.movable = 1.0f;
            int sparks = 0; bool scale_ok = true;
            float s1 = 0, s2 = 0;
            for (int t = 0; t < 60; ++t) {
                const AiOut d = enemy_ai_tick(A.ai, sp, r.env, m590);
                if (t == 0) s1 = A.ai.speed;
                if (t == 1) s2 = A.ai.speed;
                for (const AiEffect& f : d.effects) if (f.id == 72) { ++sparks; scale_ok &= f.scale >= 0.5f && f.scale < 2.5f; }
            }
            std::printf("[ai]   M590 dash: speed %.4f, %.4f; %d sparkles in 60 ticks\n", s1, s2, sparks);
            check(A.ai.f(0x78c) == 10.0f && std::fabs(A.ai.f(0x814) - 0.0075f) < 1e-7f &&
                  std::fabs(s1 - 0.06f) < 1e-7f && std::fabs(s2 - 0.0645f) < 1e-7f,
                  "Movable: ramp target 10 (+0.15/call: 0.06, 0.0645), turn rate x0.1");
            check(sparks >= 6 && sparks <= 12 && scale_ok, "sparkles: the marker effect (72), every 12 -> 4 ticks with the ramp");
            r.env.aso_frame = 7;
            AiOut w;
            for (int t = 0; t < 300 && A.ai.i(0x72c) == 201; ++t) w = enemy_ai_tick(A.ai, sp, r.env, m590);
            check(A.ai.i(0x72c) == 202 && w.section == 8 && A.ai.f(0x78c) == 2.0f && A.ai.f(0x82c) == 0.05f,
                  "frame 7 after 180 ticks: section 8 (gated), ramp target 2.0, steps 0.05");
            r.env.section = 8;
            enemy_ai_tick(A.ai, sp, r.env, m590);
            r.env.section = 2;
            enemy_ai_tick(A.ai, sp, r.env, m590);
            check(A.ai.i(0x72c) == 0 && A.ai.f(0x78c) == 0.0f && A.ai.cool() == 119, "section 8 over: stop, cool 120 - 20d");
        }
        {   // Two Sepaswaits inside 2r + 0.2: each is nudged 0.0625 apart; spark + SE 472.
            struct Two : AiWorld {
                std::list<Actor> actors;
                void for_each(const std::function<void(Actor&)>& f) override { for (Actor& a : actors) f(a); }
                Actor* find(int) override { return nullptr; }
                Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
                void remove(Actor&) override {}
                Ray ray(const Vec3&, const Vec3&) override { return {}; }
                bool sphere_hits(const Vec3&, float) override { return false; }
                void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
                const float* enemy_raw(int) override { return nullptr; }
            } w;
            Actor& A = (w.actors.emplace_back(), w.actors.back());
            Actor& B = (w.actors.emplace_back(), w.actors.back());
            A.cast_id = B.cast_id = 63; A.hp = B.hp = 50; A.pos = {0, 0, 0}; B.pos = {1.0f, 0, 0};
            B.ai.phase() = 2;
            Rig r; r.env.actor = &A; r.env.world = &w; r.env.radius = 0.9f; r.env.target = {9.0f, 1.075f, 9.0f};
            const AiOut o = enemy_ai_tick(A.ai, sp, r.env, m590);
            bool spark = false, se = false;
            for (const AiEffect& f : o.effects) spark |= f.id == 65;
            for (int s : o.sounds) se |= s == 472;
            check(std::fabs(A.pos.x + 0.0625f) < 1e-6f && B.ai.f(0x7cc) > 0.0f && spark && se,
                  "neighbour push: a 0.0625 nudge away, the other's accumulator pushed, spark + SE 472");
        }
    }

    // ---- the move primitive FUN_00477720 (build/ai_spec/MOVEPRIM.md) ----
    {
        auto bits = [](float v) { std::uint32_t u; std::memcpy(&u, &v, 4); return u; };
        {   // §4.3: M590's dash ramp, R 10, steps 0.15, f 2.0, base 0.03 (the probe off)
            Actor A; A.flags12c = 0x00110010u;
            Rig r; r.env.actor = &A;
            EnemyAi& a = A.ai;
            a.phase() = 2;
            a.f(0x818) = 0.03f; a.f(0x820) = 2.0f; a.f(0x81c) = 1.0f;
            a.f(0x78c) = 10.0f; a.f(0x82c) = 0.15f; a.f(0x830) = 0.15f; a.f(0x824) = 0.0f;
            std::uint32_t s[60];
            float r53 = 0;
            for (int n = 0; n < 60; ++n) {
                ai::move(a, p, r.env, 0, 0);
                s[n] = bits(a.speed);
                if (n == 53) r53 = a.f(0x824);
            }
            std::printf("[ai]   ramp: s0 %08x s1 %08x s5 %08x s54 %08x s59 %08x r53 %08x\n", s[0], s[1], s[5], s[54], s[59], bits(r53));
            check(s[0] == 0x3d75c28fu && s[1] == 0x3d841894u && s[5] == 0x3da8f5c2u && a.mode() == 2,
                  "ramp: 0.06, 0.0645 .. 0.0825 (f32 exact), mode 2");
            check(s[54] == 0x3e9b22d4u && s[59] == 0x3e9b22d4u && bits(r53) == 0x4101999du,
                  "ramp: r stops at 8.100003 after 54 steps; factor 10.1 from call 54 on");
            a.f(0x78c) = 0.0f; a.f(0x824) = 0.0f;
            ai::move(a, p, r.env, 0, 0);
            check(bits(a.speed) == 0x3d75c28fu, "R = 0: speed = +0x818 x +0x820 (0.06)");
        }
        struct Ledge : AiWorld {        // floor (y = 0) only where x < 0.9 or |z| >= 0.9; no walls
            void for_each(const std::function<void(Actor&)>&) override {}
            Actor* find(int) override { return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor&) override {}
            Ray ray(const Vec3& o, const Vec3& d) override {
                Ray r;
                if (d.y < 0 && o.y >= 0 && o.y + d.y <= 0 && (o.x < 0.9f || std::fabs(o.z) >= 0.9f)) {
                    r.hit = true; r.point = {o.x, 0, o.z};
                }
                return r;
            }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
        };
        auto ledge_case = [&](std::uint32_t f130, float* heading, int* mode, float* speed) {
            Ledge w;
            Actor A; A.flags12c = 0x00010010u; A.flags130 = f130;
            Rig r; r.env.actor = &A; r.env.world = &w;
            r.env.self = {0, 1.0f, 0}; r.env.height = 1.0f; r.env.radius = 0.675f; r.env.facing = 0.0f;
            EnemyAi& a = A.ai;
            a.phase() = 2; a.mode() = 2; a.heading = 0.0f; a.speed = 0.123f;
            a.f(0x818) = 0.03f; a.f(0x820) = 2.0f; a.f(0x81c) = 1.0f;
            ai::move(a, p, r.env, 0, 0);
            *heading = a.heading; *mode = a.mode(); *speed = a.speed;
        };
        float h = 0, sp = 0; int m = 0;
        ledge_case(0x800u, &h, &m, &sp);
        std::printf("[ai]   fan: heading %.7f mode %d speed %.3f\n", h, m, sp);
        check(std::fabs(h - 1.2f) < 1e-6f && m == 2 && sp == 0.123f,
              "fan: a ledge ahead, floor from +-0.6: turns to facing + 1.2 (3 clear in a row), keeps mode/speed");
        ledge_case(0u, &h, &m, &sp);
        check(h == 0.0f && m == 0 && sp == 0.0f, "no +0x130 & 0x800: the ledge stops it");
    }

    // ---- M850 Epona, the duel (build/ai_spec/M850.md) ----
    {
        struct Duel : AiWorld {         // open floor, the S_2080 arena rect, and the game flags
            std::unordered_map<int, int> g;
            void for_each(const std::function<void(Actor&)>&) override {}
            Actor* find(int) override { return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor&) override {}
            Ray ray(const Vec3&, const Vec3&) override { return {}; }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = -13.437f; r[1] = 32.7f; r[2] = 13.587f; r[3] = 55.3f; }
            const float* enemy_raw(int) override { return nullptr; }
            void set_flag(int i, int v) override { g[i] = v; }
            int flag(int i, int fb) override { const auto it = g.find(i); return it == g.end() ? fb : it->second; }
        } w;
        const EnemyBrain m850 = enemy_brain_for(78);
        const EnemyDeathHook hk = enemy_death_hook_for(78);
        check(m850 != nullptr && hk != nullptr, "Epona brain + dead hook registered (AI index 78)");
        const EnemyAiParams ep = params(0.12f, 0.05f, 1.0f, 2.0f, 0.0f);
        auto make = [](Actor& E) {
            E.cast_id = 0xab; E.ai_index = 78; E.hp = E.max_hp = 500; E.atk = 70; E.def = 70;
            E.height = 1.0f; E.vert.grounded = true;
        };
        Actor E; make(E);
        Rig r; r.env.actor = &E; r.env.world = &w; r.env.character = 1;
        r.env.self = {0, 1.0f, 48.0f}; r.env.target = {0, 1.075f, 44.0f};
        // S_2080 Battle_0's order: she is placed under 130 = 0, then 156 = 1, 130 = 1, 131 = 0.
        w.g[130] = 0; w.g[131] = 1;
        for (int t = 0; t < 5; ++t) enemy_ai_tick(E.ai, ep, r.env, m850);
        check(E.ai.phase() == 0 && E.hp == 500.0f, "flag 130 == 0: a script puppet (no INIT, no brain)");
        w.g[156] = 1; w.g[130] = 1;
        enemy_ai_tick(E.ai, ep, r.env, m850);
        check(E.ai.phase() == 0, "flag 131 != 0: the plain mover only, still no brain");
        w.g[131] = 0;
        const AiOut o0 = enemy_ai_tick(E.ai, ep, r.env, m850);
        check(E.ai.phase() == 4 && E.hp == 475.0f && E.max_hp == 475.0f && std::fabs(E.def - 68.95f) < 1e-3f &&
              E.ai.cool() == 89, "INIT (231 = 0, Normal): phase 4, HP x0.95, DEF x0.985, cool 90 - 20d");
        check(o0.gauge_hp == 475.0f && o0.gauge_len == 4000.0f, "the boss gauge: length 4000 (Yunica)");
        int picked = -1, at = 0;
        for (; at < 300 && picked < 0; ++at) {
            enemy_ai_tick(E.ai, ep, r.env, m850);
            if (E.ai.i(0x72c) != 0) picked = E.ai.i(0x72c);
        }
        const int moves[] = {10, 20, 30, 40, 50, 0x3c, 0x46, 0x50, 0x5a, 0x6e, 0x78, 0x8c, 0x96, 100};
        std::printf("[ai]   M850 first pick 0x%x at tick %d\n", picked, at);
        check(std::find(std::begin(moves), std::end(moves), picked) != std::end(moves) && at >= 89 &&
              w.g[130] == 1, "after the cooldown the picker chooses a move / combo and writes 130 = 1");
        // Combo 1: the starter, then its sequencer's step 0 (the slash), then section 7.
        E.ai.i(0x72c) = 10; E.ai.i(0x7c8) = 0; E.ai.i(0x79c) = 0;
        enemy_ai_tick(E.ai, ep, r.env, m850);
        check(E.ai.i(0x79c) == 1 && E.ai.i(0x72c) == 0xb, "combo 1 starter: +0x79c = 1, on to its sequencer (0xb)");
        enemy_ai_tick(E.ai, ep, r.env, m850);
        check(E.ai.i(0x72c) == 0x50 && E.ai.i(0x7a0) == 1, "combo 1 step 0: the slash (0x50)");
        AiOut o = enemy_ai_tick(E.ai, ep, r.env, m850);
        check(o.section == 7 && o.section_force && E.ai.i(0x72c) == 0x51 && E.ai.i(0x7c4) == 1,
              "0x50 within 11u: section 7 forced, one move into the combo");
        // The jump attack 0x5a.
        E.ai.i(0x79c) = 0; E.ai.i(0x7a0) = 0; E.ai.i(0x72c) = 0x5a;
        o = enemy_ai_tick(E.ai, ep, r.env, m850);
        check(o.section == 0x139 && o.section_force && o.vm_step && o.set_marker_block == 1 && E.ai.i(0x72c) == 0x5b &&
              E.ai.f(0x7b0) == 0.275f && E.ai.f(0x7b4) == 1.5f && std::fabs(E.ai.f(0x7ac)) <= 0.2617994f,
              "0x5a: section 0x139 + a VM step, block 1; jump 0.275, push 1.5 within +-15 deg");
        r.env.section = 0x139; r.env.aso_marker = 1; r.env.aso_marker_block = 1;
        E.vert.grounded = true; E.mover.k1 = {};
        o = enemy_ai_tick(E.ai, ep, r.env, m850);
        check(!E.vert.grounded && E.vert.t == 0.275f && o.set_marker_block == 2 && o.set_own_jump == 1 &&
              std::fabs(std::hypot(E.mover.k1.x, E.mover.k1.z) - 1.5f) < 1e-4f,
              "Marker(1) on the ground: the jump (+0x330 = 0.275) and the 1.5 push; block 2");
        // The guard: section 6, a blocked hit restores the patience, then the release (0x138).
        r.env.section = 0; r.env.aso_marker = 0; r.env.aso_marker_block = -1;
        E.vert.grounded = true; E.vert.t = 0; E.mover.k1 = {};
        E.ai.i(0x72c) = 0x82;
        o = enemy_ai_tick(E.ai, ep, r.env, m850);
        check(o.section == 6 && E.ai.i(0x72c) == 0x83 && E.ai.f(0x7b8) == 60.0f, "0x82: the guard (section 6), patience 60");
        r.env.section = 6; r.env.was_hit = true;
        enemy_ai_tick(E.ai, ep, r.env, m850);
        r.env.was_hit = false;
        check(E.ai.i(0x878) == 1 && E.ai.f(0x7b8) == 30.0f, "a blocked hit (+0x3bc & 6 == 4): one block, patience 30");
        int rel = -1;
        for (int k = 0; k < 40 && rel < 0; ++k) if (enemy_ai_tick(E.ai, ep, r.env, m850).section == 0x138) rel = k;
        check(rel == 29, "the patience runs out a tick at a time: the release (section 0x138)");
        // Death: 156 and 130 clear on the 6th dead tick (0x4d1f7a); then a pinned puppet.
        E.hp = 0; w.g[156] = 1; w.g[130] = 1;
        int cleared = -1;
        for (int t = 1; t <= 8 && cleared < 0; ++t) { hk(E, w); if (w.g[156] == 0) cleared = t; }
        check(cleared == 6 && w.g[130] == 0 && E.ai.dead_t == 1 && E.ai.alpha == 1.0f && !E.ai.hittable,
              "dead: 156 and 130 clear on the 6th dead tick; the counter back to 1, no fade");
        w.g[156] = 1;
        for (int t = 0; t < 12; ++t) hk(E, w);
        check(w.g[156] == 1 && E.ai.dead_t == 1, "then (130 == 0) the dead puppet stays pinned: no clear, no removal");
        Actor C; make(C); C.hp = 0; C.ai.i(0x8a4) = 0x100;          // +0x8a5 = 1: a clone
        w.g[231] = 1; w.g[130] = 1; w.g[156] = 1;
        for (int t = 0; t < 12; ++t) hk(C, w);
        check(w.g[156] == 1, "231 = 1: a clone's death does not end the fight");
        w.g.erase(231);
        // The arena clamp (0x4d25aa): the box, when 190 == 1 and 131 == 0 only.
        const float rect[4] = {-13.437f, 32.7f, 13.587f, 55.3f};
        Vec3 c1{20, 1, 60}, c2{20, 1, 60}, c3{20, 1, 60};
        enemy_handler_clamp(78, c1, 1, true, rect);
        enemy_handler_clamp(78, c2, 1, true, rect, 1);
        enemy_handler_clamp(78, c3, 0, true, rect);
        check(c1.x == 13.587f && c1.z == 55.3f && c2.x == 20.0f && c3.x == 20.0f,
              "arena clamp: the S_2080 box when 190 == 1 and 131 == 0, else none");
        // Hugo: the bar is 3000 long.
        Actor H; make(H);
        Rig rh; rh.env.actor = &H; rh.env.world = &w; rh.env.character = 2;
        rh.env.self = {0, 1.0f, 48.0f}; rh.env.target = {0, 1.075f, 44.0f};
        w.g[130] = 1; w.g[131] = 0;
        const AiOut oh = enemy_ai_tick(H.ai, ep, rh.env, m850);
        check(oh.gauge_len == 3000.0f && H.ai.phase() == 4, "Hugo: the gauge length is 3000");
    }

    // ---- the class-4 duel opponents M830 Galleon / M810 Hugo (build/ai_spec/M830.md, M810.md) ----
    {
        // Open ground, no walls; the game flags, the player and the gauge as the host shows them.
        struct Duel : AiWorld {
            std::unordered_map<int, int> flags;
            Actor* P = nullptr;
            int gauge_hidden = 0;
            void for_each(const std::function<void(Actor&)>&) override {}
            Actor* find(int) override { return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor&) override {}
            Ray ray(const Vec3&, const Vec3&) override { return {}; }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
            void set_flag(int i, int v) override { flags[i] = v; }
            int flag(int i, int unset) override { auto it = flags.find(i); return it == flags.end() ? unset : it->second; }
            Actor* player() override { return P; }
            void hide_boss_gauge() override { ++gauge_hidden; }
        };
        XorShift rng;
        auto env_for = [&](Actor& A, Duel& w, Vec3 target, int section) {
            AiEnv e;
            e.actor = &A; e.world = &w;
            e.self = {A.pos.x, A.pos.y + A.height, A.pos.z};
            e.height = A.height; e.radius = A.radius;
            e.target = target; e.target_floor = 0;
            e.hp = A.hp; e.max_hp = A.max_hp; e.section = section;
            e.rng = [&] { return rng(); };
            return e;
        };
        const EnemyBrain m830 = enemy_brain_for(76), m810 = enemy_brain_for(74);
        check(m830 && m810 && enemy_death_hook_for(76) && enemy_death_hook_for(74),
              "M830 / M810 brains and death hooks registered (AI 76 / 74)");
        const EnemyAiParams gp = params(0.08f, 0.05f, 1.0f, 2.0f, 1.0f);

        // ---- M830 Galleon
        {   // g_flags[130] == 0: a script puppet — no INIT, no action player, HP floored at 1.
            Duel w; w.flags[130] = 0;
            Actor G; G.instance_id = 197; G.cast_id = 0xa9; G.hp = G.max_hp = 400; G.kind = 0x84; G.height = 1.0f;
            AiEnv e = env_for(G, w, {3, 1.075f, 0}, 2);
            const AiOut o = enemy_ai_tick(G.ai, gp, e, m830);
            check(o.skip_action_player && G.ai.phase() == 0 && G.kind == 0x84 && o.gauge_hp < 0,
                  "M830, flag 130 = 0: puppet (no INIT, no action player, no gauge)");
            G.hp = 0.5f;
            enemy_death_hook_for(76)(G, w);
            check(G.hp == 1.0f && w.flags.count(156) == 0, "M830 puppet: HP below 1 is set back to 1, no flags touched");
        }
        {   // The duel: INIT, the gauge, then moves; three hits on the guard break it into the slam.
            Duel w; w.flags[130] = 1; w.flags[156] = 1;
            Actor Pl; Pl.hp = Pl.max_hp = 100;
            w.P = &Pl;
            Actor G; G.instance_id = 197; G.cast_id = 0xa9; G.hp = G.max_hp = 400; G.def = 20; G.kind = 0x84; G.height = 1.0f;
            G.flags12c = 0x00100050u | 0x40u;
            AiEnv e = env_for(G, w, {2.5f, 1.075f, 0}, 2);
            AiOut o = enemy_ai_tick(G.ai, gp, e, m830);
            check(G.ai.phase() == 4 && G.kind == 0x88 && (G.flags12c & 0x1090000u) == 0x1090000u &&
                  !(G.flags12c & 0x40u) && G.ai.f(0x7b0) == 20.0f,
                  "M830 INIT: phase 4, +0x60 = 0x88 (hittable monster), +0x12c |= 0x1090000 & ~0x40, DEF saved");
            check(o.gauge_hp == 400.0f && o.gauge_len == 3000.0f, "M830: the gauge appears (lenScale 3000)");
            int sec = 2, sec_t = 0, moves = 0, t_first = -1;
            for (int t = 0; t < 400; ++t) {
                AiEnv e2 = env_for(G, w, {2.5f, 1.075f, 0}, sec);
                o = enemy_ai_tick(G.ai, gp, e2, m830);
                if (o.section == 7 || o.section == 8 || o.section == 300 || o.section == 0x12d || o.section == 3) {
                    ++moves; if (t_first < 0) t_first = t;
                }
                if (o.section >= 0) { sec = o.section; sec_t = 0; }
                else if (++sec_t > 40 && sec != 2) sec = 2;        // a move's section lasts 40 ticks here
            }
            std::printf("[ai]   M830 first move @%d, %d move sections in 400 ticks\n", t_first, moves);
            check(t_first >= 0 && t_first <= 60 && moves >= 3, "M830 at 2.5 u: moves begin within 60 ticks and repeat");

            // The guard record (0x4e90c7): packed 0x006e5a03 = counter sub 0x6e, 90 ticks, 3 hits.
            EnemyAi& a = G.ai;
            a.i(0x72c) = 0x8c; a.i(0x7a0) = 0; a.i(0x7bc) = 0x006e5a03; a.i(0x7c0) = 0x101; a.i(0x79c) = 0x14;
            AiEnv eg = env_for(G, w, {2.5f, 1.075f, 0}, 2);
            o = enemy_ai_tick(a, gp, eg, m830);
            check(o.section == 6 && o.section_force && a.i(0x72c) == 0x8d && std::uint32_t(a.i(0x7a0)) == 0x596e5a03u,
                  "M830 guard: forced section 6, record [ticks 89 | sub 0x6e | reset 90 | hits 3]");
            int hits = 0;
            for (; hits < 3 && a.i(0x72c) == 0x8d; ++hits) {
                AiEnv eh = env_for(G, w, {2.5f, 1.075f, 0}, 6);
                eh.was_hit = true;
                o = enemy_ai_tick(a, gp, eh, m830);
            }
            check(hits == 3 && a.i(0x72c) == 0x6e && a.i(0x7bc) == 0x101 && o.section == 2,
                  "M830 guard: the third blocked hit breaks it (section 2) into the 0x6e slam");
        }
        {   // The defeat (0x4e98bd): HP < 1 with the player alive.
            Duel w; w.flags[130] = 1; w.flags[156] = 1;
            Actor Pl; Pl.hp = Pl.max_hp = 100;
            w.P = &Pl;
            Actor G; G.instance_id = 197; G.cast_id = 0xa9; G.hp = G.max_hp = 400; G.kind = 0x84; G.height = 1.0f;
            AiEnv e = env_for(G, w, {2.5f, 1.075f, 0}, 2);
            enemy_ai_tick(G.ai, gp, e, m830);
            G.ai.i(0x88c) = 1;                                    // the charge-up aura is up
            G.hp = 0.0f;
            enemy_death_hook_for(76)(G, w);
            check(w.flags[156] == 0 && w.flags[130] == 0 && w.flags[174] == 1 && G.hp == 1.0f && w.gauge_hidden == 1 &&
                  Pl.i_frames == 9999 && (G.flags12c & 0x40u) && G.ai.f(0x880) < -7777777.0f,
                  "M830 defeat: 156 = 0, 130 = 0, 174 = 1, HP 1, gauge hidden, player +0x734 = 9999");
            AiEnv e2 = env_for(G, w, {2.5f, 1.075f, 0}, 2);
            const AiOut o = enemy_ai_tick(G.ai, gp, e2, m830);
            bool killed = false;
            for (const AiEffect& f : o.effects) killed |= f.kill_tag == 0;
            check(o.skip_action_player && o.section == 5 && killed,
                  "M830 after the defeat: a puppet; the hook's section 5 and the aura kill ride the next tick");
            G.hp = 0.0f; w.flags[130] = 1; w.flags[156] = 1;
            enemy_death_hook_for(76)(G, w);
            check(w.flags[156] == 1 && G.hp == 0.0f, "M830: the defeat resolves once (+0x880 below -7777777)");
        }
        {   // The player lost first: HP 1.1, unhittable, nothing cleared.
            Duel w; w.flags[130] = 1; w.flags[156] = 1;
            Actor Pl; Pl.hp = 0; Pl.max_hp = 100;
            w.P = &Pl;
            Actor G; G.hp = 0.0f; G.max_hp = 400;
            enemy_death_hook_for(76)(G, w);
            check(std::fabs(G.hp - 1.1f) < 1e-6f && !G.ai.hittable && G.def == 999.0f && w.flags[156] == 1,
                  "M830 with the player dead: HP 1.1, FUN_0047aa80, the duel flags untouched");
        }
        {   // The handler-tail clamp: FUN_0047abf0 while the duel runs.
            const float rect[4] = {-12.396f, -22.525f, 12.983f, -0.7f};
            Vec3 o{20.0f, 1.0f, 5.0f};
            enemy_handler_clamp(76, o, 1, true, rect);
            Vec3 q{20.0f, 1.0f, 5.0f};
            enemy_handler_clamp(76, q, 1, false, rect);
            check(o.x == 12.983f && o.z == -0.7f && q.x == 20.0f, "M830 clamp: FUN_0047abf0 on the live path only");
        }

        // ---- M810 Hugo
        {   // Puppet, then INIT (g_flags[231] = 0, d = 0).
            Duel w; w.flags[130] = 0;
            Actor H; H.instance_id = 197; H.cast_id = 0xa7; H.hp = H.max_hp = 1000; H.atk = 40; H.def = 30;
            H.kind = 0x84; H.height = 1.0f; H.flags12c = 0x01180050u;
            AiEnv e = env_for(H, w, {7, 1.075f, 0}, 2);
            AiOut o = enemy_ai_tick(H.ai, gp, e, m810);
            check(o.skip_action_player && H.ai.phase() == 0, "M810, flag 130 = 0: puppet");
            w.flags[130] = 1;
            o = enemy_ai_tick(H.ai, gp, e, m810);
            check(H.ai.phase() == 4 && H.kind == 0x88 && H.flags12c == 0x1190000u && H.ai.cool() == 119 &&
                  std::fabs(H.max_hp - 1050.0f) < 1e-3f && H.hp == H.max_hp && std::fabs(H.atk - 43.5f) < 1e-5f &&
                  H.def == 34.0f && H.ai.i(0x7a4) == 320 && std::fabs(H.ai.f(0x814) - 0.05f) < 1e-7f,
                  "M810 INIT: HP x1.05, ATK - 1.5 + 5, DEF + 4, +0x12c = 0x1190000, cool 120, warp timer 320");
            check(o.gauge_hp == H.hp && o.gauge_len == 4000.0f, "M810: the gauge (lenScale 4000)");
        }
        {   // Spacing: close -> strafe (steer 5, 0x800); far -> close in (steer 4, 0x800).
            Duel w; w.flags[130] = 1;
            Actor H; H.instance_id = 197; H.cast_id = 0xa7; H.hp = H.max_hp = 1000; H.height = 1.0f;
            AiEnv e = env_for(H, w, {4, 1.075f, 0}, 2);
            enemy_ai_tick(H.ai, gp, e, m810);
            enemy_ai_tick(H.ai, gp, e, m810);
            check(H.ai.steer() == 5 && H.ai.action() == 0x800, "M810 at 4 u: strafes away (steer 5, action 0x800)");
            AiEnv f = env_for(H, w, {12, 1.075f, 0}, 2);
            enemy_ai_tick(H.ai, gp, f, m810);
            check(H.ai.steer() == 4 && H.ai.action() == 0x800, "M810 at 12 u: closes in (steer 4, action 0x800)");
        }
        {   // The volley (sub 100): a 0xc6 kind-0x1b shot on Marker(1) with block 1.
            Duel w; w.flags[130] = 1;
            Actor H; H.instance_id = 197; H.cast_id = 0xa7; H.hp = H.max_hp = 1000; H.height = 1.0f;
            AiEnv e = env_for(H, w, {7, 1.075f, 0}, 2);
            enemy_ai_tick(H.ai, gp, e, m810);
            H.ai.i(0x728) = 100; H.ai.i(0x7a4) = 300;
            AiEnv v = env_for(H, w, {7, 1.075f, 0}, 0x12e);
            v.aso_marker = 1; v.aso_marker_block = 1;
            AiOut o = enemy_ai_tick(H.ai, gp, v, m810);
            check(o.bullets.size() == 1 && o.bullets[0].kind == 0x1b && o.bullets[0].effect == 0xc6 &&
                  std::fabs(o.bullets[0].speed - 0.225f) < 1e-7f && o.set_marker_block == 2,
                  "M810 volley: Marker(1) fires one 0xc6 (kind 0x1b, 0.225/tick at d 0), block -> 2");
            v.section = 2;                                        // the section ended
            o = enemy_ai_tick(H.ai, gp, v, m810);
            check(H.ai.i(0x728) == 0 && H.ai.cool() == 179, "M810 volley over: sub 0, cooldown 180 - 25d");
            // The fan (sub 200) on Marker(1): one homing 0xc7 above half HP, two below.
            H.ai.i(0x728) = 200;
            AiEnv fe = env_for(H, w, {7, 1.075f, 0}, 0x112);
            fe.aso_marker = 1;
            o = enemy_ai_tick(H.ai, gp, fe, m810);
            check(o.bullets.size() == 1 && o.bullets[0].kind == 4 && o.bullets[0].effect == 0xc7 &&
                  o.bullets[0].home_period == 1 && o.set_marker == 2, "M810 fan: Marker(1) fires one homing 0xc7 (kind 4)");
            H.hp = 400;
            H.ai.i(0x728) = 200;
            o = enemy_ai_tick(H.ai, gp, fe, m810);
            check(o.bullets.size() == 2, "M810 fan below half HP: two shots");
        }
        {   // Hit too often -> the barrier (section 314); Marker(1) raises it: EF2 0x86, 240 ticks
            // of the guard byte; a blocked hit takes 15 (d 0) off it.
            Duel w; w.flags[130] = 1;
            Actor H; H.instance_id = 197; H.cast_id = 0xa7; H.hp = H.max_hp = 1000; H.height = 1.0f;
            AiEnv e = env_for(H, w, {7, 1.075f, 0}, 2);
            enemy_ai_tick(H.ai, gp, e, m810);
            H.ai.i(0x7a4) = 300;
            H.ai.f(0x7ac) = 5.0f;
            enemy_ai_tick(H.ai, gp, e, m810);
            check(H.ai.i(0x728) == 500, "M810: +0x7ac > 4 - d/2 forces the barrier (sub 500)");
            AiOut o = enemy_ai_tick(H.ai, gp, e, m810);
            check(o.section == 0x13a && o.section_force && o.vm_step, "M810 barrier: section 314 forced, one extra VM step");
            AiEnv b = env_for(H, w, {7, 1.075f, 0}, 0x13a);
            b.aso_marker = 1;
            o = enemy_ai_tick(H.ai, gp, b, m810);
            bool bar = false;
            for (const AiEffect& f : o.effects) bar |= f.id == 0x86 && f.tag == 0 && f.attach && f.power == -5.0f;
            check(bar && H.ai.i(0x79c) == 240, "M810 barrier: EF2 0x86 (power -5, rtype 2) owned, 240 ticks");
            b.aso_marker = 0;
            enemy_ai_tick(H.ai, gp, b, m810);
            check(H.guard399 && H.ai.i(0x79c) == 239, "M810 barrier: the guard byte +0x399 is up");
            b.was_hit = true; b.took_damage = false;
            enemy_ai_tick(H.ai, gp, b, m810);
            check(H.ai.i(0x79c) == 239 - 15 - 1, "M810 barrier: a blocked hit takes 15 ticks off (d 0)");
        }
        {   // The defeat (0x4da5fe).
            Duel w; w.flags[130] = 1; w.flags[156] = 1;
            Actor Pl; Pl.hp = Pl.max_hp = 100;
            w.P = &Pl;
            Actor H; H.instance_id = 197; H.cast_id = 0xa7; H.hp = H.max_hp = 1000; H.height = 1.0f;
            AiEnv e = env_for(H, w, {7, 1.075f, 0}, 2);
            enemy_ai_tick(H.ai, gp, e, m810);
            H.ai.i(0x88c) = 1; H.ai.i(0x7bc) = 2;                 // the barrier and the aura are up
            H.hp = -3.0f;
            enemy_death_hook_for(74)(H, w);
            check(w.flags[156] == 0 && w.flags[130] == 0 && w.flags[174] == 1 && H.hp == 1.0f && !H.ai.hittable &&
                  w.gauge_hidden == 1 && Pl.i_frames == 9999,
                  "M810 defeat: 156 = 0, 130 = 0, 174 = 1, HP 1, unhittable, gauge hidden");
            AiEnv e2 = env_for(H, w, {7, 1.075f, 0}, 2);
            const AiOut o = enemy_ai_tick(H.ai, gp, e2, m810);
            int kills = 0;
            for (const AiEffect& f : o.effects) kills += f.kill_tag == 0 || f.kill_tag == 1;
            check(o.skip_action_player && kills == 2, "M810 after the defeat: a puppet; the barrier and aura are killed");
            Duel w2; w2.flags[130] = 0;
            Actor H2; H2.hp = 0.0f;
            enemy_death_hook_for(74)(H2, w2);
            check(H2.hp == 0.0f && w2.flags.count(156) == 0, "M810 puppet: no dead branch at all");
        }
        {   // The handler-tail clamp (0x4daa9d): mode-blind, 0.01 inside the violated edge.
            const float rect[4] = {-13.437f, 32.7f, 13.587f, 55.3f};
            Vec3 o{-20.0f, 1.0f, 60.0f}, in{0.0f, 1.0f, 40.0f}, off{-20.0f, 1.0f, 60.0f};
            enemy_handler_clamp(74, o, 0, true, rect);
            enemy_handler_clamp(74, in, 0, true, rect);
            enemy_handler_clamp(74, off, 0, false, rect);
            check(o.x == -13.437f + 0.009999997913837433f && o.z == 55.3f - 0.009999997913837433f &&
                  in.x == 0.0f && in.z == 40.0f && off.x == -20.0f,
                  "M810 clamp: mode-blind, 0.01 inset, live path only");
        }
    }

    // ---- the S_3080 duel opponents M820 Toal / M840 Shion / M860 Kishgal (build/ai_spec/M820.md,
    // M840.md, M860.md): the handler gates, INIT, the gauge, the end resolution and the clamp ----
    {
        // Open ground; the game flags, the player, the gauge and the current room as the host shows them.
        struct Duel3080 : AiWorld {
            std::unordered_map<int, int> flags;
            Actor* P = nullptr;
            int gauge_hidden = 0;
            bool s3080 = true;
            void for_each(const std::function<void(Actor&)>&) override {}
            Actor* find(int) override { return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor&) override {}
            Ray ray(const Vec3&, const Vec3&) override { return {}; }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
            void set_flag(int i, int v) override { flags[i] = v; }
            int flag(int i, int unset) override { auto it = flags.find(i); return it == flags.end() ? unset : it->second; }
            Actor* player() override { return P; }
            void hide_boss_gauge() override { ++gauge_hidden; }
            bool in_room(const char* dir) override { return s3080 && std::strcmp(dir, "s_30\\s_3080\\") == 0; }
            std::vector<Fx> fx;                         // the live GoEffect pool (the effect scans)
            void for_each_effect(const std::function<void(const Fx&)>& f) override { for (const Fx& e : fx) f(e); }
        };
        XorShift rng;
        auto env_for = [&](Actor& A, Duel3080& w, int character, int section) {
            AiEnv e;
            e.actor = &A; e.world = &w; e.character = character;
            e.self = {A.pos.x, A.pos.y + A.height, A.pos.z};
            e.height = A.height; e.radius = A.radius;
            e.target = {3.0f, 1.075f, 0.0f}; e.target_floor = 0;
            e.hp = A.hp; e.max_hp = A.max_hp; e.section = section;
            e.rng = [&] { return rng(); };
            return e;
        };
        auto make_duelist = [](Actor& A, int cast, int ai_index) {
            A.instance_id = 198; A.cast_id = cast; A.ai_index = ai_index;
            A.hp = A.max_hp = 300; A.atk = 40; A.def = 30; A.exp_reward = 100;
            A.kind = 0x84; A.height = 1.0f; A.flags12c = 0x01180050u | 0xc000u;
        };
        auto killed = [](const AiOut& o, int tag) {
            for (const AiEffect& f : o.effects) if (f.kill_tag == tag) return true;
            return false;
        };
        const EnemyAiParams gp = params(0.08f, 0.05f, 1.0f, 2.0f, 1.0f);
        check(enemy_brain_for(75) && enemy_brain_for(77) && enemy_brain_for(79) && enemy_death_hook_for(75) &&
              enemy_death_hook_for(77) && enemy_death_hook_for(79) &&
              enemy_brain_or_standin(79) == enemy_brain_for(79),
              "M820 / M840 / M860 brains and death hooks registered (AI 75 / 77 / 79)");

        for (const int idx : {75, 77, 79}) {
            const EnemyBrain b = enemy_brain_for(idx);
            const int cast = idx == 75 ? 0xa8 : idx == 77 ? 0xaa : 0xac;
            {   // g_flags[130] == 0: a script puppet (no INIT, no action player), HP floored at 1.
                Duel3080 w; w.flags[130] = 0;
                Actor A; make_duelist(A, cast, idx);
                const AiOut o = enemy_ai_tick(A.ai, gp, env_for(A, w, 1, 2), b);
                bool ok = o.skip_action_player && A.ai.phase() == 0 && A.kind == 0x84 && o.gauge_hp < 0;
                A.hp = 0.5f;
                enemy_death_hook_for(idx)(A, w);
                ok = ok && A.hp == 1.0f && w.flags.count(156) == 0 && w.flags.count(174) == 0;
                char msg[96];
                std::snprintf(msg, sizeof msg, "AI %d, flag 130 = 0: puppet; the dead branch only floors HP at 1", idx);
                check(ok, msg);
            }
            {   // g_flags[131] != 0 after INIT: frozen (FUN_0045f470 only) but the gauge still follows HP.
                Duel3080 w; w.flags[130] = 1; w.flags[131] = 0; w.flags[156] = 1;
                Actor A; make_duelist(A, cast, idx);
                enemy_ai_tick(A.ai, gp, env_for(A, w, 1, 2), b);
                EnemyAi before = A.ai;
                w.flags[131] = 1;
                A.hp = 290;
                const AiOut o = enemy_ai_tick(A.ai, gp, env_for(A, w, 1, 2), b);
                const bool ok = o.skip_action_player && o.section < 0 && A.ai.i(0x72c) == before.i(0x72c) &&
                                A.ai.dur() == before.dur() && A.ai.mode() == 0 && o.gauge_hp == 290.0f;
                char msg[96];
                std::snprintf(msg, sizeof msg, "AI %d, flag 131 = 1: frozen, no brain; the gauge follows HP", idx);
                check(ok, msg);
            }
            {   // The handler-tail clamp (FUN_0047abf0) on the live path only.
                const float rect[4] = {-26.637f, 36.834f, 0.783f, 58.657f};   // Battle_0's EnclosePlayer
                Vec3 o{5.0f, 1.0f, 70.0f}, q{5.0f, 1.0f, 70.0f};
                enemy_handler_clamp(idx, o, 1, true, rect);
                enemy_handler_clamp(idx, q, 1, false, rect);
                char msg[96];
                std::snprintf(msg, sizeof msg, "AI %d clamp: FUN_0047abf0 while the duel runs, not after", idx);
                check(o.x == 0.783f && o.z == 58.657f && q.x == 5.0f && q.z == 70.0f, msg);
            }
            {   // The player lost first: HP 1.1, unhittable, DEF 999, the duel flags untouched.
                Duel3080 w; w.flags[130] = 1; w.flags[156] = 1;
                Actor Pl; Pl.hp = 0; Pl.max_hp = 100;
                w.P = &Pl;
                Actor A; make_duelist(A, cast, idx);
                A.hp = 0.0f;
                enemy_death_hook_for(idx)(A, w);
                char msg[96];
                std::snprintf(msg, sizeof msg, "AI %d with the player dead: HP 1.1, FUN_0047aa80, flags untouched", idx);
                check(std::fabs(A.hp - 1.1f) < 1e-6f && !A.ai.hittable && A.def == 999.0f && w.flags[156] == 1 &&
                      w.flags.count(174) == 0, msg);
            }
        }

        // ---- M820 Toal
        {   // INIT (0x4dfc98): +0x7b0 = DEF, +0x7b4 = the run multiplier, then +0x820 = 5; dur 30.
            Duel3080 w; w.flags[130] = 1; w.flags[156] = 1;
            Actor T; make_duelist(T, 0xa8, 75);
            const AiOut o = enemy_ai_tick(T.ai, gp, env_for(T, w, 2, 2), enemy_brain_for(75));
            check(T.ai.phase() == 4 && T.kind == 0x88 && T.flags12c == 0x1090000u && (T.flags130 & 1u) &&
                  T.ai.f(0x7b0) == 30.0f && T.ai.f(0x7b4) == 2.0f && T.ai.f(0x820) == 5.0f &&
                  T.ai.f(0x7ac) == 0.08f && T.exp_reward == 100.0f && o.effects.empty(),
                  "M820 INIT: phase 4, +0x60 0x88, +0x12c = 0x1090000, DEF / run saved, +0x820 = 5");
            check(o.gauge_hp == 300.0f && o.gauge_len == 7500.0f, "M820: the gauge (lenScale 7500)");
            Duel3080 w2; w2.flags[130] = 1; w2.flags[179] = 1;
            Actor T2; make_duelist(T2, 0xa8, 75);
            const AiOut o2 = enemy_ai_tick(T2.ai, gp, env_for(T2, w2, 2, 2), enemy_brain_for(75));
            bool aura = false;
            for (const AiEffect& f : o2.effects) aura |= f.id == 0xab && f.attach && f.tag == 0;
            check(aura && T2.exp_reward == 16100.0f && o2.gauge_len == 8500.0f,
                  "M820 INIT with flag 179: the EF2 0xab aura, EXP + 16000, lenScale 8500");
        }
        {   // The defeat (0x4e2c8b): HP < 1 with the player alive -> Battle_Win (174 = 1).
            Duel3080 w; w.flags[130] = 1; w.flags[156] = 1; w.flags[133] = 1; w.flags[174] = 0;
            Actor Pl; Pl.hp = Pl.max_hp = 100;
            w.P = &Pl;
            Actor T; make_duelist(T, 0xa8, 75);
            enemy_ai_tick(T.ai, gp, env_for(T, w, 2, 2), enemy_brain_for(75));
            T.def = 55.0f; T.ai.f(0x814) = 0.5f; T.ai.f(0x820) = 9.0f; T.ai.alpha = 0.3f;
            T.hp = 0.0f;
            enemy_death_hook_for(75)(T, w);
            check(w.flags[156] == 0 && w.flags[130] == 0 && w.flags[174] == 1 && w.flags[133] == 1 && T.hp == 1.0f &&
                  w.gauge_hidden == 1 && Pl.i_frames == 9999 && T.def == 30.0f && T.ai.f(0x814) == 0.08f &&
                  T.ai.f(0x820) == 2.0f && T.ai.alpha == 1.0f && T.ai.f(0x880) < -7777777.0f,
                  "M820 defeat: 156 = 130 = 0, 174 = 1, HP 1, DEF / turn / run restored, alpha 1");
            const AiOut o = enemy_ai_tick(T.ai, gp, env_for(T, w, 2, 2), enemy_brain_for(75));
            check(o.skip_action_player && o.section == 5 && killed(o, 0),
                  "M820 after the defeat: a puppet; section 5 and the attached effects' kill next tick");
            T.hp = 0.0f; w.flags[130] = 1; w.flags[156] = 1;
            enemy_death_hook_for(75)(T, w);
            check(w.flags[156] == 1 && T.hp == 0.0f, "M820: the defeat resolves once (+0x880 below -7777777)");
        }

        // ---- M840 Shion
        {   // INIT (0x4dd043): +0x7ac = DEF; ATK / DEF - 2 below normal difficulty; no dur write.
            Duel3080 w; w.flags[130] = 1; w.flags[156] = 1;
            Actor S; make_duelist(S, 0xaa, 77);
            const AiOut o = enemy_ai_tick(S.ai, gp, env_for(S, w, 3, 2), enemy_brain_for(77));
            check(S.ai.phase() == 4 && S.kind == 0x88 && S.flags12c == 0x1090000u && S.ai.f(0x7ac) == 30.0f &&
                  S.atk == 40.0f && S.def == 30.0f && S.ai.i(0x88c) == 0,
                  "M840 INIT: phase 4, +0x60 0x88, +0x12c = 0x1090000, +0x7ac = DEF");
            check(o.gauge_hp == 300.0f && o.gauge_len == 5000.0f, "M840: the gauge (lenScale 5000)");
            Actor S2; make_duelist(S2, 0xaa, 77);
            AiEnv e2 = env_for(S2, w, 3, 2);
            e2.difficulty = -1;
            enemy_ai_tick(S2.ai, gp, e2, enemy_brain_for(77));
            check(S2.atk == 38.0f && S2.def == 28.0f && S2.ai.f(0x7ac) == 30.0f,
                  "M840 INIT on easy: ATK - 2, DEF - 2 (DAT_006bd580), +0x7ac keeps the old DEF");
        }
        {   // The defeat (0x4df4c9): the +0x88c effect dies, the hover stops, he falls.
            Duel3080 w; w.flags[130] = 1; w.flags[156] = 1;
            Actor Pl; Pl.hp = Pl.max_hp = 100;
            w.P = &Pl;
            Actor S; make_duelist(S, 0xaa, 77);
            S.flyer = true;
            enemy_ai_tick(S.ai, gp, env_for(S, w, 3, 2), enemy_brain_for(77));
            S.ai.i(0x88c) = 1; S.ai.f(0x840) = 0.2f; S.ai.f(0x844) = 3.0f;
            S.hp = -2.0f;
            enemy_death_hook_for(77)(S, w);
            check(w.flags[156] == 0 && w.flags[130] == 0 && w.flags[174] == 1 && S.hp == 1.0f && w.gauge_hidden == 1 &&
                  Pl.i_frames == 9999 && !S.flyer && S.ai.f(0x840) == 0.0f && S.ai.f(0x844) == 0.0f &&
                  S.ai.i(0x88c) == 0 && S.ai.action() == 2 && S.ai.steer() == 0,
                  "M840 defeat: 156 = 130 = 0, 174 = 1, HP 1, bob 0, flyer byte 0");
            const AiOut o = enemy_ai_tick(S.ai, gp, env_for(S, w, 3, 2), enemy_brain_for(77));
            check(o.skip_action_player && killed(o, 0) && o.section < 0,
                  "M840 after the defeat: a puppet; the +0x88c effect is killed, no section request");
        }

        // ---- M860 Kishgal
        {   // INIT in S_3080 as Yunica (d 0): ATK floored at the player's DEF + 15, no flag 179, no aura.
            Duel3080 w; w.flags[130] = 1; w.flags[156] = 1;
            Actor Pl; Pl.hp = Pl.max_hp = 100; Pl.def = 50;
            w.P = &Pl;
            Actor K; make_duelist(K, 0xac, 79);
            const AiOut o = enemy_ai_tick(K.ai, gp, env_for(K, w, 1, 2), enemy_brain_for(79));
            check(K.ai.phase() == 4 && K.kind == 0x88 && K.flags12c == 0x1090000u && K.atk == 65.0f &&
                  K.def == 30.0f && K.ai.f(0x7d8) == 30.0f && std::fabs(K.resist[1] - 0.45f) < 1e-7f &&
                  K.exp_reward == 100.0f && w.flags.count(179) == 0 && o.effects.empty(),
                  "M860 INIT in S_3080 (Yunica): ATK = max(ATK, P.DEF + 15), resist 0.45, no 179");
            check(o.gauge_hp == 300.0f && o.gauge_len == 7500.0f, "M860 in S_3080: the gauge (lenScale 7500)");
            Actor K2; make_duelist(K2, 0xac, 79);
            AiEnv ve = env_for(K2, w, 1, 2);
            ve.difficulty = -2;
            enemy_ai_tick(K2.ai, gp, ve, enemy_brain_for(79));
            check(K2.atk == 65.0f + 12.0f + 5.0f && K2.def == 30.0f + 3.0f + 1.0f,
                  "M860 INIT in S_3080 on very easy: ATK + 12 + 5, DEF + 3 + 1");
        }
        {   // INIT elsewhere as Yunica: flag 179 = 1, the EF2 0xab aura, EXP + 7500, lenScale 8500.
            Duel3080 w; w.s3080 = false; w.flags[130] = 1;
            Actor K; make_duelist(K, 0xac, 79);
            const AiOut o = enemy_ai_tick(K.ai, gp, env_for(K, w, 1, 2), enemy_brain_for(79));
            bool aura = false;
            for (const AiEffect& f : o.effects) aura |= f.id == 0xab && f.attach && f.tag == 0;
            check(w.flags[179] == 1 && aura && K.atk == 40.0f && K.exp_reward == 7600.0f && o.gauge_len == 8500.0f,
                  "M860 INIT outside S_3080: 179 = 1, the 0xab aura, EXP + 7500, lenScale 8500");
            Duel3080 wh; wh.s3080 = false; wh.flags[130] = 1;
            Actor KH; make_duelist(KH, 0xac, 79);
            AiEnv he = env_for(KH, wh, 2, 2);
            he.difficulty = 2;
            const AiOut oh = enemy_ai_tick(KH.ai, gp, he, enemy_brain_for(79));
            check(wh.flags.count(179) == 0 && oh.effects.empty() && KH.atk == 38.5f &&
                  std::fabs(KH.resist[1] - 0.63f) < 1e-7f && KH.def == 50.0f && oh.gauge_len == 7500.0f,
                  "M860 INIT vs Hugo (d 2): no 179, ATK - 1.5, resist 0.63, DEF + 20");
        }
        {   // The S_3080 story duel ends at half HP (0x4e7fc6): HP pinned, 156 = 0, 174 = 1, 130 kept.
            Duel3080 w; w.flags[130] = 1; w.flags[156] = 1; w.flags[174] = 0;
            Actor Pl; Pl.hp = Pl.max_hp = 100;
            w.P = &Pl;
            Actor K; make_duelist(K, 0xac, 79);
            enemy_ai_tick(K.ai, gp, env_for(K, w, 1, 2), enemy_brain_for(79));
            K.hp = 151.0f;
            AiOut o = enemy_ai_tick(K.ai, gp, env_for(K, w, 1, 2), enemy_brain_for(79));
            check(K.hp == 151.0f && w.flags[156] == 1 && w.flags[174] == 0, "M860 in S_3080 above half HP: the duel goes on");
            K.hp = 120.0f;
            o = enemy_ai_tick(K.ai, gp, env_for(K, w, 1, 2), enemy_brain_for(79));
            check(K.hp == 150.0f && w.flags[156] == 0 && w.flags[174] == 1 && w.flags[130] == 1 && o.gauge_hp == 120.0f,
                  "M860 in S_3080 below half HP: HP = max / 2, 156 = 0, 174 = 1, 130 left to the script");
            // Outside S_3080 the same hit does not end it.
            Duel3080 wo; wo.s3080 = false; wo.flags[130] = 1; wo.flags[156] = 1; wo.flags[174] = 0;
            Actor K2; make_duelist(K2, 0xac, 79);
            enemy_ai_tick(K2.ai, gp, env_for(K2, wo, 1, 2), enemy_brain_for(79));
            K2.hp = 120.0f;
            enemy_ai_tick(K2.ai, gp, env_for(K2, wo, 1, 2), enemy_brain_for(79));
            check(K2.hp == 120.0f && wo.flags[156] == 1 && wo.flags[174] == 0, "M860 outside S_3080: no half-HP end");
        }
        {   // The KO (0x4e7c07) in S_3080: 174 = 1, 156 = 130 = 0, then the half-HP rule lifts HP 1 to max / 2.
            Duel3080 w; w.flags[130] = 1; w.flags[156] = 1; w.flags[133] = 1;
            Actor Pl; Pl.hp = Pl.max_hp = 100;
            w.P = &Pl;
            Actor K; make_duelist(K, 0xac, 79);
            enemy_ai_tick(K.ai, gp, env_for(K, w, 1, 2), enemy_brain_for(79));
            K.hp = -10.0f;
            enemy_death_hook_for(79)(K, w);
            check(w.flags[174] == 1 && w.flags[156] == 0 && w.flags[130] == 0 && K.hp == 150.0f && w.gauge_hidden == 1 &&
                  Pl.i_frames == 9999 && K.ai.action() == 2 && K.ai.f(0x880) < -7777777.0f,
                  "M860 KO in S_3080: 174 = 1, 156 = 130 = 0, gauge hidden, HP 1 -> max / 2");
            const AiOut o = enemy_ai_tick(K.ai, gp, env_for(K, w, 1, 2), enemy_brain_for(79));
            check(o.skip_action_player && killed(o, 0) && killed(o, 1),
                  "M860 after the KO: a puppet; every EF2 0x122 and 0xab is killed");
            // Outside S_3080 the KO leaves him at 1 HP.
            Duel3080 wo; wo.s3080 = false; wo.flags[130] = 1; wo.flags[156] = 1;
            wo.P = &Pl;
            Actor K2; make_duelist(K2, 0xac, 79);
            enemy_ai_tick(K2.ai, gp, env_for(K2, wo, 1, 2), enemy_brain_for(79));
            K2.hp = 0.0f;
            enemy_death_hook_for(79)(K2, wo);
            check(wo.flags[174] == 1 && wo.flags[156] == 0 && wo.flags[130] == 0 && K2.hp == 1.0f,
                  "M860 KO outside S_3080: 174 = 1, 156 = 130 = 0, HP 1");
        }
        {   // The effect-pool scan (0x4e33d8): a live EF2 0x90 (a player effect) sends the decide (sub 0x14)
            // to the guard 0xb4 with +0x7bc = 0xc85a09, +0x7c0 = 2 (0x4e403d); without it, not. The
            // 0x40th EF2 0x122 ends the scan before a later 0x90 is seen (0x4e3475).
            auto decide = [&](std::vector<AiWorld::Fx> fx) {
                Duel3080 w; w.flags[130] = 1; w.flags[156] = 1;
                Actor Pl; Pl.hp = Pl.max_hp = 100;
                w.P = &Pl;
                Actor K; make_duelist(K, 0xac, 79);
                enemy_ai_tick(K.ai, gp, env_for(K, w, 1, 2), enemy_brain_for(79));
                w.fx = std::move(fx);
                K.ai.i(0x72c) = 0x14; K.ai.i(0x730) = 0; K.ai.i(0x7c4) = 0; K.ai.dur() = 0;
                enemy_ai_tick(K.ai, gp, env_for(K, w, 1, 2), enemy_brain_for(79));
                return std::make_pair(K.ai.i(0x7bc), K.ai.i(0x7c0));
            };
            AiWorld::Fx f90; f90.id = 0x90;
            AiWorld::Fx f7d; f7d.id = 0x7d;
            AiWorld::Fx f122; f122.id = 0x122;
            const auto with = decide({f7d, f90}), without = decide({f7d});
            std::vector<AiWorld::Fx> capped(0x40, f122);
            capped.push_back(f90);
            const auto cap = decide(capped);
            check(with.first == 0x00c85a09 && with.second == 2 && without.first != 0x00c85a09 && cap.first != 0x00c85a09,
                  "M860 decide: a live EF2 0x90 -> the guard (0xc85a09, 2); none, or past 0x40 0x122s -> not");
        }
        {   // The puppet in S_3080 kills every live EF2 0x122 each tick (0x4e7b31); elsewhere it does not.
            Duel3080 w; w.flags[130] = 0;
            Actor K; make_duelist(K, 0xac, 79);
            const AiOut o = enemy_ai_tick(K.ai, gp, env_for(K, w, 1, 2), enemy_brain_for(79));
            Duel3080 wo; wo.s3080 = false; wo.flags[130] = 0;
            Actor K2; make_duelist(K2, 0xac, 79);
            const AiOut o2 = enemy_ai_tick(K2.ai, gp, env_for(K2, wo, 1, 2), enemy_brain_for(79));
            check(o.skip_action_player && killed(o, 1) && !killed(o, 0) && o2.effects.empty(),
                  "M860 puppet: kills the EF2 0x122 in S_3080 only");
        }
        {   // The pillar break and the freeze (0x4e3540..0x4e3bae; build/mech_re/KISHGAL_SHIELD.md §3, §4).
            struct Ice : Duel3080 {
                int breaks = 0, n_ids = 0, shield = 0, impact = 0, se = 0;
                bool toal = false;
                int break_shields(Actor&, int s, const int* ids, int n, int imp, int snd) override {
                    ++breaks; shield = s; n_ids = n; impact = imp; se = snd;
                    int t = 0;
                    for (int i = 0; i < n; ++i) t += ids[i] == 162 || ids[i] == 175 || ids[i] == 179 || ids[i] == 180;
                    toal = t == 4;
                    return 0;
                }
            };
            Ice w; w.flags[130] = 1; w.flags[156] = 1; w.s3080 = false;
            Actor Pl; Pl.hp = Pl.max_hp = 100;
            w.P = &Pl;
            Actor K; make_duelist(K, 0xac, 79);
            const EnemyBrain b = enemy_brain_for(79);
            enemy_ai_tick(K.ai, gp, env_for(K, w, 3, 2), b);                 // INIT
            check(w.breaks == 1 && w.shield == 0x122 && w.n_ids == 13 && w.impact == 0x123 && w.se == 424 && w.toal,
                  "M860: the pillar break runs every live tick (0x122, 13 ids with Toal's 162/175/179/180, 0x123, SE 424)");
            auto tick = [&](int psec, bool durable, bool alive = true) {
                AiEnv e = env_for(K, w, 3, 2);
                e.target = {3.0f, 1.075f, 2.0f}; e.target_section = psec; e.target_durable = durable;
                e.target_alive = alive; e.target_heading = 0.0f;
                return enemy_ai_tick(K.ai, gp, e, b);
            };
            AiWorld::Fx f123; f123.id = 0x123;
            AiWorld::Fx marked = f123; marked.attach = 0xff;
            // No freeze: a 0x123 the break spawned (0xff); the player in section 9; a Durable player.
            w.fx = {marked};
            AiOut o = tick(4, false);
            bool none = !o.hold_target && o.target_section < 0 && K.ai.f(0x7d4) == 0.0f;
            w.fx = {f123};
            o = tick(9, false);
            none = none && !o.hold_target && K.ai.f(0x7d4) == 0.0f;
            o = tick(4, true);
            none = none && !o.hold_target && K.ai.f(0x7d4) == 0.0f;
            check(none, "M860 freeze: not from a marked 0x123, not in section 9, not on a Durable player");
            // The start (section 0xb): EF2 0x11e x2 at the target, section 9 asked, the pin point, and the hold at once.
            o = tick(0xb, false);
            bool ice = false, mv = false;
            for (const AiEffect& f : o.effects) {
                ice |= f.id == 0x11e && f.scale == 2.0f && f.tag == 2 && f.pos.x == 3.0f && f.pos.z == 2.0f && f.power == 0.0f;
                mv |= f.move_tag == 2 && f.move_scale == 2.5f && f.pos.x == 2.5f && f.pos.z == 2.0f;
            }
            check(ice && mv && o.target_section == 9 && o.hold_target && o.hold_xz_only && o.hold_origin.x == 3.0f &&
                  o.hold_origin.z == 2.0f && o.target_wait_add == 1 && K.ai.f(0x7d4) == 19.0f,
                  "M860 freeze start: EF2 0x11e x2, section 9, pinned at the target's x / z, the block at x - cos * 0.5, 20 -> 19");
            // 19 more held ticks (the player now in section 9: no restart), then free; a Durable / dead tick skips the pin.
            int held = 1, restarts = 0;
            for (int i = 0; i < 25; ++i) {
                const bool skip = i == 3 || i == 4;
                o = i == 4 ? tick(9, false, false) : tick(9, skip, true);
                held += o.hold_target ? 1 : 0;
                restarts += o.target_section >= 0;
                if (skip && o.hold_target) restarts += 100;
            }
            check(held == 18 && restarts == 0 && K.ai.f(0x7d4) == 0.0f,
                  "M860 freeze: 20 brain ticks by the timer alone; a Durable or dead tick is not pinned; no re-trigger from section 9");
        }
    }

    // ---- the duel brains' INIT re-stat, FUN_004202e0(cast, level = +0xc8) (build/audit/S6099_DUELS.md §2) ----
    {
        // A host whose restat writes the retail stats of that cast at that level (the audit's / the game's tables;
        // cleria-enemytest checks enemy_restat against the same numbers) and records the call.
        struct Stat : AiWorld {
            std::unordered_map<int, int> flags;
            Actor pl;
            bool s3080 = false;
            std::vector<std::pair<int, int>> calls;
            void for_each(const std::function<void(Actor&)>&) override {}
            Actor* find(int) override { return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor&) override {}
            Ray ray(const Vec3&, const Vec3&) override { return {}; }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
            void set_flag(int i, int v) override { flags[i] = v; }
            int flag(int i, int unset) override { auto it = flags.find(i); return it == flags.end() ? unset : it->second; }
            Actor* player() override { return &pl; }
            bool in_room(const char* dir) override { return s3080 && std::strcmp(dir, "s_30\\s_3080\\") == 0; }
            void restat(Actor& a, int cast, int level) override {
                calls.push_back({cast, level});
                struct Row { int cast, level; float hp, atk, def, exp; };
                static const Row rows[] = {
                    {172, 48, 6249, 251, 222, 14348}, {168, 48, 7431, 247, 248, 5532}, {167, 48, 3349, 217, 240, 5532},
                    {172, 19, 2985, 121, 108, 5766},  {169, 5, 592, 58, 57, 744},      {170, 19, 1492, 121, 118, 2223},
                    {171, 5, 838, 58, 57, 744},       {50, 5, 1860, 56, 43, 526},
                };
                for (const Row& r : rows)
                    if (r.cast == cast && r.level == level) {
                        a.max_hp = a.hp = r.hp; a.atk = r.atk; a.def = r.def; a.exp_reward = r.exp; a.level = level;
                    }
            }
        };
        XorShift rng;
        const EnemyAiParams gp = params(0.08f, 0.05f, 1.0f, 2.0f, 1.0f);
        // The script's Chr_SetStatus(id, level, 200, 70, 70, 5, 6, ...) has landed; then 130 = 1 lets INIT run.
        auto init = [&](Stat& w, Actor& A, int cast, int ai_index, int level, int character) {
            A.instance_id = 198; A.cast_id = cast; A.ai_index = ai_index; A.level = level;
            A.hp = A.max_hp = 200; A.atk = 70; A.def = 70; A.exp_reward = 6;
            A.kind = 0x84; A.height = 1.0f; A.vert.grounded = true;
            w.pl.hp = w.pl.max_hp = 100; w.pl.def = 10;
            w.flags[130] = 1; w.flags[131] = 0; w.flags[156] = 1;
            AiEnv e;
            e.actor = &A; e.world = &w; e.character = character;
            e.self = {A.pos.x, A.pos.y + A.height, A.pos.z};
            e.height = A.height; e.radius = A.radius;
            e.target = {3.0f, 1.075f, 0.0f}; e.target_floor = 0;
            e.hp = A.hp; e.max_hp = A.max_hp; e.section = 2;
            e.rng = [&] { return rng(); };
            enemy_ai_tick(A.ai, gp, e, enemy_brain_for(ai_index));
            return w.calls.size() == 1 && w.calls[0] == std::pair<int, int>{cast, level};
        };
        {   // M860 Kishgal in S_6099 (not S_3080), Yunica: 0x4e3cd3, then resist / EXP + 7500 under 179.
            Stat w; Actor A;
            const bool called = init(w, A, 172, 79, 48, 1);
            check(called && A.max_hp == 6249.0f && A.hp == 6249.0f && A.atk == 251.0f && A.def == 222.0f &&
                  A.exp_reward == 21848.0f && A.ai.f(0x7d8) == 222.0f && w.flags[179] == 1,
                  "M860 INIT re-stats at the script's level 48: 6249 / 251 / 222, EXP 14348 + 7500, +0x7d8 = the new DEF");
        }
        {   // M860 in S_3080 (level 19), Yunica: the ATK floor (player DEF + 15) applies to the re-stat ATK.
            Stat w; w.s3080 = true; w.pl.def = 10; Actor A;
            const bool called = init(w, A, 172, 79, 19, 1);
            check(called && A.max_hp == 2985.0f && A.atk == 121.0f && A.def == 108.0f && A.exp_reward == 5766.0f,
                  "M860 INIT in S_3080 at level 19: 2985 / 121 / 108 / 5766");
        }
        {   // M820 Toal under 179: 0x4dfcbc, first thing after FUN_004782f0; +0x7b0 reads the new DEF; EXP + 16000.
            Stat w; w.flags[179] = 1; Actor A;
            const bool called = init(w, A, 168, 75, 48, 2);
            check(called && A.max_hp == 7431.0f && A.atk == 247.0f && A.def == 248.0f && A.exp_reward == 21532.0f &&
                  A.ai.f(0x7b0) == 248.0f,
                  "M820 INIT re-stats at level 48: 7431 / 247 / 248, EXP 5532 + 16000, +0x7b0 = the new DEF");
        }
        {   // M810 Hugo under 231: 0x4d7ebd, then ATK - 1.5, HP x1.25, DEF + 19, ATK + 31, EXP x4, and the aura's + 2.5.
            Stat w; w.flags[231] = 1; Actor A;
            const bool called = init(w, A, 167, 74, 48, 3);
            check(called && A.max_hp == 4186.25f && A.hp == 4186.25f && A.atk == 249.0f && A.def == 259.0f &&
                  A.exp_reward == 22128.0f,
                  "M810 INIT re-stats at level 48, then its own scaling: 4186.25 / 249 / 259 / 22128");
        }
        {   // M830 Galleon (S_1080, level 5): 0x4e8335; +0x7b0 = the new DEF.
            Stat w; Actor A;
            const bool called = init(w, A, 169, 76, 5, 3);
            check(called && A.max_hp == 592.0f && A.atk == 58.0f && A.def == 57.0f && A.ai.f(0x7b0) == 57.0f,
                  "M830 INIT re-stats at level 5: 592 / 58 / 57, +0x7b0 = the new DEF");
        }
        {   // M840 Shion (S_3080, level 19): 0x4dd067; +0x7ac = the new DEF.
            Stat w; w.s3080 = true; Actor A;
            const bool called = init(w, A, 170, 77, 19, 3);
            check(called && A.max_hp == 1492.0f && A.atk == 121.0f && A.def == 118.0f && A.ai.f(0x7ac) == 118.0f,
                  "M840 INIT re-stats at level 19: 1492 / 121 / 118, +0x7ac = the new DEF");
        }
        {   // M850 Epona (S_1080, level 5): the handler's call 0x4d226d, then the brain's HP x0.95 / DEF x0.985.
            Stat w; Actor A;
            const bool called = init(w, A, 171, 78, 5, 2);
            check(called && A.max_hp == 838.0f * 0.95f && A.hp == A.max_hp && A.def == 57.0f * 0.985f && A.atk == 58.0f,
                  "M850 INIT re-stats at level 5 (838 / 58 / 57), then HP x0.95 = 796.1, DEF x0.985");
        }
        {   // M460 Vagullion (S_1080, level 5): 0x4c71b7, before Hugo's scaling (HP x1.05, DEF x1.05, ATK + 5).
            Stat w; Actor A;
            const bool called = init(w, A, 50, 39, 5, 1);
            check(called && A.max_hp == 1860.0f && A.atk == 56.0f && A.def == 43.0f && A.ai.f(0x7d8) == 43.0f,
                  "M460 INIT re-stats at level 5: 1860 / 56 / 43, +0x7d8 = the new DEF");
            Stat w2; Actor B;
            const bool called2 = init(w2, B, 50, 39, 5, 2);
            check(called2 && B.max_hp == 1860.0f * 1.05f && B.hp == B.max_hp && B.def == 43.0f * 1.05f && B.atk == 61.0f,
                  "M460 INIT, Hugo: the re-stat first, then HP x1.05, DEF x1.05, ATK + 5");
        }
    }

    // ---- M900 the Eyes of Fact, and M810 commanding them (build/ai_spec/M900.md, M810.md) ----
    {
        // A pool in engine order (stable addresses), the game flags, FUN_00476d70.
        struct EyeDuel : AiWorld {
            std::list<Actor> pool;
            std::unordered_map<int, int> flags;
            int next_id = 300, spawns = 0;
            void for_each(const std::function<void(Actor&)>& f) override { for (Actor& o : pool) f(o); }
            Actor* find(int id) override {
                for (Actor& o : pool) if (o.instance_id == id) return &o;
                return nullptr;
            }
            Actor* spawn_child(Actor& p, int cast, const Vec3& d, int lv, bool keep0) override {
                pool.emplace_back();
                Actor& c = pool.back();
                c.instance_id = next_id++; c.cast_id = cast; c.ai_index = 83; c.kind = 0x81; c.level = lv;
                c.hp = c.max_hp = 10; c.height = 0.5f;
                c.pos = {p.pos.x + d.x, p.pos.y + d.y, p.pos.z + d.z};
                c.yaw = p.yaw; c.ai.heading = p.ai.heading;
                c.ai.i(0x854) = p.instance_id; c.ai.phase() = keep0 ? 0 : 4;
                ++spawns;
                return &c;
            }
            void remove(Actor&) override {}
            Ray ray(const Vec3&, const Vec3&) override { return {}; }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
            void set_flag(int i, int v) override { flags[i] = v; }
            int flag(int i, int unset) override { auto it = flags.find(i); return it == flags.end() ? unset : it->second; }
        };
        XorShift rng;
        const EnemyAiParams gp = params(0.08f, 0.05f, 1.0f, 2.0f, 1.0f);
        const Vec3 P{7.0f, 1.075f, 0.0f};                          // the player's origin
        struct Tick { int section = 2, marker = 0, block = -1; Vec3 target{7.0f, 1.075f, 0.0f}; };
        // One host tick of an actor: the visible facing +0x314 = pi/2 - yaw (the view's basis).
        auto tick = [&](Actor& X, EyeDuel& w, Tick t = {}) {
            AiEnv e;
            e.actor = &X; e.world = &w;
            e.self = {X.pos.x, X.pos.y + X.height, X.pos.z};
            e.facing = 1.5707963f - X.yaw;
            e.height = X.height; e.radius = X.radius;
            e.target = t.target; e.target_floor = 0;
            e.hp = X.hp; e.max_hp = X.max_hp; e.section = t.section;
            e.aso_marker = t.marker; e.aso_marker_block = t.block;
            e.rng = [&] { return rng(); };
            const AiOut o = enemy_ai_tick(X.ai, gp, e, enemy_brain_for(X.ai_index));
            if (o.snap_facing) X.yaw = 1.5707963f - X.ai.heading;
            X.age_ticks += 1;
            return o;
        };
        auto hugo = [](EyeDuel& w) -> Actor& {
            w.pool.emplace_back();
            Actor& H = w.pool.back();
            H.instance_id = 197; H.cast_id = 0xa7; H.ai_index = 74; H.hp = H.max_hp = 1000; H.atk = 40;
            H.kind = 0x84; H.height = 1.0f; H.yaw = 1.5707963f;    // facing 0: +x
            return H;
        };
        auto origin = [](const Actor& X) { return Vec3{X.pos.x, X.pos.y + X.height, X.pos.z}; };
        check(enemy_brain_for(83) != nullptr, "M900 brain registered (AI 83)");

        {   // M810 spawns one Eye a tick up to the wanted count (2 at full HP, 231 clear).
            EyeDuel w; w.flags[130] = 1;
            Actor& H = hugo(w);
            tick(H, w);
            const bool one = w.spawns == 1 && w.pool.back().cast_id == 0xb0 && w.pool.back().ai.i(0x730) == 0 &&
                             w.pool.back().ai.i(0x854) == 197 && w.pool.back().ai.phase() == 0;
            tick(H, w);
            tick(H, w);
            check(one && w.spawns == 2 && w.pool.back().ai.i(0x730) == 1,
                  "M810: one Eye a tick (slot 0, then 1), +0x854 = Hugo, phase 0; stops at 2");
            Actor& E = w.pool.back();
            tick(E, w);
            check(E.kind == 0x08 && !E.ai.hittable && E.atk == H.atk && H.atk == 43.5f && E.ai.phase() != 0 &&
                      E.ai.i(0x720) == 2,
                  "M900 INIT: +0x60 = (0x81 & ~0x81) | 8 (not hittable), ATK = Hugo's (43.5), hover mode 2");
        }
        {   // Mode 0: pairs 1.75 u behind Hugo, mirrored about his facing; half a unit a tick, then snap.
            EyeDuel w; w.flags[130] = 1;
            Actor& H = hugo(w);
            Actor& E0 = *w.spawn_child(H, 0xb0, {-5.0f, 0.0f, 5.0f}, 1, true);
            Actor& E1 = *w.spawn_child(H, 0xb0, {0.0f, 0.0f, 0.0f}, 1, true);
            E0.ai.i(0x730) = 0; E1.ai.i(0x730) = 1;
            tick(E0, w); tick(E1, w);                              // INIT
            const Vec3 o0 = origin(E0);
            tick(E0, w); tick(E1, w);
            const Vec3 a0 = E0.ai.v(0x894), a1 = E1.ai.v(0x894), HO = origin(H);
            const float r0 = std::hypot(a0.x - HO.x, a0.z - HO.z), r1 = std::hypot(a1.x - HO.x, a1.z - HO.z);
            check(std::fabs(r0 - 1.75f) < 1e-4f && std::fabs(r1 - 1.75f) < 1e-4f && a0.x < 0 && a1.x < 0 &&
                  std::fabs(a0.z + a1.z) < 1e-5f && a0.y == HO.y,
                  "M900 mode 0: slots 0 / 1 at 1.75 u behind Hugo, mirrored about his facing");
            const Vec3 o1 = origin(E0);
            const float stepd = std::sqrt((o1.x - o0.x) * (o1.x - o0.x) + (o1.y - o0.y) * (o1.y - o0.y) +
                                          (o1.z - o0.z) * (o1.z - o0.z));
            check(std::fabs(stepd - 0.5f) < 1e-4f, "M900 mode 0: a far Eye steps 0.5 u toward its anchor");
            for (int t = 0; t < 30; ++t) tick(E0, w);
            const Vec3 o2 = origin(E0);
            check(o2.x == a0.x && o2.y == a0.y && o2.z == a0.z && E0.hp == 10, "M900 mode 0: arrives and snaps");
        }
        {   // Six or more wanted (231 set): the path bends round the player and the step is smoothed.
            EyeDuel w; w.flags[130] = 1; w.flags[231] = 1;
            Actor& H = hugo(w);
            Actor& E = *w.spawn_child(H, 0xb0, {-6.0f, 0.0f, 6.0f}, 1, true);
            tick(E, w);
            bool smoothed = false;
            int arrived = -1;
            for (int t = 0; t < 300 && arrived < 0; ++t) {
                tick(E, w);
                const Vec3 m = E.ai.v(0x7f0), an = E.ai.v(0x894), o = origin(E);
                smoothed |= m.x != 0.0f || m.z != 0.0f;
                if (o.x == an.x && o.y == an.y && o.z == an.z) arrived = t;
            }
            std::printf("[ai]   M900 with 6 wanted: arrived @%d\n", arrived);
            check(smoothed && arrived > 0 && E.hp == 10, "M900 with 6 wanted: a smoothed (+0x7f0) path, then the snap");
        }
        {   // Mode 1: the ring, r 2.25, turning with Hugo's tick counter; slot 1 opposite slot 0.
            EyeDuel w; w.flags[130] = 1;
            Actor& H = hugo(w);
            H.age_ticks = 100;
            Actor& E0 = *w.spawn_child(H, 0xb0, {0, 0, 0}, 1, true);
            Actor& E1 = *w.spawn_child(H, 0xb0, {0, 0, 0}, 1, true);
            E0.ai.i(0x730) = 0; E1.ai.i(0x730) = 1;
            tick(E0, w); tick(E1, w);
            E0.ai.i(0x72c) = 1; E1.ai.i(0x72c) = 1;
            tick(E0, w); tick(E1, w);
            const Vec3 a0 = E0.ai.v(0x894), a1 = E1.ai.v(0x894), HO = origin(H);
            const float g = 100.0f * 0.12f * 0.5f;                 // age * 0.12 * (0.2d + 0.5), d 0
            float gw = g; while (gw > 3.14159274f) gw -= 6.28318548f;
            check(std::fabs(std::hypot(a0.x - HO.x, a0.z - HO.z) - 2.25f) < 1e-4f &&
                  std::fabs(std::atan2(a0.z - HO.z, a0.x - HO.x) - gw) < 1e-4f &&
                  std::fabs(a0.x + a1.x - 2 * HO.x) < 1e-4f && std::fabs(a0.z + a1.z - 2 * HO.z) < 1e-4f,
                  "M900 mode 1: the ring (r 2.25 at Hugo's +0x504 * 0.06 rad), slot 1 opposite");
        }
        {   // Mode 3: a point 2.5..4 u round the player; on arrival it waits, and 40 ticks on it
            // fires one 0xc6 kind-0x1b shot, owned by Hugo.
            EyeDuel w; w.flags[130] = 1;
            Actor& H = hugo(w);
            Actor& E = *w.spawn_child(H, 0xb0, {0, 0, 0}, 1, true);
            tick(E, w);
            E.ai.i(0x72c) = 3;
            tick(E, w);
            const Vec3 an = E.ai.v(0x894);
            const float r = std::hypot(an.x - P.x, an.z - P.z);
            check(E.ai.i(0x79c) == 1 && r >= 2.5f && r <= 4.0f && an.y >= 1.0f && an.y <= 3.0f,
                  "M900 mode 3: a point 2.5..4 u round the player, 1..3 u above its floor");
            int arrive = -1, fired = -1;
            AiBullet b;
            for (int t = 0; t < 2000 && fired < 0; ++t) {
                const AiOut o = tick(E, w);
                if (arrive < 0 && E.ai.i(0x79c) == 2) arrive = t;
                if (!o.bullets.empty()) { fired = t; b = o.bullets[0]; }
            }
            std::printf("[ai]   M900 mode 3: arrived @%d, fired @%d\n", arrive, fired);
            check(arrive >= 0 && fired - arrive == 40 && b.kind == 0x1b && b.effect == 0xc6 && b.owner == 197 &&
                  std::fabs(b.speed - 0.225f) < 1e-7f && std::fabs(std::sqrt(b.dir.x * b.dir.x + b.dir.y * b.dir.y +
                  b.dir.z * b.dir.z) - 1.0f) < 1e-5f,
                  "M900 mode 3: one 0xc6 kind-0x1b shot (Hugo's, 0.225/tick) 40 ticks after arriving");
        }
        {   // The timed shots M810 orders (+0x7a4 / +0x7a8), and the recall (+0x7c0): EF2 0x95.
            EyeDuel w; w.flags[130] = 1;
            Actor& H = hugo(w);
            H.ai.set_v(0x734, {0.0f, 0.0f, 5.0f});                // Hugo's target lies along +z
            Actor& E = *w.spawn_child(H, 0xb0, {0, 0, 0}, 1, true);
            tick(E, w);
            E.ai.i(0x7a4) = 2; E.ai.i(0x7a8) = 1;
            AiOut o = tick(E, w);
            check(o.bullets.size() == 1 && o.bullets[0].kind == 0x1b && std::fabs(o.bullets[0].speed - 0.3f) < 1e-7f &&
                  E.ai.i(0x7a4) == 1 && E.ai.i(0x7a8) == 10 && E.ai.i(0x7a0) == 3 &&
                  std::fabs(E.ai.heading - 1.2566371f) < 1e-5f,
                  "M900 timed shot: 0xc6 kind 0x1b at 0.3, next in 10, turned 72 deg toward Hugo's target");
            E.ai.i(0x7a4) = 0; E.ai.i(0x7a8) = 0;
            E.ai.i(0x7c0) = 210;
            o = tick(E, w);
            bool f95 = false, f96 = false;
            for (const AiEffect& f : o.effects) {
                f95 |= f.id == 0x95 && f.tag == 0 && f.attach && f.power == 5.0f && f.knock == 15.0f && f.rtype == 1;
                f96 |= f.id == 0x96 && f.attach;
            }
            check(f95 && f96 && E.ai.i(0x88c) != 0 && E.ai.i(0x7c0) == 209,
                  "M900 recall: EF2 0x95 (power 5, knock 15, rtype 1) and 0x96 attached; +0x7c0 counts down");
            E.ai.i(0x7c0) = 1;
            tick(E, w);
            o = tick(E, w);
            bool killed = false;
            for (const AiEffect& f : o.effects) killed |= f.kill_tag == 0;
            check(killed && E.ai.i(0x88c) == 0, "M900 recall over: the 0x95 is killed");
        }
        {   // Mode 2, the dash: section 271, EF2 0x92; it fades, reappears before Hugo, dies at 120.
            EyeDuel w; w.flags[130] = 1;
            Actor& H = hugo(w);
            H.pos = {1.0f, 0.0f, 2.0f}; H.ai.heading = 0.5f;
            Actor& E = *w.spawn_child(H, 0xb0, {-3, 0, 0}, 1, true);
            tick(E, w);
            E.ai.i(0x72c) = 2;
            const float atk0 = E.atk;
            AiOut o = tick(E, w, Tick{2, 0, -1, {40.0f, 1.075f, 0.0f}});
            bool f92 = false;
            for (const AiEffect& f : o.effects) f92 |= f.id == 0x92 && f.power == 12.0f && f.knock == 5.0f && f.rtype == 2;
            check(o.section == 0x10f && o.section_force && f92 && E.ai.i(0x7bc) == 120,
                  "M900 dash: section 271 forced, EF2 0x92 (power 12, knock 5, rtype 2), 120 ticks");
            int back = -1, died = -1, sec5 = 0;
            Vec3 at{};
            for (int t = 1; t <= 130 && died < 0; ++t) {
                o = tick(E, w, Tick{2, 0, -1, {40.0f, 1.075f, 0.0f}});
                if (back < 0 && E.ai.i(0x79c) == 1) { back = t; at = origin(E); }
                if (E.hp == 0.0f) { died = t; sec5 = o.section; }
            }
            check(back == 5 && std::fabs(at.x - (std::cos(0.5f) + 1.0f)) < 1e-5f && at.y == 1.0f &&
                  std::fabs(at.z - (std::sin(0.5f) + 2.0f)) < 1e-5f,
                  "M900 dash: fades out (-0.25 a tick) and reappears 1 u in front of Hugo");
            check(died == 120 && sec5 == 5 && E.atk == atk0 - 12.5f && (E.flags12c & 0x80000000u),
                  "M900 dash: dies at 120 (HP 0, section 5, ATK - 12.5)");
        }
        {   // The self-kill: a slot >= the wanted count; and the FUN_00470210 path with 130 clear.
            EyeDuel w; w.flags[130] = 1;
            Actor& H = hugo(w);
            Actor& E = *w.spawn_child(H, 0xb0, {0, 0, 0}, 1, true);
            E.ai.i(0x730) = 2;                                    // want = 2 at full HP
            tick(E, w);
            check(E.hp == 0.0f && (E.flags12c & 0x80000000u) && (E.flags130 & 0x1008u) == 0x1008u,
                  "M900: a slot >= the wanted count kills it");
            Actor& F = *w.spawn_child(H, 0xb0, {0, 0, 0}, 1, true);
            w.flags[130] = 0;
            tick(F, w);
            check(F.ai.phase() == 0 && F.kind == 0x81 && F.hp == 10, "M900 with 130 clear: FUN_00470210 (not run)");
        }
        {   // M810's shots from the Eyes: the volley (fire 0xb) — an Eye in formation 4 gets timers instead.
            EyeDuel w; w.flags[130] = 1;
            Actor& H = hugo(w);
            tick(H, w); tick(H, w);                               // INIT, two Eyes
            Actor& E1 = *std::next(w.pool.begin(), 1);
            Actor& E2 = *std::next(w.pool.begin(), 2);
            E1.ai.i(0x72c) = 0; E2.ai.i(0x72c) = 4;
            H.ai.i(0x728) = 100; H.ai.i(0x7a4) = 300;
            const AiOut o = tick(H, w, Tick{0x12e, 1, 1});
            int from_e1 = 0;
            for (const AiBullet& b : o.bullets) from_e1 += b.pos.x == E1.pos.x && b.pos.y == E1.pos.y + E1.height;
            check(o.bullets.size() == 2 && from_e1 == 1 && E1.ai.i(0x7a0) == 5 && std::fabs(E1.ai.f(0x7cc) - 0.2f) < 1e-7f,
                  "M810 volley: Hugo and the Eye fire one 0xc6 each; the Eye holds its facing (5), recoil 0.2");
            check(E2.ai.i(0x7a4) == 7 && E2.ai.i(0x7a8) == 10, "M810 volley: the formation-4 Eye gets 7 shots, the first in 10*2/2");
        }
        {   // The gate reads POOL entry k, not Eye k (0x4d930f).
            EyeDuel w; w.flags[130] = 1;
            w.pool.emplace_back();                                // pool[0]: someone else
            Actor& H = hugo(w);
            tick(H, w); tick(H, w);
            Actor& E1 = *std::next(w.pool.begin(), 2);
            Actor& E2 = *std::next(w.pool.begin(), 3);
            H.ai.i(0x7c0) = 1;                                    // pool[1] = Hugo, recall done
            H.ai.i(0x728) = 100; H.ai.i(0x7a4) = 300;
            const AiOut o = tick(H, w, Tick{0x12e, 1, 1});
            check(o.bullets.size() == 2 && E1.ai.i(0x7a0) == 0 && E2.ai.i(0x7a0) == 5,
                  "M810 shots: Eye 1 is gated by pool[1] (Hugo, +0x7c0 set), Eye 2 by pool[2]");
        }
        {   // The eye orders: 3 launches the first Eye (SE 0x9a); 1000 recalls all, +0x7c0 = 30(d + 7).
            EyeDuel w; w.flags[130] = 1;
            Actor& H = hugo(w);
            tick(H, w); tick(H, w);
            Actor& E1 = *std::next(w.pool.begin(), 1);
            Actor& E2 = *std::next(w.pool.begin(), 2);
            H.ai.i(0x728) = 300; H.ai.i(0x87c) = 20; H.ai.i(0x7a4) = 300;
            AiOut o = tick(H, w);
            const bool se = std::find(o.sounds.begin(), o.sounds.end(), 0x9a) != o.sounds.end();
            check(se && E1.ai.i(0x72c) == 2 && E2.ai.i(0x72c) == 0 && H.ai.i(0x7a0) == 0,
                  "M810 order 3: the first Eye dashes (+0x72c = 2), SE 0x9a; Hugo's +0x7a0 kept");
            H.ai.i(0x728) = 600; H.ai.i(0x87c) = 20; H.ai.i(0x7a4) = 300;
            o = tick(H, w, Tick{0x139, 1, -1});
            check(E1.ai.i(0x7c0) == 210 && E2.ai.i(0x7c0) == 210 && H.ai.i(0x7c0) == 1,
                  "M810 recall (1000): every Eye +0x7c0 = 210 at d 0");
            // The warp picks a formation: every Eye not dashing takes it (or one is launched).
            EyeDuel w2; w2.flags[130] = 1;
            Actor& H2 = hugo(w2);
            tick(H2, w2); tick(H2, w2);
            Actor& F1 = *std::next(w2.pool.begin(), 1);
            Actor& F2 = *std::next(w2.pool.begin(), 2);
            int warped = -1;
            H2.ai.i(0x7a4) = 0;
            for (int t = 0; t < 200 && warped < 0; ++t) {
                o = tick(H2, w2);
                if (o.section == 0x13d) warped = t;
            }
            const int f = H2.ai.i(0x7a0);
            const bool took = (F1.ai.i(0x72c) == f && F2.ai.i(0x72c) == f) ||
                              ((F1.ai.i(0x72c) == 2) != (F2.ai.i(0x72c) == 2));
            check(warped >= 0 && took && H2.ai.i(0x7a4) == 300, "M810 warp: the Eyes take the formation it picks");
        }
        {   // The puppet: g_flags[214] clear dismisses the owned Eyes.
            EyeDuel w; w.flags[130] = 1;
            Actor& H = hugo(w);
            tick(H, w); tick(H, w);
            w.flags[130] = 0; w.flags[214] = 1;
            tick(H, w);
            const bool kept = std::next(w.pool.begin(), 1)->hp == 10;
            w.flags[214] = 0;
            tick(H, w);
            check(kept && std::next(w.pool.begin(), 1)->hp == 0 && std::next(w.pool.begin(), 2)->hp == 0,
                  "M810 puppet: g_flags[214] = 0 dismisses the owned Eyes (HP 0)");
        }
        {   // Together: Hugo holds (cooldown), his two Eyes take their places behind him.
            EyeDuel w; w.flags[130] = 1;
            Actor& H = hugo(w);
            for (int t = 0; t < 60; ++t) {
                if (t == 1) { H.ai.cool() = 9999; H.ai.i(0x7a4) = 9999; }
                tick(H, w);
                for (auto it = std::next(w.pool.begin()); it != w.pool.end(); ++it) tick(*it, w);
            }
            const float fz = 1.5707963f - H.yaw;
            bool placed = w.pool.size() == 3;
            for (auto it = std::next(w.pool.begin()); it != w.pool.end(); ++it) {
                const Vec3 o = origin(*it), HO = origin(H);
                const std::uint32_t s = std::uint32_t(it->ai.i(0x730));
                const float g = (s & 1) ? fz + 0.8975979f : fz - 0.8975979f;
                placed = placed && it->hp == 10 && std::fabs(o.x - (HO.x + std::cos(g) * -1.75f)) < 1e-4f &&
                         std::fabs(o.z - (HO.z + std::sin(g) * -1.75f)) < 1e-4f;
            }
            check(placed, "M810 + M900: two Eyes spawn and settle 1.75 u behind Hugo");
        }
    }

    floor4_tests();
    // ---- Hugo's Eyes of Fact as the player's familiars (#57, build/ability_re/EYE_FAMILIARS.md §7..§12)
    {
        check(hugo::eye_count(0, 0, false) == 2 && hugo::eye_count(0, 0, true) == 4 && hugo::eye_count(0, 1, false) == 6 &&
              hugo::eye_count(1, 1, true) == 0, "eyes: 2 / 4 in Boost / 6 with flag 210 / 0 with flag 211");
        check(hugo::eye_effect_class(0x88) == 2 && hugo::eye_effect_class(0x89) == 1 && hugo::eye_effect_class(0x82) == 3 &&
              hugo::eye_effect_class(0x93) == 6 && hugo::eye_effect_class(0x84) == 5 && hugo::eye_effect_class(0x85) == -1,
              "FUN_0046d650: 0x88 thunder, 0x89 fire, 0x82 melee, 0x93 class 6, 0x84 class 5, 0x85 none");
        struct Eyes : AiWorld {          // the player's eyes in a list; flags 157 / 158 as set
            std::list<Actor> pool;
            int f157 = 0, f158 = 0;
            void for_each(const std::function<void(Actor&)>& f) override { for (Actor& a : pool) f(a); }
            Actor* find(int id) override { for (Actor& a : pool) if (a.instance_id == id) return &a; return nullptr; }
            Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
            void remove(Actor&) override {}
            Ray ray(const Vec3&, const Vec3&) override { return {}; }
            bool sphere_hits(const Vec3&, float) override { return false; }
            void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
            const float* enemy_raw(int) override { return nullptr; }
            int flag(int i, int fb) override { return i == 157 ? f157 : i == 158 ? f158 : fb == 1 ? 0 : fb; }
        } w;
        FamiliarHost h;
        h.player_id = 13; h.origin = {0, 1.075f, 0}; h.heading = h.facing = 0; h.hp = 60; h.level = 5;
        for (int s = 1; s <= 2; ++s) {
            Actor& E = w.pool.emplace_back();
            E.instance_id = 100 + s; E.cast_id = 0xb0; E.height = 0.5f; E.level = 5; E.hp = 1;
            E.pos = {0, 0.575f, 0};
            E.ai.i(0x854) = 13; E.ai.i(0x730) = s; E.ai.phase() = 2; E.ai.i(0x710) = -1;
            E.ai.i(0x7bc) = 2; E.ai.i(0x7c0) = 2;
            E.ai.f(0x818) = 0.02f; E.ai.f(0x81c) = 1.4f; E.ai.f(0x820) = 3.2f;
        }
        auto env_of = [&](Actor& E) {
            AiEnv e; e.actor = &E; e.world = &w; e.character = 2; e.familiar = &h;
            e.self = {E.pos.x, E.pos.y + E.height, E.pos.z}; e.section = 2;
            return e;
        };
        const EnemyAiParams ep{};
        // The Fire Wheel: flag 157 = 1 -> mode 2, +0x7ac += 0.16, the 0x89 fire attached (power -2).
        w.f157 = 1; w.f158 = 150;
        Actor& e1 = w.pool.front();
        AiOut o = hugo::familiar(e1.ai, ep, env_of(e1));
        bool fire = false;
        for (const AiEffect& f : o.effects) fire = fire || (f.id == 0x89 && f.attach && f.power == -2.0f && f.tag == 0);
        check(e1.ai.i(0x72c) == 2 && std::fabs(e1.ai.f(0x7ac) - 0.16f) < 1e-6f && fire,
              "Fire Wheel: mode 2, the wheel turns 0.16 a tick, EF2 0x89 attached (power -2)");
        // The wheel's target for slot 1: theta = (+0x7b0 + pi/2) + 0.16, T = O - (cos, sin)(theta) x 1.5; the
        // step (|D| / 10 + 0.064 along D) goes into knock channel 2.
        {
            const float th = 1.5707963705062866f + 0.16f;
            const Vec3 T{-std::cos(th) * 1.5f, 1.075f, -std::sin(th) * 1.5f};
            const Vec3 D{T.x, 0.0f, T.z};
            const float dl = std::sqrt(D.x * D.x + D.z * D.z);
            const float k = (0.064f + dl / 10.0f) / dl;
            check(std::fabs(e1.mover.k2.x - D.x * k) < 1e-4f && std::fabs(e1.mover.k2.z - D.z * k) < 1e-4f,
                  "Fire Wheel: slot 1 steps toward its place on the 1.5 ring");
        }
        // Trap Mine, normal: the free eye whose slot is the free count (2) is laid on Hugo's spot, fuse 60.
        w.f157 = 0; w.f158 = 0;
        std::vector<hugo::DeployEvent> ev;
        auto sec2 = [](const Actor&) { return 2; };
        int n = hugo::deploy_mines(false, h, w, sec2, ev);
        Actor& e2 = w.pool.back();
        check(n == 1 && hugo::b8a4(e2.ai) && !hugo::b8a5(e2.ai) && e2.ai.i(0x7c8) == 0x3c && !hugo::b8a4(e1.ai) &&
              std::fabs(e2.pos.y + e2.height - 1.075f) < 1e-6f, "Trap Mine: slot 2 laid at Hugo, fuse 60, slot 1 stays");
        check(ev.size() == 2 && ev[0].id == 0x92 && ev[1].kind == hugo::DeployEvent::kSection && ev[1].id == 0x10f,
              "Trap Mine: EF2 0x92 at the eye's old spot, section 0x10f on the eye");
        // The fuse runs out: section 5 (the FireBullet 0x88), HP 0.
        e2.ai.i(0x7c8) = 1;
        o = hugo::familiar(e2.ai, ep, env_of(e2));
        check(o.section == 5 && o.section_force && o.set_hp == 0.0f, "Trap Mine: at 0 the eye plays section 5 and dies");
        // Charged: every free eye laid on the saved mine point, fuse 180, +0x8a5; 0x94 at the point.
        hugo::set_b8a4(e2.ai, false);
        ev.clear();
        h.origin = {3, 1.075f, 4};
        n = hugo::deploy_mines(true, h, w, sec2, ev);
        bool all = n == 2;
        for (Actor& E : w.pool) all = all && hugo::b8a4(E.ai) && hugo::b8a5(E.ai) && E.ai.i(0x7c8) == 0xb4 && E.pos.x == 3;
        check(all && h.fx_7d8 && ev[0].id == 0x94 && h.mine_point.z == 4, "Trap Mine charged: both laid at the mine point, fuse 180");
        // The charged fuse: no section 5; the player's latch (FUN_0046a3e0's blast).
        e1.ai.i(0x7c8) = 1;
        o = hugo::familiar(e1.ai, ep, env_of(e1));
        check(o.section < 0 && o.set_hp == 0.0f && h.latch_7d4 == 1, "Trap Mine charged: at 0 no explosion of its own, P+0x7d4 = 1");
    }
    floor5_tests();
    floor6_tests();

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
