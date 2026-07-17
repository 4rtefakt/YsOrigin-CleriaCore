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

// The enemy chase, from build/mover2d_re/MOVER2D.md [C-DIS]. An enemy ticks its AI only
// when the player is within this distance (the engine tests d^2 >= 625 == 25^2 and does
// animation-only beyond it); it turns toward the player at most this many radians/tick
// (0.18 * a per-cast +0x36c we take as 1).
inline constexpr float kEnemyActivationRadius = 25.0f;
inline constexpr float kEnemyTurnRate = 0.18f;

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
    void clear() { actors_.clear(); player_ = nullptr; }

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
    bool id_in_use(int id) const;

    // The per-actor AI update, now LIVE for the enemy-chase case (build/mover2d_re/
    // MOVER2D.md, [C-DIS][C-COR]). A sprite enemy within kEnemyActivationRadius of the
    // player turns its heading toward the player at kEnemyTurnRate and advances at its
    // ENEMYINFO move_speed. The engine's shared mover FUN_0045f4d0 does exactly this:
    //   heading (+0x314) -> atan2(player - self), capped turn; vx=sin(h)*spd, vz=cos(h).
    // What is NOT modelled here (honestly): the movement-mode speed scaling (0.25..1.6,
    // unpinned for a chase), gravity/vertical (enemies are floor-snapped by the caller,
    // which owns the geometry), collision, and the cast-specific brain beyond "approach"
    // (attack decisions await build/attack_re/). The tick ORDER and the sweep around
    // this are validated. Vertical is the SAME quadratic model as the player
    // (physics_player.hpp) -- one path, FUN_0045a510, confirmed by build/player_re/.
    void update_actor(Actor& a);
};

}  // namespace cleria
