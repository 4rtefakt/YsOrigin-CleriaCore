// Room assembly: turn a SOB + its referenced YMOs into GPU-ready geometry plus
// per-material draw batches. Shared by cleria-dump, cleria-view, future tools.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
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
};

struct RoomMesh {
    std::vector<RoomVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DrawBatch> batches;
    std::vector<std::string> textures;  // resolved absolute DDS paths (see scene.cpp)
    std::vector<Interactable> interactables;
    // Collision meshes only (build_collision): the material name per triangle is
    // the physical SURFACE TYPE — "stone", "wood", "water", "ice", plus the control
    // names "stopper" (invisible wall) and "door". `tri_surface` has one entry per
    // triangle (indices.size()/3), indexing `surfaces`; -1 when unknown.
    std::vector<std::string> surfaces;
    std::vector<int> tri_surface;
    Vec3 bmin, bmax;                     // world-space bounds
    int objects = 0, missing = 0;
};

// If box_whitelist is non-null, chest objects ("box_NN") are included (geometry
// and interactable) only when their lower-cased name is in the set — this gates
// them by character. Null = include all chests (with opened-flag dedup).
// `hidden` (lower-cased object names) drops those objects entirely. Some rooms
// stack several variants of one object at a single spot — e.g. S_1000 places
// save_01/02/03 together — and a script picks which one is visible.
RoomMesh build_room(const std::filesystem::path& asset_root,
                    const std::filesystem::path& sob_path,
                    const std::unordered_set<std::string>* box_whitelist = nullptr,
                    const std::unordered_set<std::string>* hidden = nullptr);

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
                         const std::unordered_set<std::string>* hidden = nullptr);

// Shared by all three: assemble already-parsed placements. `script_dir` is where a
// chest's S_BOXNN.XSO is looked up. `collision` swaps each object's YMO for its `_`
// twin, fills `surfaces`/`tri_surface`, and skips the chest-script scan.
RoomMesh build_room_objects(const std::filesystem::path& asset_root, const Sob& sob,
                            const std::filesystem::path& script_dir,
                            const std::unordered_set<std::string>* box_whitelist = nullptr,
                            const std::unordered_set<std::string>* hidden = nullptr,
                            bool collision = false,
                            const char* clip_name = nullptr, float frame = 0);

}  // namespace cleria
