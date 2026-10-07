#include "cleria/content.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "cleria/inidoc.hpp"
#include "cleria/items.hpp"
#include "cleria/vfs.hpp"

namespace cleria::content {
namespace fs = std::filesystem;

namespace {

std::string upper(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = char(std::toupper((unsigned char)c));
    for (char& c : r) if (c == '\\') c = '/';
    return r;
}

std::string hex(int v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%X", v);
    return b;
}

// Tokens of a condition / action: whitespace-separated, a "quoted" token kept whole (\" unescaped).
std::vector<std::string> tokens(std::string_view s, bool* bad_quote = nullptr) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i >= s.size()) break;
        std::string t;
        if (s[i] == '"') {
            ++i;
            bool closed = false;
            while (i < s.size()) {
                if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '"') { t += '"'; i += 2; continue; }
                if (s[i] == '"') { ++i; closed = true; break; }
                t += s[i++];
            }
            if (!closed && bad_quote) *bad_quote = true;
            out.push_back("\x01" + t);   // marked as quoted
        } else {
            while (i < s.size() && s[i] != ' ' && s[i] != '\t') t += s[i++];
            out.push_back(t);
        }
    }
    return out;
}
bool is_quoted(const std::string& t) { return !t.empty() && t[0] == '\x01'; }
std::string unq(const std::string& t) { return is_quoted(t) ? t.substr(1) : t; }

bool is_op(std::string_view o) {
    return o == "=" || o == "==" || o == "!=" || o == "<" || o == "<=" || o == ">" || o == ">=";
}

void note_pair(Content& c, const std::string& old_mod, const std::string& new_mod, const std::string& what) {
    if (old_mod.empty() || old_mod == new_mod) return;
    c.notes.push_back({old_mod, what + " replaced by " + new_mod});
    c.notes.push_back({new_mod, "replaces " + what + " of " + old_mod});
}

void err(Content& c, const std::string& mod, std::string_view file, int line, const std::string& msg) {
    c.errors.push_back({mod, std::string(file) + (line > 0 ? " line " + std::to_string(line) : std::string()) +
                                 ": " + msg});
}

// ---- the five parsers ---------------------------------------------------------------------------

void merge_items(Content& c, const std::string& mod, std::string_view file, const IniDoc& d) {
    for (const IniDoc::Section& s : d.sections) {
        if (s.name.empty() && s.pairs.empty()) continue;
        const std::string sn = ini_lower(s.name);
        if (sn.rfind("item.", 0) != 0) { err(c, mod, file, s.line, "unknown section [" + s.name + "]"); continue; }
        const std::optional<int> id = parse_int(sn.substr(5));
        if (!id || *id < 0 || (*id >= kRetailItems && *id < kNewItemFirst) || *id > kNewItemLast) {
            err(c, mod, file, s.line, "item id must be 0x00..0x7F (retail) or 0x100..0x1FF (new): [" + s.name + "]");
            continue;
        }
        ItemDef& it = c.items[*id];
        it.id = *id;
        auto set = [&](const char* field, auto& slot, auto value) {
            const auto w = it.by.find(field);
            if (w != it.by.end()) note_pair(c, w->second, mod, "item " + hex(*id) + " " + field);
            slot = value;
            it.by[field] = mod;
        };
        for (const IniDoc::Pair& p : s.pairs) {
            const std::string k = ini_lower(p.key);
            if (k == "name") set("Name", it.name, std::optional<std::string>(p.value));
            else if (k == "description") set("Description", it.description, std::optional<std::string>(p.value));
            else if (k == "icon") set("Icon", it.icon, std::optional<std::string>(ini_lower(p.value)));
            else if (k == "magnitude") {
                const std::optional<int> v = parse_int(p.value);
                if (!v) err(c, mod, file, p.line, "Magnitude is not a number");
                else set("Magnitude", it.magnitude, std::optional<std::uint32_t>(std::uint32_t(*v)));
            } else if (k == "kind" || k == "amount" || k == "setsflag") {
                if (*id < kNewItemFirst) { err(c, mod, file, p.line, p.key + " applies to new items only"); continue; }
                if (k == "kind") {
                    const std::string v = ini_lower(p.value);
                    if (v != "key" && v != "heal" && v != "gold" && v != "flag")
                        err(c, mod, file, p.line, "Kind must be key, heal, gold or flag");
                    else set("Kind", it.kind, v);
                } else if (k == "amount") {
                    const std::optional<int> v = parse_int(p.value);
                    if (!v) err(c, mod, file, p.line, "Amount is not a number");
                    else set("Amount", it.amount, *v);
                } else {
                    set("SetsFlag", it.sets_flag, ini_lower(p.value));
                }
            } else {
                err(c, mod, file, p.line, "unknown key " + p.key);
            }
        }
        if (*id >= kNewItemFirst && !it.name) it.name = "Mod item " + hex(*id);   // [H] a visible default
    }
}

