// Mods: discovery, manifests, load order, checks, the session's active set (mods.hpp, MODS.md).
#include "cleria/mods.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <system_error>

#include "cleria/settings.hpp"   // Ini: the same dialect as cleria.ini
#include "miniz.h"

namespace fs = std::filesystem;

namespace cleria::mods {
namespace {

std::string trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return std::string(s.substr(a, b - a));
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A manifest value: the raw text, its surrounding quotes (if any) removed. Unlike retail's
// get_string, an unquoted value is kept whole.
std::string value(const Ini& ini, std::string_view key) {
    const auto r = ini.raw("mod", key);
    if (!r) return {};
    std::string v = trim(*r);
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') v = v.substr(1, v.size() - 2);
    return v;
}

std::string u8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}
fs::path from_u8(std::string_view s) { return fs::path(std::u8string(s.begin(), s.end())); }

std::size_t count_files(const fs::path& dir) {
    std::size_t n = 0;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (it->is_regular_file(fec)) ++n;
    }
    return n;
}

bool read_file(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Reads <root>/mod.ini into `m`.
void read_manifest(Mod& m) {
    std::string text;
    if (!read_file(m.root / kManifestName, text)) {
        m.problems.push_back("mod.ini could not be read");
        return;
    }
    m.m = parse_manifest(text, m.problems);
    m.files = count_files(m.files_dir());
}

// A zip entry name that stays inside the extraction folder: relative, no "..", no drive.
bool safe_entry(std::string_view n) {
    if (n.empty() || n.front() == '/' || n.front() == '\\' || n.find(':') != std::string_view::npos) return false;
    std::size_t i = 0;
    while (i <= n.size()) {
        std::size_t j = n.find_first_of("/\\", i);
        if (j == std::string_view::npos) j = n.size();
        if (n.substr(i, j - i) == "..") return false;
        i = j + 1;
    }
    return true;
}

// Extracts `zip` into `dest` unless dest/.stamp already records this zip's size + write time.
// Returns the folder that holds mod.ini (dest or its one top-level folder), empty on failure.
fs::path extract_zip(const fs::path& zip, const fs::path& dest, std::string& err) {
    std::error_code ec;
    const auto size = fs::file_size(zip, ec);
    // (long long: libc++'s file clock counts in __int128, which std::to_string has no overload for)
    const auto when = static_cast<long long>(fs::last_write_time(zip, ec).time_since_epoch().count());
    const std::string stamp = std::to_string(size) + " " + std::to_string(when) + " api" + std::to_string(kApiVersion);
    auto find_root = [&]() -> fs::path {
        std::error_code e2;
        if (fs::exists(dest / kManifestName, e2)) return dest;
        fs::path only;
        int dirs = 0;
        for (const auto& d : fs::directory_iterator(dest, e2))
            if (d.is_directory()) { ++dirs; only = d.path(); }
        if (dirs == 1 && fs::exists(only / kManifestName, e2)) return only;
        return {};
    };
    std::string old;
    if (read_file(dest / ".stamp", old) && old == stamp) {
        if (fs::path r = find_root(); !r.empty()) return r;
    }
    // A stale or partial extraction: the cache folder is ours (under <mods>/.cache), replace it.
    fs::remove_all(dest, ec);
    fs::create_directories(dest, ec);
    std::string bytes;
    if (!read_file(zip, bytes)) { err = "the zip could not be read"; return {}; }
    mz_zip_archive za{};
    if (!mz_zip_reader_init_mem(&za, bytes.data(), bytes.size(), 0)) { err = "not a valid zip file"; return {}; }
    const mz_uint n = mz_zip_reader_get_num_files(&za);
    bool ok = true;
    for (mz_uint i = 0; i < n && ok; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&za, i, &st)) { ok = false; err = "a zip entry is unreadable"; break; }
        if (!safe_entry(st.m_filename)) { ok = false; err = std::string("unsafe path in the zip: ") + st.m_filename; break; }
        const fs::path out = dest / from_u8(st.m_filename);
        if (mz_zip_reader_is_file_a_directory(&za, i)) { fs::create_directories(out, ec); continue; }
        std::size_t len = 0;
        void* p = mz_zip_reader_extract_to_heap(&za, i, &len, 0);
        if (!p) { ok = false; err = std::string("could not unpack ") + st.m_filename; break; }
        fs::create_directories(out.parent_path(), ec);
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        f.write(static_cast<const char*>(p), std::streamsize(len));
        mz_free(p);
        if (!f) { ok = false; err = "could not write " + u8(out); }
    }
    mz_zip_reader_end(&za);
    if (!ok) return {};
    { std::ofstream f(dest / ".stamp", std::ios::binary | std::ios::trunc); f << stamp; }
    fs::path r = find_root();
    if (r.empty()) err = "no mod.ini in the zip (at its root or in one top-level folder)";
    return r;
}

