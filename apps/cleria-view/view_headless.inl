// cleria-view — headless render_fps / render_play
// Textual part of main.cpp (split 2026-07-23, PLAN.md Phase 0): #included by
// main.cpp in a fixed order; ONE translation unit, not standalone. Keep it that way
// until a real TU split is warranted.

// First-person verification shot: spawn the player on the floor and render from
// eye level looking forward. Confirms floor height / eye placement headlessly.
static int render_fps(Gpu& g, const Room& room, const fs::path& out,
                      std::uint32_t W, std::uint32_t H, float yaw,
                      const fs::path& asset_root = {}) {
    const Vec3 c = {(room.mesh.bmin.x + room.mesh.bmax.x) * 0.5f, 0,
                    (room.mesh.bmin.z + room.mesh.bmax.z) * 0.5f};
    const float D = room_diag(room), eye = D * 0.02f;
    const float fl = floor_at(phys_mesh(room), c.x, c.z, room.mesh.bmax.y + D);
    const float fy = (fl > kNoFloor) ? fl : (room.mesh.bmin.y + room.mesh.bmax.y) * 0.5f;
    Camera cam;
    cam.pos = {c.x, fy + eye, c.z};
    cam.yaw = yaw; cam.pitch = -0.1f;
    write_mvp(g, cam.view_proj(float(W) / float(H), eye * 0.05f, D * 4.0f));

    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension = WGPUTextureDimension_2D; td.size = {W, H, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture color = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTextureView cview = wgpuTextureCreateView(color, nullptr);
    WGPUTextureView dview = make_depth(g, W, H);
    const std::uint32_t bpr = (W * 4 + 255) & ~255u;
    WGPUBufferDescriptor rbd = {};
    rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead; rbd.size = std::size_t(bpr) * H;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(g.device, &rbd);
    draw(g, room, cview, dview);
    // HUD preview (verifies text rendering headlessly).
    Hud hud; hud.init(g, res_file("hud_font.bin"), WGPUTextureFormat_RGBA8Unorm);
    if (!asset_root.empty()) {
        hud.init_atlas(g, asset_root / "IMAGE" / "STATUS_512.DDS");
    hud.init_mini(g, asset_root / "IMAGE" / "F_MINI.DDS");
        hud.init_boss(g, asset_root / "IMAGE" / "HP2.DDS");
        hud.boss_frac = 0.72f; hud.boss_name = "Nygtilger";  // preview the boss bar
    }
    hud.message("Acquired Chain Mail");
    Player pv; pv.hp = 312; pv.max_hp = 400; pv.level = 6; pv.sp = 138663; pv.exp_pct = 42.1f;
    // Bind the equipment slots here too, so a headless shot shows what the live HUD shows.
    // Without this the slots are structurally always empty in --play/--shot, which makes the
    // screenshots misleading and the equip wiring impossible to verify headlessly. Ids are
    // the new-game seed's (weapon = character-1, armor = character*6; Yunica -> 0 and 6).
    if (!asset_root.empty()) {
        const ItemTable itbl = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
        const int ids[4] = {0, 6, -1, -1};
        hud.set_equip(g, itbl, asset_root, ids);
    }
    hud.render(g, cview, W, H, room.name, 0.016f, &pv);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPUTexelCopyTextureInfo src = {}; src.texture = color; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {}; dst.buffer = readback;
    dst.layout.bytesPerRow = bpr; dst.layout.rowsPerImage = H;
    WGPUExtent3D ext = {W, H, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd); wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    MapReq mr;
    WGPUBufferMapCallbackInfo mi = {};
    mi.mode = WGPUCallbackMode_AllowProcessEvents; mi.callback = on_map; mi.userdata1 = &mr;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, rbd.size, mi);
    while (!mr.done) wgpuDevicePoll(g.device, true, nullptr);
    const auto* px = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(readback, 0, rbd.size));
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
            f.write(reinterpret_cast<const char*>(px + std::size_t(y) * bpr + x * 4), 3);
    wgpuBufferUnmap(readback);
    std::printf("fps shot %s: eye at y=%.2f (floor %.2f)\n", out.string().c_str(), fy + eye, fy);
    return 0;
}

