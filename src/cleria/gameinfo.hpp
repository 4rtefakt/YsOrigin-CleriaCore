// The stat curves, from MISC/GAMEINFO.DAT.
//
// ## Offsets here are PAYLOAD-relative. Read this before comparing to anything.
//
// The file has a 16-byte header; the runtime buffer is the PAYLOAD, at BSS
// 0x0074e690. So payload +0x20 == file +0x30 — the same bytes under two conventions,
// and this codebase has already burned a day on exactly that ambiguity: a note saying
// "+0x20" was "corrected" to "+0x30" and neither was wrong. Payload is the engine's
// own convention, so it is what we use, and every offset below says so.
//
// That the buffer is STATIC (BSS, not heap) is why the curve was findable at all:
// every read is an absolute address, so one range scan over [0x74e690, 0x74e7e0)
// turned up every consumer. Worth remembering as a technique.
//
// ## The curve [C-DIS]
//
// 11 pairs of f32 at payload +0x20, stride 8. **The two floats are the value at
// LEVEL 1 and the value at LEVEL 60**, and every consumer computes:
//
//     stat = lerp(a, b, (level - 1) / 59)
//
// Proven rather than inferred: each one does b-a, *t, +a. The 59.0 lives at
// DAT_006bda90 and had been carried as an unexplained constant for weeks — it is
// simply 60-1, the level cap minus one.
//
// ** The values are f32. ** Read as i32, pair 0 is (1112014848, 0) — 0x42480000 is
// the float 50.0. We recorded this table as int for weeks. The type has to be checked
// in both directions: ENEMYINFO's neighbouring level column is genuinely i32 and
// reads as denormal floats.
//
// ## Who reads what [C-DIS]
//
//   FUN_004202e0 (enemy spawn) : pairs 0, 1, 2, 4 -> maxHP, ATK, DEF, EXP
//   FUN_00420c40 (player)      : pairs 6, 7, 8    -> maxHP, ATK, DEF
//                                pair 10          -> the EXP-to-level curve
//
// Pairs 3 and 9 are (0,0) holes with no reader. Pair 5 is DEAD: byte-identical to
// pair 4 and read by nothing.
//
// ## Pair 10 is EXP, not HP
//
// (150, 18150) is an order of magnitude above every other pair, and that was read
// here for weeks as "the obvious maxHP candidate". It is the player's EXP curve — it
// is large because it is a different UNIT, not a bigger quantity of the same one.
// Player maxHP is pair 6, reaching 400 at level 60.
//
// ## The 4x11 block is the WEAPON ATK TABLE [char][tier] [C-DIS]
//
// At file +0xa0: 4 rows (the player character index, bounds-checked 0..3 against the
// assert "g_flags initialized for non-existent player character") x 11 columns (the
// weapon tier, clamped to 0xa in code). Row stride 0x30, computed in code.
//
//   char0: 23 31 38 45 53 60 67 74 82 89 94   <- ADOL   (arcade mode only)
//   char1: 21 28 35 42 50 57 64 71 79 86 91   <- Yunica
//   char2: 22 29 36 43 51 58 65 72 80 87 92   <- Hugo
//   char3: 20 27 34 41 49 56 63 70 78 85 90   <- Toal
//
// ** Why a three-character story has a four-row table: char 0 is ADOL, who is playable
// in ARCADE MODE. ** His sprite set is C_110 (red hair, sword and shield) and the
// player-AIA table at 0x0069bd40 lists him first.
//
// The row assignment is pinned against the game's published per-level STR, not
// assumed: Yunica 22/28/35/50/64/79/91, Hugo 23/29/36/51/65/80/92, Toal
// 21/27/34/49/63/78/90 each land in exactly ONE row at the identical indices
// [1,2,4,6,8,10], and row 0 matches none of the three — which is what identifies it as
// the fourth character. So g_flags[150] (1=Yunica) maps to these rows by IDENTITY;
// cleria-view's old `character - 1` was a real bug, caught by exactly this check.
//
// ** The 11 columns are Ark-engine weapon levels 1..11, inherited. ** Ys Origin awards
// only 7 upgrades and reads indices 0,1,2,4,6,8,10 — so 3,5,7,9 are dead
// intermediates. The tell is independent and rather good: Ys VI (Ark of Napishtim)
// publishes an eleven-level weapon table whose Livart column, 20/27/34/41/49/56/63/
// 70/78/85, is IDENTICAL to our row3 at indices 0..9 (diverging only at 10: ours 90,
// Ys VI 95). Origin reuses the Ys VI engine, and this table came along with it.
//
// [UNKNOWN] Every character's published Lv1 STR is exactly 1 ABOVE our row[0] (Yunica
// 22 vs 21). Lv2..7 match 18/18 across all three, so it is not a row-assignment error —
// and 22 appears nowhere in row1 at any index, so the published Lv1 cannot come from
// this table. Ys VI's Livart Lv1 = 20 = its index 0 with no +1, so it is not an engine
// rule either. Most likely the published Lv1 came from a different source than its
// Lv2..7. Cheapest resolution: read Lv1 STR off the status screen in-game.
//
// This is the value that closed the negative-damage puzzle. The curve above is the
// BASE ATK, not the final one: FUN_004208f0 adds the weapon on top, into the same
// field, before it is mirrored to actor+0xa4. A level-1 char0 has ATK = 25 + 23 = 48,
// not 25 — and 25 alone makes the damage formula go negative against a level-1 enemy.
//
// It survived two wrong readings worth recording. It was guessed to be 4 DIFFICULTY
// tiers (the game ships four, which is exactly the kind of plausible-number inference
// that keeps burning this project — difficulty is a separate global at 0x76bb5c). And
// it was guessed to be log2-encoded like ENEMYINFO's stat block, because
// K = 2^((v-50)/20) turns these into tidy 0.39..4.59 multipliers. Both were wrong.
// The tell for the second was that nothing anywhere ever log2-decodes this block — a
// decode that produces pretty numbers is not evidence that the engine performs it.
#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace cleria {

