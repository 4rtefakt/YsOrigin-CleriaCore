// The player's status ailments: poison, heavy, confuse, curse (build/mech_re/STATUS_AILMENTS.md, issue #60).
// Pure rules; the view owns the effects, the sounds, the popups and the readers (view_ailments.inl).
//
// The engine's state is one mask 0x76c15c and four countdown timers 0x76c160..0x76c16c (g_flags 528..532,
// past CleriaCore's 0x200-entry flag array, so it lives in `State`). All of it is player-only.
//   apply   FUN_004630a0(ecx = bit)            [BYTES 0x4630a0..0x463f1d]
//   tick    FUN_00462550, once a player tick   [BYTES 0x462550..0x462747; EXPORT 00462550.c:150-163]
//   hit     FUN_004fd350's status loop         [BYTES 0x4fdc58..0x4fde0c]   (path A)
//           FUN_004fe1a0's two loops           [BYTES 0x4fe666..0x4fe7d7]   (path B, hit flag +0x11c & 8)
// Nothing here was checked in the running game (STATUS_AILMENTS.md U11).
#pragma once

#include <cmath>
#include <cstdint>

namespace cleria::ailments {

enum : std::uint32_t { kPoison = 0x01, kHeavy = 0x08, kConfuse = 0x10, kCurse = 0x20 };

inline constexpr std::uint32_t kBits[4] = {kPoison, kHeavy, kConfuse, kCurse};
inline constexpr int kTicks[4] = {900, 300, 600, 1200};        // 0x46312a.., 0x463476.., 0x4637c4.., 0x463b14..
inline constexpr int kTicks75[4] = {675, 225, 450, 900};       // blessing bit 2 (0x4)
inline constexpr int kTicks50[4] = {450, 150, 300, 600};       // blessing bit 20 (0x100000), tested first
inline constexpr int kEffect[4] = {58, 56, 59, 57};            // EF2 ids (fx+0x1d8 0x3a / 0x38 / 0x3b / 0x39)
inline constexpr int kSound[4] = {117, 118, 119, 120};         // p_poison / p_heavy / p_confusion / p_curse
inline constexpr const char* kName[4] = {"Poison", "Heavy", "Confuse", "Curse"};          // the popup (English)
inline constexpr const char* kHudName[4] = {"poison", "heavy", "confuse", "curse"};       // the HUD line
inline constexpr std::uint32_t kColour[4] = {0x44ff00, 0x0077e2, 0xffff44, 0xff44ff};
inline constexpr const char* kCurseLine = "STR-5 DEF-5";       // the curse's second popup
inline constexpr float kHudX = 62.0f;                          // 0x6bdaa0
inline constexpr float kHudY[4] = {666.0f, 678.0f, 690.0f, 702.0f};
inline constexpr int kPoisonPeriod = 120;                      // 0x462656
inline constexpr float kPoisonFrac = 0.0066666701f;            // 0x6bd05c: max HP / 150
inline constexpr float kHeavyJump = 0.6666666f;                // 0x6bd35c: the lift-off v0 under Heavy
inline constexpr float kCurseStat = 5.0f;                      // 0x6bd868: ATK - 5 dealing (floor 0), DEF - 5 taking

struct State {
    std::uint32_t mask = 0;     // 0x76c15c
    int t[4] = {0, 0, 0, 0};    // 0x76c160 poison, 0x76c164 heavy, 0x76c168 confuse, 0x76c16c curse
    bool has(std::uint32_t bit) const { return (mask & bit) != 0; }
};

inline int index_of(std::uint32_t bit) {
    for (int i = 0; i < 4; ++i) if (kBits[i] == bit) return i;
    return -1;
}

// The duration a fresh application writes (a re-hit refreshes it): bit 20 of the blessings halves it,
// else bit 2 takes a quarter off [BYTES 0x463142].
inline int duration(int i, std::uint32_t blessings) {
    return (blessings & 0x100000u) ? kTicks50[i] : (blessings & 0x4u) ? kTicks75[i] : kTicks[i];
}

// The percent chance of path A outside a boss battle: {50, 75, 100}[g_flags[144]], minus 20 with blessing
// bit 22, else minus 10 with bit 9 [BYTES 0x4fdcf3..0x4fddd1]. Easy / Very Easy have index 0.
inline int chance(int diff_index, std::uint32_t blessings) {
    const int red = (blessings & 0x400000u) ? 20 : (blessings & 0x200u) ? 10 : 0;
    return (diff_index == 2 ? 100 : diff_index == 1 ? 75 : 50) - red;
}

struct Applied {
    int index = -1;         // 0..3, or -1 when nothing was applied (or the bit has no handler)
    bool announce = false;  // the bit was not active: the effect + SE (when no effect stands) and the popup(s)
};

// FUN_004630a0 for ONE bit (both appliers pass one). `heal_zone` is g_flags[191]: inside one nothing lands.
// [not modelled: dead in retail] the equipment resist tests 0x44 / 0x60 / 0x50 / 0x48 of 0x76a624 (no item
// sets those bits, STATUS_AILMENTS.md §3). A bit without a handler (0x02 / 0x04 / 0x40 / 0x80) is OR-ed in
// and never cleared [BYTES 0x463f1d].
inline Applied apply(State& s, std::uint32_t bit, std::uint32_t blessings, bool heal_zone) {
    Applied r;
    if (heal_zone) return r;                         // 0x4630bb
    const int i = index_of(bit);
    if (i < 0) { s.mask |= bit; return r; }
    s.t[i] = duration(i, blessings);                 // always: a re-hit refreshes the timer
    r.index = i;
    r.announce = !s.has(bit);
    s.mask |= bit;
    return r;
}

// Path A, after the hit's damage left the victim's HP: nothing on a kill or a hit of 0; in a boss battle
// (g_flags[156]) every bit lands with no roll unless g_flags[221] is set, then none does [BYTES
// 0x4fdcd8..0x4fdcee]; else one R(100) draw per set bit against chance(). `roll100()` is one game-RNG draw
// (w >> 17) % 100; `on_bit(bit)` applies.
template <class Roll100, class OnBit>
inline void on_hit(std::uint32_t status, int dmg, float hp_after, bool boss_battle, bool flag221, int diff_index,
                   std::uint32_t blessings, Roll100&& roll100, OnBit&& on_bit) {
    if (hp_after < 1.0f || dmg <= 0) return;         // 0x4fdc77..0x4fdc9e
    status &= 0xffu;
    for (std::uint32_t bit = 1; status != 0; bit <<= 1) {
        if (!(status & bit)) continue;
        if (boss_battle) { if (!flag221) on_bit(bit); }
        else if (roll100() < chance(diff_index, blessings)) on_bit(bit);
        status ^= bit;
    }
}

// Path B (the record's flag word +0x11c & 8, boss battles): two passes, the second only while the player
// lives; a bit lands on Hard / Nightmare, else on R(100) < 50. No blessing or flag-221 term.
template <class Roll100, class OnBit>
inline void on_hit_flag8(std::uint32_t status, float hp_after, int diff_index, Roll100&& roll100, OnBit&& on_bit) {
    status &= 0xffu;
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1 && hp_after < 1.0f) break;     // 0x4fe741
        std::uint32_t rest = status;
        for (std::uint32_t bit = 1; rest != 0; bit <<= 1) {
            if (!(rest & bit)) continue;
            if (diff_index > 0 || roll100() < 50) on_bit(bit);
            rest ^= bit;
        }
    }
}

