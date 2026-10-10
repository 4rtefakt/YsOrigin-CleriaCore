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

// FUN_005886b0 [BYTES 0x5886b0..0x5887ce]: the terrain ray from the anchor to the position (skipped for one or
// two calls when more than 12 / 20 sparks live), the mirror bounce with its one draw, then the history shift.
void Weather::spark_collide(Spark& s) {
    s.prev = s.pos;
    if (s.skip != 0) --s.skip;
    else {
        if (sparks_ > 12) s.skip = sparks_ > 20 ? 2u : 1u;
        const Vec3 o = s.hist[0];
        Vec3 pt{}, n{};
        // (a zero-length ray — the first pre-roll call — is taken to hit nothing)
        if (hit && (o.x != s.pos.x || o.y != s.pos.y || o.z != s.pos.z) && hit(o, s.pos, &pt, &n)) {
            const float vn = s.vel.x * n.x + s.vel.y * n.y + s.vel.z * n.z;
            if (vn < 0.0f) {
                s.pos = {o.x + (pt.x - o.x) * 0.995f, o.y + (pt.y - o.y) * 0.995f, o.z + (pt.z - o.z) * 0.995f};   // 0x6bd424
                const float m = vn * -2.0f;                                                                      // 0x6be098
                s.vel = {s.vel.x + n.x * m, s.vel.y + n.y * m, s.vel.z + n.z * m};
                const float k = (unit12(rng()) - 1.0f) * 0.2f + 0.55f;       // 0x6bd210, 0x6bd320
                const float hf = n.y >= 0.5f ? k : 0.8f;                     // 0x6bd304, 0x6bd3a8
                s.vel.x *= hf; s.vel.z *= hf; s.vel.y *= k;
            }
        }
        s.hist[0] = s.pos;
    }
    for (int i = 10; i >= 2; --i) s.hist[i] = s.hist[i - 1];
    s.hist[1] = s.pos;
}

void Weather::sparks(int count, int delay_step, int life_range, float width, const Vec3& pos, float speed0,
                     float speed1, float vy0, float vy1, float ang0, float ang1) {
    std::uint8_t delay = 0;
    for (int k = 0; k < count; ++k) {
        Obj* o = alloc(Obj::Streak);
        if (!o) return;
        Spark& s = o->sp;
        s.life = float((rng() >> 1) % std::uint32_t(life_range + 1)) + 35.0f;     // 0x6bda10
        const float angle = (ang1 - ang0) * (unit12(rng()) - 1.0f) + ang0;
        const float speed = (speed1 - speed0) * (unit12(rng()) - 1.0f) + speed0;
        const float vy = (vy1 - vy0) * (unit12(rng()) - 1.0f) + vy0;
        // FUN_00588450: the velocity from the true cos / sin (FUN_0041c530 / FUN_0041c510), the history filled
        // with the start, then ten quarter-steps — each with the FULL tick's gravity.
        ++sparks_;                                   // [H] counted before the pre-roll, as the type-1 ctor does
        s.delay = delay;
        s.pos = pos;
        s.vel = {float(std::cos(double(angle))) * speed, vy, float(std::sin(double(angle))) * speed};
        s.half = width * 0.4f;                       // 0x6bd2d4
        for (Vec3& h : s.hist) h = pos;
        for (int q = 0; q < 10; ++q) {
            spark_collide(s);
            s.pos = {s.pos.x + s.vel.x * 0.25f, s.pos.y + s.vel.y * 0.25f, s.pos.z + s.vel.z * 0.25f};   // 0x6bd244
            s.vel.y -= 0.0075f;                      // 0x6bd064
        }
        s.prev = s.pos;
        delay = std::uint8_t(delay + delay_step);
    }
}

// FUN_0045e390 [BYTES 0x45e390..0x45e459]: a random offset — the distance, then the direction (the top byte).
void Weather::offset(float lo, float hi, float* dx, float* dz) {
    const float r = (hi - lo) * (unit12(rng()) - 1.0f) + lo;
    const int i = int(rng() >> 24);
    *dx = cos_table(i) * r;
    *dz = cos_table((i - 0x40) & 0xff) * r;
}

