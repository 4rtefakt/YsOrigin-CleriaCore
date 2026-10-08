// Khonsclard ("boss04", room S_4099) — the boss object's logic, pure and headless.
//
// Ported from build/boss_re/KHONSCLARD.md (Part A = §A.n, Part B = §B.n), two byte-level passes:
//   ctor FUN_00527cc0, dispatcher FUN_0052d1c0, parts + damage FUN_00528570, the pit FUN_00528ac0,
//   hole pick FUN_00528fb0, bands / down / get-up FUN_005291c0, the pods FUN_005295a0, the START
//   emergence FUN_0052a2b0, attacks 0..4 FUN_0052a820 / FUN_0052aab0 / FUN_0052af30 / FUN_0052b1e0 /
//   FUN_0052b6e0, attack 5 0x52b990, attack 6 FUN_0052c2c0, the selector FUN_0052c680, death FUN_0052c8f0.
//
// Khonsclard is ONE GoMesh (`boss04`, (0, 0.3, 43)) sunk in an ant-lion pit. It never moves or turns.
// Its body HP is hit only through part 0 (node `core`, r 0.9), which is DEF 999 except while the boss
// is DOWN; it goes down when every enabled `boss04s` pod (parts 1..6, separate GoMeshes rising from six
// holes) is broken. The pods heal the boss when left alone; every 20 % of HP adds one (2 -> 6). Part 7
// (the sunk body, a cylinder) staggers it when broken. Between downs it fires world-fixed bullet fans
// from its eight muzzles u1..u8, a near-player WAVE (attack 5) and alternating prickle shots (6), and
// the pit pulls the player in harder as its HP falls. The world is reached only through Boss04Input and
// the Boss04Output vocabulary (as boss01..03); apps/cleria-boss04test drives it headless.
//
// Tags: [BYTES]/[EXPORT]/[DATA] as in the spec; [H] inference; [not modelled] engine behaviour left out.
// RNG: every draw goes through Boss04Input::rng (the shared xorshift128). The draws: the selector's
// R(100) per idle decision, R(6) per hole pick, one smoke-scale draw per smoking pod, attack 3's NM
// spread, attack 5's 36, the START puffs (1 per node per START frame, +4/+5 on a 1/128 hit) and the
// death's 4 per node per frame (+4/+5 per debris). The camera shake's 3 per frame is the host's
// (boss03::camera_shake).
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

namespace boss04 {

inline constexpr int kParts = 8;     // obj+0x278
inline constexpr int kPods = 6;      // blk+0..0x14: the boss04s children
inline constexpr int kNodes = 66;    // the ctor's node table blk+0x18 (names 0x68b7e8)
inline constexpr int kClips = 15;    // blk+0x120 (names 0x68b62c = the YMO order)

enum Clip : int {
    kStand = 0, kFall, kDown, kUp, kWave, kArm1, kArm2, kArm3, kArm4, kRFall, kRest, kAttack, kDeath,
    kStartWait, kStart,
};
struct ClipInfo { const char* name; int start, end; bool loop; };
extern const ClipInfo kClipTable[kClips];     // BOSS04.YMO [DATA], all rate 0.25
// The node table: 0 root, 1..16 prickle1_a, prickle1_b .. prickle8_b, 17..24 u1..u8, 25..35 ta1..11,
// 36..46 tb1..11, 47..57 tc1..11, 58..62 fang1..5, 63 eye_l, 64 eye_r, 65 core.
std::string node_name(int k);
inline constexpr int kNodeCore = 65;
inline constexpr int kNodeU1 = 17;
// The six holes (.data 0x732690) the pods rise from [BYTES §A.7].
extern const Vec3 kHoles[kPods];
// The attack table rows {f0, period, last index, rest} (0x68b728 / Nightmare 0x68b668) [§A.5.1].
struct AttackRow { int f0, period, last, rest; };
extern const AttackRow kAttackTable[2][12];

}  // namespace boss04

struct Boss04Setup {
    // EEX record 3 [DATA EEX.DAT]: (3000, 176, 175, rec3 50, 140, 140, lv 32), raw; the module applies the
    // reader FUN_00443b80's difficulty scaling (§A.2.1 item 5).
    float eex_hp = 3000, eex_str = 176, eex_def = 175;
    int level = 32;
    float exp_b0 = 0;               // +0xb0 (the caller's GAMEINFO curve at the level) [H as boss01..03]
    int diff_index = 0;             // g_flags[144] (bb5c)
    bool diff_easy = false;         // g_flags[143] (bb58)
    bool diff_very_easy = false;    // g_flags[203] (bc48)
    bool boss_rush = false;         // g_flags[193] (bc20)
    int retries = 0;                // g_flags[196] (bc2c)
    Vec3 pos{0, 0.3f, 43.0f};       // Mesh_Locate('boss04', 0, 0.3, 43)
    int model_nodes = 103;          // M+0x1c: the debris / START loops [H, §A uncertain A9]
};

