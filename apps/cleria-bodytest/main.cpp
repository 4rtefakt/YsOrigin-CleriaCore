// Self-verifying tests for the body push (src/cleria/body_push.hpp, FUN_0045e460), the player
// mover's two knock channels (hit.hpp step_knockback_player) and the hit feedback tables
// (src/cleria/hit_feedback.hpp). Spec: build/ai_spec/HITVOLUME.md §5.1,
// build/combat_re/HIT_FEEDBACK.md. With an asset root it also reports the hit-spark EF2s.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

#include "cleria/body_push.hpp"
#include "cleria/ef2.hpp"
#include "cleria/enemy_mover.hpp"
#include "cleria/hit.hpp"
#include "cleria/hit_feedback.hpp"

using namespace cleria;

namespace {
int g_fail = 0;
void check(bool ok, const char* name) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_fail;
}
bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// The player (cast 13) and a Riffligan (cast 14 / M100) as CASTINFO gives them (HITVOLUME §4.3):
// cylinders, the player's r 0.45 half 0.5 at origin - 0.25, M100's r 0.65 half 0.8 at -0.20.
struct Scene {
    RehitBank pbank, ebank, fbank;
    Impulse pk, ek, fk;
    bodypush::Body player, enemy, foe;
    std::vector<bodypush::Body*> list;
    Scene(float ex, float ez, float speed = 0.0f) {
        player.shape = body_shape({0, 1.075f, 0}, 0.45f, 0.5f, 1, -0.25f);
        player.kind = 0x81; player.push = bodypush::Push::of(pk); player.bank = &pbank; player.token = 1;
        enemy.shape = body_shape({ex, 1.025f, ez}, 0.65f, 0.8f, 1, -0.20f);
        enemy.kind = 0x88; enemy.push = bodypush::Push::of(ek); enemy.bank = &ebank; enemy.token = 2; enemy.speed = speed;
        enemy.section = 2;
        foe = enemy;
        foe.shape.centre.x += 100.0f;                  // far away unless a test moves it
        foe.push = bodypush::Push::of(fk); foe.bank = &fbank; foe.token = 3;
        list = {&player, &enemy, &foe};
    }
};
}  // namespace

