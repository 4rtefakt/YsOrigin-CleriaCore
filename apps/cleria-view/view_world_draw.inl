// cleria-view — HUD WGSL, enemies, flames, projectiles, located effects
// Textual part of main.cpp (split 2026-07-23, PLAN.md Phase 0): #included by
// main.cpp in a fixed order; ONE translation unit, not standalone. Keep it that way
// until a real TU split is warranted.

// ---- HUD: screen-space text overlay -----------------------------------------
static const char* kHudWGSL = R"WGSL(
struct VsOut { @builtin(position) pos : vec4<f32>, @location(0) uv : vec2<f32>,
               @location(1) col : vec4<f32> };
@group(0) @binding(0) var tex : texture_2d<f32>;
@group(0) @binding(1) var smp : sampler;
@vertex fn vs(@location(0) p : vec2<f32>, @location(1) uv : vec2<f32>,
              @location(2) col : vec4<f32>) -> VsOut {
  var o : VsOut; o.pos = vec4<f32>(p, 0.0, 1.0); o.uv = uv; o.col = col; return o;
}
@fragment fn fs(i : VsOut) -> @location(0) vec4<f32> {
  // Sample atlas RGBA * vertex colour. Works for white glyphs (tinted by col),
  // the solid white cell (= col), and full-colour HUD sprites (col = white).
  return textureSample(tex, smp, i.uv) * i.col;
}
)WGSL";

// ---- enemies ---------------------------------------------------------------
// SCT places them; the cast name maps to a sprite by convention (CAST_M100 ->
// ANI/M_100.AIA, holding for 89/90 casts). Enemies are AIA billboards exactly
// like the player, and share the canonical tag 0x2 = idle.
//
// No AI: the engine drives each enemy from one of ~90 bespoke C++ routines, so
// there is no behaviour table to read. These stand in their placed spots.
struct EnemyFx {
    struct Kind {
        Aia aia;
        std::vector<SpriteTex> anim[8];   // idle animation frames, per facing direction
        std::uint32_t ndir = 0;
        float height = 1.6f;
        // Other animations (walk, attack, hurt, death), decoded on demand and keyed by
        // AIA tag -> 8 facings. Idle stays in `anim` above; this is everything the enemy
        // ASO can ask for beyond it, so an attacking enemy shows its attack clip.
        std::unordered_map<std::uint32_t, std::array<std::vector<SpriteTex>, 8>> byTag;
    };
    std::unordered_map<std::string, Kind> kinds;   // sprite stem -> decoded facings

    // Frames for one animation tag + facing, decoded and cached lazily. Falls back to
    // the idle set when the tag is not in this AIA (so a script asking for a clip the
    // sprite lacks still draws something rather than vanishing).
    const std::vector<SpriteTex>& tag_frames(Gpu& g, Kind& k, std::uint32_t tag,
                                             std::uint32_t dir) {
        if (dir >= 8) dir = 0;
        auto it = k.byTag.find(tag);
        if (it == k.byTag.end()) {
            std::array<std::vector<SpriteTex>, 8> a;
            int img = -1;
            for (std::uint32_t i = 0; i < k.aia.nImg; ++i)
                if (k.aia.images[i].tag == tag) { img = int(i); break; }
            if (img >= 0) {
                const std::uint32_t nd = k.aia.images[img].nDir;
                const std::uint32_t nf = k.aia.images[img].nFrame;
                for (std::uint32_t d = 0; d < 8 && d < nd; ++d)
                    for (std::uint32_t f = 0; f < nf; ++f) {
                        AiaFrame fr;
                        if (!k.aia.decode_frame_composited(std::uint32_t(img), d, f, fr)) continue;
                        SpriteTex st = upload_rgba(g, fr.w, fr.h, fr.rgba.data());
                        st.set_placement(fr, k.aia.canvasW);
                        a[d].push_back(std::move(st));
                    }
            }
            it = k.byTag.emplace(tag, std::move(a)).first;
        }
        const std::vector<SpriteTex>& v = it->second[dir];
        if (!v.empty()) return v;
        if (!it->second[0].empty()) return it->second[0];   // fewer facings shipped
        return k.anim[dir < k.ndir ? dir : 0];              // tag absent -> idle
    }

