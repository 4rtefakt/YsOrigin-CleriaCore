// cleria-view — the STATUS_512 HUD atlas + Hud
// Textual part of main.cpp (split 2026-07-23, PLAN.md Phase 0): #included by
// main.cpp in a fixed order; ONE translation unit, not standalone. Keep it that way
// until a real TU split is warranted.

struct HudVertex { float x, y, u, v; std::uint8_t r, g, b, a; };

// STATUS_512.DDS source rects (measured; build/hud_re/hud_atlas_spec.json).
struct Rc { float x, y, w, h; };
// The gold digit font is MONOSPACE: fixed 12x12 cells at (digit*12, 24).
//
// From the emitter itself (inside builder A, 0x0043e550..0x0043e7b1):
//   MULSS XMM0, [0x006bd948]   ; f = 12.0   -> u = digit * 12
//   MULSS XMM0, [0x006bd008]   ; f = 1/512
//   v = 24/512, du = dv = 12/512
// so the cell is 12px and the band is y=24 h=12 — which also independently confirms
// the band correction we measured off the atlas pixels (an earlier spec said y15 h22
// and dragged an orange bar into every digit).
//
// hud_atlas_spec.json's per-glyph x/w (0:x0 w11, 1:x15 w6, 2:x24 w11, ...) are the
// INK EXTENTS inside those cells, not sampling rects. Sampling by ink would shift
// every glyph. `digit % 10` only ever reaches cells 0..9, so the ':' and '/' cells
// (10, 11) and the whole white/blue set at x>=144 are unreachable from this loop.
static Rc digit_cell(int d) { return {float(d) * 12.0f, 24.0f, 12.0f, 12.0f}; }
// Character portrait units (round face + ring + item tab), by g_flags[150].
// 0/1=Yunica, 2=Hugo, 3=Toal (Adol @250,257 is unused in Origin).
static const Rc kPortrait[4] = {
    {364,257,116,100}, {364,257,116,100}, {250,362,116,100}, {364,362,116,100}};
// The bar cluster — every rect below is the ENGINE's own, read from the builders'
// UV immediates, not measured off the art.
//
// THE RULE THAT MATTERS: the engine's source rects are FULL CELLS WITH TRANSPARENT
// MARGINS. Measuring opaque-alpha extents under-reports them every time — it bit us
// on the digit cells (12px cells, ink inset), on the pointed bars (252x8 cells whose
// ink is only {2,1,248,7}), and on the housing. Use the cell, not the ink, or things
// shift by a couple of px and the tips lose their margin.
//
// The channel stack inside the housing (1024x768):
//   EXP orange  {142, 718, +ratio*230, 722}   <- the ONLY non-1:1 quad (see below)
//   HP blue     {120, 726, +(hp/max)*252, 734}
//   HP trail    {120+(hp/max)*252 .. +(lag/max)*252, 726, 734}
//   MP / skill  {120, 737, +(mp/mpmax)*252, 745}
static const Rc kBarHousing  = {0, 468, 271, 44};  // confirmed from quad 2's UVs
static const Rc kHpBlue      = {0,   0, 252, 8};   // HP fill      (ink is {2,1,248,7})
static const Rc kHpTrail     = {0,   8, 252, 8};   // HP damage trail
static const Rc kExpOrange   = {0,  16, 240, 8};   // EXP fill — 240 art -> 230 screen
static const Rc kMpGreen     = {0,  44, 252, 8};   // green mana/skill fill (measured: the
                                                   // pointed green bar stacked under HP/red/pink)
static const Rc kBarTick     = {302, 1,   3, 14};  // segment-divider tick; the atlas holds a
                                                   // strip of these at x277+25k (stride 25)
static const Rc kSlotFrame   = {472, 472, 40, 40}; // the 40x40 equipment slot frame

// ---- the game's real HUD layout (from build/source_map/HUD_COORDS.md) --------
// Recovered from the status-HUD renderer FUN_004295d0 / builder FUN_0043c230.
// The HUD is authored in a 1024x768 reference frame, 1:1 against the 512x512
// STATUS atlas, and mapped to the backbuffer by FUN_005e3ed0 / FUN_005e4100 /
// FUN_005d3be0. Every element is LEFT-anchored (all 72 MapCoord calls are
// axis=X, mode=1), the effective scale is always H/768, and widescreen is Hor+:
// the HUD does NOT stretch, the extra width just reveals more scene.
struct HudMap {
    float sx = 1, sy = 1;
    bool wide = false, identity = false;
    HudMap(std::uint32_t W, std::uint32_t H) {
        const int w43 = int(H * 4 / 3);            // integer division, as in the exe
        wide = int(W) > w43;
        identity = (W == 1024 && H == 768);        // the exe's literal no-scale case
        sy = float(H) / 768.0f;
        sx = wide ? float(w43) / 1024.0f : float(W) / 1024.0f;
    }
    float x(float v) const {
        if (identity) return std::round(v) - 0.5f;
        return wide ? std::trunc(v * sx) : std::round(v * sx) - 0.5f;
    }
    float y(float v) const {
        return identity ? std::round(v) - 0.5f : std::round(v * sy) - 0.5f;
    }
};

// Authored rects in the 1024x768 frame, {x0, y0, x1, y1}.
struct Ar { float x0, y0, x1, y1; };
static const Ar kAPortrait = {0, 670, 116, 766};    // character portrait, 116x96
static const Ar kABarPlate = {115, 714, 386, 758};  // the housing / plate
static const Ar kAHpFill  = {120, 726, 372, 734};  // blue HP; max fill 252
static const Ar kAExpFill = {142, 718, 372, 722};  // orange EXP; max fill 230
static const Ar kAThinBar  = {142, 718, 372, 722};  // thin bar (EX/MP — name unconfirmed)
static const Ar kAMpFill   = {120, 737, 372, 745};  // green MP channel (below HP); max fill 252
static const Ar kASlot[4] = {{0, 620, 40, 632}, {0, 632, 40, 644},
                             {0, 644, 40, 656}, {0, 656, 40, 668}};  // equipment slots
// Portrait UVs: 4 portraits in a 2x2 grid, 116x96 px each, indexed by character.
// base (244,260); +120 x for Yunica/Toal, +104 y for Hugo/Toal.
static const Rc kPortraitReal[4] = {{244, 260, 116, 96},   // 0 Adol
                                    {364, 260, 116, 96},   // 1 Yunica
                                    {244, 364, 116, 96},   // 2 Hugo
                                    {364, 364, 116, 96}};  // 3 Toal
static const Rc kArcBlue   = {4, 378, 68, 73};    // EX arc gauge
static const Rc kArcRed    = {74, 378, 66, 73};   // MP/boost arc gauge
// Skill/item hotbar diamond slots (first row of the left icon block, cell 38).
static const Rc kSkillSlot[4] = {{1,133,38,38}, {41,133,38,38}, {81,133,38,38}, {121,133,38,38}};

