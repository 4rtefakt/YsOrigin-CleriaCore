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

    // 6b. A box's vertical corner edge (S_1080's wall pillar, x <= -10.8, z in [-10, -8]): a sphere
    //     grazing past the corner is stopped by the EDGE, not let into the box.
    {
        const float X = -10.8f, X0 = -12.4f, Z1 = -8.0f, Z0 = -10.0f, Y0 = -18.0f, Y1 = -14.0f;
        RoomMesh m = mesh({{X, Y0, Z0}, {X, Y1, Z0}, {X, Y1, Z1},  {X, Y0, Z0}, {X, Y1, Z1}, {X, Y0, Z1},
                           {X0, Y0, Z1}, {X, Y0, Z1}, {X, Y1, Z1},  {X0, Y0, Z1}, {X, Y1, Z1}, {X0, Y1, Z1},
                           {X0, Y0, Z0}, {X0, Y1, Z0}, {X, Y1, Z0},  {X0, Y0, Z0}, {X, Y1, Z0}, {X, Y0, Z0}});
        const Vec3 o{-10.123f, -16.932f, -7.993f};
        const Vec3 p = collide_and_slide(m, o, {-0.098f, 0, 0.006f}, 0.67f);
        check(!sphere_overlaps(m, o, 0.67f) && !sphere_overlaps(m, p, 0.67f) && p.x > -10.14f,
              "a sphere grazing a box corner is stopped by the vertical edge");
        // ... and one already 0.17 inside a face (an arena clamp put it there) moving out of it is let go.
        const Vec3 q{-10.3f, -16.0f, -9.0f}, qv{0.116f, 0, 0.007f};
        const Vec3 pq = collide_and_slide(m, q, qv, 0.67f);
        check(sphere_overlaps(m, q, 0.67f) && std::fabs(pq.x - (q.x + qv.x)) < 1e-4f && std::fabs(pq.z - (q.z + qv.z)) < 1e-4f,
              "a sphere inside a face moving out of it is not held (FUN_005dca20's type-4 contact)");
    }

    // 6c. The parallel skip (build/physics_re/SWEEP_PUSHOUT.md §6.3, cases 1, 3 and 6; the others
    //     need the type-4 push-out, not ported). floor_y's diagonal runs along x = z: start off it.
    {
        const auto near3 = [](Vec3 p, Vec3 e, float tol) {
            return std::fabs(p.x - e.x) < tol && std::fabs(p.y - e.y) < tol && std::fabs(p.z - e.z) < tol;
        };
        const RoomMesh f = floor_y(0.0f);
        // 1. M470 (radius 1.8, origin 1.575 over the floor): exactly parallel, the floor is skipped.
        Vec3 p = collide_and_slide(f, {2, 1.575f, -3}, {0.1125f, 0, 0}, 1.8f);
        check(near3(p, {2.1125f, 1.575f, -3}, 1e-4f), "parallel: a flat floor the sphere stands inside is skipped (M470)");
        // 3. At rest: nothing is tested, so nothing is pushed.
        p = collide_and_slide(f, {2, 1.575f, -3}, {0, 0, 0}, 1.8f);
        check(near3(p, {2, 1.575f, -3}, 1e-6f), "parallel: a resting sphere is not moved");
        // 6. 0.175 inside a wall and moving exactly along it: skipped, stays embedded.
        p = collide_and_slide(wall_x(2.0f), {1.5f, 0, 0}, {0, 0, 0.1f}, r);
        check(near3(p, {1.5f, 0, 0.1f}, 1e-4f), "parallel: a wall the sphere is inside is skipped and it stays there");
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

    // 10. Placement (FUN_00459ff0): the floor below the placed point, never a slab above it.
    {
        RoomMesh m = floor_y(0.0f);                        // a stacked room: floor 0, slab at 14
        const RoomMesh top = floor_y(14.0f);
        const std::uint32_t base = std::uint32_t(m.vertices.size());
        for (const RoomVertex& v : top.vertices) m.vertices.push_back(v);
        for (std::uint32_t k : top.indices) m.indices.push_back(base + k);
        check(std::fabs(placement_floor(m, {1, 0, -8}) - 0.0f) < 1e-4f, "placement: on the floor at its own height, not the slab above");
        check(std::fabs(placement_floor(m, {1, 1.2f, -8}) - 0.0f) < 1e-4f, "placement: a floor within 1.5 below snaps");
        check(std::fabs(placement_floor(m, {1, 3.0f, -8}) - 3.0f) < 1e-4f, "placement: 3 above the floor stays (falls)");
        check(std::fabs(placement_floor(m, {1, 0.4f, -8}, true) - 0.4f) < 1e-4f, "placement: an enemy snaps only within 0.25");
        check(std::fabs(placement_floor(m, {1, 14.5f, -8}) - 14.0f) < 1e-4f, "placement: placed on the slab, stands on it");
        check(std::fabs(placement_floor(m, {50, 2, 50}) - 2.0f) < 1e-4f, "placement: no floor below keeps the placed y");
    }

    // 11. The rise against a ceiling (sweep_up; FUN_0045a510's sphere sweep, ROUTE_F13 D2).
    {
        RoomMesh m = floor_y(13.0f);                       // a ceiling slab at 13 (either winding)
        check(std::fabs(sweep_up(m, {0, 10, 0}, r, 0.1f) - 0.1f) < 1e-5f, "ceiling: free rise far below it");
        const float d = sweep_up(m, {0, 12.3f, 0}, r, 0.1f);
        check(d >= 0.0f && std::fabs(12.3f + d - (13.0f - r)) < 0.01f, "ceiling: the rise stops with the top at 13");
        check(std::fabs(sweep_up(m, {0, 12.0f, 0}, r, -0.05f) + 0.05f) < 1e-6f, "ceiling: a sink is not touched");
        // The jump kick (+0.285, unswept) can embed the sphere: the engine's type-4 contact pushes it
        // back out along (centre - closest point), here straight down by the depth.
        const float push = sweep_up(m, {0, 12.6f, 0}, r, 0.1f);
        check(std::fabs(push - (13.0f - r - 12.6f)) < 1e-4f, "ceiling: embedded below it -> pushed down by the depth");
        // Repeated kicks never carry the centre through the slab.
        float y = 12.3f;
        for (int i = 0; i < 40; ++i) { y += 0.285f; y += sweep_up(m, {0, y, 0}, r, 0.1f); }
        check(y < 13.0f - r + 0.01f, "ceiling: mashed jump kicks stay under it");
    }
    {
        RoomMesh m = wall_x(0.5f);                          // a wall 0.5 beside the centre (radius 0.675)
        check(std::fabs(sweep_up(m, {0, 1, 0}, r, 0.1f) - 0.1f) < 1e-5f, "ceiling: an overlapped wall beside does not stop the rise");
        RoomMesh f = floor_y(0.5f);                         // a floor overlapped below the centre
        check(std::fabs(sweep_up(f, {0, 1, 0}, r, 0.1f) - 0.1f) < 1e-5f, "ceiling: an overlapped floor below does not stop it");
        // An overhang whose edge passes 0.45 to the side: the sphere's rim catches it at 0.503 below.
        RoomMesh o = mesh({{0.45f, 3, -10}, {0.45f, 3, 10}, {10, 3, 10},
                           {0.45f, 3, -10}, {10, 3, 10}, {10, 3, -10}});
        const float e = sweep_up(o, {0, 2.0f, 0}, r, 0.8f);
        const float want = 3.0f - std::sqrt(r * r - 0.45f * 0.45f) - 2.0f;
        check(std::fabs(e - want) < 0.01f, "ceiling: an overhang edge catches the sphere's rim");
        // ... but a LEDGE (that top with its riser wall below, x = 0.45) keeps the engine's sphere a
        // full radius out: from sphere_wall_clear's spot the rim passes the edge (PLAYTEST_1.md §1).
        RoomMesh l = o;
        const RoomMesh riser = mesh({{0.45f, -1, -10}, {0.45f, 3, -10}, {0.45f, 3, 10},
                                     {0.45f, -1, -10}, {0.45f, 3, 10}, {0.45f, -1, 10}});
        const std::uint32_t lb = std::uint32_t(l.vertices.size());
        for (const RoomVertex& v : riser.vertices) l.vertices.push_back(v);
        for (std::uint32_t k : riser.indices) l.indices.push_back(lb + k);
        const Vec3 cl = sphere_wall_clear(l, {0, 2.0f, 0}, r);
        check(std::fabs(cl.x - (0.45f - r - 0.01f)) < 1e-3f && cl.y == 2.0f, "ledge: the sphere stands a radius off the riser");
        check(std::fabs(sweep_up(l, cl, r, 0.8f) - 0.8f) < 1e-3f, "ledge: the rise beside a ledge is not caught");
        check(std::fabs(sphere_wall_clear(o, {0, 2.0f, 0}, r).x) < 1e-6f, "ledge: no riser, no move (the overhang still catches)");
        // A wall built of stacked quads (a seam at y 2) that the sphere overlaps (a corridor too
        // narrow to clear): its inner edges never stop a rise.
        RoomMesh s = mesh({{0.5f, -2, -10}, {0.5f, 2, -10}, {0.5f, 2, 10}, {0.5f, -2, -10}, {0.5f, 2, 10}, {0.5f, -2, 10},
                           {0.5f, 2, -10}, {0.5f, 6, -10}, {0.5f, 6, 10}, {0.5f, 2, -10}, {0.5f, 6, 10}, {0.5f, 2, 10}});
        check(std::fabs(sweep_up(s, {0, 1.5f, 0}, r, 0.8f) - 0.8f) < 1e-3f, "wall seam: an overlapped wall's inner edge does not stop the rise");
    }
    // 12. The swept sphere against a triangle EDGE (the face point misses): a wall in x=2 whose
    // side edge runs at z=0.3, 0.3 off the path, is touched at x = 2 - sqrt(r^2 - 0.09) and the
    // rest of the move slides round it (deflected to -z), instead of passing straight through.
    {
        RoomMesh m = mesh({{2, -2, 0.3f}, {2, 4, 0.3f}, {2, 4, 10},
                           {2, -2, 0.3f}, {2, 4, 10}, {2, -2, 10}});
        Vec3 p = collide_and_slide(m, {0, 0, 0}, {1.39f, 0, 0}, r);
        check(std::fabs(p.x - 1.39f) < 1e-3f && std::fabs(p.z) < 1e-6f, "edge: free up to the contact");
        p = collide_and_slide(m, {0, 0, 0}, {3, 0, 0}, r);
        check(p.z < -0.05f, "edge: the sphere's side clips a wall edge 0.3 off its path and slides round it");
        RoomMesh far = mesh({{2, -2, 0.8f}, {2, 4, 0.8f}, {2, 4, 10},
                             {2, -2, 0.8f}, {2, 4, 10}, {2, -2, 10}});
        p = collide_and_slide(far, {0, 0, 0}, {3, 0, 0}, r);
        check(std::fabs(p.x - 3.0f) < 1e-3f, "edge: an edge 0.8 off the path (> radius) is passed");
    }

    std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "ALL PASS", g_fail);
    return g_fail ? 1 : 0;
}
