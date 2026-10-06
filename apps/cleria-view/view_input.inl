// cleria-view — the settings and the input layer
// Textual part of main.cpp: #included by main.cpp in a fixed order; ONE translation unit, not
// standalone. The one place where the view touches yso_win.ini / cleria.ini (src/cleria/settings)
// and the one place where SDL keyboard / mouse / gamepad state becomes the mapper's snapshot
// (src/cleria/input_map, FUN_005d2ba0). Module APIs and file locations: build/shell_re/SHELL_CORE.md.

// ---- settings --------------------------------------------------------------------------------------
// ONE settings path. The interactive game (render_window without CLERIA_DRIVE) uses the persistent
// file: CLERIA_INI if set, else <Saved Games>\CleriaCore\yso_win.ini, imported once from retail's own
// yso_win.ini (located like retail: CLERIA_RETAIL_DIR\common.ini, else Saved Games\FALCOM\yso_win).
// Headless modes and CLERIA_DRIVE runs use the retail defaults in memory and never touch a file unless
// CLERIA_INI names one, so tests and goldens do not depend on the player's settings.
static SettingsStore g_settings;
static bool g_settings_ready = false;

static void settings_init(bool interactive) {
    g_settings_ready = true;
    if (const char* e = std::getenv("CLERIA_INI")) {
        g_settings.load(e);   // an explicit file: no import
        std::printf("settings: %s (CLERIA_INI)\n", e);
        return;
    }
    const fs::path p = interactive ? default_ini_path() : fs::path();
    if (p.empty()) { g_settings.use_memory_defaults(); return; }
    const char* rd = std::getenv("CLERIA_RETAIL_DIR");
    const fs::path retail = retail_ini_path(rd ? fs::path(rd) : fs::path());
    static const char* const kOrigin[] = {"memory", "loaded", "imported from ", "defaults"};
    const SettingsStore::Origin o = g_settings.load(p, retail);
    std::printf("settings: %s (%s%s)\n", p.string().c_str(), kOrigin[int(o)],
                o == SettingsStore::Origin::Imported ? retail.string().c_str() : "");
}
static SettingsStore& settings() {
    if (!g_settings_ready) settings_init(false);
    return g_settings;
}

// The in-game Options page works on its own copy (0..1024 sliders and the four switches, OPTIONS.md
// §2); these move it to and from the settings. Brightness <-> GammaValue per OPTIONS.md §2.
static float slider_to_gamma(int s) {   // 0..1024 -> 0.5..2.0, neutral 1.0 @512
    return s <= 512 ? float(s) / 512.0f * 0.5f + 0.5f : float(s - 512) / 512.0f + 1.0f;
}
static int gamma_to_slider(float g) {
    float s = g <= 1.0f ? (g - 0.5f) * 1024.0f : (g - 1.0f) * 512.0f + 512.0f;
    return std::clamp(int(std::lround(s)), 0, 1024);
}
// slider[0..2] = BGM / SFX / Brightness (0..1024); chk[0..3] = rows 3..6 (Analog/Dash/HoldAtk/AlwaysDash).
static void options_load(int slider[3], bool chk[4]) {
    const Settings& s = settings().s;
    slider[0] = std::clamp(int(s.sound.bgm_volume), 0, 1024);
    slider[1] = std::clamp(int(s.sound.effect_volume), 0, 1024);
    slider[2] = gamma_to_slider(s.graphics.gamma_value);
    chk[0] = s.input.pad_analog != 0;
    chk[1] = s.input.dash_control != 0;
    chk[2] = s.input.atk_btn_magic != 0;
    chk[3] = s.input.always_dash_ok != 0;
}
// FUN_0040d7e0 writes these at exit only; CleriaCore saves on every change (CONFIG.md §7.5).
static void options_save(const int slider[3], const bool chk[4]) {
    SettingsStore& st = settings();
    Settings& s = st.s;
    s.sound.bgm_volume = slider[0];
    s.sound.effect_volume = slider[1];
    if (slider[2] != gamma_to_slider(s.graphics.gamma_value)) s.graphics.gamma_value = slider_to_gamma(slider[2]);
    s.input.pad_analog = chk[0];
    s.input.dash_control = chk[1];
    s.input.atk_btn_magic = chk[2];
    s.input.always_dash_ok = chk[3];
    st.touch();
    st.save();
}
// [game] GameFirstPlay (FUN_0040b8e0 -> DAT_0074ab9b): the demo movies' skip gate (src/cleria/movie).
// Read only: the ending that sets it (FUN_0058f210) is [not modelled].
static bool ini_game_first_play() { return settings().s.game.game_first_play != 0; }

