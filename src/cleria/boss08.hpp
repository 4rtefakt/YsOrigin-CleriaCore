// Darm (GoMeshes "boss08" / "boss08t" / "boss08w" / "boss08Arm", type byte 12, room S_7099) — pure and headless.
//
// Ported from build/boss_re/DARM.md (pass 1: the room, the ctor FUN_00550aa0, the block db = *(obj+0x4a4), the
// tables), DARM_A.md (the shell FUN_0055ddc0, the body motion FUN_00552120, the push-out + High camera block
// FUN_00552510, clip chaining FUN_00559a50, the form change FUN_0055a150, the core's clip rule / aura, MatMode),
// DARM_B1.md (bank I: swings, hand raises, the breath, the Bit spawner FUN_00552730, the homing volley
// FUN_00552c30), DARM_B23.md (bank II / III: spear, High form, OnBack, the detached arms, limb smoke, lariat),
// DARM_CE.md (the tail: chaining FUN_00559dd0, timelines FUN_00558290, decisions FUN_0055cb70, the override
// tracks; the dispatcher FUN_0055d170, the four pickers, the camera hook FUN_0055f800) and DARM_D.md (parts and
// damage FUN_0055aa80, death FUN_0055d3d0). Section references below are "P1 §n", "A §n", "B1 §n", "B23 §n",
// "CE §n", "D §n". Where the parts disagree the later, more specific one is followed (each has a corrections
// table).
//
// Four GoMeshes ticked in creation order B (body), T (tail), W (core), A (arms) [H A G2], reading each other's
// blocks live; the floor s7099yuka (Y) is only read. The module owns the object fields the band touches, the
// four blocks, the nine GoXx hurt parts (8 on the body, 1 on the core), the three sub-gauges, the eight tentacle
// override-track records and the MoveTo state + mover of B and W. Everything that needs a host facility
// (effects, bullets, sounds, the player, the camera, the fader, the light, flags, the HUD, materials, the sixth
// mesh) is an entry of Boss08Output; nothing is done here.
//
// The per-frame host contract:
//   1. deliver the frame's hits with hit_part() (the engine's FUN_004faf20 gate / accumulate / pool drain);
//   2. fill Boss08Input (clip state of B / T / W / Y, node positions, the player, the rng) and call tick();
//   3. apply Boss08Output in order. A motion request is applied at the end of the target mesh's own tick
//      (FUN_00503190 -> FUN_005f5c10, A §0); a SetClip at once. Pose T's tentacles from track(i) (CE §2.3);
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

