// The CutsceneHost callbacks of the floor 1..3 script-op pass (build/event_re/FLOOR13_OPS.md),
// included from view_game.inl after the other csHost wiring. The ops themselves are decoded in
// src/cleria/xso.cpp; each callback cites the retail function it stands on.

// CLERIA_XSO_LOG=1 (CleriaCore diagnostics, not retail): print every sub-op the live VM skips as
// unmodelled (XsoVm::on_unhandled), once per script and sub-op. tools/xso_floor_ops.py --runtime
// collects these lines from headless room runs.
if (const char* xl = std::getenv("CLERIA_XSO_LOG"); xl && *xl && *xl != '0') {
    auto seen = std::make_shared<std::unordered_set<std::string>>();
    csMgr.vm.on_unhandled = [&csMgr, seen](std::uint32_t sub) {
        char key[160];
        std::snprintf(key, sizeof key, "%s/%03x", csMgr.vm.script_name.c_str(), unsigned(sub));
        if (seen->insert(key).second)
            std::printf("  [xso] unhandled 0x%03x in %s\n", unsigned(sub), csMgr.vm.script_name.c_str());
    };
}
csHost.p_grounded = &grounded;               // +0x388, the 0x15e / 0x15f grounded-only gate
csHost.origin_h = kOriginH;                  // +0x324: 0x15f measures from the origin (+0x54)
csHost.entry_label_p = &door_label;          // 0x80 Reg_EntryLabel (set by transition)
csHost.player_vanished_p = &player_vanished;  // 0x14d on the player (read by the hit pass)
// 0xD0 PadTilt(i) -> FUN_005d3500(i): the same vibration-pattern entry the player code uses.
csHost.pad_tilt_fn = [&](int i) { pad_rumble(i); };
// 0xBD Se_Play3DPos / 0xBE Se_Play3DChr: an EFFECTDEF 3D record (flag 0x10) plays at the point
// (FUN_0043a080), any other one as a plain 2D SE (FUN_0043a040 / FUN_005ce2a0(0x400)).
// [approx] Se_Play3DChr's voice follows the actor in the engine (FUN_0045d7a0); here it is placed
// once at the actor's position.
csHost.se3d_fn = [&](int se, const Vec3& p) {
    const EffectDef* d = audio.defs().find(se);
    if (d && d->is3d) sfx_at(se, p, kSfxAnimVol);
    else audio.play(se, kSfxAnimVol);
};
// 0x11F Eft_SetAlphaCurrent (FUN_0056fc70): every live effect so named (case-insensitive) gets
// +0x1e0 = a, the current alpha the 0x11C target steps from.
csHost.fx_alpha_cur_fn = [&](const std::string& n, float a) {
    for (auto& f : fxsim.effects()) {
        if (f->dead || f->name.size() != n.size()) continue;
        bool same = true;
        for (std::size_t k = 0; k < n.size() && same; ++k)
            same = std::tolower((unsigned char)f->name[k]) == std::tolower((unsigned char)n[k]);
        if (same) f->alpha_mul = a;
    }
};
// 0x17C Mesh_AttachEffect(mesh, frame, eff) (FUN_00572bd0): effect `eff` (ecx, the eftlist id) at
// local (0, 0, 0) of the mesh's world matrix (+0x20), or of its named frame (FUN_005f4e10).
// [approx] spawned once at the mesh's position: the attachment (the effect following the mesh),
// the mesh rotation and named frames are not applied. Every floor 1..3 site passes "" (no frame)
// on an object that does not move (@Break_*'s underwater effect 61, @Jump_001).
csHost.mesh_fx_fn = [&](const std::string& m, const std::string& /*frame*/, int eff) {
    std::string lm = m;
    for (char& ch : lm) ch = char(std::tolower((unsigned char)ch));
    if (const MeshObj* mo = meshes.find(lm)) spawn_sim_fx(eff, mo->pos, {}, 1.0f, {}, 0, {});
};
// 0x95 SetTimeScale(f): 0x6d6078, the scale the fixed-step accumulator runs at (main.cpp).
csHost.time_scale_fn = [&](float s) { g_time_scale = s; };
// 0x7F SetWeaponLevel: the weapon tier 0x76a634 + 8k (InvState::tier); compute_stats reads it on
// the next player_stats() (FUN_00420c40).
csHost.weapon_level_fn = [&](int k, int v) {
    if (k >= 0 && k < 4) inv.tier[k] = v;
};
// 0x82 SetLevel(n): FUN_004200f0 (the level, the EXP total that reaches it, the stats), then HP = max HP.
csHost.set_level_fn = [&](int n) {
    player.level = std::clamp(n, 1, 60);
    player.exp = ginfo.ok() ? float(ginfo.exp_to_level(player.level)) : 0.0f;
    player.max_hp = int(player_stats().max_hp);
    player.hp = player.max_hp;
    std::printf("  [script] SetLevel(%d): HP %d\n", n, player.hp);
};
// 0x89(i, v): the equipment dwords from 0x76a628 — the accessory, the held tool, the bracelet.
csHost.equip_slot_fn = [&](int i, int v) {
    if (i == 0) inv.accessory = v;
    else if (i == 1) inv.held = v;
    else if (i == 2) inv.magic = v;
};
