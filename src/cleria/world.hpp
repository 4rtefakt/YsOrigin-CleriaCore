// The actor world — the pool the XSO spawn op fills, and the registry every query
// op resolves through. Derived from build/entity_re/ENTITY.md; see actor.hpp for why
// our Actor is not the engine's GoActor.
//
// ## The primary key is the INSTANCE id, not the cast id [C-DIS][C-COR]
//
// `entity+0x128` is a PER-INSTANCE id, and it is what `FUN_00434d50` — the lookup
// behind every query op — linear-scans for. It is emphatically not the cast type:
// EVNT_3002_THOR.XSO spawns FIVE instances of cast 35 with ids 167..171.
//
// The default id IS the cast type, though, which is why op0 and op1 correlate all over
// the corpus while staying distinct — and it is exactly the kind of correlation that
// invites collapsing two fields into one. They are two fields.
//
// ## Reserved ids [C-DIS]
//
//   13   the player — but the engine takes that identity from CASTINFO+0x60 == 13
//        (the CAST_ADOL record), NOT from the spawn op's operand.
//   998  "self" — resolved in the op against ctx+0x240, never through the registry.
//   999  -> DAT_0074ab90, which the spawn op ALSO points at the player (0x5704ce).
//        ** Do not read 999 as "not the player". ** Whether anything re-points it
//        later is [UNKNOWN].
#pragma once
#include <cmath>
#include <cstdint>
#include <list>

#include "cleria/actor.hpp"
#include "cleria/mathx.hpp"
#include "cleria/xso.hpp"

namespace cleria {

struct CastTable;
struct EnemyTable;
struct GameInfo;
struct EexTable;
struct Sct;

// Ids the engine gives meaning to. `kIdSelf` never reaches the registry.
inline constexpr int kIdPlayer = 13;
inline constexpr int kIdSelf = 998;
inline constexpr int kIdPlayerAlias = 999;
// The auto-id allocator scans upward from here — just past the 186 CASTINFO records,
// so an auto-id can never collide with a default (which is a cast type). [C-DIS]
inline constexpr int kAutoIdBase = 186;

// The simulation runs at a fixed 60 Hz (ACTOR.md §3: the step length is exactly
// 1000/60 ms, drained by a catch-up loop; per-step dt is constant, actor motion is
// not delta-scaled). physics_player.hpp's kTimerStep (~1/60) is the same clock.
inline constexpr int kSimHz = 60;
inline constexpr float kSimDt = 1.0f / float(kSimHz);


// Everything the spawn op (0x12c) carries. Named so the operand order stops being
// load-bearing at call sites.
struct SpawnRequest {
    int id = 0;         // op0. 0 => allocate; see auto_id().
    int cast_type = 0;  // op1 — indexes CASTINFO.
    Vec3 pos{};
    float yaw_degrees = 0;  // the op stores DEGREES; the engine converts (FUN_00443a80)
    int group = 0;          // op6. Only 1..4 are honoured; 0 = untagged.
};

// The tables a spawn needs to turn a cast type into a live actor with real stats.
struct WorldTables {
    const CastTable* casts = nullptr;
    const EnemyTable* enemies = nullptr;
    const GameInfo* game = nullptr;
    const EexTable* eex = nullptr;  // optional: full-room boss stats (eex.hpp)

    bool ok() const { return casts && enemies && game; }
};

// ActorWorld IS the XSO host: it implements the primitive spawn/is_alive interface
// the VM calls, so a running script drives real actors. The interface stays free of
// world types (see xso.hpp); this is where the two meet.
class ActorWorld : public XsoHost {
  public:
    explicit ActorWorld(WorldTables t) : tables_(t) {}

    // The spawn op, end to end (FUN_00570400). Returns null if the cast is unknown.
    //
    // ** An existing actor with this id is REUSED, not duplicated ** — step 1 of the
    // engine's own sequence looks the id up first and only allocates on a miss.
    Actor* spawn(const SpawnRequest& r);

