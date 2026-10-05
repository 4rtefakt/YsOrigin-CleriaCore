// Headless tests for src/cleria/physics_player.hpp — the player's locomotion and
// vertical model as re-derived from FUN_00465170 / FUN_0045a510 (addresses in the header).
#include <cmath>
#include <cstdio>

#include "cleria/physics_player.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[move] %-60s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

int main() {
    CastMotion m;  // CASTINFO record 13 (CAST_ADOL), the player cast
    m.radius_base = 0.75f; m.height_base = 1.0f; m.speed_base = 0.056f;
    m.run_mult = 2.2f; m.jump_v0 = 0.285f; m.center_offset = -0.25f;
    check(near(m.height(), 1.075f) && near(m.radius(), 0.675f), "height 1.075, radius 0.675");

    // --- speed (+0x310) ---------------------------------------------------------
    check(near(player_speed(m, 1, 1, 1, false, true, false), 0.056f), "walk = 0.056/tick (3.4 u/s)");
    check(near(player_speed(m, 1, 1, 1, true, true, false), 0.056f * 2.2f * 1.125f), "Yunica run = x2.2 x1.125 (8.3 u/s)");
    check(near(player_speed(m, 2, 1, 1, true, true, false), 0.056f * 2.2f), "Hugo run = x2.2 only");
    check(near(player_speed(m, 3, 1, 1, true, true, false), 0.056f * 2.2f * 1.275f), "Toal run = x2.2 x1.275");
    check(near(player_speed(m, 1, 1, 1, true, true, false, true), 0.056f * 2.2f * 1.225f), "blessing 0x2000 -> 1.225");
    check(near(player_speed(m, 1, 1, 1, true, false, false), 0.056f * 2.2f * 0.6f), "airborne run = x2.2 x0.6");
    check(near(player_speed(m, 1, 1, 1, true, false, true), 0.056f * 2.2f * 1.125f), "airborne in a skill section keeps the char const");
    check(player_speed(m, 1, 1, 0, true, true, false) == 0.0f, "anime_Movable 0 roots the player");

    // --- planar velocity --------------------------------------------------------
    PlanarVelocity v;
    step_planar(v, true, 0.0f, 0.1f, true);
    check(near(v.x, 0.1f) && near(v.z, 0.0f), "grounded input snaps, (cos,sin) basis");
    step_planar(v, false, 0.0f, 0.0f, true);
    check(v.x == 0.0f && v.z == 0.0f, "grounded, no input: stop dead");
    v = {};
    step_planar(v, true, 1.5707963f, 0.1f, false);
    check(near(v.x, 0.0f) && near(v.z, 0.012f), "airborne input lerps at 0.12");
    v = {0.1f, 0.0f};
    step_planar(v, false, 0.0f, 0.0f, false);
    check(near(v.x, 0.095f), "airborne, no input: decays at 0.05");
    v = {0.004f, 0.004f};
    step_planar(v, false, 0.0f, 0.0f, false);
    check(v.x == 0.0f && v.z == 0.0f, "deadzone 0.005 zeroes it");

    // --- surfaces ---------------------------------------------------------------
    check(near(surface_scale(0x12, false).xz, 0.25f) && near(surface_scale(0x0d, false).xz, 0.6f) &&
          near(surface_scale(0x11, false).up, 0.6f) && near(surface_scale(1, false).xz, 1.0f) &&
          near(surface_scale(1, true).xz, 0.8f), "surface multipliers (sand2/lava/blood/water/default)");

    // --- vertical ---------------------------------------------------------------
    const float h = m.height(), floor0 = 0.0f;
    {   // Rest: a grounded actor's origin sits at floor + height and stays there.
        VerticalState s; float y = floor0 + h;
        for (int i = 0; i < 120; ++i) { y += vertical_dy(s); settle_vertical(s, y, floor0, h); }
        check(s.grounded && std::fabs(y - (floor0 + h)) < 0.01f, "idle origin rests at floor + 1.075");
    }
    {   // Jump arc.
        VerticalState s; float y = floor0 + h;
        begin_jump(s, m);
        float dy0 = vertical_dy(s);
        check(near(dy0, 0.285f), "launch tick moves t linearly (0.285), not t*|t|*4.905");
        y += dy0; settle_vertical(s, y, floor0, h);
        check(!s.grounded, "airborne after the launch tick");
        float apex = y; int apex_tick = 1, land_tick = -1;
        for (int i = 2; i < 200 && land_tick < 0; ++i) {
            y += vertical_dy(s);
            if (y > apex) { apex = y; apex_tick = i; }
            if (settle_vertical(s, y, floor0, h)) land_tick = i;
        }
        std::printf("[move]   jump: apex %.3f above rest at tick %d, lands tick %d\n",
                    apex - (floor0 + h), apex_tick, land_tick);
        check(apex_tick >= 16 && apex_tick <= 18, "apex near tick 17 (t reaches 0)");
        check(land_tick > 30 && land_tick < 40, "lands ~tick 34");
    }
    {   // Walking off a ledge: 4 coyote ticks still count as grounded.
        VerticalState s; float y = floor0 + h;
        for (int i = 0; i < 10; ++i) { y += vertical_dy(s); settle_vertical(s, y, floor0, h); }
        int grounded_ticks = 0;
        for (int i = 0; i < 6; ++i) {
            y += vertical_dy(s);
            settle_vertical(s, y, -5.0f, h);
            if (s.grounded) ++grounded_ticks;
        }
        check(grounded_ticks == 4, "4 coyote ticks before falling");
    }
    {   // Falling margin 0.4: an actor 0.39 above its rest point lands; 0.41 does not.
        VerticalState s; s.grounded = false; s.t = -0.1f;
        float y = floor0 + h + 0.39f;
        check(settle_vertical(s, y, floor0, h) && near(y, floor0 + h + 0.39f * 0.35f), "lands within 0.4, lerps 0.65");
        VerticalState s2; s2.grounded = false; s2.t = -0.1f;
        float y2 = floor0 + h + 0.41f;
        check(!settle_vertical(s2, y2, floor0, h) && !s2.grounded, "0.41 above: still falling");
        VerticalState s3; s3.grounded = false; s3.t = 0.1f;   // rising: margin 0.01
        float y3 = floor0 + h + 0.02f;
        check(!settle_vertical(s3, y3, floor0, h), "rising uses the 0.01 margin");
    }
    {   VerticalState s; s.grounded = false; s.t = -0.5f;
        check(near(vertical_dy(s), -0.65f), "terminal fall clamp -0.65/tick");
        float high = 100.0f;
        settle_vertical(s, high, 0.0f, h);
        check(near(s.t, -0.5f), "timer floors at -0.5");
    }

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
