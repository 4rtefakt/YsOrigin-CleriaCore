// cleria-view — the accessibility assists and cheats (src/cleria/access.hpp, build/shell_re/ACCESS.md)
// Textual part of main.cpp: #included after view_shell.inl; ONE translation unit, not standalone.
// The shell pages (Gameplay, Cheats, the Interface page's Accessibility rows), the cutscene-skip
// prompt (a Shell::hud layer), the per-tick application of the assists, and the CLERIA_DRIVE
// commands that test them. The game-code hooks themselves are one-liners at their sites in
// view_game.inl, each citing the retail function it sits on.

static access::SkipGate g_skip_gate;           // the cutscene skip (view_game's frozen branch steps it)
static access::FlashLimiter g_flash_limiter;   // the fader overlay under ReduceFlashes
static int g_drive_esc_hold = 0;               // CLERIA_DRIVE `eschold=N`: ticks of Esc left to hold

static access::Settings& acc() { return settings().s.access; }
static access::SkipMode skip_mode() { return access::SkipMode(std::clamp(acc().cutscene_skip, 0, 3)); }

// One logic tick, before the mapper step: the drive's held Esc, and the assists that live in
// engine objects (the camera rig's shake scale).
static void access_tick() {
    if (g_drive_esc_hold > 0) { g_sdl_in.tapped[input::kDikEscape] = true; --g_drive_esc_hold; }
    g_rig.shake_scale = std::clamp(acc().shake, 0.0f, 1.0f);
}

// The cutscene skip, once per logic tick while a Fix holds the world and no script menu is up
// (`pressed` / `held` = the mapper record's menu bit 0x10). Returns true to skip now.
static bool access_skip_step(bool pressed, bool held) {
    const access::SkipMode m = skip_mode();
    const access::SkipGate before = g_skip_gate;
    const bool skip = g_skip_gate.step(m, pressed, held);
    static const char* const kName[] = {"single", "double", "hold", "off"};
    if (skip) std::printf("  [access] cutscene skip (%s)\n", kName[int(m)]);
    else if (m == access::SkipMode::Double && pressed && before.window == 0)
        std::printf("  [access] skip prompt: press again\n");
    else if (m == access::SkipMode::Hold && pressed) std::printf("  [access] skip prompt: hold\n");
    return skip;
}

// The fader overlay (FUN_004106e0's fade state, ops 0xc8..0xca), through the flash limiter.
static void access_set_fade(Hud& hud, std::uint32_t rgb, float alpha) {
    std::uint32_t r = rgb;
    float a = alpha;
    g_flash_limiter.step(acc().reduce_flashes, rgb, alpha, &r, &a);
    hud.set_fade(r, a);
}

// ---- the prompt ----------------------------------------------------------------------------------------
// Top-right card over the game (the message box owns the bottom of the screen): the mode's line and a
// thin gold bar -- the time left for the second press, or the hold's fill.
static void access_hud(ImDrawList* dl, float w, float /*h*/) {
    const access::SkipMode m = skip_mode();
    const access::SkipGate::Prompt p = g_skip_gate.prompt(m);
    if (p == access::SkipGate::Prompt::None) return;
    const bool hold = p == access::SkipGate::Prompt::Hold;
    const float t = hold ? g_skip_gate.hold_fraction() : g_skip_gate.window_fraction();
    // Fade out over the last 20 ticks of the window (the hold card stays solid while held).
    const float fade = hold && g_skip_gate.holding ? 1.0f
                       : std::clamp(float(g_skip_gate.window) / 20.0f, 0.0f, 1.0f);
    if (fade <= 0.0f) return;
    auto A = [&](ImU32 c) {
        return (c & 0x00FFFFFFu) | (ImU32(float((c >> 24) & 0xFF) * fade) << 24);
    };
    const float S = ui::S;
    const char* head = "Skip cutscene";
    const char* line = hold ? "Hold to skip" : "Press again to skip";
    const float hpx = 13.5f * S, lpx = 21.0f * S;
    const ImVec2 ls = ui::text_size(ui::F.bold, lpx, ui::upper(line).c_str());
    const float bw = std::max(250.0f * S, ls.x + 44.0f * S), bh = 82.0f * S;
    const ImVec2 a{w - 40.0f * S - bw, 40.0f * S}, b{a.x + bw, a.y + bh};
    dl->AddRectFilled(a, b, A(IM_COL32(22, 21, 16, 225)), 6.0f * S);
    dl->AddRect(a, b, A(ui::kPanelEdge), 6.0f * S, 0, 1.2f * S);
    ui::text(dl, ui::F.cond, hpx, {a.x + 22.0f * S, a.y + 13.0f * S}, A(ui::kSection), ui::upper(head).c_str());
    ui::text(dl, ui::F.bold, lpx, {a.x + 22.0f * S, a.y + 33.0f * S}, A(ui::kTabOn), ui::upper(line).c_str());
    const ImVec2 r0{a.x + 22.0f * S, b.y - 14.0f * S}, r1{b.x - 22.0f * S, b.y - 10.0f * S};
    dl->AddRectFilled(r0, r1, A(IM_COL32(58, 55, 44, 255)), 2.0f * S);
    dl->AddRectFilled(r0, {r0.x + (r1.x - r0.x) * std::clamp(t, 0.0f, 1.0f), r1.y}, A(ui::kGold), 2.0f * S);
}

