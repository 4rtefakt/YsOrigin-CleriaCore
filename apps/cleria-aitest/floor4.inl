// Floor 4 (S_40) brains: M560 / M780 and the revive handler, M570 + M410, M280, M290 and the kind-0xf
// bomb (build/ai_spec/M560.md, M780.md, REVIVE.md, M570.md, M410.md, M280.md, M290.md, BOMB.md).
// Included by main.cpp after its helpers (check, Rig, params).
#include "cleria/brains/brains.hpp"
#include "cleria/brains/revive.hpp"
#include "cleria/bullet.hpp"
#include "cleria/sand.hpp"
#include "cleria/ice.hpp"

namespace floor4 {

struct Pool : AiWorld {        // open space; children kept in a list
    std::list<Actor> actors;
    int next_id = 400, removed = 0;
    void for_each(const std::function<void(Actor&)>& f) override { for (Actor& a : actors) if (!a.destroy_requested()) f(a); }
    Actor* find(int id) override { for (Actor& a : actors) if (a.instance_id == id && !a.destroy_requested()) return &a; return nullptr; }
    Actor* spawn_child(Actor& p, int cast, const Vec3& d, int lv, bool keep0) override {
        actors.emplace_back();
        Actor& c = actors.back();
        c.instance_id = next_id++; c.cast_id = cast; c.level = lv; c.hp = c.max_hp = 20; c.height = 0.5251f;
        c.pos = {p.pos.x + d.x, p.pos.y + d.y, p.pos.z + d.z};
        c.ai.heading = p.ai.heading; c.ai.i(0x854) = p.instance_id; c.ai.phase() = keep0 ? 0 : 4;
        return &c;
    }
    void remove(Actor& a) override { a.header_flags |= kDestroyRequested; ++removed; }
    Ray ray(const Vec3&, const Vec3&) override { return {}; }
    bool sphere_hits(const Vec3&, float) override { return false; }
    void arena(float r[4]) override { r[0] = r[1] = -3.4e38f; r[2] = r[3] = 3.4e38f; }
    const float* enemy_raw(int) override { return nullptr; }
};

}  // namespace floor4

