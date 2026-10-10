// cleria-view — GPU acquisition, Room load/free, textures, pipeline, fog, draw
// Textual part of main.cpp (split 2026-07-23, PLAN.md Phase 0): #included by
// main.cpp in a fixed order; ONE translation unit, not standalone. Keep it that way
// until a real TU split is warranted.

// ---- async acquisition ------------------------------------------------------
struct AdapterReq { WGPUAdapter adapter = nullptr; bool done = false; };
static void on_adapter(WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView msg,
                       void* u1, void*) {
    auto* r = static_cast<AdapterReq*>(u1);
    if (s == WGPURequestAdapterStatus_Success) r->adapter = a;
    else std::fprintf(stderr, "adapter: %.*s\n", int(msg.length), msg.data);
    r->done = true;
}
struct DeviceReq { WGPUDevice device = nullptr; bool done = false; };
static void on_device(WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView msg,
                      void* u1, void*) {
    auto* r = static_cast<DeviceReq*>(u1);
    if (s == WGPURequestDeviceStatus_Success) r->device = d;
    else std::fprintf(stderr, "device: %.*s\n", int(msg.length), msg.data);
    r->done = true;
}

// Persistent GPU objects (survive room changes).
struct Gpu {
    WGPUInstance instance{};
    WGPUAdapter adapter{};
    WGPUDevice device{};
    WGPUQueue queue{};
    WGPURenderPipeline pipeline{};
    WGPURenderPipeline fx_pipeline{};   // additive effect billboards (SEF/EF2)
    WGPURenderPipeline fx_pipeline_sa{};   // the same with the texture's alpha as SRCALPHA (the EF2 particles)
    WGPURenderPipeline fx_pipeline_sub{};  // EF2 render word 0x200000: REVSUBTRACT, SRCALPHA / ONE (dst - src)
    WGPURenderPipeline fx_pipeline_pm{};   // EF2 render word 0x1000000: ONE / INVSRCALPHA
    WGPURenderPipeline char_pipeline{}; // alpha-blended character/enemy sprites
    WGPURenderPipeline char_pipeline_add{}; // additive variant, for glow overlays (Topic selection wash)
    WGPURenderPipeline shadow_pipeline{}; // floor shadow decals (alpha-over, no depth write)
    WGPURenderPipeline mat_pipe[4][2]{};  // blended YMO materials: [blend mode 1..3][z-write]
    WGPURenderPipeline zpre_pipe{};       // FUN_005f8de0's depth pre-pass (material_zprepass)
    // The same three with D3DCULL_CCW, for a material without flags 0x400 (material_cull).
    WGPURenderPipeline cull_pipeline{}, cull_mat_pipe[4][2]{}, cull_zpre_pipe{};
    WGPUBuffer ubuf{};
    WGPUBindGroup ubind{};
    WGPUSampler sampler{};
    WGPUSampler sampler_mip{};    // "Smooth distant textures": the world sampler over the mip chain
    WGPUSampler sampler_point{};  // "Character sprites: Crisp": nearest
    WGPUBindGroupLayout tex_layout{};
    WGPUBindGroup white_bind{};   // failed-to-load texture (missing-ref marker)
    WGPUBindGroup black_bind{};   // genuinely untextured material (renders its dark diffuse)
    std::unordered_map<std::string, fs::path> dds_index;  // basename(lower) -> path
    std::unordered_map<std::string, fs::path> sob_index;  // stem(upper) -> path
};

// Per-room GPU objects (rebuilt on every transition).
// One scrolling material's live state: which vertices it owns, their authored UVs,
// and the accumulated phase. The engine keeps the phase on the material's render
// struct; we keep it per batch, which is the same thing for our batching.
struct ScrollBatch {
    std::uint32_t first_vert = 0, vert_count = 0;
    float vel_u = 0, vel_v = 0;
    float phase_u = 0, phase_v = 0;
};

struct Room {
    Vec3 snf_light{1.0f, 1.0f, 1.0f};  // the SNF block's light colour (white without the file: FUN_00431260's default)
    RoomMesh mesh;
    std::vector<ScrollBatch> scrolls;  // materials with flag 0x4 / nonzero velocity
    RoomMesh coll;           // the `_.YMO` collision hulls — what physics collides against
    std::vector<std::uint8_t> base_a;  // each vertex's alpha after bake_material_alpha (apply_object_alpha)
    std::vector<float> batch_alpha;    // each batch's live object (model) alpha; empty / missing = 1
    // A brain's per-material alpha (the instance material record's +0x20) of one object, by its YMO material index:
    // multiplied into that object's batch alphas by apply_object_alpha (Darm's limb groups, view_boss08.inl).
    std::unordered_map<std::string, std::vector<float>> mat_alpha;
    // ... and its per-material colour multiplier (the record's +0x14..+0x1c: Mesh_SetMaterialColor 0x182, Darm's
    // MatMode), multiplied into the vertex colours from base_rgb (each vertex's r, g, b as loaded).
    std::unordered_map<std::string, std::vector<Vec3>> mat_rgb;
    std::vector<std::uint8_t> base_rgb;
    // ... and an object's additive colour (every material record's +0x24..+0x2c, loaded into c17 and added to oD1
    // on the specular path only: material flag 0x2): the boss hit flash FUN_00504140 writes around the draw. It
    // rides in the vertices' ny as a sprite's specular does (main.cpp vs); add_ny keeps the normals it replaces.
    std::unordered_map<std::string, Vec3> mat_add;
    std::unordered_map<std::string, std::vector<float>> add_ny;
    // The objects the Mask of Eyes gates (SOB +0x14c & 0x1000 / 0x2000; view_mask.inl).
    struct MaskObj {
        std::size_t rr = 0;              // index in mesh.object_ranges
        long cr = -1;                    // index in coll.object_ranges (-1: no hull)
        std::uint32_t bits = 0;          // +0x298 & 6: 2 = gone under the mask, 4 = only under it
        float alpha = 1, target = 1, step = 0;   // +0x418 / +0x41c / +0x420
        bool hidden = false;             // +0x474
        bool coll_off = false;
        std::vector<Vec3> coll_saved;    // the hull's vertices while it is collapsed
    };
    std::vector<MaskObj> mask_objs;
    bool mask_seeded = false;
    Sen sen;
    Sfo fog;                 // per-region scene fog (SFO sidecar)
    Scm cam;                 // per-region cameras (SCM sidecar)
    Sef fx;                  // scene effects: torch flames etc (SEF sidecar)
    Sct cast;                // actor placement: enemies + player spawn (SCT sidecar)
    std::vector<SceneBox> event_boxes;  // named trigger boxes (.SEV) — op 0x13d Chr_IsInEventBox
    // Effects the room-load object-variant script placed (op 0x117 Eft_LocatePos). The
    // cleansed goddess statue's glow comes through here: S_SAVEOBJECTCHANGE picks the
    // cleansed model AND spawns eff 84 (Yunica/Hugo) / 85 (Toal) beside it. The visibility
    // run used to use a hostless VM, so `locate_effect` went nowhere and the statue stood
    // cleansed but unlit. The caller forwards these into the world after the room loads.
    struct LoadEffect { int id; float x, y, z, a, b; std::string name; };
    std::vector<LoadEffect> load_effects;
    fs::path dir;            // the room folder (its scripts live beside the SOB)
    std::string name;
    WGPUBuffer vbuf{}, ibuf{};
    std::vector<WGPUTexture> textures;
    std::vector<WGPUTextureView> views;
    std::vector<WGPUBindGroup> binds;  // parallel to mesh.textures
    bool tex_mips = false;             // g_world_mips when its textures were uploaded (load_room's `donor`)
};

// Play the footstep for the surface of `tri` on the collision mesh: material name
// -> surface (by name) -> EFFECTDEF id, variant = step_n & mask.
//
// A name that matches nothing resolves to `stone`, not to silence — the exe clamps
// an unmatched lookup to 1 (see surface.hpp). That covers "default", the second most
// common collision material. Silence is driven by base_sound_id == 0 (`none`,
// `stopper`, `reserve20`) instead. CLERIA_STEPS=1 traces each step.
// Returns the TICKS to wait before the next step, or 0 to re-check next tick. A
// silent surface returns BEFORE the reload in the exe, so its timer stays 0 and the
// check re-runs every tick — don't "reload anyway".
static int play_footstep(Audio& audio, const RoomMesh& m, int tri, unsigned step_n,
                         bool running, std::uint32_t jitter_roll) {
    const std::string_view surf = surface_of(m, tri);
    if (surf.empty()) return 0;
    const int idx = surface_index_or_default(surf);
    const int id = footstep_sound(idx, step_n);
    if (std::getenv("CLERIA_STEPS")) {
        const EffectDef* d = id >= 0 ? audio.defs().find(id) : nullptr;
        std::printf("  step %-10.*s surface=%-3d%s id=%-4d %-16s %s\n",
                    int(surf.size()), surf.data(), idx,
                    surface_index(surf) < 0 ? " (defaulted)" : "", id,
                    d ? d->name.c_str() : "(silent)", running ? "[run]" : "[walk]");
    }
    if (id < 0) return 0;  // silent surface: no reload
    audio.play(id, 0.55f);
    return footstep_interval(idx, running, jitter_roll);
}

// System SFX, from the `sys\` family at the head of EFFECTDEF.TXT (ids 0..19).
// The Japanese comment column names each one, so these three need no guessing:
// s_move is カーソル移動 ("cursor move") and s_ok is 決定 ("confirm").
constexpr int kSfxConfirm = 0;  // sys\s_ok
constexpr int kSfxCancel = 1;   // sys\s_cancel
constexpr int kSfxBoo = 2;      // sys\s_boo — the "you can't do that" buzz
constexpr int kSfxCursor = 3;   // sys\s_move
constexpr float kSfxMenuVol = 0.8f;
constexpr float kSfxAnimVol = 0.8f;  // anime_SePlay3D events (swings, impacts, voices)

// Damage-path sounds the engine plays from C rather than from animation data
// (build/audio_re/SFX_TRIGGERS.md §4.3).
constexpr int kSfxDeathEntry = 92;   // player\p_hit0 — FUN_004780e0, ANY actor whose HP hits 0
constexpr int kSfxPlayerHurt = 94;   // player\p_damage — the id C_0x0.ASO sections 4/11 emit

