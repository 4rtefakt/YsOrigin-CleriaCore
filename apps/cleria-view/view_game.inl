// cleria-view — render_window — the live game loop
// Textual part of main.cpp (split 2026-07-23, PLAN.md Phase 0): #included by
// main.cpp in a fixed order; ONE translation unit, not standalone. Keep it that way
// until a real TU split is warranted.

// ---- interactive walkthrough ------------------------------------------------
static int render_window(Gpu& g, const fs::path& asset_root, const fs::path& first_sob,
                         bool show_title = false) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("CleriaCore - Ys Origin", 1280, 800, SDL_WINDOW_RESIZABLE);
    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    WGPUSurfaceSourceWindowsHWND srcw = {};
    srcw.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    srcw.hinstance = GetModuleHandle(nullptr); srcw.hwnd = hwnd;
    WGPUSurfaceDescriptor sd = {}; sd.nextInChain = &srcw.chain;
    WGPUSurface surface = wgpuInstanceCreateSurface(g.instance, &sd);
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface, g.adapter, &caps);
    // Prefer the LINEAR (non-sRGB) surface format. Our textures are already sRGB and the shaders
    // pass them straight through; if the surface is Rgba8UnormSrgb, presenting sRGB-encodes a
    // SECOND time and the window washes out. The offscreen renders use linear RGBA8Unorm and look
    // correct -- match that here so all pipelines + the surface stay consistent.
    WGPUTextureFormat fmt = caps.formats[0];
    for (std::size_t i = 0; i < caps.formatCount; ++i) {
        const WGPUTextureFormat f = caps.formats[i];
        if (f == WGPUTextureFormat_RGBA8Unorm || f == WGPUTextureFormat_BGRA8Unorm) { fmt = f; break; }
    }
    std::uint32_t cw = 1280, ch = 800;
    auto configure = [&] {
        WGPUSurfaceConfiguration sc = {};
        sc.device = g.device; sc.format = fmt; sc.usage = WGPUTextureUsage_RenderAttachment;
        sc.width = cw; sc.height = ch; sc.alphaMode = WGPUCompositeAlphaMode_Auto;
        sc.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(surface, &sc);
    };
    configure();
    build_pipeline(g, fmt);
    Hud hud; hud.init(g, res_file("hud_font.bin"), fmt);
    hud.init_atlas(g, asset_root / "IMAGE" / "STATUS_512.DDS");
    hud.init_mini(g, asset_root / "IMAGE" / "F_MINI.DDS");
    FlameFx flames;                      // torch flames (SEF placements + EF2 flipbook)
    if (!flames.init(g, asset_root))
        std::fprintf(stderr, "flames: NAGA01.DDS failed to load\n");
    ShadowFx shadows;                    // floor shadow decals under characters
    if (!shadows.init(g, asset_root))
        std::fprintf(stderr, "shadows: SHADOW0.DDS failed to load\n");

    // The starting character (g_flags[150]: 1 Yunica / 2 Hugo / 3 Toal). CLERIA_CHAR=1|2|3
    // forces it (Tab still cycles in-game); on a normal launch the title's New Game -> the
    // character-select screen sets it. Declared here, before the title phase, so that screen
    // can write it. Toal (3, C_104) is the only shipping player sprite with AIA second layers,
    // so he is the one that exercises the composite path.
    int character = 1;
    if (const char* cs = std::getenv("CLERIA_CHAR")) {
        const int c = std::atoi(cs);
        if (c >= 1 && c <= 3) character = c;
    }
    // The rest of the New-Game choices (set by the title's popups; see the flow below):
    int skin = 0;          // 0 Normal, 1 Extra (alternate appearance -- swap pending skin RE)
    int difficulty = 2;    // 0 VeryEasy,1 Easy,2 Normal,3 Hard,4 Nightmare (persisted TitleGameDifficulty)
    bool sp_bonus = false; // "SP Bonus Acquisition": boosts this save's SP gains
    // Option settings (persisted in yso_win.ini). Function-scope so the title menu edits them and
    // the game loop (audio) can read them; loaded once up front.
    int oslider[3] = {512, 512, 512};    // BGM / SFX / Brightness (0..1024)
    bool ocheck[4] = {false, true, false, false};   // Analog / Dash / HoldAtk / AlwaysDash (rows 3..6)
    options_load(oslider, ocheck);
    (void)skin; (void)difficulty; (void)sp_bonus;   // stored now; applied to combat as RE lands

    // Audio comes up before the title screen: the title is a scene in its own
    // right (SCENELIST record "title\", BGM id 1), and `transition` far below
    // captures this to switch tracks on room entry.
    Audio audio;
    if (!std::getenv("CLERIA_MUTE"))   // CLERIA_MUTE=1: no audio device at all (quiet test runs)
        audio.init(asset_root);  // silent (and harmless) if the device or EFFECTDEF is missing
    audio.set_master_volume(oslider[1] / 1024.0f);  // Option "SFX Volume"
    audio.set_bgm_volume(oslider[0] / 1024.0f);     // Option "BGM Volume"

    // Per-room music: SCENELIST record byte +3 is a bgmdef id. The exe (re)starts
    // the track only when that id CHANGES, so walking between rooms that share one
    // plays straight through the doorway; id 0 means STOP, not "inherit".
    // See build/audio_re/BGM.md §2-3.
    SceneList scenes;
    BgmDefs bgmdefs;
    if (!scenes.load(asset_root))
        std::printf("audio: no MAP/SCENELIST.SL — no per-room BGM\n");
    else if (!bgmdefs.load(asset_root))
        std::printf("audio: no music folder found — set CLERIA_MUSIC_DIR to the game's "
                    "release\\music to hear BGM\n");
    else
        std::printf("audio: %zu scenes, music from %s\n", scenes.size(),
                    bgmdefs.dir().string().c_str());
    int cur_bgm = -1;
    auto apply_room_bgm = [&](const std::string& room_name) {
        const SceneRec* s = scenes.find_room(room_name);
        const int id = s ? int(s->bgm) : 0;
        if (id == cur_bgm) return;
        cur_bgm = id;
        if (id == 0)
            audio.stop_bgm();
        else
            audio.play_bgm(bgmdefs.track(id));
    };

    // Save slots (PLAN.md Phase 2; src/cleria/save.hpp codec). One yso_NN.bin per slot
    // under CLERIA_SAVE_DIR (default ./saves), mirroring the game's own file naming.
    auto save_dir = [] {
        const char* e = std::getenv("CLERIA_SAVE_DIR");
        return fs::path(e ? e : "saves");
    };
    auto slot_path = [&](int i) {
        char n[16]; std::snprintf(n, sizeof n, "yso_%02d.bin", i);
        return save_dir() / n;
    };
    auto slot_label = [&](int i) -> std::string {   // "" = empty slot
        std::error_code ec;
        if (!fs::exists(slot_path(i), ec)) return {};
        std::ifstream f(slot_path(i), std::ios::binary);
        std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(f), {}};
        auto r = SaveRecord::decode(raw);
        if (!r) return {};
        const SaveMeta m = meta_of(*r);
        return std::string(kCharNames[m.character & 3]) + "  " + m.area + "  " + m.date;
    };
    // A save picked on the title's Load Game screen; applied to the game state below
    // (flags/character before the first room load, stats/position after the world exists).
    std::optional<SaveState> pending_load;

    // Title screen: shown on a bare launch, before the game. New Game opens character-select
    // (pick Yunica/Hugo/Toal, then fall through to the game loop below); Esc/close quits.
    // Assets + flow from build/menu_re/MENUS.md.
    if (show_title) {
        apply_room_bgm("title");   // "title\" is a scene record like any room (BGM id 1)
        TitleFx title; title.init(g, asset_root);
        std::array<std::string, 8> tslots;
        int lsel = 0, ltab = 0;
        auto scan_tab = [&] { for (int i = 0; i < 8; ++i) tslots[std::size_t(i)] = slot_label(ltab * 8 + i); };
        MenuFont mf;   // Felghana atlas rasterised at the 960-internal font size (see render_title),
        // bilinear-upscaled to the window. Format must match the window surface (fmt), not a
        // hardcoded RGBA8Unorm, or the pipeline is incompatible with an sRGB surface pass.
        mf.init(g, fmt, find_felghana(asset_root), int(std::lround(32.0 * 960.0 / 768.0)));
        WGPUTextureView tdepth = make_depth(g, cw, ch);
        g_fog.on = false; write_mvp(g, Mat4::identity());   // screen-space draw
        // The title is a state machine mirroring the engine (MENUS.md §1.4): the MAIN list, the
        // BONUS sub-list (DAT_0074e259), and the New-Game chain CHARSELECT -> TYPESELECT (Normal/
        // Extra skin) -> DIFFICULTY (Easy..Nightmare) -> SPBONUS (SP-gain boost) -> start.
        enum TState { MAIN, BONUS, CHARSELECT, TYPESELECT, DIFFICULTY, SPBONUS, OPTIONS, LOADGAME };
        static const char* kTypeItems[3] = {"Normal", "Extra", "Cancel"};
        static const char* kDiffItems[6] = {"Very Easy", "Easy", "Normal", "Hard", "Nightmare", "Cancel"};
        static const char* kSpItems[2]  = {"[Enable]", "[Disable]"};
        TState ts = MAIN;
        int sel = 0;      // main menu: 0 New Game, 1 Load, 2 Bonus Modes, 3 Options, 4 Exit
        int bsel = 0;     // bonus sub-list: 0 Time Attack, 1 Arena, 2 Bonus Shop, 3 Back
        int csel = 0;     // character-select: 0 Yunica, 1 Hugo, 2 Toal
        int tsel = 0, dsel = 2, spsel = 0;   // popup cursors (difficulty defaults Normal, SP-bonus Enable)
        int osel = 0;                        // Option screen: focused row 0..9 (oslider/ocheck are
                                             // in render_window scope, above -- persisted to yso_win.ini)
        auto teardown = [&] { wgpuTextureViewRelease(tdepth); SDL_DestroyWindow(win); SDL_Quit(); };
        for (bool started = false; !started;) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) { teardown(); return 0; }
                else if (e.type == SDL_EVENT_KEY_DOWN) {
                    const SDL_Keycode k = e.key.key;
                    const bool up = (k == SDLK_UP || k == SDLK_W);
                    const bool down = (k == SDLK_DOWN || k == SDLK_S);
                    const bool ok = (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_E);
                    const bool esc = (k == SDLK_ESCAPE);
                    const bool left = (k == SDLK_LEFT || k == SDLK_A);
                    const bool right = (k == SDLK_RIGHT || k == SDLK_D);
                    auto wrap = [](int v, int n, bool u) { return (v + (u ? n - 1 : 1)) % n; };
                    // Menu SFX, hooked off the key classification rather than at each of the
                    // ten state branches below. up/down always moves a cursor; left/right only
                    // does anything on the Option page (sliders/checkboxes), so it only clicks
                    // there. The exe's exact call sites are being RE'd separately.
                    if (up || down || ((left || right) && ts == OPTIONS))
                        audio.play(kSfxCursor, kSfxMenuVol);
                    else if (ok)
                        audio.play(kSfxConfirm, kSfxMenuVol);
                    else if (esc)
                        audio.play(kSfxCancel, kSfxMenuVol);
                    if (ts == CHARSELECT) {
                        if (esc) ts = MAIN;                              // back to main menu
                        else if (up || down) csel = wrap(csel, TitleFx::kCharCount, up);
                        else if (ok) { character = csel + 1; ts = TYPESELECT; tsel = 0; }
                    } else if (ts == TYPESELECT) {
                        if (esc) ts = CHARSELECT;
                        else if (up || down) tsel = wrap(tsel, 3, up);
                        else if (ok) {
                            if (tsel == 2) ts = CHARSELECT;              // Cancel
                            else { skin = tsel; ts = DIFFICULTY; dsel = 2; }  // Normal/Extra -> Difficulty (Normal)
                        }
                    } else if (ts == DIFFICULTY) {
                        if (esc) ts = TYPESELECT;
                        else if (up || down) dsel = wrap(dsel, 6, up);
                        else if (ok) {
                            if (dsel == 5) ts = TYPESELECT;              // Cancel
                            else { difficulty = dsel; ts = SPBONUS; spsel = 0; }  // 0 VeryEasy..4 Nightmare
                        }
                    } else if (ts == SPBONUS) {
                        if (esc) ts = DIFFICULTY;
                        else if (up || down) spsel = wrap(spsel, 2, up);
                        else if (ok) { sp_bonus = (spsel == 0); started = true; }  // -> game
                    } else if (ts == BONUS) {
                        if (esc) ts = MAIN;
                        else if (up || down) bsel = wrap(bsel, TitleFx::kBonusCount, up);
                        else if (ok) {
                            if (bsel == 3) ts = MAIN;   // Back
                            // Time Attack / Arena Mode / Bonus Shop: not yet implemented.
                        }
                    } else if (ts == LOADGAME) {
                        if (esc) ts = MAIN;
                        else if (up || down) lsel = wrap(lsel, 8, up);
                        else if (left)  { ltab = (ltab + 7) % 8; scan_tab(); }
                        else if (right) { ltab = (ltab + 1) % 8; scan_tab(); }
                        else if (ok && !tslots[std::size_t(lsel)].empty()) {
                            std::ifstream f(slot_path(ltab * 8 + lsel), std::ios::binary);
                            std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(f), {}};
                            if (auto r = SaveRecord::decode(raw)) {
                                pending_load = from_record(*r);
                                started = true;              // -> game, state applied below
                            }
                        }
                    } else if (ts == OPTIONS) {
                        // 10 rows: 0-2 sliders, 3-6 checkboxes, 7 Default, 8 Return to Title, 9 Exit Game.
                        // Settings persist to yso_win.ini on leaving the screen (game saves config on close).
                        if (esc) { options_save(oslider, ocheck); ts = MAIN; }
                        else if (up || down) osel = wrap(osel, 10, up);
                        else if ((left || right) && osel < 3)
                            oslider[osel] = std::clamp(oslider[osel] + (right ? 8 : -8), 0, 1024);
                        else if (ok) {
                            if (osel >= 3 && osel <= 6) ocheck[osel - 3] = !ocheck[osel - 3];   // toggle
                            else if (osel == 7) {                                                // Default
                                oslider[0] = oslider[1] = oslider[2] = 512;
                                ocheck[0] = ocheck[2] = ocheck[3] = false; ocheck[1] = true;
                            } else if (osel == 8) { options_save(oslider, ocheck); ts = MAIN; }  // Return to Title
                            else if (osel == 9) { options_save(oslider, ocheck); teardown(); return 0; }  // Exit
                        }
                    } else {   // MAIN
                        if (esc) { teardown(); return 0; }
                        else if (up || down) sel = wrap(sel, TitleFx::kItemCount, up);
                        else if (ok) {
                            if (sel == 4) { teardown(); return 0; }   // Exit
                            else if (sel == 0) { ts = CHARSELECT; csel = 0; }  // New Game
                            else if (sel == 2) { ts = BONUS; bsel = 0; }       // Bonus Modes
                            else if (sel == 3) { ts = OPTIONS; osel = 0; }     // Options
                            else if (sel == 1) {                               // Load Game
                                ltab = 0; scan_tab();
                                ts = LOADGAME; lsel = 0;
                            }
                        }
                    }
                    // Drive the mixer from the sliders live, so you hear the level you are
                    // setting (and so Default resets it too). Applying these once at startup
                    // is not enough: Audio is constructed BEFORE this screen so the title has
                    // its BGM, which is earlier than any edit made here.
                    audio.set_bgm_volume(oslider[0] / 1024.0f);
                    audio.set_master_volume(oslider[1] / 1024.0f);
                } else if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                    cw = std::uint32_t(e.window.data1); ch = std::uint32_t(e.window.data2);
                    if (cw && ch) {
                        configure(); wgpuTextureViewRelease(tdepth); tdepth = make_depth(g, cw, ch);
                        mf.build_atlas(g, int(std::lround(32.0 * 960.0 / 768.0)));  // 960-internal font, upscaled
                    }
                }
            }
            WGPUSurfaceTexture st = {};
            wgpuSurfaceGetCurrentTexture(surface, &st);
            if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
                st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) { configure(); continue; }
            WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
            // Selection-bar pulse: cyan <-> near-white, ~1s period (the game animates the lit bar).
            const float pulse = 0.50f + 0.42f * std::sin(SDL_GetTicks() * 0.006f);
            if (ts == CHARSELECT) {
                title.draw_charselect(g, mf, view, tdepth, cw, ch, csel, pulse, int(SDL_GetTicks() / 110));
            } else if (ts == TYPESELECT || ts == DIFFICULTY) {
                title.draw_charselect(g, mf, view, tdepth, cw, ch, csel, pulse, int(SDL_GetTicks() / 110));   // popup backdrop
                if (ts == TYPESELECT)
                    title.draw_popup(g, mf, view, tdepth, cw, ch, "Type Select", kTypeItems, 3, tsel);
                else
                    title.draw_popup(g, mf, view, tdepth, cw, ch, "Difficulty Level", kDiffItems, 6, dsel);
            } else if (ts == SPBONUS) {
                // SP Bonus Acquisition shares the invariant popup origin over the dimmed Character
                // Select (the on-black grab was an artifact; CHARSELECT_LAYOUT.md §2.3).
                title.draw_charselect(g, mf, view, tdepth, cw, ch, csel, pulse, int(SDL_GetTicks() / 110));
                title.draw_popup(g, mf, view, tdepth, cw, ch, "SP Bonus Acquisition", kSpItems, 2, spsel);
            } else if (ts == BONUS) {
                title.draw_bonus(g, mf, view, tdepth, cw, ch, bsel);
            } else if (ts == OPTIONS) {
                title.draw_options(g, mf, view, tdepth, cw, ch, osel, oslider, ocheck);
            } else if (ts == LOADGAME) {
                title.draw_svld(g, mf, view, tdepth, cw, ch, /*save=*/false, ltab, lsel, tslots.data());
            } else {
                title.draw(g, view, tdepth, cw, ch);
                title.draw_menu(g, mf, view, tdepth, cw, ch, sel);
            }
            wgpuSurfacePresent(surface);
            wgpuTextureViewRelease(view); wgpuTextureRelease(st.texture);
        }
        wgpuTextureViewRelease(tdepth);   // the game loop rebuilds fog + its own MVP
    }
    float fx_time = 0;
    EnemyFx efx;                         // enemy sprite sets, per room
    // The game's own tables. Loaded once: the enemy chain is SCT name -> CASTINFO ->
    // ENEMYINFO -> GAMEINFO curve.
    const CastTable casts = CastTable::load(asset_root / "MISC" / "CASTINFO.DAT");
    const EnemyTable etab = EnemyTable::load(asset_root / "MISC" / "ENEMYINFO.DAT");
    const GameInfo ginfo = GameInfo::load(asset_root / "MISC" / "GAMEINFO.DAT");
    const EexTable eex = EexTable::load(asset_root / "MISC" / "EEX.DAT");  // boss stats
    // The live actors are real Actors in an ActorWorld now, not a bespoke list — one
    // spawn path, the deferred-sweep lifecycle, and the shared damage model. `render`
    // pairs each with its sprite for drawing.
    ActorWorld world({&casts, &etab, &ginfo, &eex});
    std::vector<EnemyRender> render;      // live enemies + their sprite identity
    // Our own RNG. The engine seeds its xorshift from timeGetTime(), so retail damage
    // was never replay-stable and there is no stream to match — only the algorithm's
    // shape matters, and an explicit seed buys us the determinism it lacked.
    std::mt19937 rng(20260717u);
    std::uniform_real_distribution<float> rng_pct(0.0f, 100.0f);
    // (No attack cooldown any more: the script's cancel window is the rate limit.)
    // The player's melee swing, from the ASO GenerateCollision hitbox (build/attack_re).
    // A swing arms a sphere for `atk_window` ticks; each tick it hits enemies inside it,
    // once per swing (the engine's hit-dedup). Yunica's basic swing (verified in
    // cleria-asotest): radius 0.6, 30-tick window, power 8.0, box 0.6 ahead of the actor.
    int atk_window = 0;                  // ticks left in the active hitbox
    // Recovery-hold tracking. Many attack sections end by looping on their final
    // frame with Movable pinned to 0, and several (Yunica 257/258/261, and the
    // like for Hugo/Toal) never open a cancel window at all — so `cancelable()`
    // alone would leave the player rooted there for good. The engine returns to
    // neutral via an action state machine we have not RE'd; stand in for it by
    // releasing control once the swing stops animating.
    long atk_frame_key = -1;
    int atk_stall = 0;
    // The combo stage — the engine's `actor+0x6f0`, an index into the per-character
    // attack-section table (build/combat_re/COMBO_CHAIN.md). `combo_idle` stands in for
    // the engine's `+0x6fc` post-attack counter that drops the chain back to hit #1.
    int combo_stage = 0;
    int combo_idle = 0;
    // Projectiles. The effect registries resolve a bullet's eftlist id to its real
    // EF2 and texture; without them bullets simply do not spawn (rather than draw
    // something invented).
    EftList efl;
    EftTex eft;
    if (!efl.load(asset_root) || !eft.load(asset_root))
        std::printf("effects: EFTLIST/EFTTEX missing — projectiles disabled\n");
    std::vector<Projectile> shots;
    int gauge_owner = 0;                 // the actor whose HP the boss gauge shows (DAT_006d5fd8)
    // The live hit-volume, copied from the script when GenerateCollision arms it.
    // Defaults are Yunica's combo #1 so a script that never arms behaves as before.
    AsoVm::Hitbox atk_hb;
    Vec3 atk_dir{0, 0, 1};               // the swing's facing (captured at swing start)
    Vec3 face_dir{0, 0, 1};              // world dir the player faces (from movement)
    WGPUTextureView depth = make_depth(g, cw, ch);

    // `character` was chosen above (CLERIA_CHAR override or the title's character-select).
    XsoVm game;                        // persistent game state (survives rooms)
    // The EF2 particle simulation (src/cleria/ef2_sim.hpp, build/ai_spec/EF2SIM.md): every
    // brain- and script-spawned effect except the XSO-placed ones, its particles' hits
    // included (EFFECTHIT.md). It draws from the game's one xorshift (game.rs), as every
    // effect in the engine does.
    ef2sim::Rng fx_rng;
    fx_rng.bind(game.rs);
    ef2sim::System fxsim(&fx_rng);
    std::unordered_map<int, std::unique_ptr<Ef2>> ef2_cache;   // id -> the parsed EF2 (stable)
    std::unordered_map<int, ef2sim::M4> actor_mats;           // an actor's +0x20 (its origin), for attaches
    auto ef2_of = [&](int id) -> const Ef2* {
        auto it = ef2_cache.find(id);
        if (it != ef2_cache.end()) return it->second->ok() ? it->second.get() : nullptr;
        auto ef = std::make_unique<Ef2>();
        const fs::path p = efl.path(asset_root, id);
        if (!p.empty()) *ef = Ef2::load(p);
        const Ef2* r = ef->ok() ? ef.get() : nullptr;
        ef2_cache[id] = std::move(ef);
        return r;
    };
    fxsim.set_lookup(ef2_of);                                 // impact children (B2+0x16)
    // GoXx bullets (src/cleria/bullet.hpp, BULLETS.md): each carries one effect on its matrix;
    // the effect's particles do the damage. gas_owner: the M460 whose gas floor applies
    // (fx+0x1a8; its handler pass #14).
    bullet::Bullets bullets(&fxsim, ef2_of);
    std::unordered_map<const bullet::Bullet*, int> gas_owner;
    // FUN_00450410 / the brains' inlined spawn. attach_owner != 0: FUN_00451c20 mode 2 — the
    // effect follows that actor's origin at (pos - origin); its spawn rotation / scale are
    // dropped as the attach's own (0 / 1) replace them.
    auto spawn_sim_fx = [&](int id, const Vec3& pos, const Vec3& rot, float scale,
                            ef2sim::SpawnParams sp, int attach_owner, const Vec3& origin) {
        const Ef2* ef = ef2_of(id);
        if (!ef) return;
        sp.pos = pos; sp.rot = rot; sp.scale = {scale, scale, scale};
        ef2sim::Effect* fx = fxsim.spawn(id, *ef, sp);
        if (fx && attach_owner != 0) {
            fx->local = ef2sim::translation(pos.x - origin.x, pos.y - origin.y, pos.z - origin.z);
            fx->attach_parent = &actor_mats[attach_owner];
            fx->attach_mode = 2;
        }
    };
    // The exe's own new-game init (FUN_0041fd10, NEWGAME_FLAGS.md): inventory -1 fill,
    // header set, per-character starting items. This also writes flag 149 = 1 — which is
    // what keeps Yunica's swing on her AXE (her attack ASO branches on Flag_IsEqualInt(149,1);
    // 0 would render the Crimson Lotusblade fire-skill form).
    game.new_game(character);
    Player player;                     // stat block (HP/SP/level/EXP)
    // A fresh level-1 player: HP from the GAMEINFO player curve at level 1. There is no save
    // model, so a new run always starts at level 1 with 0 EXP -- the faithful new-game state.
    // CLERIA_LEVEL forces a starting level: the RE'd damage formula is steep in the target's
    // DEF (raw = ATK^0.66 * (ATK - 0.98*DEF + 10 + power)/22.92), so a lv1 player genuinely
    // cannot dent a high-DEF enemy (Normous etc.) -- that is progression, not a bug, and this
    // lets it be seen at a realistic level.
    if (const char* e = std::getenv("CLERIA_LEVEL")) {
        int v = std::atoi(e);
        if (v >= 1 && v <= 60) player.level = v;
    }
    // The inventory / equipment state beside the flags (src/cleria/inventory.hpp,
    // build/progress_re/INVENTORY.md) and the player stat block it feeds (FUN_00420c40).
    // [not modelled] saving InvState: the save codec carries only flags / item_arr so far.
    InvState inv;
    inv_new_game(inv);
    // Saved with the game: the warp points 0x76c11c[24] (op 0xa7) and the manual pages
    // 0x76b8d8[13] (op 0x92). The Topic collection 0x74ac40 (seen byte + kill count per cast
    // id) is system data outside the save record [not modelled: persisting it].
    std::uint8_t warp_on[24] = {};
    std::uint8_t manual_on[13] = {};
    std::uint8_t topic_seen[256] = {};
    std::int32_t topic_kills[256] = {};
    const ItemTable inv_items = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
    auto player_stats = [&]() -> PlayerStats {
        if (!ginfo.ok() || inv_items.items.empty()) return PlayerStats{};
        std::copy(game.item_arr, game.item_arr + 54, inv.raval);   // 0x76a654 lives in `game`
        return compute_stats(game.g_flags, inv, ginfo, inv_items, character, player.level);
    };
    if (ginfo.ok())
        player.max_hp = player.hp = int(player_stats().max_hp);
    int player_iframes = 0;            // ticks of invulnerability after a hit (+0x734)
    Impulse player_kb{};               // knockback velocity, player +0x338/+0x340 (hit.hpp)
    Mat4 last_vp; bool have_vp = false;  // last frame's camera, for the AI's off-screen test
    const bool ai_debug = std::getenv("CLERIA_AI") != nullptr;
    std::uint64_t tick_count = 0;
    // The player's hit serial (+0x3c4): redrawn at every attack start (FUN_0046ea00); it is
    // the token each victim's re-hit bank keys on (hit_volume.hpp RehitBank).
    std::uint32_t player_hit_serial = 0;
    RehitBank player_bank;               // the player's own victim bank (+0x5e0)
    // Loaded game (title Load screen): flags + character must be right BEFORE the first
    // room load (chest gating reads them); stats/position are applied after the world
    // exists, below. This mirrors the engine's load = init-then-overwrite (SAVELOAD §4.2).
    fs::path boot_sob = first_sob;
    // A New Game from the title enters `s_00\s_0000\`, the router script that sends each
    // character into its prologue (SCENEFLOW.md §5). A room given on the command line is a
    // dev start and is kept.
    if (show_title && !pending_load) {
        auto it = g.sob_index.find("S_0000");
        if (it != g.sob_index.end()) boot_sob = it->second;
    }
    if (pending_load) {
        std::memcpy(game.g_flags, pending_load->flags, sizeof game.g_flags);
        std::memcpy(game.item_arr, pending_load->item_arr, sizeof game.item_arr);
        character = std::clamp(pending_load->character(), 1, 3);
        if (!pending_load->scene.empty()) {
            auto it = g.sob_index.find(pending_load->scene);
            if (it != g.sob_index.end()) boot_sob = it->second;
        }
    }

    // Award EXP for a kill and process level-ups (build/progress_re/LEVELING.md): the engine
    // credits the defeated enemy's EXP at the killing blow (FUN_004fbb40), then loops
    // level-up while total >= threshold(level+1) (= gameinfo exp_to_level, cumulative pair 10),
    // capped at 60. Each level-up recomputes maxHP from the curve and heals curHP by the
    // maxHP DELTA -- not a full refill. (Difficulty EXP bonus / rate mult are 1.0 here.)
    auto award_exp = [&](float enemy_exp) {
        if (!ginfo.ok()) return;
        player.exp += std::max(1.0f, std::round(enemy_exp));   // always at least 1
        while (player.level < 60 &&
               player.exp >= float(ginfo.exp_to_level(player.level + 1))) {
            ++player.level;
            player.max_hp = int(player_stats().max_hp);
            // FUN_00420c40 bit 0x01: each level-up is a full heal (FUN_004fa160(9999)) unless
            // the player is in the death state.
            if (player.hp >= 1) player.hp = player.max_hp;
            hud.message("Level up! Lv " + std::to_string(player.level));
        }
    };
    // Hand the room-load script's placed effects (op 0x117) to the world so they render.
    // S_SAVEOBJECTCHANGE both picks the statue variant AND places its idle glow; before
    // this the glow was parsed and thrown away, so a cleansed statue stood there dark.
    // Called on every path that swaps `room`, since each one re-runs that script.
    auto apply_load_effects = [&](const Room& rm) {
        for (const Room::LoadEffect& e : rm.load_effects)
            world.locate_effect(e.id, e.x, e.y, e.z, e.a, e.b, e.name);
    };
    Room room = load_room(g, asset_root, boot_sob, game.g_flags, character);
    apply_load_effects(room);
    // Particles that die on world contact trace their last step against the room (set 2).
    fxsim.set_world([&](const Vec3& f, const Vec3& d, Vec3* at) {
        Vec3 n{};
        return segment_hit(phys_mesh(room), f, {f.x + d.x, f.y + d.y, f.z + d.z}, at, &n);
    });
    Camera cam;
    bool fly = false;
    Vec3 feet{}; bool grounded = false; float speed_mult = 1.0f;
    // The engine's locomotion state (physics_player.hpp). `feet` stays the view's anchor
    // (= origin - height); the origin itself floats at floor + 1.075 (+0x324).
    VerticalState pv;            // +0x330 timer, +0x388 grounded, +0x3b0 coyote, jump flag
    PlanarVelocity pvel;         // +0x32c/+0x334
    float last_floor = 0;        // +0x318 — kept when the down-ray misses
    int cur_surface = 1;         // +0x4b9, latched surface id (surface.hpp order)
    float surface_up = 1.0f;     // the latched surface's scale on a rising dy

    // Play mode. The camera comes from the room's SCM regions (see update_camera), and
    // the player's SHAPE and VERTICAL MOTION now come from the game too — CASTINFO
    // record 13 (CAST_ADOL, the player's own cast) plus the engine's integrator. See
    // cleria/physics_player.hpp; the model is not `v += g*dt` and never was.
    CastMotion pm;
    {
        // CAST_ADOL is record 13 — the same 13 the damage path tests for. It is the
        // only record in all 186 with bespoke movement tuning.
        const CastTable ct = CastTable::load(asset_root / "MISC" / "CASTINFO.DAT");
        pm = cast_motion(ct, 13);
    }
    // ** kCharH is NOT pm.height() — and that is now understood. ** pm.height() = 1.075
    // (+0x324) is not the character's size: it is how high the ORIGIN floats above the
    // floor (audit core_gameplay.md #2; physics_player.hpp). The movement code above uses
    // it as such. kCharH remains the view's own sprite/hitbox body height until sprite
    // anchoring (sphere centre = origin + CASTINFO+0x54) and the attack-box heights are
    // re-derived against the floating origin — so it stays INVENTED for now.
    const float kCharH = 2.0f;               // INVENTED — see above
    const float kCharRadius = 0.45f;         // INVENTED — pairs with kCharH, not pm.radius()
    const float kStepUp = 0.6f;              // INVENTED — no source found
    // Walk/run speed is the engine's per-tick product now (physics_player.hpp
    // player_speed): CASTINFO+0x48 x stick x anime_Movable, x2.2 x a per-character
    // constant while running. The old "X at +0xdc" was the anim VM's +0xdc = actor+0x9cc
    // = anime_Movable, which avm.movable() already supplies.
    const float kOriginH = pm.height();   // +0x324 = 1.075: origin height above the floor
    // INVENTED: the game paces footsteps from ASO animation events, which we do not
    // run yet. One step per this much ground travel is a stand-in for that; the
    // surface lookup and variant cycling it feeds are the game's own.
    // Footstep cadence is the game's: a countdown in ticks reloaded from the surface
    // record (walk 20, run 13 — see surface.hpp). No invented distance any more.
    int step_timer = 0;               // actor+0x4bf
    unsigned step_n = 0;              // actor+0x4c0, the step counter (variant = n & mask)
    // Base player sprites: C_102/103/104 = Yunica / Hugo / Toal. (C_110-115 are the
    // late-game / weapon-state variants — wrong look for normal play.)
    auto char_aia = [](int c) -> const char* {
        return c == 2 ? "C_103.AIA" : c == 3 ? "C_104.AIA" : "C_102.AIA";
    };
    // Canonical gameplay animation tags — identical across every player sprite
    // (user-identified from animation GIFs; see tools/aia_gif.py). Resolve by TAG,
    // never by image index: the indices shift from character to character.
    // Tag 1 (run) is the dash, which the game gates behind an item — not wired yet.
    const std::uint32_t kTagWalk = 0x0, kTagRun = 0x1, kTagIdle = 0x2, kTagJump = 0x3;
    (void)kTagWalk; (void)kTagRun; (void)kTagIdle; (void)kTagJump;

    // The character is driven by the game's OWN animation script (ASO), not by a
    // state machine of ours. The players' scripts are C_010/C_020/C_030 — the only
    // three ASOs with no same-named AIA, because they *choose* their sprite:
    // anime_File(id) indexes the exe's AIA name table, and Flag_IsEqualInt(161,1)
    // picks the cutscene set (c_102) over the gameplay set (c_111).
    //
    // Section ids double as the action: 0 walk, 1 run/dash, 2 idle (the VM's initial
    // state), 3 jump. The script supplies (file, tag, frame); `dir` is NOT in it —
    // that comes from the actor's facing, which is what closes the AIA formula.
    auto char_aso = [](int c) -> const char* {
        return c == 2 ? "C_020.ASO" : c == 3 ? "C_030.ASO" : "C_010.ASO";
    };
    Aso aso = Aso::load(asset_root / "ANI" / char_aso(character));
    AsoVm avm;
    avm.bind(&aso);
    // The PLAYER's ASO reads the game flags directly. Flag 141 (DAT_0076bb50) IS the
    // Boost-active flag (audit event_vm #36): FUN_00463f40 sets it when Boost starts and
    // FUN_00462c40 clears it when the timer runs out; C_030.ASO (Toal) branches on it to pick
    // the demon form. It was never the S_1000 intro trigger.
    avm.set_flags(game.g_flags, 0x200);

    CharSprite chr;
    std::string cur_aia;
    // Load whatever sprite the script asks for. Falls back to our old hardcoded
    // choice only if the script gives nothing (e.g. a missing ASO).
    auto sync_aia = [&]() -> bool {  // true if it swapped the AIA this call
        const char* want = aso_aia_name(avm.file_id());
        const std::string stem = (want && *want) ? std::string(want)
                                                 : std::string(char_aia(character)).substr(0, 5);
        if (stem == cur_aia) return false;
        if (chr.load(g, asset_root / "ANI" / (stem + ".AIA"), 0)) {
            cur_aia = stem;
            std::printf("  [aso] anime_File(%d) -> %s.AIA\n", avm.file_id(), stem.c_str());
            return true;
        }
        return false;
    };
    avm.tick();          // run to the first Pattern so file/tag/frame are populated
    sync_aia();
    int facing = 2;              // dir2 = back to camera (facing north, into the room)
    // Edge-trigger state: the SEN trigger index the player is currently inside (-1 = none).
    // A warp fires only on ENTERING a volume (the engine arms on entry), so arriving at a
    // door-mouth coordinate -- which sits inside the return door's own volume -- does not
    // instantly warp back. inside_trigger is primed to that door at spawn.
    int inside_trigger = -1;
    std::uint32_t curAnim = std::uint32_t(std::max(0, chr.find_tag(std::uint32_t(avm.anim_tag()))));
    int animFrame = 0, shownFrame = -1;
    chr.set_anim(g, curAnim, 0);

    auto center = [](const Room& r) -> Vec3 {
        return {(r.mesh.bmin.x + r.mesh.bmax.x) * 0.5f,
                (r.mesh.bmin.y + r.mesh.bmax.y) * 0.5f,
                (r.mesh.bmin.z + r.mesh.bmax.z) * 0.5f};
    };
    auto eye_h = [&] { return room_diag(room) * 0.02f; };
    auto spawn_player = [&] {
        // The room's own spawn point: the SCT's CAST_ADOL record. Room centre is
        // only a fallback for rooms that don't place the player.
        const CastPlacement* sp = room.cast.player_spawn();
        const Vec3 c = sp ? sp->position : center(room);
        const float fl = floor_at(phys_mesh(room), c.x, c.z, room.mesh.bmax.y + room_diag(room));
        feet = {c.x, (fl > kNoFloor ? fl : c.y), c.z};
        pv = VerticalState{}; pvel = {}; last_floor = feet.y; grounded = pv.grounded;
        std::printf("spawn: %s(%.1f,%.1f,%.1f) floor=%s\n",
                    sp ? "CAST_ADOL" : "room centre", c.x, c.y, c.z,
                    fl > kNoFloor ? std::to_string(fl).c_str() : "none");
        if (fl <= kNoFloor && !sp) fly = true;  // no floor and no spawn -> free-fly
    };
    spawn_player();

    // Persistent per-kind enemy scripts + their attack section. The AsoVm in each
    // EnemyRender binds to a script here, so these must outlive the render list (which
    // is rebuilt on every spawn). Loaded and probed once per stem.
    std::unordered_map<std::string, Aso> enemy_scripts;
    std::unordered_map<std::string, int> enemy_atk_section;
    auto enemy_script = [&](const std::string& stem) -> Aso* {
        if (stem.empty()) return nullptr;
        auto it = enemy_scripts.find(stem);
        if (it == enemy_scripts.end()) {
            Aso a = Aso::load(asset_root / "ANI" / (stem + ".ASO"));
            it = enemy_scripts.emplace(stem, std::move(a)).first;
            // The attack section: the highest-priority one that arms a hit-volume. Basic
            // enemies have exactly one (M_100 -> section 7, a wind-up then a strike).
            int sec = -1, best_type = -1;
            if (it->second.ok())
                for (const Aso::Section& s : it->second.sections()) {
                    AsoVm probe;
                    probe.bind(&it->second);
                    if (!probe.set_section(s.id, /*force*/true)) continue;
                    // An attack is any section that arms a hit-volume OR fires a bullet —
                    // the same rule the player's own combo probe uses. Keying on hitboxes
                    // alone left every RANGED enemy inert: 14 shooter scripts (M_150, M_170,
                    // M_230, M_250, M_290, M_310, M_390, M_450, M_460, M_480, M_490, M_720,
                    // M_860, M_870) contain no GenerateCollision at all, so they scored -1
                    // and never attacked; several more picked their melee section and so
                    // never shot.
                    bool arms = false;
                    for (int t = 0; t < 150 && !arms; ++t) {
                        probe.tick();
                        if (probe.armed_hitbox() || !probe.bullets().empty()) arms = true;
                    }
                    if (arms && s.type > best_type) { best_type = s.type; sec = s.id; }
                }
            enemy_atk_section[stem] = sec;
        }
        return it->second.ok() ? &it->second : nullptr;
    };
    // Bind each freshly-built enemy to its script + attack section, and start it in idle.
    auto bind_one_enemy = [&](EnemyRender& e) {
        {
            Aso* a = enemy_script(e.stem);
            if (!a) return;
            e.avm.bind(a);
            // Resting section: 2 (idle for most), else 0, else whatever exists first —
            // set_section is what makes AsoVm::ok() true, so it must land on a real one.
            int idle = a->find(2) ? 2 : (a->find(0) ? 0 : a->sections().front().id);
            e.idle_section = idle;
            e.avm.set_section(idle, /*force*/true);
            e.atk_section = enemy_atk_section[e.stem];
            if (e.actor) {
                if (const Enemy* er = etab.get(e.actor->cast_id))
                {
                    e.aip = {er->ai_turn, er->ai_base, er->ai_walk, er->ai_run, er->ai_sight, {}};
                    std::copy(std::begin(er->ai_raw), std::end(er->ai_raw), e.aip.raw);
                }
                e.origin_h = cast_motion(casts, e.actor->cast_id).height();
                e.actor->height = e.origin_h;
                e.actor->radius = cast_motion(casts, e.actor->cast_id).radius();   // +0x328
                // Engine heading basis (cos, sin) from the actor's (sin, cos) yaw.
                e.facing = e.actor->ai.heading = 1.5707963f - e.actor->yaw;
            }
        }
    };
    auto bind_enemy_aso = [&] { for (EnemyRender& e : render) bind_one_enemy(e); };

    // Barrier fight (kekkai). A room's KEKKAIZAKO_*.XSO polls Chr_IsSeedLiving(1) and,
    // when the last group-1 seed enemy dies, grants the progress flag its chest guard
    // tests (EVENTVM.md §6: S_1001 -> flag 301, which S_BOX01 requires before it opens).
    std::vector<Xso> barrier_scripts;        // this room's KEKKAIZAKO_*.XSO (the poll + grant)
    std::vector<Xso> barrier_break_scripts;  // KEKKAIBREAK_*.XSO (Eft_Kill the ward + camera)
    bool barrier_seeded = false;        // a group-1 seed spawned this visit
    bool barrier_cleared = false;       // its flag has already been granted this visit
    bool barrier_break_pending = false; // grant done; the main loop must play KEKKAIBREAK
    bool room_events_dirty = true;      // (re)run the room's event scripts on the manager

    // Register any SCRIPT-spawned pickup that the mesh's box_/save_ scan can't see. The
    // S_1004 Flabellum altar is the one case in the game: its pickup (TALKITEM.XSO) fires
    // on interacting with the item stand — a 0x12c-spawned actor (entity 159), not an SOB
    // object. Add an interactable at the item's live position bound to TALKITEM (obtained
    // -flag 304). Must re-run after every room (re)build, since load_room rebuilds the mesh.
    auto register_script_pickups = [&] {
        const fs::path ti = room.dir / "TALKITEM.XSO";
        if (!fs::exists(ti)) return;                       // S_1004 only
        const Actor* item = world.find(159);               // the altar item stand
        if (!item) return;
        for (const Interactable& e : room.mesh.interactables)
            if (e.name == "altar_item") return;            // already registered this build
        Interactable it;
        it.name = "altar_item";
        it.position = item->pos;
        it.script = ti.string();
        it.opened_flag = 304;
        room.mesh.interactables.push_back(std::move(it));
        std::printf("  [altar] registered Flabellum pickup at (%.1f,%.1f,%.1f)\n",
                    item->pos.x, item->pos.y, item->pos.z);
    };

    auto spawn_enemies = [&] {
        // Populate the world the engine's way: run S_xxxx.XSO (op 0xa0 -> SCT bulk,
        // op 0x12c -> scripted), seeded with the persistent game flags. Then load a
        // sprite for every spawned cast (incl. 0x12c hub NPCs the SCT never named) and
        // pair them. Stats come from ONE path (ActorWorld::spawn): SCT name / cast_type
        // -> CASTINFO -> enemy_index -> ENEMYINFO -> GAMEINFO. Real names = CASTINFO+0x10.
        populate_room(world, room, game.g_flags);
        bullets.clear();                                      // before the effects they hold
        gas_owner.clear();
        fxsim.clear();
        actor_mats.clear();
        efx.kinds.clear();
        efx.init_from_world(g, asset_root, world, casts);
        render = build_enemy_render(world, casts, efx);
        bind_enemy_aso();
        // Detect this room's barrier fight: load its KEKKAIZAKO_*.XSO poll scripts and
        // note whether S_xxxx.XSO's 0x12c actually spawned a group-1 seed (it is gated
        // behind the very flag the barrier grants, so a re-visit spawns nothing and we
        // must not re-fire). The poll itself runs in clear_barrier, below.
        barrier_scripts.clear();
        barrier_break_scripts.clear();
        barrier_cleared = false;
        barrier_seeded = world.any_seed_alive(1);
        {
            std::error_code bec;
            for (const auto& de : fs::directory_iterator(room.dir, bec)) {
                std::string fn = de.path().filename().string();
                for (char& c : fn) c = char(std::toupper((unsigned char)c));
                const bool xso = fn.size() >= 4 && fn.compare(fn.size() - 4, 4, ".XSO") == 0;
                try {
                    if (xso && fn.rfind("KEKKAIZAKO", 0) == 0)
                        barrier_scripts.push_back(Xso::parse(read_bytes(de.path())));
                    else if (xso && fn.rfind("KEKKAIBREAK", 0) == 0)
                        barrier_break_scripts.push_back(Xso::parse(read_bytes(de.path())));
                } catch (const std::exception&) {}
            }
        }
        if (!render.empty()) {
            int statted = 0;
            for (const EnemyRender& e : render) statted += (e.actor->max_hp > 0);
            std::printf("  %zu enemies spawned (%d with game stats)\n", render.size(),
                        statted);
            if (std::getenv("CLERIA_ENEMY"))
                for (const EnemyRender& e : render) {
                    int grp = 0;
                    for (int gq = 1; gq <= 4; ++gq) if (e.actor->tag.in_group(gq)) { grp = gq; break; }
                    std::printf("    %-14s (%-6s) lv%-3d HP %6.0f  ATK %5.0f  DEF %5.0f%s\n",
                                e.display.c_str(), e.stem.c_str(), e.actor->level,
                                e.actor->max_hp, e.actor->atk, e.actor->def,
                                grp ? ("  <-- GROUP " + std::to_string(grp) + " (should glow)").c_str() : "");
                }
        }
        register_script_pickups();   // altar item etc. (needs the populated world)
    };

    spawn_enemies();

    apply_room_bgm(room.name);

    // Scripted map objects (src/cleria/meshobj.hpp, build/event_re/WAITOPS.md §4): moves,
    // clip playback and the event bit, re-baked into the room's render + collision meshes
    // when they change. `mesh_delta` is each object's displacement in the last mesh step (the
    // platform carry reads it next tick, MOVER.md §3.3).
    MeshWorld meshes;
    std::unordered_map<std::string, Vec3> baked_pos, mesh_delta;
    std::string player_floor_obj;            // the object the player stands on (+0x540)
    Vec3 carry{};                            // +0x5b0..: the platform carry
    // (Re)seed the mesh runtime from the room's objects. keep = a reload of the same room:
    // objects keep their runtime state and are re-baked from it.
    auto load_meshes = [&](bool keep) {
        std::unordered_map<std::string, MeshObj> old;
        if (keep) old = meshes.all();
        meshes.clear(); baked_pos.clear(); mesh_delta.clear();
        for (const RoomMesh::ObjRange& r : room.mesh.object_ranges) {
            std::vector<MeshClip> clips;
            for (const auto& c : r.clips) clips.push_back({c.name, c.start, c.end, c.next, c.rate});
            MeshObj& m = meshes.add(r.name, r.obj.position, std::move(clips));
            baked_pos[r.name] = r.obj.position;
            for (std::size_t i = 0; i < m.clips.size(); ++i)        // built posed at a clip's end
                if (!r.posed_clip.empty() && m.clips[i].name == r.posed_clip) {
                    m.sect = int(i); m.time = float(m.clips[i].end); m.count = 1;
                }
            auto it = old.find(r.name);
            if (it != old.end()) {
                const Vec3 sp = m.spawn;
                m = it->second; m.spawn = sp;
                m.dirty_pos = m.dirty_pose = true;
            }
        }
        carry = {};
    };

    load_meshes(false);
    float cooldown = 1.5f;
    auto transition = [&](const std::string& dest, const SenTrigger* via = nullptr) {
        auto it = g.sob_index.find(dest);
        if (it == g.sob_index.end()) { std::printf("  (no SOB for %s)\n", dest.c_str()); return; }
        std::printf(">> entering %s\n", dest.c_str());
        // ** Copy everything we need off `via` NOW. ** It points into the CURRENT room's
        // sen.triggers, which `room = std::move(next)` below frees -- reading via after
        // that is a use-after-free (it silently read as "no arrival", dropping the player
        // at the SCT default = the wrong entrance on every return). Copy by value first.
        const bool has_arr = via && via->has_arrival();
        const Vec3 arr = via ? via->arrival_pos : Vec3{};
        const bool has_fac = via && via->has_facing();
        const int fac_deg = via ? via->arrival_facing_deg : 0;
        // Every scene entry, door or script (FUN_00431840; SCENEFLOW.md 6.2): a dead player is
        // revived, and the per-scene flags are reset before the room's scripts read them.
        if (player.hp < 1) { player.hp = player.max_hp; player.sp_cur = player.sp_max; }
        game.g_flags[160] = 0;                                   // the sub-area (0 in the data)
        for (int f : {161, 209, 156, 130, 132, 189, 190, 194, 197, 200, 135, 497, 506}) game.g_flags[f] = 0;
        for (int f = 165; f <= 180; ++f) game.g_flags[f] = 0;    // the per-room scratch block
        game.g_flags[185] = 1;
        Room next = load_room(g, asset_root, it->second, game.g_flags, character);
        free_room(room, g.white_bind);
        room = std::move(next);
        load_meshes(false);
        apply_load_effects(room);
        room_events_dirty = true;   // the new room's event scripts start next frame
        apply_room_bgm(room.name);
        if (fly) {
            spawn_in(cam, room);
        } else {
            spawn_player();
            // The exit's SEN arrival (+0x22c) overrides the SCT default (FUN_00434020)
            // whenever the exit's label is set -- verbatim, with NO zero-test: a (0,0,0)
            // arrival is a LITERAL door-mouth coordinate, not "unset" (build/warp_re/
            // DOORRETURN.md: S_1004->S_1005's (0,0,0) is exactly S_1005's door mouth). The
            // per-door correctness comes from applying the CROSSED trigger's own arrival,
            // not a by-destination lookup -- rooms joined by 2-3 doors each carry a
            // different arrival, and picking the wrong one lands you at the wrong door.
            if (has_arr) {
                const float fl = floor_at(phys_mesh(room), arr.x, arr.z,
                                          room.mesh.bmax.y + room_diag(room));
                feet = {arr.x, (fl > kNoFloor ? fl : arr.y), arr.z};
                pv = VerticalState{}; pvel = {}; last_floor = feet.y; grounded = pv.grounded;
                std::printf("  arrival @ (%.1f,%.1f,%.1f) [SEN]\n", feet.x, feet.y, feet.z);
            }
            // Place the camera for the new room NOW: the real warp is hidden by a fade so
            // it cuts rather than swooping, and it gives the correct screen basis for the
            // arrival facing below.
            update_camera(room, feet, 0.0f, /*snap=*/true);
            if (g_cam.on) apply_cam_state(cam, feet);
            // Arrival facing (SEN +0x238, degrees) -> world heading -> screen sprite dir.
            // The world forward is (sin deg, cos deg), i.e. our yaw = deg directly, NO flip.
            // Provenance: build/warp_re/FACING{,_APPLY}.md trace the field to actor+0x314
            // via FUN_00443a80 = deg2rad(90-deg), consumed screen-relatively. That 90 is
            // the engine's own heading basis (0 -> +X); OUR Actor.yaw is 0 -> +Z (vx=sin,
            // vz=cos, world.cpp), a 90 offset, so the flip cancels and the degree maps to
            // our yaw as-is. Confirmed against geometry, not just the decode: across 219
            // shipped door pairs this forward points INTO the destination room (mean
            // alignment 0.81), where the un-cancelled (cos deg, sin deg) scores -0.03 and
            // aims at the wall you arrived through. heading_to_facing then maps it exactly
            // as walking that way would, so the warped-in player faces the right way.
            if (has_fac) {
                const float yaw = float(fac_deg) * 3.14159265358979323846f / 180.0f;
                face_dir = {std::sin(yaw), 0.0f, std::cos(yaw)};
                facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                std::printf("  arrival facing %d deg -> dir %d\n", fac_deg, facing);
            }
        }
        // Prime the edge-trigger: mark whatever door volume the player spawned inside, so
        // a door-return that lands them in the doorway doesn't immediately warp back.
        inside_trigger = -1;
        if (!fly) {
            const Vec3 pr = {feet.x, feet.y + kCharH * 0.5f, feet.z};
            for (std::size_t i = 0; i < room.sen.triggers.size(); ++i)
                if (room.sen.triggers[i].contains(pr)) { inside_trigger = int(i); break; }
        }
        spawn_enemies();
        cooldown = 1.5f;
        g_tick_reset = true;  // don't catch up on the load stall (the exe's DAT_0075aed5)
    };
    // Reload the current room (re-gates chests for the new character / state).
    // rerun_events=false rebuilds ONLY the visual mesh (statue model swap) without re-running
    // the room's event scripts — safe to call mid-cutscene (the goddess-cleanse swap happens
    // while the world is frozen, and re-running events then would restart BATTLESTART etc.).
    // Objects destroyed this session (SOB trigger class 1). Breakables carry no opened-flag
    // — @Break_* writes none — so this is deliberately session-scoped, matching retail where
    // pots respawn when you re-enter from a load.
    std::unordered_set<std::string> broken_objects;
    auto reload_room = [&](bool rerun_events = true) {
        auto it = g.sob_index.find(room.name);
        if (it == g.sob_index.end()) return;
        Room next = load_room(g, asset_root, it->second, game.g_flags, character,
                              broken_objects.empty() ? nullptr : &broken_objects);
        free_room(room, g.white_bind);
        room = std::move(next);
        load_meshes(/*keep*/true);
        apply_load_effects(room);
        if (rerun_events) room_events_dirty = true;   // re-run scripts for the re-gated room
        register_script_pickups();  // the rebuilt mesh lost script-spawned interactables
    };

    // Positional SFX: pan from the camera's right axis, distance from the player
    // (the engine's listener object; its emitter->listener lerp tames the pan the
    // same direction). Curve/pan-law caveats live on Audio::play_at.
    auto sfx_at = [&](int id, const Vec3& p, float vol) {
        const Vec3 f = cam.forward();
        const float rl = std::sqrt(f.x * f.x + f.z * f.z);
        const float rx = rl > 1e-4f ? f.z / rl : 0.0f;     // right = (fz, -fx) on XZ
        const float rz = rl > 1e-4f ? -f.x / rl : 1.0f;
        const float dx = p.x - feet.x, dy = p.y - feet.y, dz = p.z - feet.z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float pan = dist > 1e-4f ? (dx * rx + dz * rz) / dist : 0.0f;
        audio.play_at(id, pan, dist, vol);
    };

    // ---- save / load (PLAN.md Phase 2; codec src/cleria/save.*) -------------------
    // Goddess-statue UI state machine (STATUE_MENU.md): 0 = closed, 1 = "What will you
    // do?" choice, 3 = blessing list (Save opens the pause book's Save list). The cleansing animation
    // (Yunica/Hugo first-use) plays SAVE.XSO (the goddess cleanse cutscene) the first
    // time a sealed statue (flag 300 == 0) is touched — see the cleanse wiring below;
    // blessings list real costs but effects are not implemented.
    int statue_ui = 0, statue_sel = 0, statue_reveal = 0;
    bool cleanse_pending = false;   // a cleanse cutscene (SAVE.XSO) is playing on csMgr
    std::int32_t prev_flag300 = 0;  // to detect the cleanse flag flipping (statue model swap)
    bool statue_reload_pending = false;  // reload for the model swap once the cutscene thaws
    auto capture_save = [&]() -> SaveState {
        SaveState s;
        std::memcpy(s.flags, game.g_flags, sizeof s.flags);
        std::memcpy(s.item_arr, game.item_arr, sizeof s.item_arr);
        s.hp_max = float(player.max_hp); s.hp_cur = float(player.hp);
        s.exp = player.exp; s.level = player.level; s.sp = player.sp;
        s.accessory = inv.accessory; s.held = inv.held; s.magic = inv.magic;
        for (int c = 0; c < 4; ++c) s.tier[c] = inv.tier[c];
        s.hp_bonus = inv.hp_bonus; s.atk_buff = inv.atk_buff; s.def_buff = inv.def_buff;
        s.luck_base = inv.luck_base; s.timer_atk = inv.timer_atk; s.timer_def = inv.timer_def;
        std::memcpy(s.warp_on, warp_on, sizeof warp_on);
        std::memcpy(s.manual_on, manual_on, sizeof manual_on);
        s.scene = room.name;
        s.pos_x = feet.x; s.pos_z = feet.z;
        s.yaw = std::atan2(face_dir.x, face_dir.z) * 180.0f / 3.14159265f;  // SEN-style degrees
        return s;
    };
    auto do_save = [&](int slot) {
        std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        char date[16], tim[16];
        std::snprintf(date, sizeof date, "%02d/%02d/%04d", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900);
        std::snprintf(tim, sizeof tim, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
        const SaveRecord rec = to_record(capture_save(), room.name, date, tim);
        // Keys: any nonzero pair — the engine rerolls them per save too (SAVELOAD §4.1).
        std::random_device rd;
        const std::vector<std::uint8_t> raw = rec.encode(rd() | 1, rd() | 1);
        std::error_code ec;
        fs::create_directories(save_dir(), ec);
        std::ofstream f(slot_path(slot), std::ios::binary);
        f.write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size()));
        hud.message(f ? ("Saved to slot " + std::to_string(slot + 1))
                      : "Save FAILED (disk?)");
    };
    auto apply_loaded = [&](const SaveState& s) {
        std::memcpy(game.g_flags, s.flags, sizeof s.flags);
        std::memcpy(game.item_arr, s.item_arr, sizeof s.item_arr);
        character = std::clamp(s.character(), 1, 3);
        player.level = std::clamp(s.level, 1, 60);
        player.exp = s.exp; player.sp = s.sp;
        inv.accessory = s.accessory; inv.held = s.held; inv.magic = s.magic;
        for (int c = 0; c < 4; ++c) inv.tier[c] = s.tier[c];
        inv.hp_bonus = s.hp_bonus; inv.atk_buff = s.atk_buff; inv.def_buff = s.def_buff;
        inv.luck_base = s.luck_base; inv.timer_atk = s.timer_atk; inv.timer_def = s.timer_def;
        std::memcpy(warp_on, s.warp_on, sizeof warp_on);
        std::memcpy(manual_on, s.manual_on, sizeof manual_on);
        inv_sanitize(game.g_flags, inv);                  // FUN_00431840's clamps after a load
        player.max_hp = int(std::lround(s.hp_max));
        player.hp = int(std::lround(s.hp_cur));
        player.hp_lag = float(player.hp);
        // Re-bind the character's ASO (same sequence as the Tab character swap).
        aso = Aso::load(asset_root / "ANI" / char_aso(character));
        avm = AsoVm{};
        avm.bind(&aso);
        avm.set_flags(game.g_flags, 0x200);
        avm.tick();
        sync_aia();
        curAnim = std::uint32_t(std::max(0, chr.find_tag(std::uint32_t(avm.anim_tag()))));
        animFrame = 0; shownFrame = -1;
        if (!s.scene.empty() && s.scene != room.name) transition(s.scene);
        else reload_room();
        // Position: the save's continue-snapshot x/z + facing; y re-grounds via the
        // floor raycast (the engine does not save y either).
        feet.x = s.pos_x; feet.z = s.pos_z;
        const float fl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + room_diag(room));
        if (fl > kNoFloor) feet.y = fl;
        pv = VerticalState{}; pvel = {}; last_floor = feet.y;
        const float rad = s.yaw * 3.14159265f / 180.0f;
        face_dir = {std::sin(rad), 0, std::cos(rad)};
    };

    // The barrier fight, now wired. A KEKKAIZAKO_*.XSO polls its group-1 seeds:
    //
    //     0: 0x131(1)       <- acc = any group-1 seed still alive?   (now implemented)
    //     3: jz -> 8        <- all dead -> grant
    //     5: 0xf0 / jmp 0   <- else wait a frame and loop
    //     8: 0x64(301, 1)   <- set the progress flag (S_1001 -> 301)
    //
    // 0x131 is Chr_IsSeedLiving (FUN_00570640, EVENTVM.md §6): ActorWorld answers it
    // from the live registry. We poll each frame; the instant the last seed dies we run
    // the script to completion on the PERSISTENT VM (with the seed gone, 0x131 reads
    // false, so it falls through to its grant) and re-gate the room, so the chest guard
    // S_BOX01 — which requires flag 301 before it will open — now unlocks. This is a
    // real fight now, not the old "running it grants the flag for free": no seed spawns
    // (the 0x12c is gated behind 301) or none die => no grant.
    auto clear_barrier = [&] {
        if (barrier_scripts.empty() || barrier_cleared || !barrier_seeded) return;
        if (world.any_seed_alive(1)) return;   // seeds still alive: keep fighting
        XsoHost* saved = game.host;
        game.host = &world;
        for (const Xso& ks : barrier_scripts) { game.reset_run(); game.run(ks); }
        game.host = saved;
        barrier_cleared = true;
        // The break sequence (KEKKAIBREAK: Fix -> Scroll_MoveTo the ward -> Eft_Kill it ->
        // Scroll_Return -> Unfix) is a real camera CUTSCENE. Play it through the script
        // manager (the main loop starts it when no other cutscene is up) so the pan is
        // visible and the ward vanishes mid-pan, then re-gate the chest when it ends. With
        // no break script, re-gate immediately (old behaviour).
        if (barrier_break_scripts.empty()) { reload_room(); hud.message("The barrier fades."); }
        else barrier_break_pending = true;
        std::printf("  [barrier] all seeds down -> ran %zu KEKKAIZAKO script(s); flag 301 = %d; "
                    "break %s\n", barrier_scripts.size(), game.g_flags[301],
                    barrier_break_scripts.empty() ? "none" : "queued (cutscene)");
    };

    // ## In-engine cutscene runner (CUTSCENE_PLAN.md B3/B9) — MVP: one debug-triggered
    // script driven by the resumable XSO VM (CUTSCENE_COROUTINE.md). The host receives the
    // dialogue ops and gates Wait_Prompt on "text fully revealed + advance pressed"; the
    // fixed-step loop ticks the coroutine and FREEZES the world while it runs (op 0xb6 Fix).
    struct CutsceneHost : XsoHost {
        std::string name, text;
        int speaker = 0;
        bool box_active = false;
        int reveal = 0;           // typewriter char count (advanced by the loop)
        bool advance = false;     // set by input; consumed to advance one line
        ScriptManager* mgr = nullptr;   // for Wait_Script (is the named child still live?)
        int voice = -1;           // Window_Msg param1 — doubles as the FACE FRAME index
        std::uint8_t text_r = 255, text_g = 255, text_b = 255;   // body colour (<color:> tag)
        // Falcom colour markup: a line may lead with `<color:0xAARRGGBB>` (and rarely more
        // `<...>` tags). Apply the FIRST colour to the body and strip every tag so the
        // literal markup never shows (and the typewriter counts only visible chars).
        static std::string parse_tags(std::string s, std::uint8_t& r, std::uint8_t& gg, std::uint8_t& b) {
            r = gg = b = 255;
            bool got = false;
            std::string out;
            for (std::size_t i = 0; i < s.size();) {
                if (s[i] == '<') {
                    const std::size_t e = s.find('>', i);
                    if (e == std::string::npos) { out += s.substr(i); break; }
                    const std::string tag = s.substr(i, e - i + 1);   // "<color:0xAARRGGBB>"
                    const std::size_t hx = tag.find("0x");
                    if (!got && hx != std::string::npos && hx + 10 <= tag.size()) {
                        auto hexval = [&](std::size_t o) {
                            std::uint32_t v = 0;
                            for (std::size_t k = 0; k < 8 && o + k < tag.size(); ++k) {
                                const char c = tag[o + k];
                                v = v * 16 + (c >= '0' && c <= '9' ? c - '0'
                                            : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                            : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
                            }
                            return v;   // 0xAARRGGBB
                        };
                        const std::uint32_t argb = hexval(hx + 2);
                        r = std::uint8_t((argb >> 16) & 0xFF);
                        gg = std::uint8_t((argb >> 8) & 0xFF);
                        b = std::uint8_t(argb & 0xFF);
                        got = true;
                    }
                    i = e + 1;   // drop the whole tag
                } else { out += s[i]; ++i; }
            }
            return out;
        }
        // FUN_00573e60: an empty NAME defaults to the speaker's cast record name (+0x10).
        std::function<std::string(int)> speaker_name_fn;
        void window_msg(int sp, int vc, const std::string& nm, const std::string& tx) override {
            speaker = sp; voice = vc;
            name = (nm.empty() && sp != 0 && speaker_name_fn) ? speaker_name_fn(sp) : nm;
            text = parse_tags(tx, text_r, text_g, text_b);
            reveal = 0; box_active = true; acquire_active = false;
        }
        void window_close() override {
            box_active = false; name.clear(); text.clear(); acquire_active = false;
        }
        // The "Acquired X" box (op 0xd5 Window_Treasure): NOT the running-dialogue box — it's a
        // separate DIALOG0 9-slice centred at the op's (x,y), with the item ICON + caption
        // (FUN_005781f0). The item name is resolved by the view (which owns the item table); the
        // view renders the box from these fields. The following Wait_Prompt dismisses it.
        std::function<std::string(int)> item_name_fn;
        bool acquire_active = false; int acquire_id = -1;
        float acquire_x = 512.0f, acquire_y = 122.0f;   // authored 1024x768 space (x=512 => centred)
        void acquire_item(float x, float y, int id, const std::string& tx) override {
            acquire_active = true; acquire_id = id; acquire_x = x; acquire_y = y;
            name.clear(); speaker = 0; voice = -1;
            // FUN_005781f0 copies the op3 string into the window text (e.g. "Acquired /
            // <color:..>Ring Mail<color:>."); audit event_vm #27. The item NAME is a fallback
            // only for a NULL string.
            text = !tx.empty() ? parse_tags(tx, text_r, text_g, text_b)
                               : (item_name_fn ? item_name_fn(id) : ("item " + std::to_string(id)));
            reveal = int(text.size());   // shown at once, no typewriter
            box_active = true;           // gate the following Wait_Prompt
        }
        std::string caption_name; int caption_timer = 0;   // 0xe2 area-name card (frames left)
        void caption(const std::string& nm) override { caption_name = nm; caption_timer = 210; }
        // Standing bust portraits (0xe3/0xe4/0xe5). The intro shows one at a time (t_840a the
        // captain, then v_003a the villain), but hold a small list so overlapping opens work.
        struct Bust { std::string name; float x, y; };
        std::vector<Bust> busts;
        void portrait_open(const std::string& nm, float x, float y) override {
            for (Bust& b : busts) if (b.name == nm) { b.x = x; b.y = y; return; }
            busts.push_back({nm, x, y});
        }
        void portrait_move(const std::string& nm, float x, float y) override {
            for (Bust& b : busts) if (b.name == nm) { b.x = x; b.y = y; return; }
            busts.push_back({nm, x, y});
        }
        void portrait_close(const std::string& nm) override {
            busts.erase(std::remove_if(busts.begin(), busts.end(),
                        [&](const Bust& b) { return b.name == nm; }), busts.end());
        }
        // SKIP: while set, EVERY wait predicate reports "done" so every live script
        // bursts to its end (its side effects — flags/spawns/warps — still run). This
        // clears not just Wait_Prompt but Wait_Script / Wait_Window / Wait_Sound /
        // Wait_Fader too, which a plain Wait_Time collapse can't. Set by the skip loop.
        bool skipping = false;
        bool wait_prompt() override {
            if (skipping) return true;
            if (reveal < int(text.size())) return false;    // line still typing out
            if (advance) { advance = false;                 // one press advances / dismisses
                if (acquire_active) { acquire_active = false; box_active = false; }
                return true; }
            return false;
        }
        // Screen fader (EVENTVM_HANDLERS.md §4): a fullscreen colored overlay, alpha
        // ramping current->target at fade_rate/frame. fade_advance() steps it each tick.
        float fade_cur = 0, fade_tgt = 0, fade_rate = 1;
        std::uint32_t fade_rgb = 0;   // 0xRRGGBB (default black)
        void fader_current(float a) override { fade_cur = std::clamp(a, 0.0f, 1.0f); }
        void fader_target(float a) override { fade_tgt = std::clamp(a, 0.0f, 1.0f); }
        void fader_color(int rgb, int) override { fade_rgb = std::uint32_t(rgb) & 0xffffffu; }
        void fader_time(int frames) override { fade_rate = frames > 0 ? 1.0f / frames : 1.0f; }
        void fade_advance() {
            const float d = fade_tgt - fade_cur, step = std::min(1.0f, fade_rate);
            fade_cur = (std::abs(d) <= step) ? fade_tgt : fade_cur + (d > 0 ? step : -step);
        }
        bool wait_fader() override { return skipping || fade_cur == fade_tgt; }
        // Scene flow (build/event_re/SCENEFLOW.md). A ChangeScene is applied at the start of
        // the next frame; until then every script is frozen (the caller's burst finishes).
        std::string pending_scene;
        void change_scene(const std::string& nm) override {
            if (!pending_scene.empty() || nm.empty()) return;    // refused while one is pending
            pending_scene = nm;
            if (mgr) mgr->paused = true;
        }
        // 0x94 SetHealSpot: the statue heal objects of this room (they die with it).
        struct HealSpot { float x, y, z; int cooldown = 0; };
        std::vector<HealSpot> heal_spots;
        void heal_spot(float x, float y, float z) override { heal_spots.push_back({x, y, z, 0}); }
        // 0xa7 / 0x92 / 0x93: the warp points, the manual pages, the Topic collection.
        std::uint8_t* warp_on = nullptr;
        std::uint8_t* manual_on = nullptr;
        std::uint8_t* topic_seen = nullptr;
        std::int32_t* topic_kills = nullptr;
        void warp_enable(int i) override { if (warp_on && i >= 0 && i < 24) warp_on[i] = 1; }
        void manual_enable(int i) override { if (manual_on && i >= 0 && i < 13) manual_on[i] = 1; }
        void topic_known(int i) override {
            if (!topic_seen || i < 0 || i >= 256) return;
            topic_seen[i] = 1;
            topic_kills[i] = std::min(topic_kills[i] + 1, 50);
        }
        // 0xa6 GoWarpPoint: ChangeScene(table 0x68c190[flag 137]) (SCENEFLOW.md §3).
        void go_warp_point() override {
            static const char* kWarpRooms[22] = {
                "s_10\\s_1000", "s_10\\s_1009", "s_10\\s_1011", "s_20\\s_2000", "s_20\\s_2013", "s_21\\s_2100",
                "s_20\\s_2012", "s_30\\s_3000", "s_30\\s_3006", "s_30\\s_3015", "s_30\\s_3014", "s_40\\s_4000",
                "s_41\\s_4104", "s_40\\s_4020", "s_50\\s_5000", "s_50\\s_5010", "s_50\\s_5014", "s_60\\s_6000",
                "s_60\\s_6010", "s_60\\s_6082", "s_60\\s_6053", "s_70\\s_7000"};
            if (!mgr) return;
            const int i = mgr->vm.g_flags[137];
            if (i >= 0 && i < 22) change_scene(kWarpRooms[i]);
        }
        // 0xcd CrossFade: [not modelled] the frozen-frame dissolve (purely visual in the corpus).
        // Scripted map objects (meshobj.hpp).
        MeshWorld* meshes = nullptr;
        const std::string* player_floor = nullptr;   // the object the player stands on
        mutable std::unordered_set<std::string> missing_warned;
        void mesh_move(const std::string& n, float x, float y, float z, float r, float s, bool off) override {
            if (meshes) meshes->move_to(n, {x, y, z}, r, s, off);
        }
        void mesh_set_motion(const std::string& n, const std::string& c, bool b, int st) override {
            if (meshes) meshes->set_motion(n, c, b, st);
        }
        void mesh_stop_motion(const std::string& n, int on) override { if (meshes) meshes->stop_motion(n, on); }
        void mesh_sleep(const std::string& n, bool on) override { if (meshes) meshes->sleep(n, on); }
        void mesh_set_position(const std::string& n, float x, float y, float z) override {
            if (meshes) meshes->set_position(n, {x, y, z});
        }
        void mesh_reset_position(const std::string& n) override { if (meshes) meshes->reset_position(n); }
        void mesh_enable_event(const std::string& n, bool on) override { if (meshes) meshes->enable_event(n, on); }
        void mesh_time_scale(const std::string& n, float s) override { if (meshes) meshes->set_time_scale(n, s); }
        bool mesh_moving(const std::string& n, bool* found) const override {
            if (skipping) { *found = true; return false; }
            const bool mv = meshes && meshes->moving(n, found);
            if (!*found && missing_warned.insert(n).second)
                std::printf("  [mesh] Wait_MeshMoving on missing object '%s' (the original hangs)\n", n.c_str());
            return mv;
        }
        bool mesh_motion_finished(const std::string& n, std::uint32_t k, bool* found) const override {
            if (skipping) { *found = true; return true; }
            const bool fin = meshes && meshes->motion_finished(n, k, found);
            if (!*found && missing_warned.insert(n).second)
                std::printf("  [mesh] Wait_MeshMotionFinish on missing object '%s' (the original hangs)\n", n.c_str());
            return fin;
        }
        bool chr_on_mesh(int id, const std::string& n) const override {
            if (id != 998 && id != 13) return false;           // only the player's contact is tracked
            std::string l = n;
            for (char& c : l) c = char(std::tolower((unsigned char)c));
            return player_floor && *player_floor == l;
        }
        bool wait_window(bool) override { return true; }
        bool wait_sound(int) override { return true; }
        // 0xf4 Wait_Script(name): a parent controller blocks until its spawned talk child
        // is gone. Answer from the live set (case-insensitive; script names vary in case).
        bool wait_script_done(const std::string& n) override {
            if (skipping) return true;
            if (!mgr) return true;
            for (const auto& i : mgr->live) {
                if (i.finished || i.name.size() != n.size()) continue;
                bool same = true;
                for (std::size_t k = 0; k < n.size(); ++k)
                    if (std::tolower((unsigned char)i.name[k]) != std::tolower((unsigned char)n[k])) { same = false; break; }
                if (same) return false;   // still running -> keep waiting
            }
            return true;
        }
        void scroll_moveto(float x, float y, float z, int frames) override { ::scroll_moveto(x, y, z, frames); }
        void scroll_return(int frames) override { ::scroll_return(frames); }
        void shake(float ax, float ay, float az, int frames) override { ::shake(ax, ay, az, frames); }
        void camera_set(int region, int) override { ::camera_set(region); }
        void camera_return(int) override { ::camera_return(); }
        // Effect ops forward to the world so a cutscene's Eft_Kill / Eft_LocatePosAddStr
        // really adds/removes the ward from world.effects() (the barrier break-pan).
        XsoHost* worldHost = nullptr;
        void kill_effect(const std::string& n) override { if (worldHost) worldHost->kill_effect(n); }
        void locate_effect(int id, float x, float y, float z, float a, float b,
                           const std::string& n) override {
            if (worldHost) worldHost->locate_effect(id, x, y, z, a, b, n);
        }
        // Cutscene spawns (op 0x12c): create a REAL actor in the world so the NPC is
        // drawn (Riko, the search party = CAST_M800, …), AND remember entity->cast so
        // the dialogue can resolve a speaker to its face set. `world_changed` tells the
        // view to (re)load the new cast's sprite and rebuild its render list.
        std::unordered_map<int, int> spawn_cast;
        bool world_changed = false;
        void spawn(int entity_id, int cast_type, float x, float y, float z, float yaw,
                   int group) override {
            if (entity_id > 0) spawn_cast[entity_id] = cast_type;
            if (worldHost) { worldHost->spawn(entity_id, cast_type, x, y, z, yaw, group);
                             world_changed = true; }
        }
        void despawn(int entity_id) override {
            if (worldHost) { worldHost->despawn(entity_id); world_changed = true; }
        }
        void enclose(float xmin, float zmin, float xmax, float zmax) override {   // op 0x10e
            if (worldHost) worldHost->enclose(xmin, zmin, xmax, zmax);
        }
        // Cutscene actor control. The PLAYER (13/999) is `*playerPos` (feet), not a world
        // actor, so move it here; NPCs forward to the world. This is what makes the intro
        // actually walk the player to the platform (op 0x143) instead of standing still.
        bool p_isplayer(int id) const { return id == 13 || id == 999; }
        // The player's +0x30c is CASTINFO[13]+0x48 = 0.056 for its whole life (nothing in
        // FUN_00465170 rewrites it); a scripted walk is 0.056 x scale x Movable per step.
        float player_speed30c = 0.056f;      // set from CASTINFO[13]+0x48 below
        std::function<float()> movable_fn;   // the player's ASO anime_Movable
        Vec3 p_move_tgt{}; float p_move_spd = 0, p_move_r2 = 0; bool p_moving = false;
        void chr_pos(int id, float x, float y, float z) override {
            if (p_isplayer(id)) { if (playerPosW) *playerPosW = {x, y, z}; p_moving = false; }
            else if (worldHost) worldHost->chr_pos(id, x, y, z);
        }
        void chr_dir(int id, float deg) override {
            if (p_isplayer(id)) { if (playerYawDeg) *playerYawDeg = deg; }
            else if (worldHost) worldHost->chr_dir(id, deg);
        }
        void chr_moveto(int id, float x, float z, float radius, float scale) override {
            if (p_isplayer(id)) {
                p_move_tgt = {x, 0, z};
                p_move_spd = player_speed30c * scale;                 // 0.056 x scale (x Movable below)
                const float r = std::max(0.025f, radius);
                p_move_r2 = r * r; p_moving = true;
            } else if (worldHost) worldHost->chr_moveto(id, x, z, radius, scale);
        }
        bool chr_moving(int id) const override {
            if (p_isplayer(id)) return p_moving;
            return worldHost && worldHost->chr_moving(id);
        }
        void chr_alpha(int id, float endA) override {
            if (!p_isplayer(id) && worldHost) worldHost->chr_alpha(id, endA);
        }
        void chr_lookat(int id, int target) override {
            if (!p_isplayer(id) && worldHost) worldHost->chr_lookat(id, target);
        }
        void chr_lookat_pos(int id, float x, float z) override {
            if (!p_isplayer(id) && worldHost) worldHost->chr_lookat_pos(id, x, z);
        }
        void chr_animate(int id, int motion) override {   // 0x151 pose (NPCs; player poses TODO)
            if (!p_isplayer(id) && worldHost) worldHost->chr_animate(id, motion);
        }
        // Advance the player's cutscene walk one frame (NPCs advance in the world).
        Vec3* playerPosW = nullptr; float* playerYawDeg = nullptr;
        void advance_player_move() {
            if (!p_moving || !playerPosW) return;
            const float dx = p_move_tgt.x - playerPosW->x, dz = p_move_tgt.z - playerPosW->z;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (dx * dx + dz * dz <= p_move_r2) { p_moving = false; }   // planar arrival test first
            else { const float st = p_move_spd * (movable_fn ? movable_fn() : 1.0f);
                   playerPosW->x += dx / d * st; playerPosW->z += dz / d * st;
                   if (playerYawDeg) *playerYawDeg = std::atan2(dx, dz) * 180.0f / 3.14159265f; }
        }
        // Forward the live predicates to the WORLD — a cutscene/controller that spawned
        // actors polls these to gate itself. Returning false made the barrier controller
        // (Chr_IsSeedLiving) think its guard was already dead and break on room entry.
        bool is_alive(int id) const override { return worldHost && worldHost->is_alive(id); }
        bool any_seed_alive(int grp) const override { return worldHost && worldHost->any_seed_alive(grp); }
        // op 0x13d: is the player inside the named .SEV box? Only the player (13) is
        // positioned here; other ids resolve to no actor -> false (the engine's null path).
        const Room* roomPtr = nullptr;
        const Vec3* playerPos = nullptr;
        bool chr_in_event_box(int entityId, const std::string& name) const override {
            if (entityId != 13 || !roomPtr || !playerPos) return false;
            const SceneBox* b = find_box(roomPtr->event_boxes, name);
            return b && b->contains(*playerPos);
        }
        // ops 0x139/0x13a/0x13b position source: like 0x13d, only the player (13) is
        // positioned in cutscene context; other ids -> no actor -> false.
        bool actor_pos(int entityId, float out[3]) const override {
            if (entityId != 13 || !playerPos) return false;
            out[0] = playerPos->x; out[1] = playerPos->y; out[2] = playerPos->z;
            return true;
        }
        // op 0xc1 IsFix(): the ScriptManager's ref-counted Fix/world-freeze state IS the
        // engine's 0x75ad90 fix counter (there is no separate pause counter here yet).
        bool is_fix() const override { return mgr && mgr->world_frozen(); }
    } csHost;
    // The cutscene runtime is the concurrent GoScript manager (CUTSCENE_COROUTINE.md §2/§5):
    // a triggered script can StartScript sub-talks / captions that tick alongside it. Its
    // loader resolves child names from the CURRENT room's script dir (&room.dir stays valid
    // across transitions — `room` is reassigned in place). Flags are copied in at start and
    // back out at end, so a cutscene reads/writes the live game state.
    struct ViewLoader : ScriptLoader {
        const fs::path* dir = nullptr;
        fs::path common;                   // MAP/S_COMMON
        std::shared_ptr<const Xso> load_script(const std::string& name) override {
            // FUN_00434970: an '@' label is data\map\s_common\<name>; anything else is the
            // current room's script.
            const bool at = !name.empty() && name[0] == '@';
            std::string up = at ? name.substr(1) : name;
            for (char& c : up) c = char(std::toupper((unsigned char)c));
            const fs::path p = (at ? common : *dir) / (up + ".XSO");
            std::error_code ec;
            if (!fs::exists(p, ec)) return nullptr;    // engine would throw; we skip
            try { return std::make_shared<Xso>(Xso::parse(read_bytes(p))); }
            catch (const std::exception&) { return nullptr; }
        }
    } csLoader;
    csLoader.dir = &room.dir;
    csLoader.common = asset_root / "MAP" / "S_COMMON";
    ScriptManager csMgr;
    csMgr.vm.host = &csHost;
    csMgr.loader = &csLoader;
    csHost.mgr = &csMgr;
    csHost.warp_on = warp_on;
    csHost.manual_on = manual_on;
    csHost.topic_seen = topic_seen;
    csHost.topic_kills = topic_kills;
    csHost.meshes = &meshes;
    csHost.player_floor = &player_floor_obj;
    csHost.worldHost = &world;
    csHost.roomPtr = &room;         // op 0x13d tests the player vs the current room's .SEV boxes
    csHost.playerPos = &feet;
    csHost.playerPosW = &feet;      // Chr_Pos/Chr_MoveTo move the player's feet in a cutscene
    bool prev_frozen = false;
    bool skip_cutscene = false;   // set by ESC/Backspace during a frozen cutscene
    std::function<void()> after_cutscene;   // ran once when the world thaws (e.g. re-gate the room)
    SpriteTex captionTex; std::string captionLoaded;   // cached area-name card (op 0xe2)
    std::unordered_map<std::string, SpriteTex> bustTex;   // cached bust portraits (op 0xe3), by name

    // (Re)start the room's event scripts: run the room root S_xxxx.XSO on the PERSISTENT
    // manager. The root StartScripts the region-talk controllers (Stop*_Roi_*), the save
    // point, ambient effects, etc., which then poll each frame (op 0x13d) and fire talks when
    // the player enters a .SEV box. Seeded from the live game flags; csMgr owns its flag copy
    // for the room's lifetime. Spawn/placement ops no-op on csHost (the world is populated
    // separately by spawn_enemies), so this only drives flags + child scripts + camera/box.
    // OPT-IN (CLERIA_EVENTS): running the full room root live needs a correct STORY-FLAG state,
    // which CleriaCore doesn't model yet. With fresh/zero flags the root misroutes — e.g. S_1000
    // fires BattleStart1, which Fixes and locks the hub (cleria-cotest test 11 pins this). So it
    // is off by default (the debug-T talk + barrier break-pan still work, they're started
    // explicitly); set CLERIA_EVENTS=1 to run live region-talk triggers experimentally.
    // Room-root event scripts run by default, as in the game (PLAN.md Current focus 1):
    // the story beats, scripted warps, step plates and boss rooms all live in them.
    // CLERIA_EVENTS=0 turns them off (a debugging aid only).
    const char* ev = std::getenv("CLERIA_EVENTS");
    const bool events_enabled = !(ev && std::atoi(ev) == 0);
    // A fresh game needs no story-flag seed: with the FUN_0041fd10 new-game state the S_1000
    // root routes Yunica / Hugo / Toal to BattleStart1/2/3, the first-visit intro (audit
    // event_vm #36/#40; flag 141 only guards one EventXX call).
    auto start_room_events = [&] {
        csMgr.live.clear(); csMgr.spawn_queue.clear(); csMgr.peak_live = 0; csMgr.fix_counts[0] = csMgr.fix_counts[1] = 0;
        csMgr.paused = false;
        csHost.heal_spots.clear();
        csHost.box_active = false; csHost.reveal = 0; csHost.advance = false;
        camera_return();                            // no forced camera carries across rooms
        after_cutscene = nullptr; prev_frozen = false;
        if (!events_enabled) return;                 // safe default: no live room root
        std::copy(game.g_flags, game.g_flags + 0x200, csMgr.vm.g_flags);
        csMgr.vm.g_flags[150] = character;
        const fs::path xso = room.dir / (room.name + ".XSO");
        if (!fs::exists(xso)) return;
        try { csMgr.start_root(std::make_shared<Xso>(Xso::parse(read_bytes(xso))), room.name); }
        catch (const std::exception&) {}
        std::printf("  [events] room %s -> %zu live script(s)%s\n", room.name.c_str(),
                    csMgr.live.size(), csMgr.world_frozen() ? " (frozen)" : "");
    };

    // Start an EXTRA cutscene script (debug T, barrier break) alongside the room's live
    // scripts; its Fix (0xb6) freezes the world and it reaps itself on end. Does NOT clear the
    // persistent controllers. `debug235` sets the S_1000 test-route flag for the debug T key.
    auto start_cutscene_xso = [&](std::shared_ptr<Xso> s, const std::string& name, bool debug235) {
        // Seed the cutscene VM with the LIVE game flags: talk scripts branch on the
        // character (flag 150) and story flags, and their default paths are EMPTY —
        // with unseeded flags the script Fixes, matches nothing, Unfixes and ends in
        // one burst (no box, no freeze). start_room_events does the same copy.
        std::copy(game.g_flags, game.g_flags + 0x200, csMgr.vm.g_flags);
        if (debug235) csMgr.vm.g_flags[235] = 1;
        csMgr.start_root(std::move(s), name);
        std::printf("  [cutscene] started %s (live=%zu, frozen=%d)\n", name.c_str(),
                    csMgr.live.size(), int(csMgr.world_frozen()));
    };
    auto start_cutscene = [&](const fs::path& p, bool debug235) {
        std::shared_ptr<Xso> s;
        try { s = std::make_shared<Xso>(Xso::parse(read_bytes(p))); }
        catch (const std::exception&) { hud.message("cutscene: load failed"); return; }
        start_cutscene_xso(std::move(s), p.stem().string(), debug235);
    };

    // One fixed tick of the room's event scripts. Returns whether the world is FROZEN (a Fix
    // is held) — the caller skips the player/enemy sim for that tick but the camera still runs.
    auto tick_events = [&] {
        // Cutscene SKIP: drain every live script to completion this frame, collapsing
        // Wait_Time delays and auto-advancing dialogue prompts, until the world thaws
        // (all Fixes released) or a bounded budget is hit. Side effects (flags, spawns,
        // warps, Eft/Scroll) still run in order — only the waiting is removed. Bounded
        // so a script that legitimately never ends (a persistent controller) can't hang.
        if (skip_cutscene) {
            csHost.skipping = true;                // every wait predicate reports "done"
            for (int guard = 0; guard < 20000 && csMgr.world_frozen(); ++guard) {
                csMgr.skip_waits();                // zero every live script's Wait_Time
                csMgr.tick_all();
            }
            csHost.skipping = false;
            csHost.advance = false;
            csHost.fade_cur = csHost.fade_tgt;   // land the fade at its end state
            skip_cutscene = false;
            csHost.box_active = false;             // drop any half-revealed line
            scroll_reset(); shake(0, 0, 0, 0);     // clear the scripted look-at + shake
            camera_return();                       // drop any forced camera region
            after_cutscene = nullptr;              // its thaw-action is moot after a skip
            // Snap the region camera straight back onto the player this frame, so the
            // view doesn't stay parked where the cutscene had panned it.
            update_camera(room, feet, 0.0f, /*snap=*/true);
            if (g_cam.on) apply_cam_state(cam, feet);
            hud.message("(skipped)");
        }
        csMgr.tick_all();
        if (csHost.box_active && csHost.reveal < int(csHost.text.size())) csHost.reveal += 2;
        scroll_advance();
        shake_advance();
        csHost.fade_advance();
        // Advance cutscene walk-to moves (op 0x143): the player (feet) and NPCs (world).
        // Re-snap the player to the floor after the XZ step so a walk-to follows terrain.
        csHost.advance_player_move();
        world.advance_cutscene_moves();
        // Keep the player on the floor for the WHOLE cutscene, not just mid-step: Chr_Pos
        // teleports her to authored Y (sometimes below the floor) and she never falls while
        // the sim is frozen, so snap every frozen frame.
        if (csMgr.world_frozen() || csHost.p_moving) {
            const float fl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + room_diag(room));
            if (fl > kNoFloor) feet.y = fl;
        }
        // Cutscene NPCs spawn at authored heights (Shion at y~2) and never fall — keep
        // non-player, non-enemy actors on the floor at ALL times (they persist after the
        // cutscene too, so gating on 'frozen' left them hovering during normal play). If no
        // collision floor is under them, fall back to the player's floor level (same room).
        for (Actor& a : world.actors()) {
            if (a.is_player() || a.is_enemy()) continue;
            const float fl = floor_at(phys_mesh(room), a.pos.x, a.pos.z, a.pos.y + room_diag(room));
            a.pos.y = (fl > kNoFloor) ? fl : feet.y;
        }
        const bool frozen = csMgr.world_frozen();
        if (!frozen && prev_frozen && after_cutscene) {    // world just thawed -> queued action
            auto f = after_cutscene; after_cutscene = nullptr; f();
        }
        prev_frozen = frozen;
        if (frozen) {
            // The SCM region camera KEEPS UPDATING during a cutscene — the event VM
            // doesn't replace it, it writes a scroll look-at the normal updater folds
            // in (CUTSCENE_CAMERA.md §0). Refresh g_cam from the region every frozen
            // frame so the scripted look-at (often at floor y=0) orbits the correct
            // pitch/dist; skipping this left a stale/zero g_cam and put the eye in the
            // floor on the first-frame room-entry cutscene.
            update_camera(room, feet, kTickDt);
            if (g_cam.on) apply_cam_state(cam, feet);
        }
        return frozen;
    };

    // Melee swing. Damage and reach are OUR numbers: the game has no per-enemy HP
    // column (stats come from a level curve we have not wired), and the player's
    // attack animation tags in c_111 are not identified yet.
    // Start a swing. The hit-volume is armed for `atk_window` ticks and processed in the
    // fixed-step loop (see the swing block there). This is the engine's shape now, from
    // build/attack_re/ATTACK.md, not the old instant reach-2.6 circle: a SPHERE of radius
    // 0.6 sitting 0.6 units AHEAD of the player in the facing direction (arg[2] is both
    // the radius and the forward reach; the "+0x10c angle" is a placement offset, 0 for a
    // straight swing, NOT a sector arc), live for a 30-tick window, dealing `power` 8.0 as
    // the damage bonus, and hitting each enemy ONCE per swing (the engine's hit-dedup).
    // The numbers are Yunica's basic swing, verified in cleria-asotest against C_010.
    // Start or chain a swing. The ASO owns everything about it — its length, the
    // hit-volume (GenerateCollision arms it), and when you may act again — which is what
    // makes the three characters feel different instead of sharing one hardcoded window.
    // Measured from the scripts: Yunica's combo #1 roots her (Movable 0) and opens its
    // cancel window at tick 21, Hugo's at tick 9 while he keeps full movement, Toal's at
    // tick 10 with an 8-frame box and a slow drift. See build/attack_re/ATTACK.md.
    // The combo order is DATA, not a walk over the section ids. The engine reads
    //     section = comboTable[character][actor+0x6f0]
    // at 0x0046ef96..0x0046efb0, where the table pointer comes from 0x00732664[character]
    // and the chain length from the byte array 0x00732678[character] = {6,4,2,5}.
    // Decoded in build/combat_re/COMBO_CHAIN.md; `character` here is the same 1/2/3
    // Yunica/Hugo/Toal id the engine keeps in DAT_0076bb74. Index 0 is the unlockable
    // fourth form (its script is not one of C_010/020/030) — carried for completeness.
    // This replaces the old probe-and-upper_bound guess, which got both the ORDER
    // (0x100->0x101 instead of 0x100->0x103) and the LENGTH wrong, and which stalled
    // Hugo because his 0x101/0x102/0x104/0x105 are empty 4-dword stubs.
    static constexpr std::int32_t kComboTable[4][7] = {
        {0x100, 0x103, 0x104, 0x101, 0x102, 0x105, 0x109},   // 0: unlockable form
        {0x100, 0x103, 0x104, 0x101, 0x106, 0x107, 0x108},   // 1: Yunica
        {0x100, 0x103,     0,     0,     0,     0,     0},   // 2: Hugo — two shots
        {0x100, 0x103, 0x104, 0x101, 0x102,     0,     0},   // 3: Toal
    };
    static constexpr int kComboLen[4] = {6, 4, 2, 5};
    auto try_attack = [&] {
        const bool swinging = avm.section() >= 0x100;
        if (swinging && !avm.cancelable()) return;   // committed to the current swing
        const int chr = (character >= 0 && character < 4) ? character : 1;
        const int len = kComboLen[chr];
        if (combo_stage >= len) combo_stage = 0;
        const std::int32_t next = kComboTable[chr][combo_stage];
        if (!avm.set_section(next, /*force*/true)) return;
        // The stage advances AFTER the section is played, and WRAPS rather than stalling
        // (0x0046f351: ++stage; 0x0046f5a5: if (stage >= len) stage = 0) — holding the
        // button cycles the chain forever, which is what the game does.
        combo_stage = (combo_stage + 1 >= len) ? 0 : combo_stage + 1;
        combo_idle = 0;
        atk_dir = face_dir;         // the swing points where you're facing
        player_hit_serial = next_hit_serial(world.hit_serial_counter_);
    };

    // Spawn every bullet an ASO section fired this tick. Shared by the player and by
    // enemies: previously only the player drained bullets(), so no enemy could ever
    // shoot even when its script fired one. `origin` is the shooter's feet, `fdir` its
    // facing; the heading offset arg is applied on top of that.
    auto spawn_bullets = [&](const AsoVm& vm, const Vec3& origin, const Vec3& fdir,
                             bool from_player) {
        for (const AsoVm::BulletEvent& b : vm.bullets()) {
            Projectile p;
            p.effect_id = b.id;
            const float reach = b.f[0], voff = b.f[1], head_deg = b.f[2];
            const Vec3 d{b.f[3], b.f[4], b.f[5]};
            const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (dl > 1e-6f) {
                p.dir = {d.x / dl, d.y / dl, d.z / dl};
            } else {
                // Spawn geometry is the engine's x=sin / z=cos, same as GenerateCollision.
                const float yaw = std::atan2(fdir.x, fdir.z) +
                                  head_deg * 3.14159265358979f / 180.0f;
                p.dir = {std::sin(yaw), 0, std::cos(yaw)};
            }
            p.speed = b.f[6];
            p.accel = b.f[7];
            p.gravity = b.f[8];
            // The emitter's OWN scale is the world size. arg[12] (=0.15 for Hugo) was
            // read as a scale multiplier, but applying it shrank the shot to a
            // near-invisible sliver, so its role is not that (it may be an anim rate).
            // Default to the emitter scale as-authored; CLERIA_BULLET_SCALE tunes.
            p.scale = 1.0f;
            p.pos = {origin.x + p.dir.x * reach, origin.y + kCharH * 0.5f + voff,
                     origin.z + p.dir.z * reach};
            p.life = bullet_art(g, asset_root, efl, eft, p.effect_id).life;
            // Speed 0 means a stationary attached effect rather than a projectile —
            // that is all of Toal's 8 calls (smoke), so they must not become bullets.
            // How the script's scale arg combines with the emitter's own scale is
            // NOT pinned down by the RE, so it is left tunable rather than silently
            // fudged: CLERIA_BULLET_SCALE multiplies the result.
            if (const char* bs = std::getenv("CLERIA_BULLET_SCALE"))
                p.scale *= float(std::atof(bs));
            if (std::getenv("CLERIA_BULLET")) {
                const BulletArt& art = bullet_art(g, asset_root, efl, eft, p.effect_id);
                std::printf("  [bullet] effect %d (%s) %zu layer(s) life=%d speed=%.3f "
                            "scale=%.3f dir=(%.2f,%.2f,%.2f)\n",
                            p.effect_id, efl.name(p.effect_id).c_str(), art.layers.size(),
                            p.life, p.speed, p.scale, p.dir.x, p.dir.y, p.dir.z);
                for (const BulletArt::Layer& L : art.layers)
                    std::printf("           layer %ux%u  %zu frames  world size %.3f x %.3f\n",
                                L.tex.w, L.tex.h, L.frames.size(), L.w * p.scale,
                                L.h * p.scale);
            }
            p.from_player = from_player;
            if (p.speed > 1e-4f) shots.push_back(std::move(p));
        }
            };

    // FUN_0046cc10(amount): the EX/Boost gauge add. Refused while boosting or full, and
    // ONLY counts while g_flags[187] is set (0x76bc08 — set by the floor-1 story scripts,
    // e.g. MAP/S_01/S_0100/S1004.XSO), so a fresh game cannot charge Boost before that beat.
    // Call-site amounts (audit combat.md #22): +300 per hit dealt (FUN_004fbb40 0x4fd0bf),
    // +dmg*100 per hit taken (0x4fded0). [not yet modelled] the passive per-tick fill in the
    // mover (0x467d8c: (1-hp/max)*word[0x68b070]+1) — the old per-hit formula here was that
    // passive term misplaced.
    auto add_boost = [&](float amount) {
        if (player.boosting || player.boost >= 30000.0f || game.g_flags[187] == 0) return;
        player.boost = std::min(30000.0f, player.boost + amount);
    };

    // ---- Boost (build/ability_re/BOOST.md, ABILITY.md §4b) -------------------------------
    // Entering needs a FULL gauge — the engine compares == 30000.0 exactly — and grants 16
    // i-frames. A SECOND press spends the whole gauge on the character's EXTRA/super section.
    // The RE is explicit that Boost carries NO damage or speed multiplier: its offence is the
    // super section's own hitboxes. The drain (~30/tick) lives in the fixed-step block.
    auto try_boost = [&] {
        if (!player.boosting) {
            if (player.boost < 30000.0f) return;          // UCOMISS against the full value
            player.boosting = true;
            game.g_flags[141] = 1;                        // FUN_00463f40: Boost active
            player_iframes = std::max(player_iframes, 16);
            hud.message("Boost!");
            return;
        }
        // Second press: the EXTRA section. These live outside the 0x100..0x10f combo range,
        // so they are requested directly; they carry their own hitboxes, which the existing
        // armed_hitbox() path already resolves.
        const int extra = character == 1 ? 313 : character == 2 ? 300 : 317;
        if (!avm.set_section(extra, /*force*/true)) return;
        atk_dir = face_dir;
        player_hit_serial = next_hit_serial(world.hit_serial_counter_);
        player.boost = 0.0f;                              // spends the gauge
        player_iframes = std::max(player_iframes, 16);
    };

    // ---- Skills / magic (ABILITY.md §2a, BOOST.md §4, GAUGES_UI.md) ----------------------
    // A skill is NOT a special animation: it is a different ASO section, gated by SP. The
    // per-character SP costs are fully dumped; the section table is only partly dumped (one
    // row of PTR_DAT_0068afd0), so the per-character rows are [PARTIAL] — see the note below.
    auto try_skill = [&] {
        const int slot = player.magic == 0x74 ? 0 : player.magic == 0x75 ? 1
                       : player.magic == 0x76 ? 2 : 3;    // FUN_0046d560
        if (slot == 3) return;                            // nothing equipped
        static const short kSkillCost[4][3] = {{1000, 600, 300},   // char 0
                                               {700, 700, 400},    // Yunica
                                               {2000, 600, 900},   // Hugo
                                               {300, 700, 600}};   // Toal
        // [PARTIAL] only PTR_DAT_0068afd0's single dumped row {0x110,0x10f,0x111} is known;
        // the per-character rows of PTR_DAT_0068afe0 were never dumped. Hugo in particular is
        // documented to use 0x112 / 300-304. We request the known row and fall back to any
        // section the character's own ASO actually defines, rather than inventing ids.
        static const short kSkillSec[3] = {0x110, 0x10f, 0x111};
        const float cost = float(kSkillCost[character & 3][slot]);
        if (player.sp_cur - cost < 0.0f) return;          // threshold is 0.0
        if (avm.section() >= 0x100 && !avm.cancelable()) return;
        if (!avm.set_section(kSkillSec[slot], /*force*/true)) return;
        player.sp_cur -= cost;                            // 0x46f2eb
        atk_dir = face_dir;
        player_hit_serial = next_hit_serial(world.hit_serial_counter_);
    };

    const ItemTable items_tbl = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
    csHost.item_name_fn = [&](int id) { return items_tbl.name(id); };  // 0xd5 "Acquired X" box
    if (const Cast* pc13 = casts.get(13)) csHost.player_speed30c = pc13->speed_base;
    csHost.movable_fn = [&] { return avm.movable(); };
    // The default speaker name: the speaker's actor cast (+0x124); the player id 0xd remaps
    // by character to 0xa6/0xa7/0xa8 (audit event_vm #26).
    csHost.speaker_name_fn = [&](int sp) -> std::string {
        int cast = -1;
        if (sp == 13 || sp == 999) cast = character == 2 ? 0xa7 : character == 3 ? 0xa8 : 0xa6;
        else if (auto it = csHost.spawn_cast.find(sp); it != csHost.spawn_cast.end()) cast = it->second;
        else if (sp > 0 && sp <= 0xa5) cast = sp;
        const Cast* c = cast >= 0 ? casts.get(cast) : nullptr;
        return c ? c->display : std::string();
    };
    auto try_interact = [&] {
        const Vec3 p = fly ? cam.pos : Vec3{feet.x, feet.y, feet.z};
        // Interact reach: you must stand next to a statue/chest. Uncapped the room diagonal
        // gave ~40m; 1.5u was too tight to reach a statue behind its pedestal. 2.5u lets you
        // interact from the front without being a room-wide grab.
        const float reach = std::min(room_diag(room) * 0.09f, 2.5f);
        const Interactable* target = nullptr;
        float best = reach * reach;
        for (const Interactable& it : room.mesh.interactables) {
            const float dx = it.position.x - p.x, dy = it.position.y - p.y, dz = it.position.z - p.z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best) { best = d2; target = &it; }
        }
        if (!target) { std::printf("  (nothing to interact with here)\n"); return; }
        // Goddess statue -> the SAVE dialog (the real game's save flow; the pause
        // book's Save/Load page only LOADS).
        std::string tl = target->name;
        for (char& c : tl) c = char(std::tolower((unsigned char)c));
        if (tl.rfind("save", 0) == 0) {
            // A SEALED goddess statue (flag 300 == 0) must be CLEANSED first for Yunica/Hugo
            // (Toal skips it). Play SAVE.XSO through the live cutscene manager — it self-gates
            // on its prereqs (intro-done flag 165 + story flags), Fixes the world, spawns the
            // goddess, sets flag 300 = 1, and Unfixes. When it ends we re-gate the room so the
            // statue swaps to its cleansed model; if it didn't cleanse (prereqs unmet) we fall
            // back to the save menu. Events-off / cleansed / Toal go straight to the menu.
            const bool sealed = game.g_flags[300] == 0;
            std::printf("  [statue] flag300(cleansed)=%d 165(intro)=%d 84=%d 86=%d events=%d char=%d\n",
                        game.g_flags[300], game.g_flags[165], game.g_flags[84], game.g_flags[86],
                        int(events_enabled), character);
            if (events_enabled && sealed && (character == 1 || character == 2) &&
                fs::exists(room.dir / "SAVE.XSO")) {
                std::copy(game.g_flags, game.g_flags + 0x200, csMgr.vm.g_flags);
                csMgr.vm.g_flags[150] = character;
                if (csMgr.start("save")) {
                    cleanse_pending = true;
                    std::printf("  [statue] started cleanse (SAVE.XSO); prereqs %s\n",
                                (game.g_flags[165] && (game.g_flags[84] || game.g_flags[86]))
                                    ? "MET -> will cleanse" : "UNMET -> will fall back to menu");
                    return;
                }
            }
            statue_ui = 1;            // "What will you do?" (STATUE_MENU.md flow)
            statue_sel = 0; statue_reveal = 0;
            return;
        }
        if (target->script.empty()) { std::printf("  %s: no script\n", target->name.c_str()); return; }
        try {
            Xso x = Xso::parse(read_bytes(target->script));
            const bool preOpened = target->opened_flag >= 0 &&
                                   game.g_flags[target->opened_flag] != 0;
            game.reset_run();
            game.run(x);
            // Prefer the game's acquire-box items (0xD5); fall back to give-item.
            const std::vector<int>& got = !game.acquires.empty() ? game.acquires : game.items;
            const bool nowOpened = target->opened_flag >= 0 &&
                                   game.g_flags[target->opened_flag] != 0;
            if (got.empty() && !preOpened && nowOpened) {
                // The interaction FIRED an event that set its own flag but grants no
                // inventory item — e.g. the S_1004 Flabellum altar (TALKITEM), which
                // sets the SKILL flags 116/107 and its obtained-flag 304. That is a
                // success, not "locked": re-gate so the item glow / state updates.
                hud.message("Obtained a new power!");
                std::printf("  >> event fired: %s set flag %d\n",
                            target->name.c_str(), target->opened_flag);
                reload_room();
            } else if (got.empty()) {
                // No item can mean two different things, and they are NOT the same:
                // the chest's opened-flag is set (truly already looted), or the script
                // hit a later prerequisite guard and ended early. S_1001's box_01, for
                // instance, tests a second flag (301) that some event must set first —
                // reporting that as "already opened" is simply wrong.
                const bool opened = nowOpened;
                hud.message(std::string(target->name) +
                            (opened ? ": already opened" : ": locked (needs an event first)"));
                std::printf("  %s: no item — opened_flag %d = %d -> %s\n",
                            target->name.c_str(), target->opened_flag,
                            target->opened_flag >= 0 ? game.g_flags[target->opened_flag] : -1,
                            opened ? "already opened" : "gated by a later guard");
            } else {
                std::string msg = "Acquired";
                for (int it : got) { msg += " "; msg += items_tbl.name(it); }
                hud.message(msg);
                std::printf("  >> %s\n", msg.c_str());
                // The chest script set its opened-flag on `game`; re-gate the room so the
                // just-looted chest re-poses to its OPEN clip immediately (build_room reads
                // the flags). Enemies live in `world`, which reload_room doesn't touch.
                reload_room();
            }
        } catch (const std::exception& e) {
            std::printf("  %s: script error: %s\n", target->name.c_str(), e.what());
        }
    };

    std::printf("\nPLAY: WASD move Yunica (8-way, fixed camera), Space jump, E interact, Tab character | F free-fly debug | wheel speed | R respawn | 1-9 exit | ESC quit\n");
    bool running = true, looking = false;
    Uint64 last = SDL_GetTicks();
    // Fixed-step logic. The game runs its logic at a FIXED 60 Hz with catch-up,
    // decoupled from rendering: it accumulates REAL elapsed milliseconds scaled by
    // a timeScale, then runs 0..N ticks per Present. On a 144 Hz display the ASO VM
    // still ticks 60x/sec. `anime_Wait(n)` is therefore n/60 s of SCALED time — so
    // the accumulator is the thing to reproduce, not a per-frame dt scale.
    float accum = 0;

    // In-game pause menu (Escape opens it, Escape/B closes; the window X still quits). Draws
    // the pause-book Equip page over a frozen game. The menu renderer + font + item table are
    // loaded here since the walk loop doesn't share the title-screen instances.
    TitleFx pauseUi;
    const bool pauseOk = pauseUi.init(g, asset_root);
    // Dialogue PORTRAIT (CUTSCENE_DIALOGUE.md addendum): face = AIA frame of an ani
    // slot. Slot from the speaker class; frame = the Window_Msg voice param. Decoded
    // with the pixel-exact AIA decoder, composed at its canvas position, cached until
    // the slot/frame changes (the engine re-uploads exactly the same way, +0x264 gate).
    struct PortraitCache {
        int cast = -1, frame = -1;   // frame -2 = decode failed (don't retry per frame)
        Aia aia; bool aia_ok = false;
        SpriteTex tex{};
    } portrait;
    auto portrait_cast = [&](int sp) -> int {
        // Speaker -> face SET (empirical + CUTSCENE_DIALOGUE.md addendum): a speaker id
        // that the cutscene itself spawned (TALKC210: Roy = entity 87, cast 210) uses
        // its cast's face file F_<cast>.AIA (8 single-frame 256x256 expressions; the
        // engine routes this through runtime ani slots via actor+0x124). Player ids
        // have no F_ file — faceless for now [H: player-bust source not located].
        auto it = csHost.spawn_cast.find(sp);
        if (it != csHost.spawn_cast.end()) return it->second;
        // No spawn recorded: the doc's rule — an id <= 0xa5 IS the class/cast index
        // (S_1000 spawns entities with id == cast; TALKC210's 87 = CAST_C210 = Roy).
        return (sp > 0 && sp <= 0xa5) ? sp : -1;
    };
    auto portrait_tex = [&]() -> const SpriteTex* {
        const int cast = portrait_cast(csHost.speaker);
        int frame = csHost.voice;
        if (cast < 0 || frame < 0) return nullptr;
        if (portrait.cast != cast) {
            portrait.cast = cast; portrait.frame = -1; portrait.aia_ok = false;
            // cast INDEX -> CASTINFO name "CAST_C210" -> face set "F_210.AIA".
            // (NPC casts carry no sprite stem — the name is the reliable key.)
            std::string face;
            if (csHost.speaker == 13 || csHost.speaker == 999) {
                // The PLAYER's cutscene face (CUTSCENE_POLISH_RE.md §1): the engine remaps the
                // player class by character to CAST_M800/M810/M820 (Yunica/Hugo/Toal), whose
                // faces are F_800/F_810/F_820. CAST_ADOL has no F_ file, so this was blank —
                // that's the "introduce themselves" self-lines (e.g. Yunica's intro).
                face = (character == 2) ? "F_810" : (character == 3) ? "F_820" : "F_800";
            } else if (const Cast* cr = casts.get(cast)) {
                // The face set is F_<trailing digits of the cast name>: CAST_C210 -> F_210,
                // CAST_M840 (Shion) -> F_840, CAST_M800 -> F_800. The old rfind('C') produced
                // the bogus "F_AST_M840" for M-prefixed casts (no second 'C').
                const std::string& n = cr->name;
                std::size_t d = n.size();
                while (d > 0 && n[d - 1] >= '0' && n[d - 1] <= '9') --d;
                if (d < n.size()) face = "F_" + n.substr(d);     // "F_840"
            }
            if (!face.empty()) {
                portrait.aia = Aia{};
                portrait.aia_ok = portrait.aia.load(asset_root / "ANI" / (face + ".AIA")) &&
                                  portrait.aia.ok();
            }
            std::printf("  [portrait] speaker=%d cast=%d face='%s' aia=%d\n",
                        csHost.speaker, cast, face.c_str(), int(portrait.aia_ok));
        }
        // Clamp the expression to the sheet — an out-of-range voice shows the last
        // expression instead of a blank (CUTSCENE_POLISH_RE.md §1).
        if (portrait.aia_ok && portrait.aia.nImg > 0)
            frame = std::min(frame, int(portrait.aia.nImg) - 1);
        if (portrait.aia_ok && portrait.frame != frame) {
            AiaFrame f;
            if (portrait.aia.decode_frame(std::uint32_t(frame), 0, 0, f) && f.w > 0) {
                // Compose onto the full canvas so the face sits where it was authored
                // (the engine textures the whole record; a tight bbox would drift).
                const std::uint32_t cw = portrait.aia.canvasW, chh = portrait.aia.canvasH;
                std::vector<std::uint8_t> canvas(std::size_t(cw) * chh * 4, 0);
                for (std::uint32_t y = 0; y < f.h; ++y) {
                    const std::uint32_t dy = y + f.y0;
                    if (dy >= chh) break;
                    for (std::uint32_t x = 0; x < f.w; ++x) {
                        const std::uint32_t dx = x + f.x0;
                        if (dx >= cw) break;
                        std::memcpy(&canvas[(std::size_t(dy) * cw + dx) * 4],
                                    &f.rgba[(std::size_t(y) * f.w + x) * 4], 4);
                    }
                }
                portrait.tex = upload_rgba(g, cw, chh, canvas.data());
                portrait.frame = frame;
            } else portrait.frame = -2;
        }
        return (portrait.frame == frame && portrait.tex.tex) ? &portrait.tex : nullptr;
    };
    MenuFont pmf;
    pmf.init(g, fmt, find_felghana(asset_root), int(std::lround(32.0 * 960.0 / 768.0)));
    const ItemTable pitems = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
    bool paused = false;
    // The pause book (src/cleria/pausebook, build/menu_re/PAUSEBOOK_DRAW.md), drawn by
    // view_book.inl. It steps on the 60 Hz tick; Confirm / Cancel are key edges, the
    // directions come from the held keys with the engine's auto-repeat (§0.7).
    PauseBook book;
    BookRenderer bookR;
    const bool bookOk = bookR.init(g, fmt, asset_root, pitems);
    BookInput bookIn;
    int bookHold[4] = {};
    float bookAccum = 0;
    int bookSaveNo = 0;                                    // DAT_0074ac3e (ini SaveFileNo) [session only]
    auto book_model = [&]() {
        const PlayerStats st = player_stats();             // also refreshes inv.raval
        BookModel m;
        m.flags = game.g_flags; m.inv = &inv; m.items = &inv_items; m.gi = &ginfo;
        m.character = character; m.level = player.level;
        m.hp = float(player.hp); m.max_hp = st.max_hp; m.atk = st.atk; m.def = st.def;
        m.exp = player.exp;
        m.next_exp = ginfo.ok() ? float(ginfo.exp_to_level(player.level + 1)) : 0.0f;
        m.preview = [&](int id) {
            return book_preview(game.g_flags, inv, ginfo, inv_items, character, player.level, id);
        };
        m.player_can_act = player.hp > 0;                  // [STAND-IN] grounded / not hurt
        m.scan = [&](int slot) { return scan_slot(slot_path(slot)); };
        m.save_file_no = &bookSaveNo;
        m.sliders = oslider;
        m.checks = ocheck;
        m.topic_seen = topic_seen;
        m.topic_kills = topic_kills;
        m.manual_on = manual_on;
        m.casts = &casts;
        m.enemies = &etab;
        m.warp_on = warp_on;
        m.start_script = [&](const std::string& name) {   // FUN_00434970: start + first burst
            std::copy(game.g_flags, game.g_flags + 0x200, csMgr.vm.g_flags);
            csMgr.vm.g_flags[150] = character;
            if (!csMgr.start(name)) std::printf("  [book] %s did not start\n", name.c_str());
            std::copy(csMgr.vm.g_flags, csMgr.vm.g_flags + 0x200, game.g_flags);
        };
        return m;
    };
    std::fprintf(stderr, "[pause menu] Esc opens the pause book; assets %s\n",
                 pauseOk ? "loaded (ready)" : "MISSING TITLE_000.DDS -> book won't draw");

    // A title-screen Load lands here: flags/character/room were applied before the room
    // load above; apply_loaded now sets the stats and the saved position (idempotent on
    // the flags), completing the engine's init-then-overwrite load (SAVELOAD §4.2).
    if (pending_load) {
        apply_loaded(*pending_load);
        pending_load.reset();
        hud.message("Game loaded.");
    }

    // Establish the region camera for the INITIAL room before the first tick — a
    // room-entry cutscene can freeze on frame 1, and apply_cam_state needs a valid
    // g_cam (pitch/dist) or it puts the eye at the scroll look-at (in the floor).
    if (!fly) { update_camera(room, feet, 0.0f, /*snap=*/true); if (g_cam.on) apply_cam_state(cam, feet); }

    // A scene path ("s_10\s_1009\", "demo3\") -> the room to enter. A demo scene is a movie
    // that then warps to a fixed next scene (SCENEFLOW.md 5.1); movies are [not modelled], so
    // the demo is passed through at once. "" = no room (the title, an unknown name).
    auto scene_room = [&](std::string s) -> std::string {
        for (char& c : s) c = (c == '/') ? '\\' : char(std::tolower((unsigned char)c));
        while (!s.empty() && s.back() == '\\') s.pop_back();
        if (s.rfind("demo", 0) == 0 && s.size() == 5) {
            const bool toal = character == 3;
            switch (s[4]) {
                case '1': return toal ? "S_1000" : "S_0002";
                case '2': return toal ? "S_7001" : "S_0002";
                case '3': return "S_1000";
                case '4': return "S_6080";
                case '5': return "S_7002";
                case '7': case '8': return "S_0003";
                default: return "";                              // demo6 -> the title
            }
        }
        const std::size_t k = s.find_last_of('\\');
        std::string room_name = k == std::string::npos ? s : s.substr(k + 1);
        for (char& c : room_name) c = char(std::toupper((unsigned char)c));
        return g.sob_index.count(room_name) ? room_name : std::string();
    };

    while (running) {
        if (!csHost.pending_scene.empty()) {
            const std::string req = csHost.pending_scene;
            csHost.pending_scene.clear();
            csMgr.paused = false;
            const std::string dest = scene_room(req);
            std::printf("  [scene] ChangeScene '%s' -> %s\n", req.c_str(), dest.empty() ? "(none)" : dest.c_str());
            if (!dest.empty()) {
                transition(dest);                                // SCT default arrival, no label
                // Every arrival fades in from black over 20 steps (SCENEFLOW.md 1.5).
                csHost.fade_rgb = 0; csHost.fade_cur = 1.0f; csHost.fade_tgt = 0.0f; csHost.fade_rate = 0.05f;
            }
        }
        const Uint64 now = SDL_GetTicks();
        const float real_dt = float(now - last) / 1000.0f; last = now;
        accum += real_dt * g_time_scale;
        // The exe's own hitch protection (DAT_0075aed5): after a load it forces a
        // single tick with a zero delta rather than catching up on the stall. Room
        // transitions set this; without it a slow load bursts hundreds of ticks.
        if (g_tick_reset) { accum = kTickDt; g_tick_reset = false; }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode k = e.key.key;
                // The goddess-statue menu swallows all input while open (real flow,
                // STATUE_MENU.md: choice -> save slots / blessings; Esc backs out).
                if (statue_ui == 1) {              // "What will you do?"
                    if (k == SDLK_UP || k == SDLK_W) statue_sel = (statue_sel + 2) % 3;
                    else if (k == SDLK_DOWN || k == SDLK_S) statue_sel = (statue_sel + 1) % 3;
                    else if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_E) {
                        if (statue_sel == 0) {
                            statue_ui = 0;
                            paused = true;
                            book.open(book_model(), 1);
                            bookIn = {};
                            for (int& h : bookHold) h = 0;
                            bookAccum = 0;
                        }
                        else if (statue_sel == 1) { statue_ui = 3; statue_sel = 0; statue_reveal = 0; }
                        else statue_ui = 0;        // Nothing right now
                    } else if (k == SDLK_ESCAPE) statue_ui = 0;
                } else if (statue_ui == 3) {       // blessing list (effects pending)
                    constexpr int kBless = 15;     // 14 blessings + "Nothing right now"
                    if (k == SDLK_UP || k == SDLK_W) statue_sel = (statue_sel + kBless - 1) % kBless;
                    else if (k == SDLK_DOWN || k == SDLK_S) statue_sel = (statue_sel + 1) % kBless;
                    else if (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_E) {
                        if (statue_sel == kBless - 1) { statue_ui = 1; statue_sel = 1; }
                        else hud.message("Blessings are not implemented yet.");
                    } else if (k == SDLK_ESCAPE) { statue_ui = 1; statue_sel = 1; }
                }
                // While the world is frozen by a cutscene, a confirm key advances the dialogue
                // and other gameplay input is swallowed (the sim is skipped anyway).
                // ESC (or the dedicated skip key) SKIPS the whole cutscene — like retail's
                // cancel-to-skip: tick_events drains it to the end, applying every flag /
                // spawn / warp in order but collapsing the Wait_Time delays.
                else if (csMgr.world_frozen()) {
                    if (k == SDLK_SPACE || k == SDLK_RETURN || k == SDLK_E || k == SDLK_Z)
                        csHost.advance = true;
                    else if (k == SDLK_ESCAPE || k == SDLK_BACKSPACE)
                        skip_cutscene = true;
                }
                else if (paused) {
                    const bool ok = k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_E || k == SDLK_Z;
                    const bool esc = k == SDLK_ESCAPE || k == SDLK_BACKSPACE;
                    if (ok) bookIn.confirm = true;
                    if (esc) bookIn.cancel = true;
                }
                // T = trigger a test cutscene (S_1000's Roy/Ramona talk) — debug.
                else if (k == SDLK_T) start_cutscene(asset_root / "MAP" / "S_10" / "S_1000" / "TALKC210.XSO", /*debug235*/true);
                else if (k == SDLK_ESCAPE) {         // open the book; NEVER quit (X quits)
                    paused = true;
                    book.open(book_model());
                    bookIn = {};
                    for (int& h : bookHold) h = 0;
                    bookAccum = 0;
                }
                else if (k == SDLK_F) {
                    fly = !fly;
                    if (!fly) {  // dropping into walk: keep XZ, snap to floor
                        feet = {cam.pos.x, cam.pos.y, cam.pos.z};
                        const float fl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + room_diag(room));
                        if (fl > kNoFloor) feet.y = fl;
                        pv = VerticalState{}; pvel = {}; last_floor = feet.y;
                    }
                    std::printf("mode: %s\n", fly ? "fly" : "walk");
                }
                else if (k == SDLK_R) { if (fly) spawn_in(cam, room); else spawn_player(); }
                // Debug HP for testing the red damage-lag trail (feat: HUD_HP_BARS.md H3):
                // K = take 60 damage (watch the red ghost linger + drain), L = heal to full.
                else if (k == SDLK_K) { player.hp = std::max(1, player.hp - 60); hud.message("(debug) -60 HP"); }
                else if (k == SDLK_L) { player.hp = player.max_hp; hud.message("(debug) full heal"); }
                // Debug: B fills the EX/Boost gauge in ~4 presses (to eyeball the boost arc).
                else if (k == SDLK_B) { player.boost = std::min(30000.f, player.boost + 7500.f); hud.message("(debug) +Boost"); }
                // Boost (engine: button bit 8) and skill/magic (button bit 4). The engine's
                // pad bits have no keyboard mapping of their own, so these take free keys.
                else if (k == SDLK_X && !fly && !paused) try_boost();
                else if (k == SDLK_C && !fly && !paused) try_skill();
                else if (k == SDLK_TAB) {
                    character = character % 3 + 1;
                    game.g_flags[150] = character;
                    hud.message(std::string("Character: ") + kCharNames[character]);
                    // Swap the SCRIPT, not the sprite: the script picks the sprite.
                    aso = Aso::load(asset_root / "ANI" / char_aso(character));
                    avm = AsoVm{};
                    avm.bind(&aso);
                    avm.set_flags(game.g_flags, 0x200);
                    avm.tick();
                    sync_aia();
                    curAnim = std::uint32_t(std::max(0, chr.find_tag(std::uint32_t(avm.anim_tag()))));
                    animFrame = 0; shownFrame = -1;
                    reload_room();  // re-gate this room's chests for the new character
                }
                else if (k == SDLK_E && !fly) try_interact();
                else if (k >= SDLK_1 && k <= SDLK_9) {
                    std::size_t idx = std::size_t(k - SDLK_1);
                    if (idx < room.sen.triggers.size())
                        transition(room.sen.triggers[idx].dest_scene, &room.sen.triggers[idx]);
                }
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (fly) looking = true; else try_attack();   // play mode: LMB = attack
            }
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) looking = false;
            else if (e.type == SDL_EVENT_MOUSE_MOTION && looking) {
                cam.yaw -= e.motion.xrel * 0.004f;  // LH world: mouse-right = turn right
                cam.pitch -= e.motion.yrel * 0.004f;
                cam.pitch = std::max(-1.5f, std::min(1.5f, cam.pitch));
            } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                speed_mult *= (e.wheel.y > 0) ? 1.15f : 0.87f;
                speed_mult = std::max(0.1f, std::min(8.0f, speed_mult));
            } else if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                cw = std::uint32_t(e.window.data1); ch = std::uint32_t(e.window.data2);
                if (cw && ch) { configure(); wgpuTextureViewRelease(depth); depth = make_depth(g, cw, ch); }
            }
        }

        const bool* ks = SDL_GetKeyboardState(nullptr);
        const float D = room_diag(room);
        // A newly (re)built room (re)starts its event scripts on the persistent manager.
        if (room_events_dirty) { room_events_dirty = false; start_room_events(); }
        // The barrier just broke: play KEKKAIBREAK as a camera cutscene (once the world is not
        // already frozen). Its host forwards Eft_Kill to the world so the ward vanishes mid-pan;
        // when it ends (thaw), after_cutscene re-gates the room so the chest unlocks.
        if (barrier_break_pending && !csMgr.world_frozen() && !barrier_break_scripts.empty()) {
            barrier_break_pending = false;
            after_cutscene = [&] { reload_room(); hud.message("The barrier fades."); };
            start_cutscene_xso(std::make_shared<Xso>(barrier_break_scripts.front()),
                               "KEKKAIBREAK", /*debug235*/false);
        }
        // ---- fixed 60 Hz logic, 0..N ticks per rendered frame --------------
        // The catch-up loop in the exe has NO cap; a long stall really does emit a
        // burst of ticks. We reproduce that, relying on the load-time reset above
        // rather than on clamping, so timing stays exact under normal play.
        if (paused || statue_ui) accum = 0;  // freeze while the pause menu / statue menu is open
        if (paused && book.is_open()) {
            bookAccum += real_dt;
            while (bookAccum >= kTickDt && book.is_open()) {
                bookAccum -= kTickDt;
                // Auto-repeat (FUN_005d2ba0): a held direction fires on its 1st frame, its
                // 28th, then every 5th.
                const bool* ks = SDL_GetKeyboardState(nullptr);
                const bool held[4] = {ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A],
                                      ks[SDL_SCANCODE_RIGHT] || ks[SDL_SCANCODE_D],
                                      ks[SDL_SCANCODE_UP] || ks[SDL_SCANCODE_W],
                                      ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_S]};
                bool fire[4];
                for (int i = 0; i < 4; ++i) {
                    bookHold[i] = held[i] ? bookHold[i] + 1 : 0;
                    const int n = bookHold[i];
                    fire[i] = n == 1 || (n >= 28 && (n - 28) % 5 == 0);
                }
                BookInput in = bookIn;
                bookIn = {};
                in.hstep = fire[0] ? -1 : fire[1] ? 1 : 0;
                in.vstep = fire[2] ? -1 : fire[3] ? 1 : 0;
                in.held_left = held[0];
                in.held_right = held[1];
                BookModel bm = book_model();
                BookEvents bev;
                book.update(in, bm, bev);
                for (int id : bev.sounds) audio.play(id, kSfxMenuVol);
                if (bev.clamp_hp) player.hp = std::min(player.hp, int(player_stats().max_hp));
                if (bev.use_item >= 0) {                     // the Panacea (FUN_00443390, count - 1)
                    float hp = float(player.hp);
                    EffectEnv env;
                    env.flags = game.g_flags; env.hp = &hp; env.max_hp = player_stats().max_hp;
                    use_item(inv, inv_items, env, bev.use_item, nullptr);
                    player.hp = int(hp);
                }
                if (!bev.script.empty()) {                   // S_COMMON "@UseHammer" ...
                    std::copy(game.g_flags, game.g_flags + 0x200, csMgr.vm.g_flags);
                    csMgr.vm.g_flags[150] = character;
                    if (!csMgr.start(bev.script))
                        std::printf("  [book] %s did not start\n", bev.script.c_str());
                }
                if (bev.save_slot >= 0) do_save(bev.save_slot);
                if (bev.delete_slot >= 0) {
                    std::error_code ec;
                    fs::remove(slot_path(bev.delete_slot), ec);
                }
                if (bev.load_slot >= 0) {
                    std::ifstream f(slot_path(bev.load_slot), std::ios::binary);
                    std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(f), {}};
                    if (auto r = SaveRecord::decode(raw)) {
                        apply_loaded(from_record(*r));
                        hud.message("Game loaded.");
                    }
                }
                if (bev.options_changed) {
                    audio.set_bgm_volume(oslider[0] / 1024.0f);
                    audio.set_master_volume(oslider[1] / 1024.0f);
                    options_save(oslider, ocheck);
                }
                if (bev.option_quit == 1) running = false;  // Exit: WM_CLOSE
                if (bev.option_quit == 0)                    // [not modelled] Return to Title
                    hud.message("Return to Title is not implemented yet.");
                if (bev.closed) paused = false;
            }
        }
        int ticks = 0;
        while (accum >= kTickDt) {
        accum -= kTickDt;
        ++tick_count;
        ++ticks;
        // Tick the room's event scripts every frame (controllers poll here). If a Fix is held
        // (a talk / cutscene) the world is frozen: skip the player/enemy sim for this tick (the
        // camera still runs inside tick_events).
        // Battle-gate counter: flag 132 == DAT_0076bb2c == the engine's LIVE-ENEMY
        // count (0x76b91c + 132*4 = 0x76bb2c; Chr_Delete decrements it). A scripted
        // battle waits on Wait_FlagEqualInt(132, 0) — "all battle enemies dead" — to
        // resume (e.g. the S_1000 intro's battle_0). Maintain it each frame so killing
        // the mobs advances the cutscene. Written into BOTH VMs (the live cutscene VM
        // reads it directly; game keeps the canonical copy).
        {
            std::int32_t live = 0;
            for (const Actor& a : world.actors())
                if (a.is_enemy() && a.alive()) ++live;
            game.g_flags[132] = live;
            csMgr.vm.g_flags[132] = live;
            // Keep the world's player actor on the view's feet EVERY frame (incl. during
            // a frozen cutscene) so cutscene ops that resolve the player — Chr_LookAtChr,
            // Chr_IsInSphere/Box — see the real position. (The gameplay chase re-syncs
            // it below too; harmless.)
            if (Actor* wp = world.player()) wp->pos = feet;
        }
        const bool frozen_now = tick_events();
        // Persist what the cutscene GRANTED back to the game state. csMgr runs on a COPY
        // of the flags and its own item lists, so without this a cutscene's story-flag
        // progression AND item pickups are silently lost (e.g. BATTLE_0 hands Yunica the
        // Lila Shell + Crystal via 0xD5, and sets the intro's completion flags).
        {
            // The "Acquired X" BOX is shown live by CutsceneHost::acquire_item. Neither it nor
            // 0x116 grants anything: the item is owned through the script's own
            // Flag_SetInt(item, 1), applied with the grants below (audit event_vm #28).
            csMgr.vm.acquires.clear();
            csMgr.vm.items.clear();
            // Apply the recorded flag deltas (only what the cutscene actually changed, so
            // nothing the game set independently is clobbered).
            for (const Grant& gr : csMgr.vm.grants)
                if (gr.has_value && gr.index >= 0 && gr.index < 0x200)
                    game.g_flags[gr.index] = gr.value;
            csMgr.vm.grants.clear();
        }
        // Statue cleanse finished? SAVE.XSO has left csMgr.live. If it cleansed (flag 300
        // now set — the drain above persisted it) re-gate the room so the statue swaps to
        // its cleansed model; if it didn't (prereqs unmet, script exited early) fall back
        // to the save menu so the statue is still usable.
        if (cleanse_pending) {
            bool live = false;
            for (const auto& in : csMgr.live) if (!in.finished && in.name == "save") { live = true; break; }
            if (!live) {
                cleanse_pending = false;
                if (game.g_flags[300] == 0) { statue_ui = 1; statue_sel = 0; statue_reveal = 0; }
            }
        }
        // Statue MODEL swap timing (RE: SAVE.XSO 0xB4 spawns "s_SaveObjectChange", whose 0x16b
        // ops flip save_01 -> save_03 at the cleanse-glow beat mid-cutscene). flag300 flips
        // EARLIER (SAVE.XSO pc82), so we mark pending on the flip but do the mesh rebuild the
        // instant that child script appears on csMgr — matching the beat instead of popping the
        // model after the whole cutscene thaws. Mesh-only while frozen (no event re-run, which
        // would restart BATTLESTART); a post-thaw full reload is the fallback if it never ran.
        if (prev_flag300 == 0 && game.g_flags[300] != 0) statue_reload_pending = true;
        prev_flag300 = game.g_flags[300];
        if (statue_reload_pending) {
            bool swap_beat = false;
            for (const auto& in : csMgr.live) {
                std::string n = in.name; for (char& c : n) c = char(std::tolower((unsigned char)c));
                if (n == "s_saveobjectchange") { swap_beat = true; break; }
            }
            const bool thawed = !csMgr.world_frozen();
            if (swap_beat || thawed) {
                statue_reload_pending = false;
                reload_room(/*rerun_events=*/thawed);
                hud.message("The goddess statue is cleansed.");
            }
        }
        // A cutscene spawned/deleted an actor (op 0x12c/0x12d): load any new cast's
        // sprite and rebuild the render list so the NPC is drawn (or gone). Done here
        // so it happens whether or not the world is frozen.
        if (csHost.world_changed) {
            efx.init_from_world(g, asset_root, world, casts);
            render = build_enemy_render(world, casts, efx);
            bind_enemy_aso();   // mirror spawn_enemies: cutscene-spawned enemies must get
                                // their ASO bound too, or they render static (no idle/attack).
            csHost.world_changed = false;
        }
        // Cutscene WALK animation (CUTSCENE_POLISH_RE.md §3): the frozen 'continue' below skips
        // the whole sim, so a cutscene-moving actor's ASO never ticks and it slides in a static
        // idle frame. Drive the walk section (0) for any actor that is cmoving/p_moving, tick it
        // each frozen frame (draw_enemies / the player render read the ASO), and settle back to
        // idle on arrival. Walk clip = ASO section 0 -> AIA tag 0 for these casts.
        if (frozen_now) {
            for (EnemyRender& e : render) {
                if (!e.actor || !e.avm.ok()) continue;
                if (e.actor->cmoving) e.avm.set_section(0, /*force*/true);
                else if (e.avm.section() != e.idle_section) e.avm.set_section(e.idle_section, true);
                e.avm.tick();
            }
            if (csHost.p_moving) { avm.set_section(0, /*force*/true); avm.tick(); }
        }
        // The GoMesh pool updates every live step, a Fix or not (WAITOPS.md §4.7).
        auto step_meshes = [&] {
            meshes.step();
            mesh_delta.clear();
            for (auto& [nm, m] : meshes.all()) {
                if (!m.dirty_pos && !m.dirty_pose) continue;
                const Vec3 was = baked_pos.count(nm) ? baked_pos[nm] : m.spawn;
                if (m.dirty_pose) {
                    const char* clip = (m.sect >= 0 && std::size_t(m.sect) < m.clips.size())
                                           ? m.clips[std::size_t(m.sect)].name.c_str() : nullptr;
                    rebake_object(room.mesh, asset_root, nm, clip, m.time, &m.pos, false);
                    rebake_object(room.coll, asset_root, nm, clip, m.time, &m.pos, true);
                } else {
                    const Vec3 d{m.pos.x - was.x, m.pos.y - was.y, m.pos.z - was.z};
                    translate_object(room.mesh, nm, d);
                    translate_object(room.coll, nm, d);
                }
                mesh_delta[nm] = {m.pos.x - was.x, m.pos.y - was.y, m.pos.z - was.z};
                baked_pos[nm] = m.pos;
                static const bool mesh_log = std::getenv("CLERIA_MESHLOG") != nullptr;
                if (mesh_log && (tick_count % 30 == 0 || m.speed == 0.0f))
                    std::printf("  [mesh] %s pos (%.2f,%.2f,%.2f) speed %.3f clip %d t %.1f n %u\n", nm.c_str(),
                                m.pos.x, m.pos.y, m.pos.z, m.speed, m.sect, m.time, m.count);
                if (const RoomMesh::ObjRange* rr = room.mesh.object_range(nm))
                    if (rr->v1 > rr->v0 && rr->v1 <= room.mesh.vertices.size())
                        wgpuQueueWriteBuffer(g.queue, room.vbuf, std::size_t(rr->v0) * sizeof(RoomVertex),
                                             &room.mesh.vertices[rr->v0],
                                             std::size_t(rr->v1 - rr->v0) * sizeof(RoomVertex));
                m.dirty_pos = m.dirty_pose = false;
            }
        };
        if (frozen_now) { step_meshes(); continue; }
        const float dt = kTickDt;
        if (cooldown > 0) cooldown -= dt;

        if (fly) {
            const Vec3 f = cam.forward();
            const Vec3 right = normalize(cross({0, 1, 0}, f));
            const float step = D * 0.35f * speed_mult * dt;
            auto add = [&](const Vec3& d, float s) { cam.pos.x += d.x*s; cam.pos.y += d.y*s; cam.pos.z += d.z*s; };
            if (ks[SDL_SCANCODE_W]) add(f, step);
            if (ks[SDL_SCANCODE_S]) add(f, -step);
            if (ks[SDL_SCANCODE_D]) add(right, step);
            if (ks[SDL_SCANCODE_A]) add(right, -step);
            if (ks[SDL_SCANCODE_E] || ks[SDL_SCANCODE_SPACE]) cam.pos.y += step;
            if (ks[SDL_SCANCODE_Q] || ks[SDL_SCANCODE_LCTRL]) cam.pos.y -= step;
        } else {
            // Play mode: WASD moves the CHARACTER relative to the fixed camera; the
            // camera follows to keep her centred. Movement basis = the camera's
            // horizontal forward (into screen) and right.
            const float eye = kCharH, radius = kCharRadius;
            // anime_Movable is a movement-speed SCALE, not a boolean, and it is a large
            // part of why the three feel different: Yunica's swing drops it to 0 (rooted
            // through the commit), Hugo keeps a full 1.0, Toal goes 0.80 -> 0.05 so he
            // keeps drifting through his. anime_Turnable locks facing over the same span.
            const float stepUp = kStepUp;
            // Movement is relative to the live camera (last frame's — the 1-frame lag
            // is imperceptible and avoids a chicken-and-egg with the follow camera).
            const Vec3 fwd = normalize({std::cos(cam.yaw), 0, std::sin(cam.yaw)});  // screen-up
            const Vec3 rt = normalize(cross({0, 1, 0}, fwd));                       // screen-right
            Vec3 wish{0, 0, 0};
            if (ks[SDL_SCANCODE_W]) { wish.x += fwd.x; wish.z += fwd.z; }
            if (ks[SDL_SCANCODE_S]) { wish.x -= fwd.x; wish.z -= fwd.z; }
            if (ks[SDL_SCANCODE_D]) { wish.x += rt.x; wish.z += rt.z; }
            if (ks[SDL_SCANCODE_A]) { wish.x -= rt.x; wish.z -= rt.z; }
            const float wl = std::sqrt(wish.x * wish.x + wish.z * wish.z);
            const bool moving = wl > 1e-4f;
            // The engine's locomotion (physics_player.hpp): a keyboard direction is a full
            // stick, which is past the run threshold, so the player runs — the walk
            // modifier (+0x510 & 0x20) is not mapped yet. speed_mult is the debug wheel.
            {   // MOVER.md §3.3: the platform carry. Grounded on an object that moved last
                // step: its displacement (clamped to 0.3); airborne: the carry decays x0.975.
                auto it = mesh_delta.find(player_floor_obj);
                if (pv.grounded && it != mesh_delta.end()) {
                    Vec3 cd = it->second;
                    const float q = cd.x * cd.x + cd.y * cd.y + cd.z * cd.z;
                    if (q > 0.09f) { const float k = 0.3f / std::sqrt(q); cd = {cd.x * k, cd.y * k, cd.z * k}; }
                    carry = cd;
                } else if (!pv.grounded) {
                    carry = {carry.x * 0.975f, carry.y * 0.975f, carry.z * 0.975f};
                } else {
                    carry = {};
                }
                feet.x += carry.x; feet.y += carry.y; feet.z += carry.z;
            }
            {   const int sec = avm.section();
                const bool skill_sec = sec == 0x109 || (sec >= 0x10c && sec <= 0x111);
                const bool blessing_run = (game.g_flags[(0x76bc80 - 0x76b91c) / 4] & 0x2000) != 0;
                const float spd = player_speed(pm, character, 1.0f, avm.movable(), /*running*/ moving,
                                               pv.grounded, skill_sec, blessing_run) * speed_mult;
                step_planar(pvel, moving, std::atan2(wish.z, wish.x), spd, pv.grounded);
                const SurfaceScale ss = surface_scale(cur_surface, /*in_water*/ false);
                feet.x += pvel.x * ss.xz; feet.z += pvel.z * ss.xz;
                surface_up = ss.up;
            }
            if (moving && avm.turnable())
                face_dir = {wish.x / wl, 0, wish.z / wl};  // the swing faces where you walk
            {   // Knockback (+0x338/+0x340), spent by the player mover's own law (hit.hpp).
                const Impulse kd = step_knockback(player_kb, grounded, player.hp < 1, Mover::Player);
                feet.x += kd.x; feet.z += kd.z; }
            { Vec3 body = {feet.x, feet.y + stepUp, feet.z};
              body = resolve_walls(phys_mesh(room), body, radius, std::max(eye - stepUp, 0.5f));
              feet.x = body.x; feet.z = body.z; }
            // The engine's vertical model (physics_player.hpp), on the ORIGIN, which floats
            // at floor + 1.075: dy from the signed timer (linear while grounded, ½gt²
            // airborne), the down-ray from the origin, then landing / coyote / fall with the
            // 0.65 settle lerp. `feet` is derived back for the rest of the view.
            float oy = feet.y + kOriginH;
            {   float dy = vertical_dy(pv);
                if (dy > 0.0f) dy *= surface_up;      // sand2/lava/blood slow a rise too
                oy += dy; }
            int floor_tri = -1;
            const float fl = floor_at(phys_mesh(room), feet.x, feet.z, oy, &floor_tri);
            if (fl > kNoFloor) {
                last_floor = fl;                      // +0x318 persists when the ray misses
                if (oy - fl < 1.6f)                   // surface latch (0x45ba7e: hit t < 1.6)
                    cur_surface = surface_index_or_default(surface_of(phys_mesh(room), floor_tri));
            }
            settle_vertical(pv, oy, last_floor, kOriginH);
            grounded = pv.grounded;
            feet.y = oy - kOriginH;
            // The floor contact record (+0x540): the object under a grounded player whose floor
            // point lies within 1.1 x the origin height (FUN_0045a510 0x45c2b5).
            player_floor_obj.clear();
            if (pv.grounded && floor_tri >= 0 && fl > kNoFloor && oy - fl <= 1.1f * kOriginH)
                for (const RoomMesh::ObjRange& r : phys_mesh(room).object_ranges)
                    if (std::uint32_t(floor_tri) >= r.t0 && std::uint32_t(floor_tri) < r.t1) {
                        player_floor_obj = r.name; break;
                    }
            step_meshes();

            // Tick the enemies. The engine's enemies chase the player ACTOR; we sync it
            // to the view's player each tick, step the world (each enemy turns toward it
            // and advances at its ENEMYINFO speed -- world.hpp update_actor), then apply
            // COLLISION here, since the world is geometry-free and the view owns the mesh.
            // The engine integrates enemy motion with a wall-slide sweep inside
            // FUN_0045a510; we reuse the player's resolve_walls (push-out) against the
            // same collision mesh so enemies stop at walls/fences instead of clipping
            // through, then floor-snap. Enemy radius uses the player's for now -- a
            // per-cast collision radius (CASTINFO+0x34) is available but not yet wired.
            if (Actor* wp = world.player()) wp->pos = feet;
            // Enemy AI — the engine's handler (src/cleria/enemy_ai.hpp): sight/acquisition,
            // the M100 brain (approach at run speed, hold, waypoint wander, the 10-tick
            // pre-attack hold, the 30-tick aim, section 7, a 100..139-tick cooldown), the
            // action player and move primitive; then the sprite mover moves it along its
            // heading at speed x anime_Movable. STAND-IN for non-M100 casts: every enemy
            // runs the M100 brain until its own is ported (build/audit/ai_bosses.md).
            {
                const int d = game.g_flags[201] ? 3 : (difficulty - 2);   // -2 v.easy .. 2 nightmare
                auto in_view = [&](const Vec3& q) {
                    if (!have_vp) return true;
                    const auto& m = last_vp.m;
                    const float cx = q.x * m[0] + q.y * m[4] + q.z * m[8] + m[12];
                    const float cy = q.x * m[1] + q.y * m[5] + q.z * m[9] + m[13];
                    const float cz = q.x * m[2] + q.y * m[6] + q.z * m[10] + m[14];
                    const float cw = q.x * m[3] + q.y * m[7] + q.z * m[11] + m[15];
                    return cw > 0 && std::fabs(cx) <= cw && std::fabs(cy) <= cw && cz >= 0 && cz <= cw;
                };
                int pool = 0;
                for (const Actor& a : world.actors()) pool += a.alive();
                std::vector<std::pair<Actor*, AiSpawn>> brain_spawns;
                // The other actors, as brains that reach them see them (M460 and its bats).
                std::vector<Actor*> new_children;
                FnAiWorld aiw;
                aiw.each = [&](const std::function<void(Actor&)>& f) {
                    for (Actor& a : world.actors()) if (!a.is_player() && !a.destroy_requested()) f(a);
                };
                aiw.find_fn = [&](int id) -> Actor* {
                    for (Actor& a : world.actors())
                        if (a.instance_id == id && !a.is_player() && !a.destroy_requested()) return &a;
                    return nullptr;
                };
                aiw.spawn_fn = [&](Actor& p, int cast, const Vec3& dd, int lv, bool keep0) -> Actor* {
                    // FUN_00476d70: at the parent position + d (world axes), with its heading.
                    SpawnRequest req;
                    req.cast_type = cast;
                    req.pos = {p.pos.x + dd.x, p.pos.y + dd.y, p.pos.z + dd.z};
                    req.yaw_degrees = (1.5707963f - p.ai.heading) * 57.29577951f;
                    Actor* c = world.spawn(req);
                    if (!c) return nullptr;
                    c->height = cast_motion(casts, cast).height();
                    c->radius = cast_motion(casts, cast).radius();
                    c->level = lv;
                    c->floor_y = p.floor_y;
                    c->ai.heading = p.ai.heading;
                    c->ai.i(0x854) = p.instance_id;
                    c->ai.phase() = keep0 ? 0 : 4;
                    new_children.push_back(c);
                    return c;
                };
                aiw.remove_fn = [&](Actor& a) {                    // FUN_004593c0
                    world.request_destroy(a);
                    a.ai.hittable = false; a.hp = 0; a.visible = false;
                };
                aiw.ray_fn = [&](const Vec3& o, const Vec3& dl) {
                    AiWorld::Ray r;
                    r.hit = segment_hit(phys_mesh(room), o, {o.x + dl.x, o.y + dl.y, o.z + dl.z}, &r.point, &r.normal);
                    return r;
                };
                aiw.sphere_fn = [&](const Vec3& c, float r) { return sphere_overlaps(phys_mesh(room), c, r); };
                aiw.arena_fn = [&](float* r) { for (int k = 0; k < 4; ++k) r[k] = world.arena[k]; };
                aiw.raw_fn = [&](int cast) -> const float* {
                    const Enemy* er = etab.get(cast);
                    return er ? er->ai_raw : nullptr;
                };
                const Vec3 cam_fwd = cam.forward();
                for (EnemyRender& e : render) {
                    Actor* en = e.actor;
                    // The dead path of a handler that has extra work there (M460: its flock).
                    if (en && !en->alive() && !en->destroy_requested())
                        if (const EnemyDeathHook hk = enemy_death_hook_for(en->ai_index)) hk(*en, aiw);
                    if (!en || !en->alive() || en->destroy_requested() || !e.avm.ok()) continue;
                    const bool hurt = e.avm.section() == 4;
                    const bool attacking = e.atk_section >= 0 && e.avm.section() == e.atk_section;
                    AiEnv env;
                    env.self = {en->pos.x, en->pos.y + en->height, en->pos.z};
                    env.height = en->height;
                    env.radius = en->radius;
                    env.facing = e.facing;
                    env.grounded = en->vert.grounded;                // +0x388
                    env.jump_v0 = cast_motion(casts, en->cast_id).jump_v0;
                    env.actor = en;
                    env.world = &aiw;
                    env.character = character;
                    env.sprite_dir = sprite_dir_of(en->yaw, cam_fwd);
                    env.aso_frame = e.avm.frame();
                    env.movable = e.avm.movable();
                    env.durable = e.avm.durable();
                    env.target_floor = last_floor;
                    env.target_rehit_clear = player_iframes == 0;
                    env.target = {feet.x, feet.y + kOriginH, feet.z};
                    env.target_vel = {pvel.x, 0, pvel.z};
                    env.target_alive = player.hp >= 1;
                    env.target_grounded = pv.grounded;
                    env.was_hit = e.hit;
                    env.took_damage = e.dmg_hit;
                    env.group_tag = en->tag.tagged();
                    env.aso_marker = e.avm.marker();
                    env.aso_marker_block = e.avm.marker_block();
                    env.culled = !(en->flags12c & 0x1000000u) && !in_view(en->pos);   // never-cull bit
                    env.stun_immune = (en->flags12c & 0x80000u) != 0;                  // FACTORY.md
                    env.bless_stun = (game.g_flags[217] & 0x80) != 0;                  // GROW09
                    env.boss_fight = game.g_flags[156] == 1;                           // DAT_0076bb8c
                    env.difficulty = d;
                    env.diff_index = difficulty >= 2 ? difficulty - 2 : 0;
                    env.diff_easy = difficulty <= 1;
                    env.diff_very_easy = difficulty == 0;
                    env.pool_count = pool;
                    env.target_section = avm.section();
                    env.target_heading = std::atan2(face_dir.z, face_dir.x);
                    env.turnable = e.avm.turnable();
                    const Aso* escr = enemy_script(e.stem);
                    env.section_type = [escr](int id) {
                        const Aso::Section* s = escr ? escr->find(id) : nullptr;
                        return s ? int(s->type) : -1; };
                    env.blocked = [&](const Vec3& a0, const Vec3& b0) {
                        return segment_blocked(phys_mesh(room), a0, b0); };
                    env.has_floor = [&](float x, float z, float from_y, float depth) {
                        const float f = floor_at(phys_mesh(room), x, z, from_y);
                        return f > kNoFloor && from_y - f <= depth; };
                    env.rng = [&] { return std::uint32_t(rng()); };
                    e.hit = false;
                    e.dmg_hit = false;
                    env.hp = en->hp; env.max_hp = en->max_hp; env.section = e.avm.section();
                    const EnemyBrain own = enemy_brain_for(en->ai_index);
                    const AiOut o = enemy_ai_tick(e.actor->ai, e.aip, env, own ? own : enemy_brain_or_standin(en->ai_index));
                    // The brain's own writes to its HP (+0x98) / EXP (+0xb0), and its spawns.
                    if (o.set_hp >= 0) en->hp = o.set_hp;
                    en->exp_reward *= o.exp_scale;
                    for (const AiSpawn& s : o.spawns) brain_spawns.push_back({en, s});
                    for (int id : o.sounds) sfx_at(id, en->pos, kSfxAnimVol);
                    // [not modelled] attachment to the actor (FUN_00451c20) and the scale.
                    // The brain's effects, through the particle simulation (their hit fields arm
                    // the EF2's hit emitters; EFFECTHIT.md). Owned ones are killed / re-placed by tag.
                    for (const AiEffect& fx : o.effects) {
                        const Vec3 org{en->pos.x, en->pos.y + en->height, en->pos.z};
                        if (fx.kill_tag >= 0) { fxsim.kill_owned(en->instance_id, fx.kill_tag); continue; }
                        if (fx.move_tag >= 0) { fxsim.move_owned(en->instance_id, fx.move_tag, fx.pos, fx.rot, {1, 1, 1}); continue; }
                        ef2sim::SpawnParams sp;
                        sp.power = fx.power; sp.knock = fx.knock; sp.rtype = fx.rtype; sp.status = fx.status;
                        sp.atk = en->atk; sp.side = 1; sp.owner = en->instance_id; sp.tag = fx.tag;
                        spawn_sim_fx(fx.id, fx.pos, fx.rot, fx.scale, sp, fx.attach ? en->instance_id : 0, org);
                    }
                    pool += int(o.spawns.size());
                    if (ai_debug && (tick_count % 60) == 0)
                        std::printf("[ai] #%d cast %d phase %d sub %d act 0x%x steer %d mode %d spd %.4f "
                                    "dist %.2f head %.2f culled %d sec %d mov %.2f hp %.1f mk %d at (%.1f,%.1f)\n",
                                    en->instance_id, en->cast_id, e.actor->ai.phase(), e.actor->ai.i(0x728), e.actor->ai.action(),
                                    e.actor->ai.i(0x708), e.actor->ai.mode(), e.actor->ai.speed, e.actor->ai.dist(), e.actor->ai.heading,
                                    int(env.culled), e.avm.section(), e.avm.movable(), en->hp, env.aso_marker,
                                    en->pos.x, en->pos.z);
                    // Strike: the brain's FUN_00446550(sec,0) — a priority-gated request. A
                    // ported brain names its section literally; the M100 stand-in's 7 maps to
                    // this kind's guessed attack section.
                    const int want = (!own && o.section == 7 && e.atk_section >= 0) ? e.atk_section : o.section;
                    if (o.section >= 0 && (!hurt || o.section_force) && e.avm.set_section(want, o.section_force)) {
                        if (own && o.attack) e.atk_section = want;
                        e.atk_dx = std::cos(e.facing);
                        e.atk_dz = std::sin(e.facing);
                        e.atk_hit = false; e.atk_stall = 0; e.atk_fkey = -1;
                        e.atk_armed = false; e.atk_age = 0;
                    }
                    if (o.vm_step) e.avm.tick();                      // the brain's FUN_004472e0
                    // The brain writes its ASO registers directly (+0x9bc / +0x9c0 / +0x9d8);
                    // a section that restarts at pc 0 this tick clears the first two again.
                    if (o.set_marker > -1000) e.avm.set_marker(o.set_marker);
                    if (o.set_marker_block > -1000) e.avm.set_marker_block(o.set_marker_block);
                    if (o.set_durable >= 0) e.avm.set_durable(o.set_durable != 0);
                    if (o.set_own_jump >= 0) e.avm.set_own_jump(o.set_own_jump != 0);
                    if (o.set_frame >= 0) e.avm.set_frame(o.set_frame);
                    if (e.actor->ai.hit_cut) { e.atk_win = 0; e.actor->ai.hit_cut = false; }   // disengage
                    if (o.fast_forward >= 0) e.avm.fast_forward_to_marker(o.fast_forward);
                    if (o.snap_facing) e.facing = e.actor->ai.heading;
                    // A hit volume armed by the brain (FUN_004494e0), placed like any other.
                    for (const AiHitbox& h : o.hitboxes) {
                        if (h.new_serial) en->hit_serial = next_hit_serial(world.hit_serial_counter_);
                        const float args[13] = {h.shape, h.radius, h.fwd, h.angle_deg, h.voff, h.half_h,
                                                float(h.life), float(h.rehit), float(h.one_shot),
                                                float(h.rtype), float(h.status), h.power, h.knock};
                        e.hb = volume_from_args(args, en->hit_serial);
                        e.atk_win = h.life > 0 ? h.life : 1;
                        e.atk_armed = true;
                        e.atk_dx = std::cos(e.facing); e.atk_dz = std::sin(e.facing);
                    }
                    // GoXx bullets the brain made directly (FUN_00593550 / FUN_00593b90).
                    for (const AiBullet& b : o.bullets) {
                        bullet::Spawn s;
                        s.kind = b.kind; s.effect = b.effect;
                        const Actor* ow = b.owner < 0 ? en : world.find(b.owner);
                        s.owner = ow ? ow->instance_id : 0;
                        s.owner_monster = !(ow && ow->is_player());
                        s.owner_atk = ow ? float(ow->atk) : 0.0f;
                        s.pos = b.pos; s.dir = b.dir;
                        s.speed = b.speed; s.accel = b.accel; s.gravity = b.gravity;
                        s.power = b.power; s.knock = b.knock;
                        s.f158 = std::uint32_t(b.trail_effect); s.f15c = std::uint32_t(b.trail_period);
                        s.f160 = std::uint32_t(b.home_period); s.f164 = b.home_rate;
                        s.f168 = b.f168; s.f16c = b.f16c; s.f170 = b.f170; s.f174 = b.f174;
                        bullet::Bullet* bl = bullets.spawn(s);
                        if (!bl) continue;
                        if (b.fx_scale > 0.0f) {                            // the brain's +0x1ac.. write
                            bl->fx->scale_x = bl->fx->scale_y = bl->fx->scale_z = b.fx_scale;
                        }
                        if (b.gas) gas_owner[bl] = en->instance_id;
                    }
                    if (o.gauge_hp >= 0) {                         // FUN_0043a460: the boss gauge
                        gauge_owner = en->instance_id;
                        hud.boss_frac = en->max_hp > 0 ? std::clamp(o.gauge_hp / en->max_hp, 0.0f, 1.0f) : 0.0f;
                        const Cast* bc = casts.get(en->cast_id);
                        hud.boss_name = bc ? bc->display : std::string();
                    }
                    // The mover (FUN_00478080 -> FUN_0045f4d0, build/ai_spec/MOVER.md): the
                    // locomotion section on an index change (gated), the ASO step (Jump/Dash/
                    // Movable land here), then velocity + knockback, then the facing turn. The
                    // integrator (sweep + settle) runs in the per-actor pass after world.step().
                    const bool grounded_now = en->vert.grounded;
                    const bool movable_pre = e.avm.movable() != 0.0f;
                    int idx_pre = 0;
                    const int lsec = mover_pre(en->mover, e.actor->ai.mode(), grounded_now, e.avm.own_jump(),
                                               movable_pre, e.avm.section(), &idx_pre);
                    if (lsec >= 0) e.avm.set_section(lsec);          // FUN_00446550: priority-gated
                    e.avm.set_is_land(grounded_now);
                    e.avm.tick();
                    if (e.avm.jumped()) {                             // anime_Jump (FUN_00449460)
                        float oy = en->pos.y + en->height;
                        apply_jump(en->vert, oy, e.avm.jump_v(), cast_motion(casts, en->cast_id).jump_v0);
                        en->pos.y = oy - en->height;
                    }
                    if (e.avm.dashed()) apply_dash(en->mover, e.actor->ai.heading, e.avm.dash_d());
                    mover_post(en->mover, idx_pre, e.actor->ai.speed, e.actor->ai.heading, en->vert.grounded,
                               en->flyer, movable_pre, e.avm.movable(), en->hp, en->flags12c);
                    if (e.facing != e.actor->ai.heading)              // FUN_005eee60, x +0x36c (1.0)
                        e.facing = turn_facing(idx_pre == 2 ? 0.35f : 0.18f, e.facing, e.actor->ai.heading);
                    en->yaw = 1.5707963f - e.facing;   // view basis (sin, cos)
                    {                                                 // the handler-tail clamps
                        Vec3 org{en->pos.x, en->pos.y + en->height, en->pos.z};
                        enemy_handler_clamp(en->ai_index, org, game.g_flags[190], game.g_flags[156] != 0, world.arena);
                        en->pos = {org.x, org.y - en->height, org.z};
                    }
                    if (o.durable_rule == 1) e.avm.set_durable(true);              // the handler's
                    else if (o.durable_rule == 2 && e.avm.section() == 0x138) e.avm.set_durable(true);
                    else if ((o.durable_rule == 0 || o.durable_rule == 2) && e.avm.section() != 7) e.avm.set_durable(false);
                    if (hurt && e.avm.section_ended()) e.avm.set_section(e.idle_section, /*force*/true);
                    // anime_FireBullet (the VM case at 0x447b91, BULLETS.md §1.2): at the origin +
                    // (cos, sin)(arg4 deg + facing) x arg2, y + arg3; the direction args 5..7 or
                    // that heading; args 8.. straight into the block (13..15 as raw words).
                    for (const AsoVm::BulletEvent& fb : e.avm.bullets()) {
                        const float ang = fb.f[2] * 0.0174533f + e.facing;
                        const float c = float(std::cos(double(ang))), sn = float(std::sin(double(ang)));
                        bullet::Spawn s;
                        s.kind = fb.kind; s.effect = fb.id;
                        s.owner = en->instance_id; s.owner_atk = float(en->atk);
                        s.pos = {en->pos.x + c * fb.f[0], en->pos.y + en->height + fb.f[1], en->pos.z + sn * fb.f[0]};
                        Vec3 d{fb.f[3], fb.f[4], fb.f[5]};
                        if (d.x * d.x + d.y * d.y + d.z * d.z == 0.0f) d = {c, 0.0f, sn};
                        const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                        s.dir = dl == 0.0f ? Vec3{} : Vec3{d.x / dl, d.y / dl, d.z / dl};
                        s.speed = fb.f[6]; s.accel = fb.f[7]; s.gravity = fb.f[8];
                        s.power = fb.f[9]; s.knock = fb.f[10];
                        std::memcpy(&s.f158, &fb.f[11], 4);
                        std::memcpy(&s.f15c, &fb.f[12], 4);
                        std::memcpy(&s.f160, &fb.f[13], 4);
                        s.f164 = fb.f[14] * 0.0174533f;
                        s.f168 = fb.f[15]; s.f16c = fb.f[16]; s.f170 = fb.f[17]; s.f174 = fb.f[18];
                        bullets.spawn(s);
                    }
                    // anime_GenerateEffect (FUN_004496f0, EFFECTHIT.md §6): at origin + (cos, sin)
                    // (angle + facing) x fwd, y + voff, yaw angle - (facing + pi/2), its args 6..9
                    // as the hit fields; attach mode != 0 follows the actor (as mode 2).
                    for (const AsoVm::EffectEvent& fe : e.avm.effects()) {
                        const float a = fe.angle_deg * 0.0174533f;
                        const float an = a + e.facing;
                        const Vec3 org{en->pos.x, en->pos.y + en->height, en->pos.z};
                        const Vec3 at{org.x + std::cos(an) * fe.fwd, org.y + fe.voff, org.z + std::sin(an) * fe.fwd};
                        ef2sim::SpawnParams sp;
                        sp.power = fe.power; sp.knock = fe.knock; sp.rtype = fe.rtype; sp.status = fe.status;
                        sp.atk = en->atk; sp.side = 1; sp.owner = en->instance_id;
                        spawn_sim_fx(fe.id, at, {0.0f, a - (e.facing + 1.5707963f), 0.0f}, fe.scale, sp,
                                     fe.attach != 0 ? en->instance_id : 0, org);
                    }
                    // Its GenerateCollision arms the enemy's hit-volume, with the section's own
                    // radius/lifetime/power/knock.
                    if (e.avm.armed_hitbox()) {
                        e.hb = e.avm.hitbox();
                        e.atk_win = e.hb.life > 0 ? e.hb.life : 1;
                        e.atk_hit = false;
                        e.atk_armed = true;
                    }
                    // Release a swing that holds its last frame (attack sections jmp-hold):
                    // once the box has fired, its window is spent and the clip has stalled.
                    if (e.avm.section() == e.atk_section) {
                        ++e.atk_age;
                        const long fk = (long(e.avm.anim_tag()) << 20) ^ e.avm.frame();
                        e.atk_stall = (fk == e.atk_fkey) ? e.atk_stall + 1 : 0;
                        e.atk_fkey = fk;
                        const bool recovered = e.atk_armed && e.atk_win <= 0 && e.atk_stall >= 8;
                        if (recovered || e.atk_age >= 180) {
                            e.avm.set_section(e.idle_section, /*force*/true);
                            e.loco = -1;
                        }
                    }
                }
                // A brain's spawns (FUN_00476d70: a fresh actor of that cast at the given
                // spot and heading, phase 0). Appended after the loop: `render` can't grow
                // while it is being iterated. The child shares its parent's sprite set.
                for (auto& [parent, s] : brain_spawns) {
                    SpawnRequest req;
                    req.cast_type = s.cast;
                    req.pos = s.pos;
                    req.yaw_degrees = (1.5707963f - s.yaw) * 57.29577951f;   // heading -> view yaw
                    Actor* c = world.spawn(req);
                    if (!c) continue;
                    if (s.hp >= 0) c->hp = s.hp;
                    if (s.exp_copy) c->exp_reward = parent->exp_reward;
                    const std::string stem = casts.sprite_stem(c->cast_id);
                    if (stem.empty() || !efx.kinds.count(stem)) continue;
                    const Cast* cc = casts.get(c->cast_id);
                    render.push_back({c, stem, (cc && !cc->display.empty()) ? cc->display : stem});
                    bind_one_enemy(render.back());
                }
                for (Actor* c : new_children) {
                    const std::string stem = casts.sprite_stem(c->cast_id);
                    if (!stem.empty() && !efx.kinds.count(stem)) efx.load_kind(g, asset_root, stem);
                    if (stem.empty() || !efx.kinds.count(stem)) continue;
                    const Cast* cc = casts.get(c->cast_id);
                    const float heading = c->ai.heading;
                    render.push_back({c, stem, (cc && !cc->display.empty()) ? cc->display : stem});
                    bind_one_enemy(render.back());
                    c->ai.heading = heading;
                    render.back().facing = heading;
                    if (c->aso_local15) render.back().avm.set_local(15, c->aso_local15);   // +0xa18
                }
                // The boss gauge disappears with its owner (DAT_006d5fd8 cleared at death).
                if (hud.boss_frac >= 0.0f && gauge_owner != 0) {
                    bool live = false;
                    for (const Actor& a : world.actors()) live |= a.instance_id == gauge_owner && a.alive();
                    if (!live) { hud.boss_frac = -1.0f; gauge_owner = 0; }
                }
            }

            world.step();
            // Age script-placed effects and reap finished one-shots (the enemy warp-in rings).
            world.tick_effects([&](int id) {
                return bullet_art(g, asset_root, efl, eft, id).life;
            });
            for (Actor& a : world.actors()) {
                if (a.is_player() || !a.alive()) continue;
                if (a.hit_flash > 0) --a.hit_flash;
                // The integrator FUN_0045a510 (MOVER.md §3): one swept sphere (radius +0x328,
                // centred on the ORIGIN) through D = (displacement, dy), the floor ray from the
                // pre-move origin, then the settle (land with a 0.65 lerp / coyote / fall).
                // A flyer: dy = t (0 for the AI), no settle — the hover owns its altitude.
                {
                    const Vec3 o0{a.pos.x, a.pos.y + a.height, a.pos.z};
                    const float dy = a.flyer ? a.vert.t : vertical_dy(a.vert);
                    const float hitY = floor_at(phys_mesh(room), o0.x, o0.z, o0.y);
                    Vec3 o1 = collide_and_slide(phys_mesh(room), o0, {a.mover.dx, dy, a.mover.dz}, a.radius);
                    if (hitY > kNoFloor && o1.y > hitY) a.floor_y = hitY;     // +0x318
                    if (!a.flyer) {
                        a.vert.jumping = a.mover.loco == 3;                  // the coyote keys on +0x3a0
                        settle_vertical(a.vert, o1.y, a.floor_y, a.height);
                    }
                    a.pos = {o1.x, o1.y - a.height, o1.z};
                }
            }

            // The enemy's reaction to the player's hit (FUN_004fbb40, src/cleria/hit.hpp).
            // No i-frames and no hit-stop exist for enemies. The knockback impulse `k` is
            // ADDED to +0x338/+0x340 whatever the damage; the flinch (section 4; a re-hit
            // while flinching restarts it) needs dmg >= 1 and is skipped for rtype 3 or a
            // durable victim (anime_Durable ~ +0x9d8).
            auto react_enemy = [&](Actor& en, int dmg, bool died, int rtype, const Impulse& k) {
                en.hit_flash = 6;                       // our cosmetic tint (+0x4e8)
                en.mover.k1.x += k.x; en.mover.k1.z += k.z;   // knock channel 1 (+0x338/+0x340)
                if (died) return;                       // death is handled by the caller
                for (EnemyRender& er : render) {
                    if (er.actor != &en || !er.avm.ok()) continue;
                    er.hit = true;                      // +0x3bc & 4: wakes an unaware enemy
                    if (dmg >= 1) er.dmg_hit = true;    // +0x3bc & 2: a damaging hit
                    if (enemy_flinch_state(dmg, false, rtype, er.avm.durable(), er.avm.section()) == 4) {
                        er.avm.set_section(4, /*force*/ er.avm.section() == 4);
                        er.atk_win = 0; er.atk_armed = false; er.atk_age = 0;
                    }
                    break;
                }
            };

            // The player's melee swing (build/attack_re). While the hit-volume is live, an
            // enemy inside the sphere is hit, then shielded for the swing's re-hit timer
            // (arg[7]; the engine's 10-slot victim bank) — so a box whose timer is shorter
            // than its lifetime multi-hits. Damage is FUN_004fa550 (hit.hpp), NOT
            // damage_real: elemental resist multiplier, a min-1 chip, Yunica's x1.1.
            if (atk_window > 0) {
                --atk_window;
                // FUN_004fed50 mode 0 (build/ai_spec/HITVOLUME.md §2): the volume is placed
                // from the player's ORIGIN and current facing every tick, then tested against
                // each monster's hurt body; the victim's bank keys on this attack's serial.
                const float pface = std::atan2(face_dir.z, face_dir.x);
                const HitShape vol = volume_shape(atk_hb, {feet.x, feet.y + kOriginH, feet.z}, pface);
                const Vec3 c = vol.centre;
                int killed = 0, broke = 0;
                for (Actor& en : world.actors()) {
                    if (en.is_player() || !en.alive() || en.destroy_requested()) continue;
                    const EnemyRender* vr = nullptr;
                    for (const EnemyRender& er : render) if (er.actor == &en) { vr = &er; break; }
                    if (vr && !vr->actor->ai.hittable) continue;            // +0x60 & 0x80 clear
                    const HitShape body = body_shape({en.pos.x, en.pos.y + en.height, en.pos.z},
                                                     en.hurt_radius, en.hurt_half_h, en.hurt_shape, en.hurt_off);
                    if (!overlap(vol, body)) continue;
                    if (en.bank.check_and_record(player_hit_serial, atk_hb.rehit)) continue;   // FUN_004faac0
                    if (atk_hb.one_shot) atk_window = 0;                     // 0x4fd251
                    AttackStats a;
                    a.atk = player_stats().atk;
                    a.bonus = atk_hb.power;          // per-attack power, from this section's script
                    EnemyDefense vd; vd.def = en.def;
                    for (int k = 0; k < 4; ++k) vd.resist[k] = en.resist[k];
                    PlayerDamageMods mods;
                    mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
                    int dmg = enemy_incoming_damage(a, player_damage_class(avm.section()), vd, mods,
                        [&](float pct) { return float(rng_pct(rng)) < pct; });
                    // The guard (FUN_004fbb40): a victim in section 6 facing the swing blocks it.
                    if (vr && vr->avm.ok() &&
                        guard_blocks(en.cast_id, vr->avm.section() == 6, vr->avm.durable(), atk_hb.rtype,
                                     vr->facing, std::atan2(atk_dir.z, atk_dir.x)))
                        dmg = 0;
                    const HitResult r = apply_damage(en.hp, en.flags, dmg);
                    if (dmg > 0) add_boost(300.0f);  // +300 per hit dealt (0x4fd0bf, DAT_006bdc44)
                    // Knockback: L = walk speed (CASTINFO+0x48) x1.7 [x run mult while running —
                    // the view has no run state yet] x the swing's arg[12], driven along the
                    // player's facing (hit.hpp player_knock_angle).
                    const float kang = player_knock_angle(std::atan2(atk_dir.z, atk_dir.x),
                                                          feet.x, feet.z, feet.y, en.pos.x, en.pos.z, en.pos.y);
                    react_enemy(en, dmg, r.died, atk_hb.rtype,
                                knock_impulse(kang, player_knock_scale(pm.speed_base, false, 2.2f) * atk_hb.knock));
                    const std::string nm = casts.get(en.cast_id) && !casts.get(en.cast_id)->display.empty()
                                           ? casts.get(en.cast_id)->display : ("cast " + std::to_string(en.cast_id));
                    hud.message(r.died ? (nm + " defeated!")
                                       : (nm + "  -" + std::to_string(dmg) + " (" +
                                          std::to_string(int(en.hp)) + "/" +
                                          std::to_string(int(en.max_hp)) + ")"));
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  hit %s (lv%d DEF %.0f): %d dmg, %.0f/%.0f left\n",
                                    nm.c_str(), en.level, en.def, r.damage, en.hp, en.max_hp);
                    if (r.died) {
                        ++killed;
                        award_exp(en.exp_reward);
                        if (en.cast_id >= 0 && en.cast_id < 256) {   // FUN_004fbb40: the Topic record
                            topic_seen[en.cast_id] = 1;
                            topic_kills[en.cast_id] = std::min(topic_kills[en.cast_id] + 1, 50);
                        }
                        // Death entry plays p_hit0 from C for any actor, and the enemy's
                        // own death animation adds its z_dead* pair (SFX_TRIGGERS.md §4.3).
                        // Positional: panned/attenuated from the enemy's spot.
                        sfx_at(kSfxDeathEntry, en.pos, kSfxAnimVol);
                        for (int id : enemy_death_sounds(asset_root, casts.sprite_stem(en.cast_id)))
                            sfx_at(id, en.pos, kSfxAnimVol);
                    }
                }
                // BREAKABLES (SOB trigger class 1 with a script): the same swing volume also
                // tests the room's attackable scene objects. @Break_* is a one-hit break —
                // no SOB field holds HP, and Mesh_SetHp is called by none of the shipped
                // scripts — then it plays SE 114, rolls each of its two $I drop slots and
                // fades the mesh out. The roll rate is the script's own: @Break_5 20%,
                // @Break_30 50%, @Break_100 always.
                for (const SceneTrigger& t : room.mesh.triggers) {
                    if (!(t.trigger & 1) || broken_objects.count(t.name)) continue;
                    // Sphere vs the object's world AABB.
                    const float qx = std::max(t.aabb_min.x, std::min(c.x, t.aabb_max.x));
                    const float qy = std::max(t.aabb_min.y, std::min(c.y, t.aabb_max.y));
                    const float qz = std::max(t.aabb_min.z, std::min(c.z, t.aabb_max.z));
                    const float ddx = c.x - qx, ddy = c.y - qy, ddz = c.z - qz;
                    if (ddx * ddx + ddy * ddy + ddz * ddz > atk_hb.radius * atk_hb.radius) continue;
                    broken_objects.insert(t.name);
                    sfx_at(114, t.position, kSfxAnimVol);      // the shatter SE
                    int pct = 50;                               // @Break_30
                    if (t.script_stem.find("_5") != std::string::npos) pct = 20;
                    if (t.script_stem.find("_100") != std::string::npos) pct = 100;
                    for (int slot : {t.i0, t.i1}) {
                        if (slot <= 0) continue;
                        if (pct < 100 && rng_pct(rng) >= float(pct)) continue;
                        if (slot < int(std::size(game.item_arr))) game.item_arr[slot] = 1;
                        hud.message(items_tbl.name(slot));
                    }
                    ++broke;
                }
                if (broke) reload_room(/*rerun_events*/false);   // drop the shattered mesh
                if (killed) {
                    int left = 0;
                    for (const Actor& en : world.actors())
                        if (!en.is_player() && en.alive()) ++left;
                    std::printf("  defeated %d (%d left)\n", killed, left);
                    if (left == 0) clear_barrier();
                }
            }

            // Enemies hitting the player. Damage is the REAL player-incoming formula
            // (combat.hpp player_incoming_damage = FUN_004fa740), gated by the player's
            // i-frames exactly as the engine does (one hit per window, however many
            // enemies are on you). Damage now comes from the enemy's own ASO attack — its
            // GenerateCollision box, armed on the swing frame in the brain above — not from
            // touching it. player_def is the lv1 curve; no equipment model.
            // The countdown is frozen while a hurt section (4/9/0xb) plays (0x466cc1).
            tick_iframes(player_iframes, avm.section());
            for (Actor& a : world.actors()) a.bank.tick();          // FUN_0045c490: every actor
            player_bank.tick();
            // STEP PLATES (FUN_0045a4a0): while no script holds a fix, the player's contact with
            // an object whose touch script is enabled starts that script (single instance per
            // name). The scripts gate themselves: Mesh_EnableEvent(off), ...,
            // Wait_ChrLeaveMesh, Mesh_EnableEvent(on). Contact = the floor record; an object
            // with no collision hull falls back to its render bounds [STAND-IN].
            if (events_enabled && !csMgr.world_frozen())
                for (const SceneTrigger& t : room.mesh.triggers) {
                    if (!(t.trigger & 2) || t.script.empty()) continue;
                    std::string ln = t.name;
                    for (char& ch : ln) ch = char(std::tolower((unsigned char)ch));
                    bool on;
                    if (phys_mesh(room).object_range(ln)) {
                        on = player_floor_obj == ln;
                    } else {
                        const float pad = kCharRadius;
                        on = grounded && feet.x >= t.aabb_min.x - pad && feet.x <= t.aabb_max.x + pad &&
                             feet.z >= t.aabb_min.z - pad && feet.z <= t.aabb_max.z + pad &&
                             feet.y >= t.aabb_min.y - 1.0f && feet.y <= t.aabb_max.y + 2.0f;
                    }
                    const MeshObj* mo = meshes.find(ln);
                    if (on && (!mo || mo->event_on)) {
                        fs::path sp(t.script);
                        std::string stem = sp.stem().string();
                        for (char& ch : stem) ch = char(std::tolower((unsigned char)ch));
                        // The object's script reads its own name as $S0 and its SOB values
                        // as $I0/$I1/$F0 (audit event_vm #11; TURNSWITCH1 / S20SWICH00_FUMI
                        // pose and wait on `$S0`).
                        XsoVm::Vars ov;
                        ov.s[0] = t.name; ov.i[0] = t.i0; ov.i[1] = t.i1; ov.f[0] = t.f0;
                        if (csMgr.start(stem, ov))
                            std::printf("  [plate] %s -> %s\n", t.name.c_str(), stem.c_str());
                    }
                }
            // Boost drain (FUN_00462c40): the gauge falls ~30/tick on Normal, so a Boost runs
            // ~1000 frames (~16.7 s). At zero the mode clears and the player drops to idle.
            if (player.boosting) {
                player.boost -= 30.0f;
                if (player.boost <= 0.0f) {
                    player.boost = 0.0f;
                    player.boosting = false;
                    game.g_flags[141] = 0;                // FUN_00462c40: the timer ran out
                    avm.set_section(2, /*force*/true);      // FUN_00446550(2,0) -> idle
                }
            }
            // SP regen (mover FUN_00465170): a flat base of 5.0/tick plus a per-slot addend
            // whose table (0x68b078) is not dumped — so this is the base only. [PARTIAL]
            player.sp_cur = std::min(player.sp_max, player.sp_cur + 5.0f);
            player.mp_pct = player.sp_max > 0 ? player.sp_cur / player.sp_max * 100.0f : 0.0f;
            // The statue heal: the heal-spot objects 0x94 registered (FUN_0058c290; SCENEFLOW.md
            // §4). Each statue's S_SaveObjectChange registers one only on its cleansed arm, so
            // that is what gates healing. Within 3.0 (XZ) and the height band, no Fix: every
            // 15 steps HP +9999 and the second gauge +3000 (both clamped). Flag 191 = inside.
            if (!csMgr.world_frozen())
                for (auto& hs : csHost.heal_spots) {
                    if (hs.cooldown > 0) { --hs.cooldown; continue; }
                    const float dx = hs.x - feet.x, dz = hs.z - feet.z;
                    if (dx * dx + dz * dz >= 9.0f) { game.g_flags[191] = 0; continue; }
                    if (std::fabs(hs.y - (feet.y + kOriginH) + 1.0f) >= 0.5f) continue;
                    if (game.g_flags[191] == 0) { game.g_flags[191] = 1; sfx_at(0x12, feet, kSfxAnimVol); }
                    hs.cooldown = 15;
                    if (player.hp < 1) continue;
                    // [H-med] the second gauge 0x76a738 is taken as the SP/mana bar.
                    player.sp_cur = std::min(player.sp_max, player.sp_cur + 3000.0f);
                    if (player.hp < player.max_hp) {
                        player.hp = player.max_hp;   // +9999, clamped
                        sfx_at(0xad, feet, kSfxAnimVol);
                    }
                }
            const float pdef = player_stats().def;
            // A hit landing on the player: FUN_004fd350's player branch (src/cleria/hit.hpp).
            // Damage is FUN_004fa740 with the swing's own power as the bonus (attacker +0xfc
            // = GenerateCollision arg[11]), halved during Boost; then the severity reaction —
            // i-frames 8/12/16, hurt section 4/0xb/9, knockback 0.1 (+1.2 on a knockdown)
            // along `angle` (atan2(dz,dx) from the attacker to the player), and +dmg*100
            // Boost gauge. `rtype` is the swing's arg[9].
            auto hurt_player = [&](float atk, float bonus, int rtype, float angle) {
                const int raw = player_incoming_damage(AttackStats{atk, bonus, 0}, pdef,
                    [&](float p) { return float(rng_pct(rng)) < p; });
                const int dmg = player_damage_after_boost(raw, player.boosting);
                player.hp -= dmg;
                const bool died = player.hp < 1;              // the engine's test is < 1.0
                const PlayerReaction re = player_reaction(dmg, float(player.max_hp), rtype, died,
                                                          avm.durable(), avm.section());
                if (ai_debug)
                    std::printf("[hurt] dmg %d state %d grant %d pif %d psec 0x%x\n", dmg,
                                re.motion_state, re.iframes, player_iframes, avm.section());
                if (re.iframes > 0) player_iframes = std::max(player_iframes, re.iframes);
                if (re.motion_state >= 0 && re.motion_state != 5) avm.set_section(re.motion_state);
                if (re.face_attacker)
                    face_dir = {std::cos(angle + 3.14159265f), 0, std::sin(angle + 3.14159265f)};
                const Impulse k = knock_impulse(angle, re.knockback);
                player_kb.x += k.x; player_kb.z += k.z;
                if (re.boost_gain > 0) add_boost(re.boost_gain);
                // p_damage is the player's own hurt-reaction sound; death adds p_hit0.
                if (dmg > 0) audio.play(kSfxPlayerHurt, kSfxAnimVol);
                if (died) audio.play(kSfxDeathEntry, kSfxAnimVol);
                hud.message("-" + std::to_string(dmg) + " HP");
                if (died) {  // STAND-IN: the game-over / continue flow is not RE'd yet
                    player.hp = player.max_hp;
                    spawn_player();
                    player_kb = {};
                    player_iframes = 90;
                    hud.message("Down! respawned");
                }
            };
            for (EnemyRender& e : render) {
                Actor* en = e.actor;
                // An enemy hurts only through an armed volume: the body pass (FUN_0045e460) only
                // pushes, it never damages (HITVOLUME.md §5), so there is no contact damage.
                if (e.atk_win <= 0) continue;
                --e.atk_win;
                if (ai_debug && en)
                    std::printf("[swing] #%d win %d hit %d pif %d psec 0x%x box(%.2f,%.2f r%.2f) "
                                "dir(%.2f,%.2f) d_player %.2f\n", en->instance_id, e.atk_win,
                                int(e.atk_hit), player_iframes, avm.section(),
                                volume_centre(e.hb, {en->pos.x, en->pos.y + en->height, en->pos.z}, e.facing).x,
                                volume_centre(e.hb, {en->pos.x, en->pos.y + en->height, en->pos.z}, e.facing).z,
                                e.hb.radius, e.atk_dx, e.atk_dz,
                                std::sqrt((feet.x - en->pos.x) * (feet.x - en->pos.x) +
                                          (feet.z - en->pos.z) * (feet.z - en->pos.z)));
                // FUN_004fed50 mode 1 -> FUN_004fe9e0: the player alone (no team mask). The
                // volume follows the attacker: re-placed from its origin and visible facing
                // each tick. P+0x734 (player_iframes) guards; the player's bank keys on the
                // enemy's spawn serial, so its re-hits are paced only by the timer.
                if (!en || !en->alive() || player_iframes > 0) continue;
                const HitShape vol = volume_shape(e.hb, {en->pos.x, en->pos.y + en->height, en->pos.z}, e.facing);
                const Cast* pc = casts.get(13);
                const HitShape pbody = body_shape({feet.x, feet.y + kOriginH, feet.z},
                                                  pc ? pc->hurt_radius : 0.45f, pc ? pc->hurt_half_h : 0.5f,
                                                  pc ? pc->hurt_shape : 1, pc ? pc->center_offset : -0.25f);
                float hang = 0;
                if (!overlap(vol, pbody, &hang)) continue;
                if (player_bank.check_and_record(en->hit_serial, e.hb.rehit)) continue;
                if (e.hb.one_shot) e.atk_win = 0;
                e.atk_hit = true;
                hurt_player(en->atk, e.hb.power, e.hb.rtype, hang);
            }
            // The EF2 particles (EF2SIM.md §1.1: effects, then particles, after the actors). An
            // enemy effect's armed particle tests the player alone (EFFECTHIT.md §5): the
            // player's hit guard, the overlap, the re-hit bank keyed on token 0 (shared by every
            // effect), then the damage with the owner's ATK and the effect's power.
            {
                // M460's handler pass #14: a gas puff that sank below its floor sits on it.
                for (auto& bp : bullets.list()) {
                    auto it = gas_owner.find(bp.get());
                    if (it == gas_owner.end()) continue;
                    const Actor* ga = world.find(it->second);
                    if (!ga) continue;
                    const float lvl = ga->ai.f(0x7c8);
                    if (bp->s.dir.y != 0.0f && lvl > bp->s.pos.y) { bp->s.dir.y = 0.0f; bp->s.pos.y = lvl; }
                }
                bullet::Env benv;
                benv.player = {feet.x, feet.y + kOriginH, feet.z};
                benv.player_hp = float(player.hp);
                benv.ray = [&](const Vec3& o, const Vec3& dl, int set) {
                    bullet::RayHit r;
                    if (set != 2) return r;                          // [not modelled] water (set 1)
                    r.hit = segment_hit(phys_mesh(room), o, {o.x + dl.x, o.y + dl.y, o.z + dl.z}, &r.point, &r.normal);
                    return r;
                };
                benv.actor = [&](int id, Vec3* p, float* hp) {
                    const Actor* a = world.find(id);
                    if (!a) return false;
                    *p = {a->pos.x, a->pos.y + a->height, a->pos.z};
                    *hp = a->hp;
                    return true;
                };
                std::vector<bullet::Event> bev;
                bullets.update(benv, &bev);
                for (auto it = gas_owner.begin(); it != gas_owner.end();) {
                    bool live = false;
                    for (auto& bp : bullets.list()) live = live || bp.get() == it->first;
                    it = live ? std::next(it) : gas_owner.erase(it);
                }
                for (const bullet::Event& ev : bev) {
                    if (ev.type == bullet::Event::kSound) { sfx_at(ev.id, ev.pos, kSfxAnimVol); continue; }
                    if (ev.target == 0) {                            // FUN_004fa160: the player
                        player.hp = std::min(player.max_hp, player.hp + int(ev.amount));
                    } else if (Actor* a = world.find(ev.target)) {  // the owner, clamped to its max
                        a->hp = std::min(a->max_hp, a->hp + ev.amount);
                    }
                }
            }
            for (Actor& a : world.actors())
                actor_mats[a.instance_id] = ef2sim::translation(a.pos.x, a.pos.y + a.height, a.pos.z);
            {
                const Cast* pc = casts.get(13);
                const HitShape pbody = body_shape({feet.x, feet.y + kOriginH, feet.z},
                                                  pc ? pc->hurt_radius : 0.45f, pc ? pc->hurt_half_h : 0.5f,
                                                  pc ? pc->hurt_shape : 1, pc ? pc->center_offset : -0.25f);
                fxsim.step([&](ef2sim::Particle& p) {
                    if (p.hit.side != 1 || player_iframes > 0 || player.hp < 1) return false;
                    const HitShape vol{p.hit.radius, p.hit.centre, p.hit.half_h, p.hit.half_h > 0.0f ? 1 : 0};
                    float ang = 0;
                    if (!overlap(vol, pbody, &ang)) return false;
                    if (player_bank.check_and_record(0u, p.hit.rehit)) return false;
                    hurt_player(p.hit.atk, p.hit.power, p.hit.rtype, ang);
                    return true;
                });
                fxsim.sweep();
                if (ai_debug && (tick_count % 60) == 0 && !fxsim.effects().empty())
                {
                    std::printf("[fx] effects %zu particles %zu:", fxsim.effects().size(), fxsim.particles().size());
                    for (const auto& f : fxsim.effects()) std::printf(" %d(owner %d live %d)", f->id, f->owner, f->live);
                    std::puts("");
                }
            }

            // Footsteps — the game's rule, not ours. It is a COUNTDOWN IN TICKS
            // reloaded from the surface record, gated on the motion state:
            //
            //   FUN_0045c490, once per tick:
            //     if (!enable) skip
            //     if (timer != 0) { timer--; skip }        // 0045cee2
            //     if (!moving_state) { step_counter = 0; skip }
            //     if (motion_state in {0,1}) TryFootstep()  // 0045cf26
            //   FUN_0045d600 (TryFootstep):
            //     if (!IsLand) return
            //     rec = SURFACE[latched_surface]
            //     if (rec.se_id == 0) return               // silent: NO reload
            //     play(rec.se_id + (step_counter++ & rec.mask))
            //     timer = (state == 1) ? rec.run_ticks : rec.walk_ticks
            //     if (rec.jitter) timer += rng % rec.jitter
            //
            // `motion_state` is actor+0x994 — 0 = walk, 1 = run — which is the ASO
            // SECTION ID, so our VM supplies it directly. Note the cadence is
            // deliberately NOT animation-synced: 20 ticks/step against C_010's
            // 48-tick walk cycle, so steps drift against footplants by design.
            if (step_timer > 0) {
                --step_timer;
            } else if (grounded && (avm.section() == 0 || avm.section() == 1)) {
                const bool is_running = avm.section() == 1;
                // The exe's jitter uses xorshift128 (FUN_00439120) — the same
                // generator XsoVm implements, though the exe keeps it in a separate
                // state block (DAT_0074622c) from the script RNG.
                step_timer = play_footstep(audio, phys_mesh(room), floor_tri, step_n++,
                                           is_running, game.rng_next());
            } else if (!moving) {
                step_n = 0;   // the exe resets the counter when not in a step state
            }
            // The jump sets the TIMER, not a velocity — apex then lands at ~v0
            // seconds (0.285 -> tick 17), which is what makes it a time and not a
            // speed. No sqrt(2gh): the engine never solves for an apex.
            if (grounded && !pv.jumping && ks[SDL_SCANCODE_SPACE]) begin_jump(pv, pm);
            if (feet.y < room.mesh.bmin.y - D) spawn_player();

            // Facing: map the world movement heading to one of the 8 sprite dirs
            // (screen-relative -- see heading_to_facing). SEN arrival facing uses the
            // same helper, so a warped-in player faces the same way it would if it had
            // walked in that direction.
            if (moving) facing = heading_to_facing(wish.x, wish.z, cam.yaw);
            // Animation comes from the game's OWN script now. We only choose the
            // ACTION (a section id) and feed the VM what it polls; the script decides
            // the sprite file, the tag, the frame and how long to hold it. That last
            // part is why this matters: the game holds each anime_Pattern for its own
            // anime_Wait(n), and attacks run 1-4 ticks/frame against idle's 10 — a 10x
            // spread that no single rate constant can reproduce.
            //
            // Section ids: 0 walk, 1 run/dash, 2 idle, 3 jump. (Jump is the id!=tag
            // case — Yunica's section 3 loops on animTag 10, which is exactly why
            // resolving by tag rather than index matters.)
            // While a swing is active, play the attack section (0x100 = the basic
            // attack, present in C_010/020/030) so the sprite actually swings instead of
            // sliding in the walk pose. Otherwise: jump / walk / idle.
            const std::int32_t want_sec = !grounded ? 3 : (moving ? 0 : 2);
            // Count how long the current animation frame has been held. Active swing
            // frames advance every 1-6 ticks; a recovery hold sits on one frame for
            // 40+, so a stall well past the former marks "the swing is over".
            if (avm.section() >= 0x100) {
                const long fk = (long(avm.anim_tag()) << 20) ^ avm.frame();
                atk_stall = (fk == atk_frame_key) ? atk_stall + 1 : 0;
                atk_frame_key = fk;
                combo_idle = 0;
            } else {
                atk_stall = 0;
                atk_frame_key = -1;
                // Drop the chain back to hit #1 once the player has been out of an attack
                // for the engine's post-attack window: FUN_00465170 increments +0x6fc each
                // tick and, past 10, plays section 0x124 and clears the stage +0x6f0
                // (COMBO_CHAIN.md §2b). The counter and the 10 are [C-DIS]; that this is
                // THE combo window is [H-med].
                if (combo_stage != 0 && ++combo_idle > 10) { combo_stage = 0; combo_idle = 0; }
            }
            // Attack sections hold on their last frame rather than falling off the end,
            // so locomotion takes back over on the script's cancel window OR, for the
            // sections that never open one, once the animation has clearly stalled into
            // its recovery. Below 0x100 the priority rule inside set_section refuses.
            if (avm.section() < 0x100)
                avm.set_section(want_sec);
            else if (avm.cancelable() || atk_stall >= 10)
                avm.set_section(want_sec, /*force*/true);
            avm.set_is_land(grounded);
            avm.tick();
            // Projectile flight. The motion here IS reverse-engineered (speed += accel,
            // gravity applied to the velocity, heading re-derived from it, and the bullet
            // living exactly as long as its effect). The DAMAGE is not: PROJECTILE.md
            // marks the generic bullet->victim path [UNKNOWN] — the only damage calls in
            // the update loop sit behind a branch for five hardcoded parryable ids. The
            // sphere test and formula below are the melee ones reused so Hugo can fight
            // at all. Stand-in, not engine behaviour; revisit if that path is found.
            for (Projectile& p : shots) {
                step_projectile(p);
                // GoXx trail (+0x158 effect every +0x15c ticks, FUN_00594230 0x59565e).
                if (p.trail_period > 0 && p.trail_effect > 0 && p.age % p.trail_period == 0)
                    world.locate_effect(p.trail_effect, p.pos.x, p.pos.y, p.pos.z, 0.0f, 0.0f, "");
                for (Actor& en : world.actors()) {
                    if (en.is_player() || !en.alive() || p.hit.count(en.instance_id)) continue;
                    const float bx = en.pos.x - p.pos.x, bz = en.pos.z - p.pos.z;
                    const float by = (en.pos.y + kCharH * 0.5f) - p.pos.y;
                    const float rr = kCharRadius + 0.3f;
                    if (bx * bx + by * by + bz * bz > rr * rr) continue;
                    p.hit.insert(en.instance_id);
                    AttackStats a;
                    a.atk = player_stats().atk;
                    EnemyDefense vd; vd.def = en.def;
                    for (int k = 0; k < 4; ++k) vd.resist[k] = en.resist[k];
                    // STAND-IN: the bullet's own class/power/knock fields are [UNKNOWN]
                    // (the tag-0x18 path takes its angle from FUN_00591ef0), so it hits
                    // as class-less with the base 0.1 impulse away from the bullet.
                    const int dmg = enemy_incoming_damage(a, kDamageClassNone, vd, PlayerDamageMods{},
                        [&](float pct) { return float(rng_pct(rng)) < pct; });
                    const HitResult r = apply_damage(en.hp, en.flags, dmg);
                    if (dmg > 0) add_boost(300.0f);
                    react_enemy(en, dmg, r.died, 0,
                                knock_impulse(std::atan2(en.pos.z - p.pos.z, en.pos.x - p.pos.x), 0.1f));
                    if (r.died) {
                        award_exp(en.exp_reward);
                        sfx_at(kSfxDeathEntry, en.pos, kSfxAnimVol);
                        for (int id : enemy_death_sounds(asset_root, casts.sprite_stem(en.cast_id)))
                            sfx_at(id, en.pos, kSfxAnimVol);
                    }
                    p.age = p.life;   // die on hit — pierce-vs-not is [UNKNOWN] too
                    break;
                }
            }
            shots.erase(std::remove_if(shots.begin(), shots.end(),
                                       [](const Projectile& p) { return !p.alive(); }),
                        shots.end());

            // anime_FireBullet fired. Arg meanings from build/projectile_re/PROJECTILE.md:
            // [2] forward reach, [3] vertical offset, [4] heading offset in DEGREES added
            // to facing, [5..7] an explicit direction used iff non-zero, [8] speed,
            // [9] acceleration, [10] gravity, [12] scale (negative = sentinel for 1.0).
            spawn_bullets(avm, feet, face_dir, /*from_player*/true);
            // GenerateCollision fired: adopt this section's own box for its own lifetime.
            if (avm.armed_hitbox()) {
                atk_hb = avm.hitbox();
                atk_window = atk_hb.life > 0 ? atk_hb.life : 1;
                atk_dir = face_dir;
                player_hit_serial = next_hit_serial(world.hit_serial_counter_);
            }
            // anime_SePlay3D events (ASO funcid 0x2a): the animation names its own
            // sounds, so the swing lands on the right frame and is per-character for
            // free -- Yunica's attack section issues 139 = player\p1_sw_slash0, and
            // Hugo/Toal issue their own. Only drained here, on the logic tick: the
            // other avm.tick() calls are init/character-switch and must stay silent.
            for (const AsoVm::SoundEvent& se : avm.sounds()) audio.play(se.id, kSfxAnimVol);
            // The script may switch sprite files mid-action (e.g. the attack section
            // swaps C_111 -> C_114). When it does, the image INDEX for a tag can differ
            // between the old and new AIA, so force a re-decode -- otherwise the sprite
            // keeps a stale/mismatched frame and renders half-missing after a swing.
            const bool aia_changed = sync_aia();

            const int wantImg = chr.find_tag(std::uint32_t(avm.anim_tag()));
            if (wantImg >= 0 && (aia_changed || std::uint32_t(wantImg) != curAnim)) {
                curAnim = std::uint32_t(wantImg);
                shownFrame = -1;
            }
            const int nf = int(std::max(1u, chr.frames_of(curAnim)));
            animFrame = nf ? (avm.frame() % nf) : 0;
            if (animFrame != shownFrame) { chr.set_anim(g, curAnim, animFrame); shownFrame = animFrame; }

            // The room's authored camera (SCM): region-selected, follow + orbit, blended.
            update_camera(room, feet, dt);
            if (g_cam.on) apply_cam_state(cam, feet);
        }

        // SEN trigger: move into a doorway volume to warp. Test the PLAYER only, exactly
        // as the engine does (FUN_0042cf00 tests actor+0x50) -- the old code also tested
        // the camera, and the follow camera sits ~24u behind and above the player, so it
        // entered the door volume well before the player did and warped too early. In fly
        // mode there is no player, so the probe is the camera itself. EDGE-triggered: the
        // warp fires only on ENTERING a volume (the engine arms on entry), which is also
        // what lets a door-return spawn the player standing in the doorway without an
        // instant re-warp -- inside_trigger is primed to that door at spawn.
        {
            const Vec3 probe = fly ? cam.pos : Vec3{feet.x, feet.y + kCharH * 0.5f, feet.z};
            int now_in = -1;
            for (std::size_t i = 0; i < room.sen.triggers.size(); ++i)
                if (room.sen.triggers[i].contains(probe)) { now_in = int(i); break; }
            if (now_in >= 0 && now_in != inside_trigger && cooldown <= 0) {
                const SenTrigger& t = room.sen.triggers[std::size_t(now_in)];
                transition(t.dest_scene, &t);   // re-primes inside_trigger for the new room
            } else {
                inside_trigger = now_in;
            }
        }
        }  // ---- end fixed-step logic ----------------------------------------
        // Barrier fight: if the last group-1 seed died during this frame's ticks, grant
        // the barrier's flag and re-gate the room so its chest unlocks (no-op otherwise).
        clear_barrier();
        // UV scroll — the water. `phase += velocity` ONCE PER TICK, unscaled: the
        // disassembly is unambiguous (FUN_0044cda0 @0x44e165 — the neighbouring
        // fields ARE MULSS-scaled by a delta, the scroll block has no MULSS at all).
        // Applying `ticks * velocity` once here is exactly equivalent, since the
        // accumulation is linear. The engine uploads c1.xy = baseTexcoordOffset +
        // phase; we bake the phase into the batch's UVs, which needs no per-draw
        // uniform and keeps the batching intact.
        if (ticks && !room.scrolls.empty() && !std::getenv("CLERIA_NOSCROLL"))
            advance_scroll(g, room, ticks);
        // Presentation advances by the time we actually SIMULATED this frame, not by
        // wall time — so fog blends, flame flipbooks and HUD bars stay locked to the
        // tick and slow down with timeScale, instead of drifting off it.
        const float dt = float(ticks) * kTickDt;
        // The exe keeps separate FPS and TPS counters (0x767c34 / 0x767c3c, both
        // refreshed every 1000 ms) — proof that ticks are not frames. CLERIA_TPS=1
        // prints ours, which is how we check the decoupling actually holds.
        if (std::getenv("CLERIA_TPS")) {
            static int tps = 0, fps = 0; static Uint64 t0 = SDL_GetTicks();
            tps += ticks; ++fps;
            if (now - t0 >= 1000) {
                std::printf("  %d fps / %d tps  (timeScale %.3f)\n", fps, tps, g_time_scale);
                tps = fps = 0; t0 = now;
            }
        }

        WGPUSurfaceTexture st = {};
        wgpuSurfaceGetCurrentTexture(surface, &st);
        if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) { configure(); continue; }
        WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
        const float rad = room_diag(room);
        update_fog(room, fly ? cam.pos : feet, dt);
        last_vp = cam.view_proj(float(cw) / float(ch), rad * 0.01f, rad * 8.0f);
        have_vp = true;
        write_mvp(g, last_vp);
        draw(g, room, view, depth);
        // Character billboard (play mode only; fly is a free debug camera). Draw the floor
        // shadow first, on the actual floor under the player (so it stays put during a jump).
        if (!fly && facing >= 0 && facing < 8 && chr.dirs[facing].tex) {
            const float sfl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + 0.5f);
            draw_shadow(g, view, depth, shadows.tex, feet,
                        (sfl > kNoFloor ? sfl : feet.y), 0.6f);
            draw_character(g, view, depth, feet, kCharH, chr.dirs[facing], cam.forward(), cam.pos);
        }
        draw_enemies(g, view, depth, efx, render, cam.forward(), cam.pos, fx_time, shadows.tex);
        fx_time += dt;
        draw_flames(g, room, view, depth, flames, cam.forward(), fx_time);
        draw_projectiles(g, shots, asset_root, efl, eft, view, depth, cam.forward());
        // Script-placed persistent effects (the barrier ward). Killed by op 0x119 when
        // the barrier breaks, so it renders only while the fight is unresolved.
        draw_located_effects(g, world.effects(), asset_root, efl, eft, view, depth,
                             cam.forward(), fx_time);
        draw_particles(g, fxsim, asset_root, eft, view, depth, cam.forward());
        player.character = character;
        // EXP bar: progress from this level's threshold to the next (100% at the cap).
        if (ginfo.ok()) {
            const double e0 = ginfo.exp_to_level(player.level);
            const double e1 = ginfo.exp_to_level(player.level + 1);
            player.exp_pct = (player.level >= 60) ? 100.0f
                : float(std::clamp((double(player.exp) - e0) / std::max(1.0, e1 - e0), 0.0, 1.0) * 100.0);
        }
        // Screen fader (op 0xc8/0xc9/0xca/0xcc): the HUD draws a fullscreen solid-color
        // quad at the fader's current alpha, over the scene+HUD but UNDER the dialogue
        // box/caption (drawn below) so a partial crossfade keeps text readable while a
        // full fade blacks the scene out. set_fade routes it into the HUD's alpha batch.
        hud.set_fade(csHost.fade_rgb, csHost.fade_cur);
        // Bind the HUD's four equipment slots (FUN_0043fe00): weapon, armor, foot (flags
        // 151..153) and the accessory (0x76a628; 0xff = none). [not modelled] the dimming of a
        // used-up item and the Lv badges (INVENTORY.md §7).
        {
            const int acc = (inv.accessory == 0xff) ? -1 : inv.accessory;
            const int ids[4] = {game.g_flags[151], game.g_flags[152], game.g_flags[153], acc};
            hud.set_equip(g, items_tbl, asset_root, ids);
        }
        hud.render(g, view, cw, ch, room.name + "  (" + kCharNames[character] + ")", dt, &player);
        // Standing bust portraits (op 0xe3 Portrait_Open): IMAGE/<name>.dds sprites drawn in the
        // 2D overlay, behind the dialogue box. Anchor is bottom-left in the authored 1024x768
        // space; the 1024x1024 DDS is blitted 1:1 (its alpha channel masks the character art).
        if (!csHost.busts.empty() && pauseOk) {
            write_mvp(g, Mat4::identity());
            WGPUTextureView bdepth = make_depth(g, cw, ch);
            const float sw = float(cw), sh = float(ch);
            const float A = sh / 768.0f, xoff = (sw - 1024.0f * A) * 0.5f;
            auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
            auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
            for (const auto& b : csHost.busts) {
                auto it = bustTex.find(b.name);
                if (it == bustTex.end()) {
                    std::string nm = b.name;
                    for (char& c : nm) c = char(std::toupper((unsigned char)c));
                    Dds d = load_dds(asset_root / "IMAGE" / (nm + ".DDS"));
                    SpriteTex t = d.ok() ? upload_rgba(g, d.width, d.height, d.data.data()) : SpriteTex{};
                    it = bustTex.emplace(b.name, t).first;
                }
                const SpriteTex& t = it->second;
                if (!t.tex) continue;
                const float left = xoff + b.x * A, bottom = b.y * A;
                const float w = float(t.w) * A, h = float(t.h) * A, top = bottom - h;
                draw_screen_image(g, view, bdepth, false, t, NX(left), NY(top), NX(left + w), NY(bottom),
                                  0.09f, 0, 0, 1, 1, 0, 0, 0, 0, false, 1, 1, 1);
            }
            wgpuTextureViewRelease(bdepth);
        }
        // Cutscene dialogue box over the frozen world (FACEWIN0 bottom bar + name + text).
        if (csHost.acquire_active && pauseOk) {
            // op 0xd5 Window_Treasure: the centred item-get box with the item icon (NOT the
            // running-dialogue box). Position/name come from the op; icon from the item table.
            write_mvp(g, Mat4::identity());
            WGPUTextureView ddepth = make_depth(g, cw, ch);
            const SpriteTex* ic = pauseUi.icon(g, items_tbl, asset_root, csHost.acquire_id);
            pauseUi.draw_item_get(g, pmf, view, ddepth, cw, ch, csHost.acquire_x, csHost.acquire_y,
                                  csHost.text, ic);
            wgpuTextureViewRelease(ddepth);
        } else if (csHost.box_active && pauseOk) {
            write_mvp(g, Mat4::identity());   // the box is screen-space
            WGPUTextureView ddepth = make_depth(g, cw, ch);
            pauseUi.draw_dialogue(g, pmf, view, ddepth, cw, ch, csHost.name, csHost.text, csHost.reveal,
                                  portrait_tex(), csHost.text_r, csHost.text_g, csHost.text_b);
            wgpuTextureViewRelease(ddepth);
        }
        // Area-name caption card (op 0xe2 = pre-rendered IMAGE/<name>.dds), fading out.
        if (csHost.caption_timer > 0) {
            if (csHost.caption_name != captionLoaded) {
                std::string nm = csHost.caption_name;
                for (char& c : nm) c = char(std::toupper((unsigned char)c));
                Dds cd = load_dds(asset_root / "IMAGE" / (nm + ".DDS"));
                if (cd.ok()) { captionTex = upload_rgba(g, cd.width, cd.height, cd.data.data()); }
                captionLoaded = csHost.caption_name;
            }
            if (captionTex.tex) {
                write_mvp(g, Mat4::identity());
                WGPUTextureView cdepth = make_depth(g, cw, ch);
                const float sw = float(cw), sh = float(ch);
                // Upper-LEFT area-name card (user-corrected: the RE's "centred-upper"
                // was a guess). Sized to the 4:3 content region so it doesn't stretch on
                // widescreen; anchored near the top-left corner. [HAND-TUNED vs retail.]
                const float A = sh / 768.0f, xoff = (sw - 1024.0f * A) * 0.5f;
                const float cw2 = 384.0f * A, chh = cw2 * float(captionTex.h) / float(captionTex.w);
                const float cx = xoff + 40.0f * A, cy = 48.0f * A;
                const float a = std::min(1.0f, csHost.caption_timer / 45.0f);   // fade over the last 45f
                auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
                auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
                draw_screen_image(g, view, cdepth, false, captionTex, NX(cx), NY(cy), NX(cx + cw2), NY(cy + chh),
                                  0.1f, 0, 0, 1, 1, 0.02f, 0.02f, 0.05f, 0.0f, false, a, a, a);
                wgpuTextureViewRelease(cdepth);
            }
            --csHost.caption_timer;
        }
        // The pause book over the frozen game (BACK00 is opaque; the pillars cover the rest).
        if (paused && bookOk && book.is_open()) {
            DrawList dl;
            book.draw(dl, book_model());
            bookR.render(g, view, cw, ch, dl);
        }
        // The goddess-statue menu, over the frozen game (STATUE_MENU.md flow).
        if (statue_ui && pauseOk) {
            write_mvp(g, Mat4::identity());
            WGPUTextureView pdepth = make_depth(g, cw, ch);
            // The statue menu IS a go_wnddialog popup (WndMenu dump: Window_Menu's
            // window class = go_wnddialog.cpp) — DIALOG0 frame, MENUCURSOR capsule,
            // the traced (512,256) layout. The title types in like the engine's.
            statue_reveal = std::min(statue_reveal + 2, 64);
            char tbuf[40];
            if (statue_ui == 1) {
                static const char* kChoice[3] = {"Save", "Accept divine blessing", "Nothing right now"};
                std::snprintf(tbuf, sizeof tbuf, "%.*s", statue_reveal, "What will you do?");
                pauseUi.draw_popup(g, pmf, view, pdepth, cw, ch, tbuf, kChoice, 3, statue_sel);
            } else {
                // Blessing names + SP costs are the retail-observed table (STATUE_MENU.md).
                // Selecting one only reports "not implemented" — no SP is spent.
                static const char* kBless[15] = {
                    "Strengthen current armor - [SP:]100",
                    "Increase stationary heal rate - [SP:]1000",
                    "Reduce damage from terrain - [SP:]4000",
                    "Increase stun effect - [SP:]8000",
                    "Increase SP gain - [SP:]16000",
                    "Increase item drop rate - [SP:]24000",
                    "Reduce bad status duration LV1 - [SP:]1500",
                    "Increase effect of herbs LV1 - [SP:]2000",
                    "Bolster bad status resists LV1 - [SP:]2500",
                    "Extend drop item effects LV1 - [SP:]3000",
                    "Increase Boost recovery LV1 - [SP:]10000",
                    "Hasten Skill recovery LV1 - [SP:]20000",
                    "Reduce MP usage LV1 - [SP:]30000",
                    "Increase movement speed - [SP:]500000",
                    "Nothing right now"};
                std::snprintf(tbuf, sizeof tbuf, "%.*s", statue_reveal, "Please select a blessing.");
                // 15 rows: anchor the caption near the top so the list fits (authored
                // ~72, matching statue_blessings.png's window top).
                pauseUi.draw_popup(g, pmf, view, pdepth, cw, ch, tbuf, kBless, 15, statue_sel,
                                   false, 72.0f);
            }
            wgpuTextureViewRelease(pdepth);
        }
        wgpuSurfacePresent(surface);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(st.texture);
    }
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

