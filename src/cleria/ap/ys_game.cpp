// The Ys Origin side of an Archipelago game — see ys_game.hpp and build/shell_re/AP_GAME.md.
#include "cleria/ap/ys_game.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <sstream>

namespace cleria::ap::ys {

namespace {

constexpr int kGFlagsRel = 0x36B91C;   // retail g_flags, module-relative (ap_import.py G_FLAGS_REL)
constexpr int kItemArrRel = 0x36A654;  // retail item_arr (the Raval cells)
constexpr int kBlessFlag = 217;        // 0xD9: the blessing bitfield (retail 0x76BC80)
constexpr int kWeaponFlag = 148;       // 0x94: the weapon-upgrade progress (EQUIP_EFFECTS.md, WEAPON_LEVEL_UP.XSO)

// The retail warp registry order = CleriaCore's go_warp_point table kWarpRooms (view_game.inl, 0xa6
// GoWarpPoint, table 0x68c190), the same list the retail mod uses [MOD hook_ap.cpp:1661-1664].
constexpr int kWarpScenes[22] = {1000, 1009, 1011, 2000, 2013, 2100, 2012, 3000, 3006, 3015, 3014,
                                 4000, 4104, 4020, 5000, 5010, 5014, 6000, 6010, 6082, 6053, 7000};

// The ore count -> the weapon tier value [MOD hook_ap.cpp:640 kWeaponTier]: 1..5 ore -> Lv2..Lv6.
int ore_tier(int ore) {
    static const int k[5] = {1, 2, 4, 6, 8};
    return ore <= 0 ? 0 : k[std::min(ore, 5) - 1];
}

// Inventory cells 0x40..0x76 are booleans except the three counters 0x57 Roda Fruit, 0x58 Cleria Ore,
// 0x59 Panacea [MOD hook_ap.cpp:925-937, a survey of every shipped script's stores]. Outside that band
// the AP classification decides: filler stacks, progression / useful are set to 1 [MOD 968-974].
bool counted_item_cell(int i) { return i == 0x57 || i == 0x58 || i == 0x59; }
// The elemental skill levels 0xB6..0xB8 run 1..3 (level 4 breaks the MP regen table) [MOD 929-931, 1007-1018].
bool level_cell(int i) { return i >= 0xB6 && i <= 0xB8; }

std::optional<int> hex_offset(const json::Value& v) {
    if (v.is_number()) return int(v.as_int());
    const std::string& s = v.as_string();
    if (s.empty()) return std::nullopt;
    char* end = nullptr;
    const long long x = std::strtoll(s.c_str(), &end, 16);
    if (end == s.c_str()) return std::nullopt;
    return int(x);
}

std::optional<int> flag_of_offset(int off) {
    const int d = off - kGFlagsRel;
    if (d < 0 || d % 4 != 0 || d / 4 >= 0x200) return std::nullopt;
    return d / 4;
}

int to_int(const json::Value& v, int def = 0) {
    if (v.is_number()) return int(v.as_int(def));
    if (v.is_string() && !v.as_string().empty()) return std::atoi(v.as_string().c_str());
    return def;
}

std::vector<int> ints(const json::Value& a) {
    std::vector<int> v;
    for (const json::Value& x : a.as_array()) v.push_back(to_int(x, -1));
    return v;
}

std::map<int, int> int_map(const json::Value& o) {
    std::map<int, int> m;
    for (const auto& [k, v] : o.as_object()) m[std::atoi(k.c_str())] = to_int(v);
    return m;
}

const char* char_key(int character) {   // g_flags[150] -> the apworld's ladder key
    return character == 2 ? "hugo" : character == 3 ? "toal" : "yunica";
}

}  // namespace

int scene_number(const std::string& s) {
    if (s.size() < 3 || (s[0] != 'S' && s[0] != 's') || s[1] != '_') return 0;
    int n = 0;
    bool any = false;
    for (std::size_t i = 2; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
        n = n * 10 + (s[i] - '0');
        any = true;
    }
    return any ? n : 0;
}

bool convert_detect(const json::Value& d, Reg& out) {
    const std::string& m = d["method"].as_string();
    if (m == "flag") {
        if (d.contains("flag")) { out.detect = Detect::Flag; out.flag = to_int(d["flag"], -1); return out.flag >= 0; }
        if (d.contains("index")) { out.detect = Detect::ItemArr; out.index = to_int(d["index"], -1); return out.index >= 0; }
        const auto off = hex_offset(d["offset"]);
        if (!off) return false;
        if (const auto f = flag_of_offset(*off)) { out.detect = Detect::Flag; out.flag = *f; return true; }
        const int s = *off - kItemArrRel;
        if (s >= 0 && s % 4 == 0 && s / 4 < 54) { out.detect = Detect::ItemArr; out.index = s / 4; return true; }
        return false;
    }
    if (m == "item_arr") { out.detect = Detect::ItemArr; out.index = to_int(d["index"], -1); return out.index >= 0; }
    if (m == "scene") {
        const json::Value& s = d["scene"];
        out.scene = s.is_number() ? int(s.as_int()) : scene_number(s.as_string());
        out.detect = Detect::Scene;
        return out.scene > 0;
    }
    if (m == "bit") {
        int f = to_int(d["flag"], -1);
        if (f < 0) if (const auto off = hex_offset(d["offset"])) if (const auto ff = flag_of_offset(*off)) f = *ff;
        out.detect = Detect::Bit;
        out.flag = f;
        out.bit = to_int(d["bit"], -1);
        return f >= 0 && out.bit >= 0 && out.bit < 32;
    }
    if (m == "floor") {
        out.detect = Detect::Floor;
        out.floor = to_int(d["floor"], 0);
        return out.floor > 0;
    }
    return false;
}

int warp_index_of_scene(int scene) {
    for (int i = 0; i < 22; ++i)
        if (kWarpScenes[i] == scene) return i;
    return -1;
}
int scene_of_warp_index(int i) { return i >= 0 && i < 22 ? kWarpScenes[i] : 0; }

SlotOptions parse_slot_options(const json::Value& sd) {
    SlotOptions o;
    o.character = sd.contains("character") ? to_int(sd["character"], -1) : -1;
    o.goal = to_int(sd["goal"]);
    o.death_link = sd["death_link"].as_bool(to_int(sd["death_link"]) != 0);
    for (int i : ints(sd["suppress_items"])) if (i >= 0 && i < 0x200) o.suppress_items.insert(i);
    for (int i : ints(sd["suppress_give_ids"])) o.suppress_give_ids.insert(i);
    for (const auto& [k, v] : sd["progressive_gear"].as_object()) o.progressive_gear[k] = ints(v);
    for (const auto& [k, v] : sd["progressive_skills"].as_object())
        o.progressive_skills[k] = {to_int(v["artifact"], -1), to_int(v["power"], -1), to_int(v["level_cell"], -1)};
    for (const auto& [k, v] : sd["progressive_blessings"].as_object()) o.progressive_blessings[k] = ints(v);
    o.statue_warp_locks = sd["statue_warp_locks"].as_bool(false);
    for (const auto& [k, v] : sd["statue_unlocks"].as_object()) {
        const int scene = to_int(v["scene"]);
        const auto off = hex_offset(v["flag"]);
        const auto f = off ? flag_of_offset(*off) : std::nullopt;
        if (scene > 0 && f) o.statue_unlock_flag[scene] = *f;
    }
    o.start_statue_scene = to_int(sd["start_statue_scene"]);
    o.start_items = ints(sd["start_items"]);
    o.start_level = to_int(sd["start_level"]);
    o.start_weapon = to_int(sd["start_weapon"]);
    o.level_scaling = to_int(sd["level_scaling"]);
    o.level_margin = to_int(sd["level_margin"]);
    o.exp_base_mult = std::max(1, to_int(sd["exp_base_mult"], 1));
    o.exp_catchup_mult = std::max(1, to_int(sd["exp_catchup_mult"], 1));
    o.exp_catchup_margin = to_int(sd["exp_catchup_margin"]);
    o.scene_levels = int_map(sd["scene_levels"]);
    o.scene_floors = int_map(sd["scene_floors"]);
    for (const auto& [k, v] : sd["scene_names"].as_object()) o.scene_names[std::atoi(k.c_str())] = v.as_string();
    o.blessing_items = sd["blessing_items"].as_bool(false);
    // What CleriaCore does not (yet) do with an option the seed turned on (AP_GAME.md §6).
    if (sd["random_start"].as_bool(false)) o.unsupported.push_back("Random start (the start-statue warp)");
    if (o.blessing_items) o.unsupported.push_back("Blessing items (the shop purchase is not intercepted)");
    if (sd["blessing_costs"].size() > 0) o.unsupported.push_back("Shuffled blessing prices");
    if (to_int(sd["blessing_shop_unlock"]) != 0) o.unsupported.push_back("Blessing shop pacing (one per floor)");
    return o;
}

// ---- the save state --------------------------------------------------------------------------------

std::string ids_to_text(const std::set<std::int64_t>& ids) {
    std::string s;
    for (auto it = ids.begin(); it != ids.end();) {
        std::int64_t a = *it, b = a;
        auto nx = std::next(it);
        while (nx != ids.end() && *nx == b + 1) { b = *nx; ++nx; }
        if (!s.empty()) s += ',';
        s += std::to_string(a);
        if (b != a) s += '-' + std::to_string(b);
        it = nx;
    }
    return s;
}

std::set<std::int64_t> ids_from_text(const std::string& s) {
    std::set<std::int64_t> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        if (part.empty()) continue;
        const std::size_t dash = part.find('-', 1);
        const std::int64_t a = std::atoll(part.c_str());
        const std::int64_t b = dash == std::string::npos ? a : std::atoll(part.c_str() + dash + 1);
        for (std::int64_t i = a; i <= b && i - a < 100000; ++i) out.insert(i);
    }
    return out;
}

