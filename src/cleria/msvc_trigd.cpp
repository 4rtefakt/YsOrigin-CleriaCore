// The double sin, cos and tan of the Windows build, for every platform.
//
// Bit-exact port of the x64 Universal CRT's sin/cos/tan, C:\Windows\System32\ucrtbase.dll version
// 10.0.26100.9711: sin at RVA 0xaba70, cos at RVA 0xa7730, tan at RVA 0xacc70. Each entry point tests a CPU
// dispatch flag (dword at RVA 0x139e60, set by the CRT at startup; it reads 3 on an FMA3 machine) and, when it is
// non-zero, jumps to an FMA3 path: sin 0xabdd0, cos 0xa7ac0, tan 0xacef0. That FMA3 path is what runs on every
// x64 CPU the game supports, and it is the one ported here; the SSE2 fallback (flag == 0) is not. The paths share
// two argument reductions: RVA 0xab910 (|x| < 2e7, Cody-Waite with FMA) and RVA 0xab710 (|x| >= 2e7, Payne-Hanek
// against the 2/pi bit table at RVA 0x107360); inf and NaN go to the CRT's error helper (RVA 0xe2ed8), which
// returns the default NaN 0xfff8000000000000 for +-inf and the quieted input for NaN.
//
// The algorithm is AMD's libm (aocl-libm-ose, BSD-3-Clause, Copyright (C) 2008-2026 Advanced Micro Devices, Inc.);
// the disassembly is the authority. Every vfmadd/vfnmadd/vfmsub is an explicit std::fma, every other operation is
// a plain double operation in the CRT's order, so the file needs -ffp-contract=off (or MSVC /fp:precise).
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cleria {
namespace {

inline double d(std::uint64_t b) {
    double v;
    std::memcpy(&v, &b, sizeof v);
    return v;
}

inline std::uint64_t u(double v) {
    std::uint64_t b;
    std::memcpy(&b, &v, sizeof b);
    return b;
}

// Bytes 0 .. 159 of the CRT's 2/pi bit table (RVA 0x107360), as little-endian qwords. The reduction reads
// unaligned qwords at byte offsets, so bytes are extracted by shifting, independent of host endianness.
constexpr std::uint64_t kTwoByPiBits[20] = {
    0x7421580cc11bf1e0ull, 0x4ba9afed7ec47e35ull, 0xc5ecf41ce7de294aull, 0xa8b5d49eeb1faf97ull,
    0x26dd3d18fd9a797full, 0xb47db4d9fb3c9f2cull, 0x603fbcbc462d6829ull, 0xa0ec7fe25fff7816ull,
    0xe7d272117e2ef7e4ull, 0xf904e64758e60d4cull, 0x1213a671c09ad17dull, 0x9cfba208d7d4baedull,
    0x4873f877ac72c4a6ull, 0x4b801924bba82746ull, 0x1586dc91b8e90937ull, 0x4107f9458eaf7aefull,
    0x77036d8a5664f10eull, 0x54a7f09d5f47d4d3ull, 0x00028be60db93910ull, 0x0000000000000000ull,
};

inline std::uint64_t table_qword(std::int64_t off) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        const std::int64_t p = off + i;
        const std::uint64_t byte = (kTwoByPiBits[p >> 3] >> (8 * (p & 7))) & 0xff;
        v |= byte << (8 * i);
    }
    return v;
}

// 64 x 64 -> 128 bit unsigned multiply (x86 mul).
inline void mul64(std::uint64_t a, std::uint64_t b, std::uint64_t& lo, std::uint64_t& hi) {
    const std::uint64_t a0 = a & 0xffffffffull, a1 = a >> 32, b0 = b & 0xffffffffull, b1 = b >> 32;
    const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const std::uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffull) + (p10 & 0xffffffffull);
    lo = (mid << 32) | (p00 & 0xffffffffull);
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

// x86 bsr: index of the highest set bit; the destination keeps its old value when the source is 0.
inline std::uint64_t bsr(std::uint64_t v, std::uint64_t old) {
    if (v == 0) return old;
    std::uint64_t i = 63;
    while (!(v >> i)) --i;
    return i;
}

// The CRT's handler for inf and NaN arguments (RVA 0xe2ed8).
inline double inf_nan_result(double x) {
    const std::uint64_t b = u(x);
    if ((b & 0x000fffffffffffffull) == 0) return d(0xfff8000000000000ull);
    return d(b | 0x0008000000000000ull);
}

