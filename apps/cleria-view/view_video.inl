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
// The glare (FUN_005fd750, SCREEN_EFFECTS.md 2.4). fs_bright: the scene x its mask, weight 1 - alpha (stage 0
// BLENDTEXTUREALPHA(TFACTOR black, TEXTURE), alpha TEXTURE|COMPLEMENT). fs_blur: FUN_005fcc90's four taps at
// +-(u.x, 0) and +-(0, u.y), each x L / 255 (u.z), summed stage by stage (MULTIPLYADD saturates), alpha 1
// (stage 0's alpha is the diffuse's); u.w = 1 turns the source over in v (the helper's flag 4: a boss shadow's
// first blur pass). fs_fade: the feedback quad, black at alpha u.x.
@fragment fn fs_bright(v: VO) -> @location(0) vec4f {
    let c = textureSample(src, smp, v.uv);
    return vec4f(c.rgb * (1.0 - c.a), 1.0 - c.a);
}
@fragment fn fs_blur(v: VO) -> @location(0) vec4f {
    let c = vec2f(v.uv.x, mix(v.uv.y, 1.0 - v.uv.y, u.w));
    let s = textureSample(src, smp, c - vec2f(u.x, 0.0)).rgb + textureSample(src, smp, c + vec2f(u.x, 0.0)).rgb +
            textureSample(src, smp, c - vec2f(0.0, u.y)).rgb + textureSample(src, smp, c + vec2f(0.0, u.y)).rgb;
    return vec4f(min(s * u.z, vec3f(1.0)), 1.0);
}
@fragment fn fs_fade(v: VO) -> @location(0) vec4f { return vec4f(0.0, 0.0, 0.0, u.x); }
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
    // [graphics] MultiSampleType: the world's multisampled colour + depth, resolved into wtex.
    // The world's other pipeline sets: bit 0 = multisampled, bit 1 = with the glare mask; [0] is unused (g's own).
    WorldPipes pipes[4]{};
    bool pipes_built[4]{}, msaa_on = false;
    int pipes_on = 0;                            // the set swapped into g for the world's passes
    // [graphics] GlareEffectLevel: the quarter-size targets of FUN_005fd750 (pool surfaces 2, 3, 4)
    WGPURenderPipeline bright{}, blur{}, fade{}, comp_add{}, comp_sub{};
    WGPUTextureView gview[3]{};                  // surfaces 2, 3, 4
    WGPUBindGroup gbind[3]{};
    WGPUBuffer gubuf[5]{};                       // a blur step's (0..3) and the feedback quad's (4) constants
    WGPUBindGroup gstep[2][4]{}, gfade{};        // [level - 1][step]: the step's source with its constants
    std::uint32_t gw = 0, gh = 0;
    int glare_level = 0;                         // this frame's level (0 = no glare this frame)
    bool glare_grabbed = false;
    WGPUTextureView mview{}, mdepth{};
    std::uint32_t mw = 0, mh = 0;
    // [graphics] WaterEffectLevel: the frame grabbed before the water layer (pool surface 1)
    WGPUTextureView grab{};
    std::uint32_t grab_w = 0, grab_h = 0;
    // ... and the water's reflection or the mirror floors' image (pool surface 5) with its own depth; the
    // reflection is drawn with the reflected view's set
    WGPUTextureView refl{}, refl_depth{};
    std::uint32_t refl_w = 0, refl_h = 0;
    bool refl_new = false;                       // made this frame: the water's bind groups are made again
    WorldPipes refl_pipes{};
    bool refl_pipes_built = false;
    // the post pass's copy of the frame
    WGPUTexture ptex{};
    WGPUTextureView pview{};
    WGPUBindGroup pbind{};
    std::uint32_t pw = 0, ph = 0;
    Gpu* gpu = nullptr;                          // for video_resample (the page and the drive have no Gpu)
    std::string formats;                         // the ShowFPS 2 suffix (FUN_005d4820's format names)
};
static VideoGpu g_vgpu;

static WGPUBindGroup video_bind(Gpu& g, WGPUTextureView v, WGPUSampler s, WGPUBuffer ubuf = nullptr) {
    WGPUBindGroupEntry e[3] = {};
    e[0].binding = 0; e[0].textureView = v;
    e[1].binding = 1; e[1].sampler = s;
    e[2].binding = 2; e[2].buffer = ubuf ? ubuf : g_vgpu.ubuf; e[2].size = 16;
    WGPUBindGroupDescriptor d = {};
    d.layout = g_vgpu.bgl; d.entryCount = 3; d.entries = e;
    return wgpuDeviceCreateBindGroup(g.device, &d);
}