void merge_chests(Content& c, const std::string& mod, std::string_view file, const IniDoc& d) {
    for (const IniDoc::Section& s : d.sections) {
        if (s.name.empty() && s.pairs.empty()) continue;
        std::string key = upper(s.name);
        const std::size_t sl = key.find('/');
        if (sl == std::string::npos || sl == 0 || sl + 1 >= key.size()) {
            err(c, mod, file, s.line, "a chest is [<room>/<script>] or [<room>/<script>/<item>]: [" + s.name + "]");
            continue;
        }
        if (const std::size_t s2 = key.find('/', sl + 1); s2 != std::string::npos) {   // the item part
            const std::optional<int> it = parse_int(key.substr(s2 + 1));
            if (!it || s2 == sl + 1) {
                err(c, mod, file, s.line, "the item of [" + s.name + "] is not an item id");
                continue;
            }
            key = key.substr(0, s2 + 1) + hex(*it);
        }
        ChestDef cd;
        cd.key = key;
        cd.mod = mod;
        bool have_item = false, ok = true;
        for (const IniDoc::Pair& p : s.pairs) {
            const std::string k = ini_lower(p.key);
            if (k == "item") {
                if (ini_lower(p.value) == "none") { cd.nothing = true; have_item = true; continue; }
                const std::optional<int> v = parse_int(p.value);
                if (!v || *v < 0 || (*v >= kRetailItems && *v < kNewItemFirst) || *v > kNewItemLast) {
                    err(c, mod, file, p.line, "Item must be an item id or none");
                    ok = false;
                } else {
                    cd.item = *v;
                    have_item = true;
                }
            } else if (k == "count") {
                const std::optional<int> v = parse_int(p.value);
                if (!v || *v < 1 || *v > 99) { err(c, mod, file, p.line, "Count must be 1..99"); ok = false; }
                else cd.count = *v;
            } else if (k == "text") {
                cd.text = p.value;
            } else {
                err(c, mod, file, p.line, "unknown key " + p.key);
            }
        }
        if (!have_item) { err(c, mod, file, s.line, "[" + s.name + "] has no Item"); continue; }
        if (!ok) continue;
        const auto old = c.chests.find(key);
        if (old != c.chests.end()) note_pair(c, old->second.mod, mod, "chest " + key);
        c.chests[key] = std::move(cd);
    }
}

void merge_pickups(Content& c, const std::string& mod, std::string_view file, const IniDoc& d) {
    for (const IniDoc::Section& s : d.sections) {
        if (s.name.empty() && s.pairs.empty()) continue;
        const std::string sn = ini_lower(s.name);
        if (sn.rfind("pickup.", 0) != 0 || sn.size() <= 7) { err(c, mod, file, s.line, "unknown section [" + s.name + "]"); continue; }
        PickupDef pd;
        pd.id = sn.substr(7);
        pd.flag = "pickup." + pd.id;
        pd.mod = mod;
        bool ok = true, have_pos = false;
        for (const IniDoc::Pair& p : s.pairs) {
            const std::string k = ini_lower(p.key);
            if (k == "room") pd.room = upper(p.value);
            else if (k == "pos") {
                have_pos = std::sscanf(p.value.c_str(), "%f , %f , %f", &pd.pos[0], &pd.pos[1], &pd.pos[2]) == 3;
                if (!have_pos) { err(c, mod, file, p.line, "Pos must be x,y,z"); ok = false; }
            } else if (k == "item") {
                const std::optional<int> v = parse_int(p.value);
                if (!v || *v < 0 || (*v >= kRetailItems && *v < kNewItemFirst) || *v > kNewItemLast) {
                    err(c, mod, file, p.line, "Item must be an item id");
                    ok = false;
                } else pd.item = *v;
            } else if (k == "count") {
                const std::optional<int> v = parse_int(p.value);
                if (!v || *v < 1 || *v > 99) { err(c, mod, file, p.line, "Count must be 1..99"); ok = false; }
                else pd.count = *v;
            } else if (k == "flag") {
                pd.flag = ini_lower(p.value);
            } else {
                err(c, mod, file, p.line, "unknown key " + p.key);
            }
        }
        if (pd.room.empty() || !have_pos || pd.item < 0) {
            if (ok) err(c, mod, file, s.line, "[" + s.name + "] needs Room, Pos and Item");
            continue;
        }
        if (!ok) continue;
        const auto old = c.pickups.find(pd.id);
        if (old != c.pickups.end()) note_pair(c, old->second.mod, mod, "pickup " + pd.id);
        c.pickups[pd.id] = std::move(pd);
    }
}

