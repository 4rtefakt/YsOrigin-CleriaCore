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
#include <deque>

#include "cleria/actor.hpp"
#include "cleria/mathx.hpp"
#include "cleria/xso.hpp"

namespace cleria {

struct CastTable;
struct EnemyTable;
struct GameInfo;

// Ids the engine gives meaning to. `kIdSelf` never reaches the registry.
inline constexpr int kIdPlayer = 13;
inline constexpr int kIdSelf = 998;
inline constexpr int kIdPlayerAlias = 999;
// The auto-id allocator scans upward from here — just past the 186 CASTINFO records,
// so an auto-id can never collide with a default (which is a cast type). [C-DIS]
inline constexpr int kAutoIdBase = 186;

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

    std::deque<Actor>& actors() { return actors_; }
    const std::deque<Actor>& actors() const { return actors_; }

    // ** ITERATION ORDER IS UNDEFINED AND MUST STAY THAT WAY FOR NOW. ** The engine's
    // update order has not been read (it is the open question blocking the tick — see
    // docs/PLAN.md). Insertion order is what this happens to give; do not depend on
    // it, and do not let a caller's behaviour start relying on it, because a stable
    // order we invented would look right and be untestable against the game.
    std::size_t size() const { return actors_.size(); }

  private:
    WorldTables tables_;
    // ** A deque, not a vector, and that is load-bearing. ** spawn()/find()/player()
    // all hand out Actor*, and the engine's own idiom is a POOL of stable slots
    // (FUN_00434f30) with three globals pointing at the player. A vector would
    // reallocate on growth and dangle every one of those pointers — including
    // player_, which survives from CAST_ADOL's spawn to the end of the room. A deque
    // keeps references stable across push_back, which is the property we need.
    std::deque<Actor> actors_;
    Actor* player_ = nullptr;
    bool id_in_use(int id) const;
};

}  // namespace cleria
