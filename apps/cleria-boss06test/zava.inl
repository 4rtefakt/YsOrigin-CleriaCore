// Zava herself (src/cleria/brains/m870.cpp + boss06_zava.hpp) against build/boss_re/ZAVA_AB.md ("AB") and
// ZAVA_CG.md slice C ("CG"). Included by main.cpp; every expectation is a number of those specs.

struct ZWorld : AiWorld {
    std::map<int, int> flags;
    std::vector<Actor*> actors;
    int hidden = 0;
    void for_each(const std::function<void(Actor&)>& f) override { for (Actor* a : actors) f(*a); }
    Actor* find(int) override { return nullptr; }
    Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
    void remove(Actor&) override {}
    Ray ray(const Vec3&, const Vec3&) override { return {}; }
    bool sphere_hits(const Vec3&, float) override { return false; }
    void arena(float r[4]) override { r[0] = r[1] = -100; r[2] = r[3] = 100; }
    const float* enemy_raw(int) override { return nullptr; }
    void set_flag(int i, int v) override { flags[i] = v; }
    int flag(int i, int fb) override { const auto it = flags.find(i); return it == flags.end() ? fb : it->second; }
    void hide_boss_gauge() override { ++hidden; }
};

// The host stand-in for one Zava actor: the handler order of AB §A.1 (enemy_ai_tick -> m870_post -> [mover] ->
// m870_tail), the section the ASO VM would be in, the flag writes applied. No mover and no gravity: the tests place
// her and set the grounded byte themselves.
struct ZRig {
    Rig h;
    Actor Z;
    ZWorld w;
    AiEnv env;
    Boss06Link L;
    Boss06Output bo;
    Boss06ZavaOut zo;
    EnemyAiParams p{};
    AiOut out;
    int sec = 2;
    explicit ZRig(int diff = 0, bool easy = false, bool ve = false, int ch = 1) : h(setup(diff, easy, ve)) {
        Z.cast_id = 0xad; Z.ai_index = 80; Z.height = 1.0f; Z.instance_id = 0xad;
        at({-34.0f, 7.0f, 22.0f});
        env.actor = &Z; env.world = &w; env.boss06 = &L;
        env.rng = h.in.rng;
        env.blocked = [](const Vec3&, const Vec3&) { return false; };
        env.has_floor = [](float, float, float, float) { return true; };
        env.section_type = [](int s) { return s == 5 || s == 307 || s == 315 ? 4 : 3; };   // AB §A.11 "type"
        env.diff_index = diff; env.diff_easy = easy; env.diff_very_easy = ve; env.character = ch;
        L.heads = &h.b; L.in = &h.in; L.z = &h.z; L.out = &bo;
        w.flags[156] = 1; w.flags[130] = 1;
        h.in.player_hp_mirror = 100.0f; h.in.player_pos = {-34.0f, 7.0f, 29.0f};
    }
    Vec3 org() const { return {Z.pos.x, Z.pos.y + Z.height, Z.pos.z}; }
    void at(const Vec3& o) { Z.pos = {o.x, o.y - Z.height, o.z}; }
    EnemyAi& a() { return Z.ai; }
    AiOut step(bool dmg = false) {
        h.feed();
        bo = Boss06Output{};
        env.self = org(); env.target = h.in.player_pos; env.section = sec; env.took_damage = dmg;
        env.grounded = Z.vert.grounded; env.target_section = h.in.player_section;
        out = enemy_ai_tick(Z.ai, p, env, enemy_brain_for(80));
        brains::m870_post(Z, env, out);
        zo = L.zout;
        if (out.section >= 0) sec = out.section;
        for (const auto& f : bo.flags) w.flags[f.first] = f.second;
        h.clips(bo);
        brains::m870_tail(Z, env);
        return out;
    }
    // INIT, AI phase 1 -> 2 (the brain's own switch, or the shared action player's sight test one frame earlier),
    // the fight start.
    void boot() { for (int k = 0; k < 5 && a().i(0x79c) != 1; ++k) step(); }
    AiOut brain_only() {                                           // FUN_0053af10's head + brain, no action player
        h.feed(); bo = Boss06Output{};
        env.self = org(); env.target = h.in.player_pos; env.section = sec; env.took_damage = false;
        env.grounded = Z.vert.grounded;
        out = enemy_brain_for(80)(Z.ai, p, env);
        zo = L.zout;
        return out;
    }
    void act(int phase, int action, int stp = 0) {
        a().i(0x79c) = phase; a().i(0x728) = action; a().i(0x72c) = stp; a().i(0x87c) = 0;
    }
    void fly() { Z.flyer = true; Z.vert.grounded = false; }
    bool cam(int m) const { for (int c : zo.boss_cam) if (c == m) return true; return false; }
};

