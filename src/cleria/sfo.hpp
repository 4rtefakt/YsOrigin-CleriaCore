// SFO — per-room scene fog regions.
//
// The engine has one "region" concept (a named, optionally-rotated AABB) that
// drives both fog (SFO) and cameras (SCM). Each frame the game tests the player
// against every region; the first hit wins, otherwise it falls back to record 0,
// the room-wide default. Region changes cross-fade rather than pop.
//
// Reverse-engineered from yso_win.exe: loader FUN_00433530, consumer FUN_0042e040.
// See docs/formats/SIDECARS.md.
#pragma once
#include <cstdint>
#include <filesystem>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

// One fog region. The AABB is stored RELATIVE to `origin`.
struct FogRegion {
    Vec3 origin{};
    Vec3 aabb_min{}, aabb_max{};   // relative to origin
    float fog_near = 0, fog_far = 0;
    float r = 0, g = 0, b = 0;     // 0..1
    bool sticky = false;

    bool contains(const Vec3& p) const {
        const Vec3 q = {p.x - origin.x, p.y - origin.y, p.z - origin.z};
        return q.x >= aabb_min.x && q.x <= aabb_max.x &&
               q.y >= aabb_min.y && q.y <= aabb_max.y &&
               q.z >= aabb_min.z && q.z <= aabb_max.z;
    }
};

struct Sfo {
    std::vector<FogRegion> regions;  // [0] = room-wide default; [1..] = sub-regions

    static Sfo parse(const std::vector<std::uint8_t>& bytes);
    bool ok() const { return !regions.empty(); }
    // The sub-region containing `p`, else the room default. Null if there's no data.
    const FogRegion* pick(const Vec3& p) const;
};

}  // namespace cleria
