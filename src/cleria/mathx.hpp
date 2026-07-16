// Minimal math for CleriaCore's asset pipeline.
// Matrices are row-major with the D3D row-vector convention: a point is a row
// vector transformed as p' = p * M, and hierarchy composes local-first:
// world = local * parent_world (D3DXMatrixMultiply order).
#pragma once
#include <array>
#include <cmath>
#include <cstdint>

namespace cleria {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// Row-major 4x4. m[row*4 + col].
struct Mat4 {
    std::array<float, 16> m{1, 0, 0, 0,
                            0, 1, 0, 0,
                            0, 0, 1, 0,
                            0, 0, 0, 1};

    static Mat4 identity() { return Mat4{}; }

    // Row-vector multiply: (a * b)[i][j] = sum_k a[i][k] * b[k][j].
    friend Mat4 operator*(const Mat4& a, const Mat4& b) {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
                r.m[i * 4 + j] = s;
            }
        return r;
    }

    // Transform a position (implicit w=1) as a row vector: p * M.
    Vec3 transform_point(const Vec3& p) const {
        return {
            p.x * m[0] + p.y * m[4] + p.z * m[8] + m[12],
            p.x * m[1] + p.y * m[5] + p.z * m[9] + m[13],
            p.x * m[2] + p.y * m[6] + p.z * m[10] + m[14],
        };
    }
};

inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 normalize(const Vec3& v) {
    const float l = std::sqrt(dot(v, v));
    return l > 1e-8f ? Vec3{v.x / l, v.y / l, v.z / l} : v;
}

// Left-handed look-at, row-vector convention (matches D3DXMatrixLookAtLH).
inline Mat4 look_at_lh(const Vec3& eye, const Vec3& at, const Vec3& up) {
    const Vec3 z = normalize(at - eye);
    const Vec3 x = normalize(cross(up, z));
    const Vec3 y = cross(z, x);
    Mat4 m;
    m.m = {x.x, y.x, z.x, 0,
           x.y, y.y, z.y, 0,
           x.z, y.z, z.z, 0,
           -dot(x, eye), -dot(y, eye), -dot(z, eye), 1};
    return m;
}

// Left-handed perspective, clip z in [0,1] (WebGPU/D3D). Row-vector convention.
inline Mat4 perspective_lh(float fov_y, float aspect, float zn, float zf) {
    const float ys = 1.0f / std::tan(fov_y * 0.5f);
    const float xs = ys / aspect;
    Mat4 m;
    m.m = {xs, 0, 0, 0,
           0, ys, 0, 0,
           0, 0, zf / (zf - zn), 1,
           0, 0, -zn * zf / (zf - zn), 0};
    return m;
}

// Compose a SOB placement (scale, then Euler rotation, then translation) in the
// D3D row-vector convention: world = S * R * T.
inline Mat4 compose_trs(const Vec3& t, const Vec3& rot_rad, const Vec3& s) {
    const float cx = std::cos(rot_rad.x), sx = std::sin(rot_rad.x);
    const float cy = std::cos(rot_rad.y), sy = std::sin(rot_rad.y);
    const float cz = std::cos(rot_rad.z), sz = std::sin(rot_rad.z);

    Mat4 S; S.m[0] = s.x; S.m[5] = s.y; S.m[10] = s.z;
    Mat4 Rx; Rx.m[5] = cx; Rx.m[6] = sx; Rx.m[9] = -sx; Rx.m[10] = cx;
    Mat4 Ry; Ry.m[0] = cy; Ry.m[2] = -sy; Ry.m[8] = sy; Ry.m[10] = cy;
    Mat4 Rz; Rz.m[0] = cz; Rz.m[1] = sz; Rz.m[4] = -sz; Rz.m[5] = cz;
    Mat4 T; T.m[12] = t.x; T.m[13] = t.y; T.m[14] = t.z;

    return S * (Rx * Ry * Rz) * T;
}

}  // namespace cleria
