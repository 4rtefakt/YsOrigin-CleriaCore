// The Ys Origin side of an Archipelago game (build/shell_re/AP_GAME.md): what CleriaCore does with the
// unchanged apworld's slot_data, its locations and its items, as plain logic over the live game state
// (GameIo). The view (apps/cleria-view/view_archipelago.inl) binds GameIo to the running game and feeds
// it the moments (room entry, VM stores, ticks); apps/cleria-apgametest drives it on a fake game.
//
// None of this is retail. The retail facts it relies on are the apworld's own tables and the retail
// mod's behaviour (YsOrigin-Archipelago/mod/src/hook_ap.cpp, [MOD] below), each cited where used.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "cleria/ap/client.hpp"
#include "cleria/ap/ys_data.hpp"
#include "cleria/json.hpp"

namespace cleria::ap::ys {

// The live game the AP layer reads and writes. Pointers may be null in tests that do not need them.
struct GameIo {
    std::int32_t* flags = nullptr;            // XsoVm::g_flags[0x200] (the view's game.g_flags)
    std::int32_t* item_arr = nullptr;         // the 54 Raval cells (game.item_arr, retail 0x76A654)
    std::uint8_t* warp_on = nullptr;          // the warp registry [24] (retail 0x76C11C)
    int character = 1;                        // g_flags[150]: 1 Yunica, 2 Hugo, 3 Toal
    std::function<void(int)> add_sp;          // the live SP wallet (player.sp, retail 0x76A75C)
    std::function<void(int)> set_weapon_tier; // g_flags[148] + the 4 weapon slots (0x7f SetWeaponLevel)
    std::function<int()> level;               // Player::level
    std::function<void(int)> set_level;       // FUN_004200F0: level N, EXP at its threshold, stats
    std::function<void()> refresh;            // the stat recompute after a grant
};

// One active location and how it is detected (ARCHIPELAGO.md §5.2).
struct Reg {
    std::int64_t id = 0;
    std::string name;
    Detect detect = Detect::Unmapped;
    int flag = -1, index = -1, bit = -1, scene = 0, floor = 0;
};

// The retail-form detect of slot_data.location_detect ({"method":"flag","offset":"0x36BDD4"},
// {"method":"scene","scene":"S_1014/S_BOX01"}, ...) in CleriaCore terms: the arithmetic of
// tools/ap_import.py convert_detect. False when it maps to nothing CleriaCore can watch.
bool convert_detect(const json::Value& d, Reg& out);
// "S_1014/S_BOX01" -> 1014 (the integer after "S_"; 0 if none).
int scene_number(const std::string& s);

// The slot_data keys CleriaCore uses (ARCHIPELAGO.md §5.3).
struct SlotOptions {
    int character = -1;                       // 0 Yunica, 1 Hugo, 2 Toal (-1 absent)
    int goal = 0;                             // 0 the final boss, 1 + every floor boss
    bool death_link = false;
    std::set<int> suppress_items, suppress_give_ids;
    std::map<std::string, std::vector<int>> progressive_gear;   // for the slot's character
    struct ProgSkill { int artifact = -1, power = -1, level_cell = -1; };
    std::map<std::string, ProgSkill> progressive_skills;
    std::map<std::string, std::vector<int>> progressive_blessings;
    bool statue_warp_locks = false;
    std::map<int, int> statue_unlock_flag;    // statue scene -> its purify flag (warp locks)
    int start_statue_scene = 0;
    std::vector<int> start_items;
    int start_level = 0, start_weapon = 0;
    int level_scaling = 0, level_margin = 0, exp_base_mult = 1, exp_catchup_mult = 1, exp_catchup_margin = 0;
    std::map<int, int> scene_levels, scene_floors;
    std::map<int, std::string> scene_names;
    bool blessing_items = false;
    std::vector<std::string> unsupported;     // options set to something CleriaCore does not do (for the UI)
};
SlotOptions parse_slot_options(const json::Value& slot_data);

// The retail warp registry's statue order (FUN_xxx table 0x68c190 = CleriaCore go_warp_point's
// kWarpRooms; the same order the mod live-confirmed [MOD hook_ap.cpp:1661-1664]). -1 if not a statue.
int warp_index_of_scene(int scene);
int scene_of_warp_index(int i);

// What a save carries for AP (the [ap] block of the sidecar, CONTENT.md §2).
struct SaveState {
    std::string seed, slot_name;              // the room's seed_name and the slot, stamped once
    int slot = 0;
    int applied = 0;                          // received() items already granted into this save
    std::set<std::int64_t> checks;            // locations this save has found (sent or not)
    std::set<std::int64_t> claimed;           // flag / bit locations our own grants set (not checks)
    bool goal_sent = false;
    bool started = false;                     // the slot's New Game loadout was given
    bool saw_gameplay = false;                // a scene 1000..6999 was entered (the 7002 goal guard)
    int ore = 0;                              // Cleria Ore received (the weapon tier ladder)
    std::map<std::string, int> prog;          // progressive skill receipts so far, by name
    std::set<int> statues;                    // statue scenes unlocked by an item (warp locks)

