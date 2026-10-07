// cleria-view — mod files: `.cleriamod` (build/shell_re/MODFILE.md)
// Textual part of main.cpp: #included after view_mods.inl (ModsUi, mods_prefs_from_settings, mods_apply);
// ONE translation unit, not standalone.
//
// - `cleria-view.exe <file>.cleriamod` (what a double-click runs once the file association is there) or
//   `--install-mod <file>`: the file is taken out of the arguments in main() (modfile_take_args), then
//   installed right after settings_init, before the mods load (modfile_startup): with no CleriaCore running
//   on this mods folder the start goes on (launcher notice card, the mod loaded this session); with one
//   running, the files are installed, a message box says so, and this process exits.
// - The per-user .cleriamod association (src/cleria/fileassoc), kept in step with
//   [cleria.mods] AssociateFiles at every normal start; never in a CLERIA_DRIVE run.
// - The Mods tab's Options rows: "Install a mod file..." and "Open .cleriamod files with CleriaCore".
#include "cleria/fileassoc.hpp"
#include "cleria/modinstall.hpp"
#ifdef _WIN32
#include <commdlg.h>   // GetOpenFileNameW: "Install a mod file..."
#endif

struct ModFile {
    fs::path arg;                 // a mod file named on the command line ("" = none)
    std::string notice;           // the notice card ("Installed ... - enabled", or why not)
    bool notice_err = false;
    Uint64 notice_until = 0;      // SDL_GetTicks (a drive run keeps it up)
    std::string tab_msg;          // the Mods tab's last install result
    bool tab_err = false;
#ifdef _WIN32
    HANDLE instance = nullptr;    // the single-instance mutex, held for the process's life
#endif
};
static ModFile g_modfile;

static std::string modfile_u8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

// main(), before anything reads argv: a bare argument ending in .cleriamod (Explorer's "%1") and
// `--install-mod <file>` are removed from argv and kept in g_modfile.arg, so `cleria-view.exe x.cleriamod`
// starts like a bare launch (the default assets, the launcher) and `<assets> <room> x.cleriamod` like that.
static void modfile_take_args(int& argc, char** argv) {
    auto ends_cleriamod = [](const std::string& a) {
        const std::string e = mods::kModFileExt;
        if (a.size() < e.size()) return false;
        for (std::size_t i = 0; i < e.size(); ++i)
            if (std::tolower((unsigned char)a[a.size() - e.size() + i]) != e[i]) return false;
        return true;
    };
#ifdef _WIN32
    int wargc = 0;   // the wide command line: a path with non-ANSI characters survives
    LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv && wargc != argc) { LocalFree(wargv); wargv = nullptr; }
#endif
    int out = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        int take = -1;
        if (a == "--install-mod") {
            if (i + 1 >= argc) { std::fprintf(stderr, "--install-mod needs a file\n"); continue; }
            take = ++i;
        } else if (ends_cleriamod(a)) {
            take = i;
        }
        if (take < 0) { argv[out++] = argv[i]; continue; }
#ifdef _WIN32
        g_modfile.arg = wargv ? fs::path(wargv[take]) : fs::path(argv[take]);
#else
        g_modfile.arg = fs::path(argv[take]);
#endif
    }
#ifdef _WIN32
    if (wargv) LocalFree(wargv);
#endif
    argv[out] = nullptr;
    argc = out;
}

// This exe's full path (the association's open command and icon).
static fs::path modfile_exe() {
#ifdef _WIN32
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    buf.resize(n);
    return fs::path(buf);
#else
    return {};
#endif
}

static void modfile_store_prefs(const mods::Prefs& p) {
    auto& c = settings().s.cleria;
    c.use_mods = p.use;
    c.mod_order = mods::join_list(p.order);
    c.mod_active = mods::join_list(p.active);
    settings().touch();
    settings().save();
}

// The association, in step with `want` (AssociateFiles). Never in a CLERIA_DRIVE run or a run without a
// settings file; fileassoc::user_registry() refuses a drive run as well.
static void modfile_associate(bool want) {
    if (std::getenv("CLERIA_DRIVE")) { std::printf("[modfile] file association: skipped (drive run)\n"); return; }
    if (settings().memory_only() || settings().path().empty()) return;
    const std::unique_ptr<fileassoc::Registry> reg = fileassoc::user_registry();
    const std::string exe = modfile_u8(modfile_exe());
    if (!reg || exe.empty()) return;
    bool ok = true;
    const fileassoc::Action a = fileassoc::sync(*reg, exe, want, &ok);
    if (a != fileassoc::Action::None) {
        fileassoc::notify_shell();
        std::printf("[modfile] file association: %s (%s)\n", a == fileassoc::Action::Register ? "registered" : "removed", exe.c_str());
    }
    if (!ok) std::printf("[modfile] file association: the registry could not be updated\n");
}

