// Darm (room S_7099): the GoMesh "boss08" the room root Mesh_Locate's and the family its ctor builds — the tail
// "boss08t", the core "boss08w", the detached arms "boss08arm", the arena floor "s7099yuka" (created with the body by
// load_room, view_gpu_room.inl) — are driven by the Boss08 module (src/cleria/boss08.hpp) every live step, as
// boss01..07. There is no actor in the fight: the module owns the four objects' fields, the nine GoXx hurt parts,
// the three sub-gauges and the eight tentacle override tracks. Specs: build/boss_re/DARM.md (P1), DARM_A.md,
// DARM_B1.md, DARM_B23.md, DARM_CE.md, DARM_D.md, DARM_TRACKS.md. Included after view_boss07.inl. Per frame:
// boss8_tick in step_meshes, the camera hook in update_camera (view_camera.inl), view_boss08_hit.inl at the player's
// swing and in the mode-0 scan of the player's particles (view_fx_mode0.inl), boss8_gauges before the HUD draw.
// What hurts: EF2 hit particles of side 1 with the body's STR (DARM_B23 §9.1) and the bullets (bullet.cpp kinds 2 / 4
// / 6 / 7 / 0x25 / 0x28) through the view's ordinary paths, and the touch script TOUCH_TAIL (Chr_Damage(998, 250)) of
// the body, the tail and the sixth mesh bs08_mgcra (boss8_touch).
// [approx] the ctor runs here on the first step after the room load, not at Mesh_Locate (the family is located by the
// load, the block is built here): its CamIdx = 1 @ 0.025 is written, and held until the fight starts, only while no
// script forced a slot of its own.
// [approx] the object tick order B, T, W, A is the module's; the meshes' clips are advanced by MeshWorld::step before
// it, and a named-motion request lands at the next step (DARM_A §0: at the end of the same frame) — one tick late at
// most [DARM_A G2 / G3, DARM_CE K4].
// [approx] the node matrices given to the module and to the effects are node.world x placement of the model posed at
// the mesh's clip cursor before the tick (the record's +0x00 and +0x80 matrices are both this, DARM_B1 U9).
// The tail is posed through PoseTracks (ymo.hpp, DARM_TRACKS.md): the override tracks' selection, snapshot and blend
// are the engine's; [approx] the brain's binds / releases land after the tick's evaluation (DARM_TRACKS O7), and
// [not modelled] the main clip's own cross-fade (An+0x14) — the tail's main-owned nodes cut to a new clip.
// [approx] the collision twins BOSS08*_.YMO carry the bone tree: they are posed by the render model's clips at the
// same cursor (MeshObj::twin_clips; the engine rebuilds each entry from its render node, FUN_00502fe0) — the tail's
// hull by its main clip only, without the override tracks. A touch is the stand-in of view_xso13_touch.inl (the body
// sphere against the object's hull, the feet probe).
// [approx] the full-screen fader has one colour in the view (boss07's note); SE and SE2 are the same 2-D cue.
// [approx] the mini gauges are plain two-rect bars at their projected anchors; their 255 timers are not counted down
// (DARM_D U-D7: the counter is the HUD's, not traced).
// [approx] per-material alpha (DARM_A §A.1.1 block 3) is applied to every YMO material of the id table's name through
// Room::mat_alpha; a limb at alpha 0 is not drawn.
// MatMode's RGB (DARM_A §A.6: Down = black, form change = the outline silhouette) multiplies the vertex colours of
// the body's and the tail's materials (boss8_set_matmode, Room::mat_rgb).
// [not modelled] the "damaged limb" material bit 0x20000 and the chest flag word (§A.1.3), the scene light (state kept, no
// consumer: csHost.light_rgb / light_dir), the player tints (the ctor's grey P+0x290, the fall's P+0x148), the hit
// flash +0x2a0, fx+0x1dc |= 0x100 / 0x108 of the inline effects and their pool-full gate (never full here), the `src`
// argument of FX / FX2 (fx+0x1da), the kill record FUN_00403cd0, flags 518 / 519 / 529..532 / 544..546 (dwords past
// g_flags), FUN_00461160(0) / P+0x9fc / Toal's two burst effects at the fall, the armor bit of a closed part (no
// elemental bonus), dmg' of the pool drain (GELALDY G9), W's +0x298 & 0x8000 heal (DARM_D U-D3), the radius-14
// ground shadow of the draw case, the SE preload, boss rush's end-of-room (DARM_D §6).
constexpr int kB8Owner = -0xB08;                           // fx owner tag: the module's effect handles
const char* const boss8_name[5] = {"boss08", "boss08t", "boss08w", "boss08arm", "s7099yuka"};
const char* const boss8_ymo[4] = {"data\\enemy\\boss08\\boss08.ymo", "data\\enemy\\boss08\\boss08t.ymo",
                                  "data\\enemy\\boss08\\boss08w.ymo", "data\\enemy\\boss08\\boss08arm.ymo"};
// The body's node table 0x7b9328 and material id table 0x7b9458 (DARM.md §3.2), lower case.
const char* const boss8_bnode_name[boss08::kBodyNodes] = {
    "bone_skirt1", "bone_skirt2", "bone_head", "bone_rightleg", "bone_rightfoot", "bone_leftleg", "bone_leftfoot", "bone_spear",
    "spearmark", "leftstigma1mark", "leftstigma2mark", "leftstigma3mark", "rightstigma1mark", "rightstigma2mark",
    "rightstigma3mark", "righttoe2-3mark", "lefttoe2-3mark", "bone_righttoe3-1", "bone_lefttoe3-1", "berrymouthmark",
    "bone_maekake", "bone_spine2", "bone_lefttoe1-1", "bone_righttoe1-1"};
const char* const boss8_mat_name[boss08::kBodyMats] = {
    "00_07_chest_rinkaku", "01_06_chest_insidesmoke", "04_03_chest", "00_07_head_rinkaku", "01_06_head_insidesmoke",
    "02_01_head_horn", "04_03_head", "07_03_headeye_add", "00_07_leftwing_rinkaku", "05_04_leftwing",
    "00_07_rightwing_rinkaku", "05_04_rightwing", "00_07_skirt_rinkaku", "01_06_skirt_insidesmoke",
    "01_06_skirt_insidesmoke_double", "02_01_skirt", "02_01_skirt_nolight", "05_04_skirt", "07_04_skirteye_add",
    "07_08_wave_add", "08_09_leftthunder", "08_09_rightthunder", "04_03_spear", "06_05_leftarm", "06_05_rightarm",
    "03_02_leftleg", "00_07_leftleg_rinkaku", "01_06_leftleg_insidesmoke", "02_01_lefthand", "00_07_lefthand_rinkaku",
    "01_06_lefthand_insidesmoke", "03_02_rightleg", "00_07_rightleg_rinkaku", "01_06_rightleg_insidesmoke",
    "02_01_righthand", "00_07_righthand_rinkaku", "01_06_righthand_insidesmoke", "00_07_leftlegpat_rinkaku",
    "00_07_rightlegpat_rinkaku", "00_07_leftarm_rinkaku", "00_07_rightarm_rinkaku"};
