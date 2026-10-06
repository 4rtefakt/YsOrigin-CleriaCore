#include "cleria/scenelist.hpp"
#include "cleria/vfs.hpp"   // mods: asset opens go through vfs::resolve

#include <cstring>
#include <fstream>
#include <iterator>

namespace cleria {
namespace fs = std::filesystem;

namespace {
constexpr std::size_t kStride = 0x1c8;  // 456
constexpr std::size_t kHeader = 8;      // u32 unknown; u32 count

char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

// A fixed-width char field: NUL-terminated, but never read past the record.
std::string field(const char* rec, std::size_t off, std::size_t max) {
    const char* p = rec + off;
    std::size_t n = 0;
    while (n < max && p[n] != '\0') ++n;
    return std::string(p, n);
}

// Last non-empty component of a "s_10\s_1001\" style path, lower-cased.
std::string leaf(const std::string& path) {
    std::size_t end = path.size();
    while (end > 0 && (path[end - 1] == '\\' || path[end - 1] == '/')) --end;
    std::size_t beg = end;
    while (beg > 0 && path[beg - 1] != '\\' && path[beg - 1] != '/') --beg;
    std::string s = path.substr(beg, end - beg);
    for (char& c : s) c = lower(c);
    return s;
}
}  // namespace

bool SceneList::load(const fs::path& asset_root) {
    std::ifstream f(vfs::resolve(asset_root / "MAP" / "SCENELIST.SL"), std::ios::binary);
    if (!f) return false;
    const std::string blob((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (blob.size() < kHeader) return false;

    std::uint32_t count = 0;
    std::memcpy(&count, blob.data() + 4, 4);
    // Trust the file's own length over the header if the two disagree.
    const std::size_t avail = (blob.size() - kHeader) / kStride;
    if (count > avail) count = std::uint32_t(avail);

    recs_.clear();
    recs_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const char* r = blob.data() + kHeader + std::size_t(i) * kStride;
        SceneRec s;
        std::memcpy(&s.index, r, 2);
        s.reverb = std::uint8_t(r[2]);
        s.bgm = std::uint8_t(r[3]);
        s.path = field(r, 0x04, 0xc0);
        s.display = field(r, 0xc4, kStride - 0xc4);
        recs_.push_back(std::move(s));
    }
    return !recs_.empty();
}

const SceneRec* SceneList::find_room(std::string_view room) const {
    std::string want(room);
    for (char& c : want) c = lower(c);
    for (const SceneRec& s : recs_)
        if (!s.path.empty() && leaf(s.path) == want) return &s;
    return nullptr;
}

}  // namespace cleria
