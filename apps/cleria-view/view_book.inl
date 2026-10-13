// cleria-view — the pause book's draw-list executor (build/menu_re/PAUSEBOOK_DRAW.md §0.2-§0.4,
// §2.1 pillars, §7 text). src/cleria/pausebook emits authored 1024x768 commands in the engine's
// order; this turns each into screen quads: the texture times the diffuse PLUS the specular
// (the D3D9 specular add the book's flash/pulse rides on), opaque for 0x20001.
// #included by main.cpp after view_menus.inl; ONE translation unit.
#include "cleria/json.hpp"
#include "cleria/sidecar.hpp"

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
    WGPURenderPipeline pipeline_add{};  // render state 0x40c1: additive (SRCALPHA / ONE)
    bool add_mode = false;
    // The message box's face (TexRef::Face): f_(200 + 10*face), frame; the host owns them.
    std::function<WGPUBindGroup(int face, int frame)> face_bind;
    WGPUSampler clamp{};                // render states 0x24041 / 0x20001: address mode CLAMP (§0.3)
    WGPUBuffer vbuf{};
    std::size_t vbuf_cap = 0;
    std::unordered_map<std::string, fs::path> menu_files;    // lower-case basename -> path
    std::unordered_map<int, SpriteTex> menu_tex, icon_tex, icon2_tex;
    std::unordered_map<std::string, SpriteTex> named_tex;     // Topic portraits, manual pages
    std::unordered_map<std::string, fs::path> image_files;    // IMAGE/, lower-case basename
    std::unordered_map<int, WGPUBindGroup> wrap_bind;         // menu texture -> WRAP sampler
    SpriteTex f_middle, f_mini, white;
    MenuFont gdi;                       // Felghana at lfHeight H x 32 / 768 of the frame it last drew for (font_for)
    WGPUBindGroup gdi_bind{};
    ExtFont ext;                        // IMAGE\EXTFONT32.BMP: <ext> / <roo> glyphs (extfont.hpp)
    SpriteTex ext_tex;                  // its cells, white with the coverage as alpha
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
        {
            const std::uint8_t px[16] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
            white = upload_rgba(g, 2, 2, px);
            if (white.bind) wgpuBindGroupRelease(white.bind);
            white.bind = clamp_bind(g, white.view);
        }
        // The GDI font, here at the 768-line size; font_for re-rasterises it for the frame's height.
        gdi.init(g, color, find_felghana(asset_root), 32);
        gdi_bind = gdi.atlas_view ? clamp_bind(g, gdi.atlas_view) : nullptr;
        ext = ExtFont::load(asset_root / "IMAGE" / "EXTFONT32.BMP");
        if (ext.ok()) {
            // The raster (FUN_005ef7f0) turns a texel's coverage v into the run colour's alpha
            // through a 16-entry ramp v x a / 15; [H] v = 16 (full cover, past the ramp) as 15.
            const int n = ext.cell;
            std::vector<std::uint8_t> px(std::size_t(n) * std::size_t(n) * ExtFont::kCells * 4, 255);
            for (std::size_t k = 0; k < ext.cov.size(); ++k)
                px[k * 4 + 3] = std::uint8_t(std::min(15, int(ext.cov[k])) * 17);
            ext_tex = upload_rgba(g, std::uint32_t(n), std::uint32_t(n * ExtFont::kCells), px.data());
            if (ext_tex.bind) wgpuBindGroupRelease(ext_tex.bind);
            ext_tex.bind = clamp_bind(g, ext_tex.view);
        }

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
        WGPUBlendState add = {};
        add.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_One};
        add.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
        tgt.blend = &add;
        pipeline_add = wgpuDeviceCreateRenderPipeline(g.device, &pd);
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
            case TexRef::Face: return face_bind ? face_bind(t.id, t.aux) : nullptr;
            case TexRef::None: return white.bind;              // an untextured quad: the diffuse
            default: return nullptr;
        }
    }

    // A named texture's size (the busts are drawn 1:1); false when the file is missing.
    bool named_size(Gpu& g, const std::string& name, int* w, int* h) {
        bind_of(g, {TexRef::Named, 0, name.c_str()});
        auto it = named_tex.find(lower(name));
        if (it == named_tex.end() || !it->second.tex) return false;
        *w = int(it->second.w); *h = int(it->second.h);
        return true;
    }

    // Retail makes the window font at lfHeight +(H x 32 / 768) px (integer division) and draws its raster 1:1
    // [EXPORT 00442260.c:270-273, 334; WINDOW_FONT.md §1]; for Felghana +N is the em N. Retail keeps the
    // start-up font when the resolution changes in a session; this one follows the frame.
    void font_for(Gpu& g, std::uint32_t sh) {
        const int n = std::max(8, int(sh) * 32 / 768);
        if (n == gdi.base || !gdi.pipeline) return;
        gdi.build_atlas(g, n);
        if (gdi_bind) wgpuBindGroupRelease(gdi_bind);
        gdi_bind = gdi.atlas_view ? clamp_bind(g, gdi.atlas_view) : nullptr;
    }

    // The GDI font's metrics for the window text layout (richtext.hpp), in the pixels of a sw x sh frame.
    RichMetrics metrics(Gpu& g, std::uint32_t sw, std::uint32_t sh) {
        font_for(g, sh);
        RichMetrics m;
        m.screen_w = int(sw); m.screen_h = int(sh);
        m.height = float(gdi.lineH);
        m.ascent = float(gdi.ascent);
        m.space = float(gdi.gl[0].adv);
        const MenuFont* f = &gdi;
        m.advance = [f](unsigned char c) { return (c >= 32 && c <= 126) ? float(f->gl[c - 32].adv) : 16.0f; };
        if (ext.ok()) {
            const ExtFont* e = &ext;
            m.ext_advance = [e](int i) { return e->advance(i); };
        }
        m.english_roonic = settings().s.kernel.english_roonic != 0;
        return m;
    }

    // ---- building the vertex stream -------------------------------------------------------
    struct Batch { WGPUBindGroup bind; std::uint32_t first, count; bool add; };
    std::vector<BookVertex> verts;
    std::vector<Batch> batches;
    float W = 0, H = 0, W43 = 0;
    bool pillarbox = false;

    // §0.4: authored -> pixels. x of an anchored sprite maps into the pillarboxed 4:3 field. This frame's
    // pixel p spans [p, p + 1] where retail's (D3D9, centres on integers) spans [p - 0.5, p + 0.5]: the
    // transform's "floor(v x s) - 0.5" is floor(v x s) here, and a raw-pixel sprite (0x23D) takes + 0.5.
    float px_x(float x, int anchor) const {
        if (anchor == 0x23D) return x + 0.5f;
        if (pillarbox && anchor != 0) {
            const float off = anchor == 1 ? 0.0f : anchor == 2 ? (W - W43) / 2 : W - W43;
            return std::trunc(x / 1024.0f * W43 + off);
        }
        return std::floor(x * W / 1024.0f);
    }
    float px_y(float y, int anchor) const {
        if (anchor == 0x23D) return y + 0.5f;
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
        if (batches.empty() || batches.back().bind != bind || batches.back().add != add_mode)
            batches.push_back({bind, std::uint32_t(verts.size()), 0, add_mode});
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
        if (batches.empty() || batches.back().bind != it->second || batches.back().add)
            batches.push_back({it->second, std::uint32_t(verts.size()), 0, false});
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
        for (std::size_t k = 0; k < c.text.size(); ++k) {
            const unsigned char ch = static_cast<unsigned char>(c.text[k]);
            if (middle && ch == '#') { const std::size_t k0 = k; x += middle_shift(c.text, k); if (k != k0) continue; }
            if (ch == '\n') { x = c.x; y += middle ? 26.0f : 14.0f; continue; }
            if (ch == ' ' || ch == '\t') { x += middle ? 13.0f : 9.0f; continue; }
            Glyph gph;
            if (!(middle ? glyph_middle(ch, &gph) : glyph_mini(ch, &gph))) continue;
            aquad(bind, x, y, gph.w, gph.h, c.anchor_x, c.anchor_y, gph.u0, gph.v0, gph.u1, gph.v1, c.diffuse, 0, false);
            x += gph.advance;
        }
    }

    // F_GEO20 (FUN_00453c10 with the font object 0x731460; popup.hpp geo20_glyph): quads adv x 23, the pen moves
    // adv - 2, a space 12 — all times the routine's scale.
    void text_geo(Gpu& g, const DrawCmd& c) {
        const WGPUBindGroup bind = bind_of(g, {TexRef::Named, 0, "F_GEO20.DDS"});
        float x = c.x;
        for (unsigned char ch : c.text) {
            GeoGlyph gg;
            if (!geo20_glyph(ch, &gg)) { if (ch == ' ' || ch == '\t') x += 12.0f * c.scale; continue; }
            aquad(bind, x, c.y, gg.adv * c.scale, 23.0f * c.scale, c.anchor_x, c.anchor_y, gg.u0, gg.v0, gg.u1, gg.v1, c.diffuse, 0, false);
            x += (gg.adv - 2.0f) * c.scale;
        }
    }

    // The text quad's origin in pixels, FUN_005f0ef0 [BYTES 0x5f0f88..0x5f108a]: x = round(X x W / 1024), or
    // on a screen wider than 4:3, for a text in the 4:3 field, the field's map of X - 1; y = round((Y + 1) x
    // H / 768). The raster is drawn 1:1 from there, its black shadow one pixel left and down.
    // [approx] the half-pixel phase of the mapped x (FUN_005d3be0 returns it without the -0.5).
    float text_x(float x, int anchor) const {
        if (anchor == 0x23D) return x;
        if (pillarbox && anchor != 0) return px_x(x - 1.0f, anchor);
        return std::round(x * W / 1024.0f);
    }
    float text_y(float y, int anchor) const {
        return anchor == 0x23D ? y : std::round((y + 1.0f) * H / 768.0f);
    }
    // One glyph of the atlas at a pixel pen (line top), scaled about the baseline by k.
    void glyph_px(const MenuFont::Glyph& gp, float x, float y, float k, std::uint32_t col) {
        const float gx = x + float(gp.bx) * k, gy = y + float(gdi.ascent) - float(gp.by) * k;
        quad(gdi_bind, gx, gy, gx + float(gp.w) * k, gy + float(gp.h) * k, gp.u0, gp.v0, gp.u1, gp.v1, col, 0, false);
    }

    // §7.3: GDI text (the book's lines, csprite text): the window font's raster 1:1, a (-1,+1) pixel black
    // shadow first; a line feed is tmHeight + extra x H / 768 pixels (FUN_005f03f0).
    void text_gdi(const DrawCmd& c) {
        const float line = float(gdi.lineH) + float(int(c.line_extra * H) / 768);
        const float x0 = text_x(c.x, c.anchor_x), y0 = text_y(c.y, c.anchor_y);
        auto run = [&](float ox, float oy, std::uint32_t col) {
            const float k = c.scale;
            float x = x0 + ox, y = y0 + oy;
            for (unsigned char ch : c.text) {
                if (ch == '\r') continue;
                if (ch == '\n') { x = x0 + ox; y += line * k; continue; }
                if (ch < 32 || ch > 126) continue;
                const MenuFont::Glyph& gp = gdi.gl[ch - 32];
                // "<scale:k>" scales about the baseline (the reference's area lines sit
                // (1 - k) * ascent lower than a top-anchored scale would put them) [DATA].
                if (gp.w > 0 && gp.h > 0) glyph_px(gp, x, y, k, col);
                x += ch == ' ' && c.space_px > 0 ? c.space_px : float(gp.adv) * k;
            }
        };
        run(-1.0f, 1.0f, c.diffuse & 0xFF000000u);
        run(0.0f, 0.0f, c.diffuse);
    }

    // The window text (richtext.hpp, WNDDIALOG_DRAW.md §6.4-§6.6): every glyph's black shadow at
    // (-1, +1) first, then the glyphs in their run colours; the typewriter fades the glyph at the
    // reveal counter in. Glyphs scale about the baseline.
    void text_rich(const DrawCmd& c) {
        const float ca = float(c.diffuse >> 24) / 255.0f;
        const float x0 = text_x(c.x, c.anchor_x), y0 = text_y(c.y, c.anchor_y);
        for (int pass = 0; pass < 2; ++pass) {
            const float ox = pass == 0 ? -1.0f : 0.0f, oy = pass == 0 ? 1.0f : 0.0f;
            for (const RichGlyph& gl : c.rich) {
                if (gl.ext) { text_ext(c, gl, x0 + ox, y0 + oy, ca, pass == 0); continue; }
                if (gl.c < 33 || gl.c > 126) continue;
                const float r = reveal_alpha(gl, c.reveal);
                if (r <= 0) continue;
                const MenuFont::Glyph& gp = gdi.gl[gl.c - 32];
                if (gp.w <= 0 || gp.h <= 0) continue;
                const std::uint32_t a = std::uint32_t(float(gl.colour >> 24) * ca * r);
                // The run colour is baked into the raster; the draw colour modulates it (the
                // menu rows' 0xB0D0D0, an unfocused dialog's 0x7F7F7F).
                auto ch = [&](int sh) {
                    return std::uint32_t(((gl.colour >> sh) & 0xFF) * ((c.diffuse >> sh) & 0xFF) / 255) << sh;
                };
                const std::uint32_t col = (a << 24) | (pass == 0 ? 0u : (ch(16) | ch(8) | ch(0)));
                glyph_px(gp, x0 + gl.x + ox, y0 + gl.y + oy, gl.scale, col);
            }
        }
    }

    // An extfont cell (<ext> / <roo>; FUN_005f03f0's ext branch): the whole N x N cell at (pen x +
    // left, line top), unscaled, in the run colour, under the same (-1, +1) shadow as the letters.
    // [approx] the 32-px sheet scaled to the frame (retail loads extfont20..40 by height, #89).
    void text_ext(const DrawCmd& c, const RichGlyph& gl, float ox, float oy, float ca, bool shadow) {
        if (!ext.ok() || !ext_tex.bind || gl.c >= ExtFont::kCells) return;
        const float r = reveal_alpha(gl, c.reveal);
        if (r <= 0) return;
        const float s = H / 768.0f;
        const float n = std::round(float(ext.cell) * s);
        const float gx = ox + gl.x + std::round(float(ext.left[gl.c]) * s);
        const float gy = oy + gl.y;
        const std::uint32_t a = std::uint32_t(float(gl.colour >> 24) * ca * r);
        auto ch = [&](int sh) {
            return std::uint32_t(((gl.colour >> sh) & 0xFF) * ((c.diffuse >> sh) & 0xFF) / 255) << sh;
        };
        const std::uint32_t col = (a << 24) | (shadow ? 0u : (ch(16) | ch(8) | ch(0)));
        const float v0 = float(gl.c) / float(ExtFont::kCells), v1 = float(gl.c + 1) / float(ExtFont::kCells);
        quad(ext_tex.bind, gx, gy, gx + n, gy + n, 0.0f, v0, 1.0f, v1, col, 0, false);
    }

    // Execute `list` over `view` (loadOp Load). The side pillars (§2.1, raw pixels, opaque)
    // come first, as FUN_005a73c0 draws them.
    void render(Gpu& g, WGPUTextureView view, std::uint32_t sw, std::uint32_t sh, const DrawList& list) {
        W = float(sw); H = float(sh);
        W43 = float((4 * sh) / 3);
        pillarbox = W > W43 + 0.5f;
        font_for(g, sh);
        verts.clear(); batches.clear();
        for (const DrawCmd& c : list) {
            switch (c.kind) {
                case DrawCmd::Quad: {
                    add_mode = (c.flags & 0x80) != 0;
                    const std::size_t v0 = verts.size();
                    aquad(bind_of(g, c.tex), c.x, c.y, c.w, c.h, c.anchor_x, c.anchor_y, c.u0, c.v0, c.u1, c.v1,
                          c.diffuse, c.specular, (c.flags & 0xFFFF) == 0x0001);
                    if (c.hgrad && verts.size() == v0 + 6)       // quad()'s order: q0 q1 q2 q0 q2 q3 (q0, q3 = the left edge)
                        for (std::size_t i : {v0, v0 + 3, v0 + 5}) {
                            verts[i].d[0] = std::uint8_t(c.diffuse_left >> 16); verts[i].d[1] = std::uint8_t(c.diffuse_left >> 8);
                            verts[i].d[2] = std::uint8_t(c.diffuse_left); verts[i].d[3] = std::uint8_t(c.diffuse_left >> 24);
                        }
                    add_mode = false;
                    break;
                }
                case DrawCmd::TextRich: text_rich(c); break;
                case DrawCmd::TextMiddle: text_bitmap(g, c, true); break;
                case DrawCmd::TextMini: text_bitmap(g, c, false); break;
                case DrawCmd::TextGeo: text_geo(g, c); break;
                case DrawCmd::TextGdi: text_gdi(c); break;
                case DrawCmd::Pillars: {
                    // §2.1: drawn only when A != 1; 16:9 art from A >= 1.21, else 16:10 (tex id + 1).
                    if (!pillarbox) break;
                    const bool wide = W / W43 >= 1.21f;
                    const WGPUBindGroup pb = bind_of(g, {TexRef::Menu, wide ? c.tex.id : c.tex.id + 1});
                    const float Lx = (W - W43) / 2, R = Lx + W43;
                    const float ul = wide ? 0.16796875f : 0.1015625f, ur = wide ? 0.83203125f : 0.8984375f;
                    // The left strip is half a pixel wider than the margin (x -0.5, w Lx + 0.5: title [BYTES
                    // 0x576e53..0x576eab], book FUN_005a6230), so its last column samples the strip's last art
                    // texel; ending at Lx blends in the black texel past ul, a dark line at the field's edge.
                    quad(pb, 0, 0, Lx + 0.5f, H, 0, 0, ul, 0.75f, c.diffuse, c.specular, true);
                    // The right strip starts a pixel inside the field (R - 1.0 for the title's
                    // strips, R - 0.5 for the book's), so no seam shows against the logo / Back00.
                    quad(pb, R - 1.0f, 0, W, H, ur, 0, 1, 0.75f, c.diffuse, c.specular, true);
                    break;
                }
                case DrawCmd::Mesh: mesh(g, c); break;
            }
        }
        submit(g, view);
    }

    // The game cursor (FUN_00426f20, src/cleria/mouse): CURSOR.DDS (data\image\cursor.dds, the image
    // table's entry 3) as one quad of back-buffer pixels, the triangle strip v0 v1 v2 / v2 v1 v3,
    // white-modulated, alpha-blended (render state 0x41: SRCALPHA / INVSRCALPHA).
    void cursor(Gpu& g, WGPUTextureView view, std::uint32_t sw, std::uint32_t sh, const mouse::Quad& q) {
        W = float(sw); H = float(sh);
        verts.clear(); batches.clear();
        const WGPUBindGroup bind = bind_of(g, {TexRef::Named, 0, "CURSOR.DDS"});
        if (!bind) return;
        BookVertex b{};
        b.d[0] = std::uint8_t(q.colour >> 16); b.d[1] = std::uint8_t(q.colour >> 8);
        b.d[2] = std::uint8_t(q.colour); b.d[3] = std::uint8_t(q.colour >> 24);
        batches.push_back({bind, 0, 0, false});
        for (int i : {0, 1, 2, 2, 1, 3}) {
            BookVertex v = b;
            v.x = q.x[i] / W * 2.f - 1.f;
            v.y = 1.f - q.y[i] / H * 2.f;
            v.u = q.u[i]; v.v = q.v[i];
            verts.push_back(v);
            ++batches.back().count;
        }
        submit(g, view);
    }

    void submit(Gpu& g, WGPUTextureView view) {
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
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, 0, bytes);
        for (const Batch& b : batches) {
            wgpuRenderPassEncoderSetPipeline(pass, b.add ? pipeline_add : pipeline);
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

// The cursor sprite over the finished 2D layers (key 0x02000000 in pass 15: above the HUD, the
// windows, the book and the title; below the FPS text and the screen flashes, MOUSE.md §2.5). The field
// look turns toward the cursor when no menu window is up and a player exists. Never while the shell
// owns the input (it shows the OS pointer).
static void draw_cursor(Gpu& g, BookRenderer& br, WGPUTextureView view, std::uint32_t w, std::uint32_t h,
                        bool player) {
    if (!g_cursor.queued() || g_cursor.colour == 0 || g_shell_focus) return;
    const mouse::Quad q = mouse::cursor_quad(g_cursor, !g_cursor.menu && player, g_sdl_in.display());
    br.cursor(g, view, w, h, q);
}

// The message box's face (WNDDIALOG_DRAW.md §2.5 step 3): face index k -> ani slot 154 + k ->
// ANI/F_(200 + 10k).AIA; the frame (the Window_Msg op1) is composed onto the set's canvas, as
// the engine textures the whole record, and cached until the face or frame changes. The frame is
// clamped to the sheet [H: the engine's out-of-range behaviour is unknown, Uncertain #8].
struct FaceCache {
    int face = -1, frame = -1;            // frame -2 = decode failed (don't retry every frame)
    Aia aia;
    bool aia_ok = false;
    SpriteTex tex{};
    const SpriteTex* get(Gpu& g, const fs::path& root, int k, int fr) {
        if (k < 0 || fr < 0) return nullptr;
        if (face != k) {
            face = k; frame = -1; aia_ok = false;
            char nm[16];
            std::snprintf(nm, sizeof nm, "F_%d", 200 + 10 * k);
            aia = Aia{};
            aia_ok = aia.load(root / "ANI" / (std::string(nm) + ".AIA")) && aia.ok();
        }
        if (aia_ok && aia.nImg > 0) fr = std::min(fr, int(aia.nImg) - 1);
        if (aia_ok && frame != fr) {
            AiaFrame f;
            if (aia.decode_frame(std::uint32_t(fr), 0, 0, f) && f.w > 0) {
                const std::uint32_t cw = aia.canvasW, chh = aia.canvasH;
                std::vector<std::uint8_t> canvas(std::size_t(cw) * chh * 4, 0);
                for (std::uint32_t y = 0; y < f.h && y + f.y0 < chh; ++y)
                    for (std::uint32_t x = 0; x < f.w && x + f.x0 < cw; ++x)
                        std::memcpy(&canvas[(std::size_t(y + f.y0) * cw + x + f.x0) * 4],
                                    &f.rgba[(std::size_t(y) * f.w + x) * 4], 4);
                tex = upload_rgba(g, cw, chh, canvas.data());
                frame = fr;
            } else frame = -2;
        }
        return (frame == fr && tex.tex) ? &tex : nullptr;
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
    // CleriaCore's own (cleria.save.card, API 7): a mod's line for this save, after the area's name.
    for (const auto& [id, kv] : read_sidecar(file).mod) {
        const auto it = kv.find("cleria.card");
        if (it == kv.end()) continue;
        const auto j = json::parse(it->second);
        if (j && j->is_string() && !j->as_string().empty()) c.area += "  [" + j->as_string().substr(0, 40) + "]";
    }
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

// Headless: cleria-view <root> x --wnd out.ppm [W H]. CLERIA_WND = msg | item | menu (the
// message box with a face, the item-get box, the statue's choice box); CLERIA_WND_BG = a P6
// .ppm of the same size to draw over (e.g. a --play shot). The window is armed and stepped 240
// frames, as if Wait_Prompt were waiting on it.
static int render_wnd(Gpu& g, const fs::path& asset_root, const fs::path& out, std::uint32_t W, std::uint32_t H) {
    const ItemTable items = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D; td.size = {W, H, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture color = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTextureView cview = wgpuTextureCreateView(color, nullptr);
    {   // the background (or black)
        std::vector<std::uint8_t> px(std::size_t(W) * H * 4, 0);
        for (std::size_t i = 3; i < px.size(); i += 4) px[i] = 255;
        if (const char* bg = std::getenv("CLERIA_WND_BG")) {
            std::ifstream f(bg, std::ios::binary);
            std::string magic; std::uint32_t bw = 0, bh = 0, mx = 0;
            f >> magic >> bw >> bh >> mx; f.get();
            if (magic == "P6" && bw == W && bh == H)
                for (std::size_t i = 0; i < std::size_t(W) * H; ++i) f.read(reinterpret_cast<char*>(&px[i * 4]), 3);
        }
        WGPUTexelCopyTextureInfo dst = {}; dst.texture = color; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay = {}; lay.bytesPerRow = W * 4; lay.rowsPerImage = H;
        WGPUExtent3D ext = {W, H, 1};
        wgpuQueueWriteTexture(g.queue, &dst, px.data(), px.size(), &lay, &ext);
    }
    BookRenderer br;
    br.init(g, WGPUTextureFormat_RGBA8Unorm, asset_root, items);
    FaceCache faces;
    br.face_bind = [&](int face, int frame) -> WGPUBindGroup {
        const SpriteTex* t = faces.get(g, asset_root, face, frame);
        return t ? t->bind : nullptr;
    };
    const RichMetrics m = br.metrics(g, W, H);
    const std::string kind = std::getenv("CLERIA_WND") ? std::getenv("CLERIA_WND") : "msg";
    WndText t;
    WndDialog d;
    std::vector<int> snd;
    if (kind == "item") {
        d.treasure(512, 122, 0x54, "Received\n<color:0xfff2ea8c>Crystal<color:>.", m);
        d.arm();
    } else if (kind == "menu") {
        d.menu(512, 282, "What will you do?", {"Save", "Accept divine blessing", "Nothing right now"},
               {"@SaveMenu", "@GrowMenu", "@SaveBack"}, m, true);
        d.arm();
    } else if (kind != "title" && kind != "caption" && kind != "bust") {
        t.message(0x57, 1, 0, "Roy", "Heh, so you're the new recruits.\nWelcome to the <color:0xfff2ea8c>"
                  "Search Party<color:>.", m);
        t.arm();
    }
    CaptionCard cap;                                     // CLERIA_WND=caption: map_name04 at mid-hold
    Busts busts;                                         // CLERIA_WND=bust: t_840a at (80, 768), faded in
    busts.size_of = [&](const std::string& tx, int* w, int* h) { return br.named_size(g, tx, w, h); };
    if (kind == "caption") { cap.open("map_name04"); for (int i = 0; i < 70; ++i) cap.step(); }
    if (kind == "bust") {                                // CLERIA_WND_BUST = another card at (0, 768), e.g. v_160
        const char* bn = std::getenv("CLERIA_WND_BUST");
        busts.open(bn ? bn : "t_840a", bn ? 0.0f : 80.0f, 768);
        for (int i = 0; i < 60; ++i) busts.step();
    }
    TitleScreen ts;
    TitleGates gates;
    if (kind == "title") {                               // the title screen; CLERIA_WND_CLEAR=1 = all cleared
        if (std::getenv("CLERIA_WND_CLEAR")) gates.clear_toal = gates.clear_hugo = gates.clear_yunica = gates.nya = true;
        for (int i = 0; i < 40; ++i) ts.update({}, false, gates, m, snd);
    }
    for (int i = 0; i < 240; ++i) { t.update({}, snd); d.update({}, snd); }
    DrawList dl;
    if (kind == "title") ts.draw(dl);
    busts.draw(dl, int(W), int(H));
    cap.draw(dl);
    t.draw(dl, d.alive());
    d.draw(dl);
    br.render(g, cview, W, H, dl);
    std::printf("wnd: %s, %zu draw commands\n", kind.c_str(), dl.size());

    const std::uint32_t bpr = (W * 4 + 255) & ~255u;
    WGPUBufferDescriptor rbd = {};
    rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead; rbd.size = std::size_t(bpr) * H;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(g.device, &rbd);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPUTexelCopyTextureInfo src = {}; src.texture = color; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dstb = {}; dstb.buffer = readback;
    dstb.layout.bytesPerRow = bpr; dstb.layout.rowsPerImage = H;
    WGPUExtent3D ext = {W, H, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dstb, &ext);
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
