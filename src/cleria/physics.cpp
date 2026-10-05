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

// ---- swept-sphere collide-and-slide (Fauerby/Nettle) ------------------------
namespace {

// Barycentric point-in-triangle (with a small tolerance for the plane contact).
bool point_in_tri(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 u = sub(b, a), w = sub(c, a), vp = sub(p, a);
    const float uu = dot(u, u), uw = dot(u, w), ww = dot(w, w);
    const float pu = dot(vp, u), pw = dot(vp, w);
    const float d = uu * ww - uw * uw;
    if (std::fabs(d) < 1e-12f) return false;
    const float s = (ww * pu - uw * pw) / d;
    const float t = (uu * pw - uw * pu) / d;
    return s >= -1e-4f && t >= -1e-4f && s + t <= 1.0f + 1e-4f;
}

// Lowest root of a*t^2 + b*t + c = 0 in (0, maxT]; false if none.
bool lowest_root(float a, float b, float c, float maxT, float& root) {
    if (std::fabs(a) < 1e-12f) {  // degenerate -> linear
        if (std::fabs(b) < 1e-12f) return false;
        const float t = -c / b;
        if (t > 1e-7f && t <= maxT) { root = t; return true; }
        return false;
    }
    const float det = b * b - 4 * a * c;
    if (det < 0) return false;
    const float sq = std::sqrt(det);
    float r1 = (-b - sq) / (2 * a), r2 = (-b + sq) / (2 * a);
    if (r1 > r2) std::swap(r1, r2);
    if (r1 > 1e-7f && r1 <= maxT) { root = r1; return true; }
    if (r2 > 1e-7f && r2 <= maxT) { root = r2; return true; }
    return false;
}

struct SweepHit {
    bool hit = false;
    float t = 1.0f;
    Vec3 n{0, 1, 0};  // unit contact normal (sphere-center -> contact point, reversed)
};

// Earliest collision of sphere(p0, r) swept p0 -> p0+vel against triangle abc,
// recorded into `out` only if sooner than out.t. Face, then edges + vertices.
void sweep_tri(const Vec3& p0, const Vec3& vel, float r, const Vec3& a, const Vec3& b,
               const Vec3& c, SweepHit& out) {
    const Vec3 N = normalize(cross(sub(b, a), sub(c, a)));
    const float nDotV = dot(N, vel);
    const float dist = dot(N, sub(p0, a));  // signed distance center -> plane
    float t0;
    bool embedded = false;
    if (std::fabs(nDotV) < 1e-8f) {
        if (std::fabs(dist) >= r) return;   // parallel and never touching
        embedded = true;
        t0 = 0.0f;
    } else {
        const float ta = (r - dist) / nDotV, tb = (-r - dist) / nDotV;
        const float lo = std::min(ta, tb), hi = std::max(ta, tb);
        if (lo > 1.0f || hi < 0.0f) return;  // touch window outside [0,1]
        t0 = std::clamp(lo, 0.0f, 1.0f);
    }

    float bestT = out.t;
    Vec3 bestN{};
    bool found = false;

    // 1. Face contact at t0 (earliest possible for this triangle).
    if (!embedded) {
        const Vec3 cen{p0.x + vel.x * t0, p0.y + vel.y * t0, p0.z + vel.z * t0};
        const Vec3 onPlane{cen.x - N.x * r, cen.y - N.y * r, cen.z - N.z * r};
        if (point_in_tri(onPlane, a, b, c) && t0 < bestT) {
            bestT = t0;
            bestN = N;
            found = true;
        }
    }
    // 2. Edges + vertices (the sphere clips a border, not the face interior).
    if (!found) {
        const float vLen2 = dot(vel, vel);
        auto vtx = [&](const Vec3& P) {
            const Vec3 d = sub(p0, P);
            float t;
            if (lowest_root(vLen2, 2 * dot(vel, d), dot(d, d) - r * r, bestT, t)) {
                const Vec3 cen{p0.x + vel.x * t, p0.y + vel.y * t, p0.z + vel.z * t};
                bestT = t;
                bestN = normalize(sub(cen, P));
                found = true;
            }
        };
        auto edge = [&](const Vec3& P, const Vec3& Q) {
            const Vec3 e = sub(Q, P), d = sub(p0, P);
            const float e2 = dot(e, e), eV = dot(e, vel), eD = dot(e, d);
            const float A = e2 * (-vLen2) + eV * eV;
            const float B = e2 * (2 * dot(vel, d)) - 2 * eV * eD;
            const float C = e2 * (r * r - dot(d, d)) + eD * eD;
            float t;
            if (lowest_root(A, B, C, bestT, t)) {
                const float f = (eV * t - eD) / e2;
                if (f >= 0.0f && f <= 1.0f) {
                    const Vec3 cen{p0.x + vel.x * t, p0.y + vel.y * t, p0.z + vel.z * t};
                    const Vec3 fp{P.x + e.x * f, P.y + e.y * f, P.z + e.z * f};
                    bestT = t;
                    bestN = normalize(sub(cen, fp));
                    found = true;
                }
            }
        };
        vtx(a); vtx(b); vtx(c);
        edge(a, b); edge(b, c); edge(c, a);
    }

    if (found) {
        out.hit = true;
        out.t = bestT;
        out.n = bestN;
    }
}

}  // namespace

