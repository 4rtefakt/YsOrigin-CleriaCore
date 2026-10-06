// Room assembly: turn a SOB + its referenced YMOs into GPU-ready geometry plus
// per-material draw batches. Shared by cleria-dump, cleria-view, future tools.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cleria/mathx.hpp"
#include "cleria/sob.hpp"

namespace cleria {

// Interleaved vertex: position (3f) + world normal (3f) + uv0 (2f) + baked
// diffuse colour (4x u8, RGBA) = 36 bytes.
struct RoomVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    std::uint8_t r, g, b, a;
};

// A run of indices sharing one texture. texture indexes RoomMesh::textures,
// or -1 for untextured geometry.
struct DrawBatch {
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    int texture = -1;
    // The material's diffuse colour, packed R|G<<8|B<<16|A<<24. For an UNTEXTURED
    // material this IS the surface colour: S_1000's "stone" submeshes (0.78 grey) and
    // "none" submeshes (black) carry no texture and are meant to render flat-shaded
    // from this. Neutral white (0xFFFFFFFF) for textured materials, so a caller may
    // safely modulate by it either way; the view uses it to build a 1x1 fill for the
    // untextured case instead of a wrong white/black fallback.
    std::uint32_t diffuse = 0xFFFFFFFF;
    // UV scroll: the material's velocity, added to a runtime PHASE once per 60Hz
    // tick — unscaled, no dt (FUN_0044cda0 @0x44e165: the neighbouring fields ARE
    // MULSS-scaled by a delta; the scroll block has no MULSS at all). The disk phase
    // is 0.0 in 6642/6642 materials, so it is pure runtime state. Nonzero exactly
    // when the material's flag bit 0x4 is set (397/6642).
    float scroll_u = 0, scroll_v = 0;
    // The vertex range this batch owns, so a scrolling batch's UVs can be rewritten
    // in place rather than needing a per-draw uniform.
    std::uint32_t first_vert = 0, vert_count = 0;
};

// A scripted object the player can interact with (currently: treasure chests).
struct Interactable {
    std::string name;              // e.g. "box_01"
    Vec3 position;                 // world-space
    std::string script;            // absolute path to its XSO ("" if none)
    int opened_flag = -1;          // g_flags index its guard tests (chest identity)
    std::string script_name;       // SOB +0x188 as authored ("s_box01", "Save", "@Save")
    int i0 = 0, i1 = 0;            // -> $I0 / $I1
    float f0 = 0;                  // -> $F0
};

// A scene object that reacts to the world rather than to the action button: the SOB
// behaviour block's trigger classes 1 (attackable — pots and other breakables) and 2
// (step-on plates). Class 8 (action button) stays an Interactable; class 4/6 (touch
// volumes and spike damage) ride the same record and are carried here for later use.
struct SceneTrigger {
    std::string name;
    Vec3 position;
    Vec3 aabb_min{}, aabb_max{};   // world-space bounds of this object's geometry
    int trigger = 0;               // SOB +0x150 bitfield
    std::string script;            // resolved absolute XSO path ("" if unresolved)
    std::string script_stem;       // as authored ('@' = MAP/S_COMMON)
    int i0 = 0, i1 = 0;            // -> the script's $I0/$I1 (break drops, spike damage)
    float f0 = 0;                  // -> $F0
};

struct RoomMesh {
    std::vector<RoomVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DrawBatch> batches;
    std::vector<std::string> textures;  // resolved absolute DDS paths (see scene.cpp)
    std::vector<Interactable> interactables;
    std::vector<SceneTrigger> triggers;   // breakables + step plates (SOB +0x150)
    // Collision meshes only (build_collision): the material name per triangle is
    // the physical SURFACE TYPE — "stone", "wood", "water", "ice", plus the control
    // names "stopper" (invisible wall) and "door". `tri_surface` has one entry per
    // triangle (indices.size()/3), indexing `surfaces`; -1 when unknown.
    std::vector<std::string> surfaces;
    std::vector<int> tri_surface;
    Vec3 bmin, bmax;                     // world-space bounds
    int objects = 0, missing = 0;
    // Each placed object's vertex and triangle range (objects append contiguously), so a
    // scripted move or pose can rewrite just that object (rebake_object / translate_object).
    struct ObjRange {
        std::string name;                // lower-case
        std::uint32_t v0 = 0, v1 = 0;    // vertices [v0, v1)
        std::uint32_t t0 = 0, t1 = 0;    // triangles [t0, t1)
        SobObject obj;                   // the placement it was built from
        // Its YMO's animation sections (FUN_005f31d0): name, frames, next, rate.
        struct Clip { std::string name; std::int32_t start = 0, end = 0, next = -1; float rate = 0.25f; };
        std::vector<Clip> clips;
        std::string posed_clip;          // the clip it was built posed at ("" = rest)
    };
    std::vector<ObjRange> object_ranges;
    const ObjRange* object_range(const std::string& lname) const {
        for (const ObjRange& r : object_ranges) if (r.name == lname) return &r;
        return nullptr;
    }
};

