// cleria-aptest — the Archipelago client library (src/cleria/ap; build/shell_re/ARCHIPELAGO.md):
//   1. the protocol state machine on handwritten server exchanges (handshake + data package cache,
//      refusal, seed mismatch, items with index gaps / repeats / resync, checks and goal across a
//      reconnect, scouts, RoomUpdate, DeathLink bounce + echo filter, Get/Set, bad frames);
//   2. PrintJSON rendering to the text model (ids -> names, colours by item flags / own player);
//   3. address parsing (bare host:port -> wss then ws, default port, IPv6, bad input);
//   4. the Ys Origin table res/ap/ys_origin.json (tools/ap_import.py output);
//   5. a LOOPBACK WebSocket server (winsock, 127.0.0.1, a tiny scripted AP server) driven through
//      ap::Session: wss->ws fallback, login, items, checks, DeathLink echo, a 2 MB message, an idle
//      period longer than the handshake timeout, a server kick + automatic reconnect without
//      duplicate items, a polite close and a close the server ignores. No internet is used.
// Usage: cleria-aptest [ignored]   (run from the source root: reads res/ap/ys_origin.json; writes
// only under %TEMP%)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "cleria/ap/client.hpp"
#include "cleria/ap/session.hpp"
#include "cleria/ap/text.hpp"
#include "cleria/ap/websocket.hpp"
#include "cleria/ap/ys_data.hpp"
#include "cleria/json.hpp"

using namespace cleria;
using namespace cleria::ap;
namespace fs = std::filesystem;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[ap] %-86s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

static json::Value J(std::string_view s) {
    json::ParseError e;
    auto v = json::parse(s, &e);
    if (!v) {
        std::printf("[ap] test JSON error: %s\n", e.message.c_str());
        return {};
    }
    return *v;
}

// All commands in the client's outgoing frames.
static std::vector<json::Value> cmds(Client& c) {
    std::vector<json::Value> out;
    for (const std::string& f : c.take_outgoing()) {
        auto v = json::parse(f);
        if (v && v->is_array())
            for (const json::Value& x : v->as_array()) out.push_back(x);
        else
            out.push_back(json::Value("<not a list>"));
    }
    return out;
}
static const json::Value* find_cmd(const std::vector<json::Value>& v, const char* cmd) {
    for (const json::Value& c : v)
        if (c["cmd"].as_string() == cmd) return &c;
    return nullptr;
}
static int count_kind(const std::vector<Event>& ev, Event::Kind k) {
    int n = 0;
    for (const Event& e : ev) n += e.kind == k;
    return n;
}
static const Event* find_kind(const std::vector<Event>& ev, Event::Kind k) {
    for (const Event& e : ev)
        if (e.kind == k) return &e;
    return nullptr;
}

// A small in-memory data package store.
struct MemStore : DataPackageStore {
    std::map<std::string, json::Value> m;
    int loads = 0, stores = 0;
    std::optional<json::Value> load(const std::string& g, const std::string& s) override {
        ++loads;
        auto it = m.find(g + "|" + s);
        if (it == m.end()) return std::nullopt;
        return it->second;
    }
    void store(const std::string& g, const std::string& s, const json::Value& gd) override {
        ++stores;
        m[g + "|" + s] = gd;
    }
};

// ---- the recorded server side ---------------------------------------------------------------

static const char* kRoomInfo = R"([{"cmd":"RoomInfo","version":{"major":0,"minor":6,"build":4,"class":"Version"},
 "generator_version":{"major":0,"minor":6,"build":4,"class":"Version"},"tags":["AP"],"password":false,
 "permissions":{"release":2,"collect":2,"remaining":1},"hint_cost":10,"location_check_points":1,
 "games":["Ys Origin","Clique"],"datapackage_checksums":{"Ys Origin":"ys111","Clique":"cq222","Archipelago":"ap333"},
 "seed_name":"12345678901234567890","time":1759800000.25}])";

static const char* kDataPackage = R"([{"cmd":"DataPackage","data":{"games":{
 "Ys Origin":{"item_name_to_id":{"Cleria Ore":5853252,"Bronze Key":5853190,"SP: 50":5853300},
              "location_name_to_id":{"Wailing Blue: 2F Path 1":5857300,"Wailing Blue: 2F Path 1 #2":5857301,"Reach 1F":5857302},
              "checksum":"ys111"},
 "Clique":{"item_name_to_id":{"Button Activation":69696969},"location_name_to_id":{"The Big Red Button":69696969},"checksum":"cq222"},
 "Archipelago":{"item_name_to_id":{"Nothing":-1},"location_name_to_id":{"Cheat Console":-1,"Server":-2},"checksum":"ap333"}}}}])";

static const char* kConnected = R"([{"cmd":"Connected","team":0,"slot":2,
 "players":[{"team":0,"slot":1,"alias":"Clicker","name":"Clicker","class":"NetworkPlayer"},
            {"team":0,"slot":2,"alias":"Hugo","name":"Hugo","class":"NetworkPlayer"}],
 "missing_locations":[5857300,5857301,5857302],"checked_locations":[],
 "slot_data":{"character":1,"goal":0,"death_link":true,"weird":NaN,"location_signals":{"Reach 1F":5857302}},
 "slot_info":{"1":{"name":"Clicker","game":"Clique","type":1,"group_members":[],"class":"NetworkSlot"},
              "2":{"name":"Hugo","game":"Ys Origin","type":1,"group_members":[],"class":"NetworkSlot"}},
 "hint_points":3}])";

