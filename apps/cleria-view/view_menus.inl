// cleria-view — MenuFont, TitleFx (all menu screens), render_title, options ini
// Textual part of main.cpp (split 2026-07-23, PLAN.md Phase 0): #included by
// main.cpp in a fixed order; ONE translation unit, not standalone. Keep it that way
// until a real TU split is warranted.

// ---- menu font --------------------------------------------------------------
// A proportional serif for menu/title text, rasterised from a Windows system font via GDI --
// the engine renders its menu text from a system font too (MENUS.md §3.1), so this is the
// faithful path (and needs no bundled font/library). One antialiased coverage atlas of ASCII
// 32..126 at a base pixel height; callers ask for a target height and it scales. White glyphs
// (alpha = coverage) tinted by the vertex colour, through the same shader as the HUD font.
struct MenuFont {
    struct Glyph { float u0=0, v0=0, u1=0, v1=0; int w=0, h=0, adv=0, bx=0, by=0; };
    Glyph gl[95];
    int base = 44, ascent = 0, lineH = 0, weight_ = FW_NORMAL;
    std::string face_name = "Constantia";
    WGPURenderPipeline pipeline{};
    WGPUBindGroup bind{};
    WGPUSampler samp{};
    WGPUBuffer vbuf{};
    std::size_t vbuf_cap = 0;
    std::vector<HudVertex> verts;
    bool ok = false;

    bool init(Gpu& g, WGPUTextureFormat color, const fs::path& fontFile = {}, int emPx = 46,
              int weight = FW_NORMAL) {
        // The real menu font is Felghana, the game's bundled TrueType Release/yso_ins04.dat
        // (build/menu_re/MENU_LAYOUT.md). Load it as a private, process-local font; if it isn't
        // available fall back to a system serif. The engine's LOGFONT: weight 400, DEFAULT_CHARSET.
        face_name = "Constantia";
        if (!fontFile.empty()) {
            std::vector<std::uint8_t> bytes = read_bytes(fontFile);
            DWORD n = 0;
            if (!bytes.empty() &&
                AddFontMemResourceEx(bytes.data(), DWORD(bytes.size()), nullptr, &n) && n)
                face_name = "Felghana";
        }
        // CLERIA_FONT / CLERIA_FONT_W override the face / weight (off-window font comparison).
        if (const char* f = std::getenv("CLERIA_FONT")) face_name = f;
        if (const char* w = std::getenv("CLERIA_FONT_W")) weight = std::atoi(w);
        weight_ = weight;

        // Pipeline + sampler, built once (the HUD shader: atlas * vertex colour).
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
        WGPUSamplerDescriptor sad = {};
        sad.magFilter = sad.minFilter = WGPUFilterMode_Linear; sad.maxAnisotropy = 1;
        samp = wgpuDeviceCreateSampler(g.device, &sad);
        wgpuShaderModuleRelease(sm);
        ok = build_atlas(g, emPx);
        return ok;
    }

    // (Re)rasterise the glyph atlas at exactly emPx device pixels -- matching the game's native GDI
    // rasterisation (its LOGFONT height = renderH*32/768) so the hinting/AA is crisp, rather than a
    // downscale of a fixed-size atlas. Call again on window resize; releases the previous atlas.
    bool build_atlas(Gpu& g, int emPx) {
        base = std::max(8, emPx);
        HDC hdc = CreateCompatibleDC(nullptr);
        if (!hdc) return false;
        // Match the engine's LOGFONT (build/menu_re/FONT_RASTER.md): weight 400, OUT_TT_PRECIS,
        // PROOF_QUALITY. It rasterises at the exact device-pixel size, so `base` is that size.
        HFONT font = CreateFontA(-base, 0, 0, 0, weight_, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
                                 FF_ROMAN, face_name.c_str());
        HGDIOBJ oldf = SelectObject(hdc, font);
        TEXTMETRICA tm; GetTextMetricsA(hdc, &tm);
        ascent = tm.tmAscent; lineH = tm.tmHeight;
        MAT2 mat = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
        struct Raw { std::vector<std::uint8_t> cov; int w = 0, h = 0; };
        std::vector<Raw> raws(95);
        const int atlasW = 1024;
        int x = 1, y = 1, rowH = 0;
        for (int i = 0; i < 95; ++i) {
            const UINT ch = 32 + i;
            GLYPHMETRICS gm = {};
            // The engine rasterises with GGO_GRAY4_BITMAP (17 levels 0..16), not GRAY8 (FONT_RASTER.md).
            const DWORD sz = GetGlyphOutlineA(hdc, ch, GGO_GRAY4_BITMAP, &gm, 0, nullptr, &mat);
            gl[i].adv = gm.gmCellIncX; gl[i].bx = gm.gmptGlyphOrigin.x; gl[i].by = gm.gmptGlyphOrigin.y;
            Raw r;
            if (sz != GDI_ERROR && sz > 0) {
                std::vector<std::uint8_t> buf(sz);
                GetGlyphOutlineA(hdc, ch, GGO_GRAY4_BITMAP, &gm, sz, buf.data(), &mat);
                r.w = gm.gmBlackBoxX; r.h = gm.gmBlackBoxY;
                const int pitch = (r.w + 3) & ~3;   // GDI pads glyph rows to a DWORD
                r.cov.assign(std::size_t(r.w) * r.h, 0);
                for (int yy = 0; yy < r.h; ++yy)
                    for (int xx = 0; xx < r.w; ++xx) {
                        // Engine mapping: drop coverage 0/1, else 4-bit alpha (c-1)/15 -> 8-bit.
                        const int v = buf[yy * pitch + xx];        // 0..16 coverage
                        r.cov[yy * r.w + xx] = std::uint8_t(v <= 1 ? 0 : (v - 1) * 255 / 15);
                    }
            }
            gl[i].w = r.w; gl[i].h = r.h;
            if (x + r.w + 1 > atlasW) { x = 1; y += rowH + 1; rowH = 0; }
            gl[i].u0 = float(x); gl[i].v0 = float(y);   // pixel pos, resolved to UV below
            x += r.w + 1; rowH = std::max(rowH, r.h);
            raws[i] = std::move(r);
        }
        const int atlasH = y + rowH + 1;
        std::vector<std::uint8_t> atlas(std::size_t(atlasW) * atlasH * 4, 0);
        for (int i = 0; i < 95; ++i) {
            const int px = int(gl[i].u0), py = int(gl[i].v0);
            const Raw& r = raws[i];
            for (int yy = 0; yy < r.h; ++yy)
                for (int xx = 0; xx < r.w; ++xx) {
                    const std::size_t o = (std::size_t(py + yy) * atlasW + (px + xx)) * 4;
                    atlas[o] = 255; atlas[o + 1] = 255; atlas[o + 2] = 255;
                    atlas[o + 3] = r.cov[yy * r.w + xx];
                }
            gl[i].u0 = px / float(atlasW);          gl[i].v0 = py / float(atlasH);
            gl[i].u1 = (px + r.w) / float(atlasW);  gl[i].v1 = (py + r.h) / float(atlasH);
        }
        SelectObject(hdc, oldf); DeleteObject(font); DeleteDC(hdc);

        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = {std::uint32_t(atlasW), std::uint32_t(atlasH), 1};
        td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
        WGPUTexture tex = wgpuDeviceCreateTexture(g.device, &td);
        WGPUTexelCopyTextureInfo dst = {}; dst.texture = tex; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay = {}; lay.bytesPerRow = atlasW * 4; lay.rowsPerImage = atlasH;
        WGPUExtent3D ext = {std::uint32_t(atlasW), std::uint32_t(atlasH), 1};
        wgpuQueueWriteTexture(g.queue, &dst, atlas.data(), atlas.size(), &lay, &ext);
        WGPUTextureView view = wgpuTextureCreateView(tex, nullptr);
        WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry e[2] = {};
        e[0].binding = 0; e[0].textureView = view;
        e[1].binding = 1; e[1].sampler = samp;
        WGPUBindGroupDescriptor bd = {}; bd.layout = bgl; bd.entryCount = 2; bd.entries = e;
        WGPUBindGroup nb = wgpuDeviceCreateBindGroup(g.device, &bd);
        if (bind) wgpuBindGroupRelease(bind);   // drop the previous atlas (bind holds its view+tex)
        bind = nb;
        wgpuTextureViewRelease(view); wgpuTextureRelease(tex);
        ok = true;
        return true;
    }

