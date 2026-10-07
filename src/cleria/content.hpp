// Mod content patches (build/shell_re/MODS.md §7, build/shell_re/CONTENT.md): the data a mod's
// content/*.ini files add or change, merged over every active mod in load order, and the pure
// functions that apply it. NOT retail: the game has no such layer; everything here is CleriaCore's.
// The glue that wires it into the live game is apps/cleria-view/view_content.inl.
//
//   content/items*.ini    [item.0x59]  Name / Description / Icon / Magnitude      (patch a retail item)
//                         [item.0x100] Name / Description / Icon / Kind / Amount / SetsFlag   (a NEW item)
//   content/chests*.ini   [S_1001/S_BOX01]  Item=0x100 | none   Count=1   Text="..."   (redirect a chest)
//                         [S_1001/S_BOX03/0x36]  only the grant of item 0x36 in that script
//   content/pickups*.ini  [pickup.<id>]  Room=S_1009  Pos=x,y,z  Item=0x57  Count=1  Flag=<cflag>
//   content/rules*.ini    [rule.<name>]  On=<condition>  Do=<action>; <action>...  Once=1
//   content/text*.ini     [MAP/S_10/S_1009/SAVE.XSO]  3="new text"   "original text"="new text"
//                         [*]  "original text"="new text"   (every script)
// A file is picked by its name's prefix (items, chests, pickups, rules, text). A LATER mod in the
// load order wins an entry (an item field, a chest, a pickup, a rule, a text key) another mod also
// sets; both get a note, like the vfs file conflicts.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cleria/mod_events.hpp"
#include "cleria/sidecar.hpp"
#include "cleria/xso.hpp"

namespace cleria {
struct ItemTable;
}

