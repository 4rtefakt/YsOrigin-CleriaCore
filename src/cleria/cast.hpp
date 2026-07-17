// Actor types, from MISC/CASTINFO.DAT — the middle link of the enemy chain:
//
//   <ROOM>.SCT --name--> MISC/CASTINFO.DAT --cast_id--> MISC/ENEMYINFO.DAT
//   (placement)          (186 actor types)              (90 enemy tunings)
//
// 186 records x 512 bytes (0x200); `16 + 512*186 == 95248` accounts for the file
// exactly. Fields verified against the data (build/misc_re/):
//
//   +0x00  char[0x10]  NAME — "CAST_M220"; this is the key SCT places by
//   +0x10  char[0x14]  DISPLAY name (empty for most records)
//   +0x24  char[0x10]  AIA STEM — lower-case, and NOT the filename (see below)
//   +0x60  i32         CAST ID — equals the record index in 186/186
//   +0x64  i32         [H-medium] cross-reference; provisional, do not build on it
//   +0x6c  i32         [H-medium] category; provisional
//   +0x78  char[0x40]  DESCRIPTION
//
// ## The stem is not a filename [C-COR]
//
// It stores "c000" while the file is "C_101.AIA": the rule is UPPERCASE AND INSERT
// AN UNDERSCORE AFTER THE FIRST CHARACTER. Read literally, 0 of 95 stems resolve;
// under the rule, 86/95 do. (I tested the literal reading first, got 0/95, and
// briefly believed a corpus-validated claim was wrong. The claim was right and my
// test was.)
//
// ## The 9 that don't resolve are explained, not missing [C-COR]
//
//   CAST_ADOL  — placed 182 times across 181 of 192 rooms. It is the PLAYER SPAWN
//                MARKER, not a character; a marker needs no sprite. (Adol is not in
//                Ys Origin at all — it is a prequel set 700 years before him.) Note
//                it is category 2 while the others below are 4/16.
//   the other 8 — CAST_C000, CAST_M000..M050, CAST_M790: **never placed in any of
//                the 192 SCT files.** Dead table entries.
//
// So every cast the game actually places either resolves to an AIA or is a marker
// that should not have one. There is no gap here to chase.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace cleria {

struct Cast {
    std::string name;         // +0x00  "CAST_M220" — the SCT key
    std::string display;      // +0x10
    std::string stem;         // +0x24  as stored, lower-case ("c000")
    std::int32_t cast_id = 0;  // +0x60  == the record index
    std::string description;  // +0x78

    // True for CAST_ADOL, the player spawn marker: placed everywhere, drawn never.
    bool is_spawn_marker() const { return name == "CAST_ADOL"; }
};

struct CastTable {
    std::vector<Cast> casts;

    static CastTable load(const std::filesystem::path& path);  // MISC/CASTINFO.DAT
    const Cast* get(int cast_id) const;
    const Cast* find(std::string_view name) const;  // by the SCT's key

    // "c000" -> <root>/ANI/C_000.AIA. Empty when the cast has no stem.
    std::filesystem::path aia_path(const std::filesystem::path& root, int cast_id) const;
};

}  // namespace cleria
