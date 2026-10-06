// The cutscene overlay sprites: build/event_re/WNDDIALOG_DRAW.md §7-§8. See eventsprites.hpp.
#include "cleria/eventsprites.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace cleria {

namespace {

std::string tex_name(const std::string& name) {
    std::string up = name;
    for (char& c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
    return up + ".DDS";
}
bool starts(const std::string& s, const char* p) {
    std::size_t n = 0;
    while (p[n]) ++n;
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != p[i]) return false;
    return true;
}

bool lower_is(char c, char l) { return std::tolower(static_cast<unsigned char>(c)) == l; }

}  // namespace

// ---- FUN_005d3be0 --------------------------------------------------------------------------

double map_coord(int axis, int mode, double v, bool fit, int W, int H) {
    const int w43 = (4 * H) / 3, h34 = (3 * W) / 4;           // imul 0x55555556 / (3W + bias) >> 2
    const double dW = W, dH = H;
    double sx = w43, sy = dH;                                  // xmm4 / xmm5
    if (!fit) {
        // 0x765178 = W / W43 when W > W43, else 1.0; 0x765180 = H / (3W/4) when that is < H
        // (FUN_005d3ab0). At 1.0 the value comes back untouched.
        if (axis == 1 ? !(h34 < H) : !(w43 < W)) return v;
        if (w43 > W) { sy = dW / w43 * dH; sx = dW; }          // 0x5d3c6f
    } else if (W > w43) {
        sy = h34; sx = dW;                                     // 0x5d3c8d: the field takes the width
    }
    if (axis == 3) axis = H >= W ? 0 : 1;                      // 0x5d3cae
    const bool field = (mode >= 1 && mode <= 3) || fit;
    const double ref = axis == 0 ? 1024.0 : 768.0;
    const double s = axis == 0 ? (field ? sx : dW) : (field ? sy : dH);
    const double full = axis == 0 ? dW : dH, used = axis == 0 ? sx : sy;
    if (s == ref) {                                            // 0x5d3d10: no scale, offset only
        const double t = double(int(v));
        if (mode == 3) return t + (full - used);
        if (mode == 2) return t + (full - used) * 0.5;
        return v;
    }
    const double p = v / ref * s;                              // 0x5d3da0
    if (mode == 3) return double(int(p + (full - used)));
    if (mode == 2) return double(int(p + (full - used) * 0.5));
    return double(int(p));
}

// ---- FUN_0058b410's name dispatch --------------------------------------------------------------

CardPath card_path(const std::string& name, float x) {
    // The engine reads the name's first five bytes; past the terminator they never match.
    char n[5] = {0, 0, 0, 0, 0};
    for (std::size_t i = 0; i < 5 && i < name.size(); ++i) n[i] = name[i];
    CardPath r;
    const bool v_ = lower_is(n[0], 'v') && n[1] == '_';
    if (v_ && n[2] == '0') return r;                                                // 0x58b630
    if (v_ && n[2] == '1' && n[3] == '6' && n[4] == '0') { r.pillars = 2; return r; }  // 0x58b660
    if (v_ && n[2] == '1' && n[3] == '0' && n[4] == '9') { r.kind = CardPath::Stretch; return r; }
    if (v_ && n[2] == '3' && n[3] == '0') { r.kind = CardPath::Cover; return r; }   // 0x58b702
    if (v_) {                                                                       // 0x58b7ba
        const char a = n[2], b = n[3], c = n[4];
        const bool black = (a == '1' && (b == '5' || b == '7')) || a == '2' || (a == '3' && b == '2') ||
                           (a == '4' && ((b == '0' && (c == '0' || c == '1' || c == '2')) || b == '5'));
        if (black) { r.pillars = 1; return r; }                                     // 0x58b874
    }
    // Every other V_ (and "fin") falls to the default; the t_ busts pick the field by name or x.
    if (lower_is(n[0], 't') && n[1] == '_') {
        if (n[2] == '8' && n[3] == '6' && n[4] == '0') r.mode = 3;                  // 0x58b986
        else if (n[2] == '8' && n[3] == '8' && n[4] == '0') r.mode = 1;             // 0x58b9c9
        else if (x < 0.0f) r.mode = 1;                                              // 0x58bd96
        else if (x > 100.0f) r.mode = 3;                                            // 0x58bdbb (0x6bdafc)
    }
    return r;
}

// ---- the caption card ------------------------------------------------------------------------

