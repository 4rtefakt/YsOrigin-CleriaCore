// atan2(double, double) of the x64 Universal CRT, bit for bit, on every platform.
//
// Source: C:\Windows\System32\ucrtbase.dll 10.0.26100.9711, export atan2 at RVA 0x4d400. The entry tests the CPU
// feature word at RVA 0x139e60 ((v & 3) == 3, i.e. AVX2/FMA3 present) and jumps to the FMA3 body at RVA 0x4d420,
// otherwise to an SSE2 body at RVA 0x4d950. The word reads 3 on the reference PC, so this is the FMA3 body
// (with its out-of-line pieces at 0xf3eda..0xf3f7f, scaleUpDouble1024 at 0x4def0 and scaleDownDouble at 0x937b4).
// A Windows machine without FMA3 would run the SSE2 body, which this file does not reproduce.
// Algorithm family: AMD libm (aocl-libm-ose src/ref/atan2.c, BSD-3-Clause, Copyright (C) 2008-2026 Advanced Micro
// Devices, Inc.); the disassembly is the authority. Every vfmadd/vfnmadd is a std::fma below, every other operation
// rounds on its own (compile with -ffp-contract=off / /fp:precise). Floating-point exception flags are not modelled.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cleria {
namespace {

inline std::uint64_t bits(double d) { std::uint64_t u; std::memcpy(&u, &d, 8); return u; }
inline double dbl(std::uint64_t u) { double d; std::memcpy(&d, &u, 8); return d; }

// atan(j/256) for j = 16 .. 256: head (low 32 bits zero) and tail.
constexpr std::uint64_t kAtanLead[241] = {  // rva 0x112490
    0x3faff55b00000000ull, 0x3fb0f99e00000000ull, 0x3fb1f86d00000000ull, 0x3fb2f71900000000ull,
    0x3fb3f59f00000000ull, 0x3fb4f3fd00000000ull, 0x3fb5f23200000000ull, 0x3fb6f03b00000000ull,
    0x3fb7ee1800000000ull, 0x3fb8ebc500000000ull, 0x3fb9e94100000000ull, 0x3fbae68a00000000ull,
    0x3fbbe39e00000000ull, 0x3fbce07c00000000ull, 0x3fbddd2100000000ull, 0x3fbed98c00000000ull,
    0x3fbfd5ba00000000ull, 0x3fc068d500000000ull, 0x3fc0e6ad00000000ull, 0x3fc1646500000000ull,
    0x3fc1e1fa00000000ull, 0x3fc25f6e00000000ull, 0x3fc2dcbd00000000ull, 0x3fc359e800000000ull,
    0x3fc3d6ee00000000ull, 0x3fc453ce00000000ull, 0x3fc4d08700000000ull, 0x3fc54d1800000000ull,
    0x3fc5c98100000000ull, 0x3fc645bf00000000ull, 0x3fc6c1d400000000ull, 0x3fc73dbd00000000ull,
    0x3fc7b97b00000000ull, 0x3fc8350b00000000ull, 0x3fc8b06e00000000ull, 0x3fc92ba300000000ull,
    0x3fc9a6a800000000ull, 0x3fca217e00000000ull, 0x3fca9c2300000000ull, 0x3fcb169600000000ull,
    0x3fcb90d700000000ull, 0x3fcc0ae500000000ull, 0x3fcc84bf00000000ull, 0x3fccfe6500000000ull,
    0x3fcd77d500000000ull, 0x3fcdf11000000000ull, 0x3fce6a1400000000ull, 0x3fcee2e100000000ull,
    0x3fcf5b7500000000ull, 0x3fcfd3d100000000ull, 0x3fd025fa00000000ull, 0x3fd061ee00000000ull,
    0x3fd09dc500000000ull, 0x3fd0d97e00000000ull, 0x3fd1151a00000000ull, 0x3fd1509700000000ull,
    0x3fd18bf500000000ull, 0x3fd1c73500000000ull, 0x3fd2025500000000ull, 0x3fd23d5600000000ull,
    0x3fd2783700000000ull, 0x3fd2b2f700000000ull, 0x3fd2ed9800000000ull, 0x3fd3281800000000ull,
    0x3fd3627700000000ull, 0x3fd39cb400000000ull, 0x3fd3d6d100000000ull, 0x3fd410cb00000000ull,
    0x3fd44aa400000000ull, 0x3fd4845a00000000ull, 0x3fd4bdee00000000ull, 0x3fd4f75f00000000ull,
    0x3fd530ad00000000ull, 0x3fd569d800000000ull, 0x3fd5a2e000000000ull, 0x3fd5dbc300000000ull,
    0x3fd6148400000000ull, 0x3fd64d1f00000000ull, 0x3fd6859700000000ull, 0x3fd6bdea00000000ull,
    0x3fd6f61900000000ull, 0x3fd72e2200000000ull, 0x3fd7660700000000ull, 0x3fd79dc600000000ull,
    0x3fd7d56000000000ull, 0x3fd80cd400000000ull, 0x3fd8442200000000ull, 0x3fd87b4b00000000ull,
    0x3fd8b24d00000000ull, 0x3fd8e92900000000ull, 0x3fd91fde00000000ull, 0x3fd9566d00000000ull,
    0x3fd98cd500000000ull, 0x3fd9c31600000000ull, 0x3fd9f93000000000ull, 0x3fda2f2300000000ull,
    0x3fda64ee00000000ull, 0x3fda9a9200000000ull, 0x3fdad00f00000000ull, 0x3fdb056400000000ull,
    0x3fdb3a9100000000ull, 0x3fdb6f9600000000ull, 0x3fdba47300000000ull, 0x3fdbd92800000000ull,
    0x3fdc0db400000000ull, 0x3fdc421900000000ull, 0x3fdc765500000000ull, 0x3fdcaa6800000000ull,
    0x3fdcde5300000000ull, 0x3fdd121500000000ull, 0x3fdd45ae00000000ull, 0x3fdd791f00000000ull,
    0x3fddac6700000000ull, 0x3fdddf8500000000ull, 0x3fde127b00000000ull, 0x3fde454800000000ull,
    0x3fde77eb00000000ull, 0x3fdeaa6500000000ull, 0x3fdedcb600000000ull, 0x3fdf0ede00000000ull,
    0x3fdf40dd00000000ull, 0x3fdf72b200000000ull, 0x3fdfa45d00000000ull, 0x3fdfd5e000000000ull,
    0x3fe0039c00000000ull, 0x3fe01c3400000000ull, 0x3fe034b700000000ull, 0x3fe04d2500000000ull,
    0x3fe0657e00000000ull, 0x3fe07dc300000000ull, 0x3fe095f300000000ull, 0x3fe0ae0e00000000ull,
    0x3fe0c61400000000ull, 0x3fe0de0500000000ull, 0x3fe0f5e200000000ull, 0x3fe10daa00000000ull,
    0x3fe1255d00000000ull, 0x3fe13cfb00000000ull, 0x3fe1548500000000ull, 0x3fe16bfa00000000ull,
    0x3fe1835a00000000ull, 0x3fe19aa500000000ull, 0x3fe1b1dc00000000ull, 0x3fe1c8fe00000000ull,
    0x3fe1e00b00000000ull, 0x3fe1f70400000000ull, 0x3fe20de800000000ull, 0x3fe224b700000000ull,
    0x3fe23b7100000000ull, 0x3fe2521700000000ull, 0x3fe268a900000000ull, 0x3fe27f2600000000ull,
    0x3fe2958e00000000ull, 0x3fe2abe200000000ull, 0x3fe2c22100000000ull, 0x3fe2d84c00000000ull,
    0x3fe2ee6200000000ull, 0x3fe3046400000000ull, 0x3fe31a5200000000ull, 0x3fe3302b00000000ull,
    0x3fe345f000000000ull, 0x3fe35ba000000000ull, 0x3fe3713d00000000ull, 0x3fe386c500000000ull,
    0x3fe39c3900000000ull, 0x3fe3b19800000000ull, 0x3fe3c6e400000000ull, 0x3fe3dc1c00000000ull,
    0x3fe3f13f00000000ull, 0x3fe4064f00000000ull, 0x3fe41b4a00000000ull, 0x3fe4303200000000ull,
    0x3fe4450600000000ull, 0x3fe459c600000000ull, 0x3fe46e7200000000ull, 0x3fe4830a00000000ull,
    0x3fe4978f00000000ull, 0x3fe4ac0000000000ull, 0x3fe4c05e00000000ull, 0x3fe4d4a800000000ull,
    0x3fe4e8de00000000ull, 0x3fe4fd0100000000ull, 0x3fe5111000000000ull, 0x3fe5250c00000000ull,
    0x3fe538f500000000ull, 0x3fe54cca00000000ull, 0x3fe5608d00000000ull, 0x3fe5743c00000000ull,
    0x3fe587d800000000ull, 0x3fe59b6000000000ull, 0x3fe5aed600000000ull, 0x3fe5c23900000000ull,
    0x3fe5d58900000000ull, 0x3fe5e8c600000000ull, 0x3fe5fbf000000000ull, 0x3fe60f0800000000ull,
    0x3fe6220d00000000ull, 0x3fe634ff00000000ull, 0x3fe647de00000000ull, 0x3fe65aab00000000ull,
    0x3fe66d6600000000ull, 0x3fe6800e00000000ull, 0x3fe692a400000000ull, 0x3fe6a52700000000ull,
    0x3fe6b79800000000ull, 0x3fe6c9f700000000ull, 0x3fe6dc4400000000ull, 0x3fe6ee7f00000000ull,
    0x3fe700a700000000ull, 0x3fe712be00000000ull, 0x3fe724c300000000ull, 0x3fe736b600000000ull,
    0x3fe7489700000000ull, 0x3fe75a6700000000ull, 0x3fe76c2400000000ull, 0x3fe77dd100000000ull,
    0x3fe78f6b00000000ull, 0x3fe7a0f400000000ull, 0x3fe7b26c00000000ull, 0x3fe7c3d300000000ull,
    0x3fe7d52800000000ull, 0x3fe7e66c00000000ull, 0x3fe7f79e00000000ull, 0x3fe808c000000000ull,
    0x3fe819d000000000ull, 0x3fe82ad000000000ull, 0x3fe83bbe00000000ull, 0x3fe84c9c00000000ull,
    0x3fe85d6900000000ull, 0x3fe86e2500000000ull, 0x3fe87ed000000000ull, 0x3fe88f6b00000000ull,
    0x3fe89ff500000000ull, 0x3fe8b06f00000000ull, 0x3fe8c0d900000000ull, 0x3fe8d13200000000ull,
    0x3fe8e17a00000000ull, 0x3fe8f1b300000000ull, 0x3fe901db00000000ull, 0x3fe911f300000000ull,
    0x3fe921fb00000000ull,
};
constexpr std::uint64_t kAtanTail[241] = {  // rva 0x112c20
    0x3e56e59fbd38db2cull, 0x3e64e3aa54dedf96ull, 0x3e67e105ab1bda88ull, 0x3e48c5254d013fd0ull,
    0x3e2cf8ab3ad62670ull, 0x3e59dca4bec80468ull, 0x3e53f4b5ec98a8daull, 0x3e6b9d49619d81feull,
    0x3e43017887460934ull, 0x3e511e3eca0b9944ull, 0x3e54f3f73c5a332eull, 0x3e5c71c8ae0e00a6ull,
    0x3e67cde0f86fbdc7ull, 0x3e570f328c889c72ull, 0x3e5c07ae9b994efeull, 0x3e40c8021d7b1698ull,
    0x3e635585edb8cb22ull, 0x3e70842567b30e96ull, 0x3e799e811031472eull, 0x3e6041821416bceeull,
    0x3e7f6086e4dc96f4ull, 0x3e471a535c5f1b58ull, 0x3e765f743fe63ca1ull, 0x3e7dbd733472d014ull,
    0x3e7d18cc4d8b0d1dull, 0x3e78c12553c8fb29ull, 0x3e753b49e2e8f991ull, 0x3e77422ae148c141ull,
    0x3e4e3ec269df56a8ull, 0x3e7ff6754e7e0ac9ull, 0x3e7131267b1b5aadull, 0x3e7d14fa403a94bcull,
    0x3e62f396c089a3d8ull, 0x3e7c731d78fa95bbull, 0x3e7c50f385177399ull, 0x3e6f41409c6f2c20ull,
    0x3e7d2d90c4c39ec0ull, 0x3e680420696f2106ull, 0x3e4b40327943a2e8ull, 0x3e65d35e02f3d2a2ull,
    0x3e64a498288117b0ull, 0x3e635da119afb324ull, 0x3e714e85cdb9a908ull, 0x3e638754e5547b9aull,
    0x3e7be40ae6ce3246ull, 0x3e70c993b3bea7e7ull, 0x3e71d2dd89ac3359ull, 0x3e61476603332c46ull,
    0x3e7f25901bac55b7ull, 0x3e7f881b7c826e28ull, 0x3e7441996d698d20ull, 0x3e8407ac521ea089ull,
    0x3e82fb0c6c4b1723ull, 0x3e8ca135966a3e18ull, 0x3e6b1218e4d646e4ull, 0x3e6d4e72a350d288ull,
    0x3e84617e2f04c329ull, 0x3e6096ec41e82650ull, 0x3e79f91f25773e6eull, 0x3e659c0820f1d674ull,
    0x3e602bf7a2df1064ull, 0x3e8fb36bfc40508full, 0x3e7ea08f3f8dc892ull, 0x3e73ed6254656a0eull,
    0x3e6b83f5e5e69c58ull, 0x3e8d6ec2af768592ull, 0x3e6493889a226f94ull, 0x3e85ad8fa65279baull,
    0x3e6b615784d45434ull, 0x3e809a184368f145ull, 0x3e761a2439b0d91cull, 0x3e7ce1a65e39a978ull,
    0x3e832a39a93b6a66ull, 0x3e81c3699af804e7ull, 0x3e575e0f4e44ede8ull, 0x3e8f77ced1a7a83bull,
    0x3e284e7f0cb1b500ull, 0x3e8ec6b838b02dfeull, 0x3e83ebf4dfbeda87ull, 0x3e89397aed9cb475ull,
    0x3e707937bc239c54ull, 0x3e8aa754553131b6ull, 0x3e74a05d407c45dcull, 0x3e8132231a206dd0ull,
    0x3e72d8ecfdd69c88ull, 0x3e7a852c74218606ull, 0x3e871bf2baeebb50ull, 0x3e483d7db7491820ull,
    0x3e6ca50d92b6da14ull, 0x3e56f5cde8530298ull, 0x3e7f343198910740ull, 0x3e70e8d241ccd80aull,
    0x3e71535ac619e6c8ull, 0x3e77316041c36cd2ull, 0x3e7985a000637d8eull, 0x3e6f2f29858c0a68ull,
    0x3e8879847f96d909ull, 0x3e8ab3d319e12e42ull, 0x3e75088162dfc4c2ull, 0x3e605749a1cd9d8cull,
    0x3e5da65c6c6b8618ull, 0x3e6739bf7df1ad64ull, 0x3e6bc31252aa3340ull, 0x3e5e528191ad3aa8ull,
    0x3e8929d93df19f18ull, 0x3e5ff11eb693a080ull, 0x3e455ae3f145a3a0ull, 0x3e7cbcd8c6c0ca82ull,
    0x3e70cb04d425d304ull, 0x3e79adfcab5be678ull, 0x3e893d90c5662508ull, 0x3e768489bd35ff40ull,
    0x3e3586ed3da2b7e0ull, 0x3e87604d2e850eeeull, 0x3e7ac1d12bfb53d8ull, 0x3e39b3d468274740ull,
    0x3e7fc5d68d10e53cull, 0x3e88f9e51884becbull, 0x3e8a87f0869c06d1ull, 0x3e831e7279f685faull,
    0x3e46a8282f9719b0ull, 0x3e60d2724a8a44e0ull, 0x3e8a60524b11ad4eull, 0x3e575fdf832750f0ull,
    0x3e8cf06902e4cd36ull, 0x3e6e82422d4f6d10ull, 0x3e524a091063e6c0ull, 0x3e78a1a172dc6f38ull,
    0x3e929b6619f8a92dull, 0x3e79274d9c1b70c8ull, 0x3e50c34b1fbb7930ull, 0x3e6639866c20eb50ull,
    0x3e86d6d0f6832e9eull, 0x3e9af54def99f25eull, 0x3e916cfc52a00262ull, 0x3e8dcc1e83569c32ull,
    0x3e937f7a551ed425ull, 0x3e9f6360adc98887ull, 0x3e92c6ec8d35a2c1ull, 0x3e8bd44df84cb036ull,
    0x3e9117cf826e310eull, 0x3e9ca533f332cfc9ull, 0x3e90f208509dbc2eull, 0x3e8cd07d93c945deull,
    0x3e957bdfd67e6d72ull, 0x3e7aab89c516c658ull, 0x3e63e823b1a1b8a0ull, 0x3e8307464a9d6d3cull,
    0x3e9c5993cd438843ull, 0x3e9ba2fca02ab554ull, 0x3e801a5b6983a268ull, 0x3e6273d1b350efc8ull,
    0x3e864c238c37b0c6ull, 0x3e6aded07370a300ull, 0x3e878091197eb47eull, 0x3e74b0f245e0dabcull,
    0x3e9080d9794e2eafull, 0x3e8d4ec242b60c76ull, 0x3e4221d2f940caa0ull, 0x3e7cdbc42b2bba5cull,
    0x3e6cce37bb440840ull, 0x3e96c1d999cf1dd0ull, 0x3e5bed8a07eb0870ull, 0x3e769ed88f490e3cull,
    0x3e6cd41719b73ef0ull, 0x3e9cbc4ac95b41b7ull, 0x3e9238f1b890f5d7ull, 0x3e750c4282259cc4ull,
    0x3e9713d2de87b3e2ull, 0x3e81d5a7d2255276ull, 0x3e9c0dfd48227ac1ull, 0x3e91c964dab76753ull,
    0x3e86de56d5704496ull, 0x3e84aeb71fd19968ull, 0x3e8fbf91c57b1918ull, 0x3e9d6bef7fbe5d9aull,
    0x3e9464d3dc249066ull, 0x3e9638e2ec4d9073ull, 0x3e716f4a7247ea7cull, 0x3e31a0a740f1d440ull,
    0x3e86edbb0114a33cull, 0x3e7dbee8bf1d513cull, 0x3e95b8bdb0248f73ull, 0x3e97de3d3f5eac64ull,
    0x3e8ee24187ae448aull, 0x3e9e06c591ec5192ull, 0x3e74e3861a332738ull, 0x3e7a9599dcc2bfe4ull,
    0x3e6f732fbad43468ull, 0x3e9eb9f573b727d9ull, 0x3e98b212a2eb9897ull, 0x3e9384884c167215ull,
    0x3e90e2d363020051ull, 0x3e92820879fbd022ull, 0x3e9a1ab9893e4b30ull, 0x3e82d1b817a24478ull,
    0x3e615d7b8ded4878ull, 0x3e78968f9db3a5e4ull, 0x3e971c4171fe135full, 0x3e96d80f605d0d8cull,
    0x3e7c91f043691590ull, 0x3e839f8a15fce2b2ull, 0x3e455beda9d94b80ull, 0x3e8b12c15d60949aull,
    0x3e924167b312bfe3ull, 0x3e90ab8633070277ull, 0x3e854554ebbc80eeull, 0x3e60204aef5a4bb8ull,
    0x3e98af08c679cf2cull, 0x3e90852a330ae6c8ull, 0x3e86d3eb9ec32916ull, 0x3e8685cb7fcbbafeull,
    0x3e91f751c1e0bd95ull, 0x3e5705b1b0f72560ull, 0x3e9b98d8d808ca92ull, 0x3e62ea22c75cc980ull,
    0x3e97aba62bca0350ull, 0x3e9d73833442278cull, 0x3e95a5ca1fb18bf9ull, 0x3e61a6092b6ecf28ull,
    0x3e744fd049aac104ull, 0x3e2c114fd8df5180ull, 0x3e95972f130feae5ull, 0x3e7ca034a55fe198ull,
    0x3e96e2b149990227ull, 0x3e7b00000294592cull, 0x3e98b9bdc442620eull, 0x3e8d94fdfabf3e4eull,
    0x3e85db30b145ad9aull, 0x3e8e3e1eb95022b0ull, 0x3e9d5b8b45442bd6ull, 0x3e97a046231ecd2eull,
    0x3e9feafe3ef55232ull, 0x3e9839e7bfd78267ull, 0x3e645cf49d6fa900ull, 0x3e4be3132b27f380ull,
    0x3e9533980bb84f9full, 0x3e5889e2ce3ba390ull, 0x3e7f7778c3ad0cc8ull, 0x3e846660cec4eba2ull,
    0x3e85110b4611a626ull,
};

// scaleUpDouble1024 (RVA 0x4def0): multiply by 2^1024 exactly; a zero comes back as +0.
std::uint64_t scale_up_1024(std::uint64_t u) {
    if (u & 0x7ff0000000000000ull) return u + 0x4000000000000000ull;
    double t = dbl(u | 0x4010000000000000ull);
    t = (static_cast<std::int64_t>(u) < 0) ? t + 4.0 : t + -4.0;
    return bits(t);
}

// scaleDownDouble(u, 100) (RVA 0x937b4): divide by 2^100, rounding half up when the result is denormal.
std::uint64_t scale_down_100(std::uint64_t u) {
    const std::uint64_t sign = u & 0x8000000000000000ull;
    std::uint64_t ax = u & 0x7fffffffffffffffull;
    const std::uint32_t e = static_cast<std::uint32_t>(ax >> 52);
    std::uint64_t r;
    if (e > 100) {
        r = (static_cast<std::uint64_t>(e - 100) << 52) | (ax & 0x800fffffffffffffull);
    } else {
        const std::uint64_t m = (ax & 0x801fffffffffffffull) | 0x0010000000000000ull;
        if (static_cast<int>(101 - e) > 54) {
            r = 0;
        } else {
            const std::uint64_t t = m >> ((100 - e) & 63);
            r = (t >> 1) + (t & 1);
        }
    }
    return sign | r;
}

}  // namespace

