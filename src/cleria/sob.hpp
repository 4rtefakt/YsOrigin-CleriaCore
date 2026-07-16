// SOB parser — per-room object placement. See docs/formats/SIDECARS.md.
// Each record places a YMO model with a transform. The room's own geometry is
// just another record.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

struct SobObject {
    std::string ymo_path;   // e.g. "data\\map\\s_00\\s_0000\\s_0000.ymo"
    std::string name;       // "box_01", "save_01", ...
    Vec3 position;          // metres
    Vec3 rotation;          // radians, Euler XYZ
    Vec3 scale;             // usually (1,1,1)

    Mat4 world() const { return compose_trs(position, rotation, scale); }
};

struct Sob {
    std::vector<SobObject> objects;
    static Sob parse(std::span<const std::uint8_t> bytes);  // throws on bad data
};

}  // namespace cleria
