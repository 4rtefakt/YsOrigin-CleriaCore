#include "cleria/minifont.hpp"

#include <cstring>
#include <fstream>

namespace cleria {
namespace fs = std::filesystem;

bool MiniFont::load(const fs::path& p, const std::int8_t* widths, std::size_t n) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>()};
    if (b.size() < 128 || std::memcmp(b.data(), "DDS ", 4) != 0) return false;

    auto rd32 = [&](std::size_t o) {
        std::uint32_t v;
        std::memcpy(&v, b.data() + o, 4);
        return v;
    };
    h_ = int(rd32(12));
    w_ = int(rd32(16));
    const std::uint32_t bits = rd32(4 + 84);
    // A4R4G4B4 only — that is what the shipped f_mini is. dds.cpp's generic 16/24/32
    // path would also handle it, but this font is the one texture whose alpha we
    // cannot afford to get wrong (it is pure alpha coverage over white-ish ink), so
    // decode it explicitly rather than route it through the mask-guessing path.
    if (bits != 16 || w_ <= 0 || h_ <= 0) { w_ = h_ = 0; return false; }

    const std::size_t need = std::size_t(w_) * h_ * 2;
    if (b.size() < 128 + need) { w_ = h_ = 0; return false; }
    rgba_.resize(std::size_t(w_) * h_ * 4);
    for (std::size_t i = 0; i < std::size_t(w_) * h_; ++i) {
        const std::uint16_t v = std::uint16_t(b[128 + i * 2] | (b[128 + i * 2 + 1] << 8));
        std::uint8_t* d = &rgba_[i * 4];
        d[0] = std::uint8_t(((v >> 8) & 0xF) * 17);   // R
        d[1] = std::uint8_t(((v >> 4) & 0xF) * 17);   // G
        d[2] = std::uint8_t((v & 0xF) * 17);          // B
        d[3] = std::uint8_t(((v >> 12) & 0xF) * 17);  // A
    }

    widths_.clear();
    have_widths_ = false;
    if (widths && n) {
        widths_.assign(widths, widths + n);
        have_widths_ = true;
    }
    return true;
}

MiniGlyph MiniFont::glyph(char c) const {
    int idx = int(static_cast<unsigned char>(c)) - kFirst;
    if (idx < 0) idx = 0;  // control chars -> space
    MiniGlyph g;
    g.cell_x = (idx % kPerRow) * kCellW;
    g.cell_y = (idx / kPerRow) * kCellH;
    g.advance = (have_widths_ && std::size_t(idx) < widths_.size()) ? widths_[std::size_t(idx)] : 0;
    return g;
}

int MiniFont::measure(std::string_view s) const {
    if (!have_widths_) return -1;
    int x = 0;
    for (char c : s) x += glyph(c).advance;
    return x;
}

}  // namespace cleria