static void protocol_tests() {
    const fs::path tmp = fs::temp_directory_path() / "cleria-aptest-dp";
    std::error_code ec;
    fs::remove_all(tmp, ec);

    Config cfg;
    cfg.name = "Hugo";
    cfg.uuid = make_uuid();
    cfg.tags = {"DeathLink"};
    const std::u8string tmp8 = tmp.u8string();
    FileDataPackageStore store(std::string(tmp8.begin(), tmp8.end()));

    // ---- handshake + data package ----
    Client c(cfg, &store);
    check(c.state() == Client::State::Disconnected, "starts disconnected");
    c.on_open();
    check(c.state() == Client::State::AwaitRoomInfo && cmds(c).empty(), "socket open: waits for RoomInfo, sends nothing");
    c.on_message(kRoomInfo);
    auto out = cmds(c);
    const json::Value* gdp = find_cmd(out, "GetDataPackage");
    check(out.size() == 1 && gdp && gdp->operator[]("games").size() == 3, "RoomInfo -> GetDataPackage for the 3 uncached games");
    check(c.state() == Client::State::AwaitDataPackage && c.seed_name() == "12345678901234567890", "awaiting the data package; seed known");
    c.on_message(kDataPackage);
    out = cmds(c);
    const json::Value* con = find_cmd(out, "Connect");
    check(con != nullptr && c.state() == Client::State::AwaitConnected, "DataPackage -> Connect");
    if (con) {
        const json::Value& k = *con;
        check(k["game"].as_string() == "Ys Origin" && k["name"].as_string() == "Hugo" && k["password"].as_string().empty() &&
                  k["items_handling"].as_int() == 7 && k["slot_data"].as_bool() && k["tags"][0].as_string() == "DeathLink" &&
                  k["uuid"].as_string().size() == 36,
              "Connect carries game, name, password, uuid, items_handling 0b111, tags, slot_data");
        check(k["version"]["class"].as_string() == "Version" && k["version"]["major"].as_int() == 0 &&
                  k["version"]["minor"].as_int() == 6 && k["version"]["build"].as_int() == 4,
              "Connect version {0,6,4, class Version}");
    }
    check(fs::exists(store.path_for("Ys Origin", "ys111")) && fs::exists(store.path_for("Archipelago", "ap333")),
          "data packages cached on disk by game + checksum");
    auto ev = c.take_events();
    check(count_kind(ev, Event::Kind::RoomInfo) == 1, "RoomInfo event");

    c.on_message(kConnected);
    ev = c.take_events();
    check(c.state() == Client::State::Connected && c.slot() == 2 && c.team() == 0 && c.hint_points() == 3, "Connected: slot 2, team 0");
    check(count_kind(ev, Event::Kind::Connected) == 1, "Connected event");
    check(c.slot_data()["character"].as_int() == 1 && std::isnan(c.slot_data()["weird"].as_double()),
          "slot_data kept (a NaN from Python's json accepted)");
    check(c.missing_locations().size() == 3 && c.checked_locations().empty(), "missing / checked locations");
    check(c.player_name(1) == "Clicker" && c.player_name(2) == "Hugo" && c.player_name(0) == "Archipelago" &&
              c.game_of(1) == "Clique" && c.is_self(2) && !c.is_self(1),
          "players, slot games, self");
    check(c.item_name(5853252, 2) == "Cleria Ore" && c.item_name(69696969, 1) == "Button Activation" &&
              c.location_name(-1, 2) == "Cheat Console" && c.item_name(42, 2) == "Unknown item (ID: 42)",
          "item / location names per slot's game (+ the Archipelago game)");
    check(c.item_id("Ys Origin", "Bronze Key") == 5853190 && c.location_id("Ys Origin", "Reach 1F") == 5857302,
          "name -> id lookups");

    // ---- items: reset, append, repeat, gap + resync ----
    c.on_message(R"([{"cmd":"ReceivedItems","index":0,"items":[
        {"item":5853252,"location":5857300,"player":2,"flags":1,"class":"NetworkItem"},
        {"item":5853190,"location":69696969,"player":1,"flags":1,"class":"NetworkItem"}]}])");
    ev = c.take_events();
    check(c.received().size() == 2 && count_kind(ev, Event::Kind::ItemsReset) == 1 && c.items_in_sync(),
          "ReceivedItems index 0: the list is replaced (2 items)");
    c.on_message(R"([{"cmd":"ReceivedItems","index":2,"items":[{"item":5853300,"location":-2,"player":0,"flags":0}]}])");
    ev = c.take_events();
    const Event* ri = find_kind(ev, Event::Kind::ItemsReceived);
    check(ri && ri->index == 2 && ri->items.size() == 1 && c.received().size() == 3 && c.received()[2].player == 0,
          "index 2 (= next): appended, event index 2");
    c.on_message(R"([{"cmd":"ReceivedItems","index":2,"items":[{"item":5853300,"location":-2,"player":0,"flags":0}]}])");
    ev = c.take_events();
    check(c.received().size() == 3 && count_kind(ev, Event::Kind::ItemsReceived) == 0 && cmds(c).empty(),
          "a repeated packet (index 2 again) adds nothing");
    c.on_message(R"([{"cmd":"ReceivedItems","index":1,"items":[{"item":5853190,"location":69696969,"player":1,"flags":1},
        {"item":5853300,"location":-2,"player":0,"flags":0},{"item":5853252,"location":-1,"player":0,"flags":1}]}])");
    ev = c.take_events();
    ri = find_kind(ev, Event::Kind::ItemsReceived);
    check(c.received().size() == 4 && ri && ri->index == 3 && ri->items.size() == 1, "overlap: only the new tail (index 3) is new");
    c.check_locations({5857300});
    cmds(c);
    c.on_message(R"([{"cmd":"ReceivedItems","index":6,"items":[{"item":5853190,"location":1,"player":1,"flags":1}]}])");
    out = cmds(c);
    check(!c.items_in_sync() && c.received().size() == 4, "a gap (index 6, have 4): not stored, out of sync");
    check(out.size() == 2 && out[0]["cmd"].as_string() == "Sync" && out[1]["cmd"].as_string() == "LocationChecks" &&
              out[1]["locations"][0].as_int() == 5857300,
          "gap -> Sync then LocationChecks (the protocol's resync)");
    c.on_message(R"([{"cmd":"ReceivedItems","index":0,"items":[{"item":1,"location":1,"player":1,"flags":0},
        {"item":2,"location":2,"player":1,"flags":0},{"item":3,"location":3,"player":1,"flags":0},
        {"item":4,"location":4,"player":1,"flags":0},{"item":5,"location":5,"player":1,"flags":0},
        {"item":6,"location":6,"player":1,"flags":0},{"item":7,"location":7,"player":1,"flags":0}]}])");
    check(c.items_in_sync() && c.received().size() == 7 && c.received()[6].item == 7, "the Sync answer (index 0) restores the full list");
    c.take_events();

    // ---- checks, RoomUpdate, scouts ----
    c.check_locations({5857300, 5857301});
    out = cmds(c);
    check(out.size() == 1 && out[0]["cmd"].as_string() == "LocationChecks" && out[0]["locations"].size() == 1 &&
              out[0]["locations"][0].as_int() == 5857301,
          "check_locations sends only ids not reported before");
    c.on_message(R"([{"cmd":"RoomUpdate","checked_locations":[5857300,5857301],"hint_points":5}])");
    ev = c.take_events();
    const Event* lc = find_kind(ev, Event::Kind::LocationsChecked);
    check(lc && lc->locations.size() == 2 && c.checked_locations().count(5857301) && !c.missing_locations().count(5857300) &&
              c.hint_points() == 5,
          "RoomUpdate checked_locations: confirmed, removed from missing");
    c.scout_locations({5857302});
    out = cmds(c);
    check(out.size() == 1 && out[0]["cmd"].as_string() == "LocationScouts" && out[0]["create_as_hint"].as_int() == 0, "LocationScouts");
    c.on_message(R"([{"cmd":"LocationInfo","locations":[{"item":69696969,"location":5857302,"player":1,"flags":1}]}])");
    ev = c.take_events();
    check(c.scouted().count(5857302) && c.scouted().at(5857302).player == 1 && find_kind(ev, Event::Kind::LocationInfo),
          "LocationInfo: the scouted item (for slot 1) is kept");
    c.scout_locations({5857302});
    check(cmds(c).empty(), "a location already scouted is not asked again");

    // ---- DeathLink ----
    check(c.send_death(1759800100.5, "Hugo was crushed."), "send_death with the DeathLink tag");
    out = cmds(c);
    check(out.size() == 1 && out[0]["cmd"].as_string() == "Bounce" && out[0]["tags"][0].as_string() == "DeathLink" &&
              out[0]["data"]["source"].as_string() == "Hugo" && out[0]["data"]["time"].as_double() == 1759800100.5 &&
              out[0]["data"]["cause"].as_string() == "Hugo was crushed.",
          "Bounce{tags:[DeathLink], data:{time, source, cause}}");
    c.on_message(R"([{"cmd":"Bounced","tags":["DeathLink"],"data":{"time":1759800100.5,"source":"Hugo","cause":"Hugo was crushed."}}])");
    check(count_kind(c.take_events(), Event::Kind::DeathLink) == 0, "our own DeathLink echo is ignored");
    c.on_message(R"([{"cmd":"Bounced","tags":["DeathLink"],"data":{"time":1759800222,"source":"Clicker","cause":"Clicker clicked too hard."}}])");
    ev = c.take_events();
    const Event* dl = find_kind(ev, Event::Kind::DeathLink);
    check(dl && dl->source == "Clicker" && dl->text == "Clicker clicked too hard." && dl->time == 1759800222.0,
          "another player's death -> DeathLink event");
    c.on_message(R"([{"cmd":"Bounced","slots":[2]}])");
    check(c.take_events().empty(), "an empty keep-alive Bounced is silent");
    c.keep_alive();
    out = cmds(c);
    check(out.size() == 1 && out[0]["slots"][0].as_int() == 2 && !out[0].contains("data"), "keep_alive: an empty Bounce to the own slot");
    c.set_tags({});
    out = cmds(c);
    check(out.size() == 1 && out[0]["cmd"].as_string() == "ConnectUpdate" && out[0]["tags"].size() == 0, "set_tags -> ConnectUpdate");
    check(!c.send_death(1759800300, ""), "no DeathLink without the tag");
    c.on_message(R"([{"cmd":"Bounced","tags":["DeathLink"],"data":{"time":1759800400,"source":"Clicker"}}])");
    check(count_kind(c.take_events(), Event::Kind::DeathLink) == 0, "incoming deaths ignored once the tag is off");
    c.set_tags({"DeathLink"});
    cmds(c);

    // ---- goal + reconnect: checks and goal re-sent, items reset from index 0 ----
    c.set_status(ClientStatus::Goal);
    out = cmds(c);
    check(out.size() == 1 && out[0]["cmd"].as_string() == "StatusUpdate" && out[0]["status"].as_int() == 30, "StatusUpdate goal (30)");
    c.on_close();
    check(c.state() == Client::State::Disconnected && c.received().size() == 7, "socket closed: session data kept");
    c.check_locations({5857302});      // checked while offline
    c.say("offline hello");
    c.scout_locations({5857300});
    check(cmds(c).empty(), "nothing is sent while disconnected");
    c.on_open();
    c.on_message(kRoomInfo);
    out = cmds(c);
    check(out.size() == 1 && out[0]["cmd"].as_string() == "Connect", "reconnect: cached names -> straight to Connect");
    c.on_message(R"([{"cmd":"Connected","team":0,"slot":2,"players":[{"team":0,"slot":2,"alias":"Hugo","name":"Hugo"}],
        "missing_locations":[5857302],"checked_locations":[5857300,5857301],"slot_data":{},"slot_info":{},"hint_points":5}])");
    out = cmds(c);
    const json::Value* rc = find_cmd(out, "LocationChecks");
    const json::Value* rs = find_cmd(out, "StatusUpdate");
    const json::Value* sc = find_cmd(out, "LocationScouts");
    const json::Value* say = find_cmd(out, "Say");
    check(rc && rc->operator[]("locations").size() == 3, "after login: all local checks re-sent (incl. the offline one)");
    check(rs && rs->operator[]("status").as_int() == 30, "after login: the goal re-sent");
    check(sc && sc->operator[]("locations").size() == 1 && sc->operator[]("locations")[0].as_int() == 5857300, "after login: pending scouts sent");
    check(say && say->operator[]("text").as_string() == "offline hello", "after login: held chat sent");
    c.on_message(R"([{"cmd":"ReceivedItems","index":0,"items":[{"item":1,"location":1,"player":1,"flags":0}]}])");
    check(c.received().size() == 1, "the login's index-0 packet is the server's truth (the game applies past its saved count)");
    c.take_events();

    // ---- Get / Set / SetNotify / misc ----
    c.get({"_read_hints_0_2"}, J(R"({"tag":"x"})"));
    c.set("ys_origin_deaths", json::Value(0), J(R"([{"operation":"add","value":1}])"), true);
    c.set_notify({"ys_origin_deaths"});
    out = cmds(c);
    check(out.size() == 3 && out[0]["cmd"].as_string() == "Get" && out[0]["tag"].as_string() == "x" &&
              out[1]["cmd"].as_string() == "Set" && out[1]["operations"][0]["operation"].as_string() == "add" &&
              out[1]["want_reply"].as_bool() && out[2]["cmd"].as_string() == "SetNotify",
          "Get (with an echoed extra) / Set / SetNotify frames");
    c.on_message(R"([{"cmd":"Retrieved","keys":{"_read_hints_0_2":[]},"tag":"x"},{"cmd":"SetReply","key":"k","value":1,"original_value":0,"slot":2}])");
    ev = c.take_events();
    check(count_kind(ev, Event::Kind::Retrieved) == 1 && count_kind(ev, Event::Kind::SetReply) == 1 &&
              find_kind(ev, Event::Kind::Retrieved)->data["tag"].as_string() == "x",
          "two commands in one frame: Retrieved + SetReply");
    c.on_message("[{\"cmd\":\"RoomInfo\",");
    ev = c.take_events();
    check(ev.size() == 1 && ev[0].kind == Event::Kind::ProtocolError && ev[0].text.find("invalid JSON") != std::string::npos,
          "a truncated frame -> ProtocolError (no crash)");
    c.on_message(R"([{"cmd":"FutureThing"},{"cmd":"InvalidPacket","type":"arguments","original_cmd":"Set","text":"bad op"}])");
    ev = c.take_events();
    check(count_kind(ev, Event::Kind::ProtocolError) == 1 && find_kind(ev, Event::Kind::InvalidPacket) &&
              find_kind(ev, Event::Kind::InvalidPacket)->text.find("bad op") != std::string::npos,
          "unknown command reported; InvalidPacket described");

    // ---- refusal, retry, seed mismatch ----
    MemStore mem;
    Client r(cfg, &mem);
    r.on_open();
    r.on_message(kRoomInfo);
    cmds(r);
    r.on_message(kDataPackage);
    check(mem.stores == 3, "the store gets every downloaded game");
    cmds(r);
    r.on_message(R"([{"cmd":"ConnectionRefused","errors":["InvalidSlot"]}])");
    ev = r.take_events();
    check(r.state() == Client::State::Refused && find_kind(ev, Event::Kind::Refused) &&
              find_kind(ev, Event::Kind::Refused)->errors[0] == "InvalidSlot" && describe_refusal("InvalidSlot").find("slot") != std::string::npos,
          "ConnectionRefused -> Refused (InvalidSlot)");
    Config cfg2 = cfg;
    cfg2.name = "Yunica";
    r.retry_connect(cfg2);
    out = cmds(r);
    check(out.size() == 1 && out[0]["cmd"].as_string() == "Connect" && out[0]["name"].as_string() == "Yunica",
          "retry_connect sends a new Connect on the same socket");
    Client r2(cfg, &mem);
    r2.on_open();
    r2.on_message(kRoomInfo);
    out = cmds(r2);
    check(out.size() == 1 && out[0]["cmd"].as_string() == "Connect" && mem.loads >= 3, "a fresh client finds every game in the store");
    Config seeded = cfg;
    seeded.expected_seed = "99999";
    Client s(seeded, &mem);
    s.on_open();
    s.on_message(kRoomInfo);
    ev = s.take_events();
    check(s.state() == Client::State::Refused && cmds(s).empty() && find_kind(ev, Event::Kind::SeedMismatch) &&
              find_kind(ev, Event::Kind::SeedMismatch)->text == "12345678901234567890",
          "a room of another seed: SeedMismatch, no Connect");

    fs::remove_all(tmp, ec);
}

