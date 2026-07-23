#include "cleria/world.hpp"

#include <cmath>

#include "cleria/cast.hpp"
#include "cleria/eex.hpp"
#include "cleria/enemies.hpp"
#include "cleria/gameinfo.hpp"
#include "cleria/sct.hpp"

namespace cleria {

bool ActorWorld::id_in_use(int id) const {
    for (const Actor& a : actors_)
        if (a.instance_id == id) return true;
    return false;
}

// FUN_00434db0. The cast type itself if free, else the first free id from 186 up.
int ActorWorld::auto_id(int cast_type) const {
    if (!id_in_use(cast_type)) return cast_type;
    int id = kAutoIdBase;
    while (id_in_use(id)) ++id;
    return id;
}

// FUN_00434d50. Note the player fast paths come FIRST, before the scan.
Actor* ActorWorld::find(int id) {
    // 999 resolves to the same actor the spawn op points DAT_0074ab90 at, which is
    // the player. Treating 999 as "some other actor" would be wrong.
    if ((id == kIdPlayer || id == kIdPlayerAlias) && player_) return player_;
    for (Actor& a : actors_)
        if (a.instance_id == id) return &a;
    return nullptr;  // the engine returns 0 here too
}

Actor* ActorWorld::spawn(const SpawnRequest& r) {
    if (!tables_.ok()) return nullptr;
    const Cast* c = tables_.casts->get(r.cast_type);
    if (!c) return nullptr;

    // Step 1: an existing id is REUSED. The op looks up before it allocates, so a
    // script re-spawning an id re-points that actor rather than doubling it.
    int id = r.id ? r.id : auto_id(r.cast_type);
    Actor* a = (r.id ? find(r.id) : nullptr);
    if (!a) {
        actors_.emplace_back();
        a = &actors_.back();
    }

    // Step 4: the factory, FUN_00459410(id, +0x64, +0x60, +0x68, +0x6c).
    a->instance_id = id;
    a->cast_id = c->cast_id;
    a->ai_index = c->ai_index;
    a->kind = kind_word(c->actor_class);

    // Step 5: placement. ** The op stores DEGREES ** and the engine converts
    // (FUN_00443a80). Storing the raw operand as radians would be a silent 57x error.
    a->pos = r.pos;
    a->yaw = r.yaw_degrees * 3.14159265358979323846f / 180.0f;

    // Step 6: AND [obj+0x12c], ~0x10 — cleared at spawn. +0x12c is NOT +0x3bc (the
    // death flag), and the shared 0x10 makes them easy to conflate. What +0x12c's
    // 0x10 means is [UNKNOWN], so it is not modelled; recorded here so the gap is
    // visible rather than forgotten.

    // Step 7: the player. ** Identity comes from the CAST record, not the operand. **
    if (c->cast_id == kIdPlayer) {
        player_ = a;
        return a;  // the engine returns here — no stat setup, no tagging
    }

    // Step 8: enemies only.
    if (a->is_enemy()) {
        const Enemy* e = tables_.enemies->get(c->cast_id);
        if (e) {
            const EnemyStats s = enemy_stats(*tables_.game, *e);
            a->max_hp = s.max_hp;
            a->hp = s.max_hp;
            a->atk = s.atk;
            a->def = s.def;
            a->exp_reward = s.exp;
            a->level = e->level;
            a->move_speed = e->move_speed;
            a->enemy_index = enemy_index_for_cast(c->cast_id);
        }
        a->tag.set(r.group);
    } else if (tables_.eex) {
        // A full-room (medallion) boss: class 4, no ENEMYINFO record, RAW stats from
        // EEX. eex_index_for_cast maps the 8 boss casts -> records 0..7 (eex.hpp). The
        // engine writes HP->+0x94, STR->+0xa4, DEF->+0xa8, level->+0xc8 directly.
        const int bi = eex_index_for_cast(c->cast_id);
        if (bi >= 0 && bi < int(tables_.eex->bosses.size())) {
            const BossStats& bs = tables_.eex->bosses[bi];
            a->max_hp = bs.hp;
            a->hp = bs.hp;
            a->atk = bs.atk;
            a->def = bs.def;
            a->level = bs.level;
            // No move_speed: full-room bosses are not the sprite-chase movers.
        }
    }
    return a;
}

// XSO op 0xa0 (FUN_004341a0): bulk-spawn the room's SCT placements. Each placement's
// name resolves through CASTINFO to a cast_type, then the ordinary spawn path. The
// engine area-filters on record+0x7c, but that field is 0 for all 1290 shipped records
// (build/roomload_re/ROOMLOAD.md), so every placement spawns. Runs once per room, before
// any 0x12c scripted spawns; spawn order is SCT-record order, which the tick preserves.
void ActorWorld::spawn_placements() {
    if (!placements_ || !tables_.casts) return;
    if (placements_spawned_) return;   // op 0xa0 runs once per room load (see the guard note)
    placements_spawned_ = true;
    for (const CastPlacement& p : placements_->casts) {
        const Cast* c = tables_.casts->find(p.name);
        if (!c) continue;  // CAST_ADOL etc. that aren't in CASTINFO stay unspawned here
        spawn(SpawnRequest{0, c->cast_id, p.position,
                           p.random_yaw ? 0.0f : p.yaw * 180.0f / 3.14159265358979323846f,
                           0});
    }
}

// The enemy chase (FUN_0045f4d0). Turn toward the player, advance at move_speed. Only
// horizontal — the caller floor-snaps (it owns the geometry). No-op for the player, for
// statless casts, or when there is no player / it is out of activation range.
void ActorWorld::update_actor(Actor& a) {
    if (!player_ || &a == player_ || !a.is_enemy() || !a.alive()) return;
    const float dx = player_->pos.x - a.pos.x;
    const float dz = player_->pos.z - a.pos.z;
    const float d2 = dx * dx + dz * dz;
    if (d2 >= kEnemyActivationRadius * kEnemyActivationRadius) return;  // anim-only far off

    // AIM toward the player -- EVERY enemy in range, mobile or not (build/mobility_re:
    // facing +0x314 turns independent of translation, so a rooted turret still tracks
    // you). Heading convention matches the mover: vx = sin(h), vz = cos(h), so the
    // target is atan2(dx, dz).
    const float target = std::atan2(dx, dz);
    float dyaw = target - a.yaw;
    while (dyaw > 3.14159265358979323846f) dyaw -= 2.0f * 3.14159265358979323846f;
    while (dyaw < -3.14159265358979323846f) dyaw += 2.0f * 3.14159265358979323846f;
    if (dyaw > kEnemyTurnRate) dyaw = kEnemyTurnRate;
    if (dyaw < -kEnemyTurnRate) dyaw = -kEnemyTurnRate;
    a.yaw += dyaw;

    // MOVE only if it has a chase speed. speed 0 == immobile (mechanically frozen: the
    // engine's mover multiplies by ENEMYINFO+0x94*+0x98). Exactly 4 shipped casts are
    // speed 0 (Normous, Nustarl, Sevnam, M990) -- they aim but never translate.
    if (a.move_speed > 0 && a.move_scale > 0.0f) {
        a.pos.x += std::sin(a.yaw) * a.move_speed * a.move_scale;
        a.pos.z += std::cos(a.yaw) * a.move_speed * a.move_scale;
    }
}

// FUN_004281c0, one simulation step. Player first, then the live list in spawn order
// skipping the player, count cached so mid-step spawns wait for the next step.
void ActorWorld::step() {
    if (player_) update_actor(*player_);
    // Snapshot the count: a spawn during update_actor appends to the end (stable for a
    // list iterator) but is not visited now, because we stop after n. Its first update
    // is next step. update_actor never erases -- destruction is deferred to the sweep
    // -- so the iterator stays valid across the loop.
    std::size_t n = actors_.size();
    auto it = actors_.begin();
    for (std::size_t i = 0; i < n && it != actors_.end(); ++i, ++it) {
        if (&*it == player_) continue;  // already updated
        update_actor(*it);
    }
}

// FUN_00428b10, the once-per-frame deferred sweep. Order-preserving removal of every
// actor with kDestroyRequested; the player pointer is dropped if the player is swept.
std::size_t ActorWorld::sweep_dead() {
    std::size_t removed = 0;
    for (auto it = actors_.begin(); it != actors_.end();) {
        if (it->destroy_requested()) {
            if (&*it == player_) player_ = nullptr;  // engine re-scans; we have one
            it = actors_.erase(it);                  // deque erase preserves order
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

}  // namespace cleria
