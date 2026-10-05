// Enemy AI — the shared machinery every brain runs on, plus the M100 (Riffligan) brain.
//
// Re-derived 2026-10-05 instruction by instruction (build/audit/ai_bosses.md, then the
// export re-read): handler FUN_0047b7f0 -> brain FUN_0047adf0 -> action player
// FUN_00479b00 -> FUN_004758f0 (steer) / FUN_00476900 (heading) / FUN_00479a70 (action)
// -> FUN_00477720 (move primitive) / FUN_004776e0 (stop); idle FUN_00478470 ->
// FUN_00476f40 (wander); sight FUN_00475a80; line of sight FUN_00475c00. Field names
// carry the GoActor offset they mirror. Pure: the world is reached only through AiEnv's
// callbacks, so apps/cleria-aitest drives it headless.
//
// What it produces each tick is the engine's mover input: a heading (+0x308), a move
// mode (+0x6f8: 0 none, 1 walk, 2 run) with a speed (+0x30c), and — from the brain — a
// request to enter the attack section. The sprite mover (FUN_0045f4d0) turns that into
// motion; see enemy_motion() below.
#pragma once
#include <cstdint>
#include <functional>

#include "cleria/mathx.hpp"

namespace cleria {

// ENEMYINFO fields the brain init copies into the actor (FUN_0047adf0 state 0:
// +0x814..+0x850 = EI+0x90..+0xcc).
struct EnemyAiParams {
    float turn = 0;      // EI+0x90 -> +0x814, heading turn rate (rad/tick)
    float base = 0;      // EI+0x94 -> +0x818, base speed
    float walk_f = 0;    // EI+0x98 -> +0x81c, walk multiplier
    float run_f = 0;     // EI+0x9c -> +0x820, run multiplier
    float sight = 0;     // EI+0xb0 -> +0x834 (sight dy = sight*0.5 + 10; crumb eye offset)
};

// The +0x6f0.. AI block (zeroed by FUN_004782f0 at brain init).
struct EnemyAi {
    int phase = 0;        // +0x6f0: 0 init, 1 unaware (wander), 2 engaged, 3 dodge-hold
    int seen_phase = 0;   // +0x6f4: the phase the action player last saw (re-anchors home on change)
    int mode = 0;         // +0x6f8: mover mode 0 none / 1 walk / 2 run
    int cool = 0;         // +0x700
    int turn_hold = 0;    // +0x704: ticks the heading is locked
    int steer = 0;        // +0x708: steer mode (FUN_004758f0)
    int action = 0;       // +0x710: action id (FUN_00479a70)
    int dur = 0;          // +0x714: action duration, -1 = infinite
    int sub = 0;          // +0x728: brain sub-state (M100: 0 decide, 10 pre-attack, 11 strike)
    int t79c = 0;         // +0x79c: brain hold / cooldown timer
    int t7a0 = 0;         // +0x7a0: brain action timer
    int patience = 1;     // +0x7a4: non-attack actions taken since last approach
    int facing = 0;       // +0x7a8: consecutive ticks the target has been in front
    int leash = 0;        // +0x884: re-acquire lockout
    float speed = 0;      // +0x30c
    float heading = 0;    // +0x308, ENGINE basis: direction (cos, sin) in (x, z)
    Vec3 to_target{};     // +0x734
    float dist = 0;       // +0x740
    Vec3 steer_vec{};     // +0x748
    Vec3 waypoint{};      // +0x894
    Vec3 crumbs[4]{};     // +0x758: last-seen positions of the target (LOS breadcrumbs)
    int ncrumb = 0;       // +0x788
    Vec3 home{};          // +0x790: wander anchor
    // idle wander (FUN_00476f40)
    int w_type = 0;       // +0x424: 0 wander, 1 stand
    int w_phase = 1;      // +0x428: toggles each timer expiry; 1 = pick a point
    int w_timer = 0;      // +0x42c
    Vec3 w_target{};      // +0x434
    bool initialised = false;
};

struct AiEnv {
    Vec3 self{};                 // the actor's ORIGIN (floor + height)
    bool grounded = true;        // +0x388
    float radius = 0.675f;       // +0x328
    float height = 1.075f;       // +0x324
    Vec3 target{};               // the target's ORIGIN
    Vec3 target_vel{};           // the target's +0xa3c..+0xa44 (per tick)
    bool target_alive = true;    // HP >= 1.0 (or the scene keeps it targetable)
    bool target_grounded = true;
    bool was_hit = false;        // +0x3bc & 4 this tick (any landed hit)
    bool culled = false;         // outside the camera frustum (+0x250 && FUN_0045d3c0)
    int difficulty = 0;          // M100's d: 3 (g_flags[201]), -2 v.easy, -1 easy, 0/1/2 index
    std::function<bool(const Vec3&, const Vec3&)> blocked;   // segment hits collision
    std::function<bool(float x, float z, float from_y, float depth)> has_floor;  // ground within depth below
    std::function<std::uint32_t()> rng;  // one xorshift128 advance; returns the new word
};

// Output of one tick for the caller to act on.
struct AiOut {
    bool attack = false;   // enter the attack section now (M100: FUN_00446550(7,0))
};

// One tick of the M100 handler body: (frozen | brain + action player). The caller then
// runs the mover with enemy_motion() and the shared per-tick update.
AiOut enemy_ai_tick_m100(EnemyAi& ai, const EnemyAiParams& p, const AiEnv& env);

// The sprite mover's input stage (FUN_00478080 -> FUN_0045f4d0): the per-tick move
// speed along the heading. `movable` is the ASO's anime_Movable (+0x9cc).
inline float enemy_move_speed(const EnemyAi& ai, float movable) {
    return ai.mode == 0 ? 0.0f : ai.speed * movable;
}
// The locomotion section the mover requests (table 0x69ba48 = {2, 0, 1, 3}).
inline int enemy_loco_section(const EnemyAi& ai, bool grounded) {
    if (!grounded) return 3;
    return ai.mode == 2 ? 1 : ai.mode == 1 ? 0 : 2;
}
// The visible facing (+0x314) turns toward the heading at 0.18 rad/tick, 0.35 while
// running (FUN_0045f4d0 0x45fe8a: DAT_006bd1fc / DAT_006bd2b0, x +0x36c = 1.0).
float enemy_turn_facing(float facing, float heading, bool running);

// FUN_005eecc0: rotate `cur` toward `target` by at most `rate`, the short way round.
float rotate_toward(float cur, float target, float rate);

}  // namespace cleria