void merge_rules(Content& c, const std::string& mod, std::string_view file, const IniDoc& d) {
    for (const IniDoc::Section& s : d.sections) {
        if (s.name.empty() && s.pairs.empty()) continue;
        const std::string sn = ini_lower(s.name);
        if (sn.rfind("rule.", 0) != 0 || sn.size() <= 5) { err(c, mod, file, s.line, "unknown section [" + s.name + "]"); continue; }
        RuleDef r;
        r.name = sn.substr(5);
        r.mod = mod;
        bool ok = true, have_on = false;
        for (const IniDoc::Pair& p : s.pairs) {
            const std::string k = ini_lower(p.key);
            std::string e;
            if (k == "on") {
                const std::optional<Cond> cd = parse_cond(p.value, &e);
                if (!cd) { err(c, mod, file, p.line, e); ok = false; }
                else { r.on = *cd; have_on = true; }
            } else if (k == "do") {
                std::vector<Action> a = parse_actions(p.value, &e);
                if (!e.empty()) { err(c, mod, file, p.line, e); ok = false; }
                r.act.insert(r.act.end(), a.begin(), a.end());
            } else if (k == "once") {
                r.once = parse_int(p.value).value_or(0) != 0;
            } else {
                err(c, mod, file, p.line, "unknown key " + p.key);
            }
        }
        if (!have_on) { if (ok) err(c, mod, file, s.line, "[" + s.name + "] has no On"); continue; }
        if (!ok) continue;
        const auto old = c.rules.find(r.name);
        if (old != c.rules.end()) note_pair(c, old->second.mod, mod, "rule " + r.name);
        c.rules[r.name] = std::move(r);
    }
}

void merge_text(Content& c, const std::string& mod, std::string_view /*file*/, const IniDoc& d) {
    for (const IniDoc::Section& s : d.sections) {
        if (s.name.empty() && s.pairs.empty()) continue;
        const std::string sk = s.name == "*" ? std::string("*") : vfs::key(s.name);
        TextDef& td = c.text[sk];
        for (const IniDoc::Pair& p : s.pairs) {
            std::optional<int> idx;
            if (!p.quoted_key) idx = parse_int(p.key);
            const std::string what = idx ? "#" + std::to_string(*idx) : "\"" + p.key + "\"";
            const auto w = td.by.find(what);
            if (w != td.by.end()) note_pair(c, w->second, mod, "text " + s.name + " " + what);
            td.by[what] = mod;
            if (idx) td.by_index[*idx] = p.value;
            else td.by_text[p.key] = p.value;
        }
    }
}

}  // namespace

std::optional<int> parse_int(std::string_view s) {
    const std::string t = ini_trim(s);
    if (t.empty()) return std::nullopt;
    char* end = nullptr;
    const long v = std::strtol(t.c_str(), &end, 0);
    if (!end || *end != 0) return std::nullopt;
    return int(v);
}

bool compare(std::int32_t a, std::string_view op, std::int32_t b) {
    if (op == "=" || op == "==") return a == b;
    if (op == "!=") return a != b;
    if (op == "<") return a < b;
    if (op == "<=") return a <= b;
    if (op == ">") return a > b;
    if (op == ">=") return a >= b;
    return false;
}