namespace cleria::content {

inline constexpr int kRetailItems = 0x80;     // INVINFO's 128 records; g_flags[0..127] are their counts
inline constexpr int kNewItemFirst = 0x100;   // [H] CleriaCore's own item ids: 0x100..0x1FF
inline constexpr int kNewItemLast = 0x1FF;
inline constexpr int kNoIcon = 0xff;          // Window_Treasure's "no icon" item (FUN_005781f0)

// "0x59", "89", "-1". nullopt when not a whole number.
std::optional<int> parse_int(std::string_view s);

struct ItemDef {
    int id = -1;
    std::optional<std::string> name, description, icon;
    std::optional<std::uint32_t> magnitude;
    // New items only: what obtaining one does. "key" (default: just held), "heal" (Amount HP at
    // once, not kept), "gold" (Amount SP at once, not kept), "flag" (kept, and SetsFlag set).
    std::string kind;
    int amount = 0;
    std::string sets_flag;               // "0x1F0" (a g_flags cell) or a CleriaCore flag name
    std::map<std::string, std::string> by;   // field -> the mod that set it (conflict notes)
};

struct ChestDef {
    std::string key;                     // "S_1001/S_BOX01" or "S_1001/S_BOX03/0x36" (upper-case)
    int item = -1;                       // the item given instead
    bool nothing = false;                // Item=none: the chest gives nothing
    int count = 1;
    std::string text;                    // the "Acquired" box text; "" = the original, re-labelled
    std::string mod;
};

struct PickupDef {
    std::string id;                      // the section's name after "pickup."
    std::string room;                    // upper-case
    float pos[3] = {0, 0, 0};
    int item = -1, count = 1;
    std::string flag;                    // the CleriaCore flag that marks it collected (default "pickup.<id>")
    std::string mod;
};

// A rule's trigger. `flag 0x1A0 >= 1` fires when the comparison turns true (FlagChanged);
// `room_enter S_1100`, `item 0x63`, `chest S_1001/S_BOX01`, `death`, `new_game`, `load`, `save`.
struct Cond {
    events::Kind kind = events::Kind::FlagChanged;
    int index = -1;                      // flag: the cell; item: the item id
    std::string op = ">=";
    std::int32_t value = 1;
    std::string arg;                     // room_enter: the room; chest: the chest key (upper-case)
};
struct Action {
    enum class Kind { Give, Text, Flag, CFlag };
    Kind kind = Kind::Text;
    int item = -1, count = 1;            // Give
    std::string text;                    // Text
    int index = -1;                      // Flag
    std::string name;                    // CFlag
    std::string op = "=";                // Flag / CFlag: "=" or "+="
    std::int32_t value = 0;
};
struct RuleDef {
    std::string name;
    Cond on;
    std::vector<Action> act;
    bool once = false;                   // fire once per save (the sidecar flag "rule.<name>")
    std::string mod;
};

struct TextDef {
    std::map<int, std::string> by_index;                  // pool index -> new text
    std::map<std::string, std::string> by_text;           // exact original -> new text
    std::map<std::string, std::string> by;                // key -> the mod that set it
};

struct Note {
    std::string mod, text;
};

struct Content {
    std::map<int, ItemDef> items;
    std::map<std::string, ChestDef> chests;               // by upper-case key
    std::map<std::string, PickupDef> pickups;             // by id
    std::map<std::string, RuleDef> rules;                 // by name
    std::map<std::string, TextDef> text;                  // by vfs::key of the script, or "*"
    std::vector<std::string> mods;                        // the mods that contributed, load order
    std::vector<Note> notes;                              // conflicts (both sides)
    std::vector<Note> errors;                             // "chests.ini line 3: ..." (the entry is skipped)
    bool empty() const {
        return items.empty() && chests.empty() && pickups.empty() && rules.empty() && text.empty();
    }
};

// Parse one content file of `mod` into `c` (later calls win). `file` is the file name ("items.ini"),
// which picks the parser by its prefix.
void merge_file(Content& c, const std::string& mod, std::string_view file, std::string_view text);
// Every *.ini of a mod's content/ folder, by name. Returns the number of files read.
int merge_dir(Content& c, const std::string& mod, const std::filesystem::path& content_dir);

std::optional<Cond> parse_cond(std::string_view s, std::string* err);
std::vector<Action> parse_actions(std::string_view s, std::string* err);
bool compare(std::int32_t a, std::string_view op, std::int32_t b);

// ---- applying it -------------------------------------------------------------------------------
// Patch a loaded INVINFO table: retail fields of ids < 0x80, new records for ids 0x100..0x1FF (the
// table grows; ids in between stay empty records).
void patch_items(const Content& c, ItemTable& t);

// The chest rule for an item grant of `script` (instance name; '@' and case ignored) in `room`:
// the item-qualified key first ("S_1001/S_BOX03/0x36"), then the script's key. Null = none.
const ChestDef* find_chest(const Content& c, std::string_view room, std::string_view script, int item);

// The treasure box text re-labelled: the coloured name span <color:..>NAME<color:> becomes `name`
// and a " x N" count becomes `count`; a text without a colour span is rebuilt in the retail shape
// "Acquired\n<color:0xfff2ea8c>NAME<color:>." (the game's own "\n" escape, kept verbatim).
std::string treasure_text(const std::string& original, const std::string& name, int count);

// Apply text.ini to a parsed script whose virtual name is `vname` ("MAP/S_10/S_1009/SAVE.XSO").
// Returns the number of pool strings replaced.
int patch_script(const Content& c, std::string_view vname, Xso& x);

// What a chest redirect granted (for the event bus); `apply` = the caller must grant it (a new item:
// the VM wrote no cell), else the VM already wrote the retail cell.
struct Obtained {
    int item = -1, count = 0;
    bool apply = false;
};
// The grant filter's decision for `q` while `room` is current: a chest redirect of chests.ini, or
// Pass. Pure: `items` (the patched table, may be null) only names the item for the box text.
GrantDecision decide(const Content& c, const GrantQuery& q, std::string_view room, const ItemTable* items,
                     std::vector<Obtained>* got);

// The retail inventory rule for a count cell: max(count, 0) + n, capped at 99 (inv_sanitize).
std::int32_t add_count(std::int32_t cur, int n);

// Rules: does `r` fire on `e` (the trigger, and Once not yet spent in `sc`)?
bool rule_fires(const RuleDef& r, const events::Event& e, const Sidecar& sc);
struct RuleEnv {
    std::int32_t* flags = nullptr;       // g_flags
    Sidecar* sc = nullptr;
    std::function<void(int item, int count)> give;
    std::function<void(const std::string&)> message;
};
// Run the actions; marks a Once rule spent in the sidecar.
void run_rule(const RuleDef& r, RuleEnv& env);

}  // namespace cleria::content
