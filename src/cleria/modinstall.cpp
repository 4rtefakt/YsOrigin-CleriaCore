// Mod files: inspect and install a .cleriamod / .zip mod (modinstall.hpp, build/shell_re/MODFILE.md).
#include "cleria/modinstall.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

#include "miniz.h"

namespace fs = std::filesystem;

namespace cleria::mods {
namespace {

std::string u8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool read_file(const fs::path& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool is_package_name(const fs::path& p) {
    const std::string e = lower(u8(p.extension()));
    return e == ".zip" || e == kModFileExt;
}

// The manifest of an installed copy: a folder's mod.ini, or a package's (read in memory). Empty id = none.
std::string installed_id(const fs::path& p, std::string& version) {
    std::error_code ec;
    if (fs::is_directory(p, ec)) {
        std::string text;
        if (!read_file(p / kManifestName, text)) return {};
        std::vector<std::string> err;
        const Manifest m = parse_manifest(text, err);
        version = m.version;
        return m.id;
    }
    if (!is_package_name(p)) return {};
    const Package k = inspect_package(p);
    version = k.m.version;
    return k.m.id;   // a broken package of the same id is an older copy too, if its manifest was readable
}

}  // namespace

// ---- inspect -------------------------------------------------------------------------------------
Package inspect_package(const fs::path& file) {
    Package k;
    std::string bytes;
    if (!read_file(file, bytes)) { k.error = "the file could not be read"; return k; }
    mz_zip_archive za{};
    if (!mz_zip_reader_init_mem(&za, bytes.data(), bytes.size(), 0)) { k.error = "not a valid zip file"; return k; }
    struct Closer { mz_zip_archive* z; ~Closer() { mz_zip_reader_end(z); } } closer{&za};
    const mz_uint n = mz_zip_reader_get_num_files(&za);
    std::set<std::string> tops;               // the top-level folders
    std::string ini_root, ini_top;            // "mod.ini" at the root / "<top>/mod.ini"
    int root_index = -1;
    std::vector<std::pair<std::string, int>> top_inis;
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&za, i, &st)) { k.error = "a zip entry is unreadable"; return k; }
        const std::string name = st.m_filename;
        if (!safe_zip_entry(name)) { k.error = "unsafe path in the zip: " + name; return k; }
        std::string norm = name;
        std::replace(norm.begin(), norm.end(), '\\', '/');
        const std::size_t slash = norm.find('/');
        if (slash != std::string::npos) {
            tops.insert(lower(norm.substr(0, slash)));
            if (lower(norm.substr(slash + 1)) == kManifestName) top_inis.push_back({lower(norm.substr(0, slash)), int(i)});
        } else if (lower(norm) == kManifestName && !mz_zip_reader_is_file_a_directory(&za, i)) {
            root_index = int(i);
        }
    }
    if (!mz_zip_validate_archive(&za, 0)) { k.error = "the zip is damaged (an entry does not unpack)"; return k; }
    // the loader's rule (mods.cpp extract_zip): mod.ini at the root, else in the one top-level folder
    int at = root_index;
    if (at < 0 && tops.size() == 1)
        for (const auto& [top, idx] : top_inis)
            if (top == *tops.begin()) at = idx;
    if (at < 0) { k.error = "no mod.ini in the zip (at its root or in one top-level folder)"; return k; }
    std::size_t len = 0;
    void* p = mz_zip_reader_extract_to_heap(&za, mz_uint(at), &len, 0);
    if (!p) { k.error = "mod.ini could not be unpacked"; return k; }
    const std::string text(static_cast<const char*>(p), len);
    mz_free(p);
    std::vector<std::string> errors;
    k.m = parse_manifest(text, errors);
    if (!errors.empty()) { k.error = errors.front(); return k; }
    if (k.m.api > kApiVersion)
        k.error = "needs a newer CleriaCore (mod API " + std::to_string(k.m.api) + ", this build has " +
                  std::to_string(kApiVersion) + ")";
    return k;
}

// ---- install -------------------------------------------------------------------------------------
std::string InstallResult::message() const {
    if (!ok) return "Could not install " + file + ": " + error;
    const std::string label = name.empty() ? id : name;
    const char* dash = " \xE2\x80\x94 enabled";
    if (!replaced) return "Installed " + label + " " + version + dash;
    if (old_version.empty() || compare_versions(old_version, version) == 0) return "Reinstalled " + label + " " + version + dash;
    return "Updated " + label + " " + old_version + " -> " + version + dash;
}

