// Zava's two giant heads ("boss06a" Yogrcs / ice, "boss06b" Omrgan / fire, room S_6082) — pure and headless.
//
// Ported from build/boss_re/ZAVA.md (pass 1: ctor FUN_0053bbb0, the block map), ZAVA_D.md (the head shell: brain
// FUN_00544980, patterns FUN_005434f0 / 5439d0 / 543e00, the waypoint mover FUN_00544030, the life tick
// FUN_00544750, the move dispatcher FUN_00542cd0 and its inline moves, both callbacks), ZAVA_EF.md (moves 1 / 5 / 3,
// the FX-slot driver FUN_0053d700, the orbiters FUN_0053eab0 / FUN_0053ecd0) and ZAVA_E2.md (moves 8, 11, 12, 13,
// 14, 15, 17, the fog fader). Section references below are "D §n", "EF §n", "E2 §n", "P1 §n".
//
// This is the heads only. Zava herself (actor cast 0xad, FUN_0053af10 / FUN_0053ab20) is not here: her brain
// drives the heads by writing into their blocks, exactly as the engine does, through the public interface:
//   head(i).queued (hb+0x71) / set_pattern (FUN_005433e0), start_move (FUN_0053c390), abort_move (FUN_0053c400),
//   head(i).free / shield / phase2 / rearm / lives / busy (hb+0x7f / 0x80 / 0x81 / 0x7a / 0x83 / 0x114),
//   spawn_orbiters (FUN_0053eab0), and the Boss06Zava record (what the heads read of her and write into her).
//
// Head 0 = a (Yogrcs), head 1 = b (Omrgan); a ticks before b (D §0 [H: creation order]) and each reads the
// other's block live.
//
// Tags: [BYTES]/[DATA]/[H] as the spec. [not modelled] = engine behaviour left out (listed where it happens):
// the dead HP < 1 branch and brain modes != 2, the never-written patterns 4 / 5 and moves 4 / 16, the touch
// callback's dead damage call (D5), FUN_00540600's effect-pool scan (requested from the host), FUN_0058a5f0's
// particles and every RNG word drawn inside the effect / bullet / damage / camera-shake code.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "cleria/ef2_sim.hpp"   // ef2sim::M4 and the D3DX matrix helpers
#include "cleria/mathx.hpp"