std::optional<Cond> parse_cond(std::string_view s, std::string* e) {
    const std::vector<std::string> t = tokens(s);
    auto fail = [&](const std::string& m) -> std::optional<Cond> { if (e) *e = m; return std::nullopt; };
    if (t.empty()) return fail("On is empty");
    const std::string w = ini_lower(t[0]);
    Cond c;
    if (w == "flag") {
        // flag <index> [<op> <value>]   (default ">= 1": the flag became set)
        if (t.size() != 2 && t.size() != 4) return fail("expected: flag <index> <op> <value>");
        const std::optional<int> i = parse_int(t[1]);
        if (!i || *i < 0 || *i >= 0x200) return fail("flag index must be 0..0x1FF");
        c.kind = events::Kind::FlagChanged;
        c.index = *i;
        if (t.size() == 4) {
            if (!is_op(t[2])) return fail("unknown comparison " + t[2]);
            const std::optional<int> v = parse_int(t[3]);
            if (!v) return fail("flag value is not a number");
            c.op = t[2];
            c.value = *v;
        }
        return c;
    }
    if (w == "room_enter" || w == "room") {
        if (t.size() != 2) return fail("expected: room_enter <room>");
        c.kind = events::Kind::RoomEnter;
        c.arg = upper(unq(t[1]));
        return c;
    }
    if (w == "item") {
        if (t.size() != 2) return fail("expected: item <id>");
        const std::optional<int> i = parse_int(t[1]);
        if (!i || *i < 0 || *i > kNewItemLast) return fail("item id out of range");
        c.kind = events::Kind::ItemObtained;
        c.index = *i;
        return c;
    }
    if (w == "chest") {
        if (t.size() != 2) return fail("expected: chest <room>/<script>");
        c.kind = events::Kind::ChestOpened;
        c.arg = upper(unq(t[1]));
        return c;
    }
    if (t.size() == 1) {
        if (w == "death") { c.kind = events::Kind::PlayerDeath; return c; }
        if (w == "new_game") { c.kind = events::Kind::NewGame; return c; }
        if (w == "load") { c.kind = events::Kind::Load; return c; }
        if (w == "save") { c.kind = events::Kind::Save; return c; }
    }
    return fail("unknown condition '" + std::string(s) + "'");
}

std::vector<Action> parse_actions(std::string_view s, std::string* e) {
    std::vector<Action> out;
    // Split on ';' outside quotes.
    std::vector<std::string> parts;
    {
        std::string cur;
        bool q = false;
        for (std::size_t i = 0; i < s.size(); ++i) {
            const char ch = s[i];
            if (ch == '\\' && q && i + 1 < s.size()) { cur += ch; cur += s[++i]; continue; }
            if (ch == '"') q = !q;
            if (ch == ';' && !q) { parts.push_back(cur); cur.clear(); continue; }
            cur += ch;
        }
        parts.push_back(cur);
    }
    auto fail = [&](const std::string& m) { if (e && e->empty()) *e = m; };
    for (const std::string& part : parts) {
        bool bad_quote = false;
        const std::vector<std::string> t = tokens(part, &bad_quote);
        if (t.empty()) continue;
        if (bad_quote) { fail("unterminated quote in '" + ini_trim(part) + "'"); continue; }
        const std::string w = ini_lower(t[0]);
        Action a;
        if (w == "give") {
            if (t.size() != 2 && t.size() != 3) { fail("expected: give <item> [count]"); continue; }
            const std::optional<int> i = parse_int(t[1]);
            const std::optional<int> n = t.size() == 3 ? parse_int(t[2]) : std::optional<int>(1);
            if (!i || *i < 0 || (*i >= kRetailItems && *i < kNewItemFirst) || *i > kNewItemLast) { fail("give: bad item id"); continue; }
            if (!n || *n < 1 || *n > 99) { fail("give: count must be 1..99"); continue; }
            a.kind = Action::Kind::Give;
            a.item = *i;
            a.count = *n;
        } else if (w == "text") {
            if (t.size() != 2) { fail("expected: text \"...\""); continue; }
            a.kind = Action::Kind::Text;
            a.text = unq(t[1]);
        } else if (w == "flag" || w == "cflag") {
            if (t.size() != 4 || (t[2] != "=" && t[2] != "+=")) { fail("expected: " + w + " <name> = <value>"); continue; }
            const std::optional<int> v = parse_int(t[3]);
            if (!v) { fail(w + ": value is not a number"); continue; }
            a.op = t[2];
            a.value = *v;
            if (w == "flag") {
                const std::optional<int> i = parse_int(t[1]);
                if (!i || *i < 0 || *i >= 0x200) { fail("flag index must be 0..0x1FF"); continue; }
                a.kind = Action::Kind::Flag;
                a.index = *i;
            } else {
                a.kind = Action::Kind::CFlag;
                a.name = ini_lower(unq(t[1]));
            }
        } else {
            fail("unknown action '" + ini_trim(part) + "'");
            continue;
        }
        out.push_back(std::move(a));
    }
    return out;
}

