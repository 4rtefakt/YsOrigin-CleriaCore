// The Archipelago protocol state machine — see client.hpp.
#include "cleria/ap/client.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

namespace cleria::ap {

namespace fs = std::filesystem;

// UTF-8 std::string <-> path without the C++20-deprecated u8path.
static fs::path u8p(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }
static std::string u8s(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// ---- helpers -------------------------------------------------------------------------------

NetworkItem read_item(const json::Value& v) {
    NetworkItem it;
    it.item = v["item"].as_int();
    it.location = v["location"].as_int();
    it.player = static_cast<int>(v["player"].as_int());
    it.flags = static_cast<int>(v["flags"].as_int());
    return it;
}

static json::Value id_list(const std::vector<std::int64_t>& ids) {
    json::Value a = json::Value::array();
    for (auto id : ids) a.push_back(id);
    return a;
}

static json::Value str_list(const std::vector<std::string>& s) {
    json::Value a = json::Value::array();
    for (const auto& x : s) a.push_back(x);
    return a;
}

std::string make_uuid() {
    std::random_device rd;
    std::mt19937_64 g((static_cast<std::uint64_t>(rd()) << 32) ^ rd());
    std::uint64_t hi = g(), lo = g();
    hi = (hi & ~0xF000ull) | 0x4000ull;                      // version 4
    lo = (lo & ~(0xC000ull << 48)) | (0x8000ull << 48);      // variant 10xx
    char buf[40];
    std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx",
                  static_cast<unsigned>(hi >> 32), static_cast<unsigned>((hi >> 16) & 0xFFFF),
                  static_cast<unsigned>(hi & 0xFFFF), static_cast<unsigned>(lo >> 48),
                  static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFull));
    return buf;
}

// ---- data package cache ----------------------------------------------------------------------

std::string FileDataPackageStore::path_for(const std::string& game, const std::string& checksum) const {
    // Readable but collision-free: the sanitised name plus an FNV-1a hash of the exact name.
    std::uint32_t h = 2166136261u;
    for (unsigned char c : game) { h ^= c; h *= 16777619u; }
    std::string name;
    for (unsigned char c : game)
        name += (std::isalnum(c) && c < 0x80) || c == '-' || c == '.' ? static_cast<char>(c) : '_';
    std::string sum;
    for (unsigned char c : checksum) sum += std::isalnum(c) && c < 0x80 ? static_cast<char>(c) : '_';
    char hx[16];
    std::snprintf(hx, sizeof hx, "%08x", h);
    return u8s(u8p(dir_) / u8p(name + "_" + hx + "_" + sum + ".json"));
}

std::optional<json::Value> FileDataPackageStore::load(const std::string& game, const std::string& checksum) {
    std::ifstream f(u8p(path_for(game, checksum)), std::ios::binary);
    if (!f) return std::nullopt;
    std::stringstream ss;
    ss << f.rdbuf();
    auto v = json::parse(ss.str());
    if (!v || !v->is_object() || (*v)["checksum"].as_string() != checksum) return std::nullopt;
    return v;
}

void FileDataPackageStore::store(const std::string& game, const std::string& checksum, const json::Value& game_data) {
    std::error_code ec;
    fs::create_directories(u8p(dir_), ec);
    const fs::path p = u8p(path_for(game, checksum));
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return;
        f << json::write(game_data);
        if (!f) return;
    }
    fs::rename(tmp, p, ec);
    if (ec) fs::remove(tmp, ec);
}

// ---- Client --------------------------------------------------------------------------------

Client::Client(Config cfg, DataPackageStore* store) : cfg_(std::move(cfg)), store_(store) {}

void Client::queue(json::Value cmd) {
    if (state_ == State::Connected) out_.push_back("[" + json::write(cmd) + "]");
    else held_.push_back(std::move(cmd));
}

void Client::send_packet(json::Value cmd) { queue(std::move(cmd)); }

std::vector<std::string> Client::take_outgoing() {
    std::vector<std::string> o;
    o.swap(out_);
    return o;
}

