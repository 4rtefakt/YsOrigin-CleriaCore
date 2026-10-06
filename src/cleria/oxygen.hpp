// Oxygen under water — the mover's block 0x467d91..0x467f29 (build/physics_re/WATER.md §3,
// build/item_re/KEY_ITEMS.md §3.5). O2 is DAT_0076c1b8 (max 6000.0, 0x6bdf4c), the gauge alpha
// DAT_0076c1bc. While submerged it drains 10 a tick (1 with the Water Dragon's Scales equipped
// as the held tool, 0x76a62c == 0x61); at 0 the player loses maxHP / 60 a tick. Out of the
// water it refills 10 a tick while the alpha fades by 5, and snaps to full once the gauge has
// faded out.
#pragma once

namespace cleria {

struct Oxygen {
    static constexpr float kMax = 6000.0f;
    float o2 = kMax;        // 0x76c1b8
    int alpha = 0;          // 0x76c1bc: the HUD bar's alpha; flag 503 = (alpha != 0)

    // One logic tick. `submerged` = +0x38a; `surface` = +0x4ba; `buried` = +0x38c (sand);
    // `scales` = the held tool is item 0x61. Returns the HP to take this tick (0 unless
    // drowning; the caller applies it to DAT_0076a730 and handles death).
    float tick(bool submerged, int surface, bool buried, bool scales, float max_hp) {
        const bool drain = submerged && surface != 0x0d && surface != 0x11 && (surface != 0x12 || buried);
        if (drain) {
            alpha = 255;
            if (o2 == 0.0f) return max_hp / 60.0f;                   // 0x6bda94
            if (o2 > kMax) o2 = kMax;
            const float d = buried ? (scales ? 50.0f : 100.0f) : (scales ? 1.0f : 10.0f);
            o2 -= d;
            if (o2 < 0.0f) o2 = 0.0f;
            return 0.0f;
        }
        if (o2 < kMax) o2 = o2 + 10.0f < kMax ? o2 + 10.0f : kMax;
        if (alpha != 0) alpha -= 5;
        else o2 = kMax;
        return 0.0f;
    }
};

}  // namespace cleria
