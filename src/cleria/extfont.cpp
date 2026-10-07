// The window text's icon font. See extfont.hpp.
#include "cleria/extfont.hpp"

#include <cstring>

#include "cleria/vfs.hpp"

namespace cleria {

int roo_glyph(unsigned char c, bool english_roonic) {
    if (c >= 'A' && c <= 'Z' && !english_roonic) return int(c) - 0x35;
    if (c >= 'a' && c <= 'z' && !english_roonic) return int(c) - 0x55;
    switch (c) {
        case '@': return 2;
        case '#': return 7;
        case '$': return 3;
        case '^': return 4;
        case '*': return 1;
        case '{': return 5;
        case '}': return 6;
        default: return -1;
    }
}

ExtFont ExtFont::parse(const std::vector<std::uint8_t>& b) {
    ExtFont f;
    auto u16 = [&](std::size_t o) { return std::uint32_t(b[o]) | (std::uint32_t(b[o + 1]) << 8); };
    auto u32 = [&](std::size_t o) { return u16(o) | (u16(o + 2) << 16); };
    if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') return f;
    const std::uint32_t off = u32(10);
    const std::int32_t w = std::int32_t(u32(18)), h = std::int32_t(u32(22));
    if (u16(28) != 8 || u32(30) != 0 || w <= 0 || h == 0) return f;   // 8-bit, uncompressed
    const int n = w, rows = h < 0 ? -h : h;
    if (rows != n * kCells) return f;
    const std::size_t stride = (std::size_t(w) + 3) & ~std::size_t(3);
    if (off + stride * std::size_t(rows) > b.size()) return f;
    f.cell = n;
    f.cov.assign(std::size_t(kCells) * std::size_t(n) * std::size_t(n), 0);
    for (int y = 0; y < rows; ++y) {                       // a positive height is stored bottom-up
        const std::size_t src = off + stride * std::size_t(h > 0 ? rows - 1 - y : y);
        std::memcpy(&f.cov[std::size_t(y) * std::size_t(n)], &b[src], std::size_t(n));
    }
    for (int i = 0; i < kCells; ++i) {                     // FUN_005ef460's metric loops
        const std::uint8_t* c = &f.cov[std::size_t(i) * std::size_t(n) * std::size_t(n)];
        auto col_lit = [&](int x) {
            for (int y = 0; y < n; ++y) if (c[std::size_t(y) * std::size_t(n) + std::size_t(x)]) return true;
            return false;
        };
        int l = 1, r = n;
        for (int x = 0; x < n; ++x) if (col_lit(x)) { l = 1 - x; break; }
        for (int x = n - 1; x >= 0; --x) if (col_lit(x)) { r = x + 1; break; }
        f.left[std::size_t(i)] = std::int8_t(l);
        f.right[std::size_t(i)] = std::int8_t(r);
    }
    return f;
}

ExtFont ExtFont::load(const std::filesystem::path& bmp) { return parse(vfs::read(bmp)); }

}  // namespace cleria
