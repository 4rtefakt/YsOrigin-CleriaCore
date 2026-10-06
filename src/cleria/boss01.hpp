// Velagunder ("boss01", room S_1099) — the boss object's logic, pure and headless.
//
// Ported from build/boss_re/VELAGUNDER.md (the verified spec; § numbers below are its
// sections), re-checked against the Ghidra export of the functions it names:
//   ctor FUN_00505810 (+ EEX reader FUN_00443b80), brain FUN_005062d0, parts/damage
//   FUN_005084a0, player push FUN_005061e0, decision FUN_005095c0, states FUN_00509df0 (1),
//   FUN_0050b650 (2, side chooser FUN_0050b390), FUN_0050ea90 (3), FUN_0050f280 (4),
//   FUN_00510730 (5), FUN_005116f0 (7), FUN_00512630 (8), FUN_005093e0 (9), FUN_00509370
//   (10), common tail FUN_00512720.
//
// The world is reached only through Boss01Input (what the engine globals / the player /
// the posed model hold this tick) and Boss01Output (a vocabulary of requests the host
// executes after the tick). Nothing here touches a renderer, the EF2 sim or the actor
// pool, so apps/cleria-bosstest drives the whole fight headless.
//
// Tags in comments: addresses are [BYTES]/[EXPORT] as in the spec; [H] = inference;
// [UNKNOWN] = not resolved by the spec (the gap is named where it bites); [STAND-IN] = a
// host-side helper that is NOT engine code.
//
// RNG: every draw goes through Boss01Input::rng, which must be the game's ONE shared
// xorshift128 (0x74622c..38) returning the new word w; R(n) = (w >> 17) % n. The draw
// order of every state was checked against the export (all the seed save/restore dances
// in the decompile reduce to plain sequential draws).
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

namespace boss01 {

inline constexpr int kParts = 21;   // obj+0x278
inline constexpr int kBones = 24;   // blk+0x00..0x5c
inline constexpr int kClips = 22;   // blk+0x60..0xb4

// The clip handle table blk+0x60..0xb4, in blk order (names: ptr table 0x68b3f0).
enum Clip : int {
    kWait = 0, kRunR, kRunREnd, kRunL, kRunLEnd, kAtk01R, kAtk01L,
    kAtk04_1, kAtk04_2, kAtk04_3, kAtk05, kAtk06, kAtk07_1, kAtk07_2, kAtk07_3,
    kAction01, kPiyo1, kPiyo2, kPiyo3, kAtk10_1, kAtk10_2, kAtk10_3,
};
// Frame range (inclusive) and loop flag per clip, from BOSS01.YMO via cleria-animtest
// (§2.3 [DATA]). Every clip runs at 0.5 frames/step x the anim speed (anim+0x24).
struct ClipInfo { const char* name; int start, end; bool loop; };
extern const ClipInfo kClipTable[kClips];

// The 24 named bone handles blk+4*i (FUN_005f4e10 lookups, ptr table 0x68b448):
// i 0..15 = Null(227)..Null(242); 16 Null(190), 17 Null(197), 18 Null(213), 19 Null(215),
// 20 Null(217), 21 Null(219), 22 Null(221), 23 Null(223).
extern const char* const kBoneNames[kBones];
// Part i sits on bone kPartBone[i] (DAT_0069cc58 [BYTES]).
extern const int kPartBone[kParts];

// The camera region the boss selects (DAT_0076a158 / 15c / 1bc / 1b8, §4.4). These are
// engine GLOBALS the region camera owns; Boss01 keeps a mirror because the brain both
// writes and reads them (state 10 waits for t == 1.0, the stun entry tests cur != 5).
}  // namespace boss01

