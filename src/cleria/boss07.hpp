// Dalles (GoMesh "boss07", type byte 0xb, room S_7080) — pure and headless.
//
// Ported from build/boss_re/DALLES.md (pass 1: the room, the ctor FUN_00545940, the block blk = obj+0x4a0),
// DALLES_AB.md (the shell, turn FUN_00546dc0, drift FUN_00547120, the clip sequencer FUN_0054a340, StartAttack
// FUN_0054ebb0, the scheduler FUN_0054ee30; parts / damage / barriers / phase ends FUN_005472e0), DALLES_CD1.md
// (options + wing FUN_00549020, push-out + camera targets FUN_00549eb0, the camera hook FUN_0042c4f0, the option
// homing shot FUN_00546900, the hand attacks of the executor FUN_0054a890) and DALLES_D23.md (the capture circle
// and the super, the death FUN_0054fb20, the W / F / E events, rock bullets, option volley / barrage, the aura).
// Section references below are "P1 §n", "AB §n", "CD1 §n", "D23 §n". Where the parts disagree the later, more
// specific one is followed (each has a "Corrections to pass 1" table).
//
// One GoMesh and nothing else: the module owns the body's object fields the band touches, the block, the three
// GoXx hurt parts (body core + one per option) and the three child meshes (options R / L, the wing), which the
// engine drives entirely from the parent. Everything that needs a host facility (effects, bullets, sounds, the
// player, the camera, the fader, fog, flags, the HUD) is an entry of Boss07Output; nothing is done here.
//
// The per-frame host contract:
//   1. deliver the frame's hits with hit_part() (the engine's FUN_004faf20 gate / accumulate / pool drain);
//   2. fill Boss07Input (the body's A+0x18 / A+0x1c / A+0x20, node matrices, the player, the options' clip
//      completion counts and their core / hontai node positions, the camera yaw, the rng) and call tick();
//   3. apply Boss07Output in order; run the engine's mesh mover on body().move_* (WAITOPS §4.1) and the alpha
//      stepper on alpha_target / alpha_step of the body and the children (both write back into the module);
//   4. call camera_hook() once per camera step with the current region slot.
//
// Tags: [BYTES]/[DATA]/[H] as the spec. [not modelled] = engine behaviour left out, listed where it happens.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "cleria/ef2_sim.hpp"   // ef2sim::M4 and the D3DX matrix helpers
#include "cleria/mathx.hpp"
#include "cleria/worldgauge.hpp"

