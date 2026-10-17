// cleria-view — mods: the startup mount and the F1 panel's "Mods" tab (build/shell_re/MODS.md)
// Textual part of main.cpp: #included after view_shell.inl (the ui kit) and view_input.inl
// (settings()); ONE translation unit, not standalone.
//
// At startup (render_window, right after settings_init) the game's folders are mounted in the vfs
// and the enabled mods laid over them; that set is fixed for the session (mods::active()). The tab
// edits cleria.ini [cleria.mods] (the order and the enabled ids), which applies on the next start.
//
// The tab has sub-pages (SHELL_CORE.md's tab policy: no new top-level tabs): "Mods > <mod>" (each mod's
// page: what a running Lua mod registered with cleria.ui.page, then its details, its On / Off and its load
// order) and "Mods > Content additions" (view_content.inl's content_page). CLERIA_SHELL_PAGE=Mods:<mod id>
// or Mods:content opens one (ui::Panel::select).
#include "cleria/mods.hpp"
#ifdef _WIN32
#include <shellapi.h>   // ShellExecuteW: "Open mods folder" (WIN32_LEAN_AND_MEAN leaves it out)
#endif

// A page a running Lua mod registered (cleria.ui.page, view_lua.inl): drawn on its Mods > <mod> page.
struct ModsPage {
    std::string mod_id, title;
    std::function<void(ui::List&)> draw;
};

struct ModsUi {
    bool on = false;                  // mods are possible this session (a settings file is in use)
    fs::path dir;                     // the mods folder
    std::vector<mods::Mod> list;      // the tab's view: what is installed + the pending choices
    bool use = true;                  // the pending UseMods
    std::string msg;                  // the last action's result
    std::vector<ModsPage> pages;      // the Lua mods' pages
    std::string sub;                  // the sub-page shown: "" = the list, "content", or a mod id
    std::optional<std::string> next;  // a sub-page to switch to at the start of the next frame
    int list_sel = 0;                 // the list's selection / scroll, kept while a sub-page is open
    float list_scroll = 0;
};
static ModsUi g_mods_ui;
static bool mods_pending(const ModsUi& u);
static void mods_ui_script(ModsUi& u);
static void content_page(ui::List& L);   // Mods > Content additions (view_content.inl)
static std::string content_summary();    // its row's value (view_content.inl)
static void modfile_rows(ui::List& L, ModsUi& u);                    // Install a mod file..., the association (view_modfile.inl)
static void modfile_install_ui(ModsUi& u, const fs::path& file);     // CLERIA_MODS_UI install=<file>
static void modfile_msg_row(ui::List& L);                            // the last install's result
static void modfile_request_restart();                               // Restart now (view_modfile.inl)
// Get mods, updates, Remove (view_modget.inl)
static void modget_take(ModsUi& u);
static void modget_rows(ui::List& L, ModsUi& u);
static void modget_page(ui::List& L, ModsUi& u);
static void modget_mod_rows(ui::List& L, const mods::Mod& m);
static bool modget_website_row(ui::List& L, const mods::Mod& m);
static void modget_remove_rows(ui::List& L, ModsUi& u, const mods::Mod& m);
static bool modget_ui_op(ModsUi& u, const std::string& op, const std::string& id);
static std::optional<std::string> modget_update_version(const std::string& id);

static mods::Prefs mods_prefs_from_settings() {
    const auto& c = settings().s.cleria;
    return {c.use_mods, mods::split_list(c.mod_order), mods::split_list(c.mod_active)};
}