std::map<std::string, std::string> SaveState::to_kv() const {
    std::map<std::string, std::string> kv;
    if (empty()) return kv;
    kv["seed"] = seed;
    kv["slot"] = std::to_string(slot);
    kv["slot_name"] = slot_name;
    kv["applied"] = std::to_string(applied);
    if (!checks.empty()) kv["checks"] = ids_to_text(checks);
    if (!claimed.empty()) kv["claimed"] = ids_to_text(claimed);
    if (goal_sent) kv["goal"] = "1";
    if (started) kv["started"] = "1";
    if (saw_gameplay) kv["gameplay"] = "1";
    if (ore) kv["ore"] = std::to_string(ore);
    std::string p;
    for (const auto& [k, n] : prog) p += (p.empty() ? "" : ";") + k + "=" + std::to_string(n);
    if (!p.empty()) kv["prog"] = p;
    std::set<std::int64_t> st(statues.begin(), statues.end());
    if (!st.empty()) kv["statues"] = ids_to_text(st);
    return kv;
}

SaveState SaveState::from_kv(const std::map<std::string, std::string>& kv) {
    SaveState s;
    auto get = [&](const char* k) -> std::string {
        const auto it = kv.find(k);
        return it == kv.end() ? std::string() : it->second;
    };
    s.seed = get("seed");
    s.slot = std::atoi(get("slot").c_str());
    s.slot_name = get("slot_name");
    s.applied = std::max(0, std::atoi(get("applied").c_str()));
    s.checks = ids_from_text(get("checks"));
    s.claimed = ids_from_text(get("claimed"));
    s.goal_sent = get("goal") == "1";
    s.started = get("started") == "1";
    s.saw_gameplay = get("gameplay") == "1";
    s.ore = std::max(0, std::atoi(get("ore").c_str()));
    std::stringstream ss(get("prog"));
    std::string part;
    while (std::getline(ss, part, ';')) {
        const std::size_t eq = part.rfind('=');
        if (eq != std::string::npos && eq > 0) s.prog[part.substr(0, eq)] = std::atoi(part.c_str() + eq + 1);
    }
    for (std::int64_t v : ids_from_text(get("statues"))) s.statues.insert(int(v));
    return s;
}

