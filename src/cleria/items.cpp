#include "cleria/items.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace cleria {

ItemTable ItemTable::load(const std::filesystem::path& path) {
    ItemTable t;
    std::ifstream f(path, std::ios::binary);
    if (!f) return t;
    std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>()};
    if (b.size() < 16) return t;
    std::uint32_t rec_size, count;
    std::memcpy(&rec_size, b.data() + 8, 4);
    std::memcpy(&count, b.data() + 12, 4);
    if (rec_size == 0 || count > 4096) return t;
    t.names.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t off = 16 + std::size_t(i) * rec_size;
        if (off >= b.size()) break;
        const char* p = reinterpret_cast<const char*>(b.data() + off);
        std::size_t n = 0;
        while (n < rec_size && off + n < b.size() && p[n] != '\0') ++n;
        t.names[i].assign(p, n);
    }
    return t;
}

std::string ItemTable::name(int id) const {
    if (id >= 0 && id < int(names.size()) && !names[id].empty()) return names[id];
    char buf[16];
    std::snprintf(buf, sizeof buf, "item 0x%X", id);
    return buf;
}

}  // namespace cleria