struct Boss01Camera {
    int cur = 1;          // DAT_0076a158: current region preset (1 fight, 2 inhale, 3 bubbles,
                          // 4 sweep, 5 stun/death, 6 laser, 0xc cutscene, 0xb stun snapshot)
    int prev = 1;         // DAT_0076a15c
    float t = 1.0f;       // DAT_0076a1bc: blend parameter, 1.0 = settled
    float rate = 0.015f;  // DAT_0076a1b8: blend increment per step
    int forced = -1;      // DAT_0076a1f8: scripted forced region (-1 = none)
    // [STAND-IN] what the region camera (FUN_0042a3d0) does to t each step as CAMERA.md
    // describes it (t += rate, settled at 1.0). A host that runs the real region camera
    // writes its own t into Boss01::camera() before each tick instead.
    void step_standin() { t += rate; if (t > 1.0f) t = 1.0f; }
};

// What the caller reads from EEX record 0 and the game flags (§2.2).
struct Boss01Setup {
    float hp = 700.0f;    // EEX rec0 [0] -> +0x94/+0x98
    float str = 69.0f;    // [1] -> +0xa4
    float def = 53.0f;    // [2] -> +0xa8
    float rec3 = 50.0f;   // [3] -> +0xac
    float rec4 = 140.0f;  // [4] -> +0xc4 = (float)(int)rec4
    int level = 8;        // [6] -> +0xc8
    float exp_b0 = 0;     // +0xb0 = f(level) from the GAMEINFO curve (FUN_00443b80); the
                          // caller computes it, the brain never reads it (EXP award, §8.2)
    int character = 1;    // DAT_0076bb74 = g_flags[150]: 1 Yunica, 2 Hugo, 3 Toal, 0 (= 1)
    int diff_index = 0;   // DAT_0076bb5c = g_flags[144]: 0 Normal, 1 Hard, 2 Nightmare
    bool diff_easy = false;       // DAT_0076bb58 = g_flags[143] (set for Easy AND Very Easy)
    bool diff_very_easy = false;  // DAT_0076bc48 = g_flags[203]
    bool time_attack = false;     // DAT_0076bc20 = g_flags[193]
    int retries = 0;              // DAT_0076bc2c = g_flags[196] (continue count)
    Vec3 pos{0, 2, 33};           // Mesh_Locate position (obj+0x50)
    Vec3 player_pos{};            // blk+0x180..0x188 at construction
};

// One of the 21 GoXx kind-9 body parts (§3.1/§3.2). Offsets are the GoXx's.
struct Boss01Part {
    int bone = 0;                 // kPartBone[i]
    bool alive = true;            // cleared on the boss's death (+0x11 |= 2, +0x60 &= ~0x80)
    float hp = 0;                 // +0x98 (parts 0/1 refilled to maxHP every frame)
    float str = 0, def = 0;       // +0xa4 / +0xa8 (copied from the boss at ctor; DEF 999 = armour)
    float resist[4] = {};         // +0xb4..+0xc0 (hit.hpp EnemyDefense::resist)
    bool armor = false;           // +0x138 (skips the elemental bonus multiplier on hit)
    float dmg = 0;                // +0x134: damage accumulated since the last brain tick
    Vec3 pos{};                   // +0x50
    float hurt_radius = 0;        // +0x68 (0 = cannot be hit)
    Vec3 hurt_centre{};           // +0x6c
    // The armed hit volume this frame (boss -> player), field names as HITVOLUME.md.
    bool armed = false;           // +0x19c
    float radius = 0;             // +0xd8
    Vec3 centre{};                // +0xdc
    float half_h = 0;             // +0xe8 (NOT reset per frame; only meaningful when armed)
    int shape = 0;                // +0xec: 0 sphere, 1 cylinder
    float power = 4.0f;           // +0xfc
    int rtype = 2;                // +0x108
    std::uint32_t flags = 0;      // +0x11c: bit 2 knock angle from boss facing, bit 8 FUN_004fe1a0
};

// An actor of the engine list DAT_00730098 (engine order) as states 4 / 9 see it. The
// player is in it as cast 0x0d.
struct Boss01Actor {
    int cast = 0;        // +0x124
    Vec3 pos{};          // +0x50
    float hp = 1;        // +0x98
};
// A GoXx bullet of DAT_00730248 (state 4 swallows the bubbles).
struct Boss01Bubble {
    int effect = 0;      // +0x128
    Vec3 pos{};          // +0x12c
};

