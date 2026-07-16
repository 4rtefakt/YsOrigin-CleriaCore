#include "cleria/sfo.hpp"

#include <algorithm>
#include <cstring>

namespace cleria {
namespace {

float rdf(const std::uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}
std::uint32_t rd32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
Vec3 rdv(const std::uint8_t* p) { return {rdf(p), rdf(p + 4), rdf(p + 8)}; }

}  // namespace

Sfo Sfo::parse(const std::vector<std::uint8_t>& b) {
    Sfo out;
    // Shared sidecar container: magic[4], u32, u32 stride, u32 count, records[].
    if (b.size() < 16 || std::memcmp(b.data(), "SFO\0", 4) != 0) return out;
    const std::uint32_t stride = rd32(&b[8]);
    std::uint32_t count = rd32(&b[12]);
    if (stride < 0x34) return out;
    if (count > 16) count = 16;  // the engine clamps to 16 regions
    if (16 + std::size_t(count) * stride > b.size()) return out;

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* r = &b[16 + std::size_t(i) * stride];
        FogRegion f;
        f.origin = rdv(r);
        const Vec3 a = rdv(r + 0x0C), c = rdv(r + 0x18);
        // The loader sorts the two corners into min/max — don't assume an order.
        f.aabb_min = {std::min(a.x, c.x), std::min(a.y, c.y), std::min(a.z, c.z)};
        f.aabb_max = {std::max(a.x, c.x), std::max(a.y, c.y), std::max(a.z, c.z)};
        f.fog_near = rdf(r + 0x24);
        f.fog_far = rdf(r + 0x28);
        // Colour bytes are B,G,R,A in memory (NOT RGBA).
        f.b = r[0x2C] / 255.0f;
        f.g = r[0x2D] / 255.0f;
        f.r = r[0x2E] / 255.0f;
        f.sticky = rd32(r + 0x30) != 0;
        out.regions.push_back(f);
    }
    return out;
}

const FogRegion* Sfo::pick(const Vec3& p) const {
    if (regions.empty()) return nullptr;
    for (std::size_t i = 1; i < regions.size(); ++i)
        if (regions[i].contains(p)) return &regions[i];
    return &regions[0];  // room-wide default
}

}  // namespace cleria
