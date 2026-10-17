// SEN parser — scene exit / warp triggers. See docs/formats/SIDECARS.md.
// Each record is a box that warps the player to another scene when entered.
#pragma once
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cleria/cos_table.hpp"
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

    // Where the player arrives in the DESTINATION room (+0x22c). The engine
    // (FUN_00434020, WARP.md §3/§5) overrides the destination's SCT default spawn with
    // this whenever the record's LABEL (+0x00, our `name`) is non-empty -- NOT when the
    // arrival is non-zero. That distinction is load-bearing: 86/367 records carry a
    // (0,0,0) arrival that is a REAL position (world origin, which in these rooms sits
    // just inside a door), and every one of the 367 warp records has a label. Gating on
    // "arrival != 0" instead dropped those to the SCT default -- so returning through a
    // door with a zero arrival spawned you at the room's FIRST CAST_ADOL (the wrong
    // entrance in any multi-door room) rather than where you came in.
    Vec3 arrival_pos{};
    bool has_arrival() const { return !name.empty(); }

    // Arrival FACING (+0x238), in DEGREES. The earlier "always 0" note was wrong: the
    // corpus scan is 0×113, 90×41, 180×144, 270×47, 45/135/167/225/315 in the tail --
    // 254/367 records carry a real facing (matches WARP.md §1). ** 0 means UNSET **
    // (keep the default heading), as do the sentinels -1 and 360; has_facing() gates on
    // that. The engine converts via FUN_00443a80 = deg2rad(normalize360(90 - deg)) and
    // writes the radian to the arriving player; the view maps it to a sprite direction.
    // 2026-10-07 (PLAYTEST_1.md §2): the engine does NOT treat 0 as unset -- the SEN load
    // converts it unconditionally (0x433b80) and a labelled warp always places with it
    // (0x4340ad..0x4340e8); the view applies every arrival's facing. has_facing() is only
    // "a non-default value" now.
    int arrival_facing_deg = 0;
    bool has_facing() const {
        return arrival_facing_deg != 0 && arrival_facing_deg != -1 && arrival_facing_deg != 360;
    }

    // The volume YAW (+0x23c, degrees in the file): FUN_00433a10 wraps it (fmod 360, then -360 at
    // >= 180) and stores radians at RegionEntry+0x30. 27 of 367 records are turned boxes (S_2011's
    // door 2 at 135, S_2017's at 45 ...). box_lo / box_hi stay the UNROTATED world bounds.
    float yaw = 0.0f;

    // FUN_0042cf00's test [EXPORT 0042cf00.c]: p - centre; when +0x30 != 0 the offset turns
    // through the 256-entry cosine table 0x68f350 at i = (int)(yaw * 40.7437) (0x6bda38):
    // x' = x cos[i] - z cos[i - 64], z' = z cos[i] + x cos[i - 64]; then lo <= p' <= hi, inclusive (cos_table.hpp:
    // the shipped floats).
    bool contains(const Vec3& p) const {
        float dx = p.x - position.x, dz = p.z - position.z;
        const float dy = p.y - position.y;
        if (yaw != 0.0f) {
            const int i = int(yaw * 40.7436637878418f);
            const float c = cos_table(i & 0xff);
            const float s = cos_table((i - 0x40) & 0xff);
            const float rx = dx * c - dz * s, rz = dz * c + dx * s;
            dx = rx; dz = rz;
        }
        return dx >= lo.x && dx <= hi.x && dy >= lo.y && dy <= hi.y && dz >= lo.z && dz <= hi.z;
    }
    Vec3 lo, hi;            // the min / max corner relative to the centre (RegionEntry +0x08 / +0x14)
};

struct Sen {
    std::vector<SenTrigger> triggers;
    static Sen parse(std::span<const std::uint8_t> bytes);  // {} on bad data
};

// "S_10\\S_1000\\" -> "S_1000" (last non-empty path component, upper-cased).
std::string scene_leaf(const std::string& path);

}  // namespace cleria
