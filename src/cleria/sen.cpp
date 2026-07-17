#include "cleria/sen.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace cleria {

std::string scene_leaf(const std::string& path) {
    std::size_t end = path.size();
    while (end > 0 && (path[end - 1] == '\\' || path[end - 1] == '/')) --end;
    std::size_t start = end;
    while (start > 0 && path[start - 1] != '\\' && path[start - 1] != '/') --start;
    std::string leaf = path.substr(start, end - start);
    for (char& c : leaf) c = char(std::toupper((unsigned char)c));
    return leaf;
}

Sen Sen::parse(std::span<const std::uint8_t> b) {
    constexpr std::size_t kStride = 0x25C;
    Sen s;
    if (b.size() < 16 || std::memcmp(b.data(), "SEN\0", 4) != 0) return s;
    std::uint32_t stride, count;
    std::memcpy(&stride, b.data() + 8, 4);
    std::memcpy(&count, b.data() + 12, 4);
    if (stride != kStride || 16 + std::size_t(count) * kStride > b.size()) return s;

    auto vec3 = [&](std::size_t at) {
        Vec3 v; std::memcpy(&v, b.data() + at, sizeof(float) * 3); return v;
    };
    auto cstr = [&](std::size_t at, std::size_t cap) {
        const char* p = reinterpret_cast<const char*>(b.data() + at);
        std::size_t n = 0;
        while (n < cap && p[n] != '\0') ++n;
        return std::string(p, n);
    };

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t r = 16 + std::size_t(i) * kStride;
        SenTrigger t;
        t.name = cstr(r + 0x000, 0x104);
        t.position = vec3(r + 0x104);
        const Vec3 a = vec3(r + 0x110);  // aabb max corner (mostly +)
        const Vec3 c = vec3(r + 0x11C);  // aabb min corner (has negatives)
        t.box_lo = {t.position.x + std::min(a.x, c.x),
                    t.position.y + std::min(a.y, c.y),
                    t.position.z + std::min(a.z, c.z)};
        t.box_hi = {t.position.x + std::max(a.x, c.x),
                    t.position.y + std::max(a.y, c.y),
                    t.position.z + std::max(a.z, c.z)};
        t.dest_scene = scene_leaf(cstr(r + 0x128, 0x60));
        // The arrival position in the destination room (+0x22c). See sen.hpp: this
        // replaces the old +0x23c "angle" read, which was a mislabel — +0x23c is 0/NaN
        // across the corpus and carried no facing.
        t.arrival_pos = vec3(r + 0x22C);
        if (!t.dest_scene.empty()) s.triggers.push_back(std::move(t));
    }
    return s;
}

}  // namespace cleria