    // Decode ALL frames of one enemy type's idle animation for EVERY facing, so
    // draw_enemies can both rotate (pick the dir from the enemy's heading) and animate
    // (cycle the frames). Idempotent per stem.
    void load_kind(Gpu& g, const fs::path& asset_root, const std::string& stem) {
        if (stem.empty() || kinds.count(stem)) return;
        Kind k;
        if (!k.aia.load(asset_root / "ANI" / (stem + ".AIA"))) return;
        // Some enemy AIAs (e.g. S_1000's M_810) load with zero images -- skip them rather than
        // index images[0] out of range (was a hard crash on any room that spawns such a cast).
        if (k.aia.nImg == 0 || k.aia.images.empty()) return;
        int img = -1;
        for (std::uint32_t i = 0; i < k.aia.nImg; ++i)
            if (k.aia.images[i].tag == 0x2) { img = int(i); break; }   // idle
        if (img < 0 || img >= int(k.aia.nImg)) img = 0;
        k.ndir = k.aia.images[img].nDir;
        const std::uint32_t nfr = k.aia.images[img].nFrame;
        std::uint32_t tall = 1;
        for (std::uint32_t d = 0; d < 8 && d < k.ndir; ++d)
            for (std::uint32_t f = 0; f < nfr; ++f) {
                AiaFrame fr;
                if (!k.aia.decode_frame_composited(std::uint32_t(img), d, f, fr)) continue;
                SpriteTex st = upload_rgba(g, fr.w, fr.h, fr.rgba.data());
                st.set_placement(fr, k.aia.canvasW);   // canvas anchor, like the player
                k.anim[d].push_back(st);
                tall = std::max(tall, fr.h);
        }
        // Scale off the sprite's own pixel height, against the player's known
        // ~76px / 2.0 world units. Their true size is not established.
        k.height = float(tall) * (2.0f / 76.0f);
        kinds.emplace(stem, std::move(k));
    }

    // Sprites for the SCT placements (the pre-world path; still used headlessly).
    void init(Gpu& g, const fs::path& asset_root, const Sct& cast) {
        for (const CastPlacement& c : cast.casts) load_kind(g, asset_root, c.sprite_stem());
    }

    // Sprites for every cast actually SPAWNED into the world -- which includes op-0x12c
    // scripted casts the SCT never named, so those hub NPCs render too. cast_id ->
    // "M_100" via CastTable::sprite_stem, the same key init() derives from the SCT name.
    void init_from_world(Gpu& g, const fs::path& asset_root, const ActorWorld& world,
                         const CastTable& casts) {
        for (const Actor& a : world.actors())
            load_kind(g, asset_root, casts.sprite_stem(a.cast_id));
    }
};

// A placed enemy at runtime, with the game's own stats.
//
// The old comment here said "the game has no per-enemy HP column — stats are lerped
// from a level curve, so this is our own number until that curve is wired up". Half
// right, and the wrong half mattered: there IS no raw HP column, but there is a
// per-enemy log2 MULTIPLIER, and a stat is K(column) * curve(that enemy's own level).
// The curve alone describes no enemy that exists. See enemies.hpp.
//
// The chain: SCT places a CAST_ name -> CASTINFO gives cast_id -> ENEMYINFO[cast_id]
// gives the level and the multipliers -> GAMEINFO gives the curve.
// A rendered enemy: a real Actor in the ActorWorld, paired with its sprite identity.
// The game-state fields (hp/atk/def/level/pos/alive) live on the Actor, not here — the
// view stopped keeping its own copy so there is ONE spawn+stat path (ActorWorld::spawn),
// not three. `actor` is stable across the room: the view never sweeps, so no erase
// invalidates it (and ActorWorld's list keeps node addresses stable regardless).
struct EnemyRender {
    Actor* actor = nullptr;
    std::string stem;
    std::string display;
    // ASO-driven combat, bound in bind_enemy_aso. When avm has no script the enemy
    // falls back to the old idle-cycling render and never scripts an attack.
    AsoVm avm;
    int atk_section = -1;   // this kind's attack section id, or -1 if none
    int idle_section = 2;   // resting section (2, else 0, else the first present)
    int atk_cd = 0;         // ticks until it may attack again
    int atk_win = 0;        // active-hitbox ticks remaining
    AsoVm::Hitbox hb;
    float atk_dx = 0, atk_dz = 1;  // heading latched when the swing begins
    bool atk_hit = false;   // already connected with the player this swing
    long atk_fkey = -1;     // recovery-stall detection, as the player has
    int atk_stall = 0;
    bool atk_armed = false; // the swing's hit-volume has fired this attack
    int atk_age = 0;        // ticks in the current attack (hard-cap fallback)
};