// ---- PrintJSON ----------------------------------------------------------------------------

static void text_tests() {
    Client c(Config{}, nullptr);
    c.on_open();
    c.on_message(kRoomInfo);
    c.on_message(kDataPackage);
    c.on_message(kConnected);
    c.take_events();
    c.on_message(R"J([{"cmd":"PrintJSON","type":"ItemSend","receiving":2,
        "item":{"item":5853252,"location":69696969,"player":1,"flags":1},
        "data":[{"type":"player_id","text":"1"},{"text":" sent "},{"type":"item_id","text":"5853252","player":2,"flags":1},
                {"text":" to "},{"type":"player_id","text":"2"},{"text":" ("},
                {"type":"location_id","text":"69696969","player":1},{"text":")"}]}])J");
    auto ev = c.take_events();
    const Event* p = find_kind(ev, Event::Kind::Print);
    check(p && p->line.plain() == "Clicker sent Cleria Ore to Hugo (The Big Red Button)", "ItemSend rendered with names from both games");
    std::printf("     \"%s\"\n", p ? p->line.plain().c_str() : "");
    if (p && p->line.spans.size() == 8) {
        const auto& s = p->line.spans;
        check(s[0].kind == Span::Kind::Player && s[0].rgb == 0xFAFAD2 && s[4].rgb == 0xEE00EE, "other player yellow, own player magenta");
        check(s[2].kind == Span::Kind::Item && s[2].colored && s[2].rgb == 0xAF99EF && s[2].flags == 1, "progression item plum");
        check(s[6].kind == Span::Kind::Location && s[6].rgb == 0x00FF7F && s[6].player == 1, "location green");
        check(!s[1].colored && p->line.type == "ItemSend", "plain text uncoloured; type kept");
    } else {
        check(false, "8 spans");
    }
    json::Value parts = J(R"J([{"type":"item_id","text":"5853190","player":2,"flags":2},{"type":"item_id","text":"5853190","player":2,"flags":4},
        {"type":"item_id","text":"5853190","player":2,"flags":0},{"type":"color","color":"bold;red_bg;green","text":"G"},
        {"type":"hint_status","text":"(priority)","hint_status":30},{"type":"entrance_name","text":"Door"},
        {"type":"mystery","text":"?"},{"text":"!"},{"type":"item_id","text":"777","player":2,"flags":0},
        {"type":"player_name","text":"Stranger"}])J");
    Line l = render_parts(parts, c);
    const auto& s = l.spans;
    check(s.size() == 10, "10 parts -> 10 spans");
    if (s.size() == 10) {
        check(s[0].rgb == 0x6D8BE8 && s[1].rgb == 0xFA8072 && s[2].rgb == 0x00EEEE, "useful slateblue, trap salmon, filler cyan");
        check(s[3].bold && s[3].has_bg && s[3].bg == 0xEE0000 && s[3].rgb == 0x00FF7F, "colour chain bold;red_bg;green");
        check(s[4].kind == Span::Kind::HintStatus && s[4].rgb == 0xAF99EF && s[5].rgb == 0x6495ED, "hint status plum, entrance blue");
        check(s[6].text == "?" && !s[6].colored && s[7].text == "!", "unknown part types render as plain text");
        check(s[8].text == "Unknown item (ID: 777)" && s[9].text == "Stranger" && s[9].rgb == 0xFAFAD2, "unknown id named; player_name yellow");
    }
    std::uint32_t rgb = 0;
    check(color_rgb("plum", rgb) && rgb == 0xAF99EF && !color_rgb("chartreuse", rgb), "colour table");
}

