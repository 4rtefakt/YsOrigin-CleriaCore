// exp, log and log10 (double) of the x64 Universal CRT, bit for bit, on every platform.
//
// Source: C:\Windows\System32\ucrtbase.dll 10.0.26100.9711. These are AMD's hand-written assembly routines (AMD LibM,
// BSD-3-Clause, Copyright (C) 2008-2026 Advanced Micro Devices, Inc.). Each entry tests the CPU feature word at RVA
// 0x139e60 (cmp [..], 0 / jne) and takes an FMA3 body when it is nonzero; it reads 3 on the reference PC, so these
// are the FMA3 bodies:
//   exp   export RVA 0xa81a0; inf/nan pre-check 0xa81a0..0xa81b9 and 0xa8330..0xa835b; FMA3 body 0xa8400..0xa85b4
//   log   export RVA 0xa8c60; FMA3 body 0xa8f20..0xa918a
//   log10 export RVA 0xa91a0; FMA3 body 0xa94a0..0xa974a
// Error returns go through _handle_error wrappers (0x4f2f0, 0x981b0 / 0x4ec90 -> 0x4ecb8) that return the value they
// are given: exp overflow +inf, exp underflow +0, log(+-0) -inf, log(x < 0) and log(-inf) 0xfff8000000000000,
// NaN input -> the input with the quiet bit set. A Windows machine without FMA3 would run the SSE2 bodies, which
// this file does not reproduce. The disassembly is the authority. Every vfmadd/vfmsub is a std::fma below, every
// other operation rounds on its own (compile with -ffp-contract=off or /fp:precise). errno/_matherr and exception
// flags are not modelled.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cleria {
namespace {

inline std::uint64_t bits(double d) { std::uint64_t u; std::memcpy(&u, &d, 8); return u; }
inline double dbl(std::uint64_t u) { double d; std::memcpy(&d, &u, 8); return d; }

// kInvF: 1/F; kLog*: log(2F) and log10(2F) as lead + tail, F = 0.5 + j/512, j = 0 .. 256.
// kExp*: 2^(j/64), j = 0 .. 63, in full and as lead + tail.
constexpr std::uint64_t kInvF[257] = {  // rva 0x10a650
    0x4000000000000000ull, 0x3fffe01fe01fe020ull, 0x3fffc07f01fc07f0ull, 0x3fffa11caa01fa12ull,
    0x3fff81f81f81f820ull, 0x3fff6310aca0dbb5ull, 0x3fff44659e4a4271ull, 0x3fff25f644230ab5ull,
    0x3fff07c1f07c1f08ull, 0x3ffee9c7f8458e02ull, 0x3ffecc07b301ecc0ull, 0x3ffeae807aba01ebull,
    0x3ffe9131abf0b767ull, 0x3ffe741aa59750e4ull, 0x3ffe573ac901e574ull, 0x3ffe3a9179dc1a73ull,
    0x3ffe1e1e1e1e1e1eull, 0x3ffe01e01e01e01eull, 0x3ffde5d6e3f8868aull, 0x3ffdca01dca01dcaull,
    0x3ffdae6076b981dbull, 0x3ffd92f2231e7f8aull, 0x3ffd77b654b82c34ull, 0x3ffd5cac807572b2ull,
    0x3ffd41d41d41d41dull, 0x3ffd272ca3fc5b1aull, 0x3ffd0cb58f6ec074ull, 0x3ffcf26e5c44bfc6ull,
    0x3ffcd85689039b0bull, 0x3ffcbe6d9601cbe7ull, 0x3ffca4b3055ee191ull, 0x3ffc8b265afb8a42ull,
    0x3ffc71c71c71c71cull, 0x3ffc5894d10d4986ull, 0x3ffc3f8f01c3f8f0ull, 0x3ffc26b5392ea01cull,
    0x3ffc0e070381c0e0ull, 0x3ffbf583ee868d8bull, 0x3ffbdd2b899406f7ull, 0x3ffbc4fd65883e7bull,
    0x3ffbacf914c1bad0ull, 0x3ffb951e2b18ff23ull, 0x3ffb7d6c3dda338bull, 0x3ffb65e2e3beee05ull,
    0x3ffb4e81b4e81b4full, 0x3ffb37484ad806ceull, 0x3ffb2036406c80d9ull, 0x3ffb094b31d922a4ull,
    0x3ffaf286bca1af28ull, 0x3ffadbe87f94905eull, 0x3ffac5701ac5701bull, 0x3ffaaf1d2f87ebfdull,
    0x3ffa98ef606a63beull, 0x3ffa82e65130e159ull, 0x3ffa6d01a6d01a6dull, 0x3ffa574107688a4aull,
    0x3ffa41a41a41a41aull, 0x3ffa2c2a87c51ca0ull, 0x3ffa16d3f97a4b02ull, 0x3ffa01a01a01a01aull,
    0x3ff9ec8e951033d9ull, 0x3ff9d79f176b682dull, 0x3ff9c2d14ee4a102ull, 0x3ff9ae24ea5510daull,
    0x3ff999999999999aull, 0x3ff9852f0d8ec0ffull, 0x3ff970e4f80cb872ull, 0x3ff95cbb0be377aeull,
    0x3ff948b0fcd6e9e0ull, 0x3ff934c67f9b2ce6ull, 0x3ff920fb49d0e229ull, 0x3ff90d4f120190d5ull,
    0x3ff8f9c18f9c18faull, 0x3ff8e6527af1373full, 0x3ff8d3018d3018d3ull, 0x3ff8bfce8062ff3aull,
    0x3ff8acb90f6bf3aaull, 0x3ff899c0f601899cull, 0x3ff886e5f0abb04aull, 0x3ff87427bcc092b9ull,
    0x3ff8618618618618ull, 0x3ff84f00c2780614ull, 0x3ff83c977ab2beddull, 0x3ff82a4a0182a4a0ull,
    0x3ff8181818181818ull, 0x3ff8060180601806ull, 0x3ff7f405fd017f40ull, 0x3ff7e225515a4f1dull,
    0x3ff7d05f417d05f4ull, 0x3ff7beb3922e017cull, 0x3ff7ad2208e0ecc3ull, 0x3ff79baa6bb6398bull,
    0x3ff78a4c8178a4c8ull, 0x3ff77908119ac60dull, 0x3ff767dce434a9b1ull, 0x3ff756cac201756dull,
    0x3ff745d1745d1746ull, 0x3ff734f0c541fe8dull, 0x3ff724287f46debcull, 0x3ff713786d9c7c09ull,
    0x3ff702e05c0b8170ull, 0x3ff6f26016f26017ull, 0x3ff6e1f76b4337c7ull, 0x3ff6d1a62681c861ull,
    0x3ff6c16c16c16c17ull, 0x3ff6b1490aa31a3dull, 0x3ff6a13cd1537290ull, 0x3ff691473a88d0c0ull,
    0x3ff6816816816817ull, 0x3ff6719f3601671aull, 0x3ff661ec6a5122f9ull, 0x3ff6524f853b4aa3ull,
    0x3ff642c8590b2164ull, 0x3ff63356b88ac0deull, 0x3ff623fa77016240ull, 0x3ff614b36831ae94ull,
    0x3ff6058160581606ull, 0x3ff5f66434292dfcull, 0x3ff5e75bb8d015e7ull, 0x3ff5d867c3ece2a5ull,
    0x3ff5c9882b931057ull, 0x3ff5babcc647fa91ull, 0x3ff5ac056b015ac0ull, 0x3ff59d61f123ccaaull,
    0x3ff58ed2308158edull, 0x3ff5805601580560ull, 0x3ff571ed3c506b3aull, 0x3ff56397ba7c52e2ull,
    0x3ff5555555555555ull, 0x3ff54725e6bb82feull, 0x3ff5390948f40febull, 0x3ff52aff56a8054bull,
    0x3ff51d07eae2f815ull, 0x3ff50f22e111c4c5ull, 0x3ff5015015015015ull, 0x3ff4f38f62dd4c9bull,
    0x3ff4e5e0a72f0539ull, 0x3ff4d843bedc2c4cull, 0x3ff4cab88725af6eull, 0x3ff4bd3edda68fe1ull,
    0x3ff4afd6a052bf5bull, 0x3ff4a27fad76014aull, 0x3ff49539e3b2d067ull, 0x3ff4880522014880ull,
    0x3ff47ae147ae147bull, 0x3ff46dce34596066ull, 0x3ff460cbc7f5cf9aull, 0x3ff453d9e2c776caull,
    0x3ff446f86562d9fbull, 0x3ff43a2730abee4dull, 0x3ff42d6625d51f87ull, 0x3ff420b5265e5951ull,
    0x3ff4141414141414ull, 0x3ff40782d10e6566ull, 0x3ff3fb013fb013fbull, 0x3ff3ee8f42a5af07ull,
    0x3ff3e22cbce4a902ull, 0x3ff3d5d991aa75c6ull, 0x3ff3c995a47babe7ull, 0x3ff3bd60d9232955ull,
    0x3ff3b13b13b13b14ull, 0x3ff3a524387ac822ull, 0x3ff3991c2c187f63ull, 0x3ff38d22d366088eull,
    0x3ff3813813813814ull, 0x3ff3755bd1c945eeull, 0x3ff3698df3de0748ull, 0x3ff35dce5f9f2af8ull,
    0x3ff3521cfb2b78c1ull, 0x3ff34679ace01346ull, 0x3ff33ae45b57bcb2ull, 0x3ff32f5ced6a1dfaull,
    0x3ff323e34a2b10bfull, 0x3ff3187758e9ebb6ull, 0x3ff30d190130d190ull, 0x3ff301c82ac40260ull,
    0x3ff2f684bda12f68ull, 0x3ff2eb4ea1fed14bull, 0x3ff2e025c04b8097ull, 0x3ff2d50a012d50a0ull,
    0x3ff2c9fb4d812ca0ull, 0x3ff2bef98e5a3711ull, 0x3ff2b404ad012b40ull, 0x3ff2a91c92f3c105ull,
    0x3ff29e4129e4129eull, 0x3ff293725bb804a5ull, 0x3ff288b01288b013ull, 0x3ff27dfa38a1ce4dull,
    0x3ff27350b8812735ull, 0x3ff268b37cd60127ull, 0x3ff25e22708092f1ull, 0x3ff2539d7e9177b2ull,
    0x3ff2492492492492ull, 0x3ff23eb79717605bull, 0x3ff23456789abcdfull, 0x3ff22a0122a0122aull,
    0x3ff21fb78121fb78ull, 0x3ff21579804855e6ull, 0x3ff20b470c67c0d9ull, 0x3ff2012012012012ull,
    0x3ff1f7047dc11f70ull, 0x3ff1ecf43c7fb84cull, 0x3ff1e2ef3b3fb874ull, 0x3ff1d8f5672e4abdull,
    0x3ff1cf06ada2811dull, 0x3ff1c522fc1ce059ull, 0x3ff1bb4a4046ed29ull, 0x3ff1b17c67f2bae3ull,
    0x3ff1a7b9611a7b96ull, 0x3ff19e0119e0119eull, 0x3ff19453808ca29cull, 0x3ff18ab083902bdbull,
    0x3ff1811811811812ull, 0x3ff1778a191bd684ull, 0x3ff16e0689427379ull, 0x3ff1648d50fc3201ull,
    0x3ff15b1e5f75270dull, 0x3ff151b9a3fdd5c9ull, 0x3ff1485f0e0acd3bull, 0x3ff13f0e8d344724ull,
    0x3ff135c81135c811ull, 0x3ff12c8b89edc0acull, 0x3ff12358e75d3033ull, 0x3ff11a3019a74826ull,
    0x3ff1111111111111ull, 0x3ff107fbbe011080ull, 0x3ff0fef010fef011ull, 0x3ff0f5edfab325a2ull,
    0x3ff0ecf56be69c90ull, 0x3ff0e40655826011ull, 0x3ff0db20a88f4696ull, 0x3ff0d24456359e3aull,
    0x3ff0c9714fbcda3bull, 0x3ff0c0a7868b4171ull, 0x3ff0b7e6ec259dc8ull, 0x3ff0af2f722eecb5ull,
    0x3ff0a6810a6810a7ull, 0x3ff09ddba6af8360ull, 0x3ff0953f39010954ull, 0x3ff08cabb37565e2ull,
    0x3ff0842108421084ull, 0x3ff07b9f29b8eae2ull, 0x3ff073260a47f7c6ull, 0x3ff06ab59c7912fbull,
    0x3ff0624dd2f1a9fcull, 0x3ff059eea0727586ull, 0x3ff05197f7d73404ull, 0x3ff04949cc1664c5ull,
    0x3ff0410410410410ull, 0x3ff038c6b78247fcull, 0x3ff03091b51f5e1aull, 0x3ff02864fc7729e9ull,
    0x3ff0204081020408ull, 0x3ff0182436517a37ull, 0x3ff0101010101010ull, 0x3ff0080402010080ull,
    0x3ff0000000000000ull,
};
constexpr std::uint64_t kLogLead[257] = {  // rva 0x109420
    0x0000000000000000ull, 0x3f6ff00aa0000000ull, 0x3f7fe02a60000000ull, 0x3f87dc4750000000ull,
    0x3f8fc0a8b0000000ull, 0x3f93cea440000000ull, 0x3f97b91b00000000ull, 0x3f9b9fc020000000ull,
    0x3f9f829b00000000ull, 0x3fa1b0d980000000ull, 0x3fa39e87b0000000ull, 0x3fa58a5ba0000000ull,
    0x3fa77458f0000000ull, 0x3fa95c8300000000ull, 0x3fab42dd70000000ull, 0x3fad276b80000000ull,
    0x3faf0a30c0000000ull, 0x3fb0759830000000ull, 0x3fb16536e0000000ull, 0x3fb253f620000000ull,
    0x3fb341d790000000ull, 0x3fb42edcb0000000ull, 0x3fb51b0730000000ull, 0x3fb60658a0000000ull,
    0x3fb6f0d280000000ull, 0x3fb7da7660000000ull, 0x3fb8c345d0000000ull, 0x3fb9ab4240000000ull,
    0x3fba926d30000000ull, 0x3fbb78c820000000ull, 0x3fbc5e5480000000ull, 0x3fbd4313d0000000ull,
    0x3fbe270760000000ull, 0x3fbf0a30c0000000ull, 0x3fbfec9130000000ull, 0x3fc0671510000000ull,
    0x3fc0d77e70000000ull, 0x3fc1478580000000ull, 0x3fc1b72ad0000000ull, 0x3fc2266f10000000ull,
    0x3fc29552f0000000ull, 0x3fc303d710000000ull, 0x3fc371fc20000000ull, 0x3fc3dfc2b0000000ull,
    0x3fc44d2b60000000ull, 0x3fc4ba36f0000000ull, 0x3fc526e5e0000000ull, 0x3fc59338d0000000ull,
    0x3fc5ff3070000000ull, 0x3fc66acd40000000ull, 0x3fc6d60fe0000000ull, 0x3fc740f8f0000000ull,
    0x3fc7ab8900000000ull, 0x3fc815c0a0000000ull, 0x3fc87fa060000000ull, 0x3fc8e928d0000000ull,
    0x3fc9525a90000000ull, 0x3fc9bb3620000000ull, 0x3fca23bc10000000ull, 0x3fca8becf0000000ull,
    0x3fcaf3c940000000ull, 0x3fcb5b5190000000ull, 0x3fcbc28670000000ull, 0x3fcc296850000000ull,
    0x3fcc8ff7c0000000ull, 0x3fccf63540000000ull, 0x3fcd5c2160000000ull, 0x3fcdc1bca0000000ull,
    0x3fce270760000000ull, 0x3fce8c0250000000ull, 0x3fcef0adc0000000ull, 0x3fcf550a50000000ull,
    0x3fcfb91860000000ull, 0x3fd00e6c40000000ull, 0x3fd0402590000000ull, 0x3fd071b850000000ull,
    0x3fd0a324e0000000ull, 0x3fd0d46b50000000ull, 0x3fd1058bf0000000ull, 0x3fd1368700000000ull,
    0x3fd1675ca0000000ull, 0x3fd1980d20000000ull, 0x3fd1c898c0000000ull, 0x3fd1f8ff90000000ull,
    0x3fd22941f0000000ull, 0x3fd2596010000000ull, 0x3fd2895a10000000ull, 0x3fd2b93030000000ull,
    0x3fd2e8e2b0000000ull, 0x3fd31871c0000000ull, 0x3fd347dd90000000ull, 0x3fd3772660000000ull,
    0x3fd3a64c50000000ull, 0x3fd3d54fa0000000ull, 0x3fd4043080000000ull, 0x3fd432ef20000000ull,
    0x3fd4618bc0000000ull, 0x3fd4900680000000ull, 0x3fd4be5f90000000ull, 0x3fd4ec9730000000ull,
    0x3fd51aad80000000ull, 0x3fd548a2c0000000ull, 0x3fd5767710000000ull, 0x3fd5a42ab0000000ull,
    0x3fd5d1bdb0000000ull, 0x3fd5ff3070000000ull, 0x3fd62c82f0000000ull, 0x3fd659b570000000ull,
    0x3fd686c810000000ull, 0x3fd6b3bb20000000ull, 0x3fd6e08ea0000000ull, 0x3fd70d42e0000000ull,
    0x3fd739d7f0000000ull, 0x3fd7664e10000000ull, 0x3fd792a550000000ull, 0x3fd7bede00000000ull,
    0x3fd7eaf830000000ull, 0x3fd816f410000000ull, 0x3fd842d1d0000000ull, 0x3fd86e9190000000ull,
    0x3fd89a3380000000ull, 0x3fd8c5b7c0000000ull, 0x3fd8f11e80000000ull, 0x3fd91c67e0000000ull,
    0x3fd9479410000000ull, 0x3fd972a340000000ull, 0x3fd99d9580000000ull, 0x3fd9c86b00000000ull,
    0x3fd9f323e0000000ull, 0x3fda1dc060000000ull, 0x3fda484090000000ull, 0x3fda72a490000000ull,
    0x3fda9cec90000000ull, 0x3fdac718c0000000ull, 0x3fdaf12930000000ull, 0x3fdb1b1e00000000ull,
    0x3fdb44f770000000ull, 0x3fdb6eb590000000ull, 0x3fdb985890000000ull, 0x3fdbc1e080000000ull,
    0x3fdbeb4d90000000ull, 0x3fdc149ff0000000ull, 0x3fdc3dd7a0000000ull, 0x3fdc66f4e0000000ull,
    0x3fdc8ff7c0000000ull, 0x3fdcb8e070000000ull, 0x3fdce1af00000000ull, 0x3fdd0a63a0000000ull,
    0x3fdd32fe70000000ull, 0x3fdd5b7f90000000ull, 0x3fdd83e720000000ull, 0x3fddac3530000000ull,
    0x3fddd46a00000000ull, 0x3fddfc8590000000ull, 0x3fde248810000000ull, 0x3fde4c71a0000000ull,
    0x3fde744260000000ull, 0x3fde9bfa60000000ull, 0x3fdec399d0000000ull, 0x3fdeeb20c0000000ull,
    0x3fdf128f50000000ull, 0x3fdf39e5b0000000ull, 0x3fdf6123f0000000ull, 0x3fdf884a30000000ull,
    0x3fdfaf5880000000ull, 0x3fdfd64f20000000ull, 0x3fdffd2e00000000ull, 0x3fe011fab0000000ull,
    0x3fe02552a0000000ull, 0x3fe0389ee0000000ull, 0x3fe04bdf90000000ull, 0x3fe05f14b0000000ull,
    0x3fe0723e50000000ull, 0x3fe0855c80000000ull, 0x3fe0986f40000000ull, 0x3fe0ab76b0000000ull,
    0x3fe0be72e0000000ull, 0x3fe0d163c0000000ull, 0x3fe0e44980000000ull, 0x3fe0f72410000000ull,
    0x3fe109f390000000ull, 0x3fe11cb810000000ull, 0x3fe12f7190000000ull, 0x3fe1422020000000ull,
    0x3fe154c3d0000000ull, 0x3fe1675ca0000000ull, 0x3fe179eab0000000ull, 0x3fe18c6e00000000ull,
    0x3fe19ee6b0000000ull, 0x3fe1b154b0000000ull, 0x3fe1c3b810000000ull, 0x3fe1d610f0000000ull,
    0x3fe1e85f50000000ull, 0x3fe1faa340000000ull, 0x3fe20cdcd0000000ull, 0x3fe21f0bf0000000ull,
    0x3fe23130d0000000ull, 0x3fe2434b60000000ull, 0x3fe2555bc0000000ull, 0x3fe2676200000000ull,
    0x3fe2795e10000000ull, 0x3fe28b5000000000ull, 0x3fe29d37f0000000ull, 0x3fe2af15f0000000ull,
    0x3fe2c0e9e0000000ull, 0x3fe2d2b400000000ull, 0x3fe2e47430000000ull, 0x3fe2f62a90000000ull,
    0x3fe307d730000000ull, 0x3fe3197a00000000ull, 0x3fe32b1330000000ull, 0x3fe33ca2b0000000ull,
    0x3fe34e2890000000ull, 0x3fe35fa4e0000000ull, 0x3fe37117b0000000ull, 0x3fe38280f0000000ull,
    0x3fe393e0d0000000ull, 0x3fe3a53730000000ull, 0x3fe3b68440000000ull, 0x3fe3c7c7f0000000ull,
    0x3fe3d90260000000ull, 0x3fe3ea3390000000ull, 0x3fe3fb5b80000000ull, 0x3fe40c7a40000000ull,
    0x3fe41d8fe0000000ull, 0x3fe42e9c60000000ull, 0x3fe43f9fe0000000ull, 0x3fe4509a50000000ull,
    0x3fe4618bc0000000ull, 0x3fe4727430000000ull, 0x3fe48353d0000000ull, 0x3fe4942a80000000ull,
    0x3fe4a4f850000000ull, 0x3fe4b5bd60000000ull, 0x3fe4c679a0000000ull, 0x3fe4d72d30000000ull,
    0x3fe4e7d810000000ull, 0x3fe4f87a30000000ull, 0x3fe50913c0000000ull, 0x3fe519a4c0000000ull,
    0x3fe52a2d20000000ull, 0x3fe53aad00000000ull, 0x3fe54b2460000000ull, 0x3fe55b9350000000ull,
    0x3fe56bf9d0000000ull, 0x3fe57c57f0000000ull, 0x3fe58cadb0000000ull, 0x3fe59cfb20000000ull,
    0x3fe5ad4040000000ull, 0x3fe5bd7d30000000ull, 0x3fe5cdb1d0000000ull, 0x3fe5ddde50000000ull,
    0x3fe5ee02a0000000ull, 0x3fe5fe1ed0000000ull, 0x3fe60e32f0000000ull, 0x3fe61e3ef0000000ull,
    0x3fe62e42e0000000ull,
};
constexpr std::uint64_t kLogTail[257] = {  // rva 0x109c30
    0x0000000000000000ull, 0x3db5885e0250435aull, 0x3de620cf11f86ed2ull, 0x3dff0214edba4a25ull,
    0x3dbf807c79f3db4eull, 0x3dea352ba779a52bull, 0x3dff56c46aa49fd5ull, 0x3dfebe465fef5196ull,
    0x3e0cf0660099f1f8ull, 0x3e1247b2ff85945dull, 0x3e13fd7abf5202b6ull, 0x3e1f91c9a918d51eull,
    0x3e08cb73f118d3caull, 0x3e1d91c7d6fad074ull, 0x3de1971bec28d14cull, 0x3e15b616a423c78aull,
    0x3da162a6617cc971ull, 0x3e166391c4c06d29ull, 0x3e2d46f5c1d0c4b8ull, 0x3e2e14282df1f6d3ull,
    0x3e186f47424a660dull, 0x3e2d4c8de077753eull, 0x3e2e0c307ed24f1cull, 0x3e226ea18763bdd3ull,
    0x3e25cad69737c933ull, 0x3e2af62599088901ull, 0x3e18c66c83d6b2d0ull, 0x3e1880ceb36fb30full,
    0x3e2495aac6ca17a4ull, 0x3e2761db4210878cull, 0x3e2eb78e862bac2full, 0x3e19b2cd75790dd9ull,
    0x3e2c55e5cbd3d50full, 0x3db162a6617cc971ull, 0x3dfdbeabaaa2e519ull, 0x3e1652cb7150c647ull,
    0x3e39a11cb2cd2ee2ull, 0x3e219d0ab1a28813ull, 0x3e24bd9e80a41811ull, 0x3e3214b596faa3dfull,
    0x3e303fea46980bb8ull, 0x3e31c8ffa5fd28c7ull, 0x3dce8f743bcd96c5ull, 0x3dfd98c5395315c6ull,
    0x3e3996fa3ccfa7b2ull, 0x3e1cd2af2ad13037ull, 0x3e1d0da1bd17200eull, 0x3e3330410ba68b75ull,
    0x3df4f27a790e7c41ull, 0x3e13956a86f6ff1bull, 0x3e2c6748723551d9ull, 0x3e2500de9326cdfcull,
    0x3e1086c848df1b59ull, 0x3e04357ead6836ffull, 0x3e24832442408024ull, 0x3e3d10da8154b13dull,
    0x3e39e8ad68ec8260ull, 0x3e3cfbf706abaf18ull, 0x3e3fc56ac6326e23ull, 0x3e39105e3185cf21ull,
    0x3e3d017fe5b19cc0ull, 0x3e3d1f6b48dd13feull, 0x3e20b63358a7e73aull, 0x3e263063028c211cull,
    0x3e2e6a6886b09760ull, 0x3e3c138bb891cd03ull, 0x3e369f7722b7221aull, 0x3df57d8fac1a628cull,
    0x3e3c55e5cbd3d50full, 0x3e1552d2ff48fe2eull, 0x3e37b8b26ca431bcull, 0x3e292decdc1c5f6dull,
    0x3e3abc7c551aaa8cull, 0x3e36b540731a354bull, 0x3e32d341036b89efull, 0x3e4f9ab21a3a2e0full,
    0x3e239c871afb9fbdull, 0x3e3e6add2c81f640ull, 0x3e435c95aa313f41ull, 0x3e249d4582f6cc53ull,
    0x3e47574c1c07398full, 0x3e4ba846dece9e8dull, 0x3e16999fafbc68e7ull, 0x3e4c9145e51b0103ull,
    0x3e479ef2cb44850aull, 0x3e0beec73de11275ull, 0x3e2ef4351af5a498ull, 0x3e45713a493b4a50ull,
    0x3e45c23a61385992ull, 0x3e42a88309f57299ull, 0x3e4530faa9ac8aceull, 0x3e25fec2d792a758ull,
    0x3e35a517a71cbcd7ull, 0x3e3707dc3e1cd9a3ull, 0x3e3a1a9f8ef43049ull, 0x3e4409d0276b3674ull,
    0x3e20e2f613e85bd9ull, 0x3df0027433001e5full, 0x3e35dde2836d3265ull, 0x3e2300134d7aaf04ull,
    0x3e3cb7e0b42724f5ull, 0x3e2d6e93167e6308ull, 0x3e3d1569b1526adbull, 0x3e0e99fc338a1a41ull,
    0x3e4eb01394a11b1cull, 0x3e04f27a790e7c41ull, 0x3e25ce3ca97b7af9ull, 0x3e281f0f940ed857ull,
    0x3e4d36295d88857cull, 0x3e21aca1ec4af526ull, 0x3e445743c7182726ull, 0x3e23c491aead337eull,
    0x3e3aef401a738931ull, 0x3e21cede76092a29ull, 0x3e4fba8f44f82bb4ull, 0x3e446f5f7f3c3e1aull,
    0x3e47055f86c9674bull, 0x3e4b41a92b6b6e1aull, 0x3e443d162e927628ull, 0x3e4466174013f9b1ull,
    0x3e3b05096ad69c62ull, 0x3e40b169150faa58ull, 0x3e3cd98b1df85da7ull, 0x3e468b507b0f8fa8ull,
    0x3e48422df57499baull, 0x3e11351586970274ull, 0x3e117e08acba92eeull, 0x3e26e04314dd0229ull,
    0x3e497f3097e56d1aull, 0x3e3356e655901286ull, 0x3e0cb761457f94d6ull, 0x3e39af67a85a9dacull,
    0x3e453410931a909full, 0x3e22c587206058f5ull, 0x3e223bc358899c22ull, 0x3e4d7bf8b6d223cbull,
    0x3e47991ec5197ddbull, 0x3e4a79e6bb3a9219ull, 0x3e3a4c43ed663ec5ull, 0x3e461b5a1484f438ull,
    0x3e4b4e36f7ef0c3aull, 0x3e115f026acd0d1bull, 0x3e3f36b535cecf05ull, 0x3e2ffb7fbf3eb5c6ull,
    0x3e3e6a6886b09760ull, 0x3e3135eb27f5bbc3ull, 0x3e470be7d6f6fa57ull, 0x3e4ce43cc84ab338ull,
    0x3e4c01d7aac3bd91ull, 0x3e45c58d07961060ull, 0x3e3628bcf941456eull, 0x3e4c58b2a8461cd2ull,
    0x3e33071282fb989aull, 0x3e420dab6a80f09cull, 0x3e44f8d84c397b1eull, 0x3e40d0ee08599e48ull,
    0x3e1d68787e37da36ull, 0x3e366187d591bafcull, 0x3e22346600bae772ull, 0x3e390377d0d61b8eull,
    0x3e4f5e0dd966b907ull, 0x3e49023cb79a00e2ull, 0x3e44e05158c28ad8ull, 0x3e3bfa7b08b18ae4ull,
    0x3e4ef1e63db35f67ull, 0x3e0ec2ae39493d4full, 0x3e40afe930ab2fa0ull, 0x3e225ff8a1810dd4ull,
    0x3e469743fb1a71a5ull, 0x3e5f9cc676785571ull, 0x3e5b524da4cbf982ull, 0x3e5a4c8b381535b8ull,
    0x3e5839be809caf2cull, 0x3e50968a1cb82c13ull, 0x3e5eae6a41723fb5ull, 0x3e5d9c29a380a4dbull,
    0x3e4094aa0ada625eull, 0x3e5973ad6fc108caull, 0x3e4747322fdbab97ull, 0x3e593692fa9d4221ull,
    0x3e5c5a992dfbc7d9ull, 0x3e4e1f33e102387aull, 0x3e464fbef14c048cull, 0x3e4490f513ca5e3bull,
    0x3e37a6af4d4c799dull, 0x3e57574c1c07398full, 0x3e57b133417f8c1cull, 0x3e5feb9e0c176514ull,
    0x3e419f25bb3172f7ull, 0x3e45f68a7bbfb852ull, 0x3e5ee278497929f1ull, 0x3e5ccee006109d58ull,
    0x3e5ce081a07bd8b3ull, 0x3e570e12981817b8ull, 0x3e292ab6d93503d0ull, 0x3e58cb7dd7c3b61eull,
    0x3e4efafd0a0b78daull, 0x3e5e907267c4288eull, 0x3e5d31ef96780875ull, 0x3e23430dfcd2ad50ull,
    0x3e344d88d75bc1f9ull, 0x3e5bec0f055e04fcull, 0x3e5d85611590b9adull, 0x3df320568e583229ull,
    0x3e5a891d1772f538ull, 0x3e22edc9dabba74dull, 0x3e4b9009a1015086ull, 0x3e52a12a8c5b1a19ull,
    0x3e3a7885f0fdac85ull, 0x3e5f4ffcd43ac691ull, 0x3e52243ae2640aadull, 0x3e546513299035d3ull,
    0x3e5b39c3a62dd725ull, 0x3e5ba6dd40049f51ull, 0x3e451d1ed7177409ull, 0x3e5cb0f2fd7f5216ull,
    0x3e3ab150cd4e2213ull, 0x3e5cfd7bf3193844ull, 0x3e53fff8455f1dbdull, 0x3e5fee640b905fc9ull,
    0x3e54e2adf548084cull, 0x3e3b597adc1ecdd2ull, 0x3e4345bd096d3a75ull, 0x3e5101b9d2453c8bull,
    0x3e508ce55cc8c979ull, 0x3e5bbf017e595f71ull, 0x3e37ce733bd393dcull, 0x3e233bb0a503f8a1ull,
    0x3e30e2f613e85bd9ull, 0x3e5e67555a635b3cull, 0x3e2ea88df73d5e8bull, 0x3e3d17e03bda18a8ull,
    0x3e5b607d76044f7eull, 0x3e52adc4e71bc2fcull, 0x3e5f99dc7362d1d9ull, 0x3e5473fa008e6a6aull,
    0x3e2b75bb09cb0985ull, 0x3e5ea04dd10b9abaull, 0x3e5802d0d6979674ull, 0x3e174688ccd99094ull,
    0x3e496f16abb9df22ull, 0x3e46e66df2aa374full, 0x3e4e66525ea4550aull, 0x3e42d02f34f20cbdull,
    0x3e46cfce65047188ull, 0x3e39b78c842d58b8ull, 0x3e4735e624c24bc9ull, 0x3e47eba1f7dd1adfull,
    0x3e586b3e59f65355ull, 0x3e1ce38e637f1b4dull, 0x3e58d82ec919edc7ull, 0x3e4c52648ddcfa37ull,
    0x3e52482ceae1ac12ull, 0x3e55a312311aba4full, 0x3e411e236329f225ull, 0x3e5b48c8cd2f246cull,
    0x3e6efa39ef35793cull,
};
constexpr std::uint64_t kLog10Lead[257] = {  // rva 0x107ea0
    0x0000000000000000ull, 0x3f5bbd9e90000000ull, 0x3f6bafd470000000ull, 0x3f74b99560000000ull,
    0x3f7b9476a0000000ull, 0x3f81344da0000000ull, 0x3f849b0850000000ull, 0x3f87fe71c0000000ull,
    0x3f8b5e9080000000ull, 0x3f8ebb6af0000000ull, 0x3f910a83a0000000ull, 0x3f92b5b5e0000000ull,
    0x3f945f4f50000000ull, 0x3f96075300000000ull, 0x3f97adc3d0000000ull, 0x3f9952a4f0000000ull,
    0x3f9af5f920000000ull, 0x3f9c97c370000000ull, 0x3f9e3806a0000000ull, 0x3f9fd6c5b0000000ull,
    0x3fa0ba01a0000000ull, 0x3fa187e120000000ull, 0x3fa25502c0000000ull, 0x3fa32167c0000000ull,
    0x3fa3ed1190000000ull, 0x3fa4b80180000000ull, 0x3fa58238e0000000ull, 0x3fa64bb910000000ull,
    0x3fa7148340000000ull, 0x3fa7dc98c0000000ull, 0x3fa8a3fad0000000ull, 0x3fa96aaac0000000ull,
    0x3faa30a9d0000000ull, 0x3faaf5f920000000ull, 0x3fabba9a00000000ull, 0x3fac7e8d90000000ull,
    0x3fad41d510000000ull, 0x3fae0471a0000000ull, 0x3faec66470000000ull, 0x3faf87aeb0000000ull,
    0x3fb02428c0000000ull, 0x3fb08426f0000000ull, 0x3fb0e3d290000000ull, 0x3fb1432c30000000ull,
    0x3fb1a23440000000ull, 0x3fb200eb60000000ull, 0x3fb25f5210000000ull, 0x3fb2bd68e0000000ull,
    0x3fb31b3050000000ull, 0x3fb378a8e0000000ull, 0x3fb3d5d330000000ull, 0x3fb432afa0000000ull,
    0x3fb48f3ed0000000ull, 0x3fb4eb8120000000ull, 0x3fb5477730000000ull, 0x3fb5a32160000000ull,
    0x3fb5fe8040000000ull, 0x3fb6599440000000ull, 0x3fb6b45df0000000ull, 0x3fb70eddb0000000ull,
    0x3fb7691400000000ull, 0x3fb7c30160000000ull, 0x3fb81ca630000000ull, 0x3fb8760300000000ull,
    0x3fb8cf1830000000ull, 0x3fb927e640000000ull, 0x3fb9806d90000000ull, 0x3fb9d8aea0000000ull,
    0x3fba30a9d0000000ull, 0x3fba885fa0000000ull, 0x3fbadfd070000000ull, 0x3fbb36fcb0000000ull,
    0x3fbb8de4d0000000ull, 0x3fbbe48930000000ull, 0x3fbc3aea40000000ull, 0x3fbc910870000000ull,
    0x3fbce6e410000000ull, 0x3fbd3c7da0000000ull, 0x3fbd91d580000000ull, 0x3fbde6ec00000000ull,
    0x3fbe3bc1a0000000ull, 0x3fbe9056b0000000ull, 0x3fbee4aba0000000ull, 0x3fbf38c0c0000000ull,
    0x3fbf8c9680000000ull, 0x3fbfe02d30000000ull, 0x3fc019c2a0000000ull, 0x3fc0434f70000000ull,
    0x3fc06cbd60000000ull, 0x3fc0960c80000000ull, 0x3fc0bf3d00000000ull, 0x3fc0e84f10000000ull,
    0x3fc11142f0000000ull, 0x3fc13a18a0000000ull, 0x3fc162d080000000ull, 0x3fc18b6a90000000ull,
    0x3fc1b3e710000000ull, 0x3fc1dc4630000000ull, 0x3fc2048810000000ull, 0x3fc22cace0000000ull,
    0x3fc254b4d0000000ull, 0x3fc27c9ff0000000ull, 0x3fc2a46e80000000ull, 0x3fc2cc20b0000000ull,
    0x3fc2f3b690000000ull, 0x3fc31b3050000000ull, 0x3fc3428e20000000ull, 0x3fc369d020000000ull,
    0x3fc390f680000000ull, 0x3fc3b80160000000ull, 0x3fc3def0e0000000ull, 0x3fc405c530000000ull,
    0x3fc42c7e70000000ull, 0x3fc4531cd0000000ull, 0x3fc479a070000000ull, 0x3fc4a00970000000ull,
    0x3fc4c65800000000ull, 0x3fc4ec8c30000000ull, 0x3fc512a640000000ull, 0x3fc538a630000000ull,
    0x3fc55e8c50000000ull, 0x3fc5845890000000ull, 0x3fc5aa0b40000000ull, 0x3fc5cfa470000000ull,
    0x3fc5f52440000000ull, 0x3fc61a8ad0000000ull, 0x3fc63fd850000000ull, 0x3fc6650cd0000000ull,
    0x3fc68a2880000000ull, 0x3fc6af2b80000000ull, 0x3fc6d415e0000000ull, 0x3fc6f8e7d0000000ull,
    0x3fc71da170000000ull, 0x3fc74242e0000000ull, 0x3fc766cc40000000ull, 0x3fc78b3da0000000ull,
    0x3fc7af9730000000ull, 0x3fc7d3d910000000ull, 0x3fc7f80350000000ull, 0x3fc81c1620000000ull,
    0x3fc8401190000000ull, 0x3fc863f5c0000000ull, 0x3fc887c2e0000000ull, 0x3fc8ab7900000000ull,
    0x3fc8cf1830000000ull, 0x3fc8f2a0a0000000ull, 0x3fc9161270000000ull, 0x3fc9396db0000000ull,
    0x3fc95cb280000000ull, 0x3fc97fe100000000ull, 0x3fc9a2f950000000ull, 0x3fc9c5fb70000000ull,
    0x3fc9e8e7b0000000ull, 0x3fca0bbdf0000000ull, 0x3fca2e7e80000000ull, 0x3fca512960000000ull,
    0x3fca73bea0000000ull, 0x3fca963e70000000ull, 0x3fcab8a8f0000000ull, 0x3fcadafe20000000ull,
    0x3fcafd3e30000000ull, 0x3fcb1f6930000000ull, 0x3fcb417f40000000ull, 0x3fcb638070000000ull,
    0x3fcb856cf0000000ull, 0x3fcba744b0000000ull, 0x3fcbc907f0000000ull, 0x3fcbeab6c0000000ull,
    0x3fcc0c5130000000ull, 0x3fcc2dd750000000ull, 0x3fcc4f4950000000ull, 0x3fcc70a740000000ull,
    0x3fcc91f130000000ull, 0x3fccb32740000000ull, 0x3fccd44980000000ull, 0x3fccf55810000000ull,
    0x3fcd165300000000ull, 0x3fcd373a60000000ull, 0x3fcd580e60000000ull, 0x3fcd78cf00000000ull,
    0x3fcd997c70000000ull, 0x3fcdba16a0000000ull, 0x3fcdda9dd0000000ull, 0x3fcdfb11f0000000ull,
    0x3fce1b7330000000ull, 0x3fce3bc1a0000000ull, 0x3fce5bfd50000000ull, 0x3fce7c2660000000ull,
    0x3fce9c3ce0000000ull, 0x3fcebc40e0000000ull, 0x3fcedc3280000000ull, 0x3fcefc11d0000000ull,
    0x3fcf1bdee0000000ull, 0x3fcf3b99d0000000ull, 0x3fcf5b42a0000000ull, 0x3fcf7ad980000000ull,
    0x3fcf9a5e70000000ull, 0x3fcfb9d190000000ull, 0x3fcfd932f0000000ull, 0x3fcff882a0000000ull,
    0x3fd00be050000000ull, 0x3fd01b76a0000000ull, 0x3fd02b0430000000ull, 0x3fd03a8910000000ull,
    0x3fd04a0540000000ull, 0x3fd05978e0000000ull, 0x3fd068e3f0000000ull, 0x3fd0784670000000ull,
    0x3fd087a080000000ull, 0x3fd096f210000000ull, 0x3fd0a63b30000000ull, 0x3fd0b57bf0000000ull,
    0x3fd0c4b450000000ull, 0x3fd0d3e460000000ull, 0x3fd0e30c30000000ull, 0x3fd0f22bc0000000ull,
    0x3fd1014310000000ull, 0x3fd1105240000000ull, 0x3fd11f5940000000ull, 0x3fd12e5830000000ull,
    0x3fd13d4f00000000ull, 0x3fd14c3dd0000000ull, 0x3fd15b24a0000000ull, 0x3fd16a0370000000ull,
    0x3fd178da50000000ull, 0x3fd187a940000000ull, 0x3fd1967060000000ull, 0x3fd1a52fa0000000ull,
    0x3fd1b3e710000000ull, 0x3fd1c296c0000000ull, 0x3fd1d13eb0000000ull, 0x3fd1dfdef0000000ull,
    0x3fd1ee7770000000ull, 0x3fd1fd0860000000ull, 0x3fd20b91a0000000ull, 0x3fd21a1350000000ull,
    0x3fd2288d70000000ull, 0x3fd2370010000000ull, 0x3fd2456b30000000ull, 0x3fd253ced0000000ull,
    0x3fd2622b00000000ull, 0x3fd2707fd0000000ull, 0x3fd27ecd40000000ull, 0x3fd28d1360000000ull,
    0x3fd29b5220000000ull, 0x3fd2a989a0000000ull, 0x3fd2b7b9e0000000ull, 0x3fd2c5e2e0000000ull,
    0x3fd2d404b0000000ull, 0x3fd2e21f50000000ull, 0x3fd2f032c0000000ull, 0x3fd2fe3f20000000ull,
    0x3fd30c4470000000ull, 0x3fd31a42b0000000ull, 0x3fd32839e0000000ull, 0x3fd3362a10000000ull,
    0x3fd3441350000000ull,
};
constexpr std::uint64_t kLog10Tail[257] = {  // rva 0x1086b0
    0x0000000000000000ull, 0x3db20abc22b2208full, 0x3db10f69332e0dd4ull, 0x3dce950de87ed257ull,
    0x3dd3f3443b626d69ull, 0x3df45aeaa5363e57ull, 0x3dc443683ce1bf0bull, 0x3df989cd60c6a511ull,
    0x3dfd626f201f2e9full, 0x3de94f8bb8dabdcdull, 0x3e0088d8ef423015ull, 0x3e080413a62b79adull,
    0x3e059717c0eed3c4ull, 0x3dad4a77add44902ull, 0x3e0e763ff037300eull, 0x3de162d74706f6c3ull,
    0x3e0601cc1f4dbc14ull, 0x3deaf3e051f6e5bfull, 0x3e097a0b1e1af3ebull, 0x3dc0a38970c002c7ull,
    0x3e102e000057c751ull, 0x3e155b00eecd6e0eull, 0x3ddf86297003b5afull, 0x3e1057b9b336a36dull,
    0x3e134bc84a06ea4full, 0x3e1643da9ea1bcadull, 0x3e1d66a7b4f7ea2aull, 0x3df6b2e038f7fcefull,
    0x3df3e954c670f088ull, 0x3e047209093acab3ull, 0x3e1d708fe7275da7ull, 0x3e1fdf9e7771b9e7ull,
    0x3e0827bfa70a0660ull, 0x3e1601cc1f4dbc14ull, 0x3e0637f6106a5e5bull, 0x3e126a13f17c624bull,
    0x3e093eb2ce80623aull, 0x3e1430d1e91594deull, 0x3e1d6b10108fa031ull, 0x3e16879c0bbaf241ull,
    0x3dff08015ea6bc2bull, 0x3e29b63dcdc6676cull, 0x3e2b022cbcc4ab2cull, 0x3df917d07ddd6544ull,
    0x3e1540605703379eull, 0x3e0cd18b947a1b60ull, 0x3e17ad65277ca97eull, 0x3e11884dc59f5fa9ull,
    0x3e1711c46006d082ull, 0x3e2f092e3c3108f8ull, 0x3e1714c5e32be13aull, 0x3e26bba7fd734f9aull,
    0x3dfdf48fb5e08483ull, 0x3e232f9bc74d0b95ull, 0x3df973e848790c13ull, 0x3e1eccbc08c6586eull,
    0x3e2115e9f9524a98ull, 0x3e2f1740593131b8ull, 0x3e1bcf8b25643835ull, 0x3e1f5fa81d8bed80ull,
    0x3e244a4df929d9e4ull, 0x3e129820d8220c94ull, 0x3e2a0b489304e309ull, 0x3e1f4d56aba665feull,
    0x3e210c9019365163ull, 0x3df80f78fe592736ull, 0x3e10528825c81ccaull, 0x3de095537d6d746aull,
    0x3e1827bfa70a0660ull, 0x3e06b0a8ec45933cull, 0x3e105af81bf5dba9ull, 0x3e17e2fa2655d515ull,
    0x3e0d59ecbfaee4bfull, 0x3e1d8b2fda683fa3ull, 0x3e24b8ddfd3a3737ull, 0x3e13827e61ae1204ull,
    0x3e2c8c7b49e90f9full, 0x3e29eaf01597591dull, 0x3e19aaa66e317b36ull, 0x3e2e725609720655ull,
    0x3e261c33fc7aac54ull, 0x3e29662bcf61a252ull, 0x3e1843c811c42730ull, 0x3e2064bb0b5acb36ull,
    0x3e0a340c842701a4ull, 0x3e1a8e55b58f79d6ull, 0x3de92d219c5e9d9aull, 0x3e3f63e60d7ffd6aull,
    0x3e2e9b0ed9516314ull, 0x3e2923901962350cull, 0x3e326f8838785e81ull, 0x3e3b5b6a4caba6afull,
    0x3df0226adc8e761cull, 0x3e3c4ad7313a1aedull, 0x3e1564e87c738d17ull, 0x3e338fecf18a6618ull,
    0x3e3d929ef5777666ull, 0x3e39483bf08da0b8ull, 0x3e3bdd0eeeaa5826ull, 0x3e39c4dd590237baull,
    0x3e1af3e9e0ebcac7ull, 0x3e35ce5382270dacull, 0x3e394f74532ab9baull, 0x3e07342795888654ull,
    0x3e0c5a000be34bf0ull, 0x3e2711c46006d082ull, 0x3e250025b4ed8cf8ull, 0x3e2ed18bcef2d2a0ull,
    0x3e21282e0c0a7554ull, 0x3e0d70f33359a7caull, 0x3e2b7f7e13a84025ull, 0x3e33306ec321891eull,
    0x3e3fc7f8038b7550ull, 0x3e3eb0358cd71d64ull, 0x3e3a76c822859474ull, 0x3e3d0ec652de86e3ull,
    0x3e2fa4cce08658afull, 0x3e3b84a2d2c00a9eull, 0x3e20a5b0f2c25bd1ull, 0x3e3dd660225bf699ull,
    0x3e08b10f859bf037ull, 0x3e3e8823b590cbe1ull, 0x3e361311f31e96f6ull, 0x3e2e1f875ca20f9aull,
    0x3e2c95724939b9a5ull, 0x3e3805957a3e58e2ull, 0x3e2ff126ea9f0334ull, 0x3e3953f5598e5609ull,
    0x3e36c16ff856c448ull, 0x3e24cb220ff261f4ull, 0x3e35e120d53d53a2ull, 0x3e3a527f6189f256ull,
    0x3e3856fcffd49c0full, 0x3e300c2e8228d7daull, 0x3df113d09444dfe0ull, 0x3e2510630eea59a6ull,
    0x3e262e780f32d711ull, 0x3ded3ed91a10f8cfull, 0x3e23654a7e4bcd85ull, 0x3e055b784980ad21ull,
    0x3e212f2dd4b16e64ull, 0x3e37c4add939f50cull, 0x3e281784627180fcull, 0x3dea5162c7e14961ull,
    0x3e310c9019365163ull, 0x3e373c4d2ba17688ull, 0x3e2ae8a5e0e93d81ull, 0x3e2ab0c6f01621afull,
    0x3e301e8b74dd5b66ull, 0x3e2d206fecbb5494ull, 0x3df0b48b724fcc00ull, 0x3e3f831f0b61e229ull,
    0x3df81a97c407bcafull, 0x3e3e286c1ccbb7aaull, 0x3e28630b49220a93ull, 0x3dff0b15c1a22c5cull,
    0x3e355445e71c0946ull, 0x3e3be630f8066d85ull, 0x3e2599dff0d96c39ull, 0x3e36cc85b18fb081ull,
    0x3e34476d001ea8c8ull, 0x3e373f889e16d31full, 0x3e3357100d792a87ull, 0x3e3bd179ae6101f6ull,
    0x3e0ca31056c3f6e2ull, 0x3e3d2870629c08fbull, 0x3e3aba3880d2673full, 0x3e2c3633cb297da6ull,
    0x3e21843899efea02ull, 0x3e3bccc99d2008e6ull, 0x3e38000544bdd350ull, 0x3e2b91c226606ae1ull,
    0x3e2a7adf26b62bdfull, 0x3e18764fc8826ec9ull, 0x3e1f4f3de50f68f0ull, 0x3df760ca757995e3ull,
    0x3dfc667ed3805147ull, 0x3e3733f6196adf6full, 0x3e2fb710f33e836bull, 0x3e39886eba641013ull,
    0x3dfb5368d0af8c1aull, 0x3e358c691b8d2971ull, 0x3dfe9465226d08fbull, 0x3e33587e063f0097ull,
    0x3e3618e702129f18ull, 0x3e361c33fc7aac54ull, 0x3e3f07a68408604aull, 0x3e3c34bfe4945421ull,
    0x3e38b1f00e41300bull, 0x3e3f434284d61b63ull, 0x3e3a63095e397436ull, 0x3e34428656b919deull,
    0x3e36ca9201b2d9a6ull, 0x3e2738823a2a931cull, 0x3e3c11880e179230ull, 0x3e313ddc8d6d52feull,
    0x3e33eed58922e917ull, 0x3e295992846bdd50ull, 0x3e0ddb4d5f2e278bull, 0x3df1a5f12a0635c4ull,
    0x3e4642f0882c3c34ull, 0x3e2aee9ba7f6475eull, 0x3e264b7f834a60e4ull, 0x3e290d42e243792eull,
    0x3e4c272008134f01ull, 0x3e4a782e16d6cf5bull, 0x3e44505c79da6648ull, 0x3e4ca9d4ea4dcd21ull,
    0x3e297d3d627cd5bcull, 0x3e20b15cf9bcaa13ull, 0x3e315b2063cf76ddull, 0x3e2983e6f3aa2748ull,
    0x3e3f4c64f4ffe994ull, 0x3e46beba7ce85a0full, 0x3e3b9c69fd4ea6b8ull, 0x3e2b6aa5835fa4abull,
    0x3e43ccc3790fedd1ull, 0x3e29c04cc4404fe0ull, 0x3e40734b7a75d89dull, 0x3e1b4404c4e01612ull,
    0x3e40c565c2ce4894ull, 0x3e33c71441d935cdull, 0x3d72a492556b3b4eull, 0x3e20fa090341dc43ull,
    0x3e2e8f7009e3d9f4ull, 0x3e4b1bf68b048a45ull, 0x3e3eee52dffaa956ull, 0x3e456b0900e465bdull,
    0x3e4d929ef5777666ull, 0x3e486ea28637e260ull, 0x3e4665aff10ca2f0ull, 0x3e2f11fdaf48ec74ull,
    0x3e4cbe1b86a4d1c7ull, 0x3e25b05bfea87665ull, 0x3e41cec20a1a4a1dull, 0x3e41cd5f0a409b9full,
    0x3e453656c8265070ull, 0x3e377ed835282260ull, 0x3e2417bc3040b9d2ull, 0x3e408eef7b79eff2ull,
    0x3e4dc76f39dc57e9ull, 0x3e4c0493a70cf457ull, 0x3e4a83d6cea5a60cull, 0x3e30d6700dc557baull,
    0x3e44c96c12e8bd0aull, 0x3e3d2c1993e32315ull, 0x3e22c721135f8242ull, 0x3e279a3e4dda747dull,
    0x3dfcf89f6941a72bull, 0x3e2149a702f10831ull, 0x3e4ead4b7c8175dbull, 0x3e4e6930fe63e70aull,
    0x3e41e106bed9ee2full, 0x3e2d682b82f11c92ull, 0x3e3a07f188dba47cull, 0x3e40f9342dc172f6ull,
    0x3e03ef3fde623e25ull,
};
constexpr std::uint64_t kExpFull[64] = {  // rva 0x10c580
    0x3ff0000000000000ull, 0x3ff02c9a3e778061ull, 0x3ff059b0d3158574ull, 0x3ff0874518759bc8ull,
    0x3ff0b5586cf9890full, 0x3ff0e3ec32d3d1a2ull, 0x3ff11301d0125b51ull, 0x3ff1429aaea92de0ull,
    0x3ff172b83c7d517bull, 0x3ff1a35beb6fcb75ull, 0x3ff1d4873168b9aaull, 0x3ff2063b88628cd6ull,
    0x3ff2387a6e756238ull, 0x3ff26b4565e27cddull, 0x3ff29e9df51fdee1ull, 0x3ff2d285a6e4030bull,
    0x3ff306fe0a31b715ull, 0x3ff33c08b26416ffull, 0x3ff371a7373aa9cbull, 0x3ff3a7db34e59ff7ull,
    0x3ff3dea64c123422ull, 0x3ff4160a21f72e2aull, 0x3ff44e086061892dull, 0x3ff486a2b5c13cd0ull,
    0x3ff4bfdad5362a27ull, 0x3ff4f9b2769d2ca7ull, 0x3ff5342b569d4f82ull, 0x3ff56f4736b527daull,
    0x3ff5ab07dd485429ull, 0x3ff5e76f15ad2148ull, 0x3ff6247eb03a5585ull, 0x3ff6623882552225ull,
    0x3ff6a09e667f3bcdull, 0x3ff6dfb23c651a2full, 0x3ff71f75e8ec5f74ull, 0x3ff75feb564267c9ull,
    0x3ff7a11473eb0187ull, 0x3ff7e2f336cf4e62ull, 0x3ff82589994cce13ull, 0x3ff868d99b4492edull,
    0x3ff8ace5422aa0dbull, 0x3ff8f1ae99157736ull, 0x3ff93737b0cdc5e5ull, 0x3ff97d829fde4e50ull,
    0x3ff9c49182a3f090ull, 0x3ffa0c667b5de565ull, 0x3ffa5503b23e255dull, 0x3ffa9e6b5579fdbfull,
    0x3ffae89f995ad3adull, 0x3ffb33a2b84f15fbull, 0x3ffb7f76f2fb5e47ull, 0x3ffbcc1e904bc1d2ull,
    0x3ffc199bdd85529cull, 0x3ffc67f12e57d14bull, 0x3ffcb720dcef9069ull, 0x3ffd072d4a07897cull,
    0x3ffd5818dcfba487ull, 0x3ffda9e603db3285ull, 0x3ffdfc97337b9b5full, 0x3ffe502ee78b3ff6ull,
    0x3ffea4afa2a490daull, 0x3ffefa1bee615a27ull, 0x3fff50765b6e4540ull, 0x3fffa7c1819e90d8ull,
};
constexpr std::uint64_t kExpTail[64] = {  // rva 0x10c380
    0x0000000000000000ull, 0x3e6cef00c1dcdef9ull, 0x3e48ac2ba1d73e2aull, 0x3e60eb37901186beull,
    0x3e69f3121ec53172ull, 0x3e469e8d10103a17ull, 0x3df25b50a4ebbf1aull, 0x3e6d525bbf668203ull,
    0x3e68faa2f5b9bef9ull, 0x3e66df96ea796d31ull, 0x3e368b9aa7805b80ull, 0x3e60c519ac771dd6ull,
    0x3e6ceac470cd83f5ull, 0x3e5789f37495e99cull, 0x3e547f7b84b09745ull, 0x3e5b900c2d002475ull,
    0x3e64636e2a5bd1abull, 0x3e4320b7fa64e430ull, 0x3e5ceaa72a9c5154ull, 0x3e53967fdba86f24ull,
    0x3e682468446b6824ull, 0x3e3f72e29f84325bull, 0x3e18624b40c4dbd0ull, 0x3e5704f3404f068eull,
    0x3e54d8a89c750e5eull, 0x3e5a74b29ab4cf62ull, 0x3e5a753e077c2a0full, 0x3e5ad49f699bb2c0ull,
    0x3e6a90a852b19260ull, 0x3e56b48521ba6f93ull, 0x3e0d2ac258f87d03ull, 0x3e42a91124893ecfull,
    0x3e59fcef32422cbeull, 0x3e68ca345de441c5ull, 0x3e61d8bee7ba46e1ull, 0x3e59099f22fdba6aull,
    0x3e4f580c36bea881ull, 0x3e5b3d398841740aull, 0x3e62999c25159f11ull, 0x3e668925d901c83bull,
    0x3e415506dadd3e2aull, 0x3e622aee6c57304eull, 0x3e29b8bc9e8a0387ull, 0x3e6fbc9c9f173d24ull,
    0x3e451f8480e3e235ull, 0x3e66bbcac96535b5ull, 0x3e41f12ae45a1224ull, 0x3e55e7f6fd0fac90ull,
    0x3e62b5a75abd0e69ull, 0x3e609e2bf5ed7fa1ull, 0x3e47daf237553d84ull, 0x3e12f074891ee83dull,
    0x3e6b0aa538444196ull, 0x3e6cafa29694426full, 0x3e69df20d22a0797ull, 0x3e640f12f71a1e45ull,
    0x3e69f7490e4bb40bull, 0x3e4ed9942b84600dull, 0x3e4bdcdaf5cb4656ull, 0x3e5e2cffd89cf44cull,
    0x3e452486cc2c7b9dull, 0x3e6cc2b44eee3fa4ull, 0x3e66dc8a80ce9f09ull, 0x3e39e90d82e90a7eull,
};
constexpr std::uint64_t kExpLead[64] = {  // rva 0x10c180
    0x3ff0000000000000ull, 0x3ff02c9a30000000ull, 0x3ff059b0d0000000ull, 0x3ff0874510000000ull,
    0x3ff0b55860000000ull, 0x3ff0e3ec30000000ull, 0x3ff11301d0000000ull, 0x3ff1429aa0000000ull,
    0x3ff172b830000000ull, 0x3ff1a35be0000000ull, 0x3ff1d48730000000ull, 0x3ff2063b80000000ull,
    0x3ff2387a60000000ull, 0x3ff26b4560000000ull, 0x3ff29e9df0000000ull, 0x3ff2d285a0000000ull,
    0x3ff306fe00000000ull, 0x3ff33c08b0000000ull, 0x3ff371a730000000ull, 0x3ff3a7db30000000ull,
    0x3ff3dea640000000ull, 0x3ff4160a20000000ull, 0x3ff44e0860000000ull, 0x3ff486a2b0000000ull,
    0x3ff4bfdad0000000ull, 0x3ff4f9b270000000ull, 0x3ff5342b50000000ull, 0x3ff56f4730000000ull,
    0x3ff5ab07d0000000ull, 0x3ff5e76f10000000ull, 0x3ff6247eb0000000ull, 0x3ff6623880000000ull,
    0x3ff6a09e60000000ull, 0x3ff6dfb230000000ull, 0x3ff71f75e0000000ull, 0x3ff75feb50000000ull,
    0x3ff7a11470000000ull, 0x3ff7e2f330000000ull, 0x3ff8258990000000ull, 0x3ff868d990000000ull,
    0x3ff8ace540000000ull, 0x3ff8f1ae90000000ull, 0x3ff93737b0000000ull, 0x3ff97d8290000000ull,
    0x3ff9c49180000000ull, 0x3ffa0c6670000000ull, 0x3ffa5503b0000000ull, 0x3ffa9e6b50000000ull,
    0x3ffae89f90000000ull, 0x3ffb33a2b0000000ull, 0x3ffb7f76f0000000ull, 0x3ffbcc1e90000000ull,
    0x3ffc199bd0000000ull, 0x3ffc67f120000000ull, 0x3ffcb720d0000000ull, 0x3ffd072d40000000ull,
    0x3ffd5818d0000000ull, 0x3ffda9e600000000ull, 0x3ffdfc9730000000ull, 0x3ffe502ee0000000ull,
    0x3ffea4afa0000000ull, 0x3ffefa1be0000000ull, 0x3fff507650000000ull, 0x3fffa7c180000000ull,
};

constexpr std::uint64_t kQuiet = 0x0008000000000000ull;

// Shared front end of log and log10. Returns true with *out set for the special inputs; otherwise sets e (the
// exponent), d = x - 1, and the table index / reduced argument pieces.
struct LogArgs {
    double e;      // unbiased exponent (after normalising a denormal)
    double d;      // x - 1
    std::uint64_t mant_f;   // bits of the mantissa source for f
    std::uint64_t mant_idx; // bits the index is taken from
};

bool log_special(double x, double* out) {
    const std::uint64_t ux = bits(x);
    if ((ux & 0x7ff0000000000000ull) == 0x7ff0000000000000ull) {
        if (ux == 0x7ff0000000000000ull) { *out = x; return true; }
        if (ux == 0xfff0000000000000ull) { *out = dbl(0xfff8000000000000ull); return true; }
        *out = dbl(ux | kQuiet);
        return true;
    }
    if (!(x > 0.0)) {
        *out = (x == 0.0) ? dbl(0xfff0000000000000ull) : dbl(0xfff8000000000000ull);
        return true;
    }
    return false;
}

LogArgs log_args(double x) {
    const std::uint64_t ux = bits(x);
    LogArgs a;
    a.e = static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>((ux >> 52) - 0x3ff)));
    a.d = x - 1.0;
    std::uint64_t m = ux & 0x000fffffffffffffull;
    std::uint64_t src = ux;
    if (a.e == -1023.0) {  // denormal: (m | 1.0) - 1.0 normalises it
        const std::uint64_t n = bits(dbl(m | 0x3ff0000000000000ull) - 1.0);
        a.e = static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(n >> 52) - 0x7fd));
        m = n & 0x000fffffffffffffull;
        src = m;
    }
    a.mant_f = m;
    a.mant_idx = (src & 0x000ff00000000000ull) + ((src & 0x0000080000000000ull) << 1);
    return a;
}

