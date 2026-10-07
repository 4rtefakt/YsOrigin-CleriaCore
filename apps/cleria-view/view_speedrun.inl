// Speedrun mode in the shell (build/shell_re/SPEEDRUN.md): the "Speedrun" tab of the F1 panel, the
// timer overlay (a Shell::hud layer), and the hooks the game loop calls. The logic lives in
// src/cleria/speedrun (the run, the splits, the auto-split observer) and src/cleria/livesplit (the
// LiveSplit Server client); this file is only the glue to the view: settings, drawing, input.
// CleriaCore's own layer: nothing here changes how the game plays.
#include "cleria/livesplit.hpp"
#include "cleria/speedrun.hpp"

struct SpeedrunUi {
    speedrun::Session s;
    speedrun::LiveSplitClient ls;
    std::string room;                 // the room of the last tick (the ASL block)
    int character = 0;                // g_flags[150] of the last tick (1 Yunica / 2 Hugo / 3 Toal)
    bool scroll_end = false;          // CLERIA_DRIVE speedrun=page_end: the tab scrolled to its end
    std::uint64_t cfg_rev = 0;        // the settings revision last applied
};
static SpeedrunUi& g_sr() {
    static SpeedrunUi u;
    return u;
}
static double sr_now() { return double(SDL_GetTicksNS()) / 1.0e6; }

// The settings -> the session and the LiveSplit client (on every settings change).
static void sr_apply_settings() {
    SpeedrunUi& u = g_sr();
    if (u.cfg_rev == settings().revision()) return;
    u.cfg_rev = settings().revision();
    const auto& c = settings().s.speedrun;
    u.s.cfg.auto_start = c.auto_start;
    u.s.cfg.auto_split = c.auto_split;
    u.s.cfg.timing = c.timing == 1 ? speedrun::Timing::Igt : speedrun::Timing::Rta;
    u.s.cfg.compare = c.compare == 1 ? speedrun::Compare::BestSegments : speedrun::Compare::PersonalBest;
    if (u.s.run.phase() == speedrun::Run::Phase::Idle) u.s.run.timing = u.s.cfg.timing;
    // The splits files sit beside the settings (CLERIA_INI's folder in tests); a memory-only run
    // (CLERIA_DRIVE without CLERIA_INI) keeps them in memory.
    u.s.dir = settings().memory_only() || settings().path().empty() ? fs::path()
                                                                     : settings().path().parent_path() / "speedrun";
    u.s.set_enabled(c.enabled, sr_now());
    u.ls.configure(c.enabled && c.livesplit, c.livesplit_host, c.livesplit_port);
    if (!u.s.livesplit) u.s.livesplit = [](std::string_view cmd) { g_sr().ls.send(cmd); };
}

// ---- hooks the game loop calls ---------------------------------------------------------------------
// The title's New Game, at the difficulty pick (speedrun.com/yso: "Time starts on difficulty select").
static void speedrun_new_game(int character, int skin) {
    sr_apply_settings();
    g_sr().s.new_game(character, skin != 0, sr_now());
}
// One retail logic step (beside ++g_flags[199], the play-time count FUN_004281c0 0x428274).
static void speedrun_tick(const std::string& room, bool gauge_visible, float gauge_hp, const std::int32_t* flags,
                          bool player_alive) {
    SpeedrunUi& u = g_sr();
    if (!u.s.cfg.enabled) return;
    speedrun::GameView v;
    v.room = room; v.gauge_visible = gauge_visible; v.gauge_hp = gauge_hp; v.flags = flags;
    v.player_alive = player_alive;
    u.room = room;
    if (flags && flags[150] >= 1 && flags[150] <= 3) u.character = flags[150];
    u.s.tick(v, sr_now());
}
// A start by hand (the tab's button, a drive): the category of the character being played. [H] a
// loaded save's Extra type is not known here: the plain category.
static void sr_start_now() {
    SpeedrunUi& u = g_sr();
    u.s.reset(sr_now());
    if (u.character != 0) u.s.select(u.character, false);
    u.s.start(sr_now());
}
// CleriaCore's fixed debug keys (view_game.inl's key handler): a use is remembered by the run.
static void speedrun_debug_key(SDL_Keycode k, SDL_Keymod mod, bool blocked) {
    if (blocked) return;
    const bool ctrl = (mod & SDL_KMOD_CTRL) != 0;
    const char* what = nullptr;
    if (k == SDLK_T) what = "Debug: test cutscene (T)";
    else if (k == SDLK_F) what = "Debug: free-fly (F)";
    else if (k == SDLK_R) what = "Debug: respawn (R)";
    else if (k == SDLK_K) what = "Debug: -60 HP (K)";
    else if (k == SDLK_L) what = "Debug: full heal (L)";
    else if (k == SDLK_B) what = "Debug: +Boost (B)";
    else if (k == SDLK_TAB && ctrl) what = "Debug: character switch";
    else if (k >= SDLK_1 && k <= SDLK_9 && ctrl) what = "Debug: exit warp (Ctrl+1-9)";
    if (what) speedrun::note_modification(what);
}

