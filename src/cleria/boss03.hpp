// Gelaldy ("boss03", room S_3099) — the boss object's logic, pure and headless.
//
// Ported from build/boss_re/GELALDY.md (the verified spec; § numbers below are its
// sections), re-checked instruction by instruction against the disassembly of the
// functions it names (the spec pass's dumps, tools/exe_peek.py for every constant):
//   ctor FUN_0051ffe0, brain FUN_00521130, driver FUN_005227e0, next attack FUN_0051ea30,
//   enter special FUN_0051e880, ROLL FUN_00523880, PUNCH FUN_00523b30, SWING FUN_00523fd0,
//   BEAT FUN_00524240, MAGUMA FUN_00524930, TEETH FUN_00524f10, arm break FUN_00525300,
//   NAGI FUN_00525940, FIRE FUN_00526510, death FUN_005215e0, tail FUN_0051f520, wave
//   FUN_00527280, ring motion FUN_005275f0, atari FUN_00527910, summon cap FUN_005279f0,
//   arm poses FUN_00523480 / FUN_00523670, neck FUN_00527b50, helpers FUN_0051f190 /
//   FUN_0051f240 / FUN_0051f280 / FUN_0051f390 / FUN_0051f420.
//
// Gelaldy is ONE GoMesh (`boss03`, at (0, -3.5, 0)) with five GoXx hurt parts on nodes
// (0 Core, 1 RightNeji = the right arm, 2 RightFyubi1, 3 LeftNeji = the left arm, 4
// LeftFyubi1). It never moves; it turns. The world is reached only through Boss03Input
// and the Boss03Output request vocabulary, as in boss01 / boss02; apps/cleria-boss03test
// drives it headless.
//
// Tags in comments: addresses are [BYTES]/[EXPORT]/[DATA] as in the spec; [H] = inference;
// [UNKNOWN] = not resolved by the spec; [STAND-IN] = a host-side helper that is NOT engine
// code; [not modelled] = engine behaviour this port leaves out (listed in GELALDY.md §16).
//
// RNG: every draw goes through Boss03Input::rng (the game's ONE xorshift128, 0x74622c..38,
// returning the new word w); R(n) = (w >> 17) % n. The brain draws only in SWING (1, both
// arms), BEAT (1, both arms), FIRE (1..20 per random-pillar frame) and the death debris (7
// per burst). THE TRAP: the camera code FUN_00427c90 draws 3 more times every frame while
// the shake counter 0x76a5ac is non-zero [EXPORT 00427c90.c:104-121], and Gelaldy shakes
// a lot (BEAT, NAGI, FIRE, the 795-frame death shakes). A host must run those draws at its
// camera step — boss03::camera_shake() is that step, ported.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

