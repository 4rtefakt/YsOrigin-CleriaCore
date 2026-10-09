// The screen effects at the tail of FUN_005fed30 (build/render_re/SCREEN_EFFECTS.md §7; state in
// src/cleria/screenfx.hpp). They run after the FireBlur and Mask of Eyes blocks of the same function,
// so after render layer 14: the HUD, the windows and the book (layer 15) are drawn over the result.
//
// ZoomBlur [BYTES 0x5ff10b..0x5ff40e]: StretchRect the back buffer into surface 0 (the CURRENT frame,
// no feedback), then draw that copy over the frame as full-frame quads shrunk toward the centre by
// 0.02 k (k = 1..), preset 0x5041: alpha blend SRCALPHA / INVSRCALPHA, no Z, RGB write only,
// colour = tex, alpha = tex.a x TFACTOR.a with TFACTOR.a = floor(strength x 256). The UVs are not
// scaled: each tap is the whole frame, smaller. Drawn 0.98 -> 0.96 -> 0.94, each over the last.
// [H] (spec U1) THREE taps: taps = 4, but the TRIANGLEFAN's primitive count (16) ends inside the
// degenerate joins, so the 4th quad (0.92) is cut off; a retail frame capture would settle it.
// [H] the copy's alpha is taken as 1 (retail: the glare mask in the back buffer's alpha; there is no
// glare pass here). [H-med] the sampler: linear (not set by the draw; as FireBlur's, view_weather.inl).
//
// The frame hold [BYTES 0x5ff04c..0x5ff0fa] takes ZoomBlur's place while its opacity 0x7b248c is not 0: ONE
// full-frame quad of the same copy, preset 0x1041 (the same blend, no alpha test), TFACTOR.a from 0x7b248c. The
// copy [EXPORT 005fed30.c:110-126] is made only on a frame with 0x7b2488 set or a ZoomBlur strength, so the quad
// is the frame its writer froze (Gelaldy's death, Zava's), fading out over the live scene.
static const char* kZoomBlurWGSL = R"WGSL(
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var smp: sampler;
struct VO { @builtin(position) p: vec4f, @location(0) uv: vec2f, @location(1) a: f32 };
@vertex fn vs(@location(0) pos: vec2f, @location(1) uv: vec2f, @location(2) a: f32) -> VO {
    var o: VO;
    o.p = vec4f(pos, 0.0, 1.0);
    o.uv = uv;
    o.a = a;
    return o;
}
@fragment fn fs(v: VO) -> @location(0) vec4f { return vec4f(textureSample(src, smp, v.uv).rgb, v.a); }
)WGSL";
struct ScreenFxGpu {
    WGPURenderPipeline pipe{};
    bool tried = false;
    WGPUTexture tex{};
    WGPUTextureView view{};
    WGPUBindGroup bind{};
    std::uint32_t w = 0, h = 0;
    bool have = false;                                    // the copy holds a frame
};
static ScreenFxGpu sfx_gpu;
static const int kZoomBlurTaps = 3;                       // [H] U1: of the 4 built, the 3 the fan reaches
// src: the texture the world was drawn into (CopySrc), dst its view; tw x th its size; fw x fh the frame.
// snapshot: 0x7b2488 (a writer asked for the copy this frame).
static void draw_zoom_blur(Gpu& g, const screenfx::ZoomBlur& zb, const screenfx::FrameHold& hold, bool snapshot,
                           WGPUTexture src, WGPUTextureView dst,
                           std::uint32_t tw, std::uint32_t th, std::uint32_t fw, std::uint32_t fh) {
    const bool copy = snapshot || zb.strength != 0.0f;    // 0x7b2488 != 0 || 0x7b2490 != 0.0
    const bool held = hold.strength != 0.0f;              // 0x7b248c != 0.0: its quad, else ZoomBlur's
    if (!copy && !held) return;
    if (!g_vgpu.ready || !src || !dst || tw == 0 || th == 0 || fw == 0 || fh == 0) return;
    ScreenFxGpu& F = sfx_gpu;
    if (!F.tried) {
        F.tried = true;
        WGPUShaderSourceWGSL wgsl = {};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL; wgsl.code = sv(kZoomBlurWGSL);
        WGPUShaderModuleDescriptor smd = {}; smd.nextInChain = &wgsl.chain;
        WGPUShaderModule sm = wgpuDeviceCreateShaderModule(g.device, &smd);
        WGPUPipelineLayoutDescriptor pld = {}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &g_vgpu.bgl;
        WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(g.device, &pld);
        WGPUVertexAttribute va[3] = {};
        va[0].format = WGPUVertexFormat_Float32x2; va[0].offset = 0; va[0].shaderLocation = 0;
        va[1].format = WGPUVertexFormat_Float32x2; va[1].offset = 8; va[1].shaderLocation = 1;
        va[2].format = WGPUVertexFormat_Float32; va[2].offset = 16; va[2].shaderLocation = 2;
        WGPUVertexBufferLayout vbl = {};
        vbl.arrayStride = 20; vbl.stepMode = WGPUVertexStepMode_Vertex; vbl.attributeCount = 3; vbl.attributes = va;
        WGPUBlendState bl = {};                            // preset 0x5041: SRCALPHA / INVSRCALPHA
        bl.color.operation = WGPUBlendOperation_Add;
        bl.color.srcFactor = WGPUBlendFactor_SrcAlpha; bl.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        bl.alpha.operation = WGPUBlendOperation_Add;
        bl.alpha.srcFactor = WGPUBlendFactor_Zero; bl.alpha.dstFactor = WGPUBlendFactor_One;
        WGPUColorTargetState tgt = {}; tgt.format = g_vgpu.fmt; tgt.blend = &bl;
        tgt.writeMask = WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue;   // COLORWRITEENABLE 7
        WGPUFragmentState fs = {}; fs.module = sm; fs.entryPoint = sv("fs"); fs.targetCount = 1; fs.targets = &tgt;
        WGPURenderPipelineDescriptor pd = {};
        pd.layout = pl;
        pd.vertex.module = sm; pd.vertex.entryPoint = sv("vs");
        pd.vertex.bufferCount = 1; pd.vertex.buffers = &vbl;
        pd.primitive.topology = WGPUPrimitiveTopology_TriangleList; pd.primitive.cullMode = WGPUCullMode_None;
        pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFF;
        pd.fragment = &fs;
        F.pipe = wgpuDeviceCreateRenderPipeline(g.device, &pd);
        wgpuPipelineLayoutRelease(pl);
        wgpuShaderModuleRelease(sm);
    }
    if (!F.pipe) return;
    if (!F.tex || F.w != tw || F.h != th) {               // 0x765584: the back buffer's copy (surface 0)
        if (F.bind) wgpuBindGroupRelease(F.bind);
        if (F.view) wgpuTextureViewRelease(F.view);
        if (F.tex) wgpuTextureRelease(F.tex);
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D; td.size = {tw, th, 1};
        td.format = g_vgpu.fmt; td.mipLevelCount = 1; td.sampleCount = 1;
        F.tex = wgpuDeviceCreateTexture(g.device, &td);
        F.view = wgpuTextureCreateView(F.tex, nullptr);
        F.bind = video_bind(g, F.view, g_vgpu.linear);
        F.w = tw; F.h = th;
        F.have = false;
    }
    const int quads = held ? (F.have || copy ? 1 : 0) : (zb.draws() ? kZoomBlurTaps : 0);
    // The quads in frame pixels as retail builds them — the template 0x7b2e90's corners (-0.5, -0.5) ..
    // (W - 0.5, H - 0.5) pulled toward the centre by (1 - 0.02 k) — then to NDC as FireBlur's strip
    // (x = -0.5 is the left edge). The same at any render scale: the copy is the whole target.
    const float W = float(fw), H = float(fh);
    const float cx = zb.cx * W, cy = zb.cy * H;
    const float a = float(held ? hold.alpha8() : zb.alpha8()) / 255.0f;
    const float corner[4][2] = {{-0.5f, -0.5f}, {W - 0.5f, -0.5f}, {-0.5f, H - 0.5f}, {W - 0.5f, H - 0.5f}};
    const float uv[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    const int order[6] = {0, 1, 2, 2, 1, 3};
    float vtx[kZoomBlurTaps * 6][5];
    for (int k = 1; k <= kZoomBlurTaps; ++k) {
        const float sc = held ? 1.0f : 1.0f - zb.step_k * float(k);   // the hold: the template quad itself
        for (int j = 0; j < 6; ++j) {
            const int c = order[j];
            float* v = vtx[(k - 1) * 6 + j];
            const float x = cx + (corner[c][0] - cx) * sc, y = cy + (corner[c][1] - cy) * sc;
            v[0] = (x + 0.5f) / W * 2.0f - 1.0f;
            v[1] = 1.0f - (y + 0.5f) / H * 2.0f;
            v[2] = uv[c][0]; v[3] = uv[c][1];
            v[4] = a;
        }
    }
    WGPUBuffer vb = make_buffer(g.device, g.queue, vtx, sizeof vtx, WGPUBufferUsage_Vertex);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPUTexelCopyTextureInfo cs = {}; cs.texture = src; cs.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyTextureInfo cd = {}; cd.texture = F.tex; cd.aspect = WGPUTextureAspect_All;
    WGPUExtent3D ext = {tw, th, 1};
    if (copy) { wgpuCommandEncoderCopyTextureToTexture(enc, &cs, &cd, &ext); F.have = true; }
    WGPURenderPassColorAttachment ca = {};
    ca.view = dst; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;   // the ring outside the largest quad stays unblurred
    WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, F.pipe);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, F.bind, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
    if (quads) wgpuRenderPassEncoderDraw(pass, std::uint32_t(quads) * 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    video_submit(g, enc);
    wgpuBufferRelease(vb);
}