namespace boss08 {

inline constexpr int kBodyClips = 39, kTailClips = 27, kCoreClips = 6, kArmClips = 3, kFloorClips = 13;
inline constexpr int kBodyNodes = 24;      // the node table 0x7b9328 [P1 §3.2]
inline constexpr int kTailNodes = 49;      // 0x7b9388
inline constexpr int kBodyMats = 41;       // the material id table 0x7b9458 (BOSS08.YMO has 51 materials)
inline constexpr int kBodyParts = 8;       // obj+0x278 of B; W has 1
inline constexpr int kTracks = 8;          // An(T)+0x58: the override records 0x75acb0
inline constexpr int kEmblem = 8;          // the HP-bar emblem word 0x76c214 [BYTES 0x551ffd]
inline constexpr float kBarLen = 9999.0f;  // FUN_0043a460's lenScale [BYTES 0x55f7a3]
inline constexpr int kBitKind = 0x28;      // the deployed Bit's bullet kind [B1 §6.2]

enum Mesh : int { kB = 0, kT, kW, kA, kY };
enum Form : int { kNormal = 0, kLow = 1, kHigh = 2, kOnBack = 3 };          // db(B)+0x18
// Body nodes = indices into 0x7b9328 [P1 §3.2].
enum BNode : int {
    kSkirt1 = 0, kSkirt2, kHead, kRightLeg, kRightFoot, kLeftLeg, kLeftFoot, kSpear, kSpearMark,
    kLStigma1, kLStigma2, kLStigma3, kRStigma1, kRStigma2, kRStigma3, kRightToe23Mark, kLeftToe23Mark,
    kRightToe31, kLeftToe31, kBerryMouthMark, kMaekake, kSpine2, kLeftToe11, kRightToe11,
};
inline constexpr int kTNodeEye = 1;        // T node table index of Bone_eye

struct ClipInfo { const char* name; int first, last; bool loop; };
// [DATA ymo_*.txt via P1 §3.2]; section = index; rate 0.25 on every clip of B / T.
extern const ClipInfo kBodyClipTable[kBodyClips];
extern const ClipInfo kTailClipTable[kTailClips];
extern const ClipInfo kCoreClipTable[kCoreClips];
extern const ClipInfo kArmClipTable[kArmClips];
extern const ClipInfo kFloorClipTable[kFloorClips];
int clip_by_name(int mesh, const char* name);          // FUN_005f5c10's stricmp walk; -1 = no match

// The frame setter FUN_005f5e10's rules applied to one advance of `rate` [DOC DALLES_AB §0 "Clip timing"]:
// a loop clip wraps while frame > last + 1 (count++ per wrap), a stop clip holds its last frame with count = 1.
// [H A G3] the per-tick advance is the rate 0.25 and SetClip starts at the first frame.
void advance_clip(const ClipInfo& c, float& frame, std::uint32_t& count, float rate = 0.25f);

// Tables read raw [P1 §3.4; D §7], index = g_flags[144].
extern const float kLimbStep[3];           // f32 0x68bca4 / 94 / 84: 0.03 / 0.025 / 0.0225
extern const float kPushOthers[3];         // f32 0x68bf64: -0.1 / -0.12 / -0.14
extern const float kPushToal[3];           // f32 0x68c00c: -0.1 / -0.15 / -0.2
extern const std::uint32_t kHomingExtra[3];    // u32 0x68c018: 90 / 60 / 0
extern const std::uint32_t kHomingBase[3];     // u32 0x68c024: 420 / 390 / 360
extern const std::uint32_t kBarrierHits[3];    // u32 0x68c030: 60 / 70 / 80
extern const std::uint32_t kWindow[3];         // u32 0x68c03c: 900 / 720 / 600
extern const std::uint32_t kSwingCd[3];        // u32 0x68c048: 440 / 390 / 340
extern const std::uint32_t kAttackCd[3];       // u32 0x68c054: 180 / 140 / 100
inline constexpr std::uint32_t kExposure = 600;    // u32 0x68c060[db(W)+0x18 = 0]; 360 / 480 / 999 are dead [A C11]
extern const float kPartRadius[kBodyParts];    // f32 0x69ee98
extern const Vec3 kBitSpawn[2][6], kBitTarget[2][6];   // 0x7dded0 / 0x7ddf70, 0x7ddf20 / 0x7ddfc0 [B1 §5]
extern const Vec3 kVortex[kTracks];            // 0x7de010 [CE §3.2 (3)]
extern const char* const kTentacleNode[kTracks];   // name(k) [CE §3.2 (2)]
inline constexpr int tentacle_track(int k) { return k < 4 ? k + 4 : k - 4; }   // trk(k)

// One override-track record (0x1c bytes) [CE §2.1]. `owns` = the sub-tree's node records carry +0x108 = this
// track (set by a bind, cleared when a release fade ends). The bone evaluation [CE §2.3, BYTES 0x5f5f50..]:
// owns && !releasing -> the sub-tree plays `section` at `frame` with cross-fade weight `weight`; owns &&
// releasing -> the main clip with weight `weight`; !owns -> the main clip.
struct Track {
    float weight = 0;                         // +0x00: 1.0 -> 0.0 by dt * 0.05 per tick
    int section = 0;                          // +0x04
    int count = 0;                            // +0x08
    float frame = 0;                          // +0x0c
    float speed = 1.0f;                       // +0x10 (ctor 1.0)
    int root = -1;                            // +0x14: the tentacle index k whose "-1" bone is the root
    bool releasing = false;                   // +0x18
    bool owns = false;
};
void track_bind(Track& t, int k, int section);          // FUN_005f4f10, clip >= 0 [CE §2.2]
void track_release(Track& t);                           // FUN_005f4f10, clip < 0 (and its inline copy)
// One record of the model tick FUN_005f6000 [CE §2.3] (the tail's clips: next -2 / -1 only; the `next >= 0`
// branch is dead for Darm and [not modelled]). [H CE K4] dt = 1.0.
void track_step(Track& t, float dt = 1.0f);

// The GoMesh mover FUN_00503190 @0x5032e4 [A §0]: one step of the MoveTo state.
void mover_step(Vec3& pos, const Vec3& target, float radius, float& speed);

// The three intro cameras the ctor writes into region slots 7 / 8 / 9 [P1 §3.3 item 13] (the host's at creation).
struct IntroCam { Vec3 eye, target; float fov; };
extern const IntroCam kIntroCams[3];

}  // namespace boss08