namespace boss03 {

inline constexpr int kParts = 5;     // obj+0x278
inline constexpr int kRings = 36;    // ring00..35 / atari00..35
inline constexpr int kSeqLen = 41;   // blk+0..0xa0
inline constexpr int kClips = 32;    // blk+0xc8..0x144 (= the YMO section order)
inline constexpr int kFxIds = 22;    // blk+0x550..0x57b

// The clip name table 0x68b540; blk+0xc8+4k holds section k, so a clip id IS its name index.
enum Clip : int {
    kAll = 0, kWait1, kRRoll, kLRoll, kHDamage, kRPunchi, kLPunchi, kBeat, kLSwing, kRSwing,
    kMaguma1, kMaguma2, kMaguma3, kTeeth1, kTeeth2, kTeeth3, kTeeth4, kTeeth5, kTeeth6,
    kLDamage, kRDamage, kDDamage, kLBeat, kRNagi1, kRNagi2, kRNagi3, kLNagi1, kLNagi2, kLNagi3,
    kFire, kDemo, kDead,
};
// BOSS03.YMO (§2.5 [DATA]): absolute frame range, frames per step, loop (next -2) or stop.
struct ClipInfo { const char* name; int start, end; float rate; bool loop; };
extern const ClipInfo kClipTable[kClips];

// Every node the brain reads by name. 0..8 are the ctor's table blk+0xa4 (0x68b5c0); the
// rest are looked up inline (strings 0x69cee0..0x69d100).
enum Node : int {
    kNCore = 0, kNRightNeji, kNRightFyubi1, kNLeftNeji, kNLeftFyubi1, kNRightForeArm2, kNLeftForeArm2,
    kNSaru, kNNeck4, kNRightHand, kNLeftHand, kNRightForeArm1, kNLeftForeArm1, kNHead, kNRoot, kNodes,
};
extern const char* const kNodeNames[kNodes];
inline constexpr int kPartNode[kParts] = {kNCore, kNRightNeji, kNRightFyubi1, kNLeftNeji, kNLeftFyubi1};

// The attack codes of the sequence (switch table 0x5215a4, §4.1).
enum Code : int {
    kCodeNop = 0, kCodeSkip = 1, kCodeRoll = 2, kCodePunch = 3, kCodeSwing = 4, kCodeBeat = 5,
    kCodeMaguma = 6, kCodeTeeth = 7, kCodeNagi = 0xb, kCodeFire = 0xc, kCodeBeatPattern = 0xd,
};
inline constexpr int kPhaseSpecial = 4;   // blk+0x148 == 4: special blk+0x4a8 (8, 9, 10, 11)

// The 41-code sequence the ctor writes, with its per-difficulty edits (§4.3 [BYTES
// 0x520df9..0x52102e]): Easy idx 10 -> PUNCH; Normal/Hard/Easy idx 34 -> skip (but not
// Easy with bb5c == 2); Nightmare (not Easy) idx 17..20 and 34..38.
std::array<int, kSeqLen> sequence(bool easy, int diff_index);

// The 22 effect ids at blk+0x550 (the death's kill list, §8.4) and the 27 SEs the ctor
// preloads (0x69d134).
extern const std::uint16_t kFxIdList[kFxIds];
extern const std::uint16_t kSePreload[27];

// The S_3099.SOB atari markers (x, z), y -1.5 [DATA, the scratch SOB dump agentD §1.1].
// S_3099_1.SOB (Hard / Nightmare) moves 18 of them by < 1 unit; a host should read the SOB.
extern const float kAtariS3099[kRings][2];
std::array<Vec3, kRings> atari_s3099();
std::string ring_name(int k);    // "ring%.2d"
std::string atari_name(int k);   // "atari%.2d"

// [STAND-IN for the camera code] FUN_00427c90's shake step: while `frames` != 0 it draws 3
// times from the shared RNG (x, y, z) and returns ((2u - 1) * amp) per axis, frames -= 1;
// otherwise it draws nothing and returns 0 [EXPORT 00427c90.c:104-121]. A host that runs
// Gelaldy must call this (or its own port of that camera code) once per frame.
Vec3 camera_shake(int& frames, const Vec3& amp, const std::function<std::uint32_t()>& rng);

}  // namespace boss03

// The engine globals the boss code keeps as statics (not per instance, §12 item 13). They
// survive a retry in the original (0x746f34 is never reset), so the host keeps ONE of these
// for the process (boss03_statics()) unless it wants otherwise.
struct Boss03Statics {
    float roll_acc = 0;             // 0x7d9b94: ROLL's growing turn accumulator
    int punch_side = 0;             // 0x7ddc28: PUNCH arm, 1 right / 2 left (guard byte 0x527 + it)
    bool nagi_right = false;        // byte 0x7ddc2c: NAGI spin direction (true = the R clips)
    int nagi_n = 0;                 // 0x7d9b90: NAGI frame count (re-arms the loop SE every 100)
    std::array<int, boss03::kRings> pillar_cd{};   // 0x7ddc30[36]: FIRE pillar cooldowns
    float death_cam_y = 0;          // 0x7ddc18: final-phase camera target height
    float death_cam_h = 0;          // 0x7ddc1c: final-phase eye height
    int debris_t = 0;               // 0x7ddc20: stutter debris cadence
    int armor_latch = 0;            // 0x7ddc24: 507 latch index (0..4; 4 never fires)
    float debris_period = 10.0f;    // 0x746f34 (initial 10.0, eased toward 30, never reset)
};
Boss03Statics& boss03_statics();    // the process-wide instance

