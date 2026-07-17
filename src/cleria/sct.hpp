// SCT — per-room actor placement (enemies and the player spawn).
//
// Despite the "CAST_" names this is an *instance* list, not a cast list: one
// record per placed actor, so a room repeats `CAST_M220` once per Chryolos it
// wants. `CAST_ADOL` is the player's spawn point.
//
//   <ROOM>.SCT --name--> MISC/CASTINFO.DAT --???--> MISC/ENEMYINFO.DAT
//   (placement)          (186 actor types)          (90 enemy tunings)
//
// ** The last link is [UNKNOWN], and this comment used to assert it was `cast_id`. **
// It cannot be: CASTINFO has 186 records and ENEMYINFO has 90, so cast_id is out of
// range for 96 of them. That claim was believed long enough to get wired into the
// renderer, where it produced completely plausible enemy stats (M_150 lv27, HP 332,
// ATK 153) for 71 of the 80 placed casts and silently gave the other 9 nothing. The
// arithmetic had been sitting there the whole time.
//
// `+0x64` is the better candidate — in range for 78/80 placed casts, and it looks like
// an index (CAST_M100 -> 3, CAST_M130 -> 6, CAST_M150 -> 8) — but it is NOT right
// either: it puts level 25 and level 38 enemies in the first tower rooms. Both remain
// unverified, so enemy stats stay placeholders. Everything downstream of this index is
// solved (the level at ENEMYINFO+0xe8, the log2 multipliers at +0xc8..0xdc, the
// GAMEINFO curve, the damage formula) — this one link is all that is missing.
//
// Reverse-engineered from yso_win.exe: loader/consumer FUN_004341a0, which walks
// records from `base + 0x108` — which is why the useful fields sit late in the
// record. Corpus: 192 files, 1290 records, 1106 enemy placements in 85 rooms.
// See build/enemy_re/ and tools/sct_enemies.py.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

struct CastPlacement {
    std::string name;   // "CAST_M100", "CAST_ADOL", ...
    Vec3 position{};    // world
    float yaw = 0;      // radians; the file stores degrees (360 or -1 = random facing)
    bool random_yaw = false;

    bool is_player() const { return name == "CAST_ADOL"; }
    // Sprite stem by naming convention: CAST_M100 -> M_100 (verified: 89/90 casts
    // have a matching ANI/<stem>.AIA). Empty when the name isn't a monster.
    std::string sprite_stem() const;
};

struct Sct {
    std::vector<CastPlacement> casts;

    static Sct parse(const std::vector<std::uint8_t>& bytes);
    bool ok() const { return !casts.empty(); }
    // The player spawn, or null if the room has no CAST_ADOL record.
    const CastPlacement* player_spawn() const;
};

}  // namespace cleria
