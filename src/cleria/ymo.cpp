#include "cleria/ymo.hpp"

#include <algorithm>
#include <cmath>
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
constexpr std::size_t kClipHeader = 0xA0;     // 160 (0x28 dwords)
constexpr std::size_t kTrackHeader = 0x70;    // name[0x60] + 3 counts + type
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

    // 4. Animation clips. Header is 0xA0 = 160 bytes:
    //   0x00 name[0x60] · 0x60 aabb_min · 0x6C aabb_max · 0x78 center · 0x84 radius
    //   0x88 u32 frame_start · 0x8C u32 frame_end · 0x90 i32 next · 0x94 f32 rate
    //   0x98 f32 crossfade rate · 0x9C u32 track_count (FUN_005f31d0 005f37d3..005f3809)
    // The AABB is this clip's swept volume — the ground truth we validate against.
    mo.clips.reserve(n_clip);
    for (std::uint32_t c = 0; c < n_clip; ++c) {
        AnimClip cl;
        cl.name = r.cstr(o + 0x00, 0x60);
        cl.aabb_min = r.vec3(o + 0x60);
        cl.aabb_max = r.vec3(o + 0x6C);
        cl.center = r.vec3(o + 0x78);
        cl.radius = r.f32(o + 0x84);
        cl.frame_start = r.u32(o + 0x88);
        cl.frame_end = r.u32(o + 0x8C);
        cl.next = std::int32_t(r.u32(o + 0x90));
        cl.rate = r.f32(o + 0x94);
        cl.blend_rate = r.f32(o + 0x98);
        const std::uint32_t n_track = r.u32(o + 0x9C);
        o += kClipHeader;

        cl.tracks.reserve(n_track);
        for (std::uint32_t t = 0; t < n_track; ++t) {
            AnimTrack tr;
            tr.node = r.cstr(o + 0x00, 0x60);
            const std::uint32_t nA = r.u32(o + 0x60);   // scale
            const std::uint32_t nB = r.u32(o + 0x64);   // rotation
            const std::uint32_t nC = r.u32(o + 0x68);   // translation
            tr.type = r.u32(o + 0x6C);
            o += kTrackHeader;

            if (tr.type == 0) {
                tr.scale.reserve(nA);
                for (std::uint32_t k = 0; k < nA; ++k, o += 0x10)
                    tr.scale.push_back({r.vec3(o), r.u32(o + 0x0C)});
                tr.rotation.reserve(nB);
                for (std::uint32_t k = 0; k < nB; ++k, o += 0x14) {
                    KeyQuat q;
                    for (int j = 0; j < 4; ++j) q.q[j] = r.f32(o + j * 4);
                    q.frame = r.u32(o + 0x10);
                    tr.rotation.push_back(q);
                }
                tr.translation.reserve(nC);
                for (std::uint32_t k = 0; k < nC; ++k, o += 0x10)
                    tr.translation.push_back({r.vec3(o), r.u32(o + 0x0C)});
            } else {
                // type != 0: type * 0x50 bytes, 16-byte aligned. Never observed —
                // type is 0 for all 28,161 tracks in the corpus. Skip defensively.
                o += std::size_t(tr.type) * 0x50;
                o = (o + 15) & ~std::size_t(15);
            }
            cl.tracks.push_back(std::move(tr));
        }
        mo.clips.push_back(std::move(cl));
    }
    return mo;
}

namespace {

// Compose a node local: S, then R, then T (D3DX RegisterAnimationSRTKeys order),
// row-vector convention to match Mat4's operator*.
Mat4 compose_srt(const Vec3& s, const float q[4], const Vec3& t) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 m = Mat4::identity();
    const float r00 = 1 - 2 * (y * y + z * z), r01 = 2 * (x * y + z * w), r02 = 2 * (x * z - y * w);
    const float r10 = 2 * (x * y - z * w), r11 = 1 - 2 * (x * x + z * z), r12 = 2 * (y * z + x * w);
    const float r20 = 2 * (x * z + y * w), r21 = 2 * (y * z - x * w), r22 = 1 - 2 * (x * x + y * y);
    m.m[0] = s.x * r00; m.m[1] = s.x * r01; m.m[2] = s.x * r02; m.m[3] = 0;
    m.m[4] = s.y * r10; m.m[5] = s.y * r11; m.m[6] = s.y * r12; m.m[7] = 0;
    m.m[8] = s.z * r20; m.m[9] = s.z * r21; m.m[10] = s.z * r22; m.m[11] = 0;
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z; m.m[15] = 1;
    return m;
}

// Sample a key array at `frame`. Keys are sorted by frame; hold before the first
// and after the last. 96.6% of arrays carry one key per frame, so interpolation is
// rarely exercised — but the gappy 3.4% need it.
template <class K, class Lerp>
bool sample(const std::vector<K>& keys, float frame, Lerp lerp, decltype(K::v)& out) {
    if (keys.empty()) return false;
    if (frame <= float(keys.front().frame)) { out = keys.front().v; return true; }
    if (frame >= float(keys.back().frame)) { out = keys.back().v; return true; }
    std::size_t i = 0;
    while (i + 1 < keys.size() && float(keys[i + 1].frame) < frame) ++i;
    const K& a = keys[i];
    const K& b = keys[std::min(i + 1, keys.size() - 1)];
    const float span = float(b.frame) - float(a.frame);
    const float t = span > 0 ? (frame - float(a.frame)) / span : 0.0f;
    out = lerp(a.v, b.v, t);
    return true;
}