struct Boss04Input {
    Vec3 player_pos{};              // the player's ORIGIN (+0x50)
    float player_hp = 1;            // 0x76a730
    bool player_grounded = true;    // +0x388
    float player_floor = 0;         // +0x318
    bool fight = false;             // g_flags[156] != 0
    int clip = boss04::kStartWait;  // *(obj+0x26c)+0x18
    bool clip_done = false;         // +0x1c == 1
    float clip_frame = 0;           // the absolute YMO frame
    std::array<Vec3, boss04::kNodes> nodes{};     // the named nodes' world positions
    std::vector<Vec3> model_nodes;                // every model node (the START puffs)
    std::array<Vec3, boss04::kPods> pods{};       // the boss04s meshes' positions (+0x50)
    std::array<bool, boss04::kPods> pod_damage_done{};   // its DAMAGE clip is current and ended
    std::function<std::uint32_t()> rng;
};

// ---- the request vocabulary ----
struct Boss04Sound { int id = 0; };                       // a 2-D cue (FUN_005ce2a0 / FUN_0043a040)
struct Boss04Effect {
    int id = 0;
    Vec3 pos{}, scale{1, 1, 1};
    bool boss_stat = true;          // the boss's obj+0x94 block arms the EF2 emitters
    float power = 0, knock = 1;
    int rtype = 0;
    int tag = 0;                    // 1 = the heal aura blk+0x350 (killed by Boss04Output::kill_aura)
};
// FUN_00593550(ecx = obj, kind, effect, &pos, &dir, f0..f12): BULLETS.md §1.1 (bullet::block_spawn).
struct Boss04Bullet { int kind = 0, effect = 0; Vec3 pos{}, dir{}; float f[13] = {}; };
struct Boss04PodMove { int pod = 0; std::optional<Vec3> teleport; std::optional<float> move_y; };   // FUN_00528fb0 / Mesh_MoveTo(y, 0.02)
struct Boss04PodClip { int pod = 0; const char* clip = "STAND"; };
struct Boss04Gauge { float hp = 0, max = 0; float len_scale = 6500.0f; int emblem = 4; };
struct Boss04MiniGauge { int timer = 0; float hp = 0, max = 0; Vec3 pos{}; };   // 0x76c1cc / 0x76c1e4
struct Boss04Exp { int level = 32; float b0 = 0; float mult = 1.0f; };

struct Boss04Output {
    std::vector<int> clips;                       // SetClip, in order; the last is current
    std::vector<Boss04Sound> sounds;
    std::vector<Boss04Effect> effects;
    bool kill_aura = false;
    std::vector<Boss04Bullet> bullets;
    std::vector<Boss04PodMove> pod_moves;
    std::vector<Boss04PodClip> pod_clips;
    std::vector<int> rumble;
    std::optional<std::pair<int, Vec3>> shake;    // 0x76a5ac frames, 0x76a594.. amp
    std::optional<std::pair<float, float>> zoom;  // 0x76a550 / 0x76a554
    std::vector<std::pair<int, int>> flags;       // 223, 194, 156
    std::optional<Vec3> player_push;              // +0x5d0..0x5d8 with +0x5dc = 4 (the pit)
    float pit_scroll = 0;                         // `40jigoku005` UV scroll speed s (+0x54)
    bool player_iframes = false;                  // +0x734 = 9999 (boss rush kill)
    std::optional<Boss04Exp> exp;                 // FUN_004fa4a0 on the kill frame
    bool clear_buffs = false, achievement = false;
    bool destroy = false;
    std::optional<Boss04Gauge> gauge;             // only when the HP changed
    bool gauge_hide = false;
    std::optional<Boss04MiniGauge> pod_gauge, body_gauge;   // Normal / Easy only
};

struct Boss04Part {
    float hp = 0, max_hp = 0, str = 0, def = 0;
    float resist[4] = {};
    float dmg = 0;                  // +0x134 (the hit path's damage, consumed by the driver)
    Vec3 centre{};                  // +0x50 = +0x6c
    float hurt_radius = 0.7f;       // +0x68
    float half_h = 0;               // +0x78
    int shape = 0;                  // +0x7c: 0 sphere, 1 vertical cylinder
    bool broken = false;            // +0x138
};

struct Boss04Pod { bool enabled = false; int state = 4, t = 0, slot = 0xff, h = 0; };

