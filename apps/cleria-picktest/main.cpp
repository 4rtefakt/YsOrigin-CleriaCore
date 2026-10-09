// cleria-picktest — world item pickups (src/cleria/pickup.hpp) against
// build/item_re/PICKUPS.md: the RNG draw counts, the launch ranges, the bounce to rest, the
// 50-tick collect lock, the lifetime and fade, the chest item, the death rolls and the glint.
// Usage: cleria-picktest
#include <cmath>
#include <cstdio>
#include <vector>

#include "cleria/enemies.hpp"
#include "cleria/pickup.hpp"
#include "cleria/popup.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[pick] %-70s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

// A flat floor at y = 0 (normal +Y), as the room's segment query.
static bool floor_hit(const Vec3& a, const Vec3& b, Vec3* p, Vec3* n) {
    if (!((a.y >= 0 && b.y < 0) || (a.y > 0 && b.y <= 0))) return false;
    const float t = a.y / (a.y - b.y);
    *p = {a.x + (b.x - a.x) * t, 0, a.z + (b.z - a.z) * t};
    *n = {0, 1, 0};
    return true;
}

int main() {
    ef2sim::Rng rng;
    {   // §4.3: one enemy drop spawn = 6 draws; life 990 / 991; speeds in range.
        std::vector<Pickup> out;
        bool ranges = true, lives = true;
        for (int i = 0; i < 200; ++i) {
            const std::uint64_t before = rng.calls;
            const int id = 0x42;
            spawn_pickups(rng, &id, 1, {0, 1, 0}, SpawnArgs{}, false, out);
            if (rng.calls - before != 6) ranges = false;
            const Pickup& p = out.back();
            const float h = std::sqrt(p.vel.x * p.vel.x + p.vel.z * p.vel.z);
            if (h < 0.0099f || h > 0.0451f || p.vel.y < 0.04f || p.vel.y > 0.065f) ranges = false;
            if (p.life != 990.0f && p.life != 991.0f) lives = false;
        }
        check(ranges, "a drop spawn takes 6 draws; speed 0.01-0.045, vy 0.04-0.065");
        check(lives, "a drop lives 990 or 991 ticks");
    }
    PickupWorld w;
    w.hit = floor_hit;
    w.player = {100, 0, 100};
    {   // §6.3: it lands and comes to rest within about a second, on the floor.
        std::vector<Pickup> out;
        const int id = 0x78;
        spawn_pickups(rng, &id, 1, {0, 0.5f, 0}, SpawnArgs{}, false, out);
        Pickup& p = out.back();
        int t = 0;
        while (!p.resting && t < 300) { step_pickup(rng, p, w); ++t; }
        check(p.resting && t < 90 && std::fabs(p.pos.y) < 0.12f, "a drop bounces to rest on the floor within 90 ticks");
        check(p.ground && p.shadow_alpha() > 0.3f, "a resting drop has ground under it (the shadow)");
        // §8.2 / §8.3 (FUN_00586bf0 / FUN_00586e40): where it draws, for a camera 30 deg down
        // looking along +z (F = (0, -0.5, 0.866), V = (0, 0.866, 0.5)).
        const Vec3 F{0.0f, -0.5f, 0.8660254f}, V{0.0f, 0.8660254f, 0.5f};
        const Vec3 c = p.sprite_centre(V, F);
        check(std::fabs(c.y - (p.pos.y + 0.1f + 0.075f + 0.075f * 0.8660254f)) < 1e-4f &&
              std::fabs(c.z - (p.pos.z - 0.15f * 0.8660254f + 0.075f * 0.5f)) < 1e-4f,
              "sprite centre = pos + (0, 0.1, 0) - F x 0.15 + V x 0.075");
        check(c.y - p.half * V.y > p.pos.y - 0.02f,
              "a resting drop's camera-plane quad stays (within 2 cm) above the floor it lies on");
        const Vec3 s = p.shadow_centre(F);
        check(std::fabs(s.y - (p.ground_y + 0.075f)) < 1e-4f && std::fabs(p.ground_y - 0.02f) < 1e-4f,
              "shadow at (x, ground = hit y + 0.02, z) - F x 0.15: above the floor, not in it");
        Pickup wet = p;
        wet.in_water = true;
        check(std::fabs(wet.sprite_centre(V, F).z - (p.pos.z + 0.075f * 0.5f)) < 1e-4f, "in water the -F x 0.15 term is dropped");
    }
    {   // §6.2 / §7.2: not collectable before age 50; collected after; refused stays.
        std::vector<Pickup> out;
        const int id = 0x42;
        spawn_pickups(rng, &id, 1, {0, 0, 0}, SpawnArgs{}, false, out);
        Pickup p = out.back();
        int touched_at = -1, tick = 0;
        PickupWorld near = w;
        near.player = {0.5f, 0, 0};
        near.touch = [&](Pickup&) { if (touched_at < 0) touched_at = tick; return true; };
        for (tick = 1; tick <= 120 && !p.dead; ++tick) step_pickup(rng, p, near);
        check(touched_at == 51, "the touch test first runs on age 51 (age > 50)");
        Pickup q = out.back();
        near.touch = [&](Pickup&) { return false; };
        for (int i = 0; i < 120; ++i) step_pickup(rng, q, near);
        check(!q.dead, "a refused grant (full wallet / count) leaves the pickup on the ground");
        check(!pickup_in_reach(q, {q.pos.x, q.pos.y + 3.0f, q.pos.z}) &&
              pickup_in_reach(q, {q.pos.x, q.pos.y + 2.7f, q.pos.z}),
              "reach: height weighted by 2/3 (2.7 up in reach, 3.0 not)");
    }
    {   // §6.1 / §8.2: expiry and the 35-tick fade; flag 8 never expires.
        std::vector<Pickup> out;
        const int id = 0x42;
        spawn_pickups(rng, &id, 1, {0, 0, 0}, SpawnArgs{}, false, out);
        Pickup p = out.back(), q = out.back();
        q.flags |= 8;
        int died = -1;
        for (int t = 1; t <= 1100 && !p.dead; ++t) { step_pickup(rng, p, w); if (p.dead) died = t; }
        check(died == int(out.back().life), "a drop dies on the tick its age reaches its life");
        Pickup f = out.back();
        f.age = f.life - 17.5f;
        check(std::fabs(f.alpha() - 0.5f) < 0.01f, "alpha is (life - age) / 35 in the last 35 ticks");
        for (int t = 0; t < 1100; ++t) step_pickup(rng, q, w);
        check(!q.dead && q.alpha() == 1.0f, "flag 8: never expires, never fades");
    }
    {   // §9: the chest item rises 0.55, bobs, dies at 210 and is never collected.
        Pickup c = make_chest_item(rng, 89, {0, 0, 0}, false);
        PickupWorld near = w;
        near.player = c.pos;
        bool touched = false;
        near.touch = [&](Pickup&) { touched = true; return true; };
        float y90 = 0;
        int died = -1;
        for (int t = 1; t <= 300 && !c.dead; ++t) {
            step_pickup(rng, c, near);
            if (t == 90) y90 = c.pos.y;
            if (c.dead) died = t;
        }
        check(std::fabs(y90 - 0.85f) < 0.01f, "the chest item is 0.3 + 0.55 up at tick 90");
        check(died == 210 && !touched, "the chest item dies at 210 and is never collected");
    }
    {   // §2.2: the slot roll; a 100% slot always drops, a 0% slot never.
        Enemy e;
        e.drops[0] = {0x78, 100.0f};
        e.drops[1] = {0x42, 0.0f};
        DropContext c;
        c.boss_lock = true;
        int gold = 0, heal = 0;
        for (int i = 0; i < 100; ++i) {
            std::vector<Pickup> out;
            roll_enemy_drops(rng, e, 14, {0, 0, 0}, c, out);
            for (const Pickup& p : out) { gold += p.id == 0x78; heal += p.id == 0x42; }
        }
        check(gold == 100 && heal == 0, "slot rolls: 100% always, 0% never");
        c.no_drops = true;
        std::vector<Pickup> none;
        roll_enemy_drops(rng, e, 14, {0, 0, 0}, c, none);
        check(none.empty(), "flag 146 / +0x130 & 1: no drops");
        c.no_drops = false; c.boss_lock = false; c.launch = 0; c.combo = 99;
        int buffs = 0;
        for (int i = 0; i < 3000; ++i) {
            std::vector<Pickup> out;
            roll_enemy_drops(rng, e, 14, {0, 0, 0}, c, out);
            for (const Pickup& p : out) buffs += p.id >= 72 && p.id <= 77;
        }
        check(buffs > 2400 && buffs < 3600, "buff scatter at combo 99: ~1/3 per category (3 categories)");
    }
    {   // §7.1: the glint flashes for 160 ms every 1360 ms (grey 70 -> 7).
        Pickup p;
        p.flags = 5; p.age = 60;
        int lit = 0;
        for (std::uint32_t ms = 0; ms < 1360; ms += 16) lit += p.glint(ms) > 0;
        check(lit == 10, "the glint is lit 10 of 85 steps (160 of 1360 ms)");
    }
    {   // §13: the GoPopup.
        Popup p = make_popup("25", 0xcdacd0, 0.75f, {0, 0, 0});
        check(p.n == 4 && p.glyphs.size() == 2, "\"25\": two glyphs, n = 4 triangles");
        const float k = 0.75f * 0.02f;
        check(std::fabs(p.glyphs[0].top + 12 * k) < 1e-5f && std::fabs(p.glyphs[0].bottom + 35 * k) < 1e-5f,
              "glyphs hang from 12k to 35k below the anchor");
        check(std::fabs(p.glyphs[0].x0 + p.glyphs[1].x1 - 2.0f * k) < 1e-4f, "centred on the anchor (w counts adv - 2)");
        int life = 0;
        float a20 = 0, a_end = 0;
        while (!p.dead && life < 200) { step_popup(p); ++life; if (p.age == 20) a20 = p.alpha; if (p.age == p.n + 35) a_end = p.alpha; }
        check(life == 47, "lifetime 2 x glyphs + 43 = 47 ticks");
        check(std::fabs(a20 - 13.0f / 14.0f) < 1e-5f && a_end == 1.0f, "alpha holds at 13/14, then 1.0 at n + 35");
        Popup q = make_popup("25", 0, 1, {0, 0, 0});
        q.age = 16;
        check(std::fabs(q.rise(0) - 1.0f) < 1e-5f && q.rise(1) < 1.0f, "glyph 0 has risen 1.0 at t 16; glyph 1 is 2 ticks behind");
    }
    // FUN_00585ff0's [ebp-0x14]: the sound the tail plays at the pickup.
    check(pickup_collect_se(0x48) == 7 && pickup_collect_se(0x49) == 7 && pickup_collect_se(0x4a) == 13 &&
          pickup_collect_se(0x4b) == 13 && pickup_collect_se(0x4c) == 14 && pickup_collect_se(0x4d) == 14 &&
          pickup_collect_se(0x42) == 88 && pickup_collect_se(0x78) == 88 && pickup_collect_se(0x72) == 88 &&
          pickup_collect_se(0x59) == 88,
          "collect SE: 7 / 13 / 14 for the Strength / Defense / MP Drops, 88 (p_takeitem) for the rest");
    std::printf("[pick] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