// ---- addresses ----------------------------------------------------------------------------

static void url_tests() {
    auto u = candidate_urls("archipelago.gg:38281");
    check(u.size() == 2 && u[0].str() == "wss://archipelago.gg:38281/" && u[1].str() == "ws://archipelago.gg:38281/",
          "bare host:port -> wss then ws");
    u = candidate_urls("  localhost  ");
    check(u.size() == 2 && u[0].port == 38281 && u[1].host == "localhost", "bare host -> default port 38281, trimmed");
    u = candidate_urls("wss://archipelago.gg:51234");
    check(u.size() == 1 && u[0].secure && u[0].port == 51234, "wss:// -> only wss");
    u = candidate_urls("WS://127.0.0.1:1/room?x=1");
    check(u.size() == 1 && !u[0].secure && u[0].path == "/room?x=1", "ws:// with a path, case-insensitive scheme");
    u = candidate_urls("archipelago://example.org:5000");
    check(u.size() == 1 && !u[0].secure && u[0].host == "example.org", "archipelago:// -> ws (the reference client)");
    u = candidate_urls("[::1]:4000");
    check(u.size() == 2 && u[0].host == "::1" && u[0].str() == "wss://[::1]:4000/", "IPv6 literal");
    check(candidate_urls("").empty() && candidate_urls("http://x").empty() && candidate_urls("host:0").empty() &&
              candidate_urls("host:99999").empty() && candidate_urls("host:12ab").empty() && candidate_urls("user@host:1").empty(),
          "bad addresses refused");
}