// What the caller reads from EEX record 2 and the game flags (§2.1, §2.4).
struct Boss03Setup {
    // EEX record 2 via FUN_00443b80 [DATA EEX.DAT]: (2000, 150, 140, 50, 140, 140, lv 24).
    float eex_hp = 2000, eex_str = 150, eex_def = 140, rec3 = 50, rec4 = 140;
    int level = 24;
    float exp_b0 = 0;               // +0xb0 (the EXP formula's factor; the caller's GAMEINFO curve)
    int character = 0;              // g_flags[150]: 0 Adol, 1 Yunica, 2 Hugo, 3 Toal
    int diff_index = 0;             // g_flags[144] (bb5c): 0 Normal, 1 Hard, 2 Nightmare
    bool diff_easy = false;         // g_flags[143] (bb58)
    bool diff_very_easy = false;    // g_flags[203] (bc48)
    bool boss_rush = false;         // g_flags[193] (bc20)
    bool flag201 = false;           // g_flags[201] == 1 (bc40) [H U12: a Nightmare-like switch]
    int retries = 0;                // g_flags[196] (bc2c)
    Vec3 pos{0, -3.5f, 0};          // Mesh_Locate('boss03', 0, -3.5, 0)
    Vec3 player_pos{0, -1.5f, -17.477f};   // the ctor's first bearing (§2.1 item 11)
    // The atariNN scene objects (positions; FUN_00527910 forces their y to -1.8). A missing
    // object returns (0, 0, 0).
    std::array<Vec3, boss03::kRings> atari = boss03::atari_s3099();
    std::array<bool, boss03::kRings> atari_present = all_true();
    float ring_y0 = -1.5f;          // every ringNN at (0, -1.5, 0) [DATA SOB]
    Boss03Statics* statics = nullptr;   // nullptr = boss03_statics()

    static std::array<bool, boss03::kRings> all_true() {
        std::array<bool, boss03::kRings> a{};
        a.fill(true);
        return a;
    }
};

struct Boss03Input {
    // ---- the player (DAT_0074c09c) ----
    Vec3 player_pos{};              // +0x50
    float player_hp = 1;            // +0x98 (= 0x76a730, which the 222 test reads)
    // ---- flags ----
    bool fight = false;             // g_flags[156] == 1 (the brain does nothing otherwise)
    // ---- the model (posed before the brain, FUN_005f6000) ----
    int clip = boss03::kAll;        // *(obj+0x26c)+0x18: current section
    bool clip_done = false;         // *(obj+0x26c)+0x1c != 0 (a stop clip ended / a loop wrapped)
    float clip_frame = 0;           // *(*(M+0x29c)+0x20): the ABSOLUTE YMO frame
    std::array<Vec3, boss03::kNodes> nodes{};   // world translation of each named node
    // ---- the world ----
    int perrie_count = 0;           // live actors with cast 0x3a in 0x730098 (FUN_005279f0)
    // The current room-camera slot 0x76a150 + 0x454*[0x76a158]: pitch +0x270, up-offset
    // +0x410, eye distance +0x274 (the brain eases / writes them, Output::camera).
    float cam_pitch = 0, cam_up = 0, cam_dist = 0;
    Vec3 shake_amp{};               // 0x76a594..0x76a59c (the death decays them)
    std::function<std::uint32_t()> rng;   // ONE xorshift128 advance; returns the new word
};

