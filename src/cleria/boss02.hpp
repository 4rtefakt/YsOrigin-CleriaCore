// Nygtilger ("boss02", room S_2099) — the worm's logic, pure and headless.
//
// Ported from build/boss_re/NYGTILGER.md (the verified spec; § numbers below are its
// sections), re-checked against the Ghidra export of the functions it names:
//   ctor FUN_00514200 (+ stat table FUN_00513d80), brain FUN_00518ae0 (every piece), group
//   driver FUN_00516740 (modes 0/1/3, the laser), mode 4 FUN_00515250, the follow
//   FUN_00518220, the trail FUN_00515100, segment extras FUN_00517f30, the pre-roll
//   FUN_005136f0, the leader step FUN_00513590, the annulus clamp FUN_00513490, the body
//   probe FUN_005151b0, the player ring FUN_00518a10, the callbacks FUN_005130c0 (accept
//   hit) / LAB_00513390 (touch), the bullet spawner FUN_0051c8c0, the arc spawner
//   FUN_0051b850 (its RNG draws only), the kind-0x24 handler FUN_0059fa30 is in bullet.cpp.
//
// The worm is 36 engine objects: the 19 vector pieces (0 head boss02_h, 1..17 b01..b17,
// 18 tail c18) plus the 17 twin plates d01..d17 (not in the vector). Boss02 owns all of
// them: object index o = 0..18 is the vector piece, o = 18 + j (j = 1..17) is plate dj,
// whose owner is vector piece j. The world is reached only through Boss02Input and the
// Boss02Output request vocabulary, as in boss01; apps/cleria-boss02test drives it headless.
//
// Tags in comments: addresses are [BYTES]/[EXPORT] as in the spec; [H] = inference;
// [UNKNOWN] = not resolved by the spec; [STAND-IN] = a host-side helper that is NOT engine
// code; [not modelled] = engine behaviour this port leaves out (listed in NYGTILGER.md §15).
//
// RNG: every draw goes through Boss02Input::rng (the game's ONE xorshift128, 0x74622c..38,
// returning the new word w). R(n) = (w >> 17) % n; U() = float(w & 0x7fffff | 0x3f800000) - 1.
// The draw order is the brain's, object by object in update order (tick_order()). Two
// cosmetic engine helpers also draw from it mid-brain (the debris spawner FUN_00587010 and
// the screen-debris burst FUN_0058a5f0, both pool-dependent): Boss02Input::cosmetic is
// called SYNCHRONOUSLY at those points so the host can run them with the same rng.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

namespace boss02 {

inline constexpr int kPieces = 19;     // the vector DAT_0076a2c4: head, b01..b17, c18
inline constexpr int kPlates = 17;     // d01..d17
inline constexpr int kObjects = kPieces + kPlates;
inline constexpr int kTrail = 600;     // G+0: 600 records x 0x1c

enum Role : int { kHead = 0, kBody = 1, kTail = 2, kTwin = 3 };   // blk+4 (name[7] h/b/c/d)

// The clip id blk+0x28 and the names the code sets (FUN_005f5c10 by name, §2.3 / §5.6).
enum ClipId : int { kWalk = 0, kStop = 1, kAttack = 2, kDamage = 3, kRollA = 4, kRollC = 5 };
extern const char* const kClipNames[6];   // "walk", "stop", "attack", "damage", "roll_a", "roll_c"
inline constexpr float kClipRate = 0.25f; // every boss02 clip: 0.25 frames/step (YMO header)

// The engine object name of object o ("boss02_h", "boss02_b01", "boss02_c18", "boss02_d01").
const char* object_name(int o);
// Its model "data\enemy\boss02\boss02<s>.ymo" with <s> = "", "b", "c", "d"; the broken shell
// is bboss02<s>.ymo, the stone shell sboss02<s>.ymo (§2.1).
const char* model_suffix(int o);

// FUN_00513d80: the hard-coded table the brain reads every frame (§2.2). Easy = flag 143.
struct Stats {
    float head_hp = 700, seg_hp = 350;      // 0x7b9184 / 0x7b9188
    float head_str = 100, body_str = 103;   // 0x7b918c / 0x7b9190
    float charged_str = 114;                // 0x7b9194 (G+0x174 != 0)
    float head_def_open = 80;               // 0x7b9198 (G+0xc == 3)
    float tail_def = 98;                    // 0x7b919c (tail and twin plates)
    float proxy_str = 102;                  // 0x7b91a0 (the inert G+0x1b4 GoXx: the 0x24 owner)
    float ball_a4 = 98;                     // 0x7b91a4 (poison ball bullet +0xa4)
    int laser_cd = 140;                     // 0x7b91a8
    int circle_rev = 280;                   // 0x7b91ac
    int circle_after1 = 120;                // 0x7b91b0
    int ball_cap = 12;                      // 0x7b91b4
    int husk_roll = 260;                    // 0x7b91b8
    float hoop_rate = 0.005f;               // 0x7b91bc
    float laser_str = 113;                  // 0x7b91c0
    float smoke_str = 103;                  // 0x7b91c4
    bool nightmare = false;                 // byte 0x767bae
};

}  // namespace boss02

