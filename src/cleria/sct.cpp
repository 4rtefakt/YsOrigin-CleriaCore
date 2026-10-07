#include "cleria/sct.hpp"

#include <cmath>
#include <cstring>

namespace cleria {
namespace {

constexpr float kDegToRad = 0.0174532924f;

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

std::string CastPlacement::sprite_stem() const {
    if (name.rfind("CAST_M", 0) != 0) return {};
    return "M_" + name.substr(6);
}

Sct Sct::parse(const std::vector<std::uint8_t>& b) {
    Sct out;
    // Shared sidecar container: magic[4], u32, u32 stride, u32 count, records[].
    if (b.size() < 16 || std::memcmp(b.data(), "SCT\0", 4) != 0) return out;
    const std::uint32_t stride = rd32(&b[8]);
    const std::uint32_t count = rd32(&b[12]);
    if (stride < 0x118) return out;
    if (16 + std::size_t(count) * stride > b.size()) return out;

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* r = &b[16 + std::size_t(i) * stride];
        CastPlacement c;
        // Read to the first NUL: the US build memcpys English names over longer
        // Shift-JIS ones without clearing, leaving trailing garbage past the NUL.
        const char* nm = reinterpret_cast<const char*>(r);
        c.name.assign(nm, ::strnlen(nm, 0x104));
        // +0x108 is an INT of degrees: the room load passes it in ecx to FUN_00443a80
        // (`mov ecx,[edi]`, edi = record + 0x108, 0x4342e1). Read as a float, 180 was a
        // denormal and those placements faced 0 (PLAYTEST_1.md §2). -1 / 360 take that
        // function's "random" path, which always lands on heading 0 = yaw 90 (chr_state.hpp
        // falcom_heading) [not modelled: its game-RNG draw].
        const std::int32_t ydeg = std::int32_t(rd32(r + 0x108));
        c.random_yaw = (ydeg == -1 || ydeg == 360);
        c.yaw = c.random_yaw ? 90.0f * kDegToRad : float(ydeg) * kDegToRad;
        c.position = {rdf(r + 0x10C), rdf(r + 0x110), rdf(r + 0x114)};
        out.casts.push_back(std::move(c));
    }
    return out;
}

const CastPlacement* Sct::player_spawn() const {
    for (const CastPlacement& c : casts)
        if (c.is_player()) return &c;
    return nullptr;
}

}  // namespace cleria