std::vector<Event> Client::take_events() {
    std::vector<Event> e;
    e.swap(events_);
    return e;
}

void Client::on_open() {
    state_ = State::AwaitRoomInfo;
    refused_.clear();
}

void Client::on_close() {
    state_ = State::Disconnected;
    out_.clear();     // frames for the dead socket; the session state is re-sent after the next login
}

void Client::on_message(std::string_view frame) {
    json::ParseOptions opt;
    opt.allow_nan = true;       // the server is Python; its json emits NaN / Infinity
    json::ParseError err;
    auto v = json::parse(frame, &err, opt);
    if (!v) {
        Event e{Event::Kind::ProtocolError};
        e.text = "server sent invalid JSON: " + err.message;
        event(std::move(e));
        return;
    }
    if (v->is_array()) {
        for (const json::Value& cmd : v->as_array()) handle(cmd);
    } else if (v->is_object()) {
        handle(*v);
    } else {
        Event e{Event::Kind::ProtocolError};
        e.text = "server frame is not a list of commands";
        event(std::move(e));
    }
}

void Client::handle(const json::Value& p) {
    const std::string& cmd = p["cmd"].as_string();
    if (cmd == "RoomInfo") on_room_info(p);
    else if (cmd == "DataPackage") on_data_package(p);
    else if (cmd == "Connected") on_connected(p);
    else if (cmd == "ConnectionRefused") on_refused(p);
    else if (cmd == "ReceivedItems") on_received_items(p);
    else if (cmd == "LocationInfo") on_location_info(p);
    else if (cmd == "RoomUpdate") on_room_update(p);
    else if (cmd == "Bounced") on_bounced(p);
    else if (cmd == "PrintJSON") {
        Event e{Event::Kind::Print};
        e.line = render_print_json(p, *this);
        e.data = p;
        event(std::move(e));
    } else if (cmd == "Retrieved" || cmd == "SetReply") {
        Event e{cmd == "Retrieved" ? Event::Kind::Retrieved : Event::Kind::SetReply};
        e.data = p;
        event(std::move(e));
    } else if (cmd == "InvalidPacket") {
        Event e{Event::Kind::InvalidPacket};
        e.text = p["type"].as_string() + " problem in " + p["original_cmd"].as_string("?") + ": " +
                 p["text"].as_string();
        e.data = p;
        event(std::move(e));
    } else {
        Event e{Event::Kind::ProtocolError};
        e.text = cmd.empty() ? "server command without a \"cmd\"" : "unknown server command " + cmd;
        e.data = p;
        event(std::move(e));
    }
}

void Client::on_room_info(const json::Value& p) {
    room_info_ = p;
    event(Event{Event::Kind::RoomInfo, 0, {}, {}, {}, {}, {}, {}, 0, p});
    const std::string seed = p["seed_name"].as_string();
    if (!cfg_.expected_seed.empty() && seed != cfg_.expected_seed) {
        state_ = State::Refused;
        refused_ = {"SeedMismatch"};
        Event e{Event::Kind::SeedMismatch};
        e.text = seed;
        e.errors = refused_;
        event(std::move(e));
        return;
    }
    // Which games' names are missing or stale (the reference client's prepare_data_package):
    // every game in the room plus "Archipelago", when the server lists a checksum for it.
    std::vector<std::string> games;
    for (const json::Value& g : p["games"].as_array()) games.push_back(g.as_string());
    if (std::find(games.begin(), games.end(), "Archipelago") == games.end()) games.push_back("Archipelago");
    const json::Value& sums = p["datapackage_checksums"];
    std::vector<std::string> needed;
    for (const std::string& g : games) {
        const json::Value* sum = sums.find(g);
        if (!sum) continue;
        const std::string& remote = sum->as_string();
        if (remote.empty()) { needed.push_back(g); continue; }
        auto it = games_.find(g);
        if (it != games_.end() && it->second.checksum == remote) continue;
        if (store_) {
            if (auto cached = store_->load(g, remote)) {
                add_game_names(g, *cached);
                continue;
            }
        }
        needed.push_back(g);
    }
    if (!needed.empty()) {
        json::Value req;
        req["cmd"] = "GetDataPackage";
        req["games"] = str_list(needed);
        out_.push_back("[" + json::write(req) + "]");
        state_ = State::AwaitDataPackage;
        return;
    }
    send_connect();
}

