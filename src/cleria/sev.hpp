// The map's `.SEV` "scene-event" file: a table of named oriented trigger boxes. The
// XSO event VM's op 0x13d Chr_IsInEventBox(entity, name) tests an actor's world position
// against one of these — the predicate the region-talk controllers (Stop*_Roi_*) poll
// before firing a TalkCxxx. Geometry + math are code-grounded in
// build/event_re/OP_13D_REGION.md; the on-disk layout in build/event_re/SEV_FORMAT.md.
#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

// A named oriented box. The local AABB [amin, amax] is offset from `center`; the whole
// box is rotated `rot_y` radians about +Y. (min/max need not be symmetric about center.)
struct SceneBox {
    std::string name;
    Vec3 center{0, 0, 0};
    Vec3 amin{0, 0, 0};   // local AABB min corner
    Vec3 amax{0, 0, 0};   // local AABB max corner
    float rot_y = 0.0f;   // radians about +Y (0 => axis-aligned)

    // OBB containment (OP_13D_REGION.md §3): delta = p - center; rotate the XZ delta by
    // +rot_y into the box's local frame (Y is never rotated); inclusive AABB test.
    // NOTE: the game quantizes the angle to a 256-step cosine LUT; this uses exact trig.
    // The difference is sub-degree — the one deliberately non-bit-exact detail, flagged
    // here rather than hidden. Boxes with rot_y == 0 take an exact axis-aligned path.
    bool contains(const Vec3& p) const;
};

// Parse a `.SEV` blob into its named boxes. Returns empty on a bad container.
std::vector<SceneBox> parse_sev(std::span<const std::uint8_t> bytes);

// Load the boxes for a room, given its `<room>.SEV` path. Empty if the file is absent.
std::vector<SceneBox> load_sev(const std::filesystem::path& sev_path);

// Case-insensitive name lookup (matches the game's __stricmp scan). Null if absent.
const SceneBox* find_box(const std::vector<SceneBox>& boxes, const std::string& name);

}  // namespace cleria
