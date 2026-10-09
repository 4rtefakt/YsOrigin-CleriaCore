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
#include <optional>
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
    float speed310 = 0;    // +0x310: the last speed FUN_00477720 wrote (M170's sidestep weight)
    float alpha = 1;       // +0x4d8: sprite opacity (brains that fade, e.g. M390)
    bool hit_cut = false;  // FUN_00475860 / FUN_004758a0 zeroed +0x118 (the armed hit record's life); the host clears it
    Vec3 anchor{};         // +0x51c: the origin at the last phase change (the leash's centre; +0x744 its distance)
    bool hittable = true;  // +0x60 & 0x80: hit tests and body push skip the actor when clear
    std::uint32_t dead_t = 0;  // +0x3fc: FUN_004780e0's death counter (0 alive, 1 at death, +1 a tick)
    int w_type = 0;        // +0x424: wander type (0 wander, 1 stand)
    int w_phase = 1;       // +0x428
    int w_timer = 0;       // +0x42c
    Vec3 w_target{};       // +0x434
    bool initialised = false;
    // A death hook (EnemyDeathHook) has no AiOut. What it asks of the world rides on the
    // actor into the next tick's output (enemy_ai_tick): a FUN_00446550(id, 0) request
    // made only if `id` is not the live section, and kills of the owned effects whose tags
    // are set in the mask (bit n = tag n).
    int hook_section = -1;
    std::uint32_t hook_kill_tags = 0;
    // The integrator's surface bytes and the selector-1 probe (MOVER.md §3, M560.md §0), kept by
    // the host's enemy integrator: +0x4b9 the surface under the feet (latched within 1.6 of the
    // floor ray), +0x4ba the last floor ray's surface (any distance), +0x320 the selector-1
    // ("water" set; for a sand actor the sand top) surface above, +0x38a in it.
    int surf = 1, surf_ray = 1;
    float water_y = -1.0e30f;
    bool in_water = false;
    float exp_base = -1;   // the revive family's spawn EXP (+0xb0), restored under Silver Chimes
};

struct Actor;   // cleria/actor.hpp (which embeds an EnemyAi)

