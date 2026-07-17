#include "cleria/world.hpp"

#include <cmath>

#include "cleria/cast.hpp"
#include "cleria/enemies.hpp"
#include "cleria/gameinfo.hpp"

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
            a->enemy_index = enemy_index_for_cast(c->cast_id);
        }
        a->tag.set(r.group);
    }
    return a;
}

}  // namespace cleria