    bool empty() const { return seed.empty(); }
    std::map<std::string, std::string> to_kv() const;
    static SaveState from_kv(const std::map<std::string, std::string>& kv);
};

// The id-set text form used in the sidecar: "5857605,5857606-5857609".
std::string ids_to_text(const std::set<std::int64_t>& ids);
std::set<std::int64_t> ids_from_text(const std::string& s);

class Logic {
public:
    // Builds the location registry and the options from a Connected's slot_data; locations missing
    // from location_detect fall back to the table's detect (by id). `table` must outlive the Logic.
    void configure(const json::Value& slot_data, const Table& table);
    bool configured() const { return configured_; }
    void reset() { *this = Logic{}; }
    const SlotOptions& options() const { return opt_; }
    const std::vector<Reg>& regs() const { return regs_; }
    const Reg* reg(std::int64_t id) const;

    // ---- detection: each returns the locations newly found (added to st.checks) ----
    std::vector<std::int64_t> sweep(const GameIo& io);                  // per tick: flag / item_arr / bit
    std::vector<std::int64_t> on_scene(int scene);                      // a room entry: scene + floor
    std::vector<std::int64_t> on_store(int index, std::int32_t old_value, std::int32_t value);   // a VM store
    // ---- suppression of vanilla grants (the VM grant filter) ----
    bool suppress_store(int index, std::int32_t old_value, std::int32_t value) const;
    bool suppress_give(int item) const { return opt_.suppress_give_ids.count(item) != 0; }

    // ---- receiving ----
    struct Grant {
        std::string text;                     // what happened, for the log ("g_flags[0x63] 0 -> 1")
        std::string trap;                     // a trap to run in the view: "EXP Leech", "Chaos Warp", ...
        bool known = true;                    // false: an id the table does not know (skipped)
    };
    // Grants one received item into the game (the receipt rules of ARCHIPELAGO.md §5.2, with the retail
    // mod's bugs of §7.3 not copied). `flags` = the NetworkItem flags (1 progression, 2 useful, 4 trap).
    Grant grant(std::int64_t item, int flags, GameIo& io);
    // The New Game loadout of slot_data (start_items, start_weapon, start_level, warp-lock start statue).
    std::string start_loadout(GameIo& io);
    // Per tick: the weapon tier the received ore earned, the statue warp locks.
    void enforce(GameIo& io);

    // ---- goal (ARCHIPELAGO.md §7.6; [MOD hook_ap.cpp:476-519, 1962-1977]) ----
    bool goal_reached(const std::int32_t* flags, int scene) const;
    // ---- EXP scaling (slot_data level_scaling 2/3; [MOD hook_ap.cpp:2537-2560]) ----
    float exp_factor(int level) const;
    int level_floor(int level) const;          // level_scaling 1/3: the level to bump to (0 none)

    SaveState st;                             // the save's AP state (the view mirrors it to the sidecar)
    int expected_hi = 0;                      // deepest visited floor's expected level (session)
    int scene = 0;                            // the current scene (last on_scene)

private:
    std::vector<std::int64_t> fire(std::int64_t id);
    void claim_cell(int index);
    void claim_bit(int bit);
    bool give(int index, bool stack, GameIo& io, std::string& log);
    const Table* table_ = nullptr;
    SlotOptions opt_;
    std::vector<Reg> regs_;
    std::map<std::int64_t, std::size_t> by_id_;
    std::multimap<int, std::size_t> by_scene_, by_floor_;
    bool configured_ = false;
};

// Item display class from NetworkItem flags: "progression" / "useful" / "trap" / "filler".
const char* item_class(int flags);

}  // namespace cleria::ap::ys
