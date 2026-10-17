// cleria-view — getting mods: the Mods tab's "Get mods" page, the update check, Remove (build/shell_re/MODS.md §4.1)
// Textual part of main.cpp: #included after view_modfile.inl (modfile_install_ui) and view_mods.inl (ModsUi);
// ONE translation unit, not standalone.
//
// - Mods > Get mods: CleriaOre's list of mods (src/cleria/modget.hpp), read when the page first opens. OK on one
//   downloads its latest release's .cleriamod and installs it as "Install a mod file..." does (checked to be the
//   same id, turned on).
// - Updates: each installed mod's GitHub repository (its Website, else its entry in the list) is asked for a newer
//   release, once when the Mods tab first opens and again on "Check for updates". A mod with one shows
//   "1.0.0 > 1.1.0" on its row and an Update button on its page; "Update N mods" takes them all. An update keeps
//   the mod on or off as it was, and its settings.
// - Remove: the mod's file or folder goes to the Recycle Bin / Trash (else <mods>/.removed). A folder mod this
//   session runs stays until it is turned off and CleriaCore restarted (the game may still read its files).
//
// The network runs on worker threads; each frame (modget_take) installs what they finished, on this thread, as the
// list and the settings are this thread's. In a CLERIA_DRIVE run every step runs at once, in order.
// CLERIA_MODS_UI ops (headless checks, view_mods.inl): get=<id> installs a listed mod, check (an update check),
// update=<id>, remove=<id>.
#include <cstring>
#include <ctime>
#include <map>
#include <thread>

#include "cleria/modget.hpp"

namespace mget = cleria::mods::get;

struct ModGet {
    std::mutex mu;   // guards everything the workers write
    bool cat_asked = false, cat_busy = false;
    std::string cat_status;                                  // "" = read; else why there is no list (yet)
    std::vector<mget::Listed> listed;
    bool up_asked = false, up_busy = false;
    std::string up_status;                                   // what the last check came to
    std::map<std::string, mget::Release> found;              // mod id -> its newer release
    std::string downloading;                                 // the mod id being downloaded
    std::vector<std::pair<std::string, fs::path>> ready;     // downloaded, to install: id, file
    std::string error;                                       // a failed download, told once
    // this thread's
    std::vector<std::string> queue;                          // "Update N mods": the ones still to download
    std::string removing;                                    // Remove pressed once on this mod's page
};
static ModGet g_modget;

// A worker, or the call itself in a drive run (deterministic logs, no thread outliving the run).
static void modget_run(std::function<void()> f) {
    if (std::getenv("CLERIA_DRIVE")) f();
    else std::thread(std::move(f)).detach();
}
static void modget_say(const std::string& text, bool bad) {
    g_modfile.tab_msg = text;
    g_modfile.tab_err = bad;
    std::printf("[modget] %s\n", text.c_str());
}

static void modget_read_catalog() {
    ModGet& G = g_modget;
    {
        std::lock_guard lk(G.mu);
        if (G.cat_busy) return;
        G.cat_asked = G.cat_busy = true;
        G.cat_status = "Reading the list of mods...";
    }
    modget_run([] {
        ModGet& G = g_modget;
        std::string why;
        const std::string text = mget::catalog_text(&why);
        std::vector<mget::Listed> listed = text.empty() ? std::vector<mget::Listed>{} : mget::parse_catalog(text);
        if (why.empty() && listed.empty()) why = "The list of mods could not be read";
        std::printf("[modget] list: %zu mod(s)%s%s\n", listed.size(), why.empty() ? "" : ": ", why.c_str());
        std::lock_guard lk(G.mu);
        G.listed = std::move(listed);
        G.cat_status = why;
        G.cat_busy = false;
    });
}

