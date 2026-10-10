// cleria-view — render_window — the live game loop
// Textual part of main.cpp (split 2026-07-23, PLAN.md Phase 0): #included by
// main.cpp in a fixed order; ONE translation unit, not standalone. Keep it that way
// until a real TU split is warranted.

// The launcher's backdrops with a framed camera (position, yaw / pitch in degrees) [HAND-TUNED: picked by eye
// from headless captures], and their names in the shell's Audio page. Any other room id gets spawn_in's overview.
// A CleriaCore addition: retail has no launcher.
struct LaunchRoom { const char* room; const char* name; float x, y, z, yaw, pitch; };
static const LaunchRoom kLaunchRooms[] = {
    {"S_3005", "Fire floor - lava stairs", 0.0f, 4.0f, -26.0f, 90.0f, -5.0f},
    {"S_1009", "Floor 1 - goddess statue", -3.0f, 6.0f, -8.0f, 90.0f, -5.0f},
    {"S_7000", "Summit - the fence against the sky", -31.0f, 5.0f, -34.0f, 90.0f, 4.0f},
    {"S_2007", "Water floor - pools", -63.5f, 2.0f, -0.5f, 45.0f, -10.0f},
    {"S_0002", "Prologue - the Roda tree", 0.0f, 6.0f, -34.0f, 90.0f, 10.0f},
    {"S_6014", "Floor 6 - mirrors", 0.0f, 4.0f, -32.0f, 90.0f, -5.0f},
    {"S_1000", "Tower entrance hall - stairs", -10.0f, 4.0f, -40.0f, 90.0f, 5.0f},
    {"S_6000", "Floor 6 - statue and violet lamps", 0.0f, 25.0f, 40.0f, 90.0f, -5.0f},
    {"S_4000", "Sand floor - entrance stairs", -6.0f, 7.0f, -10.0f, 80.0f, -8.0f},
    {"S_5005", "Floor 5 - stone arch", -5.0f, 5.0f, -16.0f, 80.0f, -3.0f},
};

// ---- interactive walkthrough ------------------------------------------------
static int render_window(Gpu& g, const fs::path& asset_root, const fs::path& first_sob,
                         bool show_title = false) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    // The settings (view_input.inl): the persistent file for a played session, memory defaults for a
    // CLERIA_DRIVE run. The gamepad: SDL's gamepad layer, mapped into the retail pad codes.
    settings_init(std::getenv("CLERIA_DRIVE") == nullptr);
    clear_data_load();             // record.ta2 (view_content.inl): the clear data, before the title's gates
    struct ClearDataGuard { ~ClearDataGuard() { clear_data_shutdown(); } } clear_data_guard;   // every clean return
    // `Release\data.nya` exists (FUN_00576020 [BYTES 0x5760a7..0x5760fe]: opened, closed unread): byte 0x74c106,
    // the gate of Very Easy / Nightmare, the Arena, the Bonus Shop and Adol's bar (CLEAR_DATA.md §6). CleriaCore
    // looks where the retail install keeps it — the release folder of the install the game data came from
    // (gamedata::release_dir_for); a data folder with no known install has none.
    const bool nya_gate = [&] {
        std::error_code nec;
        const fs::path rel = cleria::gamedata::release_dir_for(asset_root);
        return !rel.empty() && fs::exists(rel / "data.nya", nec);
    }();
    if (!modfile_startup()) { SDL_Quit(); return 0; }   // a .cleriamod to install (view_modfile.inl)
    mods_startup(g, asset_root);   // the vfs + the enabled mods, before any asset loads (view_mods.inl)
    content_startup(asset_root);   // the active mods' content/ patches (view_content.inl)
    lua_startup();                 // Lua mods (Script=): start each, events, grant filter (view_lua.inl)
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) std::fprintf(stderr, "SDL gamepad: %s\n", SDL_GetError());
    else g_sdl_in.open_pad();
    // A CLERIA_DRIVE run is headless: a hidden window (nothing on the user's desktop or taskbar) and an
    // offscreen target, whatever the desktop offers. CLERIA_DRIVE_VISIBLE=1 shows and presents it.
    const bool drive_hidden = std::getenv("CLERIA_DRIVE") && !std::getenv("CLERIA_DRIVE_VISIBLE");
    SDL_Window* win = SDL_CreateWindow("CleriaCore - Ys Origin", 1280, 800,
                                       SDL_WINDOW_RESIZABLE | kWindowGpuFlag | (drive_hidden ? SDL_WINDOW_HIDDEN : 0));
    WGPUSurface surface = create_window_surface(g.instance, win);   // view_platform.inl
    WGPUSurfaceCapabilities caps = {};
    if (surface) wgpuSurfaceGetCapabilities(surface, g.adapter, &caps);
    // A CLERIA_DRIVE run renders into an offscreen target (always when hidden; otherwise when the desktop
    // gives no presentable surface), so its `shot`s work and it never depends on the desktop.
    const bool offscreen = std::getenv("CLERIA_DRIVE") && (drive_hidden || caps.formatCount == 0);
    if (caps.formatCount == 0 && !offscreen) { std::fprintf(stderr, "no presentable surface\n"); return 1; }
    // Prefer the LINEAR (non-sRGB) surface format. Our textures are already sRGB and the shaders
    // pass them straight through; if the surface is Rgba8UnormSrgb, presenting sRGB-encodes a
    // SECOND time and the window washes out. The offscreen renders use linear RGBA8Unorm and look
    // correct -- match that here so all pipelines + the surface stay consistent.
    WGPUTextureFormat fmt = offscreen ? WGPUTextureFormat_RGBA8Unorm : caps.formats[0];
    for (std::size_t i = 0; i < caps.formatCount; ++i) {
        const WGPUTextureFormat f = caps.formats[i];
        if (f == WGPUTextureFormat_RGBA8Unorm || f == WGPUTextureFormat_BGRA8Unorm) { fmt = f; break; }
    }
    std::uint32_t cw = 1280, ch = 800;
    WGPUTexture off_tex = nullptr;
    bool vsync = settings().s.cleria.vsync;              // the shell's Video > VSync (Fifo vs Immediate)
    auto configure = [&] {
        if (offscreen) {
            if (off_tex) wgpuTextureRelease(off_tex);
            WGPUTextureDescriptor td = {};
            td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
            td.dimension = WGPUTextureDimension_2D; td.size = {cw, ch, 1};
            td.format = fmt; td.mipLevelCount = 1; td.sampleCount = 1;
            off_tex = wgpuDeviceCreateTexture(g.device, &td);
            return;
        }
        WGPUSurfaceConfiguration sc = {};
        sc.device = g.device; sc.format = fmt; sc.usage = WGPUTextureUsage_RenderAttachment;
        // CopySrc: the CrossFade capture (and CLERIA_DRIVE's `shot`), where the surface allows it.
        if ((caps.usages & WGPUTextureUsage_CopySrc) || std::getenv("CLERIA_DRIVE"))
            sc.usage |= WGPUTextureUsage_CopySrc;
        sc.width = cw; sc.height = ch; sc.alphaMode = WGPUCompositeAlphaMode_Auto;
        sc.presentMode = WGPUPresentMode_Fifo;
        if (!vsync)
            for (std::size_t i = 0; i < caps.presentModeCount; ++i)
                if (caps.presentModes[i] == WGPUPresentMode_Immediate) sc.presentMode = WGPUPresentMode_Immediate;
        wgpuSurfaceConfigure(surface, &sc);
    };
    configure();
    video_init(g, fmt, offscreen || (caps.usages & WGPUTextureUsage_CopySrc) || std::getenv("CLERIA_DRIVE"));   // view_video.inl
    build_pipeline(g, fmt);
    Hud hud; hud.init(g, res_file("hud_font.bin"), fmt);
    hud.debug_text = std::getenv("CLERIA_DEBUG_HUD") != nullptr || settings().s.cleria.debug_hud;   // F3 toggles
    Shell shell;                                         // F1: CleriaCore's overlay (view_shell.inl)
    bool shell_drive_close = false;                      // CLERIA_DRIVE `shell`: close after the shot
    int shell_shot_frames = 0;                           // frames left before that shot
    shell.init(win, g, fmt);
    shell.hud["pad"] = pad_hud;                          // controller hot-plug notice (view_pad.inl)
    shell.hud["modfile"] = modfile_hud;                  // the mod-file install notice (view_modfile.inl)
    shell.hud["video"] = video_hud;                      // the FPS display / frame-time graph (view_video.inl)
    hud.init_atlas(g, asset_root / "IMAGE" / "STATUS_512.DDS");
    hud.init_mini(g, asset_root / "IMAGE" / "F_MINI.DDS");
    hud.init_boss(g, asset_root / "IMAGE" / "HP2.DDS");    // the boss HP bar (it was never loaded live)
    FlameFx flames;                      // torch flames (SEF placements + EF2 flipbook)
    if (!flames.init(g, asset_root))
        std::fprintf(stderr, "flames: NAGA01.DDS failed to load\n");
    ShadowFx shadows;                    // floor shadow decals under characters
    std::vector<Emotion> emotions;       // op 0x148's GoEmotion bubbles (pool 3)
    SpriteTex emotion_tex;               // IMAGE/EMOTION.DDS, 8 x 4 cells of 32 x 32
    {
        Dds d = load_dds(asset_root / "IMAGE" / "EMOTION.DDS");
        if (d.ok()) emotion_tex = upload_rgba(g, d.width, d.height, d.data.data());
    }
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
    // CLERIA_MUTE=1: no audio device at all (quiet test runs); the SE voices are still tracked, on the
    // game's clock (Audio::advance in the tick loop).
    audio.init(asset_root, !std::getenv("CLERIA_MUTE"));  // silent (and harmless) if the device or EFFECTDEF is missing
    audio.set_master_volume(oslider[1] / 1024.0f);  // Option "SFX Volume"
    audio.set_bgm_volume(oslider[0] / 1024.0f);     // Option "BGM Volume"

    // ---- the shell's settings pages (F1, and SETTINGS in the launcher) -------------------------
    // CleriaCore-only switches: QoL off by default (retail behaviour), cheats off. The cheats live in
    // cleria.ini [cleria.cheats] (src/cleria/access.hpp; the Cheats page is view_access.inl).
    bool &cheat_hp = settings().s.access.inf_hp, &cheat_mp = settings().s.access.inf_mp,
         &cheat_boost = settings().s.access.inf_boost, &cheat_o2 = settings().s.access.inf_o2,
         &cheat_ohko = settings().s.access.one_hit_kill;
    int window_mode = video_boot_window(win, offscreen);   // 0 windowed, 1 borderless, 2 fullscreen (view_video.inl)
    auto persist = [] { settings().touch(); settings().save(); };   // a shell edit, saved at once
    bool input_bind_mode = false;                         // Input > Configure Inputs is showing
    // The pointer (view_input.inl): a CLERIA_DRIVE run uses a virtual pointer (the drive's mouse=,
    // click, mhold, mrelease, wheel=), starting at the window centre; a played session reads the real
    // one. Each frame: the swapchain size and window mode the cursor maps through, the OS pointer.
    g_sdl_in.win = win;
    g_sdl_in.virtual_ptr = std::getenv("CLERIA_DRIVE") != nullptr;
    g_sdl_in.ptr_x = int(cw) / 2; g_sdl_in.ptr_y = int(ch) / 2;
    auto ptr_sync = [&] {
        g_sdl_in.bb_w = int(cw); g_sdl_in.bb_h = int(ch);
        g_sdl_in.windowed = window_mode == 0;
        sync_os_cursor();
    };
    // The shell's game-data setup: a folder pick (SDL's native dialog, answered on any thread), a
    // validation, and the unpack on a worker thread (gamedata::InstallTask), polled each frame.
    struct GameDataUi {
        gamedata::InstallTask task;
        gamedata::Status st;
        std::string msg;
        bool msg_ok = false;
        bool was_running = false;
        int refresh = 0;
        fs::path asset_root;
        std::mutex mu;
        std::string picked;                     // set by the dialog callback
        bool ready() const { return st.state == gamedata::State::Ready; }
        std::string status_line() const {
            if (ready()) return st.stale ? "Ready (your install changed since - unpack again)" : "Ready";
            if (st.partial) return "Unpack interrupted - locate Ys Origin again";
            return "Using the development assets folder";
        }
        void start(const fs::path& dir) {
            const gamedata::Validation v = gamedata::validate(dir);
            if (!v.ok) {
                msg = "Not a Ys Origin folder" + (v.missing.empty() ? std::string() : ": missing " + v.missing[0]);
                msg_ok = false;
                return;
            }
            msg.clear();
            if (!task.start(v.root)) { msg = "Could not start unpacking."; msg_ok = false; }
        }
        void pick(SDL_Window* w) {
            SDL_ShowOpenFolderDialog([](void* u, const char* const* list, int) {
                auto* self = static_cast<GameDataUi*>(u);
                if (!list || !list[0]) return;      // cancelled or failed
                std::lock_guard<std::mutex> lk(self->mu);
                self->picked = list[0];
            }, this, w, nullptr, false);
        }
        void find_and_install() {
            for (const gamedata::Validation& v : gamedata::find_installs())
                if (v.ok) { start(v.root); return; }
            msg = "Ys Origin was not found in your Steam libraries - use Locate instead.";
            msg_ok = false;
        }
        void poll() {
            std::string p;
            { std::lock_guard<std::mutex> lk(mu); p.swap(picked); }
            if (!p.empty()) start(fs::u8path(p));
            const bool run = task.running();
            if (was_running && !run) {          // just finished
                const gamedata::InstallResult r = task.result();
                if (r.ok) {
                    msg_ok = true;
                    char buf[160];
                    std::snprintf(buf, sizeof buf, "Unpacked %llu files in %.0f s.%s", (unsigned long long)r.files,
                                  r.seconds,
                                  fs::absolute(asset_root) == fs::absolute(gamedata::data_dir())
                                      ? "" : " Restart CleriaCore to use it.");
                    msg = buf;
                } else {
                    msg_ok = false;
                    msg = r.cancelled ? "Unpacking cancelled." : ("Unpacking failed: " + r.error);
                }
                refresh = 0;
            }
            was_running = run;
            if (!run && refresh-- <= 0) { st = gamedata::status(); refresh = 120; }
        }
    };
    static GameDataUi gd;
    gd.asset_root = asset_root;
    {
        auto vol = [&](ui::List& L, const char* label, int& v, const char* desc, bool bgm) {
            float pct = std::round(float(v) * 100.0f / 1024.0f);
            if (ui::slider(L, label, &pct, 0.0f, 100.0f, 5.0f, "%.0f%%", desc)) {
                v = int(std::lround(pct * 1024.0f / 100.0f));
                if (bgm) audio.set_bgm_volume(float(v) / 1024.0f);
                else audio.set_master_volume(float(v) / 1024.0f);
                options_save(oslider, ocheck);
            }
        };
        shell.panel.add("Prelaunch", [&](ui::List& L) {
            // The game data (src/cleria/gamedata): CleriaCore unpacks the install's own data.ni / data.na
            // (+ the data_us overlay) into its data folder; music and movies are read from the install.
            gd.poll();
            ui::section(L, "Game data");
            if (gd.task.running()) {
                const gamedata::Progress pr = gd.task.progress();
                char buf[96];
                std::snprintf(buf, sizeof buf, "Unpacking...  %d%%  (%llu / %llu files)",
                              pr.bytes_total ? int(pr.bytes_done * 100 / pr.bytes_total) : 0,
                              (unsigned long long)pr.files_done, (unsigned long long)pr.files_total);
                ui::info(L, "Status", buf, ui::kWarn);
                if (ui::button(L, "Cancel", "Stop unpacking. Nothing in your Ys Origin install is touched.")) gd.task.cancel();
            } else {
                ui::info(L, "Status", gd.status_line().c_str(), gd.ready() ? ui::kGood : ui::kWarn);
                if (!gd.st.install.empty())
                    ui::info(L, "Ys Origin install", gd.st.install.string().c_str());
                ui::info(L, "Data folder", fs::absolute(asset_root).lexically_normal().string().c_str(), ui::kValue);
                if (ui::button(L, "Locate Ys Origin...",
                               "Pick your Ys Origin install folder (the one with yso_win.exe). CleriaCore unpacks the "
                               "game's own data from it into its data folder; your install is never modified."))
                    gd.pick(win);
                if (ui::button(L, "Find automatically", "Look for Ys Origin in your Steam libraries."))
                    gd.find_and_install();
            }
            if (!gd.msg.empty()) ui::info(L, "", gd.msg.c_str(), gd.msg_ok ? ui::kGood : ui::kWarn);
            ui::section(L, "Startup");
            if (ui::toggle(L, "Skip logo movie", &settings().s.cleria.skip_logo_movie,
                       "Go straight to the title screen instead of playing the Falcom logo movie first."))
                persist();
            if (ui::toggle(L, "Unlock all characters", &settings().s.cleria.unlock_characters,
                       "Lets you pick Toal on a New Game right away. In the original he becomes available only "
                       "after you have cleared the game with Yunica or Hugo."))
                persist();
            if (ui::toggle(L, "Unlock movie skipping", &settings().s.cleria.unlock_movie_skip,
                       "Lets you skip the prologue and opening movies at any time. In the original they become "
                       "skippable only after you have cleared the game once."))
                persist();
        });
        shell.panel.add("Video", [&](ui::List& L) { video_page(L, win, window_mode, vsync, configure, persist); });
        shell.panel.add("Input", [&](ui::List& L) {
            // Configure Inputs: the 34 retail binding slots ([input] Assign{...}, CONFIG.md §3.3), rebound
            // by capturing the next key / mouse button / pad button (Shell::Capture). With [cleria.input]
            // NativePadBinds the PAD_* slots use SDL gamepad button names (Xbox layout) instead.
            if (g_pad_ui.open) { pad_page(L, shell); return; }   // Input > Controller (view_pad.inl)
            bool& binding = input_bind_mode;
            static std::string bind_msg;
            auto& in = settings().s.input;
            auto& cl = settings().s.cleria;
            if (shell.capture.done) {                       // a capture finished: apply it
                const auto cap = shell.capture;
                shell.capture = {};
                const int slot = cap.slot;
                bind_msg.clear();
                if (cap.cancelled) {
                } else if (cap.cleared) {
                    if (slot >= input::PAD_ACTION && slot <= input::PAD_SWORD_REVU && cl.native_pad_binds)
                        cl.pad_bind[std::size_t(slot - input::PAD_ACTION)] = "";
                    else in.assign[std::size_t(slot)] = 0x103;   // "---" (retail's unbound spelling)
                    persist();
                } else if (cap.kind == Shell::Capture::Key) {
                    const input::Code c = input::dik_from_sdl_scancode(cap.value);
                    if (c == 0 || !input::bindable(c)) bind_msg = "That key can't be bound.";
                    else { in.assign[std::size_t(slot)] = c; persist(); }
                } else if (cap.kind == Shell::Capture::Mouse) {
                    const int b = cap.value == SDL_BUTTON_LEFT ? 0 : cap.value == SDL_BUTTON_RIGHT ? 1
                                : cap.value == SDL_BUTTON_MIDDLE ? 2 : -1;
                    if (b < 0) bind_msg = "Only the left, right and middle buttons can be bound.";
                    else { in.assign[std::size_t(slot)] = input::kMouseL + input::Code(b); persist(); }
                } else {                                     // a pad button
                    if (cl.native_pad_binds) {
                        const char* nm = input::sdl_pad_name(cap.value);
                        if (!nm) bind_msg = "That button can't be bound.";
                        else { cl.pad_bind[std::size_t(slot - input::PAD_ACTION)] = nm; persist(); }
                    } else {
                        const int bi = input::pad_button_from_sdl(cap.value);
                        if (bi < 0) bind_msg = "The d-pad moves; it can't be bound to an action.";
                        else { in.assign[std::size_t(slot)] = input::kPadButton1 + input::Code(bi); persist(); }
                    }
                }
            }
            if (binding) {
                if (ui::button(L, "< Back", "Back to the input options.")) binding = false;
                auto value_of = [&](int slot) -> std::string {
                    if (slot >= input::PAD_ACTION && slot <= input::PAD_SWORD_REVU && cl.native_pad_binds) {
                        const std::string& n = cl.pad_bind[std::size_t(slot - input::PAD_ACTION)];
                        return n.empty() ? "---" : n;
                    }
                    const char* n = input::code_name(in.assign[std::size_t(slot)]);
                    return n ? n : "---";
                };
                auto row = [&](int slot, Shell::Capture::Kind kind, const char* desc) {
                    const std::string v = value_of(slot);
                    if (ui::choice_label(L, input::slot_label(slot), v.c_str(), desc))
                        shell.capture.start(kind, slot, input::slot_label(slot));
                };
                ui::section(L, "Keyboard");
                for (int sl = input::KEY_ACTION; sl <= input::KEY_DOWNRIGHT; ++sl)
                    row(sl, Shell::Capture::Key, "Select, then press the key to bind. Esc cancels, Delete clears. "
                        "The arrow keys and Esc always work as well.");
                ui::section(L, "Mouse");
                row(input::MOUSE_DIR, Shell::Capture::Mouse, "Hold this mouse button to walk toward the cursor.");
                for (int sl = input::MOUSE_ACTION; sl <= input::MOUSE_WALK; ++sl)
                    row(sl, Shell::Capture::Mouse, "Select, then click the mouse button to bind.");
                ui::section(L, "Gamepad");
                for (int sl = input::PAD_ACTION; sl <= input::PAD_SWORD_REVU; ++sl)
                    row(sl, Shell::Capture::Pad, "Select, then press the gamepad button to bind. The left stick "
                        "and the d-pad always move.");
                if (ui::toggle(L, "Xbox-style button names", &cl.native_pad_binds,
                               "Bind the gamepad by its own button names (A, B, X, Y, triggers...) instead of the "
                               "original game's numbered DirectInput buttons. Not a retail option."))
                    persist();
                ui::section(L, "Presets");
                if (ui::button(L, "Preset 1  [Arrows / ZXCV]", "The game's default keyboard layout.")) {
                    in.assign = input::preset1(); persist();
                }
                if (ui::button(L, "Preset 2  [WASD / Mouse]", "The game's WASD and mouse layout.")) {
                    in.assign = input::preset2(); persist();
                }
                if (!bind_msg.empty()) ui::info(L, "", bind_msg.c_str(), ui::kWarn);
                return;
            }
            ui::section(L, "Inputs");
            if (ui::button(L, "Configure Inputs", "Rebind keyboard keys, mouse buttons and gamepad buttons."))
                binding = true;
            if (ui::button(L, "Controller", "Your controller live: its buttons, sticks and triggers, rebinding on a "
                           "picture of the pad, deadzones, vibration."))
                g_pad_ui.open = true;
            ui::section(L, "Options");
            bool ch = false;
            ch |= ui::toggle(L, "Analog as digital", &ocheck[0],
                             "Treat the stick like the d-pad: 8 directions, always at full speed. (In-game Options: "
                             "Use Analog as Digital.)");
            ch |= ui::toggle(L, "Dash", &ocheck[1], "Allow dashing once the Silver Bracelet is obtained. (In-game Options.)");
            ch |= ui::toggle(L, "Hold attack for magic", &ocheck[2],
                             "Charge and cast skills by holding the attack button. (In-game Options.)");
            ch |= ui::toggle(L, "Always dash", &ocheck[3],
                             "Dash whenever you run, without holding a button. (In-game Options.)");
            if (ch) options_save(oslider, ocheck);
        }).open_sub = [&](const std::string& s) {        // Input:bind = Configure Inputs, Input:pad = Controller
            input_bind_mode = s == "bind";
            g_pad_ui.open = s == "pad";
            return input_bind_mode || g_pad_ui.open;
        };
        shell.panel.add("Audio", [&](ui::List& L) {
            ui::section(L, "Volume");
            vol(L, "Music volume", oslider[0], "Background music volume. (The same setting as the in-game Options.)", true);
            vol(L, "Sound effects volume", oslider[1], "Sound effect volume. (The same setting as the in-game Options.)", false);
            if (ui::toggle(L, "Launcher music", &settings().s.cleria.launcher_music,
                           "Plays the title theme on this launcher screen. A CleriaCore addition: the original "
                           "game has no launcher."))
                persist();
            // The launcher's backdrop ([cleria.shell] LauncherRoom): "auto" or one of kLaunchRooms.
            {
                const char* names[1 + std::size(kLaunchRooms)] = {"Follow my last save"};
                std::string& lr = settings().s.cleria.launcher_room;
                int idx = 0;
                for (int i = 0; i < int(std::size(kLaunchRooms)); ++i) {
                    names[i + 1] = kLaunchRooms[i].name;
                    if (lr == kLaunchRooms[i].room) idx = i + 1;
                }
                if (ui::choice(L, "Launcher backdrop", &idx, names, int(std::size(names)),
                               "The room behind this launcher screen. Follow my last save shows the floor of your most "
                               "recent save. Takes effect the next time CleriaCore starts. A CleriaCore addition.")) {
                    lr = idx ? kLaunchRooms[idx - 1].room : "auto";
                    persist();
                }
            }
        });
        // Gameplay (the cutscene skip, the game speed) and Cheats: view_access.inl.
        shell.panel.add("Gameplay", [&](ui::List& L) { access_gameplay_page(L, persist); });
        shell.panel.add("Cheats", [&](ui::List& L) { access_cheats_page(L, persist); });
        shell.hud["access"] = access_hud;               // the cutscene-skip prompt
        shell.hud["content"] = content_hud;             // a mod rule's text (view_content.inl)
        // Speedrun mode (view_speedrun.inl): its tab, its overlay, and the Cheats above as listed
        // modifications. A new cheat adds its line here; other features use speedrun::set_provider.
        speedrun_shell(shell, [&](std::vector<std::string>& out) {
            if (cheat_hp) out.push_back("Infinite HP");
            if (cheat_mp) out.push_back("Infinite MP");
            if (cheat_boost) out.push_back("Infinite Boost");
            if (cheat_o2) out.push_back("Infinite oxygen");
            if (cheat_ohko) out.push_back("One-hit kills");
            const access::Settings& ac = settings().s.access;
            auto fmt_rate = [](float v) { char t[16]; std::snprintf(t, sizeof t, "%g", v); return std::string(t); };
            if (ac.damage_dealt != 1.0f) out.push_back("Damage dealt x" + fmt_rate(ac.damage_dealt));
            if (ac.damage_taken != 1.0f) out.push_back("Damage taken x" + fmt_rate(ac.damage_taken));
            if (ac.exp_rate != 1.0f) out.push_back("EXP gained x" + fmt_rate(ac.exp_rate));
            if (ac.sp_rate != 1.0f) out.push_back("SP gained x" + fmt_rate(ac.sp_rate));
            if (ac.drop_rate != 1.0f) out.push_back("Drop rate x" + fmt_rate(ac.drop_rate));
            if (ac.game_speed != 1.0f) out.push_back("Game speed " + std::to_string(int(ac.game_speed * 100.0f + 0.5f)) + "%");
            if (ac.text_speed != int(access::TextSpeed::Retail)) out.push_back("Text speed (not original)");
        });
        // Mods (view_mods.inl), with its sub-pages: Mods > <mod> (a Lua mod's page, lua_shell) and Mods > Content
        // additions (view_content.inl). No new top-level tabs: build/shell_re/SHELL_CORE.md's tab policy.
        mods_shell(shell);
        lua_shell(shell);                               // the Lua mods' Mods > <mod> pages + hud layers (view_lua.inl)
        shell.panel.add("Interface", [&](ui::List& L) {
            ui::section(L, "Overlays");
            if (ui::toggle(L, "Debug text", &hud.debug_text,
                           "Shows CleriaCore's debug text: the room name, damage numbers and test notes. Not part "
                           "of the original game. (F3)")) {
                settings().s.cleria.debug_hud = hud.debug_text;
                persist();
            }
            access_interface_rows(L, persist);        // flashes, shake, text speed (view_access.inl)
        });
    }

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
    int default_bgm = 0;                 // 0x76c17c: Scene_SetDefaultBgm / Bgm_Play(-1)
    // The projection's near / far: DAT_007b2f50 / DAT_007b2f54, the 4th / 5th args of
    // D3DXMatrixPerspectiveFovLH (FUN_005eca30 0x5ecc0f). Every scene entry resets the scene copies
    // 0x74c0fc / 0x74c0f8 to 1.0 / 2000.0 (FUN_00431840) and the room's op 0xa2 Scene_SetClipPlain
    // (FUN_004348a0) overrides them: the skybox rooms ask for (1, 4000) or (1, 7000).
    float clip_near = 1.0f, clip_far = 2000.0f;
    float bgm_gain = 1.0f, bgm_gain_to = 1.0f, bgm_gain_step = 0.0f;   // op 0xBB Bgm_Volume
    // A settings edit from outside the Option page (the shell overlay: settings().s + touch()):
    // re-read the page's copy and re-apply the live volumes (CONFIG.md §7.4).
    std::uint64_t opt_rev = settings().revision();
    auto sync_settings = [&] {
        if (settings().revision() == opt_rev) return;
        opt_rev = settings().revision();
        options_load(oslider, ocheck);
        audio.set_master_volume(oslider[1] / 1024.0f);
        audio.set_bgm_volume(oslider[0] / 1024.0f * bgm_gain);
    };
    auto apply_room_bgm = [&](const std::string& room_name) {
        const SceneRec* s = scenes.find_room(room_name);
        const int id = s ? int(s->bgm) : 0;
        if (id == cur_bgm) return;
        cur_bgm = id;
        if (id == 0) {
            // [approx] Bgm_Play(0) re-queues a 3.5 s fade on a track a Bgm_Stop(ms) is already
            // fading; the earlier, shorter stop still lands first. Keep that fade instead of a cut.
            if (!audio.bgm_fading()) audio.stop_bgm();
        } else {
            audio.play_bgm(bgmdefs.track(id));
        }
    };

    // Save slots (PLAN.md Phase 2; src/cleria/save.hpp codec). One yso_NN.bin per slot
    // under CLERIA_SAVE_DIR (default ./saves), mirroring the game's own file naming. A save profile
    // (cleria.save.set_profile, CLERIA_SAVE_PROFILE; view_content.inl) moves them to its profiles/<name>/.
    if (const char* e = std::getenv("CLERIA_SAVE_PROFILE"))
        if (!save_set_profile(e)) std::printf("[save] CLERIA_SAVE_PROFILE refused (not a usable name)\n");
    auto save_dir = [] { return save_folder(); };
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

    // ---- the demo movies (build/event_re/MOVIES.md, src/cleria/movie) -------------------------
    // A `demoN\` scene: FUN_005773e0 opens the movie and FUN_00431840 switches to the scene's
    // SCENELIST BGM (the soundtrack: the AVI's own audio is never rendered); the tick
    // FUN_00577750 ends it at the end of the stream or on a skip press; FUN_005758f0 then fades
    // to black (current 0 -> target 1 at the scene-entry speed 0.05 / step) and the fade machine
    // warps to the fixed next scene when black. Entry itself fades in from black (FUN_00439170).
    // CLERIA_MOVIE=0 turns them off; a CLERIA_DRIVE run skips them unless CLERIA_MOVIE=1.
    // CLERIA_MOVIE_SHOT=<seconds> writes the frame at that time to CLERIA_SHOT_DIR (evidence; a
    // CLERIA_DRIVE run then ends the movie).
    const char* movie_env = std::getenv("CLERIA_MOVIE");
    const bool movies_off = movie_env ? std::atoi(movie_env) == 0 : std::getenv("CLERIA_DRIVE") != nullptr;
    MovieSeen movie_seen;
    movie_seen.first_play = ini_game_first_play();   // [not modelled] the clear bytes 0x74ab79..7b
    // Plays demo `demo` (1..8) and returns false if the window was closed meanwhile.
    auto play_demo = [&](int demo) -> bool {
        const std::optional<DemoScene> ds = demo_scene(demo, character == 3);
        if (!ds || movies_off) return true;
        // [graphics] DisableMovies (0x765173): FUN_005773e0 finishes at once, as on an open failure.
        // A build without a decoder (off Windows without CLERIA_FFMPEG) does the same: skipped, not black.
        const bool no_decoder = !MovieDecoder::available();
        const fs::path file = settings().s.graphics.disable_movies || no_decoder ? fs::path() : find_movie(asset_root, ds->movie);
        MovieDecoder dec;
        const bool have = !file.empty() && dec.open(file);
        double length = have ? dec.duration() : 0.0;
        if (file.empty())
            std::printf("  [movie] demo%d: %s %s -> finished at once (the open failure)\n", demo,
                        movie_file(ds->movie), settings().s.graphics.disable_movies ? "disabled"
                                               : no_decoder ? "skipped [not available on this platform]" : "not found");
        else if (!have) {
            length = avi_duration(file).value_or(0.0);
            std::printf("  [movie] demo%d: %s does not decode -> black for %.1f s [stand-in]\n", demo,
                        file.filename().string().c_str(), length);
        } else {
            std::printf("  [movie] demo%d: %s %ux%u %.1f s -> %s\n", demo, file.filename().string().c_str(),
                        dec.width(), dec.height(), length, ds->next);
        }
        // The BGM: a successful Play first calls Bgm_Play(0x6a0590[idx] = 0) (curBgm = 0, the old
        // track faded), then the scene entry's compare starts the demo scene's own track.
        if (!file.empty()) { audio.stop_bgm(); cur_bgm = 0; }
        apply_room_bgm("demo" + std::to_string(demo));

        WGPUTexture mtex = nullptr; WGPUTextureView mview = nullptr; WGPUBindGroup mbind = nullptr;
        if (have) {
            WGPUTextureDescriptor td = {};
            td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            td.dimension = WGPUTextureDimension_2D; td.size = {dec.width(), dec.height(), 1};
            td.format = WGPUTextureFormat_BGRA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
            mtex = wgpuDeviceCreateTexture(g.device, &td);
            mview = wgpuTextureCreateView(mtex, nullptr);
            mbind = hud.make_bind(g, mview);
        }
        // [cleria.qol] UnlockMovieSkip: any demo skips, seen or not (QoL, off = retail).
        const bool skippable = movie_skippable(demo, movie_seen) || settings().s.cleria.unlock_movie_skip;
        MovieSkip skip;
        access::SkipGate menu_gate;                    // the shell's skip on the menu button (below)
        bool finished = file.empty();                  // FUN_005758f0 straight from Play
        float fcur = finished ? 0.0f : 1.0f, ftgt = finished ? 1.0f : 0.0f;
        bool shown = false, quit = false, done = false;
        const char* shot_env = std::getenv("CLERIA_MOVIE_SHOT");
        double shot_at = shot_env ? std::atof(shot_env) : -1.0;
        const Uint64 t0 = SDL_GetTicksNS();
        Uint64 last = t0;
        double acc = 0;
        while (!done) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                g_sdl_in.on_event(e);
                if (e.type == SDL_EVENT_QUIT) quit = true;
                else if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                    cw = std::uint32_t(e.window.data1); ch = std::uint32_t(e.window.data2);
                    if (cw && ch) configure();
                }
            }
            if (quit) break;
            ptr_sync();
            const Uint64 now = SDL_GetTicksNS();
            const double t = double(now - t0) * 1e-9;
            acc += double(now - last) * 1e-9;
            last = now;
            while (acc >= kTickDt && !done) {
                acc -= kTickDt;
                // The skip buttons: the held mask & 0x1001 (Attack | MOUSE_DIR) from the mapper.
                access_tick();                         // a drive's held Esc reaches the movie too
                const input::Frame& mf = input_tick();
                const bool held = (mf.held & (input::kAttack | input::kMouseDir)) != 0;
                // [CleriaCore] the shell's Cutscene skip ends a movie too, by the menu button in its mode
                // (Esc / Camp Menu; single / double / hold), seen or not (ACCESS.md §1.4). Off = retail only.
                const bool menu_skip = menu_gate.step(skip_mode(), (mf.pressed & input::kMenu) != 0,
                                                      (mf.held & input::kMenu) != 0);
                if (!finished) {
                    const bool at_end = have ? dec.ended() : t >= length;
                    if (skip.step(held, skippable) || menu_skip || at_end) {
                        finished = true;                   // FUN_005758f0
                        fcur = 0.0f; ftgt = 1.0f;
                        std::printf("  [movie] demo%d %s at %.2f s\n", demo, at_end ? "ended" : "skipped", t);
                    }
                }
                const float d = ftgt - fcur;               // the fader, 0.05 a step
                fcur = std::abs(d) <= 0.05f ? ftgt : fcur + (d > 0 ? 0.05f : -0.05f);
                if (finished && fcur >= 1.0f) done = true; // black: the fade machine commits
            }
            if (done) break;
            // FUN_0044a870: the next frame, until the movie finishes (the last one stays up).
            if (have && !finished && dec.advance(t)) {
                WGPUTexelCopyTextureInfo dst = {}; dst.texture = mtex; dst.aspect = WGPUTextureAspect_All;
                WGPUTexelCopyBufferLayout lay = {}; lay.bytesPerRow = dec.width() * 4; lay.rowsPerImage = dec.height();
                WGPUExtent3D ext = {dec.width(), dec.height(), 1};
                wgpuQueueWriteTexture(g.queue, &dst, dec.bgra().data(), dec.bgra().size(), &lay, &ext);
                shown = true;
            }
            WGPUSurfaceTexture st = {};
            if (offscreen) {
                st.texture = off_tex;
                wgpuTextureAddRef(off_tex);
                st.status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
            } else {
                wgpuSurfaceGetCurrentTexture(surface, &st);
            }
            if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
                st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) { configure(); continue; }
            WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
            {   // black, then the movie quad and the fader over it (the HUD's first two layers)
                WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
                WGPURenderPassColorAttachment ca = {};
                ca.view = view; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store; ca.clearValue = {0, 0, 0, 1};
                WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
                wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
                WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
                wgpuQueueSubmit(g.queue, 1, &cmd);
                wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
            }
            const MovieQuad q = movie_quad(cw, ch);
            hud.movie_bind = shown ? mbind : nullptr;
            hud.movie_rect[0] = q.x0; hud.movie_rect[1] = q.y0; hud.movie_rect[2] = q.x1; hud.movie_rect[3] = q.y1;
            hud.set_fade(0, fcur);
            {   // only the movie and the fader: the game's HUD layers (kept from its last frame) are
                // set aside for this draw
                std::vector<HudVertex> equip[4];
                for (int i = 0; i < 4; ++i) equip[i].swap(hud.equip_verts[i]);
                std::vector<Hud::Msg> msgs;
                msgs.swap(hud.messages);
                const float overlay = hud.overlay_alpha, boss = hud.boss_frac;
                const bool dbg = hud.debug_text, wg[3] = {hud.gauge_proj[0], hud.gauge_proj[1], hud.gauge_proj[2]};
                hud.overlay_alpha = 0.0f; hud.boss_frac = -1.0f; hud.debug_text = false;
                for (bool& b : hud.gauge_proj) b = false;
                hud.render(g, view, cw, ch, "", 0.0f, nullptr);
                hud.overlay_alpha = overlay; hud.boss_frac = boss; hud.debug_text = dbg;
                for (int i = 0; i < 3; ++i) hud.gauge_proj[i] = wg[i];
                msgs.swap(hud.messages);
                for (int i = 0; i < 4; ++i) equip[i].swap(hud.equip_verts[i]);
            }
            video_post(g, st.texture, view, cw, ch);   // Brightness (view_video.inl)
            if (shot_at >= 0.0 && t >= shot_at) {      // CLERIA_MOVIE_SHOT
                shot_at = -1.0;
                const char* sd = std::getenv("CLERIA_SHOT_DIR");
                const fs::path out = fs::path(sd ? sd : ".") / ("movie_demo" + std::to_string(demo) + ".ppm");
                const std::uint32_t bpr = (cw * 4 + 255) & ~255u;
                WGPUBufferDescriptor rbd = {};
                rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead; rbd.size = std::size_t(bpr) * ch;
                WGPUBuffer rb = wgpuDeviceCreateBuffer(g.device, &rbd);
                WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
                WGPUTexelCopyTextureInfo src = {}; src.texture = st.texture; src.aspect = WGPUTextureAspect_All;
                WGPUTexelCopyBufferInfo dst = {}; dst.buffer = rb;
                dst.layout.bytesPerRow = bpr; dst.layout.rowsPerImage = ch;
                WGPUExtent3D ext = {cw, ch, 1};
                wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
                WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
                wgpuQueueSubmit(g.queue, 1, &cmd);
                wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
                MapReq mr;
                WGPUBufferMapCallbackInfo mi = {};
                mi.mode = WGPUCallbackMode_AllowProcessEvents; mi.callback = on_map; mi.userdata1 = &mr;
                wgpuBufferMapAsync(rb, WGPUMapMode_Read, 0, rbd.size, mi);
                while (!mr.done) wgpuDevicePoll(g.device, true, nullptr);
                const auto* px = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(rb, 0, rbd.size));
                const bool bgra = fmt == WGPUTextureFormat_BGRA8Unorm || fmt == WGPUTextureFormat_BGRA8UnormSrgb;
                std::ofstream f(out, std::ios::binary);
                f << "P6\n" << cw << " " << ch << "\n255\n";
                for (std::uint32_t y = 0; y < ch; ++y)
                    for (std::uint32_t x = 0; x < cw; ++x) {
                        const std::uint8_t* qx = px + std::size_t(y) * bpr + x * 4;
                        const char rgb[3] = {char(qx[bgra ? 2 : 0]), char(qx[1]), char(qx[bgra ? 0 : 2])};
                        f.write(rgb, 3);
                    }
                wgpuBufferUnmap(rb);
                wgpuBufferRelease(rb);
                std::printf("  [movie] wrote %s (t %.2f s, quad %.0f,%.0f..%.0f,%.0f)\n", out.string().c_str(), t,
                            q.x0, q.y0, q.x1, q.y1);
                // A CLERIA_DRIVE run plays a movie only for this shot: end it here (the fade follows).
                if (std::getenv("CLERIA_DRIVE") && !finished) { finished = true; fcur = 0.0f; ftgt = 1.0f; }
            }
            if (!offscreen) wgpuSurfacePresent(surface);
            else wgpuDevicePoll(g.device, true, nullptr);
            wgpuTextureViewRelease(view);
            wgpuTextureRelease(st.texture);
        }
        hud.movie_bind = nullptr;
        hud.set_fade(0, 0.0f);
        if (mbind) wgpuBindGroupRelease(mbind);
        if (mview) wgpuTextureViewRelease(mview);
        if (mtex) wgpuTextureRelease(mtex);
        return !quit;
    };
    // ---- The launcher: CleriaCore's own front end (not retail), before the game boots ----------
    // PLAY / SETTINGS / QUIT over a live room with a slow camera drift. Shown once per process on a
    // title boot (Return to Title goes straight back to the game's title). CLERIA_DRIVE runs skip
    // it unless CLERIA_LAUNCHER is set (=settings also opens the panel); they then shoot one frame
    // to CLERIA_SHOT_DIR/launcher.ppm and quit. CLERIA_LAUNCH_ROOM / CLERIA_LAUNCH_CAM
    // ("x,y,z,yaw_deg,pitch_deg") pick the backdrop; else cleria.ini [cleria.shell] LauncherRoom="S_1009" (a room id, quoted),
    // else (LauncherRoom="auto", the default) the floor of the most recently written save in the save folder
    // (launcher_backdrop, settings.hpp). Its music: the original's title theme ([cleria.shell] LauncherMusic, on by
    // default). All are CleriaCore additions: retail has no launcher.
    static bool s_launcher_shown = false;
    const char* launcher_env = std::getenv("CLERIA_LAUNCHER");
    if (show_title && !s_launcher_shown && (!std::getenv("CLERIA_DRIVE") || launcher_env)) {
        s_launcher_shown = true;
        const char* rn = std::getenv("CLERIA_LAUNCH_ROOM");
        const std::string& lr = settings().s.cleria.launcher_room;
        std::string bg_name = rn ? rn : lr, why = "fixed";
        if (!rn && !g.sob_index.count(lr)) {
            // "auto": the newest save file (by its write time; a manual save or a mod's autosave alike) names
            // the floor, launcher_backdrop the room. Not retail.
            int newest = -1;
            fs::file_time_type nt{};
            std::string scene;
            for (int i = 0; i < 100; ++i) {
                std::error_code ec;
                const fs::file_time_type t = fs::last_write_time(slot_path(i), ec);
                if (ec || (newest >= 0 && t <= nt)) continue;
                std::ifstream f(slot_path(i), std::ios::binary);
                const std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(f), {}};
                if (const auto r = SaveRecord::decode(raw)) { newest = i; nt = t; scene = from_record(*r).scene; }
            }
            bg_name = launcher_backdrop(scene);
            char b[80] = "auto: no save";
            if (newest >= 0) std::snprintf(b, sizeof b, "auto: newest save yso_%02d in %s", newest, scene.c_str());
            why = b;
        }
        Room bg;
        if (auto it = g.sob_index.find(bg_name); it != g.sob_index.end()) bg = load_room(g, asset_root, it->second);
        if (bg.mesh.vertices.empty()) {                    // a room that is missing or does not load: the entrance hall
            bg_name = "S_1000";
            if (auto it = g.sob_index.find(bg_name); it != g.sob_index.end()) bg = load_room(g, asset_root, it->second);
        }
        std::printf("[launcher] backdrop %s (%s)\n", bg_name.c_str(), why.c_str());
        Camera lcam;
        spawn_in(lcam, bg);
        for (const LaunchRoom& p : kLaunchRooms)
            if (bg_name == p.room) { lcam.pos = {p.x, p.y, p.z}; lcam.yaw = p.yaw * 0.0174533f; lcam.pitch = p.pitch * 0.0174533f; }
        if (const char* c = std::getenv("CLERIA_LAUNCH_CAM")) {
            float x, y, z, yw, pt;
            if (std::sscanf(c, "%f,%f,%f,%f,%f", &x, &y, &z, &yw, &pt) == 5) {
                lcam.pos = {x, y, z}; lcam.yaw = yw * 0.0174533f; lcam.pitch = pt * 0.0174533f;
            }
        }
        const Camera base = lcam;
        const float D = room_diag(bg);
        if (std::getenv("CLERIA_DRIVE")) {                 // framing aid: the bounds and the highest point
            Vec3 top = bg.mesh.bmin;
            for (const RoomVertex& v : bg.mesh.vertices) if (v.py > top.y) top = {v.px, v.py, v.pz};
            std::printf("  [launcher] %s bounds (%.1f %.1f %.1f)..(%.1f %.1f %.1f) top (%.1f %.1f %.1f) cam (%.1f %.1f %.1f)\n",
                        bg_name.c_str(), bg.mesh.bmin.x, bg.mesh.bmin.y, bg.mesh.bmin.z, bg.mesh.bmax.x, bg.mesh.bmax.y,
                        bg.mesh.bmax.z, top.x, top.y, top.z, lcam.pos.x, lcam.pos.y, lcam.pos.z);
        }
        g_fog.on = false;
        update_fog(bg, lcam.pos, 0.0f, /*snap*/true);
        WGPUTextureView ldepth = make_depth(g, cw, ch);
        static const char* kItems[] = {"Play", "Settings", "Quit"};
        ui::MainMenu menu;
        int choice_made = -1;
        shell.launcher = true;
        shell.under = [&](ImDrawList* dl, float w, float h, const ui::Nav& nav) {
            const int hit = menu.draw(dl, w, h, nav, kItems, 3, !shell.open);
            gd.poll();
            if (gd.task.running()) ui::status(dl, h, false, "Unpacking the game data...", "Settings  \xE2\x80\xA2  Prelaunch");
            else ui::status(dl, h, true, "Game data ready.", gd.ready() ? "Ys Origin  \xE2\x80\xA2  English"
                                                                          : "Development assets  \xE2\x80\xA2  English");
            ui::version(dl, w, h, "CleriaCore 0.1 (dev)");
            if (hit == 1) shell.open = true;
            else if (hit >= 0) choice_made = hit;
        };
        if (launcher_env && std::string(launcher_env) == "settings") shell.open = true;
        // CLERIA_SHELL_PAGE=<tab name>[:<sub-page>] (headless captures): open the panel on that tab, and that
        // sub-page (Input:bind, Input:pad = Input > Controller, Mods:<mod id>, Mods:content; ui::Panel::select).
        if (const char* pg = std::getenv("CLERIA_SHELL_PAGE")) {
            if (!shell.panel.select(pg)) std::printf("[shell] CLERIA_SHELL_PAGE=%s: no such page\n", pg);
            shell.open = true;
        }
        const Uint64 t0 = SDL_GetTicksNS();
        Uint64 last = t0;
        bool lmusic = false;
        for (int frame_no = 0; choice_made < 0; ++frame_no) {
            // The launcher's music (a CleriaCore addition): the original's title theme, the SCENELIST "title"
            // record's BGM as the title itself plays it, at the Music volume (no device under CLERIA_MUTE). The
            // game's title then finds its own track playing and lets it run on; a logo movie or a room starts
            // its music as usual. Audio > Launcher music turns it off at once.
            if (settings().s.cleria.launcher_music != lmusic) {
                lmusic = !lmusic;
                if (lmusic) apply_room_bgm("title");
                else { audio.stop_bgm(); cur_bgm = -1; }
                if (std::getenv("CLERIA_DRIVE")) std::printf("  [launcher] music %d (bgm %d)\n", int(lmusic), cur_bgm);
            }
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (shell.event(e)) continue;
                if (e.type == SDL_EVENT_QUIT) choice_made = 2;
                else if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                    cw = std::uint32_t(e.window.data1); ch = std::uint32_t(e.window.data2);
                    if (cw && ch) { configure(); wgpuTextureViewRelease(ldepth); ldepth = make_depth(g, cw, ch); }
                }
            }
            const Uint64 now = SDL_GetTicksNS();
            const float t = float(double(now - t0) * 1e-9);
            const float dt = float(double(now - last) * 1e-9);
            last = now;
            // the drift: a slow sway of the yaw and a gentle bob
            lcam = base;
            lcam.yaw = base.yaw + 0.06f * std::sin(t * 0.07f);
            lcam.pitch = base.pitch + 0.015f * std::sin(t * 0.11f);
            lcam.pos.y = base.pos.y + D * 0.002f * std::sin(t * 0.13f);
            advance_scroll(g, bg, 1);
            update_fog(bg, lcam.pos, dt);
            WGPUSurfaceTexture st = {};
            if (offscreen) { st.texture = off_tex; wgpuTextureAddRef(off_tex); st.status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal; }
            else wgpuSurfaceGetCurrentTexture(surface, &st);
            if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
                st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) { configure(); continue; }
            WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
            {   // clear to the fog colour (or black), then the room
                WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
                WGPURenderPassColorAttachment ca = {};
                ca.view = view; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store;
                ca.clearValue = g_fog.on ? WGPUColor{g_fog.r, g_fog.g, g_fog.b, 1} : WGPUColor{0, 0, 0, 1};
                WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
                wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
                WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
                wgpuQueueSubmit(g.queue, 1, &cmd);
                wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
            }
            write_mvp(g, lcam.view_proj(float(cw) / float(ch), D * 0.002f, D * 6.0f));
            draw(g, bg, view, ldepth);
            video_post(g, st.texture, view, cw, ch);   // Brightness (view_video.inl)
            lua_frame();                               // the Lua mods' "frame" handlers here too: a socket opened from
                                                       // the Mods page before a game starts is polled (view_lua.inl)
            shell.frame(g, view, cw, ch);
            if (std::getenv("CLERIA_DRIVE") && frame_no == 6) {     // a headless capture of the launcher
                const char* sd = std::getenv("CLERIA_SHOT_DIR");
                const fs::path out = fs::path(sd ? sd : ".") / "launcher.ppm";
                save_frame_ppm(g, st.texture, fmt, cw, ch, out);
                std::printf("  [launcher] wrote %s\n", out.string().c_str());
                choice_made = 2;
            }
            if (!offscreen) wgpuSurfacePresent(surface);
            else wgpuDevicePoll(g.device, true, nullptr);
            wgpuTextureViewRelease(view); wgpuTextureRelease(st.texture);
        }
        shell.under = nullptr;
        shell.launcher = false;
        shell.open = false;
        g_shell_focus = false;            // hand the input back to the game (the mapper is no longer muted)
        wgpuTextureViewRelease(ldepth);
        free_room(bg, g.white_bind);
        g_fog.on = false;
        if (choice_made == 2) { SDL_DestroyWindow(win); SDL_Quit(); return 0; }
    }
    // Boot (FUN_00442260): the first scene is demo6\, the logo, which then warps to title\.
    if (show_title && !settings().s.cleria.skip_logo_movie && !play_demo(6)) { SDL_DestroyWindow(win); SDL_Quit(); return 0; }

    // The game over's "Return to Title" comes back here (the jump at the end of this function):
    // everything declared below is destroyed and built again for the next game.
title_again:
    if (show_title) {
        // The title (build/menu_re/TITLE_DRAW.md): GoWndDemo's background and the title list
        // (src/cleria/titlescreen), and the pause book opened from it — New Game = ctx 4
        // (Character Select, then the Type / Difficulty popups), Load Game = ctx 2, Options =
        // ctx 3. SP Bonus Acquisition is not here: it is a script menu (GOHOUBIMENU.XSO) the
        // new game's router runs. Steps on the 60 Hz tick like the game.
        // [not modelled] the clear / system data behind the gates (Bonus Modes, Toal, the
        // Extra types) — a fresh install's title; Time Attack / Arena / the Bonus Shop.
        apply_room_bgm("title");   // "title\" is a scene record like any room (BGM id 1)
        pending_load.reset();      // a Return to Title: the last game's Load must not outlive it
        const ItemTable titems = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
        BookRenderer tr;
        tr.init(g, fmt, asset_root, titems);
        TitleScreen tscreen;
        PauseBook tbook;
        TitleGates tgates;
        // The gates (CLEAR_DATA.md §8): the clear bytes 0x74ab7b / 7a / 79 and 0x74c106.
        // [not modelled] what Time Attack / Arena Mode / Bonus Shop open: the entries show as in retail and
        // lead nowhere yet (BONUS_MODES.md §6.2 steps 7-12).
        tgates.clear_yunica = g_clear.cleared(0); tgates.clear_hugo = g_clear.cleared(1); tgates.clear_toal = g_clear.cleared(2);
        tgates.nya = nya_gate;
        std::printf("  [title] clear data: Yunica %02x Hugo %02x Toal %02x, data.nya %d -> Bonus Modes %d, Toal's bar %s\n",
                    g_clear.clear[0], g_clear.clear[1], g_clear.clear[2], int(nya_gate), int(tgates.any_clear()),
                    (g_clear.cleared(0) || g_clear.cleared(1)) ? "open" : "locked");
        std::int32_t tflags[0x200];
        for (std::int32_t& v : tflags) v = -1;
        InvState tinv;
        int tsave_no = 0;
        auto tmodel = [&]() {
            BookModel m;
            m.flags = tflags; m.inv = &tinv; m.items = &titems;
            m.scan = [&](int slot) { return scan_slot(slot_path(slot)); };
            m.save_file_no = &tsave_no;
            m.sliders = oslider; m.checks = ocheck;
            m.metrics = tr.metrics();
            m.charsel.open_toal = settings().s.cleria.unlock_characters || g_lua_open_toal;   // CleriaCore's, not retail's
            // Character Select / Type Select / Difficulty from the clear data (CLEAR_DATA.md §8, TITLE_DRAW.md §5-6).
            m.charsel.clear_yunica = g_clear.cleared(0); m.charsel.clear_hugo = g_clear.cleared(1);
            m.charsel.clear_toal = g_clear.cleared(2);
            m.charsel.nya = nya_gate;
            m.charsel.extra_yunica = g_clear.shop[records::ClearData::kYunicaExtra] != 0;
            m.charsel.extra_hugo = g_clear.shop[records::ClearData::kHugoExtra] != 0;
            m.charsel.extra_toal = g_clear.shop[records::ClearData::kToalExtra] != 0;
            m.charsel.adol_felghana = g_clear.shop[records::ClearData::kAdolFelghana] != 0;
            m.charsel.adol_napishtim = g_clear.shop[records::ClearData::kAdolNapishtim] != 0;
            m.charsel.yunica_outfit = g_clear.shop[records::ClearData::kYunicaOutfit] != 0;
            return m;
        };
        float taccum = 0;
        Uint64 tlast = SDL_GetTicks();
        auto teardown = [&] { SDL_DestroyWindow(win); SDL_Quit(); };
        // The title scene's fader. Its entry fades in from black like every scene (FUN_00431840 ->
        // FUN_00439170: current 1 -> target 0, 0.05 / step), and the title takes input only once
        // current == target (FUN_005765c0 [EXPORT]). New Game (FUN_00575970) runs FUN_005758f0 on
        // the title object: current 0 -> target 1, the title's tick stops (obj+0x279), and the
        // scene is committed when the screen is black.
        float tf_cur = 1.0f, tf_tgt = 0.0f;
        int ttick = 0;                                   // logic ticks on the title (the drive shot)
        bool ng_commit = false;
        mouse::Point tbook_pt;                           // the book's open-time cursor (this+0x128)
        for (bool started = false; !started;) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                g_sdl_in.on_event(e);                // the mapper sees every event (gamepad hot-plug)
                if (shell.event(e)) continue;        // the shell takes what it owns
                if (e.type == SDL_EVENT_QUIT) { teardown(); return 0; }
                else if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                    cw = std::uint32_t(e.window.data1); ch = std::uint32_t(e.window.data2);
                    if (cw && ch) configure();
                }
            }
            ptr_sync();
            const Uint64 now = SDL_GetTicks();
            taccum += float(now - tlast) / 1000.0f;
            tlast = now;
            // A drive run is lockstep here too (one tick a frame), so its title shot lands on t60 exactly.
            if (std::getenv("CLERIA_DRIVE") && !std::getenv("CLERIA_DRIVE_REALTIME")) taccum = kTickDt;
            if (shell.open) taccum = 0;                     // the shell pauses the title too
            while (taccum >= kTickDt && !started) {
                taccum -= kTickDt;
                ++ttick;
                // The input record (FUN_005d2ba0, PAUSEBOOK_DRAW.md §0.7), one step per tick: OK 0x2000,
                // Cancel 0x4000, the auto-repeat steps; the book's 4-way stick while it is up.
                const input::Frame& inf = input_tick(tbook.is_open());
                sync_settings();
                {   // the cursor manager (FUN_004263c0): the title list and the book are menu windows
                    mouse::Context cc;
                    cc.menu = tscreen.list_alive() || tbook.is_open();
                    input::Frame rec = inf;
                    cursor_tick(rec, cc);
                }
                {   // the fader, 0.05 a step
                    const float d = tf_tgt - tf_cur;
                    tf_cur = std::abs(d) <= 0.05f ? tf_tgt : tf_cur + (d > 0 ? 0.05f : -0.05f);
                }
                if (ng_commit) {                             // New Game: black, then the commit
                    if (tf_cur >= 1.0f) started = true;
                    continue;
                }
                const bool live = tf_cur == tf_tgt;          // no input mid-fade (FUN_005765c0)
                const bool confirm_edge = live && (inf.pressed & input::kOk);
                const bool cancel_edge = live && (inf.pressed & input::kCancel);
                const bool conf_held = live && (inf.held & input::kOk);
                const int hstep = live ? inf.dx : 0, vstep = live ? inf.dy : 0;
                std::vector<int> snd;
                if (tbook.is_open()) {
                    BookInput in;
                    in.hstep = hstep;
                    in.vstep = vstep;
                    in.confirm = confirm_edge; in.cancel = cancel_edge;
                    in.held_left = live && (inf.dir & input::kLeft);
                    in.held_right = live && (inf.dir & input::kRight);
                    if (live) book_mouse(in);
                    BookModel m = tmodel();
                    BookEvents ev;
                    tbook.update(in, m, ev);
                    snd = ev.sounds;
                    if (ev.warp) cursor_warp(ev.warp_x, ev.warp_y, ev.warp_mode);
                    if (ev.closed) cursor_warp(tbook_pt.x, tbook_pt.y, 2);   // the dtors' warp back (mode 2)
                    if (ev.options_changed) {
                        audio.set_bgm_volume(oslider[0] / 1024.0f);
                        audio.set_master_volume(oslider[1] / 1024.0f);
                        options_save(oslider, ocheck);
                    }
                    if (ev.option_quit == 1) { teardown(); return 0; }
                    if (ev.load_slot >= 0) {
                        std::ifstream f(slot_path(ev.load_slot), std::ios::binary);
                        std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(f), {}};
                        if (auto r = SaveRecord::decode(raw)) { pending_load = from_record(*r); started = true; }
                        content_title_load(slot_path(ev.load_slot));   // its mod sidecar (view_content.inl)
                    }
                    if (ev.new_game) {
                        const NewGameInfo& ng = ev.new_game_info;
                        character = std::clamp(ng.character, 1, 3);   // [not modelled] Adol (0)
                        skin = ng.type;
                        difficulty = ng.very_easy ? 0 : ng.easy ? 1 : 2 + ng.difficulty;
                        speedrun_new_game(character, skin);  // the speedrun start: the difficulty pick
                        ng_commit = true;                    // FUN_005758f0: fade, then commit
                        tf_cur = 0.0f; tf_tgt = 1.0f;
                        audio.fade_out_bgm(0x5dc);           // FUN_005cde10(1500) [BYTES 0x575985]
                    }
                } else {
                    WndInput in;
                    in.confirm_held = conf_held; in.confirm_pressed = confirm_edge;
                    in.cancel_pressed = cancel_edge;
                    in.vstep = vstep;
                    if (live) wnd_mouse(in);
                    const TitleAction act = tscreen.update(in, false, tgates, tr.metrics(), snd);
                    if (act == TitleAction::NewGame || act == TitleAction::LoadGame || act == TitleAction::Options)
                        tbook_pt = {g_in.x, g_in.y};            // FUN_005a4fd0 saves the record's cursor
                    switch (act) {
                        case TitleAction::NewGame: tbook.open(tmodel(), 4); break;
                        case TitleAction::LoadGame: tbook.open(tmodel(), 2); break;
                        case TitleAction::Options: tbook.open(tmodel(), 3); break;
                        case TitleAction::Exit: teardown(); return 0;
                        case TitleAction::TimeAttack: case TitleAction::Arena: case TitleAction::BonusShop:
                            // [not modelled] BONUS_MODES.md §6.2 steps 7-12: the list comes back.
                            std::printf("  [title] %s: not implemented\n", act == TitleAction::TimeAttack ? "Time Attack" :
                                        act == TitleAction::Arena ? "Arena Mode" : "Bonus Shop");
                            break;
                        default: break;
                    }
                }
                for (int id : snd) audio.play(id, kSfxMenuVol);
            }
            WGPUSurfaceTexture st = {};
            if (offscreen) { st.texture = off_tex; wgpuTextureAddRef(off_tex); st.status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal; }
            else wgpuSurfaceGetCurrentTexture(surface, &st);
            if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
                st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) { configure(); continue; }
            WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
            {   // clear, then the title (or the book over it: its Back00 and pillars are opaque)
                WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
                WGPURenderPassColorAttachment ca = {};
                ca.view = view; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store; ca.clearValue = {0, 0, 0, 1};
                WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
                wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
                WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
                wgpuQueueSubmit(g.queue, 1, &cmd);
                wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
            }
            DrawList dl;
            tscreen.draw(dl);
            if (tbook.is_open()) tbook.draw(dl, tmodel());
            tr.render(g, view, cw, ch, dl);
            if (tf_cur > 0.0f) {                             // the fader over the title
                hud.set_fade(0, tf_cur);
                hud.render(g, view, cw, ch, "", 0.0f, nullptr);
                hud.set_fade(0, 0.0f);
            }
            draw_cursor(g, tr, view, cw, ch, false);
            video_post(g, st.texture, view, cw, ch);   // Brightness (view_video.inl)
            lua_frame();                               // the Lua mods' "frame" handlers here too: a socket opened from
                                                       // the Mods page before a game starts is polled (view_lua.inl)
            shell.frame(g, view, cw, ch);
            // A CLERIA_DRIVE run reaches the title only through a Return to Title: one shot once the
            // entry fade-in is over (60 ticks > its 20), CLERIA_SHOT_DIR/title.ppm, then it quits.
            if (offscreen && ttick >= 60) {
                const char* shd = std::getenv("CLERIA_SHOT_DIR");
                const fs::path out = fs::path(shd ? shd : ".") / "title.ppm";
                save_frame_ppm(g, st.texture, fmt, cw, ch, out);
                std::printf("  [title] t%d fader %.2f list %d book %d, wrote %s\n", ttick, tf_cur,
                            int(tscreen.list_alive()), int(tbook.is_open()), out.string().c_str());
                wgpuTextureViewRelease(view); wgpuTextureRelease(st.texture);
                teardown();
                return 0;
            }
            if (!offscreen) wgpuSurfacePresent(surface);
            else wgpuDevicePoll(g.device, true, nullptr);
            wgpuTextureViewRelease(view); wgpuTextureRelease(st.texture);
            video_frame_end(0);                        // the frame limiter / counters (view_video.inl)
        }
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
    // GenerateLocus sword sweeps (src/cleria/locus.hpp): the player's live GoLocusSwords, in pool
    // order, with the damage class their record took at spawn (+0x109 = FUN_0046d5e0()).
    struct PlayerLocus { LocusSword sword; int cls = 3; };
    std::vector<PlayerLocus> loci;
    Sl2Set sl2set;
    if (!sl2set.load(asset_root)) std::printf("effects: EFFECT/SWORD*.SL2 missing — sword sweeps disabled\n");
    // The action input (src/cleria/player_actions): the attack request / combo, the air attacks,
    // the 0x112 tap-step, the dash and the double jump. The press (the mapper's +0x50c & 1 edge, or
    // a drive's atk) is latched at the top of the logic tick and consumed by that tick's mover.
    PlayerActions pact;
    bool atk_press_pending = false;
    // The bracelet switch (src/cleria/mouse SkillSwitch = FUN_0046cb00; the apply in FUN_00465170
    // 0x4652ca..0x465377): the wheel and the skill bits make a request (+0x518), the mover lands it.
    mouse::SkillSwitch skill_sw;
    int skill_req = -1;
    int jump_phase = 0;                    // +0x3a8: the jump section's marker phase
    bool fix_fall = false;                 // an airborne player under a Fix: the integrator's vertical step (S3)
    int sec3_ground = 0;                   // ticks grounded in the jump section (the stand-in guard)
    bool p_landed = false;                 // last tick's touch-down
    // Projectiles. The effect registries resolve a bullet's eftlist id to its real
    // EF2 and texture; without them bullets simply do not spawn (rather than draw
    // something invented).
    EftList efl;
    EftTex eft;
    if (!efl.load(asset_root) || !eft.load(asset_root))
        std::printf("effects: EFTLIST/EFTTEX missing — projectiles disabled\n");
    int gauge_owner = 0;                 // the actor whose HP the boss gauge shows (DAT_006d5fd8)
    // The live hit-volume, copied from the script when GenerateCollision arms it.
    // Defaults are Yunica's combo #1 so a script that never arms behaves as before.
    AsoVm::Hitbox atk_hb;
    // The swing's damage class, stamped when GenerateCollision arms it: FUN_004494e0 0x449518 writes
    // FUN_0046d5e0() (the player's section +0x994 at that moment) into the record's +0x109, and the hit
    // reads the record, not the section (ROUTE_F13_TOAL D3, issue #52).
    int atk_cls = kDamageClassMelee;
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
    world.rng = game.rs;                                       // the spawn op's glow-phase draw
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
    // World item pickups (src/cleria/pickup.hpp, build/item_re/PICKUPS.md).
    std::vector<Pickup> pickups;
    struct PendingDebris { int ticks; int cast; Vec3 origin; const AsoVm::DebrisEvent* ev; };
    std::vector<PendingDebris> pending_debris;            // a death section's GenerateDebris still to run (spawn_debris)
    std::vector<Popup> popups;                            // GoPopup (pool 7, 32 at most)
    ef2sim::Rng pick_rng;
    pick_rng.bind(game.rs);
    bullets.set_rng([&] { return pick_rng.next(); });       // the kind-0xf bombs' draws (BOMB.md)
    g_cam_rng = [&] { return pick_rng.next(); };          // the camera shake's draws
    weather::Weather wx;                                  // 0x125 / 0x122 / 0x123 (view_weather.inl)
    std::unordered_map<const bullet::Bullet*, int> gas_owner;
    // FUN_00450410 / the brains' inlined spawn. attach_owner != 0: FUN_00451c20 mode 2 — the
    // effect follows that actor's origin at (pos - origin); its spawn rotation / scale are
    // dropped as the attach's own (0 / 1) replace them.
    auto spawn_sim_fx = [&](int id, const Vec3& pos, const Vec3& rot, float scale,
                            ef2sim::SpawnParams sp, int attach_owner, const Vec3& origin) -> ef2sim::Effect* {
        const Ef2* ef = ef2_of(id);
        if (!ef) return nullptr;
        sp.pos = pos; sp.rot = rot; sp.scale = {scale, scale, scale};
        ef2sim::Effect* fx = fxsim.spawn(id, *ef, sp);
        if (fx && attach_owner != 0) {
            fx->local = ef2sim::translation(pos.x - origin.x, pos.y - origin.y, pos.z - origin.z);
            fx->attach_parent = &actor_mats[attach_owner];
            fx->attach_mode = 2;
        }
        return fx;
    };
    // anime_GenerateEffect (FUN_004496f0) for an actor at `org` facing `facing` (radians).
    //   arg 5 == 0 [EXPORT]: FUN_00450410 at org + (cos, sin)(angle + facing) x fwd, y + voff, rot
    //     (0, angle - (facing + pi/2), 0), scale (s, s, s); not attached.
    //   arg 5 != 0 [BYTES 0x4499ce..0x449a33]: spawned at 0 with scale (s, s, s), then
    //     FUN_00451c20(parent = the actor's +0x20, off (0, voff, -fwd), rot (pitch +0x4ec, yaw angle,
    //     roll 0), scale (s, s, s), mode = arg 5): local = T(off) * RotYPR * S (0x451c81..0x451d33).
    //     Mode 2/3 add only the parent's translation, so that local is the whole story. Mode 1
    //     multiplies by the full +0x20; [H] its rotation is the actor's yaw -(facing + pi/2) (the
    //     unattached branch's), baked at spawn — actor_mats carry the origin translation only.
    //     [not modelled] actor +0x4ec (taken as 0); mode 3's +0x230 height (run as mode 2).
    // The attached effect used to keep only the offset, so it lost its scale: Yunica's
    // Whirlwind (102, s 1.5, mode 2) drew at 1x.
    auto spawn_generate_effect = [&](const AsoVm::EffectEvent& fe, const Vec3& org, float facing,
                                     const ef2sim::SpawnParams& sp, int owner) -> ef2sim::Effect* {
        const float a = fe.angle_deg * 0.017453292f;                  // 0x6bd0dc
        if (fe.attach == 0) {
            const Vec3 at{org.x + std::cos(a + facing) * fe.fwd, org.y + fe.voff,
                          org.z + std::sin(a + facing) * fe.fwd};
            return spawn_sim_fx(fe.id, at, {0.0f, a - (facing + 1.5707963f), 0.0f}, fe.scale, sp, 0, {});
        }
        ef2sim::Effect* fx = spawn_sim_fx(fe.id, {}, {}, fe.scale, sp, 0, {});
        if (fx) {
            fx->local = ef2sim::mul(ef2sim::mul(ef2sim::translation(0.0f, fe.voff, -fe.fwd),
                                                ef2sim::rot_ypr(a, 0.0f, 0.0f)),
                                    ef2sim::scaling(fe.scale, fe.scale, fe.scale));
            if (fe.attach == 1)
                fx->local = ef2sim::mul(fx->local, ef2sim::rot_ypr(-(facing + 1.5707963f), 0.0f, 0.0f));
            fx->attach_parent = &actor_mats[owner];
            fx->attach_mode = fe.attach == 1 ? 1 : 2;
        }
        return fx;
    };
    // The script placements 0x117 Eft_LocatePos and 0x11A Eft_LocatePosAddStr: both reach
    // FUN_00450410 with rot = (0, -pi/2 - op5 degrees, 0) and scale (op4, op4, op4), then name the
    // effect (FUN_00451da0). BARRIER_OBJECTS.md §1 [BYTES 0x56b0fe, 0x56f920]. They are ordinary
    // EF2 effects: the barrier ward stays up because its emitters re-arm (B2 & 0x800), and goes
    // only on 0x119 Eft_Kill, which sets fx+0x1d4 (FUN_0056f880) so the 0x895&2 emitters fade.
    auto place_named_fx = [&](int id, float x, float y, float z, float scale, float rot_deg,
                              const std::string& name) {
        const Vec3 rot{0.0f, -1.5707964f - rot_deg * 0.017453292f, 0.0f};
        // [guard, not retail] our loop re-places a room's effects (a reload re-runs the statue
        // variant script), which retail never does: re-place a live same-named effect instead
        // of stacking a second copy.
        if (!name.empty())
            for (auto& f : fxsim.effects())
                if (f->name == name && f->id == id && !f->kill && !f->dead) {
                    ef2sim::System::place(*f, {x, y, z}, rot, {scale, scale, scale});
                    return;
                }
        if (ef2sim::Effect* fx = spawn_sim_fx(id, {x, y, z}, rot, scale, {}, 0, {})) fx->name = name;
    };
    auto kill_named_fx = [&](const std::string& name) {
        for (auto& f : fxsim.effects())
            if (f->name == name) f->kill = true;
    };
    // The exe's own new-game init (FUN_0041fd10, NEWGAME_FLAGS.md): inventory -1 fill,
    // header set, per-character starting items. This also writes flag 149 = 1 — which is
    // what keeps Yunica's swing on her AXE (her attack ASO branches on Flag_IsEqualInt(149,1);
    // 0 would render the Crimson Lotusblade fire-skill form).
    game.new_game(character);
    // FUN_0041fd10: flag 144 = the difficulty 0x74ac3c (0 Normal / 1 Hard / 2 Nightmare), 143 =
    // Easy (0x74e685), 203 = Very Easy (0x74e684). [not modelled] 201 (0x74e25f).
    // Flags 297 / 298 / 299 = min(clear byte, 1) for Yunica / Hugo / Toal [EXPORT 0041fd10.c:175-186]: what
    // S_0000's SP-bonus test and the clear script read.
    for (int c = 0; c < 3; ++c) game.g_flags[297 + c] = std::min<int>(g_clear.clear[std::size_t(c)], 1);
    game.g_flags[144] = difficulty >= 2 ? difficulty - 2 : 0;
    game.g_flags[143] = difficulty <= 1 ? 1 : 0;      // Very Easy sets 143 AND 203 [BYTES 0x5759ed + 0x5759f4]
    game.g_flags[203] = difficulty == 0 ? 1 : 0;
    world.stat_flags = game.g_flags;                  // the spawns' FUN_004202e0 tail (DIFFICULTY.md G2)
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
    ailments::State ail;               // poison / heavy / confuse / curse: 0x76c15c..0x76c16c (view_ailments.inl)
    bool ail_reload = false;           // a record was loaded: its effects are re-created / dropped (ail_tick)
    bool toal_air_cast = false;        // +0x7b0: Toal's last 0x76 (Ignis) cast started in the air
    // Hugo's super barrier (BOOST_SKILLS.md §2.5): +0x7dc, 420 ticks from the press; +0x7e0, the handle of
    // effect 0x90 (PC2_B_B_b) his section 300 spawns at tick 25. While the effect stands every hit on him
    // is a GUARD; the tick that finds the timer 0 kills it.
    int barrier_t = 0;
    ef2sim::Effect* barrier_fx = nullptr;
    StandingRegen regen;               // +0x760: the standing HP regeneration (hazard.hpp)
    float regen_acc = 0.0f;
    Impulse player_kb{};               // knockback velocity, player +0x338/+0x340 (hit.hpp)
    Impulse player_kb2{};              // knock channel 2, +0x344/+0x34c: body / bullet pushes
    float arena_last_x = 0, arena_last_z = 0;   // +0x3e0/+0x3e8: last point inside the mode-2 ring ([H] start)
    Mat4 last_vp; bool have_vp = false;  // last frame's camera, for the AI's off-screen test
    const bool ai_debug = std::getenv("CLERIA_AI") != nullptr;
    std::uint64_t tick_count = 0;
    // ZoomBlur (SCREEN_EFFECTS.md §7; the pass is view_screenfx.inl). zoom_blur = the two writes 0x76a550 /
    // 0x76a554 (XSO 0xce, the bosses, the brains). zoom_centre = FUN_005e0590 [EXPORT 005e0590.c]: a world point
    // to frame pixels, each axis clamped to the frame ([approx] through the last DRAWN frame's view-projection).
    // CLERIA_SCRFX_LOG=1 (CleriaCore diagnostics, not retail): each write, and each blur's first drawn frame
    // (with the centre it has then).
    screenfx::ZoomBlur zb;
    static const bool scrfx_log = [] { const char* v = std::getenv("CLERIA_SCRFX_LOG"); return v && *v && *v != '0'; }();
    bool zb_draw_logged = false;
    auto zoom_blur = [&](float s, float r) {
        if (scrfx_log && (s != zb.s || r != zb.r))         // a brain's every-tick rewrite prints once
            std::printf("  [zoomblur] t%llu ZoomBlur(%.4f, %.4f)\n", (unsigned long long)tick_count, s, r);
        if (!zb.running()) zb_draw_logged = false;
        zb.set(s, r);
    };
    auto zoom_centre = [&](const Vec3& p) {
        if (!have_vp) return;
        const float* M = last_vp.m.data();
        const float px = p.x * M[0] + p.y * M[4] + p.z * M[8] + M[12];
        const float py = p.x * M[1] + p.y * M[5] + p.z * M[9] + M[13];
        const float pw = p.x * M[3] + p.y * M[7] + p.z * M[11] + M[15];
        if (pw == 0.0f) return;
        zb.cx = std::clamp(px / pw * 0.5f + 0.5f, 0.0f, 1.0f);
        zb.cy = std::clamp(0.5f - py / pw * 0.5f, 0.0f, 1.0f);
    };
    // The frame hold (SCREEN_EFFECTS.md §7.3): 0x76a548 / 0x76a54c, and 0x7b2488 = "copy this frame" (the writers
    // set it; the frame clears it, 0x5e0563).
    screenfx::FrameHold fhold;
    bool fx_snapshot = false;
    auto frame_hold = [&](float s, float r) {
        if (scrfx_log) std::printf("  [framehold] t%llu FrameHold(%.4f, %.4f)\n", (unsigned long long)tick_count, s, r);
        fhold.set(s, r);
        fx_snapshot = true;
    };
    Vec3 light_drawn{1.0f, 1.0f, 1.0f};                    // the scene light's colour of the last frame (the log)
    // The player's specular tint struct (+0x260; hit_feedback.hpp): the GUARD and Boost-ready white flashes.
    // [not modelled] the diffuse struct +0x278 and the other writers of both (SCREEN_EFFECTS.md U14).
    hitfx::Tint player_spec;
    auto player_white_flash = [&](float step) {
        hitfx::white_flash(player_spec, step);
        if (scrfx_log) std::printf("  [tint] t%llu player specular 0xffffff -> 0, step %.3f\n", (unsigned long long)tick_count, step);
    };
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
        // A load writes the RAW clear bytes into 297..299 [EXPORT 0042f5e0.c:210-212] (0xD5 / 0x8E / 0x89 included).
        for (int c = 0; c < 3; ++c) game.g_flags[297 + c] = g_clear.clear[std::size_t(c)];
        character = std::clamp(pending_load->character(), 1, 3);
        if (!pending_load->scene.empty()) {
            auto it = g.sob_index.find(pending_load->scene);
            if (it != g.sob_index.end()) boot_sob = it->second;
        }
    }
    content_session_start(game.g_flags, character, pending_load.has_value());   // mods: sidecar, NewGame / Load

    // Award EXP and process level-ups (src/cleria/exp_award, build/progress_re/REWARDS.md §1,
    // LEVELING.md). A kill (FUN_004fbb40 0x4fcb45..) credits max(1, floor(FUN_004fa2f0(+0xb0) x rate
    // x diffMult)) x (1 + 0.01 combo): the level-difference scaler with its x0.6666, the Easy / Very
    // Easy multiplier and the hit combo meter 0x76c1a8. A boss (FUN_004fa4a0) credits the same
    // without the chain bonus, and nothing in time attack (g_flags[193]). Then the engine loops
    // level-up while total >= threshold(level+1) (= gameinfo exp_to_level, cumulative pair 10),
    // capped at 60.
    expaward::Combo kill_combo;          // 0x76c1a8 / 0x76c1b0
    // The SP credit of a kill or a boss: FUN_004fa390(base) into the wallet 0x76a75c (FUN_004fa290, clamped
    // 0..999999), skipped when the base is 0. [not modelled] the mode byte 0x74e25e (time attack / arena: 0).
    auto credit_sp = [&](int base) {
        if (base == 0) return;
        expaward::SpIn si;
        si.index = game.g_flags[144]; si.easy = game.g_flags[143] != 0;
        si.very_easy = game.g_flags[203] != 0; si.bless_sp = (game.g_flags[217] & 0x100) != 0;
        si.rate = settings().s.access.sp_rate;
        player.sp = std::clamp(player.sp + expaward::sp_award(base, si), 0, 999999);
    };
    // `boss_eex`: the boss's EEX record, whose +0x10 is the award block's +0x30, the SP FUN_004fa4a0 credits
    // after the EXP [EXPORT 004fa4a0.c:38-43; 00443b80.c: param_2[0xc] = (int)EEX+0x10] (140 for the first
    // six bosses, 0 for Dalles and Darm [DATA EEX.DAT]).
    std::function<void()> level_up_fx;   // a level gained: the popup, the SE, the effect (set below, FUN_00420c40)
    auto award_exp = [&](float b0, int enemy_level, bool boss, bool awarded = false, int boss_eex = -1) {
        if (boss && game.g_flags[193] == 0 && boss_eex >= 0 && std::size_t(boss_eex) < eex.bosses.size())
            credit_sp(int(eex.bosses[std::size_t(boss_eex)].unk10));
        if (!ginfo.ok()) return;
        // The shell's EXP rate and the Lua mods' (an AP seed's) factor stand where retail's
        // 0x76a5fc (1.0 in every save) multiplies, before the floor.
        const float rate = settings().s.access.exp_rate * lua_exp_factor(player.level);
        const int diff = difficulty >= 2 ? difficulty - 2 : 0;                      // DAT_0076bb5c
        const float dm = expaward::diff_mult(game.g_flags[143] != 0, game.g_flags[203] != 0);
        if (boss) {
            if (game.g_flags[193] != 0) return;
            player.exp += expaward::boss_award(b0, player.level, enemy_level, diff, rate, dm);
        } else {
            expaward::KillIn k;
            k.base = b0; k.player_level = player.level; k.enemy_level = enemy_level; k.difficulty = diff;
            k.rate = rate; k.diff_mult = dm; k.combo = kill_combo.count; k.awarded = awarded;
            player.exp += expaward::kill_award(k);
        }
        while (player.level < 60 &&
               player.exp >= float(ginfo.exp_to_level(player.level + 1))) {
            ++player.level;
            player.max_hp = int(player_stats().max_hp);
            // FUN_00420c40 bit 0x01: each level-up is a full heal (FUN_004fa160(9999)) unless
            // the player is in the death state.
            if (player.hp >= 1) player.hp = player.max_hp;
            hud.debug_message("Level up! Lv " + std::to_string(player.level));
            if (level_up_fx) level_up_fx();
        }
    };
    // Hand the room-load script's placed effects (op 0x117) to the world so they render.
    // S_SAVEOBJECTCHANGE both picks the statue variant AND places its idle glow; before
    // this the glow was parsed and thrown away, so a cleansed statue stood there dark.
    // Called on every path that swaps `room`, since each one re-runs that script.
    auto apply_load_effects = [&](const Room& rm) {
        for (const Room::LoadEffect& e : rm.load_effects)
            place_named_fx(e.id, e.x, e.y, e.z, e.a, e.b, e.name);
    };
    // FUN_00431840's per-room writes on every scene entry (build/item_re/KEY_ITEMS.md §1.6): the
    // room number (flag 505), the item gates 290 (S_3009, the Harmonica's noise corridor), 459
    // (S_3103, the Hammer's pillar), 474 (S_5080), and the whole-room water mode 0x76a544 = 2 in
    // five S_20 rooms. The room's own scripts may override them.
    int water_mode = 0;
    // The player's water state (FUN_0045c490, build/physics_re/WATER.md §1a): +0x38a submerged,
    // +0x389 wading. Mode 2 rooms are submerged throughout; mode 1 (a room whose collision has
    // a water tree) casts up into it every tick. [not modelled] enemies' water state (G6).
    bool submerged = false, wading = false;
    int splash_cd = 0;                     // +0x4c3: the splash cooldown
    Oxygen oxy;
    float drown_acc = 0.0f;
    SurfaceHazard hazard;                  // FUN_00462550's floor / noise counters (+0x4c8 / +0x810)
    float hazard_acc = 0.0f;
    // Toal's Godspeed (ABILITIES_VERIFIED.md §3.3, mover 0x4693ed..0x469a97): the dash counter
    // +0x7a4, the lock-on (+0x7a8 / target point +0x790), and the body-collision ramp +0x4d8 that
    // restores +0x60 |= 0x80 once it is back to 1.0.
    int gs_t = 0;
    bool gs_locked = false;
    Vec3 gs_target{};
    float gs_ramp = 1.0f;
    auto room_entry_flags = [&](const std::string& scene) {
        std::string up = scene;
        for (char& c : up) c = char(std::toupper(static_cast<unsigned char>(c)));
        const std::size_t us = up.rfind("S_");
        const int num = us == std::string::npos ? 0 : std::atoi(up.c_str() + us + 2);
        game.g_flags[505] = num;
        game.g_flags[290] = num == 3009;
        game.g_flags[459] = num == 3103;
        game.g_flags[474] = num == 5080;
        // Retail's own rule is this hard-coded list: 0x76a544 = 0, then a __stricmp of the map
        // path against the 5 pointers at 0x68a20c ("s_20\s_2008\" .. "s_20\s_2015\", 0x6988e0..
        // 0x698920) sets 2 [BYTES 0x43190e..0x431956]. No scene data marks a room underwater.
        water_mode = (num == 2008 || num == 2009 || num == 2010 || num == 2014 || num == 2015) ? 2 : 0;
        submerged = water_mode == 2;          // the factory / warp placement sets it, no transition
        wading = false;
    };
    // FUN_00431840's per-scene flag writes (SCENEFLOW.md 6.2), on every scene entry: the first
    // room too (a dev start into a room otherwise kept flag 185 = 0 and the HUD faded out).
    auto scene_entry_flags = [&](const std::string& scene) {
        game.g_flags[160] = 0;                                   // the sub-area (0 in the data)
        for (int f : {161, 209, 156, 130, 132, 189, 190, 194, 197, 200, 135, 497, 506}) game.g_flags[f] = 0;
        for (int f = 165; f <= 180; ++f) game.g_flags[f] = 0;    // the per-room scratch block
        game.g_flags[185] = 1;
        room_entry_flags(scene);
    };
    scene_entry_flags(boot_sob.stem().string());
    Room room = load_room(g, asset_root, boot_sob, game.g_flags, character);
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
    PlayerSand psand;            // +0x4ba / +0x320 / +0x38b / +0x38c: the sand2 state (src/cleria/sand.hpp)
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
    std::uint32_t curAnim = std::uint32_t(std::max(0, chr.find_tag(std::uint32_t(avm.anim_tag()))));
    int animFrame = 0, shownFrame = -1;
    chr.set_anim(g, curAnim, 0);
    int p_motion_applied = -1;   // the Chr_Animate motion last applied to her ASO (CHR_OPS.md §4)
    // Pick the player's sprite frame: FUN_00446340 uses the Chr_Pattern pin (+0xa1c/+0xa20) when
    // set, else the ASO's tag/frame; FUN_00444dc0 clamps the frame. A pinned tag this AIA lacks
    // falls back to the ASO [H: the engine falls to the AIA's default record].
    auto show_player_frame = [&](const Actor* wp) {
        const bool aia_changed = sync_aia();   // an AIA swap re-decodes (see the gameplay call site)
        int tag = avm.anim_tag(), frame = avm.frame();
        if (wp && wp->pattern.pinned() && chr.find_tag(std::uint32_t(wp->pattern.tag)) >= 0) {
            tag = wp->pattern.tag; frame = wp->pattern.frame;
        }
        const int wantImg = chr.find_tag(std::uint32_t(tag));
        if (wantImg >= 0 && (aia_changed || std::uint32_t(wantImg) != curAnim)) {
            curAnim = std::uint32_t(wantImg);
            shownFrame = -1;
        }
        const int nf = int(std::max(1u, chr.frames_of(curAnim)));
        animFrame = std::max(0, std::min(frame, nf - 1));   // FUN_00444dc0 clamps
        if (animFrame != shownFrame) { chr.set_anim(g, curAnim, animFrame); shownFrame = animFrame; }
    };

    auto center = [](const Room& r) -> Vec3 {
        return {(r.mesh.bmin.x + r.mesh.bmax.x) * 0.5f,
                (r.mesh.bmin.y + r.mesh.bmax.y) * 0.5f,
                (r.mesh.bmin.z + r.mesh.bmax.z) * 0.5f};
    };
    auto eye_h = [&] { return room_diag(room) * 0.02f; };
    // FUN_00434020 places the player through FUN_00459ff0 (the SCT default and a SEN arrival):
    // the floor below the placed point itself (placement_floor), never the room's top surface
    // at that x/z -- in a stacked room (S_2008, S_2014) that put the player on a ceiling slab,
    // hidden behind the walls.
    auto placed_floor = [&](const Vec3& p) { return placement_floor(phys_mesh(room), p); };
    auto spawn_player = [&] {
        // The room's own spawn point: the SCT's CAST_ADOL record. Room centre is
        // only a fallback for rooms that don't place the player.
        const CastPlacement* sp = room.cast.player_spawn();
        const Vec3 c = sp ? sp->position : center(room);
        const float fl = sp ? placed_floor(c)
                            : floor_at(phys_mesh(room), c.x, c.z, room.mesh.bmax.y + room_diag(room));
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
            // Every cast's script reads the game-flag table (0x76b91c): M980's morph loop reads and writes
            // g_flags[179/180] (M980.md §5), M360's death counts in g_flags[178], and the Flag_CmpInt(143 / 144 /
            // 164, ..) branches hold the Easy / Hard+ / Nightmare shot counts and the Mask of Eyes timings (#62).
            e.avm.set_flags(game.g_flags, 0x200);
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

    bool room_events_dirty = true;      // (re)run the room's event scripts on the manager


    auto spawn_enemies = [&] {
        // Populate the world the engine's way: run S_xxxx.XSO (op 0xa0 -> SCT bulk,
        // op 0x12c -> scripted), seeded with the persistent game flags. Then load a
        // sprite for every spawned cast (incl. 0x12c hub NPCs the SCT never named) and
        // pair them. Stats come from ONE path (ActorWorld::spawn): SCT name / cast_type
        // -> CASTINFO -> enemy_index -> ENEMYINFO -> GAMEINFO. Real names = CASTINFO+0x10.
        populate_room(world, room, game.g_flags);
        // This spawn pass runs the root only for its actors; its named effects (the ward) are
        // placed again by the live root on csMgr, into the EF2 sim.
        world.drop_named_effects();
        bullets.clear();                                      // before the effects they hold
        gas_owner.clear();
        fxsim.clear();
        wx.clear();                                           // FUN_00428ec0 / 0x76a558 = 0 (WEATHER.md)
        apply_load_effects(room);                             // after the clear (the statue glow)
        pickups.clear();
        pending_debris.clear();
        popups.clear();
        actor_mats.clear();
        efx.kinds.clear();
        efx.init_from_world(g, asset_root, world, casts);
        render = build_enemy_render(world, casts, efx);
        bind_enemy_aso();
        content_on_room(room, pickups, water_mode == 2);    // mods: RoomEnter, chests, pickups (view_content.inl)
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
    };

    spawn_enemies();

    apply_room_bgm(room.name);

    // Scripted map objects (src/cleria/meshobj.hpp, build/event_re/WAITOPS.md §4): moves,
    // clip playback and the event bit, re-baked into the room's render + collision meshes
    // when they change. `mesh_delta` is each object's displacement in the last mesh step (the
    // platform carry reads it next tick, MOVER.md §3.3).
    MeshWorld meshes;
    int mesh_load_serial = 0;                // the scene loads so far (load_meshes(false)): a boss brain built under
                                             // an earlier one is stale (boss_tick .. boss5_tick)
    std::unordered_map<std::string, Vec3> baked_pos, mesh_delta;
    std::unordered_map<std::string, Mat4> baked_frame;   // the floor object's frame at its last bake (the carry)
    Mat4 floor_was, floor_now;               // the floor object's frame before / after the last mesh step
    bool floor_moved = false;                // ... which changed it (the rider carry reads the pair next tick)
    std::string player_floor_obj;            // the object the player stands on (+0x540)
    int ride_tri = -1;                       // ... and, on the stunned Velagunder, the face under him and his
    Vec3 ride_uvw{};                         // place in its frame (a + u (b - a) + v (c - a) + w n): the carry
    Vec3 carry{};                            // +0x5b0..: the platform carry
    // (Re)seed the mesh runtime from the room's objects. keep = a reload of the same room:
    // objects keep their runtime state and are re-baked from it.
    auto load_meshes = [&](bool keep) {
        std::unordered_map<std::string, MeshObj> old;
        if (keep) old = meshes.all();
        else ++mesh_load_serial;
        meshes.clear(); baked_pos.clear(); mesh_delta.clear(); baked_frame.clear();
        for (const RoomMesh::ObjRange& r : room.mesh.object_ranges) {
            std::vector<MeshClip> clips;
            for (const auto& c : r.clips) clips.push_back({c.name, c.start, c.end, c.next, c.rate});
            MeshObj& m = meshes.add(r.name, r.obj.position, std::move(clips));
            m.rot = r.obj.rotation;              // the re-bake passes &m.rot: keep the placement's turn
            m.spawn_rot = m.rot;                 // +0x3dc (Mesh_ResetRotation)
            baked_pos[r.name] = r.obj.position;
            for (std::size_t i = 0; i < m.clips.size(); ++i)        // built posed at a clip's end
                if (!r.posed_clip.empty() && m.clips[i].name == r.posed_clip) {
                    m.sect = int(i); m.time = float(m.clips[i].end); m.count = 1;
                }
            // An unnamed animated object plays its section 0 from the load: the YMO loader leaves the model's
            // cursor on section 0 [EXPORT 005f31d0.c:400-426], the instance's cursor copies it [EXPORT
            // 005f5260.c:99-101] and the mesh update advances it every step [EXPORT 00503ef0.c:63-67]. No
            // script can name it to stop it: S_4015's two s40step_02 platforms (the only way to switch_01),
            // S_3004's three s30step0 lifts. [H] the start time = the section's first frame.
            // [approx] only a section 0 named 'Default' (those five, and S_3004's unnamed s30swit0). Left at
            // rest, as before: a NAMED object (it waits for its script's Mesh_SetMotion); the exporter's whole-
            // range 'ALL' sections (S_1006's Mask of Eyes wall s10wall_00, whose re-bake would undo view_mask's
            // collision switch; s30kaid0; s7099kumo, whose update kind is 3 [EXPORT 00501a30.c:208-211]).
            // 'LOOP' is the three large spinning floors s50_yuka_00 / 01 / 02 (S_5005, S_5012): a standing player
            // rides them by the frame's whole matrix (rider.hpp, MOVER.md §3.3 [BYTES 0x45a9a4..]).
            if (!r.name.empty() && r.name[0] == '#' && !m.clips.empty() &&
                (m.clips[0].name == "Default" || m.clips[0].name == "DEFAULT" || m.clips[0].name == "LOOP")) {
                m.sect = 0; m.time = float(m.clips[0].start); m.count = 0;
                m.dirty_pose = true;
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

    // What this visit's live scripts did to the room's objects (0x16B / 0x16C): a change marks the
    // room for a geometry rebuild at the end of the tick (STATUE_CLEANSE.md D2: the statue swap).
    LiveObjState live_obj;
    bool live_obj_dirty = false;
    load_meshes(false);
    // The door warp (build/warp_re/WARP.md §2/§4, build/warp_re/TRANSIT.md). FUN_0042cf00
    // is LEVEL-triggered: no inside/outside memory and no cooldown. A hit stages the volume's
    // descriptor (door_pending = 0x769fb8 + 0x769fbc), latches the scene (door_latched = 0x76a538, which
    // blocks every further trigger until the next scene load), starts the fade to black and the walk-out.
    // The fade machine FUN_004106e0 commits once current == target (door_commit = 0x74c120 |= 3); the
    // swap itself happens at the next frame (FUN_00442dd0), like a script's ChangeScene.
    std::optional<SenTrigger> door_pending, door_commit;
    bool door_latched = false;
    struct DoorWalk { bool on = false; Vec3 tgt{}; bool run = false; } door_walk;   // +0x358 / +0x364
    bool door_running = false;          // the last player tick's run stage (+0x4ca), read by the trigger
    std::function<void()> scene_entry_fader;   // FUN_00431840's fader reset (bound once the fader exists)
    bool scene_objects_reset = false;   // a scene change destroys every object: the busts too
    std::string door_label;             // 0x76a474: the last committed door's SEN label (0x80)
    bool player_vanished = false;       // the player's +0x60 & 0x80 cleared by 0x14d (a new actor per scene)
    auto transition = [&](const std::string& dest, const SenTrigger* via = nullptr) {
        // FLOOR13_OPS.md: the fade machine's commit strcpy's the door label to 0x76a474 (0x410907);
        // a script ChangeScene leaves it. The room setup FUN_00431840 resets timeScale 0x6d6078 to
        // 1.0 (0x431b62), and the arriving player is a new actor (+0x60 & 0x80 set again).
        if (via) door_label = via->name;
        g_time_scale = 1.0f;
        player_vanished = false;
        // FUN_00431260 publishes the pending scene and clears 0x76a538; the old room's staged door and
        // its walk-out die with the old player actor.
        door_pending.reset(); door_commit.reset(); door_latched = false; door_walk = {};
        auto it = g.sob_index.find(dest);
        if (it == g.sob_index.end()) { std::printf("  (no SOB for %s)\n", dest.c_str()); return; }
        std::printf(">> entering %s\n", dest.c_str());
        // ** Copy everything we need off `via` NOW. ** It points into the CURRENT room's
        // sen.triggers, which `room = std::move(next)` below frees -- reading via after
        // that is a use-after-free (it silently read as "no arrival", dropping the player
        // at the SCT default = the wrong entrance on every return). Copy by value first.
        const bool has_arr = via && via->has_arrival();
        const Vec3 arr = via ? via->arrival_pos : Vec3{};
        const int fac_deg = via ? via->arrival_facing_deg : 0;
        // Every scene entry, door or script (FUN_00431840; SCENEFLOW.md 6.2): a dead player is
        // revived, and the per-scene flags are reset before the room's scripts read them.
        if (player.hp < 1) { player.hp = player.max_hp; player.sp_cur = player.sp_max; }
        // ... and the screen fader is reset to fade in from black (#51): FUN_00431260 calls FUN_00431840
        // (0x43176f) on every scene load, and from the second load on (0x74ac3f, set once at 0x431805 and
        // never cleared) that calls FUN_00439170 (0x43187c: the fader object 0x769f18 memset -- colour
        // 0x769f30 black -- speed 0x769f28 = 0.05) and sets current 0x769f1c = 1.0, target 0x769f24 = 0,
        // hold 0x769f2c = 0. Doors, warps, ChangeScene, a load and Retry all come through here, so a
        // fader a script of the old room left black never survives the scene change.
        if (scene_entry_fader) scene_entry_fader();
        // FUN_00431260 drops the player (0x74c09c = 0) and FUN_00434020 spawns a new GoActor
        // (FUN_004590b0 zeroes +0x120..+0x8f0), so the i-frames +0x734 do not cross a scene: a boss's
        // 9999 (FUN_00461120(2)) used to last ~166 s into the next floor (ROUTE_F13_TOAL F-T2).
        player_iframes = 0;
        // The boss HP gauge does not cross a scene load either (issue #78: it stayed up after a Retry):
        // FUN_00431260 writes DAT_006d5fd8 = 0, the gauge widget's owner [EXPORT 00431260.c:211, 0x431735];
        // the fight's own setter (FUN_0043a460) shows it again when the boss registers.
        hud.hide_boss_gauge(); gauge_owner = 0;
        hud.clear_world_gauges();                  // the mini gauges: FUN_00431840 [EXPORT 00431840.c:672-687]
        // ... and neither do the sounds: FUN_00431840 calls FUN_005ce430(0) [BYTES 0x431ca6], a 1.5 s fade
        // and stop of every SE but the `manual` records (the env beds 600..606, which the next room's
        // script stops or plays on). This is what ends a loop its script never stops (618, 680, 751).
        audio.stop_all(false);
        scene_entry_flags(dest);
        clip_near = 1.0f; clip_far = 2000.0f;                    // FUN_00431840 (0x74c0fc / 0x74c0f8)
        scene_objects_reset = true;                             // busts / caption card (start_room_events)
        live_obj = {};                                           // a new visit
        emotions.clear();
        live_obj_dirty = false;
        // A scene load installs the room's SCM again — also when it is the same room (a Retry, a load): the slots'
        // edits, the forced slot and the scroll do not outlive it (FUN_0042b1a0; CAMERA_OPS.md: "the edits last
        // until the room reloads its camera file"). Without this a retried boss room kept the intro's scroll.
        g_rig_room.clear();
        Room next = load_room(g, asset_root, it->second, game.g_flags, character);
        free_room(room, g.white_bind);
        room = std::move(next);
        load_meshes(false);
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
                feet = {arr.x, placed_floor(arr), arr.z};          // FUN_00459ff0's ray down
                fly = false;    // a room with no CAST_ADOL (S_2006) is still entered on foot
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
            // Every labelled arrival carries one, 0 / -1 / 360 included: the SEN load converts +0x238
            // unconditionally (0x433b80 -> FUN_00443a80) into the region entry the warp copies to
            // 0x769fbc -> 0x76a3a0 (0x42d140, 0x4108fa), and FUN_00434020 places with 0x76a3ac
            // whenever the label byte 0x76a3b4 is set (0x4340ad..0x4340e8). 0 = Falcom 0 (+Z); the
            // sentinels = heading 0 (+X). Skipping them kept the last room's facing (PLAYTEST_1.md §2).
            if (has_arr) {
                const float h = falcom_heading(fac_deg);
                face_dir = {std::cos(h), 0.0f, std::sin(h)};
                facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                std::printf("  arrival facing %d deg -> dir %d\n", fac_deg, facing);
            }
            // FUN_00426bc0, from FUN_00434020 with xmm1 = the placed heading +0x314 [BYTES
            // 0x4340df..0x4340f2]: moveAngle = drawAngle = theta and the pointer warps (mode 0) to
            // 96 units from the screen centre along it. [H] theta = CleriaCore's facing heading.
            {
                const float theta = std::atan2(face_dir.z, face_dir.x);
                const mouse::Point sp = mouse::spawn_cursor(theta);
                cursor_warp(sp.x, sp.y, 0);
                g_cursor.move_angle = g_cursor.draw_angle = theta;
            }
        }
        spawn_enemies();
        g_tick_reset = true;  // don't catch up on the load stall (the exe's DAT_0075aed5)
    };
    // Reload the current room (re-gates chests for the new character / state).
    // rerun_events=false rebuilds ONLY the visual mesh (statue model swap) without re-running
    // the room's event scripts — safe to call mid-cutscene (the goddess-cleanse swap happens
    // while the world is frozen, and re-running events then would restart BATTLESTART etc.).
    // Objects destroyed this session (SOB trigger class 1). Breakables carry no opened-flag
    // — @Break_* writes none — so this is deliberately session-scoped, matching retail where
    // pots respawn when you re-enter from a load.
    // Keyed "<room>/<object>": a name is only unique within its room (FLOOR2 B5).
    std::unordered_set<std::string> broken_objects;
    auto broken_in = [&](const std::string& room_name) {
        std::unordered_set<std::string> out;
        const std::string pre = room_name + "/";
        for (const std::string& k : broken_objects)
            if (k.compare(0, pre.size(), pre) == 0) out.insert(k.substr(pre.size()));
        return out;
    };
    // 0x186 Mesh_SetSphereCollision'd objects of the current room (FUN_005735b0), by lower-case
    // name: the child sphere (radius, offset from the mesh), its HP (the mesh's +0x98 copy) and
    // whether its death already ran the script. [H] HP 0: no shipped script sets a mesh HP, so
    // the first damaging hit kills the child.
    // hp: the GoMesh's own +0x98, from the SOB record (FUN_00501a30; SceneTrigger::hp), taken on the first hit.
    // The 0x186 child (FUN_005735b0) copies +0x94..+0xcc: HP x10 (DAT_006bd928), DEF, resists, +0xce.
    struct HitMesh { float r = 1.0f; Vec3 off{}; float hp = 1.0f; bool hp_set = false; bool ran = false; RehitBank bank; };
    std::unordered_map<std::string, HitMesh> hit_meshes;
    auto reload_room = [&](bool rerun_events = true) {
        auto it = g.sob_index.find(room.name);
        if (it == g.sob_index.end()) return;
        const std::unordered_set<std::string> gone = broken_in(room.name);
        Room next = load_room(g, asset_root, it->second, game.g_flags, character,
                              gone.empty() ? nullptr : &gone, &live_obj);
        free_room(room, g.white_bind);
        room = std::move(next);
        load_meshes(/*keep*/true);
        apply_load_effects(room);
        if (rerun_events) room_events_dirty = true;   // re-run scripts for the re-gated room
    };

    // Positional SFX: pan from the camera's right axis, distance from the listener. The listener
    // (FUN_0042a3d0 [EXPORT 0042a3d0.c:331-341]) sits 0.175 (f32 0x6bd1f4) of the way from the camera's
    // evaluated target 0x76a160.. to its eye 0x76a16c.., facing along the camera's yaw. [not modelled]
    // FUN_0045d7a0's pull of the emitter toward the object 0x749d44 while 0x76bc1c > 0. The pan-law
    // caveat lives on Audio::play_at.
    const bool sfx_log = std::getenv("CLERIA_SFXLOG") != nullptr;   // debug: the SE / hit-effect ids
    std::vector<int> sfx_loops;                                     // ... and the looping ids, on a change
    audio.set_trace(sfx_log);                                       // ... and the 2-D requests (`[se]`)
    auto sfx_at = [&](int id, const Vec3& p, float vol) {
        const Vec3 f = cam.forward();
        const float rl = std::sqrt(f.x * f.x + f.z * f.z);
        const float rx = rl > 1e-4f ? f.z / rl : 0.0f;     // right = (fz, -fx) on XZ
        const float rz = rl > 1e-4f ? -f.x / rl : 1.0f;
        const Vec3 ce = g_cam_out.eye, ct = g_cam_out.target;
        const Vec3 ear = g_cam.on ? Vec3{ct.x + (ce.x - ct.x) * 0.175f, ct.y + (ce.y - ct.y) * 0.175f, ct.z + (ce.z - ct.z) * 0.175f} : feet;
        const float dx = p.x - ear.x, dy = p.y - ear.y, dz = p.z - ear.z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float pan = dist > 1e-4f ? (dx * rx + dz * rz) / dist : 0.0f;
        if (sfx_log) std::printf("  [sfx] t%llu %d at (%.2f, %.2f, %.2f)\n", (unsigned long long)tick_count, id, p.x, p.y, p.z);
        audio.play_at(id, pan, dist, vol);
    };
    // anime_SePlay3D (funcid 42) of an actor other than the player, FUN_004472e0 case 0x2a [BYTES 0x447d72..
    // 0x447dd5]: a 3-D record (rec+0 & 0x10) plays at the actor (FUN_0045d7a0), any other one as a plain 2-D SE
    // (FUN_005ce2a0, volume 0x400).
    auto aso_se_at = [&](int id, const Vec3& p) {
        const EffectDef* d = audio.defs().find(id);
        if (d && d->is3d) sfx_at(id, p, kSfxAnimVol);
        else audio.play(id, kSfxAnimVol);
    };
    // FUN_0044ebb0's SEs of the effects 0x83 (150) and 0x1f7 (539), at the struck particle (FUN_0043a080).
    fxsim.set_impact_se([&](int se, const Vec3& at) { aso_se_at(se, at); });

    // ---- save / load (PLAN.md Phase 2; codec src/cleria/save.*) -------------------
    // The goddess statue is all script: the sealed model runs the room's SAVE.XSO (the
    // cleanse), the cleansed one S_COMMON/SAVE.XSO, whose menu runs @SaveMenu (then 0xe0
    // SaveMenu_Enter opens the book's Save page), @GrowMenu (the blessing shop) or @SaveBack.
    auto capture_save = [&]() -> SaveState {
        SaveState s;
        std::memcpy(s.flags, game.g_flags, sizeof s.flags);
        std::memcpy(s.item_arr, game.item_arr, sizeof s.item_arr);
        s.hp_max = float(player.max_hp); s.hp_cur = float(player.hp);
        s.exp = player.exp; s.level = player.level; s.sp = player.sp;
        s.mp_max = player.sp_max; s.mp_cur = player.sp_cur;
        s.accessory = inv.accessory; s.held = inv.held; s.magic = inv.magic;
        for (int c = 0; c < 4; ++c) s.tier[c] = inv.tier[c];
        s.hp_bonus = inv.hp_bonus; s.atk_buff = inv.atk_buff; s.def_buff = inv.def_buff;
        s.luck_base = inv.luck_base; s.timer_atk = inv.timer_atk; s.timer_def = inv.timer_def;
        s.mp_buff = inv.mp_buff; s.timer_mp = inv.timer_mp;
        s.o2 = oxy.o2; s.o2_alpha = oxy.alpha;                  // 0x76c1b8 / 0x76c1bc, in the block
        s.ail[0] = std::int32_t(ail.mask);                      // g_flags 528..532, in the block
        for (int i = 0; i < 4; ++i) s.ail[i + 1] = ail.t[i];
        s.boost = player.boost;                                 // 0x76c1b4, in the block
        std::memcpy(s.warp_on, warp_on, sizeof warp_on);
        std::memcpy(s.manual_on, manual_on, sizeof manual_on);
        s.scene = room.name;
        s.pos_x = feet.x; s.pos_y = feet.y; s.pos_z = feet.z;   // FUN_00443d70 [not modelled] the +0x414 term
        // 0x76c148 is the actor heading +0x314 in RADIANS, engine basis (0 = +X): our yaw (0 = +Z,
        // forward = (sin, cos)) is pi/2 - it (FUN_00443d70; ARRIVAL_AUDIT.md "A load is the load warp").
        s.yaw = 1.5707963f - std::atan2(face_dir.x, face_dir.z);
        return s;
    };
    // FUN_00574970 (SaveMenu_Enter): the slot's area line 0x74c0ac (31 chars, header +0x90). A
    // switch on the scene index names the boss rooms and a few others; every other scene takes
    // its SCENELIST display string (+0xc4), cut after a ':' (sprintf "%s" of the text 2 past it).
    auto save_area_line = [&]() -> std::string {
        const SceneRec* sr = scenes.find_room(room.name);
        if (!sr) return room.name;                       // [STAND-IN] a room without a record
        switch (sr->index) {                             // [BYTES 0x69fe58..0x6a0054]
            case 0x1b: return character == 3 ? "Tower Entrance" : " 1F - Search Party's Base";
            case 0x24: return " 4F - Wailing Blue";
            case 0x26: return " 5F - Beast's Chamber";
            case 0x31: return " 6F - Flooded Prison";
            case 0x3d: return " 9F - Arthropod's Chamber";
            case 0x3e: return " 7F - Flooded Prison 2";
            case 0x46: return " 8F - Flooded Prison 3";
            case 0x48: return "10F - Guilty Fire";
            case 0x4e: return "11F - Guilty Fire 2";
            case 0x56: return "13F - Construct's Chamber";
            case 0x57: return "12F - Devil's Corridor";
            case 0x63: return "14F - Silent Sands";
            case 0x77: return "17F - Creeper's Chamber";
            case 0x80: return "Rado's Annex";
            case 0x81: return "18F - Blighted Blood";
            case 0x8b: return "20F - Blighted Blood 2";
            case 0x8f: return "21F - Mantid's Chamber";
            case 0x99: return "22F - Demonic Core";
            case 0x9e: return "23F - Demonic Core 2";
            case 0xa4: return "25F - Devil's Throne";
            case 0xa6: return "24F - Hall of Reflection";
            case 0xac: return "Tower Summit";
            default: break;
        }
        std::string d = sr->display;
        const std::size_t k = d.find(':');               // FUN_00627fe0(buf, ":")
        if (k != std::string::npos) d = k + 2 <= d.size() ? d.substr(k + 2) : std::string();
        return d.substr(0, 0x1f);
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
        const SaveRecord rec = to_record(capture_save(), save_area_line(), date, tim);
        // Keys: any nonzero pair — the engine rerolls them per save too (SAVELOAD §4.1).
        std::random_device rd;
        const std::vector<std::uint8_t> raw = rec.encode(rd() | 1, rd() | 1);
        std::error_code ec;
        fs::create_directories(save_dir(), ec);
        std::ofstream f(slot_path(slot), std::ios::binary);
        f.write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size()));
        hud.debug_message(f ? ("Saved to slot " + std::to_string(slot + 1))
                      : "Save FAILED (disk?)");
        if (f) content_save(slot_path(slot), slot);      // mods: the sidecar beside it (view_content.inl)
        return bool(f);
    };
    // force_scene: re-enter the scene even when it is the current one (the game over's Retry is a
    // ChangeScene to the current scene; a load of the same room only re-gates it).
    auto apply_loaded = [&](const SaveState& s, bool force_scene = false) {
        ail.mask = std::uint32_t(s.ail[0]);            // the record's g_flags 528..532 (STATUS_AILMENTS.md §7)
        for (int i = 0; i < 4; ++i) ail.t[i] = s.ail[i + 1];
        ail_reload = true;                             // ail_tick: the effects follow the loaded mask
        // The Boost gauge 0x76c1b4 comes back with the block (it kept the value of the moment of the load).
        // [not modelled] the mode word 0x76c198 (a Boost running at the snapshot).
        player.boost = s.boost;
        std::memcpy(game.g_flags, s.flags, sizeof s.flags);
        std::memcpy(game.item_arr, s.item_arr, sizeof s.item_arr);
        for (int c = 0; c < 3; ++c) game.g_flags[297 + c] = g_clear.clear[std::size_t(c)];   // the raw clear bytes (FUN_0042f5e0)
        character = std::clamp(s.character(), 1, 3);
        player.level = std::clamp(s.level, 1, 60);
        player.exp = s.exp; player.sp = s.sp;
        if (s.mp_max > 0.0f) { player.sp_max = s.mp_max; player.sp_cur = std::min(s.mp_cur, s.mp_max); }
        inv.accessory = s.accessory; inv.held = s.held; inv.magic = s.magic;
        for (int c = 0; c < 4; ++c) inv.tier[c] = s.tier[c];
        inv.hp_bonus = s.hp_bonus; inv.atk_buff = s.atk_buff; inv.def_buff = s.def_buff;
        inv.luck_base = s.luck_base; inv.timer_atk = s.timer_atk; inv.timer_def = s.timer_def;
        inv.mp_buff = s.mp_buff; inv.timer_mp = s.timer_mp;
        // O2 and its gauge come back with the block (a load, and the Retry's continue snapshot).
        // [H] 0 / 0 is a CleriaCore save from before they were stored (retail never holds that pair past
        // the new game's first tick: a drained gauge has alpha 255): treat it as a full gauge.
        oxy.o2 = s.o2; oxy.alpha = s.o2_alpha;
        if (s.o2 == 0.0f && s.o2_alpha == 0) oxy.o2 = Oxygen::kMax;
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
        if (!s.scene.empty() && (force_scene || s.scene != room.name)) transition(s.scene);
        else reload_room();
        // Position: the continue snapshot (0x76c13c/140/144) is the "load" warp's arrival
        // (FUN_0042f5e0 0x42f95b..0x42f9cb -> 0x769fbc), placed by FUN_00434020 -> FUN_00459ff0
        // like every arrival: the floor below the saved point, snapped within 1.5.
        feet = {s.pos_x, placed_floor({s.pos_x, s.pos_y, s.pos_z}), s.pos_z};
        // [H] not retail: CleriaCore saves written before y was stored hold 0 at 0x76c140; with no
        // floor under (x, 0, z) re-ground them on the room's top surface as before.
        if (s.pos_y == 0.0f && floor_at(phys_mesh(room), feet.x, feet.z, 0.0f) < -1.5f) {
            const float fl = floor_at(phys_mesh(room), feet.x, feet.z, room.mesh.bmax.y + room_diag(room));
            if (fl > kNoFloor) feet.y = fl;
        }
        pv = VerticalState{}; pvel = {}; last_floor = feet.y;
        const float rad = 1.5707963f - s.yaw;              // the engine heading (radians) -> our yaw
        face_dir = {std::sin(rad), 0, std::cos(rad)};
    };

    // ## In-engine cutscene runner (CUTSCENE_PLAN.md B3/B9) — MVP: one debug-triggered
    // script driven by the resumable XSO VM (CUTSCENE_COROUTINE.md). The host receives the
    // dialogue ops and gates Wait_Prompt on "text fully revealed + advance pressed"; the
    // fixed-step loop ticks the coroutine and FREEZES the world while it runs (op 0xb6 Fix).
    struct CutsceneHost : XsoHost {
        std::string name, text;
        int speaker = 0;
        bool box_active = false;   // a window is up (the message box or a dialog)
        // The event windows (src/cleria/wnddialog.hpp, WNDDIALOG_DRAW.md). `last_win` is the
        // script slot's window (1 = message box, 2 = dialog): Wait_Prompt arms and polls it.
        WndText wtext;
        WndDialog wdlg;
        int last_win = 0;
        RichMetrics wmetrics;
        int character = 1;
        std::function<int(int)> class_fn;                 // speaker -> entity class (-1 none)
        std::function<void(const std::string&)> start_label;   // a chosen menu record's script
        std::vector<std::string> menu_texts, menu_labels;
        std::string menu_cancel, last_label;
        bool menu_last = false, menu_ran = false;
        bool advance = false;     // set by input; consumed to advance one line
        ScriptManager* mgr = nullptr;   // for Wait_Script (is the named child still live?)
        int voice = -1;           // Window_Msg param1 — doubles as the FACE FRAME index
        // FUN_00573e60: an empty NAME defaults to the speaker's cast record name (+0x10).
        std::function<std::string(int)> speaker_name_fn;
        void window_msg(int sp, int vc, const std::string& nm, const std::string& tx) override {
            // FUN_00573e60: an unresolvable speaker (no entity, id > 0xa5) shows nothing.
            const int cls = sp == 0 ? 0 : (class_fn ? class_fn(sp) : sp);
            if (sp != 0 && cls < 0) return;
            speaker = sp; voice = vc;
            name = (nm.empty() && sp != 0 && speaker_name_fn) ? speaker_name_fn(sp) : nm;
            text = tx;
            if (wdlg.alive()) wdlg.close();               // Window_Msg replaces a dialog
            wtext.message(cls, character, vc, name, tx, wmetrics);
            last_win = 1; box_active = true;
        }
        void window_close() override {                    // 0xd2: the windows fade out by themselves
            wtext.close(); wdlg.close(); last_win = 0;
        }
        void open_dialog_common() {
            if (wtext.alive()) wtext.close();             // a dialog replaces the message box
            last_win = 2; box_active = true; menu_ran = false;
        }
        void window_menu(float x, float y, const std::string& title, const std::vector<std::string>& texts,
                         const std::vector<std::string>& labels) override {
            open_dialog_common();
            wdlg.menu(x, y, title, texts, labels, wmetrics, false);
        }
        void menu_init() override {                       // 0xd7: (512, 282), title "\p"
            menu_texts.clear(); menu_labels.clear(); menu_cancel.clear(); menu_last = false;
        }
        void menu_add(const std::string& t, const std::string& l) override { menu_texts.push_back(t); menu_labels.push_back(l); }
        void menu_assign_cancel(const std::string& l) override { menu_cancel = l; }
        void menu_cursor_last() override { menu_last = true; }
        void menu_rename_last(const std::string& l) override { last_label = l; }   // 0xdc -> 0x75ad98
        std::function<void()> save_menu_fn;
        void save_menu_enter() override { if (save_menu_fn) save_menu_fn(); }
        std::function<int()> gold_get_fn;
        std::function<void(int)> gold_set_fn;
        std::function<void(int, int)> item_arr_fn;
        int gold() const override { return gold_get_fn ? gold_get_fn() : 0; }
        void set_gold(int v) override { if (gold_set_fn) gold_set_fn(v); }
        void item_arr_set(int i, int v) override { if (item_arr_fn) item_arr_fn(i, v); }
        // The dialog's cursor step and cancel edge this frame (keys / CLERIA_DRIVE up, down, esc).
        int wnd_vstep = 0;
        bool wnd_cancel = false;
        void menu_select(const std::string& title) override {   // 0xd9: lay out, centre vertically
            open_dialog_common();
            wdlg.menu(512, 282, title, menu_texts, menu_labels, wmetrics, true);
            for (std::size_t k = 0; k < menu_labels.size(); ++k) {
                if (!menu_cancel.empty() && menu_labels[k] == menu_cancel) wdlg.set_cancel(int(k) + 1);
                if (menu_last && menu_labels[k] == last_label) wdlg.set_cursor(int(k) + 1);
            }
        }
        void set_msg_speed(int v) override { wtext.set_speed(std::uint32_t(v)); }
        // 0x8c Flag_IsEquip (FUN_00443ce0) and 0x8b EventXX's unequip sub-kinds (KEY_ITEMS.md §5).
        std::function<bool(int)> equipped_fn;
        std::function<void(int)> event_xx_fn;
        bool is_equipped(int id) const override { return equipped_fn && equipped_fn(id); }
        void event_xx(int n) override { if (event_xx_fn) event_xx_fn(n); }
        // FUN_00578960: a menu accept runs the chosen record's label script on the owner.
        void dispatch_menu() {
            if (menu_ran || !wdlg.is_menu() || !wdlg.accepted()) return;
            menu_ran = true;
            last_label = wdlg.chosen();                   // 0x75ad98
            if (!last_label.empty() && start_label) start_label(last_label);
        }
        // The "Acquired X" box (op 0xd5 Window_Treasure): a GoWndDialog with the item icon, its
        // top edge at the op's y (WNDDIALOG_DRAW.md §4). The item name fills a NULL string.
        std::function<std::string(int)> item_name_fn;
        void acquire_item(float x, float y, int id, const std::string& tx) override {
            // FUN_005781f0: the item NAME is a fallback only for a NULL string (audit event_vm #27).
            const std::string t = !tx.empty() ? tx : (item_name_fn ? item_name_fn(id) : ("item " + std::to_string(id)));
            open_dialog_common();
            wdlg.treasure(x, y, id, t, wmetrics);
        }
        // 0xe2 the area-name card and 0xe3..0xe8 the busts (eventsprites.hpp, WNDDIALOG_DRAW.md §7-§8).
        CaptionCard caption_card;
        Busts bust_set;
        void caption(const std::string& nm) override { caption_card.open(nm); }
        // Standing bust portraits (0xe3/0xe4/0xe5). The intro shows one at a time (t_840a the
        // captain, then v_003a the villain), but hold a small list so overlapping opens work.
        void portrait_open(const std::string& nm, float x, float y) override { bust_set.open(nm, x, y); }
        std::function<void(int, int, const std::string&)> get_item_fn;   // 0x116 -> the chest item
        std::function<void(const std::string&, int, int)> drop_item_fn;  // 0x17e -> a world pickup
        void mesh_drop_item(const std::string& mesh, int item, int keep) override {
            if (drop_item_fn) drop_item_fn(mesh, item, keep);
        }
        void effect_get_item(int item, int flag, const std::string& mesh) override {
            if (get_item_fn) get_item_fn(item, flag, mesh);
        }
        void portrait_move(const std::string& nm, float x, float y, int frames, int curve) override {
            bust_set.move(nm, x, y, frames, curve);
        }
        void portrait_close(const std::string& nm) override { bust_set.close(nm); }
        void portrait_color(const std::string& nm, const std::uint32_t* c) override { bust_set.colour(nm, c); }
        void portrait_priority(const std::string& nm, int pr) override { bust_set.priority(nm, pr); }
        void portrait_fade_speed(const std::string& nm, float sp) override { bust_set.fade_speed(nm, sp); }
        // SKIP: while set, EVERY wait predicate reports "done" so every live script
        // bursts to its end (its side effects — flags/spawns/warps — still run). This
        // clears not just Wait_Prompt but Wait_Script / Wait_Window / Wait_Sound /
        // Wait_Fader too, which a plain Wait_Time collapse can't. Set by the skip loop.
        bool skipping = false;
        // FUN_005741b0: arm the slot's window and return its "accepted"; with no window,
        // proceed on a Confirm press.
        bool wait_prompt() override {
            if (skipping) return true;
            if (last_win == 2 && wdlg.alive()) { wdlg.arm(); return wdlg.accepted(); }
            if (last_win == 1 && wtext.alive()) { wtext.arm(); return wtext.accepted(); }
            if (advance) { advance = false; return true; }
            return false;
        }
        // Screen fader (EVENTVM_HANDLERS.md §4): a fullscreen colored overlay, alpha
        // ramping current->target at fade_rate/frame. fade_advance() steps it each tick.
        // fade_rate boots at 0.05 (FUN_00442260 -> FUN_00439170: 0x769f28 = 0x3d4ccccd), as every scene
        // entry re-arms it; the door warp's fade-out runs at whatever rate stands.
        float fade_cur = 0, fade_tgt = 0, fade_rate = 0.05f;
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
        void mesh_set_rotation(const std::string& n, float x, float y, float z) override {
            if (meshes) meshes->set_rotation(n, {x * 0.0174533f, y * 0.0174533f, z * 0.0174533f});
        }
        void mesh_reset_rotation(const std::string& n) override { if (meshes) meshes->reset_rotation(n); }
        std::function<void(int)> eft_die_fn;
        void eft_die(int id) override { if (eft_die_fn) eft_die_fn(id); }
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
        // 0xf5 / 0xf6 Wait_WindowOpen / Close: the message box exists / is gone (it fades out
        // ~14 frames after Window_Close; STATUE_CLEANSE.md D7).
        bool wait_window(bool want_open) override {
            if (skipping) return true;
            return want_open ? wtext.alive() : !wtext.alive();
        }
        // 0xcd CrossFade(start, rate): opacity `start`, += `rate` per step, a frame captured when
        // start != 0 (0x76a548 / 0x76a54c / 0x7b2488; STATUE_CLEANSE.md §2).
        float cf_alpha = 0, cf_rate = 0;
        void crossfade(float start, float rate) override {
            cf_alpha = std::clamp(start, 0.0f, 1.0f);
            cf_rate = rate;
        }
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
            // A child StartScript'ed in this burst is still queued (it runs in the drain after
            // the burst; the engine runs it inside the burst): it counts as live
            // (STATUE_CLEANSE.md W6 — `StartScript(X); Wait_Script(X)` must wait).
            for (const auto& q : mgr->spawn_queue) {
                if (q.name.size() != n.size()) continue;
                bool same = true;
                for (std::size_t k = 0; k < n.size(); ++k)
                    if (std::tolower((unsigned char)q.name[k]) != std::tolower((unsigned char)n[k])) { same = false; break; }
                if (same) return false;
            }
            return true;
        }
        void scroll_moveto(float x, float y, float z, int frames) override { ::scroll_moveto(x, y, z, frames); }
        void scroll_return(int frames) override { ::scroll_return(frames); }
        void shake(float ax, float ay, float az, int frames) override { ::shake(ax, ay, az, frames); }
        void camera_set(int region, int frames) override { ::camera_set(region, frames); }
        void camera_return(int frames) override { ::camera_return(frames); }
        void camera_copy_set(int id) override { ::camera_copy_set(id); }
        void camera_change(float d, float y, float p, float f, int frames) override { ::camera_change(d, y, p, f, frames); }
        XsoHost* worldHost = nullptr;
        // The effect ops go to the EF2 sim (place_named_fx / kill_named_fx): a = scale, b = yaw.
        std::function<void(int, float, float, float, float, float, const std::string&)> locate_fx;
        std::function<void(const std::string&)> kill_fx;
        void kill_effect(const std::string& n) override { if (kill_fx) kill_fx(n); }
        void locate_effect(int id, float x, float y, float z, float a, float b,
                           const std::string& n) override {
            if (locate_fx) locate_fx(id, x, y, z, a, b, n);
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
        // 998 (0x3e6) = the running script's bound actor (+0x240).
        int rid(int id) const {
            if (id == 998 && mgr) id = mgr->vm.owner;
            return id == 999 ? 13 : id;
        }
        std::function<void(float)> player_face_fn;          // degrees (Falcom facing)
        std::function<void(int, int, float)> emotion_fn;    // (owner, kind, yoff)
        std::function<bool(int, Vec3*)> actor_pos_fn;       // an id's feet (13 = the player)
        void chr_emotion(int id, int kind, float yoff) override { if (emotion_fn) emotion_fn(rid(id), kind, yoff); }
        void chr_shadow(int id, bool on) override { if (worldHost) worldHost->chr_shadow(rid(id), on); }
        bool p_isplayer(int id) const { id = rid(id); return id == 13 || id == 999; }
        std::function<void(float)> player_jump_fn;
        void chr_jump(int id, float v) override {
            if (p_isplayer(id)) { if (player_jump_fn) player_jump_fn(v); }
            else if (worldHost) worldHost->chr_jump(rid(id), v);
        }
        void chr_locate_dummy(int id, float x, float y, float z) override {
            if (worldHost) { worldHost->chr_locate_dummy(id, x, y, z); world_changed = true; }
        }
        // The player's +0x30c is CASTINFO[13]+0x48 = 0.056 for its whole life (nothing in
        // FUN_00465170 rewrites it); a scripted walk is 0.056 x scale x Movable per step.
        float player_speed30c = 0.056f;      // set from CASTINFO[13]+0x48 below
        std::function<float()> movable_fn;   // the player's ASO anime_Movable
        Vec3 p_move_tgt{}; float p_move_spd = 0, p_move_r2 = 0; bool p_moving = false;
        // The player's Chr state (look block, pattern, tremble, run bit) lives on the world's
        // player actor, which every Chr op reaches through worldHost (find(13) = player_); the
        // view turns and draws her from it (CHR_OPS.md §11). Null before CAST_ADOL spawns.
        std::function<Actor*()> wplayer_fn;
        Actor* wplayer() const { return wplayer_fn ? wplayer_fn() : nullptr; }
        // Chr_Pos on the player: FUN_00459ff0 with yaw = +0x308, so +0x314 = +0x360 = +0x308.
        std::function<void(float)> player_heading_fn;   // snap her facing to an engine heading
        void chr_pos(int id, float x, float y, float z) override {
            if (p_isplayer(id)) {
                if (playerPosW) *playerPosW = {x, y, z};
                p_moving = false;
                if (Actor* wp = wplayer()) {
                    wp->look.dir = wp->look.cmd;
                    if (player_heading_fn) player_heading_fn(wp->look.cmd);
                }
            }
            else if (worldHost) worldHost->chr_pos(rid(id), x, y, z);
        }
        // Chr_Dir: +0x308 and look mode 0 on the actor; the turn happens per tick (the world's
        // tick_facing for NPCs, the view's player facing tick for her). Without a world player
        // the old instant face is kept.
        void chr_dir(int id, float deg) override {
            if (p_isplayer(id) && !wplayer()) { if (player_face_fn) player_face_fn(float(int(deg))); }
            else if (worldHost) worldHost->chr_dir(rid(id), deg);
        }
        // The player's MoveTo speed when +0x12c & 0x20 (Chr_RunOrWalk) arms the run stage +0x4ca:
        // x +0x410 (CASTINFO+0x4c = 2.2) x the grounded per-character constant (CHR_OPS.md §5,
        // WAITOPS §6.2(a); physics_player.hpp run_char_const). Set by the view.
        std::function<float()> player_run_factor_fn;
        void chr_moveto(int id, float x, float z, float radius, float scale) override {
            if (p_isplayer(id)) {
                p_move_tgt = {x, 0, z};
                p_move_spd = player_speed30c * scale;                 // 0.056 x scale (x Movable below)
                const float r = std::max(0.025f, radius);
                p_move_r2 = r * r; p_moving = true;
            } else if (worldHost) worldHost->chr_moveto(rid(id), x, z, radius, scale);
        }
        bool p_running() const {
            const Actor* wp = wplayer();
            return p_moving && wp && (wp->flags12c & 0x20u) != 0;
        }
        void chr_pattern(int id, int tag, int frame) override { if (worldHost) worldHost->chr_pattern(rid(id), tag, frame); }
        void chr_run(int id, bool run) override { if (worldHost) worldHost->chr_run(rid(id), run); }
        void chr_tremble(int id, int ticks, int period, float dx, float dy) override {
            if (worldHost) worldHost->chr_tremble(rid(id), ticks, period, dx, dy);
        }
        void chr_foot_sound(int id, int on) override { if (worldHost) worldHost->chr_foot_sound(rid(id), on); }
        void chr_fly(int id, bool on) override { if (worldHost) worldHost->chr_fly(rid(id), on); }
        void chr_flyto(int id, float x, float y, float z, float r, float sc) override {
            if (worldHost) worldHost->chr_flyto(rid(id), x, y, z, r, sc);
        }
        void chr_turn_speed(int id, float m) override { if (worldHost) worldHost->chr_turn_speed(rid(id), m); }
        void chr_look_dir(int id, float deg) override { if (worldHost) worldHost->chr_look_dir(rid(id), deg); }
        void chr_dir_chr(int id, int target) override {
            if (worldHost) worldHost->chr_dir_chr(rid(id), rid(target));
        }
        bool chr_moving(int id) const override {
            if (p_isplayer(id)) return p_moving;
            return worldHost && worldHost->chr_moving(rid(id));
        }
        // The player's Chr_Alpha fades the world player actor's +0x4d8 (drawn by the view).
        void chr_alpha(int id, float a0, float a1, int frames) override {
            if (worldHost) worldHost->chr_alpha(rid(id), a0, a1, frames);
        }
        void chr_attach_script(int id, const std::string& sc) override {
            if (worldHost) worldHost->chr_attach_script(rid(id), sc);
        }
        void chr_set_status(int id, int lv, const float* f) override {
            if (worldHost) worldHost->chr_set_status(rid(id), lv, f);
        }
        std::function<void(const std::string&, bool, bool)> mesh_state_fn;   // (name, collision?, on)
        void mesh_visible(const std::string& n, bool on) override { if (mesh_state_fn) mesh_state_fn(n, false, on); }
        void mesh_collision(const std::string& n, bool on) override { if (mesh_state_fn) mesh_state_fn(n, true, on); }
        std::function<void(const ObjLocateArgs&)> mesh_locate_fn;
        std::function<void(int)> bgm_default_fn, bgm_play_fn, bgm_stop_fn;
        std::function<void()> heal_fn;
        std::function<void(int)> muteking_fn;
        void mesh_locate(const ObjLocateArgs& a) override { if (mesh_locate_fn) mesh_locate_fn(a); }
        std::function<void(const std::string&, float, float, float, float)> mesh_sphere_fn;
        void mesh_sphere_collision(const std::string& n, float r, float ox, float oy, float oz) override {
            if (mesh_sphere_fn) mesh_sphere_fn(n, r, ox, oy, oz);
        }
        void mesh_fall(const std::string& n, float e, float h) override {
            if (meshes) { std::string ln = n; for (char& ch : ln) ch = char(std::tolower((unsigned char)ch)); meshes->fall(ln, e, h); }
        }
        void mesh_alpha_target(const std::string& n, float a, int fr) override {
            if (meshes) { std::string ln = n; for (char& ch : ln) ch = char(std::tolower((unsigned char)ch)); meshes->alpha_target(ln, a, fr); }
        }
        // 0x188 / 0x189 (meshobj.hpp). The frame heights and the +0x298 bits are the room's.
        std::function<void(const std::string&)> mesh_frame_y_fn;
        std::function<void(const std::string&, const std::string&)> mesh_sync_bits_fn;
        void mesh_distance_alpha(const std::string& a, const std::string& b, float div, float k) override {
            if (!meshes) return;
            if (mesh_frame_y_fn) { mesh_frame_y_fn(a); mesh_frame_y_fn(b); }
            meshes->distance_to_alpha(a, b, div, k);
        }
        void mesh_sync_pos(const std::string& a, const std::string& b, bool x, bool y, bool z) override {
            if (meshes) meshes->sync_pos(a, b, x, y, z);
            if (mesh_sync_bits_fn) mesh_sync_bits_fn(a, b);
        }
        void bgm_default(int id) override { if (bgm_default_fn) bgm_default_fn(id); }
        std::function<void(float, float)> scene_clip_fn;
        void scene_clip(float n, float f) override { if (scene_clip_fn) scene_clip_fn(n, f); }
        void bgm_play(int id) override { if (bgm_play_fn) bgm_play_fn(id); }
        void bgm_stop(int t) override { if (bgm_stop_fn) bgm_stop_fn(t); }
        void heal_hp() override { if (heal_fn) heal_fn(); }
        void chr_muteking(int id, int n) override { if (p_isplayer(id) && muteking_fn) muteking_fn(n); }   // player only
        std::function<void()> save_cont_fn;
        void save_cont_data() override { if (save_cont_fn) save_cont_fn(); }
        std::function<void(int)> se_fn, set_skill_fn;
        std::function<void(int, int)> bgm_volume_fn;
        void se_play(int se) override { if (se_fn) se_fn(se); }
        std::function<void(int, int)> se_vol_fn;
        std::function<void(int, int, int)> se_volume_fn;
        void se_play_vol(int se, int vol) override { if (se_vol_fn) se_vol_fn(se, vol); }
        void se_volume(int se, int vol, int ms) override { if (se_volume_fn) se_volume_fn(se, vol, ms); }
        void bgm_volume(int v, int t) override { if (bgm_volume_fn) bgm_volume_fn(v, t); }
        void set_skill(int i) override { if (set_skill_fn) set_skill_fn(i); }
        // 0xbf Se_Stop, 0x118 Eft_LocateChr, 0x11c Eft_SetAlphaTarget (the view's lambdas), 0x158
        // Chr_ZTest (the world actor's flag), 0x10a GlareLevel: the byte 0x76c1c5 only [not modelled:
        // there is no glare/bloom pass].
        std::function<void(int, int)> se_stop_fn;
        std::function<void(int, int, float, float, float, float, int, const std::string&)> locate_chr_fx;
        std::function<void(const std::string&, float, int)> fx_alpha_fn;
        int glare = 0;
        void se_stop(int id, int ms) override { if (se_stop_fn) se_stop_fn(id, ms); }
        void locate_effect_chr(int eff, int id, float dist, float deg, float dy, float scale, int mode,
                               const std::string& n) override {
            if (locate_chr_fx) locate_chr_fx(eff, rid(id), dist, deg, dy, scale, mode, n);
        }
        void effect_alpha_target(const std::string& n, float a, int frames) override { if (fx_alpha_fn) fx_alpha_fn(n, a, frames); }
        // 0x11b (the view's lambda); 0x126 / 0x127 kept as state only [not modelled: the mirrored actor draw
        // pass of FUN_00473540 and the reflection cube map belong to the renderer].
        std::function<void(int, float)> fx_camoff_fn;
        bool mirror = false;
        std::string cubemap;
        void effect_camera_offset(int id, float off) override { if (fx_camoff_fn) fx_camoff_fn(id, off); }
        void mirror_floor(bool on) override { mirror = on; }
        void cube_map(const std::string& n) override { cubemap = n; }
        void change_fog(int idx, float speed) override { fog_script(idx, speed); }                       // 0x129
        bool mesh_is_motion_finish(const std::string& n, std::uint32_t k) const override {              // 0x17a
            bool found = false;
            return meshes && meshes->motion_finished(n, k, &found);
        }
        void mesh_material_color(const std::string& n, const std::string& mat, int r, int g, int b) override {   // 0x182
            if (meshes) meshes->set_material_color(n, mat, {float(r) / 255.0f, float(g) / 255.0f, float(b) / 255.0f});
        }
        // 0xaa Light_Directional (DALLES.md §2.2): the colour 0x76a570..78 multiplies the world's materials and the
        // actor sprites (g_scene_light, SCREEN_EFFECTS.md §4.3); the direction 0x76a564..6c is kept as state only
        // [not modelled: the lit models' N.L / hemisphere term].
        Vec3 light_rgb{1.0f, 1.0f, 1.0f}, light_dir{};
        void light_directional(std::uint32_t argb, float x, float y, float z) override {
            light_rgb = {float((argb >> 16) & 0xffu) / 255.0f, float((argb >> 8) & 0xffu) / 255.0f, float(argb & 0xffu) / 255.0f};
            light_dir = {x, y, z};
        }
        void mesh_rotate(const std::string& n, float x, float y, float z, int frames) override {        // 0x172
            if (meshes) meshes->rotate(n, {x, y, z}, frames);
        }
        void glare_level(int v) override { glare = v; }
        void chr_ztest(int id, bool on) override { if (worldHost) worldHost->chr_ztest(rid(id), on); }
        // The look modes (CHR_OPS.md §6): the world writes the look block (the player's too, on
        // the world player actor) and the facing turns per tick. Without a world player the old
        // instant face is kept.
        void chr_lookat(int id, int target) override {
            const int t = target == 0 ? 0 : rid(target);
            if (p_isplayer(id) && !wplayer()) {
                Vec3 me{}, to{};
                if (actor_pos_fn && actor_pos_fn(13, &me) && actor_pos_fn(t, &to) && player_face_fn &&
                    (to.x != me.x || to.z != me.z))
                    player_face_fn(std::atan2(to.x - me.x, to.z - me.z) * 57.29578f);
            } else if (worldHost) worldHost->chr_lookat(rid(id), t);
        }
        void chr_lookat_pos(int id, float x, float z) override {
            if (p_isplayer(id) && !wplayer()) {
                Vec3 me{};
                if (actor_pos_fn && actor_pos_fn(13, &me) && player_face_fn && (x != me.x || z != me.z))
                    player_face_fn(std::atan2(x - me.x, z - me.z) * 57.29578f);
            } else if (worldHost) worldHost->chr_lookat_pos(rid(id), x, z);
        }
        // 0x151 Chr_Animate (FUN_00571000): the ASO motion +0x994 and the pattern pin released,
        // on NPCs and the player alike (the view's ASO tick plays the motion). [not modelled] the
        // player's re-bind FUN_00464fc0 (only for the literal id 13).
        void chr_animate(int id, int motion) override {
            if (worldHost) worldHost->chr_animate(rid(id), motion);
        }
        // Advance the player's cutscene walk one frame (NPCs advance in the world). FUN_00465170
        // 0x465819..0x4658d5: +0x308 = atan2(dz, dx), the step goes along +0x308 and her facing
        // TURNS toward it in the view's facing tick (0.18, or 0.35 on the run stage); on arrival
        // +0x308 = +0x314. Without a world player the old instant face is kept.
        Vec3* playerPosW = nullptr; float* playerYawDeg = nullptr;
        std::function<float()> player_heading_now_fn;   // her current facing, engine heading
        void advance_player_move() {
            if (!p_moving || !playerPosW) return;
            Actor* wp = wplayer();
            const float dx = p_move_tgt.x - playerPosW->x, dz = p_move_tgt.z - playerPosW->z;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (dx * dx + dz * dz <= p_move_r2) {                         // planar arrival test first
                p_moving = false;
                if (wp && player_heading_now_fn) wp->look.cmd = player_heading_now_fn();
                return;
            }
            float st = p_move_spd * (movable_fn ? movable_fn() : 1.0f);
            if (p_running() && player_run_factor_fn) st *= player_run_factor_fn();
            if (wp) {
                wp->look.cmd = float(std::atan2(double(dz), double(dx)));
                playerPosW->x += std::cos(wp->look.cmd) * st; playerPosW->z += std::sin(wp->look.cmd) * st;
            } else {
                playerPosW->x += dx / d * st; playerPosW->z += dz / d * st;
                if (player_face_fn) player_face_fn(std::atan2(dx, dz) * 180.0f / 3.14159265f);
            }
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
            return b && b->contains(p_origin());
        }
        // The engine tests the actor's position +0x50/+0x54/+0x58 (FUN_00570df0, FUN_00570c10/c80/cf0,
        // FUN_00571ef0), and +0x54 is the ORIGIN height, floor + +0x324 (1.075 for the player), not the
        // feet the view keeps (ROUTE_F13_TOAL.md F-T1: S_2080's EVT2_2080 box starts 0.05 above the floor).
        Vec3 p_origin() const { return {playerPos->x, playerPos->y + origin_h, playerPos->z}; }
        // ops 0x139/0x13a/0x13b position source: like 0x13d, only the player (13) is
        // positioned in cutscene context; other ids -> no actor -> false.
        bool actor_pos(int entityId, float out[3]) const override {
            if (entityId != 13 || !playerPos) return false;
            out[0] = playerPos->x; out[1] = playerPos->y + origin_h; out[2] = playerPos->z;   // +0x54: the origin
            return true;
        }
        // op 0xc1 IsFix(): the ScriptManager's ref-counted Fix/world-freeze state IS the
        // engine's 0x75ad90 fix counter (there is no separate pause counter here yet).
        bool is_fix() const override { return mgr && mgr->world_frozen(); }
        const int* water_mode_p = nullptr;
        std::function<void(float, float, float, float, float)> water_wall_fn;   // 0x112 (BULLETS.md §3.14)
        void water_wall_hitchk(float x, float y, float z, float r, float st) override {
            if (water_wall_fn) water_wall_fn(x, y, z, r, st);
        }
        std::function<void(const FireBulletOp&)> fire_bullet_fn;            // 0x111 (view_duel.inl)
        void fire_bullet(const FireBulletOp& b) override { if (fire_bullet_fn) fire_bullet_fn(b); }
        std::function<void(int, int, float, float, float)> weather_fn;      // 0x125 / 0x122 / 0x123 (view_weather.inl)
        void fire_drop(int l) override { if (weather_fn) weather_fn(0x125, l, 0, 0, 0); }
        void frame_smoke(int l) override { if (weather_fn) weather_fn(0x122, l, 0, 0, 0); }
        void fire_blur(float a, float b, float c) override { if (weather_fn) weather_fn(0x123, 0, a, b, c); }
        std::function<void(float, float)> zoom_blur_fn;                     // 0xce (view_screenfx.inl)
        void zoom_blur(float s, float r) override { if (zoom_blur_fn) zoom_blur_fn(s, r); }
        bool is_underwater_world() const override { return water_mode_p && *water_mode_p == 2; }
        // ---- the floor 1..3 completion pass (build/event_re/FLOOR13_OPS.md; view_xso13.inl) ----
        const std::string* entry_label_p = nullptr;      // 0x76a474: the last committed door's label
        std::string entry_label() const override { return entry_label_p ? *entry_label_p : std::string(); }
        struct PendingDamage { float atk; bool b; int c; };
        std::vector<PendingDamage> player_damage;        // 0x15c on the player: applied in the hit pass
        void chr_damage(int id, float atk, bool b, int c) override {
            // [not modelled] a non-player target: every floor 1..3 site is 998 = the player that
            // stepped on / touched the object (the plate path binds the script to the player).
            if (p_isplayer(id) && !(mgr && mgr->world_frozen()))   // FUN_00571ae0: no fix held
                player_damage.push_back({atk, b, c});
        }
        bool* player_vanished_p = nullptr;               // the player's +0x60 & 0x80 cleared (0x14d)
        void chr_collision_vanish(int id, bool on) override {
            if (p_isplayer(id)) { if (player_vanished_p) *player_vanished_p = on; }
            else if (worldHost) worldHost->chr_collision_vanish(rid(id), on);
        }
        void chr_stop(int id) override {
            if (p_isplayer(id)) p_moving = false;
            else if (worldHost) worldHost->chr_stop(rid(id));
        }
        std::function<void(int)> pad_tilt_fn;
        void pad_tilt(int i) override { if (pad_tilt_fn) pad_tilt_fn(i); }
        std::function<void(int, const Vec3&)> se3d_fn;   // 3D SE at a point, 2D when not flagged 3D
        void se_play_pos(int se, float x, float y, float z) override { if (se3d_fn) se3d_fn(se, Vec3{x, y, z}); }
        void se_play_chr(int se, int id) override {      // FUN_0043a150: an absent actor plays nothing
            Vec3 p{};
            if (se3d_fn && actor_pos_fn && actor_pos_fn(rid(id), &p)) se3d_fn(se, p);
        }
        std::function<void(const std::string&, float)> fx_alpha_cur_fn;
        void effect_alpha_current(const std::string& n, float a) override { if (fx_alpha_cur_fn) fx_alpha_cur_fn(n, a); }
        void mesh_alpha_current(const std::string& n, float a) override {
            if (meshes) { std::string ln = n; for (char& ch : ln) ch = char(std::tolower((unsigned char)ch)); meshes->alpha_current(ln, a); }
        }
        std::function<void(const std::string&, const std::string&, int)> mesh_fx_fn;
        void mesh_attach_effect(const std::string& m, const std::string& f, int e) override { if (mesh_fx_fn) mesh_fx_fn(m, f, e); }
        std::function<void(float)> time_scale_fn;
        void set_time_scale(float s) override { if (time_scale_fn) time_scale_fn(s); }
        std::function<void(int, int)> weapon_level_fn;
        void weapon_level(int k, int v) override { if (weapon_level_fn) weapon_level_fn(k, v); }
        // 0x163 / 0x164: the +0x130 bits on the world actor (the player's too: find(13)).
        void chr_no_force(int id, bool on) override { if (worldHost) worldHost->chr_no_force(rid(id), on); }
        void chr_ani_disable(int id, bool on) override { if (worldHost) worldHost->chr_ani_disable(rid(id), on); }
        bool p_no_force() const { const Actor* wp = wplayer(); return wp && (wp->tag.bits & 0x10000u); }
        // 0x15e / 0x15f on the player (+0x5d0 / +0x5d8, counter +0x5dc), applied by the player step
        // (view_xso13_force.inl); an actor with NoForce (+0x130 & 0x10000) is skipped. [not modelled]
        // world actors (no force channel in their mover).
        struct PForce { float fx = 0, fz = 0; int n = 0; float fy = 0; } pforce;   // fy: +0x5d4 (the box force only)
        const bool* p_grounded = nullptr;
        float origin_h = 0.0f;                           // +0x324: the origin above the feet
        void force_in_box(const std::string& box, float fx, float fy, float fz, int frames, bool gonly) override {
            if (!roomPtr || !playerPos || p_no_force() || (gonly && !(p_grounded && *p_grounded))) return;
            const SceneBox* b = find_box(roomPtr->event_boxes, box);
            if (b && b->contains(p_origin())) pforce = {fx, fz, frames, fy};   // FUN_00571ef0: actor +0x54; +0x5d0 / d4 / d8 [EXPORT 00571ef0.c:45-48]
        }
        void force_to_point(float x, float y, float z, float s, int frames, float rmin, float rmax, bool gonly) override {
            if (!playerPos || p_no_force() || (gonly && !(p_grounded && *p_grounded))) return;
            const float dx = x - playerPos->x, dy = y - (playerPos->y + origin_h), dz = z - playerPos->z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 > rmax * rmax || rmin * rmin > d2) return;
            const float inv = d2 != 0.0f ? 1.0f / std::sqrt(d2) : 1.0f;
            pforce = {dx * inv * s, dz * inv * s, frames};
        }
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
            // StartScript2 (0xb8) hands "<dir>\<file>.xso", e.g. "s_31\s_3103\s_UseHammer.xso":
            // a path under data\map\ with its own extension (FLOOR3_ROUTE_AUDIT.md B4).
            if (!at && up.find_first_of("\\/") != std::string::npos) {
                for (char& c : up) if (c == '\\') c = '/';
                fs::path rel(up);
                if (rel.extension().empty()) rel += ".XSO";
                const fs::path p2 = common.parent_path() / rel;
                std::error_code ec2;
                if (!fs::exists(p2, ec2)) return nullptr;
                try { return content_script(p2); }        // mods: text.ini (view_content.inl)
                catch (const std::exception&) { return nullptr; }
            }
            const fs::path p = (at ? common : *dir) / (up + ".XSO");
            std::error_code ec;
            if (!fs::exists(p, ec)) return nullptr;    // engine would throw; we skip
            try { return content_script(p); }            // mods: text.ini (view_content.inl)
            catch (const std::exception&) { return nullptr; }
        }
    } csLoader;
    csLoader.dir = &room.dir;
    csLoader.common = asset_root / "MAP" / "S_COMMON";
    ScriptManager csMgr;
    csMgr.vm.host = &csHost;
    csMgr.vm.rs_ext = game.rs;   // 0x81 Rand draws the one game generator 0x74622c (FUN_00439120), not a copy
    csMgr.loader = &csLoader;
    csHost.mgr = &csMgr;
    scene_entry_fader = [&] {   // FUN_00439170 + 0x431881..0x4318a7 (transition() calls it on every entry)
        csHost.fade_rgb = 0; csHost.fade_cur = 1.0f; csHost.fade_tgt = 0.0f; csHost.fade_rate = 0.05f;
    };
    {   // mods (view_content.inl): the grant filter on the live VM and the game callbacks it needs
        content_install(csMgr.vm);
        g_content.add_sp = [&](int n) { player.sp = std::clamp(player.sp + n, 0, 999999); };
        g_content.heal_hp = [&](int n) { player.hp = std::min(player.max_hp, player.hp + n); };
        g_content.save_slot = [&](int s) { do_save(s); };
        g_content.load_slot = [&](int s) {
            std::ifstream f(slot_path(s), std::ios::binary);
            std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(f), {}};
            if (auto rec = SaveRecord::decode(raw)) {
                audio.stop_all(true);                    // FUN_00431260 "load": FUN_005ce430(1) [BYTES 0x43136a]
                content_load(slot_path(s), s); apply_loaded(from_record(*rec), true);
                csHost.fade_rgb = 0; csHost.fade_cur = 1.0f; csHost.fade_tgt = 0.0f; csHost.fade_rate = 0.05f;
                // FUN_00431260 "load": the continue snapshot, as the book's and the title's Load take it
                // (ROUTE_F13.md D5: without it Retry fell back to the stand-in respawn).
                if (csHost.save_cont_fn) csHost.save_cont_fn();
            }
        };
    }
    {   // Lua mods (view_lua.inl): the live game cleria.game / cleria.player reach
        LuaBind b;
        b.flags = game.g_flags; b.item_arr = game.item_arr; b.warp_on = warp_on; b.character = &character; b.player = &player;
        b.set_weapon_tier = [&](int t) { game.g_flags[148] = t; for (std::int32_t& x : inv.tier) x = t; };   // 0x7f x4
        b.set_level = [&](int l) {                       // [H] after FUN_004200F0 [MOD]: level, EXP at its threshold, stats
            player.level = std::clamp(l, 1, 60);
            if (ginfo.ok()) player.exp = float(ginfo.exp_to_level(player.level));
            player.max_hp = int(player_stats().max_hp);
            player.hp = std::min(player.hp, player.max_hp);
        };
        b.refresh = [&] { player.max_hp = int(player_stats().max_hp); };
        b.frozen = [&] { return csMgr.world_frozen(); };
        b.warp = [&](int i) { game.g_flags[137] = i; csMgr.vm.g_flags[137] = i; csHost.go_warp_point(); };   // 0xa6
        lua_bind(std::move(b));
    }
    LuaUnbind lua_unbind;                                // ... unbound when this game's scope ends (Return to Title)
    csHost.warp_on = warp_on;
    csHost.manual_on = manual_on;
    csHost.topic_seen = topic_seen;
    csHost.topic_kills = topic_kills;
    csHost.meshes = &meshes;
    csHost.player_floor = &player_floor_obj;
    csHost.worldHost = &world;
    csHost.locate_fx = place_named_fx;
    csHost.kill_fx = kill_named_fx;
    csHost.roomPtr = &room;         // op 0x13d tests the player vs the current room's .SEV boxes
    csHost.playerPos = &feet;
    csHost.playerPosW = &feet;      // Chr_Pos/Chr_MoveTo move the player's feet in a cutscene
    // Apply the flag writes the scripts recorded (only what they changed, so nothing the game set
    // independently is clobbered) to the game state, and clear them.
    auto apply_script_grants = [&] {
        for (const Grant& gr : csMgr.vm.grants)
            if (gr.has_value && gr.index >= 0 && gr.index < 0x200)
                game.g_flags[gr.index] = gr.value;
        csMgr.vm.grants.clear();
    };
    bool prev_frozen = false;
    bool skip_cutscene = false;   // set by ESC/Backspace during a frozen cutscene
    std::function<void()> after_cutscene;   // ran once when the world thaws (e.g. re-gate the room)

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
        hit_meshes.clear();                         // the root re-issues its 0x186s on every visit
        if (scene_objects_reset) {                  // FUN_00428ec0: the busts / V_ cards and the caption
            scene_objects_reset = false;            // card die with the scene (Toal's prologue never
            csHost.bust_set.clear();                // closes its V_109 backdrop)
            csHost.caption_card = CaptionCard{};
        }
        csMgr.paused = false;
        csHost.heal_spots.clear();
        csHost.mirror = false; csHost.cubemap.clear();   // FUN_00431260 clears 0x74c104 with the scene [M]
        // ... and sets the scene light back to (1, 1, 1) [EXPORT 00431260.c:113-133]; the room's SNF block then
        // gives its colour (Room::snf_light), the root script's Light_Directional after it.
        csHost.light_rgb = room.snf_light;
        fog_script_reset();                              // a new room: no script-selected fog record
        csHost.box_active = false; csHost.advance = false;
        camera_return();                            // no forced camera carries across rooms
        after_cutscene = nullptr; prev_frozen = false;
        if (!events_enabled) return;                 // safe default: no live room root
        std::copy(game.g_flags, game.g_flags + 0x200, csMgr.vm.g_flags);
        csMgr.vm.g_flags[150] = character;
        const fs::path xso = room.dir / (room.name + ".XSO");
        if (!fs::exists(xso)) return;
        try { csMgr.start_root(content_script(xso), room.name); }   // mods: text.ini
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
        try { s = content_script(p); }                     // mods: text.ini
        catch (const std::exception&) { hud.debug_message("cutscene: load failed"); return; }
        start_cutscene_xso(std::move(s), p.stem().string(), debug235);
    };

    // One fixed tick of the room's event scripts. Returns whether the world is FROZEN (a Fix
    // is held) — the caller skips the player/enemy sim for that tick but the camera still runs.
    auto tick_events = [&] {
        // A scene change ends the old room's scripts (they die with the scene, FUN_00431840); the new
        // room's start next frame (start_room_events). Until then nothing runs: before this guard the
        // old scripts got one more tick in the new room, where a KekkaiZako watcher saw no seeds alive
        // and cleared its ward (leaving S_2008 set 336 without a fight).
        if (room_events_dirty) return csMgr.world_frozen();
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
                // Wait_ChrMoving (0x100) waits on the walk itself, which no predicate can wave through
                // without leaving the walker short of its mark: run the walk-to / fly-to steps (and the
                // turn) once per drained step instead, so a scripted walk finishes in skip time and
                // every actor ends where the scene put it (ROUTE_F13.md D3: SwordTutorial1 pc 617).
                csHost.advance_player_move();
                world.advance_cutscene_moves();
                world.tick_facing([](const Actor&) { return false; });
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
            hud.debug_message("(skipped)");
        }
        csMgr.tick_all();
        // The windows (FUN_0057a0e0 / FUN_005789a0) read this tick's input record (g_in, the mapper
        // FUN_005d2ba0): Confirm held = +0x20 & 0x12000, Cancel held / auto-repeat = +0x20 / +0x28 &
        // 0x4000, a menu's cursor = the repeat step +0x30 (WNDDIALOG_DRAW.md §3-§4). The Confirm /
        // Cancel edges arrive through csHost.advance / wnd_cancel (the tick's input step, a drive).
        if (csHost.wtext.alive() || csHost.wdlg.alive()) {
            WndInput win;
            win.confirm_held = (g_in.held & (input::kOk | input::kClick)) != 0;
            win.confirm_pressed = csHost.advance;
            csHost.advance = false;
            win.cancel_held = (g_in.held & input::kCancel) != 0;
            win.cancel_repeat = (g_in.repeat & input::kCancel) != 0;
            win.vstep = csHost.wnd_vstep != 0 ? csHost.wnd_vstep : g_in.dy;
            win.cancel_pressed = csHost.wnd_cancel;
            csHost.wnd_vstep = 0;
            csHost.wnd_cancel = false;
            wnd_mouse(win);
            std::vector<int> wsnd;
            csHost.wtext.text_speed = csHost.wdlg.text_speed = settings().s.access.text_speed;   // the shell's
            csHost.wtext.update(win, wsnd);
            csHost.wdlg.update(win, wsnd);
            csHost.dispatch_menu();
            for (int id : wsnd) audio.play(id, kSfxMenuVol);
        }
        csHost.box_active = csHost.wtext.alive() || csHost.wdlg.alive();
        csHost.caption_card.step();
        csHost.bust_set.step();
        csHost.fade_advance();
        // Advance cutscene walk-to moves (op 0x143): the player (feet) and NPCs (world).
        // Re-snap the player to the floor after the XZ step so a walk-to follows terrain.
        csHost.advance_player_move();
        world.advance_cutscene_moves();
        // The facing turn (CHR_OPS.md §6): NPCs turn toward their look goal in the world; the AI
        // loop owns a live actor's facing outside a Fix. The player turns here while a Fix holds
        // or a scripted walk runs (FUN_00465170 0x46a080..0x46a16f: no +0x36c factor); in free
        // play the stick owns her facing and +0x308 follows it [H].
        {
            const bool fz = csMgr.world_frozen();
            world.tick_facing([&](const Actor& a) { return !fz && a.alive() && !a.destroy_requested(); });
            if (Actor* wp = world.player()) {
                const float cur = std::atan2(face_dir.z, face_dir.x);
                if (fz || csHost.p_moving) {
                    float txz[2];
                    const float* tp = nullptr;
                    if (wp->look.mode == 1 && wp->look.target != 0)
                        if (const Actor* t = world.find(wp->look.target)) { txz[0] = t->pos.x; txz[1] = t->pos.z; tp = txz; }
                    const float goal = look_goal(wp->look, feet.x, feet.z, tp);
                    const float h = look_turn(cur, goal, player_turn_rate(csHost.p_running()));
                    if (h != cur) face_dir = {std::cos(h), 0.0f, std::sin(h)};
                    facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                } else {
                    wp->look.cmd = cur;
                    wp->anim_motion = -1;   // [H] in free play her own tick owns the motion (+0x994)
                }
            }
        }
        // Keep the player on the floor for the WHOLE cutscene, not just mid-step: Chr_Pos
        // teleports her to authored Y (sometimes below the floor) and she never falls while
        // the sim is frozen, so snap every frozen frame.
        // The floor below her origin (the integrator's own floor ray, FUN_0045a510 / the step at
        // `oy` below), not the room's top surface at that x/z: in a stacked room that is a roof
        // or a closed door's top (S_2012's door_01 lifted a frozen arrival from y 2 to 10.5).
        // Only a point with no floor under it at all falls back to the highest surface. [H]
        // Not an AIRBORNE player caught by a Fix (FIX_SLOTS.md S3): the original has no snap, its integrator
        // FUN_0045a510 runs on under either slot [EXPORT 0046fba0.c:50-116], so she goes on through her arc and
        // lands (fix_fall, stepped in the frozen branch). [approx] a grounded player under a Fix is still snapped:
        // the port's script placements (Chr_Pos to authored heights) rely on it; neither is a swimmer stepped.
        fix_fall = csMgr.world_frozen() && !csHost.p_moving && !pv.grounded && !submerged && !fly && player.hp >= 1 &&
                   !(world.player() && world.player()->pattern.pinned());
        if ((csMgr.world_frozen() || csHost.p_moving) && !fix_fall) {
            float fl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + kOriginH);
            if (fl <= kNoFloor) fl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + room_diag(room));
            if (fl > kNoFloor) feet.y = fl;
        }
        // Cutscene NPCs spawn at authored heights (Shion at y~2) and never fall — keep
        // non-player, non-enemy actors on the floor at ALL times (they persist after the
        // cutscene too, so gating on 'frozen' left them hovering during normal play). If no
        // collision floor is under them, fall back to the player's floor level (same room).
        for (Actor& a : world.actors()) {
            if (a.is_player() || a.is_enemy()) continue;
            if (a.flyer) continue;   // +0x398 (Chr_FlyOnOff / the factory): no floor clamp (FUN_0045a510)
            // The floor below the origin first, as for the player above: the room's top surface put
            // the medallion doors' Chr_LocateDummy talk targets on the closed doors' tops (S_3014's at
            // y 15.6 for a script y of 8), out of reach of the action probe from the floor.
            float fl = floor_at(phys_mesh(room), a.pos.x, a.pos.z, a.pos.y + kOriginH);
            if (fl <= kNoFloor) fl = floor_at(phys_mesh(room), a.pos.x, a.pos.z, a.pos.y + room_diag(room));
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
    // The attack button's press (+0x50c & 1): latched here, resolved by PlayerActions in the
    // next logic tick (the request / buffer / combo rules of FUN_00465170 + FUN_0046ea00).
    auto try_attack = [&] { atk_press_pending = true; };

    // Spawn every bullet an ASO section fired this tick. Shared by the player and by
    // enemies: previously only the player drained bullets(), so no enemy could ever
    // shoot even when its script fired one. `origin` is the shooter's feet, `fdir` its
    // facing; the heading offset arg is applied on top of that.
    // The player's anime_FireBullet (the VM case at 0x447b91, BULLETS.md §1.2-1.3, the
    // ABILITIES_VERIFIED.md FireBullet addendum): as the monsters', at the origin + (cos, sin)
    // (arg4 deg + facing +0x314) x arg2, y + arg3; the player's side with the section's class (the
    // 0x594174 table, FUN_0046d5e0's). Every bullet spawns and lives as long as its effect; only
    // emitters with a hit arm deal damage (Toal's smoke puffs have none).
    auto spawn_player_bullets = [&] {
        const float pf = std::atan2(face_dir.z, face_dir.x);
        for (const AsoVm::BulletEvent& fb : avm.bullets()) {
            const float ang = fb.f[2] * 0.0174533f + pf;
            const float c = float(std::cos(double(ang))), sn = float(std::sin(double(ang)));
            bullet::Spawn s;
            s.kind = fb.kind; s.effect = fb.id;
            s.owner = 13; s.owner_monster = false;
            s.owner_atk = float(player_stats().atk);
            s.cls = player_damage_class(avm.section());
            s.pos = {feet.x + c * fb.f[0], feet.y + kOriginH + fb.f[1], feet.z + sn * fb.f[0]};
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
    };

    // FUN_0046cc10(amount): the EX/Boost gauge add. Refused while boosting or full, and
    // ONLY counts while g_flags[187] is set (0x76bc08 — set by the floor-1 story scripts,
    // e.g. MAP/S_01/S_0100/S1004.XSO), so a fresh game cannot charge Boost before that beat.
    // Call-site amounts (audit combat.md #22): +300 per hit dealt (FUN_004fbb40 0x4fd0bf),
    // +dmg*100 per hit taken (0x4fded0), and the passive per-tick fill in the mover
    // (0x467d8c: ((1-hp/max)*word[0x68b070]+1)*M, skillmp::boost_passive, in the fixed step).
    // The engine's fix (0x75ad90 + 0x75ad94) is taken as the script world freeze [approx].
    auto add_boost = [&](float amount) {
        if (!skillmp::boost_can_add(game.g_flags[187] != 0, game.g_flags[161] != 0, csMgr.world_frozen(),
                                    player.boosting, player.boost)) return;
        player.boost += amount;
        if (player.boost >= skillmp::kBoostFull) {    // READY (0x46cd4c..): EF2 42 on the player, SE 0x13
            player.boost = skillmp::kBoostFull;
            // FUN_00451c20(player+0x20, &(0, 0, -0.5) [0x7d9e94], rot 0, scale 1, 2): half a unit
            // along world -z of the origin, following it (EXPORT 0046cc10.c).
            if (ef2sim::Effect* r = spawn_sim_fx(42, {}, {}, 1.0f, {}, 0, {})) {
                r->local = ef2sim::translation(0.0f, 0.0f, -0.5f);
                r->attach_parent = &actor_mats[13];
                r->attach_mode = 2;
            }
            player_white_flash(hitfx::kBoostReadyFlashStep);   // +0x264..+0x274
            sfx_at(0x13, feet, kSfxAnimVol);
        }
    };
    // The yaw of the camera record (+0x48c: 0x7b343c + slot * 0x4a0), as FUN_005eca30 derives it
    // [BYTES 0x5eca70..0x5ecaad]: atan2(-fwd.z, fwd.x) + pi/2 over the view's forward row, wrapped
    // into (-pi, pi]. RotY of it turns local -z toward the eye (0 = the camera on -z).
    auto camera_yaw = [&] {
        const Vec3 f = cam.forward();
        float y = std::atan2(-f.z, f.x) + 1.5707964f;
        if (y > 3.1415927f) y -= 6.2831855f;
        return y;
    };
    // An effect spawned at 0 then FUN_00451c20(player+0x20, &off, &(0, yaw, 0), &(1,1,1), 2):
    // local = T(off) * RotYPR(yaw, 0, 0), following the player's origin.
    auto attach_to_player = [&](ef2sim::Effect* f, const Vec3& off, float yaw) {
        if (!f) return f;
        f->local = ef2sim::mul(ef2sim::translation(off.x, off.y, off.z), ef2sim::rot_ypr(yaw, 0.0f, 0.0f));
        f->attach_parent = &actor_mats[13];
        f->attach_mode = 2;
        return f;
    };
    // A level gained (issue #76), once per level in FUN_00420c40's loop [EXPORT 00420c40.c:144-282]:
    // - the popup "Level Up" (s_Level_Up_006981a0, the English string) at the player's origin + 0.5
    //   (DAT_006bd304), FUN_00564da0(popup, text, 0xff00, 0.65) [BYTES 0x42127e..0x4212bb]: green, scale 0.65;
    // - FUN_0043a040(5, 0x400) [BYTES 0x4212c0]: SE 5 = sys/s_levelup, 2-D, played whether or not a popup
    //   slot was free;
    // - EF2 0x3e (62, common/Lvup.eft) placed at 0 and attached to the player, yaw = the camera's, mode 2
    //   (FUN_00451c20) [BYTES 0x421152]. [approx] its +0x1da / +0x1dc |= 0x100 fields are not modelled.
    level_up_fx = [&] {
        if (std::getenv("CLERIA_DRIVE")) std::printf("  [levelup] lv %d: popup 'Level Up' 00ff00 x0.65, SE 5, EF2 62\n", player.level);
        if (popups.size() < 32)
            popups.push_back(make_popup("Level Up", 0x00ff00, 0.65f, {feet.x, feet.y + kOriginH + 0.5f, feet.z}));
        audio.play(5, kSfxMenuVol);
        attach_to_player(spawn_sim_fx(62, {}, {}, 1.0f, {}, 0, {}), {}, camera_yaw());
    };


    // ---- Boost (build/ability_re/ABILITIES_VERIFIED.md §1, audit combat.md #19-#22) ---------
    // Entering needs a FULL gauge — the engine compares == 30000.0 exactly — and grants 60
    // i-frames (FUN_00463f40(0x20): max(+0x734, 0x3c)). A SECOND press (only with g_flags[188],
    // set late in the game) spends the whole gauge on the character's EXTRA/super section, 16
    // i-frames. Boost carries no damage or speed multiplier; it halves incoming damage
    // (hit.hpp player_damage_after_boost). The drain (~30/tick) lives in the fixed-step block.
    // The end (FUN_00462c40 0x462c9f..): the mode bit off, gauge 0, the aura killed, bb50 = 0,
    // SetSection(2, 0) (priority-gated), and Toal's PC3_mazin_hatudou (EF2 172).
    auto end_boost = [&] {
        player.boost = 0.0f;
        player.boosting = false;
        game.g_flags[141] = 0;                            // DAT_0076bb50
        kill_named_fx("#boost_aura");                     // the aura's +0x1d4 = 1
        if (character == 3) {
            // PC3_mazin_hatudou, inline in FUN_00462c40 [EXPORT 00462c40.c:122-190]: power 0, knock 1, rtype 0,
            // class 0xff, SIDE 0 (the player's: its three hit emitters strike the monsters), then
            // FUN_00451c20(player+0x20, 0, (0, camera yaw, 0), 1, mode 2). It used to spawn with the default
            // side 1 and hurt Toal himself (BOOST_SKILLS.md F10).
            ef2sim::SpawnParams sp;
            sp.side = 0; sp.power = 0.0f; sp.knock = 1.0f; sp.cls = -1; sp.owner = 13;
            sp.atk = player_stats().atk;
            attach_to_player(spawn_sim_fx(172, {}, {}, 1.0f, sp, 0, {}), {}, camera_yaw());
        }
        avm.set_section(2);                               // FUN_00446550(2, 0): refused under a higher-priority move
    };
    auto try_boost = [&] {
        if (!player.boosting) {
            // mover 0x465b1d: g_flags[187], the gauge exactly full, not busy, no fix.
            if (player.boost < skillmp::kBoostFull || game.g_flags[187] == 0) return;
            if (game.g_flags[161] != 0 || game.g_flags[151] == -1 || csMgr.world_frozen()) return;
            // 0x465bea..0x465ca6: adol_S_mode_f at (0, 0, -0.5) rotated by the camera yaw, i.e. half
            // a unit toward the eye, attached mode 2 (it used to sit unattached at the origin).
            const float cyaw = camera_yaw();
            if (ef2sim::Effect* f = attach_to_player(spawn_sim_fx(50, {}, {}, 1.0f, {}, 0, {}), {0.0f, 0.0f, -0.5f}, cyaw))
                f->name = "#boost_f";                     // [CleriaCore] a name for the drive's `fx` listing
            player.boosting = true;
            game.g_flags[141] = 1;                        // FUN_00463f40(0x20): bb50 = 1
            // FUN_00463f40 0x46490d..: the aura (39 / Toal 171) at 0, then FUN_00451c20(player+0x20,
            // &0, &(0, camera yaw, 0), &1, 2); kept in +0x758.
            if (ef2sim::Effect* a = attach_to_player(spawn_sim_fx(character == 3 ? 171 : 39, {}, {}, 1.0f, {}, 0, {}),
                                                     {}, cyaw))
                a->name = "#boost_aura";
            sfx_at(0xb4, feet, kSfxAnimVol);
            player_iframes = std::max(player_iframes, 60);
            // FUN_00446550(2, 0) (0x465cc9): drops a locomotion section to idle; the priority gate
            // refuses it while a swing / skill / hurt section of a higher type plays.
            avm.set_section(2);
            return;
        }
        // The super needs g_flags[188] (first set late, S_6009) and a section outside {9, 5}.
        if (game.g_flags[188] == 0 || avm.section() == 9 || avm.section() == 5) return;
        // ... and not busy, no fix: the block's own tests [BYTES 0x465b1d..0x465cdd] (BOOST_SKILLS.md §2.1).
        if (game.g_flags[161] != 0 || game.g_flags[151] == -1 || csMgr.world_frozen()) return;
        // Second press: the EXTRA section. These live outside the 0x100..0x10f combo range,
        // so they are requested directly; they carry their own hitboxes, which the existing
        // armed_hitbox() path already resolves.
        const int extra = character == 1 ? 313 : character == 2 ? 300 : 317;
        if (!avm.set_section(extra, /*force*/true)) return;
        if (character == 2) barrier_t = 420;              // +0x7dc = 0x1a4 (0x465d39)
        avm.set_local(8, 0);                              // +0x9fc = 0, the re-cast window (0x465d9b)
        pad_rumble(5);                                    // FUN_005d3500(5) at 0x465dc0 (view_pad.inl)
        atk_dir = face_dir;
        player_hit_serial = next_hit_serial(world.hit_serial_counter_);
        player.boost = 0.0f;                              // spends the gauge
        player_iframes = std::max(player_iframes, 16);
    };

    // ---- Skills / magic (ABILITY.md §2a, BOOST.md §4, GAUGES_UI.md) ----------------------
    // A skill is NOT a special animation: it is a different ASO section, gated by SP. The
    // per-character SP costs are fully dumped; the section table is only partly dumped (one
    // row of PTR_DAT_0068afd0), so the per-character rows are [PARTIAL] — see the note below.
    // The skill button (ABILITIES_VERIFIED.md §2.2, src/cleria/skillmp): charge while held
    // (+25/tick while affordable), cast on release — the normal {0x10d,0x10c,0x10e}, or after
    // 121 held ticks with a Lv2+ bracelet the charged {0x110,0x10f,0x111}. Run once per tick.
    // Hugo's Force Shield (ABILITIES_VERIFIED.md §3.2): +0x7c4 the shield effect (PC2_S_B, its
    // handle stored by the player's GenerateEffect 0x86 / 0x87, FUN_004496f0), +0x7c0 the hits it
    // still blocks, +0x7bc its life in ticks.
    ef2sim::Effect* shield_fx = nullptr;
    bool shield_on = false;
    bool player_hold = false;    // P+0x398 set by M470's grab: no vertical integration (view_game.inl, the brain loop)
    LedgeState ledge;            // P+0x4f1 / +0x4f4 / +0x51c / +0x3c8: the integrator's ledge recovery (ledge.hpp)
    int ledge_serial = -1;       // the scene load `ledge` was seeded for
    int shield_hits = 0, shield_life = 0;
    auto fx_live = [&](const ef2sim::Effect* p) {
        if (!p) return false;
        for (const auto& f : fxsim.effects()) if (f.get() == p) return !f->dead;
        return false;
    };
    // The removal (FUN_0046a3e0 0x46a6e2..0x46a6fd): fx+0x1d4 = 1, the three fields zeroed. The
    // break (hits spent, or a Fix starting, FUN_0046af60 L411-536) first attaches EF2 140
    // PC2_S_B_koware and plays SE 153 (0x99) at the player. [not modelled] the fade-out (10) of
    // SE 157 / 152 (FUN_005cdc40): the audio layer has no stop-by-id.
    auto remove_shield = [&](bool broken) {
        if (broken) {
            const Vec3 at{feet.x, feet.y + kOriginH, feet.z};
            spawn_sim_fx(0x8c, at, {}, 1.0f, {}, 13, at);
            sfx_at(0x99, feet, kSfxAnimVol);
        }
        if (fx_live(shield_fx)) shield_fx->kill = true;
        shield_fx = nullptr; shield_on = false;
        shield_hits = 0; shield_life = 0;
    };
    skillmp::Charge skill_charge;
    // The Fix edge, FUN_0056e8d0 (FIX_SLOTS.md §1.2, S1): when a slot's count goes 0 -> 1 the player's velocity
    // +0xa3c..+0xa44 and push +0x338..+0x340 are zeroed [EXPORT 0056e8d0.c:15-20]; every Fix zeroes the charge
    // state 0x76c134 / 0x76c138 [:57-58]. (Its kill of the ailment / buff effects is ail_tick's, in the frozen
    // branch.) The vertical timer +0x330 is not touched: a jump caught by a Fix goes on.
    csMgr.on_fix_taken = [&](int, bool first) {
        if (first) { pvel = {}; player_kb = {}; }
        skill_charge = {};
    };
    bool drive_skill = false;                             // CLERIA_DRIVE skill+ / skill-
    bool drive_jump = false;                              // CLERIA_DRIVE jump: one press
    Vec3 drive_wish{};                                    // CLERIA_DRIVE move=dx,dz,n: a held stick
    int drive_wish_n = 0;
    Vec3 drive_goto{};                                    // CLERIA_DRIVE goto=x,z,n: the stick held toward a point
    int drive_goto_n = 0;
    // anime_GenerateDebris (funcid 0x26; build/mech_re/ASO_NOOPS.md §3): the handler's colour and reviver gate
    // [BYTES 0x447980..0x447a39], then FUN_00587440 -> FUN_00587010 with texture base 0x42 at the actor's
    // origin +0x50: n + 5 * count draws of the shared xorshift, on the op's tick. An ordinary monster's corpse
    // does not run its VM here (the kill removes it), so its death section's op is read from the script once
    // (enemy_death_debris) and replayed `tick` ticks after the kill, at the place it died.
    // [H] its place among the first dead tick's other draws: after FUN_00478510's (the puff, the drops).
    static const bool debris_log = [] { const char* v = std::getenv("CLERIA_DEBRIS"); return v && *v && *v != '0'; }();
    auto spawn_debris = [&](int cast_id, const Vec3& origin, const AsoVm::DebrisEvent& ev) {
        if (debris_skipped(cast_id, (inv.equip_flags & 0x8000u) != 0)) return;
        const Enemy* er = etab.get(cast_id);
        const std::uint64_t c0 = pick_rng.calls;
        const std::size_t n0 = pickups.size();
        spawn_pickups(pick_rng, ev.bag.data(), int(ev.bag.size()), origin,
                      debris_args(ev.count, ev.spin, ev.keep, ev.fixed, debris_colour(cast_id, er ? er->blood : 0u),
                                  settings().s.game.bloody_effect_level),
                      water_mode == 2, pickups);
        if (debris_log)
            std::printf("  [debris] t%llu cast 0x%x: %zu pieces (ids %d..), %llu draws\n", (unsigned long long)tick_count,
                        cast_id, pickups.size() - n0, ev.bag.empty() ? -1 : ev.bag[0],
                        (unsigned long long)(pick_rng.calls - c0));
    };
    #include "view_hugo_eyes.inl"                         // Hugo's Eyes of Fact (the player's familiars)
    auto skill_tick = [&](bool skill_held, bool attack_held) {
        const int slot = skillmp::bracelet_slot(inv.magic);
        skillmp::SkillIn si;
        si.skill_held = skill_held; si.attack_held = attack_held;
        si.magic = inv.magic;
        si.bracelet_level = slot < 3 ? game.g_flags[182 + slot] : 0;
        si.section = avm.section();
        si.kind = skillmp::attack_kind(si.section);
        si.busy = game.g_flags[161] != 0 || game.g_flags[151] == -1;
        si.fix = csMgr.world_frozen();
        si.bc80 = std::uint32_t(game.g_flags[217]);
        si.cost.chr = character;
        si.cost.difficulty = difficulty >= 2 ? difficulty - 2 : 0;   // DAT_0076bb5c 0 / 1 / 2
        si.cost.bc80 = si.bc80;
        si.cost.boosting = player.boosting;
        si.cost.acc_mp = (inv.equip_flags & 0x200000u) != 0;   // the Ring of Ease (x0.80)
        // [not modelled] Adol's DAT_0074c0cc table.
        skillmp::Mp mp{player.sp_cur, player.sp_max, player.sp_lag};
        const int charge_lv0 = skill_charge.level;
        const skillmp::SkillOut so = skillmp::skill_tick(si, mp, skill_charge);
        // FUN_0046cf20's tail, on the tick the charge level rises [BYTES 0x46d1f4..0x46d308, table 0x46d548]:
        // level 1 plays SE 11 s_chargemax (not in sections 9 / 5); level 2 plays 791 / 792 / 793
        // (s_yunica_max / s_hugo_max / s_thor_max) by character, 11 for character 0. All 2-D at 0x400.
        // Nothing in the supers: Yunica's 0x139, Hugo's 300, 0x13d for Toal and character 0; nor for Hugo's
        // second bracelet with no live eye of his own. [not modelled] the white flash +0x268 that follows.
        if (skill_charge.level > charge_lv0) {
            bool ok = true;
            if (character == 1) ok = si.section != 0x139;
            else if (character == 2) {
                if (slot == 1) {
                    ok = false;
                    const Actor* pa = world.player();
                    for (const Actor& a : world.actors())
                        if (pa && hugo::owned_eye(a, pa->instance_id) && !hugo::b8a4(a.ai) && eye_section(a) != 5) ok = true;
                }
                if (si.section == 300) ok = false;
            } else ok = si.section != 0x13d;
            if (ok && skill_charge.level == 2) audio.play(character == 0 ? 11 : 790 + character, kSfxAnimVol);
            else if (ok && si.section != 9 && si.section != 5) audio.play(11, kSfxAnimVol);
        }
        if (so.charged_ready) {                           // the charge-ready effect, attached
            static const int kReadyFx[3] = {7, 6, 5};     // 0x69bcfc[slot] (Adol)
            const int id = character == 1 ? 0x80 : character == 2 ? 0x8d : character == 3 ? 0xb9 : kReadyFx[slot];
            const Vec3 at{feet.x, feet.y + kOriginH, feet.z};
            spawn_sim_fx(id, at, {}, 1.0f, {}, 13, at);
        }
        if (!so.cast) return;
        // FUN_0046ea00(1): refused in the super sections; [not modelled] the ASO re-cast lock.
        const int sec = avm.section();
        if (sec == 0x139 || sec == 300 || sec == 0x13d) return;
        if (character == 2 && !hugo_eyes_cast(inv.magic, so.level == 2)) return;   // Trap Mine: nothing to lay
        player_iframes = std::max(player_iframes, 3);
        // FUN_0046ea00 L77-112: Hugo's Ventus (0x74) frees the old shield and arms the new one,
        // 1 hit / 360 ticks (charged, c138 == 2: 2 / 380); in Boost (0x76c198 & 0x20) 480 ticks
        // and one hit more. [not modelled] the Extra-mode lives (DAT_0074c0d8: 420 / 440) and
        // DAT_0076bc64 (10 hits / 1000 ticks).
        if (character == 2 && inv.magic == 0x74) {
            if (fx_live(shield_fx)) shield_fx->kill = true;
            shield_fx = nullptr; shield_on = false;
            const bool charged = so.level == 2;
            shield_hits = charged ? 2 : 1;
            shield_life = charged ? 0x17c : 0x168;
            if (player.boosting) { shield_life = 0x1e0; shield_hits += 1; }
        }
        // Toal's 0x76 cast: +0x7b0 = (+0x388 == 0), "cast in the air" [EXPORT 0046ea00.c:171-173]; only the
        // next 0x76 cast rewrites it. The slow fall reads it (BOOST_SKILLS.md §3.6).
        if (character == 3 && inv.magic == 0x76) toal_air_cast = !pv.grounded;
        pad_rumble(so.level == 2 ? 2 : 0);               // FUN_005d3500: 0x46f19b charged (2) / 0x46f27a (0)
        avm.set_section(so.section, /*force*/true);
        avm.set_local(12, so.level);                      // LocalInt 12 = c138
        player.sp_cur -= float(so.cost);                  // unconditionally, after the SetSection
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
    // FUN_00573e60 step 3: the speaker's entity class (+0x124); the player is class 0xd (its
    // face remaps by character); an id with no entity is its own class only if 1..0xa5.
    csHost.class_fn = [&](int sp) -> int {
        csHost.character = character;
        if (sp == 13 || sp == 999 || sp == 0x3e6) return 0xd;
        if (auto it = csHost.spawn_cast.find(sp); it != csHost.spawn_cast.end()) return it->second;
        return (sp >= 1 && sp <= 0xa5) ? sp : -1;
    };
    csHost.equipped_fn = [&](int id) { return inv_is_equipped(game.g_flags, inv, id); };
    csHost.event_xx_fn = [&](int n) {                     // FUN_0058f210 [EXPORT], VELAGUNDER.md §1.9
        if (n == 10) inv.accessory = 0xff;
        else if (n == 7) { inv.accessory = -1; inv.held = -1; inv.magic = -1; }
        else if (n == 2) player.sp_cur = player.sp_max;  // DAT_0076a738 = DAT_0076a734: the second gauge refilled
        else if (n == 8) {                                // HealHp's body if alive; [not modelled] DAT_0076b914 |= 0x75
            if (player.hp >= 1) { player.hp = player.max_hp; player.hp_lag = float(player.hp); }
        }
        else if (n == 0 || n == 12) {                     // DAT_0076c1b4 = 0, then FUN_00462c40 ends a Boost
            if (fx_live(barrier_fx)) barrier_fx->kill = true;   // kill P+0x7e0, P+0x7dc = 0 (0x58f298 / 0x58f74b)
            barrier_fx = nullptr; barrier_t = 0;
            player.boost = 0.0f;
            if (player.boosting) end_boost();
        }
        else if (n == 1) player.boost = skillmp::kBoostFull;   // DAT_0076c1b4 = 30000 (a direct write: no READY)
        else if (n == 4) {
            // The clear (case 4 [BYTES 0x58f350..0x58f5e9; EXPORT 0058f210.c:92-229], CLEAR_DATA.md §3.2), in order:
            // byte 0x74ab9b = 1 ("GameFirstPlay" of the system ini); the character's clear byte 0x74ab7b / 7a / 79 =
            // max(old, 1), or 0xd5 / 0x8e / 0x89 when FUN_00583960 answers 1 (the Extra version), and its clear flag
            // 297 / 298 / 299 = 1; the cleared-tier byte of the run's difficulty — the rows are 0x74ab88 (Very Easy),
            // 8c (Easy), 94 (Normal), 98 (Hard), 9c (Nightmare), the column the character - 1; record.ta2
            // (FUN_00436fa0); the system ini. [H] FUN_00583960 = the Type Select answer of this session (`skin`).
            // [not modelled] the achievements (FUN_00403e80), and the clear-time test (flag 199 against 75 / 80 /
            // 90 / 120 minutes -> 0x74e258) with its leaderboard upload FUN_005a22e0: no platform here.
            settings().s.game.game_first_play = 1;
            movie_seen.first_play = true;
            const int ch = game.g_flags[150];
            records::apply_clear(g_clear, ch, skin == 1, game.g_flags[143] != 0, game.g_flags[144], game.g_flags[203] != 0);
            if (ch >= 1 && ch <= 3) game.g_flags[296 + ch] = 1;
            g_clear_dirty = true;
            const bool wrote = clear_data_save();
            settings().touch(); settings().save();
            std::printf("  [clear] EventXX(4): GameFirstPlay = 1, clear flag %d = 1\n", ch >= 1 && ch <= 3 ? 296 + ch : -1);
            std::printf("  [clear] clear bytes Yunica %02x Hugo %02x Toal %02x, %d cleared tier(s); record.ta2 %s\n", g_clear.clear[0],
                        g_clear.clear[1], g_clear.clear[2], g_clear.tier_count(), wrote ? "saved" : "NOT saved");
        }
        else if (n == 13) {
            // The SP bonus (case 13 [BYTES 0x58f8a5..0x58f94a], BONUS_MODES.md §1): 10,000 SP per cleared
            // (difficulty, character) cell, at most 150,000, a hundredth on Nightmare, through FUN_004fa290.
            const int add = records::sp_bonus(g_clear, game.g_flags[144]);
            player.sp = std::clamp(player.sp + add, 0, 999999);
            std::printf("  [clear] EventXX(13): SP bonus %d (%d cleared tier(s)) -> SP %d\n", add, g_clear.tier_count(), player.sp);
        }
        // 11 and 12 zero the four status timers 0x76c160..0x76c16c [EXPORT 0058f210.c:288-311]; the bits and
        // the effects go on the next FUN_00462550 (ail_tick: the engine's own call here returns under a Fix).
        if (n == 11 || n == 12) ailments::clear_timers(ail);
        // [not modelled] the three buff timers of 12.
    };
    csHost.start_label = [&](const std::string& l) {     // [not modelled] the owner actor
        if (!csMgr.start(l)) std::printf("  [menu] %s did not start\n", l.c_str());
    };
    // 0xBC / 0xBB / 0x8F. [H] Se_Play's 2nd operand (1024 everywhere) is not read by
    // FUN_004396f0. Bgm_Volume's (volume /1024, time) read as a linear fade over `time` ms [H].
    csHost.se_fn = [&](int se) { audio.play(se, kSfxAnimVol); };
    csHost.se_stop_fn = [&](int se, int ms) { audio.stop_se(se, ms); };
    // Se_Play's second operand is the volume of a record that is not 3-D (FUN_0043a040(se, vol), 0x400 = unity;
    // 35 of the 1663 calls differ: 900, 614, 512, 500, 400). A 3-D record takes the 3-D path, which has none.
    csHost.se_vol_fn = [&](int se, int vol) {
        const EffectDef* d = audio.defs().find(se);
        audio.play(se, kSfxAnimVol, d && d->is3d ? 1.0f : float(vol) / 1024.0f);
    };
    csHost.se_volume_fn = [&](int se, int vol, int ms) { audio.set_se_volume(se, float(vol) / 1024.0f, ms); };
    // 0x11c Eft_SetAlphaTarget (FUN_0056fba0): every effect not freed whose name matches (djb2-xor
    // hash + _stricmp) gets +0x1e4 = alpha, +0x1e8 = 1/frames (1.0 for 0); FUN_00451e60 steps it.
    // 0x12a Eft_Die (FUN_00451730): the kill flag on every live effect of that id. [not modelled] its
    // particles' +0x1b4 byte (meaning untraced).
    csHost.eft_die_fn = [&](int id) {
        for (auto& f : fxsim.effects()) if (!f->dead && f->id == id) f->kill = true;
    };
    csHost.fx_camoff_fn = [&](int id, float off) {         // FUN_0056fa00: fx+0x204 on every effect of the id
        int n = 0;
        for (auto& f : fxsim.effects()) if (!f->dead && f->id == id) { f->bias_extra = off; ++n; }
        if (std::getenv("CLERIA_XSO_LOG")) std::printf("  [xso] 0x11b effect %d camera offset %.2f -> %d live\n", id, off, n);
    };
    csHost.fx_alpha_fn = [&](const std::string& n, float a, int frames) {
        for (auto& f : fxsim.effects()) {
            if (f->dead || f->name.size() != n.size()) continue;
            bool same = true;
            for (std::size_t k = 0; k < n.size() && same; ++k)
                same = std::tolower((unsigned char)f->name[k]) == std::tolower((unsigned char)n[k]);
            if (!same) continue;
            f->alpha_target = a;
            f->alpha_step = frames != 0 ? 1.0f / float(frames) : 1.0f;
        }
    };
    #include "view_xso13.inl"                  // the floor 1..3 op pass (FLOOR13_OPS.md)
    // Kind 0x24's carrier for bullet.cpp (bullet::Env::carrier): set by the boss02 host below.
    std::function<bool(int, ef2sim::M4*, float*, float*)> boss2_carrier;
    #include "view_duel.inl"                   // the story duels' script side (DUELS.md)
    #include "view_weather.inl"                // the floor-3 weather ops (WEATHER.md)
    csHost.water_mode_p = &water_mode;
    csHost.water_wall_fn = [&](float x, float y, float z, float r, float st) {
        bullets.spawn_water_probe({x, y, z}, r, st);    // the falling splash probe (GoXx type 0x16)
    };
    // Facing in Falcom degrees maps to the view's yaw directly (memory: no 90-degree flip).
    csHost.player_face_fn = [&](float deg) {
        const float yaw = deg * 3.14159265358979323846f / 180.0f;
        face_dir = {std::sin(yaw), 0.0f, std::cos(yaw)};
        facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
    };
    // The player's Chr block lives on the world player actor (CHR_OPS.md §11); her facing is
    // face_dir, whose engine heading is atan2(z, x) (+0x314).
    csHost.wplayer_fn = [&]() -> Actor* { return world.player(); };
    csHost.player_heading_fn = [&](float h) {
        face_dir = {std::cos(h), 0.0f, std::sin(h)};
        facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
    };
    csHost.player_heading_now_fn = [&] { return std::atan2(face_dir.z, face_dir.x); };
    csHost.player_run_factor_fn = [&] {      // +0x410 x the grounded per-character constant
        const bool blessing_run = (game.g_flags[(0x76bc80 - 0x76b91c) / 4] & 0x2000) != 0;
        return pm.run_mult * run_char_const(character, blessing_run, shield_life != 0);
    };
    csHost.actor_pos_fn = [&](int id, Vec3* out) {
        if (id == 13 || id == 999) { *out = feet; return true; }
        if (const Actor* a = world.find(id)) { *out = a->pos; return true; }
        return false;
    };
    // 0x118 Eft_LocateChr (FUN_0056f600) [BYTES 0x56f600..0x56f873]: the actor's origin (+0x50..) and
    // heading (+0x314); side (fx+0x1d7) 0 for class 0xd / 0x54..0xa5, else 1.
    //   mode 0: FUN_00450410 at origin + (cos(h + deg) * dist, dy, sin(h + deg) * dist), rot
    //           (0, deg - (h + pi/2), 0), scale (s, s, s): placed once, not attached.
    //   mode m: FUN_00450410 at 0 with scale s, then FUN_00451c20: local = T(0, dy, -dist) *
    //           RotYPR(deg, 0, 0) * S(s), parent = the actor's +0x20, attach mode m.
    // [H] the actor's +0x20 is taken as its origin translation (actor_mats), as the other attaches.
    csHost.locate_chr_fx = [&](int eff, int id, float dist, float deg, float dy, float s, int mode,
                               const std::string& name) {
        const bool pl = id == 13 || id == 999;
        const Actor* a = pl ? world.player() : world.find(id);
        if (!pl && !a) return;
        const Vec3 org = pl ? Vec3{feet.x, feet.y + kOriginH, feet.z} : Vec3{a->pos.x, a->pos.y + a->height, a->pos.z};
        const float h = pl ? std::atan2(face_dir.z, face_dir.x) : heading_of_yaw(a->yaw);
        const int cls = pl ? 13 : a->cast_id;
        ef2sim::SpawnParams sp;
        sp.side = (cls == 0xd || (cls >= 0x54 && cls <= 0xa5)) ? 0 : 1;
        const float d = deg * 0.017453292f;                     // 0x6bd0dc
        ef2sim::Effect* fx = nullptr;
        if (mode == 0) {
            const float ang = h + d;
            fx = spawn_sim_fx(eff, {org.x + std::cos(ang) * dist, org.y + dy, org.z + std::sin(ang) * dist},
                              {0.0f, d - (h + 1.5707964f), 0.0f}, s, sp, 0, {});
        } else if (a) {
            fx = spawn_sim_fx(eff, {}, {}, s, sp, 0, {});
            if (fx) {
                fx->local = ef2sim::mul(ef2sim::mul(ef2sim::translation(0.0f, dy, -dist), ef2sim::rot_ypr(d, 0.0f, 0.0f)),
                                        ef2sim::scaling(s, s, s));
                fx->attach_parent = &actor_mats[a->instance_id];
                fx->attach_mode = mode;
            }
        }
        if (fx) fx->name = name;
    };
    csHost.emotion_fn = [&](int owner, int kind, float yoff) { spawn_emotion(emotions, owner, kind, yoff); };
    // 0x187 live: a mesh the room build does not have yet is added and the room rebuilt.
    csHost.mesh_sphere_fn = [&](const std::string& n, float r, float ox, float oy, float oz) {
        std::string ln = n;
        for (char& ch : ln) ch = char(std::tolower((unsigned char)ch));
        HitMesh& h = hit_meshes[ln];
        h.r = r; h.off = {ox, oy, oz};
    };
    csHost.mesh_locate_fn = [&](const ObjLocateArgs& a) {
        std::string ln = a.name;
        for (char& ch : ln) ch = char(std::tolower((unsigned char)ch));
        if (room.mesh.object_range(ln)) return;
        live_obj.located.push_back(a);
        live_obj_dirty = true;
    };
    // 0xA4 / 0xB9 / 0xBA: the default (scene) BGM, play (-1 = the default), stop.
    // Bgm_Stop(ms) = FUN_00432fc0: curBgm 0, then FUN_005cde10(ms), a linear volume ramp to 0
    // over ms and the stop ms later [BYTES 0x432fc0, 0x5cde10, 0x5cd455] (Audio::fade_out_bgm).
    csHost.bgm_default_fn = [&](int id) { default_bgm = id; };
    csHost.scene_clip_fn = [&](float n, float f) {                       // op 0xa2
        clip_near = n; clip_far = f;
        std::printf("  [scene] clip planes %.1f / %.1f (op 0xa2)\n", n, f);
    };
    csHost.bgm_play_fn = [&](int id) {
        if (id == -1) id = default_bgm;
        if (id <= 0) { audio.stop_bgm(); cur_bgm = 0; return; }
        if (id == cur_bgm) return;
        cur_bgm = id;
        audio.play_bgm(bgmdefs.track(id));
    };
    csHost.bgm_stop_fn = [&](int ms) { audio.fade_out_bgm(ms); cur_bgm = 0; };
    // 0x85 HealHp (FUN_004fa0e0): HP to max; SE 0x75 (FUN_00430790). [not modelled] FUN_00420c40.
    csHost.heal_fn = [&] {
        if (player.hp >= 1) { player.hp = player.max_hp; player.hp_lag = float(player.hp); }
        audio.play(0x75, kSfxMenuVol);
    };
    // 0x14E Chr_Muteking: the player's i-frames +0x734 = max(+0x734, n), and n == 0 clears them
    // (FUN_00571390: `uVar2 = param_3; if (param_3 != 0 && +0x734 < param_3) ...; +0x734 = uVar2`).
    // @UseWing's Muteking(998, 6000) ... Muteking(998, 0) left 100 s of invulnerability after a warp.
    csHost.muteking_fn = [&](int n) { player_iframes = n == 0 ? 0 : std::max(player_iframes, n); };
    // Chr_Jump on the player: the vertical timer +0x330 = v, the origin rises v, airborne.
    // [not modelled] +0x9d6 = 1.
    csHost.player_jump_fn = [&](float v) {
        pv.t = v; pv.grounded = false; pv.jumping = false;
        feet.y += v;
        grounded = false;
    };
    // A live 0x16B / 0x16C: record it, and rebuild only when it changes what the room shows.
    csHost.mesh_state_fn = [&](const std::string& n, bool coll, bool on) {
        std::string ln = n;
        for (char& ch : ln) ch = char(std::tolower((unsigned char)ch));
        (coll ? live_obj.collide : live_obj.visible)[ln] = on;
        const bool now = coll ? phys_mesh(room).object_range(ln) != nullptr
                              : room.mesh.object_range(ln) != nullptr;
        if (now != on) live_obj_dirty = true;
    };
    // A scene object's frame (its render node of mesh 0, posed, placed): scene.hpp object_frame.
    // False when it is hidden (no render range) or its model cannot be read.
    auto mesh_frame_mat = [&](const MeshObj& m, Mat4* f, bool* sole = nullptr) {
        const RoomMesh::ObjRange* r = room.mesh.object_range(m.name);
        if (!r) return false;
        SobObject o = r->obj;
        o.position = m.pos;
        if (m.has_world) { o.has_world = true; o.world_mat = m.world; }
        const char* clip = (m.sect >= 0 && std::size_t(m.sect) < m.clips.size())
                               ? m.clips[std::size_t(m.sect)].name.c_str() : nullptr;
        return object_frame_mat(asset_root, o, clip, m.time, f, sole);
    };
    auto mesh_frame = [&](const MeshObj& m, Vec3* t) {
        Mat4 f;
        if (!mesh_frame_mat(m, &f)) return false;
        *t = {f.m[12], f.m[13], f.m[14]};
        return true;
    };
    // Mesh_DistanceToAlpha's frame height, cached until the object moves or re-poses
    // (step_meshes clears frame_y_ok). A hidden object keeps its last height.
    csHost.mesh_frame_y_fn = [&](const std::string& n) {
        MeshObj* m = meshes.find(n);
        if (!m || (m->frame_y_ok && !m->dirty_pos && !m->dirty_pose)) return;
        Vec3 t{};
        if (mesh_frame(*m, &t)) { m->frame_y = t.y; m->frame_y_ok = true; }
    };
    // Mesh_SyncronizePos's +0x298 & 0x60 copy, on the live intent (a 0x16B / 0x16C of b).
    csHost.mesh_sync_bits_fn = [&](const std::string& a, const std::string& b) {
        std::string la = a, lb = b;
        for (char& ch : la) ch = char(std::tolower((unsigned char)ch));
        for (char& ch : lb) ch = char(std::tolower((unsigned char)ch));
        for (const bool coll : {false, true}) {
            const auto& mp = coll ? live_obj.collide : live_obj.visible;
            const auto ib = mp.find(lb), ia = mp.find(la);
            const bool vb = ib == mp.end() || ib->second, va = ia == mp.end() || ia->second;
            if (va != vb) csHost.mesh_state_fn(la, coll, vb);
        }
    };
    csHost.set_skill_fn = [&](int i) { inv.magic = i; };
    csHost.bgm_volume_fn = [&](int v, int t) {
        bgm_gain_to = std::clamp(float(v) / 1024.0f, 0.0f, 1.0f);
        const float ticks = std::max(1.0f, float(t) * 60.0f / 1000.0f);
        bgm_gain_step = (bgm_gain_to - bgm_gain) / ticks;
        if (bgm_gain_step == 0.0f) bgm_gain = bgm_gain_to;
    };
    // The action probe FUN_00460470 (build/event_re/BARRIER_OBJECTS.md §1b), on the action key
    // while no Fix is held. Its candidates:
    //  - actors with a talk script (+0x630, op 0x130) within their radius + 0.95 of the point
    //    1.05 ahead of the player's origin and within +-70 degrees of the facing, scored by that
    //    squared distance;
    //  - the first action-button object (SOB trigger 8) with its event on (op 0x176) that one of
    //    four forward rays of 1.25 hits, scored 0.7. The rays start at the origin +0.05 and
    //    -0.45, and 0.3 to either side at -0.3 [BYTES 0x460672..0x46080f].
    // Sorted by score; the first whose script starts wins. A mesh script gets its name as $S0.
    // [approx] the probe's HP >= 1 gate is applied to enemies only: the factory gives every
    // cast HP from CASTINFO+0x74 (FACTORY.md 0x4595aa), ours gives it to enemies only. The
    // +0x60 & 4 kind gate is not modelled (only 0x130 gives an actor a script).
    auto try_interact = [&]() -> bool {   // true = a script started (the press is used up)
        if (fly || csMgr.world_frozen()) return false;
        const Vec3 o{feet.x, feet.y + kOriginH, feet.z};
        const float fl = std::sqrt(face_dir.x * face_dir.x + face_dir.z * face_dir.z);
        if (fl < 1e-6f) return false;
        const float c = face_dir.x / fl, sn = face_dir.z / fl;    // the facing, (cos, sin)
        struct Cand { float score; const Actor* actor; const Interactable* obj; };
        std::vector<Cand> cands;
        const Vec3 ahead{o.x + c * 1.05f, o.y, o.z + sn * 1.05f};
        for (const Actor& a : world.actors()) {
            if (a.is_player() || a.talk_script.empty() || a.destroy_requested()) continue;
            if (a.is_enemy() && !a.alive()) continue;
            // The target's origin +0x54 (FUN_00460470 compares origins): floor + its +0x324. An actor
            // with no sprite binding (a Chr_LocateDummy talk target, CAST_C000) never had its height
            // set, so its "origin" sat on the floor, 1.075 below the player's: the medallion doors and
            // the Blue Moon altar were out of reach from the floor in front of them.
            const float ah = a.height > 0.0f ? a.height : cast_motion(casts, a.cast_id).height();
            const Vec3 ao{a.pos.x, a.pos.y + ah, a.pos.z};
            const float dx = ao.x - ahead.x, dy = ao.y - ahead.y, dz = ao.z - ahead.z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            const float r = a.hurt_radius + 0.95f;                // +0x68 + 0.95
            if (d2 > r * r) continue;
            float da = std::atan2(ao.z - o.z, ao.x - o.x) - std::atan2(sn, c);
            while (da > 3.14159265f) da -= 6.2831853f;
            while (da < -3.14159265f) da += 6.2831853f;
            if (std::fabs(da) < 1.2217305f) cands.push_back({d2, &a, nullptr});
        }
        {
            const float px = -sn * 0.3f, pz = c * 0.3f;          // 0.3 to the side
            const Vec3 from[4] = {{o.x, o.y + 0.05f, o.z}, {o.x, o.y - 0.45f, o.z},
                                  {o.x + px, o.y - 0.3f, o.z + pz}, {o.x - px, o.y - 0.3f, o.z - pz}};
            const Vec3 d{c * 1.25f, 0.0f, sn * 1.25f};
            bool hit = false;
            for (int r = 0; r < 4 && !hit; ++r)
                for (const Interactable& it : room.mesh.interactables) {
                    std::string ln = it.name;
                    for (char& ch : ln) ch = char(std::tolower((unsigned char)ch));
                    const MeshObj* mo = meshes.find(ln);
                    if (mo && !mo->event_on) continue;            // +0x298 & 0x80 (op 0x176)
                    // The collision hull, else the render geometry [STAND-IN].
                    const RoomMesh* rm = &phys_mesh(room);
                    const RoomMesh::ObjRange* rr = rm->object_range(ln);
                    if (!rr) { rm = &room.mesh; rr = rm->object_range(ln); }
                    if (!rr) continue;
                    const Vec3 b{from[r].x + d.x, from[r].y, from[r].z + d.z};
                    if (segment_hits_tris(*rm, from[r], b, rr->t0, rr->t1)) {
                        cands.push_back({0.7f, nullptr, &it});
                        hit = true;
                        break;
                    }
                }
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
        for (const Cand& cd : cands) {
            std::copy(game.g_flags, game.g_flags + 0x200, csMgr.vm.g_flags);
            csMgr.vm.g_flags[150] = character;
            if (cd.actor) {
                std::string sc = cd.actor->talk_script;
                for (char& ch : sc) ch = char(std::tolower((unsigned char)ch));
                if (csMgr.start(sc, {}, cd.actor->instance_id)) {        // bound to the actor [H]
                    std::printf("  [probe] actor %d -> %s\n", cd.actor->instance_id, sc.c_str());
                    return true;
                }
                continue;
            }
            const Interactable* target = cd.obj;
            // The object's own SOB script (+0x188) as authored; the loader resolves '@' to
            // S_COMMON. A goddess statue is the same: "Save" on the sealed model runs the room's
            // SAVE.XSO (the cleanse; its own gates decide, nothing shows when they fail), "@Save"
            // on the cleansed one the common menu (STATUE_CLEANSE.md §1.1).
            std::string name = target->script_name;
            if (name.empty() && !target->script.empty()) name = fs::path(target->script).stem().string();
            for (char& ch : name) ch = char(std::tolower((unsigned char)ch));
            if (name.empty() || target->script.empty()) continue;
            XsoVm::Vars ov;
            ov.s[0] = target->name;                                // the object's own name ($S0)
            ov.i[0] = target->i0; ov.i[1] = target->i1; ov.f[0] = target->f0;
            if (csMgr.start(name, ov, /*owner: the player*/ 13)) {
                std::printf("  [probe] %s -> %s\n", target->name.c_str(), name.c_str());
                return true;
            }
        }
        return false;
    };

    std::printf("\nPLAY: the retail bindings (yso_win.ini [input] Assign{}, the pad too); Attack also talks / opens | "
                "F3 overlay | debug keys %s: F free-fly, R respawn, T test cutscene, K/L/B, Ctrl+Tab character, "
                "Ctrl+1-9 exit (Debug tab / cleria.ini [cleria.dev] DebugKeys)\n",
                settings().s.cleria.dev_keys ? "ON" : "off");
    bool running = true, looking = false;
    // CLERIA_DRIVE (debug): timed commands for unattended verification runs, keyed by the fixed
    // tick count, e.g. "30:at=-27,24,180;60:kill1;400:shot;700:act;800:ok;900:quit".
    //   at=x,z,deg  place the player (floor y) facing deg      kill1  remove the group-1 actors
    //   act         the action key (the probe)                 ok     a confirm press
    //   shot        write the next frame to CLERIA_SHOT_DIR/t<tick>.ppm          quit
    //   flag=i,v    set a game flag      atk   the attack button      boss  face Velagunder
    //   bossaim     turn to Velagunder's part 1 (the core while stunned) from where the player stands
    //   boss2[=o]   stand by a hittable Nygtilger piece (o = its object index)
    //   boss3[=i]   stand by a hittable Gelaldy part (i = 0 core, 1 right arm, 3 left arm)
    //   boss6[=opts] fight Zava as a player does, every tick from then on: floor placement + real inputs only
    //               (opts lightning, thunder, cast; boss6=off ends it; view_boss06.inl)
    //   boss7[=opts] fight Dalles as a player does, every tick from then on: floor placement + real inputs only, the
    //               Skill 1-3 key of the barrier that is up (opts evade, skill; boss7=off ends it; view_boss07.inl)
    //   boss8[=opts] fight Darm as a player does, every tick from then on: floor placement + real inputs only
    //               (boss8=off ends it; view_boss08.inl)
    //   diff=n      the difficulty 0 Very Easy .. 4 Nightmare (flags 143 / 144 / 203; Zava's heads are built again)
    //   cam=x,y,z,yaw,pitch   draw from that fixed pose (degrees) from now on, for shots (the game runs on)
    //   ok / esc / up / down  confirm / cancel / a cursor step: the pause book while it is up (its
    //                         frames count as ticks), else the windows and the game-over menu
    //   hp=n / sp=n           the player's HP (0 = die) / the SP wallet
    //   fx                    the named (script-placed) effects and the glare byte
    //   mouse=X,Y             the virtual pointer so the record cursor reads (X, Y) (1024x768 units)
    //   click / rclick        an L / R tap (one tick); mhold / mrelease  hold / release L (MOUSE_DIR)
    //   wheel=n               one tick of raw wheel delta (+-120 a notch)
    //   cursor                print the cursor manager's state
    //   exit=i                take SEN exit i (the door's own arrival + facing, as walking through it)
    //   held=id               the held tool 0x76a62c (97 = the Water Dragon's Scales: O2 drains 1, not 10)
    //   saveload              save here, round-trip the record, load it (apply_loaded) and print the feet
    //   T:every=N,END,cmd     (parse time) cmd at T, T+N, ... below END
    //   duel=cast             near=cast + atk, only while a duel is live (156 = 1, world not frozen)
    //   dflags                print the duel flags (130 131 133 156 174 230 232 311)
    //   fader                 print the fade machine (current -> target at rate) and a staged / latched door
    //   loops                 print the ids of the looping SE voices (Audio::looping)
    //   blackout[=rrggbb]     hold the fader at full (as a script's Fader_Target(1) left behind), colour rgb
    //   oktap                 a real one-tick press of the Attack/OK key (view_access.inl)
    // A drive run is lockstep (one tick a frame); CLERIA_DRIVE_REALTIME=1 paces it by the wall clock.
    // A scene load builds every object anew (FUN_00431260 / FUN_00434020, SCENEFLOW.md �6.2): a Retry or a load
    // into a boss room constructs the boss again (full HP, its first state, FUN_0041fd10's retry count). Bosses
    // 1-5 kept their brain across the reload: the fight went on from the HP it had, and Khonsclard's six pods
    // (located by the brain's ctor, dropped by the new visit) never rose again � the fight could not be won.
    // boss_load[k]: the load serial boss k+1's brain was built under.
    int boss_load[5] = {-1, -1, -1, -1, -1};
    std::unique_ptr<Boss01> boss;            // Velagunder's brain (the boss host below)
    int drive_boss_entry = -1;               // drive bossentry=k: stand on body entry k (view_boss01_touch.inl)
    std::unique_ptr<Boss02> boss2;           // Nygtilger's brain (the boss02 host below)
    std::unique_ptr<Boss03> boss3;           // Gelaldy's brain (the boss03 host below)
    std::unique_ptr<Boss04> boss4;           // Khonsclard's brain (view_boss04.inl)
    std::function<bool(const std::string&)> boss4_drive;   // drive boss4[=i] (view_boss04.inl)
    std::unique_ptr<Boss05> boss5;           // Pictimos's brain (view_boss05.inl)
    std::function<bool(const std::string&)> boss5_drive;   // drive boss5[=i] (view_boss05.inl)
    std::unique_ptr<Boss06> boss6;           // Zava's two heads (view_boss06.inl)
    std::function<bool(const std::string&)> boss6_drive;   // drive boss6 (view_boss06.inl)
    std::unique_ptr<Boss07> boss7;           // Dalles (view_boss07.inl)
    std::function<bool(const std::string&)> boss7_drive;   // drive boss7 (view_boss07.inl)
    std::unique_ptr<Boss08> boss8;           // Darm (view_boss08.inl)
    std::function<bool(const std::string&)> boss8_drive;   // drive boss8 (view_boss08.inl)
    struct DriveCmd { std::uint64_t tick; std::string cmd; };
    std::vector<DriveCmd> drive;
    std::vector<DriveCmd> drive_more;       // `rep=` expansions, merged after the tick's commands (audit aid)
    if (const char* dv = std::getenv("CLERIA_DRIVE")) {
        std::string all = dv;
        std::size_t p0 = 0;
        while (p0 < all.size()) {
            std::size_t p1 = all.find(';', p0);
            if (p1 == std::string::npos) p1 = all.size();
            const std::string item = all.substr(p0, p1 - p0);
            const std::size_t c = item.find(':');
            // `T:every=N,END,cmd` repeats cmd at T, T+N, ... below END (long fights / ok streams).
            if (c != std::string::npos && item.compare(c + 1, 6, "every=") == 0) {
                const std::string rest = item.substr(c + 7);
                const std::size_t k1 = rest.find(','), k2 = k1 == std::string::npos ? k1 : rest.find(',', k1 + 1);
                const std::uint64_t n = k1 == std::string::npos ? 0 : std::stoull(rest.substr(0, k1));
                if (n > 0 && k2 != std::string::npos) {
                    const std::uint64_t end = std::stoull(rest.substr(k1 + 1, k2 - k1 - 1));
                    for (std::uint64_t t = std::stoull(item.substr(0, c)); t < end; t += n)
                        drive.push_back({t, rest.substr(k2 + 1)});
                }
            } else
            if (c != std::string::npos) drive.push_back({std::stoull(item.substr(0, c)), item.substr(c + 1)});
            p0 = p1 + 1;
        }
    }
    std::string drive_shot;                 // a pending `shot` file
    // ok / esc / up / down go to the pause book while it is up (set where the book lives); the
    // drive's tick count runs on through the book's frames, since the game ticks are held.
    std::function<bool(const std::string&)> drive_book;
    std::optional<Camera> drive_cam;        // `cam=`: the view drawn from instead of the rig's
    // The CrossFade capture (0xcd): the frozen frame and its bind group for the HUD overlay.
    WGPUTexture cf_tex = nullptr; WGPUTextureView cf_view = nullptr; WGPUBindGroup cf_bind = nullptr;
    std::uint32_t cf_w = 0, cf_h = 0;
    #include "view_abilities.inl"                       // abil, boost=, boostkey, hurt= (abil_drive)
    #include "view_hudstate.inl"                        // hudstate, drop=, mp= (hud_drive); PLAYTEST_1_HUD.md
    #include "view_ailments.inl"                        // ail=, ailhit=, ailstate (ail_drive); STATUS_AILMENTS.md
    auto run_drive = [&](const std::string& cmd) {
        std::printf("  [drive] t%llu %s\n", (unsigned long long)tick_count, cmd.c_str());
        if (speedrun_drive(cmd, shell)) return;          // speedrun=... (view_speedrun.inl)
        if (lua_drive(cmd)) return;                      // lua=<command> <args> (view_lua.inl)
        if (abil_drive(cmd)) return;                     // view_abilities.inl
        if (boss4_drive && boss4_drive(cmd)) return;     // view_boss04.inl
        if (boss5_drive && boss5_drive(cmd)) return;     // view_boss05.inl
        if (boss6_drive && boss6_drive(cmd)) return;     // view_boss06.inl
        if (boss7_drive && boss7_drive(cmd)) return;     // view_boss07.inl
        if (boss8_drive && boss8_drive(cmd)) return;     // view_boss08.inl
        if (hud_drive(cmd)) return;                      // view_hudstate.inl
        if (ail_drive(cmd)) return;                      // view_ailments.inl
        if (cmd.rfind("at=", 0) == 0) {
            float x = 0, z = 0, deg = 0;
            if (std::sscanf(cmd.c_str() + 3, "%f,%f,%f", &x, &z, &deg) >= 2) {
                const float fl = floor_at(phys_mesh(room), x, z, feet.y + room_diag(room));
                feet = {x, fl > kNoFloor ? fl : feet.y, z};
                if (fl > kNoFloor) ledge_serial = -1;   // a placement onto a floor re-seeds the ledge guard: its last good point is the old spot (REACQUIRE.md §1.3)
                const float yaw = deg * 3.14159265358979323846f / 180.0f;
                face_dir = {std::sin(yaw), 0.0f, std::cos(yaw)};
                facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
            }
        } else if (cmd.rfind("rep=", 0) == 0) {        // rep=every,count,cmd: cmd again every `every` ticks (audit aid)
            int ev = 0, n = 0, off = 0;
            if (std::sscanf(cmd.c_str() + 4, "%d,%d,%n", &ev, &n, &off) >= 2 && ev > 0 && off > 0)
                for (int k = 1; k <= n; ++k)
                    drive_more.push_back({tick_count + std::uint64_t(ev) * std::uint64_t(k), cmd.substr(4 + std::size_t(off))});
        } else if (cmd.rfind("at3=", 0) == 0) {        // at3=x,y,z,deg: the floor below (x, y + 1, z) (audit aid)
            float x = 0, y = 0, z = 0, deg = 0;
            if (std::sscanf(cmd.c_str() + 4, "%f,%f,%f,%f", &x, &y, &z, &deg) >= 3) {
                const float fl = floor_at(phys_mesh(room), x, z, y + 1.0f);
                feet = {x, fl > kNoFloor ? fl : y, z};
                if (fl > kNoFloor) ledge_serial = -1;
                const float yaw = deg * 3.14159265358979323846f / 180.0f;
                face_dir = {std::sin(yaw), 0.0f, std::cos(yaw)};
                facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
            }
        } else if (cmd == "kill1") {
            std::vector<int> ids;
            for (const Actor& a : world.actors()) if (a.tag.in_group(1)) ids.push_back(a.instance_id);
            for (int id : ids) world.despawn(id);
            csHost.world_changed = true;
        } else if (cmd.rfind("flag=", 0) == 0) {          // flag=index,value (game flags)
            int fi = 0, fv = 0;
            if (std::sscanf(cmd.c_str() + 5, "%d,%d", &fi, &fv) == 2 && fi >= 0 && fi < 0x200)
                game.g_flags[fi] = fv;
        } else if (cmd == "boss") {                    // stand 2.5 in front of Velagunder's part 1, facing it
            if (boss) {
                const int pi = boss->stunned() ? 0 : 1;          // the core while stunned
                const Vec3 c = boss->part(pi).pos;
                float dx = feet.x - c.x, dz = feet.z - c.z;
                const float l = std::sqrt(dx * dx + dz * dz);
                if (l > 1e-3f) { dx /= l; dz /= l; } else { dx = 0; dz = -1; }
                const float reach = pi == 0 ? 1.4f : 2.5f;
                const float x = c.x + dx * reach, z = c.z + dz * reach;
                const float fl = floor_at(phys_mesh(room), x, z, feet.y + room_diag(room));
                feet = {x, fl > kNoFloor ? fl : feet.y, z};
                face_dir = {-dx, 0.0f, -dz};
                facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
            }
        } else if (cmd == "bossaim") {                 // turn to Velagunder's part 1 (the core while stunned); no move
            if (boss) {
                const Vec3 c = boss->part(boss->stunned() ? 0 : 1).pos;
                const float dx = c.x - feet.x, dz = c.z - feet.z;
                const float l = std::sqrt(dx * dx + dz * dz);
                if (l > 1e-3f) {
                    face_dir = {dx / l, 0.0f, dz / l};
                    facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                }
            }
        } else if (cmd.rfind("bossentry=", 0) == 0) {  // bossentry=k: the player onto Velagunder's body entry k
            drive_boss_entry = std::atoi(cmd.c_str() + 10);
        } else if (cmd.rfind("boss2", 0) == 0) {       // boss2[=o]: stand by a hittable Nygtilger piece
            // The target: `o` if given, else the head once exposed (DEF < 999), else the first
            // live plate d01..d17, else the tail. The player is put 1.1 off it horizontally at
            // the piece's height (the plates ride the segments' backs, so this is ON the worm:
            // the segment is solid from above) and faces it.
            if (boss2) {
                int o = -1;
                if (cmd.size() > 6 && cmd[5] == '=') o = std::atoi(cmd.c_str() + 6);
                if (o < 0 && boss2->object(0).def < 999.0f && boss2->object(0).hp >= 1.0f) o = 0;
                for (int j = boss02::kPieces; o < 0 && j < boss02::kObjects; ++j)
                    if (boss2->mesh_hittable(j)) o = j;
                if (o < 0 && boss2->part_hittable(boss02::kPieces - 1)) o = boss02::kPieces - 1;
                if (o >= 0 && o < boss02::kObjects) {
                    const Boss02Piece& bp = boss2->object(o);
                    const Vec3 c = bp.has_part ? bp.part.pos : bp.pos();
                    float dx = -c.x, dz = -c.z;                  // from the arena centre side
                    const float l = std::sqrt(dx * dx + dz * dz);
                    if (l > 1e-3f) { dx /= l; dz /= l; } else { dx = 0; dz = -1; }
                    feet = {c.x + dx * 1.1f, c.y - kOriginH + 0.2f, c.z + dz * 1.1f};
                    if (o == 0 || o == boss02::kPieces - 1) {    // the head / tail parts: from the floor
                        const float fl = floor_at(phys_mesh(room), feet.x, feet.z, 1.0f);
                        feet.y = fl > kNoFloor ? fl : 0.0f;
                    }
                    pv = VerticalState{}; pv.grounded = false;
                    face_dir = {-dx, 0.0f, -dz};
                    facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                    std::printf("  [drive] boss2 -> %s (%.2f,%.2f,%.2f) hp %.0f pool %.0f/%.0f phase %d mode %d\n",
                                boss02::object_name(o), c.x, c.y, c.z, bp.hp, boss2->pool(), boss2->pool_max(),
                                boss2->phase(), boss2->mode());
                }
            }
        } else if (cmd.rfind("boss3", 0) == 0) {       // boss3[=i]: stand by a hittable Gelaldy part
            // The target: `i` if given, else the core once it is open, else an unguarded arm
            // (the right one first). The player is put on the arena side of the part, just
            // inside its hurt sphere horizontally, on the floor below (or at the part's height
            // when there is none), facing it.
            if (boss3) {
                int i = -1;
                if (cmd.size() > 6 && cmd[5] == '=') i = std::atoi(cmd.c_str() + 6);
                auto open = [&](int k) { return boss3->part_hittable(k) && boss3->part(k).hurt_centre.y > -500.0f; };
                for (int k : {0, 1, 3}) if (i < 0 && open(k)) i = k;
                if (i >= 0 && i < boss03::kParts) {
                    const Boss03Part& bp = boss3->part(i);
                    const Vec3 c = bp.hurt_centre;
                    const Vec3 bc = boss3->pos();
                    float dx = c.x - bc.x, dz = c.z - bc.z;
                    const float l = std::sqrt(dx * dx + dz * dz);
                    if (l > 1e-3f) { dx /= l; dz /= l; } else { dx = 0; dz = -1; }
                    const float reach = std::max(0.5f, bp.hurt_radius * 0.5f);
                    const float x = c.x + dx * reach, z = c.z + dz * reach;
                    const float fl = floor_at(phys_mesh(room), x, z, c.y + 2.0f);
                    feet = {x, fl > kNoFloor ? fl : c.y - kOriginH, z};
                    pv = VerticalState{}; pv.grounded = false;
                    face_dir = {-dx, 0.0f, -dz};
                    facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                    std::printf("  [drive] boss3 -> part %d (%.2f,%.2f,%.2f) r %.2f hp %.0f body %.0f/%.0f feet (%.2f,%.2f,%.2f)\n",
                                i, c.x, c.y, c.z, bp.hurt_radius, bp.hp, boss3->hp(), boss3->max_hp(), feet.x, feet.y, feet.z);
                } else {
                    std::printf("  [drive] boss3: no open part (core guard %d, arms %d/%d)\n", int(boss3->core_guard()),
                                int(boss3->arm_guard(0)), int(boss3->arm_guard(1)));
                }
            }
        } else if (cmd.rfind("cam=", 0) == 0) {        // cam=x,y,z,yaw,pitch (deg): a fixed view for shots
            float x = 0, y = 0, z = 0, yw = 0, pt = 0;
            if (std::sscanf(cmd.c_str() + 4, "%f,%f,%f,%f,%f", &x, &y, &z, &yw, &pt) == 5) {
                Camera c;
                c.pos = {x, y, z};
                c.yaw = yw * 3.14159265f / 180.0f;
                c.pitch = pt * 3.14159265f / 180.0f;
                drive_cam = c;
            }
        } else if (cmd.rfind("exit=", 0) == 0) {       // exit=i: take the room's SEN exit i (arrival + facing)
            const std::size_t i = std::size_t(std::atoi(cmd.c_str() + 5));
            if (i < room.sen.triggers.size()) {
                const SenTrigger t = room.sen.triggers[i];   // a committed door: no fade-out, the entry fade-in
                transition(t.dest_scene, &t);
            }
        } else if (cmd == "atk") {                     // the attack button
            try_attack();
        } else if (cmd.rfind("near=", 0) == 0) {       // near=cast: stand 2.0 from that actor, facing it
            const bool by_id = cmd.size() > 5 && cmd[5] == '#';   // near=#id: by instance id (audit aid)
            const int cast = std::atoi(cmd.c_str() + 5 + (by_id ? 1 : 0));
            float nd = 2.0f;                                      // near=cast,dist: another distance (audit aid)
            bool live = false;                                    // near=cast,live: prefer a living one (a wave after a wave)
            if (const std::size_t cm = cmd.find(','); cm != std::string::npos) {
                if (cmd.compare(cm + 1, std::string::npos, "live") == 0) {
                    for (const Actor& a : world.actors())
                        live |= (by_id ? a.instance_id : a.cast_id) == cast && !a.is_player() && a.hp > 0;
                } else {
                    nd = float(std::atof(cmd.c_str() + cm + 1));
                }
            }
            for (const Actor& a : world.actors()) {
                if ((by_id ? a.instance_id : a.cast_id) != cast || a.is_player() || (live && a.hp <= 0)) continue;
                float dx = feet.x - a.pos.x, dz = feet.z - a.pos.z;
                const float l = std::sqrt(dx * dx + dz * dz);
                if (l > 1e-3f) { dx /= l; dz /= l; } else { dx = 0; dz = -1; }
                const float x = a.pos.x + dx * nd, z = a.pos.z + dz * nd;
                const float fl = floor_at(phys_mesh(room), x, z, feet.y + room_diag(room));
                feet = {x, fl > kNoFloor ? fl : feet.y, z};
                if (fl > kNoFloor) ledge_serial = -1;
                face_dir = {-dx, 0.0f, -dz};
                facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                std::printf("  [drive] near cast %d id %d hp %.0f flags 156=%d 130=%d phase %d sub %d ai %d stem %s loaded %d pos (%.1f,%.1f,%.1f)\n", cast, a.instance_id, a.hp,
                            game.g_flags[156], game.g_flags[130], a.ai.phase(), a.ai.i(0x72c), a.ai_index, casts.sprite_stem(a.cast_id).c_str(), int(efx.kinds.count(casts.sprite_stem(a.cast_id))), a.pos.x, a.pos.y, a.pos.z);
                break;
            }
        } else if (cmd.rfind("seen=", 0) == 0) {       // seen=#id[,dist]: as near=, but only to a spot the actor
            // can see (its origin -> the player's origin clear, the floor within 2.5 of its own): near= puts the
            // player on the TOP floor at (x, z), which can be past a wall or up a cliff (issue #59's drops). Audit aid.
            const int id = std::atoi(cmd.c_str() + 5 + (cmd.size() > 5 && cmd[5] == '#'));
            float nd = 3.0f;
            if (const std::size_t cm = cmd.find(','); cm != std::string::npos) nd = float(std::atof(cmd.c_str() + cm + 1));
            for (const Actor& a : world.actors()) {
                if (a.instance_id != id || a.is_player()) continue;
                const Vec3 ao{a.pos.x, a.pos.y + a.height, a.pos.z};
                bool placed = false;
                for (int k = 0; k < 16 && !placed; ++k) {
                    const float an = float(k) * 0.3926991f + std::atan2(feet.z - a.pos.z, feet.x - a.pos.x);
                    const float x = a.pos.x + std::cos(an) * nd, z = a.pos.z + std::sin(an) * nd;
                    const float fl = floor_at(phys_mesh(room), x, z, a.pos.y + 2.5f);
                    if (fl <= kNoFloor || std::fabs(fl - a.pos.y) > 2.5f) continue;
                    if (segment_blocked(phys_mesh(room), ao, {x, fl + kOriginH, z})) continue;
                    feet = {x, fl, z};
                    if (fl > kNoFloor) ledge_serial = -1;
                    face_dir = {-std::cos(an), 0.0f, -std::sin(an)};
                    facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                    placed = true;
                }
                std::printf("  [drive] seen #%d %s feet (%.2f, %.2f, %.2f)\n", id, placed ? "placed" : "no clear spot",
                            feet.x, feet.y, feet.z);
                break;
            }
        } else if (cmd.rfind("epos=", 0) == 0) {       // epos=#id,x,z: that actor onto the floor at (x, z) (audit aid:
            int id = 0; float x = 0, z = 0;            // KEEPOUT_BOXES.md — a monster next to an exit volume)
            if (std::sscanf(cmd.c_str() + 5, "#%d,%f,%f", &id, &x, &z) == 3)
                if (Actor* a = world.find(id)) {
                    const float fl = floor_at(phys_mesh(room), x, z, a->pos.y + 2.0f);
                    a->pos = {x, fl > kNoFloor ? fl : a->pos.y, z};
                    std::printf("  [drive] epos #%d (%.2f, %.2f, %.2f)\n", id, a->pos.x, a->pos.y, a->pos.z);
                }
        } else if (cmd.rfind("duel=", 0) == 0) {       // duel=cast: as near= + atk, only while a duel is live
            // (156 = 1 and the world not frozen), so a drive can fight through a whole duel script
            // without moving the player during its cutscenes (build/event_re/DUELS.md §5).
            const int cast = std::atoi(cmd.c_str() + 5);
            if (game.g_flags[156] == 1 && !csMgr.world_frozen() && player.hp >= 1)
                for (const Actor& a : world.actors()) {
                    if (a.cast_id != cast || a.is_player() || !a.alive()) continue;
                    float dx = feet.x - a.pos.x, dz = feet.z - a.pos.z;
                    const float l = std::sqrt(dx * dx + dz * dz);
                    if (l > 1e-3f) { dx /= l; dz /= l; } else { dx = 0; dz = -1; }
                    const float x = a.pos.x + dx * 2.0f, z = a.pos.z + dz * 2.0f;
                    const float fl = floor_at(phys_mesh(room), x, z, feet.y + room_diag(room));
                    feet = {x, fl > kNoFloor ? fl : feet.y, z};
                    if (fl > kNoFloor) ledge_serial = -1;
                    face_dir = {-dx, 0.0f, -dz};
                    facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                    try_attack();
                    std::printf("  [duel] t%llu cast %d hp %.0f/%.0f player hp %d | lv %d max %g atk %g def %g exp %g\n",
                                (unsigned long long)tick_count, cast, a.hp, a.max_hp, player.hp, a.level, a.max_hp, a.atk,
                                a.def, a.exp_reward);
                    break;
                }
        } else if (cmd == "dflags") {                  // the duel flags
            std::printf("  [duel] flags 130=%d 131=%d 133=%d 156=%d 174=%d 230=%d 232=%d 311=%d player hp %d\n",
                        game.g_flags[130], game.g_flags[131], game.g_flags[133], game.g_flags[156], game.g_flags[174],
                        game.g_flags[230], game.g_flags[232], game.g_flags[311], player.hp);
        } else if (cmd == "bullets") {                 // the live GoXx bullets
            for (const auto& bp : bullets.list())
                std::printf("  [bullet] kind %d effect %d age %u pos (%.2f, %.2f, %.2f) speed %.2f fx %d%s\n", bp->s.kind,
                            bp->s.effect, bp->age, bp->s.pos.x, bp->s.pos.y, bp->s.pos.z, bp->s.speed,
                            bp->fx ? bp->fx->live : -1, bp->gone ? " gone" : "");
            for (const auto& bb : bullets.bombs())                // the kind-0xf bombs (BOMB.md)
                std::printf("  [bomb] age %.0f pos (%.2f, %.2f, %.2f) vel (%.3f, %.3f, %.3f)%s fx %d\n", bb->age,
                            bb->pos.x, bb->pos.y, bb->pos.z, bb->vel.x, bb->vel.y, bb->vel.z, bb->rest ? " rest" : "",
                            bb->fx ? bb->fx->live : -1);
        } else if (cmd == "pickups") {                // the world pickups and where they draw (audit aid)
            const Vec3 cf = cam.forward();
            const Vec3 cr = normalize(cross(Vec3{0, 1, 0}, cf)), cu = cross(cf, cr);
            for (const Pickup& pk : pickups) {
                const Vec3 sc = pk.sprite_centre(cu, cf);
                std::printf("  [pickup] %sid %d pos (%.2f, %.2f, %.2f) ground %.3f rest %d age %.0f sprite (%.2f, %.2f, %.2f) "
                            "lowest %.3f shadow y %.3f\n", pk.debris ? "debris " : "", pk.id, pk.pos.x, pk.pos.y, pk.pos.z, pk.ground_y,
                            int(pk.resting), pk.age, sc.x, sc.y, sc.z, sc.y - pk.half * std::fabs(cu.y),
                            pk.shadow_centre(cf).y);
            }
            int ns = 0;                               // the live sparkles (FUN_00584860's sprites) and the oldest's look
            const weather::Sprite* s0 = nullptr;
            for (const auto& o : wx.objects())
                if (!o->dead && o->kind == weather::Obj::Child && o->s.d.tex == weather::Tex::EftTex96) { if (!ns++) s0 = &o->s; }
            if (s0) {
                const weather::Look lk = weather::look(*s0);
                std::printf("  [pickup] sparkles %d, the oldest: age %.0f at (%.2f, %.2f, %.2f) half %.3f alpha %.1f tint %06x\n", ns,
                            s0->age, s0->pos.x, s0->pos.y, s0->pos.z, lk.half, lk.alpha, s0->d.argb & 0xffffffu);
            }
        } else if (cmd == "fxall") {                  // every live effect: id, attach, origin (and from the player's), depth bias
            auto r2 = [](float v) { return std::fabs(v) < 0.005f ? 0.0f : v; };   // no "-0.00"
            for (const auto& f : fxsim.effects())
                if (!f->dead)
                    std::printf("  [fxall] id %d attach %d at (%.2f, %.2f, %.2f) rel (%.2f, %.2f, %.2f) bias %.2f%s\n", f->id,
                                f->attach_mode, f->M.m[3][0], f->M.m[3][1], f->M.m[3][2], r2(f->M.m[3][0] - feet.x),
                                r2(f->M.m[3][1] - (feet.y + kOriginH)), r2(f->M.m[3][2] - feet.z), f->depth_bias,
                                f->name.empty() ? "" : (" " + f->name).c_str());
        } else if (cmd == "actors") {                 // the live actors (audit aid): id, cast, enemy, hp, groups, pos
            for (const Actor& a : world.actors())
                if (!a.is_player())
                    std::printf("  [actor] id %d cast %d %s hp %.0f g1 %d pos (%.1f,%.1f,%.1f) hurt %.3f\n", a.instance_id, a.cast_id,
                                a.is_enemy() ? "enemy" : "npc", a.hp, int(a.tag.in_group(1)), a.pos.x, a.pos.y, a.pos.z,
                                a.hit_flash);
        } else if (cmd.rfind("level=", 0) == 0) {     // level=n: the Lua mods' set_level (audit aid; a cheat)
            player.level = std::clamp(std::atoi(cmd.c_str() + 6), 1, 60);
            if (ginfo.ok()) player.exp = float(ginfo.exp_to_level(player.level));
            player.max_hp = int(player_stats().max_hp);
            player.hp = player.max_hp;
        } else if (cmd.rfind("diff=", 0) == 0) {      // diff=n: the difficulty, 0 Very Easy .. 4 Nightmare, with its flags as FUN_0041fd10 (audit aid)
            difficulty = std::clamp(std::atoi(cmd.c_str() + 5), 0, 4);
            game.g_flags[144] = difficulty >= 2 ? difficulty - 2 : 0;
            game.g_flags[143] = difficulty <= 1 ? 1 : 0;
            game.g_flags[203] = difficulty == 0 ? 1 : 0;
            if (boss) { boss.reset(); g_rig.clear_boss(); }   // Velagunder's stats too (Boss01Setup)
            boss6.reset();                              // Zava's heads latch it in their ctor: built again next step
            boss7.reset();                              // ... and Dalles (view_boss07.inl)
            boss8.reset();                              // ... and Darm (view_boss08.inl)
        } else if (cmd == "flagdump") {               // every nonzero game flag, "i=v" (audit aid)
            std::string s;
            for (int i = 0; i < 0x200; ++i)
                if (game.g_flags[i]) s += " " + std::to_string(i) + "=" + std::to_string(game.g_flags[i]);
            std::printf("  [flags]%s\n", s.c_str());
        } else if (cmd == "nearest" || cmd.rfind("nearest=", 0) == 0) {   // stand 2.0 (or =d) from the nearest live enemy, facing it (audit aid)
            const float stand = cmd.size() > 8 ? float(std::atof(cmd.c_str() + 8)) : 2.0f;   // Hugo's shots: =4 [H]
            const Actor* best = nullptr;
            float bd = 1e30f;
            for (const Actor& a : world.actors()) {
                if (csMgr.world_frozen()) break;          // never during a cutscene
                if (a.is_player() || !a.is_enemy() || a.hp < 1.0f) continue;
                bool in_door = false;                     // and never next to a door (it would be taken)
                for (const SenTrigger& t : room.sen.triggers)
                    for (float r : {0.0f, 2.5f})
                        if (t.contains({a.pos.x + r, a.pos.y + 1.0f, a.pos.z}) || t.contains({a.pos.x - r, a.pos.y + 1.0f, a.pos.z}) ||
                            t.contains({a.pos.x, a.pos.y + 1.0f, a.pos.z + r}) || t.contains({a.pos.x, a.pos.y + 1.0f, a.pos.z - r}))
                            in_door = true;
                if (in_door) continue;
                const float d = (a.pos.x - feet.x) * (a.pos.x - feet.x) + (a.pos.z - feet.z) * (a.pos.z - feet.z);
                if (d < bd) { bd = d; best = &a; }
            }
            if (best) {
                float dx = feet.x - best->pos.x, dz = feet.z - best->pos.z;
                const float l = std::sqrt(dx * dx + dz * dz);
                if (l > 1e-3f) { dx /= l; dz /= l; } else { dx = 0; dz = -1; }
                const float x = best->pos.x + dx * stand, z = best->pos.z + dz * stand;
                // nearest=d probes from just above the enemy's feet, so a shooter lands on its level, not a ledge above
                const float fl = floor_at(phys_mesh(room), x, z, best->pos.y + (cmd.size() > 8 ? 1.0f : 3.0f));
                feet = {x, fl > kNoFloor ? fl : feet.y, z};
                if (fl > kNoFloor) ledge_serial = -1;
                face_dir = {-dx, 0.0f, -dz};
                facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
            }
        } else if (cmd == "scripts") {                // the live scripts and their pcs
            std::printf("  [drive]   frozen %d fix %d/%d popups %zu\n", int(csMgr.world_frozen()), csMgr.fix_counts[0], csMgr.fix_counts[1], popups.size());
            for (const auto& li : csMgr.live)
                std::printf("  [drive]   script %s pc %u wait %d%s\n", li.name.c_str(), li.pc, li.wait, li.finished ? " (done)" : "");
        } else if (cmd == "shell" || cmd.rfind("shell=", 0) == 0 || cmd == "shellback") {
            // open the F1 shell overlay (shell=<Tab>[:<sub>]), shoot it, close it; shellback = Esc / the pad's
            // Back on the current tab first (a sub-page returns to its page; ui::Page::back)
            // (the shell pauses the logic ticks, so the drive closes it itself after the shot)
            if (cmd == "shellback" && !shell.panel.pages.empty()) {
                ui::Page& cur = shell.panel.pages[std::size_t(std::clamp(shell.panel.tab, 0, int(shell.panel.pages.size()) - 1))];
                const bool went = cur.back && cur.back(cur.list);
                std::printf("  [drive] shell back on %s: %s\n", cur.name.c_str(), went ? "to the page" : "would close the panel");
            }
            if (cmd.size() > 6 && cmd != "shellback" && !shell.panel.select(cmd.substr(6)))
                std::printf("  [drive] shell: no page %s\n", cmd.c_str() + 6);
            shell.open = true;
            shell_drive_close = true;
            shell_shot_frames = 3;                       // ImGui lays the tab bar out a frame late
        } else if (cmd.rfind("blackout", 0) == 0) {    // blackout[=rgb]: hold the fader at full (a script's
            csHost.fade_cur = csHost.fade_tgt = 1.0f;  // Fader_Current/Target(1) left behind), Fader_Color rgb
            csHost.fade_rgb = cmd.size() > 9 ? std::uint32_t(std::stoul(cmd.substr(9), nullptr, 16)) & 0xffffffu : 0u;
            csHost.fade_rate = 1.0f;
        } else if (cmd.rfind("surfat=", 0) == 0) {     // surfat=<surface index>: where that surface is (audit aid)
            const int want = std::atoi(cmd.c_str() + 7);
            const RoomMesh& sm = phys_mesh(room);
            int shown = 0;
            for (std::size_t t = 0; t * 3 + 2 < sm.indices.size() && shown < 6; ++t) {
                if (surface_index_or_default(surface_of(sm, int(t))) != want) continue;
                Vec3 c{};
                for (int k = 0; k < 3; ++k) { const RoomVertex& v = sm.vertices[sm.indices[t * 3 + k]]; c = {c.x + v.px / 3, c.y + v.py / 3, c.z + v.pz / 3}; }
                if (t % 7 == 0 || shown == 0) { std::printf("  [surfat] %d tri %zu at (%.2f, %.2f, %.2f)\n", want, t, c.x, c.y, c.z); ++shown; }
            }
        } else if (cmd == "sand") {                    // the player's sand2 state (src/cleria/sand.hpp)
            std::printf("  [sand] surf %d/%d water %.2f sub %d buried %d grounded %d feet y %.3f O2 %.0f\n",
                        cur_surface, psand.surf_ray, psand.water_y, int(submerged), int(psand.buried),
                        int(pv.grounded), feet.y, oxy.o2);
        } else if (cmd == "fader") {                   // the fade machine 0x769f1c / 0x769f24 / 0x769f28
            std::printf("  [drive] fader %.2f -> %.2f at %.3f%s%s rgb %06x\n", csHost.fade_cur, csHost.fade_tgt, csHost.fade_rate,
                        door_pending ? " (door pending)" : "", door_latched ? " (latched)" : "", unsigned(csHost.fade_rgb));
        } else if (cmd == "loops") {                   // the looping SE voices
            std::printf("  [drive] t%llu loops:", (unsigned long long)tick_count);
            for (int id : audio.looping()) std::printf(" %d", id);
            std::printf("\n");
        } else if (cmd == "gauge") {                   // the boss HP gauge: shown (DAT_006d5fd8 != 0) or hidden
            std::printf("  [drive] gauge %d id %d hp %.0f/%.0f\n", int(hud.boss_frac >= 0.0f), hud.boss_id,
                        double(hud.boss_cur), double(hud.boss_max));
        } else if (cmd == "pos") {                     // print the player's feet and O2
            std::printf("  [drive] feet (%.3f, %.3f, %.3f) O2 %.0f a%d hp %d sub %d sec 0x%x stage %d sp %.0f shield %d/%d/%d\n", feet.x, feet.y, feet.z,
                        oxy.o2, oxy.alpha, player.hp, int(submerged), unsigned(avm.section()), pact.stage,
                        player.sp_cur, int(shield_on), shield_hits, shield_life);
        } else if (cmd.rfind("chr=", 0) == 0) {        // chr=<id>[,<id>..]: an actor's Chr state (CHR_OPS.md)
            std::string ids = cmd.substr(4);
            for (std::size_t q0 = 0; q0 <= ids.size();) {
                std::size_t q1 = ids.find(',', q0);
                if (q1 == std::string::npos) q1 = ids.size();
                const int id = std::atoi(ids.substr(q0, q1 - q0).c_str());
                q0 = q1 + 1;
                const Actor* a = world.find(id);
                if (!a) { std::printf("  [chr] %d absent\n", id); continue; }
                const bool pl = a->is_player();
                // Facing as the engine heading in degrees (0 = +X): hers from face_dir, an NPC's from yaw.
                const float h = pl ? std::atan2(face_dir.z, face_dir.x) : heading_of_yaw(a->yaw);
                int sec = -1, tag = -1, fr = -1;
                if (pl) { sec = avm.section(); tag = avm.anim_tag(); fr = avm.frame(); }
                else for (const EnemyRender& e : render)
                    if (e.actor == a && e.avm.ok()) { sec = e.avm.section(); tag = e.avm.anim_tag(); fr = e.avm.frame(); }
                std::printf("  [chr] %d face %.1f cmd %.1f mode %d tgt %d dir %.1f mult %.2f run %d pin %d/%d motion %d "
                            "aso %d/%d/%d tremble %d/%d/%d %.1f,%.1f%s foot %d fly %d%s z %d pos (%.2f, %.2f, %.2f)\n",
                            id, h * 57.29578f, a->look.cmd * 57.29578f, int(a->look.mode), a->look.target,
                            a->look.dir * 57.29578f, a->look.mult, int((a->flags12c & 0x20u) != 0),
                            a->pattern.tag, a->pattern.frame, a->anim_motion, sec, tag, fr,
                            a->tremble.ticks, a->tremble.period, a->tremble.cnt, a->tremble.dx, a->tremble.dy,
                            tremble_active(a->tremble) ? " ON" : "", int(a->footsteps), int(a->flyer),
                            a->fly_r2 != 0.0f ? " flyto" : "", int(a->ztest),
                            pl ? feet.x : a->pos.x, pl ? feet.y : a->pos.y, pl ? feet.z : a->pos.z);
            }
        } else if (cmd == "fx") {                      // the named (script-placed) effects, the glare byte
            std::printf("  [fx] glare %d\n", csHost.glare);
            for (const auto& f : fxsim.effects())
                if (!f->name.empty() && !f->dead)
                    std::printf("  [fx] '%s' id %d alpha %.3f -> %.3f attach %d at (%.2f, %.2f, %.2f)%s\n", f->name.c_str(),
                                f->id, f->alpha_mul, f->alpha_target, f->attach_mode, f->M.m[3][0], f->M.m[3][1],
                                f->M.m[3][2], f->kill ? " (killed)" : "");
        } else if (cmd.rfind("mesh=", 0) == 0) {       // mesh=<name>: a scene object's runtime state
            if (const MeshObj* mo = meshes.find(cmd.substr(5))) {
                int va = -1;                            // its largest drawn vertex alpha
                if (const RoomMesh::ObjRange* rr = room.mesh.object_range(mo->name))
                    for (std::uint32_t k = rr->v0; k < rr->v1 && k < room.mesh.vertices.size(); ++k)
                        va = std::max(va, int(room.mesh.vertices[k].a));
                std::printf("  [drive] mesh %s pos (%.2f, %.2f, %.2f) clip %d t %.1f alpha %.4f frame_y %.3f%s max a %d\n",
                            mo->name.c_str(), mo->pos.x, mo->pos.y, mo->pos.z, mo->sect, mo->time, mo->alpha,
                            mo->frame_y, mo->frame_y_ok ? "" : " (stale)", va);
                Vec3 fr{};                              // where its posed frame is now (a moving platform)
                if (mesh_frame(*mo, &fr))
                    std::printf("  [drive] mesh %s frame (%.2f, %.2f, %.2f)\n", mo->name.c_str(), fr.x, fr.y, fr.z);
            }
        } else if (cmd.rfind("obj=", 0) == 0) {        // obj=<name>: stand 1.5 from that scene object, facing it
            std::string ln = cmd.substr(4);
            float od = 1.5f;                                      // obj=name,dist: another distance (audit aid)
            if (const std::size_t cm = ln.find(','); cm != std::string::npos) { od = float(std::atof(ln.c_str() + cm + 1)); ln.resize(cm); }
            for (char& ch : ln) ch = char(std::tolower((unsigned char)ch));
            Vec3 at{};
            bool found = false;
            if (const MeshObj* mo = meshes.find(ln)) { at = mo->pos; found = true; }
            else for (const SceneTrigger& t : room.mesh.triggers) {
                std::string tn = t.name;
                for (char& ch : tn) ch = char(std::tolower((unsigned char)ch));
                if (tn == ln) { at = t.position; found = true; break; }
            }
            if (found) {
                float dx = feet.x - at.x, dz = feet.z - at.z;
                const float l = std::sqrt(dx * dx + dz * dz);
                if (l > 1e-3f) { dx /= l; dz /= l; } else { dx = 0; dz = -1; }
                const float x = at.x + dx * od, z = at.z + dz * od;
                const float fl = floor_at(phys_mesh(room), x, z, at.y + 3.0f);
                feet = {x, fl > kNoFloor ? fl : at.y, z};
                face_dir = {-dx, 0.0f, -dz};
                facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                std::printf("  [drive] at object %s (%.2f, %.2f, %.2f)\n", ln.c_str(), at.x, at.y, at.z);
            }
        } else if (cmd.rfind("run=", 0) == 0) {        // run=<script>: start a script (owner: the player)
            const std::string nm = cmd.substr(4);
            std::printf("  [drive] run %s -> %s\n", nm.c_str(), csMgr.start(nm, {}, 13) ? "started" : "not found");
        } else if (cmd == "water") {                   // drop the player over the first water triangle
            const RoomMesh& pm2 = phys_mesh(room);
            for (std::size_t t = 0; t < pm2.tri_water.size(); ++t) {
                if (!pm2.tri_water[t]) continue;
                Vec3 c{};
                for (int k = 0; k < 3; ++k) {
                    const RoomVertex& v = pm2.vertices[pm2.indices[t * 3 + std::size_t(k)]];
                    c.x += v.px / 3.0f; c.y += v.py / 3.0f; c.z += v.pz / 3.0f;
                }
                const float fl = floor_at(pm2, c.x, c.z, c.y + 50.0f);
                feet = {c.x, c.y + 0.5f, c.z};
                pv = VerticalState{}; pv.grounded = false;
                std::printf("  [drive] water tri %zu surface %.2f floor below %.2f at (%.2f, %.2f)\n", t, c.y, fl, c.x, c.z);
                break;
            }
        } else if (cmd.rfind("move=", 0) == 0) {       // move=dx,dz,n: hold a world direction n ticks
            float dx = 0, dz = 0; int nn = 0;
            if (std::sscanf(cmd.c_str() + 5, "%f,%f,%d", &dx, &dz, &nn) == 3) { drive_wish = {dx, 0, dz}; drive_wish_n = nn; }
        } else if (cmd.rfind("goto=", 0) == 0) {       // goto=x,z,n: hold the stick toward the world point (x, z) for n ticks, or until 0.2 from it
            float x = 0, z = 0; int nn = 0;
            if (std::sscanf(cmd.c_str() + 5, "%f,%f,%d", &x, &z, &nn) == 3) { drive_goto = {x, 0, z}; drive_goto_n = nn; }
        } else if (cmd.rfind("bracelet=", 0) == 0) {   // bracelet=<item id>: the equipped bracelet 0x76a630
            inv.magic = std::atoi(cmd.c_str() + 9);
        } else if (cmd.rfind("held=", 0) == 0) {       // held=<item id>: the Item page's held tool 0x76a62c (97 = the Scales)
            inv.held = std::atoi(cmd.c_str() + 5);
        } else if (cmd.rfind("tier=", 0) == 0) {       // tier=n: the character's weapon tier inv.tier[ch] (audit aid; a cheat)
            inv.tier[std::clamp(character, 0, 3)] = std::clamp(std::atoi(cmd.c_str() + 5), 0, 10);
        } else if (cmd.rfind("acc=", 0) == 0) {        // acc=<item id>: the equipped accessory 0x76a628 (54 = the Mask of Eyes; -1 none)
            inv.accessory = std::atoi(cmd.c_str() + 4);
            (void)player_stats();                         // FUN_00420c40 -> FUN_004208f0: 0x76bbac
        } else if (cmd == "skiptap" || cmd == "oktapf") {   // a player who only presses inside a cutscene (a Fix held):
            if (csMgr.world_frozen())                        // Esc (the skip) / Attack-OK, through the mapper
                access_drive(cmd == "skiptap" ? std::string("esctap") : std::string("oktap"));
        } else if (cmd.rfind("skiphold=", 0) == 0) {   // skiphold=n: hold Esc n ticks, only from inside a cutscene
            if (csMgr.world_frozen()) access_drive("eschold=" + cmd.substr(9));
        } else if (cmd == "killall") {                 // every live enemy gone (the S_1000 intro's waves)
            std::vector<int> ids;
            for (const Actor& a : world.actors()) if (a.is_enemy() && a.alive()) ids.push_back(a.instance_id);
            for (int id : ids) world.despawn(id);
            csHost.world_changed = true;
        } else if (cmd == "wndtext") {                 // the message window: reveal, glyphs, extfont cells
            const RichLayout& wl = csHost.wtext.layout();
            int next = 0;
            std::string cells;
            for (const RichGlyph& gl : wl.glyphs)
                if (gl.ext && ++next <= 8) cells += " " + std::to_string(int(gl.c));
            std::printf("  [drive] wndtext alive %d reveal 0x%x/%d glyphs %zu ext %d cells%s\n", int(csHost.wtext.alive()),
                        unsigned(csHost.wtext.reveal()), wl.counted, wl.glyphs.size(), next, cells.c_str());
        } else if (cmd == "mask") {                    // the Mask of Eyes objects (view_mask.inl)
            std::printf("  [drive] mask %d objs %zu\n", int(mask_of_eyes_on(inv)), room.mask_objs.size());
            for (const Room::MaskObj& m : room.mask_objs) {
                Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};   // its drawn bounds
                if (m.rr < room.mesh.object_ranges.size())
                    for (std::uint32_t k = room.mesh.object_ranges[m.rr].v0; k < room.mesh.object_ranges[m.rr].v1; ++k) {
                        const RoomVertex& v = room.mesh.vertices[k];
                        lo = {std::min(lo.x, v.px), std::min(lo.y, v.py), std::min(lo.z, v.pz)};
                        hi = {std::max(hi.x, v.px), std::max(hi.y, v.py), std::max(hi.z, v.pz)};
                    }
                std::printf("  [drive] maskobj %zu bits %u hidden %d alpha %.4f coll %s box (%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f)\n",
                            m.rr, m.bits, int(m.hidden), m.alpha, m.cr < 0 ? "none" : (m.coll_off ? "off" : "on"),
                            lo.x, lo.y, lo.z, hi.x, hi.y, hi.z);
            }
        } else if (cmd == "saveload") {                // save here, encode + decode the record, load it
            const auto rec = SaveRecord::decode(to_record(capture_save(), "x", "01/01/2026", "00:00:00").encode(1u, 2u));
            if (rec) {
                apply_loaded(from_record(*rec), /*force_scene*/true);
                std::printf("  [drive] loaded %s feet (%.3f, %.3f, %.3f)\n", room.name.c_str(), feet.x, feet.y, feet.z);
            }
        } else if (cmd.rfind("hp=", 0) == 0) {         // hp=<n>: the player's HP (0 = the death)
            // Capped at the max like the game's own heals: above it the passive Boost fill
            // ((1 - hp/max) x B + 1) x M goes negative, and retail clamps neither the fill nor
            // the gauge add (0x467c43..0x467d8c, FUN_0046cc10; ROUTE_F13 D8).
            player.hp = std::clamp(std::atoi(cmd.c_str() + 3), 0, player.max_hp);
        } else if (cmd.rfind("sp=", 0) == 0) {         // sp=<n>: the SP wallet 0x76a75c
            player.sp = std::atoi(cmd.c_str() + 3);
        } else if (cmd == "stats") {                   // EXP / level / HP / SP (the access checks)
            std::printf("  [drive] stats exp %.0f lv %d hp %d/%d sp %d\n", double(player.exp), player.level, player.hp,
                        player.max_hp, player.sp);
        } else if (cmd == "arena") {                   // the arena fence: mode (flag 190), rect, ring distance
            std::printf("  [drive] arena mode %d rect (%g, %g, %g, %g) dist %.2f\n", game.g_flags[190], world.arena[0],
                        world.arena[1], world.arena[2], world.arena[3],
                        std::hypot(feet.x - world.arena[0], feet.z - world.arena[1]));
        } else if (cmd == "hudskill") {                // the HUD's skill badge cell (view_hud.inl skill_badge_cell)
            Rc c{};
            if (skill_badge_cell(inv.magic, character, player.boosting, c))
                std::printf("  [drive] hud skill %d badge (%.0f, %.0f)\n", inv.magic, c.x, c.y);
            else std::printf("  [drive] hud skill %d badge none\n", inv.magic);
        } else if (content_drive(cmd)) {               // cstate, give=, csave=, cload= (view_content.inl)
        } else if (access_drive(cmd)) {                // opt=Key=value, esctap, eschold=n (view_access.inl)
        } else if (video_drive(cmd)) {                 // vopt=Key=value, vstat (view_video.inl)
        } else if (cmd == "jump") {
            drive_jump = true;
        } else if (cmd == "skill+" || cmd == "skill-") {   // hold / release the skill button
            drive_skill = cmd == "skill+";
        } else if (cmd == "act") {
            try_interact();
        } else if (cmd.rfind("mouse=", 0) == 0) {      // the virtual pointer (view_input.inl)
            int X = 0, Y = 0;
            if (std::sscanf(cmd.c_str() + 6, "%d,%d", &X, &Y) == 2) g_sdl_in.place_virtual(X, Y);
        } else if (cmd == "click" || cmd == "rclick") {
            g_sdl_in.vtap[cmd == "click" ? 0 : 1] = true;
        } else if (cmd == "mhold" || cmd == "mrelease") {
            g_sdl_in.vbtn[0] = cmd == "mhold";
        } else if (cmd.rfind("wheel=", 0) == 0) {
            g_sdl_in.vwheel = std::atoi(cmd.c_str() + 6);
        } else if (cmd == "cursor") {
            std::printf("  [drive] cursor rec (%d, %d) drawn (%.0f, %.0f) visible %d mouse %d moved %d menu %d alpha %.2f "
                        "colour %08x move %.3f draw %.3f hold %d\n", g_in.x, g_in.y, g_cursor.X, g_cursor.Y,
                        int(g_cursor.visible), int(g_cursor.mouse_mode), int(g_cursor.moved), int(g_cursor.menu),
                        g_cursor.alpha, unsigned(g_cursor.colour), g_cursor.move_angle, g_cursor.draw_angle, g_cursor.hold);
        } else if (drive_book && drive_book(cmd)) {    // ok / esc / up / down while the book is up
        } else if (cmd == "ok") {                      // confirm: the windows, the game-over menu
            csHost.advance = true;
        } else if (cmd == "esc") {                     // a script menu's cancel
            csHost.wnd_cancel = true;
        } else if (cmd == "up" || cmd == "down") {     // one menu cursor step
            csHost.wnd_vstep = cmd == "up" ? -1 : 1;
        } else if (cmd == "shot") {
            const char* d = std::getenv("CLERIA_SHOT_DIR");
            drive_shot = (fs::path(d ? d : ".") / ("t" + std::to_string(tick_count) + ".ppm")).string();
        } else if (cmd == "quit") {
            running = false;
        }
    };
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
    FaceCache faces;                                      // the message box's face (view_book.inl)
    auto face_tex = [&](int face, int frame) { return faces.get(g, asset_root, face, frame); };
    const ItemTable pitems = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
    bool paused = false;
    // The pause book (src/cleria/pausebook, build/menu_re/PAUSEBOOK_DRAW.md), drawn by
    // view_book.inl. It steps on the 60 Hz tick; Confirm / Cancel are key edges, the
    // directions come from the held keys with the engine's auto-repeat (§0.7).
    PauseBook book;
    BookRenderer bookR;
    const bool bookOk = bookR.init(g, fmt, asset_root, pitems);
    csHost.wmetrics = bookR.metrics();
    csHost.bust_set.size_of = [&](const std::string& tex, int* w, int* h) { return bookR.named_size(g, tex, w, h); };
    // FUN_00573210: the chest item 0.3 above the named mesh (no mesh: nothing is spawned).
    // FUN_00573150: Mesh_DropItem spawns one drop 0.5 above the mesh (flags 0xd when kept).
    auto drop_at = [&](const Vec3& at, int item, bool keep) {
        SpawnArgs a;
        a.flags = keep ? 0xd : 5;
        spawn_pickups(pick_rng, &item, 1, {at.x, at.y + 0.5f, at.z}, a, water_mode == 2, pickups);
    };
    csHost.drop_item_fn = [&](const std::string& mesh, int item, int keep) {
        if (const MeshObj* mo = meshes.find(mesh)) {
            drop_at(mo->pos, item, keep != 0);
            std::printf("  [drop] item %d at %s\n", item, mesh.c_str());
        }
    };
    csHost.get_item_fn = [&](int item, int flag, const std::string& mesh) {
        const MeshObj* mo = meshes.find(mesh);
        if (!mo) { std::printf("  [0x116] no mesh '%s'\n", mesh.c_str()); return; }
        pickups.push_back(make_chest_item(pick_rng, item, mo->pos, flag != 0));
    };
    bookR.face_bind = [&](int face, int frame) -> WGPUBindGroup {
        const SpriteTex* t = face_tex(face, frame);
        return t ? t->bind : nullptr;
    };
    lua_seed_icons(g, pauseUi.iconCache, bookR.icon_tex,     // the Lua mods' icons (view_lua.inl)
                   [&](const fs::path& p) { return bookR.load(g, p); });
    BookInput bookIn;
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
        m.player_can_act = player.hp > 0 && pv.grounded;   // +0x388; [not modelled] motion states 4 / 5
        m.underwater = water_mode == 2;                    // [not modelled] submerged (+0x38a)
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
            // The bound actor (+0x240, what 998 resolves to): FUN_005abd10 starts @CheckWing, @CheckWing2
            // and @UseWing with the player *0x74c09c [BYTES 0x5ac53e, 0x5ac59f, 0x5accea], the other Use
            // scripts with none [BYTES 0x5ac411]. Without it @UseWing's Eft_LocateChr(51, 998, .., 'warp'),
            // Chr_Dir / Chr_Alpha(998) found no actor: the warp's rising rings and fade-out never drew.
            const int owner = (name == "@UseWing" || name == "@CheckWing" || name == "@CheckWing2") ? 13 : 0;
            if (!csMgr.start(name, {}, owner)) std::printf("  [book] %s did not start\n", name.c_str());
            std::copy(csMgr.vm.g_flags, csMgr.vm.g_flags + 0x200, game.g_flags);
        };
        return m;
    };
    std::fprintf(stderr, "[pause menu] Esc opens the pause book; assets %s\n",
                 pauseOk ? "loaded (ready)" : "MISSING TITLE_000.DDS -> book won't draw");
    mouse::Point book_pt;                                  // the book's open-time cursor (this+0x128)
    auto open_book = [&](int page) {
        book_pt = {g_in.x, g_in.y};                        // FUN_005a4fd0 saves the record's cursor
        paused = true;
        book.open(book_model(), page);
        bookIn = {};
        bookAccum = 0;
    };
    int drive_book_v = 0;                                  // a CLERIA_DRIVE up / down for the book
    int drive_book_h = 0;                                  // and left / right
    drive_book = [&](const std::string& c) {
        if (!(paused && book.is_open())) return false;
        if (c == "ok") bookIn.confirm = true;
        else if (c == "esc") bookIn.cancel = true;
        else if (c == "up" || c == "down") drive_book_v = c == "up" ? -1 : 1;
        else if (c == "left" || c == "right") drive_book_h = c == "left" ? -1 : 1;   // (audit aid)
        else return false;
        return true;
    };
    // 0xe0 SaveMenu_Enter (FUN_00574eb0 -> FUN_00443150): the book's Save page, refused while
    // HP < 1 (0x76a730 < 1.0) or with the book already up. [not modelled] the second window
    // gate 0x730294.
    csHost.save_menu_fn = [&] {
        if (player.hp < 1 || (paused && book.is_open())) return;
        std::printf("  [script] SaveMenu_Enter -> the book's Save page\n");
        open_book(1);
    };
    // The continue snapshot 0x767f50 (FUN_00443d70; SCENEFLOW.md §6.3): the save block and the
    // player's x / foot y / z / heading +0x314, with the scene. Taken by 0x96 SaveContData, by
    // every save write (FUN_0042e9a0) and after a load (FUN_00431260's "load" label); the game
    // over's Retry restores it. In memory only, like the engine's.
    struct ContSnap { SaveState s; Vec3 feet{}; Sidecar sc; bool valid = false; } cont;   // sc: the mods' sidecar (not retail)
    auto save_cont = [&] {
        apply_script_grants();                    // a script's writes this burst are in the block
        cont.s = capture_save();
        cont.feet = feet;
        cont.sc = g_content.sc;
        cont.valid = true;
        std::printf("  [cont] snapshot %s (%.2f, %.2f, %.2f) hp %d\n", cont.s.scene.c_str(),
                    feet.x, feet.y, feet.z, player.hp);
    };
    csHost.save_cont_fn = save_cont;
    // 0x84 / 0xaf: the SP wallet 0x76a75c and itemArr 0x76a654 (the blessing shop's purchase).
    csHost.gold_get_fn = [&] { return player.sp; };
    csHost.gold_set_fn = [&](int v) { player.sp = v; };
    csHost.item_arr_fn = [&](int i, int v) {
        game.item_arr[i] = v;
        inv.raval[i] = v;                         // [approx] FUN_00420c40's recompute: player_stats()
    };

    // A title-screen Load lands here: flags/character/room were applied before the room
    // load above; apply_loaded now sets the stats and the saved position (idempotent on
    // the flags), completing the engine's init-then-overwrite load (SAVELOAD §4.2).
    if (pending_load) {
        apply_loaded(*pending_load);
        pending_load.reset();
        save_cont();                              // FUN_00431260 "load": the continue snapshot
        hud.debug_message("Game loaded.");
    }

    // Establish the region camera for the INITIAL room before the first tick — a
    // room-entry cutscene can freeze on frame 1, and apply_cam_state needs a valid
    // g_cam (pitch/dist) or it puts the eye at the scroll look-at (in the floor).
    if (!fly) { update_camera(room, feet, 0.0f, /*snap=*/true); if (g_cam.on) apply_cam_state(cam, feet); }

    // A scene path ("s_10\s_1009\", "demo3\") -> the room to enter. A demo scene is a movie
    // that then warps to a fixed next scene (SCENEFLOW.md 5.1, src/cleria/movie demo_scene); the
    // movie itself is played by the ChangeScene handler below. "" = no room (the title, an
    // unknown name).
    auto scene_room = [&](std::string s) -> std::string {
        if (const int n = demo_number(s)) s = demo_scene(n, character == 3)->next;   // demo6 -> title
        for (char& c : s) c = (c == '/') ? '\\' : char(std::tolower((unsigned char)c));
        while (!s.empty() && s.back() == '\\') s.pop_back();
        const std::size_t k = s.find_last_of('\\');
        std::string room_name = k == std::string::npos ? s : s.substr(k + 1);
        for (char& c : room_name) c = char(std::toupper((unsigned char)c));
        return g.sob_index.count(room_name) ? room_name : std::string();
    };

    // ---- Velagunder, boss01 (src/cleria/boss01, build/boss_re/VELAGUNDER.md) ----------------
    // The GoMesh "boss01" a room script Mesh_Locate'd is driven by the Boss01 brain every live
    // step (the mesh pool runs under a Fix too): the clip advance, then the brain (FUN_00562990
    // switch +0x28d case 5), then its requests (view_boss01.inl). [not modelled] the boss01_hit1/2
    // solid meshes, the mesh-contact damage (§3.4).
    std::array<int, boss01::kBones> boss_node{};
    // The attach frames (FUN_00451c20 parents) of the boss's effects: the 24 posed bones, Bone(114),
    // the blk+0x1a0 Bone(115) copy, the laser beam blk+0x140 and part 0; refreshed every boss tick.
    std::array<ef2sim::M4, boss01::kBones> boss_bone_m{};
    ef2sim::M4 boss_b114_m, boss_b115_m, boss_beam_m, boss_part0_m;
    struct BossEntry { std::uint32_t t0 = 0, t1 = 0, flags = 0; };   // obj+0x25c (view_boss01_touch.inl)
    std::vector<BossEntry> boss_entries;
    bool boss_entries_built = false;
    int boss_n114 = -1, boss_n115 = -1;
    std::vector<RehitBank> boss_banks;                    // the parts' own re-hit banks (+0x14c)
    bool player_hurt_this_tick = false;                   // +0x3bc & 2 (the boss reads it)
    // FUN_004faf20's tail for a hit on one of the boss's GoXx parts [EXPORT 004faf20.c:239-246, 434-448]: SE 0x5c
    // (0x5d when the damage is 0; one per 51 ms, 0x74e7e4 [approx: 4 ticks]) and, on a part without armour
    // (+0x138 clear: part 1 outside the stun, the core in the stun loop), the white damage number
    // FUN_004fae00(0xffffffff). [H] both at the part's +0x50. [not modelled] the hit spark FUN_00450410.
    std::uint64_t boss_part_se_tick = 0;
    // The same tail for every boss's parts that take FUN_004faf20(0, 0) (Nygtilger's head / tail, Gelaldy,
    // Khonsclard, Pictimos, Darm): `armor` = the part's +0x138. Where a brain does not model +0x138 the caller
    // passes true: the SE only, no number [not modelled].
    auto part_hit_feedback = [&](const Vec3& pos, bool armor, int dmg, int cls) {
        if (tick_count - boss_part_se_tick >= 4) {
            boss_part_se_tick = tick_count;
            sfx_at(dmg > 0 ? hitfx::kSeHit : hitfx::kSeNoDamage, pos, kSfxAnimVol);
        }
        if (!armor) damage_popup(hudstate::dealt_popup_value(dmg, cls, game.g_flags), 0xffffff, pos);
    };
    auto boss_part_feedback = [&](const Boss01Part& bp, int dmg, int cls) { part_hit_feedback(bp.pos, bp.armor, dmg, cls); };
    auto boss_tick = [&] {
        MeshObj* bm = meshes.find("boss01");
        if (!bm) { boss.reset(); g_rig.clear_boss(); boss_entries_built = false; return; }
        Model* mdl = cached_big_model(resolve_ymo_path(asset_root, "data\\enemy\\boss01\\boss01.ymo"));
        if (!mdl) return;
        auto lw = [](std::string t) { for (char& ch : t) ch = char(std::tolower((unsigned char)ch)); return t; };
        auto node_of = [&](const char* nm) {
            for (std::size_t k = 0; k < mdl->nodes.size(); ++k) if (mdl->nodes[k].name == nm) return int(k);
            return -1;
        };
        if (boss && boss_load[0] != mesh_load_serial) { boss.reset(); g_rig.clear_boss(); boss_entries_built = false; }
        if (!boss) {
            boss_load[0] = mesh_load_serial;
            Boss01Setup su;                                // EEX record 0 (§2.2)
            if (!eex.bosses.empty()) {
                const BossStats& bs = eex.bosses[0];
                su.hp = bs.hp; su.str = bs.atk; su.def = bs.def; su.rec4 = bs.unk10; su.level = bs.level;
            }                                              // rec3 (+0x0c, 50.0) is not in EexTable: the default
            // [H] +0xb0 = the GAMEINFO enemy-EXP curve at the boss level (FUN_004217f0 not reduced).
            if (ginfo.ok()) su.exp_b0 = ginfo.curves[kEnemyExp].at(su.level);
            su.character = character;
            su.diff_index = game.g_flags[144];
            su.diff_easy = game.g_flags[143] != 0;
            su.diff_very_easy = game.g_flags[203] != 0;
            su.time_attack = game.g_flags[193] != 0;
            su.retries = game.g_flags[196];
            su.pos = bm->pos;
            su.player_pos = {feet.x, feet.y + kOriginH, feet.z};
            boss = std::make_unique<Boss01>(su);
            for (int k = 0; k < boss01::kBones; ++k) boss_node[std::size_t(k)] = node_of(boss01::kBoneNames[k]);
            boss_n114 = node_of("Bone(114)");
            boss_n115 = node_of("Bone(115)");
            boss_banks.assign(boss01::kParts, RehitBank{});
            std::printf("  [boss] Velagunder: HP %.0f STR %.0f DEF %.0f lv %d\n",
                        boss->max_hp(), boss->str(), boss->def(), boss->level());
        }
        for (RehitBank& b : boss_banks) b.tick();
        // The posed model at this step's clip frame (FUN_005f6000), placed at the boss matrix.
        std::string cur_clip;
        if (bm->sect >= 0 && std::size_t(bm->sect) < bm->clips.size()) cur_clip = bm->clips[std::size_t(bm->sect)].name;
        bool posed = false;
        for (const AnimClip& c : mdl->clips)
            if (!cur_clip.empty() && c.name == cur_clip) { mdl->pose(c, bm->time); posed = true; break; }
        if (!posed) mdl->rest_pose();
        const Mat4 place = compose_trs(bm->pos, bm->rot, {1, 1, 1});
        auto world_of = [&](int ni) -> Vec3 {
            if (ni < 0 || std::size_t(ni) >= mdl->nodes.size()) return bm->pos;
            const Mat4 w = mdl->nodes[std::size_t(ni)].world * place;
            return {w.m[12], w.m[13], w.m[14]};
        };
        auto frame_of = [&](int ni) {
            ef2sim::M4 r;
            if (ni < 0 || std::size_t(ni) >= mdl->nodes.size()) { r.m[3][0] = bm->pos.x; r.m[3][1] = bm->pos.y; r.m[3][2] = bm->pos.z; return r; }
            const Mat4 w = mdl->nodes[std::size_t(ni)].world * place;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) r.m[i][j] = w.m[std::size_t(i * 4 + j)];
            return r;
        };
        for (int k = 0; k < boss01::kBones; ++k) boss_bone_m[std::size_t(k)] = frame_of(boss_node[std::size_t(k)]);
        boss_b114_m = frame_of(boss_n114);
        boss_b115_m = frame_of(boss_n115);
        Boss01Input in;
        for (int k = 0; k < boss01::kBones; ++k) in.bones[std::size_t(k)] = world_of(boss_node[std::size_t(k)]);
        in.bone114 = world_of(boss_n114);
        in.bone115 = world_of(boss_n115);
        in.player_pos = {feet.x, feet.y + kOriginH, feet.z};
        in.player_hp = float(player.hp);
        in.player_grounded = grounded;
        in.player_floor = last_floor;
        in.player_surface = cur_surface;                  // +0x4b9: 5 (wood) high up -> the taunt (§4.3)
        in.player_section = avm.section();
        in.player_took_damage = player_hurt_this_tick;
        in.clip = boss01::kWait;
        for (int k = 0; k < boss01::kClips; ++k)
            if (!cur_clip.empty() && lw(cur_clip) == lw(boss01::kClipTable[k].name)) { in.clip = k; break; }
        in.clip_count = bm->count;
        in.clip_frame = bm->time;
        in.fight = game.g_flags[156] == 1;
        in.continue_snap = game.g_flags[175] != 0;
        for (const Actor& a : world.actors()) {
            if (a.cast_id == 0xaf && a.alive()) {
                ++in.spawn_count;
                if (a.ai.i(0x72c) == 1) in.spawn_chaser_exists = true;
            }
            in.actors.push_back({a.cast_id, {a.pos.x, a.pos.y + a.height, a.pos.z},
                                 a.is_player() ? float(player.hp) : a.hp});
        }
        in.rng = [&] { return pick_rng.next(); };
        std::vector<bullet::Bullet*> in_bullets;          // DAT_00730248, in list order
        for (auto& bp : bullets.list()) {
            if (bp->gone || bp->destroyed) continue;
            in.bubbles.push_back({bp->s.effect, bp->s.pos});
            in_bullets.push_back(bp.get());
        }
        // The region camera owns the selection globals the brain reads and writes.
        boss->camera().cur = g_rig.current(); boss->camera().prev = g_rig.from();
        boss->camera().t = g_rig.blend_t(); boss->camera().forced = g_rig.forced();
        const Boss01Output out = boss->tick(in);
        for (int id : out.rumble) pad_rumble(id);        // FUN_005d3500 (view_pad.inl)
        if (out.camera_written) {
            const Boss01Camera& bc = boss->camera();
            g_rig.write_sel(bc.cur, bc.prev, bc.t, bc.rate);
            if (bc.forced != g_rig.forced()) g_rig.write_forced(bc.forced);
        }
        if (out.cam_snapshot) {
            g_rig.write_fixed(0xb, out.cam_snapshot->first, out.cam_snapshot->second, 27.0f, 0.24434610f, 0.61086524f);
        }
        // FUN_0042bb10: the presets claim the current slot while flag 156 (the fight) is on.
        if (game.g_flags[156] != 0 && !boss->destroyed()) {
            CamRig::BossPresets bp;
            bp.eye = boss->cam_eyes(); bp.target = boss->cam_targets();
            if (out.cam_snapshot) { bp.snap_eye = out.cam_snapshot->first; bp.snap_target = out.cam_snapshot->second; }
            else { bp.snap_eye = g_rig.slot(0xb).eye.fixed_pos; bp.snap_target = g_rig.slot(0xb).target.fixed_pos; }
            g_rig.set_boss(bp);
        } else {
            g_rig.clear_boss();
        }
        static const bool boss_log = std::getenv("CLERIA_BOSSLOG") != nullptr;
        if (boss_log && tick_count % 30 == 0)
            std::printf("  [boss] t%llu state %d sub %d c0 %u phase %d hp %.0f stun %.0f%s clip %s(%d) cnt %u fr %.1f cam %d/%.2f core (%.2f,%.2f,%.2f) r%.2f def %.0f feet (%.2f,%.2f,%.2f)\n",
                        (unsigned long long)tick_count, boss->state(), boss->sub(), boss->timer(), boss->phase(),
                        boss->hp(), boss->stun_gauge(), boss->stunned() ? " STUNNED" : "", cur_clip.c_str(),
                        in.clip, in.clip_count, in.clip_frame, boss->camera().cur, boss->camera().t, boss->part(0).hurt_centre.x, boss->part(0).hurt_centre.y, boss->part(0).hurt_centre.z, boss->part(0).hurt_radius, boss->part(0).def, feet.x, feet.y, feet.z);
        // The requests, in order.
        for (const Boss01ClipSet& c : out.clips)
            if (c.clip >= 0 && c.clip < boss01::kClips) meshes.set_clip_now("boss01", boss01::kClipTable[c.clip].name);
        if (out.speed) bm->time_scale = *out.speed;
        bm->pos = boss->pos();
        bm->rot = {0.0f, boss->yaw(), 0.0f};
        bm->dirty_pose = true;
        for (const Boss01Sound& sd : out.sounds) {
            if (sd.kind == Boss01Sound::k3D) sfx_at(sd.id, sd.pos, kSfxAnimVol);
            else if (sd.kind == Boss01Sound::k2D) audio.play(sd.id, kSfxAnimVol);
        }
        if (out.beam) {                                    // blk+0x140 (state 5), the beam's attach frame
            const Mat4& bmx = *out.beam;
            for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) boss_beam_m.m[i][j] = bmx.m[std::size_t(i * 4 + j)];
        }
        boss_part0_m = ef2sim::translation(boss->part(0).pos.x, boss->part(0).pos.y, boss->part(0).pos.z);
        #include "view_boss01.inl"
        feet.x += out.player_push.x;                       // [approx] +0x338/+0x340 added as a shift
        feet.z += out.player_push.z;
        if (out.tilt) shake(out.tilt->amp.x, out.tilt->amp.y, out.tilt->amp.z, out.tilt->frames);
        if (out.zoom.strength || out.zoom.rate)            // ZoomBlur: either word alone keeps the other
            zoom_blur(out.zoom.strength.value_or(zb.s), out.zoom.rate.value_or(zb.r));
        if (out.blur_centre) zoom_centre(*out.blur_centre);   // FUN_005e0590 / 0x50fcca
        if (out.alpha_target) bm->alpha_target = *out.alpha_target;
        if (out.alpha_step) bm->alpha_step = *out.alpha_step;
        for (const auto& [fi, fv] : out.flags) if (fi >= 0 && fi < 0x200) game.g_flags[fi] = fv;
        if (out.award_exp) award_exp(boss->exp_b0(), boss->level(), /*boss*/true, false, 0);
        if (out.destroy) { bm->alpha = bm->alpha_target = 0.0f; }
        if (out.gauge) {                                   // §6: the HP2 bar, 306 px, strip 1
            hud.set_boss_gauge(-5, out.gauge->hp, out.gauge->max, out.gauge->len_scale, out.gauge->strip);
            gauge_owner = 0;
        }
        if (out.gauge_hide) hud.hide_boss_gauge();
        player_hurt_this_tick = false;
    };

    // ---- Nygtilger, boss02 (src/cleria/boss02, build/boss_re/NYGTILGER.md §11 / §15) --------
    // The GoMesh "boss02_h" the room script Mesh_Locate's builds the whole worm (§2.1): the host
    // locates the other 35 pieces (b01..b17, c18, d01..d17) and drives all 36 from the Boss02
    // brain every live step, after the clip advance (FUN_00562990 case 6). Each piece is placed
    // by its full matrix (MeshObj::world). No boss camera (N10).
    // [not modelled] the stone shells SBOSS02* (§5.5: the statue look before the wake), the
    // glows / arcs / debris (cosmetic; Boss02Input::cosmetic is not hooked, so after a debris
    // call the shared RNG runs behind the engine's), the option-byte sparks,
    // the looped SE stop/start (216 / 217 play once), the touch's section-9 reaction (U2).
    constexpr int kB2Owner = -0xB02;                       // fx owner tag for the slot effects
    std::array<RehitBank, boss02::kObjects> boss2_banks{}; // FUN_004faac0 per piece (the mesh path)
    std::array<RehitBank, 2> boss2_part_banks{};           // the head / tail kind-9 parts (+0x14c)
    ef2sim::M4 boss2_mouth, boss2_hoop, boss2_bone02;      // the attach parents G+0x80 / G+0x130 / Bone02
    // The render models, posed per piece for the entry bones and Bone02 ([approx] the broken
    // shells share the skeleton, so the base models stand in after a swap).
    std::array<Model, 3> boss2_mdl;                        // head, b, c
    std::array<std::array<int, 2>, 3> boss2_entry_node{};  // role -> "Bone01"/"Bone01(5)", "l_reg00"/"r_reg00"
    int boss2_bone02_node = -1;
    bool boss2_mdl_ok = false;
    auto to_m4 = [](const Mat4& a) {
        ef2sim::M4 r;
        for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) r.m[i][j] = a.m[std::size_t(i * 4 + j)];
        return r;
    };
    // Kind 0x24's carrier (BULLET_KINDS.md §5): Nygtilger's segment by its object index — its matrix, HP and the
    // STR of its stat block (the strike's effect 0x15b is armed with it).
    boss2_carrier = [&](int o, ef2sim::M4* w, float* hp, float* atk) {
        if (!boss2 || o < 0 || o >= boss02::kObjects || !boss2->object(o).exists) return false;
        *w = to_m4(boss2->object(o).world);
        *hp = boss2->object(o).hp;
        *atk = boss2->object(o).str;
        return true;
    };
    // The posed node `ni` of piece o's model at its current clip frame, placed by the piece.
    auto boss2_node_world = [&](int o, int ni) -> Mat4 {
        const Boss02Piece& p = boss2->object(o);
        const int mi = o == 0 ? 0 : (o == boss02::kPieces - 1 ? 2 : 1);
        Model& mdl = boss2_mdl[std::size_t(mi)];
        if (!boss2_mdl_ok || ni < 0 || std::size_t(ni) >= mdl.nodes.size()) return p.world;
        const MeshObj* m = meshes.find(boss02::object_name(o));
        bool posed = false;
        if (m && m->sect >= 0 && std::size_t(m->sect) < m->clips.size())
            for (const AnimClip& c : mdl.clips)
                if (c.name == m->clips[std::size_t(m->sect)].name) { mdl.pose(c, m->time); posed = true; break; }
        if (!posed) mdl.rest_pose();
        return mdl.nodes[std::size_t(ni)].world * p.world;
    };
    // A Boss02Output (a tick's, or accept_hit's feedback), applied in order.
    auto boss2_apply = [&](const Boss02Output& out) {
        for (int id : out.rumble) pad_rumble(id);        // FUN_005d3500 (view_pad.inl)
        for (const Boss02ClipSet& c : out.clips) {
            const char* nm = boss02::object_name(c.object);
            if (c.by_index) meshes.set_clip_index(nm, c.clip, 0);       // [H] the shell swap's re-apply
            else if (c.clip >= 0 && c.clip < 6) meshes.set_clip_now(nm, boss02::kClipNames[c.clip], std::int32_t(c.start));
        }
        for (const Boss02ModelSwap& s : out.swaps) {                    // live -> BBOSS02<s>: a room rebuild
            live_obj.model[boss02::object_name(s.object)] =
                std::string("data\\enemy\\boss02\\bboss02") + boss02::model_suffix(s.object) + ".ymo";
            live_obj_dirty = true;
        }
        for (const Boss02Sound& sd : out.sounds) {
            if (sd.kind == Boss02Sound::k3D) sfx_at(sd.id, sd.pos, kSfxAnimVol);
            else if (sd.kind == Boss02Sound::k2D || sd.kind == Boss02Sound::kLoop) audio.play(sd.id, kSfxAnimVol);
            else if (sd.kind == Boss02Sound::kStop) audio.stop_se(sd.id, sd.param);   // command 6; 216 / 217 loop by their record
        }
        for (const Boss02Effect& fe : out.effects) {
            const ef2sim::M4* parent = nullptr;
            switch (fe.attach) {
                case Boss02Effect::kMouth: parent = &boss2_mouth; break;
                case Boss02Effect::kHoop: parent = &boss2_hoop; break;
                case Boss02Effect::kBone02: parent = &boss2_bone02; break;
                default: break;
            }
            ef2sim::SpawnParams sp;
            sp.side = fe.side; sp.power = fe.power; sp.knock = fe.knock; sp.rtype = fe.rtype;
            sp.status = fe.status; sp.cls = fe.cls;
            if (fe.stat == Boss02Effect::kPiece) sp.atk = boss2->object(fe.object).str;
            else if (fe.stat == Boss02Effect::kPieceStr) sp.atk = fe.str;
            Vec3 at = fe.pos;
            const ef2sim::M4 local = ef2sim::translation(fe.attach_off.x, fe.attach_off.y, fe.attach_off.z);
            if (parent) at = ef2sim::transform_coord(fe.attach_off, *parent);
            ef2sim::Effect* fx = spawn_sim_fx(fe.id, at, fe.rot, fe.scale.x, sp, 0, {});
            if (!fx) continue;
            if (parent) {                                   // FUN_00451c20: +0x1d5 = 1, +0x1a0 = &matrix
                fx->local = local;
                fx->attach_parent = parent;
                fx->attach_mode = 1;
            }
            if (fe.slot != Boss02Effect::kNoSlot) { fx->owner = kB2Owner; fx->tag = int(fe.slot); }
        }
        for (Boss02Effect::Slot s : out.kill_slots)
            for (auto& f : fxsim.effects()) if (f->owner == kB2Owner && f->tag == int(s)) f->kill = true;
        if (out.kill_all_effects)                           // every GoEffect of [0x730128] (§8)
            for (auto& f : fxsim.effects()) f->kill = true;
        for (const Boss02Bullet& b : out.bullets) {         // FUN_00593550, BULLETS.md §3.6 / §7.1
            bullet::Spawn s = bullet::block_spawn(b.kind, b.effect, b.pos, b.dir, b.f.data());
            s.owner = 0; s.owner_monster = true;
            if (b.kind == 0x23) s.owner_atk = b.a4;         // +0xa4 = 0x7b91a4 (the kind-0x23 flies as the stand-in mover)
            if (b.kind == 0x24) s.owner_atk = b.proxy_owner ? boss2->stats().proxy_str : 0.0f;
            bullet::Bullet* bp = bullets.spawn(s);
            if (b.kind == 0x24 && bp) {                     // the caster's writes after the spawn (FUN_0051c8c0)
                bp->carrier = b.carrier; bp->v1ac = b.local; bp->t1b8 = b.target; bp->n1d0 = 0;
            }
        }
        for (const auto& [fi, fv] : out.flags) {
            if (fi < 0 || fi >= 0x200) continue;
            if (game.g_flags[fi] != fv) std::printf("  [boss02] t%llu flag %d = %d\n", (unsigned long long)tick_count, fi, fv);
            game.g_flags[fi] = fv;
        }
        if (out.player_pos) feet = {out.player_pos->x, out.player_pos->y - kOriginH, out.player_pos->z};
        if (out.player_iframes) {
            player_iframes = std::max(player_iframes, 9999);
            ailments::clear_timers(ail);                    // FUN_00461120(2) [BYTES 0x518fda] (STATUS_AILMENTS.md §5)
        }
        if (out.award_exp) {                                // [not modelled] the end block 0x76a828 / FUN_00461160
            std::printf("  [boss02] t%llu EXP %.0f (lv %d)\n", (unsigned long long)tick_count, boss2->exp_b0(), boss2->level());
            award_exp(boss2->exp_b0(), boss2->level(), /*boss*/true, false, 1);
        }
        if (out.gauge) {                                    // §10: the pool, 393.8 px, strip 2
            hud.set_boss_gauge(-6, out.gauge->hp, out.gauge->max, out.gauge->len_scale, out.gauge->strip);
            gauge_owner = 0;
        }
        if (out.gauge_hide) hud.hide_boss_gauge();
    };
    auto boss2_tick = [&] {
        MeshObj* hm = meshes.find("boss02_h");
        if (!hm) { boss2.reset(); return; }
        if (boss2 && boss_load[1] != mesh_load_serial) boss2.reset();
        if (!boss2) {
            boss_load[1] = mesh_load_serial;
            Boss02Setup su;                                // EEX record 1 (§2.1, N4)
            if (eex.bosses.size() > 1) {
                const BossStats& bs = eex.bosses[1];
                su.eex_hp = bs.hp; su.eex_str = bs.atk; su.eex_def = bs.def; su.rec4 = bs.unk10; su.level = bs.level;
            }
            // [H] +0xb0 = the GAMEINFO enemy-EXP curve at the boss level, as boss01.
            if (ginfo.ok()) su.exp_b0 = ginfo.curves[kEnemyExp].at(su.level);
            su.character = character;
            su.diff_index = game.g_flags[144];
            su.diff_easy = game.g_flags[143] != 0;
            su.diff_very_easy = game.g_flags[203] != 0;
            su.time_attack = game.g_flags[193] != 0;
            su.retries = game.g_flags[196];
            su.pos = hm->pos;
            boss2 = std::make_unique<Boss02>(su);
            boss2_banks.fill(RehitBank{});
            boss2_part_banks.fill(RehitBank{});
            // The 35 pieces the ctor creates (§2.1), at their pre-rolled positions; one rebuild.
            for (int o = 1; o < boss02::kObjects; ++o) {
                ObjLocateArgs a;
                a.ymo = std::string("data\\enemy\\boss02\\boss02") + boss02::model_suffix(o) + ".ymo";
                a.name = boss02::object_name(o);
                const Vec3 p = boss2->object(o).pos();
                a.x = p.x; a.y = p.y; a.z = p.z;
                live_obj.located.push_back(a);
            }
            live_obj_dirty = true;
            if (!boss2_mdl_ok) {
                try {
                    const char* files[3] = {"BOSS02.YMO", "BOSS02B.YMO", "BOSS02C.YMO"};
                    for (int k = 0; k < 3; ++k) {
                        boss2_mdl[std::size_t(k)] = Model::parse(read_bytes(asset_root / "ENEMY" / "BOSS02" / files[k]));
                        boss2_mdl[std::size_t(k)].resolve_hierarchy();
                    }
                    auto node = [&](int k, const char* nm) {
                        const Model& md = boss2_mdl[std::size_t(k)];
                        for (std::size_t i = 0; i < md.nodes.size(); ++i) if (md.nodes[i].name == nm) return int(i);
                        return -1;
                    };
                    boss2_entry_node[1] = {node(1, "Bone01"), node(1, "Bone01(5)")};
                    boss2_entry_node[2] = {node(2, "l_reg00"), node(2, "r_reg00")};
                    boss2_bone02_node = node(0, "Bone02");
                    boss2_mdl_ok = true;
                } catch (const std::exception& e) {
                    std::printf("  [boss02] model parse failed: %s\n", e.what());
                }
            }
            std::printf("  [boss02] Nygtilger: pool %.0f (head %.0f, segments %.0f) lv %d\n",
                        boss2->pool(), boss2->object(0).max_hp, boss2->object(1).max_hp, boss2->level());
        }
        for (RehitBank& b : boss2_banks) b.tick();
        for (RehitBank& b : boss2_part_banks) b.tick();

        Boss02Input in;
        in.player_pos = {feet.x, feet.y + kOriginH, feet.z};
        in.player_hp = float(player.hp);
        in.fight = game.g_flags[156] != 0;
        in.wake = game.g_flags[166];
        in.stir = game.g_flags[167] != 0;
        in.flag221 = game.g_flags[221];
        in.rush_select = game.g_flags[165];
        in.rush_room = room.name == "S_9113";
        in.fx_option = settings().s.game.bloody_effect_level > 0;   // option byte 0x6d5d92
        for (int o = 0; o < boss02::kObjects; ++o) {
            const MeshObj* m = meshes.find(boss02::object_name(o));
            in.clip_done[std::size_t(o)] = m && m->count != 0;
        }
        for (int o = 1; o < boss02::kPieces; ++o) {         // the entry bones, posed (before the brain)
            const int role = o == boss02::kPieces - 1 ? 2 : 1;
            for (int k = 0; k < 2; ++k)
                in.entry_bone_y[std::size_t(o)][std::size_t(k)] =
                    boss2_node_world(o, boss2_entry_node[std::size_t(role)][std::size_t(k)]).m[13];
        }
        std::vector<bullet::Bullet*> in_bullets;           // DAT_00730248 order
        for (auto& bp : bullets.list()) {
            if (bp->gone || bp->destroyed) continue;
            if (bp->s.kind != 0x23 && bp->s.kind != 0x24) continue;
            // The tail reads the MATRIX row +0x50..+0x58 [EXPORT 00518ae0.c:864-866]: for a plasma that is where
            // its handler put it (its +0x12c stays at the spawn point, BULLET_KINDS.md §5).
            const Vec3 at = bp->s.kind == 0x24 ? Vec3{bp->mat.m[3][0], bp->mat.m[3][1], bp->mat.m[3][2]} : bp->s.pos;
            in.bullets.push_back({bp->s.kind, bp->n1d0, at});
            in_bullets.push_back(bp.get());
        }
        in.rng = [&] { return pick_rng.next(); };
        // The screen debris (FUN_0058a5f0 at 0x51ab12: 3 splats 0xffc000c0, size 2.6..3.6, after the brain's own two
        // jitter draws), called in place. [not modelled] kDebris (FUN_0051b5a0 -> FUN_00587010: its draws are not taken).
        in.cosmetic = [&](const Boss02Cosmetic& c) {
            if (c.kind == Boss02Cosmetic::kScreenDebris)
                blood_at("boss02", 3, 0xffc000c0u, 2, 0x18, 2.6f, 3.6f, c.pos, 0.65f, false, false);
        };
        const Boss02Output out = boss2->tick(in);
        // The attach frames this tick (before the requests that attach to them).
        boss2_mouth = to_m4(boss2->mouth_frame());
        boss2_hoop = to_m4(boss2->hoop_frame());
        boss2_bone02 = to_m4(boss2_node_world(0, boss2_bone02_node));
        // The tail's repulsion pass (§7.3) on the markers' T and A.
        for (const Boss02BulletNudge& nd : out.nudges) {
            if (nd.index < 0 || std::size_t(nd.index) >= in_bullets.size()) continue;
            if (bullet::Bullet* pb = in_bullets[std::size_t(nd.index)]; pb->s.kind == 0x24) {
                pb->t1b8 = {pb->t1b8.x + nd.delta.x, pb->t1b8.y + nd.delta.y, pb->t1b8.z + nd.delta.z};
                pb->a1c4 = {pb->a1c4.x + nd.delta.x, pb->a1c4.y + nd.delta.y, pb->a1c4.z + nd.delta.z};
            }
        }
        boss2_apply(out);
        // The pieces: placement, fade, collision.
        for (int o = 0; o < boss02::kObjects; ++o) {
            MeshObj* m = meshes.find(boss02::object_name(o));
            if (!m) continue;
            const Boss02Piece& p = boss2->object(o);
            // [STAND-IN] as Mesh_SetAlphaTarget: alpha 0 collapses the object, anything else is opaque.
            const float alpha = (!p.exists || p.hidden) ? 0.0f : p.alpha;
            // The collision: gone with +0x298 & 0x40 / hidden, and a segment's one-way entries
            // (+0x8c, §5.4) [approx] per object: dropped only while both entries are off.
            const bool no_coll = alpha <= 0.0f || (p.f298 & 0x40u) != 0 ||
                                 ((p.role == boss02::kBody || p.role == boss02::kTail) && p.entry_off[0] && p.entry_off[1]);
            const bool moved = !m->has_world || m->world.m != p.world.m;
            if (moved || m->alpha != alpha || m->no_collide != no_coll) m->dirty_pose = true;
            m->has_world = true;
            m->world = p.world;
            m->pos = p.pos();
            m->alpha = m->alpha_target = alpha;
            m->alpha_step = 0.0f;
            m->no_collide = no_coll;
            m->frozen = p.frozen;                          // obj+0x28f: no clip advance while dormant
        }
        // (The kind-0x24 plasma balls move in bullet.cpp now: FUN_0059fa30, with boss2_carrier.)
        // The poison balls die once flag 221 is set ([0x76bc90], BULLETS.md §3.6).
        if (game.g_flags[221] != 0)
            for (auto& bp : bullets.list())
                if (bp->s.kind == 0x23 && !bp->gone && !bp->destroyed) {
                    if (bp->fx) bp->fx->kill = true;
                    bp->gone = true;
                }
        static const bool boss_log = std::getenv("CLERIA_BOSSLOG") != nullptr;
        if (boss_log && tick_count % 30 == 0) {
            const Boss02Piece& h = boss2->object(0);
            std::printf("  [boss02] t%llu mode %d phase %d pool %.0f/%.0f head (%.2f,%.2f,%.2f) hp %.0f def %.0f frozen %d awake %d f156 %d f166 %d f221 %d feet (%.2f,%.2f,%.2f)\n",
                        (unsigned long long)tick_count, boss2->mode(), boss2->phase(), boss2->pool(), boss2->pool_max(),
                        h.pos().x, h.pos().y, h.pos().z, h.hp, h.def, int(h.frozen), int(boss2->group().awake),
                        game.g_flags[156], game.g_flags[166], game.g_flags[221], feet.x, feet.y, feet.z);
        }
    };

    // ---- Gelaldy, boss03 (src/cleria/boss03, build/boss_re/GELALDY.md §12 / §16) -----------
    // The GoMesh "boss03" the room script Mesh_Locate's (one model, a yaw-only matrix) is driven
    // by the Boss03 brain every live step, after the clip advance (the mesh pool runs under a Fix
    // too). Its five GoXx parts are the sword targets / the BEAT arm cylinders; the ring floor is
    // the scene objects ring00..35 (their y, render + collision) and the markers atari00..35.
    // The camera writes go into the region camera's current slot, the shake into the rig's
    // shake globals (its step IS FUN_00427c90: 3 shared-RNG draws per frame while it runs).
    // [not modelled] the node overrides (Neji / ForeArm2 / Neck4 / the Head bug: the baked model
    // has no per-node override, so a broken forearm is not thrown away visibly),
    // the SE stop / fade, the buff clear and the
    // achievement, the drop (obj+0xc4), the element bonus on the arm HP (dmg_hp = dmg), the
    // lava floor (surface 13, §6.5), the S_3099_1.SOB ring set on Hard / Nightmare (FLOOR3 B8:
    // the root script's Scene_LoadStaticObjects pick is not honoured by the room load).
    constexpr int kB3Owner = -0xB03;                       // fx owner tag for the core slot
    std::array<int, boss03::kNodes> boss3_node{};          // kNodeNames -> the model's node index
    std::array<ef2sim::M4, boss03::kNodes> boss3_node_m{}; // the posed node frames (attach parents)
    std::array<RehitBank, boss03::kParts> boss3_banks{};   // the parts' own re-hit banks (+0x14c)
    std::array<int, boss03::kClips> boss3_sect{};          // clip id -> the mesh's section index
    auto boss3_apply = [&](const Boss03Output& out, MeshObj* bm) {
        for (int id : out.rumble) pad_rumble(id);        // FUN_005d3500 (view_pad.inl)
        for (const Boss03ClipSet& c : out.clips) {             // FUN_005f5c80 (+ the death's hard re-set)
            if (c.clip < 0 || c.clip >= boss03::kClips || boss3_sect[std::size_t(c.clip)] < 0) continue;
            meshes.set_clip_index("boss03", boss3_sect[std::size_t(c.clip)], 0);
            if (c.hard && c.frame) { bm->time = *c.frame; bm->count = 0; }
        }
        if (out.anim_speed) bm->time_scale = *out.anim_speed;
        for (const Boss03Sound& sd : out.sounds) {
            if (sd.kind == Boss03Sound::k3D) sfx_at(sd.id, sd.pos, kSfxAnimVol);
            else if (sd.kind == Boss03Sound::k2D) audio.play(sd.id, kSfxAnimVol);
            else audio.stop_se(sd.id, sd.param);                 // kStop / kFade: both audio command 6 (FUN_005d8970)
        }
        // The effects, with the kills interleaved where the brain issued them.
        std::size_t ki = 0;
        auto kills_upto = [&](std::size_t n) {
            for (; ki < out.kills.size() && out.kills[ki].effects_before <= n; ++ki) {
                const Boss03Kill& k = out.kills[ki];
                for (auto& f : fxsim.effects()) {
                    if (k.what == Boss03Kill::kId ? f->id == k.id : (f->owner == kB3Owner && f->tag == 1)) f->kill = true;
                }
            }
        };
        for (std::size_t i = 0; i < out.effects.size(); ++i) {
            kills_upto(i);
            const Boss03Effect& fe = out.effects[i];
            const Ef2* ef = ef2_of(fe.id);
            if (!ef) continue;
            ef2sim::SpawnParams sp;
            sp.pos = fe.pos; sp.rot = fe.rot; sp.scale = fe.scale;
            sp.power = fe.power; sp.knock = fe.knock; sp.rtype = fe.rtype;
            sp.atk = fe.stat == Boss03Effect::kBoss ? boss3->str() : 0.0f;
            ef2sim::Effect* fx = fxsim.spawn(fe.id, *ef, sp);
            if (!fx) continue;
            if (fe.node >= 0 && fe.node < boss03::kNodes) {
                // [approx] eff+0x1a0 = the node record, +0x1d5 = 1: the effect follows the node's
                // translation and keeps its own rotation (the node record's consumer is not traced).
                fx->local = fx->M;
                fx->local.m[3][0] = fx->local.m[3][1] = fx->local.m[3][2] = 0.0f;
                fx->attach_parent = &boss3_node_m[std::size_t(fe.node)];
                fx->attach_mode = 2;
            }
            if (fe.slot == Boss03Effect::kCore) { fx->owner = kB3Owner; fx->tag = 1; }
        }
        kills_upto(out.effects.size());
        for (const Boss03Spawn& s : out.spawns) {          // FUN_00475710 + the brain's writes (§7)
            SpawnRequest req;
            req.cast_type = s.cast;
            req.pos = s.pos;
            req.yaw_degrees = float(s.yaw_deg);
            Actor* a = world.spawn(req);
            if (!a) continue;
            a->height = cast_motion(casts, s.cast).height();
            a->radius = cast_motion(casts, s.cast).radius();
            a->level = s.level;
            a->vert.t += s.d330;                           // +0x330 -= 0.1: it falls out of the mouth
            a->vert.grounded = false;
            a->flags130 |= s.or130;
            a->flags12c |= s.or12c;
            const float fl = floor_at(phys_mesh(room), s.pos.x, s.pos.z, s.pos.y);
            if (fl > kNoFloor) a->floor_y = fl;
            csHost.world_changed = true;
            std::printf("  [boss03] t%llu Perrie spawned at (%.2f,%.2f,%.2f) yaw %d\n", (unsigned long long)tick_count,
                        s.pos.x, s.pos.y, s.pos.z, s.yaw_deg);
        }
        if (out.kill_perries)
            for (Actor& a : world.actors()) if (a.cast_id == 0x3a) a.hp = 0.0f;
        for (const Boss03RingMove& r : out.rings)           // ringNN +0x54 (render and collision follow)
            if (MeshObj* m = meshes.find(boss03::ring_name(r.ring)))   // (the endless float32 rise, G13: only on a change)
                if (m->pos.y != r.y) { m->pos.y = r.y; m->dirty_pos = true; }
        for (int k : out.atari_lowered)                     // atariNN +0x54 = -1.8
            if (MeshObj* m = meshes.find(boss03::atari_name(k))) { m->pos.y = -1.8f; m->dirty_pos = true; }
        if (out.shake.frames) g_rig.set_shake_frames(*out.shake.frames);
        if (out.shake.amp) g_rig.set_shake_amp(*out.shake.amp);
        if (out.zoom.strength || out.zoom.rate)            // ZoomBlur (0x76a550 / 0x76a554)
            zoom_blur(out.zoom.strength.value_or(zb.s), out.zoom.rate.value_or(zb.r));
        if (out.zoom.snapshot) fx_snapshot = true;         // 0x7b2488 = 1
        if (out.flash) frame_hold(out.flash->first, out.flash->second);   // the death's frame hold (0x76a548 / 0x76a54c)
        if (out.time_scale) g_time_scale = *out.time_scale;   // 0x6d6078
        {
            const Boss03Camera& c = out.camera;             // the current slot (BOSS_CAMERA.md §1b)
            CameraRegion& r = g_rig.edit_slot(g_rig.current());
            if (c.pitch) r.eye.pitch = *c.pitch;            // +0x270
            if (c.up) r.offset.y = *c.up;                   // +0x410
            if (c.dist) r.eye.dist = *c.dist;               // +0x274
            if (c.target) r.target.fixed_pos = *c.target;   // +0xb4
            if (c.target_mode) r.target.mode = std::uint32_t(*c.target_mode);   // +0x94
            if (c.eye) r.eye.fixed_pos = *c.eye;            // +0x288
            if (c.eye_mode) r.eye.mode = std::uint32_t(*c.eye_mode);            // +0x268
            if (c.eye_flags) r.eye.flags = std::uint32_t(*c.eye_flags);         // +0x278
        }
        for (const auto& [fi, fv] : out.flags) {
            if (fi < 0 || fi >= 0x200) continue;
            if (game.g_flags[fi] != fv) std::printf("  [boss03] t%llu flag %d = %d\n", (unsigned long long)tick_count, fi, fv);
            game.g_flags[fi] = fv;
        }
        if (out.player_iframes) player_iframes = std::max(player_iframes, 9999);   // +0x734 = 9999
        feet.x += out.player_push.x;                       // the head push-out (§9): +0x50 ...
        feet.z += out.player_push.z;
        player_kb.x += out.player_push_vel.x;              // ... and +0x338..0x340
        player_kb.z += out.player_push_vel.z;
        if (out.exp && out.exp->award) {                   // [H] b0 = the GAMEINFO curve at the level, as boss01/02
            std::printf("  [boss03] t%llu EXP %.0f x %.2f (lv %d)\n", (unsigned long long)tick_count, out.exp->b0,
                        out.exp->mult, out.exp->level);
            // [H] the SP of Gelaldy's own award FUN_005215e0 (obj+0xc4) taken as its EEX record's, like the others.
            award_exp(out.exp->b0, out.exp->level, /*boss*/true, false, 2);   // diffMult (out.exp->mult) applied inside
        }
        if (out.destroy) {                                  // obj+0x11 |= 2: gone, its collision too
            bm->alpha = bm->alpha_target = 0.0f;
            bm->no_collide = true;
            bm->dirty_pose = true;
            std::printf("  [boss03] t%llu destroyed\n", (unsigned long long)tick_count);
        }
        if (out.gauge) {                                    // §10: the body HP, 481 px, emblem 3
            hud.set_boss_gauge(-7, out.gauge->hp, out.gauge->max, out.gauge->len_scale, out.gauge->emblem);
            gauge_owner = 0;
        }
        if (out.gauge_hide) hud.hide_boss_gauge();
    };
    auto boss3_tick = [&] {
        MeshObj* bm = meshes.find("boss03");
        if (!bm) { boss3.reset(); return; }
        Model* mdl = cached_big_model(resolve_ymo_path(asset_root, "data\\enemy\\boss03\\boss03.ymo"));
        if (!mdl) return;
        auto lw = [](std::string t) { for (char& ch : t) ch = char(std::tolower((unsigned char)ch)); return t; };
        if (boss3 && boss_load[2] != mesh_load_serial) boss3.reset();
        if (!boss3) {
            boss_load[2] = mesh_load_serial;
            Boss03Setup su;                                // EEX record 2 (§2.1)
            if (eex.bosses.size() > 2) {
                const BossStats& bs = eex.bosses[2];
                su.eex_hp = bs.hp; su.eex_str = bs.atk; su.eex_def = bs.def; su.rec4 = bs.unk10; su.level = bs.level;
            }                                              // rec3 (+0x0c) is not in EexTable: the default
            if (ginfo.ok()) su.exp_b0 = ginfo.curves[kEnemyExp].at(su.level);   // [H] as boss01/02
            su.character = character;
            su.diff_index = game.g_flags[144];
            su.diff_easy = game.g_flags[143] != 0;
            su.diff_very_easy = game.g_flags[203] != 0;
            su.boss_rush = game.g_flags[193] != 0;
            su.flag201 = game.g_flags[201] == 1;
            su.retries = game.g_flags[196];
            su.pos = bm->pos;
            su.player_pos = {feet.x, feet.y + kOriginH, feet.z};
            for (int k = 0; k < boss03::kRings; ++k) {      // the markers of the SOB this room loaded
                const MeshObj* m = meshes.find(boss03::atari_name(k));
                su.atari_present[std::size_t(k)] = m != nullptr;
                su.atari[std::size_t(k)] = m ? m->pos : Vec3{};
            }
            if (const MeshObj* r0 = meshes.find(boss03::ring_name(0))) su.ring_y0 = r0->pos.y;
            boss3 = std::make_unique<Boss03>(su);
            boss3_banks.fill(RehitBank{});
            hud.gauge[0] = WorldGauge{};
            boss3->bind_gauge(&hud.gauge[0]);              // the arm mini gauge 0x76c1cc..
            for (int k = 0; k < boss03::kNodes; ++k) {
                boss3_node[std::size_t(k)] = -1;
                for (std::size_t n = 0; n < mdl->nodes.size(); ++n)
                    if (mdl->nodes[n].name == boss03::kNodeNames[k]) { boss3_node[std::size_t(k)] = int(n); break; }
                if (boss3_node[std::size_t(k)] < 0) std::printf("  [boss03] no node %s\n", boss03::kNodeNames[k]);
            }
            for (int c = 0; c < boss03::kClips; ++c) {      // the clip id IS the YMO section (§2.5); by name
                boss3_sect[std::size_t(c)] = -1;
                for (std::size_t s = 0; s < bm->clips.size(); ++s)
                    if (lw(bm->clips[s].name) == lw(boss03::kClipTable[c].name)) { boss3_sect[std::size_t(c)] = int(s); break; }
                if (boss3_sect[std::size_t(c)] != c)
                    std::printf("  [boss03] clip %s -> section %d\n", boss03::kClipTable[c].name, boss3_sect[std::size_t(c)]);
            }
            std::printf("  [boss03] Gelaldy: body %.0f, arms %.0f, STR %.0f DEF %.0f lv %d, %d atari, ring y %.2f\n",
                        boss3->hp(), boss3->part(1).max_hp, boss3->str(), boss3->def(), boss3->level(),
                        int(std::count(su.atari_present.begin(), su.atari_present.end(), true)), su.ring_y0);
            boss3_apply(boss3->ctor_requests(), bm);
        }
        for (RehitBank& b : boss3_banks) b.tick();
        // The posed model at this step's clip frame (FUN_005f6000), placed by the boss matrix.
        int clip = boss03::kAll;
        for (int c = 0; c < boss03::kClips; ++c) if (boss3_sect[std::size_t(c)] == bm->sect) { clip = c; break; }
        bool posed = false;
        if (bm->sect >= 0 && std::size_t(bm->sect) < bm->clips.size())
            for (const AnimClip& c : mdl->clips)
                if (c.name == bm->clips[std::size_t(bm->sect)].name) { mdl->pose(c, bm->time); posed = true; break; }
        if (!posed) mdl->rest_pose();
        const Mat4 place = boss3->matrix();
        Boss03Input in;
        for (int k = 0; k < boss03::kNodes; ++k) {
            const int ni = boss3_node[std::size_t(k)];
            const Mat4 w = (ni >= 0 && std::size_t(ni) < mdl->nodes.size()) ? mdl->nodes[std::size_t(ni)].world * place : place;
            in.nodes[std::size_t(k)] = {w.m[12], w.m[13], w.m[14]};
            boss3_node_m[std::size_t(k)] = to_m4(w);
        }
        in.player_pos = {feet.x, feet.y + kOriginH, feet.z};
        in.player_hp = float(player.hp);
        in.fight = game.g_flags[156] == 1;
        in.clip = clip;
        in.clip_done = bm->count != 0;
        in.clip_frame = bm->time;
        for (const Actor& a : world.actors()) if (a.cast_id == 0x3a && a.alive()) ++in.perrie_count;
        {
            const CameraRegion& r = g_rig.edit_slot(g_rig.current());
            in.cam_pitch = r.eye.pitch; in.cam_up = r.offset.y; in.cam_dist = r.eye.dist;
        }
        in.shake_amp = g_rig.shake_amp();
        in.rng = [&] { return pick_rng.next(); };
        const Boss03Output out = boss3->tick(in);
        boss3_apply(out, bm);
        // The placement (obj+0x20: RotY(yaw) + the position) and the fade.
        const Mat4 wm = boss3->matrix();
        if (!bm->has_world || bm->world.m != wm.m) bm->dirty_pose = true;
        bm->has_world = true;
        bm->world = wm;
        bm->pos = boss3->pos();
        static const bool boss_log = std::getenv("CLERIA_BOSSLOG") != nullptr;
        if (boss_log && tick_count % 30 == 0) {
            int sunk = 0;
            for (int k = 0; k < boss03::kRings; ++k) sunk += boss3->ring_state(k) == 2 || boss3->ring_state(k) == 3;
            std::printf("  [boss03] t%llu phase %d special %d idx %d code %d sub %d clip %s fr %.1f done %d | body %.0f core %.0f R %.0f L %.0f"
                        " guard %d/%d/%d alive %d/%d | rings sunk %d perries %d f156 %d feet (%.2f,%.2f,%.2f) php %d\n",
                        (unsigned long long)tick_count, boss3->phase(), boss3->special(), boss3->seq_index(), boss3->code(),
                        boss3->sub(), boss03::kClipTable[clip].name, bm->time, int(in.clip_done), boss3->hp(),
                        boss3->part(0).hp, boss3->part(1).hp, boss3->part(3).hp, int(boss3->core_guard()),
                        int(boss3->arm_guard(0)), int(boss3->arm_guard(1)), int(boss3->arm_alive(0)),
                        int(boss3->arm_alive(1)), sunk, in.perrie_count, game.g_flags[156], feet.x, feet.y, feet.z,
                        player.hp);
            const CameraRegion& r = g_rig.edit_slot(g_rig.current());
            std::printf("  [boss03]   cam slot %d eye mode %u yaw %.3f pitch %.3f dist %.2f flags %u | target mode %u | off (%.2f,%.2f,%.2f)"
                        " | eye (%.2f,%.2f,%.2f) -> (%.2f,%.2f,%.2f)\n", g_rig.current(), r.eye.mode, r.eye.yaw, r.eye.pitch,
                        r.eye.dist, r.eye.flags, r.target.mode, r.offset.x, r.offset.y, r.offset.z, g_cam_out.eye.x,
                        g_cam_out.eye.y, g_cam_out.eye.z, g_cam_out.target.x, g_cam_out.target.y, g_cam_out.target.z);
        }
    };
    // FUN_0042dc90: a point inside one of the room's exit volumes — the .SEN records with a destination; the
    // .SEV event boxes are another list here, as the engine skips them (KEEPOUT_BOXES.md §1, §2).
    const bool keepout_log = std::getenv("CLERIA_KEEPOUT") != nullptr;
    const bool spec_log = std::getenv("CLERIA_SPEC") != nullptr;   // the enemies' specular tint (anime_ChangeSpecular)
    const std::function<bool(const Vec3&)> exit_volume = [&](const Vec3& p) {
        for (const SenTrigger& t : room.sen.triggers) if (t.contains(p)) return true;
        return false;
    };
    #include "view_boss04.inl"                // Khonsclard's host (KHONSCLARD.md)
    #include "view_boss05.inl"                // Pictimos's host (PICTIMOS.md)
    #include "view_boss06.inl"                // Zava's heads and her link (ZAVA*.md)
    #include "view_boss07.inl"                // Dalles's host (DALLES*.md)
    #include "view_boss08.inl"                // Darm's host (DARM*.md)

    // ---- the player's death and the game over (build/gameflow_re/GAMEFLOW.md §3.4) ---------
    // FUN_0046fba0 (and FUN_0046f810) on HP < 1 without flag 133: the first dead step sets
    // +0x3fc = 1 and zeroes the motion fields, the rest count it up, and at 100 FUN_00427820 runs:
    // gameover.dds as a held type-8 card at (0, 192) (FUN_005a15d0), a two-item menu "Retry" /
    // "Return to Title" at (512, 460.8) (FUN_00578440(&pos, " ", 0) + FUN_00578680 + FUN_00578710),
    // armed at once (+0x1fd1 = 1), and Bgm_Play(36); time attack (flag 193) skips the card and the
    // BGM. Retry (LAB_004276b0) = ChangeScene(the current scene, label "continue"): FUN_00431260
    // restores the continue snapshot, counts flag 196 up and arrives at the snapshot's pose.
    // Return to Title (LAB_00427750) leaves for the title (FUN_0042f510).
    // [not modelled] the fader pair 0x769f24 = 1/3, 0x769f28 = 0.01; Hugo's scene-0x93 achievement;
    // the death count's own gate 0x74ab8f (the menu) and the actor fields beyond the motion.
    int death_t = 0;                                     // +0x3fc
    // cleria.game.save(slot) (LUA_API.md §4; not retail): the statue save's two steps (FUN_0042e9a0: the continue
    // snapshot, then the slot and its sidecar) when the game can be saved. [MOD] the retail Archipelago mod's
    // autosave gate: 185 == 1 (the player has the controls), 156 == 0 (no boss fight), 190 == 0 (no arena),
    // a second in the room. [H] the rest is this port's: a session that started, no Fix, alive, no staged
    // door or ChangeScene.
    g_lua.b.save = [&](int slot) {
        if (game.g_flags[185] != 1 || game.g_flags[156] != 0 || game.g_flags[190] != 0 || g_content.room_age < 60) return false;
        if (content_session_pending() || csMgr.world_frozen() || player.hp < 1 || death_t > 0) return false;
        if (door_pending || door_commit || door_latched || !csHost.pending_scene.empty()) return false;
        save_cont();
        return do_save(slot);
    };
    CaptionCard gameover_card;
    WndDialog gameover_menu;
    bool return_to_title = false;
    auto game_over = [&] {                               // FUN_00427820
        std::printf("  [gameover] gameover.dds + Retry / Return to Title\n");
        const bool time_attack = game.g_flags[193] != 0;
        if (!time_attack) gameover_card.open("gameover", /*hold*/true, /*alt*/true);
        gameover_menu.menu(512.0f, 460.8f, " ", {"Retry", "Return to Title"}, {"retry", "title"},
                           bookR.metrics(), /*centre_y*/false, /*tick*/false);
        gameover_menu.arm();
        if (!time_attack) csHost.bgm_play_fn(36);
    };
    auto retry = [&] {                                   // LAB_004276b0 -> FUN_00431260 "continue"
        gameover_card = CaptionCard{};
        gameover_menu = WndDialog{};
        death_t = 0;
        if (!cont.valid) {
            // [STAND-IN] no snapshot yet (the engine's 0x767f50 would hold whatever was there):
            // revive in place at the room's spawn.
            std::printf("  [gameover] Retry: no continue snapshot, respawn\n");
            player.hp = player.max_hp;
            spawn_player();
            hud.hide_boss_gauge(); gauge_owner = 0;      // as the scene load does (transition)
            scene_entry_fader();                         // Retry is a scene load: it fades in (#51)
            return;
        }
        content_retry(cont.sc);                          // mods: the sidecar rolls back with the flags, then Load
        apply_loaded(cont.s, /*force_scene*/true);
        game.g_flags[196] += 1;                          // DAT_0076bc2c, the retry count
        std::printf("  [gameover] Retry -> %s (%.2f, %.2f, %.2f), retries %d\n", cont.s.scene.c_str(),
                    cont.feet.x, cont.feet.y, cont.feet.z, game.g_flags[196]);
        feet = cont.feet;                                // the arrival 0x76a3a0..a8 = the snapshot pose
        feet.y = placed_floor(feet);                     // placed by FUN_00434020 -> FUN_00459ff0
        pv = VerticalState{}; pvel = {}; last_floor = feet.y;
        facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
        csHost.fade_rgb = 0; csHost.fade_cur = 1.0f; csHost.fade_tgt = 0.0f; csHost.fade_rate = 0.05f;
    };
    // Every scene entry fades in from black over 20 steps (FUN_00431840 -> FUN_00439170,
    // SCENEFLOW.md 1.5), the first room after the title too. Before the room's first script tick,
    // so a root's own Fader_Target (S_0000's) still wins. [dev] a command-line room starts unfaded.
    if (show_title) { csHost.fade_rgb = 0; csHost.fade_cur = 1.0f; csHost.fade_tgt = 0.0f; csHost.fade_rate = 0.05f; }
    g_tick_reset = true;   // the first room's load stall is not caught up either (DAT_0075aed5)
    // ---- The shell's Debug page: the live game's state (added once the game loop's state exists). ----
    {
        std::vector<std::string> rooms;
        for (const auto& [k, v] : g.sob_index) if (k.rfind("S_", 0) == 0 && k.size() == 6) rooms.push_back(k);
        std::sort(rooms.begin(), rooms.end());
        shell.panel.add("Debug", [&, rooms](ui::List& L) {
            static int warp_i = 0, flag_i = 0;
            static float flag_f = 0;
            char buf[160];
            ui::section(L, "Developer");
            if (ui::toggle(L, "Debug keys", &settings().s.cleria.dev_keys,
                           "CleriaCore's fixed test keys during play: F free-fly, R respawn, T test cutscene, K -60 HP, "
                           "L full heal, B +Boost, Ctrl+Tab character, Ctrl+1-9 take an exit. Off: those keys do "
                           "nothing (they can be your own bindings). Not part of the original game."))
                persist();
            ui::section(L, "Player");
            std::snprintf(buf, sizeof buf, "%s  (%s)", room.name.c_str(), kCharNames[character]);
            ui::info(L, "Room", buf);
            std::snprintf(buf, sizeof buf, "%.2f  %.2f  %.2f", feet.x, feet.y, feet.z);
            ui::info(L, "Position", buf);
            std::snprintf(buf, sizeof buf, "HP %d / %d    MP %.0f / %.0f    Lv %d", player.hp, player.max_hp,
                          player.sp_cur, player.sp_max, player.level);
            ui::info(L, "Stats", buf);
            if (ui::button(L, "Heal", "Refill HP and MP.")) { player.hp = player.max_hp; player.sp_cur = player.sp_max; }
            if (ui::button(L, fly ? "Walk" : "Fly", "Toggle the free-fly camera (F).")) {
                fly = !fly;
                if (!fly) {
                    feet = {cam.pos.x, cam.pos.y, cam.pos.z};
                    const float fl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + room_diag(room));
                    if (fl > kNoFloor) feet.y = fl;
                    pv = VerticalState{}; pvel = {}; last_floor = feet.y;
                }
            }
            if (ui::button(L, "Respawn", "Put the player back at the room's spawn point (R).")) spawn_player();
            ui::section(L, "Warp");
            if (!rooms.empty()) {
                std::vector<const char*> items;
                for (const std::string& r : rooms) items.push_back(r.c_str());
                warp_i = std::clamp(warp_i, 0, int(items.size()) - 1);
                ui::choice(L, "Room", &warp_i, items.data(), int(items.size()),
                           "Pick a room with left / right, then Warp.");
                if (ui::button(L, "Warp", "Enter the chosen room at its default spawn.")) {
                    csHost.pending_scene = rooms[warp_i];
                    shell.open = false;
                }
            }
            ui::section(L, "Flags");
            float fi = float(flag_i);
            if (ui::slider(L, "Flag", &fi, 0.0f, 511.0f, 1.0f, "%.0f", "The game flag to inspect (g_flags).")) {
                flag_i = int(fi);
                flag_f = float(game.g_flags[flag_i]);
            }
            std::snprintf(buf, sizeof buf, "%d", game.g_flags[flag_i]);
            ui::info(L, "Current value", buf);
            ui::slider(L, "New value", &flag_f, -1.0f, 9999.0f, 1.0f, "%.0f", "Left / right to change, then Set.");
            if (ui::button(L, "Set", "Write the new value into the flag.")) game.g_flags[flag_i] = int(flag_f);
        });
    }
    while (running) {
        if (!csHost.pending_scene.empty()) {
            const std::string req = csHost.pending_scene;
            csHost.pending_scene.clear();
            csMgr.paused = false;
            // A demo scene: its movie runs to its end (or a skip) and the fade to black first.
            if (const int demo = demo_number(req); demo && !play_demo(demo)) { running = false; continue; }
            // ChangeScene('Title\'): the scene name "title\" (0x698a1c, read by FUN_00431840 / FUN_00442260
            // [EXPORT strings.tsv]) is the title itself. S_0003's Clear_Event_1/2 end the game with it
            // (ROUTE_F47.md B3). [approx] by the book's Return to Title route.
            {   std::string lr = req;
                for (char& c : lr) c = (c == '/') ? '\\' : char(std::tolower((unsigned char)c));
                while (!lr.empty() && lr.back() == '\\') lr.pop_back();
                if (lr == "title") {
                    std::printf("  [scene] ChangeScene '%s' -> the title\n", req.c_str());
                    audio.fade_out_bgm(0xbb8);
                    cur_bgm = 0;
                    return_to_title = true; running = false; accum = 0;
                    continue;
                }
            }
            const std::string dest = scene_room(req);
            std::printf("  [scene] ChangeScene '%s' -> %s\n", req.c_str(), dest.empty() ? "(none)" : dest.c_str());
            if (!dest.empty()) {
                transition(dest);                                // SCT default arrival, no label
                // (and fades in from black over 20 steps: transition's scene-entry fader reset)
            }
        }
        if (door_commit) {
            // A committed door (0x74c120 bit 1): FUN_00442dd0 tears the room down at this frame and
            // FUN_00431260 loads the destination with the staged descriptor -- the crossed volume's own
            // arrival and facing (copied to 0x76a3a0 by the commit). The scene entry FUN_00431840 ->
            // FUN_00439170 then fades in from black: current 1.0, target 0, 0.05 a step (20 steps) --
            // transition()'s scene-entry fader reset.
            const SenTrigger t = *door_commit;
            transition(t.dest_scene, &t);
        }
        const Uint64 now = SDL_GetTicks();
        const float real_dt = float(now - last) / 1000.0f; last = now;
        // The shell's Game speed scales how many fixed ticks run per real second (never the tick's dt).
        accum += real_dt * g_time_scale * std::clamp(settings().s.access.game_speed, access::kSpeedMin, access::kSpeedMax);
        // The exe's own hitch protection (DAT_0075aed5): after a load it forces a
        // single tick with a zero delta rather than catching up on the stall. Room
        // transitions set this; without it a slow load bursts hundreds of ticks.
        if (g_tick_reset) { accum = kTickDt; g_tick_reset = false; }
        // A drive run is LOCKSTEP: one logic tick per rendered frame, so a drive `shot` lands on its own
        // tick and the per-frame work (a ChangeScene / door swap, the room's script start, the shell's
        // page draw) falls between the same ticks however loaded the machine is. Wall-clock pacing let a
        // slow frame run 20 ticks at once (entry_label's `scripts` then landed before the new room's
        // scripts started). CLERIA_DRIVE_REALTIME=1 restores wall-clock pacing for a drive run.
        static const bool lockstep = std::getenv("CLERIA_DRIVE") && !std::getenv("CLERIA_DRIVE_REALTIME");
        if (lockstep) accum = kTickDt;
        lua_frame();                                     // the Lua mods' "frame" handlers (view_lua.inl)

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            g_sdl_in.on_event(e);                 // the mapper's latches and the gamepad (view_input.inl)
            if (shell.event(e)) continue;         // the shell takes what it owns
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode k = e.key.key;
                // The game's own input (move, attack, jump, magic, boost, the book, the windows' OK /
                // Cancel, the cutscene skip) goes through the mapper in the logic tick below. These
                // are CleriaCore's FIXED debug keys, outside the mapper, never while a game-over, a
                // cutscene or the book has the input. Ctrl+Tab / Ctrl+1..9: their plain keys are
                // retail bindings (Preset 2's Camp Menu, the skills). Off unless the Debug tab's
                // "Debug keys" (cleria.ini [cleria.dev] DebugKeys) is on: a player's rebound key never
                // trips them. F3 is the Interface tab's Debug text and always works.
                const bool dev_keys = settings().s.cleria.dev_keys;
                if (dev_keys)
                    speedrun_debug_key(k, e.key.mod, death_t > 0 || csMgr.world_frozen() || paused);   // remembered by a run
                if (death_t > 0 || csMgr.world_frozen() || paused) {}
                else if (k == SDLK_F3) hud.debug_text = !hud.debug_text;   // our debug overlay
                else if (!dev_keys) {}
                // T = trigger a test cutscene (S_1000's Roy/Ramona talk) — debug.
                else if (k == SDLK_T) start_cutscene(asset_root / "MAP" / "S_10" / "S_1000" / "TALKC210.XSO", /*debug235*/true);
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
                else if (k == SDLK_K) { player.hp = std::max(1, player.hp - 60); hud.debug_message("(debug) -60 HP"); }
                else if (k == SDLK_L) { player.hp = player.max_hp; hud.debug_message("(debug) full heal"); }
                // Debug: B fills the EX/Boost gauge in ~4 presses (to eyeball the boost arc).
                else if (k == SDLK_B) { player.boost = std::min(30000.f, player.boost + 7500.f); hud.debug_message("(debug) +Boost"); }
                else if (k == SDLK_TAB && (e.key.mod & SDL_KMOD_CTRL)) {   // cycle the character (debug)
                    character = character % 3 + 1;
                    game.g_flags[150] = character;
                    hud.debug_message(std::string("Character: ") + kCharNames[character]);
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
                else if (k >= SDLK_1 && k <= SDLK_9 && (e.key.mod & SDL_KMOD_CTRL)) {   // warp (debug)
                    std::size_t idx = std::size_t(k - SDLK_1);
                    if (idx < room.sen.triggers.size())
                        transition(room.sen.triggers[idx].dest_scene, &room.sen.triggers[idx]);
                }
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (fly) looking = true;              // fly mode: drag to look (play: the mapper's mouse slots)
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

        sync_settings();
        ptr_sync();
        const bool* ks = SDL_GetKeyboardState(nullptr);   // free-fly's raw keys (debug, outside the mapper)
        const float D = room_diag(room);
        // A newly (re)built room (re)starts its event scripts on the persistent manager.
        if (room_events_dirty) {
            room_events_dirty = false;
            // The placement wrote the arrival / loaded heading to +0x308, +0x314 and +0x360 at
            // once (FUN_00459ff0 from FUN_00434020 0x4340ad..0x4340e8), before the room's
            // scripts run; Chr_Pos then snaps +0x314 to +0x308 (0x570a63). The world player was
            // rebuilt from the SCT (yaw 0) by populate_room, so a cutscene's first Chr_Pos(13)
            // snapped her to heading 90 and turned her back (PLAYTEST_1.md §2).
            if (Actor* wp = world.player(); wp && !fly) {
                const float h = std::atan2(face_dir.z, face_dir.x);
                wp->look.cmd = wp->look.dir = h;
                wp->yaw = yaw_of_heading(h);
            }
            start_room_events();
        }
        // ---- fixed 60 Hz logic, 0..N ticks per rendered frame --------------
        // The catch-up loop in the exe has NO cap; a long stall really does emit a
        // burst of ticks. We reproduce that, relying on the load-time reset above
        // rather than on clamping, so timing stays exact under normal play.
        if (paused) accum = 0;               // freeze while the pause book is open
        if (shell.open) accum = 0;           // and while the shell overlay is open
        if (paused && book.is_open()) {
            bookAccum = lockstep ? kTickDt : bookAccum + real_dt;   // a drive run: one book frame a frame
            while (bookAccum >= kTickDt && book.is_open()) {
                bookAccum -= kTickDt;
                if (!drive.empty()) {                        // CLERIA_DRIVE runs on through the book
                    ++tick_count;
                    for (const DriveCmd& dc : drive) if (dc.tick == tick_count) run_drive(dc.cmd);
                }
                // The input record (FUN_005d2ba0, PAUSEBOOK_DRAW.md §0.7) with the book's 4-way stick:
                // OK 0x2000 / Cancel 0x4000 edges, the auto-repeat steps (1st frame, 28th, every 5th),
                // the held Left / Right bits for the Option sliders. A drive's ok / esc / up / down too.
                const input::Frame& inf = input_tick(/*book_mode*/true);
                {   // the cursor manager: the book is a menu window; the player tick does not run
                    mouse::Context cc;
                    cc.menu = true;
                    cc.event = csMgr.world_frozen() || csHost.wtext.alive();
                    cc.player = !fly;
                    input::Frame rec = inf;
                    cursor_tick(rec, cc);
                }
                BookInput in = bookIn;
                bookIn = {};
                book_mouse(in);
                in.confirm = in.confirm || (inf.pressed & input::kOk);
                in.cancel = in.cancel || (inf.pressed & input::kCancel);
                in.hstep = inf.dx;
                in.vstep = inf.dy;
                if (drive_book_v != 0) { in.vstep = drive_book_v; drive_book_v = 0; }
                if (drive_book_h != 0) { in.hstep = drive_book_h; drive_book_h = 0; }
                in.held_left = (inf.dir & input::kLeft) != 0;
                in.held_right = (inf.dir & input::kRight) != 0;
                BookModel bm = book_model();
                BookEvents bev;
                book.update(in, bm, bev);
                for (int id : bev.sounds) audio.play(id, kSfxMenuVol);
                if (bev.warp) cursor_warp(bev.warp_x, bev.warp_y, bev.warp_mode);
                if (bev.closed) cursor_warp(book_pt.x, book_pt.y, 2);   // the dtors' warp back (mode 2)
                if (bev.closed && !drive.empty())
                    std::printf("  [book] t%llu closed (%s)\n", (unsigned long long)tick_count,
                                (inf.pressed & input::kCancel) ? "cancel" : "other");
                if (bev.clamp_hp) player.hp = std::min(player.hp, int(player_stats().max_hp));
                if (bev.use_item >= 0) {                     // the Panacea (FUN_00443390, count - 1)
                    float hp = float(player.hp);
                    EffectEnv env;
                    env.flags = game.g_flags; env.hp = &hp; env.max_hp = player_stats().max_hp;
                    const UseResult used = use_item(inv, inv_items, env, bev.use_item, nullptr);
                    player.hp = int(hp);
                    if (used == UseResult::Consumed) content_item_used(bev.use_item);   // mods: ItemUsed (view_content.inl)
                }
                if (!bev.script.empty()) {                   // S_COMMON "@UseHammer" ...
                    std::copy(game.g_flags, game.g_flags + 0x200, csMgr.vm.g_flags);
                    csMgr.vm.g_flags[150] = character;
                    if (!csMgr.start(bev.script))
                        std::printf("  [book] %s did not start\n", bev.script.c_str());
                }
                if (bev.save_slot >= 0) {
                    save_cont();                     // FUN_0042e9a0 snapshots before it writes
                    do_save(bev.save_slot);
                }
                if (bev.delete_slot >= 0) {
                    std::error_code ec;
                    fs::remove(slot_path(bev.delete_slot), ec);
                    content_delete(slot_path(bev.delete_slot));   // mods: its sidecar
                }
                if (bev.load_slot >= 0) {
                    std::ifstream f(slot_path(bev.load_slot), std::ios::binary);
                    std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(f), {}};
                    if (auto r = SaveRecord::decode(raw)) {
                        content_load(slot_path(bev.load_slot), bev.load_slot);   // mods: its sidecar
                        // A load is the "load" warp (FUN_00431260): a full scene load even into the same
                        // room, so the actors the room's scripts spawned before do not survive it.
                        audio.stop_all(true);            // FUN_005ce430(1) [BYTES 0x43136a]: the manual beds too
                        apply_loaded(from_record(*r), /*force_scene*/true);
                        // and its scene entry fades in from black (FUN_00431840 -> FUN_00439170), whatever
                        // fader a script of the room left behind
                        csHost.fade_rgb = 0; csHost.fade_cur = 1.0f; csHost.fade_tgt = 0.0f; csHost.fade_rate = 0.05f;
                        save_cont();                 // FUN_00431260 "load"
                        hud.debug_message("Game loaded.");
                    }
                }
                if (bev.options_changed) {
                    audio.set_bgm_volume(oslider[0] / 1024.0f);
                    audio.set_master_volume(oslider[1] / 1024.0f);
                    options_save(oslider, ocheck);
                }
                if (bev.option_quit == 1) running = false;  // Exit: WM_CLOSE
                if (bev.option_quit == 0) {                  // Return to Title (FUN_005b8d40, box 0xf, Yes)
                    // [BYTES 0x5b90ce..0x5b9182] the playing BGM id 0x74c0f4 = 0 (cur_bgm: the title's
                    // entry Bgm_Play(1) at 0x4323d2 then starts its track), the BGM fades over 3000 ms
                    // (FUN_005cde10(0xbb8), when the sound is on), the fader jumps to black (0x769f1c =
                    // 0x769f24 = 1.0) with the title record 0x74e678 pending (0x769fb8): FUN_004106e0
                    // changes scene on the next step. The same route as the game over's (LAB_00427750, which fades instead of
                    // jumping): leave this game, back to the title (its entry fades in from black).
                    // [not modelled] FUN_0042f510 deletes the crash backup (CleriaCore keeps none).
                    std::printf("  [book] Return to Title\n");
                    audio.fade_out_bgm(0xbb8);
                    cur_bgm = 0;
                    csHost.fade_rgb = 0; csHost.fade_cur = 1.0f; csHost.fade_tgt = 1.0f;
                    return_to_title = true; running = false; accum = 0;
                }
                if (bev.closed) paused = false;
            }
        }
        int ticks = 0;
        while (accum >= kTickDt) {
        accum -= kTickDt;
        ++tick_count;
        audio.advance(735);                        // no device (CLERIA_MUTE): 44100 / 60 frames a tick
        if (sfx_log) {
            std::vector<int> lp = audio.looping();
            if (lp != sfx_loops) {
                std::printf("  [sfx] t%llu loops:", (unsigned long long)tick_count);
                for (int id : lp) std::printf(" %d", id);
                std::printf("\n");
                sfx_loops = std::move(lp);
            }
        }
        ++game.g_flags[199];                       // 0x76bc38, the play time (FUN_004281c0 0x428274)
        speedrun_tick(room.name, hud.boss_frac >= 0.0f, hud.boss_cur, game.g_flags, player.hp > 0);   // IGT, auto-splits
        for (const DriveCmd& dc : drive) if (dc.tick == tick_count) run_drive(dc.cmd);
        if (!drive_more.empty()) { drive.insert(drive.end(), drive_more.begin(), drive_more.end()); drive_more.clear(); }
        access_tick();                            // a drive's held Esc, the shake scale (view_access.inl)
        ++ticks;
        // ---- the input record: one mapper step per logic tick (FUN_005d2ba0 from FUN_00410440) ----
        // g_in is read below by the windows (tick_events), the mover's move / walk / jump / attack /
        // magic bits; its edges are dispatched here. A drive's commands land alongside (above).
        input::Frame pin;                          // the player's copy of the record (click-to-move writes it)
        bool probe_hit = false;                    // this tick's attack press started an action (FUN_00460470)
        {
            const input::Frame& inf = input_tick();
            // The cursor manager FUN_004263c0 runs before the object ticks, on a record that only the
            // player tick sees (FUN_004106e0 restores it around them): `pin` [EXPORT FUN_004281c0].
            pin = inf;
            {
                auto project = [&](const Vec3& o, float* x, float* y) {   // 0x796170: world -> back-buffer px
                    if (!have_vp) return false;
                    const float* M = last_vp.m.data();
                    const float px = o.x * M[0] + o.y * M[4] + o.z * M[8] + M[12];
                    const float py = o.x * M[1] + o.y * M[5] + o.z * M[9] + M[13];
                    const float pw = o.x * M[3] + o.y * M[7] + o.z * M[11] + M[15];
                    if (!(pw > 1e-6f)) return false;
                    *x = (px / pw * 0.5f + 0.5f) * float(cw);
                    *y = (0.5f - py / pw * 0.5f) * float(ch);
                    return std::isfinite(*x) && std::isfinite(*y);
                };
                mouse::Context cc;
                cc.menu = csHost.wdlg.alive() || gameover_menu.alive();   // GoWndDialog windows
                cc.event = csMgr.world_frozen() || csHost.wtext.alive();  // a fix, or GoWndText
                cc.player = cc.controlled = !fly;
                // FUN_00473540's point for the arrow (0x7325c8): the floor under the player clamped into
                // [feet - 2.75, feet] [BYTES 0x473907..0x473962].
                float ax = 0, ay = 0;
                if (project({feet.x, mouse::shadow_y(feet.y, last_floor), feet.z}, &ax, &ay)) { g_cursor.px = ax; g_cursor.py = ay; }
                // The click-to-move anchor (the look point - 2.0) [BYTES 0x42684f..0x4268a9]. [H] the
                // 0.15-smoothed +0x230 is taken unsmoothed: the floor under the player + h.
                cc.anchor_ok = project({feet.x, mouse::steer_anchor_y(feet.y + kOriginH, last_floor + kOriginH, kOriginH), feet.z},
                                       &cc.anchor_x, &cc.anchor_y);
                cursor_tick(pin, cc);
            }
            if (death_t > 0 || !csMgr.world_frozen()) g_skip_gate.reset();   // no cutscene: no skip prompt
            if (death_t > 0) {
                // The game-over menu (FUN_00427820) takes the OK edge; its cursor reads the repeat step.
                if (inf.pressed & input::kOk) csHost.advance = true;
            } else if (csMgr.world_frozen()) {
                // A Fix holds the world: OK advances the window (0x12000 for the text, a dialog
                // menu's OK is 0x2000 [not modelled: the mouse hover test]); over a script menu Cancel
                // / Esc is its cancel (Menu_AssignCancel). Elsewhere Esc (the menu bit 0x10) SKIPS the
                // whole cutscene [CleriaCore]: tick_events drains it to the end, applying every flag /
                // spawn / warp in order but collapsing the Wait_Time delays. A text window's own
                // Cancel (the auto-repeat 0x4000) is retail's fast-forward, read in tick_events.
                // Retail has no cutscene skip: FUN_004106e0 ignores the menu bit while the fix count
                // 0x75ad90 + 0x75ad94 is non-zero (build/shell_re/ACCESS.md §1).
                const bool menu = csHost.wdlg.alive() && csHost.wdlg.is_menu();
                // The shell's Cutscene skip mode (single / double / hold / off: view_access.inl).
                const bool skip_now = !menu && access_skip_step((inf.pressed & input::kMenu) != 0,
                                                                (inf.held & input::kMenu) != 0);
                if (menu) g_skip_gate.reset();
                if (inf.pressed & (menu ? input::kOk : input::kOk | input::kClick)) csHost.advance = true;
                else if (menu && (inf.pressed & (input::kCancel | input::kMenu))) csHost.wnd_cancel = true;
                else if (skip_now) skip_cutscene = true;
            } else if (!paused) {
                // [EXPORT 004106e0.c L47-57] the book opens only while the fader is clear (0x769f1c == 0):
                // never during a door's fade-out / fade-in. [not modelled] its other gates (0x76bc20, flag 156).
                if ((inf.pressed & input::kMenu) && (!fly || ks[SDL_SCANCODE_ESCAPE]) && csHost.fade_cur == 0.0f) {
                    // The book (FUN_00443150 on the menu bit's edge, PAUSEBOOK_DRAW.md §1): Esc or the
                    // Camp Menu binding (only Esc in free-fly, whose raw keys may overlap it). The game
                    // ticks stop with this one.
                    open_book(0);
                    accum = 0;
                } else if (!fly) {
                    // The attack press runs the action probe FUN_00460470 first (+0x50c & 1,
                    // [BYTES 0x465abf]); only when it starts nothing is it a swing. [approx] the
                    // probe's surrounding gates in FUN_00465170 are not modelled.
                    // A started action also resets the combo (0x465b05: +0x6f0 = 0, +0x6f4 = 0).
                    if (inf.pressed & input::kAttack) {
                        probe_hit = try_interact();
                        if (!probe_hit) try_attack();
                        else { pact.stage = 0; pact.request = false; }
                    }
                    if (inf.pressed & input::kBoost) try_boost();   // the mover 0x465b1d on +0x50c & 8
                }
            }
        }
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
            // it below too; harmless.) Its HP too: Chr_IsLiving(13) (0x13F) reads it, and the
            // region triggers (S_EVT1099's boss intro and 8 more floor-1 scripts) gate on it.
            if (Actor* wp = world.player()) {
                wp->pos = feet;
                wp->hp = float(player.hp);
                wp->max_hp = float(player.max_hp);
            }
        }
        // One flag store (FLOOR1_ROUTE_AUDIT B4): the engine's own writes (156 / 130 at a boss
        // death, 141 Boost, the pickups) reach the running scripts; their writes come back
        // through the grants drain below. Writes a script made since the last drain (a probe or
        // a book script started between ticks) are applied FIRST: the copy would otherwise
        // undo them in the VM before the script's own child reads them (the statue cleanse:
        // SAVE.XSO's 311 = 1, then s_SaveObjectChange read 311 == 0 and kept the demon statue).
        apply_script_grants();
        std::copy(game.g_flags, game.g_flags + 0x200, csMgr.vm.g_flags);
        std::copy(game.item_arr, game.item_arr + 54, csMgr.vm.item_arr);   // IsXXX 7 / 8 read it
        csMgr.vm.g_flags[150] = character;
        const bool frozen_now = tick_events();
        {   // The log: a 0 <-> non-0 edge of either fix counter (FIX_SLOTS.md S0).
            static int fix_was[2] = {0, 0};
            for (int s = 0; s < 2; ++s) {
                if ((fix_was[s] != 0) != (csMgr.fix_counts[s] != 0))
                    std::printf("  [fix] t%llu slot %d %s: %d/%d\n", (unsigned long long)tick_count, s,
                                csMgr.fix_counts[s] != 0 ? "taken" : "released", csMgr.fix_counts[0], csMgr.fix_counts[1]);
                fix_was[s] = csMgr.fix_counts[s];
            }
        }
        // The door's fade machine (FUN_004106e0 0x4108a5..0x41094f, every real step, a Fix or not): once
        // current == target with a door pending, commit when the player lives (HP 0x76a730 >= 1, no game
        // over 0x74ab71) -- FUN_00430ef0 sets 0x74c120 |= 3 and the scene swaps at the next frame -- and
        // drop the pending door either way. tick_events stepped the fader (fade_advance) above, so a
        // trigger on tick T commits on T + 20, as in retail (the step after the trigger's 20th 0.05).
        if (door_pending && csHost.fade_cur == csHost.fade_tgt) {
            if (player.hp >= 1 && death_t == 0) {
                door_commit = door_pending;
                std::printf("  [door] t%llu commit %s (fade %.2f)\n", (unsigned long long)tick_count,
                            door_commit->dest_scene.c_str(), csHost.fade_cur);
            }
            door_pending.reset();
        }
        world.tick_alpha_fades();                  // Chr_Alpha (runs under a Fix too, [H])
        world.tick_tremble();                      // Chr_Tremble (the anim tick's head, 0x447306)
        for (Emotion& em : emotions) {             // FUN_004ffab0 (not frozen by a Fix)
            // [H] the engine never clears a dead owner's pointer; the port drops the bubble.
            if (em.owner != 13 && !world.find(em.owner)) em.dead = true;
            step_emotion(em);
        }
        emotions.erase(std::remove_if(emotions.begin(), emotions.end(),
                                      [](const Emotion& em) { return em.dead; }), emotions.end());
        hud.tick_boss_gauge();                     // FUN_004281c0's widget loop (FUN_0043a540)
        // ... and the same tick's mini-gauge timers (widget 0; MINI_GAUGE.md §2). [H] held with the
        // other widget state: the engine's own pause local in FUN_004281c0 is not traced (U4).
        for (int gi = 0; gi < 3; ++gi) {
            static int was[3] = {0, 0, 0};             // the log only: a gauge coming on / going off
            const WorldGauge& wg = hud.gauge[gi];
            if ((was[gi] != 0) != (wg.timer != 0)) {
                if (wg.timer != 0) std::printf("  [hud] t%llu mini gauge %d on: %g / %g\n", (unsigned long long)tick_count, gi, wg.value, wg.max);
                else std::printf("  [hud] t%llu mini gauge %d off\n", (unsigned long long)tick_count, gi);
            }
            was[gi] = wg.timer;
        }
        tick_world_gauges(hud.gauge);
        {   // FUN_004281c0: the HUD's alpha moves 0.1 per step toward 1 - the box's alpha (0 while
            // flag 185 == 0) [BYTES 0x428638..86]; the CrossFade opacity steps by its rate.
            const float box = csHost.wtext.alive() ? csHost.wtext.box_alpha() : 0.0f;
            const float target = game.g_flags[185] != 0 ? 1.0f - box : 0.0f;
            if (hud.gauge_alpha < target) hud.gauge_alpha = std::min(target, hud.gauge_alpha + 0.1f);
            else hud.gauge_alpha = std::max(target, hud.gauge_alpha - 0.1f);
            csHost.cf_alpha = std::clamp(csHost.cf_alpha + csHost.cf_rate, 0.0f, 1.0f);
        }
        if (bgm_gain_step != 0.0f) {               // Bgm_Volume's fade
            bgm_gain += bgm_gain_step;
            if (bgm_gain_step < 0.0f ? bgm_gain <= bgm_gain_to : bgm_gain >= bgm_gain_to) {
                bgm_gain = bgm_gain_to;
                bgm_gain_step = 0.0f;
            }
            audio.set_bgm_volume(oslider[0] / 1024.0f * bgm_gain);
        }
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
            apply_script_grants();
            content_tick(game.g_flags);            // mods: queued events + the g_flags diff (view_content.inl)
            lua_tick();                            // the Lua mods' "tick" handlers (view_lua.inl)
        }
        // A live script showed / hid an object (or switched its collision): rebuild the room's
        // geometry with the overrides, mesh only (the scripts keep running).
        if (live_obj_dirty) {
            live_obj_dirty = false;
            reload_room(/*rerun_events*/false);
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
        // FIX_SLOTS.md §1.4, S2 (issue #70): Hugo's Eyes of Fact are handler 83 (FUN_004731d0), which tests the
        // actor-stop byte 0x74ab8b = slot 1 alone [EXPORT 004731d0.c:22-23]. Under a Fix that holds slot 0 only
        // their handler runs on (they follow, orbit, shoot); under slot 1 they hold, as before.
        // [not modelled] the other 90 handlers 3..93 run under slot 0 too (S5c).
        bool eyes_run = false;
        if (frozen_now && !csMgr.actors_stopped())
            for (const EnemyRender& e : render) eyes_run = eyes_run || (e.actor && e.actor->ai_index == 83 && !e.actor->destroy_requested());
        if (frozen_now) {
            // CHR_OPS.md §4/§5: a Chr_Pattern pin stops the actor's ASO (FUN_004472e0 0x44735f returns
            // before the VM); a Chr_Animate motion (+0x994) is the section played, set once when it
            // changes; otherwise the locomotion section of the mover's table 0x69ba48 {idle 2, walk
            // 0, run 1}, run = moving with the RunOrWalk bit (+0x12c & 0x20).
            for (EnemyRender& e : render) {
                if (!e.actor || !e.avm.ok()) continue;
                const Actor& A = *e.actor;
                if (eyes_run && A.ai_index == 83) continue;   // its own handler runs below (S2)
                if (A.pattern.pinned()) continue;
                // The locomotion state +0x3a0 (idle / walk / run) is kept apart from the motion +0x994: when
                // it changes, FUN_0045f4d0 sets the table's motion over whatever plays, a Chr_Animate motion
                // included, unless that motion is 3 or AniDisable is on [EXPORT 0045f4d0.c L168-176]. So an
                // actor left on a Chr_Animate motion runs when the script next moves it (issue #72: Epona
                // glided out of S_1080 in her idle, still held by the Chr_Animate(201, 3) of her leap).
                {
                    const int loco = (A.cmoving || A.fly_r2 != 0.0f) ? ((A.flags12c & 0x20u) ? 1 : 0) : 2;
                    if (loco != e.fix_loco && A.anim_motion >= 0 && !(A.tag.bits & 0x80000u) && e.avm.section() != 3)
                        e.actor->anim_motion = -1;
                    e.fix_loco = loco;
                }
                if (A.anim_motion >= 0) {
                    if (e.motion_applied != A.anim_motion) {
                        e.avm.set_section(A.anim_motion, /*force*/true);
                        e.motion_applied = A.anim_motion;
                    }
                } else if (A.tag.bits & 0x80000u) {
                    e.motion_applied = -1;                  // 0x164 Chr_AniDisable: FUN_0045f4d0 keeps
                                                            // the current motion (FLOOR13_OPS.md)
                } else {
                    e.motion_applied = -1;
                    if (A.cmoving || A.fly_r2 != 0.0f) {
                        const int want = (A.flags12c & 0x20u) ? 1 : 0;
                        if (e.avm.section() != want && !e.avm.set_section(want, /*force*/true))
                            e.avm.set_section(0, /*force*/true);
                    }
                    else if (e.avm.section() != e.idle_section) e.avm.set_section(e.idle_section, true);
                }
                e.spec.tick();
                e.avm.tick();
                e.spec_event();
                // The same VM step as in play: a scripted actor's section sounds (a walk's steps, a swing).
                if (!hugo_eye(A)) for (const AsoVm::SoundEvent& se : e.avm.sounds()) aso_se_at(se.id, A.pos);
                // Surface footsteps of any actor with the enable byte +0x4be (Chr_FootSound; cast 0x7e): the
                // countdown the player runs, FUN_0045c490's tail [EXPORT 0045c490.c:374-397] and FUN_0045d600
                // [EXPORT 0045d600.c]: sections 0 / 1 only, the step at the actor (FUN_0045d7a0), the reload
                // from the surface record, the jitter (rng >> 26) % n drawn only where the record has one.
                // [H] the moving byte +0x514 = the script mover's cmoving; [approx] IsLand +0x388 is not tested
                // (a scripted actor's ground state is not kept under a Fix), nor +0x250 / +0x398 / the water rows.
                if (A.footsteps) {
                    if (e.step_timer > 0) --e.step_timer;
                    else if (!(A.cmoving || A.fly_r2 != 0.0f)) e.step_n = 0;
                    else if (e.avm.section() == 0 || e.avm.section() == 1) {
                        int tri = -1;
                        floor_at(phys_mesh(room), A.pos.x, A.pos.z, A.pos.y + A.height, &tri);
                        const std::string_view surf = surface_of(phys_mesh(room), tri);
                        const int idx = surf.empty() ? -1 : surface_index_or_default(surf);
                        const int id = idx < 0 ? -1 : footstep_sound(idx, e.step_n);
                        if (id >= 0) {                                 // a silent surface: no count, no reload
                            ++e.step_n;
                            sfx_at(id, A.pos, 0.55f);                  // the player's step volume [HAND-TUNED]
                            e.step_timer = footstep_interval(idx, e.avm.section() == 1,
                                                             kSurfaceTable[idx].jitter ? game.rng_next() >> 26 : 0u);
                        }
                    }
                }
            }
            // [H] the scripted walk keeps playing its section (a forced re-set every frame froze it on
            // frame 0) and settles to idle on arrival, as the generic mover's table does. The run
            // stage (+0x4ca, a RunOrWalk'd MoveTo) plays the run section 1 [H-high: the table].
            const Actor* wp = world.player();
            if (!(wp && wp->pattern.pinned())) {
                if (wp && wp->anim_motion >= 0) {
                    if (p_motion_applied != wp->anim_motion) {
                        avm.set_section(wp->anim_motion, /*force*/true);
                        p_motion_applied = wp->anim_motion;
                    }
                    avm.tick();
                } else if (csHost.p_moving) {
                    p_motion_applied = -1;
                    const int want = csHost.p_running() ? 1 : 0;
                    if (avm.section() != want) avm.set_section(want, /*force*/true);
                    avm.tick();
                } else if (avm.section() == 0 || avm.section() == 1 || p_motion_applied >= 0) {
                    p_motion_applied = -1;
                    avm.set_section(2, /*force*/true); avm.tick();
                } else {
                    // A Fix does not stop her motion (issue #74): FUN_0046fba0 still calls FUN_0046f980 on
                    // every real step, which under a Fix (0x75ad90 + 0x75ad94 != 0) zeroes the input record
                    // (+0x508..+0x514) and runs the mover FUN_00465170 as usual [EXPORT 0046f980.c L24-51].
                    // So the section in progress (a swing, a shot, a hurt) plays to its `end` and chains
                    // to idle, the dash 0x10a falls back to locomotion (00465170.c:1935-1943: idle with
                    // no input) and idle keeps animating. Its sounds and bullets fire as the ASO issues
                    // them. [approx] GenerateCollision / GenerateEffect / GenerateLocus of a section
                    // caught by a Fix are not spawned. An airborne player falls and lands (fix_fall, below).
                    if (avm.section() == 0x10a) avm.set_section(2, /*force*/true);
                    avm.tick();
                    spawn_player_bullets();
                    for (const AsoVm::SoundEvent& se : avm.sounds()) audio.play(se.id, kSfxAnimVol);
                }
            }
            // Her sprite under a Fix: the pinned pattern, else what the ASO shows.
            show_player_frame(wp);
        }
        // The GoMesh pool updates every live step, a Fix or not (WAITOPS.md §4.7).
        auto step_meshes = [&] {
            mask_step(g, room, mask_of_eyes_on(inv));   // the Mask of Eyes objects (view_mask.inl)
            game.g_flags[164] = mask_of_eyes_on(inv) ? 1 : 0;   // DAT_0076bbac, refreshed every game step (M450.md §2)
            meshes.step();
            boss_tick();
            boss2_tick();
            boss3_tick();
            boss4_tick();
            boss5_tick();
            boss6_tick();
            boss7_tick();
            boss8_tick();
            {   // The hit flash obj+0x2a0 of the boss GoMeshes at their draw (SCREEN_EFFECTS.md §3.2). FUN_00504140
                // [EXPORT 00504140.c]: f > 1.0 -> f -= 0.65 (0x6bd34c); every material record's +0x24..+0x2c =
                // (107, 55, 19) / 255 x 0.5 x f (0x6bdb0c, 0x6bda74, 0x6bd988, 0x6bdc20, 0x6bd304), zeroed after
                // the draw. The models' draws that call it: Room::mat_add (apply_object_alpha).
                auto flash = [&](const std::string& nm, float f) {
                    const float f0 = f;
                    if (f > 1.0f) f -= 0.65f;
                    const Vec3 c{107.0f / 255.0f * 0.5f * f, 55.0f / 255.0f * 0.5f * f, 19.0f / 255.0f * 0.5f * f};
                    const auto it = room.mat_add.find(nm);
                    const float was = it == room.mat_add.end() ? 0.0f : it->second.x;
                    if (c.x == was) return;
                    MeshObj* m = meshes.find(nm);
                    if (!m || !room.mesh.object_range(nm)) return;
                    room.mat_add[nm] = c;
                    m->dirty_alpha = true;
                    if (scrfx_log && c.x > was && f0 > 1.0f)   // a new flash (not the curve's own step up at 1.0)
                        std::printf("  [hitflash] t%llu %s +0x2a0 %.2f -> +(%.3f, %.3f, %.3f)\n", (unsigned long long)tick_count,
                                    nm.c_str(), f0, c.x, c.y, c.z);
                };
                if (boss) flash("boss01", boss->flash());
                if (boss2) for (int o = 0; o < boss02::kObjects; ++o) flash(boss02::object_name(o), boss2->object(o).flash);
                if (boss3) flash("boss03", boss3->flash());
                if (boss4) flash("boss04", boss4->flash());
                if (boss5) flash("boss05", boss5->flash());
                if (boss6) for (int i = 0; i < boss06::kHeads; ++i) flash(boss6_name[i], boss6->head(i).flash);
                if (boss7) {
                    flash("boss07", boss7->body().flash);
                    for (int i : {boss07::kOptR, boss07::kOptL}) {
                        // An option's own +0x2a0 (1.65 when its part is hit, DALLES_AB §B.7) decays in the GoMesh
                        // update's case 1 / 3 [EXPORT 00562990.c:83-88]: -= 0.075 (0x6bd174), floor 0.
                        // [H] that the options' type byte +0x28d is 1 or 3 (it is not in the spec).
                        float& cf = boss7->child(i).flash_2a0;
                        if (cf != 0.0f) { cf -= 0.075f; if (cf < 0.0f) cf = 0.0f; }
                        flash(boss7_child_name[i], cf);
                    }
                }
                if (boss8) for (int i : {boss08::kB, boss08::kT, boss08::kW, boss08::kA}) flash(boss8_name[i], boss8->mesh(i).flash);
            }
            mesh_delta.clear();
            floor_moved = false;
            for (auto& [nm, m] : meshes.all()) {
                // 0x182 Mesh_SetMaterialColor: every material of that name, by the object's YMO material index.
                if (!m.mat_color.empty() && (m.dirty_color || !room.mat_rgb.count(nm)))
                    if (const RoomMesh::ObjRange* rr = room.mesh.object_range(nm))
                        if (const Model* md = cached_big_model(resolve_ymo_path(asset_root, rr->obj.ymo_path))) {
                            std::vector<Vec3> v(md->materials.size(), Vec3{1.0f, 1.0f, 1.0f});
                            for (std::size_t k = 0; k < v.size(); ++k) {
                                std::string mn = md->materials[k].name;
                                for (char& ch : mn) ch = char(std::tolower(static_cast<unsigned char>(ch)));
                                const auto it = m.mat_color.find(mn);
                                if (it != m.mat_color.end()) v[k] = it->second;
                            }
                            room.mat_rgb[nm] = std::move(v);
                            m.dirty_color = false; m.dirty_alpha = true;
                        }
                if (!m.dirty_pos && !m.dirty_pose && !m.dirty_alpha) continue;
                if (!m.dirty_pos && !m.dirty_pose) {         // 0x16e / 0x188 alpha only
                    apply_object_alpha(room, nm, m.alpha);
                    if (const RoomMesh::ObjRange* rr = room.mesh.object_range(nm))
                        if (rr->v1 > rr->v0 && rr->v1 <= room.mesh.vertices.size())
                            wgpuQueueWriteBuffer(g.queue, room.vbuf, std::size_t(rr->v0) * sizeof(RoomVertex),
                                                 &room.mesh.vertices[rr->v0],
                                                 std::size_t(rr->v1 - rr->v0) * sizeof(RoomVertex));
                    m.dirty_alpha = false;
                    continue;
                }
                m.frame_y_ok = false;
                const Vec3 was = baked_pos.count(nm) ? baked_pos[nm] : m.spawn;
                if (m.dirty_pose) {
                    const char* clip = (m.sect >= 0 && std::size_t(m.sect) < m.clips.size())
                                           ? m.clips[std::size_t(m.sect)].name.c_str() : nullptr;
                    const Mat4* wm = m.has_world ? &m.world : nullptr;
                    rebake_object(room.mesh, asset_root, nm, clip, m.time, &m.pos, false, &m.rot, wm);
                    rebake_object(room.coll, asset_root, nm, clip, m.time, &m.pos, true, &m.rot, wm, m.twin_clips);
                    // A brain dropped its collision this tick (boss02): the hull collapses to a point.
                    if (m.no_collide)
                        if (const RoomMesh::ObjRange* cr = room.coll.object_range(nm))
                            for (std::uint32_t k = cr->v0; k < cr->v1 && k < room.coll.vertices.size(); ++k) {
                                room.coll.vertices[k].px = room.coll.vertices[cr->v0].px;
                                room.coll.vertices[k].py = room.coll.vertices[cr->v0].py;
                                room.coll.vertices[k].pz = room.coll.vertices[cr->v0].pz;
                            }
                    // The object alpha (+0x418 -> the anim object's +0xc) reaches every subset
                    // through apply_object_alpha (the draw switches a fading opaque subset to
                    // mode 1 and skips a = 0); collapsing an alpha-0 object is now redundant.
                    if (m.alpha <= 0.0f)
                        if (const RoomMesh::ObjRange* rr = room.mesh.object_range(nm))
                            for (std::uint32_t k = rr->v0; k < rr->v1 && k < room.mesh.vertices.size(); ++k) {
                                room.mesh.vertices[k].px = room.mesh.vertices[rr->v0].px;
                                room.mesh.vertices[k].py = room.mesh.vertices[rr->v0].py;
                                room.mesh.vertices[k].pz = room.mesh.vertices[rr->v0].pz;
                            }
                    apply_object_alpha(room, nm, m.alpha);   // the re-bake dropped d7 x alpha
                } else {
                    const Vec3 d{m.pos.x - was.x, m.pos.y - was.y, m.pos.z - was.z};
                    translate_object(room.mesh, nm, d);
                    translate_object(room.coll, nm, d);
                    if (m.dirty_alpha) apply_object_alpha(room, nm, m.alpha);
                }
                mesh_delta[nm] = {m.pos.x - was.x, m.pos.y - was.y, m.pos.z - was.z};
                // MOVER.md §3.3: the carry is the change of the floor sub-mesh's matrix, and a
                // twin sub-mesh rides its render node (COLLISION.md §1c), so a re-posed floor
                // object carries by its frame's move (S_1004's rising steps), not just +0x50.
                // [approx] the frame of mesh 0 stands in for the sub-mesh stood on: its whole matrix (the
                // rider carry) only for a one-mesh model that plays by itself (the unnamed '#' objects: the
                // spinning floors, the lifts), where that frame IS the sub-mesh's. A boss's body, posed by
                // its brain (its hull is not that frame), keeps the old carry by the frame's translation:
                // with the whole matrix five boss fights and Velagunder's taunt changed (drive cases).
                if (nm == player_floor_obj) {
                    Mat4 f;
                    bool sole = false;
                    if (mesh_frame_mat(m, &f, &sole)) {
                        auto bf = baked_frame.find(nm);
                        if (bf != baked_frame.end()) {
                            mesh_delta[nm] = {f.m[12] - bf->second.m[12], f.m[13] - bf->second.m[13], f.m[14] - bf->second.m[14]};
                            floor_was = bf->second; floor_now = f; floor_moved = sole && nm[0] == '#';
                        }
                        baked_frame[nm] = f;
                    }
                } else {
                    baked_frame.erase(nm);
                }
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
                m.dirty_pos = m.dirty_pose = m.dirty_alpha = false;
            }
        };
        // The dead player (FUN_0046fba0's HP < 1 branch): no input decode / mover (FUN_0046f980),
        // the death count, the ASO plays the death section on (FUN_004472e0), then the game over.
        // [approx] the rest of the world (enemies, bullets) stands still here; retail runs it on.
        if (death_t > 0) {
            if (death_t < 0x65 && ++death_t == 100) game_over();
            avm.tick();
            // The death section's frames reach the sprite and its sounds play (issue #77: the VM ran on but
            // nothing showed its Pattern(5, n), so the player stood on the fall's first frame): the dead step
            // is FUN_004472e0 alone and the draw reads the actor's +0xc0 / +0xc8 as on any other step
            // [EXPORT 0046f810.c, 0046fba0.c]. Section 5 of C_010 / C_020 / C_030.ASO: SePlay3D(90), the
            // fall Pattern(5, 0..9) (Hugo 0..11; tag 307 in Yunica's / Toal's other form), SePlay3D(91) at
            // the landing, then Wait(-1) on the last frame. [approx] its sounds play 2-D here.
            show_player_frame(nullptr);
            for (const AsoVm::SoundEvent& se : avm.sounds()) audio.play(se.id, kSfxAnimVol);
            if (std::getenv("CLERIA_DRIVE") && (death_t == 30 || death_t == 90))
                std::printf("  [death] t+%d section %d tag %d frame %d\n", death_t, avm.section(), avm.anim_tag(), avm.frame());
            step_meshes();
            fxsim.step();
            fxsim.sweep();
            gameover_card.step();
            if (gameover_menu.alive()) {                     // FUN_005789a0 on the game-over menu
                WndInput gin;
                gin.confirm_held = (g_in.held & (input::kOk | input::kClick)) != 0;   // this tick's record
                gin.confirm_pressed = csHost.advance;
                csHost.advance = false;
                gin.vstep = csHost.wnd_vstep != 0 ? csHost.wnd_vstep : g_in.dy;
                csHost.wnd_vstep = 0;
                wnd_mouse(gin);
                std::vector<int> wsnd;
                gameover_menu.update(gin, wsnd);
                for (int id : wsnd) audio.play(id, kSfxMenuVol);
                if (gameover_menu.accepted()) {              // the record's callback
                    const std::string pick = gameover_menu.chosen();
                    std::printf("  [gameover] chose %s\n", pick.c_str());
                    if (pick == "retry") retry();
                    else { return_to_title = true; running = false; accum = 0; }
                }
            }
            continue;
        }
        // Yunica's form flag g_flags[149] (DAT_0076bb70) follows the bracelet each tick, under a
        // Fix too (FUN_0046af60 L102-120): 0x74 / 0x75 -> 1, 0x76 -> 0, others leave it; a change
        // forces section 2 (+0x994 = 2 written directly; the previous kept in +0x9a0).
        if (character == 1) {
            const int was = game.g_flags[149];
            if (inv.magic == 0x74 || inv.magic == 0x75) game.g_flags[149] = 1;
            else if (inv.magic == 0x76) game.g_flags[149] = 0;
            if (game.g_flags[149] != was) avm.set_section(2, /*force*/true);
        }
        if (frozen_now) {
            ail_tick();                              // a Fix kills the four status effects (view_ailments.inl)
            if (shield_on) remove_shield(true);      // FUN_0046af60 L411-536 [H-med: 0x75ad90/94 = the fix counters]
            if (barrier_fx || barrier_t) {           // ... and the super barrier [EXPORT 0046af60.c:538-648]; [not modelled] SE 761's fade
                if (fx_live(barrier_fx)) barrier_fx->kill = true;
                barrier_fx = nullptr; barrier_t = 0;
            }
            {   // The script pushes 0x15e / 0x15f move the player under a Fix too (issue #75): the integrator
                // FUN_0045a510 adds +0x5d0 / +0x5d8 while the counter +0x5dc runs with no fix gate [EXPORT
                // 0045a510.c L263-273], and the player's tick calls it on every real step, a Fix or not
                // [EXPORT 0046fba0.c L113]. S_1013's s_Evt1013_2 holds Fix(0) while EvtWindArizigo pulls
                // toward (0, 0, 8). [approx] the walls by one resolve_walls of the pushed spot, not the sweep.
                const float px0 = feet.x, pz0 = feet.z;
                #include "view_xso13_force.inl"
                if (feet.x != px0 || feet.z != pz0) {
                    const Vec3 body = resolve_walls(phys_mesh(room), {feet.x, feet.y + kStepUp, feet.z}, kCharRadius,
                                                    std::max(kCharH - kStepUp, 0.5f));
                    feet.x = body.x; feet.z = body.z;
                }
            }
            if (fix_fall) {
                // FIX_SLOTS.md S3: the integrator's vertical step for a player in the air when a Fix holds, with
                // the input record zeroed — dy from the timer +0x330 (the Fix edge zeroed the velocity and the
                // push, not the timer: a jump goes on), 0 on surface 0x12 under a Fix [EXPORT 0045a510.c:229-231],
                // the ceiling, the floor ray, the settle; no ledge sampler under a Fix [:625-631]. Then the jump
                // section's landing phase (+0x3a8, 00465170.c:1964-1988), so it plays out to idle.
                // [approx] the live tick's sand / water / ice / glide rows are not repeated here.
                float oy = feet.y + kOriginH;
                float dy = cur_surface == 0x12 ? 0.0f : vertical_dy(pv, false, false);
                const Vec3 org = sphere_wall_clear(phys_mesh(room), {feet.x, oy, feet.z}, pm.radius());
                if (pv.t > 0.0f && segment_hit(phys_mesh(room), org, {org.x, org.y + pm.radius() * 1.05f, org.z}, nullptr, nullptr))
                    pv.t = 0.0f;
                if (dy > 0.0f) dy = sweep_up(phys_mesh(room), org, pm.radius(), dy);
                oy += dy;
                const float ffl = floor_at(phys_mesh(room), feet.x, feet.z, oy);
                if (ffl > kNoFloor) last_floor = ffl;
                const bool landed = settle_vertical(pv, oy, last_floor, kOriginH, false);
                grounded = pv.grounded;
                feet.y = oy - kOriginH;
                if (avm.section() == 3) {
                    if (jump_phase == 1 && pv.t <= 0.01f) { jump_phase = 2; avm.fast_forward_to_marker(2); avm.set_marker_block(3); }
                    if (grounded && jump_phase != 0) { jump_phase = 0; avm.fast_forward_to_marker(3); avm.set_marker_block(-1); }
                }
                if (landed) std::printf("  [fix] t%llu the player lands under the Fix at y %.3f (section 0x%x)\n",
                                        (unsigned long long)tick_count, feet.y, unsigned(avm.section()));
            }
            step_meshes();
            // Effects run on under a Fix: the GoParticle step (0x5644c0) gates only on the menu
            // and the real-step flags, not the fix counters (STATUE_CLEANSE.md §2 [BYTES
            // 0x5644c1..d5]). This is what fades the ward mid-pan after KEKKAIBREAK's Eft_Kill.
            // [H] no hit tick here: whether particle hits land under a Fix is not traced.
            step_weather();                          // pool 9's weather runs under a Fix (view_weather.inl)
            step_bullets_frozen();                   // GoXx bullets fly on under a Fix (view_duel.inl)
            // An attached effect reads its actor's +0x20 every step (FUN_00451e60), and scripts move
            // actors under a Fix (Eft_LocateChr on the smith and the player runs inside one): keep
            // the attach matrices current here too, as the unfrozen tick does.
            for (Actor& a : world.actors()) {
                actor_mats[a.instance_id] = ef2sim::translation(a.pos.x, a.pos.y + a.height, a.pos.z);
                // The hurt timer +0x238 runs out under a Fix too: the puppet FUN_0045f250 (and every
                // handler) calls the integrator FUN_0045a510 on each real step, whose first call is
                // FUN_0045a3a0 with no fix gate [EXPORT 0045f250.c L20-49, 0045a510.c L144]. It stood
                // still here, so a duel opponent hit just before the ending Fix kept the hurt specular
                // for the whole scene (issue #72: Epona stayed red after the S_1080 duel).
                a.hit_flash = hitfx::step_hurt(a.hit_flash);
            }
            actor_mats[13] = ef2sim::translation(feet.x, feet.y + kOriginH, feet.z);
            fxsim.step();
            fxsim.sweep();
            // The GoPopup step (damage numbers, GUARD, pickup texts) gates on the menu byte 0x74ab8f and
            // the real-step byte 0x74c0f0 only, never on the fix counters [BYTES 0x565170..0x56517a;
            // EXPORT 00565170.c L17, L24]: a number on screen rises and fades through a cutscene (#74).
            for (Popup& pp : popups) step_popup(pp);
            popups.erase(std::remove_if(popups.begin(), popups.end(), [](const Popup& pp) { return pp.dead; }),
                         popups.end());
            // The chest item (0x116, kind 2) rises and fades under the opening script's own Fix:
            // S_BOXnn places it, then Wait_Time(60) / Wait_Time(210) inside Fix(1), across exactly
            // its rise (30..90) and fade (176..210). [H-high] from that timing; its pool's own
            // fix gate is not traced. Display-only: nothing is collected here.
            {
                PickupWorld pw;
                pw.hit = [&](const Vec3& a, const Vec3& b, Vec3* pt, Vec3* n) {
                    return segment_hit(phys_mesh(room), a, b, pt, n);
                };
                pw.player = feet;
                pw.touch = [](Pickup&) { return false; };
                pw.sparkle = pickup_sparkle;              // view_weather.inl
                for (Pickup& pk : pickups) if (pk.kind == 2) step_pickup(pick_rng, pk, pw);
                pickups.erase(std::remove_if(pickups.begin(), pickups.end(),
                                             [](const Pickup& pk) { return pk.dead; }), pickups.end());
            }
            if (eyes_run) {
                // The eyes' handler under a Fix(0) (S2): the enemy tick for handler 83 alone. What it reads of the
                // player is what a Fix leaves: no input, not moving. +0x504 (the tick counter the ring formation
                // turns with) counts on every actor's FUN_0045c490 in the original; here for the player and the
                // eyes only.
                const bool moving = false;
                if (Actor* wp = world.player()) { wp->pos = feet; ++wp->age_ticks; }
                {
                    #include "view_revive.inl"
                    #include "view_los.inl"
                    const bool eyes_only = true;
                    #include "view_enemy_tick.inl"
                }
                // ... then each eye's integrator FUN_0045a510, as the live tick's actor loop has it for a flyer:
                // the swept sphere through the mover's displacement (an eye moves through knock channel 2), dy =
                // its timer (0), the floor under it. [approx] no body-push pass (FUN_0045e460) for the eyes here.
                for (Actor& a : world.actors()) {
                    if (a.ai_index != 83 || a.is_player() || a.destroy_requested()) continue;
                    ++a.age_ticks;
                    if (!a.alive()) continue;
                    const Vec3 o0{a.pos.x, a.pos.y + a.height, a.pos.z};
                    const float hitY = floor_at(phys_mesh(room), o0.x, o0.z, o0.y);
                    const Vec3 o1 = collide_and_slide(phys_mesh(room), o0, {a.mover.dx, a.flyer ? a.vert.t : 0.0f, a.mover.dz}, a.radius);
                    if (hitY > kNoFloor && o1.y > hitY) a.floor_y = hitY;     // +0x318
                    a.pos = {o1.x, o1.y - a.height, o1.z};
                }
            }
            continue;
        }
        const float dt = kTickDt;

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
            {   // Velagunder's entries while he is stunned: no slope limit under the player (below).
                const RoomMesh::ObjRange* br = boss && boss->stunned() ? room.coll.object_range("boss01") : nullptr;
                room.coll.climb_t0 = br ? br->t0 : 0;
                room.coll.climb_t1 = br ? br->t1 : 0;
                room.coll.climb_reach = br && player_floor_obj == "boss01" ? pm.radius() : 0.0f;
            }
            // Movement is relative to the live camera (last frame's — the 1-frame lag
            // is imperceptible and avoids a chicken-and-egg with the follow camera).
            const Vec3 fwd = normalize({std::cos(cam.yaw), 0, std::sin(cam.yaw)});  // screen-up
            const Vec3 rt = normalize(cross({0, 1, 0}, fwd));                       // screen-right
            // The stick (+0x514 direction bits, +0x18 angle; FUN_005d2ba0): angle 0 = screen right,
            // +pi/2 = screen up. A digital direction composes the basis vectors exactly as the keys
            // always did; an analog stick takes its own angle.
            // `pin` is the record the player tick sees: click-to-move may have written its direction,
            // angle and magnitude (src/cleria/mouse). In mouse mode the mover takes the player -> cursor
            // angle 0x7325fc instead of the record's (FUN_0046f980 [BYTES 0x46f9fb..0x46fa12]), so a
            // key direction while the mouse moves steers toward the cursor too.
            Vec3 wish{0, 0, 0};
            const bool cursor_angle = g_cursor.mouse_mode && pin.dir != 0;
            if (pin.analog || cursor_angle) {
                const float a = cursor_angle ? g_cursor.move_angle : pin.angle;
                const float c = std::cos(a), s = std::sin(a);
                wish.x = rt.x * c + fwd.x * s; wish.z = rt.z * c + fwd.z * s;
            } else {
                if (pin.dir & input::kUp) { wish.x += fwd.x; wish.z += fwd.z; }
                if (pin.dir & input::kDown) { wish.x -= fwd.x; wish.z -= fwd.z; }
                if (pin.dir & input::kRight) { wish.x += rt.x; wish.z += rt.z; }
                if (pin.dir & input::kLeft) { wish.x -= rt.x; wish.z -= rt.z; }
            }
            if (drive_wish_n > 0) { --drive_wish_n; wish.x += drive_wish.x; wish.z += drive_wish.z; }
            if (drive_goto_n > 0) {
                const float gx = drive_goto.x - feet.x, gz = drive_goto.z - feet.z;
                if (--drive_goto_n == 0 || gx * gx + gz * gz < 0.04f) {
                    drive_goto_n = 0;
                    std::printf("  [drive] t%llu goto ends at (%.2f,%.2f,%.2f)\n", (unsigned long long)tick_count, feet.x, feet.y, feet.z);
                } else { wish.x += gx; wish.z += gz; }
            }
            // The door walk-out (FUN_0042cf00 0x42d246..0x42d29d -> the mover FUN_00465170 l.405-420): while
            // +0x364 != 0 the mover replaces the stick with a full one (+0x508 = 1.0) toward +0x358 / +0x35c,
            // until within sqrt(+0x364) = 0.35 of it. The goal is 100 ahead, so she walks on through the fade.
            if (door_walk.on) {
                const float dx = door_walk.tgt.x - feet.x, dz = door_walk.tgt.z - feet.z;
                if (dx * dx + dz * dz > 0.1225f) wish = {dx, 0.0f, dz};
                else door_walk.on = false;
            }
            // Confuse (FUN_00465170 [BYTES 0x465a7e..0x465aa9]): the input heading + pi while no scripted MoveTo
            // (+0x364 == 0) runs. STATUS_AILMENTS.md §6.3.
            if (ail.has(ailments::kConfuse) && !door_walk.on) { wish.x = -wish.x; wish.z = -wish.z; }
            const float wl = std::sqrt(wish.x * wish.x + wish.z * wish.z);
            const bool moving = wl > 1e-4f;
            // Run or walk (FUN_00465170 l.432, +0x4ca): magnitude (+0x508) >= RunWalkThreshold and the
            // walk bit (+0x510 & 0x20) not held. A key is magnitude 1.0; retail's stick magnitude is
            // >= ~125 past the deadzone, so only the Walk binding walks (LinearStickMagnitude: QoL).
            // A drive's move= is a full stick. [not modelled] the actor flag 0x40000 forcing the walk
            // bit. The speed itself is walk x run_mult (local_38 = 1.0 [EXPORT l.404]), not x stick.
            // speed_mult is the debug wheel.
            // The walk-out sets the walk bit unless the trigger saw the run stage (+0x12c & 0x20 = +0x4ca).
            // Heavy clears the run stage +0x4ca [BYTES 0x465a6e..0x465a77]: the player walks (§6.2).
            const bool run_gait = ail.has(ailments::kHeavy) ? false
                                : door_walk.on ? door_walk.run
                                : moving && !(pin.held & input::kWalk) &&
                                  (pin.dir == 0 || pin.magnitude >= settings().s.input.run_walk_threshold);
            door_running = run_gait;
            {   // MOVER.md §3.3: the platform carry. Grounded on an object that moved last
                // step: its displacement (clamped to 0.3); airborne: the carry decays x0.975.
                auto it = mesh_delta.find(player_floor_obj);
                const RoomMesh& rcm = phys_mesh(room);
                if (pv.grounded && ride_tri >= 0 && std::size_t(ride_tri) * 3 + 2 < rcm.indices.size()) {
                    // The stunned Velagunder: the carry is the change of the entry's matrix under the player
                    // (+0x570 cache, 0x45aa95..), clamped to 0.3 [BYTES 0x6bd27c]. An entry is rigid, so the
                    // face he stood on, re-posed by step_meshes, gives his point's move. [approx] no turn of
                    // the heading; the other bodies keep the frame's translation (below).
                    const RoomVertex& A = rcm.vertices[rcm.indices[std::size_t(ride_tri) * 3]];
                    const RoomVertex& B = rcm.vertices[rcm.indices[std::size_t(ride_tri) * 3 + 1]];
                    const RoomVertex& C = rcm.vertices[rcm.indices[std::size_t(ride_tri) * 3 + 2]];
                    const Vec3 e1{B.px - A.px, B.py - A.py, B.pz - A.pz}, e2{C.px - A.px, C.py - A.py, C.pz - A.pz};
                    const Vec3 n = cross(e1, e2);
                    Vec3 cd{A.px + ride_uvw.x * e1.x + ride_uvw.y * e2.x + ride_uvw.z * n.x - feet.x,
                            A.py + ride_uvw.x * e1.y + ride_uvw.y * e2.y + ride_uvw.z * n.y - feet.y,
                            A.pz + ride_uvw.x * e1.z + ride_uvw.y * e2.z + ride_uvw.z * n.z - feet.z};
                    const float q = cd.x * cd.x + cd.y * cd.y + cd.z * cd.z;
                    if (q > 0.09f) { const float k = 0.3f / std::sqrt(q); cd = {cd.x * k, cd.y * k, cd.z * k}; }
                    carry = cd;
                } else if (pv.grounded && it != mesh_delta.end() && floor_moved) {
                    // The whole matrix (0x45aa95..0x45ac14): the origin through inverse(cached) x current, and
                    // the platform's turn onto the heading +0x308 and the facing +0x314 (rider.hpp).
                    const RiderCarry rc = rider_carry(floor_was, floor_now, {feet.x, feet.y + kOriginH, feet.z});
                    carry = rc.d;
                    if (rc.yaw != 0.0f) {
                        const float h = std::atan2(face_dir.z, face_dir.x) + rc.yaw;
                        face_dir = {std::cos(h), 0.0f, std::sin(h)};
                        facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                    }
                } else if (pv.grounded && it != mesh_delta.end()) {
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
            // The jump press (+0x50c & 2), read once per tick: the record's pressed edge +0x24, which
            // FUN_0045f250 copies to +0x50c. The mapper (FUN_005d2ba0) keeps its held history through
            // the book's and a Fix's frames, so a B / X / right-click that closed a menu and is still
            // down reads as held, not pressed. The view's own last-tick latch went stale while no
            // player tick ran and jumped on that press (PLAYTEST_1.md §5).
            const bool jump_edge = (g_in.pressed & input::kJump) || drive_jump;
            drive_jump = false;
            // The action input (FUN_00465170 / FUN_0046ea00 via src/cleria/player_actions).
            PlayerActions::Out pa_out;
            if (!fly) {
                // The bracelet switch lands (FUN_00465170 0x4652ca..0x465377): a pending request when the
                // ASO is actable (+0x9d0), or a buffered attack meets an open cancel window (+0x9d2).
                // The switch also clears the skill charge 0x76c134 / 0x76c138 (0x465323 / 0x465330) and
                // the combo stage. [not modelled] the 0x76b914 |= 0x20 bit and Adol's +0x6fc reset.
                if (skill_req != -1 && (avm.actable() || (pact.request && avm.cancelable()))) {
                    if (skill_req >= 0x74 && skill_req <= 0x77) {
                        inv.magic = skill_req == 0x77 ? 0xff : skill_req;
                        pact.stage = 0;
                        pact.request = false;
                        skill_charge = {};
                        audio.play(0x0a, kSfxMenuVol);                    // FUN_0043a040(0xa)
                    }
                    skill_req = -1;
                }
                PlayerActions::In pi;
                pi.character = character;
                pi.attack_pressed = atk_press_pending && !paused;
                pi.attack_held = !paused && (g_in.held & input::kAttack) != 0;   // +0x510 & 1
                pi.skill_held = !paused && ((g_in.held & input::kMagic) || drive_skill);   // +0x510 & 4
                pi.jump_pressed = jump_edge && !submerged;       // the water re-jump stays below
                pi.stick_active = moving;
                pi.stick_angle = std::atan2(wish.z, wish.x);
                pi.facing = std::atan2(face_dir.z, face_dir.x);
                pi.section = avm.section();
                pi.actable = avm.actable(); pi.cancelable = avm.cancelable(); pi.reserve = avm.attack_reserve();
                pi.attack_break = avm.attack_break();             // anime_AttackBreak (vm+0xe5; Adol's sets only)
                pi.jump_enable = avm.jump_enable();
                pi.grounded = pv.grounded; pi.submerged = submerged; pi.t = pv.t; pi.landed = p_landed;
                pi.flag181 = game.g_flags[181] > 0;
                pi.flag163 = game.g_flags[163] > 0;
                pi.always_dash = ocheck[3];
                pi.kind = skillmp::attack_kind(pi.section);
                pi.busy = game.g_flags[161] != 0 || game.g_flags[151] == -1;
                pi.yunica_form = game.g_flags[149] != 0;
                // The double-click lunge (MOUSE.md §4.2): mouse mode, the 0x800 edge, the player ->
                // cursor angle in the stick basis (screen y-up through the camera, as the stick's).
                pi.mouse_mode = g_cursor.mouse_mode;
                pi.double_click = (pin.pressed & input::kDoubleClick) != 0;
                {
                    const float c = std::cos(g_cursor.move_angle), s = std::sin(g_cursor.move_angle);
                    pi.mouse_angle = std::atan2(rt.z * c + fwd.z * s, rt.x * c + fwd.x * s);
                }
                pa_out = pact.tick(pi);
                // The request (FUN_0046cb00 [BYTES 0x465a56..0x465b31]): the raw wheel (15-tick
                // cooldown), Previous / Next Skill, Skill 1-3; skipped when the attack press started an
                // action. Lands on a later tick (above).
                const int r = skill_sw.tick(g_in.wheel, g_in.pressed, probe_hit, inv.magic,
                                            [&](int id) { return id >= 0 && id < 0x200 && game.g_flags[id] > 0; });
                if (r >= 0) skill_req = r;
            }
            atk_press_pending = false;
            if (pa_out.face) face_dir = {std::cos(pa_out.face_angle), 0.0f, std::sin(pa_out.face_angle)};
            if (pa_out.section >= 0) avm.set_section(pa_out.section);   // FUN_00446550(id, 0): priority-gated
            if (pa_out.new_attack) {
                atk_dir = face_dir;                                    // the swing points where you face
                player_hit_serial = next_hit_serial(world.hit_serial_counter_);
                if (pa_out.section != 0x112) hugo_eyes_fire();         // FUN_0046d6e0: the eyes shoot too
            }
            hugo_eyes_tick();                                          // FUN_0046af60's eyes, FUN_0046a3e0's blast
            if (pa_out.t_add != 0.0f) pv.t += pa_out.t_add;            // the air combo's hang time
            if (pa_out.impulse) { player_kb.x += pa_out.impulse_x; player_kb.z += pa_out.impulse_z; }
            if (pa_out.lunge) { player_kb.x = pa_out.lunge_x; player_kb.z = pa_out.lunge_z; }
            // The Force Shield's tick (FUN_0046a3e0 from the mover 0x467f2b, Hugo only): the life
            // counts down; out of life with hits left it goes quietly, out of hits it breaks.
            if (character == 2 && shield_on) {
                if (shield_life != 0) --shield_life;
                if (shield_life == 0 || shield_hits == 0) remove_shield(shield_hits == 0);
                else if (!fx_live(shield_fx)) remove_shield(false);   // [guard, not retail] the effect is gone
            }
            // The barrier's tick [EXPORT 0046a3e0.c:378-387]: at 0 the effect +0x7e0 is killed, else the timer
            // counts down.
            if (barrier_t == 0) {
                if (barrier_fx) { if (fx_live(barrier_fx)) barrier_fx->kill = true; barrier_fx = nullptr; }
            } else --barrier_t;
            // Toal's pull (the tail of FUN_0046a3e0, every logic tick in 0x10c / 0x10f, no Marker test [EXPORT
            // 0046a3e0.c:388-476]; BOOST_SKILLS.md §3.5): every live monster (kind +0x60 & 0x18, +0x130 & 0x4000
            // clear) within 10 with a clear line to him is drawn on its knock channel 2 toward the point 2.0
            // ahead of him, 0.1695 x w a tick. [H] not under a Fix (this block is the unfrozen step).
            if (character == 3 && (avm.section() == 0x10c || avm.section() == 0x10f)) {
                const float h = std::atan2(face_dir.z, face_dir.x);            // +0x308
                const Vec3 P{feet.x, feet.y + kOriginH, feet.z};
                const Vec3 Q{P.x + skillmp::kPullAhead * std::cos(h), P.y, P.z + skillmp::kPullAhead * std::sin(h)};
                for (Actor& en : world.actors()) {
                    if (en.is_player() || en.hp < 1.0f || !(en.kind & 0x18u) || (en.flags130 & 0x4000u)) continue;
                    const Vec3 E{en.pos.x, en.pos.y + en.height, en.pos.z};
                    const float dx = E.x - P.x, dy = E.y - P.y, dz = E.z - P.z;
                    const float w = skillmp::pull_weight(std::sqrt(dx * dx + dy * dy + dz * dz), player.boosting);
                    if (w == 0.0f || segment_blocked(phys_mesh(room), P, E)) continue;
                    const Vec3 v{E.x - Q.x, E.y - Q.y, E.z - Q.z};
                    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                    if (l < 1e-6f) continue;
                    en.mover.k2.x -= v.x / l * skillmp::kPullStep * w;
                    en.mover.k2.z -= v.z / l * skillmp::kPullStep * w;
                }
            }
            const float hx0 = feet.x, hz0 = feet.z;   // the tick's horizontal start (the wall push below)
            {   const int sec = avm.section();
                const bool skill_sec = sec == 0x109 || (sec >= 0x10c && sec <= 0x111);
                const bool blessing_run = (game.g_flags[(0x76bc80 - 0x76b91c) / 4] & 0x2000) != 0;
                float spd = player_speed(pm, character, 1.0f, avm.movable(), /*running*/ run_gait,
                                         pv.grounded || avm.actable(), skill_sec, blessing_run,
                                         shield_life != 0) * speed_mult;
                if (moving && (sec == 0x10a || (sec == 3 && pact.dash)))   // the dash run (0x10a, or a dash jump)
                    spd = pm.speed_base * avm.movable() * pm.run_mult *
                          dash_char_const(character, blessing_run, shield_life != 0) * speed_mult;
                const PlanarVelocity pvel0 = pvel;
                step_planar(pvel, moving, std::atan2(wish.z, wish.x), spd, pv.grounded);
                if (pv.grounded && player_ice(cur_surface, inv.equip_flags)) {   // ice: the 0.35 blend (ice.hpp)
                    pvel.x = pvel0.x * (1.0f - kIceBlend) + pvel.x * kIceBlend;
                    pvel.z = pvel0.z * (1.0f - kIceBlend) + pvel.z * kIceBlend;
                    if (std::fabs(pvel.x) < 0.005f && std::fabs(pvel.z) < 0.005f) pvel = {};
                }
                // Godspeed: each tick in 0x10d / 0x110 past Marker 1 the C++ drives the dash and
                // ends the endless ASO loop with SetSection(0x105).
                if (character == 3 && (sec == 0x10d || sec == 0x110)) {
                    gs_ramp = std::max(0.0f, gs_ramp - 0.1f);          // +0x4d8 -= 0.1 (collision off)
                    if (avm.marker() >= 1) {
                        if (gs_t == 0) {
                            gs_locked = false;
                            if (!moving) {                              // the lock-on, stick neutral
                                float best = 12.0f * 12.0f;
                                for (const Actor& en : world.actors()) {
                                    if (en.is_player() || !en.alive() || en.hp < 1.0f) continue;
                                    const float ex = en.pos.x - feet.x, ez = en.pos.z - feet.z;
                                    const float d2 = ex * ex + ez * ez;
                                    if (d2 >= best) continue;
                                    const Vec3 a{feet.x, feet.y + kOriginH, feet.z};
                                    const Vec3 b{en.pos.x, en.pos.y + en.height, en.pos.z};
                                    if (segment_blocked(phys_mesh(room), a, b)) continue;   // line of sight
                                    best = d2;
                                    gs_locked = true;
                                    gs_target = {feet.x + ex * 0.95f, en.pos.y, feet.z + ez * 0.95f};
                                }
                                if (gs_locked) {
                                    const float dx = gs_target.x - feet.x, dz = gs_target.z - feet.z;
                                    const float l = std::sqrt(dx * dx + dz * dz);
                                    if (l > 1e-4f) face_dir = {dx / l, 0.0f, dz / l};
                                }
                            }
                        }
                        const float v = pm.speed_base * 10.0f;            // +0x30c x 10 = 0.56 / tick
                        const Vec3 want{face_dir.x * v, 0.0f, face_dir.z * v};
                        if (pv.grounded) { pvel.x = want.x; pvel.z = want.z; }
                        else { pvel.x += (want.x - pvel.x) * 0.1f; pvel.z += (want.z - pvel.z) * 0.1f; }
                        ++gs_t;
                        const bool charged = sec == 0x110;
                        bool end = false;
                        if (!gs_locked) end = gs_t > (charged ? 20 : 10);
                        else {
                            const float dx = gs_target.x - feet.x, dz = gs_target.z - feet.z;
                            end = (gs_t > 4 && dx * dx + dz * dz < 4.0f) || gs_t > (charged ? 30 : 15);
                        }
                        if (end) {
                            avm.set_section(0x105, /*force*/true);       // Toal's recovery
                            gs_t = 0;
                        }
                    } else {
                        gs_t = 0;                                       // the wind-up
                    }
                } else if (gs_ramp < 1.0f) {
                    gs_ramp = std::min(1.0f, gs_ramp + 0.0784f);       // 0x6bd17c, then +0x60 |= 0x80
                    gs_t = 0;
                }
                const SurfaceScale ss = surface_scale(cur_surface, submerged,
                                                      (game.g_flags[(0x76bc80 - 0x76b91c) / 4] & 2) != 0,
                                                      /*foot gear 0x76a624 & 0x1000000 [not modelled]*/ false,
                                                      (inv.equip_flags & 0x2000000u) != 0);   // the sand gear
                feet.x += pvel.x * ss.xz; feet.z += pvel.z * ss.xz;
                surface_up = ss.up;
            }
            if (moving && avm.turnable()) {
                // The heading +0x314 turns toward the stick at 0.35 rad/tick running (0.18 walking;
                // 0x6bd2b0 / 0x6bd1fc, FUN_005eee60; +0x4ca picks [EXPORT 00465170.c l.2539]).
                const float h = turn_facing(run_gait ? 0.35f : 0.18f, std::atan2(face_dir.z, face_dir.x), std::atan2(wish.z, wish.x));
                face_dir = {std::cos(h), 0, std::sin(h)};
            }
            if (Actor* wp = world.player()) {             // a brain's write to P+0x344 / +0x34c (M710's suction,
                player_kb2.x += wp->mover.k2.x;          // M820's 0x10f pull): the world player is a mirror,
                player_kb2.z += wp->mover.k2.z;          // the view's player_kb2 is the channel the mover reads
                wp->mover.k2 = {};
            }
            {   // Knockback, both channels (+0x338/+0x340 and +0x344/+0x34c), by the player
                // mover's own law (hit.hpp step_knockback_player).
                const Impulse kd = step_knockback_player(player_kb, player_kb2, grounded, player.hp < 1,
                                                         pact.step_t != 0);
                feet.x += kd.x; feet.z += kd.z; }
            #include "view_xso13_force.inl"          // 0x15e / 0x15f pushes (FLOOR13_OPS.md)
            // Airborne, the engine's sphere (radius +0x328 about the origin, +0x324 above the feet) is not stopped
            // by a wall top below its centre: the edge is an embedded contact pushed out along centre - closest
            // point (SWEEP_PUSHOUT.md §0.2..0.4), so the sphere rides over it and the settle lands it (the floor
            // ray starts at the origin). [approx] the capsule stand-in passes such a top at once instead of over
            // the push-outs' ticks; with the grounded 0.6 [INVENTED] a double jump (apex 5.37) stopped 0.03 short
            // of a 6-unit face: no way out of S_6004's box_02 platform (drive case ledge_s6004).
            // Only for a top of the room's own mesh (its unnamed first record), found a radius ahead of the move:
            // the hulls of scene objects (a boss's parts, steps) keep the old stand-in.
            float wallStep = stepUp;
            if (!pv.grounded) {
                const float ax = feet.x - hx0, az = feet.z - hz0, al = std::sqrt(ax * ax + az * az);
                int lt = -1;
                if (al > 1e-4f &&
                    floor_at(phys_mesh(room), feet.x + ax / al * (radius + 0.25f), feet.z + az / al * (radius + 0.25f),
                             feet.y + kOriginH, &lt) > feet.y + stepUp && lt >= 0)
                    for (const RoomMesh::ObjRange& r : phys_mesh(room).object_ranges)
                        if (std::uint32_t(lt) >= r.t0 && std::uint32_t(lt) < r.t1) {
                            if (r.name.empty()) wallStep = kOriginH;
                            break;
                        }
            }
            { Vec3 body = {feet.x, feet.y + wallStep, feet.z};
              // The engine sweeps the tick's whole move with its sphere (FUN_0045a510 0x45b2eb), so no
              // move crosses a wall. resolve_walls only resolves an overlap: a step past the radius (a
              // knock's 0.36 cap plus the run) took the centre beyond a one-plane wall, which then
              // pushed it out of its FAR side (Hugo through S_1002's east wall; PLAYTEST_1.md §4).
              // The move is pushed out in sub-steps of at most half the radius [H]; one step (the
              // old single push) whenever the tick moves less.
              {   const float mx = body.x - hx0, mz = body.z - hz0;
                  const int n = std::clamp(int(std::ceil(std::sqrt(mx * mx + mz * mz) / (radius * 0.5f))), 1, 16);
                  if (n > 1) { body.x = hx0; body.z = hz0; }
                  for (int k = 0; k < n; ++k) {
                      if (n > 1) { body.x += mx / float(n); body.z += mz / float(n); }
                      body = resolve_walls(phys_mesh(room), body, radius, std::max(eye - wallStep, 0.5f));
                  } }
              feet.x = body.x; feet.z = body.z; }
            // FUN_0046fba0 0x470049 (ARENA_AND_BOSSBAR.md §1.5c): in arena mode 1 (flag 190) the
            // player is box-clamped to the EnclosePlayer rect, min then max. Mode 2 (S_1000's
            // opening fights) is the soft ring at 0x4700aa: knock channel 1 pushes back
            // (arena_player.hpp).
            if (game.g_flags[190] == 1) {
                if (feet.x < world.arena[0]) feet.x = world.arena[0];
                if (feet.z < world.arena[1]) feet.z = world.arena[1];
                if (feet.x > world.arena[2]) feet.x = world.arena[2];
                if (feet.z > world.arena[3]) feet.z = world.arena[3];
            }
            if (game.g_flags[190] == 2) {
                const ArenaPush ap = player_arena_ring(feet.x, feet.z, arena_last_x, arena_last_z, world.arena);
                player_kb.x += ap.x; player_kb.z += ap.z;
            }
            // The engine's vertical model (physics_player.hpp), on the ORIGIN, which floats
            // at floor + 1.075: dy from the signed timer (linear while grounded, ½gt²
            // airborne), the down-ray from the origin, then landing / coyote / fall with the
            // 0.65 settle lerp. `feet` is derived back for the rest of the view.
            // The stunned Velagunder's body (VELAGUNDER.md §3.4): its entries' +0x89 is clear, so the mover's
            // slope push [EXPORT 0045a510.c:426-447] is off and the player walks up the arms. The engine has
            // no slope limit: the floor is a plain down-ray and the move sweep slides along the plane it
            // meets (FUN_005dca20, MOVER.md §3.5), D - (D.n)n. Its sphere (+0x328 = 0.675 about the origin,
            // +0x324 = 1.075 over the feet) meets a face under the feet when 1.075 n.y <= 0.675.
            // [approx] the capsule stand-in: the face under the moved feet stands for the swept contact,
            // within the settle's 0.4 of the feet.
            if (const RoomMesh& cm = phys_mesh(room); cm.climb_t1 > cm.climb_t0) {
                int ct = -1;
                const float cf = floor_at(cm, feet.x, feet.z, feet.y + kOriginH, &ct);
                if (ct >= 0 && std::uint32_t(ct) >= cm.climb_t0 && std::uint32_t(ct) < cm.climb_t1 && feet.y < cf + 0.4f) {
                    const RoomVertex& A = cm.vertices[cm.indices[std::size_t(ct) * 3]];
                    const RoomVertex& B = cm.vertices[cm.indices[std::size_t(ct) * 3 + 1]];
                    const RoomVertex& C = cm.vertices[cm.indices[std::size_t(ct) * 3 + 2]];
                    const Vec3 n = normalize(cross({B.px - A.px, B.py - A.py, B.pz - A.pz}, {C.px - A.px, C.py - A.py, C.pz - A.pz}));
                    const float into = (feet.x - hx0) * n.x + (feet.z - hz0) * n.z;
                    if (n.y > 0.0f && pm.height() * n.y <= pm.radius() && into < 0.0f) { feet.x -= into * n.x; feet.z -= into * n.z; }
                }
            }
            float oy = feet.y + kOriginH;
            const float oy0 = oy;                     // the origin before this tick's vertical move
            {   // The Ventus glide: Yunica (and Adol), airborne in 0x10d / 0x110 with the timer at or
                // under 0.05: a falling timer is damped x0.95, then lifted 0.01 (Adol 0.015).
                const int ss = avm.section();
                if ((character == 1 || character == 0) && !pv.grounded && (ss == 0x10d || ss == 0x110) &&
                    pv.t <= 0.05f) {
                    if (pv.t < 0.0f) pv.t *= 0.95f;
                    pv.t += character == 0 ? 0.015f : 0.01f;
                }
                // Toal's slow fall: 0x10e / 0x111 cast in the air (+0x7b0), the timer negative
                // [EXPORT 00465170.c:2491-2502].
                if (character == 3 && toal_air_cast && (ss == 0x10e || ss == 0x111) && pv.t < 0.0f)
                    pv.t = skillmp::slow_fall(pv.t, ss == 0x111);
                // Hugo's: while the shield effect stands (any section), airborne and falling,
                // t *= 0.88 (0x6bd3d8) — a sink of ~0.074 u/tick.
                if (character == 2 && shield_on && !pv.grounded && pv.t < 0.0f) pv.t *= 0.88f;
            }
            {   float dy = player_hold ? 0.0f : vertical_dy(pv, submerged, cur_surface == 0x12);   // P+0x398 (M470's hold)
                if (submerged && psand.on_sand2()) {   // the set over sand2 (src/cleria/sand.hpp, 0x45a652)
                    dy = sand2_dy(vertical_dy(pv), moving, psand.water_y, oy);
                    cur_surface = 0x12;                // 0x45a6b7: +0x4b9 = 0x12
                    surface_up = 0.25f;
                }
                if (dy > 0.0f) dy *= surface_up;      // sand2/lava/blood slow a rise too
                // The ceiling (FUN_0045a510, after dy is taken): while the timer is rising, a ray
                // from the origin up +0x328 x 1.05 (0x6bd458) zeroes +0x330 / +0x420 on a hit
                // [BYTES 0x45b219..0x45b29a]; then the move sweep, a sphere of +0x328 from the
                // origin (0x45b2eb..0x45b32f), stops the rise at whatever is above (ROUTE_F13 D2).
                // Both from where the engine's 0.675 sphere would stand, clear of the walls beside
                // it: from the 0.45 capsule's spot its rim caught a step / ledge / wall top and cut
                // the jump short (PLAYTEST_1.md §1, physics.hpp sphere_wall_clear). [approx]
                const Vec3 org = sphere_wall_clear(phys_mesh(room), {feet.x, oy, feet.z}, pm.radius());
                if (pv.t > 0.0f && segment_hit(phys_mesh(room), org, {org.x, org.y + pm.radius() * 1.05f, org.z},
                                               nullptr, nullptr))
                    pv.t = 0.0f;
                if (dy > 0.0f) dy = sweep_up(phys_mesh(room), org, pm.radius(), dy);
                oy += dy; }
            // The ledge recovery (FUN_0045a510 0x45b607.., ledge.hpp): with nothing within 510 under where the
            // player stood, he is pulled back toward the last safe spot, and after 5 such ticks put back on the
            // last position reached from over a floor. Off under a Fix and in floor 5's +0x500 rectangles.
            // [approx] the view's move is not one swept displacement: D is taken as this tick's actual move, so
            // +0x4f4 is the pre-move origin. A new scene (a new actor) starts from its entry placement +0x51c;
            // [not modelled] FUN_00459ff0's later rewrites of +0x51c (script placements).
            if (ledge_serial != mesh_load_serial) {
                ledge_serial = mesh_load_serial;
                ledge = LedgeState{};
                ledge.good = ledge.entry = ledge.safe = {hx0, oy0, hz0};
            }
            if (csMgr.fix_counts[0] + csMgr.fix_counts[1] == 0 &&
                !ledge_exempt(std::atoi(room.name.c_str() + (room.name.size() > 2 ? 2 : 0)), hx0, hz0)) {
                const float f0 = floor_at(phys_mesh(room), hx0, hz0, oy0);
                const Vec3 now{feet.x, oy, feet.z};
                const Vec3 kept = ledge_recover(ledge, true, f0 > kNoFloor && oy0 - f0 <= 510.0f, now,
                                                {feet.x - hx0, oy - oy0, feet.z - hz0}, [&](const Vec3& q) {
                    const float f = floor_at(phys_mesh(room), q.x, q.z, q.y);
                    return f > kNoFloor && q.y - f <= 25.0f; });
                if (kept.x != now.x || kept.y != now.y || kept.z != now.z) {
                    std::printf("  [ledge] t%llu no floor under (%.2f, %.2f, %.2f): back to (%.2f, %.2f, %.2f)\n",
                                (unsigned long long)tick_count, hx0, oy0 - kOriginH, hz0, kept.x, kept.y - kOriginH, kept.z);
                    feet.x = kept.x; feet.z = kept.z; oy = kept.y;
                }
            }
            int floor_tri = -1;
            const float fl = floor_at(phys_mesh(room), feet.x, feet.z, oy, &floor_tri);
            room.coll.climb_reach = 0.0f;
            if (fl > kNoFloor && oy - fl <= 510.0f && pv.grounded) ledge.safe = {feet.x, oy, feet.z};   // +0x3c8 (0x45b99b)
            if (fl > kNoFloor) {
                last_floor = fl;                      // +0x318 persists when the ray misses
                if (oy - fl < 1.6f)                   // surface latch (0x45ba7e: hit t < 1.6)
                    cur_surface = surface_index_or_default(surface_of(phys_mesh(room), floor_tri));
                psand.surf_ray = surface_index_or_default(surface_of(phys_mesh(room), floor_tri));   // +0x4ba
                Vec3 ihit, in_n;                      // the ice slope slide (0x45b053, ice.hpp)
                if (pv.grounded && psand.surf_ray == kSurfaceIce &&
                    segment_hit(phys_mesh(room), {feet.x, oy, feet.z}, {feet.x, oy - 510.0f, feet.z}, &ihit, &in_n)) {
                    const Vec3 ip = ice_slope_push(in_n);
                    player_kb.x += ip.x; player_kb.z += ip.z;
                }
            }
            p_landed = !player_hold && settle_vertical(pv, oy, last_floor, kOriginH, submerged);
            if (submerged && psand.on_sand2() && !pv.grounded) {
                // 0x45bf52 [EXPORT 0045a510.c:912]: in the set over sand2 +0x388 stays 1; a fall that
                // entered it last tick in section 3 lands (the follow-on) [approx: the view's landing].
                pv.grounded = true;
                if (psand.entered && avm.section() == 3) p_landed = true;
            }
            {   // The water test after the vertical step (FUN_0046fba0 0x46fd7c -> FUN_0045c490).
                // Mode 2: submerged throughout. Mode 1: a ray up from feet - 0.25 into the water
                // tree; submerged past a depth of 1.1, wading between 0.35 and 2.0; a change of
                // +0x38a caps the timer at 0.1; a wading change or a surfacing splashes.
                if (splash_cd > 0) --splash_cd;
                psand.entered = psand.buried = false;                 // the word +0x38b = 0 (0x45c58c..)
                if (water_mode == 2) {
                    if (!submerged) { water_transition(pv); submerged = true; }
                    wading = false;
                } else if (phys_mesh(room).has_water()) {
                    const float o0 = feet.y - psand.probe_drop();    // 0x6bd244, 0.7 more over sand2 (0x45c787)
                    const float surf = water_above(phys_mesh(room), feet.x, o0, feet.z);
                    if (surf <= kNoFloor) {
                        submerged = wading = false;                   // no hit: both cleared, no splash
                    } else {
                        psand.water_y = surf;                         // +0x320 (0x45c85b)
                        psand.buried = sand2_buried(psand, surf, o0); // +0x38c (0x45c840)
                        const float depth = surf - o0;
                        const bool sub = depth > 1.1f;                // 0x6bd464
                        bool surfaced = false;
                        if (sub != submerged) {
                            water_transition(pv);
                            surfaced = !sub;
                            submerged = sub;
                            psand.entered = sub;                      // +0x38b (0x45c885)
                        }
                        const bool wade = depth > 0.35f && depth < 2.0f;   // 0x6bd2b0 / 0x6bd580
                        if (wade != wading || surfaced) {
                            wading = wade;
                            if (splash_cd == 0) {                     // 0x45c90d..0x45ca04 (sand2: fx 0x163, SE 0x28)
                                splash_cd = 15;
                                spawn_sim_fx(psand.on_sand2() ? 0x163 : 0x44, {feet.x, surf + 0.15f, feet.z}, {}, 1.0f, {}, 0, {});
                                sfx_at(psand.on_sand2() ? 0x28 : 0x30, feet, kSfxAnimVol);
                            }
                        }
                    }
                } else {
                    submerged = wading = false;
                }
            }
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
            ride_tri = -1;
            if (player_floor_obj == "boss01" && phys_mesh(room).climb_t1 > phys_mesh(room).climb_t0) {
                const RoomMesh& cm = phys_mesh(room);
                const RoomVertex& A = cm.vertices[cm.indices[std::size_t(floor_tri) * 3]];
                const RoomVertex& B = cm.vertices[cm.indices[std::size_t(floor_tri) * 3 + 1]];
                const RoomVertex& C = cm.vertices[cm.indices[std::size_t(floor_tri) * 3 + 2]];
                const Vec3 e1{B.px - A.px, B.py - A.py, B.pz - A.pz}, e2{C.px - A.px, C.py - A.py, C.pz - A.pz};
                const Vec3 n = cross(e1, e2), d{feet.x - A.px, feet.y - A.py, feet.z - A.pz};
                const float nn = n.x * n.x + n.y * n.y + n.z * n.z;
                const float a11 = e1.x * e1.x + e1.y * e1.y + e1.z * e1.z, a22 = e2.x * e2.x + e2.y * e2.y + e2.z * e2.z;
                const float a12 = e1.x * e2.x + e1.y * e2.y + e1.z * e2.z, det = a11 * a22 - a12 * a12;
                if (nn > 1e-12f && det > 1e-12f) {
                    const float d1 = d.x * e1.x + d.y * e1.y + d.z * e1.z, d2 = d.x * e2.x + d.y * e2.y + d.z * e2.z;
                    ride_uvw = {(d1 * a22 - d2 * a12) / det, (d2 * a11 - d1 * a12) / det, (d.x * n.x + d.y * n.y + d.z * n.z) / nn};
                    ride_tri = floor_tri;
                }
            }
            step_meshes();
            // The region camera runs inside the player's update (FUN_00473540 -> FUN_00429450),
            // once per step: after this tick's move (CAMERA_OPS.md §5).
            update_camera(room, feet, kTickDt);

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
            #include "view_revive.inl"                // the revive family's handler (REVIVE.md)
            #include "view_los.inl"                   // CLERIA_LOS: engaged -> phase 1 reports (issue #59)
            {
                const bool eyes_only = false;         // every enemy (view_enemy_tick.inl)
                #include "view_enemy_tick.inl"
            }

            world.step();
            // Age script-placed effects and reap finished one-shots (the enemy warp-in rings).
            world.tick_effects([&](int id) {
                return bullet_art(g, asset_root, efl, eft, id).life;
            });
            // FUN_0045e460, the body push (body_push.hpp): every actor's body in the list's order;
            // each ticking monster runs its pass right after its own integrator, as the handlers do
            // (FUN_0045a510 then FUN_0045e460). The player's own pass pushes nobody, so it is not run.
            // [not modelled] NPC passes (class 4 bodies: no NPC is alive() in the view).
            std::vector<bodypush::Body> bodies;
            std::vector<bodypush::Body*> body_list;
            {
                const Cast* pc = casts.get(13);
                for (Actor& a : world.actors()) {
                    bodypush::Body b;
                    if (a.is_player()) {
                        b.shape = body_shape({feet.x, feet.y + kOriginH, feet.z}, pc ? pc->hurt_radius : 0.45f,
                                             pc ? pc->hurt_half_h : 0.5f, pc ? pc->hurt_shape : 1,
                                             pc ? pc->center_offset : -0.25f);
                        // +0x60 & 0x80: cleared at death and while Godspeed's ramp runs (+0x4d8 < 1).
                        b.kind = std::uint8_t(player.hp >= 1 && gs_ramp >= 1.0f ? 0x81 : 0x01);
                        b.section = avm.section();
                        b.push = bodypush::Push::of(player_kb2);
                    } else {
                        b.shape = body_shape({a.pos.x, a.pos.y + a.height, a.pos.z}, a.hurt_radius, a.hurt_half_h,
                                             a.hurt_shape, a.hurt_off);
                        const bool live = a.alive() && a.ai.hittable && !a.destroy_requested();
                        b.kind = std::uint8_t(live ? (a.kind & 0xff) : (a.kind & 0x7f));
                        b.speed = a.ai.speed310;                     // +0x310
                        for (const EnemyRender& er : render)
                            if (er.actor == &a && er.avm.ok()) { b.section = er.avm.section(); break; }
                        b.token = 0x02000000u | std::uint32_t(a.instance_id);   // [H] +0x80 = actor+0x14
                        if (a.cast_id == 0xb8 && a.ai.phase() != 0) b.mask_clear = 8;   // M980 INIT 0x4d45f0: +0x64 &= ~8
                        // The Piache handlers (M420 0x4c00c0, M450 0x4c11d0): +0x64 bit 0 follows alpha > 0.35 — a faded
                        // one neither pushes nor is pushed by the player. [approx] the dead path keeps the last value there.
                        if ((a.cast_id == 0x2e || a.cast_id == 0x31) && !(a.ai.alpha > 0.35f)) b.mask_clear |= 1;
                        b.bank = &a.bank;
                        b.push = bodypush::Push::of(a.mover.k2);
                    }
                    bodies.push_back(b);
                }
                for (bodypush::Body& b : bodies) body_list.push_back(&b);
            }
            std::size_t body_i = 0;
            for (Actor& a : world.actors()) {
                bodypush::Body& self_body = bodies[body_i++];
                // FUN_0045c490 [BYTES 0x45d371]: a tagged actor's glow phase +1 per tick.
                // [not modelled] +4/+5 in state +0x6f0 == 2 (its meaning for enemies is unknown).
                if (a.tag.tagged()) a.glow_phase = std::uint8_t(a.glow_phase + 1);
                ++a.age_ticks;                          // +0x504, every actor every tick (0x45d32d; M900's ring reads Hugo's)
                // FUN_0045c490's foot dust [BYTES 0x45d228..0x45d2fb]: the cooldown +0x4c2 counts down; else the bit
                // anime_FootSmoke left (+0x3c0 & 1) is taken and, on the ground and out of water, five puffs rise at
                // (x, +0x318 + 0.18, z) — grey-brown, or additive grey on surface 0xa — and the cooldown is 5.
                // [approx] the water bytes +0x389 / +0x38a: the player's wading / submerged, an enemy's in_water;
                // the player's +0x318 is taken as the feet (it is grounded).
                if (a.dust_cd) --a.dust_cd;
                else {
                    AsoVm* fv = a.is_player() ? &avm : nullptr;
                    if (!fv) for (EnemyRender& er : render) if (er.actor == &a) { fv = &er.avm; break; }
                    if (fv && fv->foot_smoke_bit()) {
                        fv->clear_foot_smoke_bit();
                        const bool pl = a.is_player();
                        if ((pl ? pv.grounded : a.vert.grounded) && !(pl ? (wading || submerged) : a.ai.in_water)) {
                            a.dust_cd = 5;
                            const bool s10 = (pl ? cur_surface : a.ai.surf) == 0xa;
                            dust_at("foot", 5, s10 ? 0xa0606060u : 0xc0bda684u, s10,
                                    pl ? Vec3{feet.x, feet.y + 0.18f, feet.z} : Vec3{a.pos.x, a.floor_y + 0.18f, a.pos.z},
                                    0.35f, 0.32f, 0.012f, false);
                        }
                    }
                }
                // The hurt timer +0x238 (FUN_0045a3a0: -0.075 a tick, clamped at 0) runs on the dead
                // too (a duel puppet stays on screen).
                a.hit_flash = hitfx::step_hurt(a.hit_flash);
                if (a.is_player() || (!a.alive() && !revive_corpse(a))) continue;   // a family corpse falls on
                // The integrator FUN_0045a510 (MOVER.md §3): one swept sphere (radius +0x328,
                // centred on the ORIGIN) through D = (displacement, dy), the floor ray from the
                // pre-move origin, then the settle (land with a 0.65 lerp / coyote / fall).
                // A flyer: dy = t (0 for the AI), no settle — the hover owns its altitude.
                // Not a Chr_LocateDummy marker (CASTINFO record 1, CAST_C000: no sprite, it never moves; FUN_00570350
                // places it with FUN_00459ff0 at the script's point): [approx] the sweep's push-out lifted a marker
                // standing inside a closed door's hull onto the door's top (S_5005's (84, -8, 0.35): 6.8 above the
                // floor), out of the action probe's reach, so the Crimson Key door never opened. [H] the engine's
                // step for it has a zero displacement and leaves it where the script put it.
                if (!(a.cast_id == 1 && !a.is_enemy())) {
                    const Vec3 o0{a.pos.x, a.pos.y + a.height, a.pos.z};
                    float dy = a.flyer ? a.vert.t : vertical_dy(a.vert);
                    const float hitY = floor_at(phys_mesh(room), o0.x, o0.z, o0.y);
                    // Sand2 (MOVER.md §3.1/§3.2, 0x45a63f / 0x45a6fa): in the selector-1 set over a
                    // sand2 floor dy is clamped to [-0.015, 0.03]; on sand2 (+0x4b9) xz x0.25 and a
                    // rising dy x0.25; the settle keeps such an actor grounded above its floor.
                    enemy_surfaces(a);
                    if (a.vert.grounded && a.ai.surf != kSurfaceIce) a.mover.ice_tail = 0;   // 0x45d139
                    if (a.vert.grounded && !a.flyer && a.ai.surf_ray == kSurfaceIce && !(a.flags12c & 0x20000000u)) {
                        Vec3 ihit, in_n;                                        // the ice slope slide (0x45b053)
                        if (segment_hit(phys_mesh(room), o0, {o0.x, o0.y - 510.0f, o0.z}, &ihit, &in_n)) {
                            const Vec3 ip = ice_slope_push(in_n);
                            a.mover.k1.x += ip.x; a.mover.k1.y += ip.y; a.mover.k1.z += ip.z;
                        }
                    }
                    const bool in_sand = !a.flyer && a.ai.in_water && a.ai.surf_ray == 0x12;
                    if (in_sand) { dy = std::clamp(dy, -0.015f, 0.03f); a.ai.surf = 0x12; }   // +0x4b9 = 0x12
                    float mdx = a.mover.dx, mdz = a.mover.dz;
                    if (a.ai.surf == 0x12) {      // sand2: 0.25, xz 0.5 with blessing bit 1 (no cast test, 0x45a762)
                        const SurfaceScale ss = surface_scale(0x12, false, (game.g_flags[217] & 2) != 0);
                        mdx *= ss.xz; mdz *= ss.xz; if (dy > 0.0f) dy *= ss.up;
                    }
                    Vec3 o1 = collide_and_slide(phys_mesh(room), o0, {mdx, dy, mdz}, a.radius);
                    if (hitY > kNoFloor && o1.y > hitY) a.floor_y = hitY;     // +0x318
                    if (!a.flyer && in_sand && o1.y - a.height >= a.floor_y + 0.01f) {
                        a.vert.grounded = true;                               // 0x45bf52: +0x388 = 1 in sand
                        a.vert.t = std::max(-0.5f, a.vert.t - 0.0118f);        // the in-water decrement
                    } else if (!a.flyer) {
                        a.vert.jumping = a.mover.loco == 3;                  // the coyote keys on +0x3a0
                        settle_vertical(a.vert, o1.y, a.floor_y, a.height);
                    }
                    a.pos = {o1.x, o1.y - a.height, o1.z};
                }
                self_body.shape.centre = {a.pos.x, a.pos.y + a.height + a.hurt_off, a.pos.z};
                bodypush::pass(self_body, body_list.data(), body_list.size(), character);
            }
            boss6_zava_tail();                           // Zava's handler tail: the ride, the clamps (view_boss06.inl)

            // The enemy's reaction to the player's hit (FUN_004fbb40, src/cleria/hit.hpp).
            // No i-frames and no hit-stop exist for enemies. The knockback impulse `k` is
            // ADDED to +0x338/+0x340 whatever the damage; the flinch (section 4; a re-hit
            // while flinching restarts it) needs dmg >= 1 and is skipped for rtype 3 or a
            // durable victim (anime_Durable ~ +0x9d8).
            auto react_enemy = [&](Actor& en, int dmg, bool died, int rtype, const Impulse& k) {
                en.mover.k1.x += k.x; en.mover.k1.z += k.z;   // knock channel 1 (+0x338/+0x340)
                if (died) return;                       // death is handled by the caller
                for (EnemyRender& er : render) {
                    if (er.actor != &en || !er.avm.ok()) continue;
                    er.hit = true;                      // +0x3bc & 4: wakes an unaware enemy
                    if (dmg >= 1) er.dmg_hit = true;    // +0x3bc & 2: a damaging hit
                    // 0x4fd1d7.. [EXPORT 004fbb40.c:675-683]: a damaging hit while the attacker's kind +0x404 is 2
                    // (the thrust 0x10b) sets 0x2000, and 0x4000 too when the attacker's +0x77c != 0; the action
                    // player rolls the stun on them next tick (30 % / 60 %; enemy_ai.cpp). The attacker is the
                    // player for a bullet or an effect too (LAUNCH_STATE.md §1).
                    // [H] +0x77c is the double-jump count (player_actions dj_count): its meaning here is the spec's
                    // unknown U1.
                    if (dmg >= 1 && skillmp::attack_kind(avm.section()) == 2)
                        er.events |= 0x2000u | (pact.dj_count != 0 ? 0x4000u : 0u);
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
            // One landed hit by an attacker the player owns — the swing's box or a GoLocusSword —
            // on a monster (FUN_004fbb40). `power` / `knock` / `rtype` / `cls` are the attacker
            // record's +0xfc / +0x104 / +0x108 / +0x109; `heading` is the attack direction the guard
            // and the knockback read. Returns true on a kill.
            auto player_hit_enemy = [&](Actor& en, const EnemyRender* vr, float power, float knock, int rtype,
                                        int cls, float heading) {
                    // 0x4fbcb2..0x4fbcd1: the first player-owned locus in the pool holds its frame one
                    // tick (+0x180 += 1). [H] the victim's +0x12c bit 28 is taken as clear; the owner's
                    // own +0x118 += 1 at the same site is [not modelled].
                    if (!loci.empty()) loci.front().sword.freeze();
                    AttackStats a;
                    a.atk = player_stats().atk;
                    a.bonus = power;                 // per-attack power, from this section's script
                    EnemyDefense vd; vd.def = en.def;
                    for (int k = 0; k < 4; ++k) vd.resist[k] = en.resist[k];
                    PlayerDamageMods mods;
                    mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                    mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
                    int dmg = enemy_incoming_damage(a, cls, vd, mods,
                        [&](float pct) { return float(rng_pct(rng)) < pct; });
                    // The guard (FUN_004fbb40): a victim in section 6 facing the swing blocks it.
                    if (vr && vr->avm.ok() &&
                        guard_blocks(en.cast_id, vr->avm.section() == 6 || en.guard399, vr->avm.durable(), rtype,
                                     vr->facing, heading)) {
                        dmg = 0;
                        // 0x4fbfaa..0x4fc0da: the guard's three sparks at the victim's origin (FUN_00589130: delay
                        // step 1, life 35 + 0..25, width 0.11, speed 0.05..0.125, vy 0.05..0.075, any heading).
                        // [not modelled] its yellow tint (+0x268 = 0xffff00, +0x270 = 0.05, +0x274 = 1).
                        sparks_at("guard", 3, 1, 0x19, 0.11f, {en.pos.x, en.pos.y + en.height, en.pos.z}, 0.05f, 0.125f,
                                  0.05f, 0.075f, 0.0f, 6.2831855f);
                    }
                    dmg = access::damage_dealt(dmg, settings().s.access.damage_dealt);       // the shell's Cheats
                    if (cheat_ohko && dmg > 0) dmg = std::max(dmg, int(std::ceil(en.hp)));   // the shell's Cheats
                    HitResult r = apply_damage(en.hp, en.flags, dmg);
                    if (r.died && boss6_zava_undying(en)) { en.flags &= ~kActorDead; r.died = false; }   // her handler floors it (view_boss06.inl)
                    if (dmg > 0) kill_combo.hit();   // 0x4fc835: the combo meter, before the kill's EXP
                    // ... and in the same block, Toal in 0x10c / 0x10f: FUN_0046cc10(500.0) (refused while
                    // boosting, like every add) and the effect 0x9d at the victim, side 0, power 0, knock 1
                    // [BYTES 0x4fc873..0x4fc8a0; EXPORT 004fbb40.c:508-552].
                    if (dmg > 0 && character == 3 && (avm.section() == 0x10c || avm.section() == 0x10f)) {
                        add_boost(500.0f);
                        ef2sim::SpawnParams sp;
                        sp.side = 0; sp.power = 0.0f; sp.knock = 1.0f; sp.cls = -1; sp.owner = 13;
                        spawn_sim_fx(0x9d, {en.pos.x, en.pos.y + en.height, en.pos.z}, {}, 1.0f, sp, 0, {});
                    }
                    {   // FUN_004fbb40's own feedback (hit_feedback.hpp), at the victim's origin.
                        const Vec3 org{en.pos.x, en.pos.y + en.height, en.pos.z};
                        // The tail 0x4fd241: FUN_004fae00(0xffffffff), the white damage number (a GoPopup,
                        // scale 1.0, origin + 0.5 up; "0" for a guarded hit), every path included.
                        // PLAYTEST_1_HUD.md item 3.
                        damage_popup(hudstate::dealt_popup_value(dmg, cls, game.g_flags), 0xffffff, org);
                        if (dmg < 1) {
                            // 0x4fd1b5: a guarded / zero hit: the SE, then four sparks thrown back within a quarter
                            // turn of the hit angle (FUN_00589130: delay step 2, life 35 + 0..10, width 0.15, speed
                            // 0.15..0.2, vy -0.05..0.1) [BYTES 0x4fd1cf..0x4fd239].
                            sfx_at(hitfx::no_damage_se(en.cast_id), org, kSfxAnimVol);
                            sparks_at("zero hit", 4, 2, 0xa, 0.15f, org, 0.15f, 0.2f, -0.05f, 0.1f,
                                      heading - 1.5707964f, heading + 1.5707964f);
                        } else if (const int fx = hitfx::skill_hit_effect(cls, character); fx >= 0) {
                            // 0x4fd020: the damage class's elemental hit effect (FUN_00450410).
                            ef2sim::SpawnParams sp;
                            sp.side = 0; sp.power = 0.0f; sp.knock = 1.0f; sp.cls = cls; sp.owner = 13;
                            const float vf = vr ? vr->facing : heading_of_yaw(en.yaw);
                            spawn_sim_fx(fx, org, {0.0f, hitfx::skill_hit_yaw(vf), 0.0f}, 1.0f, sp, 0, {});
                            if (sfx_log) std::printf("  [hitfx] t%llu class %d hit effect 0x%x\n", (unsigned long long)tick_count, cls, fx);
                        }
                        // 0x4fd10e..0x4fd167 [BYTES], every hit for 1 or more: FUN_004fab40(victim, colour) = five dust
                        // puffs (20 draws) at the origin - (cos, sin)(the hit angle) x the hurt radius +0x68, in the
                        // cast's ENEMYINFO +0xe0 colour (0 = invisible, the draws are still taken; 0xff7c0000 for a
                        // cast outside the monster ranges), layer 9 when the victim is in water.
                        if (dmg >= 1) {
                            const Enemy* hr = etab.get(en.cast_id);
                            const bool mon = (en.cast_id >= 0x0e && en.cast_id <= 0x53) || (en.cast_id >= 0xa6 && en.cast_id <= 0xb0);
                            std::uint32_t col = mon && hr ? hr->blood : 0xff7c0000u;
                            if (col && !(col & 0xff000000u)) col |= 0xff000000u;
                            const int hi = int(heading * 40.743664f);
                            dust_at("hit", 5, col, false,
                                    {org.x - cos_table(hi & 0xff) * en.hurt_radius, org.y, org.z - cos_table((hi - 0x40) & 0xff) * en.hurt_radius},
                                    0.3f, 0.45f, 0.025f, en.ai.in_water);
                        }
                    }
                    // +300 per hit dealt (0x4fd0bf, DAT_006bdc44) when the kind is not 0 / 5 (skills).
                    if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                    // Knockback: L = walk speed (CASTINFO+0x48) x1.7 [x run mult while running —
                    // the view has no run state yet] x the swing's arg[12], driven along the
                    // player's facing (hit.hpp player_knock_angle).
                    const float kang = player_knock_angle(heading,
                                                          feet.x, feet.z, feet.y, en.pos.x, en.pos.z, en.pos.y);
                    react_enemy(en, dmg, r.died, rtype,
                                knock_impulse(kang, player_knock_scale(pm.speed_base, false, 2.2f) * knock));
                    const std::string nm = casts.get(en.cast_id) && !casts.get(en.cast_id)->display.empty()
                                           ? casts.get(en.cast_id)->display : ("cast " + std::to_string(en.cast_id));
                    hud.debug_message(r.died ? (nm + " defeated!")
                                       : (nm + "  -" + std::to_string(dmg) + " (" +
                                          std::to_string(int(en.hp)) + "/" +
                                          std::to_string(int(en.max_hp)) + ")"));
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  hit %s (lv%d DEF %.0f): %d dmg, %.0f/%.0f left\n",
                                    nm.c_str(), en.level, en.def, r.damage, en.hp, en.max_hp);
                    if (r.died) {
                        // The kill credit (0x4fcaf5..0x4fcd42): nothing once +0x12c & 0x4000000 is set,
                        // which this kill sets (a revived actor keeps it: REVIVE.md §3.10c).
                        award_exp(en.exp_reward, en.level, /*boss*/false, (en.flags12c & 0x4000000u) != 0);
                        // The kill SP in the same block [BYTES 0x4fcd1d..0x4fcd3d]: the victim's +0xc4 through
                        // FUN_004fa390 into the wallet (FUN_004fa290), when it is not 0.
                        if (!(en.flags12c & 0x4000000u)) credit_sp(en.sp_reward);
                        en.flags12c |= 0x4000000u;
                        // The revive family flips +0x130 ^= 9 on the first dead tick when the Chimes are
                        // on, before FUN_004780e0's death effects and drops (REVIVE.md §3.1).
                        const std::uint32_t f130 = en.flags130 ^
                            ((revive::row(en.ai_index) && (inv.equip_flags & 0x8000u)) ? 9u : 0u);
                        {   // FUN_004780e0 -> FUN_00478510's death path (hit_feedback.hpp): its entry
                            // draw (the scale), the puff (+ SE 862 with +0x130 & 0x2000) and the
                            // character's spark, from the victim's last vector to the player.
                            const std::uint32_t d0 = pick_rng.next();
                            en.hit_flash = hitfx::kHurtTimer;            // +0x238 = 1.65 (whatever +0x130)
                            const hitfx::DeathPuff dp = hitfx::death_puff(f130);
                            if (dp.spark) {
                                const Vec3 tp = en.ai.v(0x734);
                                const hitfx::Spark sk = hitfx::place_spark({en.pos.x, en.pos.y + en.height, en.pos.z},
                                    cam.forward(), tp.x, tp.z, hitfx::spark_scale(d0, hitfx::kDeathLo, hitfx::kDeathHi));
                                ef2sim::SpawnParams sp; sp.side = 0;
                                if (dp.effect >= 0) spawn_sim_fx(dp.effect, sk.pos, sk.rot, sk.scale, sp, 0, {});
                                if (dp.se >= 0) sfx_at(dp.se, sk.pos, kSfxAnimVol);
                                const int sid = hitfx::hit_spark_effect(character, game.g_flags[149], false, 0);
                                if (sid >= 0) spawn_sim_fx(sid, sk.pos, sk.rot, sk.scale, sp, 0, {});
                                if (sfx_log) std::printf("  [hitfx] t%llu death puff 0x%x spark 0x%x x%.2f\n",
                                                         (unsigned long long)tick_count, dp.effect, sid, sk.scale);
                            }
                        }
                        // FUN_00478510's drop loop is skipped while +0x130 & 1 (0x478b5c; factory 0x2009 / 0x6081 / 0x1);
                        // casts 0xaf (M890) / 0xb0 (M900) branch away before it (0x478b39..0x478b49 -> 0x479348: no drops).
                        if (const Enemy* er = etab.get(en.cast_id); er && !(f130 & 1u) && en.cast_id != 0xaf && en.cast_id != 0xb0) {
                            DropContext dc;                 // [not modelled] launch count, luck
                            dc.combo = kill_combo.count;    // 0x76c1a8 (FUN_00478510 0x479062)
                            dc.flag108 = game.g_flags[108] == 1;
                            dc.bless8 = (game.g_flags[217] & 8) != 0;
                            dc.no_drops = game.g_flags[146] != 0;
                            dc.boss_lock = game.g_flags[156] != 0;
                            dc.rate = settings().s.access.drop_rate;   // the shell's Cheats
                            // The drop position is enemy+0x50..+0x58 exactly, its ORIGIN (0x4785b5), not its feet.
                            roll_enemy_drops(pick_rng, *er, en.cast_id, {en.pos.x, en.pos.y + en.height, en.pos.z}, dc,
                                             pickups, /*entry_draw*/ false);
                        }
                        // FUN_00478510's death path, after the drops (0x479348..0x479390; reached by the casts that
                        // skip the drops too): three blood splats at the origin (18 draws) when the cast has a colour.
                        if (const Enemy* br = etab.get(en.cast_id);
                            br && br->blood != 0 && !(en.flags12c & 0x80000000u) && settings().s.game.bloody_effect_level > 0)
                            blood_at("death", 3, br->blood | 0xff000000u, 2, 0x18, 1.3f, 1.8f,
                                     {en.pos.x, en.pos.y + en.height, en.pos.z}, 0.65f, false, false);
                        // The death section's GenerateDebris (spawn_debris). M360's corpse and a familiar run their
                        // section 5 live and issue it there.
                        if (en.ai_index != 29 && !hugo_eye(en))
                            if (const DeathDebris* dd = enemy_death_debris(asset_root, casts.sprite_stem(en.cast_id))) {
                                const Vec3 org{en.pos.x, en.pos.y + en.height, en.pos.z};
                                if (dd->tick == 0) spawn_debris(en.cast_id, org, dd->ev);
                                else pending_debris.push_back({dd->tick, en.cast_id, org, &dd->ev});
                            }
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
                    return r.died;
            };
            // The mode-0 scan's breakables pass for one attacker volume `vol` (keyed on `tok`, re-hit
            // `rehit`); returns the number of @Break_* objects smashed.
            int mesh_struck = 0;                 // 0x186 child spheres struck (the particle pass reads it)
            auto smash_breakables = [&](const HitShape& vol, std::uint32_t tok, int rehit, bool one_shot,
                                        float power, int cls) {
                const Vec3 c = vol.centre;
                int broke = 0;
                // BREAKABLES (SOB trigger class 1 with a script): the same swing volume also
                // tests the room's attackable scene objects. @Break_* is a one-hit break —
                // no SOB field holds HP, and Mesh_SetHp is called by none of the shipped
                // scripts — then it plays SE 114, rolls each of its two $I drop slots and
                // fades the mesh out. The roll rate is the script's own: @Break_5 20%,
                // @Break_30 50%, @Break_100 always.
                for (const SceneTrigger& t : room.mesh.triggers) {
                    if (!(t.trigger & 1) || broken_objects.count(room.name + "/" + t.name)) continue;
                    std::string lname = t.name;
                    for (char& ch : lname) ch = char(std::tolower((unsigned char)ch));
                    if (auto hm = hit_meshes.find(lname); hm != hit_meshes.end()) {
                        // FUN_004fed50's GoMesh pass [EXPORT 004fed50 l.297-340]: the swing vs the 0x186
                        // child sphere (at the mesh + offset); FUN_004faf20(0, 0) on the child: its re-hit
                        // bank (FUN_004faac0), then FUN_004fa550 with the attacker's class +0x109 against the
                        // child's copied block (the SOB record's DEF, resist bits and no-floor byte) into
                        // child +0x134 (0x4fb489); the mesh's +0x98 -= it (0x4ff6b1), and below 1.0 (0x4ff6c9) the
                        // mesh's script runs (FUN_00434970); the object STAYS (FLOOR3 B1). The torches
                        // (flags 0x6b0) take only class 1 / 5 (the fire skill), the walls and S_3103's pole
                        // (0x670) only class 2 / 6 (thunder); 0x400 drops the min-1 chip, so a melee or
                        // wind hit deals 0 (#53).
                        HitMesh& h = hm->second;
                        if (h.ran) continue;
                        const MeshObj* mo = meshes.find(lname);
                        const Vec3 at = mo ? mo->pos : t.position;
                        const HitShape body = body_shape({at.x + h.off.x, at.y + h.off.y, at.z + h.off.z}, h.r, 0.0f, 0, 0.0f);
                        if (!overlap(vol, body)) continue;
                        if (h.bank.check_and_record(tok, rehit)) continue;
                        if (one_shot) atk_window = 0;
                        ++mesh_struck;                                 // a landed hit (FUN_004fed50's return)
                        {
                            AttackStats a;
                            a.atk = player_stats().atk;
                            a.bonus = power;
                            EnemyDefense vd;                             // the child: the SOB battle block
                            vd.def = t.def;
                            for (int k = 0; k < 4; ++k) vd.resist[k] = t.resist[k];
                            vd.no_floor = t.no_floor;
                            if (!h.hp_set) { h.hp = t.hp; h.hp_set = true; }   // the mesh's +0x98
                            PlayerDamageMods mods;
                            mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                            mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;   // FUN_004faf20 uVar9
                            h.hp -= float(enemy_incoming_damage(a, cls, vd, mods,
                                [&](float pct) { return float(rng_pct(rng)) < pct; }));
                        }
                        if (!(h.hp < 1.0f)) continue;                    // +0x98 <= 1.0 && != 1.0
                        h.ran = true;
                        std::string stem = t.script_stem;
                        for (char& ch : stem) ch = char(std::tolower((unsigned char)ch));
                        XsoVm::Vars ov;
                        ov.s[0] = t.name; ov.i[0] = t.i0; ov.i[1] = t.i1; ov.f[0] = t.f0;
                        if (csMgr.start(stem, ov, /*owner: the player*/ 13))
                            std::printf("  [hit] %s -> %s\n", t.name.c_str(), stem.c_str());
                        continue;
                    }
                    // Only the @Break_* family is the smash; any other trigger-1 script waits for
                    // its 0x186 (the root sets it on every visit).
                    if (t.script_stem.empty() || t.script_stem[0] != '@') continue;
                    // Sphere vs the object's world AABB.
                    const float qx = std::max(t.aabb_min.x, std::min(c.x, t.aabb_max.x));
                    const float qy = std::max(t.aabb_min.y, std::min(c.y, t.aabb_max.y));
                    const float qz = std::max(t.aabb_min.z, std::min(c.z, t.aabb_max.z));
                    const float ddx = c.x - qx, ddy = c.y - qy, ddz = c.z - qz;
                    if (ddx * ddx + ddy * ddy + ddz * ddz > vol.radius * vol.radius) continue;
                    broken_objects.insert(room.name + "/" + t.name);
                    {   // The object's own @Break_* script (FUN_00434970 on the mesh; FLOOR13_OPS.md):
                        // $S0 = its name, $I0/$I1 = its SOB drop ids. It poses OPEN, plays SE 114 (under
                        // water SE 779 + three effects 61 on the mesh), loads each drop id with Reg_SetInt
                        // and rolls it (0x81, then 0x17e Mesh_DropItem), then turns the collision off
                        // (the room rebuild drops the mesh), fades and hides it. AllowMultiInstance lets
                        // several pots break at once. The stand-in below runs only when it cannot start.
                        std::string bstem = t.script_stem;
                        for (char& ch : bstem) ch = char(std::tolower((unsigned char)ch));
                        XsoVm::Vars bv;
                        bv.s[0] = t.name; bv.i[0] = t.i0; bv.i[1] = t.i1; bv.f[0] = t.f0;
                        if (events_enabled && csMgr.start(bstem, bv, /*owner: the player*/ 13)) {
                            std::printf("  [hit] %s -> %s\n", t.name.c_str(), bstem.c_str());
                            continue;
                        }
                    }
                    sfx_at(114, t.position, kSfxAnimVol);      // the shatter SE
                    int pct = 50;                               // @Break_30
                    if (t.script_stem.find("_5") != std::string::npos) pct = 20;
                    if (t.script_stem.find("_100") != std::string::npos) pct = 100;
                    // The script's guard + 0x81 roll (one game-RNG draw: (xs() >> 17) % 100 < p),
                    // then 0x17e Mesh_DropItem($S0, $Ix, 1): a kept world pickup (PICKUPS.md §13.7).
                    // [STAND-IN] only when the script did not start (CLERIA_EVENTS=0 / missing).
                    for (int slot : {t.i0, t.i1}) {
                        if (slot == 0) continue;
                        if (pct < 100 && int(pick_rng.next() >> 17) % 100 >= pct) continue;
                        drop_at(t.position, slot, true);
                    }
                    ++broke;
                }
                return broke;
            };
            int killed = 0, broke = 0;
            if (atk_window > 0) {
                --atk_window;
                // FUN_004fed50 mode 0 (build/ai_spec/HITVOLUME.md §2): the volume is placed
                // from the player's ORIGIN and current facing every tick, then tested against
                // each monster's hurt body; the victim's bank keys on this attack's serial.
                const float pface = std::atan2(face_dir.z, face_dir.x);
                const HitShape vol = volume_shape(atk_hb, {feet.x, feet.y + kOriginH, feet.z}, pface);
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
                    if (player_hit_enemy(en, vr, atk_hb.power, atk_hb.knock, atk_hb.rtype,
                                         atk_cls, std::atan2(atk_dir.z, atk_dir.x)))
                        ++killed;
                }
                broke += smash_breakables(vol, player_hit_serial, atk_hb.rehit, atk_hb.one_shot, atk_hb.power, atk_cls);
                // FUN_004fed50's GoMesh pass (VELAGUNDER.md §3.3): each boss part's hurt sphere,
                // its own re-hit bank, the damage with the part's DEF / resist into part +0x134.
                // [not modelled] the armour's skipped elemental bonus, Toal's refused sections.
                if (boss && !boss->destroyed())
                    for (int pi = 0; pi < boss01::kParts; ++pi) {
                        if (!boss->part_hittable(pi)) continue;
                        const Boss01Part& bp = boss->part(pi);
                        const HitShape body = body_shape(bp.hurt_centre, bp.hurt_radius, 0.0f, 0, 0.0f);
                        if (!overlap(vol, body)) continue;
                        if (boss_banks[std::size_t(pi)].check_and_record(player_hit_serial, atk_hb.rehit)) continue;
                        if (atk_hb.one_shot) atk_window = 0;
                        AttackStats a;
                        a.atk = player_stats().atk;
                        a.bonus = atk_hb.power;
                        EnemyDefense vd; vd.def = bp.def;
                        for (int k = 0; k < 4; ++k) vd.resist[k] = bp.resist[k];
                        PlayerDamageMods mods;
                        mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
                        const int dmg = access::damage_dealt(enemy_incoming_damage(a, atk_cls, vd, mods,
                            [&](float pct) { return float(rng_pct(rng)) < pct; }), settings().s.access.damage_dealt);   // the shell's Cheats
                        if (dmg > 0) boss->add_part_damage(pi, float(dmg));
                        boss_part_feedback(bp, dmg, atk_cls);
                        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  hit boss part %d (DEF %.0f): %d dmg, HP %.0f stun %.0f\n",
                                        pi, bp.def, dmg, boss->hp(), boss->stun_gauge());
                    }
                // Nygtilger (NYGTILGER.md §3.1 / §3.2). The mesh path FUN_004fecb0: [approx] one
                // ray from the player's origin through the swing volume to its far side (the engine
                // casts one per attack sub-volume) against each struck-able piece's collision hull;
                // a struck piece asks its accept-hit callback, then its re-hit bank, then takes
                // FUN_004faf20(1, 0) with its own DEF / resist. A segment's one-way entries (+0x8c)
                // are skipped [approx] per object, while both are off (MeshObj::no_collide).
                if (boss2) {
                    const RoomMesh& b2m = phys_mesh(room);
                    const Vec3 O{feet.x, feet.y + kOriginH, feet.z};
                    Vec3 d{vol.centre.x - O.x, vol.centre.y - O.y, vol.centre.z - O.z};
                    const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                    if (dl > 1e-4f) { d.x /= dl; d.y /= dl; d.z /= dl; }
                    const Vec3 E{vol.centre.x + d.x * vol.radius, vol.centre.y + d.y * vol.radius,
                                 vol.centre.z + d.z * vol.radius};
                    auto b2_damage = [&](float def, const float* resist) {
                        AttackStats a;
                        a.atk = player_stats().atk;
                        a.bonus = atk_hb.power;
                        EnemyDefense vd; vd.def = def;
                        for (int k = 0; k < 4; ++k) vd.resist[k] = resist[k];
                        PlayerDamageMods mods;
                        mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
                        return access::damage_dealt(enemy_incoming_damage(a, atk_cls, vd, mods,
                            [&](float pct) { return float(rng_pct(rng)) < pct; }), settings().s.access.damage_dealt);   // the shell's Cheats
                    };
                    for (int o = 0; o < boss02::kObjects && atk_window > 0; ++o) {
                        if (!boss2->mesh_hittable(o)) continue;
                        const char* nm = boss02::object_name(o);
                        const MeshObj* mo = meshes.find(nm);
                        const RoomMesh::ObjRange* rr = b2m.object_range(nm);
                        if (!mo || !rr || mo->no_collide) continue;
                        Vec3 hit{};
                        if (!segment_hits_tris(b2m, O, E, rr->t0, rr->t1, &hit)) continue;
                        Boss02Output fb;
                        // The throttle's clock is real time; a (lockstep) drive run reads the tick clock so
                        // the run does not depend on the machine's speed.
                        const std::uint32_t now_ms = lockstep ? std::uint32_t(tick_count * 1000 / 60)
                                                              : std::uint32_t(SDL_GetTicks());
                        // ... and the accept-hit's own splat (0x513364: 1 of 0xffc000c0, size 1.95..2.7) on a cracked
                        // piece, with the option byte.
                        const bool ok = boss2->accept_hit(o, hit, now_ms, fb, [&](const Boss02Cosmetic& c) {
                            if (c.kind == Boss02Cosmetic::kScreenDebris)
                                blood_at("boss02 hit", 1, 0xffc000c0u, 2, 0x18, 1.95f, 2.7f, c.pos, 0.65f, false, false);
                        }, settings().s.game.bloody_effect_level > 0);
                        boss2_apply(fb);
                        if (!ok) continue;
                        if (boss2_banks[std::size_t(o)].check_and_record(player_hit_serial, atk_hb.rehit)) continue;
                        if (atk_hb.one_shot) atk_window = 0;
                        const Boss02Piece& bp = boss2->object(o);
                        const int dmg = b2_damage(bp.def, bp.resist);
                        // [H] FUN_004faf20 skips a victim already below 1 HP: the dead tail stays
                        // struck-able (its entries are re-armed every frame, §3.5), and without
                        // this gate each further hit would "kill" it again (pool -350 a time).
                        if (dmg > 0 && bp.hp >= 1.0f) boss2->add_damage(o, float(dmg));
                        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  hit %s (DEF %.0f): %d dmg, HP %.0f pool %.0f\n", nm, bp.def, dmg,
                                        bp.hp, boss2->pool());
                    }
                    // The head / tail kind-9 parts: a sphere of r 0.85 (§3.1), their own banks.
                    for (int k = 0; k < 2 && atk_window > 0; ++k) {
                        const int o = k == 0 ? 0 : boss02::kPieces - 1;
                        if (!boss2->part_hittable(o)) continue;
                        const Boss02Part& pt = boss2->object(o).part;
                        if (!overlap(vol, body_shape(pt.pos, pt.radius, 0.0f, 0, 0.0f))) continue;
                        if (boss2_part_banks[std::size_t(k)].check_and_record(player_hit_serial, atk_hb.rehit)) continue;
                        if (atk_hb.one_shot) atk_window = 0;
                        const int dmg = b2_damage(pt.def, pt.resist);
                        if (dmg > 0) boss2->add_part_damage(o, float(dmg));
                        part_hit_feedback(pt.pos, true, dmg, atk_cls);
                        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  hit %s part (DEF %.0f): %d dmg, part HP %.0f pool %.0f\n",
                                        boss02::object_name(o), pt.def, dmg, pt.hp, boss2->pool());
                    }
                }
                // Gelaldy's five GoXx parts (GELALDY.md §2.3 / §3): the hurt sphere (a guarded part's
                // centre sits at y -1000), the part's own re-hit bank, FUN_004faf20 with the part's
                // DEF / resist into +0x134 (and the part HP: the arms are counted twice, G9).
                if (boss3 && !boss3->destroyed())
                    for (int pi = 0; pi < boss03::kParts && atk_window > 0; ++pi) {
                        if (!boss3->part_hittable(pi)) continue;
                        const Boss03Part& bp = boss3->part(pi);
                        if (!overlap(vol, body_shape(bp.hurt_centre, bp.hurt_radius, 0.0f, 0, 0.0f))) continue;
                        if (boss3_banks[std::size_t(pi)].check_and_record(player_hit_serial, atk_hb.rehit)) continue;
                        if (atk_hb.one_shot) atk_window = 0;
                        AttackStats a;
                        a.atk = player_stats().atk;
                        a.bonus = atk_hb.power;
                        EnemyDefense vd; vd.def = bp.def;
                        for (int k = 0; k < 4; ++k) vd.resist[k] = bp.resist[k];
                        PlayerDamageMods mods;
                        mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                        mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
                        const int dmg = access::damage_dealt(enemy_incoming_damage(a, atk_cls, vd, mods,
                            [&](float pct) { return float(rng_pct(rng)) < pct; }), settings().s.access.damage_dealt);   // the shell's Cheats
                        if (dmg > 0) boss3->add_part_damage(pi, float(dmg));   // [not modelled] the element bonus on HP
                        part_hit_feedback(bp.pos, true, dmg, atk_cls);
                        if (skillmp::boost_hit_counts(skillmp::attack_kind(avm.section()))) add_boost(300.0f);
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  hit gelaldy part %d (DEF %.0f): %d dmg, part HP %.0f body %.0f\n",
                                        pi, bp.def, dmg, bp.hp, boss3->hp());
                    }
                #include "view_boss04_hit.inl"     // Khonsclard's parts (KHONSCLARD.md §A.2.3)
                #include "view_boss05_hit.inl"     // Pictimos's parts (PICTIMOS.md §P1.4.1)
                #include "view_boss06_hit.inl"     // Zava's heads: the accept-hit callback (ZAVA_D.md §7.1)
                #include "view_boss07_hit.inl"     // Dalles's parts (DALLES_AB.md §B.2)
                #include "view_boss08_hit.inl"     // Darm's parts (DARM_D.md §4.3)
            }
            // GoLocusSword (src/cleria/locus.hpp, build/ability_re/LOCUS.md): the player's GenerateLocus
            // sweeps. FUN_004281c0 updates the locus pool after the player and the actors; a locus with
            // hit life left is scanned once a tick by the same mode-0 path as the swing (FUN_004fed50,
            // origin NULL: its three r 0.45 spheres as placed), keyed on the swing's token.
            // [not modelled] the boss parts, the wall clank (args [4],[5]: FUN_00500b30).
            if (!loci.empty()) {
                LocusSword::Owner lo;
                lo.origin = {feet.x, feet.y + kOriginH, feet.z};   // +0x50
                lo.floor_off = kOriginH;                             // +0x324
                lo.facing = std::atan2(face_dir.z, face_dir.x);      // +0x314
                lo.dead = player.hp < 1;                             // +0x98 < 1.0
                for (std::size_t li = 0; li < loci.size(); ++li) {
                    if (!loci[li].sword.tick(lo) || !loci[li].sword.armed()) continue;
                    const std::array<HitShape, 3> sp = loci[li].sword.spheres();
                    const std::uint32_t tok = loci[li].sword.token();
                    const int rehit = loci[li].sword.rehit(), cls = loci[li].cls;
                    const float bonus = loci[li].sword.bonus(), knock = loci[li].sword.knock();
                    for (Actor& en : world.actors()) {
                        if (en.is_player() || !en.alive() || en.destroy_requested()) continue;
                        const EnemyRender* vr = nullptr;
                        for (const EnemyRender& er : render) if (er.actor == &en) { vr = &er; break; }
                        if (vr && !vr->actor->ai.hittable) continue;
                        const HitShape body = body_shape({en.pos.x, en.pos.y + en.height, en.pos.z},
                                                         en.hurt_radius, en.hurt_half_h, en.hurt_shape, en.hurt_off);
                        if (!overlap(sp[0], body) && !overlap(sp[1], body) && !overlap(sp[2], body)) continue;
                        if (en.bank.check_and_record(tok, rehit)) continue;          // FUN_004faac0
                        if (std::getenv("CLERIA_DMG"))
                            std::printf("  [locus] frame %.2f tip (%.2f, %.2f) reach %.2f\n", loci[li].sword.frame(),
                                        loci[li].sword.tip().x, loci[li].sword.tip().z,
                                        std::sqrt((en.pos.x - feet.x) * (en.pos.x - feet.x) + (en.pos.z - feet.z) * (en.pos.z - feet.z)));
                        if (player_hit_enemy(en, vr, bonus, knock, /*rtype +0x108*/ 0, cls, lo.facing)) ++killed;
                    }
                    for (const HitShape& h : sp) broke += smash_breakables(h, tok, rehit, false, bonus, cls);
                }
                loci.erase(std::remove_if(loci.begin(), loci.end(),
                                          [](const PlayerLocus& l) { return !l.sword.alive(); }),
                           loci.end());
            }
            if (broke) reload_room(/*rerun_events*/false);   // drop the shattered mesh
            if (killed) {
                int left = 0;
                for (const Actor& en : world.actors())
                    if (!en.is_player() && en.alive()) ++left;
                std::printf("  defeated %d (%d left)\n", killed, left);
            }

            // Enemies hitting the player. Damage is the REAL player-incoming formula
            // (combat.hpp player_incoming_damage = FUN_004fa740), gated by the player's
            // i-frames exactly as the engine does (one hit per window, however many
            // enemies are on you). Damage now comes from the enemy's own ASO attack — its
            // GenerateCollision box, armed on the swing frame in the brain above — not from
            // touching it. player_def is the lv1 curve; no equipment model.
            // The countdown is frozen while a hurt section (4/9/0xb) plays (0x466cc1).
            tick_iframes(player_iframes, avm.section());
            player_spec.tick();                                     // FUN_0045a3a0 (the integrator's helper)
            kill_combo.tick();                                      // 0x466b5d: the combo meter's timer
            {   // The standing HP regeneration, next in the mover (0x466b7f..0x466c77; src/cleria/hazard.hpp).
                StandingRegen::In ri;
                ri.boss_battle = game.g_flags[156] != 0;
                ri.acc_regen = (inv.equip_flags & 0x400u) != 0;
                ri.flag209 = game.g_flags[209] != 0;
                ri.stick = moving;
                ri.section = avm.section();
                ri.difficulty = game.g_flags[144];
                ri.blessing1 = (game.g_flags[217] & 1) != 0;
                ri.hp = float(player.hp); ri.max_hp = float(player.max_hp);
                const float add = regen.tick(ri);
                if (add > 0.0f) {
                    regen_acc += add;                                // [approx] the view's HP is an int
                    const int n = int(regen_acc);
                    regen_acc -= float(n);
                    player.hp = std::min(player.max_hp, player.hp + n);   // FUN_004fa160 clamps; [not modelled] DAT_0076b914 |= 4
                }
            }
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
                        // The authored name ('@' = S_COMMON: @Jump_001, @Fall_001, @Touch_Toge).
                        std::string stem = t.script_stem;
                        for (char& ch : stem) ch = char(std::tolower((unsigned char)ch));
                        // The object's script reads its own name as $S0 and its SOB values
                        // as $I0/$I1/$F0 (audit event_vm #11; TURNSWITCH1 / S20SWICH00_FUMI
                        // pose and wait on `$S0`).
                        XsoVm::Vars ov;
                        ov.s[0] = t.name; ov.i[0] = t.i0; ov.i[1] = t.i1; ov.f[0] = t.f0;
                        if (csMgr.start(stem, ov, /*owner: the player*/ 13))
                            std::printf("  [plate] %s -> %s\n", t.name.c_str(), stem.c_str());
                    }
                }
            #include "view_xso13_touch.inl"          // SOB trigger 4 side contacts (FLOOR13_OPS.md)
            {   // FUN_00462550: lava / blood floors and the S_3009 noise drain (src/cleria/hazard.hpp).
                SurfaceHazard::In hz;
                hz.surface = cur_surface;
                hz.noise = game.g_flags[290] != 0;
                hz.iframes = player_iframes > 0;
                hz.grounded = grounded;
                hz.submerged = submerged;
                hz.character = character;
                hz.boosting = player.boosting;
                hz.difficulty = difficulty >= 2 ? difficulty - 2 : 0;
                hz.easy = game.g_flags[143] != 0;
                hz.blessing2 = (game.g_flags[217] & 2) != 0;
                hz.acc_lava = (inv.equip_flags & 0x2000u) != 0;
                hz.acc_blood = (inv.equip_flags & 0x4000000u) != 0;
                hz.max_hp = float(player.max_hp);
                const float hd = hazard.tick(hz);
                if (std::getenv("CLERIA_HAZ") && tick_count % 50 == 0) std::printf("  [haz] surf %d noise %d if %d g %d t %u hd %.2f\n", hz.surface, int(hz.noise), int(hz.iframes), int(hz.grounded), unsigned(hazard.t), hd);
                if (hd > 0.0f && player.hp > 0) {
                    hazard_acc += hd;                           // [approx] the view's HP is an int
                    const int n = int(hazard_acc);
                    hazard_acc -= float(n);
                    player.hp = std::max(0, player.hp - n);
                    // FUN_004fae00(0xffa000a0): the damage popup.
                    if (n > 0 && popups.size() < 32)
                        popups.push_back(make_popup(std::to_string(n), 0xa000a0, 1.0f, {feet.x, feet.y + kOriginH + 0.5f, feet.z}));
                }
            }
            // FUN_00462550: holding the powered Evil Ring (held 0x5e with count[93] == 1) is -9999 HP
            // every tick unless the Blue Necklace (equip bit 0x20000) is on (KEY_ITEMS.md §3.6).
            // [not modelled] its 9999 popup and effect.
            if (inv.held == 0x5e && game.g_flags[93] == 1 && player.hp >= 1) {
                (void)player_stats();                         // refreshes inv.equip_flags
                if (!(inv.equip_flags & 0x20000u)) player.hp = std::max(0, player.hp - 9999);
            }
            for (PendingDebris& pd : pending_debris)
                if (--pd.ticks == 0) spawn_debris(pd.cast, pd.origin, *pd.ev);
            pending_debris.erase(std::remove_if(pending_debris.begin(), pending_debris.end(),
                                                [](const PendingDebris& pd) { return pd.ticks <= 0; }), pending_debris.end());
            // World pickups: motion, bounce, collect (FUN_00584f90 / FUN_005856c0 / FUN_00585ff0).
            if (!pickups.empty()) {
                PickupWorld pw;
                pw.live = int(pickups.size());
                pw.hit = [&](const Vec3& a, const Vec3& b, Vec3* pt, Vec3* n) {
                    return segment_hit(phys_mesh(room), a, b, pt, n);
                };
                pw.player = {feet.x, feet.y + kOriginH, feet.z};       // P+0x50, the origin (0x58606d..0x586080)
                // The player's first sword trail (pool 4's first object with +0x120 == the player [H: `loci` keeps
                // the pool's order]): its tip sphere, as placed by its last armed tick (a trail not yet placed has
                // none: radius 0).
                for (const PlayerLocus& pl : loci)
                    if (pl.sword.alive()) {
                        pw.sword = pl.sword.spheres()[0].radius > 0.0f;
                        pw.sword_tip = pl.sword.spheres()[0].centre;
                        break;
                    }
                pw.sparkle = pickup_sparkle;              // view_weather.inl
                pw.stain = debris_stain;                  // view_weather.inl
                pw.touch = [&](Pickup& pk) {
                    if (fly || player.hp <= 0) return false;
                    float hp = float(player.hp);
                    EffectEnv env;
                    env.flags = game.g_flags; env.hp = &hp; env.max_hp = float(player.max_hp);
                    env.player_alive = player.hp > 0; env.boost = &player.boost; env.oxygen = &oxy.o2;
                    env.sp_rate = settings().s.access.sp_rate;    // SPrate 0x76a5f8 (the shell's Cheats)
                    if (game.g_flags[144] == 2) env.gold_scale = 0.01f;   // DIFFICULTY.md G13 (0x6bd084)
                    inv.sp = player.sp;
                    int pop = 0;
                    if (pk.id >= content::kNewItemFirst) { if (!content_touch_new(pk)) return false; }   // mods
                    else if (pickup(inv, inv_items, env, pk.id, &pop) != PickupResult::Taken) return false;
                    content_taken(pk);                                // mods: collected flag, ItemObtained
                    player.sp = inv.sp;
                    player.hp = int(hp);
                    // The GoPopup (PICKUPS.md §7.3, §13.6); a full pool skips it, the grant stands.
                    auto pop_at = [&](const std::string& t, std::uint32_t col, float sc, const Vec3& at) {
                        if (popups.size() < 32) popups.push_back(make_popup(t, col, sc, {at.x, at.y + 0.5f, at.z}));
                    };
                    const int id = pk.id;
                    // The player-anchored ones stand at P+0x50 + (0, 0.5, 0), the ORIGIN [BYTES 0x5866a3..0x5866d1,
                    // 0x443a20..0x443a52], not the feet.
                    const Vec3 porg{feet.x, feet.y + kOriginH, feet.z};
                    if (id >= 0x78 && id <= 0x7f) pop_at(std::to_string(pop), 0xcdacd0, 0.75f, pk.pos);
                    else if (id >= 0x42 && id <= 0x47) { if (pop > 0) pop_at(std::to_string(pop), 0x00ff00, 1.0f, porg); }
                    else if (id == 0x72) pop_at("BOOST MAX", 0x00ff00, 0.65f, porg);
                    else if (id == 0x73) pop_at("OXYGEN MAX", 0x00ff00, 0.65f, porg);
                    else if (id == 0x48 || id == 0x49) pop_at("str +", 0xff6060, 1.0f, porg);
                    else if (id == 0x4a || id == 0x4b) pop_at("def +", 0x6060ff, 1.0f, porg);
                    else if (id == 0x4c || id == 0x4d) pop_at("mp +", 0xffff60, 1.0f, porg);
                    // common\cure.eft attached to the player at (0, -1, 0) [0x7de1f4 / 0x7d9c34], mode 2
                    // (FUN_00585ff0 for 0x48..0x4d, FUN_00443390 0x4434a3 for the heals and seeds);
                    // its depth bias (5.0) draws it in front of her.
                    if ((pk.id >= 0x42 && pk.id <= 0x4d) || pk.id == 0x72 || pk.id == 0x73)
                        attach_to_player(spawn_sim_fx(63, {}, {}, 1.0f, {}, 0, {}), {0.0f, -1.0f, 0.0f}, 0.0f);
                    spawn_sim_fx(72, pk.pos, {}, 1.0f, {}, 0, {});      // common\z_item.eft
                    sfx_at(pickup_collect_se(pk.id), pk.pos, kSfxAnimVol);   // p_takeitem; s_growup0..2 for the buff drops
                    return true;
                };
                for (Pickup& pk : pickups) step_pickup(pick_rng, pk, pw);
                pickups.erase(std::remove_if(pickups.begin(), pickups.end(),
                                             [](const Pickup& pk) { return pk.dead; }), pickups.end());
            }
            for (Popup& pp : popups) step_popup(pp);           // FUN_00565170
            popups.erase(std::remove_if(popups.begin(), popups.end(), [](const Popup& pp) { return pp.dead; }),
                         popups.end());
            // Boost drain (FUN_00462c40): the gauge falls ~30/tick on Normal, so a Boost runs
            // ~1000 frames (~16.7 s). The end test comes FIRST (0x462c9f: gauge <= 0 -> end), so
            // the Boost ends on the tick after the gauge reaches 0; otherwise drain and clamp at 0.
            if (player.boosting && !csMgr.world_frozen()) {
                if (player.boost <= 0.0f) {
                    end_boost();
                } else {
                    // Accessory 0x3b (DAT_0076a628) drains x0.6; flag 290 (DAT_0076bda4) is the 400 drain.
                    player.boost -= skillmp::boost_drain(difficulty >= 2 ? difficulty - 2 : 0,
                                                         game.g_flags[290] != 0, inv.accessory == 0x3b);
                    if (player.boost <= 0.0f) player.boost = 0.0f;
                }
            }
            // anime_SMode(x) (ASO funcid 0x21, Toal's skill sections): while boosting, the gauge
            // grows by x * 0.6 (0x6bd334), clamped at full — it extends a running Boost.
            if (const float sm = avm.smode(); sm != 0.0f && player.boosting)
                player.boost = std::min(skillmp::kBoostFull, player.boost + sm * 0.6f);
            // The passive Boost fill (mover 0x467c0a..0x467d8c): ((1 - hp/max) * 25 + 1) * M.
            add_boost(skillmp::boost_passive(float(player.hp), float(player.max_hp),
                                             difficulty >= 2 ? difficulty - 2 : 0, std::uint32_t(game.g_flags[217])));
            // The skill button, then MP regen with its lag (mover 0x467aff..0x467bfa).
            if (!fly) {
                const bool held = !paused && ((g_in.held & input::kMagic) || drive_skill);
                const bool atk = !paused && (g_in.held & input::kAttack) != 0;
                skill_tick(held, atk);
            }
            {
                const int slot = skillmp::bracelet_slot(inv.magic);
                skillmp::Mp mp{player.sp_cur, player.sp_max, player.sp_lag};
                mp.tick(slot < 3 ? game.g_flags[182 + slot] : 0, inv.mp_buff != 0.0f ? inv.mp_buff : 1.0f);   // b900 while c198 & 0x10
                player.sp_cur = mp.mp; player.sp_lag = mp.lag;
            }
            ail_tick();                                        // FUN_00462550, before FUN_00462c40 (view_ailments.inl)
            hud_state_tick();                                  // the buff timers, the HUD pulse (view_hudstate.inl)
            {   // Oxygen (0x467d91..0x467f29, WATER.md §3): the held tool 0x61 = the Scales.
                // surface = +0x4ba: over sand2 the ray's surface (elsewhere the latch, as before).
                const float dmg = oxy.tick(submerged, psand.on_sand2() ? 0x12 : cur_surface, psand.buried,
                                           inv.held == 0x61, float(player.max_hp));
                if (dmg > 0.0f && player.hp > 0) {               // drowning: maxHP / 60 a tick
                    drown_acc += dmg;                            // [approx] the view's HP is an int
                    const int d = int(drown_acc);
                    drown_acc -= float(d);
                    player.hp = std::max(0, player.hp - d);
                }
                game.g_flags[503] = oxy.alpha != 0;
                player.o2 = oxy.o2; player.o2_alpha = oxy.alpha;
            }
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
                    ailments::clear_timers(ail);   // every 15 ticks, healed or not [BYTES 0x58c5e9..0x58c607]
                    // [H-med] the second gauge 0x76a738 is taken as the SP/mana bar.
                    player.sp_cur = std::min(player.sp_max, player.sp_cur + 3000.0f);
                    if (player.hp < player.max_hp) {
                        player.hp = player.max_hp;   // +9999, clamped
                        sfx_at(0xad, feet, kSfxAnimVol);
                    }
                }
            // Curse: FUN_004fa740 reads DEF - 5.0, no floor [EXPORT 004fa740.c:20-23] (STATUS_AILMENTS.md §6.4).
            const float pdef = player_stats().def - (ail.has(ailments::kCurse) ? ailments::kCurseStat : 0.0f);
            // A hit landing on the player: FUN_004fd350's player branch (src/cleria/hit.hpp).
            // Damage is FUN_004fa740 with the swing's own power as the bonus (attacker +0xfc
            // = GenerateCollision arg[11]), halved during Boost; then the severity reaction —
            // i-frames 8/12/16, hurt section 4/0xb/9, knockback 0.1 (+1.2 on a knockdown)
            // along `angle` (atan2(dz,dx) from the attacker to the player), and +dmg*100
            // Boost gauge. `rtype` is the swing's arg[9].
            // The player's death, whatever the cause (a hit, lava, drowning): FUN_0046f810.
            // With g_flags[133] (a losable duel) the fight ends instead of the game: 156 = 0,
            // 130 = 0 (174 stays 0, the scripts' "lost" branch), the boss gauge hidden, HP 1,
            // i-frames 9999. [not modelled] the stat recompute and the motion fields it zeroes.
            // Otherwise the player is dead: the death count and the game over (death_t above).
            // FUN_004fe9e0 L31-34: a hit needs the victim's +0x60 & 0x80 AND its Untouchable byte
            // (+0x9d7, ASO funcid 28) clear — the player's skill sections set it.
            const bool p_untouchable = avm.untouchable() || gs_ramp < 1.0f ||
                                       player_vanished ||        // 0x14d: +0x60 & 0x80 cleared
                                       boss6_grab;               // ... and by a boss06 head's grab (view_boss06.inl)
            auto player_death = [&] {
                if (duel::losable_death(game.g_flags, player.hp, player_iframes)) {   // src/cleria/duel.hpp
                    hud.hide_boss_gauge();
                    player_kb = {}; pvel = {};               // +0x32c/+0x334/+0xa3c/+0xa44/+0x5d0.. zeroed
                    content_duel_lost();                     // mods: this 156 = 0 is not a BossDefeated (view_content.inl)
                    std::printf("  [duel] lost: flag 133 set, HP 1, 156 / 130 cleared (FUN_0046f810)\n");
                    return;
                }
                if (death_t == 0) {                          // +0x3fc 0 -> 1: the first dead step
                    death_t = 1;
                    player_kb = {}; pvel = {};               // +0x32c/+0x334/+0xa3c/+0xa44/+0x5d0.. zeroed
                    // [H] the death section 5 (the hit reaction's state for a lethal hit); for lava /
                    // drowning the setter is not traced.
                    avm.set_section(5, /*force*/true);
                    std::printf("  [death] HP %d at (%.2f, %.2f, %.2f), section %d\n", player.hp, feet.x, feet.y,
                                feet.z, avm.section());
                    content_on_death();                      // mods: PlayerDeath (view_content.inl)
                }
            };
            // The shell's Cheats (not retail): refilled before the death check.
            if (cheat_hp) player.hp = player.max_hp;
            if (cheat_mp) player.sp_cur = player.sp_max;
            if (cheat_boost && !player.boosting) player.boost = skillmp::kBoostFull;
            if (cheat_o2) oxy.o2 = 6000.0f;
            if (player.hp < 1) player_death();                // lava / noise / drowning this tick
            bool hurt_direct = false;                         // a brain's own FUN_004fd350 call: no FUN_004fe9e0 gate
            auto hurt_player = [&](float atk, float bonus, int rtype, float angle, int status = 0) {
                if (p_untouchable && !hurt_direct) return;
                // Hugo's Force Shield (FUN_004fd350 0x4fd9d4 -> 0x4fd90e): a charge left blocks the
                // hit: damage 0, i-frames at least 16, SE 121, the "GUARD" popup (0xffaa00, scale
                // 0.75, origin + 0.75; English 0x69bf04), knockback 0.1 [ABILITIES_VERIFIED §3.2].
                // The break follows on the next shield tick. [not modelled] the +0x7e0 guard (effect 0x90).
                if (character == 2 && shield_on && shield_hits > 0) {
                    --shield_hits;
                    player_iframes = std::max(player_iframes, 16);
                    player_white_flash(hitfx::kGuardFlashStep);   // +0x264..+0x274
                    audio.play(0x79, kSfxAnimVol);
                    if (popups.size() < 32)
                        popups.push_back(make_popup("GUARD", 0xffaa00, 0.75f,
                                                    {feet.x, feet.y + kOriginH + 0.75f, feet.z}));
                    // ...then on to the tail's red damage number with 0 (0x4fda7d -> 0x4fe120).
                    damage_popup(0, 0xff0000, {feet.x, feet.y + kOriginH, feet.z});
                    const Impulse k = knock_impulse(angle, 0.1f);
                    player_kb.x += k.x; player_kb.z += k.z;
                    return;
                }
                // Hugo's super barrier: while +0x7e0 stands and the Force Shield did not take the hit, a GUARD
                // with no counter and no MP [BYTES 0x4fd9fc..0x4fda7d].
                if (character == 2 && fx_live(barrier_fx)) {
                    player_iframes = std::max(player_iframes, 16);
                    player_white_flash(hitfx::kGuardFlashStep);
                    audio.play(0x79, kSfxAnimVol);
                    if (popups.size() < 32)
                        popups.push_back(make_popup("GUARD", 0xffaa00, 0.75f,
                                                    {feet.x, feet.y + kOriginH + 0.75f, feet.z}));
                    damage_popup(0, 0xff0000, {feet.x, feet.y + kOriginH, feet.z});
                    const Impulse k = knock_impulse(angle, 0.1f);
                    player_kb.x += k.x; player_kb.z += k.z;
                    return;
                }
                const int raw = player_incoming_damage(AttackStats{atk, bonus, 0}, pdef,
                    [&](float p) { return float(rng_pct(rng)) < p; },
                    IncomingMode{game.g_flags[144], game.g_flags[143] != 0, game.g_flags[201] != 0});   // DIFFICULTY.md G8
                int dmg = access::damage_taken(player_damage_after_boost(raw, player.boosting),
                                               settings().s.access.damage_taken);   // the shell's Cheats
                // The MP super-armour of Yunica's 0x10c / 0x10f and Toal's 0x10e / 0x111 (Marker 0): with the MP
                // it is spent and the hit is the GUARD above (0x4fd90e); without, 16 i-frames and x 0.75
                // (skillmp::armor, BOOST_SKILLS.md §3.3).
                if (const skillmp::Armor ar = skillmp::armor(character, avm.section(), avm.marker(), player.sp_cur);
                    ar.active) {
                    player_iframes = std::max(player_iframes, skillmp::kArmorIframes);
                    if (ar.guard) {
                        player.sp_cur -= ar.spend;
                        player_white_flash(hitfx::kGuardFlashStep);
                        audio.play(0x79, kSfxAnimVol);
                        if (popups.size() < 32)
                            popups.push_back(make_popup("GUARD", 0xffaa00, 0.75f,
                                                        {feet.x, feet.y + kOriginH + 0.75f, feet.z}));
                        damage_popup(0, 0xff0000, {feet.x, feet.y + kOriginH, feet.z});
                        const Impulse k = knock_impulse(angle, 0.1f);
                        player_kb.x += k.x; player_kb.z += k.z;
                        return;
                    }
                    dmg = int(float(dmg) * skillmp::kArmorScale);
                }
                player.hp -= dmg;
                if (player.hp < 1) player.hp = 0;             // FUN_004fd350: hp - dmg < 1.0 -> hp = 0 (no "-3/60")
                if (dmg > 0) player_hurt_this_tick = true;    // +0x3bc |= 6 (FUN_004fd350 0x4fde3c)
                // 0x4fe014 (dmg > 0, after the reaction's writes, which draw nothing): FUN_004fab40(victim,
                // 0xff550a00) = five puffs at the origin - (cos, sin)(the hit angle, 256 steps) x the hurt radius
                // +0x68, size 0.3, radius 0.45, speed 0.025, layer 9 in water [BYTES 0x4fab40..0x4fabef].
                if (dmg > 0) {
                    const Cast* hc = casts.get(13);
                    const float hr = hc ? hc->hurt_radius : 0.45f;
                    const int hi = int(angle * 40.743664f);
                    dust_at("hurt", 5, 0xff550a00u, false,
                            {feet.x - cos_table(hi & 0xff) * hr, feet.y + kOriginH, feet.z - cos_table((hi - 0x40) & 0xff) * hr},
                            0.3f, 0.45f, 0.025f, submerged);
                    // ... then, at BloodyEffectLevel 2, three blood splats (18 draws) [BYTES 0x4fe02a..0x4fe063].
                    // [H] the position [ebp-0x58] is taken as the player's origin.
                    if (settings().s.game.bloody_effect_level > 1)
                        blood_at("hurt", 3, 0xff550a00u, 2, 0x18, 1.3f, 1.8f, {feet.x, feet.y + kOriginH, feet.z}, 0.65f,
                                 false, false);
                }
                // FUN_004fd350's tail 0x4fe10b..0x4fe120: FUN_004fae00(0xffff0000), the red number of
                // the damage taken, at the player's origin + 0.5. PLAYTEST_1_HUD.md item 3.
                damage_popup(dmg, 0xff0000, {feet.x, feet.y + kOriginH, feet.z});
                const bool died = player.hp < 1;              // the engine's test is < 1.0
                ail_on_hit(status, dmg);                      // the record's +0x10b (0x4fdc58..0x4fde0c; none on a kill / 0)
                const PlayerReaction re = player_reaction(dmg, float(player.max_hp), rtype, died,
                                                          avm.durable(), avm.section());
                if (ai_debug)
                    std::printf("[hurt] dmg %d state %d grant %d pif %d psec 0x%x\n", dmg,
                                re.motion_state, re.iframes, player_iframes, avm.section());
                if (std::getenv("CLERIA_DMG"))
                    std::printf("  [hurt] dmg %d (atk %.0f + %.1f vs DEF %.0f) reaction section %d i-frames %d knock %.2f at %.1f deg\n",
                                dmg, atk, bonus, pdef, re.motion_state, re.iframes, re.knockback, angle * 57.29578f);
                if (re.iframes > 0) player_iframes = std::max(player_iframes, re.iframes);
                if (re.motion_state >= 0 && re.motion_state != 5) avm.set_section(re.motion_state);
                pad_rumble(pad::hit_effect(re.motion_state));   // FUN_005d3500 0x4fde93..0x4fdfb5 (view_pad.inl)
                if (re.face_attacker)
                    face_dir = {std::cos(angle + 3.14159265f), 0, std::sin(angle + 3.14159265f)};
                const Impulse k = knock_impulse(angle, re.knockback);
                player_kb.x += k.x; player_kb.z += k.z;
                if (re.boost_gain > 0) add_boost(re.boost_gain);
                // p_damage is the player's own hurt-reaction sound; death adds p_hit0.
                if (dmg > 0) audio.play(kSfxPlayerHurt, kSfxAnimVol);
                if (died) audio.play(kSfxDeathEntry, kSfxAnimVol);
                hud.debug_message("-" + std::to_string(dmg) + " HP");
                if (died) player_death();
            };
            #include "view_xso13_damage.inl"     // 0x15c Chr_Damage on the player (FLOOR13_OPS.md)
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
                if (!en || !en->alive() || player_iframes > 0 || p_untouchable) continue;
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
                const float php_before = player.hp;
                hurt_player(en->atk, e.hb.power, e.hb.rtype, hang, e.hb.status);
                // M470's lifesteal (FUN_0048de00 0x48e103, M470.md §2): a hit landed in section 8 heals it by the HP the
                // player lost (at least 1) when it is down by 1 HP or more. [not modelled] the green popup.
                if (en->ai_index == 40 && e.avm.section() == 8 && en->max_hp - 1.0f >= en->hp)
                    en->hp = std::min(en->max_hp, float(int(std::max(1.0f, php_before - player.hp))) + en->hp);
                if (en->ai_index == 21 && en->max_hp > en->hp) {   // M280's lifesteal (FUN_004a5300 0x4a55b7, M280.md §2)
                    const int bb5c = difficulty >= 2 ? difficulty - 2 : 0;
                    const int amt = std::max(1, int(std::floor(en->max_hp / (bb5c == 2 ? 7.5f : bb5c == 1 ? 14.0f : 20.0f))));
                    en->hp = std::min(en->max_hp, en->hp + float(amt));   // [not modelled] the green popup
                }
            }
            // The boss parts' armed volumes (GoXx kind 9, mode-0 scan; VELAGUNDER.md §3.2): all
            // share re-hit token 0 with re-hit 30 [H-med], the damage with the boss's STR.
            if (boss && !boss->destroyed() && player_iframes == 0 && player.hp >= 1) {
                const Cast* pc = casts.get(13);
                const HitShape pbody = body_shape({feet.x, feet.y + kOriginH, feet.z},
                                                  pc ? pc->hurt_radius : 0.45f, pc ? pc->hurt_half_h : 0.5f,
                                                  pc ? pc->hurt_shape : 1, pc ? pc->center_offset : -0.25f);
                for (const Boss01Part& bp : boss->parts()) {
                    if (!bp.armed || bp.radius <= 0.0f) continue;
                    const HitShape vol{bp.radius, bp.centre, bp.half_h, bp.shape};
                    float hang = 0;
                    if (!overlap(vol, pbody, &hang)) continue;
                    if (player_bank.check_and_record(0xB0550000u, 30)) break;
                    const int hp0 = player.hp;
                    hurt_player(boss->str(), bp.power, bp.rtype, hang);
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  [hurt] t%llu velagunder part %d (STR %.0f power %.1f rtype %d r %.2f) player hp %d -> %d\n",
                                    (unsigned long long)tick_count, int(&bp - &boss->parts()[0]), boss->str(), bp.power,
                                    bp.rtype, bp.radius, hp0, player.hp);
                    player_hurt_this_tick = true;
                    break;
                }
            }
            #include "view_boss01_touch.inl"        // Velagunder's body entries (VELAGUNDER.md §3.4)
            #include "view_boss06_touch.inl"        // Zava's heads: the touch push, the direct damage calls (ZAVA_D.md §7.2)
            // Nygtilger's touch (LAB_00513390, §3.6): the player in contact with a piece's live
            // collision hull [approx: a sphere of r 0.7 at the origin, not the mover's contact
            // record], then the piece's verdict, the shared re-hit record (+0xf0/+0xf4 = 15, one
            // for all pieces), the contact damage with the piece's STR [U2: power 0, rtype 0, as
            // FUN_004fd350 is not traced], then touch_applied. [not modelled] its section-9 reaction.
            if (boss2 && player_iframes == 0 && player.hp >= 1) {
                const RoomMesh& b2m = phys_mesh(room);
                const Vec3 O{feet.x, feet.y + kOriginH, feet.z};
                for (int o = 0; o < boss02::kObjects; ++o) {
                    const char* nm = boss02::object_name(o);
                    const MeshObj* mo = meshes.find(nm);
                    const RoomMesh::ObjRange* rr = b2m.object_range(nm);
                    if (!mo || !rr || mo->no_collide) continue;
                    if (!sphere_overlaps_tris(b2m, O, 0.7f, rr->t0, rr->t1)) continue;
                    const Boss02Touch t = boss2->touch(o, float(player.hp));
                    if (!t.damage) continue;
                    if (player_bank.check_and_record(0xB0520000u, t.rehit)) break;
                    const Vec3 pp = boss2->object(o).pos();
                    hurt_player(t.str, 0.0f, 0, std::atan2(O.z - pp.z, O.x - pp.x));
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  touch %s (STR %.0f): player hp %d\n", nm, t.str, player.hp);
                    boss2->touch_applied(o, float(player.hp));
                    break;
                }
            }
            // Gelaldy's armed parts: the BEAT arm cylinders (+0x19c, r 3, half-height 3), the
            // damage with the boss's STR and the part's power / rtype. [H] one shared re-hit token
            // with re-hit 30, as Velagunder's parts.
            if (boss3 && !boss3->destroyed() && player_iframes == 0 && player.hp >= 1) {
                const Cast* pc = casts.get(13);
                const HitShape pbody = body_shape({feet.x, feet.y + kOriginH, feet.z},
                                                  pc ? pc->hurt_radius : 0.45f, pc ? pc->hurt_half_h : 0.5f,
                                                  pc ? pc->hurt_shape : 1, pc ? pc->center_offset : -0.25f);
                for (int pi = 0; pi < boss03::kParts; ++pi) {
                    const Boss03Part& bp = boss3->part(pi);
                    if (!bp.armed || bp.radius <= 0.0f) continue;
                    const HitShape vol{bp.radius, bp.centre, bp.half_h, bp.shape};
                    float hang = 0;
                    if (!overlap(vol, pbody, &hang)) continue;
                    if (player_bank.check_and_record(0xB0530000u, 30)) break;
                    hurt_player(boss3->str(), bp.power, bp.rtype, hang);
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  gelaldy arm %d hits the player: hp %d\n", pi, player.hp);
                    break;
                }
            }
            step_weather();                          // pool 9 (master update step 11): after the actors, before pools 2 / 6
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
                // The player's live swing for the bubble pop (FUN_004f9e90 on P+0xd0).
                benv.swing = [&](const HitShape& v) {
                    if (atk_window <= 0) return false;
                    const HitShape vol = volume_shape(atk_hb, {feet.x, feet.y + kOriginH, feet.z},
                                                      std::atan2(face_dir.z, face_dir.x));
                    return overlap(vol, v);
                };
                benv.swing_rtype = atk_hb.rtype;
                benv.character = character;
                benv.difficulty = difficulty >= 2 ? difficulty - 2 : 0;
                benv.rng = [&] { return pick_rng.next(); };
                benv.dust = [&](std::uint32_t argb, const Vec3& at) {   // FUN_00584550: the flash, the drip's bounce
                    dust_at("bullet", 7, argb, false, at, 0.35f, 0.32f, 0.012f, false);
                };
                benv.carrier = boss2_carrier;                    // kind 0x24's segment (Nygtilger)
                // Kind 0x25's probe: the terrain without the boss meshes; a shake already running (view_boss07.inl).
                benv.ray_no_boss = [&](const Vec3& o, const Vec3& dl, int set) {
                    bullet::RayHit r;
                    if (set == 2) r.hit = boss7_seg_nb(o, {o.x + dl.x, o.y + dl.y, o.z + dl.z}, &r.point);
                    return r;
                };
                benv.shake_frames = std::uint32_t(std::max(0, g_rig.shake_frames()));
                std::vector<bullet::Event> bev;
                #include "view_crystal.inl"          // M980's bullet scan (M980.md §3.4a)
                bullets.update(benv, &bev);
                for (auto it = gas_owner.begin(); it != gas_owner.end();) {
                    bool live = false;
                    for (auto& bp : bullets.list()) live = live || bp.get() == it->first;
                    it = live ? std::next(it) : gas_owner.erase(it);
                }
                for (const bullet::Event& ev : bev) {
                    if (ev.type == bullet::Event::kSound && ev.id >= 221 && ev.id <= 223 && boss2 && std::getenv("CLERIA_BOSSLOG"))
                        std::printf("  [boss02] t%llu a plasma ball strikes (SE %d)\n", (unsigned long long)tick_count, ev.id);
                    if (ev.type == bullet::Event::kSound) { sfx_at(ev.id, ev.pos, kSfxAnimVol); continue; }
                    if (ev.type == bullet::Event::kShake) {         // 0x76a5ac / 0x76a594.. (kind 0x25's burst)
                        g_rig.set_shake_frames(ev.id); g_rig.set_shake_amp(ev.push);
                        continue;
                    }
                    if (ev.type == bullet::Event::kPush) {          // knock channel 2 (+0x344/+0x34c)
                        player_kb2.x += ev.push.x; player_kb2.z += ev.push.z;
                        continue;
                    }
                    if (ev.target == 0) {                            // FUN_004fa160: the player
                        player.hp = std::min(player.max_hp, player.hp + int(ev.amount));
                    } else if (Actor* a = world.find(ev.target)) {  // the owner, clamped to its max
                        a->hp = std::min(a->max_hp, a->hp + ev.amount);
                    }
                }
            }
            for (Actor& a : world.actors())
                actor_mats[a.instance_id] = ef2sim::translation(a.pos.x, a.pos.y + a.height, a.pos.z);
            // The player's world Actor carries no height, so its matrix sat at the FEET: every
            // player-attached effect (Whirlwind, the charge glow) drew kOriginH low. Its origin
            // is feet + kOriginH (+0x324), as every spawn above computes `org`.
            actor_mats[13] = ef2sim::translation(feet.x, feet.y + kOriginH, feet.z);
            {
                const Cast* pc = casts.get(13);
                const HitShape pbody = body_shape({feet.x, feet.y + kOriginH, feet.z},
                                                  pc ? pc->hurt_radius : 0.45f, pc ? pc->hurt_half_h : 0.5f,
                                                  pc ? pc->hurt_shape : 1, pc ? pc->center_offset : -0.25f);
                int fx_broke = 0;                        // @Break_* objects the player's particles smashed
                fxsim.step([&](ef2sim::Particle& p) {
                    if (p.hit.side == 0) {
                        // The player's own effect particles on the monsters (FUN_004fbb40 through the
                        // mode-0 scan): the victim's bank keys on the particle token, 0 in practice
                        // (EFFECTHIT.md §5.4). [H] every overlapping monster is hit in the one tick,
                        // and the knockback heading is the particle -> victim direction.
                        bool landed = false;
                        const HitShape vol{p.hit.radius, p.hit.centre, p.hit.half_h, p.hit.half_h > 0.0f ? 1 : 0};
                        for (Actor& en : world.actors()) {
                            if (en.is_player() || !en.alive() || en.destroy_requested()) continue;
                            const EnemyRender* vr = nullptr;
                            for (const EnemyRender& er : render) if (er.actor == &en) { vr = &er; break; }
                            if (vr && !vr->actor->ai.hittable) continue;            // +0x60 & 0x80 clear
                            const HitShape body = body_shape({en.pos.x, en.pos.y + en.height, en.pos.z},
                                                             en.hurt_radius, en.hurt_half_h, en.hurt_shape, en.hurt_off);
                            if (!overlap(vol, body)) continue;
                            if (en.bank.check_and_record(0u, p.hit.rehit)) continue;
                            const float hd = std::atan2(en.pos.z - p.hit.centre.z, en.pos.x - p.hit.centre.x);
                            player_hit_enemy(en, vr, p.hit.power, p.hit.knock, p.hit.rtype,
                                             p.hit.cls >= 0 ? p.hit.cls : kDamageClassNone, hd);
                            landed = true;
                        }
                        // The same mode-0 scan's GoMesh pass (FUN_004fed50 [EXPORT 004fed50.c:296-345],
                        // VELAGUNDER.md §3.3): Velagunder's parts, so magic / skill effects (Hugo's
                        // shots) reach the stun part and the core as the swing does. Token 0, the
                        // particle's re-hit; the damage with the part's DEF / resist and the particle's
                        // power / class.
                        if (boss && !boss->destroyed())
                            for (int pi = 0; pi < boss01::kParts; ++pi) {
                                if (!boss->part_hittable(pi)) continue;
                                const Boss01Part& bp = boss->part(pi);
                                if (!overlap(vol, body_shape(bp.hurt_centre, bp.hurt_radius, 0.0f, 0, 0.0f))) continue;
                                if (boss_banks[std::size_t(pi)].check_and_record(0u, p.hit.rehit)) continue;
                                AttackStats a;
                                a.atk = player_stats().atk;
                                a.bonus = p.hit.power;
                                EnemyDefense vd; vd.def = bp.def;
                                for (int k = 0; k < 4; ++k) vd.resist[k] = bp.resist[k];
                                PlayerDamageMods mods;
                                mods.atk_debuff = ail.has(ailments::kCurse);   // FUN_004fa550: ATK - 5, floor 0
                                mods.yunica_bonus = character == 1 && game.g_flags[149] == 0;
                                const int dmg = access::damage_dealt(enemy_incoming_damage(a, p.hit.cls >= 0 ? p.hit.cls : kDamageClassNone,
                                    vd, mods, [&](float pct) { return float(rng_pct(rng)) < pct; }), settings().s.access.damage_dealt);
                                if (dmg > 0) boss->add_part_damage(pi, float(dmg));
                                boss_part_feedback(bp, dmg, p.hit.cls >= 0 ? p.hit.cls : kDamageClassNone);
                                if (std::getenv("CLERIA_DMG"))
                                    std::printf("  fx %d hits boss part %d (DEF %.0f): %d dmg, HP %.0f stun %.0f\n",
                                                p.fx ? p.fx->id : -1, pi, bp.def, dmg, boss->hp(), boss->stun_gauge());
                                landed = true;
                            }
                        #include "view_fx_mode0.inl"     // the rest of the mode-0 scan for a particle (Hugo's shots)
                        return landed;
                    }
                    if (p.hit.side != 1 || player_iframes > 0 || p_untouchable || player.hp < 1) return false;
                    const HitShape vol{p.hit.radius, p.hit.centre, p.hit.half_h, p.hit.half_h > 0.0f ? 1 : 0};
                    float ang = 0;
                    if (!overlap(vol, pbody, &ang)) return false;
                    if (player_bank.check_and_record(0u, p.hit.rehit)) return false;
                    const int hp0 = player.hp;
                    hurt_player(p.hit.atk, p.hit.power, p.hit.rtype, ang, p.hit.status);
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  [hurt] t%llu fx %d (atk %.0f power %.1f knock %.2f rtype %d r %.2f) player hp %d -> %d\n",
                                    (unsigned long long)tick_count, p.fx ? p.fx->id : -1, p.hit.atk, p.hit.power,
                                    p.hit.knock, p.hit.rtype, p.hit.radius, hp0, player.hp);
                    return true;
                });
                if (fx_broke) reload_room(/*rerun_events*/false);   // drop a mesh a particle shattered
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
            // `enable` is +0x4be, Chr_FootSound's byte (FUN_00571320; class 2 defaults to 1).
            if (const Actor* fs_wp = world.player(); fs_wp && !fs_wp->footsteps) {
            } else if (step_timer > 0) {
                --step_timer;
            } else if (grounded && (avm.section() == 0 || avm.section() == 1)) {
                const bool is_running = avm.section() == 1;
                // The exe's jitter uses xorshift128 (FUN_00439120) — the one
                // generator (DAT_0074622c) the script's 0x81 roll draws too.
                step_timer = play_footstep(audio, phys_mesh(room), floor_tri, step_n++,
                                           is_running, game.rng_next());
            } else if (!moving) {
                step_n = 0;   // the exe resets the counter when not in a step state
            }
            // The jump sets the TIMER, not a velocity — apex then lands at ~v0
            // seconds (0.285 -> tick 17), which is what makes it a time and not a
            // speed. No sqrt(2gh): the engine never solves for an apex.
            // The gate takes the PRESS (+0x50c & 2). Submerged, a press re-jumps once the timer
            // is <= 0.05, and every jump kicks the origin up by v0 at once and restarts section 3
            // (+0x3a0 = -1); in mode 2 it plays SE 780 (0x468ba7..0x468dc8, WATER.md §2b).
            // Every jump sets t = v0 and kicks the origin up by it the same tick (0x468d8c..0x468d9e):
            // the ground jump and the double jump through PlayerActions, the water re-jump here.
            const bool water_jump = submerged && jump_edge && avm.jump_enable() &&
                                    ((grounded && !pv.jumping) || can_rejump(pv, true));
            if (water_jump || pa_out.jump || pa_out.double_jump) {
                float oj = feet.y + kOriginH;
                CastMotion jm = pm;                              // Heavy: the lift-off v0 x 0.6666666 [BYTES 0x468d50..0x468d84]
                if (ail.has(ailments::kHeavy)) jm.jump_v0 *= ailments::kHeavyJump;
                begin_rejump(pv, oj, jm);
                feet.y = oj - kOriginH;
                if (water_jump || pa_out.double_jump) avm.set_section(3, /*force*/true);   // +0x3a0 = -1
                if (water_jump && water_mode == 2) sfx_at(0x30c, feet, kSfxAnimVol);
                jump_phase = 1;
            }
            if (feet.y < room.mesh.bmin.y - D) spawn_player();

            // Facing: map the world movement heading to one of the 8 sprite dirs
            // (screen-relative -- see heading_to_facing). SEN arrival facing uses the
            // same helper, so a warped-in player faces the same way it would if it had
            // walked in that direction.
            facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
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
            // The locomotion table 0x69bd0c = {2, 0, 1, 3}: idle 2, walk 0, run 1, air 3. A keyboard
            // direction runs (section 1) unless the walk rule above says walk (section 0).
            const std::int32_t want_sec = !grounded ? 3 : (moving ? (pa_out.locomotion_dash ? 0x10a : run_gait ? 1 : 0) : 2);
            // An attack section ends on its own: every player section >= 0x100 of C_010/C_020/C_030
            // reaches `end`, which chains to the queued follow-on (idle 2) through the ungated
            // FUN_00446620; the looping ones (the dash 0x10a, Toal's 0x10d/0x110) are ended by the
            // mover. Some hold one frame for 10..100 ticks (0x10b, Hugo's 0x112 / 0x12b, 0x139,
            // Toal's 0x12d), so no stall release may cut them short.
            // The jump section's phases (+0x3a8, 00465170.c:1964-1988): rising it holds at marker
            // 2; once the timer falls to <= 0.01 it fast-forwards to marker 2 and holds at 3; on the
            // ground it fast-forwards to 3 and releases, so the landing frames play out.
            if (avm.section() == 3) {
                if (jump_phase == 0 && !grounded) { jump_phase = 2; avm.fast_forward_to_marker(2); avm.set_marker_block(3); }
                if (jump_phase == 1) {
                    avm.set_marker_block(2);
                    if (pv.t <= 0.01f) { jump_phase = 2; avm.fast_forward_to_marker(2); avm.set_marker_block(3); }
                }
                if (grounded && jump_phase != 0) { jump_phase = 0; avm.fast_forward_to_marker(3); avm.set_marker_block(-1); }
            } else {
                jump_phase = 0;
            }
            sec3_ground = (avm.section() == 3 && grounded) ? sec3_ground + 1 : 0;
            // The mover reselects locomotion only below section 3 or in the dash 0x10a
            // (00465170.c:1935-1943); the jump plays out and chains on its own.
            const int cur_sec = avm.section();
            if (cur_sec < 3 || cur_sec == 0x10a)
                avm.set_section(want_sec);
            else if (cur_sec == 3 && sec3_ground >= 60)
                avm.set_section(want_sec, /*force*/true);         // [STAND-IN] a jump section that never ends
            avm.set_is_land(grounded);
            avm.tick();
            // anime_Jump (FUN_00449460 [BYTES 0x449464..0x4494af], MOVER.md §4.1), as the enemies' below: grounded
            // only, t = +0x420 = v (0.0 = the cast's +0x408), y += v at once, +0x388 = 0. Toal's Ignis hops by it
            // (C_030.ASO 0x10e Jump(0.27) / 0x111 Jump(0.31) at tick 12, BOOST_SKILLS.md §3.5); the player's VM
            // dropped the op. [scoped, not retail] those two sections only: honoured in every section it changes
            // zava_fight's log (Yunica's sections with a Jump, not checked against the view's own stand-ins yet).
            if (avm.jumped() && character == 3 && (avm.section() == 0x10e || avm.section() == 0x111)) {
                float oy = feet.y + kOriginH;
                apply_jump(pv, oy, avm.jump_v(), pm.jump_v0);
                feet.y = oy - kOriginH;
                grounded = false;
            }
            if (avm.dashed()) {                         // anime_Dash (0x447665): channel 1 = (cos, sin)(heading) * d
                const float h = std::atan2(face_dir.z, face_dir.x);
                player_kb.x = std::cos(h) * avm.dash_d();
                player_kb.z = std::sin(h) * avm.dash_d();
            }

            spawn_player_bullets();                      // anime_FireBullet
            // anime_GenerateEffect from the player's ASO (FUN_004496f0, as the enemies' below): at
            // origin + (cos, sin)(angle + facing) x fwd, y + voff, yaw angle - (facing + pi/2), the
            // player's side (0), class FUN_0046d5e0 (the section's), Hugo's 0x86 / 0x87 with rtype 3
            // and their handle kept as the Force Shield (+0x7c4). Their particles hit the monsters
            // in the hit tick below.
            for (const AsoVm::EffectEvent& fe : avm.effects()) {
                const float pf = std::atan2(face_dir.z, face_dir.x);
                const Vec3 org{feet.x, feet.y + kOriginH, feet.z};
                const bool shield_id = character == 2 && (fe.id == 0x86 || fe.id == 0x87);
                ef2sim::SpawnParams sp;
                sp.power = fe.power; sp.knock = fe.knock; sp.status = fe.status;
                sp.rtype = shield_id ? 3 : fe.rtype;
                sp.cls = player_damage_class(avm.section());
                sp.atk = player_stats().atk; sp.side = 0; sp.owner = 13;
                ef2sim::Effect* fx = spawn_generate_effect(fe, org, pf, sp, 13);
                if (shield_id) { shield_fx = fx; shield_on = fx != nullptr; }
                if (character == 2 && fe.id == 0x90) barrier_fx = fx;   // +0x7e0 [EXPORT 004496f0.c:113-115]
            }
            // GenerateCollision fired: adopt this section's own box for its own lifetime.
            // anime_ChangeSpecular of the player's own set: the same struct the GUARD / Boost-ready flash uses —
            // the last writer owns it and restarts it at t = 0 (ASO_NOOPS.md §1.3; Hugo's s5 / s274 issue "off").
            if (const AsoVm::SpecularEvent* sv = avm.specular()) {
                player_spec.a = sv->a; player_spec.b = sv->b; player_spec.t = 0.0f; player_spec.step = sv->step; player_spec.count = sv->count;
            }
            if (avm.armed_hitbox()) {
                atk_hb = avm.hitbox();
                atk_cls = player_damage_class(avm.section());   // FUN_004494e0: cls at arm time
                atk_window = atk_hb.life > 0 ? atk_hb.life : 1;
                atk_dir = face_dir;
                player_hit_serial = next_hit_serial(world.hit_serial_counter_);
            }
            // GenerateLocus fired: spawn a GoLocusSword (FUN_00464960) on the character's SWORD file,
            // with the swing's token (+0x3c4) and the player-section damage class (FUN_0046d5e0).
            for (const AsoVm::LocusEvent& ev : avm.loci()) {
                LocusArgs la;
                la.row = ev.i[0]; la.a1 = ev.i[1]; la.a2 = ev.i[2]; la.a3 = ev.i[3];
                la.clank_lo = ev.i[4]; la.clank_hi = ev.i[5];
                la.bonus = ev.bonus; la.knock = ev.knock; la.life = ev.i[8]; la.rehit = ev.i[9];
                const int si = locus_sword_index(0xd, character, game.g_flags[149]);
                PlayerLocus pl;
                pl.cls = player_damage_class(avm.section());
                if (pl.sword.spawn(sl2set.sword[std::size_t(si)], la, player_hit_serial,
                                   game.g_flags[215], game.g_flags[217], character))
                    loci.push_back(pl);
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
            show_player_frame(world.player());

            // The room's authored camera (SCM): the last rig step (stepped per tick below).
            if (g_cam.on) apply_cam_state(cam, feet);
        }

        // SEN trigger FUN_0042cf00 (every real step, from FUN_004281c0 -> FUN_00429450). It tests the
        // player's ORIGIN actor+0x50/54/58 (feet + kOriginH; in fly mode the camera) against each volume,
        // the box turned by its yaw (SenTrigger::contains). LEVEL-triggered: no inside/outside memory, no
        // cooldown [EXPORT 0042cf00.c]. Gates: the step gate (no 0x74ab8f, flag 156 0x76bb8c = 0: no door
        // during a boss fight), and per hit: no game over (0x74ab71), no Fix (0x75ad90 + 0x75ad94; this
        // block only runs unfrozen), no pending scene (0x76a538: a staged door or a ChangeScene), flag 129
        // (0x76bb20) = 0. Retail data never puts an arrival inside the destination's own volumes (0 of 367
        // records with the yaw applied, build/warp_re/TRANSIT.md §1), so arriving never re-triggers.
        if (!door_latched && !door_commit && csHost.pending_scene.empty() && death_t == 0 &&
            game.g_flags[156] == 0 && game.g_flags[129] == 0) {
            const Vec3 probe = fly ? cam.pos : Vec3{feet.x, feet.y + kOriginH, feet.z};
            for (const SenTrigger& t : room.sen.triggers) {
                if (!t.contains(probe)) continue;
                // 0x42d0fc..0x42d1be: fade target 1.0, current 0, hold 0 (the speed is left as it is);
                // stage this volume's descriptor and latch its scene.
                csHost.fade_cur = 0.0f;
                csHost.fade_tgt = 1.0f;
                door_pending = t;
                door_latched = true;
                std::printf("  [door] t%llu %s -> %s (fade out)\n", (unsigned long long)tick_count,
                            room.name.c_str(), t.dest_scene.c_str());
                if (!fly) {
                    // 0x42d1cb: i-frames +0x734 = max(+0x734, 255). 0x42d246..0x42d29d: the walk-out goal
                    // +0x358 / +0x35c = origin + 100 (0x6bdafc) along +0x308, reach +0x364 = 0.1225, speed
                    // +0x368 = 1.0, run bit +0x12c & 0x20 = the run stage +0x4ca. [approx] +0x308 is taken
                    // as her facing (free play keeps the command heading on it).
                    player_iframes = std::max(player_iframes, 255);
                    const float fl = std::sqrt(face_dir.x * face_dir.x + face_dir.z * face_dir.z);
                    if (fl > 1e-6f) {
                        door_walk.on = true;
                        door_walk.tgt = {feet.x + face_dir.x / fl * 100.0f, feet.y, feet.z + face_dir.z / fl * 100.0f};
                        door_walk.run = door_running;
                    }
                }
                break;   // 0x76a538 is set: the rest of the pool fails the gate
            }
        }
        }  // ---- end fixed-step logic ----------------------------------------
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
        if (offscreen) {
            st.texture = off_tex;
            wgpuTextureAddRef(off_tex);
            st.status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
        } else {
            wgpuSurfaceGetCurrentTexture(surface, &st);
        }
        if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) { configure(); continue; }
        WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
        const float rad = room_diag(room);
        if (drive_cam) cam = *drive_cam;
        update_fog(room, fly ? cam.pos : feet, dt);
        // The game's near / far (clip_near / clip_far); free-fly keeps a room-sized pair. The old
        // rad * 0.01 near put the near plane 60 units out in a skybox room (B_100 is ~6000 across),
        // clipping the whole corridor and the player away (S_1101).
        last_vp = fly ? cam.view_proj(video_aspect(cw, ch), rad * 0.01f, rad * 8.0f)
                      : cam.view_proj(video_aspect(cw, ch), clip_near, clip_far);   // StretchAspect (view_video.inl)
        {   // The oxygen bar's anchor: the player's origin through the frame's view-projection.
            const Vec3 o{feet.x, feet.y + kOriginH, feet.z};
            const float* M = last_vp.m.data();
            const float px = o.x * M[0] + o.y * M[4] + o.z * M[8] + M[12];
            const float py = o.x * M[1] + o.y * M[5] + o.z * M[9] + M[13];
            const float pw = o.x * M[3] + o.y * M[7] + o.z * M[11] + M[15];
            if (pw > 1e-4f) {
                player.o2_px = (px / pw * 0.5f + 0.5f) * float(cw);
                player.o2_py = (0.5f - py / pw * 0.5f) * float(ch);
            }
        }
        for (int gi = 0; gi < 3; ++gi) {                   // the mini gauges' anchors (MINI_GAUGE.md §3.2)
            const Vec3 o = hud.gauge[gi].anchor;
            const float* M = last_vp.m.data();
            const float px = o.x * M[0] + o.y * M[4] + o.z * M[8] + M[12];
            const float py = o.x * M[1] + o.y * M[5] + o.z * M[9] + M[13];
            const float pw = o.x * M[3] + o.y * M[7] + o.z * M[11] + M[15];
            hud.gauge_proj[gi] = pw > 1e-4f;               // [approx] retail has no behind-the-camera test (U5)
            if (!hud.gauge_proj[gi]) continue;
            hud.gauge_px[gi] = (px / pw * 0.5f + 0.5f) * float(cw);
            hud.gauge_py[gi] = (0.5f - py / pw * 0.5f) * float(ch);
        }
        have_vp = true;
        if (scrfx_log && (csHost.light_rgb.x != light_drawn.x || csHost.light_rgb.y != light_drawn.y ||
                          csHost.light_rgb.z != light_drawn.z))
            std::printf("  [light] t%llu the scene light's colour (%.3f, %.3f, %.3f)\n", (unsigned long long)tick_count,
                        csHost.light_rgb.x, csHost.light_rgb.y, csHost.light_rgb.z);
        g_scene_light = light_drawn = csHost.light_rgb;    // 0x76a570..78 -> 0x7b2f14..1c: the world draws below
        write_mvp(g, last_vp);
        const WGPUTextureView frame_view = view, frame_depth = depth;
        video_world_begin(g, cw, ch, view, depth);       // Render scale: the world's own target (view_video.inl)
        draw(g, room, view, depth, 1);                     // render layers 3 / 5 (layer 0xb below)
        // Character billboard (play mode only; fly is a free debug camera). Draw the floor
        // shadow first, on the actual floor under the player (so it stays put during a jump).
        if (!fly && facing >= 0 && facing < 8 && chr.dirs[facing].tex) {
            const float sfl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + 0.5f);
            const Actor* wpl = world.player();
            if (!wpl || !(wpl->flags12c & 1u))          // Chr_Shadow(13, 0)
                draw_shadow(g, view, depth, shadows.tex, feet,
                            (sfl > kNoFloor ? sfl : feet.y), 0.6f);
            const float palpha = wpl ? wpl->ai.alpha : 1.0f;   // Chr_Alpha(13, ...) (+0x4d8)
            if (palpha > 0.0f)
                draw_character(g, view, depth, feet, kCharH, chr.dirs[facing], cam.forward(), cam.pos,
                               0.0f, g_scene_light.x, g_scene_light.y, g_scene_light.z, false, palpha,   // diffuse x the scene light
                               wpl && tremble_active(wpl->tremble) ? wpl->tremble.dx : 0.0f,   // Chr_Tremble(13)
                               wpl && tremble_active(wpl->tremble) ? wpl->tremble.dy : 0.0f,
                               wpl ? wpl->scale : 1.0f, player_spec.colour());   // +0x14c from the tint struct +0x260
        }
        {
            const std::uint32_t ms = std::uint32_t(SDL_GetTicks());
            const Vec3 cf = cam.forward();
            const Vec3 cright = normalize(cross(Vec3{0, 1, 0}, cf)), cup = cross(cf, cright);   // 0x795f30 / 0x795f40
            // Debris: static texture 0x42 + id = data\image\debris\debris%03d.dds, whole [EXPORT 00457410.c:44-49].
            static std::unordered_map<int, SpriteTex> debris_tex;
            auto debris_icon = [&](int id) -> const SpriteTex* {
                auto it = debris_tex.find(id);
                if (it == debris_tex.end()) {
                    char nm[32];
                    std::snprintf(nm, sizeof nm, "DEBRIS%03d.DDS", id);
                    SpriteTex s{};
                    Dds d = load_dds(asset_root / "IMAGE" / "DEBRIS" / nm);
                    if (d.ok()) s = upload_rgba(g, d.width, d.height, d.data.data());
                    it = debris_tex.emplace(id, s).first;
                }
                return it->second.tex ? &it->second : nullptr;
            };
            for (const Pickup& pk : pickups) {
                if (pk.delay > 0) continue;
                const SpriteTex* ic = pk.debris ? debris_icon(pk.id) : pauseUi.icon(g, items_tbl, asset_root, pk.id);
                if (!ic) continue;
                // FUN_00586e40: flat at (x, ground, z) - F * 0.15, ground = hit y + 0.02 (draw_shadow
                // lifts its floorY by 0.05, so it gets ground - 0.05). It used to sit at the hit y
                // itself and z-fought the floor it lies on.
                const float sa = pk.shadow_alpha();
                const Vec3 sc = pk.shadow_centre(cf);
                if (sa > 0) draw_shadow(g, view, depth, *ic, sc, sc.y - 0.05f, pk.half, 0, sa);
                // FUN_00586bf0: a camera-plane quad at pk.sprite_centre (it used to stand upright
                // with its bottom 0.18 under the floor, clipped as the camera moved); the glint is the
                // vertex specular (+0x12c, grey 7k); in water the RGB is halved.
                const float t = pk.in_water && pk.kind != 1 ? 0.5f : 1.0f;
                // The quad turns by the spin (+0x148) in the camera plane, quantised to the 256-step table; a
                // leaf's roll is a triangle wave of it [EXPORT 00584f90.c:223-229]. [approx] the turn's sense.
                Vec3 qr = cright, qu = cup;
                if (pk.spin != 0.0f) {
                    int si = int(pk.spin * 40.7437f);
                    if (pk.kind == 1) { const int u7 = (si / 8) & 0x7f; si = (u7 < 0x40 ? u7 : 0x80 - u7) - 0x20; }
                    const float a = float(si & 255) * (6.28318531f / 256.0f), c = std::cos(a), s = std::sin(a);
                    qr = {cright.x * c + cup.x * s, cright.y * c + cup.y * s, cright.z * c + cup.z * s};
                    qu = {cup.x * c - cright.x * s, cup.y * c - cright.y * s, cup.z * c - cright.z * s};
                }
                draw_billboard(g, view, depth, *ic, pk.sprite_centre(cup, cf), pk.half, qr, qu,
                               std::uint32_t(pk.glint(ms)) * 0x010101u, t, t, t, pk.alpha());
            }
            if (!popups.empty()) {                            // layer 0xf, after the world, no Z test
                static SpriteTex geo20;
                if (!geo20.tex) {
                    Dds dd = load_dds(asset_root / "IMAGE" / "F_GEO20.DDS");
                    if (dd.ok()) geo20 = upload_rgba(g, dd.width, dd.height, dd.data.data());
                }
                WGPUTextureView pdepth = make_depth(g, video_world_w(cw), video_world_h(ch));
                for (const Popup& pp : popups) draw_popup_glyphs(g, view, pdepth, geo20, pp, cam.forward());
                wgpuTextureViewRelease(pdepth);
            }
        }
        draw_enemies(g, view, depth, efx, render, cam.forward(), cam.pos, fx_time, shadows.tex);
        draw(g, room, view, depth, 2);   // layer 0xb (the water surfaces) after the actors' layers 6/8/9
        fx_time += dt;
        draw_flames(g, room, view, depth, flames, cam.forward(), fx_time);
        // Script-placed persistent effects (the barrier ward). Killed by op 0x119 when
        // the barrier breaks, so it renders only while the fight is unresolved.
        draw_located_effects(g, world.effects(), asset_root, efl, eft, view, depth,
                             cam.forward(), fx_time);
        draw_particles(g, fxsim, asset_root, eft, view, depth, cam.forward(), cam.pos);
        draw_weather(view, depth, cam.forward());                 // view_weather.inl
        {   // The bubbles (FUN_004ffdb0): at the owner's origin + 0.75 + yoff, far-to-near.
            struct EmDraw { const Emotion* e; Vec3 base; float d2; };
            std::vector<EmDraw> ems;
            for (const Emotion& em : emotions) {
                if (em.age <= 0) continue;
                Vec3 base;
                if (em.owner == 13) base = {feet.x, feet.y + kOriginH, feet.z};
                else if (const Actor* a = world.find(em.owner)) base = {a->pos.x, a->pos.y + a->height, a->pos.z};
                else continue;
                base.y += 0.75f + em.yoff;
                const float dx = base.x - cam.pos.x, dy = base.y - cam.pos.y, dz = base.z - cam.pos.z;
                ems.push_back({&em, base, dx * dx + dy * dy + dz * dz});
            }
            std::sort(ems.begin(), ems.end(), [](const EmDraw& a, const EmDraw& b) { return a.d2 > b.d2; });
            for (const EmDraw& ed : ems) {
                const EmotionQuad q = emotion_quad(*ed.e);
                draw_billboard_uv(g, view, depth, emotion_tex, ed.base, q.x0, q.x1, q.y0, q.y1,
                                  q.u0, q.v0, q.u1, q.v1, cam.forward(), float(q.a8) / 255.0f);
            }
        }
        draw_fireblur(g_vgpu.world_on ? g_vgpu.wtex : (g_vgpu.copy_ok ? st.texture : nullptr), view,
                      video_world_w(cw), video_world_h(ch), cw, ch);   // FireBlur after layer 14 (view_weather.inl)
        draw_mask_of_eyes(g, mask_of_eyes_on(inv), g_vgpu.world_on ? g_vgpu.wtex : (g_vgpu.copy_ok ? st.texture : nullptr),
                          view, video_world_w(cw), video_world_h(ch));   // then the mask pass (view_mask.inl)
        draw_zoom_blur(g, zb, fhold, fx_snapshot, g_vgpu.world_on ? g_vgpu.wtex : (g_vgpu.copy_ok ? st.texture : nullptr), view,
                       video_world_w(cw), video_world_h(ch), cw, ch);   // then ZoomBlur / the frame hold (view_screenfx.inl)
        if (scrfx_log && fhold.strength != 0.0f && fx_snapshot)
            std::printf("  [framehold] t%llu frame copied, drawn at alpha %d\n", (unsigned long long)tick_count, fhold.alpha8());
        fx_snapshot = false;                                  // 0x7b2488 = 0 at the end of the frame (0x5e0563)
        if (scrfx_log && zb.draws() && !zb_draw_logged) {
            zb_draw_logged = true;
            std::printf("  [zoomblur] t%llu draw strength %.4f alpha %d taps %d centre (%.3f, %.3f)\n",
                        (unsigned long long)tick_count, zb.strength, zb.alpha8(), kZoomBlurTaps, zb.cx, zb.cy);
        }
        video_world_end(g, frame_view, frame_depth, view, depth);   // the world onto the frame (view_video.inl)
        g_scene_light = {1.0f, 1.0f, 1.0f};                   // the HUD / menu draws that share write_mvp stay white
        player.character = character;
        player.magic = inv.magic;
        hud_state_fill();                                     // view_hudstate.inl
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
        access_set_fade(hud, csHost.fade_rgb, csHost.fade_cur);   // hud.set_fade, through ReduceFlashes
        hud.overlay_bind = cf_bind;
        hud.overlay_alpha = cf_bind ? csHost.cf_alpha : 0.0f;
        // Bind the HUD's four equipment slots (FUN_0043fe00): weapon, armor, foot (flags
        // 151..153) and the accessory (0x76a628; 0xff = none). [not modelled] the dimming of a
        // used-up item and the Lv badges (INVENTORY.md §7).
        {
            const int acc = (inv.accessory == 0xff) ? -1 : inv.accessory;
            const int ids[4] = {game.g_flags[151], game.g_flags[152], game.g_flags[153], acc};
            hud.set_equip(g, items_tbl, asset_root, ids);
        }
        hud.render(g, view, cw, ch, room.name + "  (" + kCharNames[character] + ")", dt, &player);
        // The cutscene overlay, in descending draw key (WNDDIALOG_DRAW.md §8.1: pass 15 is qsorted
        // descending [BYTES]): the busts (0x0a000000 + priority), the message box (0x08000000),
        // the caption card (0x06800000), the dialogs (0x03000000 + z).
        if (bookOk && (!csHost.bust_set.empty() || csHost.wtext.alive() || csHost.wdlg.alive() ||
                       csHost.caption_card.alive())) {
            DrawList wl;
            csHost.bust_set.draw(wl, int(cw), int(ch));
            csHost.wtext.draw(wl, csHost.wdlg.alive());
            csHost.caption_card.draw(wl);
            csHost.wdlg.draw(wl);
            bookR.render(g, view, cw, ch, wl);
        }
        // The pause book over the frozen game (BACK00 is opaque; the pillars cover the rest).
        if (paused && bookOk && book.is_open()) {
            DrawList dl;
            book.draw(dl, book_model());
            bookR.render(g, view, cw, ch, dl);
        }
        // The game over (FUN_00427820): gameover.dds as a held caption card, then its menu.
        if (bookOk && (gameover_card.alive() || gameover_menu.alive())) {
            DrawList gl;
            gameover_card.draw(gl);
            gameover_menu.draw(gl);
            bookR.render(g, view, cw, ch, gl);
        }
        // The cursor sprite (key 0x02000000: over the HUD, the windows, the book). [H] drawn after the
        // CrossFade capture, so a dissolve does not carry a ghost of it.
        const bool cursor_now = bookOk && g_cursor.queued() && g_cursor.colour != 0;
        // CrossFade: every frame is kept in cf_tex until a fade starts; the op then dissolves the
        // last frame the player saw (the op's "this frame") into the live view. [H] the engine
        // captures at its own present; keeping the last frame avoids depending on tick order.
        if (csHost.cf_alpha <= 0.0f) {
            if (!cf_tex || cf_w != cw || cf_h != ch) {
                if (cf_bind) wgpuBindGroupRelease(cf_bind);
                if (cf_view) wgpuTextureViewRelease(cf_view);
                if (cf_tex) wgpuTextureRelease(cf_tex);
                WGPUTextureDescriptor td = {};
                td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
                td.dimension = WGPUTextureDimension_2D; td.size = {cw, ch, 1};
                td.format = fmt; td.mipLevelCount = 1; td.sampleCount = 1;
                cf_tex = wgpuDeviceCreateTexture(g.device, &td);
                cf_view = wgpuTextureCreateView(cf_tex, nullptr);
                cf_bind = hud.make_bind(g, cf_view);
                cf_w = cw; cf_h = ch;
            }
            WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
            WGPUTexelCopyTextureInfo src = {}; src.texture = st.texture; src.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyTextureInfo dst = {}; dst.texture = cf_tex; dst.aspect = WGPUTextureAspect_All;
            WGPUExtent3D ext = {cw, ch, 1};
            wgpuCommandEncoderCopyTextureToTexture(enc, &src, &dst, &ext);
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
            wgpuQueueSubmit(g.queue, 1, &cmd);
            wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
        }
        if (cursor_now) draw_cursor(g, bookR, view, cw, ch, !fly);
        video_post(g, st.texture, view, cw, ch);         // Brightness (view_video.inl)
        shell.frame(g, view, cw, ch);                    // after the crossfade capture; a drive `shot` sees it
        if (shell_shot_frames > 0 && --shell_shot_frames == 0) {
            const char* d = std::getenv("CLERIA_SHOT_DIR");
            drive_shot = (fs::path(d ? d : ".") / ("shell_t" + std::to_string(tick_count) + ".ppm")).string();
        }
        if (!drive_shot.empty()) {                       // CLERIA_DRIVE `shot`
            const std::uint32_t bpr = (cw * 4 + 255) & ~255u;
            WGPUBufferDescriptor rbd = {};
            rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead; rbd.size = std::size_t(bpr) * ch;
            WGPUBuffer rb = wgpuDeviceCreateBuffer(g.device, &rbd);
            WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
            WGPUTexelCopyTextureInfo src = {}; src.texture = st.texture; src.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyBufferInfo dst = {}; dst.buffer = rb;
            dst.layout.bytesPerRow = bpr; dst.layout.rowsPerImage = ch;
            WGPUExtent3D ext = {cw, ch, 1};
            wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
            wgpuQueueSubmit(g.queue, 1, &cmd);
            wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
            MapReq mr;
            WGPUBufferMapCallbackInfo mi = {};
            mi.mode = WGPUCallbackMode_AllowProcessEvents; mi.callback = on_map; mi.userdata1 = &mr;
            wgpuBufferMapAsync(rb, WGPUMapMode_Read, 0, rbd.size, mi);
            while (!mr.done) wgpuDevicePoll(g.device, true, nullptr);
            const auto* px = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(rb, 0, rbd.size));
            const bool bgra = fmt == WGPUTextureFormat_BGRA8Unorm || fmt == WGPUTextureFormat_BGRA8UnormSrgb;
            std::ofstream f(drive_shot, std::ios::binary);
            f << "P6\n" << cw << " " << ch << "\n255\n";
            for (std::uint32_t y = 0; y < ch; ++y)
                for (std::uint32_t x = 0; x < cw; ++x) {
                    const std::uint8_t* q = px + std::size_t(y) * bpr + x * 4;
                    const char rgb[3] = {char(q[bgra ? 2 : 0]), char(q[1]), char(q[bgra ? 0 : 2])};
                    f.write(rgb, 3);
                }
            wgpuBufferUnmap(rb);
            wgpuBufferRelease(rb);
            std::printf("  [drive] wrote %s\n", drive_shot.c_str());
            drive_shot.clear();
            if (shell_drive_close) { shell.open = false; shell_drive_close = false; }
        }
        if (!offscreen) wgpuSurfacePresent(surface);
        else wgpuDevicePoll(g.device, true, nullptr);    // no vsync: wait out each frame
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(st.texture);
        video_frame_end(ticks);                          // the frame limiter / counters (view_video.inl)
    }
    // LAB_00427750 -> FUN_0042f510: the game over's Return to Title, and the pause book's
    // (FUN_005b8d40). [approx] the old room's GPU buffers are not freed (as on exit). A
    // CLERIA_DRIVE run shoots the title once and quits there (title.ppm).
    if (return_to_title) {
        std::printf("  [game] Return to Title: back to the title\n");
        show_title = true;
        audio.stop_all(true);                            // [H] forced as a load's is (0x43136a): no room loop on the title
        hud.hide_boss_gauge();                           // `hud` outlives the game: no gauge into the next one (#78)
        g_rig_room.clear();                              // the camera rig re-reads the next room
        emotions.clear();
        goto title_again;
    }
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

