// cleria-view — the Video page (build/shell_re/VIDEO.md, src/cleria/video.hpp)
// Textual part of main.cpp: #included after view_shell.inl; ONE translation unit, not standalone.
// Retail's [graphics] keys (yso_win.ini, CONFIG.md §2.3) that CleriaCore's renderer honours, CleriaCore's
// own [cleria.video] extras, the GPU passes they need -- the world target (render scale) and the post
// pass (gamma) -- the FPS display and frame-time graph (a Shell::hud layer), the frame limiter and the
// window / display-mode handling. The game-loop hooks are one-liners in view_game.inl. At the defaults
// every pass here is skipped, so a default frame is the frame without this file, pixel for pixel.

static video::Settings& vid() { return settings().s.video; }

// ---- the GPU side ------------------------------------------------------------------------------------
// One fullscreen triangle, two fragment entries: fs_copy (the world target onto the frame, bilinear)
// and fs_gamma (retail's ramp out = in^(1/g), FUN_005d4de0, as a shader over the finished frame).
// [H] the shader writes pow() in float; the hardware ramp's 16-bit entries are not re-quantised.
static const char* kVideoWGSL = R"WGSL(
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var smp: sampler;
@group(0) @binding(2) var<uniform> u: vec4f;
struct VO { @builtin(position) p: vec4f, @location(0) uv: vec2f };
@vertex fn vs(@builtin(vertex_index) i: u32) -> VO {
    var o: VO;
    let x = f32((i << 1u) & 2u);
    let y = f32(i & 2u);
    o.p = vec4f(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    o.uv = vec2f(x, y);
    return o;
}
@fragment fn fs_copy(v: VO) -> @location(0) vec4f { return textureSample(src, smp, v.uv); }
@fragment fn fs_gamma(v: VO) -> @location(0) vec4f {
    let c = textureSample(src, smp, v.uv);
    return vec4f(pow(max(c.rgb, vec3f(0.0)), vec3f(u.x)), c.a);
}
// "Smooth edges" (a CleriaCore enhancement, not a port): FXAA over the world target -- where the
// brightness steps sharply, blend along the edge. u.yz = one world texel.
fn luma(c: vec3f) -> f32 { return dot(c, vec3f(0.299, 0.587, 0.114)); }
fn tap(uv: vec2f) -> vec4f { return textureSampleLevel(src, smp, uv, 0.0); }
@fragment fn fs_fxaa(v: VO) -> @location(0) vec4f {
    let r = u.yz;
    let cm = tap(v.uv);
    let nw = luma(tap(v.uv + vec2f(-1.0, -1.0) * r).rgb);
    let ne = luma(tap(v.uv + vec2f(1.0, -1.0) * r).rgb);
    let sw = luma(tap(v.uv + vec2f(-1.0, 1.0) * r).rgb);
    let se = luma(tap(v.uv + vec2f(1.0, 1.0) * r).rgb);
    let m = luma(cm.rgb);
    let lo = min(m, min(min(nw, ne), min(sw, se)));
    let hi = max(m, max(max(nw, ne), max(sw, se)));
    if (hi - lo < max(0.0833, hi * 0.166)) { return cm; }   // [H] FXAA's default edge thresholds: texture grain stays
    var dir = vec2f(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    let reduce = max((nw + ne + sw + se) * 0.03125, 0.0078125);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), vec2f(-8.0), vec2f(8.0)) * r;
    let a = 0.5 * (tap(v.uv + dir * (1.0 / 3.0 - 0.5)) + tap(v.uv + dir * (2.0 / 3.0 - 0.5)));
    let b = a * 0.5 + 0.25 * (tap(v.uv - dir * 0.5) + tap(v.uv + dir * 0.5));
    let lb = luma(b.rgb);
    if (lb < lo || lb > hi) { return vec4f(a.rgb, cm.a); }
    return vec4f(b.rgb, cm.a);
}
)WGSL";

struct VideoGpu {
    bool ready = false, copy_ok = false;
    WGPUTextureFormat fmt = WGPUTextureFormat_Undefined;
    std::uint32_t max_dim = 8192;
    WGPUBindGroupLayout bgl{};
    WGPURenderPipeline copy{}, gamma{}, fxaa{};
    WGPUSampler linear{}, nearest{};
    WGPUBuffer ubuf{};
    // the world target (Render scale != 100%)
    WGPUTexture wtex{};
    WGPUTextureView wview{}, wdepth{};
    WGPUBindGroup wbind{};
    std::uint32_t ww = 0, wh = 0;
    bool world_on = false;
    std::uint32_t world_w = 0, world_h = 0;      // the size the world draws at this frame
    // the post pass's copy of the frame
    WGPUTexture ptex{};
    WGPUTextureView pview{};
    WGPUBindGroup pbind{};
    std::uint32_t pw = 0, ph = 0;
    Gpu* gpu = nullptr;                          // for video_resample (the page and the drive have no Gpu)
    std::string formats;                         // the ShowFPS 2 suffix (FUN_005d4820's format names)
};
static VideoGpu g_vgpu;

static WGPUBindGroup video_bind(Gpu& g, WGPUTextureView v, WGPUSampler s) {
    WGPUBindGroupEntry e[3] = {};
    e[0].binding = 0; e[0].textureView = v;
    e[1].binding = 1; e[1].sampler = s;
    e[2].binding = 2; e[2].buffer = g_vgpu.ubuf; e[2].size = 16;
    WGPUBindGroupDescriptor d = {};
    d.layout = g_vgpu.bgl; d.entryCount = 3; d.entries = e;
    return wgpuDeviceCreateBindGroup(g.device, &d);
}

