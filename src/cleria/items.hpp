// Item id -> name / icon / description, from MISC/INVINFO.DAT.
//
// 128 records x 184 bytes (0xb8); the record index IS the item id. Layout, mapped by
// inspection and corpus-checked — no disassembly needed, the fields are in the data:
//
//   +0x00  char[0x20]  NAME, NUL-terminated ASCII (English comes from data_us)
//   +0x24  u32         MAGNITUDE — the unit depends on the category (see below)
//   +0x34  char[0x10]  ICON STEM -> MENU/ICON/<STEM>.DDS  (and ICON2/<STEM>V.DDS)
//   +0x3C  f32         [UNKNOWN] — NOT the inventory grid; see below
//   +0x40  f32         [UNKNOWN] — uniform 42 pitch, but no known reader
//   +0x4C  char[0x6C]  DESCRIPTION, with CR/LF line breaks
//
// The icon stem RESOLVES FOR ALL 128 RECORDS (128 hit / 0 miss against the 132 files
// in MENU/ICON), and the prefixes partition the table exactly: SW 6, AR 24, SH 24,
// AC 12, TL 50, SK 4, GO 8 = 128. That is the mapping the HUD needs for its four
// equipment slots and the menus need for the inventory/shop lists.
//
// ** But the prefixes are NOT what we called them. ** Read the items' own names:
//
//   sh 30..47  SHOES, not shields — "Leather Boots", "Hard Leggings", "Sylphen Boots"
//   sh 48..53  the REAL shields — "Wooden Shield".."Raval Shield" (`sh_aa`..`sh_ff`,
//              the `_aa` suffix marking Adol's arcade-only gear, like `ar_aa..ff`)
//   sk 116..119 the elemental magic BRACELETS — "Ventus/Terra/Ignis Bracelet" plus an
//              "Unequip magic." stub. NOT seeds. The real seeds are tl_48 "Vitality
//              Seed" and tl_49 "Air Seed".
//   tl 78..83  the six AREA MEDALLIONS, and their descriptions name the areas in
//              tower order: Wailing Blue, Flooded Prison, Guilty Fire, Silent Sands,
//              (Cecilia's), Demonic Core — which independently corroborates the
//              S_1..S_6 section mapping derived from enemy levels.
//
// We had "sh = shields" and "sk = seeds" for weeks. Both are wrong, and both are
// refuted by the item's own name field, which we were already parsing.
//
// The dim set resolves 120/128. The 8 without one are ids 120..127 — the GOLD
// denominations (1G/5G/.../5000G, prefix GO). Gold is never equipped or
// unavailable, so it never needs a greyed icon: a reason, not a gap. That also
// confirms ICON2/*V is the "unavailable" variant.
//
// ## +0x24 is the DEF COLUMN for armour and shoes/shields [C-DIS + C-COR]
//
// ** The data states its own answer: +0x24 == the "DEF +N" written in the item's own
// DESCRIPTION, 48/48 for ar and sh. ** That is the same kind of self-check as the gold
// denominations equalling the number in their name, and it is the strongest evidence
// available from the files alone.
//
// It is NOT, however, independent confirmation from outside. The published wiki tables
// agree 36/36 — but the wiki transcribed the game's own description text, so that
// agreement only says +0x24 matches +0x4c. Two readings of one source are not two
// sources. (This is the trap that made two implementations agree to five significant
// figures on the clip-AABB outliers.)
//
// ** The 49th item is the interesting one: "Goddess Ring" (id 59, an ACCESSORY) says
// "DEF +5" in its description and has +0x24 == 0. ** So accessory DEF does NOT come
// from this column — which fits the disassembly exactly: `IMUL ..., 0xb8` (the record
// stride multiply that indexes this table) occurs ONLY in the armour and shoe/shield
// branches.
//
// ** That [UNKNOWN] is now CLOSED (build/equip_re/EQUIP.md [C-DIS]): accessory stat
// bonuses are HARDCODED constants in the aggregator FUN_004208f0, not data. ** Exactly
// two accessories change stats — Goddess Ring (id 0x3b) = +5 ATK / +5 DEF, Cleria Ring
// (id 0x3c) = +3 ATK / −3 DEF — both confirmed by their description strings. Every other
// accessory only sets ability-FLAG bits (gameplay meaning [UNKNOWN]).
//
// ## The full player stat aggregation [C-DIS] (FUN_004208f0), for when equipment is wired
//
//   final ATK = base level curve + GAMEINFO.weapon[char][tier]   (INVINFO NEVER feeds ATK)
//                                 + Goddess(+5) / Cleria(+3)
//   final DEF = base level curve + INVINFO[armor]+0x24 + INVINFO[footwear]+0x24
//                                 + Goddess(+5) / Cleria(−3)
//   final MaxHP = the level curve ALONE (lerp(60,400,t), cap 999) — equipment adds NOTHING
//                 to HP or SP (the equip-HP multiply is a hardcoded ×1.0).
//
// Equipped-item ids live in the save/flags block: weapon DAT_0076bb78, armor 0x76bb7c,
// footwear/shield 0x76bb80, accessories @inventory+0x44; the per-character weapon TIER is
// a runtime mirror at 0x76a634+char*8 (0..10). Not wired here — cleria-view has no
// inventory/save model yet — but the formula and slots are recorded so it can be.
//
// The engine reads it as an i32 via CVTDQ2PS, from the INVINFO base at 0x76f1c0
// (proven three ways: the g_scn_data.cpp loader does PUSH 0x76f1c0; +0x34 -> icon stem
// and +0x4c -> description land on known fields from that base; and the arithmetic).
// Note an earlier pass put the base at 0x76f1e4 — which is exactly 0x76f1c0 + 0x24,
// i.e. it had mistaken THIS COLUMN's address for the table's.
//
// ** There is NO weapon ATK column. ** `IMUL ..., 0xb8` — the record-stride multiply
// that indexes this table — occurs ONLY in the armour and shield branches. Weapon ATK
// comes from GAMEINFO's 4x11 [char][tier] table instead (see gameinfo.hpp), which is
// why swords here are a flat 20/999: **that sword data is dead.**
//
// That flatness is worth dwelling on. It was written up as the reason to DISTRUST the
// armour/shield DEF reading — "a generic stat column would have swords ramping too" —
// and treated as an anomaly weakening the case. It was the opposite: the clue. Swords
// are flat *because* nothing reads them. The paradox was pointing at the answer.
//
// ## The other categories [C-COR]
//
//   go (gold, 120..127)     1, 5, 10, 50, 100, 500, 1000, 5000
//   tl (tools, 66..115)     0, 10, 25, 50, 100, 200, 999
//   ar (armor, 6..29)       7, 8, 13, 16, 20, 24, 26, 32, 33, 39, 40, 48, ...
//   sh (shoes 30..47 + shields 48..53)  6, 12, 18, 24, 30, 36 | 7, 13, 20, 26, 33, 39
//   sw (swords, 0..5)       20 and 999 ONLY
//   ac / sk                 all 0
//
// The gold row is airtight: **+0x24 equals the number in the item's own NAME, 8/8**
// ("1G" -> 1 ... "5000G" -> 5000). Tools read as heal amounts (Recovery Drop 1..4 =
// 10/25/50/100). So the column is a magnitude whose UNIT depends on the category, and
// `magnitude` is what it is called here for that reason.
//
// The armour value is load-bearing: a level-1 player's DEF is the GAMEINFO curve (30)
// plus the starting Leather Armor's +0x24 (**8**) = 38. Equipment adds into the same
// field before it reaches actor+0xa8.
//
// ## +0x3C / +0x40 are NOT the inventory grid — the old claim is refuted
//
// This file used to call them "grid X — steps by exactly 38" and "grid Y — steps by
// exactly 42", [H-medium]. The X claim is simply false, and it is a clean example of
// generalising from the head of a list: the first four values ARE 160, 198, 236, 274,
// a perfect 38 pitch. The full set is not:
//
//   +0x3C: 160 198 236 260 274 298 312 336 350 374 412 418 456 472 494 510 532 548 570
//   steps:   38  38  24  14  24  14  24  14  24  38   6  38  16  22  16  22  16  22
//
// And the menu's REAL inventory cursor lattice, read out of the code, is
// `(441 + col*75, 72 + row*64)` — pitch 75/64, not 38/42. Zero of the 19 X values fit
// it; one of nine Y values does, which is chance. The menus also hardcode every rect
// as a float immediate, with no table of rects to read — so a layout grid in INVINFO
// would have nothing to feed.
//
// +0x40's 42 pitch IS uniform, and both are zero for all 8 gold items (which are
// never shown in a grid). That is suggestive of a position for *something*, but no
// reader has been found, so both are [UNKNOWN] rather than named.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cleria {