// Every installed mod's latest release. A mod whose copy names no GitHub page is looked up in the list.
static void modget_check(const ModsUi& u) {
    struct Ask { std::string id, version, repo; };
    std::vector<Ask> asks;
    for (const mods::Mod& m : u.list)
        if (!m.m.id.empty()) asks.push_back({m.m.id, m.m.version, mget::github_repo(m.m.website)});
    ModGet& G = g_modget;
    {
        std::lock_guard lk(G.mu);
        if (G.up_busy) return;
        G.up_asked = G.up_busy = true;
        G.up_status = "Looking for updates...";
    }
    modget_run([asks]() mutable {
        ModGet& G = g_modget;
        if (std::any_of(asks.begin(), asks.end(), [](const Ask& a) { return a.repo.empty(); })) {
            std::vector<mget::Listed> listed;
            {
                std::lock_guard lk(G.mu);
                listed = G.listed;
            }
            if (listed.empty()) listed = mget::parse_catalog(mget::catalog_text(nullptr));
            for (Ask& a : asks)
                for (const mget::Listed& l : listed)
                    if (a.repo.empty() && l.id == a.id) a.repo = l.repo;
        }
        std::erase_if(asks, [](const Ask& a) { return a.repo.empty(); });
        std::map<std::string, mget::Release> found;
        bool failed = false;
        for (const Ask& a : asks)
            if (std::optional<mget::Release> r = mget::latest_release(a.repo, a.version, ".cleriamod", &failed))
                found[a.id] = *r;
        const std::size_t n = found.size();
        const std::string status = !mget::supported() ? "This build cannot download (built without CLERIA_CURL)"
                                 : failed && !n ? "GitHub could not be reached: try again later"
                                 : asks.empty() ? "No installed mod has a GitHub page to update from"
                                 : n ? std::to_string(n) + (n == 1 ? " update" : " updates") + " found"
                                     : "Every mod is up to date";
        std::printf("[modget] check: %zu mod(s) asked, %s\n", asks.size(), status.c_str());
        for (const auto& [id, r] : found) std::printf("[modget]   %s -> %s (%s)\n", id.c_str(), r.version.c_str(), r.file.c_str());
        std::lock_guard lk(G.mu);
        G.found = std::move(found);
        G.up_status = status;
        G.up_busy = false;
    });
}

// `id`'s release `r` into the download folder; modget_take installs it.
static void modget_download(const std::string& id, const std::string& name, std::optional<mget::Release> r,
                            std::string repo = {}) {
    ModGet& G = g_modget;
    {
        std::lock_guard lk(G.mu);
        if (!G.downloading.empty()) return;
        G.downloading = id;
    }
    modget_run([id, name, r, repo]() mutable {
        ModGet& G = g_modget;
        std::string error;
        if (!r) {   // a mod from the list: its latest release, whatever its version
            bool failed = false;
            r = mget::latest_release(repo, "0", ".cleriamod", &failed);
            if (!r) error = name + ": " + (failed ? "GitHub could not be reached" : "it has no release to install yet");
        }
        fs::path to;
        if (r) {
            to = mget::download_dir() / r->file;
            std::string why;
            std::printf("[modget] downloading %s %s (%s)\n", id.c_str(), r->version.c_str(), r->file.c_str());
            if (!mget::download(r->url, to, &why)) error = r->file + " could not be downloaded (" + why + ")";
        }
        std::lock_guard lk(G.mu);
        G.downloading.clear();
        if (error.empty()) G.ready.emplace_back(id, to);
        else G.error = error;
    });
}

static const mods::Mod* modget_find(const ModsUi& u, const std::string& id) {
    for (const mods::Mod& m : u.list)
        if (m.m.id == id) return &m;
    return nullptr;
}

static std::optional<mget::Release> modget_update_of(const std::string& id) {
    std::lock_guard lk(g_modget.mu);
    const auto it = g_modget.found.find(id);
    return it == g_modget.found.end() ? std::nullopt : std::optional<mget::Release>(it->second);
}

static std::optional<std::string> modget_update_version(const std::string& id) {
    const auto r = modget_update_of(id);
    return r ? std::optional<std::string>(r->version) : std::nullopt;
}

static void modget_update(const std::string& id) {
    const mods::Mod* m = modget_find(g_mods_ui, id);
    if (const auto r = modget_update_of(id); r && m) modget_download(id, m->label(), r);
}

// Each frame the Mods tab is drawn: the finished downloads installed, the next queued update started.
static void modget_take(ModsUi& u) {
    ModGet& G = g_modget;
    std::vector<std::pair<std::string, fs::path>> ready;
    bool idle;
    {
        std::lock_guard lk(G.mu);
        ready.swap(G.ready);
        idle = G.downloading.empty();
        if (!G.error.empty()) modget_say(G.error, true);
        G.error.clear();
    }
    for (const auto& [id, file] : ready) {
        const mods::Package pkg = mods::inspect_package(file);
        if (pkg.ok() && pkg.m.id != id) {
            modget_say(file.filename().string() + " holds another mod (" + pkg.m.id + "): not installed", true);
        } else {
            const mods::Mod* before = modget_find(u, id);
            const bool update = before != nullptr, was_on = before && before->enabled;
            modfile_install_ui(u, file);   // checked, copied, turned on; its message on the tab
            if (!g_modfile.tab_err) {
                if (update && !was_on) {   // an update keeps the mod off (a new one is turned on)
                    mods::Prefs p = mods::prefs_of(u.list, u.use);
                    std::erase(p.active, id);
                    mods_apply(u, p);
                    const std::string& msg = g_modfile.tab_msg;
                    modget_say(msg.substr(0, msg.rfind(" \xE2\x80\x94 ")) + ": it stays off", false);
                }
                std::lock_guard lk(G.mu);
                G.found.erase(id);
                if (G.found.empty() && !G.up_status.empty()) G.up_status = "Every mod is up to date";
            }
        }
        std::error_code ec;
        fs::remove(file, ec);
    }
    if (idle && ready.empty() && !G.queue.empty()) {
        const std::string id = G.queue.front();
        G.queue.erase(G.queue.begin());
        modget_update(id);
    }
}