// Before build_pipeline: the world sampler from [graphics] TextureFilter / Anisotropy, and the passes.
// `copy_ok`: the frame can be copied (the gamma pass reads a copy of it).
static bool video_drive(const std::string& cmd);
static void video_low_res();
static void video_init(Gpu& g, WGPUTextureFormat fmt, bool copy_ok) {
    g_vgpu.gpu = &g;
    // CLERIA_VOPT="Key=value;Key=value": vopt settings from the start (a headless run's settings are
    // memory-only, and SmoothTextures / CrispSprites are read when a room / a sprite loads).
    if (const char* e = std::getenv("CLERIA_VOPT")) {
        std::stringstream ss(e);
        for (std::string kv; std::getline(ss, kv, ';');) if (!kv.empty()) video_drive("vopt=" + kv);
    }
    g_world_mips = vid().smooth_textures;
    g_sprite_point = vid().crisp_sprites;
    const auto& gr = settings().s.graphics;
    g_world_sampler = video::sampler_spec(gr.texture_filter, gr.anisotropy);
    video_low_res();                                     // a saved LowResoTexture: the index before the first room
    g_vgpu.fmt = fmt;
    g_vgpu.copy_ok = copy_ok;
    g_vgpu.formats = std::string(fmt == WGPUTextureFormat_BGRA8Unorm || fmt == WGPUTextureFormat_RGBA8Unorm
                                     ? "32(8888)" : "other") + "(D24)";   // [H] Depth24Plus, named like D24X8
    WGPULimits lim = {};
    if (wgpuDeviceGetLimits(g.device, &lim) == WGPUStatus_Success && lim.maxTextureDimension2D)
        g_vgpu.max_dim = lim.maxTextureDimension2D;

    WGPUShaderSourceWGSL wgsl = {};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL; wgsl.code = sv(kVideoWGSL);
    WGPUShaderModuleDescriptor smd = {}; smd.nextInChain = &wgsl.chain;
    WGPUShaderModule sm = wgpuDeviceCreateShaderModule(g.device, &smd);
    WGPUBindGroupLayoutEntry le[3] = {};
    le[0].binding = 0; le[0].visibility = WGPUShaderStage_Fragment;
    le[0].texture.sampleType = WGPUTextureSampleType_Float;
    le[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    le[1].binding = 1; le[1].visibility = WGPUShaderStage_Fragment;
    le[1].sampler.type = WGPUSamplerBindingType_Filtering;
    le[2].binding = 2; le[2].visibility = WGPUShaderStage_Fragment;
    le[2].buffer.type = WGPUBufferBindingType_Uniform; le[2].buffer.minBindingSize = 16;
    WGPUBindGroupLayoutDescriptor bld = {}; bld.entryCount = 3; bld.entries = le;
    g_vgpu.bgl = wgpuDeviceCreateBindGroupLayout(g.device, &bld);
    WGPUPipelineLayoutDescriptor pld = {}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &g_vgpu.bgl;
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(g.device, &pld);
    WGPUColorTargetState tgt = {}; tgt.format = fmt; tgt.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = {}; fs.module = sm; fs.entryPoint = sv("fs_copy"); fs.targetCount = 1; fs.targets = &tgt;
    WGPURenderPipelineDescriptor pd = {};
    pd.layout = pl;
    pd.vertex.module = sm; pd.vertex.entryPoint = sv("vs");
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList; pd.primitive.cullMode = WGPUCullMode_None;
    pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFF;
    pd.fragment = &fs;
    g_vgpu.copy = wgpuDeviceCreateRenderPipeline(g.device, &pd);
    fs.entryPoint = sv("fs_gamma");
    g_vgpu.gamma = wgpuDeviceCreateRenderPipeline(g.device, &pd);
    fs.entryPoint = sv("fs_fxaa");
    g_vgpu.fxaa = wgpuDeviceCreateRenderPipeline(g.device, &pd);
    wgpuPipelineLayoutRelease(pl);
    wgpuShaderModuleRelease(sm);

    WGPUSamplerDescriptor sd = {};
    sd.addressModeU = sd.addressModeV = sd.addressModeW = WGPUAddressMode_ClampToEdge;
    sd.magFilter = sd.minFilter = WGPUFilterMode_Linear; sd.maxAnisotropy = 1;
    g_vgpu.linear = wgpuDeviceCreateSampler(g.device, &sd);
    sd.magFilter = sd.minFilter = WGPUFilterMode_Nearest;
    g_vgpu.nearest = wgpuDeviceCreateSampler(g.device, &sd);
    WGPUBufferDescriptor bd = {}; bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst; bd.size = 16;
    g_vgpu.ubuf = wgpuDeviceCreateBuffer(g.device, &bd);
    g_vgpu.ready = g_vgpu.copy && g_vgpu.gamma;
}

static void video_fullscreen_pass(Gpu& g, WGPURenderPipeline pipe, WGPUBindGroup bind, WGPUTextureView dst,
                                  WGPUCommandEncoder enc) {
    WGPURenderPassColorAttachment ca = {};
    ca.view = dst; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store; ca.clearValue = {0, 0, 0, 1};
    WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, pipe);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    (void)g;
}

static void video_submit(Gpu& g, WGPUCommandEncoder enc) {
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);
}

