// The float arctangent of the Windows build, for every platform (issue #46).
//
// Almost every heading in the engine is std::atan2(float, float), i.e. the C library's atan2f, and a fight feeds
// its last bit back through thousands of ticks, so Linux and macOS must return exactly what the x64 MSVC C library
// returns. Neither glibc's atan2f nor the correctly rounded (float)atan2(double, double) does: in the drive cases
// glibc's moved Toal in kishgal_freeze, the double one made ending_chain_hugo's fight longer. The Universal CRT's
// atan2f is AMD's libm algorithm (aocl-libm-ose src/ref/atan2f.c, BSD-3-Clause, Copyright (C) 2008-2026 Advanced
// Micro Devices, Inc.); this is that algorithm. cleria-atan2ftest compares it with the CRT's on the Windows runner.
//
// Off Windows the file also defines atan2f itself, ahead of the C library's, so every program that links cleria
// uses it. It needs no fused multiply-add (-ffp-contract=off, CMakeLists.txt): each step rounds as on x64.
#include "cleria/platform_math.hpp"

#include <cstdint>
#include <cstring>

namespace cleria {
namespace {

// atan(j/256) for j = 16 .. 256, as the bit patterns of the doubles in AMD's table.
constexpr std::uint64_t kAtanJby256[241] = {
    0x3faff55bb72cfde9ull, 0x3fb0f99ea71d52a6ull, 0x3fb1f86dbf082d58ull, 0x3fb2f719318a4a9aull,
    0x3fb3f59f0e7c559dull, 0x3fb4f3fd677292fbull, 0x3fb5f2324fd2d7b2ull, 0x3fb6f03bdcea4b0cull,
    0x3fb7ee182602f10eull, 0x3fb8ebc54478fb28ull, 0x3fb9e94153cfdcf1ull, 0x3fbae68a71c722b8ull,
    0x3fbbe39ebe6f07c3ull, 0x3fbce07c5c3cca32ull, 0x3fbddd21701eba6eull, 0x3fbed98c2190043aull,
    0x3fbfd5ba9aac2f6dull, 0x3fc068d584212b3dull, 0x3fc0e6adccf40881ull, 0x3fc1646541060850ull,
    0x3fc1e1fafb043726ull, 0x3fc25f6e171a535cull, 0x3fc2dcbdb2fba1ffull, 0x3fc359e8edeb99a3ull,
    0x3fc3d6eee8c6626cull, 0x3fc453cec6092a9eull, 0x3fc4d087a9da4f17ull, 0x3fc54d18ba11570aull,
    0x3fc5c9811e3ec269ull, 0x3fc645bfffb3aa73ull, 0x3fc6c1d4898933d8ull, 0x3fc73dbde8a7d201ull,
    0x3fc7b97b4bce5b02ull, 0x3fc8350be398ebc7ull, 0x3fc8b06ee2879c28ull, 0x3fc92ba37d050271ull,
    0x3fc9a6a8e96c8626ull, 0x3fca217e601081a5ull, 0x3fca9c231b403279ull, 0x3fcb1696574d780bull,
    0x3fcb90d7529260a2ull, 0x3fcc0ae54d768466ull, 0x3fcc84bf8a742e6dull, 0x3fccfe654e1d5395ull,
    0x3fcd77d5df205736ull, 0x3fcdf110864c9d9dull, 0x3fce6a148e96ec4dull, 0x3fcee2e1451d980cull,
    0x3fcf5b75f92c80ddull, 0x3fcfd3d1fc40dbe4ull, 0x3fd025fa510665b5ull, 0x3fd061eea03d6290ull,
    0x3fd09dc597d86362ull, 0x3fd0d97ee509acb3ull, 0x3fd1151a362431c9ull, 0x3fd150973a9ce546ull,
    0x3fd18bf5a30bf178ull, 0x3fd1c735212dd883ull, 0x3fd2025567e47c95ull, 0x3fd23d562b381041ull,
    0x3fd278372057ef45ull, 0x3fd2b2f7fd9b5fe2ull, 0x3fd2ed987a823cfeull, 0x3fd328184fb58951ull,
    0x3fd362773707ebcbull, 0x3fd39cb4eb76157bull, 0x3fd3d6d129271134ull, 0x3fd410cbad6c7d32ull,
    0x3fd44aa436c2af09ull, 0x3fd4845a84d0c21bull, 0x3fd4bdee586890e6ull, 0x3fd4f75f73869978ull,
    0x3fd530ad9951cd49ull, 0x3fd569d88e1b4cd7ull, 0x3fd5a2e0175e0f4eull, 0x3fd5dbc3fbbe768dull,
    0x3fd614840309cfe1ull, 0x3fd64d1ff635c1c5ull, 0x3fd685979f5fa6fdull, 0x3fd6bdeac9cbd76cull,
    0x3fd6f61941e4def0ull, 0x3fd72e22d53aa2a9ull, 0x3fd7660752817501ull, 0x3fd79dc6899118d1ull,
    0x3fd7d5604b63b3f7ull, 0x3fd80cd46a14b1d0ull, 0x3fd84422b8df95d7ull, 0x3fd87b4b0c1ebedbull,
    0x3fd8b24d394a1b25ull, 0x3fd8e92916f5cde8ull, 0x3fd91fde7cd0c662ull, 0x3fd9566d43a34907ull,
    0x3fd98cd5454d6b18ull, 0x3fd9c3165cc58107ull, 0x3fd9f93066168001ull, 0x3fda2f233e5e530bull,
    0x3fda64eec3cc23fcull, 0x3fda9a92d59e98cfull, 0x3fdad00f5422058bull, 0x3fdb056420ae9343ull,
    0x3fdb3a911da65c6cull, 0x3fdb6f962e737efbull, 0x3fdba473378624a5ull, 0x3fdbd9281e528191ull,
    0x3fdc0db4c94ec9efull, 0x3fdc42191ff11eb6ull, 0x3fdc76550aad71f8ull, 0x3fdcaa6872f3631bull,
    0x3fdcde53432c1350ull, 0x3fdd121566b7f2adull, 0x3fdd45aec9ec862bull, 0x3fdd791f5a1226f4ull,
    0x3fddac670561bb4full, 0x3fdddf85bb026974ull, 0x3fde127b6b0744afull, 0x3fde4548066cf51aull,
    0x3fde77eb7f175a34ull, 0x3fdeaa65c7cf28c4ull, 0x3fdedcb6d43f8434ull, 0x3fdf0ede98f393cfull,
    0x3fdf40dd0b541417ull, 0x3fdf72b221a4e495ull, 0x3fdfa45dd3029258ull, 0x3fdfd5e0175fdf83ull,
    0x3fe0039c73c1a40bull, 0x3fe01c341e82422dull, 0x3fe034b709250488ull, 0x3fe04d25314342e5ull,
    0x3fe0657e94db30cfull, 0x3fe07dc3324e9b38ull, 0x3fe095f30861a58full, 0x3fe0ae0e1639866cull,
    0x3fe0c6145b5b43daull, 0x3fe0de05d7aa6f7cull, 0x3fe0f5e28b67e295ull, 0x3fe10daa77307a0dull,
    0x3fe1255d9bfbd2a8ull, 0x3fe13cfbfb1b056eull, 0x3fe1548596376469ull, 0x3fe16bfa6f5137e1ull,
    0x3fe1835a88be7c13ull, 0x3fe19aa5e5299f99ull, 0x3fe1b1dc87904284ull, 0x3fe1c8fe7341f64full,
    0x3fe1e00babdefeb3ull, 0x3fe1f7043557138aull, 0x3fe20de813e823b1ull, 0x3fe224b74c1d192aull,
    0x3fe23b71e2cc9e6aull, 0x3fe25217dd17e501ull, 0x3fe268a940696da6ull, 0x3fe27f261273d1b3ull,
    0x3fe2958e59308e30ull, 0x3fe2abe21aded073ull, 0x3fe2c2215e024465ull, 0x3fe2d84c2961e48bull,
    0x3fe2ee628406cbcaull, 0x3fe30464753b090aull, 0x3fe31a52048874beull, 0x3fe3302b39b78856ull,
    0x3fe345f01cce37bbull, 0x3fe35ba0b60eccceull, 0x3fe3713d0df6c503ull, 0x3fe386c52d3db11eull,
    0x3fe39c391cd41719ull, 0x3fe3b198e5e2564aull, 0x3fe3c6e491c78dc4ull, 0x3fe3dc1c2a188504ull,
    0x3fe3f13fb89e96f4ull, 0x3fe4064f47569f48ull, 0x3fe41b4ae06fea41ull, 0x3fe430328e4b26d5ull,
    0x3fe445065b795b55ull, 0x3fe459c652badc7full, 0x3fe46e727efe4715ull, 0x3fe4830aeb5f7bfdull,
    0x3fe4978fa3269ee1ull, 0x3fe4ac00b1c71762ull, 0x3fe4c05e22de94e4ull, 0x3fe4d4a8023414e8ull,
    0x3fe4e8de5bb6ec04ull, 0x3fe4fd013b7dd17eull, 0x3fe51110adc5ed81ull, 0x3fe5250cbef1e9faull,
    0x3fe538f57b89061eull, 0x3fe54ccaf0362c8full, 0x3fe5608d29c70c34ull, 0x3fe5743c352b33b9ull,
    0x3fe587d81f732fbaull, 0x3fe59b60f5cfab9dull, 0x3fe5aed6c5909517ull, 0x3fe5c2399c244260ull,
    0x3fe5d58987169b18ull, 0x3fe5e8c6941043cfull, 0x3fe5fbf0d0d5cc49ull, 0x3fe60f084b46e05eull,
    0x3fe6220d115d7b8dull, 0x3fe634ff312d1f3bull, 0x3fe647deb8e20b8full, 0x3fe65aabb6c07b02ull,
    0x3fe66d663923e086ull, 0x3fe6800e4e7e2857ull, 0x3fe692a40556fb6aull, 0x3fe6a5276c4b0575ull,
    0x3fe6b798920b3d98ull, 0x3fe6c9f7855c3198ull, 0x3fe6dc44551553aeull, 0x3fe6ee7f10204aefull,
    0x3fe700a7c5784633ull, 0x3fe712be84295198ull, 0x3fe724c35b4fae7bull, 0x3fe736b65a172dffull,
    0x3fe748978fba8e0full, 0x3fe75a670b82d8d8ull, 0x3fe76c24dcc6c6c0ull, 0x3fe77dd112ea22c7ull,
    0x3fe78f6bbd5d315eull, 0x3fe7a0f4eb9c19a2ull, 0x3fe7b26cad2e50fdull, 0x3fe7c3d311a6092bull,
    0x3fe7d528289fa093ull, 0x3fe7e66c01c114fdull, 0x3fe7f79eacb97898ull, 0x3fe808c03940694aull,
    0x3fe819d0b7158a4cull, 0x3fe82ad036000005ull, 0x3fe83bbec5cdee22ull, 0x3fe84c9c7653f7eaull,
    0x3fe85d69576cc2c5ull, 0x3fe86e2578f87ae5ull, 0x3fe87ed0eadc5a2aull, 0x3fe88f6bbd023118ull,
    0x3fe89ff5ff57f1f7ull, 0x3fe8b06fc1cf3dfeull, 0x3fe8c0d9145cf49dull, 0x3fe8d13206f8c4caull,
    0x3fe8e17aa99cc05dull, 0x3fe8f1b30c44f167ull, 0x3fe901db3eeef187ull, 0x3fe911f35199833bull,
    0x3fe921fb54442d18ull,
};

double from_bits(std::uint64_t u) {
    double d;
    std::memcpy(&d, &u, sizeof d);
    return d;
}

}  // namespace

float msvc_atan2f(float fy, float fx) noexcept {
    constexpr double pi = 3.1415926535897932e+00;           // 0x400921fb54442d18
    constexpr double piby2 = 1.5707963267948966e+00;        // 0x3ff921fb54442d18
    constexpr double piby4 = 7.8539816339744831e-01;        // 0x3fe921fb54442d18
    constexpr double three_piby4 = 2.3561944901923449e+00;  // 0x4002d97c7f3321d2

    std::uint32_t ufx, ufy;
    std::memcpy(&ufx, &fx, 4);
    std::memcpy(&ufy, &fy, 4);
    const bool xneg = ufx & 0x80000000u, yneg = ufy & 0x80000000u;
    const std::uint32_t aufx = ufx & 0x7fffffffu, aufy = ufy & 0x7fffffffu;
    const double x = fx, y = fy;
    // The exponents are the doubles' (the CRT's, measured): a denormal float counts at its true size.
    std::uint64_t ux, uy;
    std::memcpy(&ux, &x, 8);
    std::memcpy(&uy, &y, 8);
    const int diffexp = int((uy >> 52) & 0x7ff) - int((ux >> 52) & 0x7ff);

    if (aufx > 0x7f800000u || aufy > 0x7f800000u) return fx + fy;  // a NaN
    if (aufy == 0) {                                                 // y = +-0: +-0 for x > 0, +-pi for x < 0
        if (xneg) return yneg ? float(-pi) : float(pi);
        return fy;
    }
    if (aufx == 0) return yneg ? float(-piby2) : float(piby2);
    if (diffexp > 26) return yneg ? float(-piby2) : float(piby2);  // |y / x| > 2^26
    if (diffexp < -13 && !xneg) {                                   // atan(y / x) = y / x
        if (diffexp < -150) return yneg ? -0.0f : 0.0f;
        return float(y / x);  // (AMD scales by 2^100 and back for a denormal result: the same double y / x)
    }
    if (diffexp < -26 && xneg) return yneg ? float(-pi) : float(pi);
    if (aufx == 0x7f800000u && aufy == 0x7f800000u) {
        if (xneg) return yneg ? float(-three_piby4) : float(three_piby4);
        return yneg ? float(-piby4) : float(piby4);
    }

    // 0 < v <= u, then atan(v / u) by the table or the series.
    double u = xneg ? -x : x, v = yneg ? -y : y;
    const bool swap_vu = u < v;
    if (swap_vu) {
        const double t = u;
        u = v;
        v = t;
    }
    const double vbyu = v / u;
    double q;
    if (vbyu > 0.0625) {
        const unsigned index = unsigned(256 * vbyu + 0.5);
        const double r = (256 * v - index * u) / (256 * u + index * v);
        const double s = r * r;
        q = from_bits(kAtanJby256[index - 16]) + r - r * s * 0.33333333333224095522;
    } else if (vbyu < 1.e-4) {
        q = vbyu;
    } else {
        const double s = vbyu * vbyu;
        q = vbyu - vbyu * s * (0.33333333333333170500 - s * (0.19999999999393223405 - s * 0.14285713561807169030));
    }
    if (swap_vu) q = piby2 - q;
    if (xneg) q = pi - q;
    if (yneg) q = -q;
    return float(q);
}

}  // namespace cleria

#ifndef _WIN32
extern "C" float atan2f(float y, float x) { return cleria::msvc_atan2f(y, x); }
#endif
