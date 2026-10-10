// The floor-3 script weather: build/effect_re/WEATHER.md. Every float expression below keeps the
// retail instruction order (single precision, no contraction), so a child's descriptor matches the
// exe bit for bit (apps/cleria-weathertest replays the oracle run of the real update bodies).
#include "cleria/weather.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include "cleria/cos_table.hpp"

namespace cleria::weather {

// .rdata constants whose bits are not the nearest float to their decimal value.
constexpr float k005 = std::bit_cast<float>(0x3d4cccd0u);    // 0x6bd144 "0.05" (the camera-line lerp)
constexpr float k0002 = std::bit_cast<float>(0x3b03126eu);   // 0x6bd00c "0.002" (the smoke drift)
constexpr float k0012 = std::bit_cast<float>(0x3c449ba5u);   // 0x6bd09c "0.012" (the smoke rise; 0x6bd0a0 = 0.012f)

float unit12(std::uint32_t w) {
    const std::uint32_t b = (w & 0x7fffffu) | 0x3f800000u;
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

void Blur::step() {                                  // FUN_00427c90 0x76a558 block (0x427ded..0x427f44)
    if (level == 0.0f) return;
    level = level + delta;
    latch = true;                                    // 0x7b24a0 = byte 0x6d6071 (= 1)
    if (level <= 0.0f) {
        level = 0.0f;
        out = 0.0f;
    } else {
        if (1.0f < level) level = 1.0f;
        out = strength;
        if (grid.drawn) { grid.drawn = false; grid.step(); }   // 0x7b2e1c
    }
}

// FUN_005e44c0's phase tables (.rdata 0x6be4b0 / 0x6be390 / 0x6be1c0 / 0x6be4c0 / 0x6be470 /
// 0x6be1b0 / 0x6be480.., copied to 0x7b2cfc / 0x7b2d8c at 0x5e549b..): degrees, by strip vertex.
static constexpr std::int32_t kPhaseA[BlurGrid::kN] = {
    0, 120, 60, 360, 240, 180, 120, 60, 360, 240, 180, 0,
    0, 180, 240, 360, 60, 120, 180, 240, 360, 60, 120, 0,
    0, 120, 60, 360, 240, 180, 120, 60, 360, 240, 180, 0};
static constexpr std::int32_t kPhaseB[BlurGrid::kN] = {
    360, 240, 180, 0, 120, 60, 360, 240, 180, 0, 120, 60,
    60, 120, 0, 180, 240, 360, 60, 120, 0, 180, 240, 360,
    360, 240, 180, 0, 120, 60, 360, 240, 180, 0, 120, 60};
// 0x6983d8: the amplitude in back-buffer pixels, by strip vertex (= by lattice row 7 / 5.25 / 4 /
// 4 / 5.25 / 7; the middle band runs bottom-up).
static constexpr float kAmp[BlurGrid::kN] = {
    7, 7, 5.25f, 5.25f, 4, 4, 4, 4, 5.25f, 5.25f, 7, 7,
    7, 7, 5.25f, 5.25f, 4, 4, 4, 4, 5.25f, 5.25f, 7, 7,
    7, 7, 5.25f, 5.25f, 4, 4, 4, 4, 5.25f, 5.25f, 7, 7};
constexpr float kDeg = std::bit_cast<float>(0x3c8efa35u);    // 0x6bd0dc = pi / 180
constexpr float kThird = std::bit_cast<float>(0x3eaaab8au);  // 0x6bd2a4 "0.33334"

BlurGrid::BlurGrid() {
    std::memcpy(a, kPhaseA, sizeof a);
    std::memcpy(b, kPhaseB, sizeof b);
}

void BlurGrid::step() {                              // FUN_00427c90 0x427e66..0x427f22
    for (int i = 0; i < kN; ++i) {
        a[i] += 1;
        b[i] += 6;
        const float ca = float(std::cos(double(float(a[i]) * kDeg)));   // FUN_0041c510 = cos
        dx[i] = kAmp[i] * ca;
        const float cb = float(std::cos(double(float(b[i]) * kDeg)));
        dy[i] = cb * kAmp[i] * 1.25f;                // 0x6bd494
    }
}

// FUN_005e44c0: column k at x1 * k (x1 = (W - 0.5) * 0.33334; column 0 at -0.5), row r at y1 * r
// (y1 = (H - 0.5) * 0.2; row 0 at -0.5 in the first band but y1 * 0.0 = 0 in the other two), UV
// u1 * k / v1 * r with u1 = W / TW * 0.33334, v1 = H / TH * 0.2. Strip order: band 0 (columns 0-1)
// rows 0..5, band 1 (columns 1-2) rows 5..0, band 2 (columns 2-3) rows 0..5; each row left, right.
BlurGrid::V BlurGrid::base(int i, int W, int H, float TW, float TH) {
    const float fw = float(double(W)), fh = float(double(H));
    const float x1 = (fw - 0.5f) * kThird, y1 = (fh - 0.5f) * 0.2f;
    const float u1 = fw / TW * kThird, v1 = fh / TH * 0.2f;
    const int band = i / 12, k = i % 12;
    const int col = band + (k & 1);
    const int row = band == 1 ? 5 - k / 2 : k / 2;
    const float xs[4] = {-0.5f, x1, x1 + x1, x1 * 3.0f};
    const float us[4] = {0.0f, u1, u1 + u1, u1 * 3.0f};
    const float ys[6] = {band == 0 ? -0.5f : y1 * 0.0f, y1, y1 + y1, y1 * 3.0f, y1 * 4.0f, y1 * 5.0f};
    const float vs[6] = {0.0f, v1, v1 + v1, v1 * 3.0f, v1 * 4.0f, v1 * 5.0f};
    return {xs[col], ys[row], us[col], vs[row]};
}

Look look(const Sprite& s) {                         // FUN_00584270's key-frame lerp
    const float ph = s.age;                          // +0x1e8 = +0x138 + interp [not modelled]
    float u;
    int a, b;
    if (s.d.t1 <= ph) {
        if (ph < s.d.t2) {
            u = (ph - s.d.t1) / (s.d.t2 - s.d.t1);
            a = 1; b = 2;
        } else {
            u = (ph - s.d.t2) / (s.d.life - s.d.t2);
            a = 2; b = 3;
        }
    } else {
        u = ph / s.d.t1;
        a = 0; b = 1;
    }
    Look l;
    l.half = (s.size[b] - s.size[a]) * u + s.size[a];
    l.alpha = (s.alpha[b] - s.alpha[a]) * u + s.alpha[a];
    return l;
}

Obj* Weather::alloc(Obj::Kind k) {                   // FUN_0040f700 on pool 9: NULL when full
    if (objs_.size() >= pool_cap) return nullptr;
    objs_.push_back(std::make_unique<Obj>());
    objs_.back()->kind = k;
    return objs_.back().get();
}

void Weather::fire_drop(std::uint32_t life) {        // 0x125 [BYTES 0x56b771..0x56b80d]
    if (Obj* o = alloc(Obj::Ember)) o->cap = life;   // +0x120.. = the player position (unread)
}

void Weather::frame_smoke(std::uint32_t life) {      // 0x122 [BYTES 0x56b59c..0x56b663]
    if (Obj* o = alloc(Obj::Smoke)) o->cap = life;
}

// FUN_00583d90: a new type-0 GoXx from the descriptor.
Obj* Weather::emit(const Desc& d) {
    emits_.push_back(d);
    Obj* o = objs_.back().get();                     // the slot alloc() just handed out
    o->kind = Obj::Child;
    Sprite& s = o->s;
    s.d = d;
    s.pos = d.pos;
    s.prev = d.pos;
    s.vel = d.vel;
    s.age = 0;
    const int i = int(d.angle * 40.743664f);        // _DAT_006bda38 = 256 / 2pi
    s.cs = cos_table(i & 0xff);
    s.sn = cos_table((i - 0x40) & 0xff);
    const float k = float(d.argb >> 24);
    for (int j = 0; j < 4; ++j) {
        s.alpha[j] = d.alpha[j] * k;
        s.size[j] = d.size[j];
    }
    return o;
}

void Weather::dust(int count, std::uint32_t argb, bool additive, const Vec3& pos, float size, float radius,
                   float speed, float drag, bool layer9) {
    Desc d;
    d.tex = Tex::EftTex34;                           // 0x74a5e0, YsF_used\p_smoke1.dds
    d.rstate = additive ? 0x46c1u : 0x4641u;
    d.kind = layer9 ? 9u : 0xcu;
    d.argb = argb;
    d.drag = drag;
    d.depth_k = size * -0.25f;
    d.t1 = 5.0f; d.t2 = 18.0f;
    d.alpha[0] = 0.5f; d.alpha[1] = 1.0f; d.alpha[2] = 0.5f; d.alpha[3] = 0.0f;
    d.size[0] = size * 0.5f; d.size[1] = size; d.size[2] = size * 0.75f; d.size[3] = size * 0.5f;
    for (int k = 0; k < count; ++k) {
        if (!alloc(Obj::Child)) return;
        const int i = int(((unit12(rng()) - 1.0f) * 6.2831855f - 1.5707964f) * 40.743664f);   // 0x6bd898, 0x6bd4e4
        const float r = (radius - radius * 0.25f) * (unit12(rng()) - 1.0f) + radius * 0.25f;
        const float c = cos_table(i & 0xff), s = cos_table((i - 0x40) & 0xff);
        d.pos = {pos.x + c * r, pos.y, pos.z + s * r};
        d.vel = {c * speed, 0.0f, s * speed};
        d.angle = (unit12(rng()) - 1.0f) * 6.2831855f;
        d.life = float((rng() >> 1) & 1u) + 24.0f;   // 0x6bd9b8
        emit(d);
        d.delay += 2;
    }
}

// FUN_0058e7c0 (GoXx type 0x12), the 0x125 ember emitter [EXPORT 0058e7c0; BYTES 0x58e7c0..0x58ed71].
void Weather::ember(Obj& o, const Env& e) {
    if (o.cap <= o.n) { o.dead = true; return; }
    ++o.n;
    if (!e.map || (o.n & 0xf) != 0) return;
    const float t = (unit12(rng()) - 1.0f) * k005 + 0.5f;
    const float bx = (e.at.x - e.eye.x) * t + e.eye.x;
    const float bz = (e.at.z - e.eye.z) * t + e.eye.z;
    const float by = (e.at.y - e.eye.y) * t + e.eye.y + 3.0f;
    Desc d;
    d.tex = Tex::Sparkle;                            // cmove on +0x130 == 0 (always, from 0x125)
    d.argb = 0xffffffffu;
    float size = 0.0225f;
    if (e.noise) {                                   // 0x76bda4
        float g = unit12(rng()) - 1.0f;
        g = g + g;
        g = g + 1.0f;
        size = 0.0225f * g;
        d.argb = 0xff33ffeeu;
    }
    d.angle = ((unit12(rng()) - 1.0f) * 360.0f + 0.0f) * 0.0174532924f;
    d.t1 = float(double(o.cap)) * 0.1f;
    d.t2 = d.t1 * 4.0f;
    d.depth_k = -0.005625f;                          // 0xbbb851ec
    d.alpha[0] = 0; d.alpha[1] = 1; d.alpha[2] = 1; d.alpha[3] = 0;
    for (float& s : d.size) s = size;
    if (!alloc(Obj::Child)) return;
    rng();                                           // a step the body never reads as a float
    d.pos.x = (unit12(rng()) - 1.0f) * 12.0f - 6.0f + bx;
    d.pos.z = (unit12(rng()) - 1.0f) * 12.0f - 6.0f + bz;
    d.pos.y = (unit12(rng()) - 1.0f) * 12.0f - 6.0f + by;
    d.vel.x = (unit12(rng()) - 1.0f) * 0.048f - 0.024f;
    d.vel.z = (unit12(rng()) - 1.0f) * 0.048f - 0.024f;
    d.vel.y = (unit12(rng()) - 1.0f) * 0.012f + 0.012f;
    const std::uint32_t rem = (rng() >> 1) % 3u;
    d.life = float(double(rem)) + float(double(o.cap));
    o.child = emit(d);
}

// FUN_0058daa0 (GoXx type 0x10), the 0x122 smoke emitter [EXPORT 0058daa0; BYTES 0x58daa0..0x58e2ae].
void Weather::smoke(Obj& o, const Env& e) {
    if (o.cap <= o.n) { o.dead = true; return; }
    ++o.n;
    if (e.map) {
        o.f138 = e.map268;
        if (o.n == 1) {
            const float t = (unit12(rng()) - 1.0f) * k005 + 0.5f;
            const float lx = (e.at.x - e.eye.x) * t + e.eye.x;
            const float ly = (e.at.y - e.eye.y) * t + e.eye.y;
            const float lz = (e.at.z - e.eye.z) * t + e.eye.z;
            Desc d;
            d.tex = Tex::EftTex34;
            float size = (unit12(rng()) - 1.0f) * 0.15f + 0.1f + 1.0f;
            d.argb = 0x60ffffffu;
            if (e.noise) {
                d.tex = Tex::Kao00;
                size = size * ((unit12(rng()) - 1.0f) * 2.5f + 3.0f);
                d.argb = ((rng() & 0x20000u) ? 0xff34cc00u : 0u) + 0x20ff33aau;
            }
            d.depth_k = size * -0.2f;
            d.angle = ((unit12(rng()) - 1.0f) * 360.0f + 0.0f) * 0.0174532924f;
            const float c = float(double(o.cap));
            d.life = c;
            const float tenth = c * 0.1f;
            d.t1 = tenth + tenth;
            d.t2 = tenth * 3.0f;
            d.alpha[0] = 0; d.alpha[1] = 1; d.alpha[2] = 1; d.alpha[3] = 0;   // 0x6be210..
            for (float& s : d.size) s = size;
            if (!alloc(Obj::Child)) return;          // returns before the drift draws too
            rng();                                   // a step the body never reads as a float
            d.pos.x = (unit12(rng()) - 1.0f) * 6.0f - 3.0f + lx;
            d.pos.z = (unit12(rng()) - 1.0f) * 4.0f - 2.0f + lz;
            d.pos.y = (unit12(rng()) - 1.0f) * 8.0f - 4.0f + ly - 1.5f;
            d.vel.x = (unit12(rng()) - 1.0f) * k0002 + 0.001f;
            d.vel.z = (unit12(rng()) - 1.0f) * k0002 + 0.001f;
            d.vel.y = (unit12(rng()) - 1.0f) * k0012 + 0.006f;
            o.off = {d.pos.x - e.at.x, d.pos.y - e.at.y, d.pos.z - e.at.z};
            o.child = emit(d);
        }
    }
    if (o.child) o.child->s.pos = {o.off.x + e.at.x, e.at.y + o.off.y, e.at.z + o.off.z};
    o.off.x = (unit12(rng()) - 1.0f) * k0002 + 0.001f + o.off.x;
    o.off.z = (unit12(rng()) - 1.0f) * k0002 + 0.001f + o.off.z;
    o.off.y = (unit12(rng()) - 1.0f) * k0012 + 0.006f + o.off.y;
}

// FUN_00584030 (GoXx type 0): the start delay +0x130 counts down first (not moved, not drawn
// [EXPORT 00584030.c:62-65]); then age, move, die at the end of its life.
void Weather::sprite(Obj& o) {
    Sprite& s = o.s;
    if (s.d.delay != 0) { --s.d.delay; return; }
    s.age = s.age + 1.0f;
    if (s.d.life <= s.age) { o.dead = true; return; }
    s.prev = s.pos;
    s.pos = {s.vel.x + s.pos.x, s.vel.y + s.pos.y, s.vel.z + s.pos.z};
    if (s.d.drag != 0.0f) {
        const float k = 1.0f - s.d.drag;
        s.vel = {s.vel.x * k, s.vel.y * k, s.vel.z * k};
    }
}

void Weather::step(const Env& e) {
    const std::size_t n = objs_.size();              // the pass's count snapshot
    for (std::size_t i = 0; i < n; ++i) {
        Obj& o = *objs_[i];
        if (o.dead) continue;                        // +0x11 != 0: not updated
        switch (o.kind) {
            case Obj::Ember: ember(o, e); break;
            case Obj::Smoke: smoke(o, e); break;
            case Obj::Child: sprite(o); break;
        }
    }
}

void Weather::sweep() {
    for (auto& o : objs_)
        if (o->child && o->child->dead) o->child = nullptr;   // [H] a freed slot is not written
    objs_.erase(std::remove_if(objs_.begin(), objs_.end(), [](const std::unique_ptr<Obj>& o) { return o->dead; }),
                objs_.end());
}

std::size_t Weather::live_emitters() const {
    std::size_t k = 0;
    for (const auto& o : objs_) k += !o->dead && o->kind != Obj::Child;
    return k;
}

}  // namespace cleria::weather
