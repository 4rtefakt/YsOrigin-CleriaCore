// The .cleriamod file association (fileassoc.hpp, build/shell_re/MODFILE.md §3).
#include "cleria/fileassoc.hpp"

#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>   // SHChangeNotify
#endif

namespace cleria::fileassoc {
namespace {

std::string ext_key() { return std::string(kClasses) + "\\" + kExt; }
std::string progid_key() { return std::string(kClasses) + "\\" + kProgId; }

// Registry strings compare case-insensitively (paths on Windows).
bool same(const std::optional<std::string>& a, const std::string& b) {
    if (!a || a->size() != b.size()) return false;
    for (std::size_t i = 0; i < b.size(); ++i)
        if (std::tolower(static_cast<unsigned char>((*a)[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

}  // namespace

std::string open_command(const std::string& exe) { return "\"" + exe + "\" \"%1\""; }

std::vector<Entry> entries(const std::string& exe) {
    return {
        {ext_key(), "", kProgId},
        {ext_key() + "\\OpenWithProgids", kProgId, ""},
        {progid_key(), "", kTypeName},
        {progid_key() + "\\DefaultIcon", "", "\"" + exe + "\",0"},
        {progid_key() + "\\shell\\open\\command", "", open_command(exe)},
    };
}

bool is_current(Registry& r, const std::string& exe) {
    for (const Entry& e : entries(exe))
        if (!same(r.get(e.key, e.name), e.value)) return false;
    return true;
}

bool owned(Registry& r, const std::string& exe) {
    return same(r.get(progid_key() + "\\shell\\open\\command", ""), open_command(exe));
}

Action decide(Registry& r, const std::string& exe, bool want) {
    if (want) return is_current(r, exe) ? Action::None : Action::Register;
    return owned(r, exe) ? Action::Unregister : Action::None;
}

bool do_register(Registry& r, const std::string& exe) {
    bool ok = true;
    for (const Entry& e : entries(exe)) ok = r.set(e.key, e.name, e.value) && ok;
    return ok;
}

bool do_unregister(Registry& r, const std::string& exe) {
    if (!owned(r, exe)) return false;
    bool ok = r.erase_tree(progid_key());
    if (same(r.get(ext_key(), ""), kProgId)) ok = r.erase_value(ext_key(), "") && ok;
    const std::string owp = ext_key() + "\\OpenWithProgids";
    if (r.get(owp, kProgId)) ok = r.erase_value(owp, kProgId) && ok;
    r.erase_key_if_empty(owp);         // other programs' entries keep the keys
    r.erase_key_if_empty(ext_key());
    return ok;
}

Action sync(Registry& r, const std::string& exe, bool want, bool* ok) {
    const Action a = decide(r, exe, want);
    bool good = true;
    if (a == Action::Register) good = do_register(r, exe);
    else if (a == Action::Unregister) good = do_unregister(r, exe);
    if (ok) *ok = good;
    return good ? a : Action::None;
}

// ---- the real registry ---------------------------------------------------------------------------
#ifdef _WIN32
namespace {

std::wstring wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(std::size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(std::size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

struct UserRegistry final : Registry {
    std::optional<std::string> get(const std::string& key, const std::string& name) override {
        const std::wstring k = wide(key), n = wide(name);
        DWORD bytes = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, k.c_str(), n.empty() ? nullptr : n.c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ |
                         RRF_NOEXPAND, nullptr, nullptr, &bytes) != ERROR_SUCCESS)
            return std::nullopt;
        std::wstring v(bytes / sizeof(wchar_t) + 1, L'\0');
        bytes = DWORD(v.size() * sizeof(wchar_t));
        if (RegGetValueW(HKEY_CURRENT_USER, k.c_str(), n.empty() ? nullptr : n.c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ |
                         RRF_NOEXPAND, nullptr, v.data(), &bytes) != ERROR_SUCCESS)
            return std::nullopt;
        v.resize(wcsnlen(v.c_str(), v.size()));
        return narrow(v);
    }
    bool set(const std::string& key, const std::string& name, const std::string& value) override {
        HKEY h = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, wide(key).c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h, nullptr) != ERROR_SUCCESS)
            return false;
        const std::wstring n = wide(name), v = wide(value);
        const LONG rc = RegSetValueExW(h, n.empty() ? nullptr : n.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(v.c_str()),
                                       DWORD((v.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(h);
        return rc == ERROR_SUCCESS;
    }
    bool erase_value(const std::string& key, const std::string& name) override {
        const std::wstring n = wide(name);
        const LONG rc = RegDeleteKeyValueW(HKEY_CURRENT_USER, wide(key).c_str(), n.empty() ? nullptr : n.c_str());
        return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
    }
    bool erase_tree(const std::string& key) override {
        const LONG rc = RegDeleteTreeW(HKEY_CURRENT_USER, wide(key).c_str());
        if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) return false;
        const LONG rk = RegDeleteKeyW(HKEY_CURRENT_USER, wide(key).c_str());   // RegDeleteTree keeps the key itself
        return rk == ERROR_SUCCESS || rk == ERROR_FILE_NOT_FOUND;
    }
    bool erase_key_if_empty(const std::string& key) override {
        HKEY h = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, wide(key).c_str(), 0, KEY_QUERY_VALUE, &h) != ERROR_SUCCESS) return true;
        DWORD subkeys = 0, values = 0;
        const LONG rc = RegQueryInfoKeyW(h, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr, &values, nullptr, nullptr,
                                         nullptr, nullptr);
        RegCloseKey(h);
        if (rc != ERROR_SUCCESS || subkeys || values) return false;
        return RegDeleteKeyW(HKEY_CURRENT_USER, wide(key).c_str()) == ERROR_SUCCESS;
    }
};

}  // namespace

std::unique_ptr<Registry> user_registry() {
    if (std::getenv("CLERIA_DRIVE")) return nullptr;   // a headless / test run never touches the registry
    return std::make_unique<UserRegistry>();
}
void notify_shell() {
    if (std::getenv("CLERIA_DRIVE")) return;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
#else
std::unique_ptr<Registry> user_registry() { return nullptr; }
void notify_shell() {}
#endif

}  // namespace cleria::fileassoc