namespace cleria {

namespace boss06 {

inline constexpr int kHeads = 2;
inline constexpr int kClips = 16;       // the ctor's clip table hb+0x24..0x60
inline constexpr int kSections = 15;    // BOSS06A / BOSS06B.YMO clips
inline constexpr int kNodes = 4;        // the node table hb+0x14..0x20
inline constexpr int kFxSlots = 4;      // hb+0x130 + 0x60k
inline constexpr int kOrbiters = 4;     // hb+0x2b0 + 0x40k / hb+0x3b0 + 4k
inline constexpr int kEmblem = 6;       // the ctor writes the HP-bar emblem word 0x76c214 = 6 [BYTES 0x53c15b]

// Clip slots (P1 §3.2 item 5): the index SetClip takes.
enum Clip : int {
    kWait = 0, kAttack1, kAttack0, kDamage, kShake, kPiyori1, kPiyori2, kPiyori3, kLaser0, kLaser1, kLaser2,
    kLaser3, kShake2, kBite, kCharge0, kCharge1,
};
// Node table entries.
enum Node : int { kPiyoEfc = 0, kFireEfc, kBodyEfc, kEyeCEfc };
// Clip slot -> the model's section (the YMO clip order of P1 §3.1).
extern const int kClipSection[kClips];
struct SectionInfo { const char* name; int a_start, a_end, b_start, b_end; bool loop; };
extern const SectionInfo kSectionTable[kSections];   // [DATA], rate 0.25: ticks = (end - start) * 4
int section_ticks(int head, int section);

// The waypoint tables (.data 0x7326d8, 5 x 0xb8) [BYTES D §6.3]: raw (x, z); y is 0 everywhere.
struct WaypointTable { int count; float xz[15][2]; };
extern const WaypointTable kWaypoints[5];
// FUN_00535050 + the callers' origin: table[t][i] * 0.8 + (-34, 6, 29); (0,0,0)*0.8 + origin when out of range.
Vec3 waypoint_world(int table, int index);

enum Floor : int { kFloorNone = 0, kFloorOther, kFloorA, kFloorB };   // P+0x540: none / another object / a head

}  // namespace boss06

struct Boss06Setup {
    int diff_index = 0;                       // g_flags[144]: 0 Normal, 1 Hard, 2 Nightmare
    bool diff_easy = false;                   // g_flags[143] == 1
    bool diff_very_easy = false;              // g_flags[203] == 1
    int character = 1;                        // g_flags[150]: 0 Adol, 1 Yunica, 2 Hugo, 3 Toal (the burst test)
    Vec3 pos_a{-39.46f, 6.1f, 30.0f};         // Mesh_Locate [DATA S_6082.XSO]; the ctor forces y = 6.1
    Vec3 pos_b{-28.82f, 6.1f, 30.0f};
};

// The Zava actor (cast 0xad) as the heads see it. Owned by the host / Zava's brain; the heads read and write it
// during their tick (so b sees a's same-frame writes).
struct Boss06Zava {
    bool present = false;                     // FUN_005355e0: a cast-0xad actor is in the pool
    Vec3 pos{};                               // Z+0x50 (move 9 reads; move 8 writes the throw position)
    float hp = 1, max_hp = 1;                 // Z+0x98 / Z+0x94 (the orbiters' speed)
    std::uint32_t hits = 0;                   // Z+0x7a8: damage ticks taken while riding; the life tick and move
                                              // 12 zero it
};

struct Boss06HeadIn {
    int section = 0;                          // A+0x18
    std::uint32_t count = 0;                  // A+0x1c: completions of the current clip
    // World matrices of the node table entries piyo_efc, fire_efc, body_efc, eye_c_efc. Head b has no eye_c_efc
    // (hb+0x20 = 0): give it model node 0's matrix, which is what slot 0 addresses [EF §F.2 case 270].
    std::array<ef2sim::M4, boss06::kNodes> nodes{};
};

struct Boss06Input {
    bool player = true;                       // P != null
    Vec3 player_pos{};                        // P+0x50
    float player_hp = 1;                      // P+0x98
    float player_hp_mirror = 1;               // [0x76a730] (the life tick's test)
    int player_section = 0;                   // P+0x994
    bool player_guard = false;                // byte P+0x399
    bool player_grounded = false;             // byte P+0x388
    int player_floor = boss06::kFloorNone;    // P+0x540 and its name
    float player_r324 = 0;                    // P+0x324 (feet = y - this)
    Vec3 player_vel{};                        // P+0xa3c..0xa44 (per-frame velocity; the pillar lead)
    bool pad_pressed = false;                 // input snapshot rec+0x24 != 0
    int pad_x = 0, pad_y = 0;                 // rec+0x2c / rec+0x30
    Vec3 cam_eye{}, cam_target{};             // camera record +0 / +0xc [EF K6 H]
    bool zoom_active = false;                 // [0x76a550] != 0.0
    std::optional<Vec3> fog_colour;           // the current fog record's colour (rec+0x3c), if a record is current
    std::array<Boss06HeadIn, boss06::kHeads> head{};
    std::function<std::uint32_t()> rng;       // one xorshift128 word per call
    // FUN_00536010: the terrain ray (collision sets 2 then 0, the head's own mesh excluded). true = blocked.
    std::function<bool(int head, const Vec3& from, const Vec3& delta)> probe;
};

enum class Boss06Call { kFx, kFx2, kInline };          // FUN_00450410 / FUN_00450880 (keeps the scale) / brain-inlined
// Attach parent: a node matrix, slot_matrix, orbit_matrix; for Zava's own effects (head = -1, boss06_zava.hpp) her
// actor matrix Z+0x20 (kZava) or the static copy 0x7ddcd0 of it with y forced to 6.05 (kZavaFloor, ZAVA_AB §A.1).
enum class Boss06Parent { kNone, kNode, kSlot, kOrbit, kZava, kZavaFloor };

struct Boss06Clip { int head = 0, slot = 0, section = 0, blend = 1; };   // SetClip: restart, A+0x24 = 1.0
struct Boss06Sound { int id = 0; Vec3 pos{}; };                           // FUN_0043a080
struct Boss06Effect {
    int head = 0;                             // -1: Zava's (stat = &Z+0x94, her STR)
    Boss06Call call = Boss06Call::kFx2;
    int id = 0;
    Vec3 pos{}, rot{}, scale{1, 1, 1};
    bool stat = false;                        // stat block = &H+0x94 (ATK = the head's STR 232); false = none (ATK 0)
    float power = 0, knock = 1;
    int rtype = 0;                            // src 0, side 1, status 0, cls -1 everywhere
    int handle = 0;                           // != 0: the module keeps this effect's handle (soft kills name it)
    Boss06Parent parent = Boss06Parent::kNone;   // FUN_00451c20 (or the orbiters' inline attach)
    int parent_index = 0;
    Vec3 at_off{}, at_rot{}, at_scale{1, 1, 1};
    int at_mode = 0;                          // 1 full follow, 2 translation only
    bool flag201 = false, flag203 = false;    // fx+0x201 / fx+0x203
    std::uint32_t or_1dc = 0;                 // kInline: fx+0x1dc |= (0x100; the hit splash 0x106)
};
// FUN_00593550(ecx H, edx kind, effect, &pos, &dir, f0..f12, out 0). f5..f7 are raw ints (bit-cast).
// Kind 0x21 is not in src/cleria/bullet.cpp yet [EF §F.2].
struct Boss06Bullet { int head = 0, kind = 0, effect = 0; Vec3 pos{}, dir{}; float f[13] = {}; bool flag201 = false; };
// FUN_004fd350(ecx H, edx P, &zero16) with H+0xa4 (ATK) overridden for the call: no i-frame gate, no re-hit bank;
// byte P+0x9d8 = 1 around it (no reaction). The applier's own RNG word(s) are the host's.
struct Boss06Damage { int head = 0; float atk = 0; bool or12c_40 = false; };
// The grab, every hold frame [EF §E1.1 step 5]: P+0x12c |= 0x101041, P+0x60 &= ~0x80, velocity and +0x5b0.. = 0,
// P+0x318 = 6.0, not grounded, P.pos = pos, P+0x314 = facing, Actable off, P+0x9fc = 0; `section4`: force section 4.
struct Boss06Hold { int head = 0; Vec3 pos{}; float facing = 0; bool section4 = false; };
// The release [step 6]: P+0x12c &= 0xffefefbe, P+0x60 |= 0x80, Actable on, P.pos = pos; if `vel`: P+0x338.. = vel
// and ASO SetSection(9).
struct Boss06Release { int head = 0; Vec3 pos{}; std::optional<Vec3> vel; };
// Move 8 [E2 §2]: Zava is thrown off. The actor side (section 0x130 unless +0x7bc already is, +0x7a0 &= ~3,
// +0x814 = 0, +0x708 = 1, +0x710 = 0, +0x834/838/83c = 0, +0x12c &= 0xffffefbe, Durable off, word +0x398 = 0,
// victim sphere r 0.6, DEF 252.0, +0x4dc = +0x4e0 = 1.0) is Zava's brain's to apply; pos / vel are computed here.
struct Boss06ZavaThrow { int head = 0; Vec3 pos{}, vel{}; };
// FUN_0058a5f0(1, 0xff550a00, 2, 0x18, 1.3, 1.8, &P+0x50, 1.25, 2, 0) [EF §E1.1 step 8; K7]. Cosmetic.
struct Boss06Burst { int head = 0; Vec3 pos{}; };

struct Boss06Output {
    std::vector<Boss06Clip> clips;            // in order
    std::vector<Boss06Sound> sounds;
    std::vector<Boss06Effect> effects;
    std::vector<Boss06Bullet> bullets;
    std::vector<int> kill_ids;                // FUN_00451730: EVERY live effect of that id (kill flag + particles dying)
    std::vector<int> kill_handles;            // one effect: +0x1d4 = 1 (soft)
    std::vector<int> rumble;                  // FUN_005d3500
    std::optional<std::pair<int, Vec3>> shake;      // 0x76a5ac = frames; 0x76a594.. = amplitude (last write wins)
    std::optional<std::pair<float, float>> zoom;    // ZoomBlur: 0x76a550 / 0x76a554, 0x7b2488 = 1
    std::vector<std::pair<int, int>> flags;         // g_flags[n] = v (134: player held)
    std::optional<Boss06Hold> hold;
    std::optional<Boss06Release> release;
    bool player_free = false;                 // P+0x12c &= 0xffefefbe, P+0x60 |= 0x80, Actable on (a bite's exit)
    std::optional<Vec3> player_vel;           // P+0x338..0x340 = v (an assignment, not +=)
    std::vector<Boss06Damage> damage;
    bool surface_ice = false;                 // byte P+0x4b9 = 0x0b (move 11, head a)
    bool pillar_break_test = false;           // run FUN_00540600 this frame [E2 §3.4; not modelled here]
    std::array<bool, boss06::kHeads> materials_black{};   // the 5 cached materials' rgb = 0 (move 15)
    std::optional<Boss06ZavaThrow> zava_throw;
    bool zava_reseat = false;                 // FUN_0053aa90(Zava): if riding, Z.pos = body_efc(mount) - (0, 0.5, 0)
    std::vector<Boss06Burst> bursts;
};

struct Boss06FxSlot {                         // hb+0x130 + 0x60k
    int handle = 0;                           // +0x00
    ef2sim::M4 m{};                           // +0x10
    std::uint16_t id = 0, count = 0, timer = 0;   // +0x50 / +0x52 / +0x54
};
struct Boss06FogFade {                        // hb+0x3c0 [E2 §3.5]
    Vec3 saved{}, from{}, to{};               // +0x00 / +0x0c / +0x18
    std::uint8_t t = 0, dur = 0;              // +0x24 / +0x25
};

// One head: the object fields the brain touches and the block hb = *(H+0x49c).
struct Boss06Head {
    // --- object H ---
    Vec3 pos{};                               // H+0x50
    Vec3 rot{};                               // (pitch, yaw, roll) of the last SetRot FUN_00503990 -> H+0x20 rows
    float max_hp = 5000, hp = 5000, str = 232, def = 320;   // H+0x94 / 0x98 / 0xa4 / 0xa8
    float alpha_target = 1, alpha_step = 1;   // H+0x41c / H+0x420
    float flash = 0;                          // H+0x2a0
    bool hits_off = false;                    // H+0x298 & 0x40 (set by the life tick when the player is dead)
    bool hittable = true;                     // collision entries' +0x8a (cleared by pattern 0xff)
    bool collide = true;                      // H+0x60 & 0x80
    bool dead = false;                        // H+0x11 & 2
    // --- block hb ---
    int timer = 0;                            // 0x64 move timer
    std::uint32_t fc = 0;                     // 0x68 frame counter
    std::uint8_t id = 0;                      // 0x70: 0 a, 1 b
    std::uint8_t queued = 0, pattern = 0, pstep = 0;   // 0x71 / 0x72 / 0x73
    std::uint8_t move = 0, step = 0;          // 0x74 / 0x75
    std::uint8_t b76 = 0;                     // 0x76: never written [D4]
    std::uint8_t clip = 0;                    // 0x77
    std::uint16_t hold = 0, rearm = 0, laser_cd = 0;   // 0x78 / 0x7a / 0x7c
    std::uint8_t turn_on = 1;                 // 0x7e
    std::uint8_t free = 0;                    // 0x7f "may translate"
    std::uint8_t shield = 0, phase2 = 0;      // 0x80 / 0x81
    std::uint8_t lives = 0;                   // 0x83
    std::uint8_t wp_stop = 0xff, wp = 0;      // 0x84 / 0x85
    std::uint8_t ride = 0;                    // 0x86
    std::uint8_t cam_mode = 0;                // 0x87 (BossCam; head a only; the camera's)
    std::uint8_t blocked = 0, unstick = 0;    // 0x88 / 0x89
    std::uint8_t pil_interval = 0, pil_count = 0;   // 0x8a / 0x8b (move 11)
    Vec3 v8c{};                               // 0x8c..0x94: the laser's sweep target angles / move 11's player sample
    float pitch = 0, yaw = 0, roll = 0;       // 0xb0 / 0xb4 / 0xb8
    Vec3 fwd{};                               // 0xbc: the flattened forward
    Vec3 aim{};                               // 0xc8: the 3-D forward
    Vec3 target{};                            // 0xd4: waypoint target
    Vec3 dir{};                               // 0xe0: travel direction
    float radius = 3.0f;                      // 0xec
    float saved_speed = 0;                    // 0xf0
    float speed = 0, speed_target = 0.08f, accel = 0.001f;   // 0xf4 / 0xf8 / 0xfc
    float k_speed = 1, k_time = 1;            // 0x100 / 0x104
    std::uint8_t esc = 0;                     // 0x108
    int esc_x = 0, esc_y = 0;                 // 0x10c / 0x110
    std::uint8_t busy = 0, stun_req = 0, stunned = 0;   // 0x114 / 0x115 / 0x116
    int stars = 0;                            // 0x118: the piyopiyo effect handle
    std::array<Boss06FxSlot, boss06::kFxSlots> slots{};       // 0x130..
    std::array<ef2sim::M4, boss06::kOrbiters> orbit{};        // 0x2b0..
    std::array<int, boss06::kOrbiters> orb{};                 // 0x3b0..: orbiter effect handles
    Boss06FogFade fog{};                      // 0x3c0..
};

class Boss06 {
public:
    explicit Boss06(const Boss06Setup& s);    // FUN_0053bbb0, twice

