#pragma once
// Ini: the line-preserving document of retail's yso_win.ini dialect (Falcom ini library
// 0x5ce9d0..0x5d0f70): case-insensitive section and key names, ints with 0x / 0b prefixes, "quoted"
// strings, %0.8f floats, CRLF. Lines the model does not know (retail-only keys, comments, unknown
// sections) round-trip untouched. Spec: build/shell_re/CONFIG.md §1.3. Its own file so that the mod
// layer (mods.cpp reads mod.ini with it) does not pull in the typed settings model (settings.hpp).
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cleria {

// ---- the dialect ---------------------------------------------------------------------------------
// FUN_005d07f0's int parse: optional sign, then 0x/0X hex, 0b/0B binary, else decimal (strtol; the
// value stops at the first non-digit). nullopt if no digit follows.
std::optional<std::int32_t> parse_ini_int(std::string_view v);

class Ini {
  public:
    void parse(std::string_view text);          // \r\n or \n line ends
    bool load(const std::filesystem::path& p);  // false = absent / unreadable (the document is cleared)
    std::string text() const;                   // every line + "\r\n"
    bool save(const std::filesystem::path& p) const;   // creates the parent folders
    bool empty() const { return lines.empty(); }

    // `sec` "" = the root (before the first [section]). Names compare case-insensitively.
    bool has(std::string_view sec, std::string_view key) const;
    std::optional<std::string> raw(std::string_view sec, std::string_view key) const;   // after '=', trimmed
    std::optional<std::int32_t> get_int(std::string_view sec, std::string_view key) const;
    std::optional<float> get_float(std::string_view sec, std::string_view key) const;   // atof
    std::optional<std::string> get_string(std::string_view sec, std::string_view key) const;   // quotes stripped

    // Writes replace the value of an existing line in place (the key's spelling kept) or append the
    // key at the end of its section (a new section goes at the end of the file after a blank line;
    // a root key goes before the first section).
    void set_raw(std::string_view sec, std::string_view key, std::string_view value);
    void set_int(std::string_view sec, std::string_view key, std::int32_t v, bool hex = false);  // FUN_005d06f0
    void set_float(std::string_view sec, std::string_view key, float v);          // "%0.8f"
    void set_string(std::string_view sec, std::string_view key, std::string_view v);  // FUN_005d04d0
    bool erase(std::string_view sec, std::string_view key);

    std::vector<std::string> lines;   // without line ends
};

}  // namespace cleria
