#include "cleria/scene.hpp"

#include <cstdlib>

#include <cctype>
#include <cmath>
#include <fstream>
#include <unordered_map>

#include "cleria/sob.hpp"
#include "cleria/xso.hpp"
#include "cleria/ymo.hpp"

namespace cleria {
namespace {

// A chest's identity = the g_flags index its opening-guard tests (0x5F at pc 0).
// Two chests sharing it are character variants of one logical chest.
int chest_opened_flag(const std::filesystem::path& script) {
    std::ifstream f(script, std::ios::binary);
    if (!f) return -1;
    std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                std::istreambuf_iterator<char>()};
    try {
        const std::vector<XsoInstr> code = Xso::parse(b).disasm();
        if (!code.empty() && code[0].cls == 2 && code[0].sub == 0x5F &&
            !code[0].ops.empty())
            return code[0].ops[0];
    } catch (const std::exception&) {}
    return -1;
}

namespace fs = std::filesystem;

std::vector<std::uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

fs::path resolve_ymo(const fs::path& root, std::string path) {
    for (char& c : path)
        if (c == '\\') c = '/';
    std::string lower = path;
    for (char& c : lower) c = char(std::tolower((unsigned char)c));
    if (lower.rfind("data/", 0) == 0) path = path.substr(5);
    return root / path;
}

// Transform a direction by the upper-3x3 (row-vector convention: d * M).
Vec3 xform_dir(const Mat4& m, const Vec3& d) {
    return {
        d.x * m.m[0] + d.y * m.m[4] + d.z * m.m[8],
        d.x * m.m[1] + d.y * m.m[5] + d.z * m.m[9],
        d.x * m.m[2] + d.y * m.m[6] + d.z * m.m[10],
    };
}

}  // namespace

// Build a mesh from a single YMO placed at the origin — for viewing a model that
// isn't part of a room, e.g. the bosses under ENEMY/, which no SOB ever places
// (boss rooms' SCT holds only CAST_ADOL; their model paths are hardcoded in the exe).
RoomMesh build_model(const fs::path& asset_root, const fs::path& ymo_path) {
    // A one-object SOB, synthesised: same code path as a room, identity transform.
    Sob sob;
    SobObject o;
    o.ymo_path = fs::relative(ymo_path, asset_root).string();
    if (o.ymo_path.empty() || o.ymo_path[0] == '.') o.ymo_path = ymo_path.string();
    o.position = {0, 0, 0};
    o.rotation = {0, 0, 0};
    o.scale = {1, 1, 1};
    sob.objects.push_back(std::move(o));
    return build_room_objects(asset_root, sob, ymo_path.parent_path(), nullptr, nullptr);
}

RoomMesh build_room(const fs::path& asset_root, const fs::path& sob_path,
                    const std::unordered_set<std::string>* box_whitelist,
                    const std::unordered_set<std::string>* hidden) {
    return build_room_objects(asset_root, Sob::parse(read_file(sob_path)),
                              sob_path.parent_path(), box_whitelist, hidden);
}

