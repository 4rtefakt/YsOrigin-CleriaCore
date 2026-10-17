// Pictimos ("boss05", room S_5099) — the boss object's logic, pure and headless.
//
// Ported from build/boss_re/PICTIMOS.md (Part 1 = ctor / map §P1.n, Part 2 = slices A/B §A.n/§B.n, Part 3 = slices
// C/D §C.n), four byte-level passes over the ctor FUN_0052d9e0, the 27 KB brain FUN_0052e2c0, the part sync
// FUN_0052d480, the push-out FUN_0052d6d0 and SetState FUN_0052d980. The summons are CAST_M220 Chryolos
// (cast 0x1a) running their boss-fight brain FUN_004ab840 (build/ai_spec/M220B.md, brains/m220.cpp m220_boss);
// this module writes their fields the way the brain does (sub +0x728, mode +0x79c, target +0x888, DEF, HP,
// alpha, position, ch1), through Boss05Minion records the host copies in and out.
//
// Pictimos walks, turns and jumps (one GoMesh `boss05` with a yaw matrix), 13 GoXx parts on its nodes (only 0
// and 1 can be hurt), a booster mode (FX 379 mines from its legs), swings that throw shock waves (bullets 380 /
// 381), homing meteors (UTIAGE, 376), a jump with fissures (382) or a pounce (378 + three kind-8 shots), and
// below half HP the summon (ZOROMEKA: one Chryolos every 6 frames) and the "guts": at HP 0 with live summons it
// survives at 1 HP and absorbs them for +30..60 HP each.
//
// The scene light (LightDir 0x76a564.., LightCol 0x76a570..) is the module's while the fight runs: an ease toward the
// fight's direction and the room's colour every frame, the intro's red, the booster's violet pulse, the pink hold after
// UTIAGE, the reset at b4 500 [EXPORT 0052e2c0.c:185-208, 505-559, 876-902, 993-1021].
// Tags: [BYTES]/[DATA]/[H] as the spec; [not modelled] engine behaviour left out: the death's light (the UTIAGE flash
// and the fade, 0052e2c0.c:1614, 2155-2171), the camera preset (blk+0x1ac.., copied to region slot 3 by FUN_0042bb10), the particle emitter
// FUN_00587010 (intro / death debris and its draws), the mesh tint and the glare texture, the parts' contact
// hits (GoXx type 4, U2), the death's armour-break visuals timing beyond the effects, Hugo's scripted rounds'
// cutscene side.
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

namespace boss05 {

inline constexpr int kParts = 13;
inline constexpr int kSlots = 28;    // the node table blk+0x00 (names 0x68b9c8)
inline constexpr int kSections = 18;

enum Section : int {
    kStand = 0, kWalk, kLWalk, kRWalk, kOoburi, kYokonagi, kTatenagi, kTatakituke, kUtiage, kLaser, kZoromeka,
    kJump, kJetattack, kDead, kPikuri, kDokonjyo, kRakkatyuu, kTyakuti,
};
struct SectionInfo { const char* name; int start, end; bool loop; };
extern const SectionInfo kSectionTable[kSections];   // BOSS05.YMO [DATA], all rate 0.25
extern const char* const kSlotNames[kSlots];
// FUN_0052d480's table 0x68b960: (node slot, radius) per part.
struct PartSlot { int slot; float radius; };
extern const PartSlot kPartSlots[kParts];

}  // namespace boss05

struct Boss05Setup {
    float eex_hp = 4000, eex_str = 220, eex_def = 230;   // EEX record 4 [DATA]
    int level = 40;
    float exp_b0 = 0;
    int character = 1;
    int diff_index = 0;             // g_flags[144]
    bool diff_easy = false, diff_very_easy = false, boss_rush = false;
    int retries = 0;                // g_flags[196]
    Vec3 pos{0, 0, 16};             // Mesh_Locate; the ctor overwrites z = 17
    Vec3 light_dir{0, -1, 0}, light_col{1, 1, 1};   // 0x76a564.. / 0x76a570.. at the ctor: the backups blk+0x188 / 0x1a0
};

// One cast-0x1a actor of the pool (pool order). The module reads and writes these fields; the host copies them
// back onto the actor (`target`: 0 the player, -2 Pictimos, else an actor instance id = +0x888).
struct Boss05Minion {
    int id = 0, pool = 0;
    float hp = 0, max_hp = 0, def = 0, alpha = 1;
    Vec3 pos{}, ch1{};
    std::uint32_t dead_t = 0;       // +0x3fc
    int sub = 0, mode = 0, target = 0;   // +0x728 / +0x79c / +0x888
    std::optional<float> vt;        // +0x330 written
    std::optional<float> turn;      // +0x814 written
    std::uint32_t or130 = 0;        // +0x130 |= (absorbed: 8 | 0x2000)
};