// ---- the Ys Origin table ------------------------------------------------------------------

static void table_tests() {
    std::string err;
    auto t = ys::load("res/ap/ys_origin.json", &err);
    check(t.has_value(), "res/ap/ys_origin.json loads");
    if (!t) {
        std::printf("     %s\n", err.c_str());
        return;
    }
    check(t->locations.size() == 336 && t->items.size() == 147, "336 locations, 147 items (the apworld's whole id space)");
    check(t->location_base == 0x596000 && t->item_base == 0x595000, "the apworld's base ids");
    bool ids_ok = true, mapped = true;
    for (std::size_t i = 0; i < t->locations.size(); ++i) {
        ids_ok &= t->locations[i].id == t->location_base + static_cast<std::int64_t>(i);
        mapped &= t->locations[i].detect != ys::Detect::Unmapped;
    }
    for (std::size_t i = 0; i < t->items.size(); ++i) {
        ids_ok &= t->items[i].id == t->item_base + static_cast<std::int64_t>(i);
        mapped &= t->items[i].kind != ys::ItemKind::Unknown;
    }
    check(ids_ok, "ids are dense from the base ids (sorted-name enumeration)");
    check(mapped, "every location has a detection, every item a kind");
    const ys::Location* box = t->chest_by_script("map\\s_10\\S_1001\\s_box01.xso");
    check(box && box->key == "S_1001/S_BOX01" && box->detect == ys::Detect::Flag && box->flag == 0x12E &&
              box->box_flag == 0x12E && box->vanilla.size() == 1 && box->vanilla[0].item == 0x59 && box->scene == 1001,
          "S_1001/S_BOX01: box flag 0x12E (MODS.md §7.1), Celcetan Panacea, scene 1001");
    check(t->location(box->id) == box && t->by_flag(0x12E).size() == 1, "lookup by id / by flag");
    int chests = 0, chest_flag_ok = 0, item_arr = 0, scene = 0, bits = 0;
    for (const auto& l : t->locations) {
        if (l.type == "chest") {
            ++chests;
            chest_flag_ok += l.detect == ys::Detect::Flag ? l.flag == l.box_flag : l.detect == ys::Detect::Scene;
        }
        item_arr += l.detect == ys::Detect::ItemArr && l.index >= 0 && l.index < 54;
        scene += l.detect == ys::Detect::Scene && l.detect_scene >= 1000;
        bits += l.detect == ys::Detect::Bit && l.flag == 0xD9 && l.bit >= 0 && l.bit < 32;
    }
    check(chests == 61 && chest_flag_ok == 61, "61 chest locations; each detected by its own box flag (one by scene)");
    check(item_arr == 36 && bits == 24 && scene == 160, "36 item_arr (gear upgrades), 24 blessing bits of g_flags[0xD9], 160 scene");
    const ys::Item* key = nullptr;
    const ys::Item* sp = nullptr;
    const ys::Item* armor = nullptr;
    const ys::Item* warp = nullptr;
    for (const auto& it : t->items) {
        if (it.name == "Bronze Key") key = &it;
        if (it.name == "SP: 500") sp = &it;
        if (it.name == "Progressive Armor") armor = &it;
        if (it.name == "1F Warp") warp = &it;
    }
    check(key && key->kind == ys::ItemKind::Item && key->flag == 0x63, "Bronze Key -> g_flags[0x63]");
    check(sp && sp->kind == ys::ItemKind::Sp && sp->amount == 500, "SP: 500 -> 500 SP");
    check(armor && armor->kind == ys::ItemKind::ProgressiveGear && armor->ladder.at("hugo").size() == 5 &&
              armor->ladder.at("hugo")[0] == 13,
          "Progressive Armor -> Hugo's ladder 13..17");
    check(warp && warp->kind == ys::ItemKind::StatueUnlock && warp->scene == 1000 && warp->flag == 300,
          "1F Warp -> statue S_1000, purify flag 300");
    check(t->scene_floors.size() > 100 && t->scene_names.count(1001), "scene floors / names");
}