// ---- the request vocabulary ----
// FUN_005f5c80(section = clip, blend) (start = the section start [H]); hard: the death's
// FUN_005f5e10 re-set (+0x1c = 0) followed by a direct frame write *(+0x20) = frame.
struct Boss03ClipSet { int clip = boss03::kWait1; int blend = 1; bool hard = false; std::optional<float> frame; };
struct Boss03Sound {
    enum Kind { k3D, k2D, kStop, kFade } kind = k3D;
    int id = 0;
    Vec3 pos{};          // k3D: FUN_0043a080(ecx = id, edx = &pos)
    int param = 0;       // k2D: FUN_005ce2a0 flags (0x400 / 0x233); kStop: FUN_005cdc40(6, id) +0x10 = 100;
                         // kFade: FUN_0043a1c0(id, 50)
};
// A GoEffect: the inline FUN_004523e0 pattern or FUN_00450410 / FUN_00450880.
struct Boss03Effect {
    int id = 0;
    Vec3 pos{}, rot{}, scale{1, 1, 1};
    enum Stat { kNone, kBoss } stat = kBoss;   // kBoss: the boss's obj+0x94..0xcf block arms the EF2 emitters
    float power = 0, knock = 1;    // power = the additive ATK bonus (eff+0x1c4 inline) [H U2]
    int rtype = 0;                 // eff+0x1ec inline / FUN_00450410's rtype
    int node = -1;                 // attached (eff+0x1a0 = that node's record, +0x1d5 = 1); -1 free
    enum Slot { kNoSlot, kCore } slot = kNoSlot;   // blk+0x584 keeps the handle (377 / 499)
};
// Kill effects in order with the spawns: apply Output::effects[0 .. effects_before) first.
struct Boss03Kill {
    enum What { kId, kCoreSlot } what = kId;   // kId: every live GoEffect with id (+0x11 |= 2, +0x60 &= ~0x80)
    int id = 0;                                // kCoreSlot: the blk+0x584 effect (+0x1d4 = 1)
    std::size_t effects_before = 0;
};
// FUN_00475710(cast 0x3a, yaw degrees, pos, level) + the brain's writes on the actor (§7).
struct Boss03Spawn {
    int cast = 0x3a;
    int yaw_deg = 0;
    Vec3 pos{};
    int level = 24;               // obj+0xc8
    float d330 = -0.1f;           // A+0x330 += d330 (falls out of the mouth)
    std::uint32_t or130 = 9;      // A+0x130 |= 9 (no drops, no death-effect block)
    std::uint32_t or12c = 0x10a0000;   // A+0x12c |= (never culled, stun-immune, 0x20000 [U10])
};
// Scene object ringNN: y (+0x54) = y. Missing rings go to scene object 0 (literal) — skip them.
struct Boss03RingMove { int ring = 0; float y = 0; };
// 0x76a5ac = frames / 0x76a594..59c = amp (fields set only when written).
struct Boss03Shake { std::optional<int> frames; std::optional<Vec3> amp; };
struct Boss03ZoomBlur { std::optional<float> strength, rate; bool snapshot = false; };   // 0x76a550 / 554 / 0x7b2488 = 1
// Writes to the current camera slot (BOSS_CAMERA.md §1b), fields set only when written.
struct Boss03Camera {
    std::optional<float> pitch, up, dist;      // +0x270 / +0x410 / +0x274
    std::optional<Vec3> target;                // +0xb4
    std::optional<int> target_mode;            // +0x94
    std::optional<Vec3> eye;                   // +0x288 (absolute: RotY(a)*(0,0,d), no boss offset)
    std::optional<int> eye_mode;               // +0x268
    std::optional<int> eye_flags;              // +0x278
};
struct Boss03Gauge { float hp = 0, max = 0, len_scale = 5500.0f; int emblem = 3; };   // FUN_0043a460 (0x6d5fd4)
// The world-anchored arm mini-gauge 0x76c1cc..0x76c1e0 (§3.2, HUD_LAYOUT item 10).
struct Boss03ArmGauge { int timer = 0; float hp = 0, max = 0; Vec3 pos{}; bool right = false; };
// The rewards at the death's frame 550 (not in boss rush) [BYTES 0x522653..0x5226ed]:
// award (b0 != 0): FUN_004fa2f0(level) * b0 * 0x76a5fc * mult, FUN_004217f0, max 1, added to
// 0x76a748 (+ 0x76b914 |= 1, FUN_00420c40); drop (obj+0xc4 != 0): FUN_004fa390 -> FUN_004fa290.
struct Boss03Exp { bool award = false; int level = 24; float b0 = 0; float mult = 1.0f; bool drop = false; };
struct Boss03Quat { float x = 0, y = 0, z = 0, w = 1; };
// The procedural node overrides (visual): index 0 = right, 1 = left.
struct Boss03Pose {
    std::array<bool, 2> arm_written{};         // FUN_00523480 (right) / FUN_00523670 (left) ran
    std::array<Boss03Quat, 2> neji{};          // RightNeji / LeftNeji record +0x15c (+0x104 = 1)
    std::array<float, 2> forearm_dz{};         // ForeArm2 +0x40 = T(0, 0, rest z (+0x138) + dz)
    std::array<float, 2> forearm_alt_dy{};     // ForeArm2 +0xc0 = T(0, dy, rest z + dz) [U3 consumer]
    std::array<bool, 2> forearm_gone{};        // broken: both = T(0, -5000, 5000)
    Boss03Quat neck{};                         // Neck4 +0x15c (+0x104 = 1)
    std::optional<Vec3> head_pos;              // Head's world translation := Saru's (bug, U9)
};

