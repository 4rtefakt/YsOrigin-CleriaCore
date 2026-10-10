// cleria-view — character AIA billboard, shadow, screen image, headless --shot
// Textual part of main.cpp (split 2026-07-23, PLAN.md Phase 0): #included by
// main.cpp in a fixed order; ONE translation unit, not standalone. Keep it that way
// until a real TU split is warranted.

// ---- character sprite (AIA billboard) --------------------------------------
// Besides the texture, a sprite carries where its tight bbox sits ON THE AIA CANVAS
// (x0,y0) plus that file's constant anchor (canvasW, foot-line oy = bill[2]). This is
// what keeps an animation from sliding: every frame is placed against the SAME canvas
// hotspot (canvasW/2, oy), and only the silhouette moves within it (build/anim_re/
// SPRITEANCHOR.md). place_ok stays false for non-AIA sprites (they set no canvas data).
struct SpriteTex {
    WGPUTexture tex{}; WGPUTextureView view{}; WGPUBindGroup bind{};
    std::uint32_t w = 0, h = 0;
    float x0 = 0, y0 = 0, canvasW = 0, oy = 0;
    float footFix = 0;   // per-file foot correction: oy - resting-frame bbox bottom (grounds
                         // large-canvas sprites whose oy sits below the art; see draw_character)
    bool place_ok = false;
    // World units per canvas pixel: the quad builder FUN_00446340 (0x4463ab..) takes
    // S = AIA header +0x1c x the actor's +0x248 x 1/32 (DAT_006bd104); the actor's factor is
    // applied at draw time. render_effects.md S1/S2.
    float S = 0.03125f;
    void set_placement(const AiaFrame& fr, std::uint32_t cw, float scaleA) {
        x0 = float(fr.x0); y0 = float(fr.y0); canvasW = float(cw); oy = fr.bill[2];
        S = scaleA * 0.03125f;
        place_ok = true;
    }
};