// The shell's Debug page (heal, fly, warp, flags): an activation on it during a run is remembered.
// Wrapped from here so the page itself needs no hook; `target<DebugWrap>()` marks it as done.
struct DebugWrap {
    std::function<void(ui::List&)> inner;
    void operator()(ui::List& L) const {
        const bool in_list = ui::hovering(L.org, {L.org.x + L.size.x, L.org.y + L.size.y});
        const bool act = L.focus && (L.nav.ok || L.nav.left || L.nav.right ||
                                     (in_list && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                                                  ImGui::IsMouseClicked(ImGuiMouseButton_Right))));
        inner(L);
        if (act && g_sr().s.run.running()) speedrun::note_modification("Debug page used");
    }
};
static void sr_wrap_debug_page(Shell& shell) {
    for (ui::Page& pg : shell.panel.pages)
        if (pg.name == "Debug" && !pg.draw.target<DebugWrap>()) pg.draw = DebugWrap{pg.draw};
}

// ---- the overlay -----------------------------------------------------------------------------------
static void sr_draw_overlay(ImDrawList* dl, float w, float h) {
    SpeedrunUi& u = g_sr();
    const auto& c = settings().s.speedrun;
    speedrun::Session& s = u.s;
    const speedrun::Run& run = s.run;
    const double now = sr_now();
    const float k = ui::S * std::clamp(c.scale, 0.5f, 2.0f);
    const float bw = 300.0f * k, pad = 12.0f * k;                       // [H] the box
    const float name_px = 15.0f * k, row_px = 15.5f * k, row_h = 22.0f * k;
    const float big_px = 40.0f * k, small_px = 14.5f * k;

    std::vector<bool> act;
    const std::vector<std::string> mods = s.modifications(&act);
    // which split rows: a window of up to kRows around the current split, the last one pinned
    constexpr int kRows = 7;                                            // [H]
    std::vector<int> rows;
    if (c.show_splits && run.count() > 0) {
        const int cur = std::min(run.index(), run.count() - 1);
        int first = std::clamp(cur - 2, 0, std::max(0, run.count() - kRows));
        for (int i = first; i < run.count() && int(rows.size()) < kRows; ++i) rows.push_back(i);
        if (rows.back() != run.count() - 1) rows.back() = run.count() - 1;
    }
    const bool notice = now - s.notice_ms < 4000.0 && !s.notice.empty();   // [H] 4 s
    float bh = pad + name_px + 8.0f * k;
    bh += float(rows.size()) * row_h + (rows.empty() ? 0.0f : 6.0f * k);
    if (c.show_rta) bh += big_px + 4.0f * k;
    if (c.show_igt) bh += small_px + 8.0f * k;
    if (notice) bh += small_px + 6.0f * k;
    bh += small_px + 6.0f * k + float(mods.size()) * (small_px + 3.0f * k);
    if (c.livesplit) bh += small_px + 6.0f * k;
    bh += pad;
    const float m = 16.0f * ui::S;
    const ImVec2 a{(c.corner == 1 || c.corner == 3) ? w - m - bw : m, (c.corner >= 2) ? h - m - bh : m};
    const ImVec2 b{a.x + bw, a.y + bh};
    dl->AddRectFilled(a, b, IM_COL32(16, 15, 11, 205), 6.0f * k);
    dl->AddRect(a, b, mods.empty() ? ui::kPanelEdge : ui::kWarn, 6.0f * k, 0, 1.2f * k);
    const float x0 = a.x + pad, x1 = b.x - pad;
    float y = a.y + pad;
    auto right = [&](ImFont* f, float px, float yy, ImU32 col, const std::string& t) {
        const ImVec2 ts = ui::text_size(f, px, t.c_str());
        ui::text(dl, f, px, {x1 - ts.x, yy}, col, t.c_str());
    };
    // the header: the category and the attempt
    ui::text(dl, ui::F.cond, name_px, {x0, y}, ui::kGold, ui::upper(run.route.category.c_str()).c_str());
    right(ui::F.body, name_px, y, ui::kMuted, "#" + std::to_string(std::max(run.route.attempts, 0)));
    y += name_px + 8.0f * k;
    // the splits
    const ImU32 kAhead = IM_COL32(110, 200, 120, 255), kBehind = IM_COL32(222, 98, 82, 255);
    for (int i : rows) {
        const bool cur = run.phase() != speedrun::Run::Phase::Idle && i == run.index();
        if (cur) dl->AddRectFilled({a.x + 4.0f * k, y - 2.0f * k}, {b.x - 4.0f * k, y + row_h - 4.0f * k},
                                   ui::kRowSel, 3.0f * k);
        const std::string& nm = run.route.segs[std::size_t(i)].name;
        ui::text(dl, ui::F.body, row_px, {x0, y}, cur ? ui::kTabOn : ui::kLabel, nm.c_str());
        std::string tv;
        ImU32 tc = ui::kMuted;
        if (i < run.index()) {                                         // done: its time and delta
            const double t = run.split_time(i);
            tv = t < 0 ? "-" : speedrun::format_time(t);
            tc = run.times()[std::size_t(i)].gold ? ui::kGold : ui::kValue;
            const double d = run.delta(i, s.cfg.compare);
            if (d != speedrun::Run::kNone) {
                const std::string ds = speedrun::format_delta(d);
                const ImU32 dc = run.times()[std::size_t(i)].gold ? ui::kGold : d <= 0 ? kAhead : kBehind;
                const float tw = ui::text_size(ui::F.body, row_px, tv.c_str()).x;
                const ImVec2 dsz = ui::text_size(ui::F.medium, row_px, ds.c_str());
                ui::text(dl, ui::F.medium, row_px, {x1 - tw - 14.0f * k - dsz.x, y}, dc, ds.c_str());
            }
        } else {                                                        // ahead: the comparison
            const double cmp = run.comparison(i, s.cfg.compare);
            tv = cmp < 0 ? "-" : speedrun::format_time(cmp);
        }
        right(ui::F.body, row_px, y, tc, tv);
        y += row_h;
    }
    if (!rows.empty()) y += 6.0f * k;
    // the timer (the chosen timing big; RTA is speedrun.com's)
    if (c.show_rta) {
        const double t = run.rta(now);
        ImU32 col = ui::kTabOn;
        if (run.phase() == speedrun::Run::Phase::Idle) col = ui::kMuted;
        else if (run.phase() == speedrun::Run::Phase::Finished) col = run.new_pb() ? ui::kGold : ui::kValue;
        else if (run.index() < run.count()) {                          // live: against the comparison
            const double cmp = run.comparison(run.index(), s.cfg.compare);
            const double nowt = run.now_time(now);
            if (cmp >= 0) col = nowt <= cmp ? kAhead : kBehind;
        }
        right(ui::F.bold, big_px, y, col, speedrun::format_time(t));
        ui::text(dl, ui::F.cond, small_px, {x0, y + big_px - small_px - 6.0f * k}, ui::kSection, "RTA");
        y += big_px + 4.0f * k;
    }
    if (c.show_igt) {
        ui::text(dl, ui::F.cond, small_px, {x0, y}, ui::kSection, "IGT");
        right(ui::F.medium, small_px + 2.0f * k, y - 1.0f * k, ui::kValue,
              speedrun::format_play_time(run.phase() == speedrun::Run::Phase::Idle ? 0 : std::uint64_t(run.igt())));
        y += small_px + 8.0f * k;
    }
    if (notice) {
        ui::text(dl, ui::F.body, small_px, {x0, y}, ui::kDesc, s.notice.c_str());
        y += small_px + 6.0f * k;
    }
    // the modifications: the verification line
    dl->AddLine({x0, y - 2.0f * k}, {x1, y - 2.0f * k}, ui::kDivider, 1.0f * k);
    y += 3.0f * k;
    if (mods.empty()) {
        ui::text(dl, ui::F.cond, small_px, {x0, y}, ui::kGood, "RETAIL RULES - NO MODIFICATIONS");
    } else {
        ui::text(dl, ui::F.cond, small_px, {x0, y}, ui::kWarn, "MODIFIED");
        y += small_px + 3.0f * k;
        for (std::size_t i = 0; i < mods.size(); ++i) {
            const std::string line = "\xE2\x80\xA2 " + mods[i] + (act[i] ? "" : "  (used this run)");
            ui::text(dl, ui::F.body, small_px, {x0 + 4.0f * k, y}, act[i] ? ui::kDesc : ui::kMuted, line.c_str());
            y += small_px + 3.0f * k;
        }
        y -= small_px + 3.0f * k;
    }
    y += small_px + 6.0f * k;
    if (c.livesplit)
        ui::text(dl, ui::F.body, small_px * 0.92f, {x0, y}, u.ls.connected() ? ui::kGood : ui::kMuted,
                 ("LiveSplit: " + u.ls.status()).c_str());
}