// RVA 0xab910: x = |arg| < 2e7. r + rr = x - n*pi/2, returns n & 3.
int reduce_small(double x, double& r, double& rr) {
    const double shifter = d(0x4338000000000000ull);
    const double pio2_2 = d(0x3c91a62633145c00ull);
    const double npi2 = std::fma(x, d(0x3fe45f306dc9c883ull), shifter) - shifter;
    const int n = static_cast<int>(npi2);  // cvttpd2dq
    const double rhead = std::fma(-npi2, d(0x3ff921fb54442d18ull), x);
    const double t3 = npi2 * pio2_2;
    const double e0 = std::fma(npi2, pio2_2, -t3);
    double t5 = rhead - t3;
    double t4 = rhead - t5;
    t4 = t4 - t3;
    const double r2 = std::fma(-npi2, pio2_2, rhead);
    t5 = t5 - r2;
    t5 = t5 + t4;
    t5 = t5 - e0;
    r = r2;
    rr = std::fma(-npi2, d(0x397b839a252049c0ull), t5);
    return n & 3;
}

// RVA 0xab710: x = |arg| >= 2e7, finite. Payne-Hanek; r + rr = x - n*pi/2, returns n & 3.
int reduce_huge(double x, double& r, double& rr) {
    const std::uint64_t ux = u(x);
    std::uint64_t r11 = (ux >> 52) - 0x3ff;
    const std::int64_t off = 0x86 - static_cast<std::int64_t>(r11 >> 3);
    const std::uint64_t m = (ux & 0x000fffffffffffffull) | (1ull << 52);
    std::uint64_t r8, r9, r10, hi0, lo1, hi1;
    mul64(table_qword(off), m, r8, hi0);
    mul64(table_qword(off + 8), m, lo1, hi1);
    r11 &= 7;
    r9 = lo1 + hi0;
    r10 = hi1 + (r9 < hi0 ? 1 : 0);
    r10 += table_qword(off + 16) * m;

    std::uint64_t rcx = 0x36 - r11;
    std::uint64_t rax = r10 >> rcx;
    const std::uint64_t cf = (r10 >> (rcx - 1)) & 1;
    std::uint64_t sign = 0;
    if (cf) {
        r10 = ~r10;
        r9 = ~r9;
        r8 = ~r8;
        sign = 0x8000000000000000ull;
    }
    rax += cf;
    const int n = static_cast<int>(rax & 3);

    rcx = r11 + 10;
    r10 = (r10 << rcx) >> rcx;
    rcx -= 0x40;
    r11 = rcx;
    if (r10 != 0) {
        rcx = bsr(r10, rcx);
    } else {
        r10 = r9;
        r9 = r8;
        r8 = 0;
        rcx = bsr(r10, rcx);
        r11 -= 0x40;
    }
    r11 += rcx;
    rcx -= 0x34;
    const std::int64_t sc = static_cast<std::int64_t>(rcx);
    if (sc < 0) {
        rcx = 0 - rcx;
        rax = r9;
        r10 <<= (rcx & 63);
        r9 <<= (rcx & 63);
        rcx = 0x40 - rcx;
        rax >>= (rcx & 63);
        r10 |= rax;
        r8 >>= (rcx & 63);
        r9 |= r8;
    } else if (sc > 0) {
        r8 = r10;
        r10 >>= (rcx & 63);
        r9 >>= (rcx & 63);
        rcx = 0x40 - rcx;
        r8 <<= (rcx & 63);
        r9 |= r8;
    }
    r11 += 0x3ff;
    r10 &= ~(1ull << 52);
    rcx = r11 << 52;
    r10 |= sign;
    r10 |= rcx;
    const double hi = d(r10);
    rcx = bsr(r9, rcx);
    rcx = 0x40 - rcx;
    r9 <<= (rcx & 63);
    r9 >>= 12;
    rcx += 0x34;
    r11 -= rcx;
    r11 <<= 52;
    r9 |= sign;
    r9 |= r11;
    const double lo = d(r9);

    const double pio2 = d(0x3ff921fb54442d18ull);
    const double c1 = d(0x3ff921fb50000000ull);
    const double c2 = d(0x3e5110b460000000ull);
    const double hi4 = d(u(hi) & 0xfffffffff8000000ull);
    const double t0 = hi - hi4;
    const double t5 = hi * pio2;
    double t3 = hi4 * c1;
    t3 = t3 - t5;
    t3 = std::fma(t0, c1, t3);
    t3 = std::fma(hi4, c2, t3);
    t3 = std::fma(t0, c2, t3);
    double t4 = lo * pio2;
    t4 = std::fma(hi, d(0x3c91a62633145c06ull), t4);
    t3 = t3 + t4;
    r = t5 + t3;
    rr = (t5 - r) + t3;
    return n;
}

