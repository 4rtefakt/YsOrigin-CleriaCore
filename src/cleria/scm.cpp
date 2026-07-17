#include "cleria/scm.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cleria {
namespace {

constexpr float kHalfPi = 1.5707963f;

float rdf(const std::uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}
std::uint32_t rd32(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
Vec3 rdv(const std::uint8_t* p) { return {rdf(p), rdf(p + 4), rdf(p + 8)}; }

// CameraPoint, 0x1D4 bytes. Only the fields the engine reads for modes 0/1/3.
CameraPoint read_point(const std::uint8_t* p) {
    CameraPoint c;
    c.mode = rd32(p + 0x34);
    c.yaw = rdf(p + 0x38);
    c.pitch = rdf(p + 0x3C);
    c.dist = rdf(p + 0x40);
    c.flags = rd32(p + 0x44);
    c.fixed_pos = rdv(p + 0x54);
    return c;
}

}  // namespace

bool CameraRegion::contains(const Vec3& p) const {
    Vec3 q = {p.x - origin.x, p.y - origin.y, p.z - origin.z};
    if (region_yaw != 0) {  // regions may be rotated about Y
        const float c = std::cos(-region_yaw), s = std::sin(-region_yaw);
        const float x = q.x * c - q.z * s, z = q.x * s + q.z * c;
        q.x = x; q.z = z;
    }
    return q.x >= aabb_min.x && q.x <= aabb_max.x &&
           q.y >= aabb_min.y && q.y <= aabb_max.y &&
           q.z >= aabb_min.z && q.z <= aabb_max.z;
}

Scm Scm::parse(const std::vector<std::uint8_t>& b) {
    Scm out;
    // Shared sidecar container: magic[4], u32, u32 stride, u32 count, records[].
    if (b.size() < 16 || std::memcmp(b.data(), "SCM\0", 4) != 0) return out;
    const std::uint32_t stride = rd32(&b[8]);
    std::uint32_t count = rd32(&b[12]);
    if (stride < 0x410) return out;
    if (count > 64) count = 64;  // the engine's table holds 64 slots
    if (16 + std::size_t(count) * stride > b.size()) return out;

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* r = &b[16 + std::size_t(i) * stride];
        CameraRegion g;
        g.target = read_point(r);            // point A @ 0x000
        g.eye = read_point(r + 0x1D4);       // point B @ 0x1D4
        g.sticky = r[0x3A8] != 0;
        g.fov = rdf(r + 0x3B8);
        g.blend = rdf(r + 0x3BC);
        if (!(g.blend > 0.0f) || g.blend > 1.0f) g.blend = 0.015f;  // engine fallback
        const char* nm = reinterpret_cast<const char*>(r + 0x3C0);
        g.name.assign(nm, ::strnlen(nm, 0x20));
        const Vec3 a = rdv(r + 0x3E0), c = rdv(r + 0x3EC);
        g.aabb_min = {std::min(a.x, c.x), std::min(a.y, c.y), std::min(a.z, c.z)};
        g.aabb_max = {std::max(a.x, c.x), std::max(a.y, c.y), std::max(a.z, c.z)};
        g.origin = rdv(r + 0x3F8);
        g.region_yaw = rdf(r + 0x408);
        out.regions.push_back(std::move(g));
    }
    return out;
}

const CameraRegion* Scm::pick(const Vec3& p) const {
    if (regions.empty()) return nullptr;
    for (std::size_t i = 1; i < regions.size(); ++i)
        if (regions[i].contains(p)) return &regions[i];
    return &regions[0];
}

// FUN_0042b6e0, orbit branch (raw disassembly 0x42b7d0..0x42b8e7):
//   a = -PI/2 - yaw
//   if (flags) { d = ±(player - target); a -= atan2(d.z, d.x) + PI/2; }
//   eye = target + (-cos(a)*dist*cos(pitch), sin(pitch)*dist, sin(a)*dist*cos(pitch))
Vec3 scm_eye(const CameraPoint& b, const Vec3& target, const Vec3& player) {
    if (b.mode == 0) return b.fixed_pos;

    float a = -kHalfPi - b.yaw;
    if (b.flags != 0) {
        Vec3 d = {player.x - target.x, 0, player.z - target.z};
        if (!(b.flags & 1)) { d.x = -d.x; d.z = -d.z; }
        if (d.x != 0 || d.z != 0) a -= std::atan2(d.z, d.x) + kHalfPi;
    }
    const float cp = std::cos(b.pitch);
    return {target.x - std::cos(a) * b.dist * cp,
            target.y + std::sin(b.pitch) * b.dist,
            target.z + std::sin(a) * b.dist * cp};
}

Vec3 scm_eye(const CameraRegion& r, const Vec3& target, const Vec3& player) {
    return scm_eye(r.eye, target, player);
}

}  // namespace cleria