// Populate an ActorWorld the way the engine does (build/roomload_re/ROOMLOAD.md,
// verified against our own disassembly): run the room's S_xxxx.XSO from entry 0. Its
// opcode 0xa0 bulk-spawns the SCT once (the world's set_placements source); its opcode
// 0x12c adds scripted actors 0..N times. Both funnel through the CASTINFO factory
// (ActorWorld::spawn). The asset-load opcodes (0x9b..0xa1 minus 0xa0) are inert here —
// the view loads geometry its own way. State-gated 0x12c spawns branch on story flags,
// so we seed the VM from the persistent game state; a fresh game reaches the
// fresh-game subset (NOT the 146 static ops), which is faithful.
//
// This is what makes a HUB like S_1000 populate: its SCT holds only the player, and all
// its NPCs come from 0x12c — the SCT-only path saw none of them. If the script is
// missing, falls back to op 0xa0 alone (SCT).
static void populate_room(ActorWorld& world, const Room& room, const std::int32_t* g_flags) {
    world.clear();
    world.set_placements(&room.cast);
    const fs::path xso = room.dir / (room.name + ".XSO");
    if (fs::exists(xso)) {
        XsoVm vm;
        vm.host = &world;
        std::copy(g_flags, g_flags + 0x200, std::begin(vm.g_flags));  // story-gated spawns
        try {
            vm.run(Xso::parse(read_bytes(xso)));
        } catch (const std::exception&) {
            world.clear();
            world.set_placements(&room.cast);
            world.spawn_placements();  // a corrupt script still gets the SCT
        }
    } else {
        world.spawn_placements();  // op 0xa0 alone
    }
}

// Pair each spawned ENEMY actor with its sprite. cast_id -> "M_100" (CastTable::
// sprite_stem), skipping the player and any actor whose sprite did not load. The
// world already holds the game state (hp/pos/level); this record only carries the
// view-side sprite identity.
static std::vector<EnemyRender> build_enemy_render(ActorWorld& world,
                                                   const CastTable& casts,
                                                   const EnemyFx& fx) {
    std::vector<EnemyRender> out;
    for (Actor& a : world.actors()) {
        if (a.is_player()) continue;
        const std::string stem = casts.sprite_stem(a.cast_id);
        if (stem.empty() || !fx.kinds.count(stem)) continue;   // no loaded sprite
        const Cast* c = casts.get(a.cast_id);
        out.push_back({&a, stem, (c && !c->display.empty()) ? c->display : stem});
    }
    return out;
}