// -log(1 - r) ~ r + r^2/2 + r^3/3 + r^4/4 + r^5/5 + r^6/6 with r = (F - f) / F; returns r's polynomial q and idx.
double log_q(const LogArgs& a, std::uint32_t* idx_out) {
    const std::uint32_t idx = static_cast<std::uint32_t>(a.mant_idx >> 44);
    const double f = dbl(a.mant_f | 0x3fe0000000000000ull);
    const double big_f = dbl(a.mant_idx | 0x3fe0000000000000ull);
    const double r = (big_f - f) * dbl(kInvF[idx]);
    const double r2 = r * r;
    double c = std::fma(r, dbl(0x3fc5555555555555ull), 0.2);
    const double b = std::fma(r, dbl(0x3fd5555555555555ull), 0.5);
    c = std::fma(r, c, 0.25);
    const double r4 = r2 * r2;
    double q = std::fma(b, r2, r);
    q = std::fma(c, r4, q);
    *idx_out = idx;
    return q;
}

}  // namespace

double msvc_exp(double x) {
    const std::uint64_t ux = bits(x);
    if ((ux & 0x7fffffffffffffffull) >= 0x7ff0000000000000ull) {
        if (ux == 0x7ff0000000000000ull) return x;
        if (ux == 0xfff0000000000000ull) return 0.0;
        return dbl(ux | kQuiet);
    }
    if (!(x <= dbl(0x40862e42fefa39efull) && !(x < dbl(0xc0874046dfefd9d0ull)))) {
        if (x > dbl(0x40862e42fefa39efull)) return dbl(0x7ff0000000000000ull);  // overflow
        if (x < dbl(0xc0874910d52d3051ull)) return 0.0;                         // underflow
        return dbl(1);                                                           // smallest denormal
    }
    if ((ux & 0x7fffffffffffffffull) <= 0x3e50000000000000ull) return x + 1.0;  // |x| <= 2^-26

    const double z = x * dbl(0x40571547652b82feull);  // 64/ln2
    const double nd = std::trunc(z);
    const int n = static_cast<int>(z);
    const double r1 = std::fma(nd, dbl(0xbf862e42fefa0000ull), x);
    const double r = nd * dbl(0xbd1cf79abc9e3b39ull) + r1;
    const int j = n & 0x3f;
    const int m = n >> 6;
    double p = std::fma(r, dbl(0x3f56c16c16c16c17ull), dbl(0x3f81111111111111ull));
    p = std::fma(r, p, dbl(0x3fa5555555555555ull));
    p = std::fma(r, p, dbl(0x3fc5555555555555ull));
    p = std::fma(r, p, 0.5);
    const double q = std::fma(r * r, p, r);
    const double res = (q * dbl(kExpFull[j]) + dbl(kExpTail[j])) + dbl(kExpLead[j]);
    if (m > -1022 || (m == -1022 && res >= 1.0))
        return dbl(bits(res) + (static_cast<std::uint64_t>(static_cast<std::uint32_t>(m)) << 52));
    return res * dbl(1ull << ((m + 1074) & 63));  // 2^m into the denormal range
}