std::vector<Info> g_active;

}  // namespace

bool safe_zip_entry(std::string_view name) { return safe_entry(name); }

// ---- the manifest --------------------------------------------------------------------------------
bool valid_id(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    return true;
}

Dep parse_dep(std::string_view s) {
    Dep d;
    const std::string t = trim(s);
    const std::size_t at = t.find_first_of("<>=");
    d.id = lower(trim(t.substr(0, at)));
    if (at != std::string::npos) {
        std::size_t e = at;
        while (e < t.size() && (t[e] == '<' || t[e] == '>' || t[e] == '=')) ++e;
        d.op = t.substr(at, e - at);
        if (d.op == "==") d.op = "=";
        d.version = trim(t.substr(e));
        static const char* const kOps[] = {">=", ">", "=", "<=", "<"};
        if (std::find(std::begin(kOps), std::end(kOps), d.op) == std::end(kOps) || d.version.empty()) d.id.clear();
    }
    if (!valid_id(d.id)) d.id.clear();
    return d;
}

Manifest parse_manifest(std::string_view text, std::vector<std::string>& errors) {
    Ini ini;
    ini.parse(text);
    Manifest m;
    const std::string id = value(ini, "Id");
    m.id = lower(id);
    if (id.empty()) errors.push_back("mod.ini has no [mod] Id");
    else if (!valid_id(m.id)) errors.push_back("invalid Id \"" + id + "\" (use a-z, 0-9, '.', '_', '-')");
    m.name = value(ini, "Name");
    if (std::string v = value(ini, "Version"); !v.empty()) m.version = v;
    m.author = value(ini, "Author");
    m.description = value(ini, "Description");
    m.website = value(ini, "Website");
    if (auto p = ini.get_int("mod", "Priority")) m.priority = *p;
    if (auto a = ini.get_int("mod", "ApiVersion")) m.api = *a;
    m.script = value(ini, "Script");
    for (const char* k : {"Requires", "Conflicts"}) {
        for (const std::string& s : split_list(value(ini, k))) {
            const Dep d = parse_dep(s);
            if (d.id.empty()) { errors.push_back(std::string("bad ") + k + " entry \"" + s + "\""); continue; }
            (k[0] == 'R' ? m.requires_ : m.conflicts).push_back(d);
        }
    }
    return m;
}

int compare_versions(std::string_view a, std::string_view b) {
    auto part = [](std::string_view s, std::size_t& i) {
        const std::size_t j = std::min(s.find('.', i), s.size());
        const std::string_view r = s.substr(i, j - i);
        i = j < s.size() ? j + 1 : s.size();
        return r;
    };
    std::size_t i = 0, j = 0;
    while (i < a.size() || j < b.size()) {
        const std::string_view x = part(a, i), y = part(b, j);
        const bool nx = !x.empty() && std::all_of(x.begin(), x.end(), [](char c) { return c >= '0' && c <= '9'; });
        const bool ny = !y.empty() && std::all_of(y.begin(), y.end(), [](char c) { return c >= '0' && c <= '9'; });
        if ((nx || x.empty()) && (ny || y.empty())) {
            const long long vx = x.empty() ? 0 : std::atoll(std::string(x).c_str());
            const long long vy = y.empty() ? 0 : std::atoll(std::string(y).c_str());
            if (vx != vy) return vx < vy ? -1 : 1;
        } else if (const int c = std::string(x).compare(std::string(y)); c != 0) {
            return c < 0 ? -1 : 1;
        }
    }
    return 0;
}