// What the caller reads from EEX record 1 and the game flags (§2.1 / §2.2).
struct Boss02Setup {
    // EEX record 1 via FUN_00443b80: only level / rec3 / rec4 / EXP survive (N4).
    float eex_hp = 1200, eex_str = 120, eex_def = 124, rec3 = 50, rec4 = 140;
    int level = 16;
    float exp_b0 = 0;             // +0xb0 (the EXP award reads the head's stat block, §8)
    int character = 1;            // g_flags[150]: 1 Yunica, 2 Hugo, 3 Toal
    int diff_index = 0;           // g_flags[144]
    bool diff_easy = false;       // g_flags[143]
    bool diff_very_easy = false;  // g_flags[203]
    bool time_attack = false;     // g_flags[193]
    int retries = 0;              // g_flags[196]
    Vec3 pos{0, 0, -11};          // Mesh_Locate('boss02_h', 0,0,-11)
    // Collision-entry "struck" byte +0x8a of the head / bodies / plates [UNKNOWN U1: the
    // brain only forces it for the tail; presumably the YMO]. true = the sword may strike.
    bool entries_struck = true;
};

// One live GoXx bullet of DAT_00730248 as the tail's bookkeeping sees it (§7.3), engine order.
struct Boss02BulletView {
    int kind = 0;        // +0x124 (0x23 / 0x24; others are ignored)
    int n = 0;           // +0x1d0 (kind 0x24 frame counter)
    Vec3 pos{};          // +0x50
};

// A cosmetic engine helper the brain calls mid-tick (it draws from the shared RNG).
struct Boss02Cosmetic {
    enum Kind { kDebris, kScreenDebris } kind = kDebris;
    int object = 0;
    Vec3 pos{};
    // kDebris: FUN_0051b5a0(ecx = pos, edx, stack s1 (bag table 0x69ce8c index), s2, flag)
    // -> FUN_00587010(0x42, ...) — PICKUPS.md debris base 0x42.
    int edx = 0, s1 = 0, s2 = 0;
    bool flag = false;
    // kScreenDebris: FUN_0058a5f0(2, 0x18, 2.6, 3.6, pos, 0.65, 0, 0) (only with option byte
    // 0x6d5d92 set; the brain's own 2 jitter draws are already taken).
};

struct Boss02Input {
    // ---- the player (DAT_0074c09c) ----
    bool player_present = true;
    Vec3 player_pos{};            // +0x50 (FUN_00518a10 may clamp it this tick: Output::player_pos)
    float player_hp = 1;          // +0x98 (= 0x76a730)
    // ---- flags ----
    bool fight = false;           // g_flags[156] (DAT_0076bb8c)
    int wake = 0;                 // g_flags[166] (signed; > 0 wakes)
    bool stir = false;            // g_flags[167] (dormant segments drop debris)
    int flag221 = 0;              // g_flags[221] (the death write is `if (f221 == 0) f221 = 1`)
    int rush_select = 0;          // g_flags[165] (boss-rush room only)
    bool rush_room = false;       // stricmp(room, "s_91\\s_9113\\") == 0
    bool fx_option = false;       // option byte 0x6d5d92 (screen debris, sparks) — cosmetic
    // ---- the posed models (FUN_005f6000 runs before the brain) ----
    // The clip completion count (*(obj+0x26c)+0x1c != 0) per object, after this frame's advance.
    std::array<bool, boss02::kObjects> clip_done{};
    // World Y of each segment's two collision-entry bones (role 1 "Bone01"/"Bone01(5)", role 2
    // "l_reg00"/"r_reg00"), index = vector piece (0 unused).
    std::array<std::array<float, 2>, boss02::kPieces> entry_bone_y{};
    // ---- the bullet list (tail bookkeeping §7.3) ----
    std::vector<Boss02BulletView> bullets;
    // ---- engine services ----
    std::function<std::uint32_t()> rng;                       // ONE xorshift128 advance
    std::function<void(const Boss02Cosmetic&)> cosmetic;       // optional (see the header)
};

