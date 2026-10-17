// Ini: the line-preserving document of retail's yso_win.ini dialect. See ini.hpp and
// build/shell_re/CONFIG.md §1.3. Reader semantics re-checked in the export: FUN_005d07f0 (int),
// FUN_005d0590 (string).
#include "cleria/ini.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace cleria {

namespace {

bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
std::string_view trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}
bool header_of(std::string_view line, std::string_view* name) {
    const std::string_view t = trim(line);
    if (t.size() < 2 || t.front() != '[' || t.back() != ']') return false;
    if (name) *name = trim(t.substr(1, t.size() - 2));
    return true;
}
// A "key=value" line (not a header, not a ; / # comment). [H] spaces around the key / value are
// trimmed; retail's own files never have any.
bool kv_of(std::string_view line, std::string_view* key, std::string_view* val) {
    const std::string_view t = trim(line);
    if (t.empty() || t.front() == ';' || t.front() == '#' || t.front() == '[') return false;
    const std::size_t eq = t.find('=');
    if (eq == std::string_view::npos || eq == 0) return false;
    if (key) *key = trim(t.substr(0, eq));
    if (val) *val = trim(t.substr(eq + 1));
    return true;
}

}  // namespace

std::optional<std::int32_t> parse_ini_int(std::string_view v) {
    // FUN_005d07f0: a leading "0x"/"0X" -> base 16, "0b"/"0B" -> base 2 (both skipped), else base 10;
    // then strtol (leading blanks and a sign only in the base-10 case; garbage reads as 0).
    std::string s(v);
    int base = 10;
    std::size_t at = 0;
    if (s.size() >= 2 && s[0] == '0') {
        if (s[1] == 'x' || s[1] == 'X') { base = 16; at = 2; }
        else if (s[1] == 'b' || s[1] == 'B') { base = 2; at = 2; }
    }
    return std::int32_t(std::strtol(s.c_str() + at, nullptr, base));
}

// ---- Ini -----------------------------------------------------------------------------------------
void Ini::parse(std::string_view text) {
    lines.clear();
    std::size_t p = 0;
    while (p < text.size()) {
        std::size_t e = text.find('\n', p);
        if (e == std::string_view::npos) e = text.size();
        std::string_view l = text.substr(p, e - p);
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        lines.emplace_back(l);
        p = e + 1;
    }
}

bool Ini::load(const fs::path& p) {
    lines.clear();
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    parse(ss.str());
    return true;
}

std::string Ini::text() const {
    std::string o;
    for (const std::string& l : lines) { o += l; o += "\r\n"; }
    return o;
}

bool Ini::save(const fs::path& p) const {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const std::string t = text();
    f.write(t.data(), std::streamsize(t.size()));
    return bool(f);
}

namespace {
int find_line(const std::vector<std::string>& lines, std::string_view sec, std::string_view key) {
    std::string_view cur;   // the root
    for (int i = 0; i < int(lines.size()); ++i) {
        std::string_view h, k;
        if (header_of(lines[std::size_t(i)], &h)) { cur = h; continue; }
        if (ieq(cur, sec) && kv_of(lines[std::size_t(i)], &k, nullptr) && ieq(k, key)) return i;
    }
    return -1;
}
}  // namespace

bool Ini::has(std::string_view sec, std::string_view key) const { return find_line(lines, sec, key) >= 0; }

std::optional<std::string> Ini::raw(std::string_view sec, std::string_view key) const {
    const int i = find_line(lines, sec, key);
    if (i < 0) return std::nullopt;
    std::string_view v;
    kv_of(lines[std::size_t(i)], nullptr, &v);
    return std::string(v);
}

std::optional<std::int32_t> Ini::get_int(std::string_view sec, std::string_view key) const {
    const auto r = raw(sec, key);
    if (!r) return std::nullopt;   // 0x80ff0008: every caller keeps its default
    return parse_ini_int(*r);
}

std::optional<float> Ini::get_float(std::string_view sec, std::string_view key) const {
    const auto r = raw(sec, key);
    if (!r) return std::nullopt;
    return float(std::atof(r->c_str()));
}

std::optional<std::string> Ini::get_string(std::string_view sec, std::string_view key) const {
    // FUN_005d0590: the value from its 2nd byte (the opening quote is dropped unconditionally), cut
    // at the LAST '"'. [not modelled] the Shift-JIS lead-byte copy (byte-identical for ASCII).
    const auto r = raw(sec, key);
    if (!r) return std::nullopt;
    std::string v = r->empty() ? std::string() : r->substr(1);
    if (const std::size_t q = v.rfind('"'); q != std::string::npos) v.resize(q);
    return v;
}

void Ini::set_raw(std::string_view sec, std::string_view key, std::string_view value) {
    if (const int i = find_line(lines, sec, key); i >= 0) {
        std::string_view k;
        kv_of(lines[std::size_t(i)], &k, nullptr);
        lines[std::size_t(i)] = std::string(k) + "=" + std::string(value);
        return;
    }
    const std::string kv = std::string(key) + "=" + std::string(value);
    // The section's span [body, end).
    int hdr = -1, end = int(lines.size());
    if (sec.empty()) {
        for (int i = 0; i < int(lines.size()); ++i)
            if (header_of(lines[std::size_t(i)], nullptr)) { end = i; break; }
    } else {
        for (int i = 0; i < int(lines.size()); ++i) {
            std::string_view h;
            if (!header_of(lines[std::size_t(i)], &h)) continue;
            if (hdr < 0 && ieq(h, sec)) hdr = i;
            else if (hdr >= 0) { end = i; break; }
        }
        if (hdr < 0) {
            if (!lines.empty() && !trim(lines.back()).empty()) lines.emplace_back();
            lines.push_back("[" + std::string(sec) + "]");
            lines.push_back(kv);
            return;
        }
    }
    // After the section's last non-blank line (blank separator lines stay before the next header).
    int at = hdr + 1;
    for (int i = end - 1; i > hdr; --i)
        if (!trim(lines[std::size_t(i)]).empty()) { at = i + 1; break; }
    lines.insert(lines.begin() + at, kv);
}

void Ini::set_int(std::string_view sec, std::string_view key, std::int32_t v, bool hex) {
    char b[32];
    if (hex) std::snprintf(b, sizeof b, "0x%x", unsigned(v));
    else std::snprintf(b, sizeof b, "%d", int(v));
    set_raw(sec, key, b);
}

void Ini::set_float(std::string_view sec, std::string_view key, float v) {
    char b[64];
    std::snprintf(b, sizeof b, "%0.8f", double(v));
    set_raw(sec, key, b);
}

void Ini::set_string(std::string_view sec, std::string_view key, std::string_view v) {
    std::string q = "\"";
    for (char c : v) if ((unsigned char)c >= 0x20) q.push_back(c);   // bytes < 0x20 are dropped
    q.push_back('"');
    set_raw(sec, key, q);
}

bool Ini::erase(std::string_view sec, std::string_view key) {
    const int i = find_line(lines, sec, key);
    if (i < 0) return false;
    lines.erase(lines.begin() + i);
    return true;
}

}  // namespace cleria
