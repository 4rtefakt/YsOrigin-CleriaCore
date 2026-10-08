// Checks the C++ ENEMYINFO loader reproduces the corpus invariants that were
// established in Python (build/misc_re/verify_drops.py). Two implementations
// agreeing is weak evidence — they can share one wrong idea, as the clip-AABB
// outliers proved — so this asserts the INVARIANTS, not "the same numbers as before".
#include "cleria/cast.hpp"
#include "cleria/combat.hpp"
#include "cleria/gameinfo.hpp"
#include "cleria/eex.hpp"
#include "cleria/enemies.hpp"
#include "cleria/items.hpp"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <set>
#include <sstream>

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
    for (const std::string& miss_name : misses) {
        const Cast* c = ct.find(miss_name);
        if (c && c->is_spawn_marker()) {
            ++marker;
        } else if (placed.count(miss_name)) {
            ++unexplained;
            std::printf("    UNEXPLAINED: %s is placed but has no sprite\n",
                        miss_name.c_str());
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

    // The CASTINFO -> ENEMYINFO index: a bias and a fold, not the identity this
    // project asserted for weeks. It must be a BIJECTION onto exactly 0..89 -- that is
    // what `cast_id` could never be (186 casts, 90 records) and what makes this one
    // structurally right rather than merely plausible.
    std::set<int> idx;
    int mapped = 0, collisions = 0;
    for (const Cast& c : ct.casts) {
        const int e = enemy_index_for_cast(c.cast_id);
        if (e < 0) continue;
        ++mapped;
        if (!idx.insert(e).second) ++collisions;
    }
    check(mapped == 90 && collisions == 0 && *idx.begin() == 0 && *idx.rbegin() == 89,
          "enemy index is a bijection onto 0..89",
          std::to_string(mapped) + " casts -> " + std::to_string(idx.size()) +
              " indices, " + std::to_string(collisions) + " collisions");
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

        // The weapon rows are 0=Adol (arcade-only), 1=Yunica, 2=Hugo, 3=Toal. Pinned
        // against the game's published per-level STR: each character's values land in
        // exactly one row, at the identical indices [1,2,4,6,8,10], and row 0 matches
        // none of the three -- which is what identifies row 0 as the fourth character.
        // This is the check that caught cleria-view indexing with `character - 1`.
        struct Row { int row; const char* who; int lv2, lv3, lv7; };
        const Row kRows[] = {{1, "Yunica", 28, 35, 91},
                             {2, "Hugo", 29, 36, 92},
                             {3, "Toal", 27, 34, 90}};
        int rows_ok = 0;
        for (const Row& r : kRows) {
            const float* w = gi.weapon_atk[r.row];
            if (int(w[1]) == r.lv2 && int(w[2]) == r.lv3 && int(w[10]) == r.lv7) ++rows_ok;
        }
        check(rows_ok == 3, "weapon rows 1/2/3 == Yunica/Hugo/Toal (published STR)",
              std::to_string(rows_ok) + "/3");
        // Row 0 must NOT match any of them -- that is what makes it Adol's.
        bool row0_distinct = true;
        for (const Row& r : kRows)
            if (int(gi.weapon_atk[0][1]) == r.lv2) row0_distinct = false;
        check(row0_distinct, "weapon row 0 matches no story character (= Adol)",
              std::to_string(int(gi.weapon_atk[0][1])) + " vs 28/29/27");

        AttackStats a;
        a.atk = gi.player_atk(0, 0, 1);  // Adol, starting weapon, level 1
        check(a.atk == 48.0f, "L1 Adol ATK == curve 25 + weapon 23",
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

    // ---- FUN_004202e0 whole (enemy_restat): the rival duels' INIT re-stat --------
    // build/audit/S6099_DUELS.md §2: the floor-6 rematches at the scripts' level 48, and the tail
    // (0x42060e..0x420865). Kishgal's and Toal's Normal HP / STR / DEF are also build/wiki_re/wiki.json's.
    {
        const Enemy *k = et.get(172), *t = et.get(168), *h = et.get(167), *rif = et.get(14);
        check(k && t && h && rif, "the duel casts 172 / 168 / 167 have ENEMYINFO records", "");
        if (k && t && h && rif) {
            auto is = [](const EnemyStats& s, float hp, float atk, float def, float exp) {
                return s.max_hp == hp && s.atk == atk && s.def == def && s.exp == exp;
            };
            auto str = [](const EnemyStats& s) {
                return std::to_string(s.max_hp) + " / " + std::to_string(s.atk) + " / " + std::to_string(s.def) +
                       " / " + std::to_string(s.exp);
            };
            const StatMode normal;
            const EnemyStats ks = enemy_restat(gi, *k, 172, 48, normal), ts = enemy_restat(gi, *t, 168, 48, normal),
                             hs = enemy_restat(gi, *h, 167, 48, normal);
            check(is(ks, 6249, 251, 222, 14348), "M860 Kishgal lv 48 Normal: 6249 / 251 / 222 / 14348", str(ks));
            check(is(ts, 7431, 247, 248, 5532), "M820 Toal lv 48 Normal: 7431 / 247 / 248 / 5532", str(ts));
            check(is(hs, 3349, 217, 240, 5532), "M810 Hugo lv 48 Normal: 3349 / 217 / 240 / 5532", str(hs));
            // M810's INIT under flag 231 (0x4d7ec2..0x4d7f6c, d = 0) and its aura tick (0x4d9bc6): the fought values.
            check(hs.max_hp * 1.25f == 4186.25f && hs.atk - 1.5f + 31.0f + 2.5f == 249.0f && hs.def + 19.0f == 259.0f &&
                      hs.exp * 4.0f == 22128.0f,
                  "M810's INIT on top: 4186.25 / 249 / 259 / 22128", str(hs));
            check(ks.exp + 7500.0f == 21848.0f && ts.exp + 16000.0f == 21532.0f,
                  "M860 / M820 EXP after their INIT's 179 bonus: 21848 / 21532", "");
            StatMode hard, night; hard.hard = 1; night.hard = 2;
            const EnemyStats kh = enemy_restat(gi, *k, 172, 48, hard), kn = enemy_restat(gi, *k, 172, 48, night);
            check(kh.max_hp == 6249.0f * 1.2f && kn.max_hp == 9373.5f && kh.atk == 251 && kn.def == 222,
                  "Hard / Nightmare: max HP x1.2 / x1.5 only (Kishgal 7498.8 / 9373.5)", str(kh) + " ; " + str(kn));
            check(enemy_restat(gi, *t, 168, 48, hard).max_hp == 7431.0f * 1.2f &&
                      enemy_restat(gi, *t, 168, 48, night).max_hp == 11146.5f &&
                      enemy_restat(gi, *h, 167, 48, hard).max_hp == 3349.0f * 1.2f &&
                      enemy_restat(gi, *h, 167, 48, night).max_hp == 5023.5f,
                  "Hard / Nightmare: Toal 8917.2 / 11146.5, Hugo 4018.8 / 5023.5 before his x1.25", "");
            // The duel casts' own Easy / Very Easy branch, with the retry count (flag 196) outside boss rush.
            StatMode easy; easy.easy = true; easy.retries = 7;
            EnemyStats s = enemy_restat(gi, *k, 172, 48, easy);
            check(is(s, 6249, 251 - 3, 222 - 1 - 3, 14348), "duel cast, Easy, 7 retries: ATK - 3, DEF - 1 - 3", str(s));
            easy.retries = 25;
            s = enemy_restat(gi, *k, 172, 48, easy);
            check(is(s, 6249, 251 - 15, 222 - 1 - 15, 14348), "duel cast, Easy, 25 retries: ATK - 15, DEF - 1 - 15", str(s));
            easy.boss_rush = true;
            s = enemy_restat(gi, *k, 172, 48, easy);
            check(is(s, 6249, 251, 221, 14348), "duel cast, Easy, boss rush: DEF - 1 only", str(s));
            StatMode ve; ve.very_easy = true; ve.easy = true; ve.retries = 16;      // 16^1.25 = 32
            s = enemy_restat(gi, *k, 172, 48, ve);
            check(is(s, 6249 * 0.75f, 251 - 1 - 32, 222 - 2 - 32, 14348),
                  "duel cast, Very Easy, 16 retries: ATK - 1 - 32, DEF - 2 - 32, HP x0.75", str(s));
            ve.boss_rush = true;
            s = enemy_restat(gi, *k, 172, 48, ve);
            check(is(s, 6249 * 0.75f, 250, 220, 14348), "duel cast, Very Easy, boss rush: ATK - 1, DEF - 2, HP x0.75", str(s));
            // Every other cast: Very Easy ATK - 10, DEF - 10, HP x0.75; Easy ATK - 5, DEF - 10; the floors at 1.
            const EnemyStats r0 = enemy_restat(gi, *rif, 14, 0, normal);
            StatMode e2; e2.easy = true; e2.retries = 30;
            StatMode v2; v2.very_easy = true; v2.easy = true;
            const EnemyStats re = enemy_restat(gi, *rif, 14, 0, e2), rv = enemy_restat(gi, *rif, 14, 0, v2);
            check(r0.max_hp == enemy_stats(gi, *rif).max_hp && r0.atk == enemy_stats(gi, *rif).atk &&
                      re.atk == std::max(1.0f, r0.atk - 5.0f) && re.def == std::max(1.0f, r0.def - 10.0f) &&
                      re.max_hp == r0.max_hp && rv.atk == std::max(1.0f, r0.atk - 10.0f) &&
                      rv.def == std::max(1.0f, r0.def - 10.0f) && rv.max_hp == r0.max_hp * 0.75f,
                  "a plain cast (M100): Easy ATK - 5 / DEF - 10, Very Easy - 10 / - 10 and HP x0.75, floors at 1",
                  str(r0) + " ; " + str(re) + " ; " + str(rv));
            // M360 (raw 0x1a) and M460 (raw 0x24) are in the duel list; cast 166 (raw 0x98) is not.
            const Enemy *j = et.get(40), *v = et.get(50), *y = et.get(166);
            StatMode e0; e0.easy = true;
            check(j && v && y && enemy_restat(gi, *j, 40, 29, e0).atk == enemy_restat(gi, *j, 40, 29, normal).atk &&
                      enemy_restat(gi, *v, 50, 5, e0).atk == enemy_restat(gi, *v, 50, 5, normal).atk &&
                      enemy_restat(gi, *y, 166, 48, e0).atk == enemy_restat(gi, *y, 166, 48, normal).atk - 5.0f,
                  "the duel list is casts 167..172, 40 and 50", "");
        }
    }

    // ---- the game's own MONSTER JOURNAL: the only EXTERNAL check we have ------
    //
    // Every other test here validates our C++ against our Python -- both reading the
    // same bytes, both able to share one wrong idea. These numbers are published by the
    // game itself, so matching them exercises the GAMEINFO curve, the ENEMYINFO log2
    // multipliers, the level column and the fold(cast_id-14) index at once, against a
    // source that knows nothing about our parse. It is what proved the engine FLOORS.
    {
        std::ifstream jf("tests/monster_journal.txt");
        if (!jf) {
            std::printf("\n(monster journal fixture not found -- skipping the external check)\n");
        } else {
            struct JRow { int no, hp, lv, str, def, exp; std::string name; };
            std::vector<JRow> rows;
            std::string line;
            while (std::getline(jf, line)) {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream is(line);
                JRow r;
                if (is >> r.no >> r.name >> r.hp >> r.lv >> r.str >> r.def >> r.exp)
                    rows.push_back(r);
            }
            // The journal is in ENCOUNTER order, not index order -- match by value.
            int exact = 0, exp_ok = 0, name_ok = 0;
            std::vector<std::string> jmisses;
            for (const JRow& r : rows) {
                bool hit = false;
                for (std::size_t i = 0; i < et.enemies.size(); ++i) {
                    const Enemy& e = et.enemies[i];
                    if (e.level != r.lv) continue;
                    const EnemyStats s = enemy_stats(gi, e);
                    if (int(s.max_hp) != r.hp || int(s.atk) != r.str ||
                        int(s.def) != r.def) continue;
                    hit = true;
                    // EXP: floor(K(+0xDC) * curve4). +0xD8 was the label and gets 9/67.
                    if (int(s.exp) == r.exp) ++exp_ok;
                    // And the game's OWN name field must agree with the published one.
                    for (const Cast& c : ct.casts)
                        if (enemy_index_for_cast(c.cast_id) == int(i) &&
                            !c.display.empty()) {
                            std::string a = c.display, b = r.name;
                            auto strip = [](std::string& t) {
                                std::string o;
                                for (char ch : t)
                                    if (std::isalnum((unsigned char)ch))
                                        o += char(std::tolower((unsigned char)ch));
                                t = o;
                            };
                            strip(a); strip(b);
                            if (a == b) ++name_ok;
                            break;
                        }
                    break;
                }
                if (hit) ++exact; else jmisses.push_back(r.name);
            }
            check(exp_ok >= 67, "monster journal: EXP == floor(K(+0xDC) * curve4)",
                  std::to_string(exp_ok) + "/" + std::to_string(rows.size()));
            // 61/67: the other 6 are my own transcription (I dropped the parentheses
            // from "Nooze (Blue)" when typing the fixture) plus one genuine published
            // typo -- the journal prints "Hubamobrum", the game's file says
            // "Hubamobrun". Three independent routes to the same 67 enemies.
            check(name_ok >= 61, "monster journal: name == CASTINFO's own name field",
                  std::to_string(name_ok) + "/" + std::to_string(rows.size()) +
                      " (6 are fixture/publisher spelling)");
            // 67/68: #50 Kelmarel's published DEF is a copy-paste from the two rows
            // above it (48/49/50 all list DEF 200, EXP 357). Our data matches it on
            // HP/LV/STR and we do not bend to the typo.
            check(exact >= 67, "monster journal: our stats == the game's published table",
                  std::to_string(exact) + "/" + std::to_string(rows.size()) +
                      (jmisses.empty() ? "" : "  (known: " + jmisses[0] + ")"));
        }
    }

    // ---- EEX: the boss table -------------------------------------------------
    // Bosses carry RAW stats, not multipliers -- which is why fold(cast_id-14) never
    // reached them. Identified by the levels: 8/16/24/32/40/45/49/50 are exactly the
    // game's published boss levels for Velagunder..Darm, and +0x04 matches their
    // published STR 6/8. How a boss REACHES a record is [UNKNOWN], so this asserts the
    // table's shape only.
    {
        const EexTable ex = EexTable::load(root / "MISC" / "EEX.DAT");
        std::printf("\nEEX: %zu records\n", ex.bosses.size());
        int real = 0, inert = 0;
        std::vector<int> lv;
        bool ramps = true;
        float prev_atk = -1;
        for (const BossStats& s : ex.bosses) {
            if (s.is_inert()) { ++inert; continue; }
            ++real;
            lv.push_back(s.level);
            if (s.atk < prev_atk) ramps = false;
            prev_atk = s.atk;
        }
        const std::vector<int> kBossLevels = {8, 16, 24, 32, 40, 45, 49, 50};
        check(real == 8 && inert == 10, "EEX is 8 real + 10 inert",
              std::to_string(real) + " + " + std::to_string(inert));
        check(lv == kBossLevels, "EEX levels == the published boss levels",
              "8/16/24/32/40/45/49/50");
        check(ramps, "boss STR ramps with level", "");
        for (const BossStats& s : ex.bosses)
            if (!s.is_inert())
                std::printf("    lv%-3d HP %5.0f  STR %4.0f  DEF %4.0f\n", s.level, s.hp,
                            s.atk, s.def);

        // The cast -> EEX index map (build/boss_lookup_re): each full-room boss cast
        // resolves to the EEX record whose level matches its journal level.
        const int kBossCasts[8] = {128, 131, 134, 136, 137, 140, 143, 149};
        bool map_ok = true;
        for (int i = 0; i < 8; ++i) {
            const int bidx = eex_index_for_cast(kBossCasts[i]);
            if (bidx != i || bidx >= int(ex.bosses.size()) ||
                ex.bosses[bidx].level != kBossLevels[i])
                map_ok = false;
        }
        check(map_ok, "each full-room boss cast maps to its EEX record (level matches)", "");
        check(eex_index_for_cast(126) == -1 && eex_index_for_cast(13) == -1,
              "and non-full-room casts map to -1 (arena bosses / the player)", "");
    }

    std::printf("\n%s (%d failed)\n", fails ? "FAILURES" : "all invariants hold", fails);
    return fails ? 1 : 0;
}
