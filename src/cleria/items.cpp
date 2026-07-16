#include "cleria/items.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace cleria {
namespace fs = std::filesystem;

namespace {
std::string cstr(const std::vector<std::uint8_t>& b, std::size_t at, std::size_t cap) {
    if (at >= b.size()) return {};
    cap = std::min(cap, b.size() - at);
    const char* p = reinterpret_cast<const char*>(b.data() + at);
    std::size_t n = 0;
    while (n < cap && p[n] != '\0') ++n;
    return std::string(p, n);
}
}  // namespace

ItemTable ItemTable::load(const fs::path& path) {
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

    t.items.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t o = 16 + std::size_t(i) * rec_size;
        if (o + rec_size > b.size()) break;
        Item& it = t.items[i];
        it.name = cstr(b, o + 0x00, 0x20);
        it.icon = cstr(b, o + 0x34, 0x10);
        it.description = cstr(b, o + 0x4C, 0x6C);
        std::memcpy(&it.unk24, b.data() + o + 0x24, 4);
        std::memcpy(&it.grid_x, b.data() + o + 0x3C, 4);
        std::memcpy(&it.grid_y, b.data() + o + 0x40, 4);
    }
    return t;
}

const Item* ItemTable::get(int id) const {
    if (id < 0 || std::size_t(id) >= items.size()) return nullptr;
    return &items[std::size_t(id)];
}

std::string ItemTable::name(int id) const {
    const Item* it = get(id);
    if (it && !it->name.empty()) return it->name;
    char buf[32];
    std::snprintf(buf, sizeof buf, "item 0x%X", id);
    return buf;
}

fs::path ItemTable::icon_path(const fs::path& root, int id, bool dim) const {
    const Item* it = get(id);
    if (!it || it->icon.empty()) return {};
    // The stem is stored lower-case ("sw_00"); the extracted tree is upper-case.
    std::string stem = it->icon;
    for (char& c : stem) c = char(std::toupper((unsigned char)c));
    if (dim) return root / "MENU" / "ICON2" / (stem + "V.DDS");
    return root / "MENU" / "ICON" / (stem + ".DDS");
}

}  // namespace cleria