double msvc_atan2(double y, double x) {
    const double kPiBy2 = dbl(0x3ff921fb54442d18ull);
    const double kPi = dbl(0x400921fb54442d18ull);

    const std::uint64_t ux = bits(x), uy = bits(y);
    int xexp = static_cast<int>((ux >> 52) & 0x7ff);
    const int yexp = static_cast<int>((uy >> 52) & 0x7ff);
    const std::uint64_t ax = ux & 0x7fffffffffffffffull, ay = uy & 0x7fffffffffffffffull;
    const bool xneg = (ux >> 63) != 0, yneg = (uy >> 63) != 0;
    int diffexp = yexp - xexp;
    const std::uint64_t kInf = 0x7ff0000000000000ull;

    if (ax > kInf) return dbl(ux | 0x0008000000000000ull);
    if (ay > kInf) return dbl(uy | 0x0008000000000000ull);
    if (ay == 0) {
        if (!xneg) return y;
        return yneg ? -kPi : kPi;
    }
    if (ax == 0) {
        if (yneg) return -kPiBy2;
        // y > 0: the CRT falls through to the general path here (with xexp = 0).
    }
    if (xexp < 0x3fd && yexp < 0x3fd) {
        // Both below 1/4: scale both up by 2^1024 (only x, y and diffexp change; the sign/inf tests below use
        // the original values, as the CRT does).
        const std::uint64_t sx = scale_up_1024(ux), sy = scale_up_1024(uy);
        x = dbl(sx);
        y = dbl(sy);
        diffexp = static_cast<int>((sy >> 52) & 0x7ff) - static_cast<int>((sx >> 52) & 0x7ff);
    }
    if (diffexp > 56) return yneg ? -kPiBy2 : kPiBy2;
    if (diffexp < -28 && !xneg) {
        if (diffexp < -1074) return yneg ? -0.0 : 0.0;
        if (diffexp < -1022) {
            const double q = (dbl(0x4630000000000000ull) * y) / x;  // 2^100 * y / x
            return dbl(scale_down_100(bits(q)));
        }
        return y / x;
    }
    if (diffexp < -56 && xneg) return yneg ? -kPi : kPi;
    if (ay == kInf && ax == kInf) {
        if (!xneg) return yneg ? dbl(0xbfe921fb54442d18ull) : dbl(0x3fe921fb54442d18ull);
        return yneg ? dbl(0xc002d97c7f3321d2ull) : dbl(0x4002d97c7f3321d2ull);
    }

    // General case on |x|, |y|.
    if (xneg) x = -x;
    if (yneg) y = -y;
    double u, v;
    bool swap;
    if (y > x) { u = y; v = x; swap = true; } else { u = x; v = y; swap = false; }
    const double vbyu = v / u;
    double c, t;
    if (vbyu > 0.0625) {
        const int index = static_cast<int>(std::fma(vbyu, 256.0, 0.5));
        c = dbl(kAtanLead[index - 16]);
        const double r = static_cast<double>(static_cast<std::uint32_t>(index)) * 0.00390625;
        // Scale u and v by the same power of two so that u is near 1.
        int k = 0x3ff - static_cast<int>((bits(u) >> 52) & 0x7ff);
        const int h = k / 2;
        k -= h;
        const double s1 = dbl(static_cast<std::uint64_t>(static_cast<std::int64_t>(h) + 0x3ff) << 52);
        const double s2 = dbl(static_cast<std::uint64_t>(static_cast<std::int64_t>(k) + 0x3ff) << 52);
        const double us = (s1 * u) * s2;
        const double vs = (s1 * v) * s2;
        const double uhead = dbl(bits(us) & 0xfffffffff8000000ull);
        double num = std::fma(-uhead, r, vs);
        const double utail = us - uhead;
        num = std::fma(-r, utail, num);
        const double den = std::fma(vs, r, us);
        const double q = num / den;
        t = q + dbl(kAtanTail[index - 16]);
        const double q2 = q * q;
        double p = std::fma(-q2, dbl(0x3fc99992b9802d7dull), dbl(0x3fd5555555550877ull));
        p = p * q2;
        t = std::fma(-q, p, t);
    } else if (1e-8 > vbyu) {
        c = 0.0;
        t = vbyu;
    } else {
        c = 0.0;
        const double v2 = vbyu * vbyu;
        const double uh = dbl(bits(u) & 0xffffffff00000000ull);
        const double qh = dbl(bits(vbyu) & 0xffffffff00000000ull);
        const double ut = u - uh;
        double rem = std::fma(-uh, qh, v);
        rem = std::fma(-qh, ut, rem);
        const double qt = vbyu - qh;
        rem = std::fma(-u, qt, rem);
        double p = std::fma(-v2, dbl(0x3fb70c3192d751c2ull), dbl(0x3fbc718839b8c4ebull));
        p = std::fma(-v2, p, dbl(0x3fc2492482bd6be1ull));
        p = std::fma(-v2, p, dbl(0x3fc99999999643a3ull));
        p = std::fma(-v2, p, dbl(0x3fd5555555555538ull));
        const double v3 = v2 * vbyu;
        double s = rem / u;
        s = std::fma(-v3, p, s);
        t = s + vbyu;
    }
    if (swap) {
        c = kPiBy2 - c;
        t = dbl(0x3c91a62633145c07ull) - t;
    }
    if (xneg) {
        c = dbl(0x400921fb50000000ull) - c;
        t = dbl(0x3e6110b4611a6263ull) - t;
    }
    double res = c + t;
    if (yneg) res = -res;
    return res;
}

}  // namespace cleria
