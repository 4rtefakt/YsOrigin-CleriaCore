// The first test of the SPINE: real tables -> a real spawned actor -> real damage.
//
// Everything here is downstream of the XSO spawn op (0x12c) as build/entity_re/
// ENTITY.md decompiled it. The point is not that the numbers match a previous run —
// two of our own implementations agreeing is weak evidence, and this project has been
// burned by exactly that. The point is that the ENGINE'S OWN INVARIANTS hold on live
// objects: the id scheme, the player fork, the enemy gate, and a stat block that has
// been validated 67/68 against the game's own monster journal finally arriving on
// something that can be hit.
//
// Until this existed, nothing tested the damage formula against a running enemy,
// because there was no running enemy.
#include "cleria/cast.hpp"
#include "cleria/combat.hpp"
#include "cleria/enemies.hpp"
#include "cleria/gameinfo.hpp"
#include "cleria/sct.hpp"
#include "cleria/sen.hpp"
#include "cleria/world.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <span>
#include <fstream>
#include <iterator>
#include <exception>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace cleria;

int main(int argc, char** argv) {
    const fs::path root = (argc > 1) ? argv[1] : "assets";
    const CastTable ct = CastTable::load(root / "MISC" / "CASTINFO.DAT");
    const EnemyTable et = EnemyTable::load(root / "MISC" / "ENEMYINFO.DAT");
    const GameInfo gi = GameInfo::load(root / "MISC" / "GAMEINFO.DAT");
    if (ct.casts.empty() || et.enemies.empty() || !gi.ok()) {
        std::fprintf(stderr, "load failed (root=%s)\n", root.string().c_str());
        return 2;
    }
    std::printf("CASTINFO: %zu  ENEMYINFO: %zu  GAMEINFO: %zu curves\n\n",
                ct.casts.size(), et.enemies.size(), gi.curves.size());

    int fails = 0;
    auto check = [&](bool ok, const char* what, const std::string& detail = "") {
        std::printf("  %-46s %s%s%s\n", what, ok ? "PASS" : "FAIL",
                    detail.empty() ? "" : "  ", detail.c_str());
        if (!ok) ++fails;
    };

    const WorldTables tables{&ct, &et, &gi};

    // --- the kind word transform (actor.hpp) -------------------------------------
    // The bug this guards: CASTINFO+0x6c is {0,2,4,0x10} and NONE has bit 0x8. If the
    // raw class were stored, the enemy gate would be false for every actor alive.
    check(kind_word(0x10) == 0x88 && (kind_word(0x10) & kKindEnemy) != 0,
          "monster class 0x10 -> kind 0x88, enemy bit set");
    check((kind_word(2) & kKindEnemy) == 0 && (kind_word(4) & kKindEnemy) == 0,
          "classes 2 and 4 are NOT enemies");

    // Find the LOWEST-level monster cast. "First monster" is a trap here: cast 14 is
    // a level-19 enemy, and a level-1 attacker legitimately deals it ZERO damage
    // (the linear term goes negative, min_dmg floors at 0) -- so a naive kill test on
    // it never terminates, and that is the formula being right, not wrong.
    int monster_cast = -1, monster_level = 1 << 30;
    for (const Cast& c : ct.casts) {
        if (!c.is_monster() || enemy_index_for_cast(c.cast_id) < 0) continue;
        const Enemy* e = et.get(c.cast_id);
        if (e && e->level < monster_level) {
            monster_level = e->level;
            monster_cast = c.cast_id;
        }
    }
    check(monster_cast >= 0, "found the lowest-level monster cast",
          "cast " + std::to_string(monster_cast) + " lv" + std::to_string(monster_level));
    if (monster_cast < 0) return 2;

    // --- spawn gives the actor its VALIDATED stats -------------------------------
    {
        ActorWorld w(tables);
        Actor* a = w.spawn({0, monster_cast, {1, 2, 3}, 90.0f, 1});
        check(a != nullptr, "spawn returns an actor");
        if (!a) return 2;

        const Enemy* e = et.get(monster_cast);
        const EnemyStats s = enemy_stats(gi, *e);
        check(a->max_hp == s.max_hp && a->hp == a->max_hp,
              "hp == max_hp == the ENEMYINFO/GAMEINFO stat",
              std::to_string(int(a->hp)));
        check(a->atk == s.atk && a->def == s.def && a->exp_reward == s.exp,
              "atk / def / exp_reward all come from the chain");
        check(a->level == e->level, "level is the enemy's own (+0xe8)",
              "lv" + std::to_string(a->level));
        check(a->alive(), "a fresh enemy is alive (hp >= 1.0, the engine's test)");
        check(a->is_enemy() && !a->is_player(), "it is an enemy and not the player");

        // Degrees -> radians. The op stores DEGREES; a raw store is a 57x error.
        check(a->yaw > 1.57f && a->yaw < 1.58f, "yaw 90 deg -> ~1.5708 rad",
              std::to_string(a->yaw));

        // The group tag, and the overlap quirk that IS the engine's behaviour.
        check(a->tag.tagged() && a->tag.in_group(1), "group 1 tags, and answers group 1");
        SpawnTag t3;
        t3.set(3);
        check(t3.in_group(1) && t3.in_group(2) && t3.in_group(3),
              "group 3 ALSO answers 1 and 2 (bits 8..10, AND-tested)");
    }

    // --- the id scheme ------------------------------------------------------------
    {
        ActorWorld w(tables);
        // Five instances of one cast, ids auto-allocated: the first takes the cast
        // type, the rest come from 186 up. This is EVNT_3002_THOR.XSO's shape.
        std::set<int> ids;
        for (int i = 0; i < 5; ++i) {
            Actor* a = w.spawn({0, monster_cast, {}, 0, 0});
            if (a) ids.insert(a->instance_id);
        }
        check(ids.size() == 5, "five spawns of one cast -> five distinct instance ids",
              std::to_string(ids.size()));
        check(ids.count(monster_cast) == 1, "the first auto-id IS the cast type");
        check(*ids.rbegin() >= kAutoIdBase, "the rest allocate from 186 up",
              std::to_string(*ids.rbegin()));
        check(w.size() == 5, "and five actors exist");

        // An explicit id is REUSED, not duplicated (the op looks up before it allocates).
        const std::size_t before = w.size();
        w.spawn({monster_cast, monster_cast, {}, 0, 0});
        check(w.size() == before, "re-spawning a live id re-points it, adds no actor");
        check(w.find(monster_cast) != nullptr, "find() resolves that id");
        check(w.find(4242) == nullptr, "find() on an absent id returns null");
    }

    // --- the player fork ----------------------------------------------------------
    {
        ActorWorld w(tables);
        check(w.player() == nullptr, "no player before CAST_ADOL spawns");
        Actor* p = w.spawn({0, kIdPlayer, {}, 0, 0});
        check(p && w.player() == p, "spawning cast 13 sets the player");
        // The engine returns at step 7: no stat setup for the player on this path.
        check(p && p->is_player(), "cast 13 is the player");
        // The spawn op returns at step 7 for the player, so the ENEMY chain never
        // runs. Its real HP comes from the player-init path (FUN_00420c40, the
        // GAMEINFO player curve) which the spawn op does NOT call -- so max_hp stays
        // at our default here. (The engine's factory writes a PLACEHOLDER from
        // CASTINFO+0x74, documented as not-real-HP; we don't model that placeholder,
        // and 0 is "not set by this path", not a claim that the player has 0 HP.)
        check(p && p->max_hp == 0,
              "the spawn op sets no HP for the player (enemy chain skipped)");
        check(w.find(kIdPlayerAlias) == p,
              "id 999 resolves to the PLAYER, not to 'not the player'");
        check(w.find(kIdPlayer) == p, "id 13 fast-paths to the player");
    }

    // --- damage lands on a live actor ---------------------------------------------
    // The first time the formula has ever been applied to a spawned enemy.
    {
        ActorWorld w(tables);
        Actor* a = w.spawn({0, monster_cast, {}, 0, 0});
        if (!a) return 2;
        const float start_hp = a->hp;

        // A level-1 player's real ATK: the curve PLUS the starting weapon. Feeding the
        // bare curve here drives the formula negative -- see gameinfo.hpp.
        const float atk = gi.player_atk(1, 0, 1);  // Yunica, tier 0, level 1
        const AttackStats atk_stats{atk, 0.0f, 0.0f};

        // The engine's roll is an inlined xorshift seeded from timeGetTime(), so there
        // is no canonical stream to match; we seed our own and get determinism it
        // never had. Same convention as cleria-view.
        std::mt19937 rng(12345);
        std::uniform_real_distribution<float> pct(0.0f, 100.0f);

        HitResult r;
        int hits = 0;
        while (!r.died && hits < 1000) {
            const int dmg =
                damage_int(atk_stats, a->def, [&](float p) { return pct(rng) < p; });
            r = apply_damage(a->hp, a->flags, dmg);
            ++hits;
        }
        check(hits > 1 && hits < 1000, "a level-1 attacker kills it in a sane hit count",
              std::to_string(hits) + " hits on " + std::to_string(int(start_hp)) + " hp");
        check(r.died && a->dead(), "apply_damage sets the deferred death flag");
        check(!a->alive(), "and it is no longer alive by the engine's own test");
        check(a->hp == 0.0f, "hp is clamped to 0 on death (it went sub-1, not sub-0)");
    }

    // --- outclassed deals ZERO, and that is the formula, not a bug ----------------
    // The same level-1 attacker against a HIGH-level enemy: the linear term goes
    // negative and min_dmg floors it at 0. This is why the kill test above had to seek
    // the lowest level -- the property is real and worth pinning.
    {
        int high_cast = -1, high_level = -1;
        for (const Cast& c : ct.casts) {
            if (!c.is_monster() || enemy_index_for_cast(c.cast_id) < 0) continue;
            const Enemy* e = et.get(c.cast_id);
            if (e && e->level > high_level) { high_level = e->level; high_cast = c.cast_id; }
        }
        ActorWorld w(tables);
        Actor* a = w.spawn({0, high_cast, {}, 0, 0});
        const float atk = gi.player_atk(1, 0, 1);
        std::mt19937 rng(1);
        std::uniform_real_distribution<float> pct(0.0f, 100.0f);
        int total = 0;
        for (int i = 0; i < 50; ++i)
            total += damage_int(AttackStats{atk, 0, 0}, a->def,
                                [&](float p) { return pct(rng) < p; });
        check(total == 0, "level-1 vs the highest-level enemy deals 0 (min_dmg floor)",
              "lv" + std::to_string(high_level) + " DEF " + std::to_string(int(a->def)));
    }

    // --- END TO END: a REAL room script spawns REAL actors ------------------------
    // The whole point of the milestone. EVNT_3002_THOR.XSO is the file ENTITY.md
    // cites as spawning five instances of cast 35 (ids 167..171). We run it through
    // the VM with the world as host and see whether the decoder + host wiring turn
    // its bytes into live actors with real stats. A dynamic run may branch past some
    // spawns (flags are all 0 here), so we assert what a from-cold run robustly
    // produces, and PRINT the roster so a human can compare to the static count.
    {
        const fs::path xso = root / "MAP" / "S_30" / "S_3002" / "EVNT_3002_THOR.XSO";
        std::vector<std::uint8_t> bytes;
        if (std::ifstream f{xso, std::ios::binary}) {
            bytes.assign(std::istreambuf_iterator<char>(f), {});
        }
        check(!bytes.empty(), "loaded EVNT_3002_THOR.XSO");

        if (!bytes.empty()) {
            ActorWorld w(tables);
            XsoVm vm;
            vm.host = &w;  // <-- the connection: a running script drives the world
            try {
                const Xso script = Xso::parse(bytes);
                vm.run(script);
            } catch (const std::exception& ex) {
                check(false, "parsed and ran the script", ex.what());
            }

            std::printf("    [script spawned %zu actor(s)]\n", w.size());
            std::map<int, int> by_cast;
            std::set<int> ids;
            for (const Actor& a : w.actors()) { by_cast[a.cast_id]++; ids.insert(a.instance_id); }
            for (auto& [cast, count] : by_cast) {
                const Cast* c = ct.get(cast);
                std::printf("      cast %d x%d  %s\n", cast, count,
                            c ? c->display.c_str() : "?");
            }
            std::string idlist;
            for (int id : ids) idlist += std::to_string(id) + " ";
            std::printf("      instance ids: %s\n", idlist.c_str());

            check(w.size() > 0, "the VM decoded op 0x12c and the host spawned actors");
            // Independent corroboration: ENTITY.md's STATIC scan (xso_spawns.py) found
            // this file spawns five instances of cast 35 as ids 167..171. A DYNAMIC VM
            // run reaching the same roster is two methods agreeing, not one repeated.
            check(by_cast.size() == 1 && by_cast.count(35) && by_cast[35] == 5,
                  "5 instances of cast 35 -- matches ENTITY.md's static scan");
            check(ids == std::set<int>({167, 168, 169, 170, 171}),
                  "instance ids are exactly 167..171 -- the explicit ids in the script");
            // Every spawned enemy carries stats from the chain -- the end-to-end
            // proof that spawn wiring and the validated stat pipeline meet.
            bool all_enemies_statted = true;
            for (const Actor& a : w.actors())
                if (a.is_enemy() && a.max_hp <= 0) all_enemies_statted = false;
            check(all_enemies_statted, "every spawned enemy got real stats");
            // is_alive through the host, on a real spawned actor.
            if (w.size() > 0) {
                const int id0 = w.actors().front().instance_id;
                check(w.is_alive(id0) == w.actors().front().alive(),
                      "host is_alive() agrees with the actor's own test");
            }
        }
    }

    // --- the tick, the deferred sweep, order-preserving removal -------------------
    // The lifecycle structure from build/actor_re/ACTOR.md: player-first update, cached
    // count, destruction deferred to a frame-boundary sweep, order-preserving removal.
    {
        ActorWorld w(tables);
        Actor* p = w.spawn({0, kIdPlayer, {}, 0, 0});
        std::vector<int> spawn_order;
        for (int i = 0; i < 5; ++i) {
            Actor* a = w.spawn({0, monster_cast, {}, 0, 0});
            if (a) spawn_order.push_back(a->instance_id);
        }
        check(w.size() == 6, "player + 5 enemies live", std::to_string(w.size()));

        // A step must not remove anything -- destruction is deferred, not inline.
        w.step();
        check(w.size() == 6, "step() alone removes nothing (destruction is deferred)");

        // Kill the middle enemy (index 2 of the 5). Requesting destroy does not remove.
        int nth = 0, victim_id = -1;
        for (Actor& a : w.actors()) {
            if (a.is_enemy() && nth++ == 2) { w.request_destroy(a); victim_id = a.instance_id; }
        }
        check(w.size() == 6, "request_destroy does not remove immediately");
        check(w.find(victim_id) != nullptr, "the doomed actor is still findable pre-sweep");

        // The frame-boundary sweep removes exactly it, order-preserving.
        const std::size_t removed = w.sweep_dead();
        check(removed == 1 && w.size() == 5, "sweep removes exactly the requested one");
        check(w.find(victim_id) == nullptr, "and it is gone after the sweep");

        // Survivors keep spawn order (the removed id excised, the rest in sequence).
        std::vector<int> survivors;
        for (const Actor& a : w.actors())
            if (a.is_enemy()) survivors.push_back(a.instance_id);
        std::vector<int> expected;
        for (int id : spawn_order) if (id != victim_id) expected.push_back(id);
        check(survivors == expected, "order-preserving removal keeps spawn order");

        // The player pointer survives an unrelated sweep (list keeps references stable).
        check(w.player() == p, "player pointer stays valid across a middle erase");

        // Sweeping the player drops the pointer (engine re-scans; we have one).
        w.request_destroy(*p);
        w.sweep_dead();
        check(w.player() == nullptr, "sweeping the player nulls the player pointer");
    }

    // --- a REAL room's SCT populates the world (cleria-view's exact path) ----------
    // cleria-view now spawns enemies by routing SCT placements through ActorWorld::spawn
    // -- the SCT consumer FUN_004341a0 is a real actor creation site, so this is
    // faithful. --play can't run headlessly (it needs a window), so this exercises that
    // path GPU-free: SCT name -> CastTable::find -> cast_id -> spawn, same as the view.
    {
        const fs::path scts = root / "MAP" / "S_10" / "S_1001" / "S_1001.SCT";
        std::vector<std::uint8_t> bytes;
        if (std::ifstream f{scts, std::ios::binary})
            bytes.assign(std::istreambuf_iterator<char>(f), {});
        check(!bytes.empty(), "loaded S_1001.SCT (a room with monster placements)");

        if (!bytes.empty()) {
            const Sct sct = Sct::parse(bytes);
            ActorWorld w(tables);
            int placed = 0, statted = 0;
            for (const CastPlacement& c : sct.casts) {
                if (c.is_player()) continue;   // the view drives the player itself
                const Cast* cc = ct.find(c.name);
                if (!cc) continue;
                Actor* a = w.spawn(SpawnRequest{0, cc->cast_id, c.position, 0.0f, 0});
                if (!a) continue;
                ++placed;
                if (a->is_enemy() && a->max_hp > 0) ++statted;
            }
            check(placed > 0, "SCT placements spawned into the world",
                  std::to_string(placed) + " actors");
            check(statted > 0, "and enemies among them carry real stats",
                  std::to_string(statted) + " statted");
            check(w.size() == std::size_t(placed), "world holds exactly the spawned actors");
        }
    }

    // --- FULL room populate: run S_xxxx.XSO -> op 0xa0 (SCT) + op 0x12c (scripted) ----
    // The faithful populate (build/roomload_re/ROOMLOAD.md): a room script runs op 0xa0
    // ONCE (bulk SCT spawn) then op 0x12c 0..N times (scripted). This proves both fire
    // through a world-connected VM -- and specifically that a HUB (S_1000, whose SCT is
    // just the player) gets its enemies from 0x12c, which the SCT-only path missed.
    auto read = [&](const fs::path& p) {
        std::vector<std::uint8_t> b;
        if (std::ifstream f{p, std::ios::binary})
            b.assign(std::istreambuf_iterator<char>(f), {});
        return b;
    };
    // run_script=false gives the SCT-only baseline (op 0xa0 alone); true adds op 0x12c.
    auto populate = [&](const char* dir, const char* stem, bool run_script) -> std::size_t {
        const fs::path base = root / "MAP" / dir / stem / stem;  // .../S_3004/S_3004
        const std::vector<std::uint8_t> sct_b = read(fs::path(base) += ".SCT");
        const std::vector<std::uint8_t> xso_b = read(fs::path(base) += ".XSO");
        if (sct_b.empty() || xso_b.empty()) return 0;
        static Sct sct;              // must outlive the run (world holds a pointer)
        sct = Sct::parse(sct_b);
        ActorWorld w(tables);
        w.set_placements(&sct);      // op 0xa0's source
        if (run_script) {
            XsoVm vm;
            vm.host = &w;
            vm.g_flags[150] = 1;     // character = Yunica; state-gated spawns branch on it
            try { vm.run(Xso::parse(xso_b)); } catch (const std::exception&) {}
        } else {
            w.spawn_placements();    // op 0xa0 alone
        }
        return w.size();
    };

    // S_3004: pure-SCT (0x12c count = 0), so the script and the SCT-only path agree.
    const std::size_t n3004 = populate("S_30", "S_3004", true);
    check(n3004 >= 20 && n3004 <= 24, "S_3004 (pure-SCT) populates from op 0xa0",
          std::to_string(n3004) + " actors");
    // S_1000 is a hub: its SCT holds only the player, so almost everything comes from
    // op 0x12c. The exact count is STATE-GATED (story flags 0 => a fresh-game subset,
    // not the 146 static ops), so we don't assert 146 -- we assert the script adds
    // actors the SCT-only path cannot, which is the whole point of this milestone.
    const std::size_t n1000_sct = populate("S_10", "S_1000", false);
    const std::size_t n1000_full = populate("S_10", "S_1000", true);
    check(n1000_sct <= 1, "S_1000's SCT alone is ~empty (just the player)",
          std::to_string(n1000_sct) + " from op 0xa0");
    check(n1000_full > n1000_sct + 3,
          "and running the script adds op-0x12c scripted spawns SCT-only missed",
          std::to_string(n1000_full) + " with the script");

    // --- SEN arrival position (build/warp_re/WARP.md, verified) -------------------
    // The exit's arrival position is at +0x22c; our parser reads it. S_1001's exit to
    // S_1000 arrives at (0, 8.5, 52) -- verified against the raw bytes. (WARP.md's
    // arrival-FACING claim did NOT verify -- +0x238 is 0 in all 367 records -- so no
    // facing is parsed; this checks only the position, which does hold.)
    {
        const std::vector<std::uint8_t> b =
            read(root / "MAP" / "S_10" / "S_1001" / "S_1001.SEN");
        check(!b.empty(), "loaded S_1001.SEN");
        if (!b.empty()) {
            const Sen sen = Sen::parse(std::span<const std::uint8_t>(b.data(), b.size()));
            const SenTrigger* to1000 = nullptr;
            for (const SenTrigger& t : sen.triggers)
                if (t.dest_scene == "S_1000") to1000 = &t;
            check(to1000 != nullptr, "S_1001 has an exit to S_1000");
            if (to1000) {
                const Vec3 a = to1000->arrival_pos;
                check(to1000->has_arrival() && std::fabs(a.y - 8.5f) < 0.5f &&
                          std::fabs(a.z - 52.0f) < 0.5f,
                      "and its arrival lands at the reciprocal door (+0x22c)",
                      "(" + std::to_string(int(a.x)) + "," + std::to_string(int(a.y)) +
                          "," + std::to_string(int(a.z)) + ")");
            }
        }
    }

    std::printf("\n%s (%d failed)\n", fails ? "FAILURES" : "all invariants hold", fails);
    return fails ? 1 : 0;
}
