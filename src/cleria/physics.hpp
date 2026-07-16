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

// The surface name of triangle `tri` on a collision mesh, or "" if unknown.
// Feed it to surface_index() to reach the footstep table.
std::string_view surface_of(const RoomMesh& m, int tri);

// Push `pos` out of near-vertical wall triangles. The player is a vertical
// capsule of the given radius spanning [pos.y, pos.y+height]. Returns the
// corrected position (horizontal only).
Vec3 resolve_walls(const RoomMesh& m, Vec3 pos, float radius, float height);

}  // namespace cleria
