// cleria-view — the pause book's draw-list executor (build/menu_re/PAUSEBOOK_DRAW.md §0.2-§0.4,
// §2.1 pillars, §7 text). src/cleria/pausebook emits authored 1024x768 commands in the engine's
// order; this turns each into screen quads: the texture times the diffuse PLUS the specular
// (the D3D9 specular add the book's flash/pulse rides on), opaque for 0x20001.
// #included by main.cpp after view_menus.inl; ONE translation unit.

struct BookVertex { float x, y, u, v; std::uint8_t d[4]; std::uint8_t s[4]; };

static const char* kBookWGSL = R"WGSL(
struct VsOut { @builtin(position) pos : vec4<f32>, @location(0) uv : vec2<f32>,
               @location(1) dif : vec4<f32>, @location(2) spec : vec4<f32> };
@group(0) @binding(0) var tex : texture_2d<f32>;
@group(0) @binding(1) var smp : sampler;
@vertex fn vs(@location(0) p : vec2<f32>, @location(1) uv : vec2<f32>,
              @location(2) dif : vec4<f32>, @location(3) spec : vec4<f32>) -> VsOut {
  var o : VsOut; o.pos = vec4<f32>(p, 0.0, 1.0); o.uv = uv; o.dif = dif; o.spec = spec; return o;
}
@fragment fn fs(i : VsOut) -> @location(0) vec4<f32> {
  var c = textureSample(tex, smp, i.uv) * i.dif + vec4<f32>(i.spec.rgb, 0.0);
  if (i.spec.a > 0.5) { c.a = 1.0; }          // render state 0x20001: no blending
  return min(c, vec4<f32>(1.0));
}
)WGSL";

struct BookRenderer {
    WGPURenderPipeline pipeline{};
    WGPUSampler clamp{};                // render states 0x24041 / 0x20001: address mode CLAMP (§0.3)
    WGPUBuffer vbuf{};
    std::size_t vbuf_cap = 0;
    std::unordered_map<std::string, fs::path> menu_files;    // lower-case basename -> path
    std::unordered_map<int, SpriteTex> menu_tex, icon_tex, icon2_tex;
    std::unordered_map<std::string, SpriteTex> named_tex;     // Topic portraits, manual pages
    std::unordered_map<std::string, fs::path> image_files;    // IMAGE/, lower-case basename
    std::unordered_map<int, WGPUBindGroup> wrap_bind;         // menu texture -> WRAP sampler
    SpriteTex f_middle, f_mini;
    MenuFont gdi;                       // Felghana at a 32-px cell (lfHeight +32)
    WGPUBindGroup gdi_bind{};
    fs::path root;
    const ItemTable* items = nullptr;
    bool ok = false;

