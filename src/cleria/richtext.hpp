// The engine's window text markup and layout: build/event_re/WNDDIALOG_DRAW.md §6.2-§6.4
// (tag table 0x68fdc0, parser FUN_005efab0, measure FUN_005efb50, raster FUN_005f03f0).
//
// `<name[:value[,value2]]>` tags, case-insensitive prefixes (any tag starting with x / y is
// x / y): x, y, color, rot, scale, stpos, ldpos, code, font, ruby, endruby, count, nocount,
// ext, roo. Only LF breaks a line (pen x = 0, y += line height + extra); there is no wrapping.
// A space at the start of a line puts the pen at 10, else advances by the space width. Every
// visible glyph takes the next counted index unless inside <nocount> or ruby; the typewriter
// shows index < reveal fully and fades in index == reveal.
//
// Glyph metrics come from the host (GDI Felghana at a 32-unit cell), so the layout is exact
// for whatever font the host rasterises. [not modelled] <rot>, <font>, ruby text and the
// extfont icons of <ext> / <roo> (laid out as a box of the extfont cell width, not drawn).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace cleria {

struct RichMetrics {
    float height = 32;                       // tmHeight
    float ascent = 26;                       // tmAscent
    float space = 10;                        // the space advance at scale 1
    std::function<float(unsigned char)> advance;   // gmCellIncX at scale 1
};

struct RichGlyph {
    unsigned char c = 0;
    float x = 0, y = 0;                      // the pen (line top) in text units, before the offset
    float scale = 1;
    std::uint32_t colour = 0xFFFFFFFF;
    int index = 0;                           // the counted index (uncounted glyphs share the next)
    bool ext = false;                        // an extfont icon (<ext> / <roo>): [not drawn]
};

struct RichLayout {
    std::vector<RichGlyph> glyphs;
    float width = 0, height = 0;             // max pen x; last line top + line height
    int counted = 0;                         // counted glyphs (the reveal end, <<16 in the engine)
    int lines = 1;
};

// FUN_005efb50 / FUN_005f03f0: lay out `text` (already unescaped, wndtext.hpp).
RichLayout layout_rich(std::string_view text, const RichMetrics& m, float line_extra,
                       std::uint32_t default_colour = 0xFFFFFFFF);

// The reveal alpha of a glyph for the 16.16 counter `reveal` (§6.4): 1 before, the fraction at
// the boundary, 0 after.
float reveal_alpha(const RichGlyph& g, std::uint32_t reveal);

// Parse one tag value (FUN_005cbe40): sign, 0x hex, 0b binary, decimal; a '.' makes it a
// float. Returns the value as a float.
float rich_value(std::string_view v);

}  // namespace cleria