double msvc_log(double x) {
    double special;
    if (log_special(x, &special)) return special;
    const LogArgs a = log_args(x);
    if (std::fabs(a.d) < 0.0625) {
        const double d = x - 1.0;
        const double u0 = d / (2.0 + d);
        const double corr = d * u0;
        const double u = u0 + u0;
        const double v = u * u;
        double pa = std::fma(v, dbl(0x3f89999999bac6d4ull), dbl(0x3fb55555555554e6ull));
        const double pb = std::fma(v, dbl(0x3f3c8034c85dfff0ull), dbl(0x3f62492307f1519full));
        const double u3 = v * u;
        pa = pa * u3;
        const double u7 = (u3 * u3) * u;
        pa = std::fma(pb, u7, pa);
        return d + (pa - corr);
    }
    std::uint32_t idx;
    const double q = log_q(a, &idx);
    const double t = std::fma(a.e, dbl(0x3e6efa39ef35793cull), -q);  // e * ln2_tail - q
    const double lo = dbl(kLogTail[idx]) + t;
    const double hi = std::fma(a.e, dbl(0x3fe62e42e0000000ull), dbl(kLogLead[idx]));
    return hi + lo;
}

double msvc_log10(double x) {
    double special;
    if (log_special(x, &special)) return special;
    const LogArgs a = log_args(x);
    if (std::fabs(a.d) < 0.0625) {
        const double d = x - 1.0;
        const double u0 = d / (2.0 + d);
        const double corr = d * u0;
        const double u = u0 + u0;
        const double v = u * u;
        double pa = std::fma(v, dbl(0x3f89999999bac6d4ull), dbl(0x3fb55555555554e6ull));
        double pb = std::fma(v, dbl(0x3f3c8034c85dfff0ull), dbl(0x3f62492307f1519full));
        const double u3 = v * u;
        pa = pa * u3;
        const double u7 = (u3 * u3) * u;
        pb = pb * u7;
        double poly = (pa + pb) - corr;
        const double dh = dbl(bits(d) & 0xffffffff00000000ull);
        poly = poly + (d - dh);
        const double head = dbl(0x3fdbcb7800000000ull), tail = dbl(0x3ea8a93728719535ull);
        return ((dh * tail + poly * tail) + poly * head) + dh * head;
    }
    std::uint32_t idx;
    const double q = log_q(a, &idx) * dbl(0x3fdbcb7b1526e50eull);              // * log10(e)
    const double t = std::fma(a.e, dbl(0x3e03ef3fde623e25ull), -q);            // e * log10(2)_tail - q
    const double lo = dbl(kLog10Tail[idx]) + t;
    const double hi = std::fma(a.e, dbl(0x3fd3441350000000ull), dbl(kLog10Lead[idx]));
    return hi + lo;
}

}  // namespace cleria
