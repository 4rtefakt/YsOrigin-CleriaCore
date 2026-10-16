#include "cleria/world.hpp"
#include "cleria/factory_flags.hpp"

#include <cmath>

#include "cleria/cast.hpp"
#include "cleria/eex.hpp"
#include "cleria/extra_versions.hpp"
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

Actor* ActorWorld::spawn(const SpawnRequest& r0) {
    if (!tables_.ok()) return nullptr;
    SpawnRequest r = r0;
    const Cast* c = tables_.casts->get(r.cast_type);
    if (!c) return nullptr;
    SpawnTweak tweak;
    if (spawn_filter && c->cast_id != kIdPlayer && tables_.enemies->get(c->cast_id)) {   // mods (API 7): enemies only
        tweak.cast = r.cast_type; tweak.level = r.level;
        spawn_filter(r, tweak);
        if (tweak.skip) return nullptr;
        if (tweak.cast != r.cast_type) {                 // another ENEMY cast, or the request stands
            const Cast* c2 = tables_.casts->get(tweak.cast);
            if (c2 && c2->cast_id != kIdPlayer && tables_.enemies->get(c2->cast_id)) { c = c2; r.cast_type = tweak.cast; }
        }
        r.level = std::clamp(tweak.level, 0, 60);
    }

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
    a->speed30c = c->speed_base;   // 0x4596a8: +0x30c = CASTINFO+0x48 (WAITOPS.md §6.2)
    a->ai = EnemyAi{};    // the factory builds a fresh actor: brain phase 0
    a->kind = kind_word(c->actor_class);
    // +0x24c: cast 0xa6 runs C_980 in place of its own script while 0x74c0d0 != 0 [BYTES 0x459439..0x45944d].
    {
        const char* aso = extra::cast_aso_override(c->cast_id, school_outfit && *school_outfit != 0);
        a->aso_override = aso ? aso : "";
    }

    // Step 5: placement. ** The op stores DEGREES ** and the engine converts
    // (FUN_00443a80). Storing the raw operand as radians would be a silent 57x error.
    a->pos = r.pos;
    a->spawn_y = r.pos.y;          // +0x31c (FUN_00459ff0 0x45a012)
    a->yaw = r.yaw_degrees * 3.14159265358979323846f / 180.0f;
    // The Chr state the factory rebuilds even on a reused id (FUN_00459410 runs on every spawn):
    // +0x36c = 1.0 (0x4596d6), no pin/motion/tremble/FlyTo, look mode 0; FUN_00459ff0 then sets
    // +0x308 = +0x314 = +0x360 = the spawn yaw. Footsteps +0x4be: class 2 (the player) 1, cast
    // 0x7e forced 1, every other class 0 (CHR_OPS.md §3; class 4 [H-high]: zeroed allocation).
    a->look = LookState{};
    a->look.cmd = a->look.dir = heading_of_yaw(a->yaw);
    a->pattern = PatternPin{};
    a->anim_motion = -1;
    a->tremble = Tremble{};
    a->fly_r2 = 0.0f;
    a->footsteps = c->actor_class == 2 || c->cast_id == 0x7e;
    a->flyer = c->lift > 0.0f;     // +0x398 (FUN_00459410 0x45960f); EI+0xb0 below
    a->hurt_radius = c->hurt_radius; a->hurt_half_h = c->hurt_half_h;
    a->hurt_shape = c->hurt_shape; a->hurt_off = c->center_offset;
    a->bank = RehitBank{};
    a->hit_serial = next_hit_serial(hit_serial_counter_);   // 0x4597c8

    // The factory's per-cast flag words (its switch on the cast id; FACTORY.md §5).
    {
        const FactoryFlags ff = factory_flags(c->cast_id);
        a->flags12c = ff.f12c; a->flags130 = ff.f130;
    }
    // Step 6: AND [obj+0x12c], ~0x10 — cleared at spawn. +0x12c is NOT +0x3bc (the
    // death flag). 0x10 enables the idle wander (FUN_004601a0, FACTORY.md §7.1), so an
    // XSO-spawned enemy does not wander [the wander gate itself is not modelled yet].
    a->flags12c &= ~0x10u;

    // Step 7: the player. ** Identity comes from the CAST record, not the operand. **
    if (c->cast_id == kIdPlayer) {
        player_ = a;
        return a;  // the engine returns here — no stat setup, no tagging
    }

    // Step 8: enemies only.
    if (a->is_enemy()) {
        const Enemy* e = tables_.enemies->get(c->cast_id);
        if (e) {
            // FUN_004341a0 / FUN_00570400 -> FUN_004202e0(actor, cast - 14, 0) [BYTES 0x434472, 0x570501]: the
            // curve stats and the difficulty tail (Hard HP x1.2, Nightmare x1.5, Very Easy x0.75, the easy-side
            // ATK / DEF cuts), for every enemy (DIFFICULTY.md G2).
            StatMode sm;
            if (stat_flags) {
                sm.hard = stat_flags[144]; sm.easy = stat_flags[143] != 0; sm.very_easy = stat_flags[203] != 0;
                sm.boss_rush = stat_flags[193] != 0; sm.retries = stat_flags[196];
            }
            const EnemyStats s = enemy_restat(*tables_.game, *e, c->cast_id, r.level, sm);
            a->max_hp = std::max(1.0f, std::floor(s.max_hp * tweak.hp));   // tweak: 1 unless a mod's "spawn" filter spoke
            a->hp = a->max_hp;
            a->atk = s.atk * tweak.atk;
            a->def = s.def * tweak.def;
            for (int k = 0; k < 4; ++k) a->resist[k] = e->resist[k];
            a->exp_reward = s.exp * tweak.exp;
            a->sp_reward = s.sp * tweak.sp;
            if (e->ai_sight != 0.0f) a->flyer = true;   // EI+0xb0 (0x45967a)
            a->level = r.level ? r.level : e->level;
            a->move_speed = e->move_speed;
            a->enemy_index = enemy_index_for_cast(c->cast_id);
        }
        a->tag.set(r.group);
        // FUN_00570400's group block [BYTES 0x57052c..0x5705d0]: the marker ARGB and the glow
        // phase seed +0x2a7 += (xorshift >> 17) >> 7.
        static const std::uint32_t kMarker[4] = {0xFFE00000u, 0xFF00D000u, 0xFF0000FFu, 0xFF00C0C0u};
        if (r.group >= 1 && r.group <= 4) {
            a->glow_marker = kMarker[r.group - 1];
            if (rng) {
                std::uint32_t* q = rng;
                const std::uint32_t t = q[0] ^ (q[0] << 11);
                q[0] = q[1]; q[1] = q[2]; q[2] = q[3];
                q[3] = q[3] ^ (q[3] >> 19) ^ t ^ (t >> 8);
                a->glow_phase = std::uint8_t(a->glow_phase + ((q[3] >> 17) >> 7));
            }
        }
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
    if (on_spawned && a && tables_.enemies->get(a->cast_id)) on_spawned(*a);   // mods (API 7)
    return a;
}

// XSO op 0x18E Arena(cast, level, count): FUN_005722c0 [EXPORT 005722c0.c:70-232; consts BYTES 0x6bdf6c..].
// count 0 = 8; the eight single casts (M360, M460, the duel casts) come alone. Each monster: the ordinary
// factory, three steps of the game's xorshift (x from the first word, z from the second, the third unused), a
// place in the 18 x 12 box round the player's start (-0.085, 41.983) at y 2.1, clamped into the fence's inside,
// faded in from alpha 0 by 0.02 a tick, its stats at `level` (0 = the cast's own). The caller binds the warp
// effect 0x10D to each id returned. [not modelled] the actor pool's limit (a full pool drops the rest).
std::vector<int> ActorWorld::arena_spawn(int cast, int level, int count) {
    std::vector<int> ids;
    int n = count ? count : 8;
    if (cast == 0x32 || cast == 0x28 || (cast >= 0xa7 && cast <= 0xac)) n = 1;
    auto draw = [&]() -> std::uint32_t {
        if (!rng) return 0;
        std::uint32_t* q = rng;
        const std::uint32_t t = q[0] ^ (q[0] << 11);
        q[0] = q[1]; q[1] = q[2]; q[2] = q[3];
        q[3] = q[3] ^ (q[3] >> 19) ^ t ^ (t >> 8);
        return q[3];
    };
    for (int i = 0; i < n; ++i) {
        const std::uint32_t r1 = draw(), r2 = draw();
        draw();
        SpawnRequest r;
        r.cast_type = cast; r.level = level;
        r.pos = {float(r1 >> 17) / 32767.0f * 18.0f - 9.0f - 0.085f, 2.1f, float(r2 >> 17) / 32767.0f * 12.0f - 6.0f + 41.983f};
        Actor* a = spawn(r);
        if (!a) break;
        if (a->pos.y < 2.0f) a->pos.y = 2.0f;
        a->pos.x = std::clamp(a->pos.x, -11.325f, 11.325f);
        if (a->pos.z > 51.325f) a->pos.z = 51.325f; else if (a->pos.z < 33.0f) a->pos.z = 33.0f;
        a->ai.alpha = 0.0f; a->alpha_to = 1.0f; a->alpha_step = 0.02f;
        ids.push_back(a->instance_id);
    }
    return ids;
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
                           p.yaw * 180.0f / 3.14159265358979323846f,   // the -1 / 360 sentinel included (sct.cpp)
                           0});
    }
}

// The per-actor update hook. Enemy steering and movement are NOT here any more: they
// are the AI handler's (src/cleria/enemy_ai.hpp — sight/acquisition, the brain, the
// action player and the move primitive), which needs the actor's animation script
// (anime_Movable) and the room geometry, so the view runs it. The old invented chase
// (a universal 25-unit gate, turn-then-advance at walk speed) is gone.
void ActorWorld::update_actor(Actor&) {}

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