void Client::add_game_names(const std::string& game, const json::Value& gd) {
    GameNames n;
    n.checksum = gd["checksum"].as_string();
    for (const auto& [name, id] : gd["item_name_to_id"].as_object()) {
        n.items[id.as_int()] = name;
        n.item_ids[name] = id.as_int();
    }
    for (const auto& [name, id] : gd["location_name_to_id"].as_object()) {
        n.locations[id.as_int()] = name;
        n.location_ids[name] = id.as_int();
    }
    games_[game] = std::move(n);
}

void Client::on_data_package(const json::Value& p) {
    for (const auto& [game, gd] : p["data"]["games"].as_object()) {
        add_game_names(game, gd);
        const std::string& sum = gd["checksum"].as_string();
        if (store_ && !sum.empty()) store_->store(game, sum, gd);
    }
    if (state_ == State::AwaitDataPackage) send_connect();
}

void Client::send_connect() {
    json::Value c;
    c["cmd"] = "Connect";
    c["password"] = cfg_.password;
    c["game"] = cfg_.game;
    c["name"] = cfg_.name;
    c["uuid"] = cfg_.uuid;
    c["version"]["major"] = cfg_.version.major;
    c["version"]["minor"] = cfg_.version.minor;
    c["version"]["build"] = cfg_.version.build;
    c["version"]["class"] = "Version";
    c["items_handling"] = cfg_.items_handling;
    c["tags"] = str_list(cfg_.tags);
    c["slot_data"] = cfg_.slot_data;
    out_.push_back("[" + json::write(c) + "]");
    state_ = State::AwaitConnected;
}

void Client::retry_connect(Config cfg) {
    cfg_ = std::move(cfg);
    if (state_ == State::Refused || state_ == State::AwaitConnected) {
        refused_.clear();
        if (!cfg_.expected_seed.empty() && seed_name() != cfg_.expected_seed) return;
        send_connect();
    }
}

void Client::read_players(const json::Value& list) {
    players_.clear();
    for (const json::Value& pl : list.as_array()) {
        NetworkPlayer np;
        // a NetworkPlayer may arrive as an object or as the NamedTuple's list form
        if (pl.is_array()) {
            np.team = static_cast<int>(pl[0].as_int());
            np.slot = static_cast<int>(pl[1].as_int());
            np.alias = pl[2].as_string();
            np.name = pl[3].as_string();
        } else {
            np.team = static_cast<int>(pl["team"].as_int());
            np.slot = static_cast<int>(pl["slot"].as_int());
            np.alias = pl["alias"].as_string();
            np.name = pl["name"].as_string();
        }
        players_.push_back(std::move(np));
    }
}

void Client::read_checked(const json::Value& list, std::vector<std::int64_t>* newly) {
    for (const json::Value& v : list.as_array()) {
        const std::int64_t id = v.as_int();
        if (checked_.insert(id).second && newly) newly->push_back(id);
        missing_.erase(id);
    }
}