namespace cleria {

namespace boss07 {

inline constexpr int kClips = 23;          // the ctor's clip table blk+0x30..0x88 (21, 22 = never read) [P1 §3.2]
inline constexpr int kSections = 21;       // BOSS07.YMO clips; slot k = section k for k = 0..20
inline constexpr int kNodes = 12;          // the node table blk+0x00..0x2c
inline constexpr int kParts = 3;           // obj+0x278: the body core + one per option
inline constexpr int kEmblem = 7;          // the HP-bar emblem word 0x76c214 [BYTES 0x54647c]
inline constexpr float kBarLen = 9000.0f;  // FUN_0043a460's lenScale [BYTES 0x562b92]
inline constexpr int kBodyNodes = 171;     // *(M+0x1c) of BOSS07.YMO [DATA; D23 R13]
inline constexpr int kDebrisLatches = 100; // blk+0x231..0x294

// Clip slots = YMO sections [P1 §3.2].
enum Clip : int {
    kNWait = 0, kNDamage, kN2W, kN2F, kN2G, kWWait, kWDamage, kFWait, kFDamage, kGWait, kGDamage,
    kNShootL, kNShootR, kNImpactR, kKiaiStart, kKiaiLoop, kTunagi, kMeteor, kMeteorFromWait, kSisLazerStart,
    kSisLazer,
};
// Node table entries [P1 §3.2].
enum Node : int {
    kCore = 0, kHontai, kHandR, kHandL, kNailR0, kNailR1, kNailR2, kNailR3, kNailL0, kNailL1, kNailL2, kNailL3,
};
enum Barrier : int { kBare = 0, kBarW = 1, kBarE = 2, kBarF = 3, kBarD = 4 };          // blk+0xd0
enum Mode : int { kNormal = 0, kRaiseW = 1, kRaiseF = 2, kRaiseE = 3, kSuper = 4 };    // blk+0xa4
enum Child : int { kOptR = 0, kOptL = 1, kWing = 2 };                                   // blk+0xec / 0xf0 / 0xe8

struct SectionInfo { const char* name; int first, last; bool loop; };
extern const SectionInfo kSectionTable[kSections];   // [DATA ymo_boss07.txt], rate 0.25, blend 0.05

// The frame setter FUN_005f5e10's rules applied to one advance of `rate` [AB §0 "Clip timing"; BYTES
// 0x5f5e10..0x5f5f39]: a loop clip wraps while frame > last + 1 (count++ per wrap), a stop clip holds its last
// frame with count = 1. [H G1] the per-tick advance is the rate 0.25 and SetClip starts at the first frame.
void advance_clip(int section, float& frame, std::uint32_t& count, float rate = 0.25f);

// Tables read raw [AB §F].
extern const std::int16_t kCooldown[3];        // s16 0x68bbc8: 110 / 100 / 90
extern const std::int16_t kShotBase[3];        // s16 0x68bbd8: 180 / 170 / 160
extern const std::int16_t kOptionHp[3];        // s16 0x68bbe8: 1000 / 1200 / 1400
extern const std::uint32_t kWindow[3];         // u32 0x68bb9c: 600 / 700 / 760 (element barrier)
extern const std::uint32_t kDarkWindow[3];     // u32 0x68bb34: 700 / 800 / 860 (dark barrier)
extern const std::int16_t kBarrierHits[4][5];  // ptr 0x68bbf0[flag150] -> s16 rows, index = blk+0xd0
extern const std::int16_t kCircleDelay[3];     // s16 0x68bbe0: 120 / 110 / 100
extern const float kSuctionA[4], kSuctionB[4]; // f32 0x68bbb8 / 0x68bba8 by flag150
extern const std::uint32_t kAttacks0[6], kAttacks1[7], kAttacks2[3][8];   // u32 0x69dc60 / 0x69dc44 / 0x69dc24, 04, e4
// T = base + 2 * (base / 5) * flag144 [AB §B.3].
int barrier_threshold(int character, int barrier, int diff_index);

}  // namespace boss07

// The process statics of the band [P1 §6.1; CD1 §2.4 note]: no writer outside FUN_00549020 / FUN_00549eb0 /
// FUN_0054a890 and the ctor does not touch them, so they survive a retry. The host carries them across attempts
// (Boss07::statics() -> Boss07Setup::statics); a fresh process starts them at 0 (fade: [H] its initial value is
// not read before the first non-super frame writes 1.0).
struct Boss07Statics {
    int shoot_n = 0;                          // 0x7d9b9c: N_SHOOT_R bullet counter mod 5
    float cam_h = 0, cam_d = 0;               // 0x7d9ba0 / 0x7d9ba4: camera height / extra distance
    int wing_n = 0;                           // 0x7d9ba8: the glare pulse counter (+6 per tick)
    float wing_alpha = 0;                     // 0x7d9bac
    float opt_alpha[2] = {0, 0};              // 0x7d9bb4 (R) / 0x7d9bb0 (L)
    int super_n = 0;                          // 0x7d9bb8: super frame counter
    float fade = 1.0f;                        // 0x746f38
};

struct Boss07Setup {
    float eex_hp = 2500, eex_str = 255, eex_def = 255;   // EEX record 6 [DATA; P1 §3.3 item 5], level 49
    int character = 1;                        // g_flags[150]: 0 Adol, 1 Yunica, 2 Hugo, 3 Toal
    int diff_index = 0;                       // g_flags[144]: 0 Normal, 1 Hard, 2 Nightmare
    bool diff_easy = false;                   // g_flags[143]
    bool diff_very_easy = false;              // g_flags[203]
    bool boss_rush = false;                   // g_flags[193]
    int retries = 0;                          // g_flags[196]
    Vec3 pos{0, 0, 0};                        // Mesh_Locate [DATA S_7080.XSO]; the ctor forces y = 4.0
    Boss07Statics statics{};
};

struct Boss07Input {
    bool flag156 = true;                      // 0x76bb8c: the only fight gate [P1 §4]
    Vec3 player_pos{};                        // P+0x50
    float player_hp = 1;                      // P+0x98 (the suction's and the return's test)
    float player_hp_mirror = 1;               // [0x76a730]
    int player_section = 0;                   // P+0x994 (5 freezes the circle)
    float cam_yaw = 0;                        // f32[0x7b343c + 0x4a0 * [0x7b38f0]] (attach rotation of 522 / 33)
    int section = 0;                          // A+0x18
    std::uint32_t count = 0;                  // A+0x1c
    float frame = 310.0f;                     // A+0x20: the YMO frame the executor tests with exact equality
    std::array<ef2sim::M4, boss07::kNodes> nodes{};       // world matrices of the node table entries
    std::array<std::uint32_t, 2> opt_count{}; // *(opt+0x26c)+0x1c of options R / L
    std::array<Vec3, 2> opt_core{}, opt_hontai{};         // OptNode(i, "core" / "hontai") world positions
    std::function<std::uint32_t()> rng;       // one xorshift128 word per call
    // FUN_00584550 @0x5501bd, called in place (Boss07Sprite): its 28 draws come before the loop's next draw.
    std::function<void(const Vec3& pos, float size)> dust;
};

enum class Boss07Call { kFx, kFx2, kInline };            // FUN_00450410 / FUN_00450880 / brain-inlined GoEffect
// Attach parent: a body node matrix (index = boss07::Node), the body matrix D+0x20, an option's matrix opt+0x20
// (index 0 R / 1 L), the player's matrix P+0x20.
enum class Boss07Parent { kNone, kNode, kBody, kOption, kPlayer };

struct Boss07Clip { int slot = 0, section = 0; };        // SetClip: FUN_005f5c80(M, blk[0x30 + 4k], 1; xmm3 0)
struct Boss07Motion { int mesh = 0; const char* name = ""; };   // QueueMotion(child, name) [P1 conventions]
struct Boss07Sound { int id = 0; bool se2 = false; };    // SE FUN_0043a040(id, 0x400) / SE2 "load if needed"
struct Boss07Effect {
    Boss07Call call = Boss07Call::kFx;
    int id = 0;
    Vec3 pos{}, rot{}, scale{1, 1, 1};
    int side = 1;                             // 1 enemy; 0 for 522 / 33 [D23 R4]
    bool stat = true;                         // stat block = &D+0x94 (ATK = Dalles' STR at the spawn); false = none
    float power = 0, knock = 1;
    int rtype = 0, status = 0, cls = -1;      // src 0 everywhere
    int handle = 0;                           // != 0: the module keeps this effect's handle (kill_handles names it)
    Boss07Parent parent = Boss07Parent::kNone;
    int parent_index = 0;
    Vec3 at_off{}, at_rot{}, at_scale{1, 1, 1};
    int at_mode = 0;                          // 1 full follow, 2 translation only
    // != 0: the phase-end form [AB §B.6.3]: fx+0x120 (local matrix) = uniform scale, and the attach is written
    // inline (fx+0x1a4 = parent, fx+0x1a0 = fx+0x20, byte fx+0x1d5 = at_mode) instead of FUN_00451c20.
    float local_scale = 0;
    bool flag201 = false;                     // fx+0x201 = 1: world rays skip the boss meshes [CD1 §6]
    std::uint32_t or_1dc = 0;                 // kInline: fx+0x1dc |= 0x100
};
// raw = false: FUN_00593550(ecx D, edx kind, effect, &pos, &dir, f0..f12, out 0); raw = true: a GoXx from pool
// 0x730240 + FUN_00593b90(&block) with the same 13 floats. f5..f7 are raw ints (bit-cast), the homing shot's f12
// the packed aim dword. Kinds 0x25 / 0x26 / 0x27 are not in src/cleria/bullet.cpp yet [CD1 §4.2, §5.5].
struct Boss07Bullet {
    bool raw = false;
    int kind = 0, effect = 0;
    Vec3 pos{}, dir{};
    float f[13] = {};
    bool flag201 = false;
};
// The full-screen colour fader 0x769f1c..34 [P1 conventions "Flash"; field roles H]: only the set members are
// written; zero_30: [0x769f30] = 0.
struct Boss07Fader {
    std::optional<std::uint32_t> colour;      // 0x769f34
    std::optional<float> current;             // 0x769f1c
    std::optional<float> step;                // 0x769f28
    std::optional<float> target;              // 0x769f24
    bool zero_30 = false;
};
// The engine mesh mover's record D+0x3e8..0x3f8 as written this frame (also kept in Boss07Body).
struct Boss07MoveTo { Vec3 target{}; float radius = 0, speed = 0; };
// FUN_00584550(ecx 7, 0xc0bda684, 0, .., &pos, size, 0.32, 0.012, 0, 0): the death's dust sprite [D23 §8].
struct Boss07Sprite { Vec3 pos{}; float size = 0; };
// What the band writes on the player.
struct Boss07Player {
    Vec3 push{};                              // P+0x338 / P+0x340 += (x, z): the body push-out [CD1 §3.1]
    Vec3 suction{};                           // P+0x344 / P+0x34c += (x, z): knock channel 2 [D23 §3]
    std::optional<Vec3> pos;                  // P+0x50..0x58 = (the super's pin, the return)
    std::optional<bool> flyer;                // byte P+0x398
    std::optional<std::uint32_t> iframes;     // P+0x734 = max(P+0x734, n)
    std::vector<int> sections;                // FUN_00446620: the ungated ASO section setter, in order
    bool kill_7e0 = false;                    // Kill(P+0x7e0); P+0x7e0 = P+0x7dc = 0 (Hugo's attached effect)
    bool boost_tick = false;                  // FUN_00462c40(cl 1), after flags[550] = 0: Boost ends [D23 §16.5]
};
struct Boss07HpBar { float hp = 0, max_hp = 0; };   // FUN_0043a460(0x6d5fd4, .., maxHP, 9000.0; xmm3 HP)

struct Boss07Output {
    std::vector<Boss07Clip> clips;            // the body's SetClips, in order
    std::vector<Boss07Motion> motions;        // QueueMotion on a child mesh
    std::vector<Boss07Sound> sounds;
    std::vector<Boss07Effect> effects;
    std::vector<Boss07Bullet> bullets;
    std::vector<int> kill_handles;            // one effect: byte +0x1d4 = 1
    std::vector<Boss07Sprite> sprites;
    std::vector<int> rumble;                  // FUN_005d3500
    std::optional<std::pair<int, Vec3>> shake;      // 0x76a5ac = frames; 0x76a594.. = amplitude (last write wins)
    std::optional<std::pair<float, float>> zoom;    // ZoomBlur: 0x76a550 / 0x76a554, 0x7b2488 = 1
    std::vector<Boss07Fader> fader;           // in order
    std::optional<std::pair<int, float>> fog;       // ChangeFog(index, speed) FUN_0042e8f0
    std::optional<std::pair<int, float>> cam_slot;  // 0x76a1f8 / 0x76a1fc (last write wins)
    std::vector<std::pair<int, int>> flags;         // g_flags[n] = v, in order
    Boss07Player player;
    std::optional<Boss07MoveTo> move_to;
    std::optional<float> skybox_alpha;        // mesh "skybox" +0x418
    int skin_body = 0;                        // 2 / 3: body materials Boss07_00.dds / Boss07_01.dds -> ...ver2 / ver3,
                                              // material flag &= ~0x40000 [AB §B.6.4]
    bool skin_options = false;                // every option material -> Boss07op01, flag &= ~0x40000 [AB §B.6.2]
    bool particles_wipe = false;              // every GoParticle: byte +0x11 |= 2, +0x60 &= ~0x80 [D23 §4]
    std::optional<std::uint8_t> glare;        // byte 0x7b2e38 (GlareFeedback working copy) [CD1 §2.5]
    std::optional<Boss07HpBar> hp_bar;        // the main bar, when D+0x37c != HP
    bool exp = false;                         // FUN_004fa4a0(&D+0x94)
    bool kill_record = false;                 // FUN_00403cd0()
    bool gauge_hide = false;                  // [0x6d5fd8] = 0
    bool destroy = false;                     // opR, opL, wing, D: byte +0x11 |= 2, +0x60 &= ~0x80
};

// A world-anchored mini gauge record (0x76c1cc / 0x76c1e4 / 0x76c1fc: timer, value, max, anchor). The band only
// writes them; nothing in it counts the 255 timers down [AB C11, G4] — that is the HUD's.
using Boss07MiniGauge = WorldGauge;   // the shared records 0x76c1cc.. (worldgauge.hpp)

// One GoXx hurt part (type 6, owner D) [P1 §3.3 item 7; AB §B.2].
struct Boss07Part {
    Vec3 pos{};                               // +0x50 = +0x6c = +0xdc
    float radius = 0.5f;                      // +0x68 (1.5 body / 3.0 option once placed)
    float def = 255;                          // +0xa8: the copy taken before the body's DEF became 999
    float resist[4] = {0.4f, 0.4f, 0.4f, 0.4f};   // +0xb4..0xc0
    float pool = 0;                           // +0x98: 10 x the body's HP at the ctor; never refilled [AB §B.2]
    float dmg = 0;                            // +0x134: accumulated by the hit path, consumed by the next tick
};
enum class Boss07Hit { kRefused, kClank, kLanded };

// A child mesh (option R / L, the wing): plain GoMeshes driven entirely from the parent.
struct Boss07ChildMesh {
    Vec3 pos{};                               // +0x50
    Vec3 rot{};                               // (pitch, yaw, roll) of the last SetRot
    float alpha = 0;                          // +0x418 [H: the factory's initial value is not in the spec; every
                                              // fight frame rewrites it]
    float alpha_target = 0, alpha_step = 0;   // +0x41c / +0x420 (the death's fade; the host's stepper)
    float flash_2a0 = 0;                      // +0x2a0 = 1.65 when its part is hit [AB §B.7]
    float flash_2a4 = 0;                      // +0x2a4 = 1.65 (FUN_005458d0 and the inline twins)
    bool hits_off = false;                    // +0x298 & 0x40: destroyed
    bool translucent = false;                 // +0x294 & 0x1000 (the wing at the death)
    bool removed = false;                     // byte +0x11 & 2
};

// The body object D: the fields the band touches.
struct Boss07Body {
    Vec3 pos{};                               // D+0x50
    float max_hp = 2500, hp = 2500;           // D+0x94 / D+0x98
    float str = 255, def = 999;               // D+0xa4 / D+0xa8
    float flash = 0;                          // D+0x2a0
    float bar_hp = 0;                         // D+0x37c [H: initial value 0 -> the bar is refreshed on frame 1]
    Vec3 move_target{};                       // D+0x3e8..0x3f0
    float move_radius = 0, move_speed = 0;    // D+0x3f4 / D+0x3f8 (IsMoving = move_speed != 0)
    float alpha = 1;                          // D+0x418
    float alpha_target = 1, alpha_step = 0;   // D+0x41c / D+0x420 [H initial values; the death writes 0, 1/600]
    bool removed = false;
};

// The block blk = *(D+0x4a0) (0x2b4 bytes) [P1 §6.1 + the later parts' corrections].
struct Boss07Block {
    int phase = 0;                            // 0xa0
    int mode = 0;                             // 0xa4
    int next_barrier = 0;                     // 0xa8
    std::uint32_t cycle = 0;                  // 0xac
    int cooldown = 60;                        // 0xb0
    int variant = 0;                          // 0xb4
    bool refill = false;                      // 0xb8
    float meter = 0;                          // 0xbc: damage since the barrier went up / the last phase end
    int rr = 0;                               // 0xc0: queued barrier attack round-robin
    bool q_f = false, q_w = false, q_e = false;   // 0xc4 / 0xc5 / 0xc6
    std::uint32_t arm = 0;                    // 0xcc: barrage counter armed by the N_SHOOT windows
    int barrier = 0;                          // 0xd0
    std::uint32_t hits = 0;                   // 0xd4: frames with damage on the barrier
    std::uint32_t window = 0;                 // 0xd8
    std::uint32_t fc = 0;                     // 0xdc
    Vec3 aim[2]{};                            // 0xf4 (R) / 0x100 (L): aim vectors [CD1 C1]
    Vec3 aim_s[2]{};                          // 0x10c / 0x118: the aim, low-passed
    Vec3 fwd_s[2]{};                          // 0x124 / 0x130: the body forward, low-passed
    bool behind = false;                      // 0x15c
    bool aimed = false;                       // 0x164
    int rev[2] = {0, 0};                      // 0x168 / 0x16c: 180 at a revive; no reader
    float opt_hp[2] = {1000, 1000};           // 0x170 / 0x174
    int hit_pause = 0;                        // 0x178
    int shot_timer = 0;                       // 0x17c
    int shot = 0, shot_opt = 0;               // 0x180 / 0x184
    Vec3 rot_target{};                        // 0x188..0x190
    Vec3 rot{};                               // 0x1a0..0x1a8 (pitch, yaw, roll)
    Vec3 fwd0{}, fwd{};                       // 0x1ac.. (x zeroed) / 0x1b8..
    float drift_scale = 1.0f;                 // 0x1c4
    int drift_timer = 0;                      // 0x1c8
    std::uint32_t dash_bias = 0;              // 0x1cc
    float turn_scale = 1.0f;                  // 0x1d0
    int turn_timer = 0;                       // 0x1d4
    int cam_slot = 1;                         // 0x1d8
    int cam_n = 0;                            // 0x1dc
    float cam_t = 0;                          // 0x1e0
    int h_charge = 0;                         // 0x1e4: FX 338 handle
    int h_saikyou = 0;                        // 0x1e8: FX 334 handle [D23 R8]
    Vec3 circle{};                            // 0x1ec..0x1f4
    std::uint32_t circle_timer = 0;           // 0x1f8
    std::uint32_t capture = 0;                // 0x1fc
    int h_izigen = 0;                         // 0x200: FX 337 handle
    int rock_timer = 0;                       // 0x204
    Vec3 cam_eye{}, cam_look{};               // 0x208.. / 0x214..
    float cam_dist = 0;                       // 0x220: never written
    float cam_fov = 0;                        // 0x228
    bool death_started = false;               // 0x230
    std::array<std::uint8_t, boss07::kDebrisLatches> debris{};   // 0x231..0x294
    std::uint32_t death_fc = 0;               // 0x2a0
    bool buffs_cleared = false;               // 0x2a4
    int se322 = 0;                            // 0x2a8
    int volley_latch = 0;                     // 0x2ac
    int echo = 0;                             // 0x2b0
};

// The region-camera record S(k) fields the hook writes [CD1 §3.2; names DOC BOSS_CAMERA §1b].
struct Boss07CamRecord {
    Vec3 eye_fixed{};                         // +0x288
    Vec3 target_fixed{};                      // +0xb4
    float eye_dist = 0;                       // +0x274
    float fov = 0;                            // +0x418
    int eye_mode = 0, target_mode = 0, eye_flags = 0;   // +0x268 / +0x94 / +0x278
};

class Boss07 {
public:
    explicit Boss07(const Boss07Setup& s);    // FUN_00545940