// Render scale (a CleriaCore extra): the 3D world -- the room, the characters, the effects -- draws into
// its own target at RenderScale x the frame, which is then filtered onto the frame; the HUD, the
// windows and the menus draw over it at the native size. Swaps `view` / `depth` for the world's and
// back. At 100% it does nothing. "Smooth edges" uses the same target (at 100% too): its FXAA pass is the
// copy onto the frame, so the HUD and the menus are never filtered.
static void video_world_begin(Gpu& g, std::uint32_t cw, std::uint32_t ch, WGPUTextureView& view,
                              WGPUTextureView& depth) {
    g_vgpu.world_on = false;
    g_vgpu.world_w = cw; g_vgpu.world_h = ch;
    const float s = vid().render_scale;
    if (!g_vgpu.ready || (video::scale_is_native(s) && !vid().smooth_edges) || cw == 0 || ch == 0) return;
    const video::Extent e = video::scaled_extent(cw, ch, s, g_vgpu.max_dim);
    if (!g_vgpu.wtex || g_vgpu.ww != e.w || g_vgpu.wh != e.h) {
        if (g_vgpu.wbind) wgpuBindGroupRelease(g_vgpu.wbind);
        if (g_vgpu.wview) wgpuTextureViewRelease(g_vgpu.wview);
        if (g_vgpu.wdepth) wgpuTextureViewRelease(g_vgpu.wdepth);
        if (g_vgpu.wtex) wgpuTextureRelease(g_vgpu.wtex);
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
                   WGPUTextureUsage_CopySrc;    // CopySrc: FireBlur's copy (view_weather.inl)
        td.dimension = WGPUTextureDimension_2D; td.size = {e.w, e.h, 1};
        td.format = g_vgpu.fmt; td.mipLevelCount = 1; td.sampleCount = 1;
        g_vgpu.wtex = wgpuDeviceCreateTexture(g.device, &td);
        g_vgpu.wview = wgpuTextureCreateView(g_vgpu.wtex, nullptr);
        g_vgpu.wdepth = make_depth(g, e.w, e.h);
        g_vgpu.wbind = video_bind(g, g_vgpu.wview, g_vgpu.linear);
        g_vgpu.ww = e.w; g_vgpu.wh = e.h;
    }
    view = g_vgpu.wview;
    depth = g_vgpu.wdepth;
    g_vgpu.world_on = true;
    g_vgpu.world_w = e.w; g_vgpu.world_h = e.h;
}
static void video_world_end(Gpu& g, WGPUTextureView frame_view, WGPUTextureView frame_depth,
                            WGPUTextureView& view, WGPUTextureView& depth) {
    if (!g_vgpu.world_on) return;
    const bool fxaa = vid().smooth_edges && g_vgpu.fxaa;
    if (fxaa) {
        const float u[4] = {1.0f, 1.0f / float(g_vgpu.world_w), 1.0f / float(g_vgpu.world_h), 0};
        wgpuQueueWriteBuffer(g.queue, g_vgpu.ubuf, 0, u, sizeof u);
    }
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    video_fullscreen_pass(g, fxaa ? g_vgpu.fxaa : g_vgpu.copy, g_vgpu.wbind, frame_view, enc);
    video_submit(g, enc);
    view = frame_view;
    depth = frame_depth;
    g_vgpu.world_on = false;
}
// The size the world draws at this frame (a depth buffer made mid-world must match it).
static std::uint32_t video_world_w(std::uint32_t cw) { return g_vgpu.world_on ? g_vgpu.world_w : cw; }
static std::uint32_t video_world_h(std::uint32_t ch) { return g_vgpu.world_on ? g_vgpu.world_h : ch; }

// Brightness: [graphics] GammaValue through retail's ramp (FUN_005d4de0), over the finished frame (after
// the cursor, before the shell). Retail applies it as a hardware gamma ramp only in exclusive fullscreen
// with GammaEnable; CleriaCore applies it in every window mode. GammaValue 1.0 does nothing.
static void video_post(Gpu& g, WGPUTexture frame, WGPUTextureView view, std::uint32_t cw, std::uint32_t ch) {
    const float gv = settings().s.graphics.gamma_value;
    if (!g_vgpu.ready || !g_vgpu.copy_ok || video::gamma_is_identity(gv) || cw == 0 || ch == 0) return;
    if (!g_vgpu.ptex || g_vgpu.pw != cw || g_vgpu.ph != ch) {
        if (g_vgpu.pbind) wgpuBindGroupRelease(g_vgpu.pbind);
        if (g_vgpu.pview) wgpuTextureViewRelease(g_vgpu.pview);
        if (g_vgpu.ptex) wgpuTextureRelease(g_vgpu.ptex);
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D; td.size = {cw, ch, 1};
        td.format = g_vgpu.fmt; td.mipLevelCount = 1; td.sampleCount = 1;
        g_vgpu.ptex = wgpuDeviceCreateTexture(g.device, &td);
        g_vgpu.pview = wgpuTextureCreateView(g_vgpu.ptex, nullptr);
        g_vgpu.pbind = video_bind(g, g_vgpu.pview, g_vgpu.nearest);
        g_vgpu.pw = cw; g_vgpu.ph = ch;
    }
    const float u[4] = {video::gamma_exponent(gv), 0, 0, 0};
    wgpuQueueWriteBuffer(g.queue, g_vgpu.ubuf, 0, u, sizeof u);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPUTexelCopyTextureInfo src = {}; src.texture = frame; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyTextureInfo dst = {}; dst.texture = g_vgpu.ptex; dst.aspect = WGPUTextureAspect_All;
    WGPUExtent3D ext = {cw, ch, 1};
    wgpuCommandEncoderCopyTextureToTexture(enc, &src, &dst, &ext);
    video_fullscreen_pass(g, g_vgpu.gamma, g_vgpu.pbind, view, enc);
    video_submit(g, enc);
}

// The 3D projection's aspect: [graphics] StretchAspect pins it to 4:3 (FUN_005e2670).
static float video_aspect(std::uint32_t cw, std::uint32_t ch) {
    return video::projection_aspect(cw, ch, settings().s.graphics.stretch_aspect != 0);
}

// ---- frame statistics, the frame limiter -----------------------------------------------------------
static video::FrameStats g_vstats;
static video::FrameLimiter g_vlimit;
static std::uint64_t g_vlast_ns = 0;

// After each presented frame: the frame limiter ([cleria.video] FpsCap -- presentation only; the logic
// keeps its fixed 60 Hz accumulator and simply runs more ticks per frame), then the counters.
static void video_frame_end(int ticks) {
    std::uint64_t now = SDL_GetTicksNS();
    const std::uint64_t wait = g_vlimit.wait(now, vid().fps_cap);
    if (wait) { SDL_DelayPrecise(wait); now = SDL_GetTicksNS(); }
    const float ms = g_vlast_ns ? float(double(now - g_vlast_ns) * 1e-6) : 0.0f;
    g_vlast_ns = now;
    g_vstats.frame(now, ms, ticks);
}

// Shift+F1 (FUN_0040e940): ShowFPS 0 -> 1 -> 2 -> 0.
static void video_cycle_show_fps() {
    auto& sf = settings().s.graphics.show_fps;
    sf = (std::clamp(int(sf), 0, 2) + 1) % 3;
    settings().touch();
    settings().save();
}