void Client::on_connected(const json::Value& p) {
    state_ = State::Connected;
    refused_.clear();
    team_ = static_cast<int>(p["team"].as_int());
    slot_ = static_cast<int>(p["slot"].as_int());
    hint_points_ = static_cast<int>(p["hint_points"].as_int());
    read_players(p["players"]);
    missing_.clear();
    checked_.clear();
    for (const json::Value& v : p["missing_locations"].as_array()) missing_.insert(v.as_int());
    read_checked(p["checked_locations"], nullptr);
    slot_data_ = p["slot_data"];
    slot_info_.clear();
    for (const auto& [k, v] : p["slot_info"].as_object()) {
        NetworkSlot s;
        if (v.is_array()) {   // NamedTuple list form
            s.name = v[0].as_string();
            s.game = v[1].as_string();
            s.type = static_cast<int>(v[2].as_int(1));
            for (const json::Value& m : v[3].as_array()) s.group_members.push_back(static_cast<int>(m.as_int()));
        } else {
            s.name = v["name"].as_string();
            s.game = v["game"].as_string();
            s.type = static_cast<int>(v["type"].as_int(1));
            for (const json::Value& m : v["group_members"].as_array())
                s.group_members.push_back(static_cast<int>(m.as_int()));
        }
        slot_info_[std::atoi(k.c_str())] = std::move(s);
    }
    event(Event{Event::Kind::Connected, 0, {}, {}, {}, {}, {}, {}, 0, p});
    flush_after_connect();
}

void Client::flush_after_connect() {
    send_checks(true);
    std::vector<std::int64_t> scouts;
    for (auto id : scout_requests_)
        if (!scouted_.count(id)) scouts.push_back(id);
    if (!scouts.empty()) {
        json::Value s;
        s["cmd"] = "LocationScouts";
        s["locations"] = id_list(scouts);
        s["create_as_hint"] = 0;
        queue(std::move(s));
    }
    if (status_ != ClientStatus::Unknown) {
        json::Value s;
        s["cmd"] = "StatusUpdate";
        s["status"] = static_cast<int>(status_);
        queue(std::move(s));
    }
    std::vector<json::Value> held;
    held.swap(held_);
    for (auto& h : held) queue(std::move(h));
}

void Client::send_checks(bool all_local) {
    std::vector<std::int64_t> ids;
    for (auto id : local_checks_)
        if (all_local || !checked_.count(id)) ids.push_back(id);
    if (ids.empty() || state_ != State::Connected) return;
    json::Value c;
    c["cmd"] = "LocationChecks";
    c["locations"] = id_list(ids);
    queue(std::move(c));
}

void Client::on_refused(const json::Value& p) {
    state_ = State::Refused;
    refused_.clear();
    for (const json::Value& e : p["errors"].as_array()) refused_.push_back(e.as_string());
    Event e{Event::Kind::Refused};
    e.errors = refused_;
    e.data = p;
    event(std::move(e));
}

void Client::on_received_items(const json::Value& p) {
    const std::int64_t index = p["index"].as_int(-1);
    std::vector<NetworkItem> items;
    for (const json::Value& v : p["items"].as_array()) items.push_back(read_item(v));
    const std::int64_t have = static_cast<std::int64_t>(received_.size());
    if (index == 0) {
        received_ = items;
        desync_ = false;
        Event e{Event::Kind::ItemsReset};
        e.index = 0;
        e.items = std::move(items);
        event(std::move(e));
        return;
    }
    if (index < 0 || index > have) {
        // A gap: some items were never seen. Ask for the whole list again (the protocol's
        // resync: Sync, then LocationChecks); keep received() gap-free meanwhile.
        desync_ = true;
        json::Value s;
        s["cmd"] = "Sync";
        queue(std::move(s));
        send_checks(true);
        return;
    }
    // index <= have: overlap (a repeat) and / or new items at the end
    Event e{Event::Kind::ItemsReceived};
    e.index = static_cast<int>(have);
    for (std::size_t k = 0; k < items.size(); ++k) {
        const std::int64_t pos = index + static_cast<std::int64_t>(k);
        if (pos < have) {
            received_[static_cast<std::size_t>(pos)] = items[k];
        } else {
            received_.push_back(items[k]);
            e.items.push_back(items[k]);
        }
    }
    if (!e.items.empty()) event(std::move(e));
}

void Client::on_location_info(const json::Value& p) {
    Event e{Event::Kind::LocationInfo};
    for (const json::Value& v : p["locations"].as_array()) {
        NetworkItem it = read_item(v);
        scouted_[it.location] = it;
        e.items.push_back(it);
    }
    event(std::move(e));
}

