// SCT — per-room actor placement (enemies and the player spawn).
//
// Despite the "CAST_" names this is an *instance* list, not a cast list: one
// record per placed actor, so a room repeats `CAST_M220` once per Chryolos it
// wants. `CAST_ADOL` is the player's spawn point.
//
//   <ROOM>.SCT --name--> MISC/CASTINFO.DAT --cast_id--> MISC/ENEMYINFO.DAT
//   (placement)          (186 actor types)              (90 enemy tunings)
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