struct Boss05Input {
    bool fight = false;             // g_flags[156] != 0
    bool flag224 = false;           // g_flags[224] != 0
    bool flag291 = false;           // g_flags[291] != 0 (Hugo's scripted round 1)
    bool fix_held = false;          // 0x76bc04: a script holds Fix (the intro's 700-frame hold)
    Vec3 player_pos{};              // P+0x50 (the origin)
    float player_hp = 1;
    int section = boss05::kStand;   // A+0x18
    std::uint32_t count = 0;        // A+0x1c
    std::array<Vec3, boss05::kSlots> slots{};   // the node table's world positions
    std::vector<Vec3> nodes;        // every model node (the gather's raw indices 26..47)
    std::vector<int> pool_ids;      // the actor pool, in order (instance ids; the player included)
    std::function<std::uint32_t()> rng;
    // FUN_00475710(cast 0x1a, deg, pos, level): the new actor's instance id (0 when the pool is full) and its DEF.
    std::function<int(const Vec3& pos, int deg, int level, float* def)> spawn;
    Vec3 light_dir{0, -1, 0}, light_col{1, 1, 1};   // the scene light now (0x76a564.. / 0x76a570..)
};

struct Boss05Effect {
    int id = 0;
    Vec3 pos{}, rot{}, scale{1, 1, 1};
    bool boss_stat = true;
    float power = 0, knock = 1;
    int rtype = 0;
    int slot = -1;                  // attached to node slot (FUN_00451c20 mode 1, offset `pos`)
    int handle = 0;                 // 1..4 booster aura blk+0x100.., 5/6 heal blk+0x110/0x114
};
struct Boss05Bullet { int kind = 1, effect = 0; Vec3 pos{}, dir{}; float f[13] = {}; };
// A spawned summon's set-up (§C.2): ch1 (the throw), then the host writes EXP 0, +0x888 = Pictimos and resists.
struct Boss05Spawned { int id = 0; Vec3 ch1{}; };

struct Boss05Output {
    std::vector<int> sections;      // SetClip (restart), in order
    std::optional<float> anim_speed;   // A+0x24
    std::vector<int> sounds;        // 3-D at the boss
    std::vector<Boss05Effect> effects;
    std::vector<int> kill_handles;  // GoEffects by handle (+0x1d4 = 1)
    bool kill_376 = false;          // FUN_00451730(376): every UTIAGE meteor
    std::vector<Boss05Bullet> bullets;
    std::vector<Boss05Spawned> spawned;
    std::vector<int> rumble;
    std::optional<std::pair<int, Vec3>> shake;
    std::optional<std::pair<float, float>> zoom;
    std::vector<std::pair<int, int>> flags;
    Vec3 player_move{};             // P+0x50 +=
    Vec3 player_kb{};               // P+0x338 +=
    std::optional<std::uint32_t> player_iframes;   // P+0x734 = max(.., n)
    bool exp = false;               // FUN_004fa4a0 (the EXP award)
    bool gauge_hide = false;
    std::optional<std::pair<float, float>> gauge;  // (HP, max) when the HP changed: FUN_0043a460, lenScale 7000
    bool destroy = false;
    std::optional<float> alpha;     // obj+0x41c target (death b4 350: fade over 4)
    std::optional<Vec3> light_dir, light_col;   // the scene light written this frame
};

struct Boss05Part { int slot = 0; float radius = 0, hp = 0, max_hp = 0, def = 0, resist[4] = {}; float dmg = 0; Vec3 centre{}; };

class Boss05 {
public:
    explicit Boss05(const Boss05Setup& s);
    Boss05Output tick(const Boss05Input& in, std::vector<Boss05Minion>& minions);

    void add_part_damage(int i, float dmg) { add_part_damage(i, dmg, dmg); }
    void add_part_damage(int i, float dmg, float dmg_hp);   // FUN_004faf20: +0x134 += dmg, the part's pool HP -= dmg_hp
    bool part_hittable(int i) const;
    const Boss05Part& part(int i) const { return parts_[std::size_t(i)]; }

