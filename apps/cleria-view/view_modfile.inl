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
#include <io.h>        // _get_osfhandle: Restart now hands the logs on
#else
#include <cerrno>       // Restart now's execv
#include <fcntl.h>      // the single-instance lock file
#include <sys/file.h>   // flock
#include <unistd.h>
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
#else
    int instance_fd = -1;         // the single-instance lock file (flock), held for the process's life
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
static fs::path modfile_exe() { return platform_exe_path(); }   // view_platform.inl

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
#if defined(_WIN32)
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
#elif defined(__APPLE__)
    // CleriaCore.app declares .cleriamod in its Info.plist (tools/macos_bundle.py): Finder opens them with it and
    // modfile_dropped installs them. Nothing to register per user; a bare build uses --install-mod <file>.
    (void)want;
#else
    // The freedesktop .desktop entry + mime type under $XDG_DATA_HOME (src/cleria/fileassoc, xdg::).
    const fs::path home = fileassoc::xdg::user_data_home();
    const std::string exe = modfile_u8(modfile_exe());
    if (home.empty() || exe.empty()) return;
    bool ok = true;
    const fileassoc::Action a = fileassoc::xdg::sync(home, exe, want, &ok);
    if (a != fileassoc::Action::None) {
        fileassoc::xdg::notify(home);
        std::printf("[modfile] file association: %s (%s)\n", a == fileassoc::Action::Register ? "registered" : "removed", exe.c_str());
    }
    if (!ok) std::printf("[modfile] file association: the desktop entry could not be written\n");
#endif
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
#else
    SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_WARNING : SDL_MESSAGEBOX_INFORMATION, "CleriaCore", text.c_str(), nullptr);
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
#else
    // The same key as an advisory lock file in the temp folder, held for the process's life.
    std::error_code tec;
    const fs::path lock = fs::temp_directory_path(tec) / (mods::instance_key(dir) + ".lock");
    const int fd = tec ? -1 : ::open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd >= 0 && ::flock(fd, LOCK_EX | LOCK_NB) != 0) { other = true; ::close(fd); }
    else f.instance_fd = fd;
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

#ifndef _WIN32
// Off Windows the dialog is SDL's (the desktop portal, else zenity; Cocoa on macOS). It answers later, from
// the event pump or a thread of its own: modfile_rows takes the answer on a following frame.
static std::mutex g_modfile_pick_mu;
static std::string g_modfile_picked;
static fs::path modfile_pick_take() {
    std::lock_guard<std::mutex> lk(g_modfile_pick_mu);
    fs::path p = g_modfile_picked;
    g_modfile_picked.clear();
    return p;
}
#endif

// The native open-file dialog (*.cleriamod; *.zip). Never in a drive run. On Windows it returns the file; off
// Windows it only opens the dialog and returns nothing (modfile_pick_take has the file later).
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
#else
    static const SDL_DialogFileFilter filters[] = {{"CleriaCore mods (*.cleriamod, *.zip)", "cleriamod;zip"},
                                                   {"All files", "*"}};
    SDL_ShowOpenFileDialog([](void*, const char* const* list, int) {
        if (!list || !list[0]) return;      // cancelled or failed
        std::lock_guard<std::mutex> lk(g_modfile_pick_mu);
        g_modfile_picked = list[0];
    }, nullptr, SDL_GetKeyboardFocus(), filters, 2, nullptr, false);
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

// A file dropped on the window (any screen), or opened with CleriaCore.app on macOS (Finder's double-click, Open
// With: SDL delivers both as SDL_EVENT_DROP_FILE). A .cleriamod or .zip is installed as by "Install a mod file...",
// with the notice card; anything else is left to the screen. CLERIA_MODS_UI="drop=<file>" in headless checks.
static bool modfile_dropped(const char* path) {
    const std::string s = path;
    const fs::path p = fs::path(std::u8string(s.begin(), s.end()));
    std::string ext = p.extension().string();
    for (char& c : ext) c = char(std::tolower((unsigned char)c));
    if (ext != mods::kModFileExt && ext != ".zip") return false;
    ModFile& f = g_modfile;
    if (!g_mods_ui.on) {
        f.notice = "Could not install " + modfile_u8(p.filename()) + ": this run has no settings file";
        f.notice_err = true;
    } else {
        modfile_install_ui(g_mods_ui, p);
        f.notice = f.tab_msg + (f.tab_err ? "" : ". It loads when CleriaCore restarts");
        f.notice_err = f.tab_err;
    }
    f.notice_until = SDL_GetTicks() + 12000;
    std::printf("[modfile] dropped %s\n", modfile_u8(p).c_str());
    return true;
}

// ---- Restart now ------------------------------------------------------------------------------------------
// The Mods tab's "Restart CleriaCore now" (when the mods set differs from the loaded one): a quit as the window's
// close button makes it, then main() starts this exe again with the arguments it kept (modfile_keep_args: the
// ones left after modfile_take_args, so a .cleriamod given at the first start is not installed twice). The
// single-instance lock is let go first, so the new process is the only CleriaCore on the mods folder.
static std::vector<std::string> g_relaunch_args;   // argv[1..]
static bool g_relaunch = false;

