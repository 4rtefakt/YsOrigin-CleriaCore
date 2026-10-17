// Bit-exact ports of the x64 Universal CRT's asinf, acosf, asin and acos, for every platform.
//
// Source: C:\Windows\System32\ucrtbase.dll, version 10.0.26100.9711 (x64). The algorithms are AMD's libm
// (aocl-libm-ose asinf/acosf/asin/acos, BSD-3-Clause, Copyright (C) 2008-2026 Advanced Micro Devices, Inc.);
// the disassembly is the authority for operation order, constants and fused multiply-adds.
//
//   export  RVA      dispatch                                    path ported (FMA3)
//   asinf   0x4cf80  [rip+..]&3 == 3 ? 0x4cf9c : 0x4e468 (SSE2)   0x4cf9c
//   acosf   0x73890  [rip+..]&3 == 3 ? 0x738ac : 0x76700 (SSE2)   0x738ac
//   asin    0x4d1c0  [rip+..]&3 == 3 ? 0x4d1dc : 0x4cd70 (SSE2)   0x4d1dc (cold part at 0xf3e6c)
//   acos    0x4e9e0  [rip+..]&3 == 3 ? 0x4e9fc : 0x4cadc (SSE2)   0x4e9fc
//
// All four functions select their path from the same CPU-feature word; the FMA3 path is the one that runs on
// the reference PC (confirmed by an exhaustive comparison with the CRT there). Each vfmadd/vfnmadd/vfmsub of
// that path is one std::fma below; every other operation rounds separately, in the same order (build with
// -ffp-contract=off). Domain errors (|x| > 1, ±inf) return the CRT error handler's default result: the quiet
// NaN 0xffc00000 (float) / 0xfff8000000000000 (double). A NaN argument comes back quieted.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cleria {
namespace {

float f_bits(std::uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
std::uint32_t bits_f(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
double d_bits(std::uint64_t u) {
    double d;
    std::memcpy(&d, &u, 8);
    return d;
}
std::uint64_t bits_d(double d) {
    std::uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}

// Rational approximation shared by asinf and acosf: u = r * P(r) / Q(r).
float poly_f(float r) {
    float p = std::fma(-f_bits(0x3b81ce6bu), r, f_bits(0xbc5b3fe1u));
    p = std::fma(p, r, -f_bits(0x3d678bddu));
    p = std::fma(p, r, f_bits(0x3e3c94dcu));
    const float num = p * r;
    const float den = std::fma(-f_bits(0x3f561f0du), r, f_bits(0x3f8d6fa5u));
    return num / den;
}

// Rational approximation shared by asin and acos.
double poly_d(double r) {
    double p = std::fma(d_bits(0x3f0951665d321061ull), r, d_bits(0x3f51e5f887a62135ull));
    p = std::fma(p, r, -d_bits(0x3fac28d390c29690ull));
    p = std::fma(p, r, d_bits(0x3fd1a2bec1b7ef59ull));
    p = std::fma(p, r, -d_bits(0x3fdc7b297e269eacull));
    p = std::fma(p, r, d_bits(0x3fcd1e4180029834ull));
    const double num = p * r;
    double q = std::fma(d_bits(0x3fbb1a422982ce76ull), r, -d_bits(0x3fee324ab418f78dull));
    q = std::fma(q, r, d_bits(0x40062021571dccfcull));
    q = std::fma(q, r, -d_bits(0x400a4646f903cdeaull));
    q = std::fma(q, r, d_bits(0x3ff5d6b12001f228ull));
    return num / q;
}

const float kHalfF = 0.5f;
const float kOneF = 1.0f;
const double kPiby2TailD = d_bits(0x3c91a62633145c07ull);  // 6.123233995736766e-17
const double kPiby2D = d_bits(0x3ff921fb54442d18ull);
const double kPiD = d_bits(0x400921fb54442d18ull);

}  // namespace

float msvc_asinf(float x) {
    const std::uint32_t ux = bits_f(x);
    const std::uint32_t aux = ux & 0x7fffffffu;
    const std::uint32_t xexp = (ux >> 23) & 0xffu;
    if (aux > 0x7f800000u) return f_bits(ux | 0x00400000u);
    if (xexp < 0x71) return x;
    if (xexp >= 0x7f) {
        if (x == 1.0f) return f_bits(0x3fc90fdbu);
        if (x == -1.0f) return f_bits(0xbfc90fdbu);
        return f_bits(0xffc00000u);
    }
    const float ax = f_bits(aux);
    float r, s = 0.0f, v;
    bool transform;
    if (xexp < 0x7e) {
        transform = false;
        r = ax * ax;
    } else {
        transform = true;
        r = (kOneF - ax) * kHalfF;
        s = std::sqrt(r);
    }
    const float u = poly_f(r);
    if (!transform) {
        v = std::fma(u, ax, ax);
    } else {
        const float s1 = f_bits(bits_f(s) & 0xffff0000u);
        float c = std::fma(-s1, s1, r);
        c = c / (s1 + s);
        const float t = f_bits(0x33a22168u) - (c + c);    // piby2_tail - 2c
        const float hp = f_bits(0x3f490fdau);              // pi/4 head
        const float a = std::fma(u, s + s, -t);
        const float b = hp - (s1 + s1);
        v = hp - (a - b);
    }
    return (ux & 0x80000000u) ? -v : v;
}

float msvc_acosf(float x) {
    const std::uint32_t ux = bits_f(x);
    const std::uint32_t aux = ux & 0x7fffffffu;
    const std::uint32_t xexp = (ux >> 23) & 0xffu;
    if (aux > 0x7f800000u) return f_bits(ux | 0x00400000u);
    if (xexp < 0x65) return f_bits(0x3fc90fdbu);
    if (xexp >= 0x7f) {
        if (x == 1.0f) return 0.0f;
        if (x == -1.0f) return f_bits(0x40490fdbu);
        return f_bits(0xffc00000u);
    }
    const float ax = f_bits(aux);
    if (xexp < 0x7e) {
        const float r = ax * ax;
        const float u = poly_f(r);
        const double t0 = kPiby2TailD - static_cast<double>(u * x);
        const double t1 = static_cast<double>(x) - t0;
        return static_cast<float>(kPiby2D - t1);
    }
    const float r = (kOneF - ax) * kHalfF;
    const float s = std::sqrt(r);
    const float u = poly_f(r);
    if (ux & 0x80000000u) {
        const double a = static_cast<double>(s * u) - kPiby2TailD;
        const double b = a + static_cast<double>(s);
        return static_cast<float>(kPiD - (b + b));
    }
    const float s1 = f_bits(bits_f(s) & 0xffff0000u);
    float c = std::fma(-s1, s1, r);
    c = c / (s1 + s);
    const float t = std::fma(u, s + s, c + c);
    return t + (s1 + s1);
}

double msvc_asin(double x) {
    const std::uint64_t ux = bits_d(x);
    const std::uint64_t aux = ux & 0x7fffffffffffffffull;
    const unsigned xexp = static_cast<unsigned>(ux >> 52) & 0x7ffu;
    if (aux > 0x7ff0000000000000ull) return d_bits(ux | 0x0008000000000000ull);
    if (xexp < 0x3e3) return x;
    if (xexp >= 0x3ff) {
        if (x == 1.0) return kPiby2D;
        if (x == -1.0) return d_bits(0xbff921fb54442d18ull);
        return d_bits(0xfff8000000000000ull);
    }
    const double ax = d_bits(aux);
    double r, s = 0.0, v;
    bool transform;
    if (xexp < 0x3fe) {
        transform = false;
        r = ax * ax;
    } else {
        transform = true;
        r = (1.0 - ax) * 0.5;
        s = std::sqrt(r);
    }
    const double u = poly_d(r);
    if (!transform) {
        v = std::fma(u, ax, ax);
    } else {
        const double s1 = d_bits(bits_d(s) & 0xffffffff00000000ull);
        double c = std::fma(-s1, s1, r);
        c = c / (s1 + s);
        const double t = kPiby2TailD - (c + c);
        const double hp = d_bits(0x3fe921fb54442d18ull);  // pi/4 head
        const double a = std::fma(u, s + s, -t);
        const double b = hp - (s1 + s1);
        v = hp - (a - b);
    }
    return (ux >> 63) ? -v : v;
}

double msvc_acos(double x) {
    const std::uint64_t ux = bits_d(x);
    const std::uint64_t aux = ux & 0x7fffffffffffffffull;
    const unsigned xexp = static_cast<unsigned>(ux >> 52) & 0x7ffu;
    if (aux > 0x7ff0000000000000ull) return d_bits(ux | 0x0008000000000000ull);
    if (xexp < 0x3c7) return kPiby2D;
    if (xexp >= 0x3ff) {
        if (x == 1.0) return 0.0;
        if (x == -1.0) return kPiD;
        return d_bits(0xfff8000000000000ull);
    }
    const double ax = d_bits(aux);
    if (xexp < 0x3fe) {
        const double r = ax * ax;
        const double u = poly_d(r);
        const double t0 = std::fma(-x, u, kPiby2TailD);
        return kPiby2D - (x - t0);
    }
    const double r = (1.0 - ax) * 0.5;
    const double s = std::sqrt(r);
    const double u = poly_d(r);
    if (ux >> 63) {
        const double a = std::fma(u, s, -kPiby2TailD) + s;
        return kPiD - (a + a);
    }
    const double s1 = d_bits(bits_d(s) & 0xffffffff00000000ull);
    double c = std::fma(-s1, s1, r);
    c = c / (s1 + s);
    const double t = std::fma(u, s + s, c + c);
    return t + (s1 + s1);
}

}  // namespace cleria
