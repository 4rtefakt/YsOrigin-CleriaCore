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
    WGPURenderPipeline char_pipeline{}; // alpha-blended character/enemy sprites
    WGPURenderPipeline char_pipeline_add{}; // additive variant, for glow overlays (Topic selection wash)
    WGPURenderPipeline shadow_pipeline{}; // floor shadow decals (alpha-over, no depth write)
    WGPUBuffer ubuf{};
    WGPUBindGroup ubind{};
    WGPUSampler sampler{};
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
    RoomMesh mesh;
    std::vector<ScrollBatch> scrolls;  // materials with flag 0x4 / nonzero velocity
    RoomMesh coll;           // the `_.YMO` collision hulls — what physics collides against
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
    td.format = fmt; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);

    WGPUTexelCopyTextureInfo dst = {}; dst.texture = t; dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = ((dds.width + block - 1) / block) * blk_bytes;
    layout.rowsPerImage = (dds.height + block - 1) / block;
    WGPUExtent3D ext = {dds.width, dds.height, 1};
    wgpuQueueWriteTexture(g.queue, &dst, dds.data.data(), dds.data.size(), &layout, &ext);

    WGPUTextureView view = wgpuTextureCreateView(t, nullptr);
    room.textures.push_back(t);
    room.views.push_back(view);
    return view;
}

