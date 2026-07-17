// csprite — the MENU system's drawing primitive (`csprite.cpp`, 0xAC bytes).
//
// The menus are built entirely out of these; the in-game HUD deliberately is NOT (it
// writes raw XYZRHW quads instead). One `csprite` = one textured quad with its own
// colour, UV, texture and per-axis anchoring.
//
// Reversed from `FUN_005a43a0(csprite* /*ECX*/)`, the per-sprite draw. See
// build/menu_re/.
//
// ** +0x08/+0x0C ARE W,H — NOT x1,y1. ** Proven by the draw itself:
//     005a43d8  MOVSS XMM1,[EDI+0x8]   ; w
//     005a43e2  ADDSS XMM1,XMM3        ; x1 = x + w
// Reading them as a second corner would put every sprite in the wrong place.
//
// The menu does NOT share the HUD's 0x1C vertex: it emits a **0x20 vertex WITH
// SPECULAR**, in TL/BL/TR/BR strip order, through **DrawPrimitiveUP** (vtbl +0x144) —
// no shared VB/IB. It does reuse the same AdjustXY(4, pts, 0x20) as the HUD.
//
// A screen's sprites live in a contiguous `.data` block walked at `+= 0xAC`, and a
// sprite is VISIBLE only when the ALPHA BYTE of its diffuse is non-zero. Layout is
// hardcoded per screen in code (every rect is a float immediate) — there is no table
// of rects to read. Only list CONTENT is data-driven.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cleria {

// Per-axis anchoring, fed to MapCoord(axis, mode). The HUD only ever uses mode 1
// (left/top-anchored); modes 2 and 3 are used EXCLUSIVELY by the menus (58 and 14
// call sites, all in the window TU) — mode 2 is centre-anchored.
enum : std::uint32_t {
    kAnchorNone = 0,        // don't map this axis at all
    kAnchorStart = 1,       // left / top  (the HUD's only mode)
    kAnchorCentre = 2,      // menus
    kAnchorEnd = 3,         // menus
    kAnchorSentinel = 0x23d,  // skips BOTH axes
};

// The engine's 0xAC record, field-for-field. Offsets are the disassembled ones so a
// `.data` block can be reinterpreted directly.
struct CSprite {
    float dst[4];            // +0x00  {x, y, W, H} — default {1280,960,1,1}: parked off-screen
    float dst2[4];           // +0x10  a second copy, always written identically [UNKNOWN]
    std::uint8_t pad18[0x28 - 0x20];
    std::uint32_t diffuse;   // +0x28  ARGB, default 0xFFFFFFFF. **Alpha gates visibility.**
    std::uint32_t specular;  // +0x2C  ARGB, default 0
    std::uint8_t pad30[0x50 - 0x30];
    float uv[4];             // +0x50  {u0, v0, u1, v1} — default {0,0,1,1} = whole texture
    std::uint32_t texture;   // +0x60  IDirect3DTexture9* -> SetTexture (vtbl +0x104)
    std::uint32_t render_state;  // +0x64  -> FUN_005eb5b0
    std::uint8_t pad68[0xA4 - 0x68];  // +0x68 nested sub-record [UNKNOWN]
    std::uint32_t x_anchor;  // +0xA4  MapCoord(axis=0, mode=this)
    std::uint32_t y_anchor;  // +0xA8  MapCoord(axis=1, mode=this)

    bool visible() const { return (diffuse >> 24) != 0; }
    float x() const { return dst[0]; }
    float y() const { return dst[1]; }
    float w() const { return dst[2]; }
    float h() const { return dst[3]; }
    // The engine's own x1/y1: x + w, y + h.
    float x1() const { return dst[0] + dst[2]; }
    float y1() const { return dst[1] + dst[3]; }
};
static_assert(sizeof(CSprite) == 0xAC, "csprite must match the engine's 0xAC stride");
static_assert(offsetof(CSprite, diffuse) == 0x28, "diffuse @0x28");
static_assert(offsetof(CSprite, uv) == 0x50, "uv @0x50");
static_assert(offsetof(CSprite, texture) == 0x60, "texture @0x60");
static_assert(offsetof(CSprite, x_anchor) == 0xA4, "xAnchor @0xA4");

}  // namespace cleria