// The sounds an enemy's OWN death animation emits: ASO section 5 (the death motion
// state), funcid 0x2a. Enemies do not run an ASO VM in CleriaCore — they loop a fixed
// idle animation — so rather than invent an id, run the death section of the real
// script once per enemy kind and cache whatever it asks for. Verified against the
// scripts: M_100/M_130 -> 438 + 449, M_430 -> 439 + 449, M_910 -> 503 + 449, where
// 449 (zako\z_dead11) is the death sound shared by 71 of the 86 M_*.ASO.
static const std::vector<int>& enemy_death_sounds(const fs::path& asset_root,
                                                  const std::string& stem) {
    static std::unordered_map<std::string, std::vector<int>> cache;
    const auto it = cache.find(stem);
    if (it != cache.end()) return it->second;
    std::vector<int>& out = cache[stem];
    if (!stem.empty()) {
        Aso a = Aso::load(asset_root / "ANI" / (stem + ".ASO"));
        AsoVm vm;
        if (a.ok() && (vm.bind(&a), vm.set_section(5))) {
            for (int t = 0; t < 60 && out.size() < 4; ++t) {
                vm.tick();
                for (const AsoVm::SoundEvent& se : vm.sounds())
                    if (std::find(out.begin(), out.end(), se.id) == out.end()) out.push_back(se.id);
            }
        }
    }
    return out;
}

// The same for the death section's anime_GenerateDebris (funcid 0x26, ASO_NOOPS.md §3.3: one per script, on
// section 5's tick 0 in all but four files): the op and the tick it runs on, nullptr when the script has none.
struct DeathDebris { int tick = 0; AsoVm::DebrisEvent ev; };
static const DeathDebris* enemy_death_debris(const fs::path& asset_root, const std::string& stem) {
    static std::unordered_map<std::string, std::pair<bool, DeathDebris>> cache;
    auto it = cache.find(stem);
    if (it == cache.end()) {
        it = cache.emplace(stem, std::pair<bool, DeathDebris>{}).first;
        Aso a = stem.empty() ? Aso{} : Aso::load(asset_root / "ANI" / (stem + ".ASO"));
        AsoVm vm;
        if (a.ok() && (vm.bind(&a), vm.set_section(5)))
            for (int t = 0; t < 60 && !it->second.first; ++t) {
                vm.tick();
                if (const AsoVm::DebrisEvent* d = vm.debris()) it->second = {true, {t, *d}};
            }
    }
    return it->second.first ? &it->second.second : nullptr;
}

// What physics collides against. The game collides against the `_.YMO` hulls, not
// the render mesh; fall back to the render mesh only if a room ships no twins.
// CLERIA_COLL=render forces the old behaviour, for A/B.
static const RoomMesh& phys_mesh(const Room& r) {
    static const bool force_render = [] {
        const char* e = std::getenv("CLERIA_COLL");
        return e && std::string(e) == "render";
    }();
    return (!force_render && !r.coll.indices.empty()) ? r.coll : r.mesh;
}

static WGPUBuffer make_buffer(WGPUDevice dev, WGPUQueue q, const void* data,
                              std::size_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor bd = {};
    bd.usage = usage | WGPUBufferUsage_CopyDst; bd.size = size;
    WGPUBuffer b = wgpuDeviceCreateBuffer(dev, &bd);
    if (size) wgpuQueueWriteBuffer(q, b, 0, data, size);
    return b;
}

// CleriaCore enhancements (the Video page, view_video.inl; both off by default, neither is a port):
// g_world_mips -- a room loaded while it is set uploads its textures' mip chains and samples them
// through g.sampler_mip; g_sprite_point -- a character sprite uploaded while it is set samples nearest.
static bool g_world_mips = false, g_sprite_point = false;
// [graphics] LowResoTexture ("Low-Resolution Textures", 0x76516f): the texture index prefers a file's copy in an
// L folder over the H one (build_indexes in main.cpp; set by view_video.inl's video_low_res).
static bool g_low_res_textures = false;

static WGPUTextureView upload_texture(Gpu& g, Room& room, const Dds& dds) {
    WGPUTextureFormat fmt; std::uint32_t block = 1, blk_bytes = 4;
    switch (dds.format) {
        case Dds::BC1: fmt = WGPUTextureFormat_BC1RGBAUnorm; block = 4; blk_bytes = 8; break;
        case Dds::BC2: fmt = WGPUTextureFormat_BC2RGBAUnorm; block = 4; blk_bytes = 16; break;
        default:       fmt = WGPUTextureFormat_RGBA8Unorm;   block = 1; blk_bytes = 4; break;
    }
    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size = {dds.width, dds.height, 1};
    td.format = fmt; td.mipLevelCount = 1 + std::uint32_t(dds.mips.size()); td.sampleCount = 1;
    WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);

    WGPUTexelCopyTextureInfo dst = {}; dst.texture = t; dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = ((dds.width + block - 1) / block) * blk_bytes;
    layout.rowsPerImage = (dds.height + block - 1) / block;
    WGPUExtent3D ext = {dds.width, dds.height, 1};
    wgpuQueueWriteTexture(g.queue, &dst, dds.data.data(), dds.data.size(), &layout, &ext);
    std::uint32_t mw = dds.width, mh = dds.height;
    for (std::size_t l = 0; l < dds.mips.size(); ++l) {      // a block format's level is written in whole blocks
        mw = std::max(1u, mw / 2); mh = std::max(1u, mh / 2);
        dst.mipLevel = std::uint32_t(l + 1);
        layout.bytesPerRow = ((mw + block - 1) / block) * blk_bytes;
        layout.rowsPerImage = (mh + block - 1) / block;
        ext = {(mw + block - 1) / block * block, (mh + block - 1) / block * block, 1};
        wgpuQueueWriteTexture(g.queue, &dst, dds.mips[l].data(), dds.mips[l].size(), &layout, &ext);
    }

    WGPUTextureView view = wgpuTextureCreateView(t, nullptr);
    room.textures.push_back(t);
    room.views.push_back(view);
    return view;
}

static WGPUBindGroup tex_bindgroup(Gpu& g, WGPUTextureView view, WGPUSampler sampler = nullptr) {
    WGPUBindGroupEntry e[2] = {};
    e[0].binding = 0; e[0].textureView = view;
    e[1].binding = 1; e[1].sampler = sampler ? sampler : g.sampler;
    WGPUBindGroupDescriptor d = {};
    d.layout = g.tex_layout; d.entryCount = 2; d.entries = e;
    return wgpuDeviceCreateBindGroup(g.device, &d);
}

// ---- YMO material blend state (build/render_re/MATERIAL.md §4, §6.2) -------------------
// The blend mode FUN_005f92b0 draws a material with [EXPORT FUN_005f92b0, FUN_005faca0]:
//   per-draw alpha a = d7 (diffuse A) x node alpha x model alpha. FUN_005faca0 writes that
//     product into the subset's alpha slot, which FUN_005fb0e0 hands FUN_005f92b0 in XMM1
//     (0x5fb1fb..0x5fb469). Node and model alpha are 1.0 for a scene object at rest (node
//     alpha is initialised to 1.0 by FUN_005f4420); the runtime model alpha reaches the blended
//     subsets through apply_object_alpha, its mode switch is [not modelled].
//   mode = d3, then if mode < 2: mode = (a < 1 || flags & 0x10) ? 1 : 0.
//   (int)(d13 * a) >= 1, or flags & 0x40000, with a == 1 -> mode 0 (an alpha-test cutout)
//     [H: that override is gated by DAT_0076519a != 0, a capability byte never seen written].
// So an artist's 0.999 diffuse alpha (the s10step shadows) is how a mesh asks to be blended.
// `a` is the per-draw alpha. A scene object passes d7 (FUN_005faca0 folds it in); an EFTMESH
// particle draws with FUN_005fafc0(0, -1), whose subsets take node x model alpha instead
// (FUN_005fb0e0 0x5fb202..0x5fb216), the model alpha being the particle's track alpha.
static int material_blend_mode(const DrawBatch& b, float a) {
    int mode = int(b.blend);
    if (mode > 3) mode = 0;                       // only 0..3 occur (MATERIAL.md §4)
    if (mode >= 2) return mode;
    const int cut = int(float(b.alpha_ref) * a);  // local_6c
    if ((cut != 0 || (b.mat_flags & 0x40000)) && a < 1.0f) return 1;
    if (cut >= 1 || (b.mat_flags & 0x40000)) return 0;
    return (a < 1.0f || (b.mat_flags & 0x10)) ? 1 : 0;
}
static int material_blend_mode(const DrawBatch& b) { return material_blend_mode(b, b.mat_alpha); }
// D3DRS_ZWRITEENABLE (local_70): cleared by flags 0x20000, and by flags 0x10 on the alpha
// path [H: the 0x10 clear is skipped when DAT_00765553 & 0x20, a runtime byte the actor draw
// toggles; taken as clear for scene objects].
static bool material_zwrite(const DrawBatch& b) {
    return (b.mat_flags & 0x20000) == 0 && (b.mat_flags & 0x10) == 0;
}
// D3DRS_CULLMODE [C-DIS FUN_005f92b0, MATERIAL.md §3 / §6]: D3DCULL_CCW (3), or D3DCULL_NONE (1) for a material with
// flags 0x400. So a one-sided shell is not drawn from behind: S_7099's cloud dome (01kumo / 03kumo, flags 0xc, about
// 53 around the arena) hid the whole frame from the High form's camera, whose eye is outside it.
// [not modelled] the CW flip while DAT_007b38f0 != 0 (the reflection view).
static bool material_cull(const DrawBatch& b) { return (b.mat_flags & 0x400) == 0; }
// The depth pre-pass FUN_005fb0e0 issues right before a subset's FUN_005f92b0 draw
// (0x5fb418..0x5fb45a): draw flags & 0x1014 == 0 (a static GoMesh passes 0 or 0x400,
// FUN_00561f40), a material with flags 0x10, no 0x20000 and d3 <= 1, and
// !(DAT_00765553 & 0x20 && model flags & 1) [H: that byte taken as clear, as for local_70].
// FUN_005f8de0(.., 0xbf) then draws the subset once with ZENABLE 1, ZWRITEENABLE 1,
// ALPHABLENDENABLE 0, ALPHATESTENABLE 1, ALPHAREF 0xbf (ALPHAFUNC stays GREATEREQUAL, set
// only by the state reset FUN_005f1870), stage 0 ALPHAOP SELECTARG1(TEXTURE) on the
// material's own texture (rt+0x58, as FUN_005f6e50 binds it) and COLORWRITEENABLE 8 (alpha
// only). So a "force blend, z-write off" cutout — 00znuki00, S_0002's Roda-tree foliage —
// still puts the depth of its solid texels (tex alpha >= 0xbf) down, and whatever the layer
// draws later behind it (00znuki10, the mountain band, flags 0x20000: no pre-pass) is hidden.
static bool material_zprepass(const DrawBatch& b) {
    return (b.mat_flags & 0x20000) == 0 && (b.mat_flags & 0x10) != 0 && b.blend <= 1;
}
// FUN_005faca0's pass byte 3 (subset bit 8): a material with flags 0x8040 (and not
// 0x8000000, which goes to byte 4 first). A static GoMesh (type 0, FUN_00561f40 — the room
// and its scene objects) queues that pass on render layer 0xb (0x5621eb..0x5621f7), which
// FUN_005dfea0 flushes AFTER the actor sprite bodies (FUN_00473540: layer 6, or 9 with
// +0x2a8) and the shadows (layer 8). Floor 2's water surfaces (20zwater3 0x104e, 20zzmizu3
// 0x40104e) are such materials, so a submerged player shows through them.
static bool room_late_layer(const DrawBatch& b) {
    return (b.mat_flags & 0x8000000u) == 0 && (b.mat_flags & 0x8040u) != 0;
}