inline int reduce(double ax, double& r, double& rr) {
    return u(ax) >= u(2.0e7) ? reduce_huge(ax, r, rr) : reduce_small(ax, r, rr);
}

// sin polynomial coefficients S1..S6 and cos polynomial C1..C6.
const double S1 = d(0xbfc5555555555555ull), S2 = d(0x3f81111111110bb3ull), S3 = d(0xbf2a01a019e83e5cull),
             S4 = d(0x3ec71de3796cde01ull), S5 = d(0xbe5ae600b42fdfa7ull), S6 = d(0x3de5e0b2f9a43bb8ull);
const double C1 = d(0x3fa5555555555555ull), C2 = d(0xbf56c16c16c16967ull), C3 = d(0x3efa01a019f4ec91ull),
             C4 = d(0xbe927e4fa17f667bull), C5 = d(0x3e21eeb690382eecull), C6 = d(0xbda907db47258aa7ull);

// sin(r + rr), the reduced-argument kernel (sin 0xabebf / cos 0xa7c10).
inline double sin_kernel(double r, double rr) {
    const double x2 = r * r;
    double p = S6;
    p = std::fma(p, x2, S5);
    p = std::fma(p, x2, S4);
    p = std::fma(p, x2, S3);
    p = std::fma(p, x2, S2);
    const double x3 = r * x2;
    double t = x3 * p;
    t = (rr * 0.5) - t;
    t = x2 * t;
    t = t - rr;
    t = std::fma(-x3, S1, t);
    return r - t;
}

// cos(r + rr), the reduced-argument kernel (sin 0xabf20 / cos 0xa7ba6).
inline double cos_kernel(double r, double rr) {
    const double x2 = r * r;
    const double h = x2 * 0.5;
    const double w = 1.0 - h;
    double t = 1.0 - w;
    t = t - h;
    t = std::fma(-r, rr, t);
    const double x4 = x2 * x2;
    double p = C6;
    p = std::fma(p, x2, C5);
    p = std::fma(p, x2, C4);
    p = std::fma(p, x2, C3);
    p = std::fma(p, x2, C2);
    p = std::fma(p, x2, C1);
    p = std::fma(p, x4, t);
    return p + w;
}

// tan polynomial: numerator N0..N2 and denominator D0..D3.
const double TN0 = d(0x3fd7d50f6638564aull), TN1 = d(0xbf977c24c7569abbull), TN2 = d(0x3f2d5daf289c385aull);
const double TD0 = d(0x3ff1dfcb8caa40b8ull), TD1 = d(0xbfe08046499eb90full), TD2 = d(0x3f9ab0f4f80a0acfull),
             TD3 = d(0xbf2e7517ef6d98f8ull);

}  // namespace

double msvc_sin(double x) {
    const std::uint64_t ax = u(x) & 0x7fffffffffffffffull;
    if (ax < 0x3fe921fb54442d18ull) {
        if (ax < 0x3f20000000000000ull) {
            if (ax < 0x3e40000000000000ull) return x;
            const double x3 = (x * x) * x;
            return std::fma(-x3, d(0x3fc5555555555555ull), x);
        }
        const double x2 = x * x;
        double p = S6;
        p = std::fma(p, x2, S5);
        p = std::fma(p, x2, S4);
        p = std::fma(p, x2, S3);
        p = std::fma(p, x2, S2);
        const double x3 = x * x2;
        p = std::fma(p, x2, S1);
        return std::fma(x3, p, x);
    }
    if (ax >= 0x7ff0000000000000ull) return inf_nan_result(x);
    double r, rr;
    const int n = reduce(d(ax), r, rr);
    const double res = (n & 1) ? cos_kernel(r, rr) : sin_kernel(r, rr);
    std::uint64_t s = (n & 2) ? 0x8000000000000000ull : 0;
    s ^= u(x) & 0x8000000000000000ull;
    return d(u(res) ^ s);
}