static void mods_index(Gpu& g);
// Mount the game's folders and lay the enabled mods over them. Before any asset is loaded.
static void mods_startup(Gpu& g, const fs::path& asset_root) {
    vfs::reset();
    vfs::mount("", asset_root);
    {   // the loose install folders: music/<track>.ogg and movie/<file> are mod paths too
        BgmDefs b;
        if (b.load(asset_root) && !b.dir().empty()) vfs::mount("music", b.dir());
        if (const fs::path m = find_movie(asset_root, 0); !m.empty()) vfs::mount("movie", m.parent_path());
    }
    ModsUi& u = g_mods_ui;
    if (settings().memory_only() || settings().path().empty()) {
        // headless runs without CLERIA_INI (goldens, drive_regress) are always unmodded
        std::printf("[mods] off (no settings file)\n");
        return;
    }
    u.on = true;
    u.dir = mods::default_dir(settings().path());
    const mods::Prefs prefs = mods_prefs_from_settings();
    u.use = prefs.use;
    u.list = mods::load(u.dir, prefs);
    std::printf("[mods] folder %s: %zu installed, active: %s (%zu files)\n", u.dir.string().c_str(),
                u.list.size(), mods::active_summary().c_str(), vfs::table().size());
    for (const mods::Mod& m : u.list) {
        for (const std::string& e : m.errors) std::printf("[mods]   %s: %s\n", m.label().c_str(), e.c_str());
        for (const std::string& n : m.notes) std::printf("[mods]   %s: %s\n", m.label().c_str(), n.c_str());
    }
    mods_ui_script(u);
    mods_index(g);
}

// Textures and rooms a mod ADDS (new names) join the name indexes like build_indexes does; a
// replaced file keeps its base index entry and vfs::resolve swaps it at the open.
static void mods_index(Gpu& g) {
    for (const auto& [k, e] : vfs::table()) {
        const fs::path vp(k);
        const std::string ext = vp.extension().string();
        if (ext == ".dds") {
            g.dds_index.emplace(vp.filename().string(), e.file);
        } else if (ext == ".sob") {
            std::string stem = vp.stem().string();
            for (char& c : stem) c = char(std::toupper((unsigned char)c));
            g.sob_index.emplace(stem, e.file);
        }
    }
}

// [game] Language (build/shell_re/CONFIG_PARITY.md §7): retail mounts one overlay archive per language over
// release/data (FUN_005d68d0); here that archive's unpacked folder (gamedata::language_dir) is laid over the
// asset root, under the mods. English is the data folder itself. A language whose folder is missing runs in
// English, as retail falls back (FUN_0040ccf0). After mods_startup (which resets the vfs), before any load.
static int g_language = 1;   // the language this session runs in
static void language_startup(const fs::path& asset_root) {
    const int want = settings().s.game.language;
    const std::size_t n = vfs::set_language(gamedata::language_dir(asset_root, want));
    g_language = n ? want : 1;
    if (want != 1)
        std::printf("[language] Language=%d: %s (%zu files)\n", want, gamedata::language(g_language)->name, n);
}

// Persist the tab's order / choices; recheck the list.
static void mods_apply(ModsUi& u, mods::Prefs p) {
    p.use = u.use;
    mods::arrange(u.list, p);
    auto& c = settings().s.cleria;
    c.use_mods = u.use;
    c.mod_order = mods::join_list(p.order);
    c.mod_active = mods::join_list(p.active);
    settings().touch();
    settings().save();
}

// The tab's two actions. Turning a mod on also turns UseMods on.
static void mods_toggle(ModsUi& u, int i) {
    const std::string id = u.list[std::size_t(i)].m.id;
    if (id.empty()) return;
    mods::Prefs p = mods::prefs_of(u.list, u.use);
    const auto it = std::find(p.active.begin(), p.active.end(), id);
    const bool turn_on = it == p.active.end();
    if (turn_on) p.active.push_back(id);
    else p.active.erase(it);
    if (turn_on) u.use = true;
    mods_apply(u, p);
}
static bool mods_move(ModsUi& u, int i, int d) {
    const int j = i + d;
    if (i < 0 || j < 0 || i >= int(u.list.size()) || j >= int(u.list.size())) return false;
    std::swap(u.list[std::size_t(i)], u.list[std::size_t(j)]);
    mods_apply(u, mods::prefs_of(u.list, u.use));
    return true;
}

