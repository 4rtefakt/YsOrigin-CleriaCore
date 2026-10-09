// The platform carry of the integrator FUN_0045a510 (0x45a9a4..0x45ac1c; build/ai_spec/MOVER.md §3.3).
//
// A grounded actor remembers the matrix of the collision sub-mesh it stands on (+0x570..+0x5ac). When that
// matrix differs on a later tick [EXPORT 0045a510.c:302-396; BYTES 0x45aa95..0x45aac4]:
//   local = D3DXVec3TransformCoord(pos, D3DXMatrixInverse(cached))        the actor in the platform's space
//   moved = D3DXVec3TransformCoord(local, current)
//   carry (+0x5b0..) = moved - pos, scaled to length 0.3 (0x6bd27c) when its square exceeds 0.09 (0x6bd190)
// and, when the rotation rows changed and the object's +0x298 bit 13 is clear, an angle is added to both
// +0x308 (the heading) and +0x314 (the facing) (0x45aaca..0x45ac14).
// [H] that angle: the bytes take two atan2 (FUN_0066f80a) of vectors run through the two matrices and wrap
// their difference (FUN_005eec50); the operands were not traced one by one. For a platform turning about a
// vertical axis that difference is its turn, which is what `yaw` holds (engine basis: heading h = (cos h, sin h)
// in x / z).
#pragma once
#include <cmath>

#include "cleria/mathx.hpp"

namespace cleria {

// The inverse of an affine row-vector matrix (rows 0..2 linear, row 3 the translation), as
// D3DXMatrixInverse gives for a node matrix. A singular matrix answers the identity.
inline Mat4 affine_inverse(const Mat4& a) {
    const float* m = a.m.data();
    const float c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
    const float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    Mat4 r;
    if (std::fabs(det) < 1e-12f) return r;
    const float k = 1.0f / det;
    r.m[0] = c00 * k; r.m[1] = (m[2] * m[9] - m[1] * m[10]) * k; r.m[2] = (m[1] * m[6] - m[2] * m[5]) * k;
    r.m[4] = c01 * k; r.m[5] = (m[0] * m[10] - m[2] * m[8]) * k; r.m[6] = (m[2] * m[4] - m[0] * m[6]) * k;
    r.m[8] = c02 * k; r.m[9] = (m[1] * m[8] - m[0] * m[9]) * k;  r.m[10] = (m[0] * m[5] - m[1] * m[4]) * k;
    r.m[12] = -(m[12] * r.m[0] + m[13] * r.m[4] + m[14] * r.m[8]);
    r.m[13] = -(m[12] * r.m[1] + m[13] * r.m[5] + m[14] * r.m[9]);
    r.m[14] = -(m[12] * r.m[2] + m[13] * r.m[6] + m[14] * r.m[10]);
    return r;
}

struct RiderCarry {
    Vec3 d{};        // +0x5b0..+0x5b8
    float yaw = 0;   // the add to +0x308 / +0x314; 0 when the rotation rows did not change
};

inline RiderCarry rider_carry(const Mat4& cached, const Mat4& current, const Vec3& pos) {
    RiderCarry r;
    const Mat4 step = affine_inverse(cached) * current;
    const Vec3 moved = step.transform_point(pos);
    r.d = moved - pos;
    const float q = r.d.x * r.d.x + r.d.y * r.d.y + r.d.z * r.d.z;
    if (q > 0.09f) {
        const float k = 0.3f / std::sqrt(q);
        r.d = {r.d.x * k, r.d.y * k, r.d.z * k};
    }
    bool turned = false;
    for (int i = 0; i < 12; ++i) turned |= cached.m[std::size_t(i)] != current.m[std::size_t(i)];
    if (turned) r.yaw = std::atan2(step.m[2], step.m[0]);
    return r;
}

}  // namespace cleria