// A message for the player outside the game window (another CleriaCore is running): a native box, or
// printed in a CLERIA_DRIVE run.
static void modfile_say(const std::string& text, bool error) {
    if (std::getenv("CLERIA_DRIVE")) { std::printf("[modfile] message box: %s\n", text.c_str()); return; }
#ifdef _WIN32
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring w(std::size_t(n > 0 ? n : 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, w.data(), n);
    MessageBoxW(nullptr, w.c_str(), L"CleriaCore", MB_OK | MB_SETFOREGROUND | (error ? MB_ICONWARNING : MB_ICONINFORMATION));
#endif
}

// render_window, right after settings_init and before mods_startup. False = this process is done (it
// installed a mod for the CleriaCore already running on this mods folder): exit.
static bool modfile_startup() {
    ModFile& f = g_modfile;
    if (settings().memory_only() || settings().path().empty()) {
        if (!f.arg.empty()) {
            f.notice = "Could not install " + modfile_u8(f.arg.filename()) + ": this run has no settings file";
            f.notice_err = true;
            f.notice_until = SDL_GetTicks() + 12000;
            std::printf("[modfile] %s\n", f.notice.c_str());
        }
        return true;
    }
    const fs::path dir = mods::default_dir(settings().path());
    bool other = false;   // another CleriaCore runs on this mods folder
#ifdef _WIN32
    const std::string key = "Local\\" + mods::instance_key(dir);
    HANDLE h = CreateMutexA(nullptr, FALSE, key.c_str());
    if (h && GetLastError() == ERROR_ALREADY_EXISTS) { other = true; CloseHandle(h); }
    else f.instance = h;
#endif
    if (!other) {   // mods installed while the last CleriaCore ran: on now (it may have rewritten cleria.ini)
        const std::vector<std::string> q = mods::take_enable_queue(dir);
        mods::Prefs p = mods_prefs_from_settings();
        if (!q.empty() && mods::enable_in(p, q)) {
            modfile_store_prefs(p);
            std::printf("[modfile] turned on after an install: %s\n", mods::join_list(q).c_str());
        }
    }
    if (!f.arg.empty()) {
        mods::Prefs p = mods_prefs_from_settings();
        const mods::InstallResult r = mods::install(f.arg, dir, p);
        if (r.ok) {
            modfile_store_prefs(p);
            if (other) mods::queue_enable(dir, r.id);
        }
        std::printf("[modfile] %s\n", r.message().c_str());
        for (const fs::path& x : r.removed) std::printf("[modfile]   replaced %s\n", modfile_u8(x.filename()).c_str());
        for (const std::string& w : r.warnings) std::printf("[modfile]   %s\n", w.c_str());
        if (other) {
            modfile_say(r.ok ? r.message() + ".\n\nRestart CleriaCore to load it." : r.message() + ".", !r.ok);
            return false;
        }
        f.notice = r.message();
        f.notice_err = !r.ok;
        f.notice_until = SDL_GetTicks() + 12000;   // [H] 12 s
    }
    modfile_associate(settings().s.cleria.mod_associate);
    return true;
}

// The notice card (a Shell::hud layer): bottom-centre, ui::toast's look, as wide as its text.
static void modfile_hud(ImDrawList* dl, float w, float h) {
    const ModFile& f = g_modfile;
    if (f.notice.empty() || (!std::getenv("CLERIA_DRIVE") && SDL_GetTicks() > f.notice_until)) return;
    const float S = ui::S, px = 16.0f * S;
    const char* head = f.notice_err ? "MOD NOT INSTALLED" : "MOD INSTALLED";
    // the body, wrapped at word boundaries to the card's widest line
    const float maxw = std::min(720.0f * S, w - 160.0f * S);
    std::vector<std::string> lines;
    {
        std::string line, word;
        auto flush_word = [&] {
            const std::string t = line.empty() ? word : line + " " + word;
            if (!line.empty() && ui::text_size(ui::F.body, px, t.c_str()).x > maxw) { lines.push_back(line); line = word; }
            else line = t;
            word.clear();
        };
        for (char c : f.notice) {
            if (c == ' ') flush_word();
            else word += c;
        }
        flush_word();
        if (!line.empty()) lines.push_back(line);
    }
    float tw = ui::text_size(ui::F.cond, 13.5f * S, head).x;
    for (const std::string& l : lines) tw = std::max(tw, ui::text_size(ui::F.body, px, l.c_str()).x);
    const float bw = std::max(340.0f * S, tw + 38.0f * S), bh = 52.0f * S + float(lines.size()) * 22.0f * S;
    const ImVec2 a{(w - bw) * 0.5f, h - 31.0f * S - bh}, b{a.x + bw, a.y + bh};
    dl->AddRectFilled(a, b, IM_COL32(26, 24, 16, 235), 6.0f * S);
    dl->AddRect(a, b, f.notice_err ? ui::kWarn : ui::kPanelEdge, 6.0f * S, 0, 1.2f * S);
    ui::text(dl, ui::F.cond, 13.5f * S, {a.x + 19.0f * S, a.y + 15.0f * S}, f.notice_err ? ui::kWarn : ui::kGood, head);
    for (std::size_t i = 0; i < lines.size(); ++i)
        ui::text(dl, ui::F.body, px, {a.x + 19.0f * S, a.y + 40.0f * S + float(i) * 22.0f * S}, ui::kDesc, lines[i].c_str());
}

// The native open-file dialog (*.cleriamod; *.zip). Never in a drive run.
static fs::path modfile_pick() {
    if (std::getenv("CLERIA_DRIVE")) { std::printf("[modfile] install dialog (not in a drive run)\n"); return {}; }
#ifdef _WIN32
    wchar_t buf[32768] = L"";
    OPENFILENAMEW o{};
    o.lStructSize = sizeof o;
    o.hwndOwner = GetActiveWindow();
    o.lpstrFilter = L"CleriaCore mods (*.cleriamod, *.zip)\0*.cleriamod;*.zip\0All files (*.*)\0*.*\0";
    o.lpstrFile = buf;
    o.nMaxFile = DWORD(std::size(buf));
    o.lpstrTitle = L"Install a mod file";
    o.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&o)) return fs::path(buf);
