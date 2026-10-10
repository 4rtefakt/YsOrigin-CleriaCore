#include "cleria/gamedata.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <system_error>
#include <unordered_map>

#include "cleria/ni_archive.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cleria::gamedata {
namespace fs = std::filesystem;

namespace {

// The archives, base first; later ones override earlier ones by name. The exe opens
// release\data.na plus one language overlay (release\data_us.na for English; the exe also
// names data_fr/de/sp/it, which this install does not ship) [BYTES strings 0x292f90..].
constexpr const auto& kArchives = GameArchives::kNames;

// The movie files the engine opens from release\ (src/cleria/movie.cpp kMovieFiles, with the
// shipped yso_pro.avi standing in for en_pro.avi).
constexpr const char* kMovies[] = {"yso_pro.avi", "yso_op.avi", "yso_ins01.dat", "yso_ins02.dat",
                                   "yso_ins03.dat", "yso_logo.avi", "yso_ed01.dat", "yso_ed02.dat"};

std::mutex g_mu;
std::vector<fs::path> g_installing;   // data folders being unpacked by this process

bool is_file(const fs::path& p) {
    std::error_code ec;
    return fs::is_regular_file(p, ec);
}
bool is_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec);
}
std::string u8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
fs::path from_u8(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}
std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
fs::path env_path(const char* name) {
#ifdef _WIN32
    std::wstring w(name, name + std::strlen(name));
    if (const wchar_t* v = _wgetenv(w.c_str()); v && *v) return fs::path(v);
#else
    if (const char* v = std::getenv(name); v && *v) return fs::path(v);
#endif
    return {};
}
bool same_path(const fs::path& a, const fs::path& b) {
    std::error_code ec;
    return lower(u8(fs::weakly_canonical(a, ec))) == lower(u8(fs::weakly_canonical(b, ec)));
}

#ifdef _WIN32
fs::path reg_path(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return fs::path(buf);
}
#endif

// The quoted strings of a Valve KeyValues file (libraryfolders.vdf / appmanifest_*.acf), with
// "\\" unescaped. Enough for "key" "value" lookups.
std::vector<std::string> vdf_tokens(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::vector<std::string> out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '"') continue;
        std::string tok;
        for (++i; i < s.size() && s[i] != '"'; ++i) {
            if (s[i] == '\\' && i + 1 < s.size()) ++i;
            tok += s[i];
        }
        out.push_back(std::move(tok));
    }
    return out;
}

std::uint64_t fnv1a(std::uint64_t h, const void* p, std::size_t n) {
    const auto* b = static_cast<const std::uint8_t*>(p);
    for (std::size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 0x100000001B3ull;
    return h;
}

std::string utc_now() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::FILE* open_write(const fs::path& p) {
#ifdef _WIN32
    return _wfopen(p.c_str(), L"wb");
#else
    return std::fopen(p.c_str(), "wb");
#endif
}

fs::path install_root_of(const fs::path& dir) {
    // Accept the release\ folder itself.
    if (!is_file(dir / "yso_win.exe") && is_file(dir / "data.ni") && is_file(dir.parent_path() / "yso_win.exe"))
        return dir.parent_path();
    return dir;
}

}  // namespace

// ---- the install -------------------------------------------------------------------------

Validation validate(const fs::path& dir) {
    Validation v;
    std::error_code ec;
    v.root = fs::weakly_canonical(install_root_of(dir), ec);
    if (ec) v.root = install_root_of(dir);
    if (!is_dir(v.root)) {
        v.missing.push_back("(folder does not exist)");
        return v;
    }
    const fs::path rel = v.root / "release";
    if (!is_file(v.root / "yso_win.exe")) v.missing.push_back("yso_win.exe");
    for (const char* a : kArchives)
        for (const char* ext : {".ni", ".na"}) {
            const std::string name = std::string(a) + ext;
            if (!is_file(rel / name)) v.missing.push_back("release\\" + name);
        }
    if (v.missing.empty()) {
        // The indexes must decrypt and parse, and fit their .na.
        for (const char* a : kArchives) {
            ni::Archive ar;
            std::string err;
            if (!ar.open(rel / (std::string(a) + ".ni"), &err))
                v.missing.push_back("release\\" + std::string(a) + ".ni (unreadable: " + err + ")");
        }
    }
    if (!is_file(rel / "music" / "bgmdef.txt")) v.warnings.push_back("release\\music\\bgmdef.txt (no music)");
    else if (!is_file(rel / "music" / "YSO_001.ogg")) v.warnings.push_back("release\\music\\*.ogg (no music)");
    for (const char* m : kMovies)
        if (!is_file(rel / m)) v.warnings.push_back("release\\" + std::string(m) + " (movie)");
    if (!is_file(rel / "yso_ins04.dat")) v.warnings.push_back("release\\yso_ins04.dat (menu font)");
    v.ok = v.missing.empty();
    return v;
}