struct Hud {
    WGPURenderPipeline pipeline{};
    WGPUBindGroup bind{};                // font atlas bind group (group 0)
    WGPUBindGroupLayout bgl{};           // shared group-0 layout
    WGPUSampler samp{};
    WGPUBuffer vbuf{};
    std::size_t vbuf_cap = 0;
    std::uint32_t aw = 0, ah = 0, cw = 0, ch = 0, first = 32, cols = 16, nchars = 95;
    std::vector<HudVertex> verts;        // font + solid rects (font atlas)
    // Optional secondary texture atlas (the game's STATUS_512 HUD graphics).
    WGPUBindGroup atlas_bind{};
    std::uint32_t atlas_w = 0, atlas_h = 0;
    std::vector<HudVertex> sprites;      // quads sampling the STATUS atlas
    // Boss health bar (HP2 atlas). Shown when boss_frac >= 0.
    WGPUBindGroup boss_bind{};
    std::uint32_t boss_w = 0, boss_h = 0;
    std::vector<HudVertex> boss_sprites; // quads sampling the HP2 atlas
    // font_A = F_MINI.DDS, the engine's 12x14 proportional bitmap font (minifont.hpp,
    // FUN_00452980). Drives the [EX]/[MP]/[SP] readouts (HUD_COUNTERS.md §2-3).
    WGPUBindGroup mini_bind{};
    MiniFont mini;
    std::vector<HudVertex> mini_verts;
    // Equipped-item icons (HUD_PORTRAIT_SLOTS.md): one DDS per item id from MENU/ICON, NOT
    // atlas sub-rects, so each needs its own bind group. Slot order is weapon / armor /
    // footwear / accessory[0]; a slot with id < 0 draws its bare frame.
    std::unordered_map<int, WGPUBindGroup> icon_binds;
    WGPUBindGroup equip_bind[4] = {};
    std::vector<HudVertex> equip_verts[4];
    float boss_frac = -1.f;              // <0 = no boss gauge (DAT_006d5fd8 == 0)
    std::string boss_name;               // not drawn: the retail gauge has no text
    // Widget 3 (0x6d5fd4): +0x04 owner id, +0x30 cur, +0x34 lag, +0x38 max, +0x44 lenScale; the
    // emblem DAT_0076c214 (0 default, 1..8 = boss01..08).
    int boss_id = 0, boss_emblem = 0;
    float boss_cur = 0, boss_lag = 0, boss_max = 0, boss_len = 3000;
    // FUN_0043a460: a new owner starts with no trail; the same owner trails from its last cur.
    void set_boss_gauge(int id, float cur, float max, float len_scale, int emblem) {
        boss_lag = (boss_id == id) ? boss_cur : cur;
        boss_id = id; boss_cur = cur; boss_max = max;
        boss_len = len_scale == 0.0f ? max : len_scale;
        boss_emblem = emblem;
        boss_frac = max > 0 ? std::clamp(cur / max, 0.0f, 1.0f) : 0.0f;
    }
    void hide_boss_gauge() { boss_frac = -1.0f; boss_id = 0; }
    // FUN_0043a540, every 60 Hz step: the lag moves toward cur by w+0x2c (0.02) x max x 0.2.
    void tick_boss_gauge() {
        if (boss_frac < 0.0f) return;
        const float step = 0.02f * boss_max * 0.2f;
        if (boss_lag > boss_cur) boss_lag = std::max(boss_cur, boss_lag - step);
        else if (boss_lag < boss_cur) boss_lag = std::min(boss_cur, boss_lag + step);
    }
    struct Msg { std::string text; float ttl; };
    std::vector<Msg> messages;

    void init(Gpu& g, const fs::path& font_bin, WGPUTextureFormat color) {
        std::vector<std::uint8_t> b = read_bytes(font_bin);
        if (b.size() < 32) { std::fprintf(stderr, "hud: font missing\n"); return; }
        std::uint32_t hdr[8];
        std::memcpy(hdr, b.data(), 32);
        aw = hdr[1]; ah = hdr[2]; cw = hdr[3]; ch = hdr[4]; first = hdr[5]; nchars = hdr[6]; cols = hdr[7];
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = {aw, ah, 1};
        td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
        WGPUTexture tex = wgpuDeviceCreateTexture(g.device, &td);
        WGPUTexelCopyTextureInfo dst = {}; dst.texture = tex; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay = {}; lay.bytesPerRow = aw * 4; lay.rowsPerImage = ah;
        WGPUExtent3D ext = {aw, ah, 1};
        wgpuQueueWriteTexture(g.queue, &dst, b.data() + 32, std::size_t(aw) * ah * 4, &lay, &ext);
        WGPUTextureView view = wgpuTextureCreateView(tex, nullptr);
        wgpuTextureRelease(tex);

        WGPUShaderSourceWGSL wgsl = {};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL; wgsl.code = sv(kHudWGSL);
        WGPUShaderModuleDescriptor smd = {}; smd.nextInChain = &wgsl.chain;
        WGPUShaderModule sm = wgpuDeviceCreateShaderModule(g.device, &smd);
        WGPUVertexAttribute at[3] = {};
        at[0].format = WGPUVertexFormat_Float32x2; at[0].offset = 0;  at[0].shaderLocation = 0;
        at[1].format = WGPUVertexFormat_Float32x2; at[1].offset = 8;  at[1].shaderLocation = 1;
        at[2].format = WGPUVertexFormat_Unorm8x4;  at[2].offset = 16; at[2].shaderLocation = 2;
        WGPUVertexBufferLayout vbl = {};
        vbl.arrayStride = sizeof(HudVertex); vbl.attributeCount = 3; vbl.attributes = at;
        WGPUBlendState blend = {};
        blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
        blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
        WGPUColorTargetState tgt = {}; tgt.format = color; tgt.blend = &blend; tgt.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs = {}; fs.module = sm; fs.entryPoint = sv("fs"); fs.targetCount = 1; fs.targets = &tgt;
        WGPURenderPipelineDescriptor pd = {};
        pd.vertex.module = sm; pd.vertex.entryPoint = sv("vs");
        pd.vertex.bufferCount = 1; pd.vertex.buffers = &vbl;
        pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFF; pd.fragment = &fs;
        pipeline = wgpuDeviceCreateRenderPipeline(g.device, &pd);
        fs.entryPoint = sv("fs_opaque");                 // the CrossFade overlay
        overlay_pipeline = wgpuDeviceCreateRenderPipeline(g.device, &pd);
        overlay_bgl = wgpuRenderPipelineGetBindGroupLayout(overlay_pipeline, 0);

        WGPUSamplerDescriptor sad = {};
        sad.magFilter = sad.minFilter = WGPUFilterMode_Linear; sad.maxAnisotropy = 1;
        samp = wgpuDeviceCreateSampler(g.device, &sad);
        bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry e[2] = {};
        e[0].binding = 0; e[0].textureView = view;
        e[1].binding = 1; e[1].sampler = samp;
        WGPUBindGroupDescriptor bd = {};
        bd.layout = bgl; bd.entryCount = 2; bd.entries = e;
        bind = wgpuDeviceCreateBindGroup(g.device, &bd);
        wgpuShaderModuleRelease(sm);
    }