void Client::on_room_update(const json::Value& p) {
    if (p.contains("players")) read_players(p["players"]);
    if (p.contains("hint_points")) hint_points_ = static_cast<int>(p["hint_points"].as_int());
    std::vector<std::int64_t> newly;
    if (p.contains("checked_locations")) read_checked(p["checked_locations"], &newly);
    // RoomInfo fields (permissions, hint_cost, ...) update the room info
    for (const auto& [k, v] : p.as_object())
        if (k != "cmd" && k != "players" && k != "checked_locations" && room_info_.is_object())
            room_info_[k] = v;
    if (!newly.empty()) {
        Event e{Event::Kind::LocationsChecked};
        e.locations = std::move(newly);
        event(std::move(e));
    }
    event(Event{Event::Kind::RoomUpdate, 0, {}, {}, {}, {}, {}, {}, 0, p});
}

void Client::on_bounced(const json::Value& p) {
    bool deathlink = false;
    for (const json::Value& t : p["tags"].as_array())
        if (t.as_string() == "DeathLink") deathlink = true;
    if (deathlink) {
        const json::Value& d = p["data"];
        const double t = d["time"].as_double();
        if (last_death_sent_ >= 0 && std::fabs(t - last_death_sent_) < 1e-3) return;   // our own echo
        if (!has_tag("DeathLink")) return;
        Event e{Event::Kind::DeathLink};
        e.time = t;
        e.source = d["source"].as_string();
        e.text = d["cause"].as_string();
        e.data = p;
        event(std::move(e));
        return;
    }
    if (!p.contains("data")) return;   // a keep_alive echo
    event(Event{Event::Kind::Bounced, 0, {}, {}, {}, {}, {}, {}, 0, p});
}

// ---- game actions --------------------------------------------------------------------------

void Client::check_locations(const std::vector<std::int64_t>& ids) {
    std::vector<std::int64_t> fresh;
    for (auto id : ids)
        if (local_checks_.insert(id).second) fresh.push_back(id);
    if (fresh.empty() || state_ != State::Connected) return;   // sent after the next login
    json::Value c;
    c["cmd"] = "LocationChecks";
    c["locations"] = id_list(fresh);
    queue(std::move(c));
}

void Client::scout_locations(const std::vector<std::int64_t>& ids, int create_as_hint) {
    std::vector<std::int64_t> ask;
    for (auto id : ids) {
        scout_requests_.insert(id);
        if (create_as_hint || !scouted_.count(id)) ask.push_back(id);
    }
    if (ask.empty() || (state_ != State::Connected && !create_as_hint)) return;
    json::Value s;
    s["cmd"] = "LocationScouts";
    s["locations"] = id_list(ask);
    s["create_as_hint"] = create_as_hint;
    queue(std::move(s));
}

void Client::set_status(ClientStatus s) {
    if (status_ == ClientStatus::Goal) return;   // the goal is final
    status_ = s;
    if (state_ != State::Connected) return;
    json::Value c;
    c["cmd"] = "StatusUpdate";
    c["status"] = static_cast<int>(s);
    queue(std::move(c));
}

void Client::say(std::string_view text) {
    json::Value c;
    c["cmd"] = "Say";
    c["text"] = std::string(text);
    queue(std::move(c));
}

void Client::sync() {
    if (state_ != State::Connected) return;
    json::Value s;
    s["cmd"] = "Sync";
    queue(std::move(s));
    send_checks(true);
}

bool Client::has_tag(std::string_view tag) const {
    return std::find(cfg_.tags.begin(), cfg_.tags.end(), tag) != cfg_.tags.end();
}

bool Client::send_death(double now_unix, std::string_view cause) {
    if (!has_tag("DeathLink") || state_ != State::Connected) return false;
    last_death_sent_ = now_unix;
    json::Value b;
    b["cmd"] = "Bounce";
    b["tags"] = str_list({"DeathLink"});
    b["data"]["time"] = now_unix;
    b["data"]["source"] = player_name(slot_);
    if (!cause.empty()) b["data"]["cause"] = std::string(cause);
    queue(std::move(b));
    return true;
}