// The per-frame part (every rendered frame, title and launcher too): settings, LiveSplit, the ASL
// block, the Debug page wrap, then the overlay.
static void sr_hud_layer(Shell& shell, ImDrawList* dl, float w, float h) {
    sr_apply_settings();
    SpeedrunUi& u = g_sr();
    const double now = sr_now();
    u.ls.poll(now);
    u.s.frame(now, u.room);
    sr_wrap_debug_page(shell);
    if (settings().s.speedrun.enabled) sr_draw_overlay(dl, w, h);
}

// ---- the Speedrun tab --------------------------------------------------------------------------
static void sr_page(ui::List& L) {
    sr_apply_settings();
    SpeedrunUi& u = g_sr();
    speedrun::Session& s = u.s;
    auto& c = settings().s.speedrun;
    auto persist = [] { settings().touch(); settings().save(); };
    const double now = sr_now();
    char buf[200];
    if (u.scroll_end) L.scroll = L.scroll_to = std::max(0.0f, L.content_h - L.size.y);   // last frame's height
    ui::section(L, "Speedrun mode");
    if (ui::toggle(L, "Speedrun mode", &c.enabled,
                   "Shows a run timer, your splits and every non-retail option in use on screen, so a run can be "
                   "verified. Timing follows speedrun.com: the run starts when you pick a New Game's difficulty. "
                   "Display only: the game plays exactly as the original. Not a retail option."))
        persist();
    if (!c.enabled) {
        ui::info(L, "", "Turn Speedrun mode on for the timer, splits and LiveSplit.", ui::kMuted);
        return;
    }
    sr_apply_settings();
    ui::section(L, "Run");
    std::snprintf(buf, sizeof buf, "%s  -  attempt %d, %d finished", s.run.route.category.c_str(), s.run.route.attempts,
                  s.run.route.completed);
    ui::info(L, "Category", buf);
    {
        const speedrun::Run& r = s.run;
        if (r.phase() == speedrun::Run::Phase::Idle) std::snprintf(buf, sizeof buf, "Waiting for a New Game");
        else if (r.phase() == speedrun::Run::Phase::Finished)
            std::snprintf(buf, sizeof buf, "Finished in %s%s", speedrun::format_time(r.split_time(r.count() - 1)).c_str(),
                          r.new_pb() ? " - personal best!" : "");
        else
            std::snprintf(buf, sizeof buf, "%s  -  split %d / %d: %s", speedrun::format_time(r.rta(now)).c_str(),
                          r.index() + 1, r.count(), r.route.segs[std::size_t(r.index())].name.c_str());
        ui::info(L, "Status", buf, r.running() ? ui::kGood : ui::kValue);
        const double sob = r.sum_of_best();
        if (sob >= 0) ui::info(L, "Sum of best", speedrun::format_time(sob).c_str());
    }
    if (ui::button(L, "Start run now", "Start (or restart) the timer now. A run starts by itself when you pick the "
                                       "difficulty of a New Game; use this for a run from a loaded save."))
        sr_start_now();
    if (ui::button(L, "Split", "Split by hand (also what a split with Event=manual waits for).", s.run.running()))
        s.split(now);
    if (ui::button(L, "Undo split", "Take the last split back.", s.run.index() > 0)) s.undo(now);
    if (ui::button(L, "Skip split", "Move on without a time for this split.", s.run.running())) s.skip(now);
    if (ui::button(L, "Reset run", "Stop and clear the run. Its best segments are kept; a finished run's personal "
                                   "best was already saved.", s.run.phase() != speedrun::Run::Phase::Idle))
        s.reset(now);
    ui::section(L, "Timer");
    bool ch = false;
    ch |= ui::toggle(L, "Show real time", &c.show_rta, "The big timer: real time (RTA), what speedrun.com ranks.");
    ch |= ui::toggle(L, "Show in-game time", &c.show_igt,
                     "In-game time: the original game's own Play Time counter (60 steps a second; menus, the pause "
                     "book and load stalls do not count), from the start of the run.");
    ch |= ui::toggle(L, "Show splits", &c.show_splits, "The split list with your times and deltas.");
    static const char* kCorners[] = {"Top left", "Top right", "Bottom left", "Bottom right"};
    ch |= ui::choice(L, "Position", &c.corner, kCorners, 4, "Where the timer sits on the screen.");
    float pct = std::round(c.scale * 100.0f);
    if (ui::slider(L, "Size", &pct, 50.0f, 200.0f, 10.0f, "%.0f%%", "The timer's size.")) {
        c.scale = pct / 100.0f;
        ch = true;
    }
    static const char* kTiming[] = {"Real time (RTA)", "In-game time (IGT)"};
    ch |= ui::choice(L, "Timing", &c.timing, kTiming, 2,
                     "Which time decides your personal best and the deltas. speedrun.com ranks Ys Origin by real "
                     "time. Takes effect at the next run.");
    static const char* kCompare[] = {"Personal best", "Best segments"};
    ch |= ui::choice(L, "Compare against", &c.compare, kCompare, 2,
                     "The deltas: against your personal best run, or against the sum of your best segments.");
    ui::section(L, "Auto-splitter");
    ch |= ui::toggle(L, "Start on New Game", &c.auto_start,
                     "Start the run when the difficulty of a New Game is picked (speedrun.com's start).");
    ch |= ui::toggle(L, "Auto-split", &c.auto_split,
                     "Split by itself on the events in your splits file: bosses defeated, rooms entered, items found.");
    const fs::path rp = s.route_path();
    ui::info(L, "Splits file", rp.empty() ? "(not saved in this session)" : fs::path(rp).make_preferred().string().c_str());
    if (ui::button(L, "Reload splits file",
                   "Read your splits file again after editing it (names, events). Only between runs.",
                   s.run.phase() == speedrun::Run::Phase::Idle))
        s.reload();
    ui::section(L, "LiveSplit");
    ch |= ui::toggle(L, "Connect to LiveSplit", &c.livesplit,
                     "Mirror the run into LiveSplit: start, splits, resets and the in-game time. In LiveSplit, "
                     "right-click > Control > Start TCP Server. The address is LiveSplitHost / LiveSplitPort in "
                     "cleria.ini.");
    std::snprintf(buf, sizeof buf, "%s:%d", c.livesplit_host.c_str(), int(c.livesplit_port));
    ui::info(L, "Server", buf);
    ui::info(L, "Connection", u.ls.status().c_str(), u.ls.connected() ? ui::kGood : ui::kMuted);
    ui::section(L, "Modifications in use");
    std::vector<bool> act;
    const auto mods = s.modifications(&act);
    if (mods.empty()) ui::info(L, "", "None - retail rules", ui::kGood);
    for (std::size_t i = 0; i < mods.size(); ++i)
        ui::info(L, act[i] ? "Active" : "Used this run", mods[i].c_str(), act[i] ? ui::kWarn : ui::kMuted);
    if (ch) persist();
}