    float hp() const { return hp_; }
    float max_hp() const { return max_hp_; }
    float str() const { return str_; }
    int level() const { return level_; }
    Vec3 pos() const { return pos_; }
    void set_pos(const Vec3& p) { pos_ = p; }    // a script moved obj+0x50 (Mesh_Fall / Mesh_SetPosition in the intro)
    float yaw() const { return yaw_; }
    Mat4 matrix() const;                          // RotY(yaw) + pos (FUN_00503990)
    int state() const { return d0_; }
    int frames() const { return b4_; }
    int booster() const { return d8_; }
    int meter() const { return c8_; }
    int intro() const { return bc_; }
    int guts() const { return f8_; }
    int waves() const { return f4_; }
    bool destroyed() const { return destroyed_; }
    float flash() const { return flash_; }

private:
    void set_state(int k);
    void pushout();                               // FUN_0052d6d0
    void part_sync();                             // FUN_0052d480
    void intro_hold();
    void feet();
    void selection();
    void case_walk(); void case_ooburi(); void case_yokonagi(); void case_tatenagi(); void case_tatakituke();
    void case_utiage(); void case_zoromeka(); void case_jump(); void case_dead();
    void tail();
    void death();
    void windup();                                // YOKONAGI / TATENAGI's shared wind-up
    std::uint32_t draw();
    std::uint32_t R(std::uint32_t n) { return (draw() >> 17) % n; }
    float U1();                                   // U - 1 in [0, 1)
    float RF(float s) { return s * U1(); }
    float RR(float a, float b) { return a + (b - a) * U1(); }
    Boss05Effect& fx(int id, const Vec3& p);
    void bullet(int kind, int eff, const Vec3& p, const Vec3& d, std::initializer_list<std::pair<int, float>> f);
    void shake(int f, const Vec3& a) { out_->shake = std::make_pair(f, a); }
    void speed(float s) { out_->anim_speed = s; }
    float theta() const { return -yaw_ - 1.5707964f; }

    // setup / obj
    int char_ = 1, bb5c_ = 0;
    bool easy_ = false, very_easy_ = false, rush_ = false;
    Vec3 pos_{};
    float max_hp_ = 0, hp_ = 0, str_ = 0, def_ = 0, exp_b0_ = 0;
    int level_ = 40;
    float flash_ = 0, bar_last_ = -1;
    bool destroyed_ = false;
    std::array<Boss05Part, boss05::kParts> parts_{};
    // blk
    int ac_ = 0;                                  // 0xac round 2
    int b4_ = 0, b8_ = 0, bc_ = 0, c0_ = 0, c8_ = 0, cc_ = 0, d0_ = 0, d4_ = 0, d8_ = 0, dc_ = 650;
    Vec3 light_dir0_{0, -1, 0}, light_col0_{1, 1, 1};   // blk+0x188 / blk+0x1a0 (the room's light at the ctor)
    void light_col(const Vec3& c) { out_->light_col = c; }
    std::array<int, 4> feet_{100, 100, 100, 100}; // 0xe4..0xf0
    int f4_ = 0, f8_ = 0, fc_ = 0;
    std::array<bool, 4> aura_{};                  // 0x100..0x10c
    std::array<bool, 2> heal_{};                  // 0x110 / 0x114
    Vec3 fwd_{};                                  // 0x170
    Vec3 tgt_{};                                  // 0x17c
    float minion_def_ = 0;                        // 0x1e8
    float yaw_ = 0;                               // 0x1f4
    float skew_ = 0;                              // 0x1fc
    float vy_ = 0;                                // 0x204
    float near_ = 13.5f, utiage_deg_ = 0.13f;     // 0x208 / 0x20c
    int windup_ = 0, variant_ = 0, rounds_ = 2, minion_lv_ = 35, heal_amt_ = 40, cadence_ = 0;   // 0x210..0x224
    Vec3 aura_scale_{1, 1, 1};                    // 0x228
    // this tick
    const Boss05Input* in_ = nullptr;
    Boss05Output* out_ = nullptr;
    std::vector<Boss05Minion>* mins_ = nullptr;
    int sec_ = 0, s_ = 0;                         // the live section / the switch's (F+0xac)
    std::uint32_t count_ = 0;
    Vec3 P_{};
    float dist2_ = 0, dist_ = 0, tgt_yaw_ = 0, delta_ = 0, dx2_ = 0, dz2_ = 0;
    bool done_ = false;
    int selection_clip0_ = 0;     // clip0 (the section at frame start)
    int retries_ = 0;
};

}  // namespace cleria