    // One frame: head a, then head b (FUN_00544980 each).
    Boss06Output tick(const Boss06Input& in, Boss06Zava& z);

    // --- what Zava's brain calls between ticks ---
    Boss06Head& head(int i) { return h_[std::size_t(i)]; }
    const Boss06Head& head(int i) const { return h_[std::size_t(i)]; }
    void set_pattern(int i, int p);           // FUN_005433e0: 0x71 = 0x72 = p, 0x73 = 0
    // FUN_0053c390: 0x74 = m, 0x75 = 0, 0x64 = 0; m == 0 also 0xf8 = 0.08, 0xfc = 0.001, SetClip(0).
    void start_move(int i, int m, Boss06Output& out);
    // FUN_0053c400: step = max(step, 0x80), 0xf8 = 0.08, 0xfc = 0.01, one MoveTick (the move's finishing step).
    void abort_move(int i, const Boss06Input& in, Boss06Zava& z, Boss06Output& out);
    // FUN_0053eab0(ecx head): one FX2 424 / 425 per life (hb+0x83), attached to the orbit matrices.
    void spawn_orbiters(int i, const Boss06Input& in, Boss06Output& out);

    // --- the callbacks ---
    // FUN_0053b930 (obj+0x480): always refuses the hit (returns false). `entry_hittable` = the entry's +0x8a,
    // `now_ms` = timeGetTime() (the splash throttle 0x7ddea4 is shared by both heads).
    bool accept_hit(int i, bool entry_hittable, const Vec3& point, std::uint32_t now_ms, const Boss06Input& in,
                    Boss06Output& out);
    // FUN_0053b780 (obj+0x484): the velocity to SET on the touching actor (P+0x338..), or nothing for entry 1.
    std::optional<Vec3> touch(int i, int entry, const Vec3& actor_pos) const;
    // FUN_005351d0, called for both heads by the boss camera hook FUN_005453d0: advances the fog fader and writes
    // the current fog record's colour through `rec` (null = no current record).
    void fog_tick(int i, Vec3* rec);