    // Text width in px at a given em-height (pxH).
    float measure(const std::string& s, float pxH) const {
        const float sc = pxH / float(base);
        float w = 0;
        for (unsigned char c : s) if (c >= 32 && c <= 126) w += gl[c - 32].adv * sc;
        return w;
    }
    // Append `s` with its top at py_top (px), left edge at px_left. pxH = em height in px.
    void text(const std::string& s, float px_left, float py_top, float pxH, float sw, float sh,
              std::uint8_t r, std::uint8_t gg, std::uint8_t b, std::uint8_t a) {
        const float sc = pxH / float(base);
        const float baseline = py_top + ascent * sc;
        auto NX = [&](float v) { return v / sw * 2.f - 1.f; };
        auto NY = [&](float v) { return 1.f - v / sh * 2.f; };
        float x = px_left;
        for (unsigned char c : s) {
            if (c < 32 || c > 126) { x += base * 0.3f * sc; continue; }
            const Glyph& gp = gl[c - 32];
            if (gp.w > 0 && gp.h > 0) {
                const float gx = x + gp.bx * sc, gy = baseline - gp.by * sc;
                const float gw = gp.w * sc, gh = gp.h * sc;
                const float x0 = NX(gx), x1 = NX(gx + gw), y0 = NY(gy), y1 = NY(gy + gh);
                HudVertex q0{x0, y0, gp.u0, gp.v0, r, gg, b, a}, q1{x1, y0, gp.u1, gp.v0, r, gg, b, a};
                HudVertex q2{x1, y1, gp.u1, gp.v1, r, gg, b, a}, q3{x0, y1, gp.u0, gp.v1, r, gg, b, a};
                verts.push_back(q0); verts.push_back(q1); verts.push_back(q2);
                verts.push_back(q0); verts.push_back(q2); verts.push_back(q3);
            }
            x += gp.adv * sc;
        }
    }
    void text_centered(const std::string& s, float cx_frac, float py_top, float pxH,
                       float sw, float sh, std::uint8_t r, std::uint8_t gg, std::uint8_t b,
                       std::uint8_t a) {
        text(s, cx_frac * sw - measure(s, pxH) * 0.5f, py_top, pxH, sw, sh, r, gg, b, a);
    }
    // Flush accumulated glyphs onto `view` (loadOp Load). Clears after.
    void draw(Gpu& g, WGPUTextureView view) {
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
};

// ---- title screen -----------------------------------------------------------
// Load the title assets (build/menu_re/MENUS.md): TITLE_000.DDS is the 1024^2 logo,
// MENU/BACK00.DDS the background. Rendered as screen-space quads via draw_screen_image.
struct TitleFx {
    SpriteTex splash, pillar, cursor, altpillar;   // altpillar = golden goddess side-art (char-select/menus)
    SpriteTex curGlow;         // baked selection glow: white bar, vertical alpha gradient, drawn ADDITIVE over text
    SpriteTex frame, chrsel;   // BACK00 ornate frame; CHRSELECT atlas (bars/faces/banner)
    SpriteTex dialog;          // DIALOG0 blue scroll frame for the New-Game choice popups
    SpriteTex facewin;         // FACEWIN0 — the in-engine cutscene DIALOGUE box (bottom bar)
    SpriteTex opbtn, plaque;   // OPBTN atlas (Option-screen buttons/checkboxes) + OPTION "Option" plaque
    SpriteTex equipPlaque, wstatus, wequip, wdetail;   // pause-book Equip: plaque + status + grid + detail panel
    SpriteTex invPlaque, winvent;                      // pause-book Item page: "Inventory" plaque + 6x5 grid
    SpriteTex topicPlaque, topicBtn[3], topicCur;      // pause-book Topic page: plaque + Character/Monster/Manual + cursor glow
    SpriteTex shopBg;                                  // SHOPPILLAR16-9: widescreen margin layer
    SpriteTex shopBack, shopFeena;                     // Bonus Shop: Back01.dds backdrop + t_400a.dds Feena key-art
    SpriteTex loadPlaque, savePlaque, wsvld;           // pause-book Save/Load: plaques + W_SVLD slot/tab plate
    SpriteTex square;          // SQUARE.dds (tex 10): the item/equip grid cell tile + cursor cell
    SpriteTex blueHi;          // baked equipped/selected highlight quad (ARGB 0x4259999a)
    std::unordered_map<int, SpriteTex> iconCache;   // item-id -> MENU/ICON/<stem>.DDS (lazy, cached)
    SpriteTex portrait[3];     // CHRSELECT_PORTRAIT1/2/3 = Yunica / Hugo / Toal key art
    SpriteTex dim;             // baked solid: background dimmer behind popups
    CharSprite chibi[3];       // (legacy) AIA walking sprite -- superseded by chibiPose below
    SpriteTex chibiPose[3];    // ChrSelect_Chr{1,2,3}.dds: the dedicated fixed-pose chibi art (code-traced)
    int chibi_cur[3] = {-1, -1, -1};   // frame currently decoded per character
    SpriteTex faceClip[3];     // each char's face pre-clipped to the bar's arrow shape
    bool init(Gpu& g, const fs::path& asset_root) {
        // Background dimmer for the popups (RGBA, alpha baked in -- the char shader blends by
        // texture alpha). The popup PANEL is the real DIALOG0 asset, below.
        auto solid = [&](std::uint8_t r, std::uint8_t gg, std::uint8_t b, std::uint8_t a) {
            std::uint8_t px[4 * 4 * 4];
            for (int i = 0; i < 16; ++i) { px[i*4]=r; px[i*4+1]=gg; px[i*4+2]=b; px[i*4+3]=a; }
            return upload_rgba(g, 4, 4, px);
        };
        dim = solid(0, 2, 6, 120);
        // Selection glow (TITLE_LAYOUT.md §1 / UI_BLEND_FONT.md): the engine's menu cursor is the
        // chrome texture DAT_00749d68 (DDS identity [UNKNOWN]) drawn ADDITIVE (renderstate 0x40c1)
        // OVER the text, white/grey with a VERTICAL alpha gradient 0.125 (top) -> 0.875 -> 1.0
        // (bottom). Bake that as a small white bar carrying the gradient in its alpha, plus a soft
        // horizontal fade at the far ends so it reads as a glow band, not a hard rectangle.
        {
            // Overall additive intensity: the engine's tint is white/GREY, so the band brightens
            // the row without obliterating the (already near-white) selected glyphs under it. ~0.42
            // reads as the reference's soft highlight; CLERIA_CUR_A tunes it against the grab.
            float kGlow = 0.35f;
            if (const char* e = std::getenv("CLERIA_CUR_A")) kGlow = float(std::atof(e));
            const int GW = 96, GH = 32;
            std::vector<std::uint8_t> px(std::size_t(GW) * GH * 4);
            for (int y = 0; y < GH; ++y) {
                const float t = float(y) / float(GH - 1);           // 0 top .. 1 bottom
                // piecewise 0.125 -> 0.875 (mid) -> 1.0 (bottom)
                const float va = (t < 0.5f) ? (0.125f + (0.875f - 0.125f) * (t / 0.5f))
                                            : (0.875f + (1.0f - 0.875f) * ((t - 0.5f) / 0.5f));
                for (int x = 0; x < GW; ++x) {
                    const float u = float(x) / float(GW - 1);
                    // soft ends: fade the outer ~16% each side
                    float ha = 1.0f;
                    if (u < 0.16f) ha = u / 0.16f;
                    else if (u > 0.84f) ha = (1.0f - u) / 0.16f;
                    std::uint8_t* o = &px[(std::size_t(y) * GW + x) * 4];
                    o[0] = o[1] = o[2] = 255;
                    o[3] = std::uint8_t(std::clamp(va * ha * kGlow, 0.0f, 1.0f) * 255.0f);
                }
            }
            curGlow = upload_rgba(g, GW, GH, px.data());
        }
        Dds s = load_dds(asset_root / "IMAGE" / "TITLE_000.DDS");        // the full splash art
        Dds p = load_dds(asset_root / "MENU" / "TITLEPILLAR16-9.DDS");   // title widescreen side fill
        Dds ap = load_dds(asset_root / "MENU" / "ALTPILLAR16-9.DDS");    // golden goddess side fill (menus)
        Dds c = load_dds(asset_root / "IMAGE" / "MENUCURSOR.DDS");       // scroll-curl selection bar
        Dds fr = load_dds(asset_root / "MENU" / "BACK00.DDS");           // ornate menu frame
        Dds cs = load_dds(asset_root / "MENU" / "CHRSELECT.DDS");        // char-select atlas
        Dds dg = load_dds(asset_root / "IMAGE" / "DIALOG0.DDS");         // popup scroll frame
        { Dds fw = load_dds(asset_root / "IMAGE" / "FACEWIN0.DDS");      // cutscene dialogue box
          if (fw.ok()) facewin = upload_rgba(g, fw.width, fw.height, fw.data.data()); }
        if (s.ok()) splash = upload_rgba(g, s.width, s.height, s.data.data());
        if (p.ok()) pillar = upload_rgba(g, p.width, p.height, p.data.data());
        if (ap.ok()) altpillar = upload_rgba(g, ap.width, ap.height, ap.data.data());
        // MENUCURSOR is an ornate pill OUTLINE (bright silver lines + curls, transparent interior),
        // blue-tinted. The engine draws it WHITE. Desaturate to the max channel and lift the bright
        // lines to white while keeping the dark inner edge dark (that edge is what gives the thin
        // lines contrast on the bright splash -- flattening to pure white made them vanish).
        if (c.ok()) {
            // The engine's selection pill is a bright-WHITE but FAINT glow, not an opaque silver
            // outline: push the lines to white (whitening >100%) and drop the opacity hard. A strong
            // opaque pill over-darkens the lit row vs the reference (title_pixeldiff.py).
            int white = 340, calpha = 200;   // whitening %, alpha scale % (bright-white, near-opaque)
            if (const char* e = std::getenv("CLERIA_CUR_WHITE")) white = std::atoi(e);
            if (const char* e = std::getenv("CLERIA_CUR_ALPHA")) calpha = std::atoi(e);
            for (std::size_t i = 0; i + 3 < c.data.size(); i += 4) {
                const int v = std::max(c.data[i], std::max(c.data[i + 1], c.data[i + 2]));
                const std::uint8_t o = std::uint8_t(std::min(255, v * white / 100));
                c.data[i] = c.data[i + 1] = c.data[i + 2] = o;
                c.data[i + 3] = std::uint8_t(std::min(255, c.data[i + 3] * calpha / 100));
            }
            cursor = upload_rgba(g, c.width, c.height, c.data.data());
        }
        if (fr.ok()) frame = upload_rgba(g, fr.width, fr.height, fr.data.data());
        if (cs.ok()) chrsel = upload_rgba(g, cs.width, cs.height, cs.data.data());
        if (dg.ok()) dialog = upload_rgba(g, dg.width, dg.height, dg.data.data());
        { Dds ob = load_dds(asset_root / "MENU" / "OPBTN.DDS");    // Option-screen button/checkbox atlas
          if (ob.ok()) opbtn = upload_rgba(g, ob.width, ob.height, ob.data.data()); }
        { Dds op = load_dds(asset_root / "MENU" / "OPTION.DDS");   // "Option" title plaque (2 states)
          if (op.ok()) plaque = upload_rgba(g, op.width, op.height, op.data.data()); }
        // Pause-book Equip page (PAUSEBOOK.md §3): the "Equip" plaque, the left Status panel bg
        // (w_Status), and the right equip grid plate (w_Equip, WEAPON/ARMOR/BOOTS/ACCESSORIES/SKILLS).
        { Dds e = load_dds(asset_root / "MENU" / "EQUIP.DDS");
          if (e.ok()) equipPlaque = upload_rgba(g, e.width, e.height, e.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "W_STATUS.DDS");
          if (w.ok()) wstatus = upload_rgba(g, w.width, w.height, w.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "W_EQUIP.DDS");
          if (w.ok()) wequip = upload_rgba(g, w.width, w.height, w.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "W_FS_MSG.DDS");   // Equip/Item bottom detail panel (ornate)
          if (w.ok()) wdetail = upload_rgba(g, w.width, w.height, w.data.data()); }
        { Dds e = load_dds(asset_root / "MENU" / "INVENTORY.DDS");   // Item-page "Inventory" plaque + 6x5 grid
          if (e.ok()) invPlaque = upload_rgba(g, e.width, e.height, e.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "W_INVENT.DDS");
          if (w.ok()) winvent = upload_rgba(g, w.width, w.height, w.data.data()); }
        { Dds e = load_dds(asset_root / "MENU" / "TOPIC.DDS");   // Topic page: plaque + 3 category buttons + cursor
          if (e.ok()) topicPlaque = upload_rgba(g, e.width, e.height, e.data.data()); }
        for (int i = 0; i < 3; ++i) {
            Dds b = load_dds(asset_root / "MENU" / ("TOPIC_BTN" + std::to_string(i) + ".DDS"));
            if (b.ok()) topicBtn[i] = upload_rgba(g, b.width, b.height, b.data.data());
        }
        { Dds c = load_dds(asset_root / "MENU" / "TOPIC_CUR.DDS");
          if (c.ok()) topicCur = upload_rgba(g, c.width, c.height, c.data.data()); }
        { Dds b = load_dds(asset_root / "MENU" / "SHOPPILLAR16-9.DDS");   // widescreen margin layer
          if (b.ok()) shopBg = upload_rgba(g, b.width, b.height, b.data.data()); }
        { Dds b = load_dds(asset_root / "MENU" / "BACK01.DDS");           // Bonus Shop backdrop (SHOP_LAYOUT.md)
          if (b.ok()) shopBack = upload_rgba(g, b.width, b.height, b.data.data()); }
        { Dds b = load_dds(asset_root / "IMAGE" / "T_400A.DDS");          // Feena key-art illustration
          if (b.ok()) shopFeena = upload_rgba(g, b.width, b.height, b.data.data()); }
        { Dds e = load_dds(asset_root / "MENU" / "LOAD.DDS");     // Save/Load: plaques + W_SVLD slot/tab plate
          if (e.ok()) loadPlaque = upload_rgba(g, e.width, e.height, e.data.data()); }
        { Dds e = load_dds(asset_root / "MENU" / "SAVE.DDS");
          if (e.ok()) savePlaque = upload_rgba(g, e.width, e.height, e.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "W_SVLD.DDS");
          if (w.ok()) wsvld = upload_rgba(g, w.width, w.height, w.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "SQUARE.DDS");   // grid cell tile + cursor (tex 10)
          if (w.ok()) square = upload_rgba(g, w.width, w.height, w.data.data()); }
        // Equipped/selected highlight quad (0x4259999a ARGB = teal-blue (89,153,154) @ alpha 66).
        blueHi = solid(89, 153, 154, 66);
        // Character-select key art + fixed-pose chibi: index 1/2/3 (Yunica/Hugo/Toal); 0 is Adol (unused).
        for (int i = 0; i < 3; ++i) {
            Dds d = load_dds(asset_root / "MENU" /
                             ("CHRSELECT_PORTRAIT" + std::to_string(i + 1) + ".DDS"));
            if (d.ok()) portrait[i] = upload_rgba(g, d.width, d.height, d.data.data());
            Dds cd = load_dds(asset_root / "MENU" / ("CHRSELECT_CHR" + std::to_string(i + 1) + ".DDS"));
            if (cd.ok()) chibiPose[i] = upload_rgba(g, cd.width, cd.height, cd.data.data());
        }
        // The little walking chibi (C_102/103/104 = Yunica/Hugo/Toal); resolve the walk animation
        // by tag 0 (canonical across characters) so char-select can animate it bottom-right.
        static const char* cai[3] = {"C_102.AIA", "C_103.AIA", "C_104.AIA"};
        std::uint32_t chibiTag = 1;   // run -- shows the weapon (0 walk / 1 run / 2 idle); CLERIA_CS_CHIBI_TAG
        if (const char* e = std::getenv("CLERIA_CS_CHIBI_TAG")) chibiTag = std::uint32_t(std::atoi(e));
        for (int i = 0; i < 3; ++i)
            if (chibi[i].load(g, asset_root / "ANI" / cai[i])) {
                const int w = chibi[i].find_tag(chibiTag);
                if (w >= 0) chibi[i].set_anim(g, std::uint32_t(w), 0);
            }
        // Arrow-clip the face cutouts: the atlas face crops are RECTANGULAR, but the game clips each
        // into the bar's arrow shape. Multiply each face's alpha by the bar-arrow alpha (dark-bar
        // region) sampled at the face's position within the bar -- matching the draw rects (bar
        // x60..956 == atlas u[0.06,0.875]; face x654..897 / y row+14..+110). Kills the rectangular
        // overhang past the arrow's tapering right edge.
        {
            Dds csd = load_dds(asset_root / "MENU" / "CHRSELECT.DDS");
            if (csd.ok()) {
                const int AW = int(csd.width), AH = int(csd.height);
                auto samp = [&](float u, float v, std::uint8_t* out) {
                    const int x = std::clamp(int(u * AW), 0, AW - 1), y = std::clamp(int(v * AH), 0, AH - 1);
                    std::memcpy(out, &csd.data[(std::size_t(y) * AW + x) * 4], 4);
                };
                // bar-arrow alpha region under the face (atlas uv), from the draw rects:
                const float bu0 = 0.600f, bu1 = 0.821f, bv0 = 0.320f, bv1 = 0.493f;
                // Face atlas UVs (code-traced): u = col*0.25, v[0.5625, 0.7578125].
                static const float kU[3][2] = {{0.0f, 0.25f}, {0.25f, 0.5f}, {0.5f, 0.75f}};
                constexpr float fv0 = 0.5625f, fv1 = 0.7578125f;
                const int FW = 256, FH = 128;
                std::vector<std::uint8_t> buf(std::size_t(FW) * FH * 4);
                for (int i = 0; i < 3; ++i) {
                    for (int y = 0; y < FH; ++y)
                        for (int x = 0; x < FW; ++x) {
                            const float sx = float(x) / FW, sy = float(y) / FH;
                            std::uint8_t face[4], bar[4];
                            samp(kU[i][0] + sx * (kU[i][1] - kU[i][0]), fv0 + sy * (fv1 - fv0), face);
                            samp(bu0 + sx * (bu1 - bu0), bv0 + sy * (bv1 - bv0), bar);
                            std::uint8_t* o = &buf[(std::size_t(y) * FW + x) * 4];
                            o[0] = face[0]; o[1] = face[1]; o[2] = face[2];
                            // Clip to the arrow AND suppress the crop's soft light halo (square the
                            // face alpha) so only the face shows, not a light rectangular box.
                            const int fa = face[3] * face[3] / 255;
                            o[3] = std::uint8_t(fa * bar[3] / 255);
                        }
                    faceClip[i] = upload_rgba(g, FW, FH, buf.data());
                }
            }
        }
        return splash.tex != nullptr;
    }
    // Compose the title: the pillarbox art fills the whole (widescreen) screen, then the
    // square 1024^2 splash (TITLE_000: logo + moon + landscape + credits) is drawn fit to
    // HEIGHT and centred on top, so its full height (incl. the bottom credits) shows and
    // the pillar continues the misty landscape into the 16:9 side bars.
    void draw(Gpu& g, WGPUTextureView cview, WGPUTextureView dview, std::uint32_t W, std::uint32_t H) {
        const float aspect = float(W) / float(H);
        // The real art lives in the top 768 of each 1024^2 (4:3); the bottom 256 is padding
        // (brown on the splash, transparent on the pillar). Crop V to 0.75.
        constexpr float vc = 0.75f;
        const float sw = (4.0f / 3.0f) / aspect;   // 4:3 splash fit to height -> half-width
        // The side bars must be ANCHORED at their inner edge to the splash edge (±sw), not drawn
        // full-screen. The pillar's landscape meets the splash where its transparent centre begins
        // (u ~0.168 left / ~0.831 right). Draw the LEFT strip u[0,0.168] into the left margin [-1,-sw]
        // (its inner edge u0.168 landing exactly on -sw) and the RIGHT strip u[0.831,1] into [sw,1] --
        // so the landscape lines up at any aspect instead of the strip being cut mid-way by the splash.
        // Inner-edge u pulled ~2 texels inside the opaque strip (true transparent-centre bounds are
        // 0.168/0.831): the pillar goes transparent-black exactly at 0.168, and sampling that edge
        // bleeds a dark column at the seam. 0.166/0.833 keeps the landscape aligned with no bleed.
        constexpr float pc0 = 0.166f, pc1 = 0.833f;
        draw_screen_image(g, cview, dview, /*clear*/true, pillar, -1, 1, -sw, -1, 0.9f, 0.0f, 0.0f, pc0, vc);
        draw_screen_image(g, cview, dview, false, pillar, sw, 1, 1, -1, 0.9f, pc1, 0.0f, 1.0f, vc);
        // The 4:3 splash (logo + moon + landscape + credits) centred, full height.
        draw_screen_image(g, cview, dview, false, splash, -sw, 1, sw, -1, 0.5f, 0, 0, 1, vc);
    }

