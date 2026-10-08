// The Mask of Eyes (accessory 0x36; build/audit/PLAYTEST_1_STORY.md §1).
//
// Equipping it sets equip-flags bit 0x10000, which the stat recompute FUN_004208f0 copies to
// 0x76bbac (@0x420b76: `(flags >> 16) & 1`). Two things read that bit:
//
// 1. The scene objects a SOB marks. The object ctor FUN_00501a30 turns the SOB record's +0x14c
//    word into +0x298 bits (@0x50257e: & 0x1000 -> |= 2, & 0x2000 -> |= 4). Bit 2 = an illusion
//    that the mask removes (S_1006's s10wall_00, word 0x1800), bit 4 = a thing only the mask shows
//    (S_3003 s30mask0, S_4003 s40yuka_00). Such an object is
//      - out of the collision queries outright while gated (FUN_0041e420 / FUN_0041ea80 skip it:
//        `(f & 2) && mask || (f & 4) && !mask`), and out of the swing's GoMesh pass (FUN_004fed50);
//      - faded: FUN_00503190 sets +0x474 = gated, and on a change the alpha target +0x41c = 0
//        (gated) / 1.0, step +0x420 = 0x3c888889 (1/60): +0x418 walks there in 60 updates and is
//        the model alpha (+0x26c -> +0xc);
//      - born gated at alpha 0 (FUN_00501a30 @0x502ac4: +0x474 = 1, +0x418 = +0x41c = +0x420 = 0).
// 2. The screen. FUN_00427c90 sets the pass colour 0x7b2e68 = 0xffffffff with the mask, else
//    0x00ffffff; FUN_005fed30 then (unless [graphics] MaskOfEyes = 1, 0x76519b) StretchRects the
//    back buffer into a texture and FUN_005fe3c0 redraws it over the screen with that colour as
//    TFACTOR: alpha 0xff -> opaque (blend 1), alpha 0 -> blended away (0x41). The texture stages
//    (the 0x7651ab != 0 branch): stage 0 MULTIPLYADD diffuse + tex x diffuse (diffuse 0x808080),
//    stage 1 DOTPRODUCT3 (current, specular 0xa6cb8e), stage 2 MODULATE (current, TFACTOR). The
//    specular is (0.30, 0.59, 0.11) biased by 0.5: the dot is the luminance — the world goes grey.
//    It runs after the FireBlur block of the same function, so after render layer 14: the HUD,
//    windows and book (layer 15) keep their colour.
//    [H] the branch: 0x7651ab is a [graphics] struct byte (+0x73) whose writer is not traced; the
//    other branch modulates by (0, 1, 0.5) instead (TFACTOR & 0xff00ff80 | 0xff80, no dot3) — a
//    fallback for cards without DOTPRODUCT3, which every D3D9 card with PS has.

// The mask bit as FUN_004208f0 derives it: only accessory 0x36 sets 0x10000. The game update
// FUN_004281c0 runs the recompute FUN_00420c40 every step (@0x428482, unconditionally), so the bit follows the slot at once — also after @ResetMask's EventXX(10), whose
// case (FUN_0058f210) only writes 0x76a628 = -1.
static bool mask_of_eyes_on(const InvState& inv) { return inv.accessory == 0x36; }

// Is this object gated right now (the FUN_0041e420 / FUN_00503190 test)?
static bool mask_gated(std::uint32_t bits, bool mask) {
    return ((bits & 2u) && mask) || ((bits & 4u) && !mask);
}

static void mask_upload(Gpu& g, Room& room, const RoomMesh::ObjRange& rr) {
    if (room.vbuf && rr.v1 > rr.v0 && rr.v1 <= room.mesh.vertices.size())
        wgpuQueueWriteBuffer(g.queue, room.vbuf, std::size_t(rr.v0) * sizeof(RoomVertex),
                             &room.mesh.vertices[rr.v0], std::size_t(rr.v1 - rr.v0) * sizeof(RoomVertex));
}