    // One frame of FUN_00562990 case 0xb [AB §A.1]: turn, drift, children, parts, camera targets; then the option
    // shot, scheduler, sequencer, executor (HP >= 1) or the death; the frame counter, flash decay and HP bar.
    Boss07Output tick(const Boss07Input& in);

    // FUN_004faf20 for part i with the damage FUN_004fa550 returned (hit.hpp enemy_incoming_damage with the part's
    // def / resist) [AB §B.2]: refused when the part's own pool < 1; part+0x134 += dmg (also 0); dmg <= 0 = the
    // barrier clank (the engine plays SE 368); dmg > 0 drains the pool (floor 0). [not modelled] dmg' =
    // round(dmg * (1 + 0.1 * g_flag)) for the pool drain [DOC GELALDY G9], the boost gain and the hit particles.
    Boss07Hit hit_part(int i, int dmg) { return hit_part(i, dmg, dmg); }
    Boss07Hit hit_part(int i, int dmg, int dmg_hp);   // dmg_hp: the element bonus on the pool (the host's)

    // FUN_0042c4f0(obj, &slot) [CD1 §3.2]: rec = the host's region-camera record of the CURRENT slot k.
    bool camera_hook(int k, Boss07CamRecord& rec);

    Boss07Body& body() { return body_; }
    const Boss07Body& body() const { return body_; }
    Boss07Block& blk() { return blk_; }
    const Boss07Block& blk() const { return blk_; }
    Boss07Part& part(int i) { return parts_[std::size_t(i)]; }
    const Boss07Part& part(int i) const { return parts_[std::size_t(i)]; }
    Boss07ChildMesh& child(int i) { return child_[std::size_t(i)]; }
    const Boss07ChildMesh& child(int i) const { return child_[std::size_t(i)]; }
    Boss07MiniGauge& mini(int i) { return (gauges_ ? gauges_ : mini_.data())[i]; }
    const Boss07MiniGauge& mini(int i) const { return (gauges_ ? gauges_ : mini_.data())[i]; }
    // The engine's three records are globals the HUD tick counts down (MINI_GAUGE.md §2): the host hands
    // its store in; unbound (the unit tests) the object's own array stands for them.
    void bind_gauges(WorldGauge* g) { gauges_ = g; }
    Boss07Statics& statics() { return st_; }
    const Boss07Statics& statics() const { return st_; }
    ef2sim::M4 matrix() const;                // FUN_00503990's rows (YawPitchRoll of blk.rot) + the position row
    // FUN_0054ebb0(ecx D, edx variant, push wrap) [AB §A.5]. Public for the tests / the drive case.
    void start_attack(int variant, std::uint32_t wrap, Boss07Output& out);

private:
    struct Scope;
    void turn();                              // FUN_00546dc0
    void drift();                             // FUN_00547120
    void children();                          // FUN_00549020
    void parts();                             // FUN_005472e0
    void phase_end();                         // AB §B.5 / §B.6
    void dark_break();                        // AB §B.8
    void camera();                            // FUN_00549eb0
    void option_shot();                       // FUN_00546900
    void scheduler();                         // FUN_0054ee30
    void sequencer();                         // FUN_0054a340
    void executor();                          // FUN_0054a890
    void exec_hands();                        // CD1 §5
    void exec_super();                        // D23 §2..§5
    void exec_barriers();                     // D23 §9..§15
    void death();                             // FUN_0054fb20
    void start_attack_(int variant, std::uint32_t wrap);
    void dash(float lim, std::uint32_t mod, float speed, float radius);
    void revive();
    void spark_fx();                          // AB §B.4
    void spark(int id, int node, float base); // CD1 §5.1
    void ice_pair();
    void set_clip(int k);
    void queue_motion(int mesh, const char* name);
    void se(int id) { out_->sounds.push_back({id, false}); }
    void se2(int id) { out_->sounds.push_back({id, true}); }
    void kill(int& h);
    Boss07Effect& fx(int id, const Vec3& p, Boss07Call call = Boss07Call::kFx);
    void bullet(bool raw, int kind, int effect, const Vec3& pos, const Vec3& dir, const float* f);
    void cam_slot(int n, float speed);
    void player_section(int n);
    Vec3 node(int k) const;
    std::uint32_t W();
    std::uint32_t R(std::uint32_t n) { return (W() >> 17) % n; }
    float U();
    bool bit17() { return (W() & 0x20000u) != 0; }
    float Rf(float lo, float hi);
    bool eligible() const { return (sec_ == boss07::kNWait || sec_ == boss07::kNDamage) && !blk_.refill; }

    Boss07Body body_{};
    Boss07Block blk_{};
    std::array<Boss07Part, boss07::kParts> parts_{};
    std::array<Boss07ChildMesh, 3> child_{};
    std::array<Boss07MiniGauge, 3> mini_{};
    WorldGauge* gauges_ = nullptr;
    Boss07Statics st_{};
    int char_ = 1, diff_ = 0;
    bool easy_ = false, very_easy_ = false, rush_ = false;
    int next_handle_ = 1;
    // this call
    const Boss07Input* in_ = nullptr;
    Boss07Output* out_ = nullptr;
    int sec_ = 0;                             // A+0x18, live (a SetClip changes it within the frame)
    std::uint32_t cnt_ = 0;                   // A+0x1c, zeroed by a SetClip
    float f_ = 0;                             // A+0x20 [H V1: a SetClip leaves it at the section's first frame]
    std::array<std::uint32_t, 2> opt_cnt_{};
    Vec3 ppos_{};                             // P+0x50, live (the super's pin and the return move the player)
    int psec_ = 0;                            // P+0x994, live
};

}  // namespace cleria