Boss08Statics boss8_statics;                               // 0x7d9bbc / c0, 0x7de074..: process statics, kept across attempts
std::array<int, boss08::kBodyNodes> boss8_bnode{};         // node table -> the model's node
std::array<int, boss08::kTailNodes> boss8_tnode{};
std::array<int, boss08::kBodyMats> boss8_mat_id{};         // material id table -> the YMO material
std::array<std::vector<int>, 5> boss8_sect;                // per mesh: section -> the mesh's clip index
std::array<ef2sim::M4, boss08::kBodyNodes> boss8_bnode_m{};   // attach parents: the body's node records,
std::array<ef2sim::M4, boss08::kTailNodes> boss8_tnode_m{};   // the tail's,
ef2sim::M4 boss8_body_m, boss8_ora_m;                      // B+0x20 and W's OraMarker
Model boss8_w_mdl;                                         // BOSS08W.YMO, posed for OraMarker
bool boss8_w_ok = false;
int boss8_w_ora = -1;
PoseTracks boss8_tpose;                                    // the tail's node records: owners, samples, snapshots
std::array<int, boss08::kTracks> boss8_trk_root{};         // each override track's root node (the bind's)
std::array<RehitBank, boss08::kBodyParts + 1> boss8_banks{};   // the parts' own re-hit banks
std::vector<float> boss8_mat;                              // the body's per-material alpha as last applied
bool boss8_owned = false;                                  // the module placed the meshes (MeshObj::has_world is ours)
bool boss8_pin = false;                                    // byte P+0x398 = 1 (the dead player under the rim)
bool boss8_mgcra = false;                                  // the sixth mesh is in the scene
int boss8_matmode = 0;                                     // the last MatMode
std::array<std::vector<Vec3>, 2> boss8_rgb;                // the body's and the tail's material RGB (MatMode), per YMO material
bool boss8_rgb_dirty = false;
std::uint32_t boss8_ptint = 0xffffffffu;                   // P+0x148 / +0x278 as last written [not modelled]
bool boss8_started = false, boss8_over = false;            // flag 156 was seen; the kill record was written
bool boss8_cam_scripted = false;                           // a script forced a camera slot before the fight
std::function<void()> boss8_pilot;                         // drive boss8: the per-tick player pilot (below)
static const bool boss8_log = std::getenv("CLERIA_BOSSLOG") != nullptr;
auto boss8_m4 = [](const ef2sim::M4& a) {
    Mat4 r;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) r.m[std::size_t(i * 4 + j)] = a.m[i][j];
    return r;
};
auto boss8_lw = [](std::string t) { for (char& ch : t) ch = char(std::tolower((unsigned char)ch)); return t; };
// MatMode FUN_005507a0 (DARM_A §A.6) [BYTES 0x5507a0..0x550a91]: the instance material records' RGB (+0x14..+0x1c).
// m 0 / 1: every material of the body and of the tail = 1 - m (lit / black). m 2: the body's 41 id-table materials
// = 1 for the 16 of the set below (the outlines and the three _add layers), else 0 — the other materials keep their
// RGB — and the tail's material 4 = 1, the others 0 ([H] the record index is the YMO material order). boss8_tick
// hands them to the draw (Room::mat_rgb).
auto boss8_set_matmode = [&](int m) {
    boss8_matmode = m;
    const Model* md[2] = {cached_big_model(resolve_ymo_path(asset_root, boss8_ymo[0])),
                          cached_big_model(resolve_ymo_path(asset_root, boss8_ymo[1]))};
    for (int i = 0; i < 2; ++i) if (md[i]) boss8_rgb[std::size_t(i)].resize(md[i]->materials.size(), Vec3{1.0f, 1.0f, 1.0f});
    if (m != 2) {
        const float v = float(1 - m);
        for (std::vector<Vec3>& l : boss8_rgb) for (Vec3& c : l) c = {v, v, v};
    } else {
        static const int kOn[16] = {0, 3, 7, 8, 10, 12, 18, 19, 26, 29, 32, 35, 37, 38, 39, 40};
        for (int k = 0; k < boss08::kBodyMats; ++k) {
            const int id = boss8_mat_id[std::size_t(k)];
            const float v = std::find(std::begin(kOn), std::end(kOn), k) != std::end(kOn) ? 1.0f : 0.0f;
            if (id >= 0 && std::size_t(id) < boss8_rgb[0].size()) boss8_rgb[0][std::size_t(id)] = {v, v, v};
        }
        for (std::size_t n = 0; n < boss8_rgb[1].size(); ++n) { const float v = n == 4 ? 1.0f : 0.0f; boss8_rgb[1][n] = {v, v, v}; }
    }
    boss8_rgb_dirty = true;
};
// FUN_0042bb10's boss08 path (DARM.md §3.1, DARM_CE.md §4.7): once per camera step while the object "boss08" has its
// block, the hook FUN_0055f800 rewrites the CURRENT slot (1 Normal, 2 Low, 3 High, 4 OnBack, 5 the fall) and returns
// 1 for every k: the region pick is skipped (the forced slot 0x76a1f8 applies after it either way).
g_boss08_cam_hook = [&]() -> bool {
    if (!boss8) return false;
    const int k = g_rig.current();
    if (k < 0 || k >= CamRig::kSlots) return false;
    const CameraRegion& s = g_rig.slot(k);
    Boss08CamRecord rec;
    rec.eye_fixed = s.eye.fixed_pos; rec.target_fixed = s.target.fixed_pos; rec.eye_pitch = s.eye.pitch;
    rec.eye_dist = s.eye.dist; rec.fov = s.fov; rec.offset = s.offset;
    rec.eye_mode = int(s.eye.mode); rec.target_mode = int(s.target.mode);
    const bool claimed = boss8->camera_hook(k, rec, {feet.x, feet.y + kOriginH, feet.z});
    if (k >= 1 && k <= 5) {
        CameraRegion& w = g_rig.edit_slot(k);
        w.eye.fixed_pos = rec.eye_fixed; w.target.fixed_pos = rec.target_fixed; w.eye.pitch = rec.eye_pitch;
        w.eye.dist = rec.eye_dist; w.fov = rec.fov; w.offset = rec.offset;
        w.eye.mode = std::uint32_t(rec.eye_mode); w.target.mode = std::uint32_t(rec.target_mode);
    }
    boss8_statics = boss8->statics();
    return claimed;
};
// The tail's pose (DARM_CE §2.3, DARM_TRACKS.md): its main clip at the mesh's cursor, each tentacle sub-tree a bound
// track owns from the track's clip at its own cursor, cross-faded against the snapshot by the track's weight (the
// records are the module's, stepped by it). [not modelled] the main clip's own cross-fade weight An+0x14 (0 here, as
// for every mesh: MeshWorld's SetClip has no blend path).
auto boss8_pose_tail = [&](Model& m) {
    const MeshObj* o = meshes.find(boss8_name[1]);
    const AnimClip* mainc = nullptr;
    if (o && o->sect >= 0 && std::size_t(o->sect) < o->clips.size())
        for (const AnimClip& c : m.clips) if (c.name == o->clips[std::size_t(o->sect)].name) { mainc = &c; break; }
    if (!mainc || !boss8) { m.rest_pose(); return; }
    PoseTracks::Track tk[boss08::kTracks];
    for (int i = 0; i < boss08::kTracks; ++i) {
        const boss08::Track& t = boss8->track(i);
        const int ci = t.section >= 0 && t.section < boss08::kTailClips ? boss8_sect[1][std::size_t(t.section)] : -1;
        if (o && ci >= 0 && std::size_t(ci) < o->clips.size())
            for (const AnimClip& c : m.clips) if (c.name == o->clips[std::size_t(ci)].name) { tk[i].clip = &c; break; }
        tk[i].frame = t.frame; tk[i].weight = t.weight; tk[i].releasing = t.releasing;
    }
    boss8_tpose.evaluate(m, *mainc, o->time, 0.0f, tk);
};
// ... and the same pose for the tail's re-bake (scene.cpp poses a kept model by one clip otherwise).
set_object_pose_hook([&](const std::string& n, Model& m) {
    if (!boss8 || n != boss8_name[1] || boss8_sect[1].empty()) return false;
    boss8_pose_tail(m);
    return true;
});
struct Boss8HookGuard { ~Boss8HookGuard() { g_boss08_cam_hook = nullptr; set_object_pose_hook(nullptr); } } boss8_hook_guard;   // the hooks capture this scope
auto boss8_reset = [&] {
    if (boss8) boss8_statics = boss8->statics();           // no writer but the band: they outlive the object
    const bool had = boss8 != nullptr;
    boss8.reset();
    if (boss8_pin) { boss8_pin = false; player_hold = false; }
    boss8_owned = false; boss8_mgcra = false; boss8_started = false; boss8_over = false;
    boss8_mat.clear();
    if (had) for (auto& mb : hud.mini_bar) mb.frac = -1.0f;
};
// One contact of the player with a touch-flagged mesh ("Touch on": +0x28c |= 0x0c, +0x2e4 = "Touch_Tail"; DARM_CE §0):
// a side or a floor contact starts the object's script TOUCH_TAIL = Chr_Damage(998, 250.0, 0, 0) (DARM.md §1.5), when
// no fix is held [approx: the header's stand-in].
auto boss8_touch = [&](const char* nm) {
    if (csMgr.world_frozen() || player.hp < 1) return;
    const RoomMesh& cm = phys_mesh(room);
    const RoomMesh::ObjRange* r = cm.object_range(nm);
    const MeshObj* mo = meshes.find(nm);
    if (!r || r->t1 <= r->t0 || !mo || mo->no_collide) return;
    const Vec3 O{feet.x, feet.y + kOriginH, feet.z};
    const bool side = sphere_overlaps_tris(cm, O, kCharRadius + 0.05f, r->t0, r->t1);
    const bool floor = pv.grounded && segment_hits_tris(cm, O, {feet.x, feet.y - 0.2f, feet.z}, r->t0, r->t1);
    if (!side && !floor) return;
    if (csMgr.start("touch_tail", XsoVm::Vars{}, /*owner: the player*/13) && boss8_log)
        std::printf("  [boss08] t%llu touch %s -> touch_tail\n", (unsigned long long)tick_count, nm);
};
auto boss8_apply = [&](const Boss08Output& out, MeshObj** mo) {
    for (int id : out.rumble) pad_rumble(id);
    for (const Boss08Clip& c : out.clips) {                // SetClip: FUN_005f5c80(M, section, 1; xmm3 0)
        if (c.mesh < 0 || c.mesh > boss08::kW) continue;
        const std::vector<int>& sm = boss8_sect[std::size_t(c.mesh)];
        if (c.section < 0 || std::size_t(c.section) >= sm.size() || sm[std::size_t(c.section)] < 0) continue;
        meshes.set_clip_index(boss8_name[c.mesh], sm[std::size_t(c.section)], 0);
        mo[c.mesh]->time_scale = 1.0f;
    }
    for (const Boss08Motion& mn : out.motions)             // the request +0x424 (flag = +0x444), count 0
        if (mn.mesh >= 0 && mn.mesh < 5) meshes.set_motion(boss8_name[mn.mesh], mn.name, mn.flag, 0);
    for (const Boss08Sound& sd : out.sounds) audio.play(sd.id, kSfxAnimVol);   // FUN_0043a040(id, 0x400): 2-D
    for (int h : out.kill_handles)                         // one effect: +0x1d4 = 1
        for (auto& f : fxsim.effects()) if (f->owner == kB8Owner && f->tag == h) f->kill = true;
    for (const Boss08Effect& fe : out.effects) {
        const Ef2* ef = ef2_of(fe.id);
        if (!ef) { if (boss8_log) std::printf("  [boss08] no EF2 %d\n", fe.id); continue; }
        const ef2sim::M4* parent = nullptr;
        switch (fe.parent) {
            case Boss08Parent::kBody: parent = &boss8_body_m; break;
            case Boss08Parent::kBodyNode: parent = &boss8_bnode_m[std::size_t(std::clamp(fe.parent_index, 0, boss08::kBodyNodes - 1))]; break;
            case Boss08Parent::kTailNode: parent = &boss8_tnode_m[std::size_t(std::clamp(fe.parent_index, 0, boss08::kTailNodes - 1))]; break;
            case Boss08Parent::kCoreOra: parent = &boss8_ora_m; break;
            default: break;
        }
        ef2sim::SpawnParams sp;
        sp.pos = fe.pos; sp.rot = fe.rot; sp.scale = fe.scale;
        sp.power = fe.power; sp.knock = fe.knock; sp.rtype = fe.rtype; sp.status = fe.status; sp.cls = fe.cls;
        sp.side = fe.side;
        // The stat block &B+0x94 / &T+0x94 / &W+0x94 copied at the spawn.
        sp.atk = boss8->mesh(fe.stat == Boss08Stat::kTail ? boss08::kT : fe.stat == Boss08Stat::kCore ? boss08::kW : boss08::kB).str;
        // FUN_00451c20: local = T(off) . RotYPR(rot) . S(scale); mode 1 fx.M = local . parent, mode 2 local + its row 3.
        const ef2sim::M4 local = ef2sim::mul(ef2sim::mul(ef2sim::translation(fe.at_off.x, fe.at_off.y, fe.at_off.z),
                                                         ef2sim::rot_ypr(fe.at_rot.y, fe.at_rot.x, fe.at_rot.z)),
                                             ef2sim::scaling(fe.at_scale.x, fe.at_scale.y, fe.at_scale.z));
        if (parent) {
            const ef2sim::M4 m0 = ef2sim::mul(local, *parent);
            sp.pos = fe.at_mode == 1 ? Vec3{m0.m[3][0], m0.m[3][1], m0.m[3][2]}
                                     : Vec3{parent->m[3][0] + fe.at_off.x, parent->m[3][1] + fe.at_off.y,
                                            parent->m[3][2] + fe.at_off.z};
        }
        ef2sim::Effect* fx = fxsim.spawn(fe.id, *ef, sp);
        if (!fx) continue;
        if (fe.call == Boss08Call::kFx2) { fx->scale_x = fe.scale.x; fx->scale_y = fe.scale.y; fx->scale_z = fe.scale.z; }   // FUN_00450880 keeps it
        if (parent) { fx->local = local; fx->attach_parent = parent; fx->attach_mode = fe.at_mode == 1 ? 1 : 2; }
        if (fe.flag201) fx->no_boss = true;                // fx+0x201: its world rays skip the boss meshes
        if (fe.handle) { fx->owner = kB8Owner; fx->tag = fe.handle; }
        if (boss8_log && fe.id != 464 && fe.id != 454)
            std::printf("  [boss08] t%llu effect %d at (%.1f,%.1f,%.1f)%s\n", (unsigned long long)tick_count, fe.id, sp.pos.x, sp.pos.y,
                        sp.pos.z, parent ? " attached" : "");
    }
    int nb = 0;
    for (const Boss08Bullet& bb : out.bullets) {            // FUN_00593550 / the raw GoXx + FUN_00593b90, owner B
        bullet::Spawn s = bullet::block_spawn(bb.kind, bb.effect, bb.pos, bb.dir, bb.f);
        s.owner = 0; s.owner_monster = true; s.owner_atk = boss8->mesh(boss08::kB).str;
        bullet::Bullet* bl = bullets.spawn(s);
        if (bl && bl->fx && bb.flag201) bl->fx->no_boss = true;
        nb += bl != nullptr;
    }
    if (boss8_log && nb && out.bullets.front().effect != 487)
        std::printf("  [boss08] t%llu %d bullet(s) kind 0x%x effect %d\n", (unsigned long long)tick_count, nb, out.bullets.front().kind,
                    out.bullets.front().effect);
    if (out.shake) { g_rig.set_shake_frames(out.shake->frames); g_rig.set_shake_amp({out.shake->ax, out.shake->ay, 0.0f}); }
    if (out.zoom) zoom_blur(out.zoom->first, out.zoom->second);   // ZoomBlur (0x76a550 / 0x76a554)
    for (const Boss08Fader& fd : out.fader) {              // 0x769f34 / 1c / 28 / 24 ([approx] one colour)
        if (fd.colour) csHost.fade_rgb = *fd.colour & 0xffffffu;
        if (fd.current) csHost.fade_cur = *fd.current;
        if (fd.step) csHost.fade_rate = *fd.step;
        if (fd.target) csHost.fade_tgt = *fd.target;
    }
    if (out.cam_slot) {                                    // 0x76a1f8 / 0x76a1fc
        g_rig.write_forced(out.cam_slot->first, out.cam_slot->second);
        std::printf("  [boss08] t%llu CamSlot %d (%.3f)\n", (unsigned long long)tick_count, out.cam_slot->first, out.cam_slot->second);
    }
    for (const auto& [fi, fv] : out.flags) {
        if (fi >= 529 && fi <= 532) { ail.t[fi - 529] = fv; continue; }   // the status timers 0x76c160..0x76c16c
        if (fi < 0 || fi >= 0x200) continue;               // 518 / 519, 529..532, 544..546 lie past g_flags: [not modelled]
        if (game.g_flags[fi] != fv) std::printf("  [boss08] t%llu flag %d = %d\n", (unsigned long long)tick_count, fi, fv);
        game.g_flags[fi] = fv;
    }
    const Boss08Player& pw = out.player;
    player_kb.x += pw.push.x; player_kb.z += pw.push.z;    // P+0x338 / +0x340: the body push-out
    player_kb2.x += pw.knock2.x; player_kb2.z += pw.knock2.z;   // P+0x344 / +0x34c: knock channel 2 (the cannon's pull)
    if (pw.ext && player.hp >= 1) csHost.pforce = {pw.ext->v.x, pw.ext->v.z, pw.ext->ticks};   // P+0x5d0 / +0x5d8, +0x5dc
    if (pw.dead_stop) csHost.pforce = {};                  // P+0x5d0..0x5dc = 0 ([not modelled] P+0xa3c..0xa44)
    if (pw.fall_start) {                                   // P+0x314 = P+0x308 = -pi/2 ([not modelled] the rest)
        face_dir = {0.0f, 0.0f, -1.0f};
        facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
        std::printf("  [boss08] t%llu the player fell off the floor (y %.1f)\n", (unsigned long long)tick_count, feet.y + kOriginH);
    }
    if (pw.pos) {                                          // P+0x50..0x58 =
        feet = {pw.pos->x, pw.pos->y - kOriginH, pw.pos->z};
        pvel = {};
    }
    if (pw.fall_end) {                                     // P+0x344.. = P+0x338.. = P+0x5d0.. = 0, P+0x5dc = 0
        player_kb = {}; player_kb2 = {}; csHost.pforce = {};
        std::printf("  [boss08] t%llu the player is put back at (%.1f,%.1f,%.1f)\n", (unsigned long long)tick_count, feet.x,
                    feet.y + kOriginH, feet.z);
    }
    if (pw.tint) boss8_ptint = *pw.tint;                   // [not modelled]
    if (pw.flyer && !boss8_pin) { boss8_pin = true; player_hold = true; }   // byte P+0x398 = 1
    if (pw.iframes) player_iframes = int(*pw.iframes);     // P+0x734 = n
    if (pw.iframes_min) player_iframes = std::max(player_iframes, int(*pw.iframes_min));
    for (int sct : pw.sections) avm.set_section(sct, /*force*/true);   // FUN_00446620: ungated
    if (out.light) {                                       // FUN_00443ea0(colour, &dir): state only [not modelled]
        csHost.light_rgb = {float((*out.light >> 16) & 0xffu) / 255.0f, float((*out.light >> 8) & 0xffu) / 255.0f,
                            float(*out.light & 0xffu) / 255.0f};
        csHost.light_dir = {-0.182574f, -0.912871f, 0.365148f};
    }
    if (out.death_light) { csHost.light_rgb = {0.376471f, 0.376471f, 0.376471f}; csHost.light_dir = {-0.182574f, -0.912871f, 0.365148f}; }
    static const char* const kMode[3] = {"lit", "black (Down)", "the outline (form change)"};
    for (int m : out.mat_mode) {
        boss8_set_matmode(m);
        std::printf("  [boss08] t%llu MatMode %d: %s\n", (unsigned long long)tick_count, m, kMode[std::clamp(m, 0, 2)]);
    }
    // The sixth mesh (DARM_B23 §4.6): a live-located map object at the origin with the step + touch bits and the
    // script Touch_Tail (boss8_tick runs its contact); FUN_0040b290 = hidden with its collision off.
    if (out.mgcra_spawn) {
        if (!room.mesh.object_range("bs08_mgcra")) {
            bool have = false;
            for (const ObjLocateArgs& l : live_obj.located) have = have || boss8_lw(l.name) == "bs08_mgcra";
            if (!have) {
                ObjLocateArgs a;
                a.ymo = "DATA\\map\\Mapobj\\bs08_mgcra\\bs08_mgcra.ymo"; a.name = "bs08_mgcra";
                live_obj.located.push_back(a);
            }
            live_obj.visible["bs08_mgcra"] = true; live_obj.collide["bs08_mgcra"] = true;
            live_obj_dirty = true;
        }
        boss8_mgcra = true;
        std::printf("  [boss08] t%llu bs08_mgcra created\n", (unsigned long long)tick_count);
    }
    if (out.mgcra_delete) {
        live_obj.visible["bs08_mgcra"] = false; live_obj.collide["bs08_mgcra"] = false;
        live_obj_dirty = true;
        boss8_mgcra = false;
        std::printf("  [boss08] t%llu bs08_mgcra deleted\n", (unsigned long long)tick_count);
    }
    if (out.kill_bit_particles)                            // every GoParticle of effect 467: +0x11 |= 2, +0x60 &= ~0x80
        for (const auto& p : fxsim.particles())
            if (p->fx && p->fx->id == 467 && !p->dead) { p->dead = true; p->hit.armed = false; p->collide = false; }
    if (out.hp_bar) { hud.set_boss_gauge(-12, out.hp_bar->hp, out.hp_bar->max_hp, boss08::kBarLen, boss08::kEmblem); gauge_owner = 0; }
    if (out.gauge_hide) hud.hide_boss_gauge();
    if (out.kill_record && !boss8_over) {                  // FUN_00403cd0 [not modelled], every frame from here
        boss8_over = true;
        std::printf("  [boss08] t%llu the body is gone (alpha 0)\n", (unsigned long long)tick_count);
    }
};
// FUN_004faf20 on Darm's nine GoXx parts for one attacker volume (DARM_D §4.3; the global part list, no owner-mesh
// flag): the body's eight spheres (a sunk one is 999 below) and the core's, each with its own re-hit bank;
// FUN_004fa550 with the part's DEF and resist 0 [H DARM_D U-D2] — DEF 998 (an open limb, the barrier) makes every
// landed hit exactly 1 by the min-1 rule, DEF 999 (closed) 0, the exposed core the real formula. The module consumes
// the accumulated damage on its next tick (one limb step per part-frame with damage). Type 7: no sound on a 0-damage
// hit. True = a part took the hit; *banked = a part's bank recorded it (the swing's one-shot rule).
auto boss8_hit = [&](const HitShape& vol, std::uint32_t token, int rehit, float power, int cls, bool swing, bool* banked) {
    bool any = false;
    if (!boss8 || game.g_flags[156] == 0) return any;
    for (int pi = 0; pi <= boss08::kBodyParts; ++pi) {
        const Boss08Part& bp = boss8->part(pi);
        if (!overlap(vol, body_shape(bp.pos, bp.radius, 0.0f, 0, 0.0f))) continue;
        if (boss8_banks[std::size_t(pi)].check_and_record(token, rehit)) continue;
        if (banked) *banked = true;
        AttackStats a;
        a.atk = player_stats().atk;
        a.bonus = power;
        EnemyDefense vd; vd.def = bp.def;
        PlayerDamageMods mods;
        mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
        const int dmg = access::damage_dealt(enemy_incoming_damage(a, cls, vd, mods,
            [&](float pct) { return float(rng_pct(rng)) < pct; }), settings().s.access.damage_dealt);   // the shell's Cheats
        const bool core = pi == boss08::kBodyParts;
        const Boss08Hit r = boss8->hit_part(core ? boss08::kW : boss08::kB, core ? 0 : pi, dmg);
        if (r != Boss08Hit::kRefused) { any = true; part_hit_feedback(bp.pos, bp.armor, dmg, cls); }   // FUN_004faf20's tail
        if (boss8_log || std::getenv("CLERIA_DMG"))
            std::printf("  [boss08] t%llu %s hits %s %d (DEF %.0f, class %d): %d dmg%s | limbs %.3f/%.3f/%.3f core hp %.0f barrier %u\n",
                        (unsigned long long)tick_count, swing ? "the swing" : "a particle", core ? "the core" : "part", core ? 0 : pi,
                        bp.def, cls, dmg, r == Boss08Hit::kRefused ? " (refused: the pool is dry)" : "", boss8->body().chest_t,
                        boss8->body().leg_l_t, boss8->body().leg_r_t, boss8->mesh(boss08::kW).hp, unsigned(boss8->body().barrier_hits));
        if (swing && atk_hb.one_shot) break;               // 0x4fd251: a one-shot volume is spent on its first victim
    }
    return any;
};
auto boss8_tick = [&] {
    MeshObj* bm = meshes.find("boss08");
    if (!bm) { boss8_reset(); return; }
    // A scene load re-created the meshes under the old module (a Retry): the placement the module wrote is gone. The
    // module starts over, its statics kept.
    if (boss8 && boss8_owned && !bm->has_world) boss8_reset();
    MeshObj* mo[5] = {bm, meshes.find(boss8_name[1]), meshes.find(boss8_name[2]), meshes.find(boss8_name[3]), meshes.find(boss8_name[4])};
    for (MeshObj* m : mo) if (!m) return;                  // the family is located with the body (load_room)
    Model* bmdl = cached_big_model(resolve_ymo_path(asset_root, boss8_ymo[0]));
    Model* tmdl = cached_big_model(resolve_ymo_path(asset_root, boss8_ymo[1]));
    if (!bmdl || !tmdl) return;
    if (!boss8_w_ok) {
        try {
            boss8_w_mdl = Model::parse(read_bytes(asset_root / "ENEMY" / "BOSS08" / "BOSS08W.YMO"));
            boss8_w_mdl.resolve_hierarchy();
            boss8_w_ok = true;
            for (std::size_t n = 0; n < boss8_w_mdl.nodes.size(); ++n)
                if (boss8_lw(boss8_w_mdl.nodes[n].name) == "oramarker") { boss8_w_ora = int(n); break; }
        } catch (const std::exception&) { return; }
    }
    const unsigned long long T = (unsigned long long)tick_count;
    if (!boss8) {
        Boss08Setup su;                                    // EEX record 7
        if (eex.bosses.size() > 7) {
            const BossStats& bs = eex.bosses[7];
            su.eex_hp = bs.hp; su.eex_str = bs.atk; su.eex_def = bs.def;
        }
        su.character = character;
        su.diff_index = game.g_flags[144];
        su.diff_easy = game.g_flags[143] != 0;
        su.diff_very_easy = game.g_flags[203] != 0;
        su.boss_rush = game.g_flags[193] != 0;
        su.retries = game.g_flags[196];
        su.pos = bm->pos;
        su.statics = boss8_statics;
        boss8 = std::make_unique<Boss08>(su);
        boss8_banks.fill(RehitBank{});
        boss8_owned = false; boss8_mgcra = false; boss8_started = false; boss8_over = false;
        if (boss8_pin) { boss8_pin = false; player_hold = false; }
        boss8_mat.clear();
        auto find_node = [&](const Model& m, const std::string& nm) {
            for (std::size_t n = 0; n < m.nodes.size(); ++n) if (boss8_lw(m.nodes[n].name) == nm) return int(n);
            std::printf("  [boss08] no node %s\n", nm.c_str());
            return -1;
        };
        for (int k = 0; k < boss08::kBodyNodes; ++k) boss8_bnode[std::size_t(k)] = find_node(*bmdl, boss8_bnode_name[k]);
        // The tail's node table 0x7b9388: Bone_TailRoot, Bone_eye, Bone_Tail1..7, Bone_RightTentacle{1..4}-{1..5},
        // Bone_LeftTentacle{1..4}-{1..5}.
        for (int k = 0; k < boss08::kTailNodes; ++k) {
            std::string nm = k == 0 ? "bone_tailroot" : k == 1 ? "bone_eye" : k < 9 ? "bone_tail" + std::to_string(k - 1) : "";
            if (k >= 9) nm = std::string(k < 29 ? "bone_righttentacle" : "bone_lefttentacle") + std::to_string((k - (k < 29 ? 9 : 29)) / 5 + 1) +
                             "-" + std::to_string((k - (k < 29 ? 9 : 29)) % 5 + 1);
            boss8_tnode[std::size_t(k)] = find_node(*tmdl, nm);
        }
        for (int k = 0; k < boss08::kBodyMats; ++k) {
            boss8_mat_id[std::size_t(k)] = -1;
            for (std::size_t n = 0; n < bmdl->materials.size(); ++n)
                if (boss8_lw(bmdl->materials[n].name) == boss8_mat_name[k]) { boss8_mat_id[std::size_t(k)] = int(n); break; }
            if (boss8_mat_id[std::size_t(k)] < 0) std::printf("  [boss08] no material %s\n", boss8_mat_name[k]);
        }
        static const boss08::ClipInfo* const kTab[5] = {boss08::kBodyClipTable, boss08::kTailClipTable, boss08::kCoreClipTable,
                                                        boss08::kArmClipTable, boss08::kFloorClipTable};
        static const int kTabN[5] = {boss08::kBodyClips, boss08::kTailClips, boss08::kCoreClips, boss08::kArmClips, boss08::kFloorClips};
        for (int i = 0; i < 5; ++i) {
            boss8_sect[std::size_t(i)].assign(std::size_t(kTabN[i]), -1);
            for (int c = 0; c < kTabN[i]; ++c) {
                for (std::size_t s = 0; s < mo[i]->clips.size(); ++s)
                    if (boss8_lw(mo[i]->clips[s].name) == boss8_lw(kTab[i][c].name)) { boss8_sect[std::size_t(i)][std::size_t(c)] = int(s); break; }
                if (boss8_sect[std::size_t(i)][std::size_t(c)] != c)
                    std::printf("  [boss08] %s clip %s -> section %d\n", boss8_name[i], kTab[i][c].name, boss8_sect[std::size_t(i)][std::size_t(c)]);
            }
        }
        // The ctor's host side (boss08.cpp, the ctor's closing note; DARM.md §3.3 items 8, 10..13, 16): the family's
        // first motions, MatMode(B, 0), CamIdx = 1 @ 0.025 [approx: the header], flag227 = 0, flag215 = 1, the intro
        // cameras 7 / 8 / 9 (eye / target fixed, the fov). [H] the arms and the floor start on their first section
        // (DARM_A §A.4 for the floor's "a"). [not modelled] the grey player tint, the SE preload.
        meshes.set_clip_index(boss8_name[0], boss8_sect[0][0], 0); bm->time_scale = 1.0f;
        meshes.set_motion(boss8_name[1], "01_NormalWait", false, 0);
        meshes.set_motion(boss8_name[2], "03_Wait", false, 0);
        for (int i : {3, 4}) if (mo[i]->sect < 0 && !mo[i]->clips.empty()) meshes.set_clip_index(boss8_name[i], 0, 0);
        boss8_set_matmode(0);
        tmdl->rest_pose();
        boss8_tpose.reset(*tmdl);                          // FUN_005f3c70's node records; the eight records are the module's
        boss8_trk_root.fill(-1);
        boss8_cam_scripted = false;
        game.g_flags[227] = 0; game.g_flags[215] = 1;
        if (g_rig.ok())
            for (int k = 0; k < 3; ++k) {
                CameraRegion& s = g_rig.edit_slot(7 + k);
                s.eye.mode = 0; s.target.mode = 0;
                s.eye.fixed_pos = boss08::kIntroCams[k].eye; s.target.fixed_pos = boss08::kIntroCams[k].target;
                s.fov = boss08::kIntroCams[k].fov;
            }
        std::printf("  [boss08] Darm: core HP %.0f STR %.0f DEF %.0f, parts DEF %.0f pool %.0f\n", boss8->mesh(boss08::kW).hp,
                    boss8->mesh(boss08::kB).str, boss8->mesh(boss08::kB).def, boss8->part(0).def, boss8->part(0).pool);
    }
    Boss08Mesh &B = boss8->mesh(boss08::kB), &Tm = boss8->mesh(boss08::kT), &Wm = boss8->mesh(boss08::kW), &Am = boss8->mesh(boss08::kA);
    const bool on = game.g_flags[156] != 0;
    // The ctor's CamIdx = 1 @ 0.025 [approx: the header]: held until the fight starts, unless a script (the intro)
    // forces a slot of its own — the view's scene start drops any forced slot one step after this host was built.
    if (!boss8_started && g_rig.ok()) {
        if (g_rig.forced() >= 0 && g_rig.forced() != 1) boss8_cam_scripted = true;
        else if (g_rig.forced() < 0 && !boss8_cam_scripted) g_rig.write_forced(1, 0.025f);
    }
    if (on && !boss8_started) {
        boss8_started = true;
        std::printf("  [boss08] t%llu the fight starts, the body at (%.2f,%.2f,%.2f)\n", T, B.pos.x, B.pos.y, B.pos.z);
    }
    // The MeshObjs are the object records for what a script writes: the alpha stepper's +0x418 / +0x41c of the body
    // and the tail (the intro's fade-in), the core's while the fight is off.
    B.alpha = bm->alpha; B.alpha_target = bm->alpha_target;
    Tm.alpha = mo[1]->alpha; Tm.alpha_target = mo[1]->alpha_target;
    for (RehitBank& b : boss8_banks) b.tick();
    Boss08Input in;
    in.flag156 = on;
    in.flag227 = game.g_flags[227];
    in.player_pos = {feet.x, feet.y + kOriginH, feet.z};
    in.player_hp = in.player_hp_mirror = float(player.hp);
    in.player_grounded = pv.grounded;
    in.player_section = avm.section();
    for (const auto& bl : bullets.list()) in.bits_deployed += !bl->destroyed && bl->s.kind == boss08::kBitKind;
    auto sect_of = [&](int i, int def) {                   // the mesh's clip index -> the module's section
        const std::vector<int>& sm = boss8_sect[std::size_t(i)];
        for (std::size_t c = 0; c < sm.size(); ++c) if (sm[c] == mo[i]->sect) return int(c);
        return def;
    };
    in.b_section = sect_of(0, 0); in.b_count = bm->count; in.b_frame = bm->time;
    in.t_section = sect_of(1, 0); in.t_count = mo[1]->count; in.t_frame = mo[1]->time;
    in.w_section = sect_of(2, 2);
    in.y_section = sect_of(4, 0); in.y_count = mo[4]->count;
    auto pose = [&](Model& m, const MeshObj& o) {
        if (o.sect >= 0 && std::size_t(o.sect) < o.clips.size())
            for (const AnimClip& c : m.clips)
                if (c.name == o.clips[std::size_t(o.sect)].name) { m.pose(c, o.time); return; }
        m.rest_pose();
    };
    // B: the node records under B+0x20 (the placement of the last tick: the module moves it below).
    pose(*bmdl, *bm);
    const Mat4 bplace = boss8_m4(boss8->matrix());
    for (int k = 0; k < boss08::kBodyNodes; ++k) {
        const int ni = boss8_bnode[std::size_t(k)];
        const Mat4 w = ni >= 0 ? bmdl->nodes[std::size_t(ni)].world * bplace : bplace;
        boss8_bnode_m[std::size_t(k)] = to_m4(w);
        in.b_node[std::size_t(k)] = {w.m[12], w.m[13], w.m[14]};
    }
    boss8_body_m = boss8->matrix();
    const Mat4 skirt = boss8_bnode[boss08::kSkirt1] >= 0 ? bmdl->nodes[std::size_t(boss8_bnode[boss08::kSkirt1])].world : Mat4::identity();
    // T: T+0x20..0x5f = the world matrix of B's Bone_Skirt1 (DARM_A §A.1.5); its node records, the override tracks.
    const Mat4 tplace = skirt * bplace;
    boss8_pose_tail(*tmdl);
    for (int k = 0; k < boss08::kTailNodes; ++k) {
        const int ni = boss8_tnode[std::size_t(k)];
        const Mat4 w = ni >= 0 ? tmdl->nodes[std::size_t(ni)].world * tplace : tplace;
        boss8_tnode_m[std::size_t(k)] = to_m4(w);
        // NR+0x130: the node's snapshot (blend-from) local translation (DARM_TRACKS §1, §5).
        if (ni >= 0 && std::size_t(ni) < boss8_tpose.recs.size()) in.t_local[std::size_t(k)] = boss8_tpose.recs[std::size_t(ni)].snap_t;
    }
    const Mat4 eye_l = boss8_tnode[boss08::kTNodeEye] >= 0 ? tmdl->nodes[std::size_t(boss8_tnode[boss08::kTNodeEye])].world : Mat4::identity();
    in.t_eye = {boss8_tnode_m[boss08::kTNodeEye].m[3][0], boss8_tnode_m[boss08::kTNodeEye].m[3][1], boss8_tnode_m[boss08::kTNodeEye].m[3][2]};
    // W: its placement is its position (in OnBack the world matrix of T's Bone_eye); OraMarker under it.
    auto wplace_of = [&](const Mat4& tpl) {
        if (boss8->body().form == boss08::kOnBack) return eye_l * tpl;
        Mat4 m = Mat4::identity();
        m.m[12] = Wm.pos.x; m.m[13] = Wm.pos.y; m.m[14] = Wm.pos.z;
        return m;
    };
    pose(boss8_w_mdl, *mo[2]);
    {
        const Mat4 wp = wplace_of(tplace);
        const Mat4 w = boss8_w_ora >= 0 ? boss8_w_mdl.nodes[std::size_t(boss8_w_ora)].world * wp : wp;
        boss8_ora_m = to_m4(w);
        in.w_ora = {w.m[12], w.m[13], w.m[14]};
    }
    in.rng = [&] { return pick_rng.next(); };
    const Boss08BodyBlock b0 = boss8->body();
    const Boss08CoreBlock w0 = boss8->core();
    const float hp0 = Wm.hp;
    bool owns0[boss08::kTracks];
    for (int i = 0; i < boss08::kTracks; ++i) owns0[i] = boss8->track(i).owns;
    const Boss08Output out = boss8->tick(in);
    boss8_apply(out, mo);
    boss8_statics = boss8->statics();
    // The tail's node records (DARM_TRACKS §2, §4). A blended SetClip of the tail snapshots its main-owned nodes
    // (FUN_005f5c80 -> FUN_005f6400(M, -1)); a bind paints the named node's sub-tree and snapshots it (the main
    // clip's last sample), a release snapshots the track's nodes (the override's last sample); a releasing record
    // whose weight reached 0 in this tick's step hands its sub-tree back.
    for (const Boss08Clip& c : out.clips) if (c.mesh == boss08::kT) boss8_tpose.snapshot(-1);
    for (const Boss08Motion& mn : out.motions) if (mn.mesh == boss08::kT && mn.flag) boss8_tpose.snapshot(-1);
    for (const Boss08TrackEvent& te : out.tracks) {
        if (te.track < 0 || te.track >= boss08::kTracks) continue;
        if (te.bind) {
            int root = -1;
            for (std::size_t n = 0; n < tmdl->nodes.size(); ++n) if (boss8_lw(tmdl->nodes[n].name) == boss8_lw(te.node)) { root = int(n); break; }
            boss8_trk_root[std::size_t(te.track)] = root;
            boss8_tpose.paint(*tmdl, root, te.track);
        }
        boss8_tpose.snapshot(te.track);
    }
    for (int i = 0; i < boss08::kTracks; ++i)
        if (owns0[i] && !boss8->track(i).owns) boss8_tpose.paint(*tmdl, boss8_trk_root[std::size_t(i)], -1);
    // The placements: B+0x20 = YawPitchRoll(yaw, 0, 0) + pos, T glued to Bone_Skirt1, W at its position (the eye in
    // OnBack), A = RotationYawPitchRoll(yaw, 0, 0) + pos.
    {
        const Mat4 bw = boss8_m4(boss8->matrix());
        const Mat4 tw = skirt * bw;
        const Mat4 ww = wplace_of(tw);
        ef2sim::M4 am = ef2sim::rot_ypr(Am.yaw, 0.0f, 0.0f);
        am.m[3][0] = Am.pos.x; am.m[3][1] = Am.pos.y; am.m[3][2] = Am.pos.z;
        const Mat4 world[4] = {bw, tw, ww, boss8_m4(am)};
        for (int i = 0; i < 4; ++i) {
            const Vec3 p{world[i].m[12], world[i].m[13], world[i].m[14]};
            if (!mo[i]->has_world || mo[i]->world.m != world[i].m) mo[i]->dirty_pose = true;
            mo[i]->pos = p; mo[i]->dest = p; mo[i]->speed = 0; mo[i]->has_world = true; mo[i]->world = world[i];
            mo[i]->twin_clips = true;                      // the hull's bones follow the clip (FUN_00502fe0)
        }
        boss8_owned = true;
    }
    // The alphas: the body's and the tail's are the module's only once it wrote them (the death's fade); the core's
    // from the fight's start (W+0x418 = db(W)+0x1b0); the arms' always (db(A)+0x1c8).
    auto set_alpha = [&](int i, float a) {
        if (a != mo[i]->alpha) meshes.alpha_current(boss8_name[i], a);
        mo[i]->alpha_target = a;
    };
    if (B.alpha != bm->alpha) set_alpha(0, B.alpha);
    if (Tm.alpha != mo[1]->alpha) set_alpha(1, std::clamp(Tm.alpha, 0.0f, 1.0f));
    if (on) set_alpha(2, Wm.alpha);
    set_alpha(3, Am.alpha);
    // Every collision entry of B +0x89 (the OnBack window: the body's hull is off).
    if (bm->no_collide == B.collide) { bm->no_collide = !B.collide; bm->dirty_pose = true; }
    // The body's material alphas (DARM_A §A.1.1 block 3), per YMO material.
    {
        std::vector<float> v(bmdl->materials.size(), 1.0f);
        for (int k = 0; k < boss08::kBodyMats; ++k)
            if (boss8_mat_id[std::size_t(k)] >= 0) v[std::size_t(boss8_mat_id[std::size_t(k)])] = boss8->mat_alpha()[std::size_t(k)];
        if (v != boss8_mat || !room.mat_alpha.count(boss8_name[0])) {
            boss8_mat = v;
            room.mat_alpha[boss8_name[0]] = std::move(v);
            bm->dirty_alpha = true;
        }
    }
    // ... and the body's and the tail's material RGB (MatMode; a scene reload drops the room's copy).
    for (int i = 0; i < 2; ++i)
        if (!boss8_rgb[std::size_t(i)].empty() && (boss8_rgb_dirty || !room.mat_rgb.count(boss8_name[i]))) {
            room.mat_rgb[boss8_name[i]] = boss8_rgb[std::size_t(i)];
            mo[i]->dirty_alpha = true;
        }
    boss8_rgb_dirty = false;
    // The touch scripts: the body's and the tail's "Touch on", the sixth mesh's from its creation.
    if (on) {
        if (B.touch) boss8_touch(boss8_name[0]);
        if (Tm.touch) boss8_touch(boss8_name[1]);
    }
    if (boss8_mgcra) {
        if (MeshObj* mg = meshes.find("bs08_mgcra")) {
            if (mg->sect < 0 && !mg->clips.empty()) meshes.set_clip_index("bs08_mgcra", 0, 0);   // [H] its one loop clip runs
            boss8_touch("bs08_mgcra");
        }
    }
    // The run's facts, once each as they happen (the drive case reads them).
    const Boss08BodyBlock& b = boss8->body();
    const Boss08CoreBlock& w = boss8->core();
    static const char* const kForm[4] = {"Normal", "Low", "High", "OnBack"};
    static const char* const kWeak[3] = {"the belly", "the left leg", "the right leg"};
    if (b.form != b0.form) std::printf("  [boss08] t%llu form %s -> %s (weak point: %s)\n", T, kForm[std::clamp(b0.form, 0, 3)], kForm[std::clamp(b.form, 0, 3)], kWeak[std::clamp(w.weak, 0, 2)]);
    if (b.settled && !b0.settled) std::printf("  [boss08] t%llu settled in %s\n", T, kForm[std::clamp(b.form, 0, 3)]);
    if (b.down && !b0.down) std::printf("  [boss08] t%llu DOWN: the core is exposed (weak point %s)\n", T, kWeak[std::clamp(w.weak, 0, 2)]);
    if (w0.exposure != 0 && w.exposure == 0 && b.form != boss08::kOnBack) std::printf("  [boss08] t%llu the exposure ends, core hp %.0f / %.0f\n", T, Wm.hp, Wm.max_hp);
    if (w.band != w0.band) std::printf("  [boss08] t%llu band %u (the refill starts at hp %.0f)\n", T, unsigned(w.band), Wm.hp);
    if (w0.refill && !w.refill) std::printf("  [boss08] t%llu the refill ends at hp %.0f\n", T, Wm.hp);
    if (b0.window == 0 && b.window != 0 && !b.dead) std::printf("  [boss08] t%llu the barrier breaks (window %u)\n", T, unsigned(b.window));
    if (b0.window != 0 && b.window == 0) std::printf("  [boss08] t%llu the window closed: the barrier is back\n", T);
    if (w0.h_barrier == 0 && w.h_barrier != 0) std::printf("  [boss08] t%llu the barrier is up\n", T);
    if (b.beat_l == 1 && b0.beat_l == 0) std::printf("  [boss08] t%llu the left arm beats\n", T);
    if (b.beat_r == 1 && b0.beat_r == 0) std::printf("  [boss08] t%llu the right arm beats\n", T);
    if (b.clap == 1 && b0.clap == 0) std::printf("  [boss08] t%llu the arms clap\n", T);
    if (b.fall != 0 && b0.fall == 0) std::printf("  [boss08] t%llu the fall sequence\n", T);
    if (b.dead && !b0.dead) std::printf("  [boss08] t%llu the kill (core hp %.0f -> 0)\n", T, hp0);
    if (b.death_started && !b0.death_started) std::printf("  [boss08] t%llu the death timeline starts\n", T);
    for (const Boss08Clip& c : out.clips)
        if (c.mesh == boss08::kB && (boss8_log || c.section >= 36))
            std::printf("  [boss08] t%llu clip %s\n", T, boss08::kBodyClipTable[std::clamp(c.section, 0, boss08::kBodyClips - 1)].name);
        else if (c.mesh == boss08::kT && boss8_log)
            std::printf("  [boss08] t%llu tail clip %s\n", T, boss08::kTailClipTable[std::clamp(c.section, 0, boss08::kTailClips - 1)].name);
    for (const Boss08Motion& mn : out.motions)
        if (boss8_log) std::printf("  [boss08] t%llu motion %s %s\n", T, boss8_name[std::clamp(mn.mesh, 0, 4)], mn.name);
    for (const Boss08TrackEvent& te : out.tracks)
        if (boss8_log) std::printf("  [boss08] t%llu track %d %s %s%s\n", T, te.track, te.bind ? "bound to" : "released from", te.node,
                                   !te.bind ? "" : te.section == 19 ? " (20_Beat)" : te.section == 23 ? " (24_StabInTheGround)" : " (25_PullFromTheGround)");
    if (boss8_log && tick_count % 30 == 0)
        std::printf("  [boss08] t%llu form %s%s%s cd %u B %s f %.2f T %s Y %d/%u | limbs %.2f/%.2f/%.2f (t %.2f/%.2f/%.2f) weak %d exp %u seq %d | "
                    "core hp %.0f band %u (%.1f,%.1f,%.1f) a %.2f | win %u bar %u | B (%.1f,%.1f,%.1f) yaw %.2f | feet (%.2f,%.2f,%.2f) sec 0x%x php %d slot %d\n",
                    T, kForm[std::clamp(b.form, 0, 3)], b.settled ? "" : " (moving)", b.down ? " DOWN" : "", unsigned(b.cooldown),
                    boss08::kBodyClipTable[std::clamp(in.b_section, 0, boss08::kBodyClips - 1)].name, double(in.b_frame),
                    boss08::kTailClipTable[std::clamp(in.t_section, 0, boss08::kTailClips - 1)].name, in.y_section, unsigned(in.y_count),
                    b.chest, b.leg_l, b.leg_r, b.chest_t, b.leg_l_t, b.leg_r_t, w.weak, unsigned(w.exposure), w.seq, Wm.hp, unsigned(w.band),
                    Wm.pos.x, Wm.pos.y, Wm.pos.z, w.alpha, unsigned(b.window), unsigned(b.barrier_hits), B.pos.x, B.pos.y, B.pos.z, b.yaw,
                    feet.x, feet.y, feet.z, unsigned(avm.section()), player.hp, g_rig.current());
    if (boss8_pilot) boss8_pilot();
};
// The three world-anchored mini gauges (0x76c1cc / 0x76c1e4 / 0x76c1fc; DARM_D §2.1, §4.1, §4.2): the chest group or
// the barrier, the left leg, the right leg. Run after boss7_gauges (it clears the bars). [approx] see the header.
auto boss8_gauges = [&](const Mat4& vp, std::uint32_t cw, std::uint32_t ch) {
    if (!boss8 || game.g_flags[156] == 0) return;
    for (int i = 0; i < 3; ++i) {
        const Boss08MiniGauge& mg = boss8->mini(i);
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
// drive `boss8`: the fight as a player fights it, from then on every tick (`boss8=off` ends it). The pilot only ever
// (a) puts the player ON THE ARENA FLOOR, while grounded, at a spot beside its target a player would run to — on a
// resting floor tile away from the rim, clear of the body's and the tail's hulls and of the body's push-out — facing
// the target, and (b) presses Attack / Jump / the stick through the normal input path. It never lifts the player and
// never touches Darm; what it knows is what a player sees (the form, which limbs have faded, the core once it is
// out, the barrier).
//   forms Normal / Low / High   break the limbs: the left leg (parts 4 / 5), then the right leg (6 / 7), then the
//             chest and skirt (0..3), each until it has faded away (its gauge is empty) — whichever of them holds the
//             core brings Darm DOWN. Of a group's parts the lowest one that can be got at is struck: from the floor
//             beside it when a grounded swing reaches, else from a jump under it (the stick held toward it, Attack
//             in the air; the second jump near the top while still short).
//   DOWN      the core sinks to y 1.5 for 600 frames: stand beside it and swing (jump at it while it is still high).
//   a form change   the floor re-arranges: keep to a resting tile, step to the nearest one when it starts to move.
//   OnBack    the core rides the tail's eye behind the barrier: 60 hits open it (each worth 1), then the open window.
// It does nothing while the player falls (the rim, the 220-frame fall sequence).
bool b8p_on = false;
int b8p_cool = 0;                                          // ticks until its next press on the ground
int b8p_air = -1;                                          // ticks since the pilot's jump press (-1 = not its jump)
bool b8p_second = false;                                   // its second jump is spent
int b8p_group = 1;                                         // the limb group it is working on (1 left leg, 2 right leg, 0 chest)
int b8p_tgt = -1;                                          // the part it is at
bool b8p_done[3] = {};                                     // groups it emptied in this form without a DOWN: not the core's
int b8p_last = -1;                                         // the plan last printed
float b8p_floor = -1e9f;                                   // the height the floor's tiles rest at, as last stood on
constexpr float kB8pJump = 2.3f;                           // how far a jump lifts the origin (a single jump tops 2.6 up)
boss8_pilot = [&] {
    if (!b8p_on) return;
    if (!boss8 || game.g_flags[156] != 1 || csMgr.world_frozen() || player.hp < 1) { b8p_air = -1; return; }
    const Boss08BodyBlock& b = boss8->body();
    const Boss08CoreBlock& w = boss8->core();
    if (b.dead || b.fall != 0) { b8p_air = -1; return; }   // dying; the fall sequence
    const unsigned long long T = (unsigned long long)tick_count;
    const RoomMesh& cm = phys_mesh(room);
    // The arena floor's own height at (x, z): the tiles of s7099yuka (not a hull, not the wheel's blades).
    const RoomMesh::ObjRange* yr = cm.object_range(boss8_name[4]);
    auto floor_y = [&](float x, float z, float* y) {
        Vec3 h{};
        if (!yr || !segment_hits_tris(cm, {x, 3.0f, z}, {x, -3.0f, z}, yr->t0, yr->t1, &h)) return false;
        *y = h.y;
        return true;
    };
    {   // where the tiles rest in this form: what the player stands on once the floor has settled
        float y = 0.0f;
        if (pv.grounded && b.settled && floor_y(feet.x, feet.z, &y) && std::fabs(y - feet.y) < 0.05f) b8p_floor = y;
    }
    auto face = [&](float dx, float dz) {
        const float l = std::sqrt(dx * dx + dz * dz);
        if (!(l > 1e-3f)) return;
        face_dir = {dx / l, 0.0f, dz / l};
        facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
    };
    const Vec3 pc = ef2sim::transform_coord({0.0f, 0.0f, 1.5f}, boss8->matrix());   // the push-out's centre
    const float pr = (b.down ? 2.0f : 4.0f) + 0.3f;
    // A spot to stand on: a floor tile at rest, floor all round it (not the rim), outside the push-out, clear of the
    // hulls of the body and the tail.
    auto tile = [&](float x, float z) {
        float y = 0.0f;
        return floor_y(x, z, &y) && std::fabs(y - b8p_floor) < 0.12f;
    };
    auto spot_ok = [&](float x, float z) {
        if (!tile(x, z) || !tile(x + 1.0f, z) || !tile(x - 1.0f, z) || !tile(x, z + 1.0f) || !tile(x, z - 1.0f)) return false;
        if ((x - pc.x) * (x - pc.x) + (z - pc.z) * (z - pc.z) < pr * pr) return false;
        for (int i = 0; i < 2; ++i)
            if (const RoomMesh::ObjRange* r = cm.object_range(boss8_name[i]))
                if (r->t1 > r->t0 && sphere_overlaps_tris(cm, {x, b8p_floor + kOriginH, z}, kCharRadius + 0.3f, r->t0, r->t1)) return false;
        return true;
    };
    auto put = [&](float x, float z) { float y = b8p_floor; floor_y(x, z, &y); feet = {x, y, z}; };
    // The floor re-arranges under a form change (its one-shot clip, 640 frames; the body crosses the arena): keep to
    // a resting tile clear of the body — the nearest one when this one starts to move.
    if (!b.settled || w.exposure != 0) b8p_done[0] = b8p_done[1] = b8p_done[2] = false;
    if (!b.settled) {
        b8p_air = -1;
        if (!pv.grounded || spot_ok(feet.x, feet.z)) return;
        for (int r = 1; r <= 12; ++r)
            for (int k = 0; k < 12; ++k) {
                const float a = float(k) * 0.5235988f;
                const float x = feet.x + std::cos(a) * float(r), z = feet.z + std::sin(a) * float(r);
                if (!spot_ok(x, z)) continue;
                put(x, z);
                if (boss8_log) std::printf("  [pilot] t%llu the floor moves: over to (%.1f,%.1f)\n", T, x, z);
                return;
            }
        return;
    }
    auto open = [&](int i) {                               // a part a hit can count on: not sunk, not closed
        const Boss08Part& p = boss8->part(i);
        return p.pos.y > -500.0f && p.def < 999.0f;
    };
    if (!pv.grounded) {                                    // in the air: only its own jump is steered
        if (b8p_air < 0 || b8p_tgt < 0 || !open(b8p_tgt)) return;
        ++b8p_air;
        const Boss08Part& pt = boss8->part(b8p_tgt);
        const float px = pt.pos.x - feet.x, pz = pt.pos.z - feet.z, pl = std::sqrt(px * px + pz * pz);
        if (pl > 0.3f) { drive_wish = {px / pl, 0.0f, pz / pl}; drive_wish_n = 1; }
        const float up = pt.pos.y - (feet.y + kOriginH), reach = pt.radius + 0.6f;
        if (!b8p_second && pv.t <= 0.12f && std::sqrt(pl * pl + up * up) > reach) {   // near the first jump's top and still short: the second
            drive_jump = true; b8p_second = true;
            if (boss8_log) std::printf("  [pilot] t%llu second jump, feet y %.2f\n", T, feet.y);
        }
        if (std::sqrt(pl * pl + up * up) < reach + 0.3f && b8p_air % 5 == 0) try_attack();
        return;
    }
    if (b8p_air >= 0 && b8p_air < 4) { ++b8p_air; return; }   // its press is taking off
    b8p_air = -1;
    // How a part can be got at from the floor: where to stand (`here` = the player already does) and whether it
    // takes a jump. False = out of reach, or no spot round it right now (the hull, the rim, the push-out).
    struct Plan { bool jump = false, here = false; float x = 0, z = 0; };
    auto plan_for = [&](int i, Plan* out) {
        const Boss08Part& p = boss8->part(i);
        const float reach = p.radius + 0.6f;               // the sphere plus what a swing covers (the pilot's estimate)
        const float dy = p.pos.y - (b8p_floor + kOriginH);
        const float px = p.pos.x - feet.x, pz = p.pos.z - feet.z, d = std::sqrt(px * px + pz * pz);
        const float bx = d > 1e-3f ? -px / d : 0.0f, bz = d > 1e-3f ? -pz / d : -1.0f;   // the player's side of it first
        for (const bool jump : {false, true}) {            // from the floor when a grounded swing reaches; else, or with no spot for that, a jump
            float r0;
            if (!jump) {
                if (dy > reach - 0.3f) continue;
                r0 = std::max(0.3f, std::sqrt(reach * reach - dy * dy) - 0.4f);
            } else {
                const float up = std::max(0.0f, dy - kB8pJump);
                if (up > reach - 0.2f) continue;
                r0 = std::max(0.3f, std::sqrt(reach * reach - up * up) - 0.5f);
            }
            Plan pl;
            pl.jump = jump;
            if (d <= r0 + 0.3f && (jump || d >= r0 * 0.4f - 0.3f) && spot_ok(feet.x, feet.z)) { pl.here = true; pl.x = feet.x; pl.z = feet.z; *out = pl; return true; }
            for (const float h : {r0, r0 * 0.7f, r0 * 0.4f})
                for (int k = 0; k < 12; ++k) {             // 0, +30, -30, +60, .. degrees round the part
                    const float a = float((k + 1) / 2) * 0.5235988f * ((k & 1) ? 1.0f : -1.0f);
                    const float c = std::cos(a), sn = std::sin(a);
                    const float x = p.pos.x + (bx * c - bz * sn) * h, z = p.pos.z + (bx * sn + bz * c) * h;
                    if (!spot_ok(x, z)) continue;
                    pl.x = x; pl.z = z; *out = pl;
                    return true;
                }
        }
        return false;
    };
    // The target part.
    int tgt = -1;
    Plan plan;
    if (b.form == boss08::kOnBack || w.exposure != 0) {
        if (open(boss08::kBodyParts) && plan_for(boss08::kBodyParts, &plan)) tgt = boss08::kBodyParts;
    } else {
        // A group that faded away without bringing Darm down does not hold the core: it is left alone (it grows
        // back after 600 frames) until the next form.
        const float gauge[3] = {b.chest_t, b.leg_l_t, b.leg_r_t};
        for (int g = 0; g < 3; ++g) if (!(gauge[g] > 0.0f)) b8p_done[g] = true;
        if (b8p_done[0] && b8p_done[1] && b8p_done[2]) b8p_done[0] = b8p_done[1] = b8p_done[2] = false;
        static const int kFirst[3] = {0, 4, 6}, kCount[3] = {4, 2, 2};
        const int order[4] = {b8p_group, 1, 2, 0};
        for (int gi = 0; gi < 4 && tgt < 0; ++gi) {
            const int g = order[gi];
            if (b8p_done[g] || !(gauge[g] > 0.0f)) continue;
            bool tried[boss08::kBodyParts] = {};
            for (int n = 0; n <= kCount[g] && tgt < 0; ++n) {  // the part it is at, then the open ones, the lowest surface first
                int best = -1;
                if (n == 0) { if (b8p_tgt >= kFirst[g] && b8p_tgt < kFirst[g] + kCount[g] && open(b8p_tgt)) best = b8p_tgt; else continue; }
                else
                    for (int i = kFirst[g]; i < kFirst[g] + kCount[g]; ++i)
                        if (!tried[i] && open(i) && (best < 0 || boss8->part(i).pos.y - boss8->part(i).radius < boss8->part(best).pos.y - boss8->part(best).radius)) best = i;
                if (best < 0) break;
                tried[best] = true;
                if (plan_for(best, &plan)) { tgt = best; b8p_group = g; }
            }
        }
    }
    b8p_tgt = tgt;
    if (tgt < 0) {                                         // nothing to get at right now
        if (boss8_log && tick_count % 120 == 0) std::printf("  [pilot] t%llu waits (no part within reach from a clear spot)\n", T);
        return;
    }
    const Boss08Part& pt = boss8->part(tgt);
    if (!plan.here) put(plan.x, plan.z);
    face(pt.pos.x - feet.x, pt.pos.z - feet.z);
    const int key = tgt * 2 + (plan.jump ? 1 : 0);
    if (key != b8p_last) {
        b8p_last = key;
        std::printf("  [pilot] t%llu target %s %d %s, from (%.2f,%.2f)\n", T, tgt == boss08::kBodyParts ? "the core" : "part",
                    tgt == boss08::kBodyParts ? 0 : tgt, plan.jump ? "with a jump" : "from the floor", feet.x, feet.z);
    }
    if (b8p_cool > 0) { --b8p_cool; return; }
    if (plan.jump) {
        drive_jump = true; b8p_air = 0; b8p_second = false; b8p_cool = 4;
        return;
    }
    try_attack(); b8p_cool = 6;
};
boss8_drive = [&](const std::string& cmd) {
    if (cmd.rfind("boss8", 0) != 0) return false;
    b8p_on = cmd != "boss8=off";
    if (!b8p_on) b8p_air = -1;
    return true;
};
