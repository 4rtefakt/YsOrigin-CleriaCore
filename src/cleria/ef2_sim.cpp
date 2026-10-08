#include "cleria/ef2_sim.hpp"

#include <cmath>
#include <cstring>

namespace cleria::ef2sim {
namespace {

constexpr float kK = 3.051851e-05f;           // 0x6bcfb0, ~1/32767
constexpr float kDeg = 0.017453292f;          // 0x6bd0dc

template <class T>
T rd(const std::vector<std::uint8_t>& b, std::size_t o) {
    T v{};
    if (o + sizeof(T) <= b.size()) std::memcpy(&v, b.data() + o, sizeof(T));
    return v;
}

float asfloat(std::uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
std::uint32_t asuint(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }

int sr(Rng& r) {                              // (w >> 17) - ((w >> 2) & 0x7fff)
    const std::uint32_t w = r.next();
    return int(w >> 17) - int((w >> 2) & 0x7fffu);
}
float uni(Rng& r) { return asfloat(0x3f800000u | (r.next() & 0x7fffffu)) - 1.0f; }
float rmul(Rng& r, float v) {                 // FUN_0044be50
    if (v == 0.0f) return 1.0f;
    const float x = float(sr(r)) * kK * v + 1.0f;
    return x >= 0.0f ? x : 1.0f / x;
}

Vec3 v3(float x, float y, float z) { return {x, y, z}; }
Vec3 vadd(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 vsub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 vmul(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float vdot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 vcross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float vlen(Vec3 a) { return std::sqrt(vdot(a, a)); }
Vec3 vnorm(Vec3 a) { const float l = vlen(a); return l == 0.0f ? Vec3{} : vmul(a, 1.0f / l); }

// r = x cos a + (n x x) sin a + n (n.x)(1 - cos a): the right-handed rotation (EF2SIM §3.4).
Vec3 rot_axis(Vec3 x, Vec3 n, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return vadd(vadd(vmul(x, c), vmul(vcross(n, x), s)), vmul(n, vdot(n, x) * (1.0f - c)));
}

// FUN_0044bd70: turn V about the fixed axes v̂ (A[2]), e1 (A[0]), e2 (A[1]), in that order.
Vec3 turn(Vec3 V, Vec3 A) {
    Vec3 u{0, 1, 0};
    if (V.x == 0.0f && V.z == 0.0f) u = V.y <= 0.0f ? Vec3{1, 0, 0} : Vec3{-1, 0, 0};
    const Vec3 vh = vnorm(V);
    const Vec3 e1 = vnorm(vcross(u, vh));
    const Vec3 e2 = vcross(vh, e1);
    V = rot_axis(V, vh, A.z);
    V = rot_axis(V, e1, A.x);
    V = rot_axis(V, e2, A.y);
    return V;
}

// FUN_0044ba70: a random point in the elevation / azimuth spans (degrees), radius [rmin, rmax).
Vec3 offset(Rng& r, float elev_deg, float azim_deg, float rmin, float rmax) {
    const float hx = elev_deg * kDeg * 0.5f, hy = azim_deg * kDeg * 0.5f;
    const float u1 = uni(r), u2 = uni(r);
    (void)uni(r);                                              // u3 (A.z = 0)
    const float ax = 2.0f * hx * u1 - hx, ay = 2.0f * hy * u2 - hy;
    const float rr = (rmax - rmin) * uni(r) + rmin;
    return {rr * std::cos(ax) * std::sin(ay), rr * -std::sin(ax), rr * std::cos(ax) * std::cos(ay)};
}

float smap(float v) { return v >= 0.5f ? (v - 0.5f) * 10.0f + 1.0f : 1.0f / ((0.5f - v) * 10.0f + 1.0f); }
std::uint32_t cbyte(float v) {
    const float f = v + 1.0f;
    if (f >= 2.0f || (asuint(f) & 0x80000000u)) return 255;
    return (asuint(f) >> 15) & 0xffu;
}
float turnrate(float v) { return v * 0.41887903f - 0.20943952f; }
float spinrate(float v) { return v * 1.2566371f - 0.62831855f; }

// One track at B2 + 0x19c + k*0x68: {u32 ?, u32 count, key[8]{t, value, inv_dt}}.
struct Track {
    const std::vector<std::uint8_t>* b; std::size_t o;
    std::uint32_t count() const { return rd<std::uint32_t>(*b, o + 4); }
    float t(int i) const { return rd<float>(*b, o + 8 + std::size_t(i) * 12); }
    float val(int i) const { return rd<float>(*b, o + 8 + std::size_t(i) * 12 + 4); }
    float inv(int i) const { return rd<float>(*b, o + 8 + std::size_t(i) * 12 + 8); }
};
Track track(const Ef2Emitter& E, int k) { return {&E.b2, 0x19c + std::size_t(k) * 0x68}; }
float S(const Track& tr, float t) {                            // FUN_0044bef0
    const std::uint32_t n = tr.count();
    if (n <= 1) return tr.val(0);
    if (n == 2) return tr.val(0) + (tr.val(1) - tr.val(0)) * t;
    int lo = 0, hi = int(n) - 1;
    while (lo + 1 != hi) {
        const int mid = (lo + hi) / 2;
        if (tr.t(mid) <= t) lo = mid; else hi = mid;
    }
    return tr.val(lo) + (tr.val(lo + 1) - tr.val(lo)) * ((t - tr.t(lo)) * tr.inv(lo));
}
float L2(const Track& tr, float t) { return tr.count() == 1 ? tr.val(0) : tr.val(0) + (tr.val(1) - tr.val(0)) * t; }

const Ef2Emitter& emitter(const Particle& p) { return p.fx->ef->emitters()[std::size_t(p.em)]; }

}  // namespace

M4 mul(const M4& a, const M4& b) {
    M4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j];
    return r;
}
M4 scaling(float x, float y, float z) { M4 r; r.m[0][0] = x; r.m[1][1] = y; r.m[2][2] = z; return r; }
M4 translation(float x, float y, float z) { M4 r; r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z; return r; }
M4 rot_ypr(float yaw, float pitch, float roll) {
    // D3DX: roll about z, then pitch about x, then yaw about y (row vectors: Rz * Rx * Ry).
    M4 rz, rx, ry;
    const float cr = std::cos(roll), sr2 = std::sin(roll);
    rz.m[0][0] = cr; rz.m[0][1] = sr2; rz.m[1][0] = -sr2; rz.m[1][1] = cr;
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    rx.m[1][1] = cp; rx.m[1][2] = sp; rx.m[2][1] = -sp; rx.m[2][2] = cp;
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    ry.m[0][0] = cy; ry.m[0][2] = -sy; ry.m[2][0] = sy; ry.m[2][2] = cy;
    return mul(mul(rz, rx), ry);
}
Vec3 transform_coord(const Vec3& v, const M4& m) {
    const float x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + m.m[3][0];
    const float y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + m.m[3][1];
    const float z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + m.m[3][2];
    const float w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + m.m[3][3];
    return w != 0.0f && w != 1.0f ? Vec3{x / w, y / w, z / w} : Vec3{x, y, z};
}

Vec3 depth_bias_offset(const Vec3& fx_pos, const Vec3& eye, float bias) {
    if (bias == 0.0f) return {};
    const Vec3 d{fx_pos.x - eye.x, fx_pos.y - eye.y, fx_pos.z - eye.z};
    const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (l == 0.0f) return {};
    return {d.x / l * bias, d.y / l * bias, d.z / l * bias};
}

Effect* System::spawn(int id, const Ef2& ef, const SpawnParams& sp) {
    if (!ef.ok()) return nullptr;
    auto fx = std::make_unique<Effect>();
    fx->id = id; fx->ef = &ef;
    fx->M = mul(mul(scaling(sp.scale.x, sp.scale.y, sp.scale.z), rot_ypr(sp.rot.y, sp.rot.x, sp.rot.z)),
                translation(sp.pos.x, sp.pos.y, sp.pos.z));
    fx->power = sp.power; fx->knock = sp.knock; fx->atk = sp.atk;
    fx->rtype = sp.rtype; fx->status = sp.status; fx->cls = sp.cls; fx->side = sp.side;
    fx->owner = sp.owner; fx->tag = sp.tag;
    if (!ef.emitters().empty() && ef.emitters()[0].b2.size() >= 0x10)   // +0x1f0 = B2[0]+0x0c
        fx->depth_bias = rd<float>(ef.emitters()[0].b2, 0x0c);
    Effect* e = fx.get();
    fx_.push_back(std::move(fx));
    for (const Ef2Root& a : ef.roots()) emit(*e, a.emitter, int(a.delay), nullptr);
    return e;
}

void System::emit(Effect& fx, int em, int delay, Particle* parent) {
    if (em < 0 || std::size_t(em) >= fx.ef->emitters().size()) return;
    const Ef2Emitter& E = fx.ef->emitters()[std::size_t(em)];
    const std::uint16_t flags = rd<std::uint16_t>(E.b2, 0);
    if (flags & 0x10) return;                                      // sub-effect: no shipped emitter
    const int n = rd<std::uint8_t>(E.b2, 4);
    for (int i = 0; i < n; ++i) {
        auto p = std::make_unique<Particle>();
        rng_->next();                                              // the drop-shadow probe phase
        p->world = translation(fx.M.m[3][0], fx.M.m[3][1], fx.M.m[3][2]);
        p->prev_world = p->world_pos();
        spawn_init(*p, fx, em, delay, parent);
        if (rd<std::uint8_t>(E.b2, 0x894) != 0) {                  // EFFECTHIT §3: arm a hit record
            const float s = (rd<float>(E.b1, 0x28) + rd<float>(E.b1, 0x2c) + rd<float>(E.b1, 0x30)) * 0.333333f;
            HitRecord& h = p->hit;
            h.mode = rd<std::uint8_t>(E.b2, 0x894);
            h.armed = rd<std::int32_t>(E.b2, 0x890) != 0;
            h.radius = rd<float>(E.b2, 0x884) * s;
            h.half_h = rd<float>(E.b2, 0x88c) != 0.0f ? rd<float>(E.b2, 0x88c) * s : 0.0f;
            h.rehit = rd<std::uint8_t>(E.b2, 0x896);
            h.power = fx.power; h.knock = fx.knock; h.rtype = fx.rtype; h.status = fx.status;
            h.cls = fx.cls; h.side = fx.side; h.atk = fx.atk;
        }
        if (rd<std::uint8_t>(E.b2, 0x895) & 1) p->collide = true;   // EFFECTHIT §1 0x45039e
        fx.live += 1;
        parts_.push_back(std::move(p));
    }
}

void System::spawn_init(Particle& p, Effect& fx, int em, int delay, Particle* parent) {
    const Ef2Emitter& E = fx.ef->emitters()[std::size_t(em)];
    const auto& B1 = E.b1;
    const auto& B2 = E.b2;
    Rng& r = *rng_;
    p.fx = &fx; p.em = em;
    // 1. the B1 copy
    p.mesh_time = rd<float>(B1, 0x10);
    p.colour_lock = rd<std::uint8_t>(B1, 0x17) != 0;
    p.attach = rd<std::uint8_t>(B1, 0x18);
    p.frame_counter = rd<std::uint8_t>(B1, 0x19);
    p.frame = rd<std::uint8_t>(B1, 0x1a);
    p.delay = delay & 0xffff;
    p.remaining = rd<std::int32_t>(B1, 0x24);
    p.scale = v3(rd<float>(B1, 0x28), rd<float>(B1, 0x2c), rd<float>(B1, 0x30));
    p.vel = v3(rd<float>(B1, 0x40), rd<float>(B1, 0x44), rd<float>(B1, 0x48));
    p.pos = v3(rd<float>(B1, 0x4c), rd<float>(B1, 0x50), rd<float>(B1, 0x54));
    p.rot = v3(rd<float>(B1, 0x58), rd<float>(B1, 0x5c), rd<float>(B1, 0x60));
    p.fade_rate = rd<std::int32_t>(B1, 0x6c);
    p.fade_acc = rd<std::int32_t>(B1, 0x70);
    p.uv_u = rd<float>(B1, 0x74); p.uv_v = rd<float>(B1, 0x78);
    p.diffuse = rd<std::uint32_t>(B1, 0x88); p.specular = rd<std::uint32_t>(B1, 0x8c);
    // 3. life jitter (C remainder: the sign of sr)
    const std::int32_t J = rd<std::int32_t>(B2, 0x8d4);
    if (J != 0) p.remaining += sr(r) % J;
    p.life = p.remaining;
    // 4. delay jitter (unsigned)
    const std::int16_t D = rd<std::int16_t>(B2, 0x28);
    if (D != 0) p.delay += int(r.next() % std::uint32_t(std::uint16_t(D)));
    // 5. speed
    p.speed = rmul(r, rd<float>(B1, 0x68));
    // 6. rotation jitter
    if (const float j = rd<float>(B2, 0x908); j != 0.0f) p.rot.x += float(sr(r)) * kK * j;
    if (const float j = rd<float>(B2, 0x90c); j != 0.0f) p.rot.y += float(sr(r)) * kK * j;
    if (const float j = rd<float>(B2, 0x910); j != 0.0f) p.rot.z += float(sr(r)) * kK * j;
    // 7. scale variance (negative y / z = reuse x)
    const float mx = rmul(r, rd<float>(B2, 0x8d8));
    const float my = rd<float>(B2, 0x8dc) >= 0.0f ? rmul(r, rd<float>(B2, 0x8dc)) : mx;
    const float mz = rd<float>(B2, 0x8e0) >= 0.0f ? rmul(r, rd<float>(B2, 0x8e0)) : mx;
    p.scale = v3(p.scale.x * mx, p.scale.y * my, p.scale.z * mz);
    // 8. velocity spread
    const float sa = rd<float>(B2, 0x914) != 0.0f ? float(sr(r)) * kK * rd<float>(B2, 0x914) : 0.0f;
    const float sb = rd<float>(B2, 0x918) != 0.0f ? float(sr(r)) * kK * rd<float>(B2, 0x918) : 0.0f;
    p.vel = turn(p.vel, v3(sa, sb, 0));
    // 9. spawn offset
    const float rmax = rd<float>(B2, 0x8f4);
    if (rmax != 0.0f) p.pos = vadd(p.pos, offset(r, rd<float>(B2, 0x8e4), rd<float>(B2, 0x8e8), rd<float>(B2, 0x8f0), rmax));
    // 10. homing emitters attached: start at the effect's origin
    if (rd<std::uint8_t>(B2, 0x8c9) && p.attach) p.pos = vadd(p.pos, v3(fx.M.m[3][0], fx.M.m[3][1], fx.M.m[3][2]));
    // 11. relative to the parent
    if (parent) {
        p.parent = parent;
        const std::uint8_t f = rd<std::uint8_t>(B2, 0x3d);
        const Vec3 base = (f & 0x10) ? parent->world_pos() : parent->pos;
        if (f & 2) {
            p.pos = vadd(p.pos, base);
            if (rmax != 0.0f) p.pos = vadd(p.pos, offset(r, rd<float>(B2, 0x8e4), rd<float>(B2, 0x8e8), rd<float>(B2, 0x8f0), rmax));
        }
        if (f & 4) {
            p.vel = vadd(p.vel, parent->vel);
            p.vel = turn(p.vel, v3(rd<float>(B2, 0x2c), rd<float>(B2, 0x30), rd<float>(B2, 0x34)));
            p.vel = turn(p.vel, v3(sa, sb, 0));
        }
        if (f & 8) {                                                // TransformNormal by the parent rows
            const M4& w = parent->world;
            const Vec3 v = p.vel;
            p.vel = {v.x * w.m[0][0] + v.y * w.m[1][0] + v.z * w.m[2][0],
                     v.x * w.m[0][1] + v.y * w.m[1][1] + v.z * w.m[2][1],
                     v.x * w.m[0][2] + v.y * w.m[1][2] + v.z * w.m[2][2]};
        }
    }
}

bool System::update_particle(Particle& p, bool moving) {
    const Ef2Emitter& E = emitter(p);
    if (p.remaining == 0) return false;
    if (p.delay != 0) { if (moving) p.delay -= 1; return false; }
    if (moving) {
        p.remaining -= 1;
        if (p.fx->kill && (rd<std::uint8_t>(E.b2, 0x895) & 2)) {
            p.dying = true;
            if (p.fade_rate == 0) p.fade_rate = p.fx->fade_rate ? p.fx->fade_rate : 6;
        }
        if (p.dying) {
            p.hit.armed = false;                                   // FUN_0044b5d0
            bool dead = p.fade_rate == 0;
            if (!dead) {
                p.fade_acc += p.fade_rate;
                if (p.fade_acc > 255) { p.fade_acc = 255; dead = true; }
            }
            if (dead) { p.remaining = 0; p.dead = true; return false; }
        }
    }
    const std::uint16_t flags = rd<std::uint16_t>(E.b2, 0);
    if (flags & 3) full_step(p, moving);
    else if (flags & 4) simple_step(p, moving);
    if (moving && (flags & 0x800) && !p.unloop && p.remaining == 0) p.remaining = p.life;
    return true;
}

void System::full_step(Particle& p, bool moving) {
    const Ef2Emitter& E = emitter(p);
    const auto& B2 = E.b2;
    Effect& fx = *p.fx;
    const std::uint16_t flags = rd<std::uint16_t>(B2, 0);
    const float t = (float(p.life) - float(p.remaining)) / float(p.life);
    const bool all_s = (flags & 1) != 0;
    float T[17];
    for (int k = 0; k < 17; ++k) {
        const Track tr = track(E, k);
        const bool s = all_s || k == 1 || k >= 14;
        T[k] = s ? S(tr, t) : L2(tr, t);
    }
    // colour
    int a = int(cbyte(T[1])) - p.fade_acc;
    a = int(float(a) * fx.alpha_mul);
    std::uint32_t A = a >= 0 ? (std::uint32_t(a) << 24) : 0u;
    if ((rd<std::uint8_t>(B2, 0x3d) & 1) && p.parent) A = p.parent->diffuse & 0xff000000u;
    if (!p.colour_lock) {
        p.diffuse = A | cbyte(T[2]) << 16 | cbyte(T[3]) << 8 | cbyte(T[4]);
        p.specular = cbyte(T[5]) << 16 | cbyte(T[6]) << 8 | cbyte(T[7]);
    }
    p.mult = v3(smap(T[14]), smap(T[15]), smap(T[16]));
    if (moving) {
        p.rot = vadd(p.rot, v3(spinrate(T[11]), spinrate(T[12]), spinrate(T[13])));
        const Vec3 tr{turnrate(T[8]), turnrate(T[9]), turnrate(T[10])};
        if (tr.x != 0.0f || tr.y != 0.0f || tr.z != 0.0f) p.vel = turn(p.vel, tr);
        const std::uint8_t m = rd<std::uint8_t>(B2, 0x8a2);
        if (m && (std::uint32_t(p.remaining) & m)) {
            if ((rng_->next() >> 24) < rd<std::uint8_t>(B2, 0x8a0)) {
                float x[3];
                const std::size_t lo[3] = {0x8a4, 0x8a8, 0x8ac}, hi[3] = {0x8b0, 0x8b4, 0x8b8}, cl[3] = {0x8bc, 0x8c0, 0x8c4};
                for (int k = 0; k < 3; ++k) {
                    x[k] = rd<float>(B2, lo[k]) + (rd<float>(B2, hi[k]) - rd<float>(B2, lo[k])) * uni(*rng_);
                    const float c = rd<float>(B2, cl[k]);
                    if (c != 0.0f) x[k] = x[k] < -c ? -c : x[k] > c ? c : x[k];
                }
                p.vel = turn(p.vel, v3(x[0], x[1], x[2]));
            }
        }
        p.pos = vadd(p.pos, vmul(p.vel, p.speed * smap(T[0])));
        const int age = p.remaining >= 0 ? p.life - p.remaining : p.remaining - p.life;
        p.pos.y -= rd<float>(B2, 0x898) + float(age) * rd<float>(B2, 0x89c);
    }
    // matrix and placement
    M4 M = mul(scaling(p.scale.x * p.mult.x, p.scale.y * p.mult.y, p.scale.z * p.mult.z), rot_ypr(p.rot.y, p.rot.x, p.rot.z));
    M.m[3][0] += p.pos.x; M.m[3][1] += p.pos.y; M.m[3][2] += p.pos.z;
    if (p.attach && !rd<std::uint8_t>(B2, 0x8c9)) {
        Particle* P = p.parent;
        if (P) {
            if (P->remaining == 0) { p.remaining = 0; p.parent = nullptr; p.attach = 0; return; }
            if (P->dying && !p.dying) {                            // FUN_00451d80
                p.dying = true;
                if (p.fade_rate == 0) p.fade_rate = fx.fade_rate ? fx.fade_rate : 6;
            }
            if (p.attach == 1) M = mul(M, P->world);
            else if (p.attach == 2) { M.m[3][0] += P->world.m[3][0]; M.m[3][1] += P->world.m[3][1]; M.m[3][2] += P->world.m[3][2]; }
            else if (p.attach == 3) {
                M4 R = P->world;
                R.m[3][0] = fx.M.m[3][0]; R.m[3][1] = fx.M.m[3][1]; R.m[3][2] = fx.M.m[3][2];
                M = mul(M, R);
            }
        }
    } else if (!(rd<std::uint8_t>(B2, 0x3d) & 0x10)) {
        M = mul(M, fx.M);
    }
    p.world = M;
    if (!moving) return;
    // homing
    const std::uint8_t hm = rd<std::uint8_t>(B2, 0x8c9);
    if (hm && p.life - p.remaining >= rd<std::int32_t>(B2, 0x8d0)) {
        const std::uint8_t per = rd<std::uint8_t>(B2, 0x8ca);
        if ((rng_->next() >> 24) < rd<std::uint8_t>(B2, 0x8cb) && per != 0 && p.remaining % int(per) == 0) {
            const Vec3 target = p.parent ? p.parent->pos : Vec3{};
            const Vec3 d = vnorm(vsub(target, p.pos));
            const float s = vlen(p.vel);
            if (hm == 1) p.vel = vmul(d, s);
            else if (hm == 2) p.pos = vadd(p.pos, vmul(d, s * p.speed * smap(T[0]) * (rd<float>(B2, 0x8cc) + 1.0f)));
            else if (hm == 3) {
                const float f = float(p.remaining) / float(p.life);
                const Vec3 n = vadd(vmul(d, 1.0f - f), vmul(p.vel, f));
                const float l = vlen(n);
                p.vel = l != 0.0f ? vmul(n, s / l) : n;
            }
        }
    }
    // mesh time, UV scroll, frames
    if (flags & 0x200) {
        p.mesh_time += rd<float>(B2, 0x1c);
        if (p.mesh_time >= rd<float>(B2, 0x24)) p.mesh_time = rd<std::uint8_t>(B2, 0x19) ? rd<float>(B2, 0x20) : rd<float>(B2, 0x24);
    }
    if (rd<float>(B2, 0x18c) != 0.0f || rd<float>(B2, 0x190) != 0.0f) { p.uv_u += rd<float>(B2, 0x18c); p.uv_v += rd<float>(B2, 0x190); }
    if (std::uint32_t(p.frame_counter) >= rd<std::uint32_t>(B2, 0x44)) {
        p.frame_counter = 0;
        p.frame += 1;
        if (std::uint32_t(p.frame) >= rd<std::uint32_t>(B2, 0x48)) p.frame = rd<std::uint32_t>(B2, 0x40) ? 0 : std::uint8_t(p.frame - 1);
    }
    p.frame_counter += 1;
    // children (not while dying)
    if (p.dying) return;
    for (int i = 0; i < 8; ++i) {
        const std::uint8_t c = rd<std::uint8_t>(B2, 0x194 + std::size_t(i));
        if (c == 0xff || std::size_t(c) >= fx.ef->emitters().size()) break;
        const Ef2Emitter& C = fx.ef->emitters()[c];
        const std::uint8_t mode = rd<std::uint8_t>(C.b2, 0x3c);
        const std::uint32_t period = rd<std::uint32_t>(C.b2, 0x38);
        const int age = p.remaining >= 0 ? p.life - p.remaining : p.remaining - p.life;
        switch (mode) {
            case 0: if (p.remaining == p.life - 1) emit(fx, c, 0, &p); break;
            case 1: if (p.remaining == 0) emit(fx, c, 0, &p); break;
            case 2: if (std::uint32_t(age) == period) emit(fx, c, 0, &p); break;
            case 3:
                if (p.remaining != 0) {
                    if (p.remaining == p.life - 1) emit(fx, c, 0, &p);
                    if (period != 0 && std::uint32_t(p.remaining) % period == 0) emit(fx, c, 0, &p);
                }
                break;
            default: break;
        }
    }
}

void System::simple_step(Particle& p, bool moving) {
    const Ef2Emitter& E = emitter(p);
    const auto& B2 = E.b2;
    const float t = (float(p.life) - float(p.remaining)) / float(p.life);
    int a = int(cbyte(S(track(E, 1), t))) - p.fade_acc;
    const std::uint32_t A = a >= 0 ? (std::uint32_t(a) << 24) : 0u;
    p.diffuse = A | cbyte(L2(track(E, 2), t)) << 16 | cbyte(L2(track(E, 3), t)) << 8 | cbyte(L2(track(E, 4), t));
    p.specular = cbyte(L2(track(E, 5), t)) << 16 | cbyte(L2(track(E, 6), t)) << 8 | cbyte(L2(track(E, 7), t));
    p.mult = v3(smap(L2(track(E, 14), t)), smap(L2(track(E, 15), t)), smap(L2(track(E, 16), t)));
    if (moving) {
        p.pos = vadd(p.pos, vmul(p.vel, smap(L2(track(E, 0), t))));
        const int age = p.remaining >= 0 ? p.life - p.remaining : p.remaining - p.life;
        p.pos.y -= rd<float>(B2, 0x898) + float(age) * rd<float>(B2, 0x89c);
    }
    p.world = mul(translation(p.pos.x, p.pos.y, p.pos.z), p.fx->M);
    if (moving) {
        if (std::uint32_t(p.frame_counter) >= rd<std::uint32_t>(B2, 0x44)) {
            p.frame_counter = 0;
            p.frame += 1;
            if (std::uint32_t(p.frame) >= rd<std::uint32_t>(B2, 0x48)) p.frame = rd<std::uint32_t>(B2, 0x40) ? 0 : std::uint8_t(p.frame - 1);
        }
        p.frame_counter += 1;
    }
}

void System::hit_tick(Particle& p, const std::function<bool(Particle&)>& on_hit) {
    if (!p.hit.armed) return;
    const Ef2Emitter& E = emitter(p);
    const float s = (rd<float>(E.b1, 0x28) + rd<float>(E.b1, 0x2c) + rd<float>(E.b1, 0x30)) * 0.333333f;
    const Vec3 w = p.world_pos();
    p.hit.centre = {w.x, w.y + rd<float>(E.b2, 0x888) * s, w.z};
    const Effect& fx = *p.fx;
    p.hit.radius = rd<float>(E.b2, 0x884) *
                   (fx.scale_x * p.mult.x + fx.scale_y * p.mult.y + fx.scale_z * p.mult.z) * 0.333333f;
    if (!on_hit || !on_hit(p)) return;
    if (impact(p, false)) { p.collide = false; p.hit.armed = false; }
}

// FUN_0044ebb0 (EFFECTHIT §5.5): mode 2 or a forced (world-contact) call kills the particle and
// raises the effect's kill flag; both modes then spawn the impact child B2+0x16 at the particle
// with the effect's hit fields (power 8.0 for impact id 0x8b). The child gate's "culled by the
// last draw" byte is taken as 0. [not modelled] impact id 0x194's bullet; the SEs of §5.5.
bool System::impact(Particle& p, bool force) {
    const Ef2Emitter& E = emitter(p);
    bool ret = false;
    if (!force) {
        if (p.hit.mode == 0) return false;
    }
    if (force || p.hit.mode == 2) {
        ret = true;
        p.dying = true;
        p.fx->kill = true;
        p.fx->fade_rate = p.fade_rate;
    }
    const int id = rd<std::uint16_t>(E.b2, 0x16);
    if (id != 0 && lookup_) {
        if (const Ef2* ef = lookup_(id)) {
            const Effect& fx = *p.fx;
            SpawnParams sp;
            sp.pos = p.world_pos();
            sp.power = id == 0x8b ? 8.0f : fx.power;
            sp.knock = fx.knock; sp.rtype = fx.rtype; sp.status = fx.status; sp.cls = fx.cls;
            sp.side = fx.side; sp.atk = fx.atk; sp.owner = fx.owner;
            spawn(id, *ef, sp);
        }
    }
    return ret;
}

// FUN_005645b0 0x56460b..0x564756: a world-colliding particle traces its last live step against
// the terrain; on contact it snaps there and dies through FUN_0044ebb0(force 1) (hit disarmed).
// With fx+0x1dc & 0x80 the test is only "below y = 0" ([not modelled]: no effect sets it here).
void System::collide_tick(Particle& p) {
    const Vec3 w = p.world_pos();
    if (!p.latched) { p.latched = true; p.latch = w; return; }
    if (p.fx && p.fx->set1) { p.latch = w; return; }              // [not modelled] the water set
    Vec3 at{};
    if (world_ && world_(p.latch, vsub(w, p.latch), &at)) {
        const bool died = impact(p, true);                        // the child at p+0x150 (pre-snap)
        p.world.m[3][0] = at.x; p.world.m[3][1] = at.y; p.world.m[3][2] = at.z;
        if (died) { p.collide = false; p.hit.armed = false; return; }
    }
    p.latch = p.world_pos();
}

void System::step(const std::function<bool(Particle&)>& on_hit) {
    const std::size_t nf = fx_.size();
    for (std::size_t i = 0; i < nf; ++i) {                         // FUN_00451e60
        Effect& fx = *fx_[i];
        if (fx.dead) continue;
        if (fx.live == 0) { fx.dead = true; continue; }
        if (fx.attach_parent) {
            if (fx.attach_mode == 1) fx.M = mul(fx.local, *fx.attach_parent);
            else if (fx.attach_mode == 2) {
                fx.M = fx.local;
                fx.M.m[3][0] += fx.attach_parent->m[3][0];
                fx.M.m[3][1] += fx.attach_parent->m[3][1];
                fx.M.m[3][2] += fx.attach_parent->m[3][2];
            }
        }
        if (fx.alpha_mul != fx.alpha_target) {
            if (fx.alpha_mul < fx.alpha_target) fx.alpha_mul = std::fmin(fx.alpha_mul + fx.alpha_step, fx.alpha_target);
            else fx.alpha_mul = std::fmax(fx.alpha_mul - fx.alpha_step, fx.alpha_target);
        }
    }
    const std::size_t np = parts_.size();
    for (std::size_t i = 0; i < np; ++i) {
        Particle& p = *parts_[i];
        if (p.dead) continue;
        p.prev_world = p.world_pos();
        update_particle(p, true);
        if (p.remaining == 0) { p.dead = true; continue; }
        if (p.delay != 0) continue;
        if (p.collide) collide_tick(p);
        hit_tick(p, on_hit);
    }
}

void System::kill_owned(int owner, int tag) {
    for (auto& f : fx_) {
        if (f->owner != owner || f->tag != tag) continue;
        for (auto& p : parts_) {
            if (p->fx != f.get() || p->dead) continue;
            p->fade_rate = 16; p->life = 16; p->remaining = 16;
            p->hit.armed = false;
            p->dying = true;
            f->kill = true;
            f->fade_rate = 16;
        }
        f->tag = -1;                                               // the owner's pointer is dropped
    }
}

void System::set_remaining_owned(int owner, int tag, int n) {
    for (auto& f : fx_) {
        if (f->owner != owner || f->tag != tag) continue;
        for (auto& p : parts_) if (p->fx == f.get() && !p->dead) p->remaining = n;
        f->tag = -1;
    }
}

void System::release_owned(int owner, int tag) {
    for (auto& f : fx_) {
        if (f->owner != owner || f->tag != tag) continue;
        for (auto& p : parts_) if (p->fx == f.get() && !p->dead) p->unloop = true;
        f->tag = -1;
    }
}

void System::dying_owned(int owner, int tag) {
    for (auto& f : fx_) {
        if (f->owner != owner || f->tag != tag) continue;
        kill_effect(f.get());
        f->tag = -1;
    }
}

void System::kill_effect(Effect* fx) {
    if (!fx) return;
    bool listed = false;
    for (auto& f : fx_) listed |= f.get() == fx;
    if (!listed) return;
    for (auto& p : parts_) if (p->fx == fx && !p->dead) p->dying = true;
    fx->kill = true;
}

void System::move_owned(int owner, int tag, const Vec3& pos, const Vec3& rot, const Vec3& scale) {
    for (auto& f : fx_)
        if (f->owner == owner && f->tag == tag) place(*f, pos, rot, scale);
}

void System::place(Effect& fx, const Vec3& pos, const Vec3& rot, const Vec3& scale) {
    fx.scale_x = scale.x; fx.scale_y = scale.y; fx.scale_z = scale.z;
    fx.M = mul(mul(scaling(scale.x, scale.y, scale.z), rot_ypr(rot.y, rot.x, rot.z)),
               translation(pos.x, pos.y, pos.z));
}

Particle* System::first_particle(const Effect* fx) {
    for (auto& p : parts_)
        if (p->fx == fx && !p->dead) return p.get();
    return nullptr;
}

bool System::strike(Particle& p) {
    if (!impact(p, true)) return false;
    p.collide = false; p.hit.armed = false;                        // p+0x2b0 = 0, p+0x118 = 0
    return true;
}

void System::sweep() {
    for (std::size_t i = 0; i < parts_.size();) {
        if (!parts_[i]->dead) { ++i; continue; }
        Particle* d = parts_[i].get();
        for (auto& q : parts_) if (q->parent == d) q->parent = nullptr;
        d->fx->live -= 1;
        parts_.erase(parts_.begin() + std::ptrdiff_t(i));
    }
    for (std::size_t i = 0; i < fx_.size();) {
        if (fx_[i]->dead && fx_[i]->live == 0 && !fx_[i]->held) fx_.erase(fx_.begin() + std::ptrdiff_t(i));
        else ++i;
    }
}

}  // namespace cleria::ef2sim