// ---- loopback WebSocket server ------------------------------------------------------------

// SHA-1 (FIPS 180-1) for the Sec-WebSocket-Accept header; test-only.
static std::string sha1(const std::string& msg) {
    std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    std::string m = msg;
    const std::uint64_t bits = static_cast<std::uint64_t>(msg.size()) * 8;
    m += '\x80';
    while (m.size() % 64 != 56) m += '\0';
    for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xFF);
    const auto rol = [](std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
    for (std::size_t off = 0; off < m.size(); off += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(std::uint8_t(m[off + i * 4])) << 24) | (std::uint32_t(std::uint8_t(m[off + i * 4 + 1])) << 16) |
                   (std::uint32_t(std::uint8_t(m[off + i * 4 + 2])) << 8) | std::uint32_t(std::uint8_t(m[off + i * 4 + 3]));
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const std::uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = t;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::string out;
    for (std::uint32_t x : h)
        for (int i = 3; i >= 0; --i) out += static_cast<char>((x >> (i * 8)) & 0xFF);
    return out;
}

static std::string base64(const std::string& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const std::uint32_t v = (std::uint8_t(in[i]) << 16) | (std::uint8_t(in[i + 1]) << 8) | std::uint8_t(in[i + 2]);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        const std::uint32_t v = std::uint8_t(in[i]) << 16;
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        const std::uint32_t v = (std::uint8_t(in[i]) << 16) | (std::uint8_t(in[i + 1]) << 8);
        out += t[v >> 18]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += '=';
    }
    return out;
}

// A one-client-at-a-time WebSocket server on 127.0.0.1 that plays a minimal AP server.
struct MiniServer {
    SOCKET listener = INVALID_SOCKET;
    int port = 0;
    std::thread th;
    std::atomic<bool> quit{false}, kick{false}, ignore_close{false};
    std::mutex mu;
    std::vector<json::Value> got;          // every client command
    std::atomic<int> connections{0}, tls_attempts{0}, polite_closes{0};
    std::string big;                        // sent once after login when non-empty

