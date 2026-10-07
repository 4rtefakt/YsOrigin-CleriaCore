// The window text's icon font: data\image\extfont{20,25,32,36,40}.bmp — the glyphs of <ext:n>
// and of the Roo alphabet (<roo>). build/audit/PLAYTEST_1_STORY.md §2, WNDDIALOG_DRAW.md §6.2.
//
// FUN_005ef460 (the text-surface ctor) loads the file once: an 8-bit BMP of 52 square cells
// stacked vertically (N x 52N, N = 32 for the 32-unit cell CleriaCore's window text uses: table
// 0x6b94c0 = {20, 25, 32, 36, 40}, index 2 outside HighResoText's screen-size pick), each texel a
// 0..16 coverage like GDI's GGO_GRAY4. Per cell it computes two signed bytes at 0x746c00:
//   [0] left = 1 - (the first column holding a texel)   (1 for an empty cell)
//   [1] right = the last column holding a texel + 1      (N for an empty cell)
// The raster (FUN_005f03f0, ext branch) blits the whole N x N cell at (pen x + left, line top
// + the descriptor's y offset) — no baseline, no <scale> — and advances the pen by left + right;
// the measure (FUN_005efb50 @LAB_005f0145) advances the same.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace cleria {

struct ExtFont {
    static constexpr int kCells = 52;
    int cell = 0;                                  // N
    std::vector<std::uint8_t> cov;                 // kCells * N * N coverage, cell-major, rows top-down
    std::array<std::int8_t, kCells> left{}, right{};
    bool ok() const { return cell > 0; }
    float advance(int i) const { return (i >= 0 && i < kCells) ? float(left[std::size_t(i)] + right[std::size_t(i)]) : 0.0f; }
    // The file through vfs::read. An empty font on failure.
    static ExtFont load(const std::filesystem::path& bmp);
    // From already-read file bytes (tests).
    static ExtFont parse(const std::vector<std::uint8_t>& bytes);
};

// The Roo alphabet (FUN_005f03f0 / FUN_005efb50 case 0xe after <roo>, tag 15): the extfont cell for
// character `c`, or -1 when `c` stays a normal glyph. A-Z -> c - 0x35, a-z -> c - 0x55 (both 12..37)
// unless EnglishRoonic (0x76519d) is set; '*' 1, '@' 2, '$' 3, '^' 4, '{' 5, '}' 6, '#' 7 always.
int roo_glyph(unsigned char c, bool english_roonic);

}  // namespace cleria