Vec3 collide_and_slide(const RoomMesh& m, Vec3 pos, Vec3 vel, float radius) {
    const auto& idx = m.indices;
    const auto& vp = m.vertices;
    const float skin = 0.002f;  // gap kept off the surface to avoid re-embedding
    for (int iter = 0; iter < 15; ++iter) {
        if (dot(vel, vel) < 1e-12f) break;
        const Vec3 dest{pos.x + vel.x, pos.y + vel.y, pos.z + vel.z};
        SweepHit best;
        // Broadphase: the swept-sphere AABB over this segment.
        const float lox = std::min(pos.x, dest.x) - radius, hix = std::max(pos.x, dest.x) + radius;
        const float loy = std::min(pos.y, dest.y) - radius, hiy = std::max(pos.y, dest.y) + radius;
        const float loz = std::min(pos.z, dest.z) - radius, hiz = std::max(pos.z, dest.z) + radius;
        for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
            const Vec3 a = v(vp[idx[i]]), b = v(vp[idx[i + 1]]), c = v(vp[idx[i + 2]]);
            if (std::max({a.x, b.x, c.x}) < lox || std::min({a.x, b.x, c.x}) > hix) continue;
            if (std::max({a.y, b.y, c.y}) < loy || std::min({a.y, b.y, c.y}) > hiy) continue;
            if (std::max({a.z, b.z, c.z}) < loz || std::min({a.z, b.z, c.z}) > hiz) continue;
            sweep_tri(pos, vel, radius, a, b, c, best);
        }
        if (!best.hit) { pos = dest; break; }

        // Advance to just before contact, keeping the skin gap off the surface.
        const float vlen = std::sqrt(dot(vel, vel));
        const float t = std::max(0.0f, best.t - skin / std::max(vlen, 1e-6f));
        pos = {pos.x + vel.x * t, pos.y + vel.y * t, pos.z + vel.z * t};

        // Slide: project the remaining travel onto the contact plane.
        const Vec3 remain = sub(dest, pos);
        const float pen = dot(remain, best.n);
        vel = {remain.x - best.n.x * pen, remain.y - best.n.y * pen, remain.z - best.n.z * pen};
    }
    return pos;
}

bool segment_blocked(const RoomMesh& m, const Vec3& a, const Vec3& b) {
    // Moller-Trumbore against every triangle, t restricted to (0, 1).
    const Vec3 d = sub(b, a);
    const auto& idx = m.indices;
    const auto& vp = m.vertices;
    for (std::size_t i = 0; i + 2 < idx.size(); i += 3) {
        const Vec3 p0 = v(vp[idx[i]]), p1 = v(vp[idx[i + 1]]), p2 = v(vp[idx[i + 2]]);
        const Vec3 e1 = sub(p1, p0), e2 = sub(p2, p0);
        const Vec3 h = cross(d, e2);
        const float det = dot(e1, h);
        if (std::fabs(det) < 1e-9f) continue;
        const float inv = 1.0f / det;
        const Vec3 s0 = sub(a, p0);
        const float u = dot(s0, h) * inv;
        if (u < 0.0f || u > 1.0f) continue;
        const Vec3 q = cross(s0, e1);
        const float w = dot(d, q) * inv;
        if (w < 0.0f || u + w > 1.0f) continue;
        const float t = dot(e2, q) * inv;
        if (t > 1e-4f && t < 1.0f - 1e-4f) return true;
    }
    return false;
}

}  // namespace cleria
