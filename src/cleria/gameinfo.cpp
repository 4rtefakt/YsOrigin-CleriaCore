#include "cleria/gameinfo.hpp"

#include <cstring>
#include <fstream>

namespace cleria {
namespace fs = std::filesystem;

GameInfo GameInfo::load(const fs::path& path) {
    GameInfo g;
    std::ifstream f(path, std::ios::binary);
    if (!f) return g;
    std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>()};
    // 16-byte header, then the payload the engine keeps at BSS 0x0074e690. Curves are
    // at PAYLOAD +0x20, hence file +0x30.
    const std::size_t base = 16 + 0x20;
    if (b.size() < base + kCurveCount * 8) return g;

    g.curves.resize(kCurveCount);
    for (int i = 0; i < kCurveCount; ++i) {
        std::memcpy(&g.curves[i].at_level_1, b.data() + base + i * 8, 4);
        std::memcpy(&g.curves[i].at_level_60, b.data() + base + i * 8 + 4, 4);
    }

    // The weapon ATK table at FILE +0xa0 (note: file, not payload -- this offset is
    // quoted from the raw file in the disassembly). Row stride 0x30 = 11 floats plus
    // one pad; 0xa0 + 4*0x30 == 352 accounts for the file exactly.
    const std::size_t wbase = 0xA0;
    if (b.size() >= wbase + kChars * 0x30) {
        for (int c = 0; c < kChars; ++c)
            for (int t = 0; t < kTiers; ++t)
                std::memcpy(&g.weapon_atk[c][t], b.data() + wbase + c * 0x30 + t * 4, 4);
    }
    return g;
}

double GameInfo::exp_to_level(int level) const {
    if (!ok() || level <= 1) return 0.0;
    const StatCurve& c = curves[kPlayerExp];
    double total = 0.0;
    for (int i = 0; i < level - 1; ++i)
        total += double(c.at_level_1) +
                 (double(c.at_level_60) - double(c.at_level_1)) * (double(i) / 59.0);
    return total;
}

}  // namespace cleria
