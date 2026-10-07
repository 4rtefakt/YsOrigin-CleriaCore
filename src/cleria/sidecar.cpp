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
    if (!ap.empty()) {
        o << "\r\n[ap]\r\n";
        for (const auto& [k, v] : ap) o << k << '=' << quote(v) << "\r\n";
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
            } else if (n == "ap") {
                s.ap[k] = p.value;
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

}  // namespace cleria
