// Falcom NI/NA archives — the game's `release\data.ni/.na` (+ the `data_us` language overlay).
// See build/shell_re/GAMEDATA.md for the format and the layering. [DATA]: the reader's output
// was byte-compared against the Python-extracted assets/ tree (every entry, see that doc).
//
//   .ni  index: 16-byte header {"NNI\0", u32 n_entries, u32 names_size, u32 flags}, then the
//        TOC (n_entries x 16 bytes) and the names blob (names_size bytes), each encrypted on
//        its own with the multiplicative stream cipher (the key restarts per section):
//            k = k * 0x3D09 (mod 2^32), plain = cipher - (k >> 16)   k0 = 0x7C53F961
//        TOC entry = 4 x u32 {hash, size, pos, namepos}: `hash` ascending (16-bit values),
//        `size` = the bytes STORED in the .na (for `.z` entries that includes the 8-byte prefix,
//        NOT the unpacked size — the sum of all sizes is exactly the .na length), `pos` = offset
//        in the .na, `namepos` = offset of a NUL-terminated CP932 path ("MAP\S_10\...").
//   .na  the files back to back. `.z` entries: u32 crc32 (of the unpacked bytes), u32 unpacked
//        size, then a zlib stream; others are stored raw.
//   .nu  (not needed here) one 20-byte record per TOC entry: {u32 hash, SYSTEMTIME}.
#pragma once
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cleria::ni {

inline constexpr std::uint32_t kCipherInit = 0x7C53F961u;
inline constexpr std::uint32_t kCipherMul = 0x3D09u;

// Decrypts one index section in place (the key starts at kCipherInit for every section).
void decrypt(std::uint8_t* p, std::size_t n);

// CP932 (Shift-JIS, the Microsoft variant) <-> UTF-8. Invalid input becomes U+FFFD / '?'.
std::string cp932_to_utf8(std::string_view s);
std::string utf8_to_cp932(std::string_view s);

// The on-disk name tools/extract_assets.py gives an archive path: '\' -> '/', the trailing
// ".z" dropped (case-insensitive), and each of <>:"|?* replaced by '_' (the archive has names
// like "Z_TREE\H\?????.DDS.Z" that Windows cannot create). Input and output are UTF-8.
std::string clean_name(std::string_view utf8_path);

struct Entry {
    std::string raw;        // the stored CP932 path, e.g. "MAP\S_00\S_0003\EVT_0001_HUGO.XSO.Z"
    std::string name;       // clean_name(cp932_to_utf8(raw)): "MAP/S_00/S_0003/EVT_0001_HUGO.XSO"
    std::uint32_t hash = 0;
    std::uint32_t size = 0; // stored bytes in the .na
    std::uint32_t pos = 0;
    bool compressed = false;
};

class Archive {
  public:
    // Reads and decrypts `<stem>.ni`; the data file is the sibling `<stem>.na`.
    bool open(const std::filesystem::path& ni_path, std::string* err = nullptr);

    const std::vector<Entry>& entries() const { return entries_; }
    const std::filesystem::path& ni_path() const { return ni_; }
    const std::filesystem::path& na_path() const { return na_; }
    std::uint64_t na_size() const { return na_size_; }

    // By clean name or raw path; case-insensitive, '/' and '\' equivalent, ".z" optional.
    const Entry* find(std::string_view name) const;

    // Convenience: opens the .na for this one read.
    bool read(const Entry& e, std::vector<std::uint8_t>& out, std::string* err = nullptr) const;

  private:
    std::filesystem::path ni_, na_;
    std::uint64_t na_size_ = 0;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, std::size_t> by_key_;
};

// Lowercase, '/'-separated, ".z"-less key used by Archive::find and the overlay merge.
std::string lookup_key(std::string_view name);

// One open handle on an archive's .na, for repeated reads (one per thread).
class NaReader {
  public:
    explicit NaReader(const Archive& a);
    ~NaReader();
    NaReader(const NaReader&) = delete;
    NaReader& operator=(const NaReader&) = delete;
    bool ok() const { return f_ != nullptr; }
    // Unpacks `e` into `out`. `.z` entries are inflated and their crc32 and size checked. A
    // zero-size entry reads as an empty file: data.ni has 56 (52 `.z` ones, all at pos 0, which
    // tools/extract_assets.py wrongly filled with the bytes at pos 0 = ANI/C_010.ASO, + 4 stored).
    bool read(const Entry& e, std::vector<std::uint8_t>& out, std::string* err = nullptr);

  private:
    const Archive& a_;
    std::FILE* f_ = nullptr;
    std::vector<std::uint8_t> buf_;
};

}  // namespace cleria::ni
