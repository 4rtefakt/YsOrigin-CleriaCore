// cleria-modfiletest — mod files (build/shell_re/MODFILE.md): the .cleriamod package checks, installing
// into a scratch mods folder (fresh, update, reinstall, invalid files leaving nothing behind, the order /
// active choices), the loader taking .cleriamod files, the enable queue, the single-instance key, and the
// .cleriamod file association against a FAKE registry (the real one is never opened here).
// Usage: cleria-modfiletest   (writes only under %TEMP%)
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cleria/fileassoc.hpp"
#include "cleria/modinstall.hpp"
#include "cleria/mods.hpp"
#include "miniz.h"

using namespace cleria;
namespace fs = std::filesystem;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[modfile] %-88s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

static void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
static std::string manifest(const std::string& id, const std::string& version, const std::string& extra = "") {
    return "[mod]\r\nId=" + id + "\r\nName=\"" + id + " mod\"\r\nVersion=" + version + "\r\n" + extra;
}
static bool make_zip(const fs::path& out, const std::vector<std::pair<std::string, std::string>>& files) {
    mz_zip_archive z{};
    if (!mz_zip_writer_init_heap(&z, 0, 0)) return false;
    for (const auto& [n, t] : files)
        if (!mz_zip_writer_add_mem(&z, n.c_str(), t.data(), t.size(), MZ_DEFAULT_LEVEL)) { mz_zip_writer_end(&z); return false; }
    void* buf = nullptr;
    std::size_t len = 0;
    const bool ok = mz_zip_writer_finalize_heap_archive(&z, &buf, &len);
    if (ok) {
        fs::create_directories(out.parent_path());
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        f.write(static_cast<const char*>(buf), std::streamsize(len));
    }
    mz_free(buf);
    mz_zip_writer_end(&z);
    return ok;
}
// Every name in a folder, recursively ("a/b.txt", "a/"), to compare "nothing changed".
static std::set<std::string> listing(const fs::path& dir) {
    std::set<std::string> s;
    std::error_code ec;
    if (!fs::exists(dir, ec)) return s;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        s.insert(fs::relative(it->path(), dir).generic_string() + (it->is_directory() ? "/" : ""));
    return s;
}
static bool has(const std::vector<std::string>& v, const std::string& x) {
    for (const std::string& s : v) if (s == x) return true;
    return false;
}

// HKCU as a map: key (lowercased) -> name -> value.
struct FakeRegistry final : fileassoc::Registry {
    std::map<std::string, std::map<std::string, std::string>> keys;
    int writes = 0;
    static std::string low(std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; }
    std::optional<std::string> get(const std::string& key, const std::string& name) override {
        const auto k = keys.find(low(key));
        if (k == keys.end()) return std::nullopt;
        const auto v = k->second.find(low(name));
        if (v == k->second.end()) return std::nullopt;
        return v->second;
    }
    bool set(const std::string& key, const std::string& name, const std::string& value) override {
        ++writes;
        std::string k = low(key);   // RegCreateKeyEx makes the parents
        for (std::size_t p = k.find('\\'); p != std::string::npos; p = k.find('\\', p + 1)) keys[k.substr(0, p)];
        keys[k][low(name)] = value;
        return true;
    }
    bool erase_value(const std::string& key, const std::string& name) override {
        ++writes;
        const auto k = keys.find(low(key));
        if (k != keys.end()) k->second.erase(low(name));
        return true;
    }
    bool erase_tree(const std::string& key) override {
        ++writes;
        const std::string k = low(key);
        for (auto it = keys.begin(); it != keys.end();)
            it = (it->first == k || it->first.rfind(k + "\\", 0) == 0) ? keys.erase(it) : std::next(it);
        return true;
    }
    bool erase_key_if_empty(const std::string& key) override {
        const std::string k = low(key);
        const auto it = keys.find(k);
        if (it == keys.end()) return true;
        if (!it->second.empty()) return false;
        for (const auto& [o, _] : keys) if (o.rfind(k + "\\", 0) == 0) return false;
        ++writes;
        keys.erase(it);
        return true;
    }
};

