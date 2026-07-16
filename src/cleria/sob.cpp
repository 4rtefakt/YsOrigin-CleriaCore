#include "cleria/sob.hpp"

#include <cstring>
#include <stdexcept>

namespace cleria {

Sob Sob::parse(std::span<const std::uint8_t> b) {
    constexpr std::size_t kStride = 0x2A8;  // 680
    if (b.size() < 16 || std::memcmp(b.data(), "SOB\0", 4) != 0)
        throw std::runtime_error("SOB: bad magic");

    std::uint32_t stride, count;
    std::memcpy(&stride, b.data() + 8, 4);
    std::memcpy(&count, b.data() + 12, 4);
    if (stride != kStride)
        throw std::runtime_error("SOB: unexpected stride");
    if (16 + std::size_t(count) * kStride > b.size())
        throw std::runtime_error("SOB: truncated");

    auto vec3 = [&](std::size_t at) {
        Vec3 v;
        std::memcpy(&v, b.data() + at, sizeof(float) * 3);
        return v;
    };
    auto cstr = [&](std::size_t at, std::size_t cap) {
        const char* p = reinterpret_cast<const char*>(b.data() + at);
        std::size_t n = 0;
        while (n < cap && p[n] != '\0') ++n;
        return std::string(p, n);
    };

    Sob s;
    s.objects.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t r = 16 + std::size_t(i) * kStride;
        SobObject o;
        o.ymo_path = cstr(r + 0x000, 0x104);
        o.position = vec3(r + 0x104);
        o.rotation = vec3(r + 0x110);
        o.scale = vec3(r + 0x11C);
        o.name = cstr(r + 0x128, 0x20);
        s.objects.push_back(std::move(o));
    }
    return s;
}

}  // namespace cleria
