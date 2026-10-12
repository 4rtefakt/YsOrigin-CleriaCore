// GoPopup: the floating numbers and labels over pickups and the player (build/item_re/PICKUPS.md
// §13: setup FUN_00564da0, tick FUN_00565170, draw FUN_00565320). The text is laid out in the
// F_GEO20 bitmap font (data\image\f_geo20.dds, 512x128, a dark rim baked in), centred on the
// anchor and hanging below it; each glyph rises 1.0 unit over 16 ticks (sin ease-out), two ticks
// behind the previous one, and rises again (2 - cos) before the 8-tick fade-out. It is
// world-positioned and camera-facing, with no depth test.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

struct PopupGlyph {
    float x0, x1, top, bottom;               // billboard-local units (x right, y up), before the rise
    float u0, v0, u1, v1;                    // F_GEO20
};

struct Popup {
    Vec3 pos;                                // +0x50: the anchor (never moves)
    std::uint32_t colour = 0xFFFFFF;         // +0x128 (RGB)
    float alpha = 0;                         // +0x12c
    int age = 0;                             // +0x124
    int n = 0;                               // +0x154: triangles = 2 x glyphs
    std::vector<PopupGlyph> glyphs;
    bool dead = false;

    // FUN_00565320: glyph i's vertical rise this tick.
    float rise(int i) const;
    std::uint32_t diffuse() const;           // pack(alpha) << 24 | colour
};

// One F_GEO20 glyph as FUN_004535d0 lays it out at scale 1 (the font object 0x731460: 24-px cells in 21 columns of
// f_geo20.dds, the widths 0x69b1f0): the quad is `adv` x 23 and the pen moves on by adv - 2. False for a char
// with no glyph (a space or a tab moves the pen by 12).
struct GeoGlyph { float adv = 0, u0 = 0, v0 = 0, u1 = 0, v1 = 0; };
bool geo20_glyph(unsigned char c, GeoGlyph* g);

// FUN_00564da0: lay `text` out (31 chars at most) at `scale` (gold 0.75, heals 1.0, the seeds'
// "BOOST MAX" / "OXYGEN MAX" 0.65).
Popup make_popup(const std::string& text, std::uint32_t colour, float scale, const Vec3& pos);
// FUN_00565170: one tick (age, alpha, death at age n + 43).
void step_popup(Popup& p);

}  // namespace cleria