    // --- XsoHost: the primitive interface the VM calls -------------------------
    void spawn(int entity_id, int cast_type, float x, float y, float z,
               float yaw_deg, int group) override {
        spawn(SpawnRequest{entity_id, cast_type, {x, y, z}, yaw_deg, group});
    }
    bool is_alive(int entity_id) const override {
        // find() is non-const (it may resolve the player fast path), so this mirrors
        // its logic read-only rather than casting away const.
        if ((entity_id == kIdPlayer || entity_id == kIdPlayerAlias) && player_)
            return player_->alive();
        for (const Actor& a : actors_)
            if (a.instance_id == entity_id) return a.alive();
        return false;
    }
    // Region-query position source (ops 0x139/0x13a/0x13b): the worker's actor resolve
    // (FUN_00434d50 by id, player fast path) then position read (+0x50/+0x54/+0x58).
    bool actor_pos(int entity_id, float out[3]) const override {
        const Actor* a = nullptr;
        if ((entity_id == kIdPlayer || entity_id == kIdPlayerAlias) && player_) {
            a = player_;
        } else {
            for (const Actor& c : actors_)
                if (c.instance_id == entity_id) { a = &c; break; }
        }
        if (!a) return false;
        out[0] = a->pos.x; out[1] = a->pos.y; out[2] = a->pos.z;
        return true;
    }
    // op 0x131 Chr_IsSeedLiving(group): FUN_00570640 scans the live registry for any
    // entity whose +0x130 group bits include (group<<8). The player is never tagged, so
    // this is just "any living group-N seed enemy left?".
    bool any_seed_alive(int group) const override {
        for (const Actor& a : actors_)
            if (a.tag.in_group(group) && a.alive()) return true;
        return false;
    }
    // Cutscene actor control (ops 0x140/0x143/0x133/0x100). The player (id 13/999) is
    // NOT a world actor in the view — the host special-cases it — so these act on
    // spawned NPCs only; an absent id is a no-op.
    void chr_pos(int id, float x, float y, float z) override {          // 0x140 teleport
        if (Actor* a = find(id)) { a->pos = {x, y, z}; a->cmoving = false; }
    }
    void chr_dir(int id, float deg) override {                          // 0x133 facing (degrees)
        if (Actor* a = find(id)) a->yaw = deg * 3.14159265358979323846f / 180.0f;
    }
    void chr_moveto(int id, float x, float z, float speed) override {   // 0x143 walk-to
        if (Actor* a = find(id)) {
            a->cmove_tgt = {x, a->pos.y, z};
            a->cmove_spd = speed > 0 ? speed : 0.1f;
            a->cmoving = true;
        }
    }
    bool chr_moving(int id) const override {                            // 0x100 Wait_ChrMoving
        for (const Actor& a : actors_) if (a.instance_id == id) return a.cmoving;
        return false;
    }
    void chr_alpha(int id, float end_alpha) override {                  // 0x154 Chr_Alpha (end state)
        if (Actor* a = find(id)) a->visible = end_alpha > 0.01f;
    }
    // Facing control (ops 0x134 Chr_DirChr / 0x136 Chr_LookAtChr): actor `id` turns to
    // face actor `target`. 0x137 Chr_LookAtPos faces a world point. Player (13/999)
    // resolves through the synced player_ actor. Billboard sprites use yaw for the
    // 8-direction ring, so this rotates the on-screen sprite toward the target.
    void chr_lookat(int id, int target) override {
        Actor* a = find(id); Actor* t = find(target);
        if (a && t) a->yaw = std::atan2(t->pos.x - a->pos.x, t->pos.z - a->pos.z);
    }
    void chr_lookat_pos(int id, float x, float z) override {
        if (Actor* a = find(id)) a->yaw = std::atan2(x - a->pos.x, z - a->pos.z);
    }
    void chr_animate(int id, int motion) override {   // 0x151 pin/clear the pose tag
        if (Actor* a = find(id)) a->anim_pin = motion;
    }
    // Advance every cutscene walk-to one frame (called each tick, incl. while frozen —
    // NPCs move during a Fix). Turns toward the target and steps; stops within reach.
    void advance_cutscene_moves() {
        for (Actor& a : actors_) {
            if (!a.cmoving) continue;
            const float dx = a.cmove_tgt.x - a.pos.x, dz = a.cmove_tgt.z - a.pos.z;
            const float d = std::sqrt(dx * dx + dz * dz);
            if (d <= a.cmove_spd || d < 1e-3f) { a.pos.x = a.cmove_tgt.x; a.pos.z = a.cmove_tgt.z; a.cmoving = false; }
            else { a.pos.x += dx / d * a.cmove_spd; a.pos.z += dz / d * a.cmove_spd; a.yaw = std::atan2(dx, dz); }
        }
    }

    // op 0x12d Chr_Delete(id): remove the actor with this instance id (never the
    // player). std::list keeps other Actor* handles valid across the erase.
    void despawn(int entity_id) override {
        for (auto it = actors_.begin(); it != actors_.end(); ++it)
            if (it->instance_id == entity_id && &*it != player_) { actors_.erase(it); return; }
    }

