#include "cleria/ymo.hpp"

#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace cleria {
namespace {

// Little-endian cursor with bounds checking.
struct Reader {
    std::span<const std::uint8_t> b;
    std::size_t pos = 0;

    void need(std::size_t at, std::size_t n) const {
        if (at + n > b.size())
            throw std::runtime_error("YMO: read past end of buffer");
    }
    std::uint32_t u32(std::size_t at) const {
        need(at, 4);
        std::uint32_t v;
        std::memcpy(&v, b.data() + at, 4);
        return v;
    }
    float f32(std::size_t at) const {
        std::uint32_t u = u32(at);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    Vec3 vec3(std::size_t at) const { return {f32(at), f32(at + 4), f32(at + 8)}; }
    std::string cstr(std::size_t at, std::size_t cap) const {
        need(at, cap);
        const char* p = reinterpret_cast<const char*>(b.data() + at);
        std::size_t n = 0;
        while (n < cap && p[n] != '\0') ++n;
        return std::string(p, n);
    }
};

constexpr std::size_t kHeader = 0xA3 * 4;     // 652
constexpr std::size_t kMaterial = 0xBB * 4;   // 748
constexpr std::size_t kNode = 0x2A0;          // 672
constexpr std::size_t kMeshHeader = 0xB4;     // 180
constexpr std::size_t kVbDesc = 0x14;         // 20
constexpr std::size_t kIbDesc = 0x14;         // 20

}  // namespace

Vec3 VertexBuffer::position(std::uint32_t i) const {
    Vec3 v;
    std::memcpy(&v, data.data() + std::size_t(i) * stride, sizeof(float) * 3);
    return v;
}

Vec3 VertexBuffer::normal(std::uint32_t i) const {
    if (!has_normal()) return {};
    Vec3 v;
    std::memcpy(&v, data.data() + std::size_t(i) * stride + 12, sizeof(float) * 3);
    return v;
}

int VertexBuffer::uv0_offset() const {
    // Both real FVFs (0x1D2, 0x2D2) place TEX0 at byte 32. Declarations (fvf==0)
    // and the unused 0x1C2 aren't decoded here.
    return (fvf == 0x1D2 || fvf == 0x2D2) ? 32 : -1;
}

std::uint32_t VertexBuffer::diffuse(std::uint32_t i) const {
    if (!has_diffuse()) return 0xFFFFFFFF;
    std::uint32_t c;
    std::memcpy(&c, data.data() + std::size_t(i) * stride + 24, 4);
    return c;
}

// --- declaration-path raw reads ---------------------------------------------
// Bounds-checked so a wrong offset yields zeros instead of reading off the end.
bool VertexBuffer::in_range(std::uint32_t i, int off, std::size_t n) const {
    if (off < 0 || i >= count) return false;
    return std::size_t(i) * stride + std::size_t(off) + n <= data.size();
}

float VertexBuffer::raw_f32(std::uint32_t i, int off) const {
    if (!in_range(i, off, 4)) return 0.0f;
    float f;
    std::memcpy(&f, data.data() + std::size_t(i) * stride + off, 4);
    return f;
}

Vec3 VertexBuffer::raw_vec3(std::uint32_t i, int off) const {
    if (!in_range(i, off, 12)) return {0, 0, 0};
    float f[3];
    std::memcpy(f, data.data() + std::size_t(i) * stride + off, 12);
    return {f[0], f[1], f[2]};
}

std::uint32_t VertexBuffer::raw_u32(std::uint32_t i, int off) const {
    if (!in_range(i, off, 4)) return 0xFFFFFFFF;
    std::uint32_t c;
    std::memcpy(&c, data.data() + std::size_t(i) * stride + off, 4);
    return c;
}

void VertexBuffer::raw_uv(std::uint32_t i, int off, float& u, float& v) const {
    u = v = 0;
    if (!in_range(i, off, 8)) return;
    float f[2];
    std::memcpy(f, data.data() + std::size_t(i) * stride + off, 8);
    u = f[0];
    v = f[1];
}

void VertexBuffer::uv0(std::uint32_t i, float& u, float& v) const {
    const int off = uv0_offset();
    if (off < 0) { u = 0; v = 0; return; }
    float uv[2];
    std::memcpy(uv, data.data() + std::size_t(i) * stride + off, sizeof(uv));
    u = uv[0];
    v = uv[1];
}

Model Model::parse(std::span<const std::uint8_t> bytes) {
    Reader r{bytes};
    if (r.u32(0) != 0x004F4D59)
        throw std::runtime_error("YMO: bad magic");
    if (r.u32(4) != 0x11)
        throw std::runtime_error("YMO: unexpected version");

    Model mo;
    const std::uint32_t n_mat = r.u32(0x10);
    const std::uint32_t n_clip = r.u32(0x14);  // animation — skipped here
    const std::uint32_t n_mesh = r.u32(0x18);
    const std::uint32_t n_node = r.u32(0x1C);
    (void)n_clip;
    mo.flags = r.u32(0x08);
    mo.flags2 = r.u32(0x0C);
    mo.aabb_min = r.vec3(0x24);
    mo.aabb_max = r.vec3(0x30);

    std::size_t o = kHeader;

    // 1. Materials.
    mo.materials.reserve(n_mat);
    for (std::uint32_t i = 0; i < n_mat; ++i) {
        Material m;
        m.flags = r.u32(o + 0x00);
        m.blend = r.u32(o + 0x0C);
        for (int k = 0; k < 4; ++k) m.diffuse[k] = r.f32(o + 0x10 + k * 4);
        m.scroll_u = r.f32(o + 0x44);
        m.scroll_v = r.f32(o + 0x48);
        m.name = r.cstr(o + 0x4C, 0x60);
        // Texture slots are 0xC0 apart, not 0x30: 0xAC + 3*0xC0 == 0x2EC == the
        // record size. Reading at 0x30 lands inside slot 0's buffer and returns "".
        for (int t = 0; t < 3; ++t)
            m.texture[t] = r.cstr(o + 0xAC + std::size_t(t) * 0xC0, 0xC0);
        mo.materials.push_back(std::move(m));
        o += kMaterial;
    }

    // 2. Nodes (D3DXFRAME hierarchy).
    mo.nodes.reserve(n_node);
    for (std::uint32_t i = 0; i < n_node; ++i) {
        Node nd;
        nd.name = r.cstr(o + 0x000, 0x60);
        nd.first_child = r.cstr(o + 0x060, 0x60);
        nd.next_sibling = r.cstr(o + 0x0C0, 0x60);
        nd.parent = r.cstr(o + 0x120, 0x60);
        nd.mesh = r.cstr(o + 0x180, 0x60);
        // A @0x1E0 = local transform; B @0x220 = bone offset (inverse-bind).
        // Reading B as the local matrix is invisible on rooms (B is identity there)
        // but wrong for every skinned model. See docs/formats/YMO.md#skinning.
        for (int k = 0; k < 16; ++k) nd.local.m[k] = r.f32(o + 0x1E0 + k * 4);
        for (int k = 0; k < 16; ++k) nd.offset.m[k] = r.f32(o + 0x220 + k * 4);
        // S, R, T — D3DX's RegisterAnimationSRTKeys order. 0x260 is SCALE and 0x26C
        // is TRANSLATION, not the reverse: composing them the other way reproduces
        // `local` for 0 of 7016 nodes, this way for 5832.
        nd.scale = r.vec3(o + 0x260);
        nd.translation = r.vec3(o + 0x26C);
        for (int k = 0; k < 4; ++k) nd.quat[k] = r.f32(o + 0x278 + k * 4);
        mo.nodes.push_back(std::move(nd));
        o += kNode;
    }

    // 3. Meshes.
    mo.meshes.reserve(n_mesh);
    for (std::uint32_t i = 0; i < n_mesh; ++i) {
        Mesh me;
        me.name = r.cstr(o + 0x00, 0x60);
        const std::uint32_t n_submesh = r.u32(o + 0x88);
        const std::uint32_t n_vb = r.u32(o + 0x8C);
        const std::uint32_t n_ib = r.u32(o + 0x90);
        // +0x94 is the skinning PALETTE SIZE (max 24 = vs_1_1's constant budget),
        // and it is also why the submesh stride is 0x20 + palette*4: the trailing
        // dwords are the D3DXBONECOMBINATION's BoneId[] array.
        const std::uint32_t n_idxlen = r.u32(o + 0x94);
        me.palette_size = n_idxlen;
        me.influences = r.u32(o + 0x98);   // 1..4, or 0 when unskinned
        const std::uint32_t n_nodebind = r.u32(o + 0x9C);
        o += kMeshHeader;
        // The bone list: `n_nodebind` names of 0x60 bytes each.
        me.bones.reserve(n_nodebind);
        for (std::uint32_t b = 0; b < n_nodebind; ++b)
            me.bones.push_back(r.cstr(o + std::size_t(b) * 0x60, 0x60));
        o += std::size_t(n_nodebind) * 0x60;

        // Submeshes: [material, first_tri, tri_count, first_vert, vert_count, ...]
        const std::size_t sub_stride = 0x20 + std::size_t(n_idxlen) * 4;
        me.submeshes.reserve(n_submesh);
        for (std::uint32_t s = 0; s < n_submesh; ++s) {
            const std::size_t sr = o + std::size_t(s) * sub_stride;
            Submesh sm;
            sm.material = r.u32(sr + 0x00);
            sm.first_tri = r.u32(sr + 0x04);
            sm.tri_count = r.u32(sr + 0x08);
            sm.first_vert = r.u32(sr + 0x0C);
            sm.vert_count = r.u32(sr + 0x10);
            // BoneId[]: the palette. Follows the 0x20-byte record.
            sm.bone_ids.reserve(n_idxlen);
            for (std::uint32_t b = 0; b < n_idxlen; ++b)
                sm.bone_ids.push_back(r.u32(sr + 0x20 + std::size_t(b) * 4));
            me.submeshes.push_back(std::move(sm));
        }
        o += std::size_t(n_submesh) * sub_stride;

        // Vertex buffers: all descriptors, then all payloads.
        std::size_t desc = o;
        o += std::size_t(n_vb) * kVbDesc;
        me.vbs.resize(n_vb);
        for (std::uint32_t v = 0; v < n_vb; ++v) {
            VertexBuffer& vb = me.vbs[v];
            vb.count = r.u32(desc + v * kVbDesc + 8);
            vb.stride = r.u32(desc + v * kVbDesc + 0xC);
            vb.fvf = r.u32(desc + v * kVbDesc + 0x10);
            const std::size_t bytes_n = std::size_t(vb.count) * vb.stride;
            r.need(o, bytes_n);
            vb.data.assign(bytes.begin() + o, bytes.begin() + o + bytes_n);
            o += bytes_n;
        }

        // Index buffers: all descriptors, then all payloads.
        desc = o;
        o += std::size_t(n_ib) * kIbDesc;
        me.ibs.resize(n_ib);
        for (std::uint32_t b = 0; b < n_ib; ++b) {
            IndexBuffer& ib = me.ibs[b];
            ib.count = r.u32(desc + b * kIbDesc + 8);
            const std::uint32_t stride = r.u32(desc + b * kIbDesc + 0xC);
            const std::size_t bytes_n = std::size_t(ib.count) * stride;
            r.need(o, bytes_n);
            ib.idx.resize(ib.count);
            std::memcpy(ib.idx.data(), bytes.data() + o, bytes_n);
            o += bytes_n;
        }
        mo.meshes.push_back(std::move(me));
    }

    // Animation clips (section 4) are not needed for geometry; ignore the tail.
    return mo;
}

void Model::resolve_hierarchy() {
    std::unordered_map<std::string, int> node_by_name, mesh_by_name;
    for (int i = 0; i < int(nodes.size()); ++i) node_by_name[nodes[i].name] = i;
    for (int i = 0; i < int(meshes.size()); ++i) mesh_by_name[meshes[i].name] = i;

    // Skinning bone lists: name -> node index. Left at -1 if a name doesn't resolve,
    // so a bad list degrades to the bind pose rather than indexing out of bounds.
    for (Mesh& me : meshes) {
        me.bone_nodes.assign(me.bones.size(), -1);
        for (std::size_t b = 0; b < me.bones.size(); ++b) {
            auto it = node_by_name.find(me.bones[b]);
            if (it != node_by_name.end()) me.bone_nodes[b] = it->second;
        }
    }

    for (Node& n : nodes) {
        auto pi = node_by_name.find(n.parent);
        n.parent_index = (pi != node_by_name.end()) ? pi->second : -1;
        auto mi = mesh_by_name.find(n.mesh);
        n.mesh_index = (mi != mesh_by_name.end()) ? mi->second : -1;
    }

    // world = local * parent.world, memoised. Guard against malformed cycles.
    std::vector<int> state(nodes.size(), 0);  // 0=todo 1=in-progress 2=done
    auto compute = [&](auto&& self, int i) -> Mat4 {
        if (state[i] == 2) return nodes[i].world;
        if (state[i] == 1) return nodes[i].local;  // cycle: treat as root
        state[i] = 1;
        Mat4 w = nodes[i].local;
        if (nodes[i].parent_index >= 0)
            w = nodes[i].local * self(self, nodes[i].parent_index);
        nodes[i].world = w;
        state[i] = 2;
        return w;
    };
    for (int i = 0; i < int(nodes.size()); ++i) compute(compute, i);
}

}  // namespace cleria