void CaptionCard::open(const std::string& name, bool hold, bool alt) {
    tex_ = tex_name(name);
    timer_ = 150;
    hold_ = hold;
    alt_ = alt;
}

void CaptionCard::step() {
    if (timer_ > 0 && (!hold_ || timer_ > 45)) --timer_;   // +0x124 holds it at 45 (full alpha)
}

void CaptionCard::draw(DrawList& out) const {
    if (timer_ <= 0) return;
    const float t = float(timer_);
    const float a = t >= 105 ? (150 - t) / 45.0f : t <= 45 ? t / 45.0f : 1.0f;
    const std::uint32_t al = std::uint32_t(std::clamp(a, 0.0f, 1.0f) * 255.0f);
    DrawCmd c;
    c.kind = DrawCmd::Quad;
    c.tex = {TexRef::Named, 0, tex_.c_str()};
    c.x = alt_ ? 0.0f : 16.0f; c.y = alt_ ? 192.0f : 16.0f; c.w = 1024; c.h = 256;
    c.u0 = 0; c.v0 = 0; c.u1 = 1; c.v1 = 1;
    c.diffuse = (al << 24) | 0xFFFFFF;
    c.flags = 0x40c1;                                      // additive
    c.anchor_x = alt_ ? 2 : 1; c.anchor_y = 0;             // the 4:3 field / the screen's left edge
    if (al) out.push_back(std::move(c));
}

// ---- the busts --------------------------------------------------------------------------------

Busts::Bust* Busts::find(const std::string& name) {
    for (Bust& b : list_) if (!b.dead && b.name == name) return &b;
    return nullptr;
}

void Busts::open(const std::string& name, float x, float y) {
    if (find(name)) return;                                // FUN_0058b090: no-op if it exists
    Bust b;
    b.name = name;
    b.tex = tex_name(name);
    if (size_of) size_of(b.tex, &b.w, &b.h);
    b.x = b.sx = b.tx = x;
    b.y = b.sy = b.ty = y;
    list_.push_back(std::move(b));
}

void Busts::close(const std::string& name) {
    if (Bust* b = find(name)) b->closing = true;
}

void Busts::move(const std::string& name, float x, float y, int frames, int curve) {
    Bust* b = find(name);
    if (!b) return;
    b->sx = b->x; b->sy = b->y;
    b->tx = x; b->ty = y;
    b->t = 0;
    b->dt = frames > 0 ? 1.0f / float(frames) : 1.0f;
    b->curve = curve;
}

void Busts::colour(const std::string& name, const std::uint32_t c[4]) {
    if (Bust* b = find(name)) for (int i = 0; i < 4; ++i) b->corner[i] = c[i];
}
void Busts::priority(const std::string& name, int p) {
    if (Bust* b = find(name)) b->prio = p;
}
void Busts::fade_speed(const std::string& name, float s) {
    if (Bust* b = find(name)) b->speed = s;
}

void Busts::step() {
    for (Bust& b : list_) {
        if (b.closing) {
            b.fade -= 0.02f * b.speed;
            if (b.fade <= 0) { b.fade = 0; b.dead = true; }
        } else {
            b.fade = std::min(1.0f, b.fade + 0.02f * b.speed);
        }
        if (b.t < 1.0f) {
            b.t = std::min(1.0f, b.t + b.dt);
            const float t = b.t;
            const float e = b.curve == 0 ? t * t * (3.0f - 2.0f * t)              // smoothstep (FUN_0042ca10)
                          : b.curve == 2 ? std::sin(t * 1.5707964f) : t;           // sine, else linear
            b.x = b.sx + (b.tx - b.sx) * e;
            b.y = b.sy + (b.ty - b.sy) * e;
        }
    }
    list_.erase(std::remove_if(list_.begin(), list_.end(), [](const Bust& b) { return b.dead; }), list_.end());
}