    // op 0xa0: spawn every placement in the room's SCT (set via set_placements). No-op
    // if no placements are set. Defined in world.cpp (needs the Sct/CastTable bodies).
    void spawn_placements() override;

    // The room's SCT placements, the source op 0xa0 spawns from. The caller sets this
    // to the loaded room's SCT before running its script. Not owned.
    void set_placements(const Sct* s) { placements_ = s; }

    // FUN_00434d50 — the lookup behind every query op. A linear scan by instance id.
    // Returns null when absent (the engine returns 0), and resolves the player fast
    // path for id 13 / 999.
    Actor* find(int id);

    // FUN_00434db0 — the auto-id allocator, used when the op's id operand is 0.
    // The cast type itself if free, else the first free id from 186 upward.
    int auto_id(int cast_type) const;

    // The player, or null before CAST_ADOL has spawned. The engine keeps three
    // globals pointing here (DAT_0074c09c / DAT_0074ab90 / DAT_00749d44); why three,
    // and whether they ever diverge, is [UNKNOWN], so we keep one and say so.
    Actor* player() { return player_; }

    std::list<Actor>& actors() { return actors_; }
    const std::list<Actor>& actors() const { return actors_; }
    std::size_t size() const { return actors_.size(); }

    // Drop every actor (e.g. a room change — the engine's FUN_00428ec0 destroy-all).
    // Invalidates every Actor* handed out, including the player pointer.
    void clear() {
        actors_.clear(); player_ = nullptr; effects_.clear(); placements_spawned_ = false;
        reset_arena();
    }
    // The arena rectangle DAT_0076a5cc..d8 {xmin, zmin, xmax, zmax}: set by op 0x10e, reset
    // to +/-FLT_MAX on every room load (FUN_00431840 0x431aae) and never by a script.
    float arena[4] = {-3.402823466e+38f, -3.402823466e+38f, 3.402823466e+38f, 3.402823466e+38f};
    void reset_arena() {
        arena[0] = arena[1] = -3.402823466e+38f;
        arena[2] = arena[3] = 3.402823466e+38f;
    }
    void enclose(float xmin, float zmin, float xmax, float zmax) override {
        arena[0] = xmin; arena[1] = zmin; arena[2] = xmax; arena[3] = zmax;
    }

    // ## Located effects (op 0x11a / 0x119) --------------------------------------
    // A script-placed, persistent effect instance: an eftlist effect id at a world
    // position, addressable by name for later Eft_Kill. The engine spawns a GoEffect;
    // the VM stays headless, so we just record what to draw and let the view resolve
    // the id -> EF2 and render it. The barrier ward (id 210, name "eft_barrier") lives
    // here from room-load until the barrier breaks.
    // `age` ticks up every sim frame; a ONE-SHOT effect is removed when it passes its EF2
    // lifetime (EFFECTS.md §: the effect object's tick has a lifetime gate `+0x130 >= +0x12c`
    // that sets the destroy bit). `hold` is the op's 6th operand: 0 for fire-and-forget
    // bursts (enemy warp-in `z_warp`, hit sparks) and 25.0 for the effects that must stay up
    // until an explicit Eft_Kill (the `map_kekkai` wards, the `save_*zou` statue auras).
    // [H-med] the operand's exact unit isn't traced, but the 0-vs-nonzero split is exactly the
    // observed one-shot/persistent split across the whole S_10 script corpus.
    struct LocatedEffect { int id; float x, y, z, a, b; std::string name; int age = 0; };
    void locate_effect(int id, float x, float y, float z, float a, float b,
                       const std::string& name) override {
        // Effects are addressed BY NAME (Eft_Kill looks a name up), so a name can only denote
        // one live effect: re-placing a name replaces it rather than stacking a second copy.
        // Without this, a script re-run piles duplicate auras on the goddess statue.
        if (!name.empty())
            for (LocatedEffect& e : effects_)
                if (e.name == name) { e = {id, x, y, z, a, b, name, 0}; return; }
        effects_.push_back({id, x, y, z, a, b, name, 0});
    }
    // Age every located effect one sim tick and drop the finished one-shots. `life_of(id)`
    // resolves the EF2's own lifetime in frames (the view owns the EF2 registry).
    void tick_effects(const std::function<int(int)>& life_of) {
        for (auto it = effects_.begin(); it != effects_.end();) {
            ++it->age;
            const bool persistent = it->b != 0.0f;      // held until Eft_Kill
            const int life = life_of(it->id);
            it = (!persistent && life > 0 && it->age >= life) ? effects_.erase(it) : it + 1;
        }
    }
    void kill_effect(const std::string& name) override {
        for (auto it = effects_.begin(); it != effects_.end();)
            it = (it->name == name) ? effects_.erase(it) : it + 1;
    }
    const std::vector<LocatedEffect>& effects() const { return effects_; }