class Boss04 {
public:
    explicit Boss04(const Boss04Setup& s);
    const Boss04Output& ctor_requests() const { return ctor_; }
    Boss04Output tick(const Boss04Input& in);     // one FUN_0052d1c0

    // FUN_004faf20 on part i: +0x134 += dmg; parts 1..7 lose HP here (once); the core's goes to the body.
    void add_part_damage(int i, float dmg);
    bool part_hittable(int i) const;              // HP >= 1 (the hit path's gate); DEF 999 = no damage

    const Boss04Part& part(int i) const { return parts_[std::size_t(i)]; }
    const Boss04Pod& pod(int n) const { return pods_[std::size_t(n)]; }
    float hp() const { return hp_; }
    float max_hp() const { return max_hp_; }
    float str() const { return str_; }
    int level() const { return level_; }
    Vec3 pos() const { return pos_; }
    int attack() const { return atk_; }           // blk+0x160 (11 idle)
    int prev_attack() const { return prev_; }
    int shots() const { return shots_; }
    int down() const { return down_; }            // blk+0x238
    int stagger() const { return stagger_; }      // blk+0x23c
    int band() const { return band_; }            // blk+0x240
    float suction() const { return s_; }          // blk+0x258
    float alpha() const { return alpha_; }        // obj+0x418
    float flash() const { return flash_; }        // obj+0x2a0
    int death_frames() const { return dk_; }      // blk+0x348
    bool destroyed() const { return destroyed_; }

private:
    void emergence();                 // FUN_0052a2b0
    void parts();                     // FUN_00528570
    void pit();                       // FUN_00528ac0
    void dispatch_clips();            // 0x52d200
    void pods_tick();                 // FUN_005295a0
    void bands();                     // FUN_005291c0
    void pick_hole(int n);            // FUN_00528fb0
    void selector();                  // FUN_0052c680
    void fan(int i, float step, Vec3* pos, Vec3* dir);   // §B.1.1
    void a0(); void a1(); void a2(); void a3(); void a4(); void a5(); void a6();
    void death();                     // FUN_0052c8f0
    void reset_after_down();          // FUN_005291c0 RESET
    void abort_attack();

    std::uint32_t draw();
    std::uint32_t R(std::uint32_t n) { return (draw() >> 17) % n; }
    float U();                        // [0, 1)
    void set_clip(int c);
    void se(int id) { out_->sounds.push_back({id}); }
    Boss04Effect& fx(int id, const Vec3& p);
    void bullet(int kind, int eff, const Vec3& p, const Vec3& d, std::initializer_list<std::pair<int, float>> f);
    void flag(int n, int v) { out_->flags.push_back({n, v}); }
    const boss04::AttackRow& row(int a) const { return table_[std::size_t(a)]; }

    // setup
    int bb5c_ = 0;
    bool easy_ = false, very_easy_ = false, rush_ = false;
    int model_nodes_ = 103;
    std::array<boss04::AttackRow, 12> table_{};
    // obj
    Vec3 pos_{};
    float max_hp_ = 0, hp_ = 0, str_ = 0, def_ = 0, exp_b0_ = 0;
    int level_ = 32;
    float flash_ = 0, bar_last_ = -1.0f, alpha_ = 1.0f;
    bool destroyed_ = false;
    std::array<Boss04Part, boss04::kParts> parts_{};
    std::array<Boss04Pod, boss04::kPods> pods_{};
    // blk
    int rot_ = 0;                     // 0x15c
    int atk_ = 0, prev_ = 0;          // 0x160 / 0x164
    int shots_ = 0, rest_ = 0;        // 0x168 / 0x16c
    int sub_ = 0, side_ = 0;          // 0x230 / 0x234
    int down_ = 0, stagger_ = 0;      // 0x238 / 0x23c
    int band_ = 0, pod_limit_ = 1000; // 0x240 / 0x244
    float def_save_ = 0;              // 0x248 = 0x24c
    int suction_mode_ = 1;            // 0x250
    float s_ = 0;                     // 0x258
    std::uint32_t frames_ = 0;        // 0x25c
    bool killed_ = false;             // byte 0x2d8
    std::array<bool, 100> debris_{};  // bytes 0x2d9..
    int dk_ = 0;                      // 0x348
    bool buffs_cleared_ = false;      // byte 0x34c
    bool aura_ = false;               // 0x350 != 0
    int aura_cd_ = 0;                 // 0x7d9b98 (a static global)
    Boss04Output ctor_;
    // this tick
    const Boss04Input* in_ = nullptr;
    Boss04Output* out_ = nullptr;
    int clip_ = 0;
    bool done_ = false;
    float frame_ = 0;
};

}  // namespace cleria
