// YMO model parser. Format fully specified in docs/formats/YMO.md — a serialized
// DirectX .X hierarchy: D3DXFRAME nodes + D3DXMESHCONTAINER meshes (raw D3D9
// vertex buffers + D3DFMT_INDEX16 triangle lists).
//
// Two vertex paths: rooms and props use legacy FVFs (0x1D2/0x2D2) bound rigidly to
// nodes; the bosses use one of 8 static vertex DECLARATIONS held in the exe, and
// those are two-stream and skinned (D3DX indexed palette). We decode both, but only
// draw the declaration path in its BIND POSE — the palette isn't applied yet.
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "cleria/mathx.hpp"

namespace cleria {

// The material record's first 0x4C bytes (disk dwords 0..0x12) are copied verbatim
// into the engine's 100-byte render struct — every draw-time consumer reads that,
// so those fields are the ones that matter. `name` at 0x4C is only a label.
struct Material {
    std::uint32_t flags = 0;     // +0x00  bit 0x4 = UV scroll, 0x8 = ZENABLE, 0x400 = no cull
    std::uint32_t blend = 0;     // +0x0C  0..3, indexes the engine's blend-mode jump table
    float diffuse[4]{1, 1, 1, 1};  // +0x10  RGBA; alpha is what the alpha test reads
    float scroll_u = 0, scroll_v = 0;  // +0x44/+0x48  UV scroll velocity -> c1.xy
    std::string name;
    std::string texture[3];  // three slots, stride 0xC0; ".DDS", may be empty
};

struct VertexBuffer {
    std::uint32_t count = 0;
    std::uint32_t stride = 0;
    std::uint32_t fvf = 0;
    std::vector<std::uint8_t> data;  // raw, count*stride bytes

    // POSITION is always at offset 0 (confirmed across the corpus).
    Vec3 position(std::uint32_t i) const;
    // NORMAL sits at offset 12 for the two real FVFs; (0,0,0) if unavailable.
    Vec3 normal(std::uint32_t i) const;
    bool has_normal() const { return fvf == 0x1D2 || fvf == 0x2D2; }
    // TEX0 offset for the two real FVFs, or -1 if not decodable.
    int uv0_offset() const;
    // TEX0 (u,v) for the two real FVFs; (0,0) otherwise.
    void uv0(std::uint32_t i, float& u, float& v) const;
    // DIFFUSE vertex colour (baked lighting) at offset 24; D3DCOLOR 0xAARRGGBB.
    bool has_diffuse() const { return fvf == 0x1D2 || fvf == 0x2D2; }
    std::uint32_t diffuse(std::uint32_t i) const;

    // Declaration-path accessors: read an element at an arbitrary byte offset, for
    // the two-stream layouts whose offsets come from the exe's declarations rather
    // than an FVF. Out-of-range reads return zero rather than throwing.
    bool in_range(std::uint32_t i, int off, std::size_t n) const;
    float raw_f32(std::uint32_t i, int off) const;
    Vec3 raw_vec3(std::uint32_t i, int off) const;
    std::uint32_t raw_u32(std::uint32_t i, int off) const;
    void raw_uv(std::uint32_t i, int off, float& u, float& v) const;
};

struct IndexBuffer {
    std::uint32_t count = 0;         // number of 16-bit indices
    std::vector<std::uint16_t> idx;  // triangle list
};

// A run of triangles in the shared IB using one material. Indices are absolute
// into the mesh's vertex buffer. See docs/formats/YMO.md (submesh record).
//
// The 32-byte record is a serialized `D3DXBONECOMBINATION`:
//   { AttribId, FaceStart, FaceCount, VertexStart, VertexCount, DWORD* BoneId, 0, 0 }
// Field 5 is a leftover heap pointer from the save (nonzero on skinned models, NULL
// on static ones). The BoneId array follows the record: `palette_size` dwords, which
// is why the on-disk stride is `0x20 + palette_size*4`.
struct Submesh {
    std::uint32_t material = 0;
    std::uint32_t first_tri = 0, tri_count = 0;
    std::uint32_t first_vert = 0, vert_count = 0;
    // BoneId[]: palette slot -> index into Mesh::bones. 0xFFFFFFFF = unused slot.
    // A vertex's BLENDINDICES byte is a PALETTE SLOT, not a bone id — it must be
    // resolved through this per-submesh table.
    std::vector<std::uint32_t> bone_ids;
};

struct Mesh {
    std::string name;
    std::vector<VertexBuffer> vbs;
    std::vector<IndexBuffer> ibs;
    std::vector<Submesh> submeshes;

    // The bosses don't use an FVF — they use one of the exe's 8 static vertex
    // DECLARATIONS, and the skinned ones (`decl[4..7]`, i.e. the `_1w`..`_4w`
    // shaders) are TWO-STREAM: `VB[i]` binds to stream `i`.
    //
    //   stream 0 (stride 16/20/24/28, = 12 + 4*influences):
    //     BLENDWEIGHT float[n-1] @0 · BLENDINDICES D3DCOLOR @stride-16
    //     COLOR0 D3DCOLOR @stride-12 · TEXCOORD0 float2 @stride-8
    //   stream 1 (stride 60):
    //     POSITION float3 @0 · NORMAL float3 @12 · TANGENT0/1/2 @24/36/48
    //
    // **Stream 0 has no position.** Reading its first 12 bytes as POSITION yields
    // the blend weights, which look exactly like a tiny model at the origin — that
    // is why BOSS01 rendered as a sub-unit blob. Its real bounds are ±15 in X.
    // Note `fvf == 0 && stride == 28` does NOT imply skinned: `decl[1]` is also
    // stride 28 and unskinned. Two VBs with stream 1 at stride 60 is the tell.
    bool two_stream() const { return vbs.size() >= 2 && vbs[1].stride == 60; }