// FUN_0058a210 [BYTES 0x58a210..0x58a5e8]: the water rooms' substitute — clouds that only grow and fade.
bool Weather::water_cloud(int count, std::uint32_t argb, int delay_step, int life_range, float size0, float size1,
                          const Vec3& pos, float radius) {
    if (water_mode != 2) return false;               // mode 1's probes: [not modelled], see weather.hpp
    const float cap = 1000.0f;
    Desc d;
    d.tex = Tex::EftTex34;
    d.rstate = 0x4641u;
    d.kind = 9;
    d.argb = argb;
    d.pad = 1;                                       // dword 5 = 1 (0x58a35e; not read by the init)
    d.t1 = 35.0f; d.t2 = 158.0f;
    d.alpha[0] = 0.5f; d.alpha[1] = 1.0f; d.alpha[2] = 0.05f; d.alpha[3] = 0.0f;
    const float radius2 = radius + radius;
    const std::uint32_t life_mod = std::uint32_t(life_range * 4 + 1);
    for (int k = 0; k < count; ++k) {
        if (!alloc(Obj::Child)) return true;
        const float s = ((size1 - size0) * (unit12(rng()) - 1.0f) + size0) * 0.75f;
        d.size[0] = s * 0.5f; d.size[1] = s * 0.75f; d.size[2] = s; d.size[3] = s * 1.25f;
        float dx, dz;
        offset(0.0f, radius2, &dx, &dz);
        d.pos = {pos.x + dx, std::min(pos.y, cap), pos.z + dz};
        d.angle = (unit12(rng()) - 1.0f) * 6.2831855f;
        d.life = float((rng() >> 1) % life_mod) + 254.0f;   // 0x6bdc1c
        emit(d);
        d.delay += std::uint32_t(delay_step * 4);
    }
    return true;
}

// FUN_00589a60's head [BYTES 0x589a60..0x589b9b]: the ray 5 down from the point; no terrain there = no decal.
void Weather::decal_init(Obj& o, const Decal& d) {
    o.dc = d;
    Vec3 pt{}, n{};
    if (!hit || !hit(d.pos, {d.pos.x, d.pos.y - 5.0f, d.pos.z}, &pt, &n)) { o.dead = true; return; }
    o.dc.pos = pt;
    o.dc.normal = n;
    if (!surface) return;
    std::vector<Vec3> tris;
    surface(pt, d.size, tris);
    decal_mesh(o.dc, tris);
    if (o.dc.verts.empty()) o.dead = true;                       // +0x1a0 == 0 [EXPORT 00589a60.c:165-176]
}