// CLERIA_MODS_UI="toggle=<id>;up=<id>;down=<id>" (headless checks): the tab's actions, once, after the
// startup load, as if clicked; they persist like the tab's own edits.
static void mods_ui_script(ModsUi& u) {
    const char* e = std::getenv("CLERIA_MODS_UI");
    if (!e || !u.on) return;
    const std::string all = e;
    std::size_t p0 = 0;
    while (p0 < all.size()) {
        std::size_t p1 = all.find(';', p0);
        if (p1 == std::string::npos) p1 = all.size();
        const std::string it = all.substr(p0, p1 - p0);
        p0 = p1 + 1;
        const std::size_t eq = it.find('=');
        if (eq == std::string::npos) continue;
        const std::string op = it.substr(0, eq), id = it.substr(eq + 1);
        if (op == "install") { modfile_install_ui(u, fs::path(id)); continue; }   // the tab's Install a mod file...
        if (op == "drop") { modfile_dropped(id.c_str()); continue; }               // a file dropped on the window
        if (op == "restart") { modfile_request_restart(); continue; }              // Restart now (a drive run only prints)
        if (modget_ui_op(u, op, id)) continue;                                      // get / check / update / remove
        int i = -1;
        for (int k = 0; k < int(u.list.size()); ++k)
            if (u.list[std::size_t(k)].m.id == id) i = k;
        if (i < 0) { std::printf("[mods] ui: no mod %s\n", id.c_str()); continue; }
        if (op == "toggle") mods_toggle(u, i);
        else if (op == "up") mods_move(u, i, -1);
        else if (op == "down") mods_move(u, i, +1);
        std::printf("[mods] ui: %s %s -> Order=\"%s\" Active=\"%s\" (%s)\n", op.c_str(), id.c_str(),
                    settings().s.cleria.mod_order.c_str(), settings().s.cleria.mod_active.c_str(),
                    mods_pending(u) ? "restart to apply" : "as loaded");
    }
}

// True when the choices differ from what this session loaded (the change needs a restart).
static bool mods_pending(const ModsUi& u) {
    std::vector<std::string> now, then;
    for (const mods::Mod& m : u.list)
        if (m.state == mods::State::Active) now.push_back(m.m.id);
    for (const mods::Info& i : mods::active()) then.push_back(i.id);
    return now != then;
}

static std::string mods_deps(const std::vector<mods::Dep>& v) {
    std::string s;
    for (const mods::Dep& x : v) s += (s.empty() ? "" : ", ") + x.id + (x.op.empty() ? "" : " " + x.op + " " + x.version);
    return s;
}

static std::string mods_desc(const mods::Mod& m) {
    std::string d = m.label();
    if (!m.m.version.empty()) d += "  " + m.m.version;
    if (!m.m.author.empty()) d += "\nby " + m.m.author;
    if (!m.m.description.empty()) d += "\n\n" + m.m.description;
    d += "\n\n" + (m.m.id.empty() ? std::string("(no id)") : m.m.id) + "  \xE2\x80\xA2  " + std::to_string(m.files) +
         " file(s)  \xE2\x80\xA2  " + m.source.filename().string() + (m.zip ? "" : "/");
    if (!m.m.requires_.empty()) d += "\nRequires: " + mods_deps(m.m.requires_);
    if (!m.m.conflicts.empty()) d += "\nConflicts with: " + mods_deps(m.m.conflicts);
    for (const std::string& e : m.errors) d += "\nError: " + e;
    for (const std::string& n : m.notes) d += "\n" + n;
    return d;
}

// The Lua pages of a mod (empty: none, or its script does not run this session).
static int mods_page_count(const ModsUi& u, const std::string& id) {
    int n = 0;
    for (const ModsPage& p : u.pages) n += p.mod_id == id;
    return n;
}

// Register a running Lua mod's page (lua_shell): it shows on that mod's Mods > <mod> page.
static void mods_add_page(const std::string& mod_id, const std::string& title, std::function<void(ui::List&)> draw) {
    g_mods_ui.pages.push_back({mod_id, title, std::move(draw)});
}