struct Boss03Output {
    std::vector<Boss03ClipSet> clips;          // in order; the last one is current
    std::optional<float> anim_speed;           // *(*(M+0x29c)+0x24)
    std::vector<Boss03Sound> sounds;
    std::vector<Boss03Effect> effects;
    std::vector<Boss03Kill> kills;
    std::vector<Boss03Spawn> spawns;
    bool kill_perries = false;                 // every live cast-0x3a actor: HP = 0 (§8.2)
    std::vector<Boss03RingMove> rings;
    std::vector<int> atari_lowered;            // atariNN +0x54 = -1.8 (reported once per object)
    std::vector<int> rumble;                   // FUN_005d3500(n) — pad, ignorable
    Boss03Shake shake;
    Boss03ZoomBlur zoom;
    std::optional<std::pair<float, float>> flash;   // 0x76a548 / 0x76a54c
    std::optional<float> time_scale;           // 0x6d6078 (0.875 during the death stutters)
    Boss03Camera camera;
    std::vector<std::pair<int, int>> flags;    // g_flags writes: 134, 222 (156 = 0 in boss rush)
    bool player_iframes = false;               // player +0x734 = 9999
    Vec3 player_push{};                        // add to player +0x50 (the head push-out, §9)
    Vec3 player_push_vel{};                    // add to player +0x338..0x340
    bool clear_buffs = false;                  // 0x76c19c/1a0/1a4 = 0, byte 0x76a840 = 1
    bool steam_achievement = false;            // FUN_00403cd0
    std::optional<Boss03Exp> exp;
    bool destroy = false;                      // alpha 0, obj+0x11 |= 2, obj+0x60 &= ~0x80
    std::optional<Boss03Gauge> gauge;          // only when HP changed
    bool gauge_hide = false;                   // DAT_006d5fd8 = 0
    std::optional<Boss03ArmGauge> arm_gauge;   // the driver's write of the mini-gauge globals
    std::optional<Boss03Pose> pose;            // the tail's node overrides
};

// One GoXx hurt / attack part (§2.3). Field names carry the GoXx offsets (HITVOLUME.md).
struct Boss03Part {
    int node = 0;                 // boss03::kPartNode[i]
    float hp = 0, max_hp = 0;     // +0x98 / +0x94
    float str = 0, def = 0;       // +0xa4 / +0xa8 (the softened body block, copied at the ctor)
    float resist[4] = {};         // +0xb4..+0xc0
    float dmg = 0;                // +0x134: the hit path's accumulated damage, consumed by the driver
    Vec3 pos{};                   // +0x50
    float hurt_radius = 0;        // +0x68 (0 = cannot be hit)
    Vec3 hurt_centre{};           // +0x6c (y -1000 = guarded / buried)
    bool armed = false;           // +0x19c
    float radius = 0;             // +0xd8
    Vec3 centre{};                // +0xdc
    float half_h = 0;             // +0xe8 (NOT reset per frame)
    int shape = 0;                // +0xec: 0 sphere, 1 vertical cylinder
    float power = 1.0f;           // +0xfc
    float f100 = 1.0f;            // +0x100
    int rtype = 2;                // +0x108
    std::uint32_t flags = 0;      // +0x11c
};

class Boss03 {
public:
    explicit Boss03(const Boss03Setup& s);

    // The ctor's requests (§2.1 items 10..12): clip WAIT1 and the core glow 377 (slot kCore).
    // (It also sets the HP-bar emblem 0x76c214 = 3: Boss03Gauge::emblem.) The host runs them
    // once, after construction.
    const Boss03Output& ctor_requests() const { return ctor_; }

    // One engine frame: the brain FUN_00521130. A no-op once destroyed.
    Boss03Output tick(const Boss03Input& in);

    // ---- damage in (between ticks) ----
    // FUN_004faf20 on part i: +0x134 += dmg (the int damage), HP -= dmg_hp (the same damage
    // times the element bonus 1 + 0.1 g_flags[0xb6..0xb8]), HP < 1 -> 0 [EXPORT
    // 004faf20.c:303,395-398]. The driver then subtracts +0x134 from an arm's HP AGAIN (G9).
    void add_part_damage(int i, float dmg) { add_part_damage(i, dmg, dmg); }
    void add_part_damage(int i, float dmg, float dmg_hp);
    // The hit path's gate: HP >= 1 and a hurt radius (geometry: hurt_centre y -1000 = buried).
    bool part_hittable(int i) const;