// The world as the brains that reach OTHER actors see it (M460 and its bats): the live
// actor list in engine order, the generic child spawn, removal, and the collision
// queries their waypoint searches use. Implemented by the host (the view).
struct AiWorld {
    virtual ~AiWorld() = default;
    virtual void for_each(const std::function<void(Actor&)>& f) = 0;   // DAT_00730098 order
    virtual Actor* find(int instance_id) = 0;
    // FUN_00476d70(parent, cast, dx, dy, dz, level, keep_phase0): a fresh actor of `cast`
    // at the parent's origin + d with the parent's heading; AI memset, +0x854 = parent,
    // phase 0 if keep_phase0 else 4. nullptr when the pool is full.
    virtual Actor* spawn_child(Actor& parent, int cast, const Vec3& d, int level,
                               bool keep_phase0) = 0;
    virtual void remove(Actor& a) = 0;                       // FUN_004593c0
    // surface: the hit triangle's surface index (rec+0xaa), filled for straight-down rays; -1 unknown.
    struct Ray { bool hit = false; Vec3 point{}, normal{}; int surface = -1; };
    virtual Ray ray(const Vec3& origin, const Vec3& delta) = 0;   // FUN_005e9380 + FUN_0041e420
    virtual bool sphere_hits(const Vec3& centre, float radius) = 0;  // FUN_005e92b0 + FUN_0041ea80
    // DAT_0076a5cc..d8 = {xmin, zmin, xmax, zmax}: the arena rectangle.
    virtual void arena(float rect[4]) = 0;
    // ENEMYINFO +0x90..+0xcc (16 floats) for a cast, or nullptr.
    virtual const float* enemy_raw(int cast) = 0;
    // A game flag the brain writes (M460's dead path clears the boss-battle flags 156 / 130).
    virtual void set_flag(int /*index*/, int /*value*/) {}
    // A game flag a handler reads (g_flags[index], DAT_0076b91c + 4*index: 130 "boss AI live",
    // 131 the hold, 231 the rematch). A host that does not override it answers `fallback`, which
    // each caller sets to the value its fight runs under [H] (build/ai_spec/M850.md §8).
    virtual int flag(int /*index*/, int fallback) { return fallback; }
    // The player actor (DAT_0074c09c), for handler dead paths that read its HP or write its
    // +0x734 (i-frames); nullptr when the host does not expose it.
    virtual Actor* player() { return nullptr; }
    // DAT_006d5fd8 = 0: hide the boss HP gauge (the class-4 duel handlers' defeat paths).
    virtual void hide_boss_gauge() {}
    // FUN_004358b0(dir) then `cmp [0x76a534], word [record]`: the current room is the scene-list
    // record named `dir` (case-insensitive, e.g. "s_30\\s_3080\\"; M860's story-duel rules). A
    // host that does not override it answers false, the engine's answer when no record matches.
    virtual bool in_room(const char* /*dir*/) { return false; }
    // The GoEffect pool (DAT_007300bc[0 .. DAT_007300c0)) as the brains' effect scans walk it
    // (M820 0x4dfbc3, M860 0x4e33d8): every effect whose object byte +0x11 & 2 is clear, in pool
    // order. `id` = +0x1d8 (the EFTLIST id), `attach` = +0x1d5, `kill` = +0x1d4, `owner` = the
    // spawning actor's instance id (0 = none; 13 = the player). A host that does not override it
    // has no effects (the scans then read "none live").
    struct Fx { int id = 0; int attach = 0; bool kill = false; int owner = 0; int tag = -1; };
    virtual void for_each_effect(const std::function<void(const Fx&)>& /*f*/) {}
    // M710's cull (FUN_004c3300 0x4c3c98, its handler's dead path 0x4c3dd4): over the GoEffect pool in order, the
    // effects of EFTLIST id `id` attached to `self` (e+0x1a4 == self+0x20); every one past the first `keep` has its
    // root particle (FUN_00564a20) ended, p+0x1c4 = 0.
    virtual void cull_attached_fx(Actor& /*self*/, int /*id*/, int /*keep*/) {}
    // The GoXx pool's live bullets in pool order (DAT_00730248; BULLETS.md §1.1), for M420's orb hunt: `handle` is
    // a stable id (the engine keeps the pointer), `owner` an actor instance id (+0x120), then +0x124, +0x12c.., 
    // +0x138.., +0x144 and the per-kind floats +0x168 / +0x16c / +0x170. The pointers are writable.
    struct BulletRef {
        int handle = 0, kind = 0; int* owner = nullptr; Vec3 pos{}; Vec3* dir = nullptr; float* speed = nullptr;
        float* f168 = nullptr; float* f16c = nullptr; float* f170 = nullptr;
    };
    virtual void for_each_bullet(const std::function<void(const BulletRef&)>& /*f*/) {}
    // DAT_0076a1f8 / DAT_0076a1fc: the forced camera region and its blend rate (M360's INIT / death).
    virtual void force_camera(int /*region*/, float /*rate*/) {}
    // FUN_004202e0(ecx = a, edx = cast's ENEMYINFO record, level): +0x94 max HP, +0x98 = +0x94, +0xa4 STR,
    // +0xa8 DEF, +0xb0 EXP, +0xb4..+0xc0 resists, +0xc8 = level (0 = the record's default level).
    // A host that does not override it leaves the stats alone.
    virtual void restat(Actor& /*a*/, int /*cast*/, int /*level*/) {}
    // FUN_0043a080(id, &pos): a positional sound a handler plays at another actor (M360's crystal kills).
    virtual void sound_at(int /*id*/, const Vec3& /*pos*/) {}
    // M860's pillar break (FUN_004e3070 0x4e3540..0x4e3879; KISHGAL_SHIELD.md §3): every live, unmarked EF2
    // `shield_id` whose hit particle overlaps a hit particle of a live effect with one of the `n` ids is marked
    // (+0x1d5 = 0xff) and killed (FUN_0047a7c0); per pillar the 2-D SE `se` (FUN_0043a040) plays and EF2 `impact_id`
    // spawns at the pillar particle (power 0, knock 1, self's stats), itself marked 0xff. Returns the pillars broken.
    virtual int break_shields(Actor& /*self*/, int /*shield_id*/, const int* /*ids*/, int /*n*/, int /*impact_id*/,
                              int /*se*/) { return 0; }
};

