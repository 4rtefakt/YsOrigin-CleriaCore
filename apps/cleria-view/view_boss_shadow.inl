// cleria-view — the bosses' projected shadows (build/render_re/SHADOWS.md 4, RENDER_LEFTOVERS.md 2)
// Textual part of main.cpp: #included after view_video.inl; ONE translation unit, not standalone.
//
// A boss object (GoMesh types 5, 9, 0xa, 0xb, 0xc by its name [EXPORT 00501a30.c:228-264]) owns a small render
// target. Every frame FUN_005043e0 [EXPORT 005043e0.c] draws a model into it from a camera h BELOW the object
// looking up -- FUN_00457a90 [EXPORT 00457a90.c]: eye = pos - (0, h, 0), at = pos, up = (0, 0, -1), fovy pi/4,
// aspect 1, zn 0.1, zf h + 15 -- then blurs it twice (FUN_005fcc90: four taps x 0x40 / 255, k = 1 into pool
// surface 2, V turned over except for type 0xa; k = 2 back into the target).
//   types 5 / 9 / 0xb / 0xc: draw flags 0x14 -- no texture bound, colour = the material's diffuse x the scene
//     light, alpha = vertex alpha x the instance alpha 0.9999 (type 9: max(0, 0.9999 - y / 24)), SRCALPHA /
//     INVSRCALPHA, RGB only, and for a material with the fog flag 8 the colour fades to 0 between n1 and n2 above
//     the object (the `x` shaders' clip-z fog: c1.z = -1 / (n2 - n1), c1.w = (h + n2) / (n2 - n1)). The map
//     materials with flag 0x10000000 then read it top-down around the boss: rgb x (1 - target x strength),
//     u = 0.5 + (x - cx) / 2E, v = 0.5 - (z - cz) / 2E (draw()'s receiver path, main.cpp's boss_shadow()).
//   type 0xa (Zava's heads): draw flags 4 -- the model as an unlit textured draw -- and the target is the
//     texture of one flat additive quad under the head (0x5047f0 [BYTES 0x5047f0..0x504a35], preset 0x206c1),
//     queued on layer 1 while MirrorFloor is on: it shows in the mirror floor's image, not on the floor.
// Target size [BYTES 0x5042e0..0x5043c8]: 256 x 256 at Shadows Full, / 2 at Player, / 4 at Simple, clamped to pool
// surface 2 (the render size / 4).
//
// [H] T, the colour the fixed-function stage returns for an unbound texture, is taken as opaque white (the
//   D3D9 documentation's value; RENDER_LEFTOVERS.md K1): the 0x14 pass then writes the flat diffuse x light. With
//   T = 0 the pass would write nothing and these four bosses would cast no shadow. Retail check 6.1.
// [H] the blur's output alpha is 1 (K2), the receiver strength's factor f (obj+0x418) is the object's alpha.
// [approx] the caster is the boss's collision hull as CleriaCore poses it (retail: the hull model obj+0x270 for
//   types 9 / 0xb / 0xc; for type 5 the model boss01_shadow_ of a linked actor, posed with the boss's bones);
//   type 0xc casts the body's hull only (retail's flags 1 adds a second piece, and each of Darm's four type-0xc
//   meshes renders and publishes in turn: the last one wins); the hull is drawn unculled; type 0xa's pass keeps
//   no height fade; the targets have the frame's format (retail: A1R5G5B5 where the device has it); the receiver
//   strength is this frame's (retail's lags one frame).
//   type 6 (Nygtilger's pieces, `boss02*`): a 64 x 64 target each (h = 16, n2 = 16; the render model at Shadows
//     Full, the hull below it [EXPORT 005043e0.c case 6]), draw flags 0x14, and one flat quad under the piece that
//     darkens the floor with it: B = (x, 0.1, z), half-size 6.0, strength f x 0.35, mode 1, no flags, preset 0x20649
//     = dest x (1 - target x grey) [BYTES 0x56302b..0x5630f0, 0x6bd2b0 = 0.35; RENDER_LEFTOVERS.md 2.5].
// [not modelled] boss03's shadow0 decal under three of its nodes (type 7: the node list at [obj+0x490]+0xa4 was
//   not traced); type 6's attached model at Shadows Full (flags 4) and its `[[obj+0x48c]+4] == 3` gate (K3); the
//   0x20649 quad's fog. The blur's taps are k / 2 texels of the source.
struct BossCaster { const char* name; int type; float h, n2, E, k; };
static const BossCaster kBossCasters[] = {       // [EXPORT 005043e0.c; BYTES 0x6bd994 = 20, 0x6bd960 = 14, 0x6bd350 = 0.66]
    {"boss01", 5, 47.0f, 8.0f, 20.0f, 0.66f},
    {"boss05", 9, 47.0f, 6.5f, 20.0f, 0.66f},
    {"boss07", 0xb, 47.0f, 8.0f, 20.0f, 0.66f},
    {"boss08", 0xc, 34.0f, 7.0f, 14.0f, 1.0f},
};
static const char* kBossShadowWGSL = R"WGSL(
struct U { mvp : mat4x4<f32>, k : vec4<f32>, c : vec4<f32> };   // k.x = c1.z, k.y = c1.w; c.a = 1: c.rgb, fogged
@group(0) @binding(0) var<uniform> u : U;
struct VO { @builtin(position) p : vec4<f32>, @location(0) c : vec4<f32> };
@vertex fn vs(@location(0) pos : vec3<f32>, @location(1) nrm : vec3<f32>, @location(2) uv : vec2<f32>,
              @location(3) color : vec4<f32>) -> VO {
  var o : VO;
  o.p = u.mvp * vec4<f32>(pos, 1.0);
  o.p.y = o.p.y * u.k.w;                           // -1: drawn turned over in v (the atlas of Nygtilger's pieces)
  // nrm.x = 1: the material has the fog flag. u.c.a = 1: a room vertex -- the colour and the flag from u.c.
  let fog = mix(1.0, clamp(o.p.z * u.k.x + u.k.y, 0.0, 1.0), mix(nrm.x, 1.0, u.c.a));
  o.c = vec4<f32>(mix(color.rgb, u.c.rgb, u.c.a) * fog, color.a * mix(1.0, u.k.z, u.c.a));
  return o;
}
@fragment fn fs(v : VO) -> @location(0) vec4<f32> {
  if (v.c.a * 255.0 < 1.0) { discard; }            // ALPHATESTENABLE, ALPHAREF 1
  return v.c;
}
)WGSL";
struct BossShadowGpu {
    WGPURenderPipeline flat{};
    WGPUBuffer bubuf[2]{};                       // the two blur passes' constants
    struct Slot {
        WGPUTextureView view{}, depth{};
        WGPUBindGroup src{}, tex{};              // as the blur's source; as a quad's / the receivers' texture
        std::uint32_t w = 0, h = 0;
        WGPUBuffer ubuf{};                       // the flat pass's constants (its own: the passes of a frame share a submit)
        WGPUBindGroup ubind{};
    };
    struct Job { int slot; std::uint32_t first, count; WGPUBuffer vb, ib; };   // vb / ib: a room's own buffers, indexed
    std::vector<Job> jobs;                       // the flat passes recorded since the last boss_shadow_flush
    std::vector<RoomVertex> vs;                  // ... and their vertices
    std::vector<Slot> slot;                      // 0: the receivers' boss; a Zava head / a Nygtilger piece each
    struct Quad { WGPUBindGroup bind; RoomVertex v[4]; };
    std::vector<Quad> quads;                     // this frame's darkening quads (type 6), drawn with the shadows
    // Nygtilger's pieces share one atlas of 8 x 8 cells (a pass per piece and two blur passes each was 180 passes
    // a frame): the cells' flat draws in one pass, then the two blur passes over the whole atlas.
    struct Atlas {
        WGPUTextureView view{}, depth{}, tmp{};
        WGPUBindGroup src{}, tmp_src{}, tex{};
        std::uint32_t cell = 0;
        std::vector<std::pair<WGPUBuffer, WGPUBindGroup>> u;   // a cell's flat-pass constants
    } atlas;
    WGPUTextureView tmp{};                       // pool surface 2
    WGPUBindGroup tmp_src{};
    std::uint32_t tw = 0, th = 0;
    WGPUBuffer vb{};
    std::uint64_t cap = 0;
};
static BossShadowGpu g_bsg;

