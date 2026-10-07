// Included inside cleria-bosstest's main(): the attacks' outputs the host turns into world objects
// (the laser's terrain rays, the inhale's pulls / swallows / heal, the Spawns' stun), the M890 brain
// (brains/m890.cpp), and the BOSS01.YMO node flags the body's touch reads (with an asset root).

// ---------------------------------------------------------------- the attacks over a scripted fight
for (int character : {1, 3}) {
    Boss01 b(setup(0, false, false, character));
    Rig r;
    r.in.fight = true;
    const float k_in = character == 3 ? 0.0145f : 0.0125f;   // state 4 sub 2, Normal (0x6bd0bc / 0x6bd0a8)
    int ticks = 0, ray_ticks = 0, ray_bad = 0, ray_geom_bad = 0, laser_ticks = 0, hurt_rays = 0;
    int pulls = 0, pull_bad = 0, eaten_max = 0, spawn_pulls = 0, spawn_pull_bad = 0;
    bool healed = false, shorten = false, stun100 = false, stun1 = false;
    const float players[4][2] = {{0, 20.22f}, {6, 26}, {-8, 24}, {0, 29}};
    while (!b.destroyed() && ticks < 60000) {
        r.in.player_pos = {players[(ticks / 900) % 4][0], 2, players[(ticks / 900) % 4][1]};
        // (no stun-part hits during the inhale, so it runs to its bite)
        if (ticks % 90 == 0 && b.part(1).def < 999.0f && b.state() != 4) b.add_part_damage(1, 25);
        if (ticks % 30 == 0 && b.part(0).def < 999.0f) b.add_part_damage(0, 12);
        const bool hurt = ticks % 97 == 0;
        if (hurt) r.in.player_took_damage = true;
        const Vec3 pp = r.in.player_pos;
        std::vector<Vec3> spawn_before;
        for (const Boss01Actor& a : r.in.actors) spawn_before.push_back(a.pos);
        const int st0 = b.state(), sub0 = b.sub();
        const std::uint32_t c00 = b.timer();
        const float hp0 = b.hp();
        const Boss01Output o = r.step(b);
        ++ticks;
        if (st0 == 5 && sub0 == 2 && b.state() == 5) {        // the beam: a ray every 4th T1
            ++laser_ticks;
            const bool due = ((b.t1() - 1) & 3u) == 0;
            const bool want = due && !(character == 3 && hurt);
            if (int(o.rays.size()) != (want ? 1 : 0)) ++ray_bad;
            if (hurt && due && character == 3) ++hurt_rays;
            for (const Boss01Ray& ry : o.rays) {
                ++ray_ticks;
                const Vec3 m = r.in.bones[11];                // Null(238)
                const Vec3 d = ry.delta;
                const float l = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                const bool ok = near_eq(ry.from.x, m.x + d.x / l) && near_eq(ry.from.y, m.y + d.y / l) &&
                                near_eq(ry.from.z, m.z + d.z / l) && near_eq(m.y + d.y, 1.9f) &&
                                ry.fx.id == (character == 3 ? 0x144 : 0x146) && ry.fx.rtype == 1 &&
                                ry.at_aim == (character == 3);
                ray_geom_bad += !ok;
            }
            shorten |= o.shorten_146;
        }
        if (st0 == 4 && sub0 == 2) {                          // the inhale: the player pulled 15 k / tick
            for (const Boss01ActorEdit& e : o.actor_edits) {
                const Vec3 before = e.index == 0 ? pp : spawn_before[std::size_t(e.index)];
                const float dx = e.pos.x - before.x, dz = e.pos.z - before.z;
                const float step = std::sqrt(dx * dx + dz * dz);
                if (e.index == 0) {
                    // A.xz -= n.xz k with n = 15 d / |d| in 3-D (d = A - P): the planar step is
                    // 15 k |d.xz| / |d|. P = Null(242) + RotY(d4) (0, 0, -1.5), y - 0.5.
                    ++pulls;
                    const Vec3 b15 = r.in.bones[15];
                    const float c = std::cos(b.yaw()), sn = std::sin(b.yaw());
                    const Vec3 P{b15.x + (-1.5f) * sn, b15.y - 0.5f, b15.z + (-1.5f) * c};
                    const float px = before.x - P.x, py = before.y - P.y, pz = before.z - P.z;
                    const float f = std::sqrt(px * px + pz * pz) / std::sqrt(px * px + py * py + pz * pz);
                    if (!near_eq(step, 15.0f * k_in * f, 2e-3f) && !near_eq(step, 15.0f * 0.009f * f, 2e-3f)) ++pull_bad;
                } else if (e.set_pos) {
                    ++spawn_pulls;
                    const float dy = e.pos.y - before.y;
                    const float s3 = std::sqrt(step * step + dy * dy);
                    if (!near_eq(s3, 15.0f * 0.014f, 2e-3f) || !e.set_330 || !near_eq(e.f330, 0.014f)) ++spawn_pull_bad;
                }
            }
        }
        eaten_max = std::max(eaten_max, b.eaten());
        if (st0 == 4 && sub0 == 3 && c00 + 1 == 35 && b.eaten() > 0)   // the heal on c0 == 35
            healed |= near_eq(b.hp(), std::min(b.max_hp(), hp0 + b.max_hp() / 30.0f * float(b.eaten())), 0.05f) ||
                      (hurt && b.hp() > hp0);
        stun100 |= o.minion_730 == 100;
        stun1 |= o.minion_730 == 1;
    }
    char what[160];
    std::snprintf(what, sizeof what, "%s laser: a terrain ray every 4th tick (%d rays over %d beam ticks)",
                  character == 3 ? "Toal" : "Yunica", ray_ticks, laser_ticks);
    check(laser_ticks > 0 && ray_ticks > 0 && ray_bad == 0, what);
    check(ray_geom_bad == 0, character == 3 ? "Toal laser ray: Null(238)+u along aim-Null(238), aim y 1.9, EF2 0x144 at the aim"
                                            : "laser ray: Null(238)+u along aim-Null(238), aim y 1.9, EF2 0x146 at the contact");
    if (character == 3) check(hurt_rays > 0, "Toal laser: no ray on a tick the player took damage");
    else check(shorten, "laser at phase >= 2: the live 0x146 bursts are shortened to 30");
    std::snprintf(what, sizeof what, "inhale: the player pulled 15 x %.4f in the cone / 15 x 0.009 out of it (%d pulls)", k_in, pulls);
    check(pulls > 0 && pull_bad == 0, what);
    check(spawn_pulls > 0 && spawn_pull_bad == 0, "inhale: Spawns pulled 15 x 0.014 (3-D), +0x330 = 0.014");
    check(eaten_max > 0 && healed, "inhale: Spawns within 3 of the pull point are eaten; the bite heals max/30 per Spawn");
    check(stun100 && stun1, "stun: every Spawn +0x730 = 100 at PIYOPIYO_2, = 1 when the dizzy loop ends");
}

