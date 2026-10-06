// The virtual file system: ONE resolver between every asset load and the disk, so mods can lay
// files over the game's own (build/shell_re/MODS.md §3).
//
// The game reads an unpacked asset tree (the data folder / assets/, src/cleria/gamedata) plus a few
// loose install folders (music, movies). Each of those roots is MOUNTED under a virtual prefix:
//     ""       the asset root          MAP/S_10/COMMON/H/10YUKA04.DDS, IMAGE/STATUS_512.DDS, ...
//     "music"  the BGM folder          music/YSO_001.ogg, music/bgmdef.txt
//     "movie"  the movie folder        movie/yso_ins01.dat
// A LAYER is a folder laid over all mounts (a mod's files/ folder); its relative paths use the
// same virtual names. resolve(p) takes the real path a loader built (asset_root / "MAP" / ...),
// maps it back to its virtual name through the mounts and returns the file of the highest layer
// that has it, else `p` itself. Names compare case-insensitively with '/' and '\' equivalent, like
// the archives (ni::lookup_key).
//
// Lifetime: mount() and set_layers() run once at startup, before the first load; afterwards the
// tables are read-only, so resolve() needs no lock. With no layers, resolve() returns `p` at once.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cleria::vfs {

// A folder laid over the mounts; later layers win.
struct Layer {
    std::string id;                   // the owner (a mod id), for reports
    std::filesystem::path root;       // its files, by virtual name
};

// Where a virtual name currently resolves to.
struct Entry {
    std::filesystem::path file;       // the winning layer's file
    int layer = -1;                   // index into layers()
    std::vector<int> also;            // lower layers that also provide this name (overridden)
};

// "MAP\\S_10\\x.dds" -> "map/s_10/x.dds": lowercase ASCII, '/' separators, no leading "./" or "/".
std::string key(std::string_view virtual_name);

// Mount `dir` under `prefix` ("" = the asset root). Re-mounting a prefix replaces it.
void mount(const std::string& prefix, const std::filesystem::path& dir);

// Replace the layers (index 0 = lowest priority) and rebuild the name table by walking each
// layer's root. Returns the number of distinct names provided.
std::size_t set_layers(std::vector<Layer> layers);

// Back to no mounts and no layers (tests).
void reset();

// The file to open for `p`: a layer's file when `p` lies under a mount and a layer provides its
// virtual name, else `p` unchanged. Never touches the disk.
std::filesystem::path resolve(const std::filesystem::path& p);

// The virtual name of `p` if it lies under a mount ("" prefix => "MAP/..."), else empty.
std::string virtual_name(const std::filesystem::path& p);

// The real path of a virtual name: the winning layer's file, else <mount dir>/<rest> (which may not
// exist), else empty when no mount matches.
std::filesystem::path locate(std::string_view virtual_name);

// resolve() + read the whole file (empty on failure).
std::vector<std::uint8_t> read(const std::filesystem::path& p);

// resolve() + exists: also true for a NEW file a layer adds under a mount.
bool exists(const std::filesystem::path& p);

const std::vector<Layer>& layers();
const std::unordered_map<std::string, Entry>& table();   // key(virtual name) -> entry

}  // namespace cleria::vfs
