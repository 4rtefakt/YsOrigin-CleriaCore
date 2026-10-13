// cleria-dump: load a room's SOB, resolve every placed YMO, and export the whole
// room as a Wavefront .obj. First end-to-end exercise of the asset pipeline —
// and a differential test: the vertex/triangle totals must match tools/*.py.
//
// Usage: cleria-dump <asset_root> <room.SOB> <out.obj>
//   asset_root: directory of extracted assets (contains MAP/, ENEMY/, ...)
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>

#include "cleria/scene.hpp"
#include "cleria/sob.hpp"
#include "cleria/ymo.hpp"

namespace fs = std::filesystem;
using namespace cleria;

static std::vector<std::uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc != 4) {
        std::fprintf(stderr,
                     "usage: cleria-dump <asset_root> <room.SOB> <out.obj>\n");
        return 2;
    }
    const fs::path asset_root = argv[1];
    const fs::path sob_path = argv[2];
    const fs::path out_path = argv[3];

    Sob sob;
    try {
        sob = Sob::parse(read_file(sob_path));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "SOB parse failed: %s\n", e.what());
        return 1;
    }
    std::printf("room %s: %zu object(s)\n", sob_path.filename().string().c_str(),
                sob.objects.size());

    std::ofstream obj(out_path);
    if (!obj) {
        std::fprintf(stderr, "cannot write %s\n", out_path.string().c_str());
        return 1;
    }
    obj << "# CleriaCore room export: " << sob_path.filename().string() << "\n";

    std::uint64_t total_v = 0, total_t = 0;
    std::size_t base = 0;  // 0-based running vertex count for OBJ face indexing
    int missing = 0;

    for (const SobObject& o : sob.objects) {
        // "data\\map\\s_00\\...\\x.ymo" -> asset_root/MAP/S_00/.../X.YMO, as spelled on disk (scene.cpp).
        const fs::path ymo_file = resolve_ymo_path(asset_root, o.ymo_path);
        if (!fs::exists(ymo_file)) {
            std::printf("  MISS %-40s (%s)\n", o.ymo_path.c_str(),
                        o.name.c_str());
            ++missing;
            continue;
        }
        Model model;
        try {
            model = Model::parse(read_file(ymo_file));
            model.resolve_hierarchy();
        } catch (const std::exception& e) {
            std::printf("  FAIL %-40s %s\n", o.ymo_path.c_str(), e.what());
            continue;
        }

        const Mat4 place = o.world();
        std::uint64_t obj_v = 0, obj_t = 0;

        for (const Node& node : model.nodes) {
            if (node.mesh_index < 0) continue;
            const Mesh& mesh = model.meshes[node.mesh_index];
            const Mat4 xf = node.world * place;  // local -> node -> room

            // OBJ can't share a vertex list across differing VBs cleanly, so we
            // emit one contiguous vertex block per (mesh, vb) and index into it.
            for (std::size_t vi = 0; vi < mesh.vbs.size(); ++vi) {
                const VertexBuffer& vb = mesh.vbs[vi];
                obj << "o " << o.name << "/" << mesh.name << "#" << vi << "\n";
                for (std::uint32_t k = 0; k < vb.count; ++k) {
                    const Vec3 p = xf.transform_point(vb.position(k));
                    obj << "v " << p.x << " " << p.y << " " << p.z << "\n";
                }
                // Emit faces from the index buffer paired to this VB by order.
                if (vi < mesh.ibs.size()) {
                    const IndexBuffer& ib = mesh.ibs[vi];
                    for (std::uint32_t t = 0; t + 2 < ib.count + 1 &&
                                             t + 2 < ib.idx.size();
                         t += 3) {
                        const std::size_t a = base + ib.idx[t] + 1;
                        const std::size_t b = base + ib.idx[t + 1] + 1;
                        const std::size_t c = base + ib.idx[t + 2] + 1;
                        obj << "f " << a << " " << b << " " << c << "\n";
                        ++obj_t;
                    }
                }
                base += vb.count;
                obj_v += vb.count;
            }
        }
        std::printf("  ok   %-40s v=%llu t=%llu\n", o.ymo_path.c_str(),
                    (unsigned long long)obj_v, (unsigned long long)obj_t);
        total_v += obj_v;
        total_t += obj_t;
    }

    std::printf("\nTOTAL  vertices=%llu  triangles=%llu  (missing %d)\n",
                (unsigned long long)total_v, (unsigned long long)total_t,
                missing);
    return 0;
}