    ef2sim::M4 matrix(int i) const;           // FUN_00503990's rows (YawPitchRoll of `rot`) + the position row
    int diff_level() const { return lvl_; }   // FUN_00535280: VE 0, Easy 1, Normal 2, Hard 3, NM 4

private:
    struct Scope;
    void brain(int i);
    void pattern1(int i); void pattern2(int i); void pattern3(int i);
    void mover(int i, int table);             // FUN_00544030
    void life_tick(int i);                    // FUN_00544750
    void move_tick(int i);                    // FUN_00542cd0
    void move_bite(int i); void move_stun(int i); void move_laser(int i);
    void move_fusion(int i); void move_floor(int i); void move_swap(int i); void move_suction(int i);
    void move_bomb(int i); void move_death(int i); void move_dive(int i);
    void orbiters(int i);                     // FUN_0053ecd0
    void fx_slots(int i);                     // FUN_0053d700
    void set_clip(int i, int k, int blend = 1);
    void start(int i, int m);                 // the inline Start
    void start_move_(int i, int m);
    void abort_(int i);
    void finish(int i);                       // FINISH 0x542e3e
    void turn(int i, Vec3 d, float rate, bool pitch_on, bool clamp);   // FUN_00535dc0
    void turn_ang(int i, const Vec3& ang, float rate);                // FUN_00535c20
    void set_fwd(int i, bool zero_x);
    bool near_other(int i, const Vec3& p) const;                      // FUN_0053c2f0
    bool probe(int i, const Vec3& from, const Vec3& delta) const;
    void slot_set(int i, int k, int id);
    void slot_kill(int i, int k, bool soft);  // FUN_00534eb0
    void orb_kill(int i, int k);              // FUN_0053f1b0
    void fog_fade(int i, const Vec3& target); // FUN_00535170
    Vec3 dir_p(int i) const;                  // FUN_005357c0
    Vec3 p_pos() const;                       // FUN_00535780
    float d_p2(int i) const;
    bool burst() const;                       // FUN_005352d0
    bool done(int i) const { return count_[std::size_t(i)] != 0; }
    Vec3 node(int i, int k) const;
    std::uint32_t draw();
    std::uint32_t R(std::uint32_t n) { return (draw() >> 17) % n; }
    Vec3 rand_between(const Vec3& a, const Vec3& b);                  // FUN_00535320
    Boss06Effect& fx2(int i, int id, const Vec3& p);
    void se(int id, const Vec3& p);

    std::array<Boss06Head, boss06::kHeads> h_{};
    int bb5c_ = 0, char_ = 1, lvl_ = 2;
    bool easy_ = false, very_easy_ = false;
    int next_handle_ = 1;
    std::uint32_t last_splash_ms_ = 0;        // 0x7ddea4
    // this call
    const Boss06Input* in_ = nullptr;
    Boss06Output* out_ = nullptr;
    Boss06Zava* z_ = nullptr;
    std::array<std::uint32_t, boss06::kHeads> count_{};   // A+0x1c, zeroed by a SetClip
    Vec3 ppos_{};                             // P+0x50, live (the grab moves the player)
};

}  // namespace cleria
