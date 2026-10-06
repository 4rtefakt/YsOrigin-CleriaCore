// cleria-gamedata — locate a Ys Origin install and unpack its archives into CleriaCore's data
// folder (src/cleria/gamedata, build/shell_re/GAMEDATA.md). Replaces tools/extract_assets.py.
//
//   cleria-gamedata --find                          Steam installs found (validated)
//   cleria-gamedata --validate <dir>                is <dir> a usable install? (exit 0 = yes)
//   cleria-gamedata --install [<dir>] [--out <d>]   unpack (default: the first found install,
//                                                   into $CLERIA_DATA / %LOCALAPPDATA%\CleriaCore\data)
//   cleria-gamedata --list [<dir>] [filter]         the merged entries (data_us over data)
//   cleria-gamedata --status [<data dir>]           none / installing / ready (+ paths)
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "cleria/gamedata.hpp"
#include "cleria/ni_archive.hpp"

using namespace cleria;
namespace fs = std::filesystem;

static std::string u8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

static void print_validation(const gamedata::Validation& v) {
    std::printf("%s  %s\n", v.ok ? "OK     " : "INVALID", u8(v.root).c_str());
    for (const std::string& m : v.missing) std::printf("    missing: %s\n", m.c_str());
    for (const std::string& w : v.warnings) std::printf("    warning: %s\n", w.c_str());
}

static fs::path first_install() {
    for (const gamedata::Validation& v : gamedata::find_installs())
        if (v.ok) return v.root;
    return {};
}

static int usage() {
    std::fprintf(stderr,
                 "usage: cleria-gamedata --find | --validate <dir> | --install [<dir>] [--out <dir>] |\n"
                 "                       --list [<dir>] [filter] | --status [<data dir>]\n");
    return 2;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    std::vector<std::string> pos;
    fs::path out = gamedata::data_dir();
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
        else pos.push_back(argv[i]);
    }

    if (cmd == "--find") {
        const std::vector<gamedata::Validation> found = gamedata::find_installs();
        if (found.empty()) std::printf("no Steam install of Ys Origin found\n");
        for (const gamedata::Validation& v : found) print_validation(v);
        return found.empty() || !found.front().ok ? 1 : 0;
    }

    if (cmd == "--validate") {
        if (pos.empty()) return usage();
        const gamedata::Validation v = gamedata::validate(pos[0]);
        print_validation(v);
        return v.ok ? 0 : 1;
    }

    if (cmd == "--status") {
        const gamedata::Status s = gamedata::status(pos.empty() ? gamedata::data_dir() : fs::path(pos[0]));
        const char* st = s.state == gamedata::State::Ready ? "ready"
                         : s.state == gamedata::State::Installing ? "installing" : "none";
        std::printf("state:   %s%s%s\ndata:    %s\n", st, s.partial ? " (partial unpack present)" : "",
                    s.stale ? " (STALE: the install's data changed since)" : "", u8(s.data).c_str());
        if (s.manifest)
            std::printf("install: %s\nrelease: %s\nfiles:   %llu (%llu bytes)\nsource:  %s\ncreated: %s\n",
                        u8(s.install).c_str(), s.release.empty() ? "(gone)" : u8(s.release).c_str(),
                        (unsigned long long)s.manifest->files, (unsigned long long)s.manifest->bytes,
                        s.manifest->source_hash.c_str(), s.manifest->created.c_str());
        return s.state == gamedata::State::Ready ? 0 : 1;
    }

    const fs::path game = pos.empty() ? first_install() : fs::path(pos[0]);
    if (game.empty()) {
        std::fprintf(stderr, "no install given and none found; pass the Ys Origin folder\n");
        return 1;
    }

    if (cmd == "--list") {
        const gamedata::Validation v = gamedata::validate(game);
        if (!v.ok) { print_validation(v); return 1; }
        gamedata::GameArchives src;
        std::string err;
        if (!src.open(v.root, &err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 1;
        }
        const std::string filter = pos.size() > 1 ? ni::lookup_key(pos[1]) : std::string();
        std::size_t n = 0;
        for (const gamedata::GameArchives::File& f : src.files) {
            const ni::Entry& e = *f.entry;
            if (!filter.empty() && ni::lookup_key(e.name).find(filter) == std::string::npos) continue;
            std::printf("%-8s %10u %c %s\n", gamedata::GameArchives::kNames[f.archive], e.size,
                        e.compressed ? 'z' : ' ', e.name.c_str());
            ++n;
        }
        std::printf("(%zu file(s); data %zu + data_us %zu entries; size = stored bytes, z = zlib)\n", n,
                    src.archives[0].entries().size(), src.archives[1].entries().size());
        return 0;
    }

    if (cmd == "--install") {
        std::printf("install: %s\nout:     %s\n", u8(game).c_str(), u8(out).c_str());
        int last_pct = -10;
        const gamedata::InstallResult r = gamedata::install(game, out, [&](const gamedata::Progress& p) {
            const int pct = p.bytes_total ? int(p.bytes_done * 100 / p.bytes_total) : 0;
            if (pct / 10 != last_pct / 10 || p.files_done == p.files_total) {
                std::printf("  %3d%%  %llu / %llu files, %.0f MB written\n", pct,
                            (unsigned long long)p.files_done, (unsigned long long)p.files_total,
                            double(p.bytes_written) / 1e6);
                last_pct = pct;
            }
        });
        for (const std::string& f : r.failures) std::printf("  fail: %s\n", f.c_str());
        if (!r.ok) {
            std::fprintf(stderr, "install failed: %s\n", r.error.c_str());
            return 1;
        }
        std::printf("done: %llu files, %.1f MB in %.1f s\n", (unsigned long long)r.files,
                    double(r.bytes) / 1e6, r.seconds);
        return 0;
    }
    return usage();
}