static WGPUTextureView boss_shadow_tex(Gpu& g, std::uint32_t w, std::uint32_t h) {
    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    td.dimension = WGPUTextureDimension_2D; td.size = {w, h, 1};
    td.format = g_vgpu.fmt; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTextureView v = wgpuTextureCreateView(t, nullptr);
    wgpuTextureRelease(t);
    return v;
}
// The target of slot `i` (`base` a side at Shadows Full) and pool surface 2 at this frame's sizes; false when the
// passes cannot run.
static bool boss_shadow_targets(Gpu& g, int i, std::uint32_t cw, std::uint32_t ch, std::uint32_t base = 256) {
    if (!g_vgpu.ready || !g_vgpu.blur || cw == 0 || ch == 0) return false;
    BossShadowGpu& b = g_bsg;
    if (!b.flat) {
        WGPUShaderSourceWGSL wgsl = {};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL; wgsl.code = sv(kBossShadowWGSL);
        WGPUShaderModuleDescriptor smd = {}; smd.nextInChain = &wgsl.chain;
        WGPUShaderModule sm = wgpuDeviceCreateShaderModule(g.device, &smd);
        WGPUVertexAttribute attrs[4] = {};
        attrs[0].format = WGPUVertexFormat_Float32x3; attrs[0].offset = 0;  attrs[0].shaderLocation = 0;
        attrs[1].format = WGPUVertexFormat_Float32x3; attrs[1].offset = 12; attrs[1].shaderLocation = 1;
        attrs[2].format = WGPUVertexFormat_Float32x2; attrs[2].offset = 24; attrs[2].shaderLocation = 2;
        attrs[3].format = WGPUVertexFormat_Unorm8x4;  attrs[3].offset = 32; attrs[3].shaderLocation = 3;
        WGPUVertexBufferLayout vbl = {};
        vbl.stepMode = WGPUVertexStepMode_Vertex; vbl.arrayStride = sizeof(RoomVertex);
        vbl.attributeCount = 4; vbl.attributes = attrs;
        WGPUBlendState bl = {};
        bl.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
        bl.alpha = bl.color;
        WGPUColorTargetState tgt = {};
        tgt.format = g_vgpu.fmt; tgt.blend = &bl;
        tgt.writeMask = WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue;
        WGPUFragmentState fs = {}; fs.module = sm; fs.entryPoint = sv("fs"); fs.targetCount = 1; fs.targets = &tgt;
        WGPUDepthStencilState ds = {};
        ds.format = WGPUTextureFormat_Depth24Plus;
        ds.depthWriteEnabled = WGPUOptionalBool_True; ds.depthCompare = WGPUCompareFunction_LessEqual;
        ds.stencilFront.compare = WGPUCompareFunction_Always; ds.stencilBack.compare = WGPUCompareFunction_Always;
        WGPURenderPipelineDescriptor pd = {};
        pd.vertex.module = sm; pd.vertex.entryPoint = sv("vs"); pd.vertex.bufferCount = 1; pd.vertex.buffers = &vbl;
        pd.primitive.topology = WGPUPrimitiveTopology_TriangleList; pd.primitive.cullMode = WGPUCullMode_None;
        pd.depthStencil = &ds; pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFF;
        pd.fragment = &fs;
        b.flat = wgpuDeviceCreateRenderPipeline(g.device, &pd);
        wgpuShaderModuleRelease(sm);
        WGPUBufferDescriptor bd = {}; bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        bd.size = 16;
        for (WGPUBuffer& u : b.bubuf) u = wgpuDeviceCreateBuffer(g.device, &bd);
    }
    const video::Extent e = video::scaled_extent(cw, ch, vid().render_scale, g_vgpu.max_dim);
    const std::uint32_t tw = std::max(1u, e.w / 4), th = std::max(1u, e.h / 4);
    const std::uint32_t side = g_shadow_level == 0 ? base / 4 : g_shadow_level == 1 ? base / 2 : base;
    const std::uint32_t w = std::min(side, tw), h = std::min(side, th);
    if (!b.tmp || b.tw != tw || b.th != th) {
        if (b.tmp_src) wgpuBindGroupRelease(b.tmp_src);
        if (b.tmp) wgpuTextureViewRelease(b.tmp);
        b.tmp = boss_shadow_tex(g, tw, th);
        b.tmp_src = video_bind(g, b.tmp, g_vgpu.linear, b.bubuf[1]);
        b.tw = tw; b.th = th;
    }
    if (b.slot.size() <= std::size_t(i)) b.slot.resize(std::size_t(i) + 1);
    BossShadowGpu::Slot& s = b.slot[std::size_t(i)];
    if (!s.view || s.w != w || s.h != h) {
        if (s.src) wgpuBindGroupRelease(s.src);
        if (s.tex) wgpuBindGroupRelease(s.tex);
        if (s.view) wgpuTextureViewRelease(s.view);
        if (s.depth) wgpuTextureViewRelease(s.depth);
        s.view = boss_shadow_tex(g, w, h);
        s.depth = make_depth(g, w, h);
        s.src = video_bind(g, s.view, g_vgpu.linear, b.bubuf[0]);
        s.tex = tex_bindgroup(g, s.view, g_vgpu.linear);
        s.w = w; s.h = h;
        if (!s.ubuf) {
            WGPUBufferDescriptor bd = {}; bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst; bd.size = 96;
            s.ubuf = wgpuDeviceCreateBuffer(g.device, &bd);
            WGPUBindGroupEntry e = {}; e.binding = 0; e.buffer = s.ubuf; e.size = 96;
            WGPUBindGroupDescriptor d = {};
            d.layout = wgpuRenderPipelineGetBindGroupLayout(b.flat, 0); d.entryCount = 1; d.entries = &e;
            s.ubind = wgpuDeviceCreateBindGroup(g.device, &d);
        }
    }
    return true;
}
// FUN_00457a90's camera.
static Mat4 boss_shadow_vp(const Vec3& pos, float h) {
    return look_at_lh({pos.x, pos.y - h, pos.z}, pos, {0, 0, -1}) * perspective_lh(0.78539816f, 1.0f, 0.1f, h + 15.0f);
}
// The two blur passes of slot `i`'s target (fs_blur, added into a cleared target).
static void boss_shadow_blur(Gpu& g, int i, bool flip_v) {
    BossShadowGpu& b = g_bsg;
    BossShadowGpu::Slot& s = b.slot[std::size_t(i)];
    const float u0[4] = {0.5f / float(s.w), 0.5f / float(s.h), 64.0f / 255.0f, flip_v ? 1.0f : 0.0f};
    const float u1[4] = {1.0f / float(b.tw), 1.0f / float(b.th), 64.0f / 255.0f, 0.0f};
    wgpuQueueWriteBuffer(g.queue, b.bubuf[0], 0, u0, 16);
    wgpuQueueWriteBuffer(g.queue, b.bubuf[1], 0, u1, 16);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    video_fullscreen_pass(g, nullptr, nullptr, b.tmp, enc);
    video_fullscreen_pass(g, g_vgpu.blur, s.src, b.tmp, enc, true);
    video_fullscreen_pass(g, nullptr, nullptr, s.view, enc);
    video_fullscreen_pass(g, g_vgpu.blur, b.tmp_src, s.view, enc, true);
    video_submit(g, enc);
}
// The receivers' boss of this room, before write_mvp: g_bshadow's constants (the pass itself follows it).
static const BossCaster* boss_shadow_setup(const Room& room, MeshWorld& meshes) {
    g_bshadow.on = false;
    for (const BossCaster& c : kBossCasters) {
        const MeshObj* m = meshes.find(c.name);
        if (!m || !room.coll.object_range(c.name)) continue;
        g_bshadow.cx = m->pos.x; g_bshadow.cz = m->pos.z; g_bshadow.E = c.E;
        g_bshadow.strength = std::clamp(m->alpha * c.k, 0.0f, 1.0f);
        return &c;
    }
    return nullptr;
}
// The flat pass (draw flags 0x14) of the triangles [t0, t1) of `m` into slot `i`, from h below `pos`: recorded, and
// drawn with its two blur passes by boss_shadow_flush. `inst` = the instance alpha. Returns the triangles.
// `direct`: `m` is that room's drawn mesh and its materials are white with the fog flag -- the triangles are drawn
// from the room's own buffers (no copy), the colour = the scene light.
// `ub`: the constants go there instead of slot i's (an atlas cell), with the picture turned over in v.
static int boss_shadow_flat(Gpu& g, const RoomMesh& m, std::uint32_t t0, std::uint32_t t1, int i, const Vec3& pos,
                            float h, float n2, float inst, const Room* direct = nullptr, WGPUBuffer ub = nullptr) {
    BossShadowGpu& b = g_bsg;
    const std::size_t first = b.vs.size();
    for (const DrawBatch& db : m.batches) {
        if (direct) break;
        const std::uint32_t i0 = std::max(db.first_index, t0 * 3), i1 = std::min(db.first_index + db.index_count, t1 * 3);
        const float fog = (db.mat_flags & 8u) ? 1.0f : 0.0f;
        for (std::uint32_t k = i0; k < i1 && k < m.indices.size(); ++k) {
            RoomVertex v = m.vertices[m.indices[k]];
            v.nx = fog;
            v.r = std::uint8_t(float(db.diffuse & 0xff) * std::clamp(g_scene_light.x, 0.0f, 1.0f));
            v.g = std::uint8_t(float((db.diffuse >> 8) & 0xff) * std::clamp(g_scene_light.y, 0.0f, 1.0f));
            v.b = std::uint8_t(float((db.diffuse >> 16) & 0xff) * std::clamp(g_scene_light.z, 0.0f, 1.0f));
            v.a = std::uint8_t(std::clamp(float(v.a) * inst, 0.0f, 255.0f));
            b.vs.push_back(v);
        }
    }
    float u[24];
    const Mat4 vp = boss_shadow_vp(pos, h);
    std::memcpy(u, vp.m.data(), 64);
    u[16] = -1.0f / (n2 - 1.0f); u[17] = (h + n2) / (n2 - 1.0f); u[18] = inst; u[19] = ub ? -1.0f : 1.0f;   // n1 = 1
    u[20] = std::clamp(g_scene_light.x, 0.0f, 1.0f); u[21] = std::clamp(g_scene_light.y, 0.0f, 1.0f);
    u[22] = std::clamp(g_scene_light.z, 0.0f, 1.0f); u[23] = direct ? 1.0f : 0.0f;
    wgpuQueueWriteBuffer(g.queue, ub ? ub : b.slot[std::size_t(i)].ubuf, 0, u, sizeof u);
    if (direct) {
        b.jobs.push_back({i, t0 * 3, (t1 - t0) * 3, direct->vbuf, direct->ibuf});
        return int(t1 - t0);
    }
    b.jobs.push_back({i, std::uint32_t(first), std::uint32_t(b.vs.size() - first), nullptr, nullptr});
    return int((b.vs.size() - first) / 3);
}
// The recorded flat passes, each followed by its blur (the first pass turned over in v), in one submit. The jobs
// of one flush have targets of one size (one boss, or Nygtilger's pieces).
static void boss_shadow_flush(Gpu& g) {
    BossShadowGpu& b = g_bsg;
    if (b.jobs.empty()) return;
    const std::uint64_t size = b.vs.size() * sizeof(RoomVertex);
    if (size > b.cap) {
        if (b.vb) wgpuBufferRelease(b.vb);
        b.cap = size * 2;
        WGPUBufferDescriptor bd = {}; bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = b.cap;
        b.vb = wgpuDeviceCreateBuffer(g.device, &bd);
    }
    if (size) wgpuQueueWriteBuffer(g.queue, b.vb, 0, b.vs.data(), size);
    const BossShadowGpu::Slot& s0 = b.slot[std::size_t(b.jobs[0].slot)];
    const float u0[4] = {0.5f / float(s0.w), 0.5f / float(s0.h), 64.0f / 255.0f, 1.0f};
    const float u1[4] = {1.0f / float(b.tw), 1.0f / float(b.th), 64.0f / 255.0f, 0.0f};
    wgpuQueueWriteBuffer(g.queue, b.bubuf[0], 0, u0, 16);
    wgpuQueueWriteBuffer(g.queue, b.bubuf[1], 0, u1, 16);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    for (const BossShadowGpu::Job& j : b.jobs) {
        const BossShadowGpu::Slot& s = b.slot[std::size_t(j.slot)];
        WGPURenderPassColorAttachment ca = {};
        ca.view = s.view; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store; ca.clearValue = {0, 0, 0, 0};
        WGPURenderPassDepthStencilAttachment da = {};
        da.view = s.depth; da.depthLoadOp = WGPULoadOp_Clear; da.depthStoreOp = WGPUStoreOp_Store; da.depthClearValue = 1.0f;
        WGPURenderPassDescriptor rp = {};
        rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        if (j.count && (j.vb || size)) {
            wgpuRenderPassEncoderSetPipeline(pass, b.flat);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, s.ubind, 0, nullptr);
            if (j.vb) {
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, j.vb, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderSetIndexBuffer(pass, j.ib, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderDrawIndexed(pass, j.count, 1, j.first, 0, 0);
            } else {
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, b.vb, 0, size);
                wgpuRenderPassEncoderDraw(pass, j.count, 1, j.first, 0);
            }
        }
        wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
        video_fullscreen_pass(g, nullptr, nullptr, b.tmp, enc);
        video_fullscreen_pass(g, g_vgpu.blur, s.src, b.tmp, enc, true);
        video_fullscreen_pass(g, nullptr, nullptr, s.view, enc);
        video_fullscreen_pass(g, g_vgpu.blur, b.tmp_src, s.view, enc, true);
    }
    video_submit(g, enc);
    b.jobs.clear(); b.vs.clear();
}
// The receivers' boss `c`: its hull into slot 0; the receivers read it.
static void boss_shadow_cast(Gpu& g, const Room& room, MeshWorld& meshes, const BossCaster* c, std::uint32_t cw,
                             std::uint32_t ch) {
    g_bshadow.stat[0] = g_bshadow.stat[1] = 0;
    if (!c) return;
    const MeshObj* m = meshes.find(c->name);
    const RoomMesh::ObjRange* r = room.coll.object_range(c->name);
    if (!m || !r || !boss_shadow_targets(g, 0, cw, ch)) return;
    const float inst = c->type == 9 ? std::max(0.0f, 0.9999f - m->pos.y / 24.0f) : 0.9999f;   // [BYTES 0x6bd9b8 = 24]
    const int tris = boss_shadow_flat(g, room.coll, r->t0, r->t1, 0, m->pos, c->h, c->n2, inst);
    boss_shadow_flush(g);
    BossShadowGpu::Slot& s = g_bsg.slot[0];
    if (!g.ubind_shd_view || g.ubind_shd_view != s.view) {       // the receivers' stage: the target, clamped
        if (g.ubind_shd) wgpuBindGroupRelease(g.ubind_shd);
        WGPUBindGroupEntry be[4] = {};
        be[0].binding = 0; be[0].buffer = g.ubuf_shd; be[0].size = 128;
        be[1].binding = 1; be[1].textureView = s.view;
        be[2].binding = 2; be[2].sampler = g_vgpu.linear;
        be[3].binding = 3; be[3].textureView = s.view;
        WGPUBindGroupDescriptor d = {};
        d.layout = g.u_layout; d.entryCount = 4; d.entries = be;
        g.ubind_shd = wgpuDeviceCreateBindGroup(g.device, &d);
        g.ubind_shd_view = s.view;
    }
    g_bshadow.on = true;
    g_bshadow.stat[0] = int(s.w); g_bshadow.stat[1] = tris;
}
// Zava's heads (type 0xa): each head's model, textured, from below into its own target, blurred without the
// turn, then the additive quad under it [BYTES 0x5634b6..0x56357b: B = (x, y + 0.1, z), half-size 5.0, strength
// f x 0.9, mode 1, flags 8; h = 14, n2 = 7 EXPORT 005043e0.c case 10]. The quad's law is the shadow's
// (FUN_00456da0): 0.025 |B - A| off the size and the alpha, centre B - forward x 0.15, flag 8 turns v over, no
// turn by the camera's yaw (flag 2 clear). Queued for the mirror image while MirrorFloor is on (g_mirror.quads);
// otherwise retail draws it on layer 8 -- [not modelled] here (no shipped room has Zava off a mirror floor).
// `vp` = the frame's view-projection, written back after the passes. Runs before the frame's first world pass.
static void boss_shadow_zava(Gpu& g, const Room& room, MeshWorld& meshes, std::uint32_t cw, std::uint32_t ch,
                             const Mat4& vp, const Vec3& fwd) {
    g_bshadow.stat[2] = 0;
    static const char* const kHeads[2] = {"boss06a", "boss06b"};
    bool wrote = false;
    for (int i = 0; i < 2; ++i) {
        const MeshObj* m = meshes.find(kHeads[i]);
        const RoomMesh::ObjRange* r = room.mesh.object_range(kHeads[i]);
        if (!m || !r || !g_mirror.floor_on || !boss_shadow_targets(g, i, cw, ch)) continue;
        BossShadowGpu::Slot& s = g_bsg.slot[std::size_t(i)];
        const FogState keep = g_fog;
        g_fog.on = false;
        write_mvp(g, boss_shadow_vp(m->pos, 14.0f));
        g_fog = keep;
        wrote = true;
        g_mirror.v0 = r->v0; g_mirror.v1 = r->v1;
        draw(g, room, s.view, s.depth, 6);
        boss_shadow_blur(g, i, false);
        const float v = std::clamp(m->alpha * 0.9f, 0.0f, 1.0f), k = 0.025f * 0.1f, sz = 5.0f - k;
        if (v - k < 0.0f) continue;
        const Vec3 C{m->pos.x - fwd.x * 0.15f, m->pos.y + 0.1f - fwd.y * 0.15f, m->pos.z - fwd.z * 0.15f};
        const std::uint8_t gv = std::uint8_t(v * 255.0f);
        const float nx = 1.0f - (v - k);
        auto V = [&](float ox, float oz, float tu, float tv) -> RoomVertex {
            return {C.x + ox, C.y, C.z + oz, nx, 0, -1, tu, tv, gv, gv, gv, 0};
        };
        // corners (-s, -s) (-s, +s) (+s, -s) (+s, +s), u = 0 0 1 1, v = 0 1 0 1 with flag 8 [BYTES 0x456f96..0x45711c]
        Mirror::Quad q{s.tex, {V(-sz, -sz, 0, 0), V(sz, -sz, 1, 0), V(sz, sz, 1, 1), V(-sz, sz, 0, 1)}, true};
        g_mirror.quads.push_back(q);
        ++g_bshadow.stat[2];
    }
    if (wrote) write_mvp(g, vp);
}
// Nygtilger's pieces (type 6): each `boss02*` object's flat pass and the quad that darkens the floor with it, kept
// in g_bsg.quads for boss_shadow_quads (the shadows' layer 8). After boss_shadow_zava.
// [approx] retail gives each piece its own 64 x 64 target and blurs each through pool surface 2; here the pieces are
// cells of one atlas (drawn turned over in v instead of the first blur pass's turn), blurred as a whole with the
// same taps -- a cell's edge texels can take a neighbour's.
static void boss_shadow_nygtilger(Gpu& g, const Room& room, MeshWorld& meshes, std::uint32_t cw, std::uint32_t ch,
                                  const Vec3& fwd) {
    BossShadowGpu& b = g_bsg;
    b.quads.clear();
    constexpr std::uint32_t kGrid = 8;
    BossShadowGpu::Atlas& at = b.atlas;
    std::uint32_t cells = 0;
    const bool full = g_shadow_level >= 2;
    for (const RoomMesh::ObjRange& mr : room.mesh.object_ranges) {
        if (mr.name.compare(0, 6, "boss02") != 0 || cells >= kGrid * kGrid) continue;
        const MeshObj* m = meshes.find(mr.name);
        const RoomMesh::ObjRange* hr = room.coll.object_range(mr.name);
        if (!m || m->alpha <= 0.0f || (!full && !hr)) continue;
        if (cells == 0) {                            // the first piece: the atlas at this frame's cell size
            if (!boss_shadow_targets(g, 0, cw, ch, 64)) return;
            const std::uint32_t cell = b.slot[0].w;  // 64 / 32 / 16 by the level, clamped as a target is
            if (!at.view || at.cell != cell) {
                for (WGPUBindGroup* bg : {&at.src, &at.tmp_src, &at.tex}) if (*bg) wgpuBindGroupRelease(*bg);
                for (WGPUTextureView* v : {&at.view, &at.depth, &at.tmp}) if (*v) wgpuTextureViewRelease(*v);
                at.view = boss_shadow_tex(g, cell * kGrid, cell * kGrid);
                at.tmp = boss_shadow_tex(g, cell * kGrid, cell * kGrid);
                at.depth = make_depth(g, cell * kGrid, cell * kGrid);
                at.src = video_bind(g, at.view, g_vgpu.linear, b.bubuf[0]);
                at.tmp_src = video_bind(g, at.tmp, g_vgpu.linear, b.bubuf[1]);
                at.tex = tex_bindgroup(g, at.view, g_vgpu.linear);
                at.cell = cell;
            }
        }
        if (at.u.size() <= cells) {
            WGPUBufferDescriptor bd = {}; bd.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst; bd.size = 96;
            WGPUBuffer ub = wgpuDeviceCreateBuffer(g.device, &bd);
            WGPUBindGroupEntry e = {}; e.binding = 0; e.buffer = ub; e.size = 96;
            WGPUBindGroupDescriptor d = {};
            d.layout = wgpuRenderPipelineGetBindGroupLayout(b.flat, 0); d.entryCount = 1; d.entries = &e;
            at.u.push_back({ub, wgpuDeviceCreateBindGroup(g.device, &d)});
        }
        // [DATA] every material of BOSS02 / B / C / D.YMO has the diffuse (1, 1, 1) and the fog flag 8.
        const int tris = full ? boss_shadow_flat(g, room.mesh, mr.t0, mr.t1, int(cells), m->pos, 16.0f, 16.0f, 0.9999f, &room, at.u[cells].first)
                              : boss_shadow_flat(g, room.coll, hr->t0, hr->t1, int(cells), m->pos, 16.0f, 16.0f, 0.9999f, nullptr, at.u[cells].first);
        g_bshadow.stat[0] = int(at.cell); g_bshadow.stat[1] += tris;
        const std::uint32_t cell = cells++;
        const float v = std::clamp(m->alpha * 0.35f, 0.0f, 1.0f), k = 0.025f * std::fabs(m->pos.y - 0.1f), sz = 6.0f - k;
        if (v - k < 0.0f || sz < 0.0f) continue;
        const Vec3 C{m->pos.x - fwd.x * 0.15f, 0.1f - fwd.y * 0.15f, m->pos.z - fwd.z * 0.15f};
        const std::uint8_t gv = std::uint8_t(v * 255.0f);
        const float cu = float(cell % kGrid) / float(kGrid), cv = float(cell / kGrid) / float(kGrid), cs = 1.0f / float(kGrid);
        auto V = [&](float ox, float oz, float tu, float tv) -> RoomVertex {
            return {C.x + ox, C.y, C.z + oz, 0, 0, -1, cu + tu * cs, cv + tv * cs, gv, gv, gv, 0};
        };
        // no flags: u = 0 at -x, v = 1 at -z [BYTES 0x456f96..0x45711c]
        b.quads.push_back({at.tex, {V(-sz, -sz, 0, 1), V(sz, -sz, 1, 1), V(sz, sz, 1, 0), V(-sz, sz, 0, 0)}});
        ++g_bshadow.stat[2];
    }
    if (b.jobs.empty()) return;
    const std::uint64_t size = b.vs.size() * sizeof(RoomVertex);
    if (size > b.cap) {
        if (b.vb) wgpuBufferRelease(b.vb);
        b.cap = size * 2;
        WGPUBufferDescriptor bd = {}; bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = b.cap;
        b.vb = wgpuDeviceCreateBuffer(g.device, &bd);
    }
    if (size) wgpuQueueWriteBuffer(g.queue, b.vb, 0, b.vs.data(), size);
    const float side = float(at.cell * kGrid);
    const float u0[4] = {0.5f / side, 0.5f / side, 64.0f / 255.0f, 0.0f};                       // k = 1: half a texel
    const float u1[4] = {1.0f / (float(b.tw) * float(kGrid)), 1.0f / (float(b.th) * float(kGrid)), 64.0f / 255.0f, 0.0f};   // k = 2: a texel of pool surface 2
    wgpuQueueWriteBuffer(g.queue, b.bubuf[0], 0, u0, 16);
    wgpuQueueWriteBuffer(g.queue, b.bubuf[1], 0, u1, 16);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = at.view; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store; ca.clearValue = {0, 0, 0, 0};
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = at.depth; da.depthLoadOp = WGPULoadOp_Clear; da.depthStoreOp = WGPUStoreOp_Store; da.depthClearValue = 1.0f;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, b.flat);
    for (const BossShadowGpu::Job& j : b.jobs) {
        if (!j.count || !(j.vb || size)) continue;
        const std::uint32_t cx = std::uint32_t(j.slot) % kGrid * at.cell, cy = std::uint32_t(j.slot) / kGrid * at.cell;
        wgpuRenderPassEncoderSetViewport(pass, float(cx), float(cy), float(at.cell), float(at.cell), 0.0f, 1.0f);
        wgpuRenderPassEncoderSetScissorRect(pass, cx, cy, at.cell, at.cell);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, at.u[std::size_t(j.slot)].second, 0, nullptr);
        if (j.vb) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, j.vb, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(pass, j.ib, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexed(pass, j.count, 1, j.first, 0, 0);
        } else {
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, b.vb, 0, size);
            wgpuRenderPassEncoderDraw(pass, j.count, 1, j.first, 0);
        }
    }
    wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    video_fullscreen_pass(g, nullptr, nullptr, at.tmp, enc);
    video_fullscreen_pass(g, g_vgpu.blur, at.src, at.tmp, enc, true);
    video_fullscreen_pass(g, nullptr, nullptr, at.view, enc);
    video_fullscreen_pass(g, g_vgpu.blur, at.tmp_src, at.view, enc, true);
    video_submit(g, enc);
    b.jobs.clear(); b.vs.clear();
}
static void boss_shadow_quads(Gpu& g, WGPUTextureView color, WGPUTextureView depth) {
    if (g_bsg.quads.empty()) return;
    QuadScope scope(g);
    for (const BossShadowGpu::Quad& q : g_bsg.quads) draw_quad(g, color, depth, g.darken_pipe, q.bind, q.v);
}
