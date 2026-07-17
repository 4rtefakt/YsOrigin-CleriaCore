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
//
// ** But treat the published BOSS rows as a weak oracle. ** Unlike the monster journal
// (entries 1..68, complete, and which our stats match 67/68), the boss section is
// explicitly a STUB: its HP/DEF/EXP columns are blank, and its remaining columns are
// crowd-sourced from DIFFERENT difficulties — HP from Easy, STR from Normal, DEF from
// Hard, EXP from Very Easy, SP from Nightmare. Only STR is baseline-comparable.
//
// That matters for a live question we cannot currently answer: whether the story
// characters have PER-CHARACTER stats to balance the three routes. Kishgal is published
// twice at level 43 with STR 228 and 231, and neither is reachable at ANY level (43
// gives 229; its neighbours give 224 and 233), so a level override alone cannot explain
// them. A per-route modifier would — but so would two contributors playing different
// difficulties, and a constant per-character modifier is contradicted by Hugo, whose
// two ratios are 1.05 and 1.14. **The data supports "something else is going on" and
// cannot distinguish which. It needs an in-game reading, not more arithmetic.**
//
// Hugo (e71) is the loudest outlier: lv12 -> 78 vs published 82, lv48 -> 217 vs 249.
// Not an off-by-one — a different scale, and [UNKNOWN].
//
// ## A prior pass had this table and rejected it
//
// It counted "8 real + 10 inert" correctly, noted `+0x0c` = 50.0 correctly, and then
// **refused to call the 8 "the bosses" because "the levels don't match"** — while
// reading the wrong offset. `+0x18` is an i32 and reads as a DENORMAL float, the same
// trap that hid ENEMYINFO's level column. The refusal was good discipline applied to a
// bad read: it declined to name the table on evidence that was itself broken.
//
// ## The game has TWO kinds of boss, and they are two data paths [C-COR]
//
// Identified by someone who has played it, then confirmed in the data:
//
//   1. "FULL-ROOM" bosses — they fill most of the arena and are gated behind a
//      medallion. These are the 8 EEX records, and they carry RAW stats.
//   2. ARENA fights against STORY CHARACTERS who try to stop you climbing —
//      Kishgal, Epona, Toal, Galleon, Shion, Hugo. **These are ORDINARY ENEMYINFO
//      enemies**, reached by the same fold(cast_id-14) as any monster.
//
// CASTINFO's high range is the cast list, not a monster list:
//
//   166 CAST_M800 "Yunica"  class 4 -> enemy 70     172 CAST_M860 "Kishgal" -> 76
//   167 CAST_M810 "Hugo"    class 4 -> enemy 71     173 CAST_M870 "Zava"    -> 77
//   168 CAST_M820 "Toal"    class 4 -> enemy 72     174 CAST_M880 "Dalles"  -> 78
//   169 CAST_M830 "Galleon" class 4 -> enemy 73     176 CAST_M900 "Eye of Fact" class 2
//   170 CAST_M840 "Shion"   class 4 -> enemy 74
//   171 CAST_M850 "Epona"   class 4 -> enemy 75
//
// So class 4 vs class 16 is the boss/monster discriminator — NOT a different index.
//
// ## The LEVEL OVERRIDE is what makes rematches work [C-COR]
//
// Story characters are fought several times during the climb. One ENEMYINFO record
// serves every rematch, with the spawn op supplying a level override (FUN_004202e0
// takes "the spawn op's level_override if nonzero"). Evaluating each record at its
// PUBLISHED level rather than its own reproduces the published STR exactly:
//
//   Toal    (e72, own lv1)  lv19 -> 119 ✔   lv48 -> 247 ✔   <- ONE record, TWO fights
//   Galleon (e73)           lv5  -> 58  ✔
//   Shion   (e74)           lv19 -> 121 ✔
//   Epona   (e75, own lv48) lv5  -> 58  ✔   lv12 -> 89  ✔
//   Kishgal (e76, own lv1)  lv19 -> 121 ✔   lv48 -> 251 ✔
//
// ** ZAVA AND DALLES ARE IN BOTH TABLES, and their boss fight reads EEX. ** Their
// ENEMYINFO records give STR 242 / 269 at the published levels; EEX gives 232 / 255 —
// the published values exactly. That is the two-kinds-of-boss split showing up as two
// data paths, and it is the sharpest available probe for finding the fork.
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