struct AiEnv {
    Actor* actor = nullptr;      // this actor (brains that write its stats/scale/opacity)
    AiWorld* world = nullptr;    // the other actors (nullptr when the host has none)
    int character = 1;           // DAT_0076bb74 = g_flags[150]: 0 Adol, 1 Yunica, 2 Hugo, 3 Toal
    int sprite_dir = 0;          // +0x9b4: the sprite's camera-relative direction row (0..7)
    int aso_frame = 0;           // +0x9b8: the current anime_Pattern frame
    float movable = 1;           // +0x9cc: the ASO anime_Movable value
    bool durable = false;        // +0x9d8: anime_Durable
    Vec3 self{};                 // the actor's ORIGIN (floor + height)
    float facing = 0;            // +0x314: the visible facing, engine basis (cos, sin)
    float jump_v0 = 0;           // +0x408: the cast's jump speed (CASTINFO+0x50), the 0-arg jump
    bool grounded = true;        // +0x388
    float radius = 0.675f;       // +0x328
    float height = 1.075f;       // +0x324
    float hp = 1, max_hp = 1;    // +0x98 / +0x94
    int section = 0;             // +0x994, the live motion section
    Vec3 target{};               // the target's ORIGIN
    float target_floor = 0;      // the target's floor (+0x318), latched by the hover when grounded
    bool target_rehit_clear = true;  // no attacker holds a re-hit timer on the player (P+0x90 bank)
    Vec3 target_vel{};           // the target's +0xa3c..+0xa44 (per tick)
    bool target_alive = true;    // HP >= 1.0 (or the scene keeps it targetable)
    bool bloody = false;         // byte [0x6d5d92] > 1 (BloodyEffectLevel): the action player hunts a dead target too
    bool target_durable = false; // the target's byte +0x9d8 (anime_Durable; M860's freeze tests it)
    bool target_grounded = true;
    bool was_hit = false;        // +0x3bc & 4 this tick (any landed hit)
    bool culled = false;         // outside the camera frustum (+0x250 && FUN_0045d3c0)
    bool took_damage = false;    // +0x3bc & 2: a hit of >= 1 damage landed (cleared each tick)
    bool group_tag = false;      // +0x130 & 0x700: spawned with an XSO group tag
    int aso_marker = 0;          // +0x9bc: the animation script's anime_Marker value
    int aso_marker_block = -1;   // +0x9c0: the marker block value (-1 after a section start)
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
    int target_surface = 1;      // the target's +0x4b9 (the surface under its feet; 0x12 = sand2)
    bool chimes = false;         // DAT_0076a624 & 0x8000: Silver Chimes (the revive family, REVIVE.md §4)
    bool target_moving = false;  // the target's byte +0x514 (the player's movement input; M280's shake-off)
    std::uint32_t events = 0;    // +0x3bc this tick: 0x2000/0x4000 = the stun-rolling hits (REACT.md §1)
    bool stun_immune = false;    // +0x12c & 0x80000
    bool bless_stun = false;     // g_flags[217] & 0x80: the "Increase stun effect" blessing
    bool mask_of_eyes = false;   // DAT_0076bbac = g_flags[164]: the Mask of Eyes is worn (the Piaches' visibility)
    Vec3 cam_back{};             // [H] the camera region's eye - look-at (C+0x38.. - C+0x44.., M470's held-player nudge)
    bool boss_fight = false;     // DAT_0076bb8c = g_flags[156] == 1 (M540's handler picks its boss brain)
    // This actor's ASO section priority ("type") by id, or -1 if it has no such section —
    // what FUN_00446550 compares, so a brain can tell whether its request is accepted.
    std::function<int(int)> section_type;
    std::function<bool(const Vec3&, const Vec3&)> blocked;   // segment hits collision
    std::function<bool(float x, float z, float from_y, float depth)> has_floor;
    std::function<std::uint32_t()> rng;  // one xorshift128 advance; returns the new word
    struct FamiliarHost* familiar = nullptr;  // the player side of Hugo's Eyes of Fact (hugo_eyes.hpp)
    // Zava (AI index 80, FUN_0053af10): her link to the two boss06 heads and the handler's extra inputs / outputs
    // (boss06_zava.hpp). Null for every other actor; with it null her brain is inert.
    struct Boss06Link* boss06 = nullptr;
};