void Busts::draw(DrawList& out, int W, int H) const {
    std::vector<const Bust*> order;
    for (const Bust& b : list_) order.push_back(&b);
    // A LOWER prio draws on top: the overlay pass (15) is qsorted DESCENDING by its draw key
    // (comparator 0x5df950 = b.key - a.key; the pass's comparator index 0x703eb4 is 0 in .data) and
    // a bust's key is 0x0a000000 + prio [BYTES 0x5e0134.., 0x58b3b8]. The scripts open each card at
    // 0 over a backdrop at 1/3. [H] equal keys keep their open order (the CRT qsort is not stable).
    std::stable_sort(order.begin(), order.end(), [](const Bust* a, const Bust* b) { return a->prio > b->prio; });
    const bool wide = W > (4 * H) / 3;                     // 0x765178 != 1.0
    for (const Bust* b : order) {
        if (b->w <= 0 || b->h <= 0) continue;
        const bool top = starts(b->name, "tfp");
        const CardPath path = card_path(b->name, b->x);
        DrawCmd c;
        c.kind = DrawCmd::Quad;
        c.tex = {TexRef::Named, 0, b->tex.c_str()};
        c.x = b->x; c.y = top ? b->y : b->y - float(b->h);
        c.w = float(b->w); c.h = float(b->h);
        c.u0 = 0; c.v0 = 0; c.u1 = 1; c.v1 = 1;
        // Per-corner colour (+0x150.. = the TL, BL, TR, BR vertices): corner 0 for the whole quad
        // [approximation].
        const std::uint32_t a = std::uint32_t(float(b->corner[0] >> 24) * b->fade);
        c.diffuse = (a << 24) | (b->corner[0] & 0xFFFFFF);
        c.flags = 0x24041;
        c.anchor_x = path.mode; c.anchor_y = 0;
        if (wide && path.kind == CardPath::Stretch) c.anchor_x = 0;     // V_109: the plain W/1024 stretch
        if (wide && path.kind == CardPath::Cover) {                     // V_30x: pixels from the mapper
            const float x0 = float(map_coord(0, 2, c.x, true, W, H)), x1 = float(map_coord(0, 2, c.x + c.w, true, W, H));
            const float y0 = float(map_coord(1, 2, c.y, true, W, H)), y1 = float(map_coord(1, 2, c.y + c.h, true, W, H));
            c.x = x0; c.y = y0; c.w = x1 - x0; c.h = y1 - y0;
            c.anchor_x = c.anchor_y = 0x23D;
        }
        if (a) out.push_back(c);                           // (alpha 0 draws nothing at alpha ref 1)
        if (!wide || path.pillars == 0) continue;
        // The side pillars, in raw pixels, left then right [BYTES 0x58bb4c..0x58c26a]. Lx / R = the
        // 4:3 field's edges (mode 2 of 0 / 1024), W = mode 3 of 1024.
        const float Lx = float(map_coord(0, 2, 0.0, false, W, H)), R = float(map_coord(0, 2, 1024.0, false, W, H));
        const float fw = float(map_coord(0, 3, 1024.0, false, W, H)), fh = float(H);
        DrawCmd p;
        p.kind = DrawCmd::Quad;
        p.anchor_x = p.anchor_y = 0x23D;
        p.specular = 0;
        if (path.pillars == 1) {
            // AltPillar16-9 (menu 48), a one-texel column, diffuse 0 under 0x20001 (modulate, no
            // blend): opaque black whatever the card's fade.
            p.tex = {TexRef::Menu, 48};
            p.u0 = 0.177734375f; p.v0 = 0; p.u1 = 0.1787109375f; p.v1 = 0.75f;   // 0x6be8b0
            p.diffuse = 0; p.flags = 0x20001;
            p.x = 0; p.y = 0; p.w = Lx; p.h = fh;
            out.push_back(p);
            p.x = R; p.w = fw - R;
            out.push_back(p);
        } else {
            // HugoPillar16-9 (menu 56) from A >= 1.21 (0x6bd560), else HugoPillar16-10 (57); alpha
            // blended at the card's corner-0 colour and fade.
            const bool w169 = double(W) / double((4 * H) / 3) >= 1.21;
            p.tex = {TexRef::Menu, w169 ? 56 : 57};
            p.diffuse = c.diffuse; p.flags = 0x24041;
            p.x = -0.5f; p.y = w169 ? -0.0f : -0.5f; p.w = Lx; p.h = fh - 0.5f;
            p.u0 = 0; p.v0 = 0; p.u1 = w169 ? 0.16796875f : 0.1015625f; p.v1 = 0.75f;   // 0x6be8a0 / 0x6be890
            out.push_back(p);
            p.x = R; p.w = fw - R + 0.5f;
            p.u0 = w169 ? 0.83203125f : 0.8984375f; p.u1 = 1.0f;                     // 0x6be920 / 0x6be930
            out.push_back(p);
        }
    }
}

}  // namespace cleria
