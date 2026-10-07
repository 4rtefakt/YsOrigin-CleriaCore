// World item pickups: build/item_re/PICKUPS.md. The enemy death rolls (FUN_00478510), the
// spawner (FUN_00587010) and constructor (FUN_00584c90), the per-tick motion and bounce
// (FUN_00584f90 / FUN_005856c0 / FUN_00585cb0), the collect test (FUN_00585ff0 §7.1-7.2), and
// the chest item of op 0x116 Effect_GetItem (FUN_00573210 / FUN_00586910). The grant itself is
// inventory.hpp's pickup(); the host draws the result (icon billboard, flat shadow, glint).
//
// Every speed is units per 60 Hz tick; the world is Y-up. All randomness draws from the game's
// one xorshift128 (ef2sim::Rng bound to the shared state), in the engine's order.
//
// [not modelled] the water-surface probe (rooms with water mode 1), the probe modes' collider
// filters, the sword-trail collect point (GoLocusSword), the sparkle particles (the host is told
// when one is emitted), the bounce effect (never enabled for items).
#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "cleria/ef2_sim.hpp"
#include "cleria/mathx.hpp"

namespace cleria {

struct Enemy;

struct Pickup {
    int id = 0;                       // +0x134 (the texture is MENU/ICON/<INVINFO stem>.DDS)
    Vec3 pos, prev, vel;              // +0x50, +0x184, +0x220
    Vec3 base;                        // +0x178 (the chest item's anchor)
    float age = 0, life = 0;          // +0x150, +0x154
    int delay = 0;                    // +0x13c: ticks before it starts
    std::uint8_t flags = 0;           // +0x13d: 1 probe mode 0, 2 no ground probe, 4 pickup,
                                      //   8 never expires, 0x10 static, 0x20 chest close-on-window
    int kind = 0;                     // +0x147: 0 normal, 2 chest item
    bool resting = false;             // +0x142
    bool ground = false;              // +0x143: ground under it (the shadow is drawn)
    float ground_y = 0;               // +0x158
    bool in_water = false;            // +0x144 (whole-room water, mode 2)
    int glint_phase = 0;              // +0x13f
    int sparkle = 0;                  // +0x140
    float rise = 0;                   // +0x170 (chest item)
    int t = 0;                        // +0x174 (chest item)
    bool closing = false;             // +0x146
    float half = 0.2835f;             // +0x15c
    bool dead = false;
    int content = -1;                 // CleriaCore mods: a content pickups.ini entry (-1 = the game's own)

    // FUN_00584f90 render prep: the sprite alpha 0..1 (the last 35 ticks fade).
    float alpha() const;
    // FUN_00585ff0 §7.1: the white glint, a grey added to the sprite (0..70), by wall-clock ms.
    int glint(std::uint32_t ms) const;
    // FUN_00586e40: the flat shadow's alpha (0 = not drawn).
    float shadow_alpha() const;
};

// FUN_00587010's arguments (PICKUPS.md §4.1); the defaults are an enemy drop's.
struct SpawnArgs {
    int count = 1;
    int delay_step = 2;
    int extra_life = 1;               // lifetime += (xs() >> 1) % (extra_life + 1)
    std::uint8_t flags = 5;
    float size = 0.9f;
    float h0 = 0.01f, h1 = 0.045f;    // horizontal speed range
    float v0 = 0.04f, v1 = 0.065f;    // vertical speed range
    float yaw0 = -3.14159265f, yaw1 = 3.14159265f;
};
void spawn_pickups(ef2sim::Rng& rng, const int* bag, int n, const Vec3& pos, const SpawnArgs& a,
                   bool whole_room_water, std::vector<Pickup>& out);

// FUN_00478510's rolls: the 8 ENEMYINFO slots, then the Strength / Defense / MP Drop scatter.
struct DropContext {
    int launch = 0;                   // enemy+0x4d0, the hit-launch counter
    int combo = 0;                    // 0x76c1a8
    float luck = 0;                   // killer+0xac
    bool flag108 = false;             // count[108] == 1: +2.5 luck
    bool bless8 = false;              // blessings & 8: +5 luck
    bool no_drops = false;            // flag 146 or enemy+0x130 & 1: nothing at all
    bool boss_lock = false;           // 0x76bb8c != 0: no buff scatter
    bool whole_room_water = false;
    float rate = 1.0f;                // CleriaCore's drop-rate cheat (access.hpp): x every slot's chance
};
// `entry_draw` false: the caller already took FUN_00478510's entry draw (the death effect's
// scale) and spawned the death effects, which draw too, before these rolls.
void roll_enemy_drops(ef2sim::Rng& rng, const Enemy& rec, int cast_id, const Vec3& pos,
                      const DropContext& c, std::vector<Pickup>& out, bool entry_draw = true);

// 0x116 Effect_GetItem: the display-only chest item at a mesh (+0.3 up, life 210, kind 2).
Pickup make_chest_item(ef2sim::Rng& rng, int id, const Vec3& mesh_pos, bool flag);

// One simulation tick. `hit(a, b, point, normal)` is the room's nearest segment hit;
// `touch(p)` is called when a collectable pickup is within reach of the player and returns
// true when the grant took it (false leaves it on the ground: a full wallet / count).
// `sparkle(p)` is called when the engine would emit a sparkle particle (2 RNG draws are taken
// here, in order). `window_open` is for the chest item's close-on-window form.
struct PickupWorld {
    std::function<bool(const Vec3&, const Vec3&, Vec3*, Vec3*)> hit;
    std::function<bool(Pickup&)> touch;
    std::function<void(const Pickup&, float angle)> sparkle;
    Vec3 player;
    bool window_open = false;
};
void step_pickup(ef2sim::Rng& rng, Pickup& p, const PickupWorld& w);

// The touch test (§7.2): within 1.9 of the player, height weighted by 2/3.
bool pickup_in_reach(const Pickup& p, const Vec3& player);

}  // namespace cleria
