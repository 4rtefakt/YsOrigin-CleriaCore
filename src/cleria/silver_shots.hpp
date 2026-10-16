// The final battle's silver shots: while flag 215 (DAT_0076bc78, set when Darm's fight starts and cleared by the
// killing hit; DARM_D.md C11) is on, every swing the attack dispatcher FUN_0046ea00 picks arms a volley of light
// shots, and the player's magic tick FUN_0046af60 fires it through FUN_0046e460: three kind-1 bullets of effect
// 0xc3 in a fan about the facing. They are what lets the player hit Darm from outside his reach. Pure state; the
// host arms it from the dispatch (PlayerActions::Out::shot_arm), the down thrust's landing, and ticks it.
//
// The arm [BYTES 0x46ebd0..0x46ed19; EXPORT 0046ea00.c:298-347]: +0x7fc = 1 for the jump attack (0x109) and every
// swing of the air combo (+0x76c), else 0; Toal (DAT_0076bb74 == 3) with +0x7fc = 1 gets +0x804 = 3 shots
// +0x808 = 12 ticks apart, every other case one shot (+0x808 = 0). The tap step (0x112) and the down thrust's start
// (0x10b) arm nothing; the thrust's landing (FUN_0046fba0) arms +0x7fc = 1, one shot [BYTES 0x46ffa6..0x46ffd1].
// The arm leaves +0x800 / +0x80c as they are.
// The tick [BYTES 0x46c9c9..0x46cab7]: while +0x804 != 0, SE 762 (3-D at the origin) every tick; then, while
// +0x800 < +0x804, a shot when +0x808 == 0, or when +0x80c % +0x808 == 1; +0x80c counts every such tick; after
// the last shot all five fields are 0. The tick runs before the dispatch (FUN_00465170 L1589 vs L1884), so a
// swing's first shot leaves on the next tick.
// The shot [BYTES 0x46e460..0x46e9d2]: FUN_00593550(0xc3, &origin, &dir, kind 1 (edx), speed 0.5, accel 0,
// gravity 0, power 20 (22 while a Boost runs, DAT_0076c198 & 0x20), knock 1.0, trail effect 0xc4 every 20 ticks)
// for each angle of the fan (degrees, added to the facing +0x314): +0x7fc = 1 {-15, 0, 15} (0x69bc7c; the
// same under Boost, 0x69bc88), +0x7fc = 0 {-3, 0, 3} (0x69bc94; {-6, 0, 6} under Boost, 0x69bca0).
#pragma once

namespace cleria {

struct SilverShots {
    bool wide = false;            // +0x7fc
    unsigned fired = 0;           // +0x800
    unsigned count = 0;           // +0x804
    unsigned period = 0;          // +0x808
    unsigned ticks = 0;           // +0x80c

    // FUN_0046ea00's arm (and the landing's with volley = false).
    void arm(bool w, int character, bool volley = true) {
        wide = w;
        if (w && volley && character == 3) { count = 3; period = 12; }
        else { count = 1; period = 0; }
    }

    struct Tick {
        bool se = false;          // SE 762 this tick
        bool shot = false;        // FUN_0046e460 this tick
        bool wide = false;        // its +0x7fc
    };
    Tick tick(bool flag215) {
        Tick t;
        if (!flag215 || count == 0) return t;
        t.se = true;
        if (fired >= count) return t;
        bool end = false;
        if (period == 0 || ticks % period == 1) {
            ++fired;
            t.shot = true; t.wide = wide;
            end = fired == count;
        }
        ++ticks;
        if (end) *this = SilverShots{};
        return t;
    }
};

// The fan of one shot, degrees from the facing.
inline const float* silver_fan(bool wide, bool boost) {
    static const float kFan[4][3] = {{-15.0f, 0.0f, 15.0f}, {-15.0f, 0.0f, 15.0f}, {-3.0f, 0.0f, 3.0f}, {-6.0f, 0.0f, 6.0f}};
    return kFan[(wide ? 0 : 2) + (boost ? 1 : 0)];
}
inline float silver_power(bool boost) { return boost ? 22.0f : 20.0f; }
inline constexpr int kSilverEffect = 0xc3, kSilverTrail = 0xc4, kSilverSe = 762;

}  // namespace cleria
