// cleria-bullettest — the GoXx bullet (src/cleria/bullet.hpp) against build/ai_spec/BULLETS.md:
// the orientation / homing helpers, worked example A (M510's disc, §5), the kill-flag park, the
// kind-2 homing window, the heal shot, the drip's bounce, the spawn's knock / side rules, the
// trail, Velagunder's bubble (kind 0x1e) and drool drop (kind 0x1f), Hugo's volley (kind 0x1b), the
// aimed kind 0x16, and XSO op 0x112's falling water probe (GoXx type 0x16).
// Usage: cleria-bullettest <assets dir>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>

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

    std::printf("[bullet] %s (%d failed)\n", fails ? "FAILURES" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