// The Recycle Bin (Windows), the Trash (macOS, the freedesktop one elsewhere), else <mods>/.removed. "" when it
// could not be moved; else where it went, for the message.
static std::string modget_trash(const fs::path& p, const fs::path& mods_dir) {
    std::error_code ec;
#ifdef _WIN32
    std::wstring from = p.wstring();
    from.push_back(L'\0');   // the list ends with two
    SHFILEOPSTRUCTW op{};
    op.wFunc = FO_DELETE;
    op.pFrom = from.c_str();
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
    if (SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted && !fs::exists(p, ec)) return "the Recycle Bin";
#else
    auto free_name = [&](const fs::path& dir) {   // name, name (2), name (3)...
        fs::path to = dir / p.filename();
        for (int k = 2; fs::exists(to, ec) || fs::is_symlink(to, ec); ++k)
            to = dir / (p.stem().string() + " (" + std::to_string(k) + ")" + p.extension().string());
        return to;
    };
    const char* home = std::getenv("HOME");
#ifdef __APPLE__
    if (home && *home) {
        const fs::path to = free_name(fs::path(home) / ".Trash");
        fs::rename(p, to, ec);
        if (!ec) return "the Trash";
    }
#else
    fs::path trash;   // the freedesktop.org Trash: files/<name> and info/<name>.trashinfo
    if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) trash = fs::path(x) / "Trash";
    else if (home && *home) trash = fs::path(home) / ".local" / "share" / "Trash";
    if (!trash.empty()) {
        fs::create_directories(trash / "files", ec);
        fs::create_directories(trash / "info", ec);
        const fs::path to = free_name(trash / "files");
        char when[32] = "";
        const std::time_t now = std::time(nullptr);
        if (const std::tm* tm = std::localtime(&now)) std::strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%S", tm);
        std::string path;   // percent-encoded, as the spec asks
        for (unsigned char c : fs::absolute(p, ec).string()) {
            if (std::isalnum(c) || std::strchr("/-_.~", c)) path += char(c);
            else { char h[4]; std::snprintf(h, sizeof h, "%%%02X", c); path += h; }
        }
        const fs::path info = trash / "info" / (to.filename().string() + ".trashinfo");
        if (std::ofstream(info) << "[Trash Info]\nPath=" << path << "\nDeletionDate=" << when << "\n") {
            fs::rename(p, to, ec);
            if (!ec) return "the Trash";
            fs::remove(info, ec);
        }
    }
#endif
#endif
    ec.clear();
    const fs::path dir = mods_dir / ".removed";   // another disk, or no Trash: kept beside the mods
    fs::create_directories(dir, ec);
    fs::path to = dir / p.filename();
    for (int k = 2; fs::exists(to, ec); ++k) to = dir / (p.filename().string() + "." + std::to_string(k));
    fs::rename(p, to, ec);
    return ec ? std::string() : "the mods folder's .removed folder";
}

// True when `m` cannot be removed now: a folder mod this session loaded (its files may still be read).
static bool modget_in_use(const mods::Mod& m) {
    if (m.zip) return false;   // a zip mod runs from its extraction in .cache
    for (const mods::Info& a : mods::active())
        if (a.id == m.m.id) return true;
    return false;
}

