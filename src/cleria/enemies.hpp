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
// and every one of the 443 live drop ids is >= 66, with none below. The boundary in
// the data and the boundary in the drop table are the same number.
//
// ## Level [H-high] — +0xe8, an i32 in 1..60
//
// Tagged [H-high], not [C-COR], and the distinction matters: what is PROVEN is "an
// i32 column, range 1..60, 47 distinct, monotone with drop value (rho 0.81 with
// expected gold; <=2 drops 0.9G, >=55 drops 11167G)". That it IS the level is an
// inference from range and correlation — no code has been observed reading it.
//
// It hid because it reads as DENORMAL floats (1.4013e-45 is the i32 1). This file's
// numeric types cannot be assumed in either direction: the same table's 0xc8..0xdc
// are genuine floats, and GAMEINFO.DAT was misrecorded for weeks as i32 when it is
// f32 throughout.
//
// ## Not here yet
//
// +0xc8..0xdc is a six-wide log2-encoded stat block (K = 2^((v-50)/20)); +0xd4 is
// constant 50.0 => K == 1.0 exactly, the neutral slot. +0xc8 is the HP scale
// (K = 0.25..32.0), read only at spawn (FUN_004202e0) and never by the damage path.
// The rest await the disassembler: there is NO raw EXP or gold value anywhere in the
// record (every non-drop i32 column was scanned for a wide non-log2 range and none
// exists), so EXP and stats must be computed from a curve times these multipliers.
// Until that reader is found, this header does not guess at them.
#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

namespace cleria {

// One roll. The engine tries each live slot independently, in order.
struct DropSlot {
    int item_id = -1;   // indexes INVINFO; -1 = unused
    float percent = 0;  // 0..100
};

struct Enemy {
    DropSlot drops[8];
    std::int32_t level = 0;  // +0xe8 [H-high]

    // Live slots are front-packed (90/90), so this is the count.
    int drop_count() const {
        int n = 0;
        while (n < 8 && drops[n].item_id >= 0) ++n;
        return n;
    }
};

struct EnemyTable {
    std::vector<Enemy> enemies;

    static EnemyTable load(const std::filesystem::path& path);  // MISC/ENEMYINFO.DAT
    const Enemy* get(int cast_id) const;
};

}  // namespace cleria
