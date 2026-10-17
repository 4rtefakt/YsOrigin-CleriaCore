// pow(double, double) of the x64 Universal CRT, bit for bit, on every platform.
//
// Source: C:\Windows\System32\ucrtbase.dll 10.0.26100.9711, export pow at RVA 0x766e0. The entry tests the CPU
// feature word at RVA 0x139e60 ((v & 3) == 3: AVX2/FMA3) and jumps to the FMA3 body at RVA 0x5e990, otherwise to
// an SSE2 body at RVA 0x9dd10. The word reads 3 on the reference PC, so this is the FMA3 body (0x5e990..0x5f092)
// with its helpers: __math_divzero 0xa30b0, __math_invalid 0xe4950, __math_oflow 0xe4990, __math_uflow 0x916dc
// (returns a signed zero directly), check_oflow 0xa3c6c / check_uflow 0x7a030 (errno only). A Windows machine
// without FMA3 would run the SSE2 body, which this file does not reproduce.
// Algorithm family: AMD libm (aocl-libm-ose), itself derived from the Arm optimized-routines pow (MIT / Apache-2.0
// WITH LLVM-exception, Copyright (c) 2018 Arm Ltd.): log with a 128-entry table, exp with a 256-entry table, and
// AMD's own log polynomial plus y == 1 and |x| == 1 shortcuts. The disassembly is the authority. Every vfmadd is a
// std::fma below, every other operation rounds on its own, in the CRT's order (compile with -ffp-contract=off or
// /fp:precise). errno and floating-point exception flags are not modelled.
#include <cmath>
#include <cstdint>
#include <cstring>