// ---- the logic -------------------------------------------------------------------------------------

void Logic::configure(const json::Value& sd, const Table& table) {
    table_ = &table;
    opt_ = parse_slot_options(sd);
    regs_.clear();
    by_id_.clear();
    by_scene_.clear();
    by_floor_.clear();
    auto add = [&](Reg r) {
        if (by_id_.count(r.id)) return;
        by_id_[r.id] = regs_.size();
        if (r.detect == Detect::Scene) by_scene_.emplace(r.scene, regs_.size());
        if (r.detect == Detect::Floor) by_floor_.emplace(r.floor, regs_.size());
        regs_.push_back(std::move(r));
    };
    auto from_table = [&](const Location& l, Reg& r) {
        r.detect = l.detect;
        r.flag = l.flag; r.index = l.index; r.bit = l.bit;
        r.scene = l.detect_scene; r.floor = l.detect_floor;
        return l.detect != Detect::Unmapped;
    };
    const json::Value& sig = sd["location_signals"];
    if (sig.size() > 0) {
        // The ACTIVE locations of this slot [MOD hook_ap.cpp on_slot_connected]; their detect as the
        // seed published it (statues switch to scene detection under warp locks).
        for (const auto& [name, idv] : sig.as_object()) {
            Reg r;
            r.id = idv.as_int();
            r.name = name;
            bool ok = false;
            if (const json::Value* d = sd["location_detect"].find(name)) ok = convert_detect(*d, r);
            if (!ok)
                if (const Location* l = table.location(r.id)) ok = from_table(*l, r);
            if (ok) add(std::move(r));
        }
    } else {
        for (const Location& l : table.locations) {   // no slot_data: every location of the table
            Reg r;
            r.id = l.id;
            r.name = l.name;
            if (from_table(l, r)) add(std::move(r));
        }
    }
    if (opt_.scene_floors.empty()) opt_.scene_floors = table.scene_floors;
    if (opt_.scene_names.empty()) opt_.scene_names = table.scene_names;
    configured_ = true;
}

