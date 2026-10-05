// Enemy AI — the shared machinery every brain runs on, and the brain registry.
//
// Re-derived 2026-10-05 instruction by instruction (build/audit/ai_bosses.md, then the
// export re-read): handler FUN_0047b7f0 -> brain FUN_0047adf0 -> action player
// FUN_00479b00 -> FUN_004758f0 (steer) / FUN_00476900 (heading) / FUN_00479a70 (action)
// -> FUN_00477720 (move primitive) / FUN_004776e0 (stop); idle FUN_00478470 ->
// FUN_00476f40 (wander); sight FUN_00475a80; line of sight FUN_00475c00. Pure: the world
// is reached only through AiEnv's callbacks, so apps/cleria-aitest drives it headless.
//
// The AI state is a RAW mirror of the GoActor's +0x6f0..+0x8b0 block (the range the
// brain init memsets, plus the phase word), so a brain is ported line by line against
// the decompile: `ai.i(0x728)` is exactly the decompile's `*(int*)(this + 0x728)`.
// Named accessors cover the fields the shared machinery uses. Brains live in
// src/cleria/brains/ (one file per brain, specs in build/ai_spec/), registered in
// brains/registry.cpp by AI index (actor +0x120 = CASTINFO +0x64).
//
// Each tick produces the engine's mover input: a heading (+0x308), a move mode
// (+0x6f8: 0 none, 1 walk, 2 run) with a speed (+0x30c), and section requests.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

// ENEMYINFO fields the brain init copies into the actor (+0x814..+0x850 =
// EI+0x90..+0xcc). The first five are what the shared machinery reads.
struct EnemyAiParams {
    float turn = 0;      // EI+0x90 -> +0x814, heading turn rate (rad/tick)
    float base = 0;      // EI+0x94 -> +0x818, base speed
    float walk_f = 0;    // EI+0x98 -> +0x81c, walk multiplier
    float run_f = 0;     // EI+0x9c -> +0x820, run multiplier
    float sight = 0;     // EI+0xb0 -> +0x834 (sight dy = sight*0.5 + 10; crumb eye offset)
    float raw[16] = {};  // EI+0x90..+0xcc verbatim (+0x814..+0x850), for brain-specific reads
};

struct EnemyAi {
    static constexpr int kBase = 0x6f0, kEnd = 0x8b0;
    std::array<std::uint32_t, (kEnd - kBase) / 4> blk{};

    std::int32_t& i(int off) { return reinterpret_cast<std::int32_t&>(blk[std::size_t(off - kBase) / 4]); }
    float& f(int off) { return reinterpret_cast<float&>(blk[std::size_t(off - kBase) / 4]); }
    std::int32_t i(int off) const { return std::int32_t(blk[std::size_t(off - kBase) / 4]); }
    float f(int off) const { float x; std::memcpy(&x, &blk[std::size_t(off - kBase) / 4], 4); return x; }
    Vec3 v(int off) const { return {f(off), f(off + 4), f(off + 8)}; }
    void set_v(int off, const Vec3& x) { f(off) = x.x; f(off + 4) = x.y; f(off + 8) = x.z; }

    // Named views of the block (the shared machinery's fields).
    std::int32_t& phase() { return i(0x6f0); }        // 0 init, 1 unaware, 2 engaged, 3 dodge
    std::int32_t phase() const { return i(0x6f0); }
    std::int32_t& seen_phase() { return i(0x6f4); }   // the phase the action player last saw
    std::int32_t& mode() { return i(0x6f8); }         // mover mode 0 none / 1 walk / 2 run
    std::int32_t mode() const { return i(0x6f8); }
    std::int32_t& cool() { return i(0x700); }
    std::int32_t cool() const { return i(0x700); }
    std::int32_t& turn_hold() { return i(0x704); }    // ticks the heading is locked
    std::int32_t& steer() { return i(0x708); }        // steer mode (FUN_004758f0)
    std::int32_t& action() { return i(0x710); }       // action id (FUN_00479a70)
    std::int32_t action() const { return i(0x710); }
    std::int32_t& dur() { return i(0x714); }          // action duration, -1 = infinite
    std::int32_t& sub() { return i(0x728); }          // brain sub-state
    float& dist() { return f(0x740); }                // distance to the target
    float dist() const { return f(0x740); }
    std::int32_t& ncrumb() { return i(0x788); }
    std::int32_t& leash() { return i(0x884); }