    bool start() {
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof a) || listen(listener, 4)) return false;
        int len = sizeof a;
        getsockname(listener, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
        th = std::thread([this] { run(); });
        return true;
    }
    void stop() {
        quit = true;
        closesocket(listener);
        if (th.joinable()) th.join();
    }
    std::vector<json::Value> commands() {
        std::lock_guard<std::mutex> l(mu);
        return got;
    }
    int count(const char* cmd) {
        int n = 0;
        for (const auto& c : commands()) n += c["cmd"].as_string() == cmd;
        return n;
    }

    static bool send_all(SOCKET s, const std::string& d) {
        std::size_t off = 0;
        while (off < d.size()) {
            const int n = ::send(s, d.data() + off, static_cast<int>(std::min<std::size_t>(d.size() - off, 1 << 20)), 0);
            if (n <= 0) return false;
            off += n;
        }
        return true;
    }
    static bool send_frame(SOCKET s, int opcode, const std::string& payload) {
        std::string f;
        f += static_cast<char>(0x80 | opcode);
        const std::uint64_t n = payload.size();
        if (n < 126) f += static_cast<char>(n);
        else if (n < 65536) { f += static_cast<char>(126); f += static_cast<char>(n >> 8); f += static_cast<char>(n & 0xFF); }
        else { f += static_cast<char>(127); for (int i = 7; i >= 0; --i) f += static_cast<char>((n >> (i * 8)) & 0xFF); }
        return send_all(s, f + payload);
    }

    void run() {
        while (!quit) {
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(listener, &rs);
            timeval tv{0, 50000};
            if (select(0, &rs, nullptr, nullptr, &tv) <= 0) continue;
            SOCKET s = accept(listener, nullptr, nullptr);
            if (s == INVALID_SOCKET) continue;
            serve(s);
            closesocket(s);
        }
    }

    void serve(SOCKET s) {
        // the HTTP upgrade request
        std::string req;
        char buf[65536];
        while (req.find("\r\n\r\n") == std::string::npos) {
            const int n = recv(s, buf, sizeof buf, 0);
            if (n <= 0) return;
            req.append(buf, n);
            if (req.size() >= 4 && req.compare(0, 4, "GET ") != 0) { ++tls_attempts; return; }   // a TLS hello: not ours
        }
        std::string key;
        {
            std::string low = req;
            for (char& ch : low) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
            const std::size_t k = low.find("sec-websocket-key:");
            if (k == std::string::npos) return;
            std::size_t b = k + 18;
            while (req[b] == ' ') ++b;
            key = req.substr(b, req.find("\r\n", b) - b);
        }
        const std::string accept = base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"));
        send_all(s, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                        accept + "\r\n\r\n");
        ++connections;
        send_frame(s, 1, kRoomInfo);

        std::string in, msg;
        while (!quit) {
            if (kick.exchange(false)) return;       // drop the TCP connection without a close frame
            fd_set rs;
            FD_ZERO(&rs);
            FD_SET(s, &rs);
            timeval tv{0, 20000};
            if (select(0, &rs, nullptr, nullptr, &tv) < 0) return;
            if (FD_ISSET(s, &rs)) {
                const int n = recv(s, buf, sizeof buf, 0);
                if (n <= 0) return;
                in.append(buf, n);
            }
            // parse complete client frames (always masked)
            while (in.size() >= 2) {
                const int op = in[0] & 0x0F;
                const bool fin = (in[0] & 0x80) != 0;
                std::uint64_t len = in[1] & 0x7F;
                std::size_t hdr = 2;
                if (len == 126) { if (in.size() < 4) break; len = (std::uint8_t(in[2]) << 8) | std::uint8_t(in[3]); hdr = 4; }
                else if (len == 127) { if (in.size() < 10) break; len = 0; for (int i = 0; i < 8; ++i) len = (len << 8) | std::uint8_t(in[2 + i]); hdr = 10; }
                if (in.size() < hdr + 4 + len) break;
                const char* mask = in.data() + hdr;
                std::string payload = in.substr(hdr + 4, static_cast<std::size_t>(len));
                for (std::size_t i = 0; i < payload.size(); ++i) payload[i] ^= mask[i % 4];
                in.erase(0, hdr + 4 + static_cast<std::size_t>(len));
                if (op == 8) {
                    if (ignore_close) continue;
                    ++polite_closes;
                    send_frame(s, 8, payload.substr(0, 2));
                    return;
                }
                if (op == 9) { send_frame(s, 10, payload); continue; }
                if (op == 10) continue;
                msg += payload;
                if (!fin) continue;
                on_text(s, msg);
                msg.clear();
            }
        }
    }

    void on_text(SOCKET s, const std::string& text) {
        auto v = json::parse(text);
        if (!v) return;
        for (const json::Value& c : v->as_array()) {
            {
                std::lock_guard<std::mutex> l(mu);
                got.push_back(c);
            }
            const std::string& cmd = c["cmd"].as_string();
            if (cmd == "GetDataPackage") send_frame(s, 1, kDataPackage);
            else if (cmd == "Connect" && c["name"].as_string() == "Nobody") {
                send_frame(s, 1, R"([{"cmd":"ConnectionRefused","errors":["InvalidSlot"]}])");
            } else if (cmd == "Connect") {
                send_frame(s, 1, kConnected);
                send_frame(s, 1, R"([{"cmd":"ReceivedItems","index":0,"items":[{"item":5853252,"location":5857300,"player":2,"flags":1},
                    {"item":5853190,"location":69696969,"player":1,"flags":1}]},
                    {"cmd":"PrintJSON","type":"Join","data":[{"text":"Hugo (Team #1) playing Ys Origin has joined."}],"team":0,"slot":2}])");
                if (!big.empty()) send_frame(s, 1, big);
            } else if (cmd == "LocationChecks") {
                json::Value up;
                up["cmd"] = "RoomUpdate";
                up["checked_locations"] = c["locations"];
                send_frame(s, 1, "[" + json::write(up) + "]");
            } else if (cmd == "Bounce") {
                json::Value b = c;
                b["cmd"] = "Bounced";
                send_frame(s, 1, "[" + json::write(b) + "]");
            } else if (cmd == "Say") {
                send_frame(s, 1, R"([{"cmd":"PrintJSON","type":"Chat","data":[{"text":"Hugo: )" + c["text"].as_string() + R"("}]}])");
            }
        }
    }
};