// Draw every living enemy as an upright camera-facing billboard, cycling its idle
// animation. `t_anim` is the presentation clock (seconds).
static void draw_enemies(Gpu& g, WGPUTextureView color, WGPUTextureView depth,
                         EnemyFx& fx, const std::vector<EnemyRender>& list,
                         const Vec3& camFwd, const Vec3& camPos, float t_anim,
                         const SpriteTex& shadow) {
    constexpr float kEnemyAnimFps = 8.0f;   // TUNED: real rate is per-enemy ASO Wait
    // Screen basis, matching the player's facing calibration: horizontal camera forward
    // (screen-up) and its right. The enemy's heading is mapped into this basis so the
    // sprite ring rotates with the enemy the same way the player's does.
    Vec3 fwdH = {camFwd.x, 0, camFwd.z};
    const float fl = std::sqrt(fwdH.x * fwdH.x + fwdH.z * fwdH.z);
    if (fl > 1e-4f) { fwdH.x /= fl; fwdH.z /= fl; }
    const Vec3 rt = normalize(cross(Vec3{0, 1, 0}, fwdH));
    for (const EnemyRender& e : list) {
        // Skip DEAD ENEMIES only. Cutscene NPCs (Shion/Roy/…) are non-enemy actors that
        // ActorWorld::spawn leaves at hp=0, so alive() is false for them though they are
        // very much present — gating on alive() alone made every intro NPC invisible.
        if (!e.actor || (e.actor->is_enemy() && !e.actor->alive())) continue;
        if (!e.actor->visible) continue;   // Chr_Alpha faded it out (cutscene visibility)
        auto it = fx.kinds.find(e.stem);
        if (it == fx.kinds.end()) continue;
        EnemyFx::Kind& k = it->second;
        // Floor shadow first (enemies are grounded, so pos.y is the floor).
        draw_shadow(g, color, depth, shadow, e.actor->pos, e.actor->pos.y, 0.6f);
        // Facing: the enemy's heading (yaw, vx=sin/vz=cos convention) mapped to the same
        // 8-dir ring the player uses -- up->dir2 (back), right->dir0, down->dir6 (front).
        // A chasing enemy faces the player (~the camera), so it mostly shows its front.
        const Vec3 hd = {std::sin(e.actor->yaw), 0, std::cos(e.actor->yaw)};
        const float sUp = hd.x * fwdH.x + hd.z * fwdH.z;
        const float sRt = hd.x * rt.x + hd.z * rt.z;
        const float deg = std::atan2(sUp, sRt) * 57.29578f;
        int dir = int(std::lround((deg + 360.0f) / 45.0f)) & 7;
        const std::vector<SpriteTex>* frames = nullptr;
        int fi = 0;
        if (e.avm.ok()) {
            // ASO-driven: the running section's anime_Pattern picks the clip (tag) and the
            // VM the frame, so an attacking enemy shows its attack animation, an idle one
            // its idle, all in step with the hit-volume the same script arms.
            const std::vector<SpriteTex>& fr =
                fx.tag_frames(g, k, std::uint32_t(e.avm.anim_tag()), std::uint32_t(dir));
            if (!fr.empty()) {
                frames = &fr;
                const int n = int(fr.size());
                fi = (e.avm.frame() % n + n) % n;
            }
        }
        if (!frames) {  // scriptless enemy: cycle idle, offset per instance
            if (dir >= int(k.ndir) || k.anim[dir].empty()) dir = 0;
            if (k.anim[dir].empty()) continue;
            frames = &k.anim[dir];
            const int n = int(frames->size());
            fi = ((int(t_anim * kEnemyAnimFps) + e.actor->instance_id * 2) % n + n) % n;
        }
        const SpriteTex* t = &(*frames)[fi];
        if (!t->tex) continue;
        // Flash white on a recent hit (hit_flash counts down 6..0 in the tick).
        const float flash = e.actor->hit_flash > 0 ? 0.8f * float(e.actor->hit_flash) / 6.0f : 0.0f;
        draw_character(g, color, depth, e.actor->pos, k.height, *t, camFwd, camPos, flash);
        // Event-bound (group-1..4) enemies GLOW their marker colour, ADDED to their own sprite and
        // pulsing by |sin(phase)| — the engine's FUN_00473c60 mechanism (build/effect_re/EF2_RENDER.md
        // §B): marker ARGB, alpha = intensity, per-enemy phase. Group 1 = 0xFFE00000 (red 224). We
        // reproduce it as a second ADDITIVE pass of the same sprite tinted by RGB*(A*|sin|/255).
        static const std::uint32_t kGroupColour[4] = {0xFFE00000u, 0xFF00D000u, 0xFF0000FFu, 0xFF00C0C0u};
        for (int grp = 1; grp <= 4; ++grp) {
            if (!e.actor->tag.in_group(grp)) continue;
            const std::uint32_t mk = kGroupColour[grp - 1];
            const float A = float((mk >> 24) & 0xFF) / 255.0f;
            const float mr = float((mk >> 16) & 0xFF) / 255.0f;
            const float mg = float((mk >> 8) & 0xFF) / 255.0f;
            const float mb = float(mk & 0xFF) / 255.0f;
            const float phase = float(e.actor->instance_id) * 2.4f + t_anim * 6.2831853f;  // per-enemy offset
            // Keep a floor under the pulse so a ward seed always reads as glowing (it never
            // fully fades to 0), and make the pulse a touch stronger — the retail ward glow
            // is subtle but always present.
            const float I = A * (0.45f + 0.55f * std::fabs(std::sin(phase)));               // pulsing intensity
            draw_character(g, color, depth, e.actor->pos, k.height, *t, camFwd, camPos,
                           0.0f, mr * I, mg * I, mb * I, /*additive*/true);
            break;
        }
    }
}

