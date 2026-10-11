// retail_probe_tests.ini parser. Format: see the header of that file.
#include "tests.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) a++;
    while (b > a && isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
static std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        size_t b = s.find(sep, a);
        std::string part = trim(s.substr(a, b == std::string::npos ? b : b - a));
        if (!part.empty()) out.push_back(part);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}
static bool to_int(const std::string& s, int& v) {
    char* end = nullptr;
    long x = strtol(s.c_str(), &end, 0);
    if (end == s.c_str() || *end) return false;
    v = (int)x;
    return true;
}
// "a=1, b=2" -> pairs; a bare "a" means a=1.
static bool int_pairs(const std::string& s, std::vector<std::pair<int, int>>& out) {
    for (const std::string& part : split(s, ',')) {
        size_t eq = part.find('=');
        int k = 0, v = 1;
        if (!to_int(trim(part.substr(0, eq)), k)) return false;
        if (eq != std::string::npos && !to_int(trim(part.substr(eq + 1)), v)) return false;
        out.push_back({k, v});
    }
    return true;
}

const char* char_name(int c) {
    static const char* n[] = {"Adol", "Yunica", "Hugo", "Toal"};
    return (c >= 0 && c < 4) ? n[c] : "any";
}
const char* diff_name(int d) {
    static const char* n[] = {"Very Easy", "Easy", "Normal", "Hard", "Nightmare"};
    return (d >= 0 && d < 5) ? n[d] : "keep";
}

static bool set_key(Test& t, const std::string& key, const std::string& val, std::string& err) {
    const std::string v = lower(val);
    if (key == "group") t.group = val;
    else if (key == "question") t.question = val;
    else if (key == "do") t.todo = val;
    else if (key == "watch") t.watch = val;
    else if (key == "status") t.status = val;
    else if (key == "note") t.note = val;
    else if (key == "room") t.room = val;
    else if (key == "character") {
        if (v == "any") t.character = -1;
        else if (v == "adol") t.character = 0;
        else if (v == "yunica") t.character = 1;
        else if (v == "hugo") t.character = 2;
        else if (v == "toal") t.character = 3;
        else { err = "character must be any/yunica/hugo/toal/adol"; return false; }
    } else if (key == "difficulty") {
        if (v == "any" || v == "keep") t.difficulty = -1;
        else if (v == "veryeasy") t.difficulty = 0;
        else if (v == "easy") t.difficulty = 1;
        else if (v == "normal") t.difficulty = 2;
        else if (v == "hard") t.difficulty = 3;
        else if (v == "nightmare") t.difficulty = 4;
        else { err = "difficulty must be keep/veryeasy/easy/normal/hard/nightmare"; return false; }
    } else if (key == "pos") {
        if (sscanf(val.c_str(), "%f , %f , %f", &t.pos[0], &t.pos[1], &t.pos[2]) != 3) {
            err = "pos must be x,y,z"; return false;
        }
        t.has_pos = true;
    } else if (key == "facing") {
        t.facing_deg = (float)atof(val.c_str());
        t.has_facing = true;
    } else if (key == "level") {
        if (!to_int(val, t.level) || t.level < 0 || t.level > 99) { err = "level 1..99"; return false; }
    } else if (key == "weapon_tier") {
        if (!to_int(val, t.weapon_tier) || t.weapon_tier < 0 || t.weapon_tier > 10) { err = "weapon_tier 0..10"; return false; }
    } else if (key == "items") {
        if (!int_pairs(val, t.items)) { err = "items: id or id=count, comma separated"; return false; }
        for (auto& p : t.items) if (p.first < 0 || p.first > 127) { err = "item id 0..127"; return false; }
    } else if (key == "flags") {
        if (!int_pairs(val, t.flags)) { err = "flags: index=value, comma separated"; return false; }
        for (auto& p : t.flags) if (p.first < 128 || p.first > 511) { err = "flag index 128..511 (items go in items=)"; return false; }
    } else if (key == "gear") {
        if (!int_pairs(val, t.gear)) { err = "gear: item id=value"; return false; }
        for (auto& p : t.gear) if (p.first < 0 || p.first > 53) { err = "gear cell 0..53"; return false; }
    } else if (key == "skills") {
        for (const std::string& part : split(v, ',')) {
            size_t eq = part.find('=');
            std::string k = trim(part.substr(0, eq));
            int lv = 3;
            if (eq != std::string::npos && !to_int(trim(part.substr(eq + 1)), lv)) { err = "skills: wind=3,fire=1"; return false; }
            int slot = k == "wind" ? 0 : k == "thunder" ? 1 : k == "fire" ? 2 : -1;
            if (slot < 0 || lv < 0 || lv > 3) { err = "skills: wind/thunder/fire = 0..3"; return false; }
            t.skill_lv[slot] = lv;
        }
    } else if (key == "equip_skill") {
        t.equip_skill = v == "none" ? -1 : v == "wind" ? 0 : v == "thunder" ? 1 : v == "fire" ? 2 : -2;
        if (t.equip_skill == -2 && v != "keep") { err = "equip_skill: keep/none/wind/thunder/fire"; return false; }
    } else if (key == "hp") t.hp_full = (v != "keep");
    else if (key == "mp") t.mp_full = (v != "keep");
    else if (key == "boost") t.boost = v == "full" ? 1 : v == "empty" ? 0 : -1;
    else if (key == "sp") { if (!to_int(val, t.sp)) { err = "sp: a number"; return false; } }
    else if (key == "config") {
        for (const std::string& part : split(v, ',')) {
            size_t eq = part.find('=');
            int x = 0;
            if (eq == std::string::npos || !to_int(trim(part.substr(eq + 1)), x)) { err = "config: name=value"; return false; }
            t.config.push_back({trim(part.substr(0, eq)), x});
        }
    } else if (key == "config_buttons") {
        for (const std::string& part : split(v, ',')) {
            size_t c = part.find(':');
            if (c == std::string::npos) { err = "config_buttons: name:0|2"; return false; }
            CfgButton b;
            b.name = trim(part.substr(0, c));
            for (const std::string& n : split(part.substr(c + 1), '|')) {
                int x = 0;
                if (!to_int(n, x)) { err = "config_buttons: name:0|2"; return false; }
                b.values.push_back(x);
            }
            t.cfg_buttons.push_back(b);
        }
    } else if (key == "helpers") t.helpers = split(v, ',');
    else if (key == "answers") t.answers = split(val, '|');
    else if (key == "show_flags") {
        for (const std::string& n : split(val, ',')) {
            int x = 0;
            if (!to_int(n, x) || x < 0 || x > 511) { err = "show_flags: indices 0..511"; return false; }
            t.show_flags.push_back(x);
        }
    } else { err = "unknown key"; return false; }
    return true;
}

