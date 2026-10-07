#include "cleria/inidoc.hpp"

#include <cctype>

namespace cleria {

std::string ini_lower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = char(std::tolower((unsigned char)c));
    return r;
}

std::string ini_trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}

const IniDoc::Pair* IniDoc::Section::find(std::string_view key) const {
    const std::string k = ini_lower(key);
    for (auto it = pairs.rbegin(); it != pairs.rend(); ++it)
        if (ini_lower(it->key) == k) return &*it;
    return nullptr;
}

namespace {
// A quoted token starting at s[i] == '"': its text (\" unescaped) and the index past the close.
bool read_quoted(std::string_view s, std::size_t& i, std::string& out) {
    out.clear();
    ++i;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '\\' && i + 1 < s.size() && s[i + 1] == '"') { out += '"'; i += 2; continue; }
        if (c == '"') { ++i; return true; }
        out += c;
        ++i;
    }
    return false;   // unterminated
}

// Cut a trailing " ; comment" (whitespace, then ';', outside quotes).
std::string_view strip_comment(std::string_view v) {
    bool q = false;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const char c = v[i];
        if (c == '\\' && q && i + 1 < v.size()) { ++i; continue; }
        if (c == '"') q = !q;
        else if (!q && c == ';' && i > 0 && (v[i - 1] == ' ' || v[i - 1] == '\t')) return v.substr(0, i);
    }
    return v;
}
}  // namespace

IniDoc IniDoc::parse(std::string_view text) {
    IniDoc d;
    d.sections.push_back({});   // the root
    std::size_t p = 0;
    int ln = 0;
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
        p = 3;   // a UTF-8 BOM (Notepad)
    while (p <= text.size()) {
        std::size_t e = text.find('\n', p);
        if (e == std::string_view::npos) e = text.size();
        ++ln;
        const std::string line = ini_trim(text.substr(p, e - p));
        p = e + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            if (e == text.size()) break;
            continue;
        }
        if (line[0] == '[') {
            const std::size_t c = line.rfind(']');
            if (c == std::string::npos) {
                d.errors.push_back("line " + std::to_string(ln) + ": no ']'");
            } else {
                d.sections.push_back({ini_trim(std::string_view(line).substr(1, c - 1)), ln, {}});
            }
        } else {
            Pair pr;
            pr.line = ln;
            std::size_t i = 0;
            bool ok = true;
            if (line[0] == '"') {
                pr.quoted_key = true;
                ok = read_quoted(line, i, pr.key);
                while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
                if (!ok || i >= line.size() || line[i] != '=') ok = false;
            } else {
                i = line.find('=');
                if (i == std::string::npos) ok = false;
                else pr.key = ini_trim(std::string_view(line).substr(0, i));
            }
            if (!ok || (pr.key.empty() && !pr.quoted_key)) {
                d.errors.push_back("line " + std::to_string(ln) + ": expected key=value");
            } else {
                std::string v = ini_trim(strip_comment(std::string_view(line).substr(i + 1)));
                if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                    std::size_t k = 0;
                    std::string un;
                    if (read_quoted(v, k, un) && k == v.size()) v = un;
                }
                pr.value = std::move(v);
                d.sections.back().pairs.push_back(std::move(pr));
            }
        }
        if (e == text.size()) break;
    }
    return d;
}

}  // namespace cleria
