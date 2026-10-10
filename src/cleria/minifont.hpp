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
// The font is PROPORTIONAL — barely. The advance comes from an int8 table in the exe
// at `.data 0x0068ace0` (224 entries, ending 0x0068adc0): the quad is drawn
// `width[idx] + 1` wide (an unconditional `INC EAX` at 0x00452ace) and the PEN ADVANCES BY
// `width[idx]` (that width - 1.0, 0x452e5e..0x452e7e; corrected 2026-10-10: it was read as
// width + 1). Default 9 => a 10px quad inside the 12px cell, the next glyph 9 further.
//
// In practice it is effectively MONOSPACE: only 10 of 224 entries differ from 9 —
// '('=8 '.'=5 ':'=8 ';'=8 'T'=8 '['=5 '_'=6 'o'=8 'p'=8 '~'=6. Note ']' is 9 while
// '[' is 5, so the set is ad-hoc; use the table verbatim rather than inferring a rule.
//
// INDEXING IS NOT SIMPLY `c - 0x20`: `c <= 0x7F -> c - 0x20`, but `c > 0x7F ->
// c - 0x27` (byte-verified), which is why the table runs to 224 (max idx 216). It is
// a char*, not wide. The two ranges OVERLAP (c=0x79..0x7F and c=0x80..0x86 both land
// on idx 0x59..0x5F) — reported as disassembled, unexplained, and it needs a runtime
// check before trusting high characters.
//
// Space AND tab do NOT come from the table: both hardcode `pen_x += 9.0` (9, not 10).
// Newline does `pen_y += 14.0` and resets pen_x to the STRING'S ORIGIN X, not to 0.
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

// The exe's table at .data 0x0068ace0, verbatim. Entries past the dumped 160 are the
// high/accented range and default to 9, the table's overwhelming norm (150 of 160).
inline constexpr std::int8_t kMiniWidths[224] = {
     9,  9,  9,  9,  9,  9,  9,  9,  8,  9,  9,  9,  9,  9,  5,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  8,  8,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  8,  9,  9,  9,  9,  9,  9,  5,  9,  9,  9,  6,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  8,
     8,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  6,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
     9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,  9,
};

class MiniFont {
  public:
    static constexpr int kCellW = 12, kCellH = 14, kPerRow = 16, kFirst = 0x20;
    // Hardcoded in the drawer, not read from the table (0x6bd900 = 9.0).
    static constexpr int kSpaceAdvance = 9;
    static constexpr float kLineHeight = 14.0f;

    // `widths` is the exe's int8 table at 0x0068ace0, first entry = ' ' (0x20).
    // Pass an empty span to leave advances at 0 — callers must then not lay out text.
    bool load(const std::filesystem::path& dds_path, const std::int8_t* widths,
              std::size_t width_count);

    bool ok() const { return w_ > 0 && !rgba_.empty(); }
    bool has_widths() const { return have_widths_; }
    int width() const { return w_; }
    int height() const { return h_; }
    const std::vector<std::uint8_t>& rgba() const { return rgba_; }  // RGBA8, w*h*4

    // The exe's index mapping (see the note above -- it is not just c - 0x20).
    static int index(char c);
    // The cell + advance for a character.
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
