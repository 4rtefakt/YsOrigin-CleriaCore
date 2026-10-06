#include "cleria/sev.hpp"
#include "cleria/vfs.hpp"   // mods: asset opens go through vfs::resolve

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace cleria {

bool SceneBox::contains(const Vec3& p) const {
    const float dx = p.x - center.x;
    const float dy = p.y - center.y;
    const float dz = p.z - center.z;
    float lx, lz;
    if (rot_y == 0.0f) {                       // axis-aligned fast path (matches the exe)
        lx = dx;
        lz = dz;
    } else {                                    // rotate the XZ delta into the box frame
        const float c = std::cos(rot_y), s = std::sin(rot_y);
        lx = dx * c - dz * s;                   // [lx; lz] = R(+rot_y) . [dx; dz]
        lz = dz * c + dx * s;
    }
    const float ly = dy;                        // Y is never rotated
    return lx >= amin.x && lx <= amax.x &&
           ly >= amin.y && ly <= amax.y &&
           lz >= amin.z && lz <= amax.z;        // inclusive AABB (OP_13D_REGION.md §3)
}

const SceneBox* find_box(const std::vector<SceneBox>& boxes, const std::string& name) {
    for (const SceneBox& b : boxes) {
        if (b.name.size() != name.size()) continue;
        bool same = true;
        for (std::size_t i = 0; i < name.size(); ++i)
            if (std::tolower((unsigned char)b.name[i]) != std::tolower((unsigned char)name[i])) {
                same = false;
                break;
            }
        if (same) return &b;
    }
    return nullptr;
}

std::vector<SceneBox> parse_sev(std::span<const std::uint8_t> b) {
    // Header (16 bytes, SEV_FORMAT.md §1): "SEV\0", u32 reserved(=0), u32 record-size(=0x25c),
    // u32 count. Records start at +0x10, stride = record-size. Per record (§2), when the skip
    // flag int32@+0x240 == 0: name@+0x000, center@+0x104, two corners@+0x110/+0x11c (component-
    // wise sorted into min/max), and Y-rotation in INTEGER DEGREES@+0x23c (→ radians).
    std::vector<SceneBox> out;
    if (b.size() < 16 || std::memcmp(b.data(), "SEV\0", 4) != 0) return out;
    const std::uint8_t* d = b.data();
    auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, d + o, 4); return v; };
    auto i32 = [&](std::size_t o) { std::int32_t v; std::memcpy(&v, d + o, 4); return v; };
    auto f32 = [&](std::size_t o) { float v; std::memcpy(&v, d + o, 4); return v; };

    const std::uint32_t rec = u32(0x08);      // record size (0x25c in the shipped corpus)
    const std::uint32_t count = u32(0x0c);
    if (rec < 0x244) return out;              // must hold every field we read (skip flag @+0x240)

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t base = 0x10 + std::size_t(i) * rec;
        if (base + rec > b.size()) break;
        if (i32(base + 0x240) != 0) continue;  // record marked inactive -> not a live box

        SceneBox box;
        const char* np = reinterpret_cast<const char*>(d + base + 0x00);
        std::size_t nl = 0;
        while (nl < 255 && np[nl]) ++nl;       // name is a NUL-terminated 256-byte field
        box.name.assign(np, nl);

        box.center = {f32(base + 0x104), f32(base + 0x108), f32(base + 0x10c)};
        const Vec3 A = {f32(base + 0x110), f32(base + 0x114), f32(base + 0x118)};
        const Vec3 B = {f32(base + 0x11c), f32(base + 0x120), f32(base + 0x124)};
        box.amin = {std::min(A.x, B.x), std::min(A.y, B.y), std::min(A.z, B.z)};
        box.amax = {std::max(A.x, B.x), std::max(A.y, B.y), std::max(A.z, B.z)};

        std::int32_t deg = i32(base + 0x23c);  // on disk: integer degrees
        if (deg >= 180) deg -= 360;            // wrap to (-180, 180], as the exe does
        box.rot_y = float(deg) * 0.0174532925f;  // -> radians (pi/180)

        out.push_back(std::move(box));
    }
    return out;
}

std::vector<SceneBox> load_sev(const std::filesystem::path& sev_path) {
    std::ifstream f(vfs::resolve(sev_path), std::ios::binary);
    if (!f) return {};
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(f), {}};
    return parse_sev(bytes);
}

}  // namespace cleria
