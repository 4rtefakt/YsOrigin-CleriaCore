// Self-verifying tests for collide_and_slide (the swept-sphere wall response).
//
// The FEEL of the movement needs a human, but the ALGORITHM's correctness does
// not: given known geometry and a velocity, the swept sphere must slide / stop /
// step in geometrically determined ways. These asserts pin that down headlessly,
// so the Fauerby port (build/physics_re/COLLISION.md) can be trusted before it is
// wired into the game.
#include <cmath>
#include <cstdio>
#include <vector>

#include "cleria/physics.hpp"
#include "cleria/scene.hpp"

using namespace cleria;

namespace {
int g_fail = 0;

RoomVertex vtx(float x, float y, float z) {
    return {x, y, z, 0, 1, 0, 0, 0, 255, 255, 255, 255};
}

// Build a mesh from a flat list of triangles (each 3 Vec3).
RoomMesh mesh(std::initializer_list<Vec3> tris) {
    RoomMesh m;
    for (const Vec3& p : tris) {
        m.indices.push_back(std::uint32_t(m.vertices.size()));
        m.vertices.push_back(vtx(p.x, p.y, p.z));
    }
    return m;
}

// A large vertical wall in the X=wx plane (its normal points toward -X), spanning
// z in [-10,10], y in [-2,4]. Two triangles.
RoomMesh wall_x(float wx) {
    return mesh({{wx, -2, -10}, {wx, 4, -10}, {wx, 4, 10},
                 {wx, -2, -10}, {wx, 4, 10}, {wx, -2, 10}});
}

// A horizontal floor at y=fy over x,z in [-10,10] (normal +Y).
RoomMesh floor_y(float fy) {
    return mesh({{-10, fy, -10}, {-10, fy, 10}, {10, fy, 10},
                 {-10, fy, -10}, {10, fy, 10}, {10, fy, -10}});
}

void check(bool ok, const char* name) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++g_fail;
}

float dist2(Vec3 a, Vec3 b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}
}  // namespace

int main() {
    const float r = 0.675f;  // player radius (CASTINFO+0x34 * 0.9)
    std::printf("collide_and_slide tests (radius %.3f):\n", r);

    // 1. No geometry -> free travel to destination.
    {
        RoomMesh m;
        Vec3 p = collide_and_slide(m, {0, 0, 0}, {3, 0, 0}, r);
        check(std::fabs(p.x - 3) < 1e-3f && std::fabs(p.z) < 1e-3f, "free move reaches destination");
    }

    // 2. Walk straight into a wall at x=2 -> stopped ~radius short, no penetration.
    {
        RoomMesh m = wall_x(2.0f);
        Vec3 p = collide_and_slide(m, {0, 0, 0}, {5, 0, 0}, r);
        check(p.x < 2.0f - r + 0.05f && p.x > 2.0f - r - 0.1f, "head-on wall stops ~radius short");
        check(p.x < 2.0f, "does not penetrate the wall");
    }

    // 3. Diagonal into the wall -> slides along Z (keeps its Z motion, blocked in X).
    {
        RoomMesh m = wall_x(2.0f);
        Vec3 p = collide_and_slide(m, {0, 0, 0}, {5, 0, 5}, r);
        check(p.x < 2.0f, "diagonal: blocked in X");
        check(p.z > 3.0f, "diagonal: slides along the wall in Z");
    }

    // 4. High speed at a thin wall -> no tunnelling (swept test, not point test).
    {
        RoomMesh m = wall_x(2.0f);
        Vec3 p = collide_and_slide(m, {0, 0, 0}, {500, 0, 0}, r);
        check(p.x < 2.0f, "high-speed does not tunnel through the wall");
    }

    // 5. Fall onto a floor at y=0 from above -> lands on it (center ~radius above).
    {
        RoomMesh m = floor_y(0.0f);
        Vec3 p = collide_and_slide(m, {0, 3, 0}, {0, -5, 0}, r);
        check(p.y > -0.05f && p.y < r + 0.05f, "falls and lands on the floor (no fall-through)");
    }

    // 6. Inside corner (walls at x=2 and z=2) pushed diagonally -> wedged, both blocked.
    {
        RoomMesh m = wall_x(2.0f);
        RoomMesh z = mesh({{-10, -2, 2}, {10, -2, 2}, {10, 4, 2},
                           {-10, -2, 2}, {10, 4, 2}, {-10, 4, 2}});
        for (auto& iv : z.indices) m.indices.push_back(iv + std::uint32_t(m.vertices.size()));
        for (auto& vv : z.vertices) m.vertices.push_back(vv);
        Vec3 p = collide_and_slide(m, {0, 0, 0}, {5, 0, 5}, r);
        check(p.x < 2.0f && p.z < 2.0f, "inside corner wedges (blocked on both walls)");
    }

    // 7. Slide along a floor while moving horizontally on it (stays grounded, moves X).
    {
        RoomMesh m = floor_y(0.0f);
        Vec3 p = collide_and_slide(m, {0, r, 0}, {3, 0, 0}, r);
        check(std::fabs(p.x - 3) < 0.1f, "walk across a floor keeps horizontal motion");
        check(std::fabs(p.y - r) < 0.1f, "walk across a floor stays at ride height");
    }

    // 9. Water (build/physics_re/WATER.md §1): a "water" triangle is in the water tree only.
    {
        RoomMesh m = floor_y(0.0f);
        const RoomMesh w = floor_y(2.0f);
        const std::uint32_t base = std::uint32_t(m.vertices.size());
        for (const RoomVertex& v : w.vertices) m.vertices.push_back(v);
        for (std::uint32_t k : w.indices) m.indices.push_back(base + k);
        m.tri_water = {0, 0, 1, 1};
        check(m.has_water(), "water: the mesh reports a water tree");
        check(std::fabs(floor_at(m, 1, 1, 10) - 0.0f) < 1e-4f, "water: floor_at stands on the floor, not the water");
        check(std::fabs(water_above(m, 1, 0.5f, 1) - 2.0f) < 1e-4f, "water: the up-ray finds the surface at 2");
        check(water_above(m, 1, 3.0f, 1) == kNoFloor, "water: nothing above the surface");
        check(!sphere_overlaps(m, {1, 2, 1}, 0.5f), "water: a sphere in the surface does not collide");
    }

    std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
