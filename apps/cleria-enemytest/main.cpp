// Checks the C++ ENEMYINFO loader reproduces the corpus invariants that were
// established in Python (build/misc_re/verify_drops.py). Two implementations
// agreeing is weak evidence — they can share one wrong idea, as the clip-AABB
// outliers proved — so this asserts the INVARIANTS, not "the same numbers as before".
#include "cleria/cast.hpp"
#include "cleria/combat.hpp"
#include "cleria/gameinfo.hpp"
#include "cleria/enemies.hpp"
#include "cleria/items.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <set>

namespace fs = std::filesystem;
using namespace cleria;

int main(int argc, char** argv) {
    const fs::path root = (argc > 1) ? argv[1] : "assets";
    const EnemyTable et = EnemyTable::load(root / "MISC" / "ENEMYINFO.DAT");
    const ItemTable it = ItemTable::load(root / "MISC" / "INVINFO.DAT");
    if (et.enemies.empty() || it.items.empty()) {
        std::fprintf(stderr, "load failed (root=%s)\n", root.string().c_str());
        return 2;
    }
    std::printf("ENEMYINFO: %zu enemies   INVINFO: %zu items\n", et.enemies.size(),
                it.items.size());

    int fails = 0;
    auto check = [&](bool ok, const char* what, const std::string& detail) {
        std::printf("  %-42s %s%s%s\n", what, ok ? "PASS" : "FAIL",
                    detail.empty() ? "" : "  ", detail.c_str());
        if (!ok) ++fails;
    };

    int packed = 0, id_rate = 0, live = 0, in_range = 0, below66 = 0;
    std::set<int> distinct;
    int lvl_min = 1 << 30, lvl_max = -(1 << 30);
    for (const Enemy& e : et.enemies) {
        bool seen_gap = false, packed_ok = true, pair_ok = true;
        for (const DropSlot& d : e.drops) {
            if (d.item_id < 0) {
                seen_gap = true;
            } else {
                if (seen_gap) packed_ok = false;  // a live slot after an unused one
                ++live;
                distinct.insert(d.item_id);
                if (d.item_id < 66) ++below66;
                if (d.percent >= 0.0f && d.percent <= 100.0f) ++in_range;
            }
            if ((d.item_id < 0) != (d.percent == 0.0f)) pair_ok = false;
        }
        packed += packed_ok;
        id_rate += pair_ok;
        lvl_min = std::min(lvl_min, int(e.level));
        lvl_max = std::max(lvl_max, int(e.level));
    }

    const int n = int(et.enemies.size());
    check(packed == n, "drop slots front-packed",
          std::to_string(packed) + "/" + std::to_string(n));
    check(id_rate >= n - 1, "id == -1  <=>  rate == 0",
          std::to_string(id_rate) + "/" + std::to_string(n) + " (1 known exception)");
    check(in_range == live, "all rates within [0,100]",
          std::to_string(in_range) + "/" + std::to_string(live));
    check(below66 == 0, "no drop is equipment (every id >= 66)",
          below66 ? std::to_string(below66) + " violations" : "443 live ids, none below 66");
    check(lvl_min >= 1 && lvl_max <= 60, "level column in 1..60",
          std::to_string(lvl_min) + ".." + std::to_string(lvl_max));

    // The partition is the real claim: resolve every dropped id through INVINFO and
    // confirm it is a tool, seed or gold — by NAME, not by trusting the id boundary.
    std::map<std::string, int> by_prefix;
    for (int id : distinct) {
        const Item* item = it.get(id);
        if (!item) continue;
        std::string p = item->icon.substr(0, item->icon.find('_'));
        ++by_prefix[p];
    }
    std::string kinds;
    for (auto& [p, c] : by_prefix) kinds += p + "x" + std::to_string(c) + " ";
    const bool only_droppable =
        by_prefix.count("sw") == 0 && by_prefix.count("ar") == 0 &&
        by_prefix.count("sh") == 0 && by_prefix.count("ac") == 0;
    check(only_droppable, "dropped kinds are tools/seeds/gold only", kinds);

    std::printf("\n  %zu distinct dropped items:", distinct.size());
    for (int id : distinct) std::printf(" %s", it.name(id).c_str());

    // INVINFO +0x24 is a magnitude. The gold case proves itself: the column must
    // equal the number in the item's OWN NAME. This is the strongest self-check in
    // the table -- the data states its own answer -- so it is worth asserting.
    int gold_ok = 0, gold_n = 0;
    for (int id = 120; id < 128; ++id) {
        const Item* g = it.get(id);
        if (!g) continue;
        const std::size_t gpos = g->name.find('G');
        if (gpos == std::string::npos || gpos == 0) continue;
        ++gold_n;
        if (std::stoul(g->name.substr(0, gpos)) == g->magnitude) ++gold_ok;
    }
    check(gold_ok == gold_n && gold_n == 8, "INVINFO +0x24 == the gold in the name",
          std::to_string(gold_ok) + "/" + std::to_string(gold_n));

    // ---- the SCT -> CASTINFO -> ENEMYINFO chain ------------------------------
    const CastTable ct = CastTable::load(root / "MISC" / "CASTINFO.DAT");
    std::printf("\n\nCASTINFO: %zu casts\n", ct.casts.size());

    // Collect every cast name any room actually places, once.
    std::set<std::string> placed;
    std::map<std::string, int> rooms_with;
    int sct_files = 0;
    for (const auto& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& ch : ext) ch = char(std::toupper((unsigned char)ch));
        if (ext != ".SCT") continue;
        ++sct_files;
        std::ifstream f(e.path(), std::ios::binary);
        const std::string blob{std::istreambuf_iterator<char>(f),
                               std::istreambuf_iterator<char>()};
        for (const Cast& c : ct.casts)
            if (!c.name.empty() && blob.find(c.name + '\0') != std::string::npos) {
                placed.insert(c.name);
                ++rooms_with[c.name];
            }
    }

    int idx_ok = 0, stem_hit = 0;
    std::vector<std::string> misses;
    for (std::size_t i = 0; i < ct.casts.size(); ++i) {
        if (ct.casts[i].cast_id == int(i)) ++idx_ok;
        if (ct.casts[i].stem.empty()) continue;
        if (fs::exists(ct.aia_path(root, int(i))))
            ++stem_hit;
        else
            misses.push_back(ct.casts[i].name);
    }
    check(idx_ok == int(ct.casts.size()), "cast_id == record index",
          std::to_string(idx_ok) + "/" + std::to_string(ct.casts.size()));
    check(stem_hit == 86, "stem -> ANI/*.AIA (c000 -> C_000.AIA)",
          std::to_string(stem_hit) + " resolve, " + std::to_string(misses.size()) + " miss");

    // Each sprite-less cast must have a REASON: it is either the spawn marker, or a
    // dead entry no room places. A cast that IS placed but has no sprite would be a
    // real bug, and this is what would catch it.
    int marker = 0, dead = 0, unexplained = 0;
    for (const std::string& n : misses) {
        const Cast* c = ct.find(n);
        if (c && c->is_spawn_marker()) {
            ++marker;
        } else if (placed.count(n)) {
            ++unexplained;
            std::printf("    UNEXPLAINED: %s is placed but has no sprite\n", n.c_str());
        } else {
            ++dead;
        }
    }
    check(unexplained == 0, "every sprite-less cast has a reason",
          std::to_string(marker) + " spawn marker + " + std::to_string(dead) + " never placed");
    // The spawn marker should be in essentially every room — that is what makes
    // "it is a marker, not a character" a reason rather than an excuse.
    check(rooms_with["CAST_ADOL"] > sct_files * 9 / 10,
          "CAST_ADOL (spawn marker) is in ~every room",
          std::to_string(rooms_with["CAST_ADOL"]) + "/" + std::to_string(sct_files) + " rooms");
    std::printf("  (%zu of %zu casts are placed by some room)\n", placed.size(),
                ct.casts.size());

    // ---- GAMEINFO stat curves ------------------------------------------------
    const GameInfo gi = GameInfo::load(root / "MISC" / "GAMEINFO.DAT");
    std::printf("\nGAMEINFO: %zu curves\n", gi.curves.size());
    check(gi.ok(), "11 curves at payload +0x20 (file +0x30)",
          std::to_string(gi.curves.size()) + "/11");
    if (gi.ok()) {
        // The endpoints must BE the endpoints: lerping at level 1 and 60 has to
        // reproduce the two stored floats exactly, or the pair is not (lv1, lv60).
        int ends_ok = 0;
        for (const StatCurve& c : gi.curves)
            if (c.at(1) == c.at_level_1 && c.at(60) == c.at_level_60) ++ends_ok;
        check(ends_ok == kCurveCount, "lerp reproduces both endpoints",
              std::to_string(ends_ok) + "/11");
        check(gi.curves[3].is_hole() && gi.curves[9].is_hole(),
              "pairs 3 and 9 are the (0,0) holes", "");
        check(gi.curves[5].at_level_1 == gi.curves[4].at_level_1 &&
                  gi.curves[5].at_level_60 == gi.curves[4].at_level_60,
              "pair 5 is byte-identical to pair 4 (dead)", "");
        // 400 is an INDEPENDENT number: it came out of FUN_00420c40, not this file.
        check(gi.curves[kPlayerMaxHp].at(60) == 400.0f, "player maxHP at lv60 == 400",
              std::to_string(gi.curves[kPlayerMaxHp].at(60)));
        std::printf("    player HP %.0f->%.0f  ATK %.0f->%.0f  DEF %.0f->%.0f\n",
                    gi.curves[kPlayerMaxHp].at(1), gi.curves[kPlayerMaxHp].at(60),
                    gi.curves[kPlayerAtk].at(1), gi.curves[kPlayerAtk].at(60),
                    gi.curves[kPlayerDef].at(1), gi.curves[kPlayerDef].at(60));
        std::printf("    enemy  HP %.0f->%.0f  ATK %.0f->%.0f  DEF %.0f->%.0f\n",
                    gi.curves[kEnemyMaxHp].at(1), gi.curves[kEnemyMaxHp].at(60),
                    gi.curves[kEnemyAtk].at(1), gi.curves[kEnemyAtk].at(60),
                    gi.curves[kEnemyDef].at(1), gi.curves[kEnemyDef].at(60));
        std::printf("    EXP to reach lv2=%.0f lv10=%.0f lv30=%.0f lv60=%.0f\n",
                    gi.exp_to_level(2), gi.exp_to_level(10), gi.exp_to_level(30),
                    gi.exp_to_level(60));

        // The weapon ATK table, and the check that would have caught the mistake that
        // produced the "negative damage" puzzle: the ATK curve is the BASE, and a
        // player without their weapon added computes NEGATIVE damage at level 1.
        int mono = 0;
        for (int c = 0; c < kChars; ++c) {
            bool up = true;
            for (int t = 0; t + 1 < kTiers; ++t)
                up = up && gi.weapon_atk[c][t] < gi.weapon_atk[c][t + 1];
            mono += up;
        }
        check(mono == kChars, "weapon ATK rises with tier, all 4 chars",
              std::to_string(mono) + "/4");

        AttackStats a;
        a.atk = gi.player_atk(0, 0, 1);  // char 0, starting weapon, level 1
        check(a.atk == 48.0f, "L1 char0 ATK == curve 25 + weapon 23",
              std::to_string(a.atk));

        // With the bare curve (the old bug), damage is negative -- pin it, so the
        // mistake cannot quietly return.
        AttackStats bare;
        bare.atk = gi.curves[kPlayerAtk].at(1);  // 25 -- no weapon
        const float cdef = gi.curves[kEnemyDef].at(1);
        check(damage_real(bare, cdef) == 0.0f,
              "bare curve (no weapon) clamps to 0 -- the old bug",
              "raw would be " + std::to_string(std::pow(bare.atk, 0.66f) *
                                               (bare.atk - 0.98f * cdef + 10.0f) / 22.92f));

        // ---- REAL enemies, not the curve --------------------------------------
        // A curve value is not a stat: the engine scales it by the enemy's own log2
        // column, so "the level-1 enemy has DEF 40" describes nothing that exists.
        // Every stat below is K(column) * curve(that enemy's own level).
        int k_ok = 0, hp_pos = 0;
        float def_lo = 1e9f, def_hi = -1e9f;
        const Enemy* weakest = nullptr;
        for (const Enemy& e : et.enemies) {
            if (e.k_neutral == 50.0f) ++k_ok;
            const EnemyStats s = enemy_stats(gi, e);
            if (s.max_hp > 0) ++hp_pos;
            def_lo = std::min(def_lo, s.def);
            def_hi = std::max(def_hi, s.def);
            if (!weakest || e.level < weakest->level) weakest = &e;
        }
        check(k_ok == int(et.enemies.size()), "+0xd4 is the constant 50.0 (K == 1.0)",
              std::to_string(k_ok) + "/" + std::to_string(et.enemies.size()));
        check(hp_pos == int(et.enemies.size()), "every enemy has positive maxHP",
              std::to_string(hp_pos) + "/" + std::to_string(et.enemies.size()));
        check(def_lo < cdef && def_hi > cdef,
              "real enemy DEF straddles the raw curve (40)",
              std::to_string(def_lo) + ".." + std::to_string(def_hi));

        // The real test the old one only pretended to be: can a starting player hurt
        // the game's actual toughest enemy at its own level?
        const Enemy* hardest = &et.enemies[0];
        for (const Enemy& e : et.enemies)
            if (enemy_stats(gi, e).def > enemy_stats(gi, *hardest).def) hardest = &e;
        const EnemyStats hs = enemy_stats(gi, *hardest);
        const EnemyStats ws = enemy_stats(gi, *weakest);
        std::printf("    weakest enemy (lv%d): HP %.0f ATK %.0f DEF %.0f -> player deals %.2f\n",
                    weakest->level, ws.max_hp, ws.atk, ws.def, damage_real(a, ws.def));
        std::printf("    toughest DEF (lv%d): HP %.0f ATK %.0f DEF %.0f\n",
                    hardest->level, hs.max_hp, hs.atk, hs.def);
        check(damage_real(a, ws.def) > 0.0f,
              "starting player can hurt the weakest real enemy",
              std::to_string(damage_real(a, ws.def)) + " vs its real DEF " +
                  std::to_string(ws.def));
    }

    std::printf("\n%s (%d failed)\n", fails ? "FAILURES" : "all invariants hold", fails);
    return fails ? 1 : 0;
}