// The hull out of / back into the collision queries: collapsed to a point (degenerate triangles
// are never hit — the same device boss02's one-way entries use) and restored from a copy.
static void mask_collide(Room& room, Room::MaskObj& m, bool off) {
    if (m.cr < 0 || std::size_t(m.cr) >= room.coll.object_ranges.size() || m.coll_off == off) return;
    const RoomMesh::ObjRange& cr = room.coll.object_ranges[std::size_t(m.cr)];
    if (cr.v1 > room.coll.vertices.size() || cr.v1 <= cr.v0) return;
    if (off) {
        m.coll_saved.clear();
        for (std::uint32_t k = cr.v0; k < cr.v1; ++k)
            m.coll_saved.push_back({room.coll.vertices[k].px, room.coll.vertices[k].py, room.coll.vertices[k].pz});
        for (std::uint32_t k = cr.v0; k < cr.v1; ++k) {
            room.coll.vertices[k].px = m.coll_saved[0].x;
            room.coll.vertices[k].py = m.coll_saved[0].y;
            room.coll.vertices[k].pz = m.coll_saved[0].z;
        }
    } else if (m.coll_saved.size() == std::size_t(cr.v1 - cr.v0)) {
        for (std::uint32_t k = cr.v0; k < cr.v1; ++k) {
            const Vec3& p = m.coll_saved[k - cr.v0];
            room.coll.vertices[k].px = p.x; room.coll.vertices[k].py = p.y; room.coll.vertices[k].pz = p.z;
        }
    }
    m.coll_off = off;
}

// The objects' ctor side (FUN_00501a30): find the gated ones; a gated one is born invisible.
static void mask_seed(Gpu& g, Room& room, bool mask) {
    room.mask_seeded = true;
    room.mask_objs.clear();
    for (std::size_t i = 0; i < room.mesh.object_ranges.size(); ++i) {
        const RoomMesh::ObjRange& rr = room.mesh.object_ranges[i];
        const std::int32_t w = rr.obj.rflags;
        std::uint32_t bits = 0;
        if (w & 0x1000) bits |= 2u;                       // @0x50257e
        if (w & 0x2000) bits |= 4u;                       // @0x50258c
        if (!bits) continue;
        Room::MaskObj m;
        m.rr = i; m.bits = bits;
        for (std::size_t c = 0; c < room.coll.object_ranges.size(); ++c) {   // its X_.YMO twin
            const SobObject& o = room.coll.object_ranges[c].obj;
            if (o.name == rr.obj.name && o.position.x == rr.obj.position.x && o.position.y == rr.obj.position.y &&
                o.position.z == rr.obj.position.z && o.rflags == w) { m.cr = long(c); break; }
        }
        if (mask_gated(bits, mask)) {                     // @0x502ae4
            m.hidden = true; m.alpha = m.target = m.step = 0.0f;
            apply_object_alpha(room, &rr, 0.0f);
            mask_upload(g, room, rr);
        }
        mask_collide(room, m, mask_gated(bits, mask));
        room.mask_objs.push_back(std::move(m));
    }
}

// One GoMesh update's mask part (FUN_00503190 head) + the collision gate, every live step.
static void mask_step(Gpu& g, Room& room, bool mask) {
    if (!room.mask_seeded) mask_seed(g, room, mask);
    for (Room::MaskObj& m : room.mask_objs) {
        const bool c = mask_gated(m.bits, mask);
        mask_collide(room, m, c);
        if (m.hidden != c) {
            m.hidden = c;
            m.target = c ? 0.0f : 1.0f;
            m.step = 0.016666668f;                        // 0x3c888889
        }
        if (m.alpha == m.target) continue;
        if (m.target <= m.alpha) { m.alpha -= m.step; if (!(m.target < m.alpha)) m.alpha = m.target; }
        else                     { m.alpha += m.step; if (!(m.alpha < m.target)) m.alpha = m.target; }
        if (m.rr < room.mesh.object_ranges.size()) {
            const RoomMesh::ObjRange& rr = room.mesh.object_ranges[m.rr];
            apply_object_alpha(room, &rr, m.alpha);
            mask_upload(g, room, rr);
        }
    }
}

