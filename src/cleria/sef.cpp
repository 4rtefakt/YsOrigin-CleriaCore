#include "cleria/sef.hpp"

#include <cstring>

namespace cleria {
namespace {

constexpr float kDegToRad = 0.0174532924f;  // DAT_006bd0dc in the exe

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

}  // namespace

Sef Sef::parse(const std::vector<std::uint8_t>& b) {
    Sef out;
    // Shared sidecar container: magic[4], u32, u32 stride, u32 count, records[].
    if (b.size() < 16 || std::memcmp(b.data(), "SEF\0", 4) != 0) return out;
    const std::uint32_t stride = rd32(&b[8]);
    const std::uint32_t count = rd32(&b[12]);
    if (stride < 0x1C) return out;
    if (16 + std::size_t(count) * stride > b.size()) return out;

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* r = &b[16 + std::size_t(i) * stride];
        SefEffect e;
        e.id = rd32(r);
        e.position = {rdf(r + 0x04), rdf(r + 0x08), rdf(r + 0x0C)};
        // Stored in degrees; the loader multiplies by pi/180 before spawning.
        e.rotation = {rdf(r + 0x10) * kDegToRad, rdf(r + 0x14) * kDegToRad,
                      rdf(r + 0x18) * kDegToRad};
        out.effects.push_back(e);
    }
    return out;
}

}  // namespace cleria
