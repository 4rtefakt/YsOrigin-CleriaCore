// Mod files: installing a `.cleriamod` (or a `.zip` mod) into the mods folder (build/shell_re/MODFILE.md).
//
// A `.cleriamod` is exactly a zip mod (mod.ini at its root or in one top-level folder, mods.hpp) under
// CleriaCore's own extension, so Windows can open it with CleriaCore (src/cleria/fileassoc.hpp). Installing
// one validates it completely BEFORE anything is written, copies it to <mods>/<id>.cleriamod (one copy per
// id: an older .zip / .cleriamod of the same id is deleted, an older loose folder moved to <mods>/.replaced/),
// and turns it on (Order + Active, UseMods). Not to be confused with `yso_NN.cleria`, the save sidecar.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "cleria/mods.hpp"

namespace cleria::mods {

inline constexpr const char* kReplacedDir = ".replaced";            // older loose folders, moved aside
inline constexpr const char* kEnableQueue = ".cleriamod-enable";    // ids to turn on at the next start

// What a mod file holds, read in memory without unpacking it.
struct Package {
    Manifest m;
    std::string error;   // why it cannot be installed; "" = installable
    bool ok() const { return error.empty(); }
};
// Every check the loader would make, up front: a readable zip whose every entry unpacks (CRC), only safe
// entry names (mods.hpp safe_zip_entry), a mod.ini at the root or in exactly one top-level folder, a
// manifest without errors (a valid Id), an ApiVersion this build supports.
Package inspect_package(const std::filesystem::path& file);

struct InstallResult {
    bool ok = false;
    std::string file;                        // the file's name, for messages
    std::string error;                       // when !ok: nothing was written
    std::string id, name, version;
    std::string old_version;                 // the version it replaced; "" = a fresh install
    bool replaced = false;                   // an older copy of the same id was there
    std::filesystem::path dest;              // <mods>/<id>.cleriamod
    std::vector<std::filesystem::path> removed;   // the older copies taken out
    std::vector<std::string> warnings;       // an older copy that could not be removed, ...
    // "Installed Archipelago 0.1.0 - enabled", "Updated Archipelago 0.1.0 -> 0.2.0 - enabled",
    // "Reinstalled ...", or "Could not install x.cleriamod: <why>".
    std::string message() const;
};

// Install `file` into `mods_dir` and turn it on in `prefs` (its id appended to the order when new, added to
// the active ids, use = true). An invalid file returns !ok and leaves the folder and `prefs` untouched.
// Never touches <mods>/.cache (a replaced package re-extracts by its changed size / time stamp).
InstallResult install(const std::filesystem::path& file, const std::filesystem::path& mods_dir, Prefs& prefs);

// A mod installed while another CleriaCore runs on the same mods folder: that process may rewrite
// cleria.ini from its memory before it restarts, so the id is also queued in <mods>/.cleriamod-enable and
// the next start turns it on (take_enable_queue: reads and deletes the queue).
void queue_enable(const std::filesystem::path& mods_dir, const std::string& id);
std::vector<std::string> take_enable_queue(const std::filesystem::path& mods_dir);
// Turn `ids` on in `prefs` (appended to the order when new); true when anything changed.
bool enable_in(Prefs& prefs, const std::vector<std::string>& ids);

// The single-instance key: one running CleriaCore per mods folder ("CleriaCore.<16 hex digits>", from the
// folder's normalised path), so test runs on scratch folders never see the player's game and vice versa.
std::string instance_key(const std::filesystem::path& mods_dir);

}  // namespace cleria::mods