// The process statics of the band: the two bank effect handles [B23 P6] and the camera hook's eight function
// statics [CE §4.7]. The host carries them across attempts (Boss08::statics() -> Boss08Setup::statics).
struct Boss08Statics {
    int h_darkwave = 0;                       // 0x7d9bc0
    int h_rolling = 0;                        // 0x7d9bbc
    std::optional<float> fov1, pit1, h1;      // 0x7de074 / 7c / 84
    std::optional<float> fov2, pit2;          // 0x7de08c / 94
    std::optional<float> fov4, pit4, h4;      // 0x7de09c / a4 / ac
};

struct Boss08Setup {
    float eex_hp = 9999, eex_str = 258, eex_def = 275;   // EEX record 7 [DATA; P1 D2], level 50
    int character = 3;                        // g_flags[150]: 0 Adol, 1 Yunica, 2 Hugo, 3 Toal
    int diff_index = 0;                       // g_flags[144]: 0 Normal, 1 Hard, 2 Nightmare
    bool diff_easy = false;                   // g_flags[143]
    bool diff_very_easy = false;              // g_flags[203]
    bool boss_rush = false;                   // g_flags[193]
    int retries = 0;                          // g_flags[196]
    Vec3 pos{0, 0, 8.5f};                     // Mesh_Locate [DATA S_7099.XSO]
    Boss08Statics statics{};
};

struct Boss08Input {
    bool flag156 = true;                      // 0x76bb8c: the only fight gate [P1 §1.3]
    int flag227 = 0;                          // 0x76bca8: >= 2 releases the death timeline (story)
    Vec3 player_pos{};                        // P+0x50
    float player_hp = 1;                      // P+0x98
    float player_hp_mirror = 1;               // [0x76a730]
    bool player_grounded = true;              // byte P+0x388
    int player_section = 0;                   // P+0x994 (0x13d = Toal's burst)
    int bits_deployed = 0;                    // live bullets of kind 0x28 (the Bit cap) [B1 §5]
    bool fx_pool_full = false;                // [0x730134] <= [0x73012c]: the inline spawns are refused [CE §0]
    int b_section = 0;                        // An(B)+0x18
    std::uint32_t b_count = 0;                // An(B)+0x1c
    float b_frame = 10.0f;                    // An(B)+0x20: the YMO frame the bank tests with exact equality
    int t_section = 0;
    std::uint32_t t_count = 0;
    float t_frame = 10.0f;
    int w_section = 2;                        // An(W)+0x18 (03_Wait)
    int y_section = 0;                        // An(Y)+0x18 [H A §A.4: the floor starts in section 0 "a"]
    std::uint32_t y_count = 0;                // An(Y)+0x1c
    // World positions of the body's node table entries. [H B23 U-B2 / D U-D1] the record's +0x30 and +0xb0
    // translations are both taken as this.
    std::array<Vec3, boss08::kBodyNodes> b_node{};
    Vec3 t_eye{};                             // T's Bone_eye (the core rides it in OnBack)
    Vec3 w_ora{};                             // W's OraMarker
    // node+0x130..0x138 of the tail's node table entries (the spark offsets) [CE §3.2 (4), K3].
    std::array<Vec3, boss08::kTailNodes> t_local{};
    std::function<std::uint32_t()> rng;       // one xorshift128 word per call
};

enum class Boss08Call { kFx, kFx2, kInline };            // FUN_00450410 / FUN_00450880 / brain-inlined
// Attach parent: the body matrix B+0x20, a body node record (index = boss08::BNode), a tail node record (index
// into 0x7b9388), W's OraMarker record.
enum class Boss08Parent { kNone, kBody, kBodyNode, kTailNode, kCoreOra };
enum class Boss08Stat { kBody, kTail, kCore };           // stat block = &B+0x94 / &T+0x94 / &W+0x94 at the spawn

