// Actor types, from MISC/CASTINFO.DAT — the middle link of the enemy chain:
//
//   <ROOM>.SCT --name--> MISC/CASTINFO.DAT --fold(cast_id-14)--> MISC/ENEMYINFO.DAT
//   (placement)          (186 actor types)                       (90 enemy tunings)
//
// The last link is NOT the identity — see enemy_index_for_cast() in enemies.hpp. 186
// casts cannot injectively index 90 records, and this line asserted that they could
// for weeks.
//
// 186 records x 512 bytes (0x200); `16 + 512*186 == 95248` accounts for the file
// exactly. Fields verified against the data (build/misc_re/):
//
//   +0x00  char[0x10]  NAME — "CAST_M220"; this is the key SCT places by
//   +0x10  char[0x14]  DISPLAY name — the enemy's REAL name ("Riffligan", "Chryolos")
//   +0x24  char[0x10]  AIA STEM — lower-case, and NOT the filename (see below)
//   +0x60  i32         CAST ID — equals the record index in 186/186
//   +0x64  i32         -> actor+0x120 [C-DIS]; [H-high] the AI HANDLER INDEX
//   +0x6c  i32         ACTOR CLASS [C-DIS]: 0x10 = monster (86), 4 (87), 2 (2), 0 (11)
//   +0x78  char[0x100] DESCRIPTION (the Topic datacard text; up to 216 bytes in use)
//   +0x178 char[0x88]  a second text block (empty in the EN data)
//
// ## +0x10 is the real name, and we ignored it for weeks [C-COR]
//
// This line used to read "DISPLAY name (empty for most records)". It is **non-empty in
// 147 of 186 records** — exactly backwards — and it holds the names the game shows in
// its monster journal: CAST_M100 = "Riffligan", CAST_M220 = "Chryolos", CAST_ADOL =
// "Adol". The field has been parsed into `Cast::display` since the loader was written
// and nothing ever read it.
//
// I found it by cross-matching the game's published monster journal against our
// computed stats to recover enemy names — and then discovered the names were sitting
// in a field we already parsed, dismissed by our own comment. Look at the data before
// deriving it.
//
// The triangulation is worth keeping though, because it is unusually strong: the
// journal's NAME, our STAT match (level/HP/STR/DEF), and this NAME FIELD agree on
// 67/67 enemies independently. It also catches a published typo — the journal prints
// "Hubamobrum" where the game's own file says "Hubamobrun".
//
// ## +0x64 is [H-high] the AI handler index — two half-answers, joined
//
// This sat as "[H-medium] cross-reference, provisional" while the answer was spread
// across two RE notes that never met:
//
//   build/enemy_re/ENEMY_PLACEMENT.md : "+0x64 -> actor+0x120. Meaning [UNKNOWN]" [C-DIS]
//   the combat pass                    : "the 94-entry AI dispatch table at 0x69b8c0
//                                         is indexed by actor+0x120"                [C-DIS]
//
// Both legs are disassembled; only the composition was missing. Corroboration: every
// one of the 186 casts has +0x64 in 0..93 — exactly the table's index range — with 92
// distinct values across 94 handlers, and CAST_ADOL (the player) is 0.
//
// [H-high] not [C-DIS] because the final read has not been watched: "indexes the
// table" could carry an offset, and a range fit is not a reader.
//
// It is ALSO the decoy that cost us the enemy index — see enemies.hpp. It equals
// enemy_index + 3 across the M_ range, which is why it scored so well and read three
// records too far. A field can correlate with an index without being one.
//
// ## +0x6c is the actor class [C-DIS]
//
// 0x10 = monster (86 casts), 4 (87), 2 (2), 0 (11). It selects the +0x60 flag word
// (0x81/0x84/0x88), and it is the enemy/NPC discriminator — NOT the name prefix.
// It is also the gate the enemy spawn path checks (`& 8`).
//
// ## The stem is not a filename [C-COR]
//
// It stores "c000" while the file is "C_101.AIA": the rule is UPPERCASE AND INSERT
// AN UNDERSCORE AFTER THE FIRST CHARACTER. Read literally, 0 of 95 stems resolve;
// under the rule, 86/95 do. (I tested the literal reading first, got 0/95, and
// briefly believed a corpus-validated claim was wrong. The claim was right and my
// test was.)
//
// ## Record 13, CAST_ADOL, IS THE PLAYER [C-COR]
//
// This is the same 13 as the damage path's `cast != 13` player test (actor+0x124), and
// it is why that magic number is 13: CAST_ADOL is simply record 13 of this table.
//
// It carries the player's own movement tuning, and it is the ONLY record that does —
// it is unique among all 186 on three movement fields simultaneously:
//
//   +0x48 base speed  0.056   (168 other casts share 0.035)
//   +0x50 jump v0     0.285   (169 other casts share 0.22)
//   +0x4c             2.2     (86 share 1.5, 86 share 1.6)
//
// Only the player gets bespoke tuning. That is independent corroboration of the
// identification, arrived at from the movement code rather than from the name.
//
// ## The 9 that don't resolve are explained, not missing [C-COR]
//
//   CAST_ADOL  — placed 182 times across 181 of 192 rooms: it doubles as the SPAWN
//                POINT. It has no AIA because the player's sprite is per-character and
//                cannot be one fixed stem — the AIA is chosen at runtime by
//                FUN_00464fc0 from the character index. A record with no sprite is not
//                a record with no purpose. Note it is category 2, not the 4/16 below.
//
//                ** The name is NOT vestigial. ** This file used to claim "Adol is not
//                in Ys Origin at all — it is a prequel set 700 years before him". That
//                is wrong: **Adol is playable in ARCADE MODE**, just not in the story.
//                His sprite set is C_110 (red hair, sword and shield) and he is
//                character 0 of the four — which is exactly why the player table, and
//                GAMEINFO's weapon-ATK table, have FOUR rows for a three-character
//                story. I inferred "vestigial" from knowing the setting rather than
//                from the game; the data was right and the lore reasoning was wrong.
//   the other 8 — CAST_C000, CAST_M000..M050, CAST_M790: **never placed in any of
//                the 192 SCT files.** Dead table entries.
//
// So every cast the game actually places either resolves to an AIA or has a reason not
// to. There is no gap here to chase.
//
// ## Movement fields [C-DIS] — see cleria/physics_player.hpp for the model
//
//   +0x34  f32  collision radius base — the engine uses radius = +0x34 * 0.9
//   +0x38  f32  height base           — the engine uses height = +0x38 + 0.075
//   +0x48  f32  base speed scalar
//   +0x50  f32  jump v0 -> actor+0x408
//
// These are the "derived per-actor from the player struct" that cleria-view's own
// comment admitted to not modelling. Read them per-cast rather than hardcoding: they
// are why a Chryolos and Yunica move differently.
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
    std::string description;  // +0x78 (0x100: the Topic datacard's first text block)
    std::string description2;  // +0x178 (the second block; empty in the EN data)
    // Movement, per cast [C-DIS] — see the note above and physics_player.hpp. Only
    // record 13 (the player) carries bespoke values; 168-169 casts share the defaults.
    float radius_base = 0;  // +0x34
    float height_base = 0;  // +0x38
    float speed_base = 0;   // +0x48
    // The hurt body (victim record +0x60, build/ai_spec/HITVOLUME.md §4): radius, cylinder
    // half-height, shape (0 sphere, else a vertical cylinder), centred +0x54 off the origin.
    float hurt_radius = 0;        // +0x30 -> +0x68
    float hurt_half_h = 0;        // +0x58 -> +0x78
    std::int32_t hurt_shape = 0;  // +0x5c -> +0x7c
    float lift = 0;         // +0x44 -> actor+0x414: spawn height above the rest origin; > 0 makes
                            // a flyer (FUN_00459410 0x45960f) [C-DIS build/ai_spec/FLYER.md]
    float jump_v0 = 0;      // +0x50
    float run_mult = 0;     // +0x4c -> actor+0x410 (2.2 for the player)
    float center_offset = 0;  // +0x54 -> actor+0x41c (collision-sphere centre = origin + this)
    std::int32_t ai_index = 0;    // +0x64 -> actor+0x120 [H-high] the AI handler
    std::int32_t actor_class = 0; // +0x6c [C-DIS] 0x10=monster, 4, 2, 0

    bool is_monster() const { return actor_class == 0x10; }

    // True for CAST_ADOL, the player spawn marker: placed everywhere, drawn never.
    bool is_spawn_marker() const { return name == "CAST_ADOL"; }
};

struct CastTable {
    std::vector<Cast> casts;

    static CastTable load(const std::filesystem::path& path);  // MISC/CASTINFO.DAT
    const Cast* get(int cast_id) const;
    const Cast* find(std::string_view name) const;  // by the SCT's key

    // The sprite stem for a cast: "m100" -> "M_100" (uppercase, "_" after char 0),
    // the same transform aia_path uses. This is how a spawned actor (which knows only
    // its cast_id) recovers the "M_100"-style key the sprite sets are stored under —
    // it matches CastPlacement::sprite_stem's SCT-name derivation exactly, because
    // CASTINFO+0x24 is "m100" for the same cast the SCT names "CAST_M100". Empty when
    // the cast has no stem.
    std::string sprite_stem(int cast_id) const;

    // "c000" -> <root>/ANI/C_000.AIA. Empty when the cast has no stem.
    std::filesystem::path aia_path(const std::filesystem::path& root, int cast_id) const;
};

}  // namespace cleria
