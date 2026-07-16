// YMO model parser. Format fully specified in docs/formats/YMO.md — a serialized
// DirectX .X hierarchy: D3DXFRAME nodes + D3DXMESHCONTAINER meshes (raw D3D9 FVF
// vertex buffers + D3DFMT_INDEX16 triangle lists). No skinning; rigid node bind.
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
};

struct IndexBuffer {
    std::uint32_t count = 0;         // number of 16-bit indices
    std::vector<std::uint16_t> idx;  // triangle list
};

// A run of triangles in the shared IB using one material. Indices are absolute
// into the mesh's vertex buffer. See docs/formats/YMO.md (submesh record).
struct Submesh {
    std::uint32_t material = 0;
    std::uint32_t first_tri = 0, tri_count = 0;
    std::uint32_t first_vert = 0, vert_count = 0;
};

struct Mesh {
    std::string name;
    std::vector<VertexBuffer> vbs;
    std::vector<IndexBuffer> ibs;
    std::vector<Submesh> submeshes;
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

struct Model {
    std::uint32_t flags = 0, flags2 = 0;
    Vec3 aabb_min, aabb_max;
    std::vector<Material> materials;
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;

    // Parse a whole YMO. Throws std::runtime_error on any inconsistency.
    static Model parse(std::span<const std::uint8_t> bytes);

    // Resolve name references into indices and compute each node's world matrix.
    void resolve_hierarchy();
};

}  // namespace cleria
