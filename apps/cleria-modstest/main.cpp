// cleria-modstest — mods (src/cleria/mods) and the virtual file system (src/cleria/vfs) against
// build/shell_re/MODS.md: the mod.ini manifest, versions and dependencies, discovery of folder and
// zip mods, the load order and its checks, and the vfs resolution order.
// Usage: cleria-modstest [asset_root]   (writes only under %TEMP%; the asset root is only read)
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cleria/mods.hpp"
#include "cleria/vfs.hpp"
#include "miniz.h"

using namespace cleria;
namespace fs = std::filesystem;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[mods] %-84s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

static void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
}
static std::string slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
static std::string manifest(const std::string& id, const std::string& extra = "") {
    return "[mod]\r\nId=" + id + "\r\nName=\"" + id + " mod\"\r\nVersion=1.0\r\n" + extra;
}
// A zip of {name, text} entries, written to `out`.
static bool make_zip(const fs::path& out, const std::vector<std::pair<std::string, std::string>>& files) {
    mz_zip_archive z{};
    if (!mz_zip_writer_init_heap(&z, 0, 0)) return false;
    for (const auto& [n, t] : files)
        if (!mz_zip_writer_add_mem(&z, n.c_str(), t.data(), t.size(), MZ_DEFAULT_LEVEL)) { mz_zip_writer_end(&z); return false; }
    void* buf = nullptr;
    std::size_t len = 0;
    const bool ok = mz_zip_writer_finalize_heap_archive(&z, &buf, &len);
    if (ok) { std::ofstream f(out, std::ios::binary | std::ios::trunc); f.write(static_cast<const char*>(buf), std::streamsize(len)); }
    mz_free(buf);
    mz_zip_writer_end(&z);
    return ok;
}
static const mods::Mod* by_id(const std::vector<mods::Mod>& v, const std::string& id) {
    for (const mods::Mod& m : v) if (m.m.id == id) return &m;
    return nullptr;
}
static std::string order_of(const std::vector<mods::Mod>& v) {
    std::string s;
    for (const mods::Mod& m : v) s += (s.empty() ? "" : ",") + m.m.id;
    return s;
}
static bool has_error(const mods::Mod* m, const std::string& part) {
    if (!m) return false;
    for (const std::string& e : m->errors) if (e.find(part) != std::string::npos) return true;
    return false;
}

