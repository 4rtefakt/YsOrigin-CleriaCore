// Getting mods from the internet: the Get mods list, a mod's newer release on GitHub, the download
// (build/shell_re/MODS.md §4.1). What the F1 panel's Get mods page and update check stand on; the install itself
// is modinstall.hpp's, as for a file the player picked.
//
// - The list is CleriaOre's catalog/mods.json (4rtefakt/CleriaOre, branch main): the same mods for CleriaOre
//   on Windows and CleriaCore everywhere, and a mod added to it reaches every player without a new build.
//   Each entry: {"id", "name", "author", "description", "repo": "owner/repo"}.
// - A mod's updates come from its GitHub repository: its manifest's Website when that is a github.com
//   repository, else its entry in the list. The latest release counts when its tag ("v1.2.0" or "1.2.0") is a
//   newer version than the installed one and it carries a .cleriamod.
//
// Every call that reaches the network waits for it: call them from a worker thread.
// Tests (no network): CLERIA_CATALOG=<a mods.json file>; CLERIA_RELEASES=<dir> answers a repository's latest
// release from <dir>/<owner>/<repo>.json, whose asset addresses may be local files.
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cleria::mods::get {

inline constexpr const char* kCatalogUrl = "https://raw.githubusercontent.com/4rtefakt/CleriaOre/main/catalog/mods.json";

struct Listed {
    std::string id, name, author, description, repo;   // repo: "owner/repo"
};
// The list's entries with a valid id and repository (the others are left out).
std::vector<Listed> parse_catalog(std::string_view text);

// "https://github.com/owner/repo" (a trailing "/" or ".git" allowed) -> "owner/repo"; "" otherwise.
std::string github_repo(std::string_view website);

struct Release {
    std::string version, url, file;   // the tag without its "v", the asset's address and its file name
};
// A releases/latest answer: the release when its tag is newer than `installed` and it has an asset ending in
// `ext`; nullopt otherwise.
std::optional<Release> pick_release(std::string_view json_text, std::string_view installed, std::string_view ext);

// ---- the network -----------------------------------------------------------------------------------------
// False when this build cannot download (off Windows, built without CLERIA_CURL).
bool supported();

// The list's text; "" with *error set when it cannot be had.
std::string catalog_text(std::string* error);

// `repo`'s latest release when it is newer than `installed` (pick_release). *failed is set when GitHub could
// not be asked; a repository without a release is not a failure.
std::optional<Release> latest_release(const std::string& repo, const std::string& installed, std::string_view ext,
                                      bool* failed);

// `url` into the file `to` (its folder created). False with *error set on any failure.
bool download(const std::string& url, const std::filesystem::path& to, std::string* error);

// Where downloads wait to be installed: <temp>/CleriaCore/downloads.
std::filesystem::path download_dir();

}  // namespace cleria::mods::get
