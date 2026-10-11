#include "cleria/cast.hpp"
#include "cleria/vfs.hpp"   // mods: asset opens go through vfs::resolve

#include "cleria/physics_player.hpp"

#include <algorithm>
#include <cctype>
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

CastTable CastTable::load(const fs::path& path) {
    CastTable t;
    std::ifstream f(vfs::resolve(path), std::ios::binary);
    if (!f) return t;
    std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>()};
    if (b.size() < 16) return t;
    std::uint32_t rec_size, count;
    std::memcpy(&rec_size, b.data() + 8, 4);
    std::memcpy(&count, b.data() + 12, 4);
    // The header must account for the file exactly — a stride that merely fits is
    // how a misaligned parse survives its first look.
    if (rec_size == 0 || count > 4096 || std::size_t(16) + rec_size * count != b.size())
        return t;

    t.casts.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t o = 16 + std::size_t(i) * rec_size;
        Cast& c = t.casts[i];
        c.name = cstr(b, o + 0x00, 0x10);
        c.display = cstr(b, o + 0x10, 0x14);
        c.stem = cstr(b, o + 0x24, 0x10);
        c.description = cstr(b, o + 0x78, 0x100);
        c.description2 = cstr(b, o + 0x178, 0x88);
        std::memcpy(&c.cast_id, b.data() + o + 0x60, 4);
        std::memcpy(&c.radius_base, b.data() + o + 0x34, 4);
        std::memcpy(&c.height_base, b.data() + o + 0x38, 4);
        std::memcpy(&c.shadow_size, b.data() + o + 0x3C, 4);
        std::memcpy(&c.speed_base, b.data() + o + 0x48, 4);
        std::memcpy(&c.lift, b.data() + o + 0x44, 4);
        std::memcpy(&c.hurt_radius, b.data() + o + 0x30, 4);
        std::memcpy(&c.hurt_half_h, b.data() + o + 0x58, 4);
        std::memcpy(&c.hurt_shape, b.data() + o + 0x5C, 4);
        std::memcpy(&c.jump_v0, b.data() + o + 0x50, 4);
        std::memcpy(&c.run_mult, b.data() + o + 0x4C, 4);
        std::memcpy(&c.center_offset, b.data() + o + 0x54, 4);
        std::memcpy(&c.ai_index, b.data() + o + 0x64, 4);
        std::memcpy(&c.actor_class, b.data() + o + 0x6C, 4);
    }
    return t;
}

const Cast* CastTable::get(int cast_id) const {
    if (cast_id < 0 || std::size_t(cast_id) >= casts.size()) return nullptr;
    return &casts[std::size_t(cast_id)];
}

const Cast* CastTable::find(std::string_view name) const {
    for (const Cast& c : casts)
        if (c.name == name) return &c;
    return nullptr;
}

std::string CastTable::sprite_stem(int cast_id) const {
    const Cast* c = get(cast_id);
    if (!c) return {};
    // "c000" -> "C_000". The stored stem is not the filename: uppercase it and
    // insert an underscore after the first character.
    std::string s = c->stem;
    if (s.empty()) {
        // The C2xx dialogue NPCs (Eolia=CAST_C250, Dino=CAST_C280, Roy=CAST_C210, ...) store
        // NO stem in CASTINFO+0x24, but their body sprite exists name-derived: CAST_C250 ->
        // C_250.AIA. Recover it from the SCT name after the "CAST_" prefix. (The M8xx casts
        // carry a real stem, so they never reach this path.)
        if (c->name.rfind("CAST_", 0) == 0 && c->name.size() > 6) s = c->name.substr(5);
        else return {};
    }
    for (char& ch : s) ch = char(std::toupper((unsigned char)ch));
    s.insert(1, "_");
    return s;
}

fs::path CastTable::aia_path(const fs::path& root, int cast_id) const {
    const std::string s = sprite_stem(cast_id);
    if (s.empty()) return {};
    return root / "ANI" / (s + ".AIA");
}

CastMotion cast_motion(const CastTable& ct, int cast_id) {
    CastMotion m;
    if (const Cast* c = ct.get(cast_id)) {
        m.radius_base = c->radius_base;
        m.height_base = c->height_base;
        m.speed_base = c->speed_base;
        m.jump_v0 = c->jump_v0;
        m.run_mult = c->run_mult;
        m.center_offset = c->center_offset;
    }
    return m;
}

}  // namespace cleria
