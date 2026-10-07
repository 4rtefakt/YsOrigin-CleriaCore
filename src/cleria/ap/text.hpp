// Archipelago PrintJSON -> a plain / coloured text model for a chat or item-feed overlay
// (build/shell_re/ARCHIPELAGO.md §4). The colours follow the reference client's renderer
// (Archipelago NetUtils.JSONtoTextParser): own player magenta, other players yellow, items by
// their flags (progression plum, useful slate blue, trap salmon, other cyan), locations green,
// entrances blue, hint statuses by status. A part's "color" may chain several codes ("bold;red").
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cleria/json.hpp"

namespace cleria::ap {

// Resolves ids for the renderer; the protocol Client implements it from its data package.
struct NameLookup {
    virtual ~NameLookup() = default;
    virtual std::string player_name(int slot) const = 0;
    virtual std::string item_name(std::int64_t item, int slot) const = 0;      // in that slot's game
    virtual std::string location_name(std::int64_t loc, int slot) const = 0;  // in that slot's game
    virtual bool is_self(int slot) const = 0;                                 // own slot or own group
};

struct Span {
    enum class Kind : std::uint8_t { Text, Player, Item, Location, Entrance, HintStatus };
    Kind kind = Kind::Text;
    std::string text;
    bool colored = false;            // false: the overlay's default text colour
    std::uint32_t rgb = 0xFFFFFF;    // 0xRRGGBB when colored
    bool has_bg = false;
    std::uint32_t bg = 0;            // 0xRRGGBB when has_bg (the *_bg console colours)
    bool bold = false, underline = false;
    std::int64_t id = 0;             // Player: slot; Item: item id; Location: location id
    int player = 0;                  // Item / Location: the owning slot
    int flags = 0;                   // Item: NetworkItem flags (1 progression, 2 useful, 4 trap)
};

struct Line {
    std::string type;                // PrintJsonType ("ItemSend", "Chat", ...); "" when absent
    std::vector<Span> spans;
    std::string plain() const;       // the spans' text joined
};

// RGB for an Archipelago colour name (the reference client's table); false if unknown.
bool color_rgb(std::string_view name, std::uint32_t& rgb);

// Renders a PrintJSON packet (the whole {"cmd":"PrintJSON", "data":[...], "type":...} object).
Line render_print_json(const json::Value& packet, const NameLookup& names);
// Renders one part list (PrintJSON "data").
Line render_parts(const json::Value& parts, const NameLookup& names);

}  // namespace cleria::ap
