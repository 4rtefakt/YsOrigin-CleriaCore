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
// The water tree's surface straight above (x, y, z) within `reach` (FUN_0045c490's upward ray),
// or kNoFloor. Only collision meshes carry water triangles (RoomMesh::tri_water).
float water_above(const RoomMesh& m, float x, float y, float z, float reach = 50.0f);

}  // namespace cleria