static WGPUBindGroup tex_bindgroup(Gpu& g, WGPUTextureView view) {
    WGPUBindGroupEntry e[2] = {};
    e[0].binding = 0; e[0].textureView = view;
    e[1].binding = 1; e[1].sampler = g.sampler;
    WGPUBindGroupDescriptor d = {};
    d.layout = g.tex_layout; d.entryCount = 2; d.entries = e;
    return wgpuDeviceCreateBindGroup(g.device, &d);
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
};
static Room load_room(Gpu& g, const fs::path& asset_root, const fs::path& sob,
                      const std::int32_t* base_flags = nullptr, int character = 1,
                      const std::unordered_set<std::string>* extra_hidden = nullptr,
                      const LiveObjState* live = nullptr) {
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
    std::unordered_set<std::string> root_hidden;     // deleted or hidden: no model, no collision
    std::unordered_set<std::string> no_collide;      // 0x16c off: drawn, not solid
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
                // which is what binds each step_NN_shadow to its stone (op 0x188). Without
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
                tmp.run(sm);
                for (const std::string& nm : children) {
                    std::string up = nm;
                    for (char& c : up) c = char(std::toupper((unsigned char)c));
                    const fs::path cp = sob.parent_path() / (up + ".XSO");
                    if (!fs::exists(cp)) continue;
                    try { tmp.run(Xso::parse(read_bytes(cp))); } catch (const std::exception&) {}
                }
                auto low = [](std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; };
                for (const std::string& s : tmp.obj_deleted) root_hidden.insert(low(s));
                for (const auto& [nm, on] : tmp.obj_collide)
                    if (!on) no_collide.insert(low(nm));
                for (const auto& [nm, clip] : tmp.obj_state) obj_states.clip[low(nm)] = clip;
                for (const auto& [nm, p2] : tmp.obj_place)
                    obj_states.pos[low(nm)] = {p2.x, p2.y, p2.z};
                // The root also hides objects outright (S_1004 drops stepping stones
                // 04-06 for Yunica/Hugo). These were being discarded, so those rooms
                // rendered every stone regardless of character.
                for (const auto& [nm, shown] : tmp.obj_visible)
                    if (!shown) root_hidden.insert(low(nm));
                // A drop shadow (0x188) inherits its owner's fate: hidden with it, and
                // moved with it (the shadow keeps its own Y — it lies on the floor).
                for (const auto& [sh, owner] : tmp.obj_shadow_of) {
                    const std::string ls = low(sh), lo = low(owner);
                    if (root_hidden.count(lo)) root_hidden.insert(ls);
                    auto pit = obj_states.pos.find(lo);
                    if (pit != obj_states.pos.end()) {
                        auto sit = obj_states.pos.find(ls);
                        const float keepY = (sit != obj_states.pos.end()) ? sit->second[1]
                                                                         : pit->second[1];
                        obj_states.pos[ls] = {pit->second[0], keepY, pit->second[2]};
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
        for (const auto& [n, on] : live->visible) { if (on) hidden.erase(n); else hidden.insert(n); }
        for (const auto& [n, on] : live->collide) { if (on) no_collide.erase(n); else no_collide.insert(n); }
    }
    r.load_effects = std::move(load_fx);
    r.mesh = build_room(asset_root, sob, nullptr,
                        hidden.empty() ? nullptr : &hidden, base_flags,
                        (obj_states.clip.empty() && obj_states.pos.empty()) ? nullptr : &obj_states);
    std::unordered_set<std::string> coll_hidden = hidden;
    coll_hidden.insert(no_collide.begin(), no_collide.end());
    r.coll = build_collision(asset_root, sob, nullptr,
                             coll_hidden.empty() ? nullptr : &coll_hidden);
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
    // CopyDst: the UV-scroll integrator rewrites scrolling materials' vertices in
    // place each frame via wgpuQueueWriteBuffer, which requires it.
    r.vbuf = make_buffer(g.device, g.queue, r.mesh.vertices.data(),
                         r.mesh.vertices.size() * sizeof(RoomVertex),
                         WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    r.ibuf = make_buffer(g.device, g.queue, r.mesh.indices.data(),
                         r.mesh.indices.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);

    int loaded = 0;
    r.binds.reserve(r.mesh.textures.size());
    // mesh.textures are already RESOLVED absolute paths — scene.cpp resolves each
    // ref against its own YMO's directory. Do not reduce to a basename and look it
    // up game-wide: 532 basenames are ambiguous and 530 of those differ in content.
    for (const std::string& path : r.mesh.textures) {
        Dds dds = load_dds(path);
        if (dds.ok()) { r.binds.push_back(tex_bindgroup(g, upload_texture(g, r, dds))); ++loaded; }
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
        std::ifstream f(sen, std::ios::binary);
        std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                    std::istreambuf_iterator<char>()};
        r.sen = Sen::parse(b);
    }
    // SFO sidecar: the room's fog regions.
    fs::path sfo = sob; sfo.replace_extension(".SFO");
    if (fs::exists(sfo)) r.fog = Sfo::parse(read_bytes(sfo));
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
        Dds dds = load_dds(path);
        if (dds.ok()) { r.binds.push_back(tex_bindgroup(g, upload_texture(g, r, dds))); ++loaded; }
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
    WGPURenderPipelineDescriptor apd = cpd; apd.fragment = &afs;
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
        wgpuShaderModuleRelease(fm);
    }

    WGPUSamplerDescriptor sad = {};
    sad.addressModeU = sad.addressModeV = sad.addressModeW = WGPUAddressMode_Repeat;
    sad.magFilter = sad.minFilter = WGPUFilterMode_Linear; sad.maxAnisotropy = 1;
    g.sampler = wgpuDeviceCreateSampler(g.device, &sad);

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

static void write_mvp(Gpu& g, const Mat4& mvp) {
    float u[28];
    std::memcpy(u, mvp.m.data(), 64);
    const Vec3 L = normalize({0.4f, 1.0f, 0.35f});
    u[16] = L.x; u[17] = L.y; u[18] = L.z; u[19] = 1.0f;
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
    for (ScrollBatch& sb : room.scrolls) {
        sb.phase_u += sb.vel_u * float(ticks);
        sb.phase_v += sb.vel_v * float(ticks);
        // Keep the phase bounded; UVs wrap, so whole turns are free to discard. The
        // engine lets it grow, but float precision decays over a long session.
        sb.phase_u -= std::floor(sb.phase_u);
        sb.phase_v -= std::floor(sb.phase_v);
        if (std::size_t(sb.first_vert) + sb.vert_count > room.mesh.vertices.size()) continue;
        static std::vector<RoomVertex> tmp;
        tmp.assign(room.mesh.vertices.begin() + sb.first_vert,
                   room.mesh.vertices.begin() + sb.first_vert + sb.vert_count);
        for (RoomVertex& v : tmp) { v.u += sb.phase_u; v.v += sb.phase_v; }
        wgpuQueueWriteBuffer(g.queue, room.vbuf,
                             std::size_t(sb.first_vert) * sizeof(RoomVertex),
                             tmp.data(), tmp.size() * sizeof(RoomVertex));
    }
}

static void draw(Gpu& g, const Room& room, WGPUTextureView color, WGPUTextureView depth) {
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {0.055, 0.067, 0.086, 1.0};
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = depth; da.depthLoadOp = WGPULoadOp_Clear; da.depthStoreOp = WGPUStoreOp_Store;
    da.depthClearValue = 1.0f;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, g.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, room.vbuf, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, room.ibuf, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    for (const DrawBatch& b : room.mesh.batches) {
        // A resolved (or failed-load) texture is in room.binds; a genuinely untextured
        // material (index -1) renders its dark diffuse via black_bind, not white.
        WGPUBindGroup tb = (b.texture >= 0 && b.texture < int(room.binds.size()))
                               ? room.binds[b.texture] : g.black_bind;
        wgpuRenderPassEncoderSetBindGroup(pass, 1, tb, 0, nullptr);
        wgpuRenderPassEncoderDrawIndexed(pass, b.index_count, 1, b.first_index, 0, 0);
    }
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
    return v;
}

// Vertical field of view (radians). The game's 2.5D view is MUCH flatter than a
// typical 60-degree game camera — nearly orthographic. kCamDist is paired with this
// to hold the character's on-screen size: change one and you must change the other
// (dist is proportional to 1/tan(fov/2)).
// This is only a STARTING value for free-fly. In play mode `update_camera` overwrites
// it every frame from the room's SCM camera region (`g_fov = g_cam.fov`), so the game's