    // ---- queries ----
    const Boss03Part& part(int i) const { return parts_[std::size_t(i)]; }
    float hp() const { return hp_; }                  // obj+0x98 (the CORE's damage pool)
    float max_hp() const { return max_hp_; }          // obj+0x94 (1500)
    float str() const { return str_; }                // obj+0xa4
    float def() const { return def_; }                // obj+0xa8
    int level() const { return level_; }
    Vec3 pos() const { return pos_; }
    float facing() const { return facing_; }          // blk+0x4cc (atan2 convention)
    float yaw() const { return yaw_; }                // blk+0x4c8: the body matrix RotY
    Mat4 matrix() const;                              // obj+0x20
    float bearing() const { return bearing_; }        // blk+0x4d0 (last tail's)
    float dist() const { return dist_; }              // blk+0x4f0
    int phase() const { return phase_; }              // blk+0x148
    int special() const { return special_; }          // blk+0x4a8
    int seq_index() const { return idx_; }            // blk+0x4a4
    int code() const { return seq_[std::size_t(idx_)]; }
    const std::array<int, boss03::kSeqLen>& seq() const { return seq_; }
    int sub() const { return sub_; }                  // blk+0x4c4
    int counter() const { return c4ac_; }             // blk+0x4ac (death: final-phase frame)
    int stutter() const { return c4b0_; }             // blk+0x4b0
    int loops() const { return loops_; }              // blk+0x150
    int core_damage() const { return acc_; }          // blk+0x4b8
    bool arm_alive(int a) const { return alive_[std::size_t(a)]; }   // 0 right (0x524) / 1 left (0x525)
    bool arm_guard(int a) const { return guard_[std::size_t(a)]; }   // 0x528 / 0x529
    bool core_guard() const { return core_guard_; }   // 0x527
    bool core_exposed() const { return core_exposed_; }   // 0x583
    bool core02() const { return core02_; }           // 0x588
    bool stagger_pending() const { return stagger_; } // 0x52a
    bool strike() const { return strike_; }          // 0x526
    bool wave_on() const { return wave_on_; }         // 0x4a0
    int intro() const { return intro_; }              // blk+0x598
    float dmg_offset() const { return dmg_off_; }     // blk+0x590
    int fire_n(int k) const { return k ? fire_n13_ : fire_n02_; }   // 0x534 / 0x538
    int pillar_every(bool aimed) const { return aimed ? pillar_aim_ : pillar_rand_; }   // 0x540 / 0x53c
    int pillar_cooldown() const { return pillar_cd_; }   // 0x544
    int short_hold() const { return short_hold_; }    // 0x47c
    int near_steps() const { return near_steps_; }    // 0x498
    int perrie_n() const { return perrie_n_; }        // 0x548
    int punch_aim() const { return punch_aim_; }      // 0x4ec
    float nagi_far() const { return nagi_far_; }      // 0x4e0
    float nagi_near() const { return nagi_near_; }    // 0x4e4
    int ring_state(int k) const { return ring_state_[std::size_t(k)]; }   // 0 up, 2 sinking, 3 held, 4 rising
    int ring_hold(int k) const { return ring_hold_[std::size_t(k)]; }
    float ring_y(int k) const { return ring_y_[std::size_t(k)]; }
    float flash() const { return flash_; }            // obj+0x2a0 (core hits: 1.65)
    float alpha() const { return alpha_; }            // obj+0x418
    bool contact_alt() const { return (obj11c_ & 8u) != 0; }   // obj+0x11c ^= 8 every driver frame
    const Boss03ArmGauge& arm_gauge() const { return arm_gauge_; }
    bool destroyed() const { return destroyed_; }
    const Boss03Statics& statics() const { return *st_; }

private:
    // the brain pieces (FUN_00521130 order)
    void brain();
    void driver();                    // FUN_005227e0
    void next_attack();               // FUN_0051ea30
    void enter_special(int n);        // FUN_0051e880
    void special_regrow(int n);       // specials 8 / 10 (inline in the brain)
    void arm_break();                 // FUN_00525300 (special 9)
    void wave_tick();                 // 0x521469..0x5214f0
    void wave(int k, int s);          // FUN_00527280
    void ring_motion();               // FUN_005275f0
    void arm_pose(int a);             // FUN_00523480 (0) / FUN_00523670 (1)
    void tail();                      // FUN_0051f520
    void neck();                      // FUN_00527b50
    void death();                     // FUN_005215e0
    void death_stutter();
    void death_final();
    void debris(bool final_phase);    // the inline 7-draw FX 506 burst
    // the attacks
    void roll();                      // 2   FUN_00523880
    void punch();                     // 3   FUN_00523b30
    void swing();                     // 4   FUN_00523fd0
    void beat();                      // 5 / 0xd FUN_00524240
    void maguma();                    // 6   FUN_00524930
    void teeth();                     // 7   FUN_00524f10
    void nagi();                      // 0xb FUN_00525940
    void fire();                      // 0xc FUN_00526510
    void pillar(int c, float theta);  // FIRE's pillar spawn