// ---- the request vocabulary ----
struct Boss02Sound {
    enum Kind { k3D, k2D, kStop, kLoop } kind = k3D;
    int id = 0;
    Vec3 pos{};          // k3D (FUN_0043a080)
    int param = 0;       // kStop: fade ms (1000); kLoop: FUN_0043a040 looped
};
// FUN_005f5c10(name = kClipNames[clip], blend, start frame); by_index: FUN_005f5c80(section
// index = clip, blend) — the shell swap re-applies blk+0x28 to the NEW model by index [H].
struct Boss02ClipSet { int object = 0; int clip = boss02::kStop; int blend = 1; float start = 0; bool by_index = false; };
struct Boss02ModelSwap { int object = 0; };   // live -> BBOSS02<s> (broken), clip re-applied hard
// A GoEffect / FUN_00450410 spawn.
struct Boss02Effect {
    int id = 0;
    int object = 0;                  // the spawning piece
    Vec3 pos{}, rot{}, scale{1, 1, 1};
    int side = 1;
    // Whose stat block (+0x94..0xcf) arms its hit emitters: none, the spawning piece, or that
    // piece's block with STR replaced (`str`).
    enum Stat { kNone, kPiece, kPieceStr } stat = kNone;
    float str = 0;
    float power = 0.0f, knock = 1.0f;  // FUN_00450410's usual (power 0, knock 1.0); the smoke 1 / 1.5
    int rtype = 0, status = 0, cls = -1;
    // Attachments (FUN_00451c20 / eff+0x1d5 = 1, eff+0x1a0 = &matrix): the mouth frame G+0x80,
    // the hoop frame G+0x130 (both Boss02::mouth_frame() / hoop_frame()), or the head node
    // "Bone02" at attach_off.
    enum Attach { kFree, kMouth, kHoop, kBone02 } attach = kFree;
    Vec3 attach_off{};
    bool flag201 = false;            // fx+0x201 = 1
    // The group slot that keeps the handle (killed by Output::kill_slots).
    enum Slot { kNoSlot, kLaserCharge, kEye, kRolling } slot = kNoSlot;
};
// FUN_00593550 (BULLETS.md §1.1) — kinds 0x24 (plasma, §7.1) and 0x23 (poison ball, §7.2).
struct Boss02Bullet {
    int kind = 0, effect = 0;
    int carrier = 0;                 // the segment (0x24: +0x1a8; 0x23: +0x194)
    bool proxy_owner = false;        // 0x24: owner = the inert G+0x1b4 GoXx (STR proxy_str, DEF 999)
    Vec3 pos{}, dir{};
    std::array<float, 13> f{};       // f0 speed, f1 accel, f2 gravity, f3 power, f4 knock, ... f9
    float a4 = 0;                    // 0x23: bullet +0xa4 = 0x7b91a4
    Vec3 local{};                    // 0x24: +0x1ac (the carrier-local spawn vector)
    Vec3 target{};                   // 0x24: +0x1b8 (the marker's destination T)
};
// The tail's repulsion pass (§7.3): add `delta` to bullet `index`'s +0x1b8 and +0x1c4.
struct Boss02BulletNudge { int index = 0; Vec3 delta{}; };
struct Boss02Gauge { float hp = 0, max = 0, len_scale = 4500.0f; int strip = 2; };

