// The GoMesh "boss04" the room script Mesh_Locate's (S_4099, Khonsclard) is driven by the Boss04 brain
// (src/cleria/boss04.hpp, build/boss_re/KHONSCLARD.md) every live step, as boss01..03. Its six pods are
// separate GoMeshes `boss04s` the ctor creates: here they are live-located scene objects "boss04s_0".."5"
// [H the names: the engine keeps them by pointer]. Included after boss3_tick; boss4_tick runs in
// step_meshes, view_boss04_hit.inl at the player's swing.
// [not modelled] the pit's `40jigoku005` UV scroll and its drag of GoXx type-4 objects; the pull on actors
// other than the player; the zoom blur; the two world-anchored mini gauges (0x76c1cc / 0x76c1e4); the dust
// sprites (FUN_00584550); the buff clear, the achievement and the drop (obj+0xc4); the boss-rush i-frames.
constexpr int kB4Owner = -0xB04;                           // fx owner tag for the heal aura (blk+0x350)
std::array<int, boss04::kNodes> boss4_node{};              // node table -> the model's node index
std::array<int, boss04::kClips> boss4_sect{};              // clip id -> the mesh's section index
std::array<RehitBank, boss04::kParts> boss4_banks{};       // the parts' own re-hit banks
auto boss4_pod_name = [](int n) { return std::string("boss04s_") + char('0' + n); };
auto boss4_apply = [&](const Boss04Output& out) {
    for (int id : out.rumble) pad_rumble(id);
    for (int c : out.clips)
        if (c >= 0 && c < boss04::kClips && boss4_sect[std::size_t(c)] >= 0)
            meshes.set_clip_index("boss04", boss4_sect[std::size_t(c)], 0);
    for (const Boss04Sound& sd : out.sounds) audio.play(sd.id, kSfxAnimVol);   // 2-D cues
    if (out.kill_aura)
        for (auto& f : fxsim.effects()) if (f->owner == kB4Owner && f->tag == 1) f->kill = true;
    for (const Boss04Effect& fe : out.effects) {
        const Ef2* ef = ef2_of(fe.id);
        if (!ef) continue;
        ef2sim::SpawnParams sp;
        sp.pos = fe.pos; sp.scale = fe.scale;
        sp.power = fe.power; sp.knock = fe.knock; sp.rtype = fe.rtype;
        sp.atk = fe.boss_stat ? boss4->str() : 0.0f;
        ef2sim::Effect* fx = fxsim.spawn(fe.id, *ef, sp);
        if (fx && fe.tag == 1) { fx->owner = kB4Owner; fx->tag = 1; }
    }
    for (const Boss04Bullet& bb : out.bullets) {               // FUN_00593550 with the boss as owner
        bullet::Spawn s = bullet::block_spawn(bb.kind, bb.effect, bb.pos, bb.dir, bb.f);
        s.owner = 0; s.owner_monster = true; s.owner_atk = boss4->str();
        bullets.spawn(s);
    }
    for (const Boss04PodMove& pm : out.pod_moves) {
        const std::string nm = boss4_pod_name(pm.pod);
        MeshObj* m = meshes.find(nm);
        if (!m) continue;
        if (pm.teleport) { m->pos = *pm.teleport; m->dest = m->pos; m->speed = 0; m->dirty_pos = true; }
        if (pm.move_y) meshes.move_to(nm, {m->pos.x, *pm.move_y, m->pos.z}, 0.02f, 0.02f, false);   // Mesh_MoveTo
    }
    for (const Boss04PodClip& pc : out.pod_clips) meshes.set_clip_now(boss4_pod_name(pc.pod), pc.clip, 0);
    if (out.shake) { g_rig.set_shake_frames(out.shake->first); g_rig.set_shake_amp(out.shake->second); }
    for (const auto& [fi, fv] : out.flags) {
        if (fi >= 529 && fi <= 532) { ail.t[fi - 529] = fv; continue; }   // the status timers 0x76c160..0x76c16c
        if (fi < 0 || fi >= 0x200) continue;
        if (game.g_flags[fi] != fv) std::printf("  [boss04] t%llu flag %d = %d\n", (unsigned long long)tick_count, fi, fv);
        game.g_flags[fi] = fv;
    }
    if (out.player_push && player.hp >= 1) csHost.pforce = {out.player_push->x, out.player_push->z, 4};   // +0x5dc = 4
    if (out.exp) {
        std::printf("  [boss04] t%llu EXP %.0f x %.2f (lv %d)\n", (unsigned long long)tick_count, out.exp->b0,
                    out.exp->mult, out.exp->level);
        award_exp(out.exp->b0, out.exp->level, /*boss*/true, false, 3);
    }
    if (out.gauge) { hud.set_boss_gauge(-8, out.gauge->hp, out.gauge->max, out.gauge->len_scale, out.gauge->emblem); gauge_owner = 0; }
    if (out.gauge_hide) hud.hide_boss_gauge();
    if (out.destroy) {
        if (MeshObj* bm = meshes.find("boss04")) { bm->alpha = bm->alpha_target = 0.0f; bm->no_collide = true; bm->dirty_alpha = true; }
        std::printf("  [boss04] t%llu destroyed\n", (unsigned long long)tick_count);
    }
};
auto boss4_tick = [&] {
    MeshObj* bm = meshes.find("boss04");
    if (!bm) { boss4.reset(); return; }
    Model* mdl = cached_big_model(resolve_ymo_path(asset_root, "data\\enemy\\boss04\\boss04.ymo"));
    if (!mdl) return;
    auto lw = [](std::string t) { for (char& ch : t) ch = char(std::tolower((unsigned char)ch)); return t; };
    if (boss4 && boss_load[3] != mesh_load_serial) boss4.reset();   // a scene load: built again (view_game.inl boss_load)
    if (!boss4) {
        boss_load[3] = mesh_load_serial;
        Boss04Setup su;                                    // EEX record 3 (§A.2.1)
        if (eex.bosses.size() > 3) {
            const BossStats& bs = eex.bosses[3];
            su.eex_hp = bs.hp; su.eex_str = bs.atk; su.eex_def = bs.def; su.level = bs.level;
        }
        if (ginfo.ok()) su.exp_b0 = ginfo.curves[kEnemyExp].at(su.level);   // [H] as boss01..03
        su.diff_index = game.g_flags[144];
        su.diff_easy = game.g_flags[143] != 0;
        su.diff_very_easy = game.g_flags[203] != 0;
        su.boss_rush = game.g_flags[193] != 0;
        su.retries = game.g_flags[196];
        su.pos = bm->pos;
        su.model_nodes = int(mdl->nodes.size());
        boss4 = std::make_unique<Boss04>(su);
        boss4_banks.fill(RehitBank{});
        for (int k = 0; k < boss04::kNodes; ++k) {
            boss4_node[std::size_t(k)] = -1;
            const std::string want = boss04::node_name(k);
            for (std::size_t n = 0; n < mdl->nodes.size(); ++n)
                if (lw(mdl->nodes[n].name) == want) { boss4_node[std::size_t(k)] = int(n); break; }
            if (boss4_node[std::size_t(k)] < 0) std::printf("  [boss04] no node %s\n", want.c_str());
        }
        for (int c = 0; c < boss04::kClips; ++c) {
            boss4_sect[std::size_t(c)] = -1;
            for (std::size_t s = 0; s < bm->clips.size(); ++s)
                if (lw(bm->clips[s].name) == lw(boss04::kClipTable[c].name)) { boss4_sect[std::size_t(c)] = int(s); break; }
            if (boss4_sect[std::size_t(c)] != c)
                std::printf("  [boss04] clip %s -> section %d\n", boss04::kClipTable[c].name, boss4_sect[std::size_t(c)]);
        }
        for (int n = 0; n < boss04::kPods; ++n)            // the ctor's six boss04s, 999 under the boss
            if (!meshes.find(boss4_pod_name(n))) {
                ObjLocateArgs a;
                a.ymo = "data\\enemy\\boss04\\boss04s.ymo"; a.name = boss4_pod_name(n);
                a.x = bm->pos.x; a.y = bm->pos.y - 999.0f; a.z = bm->pos.z;
                live_obj.located.push_back(a);
                live_obj_dirty = true;
            }
        std::printf("  [boss04] Khonsclard: body %.0f, pods %.0f, STR %.0f lv %d, %zu model nodes\n", boss4->hp(),
                    boss4->part(1).max_hp, boss4->str(), boss4->level(), mdl->nodes.size());
        // The ctor's SetClip(STAND) ran at Mesh_Locate, BEFORE the root script's Mesh_SetMotion(START_WAIT): this
        // host builds the brain on its first step, after the script, so the ctor's clip is not re-applied.
        Boss04Output co = boss4->ctor_requests();
        co.clips.clear();
        boss4_apply(co);
    }
    for (RehitBank& b : boss4_banks) b.tick();
    int clip = boss04::kStand;
    for (int c = 0; c < boss04::kClips; ++c) if (boss4_sect[std::size_t(c)] == bm->sect) { clip = c; break; }
    bool posed = false;
    if (bm->sect >= 0 && std::size_t(bm->sect) < bm->clips.size())
        for (const AnimClip& c : mdl->clips)
            if (c.name == bm->clips[std::size_t(bm->sect)].name) { mdl->pose(c, bm->time); posed = true; break; }
    if (!posed) mdl->rest_pose();
    Mat4 place;                                            // the GoMesh placement: no code turns the body
    place.m[12] = bm->pos.x; place.m[13] = bm->pos.y; place.m[14] = bm->pos.z;
    Boss04Input in;
    for (int k = 0; k < boss04::kNodes; ++k) {
        const int ni = boss4_node[std::size_t(k)];
        const Mat4 w = (ni >= 0 && std::size_t(ni) < mdl->nodes.size()) ? mdl->nodes[std::size_t(ni)].world * place : place;
        in.nodes[std::size_t(k)] = {w.m[12], w.m[13], w.m[14]};
    }
    in.model_nodes.reserve(mdl->nodes.size());
    for (const auto& nd : mdl->nodes) { const Mat4 w = nd.world * place; in.model_nodes.push_back({w.m[12], w.m[13], w.m[14]}); }
    for (int n = 0; n < boss04::kPods; ++n) {
        const MeshObj* pm = meshes.find(boss4_pod_name(n));
        in.pods[std::size_t(n)] = pm ? pm->pos : Vec3{bm->pos.x, bm->pos.y - 999.0f, bm->pos.z};
        in.pod_damage_done[std::size_t(n)] = pm && pm->sect >= 0 && std::size_t(pm->sect) < pm->clips.size() &&
                                             lw(pm->clips[std::size_t(pm->sect)].name) == "damage" && pm->count != 0;
    }
    in.player_pos = {feet.x, feet.y + kOriginH, feet.z};
    in.player_hp = float(player.hp);
    in.player_grounded = grounded;
    in.player_floor = last_floor;
    in.fight = game.g_flags[156] != 0;
    in.clip = clip;
    in.clip_done = bm->count != 0;
    in.clip_frame = bm->time;
    in.rng = [&] { return pick_rng.next(); };
    const Boss04Output out = boss4->tick(in);
    boss4_apply(out);
    if (bm->alpha != boss4->alpha()) { bm->alpha = bm->alpha_target = boss4->alpha(); bm->dirty_alpha = true; }   // obj+0x418 (the death fade)
    static const bool boss_log = std::getenv("CLERIA_BOSSLOG") != nullptr;
    if (boss_log && tick_count % 30 == 0)
        std::printf("  [boss04] t%llu clip %s fr %.1f atk %d prev %d shots %d down %d stagger %d band %d | body %.0f/%.0f"
                    " pods %.0f/%.0f states %d%d%d%d%d%d | s %.4f f156 %d feet (%.2f,%.2f,%.2f) php %d\n",
                    (unsigned long long)tick_count, boss04::kClipTable[clip].name, bm->time, boss4->attack(),
                    boss4->prev_attack(), boss4->shots(), boss4->down(), boss4->stagger(), boss4->band(), boss4->hp(),
                    boss4->max_hp(), boss4->part(1).hp, boss4->part(2).hp, boss4->pod(0).state, boss4->pod(1).state,
                    boss4->pod(2).state, boss4->pod(3).state, boss4->pod(4).state, boss4->pod(5).state,
                    boss4->suction(), game.g_flags[156], feet.x, feet.y, feet.z, player.hp);
};
// drive `boss4[=i]`: stand 1.5 off part i (default: the first hittable of the core / the pods / part 7) and face it.
boss4_drive = [&](const std::string& cmd) {
    if (cmd.rfind("boss4", 0) != 0 || !boss4) return false;
    int i = cmd.size() > 6 && cmd[5] == '=' ? std::atoi(cmd.c_str() + 6) : -1;
    if (i < 0) {
        if (boss4->part(0).def < 999.0f) i = 0;
        else for (int k = 1; k <= 6 && i < 0; ++k) if (boss4->part(k).def < 999.0f && boss4->part(k).hp >= 1) i = k;
        if (i < 0) i = 7;
    }
    const Boss04Part& bp = boss4->part(i);
    const Vec3 c = bp.centre;
    float dx = feet.x - c.x, dz = feet.z - c.z;
    if (i >= 1 && i <= 6) { dx = boss4->pos().x - c.x; dz = boss4->pos().z - c.z; }   // a pod: from the arena side
    const float l = std::sqrt(dx * dx + dz * dz);
    if (l > 1e-3f) { dx /= l; dz /= l; } else { dx = 0; dz = -1; }
    const float reach = bp.hurt_radius + (i >= 1 && i <= 6 ? 0.3f : 1.0f);   // a pod: close (the pit pulls)
    const float x = c.x + dx * reach, z = c.z + dz * reach;
    const float fl = floor_at(phys_mesh(room), x, z, c.y + 3.0f);
    feet = {x, fl > kNoFloor ? fl : feet.y, z};
    face_dir = {-dx, 0.0f, -dz};
    facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
    if (i == 0) drive_jump = true;                     // the open core sits ~2.5 above the floor: jump at it
    std::printf("  [drive] boss4 -> part %d (%.2f,%.2f,%.2f) DEF %.0f hp %.0f body %.0f/%.0f feet (%.2f,%.2f,%.2f)\n", i, c.x,
                c.y, c.z, bp.def, bp.hp, boss4->hp(), boss4->max_hp(), feet.x, feet.y, feet.z);
    return true;
};