// oD0 = c16 * v2 (b_ndst1_trn / b_ndst2_trn, `mul oD0, c16, v2`) and c16.w is the per-draw
// alpha, so a blended material's alpha reaches the pixel through the vertex alpha. Fold it
// in once at load (the room uniform has no per-draw slot). Opaque materials never read it.
static void bake_material_alpha(RoomMesh& m) {
    std::vector<std::uint8_t> done(m.vertices.size(), 0);
    for (const DrawBatch& b : m.batches) {
        if (material_blend_mode(b) == 0 || b.mat_alpha >= 1.0f) continue;
        const float a = std::max(b.mat_alpha, 0.0f);
        for (std::uint32_t i = 0; i < b.index_count; ++i) {
            const std::size_t k = std::size_t(b.first_index) + i;
            if (k >= m.indices.size()) break;
            const std::uint32_t v = m.indices[k];
            if (v >= m.vertices.size() || done[v]) continue;
            done[v] = 1;
            m.vertices[v].a = std::uint8_t(float(m.vertices[v].a) * a + 0.5f);
        }
    }
}

// One scene object's live alpha (+0x418, copied to its anim object's +0xc each update by
// FUN_00503190: the model alpha). The per-draw alpha is d7 x node x model (MATERIAL.md §6.2):
// the draw takes the mode from it per batch (batch_alpha), so an opaque subset of a fading
// object draws as mode 1 the way FUN_005f92b0 switches it when a < 1, and a = 0 draws nothing
// (FUN_005faca0). A subset drawn blended gets its vertex alpha = load-time value x d7 (unless
// the load already baked d7 in) x this. Also restores the d7 bake after a re-bake.
static void apply_object_alpha(Room& room, const RoomMesh::ObjRange* r, float a) {
    if (!r || room.base_a.size() != room.mesh.vertices.size()) return;
    const float ko = std::clamp(a, 0.0f, 1.0f);
    room.batch_alpha.resize(room.mesh.batches.size(), 1.0f);
    const auto ma = room.mat_alpha.find(r->name);
    const auto mc = room.mat_rgb.find(r->name);
    const bool rgb = mc != room.mat_rgb.end() && room.base_rgb.size() == room.mesh.vertices.size() * 3;
    // c17 = the record's +0x24..+0x2c when the material has flag 0x2, else 0 [EXPORT 005f92b0.c:123-145]; the
    // fixed-function stage adds oD1 after the texture modulate. FUN_00504140 skips render layers >= 10 ([approx]
    // only the layer-0xb class is told apart here). The colour is quantised to the 8-bit specular ([approx]).
    const auto mad = room.mat_add.find(r->name);
    if (mad != room.mat_add.end()) {
        const Vec3 c = mad->second;
        const std::uint32_t s = (std::uint32_t(std::clamp(c.x, 0.0f, 1.0f) * 255.0f + 0.5f) << 16) |
                                (std::uint32_t(std::clamp(c.y, 0.0f, 1.0f) * 255.0f + 0.5f) << 8) |
                                std::uint32_t(std::clamp(c.z, 0.0f, 1.0f) * 255.0f + 0.5f);
        std::vector<float>& keep = room.add_ny[r->name];
        keep.resize(std::size_t(r->v1 - r->v0), 0.0f);
        for (const DrawBatch& b : room.mesh.batches) {
            if (b.first_vert < r->v0 || b.first_vert >= r->v1 || !(b.mat_flags & 0x2u) || room_late_layer(b)) continue;
            for (std::uint32_t v = b.first_vert; v < b.first_vert + b.vert_count && v < r->v1; ++v) {
                float& ny = room.mesh.vertices[v].ny;
                if (ny >= -1.5f) keep[v - r->v0] = ny;       // a real normal (a re-bake wrote it again)
                ny = s ? -(1.0f + float(s)) : keep[v - r->v0];
            }
        }
        if (!s) { room.mat_add.erase(mad); room.add_ny.erase(r->name); }
    }
    for (std::size_t i = 0; i < room.mesh.batches.size(); ++i) {
        const DrawBatch& b = room.mesh.batches[i];
        if (b.first_vert < r->v0 || b.first_vert >= r->v1) continue;
        float k = ko;
        if (ma != room.mat_alpha.end() && b.material >= 0 && std::size_t(b.material) < ma->second.size())
            k = ko * std::clamp(ma->second[std::size_t(b.material)], 0.0f, 1.0f);
        room.batch_alpha[i] = k;
        const bool baked = material_blend_mode(b) != 0 && b.mat_alpha < 1.0f;   // bake_material_alpha
        const float f = material_blend_mode(b, b.mat_alpha * k) == 0 ? 1.0f
                        : k * (baked ? 1.0f : std::clamp(b.mat_alpha, 0.0f, 1.0f));
        for (std::uint32_t v = b.first_vert; v < b.first_vert + b.vert_count && v < r->v1; ++v)
            room.mesh.vertices[v].a = std::uint8_t(float(room.base_a[v]) * f + 0.5f);
        // oD0 = c16 * v2 (above): c16.rgb is the material's colour, here the instance record's multiplier.
        if (!rgb) continue;
        Vec3 c{1.0f, 1.0f, 1.0f};
        if (b.material >= 0 && std::size_t(b.material) < mc->second.size()) c = mc->second[std::size_t(b.material)];
        for (std::uint32_t v = b.first_vert; v < b.first_vert + b.vert_count && v < r->v1; ++v) {
            const std::uint8_t* s = &room.base_rgb[std::size_t(v) * 3];
            RoomVertex& o = room.mesh.vertices[v];
            o.r = std::uint8_t(float(s[0]) * std::clamp(c.x, 0.0f, 1.0f) + 0.5f);
            o.g = std::uint8_t(float(s[1]) * std::clamp(c.y, 0.0f, 1.0f) + 0.5f);
            o.b = std::uint8_t(float(s[2]) * std::clamp(c.z, 0.0f, 1.0f) + 0.5f);
        }
    }
}
static void apply_object_alpha(Room& room, const std::string& lname, float a) {
    apply_object_alpha(room, room.mesh.object_range(lname), a);
}

// `shared_white` is owned by Gpu, not the room — missing-texture batches store
// it in binds, so it must not be released here.
static void free_room(Room& r, WGPUBindGroup shared_white) {
    if (r.vbuf) wgpuBufferRelease(r.vbuf);
    if (r.ibuf) wgpuBufferRelease(r.ibuf);
    for (WGPUBindGroup b : r.binds) if (b && b != shared_white) wgpuBindGroupRelease(b);
    for (WGPUTextureView v : r.views) wgpuTextureViewRelease(v);
    for (WGPUTexture t : r.textures) wgpuTextureRelease(t);
    r = Room{};
}