// The screen pass (FUN_005fe3c0 under the mask): the world image, grey.
static const char* kMaskEyesWGSL = R"WGSL(
@group(0) @binding(0) var src: texture_2d<f32>;
@group(0) @binding(1) var smp: sampler;
struct VO { @builtin(position) p: vec4f, @location(0) uv: vec2f };
@vertex fn vs(@builtin(vertex_index) i: u32) -> VO {
    var o: VO;
    let x = f32((i << 1u) & 2u);
    let y = f32(i & 2u);
    o.p = vec4f(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);
    o.uv = vec2f(x, y);
    return o;
}
@fragment fn fs(v: VO) -> @location(0) vec4f {
    let t = textureSample(src, smp, v.uv).rgb;
    let d = 128.0 / 255.0;                                        // diffuse 0x808080
    let a = clamp(vec3f(d) + t * d, vec3f(0.0), vec3f(1.0));      // stage 0 MULTIPLYADD
    let s = vec3f(166.0, 203.0, 142.0) / 255.0;                   // specular 0xa6cb8e
    let l = clamp(4.0 * dot(a - 0.5, s - 0.5), 0.0, 1.0);         // stage 1 DOTPRODUCT3
    return vec4f(l, l, l, 1.0);                                   // stage 2 x TFACTOR (white), opaque
}
)WGSL";
struct MaskEyesGpu {
    WGPURenderPipeline pipe{};
    bool tried = false;
    WGPUTexture tex{};
    WGPUTextureView view{};
    WGPUBindGroup bind{};
    std::uint32_t w = 0, h = 0;
};
static MaskEyesGpu me_gpu;
// src: the texture the world was drawn into (CopySrc), dst its view; tw x th its size.
static void draw_mask_of_eyes(Gpu& g, bool mask, WGPUTexture src, WGPUTextureView dst, std::uint32_t tw,
                              std::uint32_t th) {
    if (!mask || settings().s.graphics.mask_of_eyes != 0) return;   // 0x7b2e68 alpha 0 / 0x76519b
    if (!g_vgpu.ready || !src || !dst || tw == 0 || th == 0) return;
    MaskEyesGpu& F = me_gpu;
    if (!F.tried) {
        F.tried = true;
        WGPUShaderSourceWGSL wgsl = {};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL; wgsl.code = sv(kMaskEyesWGSL);
        WGPUShaderModuleDescriptor smd = {}; smd.nextInChain = &wgsl.chain;
        WGPUShaderModule sm = wgpuDeviceCreateShaderModule(g.device, &smd);
        WGPUPipelineLayoutDescriptor pld = {}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &g_vgpu.bgl;
        WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(g.device, &pld);
        WGPUColorTargetState tgt = {}; tgt.format = g_vgpu.fmt; tgt.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs = {}; fs.module = sm; fs.entryPoint = sv("fs"); fs.targetCount = 1; fs.targets = &tgt;
        WGPURenderPipelineDescriptor pd = {};
        pd.layout = pl;
        pd.vertex.module = sm; pd.vertex.entryPoint = sv("vs");
        pd.primitive.topology = WGPUPrimitiveTopology_TriangleList; pd.primitive.cullMode = WGPUCullMode_None;
        pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFF;
        pd.fragment = &fs;
        F.pipe = wgpuDeviceCreateRenderPipeline(g.device, &pd);
        wgpuPipelineLayoutRelease(pl);
        wgpuShaderModuleRelease(sm);
    }
    if (!F.pipe) return;
    if (!F.tex || F.w != tw || F.h != th) {               // 0x765584: the back buffer's copy
        if (F.bind) wgpuBindGroupRelease(F.bind);
        if (F.view) wgpuTextureViewRelease(F.view);
        if (F.tex) wgpuTextureRelease(F.tex);
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        td.dimension = WGPUTextureDimension_2D; td.size = {tw, th, 1};
        td.format = g_vgpu.fmt; td.mipLevelCount = 1; td.sampleCount = 1;
        F.tex = wgpuDeviceCreateTexture(g.device, &td);
        F.view = wgpuTextureCreateView(F.tex, nullptr);
        F.bind = video_bind(g, F.view, g_vgpu.nearest);
        F.w = tw; F.h = th;
    }
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPUTexelCopyTextureInfo cs = {}; cs.texture = src; cs.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyTextureInfo cd = {}; cd.texture = F.tex; cd.aspect = WGPUTextureAspect_All;
    WGPUExtent3D ext = {tw, th, 1};
    wgpuCommandEncoderCopyTextureToTexture(enc, &cs, &cd, &ext);
    WGPURenderPassColorAttachment ca = {};
    ca.view = dst; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, F.pipe);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, F.bind, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    video_submit(g, enc);
}
