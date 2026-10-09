#include "cleria/scene.hpp"
#include "cleria/vfs.hpp"   // mods: asset opens go through vfs::resolve
#include <cstdio>

#include <cstdlib>

#include <cctype>
#include <cmath>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <stdexcept>

#include "cleria/sob.hpp"
#include "cleria/xso.hpp"
#include "cleria/ymo.hpp"

namespace cleria {
namespace {

// A chest's identity = the g_flags index its opening-guard tests (0x5F at pc 0).
// Two chests sharing it are character variants of one logical chest.
int chest_opened_flag(const std::filesystem::path& script) {
    std::ifstream f(vfs::resolve(script), std::ios::binary);
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
    std::ifstream f(vfs::resolve(p), std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.string());
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// One spelling per file for the cache (resolve_ymo mixes / and \).
static std::string model_key(const fs::path& p) { return p.lexically_normal().make_preferred().string(); }

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
RoomMesh build_model(const fs::path& asset_root, const fs::path& ymo_path,
                     const char* clip_name, float frame) {
    // A one-object SOB, synthesised: same code path as a room, identity transform.
    Sob sob;
    SobObject o;
    o.ymo_path = fs::relative(ymo_path, asset_root).string();
    if (o.ymo_path.empty() || o.ymo_path[0] == '.') o.ymo_path = ymo_path.string();
    o.position = {0, 0, 0};
    o.rotation = {0, 0, 0};
    o.scale = {1, 1, 1};
    sob.objects.push_back(std::move(o));
    return build_room_objects(asset_root, sob, ymo_path.parent_path(), nullptr, nullptr,
                              false, clip_name, frame);
}

std::unordered_map<std::string, Model>& big_model_cache() {
    static std::unordered_map<std::string, Model> cache;
    return cache;
}

Model* cached_big_model(const fs::path& ymo_file) {
    auto it = big_model_cache().find(model_key(ymo_file));
    return it == big_model_cache().end() ? nullptr : &it->second;
}

fs::path resolve_ymo_path(const fs::path& root, const std::string& ymo) { return resolve_ymo(root, ymo); }

RoomMesh build_room(const fs::path& asset_root, const fs::path& sob_path,
                    const std::unordered_set<std::string>* box_whitelist,
                    const std::unordered_set<std::string>* hidden,
                    const std::int32_t* open_flags, const ObjStates* states,
                    const std::vector<SobObject>* extra) {
    Sob sob = Sob::parse(read_file(sob_path));
    if (extra) sob.objects.insert(sob.objects.end(), extra->begin(), extra->end());
    return build_room_objects(asset_root, sob,
                              sob_path.parent_path(), box_whitelist, hidden,
                              false, nullptr, 0, open_flags, states);
}

RoomMesh build_collision(const fs::path& asset_root, const fs::path& sob_path,
                         const std::unordered_set<std::string>* box_whitelist,
                         const std::unordered_set<std::string>* hidden,
                         const std::vector<SobObject>* extra, const ObjStates* states) {
    Sob sob = Sob::parse(read_file(sob_path));
    if (extra) sob.objects.insert(sob.objects.end(), extra->begin(), extra->end());
    RoomMesh m = build_room_objects(asset_root, sob, sob_path.parent_path(), box_whitelist, hidden, true,
                                    nullptr, 0, nullptr, states);
    // The two-pass split (WATER.md §1): surface 3 "water" goes to the water tree only.
    m.tri_water.assign(m.tri_surface.size(), 0);
    for (std::size_t t = 0; t < m.tri_surface.size(); ++t) {
        const int s = m.tri_surface[t];
        if (s >= 0 && std::size_t(s) < m.surfaces.size()) {
            std::string n = m.surfaces[std::size_t(s)];
            for (char& c : n) c = char(std::tolower(static_cast<unsigned char>(c)));
            m.tri_water[t] = n == "water";
        }
    }
    return m;
}

std::unordered_map<std::string, Model>& big_model_cache();
static std::function<bool(const std::string&, Model&)>& object_pose_hook() {
    static std::function<bool(const std::string&, Model&)> hook;
    return hook;
}
void set_object_pose_hook(std::function<bool(const std::string& lname, Model& m)> hook) { object_pose_hook() = std::move(hook); }
// A model parsed and resolved. `keep`: a clip-driven re-bake (an animated map object — Darm's floor re-poses every
// tick) keeps the parsed model and hands out a copy, which is what a parse would give.
static Model parsed_model(const fs::path& file, bool keep) {
    if (!keep) {
        Model m = Model::parse(read_file(file));
        m.resolve_hierarchy();
        return m;
    }
    static std::unordered_map<std::string, Model> cache;
    const std::string key = model_key(file);
    auto it = cache.find(key);
    if (it == cache.end()) {
        Model m = Model::parse(read_file(file));
        m.resolve_hierarchy();
        it = cache.emplace(key, std::move(m)).first;
    }
    return it->second;
}
RoomMesh build_room_objects(const fs::path& asset_root, const Sob& sob,
                            const fs::path& script_dir,
                            const std::unordered_set<std::string>* box_whitelist,
                            const std::unordered_set<std::string>* hidden,
                            bool collision, const char* clip_name, float frame,
                            const std::int32_t* open_flags, const ObjStates* states) {
    RoomMesh out;
    out.bmin = {1e30f, 1e30f, 1e30f};
    out.bmax = {-1e30f, -1e30f, -1e30f};

    // A material's texture slot is a path RELATIVE TO ITS OWN YMO'S DIRECTORY, with
    // the mip-set dir inserted before the filename: <ymo_dir>/<ref_dir>/{H|L}/<file>.
    // H is the high-res set, L the low. 71% of refs corpus-wide carry a relative
    // path (e.g. "..\common\00back00.dds"); S_1000's 23 are bare only because they
    // all live in its own H/.
    //
    // Resolving by bare filename against one game-wide index is a real bug: 532 of
    // 3988 basenames exist in more than one directory and 530 of those are
    // byte-distinct, so whichever the walk saw last wins. That is what made S_1000
    // draw S_61/COMMON's 00yuka32.dds — an atlas with a solid black 48x48 cell that
    // S_1000's own copy fills with stonework — and paint black slabs over its stairs.
    auto resolve_tex_fs = [](const fs::path& ymo_dir, std::string ref) -> fs::path {
        if (ref.empty()) return {};
        for (char& c : ref) {
            if (c == '\\') c = '/';
            c = char(std::toupper((unsigned char)c));  // the extractor writes UPPERCASE
        }
        const fs::path rp(ref);
        const fs::path dir = (ymo_dir / rp.parent_path()).lexically_normal();
        const fs::path file = rp.filename();
        // Guard the untextured case: with an empty filename every candidate below
        // collapses to the directory itself, which exists — and we would intern a
        // path that names no file.
        if (file.empty()) return {};
        for (const char* mip : {"H", "L"}) {
            const fs::path p = dir / mip / file;
            if (fs::exists(p)) return p;
        }
        const fs::path p = dir / file;  // some refs already name the mip dir
        if (fs::exists(p)) return p;
        // An EFTMESH model (EFFECT/EFT/pc1/ENGETURIN.YMO) names its textures from the EFT
        // root ("pc1\emgeturin.dds" -> EFT/PC1/H/EMGETURIN.DDS) [DATA: every EFTMESH.DAT model].
        for (fs::path a = ymo_dir; a.has_parent_path() && a != a.parent_path(); a = a.parent_path()) {
            std::string nm = a.filename().string();
            for (char& c : nm) c = char(std::toupper((unsigned char)c));
            if (nm != "EFT") continue;
            for (const char* mip : {"H", "L"}) {
                const fs::path q = (a / rp.parent_path()).lexically_normal() / mip / file;
                if (fs::exists(q)) return q;
            }
            break;
        }
        return fs::path{};
    };
    // ... once per (directory, reference) of this build: a model names one texture from many subsets, and an
    // animated object is re-baked every tick.
    std::unordered_map<std::string, fs::path> tex_memo;
    auto resolve_tex = [&](const fs::path& ymo_dir, const std::string& ref) -> fs::path {
        const std::string key = ymo_dir.string() + '|' + ref;
        auto it = tex_memo.find(key);
        if (it == tex_memo.end()) it = tex_memo.emplace(key, resolve_tex_fs(ymo_dir, ref)).first;
        return it->second;
    };

    // Interned by RESOLVED PATH, not basename: two YMOs in one room may legitimately
    // use the same bare name for different files.
    std::unordered_map<std::string, int> tex_index;
    auto intern_tex = [&](std::string name) -> int {
        if (name.empty()) return -1;
        auto it = tex_index.find(name);
        if (it != tex_index.end()) return it->second;
        const int id = int(out.textures.size());
        out.textures.push_back(name);
        tex_index.emplace(std::move(name), id);
        return id;
    };

    std::unordered_map<std::string, int> surf_index;
    auto intern_surf = [&](std::string name) -> int {
        if (name.empty()) return -1;
        for (char& c : name) c = char(std::tolower((unsigned char)c));
        auto it = surf_index.find(name);
        if (it != surf_index.end()) return it->second;
        const int id = int(out.surfaces.size());
        out.surfaces.push_back(name);
        surf_index.emplace(std::move(name), id);
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
        if (std::getenv("CLERIA_OBJ"))
            std::printf("  [obj] %-20s hidden=%d\n", lname.c_str(),
                        hidden && hidden->count(lname) ? 1 : 0);
        if (hidden && hidden->count(lname)) continue;   // a script hid this variant
        const bool is_box = lname.rfind("box", 0) == 0 && lname.size() > 4 && lname[3] == '_';
        // Character gating: a whitelisted set hides non-listed chests entirely
        // (both interactable and rendered model).
        const bool box_hidden = is_box && box_whitelist && !box_whitelist->count(lname);
        // An already-looted chest is drawn OPEN on entry (the room root's
        // 0x175(box,OPEN,0,-1) re-pose). Decide per-object from the live flags so a
        // fresh save (all opened-flags 0) still renders every chest closed.
        bool box_pose_open = false;
        if (is_box && !box_hidden && open_flags) {
            fs::path bscript = script_dir / ("S_BOX" + lname.substr(4) + ".XSO");
            if (fs::exists(bscript)) {
                const int of = chest_opened_flag(bscript);
                if (of >= 0 && of < 0x200 && open_flags[of] != 0) box_pose_open = true;
            }
        }
        if (is_box && !box_hidden && !collision) {
            Interactable it;
            it.name = o.name;
            it.position = o.position;
            it.script_name = o.script;
            it.i0 = o.i0; it.i1 = o.i1; it.f0 = o.f0;
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
        // Goddess/save statues: object "save_NN", run through their own script like any
        // action object (below). The character-variant statues stack at one spot (S_SAVEOBJECTCHANGE hides the
        // losers when flags are known); dedupe by position for the flagless path.
        if (lname.rfind("save", 0) == 0 && !collision) {
            bool dup = false;
            for (const Interactable& e : out.interactables) {
                const float dx = e.position.x - o.position.x, dz = e.position.z - o.position.z;
                if (dx * dx + dz * dz < 1.0f) { dup = true; break; }
            }
            if (!dup) {
                Interactable it;
                it.name = o.name;
                it.position = o.position;
                // The statue's own script (STATUE_CLEANSE.md §1.1): "Save" = the room's SAVE.XSO
                // (the cleanse, on the sealed model), "@Save" = S_COMMON/SAVE.XSO (the menu).
                it.script_name = o.script;
                it.i0 = o.i0; it.i1 = o.i1; it.f0 = o.f0;
                if (!o.script.empty()) {
                    const bool shared = o.script[0] == '@';
                    std::string stem = shared ? o.script.substr(1) : o.script;
                    for (char& c : stem) c = char(std::toupper((unsigned char)c));
                    const fs::path sp = shared ? (asset_root / "MAP" / "S_COMMON" / (stem + ".XSO"))
                                               : (script_dir / (stem + ".XSO"));
                    if (fs::exists(sp)) it.script = sp.string();
                }
                out.interactables.push_back(std::move(it));
            }
        }
        if (box_hidden) continue;  // skip this chest's geometry too

        // World-reactive objects (SOB behaviour block): attackable bodies (breakables) and
        // step-on plates. Their bounds are filled in from the geometry appended below, so
        // the record is pushed first and patched after. The room's own geometry record also
        // carries trigger=1 but has no script, which is what distinguishes it.
        const std::size_t trig_index = out.triggers.size();
        const bool is_trigger = !collision && !o.script.empty() &&
                                (o.step_on() || o.attackable() || o.touch());
        if (is_trigger) {
            SceneTrigger t;
            t.name = o.name;
            t.position = o.position;
            t.trigger = o.trigger;
            t.script_stem = o.script;
            t.i0 = o.i0; t.i1 = o.i1; t.f0 = o.f0;
            sob_battle_block(o, t);
            // '@' prefix = the shared script pool in MAP/S_COMMON, else beside the room.
            const bool shared = o.script[0] == '@';
            std::string stem = shared ? o.script.substr(1) : o.script;
            for (char& c : stem) c = char(std::toupper((unsigned char)c));
            fs::path sp = shared ? (asset_root / "MAP" / "S_COMMON" / (stem + ".XSO"))
                                 : (script_dir / (stem + ".XSO"));
            if (fs::exists(sp)) t.script = sp.string();
            out.triggers.push_back(std::move(t));
        }
        if (!collision && states && states->undrawn.count(lname)) continue;   // hidden: the trigger only
        const std::size_t trig_v0 = out.vertices.size();
        const std::size_t obj_i0 = out.indices.size();

        fs::path ymo_file = resolve_ymo(asset_root, o.ymo_path);
        if (collision) {
            // The collision twin sits beside the render model: X.YMO -> X_.YMO.
            ymo_file = ymo_file.parent_path() /
                       (ymo_file.stem().string() + "_" + ymo_file.extension().string());
        }
        if (!fs::exists(ymo_file)) { ++out.missing; continue; }
        // Big models (the bosses, 13 MB) are parsed once and re-posed in place: a script-driven
        // object is re-baked every tick it moves or animates.
        auto& big_models = big_model_cache();
        Model local_model;
        Model* model_ptr = &local_model;
        Model render_twin;                 // a collision twin's render model, posed (below)
        const Model* twin_src = nullptr;
        try {
            const std::string key = model_key(ymo_file);
            auto bit = big_models.find(key);
            if (bit != big_models.end()) {
                model_ptr = &bit->second;
                model_ptr->rest_pose();
            } else {
                local_model = parsed_model(ymo_file, clip_name != nullptr);
                std::error_code fec;
                const bool big = fs::file_size(vfs::resolve(ymo_file), fec) > (2u << 20);
                // A brain-placed object (a world matrix: boss02's 36 pieces) is re-baked every
                // tick, so its model is kept parsed too, whatever its size.
                if (big || o.has_world) {
                    // A big collision twin (X_.YMO, BOSS01_: one DEFAULT clip, its own nodes)
                    // moves with its render model: take the render model's clips once,
                    // re-resolved against the twin's node names. [H] the engine poses the
                    // twin from the same anim cursor. A small twin does the same when its object
                    // asks for it (SobObject::twin_clips: Darm's hulls carry the whole bone tree).
                    if (collision && (big || o.twin_clips)) {
                        std::string stem = ymo_file.stem().string();
                        if (!stem.empty() && stem.back() == '_') stem.pop_back();
                        auto rit = big_models.find(model_key(ymo_file.parent_path() / (stem + ymo_file.extension().string())));
                        if (rit != big_models.end()) {
                            for (const AnimClip& c : rit->second.clips) {
                                bool have = false;
                                for (const AnimClip& o : local_model.clips) have |= o.name == c.name;
                                if (!have) local_model.clips.push_back(c);
                            }
                            local_model.resolve_hierarchy();
                        }
                    }
                    model_ptr = &big_models.emplace(key, std::move(local_model)).first->second;
                }
            }
            Model& model = *model_ptr;
            // Pose before appending: skin_point() reads the node world matrices.
            // An already-looted chest snaps to the final frame of its "OPEN" clip
            // (lid up); otherwise honour the caller's global clip (boss viewer).
            // A room script's 0x175 names the clip explicitly; that wins over the
            // chest heuristic, since the chest case IS just 0x175(box, OPEN).
            const std::string* want = nullptr;
            if (states) {
                auto it = states->clip.find(lname);
                if (it != states->clip.end()) want = &it->second;
            }
            auto pose_as_asked = [&](Model& pm) {
                if (want) {
                    for (const AnimClip& c : pm.clips)
                        if (c.name == *want) { pm.pose(c, float(c.frame_end)); break; }
                } else if (box_pose_open) {
                    for (const AnimClip& c : pm.clips)
                        if (c.name == "OPEN") { pm.pose(c, float(c.frame_end)); break; }
                } else if (clip_name) {
                    for (const AnimClip& c : pm.clips)
                        if (c.name == clip_name) { pm.pose(c, frame); break; }
                }
            };
            if (!(!collision && model_ptr != &local_model && object_pose_hook() && object_pose_hook()(lname, model)))
                pose_as_asked(model);
            // A collision twin is not posed by its own clips. FUN_005601e0 binds each twin
            // mesh's node BY NAME (FUN_005f4e10) to the GoMesh's RENDER model node (entry
            // +0x80 = its index), and every update FUN_00502fe0 rebuilds the entry matrix as
            // B x posed world of that render node (D3DXMatrixMultiply(node rec +0, state +0x80)).
            // So the twin follows the render model's animation: S_1004's s10step_00_.YMO has only
            // a DEFAULT clip, yet its hull rises with the render model's UP (COLLISION.md §1c).
            // Only needed when a pose was asked for (at rest the twin's own rest nodes are used,
            // as before); the big-model twins keep their clip-copy path above.
            if (collision && model_ptr == &local_model && (want || box_pose_open || clip_name)) {
                const fs::path rfile = resolve_ymo(asset_root, o.ymo_path);
                Model* rp = cached_big_model(rfile);
                if (!rp && fs::exists(rfile)) {
                    render_twin = parsed_model(rfile, clip_name != nullptr);
                    rp = &render_twin;
                }
                if (rp) { if (rp != &render_twin) rp->rest_pose(); pose_as_asked(*rp); twin_src = rp; }
            }
        } catch (const std::exception&) { ++out.missing; continue; }
        const Model& model = *model_ptr;
        // 0x173 Obj_SetPos moved this object at load (S_1004 lays its stepping stones
        // out per character). Rebuild the placement from the script's position, keeping
        // the SOB's own rotation/scale.
        SobObject po = o;
        if (states) {
            auto it = states->pos.find(lname);
            if (it != states->pos.end())
                po.position = {it->second[0], it->second[1], it->second[2]};
        }
        const Mat4 place = po.has_world ? po.world_mat : po.world();
        // Debug: CLERIA_MAT=1 dumps the fields the engine's render struct actually
        // reads (flags/blend/diffuse/scroll), plus all three texture slots.
        if (std::getenv("CLERIA_MAT"))
            for (const Material& m : model.materials)
                std::printf("  [mat] %-18s fl=%08X bl=%u dif=%.2f,%.2f,%.2f,%.2f "
                            "uv=%+.4f,%+.4f t0=%-16s t1=%-14s t2=%s\n",
                            m.name.c_str(), m.flags, m.blend, m.diffuse[0], m.diffuse[1],
                            m.diffuse[2], m.diffuse[3], m.scroll_u, m.scroll_v,
                            m.texture[0].c_str(), m.texture[1].c_str(), m.texture[2].c_str());

        for (const Node& node : model.nodes) {
            if (node.mesh_index < 0) continue;
            const Mesh& mesh = model.meshes[node.mesh_index];
            if (mesh.vbs.empty() || mesh.ibs.empty()) continue;
            Mat4 xf = node.world * place;  // local -> node -> room
            if (twin_src)                  // the same-named render node, posed (see above)
                for (const Node& rn : twin_src->nodes)
                    if (rn.name == node.name) { xf = rn.offset * rn.world * place; break; }

            // Append the primary vertex buffer (submeshes index into vbs[0]).
            const VertexBuffer& vb = mesh.vbs[0];
            // Debug: CLERIA_VB=1 reports each mesh's vertex format. Buffers whose FVF
            // isn't a decoded one get uv=(0,0) for every vertex (flat single-texel look).
            if (std::getenv("CLERIA_VB")) {
                std::printf("  [vb] %-26s node=%-22s vbs=%zu ibs=%zu subs=%zu\n",
                            o.ymo_path.c_str(), node.name.c_str(), mesh.vbs.size(),
                            mesh.ibs.size(), mesh.submeshes.size());
                for (std::size_t s = 0; s < mesh.vbs.size(); ++s) {
                    const VertexBuffer& b = mesh.vbs[s];
                    std::printf("        stream%zu fvf=%#06x stride=%2u count=%5u%s\n", s,
                                b.fvf, b.stride, b.count,
                                b.uv0_offset() < 0 ? "   <-- no decodable UV0" : "");
                }
                std::size_t nres = 0;
                for (int bn : mesh.bone_nodes) if (bn >= 0) ++nres;
                std::printf("        skinned=%d palette=%u influences=%u bones=%zu resolved=%zu\n",
                            int(mesh.skinned()), mesh.palette_size, mesh.influences,
                            mesh.bones.size(), nres);
            }
            // Two-stream (the bosses): POSITION/NORMAL live in stream 1, and COLOR0/
            // TEXCOORD0 in stream 0 at stride-12 / stride-8. Reading stream 0 as
            // position gives the blend weights — the sub-unit blob. This draws the
            // BIND POSE only; the skinning palette isn't applied yet.
            const bool ts = mesh.two_stream();
            const VertexBuffer& pvb = ts ? mesh.vbs[1] : vb;   // POSITION@0, NORMAL@12
            const VertexBuffer& avb = vb;                      // COLOR0/UV0 carrier
            const int ts_col = ts ? int(avb.stride) - 12 : -1;
            const int ts_uv = ts ? int(avb.stride) - 8 : -1;
            const std::uint32_t nvert = ts ? std::min(pvb.count, avb.count) : vb.count;

            // ---- skinning (D3DX indexed palette) -----------------------------
            // v' = SUM_i w_i * (v * offset[b_i] * world[b_i]), with the last weight
            // implied: w_last = 1 - sum(previous). A vertex's BLENDINDICES byte is a
            // PALETTE SLOT, not a bone id, and the palette is PER SUBMESH — so map
            // each vertex to its submesh first.
            const bool skin = mesh.skinned() && !model.nodes.empty();
            std::vector<int> vert_sub;
            std::vector<Mat4> bone_xf;  // parallel to mesh.bone_nodes: offset * world
            if (skin) {
                vert_sub.assign(nvert, -1);
                for (std::size_t s = 0; s < mesh.submeshes.size(); ++s) {
                    const Submesh& sm = mesh.submeshes[s];
                    for (std::uint32_t k = sm.first_vert;
                         k < sm.first_vert + sm.vert_count && k < nvert; ++k)
                        vert_sub[k] = int(s);
                }
                bone_xf.resize(mesh.bone_nodes.size(), Mat4::identity());
                for (std::size_t b = 0; b < mesh.bone_nodes.size(); ++b) {
                    const int ni = mesh.bone_nodes[b];
                    if (ni >= 0 && ni < int(model.nodes.size()))
                        bone_xf[b] = model.nodes[ni].offset * model.nodes[ni].world;
                }
            }
            const int bi_off = ts ? int(avb.stride) - 16 : -1;
            // Blend a point (or a direction, which ignores translation) through the
            // palette. Falls through to the bind pose if anything fails to resolve.
            auto blend = [&](std::uint32_t k, const Vec3& v, bool is_dir, Vec3& outv) -> bool {
                const int si = vert_sub.empty() ? -1 : vert_sub[k];
                if (si < 0) return false;
                const Submesh& sm = mesh.submeshes[std::size_t(si)];
                const std::uint32_t nb = mesh.influences;
                float w[4] = {0, 0, 0, 0};
                float sum = 0;
                for (std::uint32_t i = 0; i + 1 < nb; ++i) {
                    w[i] = avb.raw_f32(k, int(i) * 4);
                    sum += w[i];
                }
                w[nb - 1] = 1.0f - sum;
                const std::uint32_t idx = avb.raw_u32(k, bi_off);
                Vec3 acc{0, 0, 0};
                float wsum = 0;
                for (std::uint32_t i = 0; i < nb; ++i) {
                    if (w[i] == 0.0f) continue;
                    // The shader's `.zyxw` un-swizzles D3DCOLOR back to byte order,
                    // so slot i is simply byte i of the dword.
                    const std::uint32_t slot = (idx >> (8 * i)) & 0xFF;
                    if (slot >= sm.bone_ids.size()) continue;
                    const std::uint32_t bid = sm.bone_ids[slot];
                    if (bid == 0xFFFFFFFFu || bid >= bone_xf.size()) continue;
                    const Mat4& m = bone_xf[bid];
                    const Vec3 t = is_dir ? xform_dir(m, v) : m.transform_point(v);
                    acc.x += t.x * w[i]; acc.y += t.y * w[i]; acc.z += t.z * w[i];
                    wsum += w[i];
                }
                if (wsum <= 1e-6f) return false;
                outv = acc;
                return true;
            };

            const std::uint32_t base = std::uint32_t(out.vertices.size());
            for (std::uint32_t k = 0; k < nvert; ++k) {
                Vec3 lp = pvb.position(k);
                Vec3 ln = ts ? pvb.raw_vec3(k, 12) : pvb.normal(k);
                if (skin) {
                    Vec3 sp, sn;
                    if (blend(k, lp, false, sp)) lp = sp;
                    if (blend(k, ln, true, sn)) ln = sn;
                }
                // A skinned vertex is already in MODEL space (the palette applied
                // offset*world for us), so only the SOB placement remains — applying
                // the node's world again would transform it twice.
                const Mat4& vxf = skin ? place : xf;
                const Vec3 p = vxf.transform_point(lp);
                Vec3 n = xform_dir(vxf, ln);
                const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                if (len > 1e-6f) { n.x /= len; n.y /= len; n.z /= len; }
                float u = 0, v = 0;
                if (ts) avb.raw_uv(k, ts_uv, u, v); else vb.uv0(k, u, v);
                const std::uint32_t d = ts ? avb.raw_u32(k, ts_col) : vb.diffuse(k);
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
            auto emit = [&](std::uint32_t first_idx, std::uint32_t idx_count, int tex,
                            int surf, const Material* mat, std::uint32_t fv,
                            std::uint32_t vc) {
                if (idx_count == 0) return;
                const std::uint32_t out_first = std::uint32_t(out.indices.size());
                for (std::uint32_t j = 0; j < idx_count; ++j) {
                    const std::size_t src = std::size_t(first_idx) + j;
                    if (src < ib.idx.size())
                        out.indices.push_back(base + ib.idx[src]);
                }
                // One surface id per triangle actually emitted — the loop above can
                // drop indices whose source is out of range, so count what landed.
                out.tri_surface.resize(out.indices.size() / 3, surf);
                DrawBatch b{out_first, idx_count, tex};
                if (mat) {
                    b.scroll_u = mat->scroll_u; b.scroll_v = mat->scroll_v;
                    auto q = [](float f) {
                        f = f < 0 ? 0 : (f > 1 ? 1 : f);
                        return std::uint32_t(f * 255.0f + 0.5f);
                    };
                    b.diffuse = q(mat->diffuse[0]) | (q(mat->diffuse[1]) << 8) |
                                (q(mat->diffuse[2]) << 16) | (q(mat->diffuse[3]) << 24);
                    b.mat_flags = mat->flags; b.blend = mat->blend;
                    b.alpha_ref = mat->alpha_ref; b.mat_alpha = mat->diffuse[3];
                    b.material = int(mat - model.materials.data());
                }
                b.first_vert = base + fv;
                b.vert_count = vc ? vc : nvert;
                out.batches.push_back(b);
            };

            if (!mesh.submeshes.empty()) {
                for (const Submesh& sm : mesh.submeshes) {
                    int tex = -1, surf = -1;
                    if (sm.material < model.materials.size()) {
                        tex = intern_tex(resolve_tex(ymo_file.parent_path(),
                                                     model.materials[sm.material].texture[0]).string());
                        surf = intern_surf(model.materials[sm.material].name);
                    }
                    const Material* mp = sm.material < model.materials.size()
                                             ? &model.materials[sm.material] : nullptr;
                    emit(sm.first_tri * 3, sm.tri_count * 3, tex, surf, mp,
                         sm.first_vert, sm.vert_count);
                }
            } else {
                int tex = -1, surf = -1;
                if (!model.materials.empty()) {
                    tex = intern_tex(resolve_tex(ymo_file.parent_path(),
                                                 model.materials[0].texture[0]).string());
                    surf = intern_surf(model.materials[0].name);
                }
                emit(0, std::uint32_t(ib.idx.size()), tex, surf,
                     model.materials.empty() ? nullptr : &model.materials[0], 0, nvert);
            }
        }
        // Fill in this trigger's world bounds from the geometry just appended.
        if (is_trigger && trig_index < out.triggers.size() &&
            out.vertices.size() > trig_v0) {
            SceneTrigger& t = out.triggers[trig_index];
            Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
            for (std::size_t vi = trig_v0; vi < out.vertices.size(); ++vi) {
                const RoomVertex& v = out.vertices[vi];
                lo.x = std::min(lo.x, v.px); lo.y = std::min(lo.y, v.py); lo.z = std::min(lo.z, v.pz);
                hi.x = std::max(hi.x, v.px); hi.y = std::max(hi.y, v.py); hi.z = std::max(hi.z, v.pz);
            }
            t.aabb_min = lo; t.aabb_max = hi;
        }
        if (out.vertices.size() > trig_v0) {
            RoomMesh::ObjRange r;
            r.name = lname;
            // An unnamed object after the room's own record gets a key of its own ("#<SOB index>"; no script
            // can name it), so the mesh runtime can play its section 0 (view_game.inl load_meshes: S_4015's
            // two s40step_02 platforms shared the key "" before).
            if (lname.empty() && &o != sob.objects.data())
                r.name = "#" + std::to_string(&o - sob.objects.data());
            r.v0 = std::uint32_t(trig_v0); r.v1 = std::uint32_t(out.vertices.size());
            r.t0 = std::uint32_t(obj_i0 / 3); r.t1 = std::uint32_t(out.indices.size() / 3);
            r.obj = po;
            for (const AnimClip& c : model.clips)
                r.clips.push_back({c.name, std::int32_t(c.frame_start), std::int32_t(c.frame_end), c.next, c.rate});
            if (states) {
                auto it = states->clip.find(lname);
                if (it != states->clip.end()) r.posed_clip = it->second;
            }
            if (r.posed_clip.empty() && box_pose_open) r.posed_clip = "OPEN";
            out.object_ranges.push_back(std::move(r));
        }
    }
    return out;
}

bool translate_object(RoomMesh& m, const std::string& lname, const Vec3& d) {
    const RoomMesh::ObjRange* r = m.object_range(lname);
    if (!r) return false;
    for (std::uint32_t i = r->v0; i < r->v1 && i < m.vertices.size(); ++i) {
        m.vertices[i].px += d.x; m.vertices[i].py += d.y; m.vertices[i].pz += d.z;
    }
    return true;
}

bool object_frame(const fs::path& asset_root, const SobObject& o, const char* clip, float frame,
                  Vec3* t) {
    Mat4 f;
    if (!object_frame_mat(asset_root, o, clip, frame, &f, nullptr)) return false;
    *t = {f.m[12], f.m[13], f.m[14]};
    return true;
}

bool object_frame_mat(const fs::path& asset_root, const SobObject& o, const char* clip, float frame,
                      Mat4* out, bool* sole) {
    // FUN_00573aa0: *(state+4 + mesh[0]+0x2c * 0x170 + 0x34), i.e. row 3 y of that node's
    // instance matrix (state +0x00; B x world, with B identity in ~98.5% of nodes).
    const fs::path file = resolve_ymo(asset_root, o.ymo_path);
    Model local;
    Model* mp = cached_big_model(file);
    try {
        if (!mp) {
            local = Model::parse(read_file(file));
            local.resolve_hierarchy();
            mp = &local;
        } else {
            mp->rest_pose();
        }
    } catch (const std::exception&) { return false; }
    if (clip)
        for (const AnimClip& c : mp->clips)
            if (c.name == clip) { mp->pose(c, frame); break; }
    const Mat4 place = o.has_world ? o.world_mat : o.world();
    for (const Node& n : mp->nodes)
        if (n.mesh_index == 0) {
            *out = n.offset * n.world * place;
            if (sole) *sole = mp->meshes.size() == 1;
            return true;
        }
    return false;
}

bool rebake_object(RoomMesh& m, const fs::path& asset_root, const std::string& lname,
                   const char* clip, float frame, const Vec3* pos, bool collision, const Vec3* rot,
                   const Mat4* world, bool twin_clips) {
    const RoomMesh::ObjRange* r = m.object_range(lname);
    if (!r) return false;
    Sob one;
    one.objects.push_back(r->obj);
    if (pos) one.objects.back().position = *pos;
    if (rot) one.objects.back().rotation = *rot;
    if (world) { one.objects.back().has_world = true; one.objects.back().world_mat = *world; }
    one.objects.back().twin_clips = twin_clips;
    const RoomMesh tmp = build_room_objects(asset_root, one, fs::path(), nullptr, nullptr,
                                            collision, clip, frame);
    if (tmp.vertices.size() != std::size_t(r->v1 - r->v0)) return false;
    for (std::size_t k = 0; k < tmp.vertices.size(); ++k) m.vertices[r->v0 + k] = tmp.vertices[k];
    return true;
}

}  // namespace cleria