static SpriteTex upload_rgba(Gpu& g, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba,
                             WGPUSampler sampler = nullptr) {
    SpriteTex s; s.w = w; s.h = h;
    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D; td.size = {w, h, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
    s.tex = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTexelCopyTextureInfo dst = {}; dst.texture = s.tex; dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout lay = {}; lay.bytesPerRow = w * 4; lay.rowsPerImage = h;
    WGPUExtent3D ext = {w, h, 1};
    wgpuQueueWriteTexture(g.queue, &dst, rgba, std::size_t(w) * h * 4, &lay, &ext);
    s.view = wgpuTextureCreateView(s.tex, nullptr);
    s.bind = tex_bindgroup(g, s.view, sampler);
    return s;
}

// A playable character's sprite set: one AIA, with the 8 facings of the current
// animation frame decoded and uploaded. dir 0..7 = the 8 compass facings.
// Animation tags (verified for the C_10x player sprites):
//   image 0 (tag 0) = idle,  image 1 (tag 1) = walk.
struct CharSprite {
    Aia aia;
    std::uint32_t image = 0;              // current animation
    SpriteTex dirs[8];                    // per-facing texture (current frame)
    std::uint32_t ndir = 0, nframe = 0;

    bool load(Gpu& g, const fs::path& aia_path, std::uint32_t img = 0) {
        if (!aia.load(aia_path)) return false;
        if (img >= aia.nImg) return false;
        set_anim(g, img, 0);
        return true;
    }
    // Frame count of an animation (0 if it doesn't exist).
    std::uint32_t frames_of(std::uint32_t img) const {
        return img < aia.nImg ? aia.images[img].nFrame : 0;
    }
    // Look an animation up by its tag. Image *indices* differ between characters
    // (Adol carries an extra animation, shifting his idle from img3 to img4), but
    // the tags are canonical — always resolve by tag. -1 if absent.
    int find_tag(std::uint32_t tag) const {
        for (std::uint32_t i = 0; i < aia.nImg; ++i)
            if (aia.images[i].tag == tag) return int(i);
        return -1;
    }
    // Switch animation and frame, re-decoding the facings.
    void set_anim(Gpu& g, std::uint32_t img, std::uint32_t frame) {
        if (img >= aia.nImg) return;
        image = img;
        ndir = aia.images[img].nDir; nframe = aia.images[img].nFrame;
        set_frame(g, frame);
    }
    // (Re)decode `frame` for every facing into the dir textures.
    void set_frame(Gpu& g, std::uint32_t frame) {
        for (std::uint32_t d = 0; d < 8 && ndir > 0; ++d) {      // dir %= nDir (FUN_00444dc0)
            AiaFrame fr;
            // Body + the second layer, composited ADDITIVELY — the engine's own blend
            // (SRCALPHA/ONE, read out of FUN_004745a0). The layer is a real overlay:
            // Yunica's fire-sword, Toal's Boost aura, projectile orbs. ~90% of frames
            // have none and this is then identical to decode_frame.
            if (!aia.decode_frame_composited(image, d % ndir, frame % std::max(1u, nframe), fr))
                continue;
            if (dirs[d].tex) { wgpuBindGroupRelease(dirs[d].bind); wgpuTextureViewRelease(dirs[d].view); wgpuTextureRelease(dirs[d].tex); }
            dirs[d] = upload_rgba(g, fr.w, fr.h, fr.rgba.data(), g_sprite_point ? g.sampler_point : nullptr);
            dirs[d].set_placement(fr, aia.canvasW, aia.scaleA);   // canvas anchor -> no animation slide
        }
    }
};

// One world quad — a pipeline, a texture, four vertices — onto (color, depth), which are kept. Drawn at once in its
// own pass, or, inside a QuadScope, recorded and drawn with its neighbours in ONE pass at the scope's end: the same
// draws in the same order with the same state, so the picture is the same. A room with 29 enemies made 58 passes,
// 58 submits and 116 two-triangle buffers a frame for their sprites and shadows alone (S_6015: 9 of 13 ms).
struct QuadBatch {
    struct Draw { WGPURenderPipeline pipe; WGPUBindGroup bind; };
    bool on = false;                              // inside a QuadScope
    WGPUTextureView color = nullptr, depth = nullptr;
    std::vector<Draw> draws;
    std::vector<RoomVertex> verts;                // four per draw
    WGPUBuffer vb = nullptr, ib = nullptr;
    std::size_t cap = 0;                          // vb's size in quads
};
static QuadBatch g_quads;

static void quad_pass(Gpu& g, WGPUTextureView color, WGPUTextureView depth, WGPUBuffer vb, WGPUBuffer ib,
                      const QuadBatch::Draw* draws, std::size_t n) {
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;   // keep the room
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = depth; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    for (std::size_t i = 0; i < n; ++i) {
        if (i == 0 || draws[i].pipe != draws[i - 1].pipe) wgpuRenderPassEncoderSetPipeline(pass, draws[i].pipe);
        if (i == 0 || draws[i].bind != draws[i - 1].bind) wgpuRenderPassEncoderSetBindGroup(pass, 1, draws[i].bind, 0, nullptr);
        wgpuRenderPassEncoderDrawIndexed(pass, 6, 1, 0, std::int32_t(i * 4), 0);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
}

static void quad_flush(Gpu& g) {
    QuadBatch& q = g_quads;
    if (q.draws.empty()) return;
    static const std::uint32_t kIdx[6] = {0, 1, 2, 0, 2, 3};
    if (!q.ib) q.ib = make_buffer(g.device, g.queue, kIdx, sizeof kIdx, WGPUBufferUsage_Index);
    if (q.cap < q.draws.size()) {
        if (q.vb) wgpuBufferRelease(q.vb);
        q.cap = std::max<std::size_t>(256, q.draws.size() * 2);
        WGPUBufferDescriptor bd = {};
        bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = q.cap * 4 * sizeof(RoomVertex);
        q.vb = wgpuDeviceCreateBuffer(g.device, &bd);
    }
    wgpuQueueWriteBuffer(g.queue, q.vb, 0, q.verts.data(), q.verts.size() * sizeof(RoomVertex));
    quad_pass(g, q.color, q.depth, q.vb, q.ib, q.draws.data(), q.draws.size());
    q.draws.clear(); q.verts.clear();
}

// Batches the draw_quad calls made while it lives (draw_shadow / draw_character / draw_billboard / _uv). Nothing
// else may draw onto the same target inside the scope: it would land before the quads recorded so far.
struct QuadScope {
    Gpu& g;
    explicit QuadScope(Gpu& gpu) : g(gpu) { g_quads.on = true; }
    ~QuadScope() { quad_flush(g); g_quads.on = false; }
};

static void draw_quad(Gpu& g, WGPUTextureView color, WGPUTextureView depth, WGPURenderPipeline pipe, WGPUBindGroup bind,
                      const RoomVertex verts[4]) {
    QuadBatch& q = g_quads;
    if (q.on) {
        if (!q.draws.empty() && (q.color != color || q.depth != depth)) quad_flush(g);
        q.color = color; q.depth = depth;
        q.draws.push_back({pipe, bind});
        q.verts.insert(q.verts.end(), verts, verts + 4);
        return;
    }
    static const std::uint32_t kIdx[6] = {0, 1, 2, 0, 2, 3};
    WGPUBuffer vb = make_buffer(g.device, g.queue, verts, 4 * sizeof(RoomVertex), WGPUBufferUsage_Vertex);
    WGPUBuffer ib = make_buffer(g.device, g.queue, kIdx, sizeof kIdx, WGPUBufferUsage_Index);
    const QuadBatch::Draw d{pipe, bind};
    quad_pass(g, color, depth, vb, ib, &d, 1);
    wgpuBufferRelease(vb); wgpuBufferRelease(ib);
}

// A sprite vertex's D3D specular (0xRRGGBB, added after the texture modulate, before fog): fs_char
// reads it from nrm.y encoded as -(1 + rgb) (main.cpp vs); 0 = none.
static float spec_ny(std::uint32_t spec) {
    spec &= 0xffffffu;
    return spec ? -(1.0f + float(spec)) : 0.0f;
}

// A quad in the camera's own plane: centre +/- right * half +/- up * half, the whole texture, the
// char pipeline (alpha-over). `right` / `up` are the camera's (inverse-view rows 0 / 1), i.e. the
// engine's 0x7961b0 billboard (FUN_005eca30 copies the camera world matrix to camera+0x340 with
// its translation zeroed; the render camera block at 0x795e70 holds that copy at 0x7961b0).
static void draw_billboard(Gpu& g, WGPUTextureView color, WGPUTextureView depth, const SpriteTex& spr,
                           const Vec3& centre, float half, const Vec3& right, const Vec3& up,
                           std::uint32_t spec = 0, float tr = 1.0f, float tg = 1.0f, float tb = 1.0f,
                           float opacity = 1.0f) {
    if (!spr.tex) return;
    const std::uint8_t cr = std::uint8_t(std::clamp(tr, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cg = std::uint8_t(std::clamp(tg, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cb = std::uint8_t(std::clamp(tb, 0.0f, 1.0f) * 255.0f);
    const float nx = 1.0f - std::clamp(opacity, 0.0f, 1.0f), ny = spec_ny(spec);
    auto V = [&](float sx, float sy, float u, float v) -> RoomVertex {
        return {centre.x + (right.x * sx + up.x * sy) * half, centre.y + (right.y * sx + up.y * sy) * half,
                centre.z + (right.z * sx + up.z * sy) * half, nx, ny, -1, u, v, cr, cg, cb, 0};
    };
    RoomVertex verts[4] = {V(-1, 1, 0, 0), V(1, 1, 1, 0), V(1, -1, 1, 1), V(-1, -1, 0, 1)};
    draw_quad(g, color, depth, g.char_pipeline, spr.bind, verts);
}

// Draw an upright, camera-facing billboard of `spr` standing at `feet`, in a
// pass that keeps the already-rendered room colour + depth (so it occludes
// correctly). worldH is the sprite's world height; camForward is the (fixed)
// camera view direction. Reuses the room pipeline (alpha-test + white modulate).
static void draw_character(Gpu& g, WGPUTextureView color, WGPUTextureView depth,
                           const Vec3& feet, float worldH, const SpriteTex& spr,
                           const Vec3& camForward, const Vec3& camPos, float flash = 0.0f,
                           float tr = 1.0f, float tg = 1.0f, float tb = 1.0f,
                           bool additive = false, float opacity = 1.0f,
                           float px_dx = 0.0f, float px_dy = 0.0f, float actor_scale = 1.0f,
                           std::uint32_t spec = 0, float depth_bias = 0.0f) {
    if (!spr.tex || spr.h == 0) return;
    const Vec3 up = {0, 1, 0};
    Vec3 right = normalize(cross(up, camForward));       // horizontal screen-right
    // Nudge the billboard a SMALL distance toward the camera ALONG THE VIEW RAY, exactly as
    // the engine does (build/anim_re/SPRITEDEPTH.md: base = feet - normalize(feet-camPos)*B,
    // B ~= 0.1 world units = bill[0]). The ray's upward component lifts the base off the
    // floor so it wins the LessEqual z-test against the floor plane. ** The old fix shoved
    // 0.9 units HORIZONTALLY, ~9x too far, which punched the sprite in front of any fence
    // closer than 0.9u ** -- this small full-3D nudge clears the floor without beating a
    // fence (a fence more than ~0.12u in front still occludes the sprite, as it should).
    Vec3 ray = {feet.x - camPos.x, feet.y - camPos.y, feet.z - camPos.z};
    const float rl = std::sqrt(ray.x * ray.x + ray.y * ray.y + ray.z * ray.z);
    if (rl > 1e-4f) { ray.x /= rl; ray.y /= rl; ray.z /= rl; }
    // Engine uses ~0.1 (bill[0]) PLUS an oz/pitch per-vertex depth lean; we don't model the
    // lean yet, so a slightly larger uniform nudge covers the floor at steep angles. Still
    // far under a fence's ~0.5u standoff, so occlusion stays correct. [H-med] until the lean.
    // `depth_bias` is the script's anime_CameraDistanceOffset (AsoVm::sprite_depth_offset; ASO_NOOPS.md §2): the
    // engine's distance is bill[0] + it. [H] it is added to the stand-in nudge until bill[0] and the lean are ported.
    constexpr float kDepthNudge = 0.2f;
    const float nudge = kDepthNudge + depth_bias;
    const Vec3 base = {feet.x - ray.x * nudge, feet.y - ray.y * nudge, feet.z - ray.z * nudge};
    // Quad extents in billboard-local units (X = along `right`, Y = world up), from feet.
    float lX, rX, tY, bY;
    if (spr.place_ok && spr.canvasW > 0) {
        // Canvas-anchored (SPRITEANCHOR.md): place the tight bbox at its (x0,y0) on the
        // AIA canvas, measured from the file's CONSTANT hotspot (canvasW/2, oy=foot line),
        // with ONE scale for every frame. That fixed hotspot is what stops an animation
        // sliding -- the attack's silhouette shifts inside the frame, not the whole sprite.
        // S: world units per canvas pixel (the file's scale x the actor's +0x248 / 32).
        const float S = spr.S * actor_scale;
        lX = (spr.x0 - spr.canvasW * 0.5f) * S;
        rX = (spr.x0 + float(spr.w) - spr.canvasW * 0.5f) * S;
        // footFix (per file, constant across frames -> no slide) drops the foot line to the
        // resting art bottom so a large-canvas sprite (M_840, oy 8px under its feet) grounds.
        const float foot = spr.oy - spr.footFix;
        tY = (foot - spr.y0) * S;
        bY = (foot - (spr.y0 + float(spr.h))) * S;
    } else {
        // Fallback (no canvas data, e.g. a DDS sprite): centre the tight bbox at the feet
        // and span feet.y .. feet.y+worldH, keeping the old aspect-preserving behaviour.
        const float Wc = worldH * (float(spr.w) / float(spr.h));
        lX = -Wc * 0.5f; rX = Wc * 0.5f; tY = worldH; bY = 0.0f;
    }
    // Chr_Tremble's offset (FUN_00446340 0x4463ca..0x446419): (dx, dy) AIA pixels added to the
    // quad origin before the pixel scale, x right, y onto the top edge (up). Same canvas-pixel
    // scale as the sprite itself (the AIA scale x the actor's x 0.03125).
    if (px_dx != 0.0f || px_dy != 0.0f) {
        const float kPx = spr.S * actor_scale;
        lX += px_dx * kPx; rX += px_dx * kPx; tY += px_dy * kPx; bY += px_dy * kPx;
    }
    // Vertex-colour alpha carries the hit-flash amount (fs_char lerps to white by it); RGB is
    // a per-quad modulate (tc.rgb * color.rgb) — white = as-is, a red tint marks event seeds.
    const std::uint8_t fa = std::uint8_t(std::max(0.0f, std::min(1.0f, flash)) * 255.0f);
    const std::uint8_t cr = std::uint8_t(std::clamp(tr, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cg = std::uint8_t(std::clamp(tg, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cb = std::uint8_t(std::clamp(tb, 0.0f, 1.0f) * 255.0f);
    const float ny = spec_ny(spec);
    auto V = [&](float lx, float ly, float u, float v) -> RoomVertex {
        // nx carries 1 - opacity to fs_char (the actor's +0x4d8); 0 = fully opaque. ny: the specular.
        return {base.x + right.x * lx, base.y + ly, base.z + right.z * lx,
                1.0f - std::clamp(opacity, 0.0f, 1.0f), ny, -1, u, v, cr, cg, cb, fa};
    };
    RoomVertex verts[4] = {V(lX, tY, 0, 0), V(rX, tY, 1, 0), V(rX, bY, 1, 1), V(lX, bY, 0, 1)};
    draw_quad(g, color, depth, additive && g.char_pipeline_add ? g.char_pipeline_add : g.char_pipeline, spr.bind, verts);
}

// GoPopup glyphs (src/cleria/popup.hpp, PICKUPS.md §13.5): F_GEO20 quads on a billboard at
// the anchor, risen per glyph, the popup's colour and alpha. `depth` must be a fresh (cleared)
// depth view: the engine draws them with no Z test, so geometry never hides one. The billboard
// keeps world up [H: 0x7961b0 is the camera-facing matrix; its exact axes are open].
static void draw_popup_glyphs(Gpu& g, WGPUTextureView color, WGPUTextureView depth, const SpriteTex& font,
                              const Popup& p, const Vec3& camForward) {
    if (!font.tex || p.glyphs.empty()) return;
    const Vec3 right = normalize(cross(Vec3{0, 1, 0}, camForward));
    const std::uint32_t d = p.diffuse();
    const float op = float(d >> 24) / 255.0f;
    std::vector<RoomVertex> verts;
    std::vector<std::uint32_t> idx;
    for (std::size_t i = 0; i < p.glyphs.size(); ++i) {
        const PopupGlyph& gl = p.glyphs[i];
        const float r = p.rise(int(i));
        auto V = [&](float lx, float ly, float u, float v) -> RoomVertex {
            return {p.pos.x + right.x * lx, p.pos.y + ly + r, p.pos.z + right.z * lx, 1.0f - op, 0, -1, u, v,
                    std::uint8_t(d >> 16), std::uint8_t(d >> 8), std::uint8_t(d), 0};
        };
        const std::uint32_t b = std::uint32_t(verts.size());
        verts.push_back(V(gl.x0, gl.top, gl.u0, gl.v0));
        verts.push_back(V(gl.x1, gl.top, gl.u1, gl.v0));
        verts.push_back(V(gl.x1, gl.bottom, gl.u1, gl.v1));
        verts.push_back(V(gl.x0, gl.bottom, gl.u0, gl.v1));
        for (std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(b + k);
    }
    WGPUBuffer vb = make_buffer(g.device, g.queue, verts.data(), verts.size() * sizeof(RoomVertex), WGPUBufferUsage_Vertex);
    WGPUBuffer ib = make_buffer(g.device, g.queue, idx.data(), idx.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = depth; da.depthLoadOp = WGPULoadOp_Clear; da.depthClearValue = 1.0f; da.depthStoreOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, g.char_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, font.bind, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, std::uint32_t(idx.size()), 1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    wgpuBufferRelease(vb); wgpuBufferRelease(ib);
}

// A soft round floor shadow under a character (build/render_re/ENVIRONMENT.md). The engine
// draws a flat black alpha decal on the floor, NOT a projected silhouette. SHADOW0.DDS is
// 64x64 with black RGB and a radial alpha falloff (peak ~0.44); loaded RGBA8 it drives
// fs_char to output (0,0,0,texA), and the alpha-over blend darkens the floor by up to ~44%.
// A camera-facing quad at `base` spanning [x0, x1] along the yaw-only camera right and [y0, y1]
// up, sampling `uv` of `tex`, at `opacity`. The bubbles' FUN_004ffdb0 basis (RotationY of the view
// yaw, upright). [approx] depth-tested through the character pipeline (the engine's 0x41 state has
// Z test off).
static void draw_billboard_uv(Gpu& g, WGPUTextureView color, WGPUTextureView depth, const SpriteTex& tex,
                              const Vec3& base, float x0, float x1, float y0, float y1,
                              float u0, float v0, float u1, float v1, const Vec3& camForward, float opacity) {
    if (!tex.tex) return;
    const Vec3 right = normalize(cross(Vec3{0, 1, 0}, camForward));
    auto V = [&](float lx, float ly, float u, float v) -> RoomVertex {
        return {base.x + right.x * lx, base.y + ly, base.z + right.z * lx,
                1.0f - std::clamp(opacity, 0.0f, 1.0f), 0, -1, u, v, 255, 255, 255, 0};
    };
    RoomVertex verts[4] = {V(x0, y1, u0, v0), V(x1, y1, u1, v0), V(x1, y0, u1, v1), V(x0, y0, u0, v1)};
    draw_quad(g, color, depth, g.char_pipeline, tex.bind, verts);
}

struct ShadowFx {
    SpriteTex tex;
    bool init(Gpu& g, const fs::path& asset_root) {
        Dds d = load_dds(asset_root / "IMAGE" / "SHADOW0.DDS");
        if (!d.ok()) return false;
        tex = upload_rgba(g, d.width, d.height, d.data.data());
        return true;
    }
};

// Draw the floor shadow decal for an actor at `feet`, on the floor plane at `floorY`, with
// half-width `radius`. A flat quad on XZ (normal +Y), alpha-over, depth-read-no-write, so it
// darkens the floor and is occluded by nearer geometry but never writes depth. Draw BEFORE
// the sprite. Vertex colour (255,255,255,0): no fog dim, no hit-flash -> pure black * texA.
// `rgb` modulates the texture (0 = a black silhouette of a coloured texture, the pickups'
// flat icon shadow) and `opacity` scales its alpha (nx carries 1 - opacity, as for sprites).
static void draw_shadow(Gpu& g, WGPUTextureView color, WGPUTextureView depth,
                        const SpriteTex& shadow, const Vec3& feet, float floorY, float radius,
                        std::uint8_t rgb = 255, float opacity = 1.0f) {
    if (!shadow.tex) return;
    const float y = floorY + 0.05f;   // a hair above the floor to avoid decal z-fight
    const float nx = 1.0f - std::clamp(opacity, 0.0f, 1.0f);
    auto V = [&](float sx, float sz, float u, float v) -> RoomVertex {
        return {feet.x + sx * radius, y, feet.z + sz * radius, nx, 1, 0, u, v, rgb, rgb, rgb, 0};
    };
    RoomVertex verts[4] = {V(-1, -1, 0, 0), V(1, -1, 1, 0), V(1, 1, 1, 1), V(-1, 1, 0, 1)};
    draw_quad(g, color, depth, g.shadow_pipeline, shadow.bind, verts);
}

// Draw a screen-space textured quad (UI/title images) at NDC corners (l,t)-(r,b), z in [0,1].
// Reuses the char pipeline (alpha-over, fs_char); the caller writes an IDENTITY MVP + fog off
// beforehand so the RoomVertex positions are taken as clip space directly. `clear` starts a
// fresh frame (clears colour to `bg` + depth); later images Load. Vertex colour (255,255,255,0):
// no fog dim, no hit-flash -> the texture shows as-is.
static void draw_screen_image(Gpu& g, WGPUTextureView color, WGPUTextureView depth, bool clear,
                              const SpriteTex& tex, float l, float t, float r, float b, float z,
                              float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1,
                              float bgr = 0.02f, float bgg = 0.02f, float bgb = 0.05f,
                              float wtint = 0.0f, bool additive = false,
                              float tr = 1.0f, float tg = 1.0f, float tb = 1.0f) {
    if (!tex.tex) return;
    // Vertex alpha carries a white-tint amount (fs_char mixes the sampled colour toward white by
    // it) -- used for the char-select selection-bar pulse. 0 = the texture as-is.
    // Vertex RGB is a per-quad MODULATE colour (fs_char does tc.rgb * color.rgb); default white =
    // texture as-is, <1 dims it (e.g. 0.5 -> 0x808080 for the Options "dim the unpicked rows" pass).
    const std::uint8_t wt = (std::uint8_t)(std::clamp(wtint, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cr = (std::uint8_t)(std::clamp(tr, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cg = (std::uint8_t)(std::clamp(tg, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cb = (std::uint8_t)(std::clamp(tb, 0.0f, 1.0f) * 255.0f);
    RoomVertex verts[4] = {{l, t, z, 0, 0, -1, u0, v0, cr, cg, cb, wt},
                           {r, t, z, 0, 0, -1, u1, v0, cr, cg, cb, wt},
                           {r, b, z, 0, 0, -1, u1, v1, cr, cg, cb, wt},
                           {l, b, z, 0, 0, -1, u0, v1, cr, cg, cb, wt}};
    std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    WGPUBuffer vb = make_buffer(g.device, g.queue, verts, sizeof verts, WGPUBufferUsage_Vertex);
    WGPUBuffer ib = make_buffer(g.device, g.queue, idx, sizeof idx, WGPUBufferUsage_Index);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {bgr, bgg, bgb, 1.0};
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = depth; da.depthLoadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    da.depthStoreOp = WGPUStoreOp_Store; da.depthClearValue = 1.0f;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, additive && g.char_pipeline_add ? g.char_pipeline_add : g.char_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, tex.bind, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, 6, 1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    wgpuBufferRelease(vb); wgpuBufferRelease(ib);
}

// A world heading (hx,hz) -> the 8-direction sprite index, screen-relative to the camera
// yaw. One calibration shared by live movement and by SEN arrival facing: the C_10x ring
// runs dir0=screen-right, +45 deg CCW per step (screen-up = dir2 back, down = dir6 front,
// left = dir4). Screen basis: fwd = (cos yaw, sin yaw) is screen-up, right = cross(up,fwd)
// = (sin yaw, -cos yaw). h need not be normalized (atan2 is scale-invariant).
static int heading_to_facing(float hx, float hz, float cam_yaw) {
    const float fx = std::cos(cam_yaw), fz = std::sin(cam_yaw);
    const float sUp = hx * fx + hz * fz;      // component along screen-up
    const float sRt = hx * fz - hz * fx;      // component along screen-right
    const float deg = std::atan2(sUp, sRt) * 57.29578f;
    return int(std::lround((deg + 360.0f) / 45.0f)) & 7;
}

// ---- headless shot ----------------------------------------------------------
struct MapReq { bool done = false; };
static void on_map(WGPUMapAsyncStatus, WGPUStringView, void* u1, void*) {
    static_cast<MapReq*>(u1)->done = true;
}

static int render_shot(Gpu& g, const Room& room, const fs::path& out,
                       std::uint32_t W, std::uint32_t H, float yaw, float pitch, float dist) {
    Camera cam; spawn_in(cam, room);
    // override to the requested orbit angle
    const Vec3 c = {(room.mesh.bmin.x + room.mesh.bmax.x) * 0.5f,
                    (room.mesh.bmin.y + room.mesh.bmax.y) * 0.5f,
                    (room.mesh.bmin.z + room.mesh.bmax.z) * 0.5f};
    const float rad = room_diag(room) * dist;
    cam.pos = {c.x + rad * std::cos(pitch) * std::cos(yaw), c.y + rad * std::sin(pitch),
               c.z + rad * std::cos(pitch) * std::sin(yaw)};
    const Vec3 fwd = normalize(c - cam.pos);
    cam.pitch = std::asin(std::max(-1.0f, std::min(1.0f, fwd.y)));
    cam.yaw = std::atan2(fwd.z, fwd.x);
    write_mvp(g, cam.view_proj(float(W) / float(H), rad * 0.02f, rad * 6.0f));

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
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPUTexelCopyTextureInfo src = {}; src.texture = color; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {}; dst.buffer = readback;
    dst.layout.bytesPerRow = bpr; dst.layout.rowsPerImage = H;
    WGPUExtent3D ext = {W, H, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);

    MapReq mr;
    WGPUBufferMapCallbackInfo mi = {};
    mi.mode = WGPUCallbackMode_AllowProcessEvents; mi.callback = on_map; mi.userdata1 = &mr;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, rbd.size, mi);
    while (!mr.done) wgpuDevicePoll(g.device, true, nullptr);
    const auto* px = static_cast<const std::uint8_t*>(
        wgpuBufferGetConstMappedRange(readback, 0, rbd.size));
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
            f.write(reinterpret_cast<const char*>(px + std::size_t(y) * bpr + x * 4), 3);
    wgpuBufferUnmap(readback);
    std::printf("wrote %s (%ux%u)\n", out.string().c_str(), W, H);
    return 0;
}

