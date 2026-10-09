// The CleriaCore save sidecar: yso_NN.cleria beside each yso_NN.bin (build/shell_re/MODS.md §7.3.2,
// build/shell_re/CONTENT.md §2). NOT retail: the retail record (save.hpp) is a fixed 0x1FC8-byte
// image of the game-state block with no room for anything CleriaCore adds, and it must stay
// byte-compatible with the real exe. Everything CleriaCore-only that belongs to a save lives here:
//   - named CleriaCore flags (a mod's "collected once" pickups, a rule's Once state, ...),
//   - the counts of NEW items (ids 0x100..0x1FF, content.hpp), which have no g_flags cell,
//   - a Lua mod's per-save storage ([mod.<id>], LUA_API.md §6; the Archipelago mod keeps its state there),
//   - a stamp of the content mods that were active when it was written.
// The format is the INI dialect of cleria.ini (CRLF, case-insensitive names):
//     [cleria]  Version=1  Mods="sample.chest_swap 1.0.0"
//     [flags]   chest_swap.p1=1
//     [items]   0x100=1
//     [mod.<id>] key="json text"
// An old file's [ap] block (the removed built-in Archipelago client's) is read and ignored.
// [H] An EMPTY sidecar is never written, and writing one removes a stale file: with no content
// mod a save writes exactly the retail file and nothing else.
#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cleria {

struct Sidecar {
    static constexpr int kVersion = 1;
    std::vector<std::string> mods;                 // "id version" of the content mods active at the save
    std::map<std::string, std::int32_t> flags;     // CleriaCore flags, by lower-case name
    std::map<int, std::int32_t> items;             // new-item id -> count (absent = never had)
    // [mod.<id>]: a Lua mod's per-save storage (cleria.save, build/shell_re/LUA_API.md): key as written -> text
    std::map<std::string, std::map<std::string, std::string>> mod;

    bool empty() const {
        for (const auto& [id, kv] : mod) if (!kv.empty()) return false;
        return flags.empty() && items.empty();
    }
    void clear() { mods.clear(); flags.clear(); items.clear(); mod.clear(); }

    std::int32_t flag(std::string_view name) const;          // 0 when unset
    void set_flag(std::string_view name, std::int32_t v);    // 0 erases (absent == 0)
    std::int32_t item(int id) const;                         // -1 = never had (like g_flags 0..127)
    void add_item(int id, std::int32_t n);                   // count = max(count, 0) + n, at most 99

    std::string serialize() const;                           // the INI text (CRLF)
    // nullopt on a file that is not a sidecar (no [cleria] Version) or of a newer version.
    static std::optional<Sidecar> parse(std::string_view text);
};

// yso_01.bin -> yso_01.cleria (same folder, same stem).
std::filesystem::path sidecar_path(const std::filesystem::path& save_file);
// Write `s` beside `save_file`; an empty sidecar removes the file instead. False on an I/O error.
bool write_sidecar(const std::filesystem::path& save_file, const Sidecar& s);
// The sidecar of `save_file`; empty when there is none (a retail save, or a save made unmodded).
Sidecar read_sidecar(const std::filesystem::path& save_file);
// Remove the sidecar of a deleted save (no error when absent).
void remove_sidecar(const std::filesystem::path& save_file);

// Save profiles (CleriaCore's, not retail's; build/shell_re/LUA_API.md §6.1): a run's saves in
// <save dir>/profiles/<name>/. The folder name of a profile: every byte outside [A-Za-z0-9._-] becomes '_',
// at most 64 characters, a trailing '.' becomes '_' (Windows drops it); "" = refused (an empty name, or dots
// only: "." / ".."). The result never holds a path separator.
std::string save_profile_name(std::string_view raw);
// The folder of profile `name` (already a save_profile_name) under `save_dir`; `save_dir` itself for "".
std::filesystem::path save_profile_dir(const std::filesystem::path& save_dir, const std::string& name);

}  // namespace cleria
