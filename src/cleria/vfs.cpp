// The virtual file system (vfs.hpp, build/shell_re/MODS.md §3).
#include "cleria/vfs.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>

namespace fs = std::filesystem;

namespace cleria::vfs {
namespace {

struct Mount {
    std::string prefix;   // key form, no trailing '/'
    std::string dir;      // absolute, normal, generic, lowercase, with a trailing '/'
    fs::path path;        // as given
};

std::vector<Mount> g_mounts;
std::vector<Layer> g_layers;
std::unordered_map<std::string, Entry> g_table;
std::unordered_map<std::string, fs::path> g_lang;   // set_language: key(virtual name under "") -> file

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

std::string u8(const fs::path& p) {
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

#ifndef _WIN32
// A path with the game's '\' separators made real ones ('\' is an ordinary character off Windows).
fs::path slashes(const fs::path& p) {
    std::string s = p.string();
    if (s.find('\\') == std::string::npos) return p;
    std::replace(s.begin(), s.end(), '\\', '/');
    return fs::path(s);
}

// Off Windows the file system is case-sensitive and the game's names are not: a path that does not
// exist as spelled is matched one component at a time, ignoring ASCII case (each folder's listing
// cached on first use). Unchanged when nothing matches.
fs::path case_fold(const fs::path& in) {
    const fs::path p = slashes(in);
    std::error_code ec;
    if (p.empty() || fs::exists(p, ec)) return p;
    static std::mutex mu;
    static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> listings;
    std::lock_guard<std::mutex> l(mu);
    fs::path cur = p.root_path();
    for (const fs::path& part : p.relative_path()) {
        const std::string name = part.string();
        fs::path next = cur / part;
        if (name == "." || name == ".." || fs::exists(next, ec)) { cur = std::move(next); continue; }
        const std::string dir = cur.empty() ? std::string(".") : cur.string();
        auto [it, fresh] = listings.try_emplace(dir);
        if (fresh)
            for (fs::directory_iterator d(dir, ec), end; !ec && d != end; d.increment(ec))
                it->second.emplace(lower(d->path().filename().string()), d->path().filename().string());
        const auto hit = it->second.find(lower(name));
        if (hit == it->second.end()) return p;
        cur /= hit->second;
    }
    return cur;
}
#endif

// The absolute, normal, '/'-separated UTF-8 form of a path.
std::string norm(const fs::path& p_in) {
#ifndef _WIN32
    const fs::path p = slashes(p_in);
#else
    const fs::path& p = p_in;
#endif
    std::error_code ec;
    fs::path a = fs::absolute(p, ec);
    if (ec) a = p;
    std::string s = u8(a.lexically_normal());
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

}  // namespace

std::string key(std::string_view name) {
    std::string k(name);
    std::replace(k.begin(), k.end(), '\\', '/');
    k = lower(std::move(k));
    while (k.rfind("./", 0) == 0) k.erase(0, 2);
    while (!k.empty() && k.front() == '/') k.erase(0, 1);
    return k;
}

void mount(const std::string& prefix, const fs::path& dir) {
    Mount m;
    m.prefix = key(prefix);
    while (!m.prefix.empty() && m.prefix.back() == '/') m.prefix.pop_back();
    m.dir = lower(norm(dir));
    if (m.dir.empty() || m.dir.back() != '/') m.dir += '/';
    m.path = dir;
    g_mounts.erase(std::remove_if(g_mounts.begin(), g_mounts.end(),
                                  [&](const Mount& o) { return o.prefix == m.prefix; }),
                   g_mounts.end());
    g_mounts.push_back(std::move(m));
    // the most specific folder first: a music folder inside the asset root maps by its own mount
    std::stable_sort(g_mounts.begin(), g_mounts.end(),
                     [](const Mount& a, const Mount& b) { return a.dir.size() > b.dir.size(); });
}

std::size_t set_layers(std::vector<Layer> layers) {
    g_layers = std::move(layers);
    g_table.clear();
    for (int i = 0; i < int(g_layers.size()); ++i) {
        const fs::path& root = g_layers[std::size_t(i)].root;
        std::error_code ec;
        if (!fs::is_directory(root, ec)) continue;
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fec;
            if (!it->is_regular_file(fec)) continue;
            const std::string k = key(u8(it->path().lexically_relative(root)));
            if (k.empty() || k.rfind("..", 0) == 0) continue;
            Entry& e = g_table[k];
            if (e.layer >= 0) e.also.push_back(e.layer);
            e.file = it->path();
            e.layer = i;
        }
    }
    return g_table.size();
}

std::size_t set_language(const fs::path& dir) {
    g_lang.clear();
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) return 0;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        const std::string k = key(u8(it->path().lexically_relative(dir)));
        if (!k.empty() && k.rfind("..", 0) != 0) g_lang[k] = it->path();
    }
    return g_lang.size();
}

