// cleria-view: real-time WebGPU renderer + free-fly walkthrough of Ys Origin.
//
//   cleria-view <asset_root> <room.SOB>              free-fly window
//   cleria-view <asset_root> <room.SOB> --shot out.ppm [W H yaw pitch dist]
//
// In the window: WASD/QE fly, hold LMB to look, wheel = speed, R = recenter,
// ESC = quit. Fly into a doorway (a SEN trigger volume) to warp to the connected
// room; number keys 1-9 take exit N directly.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <algorithm>

#include <SDL3/SDL.h>
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include "cleria/aia.hpp"
#include "cleria/dds.hpp"
#include "cleria/mathx.hpp"
#include "cleria/physics.hpp"
#include "cleria/items.hpp"
#include "cleria/scene.hpp"
#include "cleria/scm.hpp"
#include "cleria/sef.hpp"
#include "cleria/sct.hpp"
#include "cleria/sen.hpp"
#include "cleria/sfo.hpp"
#include "cleria/xso.hpp"

using namespace cleria;
namespace fs = std::filesystem;

static WGPUStringView sv(const char* s) { return {s, s ? std::strlen(s) : 0}; }

static std::vector<std::uint8_t> read_bytes(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static fs::path exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(buf).parent_path();
#endif
    return fs::current_path();
}

// Playable characters, indexed by g_flags[150] (1=Yunica, 2=Hugo, 3=Toal).
static const char* kCharNames[] = {"Default", "Yunica", "Hugo", "Toal"};

// Player state (placeholder stats until the stat/progression system lands).
struct Player {
    int level = 1;
    int hp = 400, max_hp = 400;
    int sp = 0;
    float exp_pct = 0.0f;
    float mp_pct = 100.0f;
    int character = 1;  // portrait: 1 Yunica / 2 Hugo / 3 Toal
};

// Locate a bundled resource next to the exe, or in the repo res/ dir.
static fs::path res_file(const std::string& name) {
    std::error_code ec;
    for (const fs::path& c : {exe_dir() / name, exe_dir() / ".." / ".." / "res" / name,
                              fs::path("C:/Users/kores/repos/YsOrigin-CleriaCore/res") / name})
        if (fs::exists(c, ec)) return c;
    return exe_dir() / name;
}

static const char* kWGSL = R"WGSL(
struct Uniforms {
  mvp      : mat4x4<f32>,
  light    : vec4<f32>,
  fogColor : vec4<f32>,   // rgb = fog colour
  fogParams: vec4<f32>,   // x = near, y = far, z = enabled
};
@group(0) @binding(0) var<uniform> u : Uniforms;
@group(1) @binding(0) var tex : texture_2d<f32>;
@group(1) @binding(1) var smp : sampler;

struct VsOut {
  @builtin(position) clip : vec4<f32>,
  @location(0) uv : vec2<f32>,
  @location(1) color : vec4<f32>,
  @location(2) depth : f32,     // view-space depth for fog
};

@vertex
fn vs(@location(0) pos : vec3<f32>, @location(1) nrm : vec3<f32>,
      @location(2) uv : vec2<f32>, @location(3) color : vec4<f32>) -> VsOut {
  var o : VsOut;
  o.clip = u.mvp * vec4<f32>(pos, 1.0);
  o.uv = uv;
  o.color = color;              // baked per-vertex lighting (D3D diffuse)
  o.depth = o.clip.w;           // LH perspective: w == view-space z
  return o;
}

@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
  let tc = textureSample(tex, smp, in.uv);
  if (tc.a < 0.5) { discard; }
  // Fixed-function MODULATE: texture * baked vertex colour.
  var c = tc.rgb * in.color.rgb;
  // D3D9 linear fog, driven by the room's real SFO region data:
  //   f = (far - d) / (far - near), clamped;  colour = lerp(fog, lit, f)
  if (u.fogParams.z > 0.5) {
    let f = clamp((u.fogParams.y - in.depth) / max(u.fogParams.y - u.fogParams.x, 0.001),
                  0.0, 1.0);
    c = mix(u.fogColor.rgb, c, f);
  }
  return vec4<f32>(c, 1.0);
}
)WGSL";

// Effects (SEF/EF2): additive, camera-facing billboards. No fog and no alpha test —
// the flame's soft falloff lives in its RGB (its alpha is 1-bit and merely tracks the
// black background), so additive blending drops the background for free.
static const char* kFxWGSL = R"WGSL(
struct Uniforms {
  mvp : mat4x4<f32>, light : vec4<f32>, fogColor : vec4<f32>, fogParams : vec4<f32>,
};
@group(0) @binding(0) var<uniform> u : Uniforms;
@group(1) @binding(0) var tex : texture_2d<f32>;
@group(1) @binding(1) var smp : sampler;

struct VsOut {
  @builtin(position) clip : vec4<f32>,
  @location(0) uv : vec2<f32>,
  @location(1) color : vec4<f32>,
};

@vertex
fn vs(@location(0) pos : vec3<f32>, @location(1) nrm : vec3<f32>,
      @location(2) uv : vec2<f32>, @location(3) color : vec4<f32>) -> VsOut {
  var o : VsOut;
  o.clip = u.mvp * vec4<f32>(pos, 1.0);
  o.uv = uv;
  o.color = color;
  return o;
}

@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
  let tc = textureSample(tex, smp, in.uv);
  return vec4<f32>(tc.rgb * in.color.rgb, 1.0);
}
)WGSL";

// ---- async acquisition ------------------------------------------------------
struct AdapterReq { WGPUAdapter adapter = nullptr; bool done = false; };
static void on_adapter(WGPURequestAdapterStatus s, WGPUAdapter a, WGPUStringView msg,
                       void* u1, void*) {
    auto* r = static_cast<AdapterReq*>(u1);
    if (s == WGPURequestAdapterStatus_Success) r->adapter = a;
    else std::fprintf(stderr, "adapter: %.*s\n", int(msg.length), msg.data);
    r->done = true;
}
struct DeviceReq { WGPUDevice device = nullptr; bool done = false; };
static void on_device(WGPURequestDeviceStatus s, WGPUDevice d, WGPUStringView msg,
                      void* u1, void*) {
    auto* r = static_cast<DeviceReq*>(u1);
    if (s == WGPURequestDeviceStatus_Success) r->device = d;
    else std::fprintf(stderr, "device: %.*s\n", int(msg.length), msg.data);
    r->done = true;
}

// Persistent GPU objects (survive room changes).
struct Gpu {
    WGPUInstance instance{};
    WGPUAdapter adapter{};
    WGPUDevice device{};
    WGPUQueue queue{};
    WGPURenderPipeline pipeline{};
    WGPURenderPipeline fx_pipeline{};   // additive effect billboards (SEF/EF2)
    WGPUBuffer ubuf{};
    WGPUBindGroup ubind{};
    WGPUSampler sampler{};
    WGPUBindGroupLayout tex_layout{};
    WGPUBindGroup white_bind{};
    std::unordered_map<std::string, fs::path> dds_index;  // basename(lower) -> path
    std::unordered_map<std::string, fs::path> sob_index;  // stem(upper) -> path
};

// Per-room GPU objects (rebuilt on every transition).
struct Room {
    RoomMesh mesh;
    Sen sen;
    Sfo fog;                 // per-region scene fog (SFO sidecar)
    Scm cam;                 // per-region cameras (SCM sidecar)
    Sef fx;                  // scene effects: torch flames etc (SEF sidecar)
    Sct cast;                // actor placement: enemies + player spawn (SCT sidecar)
    fs::path dir;            // the room folder (its scripts live beside the SOB)
    std::string name;
    WGPUBuffer vbuf{}, ibuf{};
    std::vector<WGPUTexture> textures;
    std::vector<WGPUTextureView> views;
    std::vector<WGPUBindGroup> binds;  // parallel to mesh.textures
};

static WGPUBuffer make_buffer(WGPUDevice dev, WGPUQueue q, const void* data,
                              std::size_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor bd = {};
    bd.usage = usage | WGPUBufferUsage_CopyDst; bd.size = size;
    WGPUBuffer b = wgpuDeviceCreateBuffer(dev, &bd);
    if (size) wgpuQueueWriteBuffer(q, b, 0, data, size);
    return b;
}

static WGPUTextureView upload_texture(Gpu& g, Room& room, const Dds& dds) {
    WGPUTextureFormat fmt; std::uint32_t block = 1, blk_bytes = 4;
    switch (dds.format) {
        case Dds::BC1: fmt = WGPUTextureFormat_BC1RGBAUnorm; block = 4; blk_bytes = 8; break;
        case Dds::BC2: fmt = WGPUTextureFormat_BC2RGBAUnorm; block = 4; blk_bytes = 16; break;
        default:       fmt = WGPUTextureFormat_RGBA8Unorm;   block = 1; blk_bytes = 4; break;
    }
    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D;
    td.size = {dds.width, dds.height, 1};
    td.format = fmt; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);

    WGPUTexelCopyTextureInfo dst = {}; dst.texture = t; dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = ((dds.width + block - 1) / block) * blk_bytes;
    layout.rowsPerImage = (dds.height + block - 1) / block;
    WGPUExtent3D ext = {dds.width, dds.height, 1};
    wgpuQueueWriteTexture(g.queue, &dst, dds.data.data(), dds.data.size(), &layout, &ext);

    WGPUTextureView view = wgpuTextureCreateView(t, nullptr);
    room.textures.push_back(t);
    room.views.push_back(view);
    return view;
}

static WGPUBindGroup tex_bindgroup(Gpu& g, WGPUTextureView view) {
    WGPUBindGroupEntry e[2] = {};
    e[0].binding = 0; e[0].textureView = view;
    e[1].binding = 1; e[1].sampler = g.sampler;
    WGPUBindGroupDescriptor d = {};
    d.layout = g.tex_layout; d.entryCount = 2; d.entries = e;
    return wgpuDeviceCreateBindGroup(g.device, &d);
}

// `shared_white` is owned by Gpu, not the room — missing-texture batches store
// it in binds, so it must not be released here.
static void free_room(Room& r, WGPUBindGroup shared_white) {
    if (r.vbuf) wgpuBufferRelease(r.vbuf);
    if (r.ibuf) wgpuBufferRelease(r.ibuf);
    for (WGPUBindGroup b : r.binds) if (b && b != shared_white) wgpuBindGroupRelease(b);
    for (WGPUTextureView v : r.views) wgpuTextureViewRelease(v);
    for (WGPUTexture t : r.textures) wgpuTextureRelease(t);
    r = Room{};
}

// Load a room's geometry, textures and SEN into fresh GPU resources.
static Room load_room(Gpu& g, const fs::path& asset_root, const fs::path& sob,
                      const std::int32_t* base_flags = nullptr, int character = 1) {
    Room r;
    r.name = sob.stem().string();
    r.dir = sob.parent_path();

    // Character-gate chests BEFORE building geometry: run the room's scene-main
    // script (the same object-enable logic the game runs on entry) on a throwaway
    // VM seeded with current g_flags + character, and whitelist the chests it
    // enables. Hidden chests are then skipped entirely (model + interactable).
    std::unordered_set<std::string> box_whitelist;
    bool gates_box = false;
    {
        fs::path scene_main = sob; scene_main.replace_extension(".XSO");
        if (fs::exists(scene_main)) {
            try {
                Xso sm = Xso::parse(read_bytes(scene_main));
                XsoVm tmp;
                if (base_flags) std::memcpy(tmp.g_flags, base_flags, sizeof(tmp.g_flags));
                tmp.g_flags[150] = character;
                tmp.run(sm);
                auto low = [](std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; };
                for (const std::string& s : tmp.obj_enable) {
                    std::string l = low(s); box_whitelist.insert(l);
                    if (l.rfind("box", 0) == 0) gates_box = true;
                }
                for (const std::string& s : tmp.obj_opened)
                    if (low(s).rfind("box", 0) == 0) gates_box = true;
            } catch (const std::exception&) {}
        }
    }

    // Object-variant scripts: some rooms stack several versions of one object at a
    // single spot and let a script choose (S_1000 places save_01/02/03 together).
    // Ops 0x16B/0x16C name the winner; run the script and hide the losers.
    std::unordered_set<std::string> hidden;
    {
        const fs::path vis = sob.parent_path() / "S_SAVEOBJECTCHANGE.XSO";
        if (fs::exists(vis)) {
            try {
                XsoVm tmp;
                if (base_flags) std::memcpy(tmp.g_flags, base_flags, sizeof(tmp.g_flags));
                tmp.g_flags[150] = character;
                tmp.run(Xso::parse(read_bytes(vis)));
                for (const auto& [name, shown] : tmp.obj_visible) {
                    if (shown) continue;
                    std::string l = name;
                    for (char& c : l) c = char(std::tolower((unsigned char)c));
                    hidden.insert(l);
                }
            } catch (const std::exception&) {}
        }
    }
    r.mesh = build_room(asset_root, sob, gates_box ? &box_whitelist : nullptr,
                        hidden.empty() ? nullptr : &hidden);
    r.vbuf = make_buffer(g.device, g.queue, r.mesh.vertices.data(),
                         r.mesh.vertices.size() * sizeof(RoomVertex), WGPUBufferUsage_Vertex);
    r.ibuf = make_buffer(g.device, g.queue, r.mesh.indices.data(),
                         r.mesh.indices.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);

    int loaded = 0;
    r.binds.reserve(r.mesh.textures.size());
    for (const std::string& name : r.mesh.textures) {
        std::string base = name;
        const auto slash = base.find_last_of("/\\");
        if (slash != std::string::npos) base = base.substr(slash + 1);
        auto it = g.dds_index.find(base);
        Dds dds = (it != g.dds_index.end()) ? load_dds(it->second) : Dds{};
        if (dds.ok()) { r.binds.push_back(tex_bindgroup(g, upload_texture(g, r, dds))); ++loaded; }
        else r.binds.push_back(g.white_bind);
    }

    // SEN sidecar sits beside the SOB (same stem).
    fs::path sen = sob; sen.replace_extension(".SEN");
    if (fs::exists(sen)) {
        std::ifstream f(sen, std::ios::binary);
        std::vector<std::uint8_t> b{std::istreambuf_iterator<char>(f),
                                    std::istreambuf_iterator<char>()};
        r.sen = Sen::parse(b);
    }
    // SFO sidecar: the room's fog regions.
    fs::path sfo = sob; sfo.replace_extension(".SFO");
    if (fs::exists(sfo)) r.fog = Sfo::parse(read_bytes(sfo));
    // SCM sidecar: the room's camera regions (the game's authored cameras).
    fs::path scm = sob; scm.replace_extension(".SCM");
    if (fs::exists(scm)) r.cam = Scm::parse(read_bytes(scm));
    // SEF sidecar: scene effects (torch flames, ...).
    fs::path sef = sob; sef.replace_extension(".SEF");
    if (fs::exists(sef)) r.fx = Sef::parse(read_bytes(sef));
    // SCT sidecar: enemy placement + the player's spawn point.
    fs::path sct = sob; sct.replace_extension(".SCT");
    if (fs::exists(sct)) r.cast = Sct::parse(read_bytes(sct));

    std::printf("[%s] %d obj, %zu tris, %d/%zu tex, %zu exits, %zu chests, %zu fog\n",
                r.name.c_str(), r.mesh.objects, r.mesh.indices.size() / 3, loaded,
                r.mesh.textures.size(), r.sen.triggers.size(),
                r.mesh.interactables.size(), r.fog.regions.size());
    {
        int nf = 0;
        for (const SefEffect& e : r.fx.effects) if (e.id == kEffectTorchFlame) ++nf;
        if (!r.fx.effects.empty())
            std::printf("    %zu effects (%d torch flames), %zu cameras\n",
                        r.fx.effects.size(), nf, r.cam.regions.size());
    }
    if (!r.fog.regions.empty()) {
        const FogRegion& f = r.fog.regions[0];
        std::printf("    fog[0] rgb(%.0f,%.0f,%.0f) near=%.0f far=%.0f\n",
                    f.r * 255, f.g * 255, f.b * 255, f.fog_near, f.fog_far);
    }
    for (const SenTrigger& t : r.sen.triggers)
        std::printf("    exit -> %s\n", t.dest_scene.c_str());
    for (const Interactable& it : r.mesh.interactables)
        std::printf("    chest %s -> %s\n", it.name.c_str(),
                    it.script.empty() ? "(no script)"
                                      : fs::path(it.script).filename().string().c_str());
    return r;
}