// Before build_pipeline: the world sampler from [graphics] TextureFilter / Anisotropy, and the passes.
// `copy_ok`: the frame can be copied (the gamma pass reads a copy of it).
static bool video_drive(const std::string& cmd);
static void video_low_res();
// [graphics] Mipmap (0x765172) and "Smooth distant textures" -> g_world_mips, for the rooms loaded from now on.
static void video_mips() {
    g_world_mips = vid().smooth_textures ? 2 : settings().s.graphics.mipmap != 0 ? 1 : 0;
}
static void video_init(Gpu& g, WGPUTextureFormat fmt, bool copy_ok) {
    g_vgpu.gpu = &g;
    // CLERIA_VOPT="Key=value;Key=value": vopt settings from the start (a headless run's settings are
    // memory-only, and SmoothTextures / CrispSprites are read when a room / a sprite loads).
    if (const char* e = std::getenv("CLERIA_VOPT")) {
        std::stringstream ss(e);
        for (std::string kv; std::getline(ss, kv, ';');) if (!kv.empty()) video_drive("vopt=" + kv);
    }
    video_mips();
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
    {   // the glare passes: the bright pass replaces; the blur and the composite add (SRCALPHA / ONE with a
        // source alpha of 1), or subtract (REVSUBTRACT); the feedback quad fades (SRCALPHA / INVSRCALPHA)
        fs.entryPoint = sv("fs_bright");
        g_vgpu.bright = wgpuDeviceCreateRenderPipeline(g.device, &pd);
        WGPUBlendState ab = {};
        ab.color = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
        ab.alpha = ab.color;
        tgt.blend = &ab;
        fs.entryPoint = sv("fs_blur");
        g_vgpu.blur = wgpuDeviceCreateRenderPipeline(g.device, &pd);
        tgt.writeMask = WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue;
        fs.entryPoint = sv("fs_copy");
        g_vgpu.comp_add = wgpuDeviceCreateRenderPipeline(g.device, &pd);
        ab.color.operation = WGPUBlendOperation_ReverseSubtract;
        g_vgpu.comp_sub = wgpuDeviceCreateRenderPipeline(g.device, &pd);
        ab.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
        fs.entryPoint = sv("fs_fade");
        g_vgpu.fade = wgpuDeviceCreateRenderPipeline(g.device, &pd);
    }
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

// `pipe` null: only the clear. `load`: draw over what the target holds.
static void video_fullscreen_pass(Gpu& g, WGPURenderPipeline pipe, WGPUBindGroup bind, WGPUTextureView dst,
                                  WGPUCommandEncoder enc, bool load = false) {
    WGPURenderPassColorAttachment ca = {};
    ca.view = dst; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = load ? WGPULoadOp_Load : WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {0, 0, 0, pipe ? 1.0 : 0.0};
    WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    if (pipe) {
        wgpuRenderPassEncoderSetPipeline(pass, pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    }
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
    const bool msaa = video::msaa_samples(settings().s.graphics.multi_sample_type) > 1;
    // The glare needs the world in its own target: its alpha is the mask, and the passes read it.
    g_glare_cfg = std::clamp(int(settings().s.graphics.glare_effect_level), 0, 2);
    g_vgpu.glare_level = 0;
    g_vgpu.glare_grabbed = false;
    if (!g_vgpu.ready || cw == 0 || ch == 0) return;
    const bool glare = g_glare_cfg != 0 && g_vgpu.blur && g_vgpu.comp_sub && g_vgpu.fade;
    g_water.on = false;                                  // the water reads a copy of the world: its own target too
    if (video::scale_is_native(s) && !vid().smooth_edges && !msaa && !glare && !g_water.want) return;
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
    if (glare) g_vgpu.glare_level = g_glare_cfg;
    // The world's passes draw with another set of the world pipelines when the target is multisampled or its
    // alpha is the glare mask; video_world_resolve puts g's own back.
    const int set = (msaa ? 1 : 0) | (glare ? 2 : 0);
    if (set == 0) return;
    if (!g_vgpu.pipes_built[set]) {
        const WorldPipes keep = g;
        static_cast<WorldPipes&>(g) = WorldPipes{};
        build_pipeline(g, g_vgpu.fmt, msaa ? video::kMsaaSamples : 1, glare);
        g_vgpu.pipes[set] = g;
        static_cast<WorldPipes&>(g) = keep;
        g_vgpu.pipes_built[set] = true;
    }
    std::swap(static_cast<WorldPipes&>(g), g_vgpu.pipes[set]);
    g_vgpu.pipes_on = set;
    if (!msaa) return;
    // MSAA: a multisampled colour + depth; video_world_resolve puts the result in wtex before the screen
    // passes that read it.
    if (!g_vgpu.mview || g_vgpu.mw != e.w || g_vgpu.mh != e.h) {
        if (g_vgpu.mview) wgpuTextureViewRelease(g_vgpu.mview);
        if (g_vgpu.mdepth) wgpuTextureViewRelease(g_vgpu.mdepth);
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_RenderAttachment;
        td.dimension = WGPUTextureDimension_2D; td.size = {e.w, e.h, 1};
        td.format = g_vgpu.fmt; td.mipLevelCount = 1; td.sampleCount = video::kMsaaSamples;
        WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);
        g_vgpu.mview = wgpuTextureCreateView(t, nullptr);
        wgpuTextureRelease(t);
        g_vgpu.mdepth = make_depth(g, e.w, e.h, video::kMsaaSamples);
        g_vgpu.mw = e.w; g_vgpu.mh = e.h;
    }
    view = g_vgpu.mview;
    depth = g_vgpu.mdepth;
    g_vgpu.msaa_on = true;
}
// The multisampled world into the world target. `keep`: the world goes on drawing after it.
static void video_msaa_resolve(Gpu& g, bool keep) {
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = g_vgpu.mview; ca.resolveTarget = g_vgpu.wview; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Load; ca.storeOp = keep ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
    WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    video_submit(g, enc);
}
// After the world's last 3D pass: g's own pipelines are put back, and a multisampled world is resolved into
// the world target, which the screen passes after it (the glare, FireBlur, the Mask of Eyes, ZoomBlur) read
// and draw on.
static void video_world_resolve(Gpu& g, WGPUTextureView& view, WGPUTextureView& depth) {
    if (g_vgpu.pipes_on) std::swap(static_cast<WorldPipes&>(g), g_vgpu.pipes[g_vgpu.pipes_on]);
    g_vgpu.pipes_on = 0;
    if (!g_vgpu.msaa_on) return;
    g_vgpu.msaa_on = false;
    video_msaa_resolve(g, false);
    view = g_vgpu.wview;
    depth = g_vgpu.wdepth;
}

// ---- the glare: [graphics] GlareEffectLevel (FUN_005fd750; build/render_re/SCREEN_EFFECTS.md 2) -------
// The world target's alpha is the mask (weight 1 - alpha). After the actors' layer the frame is taken down
// to a quarter (video_glare_grab = FUN_005fc8f0(3) after layer 6); before the screen effects the masked colour
// is blurred into an accumulation target that is added to the world (video_glare). Level 1 and 2 differ by
// their blur pass tables. [not modelled] level 2's specular path (*param_9, the PS11_SPTOA shaders), the
// actors' mask quads (FUN_00475230), Darm's layer (0x7b2e70).
// The blur pass tables [BYTES 0x5fe047..0x5fe175]: source, target (0, 1, 2 = surfaces 2, 3, 4), k.
struct GlareStep { int src, dst, k; };
static const GlareStep kGlareSteps[2][4] = {{{0, 1, 1}, {1, 0, 2}, {0, 2, 3}, {0, 0, 0}},
                                            {{0, 1, 1}, {1, 0, 2}, {0, 1, 1}, {1, 2, 3}}};
static void video_glare_grab(Gpu& g) {
    if (!g_vgpu.world_on || g_vgpu.glare_level == 0) return;
    const std::uint32_t qw = std::max(1u, g_vgpu.world_w / 4), qh = std::max(1u, g_vgpu.world_h / 4);   // [BYTES 0x5e3816..0x5e3838]
    if (!g_vgpu.gview[0] || g_vgpu.gw != qw || g_vgpu.gh != qh) {
        for (int i = 0; i < 3; ++i) {
            if (g_vgpu.gbind[i]) wgpuBindGroupRelease(g_vgpu.gbind[i]);
            if (g_vgpu.gview[i]) wgpuTextureViewRelease(g_vgpu.gview[i]);
            WGPUTextureDescriptor td = {};
            td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
            td.dimension = WGPUTextureDimension_2D; td.size = {qw, qh, 1};
            td.format = g_vgpu.fmt; td.mipLevelCount = 1; td.sampleCount = 1;
            WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);
            g_vgpu.gview[i] = wgpuTextureCreateView(t, nullptr);
            wgpuTextureRelease(t);
            g_vgpu.gbind[i] = video_bind(g, g_vgpu.gview[i], g_vgpu.linear);
        }
        for (int i = 0; i < 5; ++i)
            if (!g_vgpu.gubuf[i]) {
                WGPUBufferDescriptor bd = {}; bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst; bd.size = 16;
                g_vgpu.gubuf[i] = wgpuDeviceCreateBuffer(g.device, &bd);
            }
        for (int lv = 0; lv < 2; ++lv)
            for (int i = 0; i < 4; ++i) {
                if (g_vgpu.gstep[lv][i]) wgpuBindGroupRelease(g_vgpu.gstep[lv][i]);
                g_vgpu.gstep[lv][i] = video_bind(g, g_vgpu.gview[kGlareSteps[lv][i].src], g_vgpu.linear, g_vgpu.gubuf[i]);
            }
        if (g_vgpu.gfade) wgpuBindGroupRelease(g_vgpu.gfade);
        g_vgpu.gfade = video_bind(g, g_vgpu.gview[1], g_vgpu.linear, g_vgpu.gubuf[4]);
        g_vgpu.gw = qw; g_vgpu.gh = qh;
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        video_fullscreen_pass(g, nullptr, nullptr, g_vgpu.gview[2], enc);   // a new accumulation target is empty
        video_submit(g, enc);
    }
    if (g_vgpu.msaa_on) video_msaa_resolve(g, true);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    video_fullscreen_pass(g, g_vgpu.copy, g_vgpu.wbind, g_vgpu.gview[1], enc);   // StretchRect -> surface 3 [H] linear
    video_submit(g, enc);
    g_vgpu.glare_grabbed = true;
}
// ---- the water's frame grab: [graphics] WaterEffectLevel (build/render_re/REFLECTIONS.md 2, 5) ----------
// FUN_005fc8f0(1) before layer 0xb: the frame so far into pool surface 1, the render size at level 2 and half
// of it otherwise (c = (level != 2) + 1 [BYTES 0x5e3761..0x5e378a]), filtered (StretchRect LINEAR where the
// device can). The water's draw reads it (draw()'s water path, main.cpp's water()).
// [approx] the sprite effects (retail's layer 8 buckets) are drawn after the water here, so they are not in it.
static void video_water_grab(Gpu& g) {
    if (!g_vgpu.world_on || !g_water.want) return;
    const int c = settings().s.graphics.water_effect_level != 2 ? 2 : 1;
    const std::uint32_t w = std::max(1u, g_vgpu.world_w / std::uint32_t(c)), h = std::max(1u, g_vgpu.world_h / std::uint32_t(c));
    if (!g.dudv_bind) {                                  // the du/dv map: V8U8 (signed), level 0; the stage wraps
        const std::vector<std::uint8_t> b = read_bytes(g_water.dudv);
        if (b.size() < 128 + 256 * 256 * 2 || b[0] != 'D' || b[84] != 0 || b[88] != 16) return;   // no fourcc, 16 bits
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = {256, 256, 1};
        td.format = WGPUTextureFormat_RG8Snorm; td.mipLevelCount = 1; td.sampleCount = 1;
        WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);
        WGPUTexelCopyTextureInfo dst = {}; dst.texture = t; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout = {}; layout.bytesPerRow = 256 * 2; layout.rowsPerImage = 256;
        WGPUExtent3D ext = {256, 256, 1};
        wgpuQueueWriteTexture(g.queue, &dst, &b[128], 256 * 256 * 2, &layout, &ext);
        g.dudv_bind = tex_bindgroup(g, wgpuTextureCreateView(t, nullptr));
    }
    if (!g_vgpu.grab || g_vgpu.grab_w != w || g_vgpu.grab_h != h || g_vgpu.refl_new) {
        g_vgpu.refl_new = false;
        if (g.ubind_water) wgpuBindGroupRelease(g.ubind_water);
        if (g.ubind_refl) wgpuBindGroupRelease(g.ubind_refl);
        if (g_vgpu.grab) wgpuTextureViewRelease(g_vgpu.grab);
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D; td.size = {w, h, 1};
        td.format = g_vgpu.fmt; td.mipLevelCount = 1; td.sampleCount = 1;
        WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);
        g_vgpu.grab = wgpuTextureCreateView(t, nullptr);
        wgpuTextureRelease(t);
        WGPUBindGroupEntry e[4] = {};                    // stage 1: the grab, ADDRESSU / V CLAMP
        e[0].binding = 0; e[0].buffer = g.ubuf_water; e[0].size = 128;
        e[1].binding = 1; e[1].textureView = g_vgpu.grab;
        e[2].binding = 2; e[2].sampler = g_vgpu.linear;
        e[3].binding = 3; e[3].textureView = g_vgpu.grab;   // water2 has no stage 3
        WGPUBindGroupDescriptor d = {};
        d.layout = g.u_layout; d.entryCount = 4; d.entries = e;
        g.ubind_water = wgpuDeviceCreateBindGroup(g.device, &d);
        e[0].buffer = g.ubuf_refl;                       // stage 3: the reflection, ADDRESSU / V CLAMP
        if (g_vgpu.refl) e[3].textureView = g_vgpu.refl;
        g.ubind_refl = wgpuDeviceCreateBindGroup(g.device, &d);
        g_vgpu.grab_w = w; g_vgpu.grab_h = h;
    }
    if (g_vgpu.msaa_on) video_msaa_resolve(g, true);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    video_fullscreen_pass(g, g_vgpu.copy, g_vgpu.wbind, g_vgpu.grab, enc);
    video_submit(g, enc);
    g_water.on = true;
}
// ---- the water's reflection pass (FUN_005dfea0 [BYTES 0x5dff0f..0x5e008b]; REFLECTIONS.md 2, 4) ----------
// First in the frame, when a loaded model has a 0x200000 material (0x7b2e3c) and WaterEffectLevel != 0: the
// world from the reflected view (`rvp`: Camera::reflected_view_proj) into pool surface 5 -- the render size
// over 2c, c = (level != 2) + 1 [BYTES 0x5e3761..0x5e378a] -- with its own depth, cleared to 0xff000000, the
// culled materials culling the other winding. Retail calls the layers 2, 3, 4, 5, 7, 9, 12, 13 there
// [BYTES 0x5dff52..0x5e007f: skipped 6, 8, 10, 11, 14, 15]; drawn here: the room and its objects (draw()'s
// part 3 = layers 3 / 5, and the objects of 4), and -- between _begin and _end, by the caller -- the effects: an
// effect element draws at once from its layer's callback (FUN_0044cbd0 -> FUN_0044f2e0: its own upload and
// DrawPrimitive, or FUN_005fafc0 for a mesh element [EXPORT 0044f2e0.c:197-241, 406]), on layer 4 / 12 / 13
// [EXPORT 0044ef00.c], so it is in this pass. [not modelled] in the reflection: an actor's glare-alpha body (9)
// and sprite overlay (12; CleriaCore composites the overlay into the body's texture), boss02's attachment (7);
// the generated mip chain of surface 5 (it is sampled magnified); the reflected eye of the sort keys.
// _begin leaves the reflected view's pipelines and constants in place and returns true; _end puts g's own back
// (the caller writes the frame's constants after it).
// Pool surface 5 for a cw x ch frame: the render size over 2c, c = (level != 2) + 1 -- half at Reflections
// High, a quarter at Low and at Off [BYTES 0x5e3761..0x5e378a, 0x5e37f3..0x5e380f].
static void video_surface5(Gpu& g, std::uint32_t cw, std::uint32_t ch) {
    const video::Extent e = video::scaled_extent(cw, ch, vid().render_scale, g_vgpu.max_dim);
    const std::uint32_t c = settings().s.graphics.water_effect_level != 2 ? 4 : 2;
    const std::uint32_t w = std::max(1u, e.w / c), h = std::max(1u, e.h / c);
    if (g_vgpu.refl && g_vgpu.refl_w == w && g_vgpu.refl_h == h) return;
    if (g_vgpu.refl) wgpuTextureViewRelease(g_vgpu.refl);
    if (g_vgpu.refl_depth) wgpuTextureViewRelease(g_vgpu.refl_depth);
    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    td.dimension = WGPUTextureDimension_2D; td.size = {w, h, 1};
    td.format = g_vgpu.fmt; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);
    g_vgpu.refl = wgpuTextureCreateView(t, nullptr);
    wgpuTextureRelease(t);
    g_vgpu.refl_depth = make_depth(g, w, h);
    g_vgpu.refl_w = w; g_vgpu.refl_h = h;
    g_vgpu.refl_new = true;
    if (g.ubind_mirror) wgpuBindGroupRelease(g.ubind_mirror);
    WGPUBindGroupEntry be[4] = {};                       // a mirror floor's stage 0: the image, ADDRESSU / V CLAMP
    be[0].binding = 0; be[0].buffer = g.ubuf_mirror; be[0].size = 128;
    be[1].binding = 1; be[1].textureView = g_vgpu.refl;
    be[2].binding = 2; be[2].sampler = g_vgpu.linear;
    be[3].binding = 3; be[3].textureView = g_vgpu.refl;
    WGPUBindGroupDescriptor d = {};
    d.layout = g.u_layout; d.entryCount = 4; d.entries = be;
    g.ubind_mirror = wgpuDeviceCreateBindGroup(g.device, &d);
}
static bool video_water_reflect_begin(Gpu& g, const Room& room, std::uint32_t cw, std::uint32_t ch, const Mat4& rvp) {
    g_water.ron = false;
    if (!g_vgpu.ready || !g_water.rwant || cw == 0 || ch == 0) return false;
    video_surface5(g, cw, ch);
    if (!g_vgpu.refl_pipes_built) {
        const WorldPipes keep = g;
        static_cast<WorldPipes&>(g) = WorldPipes{};
        build_pipeline(g, g_vgpu.fmt, 1, false, true);
        g_vgpu.refl_pipes = g;
        static_cast<WorldPipes&>(g) = keep;
        g_vgpu.refl_pipes_built = true;
    }
    std::swap(static_cast<WorldPipes&>(g), g_vgpu.refl_pipes);
    write_mvp(g, rvp);
    draw(g, room, g_vgpu.refl, g_vgpu.refl_depth, 3);
    return true;
}
static void video_water_reflect_end(Gpu& g) {
    std::swap(static_cast<WorldPipes&>(g), g_vgpu.refl_pipes);
    g_water.ron = true;
}
// ---- the mirror floors' pass (FUN_005dfea0 [EXPORT 005dfea0.c]; REFLECTIONS.md 7.6) ------------------------
// Retail runs it before the main pass when layer 0 or 1 holds anything: the normal camera, surface 5 cleared to
// 0xff000000, then layer 0 (the `B` models; the mirrored chests / boxes / statues at level >= 1), layer 5
// (filtered to nothing in the shipped rooms) and layer 1 (the actors' floor images; the `B` models' additive
// subsets, filtered out). Here it runs after the actors are drawn (their images are collected while they are)
// and before the floors of layer 0xb read it; `vp` = the frame's view-projection, written back at the end.
// The mirrored objects (FUN_00562650 [EXPORT 00562650.c]): world x D3DXMatrixReflect(the plane through the
// object's position, normal +y), every subset, the cull mode unchanged -- so with the winding turned over.
// [approx] the object's placed position stands for its current one. [not modelled] layer 5's alpha stamp
// (S_9110's test floor only). Zava's quad (view_boss_shadow.inl) is among the quads.
static void video_mirror_pass(Gpu& g, const Room& room, std::uint32_t cw, std::uint32_t ch, const Mat4& vp) {
    g_mirror.on = false;
    std::vector<Mirror::Quad> quads;
    quads.swap(g_mirror.quads);
    bool want = false;
    for (const std::uint8_t k : mirror_kinds(room)) want = want || k != 0;
    for (const DrawBatch& b : room.mesh.batches) want = want || (b.index_count && (b.mat_flags & 0x800000u));
    if (!g_vgpu.ready || !want || cw == 0 || ch == 0) return;
    video_surface5(g, cw, ch);
    const int set = g_vgpu.pipes_on;                     // the image is single-sampled, its alpha is not read
    if (set) std::swap(static_cast<WorldPipes&>(g), g_vgpu.pipes[set]);
    draw(g, room, g_vgpu.refl, g_vgpu.refl_depth, 4);
    g_mirror.stat[0] = 0;
    auto is = [&](const char* n) {
        return room.name.size() == 6 && std::equal(room.name.begin(), room.name.end(), n,
                                                   [](char a, char b) { return std::toupper((unsigned char)a) == b; });
    };
    if (settings().s.graphics.water_effect_level >= 1 && (is("S_6014") || is("S_6082"))) {
        static const char* const kNames[] = {"takara_00", "box_00", "box_01", "box_02", "box_03", "save_01", "save_02", "save_03"};
        for (const RoomMesh::ObjRange& r : room.mesh.object_ranges) {
            if (std::find_if(std::begin(kNames), std::end(kNames), [&](const char* n) { return r.name == n; }) == std::end(kNames) ||
                (r.model_flags & 0x100u)) continue;
            Mat4 refl;
            refl.m[5] = -1.0f; refl.m[13] = 2.0f * r.obj.position.y;
            write_mvp(g, refl * vp);
            g_mirror.v0 = r.v0; g_mirror.v1 = r.v1;
            draw(g, room, g_vgpu.refl, g_vgpu.refl_depth, 5);
            ++g_mirror.stat[0];
        }
        if (g_mirror.stat[0]) write_mvp(g, vp);
    }
    {
        QuadScope scope(g);
        for (const Mirror::Quad& q : quads)
            draw_quad(g, g_vgpu.refl, g_vgpu.refl_depth, q.add ? g.char_pipeline_add : g.floor_quad_pipe, q.bind, q.v);
    }
    g_mirror.stat[1] = int(quads.size());
    if (set) std::swap(static_cast<WorldPipes&>(g), g_vgpu.pipes[set]);
    g_mirror.on = true;
}
// `level` = the blur gain byte L (0x7b2e39), `feedback` = F (0x7b2e38), `subtract` = 0x7b2e3a. `world` = the
// world target's view (single-sampled: after video_world_resolve).
static void video_glare(Gpu& g, WGPUTextureView world, int level, int feedback, bool subtract) {
    if (!g_vgpu.glare_grabbed) return;
    g_vgpu.glare_grabbed = false;
    // Every pass in one submit; each blur step and the feedback quad has its own constants.
    const int lv = g_vgpu.glare_level >= 2 ? 1 : 0, n = lv ? 4 : 3;
    const GlareStep* st = kGlareSteps[lv];
    for (int i = 0; i < n; ++i) {
        const float u[4] = {float(st[i].k) * 0.5f / float(g_vgpu.gw), float(st[i].k) * 0.5f / float(g_vgpu.gh),
                            float(std::clamp(level, 0, 255)) / 255.0f, 0};
        wgpuQueueWriteBuffer(g.queue, g_vgpu.gubuf[i], 0, u, 16);
    }
    const int F = std::clamp(feedback, 0, 255);
    const float uf[4] = {float(255 - F) / 255.0f, 0, 0, 0};
    wgpuQueueWriteBuffer(g.queue, g_vgpu.gubuf[4], 0, uf, 16);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    // A. the bright pass: surface 3 -> surface 2
    video_fullscreen_pass(g, g_vgpu.bright, g_vgpu.gbind[1], g_vgpu.gview[0], enc);
    // B. the feedback (FUN_005fc990): F = 0 clears the accumulation, else it keeps F / 255 of it
    if (F == 0) video_fullscreen_pass(g, nullptr, nullptr, g_vgpu.gview[2], enc);
    else video_fullscreen_pass(g, g_vgpu.fade, g_vgpu.gfade, g_vgpu.gview[2], enc, true);
    // C. the blur: surface 3 cleared, then the level's passes, each adding into its target
    video_fullscreen_pass(g, nullptr, nullptr, g_vgpu.gview[1], enc);
    for (int i = 0; i < n; ++i)
        video_fullscreen_pass(g, g_vgpu.blur, g_vgpu.gstep[lv][i], g_vgpu.gview[st[i].dst], enc, true);
    // D. the composite: the accumulation added to (or taken from) the world
    video_fullscreen_pass(g, subtract ? g_vgpu.comp_sub : g_vgpu.comp_add, g_vgpu.gbind[2], world, enc, true);
    video_submit(g, enc);
}
static void video_world_end(Gpu& g, WGPUTextureView frame_view, WGPUTextureView frame_depth,
                            WGPUTextureView& view, WGPUTextureView& depth) {
    if (!g_vgpu.world_on) return;
    video_world_resolve(g, view, depth);
    const bool fxaa = vid().smooth_edges && g_vgpu.fxaa;
    if (fxaa) {
        const float u[4] = {1.0f, 1.0f / float(g_vgpu.world_w), 1.0f / float(g_vgpu.world_h), 0};
        wgpuQueueWriteBuffer(g.queue, g_vgpu.ubuf, 0, u, sizeof u);
    }
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    video_fullscreen_pass(g, fxaa ? g_vgpu.fxaa : g_vgpu.copy, g_vgpu.wbind, frame_view, enc);
    // CLERIA_SURFACE5=1 (a debug aid): pool surface 5 -- the water's reflection or the mirror floors' image --
    // over the frame instead of the world.
    static const bool show5 = std::getenv("CLERIA_SURFACE5") != nullptr;
    if (show5 && g_vgpu.refl) {
        WGPUBindGroup b5 = video_bind(g, g_vgpu.refl, g_vgpu.linear);
        video_fullscreen_pass(g, g_vgpu.copy, b5, frame_view, enc);
        wgpuBindGroupRelease(b5);
    }
    video_submit(g, enc);
    view = frame_view;
    depth = frame_depth;
    g_vgpu.world_on = false;
}
// The size the world draws at this frame (a depth buffer made mid-world must match it).
static std::uint32_t video_world_w(std::uint32_t cw) { return g_vgpu.world_on ? g_vgpu.world_w : cw; }
static std::uint32_t video_world_h(std::uint32_t ch) { return g_vgpu.world_on ? g_vgpu.world_h : ch; }
static std::uint32_t video_world_samples() { return g_vgpu.msaa_on ? std::uint32_t(video::kMsaaSamples) : 1u; }

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
// The sprite sheets likewise (FUN_00444a50: data/ani/l/*.aia, aia_low_res), for the sheets loaded from then on.
// [not modelled] the combination with CompressedTexture (another folder).
static void reindex_textures(Gpu& g);                    // main.cpp
static void video_low_res() {
    const bool on = settings().s.graphics.low_reso_texture != 0;
    if (on == g_low_res_textures) return;
    g_low_res_textures = on;
    aia_low_res(on);
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
        bool mm = gr.mipmap != 0;                        // 0x765172 (video_mips)
        if (ui::toggle(L, "Mipmapping", &mm,
                       "Uses the smaller copies stored with each floor, wall and object texture for surfaces far "
                       "from the camera: less shimmer when you move, a little softer in the distance. On is the "
                       "original's default. Takes effect in the next area you enter. (Original: Mipmapping.)")) {
            gr.mipmap = mm;
            video_mips();
            persist();
        }
        if (ui::toggle(L, "Smooth distant textures", &v.smooth_textures,
                       "Blends between those smaller copies and keeps floors seen at a steep angle sharp (trilinear "
                       "and at least 8x anisotropic filtering, whatever the two rows below say). Takes "
                       "effect in the next area you enter. A CleriaCore extra, not a retail option.")) {
            video_mips();
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
    {   // [graphics] MultiSampleType (0x765158): the back buffer's sample count in retail; here the 3D world's.
        static const char* kMsaa[] = {"Off", "4x"};
        int m = video::msaa_samples(gr.multi_sample_type) > 1 ? 1 : 0;
        if (ui::choice(L, "Anti-aliasing (MSAA)", &m, kMsaa, 2,
                       "Multisample anti-aliasing: smooths the edges of the scenery's geometry. Off is the original's "
                       "default. CleriaCore's renderer offers 4 samples; any sample count saved by the original's "
                       "tool counts as 4x. The HUD and the menus are not affected. (Original: FSAA Type.)")) {
            gr.multi_sample_type = m ? video::kMsaaSamples : 0;
            persist();
        }
    }
    {
        static const char* kFilters[] = {"Point", "Bilinear", "Trilinear"};
        int f = std::clamp(int(gr.texture_filter), 0, 2);
        if (ui::choice(L, "Texture filtering", &f, kFilters, 3,
                       "Point keeps texels sharp and blocky; bilinear smooths them (the original's default). "
                       "Trilinear also blends between mipmap levels (with Mipmapping on), which hides the line where "
                       "one level gives way to the next. Takes effect in the next area you enter. (Original: "
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
                       "Uses the game's smaller copies of the floor, wall and object textures and of the character "
                       "sprites: blurrier, lighter on slow PCs. Takes effect in the next area you enter, and for the "
                       "characters loaded from then on. (Original: Low-Resolution Textures.)")) {
            gr.low_reso_texture = lo;
            video_low_res();
            persist();
        }
    }
    {   // [graphics] GlareEffectLevel (0x76519a): video_glare
        static const char* kGlare[] = {"Off", "Low", "High"};
        int gl = std::clamp(int(gr.glare_effect_level), 0, 2);
        if (ui::choice(L, "Glare", &gl, kGlare, 3,
                       "The soft glow around lava, magic and other bright surfaces. High is the original's default; "
                       "Low blurs it a little less; Off draws none. (Original: Glare.)")) {
            gr.glare_effect_level = gl;
            persist();
        }
    }
    {   // [graphics] ShadowEffectLevel (0x765198): shadow_actor (view_sprites.inl), read every frame
        static const char* kShadow[] = {"Simple", "Player", "Full"};
        int sl = std::clamp(int(gr.shadow_effect_level), 0, 2);
        if (ui::choice(L, "Shadows", &sl, kShadow, 3,
                       "The round shadow under each character. Full (the original's default): it lies on the steps "
                       "and slopes it falls on. Player: only yours does; the others are flat discs. Simple: all are "
                       "flat discs. The big bosses' own shadows are drawn at every setting, coarser at Player and "
                       "Simple. Toal's trail of light is twice as long at Full. (Original: Shadows.)")) {
            gr.shadow_effect_level = sl;
            persist();
        }
    }
    {   // [graphics] WaterEffectLevel (0x765199): video_water_grab, draw()'s water path
        static const char* kWater[] = {"Off", "Low", "High"};
        int wl = std::clamp(int(gr.water_effect_level), 0, 2);
        if (ui::choice(L, "Reflections", &wl, kWater, 3,
                       "How water surfaces are drawn. High (the original's default): you see the floor and the "
                       "characters through the water, wobbling. Low: the same from a half-size picture. Off: a "
                       "plain see-through sheet. The one room whose water mirrors the room shows it at High and Low. "
                       "The mirror floors of the tower's upper part always mirror; at Off their picture is the "
                       "smaller one and the chests and statues are missing from it. (Original: Reflections.)")) {
            gr.water_effect_level = wl;
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
        bool wc = gr.water_caustics != 0;                // 0x76519c (g_caustic.off, read every frame)
        if (ui::toggle(L, "Disable water caustics", &wc,
                       "Off: in the flooded rooms, moving patterns of light play over the floors, the walls and the "
                       "characters (the original's default). On: they are not drawn; the bottoms of the shallow pools "
                       "keep theirs. (Original: Disable Water Caustics.)")) {
            gr.water_caustics = wc;
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
            "The original's Lightest preset: bilinear filtering, no anisotropic filtering, no anti-aliasing, "
            "low-resolution textures, no mipmaps, no glare, brightness 1.00, VSync on; and, saved for the original "
            "game: other simplification, no shadows or reflections.",
            "The original's Light preset: bilinear filtering, no anisotropic filtering, no anti-aliasing, low glare, "
            "brightness 1.00, VSync on; and, saved for the original game: low shadows and reflections.",
            "The original's Normal preset: trilinear filtering, 2x anisotropic filtering, no anti-aliasing, full "
            "glare, brightness 1.00, VSync on; and, saved for the original game: full shadows and reflections.",
            "The original's High-Definition preset: trilinear filtering, 4x anisotropic filtering, anti-aliasing, "
            "full glare, brightness 1.00, VSync on; and, saved for the original game: full shadows and reflections."};
        for (int p = 0; p < 4; ++p) {
            const std::string d = std::string(kDesc[p]) + " Every preset also turns off Stretch widescreen, Disable "
                                  "movies and Disable Mask of Eyes effect, as the original's do. The window mode and the "
                                  "resolution stay as they are.";
            if (ui::button(L, kNames[p], d.c_str())) {
                graphics_preset(p, settings().s);
                video_mips();
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
        auto onoff = [](bool x) { return std::string(x ? "On" : "Off"); };
        auto row = [&](const char* label, const char* value, const char* what, const std::string& saved) {
            const std::string d = std::string(what) + " Saved for the original game: " + saved + ".";
            ui::choice_label(L, label, value, d.c_str(), false);
        };
        row("Other simplification", "Off",
            "A lighter version of the final boss's room for slow PCs (Original: Other Simplification). Not available "
            "yet.", onoff(gr.other_light_weight_mode != 0));
        row("Increase text resolution", "On",
            "Text drawn at the screen's resolution instead of stretched from 1024 x 768 (Original: Increase Text "
            "Resolution). CleriaCore always draws it sharp.",
            onoff(settings().s.kernel.high_reso_text != 0));
        row("Minimum frame rate", "n/a",
            "(Original: Minimum Frame Rate.) The original game reads this value and never uses it, so there is "
            "nothing to apply.", std::to_string(settings().s.kernel.clamp_fr_min));
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
        else if (k == "SmoothTextures") { vid().smooth_textures = x != 0; video_mips(); }   // from the next room load
        else if (k == "Mipmap") { gr.mipmap = x != 0; video_mips(); }                       // likewise
        else if (k == "SmoothEdges") vid().smooth_edges = x != 0;
        else if (k == "CrispSprites") g_sprite_point = vid().crisp_sprites = x != 0;     // sprites uploaded from now
        else if (k == "GammaValue") gr.gamma_value = x;
        else if (k == "ShowFPS") gr.show_fps = std::clamp(int(x), 0, 2);
        else if (k == "StretchAspect") gr.stretch_aspect = x != 0;
        else if (k == "DisableMovies") gr.disable_movies = x != 0;
        else if (k == "EnemyHealthBars") settings().s.cleria.enemy_health_bars = std::clamp(int(x), 0, 2);   // [cleria.qol]
        else if (k == "TextureFilter") { gr.texture_filter = std::clamp(int(x), 0, 2); video_resample(); }
        else if (k == "MultiSampleType") gr.multi_sample_type = std::max(0, int(x));
        else if (k == "GlareEffectLevel") gr.glare_effect_level = std::clamp(int(x), 0, 2);
        else if (k == "WaterCaustics") g_caustic.off = (gr.water_caustics = x != 0) != 0;
        else if (k == "WaterEffectLevel") gr.water_effect_level = std::clamp(int(x), 0, 2);
        else if (k == "ShadowEffectLevel") gr.shadow_effect_level = std::clamp(int(x), 0, 2);
        else if (k == "Anisotropy") { gr.anisotropy = std::clamp(int(x), 1, 16); video_resample(); }
        else if (k == "Preset") { graphics_preset(std::clamp(int(x), 0, 3), settings().s); video_resample(); video_low_res(); video_mips(); }
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
                    "textures %d edges %d (target %s) crisp %d | sampler %s%s x%d blood %d msaa %d mips %d glare %d\n",
                    g_vgpu.world_w, g_vgpu.world_h, double(vid().render_scale), double(gv),
                    g_vgpu.copy_ok && !video::gamma_is_identity(gv) ? "on" : "off",
                    double(video_aspect(g_vgpu.world_w, g_vgpu.world_h)), g_vstats.fps, g_vstats.tps, cap,
                    cap <= 0 ? "uncapped" : g_vstats.fps <= cap + 1 ? "held" : "over",
                    int(g_world_mips == 2), int(vid().smooth_edges), g_vgpu.wtex ? "on" : "off", int(g_sprite_point),
                    g_world_sampler.linear ? "linear" : "point", g_world_sampler.mip_linear ? "+mip" : "",
                    int(g_world_sampler.anisotropy), int(settings().s.game.bloody_effect_level),
                    video::msaa_samples(settings().s.graphics.multi_sample_type), g_world_mips, g_vgpu.glare_level);
        if (g_low_res_textures) std::printf("  [video] low-resolution textures on\n");
        std::printf("  [video] caustics room %d off %d frame %d scroll %.4f %.5f\n", int(g_caustic.room()), int(g_caustic.off),
                    g_caustic.frame, double(g_caustic.sx), double(g_caustic.sy));
        std::printf("  [video] shadows level %d decals %d (%d triangles) quads %d\n", g_shadow_level, g_shadow_stat[0],
                    g_shadow_stat[1], g_shadow_stat[2]);
        std::printf("  [video] water level %d surfaces %d grab %ux%u phase %.2f\n", int(settings().s.graphics.water_effect_level),
                    int(g_water.want), g_water.on ? g_vgpu.grab_w : 0u, g_water.on ? g_vgpu.grab_h : 0u, double(g_water.phase));
        if (g_trail.tried)
            std::printf("  [video] trail level %d tag %d quads %d\n", g_shadow_level, g_trail.stat[0], g_trail.stat[1]);
        if (g_bshadow.stat[0] || g_bshadow.stat[2])
            std::printf("  [video] boss shadow target %d triangles %d strength %.3f quads %d\n", g_bshadow.stat[0],
                        g_bshadow.stat[1], double(g_bshadow.on ? g_bshadow.strength : 0.0f), g_bshadow.stat[2]);
        if (g_mirror.on)
            std::printf("  [video] mirror %ux%u floor %d objects %d images %d\n", g_vgpu.refl_w, g_vgpu.refl_h,
                        int(g_mirror.floor_on), g_mirror.stat[0], g_mirror.stat[1]);
        if (g_water.rwant || g_water.ron)
            std::printf("  [video] reflection %ux%u plane y %.3f\n", g_water.ron ? g_vgpu.refl_w : 0u,
                        g_water.ron ? g_vgpu.refl_h : 0u, double(g_water.plane_y));
        return true;
    }
    return false;
}