// A curve is its endpoints: the stat at level 1 and the stat at level 60.
struct StatCurve {
    float at_level_1 = 0;
    float at_level_60 = 0;

    bool is_hole() const { return at_level_1 == 0 && at_level_60 == 0; }

    // The engine's lerp. Levels outside 1..60 are not clamped here because no
    // consumer clamps: ENEMYINFO's level column is corpus-bounded to 1..60 and the
    // player's is capped at 60 by its own writer.
    float at(int level) const {
        const float t = float(level - 1) / 59.0f;
        return at_level_1 + (at_level_60 - at_level_1) * t;
    }
};

// Which pair feeds which stat. Named for the consumer that reads it, since that is
// the only thing that gives a pair meaning — the file itself is 11 anonymous pairs.
enum : int {
    kEnemyMaxHp = 0,
    kEnemyAtk = 1,
    kEnemyDef = 2,
    // 3 — hole, no reader
    // The enemy's EXP reward -> actor+0xb0. Named by the game's own published EXP
    // column: floor(K(ENEMYINFO+0xDC) * this curve) matches 67/67 enemies. It sat as
    // "feeds actor+0xb0; what that IS remains [UNKNOWN]".
    kEnemyExp = 4,
    // 5 — byte-identical to 4 ((15, 610)). Which one the EXP path actually reads is
    // [UNKNOWN] and undecidable from the data alone, since both give the same answer.
    kPlayerMaxHp = 6,
    kPlayerAtk = 7,
    kPlayerDef = 8,
    // 9 — hole, no reader
    kPlayerExp = 10,
    kCurveCount = 11,
};

// The weapon ATK table, file +0xa0: [character 0..3][tier 0..10].
inline constexpr int kChars = 4, kTiers = 11;

struct GameInfo {
    std::vector<StatCurve> curves;  // 11, payload +0x20
    float weapon_atk[kChars][kTiers] = {};  // file +0xa0, row stride 0x30

    static GameInfo load(const std::filesystem::path& path);  // MISC/GAMEINFO.DAT
    bool ok() const { return curves.size() == kCurveCount; }

    // The player's ATK before per-attack power: the level curve PLUS the weapon.
    // The curve alone is not the actor's ATK -- see the note on the 4x11 block.
    float player_atk(int character, int tier, int level) const {
        if (!ok() || character < 0 || character >= kChars) return 0;
        if (tier < 0) tier = 0;
        if (tier >= kTiers) tier = kTiers - 1;  // the engine clamps to 0xa
        return curves[kPlayerAtk].at(level) + weapon_atk[character][tier];
    }

    // Total EXP required to REACH `level` from level 1: the running sum of pair 10
    // over the levels below it. (Sum over i in [0, level-1) of lerp(150, 18150,
    // i/59) — note the index is i, not i-1, so the first term is the level-1 value.)
    double exp_to_level(int level) const;
};

}  // namespace cleria