// Load a standalone YMO (a boss, a prop) into the same GPU objects a room uses.
static Room load_model(Gpu& g, const fs::path& asset_root, const fs::path& ymo) {
    Room r;
    r.name = ymo.stem().string();
    r.dir = ymo.parent_path();
    r.mesh = build_model(asset_root, ymo);
    r.vbuf = make_buffer(g.device, g.queue, r.mesh.vertices.data(),
                         r.mesh.vertices.size() * sizeof(RoomVertex), WGPUBufferUsage_Vertex);
    r.ibuf = make_buffer(g.device, g.queue, r.mesh.indices.data(),
                         r.mesh.indices.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);
    int loaded = 0;
    for (const std::string& name : r.mesh.textures) {
        std::string base = name;
        const auto slash = base.find_last_of("/\\");
        if (slash != std::string::npos) base = base.substr(slash + 1);
        auto it = g.dds_index.find(base);
        Dds dds = (it != g.dds_index.end()) ? load_dds(it->second) : Dds{};
        if (dds.ok()) { r.binds.push_back(tex_bindgroup(g, upload_texture(g, r, dds))); ++loaded; }
        else r.binds.push_back(g.white_bind);
    }
    std::printf("[%s] %zu tris, %d/%zu tex, bounds(%.1f,%.1f,%.1f)..(%.1f,%.1f,%.1f)\n",
                r.name.c_str(), r.mesh.indices.size() / 3, loaded, r.mesh.textures.size(),
                r.mesh.bmin.x, r.mesh.bmin.y, r.mesh.bmin.z,
                r.mesh.bmax.x, r.mesh.bmax.y, r.mesh.bmax.z);
    return r;
}

static void build_pipeline(Gpu& g, WGPUTextureFormat color) {
    WGPUShaderSourceWGSL wgsl = {};
    wgsl.chain.sType = WGPUSType_ShaderSourceWGSL; wgsl.code = sv(kWGSL);
    WGPUShaderModuleDescriptor smd = {}; smd.nextInChain = &wgsl.chain;
    WGPUShaderModule sm = wgpuDeviceCreateShaderModule(g.device, &smd);

    WGPUVertexAttribute attrs[4] = {};
    attrs[0].format = WGPUVertexFormat_Float32x3; attrs[0].offset = 0;  attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x3; attrs[1].offset = 12; attrs[1].shaderLocation = 1;
    attrs[2].format = WGPUVertexFormat_Float32x2; attrs[2].offset = 24; attrs[2].shaderLocation = 2;
    attrs[3].format = WGPUVertexFormat_Unorm8x4;  attrs[3].offset = 32; attrs[3].shaderLocation = 3;
    WGPUVertexBufferLayout vbl = {};
    vbl.stepMode = WGPUVertexStepMode_Vertex; vbl.arrayStride = sizeof(RoomVertex);
    vbl.attributeCount = 4; vbl.attributes = attrs;

    // EXPLICIT bind-group layouts, shared by the world and effect pipelines. Auto
    // ("default") layouts are exclusive to the pipeline that generated them, so bind
    // groups made from one can never be used with another.
    WGPUBindGroupLayoutEntry ue = {};
    ue.binding = 0; ue.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    ue.buffer.type = WGPUBufferBindingType_Uniform; ue.buffer.minBindingSize = 112;
    WGPUBindGroupLayoutDescriptor ubl = {}; ubl.entryCount = 1; ubl.entries = &ue;
    WGPUBindGroupLayout bgl_u = wgpuDeviceCreateBindGroupLayout(g.device, &ubl);

    WGPUBindGroupLayoutEntry te[2] = {};
    te[0].binding = 0; te[0].visibility = WGPUShaderStage_Fragment;
    te[0].texture.sampleType = WGPUTextureSampleType_Float;
    te[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    te[1].binding = 1; te[1].visibility = WGPUShaderStage_Fragment;
    te[1].sampler.type = WGPUSamplerBindingType_Filtering;
    WGPUBindGroupLayoutDescriptor tbl = {}; tbl.entryCount = 2; tbl.entries = te;
    WGPUBindGroupLayout bgl_t = wgpuDeviceCreateBindGroupLayout(g.device, &tbl);

    WGPUBindGroupLayout bgls[2] = {bgl_u, bgl_t};
    WGPUPipelineLayoutDescriptor pld = {};
    pld.bindGroupLayoutCount = 2; pld.bindGroupLayouts = bgls;
    WGPUPipelineLayout play = wgpuDeviceCreatePipelineLayout(g.device, &pld);

    WGPUBlendState blend = {};
    blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_Zero};
    blend.alpha = blend.color;
    WGPUColorTargetState target = {};
    target.format = color; target.blend = &blend; target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = {};
    fs.module = sm; fs.entryPoint = sv("fs"); fs.targetCount = 1; fs.targets = &target;

    WGPUDepthStencilState ds = {};
    ds.format = WGPUTextureFormat_Depth24Plus;
    ds.depthWriteEnabled = WGPUOptionalBool_True; ds.depthCompare = WGPUCompareFunction_Less;
    ds.stencilFront.compare = WGPUCompareFunction_Always;
    ds.stencilBack.compare = WGPUCompareFunction_Always;

    WGPURenderPipelineDescriptor pd = {};
    pd.layout = play;
    pd.vertex.module = sm; pd.vertex.entryPoint = sv("vs");
    pd.vertex.bufferCount = 1; pd.vertex.buffers = &vbl;
    pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pd.primitive.frontFace = WGPUFrontFace_CW; pd.primitive.cullMode = WGPUCullMode_None;
    pd.depthStencil = &ds; pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFF;
    pd.fragment = &fs;
    g.pipeline = wgpuDeviceCreateRenderPipeline(g.device, &pd);

    WGPUBufferDescriptor ud = {};
    ud.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst; ud.size = 112;
    g.ubuf = wgpuDeviceCreateBuffer(g.device, &ud);
    WGPUBindGroupEntry ube = {}; ube.binding = 0; ube.buffer = g.ubuf; ube.size = 112;
    WGPUBindGroupDescriptor ubd = {};
    ubd.layout = bgl_u; ubd.entryCount = 1; ubd.entries = &ube;
    g.ubind = wgpuDeviceCreateBindGroup(g.device, &ubd);
    g.tex_layout = bgl_t;

    // Effect pipeline: same vertex layout and bind groups, but additive and
    // depth-read-only, so flames add light without occluding or z-fighting.
    {
        WGPUShaderSourceWGSL fw = {};
        fw.chain.sType = WGPUSType_ShaderSourceWGSL; fw.code = sv(kFxWGSL);
        WGPUShaderModuleDescriptor fd = {}; fd.nextInChain = &fw.chain;
        WGPUShaderModule fm = wgpuDeviceCreateShaderModule(g.device, &fd);
        WGPUBlendState ab = {};
        ab.color = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
        ab.alpha = ab.color;
        WGPUColorTargetState at = {};
        at.format = color; at.blend = &ab; at.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState afs = {};
        afs.module = fm; afs.entryPoint = sv("fs"); afs.targetCount = 1; afs.targets = &at;
        WGPUDepthStencilState ads = ds;
        ads.depthWriteEnabled = WGPUOptionalBool_False;   // read depth, don't write it
        WGPURenderPipelineDescriptor apd = {};
        apd.layout = play;   // the same explicit layout as the world pipeline
        apd.vertex.module = fm; apd.vertex.entryPoint = sv("vs");
        apd.vertex.bufferCount = 1; apd.vertex.buffers = &vbl;
        apd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        apd.primitive.frontFace = WGPUFrontFace_CW; apd.primitive.cullMode = WGPUCullMode_None;
        apd.depthStencil = &ads; apd.multisample.count = 1; apd.multisample.mask = 0xFFFFFFFF;
        apd.fragment = &afs;
        g.fx_pipeline = wgpuDeviceCreateRenderPipeline(g.device, &apd);
        wgpuShaderModuleRelease(fm);
    }

    WGPUSamplerDescriptor sad = {};
    sad.addressModeU = sad.addressModeV = sad.addressModeW = WGPUAddressMode_Repeat;
    sad.magFilter = sad.minFilter = WGPUFilterMode_Linear; sad.maxAnisotropy = 1;
    g.sampler = wgpuDeviceCreateSampler(g.device, &sad);

    // Shared 1x1 white for untextured / missing.
    Room tmp;
    Dds white; white.width = white.height = 1; white.format = Dds::RGBA8;
    white.data = {255, 255, 255, 255};
    g.white_bind = tex_bindgroup(g, upload_texture(g, tmp, white));
    // deliberately leak the 1x1 (lives for the whole run)
    tmp.textures.clear(); tmp.views.clear();

    wgpuShaderModuleRelease(sm);
}

// The room's current fog, from its SFO region data (see src/cleria/sfo.cpp). The
// game cross-fades between regions rather than popping; `g_fog` is the live,
// interpolated state that gets uploaded.
struct FogState {
    float r = 0, g = 0, b = 0;
    float near_ = 1, far_ = 1e9f;
    bool on = false;
};
static FogState g_fog;

static void write_mvp(Gpu& g, const Mat4& mvp) {
    float u[28];
    std::memcpy(u, mvp.m.data(), 64);
    const Vec3 L = normalize({0.4f, 1.0f, 0.35f});
    u[16] = L.x; u[17] = L.y; u[18] = L.z; u[19] = 1.0f;
    u[20] = g_fog.r; u[21] = g_fog.g; u[22] = g_fog.b; u[23] = 1.0f;
    u[24] = g_fog.near_; u[25] = g_fog.far_; u[26] = g_fog.on ? 1.0f : 0.0f; u[27] = 0.0f;
    wgpuQueueWriteBuffer(g.queue, g.ubuf, 0, u, sizeof(u));
}

static void draw(Gpu& g, const Room& room, WGPUTextureView color, WGPUTextureView depth) {
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Clear; ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {0.055, 0.067, 0.086, 1.0};
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = depth; da.depthLoadOp = WGPULoadOp_Clear; da.depthStoreOp = WGPUStoreOp_Store;
    da.depthClearValue = 1.0f;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, g.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, room.vbuf, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, room.ibuf, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    for (const DrawBatch& b : room.mesh.batches) {
        WGPUBindGroup tb = (b.texture >= 0 && b.texture < int(room.binds.size()))
                               ? room.binds[b.texture] : g.white_bind;
        wgpuRenderPassEncoderSetBindGroup(pass, 1, tb, 0, nullptr);
        wgpuRenderPassEncoderDrawIndexed(pass, b.index_count, 1, b.first_index, 0, 0);
    }
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(enc);
}

static WGPUTextureView make_depth(WGPUDevice dev, std::uint32_t w, std::uint32_t h) {
    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment; td.dimension = WGPUTextureDimension_2D;
    td.size = {w, h, 1}; td.format = WGPUTextureFormat_Depth24Plus;
    td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture t = wgpuDeviceCreateTexture(dev, &td);
    WGPUTextureView v = wgpuTextureCreateView(t, nullptr);
    wgpuTextureRelease(t);
    return v;
}

// Vertical field of view (radians). The game's 2.5D view is MUCH flatter than a
// typical 60-degree game camera — nearly orthographic. kCamDist is paired with this
// to hold the character's on-screen size: change one and you must change the other
// (dist is proportional to 1/tan(fov/2)).
// NOTE: hand-tuned against reference screenshots. The authored value probably lives in
// the SCM camera payload — prefer that once it's decoded.
static float g_fov = 0.4712f;  // 27 degrees

// ---- free-fly camera --------------------------------------------------------
struct Camera {
    Vec3 pos{}; float yaw = 0, pitch = 0;
    Vec3 forward() const {
        return {std::cos(pitch) * std::cos(yaw), std::sin(pitch),
                std::cos(pitch) * std::sin(yaw)};
    }
    Mat4 view_proj(float aspect, float near_, float far_) const {
        const Vec3 f = forward();
        const Mat4 v = look_at_lh(pos, {pos.x + f.x, pos.y + f.y, pos.z + f.z}, {0, 1, 0});
        return v * perspective_lh(g_fov, aspect, near_, far_);
    }
};