// The overlay (a Shell::hud layer). ShowFPS 1 / 2: retail's text (FUN_00429530) [H] drawn top-left in
// the shell's font -- retail draws it with its own font at a position not traced. PerfGraph: the last
// two seconds of frame times, with 60 / 30 fps guides.
static void video_hud(ImDrawList* dl, float /*w*/, float /*h*/) {
    const int mode = std::clamp(int(settings().s.graphics.show_fps), 0, 2);
    const bool graph = vid().perf_graph;
    if (mode == 0 && !graph) return;
    const float S = ui::S;
    float x = 10.0f * S, y = 10.0f * S;
    if (mode > 0) {
        const std::string t = video::fps_text(mode, g_vstats.fps, g_vstats.tps, g_vgpu.formats);
        const float px = 17.0f * S;
        const ImVec2 ts = ui::text_size(ui::F.medium, px, t.c_str());
        dl->AddRectFilled({x, y}, {x + ts.x + 14.0f * S, y + ts.y + 8.0f * S}, IM_COL32(0, 0, 0, 150), 3.0f * S);
        ui::text(dl, ui::F.medium, px, {x + 7.0f * S, y + 4.0f * S}, ui::kTabOn, t.c_str());
        y += ts.y + 14.0f * S;
    }
    if (graph) {
        const float gw = 260.0f * S, gh = 76.0f * S, head = 22.0f * S;
        dl->AddRectFilled({x, y}, {x + gw, y + gh + head}, IM_COL32(0, 0, 0, 160), 3.0f * S);
        char buf[96];
        std::snprintf(buf, sizeof buf, "%.1f ms avg   %.1f ms max   %d fps", g_vstats.avg_ms(), g_vstats.max_ms(),
                      g_vstats.fps);
        ui::text(dl, ui::F.body, 14.0f * S, {x + 7.0f * S, y + 4.0f * S}, ui::kValue, buf);
        const float top = y + head, bot = y + head + gh - 4.0f * S;
        const float scale_ms = 50.0f;                        // [H] the graph's full height
        auto ly = [&](float ms) { return bot - std::min(ms, scale_ms) / scale_ms * (bot - top); };
        dl->AddLine({x + 4.0f * S, ly(16.667f)}, {x + gw - 4.0f * S, ly(16.667f)}, IM_COL32(190, 214, 106, 120));
        dl->AddLine({x + 4.0f * S, ly(33.333f)}, {x + gw - 4.0f * S, ly(33.333f)}, IM_COL32(230, 176, 64, 120));
        const int n = g_vstats.count;
        const float bw = (gw - 8.0f * S) / float(video::FrameStats::kHistory);
        for (int k = 0; k < n; ++k) {
            const float ms = g_vstats.at(k);
            const float bx = x + 4.0f * S + float(video::FrameStats::kHistory - n + k) * bw;
            const ImU32 c = ms > 33.4f ? IM_COL32(230, 96, 64, 230) : ms > 17.5f ? ui::kWarn : ui::kGood;
            dl->AddRectFilled({bx, ly(ms)}, {bx + std::max(1.0f, bw - 1.0f), bot}, c);
        }
    }
}

// ---- the window ------------------------------------------------------------------------------------
// The display: [graphics] Adapter as an index into SDL's display list (retail: the D3D adapter, one per
// display), else the primary one.
static SDL_DisplayID video_display() {
    int n = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&n);
    SDL_DisplayID id = SDL_GetPrimaryDisplay();
    const int a = settings().s.graphics.adapter;
    if (ids && a >= 0 && a < n) id = ids[a];
    SDL_free(ids);
    return id;
}

// Put the window in `mode`: windowed at [graphics] BackBufferWidth x Height (fitted to the desktop when
// `resize`), borderless at the desktop resolution, or exclusive fullscreen at the closest display mode to
// BackBufferWidth x Height @ RefreshRate (0 = the display's default).
static void video_apply_window(SDL_Window* win, int mode, bool resize) {
    const auto& gr = settings().s.graphics;
    const SDL_DisplayID d = video_display();
    if (mode != video::Windowed || resize)
        SDL_SetWindowPosition(win, int(SDL_WINDOWPOS_CENTERED_DISPLAY(d)), int(SDL_WINDOWPOS_CENTERED_DISPLAY(d)));
    if (mode == video::Windowed) {
        SDL_SetWindowFullscreen(win, false);
        if (resize) {
            SDL_Rect ub = {};
            SDL_GetDisplayUsableBounds(d, &ub);
            const video::Extent e = video::fit_window(gr.back_buffer_width, gr.back_buffer_height, ub.w, ub.h);
            SDL_SetWindowSize(win, int(e.w), int(e.h));
            SDL_SetWindowPosition(win, int(SDL_WINDOWPOS_CENTERED_DISPLAY(d)), int(SDL_WINDOWPOS_CENTERED_DISPLAY(d)));
        }
    } else if (mode == video::Borderless) {
        SDL_SetWindowFullscreenMode(win, nullptr);
        SDL_SetWindowFullscreen(win, true);
    } else {
        SDL_DisplayMode m = {};
        if (SDL_GetClosestFullscreenDisplayMode(d, gr.back_buffer_width, gr.back_buffer_height, float(gr.refresh_rate),
                                                false, &m))
            SDL_SetWindowFullscreenMode(win, &m);
        else
            SDL_SetWindowFullscreenMode(win, nullptr);
        SDL_SetWindowFullscreen(win, true);
    }
}

// At start: the saved window mode, and (a file-backed session) the saved resolution. A CLERIA_DRIVE run's
// hidden offscreen window is left alone (its 1280 x 800 target is what the regressions expect).
static int video_boot_window(SDL_Window* win, bool offscreen) {
    const int mode = std::clamp(vid().window_mode, 0, 2);
    if (!offscreen) video_apply_window(win, mode, !settings().memory_only());
    return mode;
}