int main() {
    const fs::path tmp = fs::temp_directory_path() / "cleria_modfiletest";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    const fs::path src = tmp / "downloads", mods = tmp / "mods";

    // ---- package checks ----
    check(make_zip(src / "good-0.1.0.cleriamod", {{"test.good/mod.ini", manifest("test.good", "0.1.0", "ApiVersion=3\r\n")},
                                                  {"test.good/files/IMAGE/A.DDS", "a"}}),
          "fixture: a .cleriamod with one top-level folder");
    {
        const mods::Package k = mods::inspect_package(src / "good-0.1.0.cleriamod");
        check(k.ok() && k.m.id == "test.good" && k.m.version == "0.1.0", "inspect: id and version from <top>/mod.ini");
    }
    make_zip(src / "flat.zip", {{"mod.ini", manifest("test.flat", "2")}, {"files/X.DDS", "x"}});
    check(mods::inspect_package(src / "flat.zip").ok(), "inspect: mod.ini at the zip root (a .zip installs too)");
    put(src / "junk.cleriamod", "this is not a zip");
    make_zip(src / "noini.cleriamod", {{"a/readme.txt", "x"}});
    make_zip(src / "twotops.cleriamod", {{"a/mod.ini", manifest("test.a", "1")}, {"b/x.txt", "x"}});
    make_zip(src / "badid.cleriamod", {{"bad/mod.ini", manifest("Bad Id!", "1")}});
    make_zip(src / "noid.cleriamod", {{"x/mod.ini", "[mod]\r\nName=\"x\"\r\n"}});
    make_zip(src / "evil.cleriamod", {{"test.evil/mod.ini", manifest("test.evil", "1")}, {"../../escaped.txt", "x"}});
    make_zip(src / "abs.cleriamod", {{"test.abs/mod.ini", manifest("test.abs", "1")}, {"C:/escaped.txt", "x"}});
    make_zip(src / "future.cleriamod", {{"test.future/mod.ini", manifest("test.future", "1", "ApiVersion=99\r\n")}});
    {   // a zip whose one entry's data is damaged (the CRC no longer matches)
        make_zip(src / "damaged.cleriamod", {{"test.dmg/mod.ini", manifest("test.dmg", "1")},
                                             {"test.dmg/files/B.DDS", std::string(4000, 'b')}});
        std::fstream f(src / "damaged.cleriamod", std::ios::in | std::ios::out | std::ios::binary);
        std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        const std::size_t at = bytes.find("test.dmg/files/B.DDS");   // the local header's name; its data follows
        f.seekp(std::streamoff(at + 24));
        f.write("\x13\x37\x13\x37", 4);
    }
    struct Bad { const char* file; const char* why; const char* what; };
    const Bad bads[] = {
        {"junk.cleriamod", "not a valid zip", "invalid: not a zip"},
        {"noini.cleriamod", "no mod.ini", "invalid: no mod.ini"},
        {"twotops.cleriamod", "no mod.ini", "invalid: two top-level folders (the loader's rule)"},
        {"badid.cleriamod", "invalid Id", "invalid: a bad id"},
        {"noid.cleriamod", "no [mod] Id", "invalid: no id"},
        {"evil.cleriamod", "unsafe path", "invalid: a '..' entry"},
        {"abs.cleriamod", "unsafe path", "invalid: a drive-letter entry"},
        {"future.cleriamod", "needs a newer CleriaCore", "invalid: ApiVersion too new"},
        {"damaged.cleriamod", "damaged", "invalid: a damaged entry (CRC)"},
        {"missing.cleriamod", "could not be read", "invalid: no such file"},
    };
    for (const Bad& b : bads) {
        const mods::Package k = mods::inspect_package(src / b.file);
        check(!k.ok() && k.error.find(b.why) != std::string::npos, b.what);
    }

    // ---- install: fresh ----
    mods::Prefs prefs{true, {"other.mod"}, {}};
    {
        const mods::InstallResult r = mods::install(src / "good-0.1.0.cleriamod", mods, prefs);
        check(r.ok && r.id == "test.good" && !r.replaced && r.dest == mods / "test.good.cleriamod" && fs::exists(r.dest),
              "install: copied to <mods>/<id>.cleriamod");
        check(r.message() == "Installed test.good mod 0.1.0 \xE2\x80\x94 enabled", "install: \"Installed <name> <version> - enabled\"");
        check(prefs.use && prefs.order == std::vector<std::string>{"other.mod", "test.good"} &&
                  prefs.active == std::vector<std::string>{"test.good"},
              "install: appended to Order, added to Active, UseMods on");
        check(listing(mods) == std::set<std::string>{"test.good.cleriamod"}, "install: nothing else written (no part file left)");
    }
    // ---- the loader takes .cleriamod ----
    {
        std::vector<mods::Mod> v = mods::discover(mods);
        check(v.size() == 1 && v[0].m.id == "test.good" && v[0].zip && v[0].problems.empty() && v[0].files == 1,
              "loader: a .cleriamod is discovered like a zip");
        check(fs::exists(mods / ".cache" / "test.good.cleriamod" / "test.good" / "mod.ini"),
              "loader: extracted to .cache/<name>.cleriamod/");
        mods::arrange(v, prefs);
        check(v[0].state == mods::State::Active, "loader: installed = enabled (Active)");
    }
    // ---- update: a newer version replaces it, the older loose folder and zip of the same id go ----
    {
        make_zip(src / "good-0.2.0.cleriamod", {{"test.good/mod.ini", manifest("test.good", "0.2.0")},
                                                {"test.good/files/IMAGE/A.DDS", "a2"}, {"test.good/files/IMAGE/B.DDS", "b2"}});
        put(mods / "good_dev" / "mod.ini", manifest("test.good", "0.0.9"));
        put(mods / "good_dev" / "files" / "C.DDS", "c");
        make_zip(mods / "good-old.zip", {{"mod.ini", manifest("test.good", "0.0.5")}});
        put(mods / "unrelated" / "mod.ini", manifest("test.unrelated", "1"));
        mods::Prefs before = prefs;
        const mods::InstallResult r = mods::install(src / "good-0.2.0.cleriamod", mods, prefs);
        check(r.ok && r.replaced && r.old_version == "0.1.0" && r.removed.size() == 2 && r.warnings.empty(),
              "update: the old copies found (the highest old version reported)");
        check(r.message() == "Updated test.good mod 0.1.0 -> 0.2.0 \xE2\x80\x94 enabled", "update: \"Updated <name> 0.1.0 -> 0.2.0\"");
        check(!fs::exists(mods / "good-old.zip") && !fs::exists(mods / "good_dev") &&
                  fs::exists(mods / ".replaced" / "good_dev" / "files" / "C.DDS") && fs::exists(mods / "unrelated" / "mod.ini"),
              "update: the older zip deleted, the loose folder moved to .replaced/, other mods untouched");
        check(prefs.order == before.order && prefs.active == before.active, "update: Order / Active unchanged (already there)");
        std::vector<mods::Mod> v = mods::discover(mods);
        int n = 0;
        for (const mods::Mod& m : v) n += m.m.id == "test.good";
        const mods::Mod* g = nullptr;
        for (const mods::Mod& m : v) if (m.m.id == "test.good") g = &m;
        check(n == 1 && g && g->m.version == "0.2.0" && g->files == 2, "update: one copy of the id, the new version (re-extracted)");
    }
    // ---- reinstall the same version; install the file already in the mods folder ----
    {
        const mods::InstallResult r = mods::install(src / "good-0.2.0.cleriamod", mods, prefs);
        check(r.ok && r.message().rfind("Reinstalled test.good mod 0.2.0", 0) == 0, "reinstall: same version -> \"Reinstalled\"");
        const mods::InstallResult s = mods::install(mods / "test.good.cleriamod", mods, prefs);
        check(s.ok && fs::exists(mods / "test.good.cleriamod"), "reinstall: the installed file itself (no self-delete)");
    }
    // ---- a turned-off mod gets turned on; a .zip installs as <id>.cleriamod ----
    {
        mods::Prefs p{false, {"test.flat"}, {}};
        const mods::InstallResult r = mods::install(src / "flat.zip", mods, p);
        check(r.ok && fs::exists(mods / "test.flat.cleriamod") && p.use && p.order == std::vector<std::string>{"test.flat"} &&
                  has(p.active, "test.flat"),
              "install: a .zip -> <id>.cleriamod, its order kept, turned on, UseMods back on");
    }
    // ---- invalid files: nothing written anywhere, prefs untouched ----
    {
        const std::set<std::string> was = listing(mods);
        const mods::Prefs before = prefs;
        bool all = true;
        for (const Bad& b : bads) {
            const mods::InstallResult r = mods::install(src / b.file, mods, prefs);
            all = all && !r.ok && r.message().rfind(std::string("Could not install ") + b.file + ": ", 0) == 0;
        }
        check(all, "invalid: every bad file refused with \"Could not install <file>: <why>\"");
        check(listing(mods) == was, "invalid: the mods folder is exactly as before (no partial copy)");
        check(prefs.order == before.order && prefs.active == before.active && prefs.use == before.use, "invalid: Order / Active untouched");
        const fs::path fresh = tmp / "fresh_mods";
        mods::Prefs p;
        const mods::InstallResult r = mods::install(src / "evil.cleriamod", fresh, p);
        check(!r.ok && !fs::exists(fresh) && p.order.empty() && p.active.empty(), "invalid: not even the mods folder is created");
    }
    // ---- a missing dependency installs, and shows the usual error ----
    {
        make_zip(src / "needy.cleriamod", {{"test.needy/mod.ini", manifest("test.needy", "1", "Requires=test.absent>=1\r\n")}});
        mods::Prefs p = prefs;
        const mods::InstallResult r = mods::install(src / "needy.cleriamod", mods, p);
        std::vector<mods::Mod> v = mods::discover(mods);
        mods::arrange(v, p);
        const mods::Mod* m = nullptr;
        for (const mods::Mod& x : v) if (x.m.id == "test.needy") m = &x;
        check(r.ok && m && m->enabled && m->state == mods::State::Error && !m->errors.empty() &&
                  m->errors.back().find("needs test.absent") != std::string::npos,
              "dependency missing: installed and enabled, in Error (\"needs test.absent ...\")");
    }
    // ---- the enable queue (a CleriaCore was running) ----
    {
        mods::queue_enable(mods, "test.good");
        mods::queue_enable(mods, "test.q");
        mods::queue_enable(mods, "test.q");
        mods::queue_enable(mods, "Bad Id");
        const std::vector<std::string> q = mods::take_enable_queue(mods);
        check(q == std::vector<std::string>{"test.good", "test.q"} && !fs::exists(mods / mods::kEnableQueue),
              "queue: ids read once, deduplicated, invalid dropped, the file deleted");
        check(mods::take_enable_queue(mods).empty(), "queue: empty afterwards");
        mods::Prefs p{false, {"test.good"}, {}};
        check(mods::enable_in(p, q) && p.use && p.order == std::vector<std::string>{"test.good", "test.q"} &&
                  p.active == std::vector<std::string>{"test.good", "test.q"} && !mods::enable_in(p, q),
              "queue: enable_in turns them on (and reports no change the second time)");
    }
    // ---- single instance ----
    {
        const std::string a = mods::instance_key(mods), b = mods::instance_key(tmp / "MODS" / "."),
                          c = mods::instance_key(tmp / "other");
        check(a.rfind("CleriaCore.", 0) == 0 && a.size() == 27 && a == b && a != c,
              "instance key: per mods folder, case / '.' insensitive");
    }

    // ---- the file association, on a fake registry ----
    {
        const std::string exe = "C:\\Games\\CleriaCore\\cleria-view.exe", other = "D:\\Old\\cleria-view.exe";
        const auto es = fileassoc::entries(exe);
        std::map<std::string, std::string> want;
        for (const auto& e : es) want[e.key + "|" + e.name] = e.value;
        check(es.size() == 5 && want["Software\\Classes\\.cleriamod|"] == "CleriaCore.Mod" &&
                  want["Software\\Classes\\.cleriamod\\OpenWithProgids|CleriaCore.Mod"] == "" &&
                  want["Software\\Classes\\CleriaCore.Mod|"] == "CleriaCore mod" &&
                  want["Software\\Classes\\CleriaCore.Mod\\DefaultIcon|"] == "\"C:\\Games\\CleriaCore\\cleria-view.exe\",0" &&
                  want["Software\\Classes\\CleriaCore.Mod\\shell\\open\\command|"] ==
                      "\"C:\\Games\\CleriaCore\\cleria-view.exe\" \"%1\"",
              "assoc: the five HKCU\\Software\\Classes values (.cleriamod -> CleriaCore.Mod, icon, \"exe\" \"%1\")");
        FakeRegistry r;
        check(fileassoc::decide(r, exe, true) == fileassoc::Action::Register, "assoc: missing + on -> register");
        check(fileassoc::decide(r, exe, false) == fileassoc::Action::None, "assoc: missing + off -> nothing");
        bool ok = false;
        check(fileassoc::sync(r, exe, true, &ok) == fileassoc::Action::Register && ok && fileassoc::is_current(r, exe) &&
                  fileassoc::owned(r, exe),
              "assoc: registered");
        const int w = r.writes;
        check(fileassoc::sync(r, exe, true) == fileassoc::Action::None && r.writes == w, "assoc: already current -> no write at all");
        check(fileassoc::decide(r, "c:\\games\\cleriacore\\CLERIA-VIEW.EXE", true) == fileassoc::Action::None,
              "assoc: the exe path compares case-insensitively");
        check(fileassoc::decide(r, other, true) == fileassoc::Action::Register, "assoc: registered to another exe + on -> re-register");
        check(fileassoc::decide(r, other, false) == fileassoc::Action::None && !fileassoc::do_unregister(r, other) &&
                  fileassoc::is_current(r, exe),
              "assoc: off from another exe -> its registration left alone");
        r.set("Software\\Classes\\CleriaCore.Mod\\DefaultIcon", "", "x");
        check(fileassoc::decide(r, exe, true) == fileassoc::Action::Register, "assoc: one value changed -> re-register");
        fileassoc::sync(r, exe, true);
        r.set("Software\\Classes\\.cleriamod\\OpenWithProgids", "Other.Tool", "");   // another program's entry
        check(fileassoc::sync(r, exe, false, &ok) == fileassoc::Action::Unregister && ok && !fileassoc::owned(r, exe) &&
                  !r.get("Software\\Classes\\CleriaCore.Mod", "") && !r.get("Software\\Classes\\.cleriamod", "") &&
                  !r.get("Software\\Classes\\.cleriamod\\OpenWithProgids", "CleriaCore.Mod") &&
                  r.get("Software\\Classes\\.cleriamod\\OpenWithProgids", "Other.Tool"),
              "assoc: off -> ours removed, another program's OpenWithProgids entry kept");
        FakeRegistry r2;
        fileassoc::sync(r2, exe, true);
        fileassoc::sync(r2, exe, false);
        bool clean = true;
        for (const auto& [k, v] : r2.keys)
            if (k.find("cleriamod") != std::string::npos || k.find("cleriacore") != std::string::npos) clean = false;
        check(clean, "assoc: register then unregister leaves no CleriaCore key behind");
        FakeRegistry r3;
        r3.set("Software\\Classes\\.cleriamod", "", "Someone.Else");
        r3.set("Software\\Classes\\CleriaCore.Mod\\shell\\open\\command", "", fileassoc::open_command(exe));
        fileassoc::do_unregister(r3, exe);
        check(r3.get("Software\\Classes\\.cleriamod", "") == std::optional<std::string>("Someone.Else"),
              "assoc: unregister keeps a .cleriamod default that names another ProgID");
    }

    // ---- the freedesktop association (Linux) on a scratch XDG_DATA_HOME ----
    {
        namespace xdg = fileassoc::xdg;
        const fs::path home = tmp / "xdg-data";
        const std::string exe = "/opt/Cleria Core/cleria-view", other = "/home/x/cleria-view";
        const std::string de = xdg::desktop_entry(exe);
        check(de.find("Exec=\"/opt/Cleria Core/cleria-view\" %f\n") != std::string::npos &&
                  de.find("MimeType=application/x-cleriamod;\n") != std::string::npos,
              "xdg: the .desktop entry runs \"<exe>\" %f for application/x-cleriamod");
        check(xdg::desktop_entry("/a/$b\\c").find("Exec=\"/a/\\\\$b\\\\\\\\c\" %f") != std::string::npos,
              "xdg: Exec quoting escapes $ and \\ (spec: argument quoting, then string escaping)");
        check(xdg::mime_xml().find("<glob pattern=\"*.cleriamod\"/>") != std::string::npos, "xdg: the mime type globs *.cleriamod");
        check(xdg::decide(home, exe, true) == fileassoc::Action::Register && xdg::decide(home, exe, false) == fileassoc::Action::None,
              "xdg: missing -> register when on, nothing when off");
        bool ok = false;
        check(xdg::sync(home, exe, true, &ok) == fileassoc::Action::Register && ok && xdg::is_current(home, exe) &&
                  fs::exists(home / "applications" / xdg::kDesktopName) &&
                  fs::exists(home / "mime" / "packages" / xdg::kMimeName),
              "xdg: register writes applications/cleriacore-mod.desktop + mime/packages/cleriacore-mod.xml");
        check(xdg::sync(home, exe, true) == fileassoc::Action::None, "xdg: already current -> nothing");
        check(xdg::decide(home, other, true) == fileassoc::Action::Register && !xdg::do_unregister(home, other) &&
                  xdg::is_current(home, exe),
              "xdg: another exe's entry is not removed by this one");
        check(xdg::sync(home, exe, false, &ok) == fileassoc::Action::Unregister && ok &&
                  !fs::exists(home / "applications" / xdg::kDesktopName) &&
                  !fs::exists(home / "mime" / "packages" / xdg::kMimeName),
              "xdg: off -> both files removed");
    }

    fs::remove_all(tmp, ec);
    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