// Place the camera to overlook a freshly-loaded room (orbit-style vantage).
static void spawn_in(Camera& cam, const Room& r) {
    const Vec3 c = {(r.mesh.bmin.x + r.mesh.bmax.x) * 0.5f,
                    (r.mesh.bmin.y + r.mesh.bmax.y) * 0.5f,
                    (r.mesh.bmin.z + r.mesh.bmax.z) * 0.5f};
    const Vec3 d = r.mesh.bmax - r.mesh.bmin;
    const float diag = std::max(std::sqrt(dot(d, d)), 1.0f);
    const float y = -0.7f, p = 0.9f, rad = diag * 0.75f;
    cam.pos = {c.x + rad * std::cos(p) * std::cos(y), c.y + rad * std::sin(p),
               c.z + rad * std::cos(p) * std::sin(y)};
    const Vec3 fwd = normalize(c - cam.pos);
    cam.pitch = std::asin(std::max(-1.0f, std::min(1.0f, fwd.y)));
    cam.yaw = std::atan2(fwd.z, fwd.x);
}

static float room_diag(const Room& r) {
    const Vec3 d = r.mesh.bmax - r.mesh.bmin;
    return std::max(std::sqrt(dot(d, d)), 1.0f);
}

// The live camera, driven by the room's SCM regions. The engine picks the region
// the player is in (else record 0), makes point A the look-at target (mode 1 =
// follow player) and point B the eye (mode 3 = orbit at yaw/pitch/distance), and
// blends between regions at the region's own rate. See src/cleria/scm.cpp.
// Only the REGION PARAMETERS are blended (that is what the engine cross-fades when
// the player crosses a region boundary). The follow itself is instant: point A is
// the player position, and the eye is derived from it every frame. Lerping the eye
// position instead makes the camera lag behind the character — it does not do that.
struct CamState {
    float fov = 0.5236f, yaw = 0, pitch = 0, dist = 24;
    bool on = false;
};
static CamState g_cam;

// The game aims at ground + a per-actor offset (player+0x230 + player+0x324),
// which we don't model yet; a fixed chest-height offset stands in for it.
static constexpr float kLookHeight = 1.0f;

static void update_camera(const Room& room, const Vec3& feet, float dt, bool snap = false) {
    const CameraRegion* r = room.cam.pick(feet);
    if (!r) { g_cam.on = false; return; }
    if (!g_cam.on || snap) {
        g_cam = {r->fov, r->eye.yaw, r->eye.pitch, r->eye.dist, true};
        return;
    }
    // Ease the region's parameters only; the follow stays instant.
    const float t = std::min(1.0f, dt * 60.0f * r->blend);
    auto L = [&](float c, float n) { return c + (n - c) * t; };
    g_cam.fov = L(g_cam.fov, r->fov);
    g_cam.yaw = L(g_cam.yaw, r->eye.yaw);
    g_cam.pitch = L(g_cam.pitch, r->eye.pitch);
    g_cam.dist = L(g_cam.dist, r->eye.dist);
}

// Place `cam` from the blended parameters and the player's CURRENT position, using
// the engine's orbit math (FUN_0042b6e0): a = -PI/2 - yaw, then
// eye = target + (-cos(a)*d*cos(pitch), sin(pitch)*d, sin(a)*d*cos(pitch)).
static void apply_cam_state(Camera& cam, const Vec3& feet) {
    const Vec3 target = {feet.x, feet.y + kLookHeight, feet.z};   // A.mode 1: follow
    const float a = -1.5707963f - g_cam.yaw;
    const float cp = std::cos(g_cam.pitch);
    cam.pos = {target.x - std::cos(a) * g_cam.dist * cp,
               target.y + std::sin(g_cam.pitch) * g_cam.dist,
               target.z + std::sin(a) * g_cam.dist * cp};
    const Vec3 f = normalize(target - cam.pos);
    cam.pitch = std::asin(std::max(-1.0f, std::min(1.0f, f.y)));
    cam.yaw = std::atan2(f.z, f.x);
    g_fov = g_cam.fov;
}

// Pick the fog region the player is standing in and ease `g_fog` toward it. The
// game cross-fades region changes at 0.02/frame (~0.83s at 60fps) instead of
// popping; `snap` jumps straight there (room entry / headless shots).
static void update_fog(const Room& room, const Vec3& player, float dt, bool snap = false) {
    const FogRegion* f = room.fog.pick(player);
    if (!f) { g_fog.on = false; return; }
    const float t = snap ? 1.0f : std::min(1.0f, dt * 60.0f * 0.02f);
    auto ease = [&](float cur, float tgt) { return cur + (tgt - cur) * t; };
    if (!g_fog.on) {  // first sight of fog: start there rather than fading from black
        g_fog = {f->r, f->g, f->b, f->fog_near, f->fog_far, true};
        return;
    }
    g_fog.r = ease(g_fog.r, f->r);
    g_fog.g = ease(g_fog.g, f->g);
    g_fog.b = ease(g_fog.b, f->b);
    g_fog.near_ = ease(g_fog.near_, f->fog_near);
    g_fog.far_ = ease(g_fog.far_, f->fog_far);
}

// ---- character sprite (AIA billboard) --------------------------------------
struct SpriteTex {
    WGPUTexture tex{}; WGPUTextureView view{}; WGPUBindGroup bind{};
    std::uint32_t w = 0, h = 0;
};

static SpriteTex upload_rgba(Gpu& g, std::uint32_t w, std::uint32_t h, const std::uint8_t* rgba) {
    SpriteTex s; s.w = w; s.h = h;
    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    td.dimension = WGPUTextureDimension_2D; td.size = {w, h, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
    s.tex = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTexelCopyTextureInfo dst = {}; dst.texture = s.tex; dst.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferLayout lay = {}; lay.bytesPerRow = w * 4; lay.rowsPerImage = h;
    WGPUExtent3D ext = {w, h, 1};
    wgpuQueueWriteTexture(g.queue, &dst, rgba, std::size_t(w) * h * 4, &lay, &ext);
    s.view = wgpuTextureCreateView(s.tex, nullptr);
    s.bind = tex_bindgroup(g, s.view);
    return s;
}

// A playable character's sprite set: one AIA, with the 8 facings of the current
// animation frame decoded and uploaded. dir 0..7 = the 8 compass facings.
// Animation tags (verified for the C_10x player sprites):
//   image 0 (tag 0) = idle,  image 1 (tag 1) = walk.
struct CharSprite {
    Aia aia;
    std::uint32_t image = 0;              // current animation
    SpriteTex dirs[8];                    // per-facing texture (current frame)
    std::uint32_t ndir = 0, nframe = 0;

    bool load(Gpu& g, const fs::path& aia_path, std::uint32_t img = 0) {
        if (!aia.load(aia_path)) return false;
        if (img >= aia.nImg) return false;
        set_anim(g, img, 0);
        return true;
    }
    // Frame count of an animation (0 if it doesn't exist).
    std::uint32_t frames_of(std::uint32_t img) const {
        return img < aia.nImg ? aia.images[img].nFrame : 0;
    }
    // Look an animation up by its tag. Image *indices* differ between characters
    // (Adol carries an extra animation, shifting his idle from img3 to img4), but
    // the tags are canonical — always resolve by tag. -1 if absent.
    int find_tag(std::uint32_t tag) const {
        for (std::uint32_t i = 0; i < aia.nImg; ++i)
            if (aia.images[i].tag == tag) return int(i);
        return -1;
    }
    // Switch animation and frame, re-decoding the facings.
    void set_anim(Gpu& g, std::uint32_t img, std::uint32_t frame) {
        if (img >= aia.nImg) return;
        image = img;
        ndir = aia.images[img].nDir; nframe = aia.images[img].nFrame;
        set_frame(g, frame);
    }
    // (Re)decode `frame` for every facing into the dir textures.
    void set_frame(Gpu& g, std::uint32_t frame) {
        for (std::uint32_t d = 0; d < 8 && d < ndir; ++d) {
            AiaFrame fr;
            if (!aia.decode_frame(image, d, frame % std::max(1u, nframe), fr)) continue;
            if (dirs[d].tex) { wgpuBindGroupRelease(dirs[d].bind); wgpuTextureViewRelease(dirs[d].view); wgpuTextureRelease(dirs[d].tex); }
            dirs[d] = upload_rgba(g, fr.w, fr.h, fr.rgba.data());
        }
    }
};

// Draw an upright, camera-facing billboard of `spr` standing at `feet`, in a
// pass that keeps the already-rendered room colour + depth (so it occludes
// correctly). worldH is the sprite's world height; camForward is the (fixed)
// camera view direction. Reuses the room pipeline (alpha-test + white modulate).
static void draw_character(Gpu& g, WGPUTextureView color, WGPUTextureView depth,
                           const Vec3& feet, float worldH, const SpriteTex& spr,
                           const Vec3& camForward) {
    if (!spr.tex || spr.h == 0) return;
    const Vec3 up = {0, 1, 0};
    Vec3 right = normalize(cross(up, camForward));       // horizontal screen-right
    const float Wc = worldH * (float(spr.w) / float(spr.h));
    const Vec3 ctr = {feet.x, feet.y + worldH * 0.5f, feet.z};
    auto V = [&](float sx, float sy, float u, float v) -> RoomVertex {
        return {ctr.x + right.x * sx * Wc * 0.5f, ctr.y + sy * worldH * 0.5f,
                ctr.z + right.z * sx * Wc * 0.5f, 0, 0, -1, u, v, 255, 255, 255, 255};
    };
    RoomVertex verts[4] = {V(-1, 1, 0, 0), V(1, 1, 1, 0), V(1, -1, 1, 1), V(-1, -1, 0, 1)};
    std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    WGPUBuffer vb = make_buffer(g.device, g.queue, verts, sizeof verts, WGPUBufferUsage_Vertex);
    WGPUBuffer ib = make_buffer(g.device, g.queue, idx, sizeof idx, WGPUBufferUsage_Index);

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;   // keep the room
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = depth; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, g.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, spr.bind, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, 6, 1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    wgpuBufferRelease(vb); wgpuBufferRelease(ib);
}

// ---- headless shot ----------------------------------------------------------
struct MapReq { bool done = false; };
static void on_map(WGPUMapAsyncStatus, WGPUStringView, void* u1, void*) {
    static_cast<MapReq*>(u1)->done = true;
}

static int render_shot(Gpu& g, const Room& room, const fs::path& out,
                       std::uint32_t W, std::uint32_t H, float yaw, float pitch, float dist) {
    Camera cam; spawn_in(cam, room);
    // override to the requested orbit angle
    const Vec3 c = {(room.mesh.bmin.x + room.mesh.bmax.x) * 0.5f,
                    (room.mesh.bmin.y + room.mesh.bmax.y) * 0.5f,
                    (room.mesh.bmin.z + room.mesh.bmax.z) * 0.5f};
    const float rad = room_diag(room) * dist;
    cam.pos = {c.x + rad * std::cos(pitch) * std::cos(yaw), c.y + rad * std::sin(pitch),
               c.z + rad * std::cos(pitch) * std::sin(yaw)};
    const Vec3 fwd = normalize(c - cam.pos);
    cam.pitch = std::asin(std::max(-1.0f, std::min(1.0f, fwd.y)));
    cam.yaw = std::atan2(fwd.z, fwd.x);
    write_mvp(g, cam.view_proj(float(W) / float(H), rad * 0.02f, rad * 6.0f));

    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension = WGPUTextureDimension_2D; td.size = {W, H, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture color = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTextureView cview = wgpuTextureCreateView(color, nullptr);
    WGPUTextureView dview = make_depth(g.device, W, H);
    const std::uint32_t bpr = (W * 4 + 255) & ~255u;
    WGPUBufferDescriptor rbd = {};
    rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead; rbd.size = std::size_t(bpr) * H;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(g.device, &rbd);

    draw(g, room, cview, dview);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPUTexelCopyTextureInfo src = {}; src.texture = color; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {}; dst.buffer = readback;
    dst.layout.bytesPerRow = bpr; dst.layout.rowsPerImage = H;
    WGPUExtent3D ext = {W, H, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);

    MapReq mr;
    WGPUBufferMapCallbackInfo mi = {};
    mi.mode = WGPUCallbackMode_AllowProcessEvents; mi.callback = on_map; mi.userdata1 = &mr;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, rbd.size, mi);
    while (!mr.done) wgpuDevicePoll(g.device, true, nullptr);
    const auto* px = static_cast<const std::uint8_t*>(
        wgpuBufferGetConstMappedRange(readback, 0, rbd.size));
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
            f.write(reinterpret_cast<const char*>(px + std::size_t(y) * bpr + x * 4), 3);
    wgpuBufferUnmap(readback);
    std::printf("wrote %s (%ux%u)\n", out.string().c_str(), W, H);
    return 0;
}

// ---- HUD: screen-space text overlay -----------------------------------------
static const char* kHudWGSL = R"WGSL(
struct VsOut { @builtin(position) pos : vec4<f32>, @location(0) uv : vec2<f32>,
               @location(1) col : vec4<f32> };
@group(0) @binding(0) var tex : texture_2d<f32>;
@group(0) @binding(1) var smp : sampler;
@vertex fn vs(@location(0) p : vec2<f32>, @location(1) uv : vec2<f32>,
              @location(2) col : vec4<f32>) -> VsOut {
  var o : VsOut; o.pos = vec4<f32>(p, 0.0, 1.0); o.uv = uv; o.col = col; return o;
}
@fragment fn fs(i : VsOut) -> @location(0) vec4<f32> {
  // Sample atlas RGBA * vertex colour. Works for white glyphs (tinted by col),
  // the solid white cell (= col), and full-colour HUD sprites (col = white).
  return textureSample(tex, smp, i.uv) * i.col;
}
)WGSL";

// ---- enemies ---------------------------------------------------------------
// SCT places them; the cast name maps to a sprite by convention (CAST_M100 ->
// ANI/M_100.AIA, holding for 89/90 casts). Enemies are AIA billboards exactly
// like the player, and share the canonical tag 0x2 = idle.
//
// No AI: the engine drives each enemy from one of ~90 bespoke C++ routines, so
// there is no behaviour table to read. These stand in their placed spots.
struct EnemyFx {
    struct Kind {
        Aia aia;
        SpriteTex dirs[8];
        std::uint32_t ndir = 0;
        float height = 1.6f;
    };
    std::unordered_map<std::string, Kind> kinds;   // sprite stem -> decoded facings