void reset() {
    g_mounts.clear();
    g_layers.clear();
    g_table.clear();
    g_lang.clear();
}

std::string virtual_name(const fs::path& p) {
    if (g_mounts.empty()) return {};
    const std::string s = norm(p);
    const std::string l = lower(s);
    for (const Mount& m : g_mounts) {
        if (l.size() <= m.dir.size() || l.compare(0, m.dir.size(), m.dir) != 0) continue;
        const std::string rest = s.substr(m.dir.size());
        return m.prefix.empty() ? rest : m.prefix + "/" + rest;
    }
    return {};
}

fs::path resolve(const fs::path& p) {
#ifndef _WIN32
    if (g_table.empty() && g_lang.empty()) return case_fold(p);   // no mods: the base path, its case found on disk
    const std::string vn = virtual_name(p);
    if (vn.empty()) return case_fold(p);
    const std::string k = key(vn);
    if (const auto it = g_table.find(k); it != g_table.end()) return it->second.file;
    const auto lt = g_lang.find(k);
    return lt == g_lang.end() ? case_fold(p) : lt->second;
#else
    if (g_table.empty() && g_lang.empty()) return p;   // no mods, no language overlay: the base path, untouched
    const std::string vn = virtual_name(p);
    if (vn.empty()) return p;
    const std::string k = key(vn);
    if (const auto it = g_table.find(k); it != g_table.end()) return it->second.file;
    const auto lt = g_lang.find(k);
    return lt == g_lang.end() ? p : lt->second;
#endif
}

fs::path locate(std::string_view vname) {
    const std::string k = key(vname);
    if (const auto it = g_table.find(k); it != g_table.end()) return it->second.file;
    if (const auto lt = g_lang.find(k); lt != g_lang.end()) return lt->second;
    // the mounts are sorted by folder length; for a virtual name the longest PREFIX must win
    const Mount* best = nullptr;
    for (const Mount& m : g_mounts) {
        const bool match = m.prefix.empty() ||
                           (k.size() > m.prefix.size() && k.compare(0, m.prefix.size(), m.prefix) == 0 &&
                            k[m.prefix.size()] == '/');
        if (match && (!best || m.prefix.size() > best->prefix.size())) best = &m;
    }
    if (!best) return {};
    std::string rest(vname);
    std::replace(rest.begin(), rest.end(), '\\', '/');
    while (!rest.empty() && rest.front() == '/') rest.erase(0, 1);
    if (!best->prefix.empty()) rest.erase(0, best->prefix.size() + 1);
    return best->path / fs::path(std::u8string(rest.begin(), rest.end()));
}

// One block read: a byte-wise istreambuf_iterator copy was ~10 ms per MB (a 1 MB effect texture's first use,
// a room's YMO and its textures at load).
std::vector<std::uint8_t> read(const fs::path& p) {
    std::ifstream f(resolve(p), std::ios::binary | std::ios::ate);
    if (!f) return {};
    const std::streamoff n = f.tellg();
    if (n <= 0) return {};
    std::vector<std::uint8_t> b(static_cast<std::size_t>(n));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(b.data()), n);
    b.resize(static_cast<std::size_t>(f.gcount()));
    return b;
}

bool exists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(resolve(p), ec);
}

const std::vector<Layer>& layers() { return g_layers; }
const std::unordered_map<std::string, Entry>& table() { return g_table; }

}  // namespace cleria::vfs