static const Boss06Effect* zfx(const Boss06Output& o, int id) { return fx(o, id, -1); }
static float len3(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

static void zava_init() {
    check(enemy_brain_for(80) == &brains::m870, "AB §H: Zava is the EnemyBrain of AI index 80");
    ZRig n;
    n.L.zin.eex5 = [](Actor& z) { z.hp = z.max_hp = 1234.0f; z.atk = 9.0f; z.def = 9.0f; z.exp_reward = 77.0f; };
    n.Z.flags12c = 0x00100050;
    const AiOut o = n.brain_only();
    check(n.Z.max_hp == 6000.0f && n.Z.hp == 6000.0f && n.Z.atk == 227.0f && n.Z.def == 252.0f && n.Z.exp_reward == 77.0f,
          "AB §A.2 INIT [0x53ad69]: EEX record 5 first, then max HP = HP = 6000, STR 227, DEF 252");
    check(n.a().phase() == 1 && n.a().i(0x79c) == 0 && n.a().i(0x710) == -1 && n.Z.kind == 0x88 &&
          (n.Z.flags12c & 0x280040u) == 0x280000u && n.a().f(0x81c) == 1.0f && n.a().f(0x820) == 1.0f &&
          n.a().f(0x7ac) == 1.0f && o.gauge_hp < 0,
          "AB §A.2 INIT: +0x6f0 = 1, +0x710 = -1, +0x60 = 0x88, +0x12c = (&~0x40)|0x280000, +0x7ac = 1.0, no gauge yet");
    ZRig nm(2); nm.step();
    check(nm.Z.max_hp == 7500.0f && nm.Z.hp == 7500.0f && nm.Z.def == 262.0f && nm.Z.atk == 227.0f,
          "AB §A.2 INIT [flag144 == 2]: Nightmare max HP = HP = 7500, DEF 262");
    ZRig e(0, true); e.w.flags[196] = 30; e.step();
    ZRig e2(0, true); e2.w.flags[196] = 7; e2.step();
    check(e.Z.atk == 207.0f && e.Z.def == 232.0f && e2.Z.atk == 224.0f && e2.Z.def == 249.0f && e.a().f(0x7ac) == 2.0f,
          "AB §A.2 INIT Easy: STR / DEF -= (n < 20 ? n >> 1 : n - 10) of flag196 (30 -> 20, 7 -> 3); +0x7ac = 2.0");
    ZRig v(0, true, true); v.w.flags[196] = 16; v.step();
    ZRig v2(0, true, true); v2.w.flags[196] = 100; v2.step();
    ZRig v3(0, true, true); v3.w.flags[196] = 16; v3.w.flags[193] = 1; v3.step();
    check(nearf(v.Z.atk, 195.0f, 1e-3f) && nearf(v.Z.def, 220.0f, 1e-3f) && v2.Z.atk == 1.0f && v2.Z.def == 1.0f &&
          v3.Z.atk == 227.0f && v3.Z.def == 252.0f && v.a().f(0x7ac) == 2.0f,
          "AB §A.2 INIT Very Easy: STR / DEF -= pow(n, 1.25) (16 -> 32), floored at 1.0; not in the boss rush (flag193)");

    // ---- the gates
    ZRig g; g.w.flags[156] = 0; g.step();
    check(g.a().phase() == 0 && g.zo.boss_cam.empty(), "AB §A.1 [0x53af3f]: flag156 == 0 -> the plain actor tick, no INIT");
    ZRig g2; g2.w.flags[130] = 0; g2.step();
    check(g2.a().phase() == 0 && g2.zo.plain_tick, "AB §A.2 [0x53ab27]: flag130 == 0 -> FUN_0045f250, no INIT");
    ZRig g3; g3.step(); g3.w.flags[131] = 1; g3.at({-30.0f, 7.0f, 25.0f});
    const int g3t = g3.a().i(0x87c); g3.step();
    check(g3.zo.frozen && veq(g3.a().v(0x808), {-30.0f, 7.0f, 25.0f}) && g3.a().i(0x87c) == g3t,
          "AB §A.1 [0x53b34c]: flag131 -> prev (+0x808) = pos, freeze; no brain, no action player");

    // ---- the fight start and the gauge
    ZRig f; f.h.in.player_pos = {-34.0f, 7.0f, 80.0f};              // out of the action player's sight
    f.step();
    const bool unaware = f.a().phase() == 1;
    f.step();
    check(unaware && f.a().phase() == 2 && f.out.gauge_hp < 0 && f.zo.boss_cam.empty(),
          "AB §A.2: AI phase 1 -> 2 when flag156 != 0; phases 0 / 1 return before TIMERS (no gauge)");
    f.step();
    check(f.cam(1) && f.a().i(0x79c) == 1 && f.a().i(0x7c4) == 119 && f.a().i(0x728) == 0 && f.h.hb(0).queued == 1 &&
          f.h.hb(1).queued == 1 && f.h.hb(0).free == 1 && f.h.hb(1).free == 1,
          "AB §A.2 [0x53ab78]: BossCam(1), +0x7c4 = 120 (-1 in TIMERS), +0x79c = 1, both heads hb+0x71 = 1, hb+0x7f = 1");
    check(f.out.gauge_hp == 6000.0f && f.out.gauge_len == 8000.0f && f.a().f(0x880) == 6000.0f,
          "AB §A.2 [0x53ac4e]: the gauge call when +0x880 != HP: HP 6000, lenScale 8000.0");
    f.a().i(0x728) = 3;                                            // an action that does nothing
    f.step();
    const bool quiet = f.out.gauge_hp < 0;
    f.Z.hp = 5900.0f; f.step();
    check(quiet && f.out.gauge_hp == 5900.0f, "AB §A.2: the gauge is called only when the HP changed");
    ZRig nh; nh.L.heads = nullptr; nh.boot(); nh.step();
    check(nh.cam(1) && nh.a().i(0x79c) == 0, "AB §A.2: with a head missing BossCam(1) repeats and the fight does not start");
}

static void zava_phase1_move() {
    // ---- action 0 -> 1 step 0 -> step 1 in one frame
    for (int diff : {0, 2}) {
        ZRig r(diff); r.boot();
        r.h.forced = {55, 90, 0, 50};                              // r0; v = (-9 + 90*18/100, 0, -9 + 50*18/100) = (7.2, 0, 0)
        const int d0 = r.h.draws;
        r.step();
        const EnemyAi& a = r.a();
        if (diff == 0) {
            check(r.out.section == 300 && r.out.section_force && snd(r.bo, 397) && r.Z.flyer && !r.Z.vert.grounded &&
                  a.i(0x7bc) == 300, "AB §B.3 step 0 / §A.6: pose 300 forced + SE 397, flyer on, airborne");
            check(a.f(0x834) == 7.0f && a.f(0x83c) == 0.005f && a.f(0x838) == 0.1f && a.f(0x818) == 0.12f &&
                  a.f(0x82c) == 0.06f && a.f(0x830) == 0.02f && a.i(0x720) == 5 && (a.i(0x7a0) & 0x141) == 0x141,
                  "AB §B.3 step 0: H 7.0 (the draw x 0.0), sink .005, rise .1, speed .12, ramps .06 / .02, +0x720 = 5, |= 0x141");
            check(r.h.draws - d0 == 4 && vnear(a.v(0x894), {-26.8f, 6.0f, 29.0f}, 1e-4f),
                  "AB §B.3 / §E: 1 discarded draw + 3 per try; candidate = (T.x + v.x, v.y + 6.0, T.z + v.z)");
            check(nearf(a.f(0x814), 0.06f, 1e-6f) && a.mode() == 2 && nearf(a.speed, 0.12f, 1e-6f) && a.i(0x72c) == 1 &&
                  r.Z.floor_y == 6.0f,
                  "AB §B.3 step 1: +0x814 = 0.05 + 0.01, steer 4 + act 0x800 (run at 0.12 / frame); §B.1 (5): +0x318 = 6.0");
        }
        check(a.i(0x8a8) == (diff == 2 ? 4 : 1), diff == 2 ? "AB §B.3 step 0: +0x8a8 = 4 on Nightmare (flag144 == 2)"
                                                           : "AB §B.3 step 0: +0x8a8 = 1 (hits to knock her down)");
    }
    {   // a candidate within 3.0 of the player is rejected, one outside the arena too
        ZRig r; r.boot();
        r.h.in.player_pos = {-20.0f, 7.0f, 29.0f};
        r.h.forced = {0, 50, 0, 50, /* (0,0,0): too near */ 90, 0, 50, /* x = -12.8: outside */ 10, 0, 50 /* x = -27.2 */};
        const int d0 = r.h.draws;
        r.step();
        check(r.h.draws - d0 == 10 && vnear(r.a().v(0x894), {-27.2f, 6.0f, 29.0f}, 1e-4f),
              "AB §B.3: accepted only > 3.0 from the player (xz) and inside the arena box; 3 draws per rejected try");
    }
    {   // the 240-frame fallback
        ZRig r; r.boot(); r.act(1, 1); r.a().i(0x87c) = 241; r.h.forced = {1, 49};
        const int d0 = r.h.draws; r.step();
        ZRig q; q.boot(); q.act(1, 1); q.a().i(0x87c) = 241; q.h.forced = {1, 50}; q.step();
        ZRig s; s.boot(); s.act(1, 1); s.a().i(0x87c) = 240; s.h.forced = {1, 90, 0, 50}; s.step();
        check(r.h.draws - d0 == 2 && veq(r.a().v(0x894), {-41.0f, 6.1f, 30.0f}) && veq(q.a().v(0x894), {-27.0f, 6.1f, 30.0f}) &&
              vnear(s.a().v(0x894), {-26.8f, 6.0f, 29.0f}, 1e-4f),
              "AB §B.3 step 0: +0x87c > 240 -> 2 draws, target (r1 < 50 ? -41 : -27, 6.1, 30)");
    }
    {   // Hugo: 7.0 from every Eye and from his effects 136 / 147 / 148
        ZRig r(0, false, false, 2); r.boot();
        Actor eye; eye.cast_id = 0xb0; eye.pos = {-26.8f, 6.0f, 29.0f}; eye.ai.i(0x7c8) = 1;
        r.w.actors.push_back(&eye);
        r.L.zin.hugo_fx[1] = Vec3{-41.2f, 6.0f, 29.0f};
        r.h.forced = {1, 90, 0, 50, /* on the Eye */ 10, 0, 50, /* (-41.2): on effect 147 */ 50, 0, 90 /* (-34, 6, 36.2) */};
        r.step();
        check(vnear(r.a().v(0x894), {-34.0f, 6.0f, 36.2f}, 1e-4f),
              "AB §B.3: Hugo — a candidate within 7.0 (3-D) of an Eye (cast 0xb0, +0x7c8 != 0) or of effects 136 / 147 / 148 is rejected");
    }
    {   // step 1: arrival and the arena-edge re-pick
        ZRig r; r.boot(); r.act(1, 1, 1); r.fly();
        r.a().set_v(0x894, {-33.0f, 6.0f, 23.0f});                 // 1.41 away
        r.step();
        ZRig q; q.boot(); q.act(1, 1, 1); q.fly(); q.at({-19.5f, 7.0f, 29.0f}); q.a().i(0x87c) = 77;
        q.a().set_v(0x894, {-30.0f, 6.0f, 29.0f});
        q.step();                                                  // heading 0: the probe 3.0 ahead is at x = -16.5
        check(r.a().i(0x728) == 4 && r.a().i(0x72c) == 0 && r.a().i(0x87c) == 1 && q.a().i(0x728) == 1 && q.a().i(0x72c) == 0 &&
              q.a().i(0x87c) == 78,
              "AB §B.3 step 1: within 2.0 (xz) -> action 4; the probe 3.0 ahead outside the arena -> step 0, +0x87c kept");
    }
    // ---- action 4
    {
        auto run = [](int t, int c7c4, int c700, float px, bool easy) {
            ZRig r(0, easy); r.boot(); r.act(1, 4); r.fly();
            r.a().i(0x87c) = t; r.a().i(0x7c4) = c7c4; r.a().cool() = c700;
            r.h.in.player_pos = {px, 7.0f, 22.0f};
            r.step();
            return r.a().i(0x728);
        };
        check(run(29, 0, 0, -20.0f, false) == 4 && run(30, 0, 5, -33.0f, false) == 8 && run(30, 5, 0, -25.0f, false) == 8 &&
              run(30, 5, 3, -25.0f, false) == 6 && run(30, 5, 0, -27.0f, false) == 6 && run(30, 5, 3, -32.5f, false) == 1,
              "AB §B.4: after 30 frames: lightning if +0x7c4 == 0 or (+0x700 == 0 and dist > 8); else shots (dist > 2) or action 1");
        check(run(59, 0, 0, -20.0f, true) == 4 && run(60, 0, 0, -20.0f, true) == 8,
              "AB §B.4: the hover is ToInt(+0x7ac * 30.0) = 60 frames on Very Easy / Easy");
    }
}

static void zava_phase1_attacks() {
    // ---- action 6: the volleys
    for (int diff : {0, 2}) {
        for (int c0 : {0, 15}) {
            ZRig r(diff); r.boot(); r.act(1, 6); r.fly(); r.at({-34.0f, 7.0f, 22.0f});
            r.a().i(0x7a0) = 0x141; r.a().i(0x8a0) = c0;
            r.h.forced = {90, 0, 50};
            std::vector<int> when; int n = 0, se391 = 0, end = -1;
            bool args = true, geom = true, first_ok = false;
            for (int t = 0; t < 70 && end < 0; ++t) {
                const Vec3 z = r.org(); const Vec3 pp = r.h.in.player_pos;
                const int d0 = r.h.draws;
                r.step();
                if (t == 0)
                    first_ok = r.h.draws - d0 == 3 && r.out.section == 0x12f && !r.out.section_force && r.a().i(0x7bc) == 0x12f &&
                               vnear(r.a().v(0x894), {-26.8f, 6.0f, 29.0f}, 1e-4f) && !(r.a().i(0x7a0) & 0x100);
                if (!r.out.bullets.empty()) {
                    when.push_back(t); n += int(r.out.bullets.size()); se391 += snd(r.bo, 391) != nullptr;
                    Vec3 d = pp - z; const float l = len3(d); d = {d.x / l, d.y / l, d.z / l};
                    const std::size_t mid = r.out.bullets.size() / 2;
                    static const float kA7[7] = {45, 30, 15, 0, 15, 30, 45}; static const float kA3[3] = {30, 0, 30};
                    for (std::size_t k = 0; k < r.out.bullets.size(); ++k) {
                        const AiBullet& b = r.out.bullets[k];
                        args = args && b.kind == 2 && b.effect == 310 && b.owner == -1 && b.speed == 0.1f && b.accel == 0.003f &&
                               b.gravity == 0.0f && b.power == 2.0f && b.knock == 0.0f && b.trail_effect == 0 &&
                               b.trail_period == 0 && b.home_period == 0 && b.home_rate == 0.0f && b.f168 == 0.0f &&
                               b.f16c == 0.0f && b.f170 == 0.0f && b.f174 == 0.0f;
                        geom = geom && vnear(b.pos, {z.x + 2 * b.dir.x, z.y + b.dir.y, z.z + 2 * b.dir.z}, 1e-4f) &&
                               nearf(len3(b.dir), 1.0f, 1e-5f) && nearf(b.dir.y, d.y, 1e-5f);
                        const float want = (r.out.bullets.size() == 7 ? kA7 : kA3)[k] * 0.017453292f;
                        const float dotxz = (b.dir.x * d.x + b.dir.z * d.z) / std::sqrt((d.x * d.x + d.z * d.z) * (b.dir.x * b.dir.x + b.dir.z * b.dir.z));
                        geom = geom && nearf(std::acos(std::min(1.0f, dotxz)), want, 2e-3f);
                    }
                    geom = geom && vnear(r.out.bullets[mid].dir, d, 1e-5f) && !vnear(r.out.bullets[0].dir, r.out.bullets.back().dir, 1e-2f);
                }
                if (r.a().i(0x728) == 1) end = t;
            }
            const int per = diff == 2 ? 7 : 3;
            if (c0 == 0) {
                check(first_ok, "AB §B.5 step 0: 3 draws per try, the gated request 0x12f, +0x7bc = 0x12f, bit 0x100 cleared");
                check(when == std::vector<int>{20, 40, 60} && n == 3 * per && se391 == 3 && end == 62,
                      diff == 2 ? "AB §B.5 Nightmare: 3 volleys of 7 (t = 20, 40, 60: 8 <= t <= 61 and +0x8a0 % 20 == 0), SE 391 each"
                                : "AB §B.5: 3 volleys of 3 (t = 20, 40, 60: 8 <= t <= 61 and +0x8a0 % 20 == 0), SE 391; action 1 after t = 61");
                check(args, "AB §B.5 [0x537e46 / 0x537f83]: BUL(kind 2, effect 310, f0 0.1, f1 0.003, f2 0, f3 power 2.0, f4..f12 0, out 0)");
                check(geom, diff == 2 ? "AB §B.5: the fan -45..45 by 15 deg about dirP (y kept), pos = Z + (2 d.x, d.y, 2 d.z)"
                                      : "AB §B.5: the fan -30 / 0 / 30 deg about dirP (y kept), pos = Z + (2 d.x, d.y, 2 d.z)");
            } else if (diff == 0) {
                check(when == std::vector<int>{25, 45} && n == 6,
                      "AB §B.5: 2 volleys when no multiple of 20 of +0x8a0 falls in the window's first 14 frames");
            }
        }
    }
    // ---- action 8: lightning
    for (int diff : {0, 2}) {
        ZRig r(diff); r.boot(); r.act(1, 8); r.fly(); r.a().i(0x7a0) = 0x141; r.a().i(0x7bc) = 300; r.sec = 300;
        std::vector<int> strikes; bool strike_ok = true, start_ok = false, kill_ok = false; int end = -1, circle = 0;
        r.h.fixed = 77;                                            // R(301) = 77
        for (int t = 0; t <= 121 && end < 0; ++t) {
            r.h.in.player_pos = {-30.0f + 0.05f * float(t), 9.0f, 27.0f};   // the strike point is not latched
            const Vec3 pp = r.h.in.player_pos;
            const int d0 = r.h.draws;
            r.step();
            if (t == 0) {
                const Boss06Effect* c = zfx(r.bo, 586);
                circle = r.a().i(0x88c);
                start_ok = r.out.section == 0x12d && !r.out.section_force && snd(r.bo, 392) && c && c->call == Boss06Call::kInline &&
                           !c->stat && c->power == 0.0f && c->knock == 1.0f && c->or_1dc == 0x108 && c->parent == Boss06Parent::kZava &&
                           c->at_mode == 2 && circle == c->handle && circle != 0 && r.a().f(0x834) == 5.0f &&
                           r.a().f(0x83c) == 0.008f && !(r.a().i(0x7a0) & 0x100);
            }
            if (const Boss06Effect* s = zfx(r.bo, 420)) {
                strikes.push_back(t);
                const Boss06Sound* se = snd(r.bo, 393);
                strike_ok = strike_ok && veq(s->pos, {pp.x, 6.0f, pp.z}) && s->stat && s->power == 10.0f && s->knock == 1.0f &&
                            s->rtype == 0 && s->flag201 && se && veq(se->pos, s->pos);
                if (t == 30) kill_ok = has(r.bo.kill_ids, 586) && r.out.section == 0x132 && r.out.section_force && r.a().i(0x88c) == 0;
            }
            if (r.a().i(0x728) == 1) {
                end = t;
                strike_ok = strike_ok && r.h.draws - d0 == 1 && r.a().cool() == 77 + 120 - 1 && r.a().i(0x7c4) == 599;
            }
        }
        if (diff == 0) {
            check(start_ok, "AB §B.7 step 0: gated 0x12d + SE 392, the inline cast circle 586 (no stat, |= 0x108) attached mode 2, H 5.0, sink 0.008");
            check(kill_ok, "AB §B.7 step 1 (t = 30): KillSlot(586), ForceSection(0x132)");
            check(strikes == std::vector<int>{30} && strike_ok && end == 120,
                  "AB §B.7: one FX2 420 (stat, power 10, knock 1, +0x201) + SE 393 at (P.x, 6.0, P.z) at t = 30; t = 120: +0x700 = R(301) + 120, +0x7c4 = 600");
        } else {
            check(strikes == std::vector<int>{30, 60, 90, 120} && strike_ok && end == 120,
                  "AB §B.7 / C8: Nightmare strikes again at t = 60, 90, 120");
        }
    }
    // ---- the dodge and action 7
    {
        auto dodge = [](int ch, int psec, float dist, bool easy, int bits = 0x141) {
            ZRig r(0, easy, false, ch); r.boot(); r.act(1, 4); r.fly(); r.a().i(0x7a0) = bits;
            r.h.in.player_pos = {-34.0f + dist, 7.0f, 22.0f}; r.h.in.player_section = psec;
            r.step();
            return r.a().i(0x728) == 7 || (r.a().i(0x728) == 1 && r.a().f(0x818) == 0.3f);
        };
        check(dodge(1, 0x10f, 3.9f, false) && dodge(1, 0x10c, 3.9f, false) && !dodge(1, 0x111, 3.9f, false) &&
              !dodge(1, 0x10f, 4.1f, false) && dodge(3, 0x111, 3.9f, false) && !dodge(3, 0x10f, 3.9f, false) &&
              !dodge(0, 0x10f, 3.9f, false) && !dodge(0, 0x111, 3.9f, false),
              "AB §B.1 (3) / C4: Yunica in section 0x10f / 0x10c or Toal in 0x111 within 4.0 (xz) -> action 7; Adol never");
        check(!dodge(1, 0x10f, 3.9f, true) && !dodge(1, 0x10f, 3.9f, false, 0x041),
              "AB §B.1 (3): no dodge on Very Easy / Easy, nor without bit 0x100 (armed by action 1 only)");
        auto hugo = [](float eye_d) {
            ZRig r(0, false, false, 2); r.boot(); r.act(1, 4); r.fly(); r.a().i(0x7a0) = 0x141;
            Actor eye; eye.cast_id = 0xb0; eye.height = 0.0f; eye.pos = {-34.0f, 7.0f + eye_d, 22.0f}; eye.ai.i(0x7c8) = 1;
            r.w.actors.push_back(&eye);
            r.step();
            return r.a().i(0x728) != 4;
        };
        check(hugo(2.9f) && !hugo(3.1f), "AB §B.1 (3): Hugo — an Eye of Fact within 3.0 (3-D) -> action 7");
        ZRig r; r.boot(); r.act(1, 7); r.fly(); r.a().i(0x7a0) = 0x141;
        r.h.forced = {99, 7, 50, 50};                              // v = (14.7, 0, 0) -> d = (1, 0, 0); r = 50*3/100 + 5 = 6.5
        const int d0 = r.h.draws;
        r.step();
        check(r.h.draws - d0 == 4 && vnear(r.a().v(0x894), {-27.5f, 7.0f, 22.0f}, 1e-4f) && r.a().f(0x818) == 0.3f &&
              nearf(r.a().f(0x814), 0.21f, 1e-6f) && !(r.a().i(0x7a0) & 0x101) && r.a().i(0x72c) == 1 && r.a().mode() == 2 &&
              nearf(r.a().speed, 0.3f, 1e-6f),
              "AB §A.7 / §B.6: the dash point = Z + normalize(v) * (R(100) * 3 / 100 + 5), 4 draws a try; speed 0.3, turn 0.2 + 0.01, bits 0x101 cleared");
        r.at({-27.8f, 7.0f, 22.0f}); r.step();
        ZRig q; q.boot(); q.act(1, 7, 1); q.fly(); q.at({-19.0f, 7.0f, 22.0f}); q.a().set_v(0x894, {-30.0f, 7.0f, 22.0f}); q.step();
        check(r.a().i(0x728) == 1 && q.a().i(0x728) == 1,
              "AB §B.6 step 1: ends within 0.5 of the point, or when the probe 2.0 ahead leaves the arena -> action 1");
        // Hugo's dash point: after a rejected candidate the next in-arena one clear of the Eyes (5.0) wins.
        ZRig hq(0, false, false, 2); hq.boot(); hq.act(1, 7); hq.fly();
        Actor eye; eye.cast_id = 0xb0; eye.height = 0.0f; eye.pos = {-27.5f, 7.0f, 22.0f}; eye.ai.i(0x7c8) = 1;
        hq.w.actors.push_back(&eye);
        hq.h.forced = {99, 7, 50, 50, 50, 7, 99, 0};               // then v = (0, 0, 14.7) -> (0, 0, 1) * 5.0
        hq.step();
        check(vnear(hq.a().v(0x894), {-34.0f, 7.0f, 27.0f}, 1e-4f),
              "AB §A.7: Hugo — a dash point within 5.0 of an Eye is passed over (4 draws per try)");
    }
}

static void zava_knockdown() {
    // ---- the first damaging hit while airborne (the fourth on Nightmare)
    ZRig r; r.boot(); r.act(1, 4); r.fly(); r.a().i(0x7a0) = 0x141; r.a().i(0x8a8) = 1; r.a().i(0x88c) = 5; r.a().i(0x7c4) = 500;
    r.a().cool() = 50;
    r.step(true);
    const EnemyAi& a = r.a();
    check(a.i(0x728) == 11 && a.i(0x72c) == 1 && (a.i(0x7a0) & 0x51) == 0x10 && !r.Z.flyer && snd(r.bo, 398) &&
          has(r.bo.kill_ids, 586) && a.i(0x88c) == 0 && a.i(0x8a8) == 0 && a.f(0x834) == 0.0f && a.f(0x83c) == 0.05f &&
          veq(a.v(0x894), {-34.0f, 6.0f, 22.0f}) && a.f(0x814) == 0.0f && a.mode() == 0,
          "AB §B.1 (1) / §A.8 step 0: the first hit while airborne -> 10 -> 11: SE 398, flyer off, bit 0x10 (0x41 cleared), circle 586 killed, stop(1)");
    ZRig nm(2); nm.boot(); nm.act(1, 4); nm.fly(); nm.a().i(0x7a0) = 0x141; nm.a().i(0x8a8) = 4; nm.a().i(0x7c4) = 500; nm.a().cool() = 50;
    int downs = 0, at = -1;
    for (int k = 1; k <= 4; ++k) { nm.a().i(0x87c) = 0; nm.step(true); if (nm.a().i(0x728) == 11 && at < 0) { at = k; ++downs; } }
    check(at == 4 && downs == 1, "AB §B.1 / §A.9: Nightmare (+0x8a8 = 4): the fourth damaging hit knocks her down");
    auto blocked = [](int which) {
        ZRig q; q.boot(); q.act(1, 4); q.fly(); q.a().i(0x7a0) = 0x141; q.a().i(0x8a8) = 1; q.a().i(0x7c4) = 500; q.a().cool() = 50;
        if (which == 0) q.a().i(0x7c8) = 5;
        if (which == 1) q.Z.guard399 = true;
        if (which == 2) q.Z.vert.grounded = true;
        if (which == 3) q.a().i(0x7a0) |= 4;
        q.step(which != 4);
        return q.a().i(0x728) == 4;
    };
    check(blocked(0) && blocked(1) && blocked(2) && blocked(3) && blocked(4),
          "AB §A.9 FUN_00536d10: no knock-down with +0x7c8 != 0, +0x399, grounded, bit 4, or without a damaging hit");
    // ---- on the ground
    for (int k = 0; k < 10; ++k) r.step();                         // still falling: nothing
    const bool falling = a.i(0x72c) == 1 && r.h.hb(0).pattern != 3;
    r.Z.vert.grounded = true;
    r.step();                                                      // the landing frame L
    check(falling && snd(r.bo, 399) && r.out.section == 9 && r.out.section_force && a.i(0x72c) == 2 && a.i(0x87c) == 1,
          "AB §A.8 step 1: the landing: SE 399, ForceSection(9), +0x87c = 0");
    int chase_at = -1, up_at = -1; bool mid = true;
    r.Z.hp = 4000.0f;                                              // below 70 %: the phase change waits for the get-up
    r.h.fixed = 100;
    for (int k = 1; k <= 130 && up_at < 0; ++k) {
        const int d0 = r.h.draws;
        r.step(true);                                              // further hits change nothing (bit 0x10)
        if (chase_at < 0 && r.h.hb(0).pattern == 3 && r.h.hb(1).pattern == 3) chase_at = k;
        if (a.i(0x728) == 0) {
            up_at = k;
            const Boss06Effect* s = zfx(r.bo, 311);
            mid = mid && s && veq(s->pos, {-34.0f, 6.0f + 0.05f, 22.0f}) && veq(s->scale, {1.5f, 1.5f, 1.5f}) && s->stat &&
                  s->power == 0.0f && s->knock == 6.0f && s->rtype == 2 && snd(r.bo, 167) && !(a.i(0x7a0) & 0x10) &&
                  a.i(0x7c8) == 59 && r.h.hb(0).queued == 1 && r.h.hb(1).queued == 1 && a.cool() == 100 + 120 - 1 &&
                  r.h.draws - d0 == 1 && a.i(0x79c) == 1;
        } else {
            mid = mid && a.i(0x79c) == 1;
        }
    }
    check(chase_at == 61, "AB §B.8: Normal+ — on the first frame with +0x87c > 60 both heads get SetPattern(3) (chase)");
    check(up_at == 120 && mid,
          "AB §A.8 step 2 / §B.8: L + 120: bit 0x10 off, +0x7c8 = 60, FX2 311 (scale 1.5, knock 6, rtype 2) at feet + 0.05, SE 167, heads hb+0x71 = 1, +0x700 = R(301) + 120");
    r.step();
    check(a.i(0x79c) == 2 && a.i(0x728) == 0 && a.i(0x72c) == 0,
          "AB §B.1 (2): HP < 70 % of max -> +0x79c = 2 on the first prologue outside the drop window");
    ZRig e(0, true); e.boot(); e.act(1, 11, 2); e.Z.vert.grounded = true; e.a().i(0x7a0) = 0x10; e.a().i(0x87c) = 1;   // as the landing leaves it
    int eup = -1; bool echase = false;
    for (int k = 1; k <= 250 && eup < 0; ++k) { e.step(); echase = echase || e.h.hb(0).pattern == 3; if (e.a().i(0x728) == 0) eup = k; }
    check(eup == 240 && !echase, "AB §A.8 / §B.8: Very Easy / Easy lie 240 frames (+0x7ac = 2.0) and the heads do not chase");
    ZRig p; p.boot(); p.act(1, 4); p.Z.hp = 4199.0f; p.step();
    ZRig p2; p2.boot(); p2.act(1, 4); p2.Z.hp = 4200.0f; p2.a().i(0x87c) = 1; p2.step();
    check(p.a().i(0x79c) == 2 && p2.a().i(0x79c) == 1, "AB §B.1 (2): max HP * 0.7 > HP (4200 of 6000) switches to phase 2");
}

// The natural cubic spline of CG §C.9 through 4 knots, evaluated independently (doubles), one component.
static double spline_ref(const double k[4], double u) {
    const double r1 = 3.0 * (k[0] - 2 * k[1] + k[2]), r2 = 3.0 * (k[1] - 2 * k[2] + k[3]);
    const double c2 = (r2 - r1 / 4.0) / (4.0 - 0.25), c1 = (r1 - c2) / 4.0;
    const double c[4] = {0, c1, c2, 0};
    double s = 3.0 * u; int i = int(std::floor(s)); if (i > 2) i = 2; s -= i;
    const double d = (c[i + 1] - c[i]) / 3.0, b = (k[i + 1] - k[i]) - c[i] - d;
    return ((d * s + c[i]) * s + b) * s + k[i];
}

static void zava_mount() {
    ZRig r; r.boot(); r.act(2, 0); r.fly(); r.at({-36.0f, 13.0f, 30.0f}); r.a().i(0x7a0) = 0x141; r.a().i(0x7bc) = 301; r.sec = 301;
    r.h.hb(1).move = 5;                                            // a stunned head is aborted too
    r.step();
    const EnemyAi& a = r.a();
    const Boss06Effect* bar = zfx(r.bo, 426);
    check(a.i(0x7c0) == 1 && r.cam(3) && bar && bar->stat && bar->power == -10.0f && bar->knock == 3.0f && bar->rtype == 2 &&
          bar->parent == Boss06Parent::kZava && bar->at_mode == 2 && a.i(0x88c) == bar->handle && snd(r.bo, 167),
          "CG §C.2 step 0: first cycle = the closer head (a), BossCam(3); FX2 426 (stat, power -10, knock 3, rtype 2) attached mode 2, SE 167");
    check(r.h.hb(0).move == 9 && r.h.hb(1).move == 0 && r.out.section == 300 && r.out.section_force && snd(r.bo, 397) &&
          (r.Z.flags12c & 0x1040u) == 0x1040u && r.Z.guard399 && r.Z.def == 999.0f && (a.i(0x7a0) & 0x65) == 0x24 &&
          a.f(0x834) == 0.0f && a.f(0x838) == 0.0f && a.f(0x83c) == 0.0f && r.Z.floor_y == 6.0f && a.i(0x72c) == 1,
          "CG §C.2 step 0: StartMove(H, 9) / (Other, 0) after AbortMove, Anim(300), +0x12c |= 0x1040, +0x399 = 1, DEF 999, +0x7a0 = (|0x24) & ~0x41");
    int f = 0;
    while (a.i(0x72c) == 1 && f < 100) { r.step(); ++f; }
    const Vec3 p0{-36.0f, 13.0f, 30.0f}, p3 = r.h.hb(0).pos;
    check(f == 60 && snd(r.bo, 394) && r.Z.alpha_to == 0.0f && r.Z.alpha_step == 0.1f && !(a.i(0x7a0) & 1) && vnear(r.org(), p0, 1e-5f),
          "CG §C.2 step 1: after 60 frames the spline is built, SE 394, +0x4dc / +0x4e0 = 0 / 0.1; t87 = 0 -> pts[0]");
    int trail = zfx(r.bo, 423) != nullptr; bool mid_ok = false, pinned = true;
    for (int t = 1; t <= 120; ++t) {
        r.step();
        trail += zfx(r.bo, 423) != nullptr;
        if (t == 60) {
            const double kx[4] = {p0.x, p0.x, double(p3.x) + 2.0, p3.x}, ky[4] = {p0.y, double(p0.y) + 7.0, double(p3.y) + 7.0, p3.y};
            mid_ok = nearf(r.org().x, float(spline_ref(kx, 0.5)), 2e-3f) && nearf(r.org().y, float(spline_ref(ky, 0.5)), 2e-3f) &&
                     (r.Z.flags12c & 1u);
        }
        if (t < 120) pinned = pinned && !(a.i(0x7a0) & 2);
    }
    check(mid_ok && trail > 30 && trail < 50,
          "CG §C.2 step 2 / §C.9: e = (cos(t87 / 120 * pi + pi) + 1) / 2; t87 = 60 -> the spline's u = 0.5; trail FX2 423 every 3rd +0x8a0");
    check(pinned && (a.i(0x7a0) & 2) && has(r.bo.kill_ids, 426) && r.h.hb(0).move == 10 && r.h.hb(0).queued == 2 &&
          r.h.hb(1).queued == 1 && r.cam(4) && r.out.set_durable == 1 && r.Z.alpha_step == 0.01f && a.i(0x72c) == 3 &&
          vnear(r.org(), {p3.x, p3.y - 0.5f, p3.z}, 1e-4f),
          "CG §C.2 step 2: t87 = 120 (121 frames): riding (bit 2), Kill(426), StartMove(H, 10), H hunts (2) / other roams (1), BossCam(4), Durable; pinned at body_efc - 0.5");
    r.step();
    const bool waits = a.i(0x72c) == 3 && r.h.hb(0).shield == 0;
    r.h.hb(0).move = 0;                                            // move 10 finished
    r.step();
    int orbs = 0; for (const Boss06Effect& e : r.bo.effects) orbs += e.id == 424 && e.head == 0;
    check(waits && r.h.hb(0).shield == 1 && r.h.hb(0).phase2 == 1 && r.h.hb(0).rearm == 60 && r.h.hb(0).lives == 4 &&
          r.h.hb(1).shield == 0 && r.h.hb(1).phase2 == 0 && r.h.hb(1).lives == 0 && orbs == 4,
          "CG §C.2 step 3 / C2: waits for move 10; the RIDDEN head only: hb+0x80 = +0x81 = 1, +0x7a = 60, 4 lives, 4 orbiters 424");
    check(r.Z.def == 262.08f && !r.Z.guard399 && !(a.i(0x7a0) & 0x20) && a.i(0x7a4) == 1 && r.h.z.hits == 0 && a.i(0x72c) == 4 &&
          r.Z.hurt_shape == 1 && r.Z.hurt_radius == 2.0f && r.Z.hurt_half_h == 2.0f,
          "CG §C.2 step 3 / C12: DEF 262.08, +0x399 = 0, bit 0x20 off, +0x7a4++, +0x7a8 = 0, +0x7c = 1, +0x68 = +0x78 = 2.0");
    // ---- the hit counter and the lives hand-off
    r.step(true); r.step(false); r.step(true);
    const bool counted = r.h.z.hits == 2;
    r.h.hb(0).move = 12; r.step(true);
    const bool not12 = r.h.z.hits == 2;
    r.h.hb(0).move = 0; r.h.hb(0).rearm = 0;
    Boss06Output ho = r.h.step();                                  // the heads' frame: the life tick reads Z+0x7a8
    check(counted && not12 && r.h.hb(0).lives == 3 && r.h.z.hits == 0 && r.h.hb(0).move == 12 && r.h.hb(1).lives == 0,
          "CG §C.2 step 4 [0x539185] / D §4.1: +0x7a8 counts damage ticks while riding (not in move 12); at the threshold (2) the ridden head burns a life and zeroes it");
    // ---- the throw (the heads' move 8) and the recovery
    ho = Boss06Output{};
    Boss06ZavaThrow th; th.head = 0; th.pos = {-37.0f, 9.0f, 27.0f}; th.vel = {0.3f, 0.0f, -0.4f};
    ho.zava_throw = th;
    AiOut eo;
    brains::m870_heads_events(r.Z, r.env, ho, eo);
    check(eo.section == 0x130 && eo.section_force && a.i(0x7bc) == 0x130 && !(a.i(0x7a0) & 3) && !r.Z.flyer && !r.Z.guard399 &&
          r.Z.def == 252.0f && r.Z.hurt_shape == 0 && r.Z.hurt_radius == 0.6f && r.Z.hurt_half_h == 0.0f && eo.set_durable == 0 &&
          veq(r.org(), th.pos) && veq(r.Z.mover.k1, th.vel) && !(r.Z.flags12c & 0x1041u) && r.Z.alpha_to == 1.0f && r.Z.alpha_step == 1.0f,
          "E2 §2 (the actor side of move 8): section 0x130, bits 3 off, flyer / guard off, DEF 252.0, sphere 0.6, pos / knock from the head");
    r.sec = 0x130; r.Z.vert.grounded = true;
    r.step(true);
    const bool s5 = a.i(0x72c) == 5 && r.h.z.hits == 1 && r.out.section < 0;
    int g = 1; while (a.i(0x72c) == 5 && g < 400) { r.step(); ++g; }
    const Boss06Effect* sh = zfx(r.bo, 311);
    check(s5 && g == 241 && a.i(0x7c8) == 59 && r.out.section == 306 && r.out.section_force && sh && sh->knock == 6.0f &&
          sh->rtype == 2 && sh->stat && a.i(0x88c) == sh->handle && snd(r.bo, 167),
          "CG §C.2 steps 4 -> 5 / C3: thrown off: +0x7a8 = 0 then ++ on a hit; after 120 * (1 + 0x7ac) = 240 frames: +0x7c8 = 60, Anim(306), FX2 311, SE 167");
    g = 0; while (a.i(0x728) == 0 && g < 100) { r.step(); ++g; }
    check(g == 30 && has(r.bo.kill_ids, 426) && a.i(0x88c) == 0 && a.i(0x728) == 7 && a.i(0x72c) == 0,
          "CG §C.2 step 6: 30 frames, Kill(426) on the handle slot (the 311's handle), -> action 7");
    // ---- a ridden head carries her (the tail's ride), and the swap's re-seat
    ZRig q; q.boot(); q.act(2, 0, 4); q.a().i(0x7a0) = 2; q.a().i(0x7c0) = 2; q.h.hb(1).shield = 1;
    q.h.hb(1).pos = {-25.0f, 8.0f, 33.0f};
    q.step();
    check(vnear(q.org(), {-25.0f, 7.5f, 33.0f}, 1e-5f) && q.a().i(0x708) == 1 && q.a().action() == -1,
          "AB §A.5 FUN_0053aa90 / CG §C.2 step 4: riding -> Z.pos = body_efc - (0, 0.5, 0) each frame; +0x710 = -1");
    q.h.hb(1).pos = {-40.0f, 8.0f, 20.0f}; q.h.feed();
    Boss06Output hs; hs.zava_reseat = true; AiOut eo2;
    brains::m870_heads_events(q.Z, q.env, hs, eo2);
    check(vnear(q.org(), {-40.0f, 7.5f, 20.0f}, 1e-5f), "E2 §5: the swap's FUN_0053aa90 re-seats her on the mounted head");
    // ---- the second cycle takes the other head
    ZRig s; s.boot(); s.act(2, 0); s.fly(); s.a().i(0x7c0) = 1; s.step();
    check(s.a().i(0x7c0) == 2 && !s.cam(3) && s.h.hb(1).move == 9, "CG §C.2 step 0: later cycles alternate (Other), no BossCam(3)");
}

static void zava_phase2_actions() {
    // ---- action 7
    auto a7 = [](float hp, bool head_out, int& next, ZRig& r) {
        r.boot(); r.act(2, 7); r.Z.hp = hp; r.Z.vert.grounded = true; r.at({-34.0f, 7.0f, 29.0f}); r.a().i(0x7a0) = 4;
        if (head_out) r.h.hb(0).pos.x = -51.0f;
        r.h.forced = {99, 7, 50, 0};                               // the point = centre + (1, 0, 0) * 5.0
        r.step();
        next = r.a().i(0x728);
    };
    {
        ZRig r; int nx = 0; const int d0 = r.h.draws; a7(6000.0f, false, nx, r);
        const EnemyAi& a = r.a();
        check(r.h.draws - d0 == 4 && vnear(a.v(0x894), {-29.0f, 6.0f, 29.0f}, 1e-4f) && r.Z.flyer && !r.Z.vert.grounded &&
              a.f(0x818) == 0.12f && a.f(0x82c) == 0.06f && a.f(0x830) == 0.02f && a.f(0x834) == 6.0f && a.f(0x7dc) == 6.0f &&
              a.f(0x83c) == 0.05f && a.f(0x838) == 0.1f && (a.i(0x7a0) & 0x45) == 0x41 && a.i(0x8a8) == 4 && a.f(0x814) == 0.5f &&
              r.out.section == 300 && !r.out.section_force && nx == 7 && a.mode() == 2,
              "CG §C.3 step 0: WarpPoint((-34, 6, 29)) (AB §A.7: 4 draws a try), Anim(300, gated), flyer, speed .12, H 6, +0x8a8 = 4, (|0x41) & ~4, turn .5");
        r.at({-30.5f, 9.0f, 29.5f}); r.step();
        ZRig lo; int nlo = 0; a7(1499.0f, false, nlo, lo); lo.at({-30.5f, 9.0f, 29.5f}); lo.step();
        ZRig ho; int nho = 0; a7(6000.0f, true, nho, ho); ho.at({-30.5f, 9.0f, 29.5f}); ho.step();
        check(r.a().i(0x728) == 8 && lo.a().i(0x728) == 20 && ho.a().i(0x728) == 12,
              "CG §C.3 step 1: within 2.0 (planar): a head outside the arena -> 12; HP < 25 % of max -> 20; else 8");
    }
    // ---- action 8
    {
        static const int kHits[5] = {1, 2, 4, 4, 6};
        bool hits_ok = true;
        for (int lvl = 0; lvl < 5; ++lvl) {
            ZRig r(lvl >= 3 ? lvl - 2 : 0, lvl <= 1, lvl == 0); r.boot(); r.act(2, 8); r.fly(); r.a().i(0x7c0) = 1; r.step();
            hits_ok = hits_ok && r.a().i(0x8a8) == kHits[lvl];
        }
        ZRig r; r.boot(); r.act(2, 8); r.fly(); r.a().i(0x7c0) = 1; r.a().i(0x7bc) = 300; r.sec = 300;
        r.step();
        const EnemyAi& a = r.a();
        const Boss06Effect* c = zfx(r.bo, 586);
        check(hits_ok && r.out.section == 301 && !r.out.section_force && snd(r.bo, 392) && a.f(0x834) == 8.0f && a.f(0x7dc) == 8.0f &&
              c && c->call == Boss06Call::kFx && !c->stat && c->knock == 1.0f && c->parent == Boss06Parent::kZava && c->at_mode == 1 &&
              a.i(0x88c) == c->handle && r.h.hb(1).move == 11 && r.h.hb(0).queued == 2,
              "CG §C.4 step 0 / C4: Anim(301) + SE 392, H 8, +0x8a8 = {1,2,4,4,6}[diff], FX 586 attached mode 1, Other(H) -> move 11, H hunts");
        int t = 1; bool low = false;
        for (; t <= 600 && a.i(0x728) == 8; ++t) {
            r.step();
            if (t == 119) low = a.f(0x834) == 8.0f;
            if (t == 120) low = low && a.f(0x834) == 4.0f && a.f(0x7dc) == 4.0f && a.f(0x83c) == 0.01f;
        }
        check(low && t == 601 && a.i(0x728) == 12 && has(r.bo.kill_ids, 586) && a.i(0x88c) == 0 && r.h.hb(1).step >= 0x80,
              "CG §C.4: from t87 = 120: H 4, sink .01; at 600: Kill(586), AbortMove(O), -> action 12");
    }
    // ---- actions 10 / 11 / 12
    {
        ZRig r; r.boot(); r.act(2, 8, 1); r.fly(); r.a().i(0x7c0) = 1; r.a().i(0x7a0) = 0x41; r.a().i(0x8a8) = 1; r.a().i(0x88c) = 9;
        r.h.hb(1).move = 11; r.h.hb(1).step = 4;
        r.step(true);
        const EnemyAi& a = r.a();
        check(a.i(0x728) == 11 && (a.i(0x7a0) & 0x10) && r.h.hb(1).step >= 0x80 && has(r.bo.kill_ids, 586) && snd(r.bo, 398) && !r.Z.flyer,
              "CG §C.1 / §C.5: the hits-left counter at 0 -> action 10: a head in move 11 (step < 0x80) is aborted, Kill(586), the drop");
        r.Z.vert.grounded = true;
        r.h.forced = {99, 7, 50, 0};
        int k = 0; while (a.i(0x728) == 11 && k < 200) { r.step(); ++k; }
        check(k == 121 && a.i(0x728) == 12 && a.i(0x72c) == 1 && (r.Z.flags12c & 0x40u) && (a.i(0x7a0) & 0x51) == 0x41 &&
              a.i(0x8a8) == 4 && r.Z.flyer && r.Z.floor_y == 6.0f && vnear(a.v(0x894), {-29.0f, 6.0f, 29.0f}, 1e-4f) &&
              r.h.hb(0).pattern != 3,
              "CG §C.5: action 11 = the knock-down (120 frames down, no head chase in phase 2), then action 12 step 0: warp point, |= 0x41, +0x12c |= 0x40");
        r.at({-29.5f, 9.0f, 29.0f}); r.h.hb(1).move = 0; r.h.hb(0).move = 11; r.step();
        const bool w11 = a.i(0x728) == 12 && a.i(0x72c) == 2;
        r.h.hb(0).move = 3; r.step();
        const bool wh = a.i(0x728) == 12;
        r.h.hb(0).move = 0; r.step();
        const bool to0 = a.i(0x728) == 0 && a.i(0x7c0) == 1;
        r.step();
        check(w11 && wh && to0 && a.i(0x7c0) == 2,
              "CG §C.5 action 12 step 2: waits while a head is in move 11 and until the designated head is idle, then action 0 (the other head)");
    }
    // ---- the hover-height avoidance
    {
        ZRig r; r.boot(); r.act(2, 3); r.a().i(0x7a0) = 0x40; r.a().f(0x7dc) = 4.0f;
        r.h.hb(0).pos = {-34.0f, 8.0f, 28.5f}; r.h.hb(1).pos = {-34.0f, 20.0f, 23.0f};   // a 6.5 away, b 1.0
        r.step();
        const float near_a = r.a().f(0x834);
        r.h.hb(0).pos.z = 29.5f; r.step();                         // a 7.5 away: b's result does not survive
        const float far_a = r.a().f(0x834);
        r.a().f(0x7dc) = 9.0f; r.step();
        check(near_a == 14.0f && far_a == 6.0f && r.a().f(0x834) == 9.0f,
              "CG §C.1: bit 0x40: H = a within radius + 4 (planar) ? a.y + 6 : 6 (b is written first, a wins), floored by +0x7dc");
    }
}

static void zava_big_attack() {
    {   // the bounce
        ZRig r; r.boot(); r.act(2, 20); r.fly(); r.h.hb(0).move = 17; r.step();
        check(r.a().i(0x728) == 7 && r.Z.def != 999.0f, "CG §C.6 step 0 / C6: a head in move 11 / 17 -> back to action 7");
    }
    static const int kRing[5] = {0, 4, 8, 10, 12};
    for (int lvl : {2, 0, 1, 3, 4}) {
        ZRig r(lvl >= 3 ? lvl - 2 : 0, lvl <= 1, lvl == 0); r.boot(); r.act(2, 20); r.fly(); r.at({-34.0f, 12.0f, 32.0f});
        r.a().i(0x7a0) = 0x45; r.a().i(0x7c0) = 1; r.a().i(0x7bc) = 300; r.sec = 300; r.Z.hp = 1000.0f;
        r.h.in.player_pos = {-32.5f, 7.0f, 29.0f}; r.h.in.player_grounded = true;   // 1.5 from the centre
        r.h.fixed = 10;                                            // Nightmare: R(21) + 40 = 50
        r.step();
        const EnemyAi& a = r.a();
        if (lvl == 2) {
            const Boss06Effect* bar = zfx(r.bo, 426);
            check(r.h.hb(0).busy == 1 && r.h.hb(1).busy == 1 && r.Z.def == 999.0f && r.Z.flyer && r.Z.guard399 &&
                  (r.Z.flags12c & 0x40u) && (a.i(0x7a0) & 0x21) == 0x20 && bar && bar->call == Boss06Call::kFx && !bar->stat &&
                  bar->parent == Boss06Parent::kZava && bar->at_mode == 1 && veq(a.v(0x894), {-34.0f, 6.0f, 32.5f}),
                  "CG §C.6 step 0: heads busy, DEF 999, Durable, +0x398 = +0x399 = 1, bit 0x20, FX 426 (no stat) attached mode 1, waypoint (-34, 6, 32.5)");
            check(a.i(0x72c) == 2 && r.out.section == 301 && r.h.hb(0).move == 17 && r.h.hb(1).move == 17 && r.out.set_durable == 1,
                  "CG §C.6 step 1: within 1.0 (planar): Anim(301), both heads AbortMove + StartMove(17)");
        }
        int f = 0; while (a.i(0x72c) == 2 && f < 200) { r.step(); ++f; }
        // `f` frames after step 1; this frame is T = 0 (t87 = 0 in step 4).
        if (lvl == 2) {
            const Boss06Effect* col = zfx(r.bo, 533);
            const Boss06Sound* s308 = snd(r.bo, 308);
            check(f == 120 && col && veq(col->pos, {-34.0f, 6.01f, 29.0f}) && !col->stat && col->flag203 && r.bo.shake &&
                  r.bo.shake->first == 1000 && veq(r.bo.shake->second, {0.05f, 0.05f, 0.05f}) && r.zo.fog && r.zo.fog->first == 2 &&
                  r.zo.fog->second == 0.01f && s308 && veq(s308->pos, {-34.0f, 6.0f, 29.0f}) && a.i(0x72c) == 4 && a.f(0x7b0) == 60.0f,
                  "CG §C.6 step 2 (T = 0, after 120 frames): FX2 533 at (-34, 6.01, 29) (+0x203), Shake(1000, .05), fog 2 at 0.01, SE 308");
        }
        std::vector<int> rings, strikes, erupt, puffs; int ring_fx = 0; bool ring_ok = true, suck_ok = true, blast_ok = false, erupt_ok = false;
        int end6 = -1, end12 = -1; bool s6_ok = false;
        const int barrier = a.i(0x88c), column = r.L.fx533;
        for (int T = 1; T <= 800 && end12 < 0; ++T) {
            if (T == 200) r.h.in.player_grounded = false;
            r.step();
            if (a.i(0x72c) == 4 || (a.i(0x72c) == 5 && T == 360)) {
                int n350 = 0; const Boss06Effect* first = nullptr; const Boss06Effect* puff = nullptr;
                for (const Boss06Effect& e : r.bo.effects)
                    if (e.id == 350) {
                        if (e.pos.y == 6.01f) { puff = &e; continue; }
                        if (!first) first = &e;
                        ++n350;
                        ring_ok = ring_ok && e.stat && e.power == 2.0f && e.knock == 2.0f && e.rtype == 2 && e.pos.y == 6.1f &&
                                  nearf(std::sqrt((e.pos.x + 34) * (e.pos.x + 34) + (e.pos.z - 29) * (e.pos.z - 29)), float(T) / 20.0f, 1e-3f);
                    }
                if (n350) { rings.push_back(T); ring_fx += n350; ring_ok = ring_ok && n350 == kRing[lvl] && nearf(first->pos.x, float(T) / 20.0f - 34.0f, 1e-4f); }
                if (puff) { puffs.push_back(T); ring_ok = ring_ok && puff->stat && puff->power == 2.0f && snd(r.bo, 301); }
                if (const Boss06Effect* e = zfx(r.bo, 388)) {
                    erupt.push_back(T);
                    erupt_ok = e->stat && e->power == 20.0f && e->knock == 0.0f && e->rtype == 3 && veq(e->pos, {-34.0f, 6.01f, 29.0f}) &&
                               r.bo.shake && r.bo.shake->first == 1000 && r.bo.shake->second.x == 0.1f && has(r.bo.rumble, 3) && snd(r.bo, 309);
                }
                if (T <= 360) {
                    const float k = std::min(1.0f, float(T) / 300.0f) * (T >= 200 ? 0.1f : 0.3f);
                    // the direction: normalize((-34 - Px, 6 - Py, 29 - Pz)) = (-1.5, -1, 0) / sqrt 3.25
                    suck_ok = suck_ok && r.bo.player_vel && nearf(len3(*r.bo.player_vel), k, 1e-5f) &&
                              nearf(r.bo.player_vel->x, -k * 0.83205029f, 1e-5f) && nearf(r.bo.player_vel->y, -k * 0.5547002f, 1e-5f) &&
                              r.bo.player_vel->z == 0.0f;
                }
            }
            if (r.zo.strike_atk) {
                strikes.push_back(T);
                blast_ok = *r.zo.strike_atk == 267.0f && r.Z.atk == 227.0f && r.bo.zoom && r.bo.zoom->first == 0.5f &&
                           r.bo.zoom->second == -0.005f && r.bo.shake && r.bo.shake->first == 75 && veq(r.bo.shake->second, {0.2f, 0.8f, 0.2f}) &&
                           snd(r.bo, 310) && has(r.bo.rumble, 5) && a.i(0x72c) == 5;
            }
            if (end6 < 0 && a.i(0x72c) == 7) {
                end6 = T;
                s6_ok = r.zo.fog && r.zo.fog->first == 0 && r.zo.fog->second == 0.01f && has(r.bo.kill_ids, 350) && has(r.bo.kill_ids, 586) &&
                        has(r.bo.kill_ids, 426) && has(r.bo.kill_handles, barrier) && has(r.bo.kill_handles, column) && r.Z.def == 252.0f &&
                        r.out.set_durable == 0 && !r.Z.guard399 && !(r.Z.flags12c & 0x40u) && a.f(0x7dc) == 4.0f && a.f(0x834) == 4.0f &&
                        !(a.i(0x7a0) & 0x24) && r.out.section == 300 && r.h.hb(0).step >= 0x80 && r.h.hb(1).step >= 0x80 && a.i(0x88c) == 0;
            }
            if (a.i(0x728) == 12) end12 = T;
        }
        std::vector<int> want_rings;
        if (kRing[lvl]) for (int T = 80; T <= 360; T += 20) want_rings.push_back(T);
        char msg[200];
        std::snprintf(msg, sizeof msg, "CG §C.6 step 4 (4): level %d — a ring of %d FX2 350 (power 2, knock 2, rtype 2) at radius t87 / 20, y 6.1, every 20 frames 80..360",
                      lvl, kRing[lvl]);
        check(rings == want_rings && ring_fx == 15 * kRing[lvl] && ring_ok, msg);
        if (lvl == 2) {
            check(erupt == std::vector<int>{101} && erupt_ok,
                  "CG §C.6 step 4 (1): t87 = 101, once: Shake(1000, .1), Rumble(3), SE 309, FX2 388 (stat, power 20, knock 0, rtype 3)");
            check(suck_ok, "CG §C.6 step 4 (2): P velocity = normalize(centre - P) * (0.3 grounded / 0.1 airborne) * min(1, t87 / 300), overwritten each frame");
            check(strikes == std::vector<int>{360} && blast_ok,
                  "CG §C.6 step 4 (5): one FUN_004fd350 at t87 == 360 within 2.0 of the centre with STR 267; ZoomBlur(.5, -.005), Shake(75, (.2,.8,.2)), SE 310, Rumble(5)");
            check(end6 == 420 && s6_ok,
                  "CG §C.6 steps 5 / 6 (T = 420): fog 0 at 0.01, Kill 350 / 586 / 426, the barrier and the 533 column by handle, DEF 252.0, Durable off, heads aborted");
            check(end12 == 780 && puffs.empty(), "CG §C.6 step 7 / §C.7: T = 780 -> action 12; no puff below Nightmare");
        }
        if (lvl == 4)
            check(puffs == std::vector<int>{120, 150, 200, 250, 300, 350} && r.a().f(0x7b0) == 50.0f,
                  "CG §C.6 step 4 (3): Nightmare — FX2 350 + SE 301 at the last sampled player position when t87 % period == 0: 60 first, then R(21) + 40 (a modulus of t87)");
    }
    {   // the blast misses outside 2.0 / above y 9.01
        auto miss = [](const Vec3& pp) {
            ZRig r; r.boot(); r.act(2, 20, 4); r.fly(); r.a().i(0x87c) = 360; r.h.in.player_pos = pp; r.L.fx388 = 1;
            r.step();
            return !r.zo.strike_atk && r.a().i(0x72c) == 5;
        };
        check(miss({-31.9f, 7.0f, 29.0f}) && miss({-34.0f, 9.02f, 29.0f}) && !miss({-34.0f, 9.0f, 30.9f}),
              "CG §C.6 step 4 (5): the damage needs lenXZ2(P - C1) < 4.0 and P.y < 9.01");
    }
}

static void zava_death() {
    // ---- the HP floor and the dying trigger
    ZRig r; r.boot(); r.act(1, 4); r.fly(); r.a().i(0x7a0) = 0x141; r.a().i(0x7c4) = 500; r.a().cool() = 9; r.Z.hp = 0.25f;
    r.h.in.player_pos = {-31.0f, 7.0f, 26.0f};                     // +0x734.. = (3, 0, 4)
    r.step();
    const EnemyAi& a = r.a();
    check(r.Z.hp == 1.0f && (a.i(0x7a0) & 8) && (r.Z.kind & 0x88u) == 0 && !a.hittable && r.zo.player_iframes == 9999 &&
          a.dead_t == 0 && a.i(0x728) == 11 && !r.Z.flyer,
          "AB §A.1 [0x53af86]: HP < 1 in AI phase 2 -> HP = 1.0, bit 8, +0x60 &= ~0x88, P i-frames >= 9999; airborne: the brain drops her (bit 8)");
    ZRig lk; lk.boot(); lk.act(2, 3); lk.a().i(0x7a0) = 0x20; lk.Z.hp = 0.0f; lk.Z.vert.grounded = true; lk.step();
    check(lk.Z.hp == 1.0f && !(lk.a().i(0x7a0) & 8) && lk.a().dead_t == 0,
          "AB §A.1: bit 0x20 (mid-flight / mid-big-attack) defers the dying trigger and the death");
    r.h.hb(0).move = 0x0b;
    r.Z.vert.grounded = true;
    const int t_before = a.i(0x87c);
    r.step();
    const bool deferred = a.dead_t == 0 && r.zo.boss_cam.empty() && a.i(0x87c) == t_before;
    r.h.hb(0).move = 3; r.h.hb(0).step = 2; r.h.hb(0).pos = {-52.0f, 6.0f, 29.0f}; r.h.in.zoom_active = false;
    r.sec = 9;
    r.step();                                                      // death frame 0
    check(deferred, "AB §A.1 [0x53b0be]: the death waits while a head is in move 0x11 or 0x0b (and for +0x388)");
    check(a.dead_t == 1 && has(r.bo.rumble, 5) && r.zo.player_unlock && r.w.flags[134] == 0 && r.zo.gauge_zero && r.zo.gauge_hide &&
          r.w.hidden == 1 && r.out.section == 5 && r.out.section_force && has(r.bo.kill_ids, 586) && has(r.bo.kill_ids, 270) &&
          r.cam(6) && r.zo.f6d6078 && r.zo.radial && r.zo.radial->first == 1.0f && r.zo.radial->second == -0.025f && r.out.vm_step &&
          a.i(0x87c) == 1 && a.i(0x708) == 1 && a.mode() == 0,
          "AB §A.3 step 0: Rumble(5), P unlocked, flag134 = 0, gauge to 0 / hidden, section 5 forced, FXkill 586 / 270, BossCam(6), radial blur 1.0 / -0.025; +0x87c = 1");
    check(!r.h.hb(0).collide && !r.h.hb(1).collide && r.h.hb(0).queued == 0xff && r.h.hb(1).queued == 0xff &&
          vnear(r.h.hb(0).pos, {-50.0f, 6.0f, 29.0f}, 1e-4f) && r.zo.player_heading &&
          nearf(*r.zo.player_heading, float(std::atan2(-4.0, -3.0)), 1e-6f),
          "AB §A.3 step 0 / §A.4: both heads: +0x60 &= ~0x80, pulled into the arena (0.5 a step), AbortMove, hb+0x71 = 0xff; P+0x308 = atan2(-(+0x73c), -(+0x734))");
    check(r.w.flags[225] == 1 && r.zo.byte_76a888 && r.w.flags.count(544) && r.w.flags[546] == 0 && r.zo.kill_record && r.zo.award_exp &&
          r.bo.shake && r.bo.shake->first == 0 && !r.bo.zoom && (a.i(0x7a0) & 0x200),
          "AB §A.1 [0x53b120] / C1: once: flag225 = 1, byte 0x76a888, flags 544..546 = 0, kill record, shake off, EXP; no zoom blur unless one runs");
    int cam7 = -1; bool once = true;
    for (int k = 1; k <= 90; ++k) {
        r.step();
        once = once && !r.zo.award_exp && !r.zo.kill_record && r.out.vm_step;
        if (r.cam(7) && cam7 < 0) cam7 = k;
    }
    check(cam7 == 79 && once && a.dead_t == 2 && r.w.flags[156] == 1,
          "AB §A.3 step 1 / C13: BossCam(7) on death frame 79 (+0x87c reaches 80); the rewards are not repeated; the gates wait for the heads");
    r.h.hb(0).step = 0x80; r.h.hb(1).step = 0x81;
    r.step();
    check(r.w.flags[156] == 0 && r.w.flags[130] == 0, "AB §A.3 step 2: both heads' hb+0x75 >= 0x80 -> flag156 = 0, flag130 = 0");
    const int before = a.i(0x87c);
    r.step();
    check(a.i(0x87c) == before && r.bo.flags.empty(), "AB §A.3: after flag156 = 0 the handler is the plain puppet tick");
    // ---- variants
    auto die = [](int rush, float php, bool zoom, ZRig& q) {
        q.boot(); q.act(1, 3); q.Z.vert.grounded = true; q.a().i(0x7a0) = 8; q.w.flags[193] = rush;
        q.h.in.player_hp_mirror = php; q.h.in.zoom_active = zoom;
        q.step();
    };
    ZRig br; die(1, 100.0f, true, br);
    ZRig pd; die(0, 0.5f, false, pd);
    check(br.w.flags[156] == 0 && !br.w.flags.count(225) && !br.zo.kill_record && br.zo.award_exp && br.bo.zoom &&
          br.bo.zoom->first == 0.5f && br.bo.zoom->second == -0.01f,
          "AB §A.1 / G14 / C1: boss rush (flag193) -> flag156 = 0 instead of 225; a running zoom blur is re-armed at 0.5 / -0.01");
    check(!pd.w.flags.count(225) && pd.zo.kill_record && !pd.zo.byte_76a888 && !(pd.Z.kind & 0x80u),
          "AB §A.1: with the player dead (PHP < 1) no flag225, the kill record still; +0x60 &= ~0x80 every frame");
    // ---- dying while riding
    ZRig d; d.boot(); d.act(2, 0, 4); d.a().i(0x7a0) = 0x2; d.a().i(0x7c0) = 1; d.Z.hp = 0.0f;
    d.h.hb(0).phase2 = 1; d.h.hb(0).move = 3; d.h.hb(0).step = 2; d.h.hb(0).timer = 9; d.h.hb(1).move = 3;
    d.step();
    check((d.a().i(0x7a0) & 0xa) == 0xa && d.h.hb(0).move == 8 && d.h.hb(0).step == 0 && d.h.hb(0).timer == 0 && d.h.hb(1).move == 3 &&
          d.a().i(0x87c) == 0 && d.a().dead_t == 0,
          "AB §A.1 [0x53b008]: dying while riding: each head with hb+0x81 gets move 8 / step 0 / timer 0; the brain does not run");
}

static void zava_handler() {
    // ---- facing and pose
    ZRig r; r.boot(); r.act(1, 3); r.fly(); r.a().i(0x7a0) = 1; r.sec = 2; r.a().i(0x7bc) = 0;
    r.h.in.player_pos = {-30.0f, 7.0f, 25.0f};                     // to the player: (4, 0, 3)
    r.step();
    check(r.zo.facing && nearf(*r.zo.facing, 1.5707964f - float(std::atan2(4.0, 3.0)), 1e-6f) && r.out.section == 0x12c &&
          !r.out.section_force,
          "AB §A.1 [0x53b470 / 0x53b4ed]: bit 1 -> +0x360 = pi/2 - atan2(+0x734, +0x73c); flying: the pose (default 0x12c) is re-requested");
    r.a().i(0x7a0) = 0; r.a().heading = 0.7f; r.a().i(0x7bc) = 0x12f; r.step();
    const bool heading = r.zo.facing && *r.zo.facing == r.a().heading && r.out.section == 0x12f;
    r.step();
    check(heading && r.out.section < 0, "AB §A.1: bit 1 clear -> +0x360 = +0x308; poses 0x12f / 0x132 / 0x12d are kept; no request once current");
    ZRig g; g.boot(); g.act(2, 3); g.Z.vert.grounded = true; g.a().i(0x7bc) = 0x130; g.sec = 9; g.step();
    const bool to131 = g.out.section == 0x131;
    g.sec = 0x130; g.step();
    check(to131 && g.out.section < 0, "AB §A.1 / C2: not flying, +0x7bc == 0x130 and section != 0x130 -> SetSection(0x131)");
    // ---- the tail: the floor ring, the clamps
    ZRig t; t.boot(); t.act(1, 3); t.fly(); t.at({-30.0f, 11.0f, 14.0f});
    int rings = 0; bool ring_ok = true;
    for (int k = 0; k < 60; ++k) {
        t.step();
        if (const Boss06Effect* e = zfx(t.bo, 270)) {
            ++rings;
            ring_ok = ring_ok && t.a().i(0x8a0) % 20 == 0 && veq(e->pos, {-30.0f, 6.05f, 15.0f}) && !e->stat && e->power == 0.0f && e->knock == 1.0f &&
                      e->parent == Boss06Parent::kZavaFloor && e->at_mode == 2;
        }
    }
    check(rings == 3 && ring_ok && t.org().z == 15.0f,
          "AB §A.1 [0x53b654 / 0x53b713]: flying: FX2 270 at (x, 6.05, z) every 20th +0x8a0 (after the ++), attached mode 2; z clamped to >= 15");
    ZRig c; c.boot(); c.act(1, 3); c.Z.vert.grounded = true; c.at({-30.0f, 2.0f, 40.0f}); c.step();
    check(c.org().z == 38.7f && c.org().y == 5.0f && zfx(c.bo, 270) == nullptr,
          "AB §A.1 [0x53b713]: z <= 38.7, y >= 6.0 - +0x324; no ring while not flying");
    // ---- the push-out
    ZRig p; p.boot(); p.act(1, 3); p.fly(); p.a().i(0x7a0) = 0x40; p.at({-34.0f, 7.0f, 22.0f});
    p.h.hb(1).pos = {-34.0f, 6.1f, 25.5f}; p.h.hb(0).pos = {-20.0f, 6.1f, 39.0f};
    p.step();
    const bool pushed = vnear(p.Z.mover.k1, {0.0f, 0.0f, -0.05f}, 1e-6f);
    p.Z.mover.k1 = {}; p.h.hb(1).pos = {-34.0f, 6.1f, 26.5f}; p.step();
    const bool far = veq(p.Z.mover.k1, {0, 0, 0});
    p.h.hb(0).pos = {-37.0f, 30.0f, 22.0f}; p.h.hb(1).pos = {-20.0f, 1.0f, 39.0f}; p.at({-34.0f, 7.0f, 22.0f}); p.step();
    const bool sic = veq(p.Z.mover.k1, {0, 0, 0});                 // head a is near and above, but b.y + b.r = 4 < Z.y
    p.h.hb(1).pos.y = 6.1f; p.step();
    check(pushed && far && sic && vnear(p.Z.mover.k1, {0.05f, 0.0f, 0.0f}, 1e-6f),
          "AB §B.1 (4): bit 0x40: within radius + 1 (xz) of a head -> knock velocity = normalize(Z - head) * 0.05; head a's height test reads head b (SIC)");
}

static void zava_tests() {
    zava_init();
    zava_phase1_move();
    zava_phase1_attacks();
    zava_knockdown();
    zava_mount();
    zava_phase2_actions();
    zava_big_attack();
    zava_death();
    zava_handler();
}