// ---- scene effects: the torch flames ---------------------------------------
// SEF gives the placements; the effect id resolves through eftlist to an EF2, and
// effect 200 (`map\syokudai_honou.eft`, the torch flame) draws a camera-facing
// billboard textured with naga01.dds — a 4x4 flipbook of 16 frames. The EF2's
// emitter 1 carries scale (1, 1.5, 1), so the quad is 1 x 1.5 world units.
//
// Honest limits: the EF2's animation tracks are normalised, and the field holding
// the absolute duration was never identified, so the frame RATE below is a choice,
// not data. Blend mode is additive on strong evidence but was not confirmed in code.
struct FlameFx {
    SpriteTex tex;
    static constexpr int kCols = 4, kRows = 4, kFrames = 16;
    static constexpr float kW = 1.0f, kH = 1.5f;   // EF2 emitter-1 scale
    static constexpr float kFps = 15.0f;           // NOT from the data — see above

    bool init(Gpu& g, const fs::path& asset_root) {
        const fs::path p = asset_root / "EFFECT" / "EFT" / "YSF_USED" / "NAGA_G" / "H" / "NAGA01.DDS";
        Dds d = load_dds(p);
        if (!d.ok()) return false;
        tex = upload_rgba(g, d.width, d.height, d.data.data());
        return true;
    }
};