// Show a sub-page ("" = back to the list): the list keeps its selection and scroll meanwhile.
static void mods_switch(ModsUi& u, ui::List& L, const std::string& key) {
    if (u.sub.empty() && !key.empty()) {
        u.list_sel = L.sel;
        u.list_scroll = L.scroll_to;
        L.sel = 0;
        L.scroll = L.scroll_to = 0;
    } else if (!u.sub.empty() && key.empty()) {
        L.sel = u.list_sel;
        L.scroll = L.scroll_to = u.list_scroll;
    } else {
        L.sel = 0;
        L.scroll = L.scroll_to = 0;
    }
    L.count_last = 0;                        // the other page's rows do not steer this frame's keys
    u.sub = key;
    if (std::getenv("CLERIA_DRIVE")) std::printf("[mods] page: Mods%s%s\n", key.empty() ? "" : " > ", key.c_str());
}
// Esc / Backspace / the pad's Back on the Mods tab (ui::Page::back): a sub-page returns to the list.
static bool mods_back(ui::List& L) {
    ModsUi& u = g_mods_ui;
    if (u.sub.empty()) return false;
    mods_switch(u, L, "");
    u.next.reset();
    return true;
}
// "content" or a mod id (ui::Page::open_sub: CLERIA_SHELL_PAGE=Mods:<key>, the drive's shell=Mods:<key>).
static bool mods_open_sub(const std::string& key) {
    ModsUi& u = g_mods_ui;
    bool ok = key == "content" || key == "get";
    for (const mods::Mod& m : u.list) ok |= !m.m.id.empty() && m.m.id == key;
    if (ok) u.next = key;
    else std::printf("[shell] Mods has no sub-page '%s'\n", key.c_str());
    return ok;
}

// A small gear: the mod has settings of its own (a Lua page).
static void mods_gear(ImDrawList* dl, ImVec2 c, ImU32 col) {
    const float S = ui::S;
    for (int k = 0; k < 8; ++k) {
        const float a = float(k) * 0.785398f;
        dl->AddLine({c.x + std::cos(a) * 4.0f * S, c.y + std::sin(a) * 4.0f * S},
                    {c.x + std::cos(a) * 6.6f * S, c.y + std::sin(a) * 6.6f * S}, col, 2.0f * S);
    }
    dl->AddCircle(c, 4.0f * S, col, 16, 1.7f * S);
}

