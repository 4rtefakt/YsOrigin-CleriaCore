// cleria-bullettest — the GoXx bullet (src/cleria/bullet.hpp) against build/ai_spec/BULLETS.md:
// the orientation / homing helpers, worked example A (M510's disc, §5), the kill-flag park, the
// kind-2 homing window, the heal shot, the drip's bounce, the spawn's knock / side rules, the
// trail, Velagunder's bubble (kind 0x1e) and drool drop (kind 0x1f), Hugo's volley (kind 0x1b), the
// aimed kind 0x16, and XSO op 0x112's falling water probe (GoXx type 0x16).
// Usage: cleria-bullettest <assets dir>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <utility>
#include <vector>

#include "cleria/bullet.hpp"
#include "cleria/ef2.hpp"
#include "cleria/ef2_sim.hpp"

using namespace cleria;
using namespace cleria::bullet;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[bullet] %-66s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}
static bool near(float a, float b, float tol = 2e-4f) { return std::fabs(a - b) <= tol; }

static Vec3 row_mul(const Vec3& v, const ef2sim::M4& m) {
    return {v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0],
            v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1],
            v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2]};
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: cleria-bullettest <assets>\n"); return 2; }
    const std::filesystem::path enemy = std::filesystem::path(argv[1]) / "EFFECT" / "EFT" / "ENEMY";
    std::map<int, Ef2> efs;
    efs[317] = Ef2::load(enemy / "M_510_ATTACK01.EF2");
    efs[302] = Ef2::load(enemy / "M_300_FIRE01.EF2");
    efs[314] = Ef2::load(enemy / "M_720_HEAL01.EF2");
    efs[500] = Ef2::load(enemy / "M_380_02.EF2");
    efs[260] = Ef2::load(enemy / "M_640_ATTACK01.EF2");
    efs[0x1eb] = Ef2::load(enemy / "M_580_AWA2.EF2");
    for (auto& [id, ef] : efs) if (!ef.ok()) { std::fprintf(stderr, "missing EF2 %d\n", id); return 2; }
    auto ef2_of = [&](int id) -> const Ef2* {
        auto it = efs.find(id);
        return it == efs.end() ? nullptr : &it->second;
    };

    // ---- FUN_00458970 / FUN_004585c0 ----
    {
        const Vec3 F{0, 0, -1};
        const Vec3 ds[] = {{1, 0, 0}, {0, 0, 1}, {0.6f, 0.0f, 0.8f}, {0.36f, -0.48f, 0.8f}, {0, 1, 0}};
        bool ok = true;
        for (const Vec3& d : ds) {
            const Vec3 o = row_mul(F, look_rotation(d));
            ok = ok && near(o.x, d.x, 1e-5f) && near(o.y, d.y, 1e-5f) && near(o.z, d.z, 1e-5f);
        }
        check(ok, "look_rotation: the base forward (0,0,-1) lands on the direction");
        const Vec3 h0 = home({0, 0, 0}, {1, 0, 0}, {0, 0, 5}, 0.0f);
        check(near(h0.x, 1) && near(h0.z, 0), "home: max turn 0 leaves the direction");
        const Vec3 h1 = home({0, 0, 0}, {1, 0, 0}, {0, 0, 5}, 0.1f);
        check(near(h1.x, std::cos(0.1f), 1e-5f) && near(h1.z, std::sin(0.1f), 1e-5f) && near(h1.y, 0),
              "home: turns 0.1 rad toward a target 90 deg away");
        const Vec3 h2 = home({0, 0, 0}, {1, 0, 0}, {3, 3, 0}, 2.0f);
        check(near(h2.x, 0.70711f, 1e-4f) && near(h2.y, 0.70711f, 1e-4f), "home: a large max turn reaches the target (3-D)");
    }

    // ---- Hugo's shot, effect 0x82: rtype 3; x 1.35 the speed under his Extra version (EXTRA_VERSIONS.md §3) ----
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        // (any EF2 stands in for 130 PC2_DEF_ATTACK: the rule keys on the id)
        Bullets bs(&fx, [&](int id) -> const Ef2* { return id == 0x82 ? &efs[317] : ef2_of(id); });
        Spawn s;
        s.kind = 2; s.effect = 0x82; s.owner = 13; s.owner_monster = false; s.owner_atk = 20;
        s.pos = {0, 0, 0}; s.dir = {1, 0, 0}; s.speed = 0.3f; s.power = 2.0f; s.knock = 0.15f;
        const Bullet* n = bs.spawn(s);
        bs.set_hugo_extra(true);
        const Bullet* x = bs.spawn(s);
        s.effect = 317;
        const Bullet* o = bs.spawn(s);
        check(n && x && o && n->s.speed == 0.3f && n->fx->rtype == 3 && x->s.speed == 0.3f * 1.35f && x->fx->rtype == 3 &&
              o->s.speed == 0.3f, "effect 0x82: rtype 3; Hugo Extra flies x 1.35, other effects do not");
    }

    // ---- §5 worked example A: M510's disc, kind 1, effect 317 ----
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Spawn s;
        s.kind = 1; s.effect = 317; s.owner = 7; s.owner_atk = 20;
        s.pos = {0, 0, 0}; s.dir = {1, 0, 0}; s.speed = 0.06f; s.accel = -0.0008f; s.knock = 1.0f; s.f168 = 1.0f;
        Bullet* b = bs.spawn(s);
        check(b && b->side == 1 && b->fx && b->fx->atk == 20 && b->fx->rtype == 0, "spawn: side 1, owner ATK, rtype 0");
        Env env;
        env.player = {100, 0, 100};
        const std::map<int, float> table{{1, 0.06f}, {2, 0.1192f}, {3, 0.1776f}, {5, 0.2920f}, {10, 0.5640f},
                                         {20, 1.0480f}, {30, 1.4520f}, {40, 1.7760f}, {50, 2.0200f},
                                         {60, 2.1840f}, {75, 2.2800f}, {80, 2.2800f}};
        bool rows = true, nonneg = true;
        int destroyed_at = -1;
        for (int k = 1; k <= 120; ++k) {
            bs.update(env, nullptr);
            if (bs.list().empty()) { destroyed_at = k; break; }
            const Bullet& c = *bs.list()[0];
            nonneg = nonneg && c.s.speed >= 0.0f;
            auto it = table.find(k);
            if (it != table.end() && !near(c.s.pos.x, it->second, 5e-4f)) {
                std::printf("[bullet]   k %d x %.4f (want %.4f)\n", k, c.s.pos.x, it->second);
                rows = false;
            }
            fx.step();
            fx.sweep();
        }
        std::printf("[bullet]   disc destroyed at update %d\n", destroyed_at);
        check(rows, "example A: the displacement table (v0 0.06)");
        check(nonneg, "example A: speed never negative");
        check(destroyed_at >= 78 && destroyed_at <= 84, "example A: destroyed when its EF2 empties (k ~ 80)");
    }

    // ---- the kill flag parks the bullet; the matrix carries the effect ----
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Spawn s;
        s.kind = 1; s.effect = 317; s.dir = {0, 0, 1}; s.speed = 0.09f;
        Bullet* b = bs.spawn(s);
        Env env;
        for (int k = 0; k < 10; ++k) { bs.update(env, nullptr); fx.step(); fx.sweep(); }
        const float z10 = b->s.pos.z;
        check(near(fx.effects()[0]->M.m[3][2], z10, 1e-5f), "the effect follows the bullet matrix (mode 1)");
        b->fx->kill = true;
        bs.update(env, nullptr);
        const float z11 = b->s.pos.z;
        bs.update(env, nullptr);
        check(b->s.speed == 0.0f && b->s.pos.z == z11, "a raised kill flag parks it (speed 0, position frozen)");
    }

    // ---- kind 2: the homing window (M350's shot: period 1, 30..120) ----
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Spawn s;
        s.kind = 2; s.effect = 302; s.dir = {1, 0, 0}; s.speed = 0.01f; s.knock = 1.0f;
        s.f160 = 1; s.f164 = 0.0174533f; s.f168 = 1.0f; s.f16c = 30.0f; s.f170 = 120.0f;
        Bullet* b = bs.spawn(s);
        Env env;
        env.player = {0, 0, 50};
        bool still = true;
        for (int k = 0; k < 30; ++k) { bs.update(env, nullptr); still = still && b->s.dir.z == 0.0f; }
        check(still, "kind 2: no homing before the start age");
        bs.update(env, nullptr);
        check(b->s.dir.z > 0.0f, "kind 2: homing from the start age");
        check(fx.first_particle(b->fx)->hit.rtype == 30, "kind 2: +0x16c also overwrites the first particle's rtype");
        for (int k = 0; k < 95; ++k) bs.update(env, nullptr);
        const Vec3 d = b->s.dir;
        bs.update(env, nullptr);
        check(b->s.dir.x == d.x && b->s.dir.z == d.z && b->s.f160 == 0, "kind 2: off for good past the end age");
    }

    // ---- kind 0xb: the heal shot ----
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Spawn s;
        s.kind = 0xb; s.effect = 314; s.owner = 9; s.pos = {1, 0, 0}; s.dir = {-1, 0, 0}; s.speed = 0.05f;
        s.knock = 1.0f; s.f160 = 2; s.f164 = 0.15707964f; s.f168 = 1.0f; s.f16c = 50.0f; s.f170 = 30.0f;
        bs.spawn(s);
        Env env;
        env.player = {50, 0, 50}; env.player_hp = 100;
        env.actor = [](int id, Vec3* p, float* hp) { if (id != 9) return false; *p = {0, 0, 0}; *hp = 40; return true; };
        std::vector<Event> ev;
        int k = 0;
        for (; k < 40 && ev.empty(); ++k) bs.update(env, &ev);
        bool heal = false, se = false;
        for (const Event& e : ev) {
            heal = heal || (e.type == Event::kHeal && e.target == 9 && e.amount == 50.0f);
            se = se || (e.type == Event::kSound && e.id == 0xad);
        }
        std::printf("[bullet]   heal shot reached its owner at update %d\n", k);
        check(heal && se && bs.list()[0]->gone && bs.list()[0]->fx->kill,
              "kind 0xb: within 0.5 of its owner it heals by +0x16c, SE 0xad, kills");
        Spawn p = s;
        p.owner = 0; p.pos = {49.5f, 0, 50};
        bs.spawn(p);
        ev.clear();
        bs.update(env, &ev);
        bool ph = false;
        for (const Event& e : ev) ph = ph || (e.type == Event::kHeal && e.target == 0 && e.amount == 30.0f);
        check(ph, "kind 0xb: within 1.0 of the player it heals the player by +0x170");
    }

    // ---- kind 0x10: the drip falls, bounces x0.6 and rests ----
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Spawn s;
        s.kind = 0x10; s.effect = 500; s.owner = 3; s.owner_atk = 25; s.pos = {0, 1.0f, 0}; s.dir = {0, -1, 0};
        s.speed = 0.09f; s.knock = 1.0f; s.f168 = 1.0f;
        Bullet* b = bs.spawn(s);
        check(b->atk == 0.0f && b->side == 1 && b->fx->atk == 0.0f, "kind 0x10: the proxy's stat block (ATK 0), side 1");
        Env env;
        env.ray = [](const Vec3& f, const Vec3& d, int set) {
            RayHit r;
            if (set != 2 || d.y >= 0.0f || f.y < 0.0f || f.y + d.y > 0.0f) return r;
            const float t = f.y / -d.y;
            r.hit = true; r.point = {f.x + d.x * t, 0.0f, f.z + d.z * t}; r.normal = {0, 1, 0};
            return r;
        };
        for (int k = 0; k < 30; ++k) bs.update(env, nullptr);
        // 0.09 x 0.6 = 0.054 < 0.1: speed 0, but the step already built (0x599f33) still lands
        // (0x59a07d adds the pre-rest velocity), so it rests 0.054 above its snap point.
        std::printf("[bullet]   drip after the bounce: speed %.4f, y %.4f dir.y %.1f\n", b->s.speed, b->s.pos.y, b->s.dir.y);
        check(b->s.speed == 0.0f && b->s.dir.y > 0.0f && b->s.pos.y > 0.05f && b->s.pos.y < 0.065f,
              "kind 0x10: 0.09 x 0.6 < 0.1 on the floor: reflected, then at rest");
    }

    // ---- the spawn: a negative knock is an rtype code; the trail ----
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Spawn s;
        s.kind = 1; s.effect = 302; s.dir = {1, 0, 0}; s.speed = 0.05f; s.knock = -3.0f; s.power = -2.0f;
        s.f158 = 302; s.f15c = 3;
        Bullet* b = bs.spawn(s);
        check(b->fx->rtype == 2 && b->s.knock == 1.0f && b->fx->knock == 1.0f && b->fx->power == -2.0f,
              "knock -3: rtype 2, knock 1.0; power passes through (-2)");
        Env env;
        for (int k = 0; k < 9; ++k) bs.update(env, nullptr);
        check(fx.effects().size() == 4, "trail: a +0x158 puff every +0x15c ticks (ages 3, 6, 9)");
        Spawn m = s;
        m.owner_monster = false; m.cls = 2; m.knock = 1.0f;
        Bullet* pb = bs.spawn(m);
        check(pb->side == 0 && pb->fx->cls == 2 && pb->y190 == m.pos.y, "a player-side owner: side 0, the weapon class");
    }

    // ---- a wall: the die-on-wall hit particle raises the kill flag, parks the bullet and
    //      spawns its impact child (310 m_hanyoutama01 -> 236 m_hanyouhit01; EFFECTHIT §4.2/§5.5)
    {
        efs[310] = Ef2::load(enemy / "M_HANYOUTAMA01.EF2");
        efs[236] = Ef2::load(enemy / "M_HANYOUHIT01.EF2");
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        fx.set_lookup(ef2_of);
        fx.set_world([](const Vec3& f, const Vec3& d, Vec3* at) {     // the plane x = 1
            if (d.x <= 0.0f || f.x > 1.0f || f.x + d.x < 1.0f) return false;
            const float t = (1.0f - f.x) / d.x;
            *at = {1.0f, f.y + d.y * t, f.z + d.z * t};
            return true;
        });
        Bullets bs(&fx, ef2_of);
        Spawn s;
        s.kind = 1; s.effect = 310; s.dir = {1, 0, 0}; s.speed = 0.1f; s.knock = 1.0f;
        Bullet* b = bs.spawn(s);
        Env env;
        int killed_at = -1;
        for (int k = 1; k <= 40 && killed_at < 0; ++k) {
            bs.update(env, nullptr);
            fx.step();
            fx.sweep();
            if (b->fx->kill) killed_at = k;
        }
        bs.update(env, nullptr);
        const float x = b->s.pos.x;
        bs.update(env, nullptr);
        bool child = false;
        for (const auto& e : fx.effects()) child = child || e->id == 236;
        std::printf("[bullet]   wall contact at update %d, parked at x %.3f\n", killed_at, x);
        // The die-on-wall particles ride ahead of the bullet's origin (local -z is the flight
        // direction), so it parks short of the wall (BULLETS.md §10.3: placement untraced).
        check(killed_at > 0 && x > 0.5f && x < 1.0f && b->s.pos.x == x && b->s.speed == 0.0f,
              "die-on-wall: the kill flag parks the bullet short of the wall");
        check(child, "die-on-wall: the impact child (236) spawns");
    }

    // ---- kind 0x1e: Velagunder's bubble (FUN_0059d2e0; 323 bs_01_awa1, BULLETS.md §3.8) ----
    const std::filesystem::path bos = std::filesystem::path(argv[1]) / "EFFECT" / "EFT" / "BOS";
    efs[323] = Ef2::load(bos / "BS_01_AWA1.EF2");
    efs[325] = Ef2::load(bos / "BS_01_AWA_HIT.EF2");
    if (!efs[323].ok() || !efs[325].ok()) { std::fprintf(stderr, "missing EF2 323/325\n"); return 2; }
    // State 7's block (VELAGUNDER.md §5.7): f0 speed, f1 accel, f2 0.0025, f4 knock 5, f9, f10, f11.
    auto bubble_block = [](float f0, float f1, float f10) {
        float f[13] = {f0, f1, 0.0025f, 0, 5.0f, 0, 0, 0, 0, 1.2f, f10, 0.45f, 0};
        Spawn s = block_spawn(0x1e, 0x143, {0, 0, 0}, {1, 0, 0}, f);
        s.owner = 5; s.owner_atk = 30;
        return s;
    };
    {
        float raw[13] = {};
        std::uint32_t one = 0x3f800000u;
        std::memcpy(&raw[7], &one, 4);
        raw[4] = -3.0f; raw[10] = 2.5f;
        const Spawn s = block_spawn(1, 302, {}, {1, 0, 0}, raw);
        check(s.f160 == 0x3f800000u && s.knock == -3.0f && s.f16c == 2.5f,
              "block_spawn: the 13 dwords land verbatim (+0x160 keeps the float bits)");
    }
    {   // motion: straight (gravity unread), accel in the tail, the 0.04 floor; scale; clock
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Bullet* b = bs.spawn(bubble_block(0.1f, -0.002f, 1.5f));
        check(b && b->side == 1 && b->fx->knock == 5.0f && b->fx->atk == 30.0f, "kind 0x1e: spawn side 1, knock 5, owner ATK");
        Env env;
        env.player = {100, 0, 100};
        ef2sim::Rng draws, peek;                                   // the same seed: peek gives the words
        const std::uint32_t w1 = peek.next(), w2 = peek.next();
        env.rng = [&] { return draws.next(); };
        // x_k = 0.1k - 0.002 k(k-1)/2 while above 0.04 (k <= 30, x_30 = 2.13); then 0.04 a tick.
        const std::map<int, float> table{{1, 0.1f}, {2, 0.198f}, {10, 0.91f}, {30, 2.13f}, {31, 2.17f}, {40, 2.53f}};
        bool rows = true, flat = true;
        float sc1 = 0, sc60 = 0, sc90 = 0;
        int rem = -1, life = -1;
        for (int k = 1; k <= 90; ++k) {
            bs.update(env, nullptr);
            if (k == 1) {
                const ef2sim::Particle* p = fx.first_particle(b->fx);
                rem = p ? p->remaining : -1; life = p ? p->life : -1;
                sc1 = b->fx->scale_x;
            }
            if (k == 60) sc60 = b->fx->scale_x;
            if (k == 90) sc90 = b->fx->scale_x;
            flat = flat && b->s.pos.y == 0.0f && b->s.pos.z == 0.0f;
            auto it = table.find(k);
            if (it != table.end() && !near(b->s.pos.x, it->second, 5e-4f)) {
                std::printf("[bullet]   bubble k %d x %.4f (want %.4f)\n", k, b->s.pos.x, it->second);
                rows = false;
            }
            fx.step();
            fx.sweep();
        }
        check(rows, "kind 0x1e: the displacement table (0.1, accel -0.002, floor 0.04)");
        check(flat, "kind 0x1e: gravity (+0x14c = 0.0025) is never applied");
        check(near(sc1, 21.0f / 80.0f * 1.5f, 1e-6f) && sc60 == 1.5f && sc90 == 1.5f,
              "kind 0x1e: fx scale = (min(n, 60) + 20) / 80 * +0x16c");
        check(rem == int((w1 >> 17) % 40u) + 320 && life == int((w2 >> 17) % 40u) + 320 && draws.calls == 2,
              "kind 0x1e: first tick, root remaining / life = 320 + R%40 (2 draws)");
        int destroyed_at = -1;
        for (int k = 91; k <= 600 && destroyed_at < 0; ++k) {
            bs.update(env, nullptr);
            if (bs.list().empty()) { destroyed_at = k; break; }
            fx.step();
            fx.sweep();
        }
        std::printf("[bullet]   bubble destroyed at update %d (root %d)\n", destroyed_at, rem);
        check(destroyed_at > rem && destroyed_at <= rem + 14, "kind 0x1e: lives while its re-clocked root (and the 10-tick children) live");
    }
    {   // Hugo: 4 draws, 420 + R%60, floor 0.004, no homing
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Bullet* b = bs.spawn(bubble_block(0.003f, 0.0f, 1.0f));
        Env env;
        env.player = {0, 0, 10}; env.character = 2; env.difficulty = 1;
        ef2sim::Rng draws;
        env.rng = [&] { return draws.next(); };
        bs.update(env, nullptr);
        const ef2sim::Particle* p = fx.first_particle(b->fx);
        check(draws.calls == 4 && p && p->remaining >= 420 && p->remaining < 480 && p->life >= 420 && p->life < 480,
              "kind 0x1e Hugo: four draws, root 420 + R%60");
        check(near(b->s.pos.x, 0.004f, 1e-7f) && b->s.dir.z == 0.0f, "kind 0x1e Hugo: floor 0.004, no homing");
    }
    {   // homing: Hard and up, level, 0.005 rad a tick, ticks 1..149
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Bullet* b = bs.spawn(bubble_block(0.05f, 0.0f, 1.0f));
        Env env;
        env.player = {0, 3, 50}; env.difficulty = 1;
        bs.update(env, nullptr);
        check(near(b->s.dir.x, std::cos(0.005f), 1e-6f) && near(b->s.dir.z, std::sin(0.005f), 1e-6f) && b->s.dir.y == 0.0f,
              "kind 0x1e: Hard turns 0.005 rad toward the player, level");
        for (int k = 2; k < 150; ++k) bs.update(env, nullptr);
        const Vec3 d = b->s.dir;
        bs.update(env, nullptr);
        check(b->u194 == 150 && near(b->s.dir.x, d.x, 1e-6f) && near(b->s.dir.z, d.z, 1e-6f),
              "kind 0x1e: no turn from tick 150");
        Bullet* n = bs.spawn(bubble_block(0.05f, 0.0f, 1.0f));
        env.difficulty = 0;
        bs.update(env, nullptr);
        check(n->s.dir.z == 0.0f, "kind 0x1e: Normal does not home");
    }
    {   // the swing pops it: kill, burst 325 at 1.2, push along bubble->player, SE 0xc5; then
        // the floor keeps it drifting 0.04 a tick although the tail parks it at 0
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Bullet* b = bs.spawn(bubble_block(0.1f, 0.0f, 1.0f));
        Env env;
        env.player = {3, 0, 0};
        float seen_r = 0;
        env.swing = [&](const HitShape& v) { seen_r = v.radius; return true; };
        std::vector<Event> ev;
        bs.update(env, &ev);
        bool push = false, se = false, burst = false;
        for (const Event& e : ev) {
            push = push || (e.type == Event::kPush && near(e.push.x, 1.0f, 1e-6f) && e.push.z == 0.0f);
            se = se || (e.type == Event::kSound && e.id == 0xc5);
        }
        for (const auto& e : fx.effects()) burst = burst || (e->id == 0x145 && near(e->M.m[0][0], 1.2f, 1e-6f));
        check(b->fx->kill && seen_r == 1.25f && push && se && burst,
              "kind 0x1e: a swing on r 1.25 pops it (kill, burst 1.2, push, SE 0xc5)");
        const float x1 = b->s.pos.x;
        bs.update(env, nullptr);
        check(near(b->s.pos.x - x1, 0.04f, 1e-6f) && b->s.speed == 0.0f,
              "kind 0x1e: popped, it still drifts at the 0.04 floor (the tail zeroes speed)");
        Bullet* t = bs.spawn(bubble_block(0.1f, 0.0f, 1.0f));
        env.character = 3;
        ev.clear();
        bs.update(env, &ev);
        bool tp = false;
        for (const Event& e : ev) tp = tp || (e.type == Event::kPush && near(e.push.x, 0.75f * 1.0f, 1e-6f));
        check(t->fx->kill && tp, "kind 0x1e: Toal is pushed x0.75");
        Bullet* r = bs.spawn(bubble_block(0.1f, 0.0f, 1.0f));
        env.swing_rtype = 1;
        ev.clear();
        bs.update(env, &ev);
        check(r->fx->kill && ev.empty(), "kind 0x1e: a swing with rtype != 0 pops it without push or SE");
        Bullet* nm = bs.spawn(bubble_block(0.1f, 0.0f, 1.0f));
        env.difficulty = 2;
        bs.update(env, nullptr);
        check(!nm->fx->kill, "kind 0x1e: Nightmare bubbles cannot be popped");
    }
    {   // player effects in the pool: 0x66 (Yunica's sword) pops it at r 0.75; 0x82 (Hugo's
        // shot) is absorbed at r 0.1 and the bubble lives. (The EF2 under those ids here is 323,
        // only for its root hit armed at spawn; the record is then placed by hand.)
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Bullet* b = bs.spawn(bubble_block(0.05f, 0.0f, 1.0f));
        ef2sim::SpawnParams sp;
        sp.side = 0;
        ef2sim::Effect* shot = fx.spawn(0x82, efs[323], sp);
        ef2sim::Particle* sp_p = nullptr;
        for (const auto& p : fx.particles()) if (p->fx == shot && p->hit.armed) sp_p = p.get();
        Env env;
        env.player = {50, 0, 0};
        if (sp_p) { sp_p->hit.centre = {0.1f, 0, 0}; sp_p->hit.radius = 0.05f; }
        bs.update(env, nullptr);
        check(sp_p && shot->kill && sp_p->dying && !sp_p->hit.armed && !b->fx->kill,
              "kind 0x1e: an armed 0x82 particle within 0.1 is killed; the bubble lives");
        ef2sim::Effect* sword = fx.spawn(0x66, efs[323], sp);
        for (const auto& p : fx.particles())
            if (p->fx == sword && p->hit.armed) { p->hit.centre = b->s.pos; p->hit.radius = 0.5f; }
        bs.update(env, nullptr);
        bool burst = false;
        for (const auto& e : fx.effects()) burst = burst || (e->id == 0x145 && near(e->M.m[0][0], 0.9f, 1e-6f));
        check(b->fx->kill && burst, "kind 0x1e: an armed 0x66 particle on r 0.75 pops it (burst 0.9)");
    }
    {   // the mode-2 root hit lands on the player: kill flag, impact child 325
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        fx.set_lookup(ef2_of);
        Bullets bs(&fx, ef2_of);
        Bullet* b = bs.spawn(bubble_block(0.1f, 0.0f, 1.0f));
        Env env;
        env.player = {1.0f, 0, 0};
        float r = 0;
        auto on_hit = [&](ef2sim::Particle& p) {
            r = p.hit.radius;
            const float dx = p.hit.centre.x - env.player.x;
            return std::fabs(dx) < p.hit.radius + 0.3f;
        };
        int hit_at = -1;
        for (int k = 1; k <= 30 && hit_at < 0; ++k) {
            bs.update(env, nullptr);
            fx.step(on_hit);
            fx.sweep();
            if (b->fx->kill) hit_at = k;
        }
        bool child = false;
        for (const auto& e : fx.effects()) child = child || e->id == 325;
        std::printf("[bullet]   bubble hit the player at update %d (hit r %.3f)\n", hit_at, r);
        check(hit_at > 0 && child, "kind 0x1e: the root's mode-2 hit lands: kill flag, impact child 325");
    }

    // ---- kind 0x1f: the drool drop (inline 0x594c77; VELAGUNDER.md §5.9) ----
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        float f[13] = {0.013f, -0.0002f, 0, 0, 0, 0, 0, 0, 0, 0, 0.3f, 0, 0};
        Spawn s = block_spawn(0x1f, 0x143, {0, 5.0f, 0}, {0, -1, 0}, f);
        s.owner = 5;
        Bullet* b = bs.spawn(s);
        Env env;
        env.player = {100, 0, 100};
        // y_k = 5 - (0.013k - 0.0002 k(k-1)/2), straight down, no gravity
        const std::map<int, float> table{{1, 4.987f}, {2, 4.9742f}, {5, 4.937f}, {10, 4.879f}};
        bool rows = true, scale = true;
        int rem = -1, destroyed_at = -1;
        for (int k = 1; k <= 60; ++k) {
            bs.update(env, nullptr);
            if (bs.list().empty()) { destroyed_at = k; break; }
            if (k == 1) { const ef2sim::Particle* p = fx.first_particle(b->fx); rem = p ? p->remaining * 100 + p->life : -1; }
            scale = scale && b->fx->scale_x == 0.3f && b->fx->scale_z == 0.3f;
            auto it = table.find(k);
            if (it != table.end() && !near(b->s.pos.y, it->second, 2e-5f)) {
                std::printf("[bullet]   drool k %d y %.5f (want %.5f)\n", k, b->s.pos.y, it->second);
                rows = false;
            }
            fx.step();
            fx.sweep();
        }
        std::printf("[bullet]   drool destroyed at update %d\n", destroyed_at);
        check(rows, "kind 0x1f: the displacement table (0.013 down, accel -0.0002)");
        check(scale, "kind 0x1f: fx scale = +0x16c every tick");
        check(rem == 15 * 100 + 15, "kind 0x1f: first tick, root remaining = life = 15");
        check(destroyed_at > 15 && destroyed_at <= 28, "kind 0x1f: gone when the 15-tick root and its children end");
        float g[13] = {0.0004f, -0.0002f};
        Bullet* z = bs.spawn(block_spawn(0x1f, 0x143, {0, 1, 0}, {0, -1, 0}, g));
        for (int k = 0; k < 3; ++k) bs.update(env, nullptr);
        check(near(z->s.pos.y, 0.9994f, 1e-6f) && z->s.dir.y == 0.0f && z->s.speed == 0.0f,
              "kind 0x1f: at speed 0 the step zeroes dir (unlike INTEGRATE)");
    }

    // ---- kind 0x1b: Hugo's volley (inline 0x594bd3; effect 198 m_810_tama, BULLETS.md §3.12) ----
    efs[198] = Ef2::load(enemy / "M_810_TAMA.EF2");
    if (!efs[198].ok()) { std::fprintf(stderr, "missing EF2 198\n"); return 2; }
    {
        // M810's block at d 0: speed 0.225, knock 1.0, every other float 0.
        float f[13] = {0.225f, 0, 0, 0, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0};
        auto run = [&](int kind, int* life2, int* rem2, int* bare2, int* gone, bool* line) {
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            Spawn s = block_spawn(kind, 198, {0, 1, 0}, {1, 0, 0}, f);
            s.owner = 197; s.owner_atk = 40;
            Bullet* b = bs.spawn(s);
            ef2sim::SpawnParams sp;                                // a 198 that is not this bullet's
            sp.pos = {0, 1, 5};
            ef2sim::Effect* other = fx.spawn(198, efs[198], sp);
            Env env;
            env.player = {100, 0, 100};
            *line = true;
            for (int k = 1; k <= 200; ++k) {
                bs.update(env, nullptr);
                if (bs.list().empty()) { *gone = k; break; }
                if (k == 2) {
                    const ef2sim::Particle* p = fx.first_particle(b->fx);
                    const ef2sim::Particle* q = fx.first_particle(other);
                    *life2 = p ? p->life : -1; *rem2 = p ? p->remaining : -1; *bare2 = q ? q->life : -1;
                }
                *line = *line && near(b->s.pos.x, 0.225f * float(k), 1e-4f) && b->s.pos.y == 1.0f && b->s.pos.z == 0.0f;
                fx.step();
                fx.sweep();
            }
        };
        int l2 = 0, r2 = 0, o2 = 0, gone = -1, cl2 = 0, cr2 = 0, co2 = 0, cgone = -1;
        bool line = false, cline = false;
        run(0x1b, &l2, &r2, &o2, &gone, &line);
        run(1, &cl2, &cr2, &co2, &cgone, &cline);
        std::printf("[bullet]   0x1b: second update root life %d remaining %d (the other 198: %d); gone at %d"
                    " (kind 1: life %d, gone at %d)\n", l2, r2, o2, gone, cl2, cgone);
        check(l2 == 80 && r2 == 79, "kind 0x1b: the second update re-clocks the life-43 root to 80 (remaining 79)");
        check(o2 == 80 && co2 == 43, "kind 0x1b: ANY effect 198's life-43 particles are re-clocked (effect id, not fx)");
        check(cl2 == 43, "kind 1 with the same effect: no re-clock");
        check(line, "kind 0x1b: FUN_00597290, a straight line at 0.225 a tick");
        check(gone > 0 && cgone > 0 && gone - cgone >= 30, "kind 0x1b: outlives a kind-1 198 by 30+ updates (the 80-tick root)");
    }

    // ---- kind 8: the ground-hugging shot (FUN_005991f0; effect 270 m_230_taiki, BULLET_KINDS.md §4) ----
    efs[270] = Ef2::load(enemy / "M_230_TAIKI.EF2");
    if (!efs[270].ok()) { std::fprintf(stderr, "missing EF2 270\n"); return 2; }
    {
        // speed 0.3, knock -2 (rtype 1), no trail and no homing here: the handler alone.
        float f[13] = {0.3f, 0, 0, 0, -2.0f, 0, 0, 0, 0, 0, 0, 0, 0};
        float floor_y = 2.0f;
        Vec3 nrm{0, 1, 0};
        int rays = 0, draws = 0;
        Env env;
        env.player = {100, 0, 100};
        env.rng = [&] { ++draws; return 0u; };
        env.ray = [&](const Vec3& from, const Vec3& d, int set) {     // a plane at floor_y with the normal nrm
            RayHit r;
            if (set != 2) return r;
            ++rays;
            if (from.y >= floor_y && from.y + d.y <= floor_y) { r.hit = true; r.point = {from.x, floor_y, from.z}; r.normal = nrm; }
            return r;
        };
        auto one = [&](const Vec3& pos, const Vec3& dir, float f9, float g, Vec3* out_dir, float* mat_y) {
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            float ff[13];
            for (int k = 0; k < 13; ++k) ff[k] = f[k];
            ff[2] = g; ff[9] = f9;
            Bullet* b = bs.spawn(block_spawn(8, 270, pos, dir, ff));
            bs.update(env, nullptr);
            *out_dir = b->s.dir; *mat_y = b->mat.m[3][1];
            return b->s.pos;
        };
        Vec3 d{}; float my = 0;
        Vec3 p = one({0, 3.5f, 0}, {1, 0.5f, 0}, 0.0f, 0.0f, &d, &my);
        check(near(p.x, 0.3f) && p.y == 2.0f && p.z == 0.0f && d.y == 0.0f && d.x == 1.0f && near(my, 3.65f),
              "kind 8: moves dir x speed, snaps to the ground under it (f9 = 0), zeroes dir.y; the matrix keeps the pre-snap y");
        p = one({0, 3.5f, 0}, {1, 0, 0}, 0.5f, 0.0f, &d, &my);
        check(near(p.y, 2.5f), "kind 8: the hover height f9 x n.y over the hit point");
        nrm = {1, 0, 0};
        p = one({0, 3.5f, 0}, {1, 0, 0}, 0.0f, 0.0f, &d, &my);
        check(near(p.y, -6.5f), "kind 8: a wall / steep face (n.y <= 0.5) drops the shot 10 under, it is not killed");
        nrm = {0, 1, 0};
        floor_y = -50.0f;                                             // nothing within 2 below
        p = one({0, 3.5f, 0}, {1, 0, 0}, 0.0f, 0.5f, &d, &my);
        check(near(p.y, 3.495f, 1e-5f), "kind 8: no ground in reach: the fall is g x 0.01 a tick (not accumulated)");
        check(rays == 4 && draws == 0, "kind 8: one terrain ray a tick, no RNG draw");
    }

    // ---- kind 0x24: Nygtilger's plasma ball (FUN_0059fa30; effect 348 bs_02_plasma03b, BULLET_KINDS.md §5) ----
    efs[0x15c] = Ef2::load(bos / "BS_02_PLASMA03B.EF2");
    efs[0x15b] = Ef2::load(bos / "BS_02_THUNDER.EF2");
    if (!efs[0x15c].ok() || !efs[0x15b].ok()) { std::fprintf(stderr, "missing EF2 348 / 347\n"); return 2; }
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        float f[13] = {0, 0.01f, 0, 0, 2.0f, 0, 0, 0, 0, 0, 0, 0, 0};      // the caster's block: accel 0.01, knock 2
        Bullet* b = bs.spawn(block_spawn(0x24, 0x15c, {11, 5.7f, 0.8f}, {1, 0, 0}, f));
        b->carrier = 3; b->v1ac = {0, 2.7f, 0.8f}; b->t1b8 = {12, 10, 0};
        float seg_hp = 350.0f;
        int draws = 0, thunder = 0;
        std::vector<Event> ev;
        Env env;
        env.player = {15, 0, 0};
        env.rng = [&] { ++draws; return 0u; };
        env.carrier = [&](int id, ef2sim::M4* w, float* hp, float* atk) {
            if (id != 3) return false;
            *w = ef2sim::translation(11, 3, 0); *hp = seg_hp; *atk = 77.0f;
            return true;
        };
        env.ray = [&](const Vec3& from, const Vec3& d, int set) {         // the floor at y 0.5
            RayHit r;
            if (set == 2 && from.y >= 0.5f && from.y + d.y <= 0.5f) { r.hit = true; r.point = {from.x, 0.5f, from.z}; r.normal = {0, 1, 0}; }
            return r;
        };
        auto at = [&] { return Vec3{b->mat.m[3][0], b->mat.m[3][1], b->mat.m[3][2]}; };
        for (int i = 0; i < 59; ++i) bs.update(env, &ev);
        check(near(at().x, 11) && near(at().y, 5.7f) && near(at().z, 0.8f) && !b->d1d4 && b->n1d0 == 59,
              "kind 0x24: glued to its segment (v through the carrier's matrix) for the first 59 calls");
        check(b->s.pos.x == 11.0f && b->s.pos.y == 5.7f, "kind 0x24: +0x12c stays at the spawn point (only the matrix row moves)");
        bs.update(env, &ev);                                               // n = 60: t = 0
        check(near(at().y, 5.7f) && near(b->t1b8.x, 12.075f), "kind 0x24: n = 60 starts the lerp at A; T steps 0.075 toward the player");
        for (int i = 0; i < 120; ++i) bs.update(env, &ev);                 // n = 180: t = 0.5
        check(near(at().y, 5.7f + (10.0f - 5.7f) * 0.5f, 1e-3f), "kind 0x24: halfway to T at n = 180 ((n - 60) / 240)");
        for (int i = 0; i < 119; ++i) bs.update(env, &ev);                 // n = 299
        const Vec3 last = at();
        check(!b->d1d4 && last.y > 9.9f && ev.empty(), "kind 0x24: at T's height by n = 299, nothing struck yet");
        for (const auto& e : fx.effects()) thunder += e->id == 0x15b;
        bs.update(env, &ev);                                               // n = 300: the strike
        int thunder2 = 0;
        Vec3 tp{};
        for (const auto& e : fx.effects()) if (e->id == 0x15b) { ++thunder2; tp = {e->M.m[3][0], e->M.m[3][1], e->M.m[3][2]}; }
        check(thunder == 0 && thunder2 == 1 && near(tp.y, 4.0f) && near(tp.x, last.x, 1e-3f) && b->d1d4 && b->fx && b->fx->kill &&
              b->carrier == -1, "kind 0x24: n = 300 strikes — effect 0x15b at (x, hit.y + 3.5, z), the kill flag, done");
        check(ev.size() == 1 && ev[0].type == Event::kSound && ev[0].id == 221 && ev[0].pos.x == 11.0f && ev[0].pos.y == 5.7f,
              "kind 0x24: the strike's SE 221 (222, 223 by the shared count) plays at the spawn point");
        check(draws == 0, "kind 0x24: no RNG draw");
        // the carrier's death and the inner radius end it early
        Bullet* c = bs.spawn(block_spawn(0x24, 0x15c, {11, 5.7f, 0.8f}, {1, 0, 0}, f));
        c->carrier = 3; c->v1ac = {0, 2.7f, 0.8f}; c->t1b8 = {12, 10, 0};
        bs.update(env, &ev);
        const bool alive1 = !c->d1d4;
        seg_hp = 0.5f;
        bs.update(env, &ev);
        check(alive1 && c->d1d4 && c->fx->kill, "kind 0x24: the carrier's HP under 1 ends the ball");
        seg_hp = 350.0f;
        Bullet* d = bs.spawn(block_spawn(0x24, 0x15c, {3, 5, 0}, {1, 0, 0}, f));
        d->carrier = 9; d->t1b8 = {3, 10, 0};                               // no carrier: the row stays at the spawn point
        bs.update(env, &ev);
        check(d->d1d4, "kind 0x24: within 8.05 of the arena's centre it ends");
    }

    // ---- kind 0x23: Nygtilger's poison ball (FUN_0059e860; effect 343 bs_02_doku_ball, BULLET_KINDS.md §3) ----
    efs[0x157] = Ef2::load(bos / "BS_02_DOKU_BALL.EF2");
    efs[0x1ff] = Ef2::load(bos / "BS_02_DOKU.EF2");            // (the test only needs the poof to spawn; any EF2 does)
    if (!efs[0x157].ok() || !efs[0x1ff].ok()) { std::fprintf(stderr, "missing EF2 343 / the poof\n"); return 2; }
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        float f[13] = {0.0166666675f, 0, 0.0163333323f, 10.0f, 2.0f, 0, 0, 0, 0, 1.0f, 0, 0, 0};   // FUN_0051c8c0's block
        Spawn sp = block_spawn(0x23, 0x157, {15, 3, 0}, {0, 1, 0}, f);
        sp.owner_monster = true; sp.owner_atk = 98.0f;
        Bullet* b = bs.spawn(sp);
        b->carrier = 5;
        int draws = 0;
        bool swing = false;
        std::vector<Event> ev;
        Env env;
        env.player = {100, 1.075f, 100};
        env.rng = [&] { ++draws; return 0x00020000u; };                    // U = 1 / 64, I = 1: v = (-0.2421875, 1, -0.2421875)
        env.carrier = [&](int id, ef2sim::M4* w, float* hp, float* atk) {
            if (id != 5) return false;
            *w = ef2sim::translation(15, 2, 0); *hp = 0.0f; *atk = 77.0f;
            return true;
        };
        env.ray = [&](const Vec3& from, const Vec3& d, int set) {         // the room's floor mesh
            RayHit r;
            if (set == 2 && from.y >= -0.02f && from.y + d.y < -0.02f) {   // (2 cm under the plane y = 0 the ball rests on)
                const float t = (from.y + 0.02f) / -d.y;
                r.hit = true; r.point = {from.x + d.x * t, -0.02f, from.z + d.z * t}; r.normal = {0, 1, 0};
            }
            return r;
        };
        env.swing = [&](const HitShape&) { return swing; };
        for (int i = 0; i < 60; ++i) bs.update(env, &ev);
        const float l = std::sqrt(0.2421875f * 0.2421875f * 2 + 1.0f);
        check(draws == 180 && b->age == 60 && near(b->s.pos.x, 15) && near(b->s.pos.y, 3) && near(b->s.dir.y, 1.0f / l) &&
              near(b->s.dir.x, -0.2421875f / l) && near(b->s.speed, 21.0f / 60.0f) && !b->f17b,
              "kind 0x23: carried 60 ticks 1 above its segment, 3 draws a tick (180); leaves along the drawn cone at (I % 6 + 20) / 60");
        bs.update(env, &ev);
        check(draws == 180 && b->f17b && b->s.pos.y > 3.0f && b->u184 == 0, "kind 0x23: from age 60 it flies (no draw), the struck test on");
        int bounces = 0, rest = -1;
        float vy0 = b->s.dir.y;
        for (int i = 0; i < 500 && rest < 0; ++i) {
            bs.update(env, &ev);
            if (vy0 < 0.0f && b->s.dir.y > 0.0f) ++bounces;
            vy0 = b->s.dir.y;
            if (b->u184 == 2) rest = int(b->age);
        }
        check(bounces >= 2 && rest > 0 && b->s.pos.y == 0.65f && b->s.speed == 0.0f && draws == 180,
              "kind 0x23: it bounces off the floor (x 0.6 a bounce) and rests on y 0.65 inside the arena, no draw");
        // The player's body pushes a resting ball: knock channel 2 for the player (channel 1 zeroed), the ball wakes.
        env.player = {b->s.pos.x + 0.5f, 1.075f, b->s.pos.z};
        ev.clear();
        bs.update(env, &ev);
        bool pushed = false;
        for (const Event& e : ev) pushed |= e.type == Event::kPush && e.id == 1 && e.push.x > 0.024f && near(e.push.z, 0.0f, 1e-3f);
        check(pushed && b->u184 == 1 && near(b->s.speed, 0.075f) && b->s.dir.x < 0.0f && b->cd198 == 1,
              "kind 0x23: the player's body pushes it (k = max(0.025, 1.05 v)): it rolls away at 0.075, cooldown 2");
        env.player = {100, 1.075f, 100};
        for (int i = 0; i < 3; ++i) bs.update(env, &ev);
        // The player bats it: two draws, away from the player with a lift, 31..34 / 60, cooldown 12.
        env.player = {b->s.pos.x - 1.0f, 1.075f, b->s.pos.z};
        swing = true;
        ev.clear();
        const int d0 = draws;
        bs.update(env, &ev);
        swing = false;
        bool se = false;
        for (const Event& e : ev) se |= e.type == Event::kSound && e.id == 0xb9;
        check(draws - d0 == 2 && se && b->n19c == 1 && b->cd198 == 12 && b->s.dir.x > 0.0f && b->s.dir.y > 0.0f &&
              b->s.speed > 0.5f && b->s.gravity == 0.0113333f,
              "kind 0x23: batted = SE 185, 2 draws, relaunched away with a lift at 31-34 / 60, gravity 0.0113333, cooldown 12");
        // Age 590: the poof 0x1ff and SE 219; past it the ball is frozen.
        env.player = {100, 1.075f, 100};
        while (b->age < 590) bs.update(env, &ev);
        ev.clear();
        bs.update(env, &ev);
        int poof = 0;
        bool se219 = false;
        for (const auto& e : fx.effects()) poof += e->id == 0x1ff;
        for (const Event& e : ev) se219 |= e.type == Event::kSound && e.id == 0xdb;
        const Vec3 at590 = b->s.pos;
        bs.update(env, &ev);
        check(poof == 1 && se219 && !b->f17b && b->s.pos.x == at590.x && b->s.pos.y == at590.y,
              "kind 0x23: at age 590 the poof (effect 0x1ff) and SE 219; after it the ball is frozen, the struck test off");
        // Flag 221 (the boss is dead) kills a ball; the pillar pushes one out to r 8.65.
        Bullet* c = bs.spawn(sp);
        c->carrier = -1;
        for (int i = 0; i < 61; ++i) bs.update(env, &ev);
        env.flag221 = true;
        bs.update(env, &ev);
        check(c->fx && c->fx->kill, "kind 0x23: flag 221 kills the ball");
        env.flag221 = false;
        Spawn sq = sp; sq.pos = {3, 5, 0};
        Bullet* q = bs.spawn(sq);
        q->carrier = -1;
        for (int i = 0; i < 61; ++i) bs.update(env, &ev);
        check(near(std::sqrt(q->s.pos.x * q->s.pos.x + q->s.pos.z * q->s.pos.z), 8.65f, 1e-3f),
              "kind 0x23: inside the pillar it is put out on the rim (r 8.65)");
    }

    // ---- kind 0x16: the aimed effect (inline 0x594b51; effect 184 PC3_mazin_sitatuki, BULLETS.md §3.13) ----
    const std::filesystem::path eft = std::filesystem::path(argv[1]) / "EFFECT" / "EFT";
    efs[184] = Ef2::load(eft / "PC" / "PC03" / "PC3_MAZIN_SITATUKI.EF2");
    efs[68] = Ef2::load(eft / "COMMON" / "Z_MIZU_SIBUKI_JYANPU.EF2");
    if (!efs[184].ok() || !efs[68].ok()) { std::fprintf(stderr, "missing EF2 184 / 68\n"); return 2; }
    {
        // FUN_0059bbd0's block (0x59c151..0x59c1af): speed / accel / gravity 0, power 10, knock 1.
        float f[13] = {0, 0, 0, 10.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0};
        // `followed` = the leading updates in which dir aimed at the moving player; `gone` = the
        // update at which the bullet was destroyed (its effect ended), or -1.
        auto run = [&](int effect, int difficulty, bool* still, bool* holds, int* followed, int* gone) {
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            Spawn s = block_spawn(0x16, effect, {0, 2, 0}, {1, 0, 0}, f);
            s.owner = 9; s.owner_atk = 12;
            Bullet* b = bs.spawn(s);
            Env env;
            env.difficulty = difficulty;
            *still = b && b->fx->power == 10.0f && b->side == 1;
            *followed = 0;
            *holds = true;
            *gone = -1;
            for (int k = 0; k < 120 && b; ++k) {
                env.player = {std::cos(0.1f * float(k)) * 5.0f, 0.5f, std::sin(0.1f * float(k)) * 5.0f};
                const Vec3 before = b->s.dir;
                bs.update(env, nullptr);
                if (bs.list().empty()) { *gone = k + 1; break; }
                Vec3 t = env.player;
                if (difficulty == 0) t.y = 1.0f;
                const Vec3 to{t.x - 0.0f, t.y - 2.0f, t.z - 0.0f};
                const float l = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
                const bool aimed = near(b->s.dir.x, to.x / l, 1e-4f) && near(b->s.dir.y, to.y / l, 1e-4f) &&
                                   near(b->s.dir.z, to.z / l, 1e-4f);
                if (aimed && *followed == k) ++*followed;
                if (k >= *followed && (b->s.dir.x != before.x || b->s.dir.z != before.z)) *holds = false;
                *still = *still && b->s.pos.x == 0.0f && b->s.pos.y == 2.0f && b->s.pos.z == 0.0f &&
                         b->mat.m[3][0] == 0.0f && b->mat.m[3][1] == 2.0f && b->mat.m[3][2] == 0.0f;
                fx.step();
                fx.sweep();
            }
        };
        // The window, on a longer-lived effect (317, ~80 ticks): the handler never reads the id.
        bool st0, ay0, st1, ay1, st2, ay2, sr, ar;
        int n0, n1, n2, nr, g0, g1, g2, gr;
        run(317, 0, &st0, &ay0, &n0, &g0);
        run(317, 1, &st1, &ay1, &n1, &g1);
        run(317, 2, &st2, &ay2, &n2, &g2);
        std::printf("[bullet]   0x16: dir follows the player for %d / %d / %d updates (Normal / Hard / Nightmare)\n",
                    n0, n1, n2);
        check(n0 == 30 && n1 == 38 && n2 == 46,
              "kind 0x16: one-step aim (2 pi; Normal at y 1.0) while age < 0x2e - {16,8,0}[dif]");
        check(ay0 && ay1 && ay2, "kind 0x16: after the window dir holds (no tail homing)");
        check(st0 && st1 && st2, "kind 0x16: power 10, side 1, never moves (kind 0's matrix reset)");
        // The real effect 184 ends before the Hard / Nightmare windows close.
        run(184, 2, &sr, &ar, &nr, &gr);
        std::printf("[bullet]   0x16 + effect 184 (Nightmare): aims %d updates, gone at update %d\n", nr, gr);
        check(sr && gr > 0 && nr == gr - 1, "kind 0x16 + 184: gone with its effect, aiming to the end on Nightmare");
    }

    // ---- GoXx type 0x16: XSO op 0x112 WaterWallHitchk's probe (FUN_0058fa00, BULLETS.md §3.14) ----
    {
        // A floor at y = 3.2: the terrain ray (set 2) hits when the segment reaches it.
        auto floor_ray = [](const Vec3& from, const Vec3& d, int set) {
            RayHit r;
            r.hit = set == 2 && from.y + d.y <= 3.2f;
            if (r.hit) { r.point = {from.x, 3.2f, from.z}; r.normal = {0, 1, 0}; }
            return r;
        };
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Env env;
        env.ray = floor_ray;
        env.player = {50, 3, 50};                                  // out of reach
        std::vector<Event> ev;
        WaterProbe* w = bs.spawn_water_probe({34, 25, 97}, 0.8f, 0.4f);
        float y = 25.0f;
        bool fall = true;
        int gone_at = -1;
        for (int k = 1; k <= 200; ++k) {
            const bool will_hit = y - 0.4f <= 3.2f;
            bs.update(env, &ev);
            if (bs.probes().empty()) { gone_at = k; break; }
            y -= 0.4f;
            fall = fall && !will_hit && w->pos.y == y && w->falls == std::uint32_t(k) && w->pos.x == 34.0f;
        }
        std::printf("[bullet]   probe: lands after %d falls (y %.2f)\n", gone_at - 1, y);
        check(fall && gone_at == 55 && ev.empty(), "probe: falls 0.4 a tick until the terrain ray meets the floor, silently");

        // The player under the column: SE 0x266 + effect 68 at the player, no push, the probe ends.
        ev.clear();
        env.player = {34.3f, 3.1f, 97.0f};
        bs.spawn_water_probe({34, 25, 97}, 0.8f, 0.4f);
        int touch_at = -1;
        for (int k = 1; k <= 200; ++k) {
            bs.update(env, &ev);
            if (bs.probes().empty()) { touch_at = k; break; }
        }
        const ef2sim::Effect* splash = nullptr;
        for (const auto& e : fx.effects()) if (e->id == 68) splash = e.get();
        // y_k = 25 - 0.4k; in reach when (y - 3.1)^2 + 0.09 < 0.64, i.e. y < 3.84: k = 53 (y 3.8).
        std::printf("[bullet]   probe: touches the player at update %d\n", touch_at);
        check(touch_at == 54 && ev.size() == 1 && ev[0].type == Event::kSound && ev[0].id == 0x266 &&
                  ev[0].pos.x == 34.3f && ev[0].pos.y == 3.1f,
              "probe: in reach -> SE 0x266 at the player, no push event");
        check(splash && splash->side == 0 && splash->power == 0.0f && splash->knock == 1.0f && splash->atk == 0.0f &&
                  splash->cls == -1 && splash->rtype == 0,
              "probe: effect 68 at the player: side 0, power 0, knock 1, ATK 0");
        bool no_hit = true;
        for (const auto& p : fx.particles()) if (p->fx == splash && p->hit.armed) no_hit = false;
        check(no_hit, "probe: the splash has no armed hit particle (EF2 68 has no hit emitter)");

        // No floor, out of reach: the 601st fall ends it.
        env.ray = nullptr;
        env.player = {50, 3, 50};
        ev.clear();
        bs.spawn_water_probe({34, 25, 97}, 0.8f, 0.4f);
        int cap_at = -1;
        for (int k = 1; k <= 700; ++k) {
            bs.update(env, &ev);
            if (bs.probes().empty()) { cap_at = k; break; }
        }
        check(cap_at == 601 && ev.empty(), "probe: no floor -> gone on the 601st fall (+0x134 > 0x258)");

        // Born dead: radius 0 or step 0.
        bs.spawn_water_probe({0, 0, 0}, 0.0f, 0.4f);
        bs.spawn_water_probe({0, 0, 0}, 0.8f, 0.0f);
        const bool dead = bs.probes().size() == 2 && bs.probes()[0]->gone && bs.probes()[1]->gone;
        bs.update(env, &ev);
        check(dead && bs.probes().empty() && ev.empty(), "probe: radius 0 or step 0 is born dead (FUN_0058f9a0)");
    }

    // ---- kind 0x2a: the boomerang (build/ai_spec/M640.md §7 worked example) ----
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Spawn s;
        s.kind = 0x2a; s.effect = 260; s.owner = 68; s.pos = {0, 0, 0}; s.dir = {1, 0, 0};
        s.speed = 0.35f; s.accel = -0.0035f; s.knock = 1.0f; s.f168 = 0.3f; s.f16c = 0.1f; s.f170 = 15.0f;
        Bullet* b = bs.spawn(s);
        Env env;
        env.player = {50, 0, 50};
        env.actor = [](int id, Vec3* p, float* hp) { if (id != 68) return false; *p = {0, 0, 0}; *hp = 40; return true; };
        int turned = -1, caught = -1;
        float far_x = 0.0f;
        for (int k = 0; k < 260 && caught < 0 && b; ++k) {
            bs.update(env, nullptr);
            far_x = std::max(far_x, b->s.pos.x);
            if (turned < 0 && b->u184 == 1) turned = int(b->age);
            if (b->fx && b->fx->kill) caught = int(b->age);
        }
        std::printf("[bullet]   boomerang: turned at age %d after %.2f u, caught at age %d\n", turned, far_x, caught);
        check(b && turned >= 100 && turned <= 101 && std::fabs(far_x - 17.67f) < 0.1f,
              "kind 0x2a: out at 0.35 - 0.0035/tick, turns at age 100 after 17.67 u");
        check(caught >= 190 && caught <= 205, "kind 0x2a: snaps home, caught within 0.5 of its owner near age 199 (FUN_0047a7c0)");
    }

    // ---- kind 7: the weave (M740's bubbles; BULLETS.md §3.7, M740.md §3.6) ----
    {
        ef2sim::Rng rng;
        ef2sim::System fx(&rng);
        Bullets bs(&fx, ef2_of);
        Spawn s;
        s.kind = 7; s.effect = 0x1eb; s.pos = {0, 0.5f, 0}; s.dir = {1, 0, 0}; s.speed = 0.15f; s.power = 1.5f;
        s.knock = 20.0f; s.f168 = 3.5f; s.f16c = 5.0f;
        Bullet* b = bs.spawn(s);
        Env env;
        env.player = {100, 0, 100};
        float side30 = 0, base30 = 0;
        for (int k = 0; k < 31 && b; ++k) {
            bs.update(env, nullptr);
            if (b->age == 31) { side30 = b->mat.m[3][2]; base30 = b->s.pos.x; }
        }
        std::printf("[bullet]   weave: age 30 base x %.3f, side z %.3f\n", base30, side30);
        // age 30: phi = 3 * 30 = 90 deg -> off 5.0 along side = (dir.z, 0, -dir.x) = (0, 0, -1)
        check(b && std::fabs(base30 - 0.15f * 31.0f) < 1e-3f && std::fabs(side30 + 5.0f) < 1e-3f,
              "kind 7: base at 0.15 a tick, the matrix 5.0 to the side at age 30 ((int)3.5 deg x age)");
    }

    // ---- kind 0x21: the boss06 breath (build/boss_re/ZAVA_EF.md §F.2 case 385, handler 0x594e1e) ----
    {
        const std::filesystem::path bos = std::filesystem::path(argv[1]) / "EFFECT" / "EFT" / "BOS";
        efs[385] = Ef2::load(bos / "BS_06_FIRE.EF2");
        check(efs[385].ok(), "kind 0x21: EF2 385 bos\\bs_06_fire loads");
        // The block the heads push: speed 0.18, accel -0.001, gravity 0.0005, power 3.0, knock 3.0, f9 1.0, f10 15.0.
        const float f[13] = {0.18f, -0.001f, 0.0005f, 3.0f, 3.0f, 0, 0, 0, 0.0f, 1.0f, 15.0f, 0.0f, 0.0f};
        auto fire = [&](Bullets& bs) {
            Spawn s = block_spawn(0x21, 385, {0, 5, 0}, {1, 0, 0}, f);
            s.owner_atk = 232.0f;
            return bs.spawn(s);
        };
        {   // free flight: v = dir * speed - (0, g, 0), dir re-derived, then speed += accel
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            Bullet* b = fire(bs);
            Env env;
            env.player = {100, 0, 100};
            int rays = 0;
            env.ray = [&](const Vec3&, const Vec3&, int set) { rays += set == 2; return RayHit{}; };
            Vec3 pos{0, 5, 0}, dir{1, 0, 0};
            float speed = 0.18f;
            bool ok = b != nullptr;
            int rays16 = -1;
            for (int k = 0; k < 20 && b; ++k) {
                bs.update(env, nullptr);
                const Vec3 v{dir.x * speed, dir.y * speed - 0.0005f, dir.z * speed};
                const float l = float(std::sqrt(double(v.y * v.y + v.x * v.x + v.z * v.z)));
                dir = {v.x / l, v.y / l, v.z / l};
                pos = {pos.x + v.x, pos.y + v.y, pos.z + v.z};
                speed = l + -0.001f;
                ok = ok && near(b->s.pos.x, pos.x, 1e-5f) && near(b->s.pos.y, pos.y, 1e-6f) && near(b->s.speed, speed, 1e-6f);
                if (k == 15) rays16 = rays;                    // ages 0..15 stepped: none probes (age > 15.0 is false)
            }
            check(ok && b && b->s.dir.y < 0.0f && b->age == 20, "kind 0x21: gravity 0.0005 bends the heading down each tick; speed += -0.001");
            check(rays16 == 0 && rays == 4, "kind 0x21: the terrain probe (set 2) runs only once age > f10 = 15 (ages 16..19)");
            check(b && b->atk == 232.0f && b->fx && b->fx->power == 3.0f && b->side == 1 && !b->fx->kill,
                  "kind 0x21: the effect is armed with the owner's ATK 232, power 3.0, side 1; no wall, no kill");
        }
        {   // a wall after age 15 kills the effect (fx+0x1d4 = 1) and the tail parks the bullet; f9 = 0 never probes
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            Bullet* b = fire(bs);
            Spawn s0 = block_spawn(0x21, 385, {0, 5, 0}, {1, 0, 0}, f);
            s0.f168 = 0.0f;
            Bullet* nb = bs.spawn(s0);
            Env env;
            env.player = {100, 0, 100};
            env.ray = [&](const Vec3& from, const Vec3& d, int) {   // a wall at x = 1.0: in reach from the first tick
                RayHit r; r.hit = from.x + d.x >= 1.0f; r.point = from; r.normal = {-1, 0, 0}; return r;
            };
            int killed = -1;
            for (int k = 0; k < 30 && b && nb && killed < 0; ++k) {
                bs.update(env, nullptr);
                if (b->fx && b->fx->kill) killed = int(b->age);
            }
            check(killed == 17 && b && b->s.speed == 0.0f, "kind 0x21: a wall is ignored until age 16, then the effect is killed and the bullet parks");
            check(nb && nb->fx && !nb->fx->kill, "kind 0x21: f9 (+0x168) == 0 never probes the terrain");
        }
    }

    // ---- boss07 (Dalles): kinds 0x25 / 0x26 / 0x27, the F shot's struck test, the impact specials
    //      (build/boss_re/DALLES_CD1.md §4.2 / §5.5, DALLES_D23.md §10 / §16.1 / §16.2) ----
    {
        const std::filesystem::path bos = std::filesystem::path(argv[1]) / "EFFECT" / "EFT" / "BOS";
        efs[54] = Ef2::load(bos / "BS_07_HORMINGTOP.EF2");
        efs[53] = Ef2::load(bos / "BS_07_HORMINGBODY.EF2");
        efs[55] = Ef2::load(bos / "BS_07_HORMINGHIT.EF2");
        efs[398] = Ef2::load(bos / "BS_07_INSEKI.EF2");
        efs[397] = Ef2::load(bos / "BS_07_INSEKI_HIT.EF2");
        efs[399] = Ef2::load(bos / "BS_07_INSEKI_OBI.EF2");
        efs[409] = Ef2::load(bos / "BS_07_BARAMAKI_B.EF2");
        efs[403] = Ef2::load(bos / "BS_07_TAMA_C.EF2");
        efs[402] = Ef2::load(bos / "BS_07_TAMA_B.EF2");
        efs[404] = Ef2::load(bos / "BS_07_TAMA_B_HIT.EF2");
        bool loaded = true;
        for (int id : {54, 53, 55, 398, 397, 399, 409, 403, 402, 404}) loaded = loaded && efs[id].ok();
        check(loaded, "boss07: the EF2s 53 / 54 / 55 / 397..399 / 402..404 / 409 load");
        auto ib = [](std::uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; };
        auto count_fx = [](ef2sim::System& fx, int id) {
            int n = 0;
            for (const auto& f : fx.effects()) n += f->id == id;
            return n;
        };
        {   // kind 0x25, the option's block (FUN_00546900): straight up, homing ages 2..60 every 2nd tick at <= 17 deg
            // toward the packed point sunk by age^2 / 300, the trail 53 every 2nd tick with its pitch, rtype 2.
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            const std::uint32_t packed = (std::uint32_t(std::uint8_t(int(20.0f + 128.0f))) << 16) |
                                         (std::uint32_t(std::uint8_t(int(1.0f + 128.0f))) << 8) |
                                         std::uint32_t(std::uint8_t(int(-6.0f + 128.0f)));
            const float f[13] = {0.8f, -1e-5f, 0.0f, -5.0f, 1.0f, ib(53), ib(2), ib(2), 0.29670596f, 1.0f, 2.0f, 60.0f, ib(packed)};
            Spawn s = block_spawn(0x25, 54, {0, 3, 0}, {0, 1, 0}, f);
            s.owner_atk = 255.0f;
            Bullet* b = bs.spawn(s);
            check(b && b->fx && b->fx->status == 8 && b->fx->power == -5.0f, "kind 0x25: effect 54 is armed with status 8 (heavy), power -5");
            Env env;
            env.player = {-50, 0, -50};                            // not the target: the aim is the packed point
            int noboss = 0, plain = 0;
            bool wall = false;
            env.ray = [&](const Vec3&, const Vec3&, int) { ++plain; return RayHit{}; };
            env.ray_no_boss = [&](const Vec3& from, const Vec3& d, int set) {
                noboss += set == 2;
                RayHit r; r.hit = wall && from.y + d.y <= 0.0f; r.point = from; r.normal = {0, 1, 0}; return r;
            };
            std::vector<Event> ev;
            bool straight = true, turned = true, rt = true;
            Vec3 dir{0, 1, 0}, pos{0, 3, 0};
            float speed = 0.8f;
            int trails = 0;
            for (int k = 0; k < 30 && b; ++k) {
                const std::uint32_t age = b->age;
                const int before = count_fx(fx, 53);
                bs.update(env, &ev);
                // the reference: move with the old dir / speed, then the tail's homing at even ages 2..60
                pos = {pos.x + dir.x * speed, pos.y + dir.y * speed, pos.z + dir.z * speed};
                if (age >= 2 && age <= 60 && age % 2 == 0) {
                    const Vec3 t{20.0f, 1.0f - float(age * age) / 300.0f, -6.0f};
                    const Vec3 nd = home(pos, dir, t, 0.29670596f);
                    const float turn = std::acos(std::min(1.0f, nd.x * dir.x + nd.y * dir.y + nd.z * dir.z));
                    turned = turned && turn <= 0.29670596f + 1e-4f;
                    dir = nd;
                } else if (age < 2) {
                    straight = straight && b->s.dir.x == 0.0f && b->s.dir.y == 1.0f && b->s.dir.z == 0.0f;
                }
                speed += -1e-5f;
                turned = turned && near(b->s.pos.x, pos.x, 2e-3f) && near(b->s.pos.y, pos.y, 2e-3f) && near(b->s.pos.z, pos.z, 2e-3f);
                if (count_fx(fx, 53) > before) {
                    ++trails;
                    turned = turned && b->age % 2 == 0;            // the age after the ++
                }
                if (const ef2sim::Particle* p = fx.first_particle(b->fx)) rt = rt && p->hit.rtype == 2;
                fx.step(); fx.sweep();
            }
            check(straight, "kind 0x25: no homing before age 2 (f10): straight up");
            check(turned && b && b->s.dir.x > 0.5f, "kind 0x25: homes on the packed point (y sinking by age^2 / 300), <= 17 deg every 2nd tick");
            check(trails == 15, "kind 0x25: a trail 53 every 2nd tick (f6 = 2)");
            check(rt, "kind 0x25: the first particle's rtype is forced to (int)f10 = 2 every tick");
            check(noboss == 30 && plain == 0 && ev.empty(), "kind 0x25: the terrain probe runs every tick with the boss meshes skipped (f9 != 0)");
            // the ground: SE 327, the kill flag, y 0.5, the burst 55 (power -5, rtype 3, status 8), the shake
            wall = true;
            if (b) { b->s.pos.y = 0.3f; b->s.dir = {0, -1, 0}; }
            bs.update(env, &ev);
            const ef2sim::Effect* burst = nullptr;
            for (const auto& fe : fx.effects()) if (fe->id == 55) burst = fe.get();
            bool se = false, shake = false;
            for (const Event& e : ev) {
                se = se || (e.type == Event::kSound && e.id == 327);
                shake = shake || (e.type == Event::kShake && e.id == 10 && e.push.x == 0.15f && e.push.y == 0.15f && e.push.z == 0.0f);
            }
            check(b && b->fx && b->fx->kill && b->s.speed == 0.0f && se, "kind 0x25: the floor: SE 327, the kill flag, the bullet parks");
            check(burst && burst->power == -5.0f && burst->knock == 1.0f && burst->rtype == 3 && burst->status == 8 &&
                  burst->atk == 255.0f && burst->side == 1 && near(burst->M.m[3][1], 0.5f),
                  "kind 0x25: the burst 55 at y 0.5: power -5, knock 1, rtype 3, status 8, the owner's ATK");
            check(shake, "kind 0x25: Shake(10, 0.15, 0.15, 0) when no shake runs");
            // ... and none while one runs
            ef2sim::System fx2(&rng);
            Bullets bs2(&fx2, ef2_of);
            Bullet* b2 = bs2.spawn(s);
            env.shake_frames = 5;
            ev.clear();
            if (b2) { b2->s.pos.y = 0.3f; b2->s.dir = {0, -1, 0}; }
            bs2.update(env, &ev);
            bool shake2 = false;
            for (const Event& e : ev) shake2 = shake2 || e.type == Event::kShake;
            check(b2 && b2->fx->kill && !shake2, "kind 0x25: no new shake while 0x76a5ac != 0");
        }
        {   // kind 0x26, N_SHOOT_L pattern B: homing from age 40 at f8, level; 12 deg at age 60 then + 1 deg to 36;
            // the dive within 0.3 of the player in XZ.
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            const float turn0 = 6.0f * 0.017453292f;
            const float f[13] = {0.045f, 0.004f, 0.0f, -2.0f, 1.0f, ib(399), ib(10), ib(1), turn0, 1.0f, 40.0f, 999.0f, 0.0f};
            Bullet* b = bs.spawn(block_spawn(0x26, 398, {0, 6, 0}, {1, 0, 0}, f));
            Env env;
            env.player = {0, 0, 40};
            int rays = 0;
            env.ray = [&](const Vec3&, const Vec3&, int) { ++rays; return RayHit{}; };
            bool pre = true, level = true, ramp = true;
            float t60 = 0, t61 = 0, t90 = 0;
            for (int k = 0; k < 91 && b; ++k) {
                bs.update(env, nullptr);
                if (b->age <= 40) pre = pre && near(b->s.dir.x, 1.0f, 1e-6f) && b->s.dir.z == 0.0f;   // ages 0..39 stepped
                level = level && b->s.dir.y == 0.0f;
                if (b->age == 60) t60 = b->s.f164;
                if (b->age == 61) t61 = b->s.f164;
                if (b->age == 62) ramp = ramp && near(b->s.f164, 0.20943952f + 0.017453292f, 1e-6f);
                if (b->age == 91) t90 = b->s.f164;
                fx.step(); fx.sweep();
            }
            check(pre && b && b->s.dir.z > 0.9f, "kind 0x26: flies along the hand until age 40 (f10), then curves onto the player");
            check(level, "kind 0x26: the homing target's y is the bullet's (level)");
            check(near(t60, turn0, 1e-6f) && near(t61, 0.20943952f, 1e-6f) && ramp && t90 >= 0.62831855f && t90 < 0.62831855f + 0.0175f,
                  "kind 0x26: max turn f8 until age 59, 12 deg at age 60, + 1 deg a tick up to 36 deg");
            check(rays == 0, "kind 0x26: f9 = 1 but no terrain probe (kind 0x25 only)");
            if (b) { b->s.pos = {0.1f, 6.0f, 39.9f}; }
            bs.update(env, nullptr);
            check(b && b->s.dir.x == 0.0f && b->s.dir.y == -1.0f && b->s.dir.z == 0.0f && b->s.f160 == 0 && b->s.f170 == 0.0f &&
                  b->s.f16c == 0.0f, "kind 0x26: within 0.3 of the player in XZ: the dive (0, -1, 0), homing off");
            // too old: the same dive
            ef2sim::System fx2(&rng);
            Bullets bs2(&fx2, ef2_of);
            Bullet* o = bs2.spawn(block_spawn(0x26, 398, {0, 6, 0}, {1, 0, 0}, f));
            if (o) { o->age = 220; o->s.accel = 0.0f; }
            env.player = {500, 0, 500};
            bs2.update(env, nullptr);
            const bool not220 = o && o->s.dir.y == 0.0f;
            bs2.update(env, nullptr);
            check(not220 && o && o->s.dir.y == -1.0f, "kind 0x26: the dive at age 221 (age > 220), not at 220");
        }
        {   // kind 0x27, N_SHOOT_R pattern B: no homing; at age 70 re-aimed level at the player, speed x 0.75,
            // accel x -3.
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            const float f[13] = {0.25f, -0.002f, 0.0f, -10.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0};
            Bullet* b = bs.spawn(block_spawn(0x27, 409, {0, 3.2f, 0}, {1, 0, 0}, f));
            Env env;
            env.player = {7, 0, 30};
            bool pre = true;
            float sp69 = 0;
            Vec3 at70{};
            for (int k = 0; k < 70 && b; ++k) {                    // ages 0..69
                bs.update(env, nullptr);
                pre = pre && near(b->s.dir.x, 1.0f, 1e-6f) && b->s.dir.z == 0.0f && b->s.accel == -0.002f;
                fx.step(); fx.sweep();
            }
            if (b) { sp69 = b->s.speed; at70 = b->s.pos; }
            bs.update(env, nullptr);                               // age 70
            const float dx = 7.0f - at70.x, dz = 30.0f - at70.z, dl = std::sqrt(dx * dx + dz * dz);
            check(pre && near(sp69, 0.25f - 70 * 0.002f, 1e-4f), "kind 0x27: straight and decelerating until age 70");
            check(b && near(b->s.dir.x, dx / dl, 1e-4f) && b->s.dir.y == 0.0f && near(b->s.dir.z, dz / dl, 1e-4f),
                  "kind 0x27: at age 70 the direction is re-aimed at the player, level");
            check(b && near(b->s.accel, 0.006f, 1e-7f) && near(b->s.speed, sp69 * 0.75f + 0.006f, 1e-5f) &&
                  near(b->s.pos.x, at70.x + dx / dl * sp69 * 0.75f, 1e-4f),
                  "kind 0x27: speed x 0.75 and accel x -3 before that tick's move");
        }
        {   // the F shot (kind 2, effect 0x193): destroyed when struck by the player's swing
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            const float f[13] = {0.12f, 1.5e-4f, 0.0f, 0.0f, 1.0f, ib(399), ib(8), ib(1), 0.035f, 0.0f, 50.0f, 300.0f, 0.0f};
            Bullet* b = bs.spawn(block_spawn(2, 403, {0, 3, 0}, {0, 1, 0}, f));
            Bullet* c = bs.spawn(block_spawn(2, 398, {0, 3, 0}, {0, 1, 0}, f));
            Env env;
            env.player = {3, 0, 0};
            bool swing = false;
            float r_seen = 0;
            env.swing = [&](const HitShape& v) { r_seen = v.radius; return swing; };
            std::vector<Event> ev;
            for (int k = 0; k < 5; ++k) bs.update(env, &ev);
            if (const ef2sim::Particle* p = b ? fx.first_particle(b->fx) : nullptr)
                check(p->hit.rtype == 50, "F shot: the first particle's rtype is (int)f10 (the kind-1 mover's overwrite)");
            check(b && !b->fx->kill && r_seen == 1.25f && ev.empty(), "F shot: the struck test's victim sphere is r 1.25; no swing, no kill");
            swing = true;
            bs.update(env, &ev);
            bool s1 = false, s2 = false;
            for (const Event& e : ev) { s1 = s1 || (e.type == Event::kSound && e.id == 0xb9); s2 = s2 || (e.type == Event::kSound && e.id == 0x5c); }
            check(b && b->fx->kill && s1 && s2, "F shot: struck by the swing: killed, SE 0xb9 and SE 0x5c");
            bs.update(env, &ev);
            check(b && b->s.speed == 0.0f, "F shot: parked by the next tail");
            check(c && !c->fx->kill, "the struck test is effect 0x193's: a meteor (398) in the same swing flies on");
        }
        {   // FUN_0044ebb0's specials: impact 0x194 spawns no effect 404 (the host's bullet), impact 0x18d plays SE 316
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            fx.set_lookup(ef2_of);
            std::vector<std::pair<int, Vec3>> sp;
            fx.set_impact_special([&](int id, const Vec3& at) { sp.push_back({id, at}); });
            ef2sim::SpawnParams p0;
            p0.pos = {1, 2, 3};
            ef2sim::Effect* w = fx.spawn(402, efs[402], p0);
            int struck404 = 0;
            for (int k = 0; k < 200 && w && struck404 == 0; ++k) {
                for (const auto& pp : fx.particles())
                    if (pp->fx == w && !pp->dying && pp->hit.armed && pp->hit.mode == 2) { fx.strike(*pp); ++struck404; break; }
                if (!struck404) { fx.step(); fx.sweep(); }
            }
            check(struck404 == 1 && sp.size() == 1 && sp[0].first == 0x194 && count_fx(fx, 404) == 0,
                  "impact id 0x194 (402's armed mode-2 emitter): the special, no effect 404");
            sp.clear();
            ef2sim::Effect* m = fx.spawn(398, efs[398], p0);
            if (ef2sim::Particle* p = m ? fx.first_particle(m) : nullptr) fx.strike(*p);
            check(sp.size() == 1 && sp[0].first == 0x18d && count_fx(fx, 397) == 1,
                  "impact id 0x18d (the meteor 398): the special (SE 316) and the child 397");
        }
    }

    // ---- boss08 (Darm): the Bit (kind 0x25 effect 467 -> kind 0x28), kind 0x25's terrain burst 485, the mover's
    //      effect cases 0x1c9 / 0x1df, effect 0x1e7's scale (build/boss_re/DARM_B1.md §6, DARM_B23.md §9.2) ----
    {
        const std::filesystem::path bos = std::filesystem::path(argv[1]) / "EFFECT" / "EFT" / "BOS";
        efs[467] = Ef2::load(bos / "BS_08_BIT.EF2");
        efs[487] = Ef2::load(bos / "BS_08_KUROKEMURI.EF2");
        efs[515] = Ef2::load(bos / "BS_08_BITBULLET2.EF2");
        efs[513] = Ef2::load(bos / "BS_08_BITBRAKE.EF2");
        efs[484] = Ef2::load(bos / "BS_08_HORMINGTOP.EF2");
        efs[483] = Ef2::load(bos / "BS_08_HORMINGBODY.EF2");
        efs[485] = Ef2::load(bos / "BS_08_HORMINGHIT.EF2");
        efs[457] = Ef2::load(bos / "BS_08_TUKIBULLET.EF2");
        efs[520] = Ef2::load(bos / "BS_08_JIRAI.EF2");
        efs[479] = Ef2::load(bos / "BS_08_ROLLINGSPEARBULLET.EF2");
        efs[480] = Ef2::load(bos / "BS_08_ROLLINGSPEARHIT.EF2");
        bool loaded = true;
        for (int id : {467, 487, 515, 513, 484, 483, 485, 457, 520, 479, 480}) loaded = loaded && efs[id].ok();
        check(loaded, "boss08: the EF2s 457 / 467 / 479 / 480 / 483..485 / 487 / 513 / 515 / 520 load");
        auto ib = [](std::uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; };
        auto count_fx = [](ef2sim::System& fx, int id) {
            int n = 0;
            for (const auto& f : fx.effects()) n += f->id == id;
            return n;
        };
        auto pack = [&](float x, float y, float z) {
            return ib((std::uint32_t(std::uint8_t(int(x + 128.0f))) << 16) | (std::uint32_t(std::uint8_t(int(y + 128.0f))) << 8) |
                      std::uint32_t(std::uint8_t(int(z + 128.0f))));
        };
        {   // the Bit, FUN_00552730's block: falls from (15, 20, 0) toward its grid point, deploys under y 1.5
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            const float f[13] = {0.3f, ib(0xba83126fu), 0.0f, -6.0f, 1.0f, ib(487), ib(3), ib(4), ib(0x3e97e9d8u), 1.0f, 2.0f, 60.0f,
                                 pack(7.5f, 1.5f, -5.0f)};
            Spawn s = block_spawn(0x25, 467, {15, 20, 0}, {0, -1, 0}, f);
            s.owner_atk = 259.0f;
            Bullet* b = bs.spawn(s);
            Env env;
            env.player = {0, 1, -10};
            bool swing = false;
            env.swing = [&](const HitShape& v) { return swing && v.radius == 1.25f; };
            env.ray_no_boss = [&](const Vec3&, const Vec3&, int) { return RayHit{}; };   // no floor under it: it deploys
            std::vector<Event> ev;
            int deploy_age = -1, trails = 0;
            bool trail3 = true;
            for (int k = 0; k < 200 && b && b->s.kind == 0x25; ++k) {
                const std::uint32_t age = b->age;
                const int before = count_fx(fx, 487);
                bs.update(env, &ev);
                if (b->s.kind == 0x28) { deploy_age = int(age); break; }
                if (count_fx(fx, 487) > before) { ++trails; trail3 = trail3 && b->age % 3 == 0; }
                fx.step(); fx.sweep();
            }
            check(b && b->s.kind == 0x28 && deploy_age >= 70 && deploy_age < 100,
                  "Bit: effect 467 under y 1.5 becomes kind 0x28 (after age 70: 18.5 of descent)");
            check(b && b->age == 1 && b->s.speed == 0.0f && b->s.accel == 0.0f && b->s.f158 == 0 && b->s.f15c == 0 && b->s.f174 == 0.0f,
                  "Bit: the deploy zeroes speed / accel / the aim / the trail; age restarts (1 after the tail)");
            check(trails > 15 && trail3, "Bit: the smoke trail 487 every 3rd tick while it falls");
            // (15 turns of <= 17 deg while it sinks: it comes down between its column and the grid point, (9.46, -3.46))
            check(b && b->s.pos.x > 7.5f && b->s.pos.x < 11.0f && b->s.pos.z < -2.0f && b->s.pos.z > -5.0f,
                  "Bit: it steers from its column toward its grid point (7.5, -5)");
            // deployed: no strike before age 10; a shot at age 90 (kind 4, effect 515, 0.24, power -5, level, no_boss)
            swing = true;
            for (int k = 0; k < 9 && b; ++k) { bs.update(env, &ev); fx.step(); fx.sweep(); }
            check(b && !b->fx->kill && !b->f17a, "Bit: not struck before age 10 (+0x17a is set past it)");
            swing = false;
            ev.clear();
            const Vec3 at = b ? b->s.pos : Vec3{};
            const Bullet* shot = nullptr;
            for (int k = 0; k < 100 && b && !shot; ++k) {
                const std::uint32_t age = b->age;
                bs.update(env, &ev);
                for (const auto& o : bs.list()) if (o->s.effect == 515) { shot = o.get(); check(age == 90, "Bit: the first shot at deployed age 90"); }
                fx.step(); fx.sweep();
            }
            bool se337 = false;
            for (const Event& e : ev) se337 = se337 || (e.type == Event::kSound && e.id == 337);
            const Vec3 want = [&] { const float dx = env.player.x - at.x, dz = env.player.z - at.z, l = std::sqrt(dx * dx + dz * dz); return Vec3{dx / l, 0, dz / l}; }();
            check(shot && se337 && shot->s.kind == 4 && shot->fx && shot->fx->no_boss && shot->fx->power == -5.0f && shot->atk == 259.0f,
                  "Bit: SE 337 and a kind-4 bullet 515, power -5, the owner's ATK, fx+0x201");
            // the shot has run its first update: dir is unchanged (no homing period), the speed is the block's
            check(shot && near(shot->s.dir.x, want.x, 1e-4f) && shot->s.dir.y == 0.0f && near(shot->s.dir.z, want.z, 1e-4f) && near(shot->s.speed, 0.24f, 1e-6f),
                  "Bit: the shot is level, at the player, 0.24 a tick");
            check(b && near(b->s.pos.x, at.x, 1e-6f) && near(b->s.pos.z, at.z, 1e-6f), "Bit: it stays put before age 480");
            // the creep: from age 481, 0.025 a tick toward the player in x / z
            if (b) b->age = 481;
            const Vec3 p0 = b ? b->s.pos : Vec3{};
            bs.update(env, &ev);
            const float moved = b ? std::sqrt((b->s.pos.x - p0.x) * (b->s.pos.x - p0.x) + (b->s.pos.z - p0.z) * (b->s.pos.z - p0.z)) : 0.0f;
            check(b && near(moved, 0.025f, 1e-5f) && b->s.pos.y == p0.y, "Bit: past age 480 it creeps 0.025 a tick toward the player, level");
            // struck: the kill flag, SE 0xb9 at the player, the burst 513
            swing = true;
            ev.clear();
            bs.update(env, &ev);
            bool se185 = false;
            for (const Event& e : ev) se185 = se185 || (e.type == Event::kSound && e.id == 0xb9 && e.pos.z == env.player.z);
            check(b && b->fx->kill && se185 && count_fx(fx, 513) == 1, "Bit: struck by the swing: killed, SE 185 at the player, effect 513");
        }
        {   // kind 0x25 with another effect (484, the homing volley) on terrain: the burst 485, no SE, the shake
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            const float f[13] = {1.2f, ib(0xba83126fu), 0.0f, -6.0f, 1.0f, ib(483), ib(2), ib(2), ib(0x3e97e9d8u), 1.0f, 2.0f, 60.0f,
                                 pack(0.0f, 1.0f, 0.0f)};
            Spawn s = block_spawn(0x25, 484, {0, 0.3f, 0}, {0, -1, 0}, f);
            s.owner_atk = 259.0f;
            Bullet* b = bs.spawn(s);
            Env env;
            env.ray_no_boss = [&](const Vec3& from, const Vec3&, int) { RayHit r; r.hit = true; r.point = from; return r; };
            std::vector<Event> ev;
            bs.update(env, &ev);
            const ef2sim::Effect* burst = nullptr;
            for (const auto& fe : fx.effects()) if (fe->id == 485) burst = fe.get();
            bool se = false, shake = false;
            for (const Event& e : ev) { se = se || e.type == Event::kSound; shake = shake || (e.type == Event::kShake && e.id == 10); }
            check(b && b->fx->kill && burst && burst->power == -6.0f && burst->knock == 1.0f && burst->rtype == 0 && burst->status == 0 &&
                  burst->atk == 259.0f && near(burst->M.m[3][1], 0.5f) && !se && shake,
                  "kind 0x25, effect 484 on terrain: the burst 485 (power -6, rtype 0, status 0) at y 0.5, the shake, no SE");
        }
        {   // the mover's splash cases: 457 (0x1c9) -> SE 349 + the mine 520; 479 (0x1df) -> 480, no SE
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            const float f[13] = {ib(0x3dc8b439u), 0.02f, 0.01f, 20.0f, 1.0f, 0, 0, 0, 0.0f, 1.0f, 0, 0, 0};
            Spawn s = block_spawn(2, 457, {3, -0.1f, 4}, {1, 0, 0}, f);
            s.owner_atk = 259.0f;
            Bullet* b = bs.spawn(s);
            const float g[13] = {ib(0x3d48b439u), 0.01f, 0.002f, 0.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0};
            Bullet* c = bs.spawn(block_spawn(2, 479, {-3, -0.1f, 4}, {0, -1, 0}, g));
            Env env;
            int rays = 0;
            env.ray = [&](const Vec3&, const Vec3&, int) { ++rays; return RayHit{}; };
            std::vector<Event> ev;
            bs.update(env, &ev);
            const ef2sim::Effect *mine = nullptr, *hit = nullptr;
            for (const auto& fe : fx.effects()) { if (fe->id == 520) mine = fe.get(); if (fe->id == 480) hit = fe.get(); }
            int se349 = 0, other = 0;
            for (const Event& e : ev) { if (e.type == Event::kSound && e.id == 349) ++se349; else ++other; }
            check(b && b->fx->kill && b->s.pos.y < -900.0f && mine && mine->power == 20.0f && mine->rtype == 2 && mine->status == 0x10 &&
                  mine->atk == 259.0f && near(mine->M.m[3][1], 0.5f) && near(mine->M.m[3][0], 3.0f, 0.2f),
                  "effect 0x1c9 under y 0: killed, the mine 520 at y 0.5 (power 20, rtype 2, status 0x10), y = -999");
            check(c && c->fx->kill && c->s.pos.y < -900.0f && hit && hit->rtype == 2 && hit->status == 0x10,
                  "effect 0x1df under y 0: killed, effect 480 (rtype 2, status 0x10), y = -999");
            check(se349 == 1 && other == 0, "the splash: SE 349 for 0x1c9 only");
            check(rays == 0, "kind 2 with f9 = 1 (TukiBullet): no terrain probe");
            bs.update(env, &ev);
            check(count_fx(fx, 520) == 1 && count_fx(fx, 480) == 1, "the splash happens once (the kill flag)");
        }
        {   // effect 0x1e7 (487 as a bullet: the limb smoke): three draws -> the scale, kept as the effect's own
            ef2sim::Rng rng;
            ef2sim::System fx(&rng);
            Bullets bs(&fx, ef2_of);
            int draws = 0;
            const std::uint32_t words[3] = {0x00400000u, 0x00200000u, 0x00600000u};   // mantissas: u = 1.5, 1.25, 1.75
            bs.set_rng([&] { return words[draws++ % 3]; });
            const float f[13] = {0.02f, 0.002f, 0.0f, 0.0f, 1.0f, 0, 0, 0, 0, 0, 0, 0, 0};
            Bullet* b = bs.spawn(block_spawn(2, 487, {0, 5, 0}, {0, 1, 0}, f));
            check(b && draws == 3 && near(b->fx->scale_x, 1.0f + 1.5f * 0.75f) && near(b->fx->scale_y, 1.25f) && near(b->fx->scale_z, 1.0f + 1.5f * 0.5f),
                  "effect 0x1e7: 3 draws, scale (1 + 1.5 (u3 - 1), u2, 1 + 1.5 (u1 - 1))");
            Bullet* o = bs.spawn(block_spawn(2, 479, {0, 5, 0}, {0, 1, 0}, f));
            check(o && draws == 3 && o->fx->scale_x == 1.0f, "another effect draws nothing");
        }
    }

    std::printf("[bullet] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