int main(int argc, char** argv) {
    // ---- the manifest ----
    {
        std::vector<std::string> err;
        const mods::Manifest m = mods::parse_manifest(
            "; a comment\r\n[mod]\r\nId=Sample.Checker_Floor\r\nName=\"Checker Floor\"\r\nVersion=1.2.0\r\n"
            "Author=CleriaCore\r\nDescription=\"A test, with \"\"quotes\"\" inside\"\r\nPriority=-5\r\nApiVersion=1\r\n"
            "Requires=\"base.lib >= 1.1, other\"\r\nConflicts=old.floor<2\r\n[other]\r\nId=ignored\r\n", err);
        check(err.empty() && m.id == "sample.checker_floor" && m.name == "Checker Floor" && m.version == "1.2.0" &&
                  m.author == "CleriaCore" && m.priority == -5 && m.api == 1,
              "manifest: [mod] keys, quoted / unquoted values, the id lowercased");
        check(m.requires_.size() == 2 && m.requires_[0].id == "base.lib" && m.requires_[0].op == ">=" &&
                  m.requires_[0].version == "1.1" && m.requires_[1].id == "other" && m.requires_[1].op.empty() &&
                  m.conflicts.size() == 1 && m.conflicts[0].id == "old.floor" && m.conflicts[0].op == "<",
              "manifest: Requires / Conflicts lists with version constraints");
        std::vector<std::string> e1, e2, e3;
        mods::parse_manifest("[mod]\r\nName=x\r\n", e1);
        mods::parse_manifest("[mod]\r\nId=has space\r\n", e2);
        mods::parse_manifest("[mod]\r\nId=a\r\nRequires=\"b >=\"\r\n", e3);
        check(e1.size() == 1 && e2.size() == 1 && e3.size() == 1, "manifest errors: no Id, an invalid Id, a bad Requires entry");
        const mods::Manifest d = mods::parse_manifest("[MOD]\r\nid=x\r\n", e1);
        check(d.version == "0" && d.name.empty() && d.priority == 0 && d.api == mods::kApiVersion && d.id == "x",
              "manifest defaults; section and key names are case-insensitive");
    }
    // ---- versions, deps, lists ----
    {
        check(mods::compare_versions("1.10", "1.9") > 0 && mods::compare_versions("1.0", "1") == 0 &&
                  mods::compare_versions("2.0.1", "2.0") > 0 && mods::compare_versions("1.0a", "1.0b") < 0 &&
                  mods::compare_versions("0.9", "1.0") < 0,
              "versions: dotted numeric (1.10 > 1.9, 1.0 == 1), text parts compare as text");
        const mods::Dep a = mods::parse_dep(" lib >= 2 "), b = mods::parse_dep("lib==2"), c = mods::parse_dep("x>>1");
        check(a.id == "lib" && a.op == ">=" && b.op == "=" && c.id.empty(), "deps: id op version, == as =, bad op rejected");
        check(mods::satisfies("2.1", a) && !mods::satisfies("1.9", a) && mods::satisfies("2", b) &&
                  mods::satisfies("anything", mods::parse_dep("lib")),
              "deps: satisfies() per operator; no operator = any version");
        check(mods::split_list(" a, b ,,c ") == std::vector<std::string>{"a", "b", "c"} &&
                  mods::join_list({"a", "b"}) == "a,b" && mods::split_list("").empty(),
              "lists: split trims and drops empties; join with ','");
    }

    const fs::path tmp = fs::temp_directory_path() / "cleria-modstest";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    const fs::path base = tmp / "assets", dir = tmp / "mods", music = tmp / "release" / "music";

    // ---- the vfs ----
    {
        put(base / "MAP" / "S_10" / "A.DDS", "base-a");
        put(base / "IMAGE" / "B.DDS", "base-b");
        put(music / "YSO_001.ogg", "base-music");
        put(tmp / "L1" / "map" / "s_10" / "a.dds", "l1-a");
        put(tmp / "L1" / "IMAGE" / "NEW.DDS", "l1-new");
        put(tmp / "L2" / "MAP" / "S_10" / "A.DDS", "l2-a");
        put(tmp / "L2" / "music" / "YSO_001.ogg", "l2-music");
        vfs::reset();
        const fs::path a = base / "MAP" / "S_10" / "A.DDS";
        vfs::mount("", base);
        check(vfs::resolve(a) == a && vfs::table().empty(), "vfs: no layers -> every path resolves to itself");
        vfs::mount("music", music);
        const std::size_t n = vfs::set_layers({{"l1", tmp / "L1"}, {"l2", tmp / "L2"}});
        check(n == 3, "vfs: the name table holds each distinct virtual name once (3)");
        check(slurp(vfs::resolve(a)) == "l2-a", "vfs: the later layer wins a name both provide");
        check(slurp(vfs::resolve(base / "map/s_10/a.dds")) == "l2-a" &&
                  slurp(vfs::resolve(fs::path(base.string() + "\\MAP\\S_10\\..\\S_10\\A.DDS"))) == "l2-a",
              "vfs: case-insensitive, '\\' == '/', '..' normalised");
        check(vfs::resolve(base / "IMAGE" / "B.DDS") == base / "IMAGE" / "B.DDS", "vfs: a name no layer has stays the base file");
        check(slurp(vfs::resolve(music / "YSO_001.ogg")) == "l2-music", "vfs: the music mount: music/<track> in a layer");
        check(vfs::resolve(tmp / "elsewhere" / "A.DDS") == tmp / "elsewhere" / "A.DDS", "vfs: a path under no mount is untouched");
        check(vfs::virtual_name(a) == "MAP/S_10/A.DDS" && vfs::virtual_name(music / "x.ogg") == "music/x.ogg" &&
                  vfs::virtual_name(tmp / "x").empty(),
              "vfs: virtual_name through the mounts (the most specific folder first)");
        check(vfs::exists(base / "IMAGE" / "NEW.DDS") && slurp(vfs::locate("image/new.dds")) == "l1-new" &&
                  vfs::locate("IMAGE/B.DDS") == base / "IMAGE" / "B.DDS" &&
                  vfs::locate("music/YSO_002.ogg") == music / "YSO_002.ogg",
              "vfs: a NEW file a layer adds exists under the mount; locate() of virtual names");
        const auto it = vfs::table().find("map/s_10/a.dds");
        check(it != vfs::table().end() && it->second.layer == 1 && it->second.also == std::vector<int>{0},
              "vfs: the table records the overridden lower layer (conflict report)");
        const std::vector<std::uint8_t> rd = vfs::read(a);
        check(std::string(rd.begin(), rd.end()) == "l2-a", "vfs: read() goes through resolve()");
        vfs::reset();
        check(vfs::resolve(a) == a, "vfs: reset() -> identity again");
        // The game's names ignore case: a file is found whatever the case it is asked for in (Windows by its
        // file system; Linux / macOS by vfs's case-insensitive lookup), '\' separators too.
        check(slurp(vfs::resolve(base / "map" / "s_10" / "a.dds")) == "base-a" &&
                  slurp(vfs::resolve(fs::path(base.string() + "\\image\\b.dds"))) == "base-b",
              "vfs: no layers -> a base file in another case / with '\\' still opens");
    }

    // ---- discovery ----
    {
        put(dir / "a_floor" / "mod.ini", manifest("a.floor", "Priority=5\r\n"));
        put(dir / "a_floor" / "files" / "MAP" / "S_10" / "A.DDS", "a-floor");
        put(dir / "b_floor" / "mod.ini", manifest("b.floor", "Priority=-1\r\n"));
        put(dir / "b_floor" / "files" / "MAP" / "S_10" / "A.DDS", "b-floor");
        put(dir / "b_floor" / "files" / "IMAGE" / "B.DDS", "b-b");
        put(dir / "not_a_mod" / "readme.txt", "x");
        put(dir / "bad_manifest" / "mod.ini", "[mod]\r\nName=no id\r\n");
        put(dir / "notes.txt", "x");
        check(make_zip(dir / "zipped.zip", {{"zipped/mod.ini", manifest("z.mod")},
                                            {"zipped/files/IMAGE/B.DDS", "zip-b"}}),
              "zip: a test zip with one top-level folder was written");
        check(make_zip(dir / "flat.zip", {{"mod.ini", manifest("flat.mod")}, {"files/IMAGE/C.DDS", "flat-c"}}),
              "zip: a test zip with mod.ini at its root was written");
        check(make_zip(dir / "evil.zip", {{"mod.ini", manifest("evil.mod")}, {"../../escaped.txt", "x"}}),
              "zip: a test zip with a '..' entry was written");
        put(dir / "junk.zip", "not a zip");
        const std::vector<mods::Mod> v = mods::discover(dir);
        // sorted by source name: a_floor, b_floor, bad_manifest, evil.zip, flat.zip, junk.zip, zipped.zip
        check(v.size() == 7, "discover: 3 folders with mod.ini + 4 zips; plain folders / files skipped");
        const mods::Mod* z = by_id(v, "z.mod");
        check(z && z->zip && z->root == dir / ".cache" / "zipped" / "zipped" && z->files == 1 && z->problems.empty() &&
                  slurp(z->files_dir() / "IMAGE" / "B.DDS") == "zip-b",
              "discover: a zip is extracted to .cache/<stem>/, its mod.ini found in the top folder");
        const mods::Mod* f = by_id(v, "flat.mod");
        check(f && f->root == dir / ".cache" / "flat" && f->files == 1, "discover: a zip with mod.ini at its root");
        const mods::Mod* e = by_id(v, "evil.mod");
        check(!e && !fs::exists(tmp / "escaped.txt") && !fs::exists(dir / "escaped.txt"),
              "discover: a zip entry with '..' is refused (nothing written outside the cache)");
        int problems = 0;
        for (const mods::Mod& m : v) problems += !m.problems.empty();
        check(problems == 3, "discover: the bad manifest, the '..' zip and the junk zip carry their problem");
        // a second discover reuses the extraction (the .stamp matches)
        put(dir / ".cache" / "zipped" / "zipped" / "marker", "kept");
        mods::discover(dir);
        check(fs::exists(dir / ".cache" / "zipped" / "zipped" / "marker"), "discover: an unchanged zip is not extracted again");
    }

    // ---- order and checks ----
    {
        std::vector<mods::Mod> v = mods::discover(dir);
        mods::arrange(v, {});
        // no prefs: by Priority (b.floor -1, the rest 0, a.floor 5), then id; the id-less mods last
        check(order_of(v).rfind("b.floor,flat.mod,z.mod,a.floor", 0) == 0,
              "order: new mods by Priority then id; mods without an id last");
        bool none_on = true;
        for (const mods::Mod& m : v) none_on &= !m.enabled && m.state != mods::State::Active;
        check(none_on, "order: a newly found mod starts disabled");
        mods::Prefs p;
        p.order = {"a.floor", "z.mod", "b.floor"};
        p.active = {"a.floor", "b.floor"};
        mods::arrange(v, p);
        check(order_of(v).rfind("a.floor,z.mod,b.floor,flat.mod", 0) == 0, "order: the player's Order first, unknown ids after");
        check(by_id(v, "a.floor")->state == mods::State::Active && by_id(v, "z.mod")->state == mods::State::Disabled,
              "order: Active lists the enabled ids");
        const mods::Prefs back = mods::prefs_of(v, true);
        check(back.order.size() == 4 && back.order[0] == "a.floor" && back.active == std::vector<std::string>{"a.floor", "b.floor"},
              "prefs_of() reproduces the order and the choices");
        p.use = false;
        mods::arrange(v, p);
        check(by_id(v, "a.floor")->state == mods::State::Disabled && mods::layers(v).empty(), "UseMods=0: nothing loads");
    }
    {   // the dependency / conflict checks, on manifests alone
        auto mk = [](const std::string& id, const std::string& extra, const std::string& ver = "1.0") {
            mods::Mod m;
            std::vector<std::string> e;
            m.m = mods::parse_manifest("[mod]\r\nId=" + id + "\r\nVersion=" + ver + "\r\n" + extra, e);
            m.source = id;
            return m;
        };
        std::vector<mods::Mod> v = {mk("lib", ""), mk("needs.lib", "Requires=lib>=1.0\r\n"),
                                    mk("needs.new", "Requires=lib>=2\r\n"), mk("needs.gone", "Requires=gone\r\n"),
                                    mk("chain", "Requires=needs.gone\r\n"), mk("hates.lib", "Conflicts=lib\r\n"),
                                    mk("future", "ApiVersion=99\r\n"), mk("lib", "", "3.0")};
        mods::Prefs p;
        for (const mods::Mod& m : v) p.active.push_back(m.m.id);
        mods::arrange(v, p);
        check(by_id(v, "lib")->state == mods::State::Active && by_id(v, "needs.lib")->state == mods::State::Active,
              "checks: a satisfied dependency loads");
        check(has_error(by_id(v, "needs.new"), "needs lib >= 2 (found 1.0)"), "checks: a too-old dependency is an error");
        check(has_error(by_id(v, "needs.gone"), "not installed"), "checks: a missing dependency is an error");
        check(has_error(by_id(v, "chain"), "cannot load"), "checks: depending on a mod in error is an error (fixed point)");
        check(has_error(by_id(v, "hates.lib"), "conflicts with lib"), "checks: an enabled declared conflict is an error");
        check(has_error(by_id(v, "future"), "newer CleriaCore"), "checks: a newer ApiVersion is an error");
        int dups = 0;
        for (const mods::Mod& m : v) dups += has_error(&m, "already uses the id");
        check(dups == 1, "checks: a duplicate id: the second one is in error");
        p.active = {"needs.lib", "hates.lib"};
        mods::arrange(v, p);
        check(has_error(by_id(v, "needs.lib"), "turned off") && by_id(v, "hates.lib")->state == mods::State::Active,
              "checks: a disabled dependency is an error; a conflict with a disabled mod is not");
        check(by_id(v, "needs.gone")->state == mods::State::Disabled, "checks: a disabled mod is not checked");
    }

    // ---- the session: load() feeds the vfs ----
    {
        vfs::reset();
        vfs::mount("", base);
        mods::Prefs p;
        p.order = {"b.floor", "a.floor", "z.mod"};
        p.active = {"a.floor", "b.floor", "z.mod"};
        std::vector<mods::Mod> v = mods::load(dir, p);
        const fs::path a = base / "MAP" / "S_10" / "A.DDS", b = base / "IMAGE" / "B.DDS";
        check(slurp(vfs::resolve(a)) == "a-floor", "load: the mod lower in the list wins (a.floor over b.floor)");
        check(slurp(vfs::resolve(b)) == "zip-b", "load: a zip mod's files are served from its extraction");
        check(mods::active().size() == 3 && mods::active()[0].id == "b.floor" && mods::active()[2].name == "z.mod mod" &&
                  mods::active_summary() == "b.floor mod 1.0, a.floor mod 1.0, z.mod mod 1.0",
              "load: mods::active() lists the loaded mods in order (for speedrun mode)");
        const mods::Mod* bf = by_id(v, "b.floor");
        const mods::Mod* af = by_id(v, "a.floor");
        bool lost = false, won = false;
        for (const std::string& n : bf->notes) lost |= n.find("replaced by a.floor") != std::string::npos;
        for (const std::string& n : af->notes) won |= n.find("replaces 1 file(s) of b.floor") != std::string::npos;
        check(lost && won, "load: the shared-file conflicts are reported on both mods");
        mods::load(dir, {});
        check(mods::active().empty() && mods::active_summary() == "none" && vfs::resolve(a) == a,
              "load: nothing enabled -> unmodded, every path the base file");
        vfs::reset();
    }

    // ---- the real asset root (read only): an override of a real game path ----
    if (argc >= 2 && fs::exists(fs::path(argv[1]) / "MAP")) {
        const fs::path root = argv[1];
        const fs::path real = root / "MAP" / "S_10" / "COMMON" / "H" / "10YUKA04.DDS";
        vfs::reset();
        vfs::mount("", root);
        put(tmp / "R" / "map" / "s_10" / "common" / "h" / "10yuka04.dds", "over");
        vfs::set_layers({{"r", tmp / "R"}});
        check(slurp(vfs::resolve(real)) == "over" && vfs::resolve(root / "MAP" / "SCENELIST.SL") == root / "MAP" / "SCENELIST.SL",
              "real assets: an override of MAP/S_10/COMMON/H/10YUKA04.DDS; other files untouched");
        vfs::reset();
    } else {
        std::printf("[mods] (no asset root given: the real-path check is skipped)\n");
    }

    fs::remove_all(tmp, ec);
    std::printf("[mods] %s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
