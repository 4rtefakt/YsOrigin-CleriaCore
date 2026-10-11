// Velagunder's requests (Boss01Output) that reach other objects: the EF2 effects with their attach
// frames, the bullets, the inhale's pulls and swallows, the M890 spawns and their stun, the laser's
// terrain rays. Included inside view_game.inl's boss_tick, right after the brain tick; `out`, `in`,
// `in_bullets` and the boss_* attach frames are that lambda's. build/boss_re/VELAGUNDER.md.
{
    // FUN_00475710(0xaf, yaw 0, pos, level = boss +0xc8) + the run's writes on the result (§5.1 step 5):
    // S+0x330 = 0.05 (a small hop), S+0x338..0x340 = dir x 0.25 (knock channel 1), S+0x72c = 1 / 2.
    // FUN_004202e0 takes the level override, so the stats are the ENEMYINFO record's at that level.
    std::vector<int> spawn_ids(out.spawns.size(), 0);
    std::size_t spawned = 0;
    auto make_spawns = [&](std::size_t upto) {
        for (; spawned < upto && spawned < out.spawns.size(); ++spawned) {
            const Boss01Spawn& s = out.spawns[spawned];
            SpawnRequest req;
            req.cast_type = s.cast;
            req.pos = {s.pos.x, s.pos.y, s.pos.z};
            req.yaw_degrees = float(s.yaw_deg);
            Actor* a = world.spawn(req);
            if (!a) continue;
            a->height = cast_motion(casts, s.cast).height();
            a->radius = cast_motion(casts, s.cast).radius();
            a->pos.y = s.pos.y - a->height;                // the request is the origin (+0x50)
            if (const Enemy* er = etab.get(s.cast); er && ginfo.ok() && s.level > 0) {
                Enemy lv = *er;
                lv.level = s.level;
                const EnemyStats st = enemy_stats(ginfo, lv);
                a->max_hp = a->hp = st.max_hp; a->atk = st.atk; a->def = st.def; a->exp_reward = st.exp;
            }
            a->level = s.level;
            a->vert.t = s.f330;                            // S+0x330 = 0.05
            a->vert.grounded = false;
            a->mover.k1.x += s.knock_vel.x; a->mover.k1.y += s.knock_vel.y; a->mover.k1.z += s.knock_vel.z;
            a->ai.i(0x72c) = s.mode72c;
            const float fl = floor_at(phys_mesh(room), s.pos.x, s.pos.z, s.pos.y);
            if (fl > kNoFloor) a->floor_y = fl;
            spawn_ids[spawned] = a->instance_id;
            actor_mats[a->instance_id] = ef2sim::translation(s.pos.x, s.pos.y, s.pos.z);
            csHost.world_changed = true;
            if (std::getenv("CLERIA_BOSSLOG"))
                std::printf("  [boss] t%llu M890 #%d spawned at (%.2f,%.2f,%.2f) hp %.0f lv %d mode %d\n",
                            (unsigned long long)tick_count, a->instance_id, s.pos.x, s.pos.y, s.pos.z, a->hp,
                            s.level, s.mode72c);
        }
    };
    // FUN_00450880 + FUN_00451c20: the effect, then (attached) its local frame T(off) x RotYPR(rot)
    // against the parent; mode 1 follows the parent's whole matrix, mode 2 its translation only.
    for (const Boss01Effect& fe : out.effects) {
        const ef2sim::M4* parent = nullptr;
        switch (fe.attach) {
            case Boss01Effect::kBone:
                if (fe.attach_bone >= 0 && fe.attach_bone < boss01::kBones) parent = &boss_bone_m[std::size_t(fe.attach_bone)];
                break;
            case Boss01Effect::kBone114: parent = &boss_b114_m; break;
            case Boss01Effect::kBone115: parent = &boss_b115_m; break;
            case Boss01Effect::kBeam: parent = &boss_beam_m; break;
            case Boss01Effect::kPart0: parent = &boss_part0_m; break;
            case Boss01Effect::kSpawnActor:
                if (fe.spawn_ref >= 0) {
                    make_spawns(std::size_t(fe.spawn_ref) + 1);
                    const int id = std::size_t(fe.spawn_ref) < spawn_ids.size() ? spawn_ids[std::size_t(fe.spawn_ref)] : 0;
                    if (id != 0) parent = &actor_mats[id];
                }
                break;
            default: break;
        }
        ef2sim::SpawnParams sp;
        sp.side = fe.side; sp.power = fe.power; sp.knock = fe.knock; sp.rtype = fe.rtype;
        sp.status = fe.status; sp.cls = fe.cls;
        sp.atk = fe.stat == Boss01Effect::kBoss ? boss->str() : 0.0f;
        if (fe.stat == Boss01Effect::kSpawn && fe.spawn_ref >= 0) {
            make_spawns(std::size_t(fe.spawn_ref) + 1);
            const int id = std::size_t(fe.spawn_ref) < spawn_ids.size() ? spawn_ids[std::size_t(fe.spawn_ref)] : 0;
            if (const Actor* sa = id ? world.find(id) : nullptr) sp.atk = sa->atk;
        }
        const ef2sim::M4 local = ef2sim::mul(ef2sim::translation(fe.attach_off.x, fe.attach_off.y, fe.attach_off.z),
                                             ef2sim::rot_ypr(fe.attach_rot.y, fe.attach_rot.x, fe.attach_rot.z));
        Vec3 at = fe.pos;
        if (parent) at = fe.attach_mode == 1 ? ef2sim::transform_coord({0, 0, 0}, ef2sim::mul(local, *parent))
                                             : Vec3{parent->m[3][0] + fe.attach_off.x, parent->m[3][1] + fe.attach_off.y,
                                                    parent->m[3][2] + fe.attach_off.z};
        // A scale the brain writes after the spawn (fx+0x1ac..0x1b4) is the effect's, not the matrix's.
        ef2sim::Effect* fx = spawn_sim_fx(fe.id, at, parent ? Vec3{} : fe.rot, fe.scale_after ? 1.0f : fe.scale.x, sp, 0, {});
        if (!fx) continue;
        if (fe.scale_after) { fx->scale_x = fe.scale.x; fx->scale_y = fe.scale.y; fx->scale_z = fe.scale.z; }
        if (fe.flag201) fx->no_boss = true;                  // fx+0x201 = 1: its world rays skip the boss meshes
        if (parent) {
            fx->local = local;
            fx->attach_parent = parent;
            fx->attach_mode = fe.attach_mode == 1 ? 1 : 2;
        }
        if (fe.life >= 0)                                    // FUN_00564a20: the first particle's clock
            if (ef2sim::Particle* p = fxsim.first_particle(fx)) { p->life = fe.life; p->remaining = fe.life; }
        // fx+0x11c |= 2 (knock_from_facing, the sweep's arm beams) lands on the GoEffect's own record,
        // not on its particles' (EFFECTHIT §3.3: p+0x11c = 0), so the particle hits ignore it [H].
    }
    make_spawns(out.spawns.size());
    for (int id : out.kill_effects)
        for (auto& f : fxsim.effects()) if (f->id == id) f->kill = true;
    // FUN_00593550: the bubble (0x1e) and drool (0x1f) bullets, BULLETS.md §3.8 / §3.11.
    for (const Boss01Bullet& b : out.bullets) {
        bullet::Spawn s = bullet::block_spawn(b.kind, b.effect, b.pos, b.dir, b.f.data());
        s.owner = 0; s.owner_monster = true; s.owner_atk = boss->str();   // [H] the boss is no world actor
        bullets.spawn(s);
    }
    for (const Boss01BubbleEdit& e : out.bubble_edits) {   // the inhale pull / swallow
        if (e.index < 0 || std::size_t(e.index) >= in_bullets.size()) continue;
        bullet::Bullet* bp = in_bullets[std::size_t(e.index)];
        bp->s.pos = e.pos;
        if (e.kill && bp->fx) { bp->fx->kill = true; bp->gone = true; }   // [H] the eat = a handler kill
    }
    // State 4's pull on the actor list (FUN_0050f280 sub 2 / 3): the player's and the Spawns'
    // +0x50 moved, a Spawn's +0x330 = 0.014, a swallowed Spawn's HP = 0 and +0x130 |= 8 (no death
    // effect). `in.actors` was built from world.actors() in order, so the index is the list's.
    if (!out.actor_edits.empty()) {
        std::vector<Actor*> list;
        for (Actor& a : world.actors()) list.push_back(&a);
        for (const Boss01ActorEdit& e : out.actor_edits) {
            if (e.index < 0 || std::size_t(e.index) >= list.size()) continue;
            Actor* a = list[std::size_t(e.index)];
            if (a->is_player()) {
                if (e.set_pos) { feet.x = e.pos.x; feet.z = e.pos.z; }
                continue;
            }
            if (e.set_pos) a->pos = {e.pos.x, e.pos.y - a->height, e.pos.z};
            if (e.set_330) a->vert.t = e.f330;
            if (e.eaten) {
                a->hp = 0.0f;
                a->flags130 |= 8u;
                if (std::getenv("CLERIA_BOSSLOG"))
                    std::printf("  [boss] t%llu M890 #%d eaten (%d so far)\n", (unsigned long long)tick_count,
                                a->instance_id, boss->eaten());
            }
        }
    }
    if (out.minion_730 != 0)                               // every cast-0xaf actor: +0x730 = 100 / 1
        for (Actor& a : world.actors()) if (a.cast_id == 0xaf) a.ai.i(0x730) = out.minion_730;
    if (out.kill_minions)
        for (Actor& a : world.actors()) if (a.cast_id == 0x19 || a.cast_id == 0xaf) a.hp = 0.0f;
    // The laser's terrain ray (FUN_005e9380 + FUN_0041e420(ecx 2, push 1) [BYTES 0x510ead / 0x511120]):
    // set 2 = the terrain list, and the pushed 1 skips the GoMeshes with +0x28d in 5..0xc, i.e. the
    // boss's own body. On contact: EF2 0x146 at the contact point (the delayed ground burst), or for
    // Toal 0x144 at the aim point; power 0, knock 1, rtype 1, the boss's stat block.
    for (const Boss01Ray& r : out.rays) {
        const RoomMesh& cm = phys_mesh(room);
        const Vec3 b{r.from.x + r.delta.x, r.from.y + r.delta.y, r.from.z + r.delta.z};
        const std::uint32_t ntri = std::uint32_t(cm.indices.size() / 3);
        std::uint32_t t0 = 0, t1 = 0;
        if (const RoomMesh::ObjRange* br = cm.object_range("boss01")) { t0 = br->t0; t1 = br->t1; }
        Vec3 best{}, pt{};
        float bd = 3.4e38f;
        bool hit = false;
        auto test = [&](std::uint32_t a0, std::uint32_t a1) {
            if (a1 <= a0 || !segment_hits_tris(cm, r.from, b, a0, a1, &pt)) return;
            const float d = (pt.x - r.from.x) * (pt.x - r.from.x) + (pt.y - r.from.y) * (pt.y - r.from.y) +
                            (pt.z - r.from.z) * (pt.z - r.from.z);
            if (d < bd) { bd = d; best = pt; hit = true; }
        };
        test(0, std::min(t0, ntri));
        test(std::min(t1, ntri), ntri);
        if (!hit) continue;
        ef2sim::SpawnParams sp;
        sp.side = r.fx.side; sp.power = r.fx.power; sp.knock = r.fx.knock; sp.rtype = r.fx.rtype;
        sp.status = r.fx.status; sp.cls = r.fx.cls; sp.atk = boss->str();
        spawn_sim_fx(r.fx.id, r.at_aim ? r.fx.pos : best, {}, 1.0f, sp, 0, {});
    }
    // FUN_00504c10(effect 0x57, pct, scale), the death sparks (§8.1) [EXPORT 00504c10.c; BYTES 0x5073cb..0x507412:
    // XMM3 = 0.5, the stack scale = 0.5 + R(25)/100]: over the render model's nodes in order, each of kind 6
    // (YMO node +0x288) draws once and, when (w >> 17) % 100 <= pct, twice more: a point (0, 0, len) in the
    // node's posed frame plus (cos a, sin a) x (radius x 0.25 + 0.5) along its x / y rows, len =
    // ((w2 >> 17) x 0.7 / 32767 + 0.05) x radius (+0x28C), a = ((w3 >> 17) / 0x7fff) x 2 pi (an integer divide:
    // 0 unless w3 >> 17 == 0x7fff). Below y 3.5 nothing spawns (the boss's +0x28d == 5 test). Same shared RNG,
    // right after the brain's own draws.
    if (out.death_sparks) {
        const Boss01Output::DeathSparks& ds = *out.death_sparks;
        for (const Node& n : mdl->nodes) {
            if (n.kind != 6) continue;
            const std::uint32_t w1 = pick_rng.next();
            if (int((w1 >> 17) % 100u) > ds.pct) continue;
            const std::uint32_t w2 = pick_rng.next(), w3 = pick_rng.next();
            const float len = (float(w2 >> 17) * 0.7f / 32767.0f + 0.05f) * n.radius;   // 0x6bd36c / 0x6bdf6c / 0x6bd13c
            const float rr = n.radius * 0.25f + 0.5f;                                    // 0x6bd244 + XMM3
            const float an = float((w3 >> 17) / 0x7fffu) * 6.2831855f;                    // 0x6bd898
            const Mat4 W = n.world * place;
            Vec3 q = W.transform_point({0.0f, 0.0f, len});
            const float ca = std::cos(an) * rr, sa = std::sin(an) * rr;
            q = {q.x + W.m[0] * ca + W.m[4] * sa, q.y + W.m[1] * ca + W.m[5] * sa, q.z + W.m[2] * ca + W.m[6] * sa};
            if (q.y < 3.5f) continue;                                                    // 0x6bd720
            ef2sim::SpawnParams sp;
            sp.side = 0;                                     // FUN_00450410(.., 0, 0, 0, 0, 1.0, 0, 0, -1): no stat
            spawn_sim_fx(ds.effect, q, {}, ds.scale, sp, 0, {});
        }
    }
    for (const auto& [fi, fv] : out.flags)                 // the end block's handshake (§8.2)
        std::printf("  [boss] t%llu flag %d = %d\n", (unsigned long long)tick_count, fi, fv);
    if (out.award_exp)
        std::printf("  [boss] t%llu EXP %.0f (lv %d)\n", (unsigned long long)tick_count, boss->exp_b0(), boss->level());
    static const bool m890_log = std::getenv("CLERIA_BOSSLOG") != nullptr;
    if (m890_log && tick_count % 60 == 0)
        for (const Actor& a : world.actors()) {
            if (a.cast_id != 0xaf || !a.alive()) continue;
            const Vec3 w = a.ai.v(0x894);
            std::printf("  [m890] t%llu #%d (%.2f,%.2f,%.2f) hp %.0f/%.0f phase %d 72c %d 730 %d act 0x%x steer %d dur %d W (%.1f,%.1f)\n",
                        (unsigned long long)tick_count, a.instance_id, a.pos.x, a.pos.y, a.pos.z, a.hp, a.max_hp,
                        a.ai.phase(), a.ai.i(0x72c), a.ai.i(0x730), unsigned(a.ai.i(0x710)), a.ai.i(0x708),
                        a.ai.i(0x714), w.x, w.z);
        }
    // Every live EF2-0x146 particle whose clock (p+0x1c0) is not 30 gets 30 / 30 (phase >= 2: the
    // ground bursts go off sooner) [BYTES 0x5111ac..0x51121d].
    if (out.shorten_146)
        for (const auto& pp : fxsim.particles()) {
            ef2sim::Particle& p = *pp;
            if (p.dead || !p.fx || p.fx->id != 0x146 || p.life == 30) continue;
            p.remaining = 30; p.life = 30;
        }
}