// ---- the pages ------------------------------------------------------------------------------------------
// A multiplier row stepped through `vals` (the stored value snaps to the nearest one).
static bool access_rate(ui::List& L, const char* label, float* v, std::initializer_list<float> vals,
                        const char* desc, const char* zero_label = nullptr) {
    std::vector<std::string> names;
    std::vector<const char*> ptrs;
    int idx = 0;
    float best = 1e9f;
    int k = 0;
    for (float x : vals) {
        char buf[32];
        if (x == 0.0f && zero_label) std::snprintf(buf, sizeof buf, "%s", zero_label);
        else std::snprintf(buf, sizeof buf, "%gx", double(x));
        names.push_back(buf);
        if (std::fabs(x - *v) < best) { best = std::fabs(x - *v); idx = k; }
        ++k;
    }
    for (const std::string& s : names) ptrs.push_back(s.c_str());
    const int old = idx;
    ui::choice(L, label, &idx, ptrs.data(), int(ptrs.size()), desc);
    if (idx == old) return false;
    *v = *(vals.begin() + idx);
    return true;
}

static void access_gameplay_page(ui::List& L, const std::function<void()>& persist) {
    access::Settings& a = acc();
    static const char* kSkip[] = {"Single press", "Double press", "Hold", "Off"};
    static const char* kSkipDesc[] = {
        "How the Camp Menu button (Esc, or your gamepad's menu button) skips a cutscene.\n\n"
        "Single press: one press skips the whole cutscene. The original game has no cutscene skip; "
        "this is CleriaCore's.",
        "How the Camp Menu button (Esc, or your gamepad's menu button) skips a cutscene.\n\n"
        "Double press: the first press shows \"Press again to skip\" for two seconds; a second press "
        "skips. Guards against skipping by accident. Not a retail option.",
        "How the Camp Menu button (Esc, or your gamepad's menu button) skips a cutscene.\n\n"
        "Hold: keep the button held for one second to skip; a bar fills while you hold. Not a retail "
        "option.",
        "How the Camp Menu button (Esc, or your gamepad's menu button) skips a cutscene.\n\n"
        "Off: cutscenes cannot be skipped, as in the original game.",
    };
    ui::section(L, "Cutscenes");
    int sk = std::clamp(a.cutscene_skip, 0, 3);
    if (ui::choice(L, "Cutscene skip", &sk, kSkip, 4, kSkipDesc[sk])) {
        a.cutscene_skip = sk;
        g_skip_gate.reset();
        persist();
    }
    ui::section(L, "Speed");
    float pct = std::round(a.game_speed * 100.0f);
    if (ui::slider(L, "Game speed", &pct, access::kSpeedMin * 100.0f, access::kSpeedMax * 100.0f, 5.0f, "%.0f%%",
                   "Runs the whole game slower or faster: at 50% you have twice as long to react to every "
                   "attack. The game still runs in its fixed 60 Hz steps; only how many run each second "
                   "changes. Not a retail option.")) {
        a.game_speed = pct / 100.0f;
        persist();
    }
}

