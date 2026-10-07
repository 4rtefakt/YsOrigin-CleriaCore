// The floor-3 script weather (build/effect_re/WEATHER.md, src/cleria/weather.hpp), included inside
// the game loop's setup after view_duel.inl. `wx` (view_game.inl) holds pool 9's weather GoXx and
// draws from the game's one xorshift (pick_rng, bound to game.rs).
//
// 0x125 FireDrop / 0x122 FrameSmoke make the emitters; 0x123 FireBlur sets the haze timer.
// CLERIA_WX_LOG=1 (CleriaCore diagnostics, not retail): each op, and every 100 ticks the live
// emitter / sprite counts and the weather's share of the shared RNG's draws.
static const bool wx_log = [] { const char* v = std::getenv("CLERIA_WX_LOG"); return v && *v && *v != '0'; }();
std::uint64_t wx_draws = 0, wx_steps = 0;
wx.rng = [&] { ++wx_draws; return pick_rng.next(); };
csHost.weather_fn = [&](int op, int life, float a, float b, float c) {
    if (op == 0x125) wx.fire_drop(std::uint32_t(life));
    else if (op == 0x122) wx.frame_smoke(std::uint32_t(life));
    else wx.fire_blur(a, b, c);
    if (wx_log) {
        if (op == 0x123) std::printf("  [weather] FireBlur(%.4f, %.4f, %.3f)\n", a, b, c);
        else std::printf("  [weather] %s(%d)\n", op == 0x125 ? "FireDrop" : "FrameSmoke", life);
    }
};

// One pass of pool 9's weather objects (FUN_004281c0 step 11) and FUN_00427c90's haze timer
// (step 9; it draws nothing, so its place relative to the emitters does not matter). The camera
// is the last LookAt the rig produced: eye 0x7b33f4, look-at 0x7b3400 (FUN_005ec270) [approx: the
// engine reads the values the last RENDERED frame left]. *0x730224 != 0 = a room is loaded.
auto step_weather = [&] {
    ++wx_steps;
    wx.step_blur();
    weather::Env e;
    if (g_cam.on) { e.eye = g_cam_out.eye; e.at = g_cam_out.target; }
    else { const Vec3 f = cam.forward(); e.eye = cam.pos; e.at = {cam.pos.x + f.x, cam.pos.y + f.y, cam.pos.z + f.z}; }
    e.noise = game.g_flags[290] != 0;                    // 0x76bda4 (the S_3009 noise)
    e.map = true;
    wx.step(e);
    wx.sweep();                                          // FUN_00428b10 runs every frame
    if (wx_log && tick_count % 100 == 0 && !wx.objects().empty()) {
        std::printf("  [weather] t%llu steps %llu emitters %zu objects %zu draws %llu haze %.3f\n",
                    (unsigned long long)tick_count, (unsigned long long)wx_steps, wx.live_emitters(), wx.objects().size(),
                    (unsigned long long)wx_draws, wx.blur.out);
    }
};

// The type-0 sprites (FUN_00584270): a yaw billboard (the 0x7961b0 basis, as the EF2 0x100 path)
// at +0x50 + 0x795f50 * +0x140 (0x795f50 = the camera forward [H-med]), turned by the +0x144 /
// +0x148 pair, half-size and alpha from the key frames, colour = alpha << 24 | +0x12c, render
// state 0x46c1 (additive). [not modelled] the render interpolation (+0x1e8 / +0x1ec..: the
// sub-tick lerp), the depth sort (additive: order-free), the FireBlur distortion pass.
auto draw_weather = [&](WGPUTextureView color, WGPUTextureView depthv, const Vec3& camFwd) {
    if (wx.objects().empty()) return;
    static std::map<int, SpriteTex> texs;                // weather::Tex -> texture (empty = failed)
    auto tex_of = [&](weather::Tex t) -> const SpriteTex* {
        auto it = texs.find(int(t));
        if (it == texs.end()) {
            fs::path p;
            if (t == weather::Tex::Sparkle) p = asset_root / "IMAGE" / "SPARKLE.DDS";
            else if (t == weather::Tex::Sparkle2) p = asset_root / "IMAGE" / "SPARKLE2.DDS";
            else if (t == weather::Tex::Kao00) p = asset_root / "IMAGE" / "KAO_00.DDS";
            else p = eft.path(asset_root, 34);           // 0x74a5e0 = EFTTEX[34]
            SpriteTex s{};
            if (!p.empty()) {
                Dds d = load_dds(p);
                if (d.ok()) s = upload_rgba(g, d.width, d.height, d.data.data());
            }
            it = texs.emplace(int(t), s).first;
        }
        return it->second.bind ? &it->second : nullptr;
    };
    const Vec3 up = {0, 1, 0};
    const Vec3 right = normalize(cross(up, camFwd));
    const Vec3 fwd = normalize(camFwd);
    struct Batch { std::vector<RoomVertex> v; std::vector<std::uint32_t> i; WGPUBindGroup bind{}; };
    std::map<int, Batch> batches;
    for (const auto& op : wx.objects()) {
        if (op->dead || op->kind != weather::Obj::Child) continue;
        const weather::Sprite& s = op->s;
        const SpriteTex* t = tex_of(s.d.tex);
        if (!t) continue;
        const weather::Look lk = weather::look(s);
        const int ai = std::clamp(int(lk.alpha), 0, 255);
        if (ai == 0) continue;
        const float a = float(ai) / 255.0f;
        const std::uint8_t R = std::uint8_t(float((s.d.argb >> 16) & 0xff) * a);
        const std::uint8_t G = std::uint8_t(float((s.d.argb >> 8) & 0xff) * a);
        const std::uint8_t B = std::uint8_t(float(s.d.argb & 0xff) * a);
        Batch& b = batches[int(s.d.tex)];
        b.bind = t->bind;
        const Vec3 o = {s.pos.x + fwd.x * s.d.depth_k, s.pos.y + fwd.y * s.d.depth_k, s.pos.z + fwd.z * s.d.depth_k};
        const float cc = s.cs * lk.half, ss = s.sn * lk.half;
        const float xy[4][2] = {{ss - cc, ss + cc}, {-ss - cc, ss - cc}, {ss + cc, cc - ss}, {cc - ss, -ss - cc}};
        const float uv[4][2] = {{0, 0}, {0, 1}, {1, 0}, {1, 1}};   // the 0x7461c4 rect {0, 0, 1, 1}
        const std::uint32_t base = std::uint32_t(b.v.size());
        for (int k = 0; k < 4; ++k)
            b.v.push_back({o.x + right.x * xy[k][0], o.y + xy[k][1], o.z + right.z * xy[k][0], 0, 1, 0,
                           uv[k][0], uv[k][1], R, G, B, 255});
        b.i.insert(b.i.end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
    }
    for (auto& [key, b] : batches) {
        if (b.i.empty()) continue;
        WGPUBuffer vb = make_buffer(g.device, g.queue, b.v.data(), b.v.size() * sizeof(RoomVertex), WGPUBufferUsage_Vertex);
        WGPUBuffer ib = make_buffer(g.device, g.queue, b.i.data(), b.i.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPURenderPassColorAttachment ca = {};
        ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDepthStencilAttachment da = {};
        da.view = depthv; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor rp = {};
        rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderSetPipeline(pass, g.fx_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, b.bind, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, std::uint32_t(b.i.size()), 1, 0, 0, 0);
        wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(g.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
        wgpuBufferRelease(vb); wgpuBufferRelease(ib);
    }
};