bool enable_in(Prefs& prefs, const std::vector<std::string>& ids) {
    bool changed = !prefs.use;
    prefs.use = true;
    for (const std::string& id : ids) {
        if (id.empty()) continue;
        if (std::find(prefs.order.begin(), prefs.order.end(), id) == prefs.order.end()) { prefs.order.push_back(id); changed = true; }
        if (std::find(prefs.active.begin(), prefs.active.end(), id) == prefs.active.end()) { prefs.active.push_back(id); changed = true; }
    }
    return changed;
}

InstallResult install(const fs::path& file, const fs::path& mods_dir, Prefs& prefs) {
    InstallResult r;
    r.file = u8(file.filename());
    // 1. every check first: nothing is written for an invalid file
    const Package k = inspect_package(file);
    if (!k.ok()) { r.error = k.error; return r; }
    r.id = k.m.id;
    r.name = k.m.name;
    r.version = k.m.version;
    std::error_code ec;
    fs::create_directories(mods_dir, ec);
    if (!fs::is_directory(mods_dir, ec)) { r.error = "the mods folder could not be created"; return r; }
    r.dest = mods_dir / (r.id + kModFileExt);
    // 2. the older copies of the same id (read before the new file lands)
    std::vector<fs::path> olds;
    for (const auto& d : fs::directory_iterator(mods_dir, ec)) {
        const fs::path p = d.path();
        const std::string fname = u8(p.filename());
        if (fname.empty() || fname[0] == '.') continue;
        std::string ver;
        if (installed_id(p, ver) != r.id) continue;
        r.replaced = true;
        if (r.old_version.empty() || compare_versions(ver, r.old_version) > 0) r.old_version = ver;
        olds.push_back(p);
    }
    // 3. the copy: to a hidden part file, then renamed over <id>.cleriamod (discovery skips '.' names)
    const bool same = fs::exists(r.dest, ec) && fs::equivalent(file, r.dest, ec);
    if (!same) {
        const fs::path part = mods_dir / ("." + r.id + kModFileExt + ".part");
        std::error_code e1, e2;
        fs::copy_file(file, part, fs::copy_options::overwrite_existing, e1);
        if (!e1 && fs::file_size(part, e2) != fs::file_size(file, e2)) e1 = std::make_error_code(std::errc::io_error);
        // a fresh write time: the loader re-extracts by size + time, and a new version may share both
        if (!e1) fs::last_write_time(part, fs::file_time_type::clock::now(), e1);
        if (!e1) fs::rename(part, r.dest, e1);
        if (e1) {
            fs::remove(part, e2);
            r.error = "could not copy it into the mods folder (" + e1.message() + ")";
            r.replaced = false;
            r.old_version.clear();
            return r;
        }
    }
    // 4. one copy per id: older packages deleted, older loose folders moved to .replaced/<name>
    for (const fs::path& p : olds) {
        std::error_code e1;
        if (fs::equivalent(p, r.dest, e1)) continue;
        if (fs::is_directory(p, e1)) {
            const fs::path aside = mods_dir / kReplacedDir / p.filename();
            fs::create_directories(aside.parent_path(), e1);
            fs::remove_all(aside, e1);
            fs::rename(p, aside, e1);
        } else {
            fs::remove(p, e1);
        }
        if (e1) r.warnings.push_back("could not remove the older copy " + u8(p.filename()) + " (" + e1.message() + ")");
        else r.removed.push_back(p);
    }
    // 5. on
    enable_in(prefs, {r.id});
    r.ok = true;
    return r;
}

// ---- the enable queue ----------------------------------------------------------------------------
void queue_enable(const fs::path& mods_dir, const std::string& id) {
    std::ofstream f(mods_dir / kEnableQueue, std::ios::binary | std::ios::app);
    f << id << "\n";
}

std::vector<std::string> take_enable_queue(const fs::path& mods_dir) {
    std::vector<std::string> ids;
    const fs::path q = mods_dir / kEnableQueue;
    std::string text;
    if (!read_file(q, text)) return ids;
    std::error_code ec;
    fs::remove(q, ec);
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = text.substr(i, j - i);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (valid_id(line) && std::find(ids.begin(), ids.end(), line) == ids.end()) ids.push_back(line);
        i = j + 1;
    }
    return ids;
}

// ---- single instance -----------------------------------------------------------------------------
std::string instance_key(const fs::path& mods_dir) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::absolute(mods_dir, ec), ec);
    if (p.empty()) p = mods_dir;
    std::string s = lower(u8(p.lexically_normal()));
    std::replace(s.begin(), s.end(), '\\', '/');
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    std::uint64_t h = 1469598103934665603ull;   // FNV-1a 64
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    char buf[40];
    std::snprintf(buf, sizeof buf, "CleriaCore.%016llx", static_cast<unsigned long long>(h));
    return buf;
}

}  // namespace cleria::mods