    static constexpr int kItemCount = 5;
    static const char* item(int i) {
        static const char* items[kItemCount] = {"New Game", "Load Game", "Bonus Modes",
                                                 "Options", "Exit"};
        return items[i];
    }
    // The Bonus Modes sub-list (engine's DAT_0074e259 != 0 state). Selecting "Bonus Modes"
    // on the main list swaps to these. Back (or Esc) returns. MENUS.md §1.4.
    static constexpr int kBonusCount = 4;
    static const char* bonusItem(int i) {
        static const char* items[kBonusCount] = {"Time Attack", "Arena Mode", "Bonus Shop",
                                                  "Back"};
        return items[i];
    }
    // Draw the centred label list with the MENUCURSOR bar on the selection. Exact geometry from
    // build/menu_re/MENU_LAYOUT.md: authored 1024x768 (4:3, fit-to-height), centre X=512, font
    // cell 32, row pitch 38.4, bottom-anchored (~y694), cursor = one 32-tall cell spanning the
    // widest label +32 each side. Shared by the title main menu and the bonus sub-list.
    void draw_list(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                   std::uint32_t W, std::uint32_t H, int count, const char* (*get)(int), int sel) {
        const float sw = float(W), sh = float(H);
        const float A = sh / 768.0f;                 // authored px -> screen px (height-fit 4:3)
        const float pxH = 32.0f * A, pitch = 38.4f * A, cellH = 32.0f * A;
        // Bottom anchor: code-traced originY gives the last row cell-top at authored 694.4
        // (TITLE_LAYOUT.md §2, no-Steam case). Measured against reference/title_main.png the rows
        // sit ~10 authored px lower (a font baseline-convention delta the RE flagged as "within
        // cell") -> nudge the anchor to 704 so the list registers with the grab.
        const float lastTop = 704.0f * A;            // top of the last row's cell
        const float cxpx = sw * 0.5f;                // X=512 of the centred 4:3 field
        auto ndcX = [&](float x) { return 2.0f * x / sw - 1.0f; };
        auto ndcY = [&](float y) { return 1.0f - 2.0f * y / sh; };
        auto rowTop = [&](int i) { return lastTop - (count - 1 - i) * pitch; };
        float maxW = 0;
        for (int i = 0; i < count; ++i) maxW = std::max(maxW, mf.measure(get(i), pxH));
        const float ty = 3.0f * A;
        // Row colours (TITLE_LAYOUT.md §1, code-traced): unselected diffuse 0xB0D0D0 = (176,208,208)
        // light teal; selected row PURE WHITE (255,255,255). NO drop-shadow -- the engine emits a
        // single glyph run per string, no second offset/tint pass (UI_BLEND_FONT.md §2). Still
        // env-tunable for A/B checks against the reference grab.
        int ur = 176, ug = 208, ub = 208, ua = 255;
        int sr = 255, sg = 255, sb = 255, sa = 255;
        if (const char* e = std::getenv("CLERIA_MENU_U")) { sscanf(e, "%d,%d,%d,%d", &ur, &ug, &ub, &ua); }
        if (const char* e = std::getenv("CLERIA_MENU_S")) { sscanf(e, "%d,%d,%d,%d", &sr, &sg, &sb, &sa); }
        // Selection cursor: the ornate scroll-curl bar (IMAGE/MENUCURSOR.DDS), 32px tall,
        // width maxW+64, centred on the selected row (TITLE_LAYOUT.md §53). The chrome
        // handle DAT_00749d68 the disasm couldn't name IS this asset — a dark plaque with
        // filigree ends and a ~transparent centre, so the text reads straight through it.
        // We were drawing a plain additive glow (curGlow) instead; draw_cursor_bar 3-slices
        // MENUCURSOR so the curls keep their thickness at any width.
        if (cursor.tex || curGlow.tex) {
            const float mid = rowTop(sel) + ty + 0.46f * pxH;   // glyph vertical centre
            const float half = 16.0f * A, barW = maxW + 64.0f * A;
            if (cursor.tex)
                draw_cursor_bar(g, cview, dview, cxpx - barW * 0.5f, mid - half,
                                cxpx + barW * 0.5f, mid + half, sw, sh);
            else
                draw_cursor_glow(g, cview, dview, cxpx - barW * 0.5f, mid - half,
                                 cxpx + barW * 0.5f, mid + half, sw, sh);
        }
        for (int i = 0; i < count; ++i) {
            const bool s = (i == sel);
            mf.text_centered(get(i), 0.5f, rowTop(i) + ty, pxH, sw, sh,
                             (std::uint8_t)(s ? sr : ur), (std::uint8_t)(s ? sg : ug),
                             (std::uint8_t)(s ? sb : ub), (std::uint8_t)(s ? sa : ua));
        }
        mf.draw(g, cview);
    }
    void draw_menu(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                   std::uint32_t W, std::uint32_t H, int sel) {
        draw_list(g, mf, cview, dview, W, H, kItemCount, item, sel);
    }
    void draw_bonus(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                    std::uint32_t W, std::uint32_t H, int sel) {
        draw(g, cview, dview, W, H);   // redraw the splash under the sub-list
        draw_list(g, mf, cview, dview, W, H, kBonusCount, bonusItem, sel);
    }