// Shift one object's vertices by `d` (a scripted translation). False if it is not in `m`.
bool translate_object(RoomMesh& m, const std::string& lname, const Vec3& d);
// Re-bake one object in place, posed at `frame` of `clip` and placed at `pos` (null = its SOB
// position). False when it is not in `m` or the vertex count changed. `collision` selects the
// X_.YMO twin, as build_collision does.
// The parsed model behind a big (> 4 MB) object YMO, cached by the room builder; nullptr when it
// has not been built yet. Posing it changes what the next rebake of that object draws.
struct Model;
Model* cached_big_model(const std::filesystem::path& ymo_file);
std::filesystem::path resolve_ymo_path(const std::filesystem::path& root, const std::string& ymo);

bool rebake_object(RoomMesh& m, const std::filesystem::path& asset_root, const std::string& lname,
                   const char* clip, float frame, const Vec3* pos, bool collision,
                   const Vec3* rot = nullptr);

// If box_whitelist is non-null, chest objects ("box_NN") are included (geometry
// and interactable) only when their lower-cased name is in the set — this gates
// them by character. Null = include all chests (with opened-flag dedup).
// `hidden` (lower-cased object names) drops those objects entirely. Some rooms
// stack several variants of one object at a single spot — e.g. S_1000 places
// save_01/02/03 together — and a script picks which one is visible.
// Per-object overrides a room script applied at load: 0x175 poses an object to a named
// YMO clip, 0x173 moves it. Keyed by the SOB object name, lower-cased by the caller.
struct ObjStates {
    std::unordered_map<std::string, std::string> clip;   // 0x175 name -> clip
    std::unordered_map<std::string, std::array<float, 3>> pos;  // 0x173 name -> world pos
};

// `extra`: objects a script placed with 0x187 Mesh_Locate (the boss), appended to the SOB's.
RoomMesh build_room(const std::filesystem::path& asset_root,
                    const std::filesystem::path& sob_path,
                    const std::unordered_set<std::string>* box_whitelist = nullptr,
                    const std::unordered_set<std::string>* hidden = nullptr,
                    const std::int32_t* open_flags = nullptr,
                    const ObjStates* states = nullptr,
                    const std::vector<SobObject>* extra = nullptr);

// Build a mesh from a single YMO at the origin — for models no SOB places, such
// as the bosses under ENEMY/ (their paths are hardcoded in the exe).
// `clip_name`/`frame` pose the hierarchy before the geometry is appended, so a
// skinned model can be sampled at any animation frame (see Model::pose). Null =
// the model's own rest pose.
RoomMesh build_model(const std::filesystem::path& asset_root,
                     const std::filesystem::path& ymo_path,
                     const char* clip_name = nullptr, float frame = 0);

// The room's COLLISION geometry: the same SOB placements, but each object's `X_.YMO`
// twin instead of `X.YMO`. Those twins are the game's real collision hulls — coarse
// (S_1000: 358 tris vs the render mesh's 13,013), untextured, and their material
// names are surface types. They also carry geometry the render mesh has no reason
// to contain, such as invisible walls. Identified bit-exactly by `flags2 == 0x10`
// (400/400 twins vs 0/606 plain models). Objects with no twin are skipped.
RoomMesh build_collision(const std::filesystem::path& asset_root,
                         const std::filesystem::path& sob_path,
                         const std::unordered_set<std::string>* box_whitelist = nullptr,
                         const std::unordered_set<std::string>* hidden = nullptr,
                         const std::vector<SobObject>* extra = nullptr);

// Shared by all three: assemble already-parsed placements. `script_dir` is where a
// chest's S_BOXNN.XSO is looked up. `collision` swaps each object's YMO for its `_`
// twin, fills `surfaces`/`tri_surface`, and skips the chest-script scan.
// `open_flags`, if given, is the live g_flags array (>= 0x200 ints): a chest whose
// opened-flag is set is built posed to its `OPEN` clip (lid up), matching the game's
// on-entry `0x175(box,OPEN,0,-1)` re-pose of an already-looted chest.
RoomMesh build_room_objects(const std::filesystem::path& asset_root, const Sob& sob,
                            const std::filesystem::path& script_dir,
                            const std::unordered_set<std::string>* box_whitelist = nullptr,
                            const std::unordered_set<std::string>* hidden = nullptr,
                            bool collision = false,
                            const char* clip_name = nullptr, float frame = 0,
                            const std::int32_t* open_flags = nullptr,
                            const ObjStates* states = nullptr);

}  // namespace cleria
