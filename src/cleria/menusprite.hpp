// The menu sprite at RUNTIME: the csprite (csprite.hpp has its raw 0xAC layout) as the book
// drives it: a textured quad with a tween target, colour targets and two pulses, ported from
// build/menu_re/PAUSEBOOK_DRAW.md §0 (the tweener FUN_005a3d00, the quad draw FUN_005a43a0).
// Pure state; a host executes the draw commands it emits.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "cleria/richtext.hpp"

namespace cleria {

// What a quad samples: a menu-table texture (data\menu\..., §0.6), an item icon
// (MENU/ICON/<stem>.DDS, 64²), an item's big icon (MENU/ICON2/<stem>V.DDS, 256²), or a file
// loaded by name (a Topic portrait or a manual page: `name`, a static string).
struct TexRef {
    // Face: a message-box face, f_(200 + 10*id), frame `aux` (the host resolves it).
    enum Kind : std::uint8_t { None, Menu, Icon, Icon2, Named, Face } kind = None;
    int id = 0;
    const char* name = nullptr;
    int aux = 0;
    bool operator==(const TexRef& o) const {
        return kind == o.kind && id == o.id && name == o.name && aux == o.aux;
    }
};

struct MenuSprite {
    float x = 1280, y = 960, w = 1, h = 1;          // +0x00: offscreen by default (.data init)
    float tx = 1280, ty = 960, tw = 1, th = 1;      // +0x10: tween target
    float pos_speed = 0, size_speed = 0;            // +0x20 / +0x24
    std::uint32_t diffuse = 0xFFFFFFFF;             // +0x28
    std::uint32_t specular = 0;                     // +0x2c (added to the textured colour)
    std::uint32_t diffuse_t = 0xFFFFFFFF;           // +0x30
    std::uint32_t specular_t = 0;                   // +0x34
    float col_speed = 0;                            // +0x38
    // +0x3d..: the specular pulse (grey 0..0xA0); +0x44 the specular saved when it starts
    bool spulse = false, spulse_dir = false; std::uint8_t spulse_cur = 0, spulse_step = 0;
    std::uint32_t saved_specular = 0;
    // +0x48..: the alpha pulse (alpha 0x20..0xFF over white)
    bool apulse = false, apulse_dir = false; std::uint8_t apulse_cur = 0, apulse_step = 0;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;           // +0x50
    TexRef tex;                                     // +0x60
    std::uint32_t flags = 1;                        // +0x64 render state (0x24041 / 0x20001 / ...)
    int anchor_x = 0, anchor_y = 0;                 // +0xa4 / +0xa8 (2 = the pillarboxed 4:3 field)

    void set_rect(float X, float Y, float W, float H) { x = tx = X; y = ty = Y; w = tw = W; h = th = H; }
    void set_target(float X, float Y, float speed) { tx = X; ty = Y; pos_speed = speed; }
    void set_colour(std::uint32_t d, std::uint32_t s) { diffuse = diffuse_t = d; specular = specular_t = s; }
    void fade_to(std::uint32_t d, std::uint32_t s, float speed) { diffuse_t = d; specular_t = s; col_speed = speed; }
    void set_uv(float a, float b, float c, float d) { u0 = a; v0 = b; u1 = c; v1 = d; }
    void pulse_specular_on(std::uint8_t step);      // save the specular, start the 0..0xA0 wave
    void pulse_specular_off();                      // stop, restore the saved specular
};

// FUN_005a3d00: one frame of the tween. True while position, size or colour still changed.
bool tween(MenuSprite& s);

// A draw command, in authored 1024x768 units, executed by the host in order.
struct DrawCmd {
    // Pillars: the side pillars in raw pixels (§2.1; tex.id = the 16:9 texture, +1 = 16:10).
    // Mesh: an indexed triangle list (the map's clouds), x scaled by min(W,H)/768, WRAP sampler.
    // TextRich: laid-out window text (richtext.hpp) at (x, y), alpha from `diffuse`, the
    // typewriter counter `reveal`, with the GDI shadow. Quad flags 0x40c1 = additive.
    enum Kind : std::uint8_t { Quad, TextMiddle, TextMini, TextGdi, Pillars, Mesh, TextRich } kind = Quad;
    // Quad
    TexRef tex;
    float x = 0, y = 0, w = 0, h = 0, u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    std::uint32_t diffuse = 0xFFFFFFFF, specular = 0, flags = 0x24041;
    int anchor_x = 2, anchor_y = 0;
    bool hgrad = false;                             // Quad: the two left vertices take diffuse_left (a raw vertex quad)
    std::uint32_t diffuse_left = 0;
    // Text (x, y = the top-left; colour = diffuse)
    std::string text;
    float line_extra = 0;                           // GDI: extra line spacing
    float scale = 1;                                // GDI: the "<scale:k>" markup
    // Mesh
    struct Vert { float x, y, u, v; std::uint32_t argb; };
    std::vector<Vert> verts;
    std::vector<std::uint16_t> indices;
    // TextRich
    std::vector<RichGlyph> rich;
    std::uint32_t reveal = 0xFFFFFFFFu;
};
using DrawList = std::vector<DrawCmd>;

// FUN_005a43a0: the quad of `s` (skipped when it has no visible pixel: alpha 0 and no specular).
void draw(DrawList& out, const MenuSprite& s);

// The bitmap fonts' metrics (PAUSEBOOK_DRAW.md §7.1 / §7.2), from the engine's width tables.
float text_width_middle(const std::string& s);   // FUN_004531d0 (with its c >= 0x84 quirk)
// FUN_00453480's "#>h" / "#<h" at s[i] == '#': the pen shift (one hex digit of pixels, signed) and i moved to
// the escape's last char; 0 and i untouched when it is not one. [not modelled] "#$" (the colour escapes).
float middle_shift(const std::string& s, std::size_t& i);
float text_width_mini(const std::string& s);     // FUN_00452ff0
// One glyph of F_MIDDLE / F_MINI: its quad size, UV and advance. False for an unknown char.
struct Glyph { float w = 0, h = 0, u0 = 0, v0 = 0, u1 = 0, v1 = 0, advance = 0; };
bool glyph_middle(unsigned char c, Glyph* g);
bool glyph_mini(unsigned char c, Glyph* g);

}  // namespace cleria