    // Decode frame 0 of the idle animation for every enemy type in the room.
    void init(Gpu& g, const fs::path& asset_root, const Sct& cast) {
        for (const CastPlacement& c : cast.casts) {
            const std::string stem = c.sprite_stem();
            if (stem.empty() || kinds.count(stem)) continue;
            Kind k;
            if (!k.aia.load(asset_root / "ANI" / (stem + ".AIA"))) continue;
            int img = -1;
            for (std::uint32_t i = 0; i < k.aia.nImg; ++i)
                if (k.aia.images[i].tag == 0x2) { img = int(i); break; }   // idle
            if (img < 0) img = 0;
            k.ndir = k.aia.images[img].nDir;
            std::uint32_t tall = 1;
            for (std::uint32_t d = 0; d < 8 && d < k.ndir; ++d) {
                AiaFrame fr;
                if (!k.aia.decode_frame(std::uint32_t(img), d, 0, fr)) continue;
                k.dirs[d] = upload_rgba(g, fr.w, fr.h, fr.rgba.data());
                tall = std::max(tall, fr.h);
            }
            // Scale off the sprite's own pixel height, against the player's known
            // ~76px / 2.0 world units. Their true size is not established.
            k.height = float(tall) * (2.0f / 76.0f);
            kinds.emplace(stem, std::move(k));
        }
    }
};

// A placed enemy at runtime. HP is a PLACEHOLDER: the game has no per-enemy HP
// column — stats are lerped from a level curve in GAMEINFO — so this is our own
// number until that curve is wired up.
struct EnemyInst {
    std::string stem;
    std::string display;
    Vec3 pos{};
    int hp = 0, max_hp = 0;
    bool alive = true;
};

// Draw every living enemy as an upright camera-facing billboard.
static void draw_enemies(Gpu& g, WGPUTextureView color, WGPUTextureView depth,
                         EnemyFx& fx, const std::vector<EnemyInst>& list,
                         const Vec3& camFwd) {
    for (const EnemyInst& e : list) {
        if (!e.alive) continue;
        auto it = fx.kinds.find(e.stem);
        if (it == fx.kinds.end()) continue;
        const EnemyFx::Kind& k = it->second;
        // Always face the camera: enemy direction rings are per-file and unverified,
        // and there is no AI yet to give them a heading.
        const SpriteTex& t = k.dirs[6 < k.ndir ? 6 : 0];
        if (!t.tex) continue;
        draw_character(g, color, depth, e.pos, k.height, t, camFwd);
    }
}

// ---- scene effects: the torch flames ---------------------------------------
// SEF gives the placements; the effect id resolves through eftlist to an EF2, and
// effect 200 (`map\syokudai_honou.eft`, the torch flame) draws a camera-facing
// billboard textured with naga01.dds — a 4x4 flipbook of 16 frames. The EF2's
// emitter 1 carries scale (1, 1.5, 1), so the quad is 1 x 1.5 world units.
//
// Honest limits: the EF2's animation tracks are normalised, and the field holding
// the absolute duration was never identified, so the frame RATE below is a choice,
// not data. Blend mode is additive on strong evidence but was not confirmed in code.
struct FlameFx {
    SpriteTex tex;
    static constexpr int kCols = 4, kRows = 4, kFrames = 16;
    static constexpr float kW = 1.0f, kH = 1.5f;   // EF2 emitter-1 scale
    static constexpr float kFps = 15.0f;           // NOT from the data — see above

    bool init(Gpu& g, const fs::path& asset_root) {
        const fs::path p = asset_root / "EFFECT" / "EFT" / "YSF_USED" / "NAGA_G" / "H" / "NAGA01.DDS";
        Dds d = load_dds(p);
        if (!d.ok()) return false;
        tex = upload_rgba(g, d.width, d.height, d.data.data());
        return true;
    }
};

