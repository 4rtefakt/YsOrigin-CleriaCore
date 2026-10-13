// Collision against the room's render geometry (triangle soup). Good enough for
// a grounded walk: floor via vertical raycast, walls via capsule push-out.
// No spatial structure — one room's ~20k tris brute-force fine at 60fps.
#pragma once
#include <string_view>

#include "cleria/mathx.hpp"
#include "cleria/scene.hpp"

namespace cleria {

constexpr float kNoFloor = -1e30f;

// Highest walkable floor Y at (x,z) not above `ceiling`; kNoFloor if none.
// "Walkable" = triangle normal.y > 0.5.
//
// `out_tri`, if given, receives the index of the winning triangle (its surface is
// `m.surfaces[m.tri_surface[*out_tri]]` on a collision mesh), or -1 if none.
float floor_at(const RoomMesh& m, float x, float z, float ceiling, int* out_tri = nullptr);

// The floor an actor placed at `p` stands on (FUN_00459ff0, every placement: the SCT
// spawn, a SEN arrival, a script's Chr_Pos): a ray straight down 510 from `p` itself
// (DAT_007302d0 = (0,-510,0)); a hit closer than 1.5 (DAT_006bd4d4; 0.25 for an enemy,
// +0x60 & 8: DAT_006bd244) is the floor, otherwise `p.y` verbatim and the actor falls
// from there [BYTES 0x45a003..0x45a0b9]. Never a surface above `p`.
float placement_floor(const RoomMesh& m, Vec3 p, bool enemy = false);

// The surface name of triangle `tri` on a collision mesh, or "" if unknown.
// Feed it to surface_index() to reach the footstep table.
std::string_view surface_of(const RoomMesh& m, int tri);

// Push `pos` out of near-vertical wall triangles. The player is a vertical
// capsule of the given radius spanning [pos.y, pos.y+height]. Returns the
// corrected position (horizontal only).
Vec3 resolve_walls(const RoomMesh& m, Vec3 pos, float radius, float height);

// Swept-sphere collide-and-slide (Fauerby/Nettle) — the engine's real wall
// response (build/physics_re/COLLISION.md, FUN_005dca20): the actor is a single
// sphere of `radius` swept from `pos` along `vel`; on contact the remaining motion
// is projected onto the contact plane and re-swept, up to 15 times. Unlike
// resolve_walls (a horizontal perpendicular push-out) this is a full 3-D sweep, so
// the same call slides along walls AND lands on floors/slopes. Returns the final
// position after the full `vel` is consumed or absorbed.
//
// Radius per the RE = CASTINFO+0x34 * 0.9 (0.675 for the player); the sphere center
// is the actor origin (feet), so max unassisted step-up ~= radius.
Vec3 collide_and_slide(const RoomMesh& m, Vec3 pos, Vec3 vel, float radius);

// The upward part of the engine's sphere sweep (FUN_005e92b0 + FUN_0041ea80 in FUN_0045a510,
// the cast vector includes the tick's dy): how far a sphere of radius `r` centred at `c` may
// rise of `dy` before it touches a triangle above. dy <= 0 is returned unchanged. When the
// sphere already overlaps a triangle above its centre (the engine's embedded contact, type 4),
// the result is <= 0: the push back down out of it. [approx] only the vertical component: no
// slide along a sloped ceiling, no sideways push-out.
float sweep_up(const RoomMesh& m, const Vec3& c, float r, float dy);

// Where the engine's sphere (radius `r`, centre `c`) would stand horizontally: `c` pushed out
// of the wall triangles (resolve_walls' |n.y| <= 0.6 set) to the full radius over the sphere's
// own height [c.y - r, c.y + r]; y unchanged. The engine sweeps that one sphere for every move
// (FUN_0045a510 0x45b2eb), so it never stands closer than r to a wall; CleriaCore's horizontal
// push-out keeps only a 0.45 capsule, which leaves the sphere 0.225 into a wall beside it and
// lets its rim catch the top of that wall (a step, a ledge, a wall's own upper triangles) on
// the way up. The player's ceiling ray and sweep_up start from this point. [approx]
// (PLAYTEST_1.md §1)
Vec3 sphere_wall_clear(const RoomMesh& m, const Vec3& c, float r);

// True if the segment a->b crosses any collision triangle (both faces). The engine's
// line queries (FUN_005e9380 ray + FUN_0041e420 test) — used for enemy sight / line of
// sight and the wall/ledge probes (src/cleria/enemy_ai.hpp).
bool segment_blocked(const RoomMesh& m, const Vec3& a, const Vec3& b);
// The nearest hit along a -> b (FUN_0041e420's "closest hit point" + the triangle's plane
// normal). False when nothing is hit.
bool segment_hit(const RoomMesh& m, const Vec3& a, const Vec3& b, Vec3* point, Vec3* normal);
// The same test restricted to triangles [t0, t1) (one object's range, RoomMesh::ObjRange);
// `point` (optional) receives the nearest crossing.
bool segment_hits_tris(const RoomMesh& m, const Vec3& a, const Vec3& b, std::uint32_t t0, std::uint32_t t1,
                       Vec3* point = nullptr);
// True when any collision triangle comes within `r` of `c` (a zero-sweep sphere query).
bool sphere_overlaps(const RoomMesh& m, const Vec3& c, float r);
// The same query restricted to triangles [t0, t1).
bool sphere_overlaps_tris(const RoomMesh& m, const Vec3& c, float r, std::uint32_t t0, std::uint32_t t1);
// The engine's embedded contact against the triangles [t0, t1) of one object: a sphere whose centre is within its
// radius of a triangle is pushed out along (centre - closest point) by the depth before any slide (FUN_005daa10's
// contact type 4, FUN_005dca20) [EXPORT 005daa10.c:868-899, 005dca20.c:231-236]. Returns `c` moved until no triangle
// of the range is closer than `r`. [approx] the horizontal part of each push only, taken to the full radius (the
// vertical part is the floor's and the ceiling's: sweep_up); a triangle straight above or below the centre is left.
Vec3 sphere_clear_tris(const RoomMesh& m, Vec3 c, float r, std::uint32_t t0, std::uint32_t t1);
// The water tree's surface straight above (x, y, z) within `reach` (FUN_0045c490's upward ray),
// or kNoFloor. Only collision meshes carry water triangles (RoomMesh::tri_water).
float water_above(const RoomMesh& m, float x, float y, float z, float reach = 50.0f);

}  // namespace cleria
