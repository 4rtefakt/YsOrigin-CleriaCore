// The player's floor hazards and the S_3009 noise drain — FUN_00462550's block
// [BYTES 0x4627d1..0x46294e] (build/event_re/FLOOR3_ROUTE_AUDIT.md B5/B6, build/item_re/KEY_ITEMS.md §3.3).
//
// On a lava (surface 13) or blood (17) floor, or while flag 290 (the noise) is set, and with no
// i-frames (+0x734), a tick counter +0x4c8 runs; every `interval` ticks the player takes damage
// through FUN_004fa160 with the red popup 0xffa000a0.
//  - Floors: interval 10 (20 with the accessory bits 0x76a624 & 0x2000 lava / 0x4000000 blood),
//    +10 with blessing 0x76bc80 & 2. Damage 3 / 6 / 9 by difficulty (0x69bd50[0x76bb5c]), 1.0 on
//    Easy (flag 143), -1 with the blessing. Only while grounded (+0x388) or submerged (+0x38a).
//  - The noise: maxHP x 0.05 whether airborne or not. Toal: every 100 ticks, or 1.0 every 300
//    while boosting (0x76c198 & 0x20). Others: +0x810 counts every 50 ticks; the interval is 50
//    up to 20 of them, 20 up to 40, 10 up to 60, then every tick.
#pragma once
#include <cstdint>

namespace cleria {

struct SurfaceHazard {
    std::uint16_t t = 0;          // +0x4c8 (advances only while a hazard applies)
    std::uint32_t noise_n = 0;    // +0x810

    struct In {
        int surface = 0;          // +0x4b9
        bool noise = false;       // flag 290 (DAT_0076bda4)
        bool iframes = false;     // +0x734 != 0
        bool grounded = false;    // +0x388
        bool submerged = false;   // +0x38a
        int character = 1;        // DAT_0076bb74
        bool boosting = false;    // 0x76c198 & 0x20
        int difficulty = 0;       // DAT_0076bb5c: 0 Normal / 1 Hard / 2 Nightmare
        bool easy = false;        // flag 143 (DAT_0076bb58)
        bool blessing2 = false;   // 0x76bc80 & 2
        bool acc_lava = false;    // 0x76a624 & 0x2000
        bool acc_blood = false;   // 0x76a624 & 0x4000000
        float max_hp = 0;         // +0x94
    };

    // One player tick: the HP to take this tick (0 = none).
    float tick(const In& in) {
        const bool lava = in.surface == 0x0d, blood = in.surface == 0x11;
        if ((!lava && !blood && !in.noise) || in.iframes) return 0.0f;
        unsigned iv = 10;
        if (lava && in.acc_lava) iv = 20;
        else if (blood && in.acc_blood) iv = 20;
        if (in.blessing2) iv += 10;
        static const float kFloor[3] = {3.0f, 6.0f, 9.0f};   // 0x69bd50
        float dmg = kFloor[in.difficulty < 0 ? 0 : in.difficulty > 2 ? 2 : in.difficulty];
        if (in.easy) dmg = 1.0f;
        else if (in.blessing2) dmg -= 1.0f;
        float out = 0.0f;
        if (!in.noise) {
            noise_n = 0;
            if (!(in.submerged || in.grounded)) { ++t; return 0.0f; }
        } else {
            dmg = in.max_hp * 0.05f;                        // 0x6bd13c
            if (in.character == 3) {
                if (in.boosting) dmg = 1.0f;
                iv = in.boosting ? 300u : 100u;
            } else {
                if (t % 50 == 0) ++noise_n;
                iv = noise_n > 20 ? 20u : 50u;
                if (noise_n > 40) iv = 10;
                if (noise_n > 60) iv = 1;
            }
        }
        if (t % iv == 0) out = dmg;
        ++t;
        return out;
    }
};

// The standing HP regeneration — the mover FUN_00465170 [BYTES 0x466b7f..0x466c77] (BLESSINGS.md §3.1,
// DIFFICULTY.md G17). It runs with the regen accessory (0x76a624 & 0x400) or in a room whose script set
// flag 209 (0x76bc60: S_1100..S_5104, zeroed at every scene entry), never in a boss battle (flag 156).
// While no stick is held (byte +0x514) and the ASO plays section 2 (FUN_0040b230 == 2, the idle stance
// [H]) the counter +0x760 runs; past the delay, every `period` ticks and with HP >= 1, HP += max HP / 60
// (doubled with the accessory inside a flag-209 room). Delay / period 300 / 24, Nightmare 600 / 48; the
// blessing bit 0 (0x76bc80 & 1) halves both, in a flag-209 room only.
struct StandingRegen {
    std::uint32_t n = 0;          // +0x760

    struct In {
        bool boss_battle = false; // flag 156 (0x76bb8c)
        bool acc_regen = false;   // 0x76a624 & 0x400
        bool flag209 = false;     // 0x76bc60
        bool stick = false;       // byte +0x514
        int section = 2;          // FUN_0040b230(+0x8f0)
        int difficulty = 0;       // DAT_0076bb5c
        bool blessing1 = false;   // 0x76bc80 & 1
        float hp = 0, max_hp = 0; // 0x76a730, +0x94
    };

    // One player-mover tick: the HP to add (0 = none; the caller clamps at max HP as FUN_004fa160).
    float tick(const In& in) {
        if (in.boss_battle) return 0.0f;
        if (!in.acc_regen && !in.flag209) return 0.0f;
        if (in.stick || in.section != 2) { n = 0; return 0.0f; }
        ++n;
        std::uint32_t delay = in.difficulty == 2 ? 600u : 300u, period = in.difficulty == 2 ? 48u : 24u;
        if (in.flag209 && in.blessing1) { delay >>= 1; period >>= 1; }
        if (n <= delay || n % period != 0 || in.hp < 1.0f) return 0.0f;
        float amt = in.max_hp / 60.0f;                          // 0x6bda94
        if (in.acc_regen && in.flag209) amt += amt;
        return amt;
    }
};

}  // namespace cleria