bool satisfies(std::string_view version, const Dep& d) {
    if (d.op.empty()) return true;
    const int c = compare_versions(version, d.version);
    if (d.op == ">=") return c >= 0;
    if (d.op == ">") return c > 0;
    if (d.op == "=") return c == 0;
    if (d.op == "<=") return c <= 0;
    if (d.op == "<") return c < 0;
    return false;
}

// ---- prefs ---------------------------------------------------------------------------------------
std::vector<std::string> split_list(std::string_view s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = s.find(',', i);
        if (j == std::string_view::npos) j = s.size();
        if (std::string t = trim(s.substr(i, j - i)); !t.empty()) out.push_back(std::move(t));
        i = j + 1;
    }
    return out;
}

std::string join_list(const std::vector<std::string>& v) {
    std::string o;
    for (const std::string& s : v) { if (!o.empty()) o += ','; o += s; }
    return o;
}

fs::path default_dir(const fs::path& settings_ini) {
    if (const char* e = std::getenv("CLERIA_MODS_DIR"); e && *e) return fs::path(e);
    return settings_ini.parent_path() / "mods";
}

// ---- discovery -----------------------------------------------------------------------------------
std::vector<Mod> discover(const fs::path& dir) {
    std::vector<Mod> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    std::vector<fs::path> entries;
    for (const auto& d : fs::directory_iterator(dir, ec)) entries.push_back(d.path());
    std::sort(entries.begin(), entries.end());
    for (const fs::path& p : entries) {
        const std::string name = u8(p.filename());
        if (name.empty() || name[0] == '.') continue;   // .cache and hidden folders
        Mod m;
        m.source = p;
        std::error_code e2;
        if (fs::is_directory(p, e2)) {
            if (!fs::exists(p / kManifestName, e2)) continue;   // not a mod folder
            m.root = p;
            read_manifest(m);
        } else if (const std::string ext = lower(u8(p.extension())); ext == ".zip" || ext == kModFileExt) {
            m.zip = true;   // a .cleriamod is a zip mod (MODFILE.md); it unpacks to .cache/<name>.cleriamod/,
            std::string err;   // never into the folder of a same-named .zip
            m.root = extract_zip(p, dir / kCacheDir / (ext == ".zip" ? p.stem() : p.filename()), err);
            if (m.root.empty()) {
                m.problems.push_back(err);
                m.m.name = name;
            } else {
                read_manifest(m);
            }
        } else {
            continue;
        }
        if (m.m.id.empty() && m.m.name.empty()) m.m.name = name;
        out.push_back(std::move(m));
    }
    return out;
}

