// The GoXx bullet (type tag 0xb), ported from build/ai_spec/BULLETS.md (FUN_00593550 /
// FUN_00593b90 spawn, FUN_00594230 update, the per-kind movers).
//
// A bullet does no damage itself: it carries one EF2 effect attached to its matrix, and that
// effect's hit particles do the damage (EFFECTHIT.md, ef2_sim.hpp). The bullet lives exactly as
// long as its effect has particles; a raised kill flag (a landed mode-2 hit, a wall contact)
// parks it. The 13 floats after the direction land at +0x144..+0x174 and mean different things
// per kind, so they are kept here as the raw slots.
//
// It also hosts XSO op 0x112's falling probe (GoXx type 0x16, not a bullet; §3.14), since it
// shares the bullet's Env (the player origin, the terrain ray) and effect system.
//
// [not modelled] kinds 3, 8, 9, 0xa, 0xc, 0xd, 0x11 and up but 0x16 / 0x1b / 0x1e / 0x1f /
// 0x21 / 0x29 / 0x2a (BULLETS.md §10.6; 0x21 the boss06 breath, ZAVA_EF.md §F.2; 0x2a the boomerang, M640.md §7) — they fly as kind 1 [STAND-IN]; kind 0xf is not a bullet (BOMB.md);
// the kind-0x10/0x23 proxy victim (the player batting a drip); the struck test (§4.4, boss
// effect ids); the effect-id special cases (0x82 rtype, 0x1e7 random scale, 0x20/0x15 Adol
// fire, 0x178, 0x1c9/0x1df splashes); the kind-0x29 water ripple and impact burst; the
// kind-0x10 spawn-actor branch; flash sprites and shadow decals (visual).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "cleria/ef2_sim.hpp"
#include "cleria/hit_volume.hpp"
#include "cleria/mathx.hpp"

