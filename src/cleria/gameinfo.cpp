#include "cleria/gameinfo.hpp"
#include "cleria/vfs.hpp"   // mods: asset opens go through vfs::resolve

#include "cleria/enemies.hpp"

#include <cmath>
#include <cstring>
#include <fstream>

namespace cleria {
namespace fs = std::filesystem;

GameInfo GameInfo::load(const fs::path& path) {
    GameInfo g;
    std::ifstream f(vfs::resolve(path), std::ios::binary);
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

// stat = FLOOR(K(the enemy's own column) * the shared curve at the enemy's own level).
//
// ** FLOOR, not round. ** This said `round` and was wrong in 59 of 68 cases. The
// engine's rounding helper FUN_004217f0 is `floorf` -- which a separate pass had
// already PROVEN (FUN_00671000 is floor(double), via `006710d6: FLDZ; RET` for
// 0<x<1) while the stat pass's pseudocode said "round(...)" and nobody reconciled
// the two. The game's own monster journal settles it: with floor, 67 of 68 published
// enemies match exactly on HP, level, STR and DEF; with round, 9.
EnemyStats enemy_stats(const GameInfo& gi, const Enemy& e) {
    EnemyStats s;
    if (!gi.ok()) return s;
    s.max_hp = std::floor(stat_k(e.k_maxhp) * gi.curves[kEnemyMaxHp].at(e.level));
    s.atk = std::floor(stat_k(e.k_atk) * gi.curves[kEnemyAtk].at(e.level));
    s.def = std::floor(stat_k(e.k_def) * gi.curves[kEnemyDef].at(e.level));
    s.exp = std::floor(stat_k(e.k_exp) * gi.curves[kEnemyExp].at(e.level));
    return s;
}

}  // namespace cleria
