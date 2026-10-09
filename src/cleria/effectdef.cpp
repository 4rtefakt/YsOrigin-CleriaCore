#include "cleria/effectdef.hpp"
#include "cleria/vfs.hpp"   // mods: asset opens go through vfs::resolve

#include <cctype>
#include <cstdio>
#include <fstream>
#include <vector>

namespace cleria {
namespace {

namespace fs = std::filesystem;

constexpr char kDelims[] = " \t\r:/.,-";

bool is_delim(char c) {
    for (const char* d = kDelims; *d; ++d)
        if (c == *d) return true;
    return false;
}

// C atoi(): leading whitespace, optional sign, digits, stop at the first non-digit.
int c_atoi(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
    int sign = 1;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
        if (s[i] == '-') sign = -1;
        ++i;
    }
    int v = 0;
    while (i < s.size() && std::isdigit((unsigned char)s[i])) { v = v * 10 + (s[i] - '0'); ++i; }
    return sign * v;
}

float c_atof(const std::string& s) { return float(c_atoi(s)); }

// The columns, in the order FUN_00439ac0's switch dispatches them.
enum Col { kId = 0, kName, k3d, kLoop, kNega, kDup, kLoad1st,
           kManual, kCancel, kMindist, kMaxdist, kForcesw, kWishhw };

void assign(std::unordered_map<int, EffectDef>& recs, int id, int col, const std::string& tok) {
    if (id < 0) return;
    EffectDef& r = recs[id];
    r.id = id;
    switch (col) {
        case kName:    r.name = tok; break;
        case k3d:      r.is3d = c_atoi(tok) != 0; break;
        case kLoop:    r.loop = c_atoi(tok) != 0; break;
        case kNega:    r.nega = c_atoi(tok) != 0; break;
        case kManual:  r.manual = c_atoi(tok) != 0; break;
        case kDup:     r.dup = c_atoi(tok); break;
        case kCancel:  r.cancel = c_atoi(tok); break;
        case kMindist: r.mindist = c_atof(tok); break;
        case kMaxdist: r.maxdist = c_atof(tok); break;
        default: break;
    }
}

}  // namespace

EffectDefs EffectDefs::parse(const std::string& text) {
    EffectDefs out;
    auto& recs = out.by_id_;

    // strtok: skip runs of delimiters, then take everything up to the next one.
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i < text.size();) {
        while (i < text.size() && is_delim(text[i])) ++i;
        if (i >= text.size()) break;
        std::size_t j = i;
        while (j < text.size() && !is_delim(text[j])) ++j;
        tokens.emplace_back(text, i, j - i);
        i = j;
    }

    int col = 0, cur_id = -1;
    bool skip_line = false;
    for (std::string tok : tokens) {
        // '\n' is not a delimiter, so a token can carry leading newlines. Each
        // one ends the current record (and any comment).
        while (!tok.empty() && tok.front() == '\n') {
            tok.erase(tok.begin());
            col = 0;
            skip_line = false;
        }
        if (tok.empty()) continue;

        if (skip_line) {
            const std::size_t nl = tok.find('\n');
            if (nl == std::string::npos) continue;  // still inside the comment
            skip_line = false;
            col = 0;
            tok = tok.substr(nl + 1);
            if (tok.empty()) continue;
        }
        if (tok[0] == ';') {
            skip_line = tok.find('\n') == std::string::npos;
            if (!skip_line) col = 0;
            continue;
        }
        // A newline inside a token ends the record mid-token; the tail becomes
        // the next record's first column.
        const std::size_t nl = tok.find('\n');
        if (nl != std::string::npos) {
            const std::string head = tok.substr(0, nl);
            std::string rest = tok.substr(nl + 1);
            if (!head.empty()) {
                if (col == 0) cur_id = c_atoi(head);
                assign(recs, cur_id, col, head);
            }
            col = 0;
            // Only the text up to a further newline belongs to the next record's
            // first column; strtok already split on delimiters, so take it whole.
            const std::size_t nl2 = rest.find('\n');
            if (nl2 != std::string::npos) rest = rest.substr(0, nl2);
            if (!rest.empty()) {
                cur_id = c_atoi(rest);
                assign(recs, cur_id, 0, rest);
                col = 1;
            }
            continue;
        }
        if (col == 0) cur_id = c_atoi(tok);
        assign(recs, cur_id, col, tok);
        ++col;
    }
    return out;
}

EffectDefs EffectDefs::load(const fs::path& asset_root) {
    const fs::path p = asset_root / "WAVE" / "EFFECT" / "EFFECTDEF.TXT";
    std::ifstream f(vfs::resolve(p), std::ios::binary);
    if (!f) return {};
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parse(text);
}

const EffectDef* EffectDefs::find(int id) const {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &it->second;
}

fs::path EffectDefs::wav_path(const fs::path& asset_root, int id) const {
    const EffectDef* d = find(id);
    if (!d || d->name.empty()) return {};
    // Names are "dir\stem"; the extracted tree is upper-case with a .WAV suffix.
    std::string n = d->name;
    for (char& c : n) {
        if (c == '\\') c = '/';
        c = char(std::toupper((unsigned char)c));
    }
    return asset_root / "WAVE" / "EFFECT" / (n + ".WAV");
}

}  // namespace cleria
