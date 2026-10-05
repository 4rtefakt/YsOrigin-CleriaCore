// Hit volumes — where an attack's hit volume sits each tick, whether it overlaps a victim, and
// the victim's re-hit bank. Spec: build/ai_spec/HITVOLUME.md (verified in the disassembly).
// The damage itself is hit.hpp.
//
//   GenerateCollision args (FUN_004494e0 -> FUN_0045e750):
//     [0] shape (0 sphere, else a flat-ended vertical cylinder)   [1] radius
//     [2] forward distance from the origin                        [3] angle offset, DEGREES
//     [4] vertical offset   [5] cylinder half-height   [6] lifetime   [7] re-hit timer
//     [8] one-shot   [9] reaction type   [10] status bits   [11] power   [12] knockback
//   (arg[2] used to be read as the radius and arg[3] as a cone; both were wrong.)
#pragma once
#include <cmath>
#include <cstdint>

#include "cleria/cos_table.hpp"
#include "cleria/mathx.hpp"

namespace cleria {

// A shape record (+0xd0 for an attacker's volume, +0x60 for a victim's body).
struct HitShape {
    float radius = 0;
    Vec3 centre{};
    float half_h = 0;   // cylinder half-height
    int shape = 0;      // 0 sphere, non-0 vertical cylinder
};

// The armed attacker block (+0xd8..+0x11c).
struct HitVolume {
    int shape = 0;
    float radius = 0, fwd = 0.75f, angle = 0, voff = 0, half_h = 0;   // angle in RADIANS
    int life = 0, rehit = 0;
    bool one_shot = false;
    int rtype = 0, status = 0;
    float power = 0, knock = 0;
    std::uint32_t token = 0;   // +0xf0: the attacker's hit serial (+0x3c4)
};

// FUN_004494e0's decode of GenerateCollision's 13 args (a[3] degrees -> radians x0.0174533).
inline HitVolume volume_from_args(const float a[13], std::uint32_t token) {
    HitVolume v;
    v.shape = int(a[0]); v.radius = a[1]; v.fwd = a[2]; v.angle = a[3] * 0.0174533f;
    v.voff = a[4]; v.half_h = a[5]; v.life = int(a[6]); v.rehit = int(a[7]);
    v.one_shot = a[8] != 0; v.rtype = int(a[9]); v.status = int(a[10]);
    v.power = a[11]; v.knock = a[12]; v.token = token;
    return v;
}

// FUN_004fed50 0x4fed8b: the centre this tick, from the attacker's ORIGIN (+0x50) and visible
// facing (+0x314). The angle is quantised to 1/256 turn (trunc toward zero, & 0xff).
inline Vec3 volume_centre(const HitVolume& v, const Vec3& origin, float facing) {
    const int i = int((v.angle + facing) * 40.743664f);   // 0x6bda38 = 256 / 2pi
    return {cos_table(i) * v.fwd + origin.x, v.voff + origin.y, cos_table(i - 0x40) * v.fwd + origin.z};
}
inline HitShape volume_shape(const HitVolume& v, const Vec3& origin, float facing) {
    return {v.radius, volume_centre(v, origin, facing), v.half_h, v.shape};
}

// A victim's body (+0x60): CASTINFO +0x30 radius, +0x58 half-height, +0x5c shape, centred
// +0x54 above/below the ORIGIN (refreshed by the integrator each tick).
inline HitShape body_shape(const Vec3& origin, float radius, float half_h, int shape, float centre_off) {
    return {radius, {origin.x, origin.y + centre_off, origin.z}, half_h, shape};
}

// FUN_004f9e90: overlap of an attacker record A with a victim record B. On a hit, *angle =
// atan2(dz, dx) from A's centre to B's. Past a cylinder's end the sphere contributes
// |cos(atan2(e, r))| — never times r — exactly as the engine computes it.
inline bool overlap(const HitShape& a, const HitShape& b, float* angle = nullptr) {
    const float dx = b.centre.x - a.centre.x, dz = b.centre.z - a.centre.z, dy = b.centre.y - a.centre.y;
    const float h2 = dx * dx + dz * dz;
    const float R2 = (a.radius + b.radius) * (a.radius + b.radius);
    auto vgap = [](float d, float h) { return d >= 0 ? d - h : -(d + h); };
    bool hit = false;
    if (b.shape == 0 && a.shape == 0) {
        hit = R2 >= dx * dx + dy * dy + dz * dz;
    } else if (b.shape == 0) {                     // A cylinder, B sphere
        if (!(R2 < h2)) {
            const float e = vgap(dy, a.half_h);
            if (e <= 0) hit = true;
            else if (!(b.radius < e)) {
                const float w = float(std::fabs(std::cos(std::atan2(double(e), double(b.radius))))) + a.radius;
                hit = w * w >= h2;
            }
        }
    } else if (a.shape == 0) {                     // A sphere, B cylinder
        if (!(R2 < h2)) {
            const float e = vgap(dy, b.half_h);
            if (e <= 0) hit = true;
            else if (!(a.radius < e)) {
                const float w = float(std::fabs(std::cos(std::atan2(double(e), double(a.radius))))) + b.radius;
                hit = w * w >= h2;
            }
        }
    } else {                                       // cylinder-cylinder
        hit = R2 >= h2 && std::fabs(dy) <= a.half_h + b.half_h;
    }
    if (hit && angle) *angle = float(std::atan2(double(dz), double(dx)));
    return hit;
}

// FUN_004faac0 + FUN_0045c490: the victim's 10-slot re-hit bank of {token, timer}.
struct RehitBank {
    struct Slot { std::uint32_t token = 0; int timer = 0; };
    Slot slot[10];
    // True = "already hit by this token" (reject). Otherwise records it (if a slot is free).
    bool check_and_record(std::uint32_t token, int rehit) {
        for (const Slot& s : slot) if (s.timer != 0 && s.token == token) return true;
        for (Slot& s : slot) if (s.timer == 0) { s = {token, rehit}; return false; }
        return false;                                  // full: allowed, not recorded
    }
    void tick() { for (Slot& s : slot) if (s.timer > 0) --s.timer; }
};

// The global hit-serial counter DAT_007d9b78: ++c; (c & 0xffffff) | 0x1000000.
inline std::uint32_t next_hit_serial(std::uint32_t& counter) {
    ++counter;
    return (counter & 0xffffffu) | 0x1000000u;
}

}  // namespace cleria