// A request a brain makes of the world this tick.
struct AiSpawn {
    int cast = 0; Vec3 pos{}; float yaw = 0;   // yaw = the engine heading (+0x308)
    float hp = -1;            // the child's HP (+0x98); < 0 = its cast's own max
    bool exp_copy = false;    // the child's EXP (+0xb0) = the parent's, after AiOut::exp_scale
};
// An EF2 effect a brain spawns at a point (the inlined pool-pop + FUN_00451a40 pattern).
// tag >= 0: the brain owns it (M530's beams); move_tag >= 0: no spawn, re-place every live
// effect this actor owns with that tag at pos (move_scale > 0: with that uniform scale, else 1);
// kill_tag >= 0: no spawn, kill them (M140's
// KILLFX — the engine fades them over 16 steps; [not modelled] the fade).
// rot = (pitch, yaw, roll) for FUN_00451a40; power / knock / rtype / status = the GoEffect hit
// fields (+0x1c4 / +0x1c8 / +0x1ec / +0x1c0) its hit emitters arm (EFFECTHIT.md); the defaults
// are the inlined spawn's usual 0 / 1.0 / 0 / 0. attach: follow the actor (FUN_00451c20 mode 2,
// offset = pos - the actor's origin).
struct AiEffect {
    int id = 0; Vec3 pos{}; float scale = 1; bool attach = false;
    int tag = -1, move_tag = -1, kill_tag = -1;
    float move_scale = 0;
    // remain_tag >= 0: no spawn; every particle of this actor's effects with that tag gets p+0x1c4 = remaining
    // and the effect is let go (its tag dropped: the owner's +0x88c = 0; M750's END).
    int remain_tag = -1, remaining = 0;
    // release_tag >= 0: no spawn; FUN_0047a880 on this actor's effects with that tag (their looping particles are
    // not renewed) and the effect is let go. dying_tag >= 0: no spawn; FUN_0047a7c0 on them (M700).
    int release_tag = -1, dying_tag = -1;
    bool attach_full = false;    // with attach: FUN_00451c20 mode 1 (the actor's full matrix), not mode 2
    Vec3 rot{};
    float power = 0, knock = 1;
    int rtype = 0, status = 0;
};
// A hit volume a brain arms by hand: FUN_004494e0, the anime_GenerateCollision body, with
// its 13 arguments in script order (hit_volume.hpp volume_from_args decodes them).
struct AiHitbox {
    float shape = 0, radius = 0, fwd = 0, angle_deg = 0, voff = 0, half_h = 0;   // [0]..[5]
    int life = 0, rehit = 0, one_shot = 0, rtype = 0, status = 0;                // [6]..[10]
    float power = 0, knock = 0;                                                 // [11], [12]
    // FUN_00477f00 (the brain-armed sphere, M340/M380 stings) draws a FRESH hit serial on
    // every call into the attacker (+0x3c4) and the record; FUN_004494e0 keeps the actor's.
    bool new_serial = false;
};
// A GoXx bullet a brain creates directly (FUN_0040f700 + FUN_00583a00 + FUN_00593b90).
// FUN_00593550's block (build/ai_spec/BULLETS.md §1.1); the host runs it as a GoXx bullet
// (src/cleria/bullet.hpp). The floats after the direction land at +0x144..+0x174.
struct AiBullet {
    int kind = 1;                             // +0x124
    int effect = 0;                           // +0x128
    int owner = -1;                           // +0x120: an actor instance id; -1 = the brain's actor
    Vec3 pos{}, dir{};
    float speed = 0, accel = 0, gravity = 0;  // +0x144 / +0x148 / +0x14c
    float power = 0, knock = 1;               // +0x150 / +0x154 (knock < 0: an rtype code)
    int trail_effect = 0, trail_period = 0;   // +0x158 / +0x15c: a trail puff every N ticks
    int home_period = 0;                      // +0x160: home every N ticks (0 = never)
    float home_rate = 0;                      // +0x164: rad per homing step
    float f168 = 0, f16c = 0, f170 = 0, f174 = 0;   // per kind (BULLETS.md §1.1)
    float fx_scale = 0;                       // > 0: the brain then sets the effect's scale (+0x1ac..)
    bool gas = false;                         // its EF2 gets +0x1a8 = owner (the M460 gas floor)
    // The brain's write into its EF2's first (root) particle after the spawn (M980: p+0x1c4 / +0x1c0);
    // < 0 = untouched.
    int root_remaining = -1, root_life = -1;
};
struct AiOut {
    int section = -1;            // FUN_00446550(section, 0): a priority-GATED request
    bool section_force = false;  // FUN_00446620: ungated
    std::vector<AiSpawn> spawns; // FUN_00476d70 / FUN_00475710 style spawns (cast ids)
    std::vector<int> sounds;     // EFFECTDEF ids, played at the actor (FUN_0043a080)
    std::vector<AiEffect> effects;
    // FUN_00479b00's first act on a tick after a damaging hit (+0x3bc & 2): FUN_00478510's
    // alive path, the character hit spark (hit_feedback.hpp), placed from the victim's LAST
    // vector to the player (+0x734/+0x73c before this tick's refresh). The host spawns it (it
    // needs the camera and the game RNG); SE 92 is queued in `sounds`.
    bool hit_spark = false;
    float hit_spark_dx = 0, hit_spark_dz = 0;
    float set_hp = -1;           // the brain wrote its own HP (+0x98); < 0 = untouched
    float exp_scale = 1;         // x its own EXP reward (+0xb0)
    std::vector<AiHitbox> hitboxes;
    std::vector<AiBullet> bullets;
    int set_marker = -1000;      // write +0x9bc (the ASO marker) when > -1000
    int set_marker_block = -1000;  // write +0x9c0 (the marker block value) when > -1000
    int set_durable = -1;        // write +0x9d8 (anime_Durable) when >= 0
    float gauge_hp = -1;         // FUN_0043a460: show the boss gauge at this HP when >= 0
    float gauge_len = -1;        // its bar length / lenScale (M850 3000 Hugo / 4000 / 7500); <= 0 = the host's rule
    int gauge_emblem = 0;        // FUN_0043a460's third argument (M360: 6)
    // A handler's add to this tick's mover displacement (+0xa3c / +0xa44) before the integrator
    // (M980's travel velocity +0x7f0 / +0x7f8, FUN_004d6ea0 0x4d71c6).
    float add_dx = 0, add_dz = 0;
    // The handler skipped the action player (FUN_00479b00) this tick: the class-4 duel
    // handlers' script-puppet path (g_flags[130] == 0 -> FUN_0045f250) and their g_flags[131]
    // freeze (FUN_0045f470 only).
    bool skip_action_player = false;
    // The brain cleared +0x3bc & 4 (the landed-hit bit) before the action player ran (M160 0x4ba9fc):
    // the action player then does not see the hit (no acquisition through it).
    bool consume_hit = false;
    bool snap_facing = false;    // the brain set +0x314 = +0x308 (visible facing = heading)
    // M470's hold (FUN_0048c300 0x48ce69): this tick the target's origin (+0x50, +0x3d4, +0x134, +0x6c) is placed at
    // hold_origin, its vertical timer +0x330 zeroed and its vertical-integration guard +0x398 set. A tick without
    // it is the release (+0x398 = 0).
    bool hold_target = false;
    Vec3 hold_origin{};
    // M860's freeze (FUN_004e3070 0x4e39d1..0x4e3a35; KISHGAL_SHIELD.md §4.3): with hold_target, only the
    // target's x and z (+0x50 / +0x58) are placed at hold_origin and its knock channels 1 and 2
    // (+0x338..+0x34c) are zeroed; y, the vertical timer and +0x398 are left alone.
    bool hold_xz_only = false;
    int target_section = -1;     // FUN_00446550(ecx = T+0x8f0, id, 0): a GATED section request on the target's VM
    int target_wait_add = 0;     // T+0x9a8 += n: the target VM's wait counter
    bool heavy_hold = false;     // M280 latched on its target: the player's status bit 0x8 with timer 1 (0x4a40bb)
    bool launch_clear = false;   // the stun exit cleared +0x4d0
    // ZoomBlur (build/render_re/SCREEN_EFFECTS.md §7.1): the brain wrote 0x76a550 (strength) and / or 0x76a554
    // (rate) — each alone keeps the other — and / or called FUN_005e0590 with a world point (the blur's centre).
    std::optional<float> zoom_s, zoom_r;
    std::optional<Vec3> zoom_centre;
    int set_own_jump = -1;       // write +0x9d6 (the own-jump byte, ASO-side) when >= 0
    int set_frame = -1;          // write +0x9b8 (the current pattern frame) when >= 0
    int fast_forward = -1;       // FUN_00446680(m) right after this tick's section request
    // A handler that overrides +0x9d8 (Durable) AFTER the VM step (M110): 1 = force on,
    // 0 = force off unless in section 7, 2 = on in section 312 else as 0 (M600), -1 = leave it
    // to the script.
    int durable_rule = -1;
    // FUN_004472e0 called by the brain right after its section request (M600's 312): one
    // extra VM step on whatever section is current, before the brain's +0x9c0 write lands.
    // [not modelled] that extra step's sound / bullet events (the main step's clear drops them).
    bool vm_step = false;
    // Back-compat for the M100 path: true when the request is the cast's attack.
    bool attack = false;
};

