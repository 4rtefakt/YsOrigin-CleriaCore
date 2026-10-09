// Headless tests for src/cleria/physics_player.hpp — the player's locomotion and
// vertical model as re-derived from FUN_00465170 / FUN_0045a510 (addresses in the header).
#include <cmath>
#include <cstdio>

#include "cleria/enemy_mover.hpp"
#include "cleria/hazard.hpp"
#include "cleria/oxygen.hpp"
#include "cleria/physics_player.hpp"
#include "cleria/rider.hpp"

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
    {   // The enemy mover + integrator, MOVER.md §7's worked example: speed 0.05 along heading 0,
        // height 1.0, flat floor at 0; anime_Jump(0.2) on tick 4.
        MoverState m; VerticalState vs; float x = 0, y = 1.0f;
        float y4 = 0, apex = 0; int apex_t = 0, land_t = 0; float y_land = 0;
        bool x_ok = true;
        for (int t = 1; t <= 40; ++t) {
            int idx_pre = 0;
            mover_pre(m, 2, vs.grounded, false, true, 1, &idx_pre);
            if (t == 4) apply_jump(vs, y, 0.2f, 0.0f);
            mover_post(m, idx_pre, 0.05f, 0.0f, vs.grounded, false, true, 1.0f, 30.0f);
            const float dy = vertical_dy(vs);
            const bool was_air = !vs.grounded;
            x += m.dx; y += dy;
            settle_vertical(vs, y, 0.0f, 1.0f);
            x_ok &= std::fabs(x - 0.05f * float(t)) < 1e-4f;
            if (t == 4) y4 = y;
            if (y > apex) { apex = y; apex_t = t; }
            if (was_air && vs.grounded && land_t == 0) { land_t = t; y_land = y; }
        }
        std::printf("[move] enemy jump: y4 %.4f apex %.4f @%d, land @%d -> %.4f\n", y4, apex, apex_t, land_t, y_land);
        check(x_ok, "enemy mover: x advances 0.05/tick throughout");
        check(std::fabs(y4 - 1.3962f) < 1e-3f, "anime_Jump(0.2) launch tick: y = 1 + 0.2 + 4.905*0.04 = 1.3962");
        check(std::fabs(apex - 2.0794f) < 2e-3f && (apex_t == 15 || apex_t == 16), "apex ~2.0794 on ticks 15-16");
        check(land_t == 27 && std::fabs(y_land - 1.1265f) < 2e-3f, "lands on tick 27 and lerps to 1.1265");
    }
    {   // FUN_005eee60: steps by the rate; snaps within it; mid-turn values wrap into (pi, 3pi].
        const float r = turn_facing(0.18f, 0.0f, 1.0f);
        check(std::fabs(std::fmod(r, 6.2831853f) - 0.18f) < 1e-5f, "turn_facing steps +0.18 toward the target");
        check(std::fabs(turn_facing(0.18f, 0.0f, 0.1f) - 0.1f) < 1e-6f, "turn_facing snaps to a target within the rate");
    }

    {   // Submerged (build/physics_re/WATER.md §2): the dy clamp, the kick, the re-jump.
        VerticalState vs; vs.grounded = true; vs.t = 0.285f;
        check(near(vertical_dy(vs, true), 0.10f), "submerged: a grounded 0.285 launch is capped at +0.10");
        vs.grounded = false; vs.t = -0.5f;
        check(near(vertical_dy(vs, true), -0.05f) && near(vertical_dy(vs, true, true), -0.015f),
              "submerged: the fall is capped at -0.05 (-0.015 on sand2)");
        const CastMotion m{0.75f, 1.0f, 0.056f, 2.2f, 0.285f, -0.25f};
        const float h = m.height();
        VerticalState w; float y = h; float apex = 0; int apex_t = 0, land_t = 0;
        begin_rejump(w, y, m);                                  // every jump kicks (0x468dc8)
        for (int t = 1; t <= 120 && !land_t; ++t) {
            y += vertical_dy(w, true);
            const bool air = !w.grounded;
            settle_vertical(w, y, 0.0f, h, true);
            if (y - h > apex) { apex = y - h; apex_t = t; }
            if (air && w.grounded) land_t = t;
        }
        std::printf("[move] water jump: apex %.4f @%d, lands @%d\n", apex, apex_t, land_t);
        check(std::fabs(apex - 1.95f) < 0.06f, "one water jump peaks near 1.95 (WATER.md §2c)");
        VerticalState c; float yc = h; int jumps = 0;
        begin_rejump(c, yc, m);
        for (int t = 1; t <= 200; ++t) {
            if (can_rejump(c, true)) { begin_rejump(c, yc, m); ++jumps; }
            yc += vertical_dy(c, true);
            settle_vertical(c, yc, 0.0f, h, true);
        }
        std::printf("[move] water climb: %d re-jumps, feet %.2f after 200 ticks\n", jumps, yc - h);
        check(yc - h > 15.0f && jumps >= 8, "re-jumping at t <= 0.05 climbs ~0.1/tick without limit");
        VerticalState tr; tr.t = 0.285f; water_transition(tr);
        check(near(tr.t, 0.1f), "a change of +0x38a caps the timer at 0.1");
        check(near(surface_scale(0, true).xz, 0.8f) && near(surface_scale(0, true, true).xz, 0.9f) &&
              near(surface_scale(0, true, false, true).xz, 1.0f), "submerged xz x0.8 / x0.9 blessing / x1 foot gear");
    }
    {   // Oxygen (WATER.md §3).
        Oxygen o; int n = 0;
        while (o.o2 > 0.0f && n < 10000) { o.tick(true, 0, false, false, 600.0f); ++n; }
        check(n == 600, "600 ticks of air without the Scales");
        check(near(o.tick(true, 0, false, false, 600.0f), 10.0f), "drowning: maxHP / 60 a tick");
        Oxygen s; n = 0;
        while (s.o2 > 0.0f && n < 10000) { s.tick(true, 0, false, true, 600.0f); ++n; }
        check(n == 6000, "6000 ticks with the Scales held");
        Oxygen r; r.tick(true, 0, false, false, 600.0f);
        int fade = 0;
        while (r.alpha != 0) { r.tick(false, 0, false, false, 600.0f); ++fade; }
        r.o2 = 100.0f; r.tick(false, 0, false, false, 600.0f);
        check(fade == 51 && r.o2 == Oxygen::kMax, "the gauge fades over 51 ticks, then snaps to full");
    }

    {   // Blessing bit 1 and the sand gear on the bad floors (BLESSINGS.md §3.2b).
        const SurfaceScale s1 = surface_scale(0x12, false, true), s2 = surface_scale(0x12, false, false, false, true),
                           s3 = surface_scale(0x12, false, true, false, true), l1 = surface_scale(0x0d, false, true),
                           b1 = surface_scale(0x11, false, true);
        check(near(s1.xz, 0.5f) && near(s1.up, 0.25f), "sand2 with the blessing: xz 0.5, the rise stays 0.25");
        check(near(s2.xz, 0.5f) && near(s2.up, 0.5f) && near(s3.xz, 0.75f) && near(s3.up, 0.5f),
              "sand2 with the sand gear: 0.5 / 0.5; gear + blessing 0.75 / 0.5");
        check(near(l1.xz, 0.8f) && near(l1.up, 0.6f) && near(b1.xz, 0.8f), "lava / blood with the blessing: xz 0.8, the rise 0.6");
    }
    {   // The standing HP regeneration (FUN_00465170 0x466b7f..0x466c77, src/cleria/hazard.hpp).
        StandingRegen::In in; in.flag209 = true; in.section = 2; in.hp = 30; in.max_hp = 120;
        StandingRegen r; int first = 0, n = 0; float got = 0;
        for (int k = 1; k <= 400; ++k) { const float a = r.tick(in); if (a > 0) { if (!first) first = k; ++n; got = a; } }
        check(first == 312 && n == 4 && near(got, 2.0f), "flag-209 room: after 300 ticks, max HP / 60 every 24");
        StandingRegen b; in.blessing1 = true; first = 0;
        for (int k = 1; k <= 200; ++k) if (b.tick(in) > 0 && !first) first = k;
        check(first == 156, "blessing bit 0: the delay 150 and the period 12");
        StandingRegen nm; in.difficulty = 2; first = 0;
        for (int k = 1; k <= 400; ++k) if (nm.tick(in) > 0 && !first) first = k;
        check(first == 312, "Nightmare doubles both (600 / 48), halved again by the blessing");
        in.difficulty = 0; in.blessing1 = false;
        StandingRegen acc; in.acc_regen = true; got = 0;
        for (int k = 1; k <= 312; ++k) { const float a = acc.tick(in); if (a > 0) got = a; }
        check(near(got, 4.0f), "the regen accessory in a flag-209 room doubles the amount");
        StandingRegen acc2; in.flag209 = false; in.blessing1 = true; first = 0; got = 0;
        for (int k = 1; k <= 312; ++k) { const float a = acc2.tick(in); if (a > 0) { got = a; if (!first) first = k; } }
        check(first == 312 && near(got, 2.0f), "the accessory alone: the unblessed rate (the blessing needs flag 209)");
        StandingRegen none; in.acc_regen = false;
        for (int k = 0; k < 400; ++k) none.tick(in);
        check(none.n == 0, "no accessory and no flag 209: nothing counts");
        StandingRegen mv; in.flag209 = true;
        for (int k = 0; k < 200; ++k) mv.tick(in);
        in.stick = true; mv.tick(in);
        check(mv.n == 0, "a stick input restarts the count");
        in.stick = false; in.section = 0x100; mv.tick(in); mv.tick(in);
        check(mv.n == 0, "any section but the idle one restarts it");
        StandingRegen bb; in.section = 2; in.boss_battle = true;
        float s = 0; for (int k = 0; k < 400; ++k) s += bb.tick(in);
        check(s == 0.0f, "no regen in a boss battle (flag 156)");
    }

    {   // Floor hazards and the noise drain (FUN_00462550, src/cleria/hazard.hpp).
        SurfaceHazard h;
        SurfaceHazard::In in; in.surface = 0x0d; in.grounded = true; in.max_hp = 100;
        float total = 0; int hits = 0;
        for (int k = 0; k < 30; ++k) { const float d = h.tick(in); total += d; hits += d > 0; }
        check(hits == 3 && near(total, 9.0f), "lava, Normal: 3 HP every 10 ticks while grounded");
        SurfaceHazard e; in.easy = true;
        check(near(e.tick(in), 1.0f), "lava on Easy: 1 HP");
        in.easy = false;
        SurfaceHazard air; in.grounded = false;
        float a2 = 0; for (int k = 0; k < 30; ++k) a2 += air.tick(in);
        check(a2 == 0.0f && air.t == 30, "lava while airborne: no damage, the counter still runs");
        SurfaceHazard iv; in.grounded = true; in.iframes = true;
        check(iv.tick(in) == 0.0f && iv.t == 0, "i-frames: the block is skipped, the counter holds");
        SurfaceHazard n; SurfaceHazard::In ni; ni.noise = true; ni.max_hp = 200;
        int nh = 0; for (int k = 0; k < 1000; ++k) nh += n.tick(ni) > 0.0f;
        check(nh == 20, "noise: maxHP x 0.05 every 50 ticks for the first 1000 ticks");
        for (int k = 0; k < 4000; ++k) n.tick(ni);
        int fast = 0; for (int k = 0; k < 100; ++k) fast += n.tick(ni) > 0.0f;
        check(fast == 100, "noise past 60 counts: every tick");
        SurfaceHazard tl; ni.character = 3; ni.boosting = true;
        int th = 0; float tv = 0; for (int k = 0; k < 900; ++k) { const float d = tl.tick(ni); th += d > 0; tv = d > 0 ? d : tv; }
        check(th == 3 && near(tv, 1.0f), "noise, Toal boosting: 1 HP every 300 ticks");
    }

    {   // The platform carry by the floor sub-mesh's whole matrix (FUN_0045a510 0x45a9a4.., src/cleria/rider.hpp).
        auto yaw_at = [](float deg, const Vec3& at) {             // a turn about the vertical axis through `at`
            const float a = deg * 0.017453292f, c = std::cos(a), s = std::sin(a);
            Mat4 m;
            m.m = {c, 0, s, 0, 0, 1, 0, 0, -s, 0, c, 0, 0, 0, 0, 1};
            const Vec3 r = m.transform_point({-at.x, -at.y, -at.z});
            m.m[12] = r.x + at.x; m.m[13] = r.y + at.y; m.m[14] = r.z + at.z;
            return m;
        };
        const Vec3 hub{44.0f, 2.0f, 42.0f};
        const Mat4 a = yaw_at(30.0f, hub), b = yaw_at(30.675f, hub);   // S_5012's floor: 0.675 degrees a tick
        const Vec3 p{51.0f, 3.075f, 42.0f};
        const RiderCarry rc = rider_carry(a, b, p);
        const Vec3 q{p.x + rc.d.x, p.y + rc.d.y, p.z + rc.d.z};
        const float r0 = std::hypot(p.x - hub.x, p.z - hub.z), r1 = std::hypot(q.x - hub.x, q.z - hub.z);
        check(near(r0, r1) && near(rc.d.y, 0.0f) && near(std::hypot(rc.d.x, rc.d.z), 7.0f * 0.675f * 0.017453292f, 1e-3f),
              "rider carry: a floor turning about its hub moves the rider along the circle (radius kept)");
        check(near(rc.yaw, 0.675f * 0.017453292f, 1e-5f), "rider carry: the floor's turn is added to the heading");
        Mat4 t0, t1;
        t1.m[12] = 0.1f; t1.m[13] = 0.05f;
        const RiderCarry tr = rider_carry(t0, t1, p);
        check(near(tr.d.x, 0.1f) && near(tr.d.y, 0.05f) && near(tr.d.z, 0.0f) && tr.yaw == 0.0f,
              "rider carry: a translating floor carries by its move, no turn");
        t1.m[12] = 3.0f; t1.m[13] = 4.0f;
        const RiderCarry big = rider_carry(t0, t1, p);
        check(near(std::sqrt(big.d.x * big.d.x + big.d.y * big.d.y + big.d.z * big.d.z), 0.3f) && near(big.d.x, 0.18f),
              "rider carry: scaled to length 0.3 when its square exceeds 0.09");
        const Mat4 inv = affine_inverse(a) * a;
        bool id = true;
        for (int i = 0; i < 16; ++i) id = id && near(inv.m[std::size_t(i)], (i % 5 == 0) ? 1.0f : 0.0f, 1e-4f);
        check(id, "affine_inverse(M) x M = identity");
    }

    std::printf("%s\n", fails ? "SOME TESTS FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
