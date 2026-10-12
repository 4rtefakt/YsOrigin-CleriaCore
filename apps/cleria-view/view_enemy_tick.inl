// The enemy handlers' tick: one pass over the room's enemies — the far freeze, the dead paths, the keep-out
// restore, the brain (enemy_ai_tick), the action player and the ASO step, the bullets / effects / hit volumes a
// section issues — then the brains' spawns. Textual part of view_game.inl's logic step, cut out on 2026-10-10 so
// that it can run more than once: on a live tick and under a dead player for every enemy (`fix0_tick` false), and
// under a Fix that holds slot 0 only (`fix0_tick` true) for the actors `fix0_runs` names: every handler 3..93 tests
// the actor-stop byte 0x74ab8b — slot 1 alone — and so runs on under a Fix(0) (build/mech_re/FIX_SLOTS.md §1.4;
// S2, issue #70: Hugo's eyes; S5c: the rest). Needs `fix0_tick` and `fix0_runs` in scope.
            {
                const int d = game.g_flags[201] ? 3 : (difficulty - 2);   // -2 v.easy .. 2 nightmare
                // The handlers' off-screen test FUN_0045d3c0: a SPHERE of 5.0 (0x6bd868) x 0.5
                // (0x6bd304, FUN_005f5b00) = 2.5 round the origin +0x50, outside when it lies wholly
                // behind any frustum plane (dist <= -r). Not a point on the feet: that let a body that
                // was already on screen stay frozen at alpha 1 (Oggnome's vanish, PLAYTEST_1.md §3).
                auto in_view = [&](const Vec3& q, float r) {
                    if (!have_vp) return true;
                    const auto& m = last_vp.m;
                    const float row[4][4] = {{m[0], m[4], m[8], m[12]}, {m[1], m[5], m[9], m[13]},
                                             {m[2], m[6], m[10], m[14]}, {m[3], m[7], m[11], m[15]}};
                    for (int k = 0; k < 6; ++k) {         // l r b t (|x|,|y| <= w), near z >= 0, far z <= w
                        float p[4];
                        for (int j = 0; j < 4; ++j)
                            p[j] = k == 4 ? row[2][j] : k == 5 ? row[3][j] - row[2][j]
                                 : row[3][j] + (k & 1 ? -1.0f : 1.0f) * row[k >> 1][j];
                        const float n = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
                        if (n > 0.0f && (p[0] * q.x + p[1] * q.y + p[2] * q.z + p[3]) / n <= -r) return false;
                    }
                    return true;
                };
                int pool = 0;
                for (const Actor& a : world.actors()) pool += a.alive();
                std::vector<std::pair<Actor*, AiSpawn>> brain_spawns;
                // The other actors, as brains that reach them see them (M460 and its bats).
                std::vector<Actor*> new_children;
                FnAiWorld aiw;
                aiw.each = [&](const std::function<void(Actor&)>& f) {
                    for (Actor& a : world.actors()) if (!a.is_player() && !a.destroy_requested()) f(a);
                };
                aiw.find_fn = [&](int id) -> Actor* {
                    for (Actor& a : world.actors())
                        if (a.instance_id == id && !a.is_player() && !a.destroy_requested()) return &a;
                    return nullptr;
                };
                aiw.spawn_fn = [&](Actor& p, int cast, const Vec3& dd, int lv, bool keep0) -> Actor* {
                    // FUN_00476d70: at the parent position + d (world axes), with its heading.
                    SpawnRequest req;
                    req.cast_type = cast;
                    req.pos = {p.pos.x + dd.x, p.pos.y + dd.y, p.pos.z + dd.z};
                    req.yaw_degrees = (1.5707963f - p.ai.heading) * 57.29577951f;
                    Actor* c = world.spawn(req);
                    if (!c) return nullptr;
                    c->height = cast_motion(casts, cast).height();
                    c->radius = cast_motion(casts, cast).radius();
                    c->level = lv;
                    c->floor_y = p.floor_y;
                    c->ai.heading = p.ai.heading;
                    c->ai.i(0x854) = p.instance_id;
                    c->ai.phase() = keep0 ? 0 : 4;
                    new_children.push_back(c);
                    return c;
                };
                aiw.remove_fn = [&](Actor& a) {                    // FUN_004593c0
                    world.request_destroy(a);
                    a.ai.hittable = false; a.hp = 0; a.visible = false;
                };
                // The edge probe's deep-water test (ENEMY_WATER.md W3): mode 1 only; the set-1 surface between
                // the far point and `depth` below it. [approx] by the up-ray from the segment's lower end.
                aiw.water_under_fn = [&](const Vec3& o, float depth) {
                    if (water_mode == 2 || !phys_mesh(room).has_water()) return -1.0e30f;
                    const float s = water_above(phys_mesh(room), o.x, o.y + depth, o.z, -depth);
                    return s > kNoFloor ? s : -1.0e30f;
                };
                aiw.ray_fn = [&](const Vec3& o, const Vec3& dl) {
                    AiWorld::Ray r;
                    r.hit = segment_hit(phys_mesh(room), o, {o.x + dl.x, o.y + dl.y, o.z + dl.z}, &r.point, &r.normal);
                    if (r.hit && dl.x == 0.0f && dl.z == 0.0f && dl.y < 0.0f) {   // rec+0xaa: a straight-down ray's surface
                        int tri = -1;
                        if (floor_at(phys_mesh(room), o.x, o.z, o.y, &tri) > kNoFloor && tri >= 0)
                            r.surface = surface_index_or_default(surface_of(phys_mesh(room), tri));
                    }
                    return r;
                };
                aiw.sphere_fn = [&](const Vec3& c, float r) { return sphere_overlaps(phys_mesh(room), c, r); };
                aiw.arena_fn = [&](float* r) { for (int k = 0; k < 4; ++k) r[k] = world.arena[k]; };
                aiw.flag_fn = [&](int i, int v) { if (i >= 0 && i < 0x200) game.g_flags[i] = v; };
                aiw.get_flag_fn = [&](int i) { return (i >= 0 && i < 0x200) ? game.g_flags[i] : 0; };
                // [approx] the handlers' +0x734 write lands on the world player actor, not on the
                // view's player_iframes.
                aiw.player_fn = [&]() -> Actor* { return world.player(); };
                aiw.hide_gauge_fn = [&] { hud.hide_boss_gauge(); };
                // The current scene's directory, e.g. "s_30\s_3080\", compared case-blind.
                aiw.effects_fn = ai_effects;                   // the GoEffect pool scans (view_duel.inl)
                aiw.break_fn = ai_break_shields;               // M860's pillar break (view_duel.inl)
                aiw.bullets_fn = [&](const std::function<void(const AiWorld::BulletRef&)>& f) {   // M420's orb hunt
                    for (auto& b : bullets.list()) {
                        if (b->gone || b->destroyed) continue;
                        AiWorld::BulletRef r;
                        r.handle = b->serial; r.kind = b->s.kind; r.owner = &b->s.owner; r.pos = b->s.pos; r.dir = &b->s.dir;
                        r.speed = &b->s.speed; r.f168 = &b->s.f168; r.f16c = &b->s.f16c; r.f170 = &b->s.f170;
                        f(r);
                    }
                };
                aiw.cull_fx_fn = [&](Actor& a, int id, int keep) {   // M710: e+0x1a4 == self+0x20 -> the actor matrix
                    const ef2sim::M4* parent = &actor_mats[a.instance_id];
                    int n = 0;
                    for (auto& f : fxsim.effects()) {
                        if (f->dead || f->id != id || f->attach_parent != parent) continue;
                        if (++n <= keep) continue;
                        if (ef2sim::Particle* pr = fxsim.first_particle(f.get())) pr->remaining = 0;   // p+0x1c4 = 0
                    }
                };
                // [approx] the region's blend rate (0x76a1fc) is the rig's own.
                aiw.camera_fn = [&](int r, float) { g_rig.write_forced(r); };
                aiw.sound_fn = [&](int id, const Vec3& p) { sfx_at(id, p, kSfxAnimVol); };
                aiw.restat_fn = [&](Actor& a, int c, int lv) {           // FUN_004202e0
                    const Enemy* er = etab.get(c);
                    if (!er) return;
                    StatMode sm;
                    sm.hard = game.g_flags[144]; sm.easy = game.g_flags[143] != 0; sm.very_easy = game.g_flags[203] != 0;
                    sm.boss_rush = game.g_flags[193] != 0; sm.retries = game.g_flags[196];
                    const EnemyStats st = enemy_restat(ginfo, *er, c, lv, sm);
                    a.max_hp = a.hp = st.max_hp; a.atk = st.atk; a.def = st.def; a.exp_reward = st.exp;
                    a.sp_reward = st.sp;
                    for (int k = 0; k < 4; ++k) a.resist[k] = er->resist[k];
                    a.level = lv ? lv : er->level;
                };
                aiw.in_room_fn = [&](const char* dir) {
                    std::string want = dir, have = (room.dir.parent_path().filename() / room.dir.filename()).string();
                    for (char& c : want) c = char(std::tolower((unsigned char)c) == '/' ? '\\' : std::tolower((unsigned char)c));
                    for (char& c : have) c = char(std::tolower((unsigned char)c) == '/' ? '\\' : std::tolower((unsigned char)c));
                    while (!want.empty() && want.back() == '\\') want.pop_back();
                    return have == want;
                };
                aiw.raw_fn = [&](int cast) -> const float* {
                    const Enemy* er = etab.get(cast);
                    return er ? er->ai_raw : nullptr;
                };
                const Vec3 cam_fwd = cam.forward();
                if (!fix0_tick) player_hold = boss6_grab || boss7_pin || boss8_pin;   // re-asserted by a holding Levenueba's brain tick; a head's grab (view_boss06.inl); Dalles's pin (view_boss07.inl); Darm's dead fall (view_boss08.inl)
                for (EnemyRender& e : render) {
                    Actor* en = e.actor;
                    if (fix0_tick && !(en && fix0_runs(*en))) continue;       // a Fix(0): the handlers that run (S2, S5c)
                    // M450's handler, every path (FUN_004c0ee0 0x4c1231): the Mask of Eyes tint +0x148 / +0x278.
                    if (en && en->ai_index == 38) en->tint = mask_of_eyes_on(inv) ? 0xff999999u : 0xffffffffu;
                    // The M680 / M740 handlers' far freeze, before their HP test: 25 u or more from the player's
                    // origin (squared ((dx2 + dy2) + dz2) not < 625, NaN included) -> only the ASO step.
                    // [approx] the world's integrator still runs on the actor (retail skips FUN_0045e460 too).
                    if (en && !en->destroy_requested() && e.avm.ok() && enemy_handler_far_freeze(en->ai_index)) {
                        const float fx = en->pos.x - feet.x, fy = (en->pos.y + en->height) - (feet.y + kOriginH),
                                    fz = en->pos.z - feet.z;
                        if (!(625.0f > (fx * fx + fy * fy) + fz * fz)) {
                            if (ai_debug && (tick_count % 60) == 0)   // no handler ran: say so (REACQUIRE.md §1.3)
                                std::printf("[ai] #%d cast %d phase %d frozen (25 u or more) t%llu\n", en->instance_id,
                                            en->cast_id, en->ai.phase(), (unsigned long long)tick_count);
                            e.spec.tick();
                            e.avm.tick();                         // the ASO step is all a frozen handler runs: its sounds too
                            e.spec_event();
                            for (const AsoVm::SoundEvent& se : e.avm.sounds()) aso_se_at(se.id, en->pos);
                            continue;
                        }
                    }
                    if (revive_tick(e)) continue;                 // the revive family's dead path
                    // M710's handler, the first dead tick (+0x3fc == 0, 0x4c3dd4): end its attached suction effects. The
                    // pool holds no new ones after that, so the view repeats it each dead tick to the same effect.
                    if (en && en->ai_index == 64 && !en->alive() && !en->destroy_requested()) aiw.cull_attached_fx(*en, 246, 0);
                    // M750's dead path (FUN_004bbc30 0x4bbc78): FUN_0047a7c0 on every effect it owns (fx+0x1a8 = self: its orb).
                    if (en && en->ai_index == 68 && !en->alive() && !en->destroy_requested())
                        for (auto& f : fxsim.effects())
                            if (!f->dead && f->owner == en->instance_id && f->tag >= 0) { fxsim.kill_effect(f.get()); f->tag = -1; }
                    // M700's dead path (FUN_004bce40 0x4bce72): FUN_0047a880 on its attached dash effect (+0x88c).
                    if (en && en->ai_index == 63 && !en->alive() && !en->destroy_requested()) fxsim.release_owned(en->instance_id, 1);
                    // The dead path of a handler that has extra work there (M460: its flock).
                    if (en && !en->alive() && !en->destroy_requested())
                        if (const EnemyDeathHook hk = enemy_death_hook_for(en->ai_index)) { hk(*en, aiw); boss_corpse(e); }
                    if (hugo_eye_dead(e)) continue;                  // a familiar's death tick (view_hugo_eyes.inl)
                    if (!en || (!en->alive() && !boss6_zava_undying(*en)) || en->destroy_requested() || !e.avm.ok()) continue;
                    // +0x314 is ONE field: a Fix's turns (world.tick_facing) and Chr_Pos write it
                    // through en->yaw, and the brain carries on from there. e.facing kept the
                    // pre-cutscene heading and snapped a script-turned enemy back (PLAYTEST_1.md §2).
                    // [approx] the brain's +0x308 copy (ai.heading; the Chr ops write look.cmd) is
                    // set to it too, so an idle brain does not turn back before it re-aims.
                    if (std::fabs(std::remainder(heading_of_yaw(en->yaw) - e.facing, 6.28318531f)) > 1e-4f)
                        e.facing = en->ai.heading = heading_of_yaw(en->yaw);
                    const bool hurt = e.avm.section() == 4;
                    const bool attacking = e.atk_section >= 0 && e.avm.section() == e.atk_section;
                    AiEnv env;
                    env.self = {en->pos.x, en->pos.y + en->height, en->pos.z};
                    env.height = en->height;
                    env.radius = en->radius;
                    env.facing = e.facing;
                    env.grounded = en->vert.grounded;                // +0x388
                    env.jump_v0 = cast_motion(casts, en->cast_id).jump_v0;
                    env.actor = en;
                    env.world = &aiw;
                    env.character = character;
                    env.sprite_dir = sprite_dir_of(en->yaw, cam_fwd);
                    env.aso_frame = e.avm.frame();
                    env.movable = e.avm.movable();
                    env.durable = e.avm.durable();
                    env.target_floor = last_floor;
                    env.target_rehit_clear = player_iframes == 0;
                    env.target = {feet.x, feet.y + kOriginH, feet.z};
                    env.target_vel = {pvel.x, 0, pvel.z};
                    env.target_alive = player.hp >= 1;
                    env.bloody = settings().s.game.bloody_effect_level > 1;   // byte 0x6d5d92 (FUN_0040b8e0)
                    env.target_durable = avm.durable();              // byte P+0x9d8
                    env.target_grounded = pv.grounded;
                    boss5_target(*en, env);                          // the +0x888 override (view_boss05.inl)
                    boss6_zava_env(*en, env);                        // Zava's link to her heads (view_boss06.inl)
                    env.was_hit = e.hit;
                    env.took_damage = e.dmg_hit;
                    env.events = e.events;                           // +0x3bc & 0x6000: the stun-rolling hits
                    env.launched = en->launch != 0;                  // +0x4d0: the brain does nothing while knocked up
                    env.group_tag = en->tag.tagged();
                    env.aso_marker = e.avm.marker();
                    env.aso_marker_block = e.avm.marker_block();
                    env.culled = !(en->flags12c & 0x1000000u) &&                     // never-cull bit
                                 !in_view({en->pos.x, en->pos.y + en->height, en->pos.z}, 2.5f);   // 0x480cb8..0x480cdc
                    env.stun_immune = (en->flags12c & 0x80000u) != 0;                  // FACTORY.md
                    env.bless_stun = (game.g_flags[217] & 0x80) != 0;                  // GROW09
                    env.boss_fight = game.g_flags[156] == 1;                           // DAT_0076bb8c
                    env.difficulty = d;
                    env.diff_index = difficulty >= 2 ? difficulty - 2 : 0;
                    env.diff_easy = difficulty <= 1;
                    env.diff_very_easy = difficulty == 0;
                    env.pool_count = pool;
                    env.target_section = avm.section();
                    env.target_heading = std::atan2(face_dir.z, face_dir.x);
                    env.turnable = e.avm.turnable();
                    env.target_surface = cur_surface;                                  // P+0x4b9
                    env.chimes = (inv.equip_flags & 0x8000u) != 0;                     // DAT_0076a624
                    env.target_moving = moving;                                        // P+0x514
                    env.mask_of_eyes = mask_of_eyes_on(inv);                           // DAT_0076bbac
                    const Aso* escr = enemy_script(e.stem);
                    env.section_type = [escr](int id) {
                        const Aso::Section* s = escr ? escr->find(id) : nullptr;
                        return s ? int(s->type) : -1; };
                    env.blocked = [&](const Vec3& a0, const Vec3& b0) {
                        return segment_blocked(phys_mesh(room), a0, b0); };
                    env.has_floor = [&](float x, float z, float from_y, float depth) {
                        const float f = floor_at(phys_mesh(room), x, z, from_y);
                        return f > kNoFloor && from_y - f <= depth; };
                    env.rng = [&] { return std::uint32_t(rng()); };
                    if (hugo_eye(*en)) {                             // a familiar: FUN_00470210 reads the player
                        fam_refresh();
                        fam.culled = !in_view(env.self, 2.5f);       // FUN_0045d3c0 (the sphere above)
                        env.culled = false;                          // the familiar handler has no cull gate
                        env.familiar = &fam;
                    }
                    e.hit = false;
                    e.dmg_hit = false;
                    e.events = 0;
                    env.hp = en->hp; env.max_hp = en->max_hp; env.section = e.avm.section();
                    en->live_section = env.section;
                    env.cam_back = {-cam_fwd.x, 0.0f, -cam_fwd.z};   // [H] M470.md §7 #3
                    // FUN_0042dc90's restore, the first thing a handler does past its far freeze and its dead path
                    // (KEEPOUT_BOXES.md §3): a monster does not stand in one of the room's exit volumes. Not the
                    // handlers without the call (0..2, 83 = M900) nor a script puppet (FUN_0045f250).
                    // [not modelled] M230 (16): FUN_004c1260 runs the same test against +0x808, which that brain
                    // also uses for its relocation offset and snapshots only in the launch state [BYTES 0x4c136e..
                    // 0x4c1396] — left as it was. Zava (80) has the restore inlined in her brain (m870.cpp).
                    if (en->ai_index >= 3 && en->ai_index != 83 && en->ai_index != 16 && en->ai_index != 80 && !hugo_eye(*en) &&
                        !enemy_handler_puppet(en->ai_index, env)) {
                        Vec3 org = env.self;
                        if (ai::keep_out(en->ai, org, exit_volume)) {
                            if (keepout_log)
                                std::printf("  [keepout] t%llu #%d cast %d (%.2f, %.2f, %.2f) -> (%.2f, %.2f, %.2f)\n",
                                            (unsigned long long)tick_count, en->instance_id, en->cast_id, env.self.x, env.self.y,
                                            env.self.z, org.x, org.y, org.z);
                            en->pos = {org.x, org.y - en->height, org.z};
                            env.self = org;
                        }
                    }
                    const EnemyBrain own = enemy_brain_for(en->ai_index);
                    const int phase_before = e.actor->ai.phase();
                    AiOut o = enemy_ai_tick(e.actor->ai, e.aip, env, own ? own : enemy_brain_or_standin(en->ai_index));
                    boss6_zava_post(*en, env, o);                    // Zava's handler past the action player (view_boss06.inl)
                    if (phase_before == 2 && e.actor->ai.phase() == 3 && std::getenv("CLERIA_DMG"))
                        std::printf("  [stun] t%llu #%d cast %d stunned for %d ticks (events %x)\n", (unsigned long long)tick_count,
                                    en->instance_id, en->cast_id, e.actor->ai.dur(), env.events);
                    if (phase_before == 2 && e.actor->ai.phase() == 1 && los_log) los_report(*en, env);   // view_los.inl
                    // The brain's own writes to its HP (+0x98) / EXP (+0xb0), and its spawns.
                    if (o.set_hp >= 0) en->hp = o.set_hp;
                    if (o.launch_clear) en->launch = 0;              // the stun's end (0x479b00: +0x4d0 = 0)
                    // M280's latch on the player: DAT_0076c15c |= 8, DAT_0076c164 = 1, a direct write (no
                    // effect, SE or popup) [BYTES 0x4a40bb, 0x4a40c2].
                    if (o.heavy_hold) { ail.mask |= ailments::kHeavy; ail.t[1] = 1; }
                    en->exp_reward *= o.exp_scale;
                    for (const AiSpawn& s : o.spawns) brain_spawns.push_back({en, s});
                    // FUN_00479b00 -> FUN_00478510's alive path (hit_feedback.hpp): one game-RNG draw
                    // (the scale), the character's hit spark one unit toward the camera; SE 92 is in
                    // o.sounds; then its tail (LAB_004799f0 [EXPORT 00478510.c:738-757]): three blood splats at the
                    // origin (18 draws) in the cast's ENEMYINFO +0xe0 colour — 0x40101010 for a cast without one, and
                    // then only under 1 HP — unless +0x12c bit 31, BloodyEffectLevel 0 or the victim is in water.
                    if (o.hit_spark) {
                        en->hit_flash = hitfx::kHurtTimer;           // +0x238 = 1.65, before the 0x1a return
                        const float sc = hitfx::spark_scale(pick_rng.next(), hitfx::kAliveLo, hitfx::kAliveHi);
                        const int sid = hitfx::hit_spark_effect(character, game.g_flags[149], true, avm.section());
                        if (sid >= 0 && !hitfx::alive_spark_suppressed(en->cast_id, game.g_flags[156] != 0)) {
                            const hitfx::Spark sk = hitfx::place_spark({en->pos.x, en->pos.y + en->height, en->pos.z},
                                                                       cam_fwd, o.hit_spark_dx, o.hit_spark_dz, sc);
                            ef2sim::SpawnParams sp; sp.side = 0;          // +0x1d7 = 0, +0x1d6 = 0xff
                            spawn_sim_fx(sid, sk.pos, sk.rot, sk.scale, sp, 0, {});
                            if (sfx_log) std::printf("  [hitfx] t%llu spark 0x%x x%.2f\n", (unsigned long long)tick_count, sid, sc);
                        }
                        const Enemy* br = etab.get(en->cast_id);
                        const std::uint32_t bcol = br ? br->blood : 0u;
                        if (!(en->flags12c & 0x80000000u) && settings().s.game.bloody_effect_level > 0 && !en->ai.in_water &&
                            (bcol != 0 || en->hp < 1.0f))
                            blood_at("hit", 3, bcol ? bcol : 0x40101010u, 2, 0x18, 1.3f, 1.8f,
                                     {en->pos.x, en->pos.y + en->height, en->pos.z}, 0.65f, false, false);
                    }
                    for (const AiSplat& sp : o.splats)               // a slime brain's puddles (FUN_0058a5f0, flag B)
                        blood_at("slime", sp.count, sp.argb, sp.delay_step, sp.life_range, sp.size0, sp.size1, sp.pos,
                                 sp.radius, sp.flag_a, sp.flag_b);
                    for (int id : o.sounds) sfx_at(id, en->pos, kSfxAnimVol);
                    if (o.popup >= 0) damage_popup(o.popup, 0xffffff, {en->pos.x, en->pos.y + en->height, en->pos.z});   // FUN_004fae00
                    if (o.zoom_s || o.zoom_r) zoom_blur(o.zoom_s.value_or(zb.s), o.zoom_r.value_or(zb.r));   // ZoomBlur
                    if (o.zoom_centre) zoom_centre(*o.zoom_centre);                                         // FUN_005e0590
                    // M470's hold (0x48ce69): the player's origin at the mouth, +0x330 = 0, +0x398 = 1 until a brain
                    // tick of that Levenueba passes without it (its release, its death, a room change).
                    // [not modelled] FUN_00459ff0's floor / facing writes.
                    // M860's freeze (0x4e39eb..0x4e3a35): x / z only, the knock channels 1 and 2 zeroed, the VM's wait
                    // counter + 1; y, +0x330 and +0x398 untouched. Its start asks the player's VM for section 9 through
                    // the gated FUN_00446550 (0x4e397e).
                    if (o.target_section >= 0) {
                        const bool took = avm.set_section(o.target_section);
                        std::printf("  [freeze] t%llu #%d: section %d %s, pinned at (%.2f, %.2f)\n", (unsigned long long)tick_count,
                                    en->instance_id, o.target_section, took ? "taken" : "refused", feet.x, feet.z);
                    }
                    if (o.hold_target && o.hold_xz_only) {
                        feet.x = o.hold_origin.x; feet.z = o.hold_origin.z;
                        player_kb = {}; player_kb2 = {};
                        avm.add_wait(o.target_wait_add);
                    } else if (o.hold_target) {
                        feet = {o.hold_origin.x, o.hold_origin.y - kOriginH, o.hold_origin.z};
                        pv.t = 0.0f;
                        player_hold = true;
                        if (ai_debug) std::printf("[hold] #%d holds the player at (%.2f, %.2f, %.2f)\n", en->instance_id,
                                                  o.hold_origin.x, o.hold_origin.y, o.hold_origin.z);
                    }
                    // [not modelled] attachment to the actor (FUN_00451c20) and the scale.
                    // The brain's effects, through the particle simulation (their hit fields arm
                    // the EF2's hit emitters; EFFECTHIT.md). Owned ones are killed / re-placed by tag.
                    for (const AiEffect& fx : o.effects) {
                        const Vec3 org{en->pos.x, en->pos.y + en->height, en->pos.z};
                        if (fx.kill_tag >= 0) { fxsim.kill_owned(en->instance_id, fx.kill_tag); continue; }
                        if (fx.remain_tag >= 0) { fxsim.set_remaining_owned(en->instance_id, fx.remain_tag, fx.remaining); continue; }
                        if (fx.release_tag >= 0) { fxsim.release_owned(en->instance_id, fx.release_tag); continue; }
                        if (fx.dying_tag >= 0) { fxsim.dying_owned(en->instance_id, fx.dying_tag); continue; }
                        if (fx.move_tag >= 0) {
                            const float ms = fx.move_scale > 0.0f ? fx.move_scale : 1.0f;
                            fxsim.move_owned(en->instance_id, fx.move_tag, fx.pos, fx.rot, {ms, ms, ms});
                            continue;
                        }
                        ef2sim::SpawnParams sp;
                        sp.power = fx.power; sp.knock = fx.knock; sp.rtype = fx.rtype; sp.status = fx.status;
                        sp.atk = en->atk; sp.side = 1; sp.owner = en->instance_id; sp.tag = fx.tag;
                        if (hugo_eye(*en)) hugo_eye_brain_fx(*en, sp, fx.id);   // the player's side
                        ef2sim::Effect* sfx = spawn_sim_fx(fx.id, fx.pos, fx.rot, fx.scale, sp, fx.attach ? en->instance_id : 0, org);
                        // FUN_00451c20 mode 1 (M700's dash effect): fx = local x the actor's full matrix. [H] as
                        // spawn_generate_effect: actor_mats carry the translation only, so the yaw is baked here.
                        if (sfx && fx.attach && fx.attach_full) {
                            sfx->local = ef2sim::mul(sfx->local, ef2sim::rot_ypr(-(e.facing + 1.5707963f), 0.0f, 0.0f));
                            sfx->attach_mode = 1;
                        }
                    }
                    pool += int(o.spawns.size());
                    if (ai_debug && (tick_count % 60) == 0)
                        std::printf("[ai] #%d cast %d phase %d sub %d act 0x%x steer %d mode %d spd %.4f "
                                    "dist %.2f head %.2f culled %d sec %d mov %.2f hp %.1f mk %d at (%.1f,%.1f) alpha %.2f t%llu\n",
                                    en->instance_id, en->cast_id, e.actor->ai.phase(), e.actor->ai.i(0x728), e.actor->ai.action(),
                                    e.actor->ai.i(0x708), e.actor->ai.mode(), e.actor->ai.speed, e.actor->ai.dist(), e.actor->ai.heading,
                                    int(env.culled), e.avm.section(), e.avm.movable(), en->hp, env.aso_marker,
                                    en->pos.x, en->pos.z, e.actor->ai.alpha, (unsigned long long)tick_count);
                    // Strike: the brain's FUN_00446550(sec,0) — a priority-gated request. A
                    // ported brain names its section literally; the M100 stand-in's 7 maps to
                    // this kind's guessed attack section.
                    const int want = (!own && o.section == 7 && e.atk_section >= 0) ? e.atk_section : o.section;
                    if (o.section >= 0 && (!hurt || o.section_force) && e.avm.set_section(want, o.section_force)) {
                        if (own && o.attack) e.atk_section = want;
                        e.atk_dx = std::cos(e.facing);
                        e.atk_dz = std::sin(e.facing);
                        e.atk_hit = false; e.atk_stall = 0; e.atk_fkey = -1;
                        e.atk_armed = false; e.atk_age = 0;
                    }
                    if (o.vm_step) {                                  // the brain's FUN_004472e0
                        e.avm.tick();
                        e.spec_event();
                        if (!hugo_eye(*en) && e.avm.section() != 5) for (const AsoVm::SoundEvent& se : e.avm.sounds()) aso_se_at(se.id, en->pos);
                    }
                    // The brain writes its ASO registers directly (+0x9bc / +0x9c0 / +0x9d8);
                    // a section that restarts at pc 0 this tick clears the first two again.
                    if (o.set_marker > -1000) e.avm.set_marker(o.set_marker);
                    if (o.set_marker_block > -1000) e.avm.set_marker_block(o.set_marker_block);
                    if (o.set_durable >= 0) e.avm.set_durable(o.set_durable != 0);
                    if (o.set_own_jump >= 0) e.avm.set_own_jump(o.set_own_jump != 0);
                    if (o.set_frame >= 0) e.avm.set_frame(o.set_frame);
                    if (e.actor->ai.hit_cut) { e.atk_win = 0; e.actor->ai.hit_cut = false; }   // disengage
                    if (o.fast_forward >= 0) e.avm.fast_forward_to_marker(o.fast_forward);
                    if (o.snap_facing) e.facing = e.actor->ai.heading;
                    // A hit volume armed by the brain (FUN_004494e0), placed like any other.
                    for (const AiHitbox& h : o.hitboxes) {
                        if (h.new_serial) en->hit_serial = next_hit_serial(world.hit_serial_counter_);
                        const float args[13] = {h.shape, h.radius, h.fwd, h.angle_deg, h.voff, h.half_h,
                                                float(h.life), float(h.rehit), float(h.one_shot),
                                                float(h.rtype), float(h.status), h.power, h.knock};
                        e.hb = volume_from_args(args, en->hit_serial);
                        e.atk_win = h.life > 0 ? h.life : 1;
                        e.atk_armed = true;
                        e.atk_dx = std::cos(e.facing); e.atk_dz = std::sin(e.facing);
                    }
                    // GoXx bullets the brain made directly (FUN_00593550 / FUN_00593b90).
                    for (const AiBullet& b : o.bullets) {
                        bullet::Spawn s;
                        s.kind = b.kind; s.effect = b.effect;
                        const Actor* ow = b.owner < 0 ? en : world.find(b.owner);
                        s.owner = ow ? ow->instance_id : 0;
                        s.owner_monster = !(ow && ow->is_player());
                        s.owner_atk = ow ? float(ow->atk) : 0.0f;
                        s.pos = b.pos; s.dir = b.dir;
                        s.speed = b.speed; s.accel = b.accel; s.gravity = b.gravity;
                        s.power = b.power; s.knock = b.knock;
                        s.f158 = std::uint32_t(b.trail_effect); s.f15c = std::uint32_t(b.trail_period);
                        s.f160 = std::uint32_t(b.home_period); s.f164 = b.home_rate;
                        s.f168 = b.f168; s.f16c = b.f16c; s.f170 = b.f170; s.f174 = b.f174;
                        bullet::Bullet* bl = bullets.spawn(s);
                        if (!bl) continue;
                        if (b.fx_scale > 0.0f) {                            // the brain's +0x1ac.. write
                            bl->fx->scale_x = bl->fx->scale_y = bl->fx->scale_z = b.fx_scale;
                        }
                        if (b.gas) gas_owner[bl] = en->instance_id;
                        if (b.root_life >= 0 && bl->fx)                     // the brain's root write (M980)
                            if (ef2sim::Particle* rp = fxsim.first_particle(bl->fx)) {
                                rp->remaining = b.root_remaining; rp->life = b.root_life;
                            }
                    }
                    if (o.gauge_hp >= 0) {                         // FUN_0043a460: the boss gauge
                        gauge_owner = en->instance_id;
                        // The brain's own lenScale when it gives one (M850 / M810 / M830); else
                        // M460's: 3000, 4000 when the player is Hugo (ARENA_AND_BOSSBAR §3.1).
                        // None of them writes the emblem (0, the default).
                        hud.set_boss_gauge(en->instance_id, o.gauge_hp, en->max_hp,
                                           o.gauge_len > 0.0f ? o.gauge_len : (character == 2 ? 4000.0f : 3000.0f),
                                           o.gauge_emblem);
                    }
                    // The mover (FUN_00478080 -> FUN_0045f4d0, build/ai_spec/MOVER.md): the
                    // locomotion section on an index change (gated), the ASO step (Jump/Dash/
                    // Movable land here), then velocity + knockback, then the facing turn. The
                    // integrator (sweep + settle) runs in the per-actor pass after world.step().
                    const bool grounded_now = en->vert.grounded;
                    const bool movable_pre = e.avm.movable() != 0.0f;
                    int idx_pre = 0;
                    // (o.plain_mover: the g_flags[131] branch, FUN_0045f470 — the moving byte +0x514 = 0)
                    const int lsec = mover_pre(en->mover, o.plain_mover ? 0 : e.actor->ai.mode(), grounded_now, e.avm.own_jump(),
                                               movable_pre, e.avm.section(), &idx_pre);
                    if (lsec >= 0) e.avm.set_section(lsec);          // FUN_00446550: priority-gated
                    e.avm.set_is_land(grounded_now);
                    e.spec.tick();                                    // FUN_0045a3a0, before the VM (ASO_NOOPS.md §1.2)
                    e.avm.tick();
                    e.spec_event();                                   // anime_ChangeSpecular: the wind-up flash
                    if (spec_log && e.avm.specular())
                        std::printf("  [spec] t%llu #%d cast %d section %d: %06x -> %06x step %.3f count %d\n",
                                    (unsigned long long)tick_count, en->instance_id, en->cast_id, e.avm.section(), e.spec.b, e.spec.a,
                                    e.spec.step, e.spec.count);
                    else if (spec_log && e.spec.step != 0.0f)
                        std::printf("  [spec] t%llu #%d specular %06x\n", (unsigned long long)tick_count, en->instance_id,
                                    e.spec.colour());
                    // anime_SePlay3D (funcid 42) of a monster's own sections: its steps, wing beats and attack
                    // cries (build/audit/SOUNDS.md root cause 1 -- the loop read bullets, effects and the hit
                    // volume but never the sounds). Not a familiar (view_hugo_eyes.inl plays its own) and not the
                    // death section, whose sounds the kill path plays from its cache (enemy_death_sounds).
                    if (!hugo_eye(*en) && e.avm.section() != 5) for (const AsoVm::SoundEvent& se : e.avm.sounds()) aso_se_at(se.id, en->pos);
                    if (e.avm.jumped()) {                             // anime_Jump (FUN_00449460)
                        float oy = en->pos.y + en->height;
                        apply_jump(en->vert, oy, e.avm.jump_v(), cast_motion(casts, en->cast_id).jump_v0);
                        en->pos.y = oy - en->height;
                    }
                    if (e.avm.dashed()) apply_dash(en->mover, e.actor->ai.heading, e.avm.dash_d());
                    mover_post(en->mover, idx_pre, e.actor->ai.speed, e.actor->ai.heading, en->vert.grounded,
                               en->flyer, movable_pre, e.avm.movable(), en->hp, en->flags12c, en->ai.surf);
                    if (o.add_dx != 0.0f || o.add_dz != 0.0f) { en->mover.dx += o.add_dx; en->mover.dz += o.add_dz; }   // M980 0x4d71c6
                    if (e.facing != e.actor->ai.heading)              // FUN_005eee60, x +0x36c (1.0)
                        e.facing = turn_facing(idx_pre == 2 ? 0.35f : 0.18f, e.facing, e.actor->ai.heading);
                    en->yaw = 1.5707963f - e.facing;   // view basis (sin, cos)
                    {                                                 // the handler-tail clamps
                        Vec3 org{en->pos.x, en->pos.y + en->height, en->pos.z};
                        enemy_handler_clamp(en->ai_index, org, game.g_flags[190], game.g_flags[156] != 0, world.arena,
                                            game.g_flags[131], e.actor->ai.phase());
                        en->pos = {org.x, org.y - en->height, org.z};
                    }
                    if (o.durable_rule == 1) e.avm.set_durable(true);              // the handler's
                    else if (o.durable_rule == 2 && e.avm.section() == 0x138) e.avm.set_durable(true);
                    else if ((o.durable_rule == 0 || o.durable_rule == 2) && e.avm.section() != 7) e.avm.set_durable(false);
                    if (hurt && e.avm.section_ended()) e.avm.set_section(e.idle_section, /*force*/true);
                    // anime_FireBullet (the VM case at 0x447b91, BULLETS.md §1.2): at the origin +
                    // (cos, sin)(arg4 deg + facing) x arg2, y + arg3; the direction args 5..7 or
                    // that heading; args 8.. straight into the block (13..15 as raw words).
                    for (const AsoVm::BulletEvent& fb : e.avm.bullets()) {
                        const float ang = fb.f[2] * 0.0174533f + e.facing;
                        const float c = float(std::cos(double(ang))), sn = float(std::sin(double(ang)));
                        bullet::Spawn s;
                        s.kind = fb.kind; s.effect = fb.id;
                        s.owner = en->instance_id; s.owner_atk = float(en->atk);
                        if (hugo_eye(*en)) hugo_eye_bullet(*en, s);       // side 0, FUN_0046d650's class
                        s.pos = {en->pos.x + c * fb.f[0], en->pos.y + en->height + fb.f[1], en->pos.z + sn * fb.f[0]};
                        Vec3 d{fb.f[3], fb.f[4], fb.f[5]};
                        if (d.x * d.x + d.y * d.y + d.z * d.z == 0.0f) d = {c, 0.0f, sn};
                        const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                        s.dir = dl == 0.0f ? Vec3{} : Vec3{d.x / dl, d.y / dl, d.z / dl};
                        s.speed = fb.f[6]; s.accel = fb.f[7]; s.gravity = fb.f[8];
                        s.power = fb.f[9]; s.knock = fb.f[10];
                        std::memcpy(&s.f158, &fb.f[11], 4);
                        std::memcpy(&s.f15c, &fb.f[12], 4);
                        std::memcpy(&s.f160, &fb.f[13], 4);
                        s.f164 = fb.f[14] * 0.0174533f;
                        s.f168 = fb.f[15]; s.f16c = fb.f[16]; s.f170 = fb.f[17]; s.f174 = fb.f[18];
                        bullets.spawn(s);
                    }
                    // anime_GenerateEffect (FUN_004496f0, EFFECTHIT.md §6): at origin + (cos, sin)
                    // (angle + facing) x fwd, y + voff, yaw angle - (facing + pi/2), its args 6..9
                    // as the hit fields; attach mode != 0 follows the actor (spawn_generate_effect).
                    for (const AsoVm::EffectEvent& fe : e.avm.effects()) {
                        const Vec3 org{en->pos.x, en->pos.y + en->height, en->pos.z};
                        ef2sim::SpawnParams sp;
                        sp.power = fe.power; sp.knock = fe.knock; sp.rtype = fe.rtype; sp.status = fe.status;
                        sp.atk = en->atk; sp.side = 1; sp.owner = en->instance_id;
                        if (hugo_eye(*en)) hugo_eye_effect(*en, sp, fe.id);  // side 0, rtype 3, kept in +0x890
                        spawn_generate_effect(fe, org, e.facing, sp, en->instance_id);
                    }
                    // Its GenerateCollision arms the enemy's hit-volume, with the section's own
                    // radius/lifetime/power/knock.
                    if (e.avm.armed_hitbox()) {
                        e.hb = e.avm.hitbox();
                        e.atk_win = e.hb.life > 0 ? e.hb.life : 1;
                        e.atk_hit = false;
                        e.atk_armed = true;
                    }
                    // [STAND-IN] for the stand-in brain only: release a swing that holds its last
                    // frame once its box has fired and the clip has stalled. A ported brain is the
                    // engine's own FUN_00446550 driver and its sections end by `end` chaining, so
                    // the release would cut its long holds short.
                    if (!own && e.avm.section() == e.atk_section) {
                        ++e.atk_age;
                        const long fk = (long(e.avm.anim_tag()) << 20) ^ e.avm.frame();
                        e.atk_stall = (fk == e.atk_fkey) ? e.atk_stall + 1 : 0;
                        e.atk_fkey = fk;
                        const bool recovered = e.atk_armed && e.atk_win <= 0 && e.atk_stall >= 8;
                        if (recovered || e.atk_age >= 180) {
                            e.avm.set_section(e.idle_section, /*force*/true);
                            e.loco = -1;
                        }
                    }
                }
                // A brain's spawns (FUN_00476d70: a fresh actor of that cast at the given
                // spot and heading, phase 0). Appended after the loop: `render` can't grow
                // while it is being iterated. The child shares its parent's sprite set.
                for (auto& [parent, s] : brain_spawns) {
                    SpawnRequest req;
                    req.cast_type = s.cast;
                    req.pos = s.pos;
                    req.yaw_degrees = (1.5707963f - s.yaw) * 57.29577951f;   // heading -> view yaw
                    Actor* c = world.spawn(req);
                    if (!c) continue;
                    if (s.hp >= 0) c->hp = s.hp;
                    if (s.exp_copy) c->exp_reward = parent->exp_reward;
                    const std::string stem = casts.sprite_stem(c->cast_id);
                    if (stem.empty() || !efx.kinds.count(stem)) continue;
                    const Cast* cc = casts.get(c->cast_id);
                    render.push_back({c, stem, (cc && !cc->display.empty()) ? cc->display : stem});
                    bind_one_enemy(render.back());
                }
                for (Actor* c : new_children) {
                    const std::string stem = casts.sprite_stem(c->cast_id);
                    if (!stem.empty() && !efx.kinds.count(stem)) efx.load_kind(g, asset_root, stem);
                    if (stem.empty() || !efx.kinds.count(stem)) continue;
                    const Cast* cc = casts.get(c->cast_id);
                    const float heading = c->ai.heading;
                    render.push_back({c, stem, (cc && !cc->display.empty()) ? cc->display : stem});
                    bind_one_enemy(render.back());
                    c->ai.heading = heading;
                    render.back().facing = heading;
                    if (c->aso_local15) render.back().avm.set_local(15, c->aso_local15);   // +0xa18
                }
                // The boss gauge disappears with its owner (DAT_006d5fd8 cleared at death).
                if (!fix0_tick && hud.boss_frac >= 0.0f && gauge_owner != 0) {
                    bool live = false;
                    for (const Actor& a : world.actors()) live |= a.instance_id == gauge_owner && a.alive();
                    if (!live) { hud.hide_boss_gauge(); gauge_owner = 0; }
                }
            }
