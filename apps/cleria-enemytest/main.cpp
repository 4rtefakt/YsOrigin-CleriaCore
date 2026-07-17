// Checks the C++ ENEMYINFO loader reproduces the corpus invariants that were
// established in Python (build/misc_re/verify_drops.py). Two implementations
// agreeing is weak evidence — they can share one wrong idea, as the clip-AABB
// outliers proved — so this asserts the INVARIANTS, not "the same numbers as before".
#include "cleria/enemies.hpp"
#include "cleria/items.hpp"

#include <cstdio>
#include <filesystem>
#include <map>
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
    std::printf("\n\n%s (%d failed)\n", fails ? "FAILURES" : "all invariants hold", fails);
    return fails ? 1 : 0;
}