    // The engine's menu texture table 0x68ce58 (71 entries; data\menu\...).
    static const char* menu_name(int i) {
        static const char* k[71] = {
            "Back00", "Equip", "Inventory", "Load", "Save", "LoadStart", "Option", "Quit", "Cur00",
            "w_Equip", "SQUARE", "w_Status", "w_Invent", "w_AskUse", "w_SvLd", "w_AskLoad", "w_AskSave",
            "w_AskDel", "w_Option", "OpBtn", "w_MsgBox", "hint1", "hint2", "MAP_POINT", "OptionStart",
            "Topic", "Manual", "Topic_Btn0", "Topic_Btn1", "Topic_Btn2", "Topic_Cur", "w_fs_msg",
            "option_r", "option_l", "MAP", "Svld_Chricon", "DC_000", "DC_001", "DC_002", "DC_003",
            "DC_004", "DC_010", "DC_011", "DC_012", "DC_999", "ADOL_000", "tab", "Cur01",
            "AltPillar16-9", "AltPillar16-10", "MapPillar16-9", "MapPillar16-10", "ShopPillar16-9",
            "ShopPillar16-10", "TitlePillar16-9", "TitlePillar16-10", "HugoPillar16-9",
            "HugoPillar16-10", "w_Status", "Cur01", "w_AskEmPr", "NotEnough", "w_ShopTop",
            "w_ShopInv", "w_AskSell", "BuyList", "w_AskBuy", "w_fs_msg", "w_RavalEquip", "SQUARE",
            "w_AskUse02"};
        return i >= 0 && i < 71 ? k[i] : nullptr;
    }
    static std::string lower(std::string s) {
        for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    WGPUBindGroup clamp_bind(Gpu& g, WGPUTextureView view) const {
        WGPUBindGroupEntry e[2] = {};
        e[0].binding = 0; e[0].textureView = view;
        e[1].binding = 1; e[1].sampler = clamp;
        WGPUBindGroupDescriptor d = {};
        d.layout = g.tex_layout; d.entryCount = 2; d.entries = e;
        return wgpuDeviceCreateBindGroup(g.device, &d);
    }
    SpriteTex load(Gpu& g, const fs::path& p) const {
        if (p.empty()) return {};
        Dds d = load_dds(p);
        if (!d.ok()) return {};
        SpriteTex t = upload_rgba(g, d.width, d.height, d.data.data());
        if (t.bind) wgpuBindGroupRelease(t.bind);
        t.bind = clamp_bind(g, t.view);
        return t;
    }

    bool init(Gpu& g, WGPUTextureFormat color, const fs::path& asset_root, const ItemTable& tbl) {
        root = asset_root; items = &tbl;
        WGPUSamplerDescriptor sad = {};
        sad.magFilter = sad.minFilter = WGPUFilterMode_Linear; sad.maxAnisotropy = 1;
        sad.addressModeU = sad.addressModeV = sad.addressModeW = WGPUAddressMode_ClampToEdge;
        clamp = wgpuDeviceCreateSampler(g.device, &sad);
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(asset_root / "MENU", ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file()) menu_files.emplace(lower(it->path().filename().string()), it->path());
        for (auto it = fs::directory_iterator(asset_root / "IMAGE", ec);
             !ec && it != fs::directory_iterator(); it.increment(ec))
            if (it->is_regular_file()) image_files.emplace(lower(it->path().filename().string()), it->path());
        f_middle = load(g, asset_root / "IMAGE" / "F_MIDDLE.DDS");
        f_mini = load(g, asset_root / "IMAGE" / "F_MINI.DDS");
        // The GDI font: lfHeight +32 is the CELL height (tmHeight); MenuFont takes an em size, so
        // rasterise once, then re-rasterise at the em that makes tmHeight 32.
        if (gdi.init(g, color, find_felghana(asset_root), 32) && gdi.lineH > 0 && gdi.lineH != 32)
            gdi.build_atlas(g, int(std::lround(32.0 * 32.0 / gdi.lineH)));
        gdi_bind = gdi.atlas_view ? clamp_bind(g, gdi.atlas_view) : nullptr;

        WGPUShaderSourceWGSL wgsl = {};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL; wgsl.code = sv(kBookWGSL);
        WGPUShaderModuleDescriptor smd = {}; smd.nextInChain = &wgsl.chain;
        WGPUShaderModule sm = wgpuDeviceCreateShaderModule(g.device, &smd);
        WGPUVertexAttribute at[4] = {};
        at[0].format = WGPUVertexFormat_Float32x2; at[0].offset = 0;  at[0].shaderLocation = 0;
        at[1].format = WGPUVertexFormat_Float32x2; at[1].offset = 8;  at[1].shaderLocation = 1;
        at[2].format = WGPUVertexFormat_Unorm8x4;  at[2].offset = 16; at[2].shaderLocation = 2;
        at[3].format = WGPUVertexFormat_Unorm8x4;  at[3].offset = 20; at[3].shaderLocation = 3;
        WGPUVertexBufferLayout vbl = {};
        vbl.arrayStride = sizeof(BookVertex); vbl.attributeCount = 4; vbl.attributes = at;
        WGPUBlendState blend = {};
        blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
        blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
        WGPUColorTargetState tgt = {}; tgt.format = color; tgt.blend = &blend; tgt.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fsd = {}; fsd.module = sm; fsd.entryPoint = sv("fs"); fsd.targetCount = 1; fsd.targets = &tgt;
        WGPUPipelineLayoutDescriptor pld = {};
        pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &g.tex_layout;
        WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(g.device, &pld);
        WGPURenderPipelineDescriptor pd = {};
        pd.layout = pl;
        pd.vertex.module = sm; pd.vertex.entryPoint = sv("vs");
        pd.vertex.bufferCount = 1; pd.vertex.buffers = &vbl;
        pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFF; pd.fragment = &fsd;
        pipeline = wgpuDeviceCreateRenderPipeline(g.device, &pd);
        wgpuPipelineLayoutRelease(pl);
        wgpuShaderModuleRelease(sm);
        ok = pipeline && f_middle.bind && f_mini.bind && gdi_bind;
        return ok;
    }

    WGPUBindGroup bind_of(Gpu& g, const TexRef& t) {
        auto fetch = [&](std::unordered_map<int, SpriteTex>& cache, int id, auto&& path) -> WGPUBindGroup {
            auto it = cache.find(id);
            if (it == cache.end()) it = cache.emplace(id, load(g, path())).first;
            return it->second.bind;
        };
        switch (t.kind) {
            case TexRef::Menu:
                return fetch(menu_tex, t.id, [&]() -> fs::path {
                    const char* n = menu_name(t.id);
                    if (!n) return {};
                    auto f = menu_files.find(lower(std::string(n) + ".dds"));
                    return f == menu_files.end() ? fs::path{} : f->second;
                });
            case TexRef::Icon: return fetch(icon_tex, t.id, [&] { return items->icon_path(root, t.id, false); });
            case TexRef::Icon2: return fetch(icon2_tex, t.id, [&] { return items->icon_path(root, t.id, true); });
            case TexRef::Named: {
                if (!t.name) return nullptr;
                const std::string key = lower(t.name);
                auto it = named_tex.find(key);
                if (it == named_tex.end()) {
                    auto f = menu_files.find(key);
                    if (f == menu_files.end()) f = image_files.find(key);
                    it = named_tex.emplace(key, f == image_files.end() ? SpriteTex{} : load(g, f->second)).first;
                }
                return it->second.bind;
            }
            default: return nullptr;
        }
    }

    // ---- building the vertex stream -------------------------------------------------------
    struct Batch { WGPUBindGroup bind; std::uint32_t first, count; };
    std::vector<BookVertex> verts;
    std::vector<Batch> batches;
    float W = 0, H = 0, W43 = 0;
    bool pillarbox = false;

    // §0.4: authored -> pixels. x of an anchored sprite maps into the pillarboxed 4:3 field.
    float px_x(float x, int anchor) const {
        if (anchor == 0x23D) return x;
        if (pillarbox && anchor != 0) {
            const float off = anchor == 1 ? 0.0f : anchor == 2 ? (W - W43) / 2 : W - W43;
            return std::trunc(x / 1024.0f * W43 + off);
        }
        return std::floor(x * W / 1024.0f);
    }
    float px_y(float y, int anchor) const {
        if (anchor == 0x23D) return y;
        return std::floor(y * H / 768.0f);
    }
    void quad(WGPUBindGroup bind, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
              std::uint32_t dif, std::uint32_t spec, bool opaque) {
        if (!bind) return;
        auto NX = [&](float v) { return v / W * 2.f - 1.f; };
        auto NY = [&](float v) { return 1.f - v / H * 2.f; };
        BookVertex b{};
        b.d[0] = std::uint8_t(dif >> 16); b.d[1] = std::uint8_t(dif >> 8); b.d[2] = std::uint8_t(dif); b.d[3] = std::uint8_t(dif >> 24);
        b.s[0] = std::uint8_t(spec >> 16); b.s[1] = std::uint8_t(spec >> 8); b.s[2] = std::uint8_t(spec); b.s[3] = opaque ? 255 : 0;
        BookVertex q[4] = {b, b, b, b};
        q[0].x = NX(x0); q[0].y = NY(y0); q[0].u = u0; q[0].v = v0;
        q[1].x = NX(x1); q[1].y = NY(y0); q[1].u = u1; q[1].v = v0;
        q[2].x = NX(x1); q[2].y = NY(y1); q[2].u = u1; q[2].v = v1;
        q[3].x = NX(x0); q[3].y = NY(y1); q[3].u = u0; q[3].v = v1;
        if (batches.empty() || batches.back().bind != bind)
            batches.push_back({bind, std::uint32_t(verts.size()), 0});
        for (int i : {0, 1, 2, 0, 2, 3}) verts.push_back(q[i]);
        batches.back().count += 6;
    }
    // An authored rect through the transform (both corners).
    void aquad(WGPUBindGroup bind, float x, float y, float w, float h, int ax, int ay, float u0, float v0,
               float u1, float v1, std::uint32_t dif, std::uint32_t spec, bool opaque) {
        quad(bind, px_x(x, ax), px_y(y, ay), px_x(x + w, ax), px_y(y + h, ay), u0, v0, u1, v1, dif, spec, opaque);
    }

    // FUN_005acf90: an indexed triangle list with per-vertex colour, WRAP-sampled. x is scaled
    // by min(W,H) over 768 (H < W) or 1024 with no pillarbox offset; y takes the usual H/768.
    void mesh(Gpu& g, const DrawCmd& c) {
        if (c.tex.kind != TexRef::Menu) return;
        auto it = wrap_bind.find(c.tex.id);
        if (it == wrap_bind.end()) {
            bind_of(g, c.tex);
            auto t = menu_tex.find(c.tex.id);
            const WGPUBindGroup b = (t != menu_tex.end() && t->second.view) ? tex_bindgroup(g, t->second.view) : nullptr;
            it = wrap_bind.emplace(c.tex.id, b).first;
        }
        if (!it->second) return;
        const float mn = std::min(W, H), ref = H < W ? 768.0f : 1024.0f;
        auto NX = [&](float v) { return v / W * 2.f - 1.f; };
        auto NY = [&](float v) { return 1.f - v / H * 2.f; };
        if (batches.empty() || batches.back().bind != it->second)
            batches.push_back({it->second, std::uint32_t(verts.size()), 0});
        for (std::uint16_t ix : c.indices) {
            const DrawCmd::Vert& v = c.verts[ix];
            BookVertex b{};
            b.x = NX(std::trunc(v.x / ref * mn));
            b.y = NY(std::floor(v.y * H / 768.0f));
            b.u = v.u; b.v = v.v;
            b.d[0] = std::uint8_t(v.argb >> 16); b.d[1] = std::uint8_t(v.argb >> 8);
            b.d[2] = std::uint8_t(v.argb); b.d[3] = std::uint8_t(v.argb >> 24);
            verts.push_back(b);
            ++batches.back().count;
        }
    }

    void text_bitmap(Gpu&, const DrawCmd& c, bool middle) {
        const WGPUBindGroup bind = middle ? f_middle.bind : f_mini.bind;
        float x = c.x, y = c.y;
        for (unsigned char ch : c.text) {
            if (ch == '\n') { x = c.x; y += middle ? 26.0f : 14.0f; continue; }
            if (ch == ' ' || ch == '\t') { x += middle ? 13.0f : 9.0f; continue; }
            Glyph gph;
            if (!(middle ? glyph_middle(ch, &gph) : glyph_mini(ch, &gph))) continue;
            aquad(bind, x, y, gph.w, gph.h, c.anchor_x, c.anchor_y, gph.u0, gph.v0, gph.u1, gph.v1, c.diffuse, 0, false);
            x += gph.advance;
        }
    }

    // §7.3: GDI text, rasterised at the 32-px cell (raster px = authored units), a (-1,+1) black
    // shadow first, then the text at (x, y+1).
    void text_gdi(const DrawCmd& c) {
        const float line = float(gdi.lineH) + c.line_extra;
        auto run = [&](float ox, float oy, std::uint32_t col) {
            const float k = c.scale;
            float x = c.x + ox, y = c.y + 1.0f + oy;
            for (unsigned char ch : c.text) {
                if (ch == '\r') continue;
                if (ch == '\n') { x = c.x + ox; y += line * k; continue; }
                if (ch < 32 || ch > 126) continue;
                const MenuFont::Glyph& gp = gdi.gl[ch - 32];
                if (gp.w > 0 && gp.h > 0) {
                    // "<scale:k>" scales about the baseline (the reference's area lines sit
                    // (1 - k) * ascent lower than a top-anchored scale would put them) [DATA].
                    const float gx = x + float(gp.bx) * k, gy = y + float(gdi.ascent) - float(gp.by) * k;
                    aquad(gdi_bind, gx, gy, float(gp.w) * k, float(gp.h) * k, c.anchor_x, c.anchor_y,
                          gp.u0, gp.v0, gp.u1, gp.v1, col, 0, false);
                }
                x += float(gp.adv) * k;
            }
        };
        run(-1.0f, 1.0f, c.diffuse & 0xFF000000u);
        run(0.0f, 0.0f, c.diffuse);
    }

    // Execute `list` over `view` (loadOp Load). The side pillars (§2.1, raw pixels, opaque)
    // come first, as FUN_005a73c0 draws them.
    void render(Gpu& g, WGPUTextureView view, std::uint32_t sw, std::uint32_t sh, const DrawList& list) {
        W = float(sw); H = float(sh);
        W43 = float((4 * sh) / 3);
        pillarbox = W > W43 + 0.5f;
        verts.clear(); batches.clear();
        for (const DrawCmd& c : list) {
            switch (c.kind) {
                case DrawCmd::Quad:
                    aquad(bind_of(g, c.tex), c.x, c.y, c.w, c.h, c.anchor_x, c.anchor_y, c.u0, c.v0, c.u1, c.v1,
                          c.diffuse, c.specular, (c.flags & 0xFFFF) == 0x0001);
                    break;
                case DrawCmd::TextMiddle: text_bitmap(g, c, true); break;
                case DrawCmd::TextMini: text_bitmap(g, c, false); break;
                case DrawCmd::TextGdi: text_gdi(c); break;
                case DrawCmd::Pillars: {
                    // §2.1: drawn only when A != 1; 16:9 art from A >= 1.21, else 16:10 (tex id + 1).
                    if (!pillarbox) break;
                    const bool wide = W / W43 >= 1.21f;
                    const WGPUBindGroup pb = bind_of(g, {TexRef::Menu, wide ? c.tex.id : c.tex.id + 1});
                    const float Lx = (W - W43) / 2, R = Lx + W43;
                    const float ul = wide ? 0.16796875f : 0.1015625f, ur = wide ? 0.83203125f : 0.8984375f;
                    quad(pb, 0, 0, Lx, H, 0, 0, ul, 0.75f, c.diffuse, c.specular, true);
                    quad(pb, R, 0, W, H, ur, 0, 1, 0.75f, c.diffuse, c.specular, true);
                    break;
                }
                case DrawCmd::Mesh: mesh(g, c); break;
            }
        }
        if (verts.empty()) return;
        const std::size_t bytes = verts.size() * sizeof(BookVertex);
        if (bytes > vbuf_cap) {
            if (vbuf) wgpuBufferRelease(vbuf);
            WGPUBufferDescriptor bd = {};
            bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            bd.size = ((bytes + 65535) / 65536) * 65536;
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
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, 0, bytes);
        for (const Batch& b : batches) {
            wgpuRenderPassEncoderSetBindGroup(pass, 0, b.bind, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, b.count, 1, b.first, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(g.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    }
};

// The header scanner FUN_00430140 on one slot file: 0 = missing, 1 = a card, 2 = unreadable
// (PAUSEBOOK_DRAW_SVLD_OPT.md §1.2).
static SlotCard scan_slot(const fs::path& file) {
    SlotCard c;
    std::error_code ec;
    if (!fs::exists(file, ec)) return c;
    const std::vector<std::uint8_t> raw = read_bytes(file);
    const auto r = SaveRecord::decode(raw);
    if (!r) { c.status = 2; return c; }
    const SaveMeta meta = meta_of(*r);
    c.status = 1;
    c.level = int(r->hdr_u32(0x34));
    c.difficulty = meta.difficulty;
    c.easy = r->hdr_u32(0x58) != 0;
    c.very_easy = r->hdr_u32(0x5c) != 0;
    c.inferno = r->hdr_u32(0x60) != 0;
    c.play_frames = r->hdr_u32(0x64);
    c.character = meta.character;
    c.date = meta.date; c.time = meta.time; c.area = meta.area;
    return c;
}

// The Equip preview (FUN_005b1680 -> FUN_00420880): the candidate written into its slot on a
// copy of the state, the stats recomputed.
static PlayerStats book_preview(const std::int32_t* flags, const InvState& inv, const GameInfo& gi,
                                const ItemTable& items, int character, int level, int id) {
    std::vector<std::int32_t> f(flags, flags + 0x200);
    InvState v = inv;
    const auto& grid = equip_grid(character);
    for (int cell = 0; cell < 22; ++cell) {
        if (grid[std::size_t(cell)] != id) continue;
        if (cell == 0) f[151] = id;
        else if (cell <= 6) f[152] = id;
        else if (cell <= 12) f[153] = id;
        else if (cell <= 18) v.accessory = id;
        else v.magic = id;
        break;
    }
    return compute_stats(f.data(), v, gi, items, character, level);
}

// Headless: cleria-view <root> x --book out.ppm [W H]. CLERIA_BOOK_SAVE = a yso_*.bin to show
// (else a new game; its folder is also the Load list's); CLERIA_BOOK_KEYS = a key script, one
// char per press with 12 idle frames between (l r u d = directions, c = confirm, x = cancel,
// . = 12 more idle frames); CLERIA_BOOK_CTX = 1 opens the Save list.
static int render_book(Gpu& g, const fs::path& asset_root, const fs::path& out, std::uint32_t W, std::uint32_t H) {
    const GameInfo gi = GameInfo::load(asset_root / "MISC" / "GAMEINFO.DAT");
    const ItemTable items = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
    SaveState s;
    InvState inv;
    inv_new_game(inv);
    s.flags[150] = 1; s.flags[151] = 0; s.flags[0] = 1; s.level = 1;
    if (const char* e = std::getenv("CLERIA_BOOK_SAVE")) {
        const std::vector<std::uint8_t> raw = read_bytes(e);
        if (auto r = SaveRecord::decode(raw)) {
            s = from_record(*r);
            inv.accessory = s.accessory; inv.held = s.held; inv.magic = s.magic;
            for (int i = 0; i < 4; ++i) inv.tier[i] = s.tier[i];
            for (int i = 0; i < 54; ++i) inv.raval[i] = s.item_arr[i];
            inv.hp_bonus = s.hp_bonus; inv.atk_buff = s.atk_buff; inv.def_buff = s.def_buff;
            inv.luck_base = s.luck_base; inv.sp = s.sp;
        } else {
            std::fprintf(stderr, "book: %s is not a save\n", e);
            return 1;
        }
    }
    inv_sanitize(s.flags, inv);
    const int ch = s.flags[150];
    const PlayerStats st = compute_stats(s.flags, inv, gi, items, ch, s.level);
    BookModel m;
    m.flags = s.flags; m.inv = &inv; m.items = &items; m.gi = &gi; m.character = ch;
    m.level = s.level; m.max_hp = st.max_hp; m.hp = s.hp_cur > 0 ? std::min(s.hp_cur, st.max_hp) : st.max_hp;
    m.atk = st.atk; m.def = st.def; m.exp = s.exp;
    m.next_exp = float(gi.exp_to_level(s.level + 1));
    m.preview = [&](int id) { return book_preview(s.flags, inv, gi, items, ch, s.level, id); };
    fs::path save_dir = "saves";
    if (const char* e = std::getenv("CLERIA_BOOK_SAVE")) save_dir = fs::path(e).parent_path();
    m.scan = [&](int slot) {
        char n[16]; std::snprintf(n, sizeof n, "yso_%02d.bin", slot);
        return scan_slot(save_dir / n);
    };
    int save_no = 0;
    m.save_file_no = &save_no;
    int sliders[3] = {512, 512, 512};
    bool checks[4] = {false, true, false, false};
    options_load(sliders, checks);
    m.sliders = sliders; m.checks = checks;
    // The Topic page and the map. The collection (seen / kills) is not in the save record;
    // CLERIA_BOOK_TOPIC_ALL=1 marks every entry seen with 50 kills for a visual check.
    const CastTable casts = CastTable::load(asset_root / "MISC" / "CASTINFO.DAT");
    const EnemyTable enemies = EnemyTable::load(asset_root / "MISC" / "ENEMYINFO.DAT");
    std::vector<std::uint8_t> seen(256, 0);
    std::vector<std::int32_t> kills(256, 0);
    if (std::getenv("CLERIA_BOOK_TOPIC_ALL")) { std::fill(seen.begin(), seen.end(), 1); std::fill(kills.begin(), kills.end(), 50); }
    m.casts = &casts; m.enemies = &enemies;
    m.topic_seen = seen.data(); m.topic_kills = kills.data();
    m.manual_on = s.manual_on;
    m.warp_on = s.warp_on;

    PauseBook book;
    book.open(m, std::getenv("CLERIA_BOOK_CTX") ? std::atoi(std::getenv("CLERIA_BOOK_CTX")) : 0);
    BookEvents ev;
    auto run = [&](BookInput in, int frames) {
        for (int i = 0; i < frames; ++i) { ev = {}; book.update(i == 0 ? in : BookInput{}, m, ev); }
    };
    run({}, 12);
    if (const char* k = std::getenv("CLERIA_BOOK_KEYS"))
        for (const char* p = k; *p; ++p) {
            BookInput in;
            switch (*p) {
                case 'l': in.hstep = -1; break;
                case 'r': in.hstep = +1; break;
                case 'u': in.vstep = -1; break;
                case 'd': in.vstep = +1; break;
                case 'c': in.confirm = true; break;
                case 'x': in.cancel = true; break;
                default: break;
            }
            run(in, 12);
        }
    run({}, 48);

    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension = WGPUTextureDimension_2D; td.size = {W, H, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture color = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTextureView cview = wgpuTextureCreateView(color, nullptr);
    {   // clear to black
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPURenderPassColorAttachment ca = {};
        ca.view = cview; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store; ca.clearValue = {0, 0, 0, 1};
        WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(g.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    }
    BookRenderer br;
    if (!br.init(g, WGPUTextureFormat_RGBA8Unorm, asset_root, items))
        std::fprintf(stderr, "book: some textures/fonts missing\n");
    DrawList dl;
    book.draw(dl, m);
    br.render(g, cview, W, H, dl);
    std::printf("book: page %d subId %d, %zu draw commands\n", book.page(), book.sub_id(), dl.size());

    const std::uint32_t bpr = (W * 4 + 255) & ~255u;
    WGPUBufferDescriptor rbd = {};
    rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead; rbd.size = std::size_t(bpr) * H;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(g.device, &rbd);
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
