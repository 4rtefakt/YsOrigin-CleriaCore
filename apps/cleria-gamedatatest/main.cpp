// cleria-gamedatatest — the NI/NA reader and the game-data module (src/cleria/ni_archive,
// src/cleria/gamedata; build/shell_re/GAMEDATA.md).
//
//   cleria-gamedatatest [<asset root>]     (default: the ready data folder, else assets/)
//
// Always: the cipher against known index bytes, the CP932 / name rules, the manifest + status
// logic and the install guard on scratch folders. With a Ys Origin install (CLERIA_GAME_DIR, else
// the first one find_installs() reports): parse both real indexes, check their invariants, and
// read known entries, comparing each with the same file under <asset root> when it is there.
// Without the install those checks are skipped, not failed.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "cleria/gamedata.hpp"
#include "cleria/ni_archive.hpp"

using namespace cleria;
namespace fs = std::filesystem;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[gamedata] %-78s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

static std::vector<std::uint8_t> file_bytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static std::uint32_t rd32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 | std::uint32_t(p[3]) << 24;
}

int main(int argc, char** argv) {
    fs::path assets = argc > 1 ? fs::path(argv[1]) : gamedata::ready_data_dir();
    if (assets.empty()) assets = "assets";

    // ---- the cipher ------------------------------------------------------------------------
    {
        // The first 8 TOC bytes of the shipped indexes [DATA]: data.ni's entry 0 is {hash 9,
        // size 0x38cd6}, data_us.ni's {hash 0x3d, size 0xd50} — hash 9 / 61 are also the first
        // keys of data.nu / data_us.nu.
        std::uint8_t a[8] = {0x68, 0x14, 0x53, 0xfa, 0x7c, 0x84, 0x7f, 0x54};
        std::uint8_t b[8] = {0x9c, 0x14, 0x53, 0xfa, 0xf6, 0x05, 0x7c, 0x54};
        ni::decrypt(a, 8);
        ni::decrypt(b, 8);
        check(rd32(a) == 9 && rd32(a + 4) == 0x38cd6, "decrypt: data.ni TOC[0] = {hash 9, size 0x38cd6}");
        check(rd32(b) == 0x3d && rd32(b + 4) == 0xd50, "decrypt: data_us.ni TOC[0] = {hash 0x3d, size 0xd50}");
        // The key stream itself: zeros decrypt to -(k >> 16) per byte.
        std::uint8_t z[8] = {};
        ni::decrypt(z, 8);
        const std::uint8_t want[8] = {0xa1, 0xec, 0xad, 0x06, 0x5a, 0x08, 0x84, 0xac};
        check(std::memcmp(z, want, 8) == 0, "decrypt: the key stream from k0 = 0x7C53F961, k *= 0x3D09");
    }

    // ---- names -----------------------------------------------------------------------------
    {
        const std::string utf8 = "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88\\\xE3\x83\x95\xE3\x82\xA1\xE3\x82\xA4\xE3\x83\xAB.DDS";
        const std::string sjis = ni::utf8_to_cp932(utf8);
        const std::string want = "\x83\x65\x83\x58\x83\x67\x5c\x83\x74\x83\x40\x83\x43\x83\x8b.DDS";
        check(sjis == want, "CP932: \"tesuto\\fairu.DDS\" encodes to the Shift-JIS bytes");
        check(ni::cp932_to_utf8(sjis) == utf8, "CP932: and decodes back to the same UTF-8");
        // "so" (0x83 0x5C) has a '\' trail byte: it must not be taken for a separator.
        check(ni::clean_name(ni::cp932_to_utf8("\x83\x5c\\A.DDS.Z")) == "\xE3\x82\xBD/A.DDS",
              "clean_name: a 0x5C trail byte is not a path separator");
        check(ni::clean_name("MAP\\MAPOBJ\\Z_TREE\\H\\?_??01_?.DDS.Z") == "MAP/MAPOBJ/Z_TREE/H/____01__.DDS",
              "clean_name: '\\'->'/', .Z dropped, ? -> _ (extract_assets.py's rule)");
        check(ni::clean_name("A\\B<>:\"|?*.WAV") == "A/B_______.WAV" && ni::clean_name("X.z") == "X",
              "clean_name: every one of <>:\"|?* is replaced; a lowercase .z is dropped too");
        check(ni::lookup_key("MAP\\S_10\\S_1000.SOB.Z") == "map/s_10/s_1000.sob", "lookup_key: case/separator/.z folded");
    }

    // ---- manifest / status / the install guard (scratch folders, no game needed) -----------
    {
        const fs::path tmp = fs::temp_directory_path() / "cleria-gamedatatest";
        std::error_code ec;
        fs::remove_all(tmp, ec);   // our own scratch folder only
        fs::create_directories(tmp / "empty", ec);
        check(gamedata::status(tmp / "empty").state == gamedata::State::None &&
                  !gamedata::status(tmp / "empty").partial,
              "status: an empty folder is None (not partial)");
        fs::create_directories(tmp / "ready", ec);
        {
            std::ofstream m(tmp / "ready" / gamedata::kManifestName);
            m << "# test\nformat=1\ninstall=" << (tmp / "nowhere").string()
              << "\nfiles=15945\nbytes=123\nsource_hash=00\ncreated=x\n";
        }
        const gamedata::Status s = gamedata::status(tmp / "ready");
        check(s.state == gamedata::State::Ready && s.manifest && s.manifest->files == 15945 && s.release.empty(),
              "status: a manifest makes Ready; a vanished install leaves release empty");
        check(gamedata::release_dir_for(tmp / "ready").empty(), "release_dir_for: empty when the install is gone");
        { std::ofstream(tmp / "ready" / gamedata::kMarkerName) << "x"; }
        check(gamedata::status(tmp / "ready").state == gamedata::State::None &&
                  gamedata::status(tmp / "ready").partial,
              "status: manifest + the unpack marker = an interrupted unpack (None, partial)");
        fs::create_directories(tmp / "foreign", ec);
        { std::ofstream(tmp / "foreign" / "mine.txt") << "user data"; }
        const gamedata::Validation nv = gamedata::validate(tmp / "foreign");
        check(!nv.ok && !nv.missing.empty(), "validate: a non-install is rejected with a missing list");
        fs::remove_all(tmp, ec);
    }

    // ---- the real install ------------------------------------------------------------------
    fs::path game;
    if (const char* e = std::getenv("CLERIA_GAME_DIR")) game = e;
    else
        for (const gamedata::Validation& v : gamedata::find_installs())
            if (v.ok) { game = v.root; break; }
    const gamedata::Validation v = game.empty() ? gamedata::Validation{} : gamedata::validate(game);
    if (!v.ok) {
        std::printf("[gamedata] (no Ys Origin install found: real-archive checks skipped)\n");
    } else {
        std::printf("[gamedata] install: %s\n[gamedata] assets:  %s\n", v.root.string().c_str(),
                    assets.string().c_str());
        gamedata::GameArchives src;
        std::string err;
        check(src.open(v.root, &err), "GameArchives::open: both real indexes decrypt and parse");
        if (src.archives.size() == 2) {
            const ni::Archive& base = src.archives[0];
            const ni::Archive& us = src.archives[1];
            check(base.entries().size() == 15945 && us.entries().size() == 813,
                  "the shipped indexes: data 15945 entries, data_us 813 [DATA]");
            check(src.files.size() == base.entries().size(), "data_us adds no new names (all 813 replace)");
            std::size_t from_us = 0;
            for (const auto& f : src.files) from_us += f.archive == 1;
            check(from_us == 813, "merge: 813 files come from data_us");
            {   // a language overlay on its own (CONFIG_PARITY.md §7): data_us stands in for data_sp, which this
                // install does not have
                const fs::path lt = fs::temp_directory_path() / "cleria-gamedatatest-lang";
                std::error_code lec;
                fs::remove_all(lt, lec);
                check(gamedata::languages(lt) == std::vector<int>{1}, "languages: English alone without a lang folder");
                const long long n = gamedata::unpack_archive(v.root / "release" / "data_us.ni", gamedata::language_dir(lt, 2), &err);
                check(n == 813 && fs::exists(lt / "lang" / "data_sp" / "MISC" / "INVINFO.DAT", lec),
                      "unpack_archive: the overlay's 813 files under lang/data_sp");
                check(gamedata::languages(lt) == std::vector<int>({1, 2}) && gamedata::language(2)->name == std::string("Spanish") &&
                          gamedata::language_dir(lt, 1).empty() && !gamedata::language(0),
                      "languages: English + the unpacked one; English and Japanese have no folder");
                fs::remove_all(lt, lec);
                const std::vector<std::string> cr = gamedata::localization_credits(v.root);
                check(cr.size() > 30 && cr[0] == "Ys Origin" && cr[1] == "PC Version - European Language Localization" &&
                          std::find(cr.begin(), cr.end(), "Translators and Editors") != cr.end(),
                      "localization_credits: both message boxes read from yso_win.exe");
            }
            for (const ni::Archive* a : {&base, &us}) {
                std::uint64_t sum = 0;
                bool sorted = true;
                for (std::size_t i = 0; i < a->entries().size(); ++i) {
                    sum += a->entries()[i].size;
                    if (i && a->entries()[i].hash < a->entries()[i - 1].hash) sorted = false;
                }
                check(sum == a->na_size() && sorted,
                      a == &base ? "data: TOC sizes tile data.na exactly; hash ascending"
                                 : "data_us: TOC sizes tile data_us.na exactly; hash ascending");
            }
            // Known entries vs the files under <assets> (the Python-extracted tree or ours).
            struct Known { const char* name; int archive; };
            const Known known[] = {
                {"MISC/INVINFO.DAT", 1},       {"MAP/SCENELIST.SL", 1},
                {"ANI/C_010.ASO", 0},          {"MAP/S_00/S_0003/EVT_0001_HUGO.XSO", 1},
                {"MENU/TOPIC/B100V.DDS", 1},   {"WAVE/EFFECT/EFFECTDEF.TXT", 0},
            };
            for (const Known& k : known) {
                const ni::Entry* e = src.archives[std::size_t(k.archive)].find(k.name);
                std::vector<std::uint8_t> got;
                const bool ok = e && src.archives[std::size_t(k.archive)].read(*e, got, &err);
                std::string what = std::string("read ") + gamedata::GameArchives::kNames[k.archive] + ":" + k.name;
                check(ok && !got.empty(), what.c_str());
                const fs::path ref = assets / k.name;
                std::error_code ec;
                if (ok && fs::is_regular_file(ref, ec)) {
                    what = std::string("  == ") + (assets / k.name).string();
                    check(got == file_bytes(ref), what.c_str());
                }
            }
            // A raw (stored) entry and a lookup by raw path, any case.
            const ni::Entry* wav = nullptr;
            for (const ni::Entry& e : base.entries())
                if (!e.compressed && e.size > 0 && e.name.size() > 4 && e.name.substr(e.name.size() - 4) == ".WAV") { wav = &e; break; }
            std::vector<std::uint8_t> wb;
            check(wav && base.read(*wav, wb) && wb.size() == wav->size && std::memcmp(wb.data(), "RIFF", 4) == 0,
                  "a stored .WAV entry reads raw (RIFF header)");
            check(base.find("map\\s_00\\s_0003\\EVT_0001_HUGO.xso.z") == base.find("MAP/S_00/S_0003/EVT_0001_HUGO.XSO"),
                  "find: raw path, any case, .z optional");
            // The zero-size .z entries (pos 0, size 0) read as empty files.
            std::size_t zero = 0;
            bool zero_ok = true;
            for (const ni::Entry& e : base.entries())
                if (e.size == 0) {
                    ++zero;
                    std::vector<std::uint8_t> zb{1};
                    zero_ok = zero_ok && base.read(e, zb) && zb.empty();
                }
            std::printf("[gamedata] %zu zero-size entries in data.ni\n", zero);
            check(zero > 0 && zero_ok, "zero-size entries read as empty files");
            // The install guard: a non-empty folder that is not ours is refused, untouched.
            const fs::path foreign = fs::temp_directory_path() / "cleria-gamedatatest-foreign";
            std::error_code ec;
            fs::create_directories(foreign, ec);
            { std::ofstream(foreign / "mine.txt") << "user data"; }
            const gamedata::InstallResult r = gamedata::install(v.root, foreign);
            check(!r.ok && r.files == 0 && !fs::exists(foreign / "MAP", ec) && !fs::exists(foreign / gamedata::kMarkerName, ec),
                  "install: refuses a non-empty folder that is not a data folder");
            check(!gamedata::install(v.root, v.root / "release").ok, "install: refuses the game's own folder");
            fs::remove(foreign / "mine.txt", ec);
            fs::remove(foreign, ec);

            // InstallTask on a worker thread: progress moves, cancel stops it, the folder is
            // left partial (marker, no manifest) and status() says so.
            const fs::path scratch = fs::temp_directory_path() / "cleria-gamedatatest-cancel";
            fs::remove_all(scratch, ec);   // our own scratch folder only
            {
                gamedata::InstallTask task;
                check(task.start(v.root, scratch), "InstallTask: starts");
                bool installing_seen = false;
                for (int i = 0; i < 2000 && task.running() && task.progress().files_done < 200; ++i) {
                    installing_seen |= gamedata::status(scratch).state == gamedata::State::Installing;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
                const gamedata::Progress p = task.progress();
                task.cancel();
                task.wait();
                const gamedata::InstallResult cr = task.result();
                std::printf("[gamedata] cancelled after %llu / %llu files\n", (unsigned long long)cr.files,
                            (unsigned long long)p.files_total);
                check(p.files_total == 15945 && p.files_done > 0 && p.bytes_total > 0, "InstallTask: progress reported");
                check(installing_seen, "status: Installing while the task runs");
                check(!cr.ok && cr.cancelled && cr.files < p.files_total, "InstallTask: cancel stops the unpack");
                const gamedata::Status s = gamedata::status(scratch);
                check(s.state == gamedata::State::None && s.partial && !fs::exists(scratch / gamedata::kManifestName, ec),
                      "a cancelled unpack is partial: no manifest");
            }
            fs::remove_all(scratch, ec);
        }
    }

    std::printf("[gamedata] %s (%d failure(s))\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
