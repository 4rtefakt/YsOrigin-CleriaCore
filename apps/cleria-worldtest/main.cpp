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
#include "cleria/aia.hpp"
#include "cleria/cast.hpp"
#include "cleria/combat.hpp"
#include "cleria/eex.hpp"
#include "cleria/enemies.hpp"
#include "cleria/gameinfo.hpp"
#include "cleria/ef2.hpp"
#include "cleria/scene.hpp"
#include "cleria/sct.hpp"
#include "cleria/sen.hpp"
#include "cleria/world.hpp"
#include "cleria/xso.hpp"

#include <array>
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
    const EexTable ex = EexTable::load(root / "MISC" / "EEX.DAT");
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

    const WorldTables tables{&ct, &et, &gi, &ex};

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

    // --- the render pairing resolves REAL sprites (cast_id -> M_100 -> AIA on disk) --
    // cleria-view pairs each spawned actor with a sprite via CastTable::sprite_stem
    // (cast_id -> "M_100"), then loads ANI/M_100.AIA. This can't be checked with a GPU
    // headlessly, but the RESOLUTION can: for a real room's spawned enemies, the stem
    // must be non-empty and the .AIA must exist. Proves the view's rendering path finds
    // real files, not just that stats are right.
    {
        const std::vector<std::uint8_t> b =
            read(root / "MAP" / "S_10" / "S_1001" / "S_1001.SCT");
        if (!b.empty()) {
            static Sct sct;
            sct = Sct::parse(b);
            ActorWorld w(tables);
            w.set_placements(&sct);
            w.spawn_placements();
            int enemies = 0, resolved = 0;
            for (const Actor& a : w.actors()) {
                if (a.is_player()) continue;
                ++enemies;
                const std::string stem = ct.sprite_stem(a.cast_id);  // "M_100"
                if (!stem.empty() && fs::exists(root / "ANI" / (stem + ".AIA"))) ++resolved;
            }
            check(enemies > 0 && resolved == enemies,
                  "every spawned enemy resolves cast_id -> stem -> an AIA on disk",
                  std::to_string(resolved) + "/" + std::to_string(enemies));
        }
    }

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
            // Bug guard: the S_1004 exit carries a (0,0,0) arrival that is the world
            // ORIGIN, a real door-side spawn -- has_arrival() must still be true (it gates
            // on the label, not on the vector), or a return through this door would fall
            // back to the wrong SCT default. This is the regression the old != 0 gate hit.
            const SenTrigger* to1004 = nullptr;
            for (const SenTrigger& t : sen.triggers)
                if (t.dest_scene == "S_1004") to1004 = &t;
            check(to1004 != nullptr, "S_1001 has an exit to S_1004");
            if (to1004) {
                const Vec3 a = to1004->arrival_pos;
                check(a.x == 0 && a.y == 0 && a.z == 0 && to1004->has_arrival(),
                      "a zero (origin) arrival still overrides spawn (label-gated)",
                      to1004->name);
                // Arrival facing (+0x238): parsed in degrees; both S_1001 exits store 180.
                check(to1004->arrival_facing_deg == 180 && to1004->has_facing(),
                      "arrival facing is parsed and non-zero (S_1004 exit = 180 deg)",
                      std::to_string(to1004->arrival_facing_deg));
            }
        }
    }

    // The SEN facing degree maps to our Actor.yaw (vx=sin, vz=cos, 0 -> +Z) DIRECTLY,
    // no flip -- the engine's deg2rad(90-deg) (build/warp_re/FACING.md) is measured from
    // its own 0 -> +X heading basis, and our yaw basis is 90 off, so the flip cancels.
    // Verified against geometry: this forward points INTO the destination room across 219
    // shipped door pairs (mean alignment 0.81; the un-cancelled axis scores -0.03 and
    // aims at the arrival wall). Lock the cardinals so a future axis flip is caught.
    {
        auto head = [](int deg) {
            const float y = float(deg) * 3.14159265358979323846f / 180.0f;
            return std::pair<float, float>(std::sin(y), std::cos(y));  // (vx, vz)
        };
        auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
        const auto n = head(0), e = head(90), s = head(180), w = head(270);
        check(near(n.first, 0) && near(n.second, 1), "facing 0 deg -> +Z (into-room north)");
        check(near(e.first, 1) && near(e.second, 0), "facing 90 deg -> +X (east)");
        check(near(s.first, 0) && near(s.second, -1), "facing 180 deg -> -Z (south)");
        check(near(w.first, -1) && near(w.second, 0), "facing 270 deg -> -X (west)");
        SenTrigger unset;  // default: no facing
        check(unset.arrival_facing_deg == 0 && !unset.has_facing(),
              "a 0-degree (unset) facing does not override heading");
    }

    // --- the enemy CHASE mover (build/mover2d_re/MOVER2D.md, speed verified) -------
    // An enemy within 25 units turns toward the player and advances at its ENEMYINFO
    // speed. GPU-free proof: place a player at the origin and an enemy 10 units away,
    // tick a second, assert it closed the distance and moved at ~its sourced speed.
    {
        ActorWorld w(tables);
        Actor* p = w.spawn({0, kIdPlayer, {0, 0, 0}, 0, 0});
        Actor* e = w.spawn({0, monster_cast, {10, 0, 0}, 0, 0});
        if (p && e) {
            p->pos = {0, 0, 0};
            e->pos = {10, 0, 0};
            e->yaw = 0;
            check(e->move_speed > 0, "the enemy carries a sourced chase speed",
                  std::to_string(e->move_speed) + " u/tick");
            const float d0 = 10.0f;
            for (int i = 0; i < 60; ++i) w.step();  // one second at 60 Hz
            const float d1 = std::sqrt(e->pos.x * e->pos.x + e->pos.z * e->pos.z);
            check(d1 < d0 - 0.01f, "it moved toward the player over 60 ticks",
                  "dist " + std::to_string(d0) + " -> " + std::to_string(d1));
            // After the brief turn-in, it advances ~move_speed/tick; over 60 ticks it
            // closes on the order of move_speed*60 (looser bound absorbs the turn phase).
            const float closed = d0 - d1;
            check(closed > e->move_speed * 30.0f && closed <= e->move_speed * 61.0f,
                  "closing rate matches the sourced speed (allowing the turn-in)",
                  std::to_string(closed));
            // An enemy with the player OUT of range does not move.
            ActorWorld w2(tables);
            Actor* p2 = w2.spawn({0, kIdPlayer, {0, 0, 0}, 0, 0});
            Actor* e2 = w2.spawn({0, monster_cast, {100, 0, 0}, 0, 0});
            p2->pos = {0, 0, 0}; e2->pos = {100, 0, 0};
            for (int i = 0; i < 10; ++i) w2.step();
            check(e2->pos.x == 100.0f, "an enemy beyond the 25-unit gate stays put");
        }
    }

    // --- full-room boss spawns with EEX stats (build/boss_lookup_re) --------------
    // A class-4 boss cast has no ENEMYINFO record, so the enemy branch skips it; the
    // EEX branch applies raw stats. Velagunder (cast 128) should get EEX record 0.
    if (!ex.bosses.empty()) {
        ActorWorld w(tables);
        Actor* b = w.spawn({0, 128, {}, 0, 0});  // C620 Velagunder
        check(b != nullptr, "spawned the boss cast 128 (Velagunder)");
        if (b) {
            check(!b->is_enemy(), "a full-room boss is class 4, NOT a kind-8 enemy");
            check(b->level == ex.bosses[0].level && b->max_hp == ex.bosses[0].hp &&
                      b->atk == ex.bosses[0].atk,
                  "and it carries EEX record 0's raw stats",
                  "lv" + std::to_string(b->level) + " HP " + std::to_string(int(b->hp)) +
                      " STR " + std::to_string(int(b->atk)));
        }
    }

    // --- a real room enemy now hits the player for a SANE amount ------------------
    // Regression for the enemy_index bug: EnemyTable::get used cast_id as a direct index
    // instead of the fold, so CAST_M100 (cast 14) got record 14 (lv19, ATK 121) and hit
    // a lv1 player for ~105. Fixed -> it is record 0 (lv1, ATK 38) and hits for ~8.
    {
        static Sct s2;
        const std::vector<std::uint8_t> sb = read(root / "MAP" / "S_10" / "S_1001" / "S_1001.SCT");
        if (!sb.empty()) {
            s2 = Sct::parse(sb);
            ActorWorld w(tables);
            w.set_placements(&s2);
            w.spawn_placements();
            std::mt19937 drng(3);
            std::uniform_real_distribution<float> dp(0.0f, 100.0f);
            const float pdef = gi.curves[kPlayerDef].at(1);
            const Actor* m100 = nullptr;
            for (const Actor& a : w.actors())
                if (a.cast_id == 14) { m100 = &a; break; }  // CAST_M100 Riffligan
            check(m100 && m100->level <= 3 && m100->atk < 60,
                  "S_1001's CAST_M100 spawns at its real low level, not lv19",
                  m100 ? ("lv" + std::to_string(m100->level) + " ATK " +
                          std::to_string(int(m100->atk))) : "not found");
            if (m100) {
                const int d = player_incoming_damage(AttackStats{m100->atk, 0, 0}, pdef,
                    [&](float p) { return dp(drng) < p; });
                check(d < 20, "and it hits the lv1 player for a sane amount, not ~105",
                      std::to_string(d) + " HP");
            }
        }
    }

    // --- player-incoming damage (FUN_004fa740, now implemented) -------------------
    // The player-target branch: same raw shape, two soft floors, 0.5 min, max(1,...).
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> pct(0.0f, 100.0f);
        auto roll = [&](float p) { return pct(rng) < p; };
        const float player_def = gi.curves[kPlayerDef].at(1);  // ~30 at level 1
        // A level-1 enemy (ATK ~40) hitting the level-1 player deals a sane, >=1 amount.
        const int d = player_incoming_damage(AttackStats{40.0f, 0, 0}, player_def, roll);
        check(d >= 1 && d <= 30, "player-incoming damage is sane for a lv1 matchup",
              std::to_string(d) + " (DEF " + std::to_string(int(player_def)) + ")");
        // The soft floor + max(1): even a near-zero attacker deals at least 1.
        const int floor_d = player_incoming_damage(AttackStats{5.0f, 0, 0}, 300.0f, roll);
        check(floor_d >= 1, "and a wildly-outclassed hit still deals >= 1 (the max(1))",
              std::to_string(floor_d));
    }

    // --- AIA reload: one Aia object must decode identically after loading another file.
    // The player sprite swaps C_111<->C_114 on every attack through a single reused Aia;
    // load() once leaked the prior file's pixel-offset table into frame_len, truncating
    // the decode to garbage. This is the regression guard: every harness before it used a
    // fresh Aia per file, which is exactly why nothing caught the corruption in play.
    {
        const fs::path a111 = root / "ANI" / "C_111.AIA";
        const fs::path a114 = root / "ANI" / "C_114.AIA";
        Aia fresh;   // ground truth: a never-reused object
        Aia reused;  // the buggy path: load a second file, then come back
        const bool loads = fresh.load(a111) && reused.load(a111) &&
                           reused.load(a114) && reused.load(a111);
        check(loads, "AIA reload: C_111/C_114 load into one object");
        // Scan EVERY region: the corruption is data-dependent -- only regions whose blob
        // gap the other file's offsets intrude into get truncated, so region 0 alone can
        // (and did) pass while others rot. Compare fresh vs reused across all of them.
        int scanned = 0, mismatch = 0;
        for (std::uint32_t r = 0; loads && r < fresh.region_count(); ++r) {
            AiaFrame ref, got;
            const bool a = fresh.decode_region(r, ref), b = reused.decode_region(r, got);
            if (a != b) { ++mismatch; continue; }
            if (!a) continue;
            ++scanned;
            if (ref.w != got.w || ref.h != got.h || ref.rgba != got.rgba) ++mismatch;
        }
        check(loads && mismatch == 0,
              "every region of a reloaded AIA decodes byte-identically to a fresh one",
              std::to_string(scanned) + " regions, " + std::to_string(mismatch) + " corrupt");
    }

    // --- EXP-to-level curve (build/progress_re/LEVELING.md). The level-up threshold is the
    // running sum of GAMEINFO pair 10 (150 -> 18150 over levels 1..60); EXP is credited at the
    // killing blow (FUN_004fbb40) and the level loops while total >= threshold(level+1), cap 60.
    // Lock the existing gameinfo.hpp::exp_to_level against the RE's closed form so a curve
    // regression is caught: total to REACH level 60 = sum_{i=0..58} lerp(150,18150,i/59) = 530850.
    {
        const double e1 = gi.exp_to_level(1);
        const double e2 = gi.exp_to_level(2);
        const double e60 = gi.exp_to_level(60);
        check(std::fabs(e1) < 1.0, "exp_to_level(1) == 0 (no EXP owed at the start)",
              std::to_string(int(e1)));
        check(std::fabs(e2 - 150.0) < 1.0, "first level-up costs 150 (pair-10 endpoint a)",
              std::to_string(int(e2)));
        check(std::fabs(e60 - 530850.0) < 2.0, "reaching lv60 needs 530850 EXP (RE closed form)",
              std::to_string(int(e60)));
        bool mono = true;
        for (int L = 2; L <= 60; ++L) if (gi.exp_to_level(L) <= gi.exp_to_level(L - 1)) mono = false;
        check(mono, "the EXP curve is strictly increasing over levels 1..60");

        // Replicate the view's level-up loop (award_exp) exactly, to prove the wired
        // progression lands the right level + maxHP: level up while total >= threshold(L+1),
        // cap 60, maxHP recomputed from the curve at the new level.
        auto level_for = [&](double total) {
            int lv = 1;
            while (lv < 60 && total >= gi.exp_to_level(lv + 1)) ++lv;
            return lv;
        };
        check(level_for(0) == 1 && level_for(149) == 1 && level_for(150) == 2,
              "level-up loop: 0/149 EXP -> lv1, 150 EXP -> lv2");
        check(level_for(530849) == 59 && level_for(530850) == 60 && level_for(1e9) == 60,
              "level-up loop: caps at 60 (530850 EXP)");
        const int hp1 = int(std::lround(gi.curves[kPlayerMaxHp].at(1)));
        const int hp60 = int(std::lround(gi.curves[kPlayerMaxHp].at(60)));
        check(hp60 > hp1, "maxHP grows from lv1 to lv60 (the player gets tougher)",
              std::to_string(hp1) + " -> " + std::to_string(hp60));
    }

    std::printf("\n%s (%d failed)\n", fails ? "FAILURES" : "all invariants hold", fails);
    // --- SCT bulk spawn is idempotent (events-on double-run safety) ---------------
    // op 0xa0 uses auto-ids, so running a room root twice (populate run-to-completion
    // + the live coroutine when events are on) would DOUBLE the roster without the
    // spawn_placements guard. Load a real room's SCT and prove a second call is a no-op.
    {
        std::vector<std::uint8_t> sbytes;
        if (std::ifstream f{root / "MAP" / "S_10" / "S_1001" / "S_1001.SCT", std::ios::binary})
            sbytes.assign(std::istreambuf_iterator<char>(f), {});
        const Sct sct = Sct::parse(sbytes);
        ActorWorld w(tables);
        w.set_placements(&sct);
        w.spawn_placements();
        const std::size_t after_first = w.size();
        w.spawn_placements();                        // the redundant second root run
        check(after_first > 0 && w.size() == after_first,
              "spawn_placements is idempotent (no doubled SCT roster on a 2nd run)",
              std::to_string(after_first) + " -> " + std::to_string(w.size()));
        // Explicit-id 0x12c also dedups: same id reuses, not doubles.
        w.spawn({999001, monster_cast, {0, 0, 0}, 0, 1});
        const std::size_t after_spawn = w.size();
        w.spawn({999001, monster_cast, {5, 0, 5}, 0, 1});
        check(w.size() == after_spawn, "explicit-id spawn reuses, not doubles");
    }

    // --- S_1001 barrier: the ward must break ONLY on the mob's death, not on entry --------
    // End-to-end against the REAL ActorWorld host + the REAL room scripts. Reproduces (or
    // refutes) the user's "the barrier event triggers on entry regardless of the kill".
    // The root spawns the barrier mob (0x12C entity 167, cast 14, group 1) only while
    // flag301 != 1, then StartScripts the watcher KekkaiZako_01, which polls
    // Chr_IsSeedLiving(group 1) and — only when it dies — sets flag301=1 and starts the
    // KekkaiBreak_01 cutscene. If the mob fails to spawn as a live group-1 seed, the poll
    // is instantly false and the ward breaks on entry — which THIS test would catch.
    {
        const std::string dir = (root / "MAP" / "S_10" / "S_1001").string() + "/";
        struct FsLoader : ScriptLoader {
            std::string dir;
            std::set<std::string> requested;
            std::shared_ptr<const Xso> load_script(const std::string& name) override {
                std::string up = name;
                for (char& c : up) c = char(std::toupper((unsigned char)c));
                requested.insert(up);
                std::ifstream f(dir + up + ".XSO", std::ios::binary);
                if (!f) return nullptr;
                std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f), {}};
                if (b.size() < 4) return nullptr;
                try { return std::make_shared<Xso>(Xso::parse(b)); }
                catch (const std::exception&) { return nullptr; }
            }
        } loader;
        loader.dir = dir;
        try {
            std::vector<std::uint8_t> sbytes;
            if (std::ifstream f{dir + "S_1001.SCT", std::ios::binary})
                sbytes.assign(std::istreambuf_iterator<char>(f), {});
            const Sct sct = Sct::parse(sbytes);
            std::vector<std::uint8_t> rbytes;
            if (std::ifstream f{dir + "S_1001.XSO", std::ios::binary})
                rbytes.assign(std::istreambuf_iterator<char>(f), {});

            ActorWorld w(tables);
            w.set_placements(&sct);
            ScriptManager mgr;
            mgr.loader = &loader;
            mgr.vm.host = &w;
            mgr.vm.g_flags[150] = 1;     // Yunica
            mgr.vm.g_flags[301] = 0;     // barrier UP (not yet broken)
            mgr.start_root(std::make_shared<Xso>(Xso::parse(rbytes)), "s_1001");

            // Phase A — mob alive: run 60 frames and prove the ward stays up the WHOLE time.
            bool broke_early = false;
            for (int f = 0; f < 60; ++f) { mgr.tick_all(); if (mgr.vm.g_flags[301] != 0) broke_early = true; }
            check(w.any_seed_alive(1),
                  "barrier mob spawns as a live group-1 seed on entry");
            // The group-1 seed must be a VISIBLE enemy (a sprite_stem), else it can't show the
            // red ward glow. Finding: cast 14 -> M_100 (a normal Riffligan), so it IS visible;
            // the "no red glow" is a render issue, not an invisible seed.
            bool seed_visible = false;
            for (const Actor& a : w.actors())
                if (a.tag.in_group(1) && !ct.sprite_stem(a.cast_id).empty()) seed_visible = true;
            check(seed_visible, "the group-1 ward seed has a sprite (visible, can carry the glow)");
            check(!broke_early && mgr.vm.g_flags[301] == 0,
                  "ward does NOT break on entry while the mob lives (flag 301 stays 0)");
            check(loader.requested.count("KEKKAIZAKO_01") > 0,
                  "the barrier watcher KekkaiZako_01 was started");

            // Phase B — kill every group-1 seed; the watcher must then break the ward.
            for (Actor& a : w.actors()) if (a.tag.in_group(1) && a.alive()) a.hp = 0;
            for (int f = 0; f < 60 && mgr.any_live(); ++f) mgr.tick_all();
            check(mgr.vm.g_flags[301] == 1,
                  "flag 301 -> 1 after the barrier mob dies (ward down)");
            check(loader.requested.count("KEKKAIBREAK_01") > 0,
                  "KekkaiBreak_01 break-cutscene starts on the mob's death");
        } catch (const std::exception& e) {
            check(false, "S_1001 barrier end-to-end", e.what());
        }
    }

    // --- Statue MODEL swap: S_SAVEOBJECTCHANGE must hide a DIFFERENT statue variant for the
    // sealed (flag300==0) vs cleansed (flag300==1) state, so the post-cleanse reload shows the
    // cleansed statue. (The view runs this at load_room and hides the losers.) --------------
    {
        const std::string dir = (root / "MAP" / "S_10" / "S_1000").string() + "/";
        try {
            std::vector<std::uint8_t> sb;
            if (std::ifstream f{dir + "S_SAVEOBJECTCHANGE.XSO", std::ios::binary})
                sb.assign(std::istreambuf_iterator<char>(f), {});
            const Xso soc = Xso::parse(sb);
            auto hidden_set = [&](std::int32_t f300) {
                XsoVm vm; vm.g_flags[150] = 1; vm.g_flags[300] = f300;
                vm.reset_run(); vm.run(soc);
                std::string s;
                for (const auto& [name, shown] : vm.obj_visible) if (!shown) { s += name; s += ","; }
                return s;
            };
            const std::string sealed = hidden_set(0), cleansed = hidden_set(1);
            check(!sealed.empty() && sealed != cleansed,
                  "statue model differs sealed vs cleansed (S_SAVEOBJECTCHANGE keys on flag 300)",
                  "sealed hides {" + sealed + "} cleansed hides {" + cleansed + "}");
        } catch (const std::exception& e) {
            check(false, "statue model-swap (S_SAVEOBJECTCHANGE)", e.what());
        }
    }

    // --- Goddess-statue CLEANSE (SAVE.XSO): must run to completion and set flag 300 (cleansed).
    // Wiring this to statue-touch requires that it doesn't soft-lock (leave the world frozen). --
    {
        const std::string dir = (root / "MAP" / "S_10" / "S_1000").string() + "/";
        struct FsLoader : ScriptLoader {
            std::string dir;
            std::shared_ptr<const Xso> load_script(const std::string& name) override {
                std::string up = name; for (char& c : up) c = char(std::toupper((unsigned char)c));
                std::ifstream f(dir + up + ".XSO", std::ios::binary);
                if (!f) return nullptr;
                std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f), {}};
                if (b.size() < 4) return nullptr;
                try { return std::make_shared<Xso>(Xso::parse(b)); } catch (const std::exception&) { return nullptr; }
            }
        } loader; loader.dir = dir;
        try {
            std::vector<std::uint8_t> sb;
            if (std::ifstream f{dir + "SAVE.XSO", std::ios::binary}) sb.assign(std::istreambuf_iterator<char>(f), {});
            ActorWorld w(tables);
            ScriptManager mgr; mgr.loader = &loader; mgr.vm.host = &w;
            mgr.vm.g_flags[150] = 1;      // Yunica
            mgr.vm.g_flags[300] = 0;      // statue SEALED
            mgr.vm.g_flags[165] = 1;      // intro/cleanse-tutorial done (gates the cleanse)
            mgr.vm.g_flags[84]  = 1;      // story-progress prereq (0x5D: flag84 || flag86)
            mgr.start_root(std::make_shared<Xso>(Xso::parse(sb)), "save");
            auto save_live = [&] { for (const auto& i : mgr.live) if (i.name == "save") return true; return false; };
            int f = 0; for (; f < 3000 && save_live(); ++f) { w.advance_cutscene_moves(); mgr.tick_all(); }
            check(!save_live() && mgr.vm.g_flags[300] == 1,
                  "statue cleanse SAVE.XSO completes and sets flag 300 (cleansed)",
                  "frames=" + std::to_string(f) + " flag300=" + std::to_string(mgr.vm.g_flags[300]));
            check(!mgr.world_frozen(), "cleanse releases the world freeze (no soft-lock)");
        } catch (const std::exception& e) {
            check(false, "statue cleanse SAVE.XSO run", e.what());
        }
    }

    // --- S_1000 intro battle: the multi-wave KILL-GATE blocks w/o kills, resumes with them --
    // battle_0 is the interactive fight ("mobs spawn, I kill them, the cutscene resumes").
    // It is a multi-wave loop: Fix -> spawn wave (0x12C, cast 14, group 0) -> Unfix ->
    // Wait_FlagEqualInt(132, 0) [op 0xF8] -> next wave. flag 132 = live-enemy count, which
    // the engine (and this test) sync from the world each frame. Two strict scenarios prove
    // the gate is real: (A) never killing anything must LEAVE the script stuck at a gate;
    // (B) killing each wave must let it run to completion (no soft-lock).
    {
        const std::string dir = (root / "MAP" / "S_10" / "S_1000").string() + "/";
        struct FsLoader : ScriptLoader {
            std::string dir;
            std::shared_ptr<const Xso> load_script(const std::string& name) override {
                std::string up = name;
                for (char& c : up) c = char(std::toupper((unsigned char)c));
                std::ifstream f(dir + up + ".XSO", std::ios::binary);
                if (!f) return nullptr;
                std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f), {}};
                if (b.size() < 4) return nullptr;
                try { return std::make_shared<Xso>(Xso::parse(b)); }
                catch (const std::exception&) { return nullptr; }
            }
        } loader;
        loader.dir = dir;
        auto live_count = [](ActorWorld& w) {
            std::int32_t n = 0;
            for (const Actor& a : w.actors()) if (a.is_enemy() && a.alive()) ++n;
            return n;
        };
        try {
            std::vector<std::uint8_t> b0;
            if (std::ifstream f{dir + "BATTLE_0.XSO", std::ios::binary})
                b0.assign(std::istreambuf_iterator<char>(f), {});
            auto battle = std::make_shared<Xso>(Xso::parse(b0));

            // (A) Never kill: the script MUST get stuck at a kill-gate (spawned mobs stay alive).
            {
                ActorWorld w(tables);
                ScriptManager mgr; mgr.loader = &loader; mgr.vm.host = &w;
                mgr.vm.g_flags[150] = 1;
                mgr.start_root(battle, "battle_0");
                bool spawned_any = false;
                for (int f = 0; f < 4000 && mgr.any_live(); ++f) {
                    mgr.vm.g_flags[132] = live_count(w);
                    if (mgr.vm.g_flags[132] > 0) spawned_any = true;
                    mgr.tick_all();
                    w.advance_cutscene_moves();   // scripted NPC walks (Wait_ChrMoving resolves)
                }
                check(spawned_any, "intro battle spawns wave enemies (cast 14, counted)");
                check(mgr.any_live(),
                      "battle STALLS at the kill-gate when nothing is killed (gate is real)");
            }
            // (B) Kill each wave: battle_0 MUST run through to its end (flag 165=1 at pc2028),
            // its root instance retiring. Persistent region triggers it spawns (s_Stop1000_*)
            // legitimately stay live for the room's lifetime, so "all scripts dead" is the
            // WRONG completion test — the signal is battle_0 itself finishing.
            {
                ActorWorld w(tables);
                ScriptManager mgr; mgr.loader = &loader; mgr.vm.host = &w;
                mgr.vm.g_flags[150] = 1;
                mgr.start_root(battle, "battle_0");
                auto battle_live = [&] {
                    for (const auto& i : mgr.live) if (i.name == "battle_0") return true;
                    return false;
                };
                int f = 0;
                for (; f < 8000 && battle_live(); ++f) {
                    mgr.vm.g_flags[132] = live_count(w);      // sync BEFORE the tick (gate reads it)
                    mgr.tick_all();
                    w.advance_cutscene_moves();               // scripted NPC walks resolve
                    for (Actor& a : w.actors()) if (a.is_enemy() && a.alive()) a.hp = 0;  // perfect combat
                }
                check(!battle_live() && mgr.vm.g_flags[165] == 1,
                      "intro battle runs to completion once each wave is killed (flag165=1)",
                      "frames=" + std::to_string(f) + " flag165=" + std::to_string(mgr.vm.g_flags[165]));
                if (battle_live())
                    for (const auto& inst : mgr.live)
                        std::printf("       [stuck] %s @pc=%u wait=%d\n",
                                    inst.name.c_str(), inst.pc, inst.wait);
            }
        } catch (const std::exception& e) {
            check(false, "S_1000 intro battle end-to-end", e.what());
        }
    }

    // --- FINDING: S_1000's entry enemies are NOT gated by flag 141 (hypothesis disproved) ---
    // The live log showed 6 Riffligan at room load on the intro. I suspected the view seeding
    // flag 141 after populate, but running the root as populate does with 141 unset vs set gives
    // the SAME roster — so the entry enemies come from an ungated spawn, not the intro gate.
    // Locked in here so the (wrong) flag-timing theory is not retried.
    {
        const std::string dir = (root / "MAP" / "S_10" / "S_1000").string() + "/";
        try {
            std::vector<std::uint8_t> rb;
            if (std::ifstream f{dir + "S_1000.XSO", std::ios::binary})
                rb.assign(std::istreambuf_iterator<char>(f), {});
            const Xso rootxso = Xso::parse(rb);
            auto count_enemies = [&](std::int32_t f141) {
                ActorWorld w(tables);
                XsoVm vm; vm.host = &w;
                vm.g_flags[150] = 1; vm.g_flags[141] = f141;
                vm.run(rootxso);
                int n = 0; for (const Actor& a : w.actors()) if (a.is_enemy()) ++n;
                return n;
            };
            check(count_enemies(0) == count_enemies(1),
                  "S_1000 entry enemies are NOT gated by flag 141 (ungated spawn)",
                  std::to_string(count_enemies(0)) + " either way");
        } catch (const std::exception& e) {
            check(false, "S_1000 flag-141 populate diagnostic", e.what());
        }
    }

    // --- The barrier ward (effect 210) resolves to MAP_KEKKAI.EF2 and textures its emitters.
    // (Finding: it uses BARIARING01/MADARA01/NAGA02/etc., NOT the boss bos\yuni_kekkai — so any
    // "wrong ward texture" is a render/compose issue or a single emitter, not a file mixup.) --
    {
        EftList efl; EftTex eft;
        if (efl.load(root) && eft.load(root)) {
            const fs::path p = efl.path(root, 210);
            bool textured = false, ring = false;
            if (!p.empty()) {
                const Ef2 ef = Ef2::load(p);
                for (const Ef2Emitter& em : ef.emitters()) {
                    if (em.frames.empty()) continue;
                    textured = true;
                    if (em.ring) ring = true;   // (ring params: inner==outer==0.30 => a ring band,
                                                //  not a flat annulus; the view's geometry is right)
                    std::string t = eft.path(root, int(em.frames[0].tex_id)).string();
                    for (char& c : t) c = char(std::tolower((unsigned char)c));
                    if (t.find("yuni_kekkai") != std::string::npos)
                        check(false, "ward emitter wrongly uses the BOSS barrier texture",
                              eft.path(root, int(em.frames[0].tex_id)).filename().string());
                }
            }
            check(p.filename().string() == "MAP_KEKKAI.EF2" && textured && ring,
                  "ward (effect 210) = MAP_KEKKAI.EF2 with a textured ring emitter");
        }
    }

    // --- S_1004 item stand: the root spawns the altar item as actor 159 (a non-enemy prop) ---
    // The Flabellum pickup (TALKITEM) triggers on interacting with this actor, so the view
    // registers an interactable at its position. Confirm the id/kind the wiring depends on.
    {
        const std::string dir = (root / "MAP" / "S_10" / "S_1004").string() + "/";
        try {
            std::vector<std::uint8_t> rb;
            if (std::ifstream f{dir + "S_1004.XSO", std::ios::binary})
                rb.assign(std::istreambuf_iterator<char>(f), {});
            ActorWorld w(tables);
            XsoVm vm; vm.host = &w; vm.g_flags[150] = 1;
            vm.run(Xso::parse(rb));
            const Actor* item = w.find(159);
            check(item != nullptr && !item->is_enemy() && !item->is_player(),
                  "S_1004 root spawns the altar item as non-enemy actor 159",
                  item ? "found" : "MISSING");
        } catch (const std::exception& e) {
            check(false, "S_1004 item-stand spawn", e.what());
        }
    }

    // --- S_1004 altar (Flabellum) pickup: flag 304 gates it, grants skill flags 116/107 ----
    // TALKITEM.XSO: if flag304==1 return (already taken); else set skill flags 116 & 107 and
    // mark 304. Proves the altar grants the wind skill once, character-independently.
    {
        const std::string dir = (root / "MAP" / "S_10" / "S_1004").string() + "/";
        try {
            std::vector<std::uint8_t> tb;
            if (std::ifstream f{dir + "TALKITEM.XSO", std::ios::binary})
                tb.assign(std::istreambuf_iterator<char>(f), {});
            const Xso talk = Xso::parse(tb);
            auto take = [&](std::int32_t f304) {
                XsoVm vm; vm.g_flags[150] = 1; vm.g_flags[304] = f304;
                vm.reset_run(); vm.run(talk);
                return std::array<std::int32_t, 3>{vm.g_flags[116], vm.g_flags[107], vm.g_flags[304]};
            };
            const auto fresh = take(0);      // first time -> grants
            const auto again = take(1);      // already have it -> no-op
            check(fresh[0] == 1 && fresh[1] == 1 && fresh[2] == 1,
                  "altar grants skill flags 116+107 and marks obtained (304)");
            check(again[0] == 0 && again[1] == 0 && again[2] == 1,
                  "an already-taken altar (flag304==1) grants nothing again");
        } catch (const std::exception& e) {
            check(false, "S_1004 altar pickup gating", e.what());
        }
    }

    // --- S_1001 chest UNLOCK gating: barrier flag (301) must gate the chest (S_BOX01) -----
    // S_BOX01.XSO: if flag302==1 return (already looted); if flag301==0 return (barrier still
    // up); else open, give item 89, set flag302=1. This is the exact path the view's interact
    // handler runs, so it proves the barrier -> chest link end to end.
    {
        const std::string dir = (root / "MAP" / "S_10" / "S_1001").string() + "/";
        try {
            std::vector<std::uint8_t> bb;
            if (std::ifstream f{dir + "S_BOX01.XSO", std::ios::binary})
                bb.assign(std::istreambuf_iterator<char>(f), {});
            const Xso box = Xso::parse(bb);
            auto opens = [&](std::int32_t f301, std::int32_t f302) {
                XsoVm vm; vm.g_flags[301] = f301; vm.g_flags[302] = f302;
                vm.reset_run(); vm.run(box);
                bool got89 = false; for (int it : vm.items) if (it == 89) got89 = true;
                return std::make_pair(got89, vm.g_flags[302]);
            };
            auto barrier_up   = opens(0, 0);   // barrier still up  -> locked
            auto barrier_down = opens(1, 0);   // barrier down, fresh -> opens
            auto already      = opens(1, 1);   // already looted    -> nothing
            check(!barrier_up.first,  "chest is LOCKED while the barrier is up (flag301==0)");
            check(barrier_down.first && barrier_down.second == 1,
                  "chest OPENS after the barrier falls: item 89 + flag302 set");
            check(!already.first, "an already-opened chest (flag302==1) gives nothing again");
        } catch (const std::exception& e) {
            check(false, "S_1001 chest-unlock gating", e.what());
        }
    }

    // --- Opened chests render POSED to their OPEN clip (build/event_re/S1001_..._RE.md) ---
    // A chest is one YMO with an in-model OPEN clip on its hinged lid; the room root
    // re-poses an already-looted chest on entry (0x175(box,OPEN,0,-1)). build_room now
    // takes the live g_flags and poses each box to its OPEN final frame when its
    // opened-flag is set. Positive proof: with S_1001's box_01 opened-flag (302) set,
    // the room geometry MUST differ from the fresh (all-closed) build, and the vertex
    // COUNT must be identical (same model, just re-posed — not hidden, not a new model).
    {
        const fs::path sob = root / "MAP" / "S_10" / "S_1001" / "S_1001.SOB";
        auto poshash = [](const RoomMesh& m) {
            double h = 0;                       // order-sensitive position checksum
            for (const RoomVertex& v : m.vertices) h = h * 1.0000001 + v.px + 2*v.py + 3*v.pz;
            return h;
        };
        try {
            std::array<std::int32_t, 0x200> closed{};      // fresh save: all flags 0
            std::array<std::int32_t, 0x200> opened{};
            opened[302] = 1;                                // box_01 looted (S_BOX01 guard)
            const RoomMesh mc = build_room(root, sob, nullptr, nullptr, closed.data());
            const RoomMesh mo = build_room(root, sob, nullptr, nullptr, opened.data());
            check(mc.vertices.size() == mo.vertices.size() && !mc.vertices.empty(),
                  "opened chest keeps the same vertex count (re-posed, not hidden/replaced)",
                  std::to_string(mc.vertices.size()) + " vs " + std::to_string(mo.vertices.size()));
            check(poshash(mc) != poshash(mo),
                  "opened chest actually MOVES geometry (OPEN lid pose applied)");
        } catch (const std::exception& e) {
            check(false, "S_1001 room build for chest-pose test", e.what());
        }
    }

    return fails ? 1 : 0;
}
