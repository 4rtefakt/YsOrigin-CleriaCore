// Sand2 (surface 0x12, S_40's quicksand) for the player: the selector-1 ("water") set over a sand2
// floor. build/physics_re/WATER.md §1a / §2a, read again from the bytes 2026-10-08:
//
// * FUN_0045c490's water test [BYTES 0x45c58c..]: the up-ray starts 0.25 (0x6bd244) under the feet,
//   0.7 (0x6bd36c) more when the last floor ray's surface +0x4ba is 0x12. On a hit over sand2 the hit
//   distance goes to +0x390 and, past 2.25 (0x6bd5d0), +0x38c ("buried") = 1 — the word write
//   +0x38b = 0 at the top clears +0x38b / +0x38c every tick. Only the oxygen reads +0x38c (oxygen.hpp:
//   buried drains 100 a tick, 50 with the Scales).
// * FUN_0045a510's dy [BYTES 0x45a63f..0x45a6b7]: in the set (+0x38a) over sand2 (+0x4ba == 0x12)
//   dy is clamped to [-0.015 (0x6bdfb4), +0.03 (0x6bd100)]; while +0x3b8 != 0 (the mover's walk
//   counter: +1.0 a tick with input, 0 when it stops — FUN_00465170) and +0x320 + 0.925 (0x6bd3e8) >
//   y + 0.01 (xmm4 = 0x6bd084) dy = 0.01: walking lifts the origin to the sand top + 0.915, standing
//   sinks at 0.015 a tick. Then +0x4b9 = 0x12 (so the xz x0.25 of surface_scale applies).
// * The settle [EXPORT 0045a510.c:912-929, the branch read in the bytes]: above the floor, once
//   jumping or past the coyote, +0x388 = 0 — but in the set over sand2 +0x388 = 1 again (the actor
//   stays "grounded" in the sand, so dy keeps the linear t); with +0x38b (it entered the set last
//   tick) and section 3, the landing follow-on (FUN_00446550(0 / 1) by +0x3a0, else FUN_00446680(3)).
// * The splash on sand2 [BYTES 0x45c90d..]: effect 0x163 (the player's 0x44 otherwise), SE 0x28
//   (sand) instead of 0x30.
#pragma once
#include <algorithm>

namespace cleria {

struct PlayerSand {
    int surf_ray = 1;            // +0x4ba: the last floor ray's surface (any distance)
    float water_y = -1.0e30f;    // +0x320: the selector-1 surface above (persists on a miss)
    bool entered = false;        // +0x38b: the set was entered on the last water test
    bool buried = false;         // +0x38c

    bool on_sand2() const { return surf_ray == 0x12; }
    // The water test's start under the feet.
    float probe_drop() const { return on_sand2() ? 0.25f + 0.7f : 0.25f; }
};

// FUN_0045a510 0x45a652..0x45a6b7: dy for an actor in the set over sand2 (`origin_y` = +0x54).
inline float sand2_dy(float dy, bool walking, float water_y, float origin_y) {
    dy = std::clamp(dy, -0.015f, 0.03f);
    if (walking && water_y + 0.925f > origin_y + 0.01f) dy = 0.01f;
    return dy;
}

// FUN_0045c490: +0x38c from the up-ray hit at `surf` over the start `o0`.
inline bool sand2_buried(const PlayerSand& s, float surf, float o0) {
    return s.on_sand2() && surf - o0 > 2.25f;
}

}  // namespace cleria
