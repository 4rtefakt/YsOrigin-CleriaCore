#include "cleria/enemies.hpp"

#include <cstring>
#include <fstream>

namespace cleria {
namespace fs = std::filesystem;

EnemyTable EnemyTable::load(const fs::path& path) {
    EnemyTable t;
    std::ifstream f(path, std::ios::binary);
    if (!f) return t;
    std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>()};
    if (b.size() < 16) return t;
    std::uint32_t rec_size, count;
    std::memcpy(&rec_size, b.data() + 8, 4);
    std::memcpy(&count, b.data() + 12, 4);
    // The header must account for the file exactly. A stride that merely fits is how
    // a misaligned parse survives its first look; this table's own header is the only
    // check that costs nothing.
    if (rec_size == 0 || count > 4096 || std::size_t(16) + rec_size * count != b.size())
        return t;

    t.enemies.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t o = 16 + std::size_t(i) * rec_size;
        Enemy& e = t.enemies[i];
        for (int k = 0; k < 8; ++k) {
            std::memcpy(&e.drops[k].item_id, b.data() + o + 0x50 + k * 4, 4);
            std::memcpy(&e.drops[k].percent, b.data() + o + 0x70 + k * 4, 4);
        }
        // i32, NOT f32 — as a float this column is denormal garbage.
        std::memcpy(&e.level, b.data() + o + 0xE8, 4);
        // ...whereas these five ARE f32. Same record, both conventions.
        std::memcpy(&e.k_maxhp, b.data() + o + 0xC8, 4);
        std::memcpy(&e.k_atk, b.data() + o + 0xCC, 4);
        std::memcpy(&e.k_def, b.data() + o + 0xD0, 4);
        std::memcpy(&e.k_neutral, b.data() + o + 0xD4, 4);
        std::memcpy(&e.k_unk_0xd8, b.data() + o + 0xD8, 4);
        std::memcpy(&e.k_exp, b.data() + o + 0xDC, 4);
        // Chase speed = +0x94 * +0x98 (the shared sprite mover's base). See enemies.hpp.
        float s94, s98;
        std::memcpy(&s94, b.data() + o + 0x94, 4);
        std::memcpy(&s98, b.data() + o + 0x98, 4);
        e.move_speed = s94 * s98;
    }
    return t;
}

const Enemy* EnemyTable::get(int cast_id) const {
    if (cast_id < 0 || std::size_t(cast_id) >= enemies.size()) return nullptr;
    return &enemies[std::size_t(cast_id)];
}

}  // namespace cleria