    // ## The tick (build/actor_re/ACTOR.md §2, §3) [C-DIS]
    //
    // ** Iteration order IS defined, and it is a correctness requirement, not a
    // detail. ** The earlier "order is undefined" note is retired: the engine updates
    // the PLAYER FIRST, then walks the live list in index (= spawn) order, skipping the
    // player. Removal is an order-preserving shift, so survivors keep their relative
    // order across deaths. Hit processing runs inside the victim's own update, so this
    // order is observable through gameplay — ACTOR.md §8 lists (player-first) +
    // (spawn-order) + (order-preserving removal) as correctness invariants.
    //
    // ** The COUNT is cached before the loop. ** An actor spawned mid-step (a script op
    // running inside another actor's update) is appended but NOT visited this step; its
    // first update is next step. Reproduced here by snapshotting the size.
    //
    // ** Per-actor AI is [UNKNOWN] and deliberately a no-op hook. ** The engine
    // dispatches through the 94-entry AI table (0x69b8c0) by actor+0x120; we have not
    // RE'd what a handler does, so `update_actor` does nothing but is the honest seam.
    // The loop STRUCTURE is validated; the loop BODY is an owned placeholder.
    //
    // Fixed 60 Hz: the sim dt is a constant 1/60 s (kSimHz). Actor motion is NOT
    // delta-scaled — the render/interpolation fraction is a separate, presentation-only
    // concern. Any variable-dt tick here would diverge from the engine.
    void step();

    // Request removal. The engine sets +0x11 bit 2 (GoActor::Kill, FUN_004593c0) after
    // the AI has played the death state; nothing is removed at that instant. We have no
    // death-anim state machine yet, so a killed actor requests destruction directly.
    void request_destroy(Actor& a) { a.header_flags |= kDestroyRequested; }

    // ** The deferred sweep — FUN_00428b10, ONCE PER RENDER FRAME, not per step. ** With
    // N catch-up steps in one frame, an actor killed in step 1 is still updated in
    // 2..N; only the frame-boundary sweep removes it. So this is called at FRAME
    // granularity by the caller, never inside step(). Removal is order-preserving, and
    // if the player actor is swept the player pointer is dropped (the engine re-scans
    // for another cast-13 actor; we have exactly one, so null is correct).
    std::size_t sweep_dead();

    // The engine's pool is a fixed 128 slots (overflow fails silently, does not throw).
    // We do NOT impose the cap: ACTOR.md §8 names the append/order/deferred-sweep
    // invariants as what matters, not the 128 ceiling. Exposed so a caller can warn.
    static constexpr std::size_t kEnginePoolCap = 128;

  private:
    WorldTables tables_;
    // ** A list, and that is load-bearing — a deque would dangle pointers. **
    // spawn()/find()/player() all hand out Actor*, and player_ lives from CAST_ADOL's
    // spawn to end of room. The engine's own storage is a slab of STABLE slots with a
    // separate pointer array for order (DAT_00730098), so a slot never moves once
    // spawned. std::list reproduces exactly that: append keeps order, and
    // order-preserving MIDDLE erase (what the sweep does) leaves every other node's
    // address untouched. std::deque was the first choice and is WRONG here — its
    // middle-erase invalidates all references, which is precisely the sweep's case.
    std::list<Actor> actors_;
    Actor* player_ = nullptr;
    const Sct* placements_ = nullptr;  // op 0xa0's source; not owned
    // op 0xa0 (SCT bulk spawn) uses AUTO-ids, so a second call would DOUBLE the room's
    // enemies. With events on the root runs twice (populate run-to-completion, then the
    // live coroutine), so guard it: spawn_placements is a no-op after the first call,
    // reset on clear() (room load). Explicit-id 0x12c spawns dedup by id already.
    bool placements_spawned_ = false;
    std::vector<LocatedEffect> effects_;  // op 0x11a placements; killed by 0x119
public:
    std::uint32_t hit_serial_counter_ = 0;   // DAT_007d9b78 (shared with the player's attacks)
private:
    bool id_in_use(int id) const;

    // The per-actor update hook (tick ORDER + the sweep around it are validated). Enemy
    // AI and movement are the handler's — src/cleria/enemy_ai.hpp, run by the view,
    // which holds each enemy's animation script and the room geometry it needs.
    void update_actor(Actor& a);
};

}  // namespace cleria
