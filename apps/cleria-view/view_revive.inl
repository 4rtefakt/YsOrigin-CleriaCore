// The revive handler family's host side (src/cleria/brains/revive.hpp, build/ai_spec/REVIVE.md):
// M560 Karon, M570 Zarue, M780 Zelkaron (and M430 / M670 when their brains land). Included in the
// tick body just before the enemy AI block, so the integrator pass sees enemy_surfaces too.
//
// enemy_surfaces(a): the integrator's surface bytes and FUN_0045c490's selector-1 probe for an
// enemy (MOVER.md §3: +0x4ba = the floor ray's surface, +0x4b9 latched within 1.6; +0x320 / +0x38a
// = the "water" set above — the sand top in S_40). The probe starts 0.25 under the feet, 0.7 on
// sand2 (the player's 0x6bd244 and its sand2 variant), and counts as in it past a depth of 1.1
// (0x6bd464), as the player's water test.
// ENEMY_WATER.md W1 (src/cleria/water.hpp): the whole test — a flooded room (mode 2) is "submerged" with no
// ray, the wading and entered bytes, the timer's cap at 0.1 on a change of +0x38a, and no probe at all for a
// cast with +0x12c & 0x8000 (M590, M910..M960, M270). [approx] for those the bytes should keep what the
// placement FUN_00459ff0 found; the port has no placement probe, so they stay dry in a mode-1 room.
// W5, mode 1 [EXPORT 0045c490.c:143-266; BYTES 0x45c90d..0x45ca04, 0x45cb13..0x45cb4b]: the splash on a wading
// change or a surfacing — effect 0x49 for any cast but 13 (0x163 on sand2) at (x, surface + 0.15, z), SE 0x30 (0x28
// on sand2), 15 ticks apart (+0x4c3) — and the bubbles of a live submerged actor at a probe depth of 1.6 or more
// (0x6bd4ec), not on sand2: effect 0x3d at the origin + 0.25 along the facing, on the ticks the two counters name
// (+0x4c5 = 90 on each change of +0x38a, 25 with +0x12c & 0x800; the breath cycle +0x4c6 of 125).
// [approx] the cull byte +0x250 (no splash while culled) and the SE's gate [0x769f1c] < 0.8 are not kept; an actor's
// first test stands for the placement's probe FUN_00459ff0, which sets the bytes with no splash and no bubble burst.
// [not modelled] lava / blood (splash 0x52, bubbles 0x4f), a flooded room's bubbles (mode 2: they rise to a ray's
// hit), the ripples of a wading actor (FUN_00454ff0, every 8th tick), the effects' in-water layer byte fx+0x1da, and
// the half colour — which is the sprite's second (layer) quad only, not the body [EXPORT 004740e0.c:29-34].
auto enemy_surfaces = [&](Actor& a) {
    const RoomMesh& pm = phys_mesh(room);
    const float oy = a.pos.y + a.height;
    int tri = -1;
    const float fl = floor_at(pm, a.pos.x, a.pos.z, oy, &tri);
    if (fl > kNoFloor && tri >= 0) {
        a.ai.surf_ray = surface_index_or_default(surface_of(pm, tri));
        if (oy - fl < 1.6f) a.ai.surf = a.ai.surf_ray;
    }
    WaterBytes wb;
    wb.wading = a.ai.wading; wb.submerged = a.ai.in_water; wb.surface = a.ai.water_y;
    const int mode = water_mode == 2 ? 2 : pm.has_water() ? 1 : 0;
    const bool skip = (a.flags12c & 0x8000u) != 0;
    const float o0 = a.pos.y - kWaterProbeDrop - (a.ai.surf_ray == 0x12 ? kWaterProbeSand : 0.0f);   // [EXPORT 0045c490.c:137-141] (0.7 alone until 2026-10-11)
    const float s = mode == 1 && !skip ? water_above(pm, a.pos.x, o0, a.pos.z) : kNoFloor;
    const bool splash = water_test(wb, a.vert, mode, skip, s > kNoFloor, s, o0);
    if (mode == 1 && !skip) {
        if (a.ai.splash_cd) --a.ai.splash_cd;
        if (a.ai.bubble_t) --a.ai.bubble_t;
        if (a.ai.breath_t) --a.ai.breath_t;
        const bool sand2 = a.ai.surf_ray == 0x12;
        const bool first = !a.ai.water_seen;
        a.ai.water_seen = true;
        if (s > kNoFloor && wb.submerged != a.ai.in_water && !first) a.ai.bubble_t = (a.flags12c & 0x800u) ? 25 : 90;
        if (splash && a.ai.splash_cd == 0 && !first) {
            a.ai.splash_cd = 15;
            spawn_sim_fx(sand2 ? 0x163 : 0x49, {a.pos.x, s + 0.15f, a.pos.z}, {}, 1.0f, {}, 0, {});
            sfx_at(sand2 ? 0x28 : 0x30, a.pos, kSfxAnimVol);
        }
        if (s > kNoFloor && wb.submerged && a.hp >= 1.0f) {
            const std::uint8_t c5 = a.ai.bubble_t, c6 = a.ai.breath_t;
            bool bub = (c5 & (c5 > 15 ? 3 : 7)) == 1;
            if (c6 < 0x1e) {
                if (!(a.flags12c & 0x800u) && (c6 & (c6 < 0x10 ? 7 : 3)) == 1) bub = true;
                if (c6 == 0) a.ai.breath_t = 0x7d;
            }
            if (bub && s - o0 >= 1.6f && !sand2)
                spawn_sim_fx(0x3d, {a.pos.x + std::sin(a.yaw) * 0.25f, oy, a.pos.z + std::cos(a.yaw) * 0.25f}, {}, 1.0f, {}, 0, {});
        }
    }
    a.ai.wading = wb.wading; a.ai.in_water = wb.submerged; a.ai.entered = wb.entered; a.ai.water_y = wb.surface;
};
// The world facts the family's handlers read.
auto revive_world = [&]() {
    revive::World w;
    w.chimes = (inv.equip_flags & 0x8000u) != 0;                  // DAT_0076a624 & 0x8000
    std::string have = (room.dir.parent_path().filename() / room.dir.filename()).string();
    for (char& c : have) c = char(c == '/' ? '\\' : std::tolower((unsigned char)c));
    w.in_4005 = have == "s_40\\s_4005";
    w.flag179 = game.g_flags[179];
    w.flag180 = &game.g_flags[180];
    w.flag265 = &game.g_flags[265];
    return w;
};
// M290 Bicmobre's handler death helper FUN_004a2f90 (M290.md §2.1): on each of the dead ticks the
// engine runs before FUN_004780e0 removes the actor (dead tick 44; +0x3fc counts them here), the
// bombs it throws, as kind-0xf objects (bullet.cpp).
auto m290_dead_tick = [&](EnemyRender& e) {
    Actor* en = e.actor;
    if (!en || en->ai_index != 22 || en->alive() || en->destroy_requested() || en->ai.dead_t >= 44u) return;
    en->ai.dead_t += 1;
    const int bb5c = difficulty >= 2 ? difficulty - 2 : 0;
    for (const AiBullet& b : brains::m290_dead(*en, bb5c, [&] { return std::uint32_t(rng()); })) {
        bullet::Spawn s;
        s.kind = b.kind; s.effect = b.effect;
        s.owner = en->instance_id; s.owner_monster = true; s.owner_atk = float(en->atk);
        s.pos = b.pos; s.dir = b.dir; s.speed = b.speed; s.accel = b.accel; s.gravity = b.gravity;
        s.power = b.power; s.knock = b.knock;
        bullets.spawn(s);
        if (ai_debug) std::printf("[ai] t%llu M290 #%d death bomb (dead tick %u)\n",
                                  (unsigned long long)tick_count, en->instance_id, en->ai.dead_t);
    }
};
// The handler's pre-brain part for one enemy of the family (alive or dead): the fresh surfaces, the
// sand clamp, the EXP block; on a dead tick the whole dead path (true = handled, skip the brain).
auto revive_tick = [&](EnemyRender& e) -> bool {
    m290_dead_tick(e);
    Actor* en = e.actor;
    const revive::Row* rr = en ? revive::row(en->ai_index) : nullptr;
    if (!rr || en->destroy_requested() || !e.avm.ok()) return false;
    const revive::World w = revive_world();
    enemy_surfaces(*en);
    {
        float oy = en->pos.y + en->height;
        revive::sand_clamp(*rr, en->ai, oy, en->hp, en->vert.grounded, w.in_4005);
        en->pos.y = oy - en->height;
    }
    revive::exp_block(*rr, *en, w.chimes);                         // order-free within the tick
    if (ai_debug && (tick_count % 60) == 0)
        std::printf("[revive] #%d cast %d hp %.0f phase %d 7a0 %d gnd %d t %.3f y %.2f floor %.2f surf %d/%d water %d %.2f dead %u cd %d\n",
                    en->instance_id, en->cast_id, en->hp, en->ai.phase(), en->ai.i(0x7a0), int(en->vert.grounded),
                    en->vert.t, en->pos.y, en->floor_y, en->ai.surf, en->ai.surf_ray, int(en->ai.in_water),
                    en->ai.water_y, en->ai.dead_t, en->ai.i(0x868));
    if (en->alive()) return false;
    const revive::DeadPre pre = revive::dead_pre(*rr, *en, w);
    if (pre.first) e.avm.set_section(5, /*force*/true);           // the damage code's death section
    if (pre.remove) {
        world.request_destroy(*en);
        en->visible = false;
        return true;
    }
    e.spec.tick();
    e.avm.tick();                                                  // FUN_004472e0 on the corpse
    e.spec_event();
    const Enemy* er = etab.get(en->cast_id);
    const revive::DeadPost post = revive::dead_post(*rr, *en, er ? er->ai_raw : nullptr);
    if (post.revived) {
        en->instance_id = revive::new_instance_id(en->cast_id, [&](int id) {
            for (const Actor& o : world.actors()) if (o.instance_id == id) return true;
            return false; });
        e.avm.set_section(10);                                     // FUN_00446550(10, 0): gated
        e.facing = en->ai.heading;
        en->visible = true;
        if (ai_debug) std::printf("[ai] t%llu cast %d revived as #%d (section %d)\n",
                                  (unsigned long long)tick_count, en->cast_id, en->instance_id, e.avm.section());
    }
    return true;
};
// M360 Jenocres' dead path in the view (M360.md §2 #1/#3/#4, after brains::m360_dead advanced +0x3fc):
// while the player lives, P+0x734 = 9999 every dead tick (0x4d3f8e); the death section 5 plays (its
// GenerateEffect 87 / 86 loops, counted in g_flags[178]) under the 255-tick linear fade; FUN_004780e0
// removes it on the call after +0x3fc reaches 0x100. The section's SEs were played at the kill
// (enemy_death_sounds), so its sound events are not replayed here.
auto boss_corpse = [&](EnemyRender& e) {
    Actor* en = e.actor;
    if (!en || en->ai_index != 29 || en->alive() || en->destroy_requested() || !e.avm.ok()) return;
    if (player.hp < 1) return;                                     // the 1.1-HP comeback (m360_dead)
    player_iframes = 9999;
    if (en->ai.dead_t >= 0x100u) { world.request_destroy(*en); en->visible = false; return; }
    if (en->ai.dead_t == 1u) {
        e.avm.set_section(5, /*force*/true);
        std::printf("  [boss] t%llu M360 death section %d, the 255-tick fade\n", (unsigned long long)tick_count, e.avm.section());
    }
    e.spec.tick();
    e.avm.tick();
    e.spec_event();
    if (const AsoVm::DebrisEvent* dv = e.avm.debris())             // anime_GenerateDebris (ASO_NOOPS.md §3)
        spawn_debris(en->cast_id, {en->pos.x, en->pos.y + en->height, en->pos.z}, *dv);
    for (const AsoVm::EffectEvent& fe : e.avm.effects()) {
        const Vec3 org{en->pos.x, en->pos.y + en->height, en->pos.z};
        ef2sim::SpawnParams sp;
        sp.power = fe.power; sp.knock = fe.knock; sp.rtype = fe.rtype; sp.status = fe.status;
        sp.atk = en->atk; sp.side = 1; sp.owner = en->instance_id;
        spawn_generate_effect(fe, org, e.facing, sp, en->instance_id);
    }
};
// Drawn while it lies there: a family corpse that has not been removed (view_world_draw.inl).
auto revive_corpse = [&](const Actor& a) {
    return !a.alive() && !a.destroy_requested() && revive::row(a.ai_index) != nullptr;
};
(void)revive_corpse;