const Reg* Logic::reg(std::int64_t id) const {
    const auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &regs_[it->second];
}

std::vector<std::int64_t> Logic::fire(std::int64_t id) {
    st.claimed.erase(id);
    if (!st.checks.insert(id).second) return {};
    return {id};
}

std::vector<std::int64_t> Logic::sweep(const GameIo& io) {
    std::vector<std::int64_t> out;
    if (!io.flags) return out;
    for (const Reg& r : regs_) {
        if (st.checks.count(r.id) || st.claimed.count(r.id)) continue;
        bool on = false;
        switch (r.detect) {
            case Detect::Flag: on = r.flag >= 0 && r.flag < 0x200 && io.flags[r.flag] >= 1; break;
            case Detect::ItemArr: on = io.item_arr && r.index >= 0 && r.index < 54 && io.item_arr[r.index] >= 1; break;
            case Detect::Bit:
                on = r.flag >= 0 && r.flag < 0x200 && ((std::uint32_t(io.flags[r.flag]) >> r.bit) & 1u) != 0;
                break;
            default: break;
        }
        if (on) {
            st.checks.insert(r.id);
            out.push_back(r.id);
        }
    }
    return out;
}

std::vector<std::int64_t> Logic::on_scene(int sc) {
    std::vector<std::int64_t> out;
    scene = sc;
    if (sc >= 1000 && sc <= 6999) st.saw_gameplay = true;   // the 7002 goal guard [MOD hook_ap.cpp:461-469]
    if (const auto lv = opt_.scene_levels.find(sc); lv != opt_.scene_levels.end()) expected_hi = std::max(expected_hi, lv->second);
    for (auto [a, b] = by_scene_.equal_range(sc); a != b; ++a)
        for (std::int64_t id : fire(regs_[a->second].id)) out.push_back(id);
    if (const auto f = opt_.scene_floors.find(sc); f != opt_.scene_floors.end())
        for (auto [a, b] = by_floor_.equal_range(f->second); a != b; ++a)
            for (std::int64_t id : fire(regs_[a->second].id)) out.push_back(id);
    return out;
}

std::vector<std::int64_t> Logic::on_store(int index, std::int32_t old_value, std::int32_t value) {
    std::vector<std::int64_t> out;
    if (index < 0 || index >= 0x200) return out;
    for (const Reg& r : regs_) {
        if (r.flag != index) continue;
        // A store of 0 is not a check [ARCHIPELAGO.md §7.3.4: the mod fired on any store].
        if (r.detect == Detect::Flag && value >= 1 && old_value < 1)
            for (std::int64_t id : fire(r.id)) out.push_back(id);
        if (r.detect == Detect::Bit && (((std::uint32_t(value) & ~std::uint32_t(old_value)) >> r.bit) & 1u))
            for (std::int64_t id : fire(r.id)) out.push_back(id);
    }
    return out;
}

bool Logic::suppress_store(int index, std::int32_t old_value, std::int32_t value) const {
    if (!configured_ || value <= std::max(old_value, 0)) return false;   // only a grant (a rise) is swallowed
    if (opt_.suppress_items.count(index)) return true;
    if (opt_.statue_warp_locks)   // a locked statue stays dark: its purify flag [MOD hook_ap.cpp:583-597]
        for (const auto& [sc, flag] : opt_.statue_unlock_flag)
            if (flag == index && sc != opt_.start_statue_scene && !st.statues.count(sc)) return true;
    return false;
}

