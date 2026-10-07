// The Archipelago protocol, client side, as a pure state machine (build/shell_re/ARCHIPELAGO.md §3):
// it is fed the server's text frames and hands back frames to send, plus events for the game. It
// never touches a socket, a clock or (except through DataPackageStore) a file, so it is tested on
// recorded exchanges (apps/cleria-aptest). Game-agnostic: Ys Origin specifics live in ys_data.hpp
// and in the game glue.
//
// Reference: Archipelago docs/network protocol.md (fetched 2026-10-07; AP 0.6.x) and the
// reference CommonClient.py. Flow:
//   socket open -> RoomInfo -> [GetDataPackage -> DataPackage]  (only for games whose checksum
//   is not cached) -> Connect -> Connected | ConnectionRefused -> ReceivedItems / LocationInfo /
//   RoomUpdate / PrintJSON / Bounced / Retrieved / SetReply / InvalidPacket.
//
// Items: received() is the slot's whole received list, index-aligned with the server's
// ReceivedItems indices. The game keeps its own "applied" count in its save and applies
// received()[applied..] (items_in_sync() must be true). A packet with index 0 replaces the list;
// a packet past the end (a gap) triggers Sync + LocationChecks and is NOT stored (the server answers
// the Sync with the full list at index 0, one round trip later) [H: the protocol allows accepting
// it; we keep the list gap-free so an index is always a position].
//
// Checks / goal / scouts survive reconnects: check_locations() remembers every id for the session
// and re-sends the ones the server has not confirmed after each Connected; the goal is re-sent too.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cleria/ap/text.hpp"
#include "cleria/json.hpp"

namespace cleria::ap {

// The AP version sent in Connect. 0.6.4 = what the retail Ys Origin mod's apclientpp sends
// (YsOrigin-Archipelago mod/build/_deps/apclientpp-src/apclient.hpp:14), proven against that apworld.
struct Version {
    int major = 0, minor = 6, build = 4;
};

struct NetworkItem {
    std::int64_t item = 0;
    std::int64_t location = 0;
    int player = 0;     // ReceivedItems: the finding slot; LocationInfo: the receiving slot
    int flags = 0;      // kProgression | kUseful | kTrap
};
enum ItemFlag : int { kProgression = 1, kUseful = 2, kTrap = 4 };

struct NetworkPlayer {
    int team = 0, slot = 0;
    std::string alias, name;
};

struct NetworkSlot {
    std::string name, game;
    int type = 1;                         // 0 spectator, 1 player, 2 group
    std::vector<int> group_members;
};

enum class ClientStatus : int { Unknown = 0, Connected = 5, Ready = 10, Playing = 20, Goal = 30 };

// One game's id <-> name tables from a data package.
struct GameNames {
    std::string checksum;
    std::unordered_map<std::int64_t, std::string> items, locations;
    std::unordered_map<std::string, std::int64_t> item_ids, location_ids;
};

// Where data packages are cached between sessions (keyed by game + checksum).
class DataPackageStore {
public:
    virtual ~DataPackageStore() = default;
    // The cached GameData object ({item_name_to_id, location_name_to_id, checksum}), if any.
    virtual std::optional<json::Value> load(const std::string& game, const std::string& checksum) = 0;
    virtual void store(const std::string& game, const std::string& checksum, const json::Value& game_data) = 0;
};

// A folder of "<game>_<checksum>.json" files (the reference client caches the same way, by checksum).
class FileDataPackageStore : public DataPackageStore {
public:
    explicit FileDataPackageStore(std::string dir) : dir_(std::move(dir)) {}
    std::optional<json::Value> load(const std::string& game, const std::string& checksum) override;
    void store(const std::string& game, const std::string& checksum, const json::Value& game_data) override;
    std::string path_for(const std::string& game, const std::string& checksum) const;

private:
    std::string dir_;
};

struct Config {
    std::string game = "Ys Origin";
    std::string name;                     // the slot name
    std::string password;
    std::string uuid;                     // a stable per-install id (make_uuid() once, persist)
    Version version;                      // the AP version this client speaks
    int items_handling = 0b111;           // other worlds + own world + starting inventory
    std::vector<std::string> tags;        // e.g. {"DeathLink"}
    bool slot_data = true;
    std::string expected_seed;            // non-empty: refuse a room of another seed (a save's seed)
};

struct Event {
    enum class Kind {
        RoomInfo,          // data = the packet; the seed is seed_name()
        SeedMismatch,      // RoomInfo's seed != Config::expected_seed; text = the room's seed. No Connect sent.
        Connected,         // logged in; slot(), slot_data(), checked/missing are set
        Refused,           // errors = ConnectionRefused errors (InvalidSlot, InvalidPassword, ...)
        ItemsReceived,     // items = the NEW items, index = the position of items[0] in received()
        ItemsReset,        // a ReceivedItems at index 0 replaced received() (connect / resync)
        LocationInfo,      // items = scouted items (player = receiving slot); also in scouted()
        LocationsChecked,  // locations = ids newly confirmed checked by the server
        Print,             // line = rendered PrintJSON; data = the packet
        DeathLink,         // source / cause / time from another player's death
        Bounced,           // any other Bounced packet (data = the packet)
        Retrieved,         // data = the packet ("keys" + echoed extras)
        SetReply,          // data = the packet
        RoomUpdate,        // data = the packet (after it was applied)
        InvalidPacket,     // text = the server's description
        ProtocolError,     // text = what was wrong with a server frame (bad JSON, ...)
    };
    Kind kind;
    int index = 0;
    std::vector<NetworkItem> items;
    std::vector<std::int64_t> locations;
    std::vector<std::string> errors;
    Line line;
    std::string text, source;
    double time = 0;
    json::Value data;
};

class Client : public NameLookup {
public:
    enum class State {
        Disconnected,      // no socket
        AwaitRoomInfo,     // socket open
        AwaitDataPackage,  // GetDataPackage sent
        AwaitConnected,    // Connect sent
        Connected,         // authenticated
        Refused,           // ConnectionRefused / seed mismatch; retry_connect() may try again
    };