struct Boss08Clip { int mesh = 0, section = 0; };        // SetClip: FUN_005f5c80(M, section, 1; xmm3 0)
// The named-motion request X+0x424 [A §0]: flag = X+0x444 (SetClip's widen / cross-fade byte), start 0.
struct Boss08Motion { int mesh = 0; const char* name = ""; bool flag = true; };
struct Boss08Sound { int id = 0; bool se2 = false; };    // SE FUN_0043a040(id, 0x400) / SE2 "load if needed"
struct Boss08Effect {
    Boss08Call call = Boss08Call::kFx;
    int id = 0;
    Vec3 pos{}, rot{}, scale{1, 1, 1};
    int src = 0, side = 1;
    Boss08Stat stat = Boss08Stat::kBody;
    float power = 0, knock = 1;
    int rtype = 0, status = 0, cls = -1;
    int handle = 0;                           // != 0: the module keeps this effect's handle (kill_handles names it)
    Boss08Parent parent = Boss08Parent::kNone;
    int parent_index = 0;
    Vec3 at_off{}, at_rot{}, at_scale{1, 1, 1};
    int at_mode = 0;                          // 1 full follow, 2 translation only
    bool flag201 = false;                     // fx+0x201 = 1: world rays skip the boss meshes
    std::uint32_t or_1dc = 0;                 // kInline: fx+0x1dc |= 0x100 (0x108 for the break's 514)
};
// raw = false: FUN_00593550(ecx B, edx kind, effect, &pos, &dir, f0..f12, out 0); raw = true: a GoXx from pool
// 0x730240 + FUN_00593b90(&block). f5..f7 are raw ints (bit-cast), a kind-0x25 f12 the packed target dword.
// The owner is always B (side 1, stat &B+0x94).
struct Boss08Bullet {
    bool raw = false;
    int kind = 0, effect = 0;
    Vec3 pos{}, dir{};
    float f[13] = {};
    bool flag201 = false;
};
// The full-screen colour fader 0x769f1c..34 [A §0 "Fade"; field roles H]: only the set members are written.
struct Boss08Fader {
    std::optional<std::uint32_t> colour;      // 0x769f34 (with [0x769f30] = 0)
    std::optional<float> current;             // 0x769f1c
    std::optional<float> step;                // 0x769f28
    std::optional<float> target;              // 0x769f24
};
struct Boss08Shake { int frames = 0; float ax = 0, ay = 0; };   // 0x76a5ac / 0x76a594 / 0x76a598, 0x76a59c = 0
// The mover's external push P+0x5d0..0x5d8 for P+0x5dc ticks [B23 §0].
struct Boss08Push { Vec3 v{}; int ticks = 0; };
// What the band writes on the player.
struct Boss08Player {
    Vec3 push{};                              // P+0x338 / P+0x340 += (x, z): the body push-out [A §A.3]
    Vec3 knock2{};                            // P+0x344 / P+0x34c += (x, z): the cannon's pull [B1 §4.1]
    std::optional<Boss08Push> ext;            // last write of the frame
    std::optional<Vec3> pos;                  // P+0x50..0x58 =
    std::optional<std::uint32_t> tint;        // P+0x148 = P+0x278 =
    std::optional<std::uint32_t> iframes;     // P+0x734 = n
    std::optional<std::uint32_t> iframes_min; // P+0x734 = max(P+0x734, n)
    std::vector<int> sections;                // FUN_00446620(P+0x8f0, n), in order
    bool dead_stop = false;                   // HP < 1: P+0x5d0..0x5dc = 0, P+0xa3c..0xa44 = 0 [A §A.1.2]
    bool flyer = false;                       // byte P+0x398 = 1
    // The fall's start: FUN_00461160(0), P+0x9fc = 0, P+0x314 = P+0x308 = -pi/2, and with section 0x13d the kill
    // bytes of P+0x7b4 / P+0x7b8 (Toal's burst effects).
    bool fall_start = false;
    // The fall's end: P+0x9fc = 0, P+0x344.. = P+0x338.. = P+0x5d0.. = 0, P+0x5dc = 0.
    bool fall_end = false;
};
struct Boss08HpBar { float hp = 0, max_hp = 0; };   // FUN_0043a460(0x6d5fd4, W+0x14, 6, maxHP, 9999.0; xmm3 HP)
// FUN_005f4f10 on the tail model [CE §2.2]: bind = the node's sub-tree gets +0x108 = track, FUN_005f6400(M,
// track) snapshots the blend-from pose, An(T)+0x1c = 0; release = the snapshot only. The record itself is
// Boss08::track(track).
struct Boss08TrackEvent { bool bind = false; int track = 0; const char* node = ""; int section = -1; };