// Draw every torch flame in the room, in one additive pass over the room's depth.
static void draw_flames(Gpu& g, const Room& room, WGPUTextureView color,
                        WGPUTextureView depth, const FlameFx& fx, const Vec3& camFwd,
                        float t) {
    if (!fx.tex.tex || room.fx.effects.empty()) return;
    const int frame = int(t * FlameFx::kFps) % FlameFx::kFrames;
    const float du = 1.0f / FlameFx::kCols, dv = 1.0f / FlameFx::kRows;
    const float u0 = float(frame % FlameFx::kCols) * du;
    const float v0 = float(frame / FlameFx::kCols) * dv;

    const Vec3 up = {0, 1, 0};
    const Vec3 right = normalize(cross(up, camFwd));
    std::vector<RoomVertex> verts;
    std::vector<std::uint32_t> idx;
    for (const SefEffect& e : room.fx.effects) {
        if (e.id != kEffectTorchFlame) continue;
        // Anchored at the SEF point, growing upward like a flame on a stand.
        const Vec3 c = {e.position.x, e.position.y + FlameFx::kH * 0.5f, e.position.z};
        const std::uint32_t b = std::uint32_t(verts.size());
        auto V = [&](float sx, float sy, float u, float v) {
            verts.push_back({c.x + right.x * sx * FlameFx::kW * 0.5f,
                             c.y + sy * FlameFx::kH * 0.5f,
                             c.z + right.z * sx * FlameFx::kW * 0.5f,
                             0, 0, -1, u, v, 255, 255, 255, 255});
        };
        V(-1, 1, u0, v0);      V(1, 1, u0 + du, v0);
        V(1, -1, u0 + du, v0 + dv); V(-1, -1, u0, v0 + dv);
        for (std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(b + k);
    }
    if (idx.empty()) return;

    WGPUBuffer vb = make_buffer(g.device, g.queue, verts.data(),
                                verts.size() * sizeof(RoomVertex), WGPUBufferUsage_Vertex);
    WGPUBuffer ib = make_buffer(g.device, g.queue, idx.data(),
                                idx.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = depth; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, g.fx_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, fx.tex.bind, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, std::uint32_t(idx.size()), 1, 0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    wgpuBufferRelease(vb); wgpuBufferRelease(ib);
}


struct HudVertex { float x, y, u, v; std::uint8_t r, g, b, a; };

// STATUS_512.DDS source rects (measured; build/hud_re/hud_atlas_spec.json).
struct Rc { float x, y, w, h; };
// Gold digit font, glyphs 0..9 then ':' then '/'. y=15 h=22, proportional width.
static const Rc kDigitGold[12] = {
    {0,15,11,22}, {15,15,6,22}, {24,15,11,22}, {37,15,10,22}, {51,15,8,22},
    {62,15,9,22}, {73,15,11,22}, {87,15,7,22}, {96,15,11,22}, {110,15,9,22},
    {123,15,6,22}, {135,15,5,22}};
// Character portrait units (round face + ring + item tab), by g_flags[150].
// 0/1=Yunica, 2=Hugo, 3=Toal (Adol @250,257 is unused in Origin).
static const Rc kPortrait[4] = {
    {364,257,116,100}, {364,257,116,100}, {250,362,116,100}, {364,362,116,100}};
static const Rc kHpFrame   = {0, 472, 270, 37};   // ornate HP-bar housing
static const Rc kGreenBand  = {1, 45, 248, 7};     // bright green gauge fill
static const Rc kGreenTrack = {1, 69, 248, 7};     // dark green track

// ---- the game's real HUD layout (from build/source_map/HUD_COORDS.md) --------
// Recovered from the status-HUD renderer FUN_004295d0 / builder FUN_0043c230.
// The HUD is authored in a 1024x768 reference frame, 1:1 against the 512x512
// STATUS atlas, and mapped to the backbuffer by FUN_005e3ed0 / FUN_005e4100 /
// FUN_005d3be0. Every element is LEFT-anchored (all 72 MapCoord calls are
// axis=X, mode=1), the effective scale is always H/768, and widescreen is Hor+:
// the HUD does NOT stretch, the extra width just reveals more scene.
struct HudMap {
    float sx = 1, sy = 1;
    bool wide = false, identity = false;
    HudMap(std::uint32_t W, std::uint32_t H) {
        const int w43 = int(H * 4 / 3);            // integer division, as in the exe
        wide = int(W) > w43;
        identity = (W == 1024 && H == 768);        // the exe's literal no-scale case
        sy = float(H) / 768.0f;
        sx = wide ? float(w43) / 1024.0f : float(W) / 1024.0f;
    }
    float x(float v) const {
        if (identity) return std::round(v) - 0.5f;
        return wide ? std::trunc(v * sx) : std::round(v * sx) - 0.5f;
    }
    float y(float v) const {
        return identity ? std::round(v) - 0.5f : std::round(v * sy) - 0.5f;
    }
};

// Authored rects in the 1024x768 frame, {x0, y0, x1, y1}.
struct Ar { float x0, y0, x1, y1; };
static const Ar kAPortrait = {0, 670, 116, 766};    // character portrait, 116x96
static const Ar kAHpPlate  = {115, 714, 386, 758};  // HP bar frame / plate
static const Ar kAHpTrack  = {120, 735, 372, 747};  // HP track; max fill width = 252
static const Ar kAHpFill   = {120, 737, 372, 745};  // HP fill (x1 is dynamic)
static const Ar kAThinBar  = {142, 718, 372, 722};  // thin bar (EX/MP — name unconfirmed)
static const Ar kASlot[4] = {{0, 620, 40, 632}, {0, 632, 40, 644},
                             {0, 644, 40, 656}, {0, 656, 40, 668}};  // equipment slots
// Portrait UVs: 4 portraits in a 2x2 grid, 116x96 px each, indexed by character.
// base (244,260); +120 x for Yunica/Toal, +104 y for Hugo/Toal.
static const Rc kPortraitReal[4] = {{244, 260, 116, 96},   // 0 Adol
                                    {364, 260, 116, 96},   // 1 Yunica
                                    {244, 364, 116, 96},   // 2 Hugo
                                    {364, 364, 116, 96}};  // 3 Toal
static const Rc kArcBlue   = {4, 378, 68, 73};    // EX arc gauge
static const Rc kArcRed    = {74, 378, 66, 73};   // MP/boost arc gauge
// Skill/item hotbar diamond slots (first row of the left icon block, cell 38).
static const Rc kSkillSlot[4] = {{1,133,38,38}, {41,133,38,38}, {81,133,38,38}, {121,133,38,38}};

struct Hud {
    WGPURenderPipeline pipeline{};
    WGPUBindGroup bind{};                // font atlas bind group (group 0)
    WGPUBindGroupLayout bgl{};           // shared group-0 layout
    WGPUSampler samp{};
    WGPUBuffer vbuf{};
    std::size_t vbuf_cap = 0;
    std::uint32_t aw = 0, ah = 0, cw = 0, ch = 0, first = 32, cols = 16, nchars = 95;
    std::vector<HudVertex> verts;        // font + solid rects (font atlas)
    // Optional secondary texture atlas (the game's STATUS_512 HUD graphics).
    WGPUBindGroup atlas_bind{};
    std::uint32_t atlas_w = 0, atlas_h = 0;
    std::vector<HudVertex> sprites;      // quads sampling the STATUS atlas
    // Boss health bar (HP2 atlas). Shown when boss_frac >= 0.
    WGPUBindGroup boss_bind{};
    std::uint32_t boss_w = 0, boss_h = 0;
    std::vector<HudVertex> boss_sprites; // quads sampling the HP2 atlas
    float boss_frac = -1.f;              // <0 = no boss on screen
    std::string boss_name;
    struct Msg { std::string text; float ttl; };
    std::vector<Msg> messages;

    void init(Gpu& g, const fs::path& font_bin, WGPUTextureFormat color) {
        std::vector<std::uint8_t> b = read_bytes(font_bin);
        if (b.size() < 32) { std::fprintf(stderr, "hud: font missing\n"); return; }
        std::uint32_t hdr[8];
        std::memcpy(hdr, b.data(), 32);
        aw = hdr[1]; ah = hdr[2]; cw = hdr[3]; ch = hdr[4]; first = hdr[5]; nchars = hdr[6]; cols = hdr[7];
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = {aw, ah, 1};
        td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
        WGPUTexture tex = wgpuDeviceCreateTexture(g.device, &td);
        WGPUTexelCopyTextureInfo dst = {}; dst.texture = tex; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay = {}; lay.bytesPerRow = aw * 4; lay.rowsPerImage = ah;
        WGPUExtent3D ext = {aw, ah, 1};
        wgpuQueueWriteTexture(g.queue, &dst, b.data() + 32, std::size_t(aw) * ah * 4, &lay, &ext);
        WGPUTextureView view = wgpuTextureCreateView(tex, nullptr);
        wgpuTextureRelease(tex);

        WGPUShaderSourceWGSL wgsl = {};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL; wgsl.code = sv(kHudWGSL);
        WGPUShaderModuleDescriptor smd = {}; smd.nextInChain = &wgsl.chain;
        WGPUShaderModule sm = wgpuDeviceCreateShaderModule(g.device, &smd);
        WGPUVertexAttribute at[3] = {};
        at[0].format = WGPUVertexFormat_Float32x2; at[0].offset = 0;  at[0].shaderLocation = 0;
        at[1].format = WGPUVertexFormat_Float32x2; at[1].offset = 8;  at[1].shaderLocation = 1;
        at[2].format = WGPUVertexFormat_Unorm8x4;  at[2].offset = 16; at[2].shaderLocation = 2;
        WGPUVertexBufferLayout vbl = {};
        vbl.arrayStride = sizeof(HudVertex); vbl.attributeCount = 3; vbl.attributes = at;
        WGPUBlendState blend = {};
        blend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_OneMinusSrcAlpha};
        blend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_OneMinusSrcAlpha};
        WGPUColorTargetState tgt = {}; tgt.format = color; tgt.blend = &blend; tgt.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fs = {}; fs.module = sm; fs.entryPoint = sv("fs"); fs.targetCount = 1; fs.targets = &tgt;
        WGPURenderPipelineDescriptor pd = {};
        pd.vertex.module = sm; pd.vertex.entryPoint = sv("vs");
        pd.vertex.bufferCount = 1; pd.vertex.buffers = &vbl;
        pd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFF; pd.fragment = &fs;
        pipeline = wgpuDeviceCreateRenderPipeline(g.device, &pd);

        WGPUSamplerDescriptor sad = {};
        sad.magFilter = sad.minFilter = WGPUFilterMode_Linear; sad.maxAnisotropy = 1;
        samp = wgpuDeviceCreateSampler(g.device, &sad);
        bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry e[2] = {};
        e[0].binding = 0; e[0].textureView = view;
        e[1].binding = 1; e[1].sampler = samp;
        WGPUBindGroupDescriptor bd = {};
        bd.layout = bgl; bd.entryCount = 2; bd.entries = e;
        bind = wgpuDeviceCreateBindGroup(g.device, &bd);
        wgpuShaderModuleRelease(sm);
    }

    // Upload a DDS (RGBA/BC1/BC2) as a group-0 bind group; returns it and its size.
    WGPUBindGroup upload_atlas(Gpu& g, const fs::path& dds_path, std::uint32_t& ow, std::uint32_t& oh) {
        Dds d = load_dds(dds_path);
        if (!d.ok()) { std::fprintf(stderr, "hud atlas: %s failed\n", dds_path.string().c_str()); return nullptr; }
        ow = d.width; oh = d.height;
        WGPUTextureFormat fmt = WGPUTextureFormat_RGBA8Unorm;
        std::uint32_t block = 1, blk = 4;
        if (d.format == Dds::BC1) { fmt = WGPUTextureFormat_BC1RGBAUnorm; block = 4; blk = 8; }
        else if (d.format == Dds::BC2) { fmt = WGPUTextureFormat_BC2RGBAUnorm; block = 4; blk = 16; }
        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = {ow, oh, 1};
        td.format = fmt; td.mipLevelCount = 1; td.sampleCount = 1;
        WGPUTexture t = wgpuDeviceCreateTexture(g.device, &td);
        WGPUTexelCopyTextureInfo dst = {}; dst.texture = t; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay = {};
        lay.bytesPerRow = ((ow + block - 1) / block) * blk;
        lay.rowsPerImage = (oh + block - 1) / block;
        WGPUExtent3D ext = {ow, oh, 1};
        wgpuQueueWriteTexture(g.queue, &dst, d.data.data(), d.data.size(), &lay, &ext);
        WGPUTextureView v = wgpuTextureCreateView(t, nullptr);
        wgpuTextureRelease(t);
        WGPUBindGroupEntry e[2] = {};
        e[0].binding = 0; e[0].textureView = v; e[1].binding = 1; e[1].sampler = samp;
        WGPUBindGroupDescriptor bd = {}; bd.layout = bgl; bd.entryCount = 2; bd.entries = e;
        return wgpuDeviceCreateBindGroup(g.device, &bd);
    }

    // Load the game's STATUS_512 HUD atlas (player status graphics).
    void init_atlas(Gpu& g, const fs::path& dds_path) {
        atlas_bind = upload_atlas(g, dds_path, atlas_w, atlas_h);
    }
    // Load the game's HP2 atlas (boss health bar graphics).
    void init_boss(Gpu& g, const fs::path& dds_path) {
        boss_bind = upload_atlas(g, dds_path, boss_w, boss_h);
    }

    // Draw a quad of the STATUS atlas: dst screen rect (px), src atlas rect (px).
    void sprite(float dx, float dy, float dw, float dh,
                float sx, float sy, float sw_px, float sh_px, float sw, float sh,
                std::uint8_t r = 255, std::uint8_t gg = 255, std::uint8_t b = 255, std::uint8_t a = 255) {
        if (!atlas_w) return;
        const float u0 = sx / atlas_w, v0 = sy / atlas_h;
        const float u1 = (sx + sw_px) / atlas_w, v1 = (sy + sh_px) / atlas_h;
        auto NX = [&](float v){ return v / sw * 2.f - 1.f; };
        auto NY = [&](float v){ return 1.f - v / sh * 2.f; };
        const float x0 = NX(dx), x1 = NX(dx + dw), y0 = NY(dy), y1 = NY(dy + dh);
        HudVertex q0{x0, y0, u0, v0, r, gg, b, a}, q1{x1, y0, u1, v0, r, gg, b, a};
        HudVertex q2{x1, y1, u1, v1, r, gg, b, a}, q3{x0, y1, u0, v1, r, gg, b, a};
        sprites.push_back(q0); sprites.push_back(q1); sprites.push_back(q2);
        sprites.push_back(q0); sprites.push_back(q2); sprites.push_back(q3);
    }

    void message(const std::string& s) { messages.push_back({s, 5.0f}); }

    // Append a string at screen pixel (px,py), scale, RGBA.
    void text(const std::string& s, float px, float py, float scale, float sw, float sh,
              std::uint8_t r, std::uint8_t gg, std::uint8_t b, std::uint8_t a) {
        const float gw = cw * scale, gh = ch * scale;
        float x = px;
        for (char c : s) {
            if (c == '\n') { x = px; py += gh; continue; }
            const int idx = int((unsigned char)c) - int(first);
            if (idx < 0 || idx >= 95) { x += gw; continue; }
            const float u0 = float((idx % cols) * cw) / aw, v0 = float((idx / cols) * ch) / ah;
            const float u1 = u0 + float(cw) / aw, v1 = v0 + float(ch) / ah;
            auto NX = [&](float v){ return v / sw * 2.f - 1.f; };
            auto NY = [&](float v){ return 1.f - v / sh * 2.f; };
            const float x0 = NX(x), x1 = NX(x + gw), y0 = NY(py), y1 = NY(py + gh);
            HudVertex q0{x0, y0, u0, v0, r, gg, b, a}, q1{x1, y0, u1, v0, r, gg, b, a};
            HudVertex q2{x1, y1, u1, v1, r, gg, b, a}, q3{x0, y1, u0, v1, r, gg, b, a};
            verts.push_back(q0); verts.push_back(q1); verts.push_back(q2);
            verts.push_back(q0); verts.push_back(q2); verts.push_back(q3);
            x += gw;
        }
    }

    // Solid filled rectangle (samples the atlas's reserved white cell `nchars`).
    void rect(float px, float py, float w, float h, float sw, float sh,
              std::uint8_t r, std::uint8_t gg, std::uint8_t b, std::uint8_t a) {
        const float su = (float((nchars % cols) * cw) + cw * 0.5f) / aw;
        const float sv = (float((nchars / cols) * ch) + ch * 0.5f) / ah;
        auto NX = [&](float v){ return v / sw * 2.f - 1.f; };
        auto NY = [&](float v){ return 1.f - v / sh * 2.f; };
        const float x0 = NX(px), x1 = NX(px + w), y0 = NY(py), y1 = NY(py + h);
        HudVertex q0{x0, y0, su, sv, r, gg, b, a}, q1{x1, y0, su, sv, r, gg, b, a};
        HudVertex q2{x1, y1, su, sv, r, gg, b, a}, q3{x0, y1, su, sv, r, gg, b, a};
        verts.push_back(q0); verts.push_back(q1); verts.push_back(q2);
        verts.push_back(q0); verts.push_back(q2); verts.push_back(q3);
    }

    // Draw an integer using the atlas gold digit font. Returns pixels advanced.
    float draw_number(int value, float x, float y, float scale, float sw, float sh) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d", value);
        float cx = x;
        for (const char* c = buf; *c; ++c) {
            if (*c < '0' || *c > '9') continue;
            const Rc& r = kDigitGold[*c - '0'];
            sprite(cx, y, r.w * scale, r.h * scale, r.x, r.y, r.w, r.h, sw, sh);
            cx += (r.w + 1) * scale;
        }
        return cx - x;
    }

    // Draw an authored 1024x768 rect of the STATUS atlas at its real screen place.
    void sprite_a(const HudMap& m, const Ar& d, const Rc& src, float sw, float sh,
                  std::uint8_t r = 255, std::uint8_t gg = 255, std::uint8_t b = 255,
                  std::uint8_t a = 255) {
        const float x0 = m.x(d.x0), y0 = m.y(d.y0);
        sprite(x0, y0, m.x(d.x1) - x0, m.y(d.y1) - y0,
               src.x, src.y, src.w, src.h, sw, sh, r, gg, b, a);
    }

    // The player status HUD, at the game's REAL authored coordinates (recovered from
    // the status-HUD builder FUN_0043c230 — see build/source_map/HUD_COORDS.md).
    void player_hud(const Player& p, float sw, float sh) {
        const HudMap m{std::uint32_t(sw), std::uint32_t(sh)};

        // NOT DRAWN: the four stacked 12px rows at x0=0 (y 620/632/644/656). Their
        // geometry is recovered, but their identity is not — an earlier pass called them
        // "equipment slots", yet they sit flush left with RUNTIME-COMPUTED widths, which
        // reads as four stacked bars, and the real game shows equipment as a horizontal
        // row beside the portrait. Drawing skill diamonds there was visibly wrong, so we
        // draw nothing until we know what they are.

        // HP plate, then the track and the fill inside its channel.
        sprite_a(m, kAHpPlate, kHpFrame, sw, sh);
        sprite_a(m, kAHpTrack, kGreenTrack, sw, sh);
        const float frac = p.max_hp > 0 ? std::max(0.f, std::min(1.f, float(p.hp) / p.max_hp)) : 0.f;
        Ar fill = kAHpFill;
        fill.x1 = fill.x0 + 252.0f * frac;   // 252 = the authored max fill width
        sprite_a(m, fill, kGreenBand, sw, sh);

        // Character portrait (drawn over the plate's left end, as the game does).
        const int ci = (p.character >= 0 && p.character < 4) ? p.character : 1;
        sprite_a(m, kAPortrait, kPortraitReal[ci], sw, sh);

        // Level digits + the SP/EXP readout. NOTE: the digit emitter (builder B or D)
        // was not decoded, so these two remain placed by eye — everything above is real.
        const float s = std::max(1.f, sh / 768.f);
        text("Lv", m.x(392), m.y(724), s * 0.6f, sw, sh, 225, 215, 165, 255);
        draw_number(p.level, m.x(414), m.y(716), s * 1.1f, sw, sh);
        char lbl[48];
        std::snprintf(lbl, sizeof lbl, "SP %d    EXP %.1f%%", p.sp, p.exp_pct);
        text(lbl, m.x(120), m.y(700), s * 0.5f, sw, sh, 175, 195, 225, 235);
    }

    // Draw a quad of the HP2 (boss) atlas: dst screen rect (px), src atlas rect (px).
    void boss_sprite(float dx, float dy, float dw, float dh,
                     float sx, float sy, float sw_px, float sh_px, float sw, float sh,
                     std::uint8_t r = 255, std::uint8_t gg = 255, std::uint8_t b = 255, std::uint8_t a = 255) {
        if (!boss_w) return;
        const float u0 = sx / boss_w, v0 = sy / boss_h;
        const float u1 = (sx + sw_px) / boss_w, v1 = (sy + sh_px) / boss_h;
        auto NX = [&](float v){ return v / sw * 2.f - 1.f; };
        auto NY = [&](float v){ return 1.f - v / sh * 2.f; };
        const float x0 = NX(dx), x1 = NX(dx + dw), y0 = NY(dy), y1 = NY(dy + dh);
        HudVertex q0{x0, y0, u0, v0, r, gg, b, a}, q1{x1, y0, u1, v0, r, gg, b, a};
        HudVertex q2{x1, y1, u1, v1, r, gg, b, a}, q3{x0, y1, u0, v1, r, gg, b, a};
        boss_sprites.push_back(q0); boss_sprites.push_back(q1); boss_sprites.push_back(q2);
        boss_sprites.push_back(q0); boss_sprites.push_back(q2); boss_sprites.push_back(q3);
    }

    // Boss health bar (top-centre), from the game's HP2 atlas: ornate frame + red fill.
    void boss_bar(float frac, const std::string& name, float sw, float sh) {
        if (!boss_w) return;
        frac = std::max(0.f, std::min(1.f, frac));
        const float s = std::max(1.f, sh / 720.f);
        const Rc frame = {0, 99, 161, 26};   // dark ornate housing
        const Rc fill  = {0, 161, 161, 12};  // red gauge
        const float bw = sw * 0.58f, bh = frame.h * (bw / frame.w);
        const float bx = (sw - bw) * 0.5f, by = 20 * s;
        boss_sprite(bx, by, bw, bh, frame.x, frame.y, frame.w, frame.h, sw, sh);
        // Red fill inside the frame's channel, clipped to the current fraction.
        const float inset = bw * 0.018f;
        const float tw = bw - 2 * inset, ry = by + bh * 0.30f, rh = bh * 0.42f;
        boss_sprite(bx + inset, ry, tw * frac, rh, fill.x, fill.y, fill.w * frac, fill.h, sw, sh);
        // Boss name centred just above the bar.
        if (!name.empty())
            text(name, sw * 0.5f - name.size() * cw * s * 0.22f, by - 20 * s, s * 0.7f,
                 sw, sh, 235, 225, 205, 245);
    }

    // Build this frame's HUD, then draw it over `colorView`.
    void render(Gpu& g, WGPUTextureView colorView, std::uint32_t sw, std::uint32_t sh,
                const std::string& room_name, float dt, const Player* player = nullptr) {
        verts.clear();
        sprites.clear();
        boss_sprites.clear();
        const float s = std::max(1.f, sh / 540.f);  // scale text to window height
        text("Room: " + room_name, 12 * s, 10 * s, s, float(sw), float(sh), 230, 220, 150, 255);
        float my = float(sh) * 0.5f;
        for (auto it = messages.begin(); it != messages.end();) {
            it->ttl -= dt;
            if (it->ttl <= 0) { it = messages.erase(it); continue; }
            const std::uint8_t a = std::uint8_t(255 * std::min(1.0f, it->ttl));
            text(it->text, 12 * s, my, s * 1.15f, float(sw), float(sh), 255, 235, 120, a);
            my += 30 * s; ++it;
        }
        if (player) player_hud(*player, float(sw), float(sh));
        if (boss_frac >= 0.f) boss_bar(boss_frac, boss_name, float(sw), float(sh));
        const std::size_t nf = verts.size(), ns = sprites.size(), nb = boss_sprites.size();
        if (nf + ns + nb == 0) return;

        // Concatenate the batches: font atlas verts, STATUS sprites, then HP2 sprites.
        const std::size_t bytes = (nf + ns + nb) * sizeof(HudVertex);
        if (bytes > vbuf_cap) {
            if (vbuf) wgpuBufferRelease(vbuf);
            WGPUBufferDescriptor bd = {};
            bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            bd.size = ((bytes + 4095) / 4096) * 4096;
            vbuf = wgpuDeviceCreateBuffer(g.device, &bd);
            vbuf_cap = bd.size;
        }
        if (nf) wgpuQueueWriteBuffer(g.queue, vbuf, 0, verts.data(), nf * sizeof(HudVertex));
        if (ns) wgpuQueueWriteBuffer(g.queue, vbuf, nf * sizeof(HudVertex), sprites.data(), ns * sizeof(HudVertex));
        if (nb) wgpuQueueWriteBuffer(g.queue, vbuf, (nf + ns) * sizeof(HudVertex), boss_sprites.data(), nb * sizeof(HudVertex));

        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPURenderPassColorAttachment ca = {};
        ca.view = colorView; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;  // keep the 3D scene
        WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        if (nf) {  // font/solid batch
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bind, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, 0, nf * sizeof(HudVertex));
            wgpuRenderPassEncoderDraw(pass, std::uint32_t(nf), 1, 0, 0);
        }
        if (ns && atlas_bind) {  // STATUS atlas batch
            wgpuRenderPassEncoderSetBindGroup(pass, 0, atlas_bind, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, nf * sizeof(HudVertex), ns * sizeof(HudVertex));
            wgpuRenderPassEncoderDraw(pass, std::uint32_t(ns), 1, 0, 0);
        }
        if (nb && boss_bind) {  // HP2 (boss bar) batch
            wgpuRenderPassEncoderSetBindGroup(pass, 0, boss_bind, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, (nf + ns) * sizeof(HudVertex), nb * sizeof(HudVertex));
            wgpuRenderPassEncoderDraw(pass, std::uint32_t(nb), 1, 0, 0);
        }
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(g.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    }
};
// First-person verification shot: spawn the player on the floor and render from
// eye level looking forward. Confirms floor height / eye placement headlessly.
static int render_fps(Gpu& g, const Room& room, const fs::path& out,
                      std::uint32_t W, std::uint32_t H, float yaw,
                      const fs::path& asset_root = {}) {
    const Vec3 c = {(room.mesh.bmin.x + room.mesh.bmax.x) * 0.5f, 0,
                    (room.mesh.bmin.z + room.mesh.bmax.z) * 0.5f};
    const float D = room_diag(room), eye = D * 0.02f;
    const float fl = floor_at(room.mesh, c.x, c.z, room.mesh.bmax.y + D);
    const float fy = (fl > kNoFloor) ? fl : (room.mesh.bmin.y + room.mesh.bmax.y) * 0.5f;
    Camera cam;
    cam.pos = {c.x, fy + eye, c.z};
    cam.yaw = yaw; cam.pitch = -0.1f;
    write_mvp(g, cam.view_proj(float(W) / float(H), eye * 0.05f, D * 4.0f));

    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension = WGPUTextureDimension_2D; td.size = {W, H, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture color = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTextureView cview = wgpuTextureCreateView(color, nullptr);
    WGPUTextureView dview = make_depth(g.device, W, H);
    const std::uint32_t bpr = (W * 4 + 255) & ~255u;
    WGPUBufferDescriptor rbd = {};
    rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead; rbd.size = std::size_t(bpr) * H;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(g.device, &rbd);
    draw(g, room, cview, dview);
    // HUD preview (verifies text rendering headlessly).
    Hud hud; hud.init(g, res_file("hud_font.bin"), WGPUTextureFormat_RGBA8Unorm);
    if (!asset_root.empty()) {
        hud.init_atlas(g, asset_root / "IMAGE" / "STATUS_512.DDS");
        hud.init_boss(g, asset_root / "IMAGE" / "HP2.DDS");
        hud.boss_frac = 0.72f; hud.boss_name = "Nygtilger";  // preview the boss bar
    }
    hud.message("Acquired Chain Mail");
    Player pv; pv.hp = 312; pv.max_hp = 400; pv.level = 6; pv.sp = 138663; pv.exp_pct = 42.1f;
    hud.render(g, cview, W, H, room.name, 0.016f, &pv);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPUTexelCopyTextureInfo src = {}; src.texture = color; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {}; dst.buffer = readback;
    dst.layout.bytesPerRow = bpr; dst.layout.rowsPerImage = H;
    WGPUExtent3D ext = {W, H, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd); wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    MapReq mr;
    WGPUBufferMapCallbackInfo mi = {};
    mi.mode = WGPUCallbackMode_AllowProcessEvents; mi.callback = on_map; mi.userdata1 = &mr;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, rbd.size, mi);
    while (!mr.done) wgpuDevicePoll(g.device, true, nullptr);
    const auto* px = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(readback, 0, rbd.size));
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
            f.write(reinterpret_cast<const char*>(px + std::size_t(y) * bpr + x * 4), 3);
    wgpuBufferUnmap(readback);
    std::printf("fps shot %s: eye at y=%.2f (floor %.2f)\n", out.string().c_str(), fy + eye, fy);
    return 0;
}

