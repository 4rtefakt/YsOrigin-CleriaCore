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
#include "cleria/world.hpp"

#include <cstdio>
#include <filesystem>
#include <random>
#include <set>
#include <string>

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
        check(p && p->max_hp == 0,
              "the player gets NO enemy stats (the op returns at step 7)");
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

    std::printf("\n%s (%d failed)\n", fails ? "FAILURES" : "all invariants hold", fails);
    return fails ? 1 : 0;
}