struct Boss01Input {
    // ---- the player (DAT_0074c09c) ----
    bool player_present = true;     // DAT_0074c09c != 0
    Vec3 player_pos{};              // +0x50
    float player_hp = 1;            // +0x98 (the death end block reads 0x76a730, the same value)
    bool player_grounded = true;    // +0x388
    float player_floor = 0;         // +0x318
    int player_surface = 0;         // byte +0x4b9 (surface type under the feet)
    int player_section = 0;         // +0x994 (Toal's laser aim freezes on 4 / 0xb [UNKNOWN meaning])
    bool player_took_damage = false;  // +0x3bc & 2 this tick
    // ---- the posed model (evaluated before the brain, FUN_005f6000) ----
    std::array<Vec3, boss01::kBones> bones{};  // world translation of each blk handle
    Vec3 bone114{};                 // "Bone(114)" (mouth: inhale effect, drool, spit)
    Vec3 bone115{};                 // "Bone(115)" (blk+0x1a0 copy; the drool attaches to it)
    // ---- the clip as the mesh layer reports it (after this frame's advance) ----
    int clip = boss01::kWait;       // current handle (*(obj+0x26c)+0x18) as a Clip id
    std::uint32_t clip_count = 0;   // +0x1c completion count (reset by set-clip)
    float clip_frame = 0;           // +0x20 current frame
    // ---- globals ----
    bool fight = false;             // DAT_0076bb8c = g_flags[156] == 1
    bool continue_snap = false;     // DAT_0076bbd8 = g_flags[175] != 0 (continue / time-attack)
    int spawn_count = 0;            // CASTINFO[0xaf]+0x70: live + fading M890 count
    bool spawn_chaser_exists = false;  // any 0xaf actor with +0x72c == 1 (state 1's seed rule)
    std::vector<Boss01Actor> actors;   // DAT_00730098 (state 4 pull/eat); may be empty otherwise
    std::vector<Boss01Bubble> bubbles; // DAT_00730248 (state 4)
    std::function<std::uint32_t()> rng;   // ONE xorshift128 advance; returns the new word
};