struct Boss02Output {
    std::vector<Boss02ClipSet> clips;
    std::vector<Boss02ModelSwap> swaps;
    std::vector<Boss02Sound> sounds;
    std::vector<Boss02Effect> effects;
    // Kill the effect a slot holds (+0x1d4 = 1), or every live GoEffect (the death, §8).
    std::vector<Boss02Effect::Slot> kill_slots;
    bool kill_all_effects = false;
    std::vector<Boss02Bullet> bullets;
    std::vector<Boss02BulletNudge> nudges;
    std::vector<int> rumble;                   // FUN_005d3500(n) — pad rumble, ignorable
    std::vector<std::pair<int, int>> flags;    // g_flags writes (221 = 1; 156 = 0 / 1; 165 = 0; 166 = 1)
    std::optional<Vec3> player_pos;            // FUN_00518a10's ring clamp (write it back)
    bool player_iframes = false;               // player +0x734 = max(+0x734, 9999), FUN_00461120(2)
    bool end_block = false;                    // 0x76a828 = 1, FUN_00461160, FUN_00461120(0)
    bool award_exp = false;                    // FUN_004fa4a0(head+0x94): EEX rec 1, level 16
    bool steam_achievement = false;            // FUN_00403cd0
    std::optional<Boss02Gauge> gauge;          // FUN_0043a460 (only when the pool changed)
    bool gauge_hide = false;                   // DAT_006d5fd8 = 0
};

// What a touch of a piece asks the host to do (LAB_00513390, §3.6).
struct Boss02Touch {
    bool damage = false;             // run the contact damage (FUN_00504b00) with `str`
    float str = 0;
    int rehit = 15;                  // obj+0xf4: the re-hit interval (FUN_004faac0 bank)
};

// The kind-9 GoXx hurt part of the head and the tail (§3.1).
struct Boss02Part {
    bool alive = false;              // +0x60 & 0x80 / slot non-null
    float hp = 0;                    // +0x98 (= piece HP x 10 at the ctor)
    float def = 0;                   // +0xa8
    float resist[4] = {};            // +0xb4..+0xc0
    float dmg = 0;                   // +0x134 (consumed by the brain)
    float radius = 0.85f;            // FUN_005a1a10 r
    Vec3 pos{};                      // +0x50 / +0x6c / +0xdc
    bool ce = true;                  // +0xce (cleared 12 frames after the crack) [UNKNOWN reader]
};

// One engine object (GoMesh + its 0x2b0 blk). Field names carry the offsets.
struct Boss02Piece {
    int role = 0, idx = 0;           // blk+4, blk+0
    int owner = -1;                  // twin: blk+0x38 (its b); body: blk+0x34 = its twin (twin)
    int twin = -1;
    bool exists = true;              // false once a twin destroyed itself (+0x11 |= 2)
    Mat4 world;                      // obj+0x20 (row 3 = the position obj+0x50)
    float max_hp = 0, hp = 0, last_hp = 0;   // +0x94, +0x98, blk+0x58
    float str = 0, def = 0;          // +0xa4, +0xa8
    float resist[4] = {};            // +0xb4..+0xc0
    bool frozen = false;             // obj+0x28f
    std::uint32_t f298 = 0;          // obj+0x298 (8 struck-able, 0x40 dead, 0x20 hidden, 0x20000 charged)
    std::uint8_t f28c = 0;           // obj+0x28c (bits 4 / 8, no reader known, U3)
    std::uint32_t f60 = 0x88;        // obj+0x60 (0x80: live)
    bool entries_struck = true;      // every collision entry +0x8a
    std::array<bool, 2> entry_off{}; // segment entries +0x8c (one-way platforms, §5.4)
    float flash = 0;                 // obj+0x2a0
    float alpha = 1, alpha_target = 1, alpha_step = 0;   // obj+0x418 / 0x41c / 0x420
    float fall_start = 0, fall_vel = 0, fall_e = 0;      // obj+0x44c / 0x450 / 0x454
    Vec3 front{}, rear{};            // blk+0xc / blk+0x18
    int kp = 0;                      // blk+8 = 30 idx + 5
    float wob = 0;                   // blk+0x24
    int clip = boss02::kStop;        // blk+0x28
    int pile = 0;                    // blk+0x2c
    Vec3 pose_pos{};                 // blk+0x3c..0x44 (mode-4 scripted pose)
    float pose_pitch = 0, pose_yaw = 0, pose_roll = 0;   // blk+0x48 / 0x4c / 0x50
    int smooth = 0;                  // blk+0x54
    bool hurts = false;              // byte blk+0x5c (touch hurts)
    bool cracked = false;            // byte blk+0x5d
    int crack_t = 0;                 // blk+0x60
    int glow_t = 0;                  // blk+0x64
    float glow = 0;                  // blk+0x68 (neighbour-summed, unclamped)
    float glow_mat = 0;              // the emissive the brain writes this frame (min 1)
    bool fire = false;               // byte blk+0x6c (on the pillar / posed)
    int death_k = 0, death_delay = 0;   // blk+0x70 / 0x74
    Vec3 drift{}, spin{};            // blk+0x78..0x80 / blk+0x84..0x8c
    std::array<int, 2> arc_life{};   // blk+0x13c + s*0xb8 (life 24; visual, RNG-relevant)
    Vec3 start_pos{};                // blk+0x204
    float stone = 1.0f;              // blk+0x210 (stone-shell opacity)
    Mat4 snapshot;                   // blk+0x220
    int husk = 0;                    // blk+0x264
    float husk_h[3] = {};            // blk+0x268..
    Vec3 husk_q[3] = {};             // blk+0x274..
    bool has_broken = true;          // blk+0x298 != 0 (the BBOSS02 model not yet swapped in)
    bool broken = false;             // the broken model is live
    bool hidden = false;             // +0x298 & 0x20
    bool has_part = false;           // obj+0x278 = 1 (head, tail)
    Boss02Part part;
    Vec3 pos() const { return {world.m[12], world.m[13], world.m[14]}; }
};