struct Boss08Output {
    std::vector<Boss08Clip> clips;            // in order
    std::vector<Boss08Motion> motions;
    std::vector<Boss08Sound> sounds;
    std::vector<Boss08Effect> effects;
    std::vector<Boss08Bullet> bullets;
    std::vector<int> kill_handles;            // one effect: byte +0x1d4 = 1
    std::vector<Boss08TrackEvent> tracks;
    std::vector<int> rumble;                  // FUN_005d3500
    std::optional<Boss08Shake> shake;         // last write wins
    std::optional<std::pair<float, float>> zoom;    // ZoomBlur: 0x76a550 / 0x76a554, 0x7b2488 = 1
    std::vector<Boss08Fader> fader;           // in order
    std::optional<std::pair<int, float>> cam_slot;  // 0x76a1f8 / 0x76a1fc (last write wins)
    std::vector<std::pair<int, int>> flags;         // g_flags[n] = v, in order
    Boss08Player player;
    std::optional<std::uint32_t> light;       // FUN_00443ea0(colour, &(-0.182574, -0.912871, 0.365148)) [CE §4.1]
    bool death_light = false;                 // 0x76a564..6c = that direction, 0x76a570..78 = 0.376471 [D §5]
    std::vector<int> mat_mode;                // MatMode(B, m) calls [A §A.6]: RGB of the body + tail materials
    bool mgcra_spawn = false;                 // create bs08_mgcra at the origin, step + touch bits, Touch_Tail [B23 §4.6]
    bool mgcra_delete = false;                // FUN_0040b290 on it
    bool kill_bit_particles = false;          // every GoParticle of effect 467: +0x11 |= 2, +0x60 &= ~0x80 [D C8]
    std::optional<Boss08HpBar> hp_bar;        // the main bar, when W+0x37c != W.HP
    bool gauge_hide = false;                  // [0x6d5fd8] = 0
    bool kill_record = false;                 // FUN_00403cd0()
};

// A world-anchored sub-gauge (0x76c1cc / 0x76c1e4 / 0x76c1fc: timer, value, max, anchor). Nothing in the band
// counts the 255 timers down [D U-D7] — that is the HUD's.
using Boss08MiniGauge = WorldGauge;   // the shared records 0x76c1cc.. (worldgauge.hpp)

// One GoXx hurt part (type 7, sphere) [D §0, §2].
struct Boss08Part {
    Vec3 pos{};                               // +0x50 = +0x6c
    float radius = 1.6f;                      // +0x68
    float def = 998;                          // +0xa8
    bool armor = false;                       // +0x138
    float power = 1.0f;                       // +0xfc
    float pool = 0;                           // +0x98: 10 x the owner's HP at the ctor; never refilled
    float dmg = 0;                            // +0x134: accumulated by the hit path, consumed by the next tick
};
enum class Boss08Hit { kRefused, kZero, kLanded };

// The object fields the band touches (one record per GoMesh B / T / W / A).
struct Boss08Mesh {
    Vec3 pos{};                               // +0x50 (T: glued by the host to B's Bone_Skirt1 matrix, not kept here)
    float yaw = 0;                            // A only: Place()'s yaw. B's is Boss08BodyBlock::yaw.
    float max_hp = 0, hp = 0;                 // +0x94 / +0x98
    float str = 0, def = 0;                   // +0xa4 / +0xa8
    float flash = 0;                          // +0x2a0
    float bar_hp = 0;                         // W+0x37c [H: initial value 0 -> the bar is refreshed on frame 1]
    float alpha = 1;                          // +0x418 [H: the factory's initial value is not in the spec]
    float alpha_target = 1;                   // +0x41c
    bool touch = false;                       // +0x28c & 0x0c with +0x2e4 = "Touch_Tail" [CE §0 "Touch on"]
    // Every collision entry's byte +0x89 = 1: the player cannot jump from it [EXPORT 00465170.c:234-240] and is slid
    // off it [BYTES 0x45ad20..0x45adcd] (view_game.inl). It is NOT the collision itself: the hull stays.
    bool no_jump = true;
    bool no_hit = false;                      // +0x298 & 0x40
    Vec3 move_target{};                       // +0x3e8..0x3f0
    float move_radius = 0, move_speed = 0;    // +0x3f4 / +0x3f8 ("moving" = move_speed != 0)
};