void Logic::claim_cell(int index) {
    for (const Reg& r : regs_)
        if (r.detect == Detect::Flag && r.flag == index && !st.checks.count(r.id)) st.claimed.insert(r.id);
}
void Logic::claim_bit(int bit) {
    for (const Reg& r : regs_)
        if (r.detect == Detect::Bit && r.flag == kBlessFlag && r.bit == bit && !st.checks.count(r.id)) st.claimed.insert(r.id);
}

// Set (or add to) a g_flags cell [MOD hook_ap.cpp:952-966 ap_give]: a key item is SET to 1 (the
// scripts test it with ==, so a 2 kills its gate), a counter adds to max(cur, 0).
bool Logic::give(int index, bool stack, GameIo& io, std::string& log) {
    if (!io.flags || index < 0 || index >= 0x200) return false;
    std::int32_t& c = io.flags[index];
    const std::int32_t was = c;
    if (level_cell(index)) c = std::min(3, std::max(c, 0) + 1);
    else if (stack) c = std::max(c, 0) + 1;
    else c = 1;
    claim_cell(index);
    char b[64];
    std::snprintf(b, sizeof b, "%sg_flags[0x%X] %d -> %d", log.empty() ? "" : ", ", index, int(was), int(c));
    log += b;
    return c != was;
}

Logic::Grant Logic::grant(std::int64_t item, int flags, GameIo& io) {
    Grant g;
    const Item* it = table_ ? table_->item(item) : nullptr;
    if (!it || !io.flags) { g.known = false; g.text = "unknown item " + std::to_string(item) + ": skipped"; return g; }
    std::string log;
    switch (it->kind) {
        case ItemKind::Item: {
            const int i = it->flag;
            const bool stack = (i >= 0x40 && i <= 0x76) ? counted_item_cell(i) : (flags & 3) == 0;
            give(i, stack, io, log);
            if (it->companion_flag >= 0) give(it->companion_flag, false, io, log);   // the power / bracelet [MOD 1855-1878]
            break;
        }
        case ItemKind::Sp:   // the live wallet, not g_flags[0xD8] (the zenny shadow) [MOD 1784-1790]
            if (io.add_sp) io.add_sp(it->amount);
            log = "SP +" + std::to_string(it->amount);
            break;
        case ItemKind::Trap:
            g.trap = it->name;
            log = "trap " + it->name;
            break;
        case ItemKind::StatueUnlock: {   // [MOD 1804-1813]
            st.statues.insert(it->scene);
            const int w = warp_index_of_scene(it->scene);
            if (io.warp_on && w >= 0) io.warp_on[w] = 1;
            if (it->flag >= 0 && it->flag < 0x200) { io.flags[it->flag] = 1; claim_cell(it->flag); }
            log = "statue S_" + std::to_string(it->scene) + " unlocked (warp " + std::to_string(w) + ")";
            break;
        }
        case ItemKind::ProgressiveGear: {   // the first tier not owned [MOD 1791-1803]
            auto lad = opt_.progressive_gear.find(it->name);
            std::vector<int> ladder = lad != opt_.progressive_gear.end() ? lad->second : std::vector<int>{};
            if (ladder.empty())
                if (const auto l = it->ladder.find(char_key(io.character)); l != it->ladder.end()) ladder = l->second;
            bool done = false;
            for (int cell : ladder)
                if (cell >= 0 && cell < 0x200 && io.flags[cell] < 1) { give(cell, false, io, log); done = true; break; }
            if (!done) log = "every tier already owned";
            break;
        }
        case ItemKind::ProgressiveSkill: {   // [MOD 1199-1220], the count kept in the save (not per connect, §7.3.2)
            SlotOptions::ProgSkill ps{it->artifact, it->power, it->level_cell};
            if (const auto p = opt_.progressive_skills.find(it->name); p != opt_.progressive_skills.end()) ps = p->second;
            const int n = ++st.prog[it->name];
            if (n == 1) {
                give(ps.artifact, false, io, log);
                give(ps.power, false, io, log);
            } else if (ps.level_cell >= 0) {
                give(ps.level_cell, true, io, log);
            }
            break;
        }
        case ItemKind::ProgressiveBlessing: {   // the next unset bit of the chain [MOD 1159-1172]
            std::vector<int> bits = it->bits;
            if (const auto p = opt_.progressive_blessings.find(it->name); p != opt_.progressive_blessings.end()) bits = p->second;
            log = "every tier already granted";
            for (int b : bits) {
                if (b < 0 || b > 31 || ((std::uint32_t(io.flags[kBlessFlag]) >> b) & 1u)) continue;
                io.flags[kBlessFlag] |= std::int32_t(1u << b);
                claim_bit(b);
                log = "blessing bit " + std::to_string(b);
                break;
            }
            break;
        }
        case ItemKind::CleriaOre: {   // the weapon tier, not the item cell [MOD 629-641, 1814-1820]
            ++st.ore;
            const int tier = ore_tier(st.ore);
            if (io.flags[kWeaponFlag] < tier && io.set_weapon_tier) io.set_weapon_tier(tier);
            log = "Cleria Ore #" + std::to_string(st.ore) + ": weapon tier " + std::to_string(tier);
            break;
        }
        case ItemKind::Gem:   // the skill level + 1, capped at 3 (also with progressive_skills off, §7.3.1)
            give(it->level_cell, true, io, log);
            break;
        case ItemKind::Blessing:
            if (it->bit >= 0 && it->bit < 32) {
                io.flags[kBlessFlag] |= std::int32_t(1u << it->bit);
                claim_bit(it->bit);
                log = "blessing bit " + std::to_string(it->bit);
            }
            break;
        case ItemKind::Unknown:
            g.known = false;
            log = "no grant rule: skipped";
            break;
    }
    if (io.refresh && it->kind != ItemKind::Trap) io.refresh();
    g.text = it->name + ": " + log;
    return g;
}