// One mod in the list: "N  NAME  version" on the left; the up / down arrows, a gear when it has settings,
// its state and a chevron on the right. OK / a click opens its page; a click on the state turns it on / off;
// Left / Right or the arrows move it in the load order.
static void mods_row(ui::List& L, ModsUi& u, int i) {
    mods::Mod& m = u.list[std::size_t(i)];
    const bool has_page = mods_page_count(u, m.m.id) > 0;
    const std::string desc = mods_desc(m) +
        "\n\nOK / click opens its page (Mods > " + m.label() + (has_page ? "): its settings, " : "): ") +
        "On / Off, the load order and every detail. A click on On / Off turns it on or off here. Left / Right or the "
        "arrows move it up / down the load order: when two mods change the same file, the one lower in the list "
        "wins. Changes apply when CleriaCore restarts.";
    const bool rclick = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    ui::RowIo r = ui::row_frame(L, desc.c_str(), true);
    const float S = ui::S, cy = (r.a.y + r.b.y) * 0.5f;
    // the arrows (mouse); a click on one does not open the page
    int move = 0;
    const float ax = r.b.x - 150.0f * S;
    for (int k = 0; k < 2; ++k) {
        const ImVec2 c{ax + float(k) * 24.0f * S, cy};
        const ImVec2 a{c.x - 10.0f * S, r.a.y + 2.0f * S}, b{c.x + 10.0f * S, r.b.y - 2.0f * S};
        const bool hov = L.focus && ui::hovering(a, b);
        if (hov) ui::hand();
        if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { move = k == 0 ? -1 : 1; r.activate = false; }
        const ImU32 col = hov ? ui::kTabOn : (r.selected ? ui::kTabOff : ui::kRowEdge);
        const float h = 5.0f * S, w = 6.0f * S;
        if (k == 0) L.dl->AddTriangleFilled({c.x - w, cy + h * 0.6f}, {c.x + w, cy + h * 0.6f}, {c.x, cy - h}, col);
        else L.dl->AddTriangleFilled({c.x - w, cy - h * 0.6f}, {c.x + w, cy - h * 0.6f}, {c.x, cy + h}, col);
    }
    if (r.delta != 0 && !rclick) move = r.delta;
    // the texts
    const float lpx = 16.5f * S, vpx = 17.0f * S;
    char num[8];
    std::snprintf(num, sizeof num, "%d", i + 1);
    float x = r.a.x + 13.0f * S;
    ui::text(L.dl, ui::F.cond, lpx, {x, cy - lpx * 0.55f}, ui::kMuted, num);
    x += 24.0f * S;
    const std::string name = ui::upper(m.label().c_str());
    ui::text(L.dl, ui::F.cond, lpx, {x, cy - lpx * 0.55f}, ui::kLabel, name.c_str());
    x += ui::text_size(ui::F.cond, lpx, name.c_str()).x + 10.0f * S;
    {   // its version, its update ("1.0.0 › 1.1.0") or why it cannot load
        const bool bad = m.state == mods::State::Error && !m.errors.empty();
        const std::optional<std::string> up = modget_update_version(m.m.id);
        const std::string note = bad ? m.errors.front() : up ? m.m.version + "  \xE2\x80\xBA  " + *up : m.m.version;
        L.dl->PushClipRect({x, r.a.y}, {ax - 18.0f * S, r.b.y}, true);
        ui::text(L.dl, ui::F.body, 15.0f * S, {x, cy - 15.0f * S * 0.55f}, bad ? ui::kWarn : up ? ui::kGold : ui::kMuted, note.c_str());
        L.dl->PopClipRect();
    }
    if (has_page) mods_gear(L.dl, {r.b.x - 100.0f * S, cy}, r.selected ? ui::kTabOn : ui::kMuted);
    // the state: a click on it toggles
    const char* v = m.state == mods::State::Error ? "Error" : m.stopped ? "Stopped" : m.enabled ? "On" : "Off";
    const ImU32 vc = m.state == mods::State::Error || m.stopped ? ui::kWarn : m.enabled ? ui::kGood : ui::kMuted;
    ImFont* vf = r.selected ? ui::F.medium : ui::F.body;
    const ImVec2 vs = ui::text_size(vf, vpx, v);
    const float vx = r.b.x - 32.0f * S - vs.x;
    const ImVec2 sa{vx - 8.0f * S, r.a.y + 3.0f * S}, sb{r.b.x - 26.0f * S, r.b.y - 3.0f * S};
    bool toggle = false;
    if (L.focus && ui::hovering(sa, sb)) {
        ui::hand();
        L.dl->AddRect(sa, sb, ui::kRowEdge, 3.0f * S, 0, 1.0f * S);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { toggle = true; r.activate = false; }
    }
    ui::text(L.dl, vf, vpx, {vx, cy - vpx * 0.55f}, vc, v);
    {   // the chevron: OK opens the page
        const float cx = r.b.x - 15.0f * S;
        const ImU32 cc = r.selected ? ui::kGold : ui::kTabOff;
        L.dl->AddLine({cx - 3.0f * S, cy - 5.0f * S}, {cx + 2.0f * S, cy}, cc, 1.6f * S);
        L.dl->AddLine({cx + 2.0f * S, cy}, {cx - 3.0f * S, cy + 5.0f * S}, cc, 1.6f * S);
    }
    // the actions
    if (toggle) mods_toggle(u, i);
    else if (r.activate && !m.m.id.empty()) u.next = m.m.id;
    else if (move != 0 && mods_move(u, i, move)) L.sel += move;   // the selection follows the mod
}

