// Window text markup and layout. See richtext.hpp.
#include "cleria/richtext.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace cleria {

namespace {

bool prefix_ci(std::string_view s, std::string_view p) {
    if (s.size() < p.size()) return false;
    for (std::size_t i = 0; i < p.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != p[i]) return false;
    return true;
}

}  // namespace

float rich_value(std::string_view v) {
    std::string s(v);
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    if (s.find('.') != std::string::npos) return float(std::atof(s.c_str()));
    bool neg = false;
    std::size_t i = 0;
    if (i < s.size() && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; ++i; }
    long long n = 0;
    if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X'))
        n = std::strtoll(s.c_str() + i + 2, nullptr, 16);
    else if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'b' || s[i + 1] == 'B'))
        n = std::strtoll(s.c_str() + i + 2, nullptr, 2);
    else
        n = std::strtoll(s.c_str() + i, nullptr, 10);
    return float(neg ? -n : n);
}

RichLayout layout_rich(std::string_view text, const RichMetrics& m, float line_extra,
                       std::uint32_t default_colour) {
    RichLayout out;
    float x = 0, y = 0, scale = 1;
    float sx = 0, sy = 0;                                  // stpos / ldpos
    std::uint32_t colour = default_colour;
    bool counting = true, roo = false;
    int counted = 0;
    auto put = [&](unsigned char c, bool ext, float adv) {
        RichGlyph g;
        g.c = c; g.x = x; g.y = y; g.scale = scale; g.colour = colour; g.index = counted; g.ext = ext;
        out.glyphs.push_back(g);
        if (counting) ++counted;
        x += adv;
        out.width = std::max(out.width, x);
    };
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '<') {
            const std::size_t e = text.find('>', i + 1);
            if (i + 1 < text.size() && text[i + 1] != '<' && e != std::string_view::npos && e - i - 1 <= 255) {
                const std::string_view tag = text.substr(i + 1, e - i - 1);
                const std::size_t colon = tag.find(':');
                const std::string_view name = tag.substr(0, colon);
                const std::string_view val = colon == std::string_view::npos ? std::string_view{} : tag.substr(colon + 1);
                const std::string_view v1 = val.substr(0, val.find(','));
                if (prefix_ci(name, "x")) x += rich_value(v1);
                else if (prefix_ci(name, "y")) y += rich_value(v1);
                else if (prefix_ci(name, "color")) {
                    colour = v1.empty() ? default_colour : std::uint32_t(std::strtoul(std::string(v1).c_str(), nullptr, 0));
                } else if (prefix_ci(name, "scale")) {
                    const bool dec = v1.find('.') != std::string_view::npos;
                    float k = rich_value(v1);
                    if (!dec && k >= 256.0f) k /= 65536.0f;
                    scale = k <= 0 ? 1.0f : k;
                } else if (prefix_ci(name, "stpos")) { sx = x; sy = y; }
                else if (prefix_ci(name, "ldpos")) { x = sx; y = sy; }
                else if (prefix_ci(name, "code")) {
                    const unsigned char cc = static_cast<unsigned char>(int(rich_value(v1)) & 0xff);
                    put(cc, false, (m.advance ? m.advance(cc) : 16.0f) * scale);
                } else if (prefix_ci(name, "nocount")) counting = false;
                else if (prefix_ci(name, "count")) counting = true;
                else if (prefix_ci(name, "ext")) put(static_cast<unsigned char>(int(rich_value(v1))), true, 32.0f * scale);
                else if (prefix_ci(name, "roo")) roo = true;
                // rot, font, ruby, endruby, unknown: swallowed
                i = e;
                continue;
            }
            if (i + 1 < text.size() && text[i + 1] == '<') { put('<', false, (m.advance ? m.advance('<') : 16.0f) * scale); ++i; continue; }
        }
        if (c == '\n') {
            x = 0;
            y += m.height + line_extra;
            ++out.lines;
            continue;
        }
        if (c == '\r') continue;
        if (c == ' ' || c == '\t') {
            x = (x == 0) ? 10.0f : x + m.space * scale;
            out.width = std::max(out.width, x);
            continue;
        }
        const bool roo_glyph = roo && (std::isalpha(c) || c == '*' || c == '@' || c == '$' || c == '^' ||
                                       c == '{' || c == '}' || c == '#');
        if (roo_glyph) put(c, true, 32.0f * scale);
        else put(c, false, (m.advance ? m.advance(c) : 16.0f) * scale);
    }
    out.counted = counted;
    out.height = y + m.height;
    return out;
}

float reveal_alpha(const RichGlyph& g, std::uint32_t reveal) {
    const int whole = int(reveal >> 16);
    if (g.index < whole) return 1.0f;
    if (g.index == whole) return float(reveal & 0xffff) / 65536.0f;
    return 0.0f;
}

}  // namespace cleria