    static constexpr int kCharCount = 3;   // g_flags[150]: 1 Yunica, 2 Hugo, 3 Toal
    static const char* charName(int i) {
        static const char* n[kCharCount] = {"Yunica", "Hugo", "Toal"};
        return n[i];
    }
    static const char* charDesc(int i) {
        static const char* d[kCharCount] = {
            "Axe-wielding fighter. Mid speed, mid range.",
            "Energy-firing mage. Slow speed, long range.",
            "Berserker. Fast, close range. Experts only!"};
        return d[i];
    }
    // The real Character Select (build/menu_re/MENUS.md + CHRSELECT.DDS atlas): the BACK00
    // frame, the "Character Select" banner, the selected character's key art on the left, three
    // selection bars on the right (the crossed one cyan, the others dark) each capped with the
    // character's face icon, and the description on the bottom bar. sel 0/1/2 -> character 1/2/3.
    void draw_charselect(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                         std::uint32_t W, std::uint32_t H, int sel, float pulse = 0.0f,
                         int chibiFrame = 0) {
        const float sw = float(W), sh = float(H);
        // The game PILLAR-BOXES char-select: the 1024x768 authored field is fit to height and
        // centred horizontally (uniform scale sh/768), NOT stretched to 16:9. The letterbox sides
        // are the Archipelago mod's sepia art in reference/charselect.png -- ignore them.
        const float fwN = (4.0f / 3.0f) * sh / sw;                    // 4:3 field half-width in NDC
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };   // authored -> NDC
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };          // authored -> screen px
        // CHRSELECT.DDS atlas sub-rects (CHARSELECT_LAYOUT.md §1, code-traced from FUN_005c2110):
        // bar UV u[0,0.875] v[0.28125,0.53125] (unselected/dark); face UV v[0.5625,0.7578125],
        // u = col*0.25 (col 0/1/2/3). The cyan selection-highlight band's V is a separate overlay.
        constexpr float kBarU0 = 0.0f, kBarU1 = 0.875f;
        constexpr float kCyanV0 = 0.045f, kCyanV1 = 0.264f;
        constexpr float kDarkV0 = 0.28125f, kDarkV1 = 0.53125f;
        constexpr float kFaceV0 = 0.5625f, kFaceV1 = 0.7578125f;
        static const float kFaceU[3][2] = {{0.0f, 0.25f}, {0.25f, 0.5f}, {0.5f, 0.75f}};
        // 1. Frame BACK00 fills the 4:3 field. Its art content ends at texture row 767/1024 =
        //    v0.749 (measured); sampling further (the old 0.772) pulled ~23 black rows in and
        //    compressed the bottom marble border ~21 authored units too high. Clears the
        //    letterbox to the dark background.
        // 0. Golden goddess-statue side-art fills the 16:9 letterbox margins (ALTPILLAR16-9: left
        //    strip u0..0.167, right strip u0.832..1, content v0..0.749). Drawn full-screen behind the
        //    field, then the frame (below) covers the centre 4:3 -- so only the margins show goddesses.
        if (altpillar.tex) {   // goddess strips anchored: each inner edge meets the frame (AX(0)/AX(1024)),
            // NOT full-screen (which cut the goddess mid-figure); u pulled 2 texels in to avoid an edge bleed.
            draw_screen_image(g, cview, dview, /*clear*/true, altpillar, -1.0f, 1.0f, AX(0), -1.0f,
                              0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f,
                              0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        }
        if (frame.tex)
            draw_screen_image(g, cview, dview, /*clear*/altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0.0f, 0.0f, 1.0f, 0.749f);
        // 2. "Character Select" banner across the top of the field. Width (authored x 290..734) is
        //    correct; the old y -8..98 stretched the glyphs 1.28x too tall (measured C-height 37 vs
        //    reference 29). y -2..81 keeps the top fixed and matches the reference glyph height.
        if (chrsel.tex)
            draw_screen_image(g, cview, dview, false, chrsel, AX(274), AY(0), AX(738), AY(80), 0.5f,
                              0.557f, 0.844f, 0.998f, 1.0f);
        // 3. Three selection bars (EXTRA_SKINS.md §4: x=60..956, h=128, y={128,272,416}; row
        //    0/1/2 = Yunica/Hugo/Toal), the crossed one cyan, the rest dark, each capped by its
        //    face icon in the wide right end. Drawn BEFORE the portrait so the character's cape
        //    blends over the bars (else the cape's semi-transparent edge writes depth and punches
        //    light "holes"/halo through the bars behind it).
        const float rowYtop[3] = {128.0f, 272.0f, 416.0f};
        for (int i = 0; i < 3; ++i) {
            const bool s = (i == sel);
            const float yt = rowYtop[i], yb = yt + 128.0f;
            if (chrsel.tex) {
                // The selected (cyan) bar PULSATES toward white in-game; `pulse` (0..1) white-tints it.
                draw_screen_image(g, cview, dview, false, chrsel, AX(60), AY(yt), AX(956), AY(yb),
                                  0.45f, kBarU0, s ? kCyanV0 : kDarkV0, kBarU1, s ? kCyanV1 : kDarkV1,
                                  0.02f, 0.02f, 0.05f, s ? pulse : 0.0f);
                if (faceClip[i].tex)   // face pre-clipped to the arrow (built in init); code rect x646 w256 h100
                    draw_screen_image(g, cview, dview, false, faceClip[i], AX(646), AY(yt + 14),
                                      AX(902), AY(yt + 114), 0.42f);
            }
        }
        // 4. The selected character's key art, on TOP of the bars. Code-traced (CHARSELECT_LAYOUT.md
        //    §1.1): the engine draws the FULL ChrSelect_Portrait{N}.dds (UV {0,0,1,1}) as a 1024x1024
        //    quad at the top-left origin (0,0) -- the character art is baked left-anchored, the visible
        //    768 shows the top. Replaces the old hand-tuned {2,41,735} crop.
        if (sel >= 0 && sel < 3 && portrait[sel].tex)
            draw_screen_image(g, cview, dview, false, portrait[sel], AX(0), AY(0),
                              AX(1024), AY(1024), 0.35f, 0.0f, 0.0f, 1.0f, 1.0f);
        // 4b. The fixed-pose chibi in the bottom-right. Code-traced (CHARSELECT_LAYOUT.md §1.1): the
        //     dedicated ChrSelect_Chr{N}.dds drawn as a 128x128 csprite centred at authored (856,644)
        //     -- NOT the AIA run animation. The DDS is a small figure on a transparent canvas.
        (void)chibiFrame;
        if (sel >= 0 && sel < 3 && chibiPose[sel].tex) {
            // The DDS is a 512x256 sheet (4x2 grid of 128px poses); the csprite samples one 128x128
            // cell from UV base (6.4,12.8)px -> u[0.0125,0.2625] v[0.05,0.55] (CHARSELECT_LAYOUT §1.1).
            const float cx = 856.0f, cy = 644.0f, half = 64.0f;   // 128x128 authored dst quad
            draw_screen_image(g, cview, dview, false, chibiPose[sel], AX(cx - half), AY(cy - half),
                              AX(cx + half), AY(cy + half), 0.3f, 0.0125f, 0.05f, 0.2625f, 0.55f);
        }
        // 5. Description: "[Name]" left-aligned at authored x=48, and the blurb at a FIXED column
        //    x=209 (measured: the reference body-start is ~209 for all three names regardless of
        //    name length -- it is a tab column, not name+spaces). Baseline authored y=704.
        const float dy = 704.0f / 768.0f * sh, dsz = 32.0f / 768.0f * sh;
        mf.text(std::string("[") + charName(sel) + "]", SX(48), dy, dsz, sw, sh, 236, 236, 236, 255);
        mf.text(charDesc(sel), SX(209), dy, dsz, sw, sh, 236, 236, 236, 255);
        mf.draw(g, cview);
    }

    // The engine's selection cursor: the baked curGlow bar drawn ADDITIVE (renderstate 0x40c1)
    // OVER the text. White with a vertical alpha gradient; additive so it brightens the row toward
    // white rather than masking it (matches the reference's soft highlight, not an opaque pill).
    void draw_cursor_glow(Gpu& g, WGPUTextureView cview, WGPUTextureView dview,
                          float lpx, float tpx, float rpx, float bpx, float sw, float sh) {
        if (!curGlow.tex) return;
        auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
        auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
        draw_screen_image(g, cview, dview, false, curGlow, NX(lpx), NY(tpx), NX(rpx), NY(bpx),
                          0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 0.02f, 0.02f, 0.05f, 0.0f, /*additive*/true);
    }
    // MENUCURSOR as a horizontal 9-slice (curls u[0,0.11] and [0.89,1.0] fixed, middle stretched)
    // so the ornate ends keep their thickness at any bar width instead of being stretched thin.
    void draw_cursor_bar(Gpu& g, WGPUTextureView cview, WGPUTextureView dview,
                         float lpx, float tpx, float rpx, float bpx, float sw, float sh) {
        if (!cursor.tex) return;
        constexpr float u1 = 0.11f, u2 = 0.89f;
        float curl = (bpx - tpx) * (u1 * 256.0f) / 32.0f;   // curl 28px wide / 32px tall native
        if (curl * 2.0f > (rpx - lpx) * 0.9f) curl = (rpx - lpx) * 0.45f;
        auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
        auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
        const float z = 0.3f;
        draw_screen_image(g, cview, dview, false, cursor, NX(lpx),        NY(tpx), NX(lpx + curl), NY(bpx), z, 0.0f, 0.0f, u1,   1.0f);
        draw_screen_image(g, cview, dview, false, cursor, NX(lpx + curl), NY(tpx), NX(rpx - curl), NY(bpx), z, u1,   0.0f, u2,   1.0f);
        draw_screen_image(g, cview, dview, false, cursor, NX(rpx - curl), NY(tpx), NX(rpx),        NY(bpx), z, u2,   0.0f, 1.0f, 1.0f);
    }
    // DIALOG0 as a horizontal 9-slice (u-cuts 0 / 0.15625 / 0.65625 / 0.8125 from MENU_LAYOUT.md
    // §3.1; content v 0..0.805): fixed scroll-curl ends, stretched middle, so the curls keep their
    // shape at any width. The engine's go_wnddialog popup frame is exactly this.
    void draw_dialog_frame(Gpu& g, WGPUTextureView cview, WGPUTextureView dview,
                           float lpx, float tpx, float rpx, float bpx, float sw, float sh) {
        if (!dialog.tex) return;
        constexpr float vc = 0.805f, u1 = 0.15625f, u2 = 0.65625f, u3 = 0.8125f;
        float curl = (bpx - tpx) * (u1 * 512.0f) / (vc * 256.0f);   // curl 80px wide / 206px tall
        if (curl * 2.0f > (rpx - lpx) * 0.9f) curl = (rpx - lpx) * 0.45f;
        auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
        auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
        const float z = 0.2f;
        draw_screen_image(g, cview, dview, false, dialog, NX(lpx),        NY(tpx), NX(lpx + curl), NY(bpx), z, 0.0f, 0.0f, u1, vc);
        draw_screen_image(g, cview, dview, false, dialog, NX(lpx + curl), NY(tpx), NX(rpx - curl), NY(bpx), z, u1,   0.0f, u2, vc);
        draw_screen_image(g, cview, dview, false, dialog, NX(rpx - curl), NY(tpx), NX(rpx),        NY(bpx), z, u2,   0.0f, u3, vc);
    }
    // The in-engine cutscene DIALOGUE box (CUTSCENE_DIALOGUE.md): FACEWIN0 as a full-width
    // bottom bar (authored y544..768 of the 768 frame => bottom 224px), 3-sliced so the ornate
    // curls keep their size while the middle stretches. `name` = cream line above the divider,
    // `text` = white body below, split on the literal "\n" authors write, revealed up to
    // `reveal` chars (typewriter; <0 shows all). Portrait slot on the left is a TODO — the
    // face asset/id mapping is still [UNKNOWN] (KAO_* is a noise texture, not portraits).
    void draw_dialogue(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                       std::uint32_t W, std::uint32_t H, const std::string& name,
                       const std::string& text, int reveal) {
        if (!facewin.tex) return;
        const float sw = float(W), sh = float(H);
        const float by1 = sh, by0 = sh - sh * (224.0f / 768.0f);   // bottom 224/768 of the frame
        const float bx0 = 0.0f, bx1 = sw;
        constexpr float vc = 0.5f, uL = 0.19f, uR = 0.586f, uEnd = 0.809f;   // FACEWIN0 slice UVs
        float curl = (by1 - by0) * (uL * 512.0f) / (vc * 512.0f);   // keep the src curl aspect
        if (curl * 2.0f > (bx1 - bx0) * 0.5f) curl = (bx1 - bx0) * 0.25f;
        auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
        auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
        const float z = 0.15f;
        draw_screen_image(g, cview, dview, false, facewin, NX(bx0),        NY(by0), NX(bx0 + curl), NY(by1), z, 0.0f, 0.0f, uL,   vc);
        draw_screen_image(g, cview, dview, false, facewin, NX(bx0 + curl), NY(by0), NX(bx1 - curl), NY(by1), z, uL,   0.0f, uR,   vc);
        draw_screen_image(g, cview, dview, false, facewin, NX(bx1 - curl), NY(by0), NX(bx1),        NY(by1), z, uR,   0.0f, uEnd, vc);
        const float boxH = by1 - by0;
        const float tx = sw * 0.15f;                 // left text margin (past the portrait slot)
        const float pxName = boxH * 0.15f, pxBody = boxH * 0.21f;
        if (!name.empty())
            mf.text(name, tx, by0 + boxH * 0.10f, pxName, sw, sh, 240, 224, 192, 255);   // cream
        std::string shown = (reveal >= 0 && reveal < int(text.size())) ? text.substr(0, reveal) : text;
        float ly = by0 + boxH * 0.34f;
        std::size_t i = 0;
        for (;;) {
            const std::size_t nl = shown.find("\\n", i);   // authors write the two chars \ n
            mf.text(shown.substr(i, (nl == std::string::npos ? shown.size() : nl) - i),
                    tx, ly, pxBody, sw, sh, 255, 255, 255, 255);
            ly += pxBody * 1.15f;
            if (nl == std::string::npos) break;
            i = nl + 2;
        }
        mf.draw(g, cview);   // flush the accumulated glyphs
    }
    // The New-Game choice popups (Type Select, Difficulty, SP Bonus Acquisition): a go_wnddialog
    // list (MENU_LAYOUT.md) -- caption at authored {512,256}, items 38.4 apart below, font cell 32,
    // centred on X=512 -- inside the DIALOG0 9-slice frame, with the MENUCURSOR bar on the choice.
    void draw_popup(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                    std::uint32_t W, std::uint32_t H, const char* title,
                    const char* const* items, int count, int sel,
                    bool onBlack = false, float capTop = 211.2f) {
        const float sw = float(W), sh = float(H);
        const float A = sh / 768.0f;
        // The engine DIMS whatever is behind the go_wnddialog popup (char-select or, historically, a
        // black screen for the SP-Bonus step -- but §2.3 shows every popup shares origin (512,256) over
        // the dimmed backdrop; the on-black grab was an artifact). Draw the dimmer twice for a ~70%
        // darken to match the reference.
        if (onBlack)
            draw_screen_image(g, cview, dview, /*clear*/true, dim, -1, 1, 1, -1, 0.25f, 0, 0, 1, 1, 0, 0, 0);
        else if (dim.tex) {   // two passes at distinct z (else the depth test dedups the 2nd) -> ~72% darken
            draw_screen_image(g, cview, dview, false, dim, -1, 1, 1, -1, 0.26f);
            draw_screen_image(g, cview, dview, false, dim, -1, 1, 1, -1, 0.24f);
        }
        const float pxH = 32.0f * A, pitch = 38.4f * A, cellH = 32.0f * A;
        const float cxpx = sw * 0.5f;                     // authored X=512 (centre)
        // Invariant origin (512,256): caption text-top at origin-44.8 = 211.2, item i (1-based) at
        // origin + (i-1)*38.4 + 6.4 -> item1 = 262.4 (caption->item1 gap 51.2). Frame top = origin-64
        // = 192; side pad 80 (CHARSELECT_LAYOUT.md §2).
        const float captionTop = capTop * A;
        const float firstItem = captionTop + 51.2f * A;
        auto itemTop = [&](int i) { return firstItem + i * pitch; };
        float maxW = mf.measure(title, pxH);
        for (int i = 0; i < count; ++i) maxW = std::max(maxW, mf.measure(items[i], pxH));
        const float fhalf = (maxW + 41.6f * A) * 0.5f + 80.0f * A;
        draw_dialog_frame(g, cview, dview, cxpx - fhalf, (capTop - 19.2f) * A,
                          cxpx + fhalf, itemTop(count - 1) + cellH + 22.0f * A, sw, sh);
        if (curGlow.tex) {                                // additive glow highlight on the choice (0x40c1)
            const float mid = itemTop(sel) + 0.46f * pxH, half = 16.0f * A, barW = maxW + 64.0f * A;
            draw_cursor_glow(g, cview, dview, cxpx - barW * 0.5f, mid - half,
                             cxpx + barW * 0.5f, mid + half, sw, sh);
        }
        mf.text_centered(title, 0.5f, captionTop, pxH, sw, sh, 240, 246, 250, 255);
        for (int i = 0; i < count; ++i) {
            const bool s = (i == sel);
            // Reference glyph colours: selected pure white [255,255,255], unselected teal ~[176,208,208].
            mf.text_centered(items[i], 0.5f, itemTop(i), pxH, sw, sh,
                             s ? 250 : 172, s ? 252 : 206, s ? 252 : 208, 255);
        }
        mf.draw(g, cview);
    }