using EnemyBrain = AiOut (*)(EnemyAi&, const EnemyAiParams&, const AiEnv&);

// Handler code that runs on the dead path (HP < 1) for some casts — M460 kills its flock.
using EnemyDeathHook = void (*)(Actor&, AiWorld&);
EnemyDeathHook enemy_death_hook_for(int ai_index);

// FUN_0047abf0, the arena clamp some handlers run after the mover input (M100's, M310's;
// build/ai_spec/ARENA_AND_BOSSBAR.md §(a)). mode = g_flags[190]: 1 clamps x/z into the rect
// {xmin, zmin, xmax, zmax}; 2 projects the ORIGIN onto the sphere {cx, cy, cz, radius}.
bool enemy_handler_clamps(int ai_index);
// The far freeze of the M680 / M740 handlers (FUN_004c4df0 0x4c4e18..0x4c4e59, FUN_004c59d0; build/ai_spec/M680.md
// §2.1): while the actor's origin is 25 u or more (3-D, or NaN) from the player's, the handler runs only the ASO
// step (FUN_004472e0) and FUN_00473540 — no HP test, brain, action player, mover, integrator or hit test.
bool enemy_handler_far_freeze(int ai_index);
void arena_clamp(Vec3& origin, int mode, const float rect[4]);
// The handler-tail position clamps per AI index: FUN_0047abf0 above for M100 / M310,
// M530's own (0x497b7a): while DAT_0076bb8c (g_flags[156]) != 0, z >= -2.75, and the class-4
// duel handlers': M850's (0x4d25aa) box clamp when arena_mode == 1 and g_flags[131] == 0; M810's
// own inset box (0x4daa9d, mode-blind) and M830's FUN_0047abf0, both only on their live path
// (g_flags[130] != 0), which `boss_fight` stands in for [H]. A host that does not pass flag131
// gets 0, the value the duel scripts set [H]. M970's (0x4cc375): the box, mode-blind, in phase 4
// (the actor's +0x6f0, passed as `phase`).
void enemy_handler_clamp(int ai_index, Vec3& origin, int arena_mode, bool boss_fight, const float rect[4],
                         int flag131 = 0, int phase = 0);