// Headless "play" shot: the fixed-angle follow camera framing the character
// billboard centred on screen, over the room, with the HUD. For iterating the
// camera angle/zoom and sprite scale via PNGs before wiring the interactive loop.
// dist / worldH are ABSOLUTE world units (see the play-mode constants).
static int render_play(Gpu& g, Room& room, const fs::path& out,
                       std::uint32_t W, std::uint32_t H, const fs::path& asset_root,
                       float yaw, float pitch, float dist, float worldH,
                       std::uint32_t dir) {
    const Vec3 c = {(room.mesh.bmin.x + room.mesh.bmax.x) * 0.5f, 0,
                    (room.mesh.bmin.z + room.mesh.bmax.z) * 0.5f};
    const float D = room_diag(room);
    const float fl = floor_at(phys_mesh(room), c.x, c.z, room.mesh.bmax.y + D);
    Vec3 feet = {c.x, (fl > kNoFloor) ? fl : (room.mesh.bmin.y + room.mesh.bmax.y) * 0.5f, c.z};
    // Debug: CLERIA_SPAWN="x,y,z" drops the character somewhere specific, for framing
    // a particular bit of the room headlessly.
    if (const char* sp = std::getenv("CLERIA_SPAWN")) {
        float x, y, z;
        if (std::sscanf(sp, "%f,%f,%f", &x, &y, &z) == 3) feet = {x, y, z};
    }

    // Fixed camera: constant angle, positioned so the character centre is centred.
    update_fog(room, feet, 0, /*snap=*/true);
    Camera cam;
    update_camera(room, feet, 0, /*snap=*/true);
    // CLERIA_SCROLL="x,y,z": inject a cutscene camera Scroll_MoveTo (instant) to verify the
    // event-camera override headlessly — the framing should re-centre on that world point.
    if (const char* sc = std::getenv("CLERIA_SCROLL")) {
        float x, y, z; if (std::sscanf(sc, "%f,%f,%f", &x, &y, &z) == 3) scroll_moveto(x, y, z, 0);
    }
    if (g_cam.on) {  // the room's authored SCM camera
        apply_cam_state(cam, feet);
        const CameraRegion* rg = room.cam.pick(feet);
        std::printf("play: SCM camera \"%s\" fov=%.1fdeg pitch=%.1f dist=%.1f yaw=%.1f\n",
                    rg ? rg->name.c_str() : "?", g_cam.fov * 57.29578f,
                    rg ? rg->eye.pitch * 57.29578f : 0.f, rg ? rg->eye.dist : 0.f,
                    rg ? rg->eye.yaw * 57.29578f : 0.f);
    } else {  // no SCM data -> fall back to the args
        const Vec3 fwd = {std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
        const Vec3 center = {feet.x, feet.y + worldH * 0.5f, feet.z};
        cam.pos = {center.x - fwd.x * dist, center.y - fwd.y * dist, center.z - fwd.z * dist};
        cam.yaw = yaw; cam.pitch = pitch;
    }
    write_mvp(g, cam.view_proj(float(W) / float(H), D * 0.01f, D * 6.0f));

    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension = WGPUTextureDimension_2D; td.size = {W, H, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture color = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTextureView cview = wgpuTextureCreateView(color, nullptr);
    WGPUTextureView dview = make_depth(g, W, H);
    const std::uint32_t bpr = (W * 4 + 255) & ~255u;
    WGPUBufferDescriptor rbd = {};
    rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead; rbd.size = std::size_t(bpr) * H;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(g.device, &rbd);

    for (const Interactable& it : room.mesh.interactables)
        std::printf("  chest %s at (%.1f, %.1f, %.1f)\n", it.name.c_str(),
                    it.position.x, it.position.y, it.position.z);
    std::printf("  spawn feet (%.1f, %.1f, %.1f)  bmin(%.1f,%.1f,%.1f) bmax(%.1f,%.1f,%.1f)\n",
                feet.x, feet.y, feet.z, room.mesh.bmin.x, room.mesh.bmin.y, room.mesh.bmin.z,
                room.mesh.bmax.x, room.mesh.bmax.y, room.mesh.bmax.z);
    {   // The floor actually BENEATH the spawn: cap the search just above the feet,
        // otherwise floor_at returns the highest surface in the room (a balcony 17
        // units up), which tells us nothing about where the player lands.
        const float ceil = feet.y + 0.5f;
        const float fr = floor_at(room.mesh, feet.x, feet.z, ceil);
        const float fc = room.coll.indices.empty()
                             ? kNoFloor : floor_at(room.coll, feet.x, feet.z, ceil);
        std::printf("  floor under spawn: render=%.3f  collision=%.3f  (spawn y=%.3f)\n",
                    fr, fc, feet.y);
    }
    // CLERIA_SCROLL_TICKS=<n>: pre-advance the UV-scroll phase by n ticks before the
    // shot. --play renders a single frame and never ticks, so without this the water
    // is always frozen at phase 0 — this is how to see it move headlessly.
    if (const char* st = std::getenv("CLERIA_SCROLL_TICKS"))
        if (!room.scrolls.empty()) advance_scroll(g, room, std::atoi(st));
    draw(g, room, cview, dview);
    // Let the game's own animation script pick the sprite, as the live path does:
    // C_010 is Yunica's script, and with flag 161 clear it selects the GAMEPLAY set
    // (anime_File(15) = C_111), not the cutscene set we used to hardcode here.
    CharSprite ch;
    Aso shot_aso = Aso::load(asset_root / "ANI" / "C_010.ASO");
    AsoVm shot_vm;
    shot_vm.bind(&shot_aso);
    std::int32_t shot_flags[0x200] = {0};
    shot_vm.set_flags(shot_flags, 0x200);
    shot_vm.tick();
    const char* shot_want = aso_aia_name(shot_vm.file_id());
    const std::string shot_stem = (shot_want && *shot_want) ? std::string(shot_want)
                                                            : std::string("C_102");
    ShadowFx shadows; shadows.init(g, asset_root);
    bool have = ch.load(g, asset_root / "ANI" / (shot_stem + ".AIA"), 0);
    if (have) {
        const int img = ch.find_tag(std::uint32_t(shot_vm.anim_tag()));
        if (img >= 0) ch.set_anim(g, std::uint32_t(img), std::uint32_t(shot_vm.frame()));
        const std::uint32_t d = dir < ch.ndir ? dir : 0;
        draw_shadow(g, cview, dview, shadows.tex, feet, feet.y, 0.6f);
        draw_character(g, cview, dview, feet, worldH, ch.dirs[d], cam.forward(), cam.pos);
        std::printf("play: [aso] file(%d)=%s tag=%d frame=%d | sprite %ux%u dir %u/%u, "
                    "worldH %.2f, cam dist %.2f\n",
                    shot_vm.file_id(), shot_stem.c_str(), shot_vm.anim_tag(), shot_vm.frame(),
                    ch.dirs[d].w, ch.dirs[d].h, d, ch.ndir, worldH, dist);
    } else std::printf("play: character AIA load FAILED (%s)\n", shot_stem.c_str());

    // Enemies: the engine's real populate (run S_xxxx.XSO -> op 0xa0 SCT + op 0x12c
    // scripted), the same path play mode uses. CLERIA_ENEMY=1 dumps the roster — the
    // only headless view of it, since the interactive loop needs a window.
    {
        EnemyFx efx;
        const CastTable casts = CastTable::load(asset_root / "MISC" / "CASTINFO.DAT");
        const EnemyTable etab = EnemyTable::load(asset_root / "MISC" / "ENEMYINFO.DAT");
        const GameInfo ginfo = GameInfo::load(asset_root / "MISC" / "GAMEINFO.DAT");
        const EexTable eex = EexTable::load(asset_root / "MISC" / "EEX.DAT");
        ActorWorld world({&casts, &etab, &ginfo, &eex});
        std::int32_t flags[0x200] = {0};
        flags[150] = 1;  // character = Yunica (this headless shot has no save state)
        populate_room(world, room, flags);
        efx.init_from_world(g, asset_root, world, casts);
        std::vector<EnemyRender> list = build_enemy_render(world, casts, efx);
        if (std::getenv("CLERIA_ENEMY"))
            for (const EnemyRender& e : list)
                std::printf("play: %-14s (%-6s) lv%-3d HP %6.0f  ATK %5.0f  DEF %5.0f\n",
                            e.display.c_str(), e.stem.c_str(), e.actor->level,
                            e.actor->max_hp, e.actor->atk, e.actor->def);
        if (std::getenv("CLERIA_BARRIER"))
            for (const Actor& a : world.actors())
                std::printf("barrier: id=%d cast=%d grp1=%d hp=%.0f alive=%d %s\n",
                            a.instance_id, a.cast_id, a.tag.in_group(1), a.hp, a.alive(),
                            casts.get(a.cast_id) ? casts.sprite_stem(a.cast_id).c_str() : "?");
        draw_enemies(g, cview, dview, efx, list, cam.forward(), cam.pos, 0.0f, shadows.tex);
        // Barrier ward + any other 0x11a-placed effects (headless preview of world.effects()).
        if (std::getenv("CLERIA_BARRIER"))
            for (const auto& e : world.effects())
                std::printf("barrier: effect id=%d '%s' at (%.1f,%.1f,%.1f) a=%.2f\n",
                            e.id, e.name.c_str(), e.x, e.y, e.z, e.a);
        {
            EftList efl2; EftTex eft2;
            if (efl2.load(asset_root) && eft2.load(asset_root))
                draw_located_effects(g, world.effects(), asset_root, efl2, eft2, cview, dview,
                                     cam.forward(), 0.30f);
        }
    }
    // Torch flames (SEF placements + the EF2 flipbook).
    {
        FlameFx fx;
        if (fx.init(g, asset_root))
            draw_flames(g, room, cview, dview, fx, cam.forward(), 0.30f);
        else std::printf("play: NAGA01.DDS (flame) load FAILED\n");
    }

    Hud hud; hud.init(g, res_file("hud_font.bin"), WGPUTextureFormat_RGBA8Unorm);
    hud.init_atlas(g, asset_root / "IMAGE" / "STATUS_512.DDS");
    hud.init_mini(g, asset_root / "IMAGE" / "F_MINI.DDS");
    Player pv; pv.hp = 312; pv.max_hp = 400; pv.level = 6; pv.sp = 138663; pv.exp_pct = 42.1f;
    // CLERIA_DLG=1: preview the cutscene dialogue box (FACEWIN0 3-slice + name/text) so its
    // slice-UVs/position can be tuned headlessly against reference/realgame/dialogue_box_*.png.
    // (Layout check — uses the HUD font here, not Felghana; TitleFx::draw_dialogue is the real path.)
    if (std::getenv("CLERIA_DLG")) {
        Dds fwd = load_dds(asset_root / "IMAGE" / "FACEWIN0.DDS");
        SpriteTex fw = fwd.ok() ? upload_rgba(g, fwd.width, fwd.height, fwd.data.data()) : SpriteTex{};
        if (fw.tex) {
            write_mvp(g, Mat4::identity());
            const float sw = float(W), sh = float(H);
            const float by1 = sh, by0 = sh - sh * (224.0f / 768.0f);
            constexpr float vc = 0.5f, uL = 0.19f, uR = 0.586f, uEnd = 0.809f;
            float curl = (by1 - by0) * (uL * 512.0f) / (vc * 512.0f);
            auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
            auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
            draw_screen_image(g, cview, dview, false, fw, NX(0),        NY(by0), NX(curl),    NY(by1), 0.15f, 0.0f, 0.0f, uL,   vc);
            draw_screen_image(g, cview, dview, false, fw, NX(curl),     NY(by0), NX(sw-curl), NY(by1), 0.15f, uL,   0.0f, uR,   vc);
            draw_screen_image(g, cview, dview, false, fw, NX(sw-curl),  NY(by0), NX(sw),      NY(by1), 0.15f, uR,   0.0f, uEnd, vc);
            const float boxH = by1 - by0, tx = sw * 0.15f;
            hud.text("Boy in Black", tx, by0 + boxH * 0.14f, 2.0f, sw, sh, 240, 224, 192, 255);
            hud.text("That voice... it can't be. Is that really you?", tx, by0 + boxH * 0.45f, 2.6f, sw, sh, 255, 255, 255, 255);
        }
    }
    // CLERIA_CAPTION=map_name03: preview the area-name card (op 0xe2 loads IMAGE/<name>.dds).
    if (const char* cap = std::getenv("CLERIA_CAPTION")) {
        std::string nm = cap; for (char& c : nm) c = char(std::toupper((unsigned char)c));
        Dds cd = load_dds(asset_root / "IMAGE" / (nm + ".DDS"));
        if (cd.ok()) {
            SpriteTex ct = upload_rgba(g, cd.width, cd.height, cd.data.data());
            write_mvp(g, Mat4::identity());
            const float sw = float(W), sh = float(H);
            const float cw2 = sw * 0.6f, chh = cw2 * float(cd.height) / float(cd.width);
            const float cx = (sw - cw2) * 0.5f, cy = sh * 0.16f;
            auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
            auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
            draw_screen_image(g, cview, dview, false, ct, NX(cx), NY(cy), NX(cx + cw2), NY(cy + chh), 0.1f);
        }
    }
    hud.render(g, cview, W, H, room.name, 0.016f, &pv);
    if (g_play_overlay) g_play_overlay(g, cview, dview, W, H);   // e.g. the real dialogue box (--dialogue)

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPUTexelCopyTextureInfo src = {}; src.texture = color; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {}; dst.buffer = readback;
    dst.layout.bytesPerRow = bpr; dst.layout.rowsPerImage = H;
    WGPUExtent3D ext = {W, H, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd); wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    MapReq mr;
    WGPUBufferMapCallbackInfo mi = {};
    mi.mode = WGPUCallbackMode_AllowProcessEvents; mi.callback = on_map; mi.userdata1 = &mr;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, rbd.size, mi);
    while (!mr.done) wgpuDevicePoll(g.device, true, nullptr);
    const auto* px = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(readback, 0, rbd.size));
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
            f.write(reinterpret_cast<const char*>(px + std::size_t(y) * bpr + x * 4), 3);
    wgpuBufferUnmap(readback);
    std::printf("play shot %s\n", out.string().c_str());
    return 0;
}