    // The Option/Config screen (build/menu_re/OPTIONS.md). Pillar-boxed like char-select: goddess
    // margins + BACK00 frame, the "Option" plaque top-left, then 10 rows -- 3 volume/brightness
    // sliders, 4 checkboxes, and Default / Return-to-Title / Exit-Game buttons. All button faces,
    // the slider track+knob and the checkbox squares are sub-rects of the OPBTN atlas (baked labels;
    // atlas order != screen order, so each screen row is label-mapped to its band). sliders[0..2] =
    // 0..1024, checks[0..3] = rows 3..6. sel = focused row 0..9.
    void draw_options(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                      std::uint32_t W, std::uint32_t H, int sel,
                      const int sliders[3], const bool checks[4]) {
        const float sw = float(W), sh = float(H);
        const float fwN = (4.0f / 3.0f) * sh / sw;
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };
        auto SY = [&](float y) { return y / 768.0f * sh; };
        constexpr float T = 1.0f / 1024.0f;   // OPBTN atlas is 1024^2
        // 0. goddess pillars + BACK00 frame (identical to char-select's backdrop)
        if (altpillar.tex)
            draw_screen_image(g, cview, dview, /*clear*/true, altpillar, -1.0f, 1.0f, AX(0), -1.0f, 0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
        if (altpillar.tex)   // right goddess strip anchored into the right margin
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f, 0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        if (frame.tex)
            draw_screen_image(g, cview, dview, /*clear*/altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0, 0, 1, 0.749f);
        // 1. "Option" plaque, top-left (OPTION.DDS: gold cartouche is u0..0.598 of the upper half;
        //    the rest of the texture is opaque black padding, so crop to the cartouche).
        if (plaque.tex)   // Option plaque anchor X=128 (VN1), quad ~153.6x51.2 (was hand-tuned x=30)
            draw_screen_image(g, cview, dview, false, plaque, AX(128), AY(8), AX(281), AY(68),
                              0.4f, 0.0f, 0.0f, 0.598f, 0.492f);
        // 2. the 10 rows. rowV = OPBTN band (px) for each SCREEN row, label-mapped; row 7 (Default)
        //    is a short button in the atlas' right area. Row Y (OPTIONS_LAYOUT.md §2): start 96, pitch
        //    54.4, +44.8 once before row 8 -> {96,150.4,...,476.8,576.0,630.4}.
        static const float rowV[10][2] = { {1,48},{53,98},{104,150},{411,456},{462,508},
                                           {514,559},{565,610},{307,356},{155,201},{206,252} };
        static const float rowY[10] = { 96, 150.4f, 204.8f, 259.2f, 313.6f, 368.0f, 422.4f,
                                        476.8f, 576.0f, 630.4f };
        // Inverted focus highlight (FUN_005bae20): SELECTED row = full white (texture as-is), every
        // UNSELECTED row dimmed toward 0x808080. The code value is RGB x0.5, but that assumes a base
        // plate ~2x brighter than the OPBTN region CleriaCore samples renders (measured: our full plate
        // ~38 == the retail *unfocused* level ~35, so x0.5 crushes to ~19, well below the reference).
        // Until the plate-brightness root cause is fixed, dim to 0.7 -> the pick still reads brighter
        // without the "too dark" regression (DIFF_VALIDATION). CLERIA_OPT_DIM overrides.
        float unfocusDim = 0.70f;
        if (const char* e = std::getenv("CLERIA_OPT_DIM")) unfocusDim = float(std::atof(e));
        for (int r = 0; r < 10; ++r) {
            const float bx = (r <= 6) ? 179.0f : (r == 7 ? 409.0f : 307.0f);
            const float bw = (r == 7) ? 175.0f : 409.6f;
            const float u0 = (r == 7) ? 409.0f * T : 0.0f;
            const float u1 = (r == 7) ? 584.0f * T : 409.6f * T;
            const float dim = (r == sel) ? 1.0f : unfocusDim;
            draw_screen_image(g, cview, dview, false, opbtn, AX(bx), AY(rowY[r]), AX(bx + bw),
                              AY(rowY[r] + 51.2f), 0.30f, u0, rowV[r][0] * T, u1, rowV[r][1] * T,
                              0.02f, 0.02f, 0.05f, 0.0f, false, dim, dim, dim);
        }
        // 3. sliders (rows 0-2): OPBTN track (metallic-bordered teal gradient bar, atlas x410..640) +
        //    a tall lit blue pill knob (atlas x32..58) positioned by value along the track. Knob
        //    travels 188.8 px over value 0..1024 (|DAT_006be148|, OPTIONS_LAYOUT.md).
        const float trackX = 652.0f, trackLen = 240.0f, knobTravel = 188.8f;
        for (int i = 0; i < 3; ++i) {
            const float ty = rowY[i] + 25.6f;
            // track = teal-gradient bar with metallic end-caps at atlas x410 and x615; the SECOND bar
            // (rainbow/gamma variant) starts at x618 with a dark-blue cap, so stop at 616 or it leaves
            // a stray blue stub past the track.
            draw_screen_image(g, cview, dview, false, opbtn, AX(trackX), AY(ty - 11.0f),
                              AX(trackX + trackLen), AY(ty + 11.0f), 0.28f, 408*T, 365*T, 616*T, 388*T);
            const float kx = trackX + (float(sliders[i]) / 1024.0f) * knobTravel;
            // knob pill: dim silver-teal (atlas x1..15) when the row isn't focused, lit blue
            // (x16..31) when it is -- the reference grab shows the dim variant.
            const bool kf = (i == sel);
            const float ku0 = (kf ? 16.0f : 1.0f) * T, ku1 = (kf ? 31.0f : 15.0f) * T;
            draw_screen_image(g, cview, dview, false, opbtn, AX(kx - 8.0f), AY(ty - 26.0f),
                              AX(kx + 8.0f), AY(ty + 26.0f), 0.26f, ku0, 305*T, ku1, 357*T);
        }
        // 4. checkboxes (rows 3-6): OPBTN square, checked U=0 / unchecked U=51 (OPTIONS.md §4)
        for (int i = 0; i < 4; ++i) {
            const float cx = 732.0f, cy = rowY[i + 3];
            const float u0 = checks[i] ? 0.0f : 51.0f * T, u1 = checks[i] ? 48.0f * T : 99.0f * T;
            draw_screen_image(g, cview, dview, false, opbtn, AX(cx), AY(cy), AX(cx + 51.2f),
                              AY(cy + 51.2f), 0.26f, u0, 616*T, u1, 661*T);
        }
        // 5. caption bar (book-level string)
        const float dsz = 27.0f * sh / 768.0f;
        mf.text("Alter the game's settings.", SX(48), SY(707), dsz, sw, sh, 236, 236, 236, 255);
        mf.draw(g, cview);
    }

    // Lazily load + cache an item's icon (MENU/ICON/<stem>.DDS via ItemTable). nullptr if unknown.
    const SpriteTex* icon(Gpu& g, const ItemTable& items, const fs::path& asset_root, int id) {
        if (id < 0) return nullptr;
        auto it = iconCache.find(id);
        if (it != iconCache.end()) return it->second.tex ? &it->second : nullptr;
        SpriteTex t{};
        fs::path p = items.icon_path(asset_root, id, false);
        if (!p.empty()) { Dds d = load_dds(p); if (d.ok()) t = upload_rgba(g, d.width, d.height, d.data.data()); }
        return (iconCache[id] = t).tex ? &iconCache[id] : nullptr;
    }

