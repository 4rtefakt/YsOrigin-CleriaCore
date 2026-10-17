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
// A captured frame (the CrossFade): its alpha channel is whatever the 3D passes left there, so
// only the vertex alpha counts.
@fragment fn fs_opaque(i : VsOut) -> @location(0) vec4<f32> {
  return vec4<f32>(textureSample(tex, smp, i.uv).rgb * i.col.rgb, i.col.a);
}
)WGSL";

// ---- enemies ---------------------------------------------------------------
// SCT places them; the cast name maps to a sprite by convention (CAST_M100 ->
// ANI/M_100.AIA, holding for 89/90 casts). Enemies are AIA billboards exactly
// like the player, and share the canonical tag 0x2 = idle.
//
// No AI: the engine drives each enemy from one of ~90 bespoke C++ routines, so
// there is no behaviour table to read. These stand in their placed spots.
//
// The stem an actor's script and sheet are read under: a mod's puppet sprite, else the factory's ASO when it is not
// the cast's own (+0x24c: cast 0xa6 under the School Outfit, EXTRA_VERSIONS.md §1.4), else the cast's.
static std::string actor_stem(const Actor& a, const CastTable& casts) {
    if (!a.sprite_override.empty()) return a.sprite_override;
    if (!a.aso_override.empty()) return a.aso_override;
    return casts.sprite_stem(a.cast_id);
}
struct EnemyFx {
    struct Kind {
        Aia aia;
        std::vector<SpriteTex> anim[8];   // idle animation frames, per facing direction
        std::uint32_t ndir = 0;
        float height = 1.6f;
        float footFix = 0;   // per-file foot correction (oy - resting bbox bottom); see draw_character
        // Other animations (walk, attack, hurt, death), decoded on demand and keyed by
        // AIA tag -> 8 facings. Idle stays in `anim` above; this is everything the enemy
        // ASO can ask for beyond it, so an attacking enemy shows its attack clip.
        // A frame is decoded when it is first shown (`got`): a tag's eight facings at once were a 30-37 ms frame
        // at an enemy's first attack / first hurt, one whole facing still up to 28 (CLERIA_PROF, S_3013, S_5014,
        // S_1002). img: the tag's image record, -1 = this AIA has none, -2 = not looked up yet.
        struct TagSet { std::array<std::vector<SpriteTex>, 8> dir; std::array<std::vector<std::uint8_t>, 8> got; int img = -2; };
        std::unordered_map<std::uint32_t, TagSet> byTag;
    };
    std::unordered_map<std::string, Kind> kinds;   // sprite stem -> decoded facings

    fs::path root;                       // the asset root of the last load_kind (sheet() loads from it)
    std::unordered_set<std::string> no_sheet;   // sheets that did not load: not asked for again

    // The sheet of an AIA file index (an actor's +0x97c; aso_aia_name), loaded at its first use like the engine's
    // slot (FUN_00444850). nullptr = no such index or file: the caller keeps the cast's own sheet.
    Kind* sheet(Gpu& g, std::int32_t file) {
        const char* nm = aso_aia_name(file);
        if (!*nm) return nullptr;
        auto it = kinds.find(nm);
        if (it == kinds.end() && !root.empty() && !no_sheet.count(nm)) {
            load_kind(g, fs::path(root), nm);
            it = kinds.find(nm);
            if (it == kinds.end()) no_sheet.insert(nm);
        }
        return it == kinds.end() ? nullptr : &it->second;
    }

    // The frame `want` (clamped to the clip, FUN_00444dc0) of one animation tag + facing, decoded and cached
    // lazily. A tag the file lacks takes the file's fallback entry, as FUN_00444dc0 does (slot +0x18: the first
    // entry whose tag is 0 [EXPORT 00444a50.c:97-101]; [H] entry 0 in a file with none, the field's initial
    // value, ANI_VARIANT.md §8.2); nullptr when there is nothing to show.
    // [approx] a frame that fails to decode is an empty texture in its place (it used to be dropped from the
    // clip, the later frames moving up).
    const SpriteTex* tag_frame(Gpu& g, Kind& k, std::uint32_t tag, std::uint32_t dir, int want) {
        if (dir >= 8) dir = 0;
        Kind::TagSet& ts = k.byTag[tag];
        if (ts.img == -2) {
            ts.img = -1;
            for (std::uint32_t i = 0; i < k.aia.nImg; ++i)
                if (k.aia.images[i].tag == tag) { ts.img = int(i); break; }
            if (ts.img < 0 && k.aia.nImg && !k.aia.images.empty()) {
                ts.img = 0;
                for (std::uint32_t i = 0; i < k.aia.nImg; ++i)
                    if (k.aia.images[i].tag == 0) { ts.img = int(i); break; }
            }
        }
        const std::uint32_t nd = ts.img >= 0 ? k.aia.images[ts.img].nDir : 0;
        const std::uint32_t nf = ts.img >= 0 ? k.aia.images[ts.img].nFrame : 0;
        if (nd == 0 || nf == 0) {                            // an empty record -> idle
            const std::vector<SpriteTex>& v = k.anim[k.ndir ? dir % k.ndir : 0];
            return v.empty() ? nullptr : &v[std::size_t(std::max(0, std::min(want, int(v.size()) - 1)))];
        }
        std::vector<SpriteTex>& v = ts.dir[dir];
        if (v.empty()) { v.resize(nf); ts.got[dir].assign(nf, 0); }
        const std::size_t fi = std::size_t(std::max(0, std::min(want, int(nf) - 1)));
        if (!ts.got[dir][fi]) {
            ts.got[dir][fi] = 1;
            AiaFrame fr;
            // FUN_00444dc0: a direction past the record's nDir is taken modulo it (C_101's
            // 2-direction tags; PLAYTEST_1.md §2).
            if (k.aia.decode_frame_composited(std::uint32_t(ts.img), dir % nd, std::uint32_t(fi), fr)) {
                v[fi] = upload_rgba(g, fr.w, fr.h, fr.rgba.data(), g_sprite_point ? g.sampler_point : nullptr);
                v[fi].set_placement(fr, k.aia.canvasW, k.aia.canvasH, k.aia.scaleA);
                v[fi].footFix = k.footFix;   // same per-file foot correction as idle
            }
        }
        return &v[fi];
    }