RoomMesh build_room_objects(const fs::path& asset_root, const Sob& sob,
                            const fs::path& script_dir,
                            const std::unordered_set<std::string>* box_whitelist,
                            const std::unordered_set<std::string>* hidden) {
    RoomMesh out;
    out.bmin = {1e30f, 1e30f, 1e30f};
    out.bmax = {-1e30f, -1e30f, -1e30f};

    std::unordered_map<std::string, int> tex_index;
    auto intern_tex = [&](std::string name) -> int {
        if (name.empty()) return -1;
        for (char& c : name) c = char(std::tolower((unsigned char)c));
        auto it = tex_index.find(name);
        if (it != tex_index.end()) return it->second;
        const int id = int(out.textures.size());
        out.textures.push_back(name);
        tex_index.emplace(std::move(name), id);
        return id;
    };

    out.objects = int(sob.objects.size());

    // Debug: CLERIA_SKIP=<substr> drops any object whose YMO path or name contains
    // <substr>, for bisecting which object produces a given artifact.
    const char* skip_env = std::getenv("CLERIA_SKIP");
    const std::string skip = skip_env ? skip_env : "";

    for (const SobObject& o : sob.objects) {
        if (!skip.empty() &&
            (o.ymo_path.find(skip) != std::string::npos || o.name.find(skip) != std::string::npos))
            continue;
        // Chests: object "box_NN" -> script "S_BOXNN.XSO" beside the room's SOB
        // (binding verified 62/62 across the corpus).
        std::string lname = o.name;
        for (char& c : lname) c = char(std::tolower((unsigned char)c));
        if (hidden && hidden->count(lname)) continue;   // a script hid this variant
        const bool is_box = lname.rfind("box", 0) == 0 && lname.size() > 4 && lname[3] == '_';
        // Character gating: a whitelisted set hides non-listed chests entirely
        // (both interactable and rendered model).
        const bool box_hidden = is_box && box_whitelist && !box_whitelist->count(lname);
        if (is_box && !box_hidden) {
            Interactable it;
            it.name = o.name;
            it.position = o.position;
            fs::path script = script_dir / ("S_BOX" + lname.substr(4) + ".XSO");
            if (fs::exists(script)) {
                it.script = script.string();
                it.opened_flag = chest_opened_flag(script);
            }
            // No whitelist -> dedupe character variants by shared opened-flag.
            bool dup = false;
            if (!box_whitelist)
                for (const Interactable& e : out.interactables)
                    if (it.opened_flag >= 0 && e.opened_flag == it.opened_flag) { dup = true; break; }
            if (!dup) out.interactables.push_back(std::move(it));
        }
        if (box_hidden) continue;  // skip this chest's geometry too

        const fs::path ymo_file = resolve_ymo(asset_root, o.ymo_path);
        if (!fs::exists(ymo_file)) { ++out.missing; continue; }
        Model model;
        try {
            model = Model::parse(read_file(ymo_file));
            model.resolve_hierarchy();
        } catch (const std::exception&) { ++out.missing; continue; }
        const Mat4 place = o.world();
        // Debug: CLERIA_MAT=1 lists materials that use texture slots beyond slot 0
        // (we only ever bind slot 0 with uv0 — multi-stage materials render wrong).
        if (std::getenv("CLERIA_MAT"))
            for (const Material& m : model.materials)
                if (!m.texture[1].empty() || !m.texture[2].empty())
                    std::printf("  [mat] %-18s tex0=%-16s tex1=%-16s tex2=%s\n",
                                m.name.c_str(), m.texture[0].c_str(),
                                m.texture[1].c_str(), m.texture[2].c_str());

        for (const Node& node : model.nodes) {
            if (node.mesh_index < 0) continue;
            const Mesh& mesh = model.meshes[node.mesh_index];
            if (mesh.vbs.empty() || mesh.ibs.empty()) continue;
            const Mat4 xf = node.world * place;  // local -> node -> room

            // Append the primary vertex buffer (submeshes index into vbs[0]).
            const VertexBuffer& vb = mesh.vbs[0];
            // Debug: CLERIA_VB=1 reports each mesh's vertex format. Buffers whose FVF
            // isn't a decoded one get uv=(0,0) for every vertex (flat single-texel look).
            if (std::getenv("CLERIA_VB"))
                std::printf("  [vb] %-28s node=%-20s fvf=%#06x stride=%2u count=%5u%s\n",
                            o.ymo_path.c_str(), node.name.c_str(), vb.fvf, vb.stride,
                            vb.count, vb.uv0_offset() < 0 ? "   <-- NO UV0 (uv=0,0!)" : "");
            const std::uint32_t base = std::uint32_t(out.vertices.size());
            for (std::uint32_t k = 0; k < vb.count; ++k) {
                const Vec3 p = xf.transform_point(vb.position(k));
                Vec3 n = xform_dir(xf, vb.normal(k));
                const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (len > 1e-6f) { n.x /= len; n.y /= len; n.z /= len; }
                float u, v; vb.uv0(k, u, v);
                const std::uint32_t d = vb.diffuse(k);  // 0xAARRGGBB
                out.vertices.push_back({p.x, p.y, p.z, n.x, n.y, n.z, u, v,
                                        std::uint8_t((d >> 16) & 0xFF),
                                        std::uint8_t((d >> 8) & 0xFF),
                                        std::uint8_t(d & 0xFF),
                                        std::uint8_t((d >> 24) & 0xFF)});
                out.bmin = {std::min(out.bmin.x, p.x), std::min(out.bmin.y, p.y),
                            std::min(out.bmin.z, p.z)};
                out.bmax = {std::max(out.bmax.x, p.x), std::max(out.bmax.y, p.y),
                            std::max(out.bmax.z, p.z)};
            }

            const IndexBuffer& ib = mesh.ibs[0];
            auto emit = [&](std::uint32_t first_idx, std::uint32_t idx_count, int tex) {
                if (idx_count == 0) return;
                const std::uint32_t out_first = std::uint32_t(out.indices.size());
                for (std::uint32_t j = 0; j < idx_count; ++j) {
                    const std::size_t src = std::size_t(first_idx) + j;
                    if (src < ib.idx.size())
                        out.indices.push_back(base + ib.idx[src]);
                }
                out.batches.push_back({out_first, idx_count, tex});
            };

            if (!mesh.submeshes.empty()) {
                for (const Submesh& sm : mesh.submeshes) {
                    int tex = -1;
                    if (sm.material < model.materials.size())
                        tex = intern_tex(model.materials[sm.material].texture[0]);
                    emit(sm.first_tri * 3, sm.tri_count * 3, tex);
                }
            } else {
                int tex = model.materials.empty()
                              ? -1 : intern_tex(model.materials[0].texture[0]);
                emit(0, std::uint32_t(ib.idx.size()), tex);
            }
        }
    }
    return out;
}

}  // namespace cleria