// Load a room's geometry, textures and SEN into fresh GPU resources.
// What this visit's live scripts did to the room's objects (0x16B / 0x16C), by lower-case name:
// applied over everything the load-time scripts decided.
struct LiveObjState {
    std::unordered_map<std::string, bool> visible, collide;
    std::vector<ObjLocateArgs> located;     // 0x187 Mesh_Locate'd live (not by the load pass)
    std::unordered_map<std::string, std::string> model;   // a located object's swapped YMO (boss02 §3.4)
};
// A 0x187 Mesh_Locate as a placed object. [H] the rotation operands as degrees (all 0 in S_10).
static SobObject located_object(const std::string& ymo, const std::string& name, float x, float y, float z,
                                float rx, float ry, float rz) {
    SobObject o;
    o.ymo_path = ymo;
    o.name = name;
    o.position = {x, y, z};
    const float k = 0.017453292f;
    o.rotation = {rx * k, ry * k, rz * k};
    o.scale = {1, 1, 1};
    return o;
}
// `donor`: the room being rebuilt (reload_room). A texture it already holds, by resolved path, is taken over
// instead of being read, decoded and uploaded again: that was 23-25 ms of a 37 ms rebuild in S_7080, every time
// a script showed or hid an object. (A bind group keeps its view and texture alive after the donor is freed.)
static Room load_room(Gpu& g, const fs::path& asset_root, const fs::path& sob,
                      const std::int32_t* base_flags = nullptr, int character = 1,
                      const std::unordered_set<std::string>* extra_hidden = nullptr,
                      const LiveObjState* live = nullptr, const Room* donor = nullptr) {
    Room r;
    r.name = sob.stem().string();
    r.dir = sob.parent_path();
    // The room's named trigger boxes, for op 0x13d Chr_IsInEventBox (region-talk triggers).
    // parse_sev returns empty until the .SEV record layout is confirmed, so this is inert now.
    r.event_boxes = load_sev(r.dir / (r.name + ".SEV"));

    // The room's root script at load, run on a throwaway VM seeded with current g_flags +
    // character: what it did to its objects. 0x169 Mesh_Delete removes an object (this is
    // how a room picks the character's chests: S_2005 deletes box_02/box_03 for Yunica and
    // Hugo; audit event_vm #29/#30), 0x16b hides one, 0x16c drops its collision, 0x175
    // poses it (stepping stones UP/STOP, switch TURN_ON/OFF, a looted chest OPEN) and 0x173
    // places it.
    ObjStates obj_states;
    std::vector<SobObject> located;                  // 0x187 Mesh_Locate (render + its X_.YMO collision twin)
    std::unordered_set<std::string> root_hidden;     // deleted or hidden: no model, no collision
    std::unordered_set<std::string> no_collide;      // 0x16c off: drawn, not solid
    std::unordered_set<std::string> gone;            // 0x169 deleted (never a kept touch volume, below)
    {
        fs::path scene_main = sob; scene_main.replace_extension(".XSO");
        if (fs::exists(scene_main)) {
            try {
                Xso sm = Xso::parse(read_bytes(scene_main));
                XsoVm tmp;
                if (base_flags) std::memcpy(tmp.g_flags, base_flags, sizeof(tmp.g_flags));
                tmp.g_flags[150] = character;
                // Follow the root's StartScript children. Their object bookkeeping is part
                // of the room's load-time state: S_1004's root starts StepShadowUpdate,
                // which is what binds each step_NN_shadow to its stone (op 0x189 Mesh_SyncronizePos). Without
                // following it a hidden stone leaves its shadow behind as a black quad.
                // Collected by STATIC disassembly, not the on_start_script sink — that
                // sink is coroutine-only by design ("run-to-completion never spawns"), and
                // this load-time pass is run-to-completion. The child list is static data
                // anyway: 0xb4's operand is a string-pool index.
                std::vector<std::string> children;
                for (const XsoInstr& in : sm.disasm()) {
                    if (in.cls != 2 || in.ops.empty()) continue;
                    if (in.sub == 0xb4) children.push_back(sm.string(in.ops[0]));
                    else if (in.sub == 0xb8 && in.ops.size() >= 2)
                        children.push_back(sm.string(in.ops[0]) + sm.string(in.ops[1]));
                }
                tmp.run(sm, 0, 2'000'000, /*skip_cycles*/true);
                for (const std::string& nm : children) {
                    std::string up = nm;
                    for (char& c : up) c = char(std::toupper((unsigned char)c));
                    const fs::path cp = sob.parent_path() / (up + ".XSO");
                    if (!fs::exists(cp)) continue;
                    try { tmp.run(Xso::parse(read_bytes(cp)), 0, 2'000'000, /*skip_cycles*/true); } catch (const std::exception&) {}
                }
                auto low = [](std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; };
                for (const std::string& s : tmp.obj_deleted) { root_hidden.insert(low(s)); gone.insert(low(s)); }
                for (const auto& [nm, on] : tmp.obj_collide)
                    if (!on) no_collide.insert(low(nm));
                for (const auto& [nm, clip] : tmp.obj_state) obj_states.clip[low(nm)] = clip;
                for (const auto& l : tmp.obj_locate)
                    located.push_back(located_object(l.ymo, low(l.name), l.x, l.y, l.z, l.rx, l.ry, l.rz));
                for (const auto& [nm, p2] : tmp.obj_place)
                    obj_states.pos[low(nm)] = {p2.x, p2.y, p2.z};
                // The root also hides objects outright (S_1004 drops stepping stones
                // 04-06 for Yunica/Hugo). These were being discarded, so those rooms
                // rendered every stone regardless of character.
                for (const auto& [nm, shown] : tmp.obj_visible)
                    if (!shown) root_hidden.insert(low(nm));
                // 0x189 Mesh_SyncronizePos(a, b, x, y, z): a takes b's hidden / no-collision
                // bits and b's position on the gated axes (S_1004's shadows: x and z; each
                // keeps its own y on the floor).
                const Sob sob_objs = tmp.obj_sync.empty() ? Sob{} : Sob::parse(read_bytes(sob));
                for (const auto& [an, sy] : tmp.obj_sync) {
                    const std::string la = low(an), lb = low(sy.from);
                    if (root_hidden.count(lb)) root_hidden.insert(la);
                    if (no_collide.count(lb)) no_collide.insert(la);
                    auto pit = obj_states.pos.find(lb);
                    if (pit != obj_states.pos.end()) {
                        auto sit = obj_states.pos.find(la);
                        std::array<float, 3> p = (sit != obj_states.pos.end()) ? sit->second : pit->second;
                        if (sit == obj_states.pos.end())     // not placed by the root: its SOB spot
                            for (const SobObject& so : sob_objs.objects)
                                if (low(so.name) == la) { p = {so.position.x, so.position.y, so.position.z}; break; }
                        if (sy.x) p[0] = pit->second[0];
                        if (sy.y) p[1] = pit->second[1];
                        if (sy.z) p[2] = pit->second[2];
                        obj_states.pos[la] = p;
                    }
                }
            } catch (const std::exception&) {}
        }
    }

    // Object-variant scripts: some rooms stack several versions of one object at a
    // single spot and let a script choose (S_1000 places save_01/02/03 together).
    // Ops 0x16B/0x16C name the winner; run the script and hide the losers.
    std::unordered_set<std::string> hidden;
    std::vector<Room::LoadEffect> load_fx;
    {
        const fs::path vis = sob.parent_path() / "S_SAVEOBJECTCHANGE.XSO";
        if (fs::exists(vis)) {
            // The script does two things at once: choose the object variant (0x16b/0x16c)
            // and place that variant's idle effect (0x117). Give it a host so the second
            // half is not silently dropped — without one the cleansed statue never glowed.
            struct FxCatcher : XsoHost {
                std::vector<Room::LoadEffect>* out;
                // This host exists only to catch 0x117; the two pure virtuals are inert
                // here (the visibility script spawns no actors and tests no liveness).
                void spawn(int, int, float, float, float, float, int) override {}
                bool is_alive(int) const override { return false; }
                void locate_effect(int id, float x, float y, float z, float a, float b,
                                   const std::string& name) override {
                    for (Room::LoadEffect& e : *out)
                        if (!name.empty() && e.name == name) { e = {id, x, y, z, a, b, name}; return; }
                    out->push_back({id, x, y, z, a, b, name});
                }
            };
            if (fs::exists(vis)) {
                try {
                    XsoVm tmp;
                    FxCatcher fc; fc.out = &load_fx;
                    tmp.host = &fc;
                    if (base_flags) std::memcpy(tmp.g_flags, base_flags, sizeof(tmp.g_flags));
                    tmp.g_flags[150] = character;
                    tmp.run(Xso::parse(read_bytes(vis)));
                    auto low = [](std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; };
                    for (const auto& [name, shown] : tmp.obj_visible)
                        if (!shown) hidden.insert(low(name));
                    for (const auto& [name, on] : tmp.obj_collide)
                        if (!on) no_collide.insert(low(name));
                } catch (const std::exception&) {}
            }
        }
    }
    // Objects the player has already destroyed this session are hidden the same way the
    // statue's losing variants are (a breakable has no opened-flag of its own — @Break_*
    // writes none, so it legitimately respawns on a fresh load, as in retail).
    if (extra_hidden)
        for (const std::string& n : *extra_hidden) hidden.insert(n);
    for (const std::string& n : root_hidden) hidden.insert(n);
    if (live) {
        for (const ObjLocateArgs& l : live->located) {
            std::string ln = l.name;
            for (char& c : ln) c = char(std::tolower((unsigned char)c));
            bool have = false;
            for (const SobObject& o : located) have |= o.name == ln;
            if (!have) located.push_back(located_object(l.ymo, ln, l.x, l.y, l.z, l.rx, l.ry, l.rz));
        }
        for (SobObject& o : located) {                   // a brain's model swap (boss02's BBOSS02*)
            auto it = live->model.find(o.name);
            if (it != live->model.end()) o.ymo_path = it->second;
        }
        for (const auto& [n, on] : live->visible) { if (on) hidden.erase(n); else hidden.insert(n); }
        for (const auto& [n, on] : live->collide) { if (on) no_collide.erase(n); else no_collide.insert(n); }
    }
    // boss08's ctor FUN_00550aa0 runs where the body is located and builds its family there (DARM.md §3.3 item 10
    // [BYTES 0x5514b2..0x551bc8]): the tail and the arms at (B.x, B.y - 999, B.z), the core (moved to (10, 2, -10) by
    // its own run of the ctor) and the arena floor s7099yuka at the origin. Darm's host (view_boss08.inl) drives them.
    // [not modelled] the floor's factory flags 0x8000.
    for (std::size_t i = 0; i < located.size(); ++i) {
        if (located[i].name != "boss08") continue;
        const Vec3 p = located[i].position;
        auto add = [&](const char* ymo, const char* nm, float x, float y, float z) {
            for (const SobObject& o : located) if (o.name == nm) return;
            located.push_back(located_object(ymo, nm, x, y, z, 0.0f, 0.0f, 0.0f));
        };
        add("data\\enemy\\boss08\\boss08t.ymo", "boss08t", p.x, p.y - 999.0f, p.z);
        add("data\\enemy\\boss08\\boss08w.ymo", "boss08w", 10.0f, 2.0f, -10.0f);
        add("data\\enemy\\boss08\\boss08Arm.ymo", "boss08arm", p.x, p.y - 999.0f, p.z);
        add("DATA\\map\\Mapobj\\s7099yuka\\s7099yuka.ymo", "s7099yuka", 0.0f, 0.0f, 0.0f);
        break;
    }
    r.load_effects = std::move(load_fx);
    // A hidden TOUCH object keeps its volume and its trigger (the floor-6 mirrors of S_6009 / S_6014 / S_6082,
    // hidden by their room roots and never 0x16c'd; ROUTE_F47.md B1). The engine: 0x16b sets +0x298 bit 0x20
    // only [EXPORT 00572aa0.c], the draw skips +0x298 & 0x30 [EXPORT 00562990.c:195], and the side-contact
    // trigger asks +0x28c & 8 and +0x298 & 0x80 [EXPORT 0045a510.c:560, 0045a4a0.c] - not the hidden bit.
    // [approx] narrow on purpose: every other hidden object still loses its collision here (S_6080's `kabe`,
    // the medallion doors' `medaru_00`), although the engine keeps bit 0x40 apart for them too.
    if (!hidden.empty()) {
        const Sob so = Sob::parse(read_bytes(sob));
        for (const SobObject& o : so.objects) {
            std::string ln = o.name;
            for (char& c : ln) c = char(std::tolower((unsigned char)c));
            if (!o.touch() || o.script.empty() || !hidden.count(ln) || gone.count(ln) || no_collide.count(ln) ||
                (extra_hidden && extra_hidden->count(ln)))
                continue;
            hidden.erase(ln);
            obj_states.undrawn.insert(ln);
        }
    }
    const bool no_states = obj_states.clip.empty() && obj_states.pos.empty() && obj_states.undrawn.empty();
    r.mesh = build_room(asset_root, sob, nullptr,
                        hidden.empty() ? nullptr : &hidden, base_flags,
                        no_states ? nullptr : &obj_states,
                        located.empty() ? nullptr : &located);
    bake_material_alpha(r.mesh);
    r.base_a.resize(r.mesh.vertices.size());
    for (std::size_t k = 0; k < r.base_a.size(); ++k) r.base_a[k] = r.mesh.vertices[k].a;
    r.base_rgb.resize(r.mesh.vertices.size() * 3);
    for (std::size_t k = 0; k < r.mesh.vertices.size(); ++k) {
        r.base_rgb[k * 3] = r.mesh.vertices[k].r; r.base_rgb[k * 3 + 1] = r.mesh.vertices[k].g; r.base_rgb[k * 3 + 2] = r.mesh.vertices[k].b;
    }
    std::unordered_set<std::string> coll_hidden = hidden;
    coll_hidden.insert(no_collide.begin(), no_collide.end());
    // The hulls take the same load-time poses / placements as the render objects: a twin sits
    // on its render model's posed node (COLLISION.md §1c; S_1004's raised steps).
    r.coll = build_collision(asset_root, sob, nullptr,
                             coll_hidden.empty() ? nullptr : &coll_hidden,
                             located.empty() ? nullptr : &located,
                             no_states ? nullptr : &obj_states);
    // Materials that scroll their UVs. Only 397 of 6642 corpus-wide do, and the disk
    // phase is 0.0 in every one — it is pure runtime state, so we start at zero too.
    for (const DrawBatch& b : r.mesh.batches)
        if ((b.scroll_u != 0 || b.scroll_v != 0) && b.vert_count)
            r.scrolls.push_back({b.first_vert, b.vert_count, b.scroll_u, b.scroll_v, 0, 0});
    if (!r.scrolls.empty()) {
        std::printf("    uv scroll: %zu batches", r.scrolls.size());
        for (const ScrollBatch& sb : r.scrolls)
            std::printf("  [%u verts %+.4f,%+.4f/tick]", sb.vert_count, sb.vel_u, sb.vel_v);
        std::printf("\n");
    }
    {
        std::size_t late = 0;
        for (const DrawBatch& b : r.mesh.batches) late += room_late_layer(b) && b.index_count;
        if (late) std::printf("    layer 0xb: %zu batches (drawn after the actors)\n", late);
        std::size_t pre = 0;
        for (const DrawBatch& b : r.mesh.batches) pre += material_zprepass(b) && b.index_count;
        if (pre) std::printf("    depth pre-pass: %zu batches (FUN_005f8de0)\n", pre);
    }
    // CopyDst: the UV-scroll integrator rewrites scrolling materials' vertices in
    // place each frame via wgpuQueueWriteBuffer, which requires it.
    r.vbuf = make_buffer(g.device, g.queue, r.mesh.vertices.data(),
                         r.mesh.vertices.size() * sizeof(RoomVertex),
                         WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    r.ibuf = make_buffer(g.device, g.queue, r.mesh.indices.data(),
                         r.mesh.indices.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);

    int loaded = 0;
    r.binds.reserve(r.mesh.textures.size());
    r.tex_mips = g_world_mips;
    if (donor && (donor->tex_mips != g_world_mips || donor->binds.size() != donor->mesh.textures.size())) donor = nullptr;
    // mesh.textures are already RESOLVED absolute paths — scene.cpp resolves each
    // ref against its own YMO's directory. Do not reduce to a basename and look it
    // up game-wide: 532 basenames are ambiguous and 530 of those differ in content.
    for (const std::string& path : r.mesh.textures) {
        if (donor) {
            const auto it = std::find(donor->mesh.textures.begin(), donor->mesh.textures.end(), path);
            const WGPUBindGroup have = it == donor->mesh.textures.end() ? nullptr
                                           : donor->binds[std::size_t(it - donor->mesh.textures.begin())];
            if (have && have != g.white_bind) {
                wgpuBindGroupAddRef(have);
                r.binds.push_back(have);
                ++loaded;
                continue;
            }
        }
        Dds dds = load_dds(path, g_world_mips);
        if (dds.ok()) {
            r.binds.push_back(tex_bindgroup(g, upload_texture(g, r, dds), g_world_mips ? g.sampler_mip : nullptr));
            ++loaded;
        }
        else {
            std::printf("    [tex] FAILED to load: %s\n", path.c_str());
            r.binds.push_back(g.white_bind);
        }
    }
    // Untextured submeshes (no texture ref) are a non-rendering DUPLICATE/helper layer
    // sitting ON the real textured floor. S_1000's platform ring carries "stone" (grey)
    // and "none" (black) materials with no texture over the same tiles that also have a
    // textured version; drawing them as flat quads paints a grey/black patch over the
    // stone — the "white staircase" was exactly this (via the old 1x1-white fallback).
    // The retail game doesn't draw them: skipping reveals the clean textured geometry
    // underneath with NO gaps (verified in S_1000, whole floor intact). So we skip any
    // batch with no texture rather than inventing a fill colour for it.
    for (DrawBatch& b : r.mesh.batches)
        if (b.texture < 0) b.index_count = 0;

    // SEN sidecar sits beside the SOB (same stem).
    fs::path sen = sob; sen.replace_extension(".SEN");
    if (fs::exists(sen)) {
        std::ifstream f(vfs::resolve(sen), std::ios::binary);
        std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                    std::istreambuf_iterator<char>()};
        r.sen = Sen::parse(b);
    }
    // SFO sidecar: the room's fog regions.
    fs::path sfo = sob; sfo.replace_extension(".SFO");
    if (fs::exists(sfo)) r.fog = Sfo::parse(read_bytes(sfo));
    // SNF sidecar: the scene light. Only its colour is read here (Scene_LoadStaticScnInf FUN_00434560 [EXPORT
    // 00434560.c:44-88]: the 'SNF\0' block's +0x0c, a D3DCOLOR / 255 -> 0x76a570..78); the block follows the
    // 16-byte file header. [DATA] 0xFFFFFFFF in every shipped room but S_TEST0 (0xFF000000).
    fs::path snf = sob; snf.replace_extension(".SNF");
    if (fs::exists(snf)) {
        const std::vector<std::uint8_t> b = read_bytes(snf);
        if (b.size() >= 0x20 && b[0] == 'S' && b[1] == 'N' && b[2] == 'F')
            r.snf_light = {float(b[0x1e]) / 255.0f, float(b[0x1d]) / 255.0f, float(b[0x1c]) / 255.0f};
    }
    // SCM sidecar: the room's camera regions (the game's authored cameras).
    fs::path scm = sob; scm.replace_extension(".SCM");
    if (fs::exists(scm)) r.cam = Scm::parse(read_bytes(scm));
    // SEF sidecar: scene effects (torch flames, ...).
    fs::path sef = sob; sef.replace_extension(".SEF");
    if (fs::exists(sef)) r.fx = Sef::parse(read_bytes(sef));
    // SCT sidecar: enemy placement + the player's spawn point.
    fs::path sct = sob; sct.replace_extension(".SCT");
    if (fs::exists(sct)) r.cast = Sct::parse(read_bytes(sct));

    std::printf("[%s] %d obj, %zu tris, %d/%zu tex, %zu exits, %zu chests, %zu fog\n",
                r.name.c_str(), r.mesh.objects, r.mesh.indices.size() / 3, loaded,
                r.mesh.textures.size(), r.sen.triggers.size(),
                r.mesh.interactables.size(), r.fog.regions.size());
    {
        int nf = 0;
        for (const SefEffect& e : r.fx.effects) if (e.id == kEffectTorchFlame) ++nf;
        if (!r.fx.effects.empty())
    if (std::getenv("CLERIA_CAM")) {
        // The game's OWN authored eye distances. Useful as a scale check: dist over
        // the player's height says how many character-heights the camera sits back.
        for (std::size_t i = 0; i < r.cam.regions.size(); ++i) {
            const CameraRegion& cr = r.cam.regions[i];
            std::printf("    [cam %zu] eye.mode=%u dist=%.2f pitch=%.3f fov=%.3f\n", i,
                        cr.eye.mode, cr.eye.dist, cr.eye.pitch, cr.fov);
        }
    }
            std::printf("    %zu effects (%d torch flames), %zu cameras\n",
                        r.fx.effects.size(), nf, r.cam.regions.size());
    }
    // CLERIA_COLLCHECK=1: sample a grid of floor heights and compare the collision
    // hulls against the render mesh, so gaps in coverage are visible as a number
    // rather than discovered by falling through a floor in play.
    if (std::getenv("CLERIA_COLLCHECK") && !r.coll.indices.empty()) {
        const int N = 48;
        const float ceil = r.mesh.bmax.y + 1e4f;
        int both = 0, only_render = 0, only_coll = 0, neither = 0;
        double sum_abs = 0.0, worst = 0.0;
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                const float x = r.mesh.bmin.x + (r.mesh.bmax.x - r.mesh.bmin.x) * (i + 0.5f) / N;
                const float z = r.mesh.bmin.z + (r.mesh.bmax.z - r.mesh.bmin.z) * (j + 0.5f) / N;
                const float fr = floor_at(r.mesh, x, z, ceil);
                const float fc = floor_at(r.coll, x, z, ceil);
                const bool hr = fr != kNoFloor, hc = fc != kNoFloor;
                if (hr && hc) {
                    ++both;
                    const double d = std::fabs(double(fr) - double(fc));
                    sum_abs += d; worst = std::max(worst, d);
                } else if (hr) ++only_render;
                else if (hc) ++only_coll;
                else ++neither;
            }
        std::printf("    COLLCHECK %-8s both=%4d only_render=%4d only_coll=%4d none=%4d "
                    "mean|dy|=%.3f worst=%.3f\n",
                    r.name.c_str(), both, only_render, only_coll, neither,
                    both ? sum_abs / both : 0.0, worst);
    }
    if (r.coll.indices.empty()) {
        std::printf("    collision: NO _.YMO twins -> falling back to render geometry\n");
    } else {
        std::printf("    collision: %zu tris (%.1f%% of render), surfaces:",
                    r.coll.indices.size() / 3,
                    100.0 * double(r.coll.indices.size()) / double(std::max<std::size_t>(r.mesh.indices.size(), 1)));
        for (const std::string& s : r.coll.surfaces) std::printf(" %s", s.c_str());
        std::printf("\n");
        // CLERIA_SURF=1: per-surface triangle counts, split by whether the triangle
        // is floor-like (|n.y| > 0.5) — i.e. whether floor_at could ever pick it.
        if (std::getenv("CLERIA_SURF")) {
            std::vector<int> tot(r.coll.surfaces.size(), 0), flat(r.coll.surfaces.size(), 0);
            int unknown = 0;
            for (std::size_t i = 0; i + 2 < r.coll.indices.size(); i += 3) {
                const int s = r.coll.tri_surface[i / 3];
                if (s < 0) { ++unknown; continue; }
                const RoomVertex& a = r.coll.vertices[r.coll.indices[i]];
                const RoomVertex& b = r.coll.vertices[r.coll.indices[i + 1]];
                const RoomVertex& c = r.coll.vertices[r.coll.indices[i + 2]];
                const Vec3 e1{b.px - a.px, b.py - a.py, b.pz - a.pz};
                const Vec3 e2{c.px - a.px, c.py - a.py, c.pz - a.pz};
                Vec3 n = cross(e1, e2);
                const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                ++tot[std::size_t(s)];
                if (len > 1e-9f && std::fabs(n.y / len) > 0.5f) ++flat[std::size_t(s)];
            }
            for (std::size_t s = 0; s < r.coll.surfaces.size(); ++s)
                std::printf("      SURF %-10s tris=%4d floor-like=%4d  id=%d\n",
                            r.coll.surfaces[s].c_str(), tot[s], flat[s],
                            surface_index(r.coll.surfaces[s]));
            if (unknown) std::printf("      SURF (unknown material) tris=%d\n", unknown);
        }
    }
    if (!r.fog.regions.empty()) {
        const FogRegion& f = r.fog.regions[0];
        std::printf("    fog[0] rgb(%.0f,%.0f,%.0f) near=%.0f far=%.0f\n",
                    f.r * 255, f.g * 255, f.b * 255, f.fog_near, f.fog_far);
    }
    for (const SenTrigger& t : r.sen.triggers)
        std::printf("    exit -> %s\n", t.dest_scene.c_str());
    for (const Interactable& it : r.mesh.interactables)
        std::printf("    chest %s -> %s\n", it.name.c_str(),
                    it.script.empty() ? "(no script)"
                                      : fs::path(it.script).filename().string().c_str());
    return r;
}