// Draw every torch flame in the room, in one additive pass over the room's depth.
static void draw_flames(Gpu& g, const Room& room, WGPUTextureView color,
                        WGPUTextureView depth, const FlameFx& fx, const Vec3& camFwd,
                        float t) {
    if (!fx.tex.tex || room.fx.effects.empty()) return;
    const int frame = int(t * FlameFx::kFps) % FlameFx::kFrames;
    const float du = 1.0f / FlameFx::kCols, dv = 1.0f / FlameFx::kRows;
    const float u0 = float(frame % FlameFx::kCols) * du;
    const float v0 = float(frame / FlameFx::kCols) * dv;

    const Vec3 up = {0, 1, 0};
    const Vec3 right = normalize(cross(up, camFwd));
    std::vector<RoomVertex> verts;
    std::vector<std::uint32_t> idx;
    for (const SefEffect& e : room.fx.effects) {
        if (e.id != kEffectTorchFlame) continue;
        // Anchored at the SEF point, growing upward like a flame on a stand.
        const Vec3 c = {e.position.x, e.position.y + FlameFx::kH * 0.5f, e.position.z};
        const std::uint32_t b = std::uint32_t(verts.size());
        auto V = [&](float sx, float sy, float u, float v) {
            verts.push_back({c.x + right.x * sx * FlameFx::kW * 0.5f,
                             c.y + sy * FlameFx::kH * 0.5f,
                             c.z + right.z * sx * FlameFx::kW * 0.5f,
                             0, 0, -1, u, v, 255, 255, 255, 255});
        };
        V(-1, 1, u0, v0);      V(1, 1, u0 + du, v0);
        V(1, -1, u0 + du, v0 + dv); V(-1, -1, u0, v0 + dv);
        for (std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(b + k);
    }
    if (idx.empty()) return;

    WGPUBuffer vb = make_buffer(g.device, g.queue, verts.data(),
                                verts.size() * sizeof(RoomVertex), WGPUBufferUsage_Vertex);
    WGPUBuffer ib = make_buffer(g.device, g.queue, idx.data(),
                                idx.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = depth; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, g.fx_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, fx.tex.bind, 0, nullptr);
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


// ---- projectiles (ASO anime_FireBullet -> an EF2 effect) --------------------
//
// A bullet is not an actor: the engine spawns a lightweight object whose only
// renderable is one EF2 effect instance, and it lives exactly as long as that
// effect does (build/projectile_re/PROJECTILE.md). So the visual is the real
// shipped effect — arg[1] is an eftlist id, and Hugo's 130 is
// pc\pc02\PC2_DEF_ATTACK.eft. Every textured emitter of the effect is drawn,
// rather than guessing which one is "the" bullet.
struct BulletArt {
    struct Layer {
        SpriteTex tex;
        std::vector<Ef2Frame> frames;   // UV rect per frame, from the EF2
        float w = 1, h = 1;             // emitter scale
        // Ring primitive (EF2_RENDER.md §A): a flat horizontal annulus in world XZ, not a
        // billboard — the kekkai ward. Segments + radii + Y come straight from the EF2.
        bool ring = false;
        int ring_segs = 0;
        float ring_outer = 0, ring_inner = 0, ring_height = 0;
    };
    std::vector<Layer> layers;
    int life = 60;                      // ticks; the effect's own lifetime
    bool ok() const { return !layers.empty(); }
};

// id -> art, loaded once. Effects that fail to resolve cache as empty so a missing
// one is not retried every shot.
static const BulletArt& bullet_art(Gpu& g, const fs::path& asset_root, const EftList& efl,
                                   const EftTex& eft, int effect_id) {
    static std::unordered_map<int, BulletArt> cache;
    auto it = cache.find(effect_id);
    if (it != cache.end()) return it->second;
    BulletArt& a = cache[effect_id];
    const fs::path p = efl.path(asset_root, effect_id);
    if (p.empty()) return a;
    const bool dbg = std::getenv("CLERIA_BARRIER") != nullptr;
    if (dbg) std::printf("  [fx] effect %d -> EF2 %s\n", effect_id, p.filename().string().c_str());
    const Ef2 ef = Ef2::load(p);
    if (!ef.ok()) return a;
    a.life = int(ef.life() > 0 ? ef.life() : 60);
    for (const Ef2Emitter& em : ef.emitters()) {
        if (!em.textured()) continue;   // frame_count 0 draws no quad at all
        const fs::path tp = eft.path(asset_root, int(em.frames[0].tex_id));
        if (dbg) std::printf("        emitter tex_id=%d -> %s%s\n", int(em.frames[0].tex_id),
                             tp.empty() ? "(unresolved)" : tp.filename().string().c_str(),
                             em.ring ? "  [ring/cylinder]" : "  [billboard]");
        if (tp.empty()) continue;
        Dds d = load_dds(tp);
        if (!d.ok()) continue;
        BulletArt::Layer L;
        L.tex = upload_rgba(g, d.width, d.height, d.data.data());
        L.frames = em.frames;
        L.w = em.sx;
        L.h = em.sy;
        L.ring = em.ring;
        L.ring_segs = em.ring_segs;
        L.ring_outer = em.ring_outer;
        L.ring_inner = em.ring_inner;
        L.ring_height = em.ring_height;
        a.layers.push_back(L);
    }
    return a;
}

struct Projectile {
    Vec3 pos{};
    Vec3 dir{1, 0, 0};
    float speed = 0, accel = 0, gravity = 0;
    float scale = 1;
    int effect_id = 0;
    int age = 0, life = 60;
    bool from_player = true;
    std::unordered_set<int> hit;   // per-bullet hit dedup, as melee does
    bool alive() const { return age < life; }
};

// One tick of flight. The engine integrates speed then re-derives heading from the
// resulting velocity, so gravity curves the bullet AND re-aims it.
static void step_projectile(Projectile& p) {
    p.speed += p.accel;
    Vec3 v = {p.dir.x * p.speed, p.dir.y * p.speed, p.dir.z * p.speed};
    v.y -= p.gravity;
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l > 1e-6f) {
        p.dir = {v.x / l, v.y / l, v.z / l};
        p.speed = l;
    }
    p.pos.x += v.x; p.pos.y += v.y; p.pos.z += v.z;
    ++p.age;
}

static void draw_projectiles(Gpu& g, const std::vector<Projectile>& shots,
                             const fs::path& asset_root, const EftList& efl, const EftTex& eft,
                             WGPUTextureView color, WGPUTextureView depth, const Vec3& camFwd) {
    if (shots.empty()) return;
    const Vec3 up = {0, 1, 0};
    const Vec3 right = normalize(cross(up, camFwd));
    // One draw per texture: group by layer so each keeps its own bind group.
    for (const Projectile& s : shots) {
        const BulletArt& art = bullet_art(g, asset_root, efl, eft, s.effect_id);
        if (!art.ok()) continue;
        for (const BulletArt::Layer& L : art.layers) {
            if (L.frames.empty()) continue;
            const int fi = std::min(int(L.frames.size()) - 1,
                                    int(std::size_t(s.age) * L.frames.size() /
                                        std::size_t(std::max(1, art.life))));
            const Ef2Frame& fr = L.frames[std::size_t(fi)];
            const float hw = L.w * s.scale * 0.5f, hh = L.h * s.scale * 0.5f;
            RoomVertex verts[4];
            std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
            auto V = [&](int n, float sx, float sy, float u, float v) {
                verts[n] = {s.pos.x + right.x * sx * hw, s.pos.y + sy * hh,
                            s.pos.z + right.z * sx * hw, 0, 0, -1, u, v, 255, 255, 255, 255};
            };
            V(0, -1, 1, fr.u0, fr.v0);  V(1, 1, 1, fr.u1, fr.v0);
            V(2, 1, -1, fr.u1, fr.v1);  V(3, -1, -1, fr.u0, fr.v1);
            WGPUBuffer vb = make_buffer(g.device, g.queue, verts, sizeof(verts),
                                        WGPUBufferUsage_Vertex);
            WGPUBuffer ib = make_buffer(g.device, g.queue, idx, sizeof(idx),
                                        WGPUBufferUsage_Index);
            WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
            WGPURenderPassColorAttachment ca = {};
            ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDepthStencilAttachment da = {};
            da.view = depth; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor rp = {};
            rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
            rp.depthStencilAttachment = &da;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
            wgpuRenderPassEncoderSetPipeline(pass, g.fx_pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, L.tex.bind, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0,
                                                WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexed(pass, 6, 1, 0, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
            wgpuQueueSubmit(g.queue, 1, &cmd);
            wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
            wgpuBufferRelease(vb); wgpuBufferRelease(ib);
        }
    }
}

// Persistent script-placed effects (op 0x11a): the barrier ward and friends. Each is one
// eftlist effect looping forever at a fixed world position until op 0x119 removes it. Every
// emitter is drawn ADDITIVE (its EF2 blend word, EF2_RENDER.md §A.3). Orientation comes from
// the DATA per emitter, no knobs:
//  - RING emitters (B2+0x00 bit 0x8, e.g. map_kekkai emitter 6) are a procedural CYLINDER WALL
//    in the world: two edge-rings at the SAME radius but +/-height/2 in Y, so the visible band
//    is vertical (FUN_0044c250). Vertex i uses the (s-c, s+c) basis (radius x sqrt2), textured
//    u-around / v-across, white verts (colour is the additive texture).
//  - other emitters are upright camera-facing billboards (the ward's central glow).
// World size = emitter scale (EF2 B1+0x28) x the locate op's effect scale (e.a). [radius/height
// scale split x/z vs y]. The emitter-scale->world-matrix compose is [H-high] per EF2_RENDER.md.
static void draw_located_effects(Gpu& g, const std::vector<ActorWorld::LocatedEffect>& fx,
                                 const fs::path& asset_root, const EftList& efl, const EftTex& eft,
                                 WGPUTextureView color, WGPUTextureView depth,
                                 const Vec3& camFwd, float t) {
    if (fx.empty()) return;
    Vec3 fwdH = {camFwd.x, 0, camFwd.z};
    const float fl = std::sqrt(fwdH.x * fwdH.x + fwdH.z * fwdH.z);
    if (fl > 1e-4f) { fwdH.x /= fl; fwdH.z /= fl; }
    const Vec3 rightH = normalize(cross(Vec3{0, 1, 0}, fwdH));
    auto submit = [&](const std::vector<RoomVertex>& verts, const std::vector<std::uint32_t>& idx,
                      WGPUBindGroup tex) {
        if (idx.empty()) return;
        WGPUBuffer vb = make_buffer(g.device, g.queue, verts.data(),
                                    verts.size() * sizeof(RoomVertex), WGPUBufferUsage_Vertex);
        WGPUBuffer ib = make_buffer(g.device, g.queue, idx.data(),
                                    idx.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPURenderPassColorAttachment ca = {};
        ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDepthStencilAttachment da = {};
        da.view = depth; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor rp = {};
        rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderSetPipeline(pass, g.fx_pipeline);   // additive
        wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, tex, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, std::uint32_t(idx.size()), 1, 0, 0, 0);
        wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(g.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
        wgpuBufferRelease(vb); wgpuBufferRelease(ib);
    };
    for (const ActorWorld::LocatedEffect& e : fx) {
        const BulletArt& art = bullet_art(g, asset_root, efl, eft, e.id);
        if (!art.ok()) continue;
        const float scale = e.a > 0.01f ? e.a : 1.0f;   // the locate op's effect scale (op arg a)
        const int age = int(t * 60.0f) % std::max(1, art.life);
        for (const BulletArt::Layer& L : art.layers) {
            if (L.frames.empty()) continue;
            const int fi = std::min(int(L.frames.size()) - 1,
                                    int(std::size_t(age) * L.frames.size() /
                                        std::size_t(std::max(1, art.life))));
            const Ef2Frame& fr = L.frames[std::size_t(fi)];
            std::vector<RoomVertex> verts;
            std::vector<std::uint32_t> idx;
            if (L.ring && L.ring_segs >= 1) {
                // Cylinder wall: N segments, top ring at +hh (radius Rtop), bottom at -hh (Rbot).
                const int N = L.ring_segs;
                const float radXZ = L.w * scale;         // emitter sx * effect scale (radius)
                const float radY = L.h * scale;          // emitter sy * effect scale (band height)
                const float Rtop = L.ring_inner, Rbot = L.ring_outer, hh = L.ring_height;
                verts.reserve(std::size_t(N + 1) * 2);
                for (int i = 0; i <= N; ++i) {
                    const float a = float(i) * 6.2831853f / float(N);
                    const float s = std::sin(a), c = std::cos(a);
                    const float u = fr.u0 + (fr.u1 - fr.u0) * (float(i) / float(N));  // around the ring
                    verts.push_back({e.x + (s - c) * Rtop * radXZ, e.y + hh * radY,
                                     e.z + (s + c) * Rtop * radXZ, 0, 1, 0, u, fr.v0, 255, 255, 255, 255});
                    verts.push_back({e.x + (s - c) * Rbot * radXZ, e.y - hh * radY,
                                     e.z + (s + c) * Rbot * radXZ, 0, 1, 0, u, fr.v1, 255, 255, 255, 255});
                }
                idx.reserve(std::size_t(N) * 6);
                for (int i = 0; i < N; ++i) {
                    const std::uint32_t t0 = std::uint32_t(2 * i), b0 = t0 + 1, t1 = t0 + 2, b1 = t0 + 3;
                    idx.insert(idx.end(), {t0, b0, t1, t1, b0, b1});
                }
            } else {
                // Upright, camera-facing additive billboard: the ward's central glow. The EF2
                // emitter's sx/sy is the particle EMISSION AREA (up to ~3.5x5 units here), not a
                // single sprite — we have no particle system, so a full-area quad reads as one
                // huge bright blob. Approximate the retail look — a contained column rising from
                // the item — by (a) anchoring the quad's BOTTOM at the placed floor Y instead of
                // centring it (a centred quad sinks half below the floor), and (b) shrinking the
                // emission area toward a glow-sized column. kGlowArea is [HAND-TUNED] against the
                // retail ward (reference/realgame/yt/hugo_p2_ward_barrier.jpg) — a stand-in until
                // a real EF2 particle emitter exists, NOT an RE'd value.
                constexpr float kGlowArea = 0.34f;
                const float hw = L.w * scale * 0.5f * kGlowArea, ht = L.h * scale * kGlowArea;
                const Vec3 c = {e.x, e.y, e.z};
                auto V = [&](float rx, float uy01, float u, float v) -> RoomVertex {
                    return {c.x + rightH.x * rx * hw, c.y + uy01 * ht, c.z + rightH.z * rx * hw,
                            0, 1, 0, u, v, 255, 255, 255, 255};
                };
                // uy01: 0 = floor (c.y), 1 = top (c.y + ht) — the glow stands on the ground.
                verts = {V(-1, 1, fr.u0, fr.v0), V(1, 1, fr.u1, fr.v0),
                         V(1, 0, fr.u1, fr.v1), V(-1, 0, fr.u0, fr.v1)};
                idx = {0, 1, 2, 0, 2, 3};
            }
            submit(verts, idx, L.tex.bind);
        }
    }
}