// Headless "play" shot: the fixed-angle follow camera framing the character
// billboard centred on screen, over the room, with the HUD. For iterating the
// camera angle/zoom and sprite scale via PNGs before wiring the interactive loop.
// dist / worldH are ABSOLUTE world units (see the play-mode constants).
static int render_play(Gpu& g, const Room& room, const fs::path& out,
                       std::uint32_t W, std::uint32_t H, const fs::path& asset_root,
                       float yaw, float pitch, float dist, float worldH,
                       std::uint32_t dir) {
    const Vec3 c = {(room.mesh.bmin.x + room.mesh.bmax.x) * 0.5f, 0,
                    (room.mesh.bmin.z + room.mesh.bmax.z) * 0.5f};
    const float D = room_diag(room);
    const float fl = floor_at(room.mesh, c.x, c.z, room.mesh.bmax.y + D);
    Vec3 feet = {c.x, (fl > kNoFloor) ? fl : (room.mesh.bmin.y + room.mesh.bmax.y) * 0.5f, c.z};
    // Debug: CLERIA_SPAWN="x,y,z" drops the character somewhere specific, for framing
    // a particular bit of the room headlessly.
    if (const char* sp = std::getenv("CLERIA_SPAWN")) {
        float x, y, z;
        if (std::sscanf(sp, "%f,%f,%f", &x, &y, &z) == 3) feet = {x, y, z};
    }

    // Fixed camera: constant angle, positioned so the character centre is centred.
    update_fog(room, feet, 0, /*snap=*/true);
    Camera cam;
    update_camera(room, feet, 0, /*snap=*/true);
    if (g_cam.on) {  // the room's authored SCM camera
        apply_cam_state(cam, feet);
        const CameraRegion* rg = room.cam.pick(feet);
        std::printf("play: SCM camera \"%s\" fov=%.1fdeg pitch=%.1f dist=%.1f yaw=%.1f\n",
                    rg ? rg->name.c_str() : "?", g_cam.fov * 57.29578f,
                    rg ? rg->eye.pitch * 57.29578f : 0.f, rg ? rg->eye.dist : 0.f,
                    rg ? rg->eye.yaw * 57.29578f : 0.f);
    } else {  // no SCM data -> fall back to the args
        const Vec3 fwd = {std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)};
        const Vec3 center = {feet.x, feet.y + worldH * 0.5f, feet.z};
        cam.pos = {center.x - fwd.x * dist, center.y - fwd.y * dist, center.z - fwd.z * dist};
        cam.yaw = yaw; cam.pitch = pitch;
    }
    write_mvp(g, cam.view_proj(float(W) / float(H), D * 0.01f, D * 6.0f));

    WGPUTextureDescriptor td = {};
    td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    td.dimension = WGPUTextureDimension_2D; td.size = {W, H, 1};
    td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
    WGPUTexture color = wgpuDeviceCreateTexture(g.device, &td);
    WGPUTextureView cview = wgpuTextureCreateView(color, nullptr);
    WGPUTextureView dview = make_depth(g.device, W, H);
    const std::uint32_t bpr = (W * 4 + 255) & ~255u;
    WGPUBufferDescriptor rbd = {};
    rbd.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead; rbd.size = std::size_t(bpr) * H;
    WGPUBuffer readback = wgpuDeviceCreateBuffer(g.device, &rbd);

    for (const Interactable& it : room.mesh.interactables)
        std::printf("  chest %s at (%.1f, %.1f, %.1f)\n", it.name.c_str(),
                    it.position.x, it.position.y, it.position.z);
    std::printf("  spawn feet (%.1f, %.1f, %.1f)  bmin(%.1f,%.1f,%.1f) bmax(%.1f,%.1f,%.1f)\n",
                feet.x, feet.y, feet.z, room.mesh.bmin.x, room.mesh.bmin.y, room.mesh.bmin.z,
                room.mesh.bmax.x, room.mesh.bmax.y, room.mesh.bmax.z);
    draw(g, room, cview, dview);
    CharSprite ch;
    bool have = ch.load(g, asset_root / "ANI" / "C_102.AIA", 0);  // Yunica (base sprite)
    if (have) {
        const std::uint32_t d = dir < ch.ndir ? dir : 0;
        draw_character(g, cview, dview, feet, worldH, ch.dirs[d], cam.forward());
        std::printf("play: sprite %ux%u dir %u/%u, worldH %.2f, cam dist %.2f\n",
                    ch.dirs[d].w, ch.dirs[d].h, d, ch.ndir, worldH, dist);
    } else std::printf("play: character AIA load FAILED\n");

    // Enemies (SCT placements + their AIA sprites).
    {
        EnemyFx efx;
        efx.init(g, asset_root, room.cast);
        std::vector<EnemyInst> list;
        for (const CastPlacement& c : room.cast.casts) {
            const std::string stem = c.sprite_stem();
            if (!stem.empty()) list.push_back({stem, stem, c.position, 20, 20, true});
        }
        draw_enemies(g, cview, dview, efx, list, cam.forward());
    }
    // Torch flames (SEF placements + the EF2 flipbook).
    {
        FlameFx fx;
        if (fx.init(g, asset_root))
            draw_flames(g, room, cview, dview, fx, cam.forward(), 0.30f);
        else std::printf("play: NAGA01.DDS (flame) load FAILED\n");
    }

    Hud hud; hud.init(g, res_file("hud_font.bin"), WGPUTextureFormat_RGBA8Unorm);
    hud.init_atlas(g, asset_root / "IMAGE" / "STATUS_512.DDS");
    Player pv; pv.hp = 312; pv.max_hp = 400; pv.level = 6; pv.sp = 138663; pv.exp_pct = 42.1f;
    hud.render(g, cview, W, H, room.name, 0.016f, &pv);

    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPUTexelCopyTextureInfo src = {}; src.texture = color; src.aspect = WGPUTextureAspect_All;
    WGPUTexelCopyBufferInfo dst = {}; dst.buffer = readback;
    dst.layout.bytesPerRow = bpr; dst.layout.rowsPerImage = H;
    WGPUExtent3D ext = {W, H, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &ext);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(g.queue, 1, &cmd); wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
    MapReq mr;
    WGPUBufferMapCallbackInfo mi = {};
    mi.mode = WGPUCallbackMode_AllowProcessEvents; mi.callback = on_map; mi.userdata1 = &mr;
    wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, rbd.size, mi);
    while (!mr.done) wgpuDevicePoll(g.device, true, nullptr);
    const auto* px = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(readback, 0, rbd.size));
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
            f.write(reinterpret_cast<const char*>(px + std::size_t(y) * bpr + x * 4), 3);
    wgpuBufferUnmap(readback);
    std::printf("play shot %s\n", out.string().c_str());
    return 0;
}