// Load a standalone YMO (a boss, a prop) into the same GPU objects a room uses.
static Room load_model(Gpu& g, const fs::path& asset_root, const fs::path& ymo) {
    Room r;
    r.name = ymo.stem().string();
    r.dir = ymo.parent_path();
    // CLERIA_CLIP=<name> [+ CLERIA_FRAME=<n>] poses a skinned model at an animation
    // frame instead of its rest pose. Frames are absolute (a clip's range is printed
    // by cleria-animtest --clips); n/60 s of scaled time.
    const char* clip = std::getenv("CLERIA_CLIP");
    const char* fr = std::getenv("CLERIA_FRAME");
    r.mesh = build_model(asset_root, ymo, clip, fr ? float(std::atof(fr)) : 0.0f);
    bake_material_alpha(r.mesh);
    // CopyDst: the UV-scroll integrator rewrites scrolling materials' vertices in
    // place each frame via wgpuQueueWriteBuffer, which requires it.
    r.vbuf = make_buffer(g.device, g.queue, r.mesh.vertices.data(),
                         r.mesh.vertices.size() * sizeof(RoomVertex),
                         WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    r.ibuf = make_buffer(g.device, g.queue, r.mesh.indices.data(),
                         r.mesh.indices.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);
    int loaded = 0;
    // mesh.textures are already RESOLVED absolute paths — scene.cpp resolves each
    // ref against its own YMO's directory. Do not reduce to a basename and look it
    // up game-wide: 532 basenames are ambiguous and 530 of those differ in content.
    for (const std::string& path : r.mesh.textures) {
        Dds dds = load_dds(path, g_world_mips);
        if (dds.ok()) {
            r.binds.push_back(tex_bindgroup(g, upload_texture(g, r, dds), g_world_mips ? g.sampler_mip : nullptr));
            ++loaded;
        }
        else {
            std::printf("    [tex] FAILED to load: %s\n", path.c_str());
            r.binds.push_back(g.white_bind);
        }
    }
    std::printf("[%s] %zu tris, %d/%zu tex, bounds(%.1f,%.1f,%.1f)..(%.1f,%.1f,%.1f)\n",
                r.name.c_str(), r.mesh.indices.size() / 3, loaded, r.mesh.textures.size(),
                r.mesh.bmin.x, r.mesh.bmin.y, r.mesh.bmin.z,
                r.mesh.bmax.x, r.mesh.bmax.y, r.mesh.bmax.z);
    return r;
}

// The world sampler's filter: [graphics] TextureFilter / Anisotropy (FUN_005d4f00, src/cleria/video), set
// by the Video page's video_init before build_pipeline. The default = bilinear, no anisotropy.
static video::SamplerSpec g_world_sampler;

// The world sampler and its mip twin from g_world_sampler: at start (build_pipeline) and again when the Video
// page changes the filter (video_resample). A bind group bakes its sampler in, so the ones already made keep
// the old sampler (alive through their reference) until their room or sprite is unloaded.
static void make_world_samplers(Gpu& g) {
    if (g.sampler) wgpuSamplerRelease(g.sampler);
    if (g.sampler_mip) wgpuSamplerRelease(g.sampler_mip);
    WGPUSamplerDescriptor sad = {};
    sad.addressModeU = sad.addressModeV = sad.addressModeW = WGPUAddressMode_Repeat;
    sad.magFilter = sad.minFilter = g_world_sampler.linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
    if (g_world_sampler.mip_linear) sad.mipmapFilter = WGPUMipmapFilterMode_Linear;
    sad.maxAnisotropy = g_world_sampler.anisotropy;
    g.sampler = wgpuDeviceCreateSampler(g.device, &sad);
    // The mip one ("Smooth distant textures", g_world_mips) blends the levels (trilinear) and, on a linear
    // filter, takes at least 8x anisotropy: trilinear alone blurs a floor seen at a steep angle more than no
    // mips at all. [H] 8.
    WGPUSamplerDescriptor md = sad;
    md.mipmapFilter = WGPUMipmapFilterMode_Linear; md.lodMaxClamp = 32.0f;
    if (g_world_sampler.linear) md.maxAnisotropy = std::max<std::uint16_t>(g_world_sampler.anisotropy, 8);
    g.sampler_mip = wgpuDeviceCreateSampler(g.device, &md);
}

static void build_pipeline(Gpu& g, WGPUTextureFormat color) {
    WGPUShaderSourceWGSL wgsl = {};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL; wgsl.code = sv(kWGSL);
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

    // EXPLICIT bind-group layouts, shared by the world and effect pipelines. Auto
    // ("default") layouts are exclusive to the pipeline that generated them, so bind
    // groups made from one can never be used with another.
    WGPUBindGroupLayoutEntry ue = {};
    ue.binding = 0; ue.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    ue.buffer.type = WGPUBufferBindingType_Uniform; ue.buffer.minBindingSize = 112;
    WGPUBindGroupLayoutDescriptor ubl = {}; ubl.entryCount = 1; ubl.entries = &ue;
    WGPUBindGroupLayout bgl_u = wgpuDeviceCreateBindGroupLayout(g.device, &ubl);

    WGPUBindGroupLayoutEntry te[2] = {};
    te[0].binding = 0; te[0].visibility = WGPUShaderStage_Fragment;
    te[0].texture.sampleType = WGPUTextureSampleType_Float;
    te[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    te[1].binding = 1; te[1].visibility = WGPUShaderStage_Fragment;
    te[1].sampler.type = WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutDescriptor tbl = {}; tbl.entryCount = 2; tbl.entries = te;
    WGPUBindGroupLayout bgl_t = wgpuDeviceCreateBindGroupLayout(g.device, &tbl);

    WGPUBindGroupLayout bgls[2] = {bgl_u, bgl_t};
    WGPUPipelineLayoutDescriptor pld = {};
    pld.bindGroupLayoutCount = 2; pld.bindGroupLayouts = bgls;
    WGPUPipelineLayout play = wgpuDeviceCreatePipelineLayout(g.device, &pld);

    WGPUBlendState blend = {};
    blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero};
    blend.alpha = blend.color;
    WGPUColorTargetState target = {};
    target.format = color; target.blend = &blend; target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = {};
    fs.module = sm; fs.entryPoint = sv("fs"); fs.targetCount = 1; fs.targets = &target;

    WGPUDepthStencilState ds = {};
    ds.format = WGPUTextureFormat_Depth24Plus;
    ds.depthWriteEnabled = WGPUOptionalBool_True; ds.depthCompare = WGPUCompareFunction_Less;
    ds.stencilFront.compare = WGPUCompareFunction_Always;
    ds.stencilBack.compare = WGPUCompareFunction_Always;

    WGPURenderPipelineDescriptor pd = {};
    pd.layout = play;
    pd.vertex.module = sm; pd.vertex.entryPoint = sv("vs");
    pd.vertex.bufferCount = 1; pd.vertex.buffers = &vbl;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = g_front; pd.primitive.cullMode = g_cull;
    pd.depthStencil = &ds; pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFF;
    pd.fragment = &fs;
    g.pipeline = wgpuDeviceCreateRenderPipeline(g.device, &pd);

    // Character/enemy pipeline: same layout, but ALPHA-BLEND with the fs_char shader
    // (real sprite alpha) instead of the room's hard alpha-test. Depth stays
    // test-less/write so sprites occlude against the room; overlapping sprites are few.
    WGPUBlendState cblend = {};
    cblend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
                    WGPUBlendFactor_OneMinusSrcAlpha};
    cblend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One,
                    WGPUBlendFactor_OneMinusSrcAlpha};
    WGPUColorTargetState ctarget = target; ctarget.blend = &cblend;
    WGPUFragmentState cfs = fs; cfs.entryPoint = sv("fs_char"); cfs.targets = &ctarget;
    // An upright billboard's lower half sits at nearly the floor's depth where it stands,
    // so it Z-FIGHTS the floor (the "bottom half disappears" the sprite showed). A
    // negative depth bias pulls the whole character clearly in front of the floor without
    // moving it on screen; the slope term handles the angled floor under it.
    WGPURenderPipelineDescriptor cpd = pd; cpd.fragment = &cfs;  // room depth (Less+write)
    g.char_pipeline = wgpuDeviceCreateRenderPipeline(g.device, &cpd);

    // Additive variant (same fs_char) for glow overlays: the sprite ADDS its colour weighted by
    // alpha, so a cyan glow brightens a row toward teal without masking the baked label under it.
    WGPUBlendState ablend = {};
    ablend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_One};
    ablend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
    WGPUColorTargetState atarget = ctarget; atarget.blend = &ablend;
    WGPUFragmentState afs = cfs; afs.targets = &atarget;
    // An overlay redraws a quad already drawn at the SAME depth (the barrier enemies' red glow
    // over their own sprite): Less + write rejected every pixel, so the glow never showed.
    // LessEqual, no write: it still hides behind walls but passes over its own sprite.
    WGPUDepthStencilState ads = ds;
    ads.depthCompare = WGPUCompareFunction_LessEqual;
    ads.depthWriteEnabled = WGPUOptionalBool_False;
    WGPURenderPipelineDescriptor apd = cpd; apd.fragment = &afs; apd.depthStencil = &ads;
    g.char_pipeline_add = wgpuDeviceCreateRenderPipeline(g.device, &apd);

    // Floor shadow decal: same alpha-over blend + fs_char, but a flat floor quad and
    // DEPTH-WRITE OFF (build/render_re/ENVIRONMENT.md) -- a decal must not punch depth
    // holes, so overlapping shadows and the sprite above still test correctly. The SHADOW
    // texture is black RGB + radial alpha, so fs_char (black*white) outputs (0,0,0,texA)
    // and the blend darkens the floor by up to ~44%.
    WGPUDepthStencilState sds = ds; sds.depthWriteEnabled = WGPUOptionalBool_False;
    WGPUFragmentState sfs = cfs; sfs.entryPoint = sv("fs_shadow");   // black, no fog
    WGPURenderPipelineDescriptor spd = cpd; spd.depthStencil = &sds; spd.fragment = &sfs;
    spd.primitive.cullMode = WGPUCullMode_None;   // a flat floor decal must not be culled
    g.shadow_pipeline = wgpuDeviceCreateRenderPipeline(g.device, &spd);

    // Blended YMO materials: FUN_005f92b0's jump table 0x5fa768 (MATERIAL.md §4) — mode 1
    // SRCALPHA/INVSRCALPHA, 2 SRCALPHA/ONE, 3 SRCALPHA/ONE REVSUBTRACT; COLORWRITEENABLE 7
    // (RGB only: `|8` is set on the opaque path alone). Depth test LessEqual (below), z-write
    // per material (material_zwrite). Modes 2/3 set FOGCOLOR 0 (local_60), so fs_mat_add
    // fogs toward black.
    for (int mode = 1; mode <= 3; ++mode)
        for (int zw = 0; zw < 2; ++zw) {
            WGPUBlendState mb = {};
            mb.color = {mode == 3 ? WGPUBlendOperation_ReverseSubtract : WGPUBlendOperation_Add,
                        WGPUBlendFactor_SrcAlpha,
                        mode == 1 ? WGPUBlendFactor_OneMinusSrcAlpha : WGPUBlendFactor_One};
            mb.alpha = mb.color;
            WGPUColorTargetState mt = target; mt.blend = &mb;
            mt.writeMask = WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue;
            WGPUFragmentState mfs = fs; mfs.targets = &mt;
            mfs.entryPoint = sv(mode == 1 ? "fs_mat" : "fs_mat_add");
            WGPUDepthStencilState mds = ds;
            mds.depthWriteEnabled = zw ? WGPUOptionalBool_True : WGPUOptionalBool_False;
            // D3DRS_ZFUNC is LESSEQUAL (4): its only writer is the state reset FUN_005f1870
            // (0x5f196b). It matters here: a blended subset must pass where its own depth
            // pre-pass (material_zprepass) has just written the same depth.
            mds.depthCompare = WGPUCompareFunction_LessEqual;
            WGPURenderPipelineDescriptor mpd = pd; mpd.fragment = &mfs; mpd.depthStencil = &mds;
            g.mat_pipe[mode][zw] = wgpuDeviceCreateRenderPipeline(g.device, &mpd);
            mpd.primitive.frontFace = WGPUFrontFace_CW; mpd.primitive.cullMode = WGPUCullMode_Back;
            g.cull_mat_pipe[mode][zw] = wgpuDeviceCreateRenderPipeline(g.device, &mpd);
        }
    // FUN_005f8de0's pre-pass (material_zprepass): alpha test texture alpha >= 0xbf, depth
    // LESSEQUAL + write, no blending. It writes destination alpha only (COLORWRITEENABLE 8);
    // nothing in CleriaCore reads destination alpha, so no colour channel is written.
    {
        WGPUColorTargetState zt = target; zt.blend = nullptr; zt.writeMask = WGPUColorWriteMask_None;
        WGPUFragmentState zfs = fs; zfs.targets = &zt; zfs.entryPoint = sv("fs_zpre");
        WGPUDepthStencilState zds = ds; zds.depthCompare = WGPUCompareFunction_LessEqual;
        WGPURenderPipelineDescriptor zpd = pd; zpd.fragment = &zfs; zpd.depthStencil = &zds;
        g.zpre_pipe = wgpuDeviceCreateRenderPipeline(g.device, &zpd);
        zpd.primitive.frontFace = WGPUFrontFace_CW; zpd.primitive.cullMode = WGPUCullMode_Back;
        g.cull_zpre_pipe = wgpuDeviceCreateRenderPipeline(g.device, &zpd);
        // material_cull: D3D's clockwise front face is WebGPU's (both rasterise y-down), so CCW culling is Back here.
        WGPURenderPipelineDescriptor kpd = pd;
        kpd.primitive.frontFace = WGPUFrontFace_CW; kpd.primitive.cullMode = WGPUCullMode_Back;
        g.cull_pipeline = wgpuDeviceCreateRenderPipeline(g.device, &kpd);
    }

    WGPUBufferDescriptor ud = {};
    ud.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst; ud.size = 112;
    g.ubuf = wgpuDeviceCreateBuffer(g.device, &ud);
    WGPUBindGroupEntry ube = {}; ube.binding = 0; ube.buffer = g.ubuf; ube.size = 112;
    WGPUBindGroupDescriptor ubd = {};
    ubd.layout = bgl_u; ubd.entryCount = 1; ubd.entries = &ube;
    g.ubind = wgpuDeviceCreateBindGroup(g.device, &ubd);
    g.tex_layout = bgl_t;

    // Effect pipeline: same vertex layout and bind groups, but additive and
    // depth-read-only, so flames add light without occluding or z-fighting.
    {
        WGPUShaderSourceWGSL fw = {};
        fw.chain.sType = WGPUSType_ShaderSourceWGSL; fw.code = sv(kFxWGSL);
        WGPUShaderModuleDescriptor fd = {}; fd.nextInChain = &fw.chain;
        WGPUShaderModule fm = wgpuDeviceCreateShaderModule(g.device, &fd);
        WGPUBlendState ab = {};
        ab.color = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
        ab.alpha = ab.color;
        WGPUColorTargetState at = {};
        at.format = color; at.blend = &ab; at.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState afs = {};
        afs.module = fm; afs.entryPoint = sv("fs"); afs.targetCount = 1; afs.targets = &at;
        WGPUDepthStencilState ads = ds;
        ads.depthWriteEnabled = WGPUOptionalBool_False;   // read depth, don't write it
        WGPURenderPipelineDescriptor apd = {};
        apd.layout = play;   // the same explicit layout as the world pipeline
        apd.vertex.module = fm; apd.vertex.entryPoint = sv("vs");
        apd.vertex.bufferCount = 1; apd.vertex.buffers = &vbl;
        apd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        apd.primitive.frontFace = g_front; apd.primitive.cullMode = g_cull;
        apd.depthStencil = &ads; apd.multisample.count = 1; apd.multisample.mask = 0xFFFFFFFF;
        apd.fragment = &afs;
        g.fx_pipeline = wgpuDeviceCreateRenderPipeline(g.device, &apd);
        afs.entryPoint = sv("fs_sa");
        g.fx_pipeline_sa = wgpuDeviceCreateRenderPipeline(g.device, &apd);
        // D3DRS_BLENDOP 3 = REVSUBTRACT (result = destination - source), same factors [EXPORT 005eb5b0.c:199-205].
        ab.color.operation = WGPUBlendOperation_ReverseSubtract;
        ab.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_Zero, WGPUBlendFactor_One};
        g.fx_pipeline_sub = wgpuDeviceCreateRenderPipeline(g.device, &apd);
        ab.color = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
        afs.entryPoint = sv("fs_pm");
        g.fx_pipeline_pm = wgpuDeviceCreateRenderPipeline(g.device, &apd);
        wgpuShaderModuleRelease(fm);
    }

    make_world_samplers(g);
    {   // "Character sprites: Crisp" (g_sprite_point above)
        WGPUSamplerDescriptor nd = {};
        nd.addressModeU = nd.addressModeV = nd.addressModeW = WGPUAddressMode_Repeat;
        nd.magFilter = nd.minFilter = WGPUFilterMode_Nearest; nd.maxAnisotropy = 1;
        g.sampler_point = wgpuDeviceCreateSampler(g.device, &nd);
    }

    // Shared 1x1 white for a texture that FAILED to load (a real ref we couldn't
    // resolve) — kept white so it reads as a "missing texture" marker while debugging.
    Room tmp;
    Dds white; white.width = white.height = 1; white.format = Dds::RGBA8;
    white.data = {255, 255, 255, 255};
    g.white_bind = tex_bindgroup(g, upload_texture(g, tmp, white));
    // Shared 1x1 black for a genuinely UNTEXTURED material (no ref at all; scene.cpp
    // returns texture index -1). Those carry a diffuse (0,0,0) in S_1000's 3 "none"
    // submeshes; sampling white made them render as bright white quads (the "white
    // staircase"). Black matches the diffuse and removes the artifact.
    Dds black; black.width = black.height = 1; black.format = Dds::RGBA8;
    black.data = {0, 0, 0, 255};
    g.black_bind = tex_bindgroup(g, upload_texture(g, tmp, black));
    // deliberately leak the 1x1s (they live for the whole run)
    tmp.textures.clear(); tmp.views.clear();

    wgpuShaderModuleRelease(sm);
}