static void floor4_tests() {
    using floor4::Pool;
    // ---- M560 Karon / M780 Zelkaron (karon.cpp) ----
    const EnemyBrain m560 = enemy_brain_for(49), m780 = enemy_brain_for(71);
    check(m560 && m780, "Karon / Zelkaron brains registered (AI 49 / 71)");
    EnemyAiParams kp = params(0.06f, 0.055f, 1.0f, 2.0f, 0.0f);
    kp.raw[5] = 2.5f;                                              // +0x828 = H
    {   // Buried on sand, the veto, the distance-delay surfacing.
        Pool w; Actor& K = (w.actors.emplace_back(), w.actors.back());
        K.instance_id = 60; K.cast_id = 60; K.ai_index = 49; K.height = 1.176f; K.hp = K.max_hp = 261;
        K.ai.surf_ray = 0x12; K.ai.water_y = -2.0f;
        Rig r; r.env.actor = &K; r.env.world = &w; r.env.target = {4.0f, 1.075f, 0};
        enemy_ai_tick(K.ai, kp, r.env, m560);
        check(K.ai.i(0x7a0) == 0 && !K.ai.hittable && std::fabs(K.pos.y + K.height + 3.5f) < 1e-5f,
              "M560 init on sand: buried 1.5 under the sand top (origin), unhittable");
        r.env.was_hit = true;
        enemy_ai_tick(K.ai, kp, r.env, m560);
        r.env.was_hit = false;
        check(K.ai.phase() == 1, "M560 veto: a hit's acquisition while buried is cancelled (phase stays 1)");
        int t = 0;
        for (; t < 200 && K.ai.phase() != 2; ++t) enemy_ai_tick(K.ai, kp, r.env, m560);
        check(K.ai.phase() == 2 && K.ai.i(0x7a0) == 1 && t >= 38 && t <= 42,
              "M560 surfaces and engages floor(10 x dist) = 40 ticks after the target came within 5");
        enemy_ai_tick(K.ai, kp, r.env, m560);
        check(K.ai.hittable, "M560 surfaced: hittable");
    }
    {   // The attack and the leap; M780's combo.
        for (int which = 0; which < 2; ++which) {
            Pool w; Actor& K = (w.actors.emplace_back(), w.actors.back());
            K.instance_id = 60; K.cast_id = which ? 82 : 60; K.ai_index = which ? 71 : 49; K.height = 1.176f;
            K.hp = K.max_hp = 261;
            Rig r; r.env.actor = &K; r.env.world = &w; r.env.target = {3.0f, 1.075f, 0};
            const EnemyBrain br = which ? m780 : m560;
            enemy_ai_tick(K.ai, kp, r.env, br);                    // init: surfaced (no sand)
            int sec7 = -1;
            for (int t = 0; t < 3000 && sec7 < 0; ++t) {
                const AiOut o = enemy_ai_tick(K.ai, kp, r.env, br);
                if (o.section == 7) sec7 = t;
            }
            const int dur = which ? 80 : 65;
            check(sec7 >= 0 && K.ai.i(0x72c) == 1 && K.ai.dur() == dur - 1 && K.ai.cool() == dur - 1,
                  which ? "M780: the 10% spin roll -> section 7, dur / cool 80" : "M560: the 5% spin roll -> section 7, dur / cool 65");
            r.env.section = 7; r.env.aso_marker = 1;
            const float y0 = K.pos.y;
            const AiOut o = enemy_ai_tick(K.ai, kp, r.env, br);
            const float push = which ? 0.5f : 0.3f;
            check(o.set_marker == 2 && std::fabs(K.vert.t - 0.15f) < 1e-6f && std::fabs(K.pos.y - y0 - 0.3f) < 1e-5f &&
                  std::fabs(std::hypot(K.mover.k1.x, K.mover.k1.z) - push) < 1e-4f && !K.vert.grounded,
                  which ? "M780 leap at Marker 1: t 0.15, y +0.3, push 0.5, marker consumed"
                        : "M560 leap at Marker 1: t 0.15, y +0.3, push 0.3, marker consumed");
            if (which) {                                           // N = max(d + 2, 2) = 2 on Normal
                r.env.aso_marker = 3;
                const AiOut o2 = enemy_ai_tick(K.ai, kp, r.env, br);
                const AiOut o3 = enemy_ai_tick(K.ai, kp, r.env, br);
                check(o2.section == 7 && K.ai.i(0x7c8) == 2 && o3.section != 7,
                      "M780 combo: Marker 3 re-attacks once on Normal (2 spins), then stops");
            }
        }
    }
    {   // The revive handler: 600 dead ticks, then phase 4; with the Chimes, removal at dead tick 44.
        const revive::Row* rr = revive::row(49);
        check(rr && revive::row(50) && revive::row(71) && !revive::row(3), "revive family rows: M560 / M570 / M780");
        Actor K; K.cast_id = 60; K.ai_index = 49; K.height = 1.176f; K.max_hp = 261; K.hp = 0;
        K.flags130 = 0x2009; K.ai.phase() = 2;
        revive::World w;
        int t = 0; bool revived = false;
        for (; t < 700 && !revived; ++t) {
            const revive::DeadPre pre = revive::dead_pre(*rr, K, w);
            if (t == 0 && !pre.first) break;
            revived = revive::dead_post(*rr, K, nullptr).revived;
        }
        check(revived && t == 600 && K.hp == 261 && K.ai.phase() == 4 && !K.ai.hittable && K.ai.alpha == 1.0f &&
              K.ai.dead_t == 0 && K.ai.i(0x7a0) == 1,
              "no Chimes: opaque corpse, revived on dead tick 600 (HP full, phase 4, unhittable)");
        Actor C; C.cast_id = 60; C.ai_index = 49; C.max_hp = 261; C.hp = 0; C.flags130 = 0x2009;
        w.chimes = true;
        int rm = -1;
        for (int k = 1; k < 100 && rm < 0; ++k) {
            if (revive::dead_pre(*rr, C, w).remove) rm = k;
            else revive::dead_post(*rr, C, nullptr);
        }
        check(rm == 44 && C.flags130 == 0x2000 && C.ai.i(0x7a4) == 1,
              "Silver Chimes: latched, +0x130 ^= 9, faded and removed on dead tick 44");
        std::vector<int> ids{60, 0xba};
        const int nid = revive::new_instance_id(60, [&](int i) { return std::find(ids.begin(), ids.end(), i) != ids.end(); });
        check(nid == 0xbb, "FUN_00434db0: the cast id taken -> the lowest free id >= 0xba");
        EnemyAi a; a.surf_ray = 0x12; a.water_y = -2.0f;
        float y = -3.5f;
        revive::sand_clamp(*rr, a, y, 0.0f, true, false);
        check(std::fabs(y + 3.44f) < 1e-5f, "sand clamp: a corpse that will revive rises 0.06 / tick");
        Actor X; X.exp_reward = 100;
        revive::exp_block(*rr, X, false);
        const float e0 = X.exp_reward;
        revive::exp_block(*rr, X, true);
        check(e0 == 0.0f && X.exp_reward == 100.0f, "EXP block: +0xb0 zeroed without the Chimes, kept with them");
    }
    // ---- M570 Zarue + M410 its hands ----
    {
        const EnemyBrain m570 = enemy_brain_for(50), m410 = enemy_brain_for(34);
        check(m570 && m410, "Zarue / hand brains registered (AI 50 / 34)");
        EnemyAiParams zp = params(0.1f, 0.035f, 1.0f, 1.5f, 1.0f);
        zp.raw[9] = 0.03f; zp.raw[10] = 0.03f; zp.raw[11] = 0.15f; zp.raw[12] = 3.0f;
        Pool w; Actor& Z = (w.actors.emplace_back(), w.actors.back());
        Z.instance_id = 61; Z.cast_id = 0x3d; Z.ai_index = 50; Z.flyer = true; Z.height = 1.5751f;
        Z.hp = Z.max_hp = 297; Z.level = 28;
        Rig r; r.env.actor = &Z; r.env.world = &w; r.env.target = {6.0f, 1.075f, 0}; r.env.target_grounded = true;
        int s7 = -1;
        for (int t = 0; t < 400 && s7 < 0; ++t) if (enemy_ai_tick(Z.ai, zp, r.env, m570).section == 7) s7 = t;
        check(s7 >= 0 && Z.ai.i(0x72c) == 0x66 && Z.ai.dur() == 110, "M570 mid range: wind-up 10, section 7 held 111");
        r.env.section = 7; r.env.aso_marker = 2;
        const AiOut so = enemy_ai_tick(Z.ai, zp, r.env, m570);
        int kids = 0, s0 = 0, s1 = 0; bool fields = true;
        for (Actor& c : w.actors) if (c.cast_id == 0x2d) {
            ++kids; (c.ai.i(0x7a0) ? s1 : s0) += 1;
            fields &= c.ai.i(0x854) == 61 && c.ai.i(0x7a4) == 100 && c.ai.f(0x7b0) == 1.0f && c.ai.phase() == 0;
        }
        check(kids == 2 && s0 == 1 && s1 == 1 && fields && !so.effects.empty() && so.effects[0].id == 277,
              "M570 summon at Marker 2: the hole (EF2 277) and 2 hands (one per side, sub 100, dist 1)");
        r.env.aso_marker = 0; r.env.section = 300;
        for (int t = 0; t < 200 && Z.ai.i(0x72c) != 0x67; ++t) enemy_ai_tick(Z.ai, zp, r.env, m570);
        check(Z.ai.i(0x72c) == 0x67, "M570 holds section 300 while both sides live");
        // The hand: wait, appear 0.5 from the player at its heading + 0.4, hittable at marker 1, fade at 2.
        Actor* H = nullptr;
        for (Actor& c : w.actors) if (c.cast_id == 0x2d && c.ai.i(0x7a0) == 0) H = &c;
        EnemyAiParams hp = params(0.1f, 0.045f, 1.0f, 2.0f, 0.0f);
        Rig hr; hr.env.actor = H; hr.env.world = &w; hr.env.target = {6.0f, 1.075f, 0}; hr.env.target_heading = 0.0f;
        hr.env.target_floor = 0.0f;
        int app = -1, sec = -1;
        for (int t = 0; t < 120 && app < 0; ++t) {
            const AiOut o = enemy_ai_tick(H->ai, hp, hr.env, m410);
            if (H->ai.alpha == 1.0f) { app = t; sec = o.section; }
        }
        const float dx = H->pos.x - 6.0f, dz = H->pos.z;
        check(app >= 18 && app <= 62 && sec == 7 && std::fabs(std::hypot(dx, dz) - 0.5f) < 1e-4f &&
              std::fabs(std::atan2(dz, dx) - 0.4f) < 1e-4f && std::fabs(H->pos.y + H->height - 0.5f) < 1e-5f &&
              H->def == 999.0f, "M410: appears 15..59 ticks in, 0.5 u out at the heading + 0.4, floor + 0.5, section 7");
        hr.env.aso_marker = 1;
        enemy_ai_tick(H->ai, hp, hr.env, m410);
        const bool hit1 = H->ai.hittable;
        hr.env.aso_marker = 2;
        enemy_ai_tick(H->ai, hp, hr.env, m410);
        check(hit1 && !H->ai.hittable && std::fabs(H->ai.alpha - 0.95f) < 1e-6f, "M410: hittable only at marker 1; fades at marker 2");
        Z.hp = 0;
        const AiOut ho = enemy_ai_tick(H->ai, hp, hr.env, m410);
        check(ho.set_hp == 0.0f, "M410 dies with its parent (parent HP < 1 -> own HP 0)");
        Z.hp = 297;
        for (Actor& c : w.actors) if (c.cast_id == 0x2d && c.ai.i(0x7a0) == 1) w.remove(c);
        r.env.section = 300;
        const AiOut zo = enemy_ai_tick(Z.ai, zp, r.env, m570);
        check(zo.section == 0x12d && Z.ai.i(0x72c) == 0x68, "M570: a side gone -> section 0x12d, rest");
    }
    // ---- M280 Chups ----
    {
        const EnemyBrain m280 = enemy_brain_for(21);
        check(m280 != nullptr, "Chups brain registered (AI 21)");
        EnemyAiParams cp = params(0.1f, 0.1f, 1.0f, 1.5f, 3.0f);
        cp.raw[9] = 0.05f; cp.raw[10] = 0.05f;
        Pool w; Actor& C = (w.actors.emplace_back(), w.actors.back());
        C.instance_id = 32; C.cast_id = 0x20; C.ai_index = 21; C.flyer = true; C.height = 0.9251f;
        C.hp = C.max_hp = 298; C.hurt_radius = 0.6f;
        Rig r; r.env.actor = &C; r.env.world = &w; r.env.self = {-1.0f, 1.075f, 0}; r.env.target = {0, 1.075f, 0};
        r.env.target_heading = 0.0f;
        int lt = -1; int sec = -1;
        for (int t = 0; t < 4000 && lt < 0; ++t) {
            const AiOut o = enemy_ai_tick(C.ai, cp, r.env, m280);
            if (C.ai.i(0x728) == 1) { lt = t; sec = o.section; }
        }
        const int dur0 = C.ai.dur();
        enemy_ai_tick(C.ai, cp, r.env, m280);
        check(lt >= 0 && sec == 7 && dur0 == 399 && !C.ai.hittable, "M280 latches from behind: section 7, 400 ticks, then unhittable");
        int boxes = 0; bool box_ok = true;
        for (int t = 0; t < 16; ++t) {
            const AiOut o = enemy_ai_tick(C.ai, cp, r.env, m280);
            for (const AiHitbox& h : o.hitboxes) { ++boxes; box_ok &= h.radius == 0.4f && h.power == -5.0f && h.rehit == 96 && h.one_shot == 1; }
        }
        check(boxes == 2 && box_ok, "M280 latched: the r 0.4 / power -5 sphere every 8 ticks (re-hit 96 on Normal)");
        r.env.target_moving = true;
        AiOut rel; int k = 0;
        for (; k < 40 && C.ai.i(0x728) == 1; ++k) {
            r.env.target_heading = (k & 1) ? 0.0f : 1.0f;          // a > 40 deg turn every tick
            rel = enemy_ai_tick(C.ai, cp, r.env, m280);
        }
        check(C.ai.i(0x728) == 0 && k == 21 && rel.section == 1 && rel.section_force && C.ai.cool() >= 299,
              "M280 shake-off: 21 turns > 40 deg release it (forced section 1, cooldown 300..399)");
    }
    // ---- M290 Bicmobre and the bomb ----
    {
        const EnemyBrain m290 = enemy_brain_for(22);
        check(m290 != nullptr, "Bicmobre brain registered (AI 22)");
        EnemyAiParams bp = params(0.12f, 0.035f, 1.0f, 2.0f, 0.0f);
        bp.raw[5] = 3.2f;
        Actor B; B.instance_id = 33; B.cast_id = 33; B.ai_index = 22; B.height = 0.976f; B.flags12c = 0x00510010u;
        Rig r; r.env.actor = &B; r.env.target = {7.0f, 1.075f, 0};
        int s7 = -1;
        for (int t = 0; t < 400 && s7 < 0; ++t) if (enemy_ai_tick(B.ai, bp, r.env, m290).section == 7) s7 = t;
        check(s7 >= 0 && B.ai.i(0x728) == 3 && B.ai.dur() == 204 && B.ai.action() == 2, "M290 inside 8.5, facing: section 7, stand 205");
        r.env.section = 7; r.env.aso_marker = 1; r.env.facing = 0.0f;
        const AiOut va = enemy_ai_tick(B.ai, bp, r.env, m290);
        const AiOut va2 = enemy_ai_tick(B.ai, bp, r.env, m290);
        r.env.aso_marker = 2;
        const AiOut vb = enemy_ai_tick(B.ai, bp, r.env, m290);
        const bool ok = va.bullets.size() == 1 && va2.bullets.empty() && vb.bullets.size() == 1 &&
                        va.bullets[0].kind == 0xf && va.bullets[0].effect == 300 && va.bullets[0].speed == 0.125f &&
                        va.bullets[0].dir.z > 0.0f && vb.bullets[0].dir.z < 0.0f;
        check(ok, "M290 volleys: one kind-0xf bomb (effect 300, 0.125) at Marker 1 (+5 deg), one at Marker 2 (-5 deg)");
        int bombs[3] = {0, 0, 0};
        XorShift xs;
        for (int k = 0; k < 3; ++k) {
            Actor D; D.height = 0.976f;
            for (int n = 0; n < 44; ++n) bombs[k] += int(brains::m290_dead(D, k, [&] { return xs(); }).size());
        }
        check(bombs[0] == 2 && bombs[1] == 3 && bombs[2] == 8, "M290 death helper: 2 / 3 / 8 bombs over the 44 dead ticks");
        // BOMB.md §8: (1, 0.6981317, 0) at 0.125 over a static floor at y = -0.5, restitution 0.65.
        ef2sim::System sys(nullptr);
        bullet::Bullets bl(&sys, [](int) -> const Ef2* { return nullptr; });
        bl.set_rng([] { return 0x00400000u; });                   // u = 0.5 -> r = 0.65; spin bit clear
        bullet::Env env;
        env.ray = [](const Vec3& o, const Vec3& d, int) {
            bullet::RayHit h;
            const float y1 = o.y + d.y;
            if (o.y >= -0.5f && y1 < -0.5f) {
                const float t = (o.y + 0.5f) / (o.y - y1);
                h.hit = true; h.point = {o.x + d.x * t, -0.5f, o.z + d.z * t}; h.normal = {0, 1, 0};
            }
            return h;
        };
        bullet::Spawn s; s.kind = 0xf; s.effect = 300;
        const float l = std::sqrt(1.0f + 0.6981317f * 0.6981317f);
        s.dir = {1.0f / l, 0.6981317f / l, 0}; s.speed = 0.125f;
        check(bl.spawn(s) == nullptr && bl.bombs().size() == 1, "kind 0xf is not a bullet: spawn() makes a bomb");
        for (int n = 0; n < 27; ++n) bl.update(env, nullptr);
        const bullet::Bomb& bo = *bl.bombs()[0];
        check(std::fabs(bo.pos.x - 3.247930f) < 2e-4f && std::fabs(bo.pos.y + 0.419566f) < 2e-4f,
              "BOMB.md worked example: the first bounce at update 27 -> (3.24793, -0.419566)");
        for (int n = 27; n < 170 && !bl.bombs()[0]->rest; ++n) bl.update(env, nullptr);
        check(bl.bombs()[0]->rest && std::fabs(bl.bombs()[0]->pos.x - 6.31854f) < 2e-3f, "... and it rests at x 6.31854");
        for (int n = 0; n < 200 && !bl.bombs().empty(); ++n) bl.update(env, nullptr);
        check(bl.bombs().empty(), "the bomb is gone at age 180");
    }
    {   // The player in quicksand (src/cleria/sand.hpp): the dy clamp and the walking lift, +0x38c.
        check(sand2_dy(-0.3f, false, -2.0f, -1.5f) == -0.015f && sand2_dy(0.2f, false, -2.0f, -1.5f) == 0.03f,
              "sand2: dy clamped to [-0.015, 0.03] (0x6bdfb4 / 0x6bd100)");
        check(sand2_dy(-0.3f, true, -2.0f, -1.5f) == 0.01f && sand2_dy(-0.3f, true, -2.0f, -1.07f) == -0.015f,
              "sand2: walking lifts 0.01 below the top + 0.925, not above it");
        PlayerSand ps; ps.surf_ray = 0x12;
        check(ps.probe_drop() == 0.95f && !sand2_buried(ps, -2.0f, -4.2f) && sand2_buried(ps, -2.0f, -4.3f),
              "sand2: the probe starts 0.95 under the feet; buried past 2.25 (0x6bd5d0)");
        ps.surf_ray = 1;
        check(ps.probe_drop() == 0.25f && !sand2_buried(ps, 0.0f, -9.0f), "not sand2: 0.25, never buried");
    }
    {   // Ice (src/cleria/ice.hpp): the monster mover's 0.35 blend and its 60-tick tail; the slope push.
        MoverState m;
        mover_post(m, 1, 0.1f, 0.0f, true, false, true, 1.0f, 10.0f, 0x00110010u, kSurfaceIce);
        const bool b1 = std::fabs(m.vx - 0.035f) < 1e-6f && m.ice_tail == 60;
        mover_post(m, 1, 0.1f, 0.0f, true, false, true, 1.0f, 10.0f, 0x00110010u, kSurfaceIce);
        const bool b2 = std::fabs(m.vx - (0.035f * 0.65f + 0.035f)) < 1e-6f;
        mover_post(m, 0, 0.1f, 0.0f, true, false, false, 1.0f, 10.0f, 0x00110010u, 1);   // off the ice, Movable 0
        check(b1 && b2 && m.ice_tail == 59 && m.vx == 0.0f, "ice: k 0.35 on 0x0b (+0x4f0 = 60); the tail writes past THE GATE at k 1");
        MoverState n;
        mover_post(n, 1, 0.1f, 0.0f, true, false, true, 1.0f, 10.0f, 0x2041c012u, kSurfaceIce);    // Nooze: +0x12c & 0x20000000
        check(n.vx == 0.1f && n.ice_tail == 0, "ice: +0x12c & 0x20000000 casts do not slip");
        const Vec3 pz = ice_slope_push({0.0f, 1.0f, 0.0f});
        const Vec3 ps = ice_slope_push({0.6f, 0.8f, 0.0f});
        check(pz.x == 0.0f && pz.z == 0.0f && std::fabs(ps.x - 0.012f) < 1e-6f && std::fabs(ps.y - (1.8f * 0.1f - 0.8f) * 0.02f) < 1e-6f,
              "ice: a flat floor pushes nothing; a slope pushes 0.02 x normalize(n.x, -n.y, n.z) down it");
        check(player_ice(kSurfaceIce, 0) && !player_ice(kSurfaceIce, 0x4000u) && !player_ice(1, 0),
              "ice: the player slips on 0x0b unless the 0x4000 accessory bit is on");
    }
}
