// The menu sprite at runtime: build/menu_re/PAUSEBOOK_DRAW.md §0, §7. See menusprite.hpp.
#include "cleria/menusprite.hpp"

#include <cmath>

namespace cleria {

namespace {

// The bitmap fonts' width bytes (F_MIDDLE 0x69b258, 222 glyphs; F_MINI 0x68ace0), read from
// yso_win.exe v1.1.1.0.
constexpr std::uint8_t kMidW[222] = {
    16, 20, 16, 16, 16, 26, 20, 16, 16, 16, 16, 10, 16, 10, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    16, 10, 10, 18, 18, 18, 16, 24, 20, 20, 21, 22, 21, 20, 22, 24, 14, 18, 23, 22, 26, 21, 22, 20,
    23, 20, 19, 22, 23, 23, 27, 21, 22, 22, 16, 16, 16, 20, 16, 20, 16, 17, 16, 17, 14, 14, 16, 18,
    12, 15, 16, 12, 23, 17, 15, 17, 17, 15, 15, 14, 16, 17, 21, 17, 17, 17, 16, 16, 16, 16, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    24, 24, 24, 24, 24, 24, 24, 16, 20, 20, 20, 20, 20, 20, 24, 21, 21, 21, 21, 21, 14, 14, 14, 14,
    22, 21, 22, 22, 22, 22, 22, 24, 22, 23, 23, 23, 23, 22, 24, 24, 16, 16, 16, 16, 16, 16, 21, 16,
    14, 14, 14, 14, 12, 12, 12, 12, 24, 17, 15, 15, 15, 15, 15, 24, 15, 16, 16, 16, 16, 17, 24, 17,
    24, 24, 24, 24, 24, 24};
constexpr std::uint8_t kMiniW[224] = {
    9, 9, 9, 9, 9, 9, 9, 9, 8, 9, 9, 9, 9, 9, 5, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 8, 9, 9, 9, 9, 9, 9, 5, 9, 9, 9, 6, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 6, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9};

// One colour of the tween: D = sqrt(sum of squared channel deltas); snap when speed^2 >= D,
// else each channel steps by delta x speed / sqrt(D), truncated. The result packs blue = red
// (FUN_005a3d00 0x5a4046..0x5a407b; harmless for the grey / white targets the book uses).
bool tween_colour(std::uint32_t& c, std::uint32_t t, float speed) {
    if (c == t) return false;
    const int ca[4] = {int(c >> 24), int((c >> 16) & 0xff), int((c >> 8) & 0xff), int(c & 0xff)};
    const int ta[4] = {int(t >> 24), int((t >> 16) & 0xff), int((t >> 8) & 0xff), int(t & 0xff)};
    float sq = 0;
    for (int k = 0; k < 4; ++k) sq += float(ta[k] - ca[k]) * float(ta[k] - ca[k]);
    const float D = std::sqrt(sq);
    if (speed * speed >= D) { c = t; return true; }
    int n[4];
    const float k = speed / std::sqrt(D);
    for (int i = 0; i < 4; ++i) n[i] = int(float(ca[i]) + float(ta[i] - ca[i]) * k) & 0xff;
    c = (std::uint32_t(n[0]) << 24) | (std::uint32_t(n[1]) << 16) | (std::uint32_t(n[2]) << 8) | std::uint32_t(n[1]);
    return true;
}

bool tween_pair(float& x, float& y, float tx, float ty, float speed) {
    const float dx = tx - x, dy = ty - y;
    const float d2 = dx * dx + dy * dy;
    if (d2 == 0.0f) return false;
    if (speed * speed >= d2) { x = tx; y = ty; return true; }
    const float l = std::sqrt(d2);
    x += speed * dx / l; y += speed * dy / l;
    return true;
}

}  // namespace

void MenuSprite::pulse_specular_on(std::uint8_t step) {
    if (!spulse) saved_specular = specular;
    spulse = true; spulse_dir = true;
    spulse_cur = std::uint8_t(specular & 0xff);
    spulse_step = step;
}

void MenuSprite::pulse_specular_off() {
    if (spulse) specular = saved_specular;
    spulse = false;
}

bool tween(MenuSprite& s) {
    bool changed = false;
    changed |= tween_pair(s.x, s.y, s.tx, s.ty, s.pos_speed);
    changed |= tween_pair(s.w, s.h, s.tw, s.th, s.size_speed);
    if (s.apulse) {                                        // alpha 0x20..0xFF over white
        int v = s.apulse_cur;
        if (!s.apulse_dir) { if (v - 0x20 < s.apulse_step) { v = 0x20; s.apulse_dir = true; } else v -= s.apulse_step; }
        else { if (0xFF - v < s.apulse_step) { v = 0xFF; s.apulse_dir = false; } else v += s.apulse_step; }
        s.apulse_cur = std::uint8_t(v);
        s.diffuse = (std::uint32_t(v) << 24) | 0xFFFFFFu;
    } else {
        changed |= tween_colour(s.diffuse, s.diffuse_t, s.col_speed);
    }
    if (s.spulse) {                                        // grey 0..0xA0
        int v = s.spulse_cur;
        if (!s.spulse_dir) { if (v < s.spulse_step) { v = 0; s.spulse_dir = true; } else v -= s.spulse_step; }
        else { if (0xA0 - v < s.spulse_step) { v = 0xA0; s.spulse_dir = false; } else v += s.spulse_step; }
        s.spulse_cur = std::uint8_t(v);
        s.specular = (std::uint32_t(v) << 16) | (std::uint32_t(v) << 8) | std::uint32_t(v);
    } else {
        changed |= tween_colour(s.specular, s.specular_t, s.col_speed);
    }
    return changed;
}

void draw(DrawList& out, const MenuSprite& s) {
    if ((s.diffuse >> 24) == 0) return;                    // alpha gates visibility
    DrawCmd c;
    c.kind = DrawCmd::Quad;
    c.tex = s.tex;
    c.x = s.x; c.y = s.y; c.w = s.w; c.h = s.h;
    c.u0 = s.u0; c.v0 = s.v0; c.u1 = s.u1; c.v1 = s.v1;
    c.diffuse = s.diffuse; c.specular = s.specular; c.flags = s.flags;
    c.anchor_x = s.anchor_x; c.anchor_y = s.anchor_y;
    out.push_back(std::move(c));
}

bool glyph_middle(unsigned char c, Glyph* g) {
    int i;
    if (c < 0x21) return false;
    if (c < 0x84) i = c - 0x21;
    else if (c < 0xFF) i = c - 0x28;
    else return false;
    if (i < 0 || i >= 222) return false;
    const float w = float(kMidW[i]);
    // FUN_00442260 0x7302f8 loop: the counter n runs 1..222, so glyph i sits in cell i + 1 (cell 0
    // of f_middle.dds is the blank space) [BYTES; DATA: the atlas].
    const int n = i + 1, col = n & 15, row = n >> 4;
    g->w = w; g->h = 25;
    g->u0 = float(col * 26 + 1) / 512.0f; g->v0 = float(row * 26) / 512.0f;
    g->u1 = g->u0 + w / 512.0f; g->v1 = g->v0 + 25.0f / 512.0f;
    g->advance = w - 2.0f;
    return true;
}

bool glyph_mini(unsigned char c, Glyph* g) {
    if (c <= 0x20) return false;
    const int i = c <= 0x7F ? c - 0x20 : c - 0x27;
    if (i < 0 || i >= 224) return false;
    const float w = float(kMiniW[i] + 1);
    const int col = i & 15, row = i >> 4;
    g->w = w; g->h = 14;
    g->u0 = float(col * 12) / 256.0f; g->u1 = float(col * 12 + w) / 256.0f;
    g->v0 = float(row * 14) / 256.0f; g->v1 = float(row * 14 + 14) / 256.0f;
    g->advance = w - 1.0f;
    return true;
}

float middle_shift(const std::string& s, std::size_t& i) {
    if (s[i] != '#' || i + 2 >= s.size() || (s[i + 1] != '>' && s[i + 1] != '<')) return 0;
    const unsigned char h = static_cast<unsigned char>(s[i + 2]);              // the table 0x68edc0: a hex digit
    const int v = h >= '0' && h <= '9' ? h - '0' : h >= 'A' && h <= 'F' ? h - 'A' + 10 : h >= 'a' && h <= 'f' ? h - 'a' + 10 : -1;
    if (v < 0) return 0;
    const float d = s[i + 1] == '>' ? float(v) : -float(v);
    i += 2;
    return d;
}

float text_width_middle(const std::string& s) {
    float w = 0;
    for (std::size_t k = 0; k < s.size(); ++k) {
        const unsigned char c = static_cast<unsigned char>(s[k]);
        if (c == '#') { const std::size_t k0 = k; w += middle_shift(s, k); if (k != k0) continue; }
        if (c == ' ' || c == '\t') { w += 13; continue; }
        // FUN_004531d0 measures with c - 0x21 for every char (the draw uses c - 0x28 >= 0x84).
        const int i = c - 0x21;
        if (i >= 0 && i < 222) w += float(kMidW[i]) - 2.0f;
    }
    return w;
}

float text_width_mini(const std::string& s) {
    float w = 0;
    for (unsigned char c : s) {
        if (c == ' ' || c == '\t') { w += 9; continue; }
        Glyph g;
        if (glyph_mini(c, &g)) w += g.advance;
    }
    return w;
}

}  // namespace cleria