// db(B) [P1 §4 + the later parts' corrections].
struct Boss08BodyBlock {
    int form = 0, form_req = 0;               // 0x18 / 0x1c
    std::uint32_t fall = 0;                   // 0x24: the fall-off sequence counter
    std::uint8_t fall_tint = 0;               // 0x2c
    std::uint32_t window = 0;                 // 0x30: OnBack open frames (0 = barrier up); 9999 once dead
    int bit_layout = 0;                       // 0x34
    int last_group = 0;                       // 0x38
    std::uint32_t barrier_hits = 0;           // 0x3c
    bool down = false;                        // 0x40
    int breath = 0;                           // 0x44: 0 fan, 1 cone, 0x63 the cannon
    std::uint32_t cooldown = 180;             // 0x48
    bool homing_req = false;                  // 0x54
    std::uint32_t homing_timer = 0;           // 0x58
    bool settled = false;                     // 0x5c: recomputed every frame
    bool restore = false, cam_back = false;   // 0x60 / 0x64
    std::uint32_t fc = 0;                     // 0x70
    bool low_latch = false;                   // 0x74
    std::uint32_t jump_lock = 0;              // 0x78
    float y_target = 0, y = 0, y_step = 0.04f;    // 0x7c / 0x80 / 0x84 (dead: the target has no non-zero writer)
    float move_base = 0; std::uint32_t move_base_t = 0;   // 0x88 / 0x8c  pair0
    float turn_base = 0; std::uint32_t turn_base_t = 0;   // 0x90 / 0x94  pair1
    float move_over = 0; std::uint32_t move_over_t = 0;   // 0x98 / 0x9c  pair2
    float turn_over = 0; std::uint32_t turn_over_t = 0;   // 0xa0 / 0xa4  pair3
    float pitch = 0, yaw = 0, roll = 0;       // 0xa8 / 0xac / 0xb0 (pitch / roll have no writer)
    float thunder_r = 0, thunder_l = 0;       // 0xb4 / 0xb8
    float thunder_r_t = 0, thunder_l_t = 0;   // 0xbc / 0xc0
    float eye = 1;                            // 0xc4
    float chest = 1, chest_t = 1;             // 0xc8 / 0xcc
    float leg_l = 1, leg_r = 1;               // 0xd0 / 0xd4
    float leg_l_t = 1, leg_r_t = 1;           // 0xd8 / 0xdc
    float hand_l = 1, hand_r = 1;             // 0xe0 / 0xe4
    float hand_l_t = 1, hand_r_t = 1;         // 0xe8 / 0xec
    std::uint32_t beat_r = 0, beat_l = 0, clap = 0;   // 0xf0 / 0xf4 / 0xf8
    std::uint32_t quake_n = 0;                // 0x100
    bool mgcra = false;                       // 0x104: the sixth mesh exists
    std::uint32_t smoke[3] = {0, 0, 0};       // 0x118 + 4g
    std::uint32_t regrow[3] = {0, 0, 0};      // 0x124 + 4g
    int h_beam = 0;                           // 0x130: FX 460 handle
    Vec3 cam_eye{}, cam_target{};             // 0x1d0.. / 0x1dc..
    float cam_dist = 0;                       // 0x1e8: never written
    float cam_fov = 0;                        // 0x1f0
    Vec3 cam_off{};                           // 0x1f8..0x200
    bool dead = false, death_started = false; // 0x20c / 0x20d
    std::uint32_t death_t = 0;                // 0x27c
    bool buffs_cleared = false;               // 0x280
    int cam_idx = 1;                          // the module's copy of CamIdx 0x76a1f8 (ctor 1) [H: no other writer
                                              // during the fight]
};
// db(T) [CE §5].
struct Boss08TailBlock {
    int form = 0;                             // 0x18: the tail's own copy
    std::uint32_t tentacle_cd = 0;            // 0x4c
    std::uint32_t swing_cd = 0;               // 0x50
    std::uint32_t fc = 0;                     // 0x70
    std::uint32_t spark_t = 0;                // 0xfc
    std::uint32_t laser = 0;                  // 0x134
    std::uint32_t beat[boss08::kTracks] = {}; // 0x138 + 4k
    std::uint32_t stab[boss08::kTracks] = {}; // 0x158 + 4k
    int vortex[boss08::kTracks] = {};         // 0x178 + 4k: FX 481 handles
    std::uint32_t dead_stab = 0;              // 0x198
};
// db(W).
struct Boss08CoreBlock {
    int weak = 0;                             // 0x20: 0 belly, 1 left leg, 2 right leg
    std::uint32_t band = 0;                   // 0x68
    bool refill = false;                      // 0x6c
    std::uint32_t fc = 0;                     // 0x70
    int h_barrier = 0;                        // 0x19c: FX2 144 handle
    float alpha_t = 0, alpha = 0;             // 0x1ac / 0x1b0
    std::uint32_t exposure = 0;               // 0x1b4
    bool was_exposed = false;                 // 0x1b8
    int seq = 0;                              // 0x1bc
    std::uint32_t seq_wait = 0;               // 0x1c0
    bool dropped = false;                     // 0x1c4
};
// db(A).
struct Boss08ArmBlock { std::uint32_t fc = 0; float alpha = 0, alpha_t = 0; };   // 0x70 / 0x1c8 / 0x1cc