// The 600-record pose trail of the leader (G+0, FUN_00515100).
struct Boss02TrailRec {
    Vec3 pos{};
    float pitch = 0, yaw = 0, roll = 0;
    std::uint32_t flags = 0;         // 1 on the pillar, 2 scripted (hoop) pose
};

// The group block G (0x1c0 bytes at head blk+0x90). Offsets in comments.
struct Boss02Group {
    float pool = 0, pool_max = 0;    // +4 / +8
    int phase = 0;                   // +0xc: 0, 2 (<= 50 %), 3 (segments dead), 4 death
    int mode = 0;                    // +0x14
    int t = 0, t2 = 0;               // +0x18 / +0x1c
    bool cw = false;                 // byte +0x20 (moving clockwise)
    float coil = 0;                  // +0x28 (annulus squeeze 0..1)
    std::uint8_t trail_mask = 0;     // byte +0x2c
    std::uint32_t trail_flags = 0;   // +0x30
    Vec3 ahead{};                    // +0x34 (the look-ahead point)
    Vec3 lead{};                     // +0x40 (the leader L)
    float pitch = 0, yaw = 0, roll = 0;   // +0x4c / +0x50 / +0x54
    float speed = 0;                 // +0x58
    bool clamp = false;              // byte +0x5c
    int bump = 0;                    // +0x60 (inner-clamp hit timer)
    int around_n = 0;                // +0x64 (no reader, U4)
    int freeze = 0;                  // +0x68 (40 after a laser start)
    float circ_tgt = 0;              // +0x6c
    int roll_wait = 0;               // +0x70
    int sub = 0;                     // +0x74: 0 hunt, 1 go home / climb, 2 roll pending, -1 descend
    int circle = 0;                  // +0x78
    int climb_cd = 0;                // +0x7c
    Mat4 mouth;                      // +0x80
    bool laser_fx = false;           // +0xc0 (the plasma03c handle, non-null)
    int laser = 0;                   // +0xc4 (charge 90 -> 0)
    int laser_cd = 0;                // +0xc8
    bool eye_fx = false;             // +0xcc
    bool fire_ok = false;            // byte +0xd0 (mode 1)
    bool flinch = false;             // byte +0xd1 (a segment died)
    bool in_range = false;           // byte +0xd2
    int glow_laser = 0, glow_blink = 0, glow_bump = 0;   // +0xd4 / +0xd8 / +0xdc
    int loop = 0;                    // +0xe0 (locomotion SE state)
    int hs = 0;                      // +0xec: mode-4 sub-phase
    float wp = 0, theta = 0, wy = 0, psi = 0, R = 0;     // +0xf0 / 0xf4 / 0xf8 / 0xfc / 0x100
    float pose_blend = 0;            // +0x104
    Vec3 hp_at{};                    // +0x108..0x110 (head position; the hoop centre in mode 4)
    bool flip = false;               // +0x114
    int hn = 0;                      // +0x118 (mode-4 phase-2 frame counter)
    float smoke_acc = 0, smoke_last = 0;   // +0x11c / +0x120
    float r_from = 0, r_to = 0;      // +0x124 / +0x128
    Mat4 hoop;                       // +0x130
    bool rolling_fx = false;         // +0x170
    bool charged = false;            // +0x174 (whole-body contact armed)
    int rolls_left = 0;              // +0x178
    float r_t = 0;                   // +0x17c
    int rolls = 0;                   // +0x180
    int g184 = 0;                    // +0x184 (no reader, U4)
    int rumble_cd = 0;               // +0x188
    bool se220 = false, braking = false;   // bytes +0x18c / +0x18d
    int cycles = 0;                  // +0x190
    float ph1 = 0, ph2 = 0;          // +0x194 / +0x198
    bool awake = false, dormant = false, started = false, sting = false, ended = false;
                                     // bytes +0x19c / 0x19d / 0x19e / 0x1b0 / 0x19f
    int balls = 0;                   // +0x1a0 (live kind-0x23 count)
    int markers = 0;                 // +0x1a4 (no reader)
    int hold = 0;                    // +0x1a8
    int shots = 0;                   // +0x1ac (SE 224/225 alternation)
};

