// The mod event bus (build/shell_re/MODS.md §7.3.5, build/shell_re/CONTENT.md §3). NOT retail: a
// small observer API over game moments that CleriaCore's own layers (the content rules, the
// Archipelago client, the speedrun watcher, trackers) subscribe to. The game emits; nothing a
// subscriber does is required for the game to run, and with no subscriber an emit is a loop over
// an empty list.
//
//   RoomEnter     a room was entered (a door, a script warp, a load, the boot room)    room, prev_room
//   FlagChanged   a g_flags cell changed since the last tick (the per-tick diff)        index, old_value, new_value
//   ChestOpened   a chest's box flag (Interactable::opened_flag) went 0 -> set          room, script, object, index
//   ItemObtained  an item was granted: chest / event script, pickup, mod rule, AP       item, count, source
//   PlayerDeath   the player's HP reached 0 (the [death] site)                          room
//   BossDefeated  a boss fight was won (content_boss_watch, view_content.inl)             room, index (220..227 or -1)
//   ItemUsed      an item was used from the book and consumed (the Panacea)             item
//   DoorOpened    a locked door's UseKey script stored its door flag                    room, index, script
//   BlessingBought a blessing-shop purchase reached its effect (op 0xAF SetXXX)          room, index (the blessing),
//                                                                                        new_value (SP paid), old_value (1 = a mod suppressed the effect)
//   NewGame       a new game started                                                    character
//   Save / Load   a save slot was written / read (after the sidecar was handled)        slot
//
// All on the game thread (the fixed tick); handlers may emit in turn (bounded depth).
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace cleria::events {

enum class Kind { RoomEnter, FlagChanged, ChestOpened, ItemObtained, PlayerDeath, NewGame, Save, Load, BossDefeated, ItemUsed, DoorOpened,
                  BlessingBought };
const char* kind_name(Kind k);

struct Event {
    Kind kind = Kind::RoomEnter;
    std::string room;                 // upper-case room name ("S_1001"); every kind carries the current room
    std::string prev_room;            // RoomEnter
    int index = -1;                   // FlagChanged / ChestOpened / DoorOpened: the g_flags cell; BossDefeated: the boss flag or -1
    std::int32_t old_value = 0, new_value = 0;   // FlagChanged
    std::string script, object;       // ChestOpened / ItemObtained from a script: "S_BOX01", "box_01"
    int item = -1, count = 0;         // ItemObtained (item may be a new id >= 0x100); ItemUsed: item
    std::string source;               // ItemObtained: "chest", "script", "pickup", "rule", "ap", ...
    int slot = -1;                    // Save / Load
    int character = 0;                // NewGame
};

using Handler = std::function<void(const Event&)>;

class Bus {
  public:
    // Subscribe to every kind / to one kind. Returns an id for unsubscribe().
    int subscribe(Handler h);
    int subscribe(Kind k, Handler h);
    void unsubscribe(int id);
    void clear();
    std::size_t size() const { return subs_.size(); }
    // Call every matching handler in subscription order. A handler that emits nests up to
    // kMaxDepth levels; deeper emits are dropped (a rule loop cannot hang the tick).
    void emit(const Event& e);
    static constexpr int kMaxDepth = 8;
    std::uint64_t emitted = 0;        // statistics for tests / the drive log
    std::uint64_t dropped = 0;

  private:
    struct Sub { int id; bool all; Kind kind; Handler h; };
    std::vector<Sub> subs_;
    int next_ = 1;
    int depth_ = 0;
};

// The process-wide bus the game emits on.
Bus& bus();

// The per-tick g_flags diff: FlagChanged for every cell that differs from the previous call.
// prime() takes a baseline without emitting (after a load / new game, so a wholesale state change
// is not reported as 512 changes).
struct FlagWatch {
    std::array<std::int32_t, 0x200> prev{};
    bool primed = false;
    void prime(const std::int32_t* flags);
    // Returns the number of changes emitted. Not primed -> primes and emits nothing.
    int diff(const std::int32_t* flags, Bus& b, const std::string& room);
};

}  // namespace cleria::events
