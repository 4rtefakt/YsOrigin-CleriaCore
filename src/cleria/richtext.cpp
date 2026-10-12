// Window text markup and layout. See richtext.hpp.
#include "cleria/richtext.hpp"

#include "cleria/extfont.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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
    const int W = std::max(1, m.screen_w), H = std::max(1, m.screen_h);
    const int W43 = (4 * H) / 3;
    const double A = W > W43 && W43 > 0 ? double(W) / double(W43) : 1.0;   // 0x765178
    float x = 0, y = 0, scale = 1;                         // the raster's pen (FUN_005f03f0), pixels
    int mx = 0, mw = 0;                                    // the measure's pen and widest line (FUN_005efb50)
    float sx = 0, sy = 0;                                  // stpos / ldpos
    int smx = 0;
    std::uint32_t colour = default_colour;
    bool counting = true, roo = false;
    int counted = 0;
    auto put = [&](unsigned char c, bool ext, float adv) {
        RichGlyph g;
        g.c = c; g.x = x; g.y = y; g.scale = scale; g.colour = colour; g.index = counted; g.ext = ext;
        out.glyphs.push_back(g);
        if (counting) ++counted;
        x += adv;
        mx += ext ? int(adv) : int(short(int(double(adv) * A)));   // [BYTES 005efb50: (short)(int)(gmCellIncX x A)]
        mw = std::max(mw, mx);
    };
    // An extfont cell: drawn at its own size, advanced by left + right (FUN_005f03f0 ext branch).
    auto put_ext = [&](int cell) {
        put(static_cast<unsigned char>(cell & 0xff), true,
            float(std::lround((m.ext_advance ? m.ext_advance(cell) : 32.0f) * float(H) / 768.0f)));
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
                if (prefix_ci(name, "x")) {               // n x W >> 10 px; the measure's x A [EXPORT 005efb50.c:170-172]
                    const int n = (int(rich_value(v1)) * W) / 1024;
                    x += float(n);
                    mx = int(double(n) * A + double(mx));
                } else if (prefix_ci(name, "y")) y += float((int(rich_value(v1)) * H) / 768);
                else if (prefix_ci(name, "color")) {
                    colour = v1.empty() ? default_colour : std::uint32_t(std::strtoul(std::string(v1).c_str(), nullptr, 0));
                } else if (prefix_ci(name, "scale")) {
                    const bool dec = v1.find('.') != std::string_view::npos;
                    float k = rich_value(v1);
                    if (!dec && k >= 256.0f) k /= 65536.0f;
                    scale = k <= 0 ? 1.0f : k;
                } else if (prefix_ci(name, "stpos")) { sx = x; sy = y; smx = mx; }
                else if (prefix_ci(name, "ldpos")) { x = sx; y = sy; mx = smx; }
                else if (prefix_ci(name, "code")) {
                    const unsigned char cc = static_cast<unsigned char>(int(rich_value(v1)) & 0xff);
                    put(cc, false, (m.advance ? m.advance(cc) : 16.0f) * scale);
                } else if (prefix_ci(name, "nocount")) counting = false;
                else if (prefix_ci(name, "count")) counting = true;
                else if (prefix_ci(name, "ext")) put_ext(int(rich_value(v1)));
                else if (prefix_ci(name, "roo")) roo = true;
                // rot, font, ruby, endruby, unknown: swallowed
                i = e;
                continue;
            }
            if (i + 1 < text.size() && text[i + 1] == '<') { put('<', false, (m.advance ? m.advance('<') : 16.0f) * scale); ++i; continue; }
        }
        if (c == '\n') {
            x = 0; mx = 0;
            y += m.height + float((int(line_extra) * H) / 768);   // tmHeight + extra x H / 768, truncated
            ++out.lines;
            continue;
        }
        if (c == '\r') continue;
        if (c == ' ' || c == '\t') {                       // 10 raw pixels at a line's start; never x A
            x = (x == 0) ? 10.0f : x + m.space * scale;
            mx = (mx == 0) ? 10 : mx + int(m.space * scale);
            mw = std::max(mw, mx);
            continue;
        }
        const int rg = roo ? roo_glyph(c, m.english_roonic) : -1;    // tag 15's letter map
        if (rg >= 0) put_ext(rg);
        else put(c, false, (m.advance ? m.advance(c) : 16.0f) * scale);
    }
    out.counted = counted;
    // out[2] = (w_px << 10) / W, out[3] = (h_px x 768) / H [EXPORT 005efb50.c:119-129]
    out.width = float((long long)(mw) * 1024 / W);
    out.height = float((long long)(y + m.height) * 768 / H);
    return out;
}

float reveal_alpha(const RichGlyph& g, std::uint32_t reveal) {
    const int whole = int(reveal >> 16);
    if (g.index < whole) return 1.0f;
    if (g.index == whole) return float(reveal & 0xffff) / 65536.0f;
    return 0.0f;
}

}  // namespace cleria