// The region-camera record S(k) fields the hook writes [CE §4.7; names DOC BOSS_CAMERA §1b].
struct Boss08CamRecord {
    Vec3 eye_fixed{};                         // +0x288
    Vec3 target_fixed{};                      // +0xb4
    float eye_pitch = 0;                      // +0x270
    float eye_dist = 0;                       // +0x274
    float fov = 0;                            // +0x418
    Vec3 offset{};                            // +0x40c..0x414 (right, up, forward)
    int eye_mode = 0, target_mode = 0;        // +0x268 / +0x94
};

class Boss08 {
public:
    explicit Boss08(const Boss08Setup& s);    // FUN_00550aa0, the four runs

    // One frame of FUN_00562990 case 12 for B, T, W, A in that order: FUN_0055ddc0(obj) then the mover of
    // FUN_00503190(obj); the override tracks are stepped with T [H CE K4].
    Boss08Output tick(const Boss08Input& in);

    // FUN_004faf20 for a part with the damage FUN_004fa550 returned (hit.hpp enemy_incoming_damage with the
    // part's def, resist 0 [H D U-D2]) [D §4.3]: mesh = kB (i = 0..7) or kW (i = 0). Refused when the part's own
    // pool < 1; part+0x134 += dmg (also 0); dmg > 0 drains the pool (floor 0). [not modelled] dmg' =
    // round(dmg * (1 + 0.1 * g_flag)) for the pool drain, the boost gain and the hit particles.
    Boss08Hit hit_part(int mesh, int i, int dmg);

    // FUN_0055f800(obj, &slot) [CE §4.7]: rec = the host's region-camera record of the CURRENT slot k.
    bool camera_hook(int k, Boss08CamRecord& rec, const Vec3& player_pos);

    Boss08Mesh& mesh(int i) { return mesh_[std::size_t(i)]; }
    const Boss08Mesh& mesh(int i) const { return mesh_[std::size_t(i)]; }
    Boss08BodyBlock& body() { return bb_; }
    const Boss08BodyBlock& body() const { return bb_; }
    Boss08TailBlock& tail() { return tb_; }
    const Boss08TailBlock& tail() const { return tb_; }
    Boss08CoreBlock& core() { return wb_; }
    const Boss08CoreBlock& core() const { return wb_; }
    Boss08ArmBlock& arms() { return ab_; }
    const Boss08ArmBlock& arms() const { return ab_; }
    Boss08Part& part(int i) { return parts_[std::size_t(i)]; }          // 0..7 the body's, 8 the core's
    const Boss08Part& part(int i) const { return parts_[std::size_t(i)]; }
    Boss08MiniGauge& mini(int i) { return (gauges_ ? gauges_ : mini_.data())[i]; }
    const Boss08MiniGauge& mini(int i) const { return (gauges_ ? gauges_ : mini_.data())[i]; }
    // The engine's three records are globals the HUD tick counts down (MINI_GAUGE.md §2): the host hands
    // its store in; unbound (the unit tests) the object's own array stands for them.
    void bind_gauges(WorldGauge* g) { gauges_ = g; }
    boss08::Track& track(int i) { return trk_[std::size_t(i)]; }
    const boss08::Track& track(int i) const { return trk_[std::size_t(i)]; }
    Boss08Statics& statics() { return st_; }
    const Boss08Statics& statics() const { return st_; }
    // The body's material records as the shell leaves them [A §A.1.1 block 3, §A.1.3]: alpha (+0x20) and the
    // "damaged limb" bit 0x20000 of the flag word, per id-table index. chest_flags_zero: in a form != Low the
    // whole flag word of materials 0 / 1 / 2 is zeroed. [H] the alphas start at 1.0.
    const std::array<float, boss08::kBodyMats>& mat_alpha() const { return mat_alpha_; }
    const std::array<bool, boss08::kBodyMats>& mat_damaged() const { return mat_damaged_; }
    bool chest_flags_zero() const { return chest_zero_; }
    ef2sim::M4 matrix() const;                // B+0x20: YawPitchRoll(yaw, 0, 0) + the position row [A §A.2]

private:
    struct Scope;
    // B
    void motion();                            // FUN_00552120
    void pushout();                           // FUN_00552510
    void bank();                              // FUN_00552ef0
    void bank_hands();                        // B1 §2..§4
    void bank_spear();                        // B23 §3, §4
    void bank_onback();                       // B23 §5
    void bank_arms();                         // B23 §6
    void bank_smoke();                        // B23 §7
    void bits(int n);                         // FUN_00552730
    void homing();                            // FUN_00552c30
    void chain();                             // FUN_00559a50
    void death();                             // FUN_0055d3d0
    void form_change(int f);                  // FUN_0055a150
    void shell_body();                        // A §A.1.1 blocks 1..7
    void fall();                              // A §A.1.2
    void mat_flags();                         // A §A.1.3
    void dispatcher();                        // FUN_0055d170
    void pick_normal();                       // FUN_0055bf50
    void pick_low();                          // FUN_0055c490
    void pick_high();                         // FUN_0055c700
    void pick_onback();                       // FUN_0055c960
    // T
    void tail_chain();                        // FUN_00559dd0
    void tail_fx();                           // FUN_00558290
    void tail_decide();                       // FUN_0055cb70
    // W
    void core_shell();                        // A §A.1.5 kind 2 (+ FUN_0055a0d0, FUN_00559780)
    // A
    void arms_shell();
    // FUN_0055aa80
    void damage_body();
    void damage_core();
    void exposure_end();                      // D §3
    void barrier_break(Boss08Stat stat);      // D §4.2.1's effect block (shared with the death)

