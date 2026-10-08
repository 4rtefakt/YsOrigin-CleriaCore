// Dalles (room S_7080): the GoMesh "boss07" the room root Mesh_Locate's is driven by the Boss07 module
// (src/cleria/boss07.hpp) every live step, as boss01..06. There is no actor in the fight: the module owns the body,
// its three GoXx hurt parts and the three child meshes the ctor builds (the options "boss07opR" / "boss07opL", the
// wing "boss07w"), which this host keeps as live-located scene objects. Specs: build/boss_re/DALLES.md (P1),
// DALLES_AB.md, DALLES_CD1.md, DALLES_D23.md. Included after view_boss06.inl. Per frame: boss7_tick in step_meshes,
// the camera hook in update_camera (view_camera.inl), view_boss07_hit.inl at the player's swing and in the mode-0
// scan of the player's particles (view_fx_mode0.inl), boss7_gauges before the HUD draw. Everything that hurts is an
// EF2 hit particle of side 1 with Dalles' STR (DALLES_CD1 §7, DALLES_D23 §18): the view's ordinary particle path.
// [approx] the ctor runs here on the first step after the room load, not at Mesh_Locate: see boss7_tick.
// [approx] the full-screen fader has one colour in the view: the module's 0x769f34 is taken as it; [0x769f30] = 0 is
// dropped. [approx] SE and SE2 are the same 2-D cue (the "load if needed" path has no counterpart).
// [approx] the mini gauges (0x76c1cc / 0x76c1e4 / 0x76c1fc) are plain two-rect bars at their projected anchors and
// their 255 timers are not counted down (DALLES_AB C11 / G4: the counter is the HUD's, not traced).
// [approx] FUN_00462c40(cl 1) (DALLES_D23 §16.5) is run as "Boost ends"; its buff-timer half is not.
// [not modelled] the re-skins (DALLES_AB §B.6.2 / §B.6.4: Boss07_00 / _01 ver2 / ver3, Boss07op01 — the view has no
// per-object texture swap), ZoomBlur (0x76a550), the glare feedback pulse (the byte is carried, nothing reads it), the
// hit-flash tints (D+0x2a0, child +0x2a0 / +0x2a4; DALLES_CD1 §3.3), the wing's translucent draw flag (+0x294 &
// 0x1000), fx+0x1dc |= 0x100 of the inline effects, the death's dust sprites (FUN_00584550, DALLES_D23 §8), the kill
// record FUN_00403cd0, flags 529..532 / 544..546 (dwords past g_flags), Hugo's P+0x7e0 effect, the "heavy" status 8
// of the homing shot's hits, dmg' of the pool drain (GELALDY G9), the skybox alpha when the room has no object "skybox", the scene light (0xaa).
constexpr int kB7Owner = -0xB07;                           // fx owner tag: the module's effect handles
const char* const boss7_child_name[3] = {"boss07opr", "boss07opl", "boss07w"};
const char* const boss7_node_name[boss07::kNodes] = {"core", "hontai", "hand_r", "hand_l", "nail_r_0", "nail_r_1", "nail_r_2",
                                                     "nail_r_3", "nail_l_0", "nail_l_1", "nail_l_2", "nail_l_3"};
