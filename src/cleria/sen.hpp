// SEN parser — scene exit / warp triggers. See docs/formats/SIDECARS.md.
// Each record is a box that warps the player to another scene when entered.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

struct SenTrigger {
    std::string name;       // region name
    std::string dest_scene; // leaf scene id, e.g. "S_1000"
    Vec3 position;          // trigger centre, world metres
    Vec3 box_lo, box_hi;    // world-space AABB (position + local min/max)
    float angle_deg = 0;    // spawn facing at destination

    bool contains(const Vec3& p) const {
        return p.x >= box_lo.x && p.x <= box_hi.x &&
               p.y >= box_lo.y && p.y <= box_hi.y &&
               p.z >= box_lo.z && p.z <= box_hi.z;
    }
};

struct Sen {
    std::vector<SenTrigger> triggers;
    static Sen parse(std::span<const std::uint8_t> bytes);  // {} on bad data
};

// "S_10\\S_1000\\" -> "S_1000" (last non-empty path component, upper-cased).
std::string scene_leaf(const std::string& path);

}  // namespace cleria
