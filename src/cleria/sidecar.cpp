#include "cleria/sidecar.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "cleria/inidoc.hpp"

namespace cleria {
namespace fs = std::filesystem;

std::int32_t Sidecar::flag(std::string_view name) const {
    const auto it = flags.find(ini_lower(name));
    return it == flags.end() ? 0 : it->second;
}

void Sidecar::set_flag(std::string_view name, std::int32_t v) {
    const std::string k = ini_lower(name);
    if (v == 0) flags.erase(k);
    else flags[k] = v;
}

std::int32_t Sidecar::item(int id) const {
    const auto it = items.find(id);
    return it == items.end() ? -1 : it->second;
}

void Sidecar::add_item(int id, std::int32_t n) {
    std::int32_t& c = items.try_emplace(id, -1).first->second;
    c = std::clamp(std::max(c, 0) + n, 0, 99);   // [H] the retail count cap (inv_sanitize: <= 99)
}

namespace {
std::string quote(const std::string& s) {
    std::string r = "\"";
    for (char c : s) {
        if (c == '"') r += "\\\"";
        else r += c;
    }
    return r + "\"";
}
// [mod.<id>] values: '%', '"', '\' and control bytes as %XX, so any text (a script's JSON) survives the
// INI quoting, which only knows \".
std::string pct_encode(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        if (c == '%' || c == '"' || c == '\\' || c < 0x20 || c == 0x7F) {
            char b[4];
            std::snprintf(b, sizeof b, "%%%02X", unsigned(c));
            r += b;
        } else {
            r += char(c);
        }
    }
    return r;
}
std::string pct_decode(const std::string& s) {
    std::string r;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            r += char(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            r += s[i];
        }
    }
    return r;
}
}  // namespace

std::string Sidecar::serialize() const {
    std::ostringstream o;
    o << "; CleriaCore save sidecar (build/shell_re/CONTENT.md). The .bin beside it is the retail save.\r\n";
    o << "[cleria]\r\nVersion=" << kVersion << "\r\n";
    if (!mods.empty()) {
        std::string m;
        for (const std::string& s : mods) m += (m.empty() ? "" : ",") + s;
        o << "Mods=" << quote(m) << "\r\n";
    }
    if (!flags.empty()) {
        o << "\r\n[flags]\r\n";
        for (const auto& [k, v] : flags) o << k << '=' << v << "\r\n";
    }
    if (!items.empty()) {
        o << "\r\n[items]\r\n";
        char b[16];
        for (const auto& [k, v] : items) {
            std::snprintf(b, sizeof b, "0x%X", k);
            o << b << '=' << v << "\r\n";
        }
    }
    for (const auto& [id, kv] : mod) {
        if (kv.empty()) continue;
        o << "\r\n[mod." << id << "]\r\n";
        for (const auto& [k, v] : kv) o << k << '=' << quote(pct_encode(v)) << "\r\n";
    }
    return o.str();
}

std::optional<Sidecar> Sidecar::parse(std::string_view text) {
    const IniDoc d = IniDoc::parse(text);
    Sidecar s;
    bool have_version = false;
    for (const IniDoc::Section& sec : d.sections) {
        const std::string n = ini_lower(sec.name);
        for (const IniDoc::Pair& p : sec.pairs) {
            const std::string k = ini_lower(p.key);
            if (n == "cleria") {
                if (k == "version") {
                    const int v = std::atoi(p.value.c_str());
                    if (v < 1 || v > kVersion) return std::nullopt;
                    have_version = true;
                } else if (k == "mods") {
                    std::string cur;
                    for (char c : p.value + ",") {
                        if (c == ',') { if (!ini_trim(cur).empty()) s.mods.push_back(ini_trim(cur)); cur.clear(); }
                        else cur += c;
                    }
                }
            } else if (n == "flags") {
                s.flags[k] = std::int32_t(std::strtol(p.value.c_str(), nullptr, 0));
            } else if (n == "items") {
                char* end = nullptr;
                const long id = std::strtol(p.key.c_str(), &end, 0);
                if (end && *end == 0) s.items[int(id)] = std::int32_t(std::strtol(p.value.c_str(), nullptr, 0));
            } else if (n.size() > 4 && n.rfind("mod.", 0) == 0) {
                s.mod[n.substr(4)][p.key] = pct_decode(p.value);   // the key as written
            }
        }
    }
    if (!have_version) return std::nullopt;
    return s;
}

fs::path sidecar_path(const fs::path& save_file) {
    fs::path p = save_file;
    p.replace_extension(".cleria");
    return p;
}

bool write_sidecar(const fs::path& save_file, const Sidecar& s) {
    const fs::path p = sidecar_path(save_file);
    std::error_code ec;
    if (s.empty()) {
        fs::remove(p, ec);
        return true;
    }
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary);
    const std::string t = s.serialize();
    f.write(t.data(), std::streamsize(t.size()));
    return bool(f);
}

Sidecar read_sidecar(const fs::path& save_file) {
    std::ifstream f(sidecar_path(save_file), std::ios::binary);
    if (!f) return {};
    const std::string t{std::istreambuf_iterator<char>(f), {}};
    return Sidecar::parse(t).value_or(Sidecar{});
}

void remove_sidecar(const fs::path& save_file) {
    std::error_code ec;
    fs::remove(sidecar_path(save_file), ec);
}

std::string save_profile_name(std::string_view raw) {
    std::string n(raw.substr(0, 64));
    for (char& c : n)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) c = '_';
    if (n.find_first_not_of('.') == std::string::npos) return {};
    if (n.back() == '.') n.back() = '_';
    return n;
}

fs::path save_profile_dir(const fs::path& save_dir, const std::string& name) {
    return name.empty() ? save_dir : save_dir / "profiles" / name;
}

}  // namespace cleria
