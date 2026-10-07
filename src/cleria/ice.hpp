// Ice (surface 0x0b; S_4002/4003/4006/4007/4008/4011/4012/4080), read from the bytes 2026-10-08.
//
// * The player's velocity blend, FUN_00465170 0x468ed6..0x468f50 [EXPORT 00465170.c:2011-2030, the
//   constants from the bytes]: with +0x4b9 == 0x0b (or the global DAT_0076a5b0, which FUN_00589f60
//   arms [not modelled]) and not (accessory bit 0x76a624 & 0x4000 with DAT_0075ad90 + DAT_0075ad94 == 0
//   — that sum is taken as 0 [H]), the grounded blend factor is 0.35 (0x6bd2b0) instead of the snap,
//   with input and without; +0x4f0 = 60. Off the ice the 60-tick tail +0x4f0 only routes the player
//   through the blend with k = 1.0, i.e. the snap: no visible effect. Airborne the 0.12 / 0.05 apply.
// * Monsters, FUN_0045f4d0 0x45faef..0x45fb63 (MOVER.md §2.4, byte-read there): +0x4b9 == 0x0b and not
//   (+0x12c & 0x20000000 with the same sum 0): k = 0.35, +0x4f0 = 60; else while +0x4f0: --, k = 1.0;
//   either way THE GATE (grounded, not a flyer, Movable 0 before the step: no velocity write) is skipped.
//   FUN_0045c490 0x45d139: a grounded actor off the ice (+0x4b9 != 0x0b) zeroes +0x4f0.
// * The slope slide, FUN_0045a510 0x45b053..0x45b160: grounded, not a flyer, the floor ray's surface
//   0x0b; the player always (its 0x4000 test only reaches the 0x20000000 one), a monster unless
//   +0x12c & 0x20000000; a floor normal with x or z != 0: s = normalize(n.x, -n.y, n.z) (FUN_0042cc50),
//   knock channel 1 += (s.x, (1 - s.y)·0.1 + s.y, s.z) · 0.02 (0x6bd0ec, 0x6bd19c).
#pragma once
#include <cmath>
#include <cstdint>

#include "cleria/mathx.hpp"

namespace cleria {

inline constexpr int kSurfaceIce = 0x0b;
inline constexpr float kIceBlend = 0.35f;   // 0x6bd2b0

// The player's grounded ice blend applies (FUN_00465170 0x468f13).
inline bool player_ice(int surf_4b9, std::uint32_t equip_flags) {
    return surf_4b9 == kSurfaceIce && !(equip_flags & 0x4000u);
}

// 0x45b0da..0x45b160: the knock channel 1 push on an ice slope with floor normal n (zero when flat).
inline Vec3 ice_slope_push(const Vec3& n) {
    if (n.x == 0.0f && n.z == 0.0f) return {};
    Vec3 s{n.x, n.y * -1.0f, n.z};
    const float l = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
    if (l == 0.0f) return {};
    const float r = 1.0f / l;
    s = {s.x * r, s.y * r, s.z * r};
    return {s.x * 0.02f, ((1.0f - s.y) * 0.1f + s.y) * 0.02f, s.z * 0.02f};
}

}  // namespace cleria
