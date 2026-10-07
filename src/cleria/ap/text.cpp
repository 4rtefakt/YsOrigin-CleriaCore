// PrintJSON text model — see text.hpp.
#include "cleria/ap/text.hpp"

#include <charconv>

namespace cleria::ap {

std::string Line::plain() const {
    std::string s;
    for (const Span& sp : spans) s += sp.text;
    return s;
}

bool color_rgb(std::string_view name, std::uint32_t& rgb) {
    // NetUtils.JSONtoTextParser.color_codes ("not exact color names, close enough").
    struct C { const char* n; std::uint32_t v; };
    static const C table[] = {
        {"black", 0x000000}, {"red", 0xEE0000}, {"green", 0x00FF7F}, {"yellow", 0xFAFAD2},
        {"blue", 0x6495ED}, {"magenta", 0xEE00EE}, {"cyan", 0x00EEEE}, {"slateblue", 0x6D8BE8},
        {"plum", 0xAF99EF}, {"salmon", 0xFA8072}, {"white", 0xFFFFFF}, {"orange", 0xFF7700},
    };
    for (const C& c : table)
        if (name == c.n) { rgb = c.v; return true; }
    return false;
}

namespace {

// Applies a "color" value ("red", "bold;underline;green", "blue_bg") to a span.
void apply_colors(Span& sp, std::string_view codes) {
    while (!codes.empty()) {
        const std::size_t semi = codes.find(';');
        const std::string_view c = codes.substr(0, semi);
        codes = semi == std::string_view::npos ? std::string_view{} : codes.substr(semi + 1);
        std::uint32_t rgb;
        if (c == "bold") sp.bold = true;
        else if (c == "underline") sp.underline = true;
        else if (c.size() > 3 && c.substr(c.size() - 3) == "_bg") {
            if (color_rgb(c.substr(0, c.size() - 3), rgb)) { sp.has_bg = true; sp.bg = rgb; }
        } else if (color_rgb(c, rgb)) {
            sp.colored = true;
            sp.rgb = rgb;
        }
    }
}

// The part's text as a number (ids arrive as strings: {"type":"item_id","text":"12"}).
bool text_int(const json::Value& part, std::int64_t& out) {
    const json::Value& t = part["text"];
    if (t.is_number()) {
        auto v = t.get_int();
        if (!v) return false;
        out = *v;
        return true;
    }
    const std::string& s = t.as_string();
    auto r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

const char* hint_status_color(std::int64_t status) {
    switch (status) {
    case 40: return "green";        // HINT_FOUND
    case 0: return "white";         // HINT_UNSPECIFIED
    case 10: return "slateblue";    // HINT_NO_PRIORITY
    case 20: return "salmon";       // HINT_AVOID
    case 30: return "plum";         // HINT_PRIORITY
    }
    return "red";
}

void item_color(Span& sp) {
    const int f = sp.flags;
    if (f == 0) apply_colors(sp, "cyan");
    else if (f & 1) apply_colors(sp, "plum");
    else if (f & 2) apply_colors(sp, "slateblue");
    else if (f & 4) apply_colors(sp, "salmon");
    else apply_colors(sp, "cyan");
}

}  // namespace

Line render_parts(const json::Value& parts, const NameLookup& names) {
    Line line;
    for (const json::Value& part : parts.as_array()) {
        Span sp;
        const std::string& type = part["type"].as_string();
        const json::Value& text = part["text"];
        sp.text = text.is_string() ? text.as_string() : (text.is_null() ? std::string() : json::write(text));
        std::int64_t n = 0;
        if (type == "player_id" && text_int(part, n)) {
            sp.kind = Span::Kind::Player;
            sp.id = n;
            sp.text = names.player_name(static_cast<int>(n));
            apply_colors(sp, names.is_self(static_cast<int>(n)) ? "magenta" : "yellow");
        } else if (type == "player_name") {
            sp.kind = Span::Kind::Player;
            apply_colors(sp, "yellow");
        } else if (type == "item_id" && text_int(part, n)) {
            sp.kind = Span::Kind::Item;
            sp.id = n;
            sp.player = static_cast<int>(part["player"].as_int());
            sp.flags = static_cast<int>(part["flags"].as_int());
            sp.text = names.item_name(n, sp.player);
            item_color(sp);
        } else if (type == "item_name") {
            sp.kind = Span::Kind::Item;
            sp.player = static_cast<int>(part["player"].as_int());
            sp.flags = static_cast<int>(part["flags"].as_int());
            item_color(sp);
        } else if (type == "location_id" && text_int(part, n)) {
            sp.kind = Span::Kind::Location;
            sp.id = n;
            sp.player = static_cast<int>(part["player"].as_int());
            sp.text = names.location_name(n, sp.player);
            apply_colors(sp, "green");
        } else if (type == "location_name") {
            sp.kind = Span::Kind::Location;
            sp.player = static_cast<int>(part["player"].as_int());
            apply_colors(sp, "green");
        } else if (type == "entrance_name") {
            sp.kind = Span::Kind::Entrance;
            apply_colors(sp, "blue");
        } else if (type == "hint_status") {
            sp.kind = Span::Kind::HintStatus;
            apply_colors(sp, hint_status_color(part["hint_status"].as_int(-1)));
        } else if (type == "color") {
            apply_colors(sp, part["color"].as_string());
        }
        // "text", absent or unknown types: plain text, as the protocol asks
        line.spans.push_back(std::move(sp));
    }
    return line;
}

Line render_print_json(const json::Value& packet, const NameLookup& names) {
    Line line = render_parts(packet["data"], names);
    line.type = packet["type"].as_string();
    return line;
}

}  // namespace cleria::ap