    // Upload a DDS (RGBA/BC1/BC2) as a group-0 bind group; returns it and its size.
    WGPUBindGroup upload_atlas(Gpu& g, const fs::path& dds_path, std::uint32_t& ow, std::uint32_t& oh) {
        Dds d = load_dds(dds_path);
        if (!d.ok()) { std::fprintf(stderr, "hud atlas: %s failed\n", dds_path.string().c_str()); return nullptr; }
        ow = d.width; oh = d.height;
        WGPUTextureFormat fmt = WGPUTextureFormat_RGBA8Unorm;
        std::uint32_t block = 1, blk = 4;
        if (d.format == Dds::BC1) { fmt = WGPUTextureFormat_BC1RGBAUnorm; block = 4; blk = 8; }
        else if (d.format == Dds::BC2) { fmt = WGPUTextureFormat_BC2RGBAUnorm; block = 4; blk = 16; }
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = {ow, oh, 1};
        td.format = fmt; td.mipLevelCount = 1; td.sampleCount = 1;
        WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);
        WGPUTexelCopyTextureInfo dst = {}; dst.texture = t; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay = {};
        lay.bytesPerRow = ((ow + block - 1) / block) * blk;
        lay.rowsPerImage = (oh + block - 1) / block;
        WGPUExtent3D ext = {ow, oh, 1};
        wgpuQueueWriteTexture(g.queue, &dst, d.data.data(), d.data.size(), &lay, &ext);
        WGPUTextureView v = wgpuTextureCreateView(t, nullptr);
        wgpuTextureRelease(t);
        WGPUBindGroupEntry e[2] = {};
        e[0].binding = 0; e[0].textureView = v; e[1].binding = 1; e[1].sampler = samp;
        WGPUBindGroupDescriptor bd = {}; bd.layout = bgl; bd.entryCount = 2; bd.entries = e;
        return wgpuDeviceCreateBindGroup(g.device, &bd);
    }

    // Load the game's STATUS_512 HUD atlas (player status graphics).
    void init_atlas(Gpu& g, const fs::path& dds_path) {
        atlas_bind = upload_atlas(g, dds_path, atlas_w, atlas_h);
    }
    // Bind the four HUD slots to the currently equipped item ids (-1 = empty). Each item's
    // icon is its own MENU/ICON/<STEM>.DDS, cached by id so this is cheap to call per frame.
    void set_equip(Gpu& g, const ItemTable& items, const fs::path& asset_root, const int ids[4]) {
        for (int i = 0; i < 4; ++i) {
            const int id = ids[i];
            if (id < 0 || id == 0xff) { equip_bind[i] = nullptr; continue; }
            auto it = icon_binds.find(id);
            if (it == icon_binds.end()) {
                std::uint32_t w = 0, h = 0;
                WGPUBindGroup bg = upload_atlas(g, items.icon_path(asset_root, id, false), w, h);
                it = icon_binds.emplace(id, bg).first;
            }
            equip_bind[i] = it->second;
        }
    }
    // Load the game's HP2 atlas (boss health bar graphics).
    void init_boss(Gpu& g, const fs::path& dds_path) {
        boss_bind = upload_atlas(g, dds_path, boss_w, boss_h);
    }
    // Load F_MINI.DDS (font_A). MiniFont decodes the A4R4G4B4 sheet to RGBA itself.
    void init_mini(Gpu& g, const fs::path& dds_path) {
        if (!mini.load(dds_path, kMiniWidths, sizeof kMiniWidths) || !mini.ok()) return;
        const std::uint32_t mw = std::uint32_t(mini.width()), mh = std::uint32_t(mini.height());
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = {mw, mh, 1};
        td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
        WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);
        WGPUTexelCopyTextureInfo dst = {}; dst.texture = t; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay = {}; lay.bytesPerRow = mw * 4; lay.rowsPerImage = mh;
        WGPUExtent3D ext = {mw, mh, 1};
        wgpuQueueWriteTexture(g.queue, &dst, mini.rgba().data(), mini.rgba().size(), &lay, &ext);
        WGPUTextureView v = wgpuTextureCreateView(t, nullptr);
        wgpuTextureRelease(t);
        WGPUBindGroupEntry e[2] = {};
        e[0].binding = 0; e[0].textureView = v; e[1].binding = 1; e[1].sampler = samp;
        WGPUBindGroupDescriptor bd = {}; bd.layout = bgl; bd.entryCount = 2; bd.entries = e;
        mini_bind = wgpuDeviceCreateBindGroup(g.device, &bd);
    }
    // font_A text at AUTHORED 1024x768 coordinates (through the HudMap, like sprite_a).
    // The engine draws each glyph as its 12x14 cell and advances by the width table
    // (FUN_00452f40); y is the cell TOP ([H] — the 12px row pitch of the readouts
    // overlaps the 14px cell by 2, exactly as the cell's empty rows allow).
    void mini_text(const HudMap& m, std::string_view s, float x, float y, float sw, float sh,
                   std::uint8_t r, std::uint8_t gg, std::uint8_t b, std::uint8_t a = 255) {
        if (!mini_bind) return;
        float pen = x;
        auto NX = [&](float v) { return v / sw * 2.f - 1.f; };
        auto NY = [&](float v) { return 1.f - v / sh * 2.f; };
        for (char c : s) {
            const MiniGlyph gl = mini.glyph(c);
            if (c != ' ') {
                const float x0 = m.x(pen), y0 = m.y(y);
                const float x1 = m.x(pen + MiniFont::kCellW), y1 = m.y(y + MiniFont::kCellH);
                const float u0 = float(gl.cell_x) / mini.width(), v0 = float(gl.cell_y) / mini.height();
                const float u1 = float(gl.cell_x + MiniFont::kCellW) / mini.width();
                const float v1 = float(gl.cell_y + MiniFont::kCellH) / mini.height();
                HudVertex q0{NX(x0), NY(y0), u0, v0, r, gg, b, a};
                HudVertex q1{NX(x1), NY(y0), u1, v0, r, gg, b, a};
                HudVertex q2{NX(x1), NY(y1), u1, v1, r, gg, b, a};
                HudVertex q3{NX(x0), NY(y1), u0, v1, r, gg, b, a};
                mini_verts.push_back(q0); mini_verts.push_back(q1); mini_verts.push_back(q2);
                mini_verts.push_back(q0); mini_verts.push_back(q2); mini_verts.push_back(q3);
            }
            pen += float(gl.advance);
        }
    }

    // Draw a quad of the STATUS atlas: dst screen rect (px), src atlas rect (px).
    void sprite(float dx, float dy, float dw, float dh,
                float sx, float sy, float sw_px, float sh_px, float sw, float sh,
                std::uint8_t r = 255, std::uint8_t gg = 255, std::uint8_t b = 255, std::uint8_t a = 255) {
        if (!atlas_w) return;
        const float u0 = sx / atlas_w, v0 = sy / atlas_h;
        const float u1 = (sx + sw_px) / atlas_w, v1 = (sy + sh_px) / atlas_h;
        auto NX = [&](float v){ return v / sw * 2.f - 1.f; };
        auto NY = [&](float v){ return 1.f - v / sh * 2.f; };
        const float x0 = NX(dx), x1 = NX(dx + dw), y0 = NY(dy), y1 = NY(dy + dh);
        HudVertex q0{x0, y0, u0, v0, r, gg, b, a}, q1{x1, y0, u1, v0, r, gg, b, a};
        HudVertex q2{x1, y1, u1, v1, r, gg, b, a}, q3{x0, y1, u0, v1, r, gg, b, a};
        sprites.push_back(q0); sprites.push_back(q1); sprites.push_back(q2);
        sprites.push_back(q0); sprites.push_back(q2); sprites.push_back(q3);
    }

    // Push one triangle into the STATUS sprite buffer: screen-px positions + atlas-PX UVs.
    // Used to build the EX/Boost arc as a pie-wedge fan sampling the atlas radially (H4).
    void tri(float ax, float ay, float au, float av,
             float bx, float by, float bu, float bv,
             float cx, float cy, float cu, float cv, float sw, float sh,
             std::uint8_t r, std::uint8_t gg, std::uint8_t b, std::uint8_t a) {
        auto NX = [&](float v){ return v / sw * 2.f - 1.f; };
        auto NY = [&](float v){ return 1.f - v / sh * 2.f; };
        sprites.push_back({NX(ax), NY(ay), au / atlas_w, av / atlas_h, r, gg, b, a});
        sprites.push_back({NX(bx), NY(by), bu / atlas_w, bv / atlas_h, r, gg, b, a});
        sprites.push_back({NX(cx), NY(cy), cu / atlas_w, cv / atlas_h, r, gg, b, a});
    }

    void message(const std::string& s) { messages.push_back({s, 5.0f}); }
    // Screen fader (cutscene op 0xc8..): a fullscreen colored quad at `alpha`, drawn
    // FIRST in render() so it sits over the 3D scene but under the HUD/dialogue text.
    std::uint32_t fade_rgb = 0; float fade_alpha = 0;
    void set_fade(std::uint32_t rgb, float alpha) { fade_rgb = rgb; fade_alpha = alpha; }
    // The global HUD tint 0x74e68c's alpha 0x74e688 (STATUE_CLEANSE.md §2): scales the gauges,
    // the equipment slots and the boss bar (the bar is [H]); not the fader or the debug text.
    float gauge_alpha = 1.0f;
    // CrossFade 0xcd (STATUE_CLEANSE.md §2): the frozen frame, drawn fullscreen at `alpha`
    // under everything else the HUD draws. [H] its order against the HUD.
    WGPUBindGroup overlay_bind = nullptr; float overlay_alpha = 0;
    WGPUBuffer ovbuf = nullptr;
    WGPURenderPipeline overlay_pipeline = nullptr;
    WGPUBindGroupLayout overlay_bgl = nullptr;
    // A bind group for a texture view on the overlay pipeline's layout (the CrossFade frame).
    WGPUBindGroup make_bind(Gpu& g, WGPUTextureView v) const {
        WGPUBindGroupEntry e[2] = {};
        e[0].binding = 0; e[0].textureView = v; e[1].binding = 1; e[1].sampler = samp;
        WGPUBindGroupDescriptor bd = {}; bd.layout = overlay_bgl; bd.entryCount = 2; bd.entries = e;
        return wgpuDeviceCreateBindGroup(g.device, &bd);
    }

    // Append a string at screen pixel (px,py), scale, RGBA.
    void text(const std::string& s, float px, float py, float scale, float sw, float sh,
              std::uint8_t r, std::uint8_t gg, std::uint8_t b, std::uint8_t a) {
        const float gw = cw * scale, gh = ch * scale;
        float x = px;
        for (char c : s) {
            if (c == '\n') { x = px; py += gh; continue; }
            const int idx = int((unsigned char)c) - int(first);
            if (idx < 0 || idx >= 95) { x += gw; continue; }
            const float u0 = float((idx % cols) * cw) / aw, v0 = float((idx / cols) * ch) / ah;
            const float u1 = u0 + float(cw) / aw, v1 = v0 + float(ch) / ah;
            auto NX = [&](float v){ return v / sw * 2.f - 1.f; };
            auto NY = [&](float v){ return 1.f - v / sh * 2.f; };
            const float x0 = NX(x), x1 = NX(x + gw), y0 = NY(py), y1 = NY(py + gh);
            HudVertex q0{x0, y0, u0, v0, r, gg, b, a}, q1{x1, y0, u1, v0, r, gg, b, a};
            HudVertex q2{x1, y1, u1, v1, r, gg, b, a}, q3{x0, y1, u0, v1, r, gg, b, a};
            verts.push_back(q0); verts.push_back(q1); verts.push_back(q2);
            verts.push_back(q0); verts.push_back(q2); verts.push_back(q3);
            x += gw;
        }
    }

    // Solid filled rectangle (samples the atlas's reserved white cell `nchars`).
    void rect(float px, float py, float w, float h, float sw, float sh,
              std::uint8_t r, std::uint8_t gg, std::uint8_t b, std::uint8_t a) {
        const float su = (float((nchars % cols) * cw) + cw * 0.5f) / aw;
        const float sv = (float((nchars / cols) * ch) + ch * 0.5f) / ah;
        auto NX = [&](float v){ return v / sw * 2.f - 1.f; };
        auto NY = [&](float v){ return 1.f - v / sh * 2.f; };
        const float x0 = NX(px), x1 = NX(px + w), y0 = NY(py), y1 = NY(py + h);
        HudVertex q0{x0, y0, su, sv, r, gg, b, a}, q1{x1, y0, su, sv, r, gg, b, a};
        HudVertex q2{x1, y1, su, sv, r, gg, b, a}, q3{x0, y1, su, sv, r, gg, b, a};
        verts.push_back(q0); verts.push_back(q1); verts.push_back(q2);
        verts.push_back(q0); verts.push_back(q2); verts.push_back(q3);
    }

    // Add text horizontally centred on cx_frac*sw. (Uses `text`; width = len*cw*scale.)
    void text_centered(const std::string& s, float cx_frac, float y_px, float scale,
                       float sw, float sh, std::uint8_t r, std::uint8_t gg, std::uint8_t b,
                       std::uint8_t a) {
        text(s, cx_frac * sw - float(s.size()) * cw * scale * 0.5f, y_px, scale, sw, sh, r, gg, b, a);
    }

    // Flush ONLY the accumulated font/solid `verts` (no status bar, no "Room:" line) to
    // `view`, keeping what's already there (loadOp Load). For menus/titles that build their
    // own text via text()/text_centered()/rect() and then call this. Clears verts after.
    void draw_lines(Gpu& g, WGPUTextureView view) {
        const std::size_t nf = verts.size();
        if (!nf) return;
        const std::size_t bytes = nf * sizeof(HudVertex);
        if (bytes > vbuf_cap) {
            if (vbuf) wgpuBufferRelease(vbuf);
            WGPUBufferDescriptor bd = {};
            bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            bd.size = ((bytes + 4095) / 4096) * 4096;
            vbuf = wgpuDeviceCreateBuffer(g.device, &bd);
            vbuf_cap = bd.size;
        }
        wgpuQueueWriteBuffer(g.queue, vbuf, 0, verts.data(), bytes);
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPURenderPassColorAttachment ca = {};
        ca.view = view; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, 0, bytes);
        wgpuRenderPassEncoderDraw(pass, std::uint32_t(nf), 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(g.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
        verts.clear();
    }

    // The game's own digit emitter (builder A, 0x0043e550..0x0043e7b1).
    //
    //   cursor = right_x (337.5 for SP);  v = clamp(value, 0, 999999)
    //   do { digit = v % 10; emit {cursor-12, y, cursor, y+12} from cell (digit*12, 24)
    //        cursor -= 12; v /= 10; } while (v != 0 && ++n < 6)
    //
    // So it walks LEFTWARD from a fixed right edge, ones digit first — i.e. the number
    // is RIGHT-ALIGNED with NO leading zeros, and capped at 6 digits because the value
    // is clamped to 0xF423F (999999) at FUN_004fa290.
    //
    // `right_x`/`y` are authored 1024x768 coords; the caller maps them.
    void draw_number_right(const HudMap& m, int value, float right_x, float y,
                           float sw, float sh) {
        int v = value < 0 ? 0 : (value > 999999 ? 999999 : value);
        float cx = right_x;
        for (int n = 0; n < 6; ++n) {
            const Rc c = digit_cell(v % 10);
            const float x0 = m.x(cx - 12.0f), y0 = m.y(y);
            sprite(x0, y0, m.x(cx) - x0, m.y(y + 12.0f) - y0, c.x, c.y, c.w, c.h, sw, sh);
            cx -= 12.0f;
            v /= 10;
            if (v == 0) break;
        }
    }

    // Draw an authored 1024x768 rect of the STATUS atlas at its real screen place.
    void sprite_a(const HudMap& m, const Ar& d, const Rc& src, float sw, float sh,
                  std::uint8_t r = 255, std::uint8_t gg = 255, std::uint8_t b = 255,
                  std::uint8_t a = 255) {
        const float x0 = m.x(d.x0), y0 = m.y(d.y0);
        sprite(x0, y0, m.x(d.x1) - x0, m.y(d.y1) - y0,
               src.x, src.y, src.w, src.h, sw, sh, r, gg, b, a);
    }

    // The player status HUD, at the game's REAL authored coordinates (recovered from
    // the status-HUD builder FUN_0043c230 — see build/source_map/HUD_COORDS.md).
    void player_hud(const Player& p, float sw, float sh) {
        const HudMap m{std::uint32_t(sw), std::uint32_t(sh)};

        // Advance the HP damage-lag (H3). Heal snaps the lag up to cur instantly (no red on
        // heal); damage lets cur drop and the lag decays down toward it, per tick. coeff ~1
        // is [UNKNOWN] — CLERIA_HPLAG scales the visible catch-up speed (default 0.05 so the
        // trail is watchable; the traced form is 0.2 * coeff * max).
        if (p.hp_lag < float(p.hp)) p.hp_lag = float(p.hp);
        else if (p.hp_lag > float(p.hp)) {
            static const float k = [] {
                if (const char* e = std::getenv("CLERIA_HPLAG")) { float v = std::atof(e); if (v > 0) return v; }
                return 0.006f;  // ~0.5 HP/frame at max 400 -> a big hit's trail drains over ~1s
            }();
            p.hp_lag -= 0.2f * k * float(p.max_hp);
            if (p.hp_lag < float(p.hp)) p.hp_lag = float(p.hp);
        }

        // NOT DRAWN: the four stacked 12px rows at x0=0 (y 620/632/644/656). Their
        // geometry is recovered, but their identity is not — an earlier pass called them
        // "equipment slots", yet they sit flush left with RUNTIME-COMPUTED widths, which
        // reads as four stacked bars, and the real game shows equipment as a horizontal
        // row beside the portrait. Drawing skill diamonds there was visibly wrong, so we
        // draw nothing until we know what they are.

        // The housing, then HP as the blue pointed bar in the UPPER channel. The
        // segmented MP channel below stays empty — that is what the real HUD shows,
        // and drawing HP into it (as we briefly did) misaligns it with the borders.
        sprite_a(m, kABarPlate, kBarHousing, sw, sh);

        // HP — the blue bar, quad 0x0043c788. Its ratio comes from the SAME globals
        // (0x006d5f20 / 0x006d5f28) that builder D prints as `%3d/%3d`, so the bar
        // and the `60/ 60` readout are provably one value.
        const float hp_frac = p.max_hp > 0
                                  ? std::max(0.f, std::min(1.f, float(p.hp) / p.max_hp)) : 0.f;
        // The red damage trail FIRST (quad 0x0043c5b2), at the lagging value, then the blue
        // current HP on top — so the red shows only in the [cur, lag] segment. Now that we
        // have the H3 lag model, this closes the long-standing "no lag model" gap.
        const float lag_frac = p.max_hp > 0
                                   ? std::max(0.f, std::min(1.f, p.hp_lag / p.max_hp)) : 0.f;
        if (lag_frac > hp_frac) {
            Ar red = kAHpFill;
            red.x1 = red.x0 + 252.0f * lag_frac;
            sprite_a(m, red, kHpBlue, sw, sh, 214, 46, 46, 255);  // red ghost behind
        }
        Ar hp = kAHpFill;
        hp.x1 = hp.x0 + 252.0f * hp_frac;
        sprite_a(m, hp, kHpBlue, sw, sh);                          // blue current HP on top

        // EXP for the current level — quad 6, the orange band. This is the ONLY HUD
        // quad that is NOT 1:1: 240px of art into 230px of screen, and 8px of art
        // squashed into 4px (2:1). That squash is why it reads thin next to the blue
        // bar — deliberate, not a mis-measurement.
        //
        // Its ratio is NOT driven by DAT_0076c1b4, and that is EXPECTED rather than
        // suspicious: **[EX] is not experience**. [EX] is the EX/BOOST GAUGE (max
        // 30000, Boost unlocks at 100%) — the same global builder C sweeps into the
        // arc around the portrait and pulses when full. EXPERIENCE-into-level is a
        // separate quantity, and this is its bar.
        Ar ex = kAExpFill;
        ex.x1 = ex.x0 + 230.0f * std::max(0.f, std::min(1.f, p.exp_pct / 100.0f));
        sprite_a(m, ex, kExpOrange, sw, sh);

        // MP / mana bar (H1). The green fill is the REAL STATUS_512 art at {0,44,252,8}
        // — the pointed green bar stacked below the blue HP / red-trail / pink rows in
        // the atlas's bar column (measured off the pixels; the earlier {252,36,...}
        // rainbow was just the wrong cell). Over it go the SEGMENT DIVIDERS the user
        // asked for: the atlas carries beveled tick sprites at x277+25k (stride 25), and
        // a 252px bar / 25 ≈ 10 cells, so 9 interior ticks split it into ~10 blocks.
        {
            const float mp = std::max(0.f, std::min(1.f, p.mp_pct / 100.0f));
            Ar mpf = kAMpFill;
            mpf.x1 = mpf.x0 + 252.0f * mp;
            sprite_a(m, mpf, kMpGreen, sw, sh);                 // green fill (real art)
            for (int k = 1; k < 10; ++k) {                      // 9 dividers -> ~10 cells
                const float cx = kAMpFill.x0 + 25.2f * float(k);
                Ar tick = {cx - 1.0f, kAMpFill.y0, cx + 1.0f, kAMpFill.y1};
                sprite_a(m, tick, kBarTick, sw, sh);
            }
        }

        // Character portrait (drawn over the plate's left end, as the game does).
        const int ci = (p.character >= 0 && p.character < 4) ? p.character : 1;
        sprite_a(m, kAPortrait, kPortraitReal[ci], sw, sh);

        // EX / Boost C-arc (H4, build/hud_re/HUD_BOOST_ARC.md): a pie-wedge fan that reveals the
        // arc graphic from STATUS_512 as the gauge fills. Center (44,719) authored, R63, sweeping
        // -ratio*190deg from 315deg; atlas sample center (44,410). Exact geometry [C-DIS]; if the
        // atlas region reads blank in CleriaCore's STATUS_512 the boost just won't show (no harm).
        {
            const float ratio = std::max(0.f, std::min(1.f, p.boost / 30000.0f));
            if (ratio > 0.001f) {
                const float DEG = 3.14159265f / 180.0f;
                // H4 traced start=315deg, sweep=190deg — both exact. The arc used to stop at ~94%
                // because the rim used x=sin/y=cos; the EX glyph in STATUS_512 (annulus centred
                // exactly on the traced UV centre 44,410, radii 26..39) spans 123.7..312.9deg,
                // which matches x=cos/y=sin to sub-pixel at both hard ends. Ghidra can't recover
                // which of FUN_0066fd70/FUN_00670d30 feeds x vs y (XMM return), so the art is the
                // ground truth here. Fills lower-right -> bottom -> upper-left, as the art reads.
                const float th0 = 315.0f * DEG;
                const float sweep = ratio * 190.0f * DEG;
                const int N = 24;
                const float apx = m.x(44.0f), apy = m.y(719.0f);   // apex (center), screen px
                auto rim = [&](float th, float& x, float& y, float& u, float& v) {
                    const float s = std::sin(th), c = std::cos(th);
                    x = m.x(44.0f + 63.0f * c); y = m.y(719.0f - 63.0f * s);  // screen px
                    u = 44.0f + 63.0f * c;      v = 410.0f - 63.0f * s;       // atlas px
                };
                const std::uint8_t t = ratio >= 0.999f ? 255 : 210;   // brighten when Boost-ready
                float px, py, pu, pv; rim(th0, px, py, pu, pv);
                for (int i = 1; i <= N; ++i) {
                    float nx, ny, nu, nv; rim(th0 - sweep * (float(i) / float(N)), nx, ny, nu, nv);
                    tri(apx, apy, 44.0f, 410.0f, px, py, pu, pv, nx, ny, nu, nv, sw, sh, t, t, t, 255);
                    px = nx; py = ny; pu = nu; pv = nv;
                }
            }
        }

        // The four 40x40 equipment slot frames — quad 12, recovered as a LOOP:
        //   x = 126 + 40*i  (126/166/206/246), y = 676..716
        //   UV (0.921875,0.921875)..(1,1) = atlas px (472,472)..(512,512)
        // Builder D blits each equipped item's icon on top at (132+40i, 682) — a 6px
        // inset — at 32x32, from a per-item texture handle array (DAT_0074a958[itemId]),
        // NOT from the atlas. Unequipped slots are drawn greyed (tint & 0xff303030). We
        // draw the empty frames, then blit each equipped icon on top.
        for (int i = 0; i < 4; ++i) {
            const Ar d{126.0f + 40.0f * i, 676.0f, 166.0f + 40.0f * i, 716.0f};
            sprite_a(m, d, kSlotFrame, sw, sh);
            equip_verts[i].clear();
            if (!equip_bind[i]) continue;
            // Icon is 32x32 at +6,+6 from the frame, so it sits INSIDE the 40x40 housing
            // (132..164 within 126..166). Read from builder D, not assumed: FUN_0043fe00
            // writes the quad's y as 0x442a8000 = 682.0 and its four size fields as
            // 0x42000000 = 32.0 (0043fe87..0043fecc) — the same shape as the equip page's
            // four 54.4 writes. HUD_PORTRAIT_SLOTS.md §2's "40x40 / {132,682,172,722}" is
            // WRONG; a 40x40 icon overhangs the frame bottom-right by 6px, which the retail
            // HUD does not do. [C-DIS]
            const float x0 = m.x(132.0f + 40.0f * i), y0 = m.y(682.0f);
            const float x1 = m.x(164.0f + 40.0f * i), y1 = m.y(714.0f);
            auto V = [&](float x, float y, float u, float v) {
                equip_verts[i].push_back({x / float(sw) * 2.0f - 1.0f,
                                          1.0f - y / float(sh) * 2.0f, u, v, 255, 255, 255, 255});
            };
            V(x0, y0, 0, 0); V(x1, y0, 1, 0); V(x1, y1, 1, 1);
            V(x0, y0, 0, 0); V(x1, y1, 1, 1); V(x0, y1, 0, 1);
        }

        // The level, in the atlas's own gold glyph font (y15..36, proportional widths,
        // measured from STATUS_512.DDS — see build/hud_re/hud_atlas_spec.json).
        //
        // The game's HUD is quads from STATUS_512.DDS and NOTHING ELSE — it contains no
        // text at all, which the RE confirms twice over: builder A emits 12 quads and
        // none is a label, and there are no HUD strings in the exe to pivot from. So the
        // "Lv" / "SP nnn" / "EXP n.n%" captions we used to draw here in our own bitmap
        // font were pure invention, and they are gone.
        //
        // The POSITION below is still ours: the digit emitter is builder B (FUN_0043e800)
        // or D (FUN_0043fe00), neither of which contains a single MapCoord call, and
        // neither is decoded. The glyphs are real; where they belong is not.
        // SP, at the game's real place: right edge 349.5, y 752..764, right-aligned.
        // (The emitter's cursor starts at 337.5 = the LEFT edge of the ones digit.)
        draw_number_right(m, p.sp, 349.5f, 752.0f, sw, sh);

        // Builder-D font readouts (HUD_COUNTERS.md §2-3, coords/format/colors [C-DIS],
        // localized -> SHIPPED, not debug):
        //   [EX]%3.3f%%  font_A @(288,670) = boost/30000*100
        //   [MP]%3.3f%%  font_A @(288,682) = MP percent
        //   HP %3d/%3d   font_B @(288,694), pale mint, always bright. font_B is the
        //                runtime GDI glyph font we don't build yet; F_MINI stands in
        //                for it here [HAND-TUNED substitution].
        //   [SP]         font_A @(248,752), grey, next to the SP counter above.
        // [MP] brightens while its lag value has caught up (0x440190); [EX]'s boost-driven
        // colour is not wired yet.
        char rbuf[32];
        std::snprintf(rbuf, sizeof rbuf, "[EX]%3.3f%%", double(p.boost) / 30000.0 * 100.0);
        mini_text(m, rbuf, 288, 670, sw, sh, 0x50, 0x72, 0x64);
        std::snprintf(rbuf, sizeof rbuf, "[MP]%3.3f%%", double(p.mp_pct));
        // 0x440190: mint 0xa1e4c9 while the lag DAT_0076c194 <= MP, dim 0x507264 while it drains.
        if (p.sp_lag <= p.sp_cur) mini_text(m, rbuf, 288, 682, sw, sh, 0xa1, 0xe4, 0xc9);
        else mini_text(m, rbuf, 288, 682, sw, sh, 0x50, 0x72, 0x64);
        std::snprintf(rbuf, sizeof rbuf, "%3d/%3d", p.hp, p.max_hp);
        mini_text(m, rbuf, 288, 694, sw, sh, 0xa1, 0xe4, 0xc9);
        mini_text(m, "[SP]", 248, 752, sw, sh, 0xbb, 0xbb, 0xbb);
    }

    // Draw a quad of the HP2 (boss) atlas: dst screen rect (px), src atlas rect (px).
    void boss_sprite(float dx, float dy, float dw, float dh,
                     float sx, float sy, float sw_px, float sh_px, float sw, float sh,
                     std::uint8_t r = 255, std::uint8_t gg = 255, std::uint8_t b = 255, std::uint8_t a = 255) {
        if (!boss_w) return;
        const float u0 = sx / boss_w, v0 = sy / boss_h;
        const float u1 = (sx + sw_px) / boss_w, v1 = (sy + sh_px) / boss_h;
        auto NX = [&](float v){ return v / sw * 2.f - 1.f; };
        auto NY = [&](float v){ return 1.f - v / sh * 2.f; };
        const float x0 = NX(dx), x1 = NX(dx + dw), y0 = NY(dy), y1 = NY(dy + dh);
        HudVertex q0{x0, y0, u0, v0, r, gg, b, a}, q1{x1, y0, u1, v0, r, gg, b, a};
        HudVertex q2{x1, y1, u1, v1, r, gg, b, a}, q3{x0, y1, u0, v1, r, gg, b, a};
        boss_sprites.push_back(q0); boss_sprites.push_back(q1); boss_sprites.push_back(q2);
        boss_sprites.push_back(q0); boss_sprites.push_back(q2); boss_sprites.push_back(q3);
    }

    // Boss health bar (top-centre), from the game's HP2 atlas: ornate frame + red fill.
    // The boss gauge FUN_0043a770 (build/hud_re/BOSS_GAUGE_DRAW.md §5): HP2.DDS, top-left, left
    // anchored through the HUD mapping. Fixed quads S1 ornament, S2 channel, S3 end cap, S4 emblem;
    // then the fill on the unit axis 0..len+48 at x 75+unit (y 24..40) with the red lag trail, or
    // only the lag while it refills. No text. [not modelled] the draw order after F_MINI (G11).
    void boss_bar(float sw, float sh) {
        if (!boss_w) return;
        const HudMap m{std::uint32_t(sw), std::uint32_t(sh)};
        const float max = boss_max > 0.0f ? boss_max : 1.0f;
        const float cur = std::clamp(boss_cur, 0.0f, max), lag = std::clamp(boss_lag, 0.0f, max);
        const float len = std::clamp(boss_len / 11.427428f, 1.0f, 875.0f);
        const float M = len + 24.0f, TOT = len + 48.0f, K = 1.0f / 256.0f;
        const std::uint8_t ta = std::uint8_t(std::clamp(gauge_alpha, 0.0f, 1.0f) * 255.0f);
        auto quad = [&](float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                        bool red) {
            const float X0 = m.x(x0) / sw * 2.f - 1.f, X1 = m.x(x1) / sw * 2.f - 1.f;
            const float Y0 = 1.f - m.y(y0) / sh * 2.f, Y1 = 1.f - m.y(y1) / sh * 2.f;
            const std::uint8_t r = 255, g = red ? 0 : 255, b = red ? 0 : 255, a = red ? 255 : ta;
            const HudVertex q0{X0, Y0, u0, v0, r, g, b, a}, q1{X1, Y0, u1, v0, r, g, b, a};
            const HudVertex q2{X1, Y1, u1, v1, r, g, b, a}, q3{X0, Y1, u0, v1, r, g, b, a};
            boss_sprites.push_back(q0); boss_sprites.push_back(q1); boss_sprites.push_back(q2);
            boss_sprites.push_back(q0); boss_sprites.push_back(q2); boss_sprites.push_back(q3);
        };
        quad(15, 8, 100, 56, 0, 0, 0.328125f, 0.1875f, false);                       // S1
        quad(99, 16, 99 + len, 48, 0, 0.375f, 0.62f, 0.5f, false);                    // S2
        quad(99 + len, 8, 136 + len, 56, 0, 0.1875f, 0.1484375f, 0.375f, false);      // S3
        if (boss_emblem <= 0) quad(33, 16, 65, 48, 0, 0.75f, 0.125f, 0.875f, false);  // S4
        else { const float u = float(boss_emblem - 1) * 32.0f * K;
               quad(33, 16, 65, 48, u, 0.875f, u + 0.125f, 1.0f, false); }
        auto fill_to = [&](float e) {                                                // A4-A6 / B1-B3
            const float c = std::min(24.0f, e);
            if (c > 0) quad(75, 24, 75 + c, 40, 0, 0.5f, c * K, 0.5625f, false);
            if (e > 24) quad(99, 24, 99 + std::min(len, e - 24), 40, 0, 0.625f, 0.62f, 0.6875f, false);
            if (e > M) { const float d = std::min(24.0f, e - M);
                         quad(99 + len, 24, 99 + len + d, 40, 0, 0.5625f, d * K, 0.625f, false); }
        };
        if (lag < cur) { fill_to(TOT * lag / max); return; }                         // branch B: refill
        const float f = TOT * cur / max, gg = TOT * lag / max;
        if (f < 24) { const float w = std::min(24 - f, gg - f);                      // A1
                      if (w > 0) quad(75 + f, 24, 75 + f + w, 40, f * K, 0.5f, (f + w) * K, 0.5625f, true); }
        if ((24 < f && f < M) || (24 < gg && gg < M) || (f < 24 && gg > M)) {        // A2
            const float a = std::max(24.0f, f);
            const float w = std::min(M - a, gg - a);
            if (w > 0) quad(75 + a, 24, 75 + a + w, 40, 0, 0.625f, 0.62f, 0.6875f, true);
        }
        if (gg > M) { const float a = std::max(M, f), u0 = (24 + std::max(0.0f, f - M)) * K;   // A3
                      const float w = std::min(TOT - a, gg - a);
                      if (w > 0) quad(75 + a, 24, 75 + a + w, 40, u0, 0.5f, u0 + (gg - a) * K, 0.5625f, true); }
        fill_to(f);
    }

    // Build this frame's HUD, then draw it over `colorView`.
    void render(Gpu& g, WGPUTextureView colorView, std::uint32_t sw, std::uint32_t sh,
                const std::string& room_name, float dt, const Player* player = nullptr) {
        verts.clear();
        sprites.clear();
        boss_sprites.clear();
        mini_verts.clear();
        // Fader FIRST (under everything else the HUD draws): a fullscreen solid quad.
        if (fade_alpha > 0.001f)
            rect(0, 0, float(sw), float(sh), float(sw), float(sh),
                 std::uint8_t((fade_rgb >> 16) & 0xff), std::uint8_t((fade_rgb >> 8) & 0xff),
                 std::uint8_t(fade_rgb & 0xff), std::uint8_t(std::min(1.0f, fade_alpha) * 255.0f));
        const float s = std::max(1.f, sh / 540.f);  // scale text to window height
        text("Room: " + room_name, 12 * s, 10 * s, s, float(sw), float(sh), 230, 220, 150, 255);
        float my = float(sh) * 0.5f;
        for (auto it = messages.begin(); it != messages.end();) {
            it->ttl -= dt;
            if (it->ttl <= 0) { it = messages.erase(it); continue; }
            const std::uint8_t a = std::uint8_t(255 * std::min(1.0f, it->ttl));
            text(it->text, 12 * s, my, s * 1.15f, float(sw), float(sh), 255, 235, 120, a);
            my += 30 * s; ++it;
        }
        const std::size_t g0 = verts.size(), g1 = sprites.size(), g3 = mini_verts.size();
        if (player) player_hud(*player, float(sw), float(sh));
        // The oxygen bar (0x43c914..0x43c98b, WATER.md §3): above the head while its alpha is
        // non-zero, STATUS cell {288,24,84,12} cut to O2 / 6000 at {pX - 40, pY - 100}.
        // [H] pX/pY = the projected origin, taken back into the 1024x768 layout.
        if (player && player->o2_alpha > 0) {
            const HudMap m{sw, sh};
            const float ax = player->o2_px / m.sx, ay = player->o2_py / m.sy;
            const float r = std::max(0.f, std::min(1.f, player->o2 / 6000.0f));
            if (r > 0.0f)
                sprite_a(m, Ar{ax - 40.0f, ay - 100.0f, ax - 40.0f + 84.0f * r, ay - 88.0f}, Rc{288, 24, 84.0f * r, 12},
                         float(sw), float(sh), 255, 255, 255, std::uint8_t(std::min(255, player->o2_alpha)));
        }
        if (boss_frac >= 0.f) {
            if (boss_max <= 0.0f) { boss_max = 1.0f; boss_cur = boss_lag = boss_frac; }   // a bare preview
            boss_bar(float(sw), float(sh));
        }
        if (gauge_alpha < 1.0f) {
            auto dim = [&](std::vector<HudVertex>& v, std::size_t from) {
                for (std::size_t k = from; k < v.size(); ++k) v[k].a = std::uint8_t(float(v[k].a) * gauge_alpha);
            };
            dim(verts, g0); dim(sprites, g1); dim(mini_verts, g3);   // the boss gauge dims itself (red trail unfaded)
            for (auto& v : equip_verts) dim(v, 0);
        }
        const std::size_t nf = verts.size(), ns = sprites.size(), nb = boss_sprites.size();
        const std::size_t nm = mini_verts.size();
        std::size_t ne = 0;                       // equipped-item icon quads (one batch/slot)
        for (const auto& v : equip_verts) ne += v.size();
        if (nf + ns + nb + nm + ne == 0) return;

        // Concatenate the batches: font atlas verts, STATUS sprites, HP2, then F_MINI.
        const std::size_t bytes = (nf + ns + nb + nm + ne) * sizeof(HudVertex);
        if (bytes > vbuf_cap) {
            if (vbuf) wgpuBufferRelease(vbuf);
            WGPUBufferDescriptor bd = {};
            bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            bd.size = ((bytes + 4095) / 4096) * 4096;
            vbuf = wgpuDeviceCreateBuffer(g.device, &bd);
            vbuf_cap = bd.size;
        }
        if (nf) wgpuQueueWriteBuffer(g.queue, vbuf, 0, verts.data(), nf * sizeof(HudVertex));
        if (ns) wgpuQueueWriteBuffer(g.queue, vbuf, nf * sizeof(HudVertex), sprites.data(), ns * sizeof(HudVertex));
        if (nb) wgpuQueueWriteBuffer(g.queue, vbuf, (nf + ns) * sizeof(HudVertex), boss_sprites.data(), nb * sizeof(HudVertex));
        if (nm) wgpuQueueWriteBuffer(g.queue, vbuf, (nf + ns + nb) * sizeof(HudVertex), mini_verts.data(), nm * sizeof(HudVertex));
        {   // each slot's icon is its own texture, so each is its own little batch
            std::size_t off = (nf + ns + nb + nm) * sizeof(HudVertex);
            for (const auto& v : equip_verts) {
                if (!v.empty()) wgpuQueueWriteBuffer(g.queue, vbuf, off, v.data(), v.size() * sizeof(HudVertex));
                off += v.size() * sizeof(HudVertex);
            }
        }

        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPURenderPassColorAttachment ca = {};
        ca.view = colorView; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;  // keep the 3D scene
        WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        if (overlay_bind && overlay_alpha > 0.0f) {   // the CrossFade frame, first
            const std::uint8_t a = std::uint8_t(std::min(1.0f, overlay_alpha) * 255.0f);
            const HudVertex q[6] = {{-1, 1, 0, 0, 255, 255, 255, a}, {1, 1, 1, 0, 255, 255, 255, a},
                                    {1, -1, 1, 1, 255, 255, 255, a}, {-1, 1, 0, 0, 255, 255, 255, a},
                                    {1, -1, 1, 1, 255, 255, 255, a}, {-1, -1, 0, 1, 255, 255, 255, a}};
            if (!ovbuf) {
                WGPUBufferDescriptor bd = {};
                bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = sizeof q;
                ovbuf = wgpuDeviceCreateBuffer(g.device, &bd);
            }
            wgpuQueueWriteBuffer(g.queue, ovbuf, 0, q, sizeof q);
            wgpuRenderPassEncoderSetPipeline(pass, overlay_pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, overlay_bind, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, ovbuf, 0, sizeof q);
            wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        }
        if (nf) {  // font/solid batch
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bind, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, 0, nf * sizeof(HudVertex));
            wgpuRenderPassEncoderDraw(pass, std::uint32_t(nf), 1, 0, 0);
        }
        if (ns && atlas_bind) {  // STATUS atlas batch
            wgpuRenderPassEncoderSetBindGroup(pass, 0, atlas_bind, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, nf * sizeof(HudVertex), ns * sizeof(HudVertex));
            wgpuRenderPassEncoderDraw(pass, std::uint32_t(ns), 1, 0, 0);
        }
        if (nb && boss_bind) {  // HP2 (boss bar) batch
            wgpuRenderPassEncoderSetBindGroup(pass, 0, boss_bind, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, (nf + ns) * sizeof(HudVertex), nb * sizeof(HudVertex));
            wgpuRenderPassEncoderDraw(pass, std::uint32_t(nb), 1, 0, 0);
        }
        if (nm && mini_bind) {  // F_MINI (font_A readouts) batch
            wgpuRenderPassEncoderSetBindGroup(pass, 0, mini_bind, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, (nf + ns + nb) * sizeof(HudVertex), nm * sizeof(HudVertex));
            wgpuRenderPassEncoderDraw(pass, std::uint32_t(nm), 1, 0, 0);
        }
        {   // equipped-item icons: one draw per occupied slot (its own DDS bind group)
            std::size_t off = (nf + ns + nb + nm) * sizeof(HudVertex);
            for (int i = 0; i < 4; ++i) {
                const std::size_t n = equip_verts[i].size();
                if (n && equip_bind[i]) {
                    wgpuRenderPassEncoderSetBindGroup(pass, 0, equip_bind[i], 0, nullptr);
                    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, off, n * sizeof(HudVertex));
                    wgpuRenderPassEncoderDraw(pass, std::uint32_t(n), 1, 0, 0);
                }
                off += n * sizeof(HudVertex);
            }
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(g.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    }
};