void merge_file(Content& c, const std::string& mod, std::string_view file, std::string_view text) {
    const IniDoc d = IniDoc::parse(text);
    for (const std::string& e : d.errors) err(c, mod, file, 0, e);
    const std::string f = ini_lower(file);
    if (std::find(c.mods.begin(), c.mods.end(), mod) == c.mods.end()) c.mods.push_back(mod);
    if (f.rfind("items", 0) == 0) merge_items(c, mod, file, d);
    else if (f.rfind("chests", 0) == 0) merge_chests(c, mod, file, d);
    else if (f.rfind("pickups", 0) == 0) merge_pickups(c, mod, file, d);
    else if (f.rfind("rules", 0) == 0) merge_rules(c, mod, file, d);
    else if (f.rfind("text", 0) == 0) merge_text(c, mod, file, d);
    else err(c, mod, file, 0, "unknown content file (items / chests / pickups / rules / text *.ini)");
}

int merge_dir(Content& c, const std::string& mod, const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    std::vector<fs::path> files;
    for (const auto& de : fs::directory_iterator(dir, ec))
        if (de.is_regular_file(ec) && ini_lower(de.path().extension().string()) == ".ini") files.push_back(de.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& p : files) {
        std::ifstream in(p, std::ios::binary);
        const std::string t{std::istreambuf_iterator<char>(in), {}};
        merge_file(c, mod, p.filename().string(), t);
    }
    return int(files.size());
}

// ---- applying -----------------------------------------------------------------------------------

void patch_items(const Content& c, ItemTable& t) {
    for (const auto& [id, d] : c.items) {
        if (id < 0) continue;
        if (std::size_t(id) >= t.items.size()) {
            if (id < kNewItemFirst) continue;   // a retail id the file does not have
            t.items.resize(std::size_t(id) + 1);
        }
        Item& it = t.items[std::size_t(id)];
        if (d.name) it.name = *d.name;
        if (d.icon) it.icon = *d.icon;
        if (d.magnitude) it.magnitude = *d.magnitude;
        if (d.description) {
            // INVINFO breaks lines with CR/LF; a mod writes the game's "\n" escape.
            std::string s;
            for (std::size_t i = 0; i < d.description->size(); ++i) {
                if ((*d.description)[i] == '\\' && i + 1 < d.description->size() && (*d.description)[i + 1] == 'n') {
                    s += "\r\n";
                    ++i;
                } else {
                    s += (*d.description)[i];
                }
            }
            it.description = s;
        }
    }
}

const ChestDef* find_chest(const Content& c, std::string_view room, std::string_view script, int item) {
    if (c.chests.empty() || room.empty() || script.empty()) return nullptr;
    std::string sc = upper(script);
    if (!sc.empty() && sc[0] == '@') sc.erase(0, 1);
    const std::string base = upper(room) + "/" + sc;
    if (item >= 0) {
        const auto q = c.chests.find(base + "/" + hex(item));
        if (q != c.chests.end()) return &q->second;
    }
    const auto it = c.chests.find(base);
    return it == c.chests.end() ? nullptr : &it->second;
}

std::string treasure_text(const std::string& original, const std::string& name, int count) {
    std::string t = original;
    const std::size_t a = t.find("<color:0x");
    const std::size_t a2 = a == std::string::npos ? a : t.find('>', a);
    const std::size_t b = a2 == std::string::npos ? a2 : t.find("<color:>", a2);
    if (b == std::string::npos)
        return "Acquired\\n<color:0xfff2ea8c>" + name + "<color:>" + (count > 1 ? " x " + std::to_string(count) : "") + ".";
    t = t.substr(0, a2 + 1) + name + t.substr(b);
    // " x N" after the span
    const std::size_t x = t.find(" x ", a2 + 1 + name.size());
    if (x != std::string::npos) {
        std::size_t e = x + 3;
        while (e < t.size() && std::isdigit((unsigned char)t[e])) ++e;
        if (e > x + 3) t = t.substr(0, x + 3) + std::to_string(count) + t.substr(e);
    } else if (count > 1) {
        const std::size_t close = t.find("<color:>", a2) + 8;
        t.insert(close, " x " + std::to_string(count));
    }
    return t;
}

int patch_script(const Content& c, std::string_view vname, Xso& x) {
    if (c.text.empty()) return 0;
    int n = 0;
    auto apply = [&](const TextDef& td) {
        for (const auto& [i, s] : td.by_index)
            if (i < x.string_count() && x.string(i) != s && x.set_string(i, s)) ++n;
        if (!td.by_text.empty())
            for (int i = 0; i < x.string_count(); ++i) {
                const auto it = td.by_text.find(x.string(i));
                if (it != td.by_text.end() && it->second != x.string(i) && x.set_string(i, it->second)) ++n;
            }
    };
    const auto all = c.text.find("*");
    if (all != c.text.end()) apply(all->second);
    if (!vname.empty()) {
        const auto it = c.text.find(vfs::key(vname));
        if (it != c.text.end()) apply(it->second);
    }
    return n;
}