void decal_mesh(Decal& d, const std::vector<Vec3>& tris) {
    d.verts.clear();
    // M = Scale(size) . R(axis = -normal, angle), row vectors; two of R's terms are not the textbook ones
    // (0x589dd5, 0x589e08) and are kept as the bytes have them. The origin puts the square's centre on pos.
    const float x = -d.normal.x, y = -d.normal.y, z = -d.normal.z;
    const float c = std::cos(d.angle), s = std::sin(d.angle), t = 1.0f - c;
    const float R[3][3] = {{t * x * x + c, t * x * y + s * z, t * x * z - s * y},
                           {t * x * y - s * z, t * y * y + c, s * x - t * y * z},
                           {s * y - t * x * z, t * y * z - s * x, t * z * z + c}};
    float A[3][3];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) A[i][j] = R[i][j] * d.size;
    const int i = int((d.angle + 0.7853982f) * 40.743664f);                       // 0x6bd3a0, 0x6bda38
    const Vec3 T{d.pos.x - cos_table(i & 0xff) * d.size * 0.70710677f, d.pos.y,
                 d.pos.z - cos_table((i - 0x40) & 0xff) * d.size * 0.70710677f};
    const float det = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
                      A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (det == 0.0f) return;
    // local = (world - T) . inverse(A): the first and third columns of the inverse give U and V.
    const float iu[3] = {(A[1][1] * A[2][2] - A[1][2] * A[2][1]) / det, -(A[1][0] * A[2][2] - A[1][2] * A[2][0]) / det,
                         (A[1][0] * A[2][1] - A[1][1] * A[2][0]) / det};
    const float iv[3] = {(A[0][1] * A[1][2] - A[0][2] * A[1][1]) / det, -(A[0][0] * A[1][2] - A[0][2] * A[1][0]) / det,
                         (A[0][0] * A[1][1] - A[0][1] * A[1][0]) / det};
    const float k = 223.0f / 255.0f;                             // 0x6bd3c8 ([H] +0x16d = 0: no writer found)
    for (std::size_t n = 0; n + 2 < tris.size() && d.verts.size() + 3 <= kDecalMaxVerts; n += 3) {
        const float cy = (tris[n].y + tris[n + 1].y + tris[n + 2].y) * 0.3333333f;
        const float dy = (d.pos.y + 0.75f) - cy;                 // 0x6bd390
        if (dy < -2.0f || dy > 2.0f) continue;
        float u[3], v[3];
        for (int q = 0; q < 3; ++q) {
            const Vec3 r{tris[n + q].x - T.x, tris[n + q].y - T.y, tris[n + q].z - T.z};
            u[q] = r.x * iu[0] + r.y * iu[1] + r.z * iu[2];
            v[q] = r.x * iv[0] + r.y * iv[1] + r.z * iv[2];
        }
        if ((u[0] < 0 && u[1] < 0 && u[2] < 0) || (u[0] > 1 && u[1] > 1 && u[2] > 1) ||
            (v[0] < 0 && v[1] < 0 && v[2] < 0) || (v[0] > 1 && v[1] > 1 && v[2] > 1))
            continue;
        for (int q = 0; q < 3; ++q) {
            const float h = std::clamp(2.0f * (tris[n + q].y - d.pos.y), -1.25f, 1.25f);
            d.verts.push_back({tris[n + q], u[q], v[q], k * std::clamp(1.0f - std::fabs(h), 0.0f, 1.0f)});
        }
    }
}

float decal_alpha(const Decal& d) {
    float a = float(d.argb >> 24) / 255.0f;                      // +0x188 (table 0x68f7a0)
    if (d.age < 6.0f) a *= d.age / 6.0f;                         // 0x6bd880
    if (!d.keep && d.age > d.life - 50.0f) a *= (d.life - d.age) / 50.0f;
    return a;
}

void Weather::blood(int count, std::uint32_t argb, int delay_step, int life_range, float size0, float size1,
                    const Vec3& pos, float radius, bool flag_a, bool flag_b) {
    if (water_cloud(count, argb, delay_step, life_range, size0, size1, pos, radius)) return;
    std::uint8_t delay = 0;
    for (int k = 0; k < count; ++k) {
        Obj* o = alloc(Obj::Splat);
        if (!o) return;
        Decal d;
        d.variant = std::uint8_t((rng() >> 16) % 3u);
        d.life = float((rng() >> 1) % std::uint32_t(life_range + 1)) + 250.0f;    // 0x6bdc14
        if (flag_b) { d.variant = 0; d.foot = true; }
        else {
            d.foot = (argb & 0xf0f0f0u) != 0 && bloody > 1;       // footprints only at level 2
            if (bloody >= 2) d.life += 120.0f;                    // 0x6bdb28
        }
        float dx, dz;
        offset(0.0f, radius, &dx, &dz);
        d.pos = {pos.x + dx, pos.y + 0.25f, pos.z + dz};
        d.angle = (unit12(rng()) - 1.0f) * 6.2831855f - 3.1415927f;
        d.size = (size1 - size0) * (unit12(rng()) - 1.0f) + size0;
        d.argb = argb; d.delay = delay; d.keep = flag_a; d.slime = flag_b;
        decal_init(*o, d);
        delay = std::uint8_t(delay + delay_step);
    }
}

void Weather::blood_drop(std::uint32_t argb, const Vec3& pos, float size0, float size1) {
    if (water_cloud(1, argb, 0, 0x24, size0, size1, pos, 0.2f)) return;
    Obj* o = alloc(Obj::Splat);
    if (!o) return;
    Decal d;
    d.variant = std::uint8_t(((rng() >> 16) & 1u) + 1u);          // variants 1 or 2
    d.life = (float((rng() >> 1) % 0x25u) + 250.0f + (bloody > 1 ? 120.0f : 0.0f)) * 0.75f;
    d.angle = (unit12(rng()) - 1.0f) * 6.2831855f - 3.1415927f;
    d.size = (size1 - size0) * (unit12(rng()) - 1.0f) + size0;
    d.argb = argb;
    d.pos = pos;                                                  // no jitter, no +0.25
    decal_init(*o, d);
}

