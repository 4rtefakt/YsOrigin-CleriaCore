// The integrator's ledge recovery (FUN_0045a510 0x45b607..0x45b953; build/ai_spec/MOVER.md §3.5).
//
// After the tick's sweep, with no Fix held (0x75ad90 + 0x75ad94 == 0), +0x12c & 0x1000 clear and byte
// +0x500 clear [BYTES 0x45b607..0x45b62e]:
//   the floor ray of the tick (510 down from the PRE-move origin, the solid set) hit:
//       +0x4f1 = 0 ; +0x4f4.. = the new position - the tick's displacement D         0x45b908..
//   it missed (nothing under where the actor stood):
//       +0x4f1 += 1
//       the player with +0x4f1 > 4: position = +0x4f4.. ; +0x4f4.. = +0x51c.. ; +0x4f1 = 0    0x45b64c..0x45b693
//       otherwise, with d = normalize((+0x3c8 - x, 0, +0x3d0 - z)) (FUN_0042cc50; +0x3c8.. = the last position
//       that was grounded with a floor under it, 0x45b99b):
//           Q = (x + d.z * 0.05, y, z + d.x * 0.05)        [BYTES 0x45b6fa..0x45b772: the engine builds this
//                                                           point twice and rays both; z and x are crossed]
//           a floor within 25 under Q (the vector 0x7302dc = (0, -25, 0)): nothing more
//           else: for k = 0..4: P = pos + ((k + 2) / 20) * d ; a floor within 25 under P -> position = P
//                 (no early exit: the farthest such point wins)                      0x45b7e0..0x45b88d
//                 none of the five: a floor within 25 under (+0x3c8, y, +0x3d0) -> position = that
//                 [not modelled] the player's +0x77c = 2, +0x780 = max(+0x780, 2)     0x45b8de..
// The actor's +0x500 is the player mover's (FUN_00465170's tail [EXPORT 00465170.c:2547-2626]): set while the
// player's x / z is inside a fixed rectangle of three floor-5 rooms (ledge_exempt).
#pragma once
#include <cmath>
#include <functional>

#include "cleria/mathx.hpp"

namespace cleria {

struct LedgeState {
    int misses = 0;      // +0x4f1
    Vec3 good{};         // +0x4f4..: the last position reached from over a floor
    Vec3 entry{};        // +0x51c..: the scene-entry placement (FUN_00431840 / FUN_00459ff0)
    Vec3 safe{};         // +0x3c8..: the last grounded position with a floor under it
};

// Byte +0x500 for the player at (x, z) in scene `scene` (5005 = s_50\s_5005\) [BYTES the constants
// 0x6bdab0 / 0x6bdaf4 / 0x6be114 / 0x6be0dc, 0x6bdad8 / 0x6bdb18 / 0x6be134 / 0x6be124, 0x6bd938 / 0x6bda8c /
// 0x6be0bc / 0x6bd868, 0x6bd9c8 / 0x6bdacc / 0x6bcf94 / 0x6bda84].
inline bool ledge_exempt(int scene, float x, float z) {
    if (scene == 5005)
        return (70.0f <= x && x <= 98.0f && -42.0f <= z && z <= -11.0f) ||
               (85.0f <= x && x <= 110.0f && -72.0f <= z && z <= -56.0f);
    if (scene == 5007) return 11.0f <= x && x <= 58.0f && -5.0f <= z && z <= 5.0f;
    if (scene == 5012) return 26.0f <= x && x <= 81.0f && 0.0f <= z && z <= 57.0f;
    return false;
}

// One tick. `pos` = the swept position, `d_move` = the tick's displacement D, `floor_under_old` = the 510
// ray from the pre-move origin hit, `floor25(p)` = a floor within 25 under p. Returns the position to keep.
inline Vec3 ledge_recover(LedgeState& s, bool player, bool floor_under_old, const Vec3& pos, const Vec3& d_move,
                          const std::function<bool(const Vec3&)>& floor25) {
    if (floor_under_old) {
        s.misses = 0;
        s.good = pos - d_move;
        return pos;
    }
    s.misses = (s.misses + 1) & 0xff;
    if (player && s.misses > 4) {
        const Vec3 back = s.good;
        s.good = s.entry;
        s.misses = 0;
        return back;
    }
    Vec3 d{s.safe.x - pos.x, 0.0f, s.safe.z - pos.z};
    const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (l != 0.0f) d = {d.x / l, 0.0f, d.z / l};
    if (floor25({pos.x + d.z * 0.05f, pos.y, pos.z + d.x * 0.05f})) return pos;
    Vec3 out = pos;
    bool found = false;
    for (int k = 0; k < 5; ++k) {
        const float f = float(k + 2) / 20.0f;
        const Vec3 p{f * d.x + pos.x, f * d.y + pos.y, f * d.z + pos.z};
        if (floor25(p)) { found = true; out = p; }
    }
    if (!found) {
        const Vec3 back{s.safe.x, pos.y, s.safe.z};
        if (floor25(back)) out = back;
    }
    return out;
}

}  // namespace cleria