    explicit Client(Config cfg, DataPackageStore* store = nullptr);

    // ---- transport side ----
    void on_open();                                  // the socket opened
    void on_message(std::string_view frame);         // one text frame from the server
    void on_close();                                 // the socket closed (keeps session data)
    std::vector<std::string> take_outgoing();        // frames to send, in order
    std::vector<Event> take_events();

    // ---- game side (all safe in any state; queued / remembered until Connected) ----
    void check_locations(const std::vector<std::int64_t>& ids);
    void scout_locations(const std::vector<std::int64_t>& ids, int create_as_hint = 0);
    void set_status(ClientStatus s);                 // Goal is remembered and re-sent after reconnects
    void say(std::string_view text);
    void sync();                                     // Sync + LocationChecks (manual resync)
    // DeathLink: a Bounce to the "DeathLink" tag; `now_unix` is the death's unix time (seconds).
    // Ignored unless the DeathLink tag is set. Own echoes are filtered by that time.
    bool send_death(double now_unix, std::string_view cause);
    void set_tags(std::vector<std::string> tags);    // ConnectUpdate when connected
    bool has_tag(std::string_view tag) const;
    void keep_alive();                               // an empty Bounce to the own slot (the reference client's keep_alive)
    void get(const std::vector<std::string>& keys, json::Value extra = {});
    void set(const std::string& key, json::Value def, json::Value operations, bool want_reply = false);
    void set_notify(const std::vector<std::string>& keys);
    void send_packet(json::Value cmd);               // any command object; sent when the socket is open
    void retry_connect(Config cfg);                  // new credentials after Refused (same socket)

    // ---- state ----
    State state() const { return state_; }
    const Config& config() const { return cfg_; }
    const json::Value& room_info() const { return room_info_; }
    std::string seed_name() const { return room_info_["seed_name"].as_string(); }
    bool password_required() const { return room_info_["password"].as_bool(); }
    int slot() const { return slot_; }
    int team() const { return team_; }
    int hint_points() const { return hint_points_; }
    const json::Value& slot_data() const { return slot_data_; }
    const std::vector<NetworkPlayer>& players() const { return players_; }
    const std::map<int, NetworkSlot>& slot_info() const { return slot_info_; }
    const std::vector<NetworkItem>& received() const { return received_; }
    bool items_in_sync() const { return !desync_; }
    const std::set<std::int64_t>& checked_locations() const { return checked_; }  // server-confirmed
    const std::set<std::int64_t>& missing_locations() const { return missing_; }
    const std::set<std::int64_t>& local_checks() const { return local_checks_; }   // reported by the game
    const std::map<std::int64_t, NetworkItem>& scouted() const { return scouted_; }
    const std::vector<std::string>& refused_errors() const { return refused_; }   // the last refusal

    // ---- names (NameLookup) ----
    std::string player_name(int slot) const override;
    std::string item_name(std::int64_t item, int slot) const override;
    std::string location_name(std::int64_t loc, int slot) const override;
    bool is_self(int slot) const override;
    std::string game_of(int slot) const;
    const GameNames* names(const std::string& game) const;
    std::optional<std::int64_t> item_id(const std::string& game, const std::string& name) const;
    std::optional<std::int64_t> location_id(const std::string& game, const std::string& name) const;
    void add_game_names(const std::string& game, const json::Value& game_data);   // e.g. a bundled package

private:
    void queue(json::Value cmd);
    void event(Event e) { events_.push_back(std::move(e)); }
    void send_connect();
    void handle(const json::Value& cmd);
    void on_room_info(const json::Value& p);
    void on_data_package(const json::Value& p);
    void on_connected(const json::Value& p);
    void on_refused(const json::Value& p);
    void on_received_items(const json::Value& p);
    void on_location_info(const json::Value& p);
    void on_room_update(const json::Value& p);
    void on_bounced(const json::Value& p);
    void read_players(const json::Value& list);
    void read_checked(const json::Value& list, std::vector<std::int64_t>* newly);
    void flush_after_connect();
    void send_checks(bool all_local);

    Config cfg_;
    DataPackageStore* store_ = nullptr;
    State state_ = State::Disconnected;
    std::vector<std::string> out_;
    std::vector<json::Value> held_;               // packets waiting for the socket / login
    std::vector<Event> events_;

    json::Value room_info_;
    std::map<std::string, GameNames> games_;
    int slot_ = 0, team_ = 0, hint_points_ = 0;
    json::Value slot_data_;
    std::vector<NetworkPlayer> players_;
    std::map<int, NetworkSlot> slot_info_;
    std::vector<NetworkItem> received_;
    bool desync_ = false;
    std::set<std::int64_t> checked_, missing_, local_checks_;
    std::map<std::int64_t, NetworkItem> scouted_;
    std::set<std::int64_t> scout_requests_;
    ClientStatus status_ = ClientStatus::Unknown;
    double last_death_sent_ = -1;
    std::vector<std::string> refused_;
};

// A random RFC 4122 version-4 UUID string (for Config::uuid; generate once and persist).
std::string make_uuid();
// The NetworkItem in a JSON object ({"item","location","player","flags"}).
NetworkItem read_item(const json::Value& v);

}  // namespace cleria::ap