// ---- order and checks ----------------------------------------------------------------------------
void arrange(std::vector<Mod>& mods, const Prefs& prefs) {
    // 1. the order: the player's list first, the rest by (Priority, id, source)
    auto rank = [&](const Mod& m) -> std::size_t {
        const auto it = std::find(prefs.order.begin(), prefs.order.end(), m.m.id);
        return m.m.id.empty() || it == prefs.order.end() ? prefs.order.size() : std::size_t(it - prefs.order.begin());
    };
    std::stable_sort(mods.begin(), mods.end(), [&](const Mod& a, const Mod& b) {
        const std::size_t ra = rank(a), rb = rank(b);
        if (ra != rb) return ra < rb;
        if (a.m.id.empty() != b.m.id.empty()) return b.m.id.empty();   // unreadable mods last
        if (a.m.priority != b.m.priority) return a.m.priority < b.m.priority;
        if (a.m.id != b.m.id) return a.m.id < b.m.id;
        return a.source < b.source;
    });
    // 2. the choices; the errors are recomputed from the discovery problems on every call
    std::set<std::string> seen;
    for (Mod& m : mods) {
        m.errors = m.problems;
        m.enabled = prefs.use && !m.m.id.empty() &&
                    std::find(prefs.active.begin(), prefs.active.end(), m.m.id) != prefs.active.end();
        if (!m.m.id.empty() && !seen.insert(m.m.id).second)
            m.errors.push_back("another mod already uses the id \"" + m.m.id + "\"");
    }
    const std::vector<std::size_t> own = [&] {
        std::vector<std::size_t> v;
        for (const Mod& m : mods) v.push_back(m.errors.size());
        return v;
    }();
    auto find = [&](const std::string& id) -> Mod* {
        for (Mod& o : mods)
            if (o.m.id == id) return &o;   // the first one: a duplicate is itself in error
        return nullptr;
    };
    auto loads = [](const Mod& m) { return m.enabled && m.errors.empty(); };
    // 3. the checks, to a fixed point (a mod whose dependency fails, fails)
    for (bool changed = true; changed;) {
        changed = false;
        for (Mod& m : mods) {
            if (!loads(m)) continue;
            std::string why;
            if (m.m.api > kApiVersion) {
                why = "needs a newer CleriaCore (mod API " + std::to_string(m.m.api) + ", this build has " +
                      std::to_string(kApiVersion) + ")";
            }
            for (const Dep& d : m.m.requires_) {
                if (!why.empty()) break;
                const Mod* o = find(d.id);
                const std::string want = d.id + (d.op.empty() ? "" : " " + d.op + " " + d.version);
                if (!o) why = "needs " + want + " (not installed)";
                else if (!satisfies(o->m.version, d)) why = "needs " + want + " (found " + o->m.version + ")";
                else if (!o->enabled) why = "needs " + o->label() + " (turned off)";
                else if (!o->errors.empty()) why = "needs " + o->label() + ", which cannot load";
            }
            for (const Dep& d : m.m.conflicts) {
                if (!why.empty()) break;
                const Mod* o = find(d.id);
                if (o && o != &m && loads(*o) && satisfies(o->m.version, d))
                    why = "conflicts with " + o->label() + " (turn one of them off)";
            }
            if (!why.empty()) { m.errors.push_back(why); changed = true; }
        }
    }
    // a problem of its own (manifest, zip, duplicate id) is an error whether or not the mod is on;
    // the load checks above only ran for enabled mods
    for (std::size_t i = 0; i < mods.size(); ++i) {
        Mod& m = mods[i];
        m.state = (own[i] > 0 || (m.enabled && !m.errors.empty())) ? State::Error
                  : m.enabled                                       ? State::Active
                                                                    : State::Disabled;
    }
}

Prefs prefs_of(const std::vector<Mod>& mods, bool use) {
    Prefs p;
    p.use = use;
    for (const Mod& m : mods) {
        if (m.m.id.empty()) continue;
        p.order.push_back(m.m.id);
        if (m.enabled) p.active.push_back(m.m.id);
    }
    return p;
}

std::vector<vfs::Layer> layers(const std::vector<Mod>& mods) {
    std::vector<vfs::Layer> out;
    for (const Mod& m : mods)
        if (m.state == State::Active) out.push_back({m.m.id, m.files_dir()});
    return out;
}

void note_overrides(std::vector<Mod>& mods) {
    const auto& ls = vfs::layers();
    std::map<std::string, std::map<std::string, int>> lost, won;   // mod id -> other id -> files
    for (const auto& [k, e] : vfs::table()) {
        if (e.also.empty()) continue;
        const std::string& winner = ls[std::size_t(e.layer)].id;
        for (int l : e.also) {
            const std::string& loser = ls[std::size_t(l)].id;
            ++lost[loser][winner];
            ++won[winner][loser];
        }
    }
    for (Mod& m : mods) {
        m.notes.clear();
        for (const auto& [other, n] : lost[m.m.id])
            m.notes.push_back(std::to_string(n) + " file(s) replaced by " + other + " (lower in the list)");
        for (const auto& [other, n] : won[m.m.id])
            m.notes.push_back("replaces " + std::to_string(n) + " file(s) of " + other);
    }
}

// ---- the session ---------------------------------------------------------------------------------
std::vector<Mod> load(const fs::path& dir, const Prefs& prefs) {
    std::vector<Mod> mods = discover(dir);
    arrange(mods, prefs);
    vfs::set_layers(layers(mods));
    note_overrides(mods);
    g_active.clear();
    for (const Mod& m : mods)
        if (m.state == State::Active) g_active.push_back({m.m.id, m.label(), m.m.version});
    return mods;
}

const std::vector<Info>& active() { return g_active; }

std::string active_summary() {
    if (g_active.empty()) return "none";
    std::string s;
    for (const Info& i : g_active) {
        if (!s.empty()) s += ", ";
        s += i.name + " " + i.version;
    }
    return s;
}

}  // namespace cleria::mods