// ---- the request vocabulary ----
struct Boss01ClipSet { int clip = 0; int blend = 1; };   // FUN_005f5c80(handle, blend)
struct Boss01Sound {
    enum Kind { k3D, k2D, kStop } kind = k3D;
    int id = 0;
    Vec3 pos{};          // k3D: FUN_0043a080 position
    int param = 0;       // k2D: FUN_0043a040 flags (0x400); kStop: SE_Set1 value (0/200/1000)
                         // or 0 for the FUN_005ce3d0 / inline stop
};
// FUN_00450880(id, pos, rot, scale, src 0, side, stat, power, knock, rtype, status, cls).
struct Boss01Effect {
    int id = 0;
    Vec3 pos{}, rot{}, scale{1, 1, 1};
    int side = 1;
    enum Stat { kNone, kBoss, kSpawn } stat = kBoss;   // whose +0x94 stat block
    float power = 0, knock = 1;
    int rtype = 0, status = 0, cls = -1;
    // FUN_00451c20 attach (mode 1/2) — offsets are the engine's, relative to the parent.
    enum Attach { kFree, kBone, kBone114, kBone115, kBeam, kPart0, kSpawnActor } attach = kFree;
    int attach_bone = -1;          // kBone: blk handle index 0..23
    Vec3 attach_off{}, attach_rot{};
    int attach_mode = 2;
    int life = -1;                 // FUN_00564a20: first particle life/remaining override
    bool scale_after = false;      // the brain then writes fx+0x1ac..0x1b4 = scale (uniform)
    bool knock_from_facing = false;  // fx+0x11c |= 2 (state 2 beams)
    bool flag201 = false;          // fx+0x201 = 1 (state 5 beam)
    int spawn_ref = -1;            // kSpawn / kSpawnActor: index into Boss01Output::spawns
};
// FUN_00593550(owner=boss, kind, eff, pos, dir, f0..f12) — BULLETS.md §1.1.
struct Boss01Bullet {
    int kind = 0, effect = 0;
    Vec3 pos{}, dir{};
    std::array<float, 13> f{};     // f0 speed, f1 accel, f2 gravity, f3 power, f4 knock, ..., f9..f12
};
// FUN_00475710(cast 0xaf, yaw 0, pos, level) + the boss's writes on the result (§5.1 step 5).
struct Boss01Spawn {
    int cast = 0xaf;
    int yaw_deg = 0;               // Falcom degrees (edx = 0)
    Vec3 pos{};
    int level = 8;                 // boss +0xc8
    float f330 = 0.05f;            // S+0x330
    Vec3 knock_vel{};              // S+0x338..0x340
    int mode72c = 1;               // S+0x72c
};
// One edit of an actor of Boss01Input::actors (by index).
struct Boss01ActorEdit {
    int index = 0;
    bool set_pos = false; Vec3 pos{};
    bool set_330 = false; float f330 = 0;
    bool eaten = false;            // HP = 0, +0x130 |= 8 (silent death)
};
struct Boss01BubbleEdit { int index = 0; Vec3 pos{}; bool kill = false; };
// The laser's terrain ray (FUN_005e9380 + FUN_0041e420(2, ...)): segment from `from` with
// `delta`; on world contact spawn `fx` (at the contact point, or at `at` when at_aim).
struct Boss01Ray { Vec3 from{}, delta{}; Boss01Effect fx; bool at_aim = false; };
struct Boss01Tilt { int frames = 0; Vec3 amp{}; };    // 0x76a5ac / 0x76a594..59c
struct Boss01ZoomBlur {                                // 0x76a550 / 0x76a554 / 0x7b2488
    std::optional<float> strength, rate;
    bool snapshot = false;
};
struct Boss01Gauge { float hp = 0, max = 0, len_scale = 3500.0f; int strip = 1; };

struct Boss01Output {
    std::vector<Boss01ClipSet> clips;          // in order; the last one is current
    std::optional<float> speed;                // anim+0x24 (last write this tick)
    std::vector<Boss01Sound> sounds;
    std::vector<Boss01Effect> effects;
    std::vector<int> kill_effects;             // FUN_00451730(id): kill every live effect of id
    std::vector<Boss01Bullet> bullets;
    std::vector<Boss01Spawn> spawns;
    std::vector<Boss01ActorEdit> actor_edits;
    std::vector<Boss01BubbleEdit> bubble_edits;
    std::vector<Boss01Ray> rays;
    int minion_730 = 0;                        // != 0: every cast-0xaf actor +0x730 = this (100 / 1)
    bool kill_minions = false;                 // every cast 0x19 / 0xaf actor: HP = 0
    bool shorten_146 = false;                  // every live EF2-0x146 particle with life != 30: life = 30
    Vec3 player_push{};                        // add to player +0x338 / +0x340 (x / z)
    std::vector<int> rumble;                   // FUN_005d3500(n) — PadTilt, safe to ignore
    std::optional<Boss01Tilt> tilt;
    Boss01ZoomBlur zoom;                       // fields set only when written
    std::optional<Vec3> blur_centre;           // world point the host projects (0x7b24a4/a8)
    std::optional<Vec3> mesh_tint;             // FUN_005f51f0(anim, rgb): every mesh entry +0x24
    std::optional<Mat4> beam;                  // blk+0x140: the laser beam matrix (state 5)
    struct DeathSparks { int effect = 0x57; int pct = 1; float scale = 0.5f; };
    // FUN_00504c10(obj, 0x57, pct, scale): needs the model's mesh list, so the host runs it
    // with the SAME rng right after tick() returns (no boss draw follows it in that tick).
    std::optional<DeathSparks> death_sparks;
    std::optional<float> alpha_target, alpha_step;   // obj+0x41c / +0x420 (k = 142)
    std::vector<std::pair<int, int>> flags;    // g_flags writes (220 = 1, or 156 = 0 in time-attack)
    bool award_exp = false;                    // FUN_004fa4a0(obj+0x94) — EXP + drop helpers
    bool clear_buffs = false;                  // 0x76c19c/1a0/1a4 = 0, byte 0x76a810 = 1
    bool destroy = false;                      // boss + boss01_hit1/2: +0x11 |= 2, +0x60 &= ~0x80
    std::optional<Boss01Gauge> gauge;          // FUN_0043a460 (called only when HP changed)
    bool gauge_hide = false;                   // DAT_006d5fd8 = 0
    bool camera_written = false;               // Boss01::camera() was written this tick
    // Stun entry mid-blend (§4b, BOSS_CAMERA.md): the slot-0xb eye / target snapshot (blk+0x25c / 0x2b0).
    std::optional<std::pair<Vec3, Vec3>> cam_snapshot;
    std::array<Mat4, 2> hit_mesh{};            // boss01_hit1/2 world matrix (obj x T(0,0,-1.75/-3))
};