    // The pause-book EQUIP page (build/menu_re/PAUSEBOOK.md §3). Stats come from GAMEINFO curves; item
    // metadata (name/icon/desc) from INVINFO. Equipped-item runtime state isn't tracked by CleriaCore
    // yet, so the caller stubs it. Pillar-boxed like the Option screen.
    struct EquipView {
        int level = 1, hp = 400, maxhp = 400, str = 0, def = 0;
        long exp = 0, next = 0;
        int character = 1;
        int weaponId = -1, armorId = -1, bootsId = -1, accId = -1;   // -1 = empty slot
        int selId = -1;                                              // detail-panel item
        const char* caption = "Your equipped weapon.";
        const char* diffWord = "Normal";
    };
    void draw_equip(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                    std::uint32_t W, std::uint32_t H, const EquipView& e,
                    const ItemTable& items, const fs::path& asset_root) {
        const float sw = float(W), sh = float(H), A = sh / 768.0f;
        const float fwN = (4.0f / 3.0f) * sh / sw;
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };
        auto SY = [&](float y) { return y / 768.0f * sh; };
        // 0. goddess pillars + BACK00 frame (shared book shell)
        if (altpillar.tex)
            draw_screen_image(g, cview, dview, true, altpillar, -1.0f, 1.0f, AX(0), -1.0f, 0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
        if (altpillar.tex)   // right goddess strip anchored into the right margin (inner edge at AX(1024))
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f, 0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        if (frame.tex)
            draw_screen_image(g, cview, dview, altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0, 0, 1, 0.749f);
        // 1. "Equip" plaque -- the book's active plaque rests at anchor X=128 (MN4/VN1), not x=30.
        if (equipPlaque.tex)
            draw_screen_image(g, cview, dview, false, equipPlaque, AX(128), AY(8), AX(281), AY(68),
                              0.4f, 0.0f, 0.0f, 0.598f, 0.492f);
        // 2. LEFT stats panel (w_Status). Plate rect code-traced (FUN_005b05c0, VN1): {182,60,217.6,
        //    358.4}. Six stat rows at pitch 38.4 (FUN_005b0860), row0=92 -> {92,130.4,...,284.0}.
        if (wstatus.tex)
            draw_screen_image(g, cview, dview, false, wstatus, AX(182), AY(60), AX(399.6f), AY(418.4f),
                              0.55f, 0.0f, 0.0f, 0.420f, 0.635f);
        {
            const float psz = 30.0f * A;
            const std::string vals[6] = {
                std::to_string(e.level), std::to_string(e.hp) + "/" + std::to_string(e.maxhp),
                std::to_string(e.str), std::to_string(e.def), std::to_string(e.exp), std::to_string(e.next) };
            static const char* labs[6] = {"Lv.", "HP", "STR", "DEF", "Exp", "Next"};
            static const float ry[6] = {92, 130.4f, 168.8f, 207.2f, 245.6f, 284.0f};
            for (int i = 0; i < 6; ++i) {
                mf.text(labs[i], SX(223), SY(ry[i]), psz, sw, sh, 240, 240, 240, 255);         // label left
                float vw = mf.measure(vals[i], psz);
                mf.text(vals[i], SX(389.6f) - vw, SY(ry[i]), psz, sw, sh, 240, 240, 240, 255);  // value right
            }
        }
        // 3. RIGHT equip grid plate (w_Equip: WEAPON/ARMOR/BOOTS/ACCESSORIES/SKILLS, content u0..0.811 v0..0.650)
        if (wequip.tex)
            draw_screen_image(g, cview, dview, false, wequip, AX(438), AY(67), AX(844), AY(386),
                              0.55f, 0.0f, 0.0f, 0.811f, 0.650f);
        // 4. equipped-item cells (PAUSEBOOK.md §7.2). The equipped WEAPON/ARMOR/BOOTS/ACCESSORIES sit in
        //    column 0, rows 0-3 -> cell centres (483, 118.4/195.2/272.0/348.8); cell 54.4. Each equipped
        //    cell gets a blue highlight quad (0x4259999a) under its icon. The owned-item grid + skills
        //    cells are data-driven from runtime inventory state CleriaCore doesn't track yet (§7.4 gap).
        auto cellCenter = [](int col, int row, float& cx, float& cy) {
            cx = 483.0f + col * 64.0f; cy = 118.4f + row * 76.8f; };
        auto drawIcon = [&](int id, float cx, float cy, float sz, float z) {
            const SpriteTex* t = icon(g, items, asset_root, id);
            if (t) draw_screen_image(g, cview, dview, false, *t, AX(cx - sz / 2), AY(cy - sz / 2),
                                     AX(cx + sz / 2), AY(cy + sz / 2), z);
        };
        const int equipIds[4] = {e.weaponId, e.armorId, e.bootsId, e.accId};
        for (int s = 0; s < 4; ++s) {
            if (equipIds[s] < 0) continue;
            float cx, cy; cellCenter(0, s, cx, cy);
            if (blueHi.tex)   // equipped highlight quad (54.4), under the icon
                draw_screen_image(g, cview, dview, false, blueHi, AX(cx - 27.2f), AY(cy - 27.2f),
                                  AX(cx + 27.2f), AY(cy + 27.2f), 0.32f);
            drawIcon(equipIds[s], cx, cy, 54.4f, 0.3f);
        }
        // Grid cursor cell (SQUARE.dds, 51.2): defaults to the weapon cell (col0,row0). e.selId's cell
        // isn't tracked; the cursor marks the focused slot.
        if (square.tex) {
            float cx, cy; cellCenter(0, 0, cx, cy);
            draw_screen_image(g, cview, dview, false, square, AX(cx - 25.6f), AY(cy - 25.6f),
                              AX(cx + 25.6f), AY(cy + 25.6f), 0.28f);
        }
        // 5. bottom item-detail panel: darken the area, big icon on the left, name + description.
        //    Reference-measured (MN4 [H-med]): the whole panel sits ~100px further left than the old
        //    {178,..,1010} -> panel x[74,914], big icon centre (203,566), name (383,488).
        if (const Item* sel = items.get(e.selId)) {
            if (wdetail.tex)   // W_FS_MSG ornate dark panel (content u0..0.893 v0..0.949)
                draw_screen_image(g, cview, dview, false, wdetail, AX(74), AY(446), AX(914), AY(682),
                                  0.5f, 0.0f, 0.0f, 0.893f, 0.949f);
            drawIcon(e.selId, 203.0f, 566.0f, 150.0f, 0.35f);
            mf.text(sel->name, SX(383), SY(488), 34.0f * A, sw, sh, 246, 246, 246, 255);
            if (sel->magnitude) {   // "STR +N" / "DEF +N" bonus line (unit depends on category)
                char b[32]; std::snprintf(b, sizeof b, "STR +%u", sel->magnitude);
                mf.text(b, SX(770), SY(488), 34.0f * A, sw, sh, 246, 246, 246, 255);
            }
            std::string d = sel->description; float dy = 552.0f; std::size_t pos = 0;
            while (pos < d.size() && dy < 676.0f) {
                std::size_t nl = d.find_first_of("\r\n", pos);
                std::string line = d.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                if (!line.empty()) mf.text(line, SX(383), SY(dy), 30.0f * A, sw, sh, 236, 236, 236, 255);
                dy += 40.0f;
                if (nl == std::string::npos) break;
                pos = d.find_first_not_of("\r\n", nl);
                if (pos == std::string::npos) break;
            }
        }
        // 6. caption bar (left) + difficulty word (right)
        const float csz = 27.0f * A;
        mf.text(e.caption, SX(48), SY(707), csz, sw, sh, 236, 236, 236, 255);
        if (e.diffWord) { float w = mf.measure(e.diffWord, csz); mf.text(e.diffWord, SX(1006) - w, SY(707), csz, sw, sh, 236, 236, 236, 255); }
        mf.draw(g, cview);
    }

    // The pause-book ITEM/Inventory page (PAUSEBOOK.md §4). Same shell + Status panel as Equip, but the
    // right side is the W_INVENT 6x5 owned-items grid (cell lattice X=441+col*75, Y=72+row*64, cell 54.4),
    // and there is no bottom detail panel (a use-prompt appears on select instead -- not modelled). `owned`
    // holds up to 30 item ids in cell order (-1 = empty). Stats reuse EquipView.
    void draw_item(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                   std::uint32_t W, std::uint32_t H, const EquipView& e,
                   const int* owned, int ownedCount,
                   const ItemTable& items, const fs::path& asset_root) {
        const float sw = float(W), sh = float(H), A = sh / 768.0f;
        const float fwN = (4.0f / 3.0f) * sh / sw;
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };
        auto SY = [&](float y) { return y / 768.0f * sh; };
        // 0. shell
        if (altpillar.tex)
            draw_screen_image(g, cview, dview, true, altpillar, -1.0f, 1.0f, AX(0), -1.0f, 0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
        if (altpillar.tex)   // right goddess strip anchored into the right margin (inner edge at AX(1024))
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f, 0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        if (frame.tex)
            draw_screen_image(g, cview, dview, altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0, 0, 1, 0.749f);
        // 1. "Inventory" plaque -- active plaque anchor X=128 (MN5), not x=30.
        if (invPlaque.tex)
            draw_screen_image(g, cview, dview, false, invPlaque, AX(128), AY(8), AX(281), AY(68),
                              0.4f, 0.0f, 0.0f, 0.598f, 0.492f);
        // 2. LEFT stats panel (identical to Equip): plate {182,60,217.6,358.4}, six rows pitch 38.4.
        if (wstatus.tex)
            draw_screen_image(g, cview, dview, false, wstatus, AX(182), AY(60), AX(399.6f), AY(418.4f),
                              0.55f, 0.0f, 0.0f, 0.420f, 0.635f);
        {
            const float psz = 30.0f * A;
            const std::string vals[6] = {
                std::to_string(e.level), std::to_string(e.hp) + "/" + std::to_string(e.maxhp),
                std::to_string(e.str), std::to_string(e.def), std::to_string(e.exp), std::to_string(e.next) };
            static const char* labs[6] = {"Lv.", "HP", "STR", "DEF", "Exp", "Next"};
            static const float ry[6] = {92, 130.4f, 168.8f, 207.2f, 245.6f, 284.0f};
            for (int i = 0; i < 6; ++i) {
                mf.text(labs[i], SX(223), SY(ry[i]), psz, sw, sh, 240, 240, 240, 255);
                float vw = mf.measure(vals[i], psz);
                mf.text(vals[i], SX(389.6f) - vw, SY(ry[i]), psz, sw, sh, 240, 240, 240, 255);
            }
        }
        // 3. RIGHT inventory grid plate (W_INVENT). Code plate: TL (403,64), size 518.4x409.6 (MN5).
        if (winvent.tex)
            draw_screen_image(g, cview, dview, false, winvent, AX(403), AY(64), AX(921.4f), AY(473.6f),
                              0.55f, 0.0f, 0.0f, 0.992f, 0.678f);
        // 4. the 6x5 cell lattice. Code (FUN_005a9d10): cell CENTRE = (473+col*75, 104+row*64); the
        //    engine draws a 51.2 SQUARE tile in EVERY cell (dark 0x303030 when empty), the owned icon
        //    (54.4) over occupied cells, a 54.4 blue highlight on the selected item, and a 64 SQUARE
        //    cursor cell. Placement is data-driven per record; CleriaCore fills sequentially (stub).
        auto icell = [](int col, int row, float& cx, float& cy) {
            cx = 473.0f + col * 75.0f; cy = 104.0f + row * 64.0f; };
        const int cursorCell = 0;   // default focus (runtime nav not modelled headless)
        for (int i = 0; i < 30; ++i) {
            const int col = i % 6, row = i / 6;
            float cx, cy; icell(col, row, cx, cy);
            const int id = (i < ownedCount) ? owned[i] : -1;
            if (square.tex) {   // per-cell stone tile; darker when empty
                const float d = (id < 0) ? 0.19f : 0.55f;   // 0x303030 empty / lighter when filled
                draw_screen_image(g, cview, dview, false, square, AX(cx - 25.6f), AY(cy - 25.6f),
                                  AX(cx + 25.6f), AY(cy + 25.6f), 0.34f, 0.0f, 0.0f, 1.0f, 1.0f,
                                  0.02f, 0.02f, 0.05f, 0.0f, false, d, d, d);
            }
            if (id >= 0) {
                if (i == cursorCell && blueHi.tex)   // selected-item blue highlight (54.4)
                    draw_screen_image(g, cview, dview, false, blueHi, AX(cx - 27.2f), AY(cy - 27.2f),
                                      AX(cx + 27.2f), AY(cy + 27.2f), 0.32f);
                const SpriteTex* t = icon(g, items, asset_root, id);
                if (t) draw_screen_image(g, cview, dview, false, *t, AX(cx - 27.2f), AY(cy - 27.2f),
                                         AX(cx + 27.2f), AY(cy + 27.2f), 0.3f);
            }
        }
        if (square.tex) {   // cursor cell: 64 SQUARE concentric with the focused cell
            const int col = cursorCell % 6, row = cursorCell / 6;
            float cx, cy; icell(col, row, cx, cy);
            draw_screen_image(g, cview, dview, false, square, AX(cx - 32.0f), AY(cy - 32.0f),
                              AX(cx + 32.0f), AY(cy + 32.0f), 0.26f);
        }
        // 5. caption + difficulty word
        const float csz = 27.0f * A;
        mf.text(e.caption, SX(48), SY(707), csz, sw, sh, 236, 236, 236, 255);
        if (e.diffWord) { float w = mf.measure(e.diffWord, csz); mf.text(e.diffWord, SX(1006) - w, SY(707), csz, sw, sh, 236, 236, 236, 255); }
        mf.draw(g, cview);
    }

    // The pause-book TOPIC category menu (PAUSEBOOK.md §4): Character / Monster / Manual, each a stacked
    // ornate button (TOPIC_BTN0/1/2, baked labels; content u0..0.69 v0..0.668), with the cyan TOPIC_CUR
    // glow behind the selected one. sel = 0..2. The category sub-viewers (image pages) are a follow-up.
    void draw_topic(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                    std::uint32_t W, std::uint32_t H, int sel) {
        const float sw = float(W), sh = float(H), A = sh / 768.0f;
        const float fwN = (4.0f / 3.0f) * sh / sw;
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };
        auto SY = [&](float y) { return y / 768.0f * sh; };
        if (altpillar.tex)
            draw_screen_image(g, cview, dview, true, altpillar, -1.0f, 1.0f, AX(0), -1.0f, 0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
        if (altpillar.tex)   // right goddess strip anchored into the right margin (inner edge at AX(1024))
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f, 0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        if (frame.tex)
            draw_screen_image(g, cview, dview, altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0, 0, 1, 0.749f);
        if (topicPlaque.tex)   // "Topic" plaque top-left
            draw_screen_image(g, cview, dview, false, topicPlaque, AX(30), AY(8), AX(176), AY(68),
                              0.4f, 0.0f, 0.0f, 0.598f, 0.492f);
        // three stacked category buttons (overlapping to form one continuous ornate frame), then the
        // cyan TOPIC_CUR selection glow washed ADDITIVELY over the chosen row -- it brightens the row
        // toward teal (as the game does) without masking the baked label under it.
        static const float cy[3] = {212, 386, 558};   // authored row centres
        const float bx0 = 256.0f, bx1 = 800.0f, bh = 208.0f;
        for (int i = 0; i < 3; ++i)
            if (topicBtn[i].tex)
                draw_screen_image(g, cview, dview, false, topicBtn[i], AX(bx0), AY(cy[i] - bh * 0.5f),
                                  AX(bx1), AY(cy[i] + bh * 0.5f), 0.42f - i * 0.01f, 0.0f, 0.0f, 0.690f, 0.668f);
        if (sel >= 0 && sel < 3 && topicCur.tex)
            draw_screen_image(g, cview, dview, false, topicCur, AX(bx0 + 20), AY(cy[sel] - bh * 0.42f),
                              AX(bx1 - 20), AY(cy[sel] + bh * 0.38f), 0.30f, 0.033f, 0.0f, 0.693f, 0.809f,
                              0.02f, 0.02f, 0.05f, 0.0f, /*additive*/true);
        mf.text("Browse monster, character or tutorial data.", SX(48), SY(707), 27.0f * A, sw, sh, 236, 236, 236, 255);
        mf.draw(g, cview);
    }