namespace cleria::bullet {

// FUN_00593550's block (§1.1) plus what the spawn reads off the owner.
struct Spawn {
    int kind = 1, effect = 0;
    int owner = 0;                // an actor instance id (0 = none)
    bool owner_monster = true;    // owner +0x60 & 8: side 1, the particles test the player only
    float owner_atk = 0;          // the owner's stat block at spawn (+0xa4)
    int cls = -1;                 // side 0: the player's weapon class (§1.3)
    Vec3 pos{}, dir{};
    float speed = 0, accel = 0, gravity = 0;      // +0x144 / +0x148 / +0x14c
    float power = 0, knock = 1;                   // +0x150 / +0x154 (knock < 0: an rtype code)
    std::uint32_t f158 = 0, f15c = 0, f160 = 0;   // trail effect, trail period, homing period (ints)
    float f164 = 0;                               // homing max turn, radians
    float f168 = 0, f16c = 0, f170 = 0, f174 = 0; // per kind (§1.1)
};

// FUN_00593550's stack block as a brain pushes it (`f` = its 13 floats, Boss01Bullet::f): the
// dwords land verbatim, so f5..f7 (+0x158/+0x15c/+0x160) keep their raw bits as ints. The
// owner fields are the host's.
Spawn block_spawn(int kind, int effect, const Vec3& pos, const Vec3& dir, const float* f13);

struct Bullet {
    Spawn s;
    std::uint32_t age = 0;        // +0x17c
    std::uint32_t u184 = 0;       // +0x184: kind 2 the saved period; kind 0x10 in water
    std::uint32_t u194 = 0;       // +0x194: kinds 0x1b / 0x1e the tick count; kind 0x1f the first-tick latch
    float y190 = 0;               // +0x190
    int side = 1;                 // +0x188
    float atk = 0;                // the stat block the effect was armed with (the proxy's 0 for 0x10/0x23)
    ef2sim::Effect* fx = nullptr; // +0x180 (held while the bullet exists)
    ef2sim::M4 mat;               // +0x20: the matrix the effect follows (mode 1)
    Vec3 owner_pos{};             // the owner's last known origin (kinds 0xb / 0xe home on it)
    bool gone = false;            // +0x11 & 2: killed by its handler, the effect fades on
    bool destroyed = false;       // FUN_00594190: the record can go
    int serial = 0;               // ours: a stable handle (the engine's users keep the pointer; M420's +0x7bc)
};

// GoXx type 0x16 (FUN_0058f9a0 init, FUN_0058fa00 update; BULLETS.md §3.14): the probe XSO op
// 0x112 WaterWallHitchk drops. It falls `step` a tick from its spawn point until the terrain
// ray meets the floor (or its 601st fall); if the player's origin comes within `radius` first, it plays
// SE 0x266 and effect 68 (a splash, no hit emitter) at the player and dies. No push, no damage.
struct WaterProbe {
    Vec3 pos{};                   // +0x120..+0x128
    float radius = 0;             // +0x12c
    float step = 0;               // +0x130: the fall per tick
    std::uint32_t falls = 0;      // +0x134
    bool gone = false;            // +0x11 & 2
};

// The kind-0xf bomb (build/ai_spec/BOMB.md, M290.md §3.7): FUN_00593550 with EDX = 0xf builds a GoXx
// physical object (FUN_00584c90) that carries the effect: it flies with gravity 0.0075, bounces with
// a random restitution, rests on a slow floor bounce, re-arms its carried particle every tick from
// age 10 (a contact detonates it through the particle's impact child 267), and at age 180 spawns
// EF2 267 (m_bom01) + SE 437 and dies. Not a bullet: no owner tracking, no pickup.
struct Bomb {
    Vec3 pos{}, prev{}, vel{};    // +0x50.., +0x184.., +0x220..+0x228
    float spin = 0, spin_rate = 0;   // +0x148 / +0x14c
    float age = 0, life = 180;    // +0x150 / +0x154
    bool rest = false;            // +0x142
    int owner = 0;
    float atk = 0;                // the owner's stat block, copied at the throw
    ef2sim::Effect* fx = nullptr; // +0x214
    ef2sim::M4 mat;               // +0x210: RotZ(spin), translation pos + (0, 0.25, 0)
    bool gone = false;            // +0x11 & 2
};

struct RayHit {
    bool hit = false;
    Vec3 point{}, normal{};
};

struct Env {
    Vec3 player{};                // the player's origin (0x74c09c +0x50)
    float player_hp = 0;          // DAT_0076a730
    // FUN_0041e420 over a segment from `from` along `d`; set 1 = water, 2 = terrain.
    std::function<RayHit(const Vec3& from, const Vec3& d, int set)> ray;
    // An actor's origin and HP by instance id; false when it no longer exists.
    std::function<bool(int id, Vec3* pos, float* hp)> actor;
    // The player's live swing (P+0x118 != 0) against a victim record: FUN_004f9e90 on the
    // player's +0xd0 attacker record (hit_volume.hpp overlap). None = no live swing.
    std::function<bool(const HitShape& victim)> swing;
    int swing_rtype = 0;          // P+0x108 (u8): the live swing's reaction type
    int character = 1;            // DAT_0076bb74: 0 Adol, 1 Yunica, 2 Hugo, 3 Toal
    int difficulty = 0;           // DAT_0076bb5c: 0 Normal, 1 Hard, 2 Nightmare
    // One advance of the engine's xorshift128 (0x74622c), returning the new word. None = word 0.
    std::function<std::uint32_t()> rng;
};

// What an update asks the host to do.
struct Event {
    enum Type { kSound, kHeal, kPush } type = kSound;
    int id = 0;                   // kSound: the EFFECTDEF id
    Vec3 pos{};
    int target = 0;               // kHeal: an actor instance id, or 0 = the player
    float amount = 0;             // kHeal: HP to add (the host clamps to the max)
    Vec3 push{};                  // kPush: add (x, z) to the player's knock channel 2 (+0x344/+0x34c)
};

// FUN_00458970 with eye 0: the rotation taking the base forward (0, 0, -1) to `d`.
ef2sim::M4 look_rotation(const Vec3& d);
// FUN_004585c0: `dir` turned toward `target` by at most `max_turn` radians (3-D).
Vec3 home(const Vec3& pos, const Vec3& dir, const Vec3& target, float max_turn);

class Bullets {
  public:
    using Ef2Of = std::function<const Ef2*(int id)>;
    Bullets(ef2sim::System* fx, Ef2Of ef2_of) : fx_(fx), ef2_of_(std::move(ef2_of)) {}
    // FUN_00593550 (kind != 0xf) -> FUN_00593b90: the record, its effect armed with the hit
    // fields and attached to the bullet's matrix. Returns the bullet, or nullptr when the
    // effect could not spawn.
    Bullet* spawn(const Spawn& s);
    // FUN_00594230 over every bullet, once per logic tick (after the actors, before the
    // effects step).
    void update(const Env& env, std::vector<Event>* events);
    void clear();
    std::vector<std::unique_ptr<Bullet>>& list() { return list_; }
    // XSO op 0x112 WaterWallHitchk -> FUN_0058f9a0: a type-0x16 probe at `pos`. It is born dead
    // when `radius` or `step` is 0. update() runs it after the bullets and drops it once gone.
    WaterProbe* spawn_water_probe(const Vec3& pos, float radius, float step);
    std::vector<std::unique_ptr<WaterProbe>>& probes() { return probes_; }
    // FUN_00593550 with kind 0xf (spawn() routes it here; it returns no Bullet): the bomb. `rng` is the
    // game's xorshift (set_rng), drawn once for the spin sign. update() steps the bombs after the bullets.
    Bomb* spawn_bomb(const Spawn& s);
    std::vector<std::unique_ptr<Bomb>>& bombs() { return bombs_; }
    void set_rng(std::function<std::uint32_t()> r) { rng_ = std::move(r); }

  private:
    ef2sim::Effect* spawn_fx(int id, const Vec3& pos, const Vec3& rot, int side, float atk,
                             float power, float knock, int rtype, int status, int cls, int owner,
                             const Vec3& scale = {1, 1, 1});
    void step(Bullet& b, const Env& env, std::vector<Event>* events);
    void step_probe(WaterProbe& w, const Env& env, std::vector<Event>* events);
    void step_bomb(Bomb& b, const Env& env, std::vector<Event>* events);
    ef2sim::System* fx_;
    Ef2Of ef2_of_;
    std::vector<std::unique_ptr<Bullet>> list_;
    int serial_ = 0;
    std::vector<std::unique_ptr<WaterProbe>> probes_;
    std::vector<std::unique_ptr<Bomb>> bombs_;
    std::function<std::uint32_t()> rng_;
};

}  // namespace cleria::bullet
