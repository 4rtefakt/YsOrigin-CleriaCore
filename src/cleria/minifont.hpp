// F_MINI — the game's small bitmap font (`data\image\f_mini.dds`).
//
// This is text system A: `FUN_00452980` draws strings from it, and it is what the
// HUD's `[EX]` / `[MP]` / `[SP]` labels are made of. The in-game HUD is otherwise
// quads from STATUS_512, which has NO letters at all — so anything alphabetic on the
// HUD comes from here, and the menus share it.
//
// Verified against the art (see build/hud_re/, tools):
//   - F_MINI.DDS is 256x256, **A4R4G4B4** (16-bit with a real alpha channel)
//   - cells are 12x14, **16 per row**, indexed `idx = c - 0x20`
//   - every glyph's ink starts at its cell's x=0 (only '1' starts at x=1) and
//     ' ' (idx 0) is empty — which confirms the grid
//   - the sheet holds ASCII 0x20..0x7E in six rows, then symbols and a full
//     accented-Latin set (the European localisation font)
//
// The font is PROPORTIONAL: the advance comes from an int8 table in the exe at
// `.data 0x0068ace0`, indexed the same way.
//
// **Do not substitute measured ink extents for the advances.** This codebase has been
// bitten three separate times by exactly that: the engine's source rects are full
// cells with transparent margins, and measuring opaque alpha under-reports them (it
// happened on STATUS_512's digit cells, the pointed gauge bars, and the bar housing).
// Ink is not advance.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace cleria {

struct MiniGlyph {
    int cell_x = 0, cell_y = 0;  // top-left of the 12x14 cell in the sheet
    int advance = 0;             // pen advance from the exe's width table
};

class MiniFont {
  public:
    static constexpr int kCellW = 12, kCellH = 14, kPerRow = 16, kFirst = 0x20;

    // `widths` is the exe's int8 table at 0x0068ace0, first entry = ' ' (0x20).
    // Pass an empty span to leave advances at 0 — callers must then not lay out text.
    bool load(const std::filesystem::path& dds_path, const std::int8_t* widths,
              std::size_t width_count);

    bool ok() const { return w_ > 0 && !rgba_.empty(); }
    bool has_widths() const { return have_widths_; }
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<std::uint8_t>& rgba() const { return rgba_; }  // RGBA8, w*h*4

    // The cell for a character. Out-of-range characters map to ' '.
    MiniGlyph glyph(char c) const;
    // Total advance of a string, or -1 if the width table isn't loaded.
    int measure(std::string_view s) const;

  private:
    int w_ = 0, h_ = 0;
    bool have_widths_ = false;
    std::vector<std::uint8_t> rgba_;
    std::vector<std::int8_t> widths_;
};

}  // namespace cleria