// FUN_00589f60's sim half (GoXx type 4): the delay, the age, the life, the footprint / slime trigger.
void Weather::decal(Obj& o, const Env& e) {
    Decal& d = o.dc;
    if (d.delay != 0) { --d.delay; return; }
    d.age += 1.0f;
    if (d.age >= d.life && !d.keep) { o.dead = true; return; }
    if (!d.foot || !e.player || !(d.age < d.life - 50.0f) || !(std::fabs(e.player_ground - d.pos.y) < 0.05f) ||
        !e.player_grounded)
        return;
    const float dx = e.player_pos.x - d.pos.x, dz = e.player_pos.z - d.pos.z, r = d.size * 0.275f;   // 0x6bd268
    if (!(dx * dx + dz * dz <= r * r)) return;
    if (!d.slime) { foot_rgb = d.argb & 0xffffffu; foot_ticks = 0x2d; }
    else { slime_ticks = 0x37; foot_rgb = 0xffffffu; }
}

std::size_t Weather::live_decals() const {
    std::size_t k = 0;
    for (const auto& o : objs_) k += !o->dead && o->kind == Obj::Splat;
    return k;
}

// FUN_00588a50 (GoXx type 3): the start delay, the age, the collision + history, then the move.
void Weather::spark(Obj& o) {
    Spark& s = o.sp;
    if (s.delay != 0) { --s.delay; return; }
    s.age += 1.0f;
    if (s.age >= s.life) { o.dead = true; return; }
    spark_collide(s);
    s.pos = {s.pos.x + s.vel.x, s.pos.y + s.vel.y, s.pos.z + s.vel.z};
    s.vel.y -= 0.0075f;
}

void spark_ribbon(const Spark& s, const Vec3& F, SparkQuad out[4]) {
    auto unit = [](Vec3 v) {
        const float l = std::sqrt(v.y * v.y + v.x * v.x + v.z * v.z);
        return l == 0.0f ? Vec3{} : Vec3{v.x / l, v.y / l, v.z / l};
    };
    for (int q = 0; q < 4; ++q) {
        const Vec3 P = s.hist[1 + 2 * q], A = s.hist[3 + 2 * q], B = q == 0 ? s.pos : P;
        const Vec3 d = unit({A.x - B.x, A.y - B.y, A.z - B.z});
        Vec3 sd = unit({F.z * d.y - F.y * d.z, F.x * d.z - F.z * d.x, F.y * d.x - F.x * d.y});   // d x F
        sd = {sd.x * s.half, sd.y * s.half, sd.z * s.half};
        out[q].p[0] = {P.x + sd.x, P.y + sd.y, P.z + sd.z};
        out[q].p[1] = {P.x - sd.x, P.y - sd.y, P.z - sd.z};
        out[q].p[2] = {A.x + sd.x, A.y + sd.y, A.z + sd.z};
        out[q].p[3] = {A.x - sd.x, A.y - sd.y, A.z - sd.z};
        out[q].v[0] = kSparkV[q]; out[q].v[1] = kSparkV[q + 1];
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
            case Obj::Streak: spark(o); break;
            case Obj::Splat: decal(o, e); break;
        }
    }
}

void Weather::sweep() {
    for (auto& o : objs_) {
        if (o->child && o->child->dead) o->child = nullptr;   // [H] a freed slot is not written
        if (o->dead && o->kind == Obj::Streak) --sparks_;     // the destructor's [0x75ae60 + 4 * type]--
    }
    objs_.erase(std::remove_if(objs_.begin(), objs_.end(), [](const std::unique_ptr<Obj>& o) { return o->dead; }),
                objs_.end());
}

std::size_t Weather::live_emitters() const {
    std::size_t k = 0;
    for (const auto& o : objs_) k += !o->dead && (o->kind == Obj::Ember || o->kind == Obj::Smoke);
    return k;
}

}  // namespace cleria::weather