#endif
    return {};
}

// Install from the Mods tab (the dialog, or CLERIA_MODS_UI="install=<file>"): the same install as a
// double-click; the list shows it at once, it loads on the next start.
static void modfile_install_ui(ModsUi& u, const fs::path& file) {
    ModFile& f = g_modfile;
    mods::Prefs p = mods::prefs_of(u.list, u.use);
    const mods::InstallResult r = mods::install(file, u.dir, p);
    std::printf("[modfile] %s\n", r.message().c_str());
    if (r.ok) {
        u.use = p.use;
        u.list = mods::discover(u.dir);
        mods_apply(u, p);
    }
    f.tab_msg = r.message();   // the Status row says "restart to apply"
    f.tab_err = !r.ok;
}

// The Mods tab's Options rows (view_mods.inl calls this after "Open mods folder").
static void modfile_rows(ui::List& L, ModsUi& u) {
    ModFile& f = g_modfile;
    if (ui::button(L, "Install a mod file...",
                   "Choose a .cleriamod file (or a .zip mod) to install: it is checked, copied into the mods folder, "
                   "replaces an older version of the same mod, and is turned on. Double-clicking a .cleriamod file does "
                   "the same. It loads when CleriaCore restarts.")) {
        if (const fs::path p = modfile_pick(); !p.empty()) modfile_install_ui(u, p);
    }
    if (!f.tab_msg.empty()) ui::info(L, "", f.tab_msg.c_str(), f.tab_err ? ui::kWarn : ui::kGood, f.tab_msg.c_str());
    bool on = settings().s.cleria.mod_associate;
    if (ui::toggle(L, "Open .cleriamod files with CleriaCore", &on,
                   "On: double-clicking a .cleriamod file installs that mod into CleriaCore and turns it on (a file "
                   "association for your Windows account only; no administrator rights). Off removes CleriaCore's "
                   "association.")) {
        settings().s.cleria.mod_associate = on;
        settings().touch();
        settings().save();
        modfile_associate(on);
    }
}