class Boss02 {
public:
    explicit Boss02(const Boss02Setup& s);

    // One engine frame: the brain FUN_00518ae0 for every live object in update order, each
    // followed by the GoMesh common update's alpha fade and Mesh_Fall (FUN_00503190).
    Boss02Output tick(const Boss02Input& in);

    // ---- damage in (between ticks) ----
    // The mesh-damage path (§3.2): may a sword ray that struck collision entry `entry` of
    // object o damage it? (FUN_005130c0; `now_ms` = timeGetTime() for the 150 ms feedback
    // throttle.) The feedback (fluid effect 0x158, debris + SE 92) goes into `fb`.
    bool accept_hit(int o, const Vec3& contact, std::uint32_t now_ms, Boss02Output& fb,
                    const std::function<void(const Boss02Cosmetic&)>& cosmetic = {}, bool fx_option = false);
    // FUN_004faf20(1, 0) on a mesh: HP -= dmg (the host computed it with def() / resist).
    void add_damage(int o, float dmg);
    // FUN_004faf20 on the kind-9 part of the head / tail: part +0x134 += dmg, part HP -= dmg.
    void add_part_damage(int o, float dmg);
    bool part_hittable(int o) const;
    // The mesh path's gate: (obj+0x298 & 0x88) == 8, not frozen, alive (+0x60 & 0x80).
    bool mesh_hittable(int o) const;
    // ---- touch (LAB_00513390) ----
    // Before the contact damage: whether to run it (player HP >= 1, blk+0x5c); the host checks
    // its own re-hit bank (FUN_004faac0, interval 15) before applying.
    Boss02Touch touch(int o, float player_hp) const;
    // After the contact damage: true = the roll's forced reaction (player section 9, [U2]);
    // a head bump without the roll starts the eye flash G+0xdc = 12.
    bool touch_applied(int o, float player_hp_after);

