// A read-only INI document for CleriaCore's own data files (mod content/*.ini, the save sidecar):
// ordered sections, ordered (and repeatable) key / value pairs, line numbers for error messages.
// The dialect follows cleria.ini (settings.hpp Ini): `[section]`, `key=value`, case-insensitive
// names (callers compare lower-case), `;` / `#` comment lines. Additions for content files:
//   - a key may be quoted:  "Original text"="Its replacement"
//   - a value may be quoted; only \" is unescaped (other backslashes, e.g. the game's own "\n"
//     line-break escape in script text, are kept verbatim),
//   - a trailing comment starts at a ';' PRECEDED BY WHITESPACE outside quotes ("Icon=X   ; note");
//     a ';' glued to a word is data ("Do=give 0x59; text \"hi\"").
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace cleria {

struct IniDoc {
    struct Pair {
        std::string key;      // as written (unquoted)
        std::string value;    // unquoted, trimmed
        int line = 0;         // 1-based
        bool quoted_key = false;
    };
    struct Section {
        std::string name;     // as written, trimmed ("" = the root, before the first header)
        int line = 0;
        std::vector<Pair> pairs;
        const Pair* find(std::string_view key) const;   // the LAST pair with this key (case-insensitive)
    };
    std::vector<Section> sections;
    std::vector<std::string> errors;   // "line 4: no '='"

    static IniDoc parse(std::string_view text);
};

std::string ini_lower(std::string_view s);
std::string ini_trim(std::string_view s);

}  // namespace cleria