namespace cleria {
namespace {

inline std::uint64_t bits(double d) { std::uint64_t u; std::memcpy(&u, &d, 8); return u; }
inline double dbl(std::uint64_t u) { double d; std::memcpy(&d, &u, 8); return d; }

// Log table: 128 entries of {invc, (unused), logc, logctail}.
// Exp table: 256 entries of {tail, sbits}.
constexpr std::uint64_t kPowLog[512] = {  // rva 0x102de8
    0x3ff6a00000000000ull, 0x0000000000000000ull, 0xbfd62c82f2b9c800ull, 0x3cfab42428375680ull,
    0x3ff6800000000000ull, 0x0000000000000000ull, 0xbfd5d1bdbf580800ull, 0xbd1ca508d8e0f720ull,
    0x3ff6600000000000ull, 0x0000000000000000ull, 0xbfd5767717455800ull, 0xbd2362a4d5b6506dull,
    0x3ff6400000000000ull, 0x0000000000000000ull, 0xbfd51aad872df800ull, 0xbce684e49eb067d5ull,
    0x3ff6200000000000ull, 0x0000000000000000ull, 0xbfd4be5f95777800ull, 0xbd041b6993293ee0ull,
    0x3ff6000000000000ull, 0x0000000000000000ull, 0xbfd4618bc21c6000ull, 0x3d13d82f484c84ccull,
    0x3ff5e00000000000ull, 0x0000000000000000ull, 0xbfd404308686a800ull, 0x3cdc42f3ed820b3aull,
    0x3ff5c00000000000ull, 0x0000000000000000ull, 0xbfd3a64c55694800ull, 0x3d20b1c686519460ull,
    0x3ff5a00000000000ull, 0x0000000000000000ull, 0xbfd347dd9a988000ull, 0x3d25594dd4c58092ull,
    0x3ff5800000000000ull, 0x0000000000000000ull, 0xbfd2e8e2bae12000ull, 0x3d267b1e99b72bd8ull,
    0x3ff5600000000000ull, 0x0000000000000000ull, 0xbfd2895a13de8800ull, 0x3d15ca14b6cfb03full,
    0x3ff5600000000000ull, 0x0000000000000000ull, 0xbfd2895a13de8800ull, 0x3d15ca14b6cfb03full,
    0x3ff5400000000000ull, 0x0000000000000000ull, 0xbfd22941fbcf7800ull, 0xbd165a242853da76ull,
    0x3ff5200000000000ull, 0x0000000000000000ull, 0xbfd1c898c1699800ull, 0xbd1fafbc68e75404ull,
    0x3ff5000000000000ull, 0x0000000000000000ull, 0xbfd1675cababa800ull, 0x3d1f1fc63382a8f0ull,
    0x3ff4e00000000000ull, 0x0000000000000000ull, 0xbfd1058bf9ae4800ull, 0xbd26a8c4fd055a66ull,
    0x3ff4c00000000000ull, 0x0000000000000000ull, 0xbfd0a324e2739000ull, 0xbd0c6bee7ef4030eull,
    0x3ff4a00000000000ull, 0x0000000000000000ull, 0xbfd0402594b4d000ull, 0xbcf036b89ef42d7full,
    0x3ff4a00000000000ull, 0x0000000000000000ull, 0xbfd0402594b4d000ull, 0xbcf036b89ef42d7full,
    0x3ff4800000000000ull, 0x0000000000000000ull, 0xbfcfb9186d5e4000ull, 0x3d0d572aab993c87ull,
    0x3ff4600000000000ull, 0x0000000000000000ull, 0xbfcef0adcbdc6000ull, 0x3d2b26b79c86af24ull,
    0x3ff4400000000000ull, 0x0000000000000000ull, 0xbfce27076e2af000ull, 0xbd172f4f543fff10ull,
    0x3ff4200000000000ull, 0x0000000000000000ull, 0xbfcd5c216b4fc000ull, 0x3d21ba91bbca681bull,
    0x3ff4000000000000ull, 0x0000000000000000ull, 0xbfcc8ff7c79aa000ull, 0x3d27794f689f8434ull,
    0x3ff4000000000000ull, 0x0000000000000000ull, 0xbfcc8ff7c79aa000ull, 0x3d27794f689f8434ull,
    0x3ff3e00000000000ull, 0x0000000000000000ull, 0xbfcbc286742d9000ull, 0x3d194eb0318bb78full,
    0x3ff3c00000000000ull, 0x0000000000000000ull, 0xbfcaf3c94e80c000ull, 0x3cba4e633fcd9066ull,
    0x3ff3a00000000000ull, 0x0000000000000000ull, 0xbfca23bc1fe2b000ull, 0xbd258c64dc46c1eaull,
    0x3ff3a00000000000ull, 0x0000000000000000ull, 0xbfca23bc1fe2b000ull, 0xbd258c64dc46c1eaull,
    0x3ff3800000000000ull, 0x0000000000000000ull, 0xbfc9525a9cf45000ull, 0xbd2ad1d904c1d4e3ull,
    0x3ff3600000000000ull, 0x0000000000000000ull, 0xbfc87fa06520d000ull, 0x3d2bbdbf7fdbfa09ull,
    0x3ff3400000000000ull, 0x0000000000000000ull, 0xbfc7ab890210e000ull, 0x3d2bdb9072534a58ull,
    0x3ff3400000000000ull, 0x0000000000000000ull, 0xbfc7ab890210e000ull, 0x3d2bdb9072534a58ull,
    0x3ff3200000000000ull, 0x0000000000000000ull, 0xbfc6d60fe719d000ull, 0xbd10e46aa3b2e266ull,
    0x3ff3000000000000ull, 0x0000000000000000ull, 0xbfc5ff3070a79000ull, 0xbd1e9e439f105039ull,
    0x3ff3000000000000ull, 0x0000000000000000ull, 0xbfc5ff3070a79000ull, 0xbd1e9e439f105039ull,
    0x3ff2e00000000000ull, 0x0000000000000000ull, 0xbfc526e5e3a1b000ull, 0xbd20de8b90075b8full,
    0x3ff2c00000000000ull, 0x0000000000000000ull, 0xbfc44d2b6ccb8000ull, 0x3d170cc16135783cull,
    0x3ff2c00000000000ull, 0x0000000000000000ull, 0xbfc44d2b6ccb8000ull, 0x3d170cc16135783cull,
    0x3ff2a00000000000ull, 0x0000000000000000ull, 0xbfc371fc201e9000ull, 0x3cf178864d27543aull,
    0x3ff2800000000000ull, 0x0000000000000000ull, 0xbfc29552f81ff000ull, 0xbd248d301771c408ull,
    0x3ff2600000000000ull, 0x0000000000000000ull, 0xbfc1b72ad52f6000ull, 0xbd2e80a41811a396ull,
    0x3ff2600000000000ull, 0x0000000000000000ull, 0xbfc1b72ad52f6000ull, 0xbd2e80a41811a396ull,
    0x3ff2400000000000ull, 0x0000000000000000ull, 0xbfc0d77e7cd09000ull, 0x3d0a699688e85bf4ull,
    0x3ff2400000000000ull, 0x0000000000000000ull, 0xbfc0d77e7cd09000ull, 0x3d0a699688e85bf4ull,
    0x3ff2200000000000ull, 0x0000000000000000ull, 0xbfbfec9131dbe000ull, 0xbd2575545ca333f2ull,
    0x3ff2000000000000ull, 0x0000000000000000ull, 0xbfbe27076e2b0000ull, 0x3d2a342c2af0003cull,
    0x3ff2000000000000ull, 0x0000000000000000ull, 0xbfbe27076e2b0000ull, 0x3d2a342c2af0003cull,
    0x3ff1e00000000000ull, 0x0000000000000000ull, 0xbfbc5e548f5bc000ull, 0xbd1d0c57585fbe06ull,
    0x3ff1c00000000000ull, 0x0000000000000000ull, 0xbfba926d3a4ae000ull, 0x3d253935e85baac8ull,
    0x3ff1c00000000000ull, 0x0000000000000000ull, 0xbfba926d3a4ae000ull, 0x3d253935e85baac8ull,
    0x3ff1a00000000000ull, 0x0000000000000000ull, 0xbfb8c345d631a000ull, 0x3d137c294d2f5668ull,
    0x3ff1a00000000000ull, 0x0000000000000000ull, 0xbfb8c345d631a000ull, 0x3d137c294d2f5668ull,
    0x3ff1800000000000ull, 0x0000000000000000ull, 0xbfb6f0d28ae56000ull, 0xbd269737c93373daull,
    0x3ff1600000000000ull, 0x0000000000000000ull, 0xbfb51b073f062000ull, 0x3d1f025b61c65e57ull,
    0x3ff1600000000000ull, 0x0000000000000000ull, 0xbfb51b073f062000ull, 0x3d1f025b61c65e57ull,
    0x3ff1400000000000ull, 0x0000000000000000ull, 0xbfb341d7961be000ull, 0x3d2c5edaccf913dfull,
    0x3ff1400000000000ull, 0x0000000000000000ull, 0xbfb341d7961be000ull, 0x3d2c5edaccf913dfull,
    0x3ff1200000000000ull, 0x0000000000000000ull, 0xbfb16536eea38000ull, 0x3d147c5e768fa309ull,
    0x3ff1000000000000ull, 0x0000000000000000ull, 0xbfaf0a30c0118000ull, 0x3d2d599e83368e91ull,
    0x3ff1000000000000ull, 0x0000000000000000ull, 0xbfaf0a30c0118000ull, 0x3d2d599e83368e91ull,
    0x3ff0e00000000000ull, 0x0000000000000000ull, 0xbfab42dd71198000ull, 0x3d1c827ae5d6704cull,
    0x3ff0e00000000000ull, 0x0000000000000000ull, 0xbfab42dd71198000ull, 0x3d1c827ae5d6704cull,
    0x3ff0c00000000000ull, 0x0000000000000000ull, 0xbfa77458f632c000ull, 0xbd2cfc4634f2a1eeull,
    0x3ff0c00000000000ull, 0x0000000000000000ull, 0xbfa77458f632c000ull, 0xbd2cfc4634f2a1eeull,
    0x3ff0a00000000000ull, 0x0000000000000000ull, 0xbfa39e87b9fec000ull, 0x3cf502b7f526feaaull,
    0x3ff0a00000000000ull, 0x0000000000000000ull, 0xbfa39e87b9fec000ull, 0x3cf502b7f526feaaull,
    0x3ff0800000000000ull, 0x0000000000000000ull, 0xbf9f829b0e780000ull, 0xbd2980267c7e09e4ull,
    0x3ff0800000000000ull, 0x0000000000000000ull, 0xbf9f829b0e780000ull, 0xbd2980267c7e09e4ull,
    0x3ff0600000000000ull, 0x0000000000000000ull, 0xbf97b91b07d58000ull, 0xbd288d5493faa639ull,
    0x3ff0400000000000ull, 0x0000000000000000ull, 0xbf8fc0a8b0fc0000ull, 0xbcdf1e7cf6d3a69cull,
    0x3ff0400000000000ull, 0x0000000000000000ull, 0xbf8fc0a8b0fc0000ull, 0xbcdf1e7cf6d3a69cull,
    0x3ff0200000000000ull, 0x0000000000000000ull, 0xbf7fe02a6b100000ull, 0xbd19e23f0dda40e4ull,
    0x3ff0200000000000ull, 0x0000000000000000ull, 0xbf7fe02a6b100000ull, 0xbd19e23f0dda40e4ull,
    0x3ff0000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull,
    0x3ff0000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull, 0x0000000000000000ull,
    0x3fefc00000000000ull, 0x0000000000000000ull, 0x3f80101575890000ull, 0xbd10c76b999d2be8ull,
    0x3fef800000000000ull, 0x0000000000000000ull, 0x3f90205658938000ull, 0xbd23dc5b06e2f7d2ull,
    0x3fef400000000000ull, 0x0000000000000000ull, 0x3f98492528c90000ull, 0xbd2aa0ba325a0c34ull,
    0x3fef000000000000ull, 0x0000000000000000ull, 0x3fa0415d89e74000ull, 0x3d0111c05cf1d753ull,
    0x3feec00000000000ull, 0x0000000000000000ull, 0x3fa466aed42e0000ull, 0xbd2c167375bdfd28ull,
    0x3fee800000000000ull, 0x0000000000000000ull, 0x3fa894aa149fc000ull, 0xbd197995d05a267dull,
    0x3fee400000000000ull, 0x0000000000000000ull, 0x3faccb73cdddc000ull, 0xbd1a68f247d82807ull,
    0x3fee200000000000ull, 0x0000000000000000ull, 0x3faeea31c006c000ull, 0xbd0e113e4fc93b7bull,
    0x3fede00000000000ull, 0x0000000000000000ull, 0x3fb1973bd1466000ull, 0xbd25325d560d9e9bull,
    0x3feda00000000000ull, 0x0000000000000000ull, 0x3fb3bdf5a7d1e000ull, 0x3d2cc85ea5db4ed7ull,
    0x3fed600000000000ull, 0x0000000000000000ull, 0x3fb5e95a4d97a000ull, 0xbd2c69063c5d1d1eull,
    0x3fed400000000000ull, 0x0000000000000000ull, 0x3fb700d30aeac000ull, 0x3cec1e8da99ded32ull,
    0x3fed000000000000ull, 0x0000000000000000ull, 0x3fb9335e5d594000ull, 0x3d23115c3abd47daull,
    0x3fecc00000000000ull, 0x0000000000000000ull, 0x3fbb6ac88dad6000ull, 0xbd1390802bf768e5ull,
    0x3feca00000000000ull, 0x0000000000000000ull, 0x3fbc885801bc4000ull, 0x3d2646d1c65aacd3ull,
    0x3fec600000000000ull, 0x0000000000000000ull, 0x3fbec739830a2000ull, 0xbd2dc068afe645e0ull,
    0x3fec400000000000ull, 0x0000000000000000ull, 0x3fbfe89139dbe000ull, 0xbd2534d64fa10afdull,
    0x3fec000000000000ull, 0x0000000000000000ull, 0x3fc1178e8227e000ull, 0x3d21ef78ce2d07f2ull,
    0x3febe00000000000ull, 0x0000000000000000ull, 0x3fc1aa2b7e23f000ull, 0x3d2ca78e44389934ull,
    0x3feba00000000000ull, 0x0000000000000000ull, 0x3fc2d1610c868000ull, 0x3d039d6ccb81b4a1ull,
    0x3feb800000000000ull, 0x0000000000000000ull, 0x3fc365fcb0159000ull, 0x3cc62fa8234b7289ull,
    0x3feb400000000000ull, 0x0000000000000000ull, 0x3fc4913d8333b000ull, 0x3d25837954fdb678ull,
    0x3feb200000000000ull, 0x0000000000000000ull, 0x3fc527e5e4a1b000ull, 0x3d2633e8e5697dc7ull,
    0x3feae00000000000ull, 0x0000000000000000ull, 0x3fc6574ebe8c1000ull, 0x3d19cf8b2c3c2e78ull,
    0x3feac00000000000ull, 0x0000000000000000ull, 0x3fc6f0128b757000ull, 0xbd25118de59c21e1ull,
    0x3feaa00000000000ull, 0x0000000000000000ull, 0x3fc7898d85445000ull, 0xbd1c661070914305ull,
    0x3fea600000000000ull, 0x0000000000000000ull, 0x3fc8beafeb390000ull, 0xbd073d54aae92cd1ull,
    0x3fea400000000000ull, 0x0000000000000000ull, 0x3fc95a5adcf70000ull, 0x3d07f22858a0ff6full,
    0x3fea000000000000ull, 0x0000000000000000ull, 0x3fca93ed3c8ae000ull, 0xbd28724350562169ull,
    0x3fe9e00000000000ull, 0x0000000000000000ull, 0x3fcb31d8575bd000ull, 0xbd0c358d4eace1aaull,
    0x3fe9c00000000000ull, 0x0000000000000000ull, 0x3fcbd087383be000ull, 0xbd2d4bc4595412b6ull,
    0x3fe9a00000000000ull, 0x0000000000000000ull, 0x3fcc6ffbc6f01000ull, 0xbcf1ec72c5962bd2ull,
    0x3fe9600000000000ull, 0x0000000000000000ull, 0x3fcdb13db0d49000ull, 0xbd2aff2af715b035ull,
    0x3fe9400000000000ull, 0x0000000000000000ull, 0x3fce530effe71000ull, 0x3cc212276041f430ull,
    0x3fe9200000000000ull, 0x0000000000000000ull, 0x3fcef5ade4dd0000ull, 0xbcca211565bb8e11ull,
    0x3fe9000000000000ull, 0x0000000000000000ull, 0x3fcf991c6cb3b000ull, 0x3d1bcbecca0cdf30ull,
    0x3fe8c00000000000ull, 0x0000000000000000ull, 0x3fd07138604d5800ull, 0x3cf89cdb16ed4e91ull,
    0x3fe8a00000000000ull, 0x0000000000000000ull, 0x3fd0c42d67616000ull, 0x3d27188b163ceae9ull,
    0x3fe8800000000000ull, 0x0000000000000000ull, 0x3fd1178e8227e800ull, 0xbd2c210e63a5f01cull,
    0x3fe8600000000000ull, 0x0000000000000000ull, 0x3fd16b5ccbacf800ull, 0x3d2b9acdf7a51681ull,
    0x3fe8400000000000ull, 0x0000000000000000ull, 0x3fd1bf99635a6800ull, 0x3d2ca6ed5147bdb7ull,
    0x3fe8200000000000ull, 0x0000000000000000ull, 0x3fd214456d0eb800ull, 0x3d0a87deba46baeaull,
    0x3fe7e00000000000ull, 0x0000000000000000ull, 0x3fd2bef07cdc9000ull, 0x3d2a9cfa4a5004f4ull,
    0x3fe7c00000000000ull, 0x0000000000000000ull, 0x3fd314f1e1d36000ull, 0xbd28e27ad3213cb8ull,
    0x3fe7a00000000000ull, 0x0000000000000000ull, 0x3fd36b6776be1000ull, 0x3d116ecdb0f177c8ull,
    0x3fe7800000000000ull, 0x0000000000000000ull, 0x3fd3c25277333000ull, 0x3d183b54b606bd5cull,
    0x3fe7600000000000ull, 0x0000000000000000ull, 0x3fd419b423d5e800ull, 0x3d08e436ec90e09dull,
    0x3fe7400000000000ull, 0x0000000000000000ull, 0x3fd4718dc271c800ull, 0xbd2f27ce0967d675ull,
    0x3fe7200000000000ull, 0x0000000000000000ull, 0x3fd4c9e09e173000ull, 0xbd2e20891b0ad8a4ull,
    0x3fe7000000000000ull, 0x0000000000000000ull, 0x3fd522ae0738a000ull, 0x3d2ebe708164c759ull,
    0x3fe6e00000000000ull, 0x0000000000000000ull, 0x3fd57bf753c8d000ull, 0x3d1fadedee5d40efull,
    0x3fe6c00000000000ull, 0x0000000000000000ull, 0x3fd5d5bddf596000ull, 0xbd0a0b2a08a465dcull,
};
constexpr std::uint64_t kPowExp[512] = {  // rva 0x103eb0
    0x0000000000000000ull, 0x3ff0000000000000ull, 0xbc84e82fc61851acull, 0x3feffb1afa5abcbfull,
    0x3c9b3b4f1a88bf6eull, 0x3feff63da9fb3335ull, 0xbc82985dd8521d32ull, 0x3feff168143b0281ull,
    0xbc7160139cd8dc5dull, 0x3fefec9a3e778061ull, 0x3c651e617061bfbdull, 0x3fefe7d42e11bbccull,
    0xbc905e7a108766d1ull, 0x3fefe315e86e7f85ull, 0x3c845fad437fa426ull, 0x3fefde5f72f654b1ull,
    0x3c8cd2523567f613ull, 0x3fefd9b0d3158574ull, 0xbc954529642b232full, 0x3fefd50a0e3c1f89ull,
    0xbc8bce8023f98efaull, 0x3fefd06b29ddf6deull, 0x3c8293708ef5c32eull, 0x3fefcbd42b72a836ull,
    0x3c60f74e61e6c861ull, 0x3fefc74518759bc8ull, 0xbc95b9280905b2a4ull, 0x3fefc2bdf66607e0ull,
    0x3c90a3e45b33d399ull, 0x3fefbe3ecac6f383ull, 0x3c84f31f32c4b7e7ull, 0x3fefb9c79b1f3919ull,
    0x3c979aa65d837b6dull, 0x3fefb5586cf9890full, 0x3c9407fb30d06420ull, 0x3fefb0f145e46c85ull,
    0x3c8eb51a92fdeffcull, 0x3fefac922b7247f7ull, 0xbc9a5d04b3b9911bull, 0x3fefa83b23395decull,
    0x3c3ebe3d702f9cd1ull, 0x3fefa3ec32d3d1a2ull, 0xbc937a01f0739546ull, 0x3fef9fa55fdfa9c5ull,
    0xbc6a033489906e0bull, 0x3fef9b66affed31bull, 0x3c8b8268b04ef0a5ull, 0x3fef973028d7233eull,
    0xbc9556522a2fbd0eull, 0x3fef9301d0125b51ull, 0xbc9ac46e44a2ebccull, 0x3fef8edbab5e2ab6ull,
    0xbc5080ef8c4eea55ull, 0x3fef8abdc06c31ccull, 0xbc65704e90c9f860ull, 0x3fef86a814f204abull,
    0xbc91c923b9d5f416ull, 0x3fef829aaea92de0ull, 0xbc897cea57e46280ull, 0x3fef7e95934f312eull,
    0x3c80d3e3e95c55afull, 0x3fef7a98c8a58e51ull, 0x3c56f01429e2b9d2ull, 0x3fef76a45471c3c2ull,
    0xbc801b15eaa59348ull, 0x3fef72b83c7d517bull, 0x3c6e653b2459034bull, 0x3fef6ed48695bbc0ull,
    0xbc8f1ff055de323dull, 0x3fef6af9388c8deaull, 0x3c92cc7ea345b7dcull, 0x3fef672658375d2full,
    0x3c8b898c3f1353bfull, 0x3fef635beb6fcb75ull, 0x3c957bfb2876ea9eull, 0x3fef5f99f8138a1cull,
    0xbc96d99c7611eb26ull, 0x3fef5be084045cd4ull, 0x3c8cdc1873af2155ull, 0x3fef582f95281c6bull,
    0x3c9aecf73e3a2f60ull, 0x3fef54873168b9aaull, 0xbc9493684653a131ull, 0x3fef50e75eb44027ull,
    0xbc8fe782cb86389dull, 0x3fef4d5022fcd91dull, 0xbc98e2899077520aull, 0x3fef49c18438ce4dull,
    0x3c8a6f4144a6c38dull, 0x3fef463b88628cd6ull, 0x3c9120fcd4f59273ull, 0x3fef42be3578a819ull,
    0x3c807a05b0e4047dull, 0x3fef3f49917ddc96ull, 0x3c89b788c188c9b8ull, 0x3fef3bdda27912d1ull,
    0x3c968efde3a8a894ull, 0x3fef387a6e756238ull, 0x3c877afbca90ef84ull, 0x3fef351ffb82140aull,
    0x3c875e18f274487dull, 0x3fef31ce4fb2a63full, 0x3c91512f082876eeull, 0x3fef2e85711ece75ull,
    0x3c80472b981fe7f2ull, 0x3fef2b4565e27cddull, 0x3c9a02f0c7d75ec6ull, 0x3fef280e341ddf29ull,
    0xbc96b87b3f71085eull, 0x3fef24dfe1f56381ull, 0xbc803297e78260bfull, 0x3fef21ba7591bb70ull,
    0x3c82f7e16d09ab31ull, 0x3fef1e9df51fdee1ull, 0xbc95b77e5ccd9fbfull, 0x3fef1b8a66d10f13ull,
    0xbc3d219b1a6fbffaull, 0x3fef187fd0dad990ull, 0xbc91e75c40b4251eull, 0x3fef157e39771b2full,
    0x3c8b3782720c0ab4ull, 0x3fef1285a6e4030bull, 0x3c98a911f1f7785aull, 0x3fef0f961f641589ull,
    0x3c6e149289cecb8full, 0x3fef0cafa93e2f56ull, 0xbc61e7c998db7dbbull, 0x3fef09d24abd886bull,
    0x3c834d754db0abb6ull, 0x3fef06fe0a31b715ull, 0x3c85425c11faadf4ull, 0x3fef0432edeeb2fdull,
    0x3c864201e2ac744cull, 0x3fef0170fc4cd831ull, 0xbc979517a03e2847ull, 0x3feefeb83ba8ea32ull,
    0x3c8fdd395dd3f84aull, 0x3feefc08b26416ffull, 0xbc800e2a46da4beeull, 0x3feef96266e3fa2dull,
    0xbc86a3803b8e5b04ull, 0x3feef6c55f929ff1ull, 0xbc87430803972b34ull, 0x3feef431a2de883bull,
    0xbc924aedcc4b5068ull, 0x3feef1a7373aa9cbull, 0xbc954de30ae02d94ull, 0x3feeef26231e754aull,
    0xbc9907f81b512d8eull, 0x3feeecae6d05d866ull, 0xbc94f2487e1c03ecull, 0x3feeea401b7140efull,
    0xbc71d1e83e9436d2ull, 0x3feee7db34e59ff7ull, 0x3c914a5432fcb2f4ull, 0x3feee57fbfec6cf4ull,
    0xbc991919b3ce1b15ull, 0x3feee32dc313a8e5ull, 0x3c79c3bba5562a2full, 0x3feee0e544ede173ull,
    0x3c859f48a72a4c6dull, 0x3feedea64c123422ull, 0xbc85a71612e21658ull, 0x3feedc70df1c5175ull,
    0xbc9312607a28698aull, 0x3feeda4504ac801cull, 0x3c86421f6f1d24d6ull, 0x3feed822c367a024ull,
    0xbc58a78f4817895bull, 0x3feed60a21f72e2aull, 0xbc9348a6815fce65ull, 0x3feed3fb2709468aull,
    0xbc7c2c9b67499a1bull, 0x3feed1f5d950a897ull, 0x3c835c43984d9871ull, 0x3feecffa3f84b9d4ull,
    0x3c4363ed60c2ac11ull, 0x3feece086061892dull, 0xbc632afc8d9473a0ull, 0x3feecc2042a7d232ull,
    0x3c9666093b0664efull, 0x3feeca41ed1d0057ull, 0xbc95fc5e44de020eull, 0x3feec86d668b3237ull,
    0x3c6ecce1daa10379ull, 0x3feec6a2b5c13cd0ull, 0xbc7ea0148327c42full, 0x3feec4e1e192aed2ull,
    0x3c93ff8e3f0f1230ull, 0x3feec32af0d7d3deull, 0xbc7a843ad1a88022ull, 0x3feec17dea6db7d7ull,
    0x3c7690cebb7aafb0ull, 0x3feebfdad5362a27ull, 0x3c892ca3bf144e63ull, 0x3feebe41b817c114ull,
    0x3c931dbdeb54e077ull, 0x3feebcb299fddd0dull, 0xbc902c99b04aa8b0ull, 0x3feebb2d81d8abffull,
    0xbc8f94340071a38eull, 0x3feeb9b2769d2ca7ull, 0x3c73e34f67e67118ull, 0x3feeb8417f4531eeull,
    0xbc87deccdc93a349ull, 0x3feeb6daa2cf6642ull, 0xbc75a3b1197ba0f0ull, 0x3feeb57de83f4eefull,
    0xbc78dec6bd0f385full, 0x3feeb42b569d4f82ull, 0x3c81bd2888075068ull, 0x3feeb2e2f4f6ad27ull,
    0xbc861246ec7b5cf6ull, 0x3feeb1a4ca5d920full, 0xbc896be8ae89ef8full, 0x3feeb070dde910d2ull,
    0x3c93350518fdd78eull, 0x3feeaf4736b527daull, 0xbc88e6ac90348602ull, 0x3feeae27dbe2c4cfull,
    0x3c7b98b72f8a9b05ull, 0x3feead12d497c7fdull, 0xbc91af7f1365c3acull, 0x3feeac0827ff07ccull,
    0x3c9063e1e21c5409ull, 0x3feeab07dd485429ull, 0xbc943a3540d1898aull, 0x3feeaa11fba87a03ull,
    0x3c34c7855019c6eaull, 0x3feea9268a5946b7ull, 0xbc951f58ddaa8090ull, 0x3feea84590998b93ull,
    0x3c9432e62b64c035ull, 0x3feea76f15ad2148ull, 0xbc82e1648e50a17cull, 0x3feea6a320dceb71ull,
    0xbc8ce44a6199769full, 0x3feea5e1b976dc09ull, 0x3c95f30eda98a575ull, 0x3feea52ae6cdf6f4ull,
    0xbc8c33c53bef4da8ull, 0x3feea47eb03a5585ull, 0x3c917ecda8a72159ull, 0x3feea3dd1d1929fdull,
    0xbc845378892be9aeull, 0x3feea34634ccc320ull, 0xbc9345f3cee1ae6eull, 0x3feea2b9febc8fb7ull,
    0xbc93cedd78565858ull, 0x3feea23882552225ull, 0xbc85c33fdf910406ull, 0x3feea1c1c70833f6ull,
    0x3c5710aa807e1964ull, 0x3feea155d44ca973ull, 0x3c81079ab5789604ull, 0x3feea0f4b19e9538ull,
    0xbc93b3efbf5e2228ull, 0x3feea09e667f3bcdull, 0x3c727df161cd7778ull, 0x3feea052fa75173eull,
    0xbc6a12ad8734b982ull, 0x3feea012750bdabfull, 0x3c93f9924a05b767ull, 0x3fee9fdcddd47645ull,
    0xbc6367efb86da9eeull, 0x3fee9fb23c651a2full, 0xbc87557939a8b5efull, 0x3fee9f9298593ae5ull,
    0xbc80dc3d54e08851ull, 0x3fee9f7df9519484ull, 0x3c51ed2f56fa9d1aull, 0x3fee9f7466f42e87ull,
    0xbc781f647e5a3ecfull, 0x3fee9f75e8ec5f74ull, 0xbc88e67a9006c909ull, 0x3fee9f8286ead08aull,
    0xbc86ee4ac08b7db0ull, 0x3fee9f9a48a58174ull, 0x3c86597566977ac8ull, 0x3fee9fbd35d7cbfdull,
    0xbc8619321e55e68aull, 0x3fee9feb564267c9ull, 0x3c92c0b7028a5c3aull, 0x3feea024b1ab6e09ull,
    0x3c909ccb5e09d4d3ull, 0x3feea0694fde5d3full, 0x3c8a30faf49cc78cull, 0x3feea0b938ac1cf6ull,
    0xbc7b32dcb94da51dull, 0x3feea11473eb0187ull, 0xbc92dad3519d7b5bull, 0x3feea17b0976cfdbull,
    0x3c94ecfd5467c06bull, 0x3feea1ed0130c132ull, 0x3c87d51410fd15c2ull, 0x3feea26a62ff86f0ull,
    0x3c65ebe1abd66c55ull, 0x3feea2f336cf4e62ull, 0xbc760a3629969871ull, 0x3feea3878491c491ull,
    0xbc88a1c52fb3cf42ull, 0x3feea427543e1a12ull, 0x3c8b18c6e3fdef5dull, 0x3feea4d2add106d9ull,
    0xbc9369b6f13b3734ull, 0x3feea589994cce13ull, 0x3c90ec1ddcb1390aull, 0x3feea64c1eb941f7ull,
    0xbc805e843a19ff1eull, 0x3feea71a4623c7adull, 0xbc522cea4f3afa1eull, 0x3feea7f4179f5b21ull,
    0xbc94d450d872576eull, 0x3feea8d99b4492edull, 0x3c7c88549b958471ull, 0x3feea9cad931a436ull,
    0x3c90ad675b0e8a00ull, 0x3feeaac7d98a6699ull, 0x3c931143962f7877ull, 0x3feeabd0a478580full,
    0x3c8db72fc1f0eab4ull, 0x3feeace5422aa0dbull, 0x3c93e9e96f112479ull, 0x3feeae05bad61778ull,
    0xbc65b6609cc5e7ffull, 0x3feeaf3216b5448cull, 0xbc8dac42a4a38df0ull, 0x3feeb06a5e0866d9ull,
    0x3c7bf68359f35f44ull, 0x3feeb1ae99157736ull, 0x3c8b99dd98b1ed84ull, 0x3feeb2fed0282c8aull,
    0xbc93091fa71e3d83ull, 0x3feeb45b0b91ffc6ull, 0xbc7885ad50cbb750ull, 0x3feeb5c353aa2fe2ull,
    0xbc5da9b88b6c1e29ull, 0x3feeb737b0cdc5e5ull, 0xbc82d5e85f3e0301ull, 0x3feeb8b82b5f98e5ull,
    0xbc6c23f97c90b959ull, 0x3feeba44cbc8520full, 0xbc51669428996971ull, 0x3feebbdd9a7670b3ull,
    0xbc92434322f4f9aaull, 0x3feebd829fde4e50ull, 0x3c71f2b2c1c4c014ull, 0x3feebf33e47a22a2ull,
    0xbc85ca6cd7668e4bull, 0x3feec0f170ca07baull, 0xbc9294f304f166b6ull, 0x3feec2bb4d53fe0dull,
    0x3c71affc2b91ce27ull, 0x3feec49182a3f090ull, 0xbc8a1e58414c07d3ull, 0x3feec674194bb8d5ull,
    0x3c6dd235e10a73bbull, 0x3feec86319e32323ull, 0xbc79740b58a20091ull, 0x3feeca5e8d07f29eull,
    0xbc87c50422622263ull, 0x3feecc667b5de565ull, 0x3c9165830a2b96c2ull, 0x3feece7aed8eb8bbull,
    0x3c8b1c86e3e231d5ull, 0x3feed09bec4a2d33ull, 0xbc903d5cbe27874bull, 0x3feed2c980460ad8ull,
    0xbc91bbd1d3bcbb15ull, 0x3feed503b23e255dull, 0x3c5986178980fce0ull, 0x3feed74a8af46052ull,
    0x3c90cc319cee31d2ull, 0x3feed99e1330b358ull, 0xbc89472975b1f2a5ull, 0x3feedbfe53c12e59ull,
    0x3c8469846e735ab3ull, 0x3feede6b5579fdbfull, 0x3c7d8157a34b7e7full, 0x3feee0e521356ebaull,
    0xbc82dfcd978e9db4ull, 0x3feee36bbfd3f37aull, 0x3c8c8a4e231ebb7dull, 0x3feee5ff3a3c2774ull,
    0x3c8c1a7792cb3387ull, 0x3feee89f995ad3adull, 0xbc888c8d11a142e5ull, 0x3feeeb4ce622f2ffull,
    0xbc907b8f4ad1d9faull, 0x3feeee07298db666ull, 0x3c889c2ea41433c7ull, 0x3feef0ce6c9a8952ull,
    0xbc55c3d956dcaebaull, 0x3feef3a2b84f15fbull, 0xbc7274aedac8ff80ull, 0x3feef68415b749b1ull,
    0xbc90a40e3da6f640ull, 0x3feef9728de5593aull, 0x3c85c620ce76df06ull, 0x3feefc6e29f1c52aull,
    0xbc68d6f438ad9334ull, 0x3feeff76f2fb5e47ull, 0xbc8fda52e1b51e41ull, 0x3fef028cf22749e4ull,
    0xbc91eee26b588a35ull, 0x3fef05b030a1064aull, 0xbc32141a7b3e2cd8ull, 0x3fef08e0b79a6f1full,
    0x3c74ffd70a5fddcdull, 0x3fef0c1e904bc1d2ull, 0xbc302899507554e5ull, 0x3fef0f69c3f3a207ull,
    0xbc91bdfbfa9298acull, 0x3fef12c25bd71e09ull, 0xbc80dda2d4c0010cull, 0x3fef16286141b33dull,
    0x3c736eae30af0cb3ull, 0x3fef199bdd85529cull, 0xbc8a007daadf8d68ull, 0x3fef1d1cd9fa652cull,
    0x3c8ee3325c9ffd94ull, 0x3fef20ab5fffd07aull, 0x3c836909391181d3ull, 0x3fef244778fafb22ull,
    0x3c84e08fd10959acull, 0x3fef27f12e57d14bull, 0xbc811cd7dbdf9547ull, 0x3fef2ba88988c933ull,
    0x3c63cdaf384e1a67ull, 0x3fef2f6d9406e7b5ull, 0xbc7ac28b7bef6621ull, 0x3fef33405751c4dbull,
    0x3c676b2c6c921968ull, 0x3fef3720dcef9069ull, 0xbc7030587207b9e1ull, 0x3fef3b0f2e6d1675ull,
    0xbc808a1883ccb5d2ull, 0x3fef3f0b555dc3faull, 0xbc8cc734592af7fcull, 0x3fef43155b5bab74ull,
    0xbc8fad5d3ffffa6full, 0x3fef472d4a07897cull, 0x3c87752a44f587e8ull, 0x3fef4b532b08c968ull,
    0xbc900dae3875a949ull, 0x3fef4f87080d89f2ull, 0x3c85b66fefeef52eull, 0x3fef53c8eacaa1d6ull,
    0x3c74a385a63d07a7ull, 0x3fef5818dcfba487ull, 0x3c5159d9d908a96eull, 0x3fef5c76e862e6d3ull,
    0xbc82919e2040220full, 0x3fef60e316c98398ull, 0x3c8c254d16117a68ull, 0x3fef655d71ff6075ull,
    0x3c8e5a50d5c192acull, 0x3fef69e603db3285ull, 0xbc8d8c329fbd0e03ull, 0x3fef6e7cd63a8315ull,
    0x3c843a59ac016b4bull, 0x3fef7321f301b460ull, 0xbc8ea6e6fbd5f2a6ull, 0x3fef77d5641c0658ull,
    0xbc82d52107b43e1full, 0x3fef7c97337b9b5full, 0xbc63e8e3eab2cbb4ull, 0x3fef81676b197d17ull,
    0xbc892ab93b470dc9ull, 0x3fef864614f5a129ull, 0xbc8b7966cd0d2cd9ull, 0x3fef8b333b16ee12ull,
    0x3c74b604603a88d3ull, 0x3fef902ee78b3ff6ull, 0xbc776caa4c2ff1cfull, 0x3fef953924676d76ull,
    0x3c83c5ec519d7271ull, 0x3fef9a51fbc74c83ull, 0xbc81d5fc525d9940ull, 0x3fef9f7977cdb740ull,
    0xbc8ff7128fd391f0ull, 0x3fefa4afa2a490daull, 0x3c855cd8aaea3d21ull, 0x3fefa9f4867cca6eull,
    0xbc8dae98e223747dull, 0x3fefaf482d8e67f1ull, 0x3c8269947c2bed4aull, 0x3fefb4aaa2188510ull,
    0x3c8ec3bc41aa2008ull, 0x3fefba1bee615a27ull, 0xbc83b6137e9afe9eull, 0x3fefbf9c1cb6412aull,
    0x3c842b94c3a9eb32ull, 0x3fefc52b376bba97ull, 0xbc69fa74878ba7c7ull, 0x3fefcac948dd7274ull,
    0x3c8a64a931d185eeull, 0x3fefd0765b6e4540ull, 0x3c901f3a75ee0efeull, 0x3fefd632798844f8ull,
    0xbc8e37bae43be3edull, 0x3fefdbfdad9cbe14ull, 0xbc516a9ce6ed84faull, 0x3fefe1d802243c89ull,
    0x3c77893b4d91cd9dull, 0x3fefe7c1819e90d8ull, 0xbc699c7db2effc76ull, 0x3fefedba3692d514ull,
    0x3c5305c14160cc89ull, 0x3feff3c22b8f71f1ull, 0x3c64b458677f9840ull, 0x3feff9d96b2a23d9ull,
};

constexpr std::uint64_t kSign = 0x8000000000000000ull;
constexpr std::uint32_t kSignBias = 0x80000;  // 0x800 << 8

double math_oflow(std::uint32_t sign) {  // 0xe4990
    const double big = dbl(0x7000000000000000ull);
    return (sign ? -big : big) * big;
}
double math_uflow(std::uint32_t sign) { return sign ? -0.0 : 0.0; }  // 0x916dc
double math_divzero(std::uint32_t sign) {  // 0xa30b0
    return dbl(0x7ff0000000000000ull) * (sign ? -1.0 : 1.0);  // (1.0 / 0.0) * (+-1)
}
double math_invalid(double x) {  // 0xe4950: (x - x) / (x - x)
    const double d = x - x;
    return d / d;
}

// checkint: 0 = not an integer, 1 = odd integer, 2 = even integer.
int checkint(std::uint64_t iy) {
    const int e = static_cast<int>((iy >> 52) & 0x7ff);
    if (e < 0x3ff) return 0;
    if (e > 0x3ff + 52) return 2;
    if (iy & ((1ull << (0x3ff + 52 - e)) - 1)) return 0;
    if (iy & (1ull << (0x3ff + 52 - e))) return 1;
    return 2;
}

bool is_signaling(std::uint64_t ix) { return 2 * (ix ^ 0x0008000000000000ull) > 0xfff0000000000000ull; }

// Exp special case (|ehi| outside [2^-54, 512)): 0x5ef86..0x5f056.
double exp_specialcase(double tmp, std::uint64_t sbits, std::uint64_t ki) {
    if ((ki & 0x80000000ull) == 0) {
        const double scale = dbl(sbits + 0xc0f0000000000000ull);  // sbits - (1009 << 52)
        const double y = scale * tmp + scale;
        return y * dbl(0x7f00000000000000ull);  // 2^1009
    }
    const std::uint64_t sb = sbits + 0x3fe0000000000000ull;  // sbits + (1022 << 52)
    const double scale = dbl(sb);
    const double st = scale * tmp;
    double y = scale + st;
    if (1.0 > std::fabs(y)) {
        const double one = (y < 0.0) ? -1.0 : 1.0;
        double lo = (scale - y) + st;
        const double hi = one + y;
        lo = lo + ((one - hi) + y);
        y = (lo + hi) - one;
        if (y == 0.0) y = dbl(sb & kSign);
    }
    return y * dbl(0x0010000000000000ull);  // 2^-1022
}

}  // namespace

double msvc_pow(double x, double y) {
    std::uint64_t ix = bits(x);
    const std::uint64_t iy = bits(y);
    std::uint32_t topx = static_cast<std::uint32_t>(ix >> 52);
    const std::uint32_t topy = static_cast<std::uint32_t>(iy >> 52);
    std::uint32_t sign_bias = 0;

    if (topx - 1 >= 0x7fe || (topy & 0x7ff) - 0x3be >= 0x80) {
        if (2 * iy - 1 >= 0xffdfffffffffffffull) {  // y is 0, inf or nan
            if (2 * iy == 0) return is_signaling(ix) ? x + y : 1.0;
            if (ix == 0x3ff0000000000000ull) return is_signaling(iy) ? x + y : 1.0;
            if (2 * ix > 0xffe0000000000000ull || 2 * iy > 0xffe0000000000000ull) return x + y;
            if (2 * ix == 0x7fe0000000000000ull) return 1.0;
            if ((2 * ix < 0x7fe0000000000000ull) == ((iy >> 63) == 0)) return 0.0;
            return y * y;
        }
        if (2 * ix - 1 >= 0xffdfffffffffffffull) {  // x is 0, inf or nan
            double x2 = x * x;
            if ((ix & kSign) && checkint(iy) == 1) {
                x2 = -x2;
                sign_bias = 1;
            }
            if (2 * ix == 0) return (iy & kSign) ? math_divzero(sign_bias) : x2;
            return (iy & kSign) ? 1.0 / x2 : x2;
        }
        if (ix & kSign) {  // finite x < 0
            const int yint = checkint(iy);
            if (yint == 0) return math_invalid(x);
            ix &= 0x7fffffffffffffffull;
            topx &= 0x7ff;
            sign_bias = (yint == 1) ? kSignBias : 0;
        }
        if ((topy & 0x7ff) - 0x3be >= 0x80) {
            if (ix == 0x3ff0000000000000ull) return 1.0;
            if ((topy & 0x7ff) < 0x3be) return (ix > 0x3ff0000000000000ull) ? y + 1.0 : 1.0 - y;
            return ((ix > 0x3ff0000000000000ull) == (topy < 0x800)) ? math_oflow(0) : math_uflow(0);
        }
        if (topx == 0) {  // subnormal x: normalise
            ix = bits(x * 4503599627370496.0);  // 2^52
            ix &= 0x7fffffffffffffffull;
            ix -= 0x0340000000000000ull;  // 52 << 52
        }
    }
    if (iy == 0x3ff0000000000000ull) return x;
    if (ix == 0x3ff0000000000000ull) return sign_bias == 0 ? 1.0 : -1.0;

    // log(|x|) as hi + lo (0x5ed10..0x5ee33).
    const std::uint64_t tmp = ix + 0xc0196aab00000000ull;  // ix - 0x3fe6955500000000
    const int i = static_cast<int>((tmp >> 45) & 0x7f);
    const std::uint64_t iz = ix - (tmp & 0xfff0000000000000ull);
    const int k = static_cast<int>(static_cast<std::int64_t>(tmp) >> 52);
    const double z = dbl(iz);
    const double kd = static_cast<double>(k);
    const double invc = dbl(kPowLog[i * 4 + 0]);
    const double logc = dbl(kPowLog[i * 4 + 2]);
    const double logctail = dbl(kPowLog[i * 4 + 3]);

    const double r = std::fma(z, invc, -1.0);
    const double t1 = kd * dbl(0x3fe62e42fefa3800ull) + logc;
    const double ar = r * -0.5;
    const double ar2 = ar * r;
    const double lo3 = std::fma(ar, r, -ar2);
    const double lo1 = kd * dbl(0x3d2ef35793c76730ull) + logctail;
    const double t2 = t1 + r;
    const double lo2 = (t1 - t2) + r;
    const double lo123 = lo3 + (lo1 + lo2);
    const double hi = ar2 + t2;
    const double lo4 = (t2 - hi) + ar2;
    const double lo1234 = lo123 + lo4;
    const double pa = dbl(0x3fe999999959554eull) - r * dbl(0x3fe555555529a47aull);
    const double pb = (r * dbl(0x3ff0002b8b263fc3ull) - dbl(0x3ff2495b9b4845e9ull)) * ar2;
    const double pc = r * dbl(0x3fe0000000000006ull) - dbl(0x3fe5555555555560ull);
    const double ar3 = ar2 * r;
    const double p = ((pa + pb) * ar2 + pc) * ar3;
    const double lo = lo1234 + p;
    const double ly = lo + hi;
    const double ltail = (hi - ly) + lo;

    // y * log(x) as ehi + elo.
    const double ehi = ly * y;
    const double elo = std::fma(ly, y, -ehi) + ltail * y;

    // exp(ehi + elo) (0x5ee4a..0x5f061).
    std::uint32_t abstop = static_cast<std::uint32_t>(bits(ehi) >> 52) & 0x7ff;
    if (abstop - 0x3c9 >= 0x3f) {
        if (abstop - 0x3c9 >= 0x80000000u) {
            const double one = ehi + 1.0;
            return sign_bias ? -one : one;
        }
        if (abstop >= 0x409) return (bits(ehi) & kSign) ? math_uflow(sign_bias) : math_oflow(sign_bias);
        abstop = 0;
    }
    const double zz = ehi * dbl(0x40771547652b82feull) + dbl(0x4238000000008000ull);
    const std::uint64_t ki = bits(zz) >> 16;
    const double kd2 = static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(ki)));
    double er = (ehi - kd2 * dbl(0x3f662e42fefc0000ull)) + kd2 * dbl(0x3d2c610ca86c3899ull);
    er = er + elo;
    const std::uint64_t idx = 2 * (ki & 0xff);
    const std::uint64_t top = (static_cast<std::uint64_t>(sign_bias) + ki) << 44;
    const double tail = dbl(kPowExp[idx]);
    const std::uint64_t sbits = kPowExp[idx + 1] + top;
    const double er2 = er * er;
    const double q1 = (er * dbl(0x3fc555555555543cull) + dbl(0x3fdffffffffffdbdull)) * er2;
    const double q2 = (er * dbl(0x3f81111167a4b553ull) + dbl(0x3fa55555cf16e1edull)) * (er2 * er2);
    const double t = (q1 + (er + tail)) + q2;
    if (abstop == 0) return exp_specialcase(t, sbits, ki);
    const double scale = dbl(sbits);
    return scale * t + scale;
}

}  // namespace cleria
