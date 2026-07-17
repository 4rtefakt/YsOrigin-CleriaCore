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
