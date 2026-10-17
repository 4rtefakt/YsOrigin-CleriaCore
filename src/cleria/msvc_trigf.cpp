// Bit-exact ports of the x64 Universal CRT's sinf, cosf and tanf, for every platform.
//
// Source: C:\Windows\System32\ucrtbase.dll, version 10.0.26100.9711 (x64). Export RVAs:
//   sinf 0xac360, cosf 0xa7f70 (large-argument helper at 0xab710), tanf 0xad740.
// Each entry starts with `cmp dword ptr [rip+...], 0; je <sse2 fallback>` on the dword at RVA 0x139e60, a CPU
// feature flag the CRT sets at startup when FMA3 is usable. These functions port the NON-zero (FMA3) branch: the
// one that runs on any FMA3 CPU (Haswell / Piledriver and later). The algorithm family is AMD's libm
// (aocl-libm-ose, BSD-3-Clause, Copyright (C) 2008-2026 Advanced Micro Devices, Inc.), but the disassembly of
// this CRT build is the authority: every vfmadd/vfnmadd below is an explicit std::fma, every other double
// operation is a plain operation in the same order (compile with -ffp-contract=off), and the Payne-Hanek
// big-argument reduction is reproduced with the same 64-bit integer arithmetic, shifts and table offsets.
//
// NaN input returns the NaN with its quiet bit set; +-inf returns 0xffc00000 (the CRT's default NaN, from its
// error path at RVA 0xe2f80, which also sets errno/calls _matherr - not reproduced here).
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cleria {
namespace {

using u64 = std::uint64_t;
using u32 = std::uint32_t;

// 0xa0 bytes of the CRT's 2/pi bit table at RVA 0x107360, as little-endian 64-bit words. The reduction reads it
// at unaligned byte offsets, so load64 assembles bytes.
constexpr u64 kTwoByPi[20] = {
    0x7421580cc11bf1e0ull, 0x4ba9afed7ec47e35ull, 0xc5ecf41ce7de294aull, 0xa8b5d49eeb1faf97ull,
    0x26dd3d18fd9a797full, 0xb47db4d9fb3c9f2cull, 0x603fbcbc462d6829ull, 0xa0ec7fe25fff7816ull,
    0xe7d272117e2ef7e4ull, 0xf904e64758e60d4cull, 0x1213a671c09ad17dull, 0x9cfba208d7d4baedull,
    0x4873f877ac72c4a6ull, 0x4b801924bba82746ull, 0x1586dc91b8e90937ull, 0x4107f9458eaf7aefull,
    0x77036d8a5664f10eull, 0x54a7f09d5f47d4d3ull, 0x00028be60db93910ull, 0x0000000000000000ull,
};

u64 load64(u64 off) {
    u64 v = 0;
    for (int i = 7; i >= 0; --i) {
        const u64 b = off + static_cast<u64>(i);
        v = (v << 8) | ((kTwoByPi[b >> 3] >> (8 * (b & 7))) & 0xff);
    }
    return v;
}

double d(u64 bits) { double r; std::memcpy(&r, &bits, 8); return r; }
u64 bits(double x) { u64 r; std::memcpy(&r, &x, 8); return r; }
float f(u32 b) { float r; std::memcpy(&r, &b, 4); return r; }
u32 bits(float x) { u32 r; std::memcpy(&r, &x, 4); return r; }

// mul r64: full 64x64 -> 128 product.
void mul128(u64 a, u64 b, u64& lo, u64& hi) {
    const u64 a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
    const u64 p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const u64 mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    lo = (mid << 32) | (p00 & 0xffffffffu);
    hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

// bsr: index of the highest set bit; leaves dst unchanged when src is 0 (what x64 hardware does).
void bsr(u64& dst, u64 src) {
    if (src == 0) return;
    u64 i = 63;
    while (!(src >> i)) --i;
    dst = i;
}

// x86 shifts mask the count to 6 bits.
u64 shl(u64 v, u64 c) { return v << (c & 63); }
u64 shr(u64 v, u64 c) { return v >> (c & 63); }

// cvttpd2dq on one double: truncation, 0x80000000 when out of int32 range or NaN.
std::int32_t cvtt(double v) {
    if (!(v > -2147483649.0 && v < 2147483648.0)) return INT32_MIN;
    return static_cast<std::int32_t>(v);
}

// Constants (bit patterns read from the image).
const double kHalf = d(0x3fe0000000000000ull);
const double kOne = d(0x3ff0000000000000ull);
const double kTwoByPiD = d(0x3fe45f306dc9c883ull);
const double kPiBy2_1 = d(0x3ff921fb54400000ull);
const double kPiBy2_1Tail = d(0x3dd0b4611a626331ull);
const double kPiBy2 = d(0x3ff921fb54442d18ull);
// sin polynomial
const double kS1 = d(0xbfc5555555555555ull), kS2 = d(0x3f81111111111111ull);
const double kS3 = d(0xbf2a01a01a01a01aull), kS4 = d(0x3ec71de3a556c734ull);
const double kSixth = d(0x3fc5555555555555ull);
// cos polynomial
const double kC1 = d(0x3fa5555555555555ull), kC2 = d(0xbf56c16c16c16c16ull);
const double kC3 = d(0x3efa01a01a01a019ull), kC4 = d(0xbe927e4fb7789f5cull);
const double kMinusHalf = d(0xbfe0000000000000ull);
// tan rational approximation
const double kT0 = d(0x3fd8a8b0da56cb17ull), kT1 = d(0xbf919dba6efd6aadull);
const double kU0 = d(0x3ff27e84a3e73a2eull), kU1 = d(0xbfe07266d7b3511bull), kU2 = d(0x3f92e29003c692d9ull);
const double kThird = d(0x3fd5555555555555ull);
const double kMinusOne = d(0xbff0000000000000ull);
// cosf big-argument helper
const double kPiBy2Hi = d(0x3ff921fb50000000ull), kPiBy2Lo = d(0x3e5110b460000000ull);
const double kPiBy2Tail = d(0x3c91a62633145c06ull);
constexpr u64 kSign = 0x8000000000000000ull;

// The CRT's NaN/inf exit (RVA 0xe2f80): inf -> default NaN, NaN -> quieted.
float special(u32 ux) {
    if ((ux & 0x7fffffu) == 0) return f(0xffc00000u);
    return f(ux | 0x400000u);
}

// Payne-Hanek reduction shared verbatim by sinf/tanf (inline) and cosf (helper at 0xab710). ax_bits is |x| as a
// double, |x| >= 2^24. Returns region, the reduced head (in units of pi/2, already normalised) as `head`, and the
// remaining low word `r9` / sign `rdx` / biased exponent `r11` that cosf's helper uses for a tail.
struct Reduced { u64 region, head, r9, rdx, r11; };

Reduced payne_hanek(u64 ax_bits) {
    u64 r11 = (ax_bits >> 52) - 0x3ff;
    u64 r10 = 0x86 - (r11 >> 3);
    const u64 w0 = load64(r10);
    const u64 m = ((ax_bits << 12) >> 12) | (1ull << 52);
    r10 += 8;
    const u64 w1 = load64(r10), w2 = load64(r10 + 8);
    u64 r8, r9, hi, lo;
    mul128(w0, m, r8, r10);
    mul128(w1, m, lo, hi);
    r11 &= 7;
    lo += r10;
    hi += (lo < r10) ? 1 : 0;
    r9 = lo;
    r10 = hi;
    r10 += w2 * m;  // low 64 bits only
    u64 rcx = 0x36 - r11;
    u64 rax = r10 >> rcx;
    const u64 cf = (r10 >> (rcx - 1)) & 1;  // last bit shifted out
    u64 rdx = 0;
    if (cf) { r10 = ~r10; r9 = ~r9; r8 = ~r8; rdx = kSign; }
    rax = (rax + cf) & 3;
    const u64 region = rax;
    rcx = r11 + 10;
    r10 = shr(shl(r10, rcx), rcx);
    rcx -= 64;
    r11 = rcx;
    bsr(rcx, r10);
    if (r10 == 0) {
        r10 = r9; r9 = r8; r8 = 0;
        bsr(rcx, r10);
        r11 -= 64;
    }
    r11 += rcx;
    rcx -= 0x34;
    if (static_cast<std::int64_t>(rcx) < 0) {
        rcx = 0 - rcx;
        rax = r9;
        r10 = shl(r10, rcx);
        r9 = shl(r9, rcx);
        rcx = 64 - rcx;
        rax = shr(rax, rcx);
        r10 |= rax;
        r8 = shr(r8, rcx);
        r9 |= r8;
    } else if (rcx != 0) {
        r8 = r10;
        r10 = shr(r10, rcx);
        r9 = shr(r9, rcx);
        rcx = 64 - rcx;
        r8 = shl(r8, rcx);
        r9 |= r8;
    }
    r11 += 0x3ff;
    r10 &= ~(1ull << 52);
    r10 |= rdx;
    r10 |= r11 << 52;
    return {region, r10, r9, rdx, r11};
}

double sin_poly(double x) {
    double x2 = x * x;
    double p = std::fma(x2, kS4, kS3);
    p = std::fma(p, x2, kS2);
    p = std::fma(p, x2, kS1);
    const double x3 = x * x2;
    return std::fma(p, x3, x);
}

// The cos kernel inside sinf: 1 - x2/2 is a fused multiply-add here.
double cos_poly_sinf(double x) {
    const double x2 = x * x;
    const double a = std::fma(x2, kMinusHalf, kOne);
    double p = std::fma(x2, kC4, kC3);
    p = std::fma(p, x2, kC2);
    p = std::fma(p, x2, kC1);
    const double x4 = x2 * x2;
    return std::fma(p, x4, a);
}

// The cos kernel inside cosf: 1 - x2*0.5 is a separate multiply and subtract.
double cos_poly_cosf(double x) {
    const double x2 = x * x;
    const double a = kOne - x2 * kHalf;
    double p = std::fma(x2, kC4, kC3);
    p = std::fma(p, x2, kC2);
    p = std::fma(p, x2, kC1);
    const double x4 = x2 * x2;
    return std::fma(p, x4, a);
}

double tan_kernel(double x) {
    const double x2 = x * x;
    const double num = std::fma(x2, kT1, kT0);
    double den = std::fma(x2, kU2, kU1);
    den = std::fma(den, x2, kU0);
    const double q = num / den;
    const double x3 = x2 * x;
    return std::fma(x3, q, x);
}

// Medium-range Cody-Waite reduction, identical in all three functions.
double reduce_medium(double ax, u64& region) {
    const std::int32_t n = cvtt(std::fma(ax, kTwoByPiD, kHalf));
    region = static_cast<u64>(static_cast<std::int64_t>(n)) & 3;
    const double dn = static_cast<double>(n);
    const double rhead = std::fma(-dn, kPiBy2_1, ax);
    const double rtail = dn * kPiBy2_1Tail;
    return rhead - rtail;
}

}  // namespace

float msvc_sinf(float x) {
    const u32 ux = bits(x);
    const u32 ax = ux & 0x7fffffffu;
    if (ax >= 0x7f800000u) return special(ux);
    const double xd = static_cast<double>(x);
    const u64 axb = bits(xd) & ~kSign;
    if (static_cast<std::int32_t>(ax) <= 0x3f490fdb) {
        if (ax < 0x3c000000u) {
            if (ax < 0x39000000u) return x;
            const double x2 = xd * xd;
            const double x3 = x2 * xd;
            return static_cast<float>(std::fma(-x3, kSixth, xd));
        }
        return static_cast<float>(sin_poly(xd));
    }
    double r;
    u64 region;
    if (ax < 0x4b800456u) {
        r = reduce_medium(d(axb), region);
    } else {
        const Reduced red = payne_hanek(axb);
        region = red.region;
        r = d(red.head) * kPiBy2;
    }
    double y = (region & 1) ? cos_poly_sinf(r) : sin_poly(r);
    if (region < 2) y = d(bits(y) ^ kSign);
    if (!(ux & 0x80000000u)) y = d(bits(y) ^ kSign);
    return static_cast<float>(y);
}

float msvc_cosf(float x) {
    const u32 ux = bits(x);
    const u32 ax = ux & 0x7fffffffu;
    if (ax >= 0x7f800000u) return special(ux);
    const double xd = static_cast<double>(x);
    const u64 axb = bits(xd) & ~kSign;
    if (static_cast<std::int32_t>(ax) <= 0x3f490fdb) {
        if (ax < 0x3c000000u) {
            if (ax < 0x39000000u) return 1.0f;
            return static_cast<float>(std::fma(-(xd * kHalf), xd, kOne));
        }
        return static_cast<float>(cos_poly_cosf(xd));
    }
    double r;
    u64 region;
    if (ax < 0x4f490fdbu) {
        r = reduce_medium(d(axb), region);
    } else {
        // Helper at RVA 0xab710: same reduction, plus a tail word and an extra-precise multiply by pi/2.
        Reduced red = payne_hanek(axb);
        region = red.region;
        const double x2h = d(red.head);
        u64 r9 = red.r9, r11 = red.r11;
        u64 rcx = r11 << 52;  // value left in rcx before `bsr rcx, r9`
        bsr(rcx, r9);
        rcx = 0 - (rcx - 64);
        r9 = shl(r9, rcx);
        r9 >>= 12;
        rcx += 0x34;
        r11 -= rcx;
        r11 <<= 52;
        r9 |= red.rdx;
        r9 |= r11;
        const double x1 = d(r9);
        const double x4 = d(bits(x2h) & 0xfffffffff8000000ull);
        const double x0 = x2h - x4;
        const double x5 = x2h * kPiBy2;
        double x3 = x4 * kPiBy2Hi;
        x3 = x3 - x5;
        x3 = std::fma(x0, kPiBy2Hi, x3);
        x3 = std::fma(x4, kPiBy2Lo, x3);
        x3 = std::fma(x0, kPiBy2Lo, x3);
        double t = x1 * kPiBy2;
        t = std::fma(x2h, kPiBy2Tail, t);
        x3 = x3 + t;
        r = x5 + x3;
    }
    double y = (region & 1) ? sin_poly(r) : cos_poly_cosf(r);
    y = d(bits(y) ^ (((region + 1) >> 1) << 63));
    return static_cast<float>(y);
}

float msvc_tanf(float x) {
    const u32 ux = bits(x);
    const u32 ax = ux & 0x7fffffffu;
    if (ax >= 0x7f800000u) return special(ux);
    const double xd = static_cast<double>(x);
    const u64 axb = bits(xd) & ~kSign;
    if (static_cast<std::int32_t>(ax) <= 0x3f490fdb) {
        if (ax < 0x39000000u) {
            if (ax < 0x32000000u) return x;
            const double x2 = xd * xd;
            const double x3 = x2 * xd;
            return static_cast<float>(std::fma(x3, kThird, xd));
        }
        return static_cast<float>(tan_kernel(xd));
    }
    double r;
    u64 region;
    if (static_cast<std::int64_t>(axb) < static_cast<std::int64_t>(0x41e921fb40000000ull)) {
        r = reduce_medium(d(axb), region);
    } else {
        const Reduced red = payne_hanek(axb);
        region = red.region;
        r = d(red.head) * kPiBy2;
    }
    double y = tan_kernel(r);
    if (region & 1) y = kMinusOne / y;
    y = d(bits(y) ^ (bits(xd) & kSign));
    return static_cast<float>(y);
}

}  // namespace cleria