static void modget_remove(ModsUi& u, const std::string id) {   // a copy: `id` may be a listed mod's, and the list is replaced
    const mods::Mod* m = modget_find(u, id);
    if (!m) return;
    const std::string name = m->label();
    if (modget_in_use(*m)) return modget_say(name + " is running: turn it off and restart CleriaCore first", true);
    if (std::getenv("CLERIA_DRIVE") && !std::getenv("CLERIA_MODS_UI_TRASH")) {   // a test never fills the real Trash
        std::error_code ec;
        fs::remove_all(m->source, ec);
        if (ec) return modget_say(name + " could not be removed", true);
        modget_say(name + " was removed (drive run: deleted)", false);
    } else {
        const std::string where = modget_trash(m->source, u.dir);
        if (where.empty()) return modget_say(name + " could not be removed", true);
        modget_say(name + " was moved to " + where, false);
    }
    mods::Prefs p = mods::prefs_of(u.list, u.use);
    std::erase(p.order, id);
    std::erase(p.active, id);
    u.list = mods::discover(u.dir);
    mods_apply(u, p);
    std::lock_guard lk(g_modget.mu);
    g_modget.found.erase(id);
}

// A web page in the browser (a mod's Website, its GitHub page).
static void modget_open_url(const std::string& url) {
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) return;
    if (std::getenv("CLERIA_DRIVE")) { std::printf("[modget] open %s (not in a drive run)\n", url.c_str()); return; }
    SDL_OpenURL(url.c_str());
}

// ---- the rows and pages ----------------------------------------------------------------------------------

// The Mods list's "Get mods" and update rows (view_mods.inl, above the installed mods).
static void modget_rows(ui::List& L, ModsUi& u) {
    ModGet& G = g_modget;
    if (!G.up_asked && !u.list.empty() && mget::supported() && !std::getenv("CLERIA_DRIVE")) modget_check(u);
    if (ui::choice_label(L, "Get mods", "Browse  \xE2\x80\xBA",
                         "The mods made for CleriaCore (and CleriaOre): achievements, challenges, a randomizer, "
                         "Archipelago and more. Pick one and it is downloaded, installed and turned on."))
        u.next = "get";
    std::string status, downloading;
    bool busy;
    std::vector<std::string> ids;
    {
        std::lock_guard lk(G.mu);
        status = G.up_status;
        busy = G.up_busy;
        downloading = G.downloading;
        for (const auto& [id, r] : G.found) ids.push_back(id);
    }
    if (u.list.empty()) return;
    const bool working = busy || !downloading.empty() || !G.queue.empty();
    std::string label = busy ? status
                      : !downloading.empty() ? "Updating " + (modget_find(u, downloading) ? modget_find(u, downloading)->label() : downloading) + "..."
                      : ids.size() == 1 ? "Update " + (modget_find(u, ids[0]) ? modget_find(u, ids[0])->label() : ids[0])
                      : ids.size() > 1 ? "Update " + std::to_string(ids.size()) + " mods"
                      : "Check for updates";
    const std::string desc = std::string(ids.empty() ? "Asks each installed mod's GitHub page for a newer release."
                                                     : "Downloads and installs the updates found. Each mod stays on or off "
                                                       "as it is, and keeps its settings.") +
                             (status.empty() || busy ? "" : "\n\nLast check: " + status);
    if (ui::button(L, label.c_str(), desc.c_str(), !working && mget::supported())) {
        if (ids.empty()) modget_check(u);
        else G.queue = ids;
    }
    if (!status.empty() && !busy && ids.empty()) ui::info(L, "Updates", status.c_str(), ui::kMuted);
}

// Mods > <mod>: its update, above everything else on its page.
static void modget_mod_rows(ui::List& L, const mods::Mod& m) {
    const auto r = modget_update_of(m.m.id);
    if (!r) return;
    std::string downloading;
    {
        std::lock_guard lk(g_modget.mu);
        downloading = g_modget.downloading;
    }
    ui::section(L, "Update");
    const std::string label = downloading == m.m.id ? "Downloading " + r->version + "..." : "Update to " + r->version;
    if (ui::button(L, label.c_str(), ("Downloads " + r->file + " from the mod's GitHub page and installs it in place of " +
                                      m.m.version + ". It stays on or off as it is, and keeps its settings.").c_str(),
                   downloading.empty()))
        modget_update(m.m.id);
}

// Mods > <mod>: Website (opens it) and Remove, at the end of its page.
static bool modget_website_row(ui::List& L, const mods::Mod& m) {
    if (m.m.website.empty()) return false;
    const bool web = m.m.website.rfind("https://", 0) == 0 || m.m.website.rfind("http://", 0) == 0;
    if (ui::choice_label(L, "Website", m.m.website.c_str(), web ? "Opens it in your browser." : nullptr) && web)
        modget_open_url(m.m.website);
    return true;
}
static void modget_remove_rows(ui::List& L, ModsUi& u, const mods::Mod& m) {
    ModGet& G = g_modget;
    ui::section(L, "Remove");
    if (modget_in_use(m)) {
        ui::info(L, "Remove", "Turn it off and restart first", ui::kMuted,
                 "This mod is a folder CleriaCore is using right now. Turn it off, restart CleriaCore, then remove it.");
        return;
    }
    const bool sure = G.removing == m.m.id;
    if (ui::button(L, sure ? "Press again to remove it" : "Remove this mod",
                   "Moves the mod's file or folder to the Recycle Bin (the Trash on macOS and Linux). What it kept with "
                   "your saves and its settings stay, should you install it again.")) {
        if (sure) {
            G.removing.clear();
            u.next = "";
            modget_remove(u, m.m.id);
        } else {
            G.removing = m.m.id;
        }
    }
}

