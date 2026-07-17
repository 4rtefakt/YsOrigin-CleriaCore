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
// **How a boss reaches an EEX record is not established** — but we now know exactly
// which records ask the question.
//
// ## CAST_C600..C830 are the 24 boss ENCOUNTERS [C-COR]
//
// CASTINFO records 126..149 map 1:1 onto the published boss journal, in order:
// **`journal_no == cast_id - 57`, 24/24 by name.**
//
//   126 C600 Vagullion  #69 lv5    138 C720 Epona      #81 lv43
//   127 C610 Epona      #70 lv5    139 C730 Kishgal    #82 lv43
//   128 C620 Velagunder #71 lv8    140 C740 Zava       #83 lv45
//   129 C630 Epona      #72 lv12   141 C750 Kishgal    #84 lv48
//   130 C640 Vagullion  #73 lv12   142 C760 Toal       #85 lv48
//   131 C650 Nygtilger  #74 lv16   143 C770 Dalles     #86 lv49
//   132 C660 Kishgal    #75 lv19   144 C780 Galleon    #87 lv5
//   133 C670 Toal       #76 lv19   145 C790 Hugo       #88 lv12
//   134 C680 Gelaldy    #77 lv24   146 C800 Shion      #89 lv19
//   135 C690 Jenocres   #78 lv29   147 C810 Kishgal    #90 lv43
//   136 C700 Khonsclard #79 lv32   148 C820 Hugo       #91 lv48
//   137 C710 Pictimos   #80 lv40   149 C830 Darm       #92 lv50
//
// All class 4, all with `enemy_index_for_cast() == -1` — 126..149 falls outside both
// fold ranges (14..83, 166..185), which is precisely why nothing ever reached them.
//
// ** EACH ENCOUNTER HAS ITS OWN CAST RECORD. ** Kishgal is fought four times and has
// four casts (C660, C730, C750, C810). That dissolves the "per-encounter modifier"
// puzzle: his two level-43 rows at STR 228 and 231 are DIFFERENT CASTS, so no single
// record ever had to produce both. It is per-encounter DATA, not a modifier — and it is
// the mechanism by which the same story boss can be balanced differently per playable
// route (the trailing block C780..C830 reads as one character's route).
//
// [UNKNOWN] what a C6xx record points AT. The 8 full-room bosses are C620, C650, C680,
// C700, C710, C740, C770, C830 — NOT contiguous, so the EEX index is not a simple bias
// off cast_id. And these casts have no ENEMYINFO record, yet every encounter has a
// level, so the level lives somewhere in this path too.
//
// Note class 4 is NOT "boss": CAST_C940 "Levinstrike Warhammer", C950 "Crimson
// Lotusblade" and C960 "Rod" are class-4 props. Class 2 appears to mean "player-side
// actor" — only CAST_ADOL and CAST_M900 "Eye of Fact" (Hugo's floating eye) carry it.
//
// Until the reader is found, this is a table we can read and not one we can use.
//
// **Only 8 of the 24 encounters are here** — the medallion-gated full-room bosses.
// Vagullion, Epona, Kishgal, Toal, Hugo, Shion, Galleon and Jenocres are the arena
// fights against story characters, and they have ordinary ENEMYINFO records reached
// through CAST_M800..M880 (see above).
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

// The 8 full-room (medallion) bosses -> their EEX record index [C-DIS][C-COR]
// (build/boss_lookup_re/BOSS_LOOKUP.md). ** This closes the [UNKNOWN] this header opened
// with. ** The index is a COMPILE-TIME IMMEDIATE baked into each boss's own handler,
// reached by dispatch on the script name "boss01".."boss08" — NOT a fold, a cast_id
// table, or a CASTINFO field (all 8 CAST_C records are byte-identical in +0x60/+0x64/
// +0x6c, which is exactly why nothing data-driven ever reached them). The reader is
// FUN_00443b80 (esi = EEX buffer + index*0x40), writing HP->actor+0x94, STR->+0xa4,
// DEF->+0xa8, level->actor+0xc8 from EEX+0x18. Verified against EEX.DAT: the record
// levels (8/16/24/32/40/45/49/50) and Zava/Dalles STR (232/255) match the published boss
// journal in this order. One loop left open in the RE: the handler->named-boss binding
// rests on that level/STR oracle, not a literal SCT/XSO trace of the "bossNN" names.
inline int eex_index_for_cast(int cast_id) {
    switch (cast_id) {
        case 128: return 0;  // C620 Velagunder lv8
        case 131: return 1;  // C650 Nygtilger  lv16
        case 134: return 2;  // C680 Gelaldy    lv24
        case 136: return 3;  // C700 Khonsclard lv32
        case 137: return 4;  // C710 Pictimos   lv40
        case 140: return 5;  // C740 Zava       lv45
        case 143: return 6;  // C770 Dalles     lv49
        case 149: return 7;  // C830 Darm       lv50
        default:  return -1;
    }
}

}  // namespace cleria
