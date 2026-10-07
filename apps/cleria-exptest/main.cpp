// cleria-exptest — the kill / boss EXP award and the combo meter (src/cleria/exp_award) against the
// bytes cited in exp_award.hpp (FUN_004fa2f0, FUN_004fbb40 0x4fcb45..0x4fcc13, FUN_004fa4a0,
// 0x4fc835..0x4fc86e, 0x466b5d..0x466b79). The expected numbers are worked by hand from those
// constants; the Selnarg rows (EXP 33, level 3) are the ones tools/drive_regress.py checks live.
#include <cmath>
#include <cstdio>

#include "cleria/exp_award.hpp"

using namespace cleria::expaward;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[exp] %-74s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b) { return std::fabs(a - b) < 1e-3f; }

static float kill(float base, int plv, int elv, int diff = 0, float rate = 1.0f, float dm = 1.0f, int combo = 0) {
    KillIn k;
    k.base = base; k.player_level = plv; k.enemy_level = elv; k.difficulty = diff;
    k.rate = rate; k.diff_mult = dm; k.combo = combo;
    return kill_award(k);
}

int main(int, char**) {
    {   // FUN_004fa2f0
        check(near(level_scale(33, 3, 3, 0), 33.0f * 0.6666f), "equal level: x0.6666 only");
        check(near(level_scale(33, 1, 3, 0), 33.0f * 2.25f * 0.6666f), "2 levels under: x1.5^2 x0.6666");
        check(near(level_scale(33, 1, 9, 0), 33.0f * 3.375f * 0.6666f), "8 levels under: the bonus caps at 1.5^3");
        check(near(level_scale(33, 5, 3, 0), 33.0f / 2.25f * 0.6666f), "2 levels over (Normal): / 1.5^2");
        check(near(level_scale(33, 5, 3, 1), 33.0f / (1.65f * 1.65f) * 0.6666f), "2 levels over (Hard): / 1.65^2");
        check(near(level_scale(33, 5, 3, 2), 33.0f / (1.8f * 1.8f) * 0.6666f), "2 levels over (Nightmare): / 1.8^2");
        check(level_scale(0, 1, 9, 0) == 0.0f, "a zero base stays 0 (the != 0 test skips the x0.6666)");
        check(kScale == 0.6666f, "0x6bd358 = 0.6666f");
    }
    {   // the kill award
        check(kill(33, 3, 3) == 21.0f, "Selnarg at its own level: floor(21.998) = 21");
        check(kill(33, 1, 3) == 49.0f, "Selnarg lv3 for a lv1 player: floor(49.495) = 49");
        check(kill(33, 5, 3) == 9.0f, "Selnarg lv3 for a lv5 player: floor(9.777) = 9");
        check(kill(33, 1, 9) == 74.0f, "8 levels under caps at 3: floor(74.24) = 74");
        check(kill(1, 20, 3) == 1.0f, "a tiny award is still 1 (max 1 after the floor)");
        check(kill(0, 1, 1) == 1.0f, "even a zero-EXP kill credits 1");
        check(kill(33, 1, 3, 0, 2.0f) == 98.0f, "the rate multiplies before the floor: floor(98.99) = 98");
        check(near(kill(33, 1, 3, 0, 1.0f, 1.0f, 4), 49.0f * 1.04f), "a 4-hit combo: x1.04 after the floor");
        check(near(kill(33, 1, 3, 0, 2.0f, 1.0f, 4), 98.0f * 1.04f), "rate 2 and a 4-hit combo: 101.92");
        check(near(kill(33, 1, 3, 0, 1.0f, 1.0f, 99), 49.0f * 1.99f), "a 99-hit combo: x1.99");
        KillIn k; k.base = 33; k.player_level = 1; k.enemy_level = 3; k.awarded = true;
        check(kill_award(k) == 0.0f, "the award-once bit: 0");
        check(diff_mult(false, false) == 1.0f && diff_mult(true, false) == 1.1f && diff_mult(false, true) == 2.0f &&
              diff_mult(true, true) == 2.0f, "difficulty: 1 / Easy 1.1 / Very Easy 2.0 (wins)");
        check(kill(33, 3, 3, 0, 1.0f, 1.1f) == 24.0f, "Easy: floor(21.998 x 1.1) = 24");
        check(near(chain_bonus(0), 1.0f) && near(chain_bonus(50), 1.5f), "chain bonus 1 + 0.01 n");
    }
    {   // FUN_004fa4a0
        check(boss_award(0, 1, 8, 0, 1.0f, 1.0f) == 0.0f, "boss: a zero EXP awards nothing");
        check(boss_award(300, 8, 8, 0, 1.0f, 1.0f) == 199.0f, "boss at the player's level: floor(199.98) = 199");
        check(boss_award(300, 5, 8, 0, 1.0f, 1.0f) == 674.0f, "boss 3 levels up: floor(300 x 3.375 x 0.6666) = 674");
        check(boss_award(300, 8, 8, 0, 1.0f, 2.0f) == 399.0f, "boss, Very Easy: floor(399.96) = 399");
    }
    {   // the combo meter
        Combo c;
        c.hit();
        check(c.count == 1 && c.timer == 720, "first hit: count 1, timer 720");
        for (int i = 0; i < 100; ++i) c.tick();
        c.hit();
        check(c.count == 2 && c.timer == 680, "a later hit adds 60 (620 + 60)");
        c.hit(); c.hit();
        check(c.count == 4 && c.timer == 720, "the timer caps at 720");
        for (int i = 0; i < 719; ++i) c.tick();
        check(c.count == 4 && c.timer == 1, "still counting one tick before the end");
        c.tick();
        check(c.count == 0 && c.timer == 0, "the count drops to 0 when the timer runs out");
        c.tick();
        check(c.count == 0 && c.timer == 0, "an idle meter stays at 0");
        Combo m;
        for (int i = 0; i < 150; ++i) m.hit();
        check(m.count == 99, "the count caps at 99");
    }
    std::printf("%s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