// ---- input: SDL -> the mapper's snapshot -----------------------------------------------------------
// SDL_Scancode values are USB HID usages, the numbering input_map's table is written in.
static_assert(SDL_SCANCODE_A == 4 && SDL_SCANCODE_ESCAPE == 41 && SDL_SCANCODE_F1 == 58 &&
              SDL_SCANCODE_UP == 82 && SDL_SCANCODE_KP_8 == 96 && SDL_SCANCODE_LSHIFT == 225,
              "input_map's scancode table assumes SDL's USB-usage numbering");
static_assert(SDL_GAMEPAD_BUTTON_SOUTH == 0 && SDL_GAMEPAD_BUTTON_BACK == 4 && SDL_GAMEPAD_BUTTON_START == 6 &&
              SDL_GAMEPAD_BUTTON_LEFT_SHOULDER == 9 && SDL_GAMEPAD_BUTTON_DPAD_UP == 11 &&
              SDL_GAMEPAD_BUTTON_MISC1 == 15 && SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 == 16,
              "input_map's gamepad table assumes SDL's SDL_GamepadButton numbering");

struct SdlInput {
    SDL_Gamepad* pad = nullptr;
    std::array<bool, 256> tapped{};   // keys that went down since the last snapshot (a sub-tick tap
    bool mouse_tapped[5] = {};        // still reads as held for one tick) [H, CleriaCore]
    int wheel = 0;
    // The shell overlay owns the input: the game sees nothing pressed (and the taps are dropped).
    bool suppressed = false;

