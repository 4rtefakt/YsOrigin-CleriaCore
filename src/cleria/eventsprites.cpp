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

}  // namespace

// ---- the caption card ------------------------------------------------------------------------

void CaptionCard::open(const std::string& name) {
    tex_ = tex_name(name);
    timer_ = 150;
}

void CaptionCard::step() {
    if (timer_ > 0) --timer_;
}

void CaptionCard::draw(DrawList& out) const {
    if (timer_ <= 0) return;
    const float t = float(timer_);
    const float a = t >= 105 ? (150 - t) / 45.0f : t <= 45 ? t / 45.0f : 1.0f;
    const std::uint32_t al = std::uint32_t(std::clamp(a, 0.0f, 1.0f) * 255.0f);
    DrawCmd c;
    c.kind = DrawCmd::Quad;
    c.tex = {TexRef::Named, 0, tex_.c_str()};
    c.x = 16; c.y = 16; c.w = 1024; c.h = 256;
    c.u0 = 0; c.v0 = 0; c.u1 = 1; c.v1 = 1;
    c.diffuse = (al << 24) | 0xFFFFFF;
    c.flags = 0x40c1;                                      // additive
    c.anchor_x = 1; c.anchor_y = 0;                        // pinned to the screen's left edge
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

void Busts::draw(DrawList& out) const {
    std::vector<const Bust*> order;
    for (const Bust& b : list_) order.push_back(&b);
    std::stable_sort(order.begin(), order.end(), [](const Bust* a, const Bust* b) { return a->prio < b->prio; });
    for (const Bust* b : order) {
        if (b->w <= 0 || b->h <= 0) continue;
        int mode = 2;                                      // x-mode by name (0x58b988..)
        if (starts(b->name, "t_860")) mode = 3;
        else if (starts(b->name, "t_880")) mode = 1;
        else if (starts(b->name, "t_")) { if (b->x < 0) mode = 1; else if (b->x > 100) mode = 3; }
        const bool top = starts(b->name, "tfp");
        DrawCmd c;
        c.kind = DrawCmd::Quad;
        c.tex = {TexRef::Named, 0, b->tex.c_str()};
        c.x = b->x; c.y = top ? b->y : b->y - float(b->h);
        c.w = float(b->w); c.h = float(b->h);
        c.u0 = 0; c.v0 = 0; c.u1 = 1; c.v1 = 1;
        // Per-corner colour (+0x150..): corner 0 for the whole quad [approximation].
        const std::uint32_t a = std::uint32_t(float(b->corner[0] >> 24) * b->fade);
        c.diffuse = (a << 24) | (b->corner[0] & 0xFFFFFF);
        c.flags = 0x24041;
        c.anchor_x = mode; c.anchor_y = 0;
        if (a) out.push_back(std::move(c));
    }
}

}  // namespace cleria