// Mods > <mod>: its own settings (the pages its Lua script registered), then what the list's row and its
// description held: On / Off, the load order, the manifest, the errors and notes.
static void mods_mod_page(ui::List& L, ModsUi& u) {
    int i = -1;
    for (int k = 0; k < int(u.list.size()); ++k)
        if (u.list[std::size_t(k)].m.id == u.sub) i = k;
    if (i < 0) { u.next = ""; return; }              // gone (a rescan)
    mods::Mod& m = u.list[std::size_t(i)];
    if (ui::back_row(L, "Mods", m.label().c_str(), mods_desc(m).c_str())) u.next = "";
    modget_mod_rows(L, m);
    const int n_pages = mods_page_count(u, m.m.id);
    for (ModsPage& p : u.pages) {
        if (p.mod_id != m.m.id) continue;
        if (n_pages > 1) ui::section(L, p.title.c_str());
        p.draw(L);
    }
    bool running = false;
    for (const mods::Info& a : mods::active()) running |= a.id == m.m.id;
    if (n_pages == 0 && !m.m.script.empty()) {
        ui::section(L, "Settings");
        ui::info(L, "Settings", running ? "None" : "Turn it on and restart", ui::kMuted,
                 running ? "This mod's script registered no page of settings."
                         : "This mod runs a Lua script; its settings show here once it is on and CleriaCore has restarted.");
    }
    ui::section(L, "Mod");
    bool on = m.enabled;
    if (ui::toggle(L, "Enabled", &on, "Turns the mod on or off. New mods start off, so a dropped-in folder never changes "
                   "the game by itself. Changes apply when CleriaCore restarts. Not a retail option."))
        mods_toggle(u, i);
    {
        const bool pend = m.enabled != running && m.state != mods::State::Error;
        const char* st = m.state == mods::State::Error ? "Error" : m.stopped ? "Stopped by an error"
                       : pend ? (m.enabled ? "On after a restart" : "Off after a restart")
                       : running ? "Running" : "Off";
        ui::info(L, "State", st, m.state == mods::State::Error || m.stopped || pend ? ui::kWarn : running ? ui::kGood : ui::kMuted);
    }
    {   // the load order: Left / Right move it
        char pos[32];
        std::snprintf(pos, sizeof pos, "%d of %d", i + 1, int(u.list.size()));
        const ui::RowIo r = ui::row_frame(L, "Its place in the load order. Left / Right move it up / down: when two mods "
                                             "change the same file, the one lower in the list wins.", true);
        ui::row_texts(L, r, "Load order", pos, true);
        if (r.delta != 0) mods_move(u, i, r.delta);
    }
    ui::section(L, "About");
    if (!m.m.version.empty()) ui::info(L, "Version", m.m.version.c_str());
    if (!m.m.author.empty()) ui::info(L, "Author", m.m.author.c_str());
    modget_website_row(L, m);
    ui::info(L, "Id", m.m.id.c_str());
    {
        const std::string src = m.source.filename().string() + (m.zip ? "" : "/");
        ui::info(L, "Source", src.c_str(), ui::kValue, m.source.string().c_str());
    }
    ui::info(L, "Files", std::to_string(m.files).c_str(), ui::kValue, "Game files it replaces or adds (its files/ folder).");
    if (!m.m.script.empty()) ui::info(L, "Script", m.m.script.c_str(), ui::kValue, "Its Lua entry file (LUA_API.md).");
    if (!m.m.requires_.empty()) ui::info(L, "Requires", mods_deps(m.m.requires_).c_str());
    if (!m.m.conflicts.empty()) ui::info(L, "Conflicts with", mods_deps(m.m.conflicts).c_str());
    for (const std::string& e : m.errors) ui::info(L, "Error", e.c_str(), ui::kWarn, e.c_str());
    for (const std::string& n : m.notes) ui::info(L, "Note", n.c_str(), ui::kMuted, n.c_str());
    modget_remove_rows(L, u, m);
}

