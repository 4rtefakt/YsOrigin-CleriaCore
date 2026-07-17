// Enemy tuning, from MISC/ENEMYINFO.DAT.
//
// 90 records x 264 bytes (0x108); the record index is the `cast_id` reached via
// CASTINFO (see sct.hpp for the chain: <ROOM>.SCT --name--> CASTINFO --cast_id-->
// ENEMYINFO). `16 + 264*90 == 23776` accounts for the file exactly.
//
// This header deliberately exposes only what is verified. The record has 264 bytes
// and most of them are still [UNKNOWN]; the stat pipeline is being disassembled and
// its fields are NOT here yet, because a plausible-looking number is worse than no
// number. See build/misc_re/verify_drops.py for everything below.
//
// ## Drop table [C-COR] — +0x50 i32[8] item ids, +0x70 f32[8] percent chances
//
// This refutes our own ledger's "Not found: EXP, gold, or drop tables". Rolls are
// INDEPENDENT and in order — rates routinely sum past 100, so it is not a weighted
// pick. Five corpus checks, none of which a misaligned parse survives:
//
//   - front-packed (no live slot after a -1): 90/90
//   - id == -1  <=>  rate == 0:                89/90
//   - 443 live slots, 13 distinct ids, all in 66..127
//   - all 443 rates within [0, 100]
//   - expected gold vs the level column: Spearman rho = 0.81
//
// ** Enemies drop only tools, seeds and gold — never equipment. ** That is not an
// inference from the ids looking high; INVINFO's icon stems partition the 128 items
// by id at exactly 66:
//
//   sw 0..5  ar 6..29  sh 30..53  ac 54..65   = EQUIPMENT   (never dropped)
//   tl 66..115  sk 116..119  go 120..127      = DROPPABLE
//
// (`sh` is SHOES 30..47 plus shields 48..53, and `sk` is the elemental BRACELETS, not
// seeds — the real seeds are tl_48/tl_49. See items.hpp. The partition boundary at 66
// is unaffected: what matters here is that nothing below 66 is ever dropped.)
//
// and every one of the 443 live drop ids is >= 66, with none below. The boundary in
// the data and the boundary in the drop table are the same number.
//
// ## Level [C-DIS] — +0xe8, an i32 in 1..60
//
// The reader is FUN_004202e0, the enemy spawn path, which feeds it to the GAMEINFO
// stat curve. Before that was found this was [H-high] on the strength of range (1..60
// is the game's cap) plus a 0.81 rank correlation with expected gold — and that
// correlation, note, was NOT what settled it. It matched a previously reported 0.79
// from the same data shape, which is agreement without independence: two reads of one
// table cannot corroborate each other. The disassembly settled it; the statistic
// never could have.
//
// It hid because it reads as DENORMAL floats (1.4013e-45 is the i32 1). This file's
// numeric types cannot be assumed in either direction: the same table's 0xc8..0xdc
// are genuine floats, and GAMEINFO.DAT was misrecorded as i32 when it is f32
// throughout.
//
// ## The stat block [C-DIS] — +0xc8..0xdc are per-enemy LOG2 MULTIPLIERS
//
// FUN_004202e0 (spawn) computes every enemy stat as `K(column) * GAMEINFO_curve(level)`,
// with `K(v) = 2^((v-50)/20)` (the helper FUN_0041b790; the 20.0 is at 0x6bd994):
//
//   maxHP  = FLOOR(K(+0xC8) * lerp(60, 475, t))
//   ATK    = FLOOR(K(+0xCC) * lerp(40, 295, t))
//   DEF    = FLOOR(K(+0xD0) * lerp(40, 295, t))
//   EXP    = FLOOR(K(+0xDC) * lerp(15, 610, t))   -> actor+0xb0
//   with t = (level - 1) / 59, level from +0xe8
//
// ** +0xDC is the EXP multiplier, and +0xD8 is not — they were swapped here. ** +0xD8
// was labelled "the EXP column" and +0xDC "feeds actor+0xb0, meaning [UNKNOWN]". The
// game's own published EXP settles it: floor(K(+0xDC) * curve4) matches 67/67, while
// +0xD8 manages 9/67. What +0xD8 IS is now [UNKNOWN] again, honestly.
//
// ** FLOOR, not round. ** The engine's helper FUN_004217f0 is floorf. This was written
// as `round` and validated against nothing until the game's own MONSTER JOURNAL was
// checked: with floor, 67 of 68 published enemies match EXACTLY on HP/level/STR/DEF;
// with round, 9. (The 68th, Kelmarel, matches on HP/level/STR and differs only on DEF
// -- and the journal lists DEF 200 for three consecutive rows, so that is a
// transcription error in the published table, not in the data.)
//
// So there is NO raw HP, ATK, DEF, EXP or gold stored anywhere in this record — every
// one is a multiplier against a shared curve. That is why a scan for "a wide non-log2
// i32 column" found nothing: the question was wrong, not the data.
//
// ** The GAMEINFO curve is NOT an enemy's stat. ** A level-1 enemy's DEF is not 40; it
// is K(+0xD0) * 40, and +0xD0's real range (39..55 -> K = 0.68..1.19) puts it at 27..48.
// This is exactly the mistake made on the player side, where the ATK curve was read as
// the final ATK and the damage formula went negative — the curve is a BASE on both
// sides, scaled by a multiplier here and added to by equipment there.
//
// +0xd4 is a constant 50.0 (K == 1.0 exactly) with NO reader in the spawn path. It is
// not consumed by the curve, and why it exists is [UNKNOWN].
//
// The player path uses the same GAMEINFO lerp shape but has NO log2 multiplier at all.
#pragma once
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace cleria {

// One roll. The engine tries each live slot independently, in order.
struct DropSlot {
    int item_id = -1;   // indexes INVINFO; -1 = unused
    float percent = 0;  // 0..100
};

// The log2 encoding every enemy stat column uses. FUN_0041b790: 50 => 1.0 (neutral),
// +20 doubles, -20 halves.
inline float stat_k(float v) { return std::pow(2.0f, (v - 50.0f) / 20.0f); }

// CASTINFO cast_id -> ENEMYINFO record index [C-DIS]. Returns -1 for a cast that has
// no enemy record.
//
//   004202ed  83 FB 53           CMP EBX,0x53
//   004202f3  8D 43 AE           LEA EAX,[EBX - 0x52]
//   004202fb  0F 47 F0           CMOVA ESI,EAX        ; UNSIGNED: if v > 83, v -= 82
//   004202fe  69 F6 08 01 00 00  IMUL ESI,ESI,0x108   ; stride 264
//   00420304  03 35 E0 E7 74 00  ADD ESI,[0x74e7e0]   ; + the ENEMYINFO PAYLOAD base
//
// with the caller supplying `cast_id - 14`:
//
//   00434474  8D 53 F2           LEA EDX,[EBX - 0xe]
//   00434479  E8 62 BE FE FF     CALL 0x4202e0
//
// A bias and a fold, no lookup table. cast 14..83 (M100..M790) -> enemy 0..69, and
// cast 166..185 (M800..M990) -> enemy 70..89: exactly 90, contiguous, no collisions.
//
// ** Note the base at 0x74e7e0 is the PAYLOAD pointer — do NOT add the file's 16-byte
// header again. ** (Same convention trap as GAMEINFO.)
//
// This project asserted `enemy_index == cast_id` for weeks. It is arithmetically
// impossible — 186 casts, 90 records — and it produced entirely plausible stats
// (M_150 -> lv27, HP 332) for the 71 in-range placed casts while silently giving the
// other 9 nothing. The real M_150 is enemy 5: **lv2, HP 113**. CASTINFO +0x64 is a
// decoy that scores well precisely because it equals enemy_index + 3 across the M_
// range — a constant-offset shadow of the right answer, reading three records too far.
// It is genuinely read (0x4342c1 -> FUN_00459410 -> stored to actor+0x120) but it
// never indexes anything.
//
// ** This is GUARDED and the engine's own fold is NOT. ** FUN_004202e0 has no range
// check: for casts below 14, `cast_id - 14` goes negative, CMOVA's UNSIGNED compare
// sees a huge value, and it indexes wildly out of bounds. Six casts (CAST_M000..M050,
// ids 7..12) pass the monster class gate and would do exactly that — but none is
// placed in any of the 192 shipped SCTs, so it is latent, not live. FUN_00459410 has
// the guard; we use it.
inline int enemy_index_for_cast(int cast_id) {
    if ((cast_id >= 14 && cast_id <= 83) || (cast_id >= 166 && cast_id <= 185)) {
        const unsigned v = unsigned(cast_id) - 14u;
        return int(v > 83u ? v - 82u : v);
    }
    return -1;
}

struct Enemy {
    DropSlot drops[8];
    std::int32_t level = 0;  // +0xe8 [C-DIS] — reader is FUN_004202e0 (enemy spawn)

    // The log2 multiplier columns, stored raw. Call stat_k() to decode; they are kept
    // encoded so the file's own values stay visible at the call site.
    float k_maxhp = 0;   // +0xc8
    float k_atk = 0;     // +0xcc
    float k_def = 0;     // +0xd0
    float k_neutral = 0;   // +0xd4 — always 50.0; no reader [UNKNOWN]
    float k_unk_0xd8 = 0;  // +0xd8 — [UNKNOWN]. This was labelled EXP and is NOT.
    float k_exp = 0;       // +0xdc — the EXP reward multiplier -> actor+0xb0

    // Live slots are front-packed (90/90), so this is the count.
    int drop_count() const {
        int n = 0;
        while (n < 8 && drops[n].item_id >= 0) ++n;
        return n;
    }
};

// An enemy's actual stats at spawn. `curve_at_level` is the matching GAMEINFO curve
// evaluated at this enemy's own level — see gameinfo.hpp. The engine rounds.
struct EnemyStats {
    float max_hp = 0, atk = 0, def = 0, exp = 0;
};

struct EnemyTable {
    std::vector<Enemy> enemies;

    static EnemyTable load(const std::filesystem::path& path);  // MISC/ENEMYINFO.DAT
    const Enemy* get(int cast_id) const;
};

// Defined in gameinfo.cpp, where the curves live: stats(e) = K(column) * curve(level).
struct GameInfo;
EnemyStats enemy_stats(const GameInfo& gi, const Enemy& e);

}  // namespace cleria
