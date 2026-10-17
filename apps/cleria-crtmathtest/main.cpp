// cleria-crtmathtest — the C library functions every platform shares (src/cleria/platform_math, msvc_*.cpp).
// Everywhere: each port gives the Windows build's result on pinned arguments (taken from a Linux run of
// ending_chain_hugo where glibc's result differed), and the function the engine links (std::sin(float) and the
// like, off Windows the definitions in platform_math.cpp) gives the port's result. On Windows with FMA3 (the CRT's
// FMA3 branch, which the ports follow): every port against the CRT on random and special arguments.
// Usage: cleria-crtmathtest [ignored]
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "cleria/platform_math.hpp"

#ifdef _WIN32
#include <intrin.h>
#endif

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[crtmath] %-78s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

static float f32(std::uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
static double f64(std::uint64_t u) { double d; std::memcpy(&d, &u, 8); return d; }
static std::uint32_t b32(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return u; }
static std::uint64_t b64(double d) { std::uint64_t u; std::memcpy(&u, &d, 8); return u; }
static bool same32(float a, float b) { return b32(a) == b32(b) || (a != a && b != b); }
static bool same64(double a, double b) { return b64(a) == b64(b) || (a != a && b != b); }

// The function each id names, as the port and as the engine links it (through volatiles: no folding).
static float port_f(int id, float a, float b) {
    switch (id) {
        case 1: return cleria::msvc_sinf(a);
        case 2: return cleria::msvc_cosf(a);
        case 3: return cleria::msvc_tanf(a);
        case 4: return cleria::msvc_asinf(a);
        case 5: return cleria::msvc_acosf(a);
        case 6: return cleria::msvc_powf(a, b);
        case 7: return cleria::msvc_hypotf(a, b);
        case 8: return cleria::msvc_atan2f(a, b);
    }
    return 0;
}
static float linked_f(int id, float a, float b) {
    volatile float va = a, vb = b;
    const float x = va, y = vb;
    switch (id) {
        case 1: return std::sin(x);
        case 2: return std::cos(x);
        case 3: return std::tan(x);
        case 4: return std::asin(x);
        case 5: return std::acos(x);
        case 6: return std::pow(x, y);
        case 7: return std::hypot(x, y);
        case 8: return std::atan2(x, y);
    }
    return 0;
}
static double port_d(int id, double a, double b) {
    switch (id) {
        case 21: return cleria::msvc_sin(a);
        case 22: return cleria::msvc_cos(a);
        case 23: return cleria::msvc_tan(a);
        case 24: return cleria::msvc_asin(a);
        case 25: return cleria::msvc_acos(a);
        case 26: return cleria::msvc_pow(a, b);
        case 28: return cleria::msvc_exp(a);
        case 29: return cleria::msvc_log(a);
        case 30: return cleria::msvc_atan2(a, b);
        case 31: return cleria::msvc_log10(a);
    }
    return 0;
}
static double linked_d(int id, double a, double b) {
    volatile double va = a, vb = b;
    const double x = va, y = vb;
    switch (id) {
        case 21: return std::sin(x);
        case 22: return std::cos(x);
        case 23: return std::tan(x);
        case 24: return std::asin(x);
        case 25: return std::acos(x);
        case 26: return std::pow(x, y);
        case 28: return std::exp(x);
        case 29: return std::log(x);
        case 30: return std::atan2(x, y);
        case 31: return std::log10(x);
    }
    return 0;
}
static const char* name(int id) {
    switch (id) {
        case 1: return "sinf"; case 2: return "cosf"; case 3: return "tanf"; case 4: return "asinf"; case 5: return "acosf";
        case 6: return "powf"; case 7: return "hypotf"; case 8: return "atan2f";
        case 21: return "sin"; case 22: return "cos"; case 23: return "tan"; case 24: return "asin"; case 25: return "acos";
        case 26: return "pow"; case 28: return "exp"; case 29: return "log"; case 30: return "atan2"; case 31: return "log10";
    }
    return "?";
}

// {id, a, b, the Windows build's result}: arguments of a Linux ending_chain_hugo run where glibc returned
// something else (its result in the comment).
static const std::uint32_t kPinnedF[][4] = {
    {1, 0x3e1fbe74u, 0, 0x3e1f18c9u},           // glibc 0x3e1f18ca
    {1, 0xbeb475e7u, 0, 0xbeb0bf4du},           // glibc 0xbeb0bf4e
    {1, 0x3f389d62u, 0, 0x3f290623u},           // glibc 0x3f290624
    {2, 0x3fb46e2fu, 0, 0x3e2456a4u},           // glibc 0x3e2456a5
    {2, 0x3f7dfa72u, 0, 0x3f0c03a7u},           // glibc 0x3f0c03a6
    {2, 0xbf5ae329u, 0, 0x3f27fc84u},           // glibc 0x3f27fc85
    {3, 0x3e72670au, 0, 0x3e7708bfu},           // glibc 0x3e7708c0
    {3, 0x3e5f7c0au, 0, 0x3e631a0du},           // glibc 0x3e631a0e
    {4, 0xbeb1cd95u, 0, 0xbeb59600u},           // glibc 0xbeb59601
    {4, 0xbec7d3bau, 0, 0xbecd4878u},           // glibc 0xbecd4879
    {4, 0xbebf08d5u, 0, 0xbec3c523u},           // glibc 0xbec3c524
    {6, 0x3efe3896u, 0x3fd49ba6u, 0x3ea00a44u}, // glibc 0x3ea00a45
    {6, 0x3ef40f12u, 0x3fd49ba6u, 0x3e958e17u}, // glibc 0x3e958e18
};
static const std::uint64_t kPinnedD[][4] = {
    {21, 0x3fe5838440000000ull, 0, 0x3fe3ede64aa5d95aull},  // glibc 0x3fe3ede64aa5d959
    {21, 0x3fe8815e40000000ull, 0, 0x3fe62df11725d59dull},  // glibc 0x3fe62df11725d59e
    {22, 0x4003ae1680000000ull, 0, 0xbfe8d9af038e26e0ull},  // glibc 0xbfe8d9af038e26e1
    {22, 0x40166243d70cb829ull, 0, 0x3fe8bc806b15173dull},  // glibc 0x3fe8bc806b15173e
    {22, 0x4003c0ba00000000ull, 0, 0xbfe908641c49cb84ull},  // glibc 0xbfe908641c49cb85
    {25, 0xbfe3333380000000ull, 0, 0x4001b6e1aaebbe60ull},  // glibc 0x4001b6e1aaebbe5f
    {25, 0xbfee702b20000000ull, 0, 0x40069f7c29a2f29aull},  // glibc 0x40069f7c29a2f299
    {30, 0xbfeffe7b00000000ull, 0x3f93b94040000000ull, 0xbff8d31513a688beull},  // glibc 0xbff8d31513a688bd
    {30, 0x3fd3693200000000ull, 0x3fd714d120000000ull, 0x3fe6601fedb484e6ull},  // glibc 0x3fe6601fedb484e5
    {30, 0xbfeff64520000000ull, 0xbfa200bba0000000ull, 0xbff9b21dcc781b70ull},  // glibc 0xbff9b21dcc781b6f
};

static const int kIdsF[] = {1, 2, 3, 4, 5, 6, 7, 8};
static const int kIdsD[] = {21, 22, 23, 24, 25, 26, 28, 29, 30, 31};

// Game-like and wide arguments for a function: (a, b).
static void args_f(int id, std::mt19937_64& rng, float& a, float& b) {
    std::uniform_real_distribution<float> game(-1000.0f, 1000.0f), unit(-1.0f, 1.0f), pos(0.0f, 4.0f);
    const bool wide = (rng() & 3) == 0;
    if (wide) { a = f32(std::uint32_t(rng())); b = f32(std::uint32_t(rng())); return; }
    switch (id) {
        case 4: case 5: a = unit(rng); b = 0; break;
        case 6: a = pos(rng); b = game(rng) / 100.0f; break;
        default: a = game(rng); b = game(rng); break;
    }
}
static void args_d(int id, std::mt19937_64& rng, double& a, double& b) {
    float fa, fb;
    if ((rng() & 3) == 0) { a = f64(rng()); b = f64(rng()); return; }
    args_f(id == 24 ? 4 : id == 25 ? 5 : id == 26 ? 6 : 1, rng, fa, fb);
    a = fa; b = fb;
    if (id == 28) a = fa / 10.0;
    if (id == 29 || id == 31) a = std::fabs(a);
}

#ifdef _WIN32
// The CRT takes its FMA3 branch when the CPU and the OS have AVX2 and FMA (its flag at ucrtbase+0x139e60).
static bool crt_fma3() {
    int r[4];
    __cpuid(r, 1);
    const bool fma = (r[2] >> 12) & 1, osxsave = (r[2] >> 27) & 1, avx = (r[2] >> 28) & 1;
    if (!(fma && osxsave && avx) || (_xgetbv(0) & 6) != 6) return false;
    __cpuidex(r, 7, 0);
    return (r[1] >> 5) & 1;   // AVX2
}
#endif

int main() {
    // ---- the pins, by the port and by the linked function ----
    int bad_port = 0, bad_linked = 0;
    for (const auto& p : kPinnedF) {
        const int id = int(p[0]);
        const float a = f32(p[1]), b = f32(p[2]), want = f32(p[3]);
        if (!same32(port_f(id, a, b), want)) { ++bad_port; std::printf("  msvc_%s(%a) = %08x, pinned %08x\n", name(id), a, b32(port_f(id, a, b)), p[3]); }
        if (!same32(linked_f(id, a, b), want)) { ++bad_linked; std::printf("  linked %s(%a) = %08x, pinned %08x\n", name(id), a, b32(linked_f(id, a, b)), p[3]); }
    }
    for (const auto& p : kPinnedD) {
        const int id = int(p[0]);
        const double a = f64(p[1]), b = f64(p[2]), want = f64(p[3]);
        if (!same64(port_d(id, a, b), want)) ++bad_port;
        if (!same64(linked_d(id, a, b), want)) { ++bad_linked; std::printf("  linked %s(%a) = %016llx, pinned %016llx\n", name(id), a, (unsigned long long)b64(linked_d(id, a, b)), (unsigned long long)p[3]); }
    }
    check(bad_port == 0, "the ports give the Windows build's result on the 23 pinned arguments");
    check(bad_linked == 0, "the linked functions (std::sin, std::acos, ...) give the same 23 results");

    // ---- the linked function is the port, on game-like and wide arguments (off Windows: the overrides) ----
    std::mt19937_64 rng(46);
    int linked_diff = 0;
    for (int id : kIdsF)
        for (int i = 0; i < 20000; ++i) {
            float a, b; args_f(id, rng, a, b);
            if (!same32(port_f(id, a, b), linked_f(id, a, b)) && linked_diff++ < 8)
                std::printf("  %s(%a, %a): port %08x, linked %08x\n", name(id), a, b, b32(port_f(id, a, b)), b32(linked_f(id, a, b)));
        }
    for (int id : kIdsD)
        for (int i = 0; i < 20000; ++i) {
            double a, b; args_d(id, rng, a, b);
            if (!same64(port_d(id, a, b), linked_d(id, a, b)) && linked_diff++ < 8)
                std::printf("  %s(%a, %a): port %016llx, linked %016llx\n", name(id), a, b,
                            (unsigned long long)b64(port_d(id, a, b)), (unsigned long long)b64(linked_d(id, a, b)));
        }
#ifdef _WIN32
    if (crt_fma3()) {
        check(linked_diff == 0, "every port equals the CRT on 20,000 arguments each (18 functions)");
        // Every 4096th float bit pattern through the float functions of one argument.
        int stride_diff = 0;
        for (int id : {1, 2, 3, 4, 5})
            for (std::uint64_t u = 0; u < 0x100000000ull; u += 4093) {
                const float a = f32(std::uint32_t(u));
                if (!same32(port_f(id, a, 0), linked_f(id, a, 0)) && stride_diff++ < 8)
                    std::printf("  %s(%a): port %08x, CRT %08x\n", name(id), a, b32(port_f(id, a, 0)), b32(linked_f(id, a, 0)));
            }
        check(stride_diff == 0, "sinf / cosf / tanf / asinf / acosf equal the CRT on every 4093rd float");
    } else {
        std::printf("[crtmath] this CPU has no FMA3: the CRT takes its SSE2 branch, which the ports do not follow; skipped\n");
    }
#else
    check(linked_diff == 0, "the linked functions are the ports on 20,000 arguments each (18 functions)");
#endif

    std::printf("%s (%d failed)\n", fails ? "FAIL" : "ALL OK", fails);
    return fails ? 1 : 0;
}