Boss07Statics boss7_statics;                               // 0x7d9b9c.. / 0x746f38: process statics, kept across attempts
std::array<int, boss07::kNodes> boss7_node{};              // node table -> the model's node
std::array<int, boss07::kSections> boss7_sect{};           // section -> the mesh's clip index
std::array<ef2sim::M4, boss07::kNodes> boss7_node_m{};     // attach parents: the body's nodes,
ef2sim::M4 boss7_body_m;                                   // D+0x20,
std::array<ef2sim::M4, 2> boss7_opt_m{};                   // opt+0x20
Model boss7_op_mdl;                                        // BOSS07OP.YMO, posed per option for "core" / "hontai"
bool boss7_op_ok = false;
int boss7_op_core = -1, boss7_op_hontai = -1;
std::array<RehitBank, boss07::kParts> boss7_banks{};       // the parts' own re-hit banks
Vec3 boss7_wpos{};                                         // D+0x50 as last seen on / written to the MeshObj
bool boss7_owned = false;                                  // the module placed the mesh (MeshObj::has_world is ours)
bool boss7_scripted = false, boss7_started = false;        // a live script moved the mesh before the fight; flag 156 was seen
bool boss7_kids = false;                                 // the child meshes were seen in the scene (a scene load drops them)
bool boss7_pin = false;                                    // byte P+0x398 = 1 (the super's pin): no vertical integration
bool boss7_gone = false;                                   // the death's destroy applied
std::function<void()> boss7_pilot;                         // drive boss7: the per-tick player pilot (below)
static const bool boss7_log = std::getenv("CLERIA_BOSSLOG") != nullptr;
auto boss7_m4 = [](const ef2sim::M4& a) {
    Mat4 r;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) r.m[std::size_t(i * 4 + j)] = a.m[i][j];
    return r;
};
auto boss7_veq = [](const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
auto boss7_child_m = [&](int i) {                         // SetRot(child, &rot) = FUN_00503990's rows + the position
    const Boss07ChildMesh& c = boss7->child(i);
    ef2sim::M4 m = ef2sim::rot_ypr(c.rot.y, c.rot.x, c.rot.z);
    m.m[3][0] = c.pos.x; m.m[3][1] = c.pos.y; m.m[3][2] = c.pos.z;
    return m;
};
// The terrain segment with the boss meshes left out (FUN_0041e420's option bit 0 = the ray's byte +0x49: every mesh
// whose type byte +0x28d is 5..0xc is skipped; DALLES_CD1 §4.2 / §6, DALLES_D23 §16.4). In S_7080 that is "boss07"
// alone (the options and the wing are not type 0xb and still block); in S_7099 Darm's four type-0xc meshes (the
// 6-character name test: boss08, boss08t, boss08w, boss08Arm; DARM.md §3.1) — not the floor, not bs08_mgcra.
auto boss7_seg_nb = [&](const Vec3& a, const Vec3& b, Vec3* at) {
    const RoomMesh& cm = phys_mesh(room);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> skip;
    for (const char* nm : {"boss07", "boss08", "boss08t", "boss08w", "boss08arm"})
        if (const RoomMesh::ObjRange* r = cm.object_range(nm)) skip.push_back({r->t0, r->t1});
    if (skip.empty()) { Vec3 n{}; return segment_hit(cm, a, b, at, &n); }
    std::sort(skip.begin(), skip.end());
    skip.push_back({std::uint32_t(cm.indices.size() / 3), 0u});   // the tail run [last t1, nt)
    auto d2 = [&](const Vec3& h) { return (h.x - a.x) * (h.x - a.x) + (h.y - a.y) * (h.y - a.y) + (h.z - a.z) * (h.z - a.z); };
    bool any = false;
    Vec3 best{};
    std::uint32_t t = 0;
    for (const auto& [s0, s1] : skip) {
        Vec3 h{};
        if (s0 > t && segment_hits_tris(cm, a, b, t, s0, &h) && (!any || d2(h) < d2(best))) { best = h; any = true; }
        t = s1;
    }
    if (any && at) *at = best;
    return any;
};
fxsim.set_world_no_boss([&](const Vec3& f, const Vec3& d, Vec3* at) { return boss7_seg_nb(f, {f.x + d.x, f.y + d.y, f.z + d.z}, at); });
// FUN_0044ebb0's id specials (DALLES_D23 §16.1 / §16.2), raised by the EF2 simulation where an impact child would
// spawn. Impact 0x18d (397, the meteor's): SE 316. Impact 0x194 (404, the W attack's): SE 323, then — while the
// GoMesh "boss07" exists and is not in the other dimension (y > 99) — a kind-4 pillar bullet owned by it, from the
// particle toward the player as seen FROM DALLES, speed 0.05..0.10 (one draw), homing level at 1 deg a tick for 600.
fxsim.set_impact_special([&](int id, const Vec3& at) {
    if (id == 0x18d) { audio.play(0x18d - 0x51, kSfxAnimVol); return; }   // FUN_0043a040(316)
    if (id != 0x194) return;
    audio.play(323, kSfxAnimVol);                          // 0x44ecea
    const MeshObj* bm = meshes.find("boss07");
    if (!bm || bm->pos.y > 99.0f) return;                  // 0x6bdaf8
    Vec3 dir{feet.x - bm->pos.x, 0.0f, feet.z - bm->pos.z};
    const float l = std::sqrt(dir.x * dir.x + dir.z * dir.z);
    if (l != 0.0f) { dir.x /= l; dir.z /= l; }
    const std::uint32_t bits = (pick_rng.next() & 0x7fffffu) | 0x3f800000u;
    float u; std::memcpy(&u, &bits, 4);
    float f[13] = {};
    f[0] = (u - 1.0f) * 0.05f + 0.05f;                     // 0x6bd13c
    f[4] = 1.0f;
    const std::uint32_t one = 1u; std::memcpy(&f[7], &one, 4);
    f[8] = 0.0174533f;                                     // 0x3c8efa35
    f[11] = 600.0f;
    bullet::Spawn s = bullet::block_spawn(4, 404, at, dir, f);
    s.owner = 0; s.owner_monster = true;
    s.owner_atk = boss7 ? boss7->body().str : 0.0f;        // his stat block at that moment (the mesh's without a module: 0)
    bullets.spawn(s);                                      // fx+0x201 is not set on it
    if (boss7_log) std::printf("  [boss07] t%llu impact 404 at (%.1f,%.1f,%.1f): a pillar bullet, speed %.3f\n",
                               (unsigned long long)tick_count, at.x, at.y, at.z, f[0]);
});
// FUN_0042bb10's boss07 path (DALLES_CD1 §3.2): once per camera step while flag 156 and the mesh has its block, the
// hook FUN_0042c4f0 rewrites the CURRENT slot from the block; true = claimed (the region pick is skipped; the
// forced slot 0x76a1f8 applies after it either way).
g_boss07_cam_hook = [&]() -> bool {
    if (game.g_flags[156] == 0 || !boss7) return false;
    const int k = g_rig.current();
    if (k < 0 || k >= CamRig::kSlots) return false;
    const CameraRegion& s = g_rig.slot(k);
    Boss07CamRecord rec;
    rec.eye_fixed = s.eye.fixed_pos; rec.target_fixed = s.target.fixed_pos; rec.eye_dist = s.eye.dist; rec.fov = s.fov;
    rec.eye_mode = int(s.eye.mode); rec.target_mode = int(s.target.mode); rec.eye_flags = int(s.eye.flags);
    const bool claimed = boss7->camera_hook(k, rec);
    if (k >= 1 && k <= 5) {
        CameraRegion& w = g_rig.edit_slot(k);
        w.eye.fixed_pos = rec.eye_fixed; w.target.fixed_pos = rec.target_fixed; w.eye.dist = rec.eye_dist; w.fov = rec.fov;
        w.eye.mode = std::uint32_t(rec.eye_mode); w.target.mode = std::uint32_t(rec.target_mode);
        w.eye.flags = std::uint32_t(rec.eye_flags);
    }
    return claimed;
};
struct Boss7HookGuard { ~Boss7HookGuard() { g_boss07_cam_hook = nullptr; } } boss7_hook_guard;   // the hook captures this scope
auto boss7_reset = [&] {
    if (boss7) boss7_statics = boss7->statics();           // no writer but the band: they outlive the object
    boss7.reset();
    if (boss7_pin) { boss7_pin = false; player_hold = false; }
    boss7_owned = false; boss7_gone = false; boss7_kids = false;
    for (auto& mb : hud.mini_bar) mb.frac = -1.0f;
};
auto boss7_apply = [&](const Boss07Output& out, MeshObj* bm) {
    for (int id : out.rumble) pad_rumble(id);
    for (const Boss07Clip& c : out.clips) {                // SetClip: FUN_005f5c80(M, section, 1; xmm3 0)
        if (c.section < 0 || c.section >= boss07::kSections || boss7_sect[std::size_t(c.section)] < 0) continue;
        meshes.set_clip_index("boss07", boss7_sect[std::size_t(c.section)], 0);
        bm->time_scale = 1.0f;
    }
    for (const Boss07Motion& mo : out.motions)             // QueueMotion: +0x424 = name, word +0x443 = 0x100, +0x448 = 0, count 0
        if (mo.mesh >= 0 && mo.mesh < 3) meshes.set_motion(boss7_child_name[mo.mesh], mo.name, /*blend +0x444*/true, 0);
    for (const Boss07Sound& sd : out.sounds) audio.play(sd.id, kSfxAnimVol);   // FUN_0043a040(id, 0x400): 2-D
    for (int h : out.kill_handles)                         // one effect: +0x1d4 = 1
        for (auto& f : fxsim.effects()) if (f->owner == kB7Owner && f->tag == h) f->kill = true;
    for (const Boss07Effect& fe : out.effects) {
        const Ef2* ef = ef2_of(fe.id);
        if (!ef) continue;
        const ef2sim::M4* parent = nullptr;
        switch (fe.parent) {
            case Boss07Parent::kNode: parent = &boss7_node_m[std::size_t(std::clamp(fe.parent_index, 0, boss07::kNodes - 1))]; break;
            case Boss07Parent::kBody: parent = &boss7_body_m; break;
            case Boss07Parent::kOption: parent = &boss7_opt_m[std::size_t(fe.parent_index & 1)]; break;
            case Boss07Parent::kPlayer: parent = &actor_mats[13]; break;   // P+0x20's origin (the mode-2 attaches read no more)
            default: break;
        }
        ef2sim::SpawnParams sp;
        sp.pos = fe.pos; sp.rot = fe.rot; sp.scale = fe.scale;
        sp.power = fe.power; sp.knock = fe.knock; sp.rtype = fe.rtype; sp.status = fe.status; sp.cls = fe.cls;
        sp.side = fe.side;
        sp.atk = fe.stat ? boss7->body().str : 0.0f;       // the stat block &D+0x94 copied at the spawn
        // FUN_00451c20: local = T(off) . RotYPR(rot) . S(scale); mode 1 fx.M = local . parent, mode 2 local + its row 3.
        // The phase-end form (DALLES_AB §B.6.3): local = a uniform scale, the attach written inline.
        const ef2sim::M4 local = fe.local_scale != 0.0f
            ? ef2sim::scaling(fe.local_scale, fe.local_scale, fe.local_scale)
            : ef2sim::mul(ef2sim::mul(ef2sim::translation(fe.at_off.x, fe.at_off.y, fe.at_off.z),
                                      ef2sim::rot_ypr(fe.at_rot.y, fe.at_rot.x, fe.at_rot.z)),
                          ef2sim::scaling(fe.at_scale.x, fe.at_scale.y, fe.at_scale.z));
        if (parent && fe.local_scale == 0.0f) {
            const ef2sim::M4 m0 = ef2sim::mul(local, *parent);
            sp.pos = fe.at_mode == 1 ? Vec3{m0.m[3][0], m0.m[3][1], m0.m[3][2]}
                                     : Vec3{parent->m[3][0] + fe.at_off.x, parent->m[3][1] + fe.at_off.y,
                                            parent->m[3][2] + fe.at_off.z};
        }
        ef2sim::Effect* fx = fxsim.spawn(fe.id, *ef, sp);
        if (!fx) continue;
        if (fe.call == Boss07Call::kFx2) { fx->scale_x = fe.scale.x; fx->scale_y = fe.scale.y; fx->scale_z = fe.scale.z; }   // FUN_00450880 keeps it
        if (parent) { fx->local = local; fx->attach_parent = parent; fx->attach_mode = fe.at_mode == 1 ? 1 : 2; }
        if (fe.flag201) fx->no_boss = true;                // fx+0x201: its world rays skip the boss meshes
        if (fe.handle) { fx->owner = kB7Owner; fx->tag = fe.handle; }
    }
    for (const Boss07Bullet& bb : out.bullets) {            // FUN_00593550 / the inline GoXx + FUN_00593b90, owner D
        bullet::Spawn s = bullet::block_spawn(bb.kind, bb.effect, bb.pos, bb.dir, bb.f);
        s.owner = 0; s.owner_monster = true; s.owner_atk = boss7->body().str;
        bullet::Bullet* bl = bullets.spawn(s);
        if (bl && bl->fx && bb.flag201) bl->fx->no_boss = true;
        if (boss7_log && bl) std::printf("  [boss07] t%llu bullet kind 0x%x effect %d\n", (unsigned long long)tick_count, bb.kind, bb.effect);
    }
    if (out.shake) { g_rig.set_shake_frames(out.shake->first); g_rig.set_shake_amp(out.shake->second); }
    // [not modelled] out.zoom (ZoomBlur), out.sprites (FUN_00584550), out.kill_record (FUN_00403cd0).
    for (const Boss07Fader& fd : out.fader) {              // 0x769f34 / 1c / 28 / 24 ([approx] one colour; zero_30 dropped)
        if (fd.colour) csHost.fade_rgb = *fd.colour & 0xffffffu;
        if (fd.current) csHost.fade_cur = *fd.current;
        if (fd.step) csHost.fade_rate = *fd.step;
        if (fd.target) csHost.fade_tgt = *fd.target;
    }
    if (out.fog && out.fog->first >= 0 && out.fog->first < int(room.fog.regions.size())) fog_script(out.fog->first, out.fog->second);
    if (out.cam_slot) {                                    // 0x76a1f8 / 0x76a1fc
        g_rig.write_forced(out.cam_slot->first, out.cam_slot->second);
        if (boss7_log) std::printf("  [boss07] t%llu CamSlot %d (%.3f)\n", (unsigned long long)tick_count, out.cam_slot->first, out.cam_slot->second);
    }
    for (const auto& [fi, fv] : out.flags) {
        if (fi == 550) { player.boost = float(fv); continue; }   // 0x76c1b4: the Boost gauge
        if (fi < 0 || fi >= 0x200) continue;               // 529..532, 544..546 lie past g_flags: [not modelled]
        if (game.g_flags[fi] != fv) std::printf("  [boss07] t%llu flag %d = %d\n", (unsigned long long)tick_count, fi, fv);
        game.g_flags[fi] = fv;
    }
    const Boss07Player& pw = out.player;
    player_kb.x += pw.push.x; player_kb.z += pw.push.z;    // P+0x338 / +0x340: the body push-out
    player_kb2.x += pw.suction.x; player_kb2.z += pw.suction.z;   // P+0x344 / +0x34c: knock channel 2 (the circle)
    if (pw.pos) {                                          // P+0x50 = (the super's pin, the return)
        feet = {pw.pos->x, pw.pos->y - kOriginH, pw.pos->z};
        pvel = {};
    }
    if (pw.flyer) {                                        // byte P+0x398
        if (boss7_pin != *pw.flyer)
            std::printf("  [boss07] t%llu the player is %s (%.1f,%.1f,%.1f)\n", (unsigned long long)tick_count,
                        *pw.flyer ? "pinned at" : "returned to", feet.x, feet.y + kOriginH, feet.z);
        boss7_pin = *pw.flyer; player_hold = boss7_pin;
        if (boss7_pin) { pv.grounded = false; grounded = false; }
    }
    if (pw.iframes) player_iframes = std::max(player_iframes, int(*pw.iframes));   // P+0x734 = max(.., n)
    for (int sct : pw.sections) avm.set_section(sct, /*force*/true);   // FUN_00446620: ungated
    if (pw.boost_tick && player.boosting) end_boost();     // FUN_00462c40(cl 1) with the gauge at 0 [approx]
    if (out.move_to) meshes.move_to("boss07", out.move_to->target, out.move_to->radius, out.move_to->speed, false);
    if (out.skybox_alpha) meshes.alpha_current("skybox", *out.skybox_alpha);
    if (out.skin_body || out.skin_options)                 // [not modelled] the re-skin
        std::printf("  [boss07] t%llu re-skin: body %d options %d\n", (unsigned long long)tick_count, out.skin_body, int(out.skin_options));
    if (out.particles_wipe) {                              // every GoParticle: +0x11 |= 2, +0x60 &= ~0x80
        for (const auto& p : fxsim.particles()) { p->dead = true; p->hit.armed = false; p->collide = false; }
        std::printf("  [boss07] t%llu every particle removed\n", (unsigned long long)tick_count);
    }
    if (out.glare) csHost.glare = int(*out.glare);         // byte 0x7b2e38 [not modelled] no reader
    if (out.hp_bar) { hud.set_boss_gauge(-11, out.hp_bar->hp, out.hp_bar->max_hp, boss07::kBarLen, boss07::kEmblem); gauge_owner = 0; }
    if (out.exp) {                                         // FUN_004fa4a0(&D+0x94) [H] b0 as boss01..05
        const int lv = eex.bosses.size() > 6 ? eex.bosses[6].level : 49;
        std::printf("  [boss07] t%llu EXP (lv %d)\n", (unsigned long long)tick_count, lv);
        if (ginfo.ok()) award_exp(ginfo.curves[kEnemyExp].at(lv), lv, /*boss*/true);
    }
    if (out.gauge_hide) hud.hide_boss_gauge();
    if (out.destroy && !boss7_gone) {                      // opR, opL, wing, D: +0x11 |= 2, +0x60 &= ~0x80
        boss7_gone = true;
        for (const char* nm : {"boss07", boss7_child_name[0], boss7_child_name[1], boss7_child_name[2]})
            if (MeshObj* m = meshes.find(nm)) { m->alpha = m->alpha_target = 0.0f; m->no_collide = true; m->dirty_alpha = true; m->dirty_pose = true; }
        std::printf("  [boss07] t%llu destroyed\n", (unsigned long long)tick_count);
    }
};
// FUN_004faf20 on Dalles' three GoXx parts for one attacker volume (DALLES_AB §B.1 / §B.2 / §B.7): the body core
// (r 1.5 at the `core` node) and one per option (r 3.0; parked 999 below before phase 2 and while destroyed), each
// with its own re-hit bank; FUN_004fa550 with the part's DEF (the copy taken before the body's became 999) and its
// resist row against the hit's damage class. The element barriers ARE that row: W passes class 0 / 4 only, F 1 / 5,
// E 2 / 6, the dark barrier nothing — a refused class is damage 0 (resist 1.0: no min-1), the clank SE 368 and no
// count. The module consumes the accumulated damage on its next tick (one barrier count per frame with damage).
// True = a part took the hit (also a clank); *banked = a part's bank recorded it (the swing's one-shot rule).
auto boss7_hit = [&](const HitShape& vol, std::uint32_t token, int rehit, float power, int cls, bool swing, bool* banked) {
    bool any = false;
    if (!boss7 || boss7_gone || game.g_flags[156] == 0) return any;
    for (int pi = 0; pi < boss07::kParts; ++pi) {
        const Boss07Part& bp = boss7->part(pi);
        if (!overlap(vol, body_shape(bp.pos, bp.radius, 0.0f, 0, 0.0f))) continue;
        if (boss7_banks[std::size_t(pi)].check_and_record(token, rehit)) continue;
        if (banked) *banked = true;
        AttackStats a;
        a.atk = player_stats().atk;
        a.bonus = power;
        EnemyDefense vd; vd.def = bp.def;
        for (int k = 0; k < 4; ++k) vd.resist[k] = bp.resist[k];
        PlayerDamageMods mods;
        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
        const int dmg = access::damage_dealt(enemy_incoming_damage(a, cls, vd, mods,
            [&](float pct) { return float(rng_pct(rng)) < pct; }), settings().s.access.damage_dealt);   // the shell's Cheats
        const Boss07Hit r = boss7->hit_part(pi, dmg);      // the pool gate, part+0x134 += dmg, the pool drain
        if (r != Boss07Hit::kRefused) {
            any = true;
            if (r == Boss07Hit::kClank) audio.play(368, kSfxAnimVol);   // case 6, dmg <= 0: FUN_0043a040(0x170)
        }
        if (boss7_log || std::getenv("CLERIA_DMG"))
            std::printf("  [boss07] t%llu %s hits part %d (class %d): %d dmg%s, barrier %d hits %u, hp %.0f, options %.0f/%.0f\n",
                        (unsigned long long)tick_count, swing ? "the swing" : "a particle", pi, cls, dmg,
                        r == Boss07Hit::kClank ? " (clank)" : r == Boss07Hit::kRefused ? " (refused: the pool is dry)" : "",
                        boss7->blk().barrier, unsigned(boss7->blk().hits), boss7->body().hp, boss7->blk().opt_hp[0],
                        boss7->blk().opt_hp[1]);
        if (swing && atk_hb.one_shot) break;               // 0x4fd251: a one-shot volume is spent on its first victim
    }
    return any;
};
auto boss7_tick = [&] {
    MeshObj* bm = meshes.find("boss07");
    if (!bm) { boss7_reset(); return; }
    // A scene load re-created the mesh under the old module (a Retry; the root script locates it again before a step
    // sees it gone): the placement the module wrote is gone, or the children it located are (a scene load drops the
    // live-located objects; a mere geometry rebuild keeps them). The module starts over, its statics kept.
    if (boss7 && ((boss7_owned && !bm->has_world) || (boss7_kids && !meshes.find(boss7_child_name[0])))) boss7_reset();
    Model* mdl = cached_big_model(resolve_ymo_path(asset_root, "data\\enemy\\boss07\\boss07.ymo"));
    if (!mdl) return;
    if (!boss7_op_ok) {
        try {
            boss7_op_mdl = Model::parse(read_bytes(asset_root / "ENEMY" / "BOSS07" / "BOSS07OP.YMO"));
            boss7_op_mdl.resolve_hierarchy();
            boss7_op_ok = true;
        } catch (const std::exception&) { return; }
    }
    auto lw = [](std::string t) { for (char& ch : t) ch = char(std::tolower((unsigned char)ch)); return t; };
    if (!boss7) {
        Boss07Setup su;                                    // EEX record 6
        if (eex.bosses.size() > 6) {
            const BossStats& bs = eex.bosses[6];
            su.eex_hp = bs.hp; su.eex_str = bs.atk; su.eex_def = bs.def;
        }
        su.character = character;
        su.diff_index = game.g_flags[144];
        su.diff_easy = game.g_flags[143] != 0;
        su.diff_very_easy = game.g_flags[203] != 0;
        su.boss_rush = game.g_flags[193] != 0;
        su.retries = game.g_flags[196];
        su.pos = bm->pos;
        su.statics = boss7_statics;
        // [approx] The engine's ctor runs at Mesh_Locate, before any script op; this host builds the module on the
        // first step after the load, and the room's load pass has already posed the object from EVERY child script
        // of the root (EV_7080's Mesh_SetPosition(0, -5.5, 0) and its last Mesh_SetMotion, also on the retry / boss
        // rush branch that never starts it). So the ctor's SetClip(N_WAIT) is applied here (no script sets a motion
        // before the intro's KIAI_START), and its pose — Mesh_Locate's (0, 0, 0) with y forced to 4.0 — and its
        // CamSlot(1, 0.025) when the fight gate rises on a mesh no live script has moved (the root went straight to
        // Battle); in the intro the position and the camera are the running script's (it ends on Mesh_MoveTo(0, 4,
        // 0) and Camera_Set(1, 80)).
        boss7 = std::make_unique<Boss07>(su);
        boss7_banks.fill(RehitBank{});
        boss7_owned = false; boss7_gone = false; boss7_kids = false;
        if (boss7_pin) { boss7_pin = false; player_hold = false; }
        for (auto& mb : hud.mini_bar) mb.frac = -1.0f;
        for (int k = 0; k < boss07::kNodes; ++k) {
            boss7_node[std::size_t(k)] = -1;
            for (std::size_t n = 0; n < mdl->nodes.size(); ++n)
                if (lw(mdl->nodes[n].name) == boss7_node_name[k]) { boss7_node[std::size_t(k)] = int(n); break; }
            if (boss7_node[std::size_t(k)] < 0) std::printf("  [boss07] no node %s\n", boss7_node_name[k]);
        }
        for (int c = 0; c < boss07::kSections; ++c) {
            boss7_sect[std::size_t(c)] = -1;
            for (std::size_t s = 0; s < bm->clips.size(); ++s)
                if (lw(bm->clips[s].name) == lw(boss07::kSectionTable[c].name)) { boss7_sect[std::size_t(c)] = int(s); break; }
            if (boss7_sect[std::size_t(c)] != c)
                std::printf("  [boss07] clip %s -> section %d\n", boss07::kSectionTable[c].name, boss7_sect[std::size_t(c)]);
        }
        boss7_op_core = boss7_op_hontai = -1;
        for (std::size_t n = 0; n < boss7_op_mdl.nodes.size(); ++n) {
            if (boss7_op_core < 0 && lw(boss7_op_mdl.nodes[n].name) == "core") boss7_op_core = int(n);
            if (boss7_op_hontai < 0 && lw(boss7_op_mdl.nodes[n].name) == "hontai") boss7_op_hontai = int(n);
        }
        // The ctor's three child GoMeshes at (x, y - 999, z) (P1 §3.3 item 9): live-located scene objects.
        static const char* const kChildYmo[3] = {"data\\enemy\\boss07\\boss07op.ymo", "data\\enemy\\boss07\\boss07op.ymo",
                                                 "data\\enemy\\boss07\\boss07w.ymo"};
        for (int i = 0; i < 3; ++i)
            if (!meshes.find(boss7_child_name[i])) {
                ObjLocateArgs a;
                a.ymo = kChildYmo[i]; a.name = boss7_child_name[i];
                a.x = boss7->child(i).pos.x; a.y = boss7->child(i).pos.y; a.z = boss7->child(i).pos.z;
                live_obj.located.push_back(a);
                live_obj_dirty = true;
            }
        if (boss7_sect[boss07::kNWait] >= 0) { meshes.set_clip_index("boss07", boss7_sect[boss07::kNWait], 0); bm->time_scale = 1.0f; }   // item 14
        boss7_wpos = bm->pos;
        boss7->body().pos = bm->pos;
        boss7_scripted = false; boss7_started = false;
        std::printf("  [boss07] Dalles: HP %.0f STR %.0f, parts DEF %.0f pool %.0f, options %.0f\n", boss7->body().hp,
                    boss7->body().str, boss7->part(0).def, boss7->part(0).pool, boss7->blk().opt_hp[0]);
    }
    if (boss7_gone) {                                      // D+0x11 & 2: the object is removed, its handler no longer runs
        if (boss7_pilot) boss7_pilot();                    // (the pilot lets go of what it holds)
        return;
    }
    Boss07Body& bd = boss7->body();
    const bool on = game.g_flags[156] != 0;
    if (!on && !boss7_veq(bm->pos, boss7_wpos)) boss7_scripted = true;
    if (on && !boss7_started) {                            // the gate rises (see the creation note)
        boss7_started = true;
        if (!boss7_scripted && boss7_veq(bm->pos, boss7_wpos)) {
            bm->pos = {bm->spawn.x, 4.0f, bm->spawn.z};    // obj+0x54 = 4.0 [BYTES 0x5459a4]
            bm->dest = bm->pos; bm->speed = 0; bm->dirty_pos = true;
            g_rig.write_forced(1, 0.025f);                 // P1 §3.3 item 8: CamSlot(1, 0.025)
        }
        std::printf("  [boss07] t%llu the fight starts at (%.2f,%.2f,%.2f)\n", (unsigned long long)tick_count, bm->pos.x, bm->pos.y, bm->pos.z);
    }
    // The MeshObj is the object record: a script's (the intro's Mesh_SetPosition / Mesh_MoveTo) or the engine mesh
    // mover's (the dashes, WAITOPS §4.1) position, the mover's speed D+0x3f8 (IsMoving), the alpha stepper's D+0x418.
    if (!boss7_veq(bm->pos, boss7_wpos)) { bd.pos = bm->pos; boss7_wpos = bm->pos; }
    bd.move_speed = bm->speed;
    bd.alpha = bm->alpha; bd.alpha_target = bm->alpha_target; bd.alpha_step = bm->alpha_step;
    MeshObj* cm[3] = {meshes.find(boss7_child_name[0]), meshes.find(boss7_child_name[1]), meshes.find(boss7_child_name[2])};
    if (cm[0]) boss7_kids = true;
    for (int i = 0; i < 3; ++i) {
        if (!cm[i]) continue;
        // [H] a factory mesh starts on its first section (N_WAIT / WAIT); the spec has no word on it.
        if (cm[i]->sect < 0 && !cm[i]->clips.empty()) meshes.set_clip_index(boss7_child_name[i], 0, 0);
        if (on) boss7->child(i).alpha = cm[i]->alpha;      // the stepper's write-back (the death's 1/180 fade)
    }
    for (RehitBank& b : boss7_banks) b.tick();
    Boss07Input in;
    in.flag156 = on;
    in.player_pos = {feet.x, feet.y + kOriginH, feet.z};
    in.player_hp = in.player_hp_mirror = float(player.hp);
    in.player_section = avm.section();
    in.cam_yaw = camera_yaw();
    int sect = boss07::kNWait;
    for (int c = 0; c < boss07::kSections; ++c) if (boss7_sect[std::size_t(c)] == bm->sect) { sect = c; break; }
    in.section = sect;
    in.count = bm->count;
    in.frame = bm->time;
    bool posed = false;
    if (bm->sect >= 0 && std::size_t(bm->sect) < bm->clips.size())
        for (const AnimClip& c : mdl->clips)
            if (c.name == bm->clips[std::size_t(bm->sect)].name) { mdl->pose(c, bm->time); posed = true; break; }
    if (!posed) mdl->rest_pose();
    // Until the module places the mesh its placement is the script's position, unrotated (blk+0x1a0.. are 0).
    Mat4 place = boss7_m4(boss7->matrix());
    for (int k = 0; k < boss07::kNodes; ++k) {
        const int ni = boss7_node[std::size_t(k)];
        const Mat4 w = (ni >= 0 && std::size_t(ni) < mdl->nodes.size()) ? mdl->nodes[std::size_t(ni)].world * place : place;
        in.nodes[std::size_t(k)] = to_m4(w);
        boss7_node_m[std::size_t(k)] = in.nodes[std::size_t(k)];
    }
    boss7_body_m = boss7->matrix();
    for (int i = 0; i < 2; ++i) {                          // OptNode(i, "core" / "hontai") and the option's count
        const std::size_t u = std::size_t(i);
        boss7_opt_m[u] = boss7_child_m(i);
        in.opt_core[u] = in.opt_hontai[u] = boss7->child(i).pos;
        if (!cm[i]) continue;
        in.opt_count[u] = cm[i]->count;
        bool op = false;
        if (cm[i]->sect >= 0 && std::size_t(cm[i]->sect) < cm[i]->clips.size())
            for (const AnimClip& c : boss7_op_mdl.clips)
                if (c.name == cm[i]->clips[std::size_t(cm[i]->sect)].name) { boss7_op_mdl.pose(c, cm[i]->time); op = true; break; }
        if (!op) boss7_op_mdl.rest_pose();
        const Mat4 pl = boss7_m4(boss7_opt_m[u]);
        auto at = [&](int ni) {
            if (ni < 0) return boss7->child(i).pos;
            const Mat4 w = boss7_op_mdl.nodes[std::size_t(ni)].world * pl;
            return Vec3{w.m[12], w.m[13], w.m[14]};
        };
        in.opt_core[u] = at(boss7_op_core); in.opt_hontai[u] = at(boss7_op_hontai);
    }
    in.rng = [&] { return pick_rng.next(); };
    const int phase0 = boss7->blk().phase, barrier0 = boss7->blk().barrier, mode0 = boss7->blk().mode;
    const std::uint32_t window0 = boss7->blk().window, capture0 = boss7->blk().capture, circle0 = boss7->blk().circle_timer;
    const float oh0[2] = {boss7->blk().opt_hp[0], boss7->blk().opt_hp[1]};
    const float hp0 = bd.hp;
    const Boss07Output out = boss7->tick(in);
    boss7_apply(out, bm);
    boss7_statics = boss7->statics();
    if (on) {
        // The body: D+0x50 (the drift's raw writes; a dash's mover keeps running from there) and SetRot(D, &blk+0x1a0).
        const Mat4 wm = boss7_m4(boss7->matrix());
        if (!bm->has_world || bm->world.m != wm.m || !boss7_veq(bm->pos, bd.pos)) bm->dirty_pose = true;
        bm->pos = bd.pos; bm->has_world = true; bm->world = wm; boss7_owned = true;
        boss7_wpos = bm->pos;
        if (bd.alpha_target != bm->alpha_target || bd.alpha_step != bm->alpha_step) {   // the death: 0 at 1/600
            bm->alpha_target = bd.alpha_target; bm->alpha_step = bd.alpha_step;
        }
        if (bd.alpha != bm->alpha) meshes.alpha_current("boss07", bd.alpha);
        for (int i = 0; i < 3; ++i) {                      // the children: position, SetRot, +0x418 / +0x41c / +0x420
            if (!cm[i] || boss7_gone) continue;
            const Boss07ChildMesh& c = boss7->child(i);
            const Mat4 w = boss7_m4(boss7_child_m(i));
            if (!cm[i]->has_world || cm[i]->world.m != w.m) cm[i]->dirty_pose = true;
            cm[i]->pos = c.pos; cm[i]->rot = c.rot; cm[i]->has_world = true; cm[i]->world = w;
            cm[i]->dest = c.pos; cm[i]->speed = 0;
            if (c.alpha != cm[i]->alpha) meshes.alpha_current(boss7_child_name[i], c.alpha);
            cm[i]->alpha_target = c.alpha_target; cm[i]->alpha_step = c.alpha_step;
            const bool solid = !c.hits_off && !c.removed;   // +0x298 & 0x40 (an option destroyed): its collision is off
            if (cm[i]->no_collide == solid) { cm[i]->no_collide = !solid; cm[i]->dirty_pose = true; }
        }
    }
    // The run's facts, once each as they happen (the drive case reads them).
    const Boss07Block& b = boss7->blk();
    const unsigned long long T = (unsigned long long)tick_count;
    if (b.phase != phase0) std::printf("  [boss07] t%llu phase %d (max HP %.0f)\n", T, b.phase, bd.max_hp);
    static const char* const kBarName[5] = {"bare", "W", "E", "F", "D"};
    if (b.barrier != barrier0) {
        if (b.barrier != 0) std::printf("  [boss07] t%llu barrier %s up\n", T, kBarName[std::clamp(b.barrier, 0, 4)]);
        else std::printf("  [boss07] t%llu barrier %s broken (window %u)\n", T, kBarName[std::clamp(barrier0, 0, 4)], unsigned(b.window));
    }
    if (window0 != 0 && b.window == 0 && boss7_log) std::printf("  [boss07] t%llu the window closed\n", T);
    for (int i = 0; i < 2; ++i)
        if (oh0[i] > 0.0f && b.opt_hp[i] == 0.0f) std::printf("  [boss07] t%llu option %c destroyed\n", T, i == 0 ? 'R' : 'L');
        else if (oh0[i] == 0.0f && b.opt_hp[i] > 0.0f) std::printf("  [boss07] t%llu option %c revived (%.0f)\n", T, i == 0 ? 'R' : 'L', b.opt_hp[i]);
    if (circle0 == 0 && b.circle_timer != 0) std::printf("  [boss07] t%llu the capture circle at (%.1f,%.1f)\n", T, b.circle.x, b.circle.z);
    if (capture0 == 0 && b.capture != 0) std::printf("  [boss07] t%llu the player is captured\n", T);
    if (b.mode != mode0) std::printf("  [boss07] t%llu mode %d -> %d\n", T, mode0, b.mode);
    if (hp0 >= 1.0f && bd.hp < 1.0f) std::printf("  [boss07] t%llu the death starts\n", T);
    for (const Boss07Clip& c : out.clips)
        if (boss7_log || (c.section >= boss07::kNShootL && c.section <= boss07::kNImpactR) || c.section == boss07::kSisLazerStart ||
            c.section == boss07::kKiaiStart)
            std::printf("  [boss07] t%llu clip %s (variant %d)\n", T, boss07::kSectionTable[std::clamp(c.section, 0, boss07::kSections - 1)].name, b.variant);
    if (boss7_log && tick_count % 30 == 0)
        std::printf("  [boss07] t%llu ph %d mode %d bar %s hits %u win %u cd %d sect %s f %.2f | hp %.0f/%.0f opt %.0f/%.0f | "
                    "pos (%.1f,%.1f,%.1f) yaw %.2f core (%.1f,%.1f,%.1f) | f156 %d feet (%.2f,%.2f,%.2f) sec 0x%x php %d mp %.0f slot %d\n",
                    T, b.phase, b.mode, kBarName[std::clamp(b.barrier, 0, 4)], unsigned(b.hits), unsigned(b.window), b.cooldown,
                    boss07::kSectionTable[sect].name, double(in.frame), bd.hp, bd.max_hp, b.opt_hp[0], b.opt_hp[1], bd.pos.x,
                    bd.pos.y, bd.pos.z, b.rot.y, boss7->part(0).pos.x, boss7->part(0).pos.y, boss7->part(0).pos.z,
                    game.g_flags[156], feet.x, feet.y, feet.z, unsigned(avm.section()), player.hp, double(player.sp_cur), g_rig.current());
    if (boss7_pilot) boss7_pilot();
};
// The three world-anchored mini gauges (0x76c1cc / 0x76c1e4 / 0x76c1fc; DALLES_AB §B.3 / §B.7): projected for the
// HUD before its draw. [approx] see the header.
auto boss7_gauges = [&](const Mat4& vp, std::uint32_t cw, std::uint32_t ch) {
    for (int i = 0; i < 3; ++i) {
        hud.mini_bar[i].frac = -1.0f;
        if (!boss7 || boss7_gone || game.g_flags[156] == 0) continue;
        const Boss07MiniGauge& mg = boss7->mini(i);
        if (mg.timer <= 0 || !(mg.max > 0.0f)) continue;
        const Vec3 o = mg.anchor;
        const float* M = vp.m.data();
        const float px = o.x * M[0] + o.y * M[4] + o.z * M[8] + M[12];
        const float py = o.x * M[1] + o.y * M[5] + o.z * M[9] + M[13];
        const float pw = o.x * M[3] + o.y * M[7] + o.z * M[11] + M[15];
        if (!(pw > 1e-4f)) continue;
        hud.mini_bar[i].frac = std::clamp(mg.value / mg.max, 0.0f, 1.0f);
        hud.mini_bar[i].px = (px / pw * 0.5f + 0.5f) * float(cw);
        hud.mini_bar[i].py = (0.5f - py / pw * 0.5f) * float(ch);
    }
};
// drive `boss7[=opts]`: the fight as a player fights it, from then on every tick (`boss7=off` ends it). The pilot only
// ever (a) puts the player ON THE ARENA FLOOR, while grounded, at a spot a player would run to, facing the target,
// and (b) presses Attack / Jump / the stick / the Skill button / the Skill 1-3 keys through the normal input path.
// It never lifts the player and never touches Dalles; what it knows is what a player sees (the aura's colour = which
// barrier is up, the options, the circle on the floor).
//   bare body (phase 0, an open window)   stand in front of the core (it hangs before the chest, 2.4 from the body's
//             axis and 1.3 above the player's origin; the body pushes out inside 2.95) and swing.
//   an element barrier (phase 1)          the Skill key of the bracelet whose class the barrier lets through — the W
//             aura: Skill 1 (0x74, section 0x10d, class 0); the F aura: Skill 3 (0x76, section 0x10e, class 1); the E
//             aura: Skill 2 (0x75, section 0x10c, class 2) — then the Skill button from the same spot, as often as the
//             MP allows; a plain swing in between does nothing to it (class 3: the clank).
//   the dark barrier (phase 2)            nothing passes: stand beside the nearer live option (1.5 below the body's
//             origin, 16.5 out) and swing until both are down; then the core again.
//   the capture circle                    `evade`: hold the stick away from its centre while inside 9.0 of it during
//             its live window; without the option the pilot fights on and is taken into the other dimension.
// Options (comma list): `evade`; `skill` = keep casting the equipped skill on the bare body too (its class passes at
// x 0.6 like any other).
bool b7p_on = false, b7p_evade = false, b7p_skill_always = false;
int b7p_cool = 0;                                          // ticks until its next press
int b7p_hold = 0;                                          // ticks of the Skill button left to hold
int b7p_key = 0;                                           // ticks until it may press a Skill 1-3 key again
int b7p_last = -1;                                         // CLERIA_BOSSLOG: the plan last printed
boss7_pilot = [&] {
    if (!b7p_on) return;
    if (b7p_hold > 0 && --b7p_hold == 0) drive_skill = false;
    if (b7p_key > 0) --b7p_key;
    if (!boss7 || boss7_gone || game.g_flags[156] != 1 || csMgr.world_frozen() || player.hp < 1) return;
    const Boss07Block& b = boss7->blk();
    const Boss07Body& bd = boss7->body();
    if (bd.hp < 1.0f || b.mode == boss07::kSuper || boss7_pin) return;   // dying; in the other dimension
    auto face = [&](float dx, float dz) {
        face_dir = {dx, 0.0f, dz};
        facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
    };
    // Stand on the arena floor at (x, z) facing (tx, tz). Only while grounded; only on the floor itself.
    auto stand = [&](float x, float z, float tx, float tz) {
        if (!pv.grounded) return false;
        const float fl = floor_at(phys_mesh(room), x, z, 1.0f);
        if (fl <= kNoFloor || std::fabs(fl - (-0.1f)) > 0.3f) return false;
        feet = {x, fl, z};
        float dx = tx - x, dz = tz - z;
        const float l = std::sqrt(dx * dx + dz * dz);
        if (l > 1e-3f) face(dx / l, dz / l);
        return true;
    };
    // The capture circle (DALLES_D23 §3): out of its 7.0 while it can take the player.
    if (b7p_evade && b.circle_timer != 0 && b.capture == 0) {
        float ox = feet.x - b.circle.x, oz = feet.z - b.circle.z;
        const float ol = std::sqrt(ox * ox + oz * oz);
        if (ol < 9.0f) {
            if (ol > 1e-3f) { ox /= ol; oz /= ol; } else { ox = 0; oz = -1; }
            drive_wish = {ox, 0.0f, oz}; drive_wish_n = 1;
            return;
        }
    }
    if (b.capture != 0) return;                            // taken: nothing to press (section 2, flag 134)
    // The target: a live option under the dark barrier, else the core.
    int tgt = 0;
    if (b.phase == 2 && b.barrier == boss07::kBarD) {
        float best = 1e30f;
        for (int i = 0; i < 2; ++i) {
            if (!(b.opt_hp[i] > 0.0f)) continue;
            const Vec3& op = boss7->child(i).pos;
            const float d = (op.x - feet.x) * (op.x - feet.x) + (op.z - feet.z) * (op.z - feet.z);
            if (d < best) { best = d; tgt = 1 + i; }
        }
    }
    const Boss07Part& pt = boss7->part(tgt);
    // The element the aura asks for, and its bracelet (skillmp::damage_class: 0x74 -> 0, 0x75 -> 2, 0x76 -> 1).
    const int want = b.barrier == boss07::kBarW ? 0x74 : b.barrier == boss07::kBarE ? 0x75 : b.barrier == boss07::kBarF ? 0x76 : 0;
    if (want != 0 && inv.magic != want && b7p_key == 0) {
        const input::Code c = g_mapper.config().assign[std::size_t(input::KEY_SWORD0 + (want - 0x74))];   // Skill 1 / 2 / 3
        if (c > 0 && c < 256) g_sdl_in.tapped[std::size_t(c)] = true;
        b7p_key = 20;
        if (boss7_log) std::printf("  [pilot] t%llu the Skill %d key (bracelet 0x%x for barrier %d)\n", (unsigned long long)tick_count,
                                   want - 0x74 + 1, want, b.barrier);
    }
    // Where to stand: `reach` from the target — before the core on the side Dalles faces (outside the body's 2.95
    // push-out), beside an option on the player's side of it — or, where that spot is off the floor (Dalles at the
    // arena's rim), the nearest spot round the target that is on it.
    const float reach = tgt == 0 ? 0.9f : pt.radius - 0.6f;
    const float cur = std::sqrt((feet.x - pt.pos.x) * (feet.x - pt.pos.x) + (feet.z - pt.pos.z) * (feet.z - pt.pos.z));
    if (std::fabs(cur - reach) > 0.6f) {
        float bx = tgt == 0 ? pt.pos.x - bd.pos.x : feet.x - pt.pos.x, bz = tgt == 0 ? pt.pos.z - bd.pos.z : feet.z - pt.pos.z;
        const float bl = std::sqrt(bx * bx + bz * bz);
        if (bl > 1e-3f) { bx /= bl; bz /= bl; } else { bx = 0; bz = -1; }
        bool placed = false;
        for (int k = 0; k < 12 && !placed; ++k) {          // 0, +30, -30, +60, .. degrees off that side
            const float a = float((k + 1) / 2) * 0.5235988f * ((k & 1) ? 1.0f : -1.0f);
            const float c = std::cos(a), sn = std::sin(a);
            placed = stand(pt.pos.x + (bx * c - bz * sn) * reach, pt.pos.z + (bx * sn + bz * c) * reach, pt.pos.x, pt.pos.z);
        }
        if (!placed) return;
    } else if (pv.grounded) {
        const float dx = pt.pos.x - feet.x, dz = pt.pos.z - feet.z, l = std::sqrt(dx * dx + dz * dz);
        if (l > 1e-3f) face(dx / l, dz / l);
    }
    const int plan = tgt * 10 + (want != 0 ? 1 : 0);
    if (boss7_log && plan != b7p_last) {
        b7p_last = plan;
        std::printf("  [pilot] t%llu target %s with %s, from (%.2f,%.2f)\n", (unsigned long long)tick_count,
                    tgt == 0 ? "the core" : tgt == 1 ? "option R" : "option L", want != 0 ? "the skill" : "the sword", feet.x, feet.z);
    }
    if (b7p_cool > 0) { --b7p_cool; return; }
    if (b.barrier == boss07::kBarD && tgt == 0) return;    // the dark barrier with no option to hit: wait
    const bool cast = (want != 0 && inv.magic == want) || (b7p_skill_always && want == 0 && skillmp::bracelet_slot(inv.magic) < 3);
    if (cast) {
        skillmp::CostIn ci;
        ci.chr = character; ci.slot = skillmp::bracelet_slot(inv.magic);
        ci.difficulty = difficulty >= 2 ? difficulty - 2 : 0;
        ci.bc80 = std::uint32_t(game.g_flags[217]); ci.boosting = player.boosting;
        if (b7p_hold == 0 && avm.actable() && pv.grounded && player.sp_cur >= skillmp::cost_f(ci)) {
            drive_skill = true; b7p_hold = 2;              // a 2-tick tap: the cast on release
            b7p_cool = 10;
            if (boss7_log) std::printf("  [pilot] t%llu the Skill button (bracelet 0x%x, MP %.0f)\n", (unsigned long long)tick_count,
                                       inv.magic, double(player.sp_cur));
        }
        return;
    }
    if (want != 0) return;                                 // the key press has not landed yet
    try_attack(); b7p_cool = 6;
};
boss7_drive = [&](const std::string& cmd) {
    if (cmd.rfind("boss7", 0) != 0) return false;
    b7p_on = cmd != "boss7=off";
    b7p_evade = cmd.find("evade") != std::string::npos;
    b7p_skill_always = cmd.find("skill") != std::string::npos;
    if (!b7p_on) { drive_skill = false; b7p_hold = 0; }
    return true;
};