    // helpers
    std::uint32_t draw();
    std::uint32_t R(std::uint32_t n) { return (draw() >> 17) % n; }
    float rel() const;                // the inline wrap(fold(bearing) - fold(facing))
    bool rel_within(float lo, float hi) const { const float d = rel(); return lo <= d && d <= hi; }
    void turn(float rate);            // FUN_0051f420
    void set_yaw_from_facing();
    bool frame_in(float lo, float hi) const { return lo <= frame_ && frame_ <= hi; }
    void set_clip(int clip);
    Vec3 node(int n) const { return in_->nodes[std::size_t(n)]; }
    Vec3 atari(int k);                // FUN_00527910 (forces the marker's y to -1.8)
    int sector_of(float a) const;     // the 10-degree ring sector loops (BEAT / NAGI / FIRE)
    void node_fx(int id, int node, int latch, bool attach, float power, int rtype);   // FUN_0051f280
    Boss03Effect& fx(int id, const Vec3& p);
    void kill_id(int id) { out_->kills.push_back({Boss03Kill::kId, id, out_->effects.size()}); }
    void kill_core_slot();
    void se3d(int id, const Vec3& p) { out_->sounds.push_back({Boss03Sound::k3D, id, p, 0}); }
    void se2d(int id, int flags) { out_->sounds.push_back({Boss03Sound::k2D, id, {}, flags}); }
    void se_latch(int id, const Vec3& p, int latch);   // FUN_0051f390 / the inline Lse sites
    void shake(int frames, const Vec3& amp);
    void zoom(float s, float r) { out_->zoom.strength = s; out_->zoom.rate = r; }
    void flag(int n, int v) { out_->flags.push_back({n, v}); }
    void latches_reset() { lfx_.fill(true); lse_.fill(true); lrb_.fill(true); }
    void ring_set_y(int k, float y);

    // ---- setup / flags ----
    Boss03Statics* st_ = nullptr;
    int char_ = 0, bb5c_ = 0;
    bool easy_ = false, very_easy_ = false, rush_ = false, flag201_ = false;
    std::array<Vec3, boss03::kRings> atari_{};
    std::array<bool, boss03::kRings> atari_present_{};
    std::array<bool, boss03::kRings> atari_lowered_{};

    // ---- obj ----
    Vec3 pos_{};
    float max_hp_ = 0, hp_ = 0, str_ = 0, def_ = 0, rec3_ = 0, exp_b0_ = 0, rec4_ = 0;
    int level_ = 0;
    float flash_ = 0;             // obj+0x2a0
    float bar_last_ = -1.0f;      // obj+0x37c [H: starts different from HP]
    float alpha_ = 1.0f;          // obj+0x418
    std::uint32_t obj11c_ = 0;    // obj+0x11c
    bool destroyed_ = false;
    std::array<Boss03Part, boss03::kParts> parts_{};

