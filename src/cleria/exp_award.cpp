#include "cleria/exp_award.hpp"

#include <algorithm>
#include <cmath>

namespace cleria::expaward {

float level_scale(float base, int player_level, int enemy_level, int difficulty, float under_base) {
    const int d = enemy_level - player_level;              // 0x4fa2f9 sub edx, ecx
    float x = base;
    if (d < 0) {                                           // over-levelled: divss base, pow(F, -d)
        const float f = kOverBase[std::clamp(difficulty, 0, 2)];
        x = base / float(std::pow(double(f), double(float(-d))));
    } else if (d > 0) {                                    // under-levelled: pow(G, min(d, 3)) * base
        x = float(std::pow(double(under_base), double(float(std::min(d, kUnderCap))))) * base;
    }
    if (x != 0.0f) x *= kScale;                            // 0x4fa36a..0x4fa377
    return x;
}

float diff_mult(bool easy, bool very_easy) {
    float m = 1.0f;                                        // 0x6bd438
    if (easy) m = 1.1f;                                    // 0x6bd464
    if (very_easy) m = 2.0f;                               // 0x6bd580
    return m;
}

float chain_bonus(int combo) {
    const double c = double(static_cast<unsigned>(combo));  // cvtdq2pd + the 2^32 fixup at 0x6becd0
    return float(c) * 0.01f + 1.0f;                        // 0x6bd084, 0x6bd438
}

float kill_award(const KillIn& in) {
    float x = level_scale(in.base, in.player_level, in.enemy_level, in.difficulty, in.under_base);
    x = x * in.rate;                                       // 0x4fcb9c
    x = x * in.diff_mult;                                  // 0x4fcba4
    x = float(std::floor(double(x)));                      // FUN_004217f0
    float amt = 1.0f > x ? 1.0f : x;                       // maxss xmm2(1.0), xmm0
    if (in.awarded) amt = 0.0f;                            // 0x4fcbad / 0x4fcbc8
    return chain_bonus(in.combo) * amt;                    // 0x4fcbff..0x4fcc03
}

float boss_award(float base, int player_level, int boss_level, int difficulty, float rate, float dm,
                 float under_base) {
    if (base == 0.0f) return 0.0f;                         // 0x4fa4b5..0x4fa4c0
    const float s = level_scale(base, player_level, boss_level, difficulty, under_base);
    float x = rate * s;                                    // 0x4fa4fd..0x4fa505
    x = x * dm;                                            // 0x4fa509
    x = float(std::floor(double(x)));                      // FUN_004217f0
    return 1.0f > x ? 1.0f : x;                            // 0x4fa521 maxss
}

void Combo::hit() {
    timer += count == 0 ? kComboFirst : kComboAdd;
    if (static_cast<unsigned>(timer) > static_cast<unsigned>(kComboTimerCap)) timer = kComboTimerCap;   // cmova
    if (count < kComboCap) ++count;
}

void Combo::tick() {
    if (timer == 0) return;
    if (--timer == 0) count = 0;
}

}  // namespace cleria::expaward
