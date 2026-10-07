// Mods: discovery, the mod.ini manifest, the load order and the dependency / conflict checks, and
// the session's active set (build/shell_re/MODS.md). The files a mod overrides go through the
// virtual file system (src/cleria/vfs.hpp); this module decides WHICH mods feed it and in what order.
//
// A mod is a folder (or a .zip of one) in the mods folder:
//     <mods>/<anything>/mod.ini       the manifest ([mod] Id, Name, Version, Author, ...)
//     <mods>/<anything>/files/...     files by their game path: files/MAP/S_10/COMMON/H/10YUKA04.DDS
// A zip is extracted once into <mods>/.cache/<zip stem>/ (re-extracted when the zip changes); its
// mod.ini may sit at the zip root or inside one top-level folder.
//
// Load order: a list; a mod LOWER in the list loads later and wins a file both provide. The user's
// order and choices persist in cleria.ini [cleria.mods] (Order / Active); a newly found mod is
// appended (by its Priority, then id) and starts DISABLED until the player turns it on.
#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "cleria/vfs.hpp"

namespace cleria::mods {

inline constexpr int kApiVersion = 3;                  // the manifest format / content API level (2: content/, CONTENT.md; 3: Script=, LUA_API.md)
inline constexpr const char* kManifestName = "mod.ini";
inline constexpr const char* kFilesDir = "files";      // the overlay root inside a mod
inline constexpr const char* kCacheDir = ".cache";     // extracted zips, inside the mods folder
inline constexpr const char* kModFileExt = ".cleriamod"; // a zip mod under CleriaCore's own extension (MODFILE.md)

// A zip entry name that stays inside the extraction folder: relative, no "..", no drive.
bool safe_zip_entry(std::string_view name);

// "id", "id>=1.2", "id = 2", "id<3"
struct Dep {
    std::string id;
    std::string op;        // "", ">=", ">", "=", "<=", "<"
    std::string version;
};

struct Manifest {
    std::string id;           // required: [a-z0-9._-], unique ("author.modname" recommended)
    std::string name;         // display name (default: the id)
    std::string version = "0";
    std::string author, description, website;
    int priority = 0;         // the default slot for a newly found mod: lower loads first
    int api = kApiVersion;    // ApiVersion: the CleriaCore mod API the mod was written for
    std::string script;       // Script: the Lua entry file (ApiVersion >= 3, build/shell_re/LUA_API.md); "" = none
    std::vector<Dep> requires_, conflicts;
};

// Parses mod.ini's text. Problems (no [mod] Id, an invalid id, a bad dependency) go to `errors`;
// the manifest is usable when `errors` stayed empty.
Manifest parse_manifest(std::string_view text, std::vector<std::string>& errors);
bool valid_id(std::string_view id);
Dep parse_dep(std::string_view s);   // id empty when unparsable
// Dotted numeric comparison ("1.10" > "1.9"; a non-numeric part compares as text). <0, 0, >0.
int compare_versions(std::string_view a, std::string_view b);
bool satisfies(std::string_view version, const Dep& d);

enum class State { Disabled, Active, Error };

struct Mod {
    Manifest m;
    std::filesystem::path source;   // the folder or the .zip in the mods folder
    std::filesystem::path root;     // the folder holding mod.ini (a zip's extraction)
    bool zip = false;
    bool enabled = false;           // the player's choice
    State state = State::Disabled;
    std::vector<std::string> problems; // its own: the manifest / zip could not be read (discover)
    std::vector<std::string> errors;   // why it cannot load: problems + the checks (arrange)
    std::vector<std::string> notes;    // warnings: shared files, order hints
    std::size_t files = 0;             // files under files/
    bool stopped = false;              // its Lua script failed this session (LUA_API.md): still loaded, script disabled
    std::filesystem::path files_dir() const { return root / kFilesDir; }
    const std::string& label() const { return m.name.empty() ? m.id : m.name; }
};

// What cleria.ini [cleria.mods] holds.
struct Prefs {
    bool use = true;                   // UseMods: 0 = run the game unmodded
    std::vector<std::string> order;    // Order: every known id, load order
    std::vector<std::string> active;   // Active: the enabled ids
};
std::vector<std::string> split_list(std::string_view s);   // "a, b,,c" -> {a, b, c}
std::string join_list(const std::vector<std::string>& v);  // {a, b} -> "a,b"

// $CLERIA_MODS_DIR, else <the settings file's folder>/mods.
std::filesystem::path default_dir(const std::filesystem::path& settings_ini);

// Every mod in `dir`: each sub-folder with a mod.ini, each *.zip / *.cleriamod (extracted on demand). Mods that
// fail to read come back in State::Error with the reason. Sorted by source name.
std::vector<Mod> discover(const std::filesystem::path& dir);

// Apply the player's order and choices, then the checks (repeated until stable): a duplicate id,
// a newer ApiVersion, a missing / disabled / wrong-version dependency, an enabled declared conflict
// each put the mod in State::Error. Never reorders what the player ordered.
void arrange(std::vector<Mod>& mods, const Prefs& prefs);

// The prefs that reproduce `mods`' current order and choices.
Prefs prefs_of(const std::vector<Mod>& mods, bool use);

// The vfs layers of the Active mods, lowest priority first.
std::vector<vfs::Layer> layers(const std::vector<Mod>& mods);

// After vfs::set_layers(layers(mods)): each Active mod's file count and the files it shares with
// another Active mod ("3 files overridden by X", "overrides 2 files of Y").
void note_overrides(std::vector<Mod>& mods);

// ---- the session -------------------------------------------------------------------------------
// One call at startup: discover, arrange, feed the vfs, record the active set.
std::vector<Mod> load(const std::filesystem::path& dir, const Prefs& prefs);

struct Info {
    std::string id, name, version;
};
// The mods this process loaded at startup, in load order (fixed for the session; a change in the
// Mods tab applies on restart). Empty = unmodded. For speedrun mode, logs and save stamps.
const std::vector<Info>& active();
std::string active_summary();   // "none", or "Checker Floor 1.0.0, Other Mod 2.1"

}  // namespace cleria::mods
