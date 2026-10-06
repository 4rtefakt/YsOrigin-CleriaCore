// cleria-view — mods: the startup mount and the F1 panel's "Mods" tab (build/shell_re/MODS.md)
// Textual part of main.cpp: #included after view_shell.inl (the ui kit) and view_input.inl
// (settings()); ONE translation unit, not standalone.
//
// At startup (render_window, right after settings_init) the game's folders are mounted in the vfs
// and the enabled mods laid over them; that set is fixed for the session (mods::active()). The tab
// edits cleria.ini [cleria.mods] (the order and the enabled ids), which applies on the next start.
#include "cleria/mods.hpp"
#ifdef _WIN32
#include <shellapi.h>   // ShellExecuteW: "Open mods folder" (WIN32_LEAN_AND_MEAN leaves it out)
#endif

struct ModsUi {
    bool on = false;                  // mods are possible this session (a settings file is in use)
    fs::path dir;                     // the mods folder
    std::vector<mods::Mod> list;      // the tab's view: what is installed + the pending choices
    bool use = true;                  // the pending UseMods
    std::string msg;                  // the last action's result
};
static ModsUi g_mods_ui;
static bool mods_pending(const ModsUi& u);
static void mods_ui_script(ModsUi& u);

static mods::Prefs mods_prefs_from_settings() {
    const auto& c = settings().s.cleria;
    return {c.use_mods, mods::split_list(c.mod_order), mods::split_list(c.mod_active)};
}

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
    // Textures and rooms a mod ADDS (new names) join the name indexes like build_indexes does; a
    // replaced file keeps its base index entry and vfs::resolve swaps it at the open.
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

static std::string mods_desc(const mods::Mod& m) {
    std::string d = m.label();
    if (!m.m.version.empty()) d += "  " + m.m.version;
    if (!m.m.author.empty()) d += "\nby " + m.m.author;
    if (!m.m.description.empty()) d += "\n\n" + m.m.description;
    d += "\n\n" + (m.m.id.empty() ? std::string("(no id)") : m.m.id) + "  \xE2\x80\xA2  " + std::to_string(m.files) +
         " file(s)  \xE2\x80\xA2  " + m.source.filename().string() + (m.zip ? "" : "/");
    auto deps = [](const std::vector<mods::Dep>& v) {
        std::string s;
        for (const mods::Dep& x : v) s += (s.empty() ? "" : ", ") + x.id + (x.op.empty() ? "" : " " + x.op + " " + x.version);
        return s;
    };
    if (!m.m.requires_.empty()) d += "\nRequires: " + deps(m.m.requires_);
    if (!m.m.conflicts.empty()) d += "\nConflicts with: " + deps(m.m.conflicts);
    for (const std::string& e : m.errors) d += "\nError: " + e;
    for (const std::string& n : m.notes) d += "\n" + n;
    d += "\n\nOK / click turns it on or off. Left / Right or the arrows move it up / down the load order: when two "
         "mods change the same file, the one lower in the list wins. Changes apply when CleriaCore restarts.";
    return d;
}

// One mod: "N  NAME  version" on the left, the state on the right, up / down arrows before it.
static void mods_row(ui::List& L, ModsUi& u, int i) {
    mods::Mod& m = u.list[std::size_t(i)];
    const std::string desc = mods_desc(m);
    const bool rclick = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    ui::RowIo r = ui::row_frame(L, desc.c_str(), true);
    const float S = ui::S, cy = (r.a.y + r.b.y) * 0.5f;
    // the arrows (mouse); a click on one is not a toggle
    int move = 0;
    const float ax = r.b.x - 150.0f * S;
    for (int k = 0; k < 2; ++k) {
        const ImVec2 c{ax + float(k) * 24.0f * S, cy};
        const ImVec2 a{c.x - 10.0f * S, r.a.y + 2.0f * S}, b{c.x + 10.0f * S, r.b.y - 2.0f * S};
        const bool hov = L.focus && ui::hovering(a, b);
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
    ui::text(L.dl, ui::F.body, 15.0f * S, {x, cy - 15.0f * S * 0.55f}, ui::kMuted, m.m.version.c_str());
    const char* v = m.state == mods::State::Error ? "Error" : m.enabled ? "On" : "Off";
    const ImU32 vc = m.state == mods::State::Error ? ui::kWarn : m.enabled ? ui::kGood : ui::kMuted;
    ImFont* vf = r.selected ? ui::F.medium : ui::F.body;
    const ImVec2 vs = ui::text_size(vf, vpx, v);
    ui::text(L.dl, vf, vpx, {r.b.x - 13.0f * S - vs.x, cy - vpx * 0.55f}, vc, v);
    // the actions
    if (r.activate) mods_toggle(u, i);
    else if (move != 0 && mods_move(u, i, move)) L.sel += move;   // the selection follows the mod
}

static void mods_page(ui::List& L) {
    ModsUi& u = g_mods_ui;
    if (!u.on) {
        ui::section(L, "Mods");
        ui::info(L, "Status", "Unavailable in this run (no settings file)", ui::kWarn,
                 "Mods are read from the mods folder next to CleriaCore's settings. This run uses built-in "
                 "defaults (a test run), so it is always unmodded.");
        return;
    }
    {   // the state of play first, so a pending restart is the first thing read
        const std::size_t n_on = mods::active().size();
        std::string s = !u.use ? "Mods off" : std::to_string(n_on) + (n_on == 1 ? " mod active" : " mods active");
        const bool pend = mods_pending(u);
        if (pend) s += "  \xE2\x80\xA2  restart to apply";
        ui::info(L, "Status", s.c_str(), pend ? ui::kWarn : ui::kGood);
    }
    ui::section(L, "Installed mods");
    if (u.list.empty()) ui::info(L, "None", "Put mods in the mods folder", ui::kMuted);
    for (int i = 0; i < int(u.list.size()); ++i) mods_row(L, u, i);
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
#endif
        }
    }
    if (ui::button(L, "Rescan", "Look for mods added or removed since CleriaCore started.")) {
        u.list = mods::discover(u.dir);
        mods::arrange(u.list, mods_prefs_from_settings());
        u.msg = std::to_string(u.list.size()) + " mod(s) found";
    }
    ui::info(L, "Mods folder", u.dir.string().c_str());
    if (!u.msg.empty()) ui::info(L, "", u.msg.c_str(), ui::kGood);
}