// ---------------------------------------------------------------- M890 Velagunder Spawn (AI 82)
{
    struct Arena : AiWorld {                                  // the S_1099 floor: rays blocked on request
        bool block = false;
        void for_each(const std::function<void(Actor&)>&) override {}
        Actor* find(int) override { return nullptr; }
        Actor* spawn_child(Actor&, int, const Vec3&, int, bool) override { return nullptr; }
        void remove(Actor&) override {}
        Ray ray(const Vec3&, const Vec3&) override { Ray r; r.hit = block; return r; }
        bool sphere_hits(const Vec3&, float) override { return false; }
        void arena(float r[4]) override { r[0] = -14; r[1] = 16.7f; r[2] = 15; r[3] = 34.8f; }
        const float* enemy_raw(int) override { return nullptr; }
    };
    const EnemyBrain m890 = enemy_brain_for(82);
    check(m890 != nullptr, "M890 brain registered at AI index 82");
    EnemyAiParams p{};
    for (int k = 0; k < 16; ++k) p.raw[k] = 0.1f * float(k + 1);   // turn 0.1, base 0.2, ...
    struct Spawn { Actor A; EnemyAi& ai() { return A.ai; } };
    auto make_env = [&](Actor& A, Arena& w, XorShift& rng, int character, int diff, bool easy) {
        AiEnv e;
        e.actor = &A; e.world = &w; e.character = character;
        e.diff_index = diff; e.diff_easy = easy; e.diff_very_easy = false;
        e.self = {A.pos.x, A.pos.y + A.height, A.pos.z};
        e.target = {0, 3.075f, 20.0f};
        e.grounded = true; e.stun_immune = true;
        e.section_type = [](int) { return 0; };
        e.rng = [&rng] { return rng(); };
        return e;
    };
    struct Row { int ch, diff; bool easy; float hp, max, def, spd, turn, b4; int c728; };
    const Row rows[] = {
        {1, 0, false, 58, 58, 20, 0.2f, 0.1f, 0.5f, 0},
        {1, 0, true, 58 * 0.75f, 58 * 0.75f, 15, 0.2f, 0.1f, 0.5f, 0},
        {2, 1, false, 58 * 1.25f * 1.5f, 58 * 1.25f * 1.5f, 25, 0.3f, 0.15f, 0.5f, 1},
        {3, 2, false, 58 * 1.5f, 58 * 1.5f, 25, 0.26f, 0.13f, -0.2f, 0},
    };
    for (const Row& rw : rows) {
        Spawn s; Actor& A = s.A;
        A.cast_id = 0xaf; A.hp = 58; A.max_hp = 58; A.def = 20; A.resist[0] = 0.5f; A.height = 0.5f;
        A.pos = {0, 2, 25};
        A.ai.i(0x72c) = 2; A.ai.i(0x730) = 7;                // what the boss wrote / garbage
        Arena w; XorShift rng;
        const AiEnv e = make_env(A, w, rng, rw.ch, rw.diff, rw.easy);
        m890(A.ai, p, e);
        char what[160];
        std::snprintf(what, sizeof what, "M890 init char %d diff %d%s: HP %.2f DEF %.2f speed %.3f turn %.3f, phase 2",
                      rw.ch, rw.diff, rw.easy ? " (Easy)" : "", rw.hp, rw.def, rw.spd, rw.turn);
        check(A.ai.phase() == 2 && near_eq(A.hp, rw.hp) && near_eq(A.max_hp, rw.max) && near_eq(A.def, rw.def) &&
                  near_eq(A.ai.f(0x818), rw.spd) && near_eq(A.ai.f(0x814), rw.turn) && near_eq(A.resist[0], rw.b4) &&
                  A.ai.i(0x728) == rw.c728 && rng.draws == 0,
              what);
        if (rw.ch == 2) check(A.ai.i(0x72c) == 2, "M890 init Hugo: +0x72c from the boss survives (the flanker)");
        else if (rw.ch == 1 && !rw.easy) check(A.ai.i(0x72c) == 0 && A.ai.i(0x730) == 0 && near_eq(A.ai.f(0x82c), 0.7f) &&
                                                   A.ai.f(0x834) == 0.0f,
                                               "M890 init: +0x72c / +0x730 zeroed, ENEMYINFO +0x90..+0xac copied (8 floats only)");
    }
    {   // the wander: PICK, the walk, the re-pick, the boss stun and its release
        Spawn s; Actor& A = s.A;
        A.cast_id = 0xaf; A.hp = A.max_hp = 58; A.height = 0.5f; A.pos = {0, 2, 25}; A.flags12c = 0x05194012;
        Arena w; XorShift rng;
        AiEnv e = make_env(A, w, rng, 1, 0, false);
        enemy_ai_tick(A.ai, p, e, m890);                     // init
        const int d0 = rng.draws;
        enemy_ai_tick(A.ai, p, e, m890);                     // the first engaged tick: PICK
        const Vec3 W = A.ai.v(0x894);
        const int nd = rng.draws - d0;
        const float L = std::sqrt((W.x - e.self.x) * (W.x - e.self.x) + (W.z - e.self.z) * (W.z - e.self.z));
        check(A.ai.i(0x72c) == 1 && nd >= 2 && nd % 2 == 0 && nd <= 20 && W.z > 16.5f && W.z < 35.0f && L >= 4.99f &&
                  L <= 15.01f,
              "M890 PICK: 2 draws a try, L = R(11)+5, waypoint inside 16.5 < z < 35, +0x72c = 1");
        // the replica of the first try's draws reproduces the waypoint when it was accepted
        if (nd == 2) {
            XorShift ref; for (int k = 0; k < d0; ++k) ref();
            const float L2 = float((ref() >> 17) % 11u + 5u), ang = float((ref() >> 17) % 360u) * 0.0174532924f;
            check(near_eq(W.x, e.self.x + L2 * std::sin(ang)) && near_eq(W.z, e.self.z + L2 * std::cos(ang)),
                  "M890 PICK: v = (0, 0, L) x RotY(a) (D3DX), first try taken");
        }
        enemy_ai_tick(A.ai, p, e, m890);                     // walking
        check(A.ai.mode() == 1 && A.ai.i(0x7a0) == 1, "M890 walk: action 0x20 (walk) toward the waypoint, +0x7a0 counts");
        A.ai.i(0x7a0) = 201;                                  // after 200 ticks: a fresh PICK
        const Vec3 W0 = A.ai.v(0x894);
        const int d1 = rng.draws;
        enemy_ai_tick(A.ai, p, e, m890);
        check(rng.draws > d1 && A.ai.i(0x7a0) == 1 && (A.ai.v(0x894).x != W0.x || A.ai.v(0x894).z != W0.z),
              "M890: +0x7a0 > 200 re-picks the waypoint and restarts the count");
        // the boss stun
        A.ai.i(0x730) = 100;
        AiOut o = enemy_ai_tick(A.ai, p, e, m890);
        bool stars = false;
        for (const AiEffect& fx : o.effects) stars |= fx.id == 60 && fx.attach && near_eq(fx.scale, 0.8f);
        bool se = false;
        for (int sd : o.sounds) se |= sd == 0xb8;
        check(A.ai.phase() == 3 && stars && se && o.section == 0x13b, "M890 boss stun: phase 3, stars (EF2 60 x0.8), SE 184, section 0x13b");
        bool held = true;
        for (int k = 0; k < 400; ++k) { enemy_ai_tick(A.ai, p, e, m890); held &= A.ai.phase() == 3; }
        check(held && A.ai.dur() >= 98, "M890 boss stun: held while +0x730 == 100 (the hold counts up, never runs out)");
        A.ai.i(0x730) = 1;
        enemy_ai_tick(A.ai, p, e, m890);
        check(A.ai.phase() == 2, "M890 boss stun: +0x730 == 1 ends the hold the same tick");
        // all ten tries blocked: W = the player
        w.block = true;
        A.ai.i(0x7a0) = 300;
        const int d2 = rng.draws;
        enemy_ai_tick(A.ai, p, e, m890);
        const Vec3 Wb = A.ai.v(0x894);
        check(rng.draws - d2 == 20 && near_eq(Wb.x, e.target.x) && near_eq(Wb.z, e.target.z),
              "M890 PICK: 10 blocked tries (20 draws) fall back to the player");
    }
    {   // Hugo: the chaser and the flanker
        Spawn s; Actor& A = s.A;
        A.cast_id = 0xaf; A.hp = A.max_hp = 58; A.height = 0.5f; A.pos = {4, 2, 25};
        Arena w; XorShift rng;
        AiEnv e = make_env(A, w, rng, 2, 0, false);
        A.ai.i(0x72c) = 1;
        enemy_ai_tick(A.ai, p, e, m890);                     // init (Hugo keeps +0x72c)
        enemy_ai_tick(A.ai, p, e, m890);
        enemy_ai_tick(A.ai, p, e, m890);
        const Vec3 W = A.ai.v(0x894);
        check(near_eq(W.x, e.target.x) && near_eq(W.z, e.target.z) && A.ai.i(0x79c) == 2, "M890 Hugo +0x72c 1: chases the player");
        A.ai.i(0x72c) = 2;
        const int d0 = rng.draws;
        enemy_ai_tick(A.ai, p, e, m890);
        const Vec3 F = A.ai.v(0x894);
        const float fx = F.x - e.target.x, fz = F.z - e.target.z, fl = std::sqrt(fx * fx + fz * fz);
        check(rng.draws - d0 == 2 && fl >= 2.99f && fl <= 5.91f, "M890 Hugo +0x72c 2: flanks at (3 + R(30)/10) off the player, 2 draws");
        A.ai.i(0x79c) = 600;                                  // > R(350) + 150 always
        enemy_ai_tick(A.ai, p, e, m890);
        const Vec3 C = A.ai.v(0x894);
        check(near_eq(C.x, e.target.x) && near_eq(C.z, e.target.z), "M890 Hugo flanker: +0x79c past R(350)+150 -> the chase");
    }
}

