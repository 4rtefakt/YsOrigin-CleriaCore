// cleria-atan2ftest — the float arctangent every platform shares (src/cleria/platform_math, issue #46).
// On Windows: cleria::msvc_atan2f against the C library's atan2f, bit for bit, on pinned arguments, on the
// special values and on 64M random ones. Everywhere: the pinned results, and std::atan2(float, float) giving them
// (off Windows that is the atan2f defined in platform_math.cpp, ahead of the C library's).
// Usage: cleria-atan2ftest [ignored]
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

#include "cleria/platform_math.hpp"

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[atan2f] %-78s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

static float from_bits(std::uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
static std::uint32_t bits(float f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// Results of the Windows build for game-sized arguments: {y, x, atan2f(y, x)}. The first group is where the
// correctly rounded (float)atan2(double, double) differs, the second where glibc's atan2f does.
static const std::uint32_t kPinned[][3] = {
    {0xc31d6f5cu, 0xc2bb353fu, 0xc006dc6du},  // atan2f(-157.434998, -93.6039963); glibc 0xc006dc6c
    {0x40203127u, 0x43478419u, 0x3c4d884bu},  // atan2f(2.50300002, 199.516006); glibc 0x3c4d884a
    {0xc31526a8u, 0xc23e926fu, 0xbff0a332u},  // atan2f(-149.151001, -47.6430016); glibc 0xbff0a331
    {0x43309646u, 0xc253c28fu, 0x3fee582au},  // atan2f(176.587006, -52.9399986); glibc 0x3fee5829
    {0xc2b22f9eu, 0xc181b021u, 0xbfe019aau},  // atan2f(-89.0930023, -16.2110004); glibc 0xbfe019a9
    {0xc2eede35u, 0x4343f687u, 0xbf0c1f79u},  // atan2f(-119.433998, 195.962997); glibc 0xbf0c1f7a
    {0x4337befau, 0x4303420cu, 0x3f73547au},  // atan2f(183.746002, 131.257996); glibc 0x3f735479
    {0x41274396u, 0x42859a1du, 0x3e1ef613u},  // atan2f(10.4540005, 66.8010025); glibc 0x3e1ef612
    {0xc1139db2u, 0x4316b2b0u, 0xbd7a73aau},  // atan2f(-9.22599983, 150.697998); rounded double 0xbd7a73ab
    {0x3c23d70au, 0x42fdc8b4u, 0x38a54548u},  // atan2f(0.00999999978, 126.891998); rounded double 0x38a54547
    {0x3c83126fu, 0x43291ae1u, 0x38c66c76u},  // atan2f(0.0160000008, 169.104996); rounded double 0x38c66c75
    {0x3c343958u, 0x42f834bcu, 0x38b9e21fu},  // atan2f(0.0109999999, 124.102997); rounded double 0x38b9e21e
    {0x411d6c8bu, 0x4325e312u, 0x3d72a7ccu},  // atan2f(9.83899975, 165.886993); rounded double 0x3d72a7cd
    {0xc1154bc7u, 0x432f8b02u, 0xbd5984b9u},  // atan2f(-9.33100033, 175.542999); rounded double 0xbd5984ba
    {0x3fef1aa0u, 0x42001cacu, 0x3d6e9fe8u},  // atan2f(1.86800003, 32.0279999); rounded double 0x3d6e9fe9
    {0x413f0e56u, 0x43470e56u, 0x3d756ad6u},  // atan2f(11.941, 199.056); rounded double 0x3d756ad7
    {0xc0dbced9u, 0x42e98106u, 0xbd70b500u},  // atan2f(-6.86899996, 116.751999); rounded double 0xbd70b501
    {0x3b03126fu, 0x41c070a4u, 0x38ae5cf4u},  // atan2f(0.00200000009, 24.0550003); rounded double 0x38ae5cf3
    {0xbfc1cac1u, 0xc1fa20c5u, 0xc045f71cu},  // atan2f(-1.51400006, -31.2660007); rounded double 0xc045f71b
    {0x412aa7f0u, 0x4334224eu, 0x3d723f53u},  // atan2f(10.6660004, 180.134003); rounded double 0x3d723f54
    {0xc11f126fu, 0x4336d3f8u, 0xbd5e8463u},  // atan2f(-9.94200039, 182.828003); rounded double 0xbd5e8464
    {0x3c83126fu, 0x43231a1du, 0x38cdba08u},  // atan2f(0.0160000008, 163.102005); rounded double 0x38cdba07
    {0x41346e98u, 0x4347672bu, 0x3d6765dfu},  // atan2f(11.2770004, 199.403); rounded double 0x3d6765e0
    {0x409f126fu, 0x429fb7cfu, 0x3d7ea2d3u},  // atan2f(4.97100019, 79.8590012); rounded double 0x3d7ea2d4
    {0xc03978d5u, 0x423e8312u, 0xbd78ebaeu},  // atan2f(-2.898, 47.6279984); rounded double 0xbd78ebaf
    {0x4098d4feu, 0x42a13c6au, 0x3d725f97u},  // atan2f(4.77600002, 80.6179962); rounded double 0x3d725f98
    {0x410beb85u, 0x431c3687u, 0x3d650f65u},  // atan2f(8.74499989, 156.212997); rounded double 0x3d650f66
    {0xc0e74396u, 0x4301ffbeu, 0xbd637945u},  // atan2f(-7.22700024, 129.998993); rounded double 0xbd637946
    {0x3bc49ba6u, 0x4298322du, 0x38a559f7u},  // atan2f(0.00600000005, 76.0979996); rounded double 0x38a559f6
    {0x3c23d70au, 0x42e1a5e3u, 0x38b9e0d1u},  // atan2f(0.00999999978, 112.823997); rounded double 0x38b9e0d0
    {0xc30129fcu, 0x40f83958u, 0xbfc16252u},  // atan2f(-129.164001, 7.75699997); rounded double 0xbfc16251
    {0x410fa1cbu, 0x4311199au, 0x3d7d1661u},  // atan2f(8.97700024, 145.100006); rounded double 0x3d7d1662
};

// Through a volatile so the compiler cannot fold the call: this is the atan2f the engine links.
static float linked_atan2f(float y, float x) {
    volatile float vy = y, vx = x;
    return std::atan2(float(vy), float(vx));
}

int main() {
    int pinned_port = 0, pinned_linked = 0;
    for (const auto& p : kPinned) {
        const float y = from_bits(p[0]), x = from_bits(p[1]);
        if (bits(cleria::msvc_atan2f(y, x)) != p[2]) {
            std::printf("  msvc_atan2f(%.9g, %.9g) = %08x, pinned %08x\n", y, x, bits(cleria::msvc_atan2f(y, x)), p[2]);
            ++pinned_port;
        }
        if (bits(linked_atan2f(y, x)) != p[2]) {
            std::printf("  atan2f(%.9g, %.9g) = %08x, pinned %08x\n", y, x, bits(linked_atan2f(y, x)), p[2]);
            ++pinned_linked;
        }
    }
    check(pinned_port == 0, "cleria::msvc_atan2f gives the pinned Windows results");
    check(pinned_linked == 0, "std::atan2(float, float) gives the pinned Windows results");

#ifdef _WIN32
    // The C library's atan2f is the reference here.
    const float inf = std::numeric_limits<float>::infinity();
    const float specials[] = {0.0f, -0.0f, inf, -inf, 1.0f, -1.0f, 1e-45f, -1e-45f, 1.17549435e-38f, 3.4028235e38f,
                              -3.4028235e38f, 1e-30f, 1e30f, 0.5f, 2.0f, 47.512f, -0.478f, 1e-7f, 8192.0f, 1.5e-5f};
    int special_diff = 0;
    for (float y : specials)
        for (float x : specials)
            if (bits(cleria::msvc_atan2f(y, x)) != bits(atan2f(y, x))) {
                if (special_diff++ < 10)
                    std::printf("  special (%.9g, %.9g): msvc_atan2f %08x, CRT %08x\n", y, x,
                                bits(cleria::msvc_atan2f(y, x)), bits(atan2f(y, x)));
            }
    check(special_diff == 0, "the special values (zeros, infinities, extremes) match the CRT");

    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    auto rnd = [&s] { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; };
    long game_diff = 0, any_diff = 0;
    for (long i = 0; i < 32L * 1024 * 1024; ++i) {
        // Game-sized: |y|, |x| up to 512 with every mantissa.
        const std::uint64_t r = rnd();
        const float y = from_bits((std::uint32_t(r) & 0x807fffffu) | (std::uint32_t(100 + (r >> 32) % 37) << 23));
        const float x = from_bits((std::uint32_t(r >> 23) & 0x807fffffu) | (std::uint32_t(100 + (r >> 48) % 37) << 23));
        if (bits(cleria::msvc_atan2f(y, x)) != bits(atan2f(y, x)) && game_diff++ < 10)
            std::printf("  game (%.9g, %.9g): msvc_atan2f %08x, CRT %08x\n", y, x, bits(cleria::msvc_atan2f(y, x)),
                        bits(atan2f(y, x)));
        // Any finite or infinite bit pattern (NaNs are compared as NaNs: only their payload may differ).
        const std::uint64_t q = rnd();
        const float a = from_bits(std::uint32_t(q)), b = from_bits(std::uint32_t(q >> 32));
        const float m = cleria::msvc_atan2f(a, b), c = atan2f(a, b);
        if (!(std::isnan(m) && std::isnan(c)) && bits(m) != bits(c) && any_diff++ < 10)
            std::printf("  bits (%.9g, %.9g): msvc_atan2f %08x, CRT %08x\n", a, b, bits(m), bits(c));
    }
    check(game_diff == 0, "32M game-sized random arguments match the CRT bit for bit");
    check(any_diff == 0, "32M random bit patterns match the CRT bit for bit");
#endif

    std::printf("[atan2f] %s\n", fails ? "FAILED" : "all OK");
    return fails ? 1 : 0;
}
