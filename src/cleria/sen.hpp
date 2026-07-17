// SEN parser — scene exit / warp triggers. See docs/formats/SIDECARS.md.
// Each record is a box that warps the player to another scene when entered.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

// A SEN record. build/warp_re/WARP.md [C-DIS]: op 0x9f's loader turns each record into
// a RegionEntry the warp system tests the player against; on entry it copies this
// record's arrival block into the warp descriptor and loads dest_scene.
//
// Offsets cross-checked against all 197 SEN files / 367 records:
//   +0x104 centre · +0x110/+0x11c corners · +0x128 dest dir · +0x22c ARRIVAL pos
//   +0x240 sub-area (0 in every record — dead, like the SCT area filter)
//   +0x244 kill-actors-on-exit (set in only 5 records)
struct SenTrigger {
    std::string name;       // region name
    std::string dest_scene; // leaf scene id, e.g. "S_1000"
    Vec3 position;          // trigger centre, world metres
    Vec3 box_lo, box_hi;    // world-space AABB (position + local min/max)

    // Where the player arrives in the DESTINATION room (+0x22c). Non-zero in 281/367
    // records, and they land on the reciprocal door — the engine (FUN_00434020)
    // overrides the destination's SCT default spawn with this when it is set. Zero
    // means "use the destination's own spawn".
    Vec3 arrival_pos{};
    bool has_arrival() const {
        return arrival_pos.x != 0 || arrival_pos.y != 0 || arrival_pos.z != 0;
    }

    // ** Arrival FACING is NOT wired: it does not verify. ** WARP.md claimed clean
    // degrees at +0x238, but +0x238 is 0 in all 367 records (and +0x23c, which this
    // parser once mislabelled as the facing, is 0/NaN). So shipped SEN carries no
    // usable arrival facing; the player keeps its current/default heading. Left out
    // rather than storing a field that is always 0.

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