    void set_clip(int mesh, int section);
    void queue_motion(int mesh, const char* name, bool flag = true);
    void move_to(int mesh, const Vec3& p, float v1, float v2);
    void touch(int mesh, bool on);
    void se(int id) { out_->sounds.push_back({id, false}); }
    void se2(int id) { out_->sounds.push_back({id, true}); }
    void kill(int& h);
    Boss08Effect& fx(int id, Boss08Call call = Boss08Call::kFx);
    Boss08Effect* inline_fx(int id, Boss08Stat stat);
    Boss08Effect& on_body(Boss08Effect& e, const Vec3& off = {});
    Boss08Effect& on_node(Boss08Effect& e, int node);
    void bullet(bool raw, int kind, int effect, const Vec3& pos, const Vec3& dir, const float* f, bool f201 = true);
    void shake(int n, float ax, float ay) { out_->shake = Boss08Shake{n, ax, ay}; }
    void zoom(float s, float r) { out_->zoom = std::make_pair(s, r); }
    void fade(std::uint32_t col, float step, float target);
    void flash() { fade(0xaaaaaa, 0.033333335f, 0.0f); }                // B23 §0 "Flash()"
    void cam(int idx, float rate) { bb_.cam_idx = idx; out_->cam_slot = std::make_pair(idx, rate); }
    void mat_mode(int m) { out_->mat_mode.push_back(m); }
    void push(const Vec3& v, int ticks) { out_->player.ext = Boss08Push{v, ticks}; }
    float pv() const;                         // the difficulty push value
    int diff_bits(int a, int b, int c) const { return diff_ == 1 ? b : diff_ == 2 ? c : a; }
    Vec3 node(int k) const { return in_->b_node[std::size_t(k)]; }
    bool feq(float n) const { return f_[boss08::kB] == n; }
    std::uint32_t W();
    std::uint32_t R(std::uint32_t n) { return (W() >> 17) % n; }
    float U();
    float Rf(float lo, float hi);
    bool burst() const { return in_->player_section == 0x13d; }

    std::array<Boss08Mesh, 4> mesh_{};
    Boss08BodyBlock bb_{};
    Boss08TailBlock tb_{};
    Boss08CoreBlock wb_{};
    Boss08ArmBlock ab_{};
    std::array<Boss08Part, boss08::kBodyParts + 1> parts_{};
    std::array<Boss08MiniGauge, 3> mini_{};
    WorldGauge* gauges_ = nullptr;
    std::array<boss08::Track, boss08::kTracks> trk_{};
    std::array<float, boss08::kBodyMats> mat_alpha_{};
    std::array<bool, boss08::kBodyMats> mat_damaged_{};
    bool chest_zero_ = false;
    Boss08Statics st_{};
    int char_ = 3, diff_ = 0;
    bool easy_ = false, rush_ = false;
    int next_handle_ = 1;
    // this call
    const Boss08Input* in_ = nullptr;
    Boss08Output* out_ = nullptr;
    int sec_[5] = {};                         // An+0x18 of B / T / W / A / Y, live (a SetClip changes it at once)
    std::uint32_t cnt_[5] = {};               // An+0x1c, zeroed by a SetClip and by a motion request
    float f_[5] = {};                         // An+0x20 [H: a SetClip leaves it at the section's first frame]
    Vec3 ppos_{};                             // P+0x50, live (the fall and the death move the player)
};

}  // namespace cleria
