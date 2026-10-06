#include "cleria/eex.hpp"
#include "cleria/vfs.hpp"   // mods: asset opens go through vfs::resolve

#include <cstring>
#include <fstream>

namespace cleria {
namespace fs = std::filesystem;

EexTable EexTable::load(const fs::path& path) {
    EexTable t;
    std::ifstream f(vfs::resolve(path), std::ios::binary);
    if (!f) return t;
    std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>()};
    if (b.size() < 16) return t;
    std::uint32_t rec_size, count;
    std::memcpy(&rec_size, b.data() + 8, 4);
    std::memcpy(&count, b.data() + 12, 4);
    // The header must account for the file exactly.
    if (rec_size == 0 || count > 4096 || std::size_t(16) + rec_size * count != b.size())
        return t;

    t.bosses.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t o = 16 + std::size_t(i) * rec_size;
        BossStats& s = t.bosses[i];
        std::memcpy(&s.hp, b.data() + o + 0x00, 4);
        std::memcpy(&s.atk, b.data() + o + 0x04, 4);
        std::memcpy(&s.def, b.data() + o + 0x08, 4);
        std::memcpy(&s.unk10, b.data() + o + 0x10, 4);
        std::memcpy(&s.unk14, b.data() + o + 0x14, 4);
        // i32. As a float this is a denormal -- the same trap that hid ENEMYINFO's
        // level column, and the one that made a prior pass reject this whole table.
        std::memcpy(&s.level, b.data() + o + 0x18, 4);
    }
    return t;
}

}  // namespace cleria
