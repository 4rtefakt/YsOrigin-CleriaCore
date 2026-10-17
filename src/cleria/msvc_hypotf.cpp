// _hypotf(float, float) (C hypotf) of the x64 Universal CRT, bit for bit, on every platform.
//
// Source: C:\Windows\System32\ucrtbase.dll 10.0.26100.9711, export _hypotf at RVA 0x4e670 (body 0x4e670..0x4e77d
// plus 0xf402c..0xf4044). It has no CPU dispatch: one SSE2 body for every machine. Algorithm family: AMD libm
// (aocl-libm-ose src/ref/hypotf.c, BSD-3-Clause): widen to double, sqrt(x*x + y*y) correctly rounded in double,
// round to float. Results above FLT_MAX go through _handle_error (0x4fbb0), which returns +inf (the value it is
// given) unless the program unmasked floating-point exceptions; errno/_matherr are not modelled.
// No fused multiply-add anywhere; compile with -ffp-contract=off or /fp:precise.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cleria {
namespace {

inline std::uint64_t bits(double d) { std::uint64_t u; std::memcpy(&u, &d, 8); return u; }
inline float flt(std::uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }

}  // namespace

float msvc_hypotf(float x, float y) {
    const double xd = static_cast<double>(x), yd = static_cast<double>(y);
    const std::uint64_t ax = bits(xd) & 0x7fffffffffffffffull, ay = bits(yd) & 0x7fffffffffffffffull;
    const std::uint64_t kMant = 0x000fffffffffffffull;
    const bool xspecial = (ax >> 52) == 0x7ff, yspecial = (ay >> 52) == 0x7ff;
    if (xspecial || yspecial) {
        // Either one infinite gives +inf (even with a NaN in the other); otherwise a NaN propagates.
        if (xspecial && (ax & kMant) == 0) return flt(0x7f800000u);
        if (yspecial && (ay & kMant) == 0) return flt(0x7f800000u);
        return x * x + y * y;
    }
    const double s = std::sqrt(yd * yd + xd * xd);
    if (s > 3.4028234663852886e+38) return flt(0x7f800000u);  // above FLT_MAX (0x47efffffe0000000)
    return static_cast<float>(s);
}

}  // namespace cleria
