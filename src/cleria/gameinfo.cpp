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
EnemyStats enemy_stats(const GameInfo& gi, const Enemy& e) { return enemy_stats(gi, e, e.level); }

// FUN_004202e0's level argument (non-zero: that level; 0 = the record's own, EI+0xe8).
EnemyStats enemy_stats(const GameInfo& gi, const Enemy& e, int level) {
    EnemyStats s;
    if (!gi.ok()) return s;
    if (level == 0) level = e.level;
    s.max_hp = std::floor(stat_k(e.k_maxhp) * gi.curves[kEnemyMaxHp].at(level));
    s.atk = std::floor(stat_k(e.k_atk) * gi.curves[kEnemyAtk].at(level));
    s.def = std::floor(stat_k(e.k_def) * gi.curves[kEnemyDef].at(level));
    s.exp = std::floor(stat_k(e.k_exp) * gi.curves[kEnemyExp].at(level));
    // +0xc4 [BYTES 0x420505..0x4205cb]: level <= 30: pow(5.0, level / 15) * 4.0 (an unsigned integer division,
    // 0x42050a..0x420519; 0x6bd5f0, 0x6bd7c0); else level * 10 - 200; times pow(2, (EI+0xd8 - 50) / 20); floor.
    const float sp_base = level <= 30 ? float(std::pow(5.0, double(float(unsigned(level) / 15u)))) * 4.0f
                                      : float(level * 10 - 200);
    s.sp = int(std::floor(stat_k(e.k_unk_0xd8) * sp_base));
    return s;
}

// FUN_004202e0 whole: the curve stats above, then its tail (0x42060e..0x420865).
EnemyStats enemy_restat(const GameInfo& gi, const Enemy& e, int cast_id, int level, const StatMode& m) {
    EnemyStats s = enemy_stats(gi, e, level);
    if (!gi.ok()) return s;
    // 0x42060e: g_flags[144] > 0 -> max HP *= the float table at 0x689f80 [flag] (1.2, 1.5).
    if (m.hard == 1) s.max_hp *= 1.2f;
    else if (m.hard == 2) s.max_hp *= 1.5f;
    // 0x420638..0x420674: ebx (the raw index, cast - 14) in 0x99..0x9e (casts 167..172), 0x1a (M360), 0x24 (M460).
    const unsigned raw = unsigned(cast_id) - 14u;
    const bool duel = (raw >= 0x99u && raw <= 0x9eu) || raw == 0x1au || raw == 0x24u;
    if (!duel) {
        if (m.very_easy) { s.atk -= 10.0f; s.def -= 10.0f; }        // 0x42067f
        else if (m.easy) { s.atk -= 5.0f; s.def -= 10.0f; }         // 0x4206a9
    } else if (m.very_easy) {                                       // 0x4206cf
        s.def -= 2.0f; s.atk -= 1.0f;
        if (!m.boss_rush) {                                         // 0x420735: pow((double)(float)n, 1.25), twice
            const float pw = float(std::pow(double(float(m.retries)), 1.25));
            s.atk -= pw; s.def -= pw;
        }
    } else if (m.easy) {                                            // 0x4206fb
        s.def -= 1.0f;
        if (!m.boss_rush) {                                         // 0x4207c9: n < 20 ? n >> 1 : n - 10
            const float n = float(m.retries < 20 ? m.retries >> 1 : m.retries - 10);
            s.atk -= n; s.def -= n;
        }
    }
    if (m.very_easy) s.max_hp *= 0.75f;                             // 0x420822
    if (1.0f >= s.atk) s.atk = 1.0f;                                // 0x420845
    if (1.0f >= s.def) s.def = 1.0f;                                // 0x420854
    return s;
}

}  // namespace cleria
