#include "cleria/sev.hpp"

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
    // Header (16 bytes): "SEV\0", u32 pad(=0), u32 stride, u32 count; then `count` records
    // of `stride` bytes, each a named box. The per-record field offsets (name, center, local
    // AABB, Y-rotation) are being pinned down in build/event_re/SEV_FORMAT.md — until that
    // lands this returns no boxes, so op 0x13d reads false (no trigger). The containment math
    // (SceneBox::contains) is already code-grounded and independently tested.
    // TODO(SEV_FORMAT.md): fill each SceneBox from record fields once the layout is confirmed.
    std::vector<SceneBox> out;
    if (b.size() < 16 || std::memcmp(b.data(), "SEV\0", 4) != 0) return out;
    return out;
}

std::vector<SceneBox> load_sev(const std::filesystem::path& sev_path) {
    std::ifstream f(sev_path, std::ios::binary);
    if (!f) return {};
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(f), {}};
    return parse_sev(bytes);
}

}  // namespace cleria
