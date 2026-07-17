// Boss stats, from MISC/EEX.DAT.
//
// 18 records x 64 bytes (`16 + 64*18 == 1168`, exact). **8 real + 10 inert.**
//
// ## Bosses do NOT use the enemy formula [C-COR]
//
// A normal enemy's stats are `FLOOR(K(ENEMYINFO column) * GAMEINFO curve(level))` —
// log2 multipliers against a shared curve (see enemies.hpp). **Bosses carry RAW
// stats.** That is why the `fold(cast_id - 14)` index never reached them: they are not
// in that table at all.
//
//   +0x00  f32  HP      700, 1200, 2000, 3000, 4000, 5000, 2500, 9999
//   +0x04  f32  STR      69,  120,  150,  176,  220,  232,  255,  258
//   +0x08  f32  DEF      53,  124,  140,  175,  230,  228,  255,  275
//   +0x0c  f32  50.0 — the same neutral marker as ENEMYINFO+0xd4. No reader found.
//   +0x10  f32  140 (mostly) — [UNKNOWN]
//   +0x14  f32  140 / 160    — [UNKNOWN]
//   +0x18  i32  LEVEL     8,  16,   24,   32,   40,   45,   49,   50
//
// The 10 inert records are level 1 with every stat at 50.0.
//
// ## Why these are the 8 bosses [C-COR]
//
// The levels are EXACTLY the game's published boss levels, 8/8:
//
//   8 Velagunder | 16 Nygtilger | 24 Gelaldy | 32 Khonsclard
//   40 Pictimos  | 45 Zava      | 49 Dalles  | 50 Darm
//
// — the tower's floor bosses, in order. And `+0x04` matches their published STR 6/8
// exactly (Gelaldy 150, Khonsclard 176, Pictimos 220, Zava 232, Dalles 255, Darm 258).
// Darm's 9999 HP is the final boss's, and the STR column ramps monotonically with the
// level column. Note the published boss table fills STR from NORMAL difficulty, which
// is the baseline — so it is directly comparable; HP comes from Easy and DEF from Hard,
// which is why those columns are not cross-checked here.
//
// [UNKNOWN] the two STR misses: Velagunder 69 vs published 70, Nygtilger 120 vs 100.
// The Velagunder +1 echoes a systematic +1 on the published weapon Lv1 values, whose
// cause is also unknown. The published table has already been caught in two errors
// (Kelmarel's DEF copied down a column; "Hubamobrum" where the file says "Hubamobrun"),
// so a typo is live — but two unexplained deltas is not something to wave away.
//
// ## A prior pass had this table and rejected it
//
// It counted "8 real + 10 inert" correctly, noted `+0x0c` = 50.0 correctly, and then
// **refused to call the 8 "the bosses" because "the levels don't match"** — while
// reading the wrong offset. `+0x18` is an i32 and reads as a DENORMAL float, the same
// trap that hid ENEMYINFO's level column. The refusal was good discipline applied to a
// bad read: it declined to name the table on evidence that was itself broken.
//
// ## [UNKNOWN] — and it is the whole reason this header exposes no lookup
//
// **How a boss reaches an EEX record is not established.** Bosses route through
// category-4 `CAST_C6xx`/`C7xx`/`C8xx` casts rather than the category-16 monsters, and
// the index expression has not been found. Until it is, this is a table we can read and
// not a table we can use.
//
// **Only 8 of ~16 unique published bosses are here.** Vagullion, Epona, Kishgal, Toal
// Fact, Hugo Fact, Shion, Galleon and Jenocres have published levels and STR but no EEX
// record. Where their stats come from is [UNKNOWN] — a different table, a different
// index into ENEMYINFO, or script.
#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace cleria {

// One boss's raw stats. Not multipliers — the values themselves.
struct BossStats {
    float hp = 0;    // +0x00
    float atk = 0;   // +0x04
    float def = 0;   // +0x08
    float unk10 = 0; // +0x10 [UNKNOWN]
    float unk14 = 0; // +0x14 [UNKNOWN]
    std::int32_t level = 0;  // +0x18 — i32, NOT f32 (it reads as a denormal)

    // The inert records: level 1, everything 50.
    bool is_inert() const { return level <= 1; }
};

struct EexTable {
    std::vector<BossStats> bosses;  // 18 records; 8 real, 10 inert

    static EexTable load(const std::filesystem::path& path);  // MISC/EEX.DAT
};

}  // namespace cleria
