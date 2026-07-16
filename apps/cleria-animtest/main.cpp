// Validation harness for YMO animation: reproduce the STORED clip AABBs.
//
// Every clip header carries the AABB of the volume that clip sweeps. That is
// ground truth we did not author: pose the model at each frame, skin it, take the
// union of the vertex bounds, and compare. If the key layout, the S/R/T order, the
// frame decoding, the palette indexing or the offset*world composition is wrong in
// any way, the union will not land on the stored box.
//
//   cleria-animtest <asset_root>            validate every skinned YMO
//   cleria-animtest <file.ymo> --clips      list one model's clips
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "cleria/ymo.hpp"

namespace fs = std::filesystem;
using namespace cleria;

static std::vector<std::uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

struct Box {
    Vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
    void add(const Vec3& p) {
        mn = {std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z)};
        mx = {std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z)};
    }
    bool valid() const { return mn.x <= mx.x; }
};

// The union of a clip's skinned vertex bounds over its whole frame range.
static Box clip_box(Model& mo, const AnimClip& cl) {
    Box b;
    // Map vertex -> submesh once per mesh: the BoneId palette is per submesh.
    std::vector<std::vector<int>> vsub(mo.meshes.size());
    for (std::size_t mi = 0; mi < mo.meshes.size(); ++mi) {
        const Mesh& me = mo.meshes[mi];
        if (!me.skinned()) continue;
        vsub[mi].assign(me.vbs[1].count, -1);
        for (std::size_t s = 0; s < me.submeshes.size(); ++s) {
            const Submesh& sm = me.submeshes[s];
            for (std::uint32_t k = sm.first_vert;
                 k < sm.first_vert + sm.vert_count && k < me.vbs[1].count; ++k)
                vsub[mi][k] = int(s);
        }
    }
    for (std::uint32_t f = cl.frame_start; f <= cl.frame_end; ++f) {
        mo.pose(cl, float(f));
        for (std::size_t mi = 0; mi < mo.meshes.size(); ++mi) {
            const Mesh& me = mo.meshes[mi];
            if (!me.skinned()) continue;
            const VertexBuffer& pvb = me.vbs[1];
            for (std::uint32_t k = 0; k < pvb.count; ++k) {
                Vec3 out;
                if (mo.skin_point(me, vsub[mi][k], k, pvb.position(k), false, out)) b.add(out);
            }
        }
    }
    return b;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: cleria-animtest <asset_root> | <file.ymo> --clips\n");
        return 2;
    }
    const fs::path arg = argv[1];

    if (argc >= 3 && std::strcmp(argv[2], "--clips") == 0) {
        Model mo = Model::parse(read_file(arg));
        mo.resolve_hierarchy();
        std::printf("%s: %zu clips, %zu nodes, %zu meshes\n", arg.stem().string().c_str(),
                    mo.clips.size(), mo.nodes.size(), mo.meshes.size());
        for (const AnimClip& c : mo.clips)
            std::printf("  %-16s frames %4u..%-4u (%3u) tracks=%-4zu aabb(%.1f,%.1f,%.1f)..(%.1f,%.1f,%.1f)\n",
                        c.name.c_str(), c.frame_start, c.frame_end,
                        c.frame_end - c.frame_start + 1, c.tracks.size(),
                        c.aabb_min.x, c.aabb_min.y, c.aabb_min.z,
                        c.aabb_max.x, c.aabb_max.y, c.aabb_max.z);
        return 0;
    }

    // Corpus validation.
    int files = 0, clips = 0, pass = 0;
    double worst = 0;
    std::string worst_at;
    std::vector<std::pair<double, std::string>> fails;
    for (const auto& e : fs::recursive_directory_iterator(arg)) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::toupper((unsigned char)c));
        if (ext != ".YMO") continue;
        Model mo;
        try {
            mo = Model::parse(read_file(e.path()));
            mo.resolve_hierarchy();
        } catch (const std::exception&) { continue; }
        bool any_skin = false;
        for (const Mesh& m : mo.meshes) any_skin = any_skin || m.skinned();
        if (!any_skin || mo.clips.empty()) continue;
        ++files;
        for (const AnimClip& cl : mo.clips) {
            const Box b = clip_box(mo, cl);
            if (!b.valid()) continue;
            ++clips;
            const double d = std::max({std::fabs(b.mn.x - cl.aabb_min.x),
                                       std::fabs(b.mn.y - cl.aabb_min.y),
                                       std::fabs(b.mn.z - cl.aabb_min.z),
                                       std::fabs(b.mx.x - cl.aabb_max.x),
                                       std::fabs(b.mx.y - cl.aabb_max.y),
                                       std::fabs(b.mx.z - cl.aabb_max.z)});
            if (d <= 1e-3) ++pass;
            else fails.emplace_back(d, e.path().filename().string() + ":" + cl.name);
            if (d > worst) { worst = d; worst_at = e.path().filename().string() + ":" + cl.name; }
        }
    }
    std::printf("\nskinned files: %d   clips checked: %d   reproduced (<=1e-3): %d\n",
                files, clips, pass);
    std::printf("worst delta: %.6g  at %s\n", worst, worst_at.c_str());
    std::sort(fails.rbegin(), fails.rend());
    for (std::size_t i = 0; i < fails.size(); ++i)
        std::printf("   FAIL %10.4f  %s\n", fails[i].first, fails[i].second.c_str());
    return pass == clips ? 0 : 1;
}