int main(int argc, char** argv) {
    std::printf("body push (FUN_0045e460):\n");
    {   // A monster overlapping the player shoves the PLAYER, not itself (0x45e555..0x45e5b2).
        Scene s(1.0f, 0.0f);
        const bool p = bodypush::pass(s.enemy, s.list.data(), s.list.size(), 1);
        check(p, "monster vs player: overlap (centres 1.0 apart, radii 0.45 + 0.65)");
        check(near(s.pk.x, -0.1f) && near(s.pk.z, 0.0f), "player pushed 0.1 away from the monster (min push)");
        check(s.ek.x == 0.0f && s.ek.z == 0.0f, "the monster itself is not pushed");
        check(s.player.touched && s.enemy.touched, "both get +0x3bc |= 8");
    }
    {   // Magnitude: max(0.1, speed * 0.7).
        Scene s(0.0f, 1.0f, 0.3f);
        bodypush::pass(s.enemy, s.list.data(), s.list.size(), 1);
        check(near(s.pk.x, 0.0f) && near(s.pk.z, -0.21f), "push = 0.7 x the pusher's +0x310 when above 0.1");
    }
    {   // Out of reach: 1.1 + epsilon apart horizontally.
        Scene s(1.11f, 0.0f);
        check(!bodypush::pass(s.enemy, s.list.data(), s.list.size(), 1), "no push past r1 + r2 (1.10)");
        Scene t(0.5f, 0.0f);
        t.enemy.shape.centre.y += 1.31f + 0.05f;        // dy beyond half-heights 0.5 + 0.8
        check(!bodypush::pass(t.enemy, t.list.data(), t.list.size(), 1), "no push past the summed half-heights");
    }
    {   // The player's own pass pushes nobody (0x45e545: kind & 1).
        Scene s(1.0f, 0.0f);
        const bool p = bodypush::pass(s.player, s.list.data(), s.list.size(), 1);
        check(p && s.ek.x == 0.0f && s.pk.x == 0.0f, "player pass: overlap flagged, no velocity written");
    }
    {   // In the burst section the player holds and the monster gives way (0x45e5da).
        Scene s(1.0f, 0.0f);
        s.player.section = 0x139;
        bodypush::pass(s.enemy, s.list.data(), s.list.size(), 1);
        check(near(s.pk.x, 0.0f) && near(s.ek.x, 0.1f), "Yunica 0x139: player net 0, monster pushed back 0.1");
        Scene t(1.0f, 0.0f);
        t.player.section = 0x139;
        bodypush::pass(t.enemy, t.list.data(), t.list.size(), 2);
        check(near(t.pk.x, -0.1f) && t.ek.x == 0.0f, "Hugo has no hold section");
        Scene u(1.0f, 0.0f);
        u.player.section = 0x13d;
        bodypush::pass(u.enemy, u.list.data(), u.list.size(), 3);
        check(near(u.pk.x, 0.0f) && near(u.ek.x, 0.1f), "Toal 0x13d holds");
    }
    {   // Section 9 (down) is not pushed.
        Scene s(1.0f, 0.0f);
        s.player.section = 9;
        bodypush::pass(s.enemy, s.list.data(), s.list.size(), 1);
        check(s.pk.x == 0.0f && s.ek.x == 0.0f, "a downed player (section 9) is not pushed");
    }
    {   // Two monsters part symmetrically; one push per actor per tick (the bank token, timer 1).
        Scene s(50.0f, 0.0f);
        s.foe.shape.centre = {50.0f + 1.0f, 1.025f, 0.0f};
        bodypush::pass(s.enemy, s.list.data(), s.list.size(), 1);
        check(near(s.fk.x, 0.1f) && near(s.ek.x, -0.1f), "monster vs monster: both pushed apart");
        bodypush::pass(s.enemy, s.list.data(), s.list.size(), 1);
        check(near(s.fk.x, 0.1f) && near(s.ek.x, -0.1f), "a second pass the same tick is refused by the bank");
        s.ebank.tick();
        bodypush::pass(s.enemy, s.list.data(), s.list.size(), 1);
        check(near(s.fk.x, 0.2f) && near(s.ek.x, -0.2f), "after FUN_0045c490's tick it pushes again");
    }
    {   // Only the FIRST overlapping actor in list order is pushed per tick.
        Scene s(1.0f, 0.0f);
        s.foe.shape.centre = {1.5f, 1.025f, 0.0f};
        bodypush::pass(s.enemy, s.list.data(), s.list.size(), 1);
        check(near(s.pk.x, -0.1f) && s.fk.x == 0.0f, "the player (first in the list) takes the one push");
    }
    {   // A dead / unhittable body (+0x60 & 0x80 clear) neither pushes nor is pushed.
        Scene s(1.0f, 0.0f);
        s.enemy.kind = 0x08;
        check(!bodypush::pass(s.enemy, s.list.data(), s.list.size(), 1), "dead self: no pass");
        Scene t(1.0f, 0.0f);
        t.player.kind = 0x01;
        check(!bodypush::pass(t.enemy, t.list.data(), t.list.size(), 1), "dead / Godspeed player: not a target");
    }
    {   // Masks: the player's 0x3c reaches monsters and NPCs, an NPC's 0x3f everything, a monster's
        // 0x2f the player (bit 0), NPCs (bit 2) and monsters (bit 3).
        check(bodypush::mask_of_kind(0x88) == 0x2f && bodypush::mask_of_kind(0x81) == 0x3c &&
              bodypush::mask_of_kind(0x84) == 0x3f, "the factory's kind -> mask pairs");
    }

    std::printf("player knock channels (FUN_00465170):\n");
    {
        Impulse k1{}, k2{-0.1f, 0.0f};
        const Impulse d = step_knockback_player(k1, k2, true, false);
        check(near(d.x, -0.1f), "the push moves the player this tick");
        check(near(k2.x, -0.1f * (1.0f - 0.675f)), "channel 2 damps x(1 - 5f) = 0.325 on the ground");
        Impulse a1{0.2f, 0.0f}, a2{-0.1f, 0.0f};
        const Impulse e = step_knockback_player(a1, a2, true, false);
        check(near(e.x, 0.1f) && near(a1.x, 0.2f * 0.865f), "channels sum; channel 1 damps x(1 - f)");
        Impulse b1{}, b2{0.5f, 0.0f};
        const Impulse g = step_knockback_player(b1, b2, true, false);
        check(near(g.x, 0.36f), "the sum is capped at 0.36 a tick");
        Impulse c1{}, c2{0.1f, 0.0f};
        step_knockback_player(c1, c2, false, false);
        check(near(c2.x, 0.1f * (1.0f - 5.0f * 0.0405f) * 0.85f), "airborne: (1 - 5f) x 0.85, f = 0.135 x 0.3");
        Impulse z1{0.01f, 0}, z2{0.004f, 0};
        step_knockback_player(z1, z2, true, false);
        check(z1.x == 0.0f && z2.x == 0.0f, "both zeroed when the SUM is under 0.015");
    }
    {   // Steady state: a monster pushing every tick keeps the player moving out at ~0.148/tick,
        // more than the player's walk speed (CASTINFO+0x48 = 0.056 x the stick) can win back.
        Impulse k1{}, k2{};
        float x = 0;
        for (int t = 0; t < 30; ++t) { x += step_knockback_player(k1, k2, true, false).x; k2.x += 0.1f; }
        const float last = step_knockback_player(k1, k2, true, false).x;
        std::printf("    steady push per tick %.4f, 30-tick travel %.3f\n", last, x);
        check(last > 0.14f && last < 0.15f, "steady state 0.1 / 0.675 = 0.148 / tick");
    }
    {   // The enemy mover spends channel 2 the same way (enemy_mover.hpp mover_post).
        MoverState m;
        m.k2 = {0.1f, 0.0f, 0.0f};
        mover_post(m, 0, 0.0f, 0.0f, true, false, true, 1.0f, 10.0f);
        check(near(m.dx, 0.1f) && near(m.k2.x, 0.0325f), "enemy: the push moves it, channel 2 x0.325");
    }

    std::printf("hit feedback (FUN_004fbb40 / FUN_00479b00 / FUN_00478510):\n");
    {
        using namespace hitfx;
        check(no_damage_se(14) == 93 && no_damage_se(0x16) == 545 && no_damage_se(0x49) == 545,
              "no-damage SE: 93 p_parry, 545 z_180_guard for casts 0x16 / 0x49");
        check(skill_hit_effect(3, 1) == -1, "plain melee (class 3): no elemental hit effect");
        check(skill_hit_effect(0, 0) == 0x18 && skill_hit_effect(4, 0) == 0x18 && skill_hit_effect(5, 0) == 0x1f &&
              skill_hit_effect(6, 0) == 0x0d, "classes 0/4, 1/5, 2/6 -> 0x18, 0x1f, 0x0d");
        check(skill_hit_effect(1, 1) == -1 && skill_hit_effect(5, 2) == -1 && skill_hit_effect(2, 1) == 0x0d,
              "Yunica / Hugo: not for classes 1 / 5");
        check(skill_hit_effect(0, 3) == -1, "Toal: never");
        check(hit_spark_effect(1, 0, true, 0) == 0x7e && hit_spark_effect(1, 1, true, 0) == 0x7f,
              "Yunica: 0x7e sword / 0x7f axe");
        check(hit_spark_effect(2, 0, true, 0) == 0x41 && hit_spark_effect(0, 0, true, 0) == 0x41, "else 0x41");
        check(hit_spark_effect(3, 0, true, 0x13d) == -1 && hit_spark_effect(3, 0, false, 0x13d) == 0xb6 &&
              hit_spark_effect(3, 0, true, 4) == 0xb6, "Toal 0xb6; skipped in 0x13d on the alive path only");
        check(near(spark_scale(0x00000000u, 1.5f, 2.0f), 1.5f) && near(spark_scale(0x007fffffu, 1.5f, 2.0f), 2.0f, 1e-6f) &&
              near(spark_scale(0xff400000u, 1.0f, 1.5f), 1.25f), "scale = lo + (hi - lo) x mantissa");
        const Spark sp = place_spark({1, 2, 3}, {0, 0, 1}, 1.0f, 0.0f, 1.7f);
        check(near(sp.pos.z, 2.0f) && near(sp.rot.y, -1.5707964f), "one unit toward the camera, faced at the player");
        check(death_puff(0).effect == 0x56 && death_puff(0).se == -1 && death_puff(0).spark, "death: 0x56 m_zakoyarare");
        check(death_puff(0x2009).effect == -1 && !death_puff(0x2009).spark, "death: +0x130 & 8 skips the puff and spark");
        check(death_puff(0x2000).effect == 0x43 && death_puff(0x2000).se == 862, "death: & 0x2000 -> 0x43 + SE 862");
        check(alive_spark_suppressed(0x1a, true) && !alive_spark_suppressed(0x1a, false), "cast 0x1a in a boss fight");
        // The hurt tint (+0x238, FUN_0045a3a0 / FUN_00473c60), run down in float as the exe does.
        float t = kHurtTimer;
        int ticks = 0;
        while (t != 0.0f && ticks < 100) { t = step_hurt(t); ++ticks; }
        check(ticks == 22 || ticks == 23, "hurt timer: 1.65 runs out in 22 (or 23, float residue) steps of 0.075");
        check(hurt_specular(kHurtTimer) == 0x6b3713u && hurt_specular(0.0f) == 0u,
              "hurt specular: (0x6b, 0x37, 0x13) while running, none after");
        check(step_hurt(0.0f) == 0.0f && step_hurt(0.05f) == 0.0f, "a stopped timer stays 0; it clamps at 0");
        std::printf("    hurt tint lasts %d ticks\n", ticks);
    }

    // The effects themselves, with an asset root: do any of them carry a hit emitter (B2+0x894)?
    if (argc > 1) {
        const std::filesystem::path root = argv[1];
        EftList efl;
        if (efl.load(root)) {
            std::printf("hit-spark EF2s (EFTLIST):\n");
            for (int id : {0x41, 0x7e, 0x7f, 0xb6, 0x56, 0x43, 0x18, 0x1f, 0x0d}) {
                const std::filesystem::path p = efl.path(root, id);
                const Ef2 e = Ef2::load(p);
                int hits = 0;
                for (const Ef2Emitter& em : e.emitters())
                    if (em.b2.size() > 0x894 && em.b2[0x894] != 0) ++hits;
                std::printf("    0x%02x %-40s %s, %zu emitters, %d hit emitters\n", id, p.filename().string().c_str(),
                            e.ok() ? "ok" : "MISSING", e.emitters().size(), hits);
                check(e.ok(), "the spark EF2 loads");
            }
        }
    }

    std::printf(g_fail ? "%d FAILED\n" : "all passed\n", g_fail);
    return g_fail ? 1 : 0;
}