std::vector<Validation> find_installs() {
    std::vector<fs::path> steam_roots;
#ifdef _WIN32
    for (const fs::path& p : {reg_path(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"),
                              reg_path(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"),
                              reg_path(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath")})
        if (!p.empty()) steam_roots.push_back(p);
    if (fs::path pf = env_path("ProgramFiles(x86)"); !pf.empty()) steam_roots.push_back(pf / "Steam");
#else
    if (fs::path h = env_path("HOME"); !h.empty()) {
#ifdef __APPLE__
        steam_roots.push_back(h / "Library" / "Application Support" / "Steam");
#else
        steam_roots.push_back(h / ".steam" / "steam");
        steam_roots.push_back(h / ".local" / "share" / "Steam");
        steam_roots.push_back(h / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam");   // Flatpak
#endif
    }
#endif
    std::vector<fs::path> libs;
    for (const fs::path& root : steam_roots) {
        libs.push_back(root);
        for (const fs::path& vdf : {root / "steamapps" / "libraryfolders.vdf", root / "config" / "libraryfolders.vdf"}) {
            const std::vector<std::string> t = vdf_tokens(vdf);
            for (std::size_t i = 0; i + 1 < t.size(); ++i)
                if (lower(t[i]) == "path") libs.push_back(from_u8(t[i + 1]));
        }
    }
    std::vector<fs::path> cands;
    for (const fs::path& lib : libs) {
        std::string dir = "Ys Origin";
        const std::vector<std::string> t =
            vdf_tokens(lib / "steamapps" / ("appmanifest_" + std::to_string(kSteamAppId) + ".acf"));
        for (std::size_t i = 0; i + 1 < t.size(); ++i)
            if (lower(t[i]) == "installdir" && !t[i + 1].empty()) dir = t[i + 1];
        const fs::path c = lib / "steamapps" / "common" / from_u8(dir);
        if (!is_dir(c)) continue;
        if (std::none_of(cands.begin(), cands.end(), [&](const fs::path& o) { return same_path(o, c); }))
            cands.push_back(c);
    }
    std::vector<Validation> out;
    for (const fs::path& c : cands) out.push_back(validate(c));
    std::stable_partition(out.begin(), out.end(), [](const Validation& v) { return v.ok; });
    return out;
}

std::string source_hash(const fs::path& install_root) {
    const fs::path rel = install_root_of(install_root) / "release";
    std::uint64_t h = 0xCBF29CE484222325ull;
    for (const char* a : kArchives) {
        std::ifstream f(rel / (std::string(a) + ".ni"), std::ios::binary);
        if (!f) return {};
        std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        h = fnv1a(h, d.data(), d.size());
        std::error_code ec;
        const std::uint64_t na = fs::file_size(rel / (std::string(a) + ".na"), ec);
        if (ec) return {};
        h = fnv1a(h, &na, sizeof na);
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

bool GameArchives::open(const fs::path& install_root, std::string* err) {
    const fs::path rel = install_root_of(install_root) / "release";
    archives.assign(std::size(kNames), ni::Archive{});
    files.clear();
    std::unordered_map<std::string, std::size_t> slot;
    for (std::size_t a = 0; a < archives.size(); ++a) {
        if (!archives[a].open(rel / (std::string(kNames[a]) + ".ni"), err)) return false;
        for (const ni::Entry& e : archives[a].entries()) {
            auto [it, fresh] = slot.emplace(ni::lookup_key(e.name), files.size());
            if (fresh) files.push_back({a, &e});
            else files[it->second] = {a, &e};
        }
    }
    return true;
}

// ---- languages ---------------------------------------------------------------------------

const Language* language(int id) {
    for (const Language& l : kLanguages)
        if (l.id == id) return &l;
    return nullptr;
}

fs::path language_dir(const fs::path& asset_root, int id) {
    const Language* l = language(id);
    if (!l || id == 1) return {};
    const fs::path e = env_path("CLERIA_LANG_DIR");
    return (e.empty() ? asset_root / "lang" : e) / l->archive;
}

std::vector<int> languages(const fs::path& asset_root) {
    std::vector<int> out = {1};
    for (const Language& l : kLanguages)
        if (l.id != 1 && is_dir(language_dir(asset_root, l.id))) out.push_back(l.id);
    return out;
}

long long unpack_archive(const fs::path& ni_path, const fs::path& out_dir, std::string* err) {
    ni::Archive a;
    if (!a.open(ni_path, err)) return -1;
    ni::NaReader rd(a);
    std::vector<std::uint8_t> buf;
    long long n = 0;
    for (const ni::Entry& e : a.entries()) {
        if (!rd.read(e, buf, err)) return -1;
        const fs::path dst = out_dir / from_u8(e.name);
        std::error_code ec;
        fs::create_directories(dst.parent_path(), ec);
        std::FILE* f = open_write(dst);
        const bool ok = f && (buf.empty() || std::fwrite(buf.data(), 1, buf.size(), f) == buf.size());
        if ((f && std::fclose(f) != 0) || !ok) {
            if (err) *err = "cannot write " + u8(dst);
            return -1;
        }
        ++n;
    }
    return n;
}

std::vector<std::string> localization_credits(const fs::path& install_root) {
    std::vector<std::string> lines;
    std::ifstream f(install_root_of(install_root) / "yso_win.exe", std::ios::binary);
    if (!f) return lines;
    const std::string d{std::istreambuf_iterator<char>(f), {}};
    auto wide = [](std::string_view s) {
        std::string w;
        for (char c : s) { w += c; w += char(0); }
        return w;
    };
    // One NUL-terminated UTF-16LE string at `at`, as UTF-8 lines appended to `lines`.
    auto take = [&](std::size_t at) {
        std::string cur;
        for (; at + 1 < d.size(); at += 2) {
            const unsigned c = std::uint8_t(d[at]) | unsigned(std::uint8_t(d[at + 1])) << 8;
            if (c == 0) break;
            if (c == 10) { lines.push_back(cur); cur.clear(); }
            else if (c < 0x80) cur += char(c);
            else if (c == 0x2013 || c == 0x2014) cur += '-';          // dashes the shell's font may lack
            else if (c < 0x800) { cur += char(0xC0 | c >> 6); cur += char(0x80 | (c & 0x3F)); }
            else { cur += char(0xE0 | c >> 12); cur += char(0x80 | ((c >> 6) & 0x3F)); cur += char(0x80 | (c & 0x3F)); }
        }
        lines.push_back(cur);
    };
    std::string first = "Ys Origin";
    first += char(10);
    first += "PC Version - European Language Localization";
    const std::size_t p1 = d.find(wide(first));
    if (p1 == std::string::npos) return lines;
    take(p1);
    const std::string t2 = wide("European Localization Credits (2/2)");
    std::size_t p2 = d.find(t2, p1);
    if (p2 == std::string::npos) return lines;
    p2 += t2.size();
    while (p2 + 1 < d.size() && d[p2] == 0 && d[p2 + 1] == 0) p2 += 2;   // the title's NUL and the padding
    lines.push_back("");
    take(p2);
    return lines;
}

// ---- the data folder ---------------------------------------------------------------------

fs::path data_dir() {
    if (fs::path e = env_path("CLERIA_DATA"); !e.empty()) return e;
#ifdef _WIN32
    if (fs::path l = env_path("LOCALAPPDATA"); !l.empty()) return l / "CleriaCore" / "data";
#elif defined(__APPLE__)
    if (fs::path h = env_path("HOME"); !h.empty()) return h / "Library" / "Application Support" / "CleriaCore" / "data";
#else
    if (fs::path x = env_path("XDG_DATA_HOME"); !x.empty()) return x / "CleriaCore" / "data";
    if (fs::path h = env_path("HOME"); !h.empty()) return h / ".local" / "share" / "CleriaCore" / "data";
#endif
    return fs::path("CleriaCore-data");
}

std::optional<Manifest> read_manifest(const fs::path& data) {
    std::ifstream f(data / kManifestName, std::ios::binary);
    if (!f) return std::nullopt;
    Manifest m;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "format") m.format = std::atoi(v.c_str());
        else if (k == "install") m.install = from_u8(v);
        else if (k == "files") m.files = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "bytes") m.bytes = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "source_hash") m.source_hash = v;
        else if (k == "created") m.created = v;
    }
    if (m.format != kFormatVersion || m.files == 0) return std::nullopt;
    return m;
}

Status status(const fs::path& data) {
    Status s;
    s.data = data;
    {
        std::lock_guard lk(g_mu);
        for (const fs::path& p : g_installing)
            if (same_path(p, data)) {
                s.state = State::Installing;
                return s;
            }
    }
    s.manifest = read_manifest(data);
    if (!s.manifest || is_file(data / kMarkerName)) {
        s.manifest.reset();
        std::error_code ec;
        s.partial = is_dir(data) && !fs::is_empty(data, ec);
        return s;
    }
    s.state = State::Ready;
    s.install = s.manifest->install;
    if (is_dir(s.install / "release")) {
        s.release = s.install / "release";
        const std::string h = source_hash(s.install);
        s.stale = !h.empty() && h != s.manifest->source_hash;
    }
    return s;
}

fs::path ready_data_dir() {
    const fs::path d = data_dir();
    // Cheap: no source re-hash here, this runs at every launch.
    if (!read_manifest(d) || is_file(d / kMarkerName)) return {};
    std::error_code ec;
    return fs::weakly_canonical(d, ec);
}

fs::path release_dir_for(const fs::path& asset_root) {
    const std::optional<Manifest> m = read_manifest(asset_root);
    if (!m) return {};
    const fs::path rel = m->install / "release";
    return is_dir(rel) ? rel : fs::path();
}

// ---- unpacking ---------------------------------------------------------------------------

InstallResult install(const fs::path& game_dir, const fs::path& out, const ProgressFn& progress,
                      const std::atomic<bool>* cancel, unsigned threads) {
    const auto t0 = std::chrono::steady_clock::now();
    InstallResult r;
    auto finish = [&]() -> InstallResult {
        r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return r;
    };

    const Validation v = validate(game_dir);
    if (!v.ok) {
        r.error = "not a usable Ys Origin install (" + u8(v.root) + "): missing";
        for (const std::string& m : v.missing) r.error += " " + m;
        return finish();
    }
    if (same_path(out, v.root) || same_path(out, v.root / "release")) {
        r.error = "the output folder must not be the game install";
        return finish();
    }

    std::error_code ec;
    if (is_dir(out) && !fs::is_empty(out, ec) && !is_file(out / kManifestName) && !is_file(out / kMarkerName)) {
        r.error = "refusing to unpack into " + u8(out) +
                  ": it is not empty and not a CleriaCore data folder (no " + kManifestName + ")";
        return finish();
    }

    GameArchives src;
    if (!src.open(v.root, &r.error)) return finish();
    const std::vector<ni::Archive>& archives = src.archives;
    const std::vector<GameArchives::File>& jobs = src.files;

    // Register, clear the old manifest, drop the marker, make the folders.
    {
        std::lock_guard lk(g_mu);
        g_installing.push_back(out);
    }
    struct Unregister {
        const fs::path& p;
        ~Unregister() {
            std::lock_guard lk(g_mu);
            for (auto it = g_installing.begin(); it != g_installing.end(); ++it)
                if (*it == p) { g_installing.erase(it); break; }
        }
    } unregister{out};

    fs::create_directories(out, ec);
    fs::remove(out / kManifestName, ec);
    { std::ofstream(out / kMarkerName) << "unpacking from " << u8(v.root) << "\n"; }
    if (!is_file(out / kMarkerName)) {
        r.error = "cannot write to " + u8(out);
        return finish();
    }
    {
        std::set<std::string> dirs;
        for (const GameArchives::File& j : jobs) {
            const std::size_t slash = j.entry->name.rfind('/');
            if (slash != std::string::npos) dirs.insert(j.entry->name.substr(0, slash));
        }
        for (const std::string& d : dirs) {
            fs::create_directories(out / from_u8(d), ec);
            if (ec) {
                r.error = "cannot create " + u8(out / from_u8(d)) + ": " + ec.message();
                return finish();
            }
        }
    }

    // Unpack on a small pool; progress is reported from this thread.
    Progress total;
    total.files_total = jobs.size();
    for (const GameArchives::File& j : jobs) total.bytes_total += j.entry->size;
    std::atomic<std::size_t> next{0};
    std::atomic<std::uint64_t> files_done{0}, bytes_done{0}, bytes_written{0};
    std::atomic<bool> failed{false};
    std::mutex fail_mu;
    auto fail = [&](std::string msg) {
        std::lock_guard lk(fail_mu);
        if (r.failures.size() < 50) r.failures.push_back(std::move(msg));
        failed = true;
    };
    auto worker = [&]() {
        std::vector<std::unique_ptr<ni::NaReader>> readers;
        for (const ni::Archive& a : archives) readers.push_back(std::make_unique<ni::NaReader>(a));
        std::vector<std::uint8_t> buf;
        for (;;) {
            if (cancel && cancel->load()) return;
            const std::size_t i = next.fetch_add(1);
            if (i >= jobs.size()) return;
            const GameArchives::File& j = jobs[i];
            std::string err;
            if (!readers[j.archive]->read(*j.entry, buf, &err)) {
                fail(err);
            } else {
                const fs::path dst = out / from_u8(j.entry->name);
                std::FILE* f = open_write(dst);
                if (!f || (!buf.empty() && std::fwrite(buf.data(), 1, buf.size(), f) != buf.size()))
                    fail("cannot write " + u8(dst));
                else
                    bytes_written += buf.size();
                if (f && std::fclose(f) != 0) fail("cannot write " + u8(dst));
            }
            bytes_done += j.entry->size;
            ++files_done;
        }
    };
    if (threads == 0) threads = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t) pool.emplace_back(worker);
    auto snapshot = [&]() {
        Progress p = total;
        p.files_done = files_done;
        p.bytes_done = bytes_done;
        p.bytes_written = bytes_written;
        return p;
    };
    while (files_done < jobs.size() && !(cancel && cancel->load())) {
        if (progress) progress(snapshot());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    for (std::thread& t : pool) t.join();
    if (progress) progress(snapshot());

    r.files = files_done;
    r.bytes = bytes_written;
    if (cancel && cancel->load() && files_done < jobs.size()) {
        r.cancelled = true;
        r.error = "cancelled";
        return finish();   // the marker stays: status() reports a partial folder
    }
    if (failed) {
        r.error = std::to_string(r.failures.size()) + " file(s) failed, first: " + r.failures.front();
        return finish();
    }

    // The other language overlays the install has, each on its own under lang/ (see kLanguages).
    for (const Language& l : kLanguages) {
        const fs::path lni = v.root / "release" / (std::string(l.archive) + ".ni");
        if (l.id == 1 || !is_file(lni)) continue;
        std::string lerr;
        if (unpack_archive(lni, out / "lang" / l.archive, &lerr) < 0) r.failures.push_back(std::string(l.archive) + ": " + lerr);
    }

    // The manifest goes last, via a rename, so it only ever exists for a complete tree.
    {
        const fs::path tmp = out / (std::string(kManifestName) + ".tmp");
        {
            std::ofstream m(tmp, std::ios::binary);
            m << "# CleriaCore game data (src/cleria/gamedata.hpp); written last, on success\n"
              << "format=" << kFormatVersion << "\n"
              << "install=" << u8(v.root) << "\n"
              << "files=" << r.files << "\n"
              << "bytes=" << r.bytes << "\n"
              << "source_hash=" << source_hash(v.root) << "\n"
              << "created=" << utc_now() << "\n";
            if (!m) {
                r.error = "cannot write the manifest";
                return finish();
            }
        }
        fs::rename(tmp, out / kManifestName, ec);
        if (ec) {
            r.error = "cannot write the manifest: " + ec.message();
            return finish();
        }
    }
    fs::remove(out / kMarkerName, ec);
    r.ok = true;
    return finish();
}

// ---- InstallTask -------------------------------------------------------------------------

InstallTask::~InstallTask() {
    cancel();
    wait();
}

bool InstallTask::start(const fs::path& game_dir, const fs::path& out) {
    if (running_) return false;
    wait();
    cancel_ = false;
    running_ = true;
    {
        std::lock_guard lk(mu_);
        prog_ = {};
        res_ = {};
    }
    th_ = std::thread([this, game_dir, out]() {
        InstallResult r = install(game_dir, out, [this](const Progress& p) {
            std::lock_guard lk(mu_);
            prog_ = p;
        }, &cancel_);
        {
            std::lock_guard lk(mu_);
            res_ = std::move(r);
        }
        running_ = false;
    });
    return true;
}

Progress InstallTask::progress() const {
    std::lock_guard lk(mu_);
    return prog_;
}

InstallResult InstallTask::result() const {
    std::lock_guard lk(mu_);
    return res_;
}

void InstallTask::wait() {
    if (th_.joinable()) th_.join();
}

}  // namespace cleria::gamedata