// [graphics] TextureFilter / Anisotropy changed: the world samplers are rebuilt at once. Textures bound from
// now on (the next area's, sprites loaded later) use them; what is already on screen keeps its sampler.
static void video_resample() {
    const auto& gr = settings().s.graphics;
    const video::SamplerSpec want = video::sampler_spec(gr.texture_filter, gr.anisotropy);
    if (!g_vgpu.gpu || (want.linear == g_world_sampler.linear && want.mip_linear == g_world_sampler.mip_linear &&
                        want.anisotropy == g_world_sampler.anisotropy)) return;
    g_world_sampler = want;
    make_world_samplers(*g_vgpu.gpu);
}

// [graphics] LowResoTexture changed (the page, a preset, the drive) or was loaded: FUN_005fba80 loads a
// texture from the L folder beside it when that copy exists [EXPORT 005fba80.c:91-140]; here the texture
// index is rebuilt with the L folders first, and the textures loaded from then on (the next area) use it.
// [not modelled] the low-resolution sprite sheets data/ani/l/*.aia (FUN_00444a50) and the combination with
// CompressedTexture (another folder).
static void reindex_textures(Gpu& g);                    // main.cpp
static void video_low_res() {
    const bool on = settings().s.graphics.low_reso_texture != 0;
    if (on == g_low_res_textures) return;
    g_low_res_textures = on;
    if (g_vgpu.gpu) reindex_textures(*g_vgpu.gpu);
}

// ---- the page ----------------------------------------------------------------------------------------
struct VideoModes {
    std::vector<video::Mode> modes, res;
    std::vector<std::string> names;              // the displays
    int refresh = 0;
    void poll(SDL_DisplayID d) {
        if (refresh-- > 0) return;
        refresh = 120;
        modes.clear();
        int n = 0;
        if (SDL_DisplayMode** ms = SDL_GetFullscreenDisplayModes(d, &n)) {
            for (int i = 0; i < n; ++i) modes.push_back({ms[i]->w, ms[i]->h, ms[i]->refresh_rate});
            SDL_free(ms);
        }
        res = video::resolutions(modes);
        names.clear();
        int k = 0;
        if (SDL_DisplayID* ids = SDL_GetDisplays(&k)) {
            for (int i = 0; i < k; ++i) {
                const char* nm = SDL_GetDisplayName(ids[i]);
                names.push_back(std::to_string(i + 1) + ": " + (nm ? nm : "Display"));
            }
            SDL_free(ids);
        }
    }
};

static int video_choice(ui::List& L, const char* label, int cur, const std::vector<std::string>& items,
                        const char* desc, bool enabled, bool* changed) {
    std::vector<const char*> p;
    for (const std::string& s : items) p.push_back(s.c_str());
    int idx = cur;
    *changed = ui::choice(L, label, &idx, p.data(), int(p.size()), desc, enabled);
    return idx;
}