bool load_tests(const char* path, TestFile& out) {
    out = TestFile();
    out.path = path;
    FILE* f = fopen(path, "r");
    if (!f) {
        out.errors.push_back(std::string("cannot open ") + path);
        return false;
    }
    char buf[4096];
    int line_no = 0;
    Test* cur = nullptr;
    bool in_probe = false;
    while (fgets(buf, sizeof(buf), f)) {
        line_no++;
        std::string line = trim(buf);
        if (line_no == 1 && line.size() >= 3 && (unsigned char)line[0] == 0xEF) line = trim(line.substr(3));  // BOM
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            size_t e = line.find(']');
            std::string name = trim(line.substr(1, e == std::string::npos ? e : e - 1));
            cur = nullptr;
            in_probe = false;
            if (lower(name) == "probe") in_probe = true;
            else if (lower(name).rfind("test ", 0) == 0) {
                out.tests.emplace_back();
                cur = &out.tests.back();
                cur->id = trim(name.substr(5));
            } else out.errors.push_back("line " + std::to_string(line_no) + ": unknown section [" + name + "]");
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            out.errors.push_back("line " + std::to_string(line_no) + ": expected key = value");
            continue;
        }
        std::string key = lower(trim(line.substr(0, eq))), val = trim(line.substr(eq + 1));
        if (in_probe) {
            if (key == "toggle_vk") to_int(val, out.toggle_vk);
            continue;
        }
        if (!cur) continue;
        std::string err;
        if (!set_key(*cur, key, val, err))
            out.errors.push_back("line " + std::to_string(line_no) + " [" + cur->id + "] " + key + ": " + err);
    }
    fclose(f);
    for (Test& t : out.tests) {
        if (t.group.empty()) t.group = "Other";
        if (t.question.empty()) t.question = t.id;
        if (t.answers.empty()) t.answers = {"Yes", "No", "Unclear"};
    }
    return true;
}
