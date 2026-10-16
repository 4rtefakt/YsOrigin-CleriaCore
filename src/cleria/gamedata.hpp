// Game data: locate a Ys Origin install, validate it, and unpack its archives into CleriaCore's
// data folder — the native replacement for tools/extract_assets.py (build/shell_re/GAMEDATA.md).
//
// The data folder (default %LOCALAPPDATA%\CleriaCore\data, override $CLERIA_DATA) IS an asset
// root: the same tree as the old assets/ (MAP/, MENU/, ...), data_us layered over data, plus
// one file, `cleria-gamedata.manifest`, written last so its presence means "complete". Music,
// movies and the menu font are NOT copied: the manifest records the install and
// release_dir_for() points the music / movie / font lookups at <install>\release.
//
// The install is never written to. For a launcher UI: find_installs() -> validate(path) ->
// InstallTask::start(path) (worker thread, poll progress(), cancel()) -> status().
#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "cleria/ni_archive.hpp"

namespace cleria::gamedata {

inline constexpr int kFormatVersion = 1;
inline constexpr const char* kManifestName = "cleria-gamedata.manifest";
inline constexpr const char* kMarkerName = ".cleria-installing";   // present while unpacking
inline constexpr std::uint32_t kSteamAppId = 207350;               // steam_appid.txt [DATA]

// ---- the install -------------------------------------------------------------------------
struct Validation {
    bool ok = false;                     // every required file is there and the indexes parse
    std::filesystem::path root;          // the install root (the folder holding yso_win.exe)
    std::vector<std::string> missing;    // required, relative to root ("release\data.ni", ...)
    std::vector<std::string> warnings;   // optional parts that are absent (music, movies, font)
};

// `dir` may be the install root or its release\ folder.
Validation validate(const std::filesystem::path& dir);

// Steam installs: the Steam root from the registry (HKCU\Software\Valve\Steam SteamPath, then
// HKLM ...\Valve\Steam InstallPath), every library in its steamapps\libraryfolders.vdf, and in
// each one appmanifest_207350.acf's installdir (default "Ys Origin"). Linux / macOS: the native
// Steam roots, and a Windows Steam inside a Wine prefix (~/.wine; on macOS every CrossOver and
// Whisky bottle), its libraries' "D:\..." paths read through the prefix's dosdevices. Read-only.
// Returns every candidate folder that exists, validated; ok ones first.
std::vector<Validation> find_installs();

// FNV-1a 64 over both .ni indexes and both .na sizes: identifies the game data version.
std::string source_hash(const std::filesystem::path& install_root);

// The install's archives, merged: release\data then release\data_us, a later archive's entry
// replacing an earlier one of the same (case-insensitive) name. [DATA] all 813 data_us entries
// replace a data entry; data_us adds no new names.
struct GameArchives {
    static constexpr const char* kNames[] = {"data", "data_us"};
    struct File {
        std::size_t archive;   // index into archives / kNames
        const ni::Entry* entry;
    };
    std::vector<ni::Archive> archives;
    std::vector<File> files;   // data's order, overridden in place
    bool open(const std::filesystem::path& install_root, std::string* err = nullptr);
};

// ---- the data folder ---------------------------------------------------------------------
std::filesystem::path data_dir();   // $CLERIA_DATA, else %LOCALAPPDATA%\CleriaCore\data

struct Manifest {
    int format = 0;
    std::filesystem::path install;   // the install root it was unpacked from
    std::uint64_t files = 0;         // files written (the merged entry count)
    std::uint64_t bytes = 0;         // unpacked bytes written
    std::string source_hash;         // source_hash(install) at unpack time
    std::string created;             // UTC, ISO 8601
};
std::optional<Manifest> read_manifest(const std::filesystem::path& data);

enum class State { None, Installing, Ready };
struct Status {
    State state = State::None;
    std::filesystem::path data;      // the data folder asked about
    std::filesystem::path install;   // from the manifest (Ready)
    std::filesystem::path release;   // <install>\release if it still exists, else empty
    bool partial = false;            // None, but an interrupted unpack left files behind
    bool stale = false;              // Ready, but the install's source_hash has changed since
    std::optional<Manifest> manifest;
};
Status status(const std::filesystem::path& data);
inline Status status() { return gamedata::status(data_dir()); }   // qualified: ADL would find fs::status

// The data folder when it is Ready, else empty — the asset root to use when none is given.
std::filesystem::path ready_data_dir();

// <install>\release for an asset root that carries a manifest whose install still exists,
// else empty. The music (\music), movie and font (yso_ins04.dat) lookups try it first.
std::filesystem::path release_dir_for(const std::filesystem::path& asset_root);

// ---- languages (build/shell_re/CONFIG_PARITY.md §7) ---------------------------------------
// [game] Language and the overlay archive retail mounts over release\data for it [EXPORT FUN_005d68d0,
// strings 0x6b8978..0x6b8998]. Japanese (0) mounts none: the base archive alone [not offered: its text
// needs a Japanese font path]. The data folder is data + data_us merged, so English needs nothing more;
// another language's archive is unpacked ON ITS OWN into <asset root>/lang/<archive>/ (the same relative
// names) and laid over the asset root by vfs::set_language. $CLERIA_LANG_DIR replaces <asset root>/lang
// for the lookup (like CLERIA_MUSIC_DIR: language data kept elsewhere).
struct Language { int id; const char* name; const char* archive; };
inline constexpr Language kLanguages[] = {
    {1, "English", "data_us"}, {2, "Spanish", "data_sp"}, {3, "French", "data_fr"},
    {4, "German", "data_de"},  {5, "Italian", "data_it"}};
const Language* language(int id);   // null for an id not in kLanguages
std::filesystem::path language_dir(const std::filesystem::path& asset_root, int id);   // empty for English / unknown
// The languages `asset_root` can run: English, then every other whose language_dir exists.
std::vector<int> languages(const std::filesystem::path& asset_root);
// Unpacks `ni` (an index; its .na beside it) on its own into `out_dir`. Returns the files written, -1 on error.
long long unpack_archive(const std::filesystem::path& ni, const std::filesystem::path& out_dir, std::string* err = nullptr);

// The config tool's "European Localization Credits" (button 603): the two message-box texts, read from the
// install's yso_win.exe (UTF-16 strings; found by their first words and the second box's title, so no address
// is assumed) and returned as UTF-8 lines. Empty when the exe or the strings are not there.
std::vector<std::string> localization_credits(const std::filesystem::path& install_root);

// ---- unpacking ---------------------------------------------------------------------------
struct Progress {
    std::uint64_t files_done = 0, files_total = 0;
    std::uint64_t bytes_done = 0, bytes_total = 0;   // archive bytes consumed (stable total)
    std::uint64_t bytes_written = 0;                 // unpacked bytes
};
using ProgressFn = std::function<void(const Progress&)>;

struct InstallResult {
    bool ok = false;
    bool cancelled = false;
    std::string error;                  // the first / fatal error
    std::vector<std::string> failures;  // per-file failures (capped)
    std::uint64_t files = 0, bytes = 0;
    double seconds = 0;
};

// Unpacks data.ni/.na then data_us.ni/.na over it into `out`, then every other language archive the install
// has (release\data_sp / _fr / _de / _it) into out/lang/<archive>/ (a failure there is a warning in
// `failures`, not an error). `progress` is called on the
// calling thread about every 50 ms and once at the end; `cancel` is polled per file. `threads`
// 0 = min(hardware threads, 8). Refuses a non-empty `out` that is not a CleriaCore data folder
// (no manifest, no marker), and deletes nothing: it only overwrites the files it writes. The
// manifest is removed at the start and written only on full success.
InstallResult install(const std::filesystem::path& game_dir, const std::filesystem::path& out,
                      const ProgressFn& progress = {}, const std::atomic<bool>* cancel = nullptr,
                      unsigned threads = 0);
inline InstallResult install(const std::filesystem::path& game_dir, const ProgressFn& progress = {},
                             const std::atomic<bool>* cancel = nullptr) {
    return install(game_dir, data_dir(), progress, cancel);
}

// install() on a worker thread, for a UI that polls once a frame.
class InstallTask {
  public:
    InstallTask() = default;
    ~InstallTask();   // cancels and joins
    InstallTask(const InstallTask&) = delete;
    InstallTask& operator=(const InstallTask&) = delete;

    bool start(const std::filesystem::path& game_dir, const std::filesystem::path& out = data_dir());
    void cancel() { cancel_ = true; }
    bool running() const { return running_; }
    Progress progress() const;
    InstallResult result() const;   // meaningful once !running()
    void wait();

  private:
    std::thread th_;
    std::atomic<bool> cancel_{false}, running_{false};
    mutable std::mutex mu_;
    Progress prog_;
    InstallResult res_;
};

}  // namespace cleria::gamedata