struct Item {
    std::string name;
    std::string icon;         // stem, lower-case as stored (e.g. "sw_00")
    std::string description;  // may contain CR/LF
    // +0x24. Named for what it IS (a magnitude) rather than what it means, because
    // what it means depends on the category and only the gold case is proven.
    std::uint32_t magnitude = 0;
    // +0x3C / +0x40. Deliberately NOT called grid_x/grid_y — that reading is refuted
    // (see the header). Named for their offsets so nothing reads meaning into them.
    float f3c = 0, f40 = 0;
};

struct ItemTable {
    std::vector<Item> items;  // indexed by item id

    static ItemTable load(const std::filesystem::path& invinfo_dat);
    std::string name(int id) const;  // falls back to "item 0xNN" if unknown
    const Item* get(int id) const;
    // Absolute path of an item's icon under `asset_root`, or "" if unknown.
    // `dim` picks MENU/ICON2/<STEM>V.DDS — the greyed variant, which pairs with the
    // HUD's `tint & 0xff303030` for an unequipped slot.
    std::filesystem::path icon_path(const std::filesystem::path& asset_root, int id,
                                    bool dim = false) const;
};

// CleriaCore mods (content items.ini, build/shell_re/CONTENT.md §5): a patch applied by every
// ItemTable::load after the file is read, so every table in the process sees the same items. Null
// (the default, and with no content mod) = the file as read. Not retail.
using ItemPatchFn = void (*)(ItemTable&);
void set_item_patch(ItemPatchFn fn);

}  // namespace cleria
