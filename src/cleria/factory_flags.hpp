// The per-cast flag words the actor factory FUN_00459410 leaves on a fresh actor: its switch
// on the cast id (byte table 0x459f2c -> jump table 0x459e6c) over the class base.
// build/ai_spec/FACTORY.md §5 (every value read from the disassembly).
//
// +0x12c bits read elsewhere (FACTORY.md §7.1): 0x40 the mover sums knock channel 2 only (with
// 0x2000000: no knock displacement at all); 0x4000 no ceiling probe; 0x8000 no water probe;
// 0x80000 stun-immune; 0x100000 the move probe FUN_00477450 is OFF; 0x200000 the sight check
// always passes; 0x400000 the jump helper FUN_00476200 runs; 0x1000000 never culled;
// 0x20000000 no ice slide; 0x10 the idle wander (the XSO spawn op clears it).
// +0x130: 0x20 the double-jump variant; 0x800 the stuck-escape fan; 0x700 the XSO group tag
// (written by the spawn op, not here); 0x4000 excluded from a Toal-only push.
#pragma once
#include <cstdint>

namespace cleria {

struct FactoryFlags {
    std::uint32_t f12c = 0x00110010;   // the class-16 base: 0x10 | 0x10000 | 0x100000
    std::uint32_t f130 = 0;
};

inline FactoryFlags factory_flags(int cast) {
    switch (cast) {
        case 0x0e: return {0x00910010, 0};            // M100 (+0x800000, the leash)
        case 0x0f: case 0x40: return {0x00490050, 0x4800};   // M110, M600
        case 0x10: return {0x00410010, 0x820};        // M120
        case 0x11: return {0x00410010, 0x800};        // M130
        case 0x12: case 0x14: case 0x19: case 0x1a: case 0x21: case 0x22: case 0x23: case 0x24:
        case 0x37: case 0x44: case 0x4a: case 0x51:
            return {0x00410010, 0};                   // M140 M160 M210 M220 M290 M300 M310 M320 M510 M640 M700 M770
        case 0x13: case 0x34: return {0x02394052, 0}; // M150, M480
        case 0x16: return {0x00010050, 0x4000};       // M180
        case 0x17: case 0x1b: case 0x27: case 0x2b: case 0x35: case 0x3b: case 0x41: case 0x46:
            return {0x00110010, 0x4000};              // M190 M230 M350 M390 M490 M550 M610 M660
        case 0x18: return {0x00010010, 0x4000};       // M200
        case 0x1c: return {0x00490050, 0x4000};       // M240
        case 0x1d: return {0x00190010, 0x4000};       // M250
        case 0x1e: case 0x2d: case 0x30: return {0x07194051, 0x6081};   // M260, M410, M440
        case 0x1f: return {0x00114010, 0};            // M270
        case 0x25: return {0x03190050, 0x4000};       // M330
        case 0x26: case 0x2a: return {0x00110010, 0x40000};   // M340, M380
        case 0x28: case 0x32: return {0x00110010, 0x1};       // M360, M460
        case 0x29: case 0x3a: return {0x00410010, 0x40000};   // M370, M540
        case 0x2c: return {0x02214050, 0};            // M400
        case 0x2e: case 0x31: return {0x00110012, 0x4000};    // M420, M450
        case 0x2f: case 0x3c: case 0x3d: case 0x47: case 0x52:
            return {0x00110010, 0x2009};              // M430 M560 M570 M670 M780
        case 0x33: return {0x80110010, 0x4000};       // M470
        case 0x36: return {0x02094053, 0};            // M500
        case 0x39: case 0x42: return {0x00010010, 0x800};     // M530, M620
        case 0x3e: return {0x02394053, 0};            // M580
        case 0x3f: return {0x00098050, 0x880};        // M590
        case 0x43: return {0x00410010, 0x840};        // M630
        case 0x49: return {0x00490050, 0xc000};       // M690
        case 0x4b: case 0x4e: case 0x4f: return {0x02190052, 0};   // M710, M740, M750
        case 0x50: return {0x00490010, 0x4000};       // M760
        case 0xa6: case 0xa8: case 0xab: return {0x01180050, 0x9};     // M800, M820, M850 (class 4)
        case 0xa7: case 0xaa: case 0xac: return {0x01180050, 0xc009};  // M810, M840, M860 (class 4)
        case 0xa9: case 0xad: return {0x00100050, 0xc000};    // M830, M870 (class 4)
        case 0xae: return {0x00100050, 0x4000};       // M880 (class 4)
        case 0xaf: return {0x05194012, 0x1};          // M890
        case 0xb0: return {0x21180010, 0x8};          // M900 (class 2)
        case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6:
            return {0x2041c012, 0};                   // M910..M960 (Nooze)
        case 0xb7: return {0x00114010, 0};            // M970 (+0x2 while [0x76bb8c] != 0: render only)
        case 0xb8: return {0x04114012, 0x1};          // M980
        default: return {};                           // M000-M050, M170, M280, M520, M650, M680, M720, M730, M790, M990
    }
}

}  // namespace cleria