// The boss mesh's contact flags this frame (§3.4): which collision entries hurt, given the
// entry's flag word +0x84 (from the YMO — [UNKNOWN] source, VELAGUNDER U4).
struct Boss01Contact {
    bool l_window = false;    // ATTACK_01_L active window (bits 0x40 / 0x04)
    bool l_mid = false;       // its mid frame (bits 0x20 / 0x02)
    bool mid80 = false;       // same mid frame (bit 0x80)
    bool action01 = false;    // clip ACTION_01 (+0x89 stays live while stunned)
    bool bite = false;        // clip ATTACK_10_3 (bit 0x08)
    bool stunned = false;     // byte blk+0x12c == 1
    bool entry_hurts(std::uint32_t f) const {   // entry +0x8b
        return ((f & 0x40) && l_window) || ((f & 0x20) && l_mid) || ((f & 4) && l_window) ||
               ((f & 2) && l_mid) || ((f & 8) && bite) || ((f & 0x80) && mid80);
    }
    bool entry_collides(float player_hp) const {  // entry +0x89
        return (action01 || !stunned) ? player_hp >= 1.0f : false;
    }
};

class Boss01 {
public:
    explicit Boss01(const Boss01Setup& s);

    // One brain step (FUN_562990 case 5 -> FUN_005062d0). No-op once destroyed.
    Boss01Output tick(const Boss01Input& in);

    // The player's swing scan landed `dmg` on part i (FUN_004faf20: part+0x134 += dmg,
    // part HP -= dmg). Consumed by the next tick (§3.3).
    void add_part_damage(int i, float dmg);
    // FUN_004faf20's gate: the part takes hits (+0x60 & 0x80, HP > 1.0, hurt radius > 0).
    bool part_hittable(int i) const;