static void modfile_keep_args(int argc, char** argv) {
    g_relaunch_args.assign(argv + 1, argv + argc);
}

static void modfile_request_restart() {
    if (std::getenv("CLERIA_DRIVE")) {   // a test never starts another process
        std::printf("[modfile] restart: would start %s again (%zu argument(s))\n", modfile_u8(modfile_exe()).c_str(),
                    g_relaunch_args.size());
        return;
    }
    g_relaunch = true;
    SDL_Event q{};
    q.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&q);
}

// main(), once the window is gone: the new process, when asked for.
static void modfile_relaunch() {
    if (!g_relaunch) return;
    const fs::path exe = modfile_exe();
    if (exe.empty()) return;
    ModFile& f = g_modfile;
#ifdef _WIN32
    if (f.instance) { CloseHandle(f.instance); f.instance = nullptr; }
    auto quote = [](const std::wstring& a) {   // CommandLineToArgvW's rules
        if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
        std::wstring q = L"\"";
        std::size_t bs = 0;
        for (wchar_t c : a) {
            if (c == L'\\') { ++bs; continue; }
            q.append(c == L'"' ? bs * 2 + 1 : bs, L'\\');
            bs = 0;
            q += c;
        }
        q.append(bs * 2, L'\\');
        return q + L"\"";
    };
    std::wstring cmd = quote(exe.wstring());
    for (const std::string& a : g_relaunch_args) {
        const std::u8string u(a.begin(), a.end());
        cmd += L" " + quote(fs::path(u).wstring());
    }
    // The new process writes its logs where this one did (a terminal, a pipe, a file): only stdout and stderr
    // are inherited (PROC_THREAD_ATTRIBUTE_HANDLE_LIST), and only when there are any (a start from Explorer has none).
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof si;
    HANDLE std2[2] = {HANDLE(_get_osfhandle(_fileno(stdout))), HANDLE(_get_osfhandle(_fileno(stderr)))};
    HANDLE pass[2];
    DWORD n = 0;
    for (HANDLE h : std2)
        if (h && h != INVALID_HANDLE_VALUE && (n == 0 || pass[0] != h) &&
            SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
            pass[n++] = h;
    std::vector<char> attrs;
    if (n) {
        SIZE_T sz = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &sz);
        attrs.resize(sz);
        si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
        if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &sz) ||
            !UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, pass, n * sizeof(HANDLE),
                                       nullptr, nullptr)) {
            si.lpAttributeList = nullptr;
            n = 0;
        } else {
            si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            si.StartupInfo.hStdInput = nullptr;
            si.StartupInfo.hStdOutput = std2[0];
            si.StartupInfo.hStdError = std2[1];
        }
    }
    PROCESS_INFORMATION pi{};
    std::fflush(nullptr);
    const BOOL started = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, n ? TRUE : FALSE,
                                        n ? EXTENDED_STARTUPINFO_PRESENT : 0, nullptr, nullptr, &si.StartupInfo, &pi);
    if (si.lpAttributeList) DeleteProcThreadAttributeList(si.lpAttributeList);
    if (started) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        std::fprintf(stderr, "[modfile] restart failed (%lu)\n", GetLastError());
    }
#else
    if (f.instance_fd >= 0) { ::close(f.instance_fd); f.instance_fd = -1; }
    const std::string path = modfile_u8(exe);
    std::vector<char*> av{const_cast<char*>(path.c_str())};
    for (std::string& a : g_relaunch_args) av.push_back(a.data());
    av.push_back(nullptr);
    std::fflush(nullptr);
    ::execv(path.c_str(), av.data());
    std::fprintf(stderr, "[modfile] restart failed: %s\n", std::strerror(errno));
#endif
}

// The last install's (or update's, or removal's) result, at the top of the Mods list and of Get mods.
static void modfile_msg_row(ui::List& L) {
    const ModFile& f = g_modfile;
    if (!f.tab_msg.empty()) ui::info(L, "", f.tab_msg.c_str(), f.tab_err ? ui::kWarn : ui::kGood, f.tab_msg.c_str());
}

// The Mods tab's Options rows (view_mods.inl calls this after "Open mods folder").
static void modfile_rows(ui::List& L, ModsUi& u) {
    if (ui::button(L, "Install a mod file...",
                   "Choose a .cleriamod file (or a .zip mod) to install: it is checked, copied into the mods folder, "
                   "replaces an older version of the same mod, and is turned on. Double-clicking a .cleriamod file, or "
                   "dropping it on this window, does the same. It loads when CleriaCore restarts.")) {
        if (const fs::path p = modfile_pick(); !p.empty()) modfile_install_ui(u, p);
    }
#ifndef _WIN32
    if (const fs::path p = modfile_pick_take(); !p.empty()) modfile_install_ui(u, p);
#endif
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