// The room's current fog, from its SFO region data (see src/cleria/sfo.cpp). The
// game cross-fades between regions rather than popping; `g_fog` is the live,
// interpolated state that gets uploaded.
struct FogState {
    float r = 0, g = 0, b = 0;
    float near_ = 1, far_ = 1e9f;
    bool on = false;
};
static FogState g_fog;
// The scene light's colour (0x7b2f14..1c, copied from 0x76a570..78 every frame by FUN_00427c90): c16.rgb =
// lightColour x the material's diffuse for EVERY material, lit or not [EXPORT 005f92b0.c:123-145], and the actor
// sprites' vertex colours take it too (FUN_00473c60's callers; build/render_re/SCREEN_EFFECTS.md §4.3). The game
// frame sets it; white (the scene reset's default) everywhere else. [not modelled] the lit models' N.L / hemisphere.
static Vec3 g_scene_light{1.0f, 1.0f, 1.0f};

static void write_mvp(Gpu& g, const Mat4& mvp) {
    float u[28];
    std::memcpy(u, mvp.m.data(), 64);
    u[16] = g_scene_light.x; u[17] = g_scene_light.y; u[18] = g_scene_light.z; u[19] = 1.0f;
    u[20] = g_fog.r; u[21] = g_fog.g; u[22] = g_fog.b; u[23] = 1.0f;
    u[24] = g_fog.near_; u[25] = g_fog.far_; u[26] = g_fog.on ? 1.0f : 0.0f; u[27] = 0.0f;
    wgpuQueueWriteBuffer(g.queue, g.ubuf, 0, u, sizeof(u));
}