// Mods > Get mods.
static void modget_page(ui::List& L, ModsUi& u) {
    ModGet& G = g_modget;
    if (ui::back_row(L, "Mods", "Get mods",
                     "Mods to install with one click. Each comes from its author's GitHub page.\n\nOK / click here, Esc, "
                     "Backspace or the pad's Back return to the Mods list."))
        u.next = "";
    if (!u.on) {
        ui::info(L, "Status", "Unavailable in this run (no settings file)", ui::kWarn);
        return;
    }
    if (!mget::supported()) {
        ui::info(L, "Status", "This build cannot download", ui::kWarn,
                 "CleriaCore was built without a web client (CLERIA_CURL off). Download a .cleriamod from the mod's page "
                 "and use Install a mod file... on the Mods list instead.");
        return;
    }
    bool asked;
    {
        std::lock_guard lk(G.mu);
        asked = G.cat_asked;
    }
    if (!asked) modget_read_catalog();
    std::vector<mget::Listed> listed;
    std::string status, downloading;
    bool reading;
    {
        std::lock_guard lk(G.mu);
        listed = G.listed;
        status = G.cat_status;
        downloading = G.downloading;
        reading = G.cat_busy;
    }
    modfile_msg_row(L);
    if (!status.empty()) {
        ui::info(L, "Status", status.c_str(), reading ? ui::kMuted : ui::kWarn);
        if (!reading && ui::button(L, "Try again")) modget_read_catalog();
        return;
    }
    std::size_t have_n = 0;
    for (const mget::Listed& l : listed) have_n += modget_find(u, l.id) != nullptr;
    {
        const std::string s = std::to_string(listed.size()) + " mods  \xE2\x80\xA2  " + std::to_string(have_n) + " installed";
        ui::info(L, "Available", s.c_str(), ui::kGood);
    }
    ui::section(L, "Mods");
    for (const mget::Listed& l : listed) {
        const mods::Mod* have = modget_find(u, l.id);
        const bool busy = downloading == l.id, can = !have && downloading.empty();
        const std::string desc = l.name + (l.author.empty() ? "" : "\nby " + l.author) + "\n\n" + l.description +
            (have ? "\n\nInstalled: " + have->m.version + (have->enabled ? ", on" : ", off") +
                        ". OK opens its page on the Mods list."
                  : "\n\nOK downloads its latest release, installs it and turns it on. It loads when CleriaCore "
                    "restarts; its settings are then on its page.") +
            "\n\ngithub.com/" + l.repo;
        const ui::RowIo r = ui::row_frame(L, desc.c_str(), have || can);
        const char* v = busy ? "Installing..." : have ? (modget_update_of(l.id) ? "Update" : "Installed") : "Install";
        ui::row_texts(L, r, l.name.c_str(), v, have || can);
        if (r.activate && have) u.next = l.id;
        else if (r.activate && can) modget_download(l.id, l.name, std::nullopt, l.repo);
    }
    ui::paragraph(L, "Mods are made by their authors, not by CleriaCore. Each one's GitHub page says what it does and "
                     "how it was tested.", ui::kMuted);
}

// CLERIA_MODS_UI's network ops (view_mods.inl mods_ui_script): true when `op` was one.
static bool modget_ui_op(ModsUi& u, const std::string& op, const std::string& id) {
    if (op == "check") {
        modget_check(u);
    } else if (op == "get") {
        if (!g_modget.cat_asked) modget_read_catalog();
        const mget::Listed* l = nullptr;
        for (const mget::Listed& x : g_modget.listed)
            if (x.id == id) l = &x;
        if (!l) std::printf("[modget] ui: %s is not in the list\n", id.c_str());
        else modget_download(l->id, l->name, std::nullopt, l->repo);
    } else if (op == "update") {
        modget_update(id);
    } else if (op == "remove") {
        modget_remove(u, id);
    } else {
        return false;
    }
    modget_take(u);
    return true;
}