// ---------------------------------------------------------------- BOSS01.YMO node flags (the touch)
if (assets) {
    try {
        const std::filesystem::path dir = std::filesystem::path(assets) / "ENEMY" / "BOSS01";
        auto load = [](const std::filesystem::path& p) {
            std::ifstream f(p, std::ios::binary);
            std::vector<std::uint8_t> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            Model m = Model::parse(b);
            m.resolve_hierarchy();
            return m;
        };
        const Model rm = load(dir / "BOSS01.YMO");
        const Model tw = load(dir / "BOSS01_.YMO");
        auto flags_of = [&](const char* nm) -> std::int64_t {
            for (const Node& n : rm.nodes) if (n.name == nm) return n.flags;
            return -1;
        };
        check(flags_of("Bone(22)") == 0x45 && flags_of("Bone(48)") == 0x23 && flags_of("Bone(18)") == 0x05 &&
                  flags_of("Bone(43)") == 0x03 && flags_of("Bone(16)") == 0x01 && flags_of("Bone(81)") == 0,
              "BOSS01.YMO node +0x290 flags: arm L 0x45 / 0x05, arm R 0x23 / 0x03 (the entry +0x84)");
        int kind6 = 0;
        float r9 = 0;
        for (const Node& n : rm.nodes) { kind6 += n.kind == 6; if (n.name == "Bone(9)") r9 = n.radius; }
        check(kind6 == 181 && near_eq(r9, 2.65f), "BOSS01.YMO: 181 nodes of kind 6 (+0x288, the death-spark nodes), Bone(9) radius 2.65 (+0x28C)");
        int lit_l = 0, lit_mid = 0, entries = 0;
        Boss01Contact lw; lw.l_window = true;
        Boss01Contact md; md.l_mid = true; md.mid80 = true;
        for (const Node& n : tw.nodes) {
            if (n.mesh_index < 0) continue;
            ++entries;
            const std::int64_t f = flags_of(n.parent.c_str());
            if (f < 0) continue;
            lit_l += lw.entry_hurts(std::uint32_t(f));
            lit_mid += md.entry_hurts(std::uint32_t(f));
        }
        char what[160];
        std::snprintf(what, sizeof what, "BOSS01_.YMO (flags 0x%x): %d entries by parent bone; ATTACK_01_L window lights %d, its mid frame %d",
                      tw.flags, entries, lit_l, lit_mid);
        check((tw.flags & 0x20) != 0 && entries == 31 && lit_l == 12 && lit_mid == 12, what);
    } catch (const std::exception& ex) {
        check(false, ex.what());
    }
}