    static const std::array<std::uint8_t, SDL_SCANCODE_COUNT>& dik_of() {
        static const auto t = [] {
            std::array<std::uint8_t, SDL_SCANCODE_COUNT> a{};
            for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc) a[std::size_t(sc)] = std::uint8_t(input::dik_from_sdl_scancode(sc));
            return a;
        }();
        return t;
    }
    // The GamePadID-th gamepad (1-based, clamped like FUN_005d5b20) unless [debug] NoInitializeGameCtrl.
    void open_pad() {
        if (pad) return;
        const Settings& s = settings().s;
        if (s.debug.no_initialize_game_ctrl.value_or(0) != 0) return;
        int n = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&n);
        if (ids && n > 0) {
            const int k = std::clamp(int(s.input.game_pad_id), 1, n) - 1;
            pad = SDL_OpenGamepad(ids[k]);
            if (pad) std::printf("input: gamepad '%s'\n", SDL_GetGamepadName(pad) ? SDL_GetGamepadName(pad) : "?");
        }
        SDL_free(ids);
    }
    void on_event(const SDL_Event& e) {
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
            if (e.key.scancode < SDL_SCANCODE_COUNT)
                if (const std::uint8_t d = dik_of()[std::size_t(e.key.scancode)]) tapped[d] = true;
        } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            static constexpr int kIdx[6] = {-1, 0, 2, 1, 3, 4};   // SDL 1 L, 2 M, 3 R, 4 X1, 5 X2
            if (e.button.button >= 1 && e.button.button <= 5) mouse_tapped[kIdx[e.button.button]] = true;
        } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            wheel += int(e.wheel.y);
        } else if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
            open_pad();
        } else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
            if (pad && SDL_GetGamepadID(pad) == e.gdevice.which) {
                SDL_CloseGamepad(pad);
                pad = nullptr;
                open_pad();
            }
        }
    }
    input::Snapshot snapshot() {
        input::Snapshot s;
        if (suppressed) { tapped = {}; for (bool& m : mouse_tapped) m = false; wheel = 0; return s; }
        int n = 0;
        const bool* ks = SDL_GetKeyboardState(&n);
        const auto& dk = dik_of();
        for (int sc = 0; sc < n && sc < SDL_SCANCODE_COUNT; ++sc)
            if (ks[sc] && dk[std::size_t(sc)]) s.key[dk[std::size_t(sc)]] = true;
        for (int d = 0; d < 256; ++d) s.key[std::size_t(d)] = s.key[std::size_t(d)] || tapped[std::size_t(d)];
        s.key[input::kDikF1] = false;   // F1 = the shell overlay, never a game key
        const SDL_MouseButtonFlags mb = SDL_GetMouseState(nullptr, nullptr);
        const SDL_MouseButtonFlags mm[5] = {SDL_BUTTON_LMASK, SDL_BUTTON_RMASK, SDL_BUTTON_MMASK,
                                            SDL_BUTTON_X1MASK, SDL_BUTTON_X2MASK};
        for (int i = 0; i < 5; ++i) s.mouse[i] = (mb & mm[i]) != 0 || mouse_tapped[i];
        s.wheel = wheel;
        if (pad) {
            s.pad_present = true;
            // SDL's -32768..32767 to DInput's DIPROP_RANGE -1024..1024 [H: linear, as DInput scales].
            auto ax = [&](SDL_GamepadAxis a) {
                return std::clamp(int(SDL_GetGamepadAxis(pad, a)) * 1024 / 32767, -1024, 1024);
            };
            s.lx = ax(SDL_GAMEPAD_AXIS_LEFTX); s.ly = ax(SDL_GAMEPAD_AXIS_LEFTY);
            s.rx = ax(SDL_GAMEPAD_AXIS_RIGHTX); s.ry = ax(SDL_GAMEPAD_AXIS_RIGHTY);
            for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
                const int i = input::pad_button_from_sdl(b);
                if (i >= 0 && SDL_GetGamepadButton(pad, SDL_GamepadButton(b))) s.pad[std::size_t(i)] = true;
            }
            // [H, CleriaCore] the triggers as Button 11 / 12 past a quarter pull.
            if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8192)
                s.pad[std::size_t(input::pad_button_from_sdl(input::kSdlLeftTrigger))] = true;
            if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8192)
                s.pad[std::size_t(input::pad_button_from_sdl(input::kSdlRightTrigger))] = true;
            // The d-pad as POV 0 (hundredths of a degree clockwise from up) [H: the HID hat].
            const bool u = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_UP);
            const bool d = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
            const bool l = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
            const bool r = SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
            const int vy = (d ? 1 : 0) - (u ? 1 : 0), vx = (r ? 1 : 0) - (l ? 1 : 0);
            static constexpr std::int32_t kPov[3][3] = {{31500, 0, 4500}, {27000, -1, 9000}, {22500, 18000, 13500}};
            s.pov = kPov[vy + 1][vx + 1];
        }
        tapped = {};
        for (bool& m : mouse_tapped) m = false;
        wheel = 0;
        return s;
    }
};
static SdlInput g_sdl_in;
static input::Mapper g_mapper;
static input::Frame g_in;                  // this logic tick's record (held / pressed / dir / ...)
static std::uint64_t g_mapper_rev = 0;
static int g_mapper_book = -1;

// One mapper step (FUN_005d2ba0): call ONCE per logic tick, in whichever loop is ticking. The
// settings' revision re-configures it live (bindings, deadzone, PadAnalog, ...). `book_mode`: the
// pause book's 0.8 deadzone and 4-way stick while it is up (FUN_005a4ab0).
static bool g_shell_focus = false;   // the shell (view_shell.inl) owns the input: the game sees none
static const input::Frame& input_tick(bool book_mode = false) {
    g_sdl_in.suppressed = g_shell_focus;
    const SettingsStore& st = settings();
    if (st.revision() != g_mapper_rev || int(book_mode) != g_mapper_book) {
        g_mapper.configure(mapper_config(st.s, book_mode));
        g_mapper_rev = st.revision();
        g_mapper_book = int(book_mode);
    }
    g_in = g_mapper.step(g_sdl_in.snapshot());
    return g_in;
}