static void mods_page(ui::List& L) {
    ModsUi& u = g_mods_ui;
    if (u.next) {                                     // a sub-page asked for last frame: switch now
        mods_switch(u, L, *u.next);
        u.next.reset();
    }
    if (u.sub == "content") {                         // Mods > Content additions (view_content.inl)
        if (ui::back_row(L, "Mods", "Content additions",
                         "What the active mods add to or change in the game, and the mod items this save holds. Not a "
                         "retail feature.\n\nOK / click here, Esc, Backspace or the pad's Back return to the Mods list."))
            u.next = "";
        content_page(L);
        return;
    }
    if (u.on) modget_take(u);                         // downloads finished since the last frame
    if (u.sub == "get") { modget_page(L, u); return; }   // Mods > Get mods (view_modget.inl)
    if (!u.sub.empty()) { mods_mod_page(L, u); return; }
    if (!u.on) {
        ui::section(L, "Mods");
        ui::info(L, "Status", "Unavailable in this run (no settings file)", ui::kWarn,
                 "Mods are read from the mods folder next to CleriaCore's settings. This run uses built-in "
                 "defaults (a test run), so it is always unmodded.");
    } else {
        {   // the state of play first, so a pending restart is the first thing read
            const std::size_t n_on = mods::active().size();
            std::string s = !u.use ? "Mods off" : std::to_string(n_on) + (n_on == 1 ? " mod active" : " mods active");
            const bool pend = mods_pending(u);
            if (pend) s += "  \xE2\x80\xA2  restart to apply";
            ui::info(L, "Status", s.c_str(), pend ? ui::kWarn : ui::kGood);
            if (pend && ui::button(L, "Restart CleriaCore now",
                                   "Closes CleriaCore and starts it again with the mods as they are set here. In a game, "
                                   "save first: progress since your last save is lost."))
                modfile_request_restart();
        }
        modfile_msg_row(L);
        ui::section(L, "Add and update");
        modget_rows(L, u);
        ui::section(L, "Installed mods");
        if (u.list.empty())
            ui::paragraph(L, "No mods yet. Open Get mods above and pick one: it is downloaded, installed and turned on. "
                             "You can also install a .cleriamod file you downloaded (Options, below). New mods load when "
                             "CleriaCore restarts; press F1 in the game to come back here.", ui::kMuted);
        for (int i = 0; i < int(u.list.size()); ++i) mods_row(L, u, i);
    }
    ui::section(L, "Content");
    {
        const std::string v = content_summary() + "  \xE2\x80\xBA";
        if (ui::choice_label(L, "Content additions", v.c_str(),
                             "What the active mods add to or change in the game: items, chests, pickups, rules, text, and "
                             "the mod items you hold. Not a retail feature."))
            u.next = "content";
    }
    if (!u.on) return;
    ui::section(L, "Options");
    if (ui::toggle(L, "Use mods", &u.use,
                   "Off runs the game exactly as shipped, whatever is installed or turned on. Applies when "
                   "CleriaCore restarts. Not a retail option."))
        mods_apply(u, mods::prefs_of(u.list, u.use));
    if (ui::button(L, "Open mods folder",
                   "Opens the mods folder. A mod is a folder (or a .zip of one) with a mod.ini and a files/ folder "
                   "that holds replacement files by their game path, e.g. files/MAP/S_10/COMMON/H/10YUKA04.DDS.")) {
        std::error_code ec;
        fs::create_directories(u.dir, ec);
        if (std::getenv("CLERIA_DRIVE")) {
            std::printf("[mods] open folder %s (not in a drive run)\n", u.dir.string().c_str());
        } else {
#ifdef _WIN32
            ShellExecuteW(nullptr, L"open", u.dir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
            const std::u8string d8 = fs::absolute(u.dir, ec).u8string();
            SDL_OpenURL(("file://" + std::string(d8.begin(), d8.end())).c_str());   // the file manager
#endif
        }
    }
    modfile_rows(L, u);
    if (ui::button(L, "Rescan", "Look for mods added or removed since CleriaCore started.")) {
        u.list = mods::discover(u.dir);
        mods::arrange(u.list, mods_prefs_from_settings());
        u.msg = std::to_string(u.list.size()) + " mod(s) found";
    }
    ui::info(L, "Mods folder", u.dir.string().c_str());
    if (!u.msg.empty()) ui::info(L, "", u.msg.c_str(), ui::kGood);
}

// The Mods tab with its sub-pages (view_game.inl registers it with this one line).
static void mods_shell(Shell& shell) {
    ui::Page& p = shell.panel.add("Mods", [](ui::List& L) { mods_page(L); });
    p.back = mods_back;
    p.open_sub = mods_open_sub;
}