// Advance every scrolling material by `ticks` and rewrite its vertices' UVs.
//
// The engine keeps a phase per material and uploads it as c1.xy at draw time; we do
// not have a per-draw uniform, so we fold the phase into the vertex UVs and re-upload
// just the affected ranges. Only 397 of 6642 materials scroll, and a room has a
// handful of such batches, so this is a few hundred bytes a frame.
static void advance_scroll(Gpu& g, Room& room, int ticks) {
    // Batches whose vertices follow one another go out in one write (S_7099 has 132 scrolling batches: 132 writes a
    // frame were 1.1 ms of GPU wait). Only the scrolled vertices are written, as before.
    static std::vector<RoomVertex> run;
    std::size_t run_v0 = 0;
    auto flush = [&] {
        if (!run.empty())
            wgpuQueueWriteBuffer(g.queue, room.vbuf, run_v0 * sizeof(RoomVertex), run.data(), run.size() * sizeof(RoomVertex));
        run.clear();
    };
    for (ScrollBatch& sb : room.scrolls) {
        sb.phase_u += sb.vel_u * float(ticks);
        sb.phase_v += sb.vel_v * float(ticks);
        // Keep the phase bounded; UVs wrap, so whole turns are free to discard. The
        // engine lets it grow, but float precision decays over a long session.
        sb.phase_u -= std::floor(sb.phase_u);
        sb.phase_v -= std::floor(sb.phase_v);
        if (std::size_t(sb.first_vert) + sb.vert_count > room.mesh.vertices.size()) continue;
        if (run.empty() || run_v0 + run.size() != sb.first_vert) { flush(); run_v0 = sb.first_vert; }
        const std::size_t at = run.size();
        run.insert(run.end(), room.mesh.vertices.begin() + sb.first_vert,
                   room.mesh.vertices.begin() + sb.first_vert + sb.vert_count);
        for (std::size_t k = at; k < run.size(); ++k) { run[k].u += sb.phase_u; run[k].v += sb.phase_v; }
    }
    flush();
}