    // Skinning (D3DX indexed palette). `palette_size` (mesh +0x94) is the max bone
    // matrices addressable per submesh — 24, exactly vs_1_1's constant budget.
    // `influences` (mesh +0x98) is 1..4 and maps 1:1 to stream 0's stride
    // {16,20,24,28} in 66/66 skinned meshes; it is 0 in all 1860 unskinned ones,
    // which makes it the cleanest "is this skinned" test.
    std::uint32_t palette_size = 0;
    std::uint32_t influences = 0;
    // The bone list (mesh +0x9C): names resolved to node indices by
    // resolve_hierarchy(). Submesh::bone_ids index into this.
    std::vector<std::string> bones;
    std::vector<int> bone_nodes;  // parallel to `bones`; -1 if unresolved

    bool skinned() const { return influences > 0 && two_stream(); }
};

struct Node {
    std::string name;         // name[0], own identity
    std::string first_child;  // name[1]
    std::string next_sibling; // name[2]
    std::string parent;       // name[3]
    std::string mesh;         // name[4], bound mesh (may be empty)
    // matrix A @ 0x1E0 — the LOCAL transform (rest pose). Walk the hierarchy with
    // this. It equals compose(scale, quat, translation) below in 5832/7016 nodes;
    // animation replaces it per node per frame.
    Mat4 local;
    // matrix B @ 0x220 — the bone OFFSET (inverse-bind) matrix, used only by the
    // skinning palette as `B_i * world_i`. Identity in ~98.5% of room nodes, which
    // is exactly why reading it as `local` looked fine on rooms for so long: it
    // meant applying identity, and room vertices are already baked to world space.
    // See docs/formats/YMO.md#skinning.
    Mat4 offset;
    Vec3 translation, scale;
    float quat[4] = {0, 0, 0, 1};

    Mat4 world = Mat4::identity();  // filled by Model::resolve_hierarchy()
    int parent_index = -1;
    int mesh_index = -1;
};

// Animation keys. Value first, FRAME last — the one place Falcom deviates from
// D3DX, whose D3DXKEY_* put Time first. The trailing field is a u32 FRAME NUMBER,
// not a float time: read as a float, frame 220 decodes to the denormal 3.08e-43.
// At 60 ticks/sec, frame n is n/60 s of scaled time.
struct KeyV3 {
    Vec3 v;
    std::uint32_t frame = 0;
};
struct KeyQuat {
    float q[4] = {0, 0, 0, 1};  // x,y,z,w — 100.00% unit across 865,462 keys
    std::uint32_t frame = 0;
};

// One node's animation within a clip. Arrays are S, R, T — D3DX's
// RegisterAnimationSRTKeys order.
struct AnimTrack {
    std::string node;
    std::uint32_t type = 0;
    std::vector<KeyV3> scale;
    std::vector<KeyQuat> rotation;
    std::vector<KeyV3> translation;
    int node_index = -1;  // resolved by resolve_hierarchy()
};

struct AnimClip {
    std::string name;
    Vec3 aabb_min, aabb_max;   // ground truth: the volume this clip sweeps
    Vec3 center;
    float radius = 0;
    std::uint32_t frame_start = 0, frame_end = 0;
    std::vector<AnimTrack> tracks;
};

struct Model {
    std::uint32_t flags = 0, flags2 = 0;
    Vec3 aabb_min, aabb_max;   // the union of the clip AABBs
    std::vector<Material> materials;
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<AnimClip> clips;

    // Parse a whole YMO. Throws std::runtime_error on any inconsistency.
    static Model parse(std::span<const std::uint8_t> bytes);

    // Resolve name references into indices and compute each node's world matrix.
    void resolve_hierarchy();

    // Pose the hierarchy at `frame` of `clip`: replace each tracked node's local
    // matrix with compose(S, R, T) sampled from its keys, then recompute worlds.
    // Untracked nodes keep their rest local. Call resolve_hierarchy() first.
    void pose(const AnimClip& clip, float frame);

    // Restore every node's local matrix to its rest pose (compose of the node's own
    // S/R/T at 0x260/0x278/0x26C) and recompute worlds.
    void rest_pose();

    // Skin one vertex of `mesh` into MODEL space through the indexed palette, using
    // the current node world matrices. `submesh` selects the BoneId palette — a
    // vertex's BLENDINDICES byte is a slot into THAT, not a bone id. `is_dir` skips
    // translation (for normals). Returns false if nothing resolved, leaving the
    // caller on the bind pose.
    bool skin_point(const Mesh& mesh, int submesh, std::uint32_t k, const Vec3& v,
                    bool is_dir, Vec3& out) const;
};

}  // namespace cleria
