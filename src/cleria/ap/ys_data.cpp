// The Ys Origin AP table — see ys_data.hpp.
#include "cleria/ap/ys_data.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace cleria::ap::ys {

namespace {

std::string norm_path(std::string s) {
    for (char& c : s) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

std::vector<std::string> strings(const json::Value& a) {
    std::vector<std::string> v;
    for (const json::Value& s : a.as_array()) v.push_back(s.as_string());
    return v;
}

int to_int(const json::Value& v, int def = -1) {
    return v.is_number() ? static_cast<int>(v.as_int(def)) : def;
}

ItemKind kind_of(const std::string& k) {
    if (k == "item") return ItemKind::Item;
    if (k == "sp") return ItemKind::Sp;
    if (k == "trap") return ItemKind::Trap;
    if (k == "statue_unlock") return ItemKind::StatueUnlock;
    if (k == "progressive_gear") return ItemKind::ProgressiveGear;
    if (k == "progressive_skill") return ItemKind::ProgressiveSkill;
    if (k == "progressive_blessing") return ItemKind::ProgressiveBlessing;
    if (k == "cleria_ore") return ItemKind::CleriaOre;
    if (k == "gem") return ItemKind::Gem;
    if (k == "blessing") return ItemKind::Blessing;
    return ItemKind::Unknown;
}

}  // namespace

const Location* Table::location(std::int64_t id) const {
    auto it = loc_index_.find(id);
    return it == loc_index_.end() ? nullptr : &locations[it->second];
}

const Item* Table::item(std::int64_t id) const {
    auto it = item_index_.find(id);
    return it == item_index_.end() ? nullptr : &items[it->second];
}

const Location* Table::chest_by_script(const std::string& script) const {
    const std::string want = norm_path(script);
    for (const Location& l : locations)
        if (!l.script.empty() && norm_path(l.script) == want) return &l;
    return nullptr;
}

std::vector<const Location*> Table::by_flag(int flag) const {
    std::vector<const Location*> v;
    for (const Location& l : locations)
        if (l.detect == Detect::Flag && l.flag == flag) v.push_back(&l);
    return v;
}

std::optional<Table> from_json(const json::Value& doc, std::string* err) {
    const auto fail = [&](const std::string& m) -> std::optional<Table> {
        if (err) *err = m;
        return std::nullopt;
    };
    if (doc["format"].as_int() != 1) return fail("unsupported table format");
    if (doc["game"].as_string() != "Ys Origin") return fail("not a Ys Origin table");
    Table t;
    t.location_base = doc["location_base_id"].as_int();
    t.item_base = doc["item_base_id"].as_int();
    t.commit = doc["source"]["commit"].as_string();
    for (const auto& [k, v] : doc["scene_names"].as_object()) t.scene_names[std::atoi(k.c_str())] = v.as_string();
    for (const auto& [k, v] : doc["scene_floors"].as_object()) t.scene_floors[std::atoi(k.c_str())] = to_int(v, 0);

    for (const json::Value& j : doc["locations"].as_array()) {
        Location l;
        l.id = j["id"].as_int();
        l.name = j["name"].as_string();
        l.key = j["key"].as_string();
        l.type = j["type"].as_string();
        l.zone = j["zone"].as_string();
        l.floor = j["floor"].as_string();
        l.room = j["room"].as_string();
        l.scene = to_int(j["scene"], 0);
        l.excluded = j["excluded"].as_bool();
        const json::Value& d = j["detect"];
        const std::string& m = d["method"].as_string();
        if (m == "flag") { l.detect = Detect::Flag; l.flag = to_int(d["flag"]); }
        else if (m == "item_arr") { l.detect = Detect::ItemArr; l.index = to_int(d["index"]); }
        else if (m == "scene") { l.detect = Detect::Scene; l.detect_scene = to_int(d["scene"], 0); }
        else if (m == "bit") { l.detect = Detect::Bit; l.flag = to_int(d["flag"]); l.bit = to_int(d["bit"]); }
        else if (m == "floor") { l.detect = Detect::Floor; l.detect_floor = to_int(d["floor"], 0); l.flag = to_int(d["flag"]); }
        l.statue_scene = to_int(j["statue_scene"], 0);
        if (const json::Value* c = j.find("chest")) {
            l.box_flag = to_int((*c)["box_flag"]);
            l.script = (*c)["script"].as_string();
        }
        l.chars = strings(j["chars"]);
        for (const json::Value& v : j["vanilla"].as_array())
            l.vanilla.push_back({to_int(v["item"]), v["name"].as_string(), v["class"].as_string()});
        if (!l.id) return fail("a location without an id");
        t.locations.push_back(std::move(l));
    }
    for (const json::Value& j : doc["items"].as_array()) {
        Item it;
        it.id = j["id"].as_int();
        it.name = j["name"].as_string();
        it.cls = j["class"].as_string();
        it.kind = kind_of(j["kind"].as_string());
        it.flag = to_int(j["flag"]);
        it.companion_flag = to_int(j["companion_flag"]);
        it.amount = to_int(j["amount"], 0);
        it.scene = to_int(j["scene"], 0);
        it.bit = to_int(j["bit"]);
        for (const json::Value& b : j["bits"].as_array()) it.bits.push_back(to_int(b));
        it.index = to_int(j["index"]);
        it.level_cell = to_int(j["level_cell"]);
        it.artifact = to_int(j["artifact"]);
        it.power = to_int(j["power"]);
        for (const auto& [ch, lad] : j["ladder"].as_object())
            for (const json::Value& x : lad.as_array()) it.ladder[ch].push_back(to_int(x));
        it.chars = strings(j["chars"]);
        it.goal_item = j["goal_item"].as_bool();
        if (!it.id) return fail("an item without an id");
        t.items.push_back(std::move(it));
    }
    std::sort(t.locations.begin(), t.locations.end(), [](const Location& a, const Location& b) { return a.id < b.id; });
    std::sort(t.items.begin(), t.items.end(), [](const Item& a, const Item& b) { return a.id < b.id; });
    for (std::size_t i = 0; i < t.locations.size(); ++i)
        if (!t.loc_index_.emplace(t.locations[i].id, i).second) return fail("duplicate location id");
    for (std::size_t i = 0; i < t.items.size(); ++i)
        if (!t.item_index_.emplace(t.items[i].id, i).second) return fail("duplicate item id");
    return t;
}

std::optional<Table> load(const std::string& path, std::string* err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open " + path;
        return std::nullopt;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    json::ParseError pe;
    auto doc = json::parse(ss.str(), &pe);
    if (!doc) {
        if (err) *err = path + ": " + pe.message;
        return std::nullopt;
    }
    return from_json(*doc, err);
}

}  // namespace cleria::ap::ys