    // Outside the block.
    float heading = 0;     // +0x308, ENGINE basis: direction (cos, sin) in (x, z)
    float speed = 0;       // +0x30c
    float alpha = 1;       // +0x4d8: sprite opacity (brains that fade, e.g. M390)
    bool hittable = true;  // +0x60 & 0x80: hit tests and body push skip the actor when clear
    int w_type = 0;        // +0x424: wander type (0 wander, 1 stand)
    int w_phase = 1;       // +0x428
    int w_timer = 0;       // +0x42c
    Vec3 w_target{};       // +0x434
    bool initialised = false;
};

struct AiEnv {
    Vec3 self{};                 // the actor's ORIGIN (floor + height)
    bool grounded = true;        // +0x388
    float radius = 0.675f;       // +0x328
    float height = 1.075f;       // +0x324
    float hp = 1, max_hp = 1;    // +0x98 / +0x94
    int section = 0;             // +0x994, the live motion section
    Vec3 target{};               // the target's ORIGIN
    Vec3 target_vel{};           // the target's +0xa3c..+0xa44 (per tick)
    bool target_alive = true;    // HP >= 1.0 (or the scene keeps it targetable)
    bool target_grounded = true;
    bool was_hit = false;        // +0x3bc & 4 this tick (any landed hit)
    bool culled = false;         // outside the camera frustum (+0x250 && FUN_0045d3c0)
    bool took_damage = false;    // +0x3bc & 2: a hit of >= 1 damage landed (cleared each tick)
    bool group_tag = false;      // +0x130 & 0x700: spawned with an XSO group tag
    int aso_marker = 0;          // +0x9bc: the animation script's anime_Marker value
    bool launched = false;       // +0x4d0 != 0: the knock-up state (brains early-out) [not modelled]
    int difficulty = 0;          // 3 (g_flags[201]), -2 v.easy, -1 easy, 0/1/2 index
    // The raw difficulty globals, for brains that read them directly:
    int diff_index = 0;          // DAT_0076bb5c = g_flags[144]: 0 normal, 1 hard, 2 nightmare
    bool diff_easy = false;      // DAT_0076bb58 = g_flags[143]: easy OR very easy
    bool diff_very_easy = false; // DAT_0076bc48: very easy (set together with bb58)
    int pool_count = 0;          // DAT_0073009c: live actors in the GoActor pool
    int target_section = -1;     // the player's ASO section (P+0x994)
    float target_heading = 0;    // the player's heading (P+0x308), engine basis (cos, sin)
    bool turnable = true;        // this actor's ASO anime_Turnable (+0x9d3)
    // This actor's ASO section priority ("type") by id, or -1 if it has no such section —
    // what FUN_00446550 compares, so a brain can tell whether its request is accepted.
    std::function<int(int)> section_type;
    std::function<bool(const Vec3&, const Vec3&)> blocked;   // segment hits collision
    std::function<bool(float x, float z, float from_y, float depth)> has_floor;
    std::function<std::uint32_t()> rng;  // one xorshift128 advance; returns the new word
};

// A request a brain makes of the world this tick.
struct AiSpawn {
    int cast = 0; Vec3 pos{}; float yaw = 0;   // yaw = the engine heading (+0x308)
    float hp = -1;            // the child's HP (+0x98); < 0 = its cast's own max
    bool exp_copy = false;    // the child's EXP (+0xb0) = the parent's, after AiOut::exp_scale
};
// An EF2 effect a brain spawns at a point (the inlined pool-pop + FUN_00451a40 pattern).
struct AiEffect { int id = 0; Vec3 pos{}; float scale = 1; bool attach = false; };
struct AiOut {
    int section = -1;            // FUN_00446550(section, 0): a priority-GATED request
    bool section_force = false;  // FUN_00446620: ungated
    std::vector<AiSpawn> spawns; // FUN_00476d70 / FUN_00475710 style spawns (cast ids)
    std::vector<int> sounds;     // EFFECTDEF ids, played at the actor (FUN_0043a080)
    std::vector<AiEffect> effects;
    float set_hp = -1;           // the brain wrote its own HP (+0x98); < 0 = untouched
    float exp_scale = 1;         // x its own EXP reward (+0xb0)
    // Back-compat for the M100 path: true when the request is the cast's attack.
    bool attack = false;
};

