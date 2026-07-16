#include "cleria/physics.hpp"

#include <algorithm>
#include <cmath>

namespace cleria {
namespace {

inline Vec3 v(const RoomVertex& r) { return {r.px, r.py, r.pz}; }
inline Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

// Closest point on triangle abc to p (Ericson, Real-Time Collision Detection).
Vec3 closest_on_tri(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab = sub(b, a), ac = sub(c, a), ap = sub(p, a);
    const float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0) return a;
    const Vec3 bp = sub(p, b);
    const float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3) return b;
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0) {
        const float t = d1 / (d1 - d3);
        return {a.x + ab.x * t, a.y + ab.y * t, a.z + ab.z * t};
    }
    const Vec3 cp = sub(p, c);
    const float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6) return c;
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0) {
        const float t = d2 / (d2 - d6);
        return {a.x + ac.x * t, a.y + ac.y * t, a.z + ac.z * t};
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) {
        const float t = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        const Vec3 bc = sub(c, b);
        return {b.x + bc.x * t, b.y + bc.y * t, b.z + bc.z * t};
    }
    const float denom = 1.0f / (va + vb + vc);
    const float w1 = vb * denom, w2 = vc * denom;
    return {a.x + ab.x * w1 + ac.x * w2, a.y + ab.y * w1 + ac.y * w2,
            a.z + ab.z * w1 + ac.z * w2};
}

}  // namespace

std::string_view surface_of(const RoomMesh& m, int tri) {
    if (tri < 0 || std::size_t(tri) >= m.tri_surface.size()) return {};
    const int s = m.tri_surface[std::size_t(tri)];
    if (s < 0 || std::size_t(s) >= m.surfaces.size()) return {};
    return m.surfaces[std::size_t(s)];
}

float floor_at(const RoomMesh& m, float x, float z, float ceiling, int* out_tri) {
    float best = kNoFloor;
    if (out_tri) *out_tri = -1;
    const auto& idx = m.indices;
    const auto& vp = m.vertices;
    for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
        const Vec3 a = v(vp[idx[i]]), b = v(vp[idx[i + 1]]), c = v(vp[idx[i + 2]]);
        // XZ bounding-box reject.
        if (x < std::min({a.x, b.x, c.x}) || x > std::max({a.x, b.x, c.x}) ||
            z < std::min({a.z, b.z, c.z}) || z > std::max({a.z, b.z, c.z}))
            continue;
        const Vec3 n = cross(sub(b, a), sub(c, a));
        if (n.y <= 0 && -n.y <= 0) continue;
        const Vec3 nn = normalize(n);
        if (std::fabs(nn.y) < 0.5f) continue;  // not floor-like
        // Barycentric in XZ.
        const float d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::fabs(d) < 1e-8f) continue;
        const float wa = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / d;
        const float wb = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / d;
        const float wc = 1.0f - wa - wb;
        if (wa < -1e-3f || wb < -1e-3f || wc < -1e-3f) continue;
        const float y = wa * a.y + wb * b.y + wc * c.y;
        if (y <= ceiling && y > best) { best = y; if (out_tri) *out_tri = int(i / 3); }
    }
    return best;
}

Vec3 resolve_walls(const RoomMesh& m, Vec3 pos, float radius, float height) {
    const auto& idx = m.indices;
    const auto& vp = m.vertices;
    // Query point at mid-capsule height.
    for (int iter = 0; iter < 4; ++iter) {
        bool moved = false;
        const Vec3 q = {pos.x, pos.y + height * 0.5f, pos.z};
        for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
            const Vec3 a = v(vp[idx[i]]), b = v(vp[idx[i + 1]]), c = v(vp[idx[i + 2]]);
            // Vertical span reject: triangle entirely above/below the capsule.
            if (std::max({a.y, b.y, c.y}) < pos.y ||
                std::min({a.y, b.y, c.y}) > pos.y + height) continue;
            // XZ proximity reject.
            if (q.x + radius < std::min({a.x, b.x, c.x}) ||
                q.x - radius > std::max({a.x, b.x, c.x}) ||
                q.z + radius < std::min({a.z, b.z, c.z}) ||
                q.z - radius > std::max({a.z, b.z, c.z})) continue;
            const Vec3 nn = normalize(cross(sub(b, a), sub(c, a)));
            if (std::fabs(nn.y) > 0.6f) continue;  // floors/ceilings don't block
            const Vec3 cp = closest_on_tri(q, a, b, c);
            const float dx = q.x - cp.x, dz = q.z - cp.z;
            const float d2 = dx * dx + dz * dz;
            if (d2 < radius * radius && d2 > 1e-9f) {
                const float d = std::sqrt(d2);
                const float push = (radius - d);
                pos.x += (dx / d) * push;
                pos.z += (dz / d) * push;
                moved = true;
            }
        }
        if (!moved) break;
    }
    return pos;
}

}  // namespace cleria
