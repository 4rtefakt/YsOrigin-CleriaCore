// The player's MP gauge, the skill button's hold/charge/release machine and the Boost gauge's
// fill/drain rules, as pure state (build/ability_re/ABILITIES_VERIFIED.md §1-§2). The host feeds
// the button masks, the equipped bracelet and the player's section/kind; it performs the cast
// (SetSection) and the cosmetic effects the results ask for.
//
// Tables read from yso_win.exe v1.1.1.0 [BYTES]:
//   cost   PTR_0068b0bc[char][slot]   Adol {1000,600,300} Yunica {700,700,400} Hugo {2000,600,900} Toal {300,700,600}
//   +NM    PTR_0068b08c[char][slot]   {0,0,0} {300,300,300} {0,300,300} {300,300,300}
//   normal PTR_0068afe0 {0x10d,0x10c,0x10e}, charged PTR_0068afd0 {0x110,0x10f,0x111} (all chars)
//   charge thresholds 0x68b030..0x68b04c: held {1,T}, auto {400,T}; T 3000 / 2700 (bc80&0x400) /
//          2400 (&0x800) / 2000 (&0x4000)
//   regen  0x68b078 {2,4,6,8} by bracelet level; Boost B 0x68b070 = 25 (NM 0x68b068 = 30);
//          drain 0x68b060 = 30 (NM 0x68b058 = 90, 0x68b17c = 400 with DAT_0076bda4)
//   attack kind 0x69bc14[section] (sections >= 0x100)
#pragma once
#include <cstdint>

