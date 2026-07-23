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
        int lsel = 0;
        MenuFont mf;   // Felghana atlas rasterised at the 960-internal font size (see render_title),
        // bilinear-upscaled to the window. Format must match the window surface (fmt), not a
        // hardcoded RGBA8Unorm, or the pipeline is incompatible with an sRGB surface pass.
        mf.init(g, fmt, find_felghana(asset_root), int(std::lround(32.0 * 960.0 / 768.0)));
        WGPUTextureView tdepth = make_depth(g.device, cw, ch);
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
                        else if (ok && !tslots[std::size_t(lsel)].empty()) {
                            std::ifstream f(slot_path(lsel), std::ios::binary);
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
                                for (int i = 0; i < 8; ++i) tslots[std::size_t(i)] = slot_label(i);
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
                        configure(); wgpuTextureViewRelease(tdepth); tdepth = make_depth(g.device, cw, ch);
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
                title.draw_svld(g, mf, view, tdepth, cw, ch, /*save=*/false, 0, lsel, tslots.data());
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
    // Projectiles. The effect registries resolve a bullet's eftlist id to its real
    // EF2 and texture; without them bullets simply do not spawn (rather than draw
    // something invented).
    EftList efl;
    EftTex eft;
    if (!efl.load(asset_root) || !eft.load(asset_root))
        std::printf("effects: EFTLIST/EFTTEX missing — projectiles disabled\n");
    std::vector<Projectile> shots;
    // The live hit-volume, copied from the script when GenerateCollision arms it.
    // Defaults are Yunica's combo #1 so a script that never arms behaves as before.
    AsoVm::Hitbox atk_hb{0.6f, 0.0f, 30, 8.0f};
    Vec3 atk_dir{0, 0, 1};               // the swing's facing (captured at swing start)
    std::set<int> atk_hit;               // instance ids already hit this swing (dedup)
    Vec3 face_dir{0, 0, 1};              // world dir the player faces (from movement)
    WGPUTextureView depth = make_depth(g.device, cw, ch);

    // `character` was chosen above (CLERIA_CHAR override or the title's character-select).
    XsoVm game;                        // persistent game state (survives rooms)
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
    if (ginfo.ok())
        player.max_hp = player.hp = int(std::lround(ginfo.curves[kPlayerMaxHp].at(player.level)));
    int player_iframes = 0;            // ticks of invulnerability after a hit (+0x734)
    // Loaded game (title Load screen): flags + character must be right BEFORE the first
    // room load (chest gating reads them); stats/position are applied after the world
    // exists, below. This mirrors the engine's load = init-then-overwrite (SAVELOAD §4.2).
    fs::path boot_sob = first_sob;
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
            const int old_max = player.max_hp;
            ++player.level;
            player.max_hp = int(std::lround(ginfo.curves[kPlayerMaxHp].at(player.level)));
            player.hp += (player.max_hp - old_max);
            hud.message("Level up! Lv " + std::to_string(player.level));
        }
    };
    Room room = load_room(g, asset_root, boot_sob, game.g_flags, character);
    Camera cam;
    bool fly = false;
    Vec3 feet{}; float vy = 0; bool grounded = false; float speed_mult = 1.0f;

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
    // ** kCharH is NOT pm.height(), and that is a deliberate refusal. **
    //
    // The disassembly says the engine derives a player height of `+0x38 + 0.075` =
    // 1.075 and a radius of `+0x34 * 0.9` = 0.675. Both were verified against the file.
    // But using 1.075 as the character's WORLD height contradicts two independent
    // measurements from the game's own data:
    //
    //   * S_1000's authored SCM cameras sit 18..32 units back. At 1.075 tall the
    //     camera is 17..30x the character's height away — no 2.5D action game frames
    //     itself like that. At 2.0 it is 9..16x, which is what Ys Origin looks like.
    //   * The engine's own jump model peaks at 2.46 units. That is 1.2x a 2.0-tall
    //     character (right) but 2.3x a 1.075-tall one (far too high). Note our
    //     hand-tuned kJumpH was 2.3 — it agrees with the engine's apex to 7%, which is
    //     what says the vertical model below IS in these units.
    //
    // So one of three things is true and we have not established which: the derivation
    // is wrong; +0x38 is a scale rather than a length; or the collision capsule is
    // simply not the sprite's size and this code conflates them. Until that is settled,
    // the sprite keeps the number that matches the game's framing, marked INVENTED —
    // shipping a "validated" 1.075 that visibly renders a doll would be trading a
    // working guess for a sourced mistake.
    const float kCharH = 2.0f;               // INVENTED — see above
    const float kCharRadius = 0.45f;         // INVENTED — pairs with kCharH, not pm.radius()
    const float kJumpV0 = pm.jump_v0;        // 0.285 — CASTINFO +0x50 [C-DIS]
    const float kStepUp = 0.6f;              // INVENTED — no source found
    // INVENTED: walk speed. The engine does `actor+0x30c * scale * X` and X resolves
    // to +0xdc on an object we have not identified, so the real value is unknown. It
    // is deliberately NOT back-derived from the 20/13 footstep cadence: that would
    // look right, match the audio, and be unfalsifiable.
    const float kWalkSpeed = 10.0f;   // units/s
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
        vy = 0; grounded = false;
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
                    bool arms = false;
                    for (int t = 0; t < 150 && !arms; ++t) {
                        probe.tick();
                        if (probe.armed_hitbox()) arms = true;
                    }
                    if (arms && s.type > best_type) { best_type = s.type; sec = s.id; }
                }
            enemy_atk_section[stem] = sec;
        }
        return it->second.ok() ? &it->second : nullptr;
    };
    // Bind each freshly-built enemy to its script + attack section, and start it in idle.
    auto bind_enemy_aso = [&] {
        for (EnemyRender& e : render) {
            Aso* a = enemy_script(e.stem);
            if (!a) continue;
            e.avm.bind(a);
            // Resting section: 2 (idle for most), else 0, else whatever exists first —
            // set_section is what makes AsoVm::ok() true, so it must land on a real one.
            int idle = a->find(2) ? 2 : (a->find(0) ? 0 : a->sections().front().id);
            e.idle_section = idle;
            e.avm.set_section(idle, /*force*/true);
            e.atk_section = enemy_atk_section[e.stem];
        }
    };

    // Barrier fight (kekkai). A room's KEKKAIZAKO_*.XSO polls Chr_IsSeedLiving(1) and,
    // when the last group-1 seed enemy dies, grants the progress flag its chest guard
    // tests (EVENTVM.md §6: S_1001 -> flag 301, which S_BOX01 requires before it opens).
    std::vector<Xso> barrier_scripts;        // this room's KEKKAIZAKO_*.XSO (the poll + grant)
    std::vector<Xso> barrier_break_scripts;  // KEKKAIBREAK_*.XSO (Eft_Kill the ward + camera)
    bool barrier_seeded = false;        // a group-1 seed spawned this visit
    bool barrier_cleared = false;       // its flag has already been granted this visit
    bool barrier_break_pending = false; // grant done; the main loop must play KEKKAIBREAK
    bool room_events_dirty = true;      // (re)run the room's event scripts on the manager

    auto spawn_enemies = [&] {
        // Populate the world the engine's way: run S_xxxx.XSO (op 0xa0 -> SCT bulk,
        // op 0x12c -> scripted), seeded with the persistent game flags. Then load a
        // sprite for every spawned cast (incl. 0x12c hub NPCs the SCT never named) and
        // pair them. Stats come from ONE path (ActorWorld::spawn): SCT name / cast_type
        // -> CASTINFO -> enemy_index -> ENEMYINFO -> GAMEINFO. Real names = CASTINFO+0x10.
        populate_room(world, room, game.g_flags);
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
                for (const EnemyRender& e : render)
                    std::printf("    %-14s (%-6s) lv%-3d HP %6.0f  ATK %5.0f  DEF %5.0f\n",
                                e.display.c_str(), e.stem.c_str(), e.actor->level,
                                e.actor->max_hp, e.actor->atk, e.actor->def);
        }
    };

    spawn_enemies();

    apply_room_bgm(room.name);

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
        Room next = load_room(g, asset_root, it->second, game.g_flags, character);
        free_room(room, g.white_bind);
        room = std::move(next);
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
                vy = 0; grounded = false;
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
    auto reload_room = [&] {
        auto it = g.sob_index.find(room.name);
        if (it == g.sob_index.end()) return;
        Room next = load_room(g, asset_root, it->second, game.g_flags, character);
        free_room(room, g.white_bind);
        room = std::move(next);
        room_events_dirty = true;   // re-run the room's event scripts for the re-gated room
    };

    // ---- save / load (PLAN.md Phase 2; codec src/cleria/save.*) -------------------
    // Pause-book Save/Load page state: slot cursor + cached slot lines (refreshed on
    // page entry and after a save — not per frame, that would be 8 file reads a frame).
    int svldSlot = 0;
    std::array<std::string, 8> svldLabels;
    auto refresh_slots = [&] { for (int i = 0; i < 8; ++i) svldLabels[std::size_t(i)] = slot_label(i); };
    auto capture_save = [&]() -> SaveState {
        SaveState s;
        std::memcpy(s.flags, game.g_flags, sizeof s.flags);
        std::memcpy(s.item_arr, game.item_arr, sizeof s.item_arr);
        s.hp_max = float(player.max_hp); s.hp_cur = float(player.hp);
        s.exp = player.exp; s.level = player.level; s.sp = player.sp;
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
        refresh_slots();
    };
    auto apply_loaded = [&](const SaveState& s) {
        std::memcpy(game.g_flags, s.flags, sizeof s.flags);
        std::memcpy(game.item_arr, s.item_arr, sizeof s.item_arr);
        character = std::clamp(s.character(), 1, 3);
        player.level = std::clamp(s.level, 1, 60);
        player.exp = s.exp; player.sp = s.sp;
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
        vy = 0;
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
        void window_msg(int sp, int, const std::string& nm, const std::string& tx) override {
            speaker = sp; name = nm; text = tx; reveal = 0; box_active = true;
        }
        void window_close() override { box_active = false; name.clear(); text.clear(); }
        std::string caption_name; int caption_timer = 0;   // 0xe2 area-name card (frames left)
        void caption(const std::string& nm) override { caption_name = nm; caption_timer = 210; }
        bool wait_prompt() override {
            if (reveal < int(text.size())) return false;    // line still typing out
            if (advance) { advance = false; return true; }  // one press advances one line
            return false;
        }
        // 0xf4 Wait_Script(name): a parent controller blocks until its spawned talk child
        // is gone. Answer from the live set (case-insensitive; script names vary in case).
        bool wait_script_done(const std::string& n) override {
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
        // Effect ops forward to the world so a cutscene's Eft_Kill / Eft_LocatePosAddStr
        // really adds/removes the ward from world.effects() (the barrier break-pan).
        XsoHost* worldHost = nullptr;
        void kill_effect(const std::string& n) override { if (worldHost) worldHost->kill_effect(n); }
        void locate_effect(int id, float x, float y, float z, float a, float b,
                           const std::string& n) override {
            if (worldHost) worldHost->locate_effect(id, x, y, z, a, b, n);
        }
        void spawn(int, int, float, float, float, float, int) override {}
        bool is_alive(int) const override { return false; }
        bool any_seed_alive(int) const override { return false; }
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
        std::shared_ptr<const Xso> load_script(const std::string& name) override {
            std::string up = name;
            for (char& c : up) c = char(std::toupper((unsigned char)c));
            const fs::path p = *dir / (up + ".XSO");
            std::error_code ec;
            if (!fs::exists(p, ec)) return nullptr;    // engine would throw; we skip
            try { return std::make_shared<Xso>(Xso::parse(read_bytes(p))); }
            catch (const std::exception&) { return nullptr; }
        }
    } csLoader;
    csLoader.dir = &room.dir;
    ScriptManager csMgr;
    csMgr.vm.host = &csHost;
    csMgr.loader = &csLoader;
    csHost.mgr = &csMgr;
    csHost.worldHost = &world;
    csHost.roomPtr = &room;         // op 0x13d tests the player vs the current room's .SEV boxes
    csHost.playerPos = &feet;
    bool prev_frozen = false;
    std::function<void()> after_cutscene;   // ran once when the world thaws (e.g. re-gate the room)
    SpriteTex captionTex; std::string captionLoaded;   // cached area-name card (op 0xe2)

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
    const bool events_enabled = std::getenv("CLERIA_EVENTS") != nullptr;
    auto start_room_events = [&] {
        csMgr.live.clear(); csMgr.spawn_queue.clear(); csMgr.peak_live = 0; csMgr.fix_count = 0;
        csHost.box_active = false; csHost.reveal = 0; csHost.advance = false;
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
        csMgr.tick_all();
        if (csHost.box_active && csHost.reveal < int(csHost.text.size())) csHost.reveal += 2;
        scroll_advance();
        shake_advance();
        const bool frozen = csMgr.world_frozen();
        if (!frozen && prev_frozen && after_cutscene) {    // world just thawed -> queued action
            auto f = after_cutscene; after_cutscene = nullptr; f();
        }
        prev_frozen = frozen;
        if (frozen && g_cam.on) apply_cam_state(cam, feet);
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
    // A character's real combo sections: the ids in [0x100,0x110) that actually arm a
    // hitbox or fire a bullet, ascending. The dud sections in between (Hugo's 257/258/…
    // do neither) must be SKIPPED — chaining to the literal next id lands on one and
    // stalls him after a single shot. Found by running each section briefly, cached per
    // character; the combo just walks this list, wrapping.
    auto combo_sections = [&](int chr) -> const std::vector<int>& {
        static std::unordered_map<int, std::vector<int>> cache;
        auto it = cache.find(chr);
        if (it != cache.end()) return it->second;
        std::vector<int>& out = cache[chr];
        for (const Aso::Section& s : aso.sections()) {
            if (s.id < 0x100 || s.id >= 0x110) continue;
            AsoVm probe;
            probe.bind(&aso);
            if (!probe.set_section(s.id, /*force*/true)) continue;
            bool real = false;
            for (int t = 0; t < 150 && !real; ++t) {
                probe.tick();
                if (probe.armed_hitbox() || !probe.bullets().empty()) real = true;
            }
            if (real) out.push_back(s.id);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    auto try_attack = [&] {
        const bool swinging = avm.section() >= 0x100;
        if (swinging && !avm.cancelable()) return;   // committed to the current swing
        const std::vector<int>& combo = combo_sections(character);
        if (combo.empty()) return;
        std::int32_t next = combo.front();
        if (swinging) {
            // Advance to the next real section after the current one, wrapping. For Hugo
            // that is 0x100->0x103->0x106->…, a bullet on each; for Yunica/Toal the melee
            // combo. This is NOT the game's authored combo order (which lives in the
            // script's own AttackReserve branches, not run here) — but each step is a
            // genuine attack, and the recovery-stall release keeps it from ever sticking.
            auto p = std::upper_bound(combo.begin(), combo.end(), int(avm.section()));
            next = (p != combo.end()) ? *p : combo.front();
        }
        if (!avm.set_section(next, /*force*/true)) return;
        atk_dir = face_dir;         // the swing points where you're facing
        atk_hit.clear();
    };

    const ItemTable items_tbl = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
    auto try_interact = [&] {
        const Vec3 p = fly ? cam.pos : Vec3{feet.x, feet.y, feet.z};
        const float reach = room_diag(room) * 0.09f;
        const Interactable* target = nullptr;
        float best = reach * reach;
        for (const Interactable& it : room.mesh.interactables) {
            const float dx = it.position.x - p.x, dy = it.position.y - p.y, dz = it.position.z - p.z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best) { best = d2; target = &it; }
        }
        if (!target) { std::printf("  (nothing to interact with here)\n"); return; }
        if (target->script.empty()) { std::printf("  %s: no script\n", target->name.c_str()); return; }
        try {
            Xso x = Xso::parse(read_bytes(target->script));
            game.reset_run();
            game.run(x);
            // Prefer the game's acquire-box items (0xD5); fall back to give-item.
            const std::vector<int>& got = !game.acquires.empty() ? game.acquires : game.items;
            if (got.empty()) {
                // No item can mean two different things, and they are NOT the same:
                // the chest's opened-flag is set (truly already looted), or the script
                // hit a later prerequisite guard and ended early. S_1001's box_01, for
                // instance, tests a second flag (301) that some event must set first —
                // reporting that as "already opened" is simply wrong.
                const bool opened = target->opened_flag >= 0 &&
                                    game.g_flags[target->opened_flag] != 0;
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
    MenuFont pmf;
    pmf.init(g, fmt, find_felghana(asset_root), int(std::lround(32.0 * 960.0 / 768.0)));
    const ItemTable pitems = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
    bool paused = false;
    int pausePage = 0;  // pause-book tab: 0 Equip, 1 Item, 2 Topic, 3 Save/Load (Q/E cycle)
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

    while (running) {
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
                // While the world is frozen by a cutscene, a confirm key advances the dialogue
                // and other gameplay input is swallowed (the sim is skipped anyway).
                if (csMgr.world_frozen()) {
                    if (k == SDLK_SPACE || k == SDLK_RETURN || k == SDLK_E || k == SDLK_Z)
                        csHost.advance = true;
                }
                // T = trigger a test cutscene (S_1000's Roy/Ramona talk) — debug.
                else if (k == SDLK_T) start_cutscene(asset_root / "MAP" / "S_10" / "S_1000" / "TALKC210.XSO", /*debug235*/true);
                else if (k == SDLK_ESCAPE) {
                    paused = !paused;  // toggle pause; NEVER quit (X quits)
                    if (paused && pausePage == 3) refresh_slots();
                }
                // Save/Load page (pause tab 3): Up/Down slot, Enter saves, L loads.
                // Placed BEFORE the debug K/L handlers so L means "load" here.
                else if (paused && pausePage == 3 && (k == SDLK_UP || k == SDLK_W))
                    svldSlot = (svldSlot + 7) % 8;
                else if (paused && pausePage == 3 && (k == SDLK_DOWN || k == SDLK_S))
                    svldSlot = (svldSlot + 1) % 8;
                else if (paused && pausePage == 3 && (k == SDLK_RETURN || k == SDLK_SPACE))
                    do_save(svldSlot);
                else if (paused && pausePage == 3 && k == SDLK_L) {
                    std::ifstream f(slot_path(svldSlot), std::ios::binary);
                    std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(f), {}};
                    if (auto r = SaveRecord::decode(raw)) {
                        apply_loaded(from_record(*r));
                        paused = false;
                        hud.message("Game loaded.");
                    } else hud.message("No data in slot " + std::to_string(svldSlot + 1));
                }
                else if (k == SDLK_F) {
                    fly = !fly;
                    if (!fly) {  // dropping into walk: keep XZ, snap to floor
                        feet = {cam.pos.x, cam.pos.y, cam.pos.z};
                        const float fl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + room_diag(room));
                        if (fl > kNoFloor) feet.y = fl;
                        vy = 0;
                    }
                    std::printf("mode: %s\n", fly ? "fly" : "walk");
                }
                else if (k == SDLK_R) { if (fly) spawn_in(cam, room); else spawn_player(); }
                // Debug HP for testing the red damage-lag trail (feat: HUD_HP_BARS.md H3):
                // K = take 60 damage (watch the red ghost linger + drain), L = heal to full.
                else if (k == SDLK_K) { player.hp = std::max(1, player.hp - 60); hud.message("(debug) -60 HP"); }
                else if (k == SDLK_L) { player.hp = player.max_hp; hud.message("(debug) full heal"); }
                // Pause-book tab cycling (only while the menu is open): E = next, Q = prev page.
                else if (paused && k == SDLK_E) {
                    pausePage = (pausePage + 1) % 4;
                    if (pausePage == 3) refresh_slots();
                }
                else if (paused && k == SDLK_Q) {
                    pausePage = (pausePage + 3) % 4;
                    if (pausePage == 3) refresh_slots();
                }
                // Debug: B fills the EX/Boost gauge in ~4 presses (to eyeball the boost arc).
                else if (k == SDLK_B) { player.boost = std::min(30000.f, player.boost + 7500.f); hud.message("(debug) +Boost"); }
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
                if (cw && ch) { configure(); wgpuTextureViewRelease(depth); depth = make_depth(g.device, cw, ch); }
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
        if (paused) accum = 0;  // freeze the game while the pause menu is open (no unpause burst)
        int ticks = 0;
        while (accum >= kTickDt) {
        accum -= kTickDt;
        ++ticks;
        // Tick the room's event scripts every frame (controllers poll here). If a Fix is held
        // (a talk / cutscene) the world is frozen: skip the player/enemy sim for this tick (the
        // camera still runs inside tick_events).
        if (tick_events()) continue;
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
            const float walk = kWalkSpeed * speed_mult * avm.movable(), stepUp = kStepUp;
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
            if (moving) {
                feet.x += wish.x / wl * walk * dt; feet.z += wish.z / wl * walk * dt;
                if (avm.turnable())
                    face_dir = {wish.x / wl, 0, wish.z / wl};  // the swing faces where you walk
            }
            { Vec3 body = {feet.x, feet.y + stepUp, feet.z};
              body = resolve_walls(phys_mesh(room), body, radius, std::max(eye - stepUp, 0.5f));
              feet.x = body.x; feet.z = body.z; }
            // The engine's vertical model, not ours. `vy` is NOT a velocity: it is the
            // signed TIME at actor+0x330, decremented ~1/60 per tick, and the Y delta
            // is closed-form. See cleria/physics_player.hpp — reproduce it literally,
            // it is non-physical but tuned.
            float dy = 0.5f * kGravity * vy * std::fabs(vy);  // v*|v| — the sign carries
            dy = std::fmax(dy, kTerminalFall);
            feet.y += dy;
            vy -= kTimerStep;
            if (vy < kTimerFloor) vy = kTimerFloor;
            int floor_tri = -1;
            const float fl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + stepUp, &floor_tri);
            if (fl > kNoFloor && dy <= 0.0f && fl >= feet.y - stepUp) {
                feet.y = fl; vy = 0; grounded = true;
            } else grounded = false;

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
            // Enemy brains, BEFORE the chase mover so move_scale gates this frame's step.
            // Each enemy runs its own ASO: chase until in range, then commit to its attack
            // section (the one that arms a hit-volume), plant for the swing, and return to
            // idle when the animation stalls into recovery. This replaces the old
            // contact-damage stand-in with the real animated attack + scripted hitbox.
            constexpr float kEnemyAtkReach = 1.4f;   // wind-up trigger distance (+ radius)
            constexpr int kEnemyAtkCooldown = 40;    // ticks between an enemy's swings
            for (EnemyRender& e : render) {
                Actor* en = e.actor;
                if (!en || !en->alive() || !e.avm.ok()) continue;
                // Hitstun: a landed player hit interrupts the enemy. Drop out of the attack
                // section back to idle, clear the swing state, and hold (no chase/attack)
                // until the stun ends — so the swing visibly STOPS instead of playing on.
                if (en->hitstun > 0) {
                    --en->hitstun;
                    if (e.avm.section() == e.atk_section) {
                        e.avm.set_section(e.idle_section, /*force*/true);
                        e.atk_win = 0; e.atk_armed = false; e.atk_age = 0;
                        e.atk_cd = kEnemyAtkCooldown;   // no instant retaliation
                    }
                    en->move_scale = 0.0f;              // planted (knockback still slides it)
                    e.avm.set_is_land(true);
                    e.avm.tick();
                    continue;
                }
                if (e.atk_cd > 0) --e.atk_cd;
                const float dx = feet.x - en->pos.x, dz = feet.z - en->pos.z;
                const float dist2 = dx * dx + dz * dz;
                const bool attacking = e.atk_section >= 0 && e.avm.section() == e.atk_section;
                if (!attacking) {
                    const float reach = kCharRadius + kEnemyAtkReach;
                    if (e.atk_section >= 0 && e.atk_cd <= 0 && dist2 < reach * reach &&
                        e.avm.set_section(e.atk_section, /*force*/true)) {
                        const float dl = std::sqrt(dist2);
                        e.atk_dx = dl > 1e-4f ? dx / dl : std::sin(en->yaw);
                        e.atk_dz = dl > 1e-4f ? dz / dl : std::cos(en->yaw);
                        e.atk_hit = false;
                        e.atk_stall = 0;
                        e.atk_fkey = -1;
                        e.atk_armed = false;
                        e.atk_age = 0;
                        en->move_scale = 0.0f;
                    } else {
                        en->move_scale = 1.0f;   // chase
                    }
                } else {
                    en->move_scale = 0.0f;       // planted through the swing
                }
                e.avm.set_is_land(true);
                e.avm.tick();
                // Its GenerateCollision arms the enemy's hit-volume, with the section's own
                // radius/lifetime/power (M_100: frame 26, radius 1.0).
                if (e.avm.armed_hitbox()) {
                    e.hb = e.avm.hitbox();
                    e.atk_win = e.hb.lifetime > 0 ? e.hb.lifetime : 1;
                    e.atk_hit = false;
                    e.atk_armed = true;
                }
                // Release the swing. A wind-up can hold a single frame for many ticks
                // BEFORE the hit fires (M_100 holds frame 2 for 12 ticks, then arms at
                // tick 26), so a bare stall would end the attack before it ever hit —
                // only release once the hit-volume has fired and its window is spent and
                // the animation has stalled into recovery. The age cap is a safety net
                // for a section that somehow never arms.
                if (e.avm.section() == e.atk_section) {
                    ++e.atk_age;
                    const long fk = (long(e.avm.anim_tag()) << 20) ^ e.avm.frame();
                    e.atk_stall = (fk == e.atk_fkey) ? e.atk_stall + 1 : 0;
                    e.atk_fkey = fk;
                    const bool recovered = e.atk_armed && e.atk_win <= 0 && e.atk_stall >= 8;
                    if (recovered || e.atk_age >= 180) {
                        e.avm.set_section(e.idle_section, /*force*/true);
                        e.atk_cd = kEnemyAtkCooldown;
                        en->move_scale = 1.0f;
                    }
                }
            }

            world.step();
            for (Actor& a : world.actors()) {
                if (a.is_player() || !a.alive()) continue;
                if (a.hit_flash > 0) --a.hit_flash;
                // Knockback: add the decaying impulse, then let wall-resolve stop it at
                // walls like any other motion.
                a.pos.x += a.knockback.x; a.pos.z += a.knockback.z;
                a.knockback.x *= 0.75f; a.knockback.z *= 0.75f;
                Vec3 body = {a.pos.x, a.pos.y + stepUp, a.pos.z};
                body = resolve_walls(phys_mesh(room), body, radius, std::max(eye - stepUp, 0.5f));
                a.pos.x = body.x; a.pos.z = body.z;
                const float efl = floor_at(phys_mesh(room), a.pos.x, a.pos.z, a.pos.y + kCharH);
                if (efl > kNoFloor) a.pos.y = efl;
            }

            // The player's melee swing (build/attack_re). While the hit-volume is live,
            // hit every enemy inside the sphere once. The box sphere sits 0.6 ahead in
            // atk_dir with radius 0.6; an enemy is hit if its centre is within
            // (0.6 + enemy radius). Damage uses the game's formula with the ASO's power
            // as the +0xfc bonus (8.0), and marks each enemy hit so a 30-tick window
            // deals one hit per swing, not 30.
            if (atk_window > 0) {
                --atk_window;
                // Radius and power now come from the script's own GenerateCollision
                // rather than Yunica's values hardcoded for everyone.
                const Vec3 c = {feet.x + atk_dir.x * atk_hb.radius, feet.y + kCharH * 0.5f,
                                feet.z + atk_dir.z * atk_hb.radius};
                const float rr = atk_hb.radius + kCharRadius;  // box radius + enemy radius
                int killed = 0;
                for (Actor& en : world.actors()) {
                    if (en.is_player() || !en.alive() || atk_hit.count(en.instance_id)) continue;
                    const float dx = en.pos.x - c.x, dz = en.pos.z - c.z;
                    const float ddy = (en.pos.y + kCharH * 0.5f) - c.y;
                    if (dx * dx + ddy * ddy + dz * dz > rr * rr) continue;
                    atk_hit.insert(en.instance_id);   // the engine's per-attack hit-dedup
                    AttackStats a;
                    a.atk = ginfo.player_atk(character, /*tier*/ 0, player.level);
                    a.bonus = atk_hb.power;          // per-attack power, from this section's script
                    const int dmg = damage_int(a, en.def,
                        [&](float pct) { return float(rng_pct(rng)) < pct; });
                    const HitResult r = apply_damage(en.hp, en.flags, dmg);
                    // Boost builds on hits DEALT too (BOOST.md hook). Amount [H] (see hurt_player).
                    player.boost = std::min(30000.f, player.boost + 1500.f);
                    // Knockback away from the player + a white hit-flash (the engine's
                    // +0x338/+0x340 impulse and +0x4e8 tint). Decayed in the enemy tick.
                    const float kdx = en.pos.x - feet.x, kdz = en.pos.z - feet.z;
                    const float kd = std::sqrt(kdx * kdx + kdz * kdz);
                    if (kd > 1e-3f) en.knockback = {kdx / kd * 0.30f, 0, kdz / kd * 0.30f};
                    en.hit_flash = 6;
                    if (!r.died) en.hitstun = 14;   // interrupt its attack (duration [TUNED])
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
                        // Death entry plays p_hit0 from C for any actor, and the enemy's
                        // own death animation adds its z_dead* pair (SFX_TRIGGERS.md §4.3).
                        audio.play(kSfxDeathEntry, kSfxAnimVol);
                        for (int id : enemy_death_sounds(asset_root, casts.sprite_stem(en.cast_id)))
                            audio.play(id, kSfxAnimVol);
                    }
                }
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
            if (player_iframes > 0) --player_iframes;
            const float pdef = ginfo.curves[kPlayerDef].at(player.level);
            auto hurt_player = [&](float atk) {
                const int dmg = player_incoming_damage(AttackStats{atk, 0, 0}, pdef,
                    [&](float p) { return float(rng_pct(rng)) < p; });
                player.hp -= dmg;
                // Boost builds on hits TAKEN (BOOST.md hook FUN_0046cc10, missing-HP-scaled) —
                // the comeback mechanic. Amount is [H] (tuned for visibility; base const is 25).
                player.boost = std::min(30000.f, player.boost + 2500.f);
                // p_damage is the player's own hurt-reaction sound; death adds p_hit0.
                audio.play(kSfxPlayerHurt, kSfxAnimVol);
                if (player.hp <= 0) audio.play(kSfxDeathEntry, kSfxAnimVol);
                // Player i-frame window (~0.75s) is TUNED, not RE'd; the engine's generic
                // 16-tick grant lets a real-level enemy shred the fresh lv1 player.
                player_iframes = 45;
                hud.message("-" + std::to_string(dmg) + " HP");
                if (player.hp <= 0) {  // down: respawn at full with a grace window
                    player.hp = player.max_hp;
                    spawn_player();
                    player_iframes = 90;
                    hud.message("Down! respawned");
                }
            };
            for (EnemyRender& e : render) {
                Actor* en = e.actor;
                if (e.atk_win <= 0) {
                    // Scriptless enemies (no ASO) keep the old contact check so they are
                    // not harmless; scripted ones deal damage only through their swing.
                    if (!e.avm.ok() && en && en->alive() && en->atk > 0 && player_iframes == 0) {
                        const float mreach = kCharRadius + 0.6f;
                        const float dx = en->pos.x - feet.x, dz = en->pos.z - feet.z;
                        if (dx * dx + dz * dz <= mreach * mreach) hurt_player(en->atk);
                    }
                    continue;
                }
                --e.atk_win;
                if (!en || !en->alive() || en->atk <= 0 || e.atk_hit || player_iframes > 0)
                    continue;
                // The swing's box sits hb.radius ahead of the enemy in its latched heading.
                const float cx = en->pos.x + e.atk_dx * e.hb.radius;
                const float cz = en->pos.z + e.atk_dz * e.hb.radius;
                const float rr = e.hb.radius + kCharRadius;
                const float bx = feet.x - cx, bz = feet.z - cz;
                if (bx * bx + bz * bz > rr * rr) continue;
                e.atk_hit = true;                 // one hit per swing
                hurt_player(en->atk);
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
            if (grounded && ks[SDL_SCANCODE_SPACE]) { vy = kJumpV0; grounded = false; }
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
            } else {
                atk_stall = 0;
                atk_frame_key = -1;
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
                if (!p.from_player) continue;
                for (Actor& en : world.actors()) {
                    if (en.is_player() || !en.alive() || p.hit.count(en.instance_id)) continue;
                    const float bx = en.pos.x - p.pos.x, bz = en.pos.z - p.pos.z;
                    const float by = (en.pos.y + kCharH * 0.5f) - p.pos.y;
                    const float rr = kCharRadius + 0.3f;
                    if (bx * bx + by * by + bz * bz > rr * rr) continue;
                    p.hit.insert(en.instance_id);
                    AttackStats a;
                    a.atk = ginfo.player_atk(character, /*tier*/ 0, player.level);
                    const int dmg = damage_int(a, en.def,
                        [&](float pct) { return float(rng_pct(rng)) < pct; });
                    const HitResult r = apply_damage(en.hp, en.flags, dmg);
                    en.hit_flash = 6;
                    if (r.died) {
                        award_exp(en.exp_reward);
                        audio.play(kSfxDeathEntry, kSfxAnimVol);
                        for (int id : enemy_death_sounds(asset_root, casts.sprite_stem(en.cast_id)))
                            audio.play(id, kSfxAnimVol);
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
            for (const AsoVm::BulletEvent& b : avm.bullets()) {
                Projectile p;
                p.effect_id = b.id;
                const float reach = b.f[0], voff = b.f[1], head_deg = b.f[2];
                const Vec3 d{b.f[3], b.f[4], b.f[5]};
                const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                if (dl > 1e-6f) {
                    p.dir = {d.x / dl, d.y / dl, d.z / dl};
                } else {
                    // Spawn geometry is the engine's x=sin / z=cos, same as GenerateCollision.
                    const float yaw = std::atan2(face_dir.x, face_dir.z) +
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
                p.pos = {feet.x + p.dir.x * reach, feet.y + kCharH * 0.5f + voff,
                         feet.z + p.dir.z * reach};
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
                if (p.speed > 1e-4f) shots.push_back(std::move(p));
            }
            // GenerateCollision fired: adopt this section's own box for its own lifetime.
            if (avm.armed_hitbox()) {
                atk_hb = avm.hitbox();
                atk_window = atk_hb.lifetime > 0 ? atk_hb.lifetime : 1;
                atk_dir = face_dir;
                atk_hit.clear();
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
        write_mvp(g, cam.view_proj(float(cw) / float(ch), rad * 0.01f, rad * 8.0f));
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
        player.character = character;
        // EXP bar: progress from this level's threshold to the next (100% at the cap).
        if (ginfo.ok()) {
            const double e0 = ginfo.exp_to_level(player.level);
            const double e1 = ginfo.exp_to_level(player.level + 1);
            player.exp_pct = (player.level >= 60) ? 100.0f
                : float(std::clamp((double(player.exp) - e0) / std::max(1.0, e1 - e0), 0.0, 1.0) * 100.0);
        }
        hud.render(g, view, cw, ch, room.name + "  (" + kCharNames[character] + ")", dt, &player);
        // Cutscene dialogue box over the frozen world (FACEWIN0 bottom bar + name + text).
        if (csHost.box_active && pauseOk) {
            write_mvp(g, Mat4::identity());   // the box is screen-space
            WGPUTextureView ddepth = make_depth(g.device, cw, ch);
            pauseUi.draw_dialogue(g, pmf, view, ddepth, cw, ch, csHost.name, csHost.text, csHost.reveal);
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
                WGPUTextureView cdepth = make_depth(g.device, cw, ch);
                const float sw = float(cw), sh = float(ch);
                const float cw2 = sw * 0.6f, chh = cw2 * float(captionTex.h) / float(captionTex.w);
                const float cx = (sw - cw2) * 0.5f, cy = sh * 0.16f;
                const float a = std::min(1.0f, csHost.caption_timer / 45.0f);   // fade over the last 45f
                auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
                auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
                draw_screen_image(g, view, cdepth, false, captionTex, NX(cx), NY(cy), NX(cx + cw2), NY(cy + chh),
                                  0.1f, 0, 0, 1, 1, 0.02f, 0.02f, 0.05f, 0.0f, false, a, a, a);
                wgpuTextureViewRelease(cdepth);
            }
            --csHost.caption_timer;
        }
        // Pause menu overlay: the pause-book Equip page over the frozen game (Esc to close).
        if (paused && pauseOk) {
            TitleFx::EquipView ev;
            ev.character = character;
            ev.level = player.level;
            ev.hp = player.hp;
            ev.maxhp = player.max_hp;
            if (ginfo.ok()) {
                ev.str = int(std::lround(ginfo.player_atk(character, 0, player.level)));
                ev.def = int(std::lround(ginfo.curves[kPlayerDef].at(player.level)));
                ev.exp = long(std::llround(ginfo.exp_to_level(player.level)));
                ev.next = long(std::llround(ginfo.exp_to_level(player.level + 1)
                                            - ginfo.exp_to_level(player.level)));
            }
            write_mvp(g, Mat4::identity());  // the pause-book pages are screen-space
            // Fresh depth (like the title-menu path's tdepth) so the world's z-buffer can't
            // occlude the book; only allocated while the pause menu is actually open.
            WGPUTextureView pdepth = make_depth(g.device, cw, ch);
            switch (pausePage) {
                case 1: {  // Item / Inventory
                    static const int owned[] = {1, 48, 8, 78, 79, 116};  // stub inventory
                    ev.caption = "Examine or use an item.";
                    pauseUi.draw_item(g, pmf, view, pdepth, cw, ch, ev, owned, 6, pitems, asset_root);
                    break;
                }
                case 2:  // Topic (Character / Monster / Manual)
                    pauseUi.draw_topic(g, pmf, view, pdepth, cw, ch, 0);
                    break;
                case 3:  // Save / Load (Up/Down slot, Enter = save, L = load)
                    pauseUi.draw_svld(g, pmf, view, pdepth, cw, ch, /*save=*/true, /*fileTab=*/0,
                                      svldSlot, svldLabels.data());
                    break;
                default:  // Equip
                    ev.caption = "Your equipped weapon.";
                    pauseUi.draw_equip(g, pmf, view, pdepth, cw, ch, ev, pitems, asset_root);
                    break;
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