std::string Logic::start_loadout(GameIo& io) {
    if (st.started || !configured_ || !io.flags) return {};
    st.started = true;
    std::string log;
    for (int i : opt_.start_items)
        if (i >= 0 && i < 0x200 && io.flags[i] < 1) give(i, false, io, log);
    if (opt_.start_weapon > io.flags[kWeaponFlag] && io.set_weapon_tier) {
        io.set_weapon_tier(opt_.start_weapon);
        log += (log.empty() ? "" : ", ") + std::string("weapon tier ") + std::to_string(opt_.start_weapon);
    }
    if (opt_.start_level > 1 && io.level && io.set_level && io.level() < opt_.start_level) {
        io.set_level(opt_.start_level);
        log += (log.empty() ? "" : ", ") + std::string("level ") + std::to_string(opt_.start_level);
    }
    if (io.refresh) io.refresh();
    return log.empty() ? "nothing to give" : log;
}

void Logic::enforce(GameIo& io) {
    if (!configured_ || !io.flags) return;
    const int tier = ore_tier(st.ore);   // re-enforced like the mod's [MOD 2571-2590]
    if (tier > 0 && io.flags[kWeaponFlag] < tier && io.set_weapon_tier) io.set_weapon_tier(tier);
    if (!opt_.statue_warp_locks || !io.warp_on) return;
    for (const auto& [sc, flag] : opt_.statue_unlock_flag) {   // [MOD hook_ap.cpp:2510-2530]
        const int w = warp_index_of_scene(sc);
        if (w < 0) continue;
        if (st.statues.count(sc)) {
            io.warp_on[w] = 1;
            if (io.flags[flag] != 1) io.flags[flag] = 1;
        } else if (sc != opt_.start_statue_scene) {
            io.warp_on[w] = 0;
        }
    }
}

bool Logic::goal_reached(const std::int32_t* f, int sc) const {
    if (!f) return false;
    const bool final_done = ((f[150] == 1 || f[150] == 2) && f[226] >= 1) || (f[150] == 3 && f[227] >= 2) ||
                            (sc == 7002 && st.saw_gameplay);
    if (!final_done) return false;
    if (opt_.goal == 1)
        for (int i = 220; i <= 225; ++i)
            if (f[i] < 1) return false;
    return true;
}

float Logic::exp_factor(int level) const {
    if (opt_.level_scaling != 2 && opt_.level_scaling != 3) return 1.0f;
    const bool catching_up = expected_hi > 0 && level <= expected_hi + opt_.exp_catchup_margin;
    return float(catching_up ? opt_.exp_catchup_mult : opt_.exp_base_mult);
}

int Logic::level_floor(int level) const {
    if (opt_.level_scaling != 1 && opt_.level_scaling != 3) return 0;
    const auto it = opt_.scene_levels.find(scene);
    if (it == opt_.scene_levels.end() || it->second <= 0) return 0;
    const int target = std::min(60, it->second - opt_.level_margin);
    return target > level && target > 1 ? target : 0;
}

const char* item_class(int flags) {
    if (flags & kTrap) return "trap";
    if (flags & kProgression) return "progression";
    if (flags & kUseful) return "useful";
    return "filler";
}

}  // namespace cleria::ap::ys
