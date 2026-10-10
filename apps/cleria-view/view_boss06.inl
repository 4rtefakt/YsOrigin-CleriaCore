// Zava (room S_6082): the two GoMeshes "boss06a" (Yogrcs, ice) / "boss06b" (Omrgan, fire) the room script
// Mesh_Locate's are driven by the Boss06 module (src/cleria/boss06.hpp) every live step, as boss01..05; Zava herself
// is the cast-0xad actor of the enemy loop (brains::m870, src/cleria/boss06_zava.hpp) with a Boss06Link hung on her
// AiEnv. Specs: build/boss_re/ZAVA.md (P1), ZAVA_AB.md, ZAVA_CG.md (slice G = the camera), ZAVA_D.md, ZAVA_EF.md,
// ZAVA_E2.md. Included after view_boss05.inl. Per frame: boss6_tick in step_meshes (head a, then b), the camera hook
// in update_camera (view_camera.inl), boss6_zava_env / boss6_zava_post round her enemy_ai_tick, boss6_zava_tail after
// the integrator, view_boss06_hit.inl at the player's swing, view_boss06_touch.inl in the hit phase (the touch push
// and the direct damage calls).
// [H] the heads tick before Zava within a frame (the engine's order between the GoMesh pool and the actor pool is
// not in the spec); what a head's move asks of her (the throw, the re-seat) reaches her after her own handler.
// [not modelled] FUN_0058a5f0's particle
// burst, FUN_00540600 (the player's fire against the ice pillars), the flash tint (ZAVA_CG §G.5), byte P+0x399 (taken as 0), the
// keep-out box FUN_0042dc90, FUN_00403cd0 (the kill record), f32 [0x6d6078] = 0.8 and byte [0x76a888] (ZAVA_AB G5),
// the touch on actors other than the player, the ground shadow quad (P1 §3.1).
constexpr int kB6Owner = -0xB06;                           // fx owner tag: the modules' effect handles
const char* const boss6_name[boss06::kHeads] = {"boss06a", "boss06b"};
const char* const boss6_ymo[boss06::kHeads] = {"BOSS06A.YMO", "BOSS06B.YMO"};
const char* const boss6_twin[boss06::kHeads] = {"BOSS06A_.YMO", "BOSS06B_.YMO"};
// The render models, posed per head for the node table (the files are under the room bake's 2 MB "big model" line,
// so its cache does not hold them: the host keeps its own, as boss02's).
std::array<Model, boss06::kHeads> boss6_mdl;
bool boss6_mdl_ok = false;
const char* const boss6_node_name[boss06::kNodes] = {"piyo_efc", "fire_efc", "body_efc", "eye_c_efc"};
std::array<std::array<int, boss06::kNodes>, boss06::kHeads> boss6_node{};          // node table -> the model's node
std::array<std::array<int, boss06::kSections>, boss06::kHeads> boss6_sect{};       // section -> the mesh's clip index
std::array<std::array<ef2sim::M4, boss06::kNodes>, boss06::kHeads> boss6_node_m{};      // attach parents: the nodes,
std::array<std::array<ef2sim::M4, boss06::kFxSlots>, boss06::kHeads> boss6_slot_m{};    // the FX slots (hb+0x140..),
std::array<std::array<ef2sim::M4, boss06::kOrbiters>, boss06::kHeads> boss6_orbit_m{};  // the orbiters (hb+0x2b0..)
ef2sim::M4 boss6_zava_floor_m;                             // 0x7ddcd0: her matrix with y forced to 6.05
std::array<Vec3, boss06::kHeads> boss6_wpos{}, boss6_wrot{};   // the pose last seen on / written to the MeshObj
std::array<float, boss06::kHeads> boss6_wat{}, boss6_was{};    // H+0x41c / H+0x420 as last applied
std::array<bool, boss06::kHeads> boss6_gone{};             // H+0x11 & 2 applied
std::array<bool, boss06::kHeads> boss6_owned{};            // the brain placed the mesh (MeshObj::has_world is ours)
std::array<std::vector<std::pair<std::uint32_t, std::uint32_t>>, boss06::kHeads> boss6_entries;   // twin mesh -> tris
std::array<bool, boss06::kHeads> boss6_entries_built{};
std::array<int, boss06::kHeads> boss6_move_log{};          // CLERIA_BOSSLOG: the last move printed
Boss06Input boss6_in;                                      // this frame's (the heads' tick, then her brain)
Boss06Zava boss6_z;                                        // the shared record (Z+0x7a8 in hits)
Boss06Link boss6_link;                                     // her link: the spline, the handles, the frame bookkeeping
Boss06Output boss6_zbo;                                    // her world requests of this frame (Boss06Link::out)
std::optional<Boss06ZavaThrow> boss6_throw;                // a head's move 8 / 12 events, until her next handler
bool boss6_reseat = false;
struct Boss6Damage { float atk; bool no_reaction; int head; };
std::vector<Boss6Damage> boss6_dmg;                        // FUN_004fd350 calls, applied in view_boss06_touch.inl
bool boss6_grab = false;                                   // a head holds the player (P+0x12c |= 0x101041, +0x60 & ~0x80)
int boss6_zava_id = 0;                                     // her instance id (0 = not seen)
bool boss6_zava_ran = false;                               // her handler ran this frame (the tail consumes it)
float boss6_zava_face = 0;                                 // her facing before this frame's mover
int boss6_zava_log = -1;                                   // CLERIA_BOSSLOG: phase * 100 + action last printed
std::function<void()> boss6_pilot;                         // drive boss6: the per-tick player pilot (below)
float boss6_cam_t = 0, boss6_cam_step = 0.005f, boss6_cam_from = 0;   // hbA+0x11c / +0x120 / +0x124 (ZAVA_CG §G.1)
static const bool boss6_log = std::getenv("CLERIA_BOSSLOG") != nullptr;
auto boss6_m4 = [](const ef2sim::M4& a) {
    Mat4 r;
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) r.m[std::size_t(i * 4 + j)] = a.m[i][j];
    return r;
};
auto boss6_veq = [](const Vec3& a, const Vec3& b) { return a.x == b.x && a.y == b.y && a.z == b.z; };
auto boss6_zava = [&]() -> Actor* {                       // FUN_005355e0: the first pool actor of cast 0xad
    for (Actor& a : world.actors()) if (a.cast_id == 0xad && !a.is_player() && !a.destroy_requested()) return &a;
    return nullptr;
};
// Her HP never ends her through the damage applier: the handler floors it at 1.0 and runs the death itself (ZAVA_AB
// §A.1 0x53af86). [approx] FUN_004fbb40's kill branch for such a victim is not traced: the view withholds its generic
// kill credit / puff / drops for her (her handler awards the EXP, FUN_004fa4a0) and lets her handler see HP < 1.
// The same before her INIT: a Chr_Locate'd cast 0xad has no ENEMYINFO stats (HP 0) until the brain writes them.
auto boss6_zava_undying = [&](const Actor& a) { return a.ai_index == 80 && a.cast_id == 0xad; };
// The fog record the fader reads / writes (ZAVA_CG §G.3: fogRecs + fogCur * 0x54 + 0x3c); -1 = none.
auto boss6_fog_rec = [&]() -> FogRegion* {
    const int k = fog_current(room, feet);
    return k >= 0 && std::size_t(k) < room.fog.regions.size() ? &room.fog.regions[std::size_t(k)] : nullptr;
};
auto boss6_player_in = [&](Boss06Input& in) {
    in.player = true;
    in.player_pos = {feet.x, feet.y + kOriginH, feet.z};
    in.player_hp = in.player_hp_mirror = float(player.hp);
    in.player_section = avm.section();
    in.player_guard = false;                               // [not modelled] byte P+0x399
    in.player_grounded = grounded;
    // P+0x540, the ground object. [H] every floor of the room is a scene object (the SOB's meshes; the view's
    // contact record names only the named ones), so a grounded player off the heads stands on "another object".
    in.player_floor = player_floor_obj == boss6_name[0] ? boss06::kFloorA
                    : player_floor_obj == boss6_name[1] ? boss06::kFloorB
                    : grounded ? boss06::kFloorOther : boss06::kFloorNone;
    in.player_r324 = kOriginH;
    in.player_vel = {pvel.x, 0.0f, pvel.z};                // P+0xa3c..0xa44 [approx] no y (as AiEnv::target_vel)
    in.pad_pressed = g_in.pressed != 0;                    // rec+0x24
    in.pad_x = g_in.dx; in.pad_y = g_in.dy;                // rec+0x2c / rec+0x30
    in.cam_eye = g_cam_out.eye; in.cam_target = g_cam_out.target;
    in.zoom_active = zb.running();                         // [0x76a550] != 0.0
    in.fog_colour.reset();
    if (const FogRegion* fr = boss6_fog_rec()) in.fog_colour = Vec3{fr->r, fr->g, fr->b};
    in.rng = [&] { return pick_rng.next(); };
    // FUN_00536010 (ZAVA_D §6.4): the terrain ray with the head's own mesh excluded (the other head is not).
    // [approx] collision sets 2 and 0 are the view's one physics mesh.
    in.probe = [&](int head, const Vec3& from, const Vec3& d) {
        const RoomMesh& cm = phys_mesh(room);
        const Vec3 to{from.x + d.x, from.y + d.y, from.z + d.z};
        const RoomMesh::ObjRange* r = cm.object_range(boss6_name[head & 1]);
        if (!r) return segment_blocked(cm, from, to);
        const std::uint32_t nt = std::uint32_t(cm.indices.size() / 3);
        return segment_hits_tris(cm, from, to, 0, r->t0) || segment_hits_tris(cm, from, to, r->t1, nt);
    };
};
// BossCam(cl m) = FUN_00545000 (ZAVA_CG §G.1): the director's selection and the slot presets of a mode.
auto boss6_cam = [&](int m) {
    if (!boss6) return;                                    // no "boss06a" / no block
    Boss06Head& ha = boss6->head(0);
    if (int(ha.cam_mode) == m) return;                     // already in this mode
    if (game.g_flags[130] == 0) return;                    // the mode byte is not written then
    ha.cam_mode = std::uint8_t(m);
    const int cur = g_rig.current();
    boss6_cam_t = 0.0f; boss6_cam_step = 0.005f; boss6_cam_from = g_rig.slot(cur).eye.dist;
    const Vec3 pa = boss6->head(0).pos, pb = boss6->head(1).pos;
    if (boss6_log) std::printf("  [boss06] t%llu BossCam %d (slot %d)\n", (unsigned long long)tick_count, m, cur);
    switch (m) {
        case 1: {                                          // the fight: slot 2, 30 deg, 35.0, fov 30 deg
            g_rig.write_sel(2, cur, 0.0f, 0.01f);
            CameraRegion& s = g_rig.edit_slot(2);
            s.eye.pitch = 0.52359879f; s.eye.dist = 35.0f; s.fov = 0.52359879f;
            break;
        }
        case 3: {                                          // the first mount: slot 4 takes the old slot's pitch / dist
            const float p = g_rig.slot(cur).eye.pitch, d = g_rig.slot(cur).eye.dist;
            CameraRegion& s = g_rig.edit_slot(4);
            s.eye.pitch = p; s.eye.dist = d;
            g_rig.write_sel(4, cur, 0.0f, 0.01f);
            break;
        }
        case 4: {                                          // riding: slot 5, 30 deg, 26.0
            g_rig.write_sel(5, cur, 0.0f, 0.025f);
            CameraRegion& s = g_rig.edit_slot(5);
            s.eye.pitch = 0.52359879f; s.eye.dist = 26.0f;
            break;
        }
        case 5: {                                          // (never passed) slot 6, 10 deg, 15.0, fov 44 deg
            g_rig.write_sel(6, cur, 0.0f, 0.015f);
            CameraRegion& s = g_rig.edit_slot(6);
            s.eye.pitch = 0.17453292f; s.eye.dist = 15.0f; s.fov = 0.767944872f;
            break;
        }
        case 6: {                                          // the death: slot 2 at once, 20 deg, round Zava at 20.0
            g_rig.write_sel(2, cur, 1.0f, 1.015f);
            CameraRegion& s = g_rig.edit_slot(2);
            s.eye.pitch = 0.34906584f; s.eye.dist = 30.0f;
            if (const Actor* zv = boss6_zava()) {
                s.target.fixed_pos = {zv->pos.x, zv->pos.y + zv->height, zv->pos.z};
                s.target.mode = 0; s.eye.dist = 20.0f;
            }
            break;
        }
        case 7: {                                          // the death, the heads: slot 3, both points computed once
            g_rig.write_sel(3, cur, 0.0f, 0.005f);
            CameraRegion& s = g_rig.edit_slot(3);
            const Vec3 ab{pb.x - pa.x, pb.y - pa.y, pb.z - pa.z};
            s.target.fixed_pos = {pa.x + ab.x * 0.3f, pa.y + ab.y * 0.3f, pa.z + ab.z * 0.3f};
            const float d = std::max(20.0f, 2.0f * std::sqrt(ab.x * ab.x + ab.y * ab.y + ab.z * ab.z));
            s.eye.fixed_pos = {-34.0f, 6.0f + 0.5f * d, 29.0f - d};
            break;
        }
        default: break;                                    // 0, 2: only the mode byte and the ease state
    }
};
// FUN_0042bb10's boss06 path and the hook FUN_005453d0 (ZAVA_CG §G.2), once per camera step: true = the step is
// claimed (the region pick is skipped). The fog faders of both heads tick from here (§G.3), b then a.
g_boss06_cam_hook = [&]() -> bool {
    if (game.g_flags[156] == 0 || !boss6) return false;
    const int k = g_rig.current();
    if (k == 0) return true;                               // claimed, nothing written
    if (k < 2 || k > 6) return false;
    if (game.g_flags[130] == 0) return true;
    CameraRegion& cam_s = g_rig.edit_slot(k);
    {
        FogRegion* fr = boss6_fog_rec();
        Vec3 col = fr ? Vec3{fr->r, fr->g, fr->b} : Vec3{};
        boss6->fog_tick(1, fr ? &col : nullptr);
        boss6->fog_tick(0, fr ? &col : nullptr);
        if (fr) { fr->r = col.x; fr->g = col.y; fr->b = col.z; }
    }
    cam_s.target.mode = 1; cam_s.eye.mode = 3;             // default: follow the look point, orbit eye
    const Vec3 p{feet.x, feet.y + kOriginH, feet.z};
    const Vec3 pa = boss6->head(0).pos, pb = boss6->head(1).pos;
    const Actor* zv = boss6_zava();
    const Vec3 zp = zv ? Vec3{zv->pos.x, zv->pos.y + zv->height, zv->pos.z} : Vec3{};
    auto xz2 = [](const Vec3& a, const Vec3& b) { return (a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z); };
    switch (boss6->head(0).cam_mode) {
        case 3:                                            // the first flight: a fixed eye, 70 % of the way to her
            if (zv) {
                cam_s.target.fixed_pos = {(zp.x - p.x) * 0.7f + p.x, (zp.y - p.y) * 0.7f + p.y, (zp.z - p.z) * 0.7f + p.z};
                cam_s.target.mode = 0;
                const float d = std::sqrt((p.x - zp.x) * (p.x - zp.x) + (p.y - zp.y) * (p.y - zp.y) + (p.z - zp.z) * (p.z - zp.z));
                cam_s.eye.fixed_pos = {-34.0f, 16.0f, 24.0f - std::max(25.0f, d)};
                cam_s.eye.mode = 0;
            }
            break;
        case 5:
            cam_s.target.fixed_pos = {(pa.x + pb.x + p.x) * 0.333333343f, (p.y + 16.0f) * 0.333333343f,
                                      (pb.z + pa.z + p.z) * 0.333333343f};
            cam_s.target.mode = 0;
            cam_s.eye.fixed_pos = {-34.0f, 14.0f, 17.0f};
            cam_s.eye.mode = 0;
            break;
        case 6: cam_s.target.mode = 0; break;              // the fixed target of BossCam(6), orbit eye
        case 7: cam_s.target.mode = 0; cam_s.eye.mode = 0; break;
        default: break;
    }
    if (boss6->head(0).cam_mode == 4 && zv) {              // the tail 0x5456a3 (the engine has no null test on Zava)
        const float m2 = std::max(xz2(p, zp), std::max(xz2(p, pa), xz2(p, pb)));
        const float D = std::min(38.0f, std::max(33.0f, std::sqrt(m2) * 2.79999995f));
        if (boss6_cam_t != 1.0f) {
            boss6_cam_t += std::min(1.0f, boss6_cam_step);
            if (boss6_cam_t >= 1.0f) boss6_cam_t = 1.0f;
        }
        const float e = boss6_cam_t * boss6_cam_t * (3.0f - 2.0f * boss6_cam_t);   // FUN_0042ca10 [DOC CAMERA_OPS: smoothstep]
        cam_s.eye.dist = boss6_cam_from + e * (D - boss6_cam_from);
    }
    return true;
};
struct Boss6HookGuard { ~Boss6HookGuard() { g_boss06_cam_hook = nullptr; } } boss6_hook_guard;   // the hook captures this scope
// A Boss06Output (the heads' tick, an accept-hit's feedback, or Zava's requests with head = -1), applied in order.
auto boss6_apply = [&](const Boss06Output& out) {
    Actor* zv = boss6_zava_id ? world.find(boss6_zava_id) : nullptr;
    for (int id : out.rumble) pad_rumble(id);
    for (const Boss06Clip& c : out.clips) {                // SetClip: restart, A+0x24 = 1.0 [not modelled] the blend flag
        if (c.head < 0 || c.head >= boss06::kHeads || c.section < 0 || c.section >= boss06::kSections) continue;
        const int idx = boss6_sect[std::size_t(c.head)][std::size_t(c.section)];
        if (idx < 0) continue;
        meshes.set_clip_index(boss6_name[c.head], idx, 0);
        if (MeshObj* bm = meshes.find(boss6_name[c.head])) bm->time_scale = 1.0f;
    }
    for (const Boss06Sound& sd : out.sounds) sfx_at(sd.id, sd.pos, kSfxAnimVol);
    for (int h : out.kill_handles)                         // one effect: +0x1d4 = 1
        for (auto& f : fxsim.effects()) if (f->owner == kB6Owner && f->tag == h) f->kill = true;
    for (int id : out.kill_ids)                            // FUN_00451730: every live effect of that id
        for (auto& f : fxsim.effects()) if (!f->dead && f->id == id) fxsim.kill_effect(f.get());
    for (const Boss06Effect& fe : out.effects) {
        const Ef2* ef = ef2_of(fe.id);
        if (!ef) continue;
        const bool hd = fe.head >= 0 && fe.head < boss06::kHeads;
        const std::size_t hi = std::size_t(hd ? fe.head : 0), pi = std::size_t(fe.parent_index & 3);
        const ef2sim::M4* parent = nullptr;
        switch (fe.parent) {
            case Boss06Parent::kNode: if (hd) parent = &boss6_node_m[hi][pi]; break;
            case Boss06Parent::kSlot: if (hd) parent = &boss6_slot_m[hi][pi]; break;
            case Boss06Parent::kOrbit: if (hd) parent = &boss6_orbit_m[hi][pi]; break;
            // [approx] actor_mats carry her origin only: a mode-1 attach (586, the big attack's 426) loses Z+0x20's
            // rotation rows.
            case Boss06Parent::kZava: if (zv) parent = &actor_mats[zv->instance_id]; break;
            case Boss06Parent::kZavaFloor: parent = &boss6_zava_floor_m; break;
            default: break;
        }
        ef2sim::SpawnParams sp;
        sp.pos = fe.pos; sp.rot = fe.rot; sp.scale = fe.scale;
        sp.power = fe.power; sp.knock = fe.knock; sp.rtype = fe.rtype;
        sp.atk = !fe.stat ? 0.0f : hd ? boss6->head(fe.head).str : (zv ? float(zv->atk) : 0.0f);
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
        if (fe.call == Boss06Call::kFx2) { fx->scale_x = fe.scale.x; fx->scale_y = fe.scale.y; fx->scale_z = fe.scale.z; }   // FUN_00450880 keeps it
        if (parent) { fx->local = local; fx->attach_parent = parent; fx->attach_mode = fe.at_mode == 1 ? 1 : 2; }
        if (fe.flag201) fx->set1 = true;                   // fx+0x201
        // [approx] fx+0x203 (drop the hit record at the first particle step, ZAVA_EF L3): disarmed at the spawn.
        if (fe.flag203) if (ef2sim::Particle* p = fxsim.first_particle(fx)) p->hit.armed = false;
        if (fe.handle) { fx->owner = kB6Owner; fx->tag = fe.handle; }
    }
    for (const Boss06Bullet& bb : out.bullets) {            // FUN_00593550 with the head as owner
        bullet::Spawn s = bullet::block_spawn(bb.kind, bb.effect, bb.pos, bb.dir, bb.f);
        s.owner = 0; s.owner_monster = true;
        s.owner_atk = bb.head >= 0 && bb.head < boss06::kHeads ? boss6->head(bb.head).str : 0.0f;
        bullet::Bullet* bl = bullets.spawn(s);
        if (bl && bl->fx && bb.flag201) bl->fx->set1 = true;
    }
    if (out.shake) { g_rig.set_shake_frames(out.shake->first); g_rig.set_shake_amp(out.shake->second); }
    if (out.zoom) zoom_blur(out.zoom->first, out.zoom->second);   // ZoomBlur (0x76a550 / 0x76a554)
    // [not modelled] out.bursts (FUN_0058a5f0), out.pillar_break_test (FUN_00540600, ZAVA_E2 §3.4).
    for (const auto& [fi, fv] : out.flags) {
        if (fi < 0 || fi >= 0x200) continue;               // 544..546 (dwords 0x76c19c..) lie past g_flags: [not modelled]
        if (game.g_flags[fi] != fv) std::printf("  [boss06] t%llu flag %d = %d\n", (unsigned long long)tick_count, fi, fv);
        game.g_flags[fi] = fv;
    }
    // The grab, every hold frame (ZAVA_EF §E1.1 step 5). [approx] P+0x12c |= 0x101041 is run as M470's hold (no
    // vertical integration, player_hold) and Actable off (byte P+0x9d0) as the section-4 force of each frame.
    if (out.hold) {
        const Boss06Hold& h = *out.hold;
        feet = {h.pos.x, h.pos.y - kOriginH, h.pos.z};
        player_kb = {}; pvel = {};
        last_floor = 6.0f;                                 // P+0x318
        pv.grounded = false; grounded = false;             // byte P+0x388 = 0
        face_dir = {std::cos(h.facing), 0.0f, std::sin(h.facing)};   // P+0x314
        facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
        if (h.section4) avm.set_section(4, /*force*/true);
        if (!boss6_grab) std::printf("  [boss06] t%llu head %c holds the player\n", (unsigned long long)tick_count, 'a' + (h.head & 1));
        boss6_grab = true; player_hold = true;
    }
    if (out.release) {                                     // step 6: spat out
        const Boss06Release& r = *out.release;
        boss6_grab = false; player_hold = false;
        feet = {r.pos.x, r.pos.y - kOriginH, r.pos.z};
        if (r.vel) {                                       // [approx] the view's knock channel 1 has no y
            player_kb.x = r.vel->x; player_kb.z = r.vel->z;
            avm.set_section(9);                            // FUN_00446550(9, 0)
        }
        std::printf("  [boss06] t%llu head %c lets the player go\n", (unsigned long long)tick_count, 'a' + (r.head & 1));
    }
    if (out.player_free) { boss6_grab = false; player_hold = false; }
    if (out.player_vel) { player_kb.x = out.player_vel->x; player_kb.z = out.player_vel->z; }   // an assignment [approx] no y
    for (const Boss06Damage& d : out.damage) boss6_dmg.push_back({d.atk, true, d.head});   // [not modelled] P+0x12c |= 0x40 (the burn)
    if (out.surface_ice) cur_surface = kSurfaceIce;        // byte P+0x4b9 = 0x0b: the next move reads it
    for (int i = 0; i < boss06::kHeads; ++i)               // move 15: the 5 cached materials' rgb = 0 (P1 §3.2 item 6)
        if (out.materials_black[std::size_t(i)])
            for (const char* mt : {"yog_tex02", "yog_tex03", i == 0 ? "yog_parts" : "omr_tex00", i == 0 ? "yog_efc" : "orm_efc",
                                   i == 0 ? "yog_efc01" : "orm_efc01"})
                meshes.set_material_color(boss6_name[i], mt, {0.0f, 0.0f, 0.0f});
    if (out.zava_throw) boss6_throw = out.zava_throw;
    if (out.zava_reseat) boss6_reseat = true;
};
// The collision twin's meshes in the bake's order: one collision entry each (obj+0x25c; view_boss01_touch.inl has
// the walk). A mismatch with the baked object leaves one entry 0 over the whole hull.
auto boss6_build_entries = [&](int i) {
    boss6_entries_built[std::size_t(i)] = true;
    auto& ents = boss6_entries[std::size_t(i)];
    ents.clear();
    const RoomMesh::ObjRange* br = phys_mesh(room).object_range(boss6_name[i]);
    if (!br) return;
    Model twin;
    bool tw = false;
    try {
        twin = Model::parse(read_bytes(asset_root / "ENEMY" / "BOSS06" / boss6_twin[i]));
        twin.resolve_hierarchy();
        tw = true;
    } catch (const std::exception&) {}
    std::uint32_t t = 0;
    if (tw)
        for (const Node& n : twin.nodes) {
            if (n.mesh_index < 0) continue;
            const Mesh& me = twin.meshes[std::size_t(n.mesh_index)];
            if (me.vbs.empty() || me.ibs.empty()) continue;
            std::uint32_t nt = 0;
            const std::size_t ni = me.ibs[0].idx.size();
            if (!me.submeshes.empty()) {
                for (const Submesh& sm : me.submeshes)
                    for (std::uint32_t j = 0; j < sm.tri_count * 3; ++j) nt += std::size_t(sm.first_tri) * 3 + j < ni ? 1 : 0;
            } else {
                nt = std::uint32_t(ni);
            }
            nt /= 3;
            ents.push_back({t, t + nt});
            t += nt;
        }
    if (t != br->t1 - br->t0) {
        std::printf("  [boss06] %s: the twin's %u tris do not match the baked hull's %u: one entry\n", boss6_name[i], t,
                    br->t1 - br->t0);
        ents.assign(1, {0u, br->t1 - br->t0});
    } else if (boss6_log) {
        std::printf("  [boss06] %s: %zu collision entries, %u tris\n", boss6_name[i], ents.size(), t);
    }
};
auto boss6_tick = [&] {
    MeshObj* bm[boss06::kHeads] = {meshes.find(boss6_name[0]), meshes.find(boss6_name[1])};
    if (!bm[0] || !bm[1]) {
        if (boss6) { boss6.reset(); boss6_grab = false; boss6_throw.reset(); boss6_reseat = false; boss6_dmg.clear(); }
        boss6_entries_built.fill(false);
        return;
    }
    // A room (re)load re-created the meshes under a running fight (a Retry: the root script locates them again
    // before a step sees them gone): a mesh the brain had placed lost its world matrix. The heads start over.
    if (boss6 && ((boss6_owned[0] && !bm[0]->has_world) || (boss6_owned[1] && !bm[1]->has_world))) {
        boss6.reset(); boss6_grab = false; boss6_throw.reset(); boss6_reseat = false; boss6_dmg.clear();
    }
    if (!boss6_mdl_ok) {
        try {
            for (int i = 0; i < boss06::kHeads; ++i) {
                boss6_mdl[std::size_t(i)] = Model::parse(read_bytes(asset_root / "ENEMY" / "BOSS06" / boss6_ymo[i]));
                boss6_mdl[std::size_t(i)].resolve_hierarchy();
            }
            boss6_mdl_ok = true;
        } catch (const std::exception&) { return; }
    }
    Model* mdl[boss06::kHeads] = {&boss6_mdl[0], &boss6_mdl[1]};
    auto lw = [](std::string t) { for (char& ch : t) ch = char(std::tolower((unsigned char)ch)); return t; };
    if (!boss6) {
        Boss06Setup su;
        su.diff_index = game.g_flags[144];
        su.diff_easy = game.g_flags[143] != 0;
        su.diff_very_easy = game.g_flags[203] != 0;
        su.character = character;
        su.pos_a = bm[0]->pos; su.pos_b = bm[1]->pos;
        boss6 = std::make_unique<Boss06>(su);
        boss6_grab = false; boss6_throw.reset(); boss6_reseat = false; boss6_dmg.clear();
        boss6_z = Boss06Zava{};
        boss6_cam_t = 0.0f; boss6_cam_step = 0.005f; boss6_cam_from = 0.0f;
        for (int i = 0; i < boss06::kHeads; ++i) {
            const std::size_t u = std::size_t(i);
            for (int k = 0; k < boss06::kNodes; ++k) {
                boss6_node[u][std::size_t(k)] = -1;
                for (std::size_t n = 0; n < mdl[i]->nodes.size(); ++n)
                    if (lw(mdl[i]->nodes[n].name) == boss6_node_name[k]) { boss6_node[u][std::size_t(k)] = int(n); break; }
                // boss06b has no eye_c_efc (hb+0x20 = 0): model node 0 stands in (ZAVA_EF §F.2 case 270).
                if (boss6_node[u][std::size_t(k)] < 0 && !(i == 1 && k == boss06::kEyeCEfc))
                    std::printf("  [boss06] %s: no node %s\n", boss6_name[i], boss6_node_name[k]);
            }
            for (int c = 0; c < boss06::kSections; ++c) {
                boss6_sect[u][std::size_t(c)] = -1;
                for (std::size_t s = 0; s < bm[i]->clips.size(); ++s)
                    if (lw(bm[i]->clips[s].name) == lw(boss06::kSectionTable[c].name)) { boss6_sect[u][std::size_t(c)] = int(s); break; }
                if (boss6_sect[u][std::size_t(c)] != c)
                    std::printf("  [boss06] %s: clip %s -> section %d\n", boss6_name[i], boss06::kSectionTable[c].name,
                                boss6_sect[u][std::size_t(c)]);
            }
            // The ctor forces y = 6.1 (obj+0x54, P1 §3.2 item 11); the pose is the script's until the brain moves it.
            if (bm[i]->pos.y != boss6->head(i).pos.y) { bm[i]->pos.y = boss6->head(i).pos.y; bm[i]->dirty_pos = true; }
            boss6->head(i).rot = bm[i]->rot;
            boss6_wpos[u] = bm[i]->pos; boss6_wrot[u] = bm[i]->rot;
            boss6_wat[u] = boss6->head(i).alpha_target; boss6_was[u] = boss6->head(i).alpha_step;
            boss6_gone[u] = false; boss6_owned[u] = false; boss6_move_log[u] = 0;
            boss6_entries_built[u] = false;
        }
        std::printf("  [boss06] Zava's heads: a (%.2f,%.2f,%.2f) b (%.2f,%.2f,%.2f), HP %.0f STR %.0f DEF %.0f, level %d\n",
                    bm[0]->pos.x, bm[0]->pos.y, bm[0]->pos.z, bm[1]->pos.x, bm[1]->pos.y, bm[1]->pos.z, boss6->head(0).hp,
                    boss6->head(0).str, boss6->head(0).def, boss6->diff_level());
    }
    Boss06Input& in = boss6_in;
    boss6_player_in(in);
    for (int i = 0; i < boss06::kHeads; ++i) {
        const std::size_t u = std::size_t(i);
        Boss06Head& hb = boss6->head(i);
        // A script moved / turned the mesh (SUMMON's Mesh_SetPosition / Mesh_MoveTo / Mesh_SetRotation): the object's
        // +0x50 and its rotation rows are the one record the brain reads.
        const bool moved = !boss6_veq(bm[i]->pos, boss6_wpos[u]), turned = !boss6_veq(bm[i]->rot, boss6_wrot[u]);
        if (moved) { hb.pos = bm[i]->pos; boss6_wpos[u] = bm[i]->pos; }
        if (turned) { hb.rot = bm[i]->rot; boss6_wrot[u] = bm[i]->rot; }
        if ((moved || turned) && bm[i]->has_world) { bm[i]->world = boss6_m4(boss6->matrix(i)); bm[i]->dirty_pose = true; }
        int sect = 0;
        for (int c = 0; c < boss06::kSections; ++c) if (boss6_sect[u][std::size_t(c)] == bm[i]->sect) { sect = c; break; }
        in.head[u].section = sect;
        in.head[u].count = bm[i]->count;
        bool posed = false;
        if (bm[i]->sect >= 0 && std::size_t(bm[i]->sect) < bm[i]->clips.size())
            for (const AnimClip& c : mdl[i]->clips)
                if (c.name == bm[i]->clips[std::size_t(bm[i]->sect)].name) { mdl[i]->pose(c, bm[i]->time); posed = true; break; }
        if (!posed) mdl[i]->rest_pose();
        const Mat4 place = boss6_m4(boss6->matrix(i));
        for (int k = 0; k < boss06::kNodes; ++k) {
            int ni = boss6_node[u][std::size_t(k)];
            if (ni < 0 && !mdl[i]->nodes.empty()) ni = 0;
            const Mat4 w = ni >= 0 ? mdl[i]->nodes[std::size_t(ni)].world * place : place;
            in.head[u].nodes[std::size_t(k)] = to_m4(w);
            boss6_node_m[u][std::size_t(k)] = in.head[u].nodes[std::size_t(k)];
        }
    }
    {
        const Actor* zv = boss6_zava();
        boss6_z.present = zv != nullptr;
        if (zv) { boss6_z.pos = {zv->pos.x, zv->pos.y + zv->height, zv->pos.z}; boss6_z.hp = zv->hp; boss6_z.max_hp = zv->max_hp; }
    }
    const Boss06Output out = boss6->tick(in, boss6_z);
    for (int i = 0; i < boss06::kHeads; ++i) {
        const std::size_t u = std::size_t(i);
        const Boss06Head& hb = boss6->head(i);
        for (int k = 0; k < boss06::kFxSlots; ++k) boss6_slot_m[u][std::size_t(k)] = hb.slots[std::size_t(k)].m;
        for (int k = 0; k < boss06::kOrbiters; ++k) boss6_orbit_m[u][std::size_t(k)] = hb.orbit[std::size_t(k)];
    }
    boss6_apply(out);
    for (int i = 0; i < boss06::kHeads; ++i) {
        const std::size_t u = std::size_t(i);
        const Boss06Head& hb = boss6->head(i);
        // The brain's SetRot / position writes: from then on the placement is its matrix (FUN_00503990's
        // yaw-pitch-roll rows, which the re-bake's X*Y*Z compose does not give for a pitched head).
        if (!boss6_veq(hb.pos, boss6_wpos[u]) || !boss6_veq(hb.rot, boss6_wrot[u])) {
            bm[i]->pos = hb.pos; bm[i]->rot = hb.rot;
            bm[i]->dest = hb.pos; bm[i]->speed = 0;       // a brain's write ends a script move
            bm[i]->has_world = true; boss6_owned[u] = true;
            bm[i]->world = boss6_m4(boss6->matrix(i));
            bm[i]->dirty_pose = true;
            boss6_wpos[u] = hb.pos; boss6_wrot[u] = hb.rot;
        }
        if (hb.alpha_target != boss6_wat[u] || hb.alpha_step != boss6_was[u]) {   // H+0x41c / H+0x420
            bm[i]->alpha_target = hb.alpha_target; bm[i]->alpha_step = hb.alpha_step;
            boss6_wat[u] = hb.alpha_target; boss6_was[u] = hb.alpha_step;
        }
        const bool solid = hb.collide && !hb.dead;         // H+0x60 & 0x80
        if (bm[i]->no_collide == solid) { bm[i]->no_collide = !solid; bm[i]->dirty_pose = true; }
        if (hb.dead && !boss6_gone[u]) {                   // H+0x11 |= 2 (move 15's end)
            boss6_gone[u] = true;
            bm[i]->alpha = bm[i]->alpha_target = 0.0f; bm[i]->dirty_alpha = true;
            std::printf("  [boss06] t%llu head %c destroyed\n", (unsigned long long)tick_count, 'a' + i);
        }
        if (boss6_log && int(hb.move) != boss6_move_log[u]) {
            boss6_move_log[u] = int(hb.move);
            std::printf("  [boss06] t%llu head %c move %d (pattern %d, lives %d)\n", (unsigned long long)tick_count, 'a' + i,
                        int(hb.move), int(hb.pattern), int(hb.lives));
        }
    }
    if (boss6_log && tick_count % 30 == 0) {
        const Boss06Head& a = boss6->head(0); const Boss06Head& b = boss6->head(1);
        const Actor* zv = boss6_zava();
        std::printf("  [boss06] t%llu a pat %d mv %d/%d (%.1f,%.1f,%.1f) lives %d | b pat %d mv %d/%d (%.1f,%.1f,%.1f) lives %d |"
                    " zava ph %d act %d/%d hp %.0f/%.0f f %x at (%.1f,%.1f,%.1f) | f156 %d feet (%.2f,%.2f,%.2f) on '%s' php %d\n",
                    (unsigned long long)tick_count, int(a.pattern), int(a.move), int(a.step), a.pos.x, a.pos.y, a.pos.z,
                    int(a.lives), int(b.pattern), int(b.move), int(b.step), b.pos.x, b.pos.y, b.pos.z, int(b.lives),
                    zv ? zv->ai.i(0x79c) : -1, zv ? zv->ai.i(0x728) : -1, zv ? zv->ai.i(0x72c) : -1, zv ? zv->hp : 0.0f,
                    zv ? zv->max_hp : 0.0f, zv ? unsigned(zv->ai.i(0x7a0)) : 0u, zv ? zv->pos.x : 0.0f,
                    zv ? zv->pos.y + zv->height : 0.0f, zv ? zv->pos.z : 0.0f, game.g_flags[156], feet.x, feet.y, feet.z,
                    player_floor_obj.c_str(), player.hp);
    }
    if (boss6_pilot) boss6_pilot();
};
// Zava in the enemy loop, before her enemy_ai_tick: the link (ZAVA_AB §A.1; boss06_zava.hpp).
auto boss6_zava_env = [&](Actor& en, AiEnv& env) {
    if (en.ai_index != 80) return;
    if (boss6_zava_id != en.instance_id || en.ai.phase() == 0) {   // a new actor (INIT pending): the statics start over
        boss6_link = Boss06Link{};
        if (boss6_zava_id != en.instance_id) boss6_zava_log = -1;
        boss6_zava_id = en.instance_id;
        boss6_z.hits = 0;
    }
    if (!boss6) boss6_player_in(boss6_in);                 // no heads: the player snapshot only (the fight does not start)
    boss6_zbo = Boss06Output{};
    boss6_link.heads = boss6.get(); boss6_link.in = &boss6_in; boss6_link.z = &boss6_z; boss6_link.out = &boss6_zbo;
    boss6_link.zout = Boss06ZavaOut{};
    static constexpr int kHugoFx[3] = {136, 147, 148};     // FindFx (FUN_00452270): PC2_S_C, PC2_S_C2, PC2_S_C2_tame
    for (int k = 0; k < 3; ++k) {
        boss6_link.zin.hugo_fx[std::size_t(k)].reset();
        for (const auto& f : fxsim.effects())
            if (!f->dead && f->id == kHugoFx[k]) { boss6_link.zin.hugo_fx[std::size_t(k)] = Vec3{f->M.m[3][0], f->M.m[3][1], f->M.m[3][2]}; break; }
    }
    boss6_link.zin.keep_out = exit_volume;                 // FUN_0042dc90 (KEEPOUT_BOXES.md §3.1; view_game.inl)
    // FUN_00443b80(5, &Z+0x94): EEX record 5. [H] +0xb0 = the GAMEINFO enemy-EXP curve at the record's level, as
    // boss01..05; [not modelled] the loader's difficulty scaling (the brain overrides HP / STR / DEF right after).
    boss6_link.zin.eex5 = [&](Actor& z) {
        if (eex.bosses.size() <= 5) return;
        const BossStats& bs = eex.bosses[5];
        z.max_hp = z.hp = bs.hp; z.atk = bs.atk; z.def = bs.def; z.level = bs.level;
        if (ginfo.ok()) z.exp_reward = ginfo.curves[kEnemyExp].at(bs.level);
    };
    env.boss06 = &boss6_link;
    boss6_zava_face = env.facing;
    boss6_zava_ran = true;
};
// ... and after it: the handler's facing / pose (m870_post), a head's events of this frame, her extra requests.
auto boss6_zava_post = [&](Actor& en, const AiEnv& env, AiOut& o) {
    if (en.ai_index != 80 || env.boss06 != &boss6_link) return;
    brains::m870_post(en, env, o);
    if (boss6_throw || boss6_reseat) {                     // move 8's throw (ZAVA_E2 §2), move 12's re-seat
        Boss06Output ho;
        ho.zava_throw = boss6_throw; ho.zava_reseat = boss6_reseat;
        brains::m870_heads_events(en, env, ho, o);
        if (boss6_throw) std::printf("  [boss06] t%llu Zava is thrown off head %c\n", (unsigned long long)tick_count, 'a' + (boss6_throw->head & 1));
        boss6_throw.reset(); boss6_reseat = false;
    }
    if (o.gauge_hp >= 0) o.gauge_emblem = boss06::kEmblem; // the heads' ctor wrote 0x76c214 = 6
    const Boss06ZavaOut& zo = boss6_link.zout;
    for (int m : zo.boss_cam) boss6_cam(m);
    if (zo.player_iframes) player_iframes = std::max(player_iframes, zo.player_iframes);   // P+0x734 = max(.., 9999)
    if (zo.player_unlock) { boss6_grab = false; player_hold = false; }
    if (zo.player_heading) {                               // P+0x308
        face_dir = {std::cos(*zo.player_heading), 0.0f, std::sin(*zo.player_heading)};
        facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
    }
    // FUN_004fd350(Z, P) with STR 267: the ordinary applier (its reaction runs). [H] her +0xfc / +0x108 are 0.
    if (zo.strike_atk) boss6_dmg.push_back({*zo.strike_atk, false, -1});
    if (zo.fog && zo.fog->first < int(room.fog.regions.size())) fog_script(zo.fog->first, zo.fog->second);   // ChangeFog
    if (zo.radial) frame_hold(zo.radial->first, zo.radial->second);   // the death's frame hold (0x76a548 / 0x76a54c, 0x7b2488 = 1)
    // [not modelled] zo.f6d6078, zo.byte_76a888, zo.kill_record (FUN_00403cd0).
    if (zo.gauge_zero || zo.gauge_hide) { hud.hide_boss_gauge(); gauge_owner = 0; }   // HP 0.0 drawn, then [0x6d5fd8] = 0
    if (zo.award_exp) {                                    // FUN_004fa4a0(Z+0x94)
        std::printf("  [boss06] t%llu EXP (lv %d)\n", (unsigned long long)tick_count, en.level);
        award_exp(en.exp_reward, en.level, /*boss*/true, false, 5);
    }
    const int st = en.ai.i(0x79c) * 100 + en.ai.i(0x728);
    if (boss6_log && env.took_damage)                      // Z+0x3bc & 2 as her brain read it this frame
        std::printf("  [boss06] t%llu Zava took a hit: hp %.0f, y %.2f, %s, hits left %d, ride count %u, lives %d/%d\n",
                    (unsigned long long)tick_count, en.hp, en.pos.y + en.height,
                    (en.ai.i(0x7a0) & 2) ? "riding" : en.flyer ? "flying" : en.vert.grounded ? "grounded" : "falling",
                    en.ai.i(0x8a8), unsigned(boss6_z.hits), boss6 ? int(boss6->head(0).lives) : 0,
                    boss6 ? int(boss6->head(1).lives) : 0);
    if (boss6_log && st != boss6_zava_log) {
        boss6_zava_log = st;
        std::printf("  [boss06] t%llu Zava phase %d action %d hp %.0f/%.0f\n", (unsigned long long)tick_count, en.ai.i(0x79c),
                    en.ai.i(0x728), en.hp, en.max_hp);
    }
};
// The handler's tail, after the integrator (0x53b5a7..): the ride, the floor ring, the z / y clamp; then her
// requests of the frame. [approx] byte Z+0x39a = 3 / Z+0x360: the sprite turns toward that angle at the mover's rate.
auto boss6_zava_tail = [&] {
    if (!boss6_zava_ran) return;
    boss6_zava_ran = false;
    Actor* zv = boss6_zava_id ? world.find(boss6_zava_id) : nullptr;
    if (!zv || zv->destroy_requested()) return;
    AiEnv env;
    env.actor = zv; env.boss06 = &boss6_link;
    if (game.g_flags[156] == 1) {
        brains::m870_tail(*zv, env);
        boss6_zava_floor_m = ef2sim::translation(zv->pos.x, 6.05f, zv->pos.z);
    }
    if (boss6_link.zout.facing)
        for (EnemyRender& er : render)
            if (er.actor == zv) {
                er.facing = turn_facing(0.18f, boss6_zava_face, *boss6_link.zout.facing);
                zv->yaw = 1.5707963f - er.facing;
                break;
            }
    actor_mats[zv->instance_id] = ef2sim::translation(zv->pos.x, zv->pos.y + zv->height, zv->pos.z);
    boss6_apply(boss6_zbo);
    boss6_zbo = Boss06Output{};
};
// drive `boss6[=opts]`: the fight as a player fights it, from then on every tick (`boss6=off` ends it). The pilot only
// ever (a) puts the player ON THE ARENA FLOOR (y 6.0), while grounded, at a spot a player would run to, and faces the
// target, and (b) presses Jump / Attack / the stick / the Skill button through the normal input path. It never lifts
// the player and never touches Zava or the heads; what it knows is what a player sees (where she is, whether she
// flies, casts, lies or rides).
//   phase 1   she flies at origin y 13 (ZAVA_AB B.3), out of a grounded swing's and a single jump's reach: stand under
//             her while she holds her place (action 4 the hover, 8 the lightning cast), jump, jump again near the
//             top (flag 163, the double jump), then the air combo (the stick toward her + Attack). Knocked down
//             (A.8): stand by her and swing.
//   phase 2   riding (Z+0x7a0 & 2): stand on the floor beside the ridden (orbited) head, toward her, and swing: each
//             tick she takes damage counts Z+0x7a8 (ZAVA_CG C.2 step 4). Thrown off (step 5): stand by her and
//             swing. The mount and the floor cast: wait. The big attack (C.6): run to the arena's edge while she
//             casts, then hold the stick away from the centre and keep jumping. A head's bite (ZAVA_EF E1.1): mash
//             Attack (each press tick counts toward the escape).
// Options (comma list): `lightning` = phase 1 jumps only while she casts lightning (action 8), not in the plain hover;
// `thunder` = phase 1 taps the Skill button from the second jump in place of the air combo (Yunica's Thunder 0x10c
// is a rising slash, C_010.ASO anime_Jump(0.14); cast in her hover it sets off her dodge, B.1 (3)); `cast` = also
// jump at her while she casts the floor attack (phase 2 action 8: her "hits left", C.4).
bool b6p_on = false, b6p_thunder = false, b6p_lightning = false, b6p_cast = false;
int b6p_air = -1;                                          // ticks since the pilot's jump press (-1 = not its jump)
bool b6p_second = false;                                   // its second jump is spent
int b6p_cool = 0;                                          // ticks until its next press on the ground
int b6p_skill = 0;                                         // ticks of the Skill button left to hold
boss6_pilot = [&] {
    if (!b6p_on) return;
    if (b6p_skill > 0 && --b6p_skill == 0) drive_skill = false;
    const Actor* zv = boss6_zava();
    if (boss6_grab && player.hp >= 1 && !csMgr.world_frozen()) {   // in a head's mouth: mash (a press every other tick)
        if (tick_count % 2 == 0) access_drive("oktap");
        b6p_air = -1;
        return;
    }
    if (!zv || game.g_flags[156] != 1 || csMgr.world_frozen() || player.hp < 1 || !boss6) { b6p_air = -1; return; }
    if (!zv->alive() || !zv->ai.hittable) return;          // dying
    const Vec3 c{zv->pos.x, zv->pos.y + zv->height, zv->pos.z};
    const int ph = zv->ai.i(0x79c), act = zv->ai.i(0x728), zf = zv->ai.i(0x7a0);
    const bool riding = (zf & 2) != 0, open = zv->def < 999.0f;
    const bool lying = !zv->flyer && zv->vert.grounded && !riding;   // knocked down / thrown off
    auto toward = [&](float& dx, float& dz) {              // unit xz from the player to her (+z right under her); the distance
        dx = c.x - feet.x; dz = c.z - feet.z;
        const float l = std::sqrt(dx * dx + dz * dz);
        if (l > 1e-3f) { dx /= l; dz /= l; } else { dx = 0; dz = 1; }
        return l;
    };
    auto face = [&](float dx, float dz) {
        face_dir = {dx, 0.0f, dz};
        facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
    };
    // Stand on the arena floor `d` from (x, z) on the player's side of it, facing it. Only while grounded.
    auto stand = [&](float x, float z, float d) {
        float dx = feet.x - x, dz = feet.z - z;
        const float l = std::sqrt(dx * dx + dz * dz);
        if (l > 1e-3f) { dx /= l; dz /= l; } else { dx = 0; dz = -1; }
        const float px = std::clamp(x + dx * d, -49.0f, -19.0f), pz = std::clamp(z + dz * d, 15.5f, 38.0f);
        const float fl = floor_at(phys_mesh(room), px, pz, 6.0f + 0.5f);   // the floor, never a head's back
        if (fl <= kNoFloor || std::fabs(fl - 6.0f) > 0.3f) return false;
        feet = {px, fl, pz};
        face(-dx, -dz);
        return true;
    };
    // The big attack (ZAVA_CG §C.6): run to the arena's edge while she flies to her spot and casts (steps 0..2, some
    // 200 frames), then hold the stick away from the centre and keep jumping (the pull is 0.3 a frame on the ground,
    // 0.1 in the air).
    if (ph == 2 && act == 20 && !open) {
        const int stp = zv->ai.i(0x72c);
        float ox = feet.x + 34.0f, oz = feet.z - 29.0f;
        const float ol = std::sqrt(ox * ox + oz * oz);
        if (ol > 1e-3f) { ox /= ol; oz /= ol; } else { ox = 0; oz = -1; }
        if (stp < 4) { if (pv.grounded && ol < 12.0f) stand(-34.0f, 29.0f, 40.0f); }
        else if (stp == 4) {
            drive_wish = {ox, 0.0f, oz}; drive_wish_n = 1;
            if (pv.grounded) drive_jump = true;
        }
        b6p_air = -1;
        return;
    }
    if (!pv.grounded) {                                    // in the air: only its own jump is steered
        if (b6p_air < 0) return;
        ++b6p_air;
        float dx, dz;
        const float l = toward(dx, dz);
        // The stick, held toward her from the lift-off on: the air combo needs it (COMBO_CHAIN: a neutral stick gives
        // the single jump attack 0x109), and a stick let go inside 10 ticks would open the tap window, which
        // refuses the jump press.
        drive_wish = {dx, 0.0f, dz}; drive_wish_n = 1;
        if (!b6p_second && pv.t <= 0.12f) {                // near the first jump's top (the engine's gate is t <= 0.15): the second
            drive_jump = true; b6p_second = true;
            if (boss6_log) std::printf("  [pilot] t%llu second jump, feet y %.2f\n", (unsigned long long)tick_count, feet.y);
        }
        const float need = c.y - (feet.y + kOriginH);     // her origin above the player's
        if (b6p_thunder && ph == 1) {                      // the skill from the second jump's rise (a 2-tick tap, every 24)
            if (b6p_second && need < 4.0f && l < 2.5f && open && b6p_skill == 0 && b6p_air % 24 == 0) {
                drive_skill = true; b6p_skill = 2;
                if (boss6_log) std::printf("  [pilot] t%llu air skill, feet y %.2f, Zava %.2f above, %.2f away\n",
                                           (unsigned long long)tick_count, feet.y, need, l);
            }
        } else if (b6p_second && need < 2.2f && l < 2.5f && open && b6p_air % 5 == 0) {
            try_attack();
            if (boss6_log) std::printf("  [pilot] t%llu air attack, feet y %.2f, Zava %.2f above, %.2f away\n",
                                       (unsigned long long)tick_count, feet.y, need, l);
        }
        return;
    }
    if (b6p_air >= 0 && b6p_air < 4) { ++b6p_air; return; }   // its press is taking off
    b6p_air = -1;
    if (b6p_cool > 0) { --b6p_cool; return; }
    if (!open) return;                                     // behind her barrier (the mount): wait
    if (lying) {                                           // on the ground: stand by her and swing
        float dx, dz;
        if (toward(dx, dz) > zv->hurt_radius + 1.0f) stand(c.x, c.z, zv->hurt_radius + 0.5f);
        else face(dx, dz);
        try_attack(); b6p_cool = 6;
        return;
    }
    if (riding) {                                          // on a head (her origin ~1.5 above the player's): from the floor beside it
        float dx, dz;
        if (toward(dx, dz) > zv->hurt_radius + 1.5f) stand(c.x, c.z, zv->hurt_radius + 0.9f);
        else face(dx, dz);
        try_attack(); b6p_cool = 6;
        return;
    }
    const float up = c.y - (6.0f + kOriginH);              // her origin above a grounded player's
    const bool holds = ph == 1 ? ((act == 4 && !b6p_lightning) || act == 8) : (b6p_cast && ph == 2 && act == 8);
    if (!zv->flyer || !holds) return;                      // she is moving: wait for her to stop
    if (!stand(c.x, c.z, 0.4f)) return;
    drive_jump = true; b6p_air = 0; b6p_second = false;
    if (boss6_log) std::printf("  [pilot] t%llu jump under Zava (act %d), %.2f above\n", (unsigned long long)tick_count, act, up);
};
boss6_drive = [&](const std::string& cmd) {
    if (cmd.rfind("boss6", 0) != 0) return false;
    b6p_on = cmd != "boss6=off";
    b6p_thunder = cmd.find("thunder") != std::string::npos;
    b6p_lightning = cmd.find("lightning") != std::string::npos;
    b6p_cast = cmd.find("cast") != std::string::npos;
    if (!b6p_on) { drive_skill = false; b6p_skill = 0; b6p_air = -1; }
    return true;
};