namespace cleria::skillmp {

constexpr float kMpMax = 3000.0f;       // DAT_0076a734 (const 0x6bdf48)
constexpr float kBoostFull = 30000.0f;  // const 0x6bdf68
constexpr float kChargeStep = 25.0f;    // 0x6bd9c0
constexpr float kChargeClear = -9999.0f;  // 0x6be158

// FUN_0046d560: the equipped bracelet 0x74/0x75/0x76 -> slot 0/1/2, else 3 (none).
int bracelet_slot(int magic);
// The ASO section of a cast (FUN_0046ea00): charged = c138 == 2.
int skill_section(int slot, bool charged);
// The damage class the cast writes to player+0x109 (0x74 -> 0, 0x75 -> 2, 0x76 -> 1; else 3).
int damage_class(int magic);
// player+0x404, the attack kind, from 0x69bc14[section] (0 below 0x100 and past the table).
int attack_kind(int section);

struct CostIn {
    int chr = 1;               // DAT_0076bb74: 0 Adol, 1 Yunica, 2 Hugo, 3 Toal
    int slot = 0;              // bracelet_slot
    int difficulty = 0;        // DAT_0076bb5c: 2 = Nightmare adds PTR_0068b08c
    std::uint32_t bc80 = 0;    // DAT_0076bc80 blessings ("Reduce MP usage" 0x40 / 0x40000 / 0x80000)
    bool acc_mp = false;       // accessory bit 0x76a624 & 0x200000 (x0.80)
    bool boosting = false;     // Yunica pays half while boosting
    bool adol_alt = false;     // char 0 with DAT_0074c0cc: PTR_0068b080 {1500,1800,1200}
};
// The cast cost as a float (the hold test) and its ftol'd int (the spend at release).
float cost_f(const CostIn& in);
int cost(const CostIn& in);

// MP + its lag value DAT_0076c194 (the HUD's trailing segment). Mover 0x467aff..0x467bfa.
struct Mp {
    float mp = kMpMax, max = kMpMax, lag = kMpMax;
    // One logic tick. `level` = g_flags[182 + slot] (0..3; 0 with no bracelet), `buff` =
    // DAT_0076b900 while c198 & 0x10 (the MP-drop buff), else 1.
    void tick(int level, float buff = 1.0f);
};

// The skill charge DAT_0076c134 / its level DAT_0076c138. FUN_0046cf20(amount, held).
struct Charge {
    float amount = 0;
    int level = 0;             // 0 none, 1 cast on release, 2 charged cast on release
    // Returns true on the tick the level becomes 2 (the host spawns the charge-ready EF2
    // 0x80 / 0x8d / 0xb9 by character, else 0x69bcfc[slot] {7,6,5}, and the white flash).
    // `bracelet_level` = g_flags[182 + slot]: at level 0 the charge stops at c138 = 1.
    bool add(float a, bool held, int bracelet_level, std::uint32_t bc80);
};

// What one tick of the skill button decided (mover 0x466052.. / 0x466a64..).
struct SkillIn {
    bool skill_held = false;   // +0x510 & 4
    bool attack_held = false;  // +0x510 & 1
    bool auto_charge = false;  // byte 0x6d606f [not modelled: a config option]: charges without the button
    int magic = -1;            // DAT_0076a630
    int bracelet_level = 0;    // g_flags[182 + slot]
    int section = 0;           // player+0x994
    int kind = 0;              // player+0x404 (attack_kind of the section)
    bool busy = false;         // g_flags[161] != 0 || g_flags[151] == -1
    bool fix = false;          // 0x75ad90 + 0x75ad94
    std::uint32_t bc80 = 0;
    CostIn cost;               // chr / difficulty / multipliers (slot is filled from magic)
};
struct SkillOut {
    bool cast = false;         // call FUN_0046ea00(1): SetSection(section), MP -= cost
    int section = 0;
    int cost = 0;
    int damage_class = 3;
    int level = 0;             // LocalInt 12 = c138 at the cast
    bool charged_ready = false;  // the level just reached 2 (effects)
};
// One tick. On a cast the caller must apply `iframes = max(iframes, 3)`, set the section and
// spend: Mp::mp -= cost (FUN_0046ea00 refusals — the super sections / the re-cast lock — are
// the caller's, before it spends).
SkillOut skill_tick(const SkillIn& in, const Mp& mp, Charge& ch);

// ---- Boost (§1) ---------------------------------------------------------------------------
// The passive fill added every logic tick: ((1 - hp/max) * B + 1) * M.
// M = 2.0 / 1.66 / 1.33 / 1.0 for bc80 & 0x10000 / 0x8000 / 0x20 / none.
// Unclamped, as in retail (0x467c43..0x467d89: +0x98 / +0x94 straight into the formula, and
// FUN_0046cc10 adds it with no lower bound): HP above the max gives a negative fill.
// B = the word tables 0x68b068 (30, difficulty 2) / 0x68b070 (25), the same for every character.
float boost_passive(float hp, float hp_max, int difficulty, std::uint32_t bc80);
// The drain per tick while boosting (FUN_00462c40): drain * (0.6 with accessory 0x3b).
float boost_drain(int difficulty, bool bda4, bool acc_3b);
// FUN_0046cc10's gate on a gauge add (the caller adds and clamps; READY when it reaches full).
bool boost_can_add(bool enabled187, bool busy161, bool fix, bool boosting, float gauge);
// +300 per landed hit only for attack kinds outside {0, 5}.
inline bool boost_hit_counts(int kind) { return kind != 0 && kind != 5; }

// The MP super-armour of a hit taken inside a skill section (FUN_004fd350 [BYTES 0x4fd8c3..0x4fd928,
// 0x4fda82..0x4fdab3, 0x4fd732..0x4fd75a]; BOOST_SKILLS.md §3.3): Yunica in 0x10c / 0x10f (500 MP), Toal in
// 0x10e / 0x111 while the Marker is below 1 (200 MP). With the MP: it is spent and the hit is a GUARD
// (damage 0, 16 i-frames). Without: 16 i-frames and the damage x 0.75, truncated. [not modelled] Adol.
struct Armor {
    bool active = false;   // the rule applies to this hit
    bool guard = false;    // MP >= cost: spend it, damage 0
    float spend = 0;
    float scale = 0.75f;   // no guard: the damage's factor (0x6bd390; Felghana Adol 0.25 / 0.5 / 0.75 by skill)
};
// `adol_version` = DAT_0074c0cc (0 Felghana, 1 Napishtim): Felghana Adol's skill sections [BYTES 0x4fd6e5..0x4fd8be;
// build/mode_re/ADOL.md §2.4]: Terra 0x10c / 0x10f the 200-MP guard, else x 0.25; Ignis 0x10e / 0x111 x 0.5; Ventus
// 0x10d / 0x110 x 0.75; 16 i-frames in each. Napishtim has none.
Armor armor(int chr, int section, int marker, float mp, int adol_version = 0);
inline constexpr float kArmorScale = 0.75f;   // 0x6bd390
inline constexpr int kArmorIframes = 16;

// Toal's pull in 0x10c / 0x10f (the tail of FUN_0046a3e0 [EXPORT 0046a3e0.c:388-476]; BOOST_SKILLS.md
// §3.5): the weight of a monster at 3-D distance d. Nothing at d >= 10; else 0.1 .. 1.0 by the thresholds
// k x {10, 9, .. 1} (k = 1.5 while boosting), the last one passed winning. Each tick the monster's knock
// channel 2 loses normalize(e - P') x 15 x 0.0113 x w, P' = the point 2.0 ahead of Toal.
float pull_weight(float d, bool boosting);
inline constexpr float kPullAhead = 2.0f;                 // the point in front of him
inline constexpr float kPullStep = 15.0f * 0.0113f;       // 0x6bd964 x 0.6bd090
// Toal's slow fall in 0x10e / 0x111 cast in the air (FUN_00465170 [EXPORT 00465170.c:2491-2502]): while the
// vertical timer is negative, t = t x a + b.
inline float slow_fall(float t, bool charged) {
    return charged ? t * 0.925f + 0.009f                   // 0x6bd3e8, 0x6bd074
                   : t * 0.94f + 0.006f;                   // 0x6bd3ec, 0x6bd04c
}

}  // namespace cleria::skillmp