std::int32_t add_count(std::int32_t cur, int n) { return std::clamp(std::max(cur, 0) + n, 0, 99); }

GrantDecision decide(const Content& c, const GrantQuery& q, std::string_view room, const ItemTable* items,
                     std::vector<Obtained>* got) {
    GrantDecision d;
    if (c.chests.empty()) return d;
    using K = GrantQuery::Kind;
    using A = GrantDecision::Action;
    if (q.kind == K::Store) {
        // Only an item grant: a count cell rising above max(old, 0) (a set from -1 or an add).
        if (q.index < 0 || q.index >= kRetailItems || q.value <= std::max(q.old_value, 0)) return d;
        const ChestDef* cd = find_chest(c, room, q.script, q.index);
        if (!cd) return d;
        if (cd->nothing) { d.action = A::Suppress; return d; }
        if (cd->item < kRetailItems) {
            // The new item's cell with the inventory rule. When it is the cell being written, its
            // pre-op value is the base (the VM's flags already hold the op's result).
            const std::int32_t cur = cd->item == q.index ? q.old_value : (q.flags ? q.flags[cd->item] : 0);
            d.action = A::Replace;
            d.index = cd->item;
            d.value = add_count(cur, cd->count);
            if (got) got->push_back({cd->item, cd->count, false});
        } else {
            d.action = A::Replace;   // no cell: the caller grants the new item (sidecar)
            d.index = -1;
            if (got) got->push_back({cd->item, cd->count, true});
        }
        return d;
    }
    const ChestDef* cd = find_chest(c, room, q.script, q.index);
    if (!cd) return d;
    if (q.kind == K::Icon) {
        if (cd->nothing) { d.action = A::Suppress; return d; }
        d.action = A::Replace;
        d.index = cd->item;
        return d;
    }
    // Window: the box is the script's prompt target, so it is never suppressed (the script would
    // wait for a confirm with nothing on screen); an empty chest says so with no icon.
    d.action = A::Replace;
    if (cd->nothing) {
        d.index = kNoIcon;
        d.text = cd->text.empty() ? std::string("The chest was empty.") : cd->text;   // [H]
        return d;
    }
    d.index = cd->item;
    std::string name = items ? items->name(cd->item) : hex(cd->item);
    d.text = cd->text.empty() ? treasure_text(q.text, name, cd->count) : cd->text;
    return d;
}

bool rule_fires(const RuleDef& r, const events::Event& e, const Sidecar& sc) {
    if (e.kind != r.on.kind) return false;
    if (r.once && sc.flag("rule." + r.name) != 0) return false;
    switch (e.kind) {
        case events::Kind::FlagChanged:
            return e.index == r.on.index && compare(e.new_value, r.on.op, r.on.value) &&
                   !compare(e.old_value, r.on.op, r.on.value);
        case events::Kind::RoomEnter: return upper(e.room) == r.on.arg;
        case events::Kind::ItemObtained: return e.item == r.on.index;
        case events::Kind::ChestOpened: {
            std::string sc2 = upper(e.script);
            if (!sc2.empty() && sc2[0] == '@') sc2.erase(0, 1);
            return upper(e.room) + "/" + sc2 == r.on.arg;
        }
        default: return true;
    }
}

void run_rule(const RuleDef& r, RuleEnv& env) {
    if (r.once && env.sc) env.sc->set_flag("rule." + r.name, 1);
    for (const Action& a : r.act) {
        switch (a.kind) {
            case Action::Kind::Give:
                if (env.give) env.give(a.item, a.count);
                break;
            case Action::Kind::Text:
                if (env.message) env.message(a.text);
                break;
            case Action::Kind::Flag:
                if (env.flags && a.index >= 0 && a.index < 0x200)
                    env.flags[a.index] = a.op == "+=" ? env.flags[a.index] + a.value : a.value;
                break;
            case Action::Kind::CFlag:
                if (env.sc) env.sc->set_flag(a.name, a.op == "+=" ? env.sc->flag(a.name) + a.value : a.value);
                break;
        }
    }
}

}  // namespace cleria::content
