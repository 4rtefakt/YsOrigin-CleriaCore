// SCT — per-room actor placement (enemies and the player spawn).
//
// Despite the "CAST_" names this is an *instance* list, not a cast list: one
// record per placed actor, so a room repeats `CAST_M220` once per Chryolos it
// wants. `CAST_ADOL` is the player's spawn point.
//
//   <ROOM>.SCT --name--> MISC/CASTINFO.DAT --fold--> MISC/ENEMYINFO.DAT
//   (placement)          (186 actor types)           (90 enemy tunings)
//
// ** The last link is SOLVED: `enemy_index_for_cast()` in enemies.hpp. ** It is a FOLD
// of two cast ranges onto one table — `cast_id - 14`, with 166..185 folding back down
// behind 14..83. Do not re-derive it here; that header owns it, states the engine's
// unguarded original (FUN_004202e0) and why ours guards, and carries the evidence.
//
// The whole chain is closed and EXTERNALLY VALIDATED — our stats reproduce the game's
// own in-game monster journal 67/68 on HP/level/STR/DEF/EXP, the one miss being a typo
// in the journal that our data locates. That is the only check this project has that
// is not our own code against our own code.
//
// ** This comment asserted the opposite for weeks after the fold was proven, which is
// why it now points at the owner instead of restating the answer. ** Two wrong readings
// are worth keeping, because both were plausible and one shipped:
//   - `cast_id` directly: arithmetically impossible (186 casts, 90 records), yet it was
//     believed long enough to reach the renderer, where it produced entirely plausible
//     stats (M_150 lv27, HP 332, ATK 153) for 71 of 80 casts and silently gave the
//     other 9 nothing. The disproving arithmetic had been sitting there the whole time.
//   - `+0x64`: scored BETTER (in range for 78/80, and it looks like an index —
//     CAST_M100 -> 3, CAST_M130 -> 6, CAST_M150 -> 8) and is genuinely read by the
//     engine. It is a decoy that tracks the truth at a constant offset: it equals
//     enemy_index + 3 across the M_ range, reading three records too far. A near-miss
//     that shadows the right answer is worse than a random guess.
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
    float yaw = 0;      // radians; the file stores int degrees (360 or -1: the "random" path = 90)
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