// A boss handler's gates ahead of its brain (M850 FUN_004d1c90, build/ai_spec/M850.md §1), read
// through env.world->flag: puppet = g_flags[130] == 0 (FUN_0045f250, the plain-actor tick: no
// INIT, no brain, no action player); held = g_flags[131] != 0 (the plain mover only). Both are
// consulted by enemy_ai_tick for env.actor's AI index.
bool enemy_handler_puppet(int ai_index, const AiEnv& env);
bool enemy_handler_held(int ai_index, const AiEnv& env);
// The handler code between the action player and the mover tail (src/cleria/brains/revive.cpp):
// M560 / M780's acquisition veto (REVIVE.md §5.4), given the phase read right after the brain.
void enemy_handler_post(int ai_index, EnemyAi& a, int phase_after_brain);

// The brain for an AI index (actor +0x120 = CASTINFO +0x64), or nullptr when not yet
// ported (callers fall back to the M100 brain as a stand-in and say so).
EnemyBrain enemy_brain_for(int ai_index);
// enemy_brain_for, or the M100 brain as a STAND-IN for a brain not yet ported.
EnemyBrain enemy_brain_or_standin(int ai_index);

// One tick of the standard handler body (FUN_0047b7f0 shape): off-screen and unaware ->
// nothing; else brain + action player. A boss handler's gates (enemy_handler_puppet /
// enemy_handler_held) come first, in its own order.
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
bool blocked_ahead(const EnemyAi& a, const AiEnv& e);                    // FUN_00477450(origin)
bool probe_ahead(const EnemyAi& a, const AiEnv& e, const Vec3& P);       // FUN_00477450(P)
void move(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, int approach, int walk);  // FUN_00477720
void steer(EnemyAi& a, const AiEnv& e);                                  // FUN_004758f0(+0x708)
void turn(EnemyAi& a, float rate);                                       // FUN_00476900
// FUN_00479a70(+0x710); with `out`, the jump helper FUN_00476200 runs for jump-capable casts.
void act(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, AiOut* out = nullptr);
void acquire(EnemyAi& a);                                                // FUN_004758a0
void disengage(EnemyAi& a);                                              // FUN_00475860
bool sees(const EnemyAi& a, const AiEnv& e, float max_dy, float d2max, float cone);  // FUN_00475a80
bool los_keep(EnemyAi& a, const AiEnv& e, float max_dy, float sight);    // FUN_00475c00
void wander(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e);         // FUN_00476f40
void idle(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e);           // FUN_00478470
void action_player(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e, AiOut& out);  // FUN_00479b00
void init(EnemyAi& a, const EnemyAiParams& p, const AiEnv& e);         // FUN_004782f0
// The same for an actor that is not the caller (M460 initialising a bat it just spawned):
// home = its origin, raw = its ENEMYINFO +0x90..+0xcc (nullptr: leave +0x814.. zero).
void init_block(EnemyAi& a, const Vec3& home, const float raw[16]);
// FUN_00476960(mode): the flyer altitude controller. Moves `self`'s origin y toward
// base + H (+0x834) by at most one rise (+0x838) / sink (+0x83c) step, with the bob
// (+0x840 amplitude, +0x844 deg/tick, +0x848 phase). `ref` is +0x888 (null = the player,
// read from e.target_floor / e.target_grounded). build/ai_spec/FLYER.md §1.
void hover(EnemyAi& a, Actor& self, const AiEnv& e, const Actor* ref, int mode);
// FUN_005eec50: norm(a) - norm(h) (FUN_005ef010, into [0, 2pi]) wrapped into [-pi, pi].
double angle_diff(float h, float a);
// FUN_00446550(id, no_restart) as the brain sees it: accepted unless the section is
// missing, already current with no_restart, or of lower priority than the current one.
// On acceptance `cur` becomes id (FUN_00446620 updates +0x994 at once).
bool request(int& cur, int id, bool no_restart, const AiEnv& e);
}  // namespace ai

}  // namespace cleria