// Register the tab and the overlay. `cheats` lists the shell's Cheats page switches that are on
// (view_game.inl owns those flags); the other CleriaCore options this file can see are listed here.
// Other features add their own lines with speedrun::set_provider("<feature>", ...).
static void speedrun_shell(Shell& shell, speedrun::Provider cheats) {
    speedrun::set_provider("cheats", std::move(cheats));
    // g_time_scale (0x6d6078) is the game's own: boss slow-mo and scripts (op 0x95 SetTimeScale, 0.3-1.0 in
    // S_3080's victory scenes) write it, so it is not a modification. The shell's Game speed is
    // access.game_speed (ticks per second), listed with the cheats.
    speedrun::set_provider("qol", [](std::vector<std::string>& out) {
        // Skips a movie the original only lets you skip after a clear: it saves time.
        if (settings().s.cleria.unlock_movie_skip) out.push_back("Movie skipping unlocked");
    });
    shell.panel.add("Speedrun", sr_page);
    shell.hud["speedrun"] = [&shell](ImDrawList* dl, float w, float h) { sr_hud_layer(shell, dl, w, h); };
}

// CLERIA_DRIVE: speedrun=on|off|start|split|skip|undo|reset|status|page. True when it was one.
static bool speedrun_drive(const std::string& cmd, Shell& shell) {
    if (cmd.rfind("speedrun=", 0) != 0) {
        // A drive command that moves the player or writes game state is a modification of the run.
        static const char* const kEdits[] = {"at=", "kill1", "flag=", "boss", "near=", "hp=", "sp=", "run=",
                                             "water", "bracelet=", "obj="};
        for (const char* p : kEdits)
            if (cmd.rfind(p, 0) == 0) { speedrun::note_modification("CLERIA_DRIVE " + cmd); break; }
        return false;
    }
    const std::string a = cmd.substr(9);
    SpeedrunUi& u = g_sr();
    const double now = sr_now();
    if (a == "on" || a == "off") {
        settings().s.speedrun.enabled = a == "on";
        settings().touch();
        sr_apply_settings();
    } else if (a == "start") sr_start_now();
    else if (a == "split") u.s.split(now);
    else if (a == "skip") u.s.skip(now);
    else if (a == "undo") u.s.undo(now);
    else if (a == "reset") u.s.reset(now);
    else if (a == "page" || a == "page_end") {
        u.scroll_end = a == "page_end";
        for (int i = 0; i < int(shell.panel.pages.size()); ++i)
            if (shell.panel.pages[std::size_t(i)].name == "Speedrun") shell.panel.tab = i;
    }
    const speedrun::Run& r = u.s.run;
    std::string mods;
    for (const std::string& m : u.s.modifications()) mods += (mods.empty() ? "" : ", ") + m;
    std::printf("  [speedrun] phase %d split %d/%d rta %s igt %s (%llu steps) mods [%s]\n", int(r.phase()), r.index(),
                r.count(), speedrun::format_time(r.rta(now)).c_str(),
                speedrun::format_play_time(std::uint64_t(r.igt())).c_str(), (unsigned long long)r.igt_ticks(),
                mods.c_str());
    return true;
}