void Client::set_tags(std::vector<std::string> tags) {
    cfg_.tags = std::move(tags);
    if (state_ != State::Connected) return;   // the next Connect carries them
    json::Value c;
    c["cmd"] = "ConnectUpdate";
    c["tags"] = str_list(cfg_.tags);
    queue(std::move(c));
}

void Client::keep_alive() {
    if (state_ != State::Connected) return;
    json::Value b;
    b["cmd"] = "Bounce";
    b["slots"] = id_list({slot_});
    queue(std::move(b));
}

void Client::get(const std::vector<std::string>& keys, json::Value extra) {
    json::Value g = extra.is_object() ? std::move(extra) : json::Value::object();
    g["cmd"] = "Get";
    g["keys"] = str_list(keys);
    queue(std::move(g));
}

void Client::set(const std::string& key, json::Value def, json::Value operations, bool want_reply) {
    json::Value s;
    s["cmd"] = "Set";
    s["key"] = key;
    s["default"] = std::move(def);
    s["want_reply"] = want_reply;
    s["operations"] = operations.is_array() ? std::move(operations) : json::Value::array();
    queue(std::move(s));
}

void Client::set_notify(const std::vector<std::string>& keys) {
    json::Value s;
    s["cmd"] = "SetNotify";
    s["keys"] = str_list(keys);
    queue(std::move(s));
}

// ---- names ---------------------------------------------------------------------------------

std::string Client::player_name(int slot) const {
    if (slot == 0) return "Archipelago";
    for (const NetworkPlayer& p : players_)
        if (p.team == team_ && p.slot == slot) return p.alias.empty() ? p.name : p.alias;
    auto it = slot_info_.find(slot);
    if (it != slot_info_.end() && !it->second.name.empty()) return it->second.name;
    if (slot == slot_ && !cfg_.name.empty()) return cfg_.name;
    return "Player " + std::to_string(slot);
}

std::string Client::game_of(int slot) const {
    auto it = slot_info_.find(slot);
    if (it != slot_info_.end()) return it->second.game;
    if (slot == slot_) return cfg_.game;
    return {};
}

bool Client::is_self(int slot) const {
    if (slot == slot_) return true;
    auto it = slot_info_.find(slot);
    if (it == slot_info_.end()) return false;
    const auto& m = it->second.group_members;
    return std::find(m.begin(), m.end(), slot_) != m.end();
}

const GameNames* Client::names(const std::string& game) const {
    auto it = games_.find(game);
    return it == games_.end() ? nullptr : &it->second;
}

std::string Client::item_name(std::int64_t item, int slot) const {
    std::string game = game_of(slot);
    if (game.empty()) game = cfg_.game;
    for (const std::string& g : {game, std::string("Archipelago")}) {
        if (const GameNames* n = names(g)) {
            auto it = n->items.find(item);
            if (it != n->items.end()) return it->second;
        }
    }
    return "Unknown item (ID: " + std::to_string(item) + ")";
}

std::string Client::location_name(std::int64_t loc, int slot) const {
    std::string game = game_of(slot);
    if (game.empty()) game = cfg_.game;
    for (const std::string& g : {game, std::string("Archipelago")}) {
        if (const GameNames* n = names(g)) {
            auto it = n->locations.find(loc);
            if (it != n->locations.end()) return it->second;
        }
    }
    return "Unknown location (ID: " + std::to_string(loc) + ")";
}

std::optional<std::int64_t> Client::item_id(const std::string& game, const std::string& name) const {
    if (const GameNames* n = names(game)) {
        auto it = n->item_ids.find(name);
        if (it != n->item_ids.end()) return it->second;
    }
    return std::nullopt;
}

std::optional<std::int64_t> Client::location_id(const std::string& game, const std::string& name) const {
    if (const GameNames* n = names(game)) {
        auto it = n->location_ids.find(name);
        if (it != n->location_ids.end()) return it->second;
    }
    return std::nullopt;
}

}  // namespace cleria::ap
