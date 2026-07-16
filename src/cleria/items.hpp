// Item id -> name / icon / description, from MISC/INVINFO.DAT.
//
// 128 records x 184 bytes (0xb8); the record index IS the item id. Layout, mapped by
// inspection and corpus-checked — no disassembly needed, the fields are in the data:
//
//   +0x00  char[0x20]  NAME, NUL-terminated ASCII (English comes from data_us)
//   +0x24  u32         [UNKNOWN] small ints (0,1,5..20) and a 999 sentinel
//   +0x34  char[0x10]  ICON STEM -> MENU/ICON/<STEM>.DDS  (and ICON2/<STEM>V.DDS)
//   +0x3C  f32         [H-medium] grid X — steps by exactly 38 (160,198,236,274,...)
//   +0x40  f32         [H-medium] grid Y — steps by exactly 42 (54,96,138,180,...)
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
// The two floats' regular 38/42 pitch reads as a fixed slot grid in the inventory
// screen, but nothing has been traced to confirm it — hence [H-medium].
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
    std::uint32_t unk24 = 0;
    float grid_x = 0, grid_y = 0;
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
