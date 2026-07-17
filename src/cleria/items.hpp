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
// The dim set resolves 120/128. The 8 without one are ids 120..127 — the GOLD
// denominations (1G/5G/.../5000G, prefix GO). Gold is never equipped or
// unavailable, so it never needs a greyed icon: a reason, not a gap. That also
// confirms ICON2/*V is the "unavailable" variant.
//
// ## +0x24 is a MAGNITUDE, and its unit depends on the category [C-COR]
//
//   go (gold, 120..127)     1, 5, 10, 50, 100, 500, 1000, 5000
//   tl (tools, 66..115)     0, 10, 25, 50, 100, 200, 999
//   ar (armor, 6..29)       7, 8, 13, 16, 20, 24, 26, 32, 33, 39, 40, 48, ...
//   sh (shields, 30..53)    6, 7, 12, 13, 18, 20, 24, 26, 30, 33, 36, 39, ...
//   sw (swords, 0..5)       20 and 999 ONLY
//   ac / sk                 all 0
//
// The gold row is airtight: **+0x24 equals the number in the item's own NAME, 8/8**
// ("1G" -> 1 ... "5000G" -> 5000). Tools read as heal amounts (Recovery Drop 1..4 =
// 10/25/50/100). Armor and shields ramp, which makes them a DEF candidate.
//
// ** But swords are all 20 or 999, so this is NOT a generic "stat value". ** If it
// were, swords would ramp like armor and they do not. So the armor/shield DEF reading
// is [H-high] at best and is NOT exposed here. The 999 sentinel lands on exactly three
// items: 4 (sw_04), 5 (sw_05), 71 (tl_05 "Recovery Drop 6") — plainly a full-heal on
// the tool, and something else on two swords.
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

}  // namespace cleria