static double now_s() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// Pumps the session until `done` or the timeout; collects events.
static bool pump(Session& s, std::vector<Event>& ev, const std::function<bool()>& done, double timeout_s) {
    const double end = now_s() + timeout_s;
    while (now_s() < end) {
        s.update(now_s());
        for (Event& e : s.take_events()) ev.push_back(std::move(e));
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

static void loopback_tests() {
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
    check(base64(sha1("dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11")) == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=",
          "test server's Sec-WebSocket-Accept (RFC 6455 §1.3 example)");
    MiniServer srv;
    // a 2 MB message (larger than the 64 KB receive buffer: arrives as fragments)
    srv.big = R"([{"cmd":"Retrieved","keys":{"blob":")" + std::string(2u << 20, 'x') + R"("}}])";
    check(srv.start(), "loopback server listening on 127.0.0.1");
    const std::string addr = "127.0.0.1:" + std::to_string(srv.port);
    std::printf("     server at %s\n", addr.c_str());

    MemStore store;
    Session sess(&store);
    Config cfg;
    cfg.name = "Hugo";
    cfg.uuid = make_uuid();
    cfg.tags = {"DeathLink"};
    Session::Options opt;
    opt.first_retry_s = 0.3;
    opt.ws.handshake_timeout_ms = 800;     // shorter than the idle wait below
    std::vector<Event> ev;
    sess.connect(addr, cfg, opt);
    check(sess.status() == Session::Status::Connecting && sess.status_text(now_s()).find("connecting to wss://") == 0,
          "status: connecting to wss:// first");
    const bool logged = pump(sess, ev, [&] { return sess.status() == Session::Status::Connected && sess.client().received().size() == 2; }, 15);
    check(logged, "bare address: wss fails, ws:// logs in; items arrive");
    std::printf("     status: %s | url %s | tls attempts seen %d\n", sess.status_text(now_s()).c_str(), sess.url().c_str(), srv.tls_attempts.load());
    check(sess.url().rfind("ws://127.0.0.1:", 0) == 0 && srv.tls_attempts >= 1, "the TLS attempt reached the server first, then ws:// won");
    check(sess.status_text(now_s()) == "connected as Hugo (slot 2)", "status: connected as Hugo (slot 2)");
    check(srv.count("GetDataPackage") == 1 && srv.count("Connect") == 1, "server saw GetDataPackage + Connect");
    pump(sess, ev, [&] { return find_kind(ev, Event::Kind::Retrieved) != nullptr; }, 10);
    const Event* big = find_kind(ev, Event::Kind::Retrieved);
    check(big && big->data["keys"]["blob"].as_string().size() == (2u << 20), "a 2 MB message arrives whole");
    check(find_kind(ev, Event::Kind::Print) && find_kind(ev, Event::Kind::Print)->line.plain().find("has joined") != std::string::npos,
          "PrintJSON over the wire");

    // idle longer than the handshake timeout, then use the socket
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    ev.clear();
    sess.client().check_locations({5857301});
    const bool upd = pump(sess, ev, [&] { return sess.client().checked_locations().count(5857301) > 0; }, 5);
    check(upd && sess.status() == Session::Status::Connected, "after 1.5 s idle the socket still works (check -> RoomUpdate)");

    // DeathLink: our bounce comes back from the server and is filtered
    ev.clear();
    sess.client().send_death(1759800500.0, "Hugo fell.");
    sess.client().say("hi");
    pump(sess, ev, [&] { return find_kind(ev, Event::Kind::Print) != nullptr; }, 5);
    check(srv.count("Bounce") == 1 && count_kind(ev, Event::Kind::DeathLink) == 0, "DeathLink sent; the echo is ignored");
    check(find_kind(ev, Event::Kind::Print) && find_kind(ev, Event::Kind::Print)->line.plain() == "Hugo: hi", "Say -> chat line");

    // the server drops the connection: automatic reconnect, items from index 0 again, no duplicates
    ev.clear();
    srv.kick = true;
    const bool lost = pump(sess, ev, [&] { return sess.status() == Session::Status::Disconnected; }, 5);
    std::printf("     status: %s\n", sess.status_text(now_s()).c_str());
    check(lost && sess.status_text(now_s()).find("disconnected: ") == 0 && sess.retry_at() > 0,
          "kick -> disconnected: <reason> (retry scheduled)");
    const bool back = pump(sess, ev, [&] { return sess.status() == Session::Status::Connected; }, 15);
    check(back && srv.connections >= 2 && store.loads > 0 && srv.count("GetDataPackage") == 1,
          "reconnected and logged in again (names from the cache: no 2nd GetDataPackage)");
    pump(sess, ev, [&] { return srv.count("LocationChecks") >= 2; }, 5);
    check(sess.client().received().size() == 2 && srv.count("LocationChecks") >= 2, "after reconnect: 2 items (not 4), checks re-sent");

    // polite close
    sess.disconnect();
    const bool closed = pump(sess, ev, [&] { return srv.polite_closes >= 1; }, 5);
    check(closed && sess.status() == Session::Status::Idle, "disconnect(): close frame answered, status idle");

    // a wrong slot name: auth failed, no reconnect loop; retry_login on the same socket works
    {
        Session bad_slot(&store);
        Config nobody = cfg;
        nobody.name = "Nobody";
        std::vector<Event> ev2;
        bad_slot.connect("ws://127.0.0.1:" + std::to_string(srv.port), nobody, opt);
        const bool refused = pump(bad_slot, ev2, [&] { return bad_slot.status() == Session::Status::Refused; }, 10);
        std::printf("     status: %s\n", bad_slot.status_text(now_s()).c_str());
        check(refused && bad_slot.status_text(now_s()) == "auth failed: no slot with that name in this room" &&
                  bad_slot.retry_at() < 0,
              "wrong slot: \"auth failed: no slot with that name in this room\", no retry scheduled");
        bad_slot.retry_login(cfg);
        const bool ok = pump(bad_slot, ev2, [&] { return bad_slot.status() == Session::Status::Connected; }, 10);
        check(ok && bad_slot.status_text(now_s()) == "connected as Hugo (slot 2)", "retry_login with the right slot logs in");
        bad_slot.disconnect();
        pump(bad_slot, ev2, [&] { return bad_slot.status() == Session::Status::Idle; }, 3);
        check(bad_slot.status_text(now_s()) == "not connected", "after disconnect(): \"not connected\"");
    }

    // a server that ignores the close frame: close() still finishes (abort after ~2 s)
    srv.ignore_close = true;
    WebSocket ws;
    ws.open("ws://127.0.0.1:" + std::to_string(srv.port));
    bool opened = false, closed2 = false;
    const double t0 = now_s();
    while (now_s() - t0 < 10 && !closed2) {
        for (auto& e : ws.poll()) {
            if (e.kind == WebSocket::Event::Kind::Opened) { opened = true; ws.close(); }
            if (e.kind == WebSocket::Event::Kind::Closed) closed2 = !e.error;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(opened && closed2 && now_s() - t0 < 6, "close() against a server that never answers finishes by itself");
    srv.ignore_close = false;

    // nothing listening: a clear failure, then retries
    WebSocket dead;
    dead.open("ws://127.0.0.1:1");
    std::string reason;
    const double t1 = now_s();
    while (now_s() - t1 < 15 && reason.empty()) {
        for (auto& e : dead.poll())
            if (e.kind == WebSocket::Event::Kind::Closed) reason = e.text;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::printf("     \"%s\"\n", reason.c_str());
    check(reason.find("could not connect: ws://127.0.0.1:1/") == 0, "nothing listening: \"could not connect: <url>: <why>\"");
    WebSocket bad;
    bad.open("http://nope");
    auto be = bad.poll();
    check(be.size() == 1 && be[0].kind == WebSocket::Event::Kind::Closed && be[0].text.find("invalid server address") == 0,
          "an invalid address fails at once");

    srv.stop();
    WSACleanup();
}

// Manual check, never run by ctest: `cleria-aptest --live <address>` opens a socket and prints
// what happens for 10 s (RoomInfo of a live room, or the failure). Sends nothing but the handshake.
static int live(const char* address) {
    WebSocket ws;
    ws.open(address);
    const double t0 = now_s();
    while (now_s() - t0 < 10) {
        for (auto& e : ws.poll()) {
            if (e.kind == WebSocket::Event::Kind::Opened) std::printf("[live] opened %s\n", e.text.c_str());
            if (e.kind == WebSocket::Event::Kind::Message) std::printf("[live] message: %.300s\n", e.text.c_str());
            if (e.kind == WebSocket::Event::Kind::Closed) {
                std::printf("[live] closed: %s\n", e.text.c_str());
                return 0;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ws.close();
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--live") return live(argv[2]);
    protocol_tests();
    text_tests();
    url_tests();
    table_tests();
    loopback_tests();
    std::printf("[ap] %s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