struct Ticked {
    std::uint32_t cleared = 0;   // bits that ended this tick (their effects die)
    int poison = 0;              // HP the poison takes this tick (0 = none): SE 117, a 0xff00ff number
};

// FUN_00462550, once a player tick; the caller skips it while a Fix is held (0x75ad90 + 0x75ad94 != 0).
// `curse_guard`: the equipment word 0x76a624 & 0x20048, live only through the Blue Necklace's 0x20000
// (item 0x37): the curse timer is zeroed every tick [BYTES 0x46261b..0x462633]. Poison: the timer counts
// down and on every multiple of 120 (0 included) takes max(1, floor(max HP x 0.00666667)), never the last
// HP [BYTES 0x46263d..0x462709].
inline Ticked tick(State& s, float max_hp, float hp, bool curse_guard) {
    Ticked r;
    if (curse_guard) s.t[3] = 0;
    if (s.mask & kPoison) {
        if (s.t[0] == 0) { s.mask ^= kPoison; r.cleared |= kPoison; }
        else {
            --s.t[0];
            if (s.t[0] % kPoisonPeriod == 0) {
                int d = int(std::floor(max_hp * kPoisonFrac));
                if (d < 1) d = 1;
                if (hp - float(d) >= 1.0f) r.poison = d;
            }
        }
    }
    for (int i = 1; i < 4; ++i) {
        if (!(s.mask & kBits[i])) continue;
        if (s.t[i] == 0) { s.mask ^= kBits[i]; r.cleared |= kBits[i]; }
        else --s.t[i];
    }
    return r;
}

// FUN_00461120(ecx): 0 = the poison timer, 1 = heavy / confuse / curse, anything else all four. Every
// "clear" path zeroes TIMERS; the bit and the effect go on the next tick().
inline void clear_timers(State& s, int which = 2) {
    if (which != 1) s.t[0] = 0;
    if (which != 0) s.t[1] = s.t[2] = s.t[3] = 0;
}

}  // namespace cleria::ailments
