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
// 0x21 / 0x25 / 0x26 / 0x27 / 0x28 / 0x29 / 0x2a (BULLETS.md §10.6; 0x21 the boss06 breath, ZAVA_EF.md §F.2; 0x2a the
// boomerang, M640.md §7; 0x25 / 0x26 / 0x27 boss07's homing shot and meteors, DALLES_CD1.md §4.2 / §5.5; 0x25 with
// effect 467 and 0x28 boss08's Bit, DARM_B1.md §6) — they fly as kind 1 [STAND-IN]; kind 0xf is not a bullet (BOMB.md);
// the kind-0x10/0x23 proxy victim (the player batting a drip); the struck test (§4.4) for every boss effect id
// but 0x193 (boss07's F shot, DALLES_D23.md §10) and 0x1d3 (boss08's deployed Bit); the effect-id special cases
// (0x82 rtype, 0x20/0x15 Adol fire, 0x178); the kind-0x29 water ripple and impact burst; the
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
    bool show_only = false;       // CleriaCore's own (mods, API 7; not retail): side 2, its particles hit nobody
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
    std::uint8_t n178 = 0;        // +0x178 (a byte): kind 8 the handler's tick count
    // Kind 0x24 (FUN_0059fa30, Nygtilger's plasma ball; BULLET_KINDS.md §5), written by the caster after the spawn:
    int carrier = -1;             // +0x1a8: the carrier segment (the host's id for Env::carrier; -1 = none)
    Vec3 v1ac{};                  // +0x1ac: the carrier-local vector v
    Vec3 t1b8{};                  // +0x1b8: the target T
    Vec3 a1c4{};                  // +0x1c4: the anchor A (v through the carrier's matrix)
    int n1d0 = 0;                 // +0x1d0: the handler's frame count
    bool d1d4 = false;            // +0x1d4: done
    bool f17a = false;            // +0x17a: destroyed when struck (the deployed Bit sets it past age 10, DARM_B1.md §6.2)
    // Kind 0x23 (FUN_0059e860, Nygtilger's poison ball; BULLET_KINDS.md §3). Its state is u184 (0 / 1 flying, 2
    // resting) and its carrier (+0x194, the dead segment) is `carrier`, written by the caster after the spawn.
    bool f17b = false;            // +0x17b: the struck test is on (age 60..590)
    std::uint32_t cd198 = 0;      // +0x198: the push cooldown (the tail counts it down)
    int n19c = 0;                 // +0x19c: times the player batted it
    float y1a0 = 0;               // +0x1a0: the shadow's height
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
    // The same with the ray's byte +0x49 = 1 (option bit 0): every mesh whose type byte +0x28d is 5..0xc — the
    // boss meshes — is skipped [BYTES 0x41e4d2..0x41e4f4; DALLES_CD1.md §4.2]. None = `ray`.
    std::function<RayHit(const Vec3& from, const Vec3& d, int set)> ray_no_boss;
    std::uint32_t shake_frames = 0;   // 0x76a5ac: a camera shake is running (kind 0x25's burst starts one only at 0)
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
    // Kind 0x24's carrier (+0x1a8, a boss segment) by the host's id: its matrix +0x20, its HP +0x98 and the ATK of
    // its stat block +0x94.. (the strike's effect is armed with it). False when it no longer exists.
    std::function<bool(int id, ef2sim::M4* world, float* hp, float* atk)> carrier;
    // FUN_00584550(ecx 7, argb, 0, .., &pos, 0.35, 0.32, 0.012, 0, 0): seven dust puffs at the bullet, called
    // in place (28 draws of the shared xorshift; GOXX_PARTICLES.md §3.1). None = not drawn, no draws.
    std::function<void(std::uint32_t argb, const Vec3& pos)> dust;
    // Kind 0x29's burst on impact: FUN_0058a5f0(count, 0xff22cc44, 0, life_range, size0, size1, &pos, radius, 0, 1),
    // slime puddles (6 draws each of the shared xorshift, taken by the host). None = no puddle, no draws.
    std::function<void(int count, int life_range, float size0, float size1, const Vec3& pos, float radius)> slime;
    // Kind 0x23 (the poison ball): flag 221 [0x76bc90] (the boss is dead: every ball goes), the player's body
    // radius P+0x68 and speed P+0x310, and the boss point the balls bounce off (*[0x76a2c4]: the head, or the
    // hoop centre +0x108 in mode 4).
    bool flag221 = false;
    float player_r = 0.45f, player_speed = 0;
    bool boss_point_on = false;
    Vec3 boss_point{};
};

// What an update asks the host to do.
struct Event {
    enum Type { kSound, kHeal, kPush, kShake } type = kSound;
    int id = 0;                   // kSound: the EFFECTDEF id; kShake: the frames (0x76a5ac), `push` the amplitude;
                                  // kPush: 1 = zero the knock channel 1 (+0x338 / +0x340) first (kind 0x23)
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
    // CleriaCore's own (mods, API 7): told of each bullet of the player's side as it is asked for.
    std::function<void(const Spawn&)> on_player_spawn;
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
    // DAT_0074c0d8 (Hugo's Extra version, EXTRA_VERSIONS.md §3): spawn() speeds every effect-0x82 bullet x 1.35.
    void set_hugo_extra(bool on) { hugo_extra_ = on; }

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
    int strikes_ = 0;             // DAT_007de270: kind 0x24's strike count, shared by every plasma (never reset)
    std::vector<std::unique_ptr<WaterProbe>> probes_;
    std::vector<std::unique_ptr<Bomb>> bombs_;
    std::function<std::uint32_t()> rng_;
    bool hugo_extra_ = false;
};

}  // namespace cleria::bullet