static void access_cheats_page(ui::List& L, const std::function<void()>& persist) {
    access::Settings& a = acc();
    ui::info(L, "Status", a.cheats_active() ? "Cheats active" : "Original game", a.cheats_active() ? ui::kWarn : ui::kGood,
             "Whether any cheat on this page changes the game. Cheats are saved with your settings.");
    bool ch = false;
    if (ui::button(L, "Reset all cheats", "Turns every cheat on this page off and every multiplier back to 1x.")) {
        a.reset_cheats();
        ch = true;
    }
    ui::section(L, "Resources");
    ch |= ui::toggle(L, "Infinite HP", &a.inf_hp, "Your HP is refilled every frame. Not a retail option.");
    ch |= ui::toggle(L, "Infinite MP", &a.inf_mp, "Your skill gauge never runs out. Not a retail option.");
    ch |= ui::toggle(L, "Infinite Boost", &a.inf_boost, "The Boost gauge stays full. Not a retail option.");
    ch |= ui::toggle(L, "Infinite oxygen", &a.inf_o2, "Never run out of air underwater. Not a retail option.");
    ui::section(L, "Combat");
    ch |= ui::toggle(L, "One-hit kills", &a.one_hit_kill, "Every hit you land defeats the enemy. Not a retail option.");
    ch |= access_rate(L, "Damage dealt", &a.damage_dealt, {0.25f, 0.5f, 0.75f, 1, 1.5f, 2, 3, 5, 10},
                      "Multiplies the damage of every hit you land, on enemies and bosses alike. A guarded "
                      "hit still does nothing. Not a retail option.");
    ch |= access_rate(L, "Damage taken", &a.damage_taken, {0, 0.25f, 0.5f, 0.75f, 1, 1.5f, 2, 3, 4},
                      "Multiplies the damage enemies and bosses do to you. \"None\" makes you invincible to "
                      "attacks (lava, drowning and the like still hurt; use Infinite HP for those). Not a "
                      "retail option.", "None");
    ui::section(L, "Rewards");
    ch |= access_rate(L, "EXP gained", &a.exp_rate, {0.25f, 0.5f, 0.75f, 1, 1.5f, 2, 3, 4, 5, 10},
                      "Multiplies the experience every defeated enemy and boss gives you (the game's own EXP "
                      "rate, normally 1x). Not a retail option.");
    ch |= access_rate(L, "SP gained", &a.sp_rate, {0.25f, 0.5f, 0.75f, 1, 1.5f, 2, 3, 4, 5, 10},
                      "Multiplies the SP (the currency you spend at the goddess statues) that gold pickups "
                      "give you (the game's own SP rate, normally 1x). Not a retail option.");
    ch |= access_rate(L, "Drop rate", &a.drop_rate, {0, 0.25f, 0.5f, 0.75f, 1, 1.5f, 2, 3, 5, 10},
                      "Multiplies the chance of every item an enemy can drop (gold, recovery drops, seeds). "
                      "\"None\" turns enemy drops off. Not a retail option.", "None");
    if (ch) persist();
}

static void access_interface_rows(ui::List& L, const std::function<void()>& persist) {
    access::Settings& a = acc();
    ui::section(L, "Accessibility");
    if (ui::toggle(L, "Reduce flashes", &a.reduce_flashes,
                   "Softens bright full-screen flashes in cutscenes (the scripted screen fader): they are dimmed "
                   "and fade in over a few frames instead of in one. Fades to black are unchanged. Not a retail "
                   "option."))
        persist();
    float pct = std::round(a.shake * 100.0f);
    if (ui::slider(L, "Screen shake", &pct, 0.0f, 100.0f, 10.0f, "%.0f%%",
                   "How strongly the camera shakes for quakes, explosions and boss attacks. 0% turns it off. "
                   "Not a retail option.")) {
        a.shake = pct / 100.0f;
        persist();
    }
    static const char* kText[] = {"Original", "Fast", "Instant"};
    int ts = std::clamp(a.text_speed, 0, 2);
    if (ui::choice(L, "Text speed", &ts, kText, 3,
                   "How fast dialogue text appears. Original types it out (hold Confirm or Cancel to speed it "
                   "up, as in the original game); Fast always uses that faster speed; Instant shows each "
                   "message at once. You still press Confirm to continue. Not a retail option.")) {
        a.text_speed = ts;
        persist();
    }
}

// ---- CLERIA_DRIVE -----------------------------------------------------------------------------------------
// opt=<Key>=<value>  one [cleria.access] / [cleria.cheats] key by its cleria.ini name (opt=ExpRate=2)
// esctap             Esc for one tick (through the real mapper: the menu bit 0x10)
// eschold=<n>        Esc held for n ticks
static bool access_drive(const std::string& cmd) {
    if (cmd.rfind("opt=", 0) == 0) {
        const std::string kv = cmd.substr(4);
        const std::size_t eq = kv.find('=');
        const bool ok = eq != std::string::npos && access::set_by_key(acc(), kv.substr(0, eq), kv.substr(eq + 1));
        std::printf("  [access] opt %s%s\n", kv.c_str(), ok ? "" : " (unknown key)");
        return true;
    }
    if (cmd == "esctap") { g_sdl_in.tapped[input::kDikEscape] = true; return true; }
    if (cmd.rfind("eschold=", 0) == 0) { g_drive_esc_hold = std::atoi(cmd.c_str() + 8); return true; }
    return false;
}
