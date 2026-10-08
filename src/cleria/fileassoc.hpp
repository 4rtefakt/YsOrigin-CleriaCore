// The `.cleriamod` file association (build/shell_re/MODFILE.md §3): per-user, under
// HKEY_CURRENT_USER\Software\Classes (no administrator rights), so a double-click on a mod file runs
// "<cleria-view.exe>" "<file>", which installs it (modinstall.hpp).
//
//   Software\Classes\.cleriamod                         (default) = CleriaCore.Mod
//   Software\Classes\.cleriamod\OpenWithProgids         CleriaCore.Mod = ""
//   Software\Classes\CleriaCore.Mod                     (default) = CleriaCore mod
//   Software\Classes\CleriaCore.Mod\DefaultIcon         (default) = "<exe>",0
//   Software\Classes\CleriaCore.Mod\shell\open\command  (default) = "<exe>" "%1"
//
// Every registry access goes through `Registry`, so the logic is tested against a fake; the real HKCU
// (user_registry) is never opened by tests, and refuses in a CLERIA_DRIVE run.
#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cleria::fileassoc {

inline constexpr const char* kExt = ".cleriamod";
inline constexpr const char* kProgId = "CleriaCore.Mod";
inline constexpr const char* kTypeName = "CleriaCore mod";
inline constexpr const char* kClasses = "Software\\Classes";

// One registry value under HKCU; `name` "" is the key's default value. Strings are UTF-8.
struct Entry {
    std::string key, name, value;
};
// The values that make .cleriamod open with `exe` (its full path, UTF-8).
std::vector<Entry> entries(const std::string& exe);
std::string open_command(const std::string& exe);   // "\"<exe>\" \"%1\""

// HKEY_CURRENT_USER, keys relative to it.
struct Registry {
    virtual ~Registry() = default;
    virtual std::optional<std::string> get(const std::string& key, const std::string& name) = 0;
    virtual bool set(const std::string& key, const std::string& name, const std::string& value) = 0;   // creates the key
    virtual bool erase_value(const std::string& key, const std::string& name) = 0;
    virtual bool erase_tree(const std::string& key) = 0;           // the key and everything under it
    virtual bool erase_key_if_empty(const std::string& key) = 0;   // no values and no sub-keys left
};

bool is_current(Registry& r, const std::string& exe);   // every entry there, for this exe
bool owned(Registry& r, const std::string& exe);        // CleriaCore.Mod's open command runs this exe

enum class Action { None, Register, Unregister };
// want: (re)register when anything is missing or names another exe; !want: remove ours, only when it
// runs this exe (another CleriaCore install's registration is left alone).
Action decide(Registry& r, const std::string& exe, bool want);
bool do_register(Registry& r, const std::string& exe);
bool do_unregister(Registry& r, const std::string& exe);   // false (and nothing removed) when not owned
// decide + apply; returns the action taken (None when nothing needed doing or it failed: *ok = false).
Action sync(Registry& r, const std::string& exe, bool want, bool* ok = nullptr);

// The real HKEY_CURRENT_USER (Windows); nullptr elsewhere and in a CLERIA_DRIVE run.
std::unique_ptr<Registry> user_registry();
// Tell Explorer the associations changed (SHChangeNotify SHCNE_ASSOCCHANGED).
void notify_shell();

// ---- Linux: the freedesktop equivalent (issue #46) -----------------------------------------------
// Per user, under $XDG_DATA_HOME (default ~/.local/share), no root:
//   mime/packages/cleriacore-mod.xml      the application/x-cleriamod type, glob *.cleriamod
//   applications/cleriacore-mod.desktop   Exec="<exe>" %f, MimeType=application/x-cleriamod;
// then update-mime-database / update-desktop-database / xdg-mime default (notify; best effort). The
// files are plain text, so the logic is tested on any OS against a scratch folder. macOS has no
// per-user equivalent without an app bundle: the view says so and does nothing there.
namespace xdg {
inline constexpr const char* kMime = "application/x-cleriamod";
inline constexpr const char* kDesktopName = "cleriacore-mod.desktop";
inline constexpr const char* kMimeName = "cleriacore-mod.xml";
std::string mime_xml();
std::string desktop_entry(const std::string& exe);   // exe: full path, UTF-8
bool is_current(const std::filesystem::path& data_home, const std::string& exe);
bool owned(const std::filesystem::path& data_home, const std::string& exe);   // our .desktop runs this exe
Action decide(const std::filesystem::path& data_home, const std::string& exe, bool want);
bool do_register(const std::filesystem::path& data_home, const std::string& exe);
bool do_unregister(const std::filesystem::path& data_home, const std::string& exe);
Action sync(const std::filesystem::path& data_home, const std::string& exe, bool want, bool* ok = nullptr);
// $XDG_DATA_HOME, else $HOME/.local/share; empty when neither is set or in a CLERIA_DRIVE run.
std::filesystem::path user_data_home();
// Refresh the desktop's caches (Linux only; never in a CLERIA_DRIVE run).
void notify(const std::filesystem::path& data_home);
}  // namespace xdg

}  // namespace cleria::fileassoc