    // Decode ALL frames of one enemy type's idle animation for EVERY facing, so
    // draw_enemies can both rotate (pick the dir from the enemy's heading) and animate
    // (cycle the frames). Idempotent per stem.
    void load_kind(Gpu& g, const fs::path& asset_root, const std::string& stem) {
        if (stem.empty() || kinds.count(stem)) return;
        root = asset_root;
        Kind k;
        // A stem that is a whole path is a mod's own sheet (world.form{sprite =}, API 7): read where it is.
        if (!k.aia.load(fs::path(stem).is_absolute() ? fs::path(stem) : asset_root / "ANI" / (stem + ".AIA"))) return;
        // Some enemy AIAs are 40-byte stubs with no images (M_810 / M_820: Hugo and Toal as
        // duel opponents). Their ASO selects the real set with anime_File (the player's
        // C_1xx), so follow it [H: the factory's ASO index picks the same file]; otherwise skip
        // them rather than index images[0] out of range.
        if (k.aia.nImg == 0 || k.aia.images.empty()) {
            Aso a = Aso::load(asset_root / "ANI" / (stem + ".ASO"));
            std::string alt;
            if (a.ok() && !a.sections().empty()) {
                AsoVm vm;
                vm.bind(&a);
                if (vm.set_section(a.find(2) ? 2 : a.sections().front().id, /*force*/true))
                    for (int t = 0; t < 8 && vm.file_id() <= 0; ++t) vm.tick();
                alt = aso_aia_name(vm.file_id());
            }
            if (alt.empty() || !k.aia.load(asset_root / "ANI" / (alt + ".AIA"))) return;
            if (k.aia.nImg == 0 || k.aia.images.empty()) return;
            std::printf("  [sprite] %s: stub AIA, anime_File -> %s\n", stem.c_str(), alt.c_str());
        }
        int img = -1;
        for (std::uint32_t i = 0; i < k.aia.nImg; ++i)
            if (k.aia.images[i].tag == 0x2) { img = int(i); break; }   // idle
        if (img < 0 || img >= int(k.aia.nImg)) img = 0;
        k.ndir = k.aia.images[img].nDir;
        const std::uint32_t nfr = k.aia.images[img].nFrame;
        // Per-file foot correction (CUTSCENE_POLISH_RE.md §2): a large canvas can place the
        // foot line `oy` below the character art (M_840 256px: oy=180 vs art bottom ~172), so
        // the sprite floats. footFix = oy - resting bbox bottom; subtracting it in
        // draw_character grounds the resting frame, and being ONE constant per file it adds
        // no slide. Measured on the idle resting frame (image `img`, dir 0, frame 0).
        { AiaFrame rest;
          if (k.aia.decode_frame_composited(std::uint32_t(img), 0, 0, rest))
              k.footFix = rest.bill[2] - float(rest.y0 + rest.h); }
        std::uint32_t tall = 1;
        for (std::uint32_t d = 0; d < 8 && d < k.ndir; ++d)
            for (std::uint32_t f = 0; f < nfr; ++f) {
                AiaFrame fr;
                if (!k.aia.decode_frame_composited(std::uint32_t(img), d, f, fr)) continue;
                SpriteTex st = upload_rgba(g, fr.w, fr.h, fr.rgba.data(), g_sprite_point ? g.sampler_point : nullptr);
                st.set_placement(fr, k.aia.canvasW, k.aia.canvasH, k.aia.scaleA);   // canvas anchor, like the player
                st.footFix = k.footFix;
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
            load_kind(g, asset_root, actor_stem(a, casts));
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
    // The specular tint struct +0x260 (hit_feedback.hpp Tint): anime_ChangeSpecular writes it — the attack
    // wind-up flash (build/mech_re/ASO_NOOPS.md §1) — and FUN_0045a3a0 steps it once per actor tick, before the
    // VM runs [EXPORT 0045a510.c:144 vs 890]. spec_event applies what the VM's last tick issued.
    hitfx::Tint spec;
    void spec_event() {
        if (const AsoVm::SpecularEvent* s = avm.specular()) { spec.a = s->a; spec.b = s->b; spec.t = 0.0f; spec.step = s->step; spec.count = s->count; }
    }
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
    // This cast's ENEMYINFO row (the AI block itself lives on the Actor: actor->ai).
    EnemyAiParams aip;
    float facing = 0;       // +0x314, the visible facing, ENGINE basis (cos, sin)
    int loco = -1;          // last locomotion section requested (table 0x69ba48)
    int motion_applied = -1;  // the Chr_Animate motion (+0x994) last set on avm (CHR_OPS.md §4)
    int fix_loco = 2;         // the script puppet's locomotion state +0x3a0, as its section (idle 2 / walk 0 / run 1)
    int step_timer = 0;       // +0x4bf, the surface-footstep countdown of an actor with Chr_FootSound on
    unsigned step_n = 0;      // +0x4c0, its step counter
    bool hit = false;       // landed hit this tick (+0x3bc & 4), read by the next AI tick
    bool dmg_hit = false;   // ...and it did >= 1 damage (+0x3bc & 2)
    std::uint32_t events = 0;   // ...and the stun-rolling bits of +0x3bc (0x2000 / 0x4000; LAUNCH_STATE.md §0)
    float origin_h = 1.075f;  // +0x324: this cast's origin height above its floor
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

// The sprite's camera-relative direction row (+0x9b4, what the AIA renderer draws): the
// actor's facing (yaw, vx=sin/vz=cos) mapped onto the 8-way ring the player uses —
// up -> dir2 (back), right -> dir0, down -> dir6 (front). Brains read it (M460's formation).
static int sprite_dir_of(float yaw, const Vec3& camFwd) {
    Vec3 fwdH = {camFwd.x, 0, camFwd.z};
    const float fl = std::sqrt(fwdH.x * fwdH.x + fwdH.z * fwdH.z);
    if (fl > 1e-4f) { fwdH.x /= fl; fwdH.z /= fl; }
    const Vec3 rt = normalize(cross(Vec3{0, 1, 0}, fwdH));
    const Vec3 hd = {std::sin(yaw), 0, std::cos(yaw)};
    const float sUp = hd.x * fwdH.x + hd.z * fwdH.z;
    const float sRt = hd.x * rt.x + hd.z * rt.z;
    const float deg = std::atan2(sUp, sRt) * 57.29578f;
    return int(std::lround((deg + 360.0f) / 45.0f)) & 7;
}

// The AiWorld a brain reaches other actors through, implemented with the host's lambdas.
struct FnAiWorld : AiWorld {
    std::function<void(const std::function<void(Actor&)>&)> each;
    std::function<Actor*(int)> find_fn;
    std::function<Actor*(Actor&, int, const Vec3&, int, bool)> spawn_fn;
    std::function<void(Actor&)> remove_fn;
    std::function<Ray(const Vec3&, const Vec3&)> ray_fn;
    std::function<bool(const Vec3&, float)> sphere_fn;
    std::function<float(const Vec3&, float)> water_under_fn;   // the edge probe's set-1 test (ENEMY_WATER.md W3)
    float water_under(const Vec3& o, float depth) override { return water_under_fn ? water_under_fn(o, depth) : -1.0e30f; }
    std::function<void(float*)> arena_fn;
    std::function<const float*(int)> raw_fn;
    std::function<void(int, int)> flag_fn;
    void set_flag(int i, int v) override { if (flag_fn) flag_fn(i, v); }
    std::function<int(int)> get_flag_fn;                 // g_flags[i] (the duel handlers' 130 / 131 / 231)
    std::function<Actor*()> player_fn;
    std::function<void()> hide_gauge_fn;
    int flag(int i, int fallback) override { return get_flag_fn ? get_flag_fn(i) : fallback; }
    Actor* player() override { return player_fn ? player_fn() : nullptr; }
    void hide_boss_gauge() override { if (hide_gauge_fn) hide_gauge_fn(); }
    std::function<bool(const char*)> in_room_fn;        // the scene record's path test (M860's S_3080 rule)
    bool in_room(const char* dir) override { return in_room_fn && in_room_fn(dir); }
    std::function<void(const std::function<void(const Fx&)>&)> effects_fn;   // the GoEffect pool (view_duel.inl)
    void for_each_effect(const std::function<void(const Fx&)>& f) override { if (effects_fn) effects_fn(f); }
    std::function<int(Actor&, int, const int*, int, int, int)> break_fn;   // M860's pillar break (view_duel.inl)
    int break_shields(Actor& a, int shield, const int* ids, int n, int impact, int se) override {
        return break_fn ? break_fn(a, shield, ids, n, impact, se) : 0;
    }
    std::function<void(Actor&, int, int)> cull_fx_fn;   // M710's attached-effect cull
    void cull_attached_fx(Actor& a, int id, int keep) override { if (cull_fx_fn) cull_fx_fn(a, id, keep); }
    std::function<void(int, float)> camera_fn;          // the forced camera region (M360)
    void force_camera(int r, float rate) override { if (camera_fn) camera_fn(r, rate); }
    std::function<void(int, const Vec3&)> sound_fn;     // a positional SE (M360's crystal kills)
    void sound_at(int id, const Vec3& p) override { if (sound_fn) sound_fn(id, p); }
    std::function<void(int, float)> sound_2d_fn;
    void sound_2d(int id, float vol) override { if (sound_2d_fn) sound_2d_fn(id, vol); }
    std::function<void(int, int)> stop_sound_fn;
    void stop_sound(int id, int ms) override { if (stop_sound_fn) stop_sound_fn(id, ms); }
    std::function<void(Actor&, int, int)> restat_fn;    // FUN_004202e0 (M360's INIT)
    void restat(Actor& a, int c, int lv) override { if (restat_fn) restat_fn(a, c, lv); }
    std::function<void(const std::function<void(const BulletRef&)>&)> bullets_fn;   // the GoXx bullets (M420)
    void for_each_bullet(const std::function<void(const BulletRef&)>& f) override { if (bullets_fn) bullets_fn(f); }
    void for_each(const std::function<void(Actor&)>& f) override { each(f); }
    Actor* find(int id) override { return id ? find_fn(id) : nullptr; }
    Actor* spawn_child(Actor& p, int c, const Vec3& d, int lv, bool k) override { return spawn_fn(p, c, d, lv, k); }
    void remove(Actor& a) override { remove_fn(a); }
    Ray ray(const Vec3& o, const Vec3& dl) override { return ray_fn(o, dl); }
    bool sphere_hits(const Vec3& c, float r) override { return sphere_fn(c, r); }
    void arena(float r[4]) override { arena_fn(r); }
    const float* enemy_raw(int c) override { return raw_fn(c); }
};

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
        const std::string stem = actor_stem(a, casts);   // a mod's puppet, cast 0xa6's C_980, the cast's own
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
                         const SpriteTex& shadow, ShadowFrame* sf = nullptr, const CastTable* casts = nullptr) {
    constexpr float kEnemyAnimFps = 8.0f;   // TUNED: real rate is per-enemy ASO Wait
    // Screen basis, matching the player's facing calibration: horizontal camera forward
    // (screen-up) and its right. The enemy's heading is mapped into this basis so the
    // sprite ring rotates with the enemy the same way the player's does.
    Vec3 fwdH = {camFwd.x, 0, camFwd.z};
    const float fl = std::sqrt(fwdH.x * fwdH.x + fwdH.z * fwdH.z);
    if (fl > 1e-4f) { fwdH.x /= fl; fwdH.z /= fl; }
    const Vec3 rt = normalize(cross(Vec3{0, 1, 0}, fwdH));
    // With a ShadowFrame (the game frame): every shadow first, by SHADOWS.md 2 -- retail draws them on layer 8,
    // the bodies after -- then the decals, then the sprites. Without one (headless): the old flat disc per enemy.
    if (sf) {
        {
            QuadScope quads(g);
            for (const EnemyRender& e : list) {
                if (!e.actor || (e.actor->is_enemy() && !e.actor->alive() &&
                                 (e.actor->destroy_requested() || !(revive::corpse_drawn(*e.actor) || e.actor->ai.dead_fade)))) continue;
                // FUN_00473540 queues the shadow when (+0x12c & 1) == 0 and +0x4d8 != 0 [BYTES 0x47385f..0x47387f].
                if (!e.actor->visible || e.actor->ai.alpha <= 0.0f || !fx.kinds.count(e.stem) || (e.actor->flags12c & 1u)) continue;
                const Actor& a = *e.actor;
                const float fy = a.flyer ? a.floor_y : a.pos.y;
                const float size = casts && a.cast_id >= 0 && std::size_t(a.cast_id) < casts->casts.size()
                                       ? casts->casts[std::size_t(a.cast_id)].shadow_size : 0.6f;
                // [approx] +0x2b8 (floor within 4 below) from the kept floor height; an enemy's quad is not tilted.
                // FUN_00459410's cast switch (+0x124) gives casts 0x26 / 0x29 / 0x2a / 0x3a +0x130 |= 0x40000 and an rgb
                // in +0x290 [EXPORT 00459410.c:245-288]: their shadow is the light2 under-glow.
                const std::uint32_t glow = a.cast_id == 0x26 ? 0xffffffu : a.cast_id == 0x29 || a.cast_id == 0x2a ? 0xff8000u
                                         : a.cast_id == 0x3a ? 0x4000ffu : 0u;
                shadow_actor(g, color, depth, shadow, *sf, a.pos, fy, size, a.tint_alpha * float(a.tint >> 24) / 255.0f,
                             shadow_decal_ok(*sf, {a.pos.x, fy + 0.05f, a.pos.z}, false, a.flags12c, a.pos.y - fy <= 4.0f),
                             {0, 1, 0}, glow != 0, glow);
            }
        }
        shadow_flush(g, color, depth, shadow, *sf);
    }
    QuadScope quads(g);                    // every shadow and sprite below in one pass (view_sprites.inl)
    for (const EnemyRender& e : list) {
        // Skip DEAD ENEMIES only. Cutscene NPCs (Shion/Roy/…) are non-enemy actors that
        // ActorWorld::spawn leaves at hp=0, so alive() is false for them though they are
        // very much present — gating on alive() alone made every intro NPC invisible.
        // The revive family's corpse lies on (REVIVE.md §3.4: opaque, never removed without the Chimes); an ordinary
        // monster's body fades out on its dead path (FUN_004780e0: ai.dead_fade, the opacity in ai.alpha).
        if (!e.actor || (e.actor->is_enemy() && !e.actor->alive() &&
                         (e.actor->destroy_requested() || !(revive::corpse_drawn(*e.actor) || e.actor->ai.dead_fade)))) continue;
        if (!e.actor->visible) continue;   // Chr_Alpha faded it out (cutscene visibility)
        // A brain's opacity (+0x4d8, e.g. M390's warp fades): the render submit FUN_00473540
        // skips the actor at exactly 0; otherwise it is drawn at that opacity.
        if (e.actor->ai.alpha <= 0.0f) continue;
        auto it = fx.kinds.find(e.stem);
        if (it == fx.kinds.end()) continue;
        // The sheet is the actor's AIA file +0x97c, one of the decode's four cache keys (file, tag, direction,
        // frame) [BYTES 0x446220..0x44632e]; its scale and placement are the sheet's own (FUN_00446340). An actor
        // without one (no script bound) keeps its cast's sheet.
        EnemyFx::Kind* sheet = fx.sheet(g, e.actor->aia_file);
        EnemyFx::Kind& k = sheet ? *sheet : it->second;
        // Floor shadow first (enemies are grounded, so pos.y is the floor).
        if (!sf && !(e.actor->flags12c & 1u))          // +0x12c bit 1: no shadow (Chr_Shadow 0x153)
        draw_shadow(g, color, depth, shadow, e.actor->pos,
                    e.actor->flyer ? e.actor->floor_y : e.actor->pos.y, 0.6f);   // flyers: on the floor
        // Facing: the enemy's heading (yaw, vx=sin/vz=cos convention) mapped to the same
        // 8-dir ring the player uses -- up->dir2 (back), right->dir0, down->dir6 (front).
        // A chasing enemy faces the player (~the camera), so it mostly shows its front.
        int dir = sprite_dir_of(e.actor->yaw, camFwd);
        const SpriteTex* t = nullptr;
        // Chr_Pattern(id, tag, frame) (FUN_005712e0 -> FUN_00446340 0x446365): the pinned AIA
        // tag and frame override the ASO's; FUN_00444dc0 clamps the frame. A tag this AIA
        // lacks falls through to the ASO [H: the engine uses the AIA's default record].
        if (e.actor->pattern.pinned())
            t = fx.tag_frame(g, k, std::uint32_t(e.actor->pattern.tag), std::uint32_t(dir), e.actor->pattern.frame);
        // ASO-driven: the running section's anime_Pattern picks the clip (tag) and the
        // VM the frame, so an attacking enemy shows its attack animation, an idle one
        // its idle, all in step with the hit-volume the same script arms.
        if (!t && e.avm.ok())
            t = fx.tag_frame(g, k, std::uint32_t(e.avm.anim_tag()), std::uint32_t(dir), int(e.avm.frame()));
        if (!t) {  // scriptless enemy: cycle idle, offset per instance
            if (k.ndir && dir >= int(k.ndir)) dir %= int(k.ndir);   // FUN_00444dc0 dir %= nDir
            if (k.anim[dir].empty()) dir = 0;
            if (k.anim[dir].empty()) continue;
            const int n = int(k.anim[dir].size());
            t = &k.anim[dir][std::size_t(((int(t_anim * kEnemyAnimFps) + e.actor->instance_id * 2) % n + n) % n)];
        }
        if (!t->tex) continue;
        // Its image on a mirror floor, under the shadow's own conditions (view_sprites.inl's mirror_actor_quad).
        if (sf && !(e.actor->flags12c & 1u))
            mirror_actor_quad(*t, e.actor->pos, e.actor->flyer ? e.actor->floor_y : e.actor->pos.y, e.actor->scale,
                              e.actor->tint, e.actor->tint_alpha * float(e.actor->tint >> 24) / 255.0f, camPos, camFwd,
                              mirror_quad_unturned(e.actor->cast_id, e.actor->pattern.pinned() ? int(e.actor->pattern.tag)
                                                                         : e.avm.ok() ? int(e.avm.anim_tag()) : -1,
                                                   e.actor->pattern.pinned() ? int(e.actor->pattern.frame)
                                                                         : e.avm.ok() ? int(e.avm.frame()) : 0));
        // The specular starts from the tint struct +0x260 (anime_ChangeSpecular's wind-up flash; EnemyRender::spec)
        // and the hurt tint REPLACES it (hit_feedback.hpp): while +0x238 runs, FUN_00473c60 sets the sprite's
        // specular to 0x6b3713 [BYTES 0x473ea8..0x473ec5], added after the texture modulate.
        // [not modelled] the glare split of the specular (GlareEffectLevel, ASO_NOOPS.md §1.2).
        // The diffuse word's R, G, B x the scene light's colour [BYTES 0x473a16..0x473a6d]; the specular is not.
        const std::uint32_t tc = e.actor->tint;
        draw_character(g, color, depth, e.actor->pos, k.height * e.actor->scale, *t, camFwd, camPos, 0.0f,
                       float((tc >> 16) & 0xff) / 255.0f * g_scene_light.x, float((tc >> 8) & 0xff) / 255.0f * g_scene_light.y,
                       float(tc & 0xff) / 255.0f * g_scene_light.z, false, e.actor->ai.alpha * e.actor->tint_alpha,
                       tremble_active(e.actor->tremble) ? e.actor->tremble.dx : 0.0f,     // Chr_Tremble
                       tremble_active(e.actor->tremble) ? e.actor->tremble.dy : 0.0f, e.actor->scale,
                       e.actor->hit_flash != 0.0f ? hitfx::kHurtSpecular : e.actor->mod_spec ? e.actor->mod_spec : e.spec.colour(),
                       e.avm.ok() ? e.avm.sprite_depth_offset() : 0.0f,                   // anime_CameraDistanceOffset
                       // The caustics on a body under water (+0x38a && !+0x389 && +0x4ba != 0x12 && WaterCaustics == 0
                       // [EXPORT 00474290.c:96-123]). [approx] an enemy's wading byte is not kept: in_water stands for both.
                       !g_caustic.off && (g_caustic.mode2 || e.actor->ai.in_water) && e.actor->ai.surf_ray != 0x12);
        // A puppet drawn as Toal (the mods', API 7) leaves his trail: its history is the puppet's place at each
        // drawn frame [approx: retail's is per tick], the trail's pictures the ones his own tags have in C_119.
        if (e.actor->sprite_override == "C_113" && e.actor->pattern.pinned()) {
            if (g_puppet_trails.size() > 8 && !g_puppet_trails.count(e.actor->instance_id)) g_puppet_trails.clear();
            ToalTrail& tt = g_puppet_trails[e.actor->instance_id];
            trail_record(e.actor->pos, tt);
            trail_draw(g, color, depth, fx.root, *t, e.actor->pos, k.height * e.actor->scale, e.actor->pattern.tag, dir,
                       e.actor->pattern.frame, camFwd, camPos, g_scene_light.x, g_scene_light.y, g_scene_light.z, 0.0f, 0.0f,
                       e.actor->scale, 0.0f, tt);
        }
        // Event-bound (group-1..4) enemies GLOW their marker colour (FUN_00473c60 [BYTES 0x473ec8..f87],
        // BARRIER_OBJECTS.md §1a): A = marker alpha * |sin(2 pi phase / 255)|, no floor, the phase
        // stepped in the actor tick (so it holds still under a Fix). [H] the composition: the
        // engine adds RGB * A / 255 into the actor's colour word; we draw it as a second additive
        // pass of the same sprite.
        if (const std::uint32_t mk = e.actor->glow_marker) {
            const float I = float((mk >> 24) & 0xFF) / 255.0f *
                            std::fabs(std::sin(float(e.actor->glow_phase) * 6.2831855f / 255.0f));
            const float mr = float((mk >> 16) & 0xFF) / 255.0f;
            const float mg = float((mk >> 8) & 0xFF) / 255.0f;
            const float mb = float(mk & 0xFF) / 255.0f;
            if (I > 0.0f)
                draw_character(g, color, depth, e.actor->pos, k.height * e.actor->scale, *t, camFwd, camPos,
                               0.0f, mr * I, mg * I, mb * I, /*additive*/true, e.actor->ai.alpha,
                               tremble_active(e.actor->tremble) ? e.actor->tremble.dx : 0.0f,
                               tremble_active(e.actor->tremble) ? e.actor->tremble.dy : 0.0f, e.actor->scale, 0,
                               e.avm.ok() ? e.avm.sprite_depth_offset() : 0.0f);
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
        // Emitter scale (EF2 B1+0x28). For a NON-ring emitter the mesh is the unit quad at
        // B1+0x7c — verified to be exactly (±0.5, ±0.5, 0) in every player/effect EF2 — so
        // the world half-size is 0.5*w and 0.5*h. sx/sy is therefore a genuine SPRITE SIZE
        // here, not the particle emission area it is on the ring/ward path; see
        // build/effect_re/EF2_BULLET_ART.md §1. Do NOT apply a kGlowArea-style shrink.
        float w = 1, h = 1;
        // Packed D3D render-state word B2+0x08, bit 0x80: set = additive (DST=ONE),
        // clear = alpha (DST=INVSRCALPHA). EF2_RENDER.md §A.3.
        bool additive = true;
        // B2+0x00 bit 0x100 — EF2_RENDER.md §A.4's candidate "camera-facing" flag. Parsed
        // and carried, but NOT acted on: it does not separate the cases. MAP_KEKKAI's two
        // BARIARING01 ring emitters (7, 8) and SAVE_MEGAMIZOU01's big glow emitters (1, 3,
        // 4) are all mode 0x0001, yet the ward's rings clearly lie FLAT in the retail frame
        // while the statue's glows clearly stand UP. Drawing every 0x100-clear emitter flat
        // reproduced the ward and destroyed the statue. The real discriminator is unknown.
        bool face_cam = true;
        // B1+0x48[2] — this emitter's height offset above the effect's placed origin.
        float hoff = 0;   // B1+0x48[2] * effect scale, added to the effect's Y
        std::uint32_t orient = 0;   // B2+0x44: 0 camera-facing, 1 world-oriented
        bool flat = false;          // quad lies in the world XZ plane (see Ef2Emitter::flat)
        // Ring primitive (EF2_RENDER.md §A): a flat horizontal annulus in world XZ, not a
        // billboard — the kekkai ward. Segments + radii + Y come straight from the EF2.
        bool ring = false;
        int ring_segs = 0;
        float ring_outer = 0, ring_inner = 0, ring_height = 0;
        // This EMITTER's own lifetime in frames (EF2 B1+0x24), which is NOT the effect's.
        // PC2_DEF_ATTACK's emitters run 7/14/10 ticks under 43-tick particle drivers, so
        // drawing every layer for the effect's 43 ticks left the big 14-tick flash on
        // screen three times too long and stretched its 9-frame flipbook to match.
        int life = 60;
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
        L.additive = em.additive;
        L.face_cam = em.face_cam;
        L.hoff = em.off[2];
        L.orient = em.orient;
        L.flat = em.flat;
        L.ring = em.ring;
        L.ring_segs = em.ring_segs;
        L.ring_outer = em.ring_outer;
        L.ring_inner = em.ring_inner;
        L.ring_height = em.ring_height;
        L.life = em.life > 0 ? int(em.life) : 60;
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
    int trail_effect = 0, trail_period = 0;   // GoXx +0x158/+0x15c: a trail puff every N ticks
    std::unordered_set<int> hit;   // per-bullet hit dedup, as melee does
    bool alive() const { return age < life; }
};

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
            // Each emitter dies on its OWN lifetime, and its flipbook is paced against that
            // — not against the effect's (longest) life, which stretched short flashes.
            // Emitters CYCLE for the projectile's life rather than expiring with it. A
            // real particle emitter emits continuously for the effect's duration and each
            // particle lives L.life; with no particle system, treating L.life as a hard
            // cutoff made the shot VANISH mid-flight — PC2_DEF_ATTACK's layers run 7/14/10
            // ticks against the effect's 43, so Hugo's bolt was invisible for its last 29
            // ticks while still travelling and still able to hit. Found with
            // CLERIA_BULLET_PREVIEW.
            const int la = (L.life > 0) ? (s.age % L.life) : s.age;
            const int fi = std::min(int(L.frames.size()) - 1,
                                    int(std::size_t(la) * L.frames.size() /
                                        std::size_t(std::max(1, L.life))));
            const Ef2Frame& fr = L.frames[std::size_t(fi)];
            const float hw = L.w * s.scale * 0.5f, hh = L.h * s.scale * 0.5f;
            // fs_char reads vertex alpha as the hit-flash (lerp to white): an alpha-over layer
            // must pass 0 or it draws as a white silhouette. The additive shader keeps 255.
            const std::uint8_t va = L.additive ? 255 : 0;
            RoomVertex verts[4];
            std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
            if (L.flat) {
                // Same world-oriented rule the located effects use (Ef2Emitter::flat): an
                // emitter that authored a meaningful sz lies in the world plane instead of
                // facing the camera. On a projectile "the world plane" is the plane the shot
                // travels in, so the quad is laid out along the FLIGHT direction (its L.h
                // extent) and across it (L.w) — a trail, not a billboard.
                // PC2_DEF_ATTACK: em2 is AEF_2_00, a 9-frame lightning crackle at 0.5x5.0,
                // which as an axis-aligned billboard stood up as a 5-unit vertical BAR
                // through Hugo's shot. em4 (1,1,1, the AEF_4_02 orb) keeps sz == 1 and stays
                // camera-facing, which is what retail actually shows flying.
                const Vec3 f = normalize(Vec3{s.dir.x, 0, s.dir.z});
                const Vec3 rt = normalize(cross(Vec3{0, 1, 0}, f));
                auto V = [&](int n, float a, float b, float u, float v) {
                    verts[n] = {s.pos.x + rt.x * a * hw + f.x * b * hh, s.pos.y,
                                s.pos.z + rt.z * a * hw + f.z * b * hh,
                                0, 1, 0, u, v, 255, 255, 255, va};
                };
                V(0, -1, 1, fr.u0, fr.v0);  V(1, 1, 1, fr.u1, fr.v0);
                V(2, 1, -1, fr.u1, fr.v1);  V(3, -1, -1, fr.u0, fr.v1);
            } else {
                auto V = [&](int n, float sx, float sy, float u, float v) {
                    verts[n] = {s.pos.x + right.x * sx * hw, s.pos.y + sy * hh,
                                s.pos.z + right.z * sx * hw, 0, 0, -1, u, v, 255, 255, 255, va};
                };
                V(0, -1, 1, fr.u0, fr.v0);  V(1, 1, 1, fr.u1, fr.v0);
                V(2, 1, -1, fr.u1, fr.v1);  V(3, -1, -1, fr.u0, fr.v1);
            }
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
            // Blend comes from the emitter's own render-state word, not from an assumption:
            // additive (fx) when B2+0x08 & 0x80, alpha-over (char) otherwise. Every emitter
            // of PC2_DEF_ATTACK is 0x46c1/0xc6c1 = additive, so Hugo looks the same — but a
            // bullet whose art is alpha-blended no longer renders as a glow.
            wgpuRenderPassEncoderSetPipeline(pass, L.additive ? g.fx_pipeline : g.char_pipeline);
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
        // Age from the effect's OWN spawn, not wall-clock: a one-shot burst (b == 0, e.g. the
        // enemy warp-in ring) plays its EF2 once and is then reaped by tick_effects; only a
        // held effect (b != 0 — the wards, the statue aura) loops. Previously every effect
        // used `t*60 % life`, so one-shots re-played forever and rings never disappeared.
        const int life = std::max(1, art.life);
        const int age = (e.b != 0.0f) ? (e.age % life) : std::min(e.age, life - 1);
        for (const BulletArt::Layer& L : art.layers) {
            if (L.frames.empty()) continue;
            const int fi = std::min(int(L.frames.size()) - 1,
                                    int(std::size_t(age) * L.frames.size() /
                                        std::size_t(std::max(1, art.life))));
            const Ef2Frame& fr = L.frames[std::size_t(fi)];
            // Each emitter sits at its own height above the effect's placed origin
            // (B1+0x48[2], scaled by the locate op's effect scale). This is what stacks
            // the ward's bands (cylinder 0.50, rings 0.65 and 0.40) and what lifts the
            // statue's wing glow off its floor pool.
            const float ey = e.y + L.hoff * scale;
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
                    verts.push_back({e.x + (s - c) * Rtop * radXZ, ey + hh * radY,
                                     e.z + (s + c) * Rtop * radXZ, 0, 1, 0, u, fr.v0, 255, 255, 255, 255});
                    verts.push_back({e.x + (s - c) * Rbot * radXZ, ey - hh * radY,
                                     e.z + (s + c) * Rbot * radXZ, 0, 1, 0, u, fr.v1, 255, 255, 255, 255});
                }
                idx.reserve(std::size_t(N) * 6);
                for (int i = 0; i < N; ++i) {
                    const std::uint32_t t0 = std::uint32_t(2 * i), b0 = t0 + 1, t1 = t0 + 2, b1 = t0 + 3;
                    idx.insert(idx.end(), {t0, b0, t1, t1, b0, b1});
                }
            } else if (L.flat) {
                // WORLD-ORIENTED quad (B2+0x44 == 1), drawn FLAT in the world XZ plane.
                // This is the ward's magenta ring: MAP_KEKKAI emitters 7 and 8 are
                // BARIARING01.DDS, a texture that is literally a purple annulus, and they
                // carry orient 1 exactly as its cylinder ring (em6) does, while every one
                // of its upright glows carries 0 -- as does every glow of the goddess
                // statue's aura. Drawn camera-facing these stood on edge; laid flat they
                // reproduce the ring around the pedestal in
                // reference/realgame/yt/hugo_p2_ward_barrier.jpg.
                const float hw = L.w * scale * 0.5f, hd = L.h * scale * 0.5f;
                const Vec3 c = {e.x, ey, e.z};
                auto V = [&](float rx, float rz, float u, float v) -> RoomVertex {
                    return {c.x + rx * hw, c.y, c.z + rz * hd, 0, 1, 0, u, v, 255, 255, 255, 255};
                };
                verts = {V(-1, -1, fr.u0, fr.v0), V(1, -1, fr.u1, fr.v0),
                         V(1, 1, fr.u1, fr.v1), V(-1, 1, fr.u0, fr.v1)};
                idx = {0, 1, 2, 0, 2, 3};
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
                const Vec3 c = {e.x, ey, e.z};
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


// The EF2 particle simulation's particles (src/cleria/ef2_sim.hpp; build/ai_spec/EF2SIM.md §4).
// B2[0] & 0x100: a camera-facing billboard, half-size scale.x * mult.x * fx scale * 0.5 (x only),
// rolled by rot.z through the cosine table; otherwise the unit quad (+/-0.5) transformed by the
// particle matrix (B1 pitch pi/2 lies it flat). Colour = the track diffuse ARGB: additive emitters
// (B2+0x08 & 0x80) add rgb * alpha, the others blend alpha-over. EFTMESH (& 0x200) emitters:
// draw_eftmesh_particles. Ring (& 8) emitters: the band FUN_0044c250 builds (EF2_RENDER.md §A.2a
// [C-DIS 0x44c3a8..0x44c4e3]): N = B2+0x8f8 segments, step i at a = 2 pi i / N, the top edge
// ((s - c) R_top, +h/2, (s + c) R_top) and the bottom ((s - c) R_bot, -h/2, (s + c) R_bot) with
// R_bot / R_top / h = B2+0x8fc / +0x900 / +0x904, one strip in the particle's frame (Velagunder's
// laser beam streaks). [H] u runs across the frame's rect around the ring, v across the band.
// [not modelled] the specular add, the UV scroll (the ring's +0x130 phase), render interpolation.
//
// EFTMESH emitters — FUN_0044f2e0's `B2 & 0x200` branch [EXPORT 0x44f2e0, EF2SIM.md §4]: the
// YMO `DAT_0074e274[B2+0x18]` (EFTMESH.DAT: u32 0, u32 count, then {u32 id, char name[0xC0]}
// records stored BY id — FUN_00450d50) drawn through the ordinary model path FUN_005fafc0(0, -1)
// with the particle's world matrix. Before the draw, every material of the mesh gets:
//   diffuse rgb = the track colour / 255; model alpha (+0x29c+0xc) = track alpha / 255, snapped
//   to 1 above 0.985 (0x6bd418) and to 0 below 0.015 (0x6bd0c0);
//   UV scroll on (flag 4) with phase = the particle's uv + the frame's (u0, v0) (B2+0x8c/+0x90 +
//   16 * frame), or off when both are 0;
//   when the emitter has frames (B2+0x48), the frame's EFTTEX texture replaces the mesh's (the
//   first 8 materials; [approx] counted here per batch, one batch per material in practice).
// Each material then blends by its own d3 with that alpha (material_blend_mode), and colour =
// texture x vertex colour x c16, c16 = material diffuse (x the scene colour, [not modelled] as
// on the room). Vertices are transformed on the CPU (the room uniform has no per-draw matrix).
// [not modelled] the mesh's animation time (B2+0x1c..0x24, FUN_005f5e10): the rest pose is drawn.
// CLERIA_FX_ONLY="id[:em],..." (audit aid): draw only those effects' particles (all emitters, or
// only emitter `em`), to look at one emitter of a busy effect in a drive `shot`.
static bool fx_debug_skip(int id, int em) {
    static const std::string only = [] { const char* e = std::getenv("CLERIA_FX_ONLY"); return std::string(e ? e : ""); }();
    if (only.empty()) return false;
    std::size_t p = 0;
    while (p < only.size()) {
        std::size_t q = only.find(',', p);
        if (q == std::string::npos) q = only.size();
        const std::string t = only.substr(p, q - p);
        const std::size_t c = t.find(':');
        if (std::atoi(t.c_str()) == id && (c == std::string::npos || std::atoi(t.c_str() + c + 1) == em)) return false;
        p = q + 1;
    }
    return true;
}

static void draw_eftmesh_particles(Gpu& g, const ef2sim::System& sim, const fs::path& asset_root,
                                   const std::function<const SpriteTex*(std::uint32_t)>& tex_of,
                                   WGPUTextureView color, WGPUTextureView depth, bool early) {
    static std::unordered_map<int, fs::path> names;           // EFTMESH.DAT id -> YMO
    static bool names_read = false;
    if (!names_read) {
        names_read = true;
        const std::vector<std::uint8_t> b = read_bytes(asset_root / "EFFECT" / "EFT" / "EFTMESH.DAT");
        auto u32 = [&](std::size_t o) { std::uint32_t v = 0; if (o + 4 <= b.size()) std::memcpy(&v, &b[o], 4); return v; };
        const std::uint32_t n = b.size() >= 8 && u32(0) == 0 ? std::min<std::uint32_t>(u32(4), 0x100) : 0;
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::size_t o = 8 + std::size_t(i) * 0xC4;
            if (o + 0xC4 > b.size()) break;
            std::string nm(reinterpret_cast<const char*>(&b[o + 4]), 0xC0);
            nm.resize(std::strlen(nm.c_str()));
            for (char& c : nm) if (c == '\\') c = '/';
            if (!nm.empty() && u32(o) < 0x100) names[int(u32(o))] = asset_root / "EFFECT" / "EFT" / nm;
        }
    }
    static std::unordered_map<int, Room> models;              // id -> uploaded YMO (empty = failed)
    // The particle's render layer (FUN_0044ef00 [EXPORT 0044ef00.c:24-45]): 12 (10 with fx+0x1da, 13 with
    // fx+0x1dc & 0x20), but **4** for a mesh whose model header (+8) has flag 0x80 -- 46 of the 153 effect meshes
    // [DATA]: the solid ones (rocks, blades, spikes, Dalles's death shards). Layer 4 is drawn with the room's
    // opaque objects, before the actors and before the glare's source is taken; `early` selects those.
    static std::unordered_map<int, bool> layer4;
    auto model_of = [&](int id) -> const Room* {
        auto it = models.find(id);
        if (it == models.end()) {
            Room r;
            auto nt = names.find(id);
            if (nt != names.end() && fs::exists(nt->second)) {
                const std::vector<std::uint8_t> hb = read_bytes(nt->second);
                layer4[id] = hb.size() >= 12 && (hb[8] & 0x80) != 0;
            }
            const double t0 = g_prof.on ? prof_ms() : 0.0;
            if (nt != names.end() && fs::exists(nt->second)) r = load_model(g, asset_root, nt->second);
            prof_load("effect mesh", nt != names.end() ? nt->second.filename().string() : std::string("?"), t0);
            it = models.emplace(id, std::move(r)).first;
        }
        return it->second.mesh.vertices.empty() ? nullptr : &it->second;
    };
    // A particle's mesh goes out as the model's own vertices, transformed, drawn with the model's index buffer
    // (one indexed draw per particle and subset, in the old order). It used to be expanded triangle by triangle:
    // 2436 vertices for each of Dalles's death meshes, 66 of them at once.
    struct MeshDraw { WGPUBuffer ibuf; std::uint32_t first, count; std::int32_t base; };
    // `glare`: an opaque material's glare amount (int)(d13 x a) >= 1 -- the draw writes the constant
    // max(0x100 - g, 2) / 255 as the glare mask instead of its texture's alpha, as draw() does for the room
    // [EXPORT 005f92b0.c:571-585]. Dalles's death shards (EFTMESH 26, d13 = 300) are such a material: without it
    // they drew as flat grey wedges with no glow (issue #84).
    struct Bucket { std::vector<RoomVertex> v; WGPURenderPipeline pipe{}; WGPUBindGroup bind{}; std::vector<MeshDraw> draws{}; int glare = 0; };
    std::vector<Bucket> opaque, blended;
    // Each subset's vertex span [lo, hi) and its usable index count (whole triangles before a bad index), per model.
    struct Span { std::uint32_t lo = 0, hi = 0, count = 0; };
    static std::unordered_map<const Room*, std::vector<Span>> spans;
    auto spans_of = [&](const Room* m) -> const std::vector<Span>& {
        auto it = spans.find(m);
        if (it != spans.end()) return it->second;
        std::vector<Span> out;
        for (const DrawBatch& bt : m->mesh.batches) {
            Span sp;
            sp.lo = 0xffffffffu;
            for (std::uint32_t k = 0; k < bt.index_count; ++k) {
                const std::size_t ii = std::size_t(bt.first_index) + k;
                if (ii >= m->mesh.indices.size() || m->mesh.indices[ii] >= m->mesh.vertices.size()) break;
                sp.lo = std::min(sp.lo, m->mesh.indices[ii]); sp.hi = std::max(sp.hi, m->mesh.indices[ii] + 1);
                ++sp.count;
            }
            sp.count -= sp.count % 3;
            out.push_back(sp);
        }
        return spans.emplace(m, std::move(out)).first->second;
    };
    for (const auto& pp : sim.particles()) {
        const ef2sim::Particle& p = *pp;
        if (p.dead || p.delay != 0 || p.remaining == 0 || !p.drawn) continue;
        const Ef2Emitter& E = p.fx->ef->emitters()[std::size_t(p.em)];
        std::uint32_t flags = 0;
        std::memcpy(&flags, E.b2.data(), 4);
        if (!(flags & 0x200) || (flags & 0x400)) continue;
        if (fx_debug_skip(p.fx->id, p.em)) continue;
        const Room* m = model_of(E.b2[0x18]);
        if (!m || layer4[E.b2[0x18]] != early) continue;
        float a = float(p.diffuse >> 24) / 255.0f;
        a = a > 0.985f ? 1.0f : (a < 0.015f ? 0.0f : a);
        if (a <= 0.0f) continue;                                  // FUN_005faca0: nothing above 0
        const float tr = float((p.diffuse >> 16) & 0xff) / 255.0f, tg = float((p.diffuse >> 8) & 0xff) / 255.0f,
                    tb = float(p.diffuse & 0xff) / 255.0f;
        const std::size_t fo = 0x8c + std::size_t(p.frame) * 16;
        float du = p.uv_u, dv = p.uv_v, fu = 0, fv = 0;
        if (fo + 8 <= E.b2.size()) { std::memcpy(&fu, &E.b2[fo], 4); std::memcpy(&fv, &E.b2[fo + 4], 4); }
        du += fu; dv += fv;
        const SpriteTex* over = nullptr;
        if (!E.frames.empty())
            over = tex_of(E.frames[std::min<std::size_t>(p.frame, E.frames.size() - 1)].tex_id);
        int mat_i = 0;
        const std::vector<Span>& msp = spans_of(m);
        for (const DrawBatch& bt : m->mesh.batches) {
            if (bt.index_count == 0) continue;
            const int mode = material_blend_mode(bt, a);
            WGPUBindGroup bind = (bt.texture >= 0 && bt.texture < int(m->binds.size())) ? m->binds[bt.texture] : g.black_bind;
            if (over && mat_i < 8) bind = over->bind;
            ++mat_i;
            const bool cull = material_cull(bt);
            const int glare = mode == 0 && g.glare_pipe ? int(float(bt.alpha_ref) * a) : 0;
            const WGPURenderPipeline pipe = glare >= 1 ? (cull ? g.cull_glare_pipe : g.glare_pipe)
                                            : mode == 0 ? (cull ? g.cull_pipeline : g.pipeline)
                                                        : (cull ? g.cull_mat_pipe : g.mat_pipe)[mode][material_zwrite(bt) ? 1 : 0];
            std::vector<Bucket>& list = mode == 0 ? opaque : blended;
            if (list.empty() || list.back().pipe != pipe || list.back().bind != bind || list.back().glare != glare) {
                list.push_back({{}, pipe, bind});
                list.back().glare = glare;
            }
            const Span& sp = msp[std::size_t(&bt - m->mesh.batches.data())];
            if (sp.count == 0) continue;
            std::vector<RoomVertex>& out = list.back().v;
            list.back().draws.push_back({m->ibuf, bt.first_index, sp.count, std::int32_t(out.size()) - std::int32_t(sp.lo)});
            for (std::uint32_t k = sp.lo; k < sp.hi; ++k) {
                RoomVertex v = m->mesh.vertices[k];
                const Vec3 w = ef2sim::transform_coord({v.px, v.py, v.pz}, p.world);
                v.px = w.x; v.py = w.y; v.pz = w.z;
                v.u += du; v.v += dv;
                v.r = std::uint8_t(float(v.r) * tr + 0.5f);
                v.g = std::uint8_t(float(v.g) * tg + 0.5f);
                v.b = std::uint8_t(float(v.b) * tb + 0.5f);
                v.a = std::uint8_t(float(v.a) * a + 0.5f);
                out.push_back(v);
            }
        }
    }
    // One vertex buffer kept between frames, each bucket written at its own offset (the writes land before the
    // bucket's submit). A buffer made per bucket per frame cost ~31 ms once it passed 5 MB (measured: those 66
    // meshes as 160776 expanded vertices), every frame the burst lasted.
    static WGPUBuffer kept = nullptr;
    static std::uint64_t kept_size = 0;
    std::uint64_t need = 0, at = 0;
    for (std::vector<Bucket>* list : {&opaque, &blended})
        for (const Bucket& bk : *list) need += bk.v.size() * sizeof(RoomVertex);
    if (need > kept_size) {
        if (kept) wgpuBufferRelease(kept);
        kept_size = need + need / 2;
        WGPUBufferDescriptor bd = {};
        bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst; bd.size = kept_size;
        kept = wgpuDeviceCreateBuffer(g.device, &bd);
    }
    for (std::vector<Bucket>* list : {&opaque, &blended})
        for (const Bucket& bk : *list) {
            if (bk.v.empty()) continue;
            const std::uint64_t vsize = bk.v.size() * sizeof(RoomVertex), voff = at;
            wgpuQueueWriteBuffer(g.queue, kept, voff, bk.v.data(), vsize);
            at += vsize;
            WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
            WGPURenderPassColorAttachment ca = {};
            ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDepthStencilAttachment da = {};
            da.view = depth; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor rp = {};
            rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
            wgpuRenderPassEncoderSetPipeline(pass, bk.pipe);
            if (bk.glare >= 1) {
                const WGPUColor k = {0, 0, 0, double(std::max(0x100 - bk.glare, 2)) / 255.0};
                wgpuRenderPassEncoderSetBlendConstant(pass, &k);
            }
            wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, bk.bind, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, kept, voff, vsize);
            WGPUBuffer ib = nullptr;
            for (const MeshDraw& d : bk.draws) {
                if (d.ibuf != ib) wgpuRenderPassEncoderSetIndexBuffer(pass, ib = d.ibuf, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderDrawIndexed(pass, d.count, 1, d.first, d.base, 0);
            }
            wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
            wgpuQueueSubmit(g.queue, 1, &cmd);
            wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
        }
}

// FUN_0044f2e0's three quad kinds:
//  - `& 0x100` billboards: the world matrix is 0x7961b0 — the camera's world rotation (FUN_005eca30
//    copies the inverse view to camera+0x340 and zeroes its translation; 0x795e70 is the render
//    camera block, so 0x7961b0 = its +0x340) — so the quad lies in the camera plane, tilted with the
//    pitch, not stood upright. Translation p+0x150 - fx+0x1f4 (the depth bias) [0x44fb95..0x44fbe4].
//  - world quads: p+0x120's rows with the translation p+0x150 - fx+0x1f4 [EXPORT 0044f2e0.c:204-219].
//  - rings: p+0x120 as is, no bias [0x44f931]. Their mesh (FUN_0044c250 [BYTES 0x44c348..0x44c4e3]):
//    x = (cos a - sin a) r, z = (sin a + cos a) r (FUN_0041c530 = cos: 0x66fd70 falls back to fcos);
//    the top edge takes the frame's v0, the bottom v1, and u ALTERNATES u0, u1, u0, ... per step, so
//    every segment shows the whole frame (it used to spread one frame once around the ring).
static void draw_particles(Gpu& g, const ef2sim::System& sim, const fs::path& asset_root,
                           const EftTex& eft, WGPUTextureView color, WGPUTextureView depth,
                           const Vec3& camFwd, const Vec3& camPos, bool layer4_only = false) {
    if (sim.particles().empty()) return;
    static std::unordered_map<std::uint32_t, SpriteTex> texs;   // tex_id -> texture (empty = failed)
    auto tex_of = [&](std::uint32_t id) -> const SpriteTex* {
        auto it = texs.find(id);
        if (it == texs.end()) {
            SpriteTex t{};
            const fs::path p = eft.path(asset_root, int(id));
            const double t0 = g_prof.on ? prof_ms() : 0.0;
            if (!p.empty()) {
                Dds d = load_dds(p);
                if (d.ok()) t = upload_rgba(g, d.width, d.height, d.data.data());
            }
            prof_load("effect texture", p.filename().string(), t0);
            it = texs.emplace(id, t).first;
        }
        return it->second.bind ? &it->second : nullptr;
    };
    draw_eftmesh_particles(g, sim, asset_root, tex_of, color, depth, layer4_only);
    if (layer4_only) return;
    const Vec3 up = {0, 1, 0};
    const Vec3 right = normalize(cross(up, camFwd));
    const Vec3 cam_up = cross(camFwd, right);                // the camera's up (0x7961b0 row 1)
    // mode: the emitter's blend (Ef2Emitter): 0 alpha, 1 additive, 2 subtractive, 3 premultiplied.
    struct Batch { std::vector<RoomVertex> v; std::vector<std::uint32_t> i; WGPUBindGroup bind{}; int mode = 1; };
    std::map<std::pair<std::uint32_t, int>, Batch> batches;
    for (const auto& pp : sim.particles()) {
        const ef2sim::Particle& p = *pp;
        // Not stepped yet (a child fired inside this step's particle loop): the engine has not enqueued it.
        // It drew here once with its spawn state, a unit quad of the B1 colour at the effect origin: the
        // white disc flashing at a goddess statue's base, SAVE_MEGAMIZOU01's em 4 every 15 ticks (issue #69).
        if (p.dead || p.delay != 0 || p.remaining == 0 || !p.drawn) continue;
        const Ef2Emitter& E = p.fx->ef->emitters()[std::size_t(p.em)];
        std::uint16_t flags = 0;
        std::memcpy(&flags, E.b2.data(), 2);
        if ((flags & 0x400) || !(flags & 7) || (flags & 0x200)) continue;
        if (E.frames.empty()) continue;
        if (fx_debug_skip(p.fx->id, p.em)) continue;
        const Ef2Frame& fr = E.frames[std::min<std::size_t>(p.frame, E.frames.size() - 1)];
        const SpriteTex* t = tex_of(fr.tex_id);
        if (!t) continue;
        const int mode = E.additive ? 1 : E.subtractive ? 2 : E.premult ? 3 : 0;
        Batch& b = batches[{fr.tex_id, mode}];
        b.bind = t->bind; b.mode = mode;
        const float a = float(p.diffuse >> 24) / 255.0f;
        float r = float((p.diffuse >> 16) & 0xff), gc = float((p.diffuse >> 8) & 0xff), bc = float(p.diffuse & 0xff);
        if (mode == 1 || mode == 2) { r *= a; gc *= a; bc *= a; }   // SRCALPHA into a ONE destination
        const std::uint8_t R = std::uint8_t(r), G = std::uint8_t(gc), B = std::uint8_t(bc);
        // fs_char: vertex alpha is the hit-flash (0), opacity = 1 - nrm.x. fs_pm: vertex alpha = the diffuse alpha.
        const float nx = mode == 0 ? 1.0f - a : 0.0f;
        const std::uint8_t VA = mode == 0 ? 0 : mode == 3 ? std::uint8_t(p.diffuse >> 24) : 255;
        const std::uint32_t base = std::uint32_t(b.v.size());
        if (flags & 8) {                                     // the ring band
            if (E.b2.size() < 0x908) continue;
            std::uint32_t N = 0;
            float rb = 0, rt = 0, ht = 0;
            std::memcpy(&N, E.b2.data() + 0x8f8, 4);
            std::memcpy(&rb, E.b2.data() + 0x8fc, 4);
            std::memcpy(&rt, E.b2.data() + 0x900, 4);
            std::memcpy(&ht, E.b2.data() + 0x904, 4);
            if (N == 0 || N > 256) continue;
            const float hh = ht * 0.5f;                      // 0x6bd304
            for (std::uint32_t i = 0; i <= N; ++i) {
                const float an = float(i) * (6.2831855f / float(N));   // 0x6bd898
                const float s = std::sin(an), cs = std::cos(an);
                const Vec3 A = ef2sim::transform_coord({(cs - s) * rt, hh, (s + cs) * rt}, p.world);
                const Vec3 Bv = ef2sim::transform_coord({(cs - s) * rb, -hh, (s + cs) * rb}, p.world);
                const float u = (i & 1) ? fr.u1 : fr.u0;       // 0x44c4af: u0, u1, u0, ...
                b.v.push_back({A.x, A.y, A.z, nx, 1, 0, u, fr.v0, R, G, B, VA});
                b.v.push_back({Bv.x, Bv.y, Bv.z, nx, 1, 0, u, fr.v1, R, G, B, VA});
            }
            for (std::uint32_t i = 0; i < N; ++i) {
                const std::uint32_t k = base + 2 * i;
                b.i.insert(b.i.end(), {k, k + 1, k + 2, k + 2, k + 1, k + 3});
            }
            continue;
        }
        Vec3 c[4];
        const float uv[4][2] = {{fr.u0, fr.v0}, {fr.u0, fr.v1}, {fr.u1, fr.v0}, {fr.u1, fr.v1}};
        // fx+0x1f4 (ef2_sim.hpp depth_bias): the quad is drawn that far toward the eye.
        const Vec3 fxo{p.fx->M.m[3][0], p.fx->M.m[3][1], p.fx->M.m[3][2]};
        const Vec3 bias = ef2sim::depth_bias_offset(fxo, camPos, p.fx->depth_bias + p.fx->bias_extra);   // + fx+0x204
        if (flags & 0x100) {
            const Vec3 wp = p.world_pos();
            const Vec3 o{wp.x - bias.x, wp.y - bias.y, wp.z - bias.z};
            const float h = p.scale.x * p.mult.x * p.fx->scale_x * 0.5f;            const int ti = int(p.rot.z * 40.743664f) & 0xff;
            const float cc = cos_table(ti) * h, ss = cos_table((ti - 64) & 0xff) * h;
            const float xy[4][2] = {{ss - cc, ss + cc}, {-ss - cc, ss - cc}, {ss + cc, cc - ss}, {cc - ss, -ss - cc}};
            for (int k = 0; k < 4; ++k)
                c[k] = {o.x + right.x * xy[k][0] + cam_up.x * xy[k][1], o.y + right.y * xy[k][0] + cam_up.y * xy[k][1],
                        o.z + right.z * xy[k][0] + cam_up.z * xy[k][1]};
        } else {
            const float q[4][2] = {{-0.5f, 0.5f}, {-0.5f, -0.5f}, {0.5f, 0.5f}, {0.5f, -0.5f}};
            for (int k = 0; k < 4; ++k) {
                c[k] = ef2sim::transform_coord({q[k][0], q[k][1], 0.0f}, p.world);
                c[k] = {c[k].x - bias.x, c[k].y - bias.y, c[k].z - bias.z};
            }
        }
        for (int k = 0; k < 4; ++k)
            b.v.push_back({c[k].x, c[k].y, c[k].z, nx, 1, 0, uv[k][0], uv[k][1], R, G, B, VA});
        b.i.insert(b.i.end(), {base, base + 1, base + 2, base + 2, base + 1, base + 3});
    }
    for (auto& [key, b] : batches) {
        if (b.i.empty()) continue;
        WGPUBuffer vb = make_buffer(g.device, g.queue, b.v.data(), b.v.size() * sizeof(RoomVertex), WGPUBufferUsage_Vertex);
        WGPUBuffer ib = make_buffer(g.device, g.queue, b.i.data(), b.i.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPURenderPassColorAttachment ca = {};
        ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDepthStencilAttachment da = {};
        da.view = depth; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor rp = {};
        rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderSetPipeline(pass, b.mode == 1 ? g.fx_pipeline_sa : b.mode == 2 ? g.fx_pipeline_sub
                                               : b.mode == 3 ? g.fx_pipeline_pm : g.char_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, b.bind, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, std::uint32_t(b.i.size()), 1, 0, 0, 0);
        wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(g.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
        wgpuBufferRelease(vb); wgpuBufferRelease(ib);
    }
}