    // ---- queries ----
    const Boss01Part& part(int i) const { return parts_[std::size_t(i)]; }
    const std::array<Boss01Part, boss01::kParts>& parts() const { return parts_; }
    float hp() const { return hp_; }                 // obj+0x98
    float max_hp() const { return max_hp_; }         // obj+0x94
    float str() const { return str_; }               // obj+0xa4
    float def() const { return def_; }               // obj+0xa8
    float rec3() const { return rec3_; }             // obj+0xac
    float exp_b0() const { return exp_b0_; }         // obj+0xb0
    float rec4() const { return rec4_; }             // obj+0xc4
    int level() const { return level_; }             // obj+0xc8
    float hp_bar_fraction() const { return max_hp_ > 0 ? hp_ / max_hp_ : 0.0f; }   // §6
    bool hp_bar_visible(bool fight) const { return fight && hp_ >= 1.0f; }
    Vec3 pos() const { return pos_; }                // obj+0x50
    float yaw() const { return d4_; }                // blk+0xd4: D3D RotationY of the model
    Mat4 matrix() const;                             // obj+0x20: RotY(d4) + position
    int state() const { return state_; }             // blk+0xb8
    int sub() const { return bc_; }                  // blk+0xbc
    std::uint32_t timer() const { return c0_; }      // blk+0xc0
    int aux() const { return c4_; }                  // blk+0xc4
    int phase() const { return phase_; }             // blk+0x118
    float stun_gauge() const { return stun_; }       // blk+0xcc
    float stun_damage() const { return dmg_since_stun_; }  // blk+0xd0
    bool stunned() const { return stun_flag_ == 1; }       // byte blk+0x12c
    float flash() const { return flash_; }           // obj+0x2a0
    // FUN_00504140: the draw-time additive tint from the flash (§3.5).
    Vec3 flash_tint() const;
    Vec3 aim() const { return aim_; }                // blk+0x180..0x188
    bool contact_alt() const { return (obj11c_ & 8) != 0; }  // obj+0x11c & 8 -> FUN_004fe1a0
    bool not_stunned_bit() const { return !stunned(); }        // obj+0x298 & 0x400 [U13]
    const Boss01Contact& contact() const { return contact_; }
    bool destroyed() const { return destroyed_; }
    int eaten() const { return eaten_; }             // blk+0x1e4
    Boss01Camera& camera() { return cam_; }
    // The six camera presets blk+0x214.. (eye) / blk+0x268.. (target), index 1..6, rebuilt at
    // the end of every tick (BOSS_CAMERA.md §3). [0] is unused.
    const std::array<Vec3, 7>& cam_eyes() const { return cam_eye_; }
    const std::array<Vec3, 7>& cam_targets() const { return cam_tgt_; }
    const Boss01Camera& camera() const { return cam_; }

    // Raw blk fields the decision reads (exposed for goldens / the test).
    std::uint32_t stun_hits_a() const { return n1_; }   // blk+0xf4
    std::uint32_t stun_hits_b() const { return n2_; }   // blk+0xf8
    std::uint32_t near_frames() const { return near_; } // blk+0x120
    std::uint32_t t1() const { return t1_; }            // blk+0x124
    std::uint32_t t2() const { return t2_; }            // blk+0x128
    bool f7_pending() const { return f7_ != 0; }        // byte blk+0x12d (ATTACK_06 pending)
    bool fr_pending() const { return fr_ != 0; }        // byte blk+0x12e (reposition pending)
    int run_slot() const { return ec_; }                // blk+0xec

private:
    // per-frame pieces (in brain order)
    void parts_update();          // FUN_005084a0
    void aim_update();            // 005062d0.c:206-290
    void stun_entry();            // 0x506a65..0x506fdb
    void common_tail();           // FUN_00512720
    void death_visuals();         // 0x50720f..0x507478
    void push_player();           // FUN_005061e0
    void tail_always();           // LAB_00507488: HP bar, flash decay, hit meshes

    void state0();  void state1();  void state2();  void state3();  void state4();
    void state5();  void state7();  void state8();  void state9();  void state10();

    // helpers
    std::uint32_t draw();                         // one xorshift step
    std::uint32_t R(std::uint32_t n) { return (draw() >> 17) % n; }
    void commit(int s);                           // FUN_00509260
    void set_clip(int clip, int blend);
    bool clip_done() const { return clip_count_ != 0; }
    void speed(float v) { out_->speed = v; }
    void se3d(int id, const Vec3& p) { out_->sounds.push_back({Boss01Sound::k3D, id, p, 0}); }
    void se2d(int id) { out_->sounds.push_back({Boss01Sound::k2D, id, {}, 0x400}); }
    void se_stop(int id, int v) { out_->sounds.push_back({Boss01Sound::kStop, id, {}, v}); }
    void cam(int cur, int prev, float rate);      // cur/prev/t=0/rate
    void tilt(int frames, Vec3 amp) { out_->tilt = Boss01Tilt{frames, amp}; }
    void kill_fx(int id) { out_->kill_effects.push_back(id); }
    Boss01Effect& fx(int id, const Vec3& p);
    void pull_actors(bool sub2);                  // state 4 sub 2 / sub 3
    Vec3 pull_point();                            // state 4: Null(242) + RotY(d4)*(0,0,-1.5), y-0.5
    int side_choose();                            // FUN_0050b390
    void heal(float divisor);

