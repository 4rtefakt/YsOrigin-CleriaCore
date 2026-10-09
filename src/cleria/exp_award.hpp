// The EXP a kill credits, as pure functions: the level-difference scaler FUN_004fa2f0, the
// kill award in FUN_004fbb40 (0x4fcb45..0x4fcc13), the boss award FUN_004fa4a0, and the hit
// combo meter 0x76c1a8 / 0x76c1b0 whose count is the kill award's chain bonus.
// build/progress_re/REWARDS.md §1 (structure), build/audit/combat.md #28/#29/#36 (the x0.6666
// and the floor), re-read from the bytes 2026-10-07 — every constant below is [BYTES] unless
// tagged otherwise.
//
//   kill  (FUN_004fbb40, victim = an actor the player's hit killed):
//       amt  = max(1, floor(level_scale(+0xb0) * rate 0x76a5fc * diffMult))   (0 if +0x12c & 0x4000000)
//       EXP += amt * (1 + 0.01 * combo)
//   boss  (FUN_004fa4a0(obj), skipped on g_flags[193] / a zero EXP):
//       EXP += max(1, floor(rate * level_scale(obj+0x1c, level obj+0x34) * diffMult))   (no chain)
#pragma once

namespace cleria::expaward {

constexpr float kScale = 0.6666f;                        // 0x6bd358 (f32 0.66659999): every non-zero award
constexpr float kOverBase[3] = {1.5f, 1.65f, 1.8f};      // 0x68b26c[DAT_0076bb5c]: over-levelled divisor base
constexpr float kUnderBase = 1.5f;                       // 0x74e6ac = GAMEINFO payload +0x1c (file +0x2c) [DATA]
constexpr int kUnderCap = 3;                             // CMOVG at 0x4fa34e: at most 3 levels of bonus
constexpr int kComboCap = 99;                            // 0x4fc866 (cmp ecx,0x63; jae)
constexpr int kComboFirst = 720;                         // 0x4fc844: +0x2d0 on the first hit
constexpr int kComboAdd = 60;                            // 0x4fc84b: +0x3c on each later hit
constexpr int kComboTimerCap = 720;                      // 0x4fc857: the timer never exceeds 0x2d0

// FUN_004fa2f0(ECX = player level, EDX = enemy level, XMM0 = base): d = enemy - player;
// d < 0: base / F[difficulty]^-d; d > 0: base * G^min(d, 3); then x 0.6666 unless the result is
// exactly 0 (0x4fa36a: MSVC's `!= 0.0f` idiom). `difficulty` is DAT_0076bb5c (0 Normal, 1 Hard,
// 2 Nightmare); `under_base` is G (GAMEINFO payload +0x1c).
float level_scale(float base, int player_level, int enemy_level, int difficulty, float under_base = kUnderBase);

// 1.0, 1.1 with DAT_0076bb58 (Easy, g_flags[143]), 2.0 with DAT_0076bc48 (Very Easy, g_flags[203]);
// the latter wins (0x4fcb72..0x4fcb94 / 0x4fa4d0..0x4fa4f5).
float diff_mult(bool easy, bool very_easy);

// The chain bonus 1 + 0.01 * combo (0x4fcbcb..0x4fcbff): the count as an unsigned double, to
// float, x 0.01f, + 1.0f.
float chain_bonus(int combo);

// The kill award (0x4fcb65..0x4fcc0b). `rate` is DAT_0076a5fc (1.0 in every real save) — the
// shell's EXP rate and the Lua mods' factor multiply into it; `awarded` is the victim's award-once
// bit +0x12c & 0x4000000 (the award is then 0).
struct KillIn {
    float base = 0;           // victim +0xb0
    int player_level = 1;     // DAT_0076a760
    int enemy_level = 1;      // victim +0xc8
    int difficulty = 0;       // DAT_0076bb5c
    float rate = 1.0f;
    float diff_mult = 1.0f;
    int combo = 0;            // DAT_0076c1a8 (already counting this killing hit)
    bool awarded = false;
    float under_base = kUnderBase;
};
float kill_award(const KillIn& in);

// FUN_004fa4a0 (the bosses' award): 0 when `base` is 0 (0x4fa4b5) — the caller also skips it on
// g_flags[193] (time attack, 0x4fa4a0). No chain bonus.
float boss_award(float base, int player_level, int boss_level, int difficulty, float rate, float diff_mult,
                 float under_base = kUnderBase);

// The hit combo meter: `count` 0x76c1a8 (0..99), `timer` 0x76c1b0 (ticks).
// The SP a kill or a boss credits: FUN_004fa390(ECX = amount) [BYTES 0x4fa390..0x4fa435], whose result
// FUN_004fa290 adds to the wallet 0x76a75c (clamped 0..999999). A kill passes the victim's +0xc4
// (FUN_004fbb40 0x4fcd1d..0x4fcd3d, skipped when it is 0); `rate` is the SP rate 0x76a5f8.
//   mode 0 (the story, byte 0x74e25e): s = 1.0; 1.5 on the easy side (g_flags[143]); 2.0 on Very Easy
//       (g_flags[203]); x 0.01 when the index g_flags[144] == 2 (Nightmare); x 1.25 with blessing bit 0x100
//   mode 2, not the easy side: s = 1.0 + (2 * index + 2)
//   mode 1: s = 1.0
struct SpIn {
    int mode = 0;             // byte 0x74e25e
    int index = 0;            // g_flags[144]
    bool easy = false, very_easy = false;   // g_flags[143] / [203]
    bool bless_sp = false;    // 0x76bc80 & 0x100
    float rate = 1.0f;        // 0x76a5f8
};
int sp_award(int amount, const SpIn& in);

struct Combo {
    int count = 0;
    int timer = 0;
    // A player-side hit that dealt damage > 0 to a monster (FUN_004fbb40 0x4fc835..0x4fc86e; the
    // attacker resolves to the player for its own swing, its loci and every non-actor object).
    void hit();
    // One player-mover tick with no fix held (FUN_00465170 0x466b5d..0x466b79): the timer counts
    // down and the count drops to 0 on the tick it reaches 0.
    void tick();
};

}  // namespace cleria::expaward