    // ---- blk (offsets in comments) ----
    std::array<int, boss03::kSeqLen> seq_{};   // 0x000
    int phase_ = 0;               // 0x148
    bool gauge_right_ = false;    // 0x14c
    int loops_ = 0;               // 0x150
    std::array<int, boss03::kRings> ring_state_{};   // 0x1e4
    std::array<int, boss03::kRings> ring_timer_{};   // 0x274
    std::array<int, boss03::kRings> ring_hold_{};    // 0x304
    std::array<bool, boss03::kRings> ring_394_{};    // 0x394 (no reader, U7)
    std::array<bool, boss03::kRings> ring_latch_{};  // 0x3b8 "not yet struck"
    std::array<int, boss03::kRings> se238_{};        // 0x3dc
    std::array<float, boss03::kRings> ring_y_{};     // the scene objects' +0x54 (mirror)
    int wave_base_ = 0;           // 0x46c
    int wave_var_ = 0;            // 0x470
    bool beat_pending_ = false;   // 0x474
    int blk478_ = 5;              // 0x478: wave step period 5 (death: frame counter)
    int short_hold_ = 0;          // 0x47c
    int nagi_hold_ = 200;         // 0x480
    float sink_speed_ = 0.1f;     // 0x488
    float sink_bottom_ = -2.75f;  // 0x48c
    int wave_frame_ = 0;          // 0x490
    int wave_step_ = 0;           // 0x494
    int near_steps_ = 0;          // 0x498
    int near2_ = 2;               // 0x49c
    bool wave_on_ = false;        // 0x4a0
    int idx_ = 1;                 // 0x4a4
    int special_ = 0;             // 0x4a8
    int c4ac_ = 0, c4b0_ = 0, c4b4_ = 0;   // 0x4ac / 0x4b0 / 0x4b4
    int acc_ = 0;                 // 0x4b8
    int smoke_t_ = 0;             // 0x4bc
    int c4c0_ = 0;                // 0x4c0 (per-attack clip choice)
    int sub_ = 0;                 // 0x4c4
    float yaw_ = 0, facing_ = 0, bearing_ = 0;   // 0x4c8 / 0x4cc / 0x4d0
    float turn_rate_ = 0.04f;     // 0x4d4
    float f4d8_ = 0.02f;          // 0x4d8 (no reader found)
    float nagi_w_ = 0;            // 0x4dc
    float nagi_far_ = 0, nagi_near_ = 0;   // 0x4e0 / 0x4e4
    float nagi_m_ = 0;            // 0x4e8
    int punch_aim_ = 0;           // 0x4ec
    float dist_ = 0;              // 0x4f0
    float screw_rate_ = 35.0f;    // 0x4f4
    float slide_rate_ = 1.5f;     // 0x510
    std::array<float, 2> screw_tgt_{}, screw_base_{}, screw_{};   // 0x4f8/0x4fc, 0x500/0x504, 0x508/0x50c
    std::array<float, 2> slide_tgt_{}, slide_{};                  // 0x514/0x518, 0x51c/0x520
    std::array<bool, 2> alive_{true, true};    // 0x524 right / 0x525 left
    bool strike_ = false;         // 0x526
    bool core_guard_ = true;      // 0x527
    std::array<bool, 2> guard_{};  // 0x528 right / 0x529 left
    bool stagger_ = false;        // 0x52a
    float sector_w_ = 10.0f;      // 0x530
    int fire_n02_ = 0, fire_n13_ = 0, pillar_rand_ = 0, pillar_aim_ = 0, pillar_cd_ = 0, perrie_n_ = 0;   // 0x534..0x548
    std::array<bool, 4> lfx_{};   // 0x54c..0x54f
    std::array<bool, 4> lse_{};   // 0x57c..0x57f
    std::array<bool, 2> lrb_{};   // 0x580..0x581
    bool core_exposed_ = false;   // 0x583
    bool core_fx_ = false;        // 0x584 != 0
    bool core02_ = false;         // 0x588
    float dmg_off_ = 0;           // 0x590
    float neck_ = 0;              // 0x594
    int intro_ = 0;               // 0x598
    Boss03ArmGauge arm_gauge_;    // 0x76c1cc..0x76c1e0 (engine globals, mirrored)
    std::array<Boss03Quat, 2> neji_q_{};   // the Neji records' +0x15c (persist across frames)

    Boss03Output ctor_;

    // ---- this tick ----
    const Boss03Input* in_ = nullptr;
    Boss03Output* out_ = nullptr;
    Boss03Pose* pose_ = nullptr;
    int clip_ = 0;
    bool done_ = false;
    float frame_ = 0;
    Vec3 player_{};
    float player_hp_ = 1;
    bool f156_ = false;
    int perries_ = 0;
    float cam_pitch_ = 0, cam_up_ = 0, cam_dist_ = 0;
    Vec3 shake_amp_{};
};

}  // namespace cleria
