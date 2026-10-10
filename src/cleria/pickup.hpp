// World item pickups: build/item_re/PICKUPS.md. The enemy death rolls (FUN_00478510), the
// spawner (FUN_00587010) and constructor (FUN_00584c90), the per-tick motion and bounce
// (FUN_00584f90 / FUN_005856c0 / FUN_00585cb0), the collect test (FUN_00585ff0 §7.1-7.2), and
// the chest item of op 0x116 Effect_GetItem (FUN_00573210 / FUN_00586910). The grant itself is
// inventory.hpp's pickup(); the host draws the result (icon billboard, flat shadow, glint).
//
// Every speed is units per 60 Hz tick; the world is Y-up. All randomness draws from the game's
// one xorshift128 (ef2sim::Rng bound to the shared state), in the engine's order.
//
// Debris (build/mech_re/ASO_NOOPS.md §3, GOXX_PARTICLES.md §2.1) is the same object with texture
// base 0x42 (IMAGE/DEBRIS/DEBRISnnn.DDS) instead of 0x304: no pickup flag, a spin, the leaf kind.
//
// [not modelled] the water-surface probe (rooms with water mode 1), the probe modes' collider
// filters (debris probes with mode 2, items with mode 0), the sparkle particles (the host is told
// when one is emitted), a leaf in water.
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
    int kind = 0;                     // +0x147: 0 normal, 1 leaf (debris only), 2 chest item
    bool debris = false;              // the texture family: base 0x42 (debris) instead of 0x304 (items)
    float spin = 0, spin_rate = 0;    // +0x148, +0x14c (rad, rad / tick): 0 for every item
    std::uint32_t blood = 0;          // +0x130: the colour of the stain a bounce leaves (0 = none)
    int stains = 0;                   // +0x141: stains left so far (2 at most)
    int throttle = 0;                 // +0x13e: probe-less ticks left (debris in a crowd)
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
    float mod_value = -1.0f;          // CleriaCore mods: what it applies when taken (< 0 = the game's own value)

    // FUN_00584f90 render prep: the sprite alpha 0..1 (the last 35 ticks fade).
    float alpha() const;
    // FUN_00585ff0 §7.1: the white glint, a grey added to the sprite (0..70), by wall-clock ms.
    int glint(std::uint32_t ms) const;
    // FUN_00586e40: the flat shadow's alpha (0 = not drawn).
    float shadow_alpha() const;
    // FUN_00586bf0 [EXPORT, constants BYTES 0x6bd19c / 0x6bd1d8 / 0x6bd174]: the sprite is a quad in
    // the camera plane (0x7961b0) centred at pos + (0, 0.1, 0) - F * 0.15 + V * 0.075, F = 0x795f50
    // the camera forward, V = 0x795f40 the camera up (rows 2 / 1 of the camera's transposed view,
    // FUN_005eca30); in water (+0x144) the F term is dropped.
    Vec3 sprite_centre(const Vec3& cam_up, const Vec3& cam_fwd) const;
    // FUN_00586e40: the flat shadow sits at (x, ground, z) - F * 0.15, ground = +0x158 = hit y + 0.02.
    Vec3 shadow_centre(const Vec3& cam_fwd) const;
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
    bool debris = false;              // texture base 0x42: the leaf list applies
    float spin_rate = 0;              // [ebp+0x24]
    std::uint32_t colour = 0;         // [ebp+0x40] -> +0x130, the bounce stain's colour
    int bloody = 2;                   // byte 0x6d5d92 (BloodyEffectLevel; the image's value is 2)
};
// FUN_00587440's arguments to FUN_00587010 for anime_GenerateDebris(count, spin, keep, fixed, ...).
SpawnArgs debris_args(int count, bool spin, bool keep, bool fixed, std::uint32_t colour, int bloody);
// The ASO handler's colour [BYTES 0x447980..0x447a39]: 0xff550a00, or for a monster cast (0xe..0x53,
// 0xa6..0xb9) its ENEMYINFO +0xe0 | 0xff000000 (0 when that is 0).
std::uint32_t debris_colour(int cast_id, std::uint32_t enemyinfo_e0);
// ... and its gate: the five reviver casts, whose corpse does not shatter without the Silver Chimes.
bool debris_skipped(int cast_id, bool chimes);
// 0x42 + id is in the u16 list 0x6a2b5c[24]: a leaf piece.
bool debris_is_leaf(int id);
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
    // CleriaCore mods (LUA_API.md §5.2): asked for every drop that passed its roll. `slot` 0..7 the ENEMYINFO
    // slot (`chance` what its roll was under), 8..10 the Strength / Defense / MP scatter (chance 0). Returns the
    // item to spawn, < 0 for none; `*value` >= 0 becomes the pickup's mod_value. Null = retail.
    std::function<int(int item, int slot, float chance, float* value)> decide;
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
// `sparkle(p, angle, rot)` is called when the engine emits a sparkle particle (FUN_00584860; its 2 RNG
// draws are taken here, in order): `angle` the velocity's heading, `rot` the quad's rotation
// ((u - 1) * 2 pi [0x6bd898]). `window_open` is for the chest item's close-on-window form.
struct PickupWorld {
    std::function<bool(const Vec3&, const Vec3&, Vec3*, Vec3*)> hit;
    std::function<bool(Pickup&)> touch;
    std::function<void(const Pickup&, float angle, float rot)> sparkle;
    Vec3 player;                      // the player's origin P+0x50 (FUN_00585ff0 0x58606d..0x586080)
    // Out of the player's reach, the first GoLocusSword of the player in pool 4 (0x730104) collects: its
    // +0xdc..+0xe4, the centre of its first hit record +0xd0 (the trail's TIP sphere), within 0.45 of the
    // pickup (squared < 0.2025, 0x6bd214) [BYTES 0x5860bf..0x586139].
    bool sword = false;               // the player has a live sword trail
    Vec3 sword_tip;                   // its +0xdc..+0xe4
    bool window_open = false;
    int live = 0;                     // [0x75ae64]: the live kind-1 objects (the debris throttle)
    // The bounce stain FUN_0058a900(&pos, 0.75, 0.9) with the piece's colour (its draws are the callee's).
    std::function<void(const Pickup&)> stain;
};
void step_pickup(ef2sim::Rng& rng, Pickup& p, const PickupWorld& w);

// The touch test (§7.2): within 1.9 of the player's origin (P+0x50), height weighted by 2/3.
bool pickup_in_reach(const Pickup& p, const Vec3& player);
// ... or, failing that, within 0.45 of the sword trail's tip sphere.
bool pickup_in_sword_reach(const Pickup& p, const Vec3& tip);

// The sound FUN_00585ff0's tail plays at the pickup (FUN_0043a080 at 0x5868cb with [ebp-0x14]): 0x58
// player\p_takeitem (0x58614d), but 7 / 0xd / 0xe for the Strength / Defense / MP Drops 0x48..0x4d
// [BYTES 0x586591, 0x5865f1 / 0x586614, 0x586672].
int pickup_collect_se(int id);

}  // namespace cleria