    // ---- setup / flags ----
    int char_ = 1, bb5c_ = 0;
    bool bb58_ = false, bc48_ = false, time_attack_ = false;
    bool toal() const { return char_ == 3; }

    // ---- obj ----
    float max_hp_ = 0, hp_ = 0, str_ = 0, def_ = 0, rec3_ = 0, exp_b0_ = 0, rec4_ = 0;
    int level_ = 0;
    Vec3 pos_{};
    float flash_ = 0;             // obj+0x2a0
    float last_bar_hp_ = 0;       // obj+0x37c
    std::uint32_t obj11c_ = 0;    // obj+0x11c (bit 8 toggles every live frame)
    bool destroyed_ = false;

    // ---- blk (offsets in comments) ----
    int state_ = 0;               // 0xb8
    int bc_ = 0;                  // 0xbc  sub-phase / death frame k
    std::uint32_t c0_ = 0;        // 0xc0  timer
    int c4_ = 0;                  // 0xc4  cooldown / side / direction
    std::uint8_t started_ = 0;    // 0xc8  fight started
    std::uint8_t arrived_ = 0;    // 0xc9  state 1 arrival latch
    float stun_ = 0;              // 0xcc  stun gauge
    float dmg_since_stun_ = 0;    // 0xd0
    float d4_ = 0;                // 0xd4  model yaw
    float d8_ = 0, dc_ = 0;       // 0xd8 / 0xdc  3-D / planar distance to the player
    float e0_ = 0, e4_ = 0, e8_ = 0;   // 0xe0 angle to player, 0xe4 heading, 0xe8 pending turn
    int ec_ = 0, f0_ = 0;         // 0xec / 0xf0  state 1 slots
    std::uint32_t n1_ = 0, n2_ = 0;    // 0xf4 / 0xf8  stun-part hit counters
    float fc_ = 0.96f;            // 0xfc
    Vec3 look_{0, 2, 22};         // 0x100..0x108  the fixed facing point (= arena centre C)
    Vec3 home_{0, 2, 33};         // 0x10c..0x114
    int phase_ = 0;               // 0x118
    std::uint32_t near_ = 0, t1_ = 0, t2_ = 0;   // 0x120 / 0x124 / 0x128
    std::uint8_t stun_flag_ = 0;  // 0x12c
    std::uint8_t f7_ = 0, fr_ = 0;     // 0x12d / 0x12e
    int after06_ = 0;             // 0x130
    int blk134_ = 0;              // 0x134 (no writer found besides the ctor's 0)
    Vec3 aim_{};                  // 0x180..0x188
    std::uint8_t drool_ = 0;      // 0x198
    float tint_v_ = 0;            // 0x1e0
    int eaten_ = 0;               // 0x1e4
    std::uint8_t latch_ = 0;      // 0x1e8  state 2 hit latch
    // 0x1ec..0x214: effect handles. Only "non-zero" matters to the code that reads them
    // (the death / stun-entry kill guards); index = (off - 0x1ec) / 4. 0x1ec stars, 0x1f0
    // laser, 0x1f4 + 4n spawn slime (n = count after the factory ++), 0x20c inhale (aliases
    // spawn slot 6 — VELAGUNDER A §2.6, [UNKNOWN] consequence).
    std::array<bool, 11> fx_slot_{};

    Boss01Camera cam_;
    std::array<Vec3, 7> cam_eye_{}, cam_tgt_{};
    void camera_presets();
    std::array<Boss01Part, boss01::kParts> parts_{};
    Boss01Contact contact_;

    // ---- this tick ----
    const Boss01Input* in_ = nullptr;
    Boss01Output* out_ = nullptr;
    std::array<Vec3, boss01::kBones> bones_{};
    Vec3 player_now_{};           // the player's position as later reads see it (state 4 pulls it)
    int clip_ = 0;
    std::uint32_t clip_count_ = 0;
    float clip_frame_ = 0;
};

}  // namespace cleria