using EnemyBrain = AiOut (*)(EnemyAi&, const EnemyAiParams&, const AiEnv&);

// The brain for an AI index (actor +0x120 = CASTINFO +0x64), or nullptr when not yet
// ported (callers fall back to the M100 brain as a stand-in and say so).
EnemyBrain enemy_brain_for(int ai_index);
// enemy_brain_for, or the M100 brain as a STAND-IN for a brain not yet ported.
EnemyBrain enemy_brain_or_standin(int ai_index);

// One tick of the standard handler body (FUN_0047b7f0 shape): off-screen and unaware ->
// nothing; else brain + action player.
AiOut enemy_ai_tick(EnemyAi& ai, const EnemyAiParams& p, const AiEnv& env, EnemyBrain brain);
AiOut enemy_ai_tick_m100(EnemyAi& ai, const EnemyAiParams& p, const AiEnv& env);

// The sprite mover's input stage (FUN_00478080 -> FUN_0045f4d0).
inline float enemy_move_speed(const EnemyAi& ai, float movable) {
    return ai.mode() == 0 ? 0.0f : ai.speed * movable;
}
// The locomotion section the mover requests (table 0x69ba48 = {2, 0, 1, 3}).
inline int enemy_loco_section(const EnemyAi& ai, bool grounded) {
    if (!grounded) return 3;
    return ai.mode() == 2 ? 1 : ai.mode() == 1 ? 0 : 2;
}
// The visible facing (+0x314) turns toward the heading at 0.18 rad/tick, 0.35 running.
float enemy_turn_facing(float facing, float heading, bool running);
// FUN_005eecc0: rotate `cur` toward `target` by at most `rate`, the short way round.
float rotate_toward(float cur, float target, float rate);

// ---------------------------------------------------------------------------
// The shared primitives, for brain implementations (src/cleria/brains/*.cpp).
namespace ai {
inline std::uint32_t r15(const AiEnv& e) { return e.rng() >> 17; }       // (w >> 17)
void stop(EnemyAi& a, int hold);                                         // FUN_004776e0
bool blocked_ahead(const EnemyAi& a, const AiEnv& e);                    // FUN_00477450
void move(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, int approach, int walk);  // FUN_00477720
void steer(EnemyAi& a, const AiEnv& e);                                  // FUN_004758f0(+0x708)
void turn(EnemyAi& a, float rate);                                       // FUN_00476900
void act(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e);            // FUN_00479a70(+0x710)
void acquire(EnemyAi& a);                                                // FUN_004758a0
void disengage(EnemyAi& a);                                              // FUN_00475860
bool sees(const EnemyAi& a, const AiEnv& e, float max_dy, float d2max, float cone);  // FUN_00475a80
bool los_keep(EnemyAi& a, const AiEnv& e, float max_dy, float sight);    // FUN_00475c00
void wander(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e);         // FUN_00476f40
void idle(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e);           // FUN_00478470
void action_player(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e);  // FUN_00479b00
void init(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e);         // FUN_004782f0
// FUN_005eec50: norm(a) - norm(h) (FUN_005ef010, into [0, 2pi]) wrapped into [-pi, pi].
double angle_diff(float h, float a);
// FUN_00446550(id, no_restart) as the brain sees it: accepted unless the section is
// missing, already current with no_restart, or of lower priority than the current one.
// On acceptance `cur` becomes id (FUN_00446620 updates +0x994 at once).
bool request(int& cur, int id, bool no_restart, const AiEnv& e);
}  // namespace ai

}  // namespace cleria