    // The Bonus Shop (SHOP_LAYOUT.md, host GoWndShop2). Two-layer backdrop: data\menu\Back01.dds
    // full-screen + the data\image\t_400a.dds Feena illustration at {-288,-116.8}/819 (left-anchored,
    // scale H/768). The framework-A go_wnddialog TEXT LIST anchors at origin {672,192}; SP counter is
    // LEFT-anchored at {608,656}; description at {48,704}. `items` are pre-formatted rows.
    void draw_shop(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                   std::uint32_t W, std::uint32_t H, const char* const* items, int count, int sel, int arenaSP) {
        const float sw = float(W), sh = float(H);
        const float S = sh / 768.0f;                     // authored -> screen px, LEFT-anchored (x=0 at left)
        auto sxp = [&](float x) { return x * S; };
        auto syp = [&](float y) { return y * S; };
        // 1. Back01 backdrop (1024x768) stretched full-screen, then the Feena key-art over it.
        if (shopBack.tex)
            draw_screen_image(g, cview, dview, true, shopBack, -1, 1, 1, -1, 0.9f, 0, 0, 1, 1);
        else
            draw_screen_image(g, cview, dview, true, dim, -1, 1, 1, -1, 0.9f, 0, 0, 1, 1, 0.03f, 0.02f, 0.05f);
        if (shopFeena.tex) {   // {-288,-116.8} size 819x819, left-anchored + H/768
            auto NX = [&](float px) { return 2.0f * px / sw - 1.0f; };
            auto NY = [&](float py) { return 1.0f - 2.0f * py / sh; };
            draw_screen_image(g, cview, dview, false, shopFeena,
                              NX(sxp(-288.0f)), NY(syp(-116.8f)), NX(sxp(-288.0f + 819.0f)), NY(syp(-116.8f + 819.0f)),
                              0.7f, 0, 0, 1, 1);
        }
        // 2. the go_wnddialog list, anchored at {672,192}. Caption at origin, rows below at pitch 44.8.
        const float pxH = 30.0f * S;
        const float ox = 672.0f, oy = 192.0f, pitchA = 44.8f;
        const float itemX = sxp(ox), hy = syp(oy);
        const float iy0 = syp(oy + 44.8f), pitch = pitchA * S;
        float maxW = mf.measure("Please select a bonus to unlock:", pxH);
        for (int i = 0; i < count; ++i) maxW = std::max(maxW, mf.measure(items[i], pxH));
        // DIALOG0 scroll frame around the caption..last row.
        const float fl = itemX - 40.0f * S, fr = itemX + maxW + 40.0f * S;
        draw_dialog_frame(g, cview, dview, fl, hy - 20.0f * S, fr, iy0 + (count - 1) * pitch + pxH + 20.0f * S, sw, sh);
        if (sel >= 0 && sel < count && curGlow.tex) {   // additive glow on the selected row
            const float mid = iy0 + sel * pitch + pxH * 0.46f, half = 16.0f * S, barW = maxW + 48.0f * S;
            draw_cursor_glow(g, cview, dview, itemX - 16.0f * S, mid - half, itemX - 16.0f * S + barW, mid + half, sw, sh);
        }
        { const char* hdr = "Please select a bonus to unlock:"; float hw = mf.measure(hdr, pxH);
          mf.text(hdr, (fl + fr) * 0.5f - hw * 0.5f, hy, pxH, sw, sh, 240, 246, 250, 255); }
        for (int i = 0; i < count; ++i) {
            const bool s = (i == sel);
            mf.text(items[i], itemX, iy0 + i * pitch, pxH, sw, sh, s ? 250 : 172, s ? 252 : 206, s ? 252 : 208, 255);
        }
        // 3. SP counter at y656. Code X is left-anchored 608, but under the (list-validated) H/768
        //    widescreen mapping that lands left of the grab, which sits it under the list -> centre it
        //    on the frame to match reference/bonusshop.png.
        { std::string sp = "Current [Arena SP]: " + std::to_string(arenaSP);
          float w = mf.measure(sp, pxH);
          mf.text(sp, (fl + fr) * 0.5f - w * 0.5f, syp(656.0f), pxH, sw, sh, 236, 236, 236, 255); }
        mf.text("You can use SP earned in Arena Mode to purchase special bonuses.",
                sxp(48.0f), syp(704.0f), 27.0f * S, sw, sh, 236, 236, 236, 255);
        mf.draw(g, cview);
    }

    // The pause-book SAVE / LOAD page (SAVELOAD.md): a file-tab row (1..8) + 8 slot bars from W_SVLD
    // (container panel u0..0.79 v0..0.556; selected bar sprite v0.571..0.625; tabs selected v0.703..0.75
    // / dim v0.762..0.806, each ~79px). CleriaCore has no save system, so every slot renders "No Data".
    // `save` = true for the Save plaque/caption, false for Load. fileTab 0..7, selSlot 0..7.
    // `labels`: optional 8 slot lines ("" = empty slot -> "No Data"); null = all empty.
    void draw_svld(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                   std::uint32_t W, std::uint32_t H, bool save, int fileTab, int selSlot,
                   const std::string* labels = nullptr) {
        const float sw = float(W), sh = float(H), A = sh / 768.0f;
        const float fwN = (4.0f / 3.0f) * sh / sw;
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };
        auto SY = [&](float y) { return y / 768.0f * sh; };
        constexpr float T = 1.0f / 1024.0f;
        if (altpillar.tex)
            draw_screen_image(g, cview, dview, true, altpillar, -1.0f, 1.0f, AX(0), -1.0f, 0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
        if (altpillar.tex)   // right goddess strip anchored into the right margin (inner edge at AX(1024))
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f, 0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        if (frame.tex)
            draw_screen_image(g, cview, dview, altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0, 0, 1, 0.749f);
        const SpriteTex& plaque = (save && savePlaque.tex) ? savePlaque : loadPlaque;
        if (plaque.tex)   // active plaque anchor X=128 (same as the other book pages)
            draw_screen_image(g, cview, dview, false, plaque, AX(128), AY(8), AX(281), AY(68),
                              0.4f, 0.0f, 0.0f, 0.598f, 0.492f);
        // File-tab row 1..8 across the top (reference/pause_load.png shows this row + slots numbered
        // No.(fileTab*8+i+1) -> tab 6 = No.41..48; the MN9 "remove the tabs" note is refuted by the
        // grab). Selected tab = bright sprite (v0.708..0.759), others dim (v0.774..0.810), stride 79px.
        for (int i = 0; i < 8; ++i) {
            if (!wsvld.tex) break;
            const float tw = 72.0f, tx = 344.0f + i * tw, ty = 100.0f;
            const float u0 = (i * 79 + 4) * T, u1 = (i * 79 + 74) * T;
            const float v0 = (i == fileTab ? 725 : 793) * T, v1 = (i == fileTab ? 777 : 829) * T;
            draw_screen_image(g, cview, dview, false, wsvld, AX(tx), AY(ty), AX(tx + tw - 4), AY(ty + 44),
                              0.35f, u0, v0, u1, v1);
        }
        // 8 slot bars -- individual W_SVLD arrow plates (the reference draws raised bars, NOT the flat
        // container grooves). Bar top Y = 130 + 72*slot (SVLD_LAYOUT.md), x[152.4,895], solid arrow
        // sprite u[0,0.714] v[0.587,0.643]; the focused slot is brightened. CleriaCore has no save
        // system, so every slot renders "No Data" (the occupied preview card is a runtime-state gap).
        const float barX0 = 152.4f, barX1 = 895.0f, barTop0 = 130.0f, barPitch = 72.0f, barH = 58.0f;
        const float tsz = 26.0f * A;
        for (int i = 0; i < 8; ++i) {
            const float yt = barTop0 + i * barPitch, cy = yt + barH * 0.5f;
            if (wsvld.tex) {
                const float wt = (i == selSlot) ? 0.28f : 0.0f;   // brighten the focused slot
                draw_screen_image(g, cview, dview, false, wsvld, AX(barX0), AY(yt), AX(barX1), AY(yt + barH),
                                  0.45f, 0.0f, 0.587f, 0.714f, 0.643f, 0.02f, 0.02f, 0.05f, wt);
            }
            char no[16]; std::snprintf(no, sizeof no, "No.%02d", fileTab * 8 + i + 1);
            mf.text(no, SX(barX0 + 26), SY(cy - 13), tsz, sw, sh, 236, 236, 236, 255);
            const bool occupied = labels && !labels[i].empty();
            const char* nd = occupied ? labels[i].c_str()
                                      : "-------------- No Data --------------";
            float w = mf.measure(nd, tsz);
            mf.text(nd, SX(560) - w * 0.5f, SY(cy - 13), tsz, sw, sh,
                    occupied ? 236 : 210, occupied ? 236 : 214, occupied ? 200 : 220, 255);
        }
        mf.text(save ? "Save the current gameplay session." : "Load a previously saved gameplay session.",
                SX(48), SY(707), tsz, sw, sh, 236, 236, 236, 255);
        mf.draw(g, cview);
    }
};

// Locate the game's Felghana font (Release/yso_ins04.dat) for MenuFont. It is loose in the game's
// release dir, not in the extracted asset pack, so check CLERIA_FONT_FILE and a few conventional
// spots; empty -> MenuFont falls back to a system serif.
static fs::path find_felghana(const fs::path& asset_root) {
    if (const char* e = std::getenv("CLERIA_FONT_FILE")) {
        fs::path p = e; std::error_code ec; if (fs::exists(p, ec)) return p;
    }
    const fs::path cands[] = {
        asset_root / "yso_ins04.dat",
        asset_root / "FONT" / "yso_ins04.dat",
        asset_root.parent_path() / "release" / "yso_ins04.dat",
        asset_root.parent_path() / "Release" / "yso_ins04.dat",
    };
    for (const auto& p : cands) { std::error_code ec; if (fs::exists(p, ec)) return p; }
    return {};
}

