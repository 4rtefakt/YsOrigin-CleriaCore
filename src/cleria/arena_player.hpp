// The player's arena fence, mode 2 (g_flags[190] == 2): the player AI tick FUN_0046fba0, after
// its mode-1 box clamp, at 0x4700aa..0x4701f2 [BYTES, tools/exe_peek.py]. S_1000's opening
// fights (BATTLESTART1/2/A, BATTLE_0/1/2) are the only mode-2 users in the corpus; they pair
// 0x64(190, 2) with EnclosePlayer(0, 0, -1.3, 14). build/ai_spec/ARENA_AND_BOSSBAR.md §1.5c.
//
// The test is 2-D in XZ: centre (a5cc, a5d0) = (rect[0], rect[1]), radius a5d8 = rect[3];
// a5d4 (rect[2]) is unused here (the enemy readers use it as the sphere's z). It does NOT clamp:
//   inside  (|c - p|^2 <= r^2, the length squared again after sqrt): last (+0x3e0..0x3e8) = p
//   outside: n = normalize(c - p) (FUN_0042cc50, 0 when |c - p| == 0),
//            k = ((p - last) . n) * -2.0 (0x6be098), the y term multiplied by 0.0 (0x6bcf94),
//            knock channel 1 of the player (+0x338..0x340) += n * k * 0.1 (0x6bd19c).
// So the push grows with the depth past the last inside point: a soft spring wall the knock
// channel's own damping (hit.hpp step_knockback_player) settles.
#pragma once

#include <cmath>

namespace cleria {

struct ArenaPush { float x = 0, z = 0; };

// p / last are (x, z); `last` is updated in place. Returns the impulse to add to knock channel 1.
inline ArenaPush player_arena_ring(float px, float pz, float& last_x, float& last_z, const float rect[4]) {
    const float dx = rect[0] - px, dz = rect[1] - pz;
    const float d = std::sqrt(dx * dx + 0.0f + dz * dz);              // FUN_0041f570
    if (d * d <= rect[3] * rect[3]) { last_x = px; last_z = pz; return {}; }
    float nx = 0, nz = 0;
    if (d != 0.0f) { const float inv = 1.0f / d; nx = dx * inv; nz = dz * inv; }   // FUN_0042cc50
    const float k = ((px - last_x) * nx + (pz - last_z) * nz) * -2.0f;
    return {nx * k * 0.1f, nz * k * 0.1f};
}

}  // namespace cleria