// ---- interactive walkthrough ------------------------------------------------
static int render_window(Gpu& g, const fs::path& asset_root, const fs::path& first_sob) {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("CleriaCore - Ys Origin", 1280, 800, SDL_WINDOW_RESIZABLE);
    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    WGPUSurfaceSourceWindowsHWND srcw = {};
    srcw.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    srcw.hinstance = GetModuleHandle(nullptr); srcw.hwnd = hwnd;
    WGPUSurfaceDescriptor sd = {}; sd.nextInChain = &srcw.chain;
    WGPUSurface surface = wgpuInstanceCreateSurface(g.instance, &sd);
    WGPUSurfaceCapabilities caps = {};
    wgpuSurfaceGetCapabilities(surface, g.adapter, &caps);
    const WGPUTextureFormat fmt = caps.formats[0];
    std::uint32_t cw = 1280, ch = 800;
    auto configure = [&] {
        WGPUSurfaceConfiguration sc = {};
        sc.device = g.device; sc.format = fmt; sc.usage = WGPUTextureUsage_RenderAttachment;
        sc.width = cw; sc.height = ch; sc.alphaMode = WGPUCompositeAlphaMode_Auto;
        sc.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(surface, &sc);
    };
    configure();
    build_pipeline(g, fmt);
    Hud hud; hud.init(g, res_file("hud_font.bin"), fmt);
    hud.init_atlas(g, asset_root / "IMAGE" / "STATUS_512.DDS");
    FlameFx flames;                      // torch flames (SEF placements + EF2 flipbook)
    if (!flames.init(g, asset_root))
        std::fprintf(stderr, "flames: NAGA01.DDS failed to load\n");
    float fx_time = 0;
    EnemyFx efx;                         // enemy sprite sets, per room
    std::vector<EnemyInst> enemies;      // live enemy instances
    float atk_cd = 0;                    // attack cooldown
    WGPUTextureView depth = make_depth(g.device, cw, ch);

    int character = 1;                 // g_flags[150]: 1 Yunica / 2 Hugo / 3 Toal
    XsoVm game;                        // persistent game state (survives rooms)
    game.g_flags[150] = character;
    Player player;                     // stat block (HP/SP/level/EXP)
    Room room = load_room(g, asset_root, first_sob, game.g_flags, character);
    Camera cam;
    bool fly = false;
    Vec3 feet{}; float vy = 0; bool grounded = false; float speed_mult = 1.0f;

    // Play mode. The camera is NOT hand-tuned any more — it comes from the room's SCM
    // regions (see update_camera). Character size stays an absolute world height: the
    // engine derives it per-actor from the player struct, which we don't model yet.
    const float kCharH = 2.0f;      // character height (world units)
    // Player physics, also in absolute world units (see the note above — anything
    // scaled by room size ends up different in every room).
    const float kCharRadius = 0.45f;  // collision radius
    const float kGravity = 30.0f;     // units/s^2
    const float kWalkSpeed = 10.0f;   // units/s
    const float kStepUp = 0.6f;       // max step-up
    const float kJumpH = 2.3f;        // jump apex (~1.1x her height)
    // Base player sprites: C_102/103/104 = Yunica / Hugo / Toal. (C_110-115 are the
    // late-game / weapon-state variants — wrong look for normal play.)
    auto char_aia = [](int c) -> const char* {
        return c == 2 ? "C_103.AIA" : c == 3 ? "C_104.AIA" : "C_102.AIA";
    };
    // Canonical gameplay animation tags — identical across every player sprite
    // (user-identified from animation GIFs; see tools/aia_gif.py). Resolve by TAG,
    // never by image index: the indices shift from character to character.
    // Tag 1 (run) is the dash, which the game gates behind an item — not wired yet.
    const std::uint32_t kTagWalk = 0x0, kTagRun = 0x1, kTagIdle = 0x2, kTagJump = 0x3;
    CharSprite chr;
    chr.load(g, asset_root / "ANI" / char_aia(character), 0);
    int facing = 2;              // dir2 = back to camera (facing north, into the room)
    std::uint32_t curAnim = std::uint32_t(std::max(0, chr.find_tag(kTagIdle)));
    int animFrame = 0, shownFrame = -1; float animT = 0;
    chr.set_anim(g, curAnim, 0);
    (void)kTagRun;

    auto center = [](const Room& r) -> Vec3 {
        return {(r.mesh.bmin.x + r.mesh.bmax.x) * 0.5f,
                (r.mesh.bmin.y + r.mesh.bmax.y) * 0.5f,
                (r.mesh.bmin.z + r.mesh.bmax.z) * 0.5f};
    };
    auto eye_h = [&] { return room_diag(room) * 0.02f; };
    auto spawn_player = [&] {
        // The room's own spawn point: the SCT's CAST_ADOL record. Room centre is
        // only a fallback for rooms that don't place the player.
        const CastPlacement* sp = room.cast.player_spawn();
        const Vec3 c = sp ? sp->position : center(room);
        const float fl = floor_at(room.mesh, c.x, c.z, room.mesh.bmax.y + room_diag(room));
        feet = {c.x, (fl > kNoFloor ? fl : c.y), c.z};
        vy = 0; grounded = false;
        std::printf("spawn: %s(%.1f,%.1f,%.1f) floor=%s\n",
                    sp ? "CAST_ADOL" : "room centre", c.x, c.y, c.z,
                    fl > kNoFloor ? std::to_string(fl).c_str() : "none");
        if (fl <= kNoFloor && !sp) fly = true;  // no floor and no spawn -> free-fly
    };
    spawn_player();

    auto spawn_enemies = [&] {
        enemies.clear();
        efx.kinds.clear();
        efx.init(g, asset_root, room.cast);
        for (const CastPlacement& c : room.cast.casts) {
            const std::string stem = c.sprite_stem();
            if (stem.empty() || !efx.kinds.count(stem)) continue;
            enemies.push_back({stem, stem, c.position, 20, 20, true});
        }
        if (!enemies.empty())
            std::printf("  %zu enemies spawned\n", enemies.size());
    };

    spawn_enemies();

    float cooldown = 1.5f;
    auto transition = [&](const std::string& dest) {
        auto it = g.sob_index.find(dest);
        if (it == g.sob_index.end()) { std::printf("  (no SOB for %s)\n", dest.c_str()); return; }
        std::printf(">> entering %s\n", dest.c_str());
        Room next = load_room(g, asset_root, it->second, game.g_flags, character);
        free_room(room, g.white_bind);
        room = std::move(next);
        if (fly) spawn_in(cam, room); else spawn_player();
        spawn_enemies();
        cooldown = 1.5f;
    };
    // Reload the current room (re-gates chests for the new character / state).
    auto reload_room = [&] {
        auto it = g.sob_index.find(room.name);
        if (it == g.sob_index.end()) return;
        Room next = load_room(g, asset_root, it->second, game.g_flags, character);
        free_room(room, g.white_bind);
        room = std::move(next);
    };

    // Populate the room's enemies from its SCT placements.
    // The barrier fight is NOT wired up, deliberately.
    //
    // A room's KEKKAIZAKO*.XSO ("kekkai" = barrier, "zako" = mobs) is what sets the
    // progress flag a barrier gates — S_1001's grants g_flags[301], which
    // S_BOX01.XSO's second guard tests. But its real condition is sub-op 0x131(1),
    // which we do not implement:
    //
    //     0: 0x131(1)       <- the barrier-clear test  (UNIMPLEMENTED)
    //     3: class 6        jump-if-acc==0
    //     8: 0x64(301, 1)   set flag 301
    //
    // Because the VM skips unknown sub-ops, `acc` stays 0, the guard always passes,
    // and merely RUNNING the script grants the flag — no matter what the player did.
    // That is not a barrier fight, it is a free flag. 0x131 occurs 34 times across
    // exactly 4 files, all KEKKAIZAKO*, so it is precisely the check we're missing.
    // The trigger is also not "kill everything" (this barrier wants one specific
    // enemy), so guessing a trigger would compound the error.
    //
    // Wire this only once 0x131 is reverse-engineered (its handler is in the entity
    // interpreter FUN_004472e0). Until then, killing enemies does nothing.
    auto clear_barrier = [&] {};

    // Melee swing. Damage and reach are OUR numbers: the game has no per-enemy HP
    // column (stats come from a level curve we have not wired), and the player's
    // attack animation tags in c_111 are not identified yet.
    auto try_attack = [&] {
        if (atk_cd > 0) return;
        atk_cd = 0.35f;
        const float reach = 2.6f;
        int killed = 0, hit = 0;
        for (EnemyInst& e : enemies) {
            if (!e.alive) continue;
            const float dx = e.pos.x - feet.x, dz = e.pos.z - feet.z;
            const float dy = e.pos.y - feet.y;
            if (dx * dx + dz * dz > reach * reach || std::fabs(dy) > 3.0f) continue;
            e.hp -= 10; ++hit;
            if (e.hp <= 0) { e.alive = false; ++killed; }
        }
        if (!hit) return;
        if (killed) {
            int left = 0;
            for (const EnemyInst& e : enemies) if (e.alive) ++left;
            std::printf("  defeated %d (%d left)\n", killed, left);
            if (left == 0) clear_barrier();
        }
    };

    const ItemTable items_tbl = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
    auto try_interact = [&] {
        const Vec3 p = fly ? cam.pos : Vec3{feet.x, feet.y, feet.z};
        const float reach = room_diag(room) * 0.09f;
        const Interactable* target = nullptr;
        float best = reach * reach;
        for (const Interactable& it : room.mesh.interactables) {
            const float dx = it.position.x - p.x, dy = it.position.y - p.y, dz = it.position.z - p.z;
            const float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best) { best = d2; target = &it; }
        }
        if (!target) { std::printf("  (nothing to interact with here)\n"); return; }
        if (target->script.empty()) { std::printf("  %s: no script\n", target->name.c_str()); return; }
        try {
            Xso x = Xso::parse(read_bytes(target->script));
            game.reset_run();
            game.run(x);
            // Prefer the game's acquire-box items (0xD5); fall back to give-item.
            const std::vector<int>& got = !game.acquires.empty() ? game.acquires : game.items;
            if (got.empty()) {
                // No item can mean two different things, and they are NOT the same:
                // the chest's opened-flag is set (truly already looted), or the script
                // hit a later prerequisite guard and ended early. S_1001's box_01, for
                // instance, tests a second flag (301) that some event must set first —
                // reporting that as "already opened" is simply wrong.
                const bool opened = target->opened_flag >= 0 &&
                                    game.g_flags[target->opened_flag] != 0;
                hud.message(std::string(target->name) +
                            (opened ? ": already opened" : ": locked (needs an event first)"));
                std::printf("  %s: no item — opened_flag %d = %d -> %s\n",
                            target->name.c_str(), target->opened_flag,
                            target->opened_flag >= 0 ? game.g_flags[target->opened_flag] : -1,
                            opened ? "already opened" : "gated by a later guard");
            } else {
                std::string msg = "Acquired";
                for (int it : got) { msg += " "; msg += items_tbl.name(it); }
                hud.message(msg);
                std::printf("  >> %s\n", msg.c_str());
            }
        } catch (const std::exception& e) {
            std::printf("  %s: script error: %s\n", target->name.c_str(), e.what());
        }
    };

    std::printf("\nPLAY: WASD move Yunica (8-way, fixed camera), Space jump, E interact, Tab character | F free-fly debug | wheel speed | R respawn | 1-9 exit | ESC quit\n");
    bool running = true, looking = false;
    Uint64 last = SDL_GetTicks();
    while (running) {
        const Uint64 now = SDL_GetTicks();
        float dt = float(now - last) / 1000.0f; last = now;
        if (dt > 0.1f) dt = 0.1f;
        if (cooldown > 0) cooldown -= dt;
        if (atk_cd > 0) atk_cd -= dt;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode k = e.key.key;
                if (k == SDLK_ESCAPE) running = false;
                else if (k == SDLK_F) {
                    fly = !fly;
                    if (!fly) {  // dropping into walk: keep XZ, snap to floor
                        feet = {cam.pos.x, cam.pos.y, cam.pos.z};
                        const float fl = floor_at(room.mesh, feet.x, feet.z, feet.y + room_diag(room));
                        if (fl > kNoFloor) feet.y = fl;
                        vy = 0;
                    }
                    std::printf("mode: %s\n", fly ? "fly" : "walk");
                }
                else if (k == SDLK_R) { if (fly) spawn_in(cam, room); else spawn_player(); }
                else if (k == SDLK_TAB) {
                    character = character % 3 + 1;
                    game.g_flags[150] = character;
                    hud.message(std::string("Character: ") + kCharNames[character]);
                    chr.load(g, asset_root / "ANI" / char_aia(character), 0);  // swap sprite
                    curAnim = std::uint32_t(std::max(0, chr.find_tag(kTagIdle)));
                    animFrame = 0; shownFrame = -1; animT = 0;
                    reload_room();  // re-gate this room's chests for the new character
                }
                else if (k == SDLK_E && !fly) try_interact();
                else if (k >= SDLK_1 && k <= SDLK_9) {
                    std::size_t idx = std::size_t(k - SDLK_1);
                    if (idx < room.sen.triggers.size())
                        transition(room.sen.triggers[idx].dest_scene);
                }
            } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (fly) looking = true; else try_attack();   // play mode: LMB = attack
            }
            else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) looking = false;
            else if (e.type == SDL_EVENT_MOUSE_MOTION && looking) {
                cam.yaw -= e.motion.xrel * 0.004f;  // LH world: mouse-right = turn right
                cam.pitch -= e.motion.yrel * 0.004f;
                cam.pitch = std::max(-1.5f, std::min(1.5f, cam.pitch));
            } else if (e.type == SDL_EVENT_MOUSE_WHEEL) {
                speed_mult *= (e.wheel.y > 0) ? 1.15f : 0.87f;
                speed_mult = std::max(0.1f, std::min(8.0f, speed_mult));
            } else if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                cw = std::uint32_t(e.window.data1); ch = std::uint32_t(e.window.data2);
                if (cw && ch) { configure(); wgpuTextureViewRelease(depth); depth = make_depth(g.device, cw, ch); }
            }
        }

        const bool* ks = SDL_GetKeyboardState(nullptr);
        const float D = room_diag(room);
        if (fly) {
            const Vec3 f = cam.forward();
            const Vec3 right = normalize(cross({0, 1, 0}, f));
            const float step = D * 0.35f * speed_mult * dt;
            auto add = [&](const Vec3& d, float s) { cam.pos.x += d.x*s; cam.pos.y += d.y*s; cam.pos.z += d.z*s; };
            if (ks[SDL_SCANCODE_W]) add(f, step);
            if (ks[SDL_SCANCODE_S]) add(f, -step);
            if (ks[SDL_SCANCODE_D]) add(right, step);
            if (ks[SDL_SCANCODE_A]) add(right, -step);
            if (ks[SDL_SCANCODE_E] || ks[SDL_SCANCODE_SPACE]) cam.pos.y += step;
            if (ks[SDL_SCANCODE_Q] || ks[SDL_SCANCODE_LCTRL]) cam.pos.y -= step;
        } else {
            // Play mode: WASD moves the CHARACTER relative to the fixed camera; the
            // camera follows to keep her centred. Movement basis = the camera's
            // horizontal forward (into screen) and right.
            const float eye = kCharH, radius = kCharRadius, gravity = kGravity;
            const float walk = kWalkSpeed * speed_mult, stepUp = kStepUp;
            // Movement is relative to the live camera (last frame's — the 1-frame lag
            // is imperceptible and avoids a chicken-and-egg with the follow camera).
            const Vec3 fwd = normalize({std::cos(cam.yaw), 0, std::sin(cam.yaw)});  // screen-up
            const Vec3 rt = normalize(cross({0, 1, 0}, fwd));                       // screen-right
            Vec3 wish{0, 0, 0};
            if (ks[SDL_SCANCODE_W]) { wish.x += fwd.x; wish.z += fwd.z; }
            if (ks[SDL_SCANCODE_S]) { wish.x -= fwd.x; wish.z -= fwd.z; }
            if (ks[SDL_SCANCODE_D]) { wish.x += rt.x; wish.z += rt.z; }
            if (ks[SDL_SCANCODE_A]) { wish.x -= rt.x; wish.z -= rt.z; }
            const float wl = std::sqrt(wish.x * wish.x + wish.z * wish.z);
            const bool moving = wl > 1e-4f;
            if (moving) { feet.x += wish.x / wl * walk * dt; feet.z += wish.z / wl * walk * dt; }
            { Vec3 body = {feet.x, feet.y + stepUp, feet.z};
              body = resolve_walls(room.mesh, body, radius, std::max(eye - stepUp, 0.5f));
              feet.x = body.x; feet.z = body.z; }
            vy -= gravity * dt; feet.y += vy * dt;
            const float fl = floor_at(room.mesh, feet.x, feet.z, feet.y + stepUp);
            if (fl > kNoFloor && vy <= 0.0f && fl >= feet.y - stepUp) {
                feet.y = fl; vy = 0; grounded = true;
            } else grounded = false;
            if (grounded && ks[SDL_SCANCODE_SPACE]) { vy = std::sqrt(2 * gravity * kJumpH); grounded = false; }
            if (feet.y < room.mesh.bmin.y - D) spawn_player();

            // Facing: map the screen-relative heading to one of the 8 sprite dirs.
            // (dir7 faces the camera; ring runs dir0=SW,1=W,2=NW,3=N,4=NE,5=E,6=SE,7=S.)
            if (moving) {
                const float sUp = (wish.x * fwd.x + wish.z * fwd.z) / wl;
                const float sRt = (wish.x * rt.x + wish.z * rt.z) / wl;
                const float deg = std::atan2(sUp, sRt) * 57.29578f;
                // Screen heading -> sprite facing, calibrated from playtest against
                // the C_10x ring (dir0=E, dir1=NE, dir2=N/back, ... 45 deg CCW per step):
                // up->dir2(back), right->dir0, down->dir6(front), left->dir4.
                facing = int(std::lround((deg + 360.0f) / 45.0f)) & 7;
            }
            // Animation state: airborne -> jump (plays once, holds its last frame),
            // else walk while moving, else idle. Only re-decode when the frame changes.
            std::uint32_t wantTag; float rate; bool loop = true;
            if (!grounded)   { wantTag = kTagJump; rate = 0.07f; loop = false; }
            else if (moving) { wantTag = kTagWalk; rate = 0.09f; }
            else             { wantTag = kTagIdle; rate = 0.14f; }  // idle breathes slower
            const int wantImg = chr.find_tag(wantTag);
            if (wantImg >= 0 && std::uint32_t(wantImg) != curAnim) {
                curAnim = std::uint32_t(wantImg); animFrame = 0; animT = 0; shownFrame = -1;
            }
            const int nf = int(std::max(1u, chr.frames_of(curAnim)));
            animT += dt;
            if (animT >= rate) {
                animT = 0;
                if (loop) animFrame = (animFrame + 1) % nf;
                else if (animFrame < nf - 1) ++animFrame;  // hold the last jump frame
            }
            if (animFrame != shownFrame) { chr.set_anim(g, curAnim, animFrame); shownFrame = animFrame; }

            // The room's authored camera (SCM): region-selected, follow + orbit, blended.
            update_camera(room, feet, dt);
            if (g_cam.on) apply_cam_state(cam, feet);
        }

        // SEN trigger: move into a doorway volume to warp.
        if (cooldown <= 0) {
            const Vec3 probe = fly ? cam.pos : Vec3{feet.x, feet.y + kCharH * 0.5f, feet.z};
            for (const SenTrigger& t : room.sen.triggers)
                if (t.contains(probe) || t.contains(cam.pos)) { transition(t.dest_scene); break; }
        }

        WGPUSurfaceTexture st = {};
        wgpuSurfaceGetCurrentTexture(surface, &st);
        if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) { configure(); continue; }
        WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
        const float rad = room_diag(room);
        update_fog(room, fly ? cam.pos : feet, dt);
        write_mvp(g, cam.view_proj(float(cw) / float(ch), rad * 0.01f, rad * 8.0f));
        draw(g, room, view, depth);
        // Character billboard (play mode only; fly is a free debug camera).
        if (!fly && facing >= 0 && facing < 8 && chr.dirs[facing].tex)
            draw_character(g, view, depth, feet, kCharH, chr.dirs[facing], cam.forward());
        draw_enemies(g, view, depth, efx, enemies, cam.forward());
        fx_time += dt;
        draw_flames(g, room, view, depth, flames, cam.forward(), fx_time);
        player.character = character;
        hud.render(g, view, cw, ch, room.name + "  (" + kCharNames[character] + ")", dt, &player);
        wgpuSurfacePresent(surface);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(st.texture);
    }
    SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}