// Headless title render (--title out.ppm), to verify the composition off-window.
// mode: 0 main menu, 1 character-select, 2 bonus sub-list.
static void options_load(int slider[3], bool chk[4]);   // yso_win.ini loader (defined below render_window)
static int render_title(Gpu& g, const fs::path& asset_root, const fs::path& out,
                        std::uint32_t W, std::uint32_t H, int mode = 0) {
    TitleFx title;
    if (!title.init(g, asset_root)) { std::fprintf(stderr, "title: TITLE_000.DDS load failed\n"); return 1; }
    g_fog.on = false;
    write_mvp(g, Mat4::identity());   // screen space: RoomVertex positions ARE clip space
    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension = WGPUTextureDimension_2D; td.size = {W, H, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture color = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTextureView cview = wgpuTextureCreateView(color, nullptr);
    WGPUTextureView dview = make_depth(g.device, W, H);
    const std::uint32_t bpr = (W * 4 + 255) & ~255u;
    WGPUBufferDescriptor rbd = {};
    rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead; rbd.size = std::size_t(bpr) * H;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(g.device, &rbd);

    MenuFont mf;
    // Rasterise at the native device-pixel em size, GGO_GRAY4 (FONT_RASTER.md): the engine's LOGFONT
    // height is screenH*32/768 and it rasterises per-size, so match that exactly.
    // The game renders its 2D overlay at the native 1280x960 internal backbuffer, then upscales to
    // the display. So rasterise the glyph atlas at the 960-height font size (32*960/768 = 40) and let
    // the draw bilinear-upscale it to screen -- this reproduces the game's characteristic softness
    // (a full-res crisp atlas over-sharpens vs the reference). build/menu_re/title_pixeldiff.py.
    int atlasPx = int(std::lround(32.0 * 960.0 / 768.0));
    if (const char* e = std::getenv("CLERIA_ATLAS_PX")) { int v = std::atoi(e); if (v >= 8 && v <= 400) atlasPx = v; }
    mf.init(g, WGPUTextureFormat_RGBA8Unorm, find_felghana(asset_root), atlasPx);
    if (mode == 1) {
        int cs = 0;   // CLERIA_CS_SEL=0|1|2 previews Yunica/Hugo/Toal off-window
        if (const char* e = std::getenv("CLERIA_CS_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 2) cs = v; }
        float pulse = 0.72f;   // CLERIA_CS_PULSE phases the selection-bar glow (matches the ref frame)
        if (const char* e = std::getenv("CLERIA_CS_PULSE")) pulse = float(std::atof(e));
        title.draw_charselect(g, mf, cview, dview, W, H, cs, pulse, std::getenv("CLERIA_CS_CHIBI") ? std::atoi(std::getenv("CLERIA_CS_CHIBI")) : 0);
        // CLERIA_POPUP=type|diff|sp overlays a New-Game popup for off-window verification.
        if (const char* e = std::getenv("CLERIA_POPUP")) {
            static const char* ti[3] = {"Normal", "Extra", "Cancel"};
            static const char* di[6] = {"Very Easy", "Easy", "Normal", "Hard", "Nightmare", "Cancel"};
            static const char* si[2] = {"[Enable]", "[Disable]"};
            if (std::string(e) == "type") title.draw_popup(g, mf, cview, dview, W, H, "Type Select", ti, 3, 0);
            else if (std::string(e) == "diff") title.draw_popup(g, mf, cview, dview, W, H, "Difficulty Level", di, 6, 2);
            else if (std::string(e) == "sp") title.draw_popup(g, mf, cview, dview, W, H, "SP Bonus Acquisition", si, 2, 0);
        }
    } else if (mode == 2) {
        title.draw_bonus(g, mf, cview, dview, W, H, /*sel=*/0);        // Time Attack lit
    } else if (mode == 3) {
        int osel = 0;   // CLERIA_OPT_SEL = focused row 0..9
        if (const char* e = std::getenv("CLERIA_OPT_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 9) osel = v; }
        int sliders[3] = {512, 512, 512};                     // reflects yso_win.ini if present
        bool checks[4] = {false, true, false, false};
        options_load(sliders, checks);
        title.draw_options(g, mf, cview, dview, W, H, osel, sliders, checks);
    } else if (mode == 4) {
        // Pause-book Equip page preview. Stats from GAMEINFO curves; equipped items stubbed
        // (CleriaCore has no runtime equip state yet). CLERIA_CS_SEL = character, CLERIA_LEVEL = level.
        const GameInfo ginfo = GameInfo::load(asset_root / "MISC" / "GAMEINFO.DAT");
        const ItemTable items = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
        int ch = 1; if (const char* e = std::getenv("CLERIA_CS_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 2) ch = v + 1; }
        int lvl = 52; if (const char* e = std::getenv("CLERIA_LEVEL")) { int v = std::atoi(e); if (v >= 1 && v <= 60) lvl = v; }
        const int tier = 4;   // stub weapon tier for the STR figure
        TitleFx::EquipView ev;
        ev.character = ch; ev.level = lvl;
        if (ginfo.ok()) {
            ev.maxhp = ev.hp = int(std::lround(ginfo.curves[kPlayerMaxHp].at(lvl)));
            ev.str = int(std::lround(ginfo.player_atk(ch, tier, lvl)));
            ev.def = int(std::lround(ginfo.curves[kPlayerDef].at(lvl)));
            ev.exp = long(std::llround(ginfo.exp_to_level(lvl)));
            ev.next = long(std::llround(ginfo.exp_to_level(lvl + 1) - ginfo.exp_to_level(lvl)));
        }
        ev.weaponId = 1; ev.armorId = 8; ev.selId = 1;   // stub equipped set (weapon shown in the detail)
        title.draw_equip(g, mf, cview, dview, W, H, ev, items, asset_root);
    } else if (mode == 5) {
        // Pause-book Item/Inventory page preview. Stats as Equip; owned items stubbed.
        const GameInfo ginfo = GameInfo::load(asset_root / "MISC" / "GAMEINFO.DAT");
        const ItemTable items = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
        int ch = 1; if (const char* e = std::getenv("CLERIA_CS_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 2) ch = v + 1; }
        int lvl = 52; if (const char* e = std::getenv("CLERIA_LEVEL")) { int v = std::atoi(e); if (v >= 1 && v <= 60) lvl = v; }
        TitleFx::EquipView ev; ev.character = ch; ev.level = lvl; ev.caption = "Examine or use an item.";
        if (ginfo.ok()) {
            ev.maxhp = ev.hp = int(std::lround(ginfo.curves[kPlayerMaxHp].at(lvl)));
            ev.str = int(std::lround(ginfo.player_atk(ch, 4, lvl)));
            ev.def = int(std::lround(ginfo.curves[kPlayerDef].at(lvl)));
            ev.exp = long(std::llround(ginfo.exp_to_level(lvl)));
            ev.next = long(std::llround(ginfo.exp_to_level(lvl + 1) - ginfo.exp_to_level(lvl)));
        }
        const int owned[] = {1, 48, 8, 78, 79, 116};   // stub: weapon, seed, armor, 2 medallions, bracelet
        title.draw_item(g, mf, cview, dview, W, H, ev, owned, 6, items, asset_root);
    } else if (mode == 6) {
        int sel = 0; if (const char* e = std::getenv("CLERIA_TOPIC_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 2) sel = v; }
        title.draw_topic(g, mf, cview, dview, W, H, sel);
    } else if (mode == 7) {
        // Bonus Shop preview (BONUSSHOP.md item list). Unlock state stubbed.
        static const char* shopItems[] = {
            "[5,000] Arena Stage 2", "[Unlocked] Adol (Felghana Version)",
            "[Unlocked] Yunica (Extra Version)", "[Unlocked] Hugo (Extra Version)",
            "[Unlocked] Toal (Extra Version)", "[Unlocked] Adol (Napishtim Version)", "Cancel"};
        int sel = 0; if (const char* e = std::getenv("CLERIA_SHOP_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 6) sel = v; }
        title.draw_shop(g, mf, cview, dview, W, H, shopItems, 7, sel, 2811);
    } else if (mode == 8 || mode == 9) {
        title.draw_svld(g, mf, cview, dview, W, H, /*save=*/mode == 9, /*fileTab=*/5, /*selSlot=*/0);
    } else {
        int ts = 1;   // CLERIA_TITLE_SEL overrides which item is lit (0 New Game ... 4 Exit)
        if (const char* e = std::getenv("CLERIA_TITLE_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 4) ts = v; }
        title.draw(g, cview, dview, W, H);
        title.draw_menu(g, mf, cview, dview, W, H, ts);
    }
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
    const auto* px = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(readback, 0, rbd.size));
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
            f.write(reinterpret_cast<const char*>(px + std::size_t(y) * bpr + x * 4), 3);
    wgpuBufferUnmap(readback);
    std::printf("wrote %s (%ux%u)\n", out.string().c_str(), W, H);
    return 0;
}

// ---- Option-screen persistence: yso_win.ini (build/menu_re/OPTIONS.md §3) -------------------
// The game stores config in yso_win.ini via FUN_0040b8e0 (load) / FUN_0040d7e0 (save): sections
// [sound] BgmVolume/EffectVolume (0..1024 decimal strings), [graphics] GammaValue ("%0.8f" float),
// [input] PadAnalog/DashControl/AtkBtnMagic/AlwaysDashOK (0/1). We read-modify-write so the file's
// other keys (PlayerName, TitleGameDifficulty, ...) survive. Slider<->gamma per OPTIONS.md §2.
static float slider_to_gamma(int s) {   // 0..1024 -> 0.5..2.0, neutral 1.0 @512
    return s <= 512 ? float(s) / 512.0f * 0.5f + 0.5f : float(s - 512) / 512.0f + 1.0f;
}
static int gamma_to_slider(float g) {
    float s = g <= 1.0f ? (g - 0.5f) * 1024.0f : (g - 1.0f) * 512.0f + 512.0f;
    return std::clamp(int(std::lround(s)), 0, 1024);
}
static fs::path options_ini_path() {
    if (const char* e = std::getenv("CLERIA_INI")) return e;
    std::error_code ec; return fs::current_path(ec) / "yso_win.ini";
}
// A line-preserving INI editor (keeps comments / unknown keys / section order).
struct IniFile {
    std::vector<std::string> lines;
    static std::string trim(const std::string& s) {
        std::size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        std::size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
    void load(const fs::path& p) {
        std::ifstream f(p); std::string l;
        while (std::getline(f, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); lines.push_back(l); }
    }
    void save(const fs::path& p) const {
        std::ofstream f(p, std::ios::binary);
        for (const auto& l : lines) f << l << "\r\n";
    }
    bool get(const std::string& sec, const std::string& key, std::string& out) const {
        std::string cur;
        for (const auto& raw : lines) {
            std::string t = trim(raw);
            if (t.size() >= 2 && t.front() == '[' && t.back() == ']') { cur = t.substr(1, t.size() - 2); continue; }
            if (cur != sec) continue;
            std::size_t eq = t.find('=');
            if (eq != std::string::npos && trim(t.substr(0, eq)) == key) { out = trim(t.substr(eq + 1)); return true; }
        }
        return false;
    }
    void set(const std::string& sec, const std::string& key, const std::string& val) {
        int hdr = -1, bodyEnd = int(lines.size());
        for (int i = 0; i < int(lines.size()); ++i) {
            std::string t = trim(lines[i]);
            if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
                if (hdr < 0 && t.substr(1, t.size() - 2) == sec) hdr = i;
                else if (hdr >= 0) { bodyEnd = i; break; }
            }
        }
        if (hdr >= 0) {
            for (int i = hdr + 1; i < bodyEnd; ++i) {
                std::string t = trim(lines[i]);
                std::size_t eq = t.find('=');
                if (eq != std::string::npos && trim(t.substr(0, eq)) == key) { lines[i] = key + "=" + val; return; }
            }
            lines.insert(lines.begin() + bodyEnd, key + "=" + val);
        } else {
            if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
            lines.push_back("[" + sec + "]");
            lines.push_back(key + "=" + val);
        }
    }
};
// slider[0..2] = BGM / SFX / Brightness (0..1024); chk[0..3] = rows 3..6 (Analog/Dash/HoldAtk/AlwaysDash).
static void options_load(int slider[3], bool chk[4]) {
    IniFile f; f.load(options_ini_path()); std::string v;
    auto geti = [&](const char* s, const char* k, int def) { return f.get(s, k, v) ? std::atoi(v.c_str()) : def; };
    slider[0] = std::clamp(geti("sound", "BgmVolume", 512), 0, 1024);
    slider[1] = std::clamp(geti("sound", "EffectVolume", 512), 0, 1024);
    slider[2] = f.get("graphics", "GammaValue", v) ? gamma_to_slider(float(std::atof(v.c_str()))) : 512;
    chk[0] = geti("input", "PadAnalog", 0) != 0;
    chk[1] = geti("input", "DashControl", 1) != 0;
    chk[2] = geti("input", "AtkBtnMagic", 0) != 0;
    chk[3] = geti("input", "AlwaysDashOK", 0) != 0;
}
static void options_save(const int slider[3], const bool chk[4]) {
    const fs::path p = options_ini_path();
    IniFile f; f.load(p);   // preserve existing content
    f.set("sound", "BgmVolume", std::to_string(slider[0]));
    f.set("sound", "EffectVolume", std::to_string(slider[1]));
    char g[32]; std::snprintf(g, sizeof g, "%0.8f", slider_to_gamma(slider[2]));
    f.set("graphics", "GammaValue", g);
    f.set("input", "PadAnalog", chk[0] ? "1" : "0");
    f.set("input", "DashControl", chk[1] ? "1" : "0");
    f.set("input", "AtkBtnMagic", chk[2] ? "1" : "0");
    f.set("input", "AlwaysDashOK", chk[3] ? "1" : "0");
    f.save(p);
}