// The Video tab. `window_mode` / `vsync` are render_window's (the pointer mapping and the swap chain read
// them); `configure` rebuilds the swap chain, `persist` saves.
static void video_page(ui::List& L, SDL_Window* win, int& window_mode, bool& vsync,
                       const std::function<void()>& configure, const std::function<void()>& persist) {
    auto& gr = settings().s.graphics;
    auto& v = vid();
    static VideoModes vm;
    vm.poll(video_display());
    bool ch = false;
    if (const char* e = std::getenv("CLERIA_VIDEO_SCROLL")) {   // headless captures of the lower rows (px at 1000 h)
        const float mx = std::max(0.0f, L.content_h - L.size.y);
        L.scroll = L.scroll_to = std::min(float(std::atof(e)) * ui::S, mx);
    }

    ui::section(L, "Display");
    {
        static const char* kModes[] = {"Windowed", "Borderless fullscreen", "Fullscreen"};
        if (ui::choice(L, "Window mode", &window_mode, kModes, 3,
                       "Windowed; borderless fullscreen at your desktop resolution; or exclusive fullscreen at the "
                       "resolution below. (Original: Full Screen. Borderless is not a retail option.)")) {
            v.window_mode = window_mode;
            settings().s.cleria.borderless_fullscreen = window_mode == video::Borderless;
            gr.windowed = video::retail_windowed(window_mode);   // retail follows the same choice
            video_apply_window(win, window_mode, window_mode == video::Windowed);
            persist();
        }
    }
    {
        std::vector<video::Mode> res = vm.res;
        bool listed = false;
        for (const video::Mode& m : res) listed |= m.w == gr.back_buffer_width && m.h == gr.back_buffer_height;
        if (!listed) res.insert(res.begin(), {gr.back_buffer_width, gr.back_buffer_height, 0.0f});   // the saved one
        std::vector<std::string> items;
        for (const video::Mode& m : res) items.push_back(std::to_string(m.w) + " x " + std::to_string(m.h));
        const int cur = std::max(0, video::nearest_resolution(res, gr.back_buffer_width, gr.back_buffer_height));
        const int idx = video_choice(L, "Resolution", cur, items,
                                     "The window's size, and the screen mode in fullscreen. Borderless fullscreen always "
                                     "uses your desktop resolution. (Original: Resolution.)",
                                     window_mode != video::Borderless, &ch);
        if (ch && idx >= 0 && idx < int(res.size())) {
            gr.back_buffer_width = res[std::size_t(idx)].w;
            gr.back_buffer_height = res[std::size_t(idx)].h;
            if (window_mode != video::Borderless) video_apply_window(win, window_mode, true);
            persist();
        }
    }
    {
        const std::vector<int> rates = video::refresh_rates(vm.modes, gr.back_buffer_width, gr.back_buffer_height);
        std::vector<std::string> items = {"Default"};
        for (int r : rates) items.push_back(std::to_string(r) + " Hz");
        int cur = 0;
        for (int i = 0; i < int(rates.size()); ++i) if (rates[std::size_t(i)] == gr.refresh_rate) cur = i + 1;
        if (gr.refresh_rate > 0 && cur == 0) { items.push_back(std::to_string(gr.refresh_rate) + " Hz"); cur = int(items.size()) - 1; }
        const int idx = video_choice(L, "Refresh rate", cur, items,
                                     "The display's refresh rate in exclusive fullscreen. Default keeps the display's own. "
                                     "(Original: Refresh Rate.)",
                                     window_mode == video::Exclusive, &ch);
        if (ch) {
            gr.refresh_rate = idx == 0 ? 0 : idx - 1 < int(rates.size()) ? rates[std::size_t(idx - 1)] : gr.refresh_rate;
            video_apply_window(win, window_mode, false);
            persist();
        }
    }
    if (vm.names.size() > 1) {
        const int cur = std::clamp(int(gr.adapter), 0, int(vm.names.size()) - 1);
        const int idx = video_choice(L, "Display", cur, vm.names,
                                     "The monitor the game opens on. (Original: Adapter.)", true, &ch);
        if (ch) {
            gr.adapter = idx;
            vm.refresh = 0;
            video_apply_window(win, window_mode, window_mode == video::Windowed);
            persist();
        }
    }
    if (ui::toggle(L, "VSync", &vsync, "Synchronise frames with the display's refresh rate to avoid tearing.")) {
        configure();
        settings().s.cleria.vsync = vsync;
        gr.wait_vsync = vsync;                               // retail's WaitVSync follows the same choice
        persist();
    }
    {
        static const int kCaps[] = {0, 30, 60, 75, 90, 120, 144, 165, 240};
        std::vector<int> caps(std::begin(kCaps), std::end(kCaps));
        if (std::find(caps.begin(), caps.end(), v.fps_cap) == caps.end()) caps.push_back(v.fps_cap);
        std::vector<std::string> items;
        for (int c : caps) items.push_back(c == 0 ? std::string("Off") : std::to_string(c) + " fps");
        const int cur = int(std::find(caps.begin(), caps.end(), v.fps_cap) - caps.begin());
        const int idx = video_choice(L, "Frame rate limit", cur, items,
                                     "Caps how many frames are drawn per second, to save power or steady the frame pace. "
                                     "The game itself always runs at its fixed 60 updates a second. Not a retail option.",
                                     true, &ch);
        if (ch) { v.fps_cap = caps[std::size_t(idx)]; persist(); }
    }

    ui::section(L, "Image");
    {
        float gv = std::clamp(gr.gamma_value, 0.5f, 2.0f);
        if (ui::slider(L, "Brightness", &gv, 0.5f, 2.0f, 0.05f, "%.2f",
                       "Screen gamma: above 1.00 brightens the dark tones, below darkens them. The same setting as the "
                       "in-game Options' Screen Brightness. The original applies it only in fullscreen; CleriaCore in "
                       "every window mode.")) {
            gr.gamma_value = std::round(gv * 100.0f) / 100.0f;
            persist();
        }
        if (ui::button(L, "Reset brightness", "Back to 1.00, the unchanged picture. (Original: the Normal button beside "
                       "Gamma Correction.)", gr.gamma_value != 1.0f)) {
            gr.gamma_value = 1.0f;
            persist();
        }
    }
    {
        float pct = v.render_scale * 100.0f;
        if (ui::slider(L, "Render scale", &pct, video::kScaleMin * 100.0f, video::kScaleMax * 100.0f,
                       video::kScaleStep * 100.0f, "%.0f%%",
                       "The 3D world's resolution relative to the window. Above 100% renders more pixels and filters "
                       "them down (anti-aliasing, for strong GPUs); below 100% renders fewer (for weak GPUs). The HUD "
                       "and menus stay sharp. Not a retail option.")) {
            v.render_scale = video::clamp_scale(pct / 100.0f);
            persist();
        }
    }
    {   // CleriaCore's own "looks better" switches: none is a retail option, all are off by default.
        if (ui::toggle(L, "Smooth distant textures", &v.smooth_textures,
                       "Floors and walls far from the camera look cleaner and shimmer less when you move. Takes "
                       "effect in the next area you enter. A CleriaCore extra, not a retail option.")) {
            g_world_mips = v.smooth_textures;
            persist();
        }
        if (ui::toggle(L, "Smooth edges", &v.smooth_edges,
                       "Softens the jagged, stair-stepped edges of the scenery and the characters. The HUD and the "
                       "menus stay sharp. A CleriaCore extra, not a retail option."))
            persist();
        static const char* kSprites[] = {"Soft", "Crisp"};
        int sp = v.crisp_sprites ? 1 : 0;
        if (ui::choice(L, "Character sprites", &sp, kSprites, 2,
                       "Soft blends the pixels of the characters and monsters (the default). Crisp keeps every pixel "
                       "sharp, like pixel art. Takes effect for the characters loaded from now on (the next area). A "
                       "CleriaCore extra, not a retail option.")) {
            g_sprite_point = v.crisp_sprites = sp == 1;   // sprites uploaded from now sample that way
            persist();
        }
    }
    {
        static const char* kFilters[] = {"Point", "Bilinear", "Trilinear"};
        int f = std::clamp(int(gr.texture_filter), 0, 2);
        if (ui::choice(L, "Texture filtering", &f, kFilters, 3,
                       "Point keeps texels sharp and blocky; bilinear smooths them (the original's default). "
                       "Trilinear also blends mipmap levels, which CleriaCore only loads with Smooth distant textures "
                       "on, so here it looks like bilinear. Takes effect in the next area you enter. (Original: "
                       "Texture Filter.)")) {
            gr.texture_filter = f;
            video_resample();
            persist();
        }
        static const char* kAniso[] = {"Off", "2x", "4x", "8x", "16x"};
        int a = 0;
        while (a < 4 && (2 << a) <= gr.anisotropy) ++a;
        if (ui::choice(L, "Anisotropic filtering", &a, kAniso, 5,
                       "Keeps floors and walls seen at a steep angle sharper. Needs bilinear or trilinear filtering. "
                       "Takes effect in the next area you enter. (Original: Anisotropic Filter.)", gr.texture_filter != 0)) {
            gr.anisotropy = a == 0 ? 1 : (1 << a);
            video_resample();
            persist();
        }
    }
    {
        bool lo = gr.low_reso_texture != 0;                  // 0x76516f (video_low_res)
        if (ui::toggle(L, "Low-resolution textures", &lo,
                       "Uses the game's smaller copies of the floor, wall and object textures: blurrier, lighter on "
                       "slow PCs. Takes effect in the next area you enter. The characters keep their full "
                       "resolution for now. (Original: Low-Resolution Textures.)")) {
            gr.low_reso_texture = lo;
            video_low_res();
            persist();
        }
    }
    {   // [game] BloodyEffectLevel (0x6d5d92): read live by the enemy hit / death ports (> 0 the blood effects
        // and red debris, > 1 the extra ones)
        static const char* kBlood[] = {"Off", "Low", "High"};
        int b = std::clamp(int(settings().s.game.bloody_effect_level), 0, 2);
        if (ui::choice(L, "Blood", &b, kBlood, 3,
                       "How much blood enemy hits and deaths show. High is the original's default. (Original: Blood.)")) {
            settings().s.game.bloody_effect_level = b;
            persist();
        }
    }
    {
        bool st = gr.stretch_aspect != 0;
        if (ui::toggle(L, "Stretch widescreen", &st,
                       "Off: a wide window shows more of the scene at the sides (the original's default). On: the 4:3 "
                       "view is stretched to fill the window. The HUD and menus keep their 4:3 layout for now. "
                       "(Original: Stretch Widescreen.)")) {
            gr.stretch_aspect = st;
            persist();
        }
        bool dm = gr.disable_movies != 0;
        if (ui::toggle(L, "Disable movies", &dm,
                       "Skips every movie: the logo, the prologue, the opening and the ending. (Original: Disable all "
                       "movie playback.)")) {
            gr.disable_movies = dm;
            persist();
        }
        bool me = gr.mask_of_eyes != 0;                  // 0x76519b, read by FUN_005fed30 (view_mask.inl)
        if (ui::toggle(L, "Disable Mask of Eyes effect", &me,
                       "Off: wearing the Mask of Eyes turns the world grey (the HUD and menus keep their colour). "
                       "On: the screen keeps its colour; hidden walls and floors still react to the mask. (Original: "
                       "Disable Mask of Eyes Effect.)")) {
            gr.mask_of_eyes = me;
            persist();
        }
    }

    ui::section(L, "Overlay");
    {
        static const char* kFps[] = {"Off", "FPS", "FPS and ticks"};
        int sf = std::clamp(int(gr.show_fps), 0, 2);
        if (ui::choice(L, "FPS display", &sf, kFps, 3,
                       "Frames drawn per second (and game updates per second) in the corner. Shift+F1 cycles it. "
                       "(Original: FPS Display.)")) {
            gr.show_fps = sf;
            persist();
        }
        if (ui::toggle(L, "Frame-time graph", &v.perf_graph,
                       "A graph of the last two seconds of frame times, with the 60 and 30 fps lines. Not a retail "
                       "option."))
            persist();
    }

    ui::section(L, "Presets");                             // the config tool's "Basic Settings" (graphics_preset, settings.hpp)
    {
        static const char* const kNames[] = {"Lightest", "Light", "Normal", "High-definition"};
        static const char* const kDesc[] = {
            "The original's Lightest preset: bilinear filtering, no anisotropic filtering, brightness 1.00, VSync on; "
            "and, saved for the original game: low-resolution textures, other simplification, no mipmaps, no shadows, "
            "reflections or glare.",
            "The original's Light preset: bilinear filtering, no anisotropic filtering, brightness 1.00, VSync on; and, "
            "saved for the original game: low shadows, reflections and glare.",
            "The original's Normal preset: trilinear filtering, 2x anisotropic filtering, brightness 1.00, VSync on; and, "
            "saved for the original game: full shadows, reflections and glare.",
            "The original's High-Definition preset: trilinear filtering, 16x anisotropic filtering, brightness 1.00, "
            "VSync on; and, saved for the original game: anti-aliasing, full shadows, reflections and glare."};
        for (int p = 0; p < 4; ++p) {
            const std::string d = std::string(kDesc[p]) + " The window mode and the resolution stay as they are.";
            if (ui::button(L, kNames[p], d.c_str())) {
                graphics_preset(p, settings().s);
                video_low_res();
                if (!vsync) { vsync = true; configure(); }
                video_resample();
                persist();
            }
        }
    }

    // Retail's switches CleriaCore's renderer cannot honour yet: the value is what CleriaCore does; the
    // description names the original option and the value saved for the original game (kept in yso_win.ini,
    // not editable here).
    ui::section(L, "Not available yet");
    {
        auto lvl = [](int x) { static const char* k[] = {"Off", "Low", "High"}; return std::string(k[std::clamp(x, 0, 2)]); };
        auto onoff = [](bool x) { return std::string(x ? "On" : "Off"); };
        auto row = [&](const char* label, const char* value, const char* what, const std::string& saved) {
            const std::string d = std::string(what) + " Saved for the original game: " + saved + ".";
            ui::choice_label(L, label, value, d.c_str(), false);
        };
        row("Anti-aliasing (MSAA)", "Off",
            "Multisample anti-aliasing (Original: FSAA Type). Not available yet: every world pipeline would need a "
            "multisampled twin. Smooth edges, or Render scale above 100%, smooths edges instead.",
            gr.multi_sample_type >= 2 ? std::to_string(gr.multi_sample_type) + " samples" : std::string("none"));
        row("Mipmapping", "Off",
            "Smaller copies of each texture for distant surfaces (Original: Mipmapping). The retail switch is not "
            "read: Smooth distant textures above is CleriaCore's own.", onoff(gr.mipmap != 0));
        row("Shadows", "Full",
            "Shadow detail (Original: Shadows). Not available yet: what each level removes is not traced, so CleriaCore "
            "always draws its shadows.", lvl(gr.shadow_effect_level));
        row("Reflections", "None",
            "Water reflections (Original: Reflections). Not available yet: CleriaCore has no reflection pass.",
            lvl(gr.water_effect_level));
        row("Glare", "None", "Glare / bloom (Original: Glare). Not available yet: CleriaCore has no glare pass.",
            lvl(gr.glare_effect_level));
        row("Water caustics", "None",
            "The light patterns under water (Original: Disable Water Caustics). Not available yet.",
            gr.water_caustics ? "disabled" : "enabled");
        row("Other simplification", "Off",
            "Lighter effects for slow PCs (Original: Other Simplification). Not available yet.",
            onoff(gr.other_light_weight_mode != 0));
        row("Increase text resolution", "On",
            "The high-resolution text art (Original: Increase Text Resolution). CleriaCore always uses it.",
            onoff(settings().s.kernel.high_reso_text != 0));
        row("Direct3D 9 options", "n/a",
            "Triple buffering, 32-bit colour, compressed textures, hardware vertex / pixel shaders, the timer source: "
            "Direct3D 9 settings "
            "with no meaning for CleriaCore's renderer.",
            "triple buffering " + onoff(gr.triple_buffer != 0) + ", 32-bit " + onoff(gr.back_buffer_format32 != 0));
    }
}