    // ---- queries ----
    const Boss02Piece& object(int o) const { return obj_[std::size_t(o)]; }
    const Boss02Group& group() const { return g_; }
    const std::array<Boss02TrailRec, boss02::kTrail>& trail() const { return trail_; }
    const boss02::Stats& stats() const { return st_; }
    const std::array<int, boss02::kObjects>& tick_order() const { return order_; }
    float pool() const { return g_.pool; }
    float pool_max() const { return g_.pool_max; }
    int phase() const { return g_.phase; }
    int mode() const { return g_.mode; }
    const Mat4& mouth_frame() const { return g_.mouth; }   // G+0x80 (laser effects)
    const Mat4& hoop_frame() const { return g_.hoop; }     // G+0x130 (rolling01)
    float head_glow() const { return head_glow_; }         // the nig_tex_ele* material value
    bool finished() const { return obj_[0].hidden; }       // the head faded out (+0x298 |= 0x20)
    int level() const { return level_; }
    float rec3() const { return rec3_; }
    float rec4() const { return rec4_; }
    float exp_b0() const { return exp_b0_; }

private:
    // the brain, per object (FUN_00518ae0) and its pieces
    void brain(int o);
    bool death_block(int o);          // the G+0xc == 4 head of the brain; true = returned
    void stone_roll(int o);
    void twin_tail(int o, bool& gone);
    void parts_and_crack(int o);
    void glow_plasma_husk(int o);
    void head_glow_update(int o);
    void tail_bullets();
    void hp_change(int o, bool& died);
    void shell_swap(int o);
    void stat_block(int o);
    void common_update(int o);        // [STAND-IN order] FUN_00503190's alpha + fall
    // the group (head only)
    void driver();                    // FUN_00516740
    void mode1();
    bool mode3_body();                // false = it switched to mode 4 (already run) and returned
    void hoop();                      // FUN_00515250
    void player_ring();               // FUN_00518a10
    // the chain
    void trail_shift();               // FUN_00515100
    void follow(int o, bool snap, float a, float b);   // FUN_00518220
    void seg_extras(int o);           // FUN_00517f30
    void preroll(int variant);        // FUN_005136f0
    void leader_step(int& out);       // FUN_00513590
    void annulus(Vec3& p, int& out) const;   // FUN_00513490
    int body_under(int self, const Vec3& q, float r) const;   // FUN_005151b0 (-1 = none)
    void arc_spawn(int o, int slot);  // FUN_0051b850 (RNG draws + life 24)
    void plasma_spawn(int o, const Vec3& v);   // FUN_0051c8c0(.., 0)
    void poison_spawn(int o, const Vec3& p);   // FUN_0051c8c0(.., 1)
    void loop_state(int s);           // FUN_0051ad80 / the driver's inline copy

    // helpers
    std::uint32_t draw();
    std::uint32_t R(std::uint32_t n) { return (draw() >> 17) % n; }
    float U();
    void set_clip(int o, int clip, int blend, float start = 0);
    void se3d(int id, const Vec3& p) { out_->sounds.push_back({Boss02Sound::k3D, id, p, 0}); }
    void se2d(int id) { out_->sounds.push_back({Boss02Sound::k2D, id, {}, 0}); }
    void se_loop(int id) { out_->sounds.push_back({Boss02Sound::kLoop, id, {}, 0}); }
    void se_stop(int id, int fade) { out_->sounds.push_back({Boss02Sound::kStop, id, {}, fade}); }
    void debris(int o, const Vec3& p, int edx, int s1, int s2, bool flag);
    Boss02Effect& fx(int id, int o, const Vec3& p);
    int nvec() const;                 // live vector size (always 19 here)
    Boss02Piece& head() { return obj_[0]; }
    Vec3 player() const { return player_now_; }

    // ---- setup ----
    boss02::Stats st_;
    int char_ = 1, bb5c_ = 0;
    bool easy_ = false, time_attack_ = false;
    int level_ = 16;
    float rec3_ = 50, rec4_ = 140, exp_b0_ = 0;

    std::array<Boss02Piece, boss02::kObjects> obj_{};
    std::array<int, boss02::kObjects> order_{};
    std::array<Boss02TrailRec, boss02::kTrail> trail_{};
    Boss02Group g_;
    float head_glow_ = 0;
    float bar_last_ = -1.0f;          // head obj+0x37c (starts different from the pool)
    std::uint32_t fb_time_ = 0;       // [0x74e7e4] the accept-hit feedback throttle (real ms)

    // ---- this tick ----
    const Boss02Input* in_ = nullptr;
    Boss02Output* out_ = nullptr;
    Vec3 player_now_{};
    float player_hp_ = 1;
    bool f156_ = false;               // flag 156 as this tick's later reads see it
    int wake_ = 0;                    // flag 166
    int flag221_ = 0;                 // flag 221
    int balls_spawned_ = 0;           // kind-0x23 spawns this tick (the tail's count, [H])
    std::array<bool, boss02::kObjects> clip_reset_{};   // a set-clip this tick zeroed the count
};

}  // namespace cleria
