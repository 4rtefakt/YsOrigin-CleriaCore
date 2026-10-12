// GoPopup: build/item_re/PICKUPS.md §13. See popup.hpp.
#include "cleria/popup.hpp"

#include <algorithm>
#include <cmath>

namespace cleria {

namespace {

// The glyph advances at 0x69b1f0 (222 signed bytes; the table 0x731460 that FUN_00442260 builds
// for F_GEO20), read from yso_win.exe v1.1.1.0.
constexpr std::int8_t kGeoAdv[222] = {
    9, 9, 15, 15, 20, 18, 6, 12, 12, 12, 14, 11, 12, 11, 15, 15, 10, 13, 14, 14, 14, 14, 14, 14, 13, 12, 13, 15,
    16, 15, 16, 16, 18, 18, 16, 17, 17, 17, 18, 21, 12, 14, 17, 17, 23, 19, 17, 17, 17, 18, 16, 18, 18, 18, 22, 18,
    17, 18, 12, 11, 12, 13, 17, 9, 14, 15, 13, 15, 13, 14, 15, 15, 10, 13, 15, 10, 21, 16, 13, 17, 14, 13, 13, 12,
    15, 16, 19, 15, 16, 15, 13, 13, 13, 15, 16, 16, 16, 16, 16, 0, 0, 0, 0, 0, 16, 20, 16, 16, 16, 26, 20, 16,
    16, 16, 16, 10, 16, 10, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 10, 10, 18, 18, 18, 16, 24, 20, 20, 21, 22,
    21, 20, 22, 24, 14, 18, 23, 22, 26, 21, 22, 20, 23, 20, 19, 22, 23, 23, 27, 21, 22, 22, 16, 16, 16, 20, 16, 20,
    16, 17, 16, 17, 14, 14, 16, 18, 12, 15, 16, 12, 23, 17, 15, 17, 17, 15, 15, 14, 16, 17, 21, 17, 17, 17, 16, 16,
    16, 16, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24};

}  // namespace

bool geo20_glyph(unsigned char c, GeoGlyph* g) {
    if (c <= 0x20) return false;
    const int e = c < 0x84 ? c - 0x21 : c < 0xff ? c - 0x28 : 9;
    if (e < 0 || e >= 222) return false;
    g->adv = float(kGeoAdv[e]);
    g->u0 = float((e % 21) * 24) / 512.0f;
    g->v0 = float((e / 21) * 24) / 128.0f;
    g->u1 = g->u0 + g->adv / 512.0f;
    g->v1 = g->v0 + 0.1796875f;
    return true;
}

Popup make_popup(const std::string& text, std::uint32_t colour, float scale, const Vec3& pos) {
    Popup p;
    p.pos = pos;
    p.colour = colour & 0xFFFFFF;
    const std::string t = text.substr(0, 31);
    // FUN_004535d0 at scale 1: quads [pen, pen + adv] x [0, 23], pen += adv - 2; space / tab = 12.
    struct Raw { float x0, x1; int e; };
    std::vector<Raw> raw;
    float pen = 0, w = 0;
    for (unsigned char c : t) {
        if (c == ' ' || c == '\t') { pen += 12; continue; }
        if (c <= 0x20) continue;
        const int e = c < 0x84 ? c - 0x21 : c < 0xff ? c - 0x28 : 9;
        if (e < 0 || e >= 222) continue;
        const float adv = float(kGeoAdv[e]);
        raw.push_back({pen, pen + adv, e});
        pen += adv - 2;
        w += adv - 2;
    }
    const float h = 24, k = scale * 0.02f;
    for (const Raw& r : raw) {
        PopupGlyph g;
        g.x0 = (r.x0 - w / 2) * k;
        g.x1 = (r.x1 - w / 2) * k;
        g.top = (-0.0f - h / 2) * k;
        g.bottom = (-23.0f - h / 2) * k;
        const int col = r.e % 21, row = r.e / 21;
        g.u0 = float(col * 24) / 512.0f;
        g.v0 = float(row * 24) / 128.0f;
        g.u1 = g.u0 + float(kGeoAdv[r.e]) / 512.0f;
        g.v1 = g.v0 + 0.1796875f;
        p.glyphs.push_back(g);
    }
    p.n = int(p.glyphs.size()) * 2;
    return p;
}

void step_popup(Popup& p) {
    if (p.dead) return;
    p.age += 1;
    if (p.age < 14) p.alpha = float(p.age) / 14.0f;
    else if (p.age < p.n + 35) {}
    else if (p.age < p.n + 43) p.alpha = float(p.n + 43 - p.age) * 0.125f;
    else p.dead = true;
}

float Popup::rise(int i) const {
    const float t = float(age) - 2.0f * float(i);
    if (t < 16.0f) return std::sin(t * 0.0981748f);
    if (t < float(n + 30)) return 1.0f;
    return 2.0f - std::cos((t - float(n + 30)) * 0.1308997f);
}

std::uint32_t Popup::diffuse() const {
    const std::uint32_t a = alpha >= 1.0f ? 255u : std::uint32_t(std::max(0.0f, std::floor(alpha * 256.0f)));
    return (a << 24) | colour;
}

}  // namespace cleria