// ---- CLERIA_DRIVE ------------------------------------------------------------------------------------
// vopt=Key=value: a Video setting for this run -- RenderScale, FpsCap, PerfGraph, SmoothTextures, SmoothEdges,
// CrispSprites ([cleria.video]; CLERIA_VOPT sets them from the start) or retail's
// GammaValue, ShowFPS, StretchAspect, DisableMovies ([graphics]), and [cleria.qol] EnemyHealthBars (0 / 1 / 2).
// vstat: what the passes did last frame.
static bool video_drive(const std::string& cmd) {
    if (cmd.rfind("vopt=", 0) == 0) {
        const std::string kv = cmd.substr(5);
        const std::size_t eq = kv.find('=');
        const std::string k = eq == std::string::npos ? kv : kv.substr(0, eq);
        const float x = eq == std::string::npos ? 0.0f : float(std::atof(kv.c_str() + eq + 1));
        auto& gr = settings().s.graphics;
        bool ok = true;
        if (k == "RenderScale") vid().render_scale = video::clamp_scale(x);
        else if (k == "FpsCap") vid().fps_cap = x <= 0 ? 0 : std::clamp(int(x), video::kFpsCapMin, video::kFpsCapMax);
        else if (k == "PerfGraph") vid().perf_graph = x != 0;
        else if (k == "SmoothTextures") g_world_mips = vid().smooth_textures = x != 0;   // from the next room load
        else if (k == "SmoothEdges") vid().smooth_edges = x != 0;
        else if (k == "CrispSprites") g_sprite_point = vid().crisp_sprites = x != 0;     // sprites uploaded from now
        else if (k == "GammaValue") gr.gamma_value = x;
        else if (k == "ShowFPS") gr.show_fps = std::clamp(int(x), 0, 2);
        else if (k == "StretchAspect") gr.stretch_aspect = x != 0;
        else if (k == "DisableMovies") gr.disable_movies = x != 0;
        else if (k == "EnemyHealthBars") settings().s.cleria.enemy_health_bars = std::clamp(int(x), 0, 2);   // [cleria.qol]
        else if (k == "TextureFilter") { gr.texture_filter = std::clamp(int(x), 0, 2); video_resample(); }
        else if (k == "Anisotropy") { gr.anisotropy = std::clamp(int(x), 1, 16); video_resample(); }
        else if (k == "Preset") { graphics_preset(std::clamp(int(x), 0, 3), settings().s); video_resample(); video_low_res(); }
        else if (k == "LowResoTexture") { gr.low_reso_texture = x != 0; video_low_res(); }
        else if (k == "BloodyEffectLevel") settings().s.game.bloody_effect_level = std::clamp(int(x), 0, 2);   // [game]
        // The other pages' retail keys, as the shell changes them (touch(): the mapper, the Options copy and
        // the audio re-read the settings): [sound] PlayBgm / PlayEffect, [input] DashControl / AtkBtnMagic /
        // AlwaysDashOK / MouseButtonAlwaysOkCancel.
        else if (k == "PlayBgm") { settings().s.sound.play_bgm = x != 0; settings().touch(); }
        else if (k == "PlayEffect") { settings().s.sound.play_effect = x != 0; settings().touch(); }
        else if (k == "SoundDevice") { settings().s.sound.device = std::max(0, int(x)); settings().touch(); }   // [sound] Device
        else if (k == "DashControl") { settings().s.input.dash_control = x != 0; settings().touch(); }
        else if (k == "AtkBtnMagic") { settings().s.input.atk_btn_magic = x != 0; settings().touch(); }
        else if (k == "AlwaysDashOK") { settings().s.input.always_dash_ok = x != 0; settings().touch(); }
        else if (k == "MouseButtonAlwaysOkCancel") { settings().s.input.mouse_button_always_ok_cancel = x != 0; settings().touch(); }
        else ok = false;
        std::printf("  [video] opt %s%s\n", kv.c_str(), ok ? "" : " (unknown key)");
        return true;
    }
    if (cmd == "vstat") {
        const float gv = settings().s.graphics.gamma_value;
        const int cap = vid().fps_cap;
        std::printf("  [video] world %ux%u scale %.2f gamma %.2f post %s aspect %.4f fps %d tps %d cap %d %s | looks: "
                    "textures %d edges %d (target %s) crisp %d | sampler %s%s x%d blood %d\n",
                    g_vgpu.world_w, g_vgpu.world_h, double(vid().render_scale), double(gv),
                    g_vgpu.copy_ok && !video::gamma_is_identity(gv) ? "on" : "off",
                    double(video_aspect(g_vgpu.world_w, g_vgpu.world_h)), g_vstats.fps, g_vstats.tps, cap,
                    cap <= 0 ? "uncapped" : g_vstats.fps <= cap + 1 ? "held" : "over",
                    int(g_world_mips), int(vid().smooth_edges), g_vgpu.wtex ? "on" : "off", int(g_sprite_point),
                    g_world_sampler.linear ? "linear" : "point", g_world_sampler.mip_linear ? "+mip" : "",
                    int(g_world_sampler.anisotropy), int(settings().s.game.bloody_effect_level));
        if (g_low_res_textures) std::printf("  [video] low-resolution textures on\n");
        return true;
    }
    return false;
}