double msvc_cos(double x) {
    const std::uint64_t ax = u(x) & 0x7fffffffffffffffull;
    if (ax <= 0x3fe921fb54442d18ull) {
        if (ax < 0x3f20000000000000ull) {
            if (ax < 0x3e40000000000000ull) return 1.0;
            return std::fma(-(x * 0.5), x, 1.0);
        }
        const double x2 = x * x;
        double p = C6;
        p = std::fma(p, x2, C5);
        p = std::fma(p, x2, C4);
        p = std::fma(p, x2, C3);
        p = std::fma(p, x2, C2);
        p = std::fma(p, x2, C1);
        p = std::fma(p, x2, -0.5);
        return std::fma(p, x2, 1.0);
    }
    if ((ax & 0x7ff0000000000000ull) == 0x7ff0000000000000ull) return inf_nan_result(x);
    double r, rr;
    const int n = reduce(d(ax), r, rr);
    const double res = (n & 1) ? sin_kernel(r, rr) : cos_kernel(r, rr);
    return ((n + 1) & 2) ? d(u(res) ^ 0x8000000000000000ull) : res;
}

double msvc_tan(double x) {
    const std::uint64_t ax = u(x) & 0x7fffffffffffffffull;
    const double pio4 = d(0x3fe921fb54442d18ull);
    const double pio4lo = d(0x3c81a62633145c06ull);
    if (ax < 0x3fe921fb54442d18ull) {
        double xr = x;
        double sgn = 0.0;
        if (ax > 0x3fe5c28f5c28f5c3ull) {  // |x| > 0.68
            if (x >= 0.0) {
                sgn = 1.0;
                xr = (pio4 - x) + pio4lo;
            } else {
                sgn = -1.0;
                xr = (pio4 + x) + pio4lo;
            }
        } else if (ax < 0x3e40000000000000ull) {
            return x;
        }
        const double x2 = xr * xr;
        double num = TN2;
        double den = TD3;
        den = std::fma(den, x2, TD2);
        num = std::fma(num, x2, TN1);
        den = std::fma(den, x2, TD1);
        num = std::fma(num, x2, TN0);
        den = std::fma(den, x2, TD0);
        double q = num / den;
        q = q * x2;
        double t = std::fma(xr, q, xr);
        if (sgn == 0.0) return t;
        const double v = t / (t + 1.0);
        return std::fma(-v, 2.0, 1.0) * sgn;
    }
    if (ax >= 0x7ff0000000000000ull) return inf_nan_result(x);
    double r, rr;
    const int n = reduce(d(ax), r, rr);
    double sgn = 0.0;
    if (r > 0.68) {
        sgn = 1.0;
        r = (pio4 - r) + (pio4lo - rr);
        rr = 0.0;
    } else if (!(r >= -0.68)) {
        sgn = -1.0;
        r = (pio4 + r) + (pio4lo + rr);
        rr = 0.0;
    }
    const double x2 = r * r;
    const double t = std::fma(rr * r, 2.0, x2);
    double num = TN2;
    num = std::fma(num, t, TN1);
    num = std::fma(num, t, TN0);
    double den = TD3;
    den = std::fma(den, t, TD2);
    den = std::fma(den, t, TD1);
    den = std::fma(den, t, TD0);
    double q = num / den;
    q = q * t;
    q = std::fma(r, q, rr);
    const double s = r + q;
    double res;
    if (sgn != 0.0) {
        if (n & 1) {
            const double v = s / (s - 1.0);
            res = std::fma(v, 2.0, -1.0) * sgn;
        } else {
            const double v = s / (s + 1.0);
            res = std::fma(-v, 2.0, 1.0) * sgn;
        }
    } else if (n & 1) {
        const double sh = d(u(s) & 0xffffffff00000000ull);
        double e = sh - r;
        e = q - e;
        const double inv = -1.0 / s;
        const double ih = d(u(inv) & 0xffffffff00000000ull);
        double w = std::fma(sh, ih, 1.0);
        w = std::fma(e, ih, w);
        w = std::fma(inv, w, ih);
        res = w;
    } else {
        res = s;
    }
    return d(u(res) ^ (u(x) & 0x8000000000000000ull));
}

}  // namespace cleria