// part 0: the whole room (headless / backgrounds). part 1: the room's layers 3 and 5 (the
// frame's first pass, it clears). part 2: layer 0xb only, loaded over the actors drawn
// between (room_late_layer).
static void draw(Gpu& g, const Room& room, WGPUTextureView color, WGPUTextureView depth, int part = 0) {
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = part == 2 ? WGPULoadOp_Load : WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {0.055, 0.067, 0.086, 1.0};
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = depth; da.depthLoadOp = part == 2 ? WGPULoadOp_Load : WGPULoadOp_Clear;
    da.depthStoreOp = WGPUStoreOp_Store;
    da.depthClearValue = 1.0f;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, room.vbuf, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, room.ibuf, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    // Opaque first, then the blended materials: a scene object queues its opaque subsets
    // (pass bit 1) on render layer 3 and its blended (bit 4) and additive (bit 0x10) ones
    // on layer 5 (FUN_00561f40 @0x562186..0x5621e3; the room itself: layers 2 / 5,
    // FUN_005053e0). Layer 5's per-object distance sort is [not modelled]: batch order.
    for (int blended = 0; blended < 2; ++blended) {
        for (std::size_t bi = 0; bi < room.mesh.batches.size(); ++bi) {
            const DrawBatch& b = room.mesh.batches[bi];
            if (b.index_count == 0) continue;
            if (part != 0 && room_late_layer(b) != (part == 2)) continue;
            const float ka = bi < room.batch_alpha.size() ? room.batch_alpha[bi] : 1.0f;   // the object's alpha
            if (ka <= 0.0f) continue;                       // FUN_005faca0: nothing at a = 0
            const int mode = material_blend_mode(b, b.mat_alpha * ka);
            if ((mode != 0) != (blended != 0)) continue;
            // A resolved (or failed-load) texture is in room.binds; a genuinely untextured
            // material (index -1) renders its dark diffuse via black_bind, not white.
            WGPUBindGroup tb = (b.texture >= 0 && b.texture < int(room.binds.size()))
                                   ? room.binds[b.texture] : g.black_bind;
            wgpuRenderPassEncoderSetBindGroup(pass, 1, tb, 0, nullptr);
            // The depth pre-pass, right before the subset's own draw (FUN_005fb0e0 0x5fb45a).
            // Only for a blended draw: an opaque one (the d13 override) writes a superset of
            // that depth itself.
            const bool cull = material_cull(b);
            if (blended && material_zprepass(b)) {
                wgpuRenderPassEncoderSetPipeline(pass, cull ? g.cull_zpre_pipe : g.zpre_pipe);
                wgpuRenderPassEncoderDrawIndexed(pass, b.index_count, 1, b.first_index, 0, 0);
            }
            if (blended)
                wgpuRenderPassEncoderSetPipeline(pass, (cull ? g.cull_mat_pipe : g.mat_pipe)[mode][material_zwrite(b) ? 1 : 0]);
            else
                wgpuRenderPassEncoderSetPipeline(pass, cull ? g.cull_pipeline : g.pipeline);
            wgpuRenderPassEncoderDrawIndexed(pass, b.index_count, 1, b.first_index, 0, 0);
        }
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);
}

// A depth-only pass that clears `v` to far (1.0).
static void clear_depth(Gpu& g, WGPUTextureView v) {
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = v; da.depthLoadOp = WGPULoadOp_Clear; da.depthStoreOp = WGPUStoreOp_Store;
    da.depthClearValue = 1.0f;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 0; rp.depthStencilAttachment = &da;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);
}

static WGPUTextureView make_depth(Gpu& g, std::uint32_t w, std::uint32_t h) {
    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment; td.dimension = WGPUTextureDimension_2D;
    td.size = {w, h, 1}; td.format = WGPUTextureFormat_Depth24Plus;
    td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTextureView v = wgpuTextureCreateView(t, nullptr);
    wgpuTextureRelease(t);
    // CLEAR to far (1.0) immediately: a fresh depth texture's contents are undefined
    // (in practice zero = NEAREST), so any overlay that draws with loadOp Load before
    // a clearing pass — the dialogue box, the statue menu — silently depth-fails every
    // quad. A depth-only clearing pass here makes every make_depth safe to Load from.
    clear_depth(g, v);
    return v;
}

// Vertical field of view (radians). The game's 2.5D view is MUCH flatter than a
// typical 60-degree game camera — nearly orthographic. kCamDist is paired with this
// to hold the character's on-screen size: change one and you must change the other
// (dist is proportional to 1/tan(fov/2)).
// This is only a STARTING value for free-fly. In play mode `update_camera` overwrites
// it every frame from the room's SCM camera region (`g_fov = g_cam.fov`), so the game's