Vec3 lerp3(const Vec3& a, const Vec3& b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

}  // namespace

void Model::rest_pose() {
    for (Node& n : nodes) n.local = compose_srt(n.scale, n.quat, n.translation);
    resolve_hierarchy();
}

void Model::pose(const AnimClip& clip, float frame) {
    for (const AnimTrack& tr : clip.tracks) {
        if (tr.node_index < 0 || tr.node_index >= int(nodes.size())) continue;
        Node& n = nodes[std::size_t(tr.node_index)];
        Vec3 s = n.scale, t = n.translation;
        float q[4] = {n.quat[0], n.quat[1], n.quat[2], n.quat[3]};
        sample(tr.scale, frame, lerp3, s);
        sample(tr.translation, frame, lerp3, t);
        // Rotation: nlerp with a hemisphere fix. slerp and nlerp both reproduce the
        // stored AABBs; nlerp is cheaper and the arrays are dense.
        if (!tr.rotation.empty()) {
            const auto& ks = tr.rotation;
            std::size_t i = 0;
            if (frame <= float(ks.front().frame)) {
                for (int j = 0; j < 4; ++j) q[j] = ks.front().q[j];
            } else if (frame >= float(ks.back().frame)) {
                for (int j = 0; j < 4; ++j) q[j] = ks.back().q[j];
            } else {
                while (i + 1 < ks.size() && float(ks[i + 1].frame) < frame) ++i;
                const KeyQuat& a = ks[i];
                const KeyQuat& b = ks[std::min(i + 1, ks.size() - 1)];
                const float span = float(b.frame) - float(a.frame);
                const float u = span > 0 ? (frame - float(a.frame)) / span : 0.0f;
                float dot = 0;
                for (int j = 0; j < 4; ++j) dot += a.q[j] * b.q[j];
                const float sgn = dot < 0 ? -1.0f : 1.0f;  // shortest arc
                float len = 0;
                for (int j = 0; j < 4; ++j) {
                    q[j] = a.q[j] * (1 - u) + b.q[j] * sgn * u;
                    len += q[j] * q[j];
                }
                len = std::sqrt(len);
                if (len > 1e-8f) for (int j = 0; j < 4; ++j) q[j] /= len;
            }
        }
        n.local = compose_srt(s, q, t);
    }
    resolve_hierarchy();
}

bool Model::skin_point(const Mesh& mesh, int submesh, std::uint32_t k, const Vec3& v,
                       bool is_dir, Vec3& out) const {
    if (!mesh.skinned() || submesh < 0 || submesh >= int(mesh.submeshes.size())) return false;
    const Submesh& sm = mesh.submeshes[std::size_t(submesh)];
    const VertexBuffer& avb = mesh.vbs[0];
    const std::uint32_t nb = mesh.influences;
    float w[4] = {0, 0, 0, 0};
    float sum = 0;
    for (std::uint32_t i = 0; i + 1 < nb; ++i) {
        w[i] = avb.raw_f32(k, int(i) * 4);
        sum += w[i];
    }
    w[nb - 1] = 1.0f - sum;  // the last weight is implied

    const std::uint32_t idx = avb.raw_u32(k, int(avb.stride) - 16);
    Vec3 acc{0, 0, 0};
    float wsum = 0;
    for (std::uint32_t i = 0; i < nb; ++i) {
        if (w[i] == 0.0f) continue;
        // The shader's `.zyxw` un-swizzles D3DCOLOR back to byte order, so slot i
        // is simply byte i of the dword. It is a PALETTE SLOT, not a bone id.
        const std::uint32_t slot = (idx >> (8 * i)) & 0xFF;
        if (slot >= sm.bone_ids.size()) continue;
        const std::uint32_t bid = sm.bone_ids[slot];
        if (bid == 0xFFFFFFFFu || bid >= mesh.bone_nodes.size()) continue;
        const int ni = mesh.bone_nodes[bid];
        if (ni < 0 || ni >= int(nodes.size())) continue;
        const Mat4 m = nodes[std::size_t(ni)].offset * nodes[std::size_t(ni)].world;
        Vec3 t;
        if (is_dir) {
            t = {v.x * m.m[0] + v.y * m.m[4] + v.z * m.m[8],
                 v.x * m.m[1] + v.y * m.m[5] + v.z * m.m[9],
                 v.x * m.m[2] + v.y * m.m[6] + v.z * m.m[10]};
        } else {
            t = m.transform_point(v);
        }
        acc.x += t.x * w[i];
        acc.y += t.y * w[i];
        acc.z += t.z * w[i];
        wsum += w[i];
    }
    if (wsum <= 1e-6f) return false;
    out = acc;
    return true;
}

void Model::resolve_hierarchy() {
    std::unordered_map<std::string, int> node_by_name, mesh_by_name;
    for (int i = 0; i < int(nodes.size()); ++i) node_by_name[nodes[i].name] = i;
    for (int i = 0; i < int(meshes.size()); ++i) mesh_by_name[meshes[i].name] = i;

    // Animation tracks: node name -> index.
    for (AnimClip& cl : clips)
        for (AnimTrack& tr : cl.tracks) {
            auto it = node_by_name.find(tr.node);
            tr.node_index = (it != node_by_name.end()) ? it->second : -1;
        }

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
