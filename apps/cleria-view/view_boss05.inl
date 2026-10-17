// The GoMesh "boss05" the room script Mesh_Locate's (S_5099, Pictimos) is driven by the Boss05 brain
// (src/cleria/boss05.hpp, build/boss_re/PICTIMOS.md) every live step, as boss01..04. Its summons are ordinary
// cast-0x1a actors (CAST_M220 Chryolos, boss-fight brain m220_boss): the host copies their fields into
// Boss05Minion records (pool order) and back. Included after view_boss04.inl; boss5_tick runs in step_meshes,
// view_boss05_hit.inl at the player's swing, boss5_target in the enemy env (the +0x888 target override).
// The scene light is the module's (Boss05Output::light_dir / light_col), the camera's slot 3 its preset
// (g_boss05_cam_hook). [not modelled] the hook's distance fade of the effects 0xd3, the particle debris, the mesh
// tint / glare,
// the parts' contact hits.
g_boss05_cam_hook = [&]() -> bool {
    if (game.g_flags[156] == 0 || !boss5) return false;
    const int k = g_rig.current();
    if (k == 0) return true;                               // claimed, nothing written
    if (k != 3) return false;
    const Boss05Camera& c = boss5->camera();
    CameraRegion& s = g_rig.edit_slot(3);
    s.eye.fixed_pos = c.eye; s.target.fixed_pos = c.target;
    s.eye.dist = c.dist; s.eye.pitch = c.pitch; s.fov = c.fov;
    s.eye.mode = 0; s.target.mode = 0;
    return true;
};
struct Boss5HookGuard { ~Boss5HookGuard() { g_boss05_cam_hook = nullptr; } } boss5_hook_guard;   // the hook captures this scope
constexpr int kB5Owner = -0xB05;                           // fx owner tag for the booster auras / heals (handles)
std::array<int, boss05::kSlots> boss5_slot{};              // slot -> the model's node index
std::array<int, boss05::kSections> boss5_sect{};           // section -> the mesh's clip index
std::array<ef2sim::M4, boss05::kSlots> boss5_slot_m{};     // the posed slot frames (attach parents)
std::array<RehitBank, boss05::kParts> boss5_banks{};
std::unordered_map<int, std::uint32_t> boss5_dead_t;       // a summon's dead ticks (+0x3fc) by instance id
auto boss5_apply = [&](const Boss05Output& out, MeshObj* bm) {
    if (out.light_dir) csHost.light_dir = *out.light_dir;   // 0x76a564..
    bm->frozen = boss5->clip_frozen();                     // obj+0x28f: the next step's clip advance is skipped
    if (out.light_col) csHost.light_rgb = *out.light_col;   // 0x76a570..
    for (int id : out.rumble) pad_rumble(id);
    for (int s : out.sections)
        if (s >= 0 && s < boss05::kSections && boss5_sect[std::size_t(s)] >= 0)
            meshes.set_clip_index("boss05", boss5_sect[std::size_t(s)], 0);
    if (out.anim_speed) bm->time_scale = *out.anim_speed;   // A+0x24
    for (int id : out.sounds) sfx_at(id, boss5->pos(), kSfxAnimVol);
    for (int h : out.kill_handles)
        for (auto& f : fxsim.effects()) if (f->owner == kB5Owner && f->tag == h) f->kill = true;
    if (out.kill_376) for (auto& f : fxsim.effects()) if (f->id == 376) f->kill = true;
    for (const Boss05Effect& fe : out.effects) {
        const Ef2* ef = ef2_of(fe.id);
        if (!ef) continue;
        ef2sim::SpawnParams sp;
        sp.pos = fe.pos; sp.rot = fe.rot; sp.scale = fe.scale;
        sp.power = fe.power; sp.knock = fe.knock; sp.rtype = fe.rtype;
        sp.atk = fe.boss_stat ? boss5->str() : 0.0f;
        if (fe.slot >= 0 && fe.slot < boss05::kSlots) {     // FUN_00451c20 mode 1: offset `pos` on the node
            const ef2sim::M4& nm = boss5_slot_m[std::size_t(fe.slot)];
            sp.pos = {nm.m[3][0], nm.m[3][1], nm.m[3][2]};
        }
        ef2sim::Effect* fx = fxsim.spawn(fe.id, *ef, sp);
        if (!fx) continue;
        if (fe.slot >= 0 && fe.slot < boss05::kSlots) {
            fx->local = fx->M;                                // [approx] the node's translation + the offset
            fx->local.m[3][0] = fe.pos.x; fx->local.m[3][1] = fe.pos.y; fx->local.m[3][2] = fe.pos.z;
            fx->attach_parent = &boss5_slot_m[std::size_t(fe.slot)];
            fx->attach_mode = 2;
        }
        if (fe.handle) { fx->owner = kB5Owner; fx->tag = fe.handle; }
    }
    for (const Boss05Bullet& bb : out.bullets) {
        bullet::Spawn s = bullet::block_spawn(bb.kind, bb.effect, bb.pos, bb.dir, bb.f);
        s.owner = 0; s.owner_monster = true; s.owner_atk = boss5->str();
        bullets.spawn(s);
    }
    for (const Boss05Spawned& sp : out.spawned)              // the throw (ch1) on the new summon
        if (Actor* a = world.find(sp.id)) a->mover.k1 = sp.ch1;
    if (out.shake) { g_rig.set_shake_frames(out.shake->first); g_rig.set_shake_amp(out.shake->second); }
    if (out.zoom) zoom_blur(out.zoom->first, out.zoom->second);   // ZoomBlur (0x76a550 / 0x76a554)
    for (const auto& [fi, fv] : out.flags) {
        if (set_high_flag(fi, fv)) continue;               // 544..546 lie past g_flags
        if (fi < 0 || fi >= 0x200) continue;
        if (game.g_flags[fi] != fv) std::printf("  [boss05] t%llu flag %d = %d\n", (unsigned long long)tick_count, fi, fv);
        game.g_flags[fi] = fv;
    }
    feet.x += out.player_move.x; feet.z += out.player_move.z;
    player_kb.x += out.player_kb.x; player_kb.z += out.player_kb.z;
    if (out.player_iframes) player_iframes = std::max(player_iframes, int(*out.player_iframes));
    if (out.exp) {
        std::printf("  [boss05] t%llu EXP (lv %d)\n", (unsigned long long)tick_count, boss5->level());
        if (ginfo.ok()) award_exp(ginfo.curves[kEnemyExp].at(boss5->level()), boss5->level(), /*boss*/true, false, 4);   // [H] b0 as boss01..04
    }
    if (out.gauge) { hud.set_boss_gauge(-9, out.gauge->first, out.gauge->second, 7000.0f, 5); gauge_owner = 0; }
    if (out.gauge_hide) hud.hide_boss_gauge();
    if (out.alpha) meshes.alpha_target("boss05", *out.alpha, 4);   // FUN_00504a40: alpha 0 over 4
    if (out.destroy) {
        bm->alpha = bm->alpha_target = 0.0f; bm->no_collide = true; bm->dirty_alpha = true;
        std::printf("  [boss05] t%llu destroyed\n", (unsigned long long)tick_count);
    }
};
auto boss5_tick = [&] {
    MeshObj* bm = meshes.find("boss05");
    if (!bm) { boss5.reset(); boss5_dead_t.clear(); return; }
    Model* mdl = cached_big_model(resolve_ymo_path(asset_root, "data\\enemy\\boss05\\boss05.ymo"));
    if (!mdl) return;
    auto lw = [](std::string t) { for (char& ch : t) ch = char(std::tolower((unsigned char)ch)); return t; };
    if (boss5 && boss_load[4] != mesh_load_serial) { boss5.reset(); boss5_dead_t.clear(); }   // a scene load (boss_load)
    if (!boss5) {
        boss_load[4] = mesh_load_serial;
        Boss05Setup su;                                    // EEX record 4
        su.light_dir = csHost.light_dir; su.light_col = csHost.light_rgb;
        if (eex.bosses.size() > 4) {
            const BossStats& bs = eex.bosses[4];
            su.eex_hp = bs.hp; su.eex_str = bs.atk; su.eex_def = bs.def; su.level = bs.level;
        }
        su.character = character;
        su.diff_index = game.g_flags[144];
        su.diff_easy = game.g_flags[143] != 0;
        su.diff_very_easy = game.g_flags[203] != 0;
        su.boss_rush = game.g_flags[193] != 0;
        su.retries = game.g_flags[196];
        su.pos = bm->pos;
        boss5 = std::make_unique<Boss05>(su);
        boss5_banks.fill(RehitBank{});
        boss5_dead_t.clear();
        for (int k = 0; k < boss05::kSlots; ++k) {
            boss5_slot[std::size_t(k)] = -1;
            for (std::size_t n = 0; n < mdl->nodes.size(); ++n)
                if (lw(mdl->nodes[n].name) == lw(boss05::kSlotNames[k])) { boss5_slot[std::size_t(k)] = int(n); break; }
            if (boss5_slot[std::size_t(k)] < 0) std::printf("  [boss05] no node %s\n", boss05::kSlotNames[k]);
        }
        for (int c = 0; c < boss05::kSections; ++c) {
            boss5_sect[std::size_t(c)] = -1;
            for (std::size_t s = 0; s < bm->clips.size(); ++s)
                if (lw(bm->clips[s].name) == lw(boss05::kSectionTable[c].name)) { boss5_sect[std::size_t(c)] = int(s); break; }
        }
        std::printf("  [boss05] Pictimos: body %.0f, STR %.0f lv %d, %zu model nodes, at (%.1f,%.1f,%.1f)\n", boss5->hp(),
                    boss5->str(), boss5->level(), mdl->nodes.size(), boss5->pos().x, boss5->pos().y, boss5->pos().z);
    } else if (bm->pos.x != boss5->pos().x || bm->pos.y != boss5->pos().y || bm->pos.z != boss5->pos().z) {
        boss5->set_pos(bm->pos);                           // a script moved it (the intro's Mesh_Fall)
    }
    for (RehitBank& b : boss5_banks) b.tick();
    int sect = boss05::kStand;
    for (int c = 0; c < boss05::kSections; ++c) if (boss5_sect[std::size_t(c)] == bm->sect) { sect = c; break; }
    bool posed = false;
    if (bm->sect >= 0 && std::size_t(bm->sect) < bm->clips.size())
        for (const AnimClip& c : mdl->clips)
            if (c.name == bm->clips[std::size_t(bm->sect)].name) { mdl->pose(c, bm->time); posed = true; break; }
    if (!posed) mdl->rest_pose();
    const Mat4 place = boss5->matrix();
    Boss05Input in;
    for (int k = 0; k < boss05::kSlots; ++k) {
        const int ni = boss5_slot[std::size_t(k)];
        const Mat4 w = (ni >= 0 && std::size_t(ni) < mdl->nodes.size()) ? mdl->nodes[std::size_t(ni)].world * place : place;
        in.slots[std::size_t(k)] = {w.m[12], w.m[13], w.m[14]};
        boss5_slot_m[std::size_t(k)] = to_m4(w);
    }
    in.nodes.reserve(mdl->nodes.size());
    for (const auto& nd : mdl->nodes) { const Mat4 w = nd.world * place; in.nodes.push_back({w.m[12], w.m[13], w.m[14]}); }
    std::vector<Boss05Minion> mins;
    {
        int i = 0;
        for (Actor& a : world.actors()) {
            in.pool_ids.push_back(a.instance_id);
            if (a.cast_id == 0x1a && !a.destroy_requested()) {
                Boss05Minion m;
                m.id = a.instance_id; m.pool = i;
                m.hp = a.hp; m.max_hp = a.max_hp; m.def = a.def; m.alpha = a.ai.alpha;
                m.pos = {a.pos.x, a.pos.y + a.height, a.pos.z}; m.ch1 = a.mover.k1;
                std::uint32_t& dt = boss5_dead_t[a.instance_id];
                dt = a.alive() ? 0u : dt + 1u;
                m.dead_t = dt;
                // The handler's death burst (FUN_004ac470 0x4ac4f0..0x4ac6c0): on the first dead tick of a fight
                // (g_flags[156]) with +0x130 & 0x2000, SE 11 and EF2 0x43 at the origin, rot (0, -pi/2, 0), scale 1.
                if (dt == 1u && game.g_flags[156] != 0 && (a.flags130 & 0x2000u)) {
                    sfx_at(11, a.pos, kSfxAnimVol);
                    spawn_sim_fx(0x43, m.pos, {0.0f, -1.5707964f, 0.0f}, 1.0f, {}, 0, {});
                    std::printf("  [boss05] t%llu summon #%d death burst (SE 11, effect 0x43)\n",
                                (unsigned long long)tick_count, a.instance_id);
                }
                m.sub = a.ai.i(0x728); m.mode = a.ai.i(0x79c); m.target = a.ai.i(0x888);
                mins.push_back(m);
            }
            ++i;
        }
    }
    in.fight = game.g_flags[156] != 0;
    in.flag224 = game.g_flags[224] != 0;
    in.flag291 = game.g_flags[291] != 0;
    in.fix_held = csMgr.world_frozen();
    in.light_dir = csHost.light_dir; in.light_col = csHost.light_rgb;
    in.player_pos = {feet.x, feet.y + kOriginH, feet.z};
    in.player_hp = float(player.hp);
    in.section = sect;
    in.count = bm->count;
    in.rng = [&] { return pick_rng.next(); };
    in.spawn = [&](const Vec3& p, int deg, int lvl, float* def) -> int {   // FUN_00475710(cast 0x1a, deg, pos, level)
        SpawnRequest req;
        req.cast_type = 0x1a; req.pos = p; req.yaw_degrees = float(deg);
        Actor* a = world.spawn(req);
        if (!a) return 0;
        a->height = cast_motion(casts, 0x1a).height();
        a->radius = cast_motion(casts, 0x1a).radius();
        a->pos.y = p.y - a->height;                        // the request is the origin (+0x50)
        if (const Enemy* er = etab.get(0x1a); er && ginfo.ok() && lvl > 0) {   // FUN_004202e0's level override
            Enemy lv = *er;
            lv.level = lvl;
            const EnemyStats st = enemy_stats(ginfo, lv);
            a->max_hp = a->hp = st.max_hp; a->atk = st.atk; a->def = st.def;
        }
        a->level = lvl;
        a->exp_reward = 0.0f;                              // +0xb0 = 0
        a->ai.i(0x888) = -2;                               // +0x888 = Pictimos
        a->resist[0] = 0.7f; a->resist[1] = 0.5f; a->resist[2] = 0.5f;
        const float fl = floor_at(phys_mesh(room), p.x, p.z, p.y + 1.0f);
        if (fl > kNoFloor) a->floor_y = fl;
        actor_mats[a->instance_id] = ef2sim::translation(p.x, p.y, p.z);
        csHost.world_changed = true;
        if (def) *def = a->def;
        return a->instance_id;
    };
    const Boss05Output out = boss5->tick(in, mins);
    for (const Boss05Minion& m : mins)                     // the module's writes, back onto the actors
        if (Actor* a = world.find(m.id)) {
            a->hp = m.hp; a->def = m.def; a->ai.alpha = m.alpha;
            a->pos = {m.pos.x, m.pos.y - a->height, m.pos.z}; a->mover.k1 = m.ch1;
            a->ai.i(0x728) = m.sub; a->ai.i(0x79c) = m.mode; a->ai.i(0x888) = m.target;
            if (m.vt) a->vert.t = *m.vt;
            if (m.turn) a->ai.f(0x814) = *m.turn;
            a->flags130 |= m.or130;
        }
    boss5_apply(out, bm);
    const Mat4 wm = boss5->matrix();
    if (!bm->has_world || bm->world.m != wm.m) bm->dirty_pose = true;
    bm->has_world = true;
    bm->world = wm;
    bm->pos = boss5->pos();
    static const bool boss_log = std::getenv("CLERIA_BOSSLOG") != nullptr;
    if (boss_log && tick_count % 30 == 0)
        std::printf("  [boss05] t%llu state %d sect %s b4 %d intro %d booster %d meter %d | body %.0f/%.0f guts %d waves %d"
                    " summons %d/%zu | pos (%.1f,%.1f,%.1f) yaw %.2f f156 %d feet (%.2f,%.2f,%.2f) php %d\n",
                    (unsigned long long)tick_count, boss5->state(), boss05::kSectionTable[sect].name, boss5->frames(),
                    boss5->intro(), boss5->booster(), boss5->meter(), boss5->hp(), boss5->max_hp(), boss5->guts(),
                    boss5->waves(), int(std::count_if(mins.begin(), mins.end(), [](const Boss05Minion& m) { return m.hp >= 1.0f; })),
                    mins.size(), boss5->pos().x, boss5->pos().y, boss5->pos().z, boss5->yaw(),
                    game.g_flags[156], feet.x, feet.y, feet.z, player.hp);
};
// The +0x888 target override for the enemy env (the action player's T): -2 = Pictimos, > 0 = an actor.
auto boss5_target = [&](const Actor& en, AiEnv& env) {
    const int t = en.ai.i(0x888);
    if (t == 0) return;
    if (t == -2) { if (boss5) env.target = boss5->pos(); return; }
    if (const Actor* o = world.find(t)) { env.target = {o->pos.x, o->pos.y + o->height, o->pos.z}; env.target_alive = o->alive(); }
};
// drive `boss5[=i]`: stand off part i (default the lower hittable of the two hurtable parts 0 / 1) and face it.
boss5_drive = [&](const std::string& cmd) {
    if (cmd.rfind("boss5", 0) != 0 || !boss5) return false;
    if (boss5->destroyed() || boss5->hp() < 1.0f) return true;   // dying / gone: nothing to stand by
    int i = cmd.size() > 6 && cmd[5] == '=' ? std::atoi(cmd.c_str() + 6) : -1;
    if (i < 0) {
        const bool h0 = boss5->part_hittable(0), h1 = boss5->part_hittable(1);
        i = h0 && h1 ? (boss5->part(1).centre.y < boss5->part(0).centre.y ? 1 : 0) : (h0 ? 0 : 1);
    }
    const Boss05Part& bp = boss5->part(i);
    const Vec3 c = bp.centre;
    float dx = feet.x - c.x, dz = feet.z - c.z;
    const float l = std::sqrt(dx * dx + dz * dz);
    if (l > 1e-3f) { dx /= l; dz /= l; } else { dx = 0; dz = -1; }
    const float reach = bp.radius + 1.0f;
    const float x = c.x + dx * reach, z = c.z + dz * reach;
    const float fl = floor_at(phys_mesh(room), x, z, c.y + 3.0f);
    feet = {x, fl > kNoFloor ? fl : feet.y, z};
    face_dir = {-dx, 0.0f, -dz};
    facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
    if (c.y - feet.y > 2.5f) drive_jump = true;
    std::printf("  [drive] boss5 -> part %d (%.2f,%.2f,%.2f) hp %.0f body %.0f/%.0f feet (%.2f,%.2f,%.2f)\n", i, c.x, c.y,
                c.z, bp.hp, boss5->hp(), boss5->max_hp(), feet.x, feet.y, feet.z);
    return true;
};