static void build_indexes(Gpu& g, const fs::path& root) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec || !it->is_regular_file()) continue;
        const fs::path& p = it->path();
        std::string ext = p.extension().string();
        for (char& c : ext) c = char(std::tolower((unsigned char)c));
        if (ext == ".dds") {
            std::string base = p.filename().string();
            for (char& c : base) c = char(std::tolower((unsigned char)c));
            const bool high = p.parent_path().filename() == "H";
            auto f = g.dds_index.find(base);
            if (f == g.dds_index.end() || high) g.dds_index[base] = p;
        } else if (ext == ".sob") {
            std::string stem = p.stem().string();
            for (char& c : stem) c = char(std::toupper((unsigned char)c));
            g.sob_index.emplace(stem, p);
        }
    }
    std::printf("indexed %zu textures, %zu rooms\n", g.dds_index.size(), g.sob_index.size());
}

// Locate the extracted-asset tree when not given on the command line:
// $CLERIA_ASSETS, then <exe>/../../assets and <exe>/assets, then the repo path.
static fs::path default_assets(const char* argv0) {
    std::error_code ec;
    if (const char* e = std::getenv("CLERIA_ASSETS"))
        if (fs::exists(fs::path(e) / "MAP", ec)) return e;
    const fs::path exedir = fs::absolute(fs::path(argv0), ec).parent_path();
    for (const fs::path& c : {exedir / ".." / ".." / "assets", exedir / "assets",
                              fs::path("C:/Users/kores/repos/YsOrigin-CleriaCore/assets")})
        if (fs::exists(c / "MAP", ec)) return fs::weakly_canonical(c, ec);
    return "C:/Users/kores/repos/YsOrigin-CleriaCore/assets";
}

int main(int argc, char** argv) {
    // No args -> free-fly the tower base (S_1000) with default assets.
    const fs::path asset_root = (argc >= 2) ? fs::path(argv[1]) : default_assets(argv[0]);
    const bool shot = argc >= 4 && std::strcmp(argv[3], "--shot") == 0;
    const bool walk = argc >= 4 && std::strcmp(argv[3], "--walk") == 0;
    const bool fps = argc >= 4 && std::strcmp(argv[3], "--fps") == 0;
    const bool play = argc >= 4 && std::strcmp(argv[3], "--play") == 0;

    // Headless chest test: run every chest script in a room (no GPU needed).
    if (argc >= 4 && std::strcmp(argv[3], "--chests") == 0) {
        const fs::path room_sob = argv[2];
        RoomMesh m = build_room(asset_root, room_sob);
        const ItemTable items = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
        XsoVm game;
        std::printf("%s: %zu chest(s)\n", room_sob.filename().string().c_str(),
                    m.interactables.size());
        for (const Interactable& it : m.interactables) {
            if (it.script.empty()) { std::printf("  %s: no script\n", it.name.c_str()); continue; }
            game.reset_run();
            game.run(Xso::parse(read_bytes(it.script)));
            std::printf("  %s (%s):", it.name.c_str(), fs::path(it.script).filename().string().c_str());
            if (game.items.empty()) std::printf(" (no item on this path)");
            for (int id : game.items) std::printf(" [%s]", items.name(id).c_str());
            std::printf("\n");
        }
        return 0;
    }

    Gpu g;
    g.instance = wgpuCreateInstance(nullptr);
    AdapterReq ar;
    WGPURequestAdapterOptions ao = {}; ao.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo aci = {};
    aci.mode = WGPUCallbackMode_AllowProcessEvents; aci.callback = on_adapter; aci.userdata1 = &ar;
    wgpuInstanceRequestAdapter(g.instance, &ao, aci);
    for (int i = 0; !ar.done && i < 100; ++i) wgpuInstanceProcessEvents(g.instance);
    if (!ar.adapter) { std::fprintf(stderr, "no adapter\n"); return 1; }
    g.adapter = ar.adapter;

    DeviceReq dr;
    WGPUFeatureName feats[1] = {WGPUFeatureName_TextureCompressionBC};
    WGPUDeviceDescriptor dd = {}; dd.requiredFeatureCount = 1; dd.requiredFeatures = feats;
    WGPURequestDeviceCallbackInfo dci = {};
    dci.mode = WGPUCallbackMode_AllowProcessEvents; dci.callback = on_device; dci.userdata1 = &dr;
    wgpuAdapterRequestDevice(g.adapter, &dd, dci);
    for (int i = 0; !dr.done && i < 100; ++i) wgpuInstanceProcessEvents(g.instance);
    if (!dr.device) { std::fprintf(stderr, "no device\n"); return 1; }
    g.device = dr.device;
    g.queue = wgpuDeviceGetQueue(g.device);
    { WGPUAdapterInfo info = {}; wgpuAdapterGetInfo(g.adapter, &info);
      std::printf("adapter: %.*s\n", int(info.device.length), info.device.data); }

    build_indexes(g, asset_root);

    // Default start room = S_1000 (tower base) when no SOB given.
    fs::path sob_path;
    if (argc >= 3) {
        sob_path = argv[2];
    } else {
        auto it = g.sob_index.find("S_1000");
        if (it == g.sob_index.end()) {
            std::fprintf(stderr, "no default room S_1000 under %s\n",
                         asset_root.string().c_str());
            return 1;
        }
        sob_path = it->second;
    }
    std::printf("assets: %s\nstart:  %s\n",
                asset_root.string().c_str(), sob_path.string().c_str());

    if (shot) {
        // shot mode builds its own pipeline (RGBA target) then loads one room
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        Room room = load_room(g, asset_root, sob_path);
        std::uint32_t W = argc >= 6 ? std::uint32_t(std::atoi(argv[5])) : 1280;
        std::uint32_t H = argc >= 7 ? std::uint32_t(std::atoi(argv[6])) : 860;
        float yaw   = argc >= 8 ? float(std::atof(argv[7])) : -0.7f;
        float pitch = argc >= 9 ? float(std::atof(argv[8])) : 0.82f;
        float dist  = argc >= 10 ? float(std::atof(argv[9])) : 1.2f;
        return render_shot(g, room, argv[4], W, H, yaw, pitch, dist);
    }
    if (argc >= 4 && std::strcmp(argv[3], "--model") == 0) {
        // cleria-view <root> <anything> --model <file.ymo> out.ppm [W H yaw pitch dist]
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        g_fov = 1.0472f;                       // a plain lens for inspection
        Room m = load_model(g, asset_root, argv[4]);
        const char* out = argc >= 6 ? argv[5] : "model.ppm";
        std::uint32_t W = argc >= 7 ? std::uint32_t(std::atoi(argv[6])) : 900;
        std::uint32_t H = argc >= 8 ? std::uint32_t(std::atoi(argv[7])) : 900;
        float yaw   = argc >= 9  ? float(std::atof(argv[8]))  : -0.7f;
        float pitch = argc >= 10 ? float(std::atof(argv[9]))  : 0.25f;
        float dist  = argc >= 11 ? float(std::atof(argv[10])) : 0.9f;
        return render_shot(g, m, out, W, H, yaw, pitch, dist);
    }
    if (fps) {
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        Room room = load_room(g, asset_root, sob_path);
        std::uint32_t W = argc >= 6 ? std::uint32_t(std::atoi(argv[5])) : 1280;
        std::uint32_t H = argc >= 7 ? std::uint32_t(std::atoi(argv[6])) : 720;
        float yaw = argc >= 8 ? float(std::atof(argv[7])) : 1.0f;
        return render_fps(g, room, argv[4], W, H, yaw, asset_root);
    }
    if (play) {
        // cleria-view <root> <sob> --play out.ppm [W H yaw pitch distFrac heightFrac dir]
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        Room room = load_room(g, asset_root, sob_path);
        std::uint32_t W = argc >= 6 ? std::uint32_t(std::atoi(argv[5])) : 1280;
        std::uint32_t H = argc >= 7 ? std::uint32_t(std::atoi(argv[6])) : 720;
        float yaw   = argc >= 8  ? float(std::atof(argv[7]))  : 1.5708f;
        float pitch = argc >= 9  ? float(std::atof(argv[8]))  : -0.72f;
        float dist  = argc >= 10 ? float(std::atof(argv[9]))  : 28.8f;   // world units
        float hgt   = argc >= 11 ? float(std::atof(argv[10])) : 2.0f;    // world units
        std::uint32_t dir = argc >= 12 ? std::uint32_t(std::atoi(argv[11])) : 2;
        if (argc >= 13) g_fov = float(std::atof(argv[12]));
        return render_play(g, room, argv[4], W, H, asset_root, yaw, pitch, dist, hgt, dir);
    }

    if (walk) {
        // Headless tour: follow exit[0] room-to-room, shooting each. Exercises
        // the load -> free -> load transition path and traces a tower path.
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        const int steps = argc >= 5 ? std::atoi(argv[4]) : 6;
        const std::string prefix = argc >= 6 ? argv[5] : "walk";
        Room room = load_room(g, asset_root, sob_path);
        std::vector<std::string> visited;
        for (int i = 0; i < steps; ++i) {
            char out[256];
            std::snprintf(out, sizeof(out), "%s_%02d_%s.ppm", prefix.c_str(), i, room.name.c_str());
            render_shot(g, room, out, 720, 480, -0.7f, 0.85f, 1.15f);
            visited.push_back(room.name);
            if (room.sen.triggers.empty()) { std::printf("dead end at %s\n", room.name.c_str()); break; }
            // pick the first exit that leads somewhere new, else the first exit.
            std::string dest = room.sen.triggers[0].dest_scene;
            for (const SenTrigger& t : room.sen.triggers)
                if (std::find(visited.begin(), visited.end(), t.dest_scene) == visited.end()) { dest = t.dest_scene; break; }
            auto it = g.sob_index.find(dest);
            if (it == g.sob_index.end()) { std::printf("no SOB for %s\n", dest.c_str()); break; }
            Room next = load_room(g, asset_root, it->second);
            free_room(room, g.white_bind);
            room = std::move(next);
        }
        std::printf("walked %zu rooms\n", visited.size());
        return 0;
    }

    // Window mode: render_window builds the pipeline (needs the surface format),
    // then loads the first room (needs the pipeline's white_bind/tex_layout).
    return render_window(g, asset_root, sob_path);
}
