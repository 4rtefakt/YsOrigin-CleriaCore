// FUN_0057ad90. See wndtext.hpp.
#include "cleria/wndtext.hpp"

namespace cleria {

std::string wnd_unescape(std::string_view s) {
    static constexpr const char* kFirst = "Adol";            // 0x6d6020
    static constexpr const char* kLast = "Christin";         // 0x6d602c
    static constexpr const char* kFull = "Adol Christin";    // 0x6d6044
    static constexpr const char* kExtra = " ";               // 0x6d6040
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\0') break;
        if (c != '\\' || i + 1 >= s.size()) { out += c; continue; }
        const char n = s[i + 1];
        if (n == 'r') { out += '\r'; ++i; continue; }
        if (n == 'n') { out += '\n'; ++i; continue; }
        if (n == 't') { out += ' '; ++i; continue; }
        if (n == 'a') { out += kFirst; ++i; continue; }
        if (n == 'c') { out += kLast; ++i; continue; }
        if ((n == 'x' || n == 'f') && i + 2 < s.size()) {
            const char d = s[i + 2];
            if (d == '4') { out += kExtra; i += 2; continue; }
            if (d == '3') { out += kFull; i += 2; continue; }
            if (d == '1') { out += kFirst; i += 2; continue; }
            if (d == '2') { out += kLast; i += 2; continue; }
            if (d == '0') { out += kFirst[0]; break; }       // the engine terminates here
        }
        out += n;                                            // LAB_0057afe6: drop the backslash
        ++i;
    }
    while (!out.empty() && (out.back() == '\r' || out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

}  // namespace cleria
