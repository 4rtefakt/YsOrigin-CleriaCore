// cleria-view: real-time WebGPU renderer + free-fly walkthrough of Ys Origin.
//
//   cleria-view <asset_root> <room.SOB>              free-fly window
//   cleria-view <asset_root> <room.SOB> --shot out.ppm [W H yaw pitch dist]
//
// In the window: WASD/QE fly, hold LMB to look, wheel = speed, R = recenter,
// ESC = quit. Fly into a doorway (a SEN trigger volume) to warp to the connected
// room; number keys 1-9 take exit N directly.
#include <functional>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unordered_map>
#include <array>
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
#include "cleria/aso.hpp"
#include "cleria/ef2.hpp"
#include "cleria/dds.hpp"
#include "cleria/mathx.hpp"
#include "cleria/cast.hpp"
#include "cleria/gameinfo.hpp"
#include "cleria/eex.hpp"
#include "cleria/enemies.hpp"
#include "cleria/combat.hpp"
#include "cleria/physics.hpp"
#include "cleria/physics_player.hpp"
#include "cleria/surface.hpp"
#include "cleria/audio.hpp"
#include "cleria/bgmdef.hpp"
#include "cleria/scenelist.hpp"
#include "cleria/items.hpp"
#include "cleria/scene.hpp"
#include "cleria/sev.hpp"
#include "cleria/scm.hpp"
#include "cleria/sef.hpp"
#include "cleria/sct.hpp"
#include "cleria/sen.hpp"
#include "cleria/sfo.hpp"
#include "cleria/world.hpp"
#include "cleria/xso.hpp"
// CLERIA_CULL=off|cw|ccw — probe. The exe sets D3DCULL_CCW, but D3D9's left-handed
// winding inverts on the way to WebGPU, so its semantics don't transplant directly:
// measured in S_1000, "ccw" changes no visible pixel and "cw" culls the whole room,
// i.e. every visible triangle here is wound the same way. Default stays None until
// the per-material no-cull flag (0x400, 543/6642 materials) is wired.
// ---- the logic tick ---------------------------------------------------------
// The game is FIXED-STEP: 60 ticks per second of SCALED time, decoupled from
// rendering (RefreshRate only governs Present). Everything the ASO animation VM
// measures is in ticks — `anime_Wait(n)` is n ticks, i.e. n/60 s at timeScale 1.
constexpr int kTickHz = 60;
constexpr float kTickDt = 1.0f / float(kTickHz);
// The exe writes timeScale at 4 sites: 1.0 (0x431b62, 0x521fcc), 0.875 (0x5216a8)
// and 0.8 (0x53a95c) — real slow-mo, i.e. 52.5 / 48 ticks per REAL second. We only
// ever run 1.0 so far; the accumulator is already shaped to honour the others.
static float g_time_scale = 1.0f;
// Set after a load so the next frame runs one tick instead of catching up on the
// stall — the exe does this via DAT_0075aed5.
static bool g_tick_reset = false;

static WGPUFrontFace g_front = WGPUFrontFace_CW;
static WGPUCullMode  g_cull  = WGPUCullMode_None;
static void init_cull_env() {
    const char* c = std::getenv("CLERIA_CULL");
    if (!c) return;
    std::string v(c);
    if (v == "off")      { g_cull = WGPUCullMode_None; }
    else if (v == "cw")  { g_front = WGPUFrontFace_CCW; g_cull = WGPUCullMode_Back; }
    else if (v == "ccw") { g_front = WGPUFrontFace_CW;  g_cull = WGPUCullMode_Back; }
}

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
    int sp = 59;   // placeholder until the stat model lands
    float exp = 0;          // running EXP total (build/progress_re/LEVELING.md)
    float exp_pct = 0.0f;   // progress toward the next level, for the HUD bar
    float mp_pct = 100.0f;
    int character = 1;  // portrait: 1 Yunica / 2 Hugo / 3 Toal
    // Red HP damage-lag "ghost" bar (build/hud_re/HUD_HP_BARS.md, H3). The HP widget is
    // {cur, lag, max}: on damage the lag holds the old value and decays toward cur, drawing
    // the red trail in the [cur, lag] segment. Decay = 0.2 * coeff * max per tick; the coeff
    // (widget+0x2c) is a static-init [UNKNOWN] (~1), so the RATE is env-tunable (CLERIA_HPLAG);
    // the MODEL (linear catch-up) is [C-DIS]. mutable: a render-side catch-up cache.
    mutable float hp_lag = 400;
    // EX / Boost gauge (build/ability_re/BOOST.md, build/hud_re/HUD_BOOST_ARC.md). 0..30000,
    // full = Boost-ready. Builds on combat hits (dealt + taken); drawn as the code-traced C-arc
    // around the portrait. Exact per-hit build amount is [H] (BOOST.md's hook is missing-HP-scaled).
    float boost = 0;
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

// Character/enemy sprites: ALPHA-BLEND, not the room's hard 0.5 alpha-test. The AIA
// sprites carry genuine partial alpha (anti-aliased edges, translucent detail); cutting
// it at 0.5 punched holes in the player and read as "half transparent". Discard only
// the fully-transparent texels and hand the real alpha to the blender.
@fragment
fn fs_char(in : VsOut) -> @location(0) vec4<f32> {
  let tc = textureSample(tex, smp, in.uv);
  if (tc.a < 0.004) { discard; }
  var c = tc.rgb * in.color.rgb;
  if (u.fogParams.z > 0.5) {
    let f = clamp((u.fogParams.y - in.depth) / max(u.fogParams.y - u.fogParams.x, 0.001),
                  0.0, 1.0);
    c = mix(u.fogColor.rgb, c, f);
  }
  // Hit-flash: the vertex-colour alpha carries a white-tint amount (0 normally). The
  // room shader (fs) never reads color.a, so this channel is free for characters.
  c = mix(c, vec3<f32>(1.0, 1.0, 1.0), in.color.a);
  return vec4<f32>(c, tc.a);
}

// Floor shadow decal: pure black at the texture's alpha, NO fog (fs_char's fog would
// tint the shadow toward the fog colour and it would blend invisibly over the floor).
@fragment
fn fs_shadow(in : VsOut) -> @location(0) vec4<f32> {
  let a = textureSample(tex, smp, in.uv).a;
  if (a < 0.004) { discard; }
  return vec4<f32>(0.0, 0.0, 0.0, a);
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
    WGPURenderPipeline char_pipeline{}; // alpha-blended character/enemy sprites
    WGPURenderPipeline char_pipeline_add{}; // additive variant, for glow overlays (Topic selection wash)
    WGPURenderPipeline shadow_pipeline{}; // floor shadow decals (alpha-over, no depth write)
    WGPUBuffer ubuf{};
    WGPUBindGroup ubind{};
    WGPUSampler sampler{};
    WGPUBindGroupLayout tex_layout{};
    WGPUBindGroup white_bind{};   // failed-to-load texture (missing-ref marker)
    WGPUBindGroup black_bind{};   // genuinely untextured material (renders its dark diffuse)
    std::unordered_map<std::string, fs::path> dds_index;  // basename(lower) -> path
    std::unordered_map<std::string, fs::path> sob_index;  // stem(upper) -> path
};

// Per-room GPU objects (rebuilt on every transition).
// One scrolling material's live state: which vertices it owns, their authored UVs,
// and the accumulated phase. The engine keeps the phase on the material's render
// struct; we keep it per batch, which is the same thing for our batching.
struct ScrollBatch {
    std::uint32_t first_vert = 0, vert_count = 0;
    float vel_u = 0, vel_v = 0;
    float phase_u = 0, phase_v = 0;
};

struct Room {
    RoomMesh mesh;
    std::vector<ScrollBatch> scrolls;  // materials with flag 0x4 / nonzero velocity
    RoomMesh coll;           // the `_.YMO` collision hulls — what physics collides against
    Sen sen;
    Sfo fog;                 // per-region scene fog (SFO sidecar)
    Scm cam;                 // per-region cameras (SCM sidecar)
    Sef fx;                  // scene effects: torch flames etc (SEF sidecar)
    Sct cast;                // actor placement: enemies + player spawn (SCT sidecar)
    std::vector<SceneBox> event_boxes;  // named trigger boxes (.SEV) — op 0x13d Chr_IsInEventBox
    fs::path dir;            // the room folder (its scripts live beside the SOB)
    std::string name;
    WGPUBuffer vbuf{}, ibuf{};
    std::vector<WGPUTexture> textures;
    std::vector<WGPUTextureView> views;
    std::vector<WGPUBindGroup> binds;  // parallel to mesh.textures
};

// Play the footstep for the surface of `tri` on the collision mesh: material name
// -> surface (by name) -> EFFECTDEF id, variant = step_n & mask.
//
// A name that matches nothing resolves to `stone`, not to silence — the exe clamps
// an unmatched lookup to 1 (see surface.hpp). That covers "default", the second most
// common collision material. Silence is driven by base_sound_id == 0 (`none`,
// `stopper`, `reserve20`) instead. CLERIA_STEPS=1 traces each step.
// Returns the TICKS to wait before the next step, or 0 to re-check next tick. A
// silent surface returns BEFORE the reload in the exe, so its timer stays 0 and the
// check re-runs every tick — don't "reload anyway".
static int play_footstep(Audio& audio, const RoomMesh& m, int tri, unsigned step_n,
                         bool running, std::uint32_t jitter_roll) {
    const std::string_view surf = surface_of(m, tri);
    if (surf.empty()) return 0;
    const int idx = surface_index_or_default(surf);
    const int id = footstep_sound(idx, step_n);
    if (std::getenv("CLERIA_STEPS")) {
        const EffectDef* d = id >= 0 ? audio.defs().find(id) : nullptr;
        std::printf("  step %-10.*s surface=%-3d%s id=%-4d %-16s %s\n",
                    int(surf.size()), surf.data(), idx,
                    surface_index(surf) < 0 ? " (defaulted)" : "", id,
                    d ? d->name.c_str() : "(silent)", running ? "[run]" : "[walk]");
    }
    if (id < 0) return 0;  // silent surface: no reload
    audio.play(id, 0.55f);
    return footstep_interval(idx, running, jitter_roll);
}

// System SFX, from the `sys\` family at the head of EFFECTDEF.TXT (ids 0..19).
// The Japanese comment column names each one, so these three need no guessing:
// s_move is カーソル移動 ("cursor move") and s_ok is 決定 ("confirm").
constexpr int kSfxConfirm = 0;  // sys\s_ok
constexpr int kSfxCancel = 1;   // sys\s_cancel
constexpr int kSfxBoo = 2;      // sys\s_boo — the "you can't do that" buzz
constexpr int kSfxCursor = 3;   // sys\s_move
constexpr float kSfxMenuVol = 0.8f;
constexpr float kSfxAnimVol = 0.8f;  // anime_SePlay3D events (swings, impacts, voices)

// Damage-path sounds the engine plays from C rather than from animation data
// (build/audio_re/SFX_TRIGGERS.md §4.3).
constexpr int kSfxDeathEntry = 92;   // player\p_hit0 — FUN_004780e0, ANY actor whose HP hits 0
constexpr int kSfxPlayerHurt = 94;   // player\p_damage — the id C_0x0.ASO sections 4/11 emit

// The sounds an enemy's OWN death animation emits: ASO section 5 (the death motion
// state), funcid 0x2a. Enemies do not run an ASO VM in CleriaCore — they loop a fixed
// idle animation — so rather than invent an id, run the death section of the real
// script once per enemy kind and cache whatever it asks for. Verified against the
// scripts: M_100/M_130 -> 438 + 449, M_430 -> 439 + 449, M_910 -> 503 + 449, where
// 449 (zako\z_dead11) is the death sound shared by 71 of the 86 M_*.ASO.
static const std::vector<int>& enemy_death_sounds(const fs::path& asset_root,
                                                  const std::string& stem) {
    static std::unordered_map<std::string, std::vector<int>> cache;
    const auto it = cache.find(stem);
    if (it != cache.end()) return it->second;
    std::vector<int>& out = cache[stem];
    if (!stem.empty()) {
        Aso a = Aso::load(asset_root / "ANI" / (stem + ".ASO"));
        AsoVm vm;
        if (a.ok() && (vm.bind(&a), vm.set_section(5))) {
            for (int t = 0; t < 60 && out.size() < 4; ++t) {
                vm.tick();
                for (const AsoVm::SoundEvent& se : vm.sounds())
                    if (std::find(out.begin(), out.end(), se.id) == out.end()) out.push_back(se.id);
            }
        }
    }
    return out;
}

// What physics collides against. The game collides against the `_.YMO` hulls, not
// the render mesh; fall back to the render mesh only if a room ships no twins.
// CLERIA_COLL=render forces the old behaviour, for A/B.
static const RoomMesh& phys_mesh(const Room& r) {
    static const bool force_render = [] {
        const char* e = std::getenv("CLERIA_COLL");
        return e && std::string(e) == "render";
    }();
    return (!force_render && !r.coll.indices.empty()) ? r.coll : r.mesh;
}

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
    // The room's named trigger boxes, for op 0x13d Chr_IsInEventBox (region-talk triggers).
    // parse_sev returns empty until the .SEV record layout is confirmed, so this is inert now.
    r.event_boxes = load_sev(r.dir / (r.name + ".SEV"));

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
    r.coll = build_collision(asset_root, sob, gates_box ? &box_whitelist : nullptr,
                             hidden.empty() ? nullptr : &hidden);
    // Materials that scroll their UVs. Only 397 of 6642 corpus-wide do, and the disk
    // phase is 0.0 in every one — it is pure runtime state, so we start at zero too.
    for (const DrawBatch& b : r.mesh.batches)
        if ((b.scroll_u != 0 || b.scroll_v != 0) && b.vert_count)
            r.scrolls.push_back({b.first_vert, b.vert_count, b.scroll_u, b.scroll_v, 0, 0});
    if (!r.scrolls.empty()) {
        std::printf("    uv scroll: %zu batches", r.scrolls.size());
        for (const ScrollBatch& sb : r.scrolls)
            std::printf("  [%u verts %+.4f,%+.4f/tick]", sb.vert_count, sb.vel_u, sb.vel_v);
        std::printf("\n");
    }
    // CopyDst: the UV-scroll integrator rewrites scrolling materials' vertices in
    // place each frame via wgpuQueueWriteBuffer, which requires it.
    r.vbuf = make_buffer(g.device, g.queue, r.mesh.vertices.data(),
                         r.mesh.vertices.size() * sizeof(RoomVertex),
                         WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    r.ibuf = make_buffer(g.device, g.queue, r.mesh.indices.data(),
                         r.mesh.indices.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);

    int loaded = 0;
    r.binds.reserve(r.mesh.textures.size());
    // mesh.textures are already RESOLVED absolute paths — scene.cpp resolves each
    // ref against its own YMO's directory. Do not reduce to a basename and look it
    // up game-wide: 532 basenames are ambiguous and 530 of those differ in content.
    for (const std::string& path : r.mesh.textures) {
        Dds dds = load_dds(path);
        if (dds.ok()) { r.binds.push_back(tex_bindgroup(g, upload_texture(g, r, dds))); ++loaded; }
        else {
            std::printf("    [tex] FAILED to load: %s\n", path.c_str());
            r.binds.push_back(g.white_bind);
        }
    }
    // Untextured submeshes (no texture ref) are a non-rendering DUPLICATE/helper layer
    // sitting ON the real textured floor. S_1000's platform ring carries "stone" (grey)
    // and "none" (black) materials with no texture over the same tiles that also have a
    // textured version; drawing them as flat quads paints a grey/black patch over the
    // stone — the "white staircase" was exactly this (via the old 1x1-white fallback).
    // The retail game doesn't draw them: skipping reveals the clean textured geometry
    // underneath with NO gaps (verified in S_1000, whole floor intact). So we skip any
    // batch with no texture rather than inventing a fill colour for it.
    for (DrawBatch& b : r.mesh.batches)
        if (b.texture < 0) b.index_count = 0;

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
    if (std::getenv("CLERIA_CAM")) {
        // The game's OWN authored eye distances. Useful as a scale check: dist over
        // the player's height says how many character-heights the camera sits back.
        for (std::size_t i = 0; i < r.cam.regions.size(); ++i) {
            const CameraRegion& cr = r.cam.regions[i];
            std::printf("    [cam %zu] eye.mode=%u dist=%.2f pitch=%.3f fov=%.3f\n", i,
                        cr.eye.mode, cr.eye.dist, cr.eye.pitch, cr.fov);
        }
    }
            std::printf("    %zu effects (%d torch flames), %zu cameras\n",
                        r.fx.effects.size(), nf, r.cam.regions.size());
    }
    // CLERIA_COLLCHECK=1: sample a grid of floor heights and compare the collision
    // hulls against the render mesh, so gaps in coverage are visible as a number
    // rather than discovered by falling through a floor in play.
    if (std::getenv("CLERIA_COLLCHECK") && !r.coll.indices.empty()) {
        const int N = 48;
        const float ceil = r.mesh.bmax.y + 1e4f;
        int both = 0, only_render = 0, only_coll = 0, neither = 0;
        double sum_abs = 0.0, worst = 0.0;
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                const float x = r.mesh.bmin.x + (r.mesh.bmax.x - r.mesh.bmin.x) * (i + 0.5f) / N;
                const float z = r.mesh.bmin.z + (r.mesh.bmax.z - r.mesh.bmin.z) * (j + 0.5f) / N;
                const float fr = floor_at(r.mesh, x, z, ceil);
                const float fc = floor_at(r.coll, x, z, ceil);
                const bool hr = fr != kNoFloor, hc = fc != kNoFloor;
                if (hr && hc) {
                    ++both;
                    const double d = std::fabs(double(fr) - double(fc));
                    sum_abs += d; worst = std::max(worst, d);
                } else if (hr) ++only_render;
                else if (hc) ++only_coll;
                else ++neither;
            }
        std::printf("    COLLCHECK %-8s both=%4d only_render=%4d only_coll=%4d none=%4d "
                    "mean|dy|=%.3f worst=%.3f\n",
                    r.name.c_str(), both, only_render, only_coll, neither,
                    both ? sum_abs / both : 0.0, worst);
    }
    if (r.coll.indices.empty()) {
        std::printf("    collision: NO _.YMO twins -> falling back to render geometry\n");
    } else {
        std::printf("    collision: %zu tris (%.1f%% of render), surfaces:",
                    r.coll.indices.size() / 3,
                    100.0 * double(r.coll.indices.size()) / double(std::max<std::size_t>(r.mesh.indices.size(), 1)));
        for (const std::string& s : r.coll.surfaces) std::printf(" %s", s.c_str());
        std::printf("\n");
        // CLERIA_SURF=1: per-surface triangle counts, split by whether the triangle
        // is floor-like (|n.y| > 0.5) — i.e. whether floor_at could ever pick it.
        if (std::getenv("CLERIA_SURF")) {
            std::vector<int> tot(r.coll.surfaces.size(), 0), flat(r.coll.surfaces.size(), 0);
            int unknown = 0;
            for (std::size_t i = 0; i + 2 < r.coll.indices.size(); i += 3) {
                const int s = r.coll.tri_surface[i / 3];
                if (s < 0) { ++unknown; continue; }
                const RoomVertex& a = r.coll.vertices[r.coll.indices[i]];
                const RoomVertex& b = r.coll.vertices[r.coll.indices[i + 1]];
                const RoomVertex& c = r.coll.vertices[r.coll.indices[i + 2]];
                const Vec3 e1{b.px - a.px, b.py - a.py, b.pz - a.pz};
                const Vec3 e2{c.px - a.px, c.py - a.py, c.pz - a.pz};
                Vec3 n = cross(e1, e2);
                const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                ++tot[std::size_t(s)];
                if (len > 1e-9f && std::fabs(n.y / len) > 0.5f) ++flat[std::size_t(s)];
            }
            for (std::size_t s = 0; s < r.coll.surfaces.size(); ++s)
                std::printf("      SURF %-10s tris=%4d floor-like=%4d  id=%d\n",
                            r.coll.surfaces[s].c_str(), tot[s], flat[s],
                            surface_index(r.coll.surfaces[s]));
            if (unknown) std::printf("      SURF (unknown material) tris=%d\n", unknown);
        }
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
    // CLERIA_CLIP=<name> [+ CLERIA_FRAME=<n>] poses a skinned model at an animation
    // frame instead of its rest pose. Frames are absolute (a clip's range is printed
    // by cleria-animtest --clips); n/60 s of scaled time.
    const char* clip = std::getenv("CLERIA_CLIP");
    const char* fr = std::getenv("CLERIA_FRAME");
    r.mesh = build_model(asset_root, ymo, clip, fr ? float(std::atof(fr)) : 0.0f);
    // CopyDst: the UV-scroll integrator rewrites scrolling materials' vertices in
    // place each frame via wgpuQueueWriteBuffer, which requires it.
    r.vbuf = make_buffer(g.device, g.queue, r.mesh.vertices.data(),
                         r.mesh.vertices.size() * sizeof(RoomVertex),
                         WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst);
    r.ibuf = make_buffer(g.device, g.queue, r.mesh.indices.data(),
                         r.mesh.indices.size() * sizeof(std::uint32_t), WGPUBufferUsage_Index);
    int loaded = 0;
    // mesh.textures are already RESOLVED absolute paths — scene.cpp resolves each
    // ref against its own YMO's directory. Do not reduce to a basename and look it
    // up game-wide: 532 basenames are ambiguous and 530 of those differ in content.
    for (const std::string& path : r.mesh.textures) {
        Dds dds = load_dds(path);
        if (dds.ok()) { r.binds.push_back(tex_bindgroup(g, upload_texture(g, r, dds))); ++loaded; }
        else {
            std::printf("    [tex] FAILED to load: %s\n", path.c_str());
            r.binds.push_back(g.white_bind);
        }
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
    pd.primitive.frontFace = g_front; pd.primitive.cullMode = g_cull;
    pd.depthStencil = &ds; pd.multisample.count = 1; pd.multisample.mask = 0xFFFFFFFF;
    pd.fragment = &fs;
    g.pipeline = wgpuDeviceCreateRenderPipeline(g.device, &pd);

    // Character/enemy pipeline: same layout, but ALPHA-BLEND with the fs_char shader
    // (real sprite alpha) instead of the room's hard alpha-test. Depth stays
    // test-less/write so sprites occlude against the room; overlapping sprites are few.
    WGPUBlendState cblend = {};
    cblend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha,
                    WGPUBlendFactor_OneMinusSrcAlpha};
    cblend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One,
                    WGPUBlendFactor_OneMinusSrcAlpha};
    WGPUColorTargetState ctarget = target; ctarget.blend = &cblend;
    WGPUFragmentState cfs = fs; cfs.entryPoint = sv("fs_char"); cfs.targets = &ctarget;
    // An upright billboard's lower half sits at nearly the floor's depth where it stands,
    // so it Z-FIGHTS the floor (the "bottom half disappears" the sprite showed). A
    // negative depth bias pulls the whole character clearly in front of the floor without
    // moving it on screen; the slope term handles the angled floor under it.
    WGPURenderPipelineDescriptor cpd = pd; cpd.fragment = &cfs;  // room depth (Less+write)
    g.char_pipeline = wgpuDeviceCreateRenderPipeline(g.device, &cpd);

    // Additive variant (same fs_char) for glow overlays: the sprite ADDS its colour weighted by
    // alpha, so a cyan glow brightens a row toward teal without masking the baked label under it.
    WGPUBlendState ablend = {};
    ablend.color = {WGPUBlendOperation_Add, WGPUBlendFactor_SrcAlpha, WGPUBlendFactor_One};
    ablend.alpha = {WGPUBlendOperation_Add, WGPUBlendFactor_One, WGPUBlendFactor_One};
    WGPUColorTargetState atarget = ctarget; atarget.blend = &ablend;
    WGPUFragmentState afs = cfs; afs.targets = &atarget;
    WGPURenderPipelineDescriptor apd = cpd; apd.fragment = &afs;
    g.char_pipeline_add = wgpuDeviceCreateRenderPipeline(g.device, &apd);

    // Floor shadow decal: same alpha-over blend + fs_char, but a flat floor quad and
    // DEPTH-WRITE OFF (build/render_re/ENVIRONMENT.md) -- a decal must not punch depth
    // holes, so overlapping shadows and the sprite above still test correctly. The SHADOW
    // texture is black RGB + radial alpha, so fs_char (black*white) outputs (0,0,0,texA)
    // and the blend darkens the floor by up to ~44%.
    WGPUDepthStencilState sds = ds; sds.depthWriteEnabled = WGPUOptionalBool_False;
    WGPUFragmentState sfs = cfs; sfs.entryPoint = sv("fs_shadow");   // black, no fog
    WGPURenderPipelineDescriptor spd = cpd; spd.depthStencil = &sds; spd.fragment = &sfs;
    spd.primitive.cullMode = WGPUCullMode_None;   // a flat floor decal must not be culled
    g.shadow_pipeline = wgpuDeviceCreateRenderPipeline(g.device, &spd);

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
        apd.primitive.frontFace = g_front; apd.primitive.cullMode = g_cull;
        apd.depthStencil = &ads; apd.multisample.count = 1; apd.multisample.mask = 0xFFFFFFFF;
        apd.fragment = &afs;
        g.fx_pipeline = wgpuDeviceCreateRenderPipeline(g.device, &apd);
        wgpuShaderModuleRelease(fm);
    }

    WGPUSamplerDescriptor sad = {};
    sad.addressModeU = sad.addressModeV = sad.addressModeW = WGPUAddressMode_Repeat;
    sad.magFilter = sad.minFilter = WGPUFilterMode_Linear; sad.maxAnisotropy = 1;
    g.sampler = wgpuDeviceCreateSampler(g.device, &sad);

    // Shared 1x1 white for a texture that FAILED to load (a real ref we couldn't
    // resolve) — kept white so it reads as a "missing texture" marker while debugging.
    Room tmp;
    Dds white; white.width = white.height = 1; white.format = Dds::RGBA8;
    white.data = {255, 255, 255, 255};
    g.white_bind = tex_bindgroup(g, upload_texture(g, tmp, white));
    // Shared 1x1 black for a genuinely UNTEXTURED material (no ref at all; scene.cpp
    // returns texture index -1). Those carry a diffuse (0,0,0) in S_1000's 3 "none"
    // submeshes; sampling white made them render as bright white quads (the "white
    // staircase"). Black matches the diffuse and removes the artifact.
    Dds black; black.width = black.height = 1; black.format = Dds::RGBA8;
    black.data = {0, 0, 0, 255};
    g.black_bind = tex_bindgroup(g, upload_texture(g, tmp, black));
    // deliberately leak the 1x1s (they live for the whole run)
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

// Advance every scrolling material by `ticks` and rewrite its vertices' UVs.
//
// The engine keeps a phase per material and uploads it as c1.xy at draw time; we do
// not have a per-draw uniform, so we fold the phase into the vertex UVs and re-upload
// just the affected ranges. Only 397 of 6642 materials scroll, and a room has a
// handful of such batches, so this is a few hundred bytes a frame.
static void advance_scroll(Gpu& g, Room& room, int ticks) {
    for (ScrollBatch& sb : room.scrolls) {
        sb.phase_u += sb.vel_u * float(ticks);
        sb.phase_v += sb.vel_v * float(ticks);
        // Keep the phase bounded; UVs wrap, so whole turns are free to discard. The
        // engine lets it grow, but float precision decays over a long session.
        sb.phase_u -= std::floor(sb.phase_u);
        sb.phase_v -= std::floor(sb.phase_v);
        if (std::size_t(sb.first_vert) + sb.vert_count > room.mesh.vertices.size()) continue;
        static std::vector<RoomVertex> tmp;
        tmp.assign(room.mesh.vertices.begin() + sb.first_vert,
                   room.mesh.vertices.begin() + sb.first_vert + sb.vert_count);
        for (RoomVertex& v : tmp) { v.u += sb.phase_u; v.v += sb.phase_v; }
        wgpuQueueWriteBuffer(g.queue, room.vbuf,
                             std::size_t(sb.first_vert) * sizeof(RoomVertex),
                             tmp.data(), tmp.size() * sizeof(RoomVertex));
    }
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
        // A resolved (or failed-load) texture is in room.binds; a genuinely untextured
        // material (index -1) renders its dark diffuse via black_bind, not white.
        WGPUBindGroup tb = (b.texture >= 0 && b.texture < int(room.binds.size()))
                               ? room.binds[b.texture] : g.black_bind;
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
// This is only a STARTING value for free-fly. In play mode `update_camera` overwrites
// it every frame from the room's SCM camera region (`g_fov = g_cam.fov`), so the game's
// authored FOV wins and this number never reaches the screen. It was hand-tuned against
// screenshots back when the SCM camera payload was undecoded; kept as the free-fly
// default only.
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

// The live camera, driven by the room's SCM regions. build/camera_re/CAMERA.md
// CONFIRMED this model against the disassembly (writer FUN_0042a3d0, orbit FUN_0042b6e0)
// and re-verified our scm.cpp field-for-field: pick() tests regions 1..N-1 as rotatable
// AABBs, first-containing wins, region 0 is the never-tested fallback; point A = follow
// target (mode 1), point B = orbit eye (mode 3 at yaw/pitch/dist); FOV modal 30deg,
// blend modal 0.015/frame -- both verified in the SCM corpus. The follow is INSTANT
// (eye re-derived from the player every frame), which is right: it is a static authored
// shot re-aimed at the player, an orbit rig pivoted on the player -- no dead-zone, no
// trailing lerp. apply_cam_state now calls the shared scm_eye (auto-yaw included).
//
// ** One sourced refinement DEFERRED (needs interactive verification, and --play can't
// run headless): ** the engine's region CHANGE lerps the EYE/TARGET/FOV positions on a
// bounded cubic-eased t:0->1 over ~0.015/frame, re-computing BOTH regions' eyes from the
// live player each frame (so neither endpoint lags). We instead lerp the orbit PARAMS
// exponentially -- a close approximation that avoids lag, but not the exact transition
// curve. Also unadopted: region STICKINESS (+0x3A8 bit0) and a target-Y low-pass
// (0.35/frame). All sourced in CAMERA.md; left until the camera can be watched in motion.
struct CamState {
    float fov = 0.5236f, yaw = 0, pitch = 0, dist = 24;
    // Captured from the current region so apply_cam_state can call the shared scm_eye
    // (which does the auto-yaw for flagged regions) instead of re-deriving the orbit.
    std::uint32_t eye_mode = 3, eye_flags = 0;
    Vec3 eye_fixed{};
    bool on = false;
};
static CamState g_cam;

// The game aims at ground + a per-actor offset (player+0x230 + player+0x324),
// which we don't model yet; a fixed chest-height offset stands in for it.
static constexpr float kLookHeight = 1.0f;

static void update_camera(const Room& room, const Vec3& feet, float dt, bool snap = false) {
    const CameraRegion* r = room.cam.pick(feet);
    if (!r) { g_cam.on = false; return; }
    g_cam.eye_mode = r->eye.mode;      // mode/flags/fixed track the current region
    g_cam.eye_flags = r->eye.flags;
    g_cam.eye_fixed = r->eye.fixed_pos;
    if (!g_cam.on || snap) {
        g_cam.fov = r->fov; g_cam.yaw = r->eye.yaw;
        g_cam.pitch = r->eye.pitch; g_cam.dist = r->eye.dist;
        g_cam.on = true;
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
// Event-camera SCROLL override (CUTSCENE_CAMERA.md §9): op 0xc3 Scroll_MoveTo slides the
// camera's LOOK-AT point (not the eye) to a scripted world target over `frames`, linearly;
// 0xc4 Scroll_Return slides it back to the player. The orbit rig (scm_eye) then frames the
// scripted point automatically. State + the last follow point (the scroll_base source) live
// here so apply_cam_state can lerp; scroll_advance() steps `t` once per 60 Hz frame.
struct ScrollCam { Vec3 target{}, base{}; float t = 0, t_dest = 0, rate = 1; bool active = false; };
static ScrollCam g_scroll;
static Vec3 g_last_look{};

// Optional overlay for the headless --play path: main can set this to draw UI (e.g. the
// real cutscene dialogue box via TitleFx::draw_dialogue) over the room after the HUD, so a
// preview goes through the actual render code rather than a re-implementation.
static std::function<void(Gpu&, WGPUTextureView, WGPUTextureView, std::uint32_t, std::uint32_t)> g_play_overlay;
static void scroll_moveto(float x, float y, float z, int frames) {
    g_scroll.target = {x, y, z}; g_scroll.base = g_last_look;
    g_scroll.t = 0; g_scroll.t_dest = 1; g_scroll.rate = frames > 0 ? 1.0f / frames : 1.0f;
    g_scroll.active = true;
    if (g_scroll.rate >= 1.0f) g_scroll.t = 1.0f;   // instant
}
static void scroll_return(int frames) {
    g_scroll.base = g_last_look; g_scroll.t_dest = 0;
    g_scroll.rate = frames > 0 ? 1.0f / frames : 1.0f;
    if (g_scroll.rate >= 1.0f) { g_scroll.t = 0; g_scroll.active = false; }
}
static void scroll_advance() {
    if (!g_scroll.active) return;
    const float d = g_scroll.t_dest - g_scroll.t, step = std::min(1.0f, g_scroll.rate);
    if (std::abs(d) <= step) { g_scroll.t = g_scroll.t_dest; if (g_scroll.t == 0.0f) g_scroll.active = false; }
    else g_scroll.t += (d > 0 ? step : -step);
}

// Event-camera SHAKE (CUTSCENE_CAMERA.md §6): op 0xcf jitters the camera for `frames` frames
// by a fresh random per-axis offset in [-amp,+amp], decaying to nothing when the counter hits
// 0. shake_advance() regenerates the offset + counts down once per 60 Hz frame; apply_cam_state
// folds the offset into the look-at. The exact geometric axis (basis vs eye) is [H]; the
// random-per-frame decay is the certain, code-grounded part. (PRNG is a local xorshift — the
// game's exact seed isn't matched, noted rather than hidden.)
struct ShakeCam { Vec3 amp{}, off{}; int frames = 0; std::uint32_t rs = 0x9e3779b9u; };
static ShakeCam g_shake;
static void shake(float ax, float ay, float az, int frames) {
    g_shake.amp = {ax, ay, az}; g_shake.frames = frames; g_shake.off = {0, 0, 0};
}
static void shake_advance() {
    if (g_shake.frames <= 0) { g_shake.off = {0, 0, 0}; return; }
    auto nextf = [&] {                       // xorshift32 -> [-1, 1)
        std::uint32_t x = g_shake.rs;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5; g_shake.rs = x;
        return (float(x) / 4294967296.0f) * 2.0f - 1.0f;
    };
    g_shake.off = {nextf() * g_shake.amp.x, nextf() * g_shake.amp.y, nextf() * g_shake.amp.z};
    --g_shake.frames;
}

static void apply_cam_state(Camera& cam, const Vec3& feet) {
    Vec3 target = {feet.x, feet.y + kLookHeight, feet.z};   // A.mode 1: follow
    g_last_look = target;                                    // remember the follow point (scroll base)
    if (g_scroll.active && g_scroll.t != 0.0f)              // event scroll: lerp toward the scripted point
        target = {g_scroll.base.x + (g_scroll.target.x - g_scroll.base.x) * g_scroll.t,
                  g_scroll.base.y + (g_scroll.target.y - g_scroll.base.y) * g_scroll.t,
                  g_scroll.base.z + (g_scroll.target.z - g_scroll.base.z) * g_scroll.t};
    if (g_shake.frames > 0) { target.x += g_shake.off.x; target.y += g_shake.off.y; target.z += g_shake.off.z; }
    // Use the shared orbit (scm_eye), built from the BLENDED yaw/pitch/dist plus the
    // current region's mode/flags. This is the engine's FUN_0042b6e0 -- including the
    // auto-yaw for flagged regions, which the old hand-rolled copy here dropped.
    CameraPoint b;
    b.mode = g_cam.eye_mode;
    b.yaw = g_cam.yaw;
    b.pitch = g_cam.pitch;
    b.dist = g_cam.dist;
    b.flags = g_cam.eye_flags;
    b.fixed_pos = g_cam.eye_fixed;
    cam.pos = scm_eye(b, target, feet);
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
// Besides the texture, a sprite carries where its tight bbox sits ON THE AIA CANVAS
// (x0,y0) plus that file's constant anchor (canvasW, foot-line oy = bill[2]). This is
// what keeps an animation from sliding: every frame is placed against the SAME canvas
// hotspot (canvasW/2, oy), and only the silhouette moves within it (build/anim_re/
// SPRITEANCHOR.md). place_ok stays false for non-AIA sprites (they set no canvas data).
struct SpriteTex {
    WGPUTexture tex{}; WGPUTextureView view{}; WGPUBindGroup bind{};
    std::uint32_t w = 0, h = 0;
    float x0 = 0, y0 = 0, canvasW = 0, oy = 0;
    bool place_ok = false;
    void set_placement(const AiaFrame& fr, std::uint32_t cw) {
        x0 = float(fr.x0); y0 = float(fr.y0); canvasW = float(cw); oy = fr.bill[2];
        place_ok = true;
    }
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
            // Body + the second layer, composited ADDITIVELY — the engine's own blend
            // (SRCALPHA/ONE, read out of FUN_004745a0). The layer is a real overlay:
            // Yunica's fire-sword, Toal's Boost aura, projectile orbs. ~90% of frames
            // have none and this is then identical to decode_frame.
            if (!aia.decode_frame_composited(image, d, frame % std::max(1u, nframe), fr))
                continue;
            if (dirs[d].tex) { wgpuBindGroupRelease(dirs[d].bind); wgpuTextureViewRelease(dirs[d].view); wgpuTextureRelease(dirs[d].tex); }
            dirs[d] = upload_rgba(g, fr.w, fr.h, fr.rgba.data());
            dirs[d].set_placement(fr, aia.canvasW);   // canvas anchor -> no animation slide
        }
    }
};

// Draw an upright, camera-facing billboard of `spr` standing at `feet`, in a
// pass that keeps the already-rendered room colour + depth (so it occludes
// correctly). worldH is the sprite's world height; camForward is the (fixed)
// camera view direction. Reuses the room pipeline (alpha-test + white modulate).
static void draw_character(Gpu& g, WGPUTextureView color, WGPUTextureView depth,
                           const Vec3& feet, float worldH, const SpriteTex& spr,
                           const Vec3& camForward, const Vec3& camPos, float flash = 0.0f,
                           float tr = 1.0f, float tg = 1.0f, float tb = 1.0f,
                           bool additive = false) {
    if (!spr.tex || spr.h == 0) return;
    const Vec3 up = {0, 1, 0};
    Vec3 right = normalize(cross(up, camForward));       // horizontal screen-right
    // Nudge the billboard a SMALL distance toward the camera ALONG THE VIEW RAY, exactly as
    // the engine does (build/anim_re/SPRITEDEPTH.md: base = feet - normalize(feet-camPos)*B,
    // B ~= 0.1 world units = bill[0]). The ray's upward component lifts the base off the
    // floor so it wins the LessEqual z-test against the floor plane. ** The old fix shoved
    // 0.9 units HORIZONTALLY, ~9x too far, which punched the sprite in front of any fence
    // closer than 0.9u ** -- this small full-3D nudge clears the floor without beating a
    // fence (a fence more than ~0.12u in front still occludes the sprite, as it should).
    Vec3 ray = {feet.x - camPos.x, feet.y - camPos.y, feet.z - camPos.z};
    const float rl = std::sqrt(ray.x * ray.x + ray.y * ray.y + ray.z * ray.z);
    if (rl > 1e-4f) { ray.x /= rl; ray.y /= rl; ray.z /= rl; }
    // Engine uses ~0.1 (bill[0]) PLUS an oz/pitch per-vertex depth lean; we don't model the
    // lean yet, so a slightly larger uniform nudge covers the floor at steep angles. Still
    // far under a fence's ~0.5u standoff, so occlusion stays correct. [H-med] until the lean.
    constexpr float kDepthNudge = 0.2f;
    const Vec3 base = {feet.x - ray.x * kDepthNudge, feet.y - ray.y * kDepthNudge,
                       feet.z - ray.z * kDepthNudge};
    // Quad extents in billboard-local units (X = along `right`, Y = world up), from feet.
    float lX, rX, tY, bY;
    if (spr.place_ok && spr.canvasW > 0) {
        // Canvas-anchored (SPRITEANCHOR.md): place the tight bbox at its (x0,y0) on the
        // AIA canvas, measured from the file's CONSTANT hotspot (canvasW/2, oy=foot line),
        // with ONE scale for every frame. That fixed hotspot is what stops an animation
        // sliding -- the attack's silhouette shifts inside the frame, not the whole sprite.
        // kSpriteScale is world units per canvas pixel (the old 76px = ~2.0-world reading).
        constexpr float kSpriteScale = 2.0f / 76.0f;
        const float S = kSpriteScale;
        lX = (spr.x0 - spr.canvasW * 0.5f) * S;
        rX = (spr.x0 + float(spr.w) - spr.canvasW * 0.5f) * S;
        tY = (spr.oy - spr.y0) * S;
        bY = (spr.oy - (spr.y0 + float(spr.h))) * S;
    } else {
        // Fallback (no canvas data, e.g. a DDS sprite): centre the tight bbox at the feet
        // and span feet.y .. feet.y+worldH, keeping the old aspect-preserving behaviour.
        const float Wc = worldH * (float(spr.w) / float(spr.h));
        lX = -Wc * 0.5f; rX = Wc * 0.5f; tY = worldH; bY = 0.0f;
    }
    // Vertex-colour alpha carries the hit-flash amount (fs_char lerps to white by it); RGB is
    // a per-quad modulate (tc.rgb * color.rgb) — white = as-is, a red tint marks event seeds.
    const std::uint8_t fa = std::uint8_t(std::max(0.0f, std::min(1.0f, flash)) * 255.0f);
    const std::uint8_t cr = std::uint8_t(std::clamp(tr, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cg = std::uint8_t(std::clamp(tg, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cb = std::uint8_t(std::clamp(tb, 0.0f, 1.0f) * 255.0f);
    auto V = [&](float lx, float ly, float u, float v) -> RoomVertex {
        return {base.x + right.x * lx, base.y + ly, base.z + right.z * lx,
                0, 0, -1, u, v, cr, cg, cb, fa};
    };
    RoomVertex verts[4] = {V(lX, tY, 0, 0), V(rX, tY, 1, 0), V(rX, bY, 1, 1), V(lX, bY, 0, 1)};
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
    wgpuRenderPassEncoderSetPipeline(pass, additive && g.char_pipeline_add ? g.char_pipeline_add : g.char_pipeline);
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

// A soft round floor shadow under a character (build/render_re/ENVIRONMENT.md). The engine
// draws a flat black alpha decal on the floor, NOT a projected silhouette. SHADOW0.DDS is
// 64x64 with black RGB and a radial alpha falloff (peak ~0.44); loaded RGBA8 it drives
// fs_char to output (0,0,0,texA), and the alpha-over blend darkens the floor by up to ~44%.
struct ShadowFx {
    SpriteTex tex;
    bool init(Gpu& g, const fs::path& asset_root) {
        Dds d = load_dds(asset_root / "IMAGE" / "SHADOW0.DDS");
        if (!d.ok()) return false;
        tex = upload_rgba(g, d.width, d.height, d.data.data());
        return true;
    }
};

// Draw the floor shadow decal for an actor at `feet`, on the floor plane at `floorY`, with
// half-width `radius`. A flat quad on XZ (normal +Y), alpha-over, depth-read-no-write, so it
// darkens the floor and is occluded by nearer geometry but never writes depth. Draw BEFORE
// the sprite. Vertex colour (255,255,255,0): no fog dim, no hit-flash -> pure black * texA.
static void draw_shadow(Gpu& g, WGPUTextureView color, WGPUTextureView depth,
                        const SpriteTex& shadow, const Vec3& feet, float floorY, float radius) {
    if (!shadow.tex) return;
    const float y = floorY + 0.05f;   // a hair above the floor to avoid decal z-fight
    auto V = [&](float sx, float sz, float u, float v) -> RoomVertex {
        return {feet.x + sx * radius, y, feet.z + sz * radius, 0, 1, 0, u, v, 255, 255, 255, 0};
    };
    RoomVertex verts[4] = {V(-1, -1, 0, 0), V(1, -1, 1, 0), V(1, 1, 1, 1), V(-1, 1, 0, 1)};
    std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    WGPUBuffer vb = make_buffer(g.device, g.queue, verts, sizeof verts, WGPUBufferUsage_Vertex);
    WGPUBuffer ib = make_buffer(g.device, g.queue, idx, sizeof idx, WGPUBufferUsage_Index);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = depth; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, g.shadow_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, shadow.bind, 0, nullptr);
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

// Draw a screen-space textured quad (UI/title images) at NDC corners (l,t)-(r,b), z in [0,1].
// Reuses the char pipeline (alpha-over, fs_char); the caller writes an IDENTITY MVP + fog off
// beforehand so the RoomVertex positions are taken as clip space directly. `clear` starts a
// fresh frame (clears colour to `bg` + depth); later images Load. Vertex colour (255,255,255,0):
// no fog dim, no hit-flash -> the texture shows as-is.
static void draw_screen_image(Gpu& g, WGPUTextureView color, WGPUTextureView depth, bool clear,
                              const SpriteTex& tex, float l, float t, float r, float b, float z,
                              float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1,
                              float bgr = 0.02f, float bgg = 0.02f, float bgb = 0.05f,
                              float wtint = 0.0f, bool additive = false,
                              float tr = 1.0f, float tg = 1.0f, float tb = 1.0f) {
    if (!tex.tex) return;
    // Vertex alpha carries a white-tint amount (fs_char mixes the sampled colour toward white by
    // it) -- used for the char-select selection-bar pulse. 0 = the texture as-is.
    // Vertex RGB is a per-quad MODULATE colour (fs_char does tc.rgb * color.rgb); default white =
    // texture as-is, <1 dims it (e.g. 0.5 -> 0x808080 for the Options "dim the unpicked rows" pass).
    const std::uint8_t wt = (std::uint8_t)(std::clamp(wtint, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cr = (std::uint8_t)(std::clamp(tr, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cg = (std::uint8_t)(std::clamp(tg, 0.0f, 1.0f) * 255.0f);
    const std::uint8_t cb = (std::uint8_t)(std::clamp(tb, 0.0f, 1.0f) * 255.0f);
    RoomVertex verts[4] = {{l, t, z, 0, 0, -1, u0, v0, cr, cg, cb, wt},
                           {r, t, z, 0, 0, -1, u1, v0, cr, cg, cb, wt},
                           {r, b, z, 0, 0, -1, u1, v1, cr, cg, cb, wt},
                           {l, b, z, 0, 0, -1, u0, v1, cr, cg, cb, wt}};
    std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
    WGPUBuffer vb = make_buffer(g.device, g.queue, verts, sizeof verts, WGPUBufferUsage_Vertex);
    WGPUBuffer ib = make_buffer(g.device, g.queue, idx, sizeof idx, WGPUBufferUsage_Index);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
    WGPURenderPassColorAttachment ca = {};
    ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    ca.loadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {bgr, bgg, bgb, 1.0};
    WGPURenderPassDepthStencilAttachment da = {};
    da.view = depth; da.depthLoadOp = clear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    da.depthStoreOp = WGPUStoreOp_Store; da.depthClearValue = 1.0f;
    WGPURenderPassDescriptor rp = {};
    rp.colorAttachmentCount = 1; rp.colorAttachments = &ca; rp.depthStencilAttachment = &da;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderSetPipeline(pass, additive && g.char_pipeline_add ? g.char_pipeline_add : g.char_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, tex.bind, 0, nullptr);
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

// A world heading (hx,hz) -> the 8-direction sprite index, screen-relative to the camera
// yaw. One calibration shared by live movement and by SEN arrival facing: the C_10x ring
// runs dir0=screen-right, +45 deg CCW per step (screen-up = dir2 back, down = dir6 front,
// left = dir4). Screen basis: fwd = (cos yaw, sin yaw) is screen-up, right = cross(up,fwd)
// = (sin yaw, -cos yaw). h need not be normalized (atan2 is scale-invariant).
static int heading_to_facing(float hx, float hz, float cam_yaw) {
    const float fx = std::cos(cam_yaw), fz = std::sin(cam_yaw);
    const float sUp = hx * fx + hz * fz;      // component along screen-up
    const float sRt = hx * fz - hz * fx;      // component along screen-right
    const float deg = std::atan2(sUp, sRt) * 57.29578f;
    return int(std::lround((deg + 360.0f) / 45.0f)) & 7;
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
        std::vector<SpriteTex> anim[8];   // idle animation frames, per facing direction
        std::uint32_t ndir = 0;
        float height = 1.6f;
        // Other animations (walk, attack, hurt, death), decoded on demand and keyed by
        // AIA tag -> 8 facings. Idle stays in `anim` above; this is everything the enemy
        // ASO can ask for beyond it, so an attacking enemy shows its attack clip.
        std::unordered_map<std::uint32_t, std::array<std::vector<SpriteTex>, 8>> byTag;
    };
    std::unordered_map<std::string, Kind> kinds;   // sprite stem -> decoded facings

    // Frames for one animation tag + facing, decoded and cached lazily. Falls back to
    // the idle set when the tag is not in this AIA (so a script asking for a clip the
    // sprite lacks still draws something rather than vanishing).
    const std::vector<SpriteTex>& tag_frames(Gpu& g, Kind& k, std::uint32_t tag,
                                             std::uint32_t dir) {
        if (dir >= 8) dir = 0;
        auto it = k.byTag.find(tag);
        if (it == k.byTag.end()) {
            std::array<std::vector<SpriteTex>, 8> a;
            int img = -1;
            for (std::uint32_t i = 0; i < k.aia.nImg; ++i)
                if (k.aia.images[i].tag == tag) { img = int(i); break; }
            if (img >= 0) {
                const std::uint32_t nd = k.aia.images[img].nDir;
                const std::uint32_t nf = k.aia.images[img].nFrame;
                for (std::uint32_t d = 0; d < 8 && d < nd; ++d)
                    for (std::uint32_t f = 0; f < nf; ++f) {
                        AiaFrame fr;
                        if (!k.aia.decode_frame_composited(std::uint32_t(img), d, f, fr)) continue;
                        SpriteTex st = upload_rgba(g, fr.w, fr.h, fr.rgba.data());
                        st.set_placement(fr, k.aia.canvasW);
                        a[d].push_back(std::move(st));
                    }
            }
            it = k.byTag.emplace(tag, std::move(a)).first;
        }
        const std::vector<SpriteTex>& v = it->second[dir];
        if (!v.empty()) return v;
        if (!it->second[0].empty()) return it->second[0];   // fewer facings shipped
        return k.anim[dir < k.ndir ? dir : 0];              // tag absent -> idle
    }

    // Decode ALL frames of one enemy type's idle animation for EVERY facing, so
    // draw_enemies can both rotate (pick the dir from the enemy's heading) and animate
    // (cycle the frames). Idempotent per stem.
    void load_kind(Gpu& g, const fs::path& asset_root, const std::string& stem) {
        if (stem.empty() || kinds.count(stem)) return;
        Kind k;
        if (!k.aia.load(asset_root / "ANI" / (stem + ".AIA"))) return;
        // Some enemy AIAs (e.g. S_1000's M_810) load with zero images -- skip them rather than
        // index images[0] out of range (was a hard crash on any room that spawns such a cast).
        if (k.aia.nImg == 0 || k.aia.images.empty()) return;
        int img = -1;
        for (std::uint32_t i = 0; i < k.aia.nImg; ++i)
            if (k.aia.images[i].tag == 0x2) { img = int(i); break; }   // idle
        if (img < 0 || img >= int(k.aia.nImg)) img = 0;
        k.ndir = k.aia.images[img].nDir;
        const std::uint32_t nfr = k.aia.images[img].nFrame;
        std::uint32_t tall = 1;
        for (std::uint32_t d = 0; d < 8 && d < k.ndir; ++d)
            for (std::uint32_t f = 0; f < nfr; ++f) {
                AiaFrame fr;
                if (!k.aia.decode_frame_composited(std::uint32_t(img), d, f, fr)) continue;
                SpriteTex st = upload_rgba(g, fr.w, fr.h, fr.rgba.data());
                st.set_placement(fr, k.aia.canvasW);   // canvas anchor, like the player
                k.anim[d].push_back(st);
                tall = std::max(tall, fr.h);
        }
        // Scale off the sprite's own pixel height, against the player's known
        // ~76px / 2.0 world units. Their true size is not established.
        k.height = float(tall) * (2.0f / 76.0f);
        kinds.emplace(stem, std::move(k));
    }

    // Sprites for the SCT placements (the pre-world path; still used headlessly).
    void init(Gpu& g, const fs::path& asset_root, const Sct& cast) {
        for (const CastPlacement& c : cast.casts) load_kind(g, asset_root, c.sprite_stem());
    }

    // Sprites for every cast actually SPAWNED into the world -- which includes op-0x12c
    // scripted casts the SCT never named, so those hub NPCs render too. cast_id ->
    // "M_100" via CastTable::sprite_stem, the same key init() derives from the SCT name.
    void init_from_world(Gpu& g, const fs::path& asset_root, const ActorWorld& world,
                         const CastTable& casts) {
        for (const Actor& a : world.actors())
            load_kind(g, asset_root, casts.sprite_stem(a.cast_id));
    }
};

// A placed enemy at runtime, with the game's own stats.
//
// The old comment here said "the game has no per-enemy HP column — stats are lerped
// from a level curve, so this is our own number until that curve is wired up". Half
// right, and the wrong half mattered: there IS no raw HP column, but there is a
// per-enemy log2 MULTIPLIER, and a stat is K(column) * curve(that enemy's own level).
// The curve alone describes no enemy that exists. See enemies.hpp.
//
// The chain: SCT places a CAST_ name -> CASTINFO gives cast_id -> ENEMYINFO[cast_id]
// gives the level and the multipliers -> GAMEINFO gives the curve.
// A rendered enemy: a real Actor in the ActorWorld, paired with its sprite identity.
// The game-state fields (hp/atk/def/level/pos/alive) live on the Actor, not here — the
// view stopped keeping its own copy so there is ONE spawn+stat path (ActorWorld::spawn),
// not three. `actor` is stable across the room: the view never sweeps, so no erase
// invalidates it (and ActorWorld's list keeps node addresses stable regardless).
struct EnemyRender {
    Actor* actor = nullptr;
    std::string stem;
    std::string display;
    // ASO-driven combat, bound in bind_enemy_aso. When avm has no script the enemy
    // falls back to the old idle-cycling render and never scripts an attack.
    AsoVm avm;
    int atk_section = -1;   // this kind's attack section id, or -1 if none
    int idle_section = 2;   // resting section (2, else 0, else the first present)
    int atk_cd = 0;         // ticks until it may attack again
    int atk_win = 0;        // active-hitbox ticks remaining
    AsoVm::Hitbox hb;
    float atk_dx = 0, atk_dz = 1;  // heading latched when the swing begins
    bool atk_hit = false;   // already connected with the player this swing
    long atk_fkey = -1;     // recovery-stall detection, as the player has
    int atk_stall = 0;
    bool atk_armed = false; // the swing's hit-volume has fired this attack
    int atk_age = 0;        // ticks in the current attack (hard-cap fallback)
};

// Populate an ActorWorld the way the engine does (build/roomload_re/ROOMLOAD.md,
// verified against our own disassembly): run the room's S_xxxx.XSO from entry 0. Its
// opcode 0xa0 bulk-spawns the SCT once (the world's set_placements source); its opcode
// 0x12c adds scripted actors 0..N times. Both funnel through the CASTINFO factory
// (ActorWorld::spawn). The asset-load opcodes (0x9b..0xa1 minus 0xa0) are inert here —
// the view loads geometry its own way. State-gated 0x12c spawns branch on story flags,
// so we seed the VM from the persistent game state; a fresh game reaches the
// fresh-game subset (NOT the 146 static ops), which is faithful.
//
// This is what makes a HUB like S_1000 populate: its SCT holds only the player, and all
// its NPCs come from 0x12c — the SCT-only path saw none of them. If the script is
// missing, falls back to op 0xa0 alone (SCT).
static void populate_room(ActorWorld& world, const Room& room, const std::int32_t* g_flags) {
    world.clear();
    world.set_placements(&room.cast);
    const fs::path xso = room.dir / (room.name + ".XSO");
    if (fs::exists(xso)) {
        XsoVm vm;
        vm.host = &world;
        std::copy(g_flags, g_flags + 0x200, std::begin(vm.g_flags));  // story-gated spawns
        try {
            vm.run(Xso::parse(read_bytes(xso)));
        } catch (const std::exception&) {
            world.clear();
            world.set_placements(&room.cast);
            world.spawn_placements();  // a corrupt script still gets the SCT
        }
    } else {
        world.spawn_placements();  // op 0xa0 alone
    }
}

// Pair each spawned ENEMY actor with its sprite. cast_id -> "M_100" (CastTable::
// sprite_stem), skipping the player and any actor whose sprite did not load. The
// world already holds the game state (hp/pos/level); this record only carries the
// view-side sprite identity.
static std::vector<EnemyRender> build_enemy_render(ActorWorld& world,
                                                   const CastTable& casts,
                                                   const EnemyFx& fx) {
    std::vector<EnemyRender> out;
    for (Actor& a : world.actors()) {
        if (a.is_player()) continue;
        const std::string stem = casts.sprite_stem(a.cast_id);
        if (stem.empty() || !fx.kinds.count(stem)) continue;   // no loaded sprite
        const Cast* c = casts.get(a.cast_id);
        out.push_back({&a, stem, (c && !c->display.empty()) ? c->display : stem});
    }
    return out;
}

// Draw every living enemy as an upright camera-facing billboard, cycling its idle
// animation. `t_anim` is the presentation clock (seconds).
static void draw_enemies(Gpu& g, WGPUTextureView color, WGPUTextureView depth,
                         EnemyFx& fx, const std::vector<EnemyRender>& list,
                         const Vec3& camFwd, const Vec3& camPos, float t_anim,
                         const SpriteTex& shadow) {
    constexpr float kEnemyAnimFps = 8.0f;   // TUNED: real rate is per-enemy ASO Wait
    // Screen basis, matching the player's facing calibration: horizontal camera forward
    // (screen-up) and its right. The enemy's heading is mapped into this basis so the
    // sprite ring rotates with the enemy the same way the player's does.
    Vec3 fwdH = {camFwd.x, 0, camFwd.z};
    const float fl = std::sqrt(fwdH.x * fwdH.x + fwdH.z * fwdH.z);
    if (fl > 1e-4f) { fwdH.x /= fl; fwdH.z /= fl; }
    const Vec3 rt = normalize(cross(Vec3{0, 1, 0}, fwdH));
    for (const EnemyRender& e : list) {
        if (!e.actor || !e.actor->alive()) continue;
        auto it = fx.kinds.find(e.stem);
        if (it == fx.kinds.end()) continue;
        EnemyFx::Kind& k = it->second;
        // Floor shadow first (enemies are grounded, so pos.y is the floor).
        draw_shadow(g, color, depth, shadow, e.actor->pos, e.actor->pos.y, 0.6f);
        // Facing: the enemy's heading (yaw, vx=sin/vz=cos convention) mapped to the same
        // 8-dir ring the player uses -- up->dir2 (back), right->dir0, down->dir6 (front).
        // A chasing enemy faces the player (~the camera), so it mostly shows its front.
        const Vec3 hd = {std::sin(e.actor->yaw), 0, std::cos(e.actor->yaw)};
        const float sUp = hd.x * fwdH.x + hd.z * fwdH.z;
        const float sRt = hd.x * rt.x + hd.z * rt.z;
        const float deg = std::atan2(sUp, sRt) * 57.29578f;
        int dir = int(std::lround((deg + 360.0f) / 45.0f)) & 7;
        const std::vector<SpriteTex>* frames = nullptr;
        int fi = 0;
        if (e.avm.ok()) {
            // ASO-driven: the running section's anime_Pattern picks the clip (tag) and the
            // VM the frame, so an attacking enemy shows its attack animation, an idle one
            // its idle, all in step with the hit-volume the same script arms.
            const std::vector<SpriteTex>& fr =
                fx.tag_frames(g, k, std::uint32_t(e.avm.anim_tag()), std::uint32_t(dir));
            if (!fr.empty()) {
                frames = &fr;
                const int n = int(fr.size());
                fi = (e.avm.frame() % n + n) % n;
            }
        }
        if (!frames) {  // scriptless enemy: cycle idle, offset per instance
            if (dir >= int(k.ndir) || k.anim[dir].empty()) dir = 0;
            if (k.anim[dir].empty()) continue;
            frames = &k.anim[dir];
            const int n = int(frames->size());
            fi = ((int(t_anim * kEnemyAnimFps) + e.actor->instance_id * 2) % n + n) % n;
        }
        const SpriteTex* t = &(*frames)[fi];
        if (!t->tex) continue;
        // Flash white on a recent hit (hit_flash counts down 6..0 in the tick).
        const float flash = e.actor->hit_flash > 0 ? 0.8f * float(e.actor->hit_flash) / 6.0f : 0.0f;
        draw_character(g, color, depth, e.actor->pos, k.height, *t, camFwd, camPos, flash);
        // Event-bound (group-1..4) enemies GLOW their marker colour, ADDED to their own sprite and
        // pulsing by |sin(phase)| — the engine's FUN_00473c60 mechanism (build/effect_re/EF2_RENDER.md
        // §B): marker ARGB, alpha = intensity, per-enemy phase. Group 1 = 0xFFE00000 (red 224). We
        // reproduce it as a second ADDITIVE pass of the same sprite tinted by RGB*(A*|sin|/255).
        static const std::uint32_t kGroupColour[4] = {0xFFE00000u, 0xFF00D000u, 0xFF0000FFu, 0xFF00C0C0u};
        for (int grp = 1; grp <= 4; ++grp) {
            if (!e.actor->tag.in_group(grp)) continue;
            const std::uint32_t mk = kGroupColour[grp - 1];
            const float A = float((mk >> 24) & 0xFF) / 255.0f;
            const float mr = float((mk >> 16) & 0xFF) / 255.0f;
            const float mg = float((mk >> 8) & 0xFF) / 255.0f;
            const float mb = float(mk & 0xFF) / 255.0f;
            const float phase = float(e.actor->instance_id) * 2.4f + t_anim * 6.2831853f;  // per-enemy offset
            const float I = A * std::fabs(std::sin(phase));                                 // pulsing intensity
            draw_character(g, color, depth, e.actor->pos, k.height, *t, camFwd, camPos,
                           0.0f, mr * I, mg * I, mb * I, /*additive*/true);
            break;
        }
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


// ---- projectiles (ASO anime_FireBullet -> an EF2 effect) --------------------
//
// A bullet is not an actor: the engine spawns a lightweight object whose only
// renderable is one EF2 effect instance, and it lives exactly as long as that
// effect does (build/projectile_re/PROJECTILE.md). So the visual is the real
// shipped effect — arg[1] is an eftlist id, and Hugo's 130 is
// pc\pc02\PC2_DEF_ATTACK.eft. Every textured emitter of the effect is drawn,
// rather than guessing which one is "the" bullet.
struct BulletArt {
    struct Layer {
        SpriteTex tex;
        std::vector<Ef2Frame> frames;   // UV rect per frame, from the EF2
        float w = 1, h = 1;             // emitter scale
        // Ring primitive (EF2_RENDER.md §A): a flat horizontal annulus in world XZ, not a
        // billboard — the kekkai ward. Segments + radii + Y come straight from the EF2.
        bool ring = false;
        int ring_segs = 0;
        float ring_outer = 0, ring_inner = 0, ring_height = 0;
    };
    std::vector<Layer> layers;
    int life = 60;                      // ticks; the effect's own lifetime
    bool ok() const { return !layers.empty(); }
};

// id -> art, loaded once. Effects that fail to resolve cache as empty so a missing
// one is not retried every shot.
static const BulletArt& bullet_art(Gpu& g, const fs::path& asset_root, const EftList& efl,
                                   const EftTex& eft, int effect_id) {
    static std::unordered_map<int, BulletArt> cache;
    auto it = cache.find(effect_id);
    if (it != cache.end()) return it->second;
    BulletArt& a = cache[effect_id];
    const fs::path p = efl.path(asset_root, effect_id);
    if (p.empty()) return a;
    const Ef2 ef = Ef2::load(p);
    if (!ef.ok()) return a;
    a.life = int(ef.life() > 0 ? ef.life() : 60);
    for (const Ef2Emitter& em : ef.emitters()) {
        if (!em.textured()) continue;   // frame_count 0 draws no quad at all
        const fs::path tp = eft.path(asset_root, int(em.frames[0].tex_id));
        if (tp.empty()) continue;
        Dds d = load_dds(tp);
        if (!d.ok()) continue;
        BulletArt::Layer L;
        L.tex = upload_rgba(g, d.width, d.height, d.data.data());
        L.frames = em.frames;
        L.w = em.sx;
        L.h = em.sy;
        L.ring = em.ring;
        L.ring_segs = em.ring_segs;
        L.ring_outer = em.ring_outer;
        L.ring_inner = em.ring_inner;
        L.ring_height = em.ring_height;
        a.layers.push_back(L);
    }
    return a;
}

struct Projectile {
    Vec3 pos{};
    Vec3 dir{1, 0, 0};
    float speed = 0, accel = 0, gravity = 0;
    float scale = 1;
    int effect_id = 0;
    int age = 0, life = 60;
    bool from_player = true;
    std::unordered_set<int> hit;   // per-bullet hit dedup, as melee does
    bool alive() const { return age < life; }
};

// One tick of flight. The engine integrates speed then re-derives heading from the
// resulting velocity, so gravity curves the bullet AND re-aims it.
static void step_projectile(Projectile& p) {
    p.speed += p.accel;
    Vec3 v = {p.dir.x * p.speed, p.dir.y * p.speed, p.dir.z * p.speed};
    v.y -= p.gravity;
    const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l > 1e-6f) {
        p.dir = {v.x / l, v.y / l, v.z / l};
        p.speed = l;
    }
    p.pos.x += v.x; p.pos.y += v.y; p.pos.z += v.z;
    ++p.age;
}

static void draw_projectiles(Gpu& g, const std::vector<Projectile>& shots,
                             const fs::path& asset_root, const EftList& efl, const EftTex& eft,
                             WGPUTextureView color, WGPUTextureView depth, const Vec3& camFwd) {
    if (shots.empty()) return;
    const Vec3 up = {0, 1, 0};
    const Vec3 right = normalize(cross(up, camFwd));
    // One draw per texture: group by layer so each keeps its own bind group.
    for (const Projectile& s : shots) {
        const BulletArt& art = bullet_art(g, asset_root, efl, eft, s.effect_id);
        if (!art.ok()) continue;
        for (const BulletArt::Layer& L : art.layers) {
            if (L.frames.empty()) continue;
            const int fi = std::min(int(L.frames.size()) - 1,
                                    int(std::size_t(s.age) * L.frames.size() /
                                        std::size_t(std::max(1, art.life))));
            const Ef2Frame& fr = L.frames[std::size_t(fi)];
            const float hw = L.w * s.scale * 0.5f, hh = L.h * s.scale * 0.5f;
            RoomVertex verts[4];
            std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
            auto V = [&](int n, float sx, float sy, float u, float v) {
                verts[n] = {s.pos.x + right.x * sx * hw, s.pos.y + sy * hh,
                            s.pos.z + right.z * sx * hw, 0, 0, -1, u, v, 255, 255, 255, 255};
            };
            V(0, -1, 1, fr.u0, fr.v0);  V(1, 1, 1, fr.u1, fr.v0);
            V(2, 1, -1, fr.u1, fr.v1);  V(3, -1, -1, fr.u0, fr.v1);
            WGPUBuffer vb = make_buffer(g.device, g.queue, verts, sizeof(verts),
                                        WGPUBufferUsage_Vertex);
            WGPUBuffer ib = make_buffer(g.device, g.queue, idx, sizeof(idx),
                                        WGPUBufferUsage_Index);
            WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
            WGPURenderPassColorAttachment ca = {};
            ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDepthStencilAttachment da = {};
            da.view = depth; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor rp = {};
            rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
            rp.depthStencilAttachment = &da;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
            wgpuRenderPassEncoderSetPipeline(pass, g.fx_pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, L.tex.bind, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(pass, ib, WGPUIndexFormat_Uint32, 0,
                                                WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexed(pass, 6, 1, 0, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
            wgpuQueueSubmit(g.queue, 1, &cmd);
            wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
            wgpuBufferRelease(vb); wgpuBufferRelease(ib);
        }
    }
}

// Persistent script-placed effects (op 0x11a): the barrier ward and friends. Each is one
// eftlist effect looping its flipbook forever at a fixed world position, until op 0x119
// removes it. Unlike a projectile it is NOT a fixed billboard — the kekkai ward stands in
// the world around the chest, and its exact rake is still being matched to the retail
// effect, so orientation is fully KNOB-DRIVEN: CLERIA_BARRIER_TILT rotates the plane from
// UPRIGHT (0deg, top = straight up) toward FLAT on the ground (90deg, top = raked away from
// the camera), about the camera's horizontal-right axis; CLERIA_BARRIER_LIFT raises the
// centre off the floor; CLERIA_BARRIER_SCALE sizes it. Dial these live, then bake the
// winners in as the defaults.
static void draw_located_effects(Gpu& g, const std::vector<ActorWorld::LocatedEffect>& fx,
                                 const fs::path& asset_root, const EftList& efl, const EftTex& eft,
                                 WGPUTextureView color, WGPUTextureView depth,
                                 const Vec3& camFwd, float t) {
    if (fx.empty()) return;
    float tiltDeg = 90.0f, lift = 0.8f, scaleOv = -1.0f;   // flat + lifted (user: 90 closest, wants it higher/around the chest)
    if (const char* s = std::getenv("CLERIA_BARRIER_TILT")) tiltDeg = float(std::atof(s));
    if (const char* s = std::getenv("CLERIA_BARRIER_LIFT")) lift = float(std::atof(s));
    if (const char* s = std::getenv("CLERIA_BARRIER_SCALE")) { float v = float(std::atof(s)); if (v > 0) scaleOv = v; }
    const float trFlat = tiltDeg * 3.14159265f / 180.0f;
    Vec3 fwdH = {camFwd.x, 0, camFwd.z};
    const float fl = std::sqrt(fwdH.x * fwdH.x + fwdH.z * fwdH.z);
    if (fl > 1e-4f) { fwdH.x /= fl; fwdH.z /= fl; }
    const Vec3 rightH = normalize(cross(Vec3{0, 1, 0}, fwdH));
    // The retail ward is a FLAT ground ring PLUS a VERTICAL light beam (user's 1080p shot).
    // Those are separate layers of map_kekkai, so orient each by its aspect: a tall layer
    // (h > w) is the beam -> stand it UPRIGHT (world-up, camera-facing); a wide/square layer
    // is the ring -> lay it FLAT (raked by tiltDeg). upDir is the layer's vertical axis.
    const Vec3 upFlat = {fwdH.x * std::sin(trFlat), std::cos(trFlat), fwdH.z * std::sin(trFlat)};
    const Vec3 upBeam = {0, 1, 0};
    for (const ActorWorld::LocatedEffect& e : fx) {
        const BulletArt& art = bullet_art(g, asset_root, efl, eft, e.id);
        if (!art.ok()) continue;
        const float sc = scaleOv > 0 ? scaleOv : (e.a > 0.01f ? e.a : 1.0f);
        const int age = int(t * 60.0f) % std::max(1, art.life);
        for (const BulletArt::Layer& L : art.layers) {
            if (L.frames.empty()) continue;
            const bool beam = L.h > L.w * 1.3f;          // tall layer -> the vertical beam
            const Vec3 upDir = beam ? upBeam : upFlat;
            const int fi = std::min(int(L.frames.size()) - 1,
                                    int(std::size_t(age) * L.frames.size() /
                                        std::size_t(std::max(1, art.life))));
            const Ef2Frame& fr = L.frames[std::size_t(fi)];
            const float hw = L.w * sc * 0.5f, hh = L.h * sc * 0.5f;
            // The flat ring sits at the ward point (raised by `lift`); the beam STANDS ON the
            // ground and rises, so its centre is one half-height up from the floor.
            const Vec3 c = beam ? Vec3{e.x, e.y + hh, e.z} : Vec3{e.x, e.y + lift, e.z};
            RoomVertex verts[4];
            std::uint32_t idx[6] = {0, 1, 2, 0, 2, 3};
            auto V = [&](int n, float rx, float uy, float u, float v) {
                verts[n] = {c.x + rightH.x * rx * hw + upDir.x * uy * hh,
                            c.y + upDir.y * uy * hh,
                            c.z + rightH.z * rx * hw + upDir.z * uy * hh,
                            0, 1, 0, u, v, 255, 255, 255, 255};
            };
            V(0, -1, 1, fr.u0, fr.v0);  V(1, 1, 1, fr.u1, fr.v0);   // +upDir = top (up / raked away)
            V(2, 1, -1, fr.u1, fr.v1);  V(3, -1, -1, fr.u0, fr.v1); // -upDir = bottom (down / near)
            WGPUBuffer vb = make_buffer(g.device, g.queue, verts, sizeof(verts), WGPUBufferUsage_Vertex);
            WGPUBuffer ib = make_buffer(g.device, g.queue, idx, sizeof(idx), WGPUBufferUsage_Index);
            WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
            WGPURenderPassColorAttachment ca = {};
            ca.view = color; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDepthStencilAttachment da = {};
            da.view = depth; da.depthLoadOp = WGPULoadOp_Load; da.depthStoreOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor rp = {};
            rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
            rp.depthStencilAttachment = &da;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
            wgpuRenderPassEncoderSetPipeline(pass, g.fx_pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, g.ubind, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, L.tex.bind, 0, nullptr);
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
    }
}

struct HudVertex { float x, y, u, v; std::uint8_t r, g, b, a; };

// STATUS_512.DDS source rects (measured; build/hud_re/hud_atlas_spec.json).
struct Rc { float x, y, w, h; };
// The gold digit font is MONOSPACE: fixed 12x12 cells at (digit*12, 24).
//
// From the emitter itself (inside builder A, 0x0043e550..0x0043e7b1):
//   MULSS XMM0, [0x006bd948]   ; f = 12.0   -> u = digit * 12
//   MULSS XMM0, [0x006bd008]   ; f = 1/512
//   v = 24/512, du = dv = 12/512
// so the cell is 12px and the band is y=24 h=12 — which also independently confirms
// the band correction we measured off the atlas pixels (an earlier spec said y15 h22
// and dragged an orange bar into every digit).
//
// hud_atlas_spec.json's per-glyph x/w (0:x0 w11, 1:x15 w6, 2:x24 w11, ...) are the
// INK EXTENTS inside those cells, not sampling rects. Sampling by ink would shift
// every glyph. `digit % 10` only ever reaches cells 0..9, so the ':' and '/' cells
// (10, 11) and the whole white/blue set at x>=144 are unreachable from this loop.
static Rc digit_cell(int d) { return {float(d) * 12.0f, 24.0f, 12.0f, 12.0f}; }
// Character portrait units (round face + ring + item tab), by g_flags[150].
// 0/1=Yunica, 2=Hugo, 3=Toal (Adol @250,257 is unused in Origin).
static const Rc kPortrait[4] = {
    {364,257,116,100}, {364,257,116,100}, {250,362,116,100}, {364,362,116,100}};
// The bar cluster — every rect below is the ENGINE's own, read from the builders'
// UV immediates, not measured off the art.
//
// THE RULE THAT MATTERS: the engine's source rects are FULL CELLS WITH TRANSPARENT
// MARGINS. Measuring opaque-alpha extents under-reports them every time — it bit us
// on the digit cells (12px cells, ink inset), on the pointed bars (252x8 cells whose
// ink is only {2,1,248,7}), and on the housing. Use the cell, not the ink, or things
// shift by a couple of px and the tips lose their margin.
//
// The channel stack inside the housing (1024x768):
//   EXP orange  {142, 718, +ratio*230, 722}   <- the ONLY non-1:1 quad (see below)
//   HP blue     {120, 726, +(hp/max)*252, 734}
//   HP trail    {120+(hp/max)*252 .. +(lag/max)*252, 726, 734}
//   MP / skill  {120, 737, +(mp/mpmax)*252, 745}
static const Rc kBarHousing  = {0, 468, 271, 44};  // confirmed from quad 2's UVs
static const Rc kHpBlue      = {0,   0, 252, 8};   // HP fill      (ink is {2,1,248,7})
static const Rc kHpTrail     = {0,   8, 252, 8};   // HP damage trail
static const Rc kExpOrange   = {0,  16, 240, 8};   // EXP fill — 240 art -> 230 screen
static const Rc kMpGreen     = {0,  44, 252, 8};   // green mana/skill fill (measured: the
                                                   // pointed green bar stacked under HP/red/pink)
static const Rc kBarTick     = {302, 1,   3, 14};  // segment-divider tick; the atlas holds a
                                                   // strip of these at x277+25k (stride 25)
static const Rc kSlotFrame   = {472, 472, 40, 40}; // the 40x40 equipment slot frame

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
static const Ar kABarPlate = {115, 714, 386, 758};  // the housing / plate
static const Ar kAHpFill  = {120, 726, 372, 734};  // blue HP; max fill 252
static const Ar kAExpFill = {142, 718, 372, 722};  // orange EXP; max fill 230
static const Ar kAThinBar  = {142, 718, 372, 722};  // thin bar (EX/MP — name unconfirmed)
static const Ar kAMpFill   = {120, 737, 372, 745};  // green MP channel (below HP); max fill 252
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

    // Push one triangle into the STATUS sprite buffer: screen-px positions + atlas-PX UVs.
    // Used to build the EX/Boost arc as a pie-wedge fan sampling the atlas radially (H4).
    void tri(float ax, float ay, float au, float av,
             float bx, float by, float bu, float bv,
             float cx, float cy, float cu, float cv, float sw, float sh,
             std::uint8_t r, std::uint8_t gg, std::uint8_t b, std::uint8_t a) {
        auto NX = [&](float v){ return v / sw * 2.f - 1.f; };
        auto NY = [&](float v){ return 1.f - v / sh * 2.f; };
        sprites.push_back({NX(ax), NY(ay), au / atlas_w, av / atlas_h, r, gg, b, a});
        sprites.push_back({NX(bx), NY(by), bu / atlas_w, bv / atlas_h, r, gg, b, a});
        sprites.push_back({NX(cx), NY(cy), cu / atlas_w, cv / atlas_h, r, gg, b, a});
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

    // Add text horizontally centred on cx_frac*sw. (Uses `text`; width = len*cw*scale.)
    void text_centered(const std::string& s, float cx_frac, float y_px, float scale,
                       float sw, float sh, std::uint8_t r, std::uint8_t gg, std::uint8_t b,
                       std::uint8_t a) {
        text(s, cx_frac * sw - float(s.size()) * cw * scale * 0.5f, y_px, scale, sw, sh, r, gg, b, a);
    }

    // Flush ONLY the accumulated font/solid `verts` (no status bar, no "Room:" line) to
    // `view`, keeping what's already there (loadOp Load). For menus/titles that build their
    // own text via text()/text_centered()/rect() and then call this. Clears verts after.
    void draw_lines(Gpu& g, WGPUTextureView view) {
        const std::size_t nf = verts.size();
        if (!nf) return;
        const std::size_t bytes = nf * sizeof(HudVertex);
        if (bytes > vbuf_cap) {
            if (vbuf) wgpuBufferRelease(vbuf);
            WGPUBufferDescriptor bd = {};
            bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            bd.size = ((bytes + 4095) / 4096) * 4096;
            vbuf = wgpuDeviceCreateBuffer(g.device, &bd);
            vbuf_cap = bd.size;
        }
        wgpuQueueWriteBuffer(g.queue, vbuf, 0, verts.data(), bytes);
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPURenderPassColorAttachment ca = {};
        ca.view = view; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, 0, bytes);
        wgpuRenderPassEncoderDraw(pass, std::uint32_t(nf), 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(g.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
        verts.clear();
    }

    // The game's own digit emitter (builder A, 0x0043e550..0x0043e7b1).
    //
    //   cursor = right_x (337.5 for SP);  v = clamp(value, 0, 999999)
    //   do { digit = v % 10; emit {cursor-12, y, cursor, y+12} from cell (digit*12, 24)
    //        cursor -= 12; v /= 10; } while (v != 0 && ++n < 6)
    //
    // So it walks LEFTWARD from a fixed right edge, ones digit first — i.e. the number
    // is RIGHT-ALIGNED with NO leading zeros, and capped at 6 digits because the value
    // is clamped to 0xF423F (999999) at FUN_004fa290.
    //
    // `right_x`/`y` are authored 1024x768 coords; the caller maps them.
    void draw_number_right(const HudMap& m, int value, float right_x, float y,
                           float sw, float sh) {
        int v = value < 0 ? 0 : (value > 999999 ? 999999 : value);
        float cx = right_x;
        for (int n = 0; n < 6; ++n) {
            const Rc c = digit_cell(v % 10);
            const float x0 = m.x(cx - 12.0f), y0 = m.y(y);
            sprite(x0, y0, m.x(cx) - x0, m.y(y + 12.0f) - y0, c.x, c.y, c.w, c.h, sw, sh);
            cx -= 12.0f;
            v /= 10;
            if (v == 0) break;
        }
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

        // Advance the HP damage-lag (H3). Heal snaps the lag up to cur instantly (no red on
        // heal); damage lets cur drop and the lag decays down toward it, per tick. coeff ~1
        // is [UNKNOWN] — CLERIA_HPLAG scales the visible catch-up speed (default 0.05 so the
        // trail is watchable; the traced form is 0.2 * coeff * max).
        if (p.hp_lag < float(p.hp)) p.hp_lag = float(p.hp);
        else if (p.hp_lag > float(p.hp)) {
            static const float k = [] {
                if (const char* e = std::getenv("CLERIA_HPLAG")) { float v = std::atof(e); if (v > 0) return v; }
                return 0.006f;  // ~0.5 HP/frame at max 400 -> a big hit's trail drains over ~1s
            }();
            p.hp_lag -= 0.2f * k * float(p.max_hp);
            if (p.hp_lag < float(p.hp)) p.hp_lag = float(p.hp);
        }

        // NOT DRAWN: the four stacked 12px rows at x0=0 (y 620/632/644/656). Their
        // geometry is recovered, but their identity is not — an earlier pass called them
        // "equipment slots", yet they sit flush left with RUNTIME-COMPUTED widths, which
        // reads as four stacked bars, and the real game shows equipment as a horizontal
        // row beside the portrait. Drawing skill diamonds there was visibly wrong, so we
        // draw nothing until we know what they are.

        // The housing, then HP as the blue pointed bar in the UPPER channel. The
        // segmented MP channel below stays empty — that is what the real HUD shows,
        // and drawing HP into it (as we briefly did) misaligns it with the borders.
        sprite_a(m, kABarPlate, kBarHousing, sw, sh);

        // HP — the blue bar, quad 0x0043c788. Its ratio comes from the SAME globals
        // (0x006d5f20 / 0x006d5f28) that builder D prints as `%3d/%3d`, so the bar
        // and the `60/ 60` readout are provably one value.
        const float hp_frac = p.max_hp > 0
                                  ? std::max(0.f, std::min(1.f, float(p.hp) / p.max_hp)) : 0.f;
        // The red damage trail FIRST (quad 0x0043c5b2), at the lagging value, then the blue
        // current HP on top — so the red shows only in the [cur, lag] segment. Now that we
        // have the H3 lag model, this closes the long-standing "no lag model" gap.
        const float lag_frac = p.max_hp > 0
                                   ? std::max(0.f, std::min(1.f, p.hp_lag / p.max_hp)) : 0.f;
        if (lag_frac > hp_frac) {
            Ar red = kAHpFill;
            red.x1 = red.x0 + 252.0f * lag_frac;
            sprite_a(m, red, kHpBlue, sw, sh, 214, 46, 46, 255);  // red ghost behind
        }
        Ar hp = kAHpFill;
        hp.x1 = hp.x0 + 252.0f * hp_frac;
        sprite_a(m, hp, kHpBlue, sw, sh);                          // blue current HP on top

        // EXP for the current level — quad 6, the orange band. This is the ONLY HUD
        // quad that is NOT 1:1: 240px of art into 230px of screen, and 8px of art
        // squashed into 4px (2:1). That squash is why it reads thin next to the blue
        // bar — deliberate, not a mis-measurement.
        //
        // Its ratio is NOT driven by DAT_0076c1b4, and that is EXPECTED rather than
        // suspicious: **[EX] is not experience**. [EX] is the EX/BOOST GAUGE (max
        // 30000, Boost unlocks at 100%) — the same global builder C sweeps into the
        // arc around the portrait and pulses when full. EXPERIENCE-into-level is a
        // separate quantity, and this is its bar.
        Ar ex = kAExpFill;
        ex.x1 = ex.x0 + 230.0f * std::max(0.f, std::min(1.f, p.exp_pct / 100.0f));
        sprite_a(m, ex, kExpOrange, sw, sh);

        // MP / mana bar (H1). The green fill is the REAL STATUS_512 art at {0,44,252,8}
        // — the pointed green bar stacked below the blue HP / red-trail / pink rows in
        // the atlas's bar column (measured off the pixels; the earlier {252,36,...}
        // rainbow was just the wrong cell). Over it go the SEGMENT DIVIDERS the user
        // asked for: the atlas carries beveled tick sprites at x277+25k (stride 25), and
        // a 252px bar / 25 ≈ 10 cells, so 9 interior ticks split it into ~10 blocks.
        {
            const float mp = std::max(0.f, std::min(1.f, p.mp_pct / 100.0f));
            Ar mpf = kAMpFill;
            mpf.x1 = mpf.x0 + 252.0f * mp;
            sprite_a(m, mpf, kMpGreen, sw, sh);                 // green fill (real art)
            for (int k = 1; k < 10; ++k) {                      // 9 dividers -> ~10 cells
                const float cx = kAMpFill.x0 + 25.2f * float(k);
                Ar tick = {cx - 1.0f, kAMpFill.y0, cx + 1.0f, kAMpFill.y1};
                sprite_a(m, tick, kBarTick, sw, sh);
            }
        }

        // Character portrait (drawn over the plate's left end, as the game does).
        const int ci = (p.character >= 0 && p.character < 4) ? p.character : 1;
        sprite_a(m, kAPortrait, kPortraitReal[ci], sw, sh);

        // EX / Boost C-arc (H4, build/hud_re/HUD_BOOST_ARC.md): a pie-wedge fan that reveals the
        // arc graphic from STATUS_512 as the gauge fills. Center (44,719) authored, R63, sweeping
        // -ratio*190deg from 315deg; atlas sample center (44,410). Exact geometry [C-DIS]; if the
        // atlas region reads blank in CleriaCore's STATUS_512 the boost just won't show (no harm).
        {
            const float ratio = std::max(0.f, std::min(1.f, p.boost / 30000.0f));
            if (ratio > 0.001f) {
                const float DEG = 3.14159265f / 180.0f;
                // H4 traced start=315deg, sweep=190deg. The user reports it doesn't fill fully, so
                // expose both for dialing until we verify against the real arc visually.
                auto envf = [](const char* k, float d){ if (const char* e = std::getenv(k)) { float v = std::atof(e); if (v != 0) return v; } return d; };
                const float th0 = envf("CLERIA_BOOST_START", 315.0f) * DEG;
                const float sweep = ratio * envf("CLERIA_BOOST_SWEEP", 190.0f) * DEG;
                const int N = 24;
                const float apx = m.x(44.0f), apy = m.y(719.0f);   // apex (center), screen px
                auto rim = [&](float th, float& x, float& y, float& u, float& v) {
                    const float s = std::sin(th), c = std::cos(th);
                    x = m.x(44.0f + 63.0f * s); y = m.y(719.0f - 63.0f * c);  // screen px
                    u = 44.0f + 63.0f * s;      v = 410.0f - 63.0f * c;       // atlas px
                };
                const std::uint8_t t = ratio >= 0.999f ? 255 : 210;   // brighten when Boost-ready
                float px, py, pu, pv; rim(th0, px, py, pu, pv);
                for (int i = 1; i <= N; ++i) {
                    float nx, ny, nu, nv; rim(th0 - sweep * (float(i) / float(N)), nx, ny, nu, nv);
                    tri(apx, apy, 44.0f, 410.0f, px, py, pu, pv, nx, ny, nu, nv, sw, sh, t, t, t, 255);
                    px = nx; py = ny; pu = nu; pv = nv;
                }
            }
        }

        // The four 40x40 equipment slot frames — quad 12, recovered as a LOOP:
        //   x = 126 + 40*i  (126/166/206/246), y = 676..716
        //   UV (0.921875,0.921875)..(1,1) = atlas px (472,472)..(512,512)
        // Builder D blits each equipped item's icon on top at (132+40i, 682) — a 6px
        // inset — from a per-item texture handle array (DAT_0074a958[itemId]), NOT
        // from the atlas. Unequipped slots are drawn greyed (tint & 0xff303030). We
        // draw the empty frames; the icons need the item/equipment model.
        for (int i = 0; i < 4; ++i) {
            const Ar d{126.0f + 40.0f * i, 676.0f, 166.0f + 40.0f * i, 716.0f};
            sprite_a(m, d, kSlotFrame, sw, sh);
        }

        // The level, in the atlas's own gold glyph font (y15..36, proportional widths,
        // measured from STATUS_512.DDS — see build/hud_re/hud_atlas_spec.json).
        //
        // The game's HUD is quads from STATUS_512.DDS and NOTHING ELSE — it contains no
        // text at all, which the RE confirms twice over: builder A emits 12 quads and
        // none is a label, and there are no HUD strings in the exe to pivot from. So the
        // "Lv" / "SP nnn" / "EXP n.n%" captions we used to draw here in our own bitmap
        // font were pure invention, and they are gone.
        //
        // The POSITION below is still ours: the digit emitter is builder B (FUN_0043e800)
        // or D (FUN_0043fe00), neither of which contains a single MapCoord call, and
        // neither is decoded. The glyphs are real; where they belong is not.
        // SP, at the game's real place: right edge 349.5, y 752..764, right-aligned.
        // (The emitter's cursor starts at 337.5 = the LEFT edge of the ones digit.)
        draw_number_right(m, p.sp, 349.5f, 752.0f, sw, sh);
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
    const float fl = floor_at(phys_mesh(room), c.x, c.z, room.mesh.bmax.y + D);
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
static int render_play(Gpu& g, Room& room, const fs::path& out,
                       std::uint32_t W, std::uint32_t H, const fs::path& asset_root,
                       float yaw, float pitch, float dist, float worldH,
                       std::uint32_t dir) {
    const Vec3 c = {(room.mesh.bmin.x + room.mesh.bmax.x) * 0.5f, 0,
                    (room.mesh.bmin.z + room.mesh.bmax.z) * 0.5f};
    const float D = room_diag(room);
    const float fl = floor_at(phys_mesh(room), c.x, c.z, room.mesh.bmax.y + D);
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
    // CLERIA_SCROLL="x,y,z": inject a cutscene camera Scroll_MoveTo (instant) to verify the
    // event-camera override headlessly — the framing should re-centre on that world point.
    if (const char* sc = std::getenv("CLERIA_SCROLL")) {
        float x, y, z; if (std::sscanf(sc, "%f,%f,%f", &x, &y, &z) == 3) scroll_moveto(x, y, z, 0);
    }
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
    {   // The floor actually BENEATH the spawn: cap the search just above the feet,
        // otherwise floor_at returns the highest surface in the room (a balcony 17
        // units up), which tells us nothing about where the player lands.
        const float ceil = feet.y + 0.5f;
        const float fr = floor_at(room.mesh, feet.x, feet.z, ceil);
        const float fc = room.coll.indices.empty()
                             ? kNoFloor : floor_at(room.coll, feet.x, feet.z, ceil);
        std::printf("  floor under spawn: render=%.3f  collision=%.3f  (spawn y=%.3f)\n",
                    fr, fc, feet.y);
    }
    // CLERIA_SCROLL_TICKS=<n>: pre-advance the UV-scroll phase by n ticks before the
    // shot. --play renders a single frame and never ticks, so without this the water
    // is always frozen at phase 0 — this is how to see it move headlessly.
    if (const char* st = std::getenv("CLERIA_SCROLL_TICKS"))
        if (!room.scrolls.empty()) advance_scroll(g, room, std::atoi(st));
    draw(g, room, cview, dview);
    // Let the game's own animation script pick the sprite, as the live path does:
    // C_010 is Yunica's script, and with flag 161 clear it selects the GAMEPLAY set
    // (anime_File(15) = C_111), not the cutscene set we used to hardcode here.
    CharSprite ch;
    Aso shot_aso = Aso::load(asset_root / "ANI" / "C_010.ASO");
    AsoVm shot_vm;
    shot_vm.bind(&shot_aso);
    std::int32_t shot_flags[0x200] = {0};
    shot_vm.set_flags(shot_flags, 0x200);
    shot_vm.tick();
    const char* shot_want = aso_aia_name(shot_vm.file_id());
    const std::string shot_stem = (shot_want && *shot_want) ? std::string(shot_want)
                                                            : std::string("C_102");
    ShadowFx shadows; shadows.init(g, asset_root);
    bool have = ch.load(g, asset_root / "ANI" / (shot_stem + ".AIA"), 0);
    if (have) {
        const int img = ch.find_tag(std::uint32_t(shot_vm.anim_tag()));
        if (img >= 0) ch.set_anim(g, std::uint32_t(img), std::uint32_t(shot_vm.frame()));
        const std::uint32_t d = dir < ch.ndir ? dir : 0;
        draw_shadow(g, cview, dview, shadows.tex, feet, feet.y, 0.6f);
        draw_character(g, cview, dview, feet, worldH, ch.dirs[d], cam.forward(), cam.pos);
        std::printf("play: [aso] file(%d)=%s tag=%d frame=%d | sprite %ux%u dir %u/%u, "
                    "worldH %.2f, cam dist %.2f\n",
                    shot_vm.file_id(), shot_stem.c_str(), shot_vm.anim_tag(), shot_vm.frame(),
                    ch.dirs[d].w, ch.dirs[d].h, d, ch.ndir, worldH, dist);
    } else std::printf("play: character AIA load FAILED (%s)\n", shot_stem.c_str());

    // Enemies: the engine's real populate (run S_xxxx.XSO -> op 0xa0 SCT + op 0x12c
    // scripted), the same path play mode uses. CLERIA_ENEMY=1 dumps the roster — the
    // only headless view of it, since the interactive loop needs a window.
    {
        EnemyFx efx;
        const CastTable casts = CastTable::load(asset_root / "MISC" / "CASTINFO.DAT");
        const EnemyTable etab = EnemyTable::load(asset_root / "MISC" / "ENEMYINFO.DAT");
        const GameInfo ginfo = GameInfo::load(asset_root / "MISC" / "GAMEINFO.DAT");
        const EexTable eex = EexTable::load(asset_root / "MISC" / "EEX.DAT");
        ActorWorld world({&casts, &etab, &ginfo, &eex});
        std::int32_t flags[0x200] = {0};
        flags[150] = 1;  // character = Yunica (this headless shot has no save state)
        populate_room(world, room, flags);
        efx.init_from_world(g, asset_root, world, casts);
        std::vector<EnemyRender> list = build_enemy_render(world, casts, efx);
        if (std::getenv("CLERIA_ENEMY"))
            for (const EnemyRender& e : list)
                std::printf("play: %-14s (%-6s) lv%-3d HP %6.0f  ATK %5.0f  DEF %5.0f\n",
                            e.display.c_str(), e.stem.c_str(), e.actor->level,
                            e.actor->max_hp, e.actor->atk, e.actor->def);
        if (std::getenv("CLERIA_BARRIER"))
            for (const Actor& a : world.actors())
                std::printf("barrier: id=%d cast=%d grp1=%d hp=%.0f alive=%d %s\n",
                            a.instance_id, a.cast_id, a.tag.in_group(1), a.hp, a.alive(),
                            casts.get(a.cast_id) ? casts.sprite_stem(a.cast_id).c_str() : "?");
        draw_enemies(g, cview, dview, efx, list, cam.forward(), cam.pos, 0.0f, shadows.tex);
        // Barrier ward + any other 0x11a-placed effects (headless preview of world.effects()).
        if (std::getenv("CLERIA_BARRIER"))
            for (const auto& e : world.effects())
                std::printf("barrier: effect id=%d '%s' at (%.1f,%.1f,%.1f) a=%.2f\n",
                            e.id, e.name.c_str(), e.x, e.y, e.z, e.a);
        {
            EftList efl2; EftTex eft2;
            if (efl2.load(asset_root) && eft2.load(asset_root))
                draw_located_effects(g, world.effects(), asset_root, efl2, eft2, cview, dview,
                                     cam.forward(), 0.30f);
        }
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
    // CLERIA_DLG=1: preview the cutscene dialogue box (FACEWIN0 3-slice + name/text) so its
    // slice-UVs/position can be tuned headlessly against reference/realgame/dialogue_box_*.png.
    // (Layout check — uses the HUD font here, not Felghana; TitleFx::draw_dialogue is the real path.)
    if (std::getenv("CLERIA_DLG")) {
        Dds fwd = load_dds(asset_root / "IMAGE" / "FACEWIN0.DDS");
        SpriteTex fw = fwd.ok() ? upload_rgba(g, fwd.width, fwd.height, fwd.data.data()) : SpriteTex{};
        if (fw.tex) {
            write_mvp(g, Mat4::identity());
            const float sw = float(W), sh = float(H);
            const float by1 = sh, by0 = sh - sh * (224.0f / 768.0f);
            constexpr float vc = 0.5f, uL = 0.19f, uR = 0.586f, uEnd = 0.809f;
            float curl = (by1 - by0) * (uL * 512.0f) / (vc * 512.0f);
            auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
            auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
            draw_screen_image(g, cview, dview, false, fw, NX(0),        NY(by0), NX(curl),    NY(by1), 0.15f, 0.0f, 0.0f, uL,   vc);
            draw_screen_image(g, cview, dview, false, fw, NX(curl),     NY(by0), NX(sw-curl), NY(by1), 0.15f, uL,   0.0f, uR,   vc);
            draw_screen_image(g, cview, dview, false, fw, NX(sw-curl),  NY(by0), NX(sw),      NY(by1), 0.15f, uR,   0.0f, uEnd, vc);
            const float boxH = by1 - by0, tx = sw * 0.15f;
            hud.text("Boy in Black", tx, by0 + boxH * 0.14f, 2.0f, sw, sh, 240, 224, 192, 255);
            hud.text("That voice... it can't be. Is that really you?", tx, by0 + boxH * 0.45f, 2.6f, sw, sh, 255, 255, 255, 255);
        }
    }
    // CLERIA_CAPTION=map_name03: preview the area-name card (op 0xe2 loads IMAGE/<name>.dds).
    if (const char* cap = std::getenv("CLERIA_CAPTION")) {
        std::string nm = cap; for (char& c : nm) c = char(std::toupper((unsigned char)c));
        Dds cd = load_dds(asset_root / "IMAGE" / (nm + ".DDS"));
        if (cd.ok()) {
            SpriteTex ct = upload_rgba(g, cd.width, cd.height, cd.data.data());
            write_mvp(g, Mat4::identity());
            const float sw = float(W), sh = float(H);
            const float cw2 = sw * 0.6f, chh = cw2 * float(cd.height) / float(cd.width);
            const float cx = (sw - cw2) * 0.5f, cy = sh * 0.16f;
            auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
            auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
            draw_screen_image(g, cview, dview, false, ct, NX(cx), NY(cy), NX(cx + cw2), NY(cy + chh), 0.1f);
        }
    }
    hud.render(g, cview, W, H, room.name, 0.016f, &pv);
    if (g_play_overlay) g_play_overlay(g, cview, dview, W, H);   // e.g. the real dialogue box (--dialogue)

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


// ---- menu font --------------------------------------------------------------
// A proportional serif for menu/title text, rasterised from a Windows system font via GDI --
// the engine renders its menu text from a system font too (MENUS.md §3.1), so this is the
// faithful path (and needs no bundled font/library). One antialiased coverage atlas of ASCII
// 32..126 at a base pixel height; callers ask for a target height and it scales. White glyphs
// (alpha = coverage) tinted by the vertex colour, through the same shader as the HUD font.
struct MenuFont {
    struct Glyph { float u0=0, v0=0, u1=0, v1=0; int w=0, h=0, adv=0, bx=0, by=0; };
    Glyph gl[95];
    int base = 44, ascent = 0, lineH = 0, weight_ = FW_NORMAL;
    std::string face_name = "Constantia";
    WGPURenderPipeline pipeline{};
    WGPUBindGroup bind{};
    WGPUSampler samp{};
    WGPUBuffer vbuf{};
    std::size_t vbuf_cap = 0;
    std::vector<HudVertex> verts;
    bool ok = false;

    bool init(Gpu& g, WGPUTextureFormat color, const fs::path& fontFile = {}, int emPx = 46,
              int weight = FW_NORMAL) {
        // The real menu font is Felghana, the game's bundled TrueType Release/yso_ins04.dat
        // (build/menu_re/MENU_LAYOUT.md). Load it as a private, process-local font; if it isn't
        // available fall back to a system serif. The engine's LOGFONT: weight 400, DEFAULT_CHARSET.
        face_name = "Constantia";
        if (!fontFile.empty()) {
            std::vector<std::uint8_t> bytes = read_bytes(fontFile);
            DWORD n = 0;
            if (!bytes.empty() &&
                AddFontMemResourceEx(bytes.data(), DWORD(bytes.size()), nullptr, &n) && n)
                face_name = "Felghana";
        }
        // CLERIA_FONT / CLERIA_FONT_W override the face / weight (off-window font comparison).
        if (const char* f = std::getenv("CLERIA_FONT")) face_name = f;
        if (const char* w = std::getenv("CLERIA_FONT_W")) weight = std::atoi(w);
        weight_ = weight;

        // Pipeline + sampler, built once (the HUD shader: atlas * vertex colour).
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
        wgpuShaderModuleRelease(sm);
        ok = build_atlas(g, emPx);
        return ok;
    }

    // (Re)rasterise the glyph atlas at exactly emPx device pixels -- matching the game's native GDI
    // rasterisation (its LOGFONT height = renderH*32/768) so the hinting/AA is crisp, rather than a
    // downscale of a fixed-size atlas. Call again on window resize; releases the previous atlas.
    bool build_atlas(Gpu& g, int emPx) {
        base = std::max(8, emPx);
        HDC hdc = CreateCompatibleDC(nullptr);
        if (!hdc) return false;
        // Match the engine's LOGFONT (build/menu_re/FONT_RASTER.md): weight 400, OUT_TT_PRECIS,
        // PROOF_QUALITY. It rasterises at the exact device-pixel size, so `base` is that size.
        HFONT font = CreateFontA(-base, 0, 0, 0, weight_, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, PROOF_QUALITY,
                                 FF_ROMAN, face_name.c_str());
        HGDIOBJ oldf = SelectObject(hdc, font);
        TEXTMETRICA tm; GetTextMetricsA(hdc, &tm);
        ascent = tm.tmAscent; lineH = tm.tmHeight;
        MAT2 mat = {{0, 1}, {0, 0}, {0, 0}, {0, 1}};
        struct Raw { std::vector<std::uint8_t> cov; int w = 0, h = 0; };
        std::vector<Raw> raws(95);
        const int atlasW = 1024;
        int x = 1, y = 1, rowH = 0;
        for (int i = 0; i < 95; ++i) {
            const UINT ch = 32 + i;
            GLYPHMETRICS gm = {};
            // The engine rasterises with GGO_GRAY4_BITMAP (17 levels 0..16), not GRAY8 (FONT_RASTER.md).
            const DWORD sz = GetGlyphOutlineA(hdc, ch, GGO_GRAY4_BITMAP, &gm, 0, nullptr, &mat);
            gl[i].adv = gm.gmCellIncX; gl[i].bx = gm.gmptGlyphOrigin.x; gl[i].by = gm.gmptGlyphOrigin.y;
            Raw r;
            if (sz != GDI_ERROR && sz > 0) {
                std::vector<std::uint8_t> buf(sz);
                GetGlyphOutlineA(hdc, ch, GGO_GRAY4_BITMAP, &gm, sz, buf.data(), &mat);
                r.w = gm.gmBlackBoxX; r.h = gm.gmBlackBoxY;
                const int pitch = (r.w + 3) & ~3;   // GDI pads glyph rows to a DWORD
                r.cov.assign(std::size_t(r.w) * r.h, 0);
                for (int yy = 0; yy < r.h; ++yy)
                    for (int xx = 0; xx < r.w; ++xx) {
                        // Engine mapping: drop coverage 0/1, else 4-bit alpha (c-1)/15 -> 8-bit.
                        const int v = buf[yy * pitch + xx];        // 0..16 coverage
                        r.cov[yy * r.w + xx] = std::uint8_t(v <= 1 ? 0 : (v - 1) * 255 / 15);
                    }
            }
            gl[i].w = r.w; gl[i].h = r.h;
            if (x + r.w + 1 > atlasW) { x = 1; y += rowH + 1; rowH = 0; }
            gl[i].u0 = float(x); gl[i].v0 = float(y);   // pixel pos, resolved to UV below
            x += r.w + 1; rowH = std::max(rowH, r.h);
            raws[i] = std::move(r);
        }
        const int atlasH = y + rowH + 1;
        std::vector<std::uint8_t> atlas(std::size_t(atlasW) * atlasH * 4, 0);
        for (int i = 0; i < 95; ++i) {
            const int px = int(gl[i].u0), py = int(gl[i].v0);
            const Raw& r = raws[i];
            for (int yy = 0; yy < r.h; ++yy)
                for (int xx = 0; xx < r.w; ++xx) {
                    const std::size_t o = (std::size_t(py + yy) * atlasW + (px + xx)) * 4;
                    atlas[o] = 255; atlas[o + 1] = 255; atlas[o + 2] = 255;
                    atlas[o + 3] = r.cov[yy * r.w + xx];
                }
            gl[i].u0 = px / float(atlasW);          gl[i].v0 = py / float(atlasH);
            gl[i].u1 = (px + r.w) / float(atlasW);  gl[i].v1 = (py + r.h) / float(atlasH);
        }
        SelectObject(hdc, oldf); DeleteObject(font); DeleteDC(hdc);

        WGPUTextureDescriptor td = {};
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        td.dimension = WGPUTextureDimension_2D; td.size = {std::uint32_t(atlasW), std::uint32_t(atlasH), 1};
        td.format = WGPUTextureFormat_RGBA8Unorm; td.mipLevelCount = 1; td.sampleCount = 1;
        WGPUTexture tex = wgpuDeviceCreateTexture(g.device, &td);
        WGPUTexelCopyTextureInfo dst = {}; dst.texture = tex; dst.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout lay = {}; lay.bytesPerRow = atlasW * 4; lay.rowsPerImage = atlasH;
        WGPUExtent3D ext = {std::uint32_t(atlasW), std::uint32_t(atlasH), 1};
        wgpuQueueWriteTexture(g.queue, &dst, atlas.data(), atlas.size(), &lay, &ext);
        WGPUTextureView view = wgpuTextureCreateView(tex, nullptr);
        WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry e[2] = {};
        e[0].binding = 0; e[0].textureView = view;
        e[1].binding = 1; e[1].sampler = samp;
        WGPUBindGroupDescriptor bd = {}; bd.layout = bgl; bd.entryCount = 2; bd.entries = e;
        WGPUBindGroup nb = wgpuDeviceCreateBindGroup(g.device, &bd);
        if (bind) wgpuBindGroupRelease(bind);   // drop the previous atlas (bind holds its view+tex)
        bind = nb;
        wgpuTextureViewRelease(view); wgpuTextureRelease(tex);
        ok = true;
        return true;
    }

    // Text width in px at a given em-height (pxH).
    float measure(const std::string& s, float pxH) const {
        const float sc = pxH / float(base);
        float w = 0;
        for (unsigned char c : s) if (c >= 32 && c <= 126) w += gl[c - 32].adv * sc;
        return w;
    }
    // Append `s` with its top at py_top (px), left edge at px_left. pxH = em height in px.
    void text(const std::string& s, float px_left, float py_top, float pxH, float sw, float sh,
              std::uint8_t r, std::uint8_t gg, std::uint8_t b, std::uint8_t a) {
        const float sc = pxH / float(base);
        const float baseline = py_top + ascent * sc;
        auto NX = [&](float v) { return v / sw * 2.f - 1.f; };
        auto NY = [&](float v) { return 1.f - v / sh * 2.f; };
        float x = px_left;
        for (unsigned char c : s) {
            if (c < 32 || c > 126) { x += base * 0.3f * sc; continue; }
            const Glyph& gp = gl[c - 32];
            if (gp.w > 0 && gp.h > 0) {
                const float gx = x + gp.bx * sc, gy = baseline - gp.by * sc;
                const float gw = gp.w * sc, gh = gp.h * sc;
                const float x0 = NX(gx), x1 = NX(gx + gw), y0 = NY(gy), y1 = NY(gy + gh);
                HudVertex q0{x0, y0, gp.u0, gp.v0, r, gg, b, a}, q1{x1, y0, gp.u1, gp.v0, r, gg, b, a};
                HudVertex q2{x1, y1, gp.u1, gp.v1, r, gg, b, a}, q3{x0, y1, gp.u0, gp.v1, r, gg, b, a};
                verts.push_back(q0); verts.push_back(q1); verts.push_back(q2);
                verts.push_back(q0); verts.push_back(q2); verts.push_back(q3);
            }
            x += gp.adv * sc;
        }
    }
    void text_centered(const std::string& s, float cx_frac, float py_top, float pxH,
                       float sw, float sh, std::uint8_t r, std::uint8_t gg, std::uint8_t b,
                       std::uint8_t a) {
        text(s, cx_frac * sw - measure(s, pxH) * 0.5f, py_top, pxH, sw, sh, r, gg, b, a);
    }
    // Flush accumulated glyphs onto `view` (loadOp Load). Clears after.
    void draw(Gpu& g, WGPUTextureView view) {
        const std::size_t nf = verts.size();
        if (!nf) return;
        const std::size_t bytes = nf * sizeof(HudVertex);
        if (bytes > vbuf_cap) {
            if (vbuf) wgpuBufferRelease(vbuf);
            WGPUBufferDescriptor bd = {};
            bd.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            bd.size = ((bytes + 4095) / 4096) * 4096;
            vbuf = wgpuDeviceCreateBuffer(g.device, &bd);
            vbuf_cap = bd.size;
        }
        wgpuQueueWriteBuffer(g.queue, vbuf, 0, verts.data(), bytes);
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(g.device, nullptr);
        WGPURenderPassColorAttachment ca = {};
        ca.view = view; ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        ca.loadOp = WGPULoadOp_Load; ca.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor rp = {}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vbuf, 0, bytes);
        wgpuRenderPassEncoderDraw(pass, std::uint32_t(nf), 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(enc, nullptr);
        wgpuQueueSubmit(g.queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd); wgpuCommandEncoderRelease(enc);
        verts.clear();
    }
};

// ---- title screen -----------------------------------------------------------
// Load the title assets (build/menu_re/MENUS.md): TITLE_000.DDS is the 1024^2 logo,
// MENU/BACK00.DDS the background. Rendered as screen-space quads via draw_screen_image.
struct TitleFx {
    SpriteTex splash, pillar, cursor, altpillar;   // altpillar = golden goddess side-art (char-select/menus)
    SpriteTex curGlow;         // baked selection glow: white bar, vertical alpha gradient, drawn ADDITIVE over text
    SpriteTex frame, chrsel;   // BACK00 ornate frame; CHRSELECT atlas (bars/faces/banner)
    SpriteTex dialog;          // DIALOG0 blue scroll frame for the New-Game choice popups
    SpriteTex facewin;         // FACEWIN0 — the in-engine cutscene DIALOGUE box (bottom bar)
    SpriteTex opbtn, plaque;   // OPBTN atlas (Option-screen buttons/checkboxes) + OPTION "Option" plaque
    SpriteTex equipPlaque, wstatus, wequip, wdetail;   // pause-book Equip: plaque + status + grid + detail panel
    SpriteTex invPlaque, winvent;                      // pause-book Item page: "Inventory" plaque + 6x5 grid
    SpriteTex topicPlaque, topicBtn[3], topicCur;      // pause-book Topic page: plaque + Character/Monster/Manual + cursor glow
    SpriteTex shopBg;                                  // SHOPPILLAR16-9: widescreen margin layer
    SpriteTex shopBack, shopFeena;                     // Bonus Shop: Back01.dds backdrop + t_400a.dds Feena key-art
    SpriteTex loadPlaque, savePlaque, wsvld;           // pause-book Save/Load: plaques + W_SVLD slot/tab plate
    SpriteTex square;          // SQUARE.dds (tex 10): the item/equip grid cell tile + cursor cell
    SpriteTex blueHi;          // baked equipped/selected highlight quad (ARGB 0x4259999a)
    std::unordered_map<int, SpriteTex> iconCache;   // item-id -> MENU/ICON/<stem>.DDS (lazy, cached)
    SpriteTex portrait[3];     // CHRSELECT_PORTRAIT1/2/3 = Yunica / Hugo / Toal key art
    SpriteTex dim;             // baked solid: background dimmer behind popups
    CharSprite chibi[3];       // (legacy) AIA walking sprite -- superseded by chibiPose below
    SpriteTex chibiPose[3];    // ChrSelect_Chr{1,2,3}.dds: the dedicated fixed-pose chibi art (code-traced)
    int chibi_cur[3] = {-1, -1, -1};   // frame currently decoded per character
    SpriteTex faceClip[3];     // each char's face pre-clipped to the bar's arrow shape
    bool init(Gpu& g, const fs::path& asset_root) {
        // Background dimmer for the popups (RGBA, alpha baked in -- the char shader blends by
        // texture alpha). The popup PANEL is the real DIALOG0 asset, below.
        auto solid = [&](std::uint8_t r, std::uint8_t gg, std::uint8_t b, std::uint8_t a) {
            std::uint8_t px[4 * 4 * 4];
            for (int i = 0; i < 16; ++i) { px[i*4]=r; px[i*4+1]=gg; px[i*4+2]=b; px[i*4+3]=a; }
            return upload_rgba(g, 4, 4, px);
        };
        dim = solid(0, 2, 6, 120);
        // Selection glow (TITLE_LAYOUT.md §1 / UI_BLEND_FONT.md): the engine's menu cursor is the
        // chrome texture DAT_00749d68 (DDS identity [UNKNOWN]) drawn ADDITIVE (renderstate 0x40c1)
        // OVER the text, white/grey with a VERTICAL alpha gradient 0.125 (top) -> 0.875 -> 1.0
        // (bottom). Bake that as a small white bar carrying the gradient in its alpha, plus a soft
        // horizontal fade at the far ends so it reads as a glow band, not a hard rectangle.
        {
            // Overall additive intensity: the engine's tint is white/GREY, so the band brightens
            // the row without obliterating the (already near-white) selected glyphs under it. ~0.42
            // reads as the reference's soft highlight; CLERIA_CUR_A tunes it against the grab.
            float kGlow = 0.35f;
            if (const char* e = std::getenv("CLERIA_CUR_A")) kGlow = float(std::atof(e));
            const int GW = 96, GH = 32;
            std::vector<std::uint8_t> px(std::size_t(GW) * GH * 4);
            for (int y = 0; y < GH; ++y) {
                const float t = float(y) / float(GH - 1);           // 0 top .. 1 bottom
                // piecewise 0.125 -> 0.875 (mid) -> 1.0 (bottom)
                const float va = (t < 0.5f) ? (0.125f + (0.875f - 0.125f) * (t / 0.5f))
                                            : (0.875f + (1.0f - 0.875f) * ((t - 0.5f) / 0.5f));
                for (int x = 0; x < GW; ++x) {
                    const float u = float(x) / float(GW - 1);
                    // soft ends: fade the outer ~16% each side
                    float ha = 1.0f;
                    if (u < 0.16f) ha = u / 0.16f;
                    else if (u > 0.84f) ha = (1.0f - u) / 0.16f;
                    std::uint8_t* o = &px[(std::size_t(y) * GW + x) * 4];
                    o[0] = o[1] = o[2] = 255;
                    o[3] = std::uint8_t(std::clamp(va * ha * kGlow, 0.0f, 1.0f) * 255.0f);
                }
            }
            curGlow = upload_rgba(g, GW, GH, px.data());
        }
        Dds s = load_dds(asset_root / "IMAGE" / "TITLE_000.DDS");        // the full splash art
        Dds p = load_dds(asset_root / "MENU" / "TITLEPILLAR16-9.DDS");   // title widescreen side fill
        Dds ap = load_dds(asset_root / "MENU" / "ALTPILLAR16-9.DDS");    // golden goddess side fill (menus)
        Dds c = load_dds(asset_root / "IMAGE" / "MENUCURSOR.DDS");       // scroll-curl selection bar
        Dds fr = load_dds(asset_root / "MENU" / "BACK00.DDS");           // ornate menu frame
        Dds cs = load_dds(asset_root / "MENU" / "CHRSELECT.DDS");        // char-select atlas
        Dds dg = load_dds(asset_root / "IMAGE" / "DIALOG0.DDS");         // popup scroll frame
        { Dds fw = load_dds(asset_root / "IMAGE" / "FACEWIN0.DDS");      // cutscene dialogue box
          if (fw.ok()) facewin = upload_rgba(g, fw.width, fw.height, fw.data.data()); }
        if (s.ok()) splash = upload_rgba(g, s.width, s.height, s.data.data());
        if (p.ok()) pillar = upload_rgba(g, p.width, p.height, p.data.data());
        if (ap.ok()) altpillar = upload_rgba(g, ap.width, ap.height, ap.data.data());
        // MENUCURSOR is an ornate pill OUTLINE (bright silver lines + curls, transparent interior),
        // blue-tinted. The engine draws it WHITE. Desaturate to the max channel and lift the bright
        // lines to white while keeping the dark inner edge dark (that edge is what gives the thin
        // lines contrast on the bright splash -- flattening to pure white made them vanish).
        if (c.ok()) {
            // The engine's selection pill is a bright-WHITE but FAINT glow, not an opaque silver
            // outline: push the lines to white (whitening >100%) and drop the opacity hard. A strong
            // opaque pill over-darkens the lit row vs the reference (title_pixeldiff.py).
            int white = 340, calpha = 200;   // whitening %, alpha scale % (bright-white, near-opaque)
            if (const char* e = std::getenv("CLERIA_CUR_WHITE")) white = std::atoi(e);
            if (const char* e = std::getenv("CLERIA_CUR_ALPHA")) calpha = std::atoi(e);
            for (std::size_t i = 0; i + 3 < c.data.size(); i += 4) {
                const int v = std::max(c.data[i], std::max(c.data[i + 1], c.data[i + 2]));
                const std::uint8_t o = std::uint8_t(std::min(255, v * white / 100));
                c.data[i] = c.data[i + 1] = c.data[i + 2] = o;
                c.data[i + 3] = std::uint8_t(std::min(255, c.data[i + 3] * calpha / 100));
            }
            cursor = upload_rgba(g, c.width, c.height, c.data.data());
        }
        if (fr.ok()) frame = upload_rgba(g, fr.width, fr.height, fr.data.data());
        if (cs.ok()) chrsel = upload_rgba(g, cs.width, cs.height, cs.data.data());
        if (dg.ok()) dialog = upload_rgba(g, dg.width, dg.height, dg.data.data());
        { Dds ob = load_dds(asset_root / "MENU" / "OPBTN.DDS");    // Option-screen button/checkbox atlas
          if (ob.ok()) opbtn = upload_rgba(g, ob.width, ob.height, ob.data.data()); }
        { Dds op = load_dds(asset_root / "MENU" / "OPTION.DDS");   // "Option" title plaque (2 states)
          if (op.ok()) plaque = upload_rgba(g, op.width, op.height, op.data.data()); }
        // Pause-book Equip page (PAUSEBOOK.md §3): the "Equip" plaque, the left Status panel bg
        // (w_Status), and the right equip grid plate (w_Equip, WEAPON/ARMOR/BOOTS/ACCESSORIES/SKILLS).
        { Dds e = load_dds(asset_root / "MENU" / "EQUIP.DDS");
          if (e.ok()) equipPlaque = upload_rgba(g, e.width, e.height, e.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "W_STATUS.DDS");
          if (w.ok()) wstatus = upload_rgba(g, w.width, w.height, w.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "W_EQUIP.DDS");
          if (w.ok()) wequip = upload_rgba(g, w.width, w.height, w.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "W_FS_MSG.DDS");   // Equip/Item bottom detail panel (ornate)
          if (w.ok()) wdetail = upload_rgba(g, w.width, w.height, w.data.data()); }
        { Dds e = load_dds(asset_root / "MENU" / "INVENTORY.DDS");   // Item-page "Inventory" plaque + 6x5 grid
          if (e.ok()) invPlaque = upload_rgba(g, e.width, e.height, e.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "W_INVENT.DDS");
          if (w.ok()) winvent = upload_rgba(g, w.width, w.height, w.data.data()); }
        { Dds e = load_dds(asset_root / "MENU" / "TOPIC.DDS");   // Topic page: plaque + 3 category buttons + cursor
          if (e.ok()) topicPlaque = upload_rgba(g, e.width, e.height, e.data.data()); }
        for (int i = 0; i < 3; ++i) {
            Dds b = load_dds(asset_root / "MENU" / ("TOPIC_BTN" + std::to_string(i) + ".DDS"));
            if (b.ok()) topicBtn[i] = upload_rgba(g, b.width, b.height, b.data.data());
        }
        { Dds c = load_dds(asset_root / "MENU" / "TOPIC_CUR.DDS");
          if (c.ok()) topicCur = upload_rgba(g, c.width, c.height, c.data.data()); }
        { Dds b = load_dds(asset_root / "MENU" / "SHOPPILLAR16-9.DDS");   // widescreen margin layer
          if (b.ok()) shopBg = upload_rgba(g, b.width, b.height, b.data.data()); }
        { Dds b = load_dds(asset_root / "MENU" / "BACK01.DDS");           // Bonus Shop backdrop (SHOP_LAYOUT.md)
          if (b.ok()) shopBack = upload_rgba(g, b.width, b.height, b.data.data()); }
        { Dds b = load_dds(asset_root / "IMAGE" / "T_400A.DDS");          // Feena key-art illustration
          if (b.ok()) shopFeena = upload_rgba(g, b.width, b.height, b.data.data()); }
        { Dds e = load_dds(asset_root / "MENU" / "LOAD.DDS");     // Save/Load: plaques + W_SVLD slot/tab plate
          if (e.ok()) loadPlaque = upload_rgba(g, e.width, e.height, e.data.data()); }
        { Dds e = load_dds(asset_root / "MENU" / "SAVE.DDS");
          if (e.ok()) savePlaque = upload_rgba(g, e.width, e.height, e.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "W_SVLD.DDS");
          if (w.ok()) wsvld = upload_rgba(g, w.width, w.height, w.data.data()); }
        { Dds w = load_dds(asset_root / "MENU" / "SQUARE.DDS");   // grid cell tile + cursor (tex 10)
          if (w.ok()) square = upload_rgba(g, w.width, w.height, w.data.data()); }
        // Equipped/selected highlight quad (0x4259999a ARGB = teal-blue (89,153,154) @ alpha 66).
        blueHi = solid(89, 153, 154, 66);
        // Character-select key art + fixed-pose chibi: index 1/2/3 (Yunica/Hugo/Toal); 0 is Adol (unused).
        for (int i = 0; i < 3; ++i) {
            Dds d = load_dds(asset_root / "MENU" /
                             ("CHRSELECT_PORTRAIT" + std::to_string(i + 1) + ".DDS"));
            if (d.ok()) portrait[i] = upload_rgba(g, d.width, d.height, d.data.data());
            Dds cd = load_dds(asset_root / "MENU" / ("CHRSELECT_CHR" + std::to_string(i + 1) + ".DDS"));
            if (cd.ok()) chibiPose[i] = upload_rgba(g, cd.width, cd.height, cd.data.data());
        }
        // The little walking chibi (C_102/103/104 = Yunica/Hugo/Toal); resolve the walk animation
        // by tag 0 (canonical across characters) so char-select can animate it bottom-right.
        static const char* cai[3] = {"C_102.AIA", "C_103.AIA", "C_104.AIA"};
        std::uint32_t chibiTag = 1;   // run -- shows the weapon (0 walk / 1 run / 2 idle); CLERIA_CS_CHIBI_TAG
        if (const char* e = std::getenv("CLERIA_CS_CHIBI_TAG")) chibiTag = std::uint32_t(std::atoi(e));
        for (int i = 0; i < 3; ++i)
            if (chibi[i].load(g, asset_root / "ANI" / cai[i])) {
                const int w = chibi[i].find_tag(chibiTag);
                if (w >= 0) chibi[i].set_anim(g, std::uint32_t(w), 0);
            }
        // Arrow-clip the face cutouts: the atlas face crops are RECTANGULAR, but the game clips each
        // into the bar's arrow shape. Multiply each face's alpha by the bar-arrow alpha (dark-bar
        // region) sampled at the face's position within the bar -- matching the draw rects (bar
        // x60..956 == atlas u[0.06,0.875]; face x654..897 / y row+14..+110). Kills the rectangular
        // overhang past the arrow's tapering right edge.
        {
            Dds csd = load_dds(asset_root / "MENU" / "CHRSELECT.DDS");
            if (csd.ok()) {
                const int AW = int(csd.width), AH = int(csd.height);
                auto samp = [&](float u, float v, std::uint8_t* out) {
                    const int x = std::clamp(int(u * AW), 0, AW - 1), y = std::clamp(int(v * AH), 0, AH - 1);
                    std::memcpy(out, &csd.data[(std::size_t(y) * AW + x) * 4], 4);
                };
                // bar-arrow alpha region under the face (atlas uv), from the draw rects:
                const float bu0 = 0.600f, bu1 = 0.821f, bv0 = 0.320f, bv1 = 0.493f;
                // Face atlas UVs (code-traced): u = col*0.25, v[0.5625, 0.7578125].
                static const float kU[3][2] = {{0.0f, 0.25f}, {0.25f, 0.5f}, {0.5f, 0.75f}};
                constexpr float fv0 = 0.5625f, fv1 = 0.7578125f;
                const int FW = 256, FH = 128;
                std::vector<std::uint8_t> buf(std::size_t(FW) * FH * 4);
                for (int i = 0; i < 3; ++i) {
                    for (int y = 0; y < FH; ++y)
                        for (int x = 0; x < FW; ++x) {
                            const float sx = float(x) / FW, sy = float(y) / FH;
                            std::uint8_t face[4], bar[4];
                            samp(kU[i][0] + sx * (kU[i][1] - kU[i][0]), fv0 + sy * (fv1 - fv0), face);
                            samp(bu0 + sx * (bu1 - bu0), bv0 + sy * (bv1 - bv0), bar);
                            std::uint8_t* o = &buf[(std::size_t(y) * FW + x) * 4];
                            o[0] = face[0]; o[1] = face[1]; o[2] = face[2];
                            // Clip to the arrow AND suppress the crop's soft light halo (square the
                            // face alpha) so only the face shows, not a light rectangular box.
                            const int fa = face[3] * face[3] / 255;
                            o[3] = std::uint8_t(fa * bar[3] / 255);
                        }
                    faceClip[i] = upload_rgba(g, FW, FH, buf.data());
                }
            }
        }
        return splash.tex != nullptr;
    }
    // Compose the title: the pillarbox art fills the whole (widescreen) screen, then the
    // square 1024^2 splash (TITLE_000: logo + moon + landscape + credits) is drawn fit to
    // HEIGHT and centred on top, so its full height (incl. the bottom credits) shows and
    // the pillar continues the misty landscape into the 16:9 side bars.
    void draw(Gpu& g, WGPUTextureView cview, WGPUTextureView dview, std::uint32_t W, std::uint32_t H) {
        const float aspect = float(W) / float(H);
        // The real art lives in the top 768 of each 1024^2 (4:3); the bottom 256 is padding
        // (brown on the splash, transparent on the pillar). Crop V to 0.75.
        constexpr float vc = 0.75f;
        const float sw = (4.0f / 3.0f) / aspect;   // 4:3 splash fit to height -> half-width
        // The side bars must be ANCHORED at their inner edge to the splash edge (±sw), not drawn
        // full-screen. The pillar's landscape meets the splash where its transparent centre begins
        // (u ~0.168 left / ~0.831 right). Draw the LEFT strip u[0,0.168] into the left margin [-1,-sw]
        // (its inner edge u0.168 landing exactly on -sw) and the RIGHT strip u[0.831,1] into [sw,1] --
        // so the landscape lines up at any aspect instead of the strip being cut mid-way by the splash.
        // Inner-edge u pulled ~2 texels inside the opaque strip (true transparent-centre bounds are
        // 0.168/0.831): the pillar goes transparent-black exactly at 0.168, and sampling that edge
        // bleeds a dark column at the seam. 0.166/0.833 keeps the landscape aligned with no bleed.
        constexpr float pc0 = 0.166f, pc1 = 0.833f;
        draw_screen_image(g, cview, dview, /*clear*/true, pillar, -1, 1, -sw, -1, 0.9f, 0.0f, 0.0f, pc0, vc);
        draw_screen_image(g, cview, dview, false, pillar, sw, 1, 1, -1, 0.9f, pc1, 0.0f, 1.0f, vc);
        // The 4:3 splash (logo + moon + landscape + credits) centred, full height.
        draw_screen_image(g, cview, dview, false, splash, -sw, 1, sw, -1, 0.5f, 0, 0, 1, vc);
    }

    static constexpr int kItemCount = 5;
    static const char* item(int i) {
        static const char* items[kItemCount] = {"New Game", "Load Game", "Bonus Modes",
                                                 "Options", "Exit"};
        return items[i];
    }
    // The Bonus Modes sub-list (engine's DAT_0074e259 != 0 state). Selecting "Bonus Modes"
    // on the main list swaps to these. Back (or Esc) returns. MENUS.md §1.4.
    static constexpr int kBonusCount = 4;
    static const char* bonusItem(int i) {
        static const char* items[kBonusCount] = {"Time Attack", "Arena Mode", "Bonus Shop",
                                                  "Back"};
        return items[i];
    }
    // Draw the centred label list with the MENUCURSOR bar on the selection. Exact geometry from
    // build/menu_re/MENU_LAYOUT.md: authored 1024x768 (4:3, fit-to-height), centre X=512, font
    // cell 32, row pitch 38.4, bottom-anchored (~y694), cursor = one 32-tall cell spanning the
    // widest label +32 each side. Shared by the title main menu and the bonus sub-list.
    void draw_list(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                   std::uint32_t W, std::uint32_t H, int count, const char* (*get)(int), int sel) {
        const float sw = float(W), sh = float(H);
        const float A = sh / 768.0f;                 // authored px -> screen px (height-fit 4:3)
        const float pxH = 32.0f * A, pitch = 38.4f * A, cellH = 32.0f * A;
        // Bottom anchor: code-traced originY gives the last row cell-top at authored 694.4
        // (TITLE_LAYOUT.md §2, no-Steam case). Measured against reference/title_main.png the rows
        // sit ~10 authored px lower (a font baseline-convention delta the RE flagged as "within
        // cell") -> nudge the anchor to 704 so the list registers with the grab.
        const float lastTop = 704.0f * A;            // top of the last row's cell
        const float cxpx = sw * 0.5f;                // X=512 of the centred 4:3 field
        auto ndcX = [&](float x) { return 2.0f * x / sw - 1.0f; };
        auto ndcY = [&](float y) { return 1.0f - 2.0f * y / sh; };
        auto rowTop = [&](int i) { return lastTop - (count - 1 - i) * pitch; };
        float maxW = 0;
        for (int i = 0; i < count; ++i) maxW = std::max(maxW, mf.measure(get(i), pxH));
        const float ty = 3.0f * A;
        // Row colours (TITLE_LAYOUT.md §1, code-traced): unselected diffuse 0xB0D0D0 = (176,208,208)
        // light teal; selected row PURE WHITE (255,255,255). NO drop-shadow -- the engine emits a
        // single glyph run per string, no second offset/tint pass (UI_BLEND_FONT.md §2). Still
        // env-tunable for A/B checks against the reference grab.
        int ur = 176, ug = 208, ub = 208, ua = 255;
        int sr = 255, sg = 255, sb = 255, sa = 255;
        if (const char* e = std::getenv("CLERIA_MENU_U")) { sscanf(e, "%d,%d,%d,%d", &ur, &ug, &ub, &ua); }
        if (const char* e = std::getenv("CLERIA_MENU_S")) { sscanf(e, "%d,%d,%d,%d", &sr, &sg, &sb, &sa); }
        // Selection cursor: the ornate scroll-curl bar (IMAGE/MENUCURSOR.DDS), 32px tall,
        // width maxW+64, centred on the selected row (TITLE_LAYOUT.md §53). The chrome
        // handle DAT_00749d68 the disasm couldn't name IS this asset — a dark plaque with
        // filigree ends and a ~transparent centre, so the text reads straight through it.
        // We were drawing a plain additive glow (curGlow) instead; draw_cursor_bar 3-slices
        // MENUCURSOR so the curls keep their thickness at any width.
        if (cursor.tex || curGlow.tex) {
            const float mid = rowTop(sel) + ty + 0.46f * pxH;   // glyph vertical centre
            const float half = 16.0f * A, barW = maxW + 64.0f * A;
            if (cursor.tex)
                draw_cursor_bar(g, cview, dview, cxpx - barW * 0.5f, mid - half,
                                cxpx + barW * 0.5f, mid + half, sw, sh);
            else
                draw_cursor_glow(g, cview, dview, cxpx - barW * 0.5f, mid - half,
                                 cxpx + barW * 0.5f, mid + half, sw, sh);
        }
        for (int i = 0; i < count; ++i) {
            const bool s = (i == sel);
            mf.text_centered(get(i), 0.5f, rowTop(i) + ty, pxH, sw, sh,
                             (std::uint8_t)(s ? sr : ur), (std::uint8_t)(s ? sg : ug),
                             (std::uint8_t)(s ? sb : ub), (std::uint8_t)(s ? sa : ua));
        }
        mf.draw(g, cview);
    }
    void draw_menu(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                   std::uint32_t W, std::uint32_t H, int sel) {
        draw_list(g, mf, cview, dview, W, H, kItemCount, item, sel);
    }
    void draw_bonus(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                    std::uint32_t W, std::uint32_t H, int sel) {
        draw(g, cview, dview, W, H);   // redraw the splash under the sub-list
        draw_list(g, mf, cview, dview, W, H, kBonusCount, bonusItem, sel);
    }

    static constexpr int kCharCount = 3;   // g_flags[150]: 1 Yunica, 2 Hugo, 3 Toal
    static const char* charName(int i) {
        static const char* n[kCharCount] = {"Yunica", "Hugo", "Toal"};
        return n[i];
    }
    static const char* charDesc(int i) {
        static const char* d[kCharCount] = {
            "Axe-wielding fighter. Mid speed, mid range.",
            "Energy-firing mage. Slow speed, long range.",
            "Berserker. Fast, close range. Experts only!"};
        return d[i];
    }
    // The real Character Select (build/menu_re/MENUS.md + CHRSELECT.DDS atlas): the BACK00
    // frame, the "Character Select" banner, the selected character's key art on the left, three
    // selection bars on the right (the crossed one cyan, the others dark) each capped with the
    // character's face icon, and the description on the bottom bar. sel 0/1/2 -> character 1/2/3.
    void draw_charselect(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                         std::uint32_t W, std::uint32_t H, int sel, float pulse = 0.0f,
                         int chibiFrame = 0) {
        const float sw = float(W), sh = float(H);
        // The game PILLAR-BOXES char-select: the 1024x768 authored field is fit to height and
        // centred horizontally (uniform scale sh/768), NOT stretched to 16:9. The letterbox sides
        // are the Archipelago mod's sepia art in reference/charselect.png -- ignore them.
        const float fwN = (4.0f / 3.0f) * sh / sw;                    // 4:3 field half-width in NDC
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };   // authored -> NDC
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };          // authored -> screen px
        // CHRSELECT.DDS atlas sub-rects (CHARSELECT_LAYOUT.md §1, code-traced from FUN_005c2110):
        // bar UV u[0,0.875] v[0.28125,0.53125] (unselected/dark); face UV v[0.5625,0.7578125],
        // u = col*0.25 (col 0/1/2/3). The cyan selection-highlight band's V is a separate overlay.
        constexpr float kBarU0 = 0.0f, kBarU1 = 0.875f;
        constexpr float kCyanV0 = 0.045f, kCyanV1 = 0.264f;
        constexpr float kDarkV0 = 0.28125f, kDarkV1 = 0.53125f;
        constexpr float kFaceV0 = 0.5625f, kFaceV1 = 0.7578125f;
        static const float kFaceU[3][2] = {{0.0f, 0.25f}, {0.25f, 0.5f}, {0.5f, 0.75f}};
        // 1. Frame BACK00 fills the 4:3 field. Its art content ends at texture row 767/1024 =
        //    v0.749 (measured); sampling further (the old 0.772) pulled ~23 black rows in and
        //    compressed the bottom marble border ~21 authored units too high. Clears the
        //    letterbox to the dark background.
        // 0. Golden goddess-statue side-art fills the 16:9 letterbox margins (ALTPILLAR16-9: left
        //    strip u0..0.167, right strip u0.832..1, content v0..0.749). Drawn full-screen behind the
        //    field, then the frame (below) covers the centre 4:3 -- so only the margins show goddesses.
        if (altpillar.tex) {   // goddess strips anchored: each inner edge meets the frame (AX(0)/AX(1024)),
            // NOT full-screen (which cut the goddess mid-figure); u pulled 2 texels in to avoid an edge bleed.
            draw_screen_image(g, cview, dview, /*clear*/true, altpillar, -1.0f, 1.0f, AX(0), -1.0f,
                              0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f,
                              0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        }
        if (frame.tex)
            draw_screen_image(g, cview, dview, /*clear*/altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0.0f, 0.0f, 1.0f, 0.749f);
        // 2. "Character Select" banner across the top of the field. Width (authored x 290..734) is
        //    correct; the old y -8..98 stretched the glyphs 1.28x too tall (measured C-height 37 vs
        //    reference 29). y -2..81 keeps the top fixed and matches the reference glyph height.
        if (chrsel.tex)
            draw_screen_image(g, cview, dview, false, chrsel, AX(274), AY(0), AX(738), AY(80), 0.5f,
                              0.557f, 0.844f, 0.998f, 1.0f);
        // 3. Three selection bars (EXTRA_SKINS.md §4: x=60..956, h=128, y={128,272,416}; row
        //    0/1/2 = Yunica/Hugo/Toal), the crossed one cyan, the rest dark, each capped by its
        //    face icon in the wide right end. Drawn BEFORE the portrait so the character's cape
        //    blends over the bars (else the cape's semi-transparent edge writes depth and punches
        //    light "holes"/halo through the bars behind it).
        const float rowYtop[3] = {128.0f, 272.0f, 416.0f};
        for (int i = 0; i < 3; ++i) {
            const bool s = (i == sel);
            const float yt = rowYtop[i], yb = yt + 128.0f;
            if (chrsel.tex) {
                // The selected (cyan) bar PULSATES toward white in-game; `pulse` (0..1) white-tints it.
                draw_screen_image(g, cview, dview, false, chrsel, AX(60), AY(yt), AX(956), AY(yb),
                                  0.45f, kBarU0, s ? kCyanV0 : kDarkV0, kBarU1, s ? kCyanV1 : kDarkV1,
                                  0.02f, 0.02f, 0.05f, s ? pulse : 0.0f);
                if (faceClip[i].tex)   // face pre-clipped to the arrow (built in init); code rect x646 w256 h100
                    draw_screen_image(g, cview, dview, false, faceClip[i], AX(646), AY(yt + 14),
                                      AX(902), AY(yt + 114), 0.42f);
            }
        }
        // 4. The selected character's key art, on TOP of the bars. Code-traced (CHARSELECT_LAYOUT.md
        //    §1.1): the engine draws the FULL ChrSelect_Portrait{N}.dds (UV {0,0,1,1}) as a 1024x1024
        //    quad at the top-left origin (0,0) -- the character art is baked left-anchored, the visible
        //    768 shows the top. Replaces the old hand-tuned {2,41,735} crop.
        if (sel >= 0 && sel < 3 && portrait[sel].tex)
            draw_screen_image(g, cview, dview, false, portrait[sel], AX(0), AY(0),
                              AX(1024), AY(1024), 0.35f, 0.0f, 0.0f, 1.0f, 1.0f);
        // 4b. The fixed-pose chibi in the bottom-right. Code-traced (CHARSELECT_LAYOUT.md §1.1): the
        //     dedicated ChrSelect_Chr{N}.dds drawn as a 128x128 csprite centred at authored (856,644)
        //     -- NOT the AIA run animation. The DDS is a small figure on a transparent canvas.
        (void)chibiFrame;
        if (sel >= 0 && sel < 3 && chibiPose[sel].tex) {
            // The DDS is a 512x256 sheet (4x2 grid of 128px poses); the csprite samples one 128x128
            // cell from UV base (6.4,12.8)px -> u[0.0125,0.2625] v[0.05,0.55] (CHARSELECT_LAYOUT §1.1).
            const float cx = 856.0f, cy = 644.0f, half = 64.0f;   // 128x128 authored dst quad
            draw_screen_image(g, cview, dview, false, chibiPose[sel], AX(cx - half), AY(cy - half),
                              AX(cx + half), AY(cy + half), 0.3f, 0.0125f, 0.05f, 0.2625f, 0.55f);
        }
        // 5. Description: "[Name]" left-aligned at authored x=48, and the blurb at a FIXED column
        //    x=209 (measured: the reference body-start is ~209 for all three names regardless of
        //    name length -- it is a tab column, not name+spaces). Baseline authored y=704.
        const float dy = 704.0f / 768.0f * sh, dsz = 32.0f / 768.0f * sh;
        mf.text(std::string("[") + charName(sel) + "]", SX(48), dy, dsz, sw, sh, 236, 236, 236, 255);
        mf.text(charDesc(sel), SX(209), dy, dsz, sw, sh, 236, 236, 236, 255);
        mf.draw(g, cview);
    }

    // The engine's selection cursor: the baked curGlow bar drawn ADDITIVE (renderstate 0x40c1)
    // OVER the text. White with a vertical alpha gradient; additive so it brightens the row toward
    // white rather than masking it (matches the reference's soft highlight, not an opaque pill).
    void draw_cursor_glow(Gpu& g, WGPUTextureView cview, WGPUTextureView dview,
                          float lpx, float tpx, float rpx, float bpx, float sw, float sh) {
        if (!curGlow.tex) return;
        auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
        auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
        draw_screen_image(g, cview, dview, false, curGlow, NX(lpx), NY(tpx), NX(rpx), NY(bpx),
                          0.15f, 0.0f, 0.0f, 1.0f, 1.0f, 0.02f, 0.02f, 0.05f, 0.0f, /*additive*/true);
    }
    // MENUCURSOR as a horizontal 9-slice (curls u[0,0.11] and [0.89,1.0] fixed, middle stretched)
    // so the ornate ends keep their thickness at any bar width instead of being stretched thin.
    void draw_cursor_bar(Gpu& g, WGPUTextureView cview, WGPUTextureView dview,
                         float lpx, float tpx, float rpx, float bpx, float sw, float sh) {
        if (!cursor.tex) return;
        constexpr float u1 = 0.11f, u2 = 0.89f;
        float curl = (bpx - tpx) * (u1 * 256.0f) / 32.0f;   // curl 28px wide / 32px tall native
        if (curl * 2.0f > (rpx - lpx) * 0.9f) curl = (rpx - lpx) * 0.45f;
        auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
        auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
        const float z = 0.3f;
        draw_screen_image(g, cview, dview, false, cursor, NX(lpx),        NY(tpx), NX(lpx + curl), NY(bpx), z, 0.0f, 0.0f, u1,   1.0f);
        draw_screen_image(g, cview, dview, false, cursor, NX(lpx + curl), NY(tpx), NX(rpx - curl), NY(bpx), z, u1,   0.0f, u2,   1.0f);
        draw_screen_image(g, cview, dview, false, cursor, NX(rpx - curl), NY(tpx), NX(rpx),        NY(bpx), z, u2,   0.0f, 1.0f, 1.0f);
    }
    // DIALOG0 as a horizontal 9-slice (u-cuts 0 / 0.15625 / 0.65625 / 0.8125 from MENU_LAYOUT.md
    // §3.1; content v 0..0.805): fixed scroll-curl ends, stretched middle, so the curls keep their
    // shape at any width. The engine's go_wnddialog popup frame is exactly this.
    void draw_dialog_frame(Gpu& g, WGPUTextureView cview, WGPUTextureView dview,
                           float lpx, float tpx, float rpx, float bpx, float sw, float sh) {
        if (!dialog.tex) return;
        constexpr float vc = 0.805f, u1 = 0.15625f, u2 = 0.65625f, u3 = 0.8125f;
        float curl = (bpx - tpx) * (u1 * 512.0f) / (vc * 256.0f);   // curl 80px wide / 206px tall
        if (curl * 2.0f > (rpx - lpx) * 0.9f) curl = (rpx - lpx) * 0.45f;
        auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
        auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
        const float z = 0.2f;
        draw_screen_image(g, cview, dview, false, dialog, NX(lpx),        NY(tpx), NX(lpx + curl), NY(bpx), z, 0.0f, 0.0f, u1, vc);
        draw_screen_image(g, cview, dview, false, dialog, NX(lpx + curl), NY(tpx), NX(rpx - curl), NY(bpx), z, u1,   0.0f, u2, vc);
        draw_screen_image(g, cview, dview, false, dialog, NX(rpx - curl), NY(tpx), NX(rpx),        NY(bpx), z, u2,   0.0f, u3, vc);
    }
    // The in-engine cutscene DIALOGUE box (CUTSCENE_DIALOGUE.md): FACEWIN0 as a full-width
    // bottom bar (authored y544..768 of the 768 frame => bottom 224px), 3-sliced so the ornate
    // curls keep their size while the middle stretches. `name` = cream line above the divider,
    // `text` = white body below, split on the literal "\n" authors write, revealed up to
    // `reveal` chars (typewriter; <0 shows all). Portrait slot on the left is a TODO — the
    // face asset/id mapping is still [UNKNOWN] (KAO_* is a noise texture, not portraits).
    void draw_dialogue(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                       std::uint32_t W, std::uint32_t H, const std::string& name,
                       const std::string& text, int reveal) {
        if (!facewin.tex) return;
        const float sw = float(W), sh = float(H);
        const float by1 = sh, by0 = sh - sh * (224.0f / 768.0f);   // bottom 224/768 of the frame
        const float bx0 = 0.0f, bx1 = sw;
        constexpr float vc = 0.5f, uL = 0.19f, uR = 0.586f, uEnd = 0.809f;   // FACEWIN0 slice UVs
        float curl = (by1 - by0) * (uL * 512.0f) / (vc * 512.0f);   // keep the src curl aspect
        if (curl * 2.0f > (bx1 - bx0) * 0.5f) curl = (bx1 - bx0) * 0.25f;
        auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
        auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
        const float z = 0.15f;
        draw_screen_image(g, cview, dview, false, facewin, NX(bx0),        NY(by0), NX(bx0 + curl), NY(by1), z, 0.0f, 0.0f, uL,   vc);
        draw_screen_image(g, cview, dview, false, facewin, NX(bx0 + curl), NY(by0), NX(bx1 - curl), NY(by1), z, uL,   0.0f, uR,   vc);
        draw_screen_image(g, cview, dview, false, facewin, NX(bx1 - curl), NY(by0), NX(bx1),        NY(by1), z, uR,   0.0f, uEnd, vc);
        const float boxH = by1 - by0;
        const float tx = sw * 0.15f;                 // left text margin (past the portrait slot)
        const float pxName = boxH * 0.15f, pxBody = boxH * 0.21f;
        if (!name.empty())
            mf.text(name, tx, by0 + boxH * 0.10f, pxName, sw, sh, 240, 224, 192, 255);   // cream
        std::string shown = (reveal >= 0 && reveal < int(text.size())) ? text.substr(0, reveal) : text;
        float ly = by0 + boxH * 0.34f;
        std::size_t i = 0;
        for (;;) {
            const std::size_t nl = shown.find("\\n", i);   // authors write the two chars \ n
            mf.text(shown.substr(i, (nl == std::string::npos ? shown.size() : nl) - i),
                    tx, ly, pxBody, sw, sh, 255, 255, 255, 255);
            ly += pxBody * 1.15f;
            if (nl == std::string::npos) break;
            i = nl + 2;
        }
        mf.draw(g, cview);   // flush the accumulated glyphs
    }
    // The New-Game choice popups (Type Select, Difficulty, SP Bonus Acquisition): a go_wnddialog
    // list (MENU_LAYOUT.md) -- caption at authored {512,256}, items 38.4 apart below, font cell 32,
    // centred on X=512 -- inside the DIALOG0 9-slice frame, with the MENUCURSOR bar on the choice.
    void draw_popup(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                    std::uint32_t W, std::uint32_t H, const char* title,
                    const char* const* items, int count, int sel,
                    bool onBlack = false, float capTop = 211.2f) {
        const float sw = float(W), sh = float(H);
        const float A = sh / 768.0f;
        // The engine DIMS whatever is behind the go_wnddialog popup (char-select or, historically, a
        // black screen for the SP-Bonus step -- but §2.3 shows every popup shares origin (512,256) over
        // the dimmed backdrop; the on-black grab was an artifact). Draw the dimmer twice for a ~70%
        // darken to match the reference.
        if (onBlack)
            draw_screen_image(g, cview, dview, /*clear*/true, dim, -1, 1, 1, -1, 0.25f, 0, 0, 1, 1, 0, 0, 0);
        else if (dim.tex) {   // two passes at distinct z (else the depth test dedups the 2nd) -> ~72% darken
            draw_screen_image(g, cview, dview, false, dim, -1, 1, 1, -1, 0.26f);
            draw_screen_image(g, cview, dview, false, dim, -1, 1, 1, -1, 0.24f);
        }
        const float pxH = 32.0f * A, pitch = 38.4f * A, cellH = 32.0f * A;
        const float cxpx = sw * 0.5f;                     // authored X=512 (centre)
        // Invariant origin (512,256): caption text-top at origin-44.8 = 211.2, item i (1-based) at
        // origin + (i-1)*38.4 + 6.4 -> item1 = 262.4 (caption->item1 gap 51.2). Frame top = origin-64
        // = 192; side pad 80 (CHARSELECT_LAYOUT.md §2).
        const float captionTop = capTop * A;
        const float firstItem = captionTop + 51.2f * A;
        auto itemTop = [&](int i) { return firstItem + i * pitch; };
        float maxW = mf.measure(title, pxH);
        for (int i = 0; i < count; ++i) maxW = std::max(maxW, mf.measure(items[i], pxH));
        const float fhalf = (maxW + 41.6f * A) * 0.5f + 80.0f * A;
        draw_dialog_frame(g, cview, dview, cxpx - fhalf, (capTop - 19.2f) * A,
                          cxpx + fhalf, itemTop(count - 1) + cellH + 22.0f * A, sw, sh);
        if (curGlow.tex) {                                // additive glow highlight on the choice (0x40c1)
            const float mid = itemTop(sel) + 0.46f * pxH, half = 16.0f * A, barW = maxW + 64.0f * A;
            draw_cursor_glow(g, cview, dview, cxpx - barW * 0.5f, mid - half,
                             cxpx + barW * 0.5f, mid + half, sw, sh);
        }
        mf.text_centered(title, 0.5f, captionTop, pxH, sw, sh, 240, 246, 250, 255);
        for (int i = 0; i < count; ++i) {
            const bool s = (i == sel);
            // Reference glyph colours: selected pure white [255,255,255], unselected teal ~[176,208,208].
            mf.text_centered(items[i], 0.5f, itemTop(i), pxH, sw, sh,
                             s ? 250 : 172, s ? 252 : 206, s ? 252 : 208, 255);
        }
        mf.draw(g, cview);
    }

    // The Option/Config screen (build/menu_re/OPTIONS.md). Pillar-boxed like char-select: goddess
    // margins + BACK00 frame, the "Option" plaque top-left, then 10 rows -- 3 volume/brightness
    // sliders, 4 checkboxes, and Default / Return-to-Title / Exit-Game buttons. All button faces,
    // the slider track+knob and the checkbox squares are sub-rects of the OPBTN atlas (baked labels;
    // atlas order != screen order, so each screen row is label-mapped to its band). sliders[0..2] =
    // 0..1024, checks[0..3] = rows 3..6. sel = focused row 0..9.
    void draw_options(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                      std::uint32_t W, std::uint32_t H, int sel,
                      const int sliders[3], const bool checks[4]) {
        const float sw = float(W), sh = float(H);
        const float fwN = (4.0f / 3.0f) * sh / sw;
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };
        auto SY = [&](float y) { return y / 768.0f * sh; };
        constexpr float T = 1.0f / 1024.0f;   // OPBTN atlas is 1024^2
        // 0. goddess pillars + BACK00 frame (identical to char-select's backdrop)
        if (altpillar.tex)
            draw_screen_image(g, cview, dview, /*clear*/true, altpillar, -1.0f, 1.0f, AX(0), -1.0f, 0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
        if (altpillar.tex)   // right goddess strip anchored into the right margin
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f, 0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        if (frame.tex)
            draw_screen_image(g, cview, dview, /*clear*/altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0, 0, 1, 0.749f);
        // 1. "Option" plaque, top-left (OPTION.DDS: gold cartouche is u0..0.598 of the upper half;
        //    the rest of the texture is opaque black padding, so crop to the cartouche).
        if (plaque.tex)   // Option plaque anchor X=128 (VN1), quad ~153.6x51.2 (was hand-tuned x=30)
            draw_screen_image(g, cview, dview, false, plaque, AX(128), AY(8), AX(281), AY(68),
                              0.4f, 0.0f, 0.0f, 0.598f, 0.492f);
        // 2. the 10 rows. rowV = OPBTN band (px) for each SCREEN row, label-mapped; row 7 (Default)
        //    is a short button in the atlas' right area. Row Y (OPTIONS_LAYOUT.md §2): start 96, pitch
        //    54.4, +44.8 once before row 8 -> {96,150.4,...,476.8,576.0,630.4}.
        static const float rowV[10][2] = { {1,48},{53,98},{104,150},{411,456},{462,508},
                                           {514,559},{565,610},{307,356},{155,201},{206,252} };
        static const float rowY[10] = { 96, 150.4f, 204.8f, 259.2f, 313.6f, 368.0f, 422.4f,
                                        476.8f, 576.0f, 630.4f };
        // Inverted focus highlight (FUN_005bae20): SELECTED row = full white (texture as-is), every
        // UNSELECTED row dimmed toward 0x808080. The code value is RGB x0.5, but that assumes a base
        // plate ~2x brighter than the OPBTN region CleriaCore samples renders (measured: our full plate
        // ~38 == the retail *unfocused* level ~35, so x0.5 crushes to ~19, well below the reference).
        // Until the plate-brightness root cause is fixed, dim to 0.7 -> the pick still reads brighter
        // without the "too dark" regression (DIFF_VALIDATION). CLERIA_OPT_DIM overrides.
        float unfocusDim = 0.70f;
        if (const char* e = std::getenv("CLERIA_OPT_DIM")) unfocusDim = float(std::atof(e));
        for (int r = 0; r < 10; ++r) {
            const float bx = (r <= 6) ? 179.0f : (r == 7 ? 409.0f : 307.0f);
            const float bw = (r == 7) ? 175.0f : 409.6f;
            const float u0 = (r == 7) ? 409.0f * T : 0.0f;
            const float u1 = (r == 7) ? 584.0f * T : 409.6f * T;
            const float dim = (r == sel) ? 1.0f : unfocusDim;
            draw_screen_image(g, cview, dview, false, opbtn, AX(bx), AY(rowY[r]), AX(bx + bw),
                              AY(rowY[r] + 51.2f), 0.30f, u0, rowV[r][0] * T, u1, rowV[r][1] * T,
                              0.02f, 0.02f, 0.05f, 0.0f, false, dim, dim, dim);
        }
        // 3. sliders (rows 0-2): OPBTN track (metallic-bordered teal gradient bar, atlas x410..640) +
        //    a tall lit blue pill knob (atlas x32..58) positioned by value along the track. Knob
        //    travels 188.8 px over value 0..1024 (|DAT_006be148|, OPTIONS_LAYOUT.md).
        const float trackX = 652.0f, trackLen = 240.0f, knobTravel = 188.8f;
        for (int i = 0; i < 3; ++i) {
            const float ty = rowY[i] + 25.6f;
            // track = teal-gradient bar with metallic end-caps at atlas x410 and x615; the SECOND bar
            // (rainbow/gamma variant) starts at x618 with a dark-blue cap, so stop at 616 or it leaves
            // a stray blue stub past the track.
            draw_screen_image(g, cview, dview, false, opbtn, AX(trackX), AY(ty - 11.0f),
                              AX(trackX + trackLen), AY(ty + 11.0f), 0.28f, 408*T, 365*T, 616*T, 388*T);
            const float kx = trackX + (float(sliders[i]) / 1024.0f) * knobTravel;
            // knob pill: dim silver-teal (atlas x1..15) when the row isn't focused, lit blue
            // (x16..31) when it is -- the reference grab shows the dim variant.
            const bool kf = (i == sel);
            const float ku0 = (kf ? 16.0f : 1.0f) * T, ku1 = (kf ? 31.0f : 15.0f) * T;
            draw_screen_image(g, cview, dview, false, opbtn, AX(kx - 8.0f), AY(ty - 26.0f),
                              AX(kx + 8.0f), AY(ty + 26.0f), 0.26f, ku0, 305*T, ku1, 357*T);
        }
        // 4. checkboxes (rows 3-6): OPBTN square, checked U=0 / unchecked U=51 (OPTIONS.md §4)
        for (int i = 0; i < 4; ++i) {
            const float cx = 732.0f, cy = rowY[i + 3];
            const float u0 = checks[i] ? 0.0f : 51.0f * T, u1 = checks[i] ? 48.0f * T : 99.0f * T;
            draw_screen_image(g, cview, dview, false, opbtn, AX(cx), AY(cy), AX(cx + 51.2f),
                              AY(cy + 51.2f), 0.26f, u0, 616*T, u1, 661*T);
        }
        // 5. caption bar (book-level string)
        const float dsz = 27.0f * sh / 768.0f;
        mf.text("Alter the game's settings.", SX(48), SY(707), dsz, sw, sh, 236, 236, 236, 255);
        mf.draw(g, cview);
    }

    // Lazily load + cache an item's icon (MENU/ICON/<stem>.DDS via ItemTable). nullptr if unknown.
    const SpriteTex* icon(Gpu& g, const ItemTable& items, const fs::path& asset_root, int id) {
        if (id < 0) return nullptr;
        auto it = iconCache.find(id);
        if (it != iconCache.end()) return it->second.tex ? &it->second : nullptr;
        SpriteTex t{};
        fs::path p = items.icon_path(asset_root, id, false);
        if (!p.empty()) { Dds d = load_dds(p); if (d.ok()) t = upload_rgba(g, d.width, d.height, d.data.data()); }
        return (iconCache[id] = t).tex ? &iconCache[id] : nullptr;
    }

    // The pause-book EQUIP page (build/menu_re/PAUSEBOOK.md §3). Stats come from GAMEINFO curves; item
    // metadata (name/icon/desc) from INVINFO. Equipped-item runtime state isn't tracked by CleriaCore
    // yet, so the caller stubs it. Pillar-boxed like the Option screen.
    struct EquipView {
        int level = 1, hp = 400, maxhp = 400, str = 0, def = 0;
        long exp = 0, next = 0;
        int character = 1;
        int weaponId = -1, armorId = -1, bootsId = -1, accId = -1;   // -1 = empty slot
        int selId = -1;                                              // detail-panel item
        const char* caption = "Your equipped weapon.";
        const char* diffWord = "Normal";
    };
    void draw_equip(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                    std::uint32_t W, std::uint32_t H, const EquipView& e,
                    const ItemTable& items, const fs::path& asset_root) {
        const float sw = float(W), sh = float(H), A = sh / 768.0f;
        const float fwN = (4.0f / 3.0f) * sh / sw;
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };
        auto SY = [&](float y) { return y / 768.0f * sh; };
        // 0. goddess pillars + BACK00 frame (shared book shell)
        if (altpillar.tex)
            draw_screen_image(g, cview, dview, true, altpillar, -1.0f, 1.0f, AX(0), -1.0f, 0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
        if (altpillar.tex)   // right goddess strip anchored into the right margin (inner edge at AX(1024))
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f, 0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        if (frame.tex)
            draw_screen_image(g, cview, dview, altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0, 0, 1, 0.749f);
        // 1. "Equip" plaque -- the book's active plaque rests at anchor X=128 (MN4/VN1), not x=30.
        if (equipPlaque.tex)
            draw_screen_image(g, cview, dview, false, equipPlaque, AX(128), AY(8), AX(281), AY(68),
                              0.4f, 0.0f, 0.0f, 0.598f, 0.492f);
        // 2. LEFT stats panel (w_Status). Plate rect code-traced (FUN_005b05c0, VN1): {182,60,217.6,
        //    358.4}. Six stat rows at pitch 38.4 (FUN_005b0860), row0=92 -> {92,130.4,...,284.0}.
        if (wstatus.tex)
            draw_screen_image(g, cview, dview, false, wstatus, AX(182), AY(60), AX(399.6f), AY(418.4f),
                              0.55f, 0.0f, 0.0f, 0.420f, 0.635f);
        {
            const float psz = 30.0f * A;
            const std::string vals[6] = {
                std::to_string(e.level), std::to_string(e.hp) + "/" + std::to_string(e.maxhp),
                std::to_string(e.str), std::to_string(e.def), std::to_string(e.exp), std::to_string(e.next) };
            static const char* labs[6] = {"Lv.", "HP", "STR", "DEF", "Exp", "Next"};
            static const float ry[6] = {92, 130.4f, 168.8f, 207.2f, 245.6f, 284.0f};
            for (int i = 0; i < 6; ++i) {
                mf.text(labs[i], SX(223), SY(ry[i]), psz, sw, sh, 240, 240, 240, 255);         // label left
                float vw = mf.measure(vals[i], psz);
                mf.text(vals[i], SX(389.6f) - vw, SY(ry[i]), psz, sw, sh, 240, 240, 240, 255);  // value right
            }
        }
        // 3. RIGHT equip grid plate (w_Equip: WEAPON/ARMOR/BOOTS/ACCESSORIES/SKILLS, content u0..0.811 v0..0.650)
        if (wequip.tex)
            draw_screen_image(g, cview, dview, false, wequip, AX(438), AY(67), AX(844), AY(386),
                              0.55f, 0.0f, 0.0f, 0.811f, 0.650f);
        // 4. equipped-item cells (PAUSEBOOK.md §7.2). The equipped WEAPON/ARMOR/BOOTS/ACCESSORIES sit in
        //    column 0, rows 0-3 -> cell centres (483, 118.4/195.2/272.0/348.8); cell 54.4. Each equipped
        //    cell gets a blue highlight quad (0x4259999a) under its icon. The owned-item grid + skills
        //    cells are data-driven from runtime inventory state CleriaCore doesn't track yet (§7.4 gap).
        auto cellCenter = [](int col, int row, float& cx, float& cy) {
            cx = 483.0f + col * 64.0f; cy = 118.4f + row * 76.8f; };
        auto drawIcon = [&](int id, float cx, float cy, float sz, float z) {
            const SpriteTex* t = icon(g, items, asset_root, id);
            if (t) draw_screen_image(g, cview, dview, false, *t, AX(cx - sz / 2), AY(cy - sz / 2),
                                     AX(cx + sz / 2), AY(cy + sz / 2), z);
        };
        const int equipIds[4] = {e.weaponId, e.armorId, e.bootsId, e.accId};
        for (int s = 0; s < 4; ++s) {
            if (equipIds[s] < 0) continue;
            float cx, cy; cellCenter(0, s, cx, cy);
            if (blueHi.tex)   // equipped highlight quad (54.4), under the icon
                draw_screen_image(g, cview, dview, false, blueHi, AX(cx - 27.2f), AY(cy - 27.2f),
                                  AX(cx + 27.2f), AY(cy + 27.2f), 0.32f);
            drawIcon(equipIds[s], cx, cy, 54.4f, 0.3f);
        }
        // Grid cursor cell (SQUARE.dds, 51.2): defaults to the weapon cell (col0,row0). e.selId's cell
        // isn't tracked; the cursor marks the focused slot.
        if (square.tex) {
            float cx, cy; cellCenter(0, 0, cx, cy);
            draw_screen_image(g, cview, dview, false, square, AX(cx - 25.6f), AY(cy - 25.6f),
                              AX(cx + 25.6f), AY(cy + 25.6f), 0.28f);
        }
        // 5. bottom item-detail panel: darken the area, big icon on the left, name + description.
        //    Reference-measured (MN4 [H-med]): the whole panel sits ~100px further left than the old
        //    {178,..,1010} -> panel x[74,914], big icon centre (203,566), name (383,488).
        if (const Item* sel = items.get(e.selId)) {
            if (wdetail.tex)   // W_FS_MSG ornate dark panel (content u0..0.893 v0..0.949)
                draw_screen_image(g, cview, dview, false, wdetail, AX(74), AY(446), AX(914), AY(682),
                                  0.5f, 0.0f, 0.0f, 0.893f, 0.949f);
            drawIcon(e.selId, 203.0f, 566.0f, 150.0f, 0.35f);
            mf.text(sel->name, SX(383), SY(488), 34.0f * A, sw, sh, 246, 246, 246, 255);
            if (sel->magnitude) {   // "STR +N" / "DEF +N" bonus line (unit depends on category)
                char b[32]; std::snprintf(b, sizeof b, "STR +%u", sel->magnitude);
                mf.text(b, SX(770), SY(488), 34.0f * A, sw, sh, 246, 246, 246, 255);
            }
            std::string d = sel->description; float dy = 552.0f; std::size_t pos = 0;
            while (pos < d.size() && dy < 676.0f) {
                std::size_t nl = d.find_first_of("\r\n", pos);
                std::string line = d.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                if (!line.empty()) mf.text(line, SX(383), SY(dy), 30.0f * A, sw, sh, 236, 236, 236, 255);
                dy += 40.0f;
                if (nl == std::string::npos) break;
                pos = d.find_first_not_of("\r\n", nl);
                if (pos == std::string::npos) break;
            }
        }
        // 6. caption bar (left) + difficulty word (right)
        const float csz = 27.0f * A;
        mf.text(e.caption, SX(48), SY(707), csz, sw, sh, 236, 236, 236, 255);
        if (e.diffWord) { float w = mf.measure(e.diffWord, csz); mf.text(e.diffWord, SX(1006) - w, SY(707), csz, sw, sh, 236, 236, 236, 255); }
        mf.draw(g, cview);
    }

    // The pause-book ITEM/Inventory page (PAUSEBOOK.md §4). Same shell + Status panel as Equip, but the
    // right side is the W_INVENT 6x5 owned-items grid (cell lattice X=441+col*75, Y=72+row*64, cell 54.4),
    // and there is no bottom detail panel (a use-prompt appears on select instead -- not modelled). `owned`
    // holds up to 30 item ids in cell order (-1 = empty). Stats reuse EquipView.
    void draw_item(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                   std::uint32_t W, std::uint32_t H, const EquipView& e,
                   const int* owned, int ownedCount,
                   const ItemTable& items, const fs::path& asset_root) {
        const float sw = float(W), sh = float(H), A = sh / 768.0f;
        const float fwN = (4.0f / 3.0f) * sh / sw;
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };
        auto SY = [&](float y) { return y / 768.0f * sh; };
        // 0. shell
        if (altpillar.tex)
            draw_screen_image(g, cview, dview, true, altpillar, -1.0f, 1.0f, AX(0), -1.0f, 0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
        if (altpillar.tex)   // right goddess strip anchored into the right margin (inner edge at AX(1024))
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f, 0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        if (frame.tex)
            draw_screen_image(g, cview, dview, altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0, 0, 1, 0.749f);
        // 1. "Inventory" plaque -- active plaque anchor X=128 (MN5), not x=30.
        if (invPlaque.tex)
            draw_screen_image(g, cview, dview, false, invPlaque, AX(128), AY(8), AX(281), AY(68),
                              0.4f, 0.0f, 0.0f, 0.598f, 0.492f);
        // 2. LEFT stats panel (identical to Equip): plate {182,60,217.6,358.4}, six rows pitch 38.4.
        if (wstatus.tex)
            draw_screen_image(g, cview, dview, false, wstatus, AX(182), AY(60), AX(399.6f), AY(418.4f),
                              0.55f, 0.0f, 0.0f, 0.420f, 0.635f);
        {
            const float psz = 30.0f * A;
            const std::string vals[6] = {
                std::to_string(e.level), std::to_string(e.hp) + "/" + std::to_string(e.maxhp),
                std::to_string(e.str), std::to_string(e.def), std::to_string(e.exp), std::to_string(e.next) };
            static const char* labs[6] = {"Lv.", "HP", "STR", "DEF", "Exp", "Next"};
            static const float ry[6] = {92, 130.4f, 168.8f, 207.2f, 245.6f, 284.0f};
            for (int i = 0; i < 6; ++i) {
                mf.text(labs[i], SX(223), SY(ry[i]), psz, sw, sh, 240, 240, 240, 255);
                float vw = mf.measure(vals[i], psz);
                mf.text(vals[i], SX(389.6f) - vw, SY(ry[i]), psz, sw, sh, 240, 240, 240, 255);
            }
        }
        // 3. RIGHT inventory grid plate (W_INVENT). Code plate: TL (403,64), size 518.4x409.6 (MN5).
        if (winvent.tex)
            draw_screen_image(g, cview, dview, false, winvent, AX(403), AY(64), AX(921.4f), AY(473.6f),
                              0.55f, 0.0f, 0.0f, 0.992f, 0.678f);
        // 4. the 6x5 cell lattice. Code (FUN_005a9d10): cell CENTRE = (473+col*75, 104+row*64); the
        //    engine draws a 51.2 SQUARE tile in EVERY cell (dark 0x303030 when empty), the owned icon
        //    (54.4) over occupied cells, a 54.4 blue highlight on the selected item, and a 64 SQUARE
        //    cursor cell. Placement is data-driven per record; CleriaCore fills sequentially (stub).
        auto icell = [](int col, int row, float& cx, float& cy) {
            cx = 473.0f + col * 75.0f; cy = 104.0f + row * 64.0f; };
        const int cursorCell = 0;   // default focus (runtime nav not modelled headless)
        for (int i = 0; i < 30; ++i) {
            const int col = i % 6, row = i / 6;
            float cx, cy; icell(col, row, cx, cy);
            const int id = (i < ownedCount) ? owned[i] : -1;
            if (square.tex) {   // per-cell stone tile; darker when empty
                const float d = (id < 0) ? 0.19f : 0.55f;   // 0x303030 empty / lighter when filled
                draw_screen_image(g, cview, dview, false, square, AX(cx - 25.6f), AY(cy - 25.6f),
                                  AX(cx + 25.6f), AY(cy + 25.6f), 0.34f, 0.0f, 0.0f, 1.0f, 1.0f,
                                  0.02f, 0.02f, 0.05f, 0.0f, false, d, d, d);
            }
            if (id >= 0) {
                if (i == cursorCell && blueHi.tex)   // selected-item blue highlight (54.4)
                    draw_screen_image(g, cview, dview, false, blueHi, AX(cx - 27.2f), AY(cy - 27.2f),
                                      AX(cx + 27.2f), AY(cy + 27.2f), 0.32f);
                const SpriteTex* t = icon(g, items, asset_root, id);
                if (t) draw_screen_image(g, cview, dview, false, *t, AX(cx - 27.2f), AY(cy - 27.2f),
                                         AX(cx + 27.2f), AY(cy + 27.2f), 0.3f);
            }
        }
        if (square.tex) {   // cursor cell: 64 SQUARE concentric with the focused cell
            const int col = cursorCell % 6, row = cursorCell / 6;
            float cx, cy; icell(col, row, cx, cy);
            draw_screen_image(g, cview, dview, false, square, AX(cx - 32.0f), AY(cy - 32.0f),
                              AX(cx + 32.0f), AY(cy + 32.0f), 0.26f);
        }
        // 5. caption + difficulty word
        const float csz = 27.0f * A;
        mf.text(e.caption, SX(48), SY(707), csz, sw, sh, 236, 236, 236, 255);
        if (e.diffWord) { float w = mf.measure(e.diffWord, csz); mf.text(e.diffWord, SX(1006) - w, SY(707), csz, sw, sh, 236, 236, 236, 255); }
        mf.draw(g, cview);
    }

    // The pause-book TOPIC category menu (PAUSEBOOK.md §4): Character / Monster / Manual, each a stacked
    // ornate button (TOPIC_BTN0/1/2, baked labels; content u0..0.69 v0..0.668), with the cyan TOPIC_CUR
    // glow behind the selected one. sel = 0..2. The category sub-viewers (image pages) are a follow-up.
    void draw_topic(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                    std::uint32_t W, std::uint32_t H, int sel) {
        const float sw = float(W), sh = float(H), A = sh / 768.0f;
        const float fwN = (4.0f / 3.0f) * sh / sw;
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };
        auto SY = [&](float y) { return y / 768.0f * sh; };
        if (altpillar.tex)
            draw_screen_image(g, cview, dview, true, altpillar, -1.0f, 1.0f, AX(0), -1.0f, 0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
        if (altpillar.tex)   // right goddess strip anchored into the right margin (inner edge at AX(1024))
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f, 0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        if (frame.tex)
            draw_screen_image(g, cview, dview, altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0, 0, 1, 0.749f);
        if (topicPlaque.tex)   // "Topic" plaque top-left
            draw_screen_image(g, cview, dview, false, topicPlaque, AX(30), AY(8), AX(176), AY(68),
                              0.4f, 0.0f, 0.0f, 0.598f, 0.492f);
        // three stacked category buttons (overlapping to form one continuous ornate frame), then the
        // cyan TOPIC_CUR selection glow washed ADDITIVELY over the chosen row -- it brightens the row
        // toward teal (as the game does) without masking the baked label under it.
        static const float cy[3] = {212, 386, 558};   // authored row centres
        const float bx0 = 256.0f, bx1 = 800.0f, bh = 208.0f;
        for (int i = 0; i < 3; ++i)
            if (topicBtn[i].tex)
                draw_screen_image(g, cview, dview, false, topicBtn[i], AX(bx0), AY(cy[i] - bh * 0.5f),
                                  AX(bx1), AY(cy[i] + bh * 0.5f), 0.42f - i * 0.01f, 0.0f, 0.0f, 0.690f, 0.668f);
        if (sel >= 0 && sel < 3 && topicCur.tex)
            draw_screen_image(g, cview, dview, false, topicCur, AX(bx0 + 20), AY(cy[sel] - bh * 0.42f),
                              AX(bx1 - 20), AY(cy[sel] + bh * 0.38f), 0.30f, 0.033f, 0.0f, 0.693f, 0.809f,
                              0.02f, 0.02f, 0.05f, 0.0f, /*additive*/true);
        mf.text("Browse monster, character or tutorial data.", SX(48), SY(707), 27.0f * A, sw, sh, 236, 236, 236, 255);
        mf.draw(g, cview);
    }

    // The Bonus Shop (SHOP_LAYOUT.md, host GoWndShop2). Two-layer backdrop: data\menu\Back01.dds
    // full-screen + the data\image\t_400a.dds Feena illustration at {-288,-116.8}/819 (left-anchored,
    // scale H/768). The framework-A go_wnddialog TEXT LIST anchors at origin {672,192}; SP counter is
    // LEFT-anchored at {608,656}; description at {48,704}. `items` are pre-formatted rows.
    void draw_shop(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                   std::uint32_t W, std::uint32_t H, const char* const* items, int count, int sel, int arenaSP) {
        const float sw = float(W), sh = float(H);
        const float S = sh / 768.0f;                     // authored -> screen px, LEFT-anchored (x=0 at left)
        auto sxp = [&](float x) { return x * S; };
        auto syp = [&](float y) { return y * S; };
        // 1. Back01 backdrop (1024x768) stretched full-screen, then the Feena key-art over it.
        if (shopBack.tex)
            draw_screen_image(g, cview, dview, true, shopBack, -1, 1, 1, -1, 0.9f, 0, 0, 1, 1);
        else
            draw_screen_image(g, cview, dview, true, dim, -1, 1, 1, -1, 0.9f, 0, 0, 1, 1, 0.03f, 0.02f, 0.05f);
        if (shopFeena.tex) {   // {-288,-116.8} size 819x819, left-anchored + H/768
            auto NX = [&](float px) { return 2.0f * px / sw - 1.0f; };
            auto NY = [&](float py) { return 1.0f - 2.0f * py / sh; };
            draw_screen_image(g, cview, dview, false, shopFeena,
                              NX(sxp(-288.0f)), NY(syp(-116.8f)), NX(sxp(-288.0f + 819.0f)), NY(syp(-116.8f + 819.0f)),
                              0.7f, 0, 0, 1, 1);
        }
        // 2. the go_wnddialog list, anchored at {672,192}. Caption at origin, rows below at pitch 44.8.
        const float pxH = 30.0f * S;
        const float ox = 672.0f, oy = 192.0f, pitchA = 44.8f;
        const float itemX = sxp(ox), hy = syp(oy);
        const float iy0 = syp(oy + 44.8f), pitch = pitchA * S;
        float maxW = mf.measure("Please select a bonus to unlock:", pxH);
        for (int i = 0; i < count; ++i) maxW = std::max(maxW, mf.measure(items[i], pxH));
        // DIALOG0 scroll frame around the caption..last row.
        const float fl = itemX - 40.0f * S, fr = itemX + maxW + 40.0f * S;
        draw_dialog_frame(g, cview, dview, fl, hy - 20.0f * S, fr, iy0 + (count - 1) * pitch + pxH + 20.0f * S, sw, sh);
        if (sel >= 0 && sel < count && curGlow.tex) {   // additive glow on the selected row
            const float mid = iy0 + sel * pitch + pxH * 0.46f, half = 16.0f * S, barW = maxW + 48.0f * S;
            draw_cursor_glow(g, cview, dview, itemX - 16.0f * S, mid - half, itemX - 16.0f * S + barW, mid + half, sw, sh);
        }
        { const char* hdr = "Please select a bonus to unlock:"; float hw = mf.measure(hdr, pxH);
          mf.text(hdr, (fl + fr) * 0.5f - hw * 0.5f, hy, pxH, sw, sh, 240, 246, 250, 255); }
        for (int i = 0; i < count; ++i) {
            const bool s = (i == sel);
            mf.text(items[i], itemX, iy0 + i * pitch, pxH, sw, sh, s ? 250 : 172, s ? 252 : 206, s ? 252 : 208, 255);
        }
        // 3. SP counter at y656. Code X is left-anchored 608, but under the (list-validated) H/768
        //    widescreen mapping that lands left of the grab, which sits it under the list -> centre it
        //    on the frame to match reference/bonusshop.png.
        { std::string sp = "Current [Arena SP]: " + std::to_string(arenaSP);
          float w = mf.measure(sp, pxH);
          mf.text(sp, (fl + fr) * 0.5f - w * 0.5f, syp(656.0f), pxH, sw, sh, 236, 236, 236, 255); }
        mf.text("You can use SP earned in Arena Mode to purchase special bonuses.",
                sxp(48.0f), syp(704.0f), 27.0f * S, sw, sh, 236, 236, 236, 255);
        mf.draw(g, cview);
    }

    // The pause-book SAVE / LOAD page (SAVELOAD.md): a file-tab row (1..8) + 8 slot bars from W_SVLD
    // (container panel u0..0.79 v0..0.556; selected bar sprite v0.571..0.625; tabs selected v0.703..0.75
    // / dim v0.762..0.806, each ~79px). CleriaCore has no save system, so every slot renders "No Data".
    // `save` = true for the Save plaque/caption, false for Load. fileTab 0..7, selSlot 0..7.
    void draw_svld(Gpu& g, MenuFont& mf, WGPUTextureView cview, WGPUTextureView dview,
                   std::uint32_t W, std::uint32_t H, bool save, int fileTab, int selSlot) {
        const float sw = float(W), sh = float(H), A = sh / 768.0f;
        const float fwN = (4.0f / 3.0f) * sh / sw;
        auto AX = [&](float x) { return (2.0f * x / 1024.0f - 1.0f) * fwN; };
        auto AY = [&](float y) { return 1.0f - 2.0f * y / 768.0f; };
        auto SX = [&](float x) { return (AX(x) + 1.0f) * 0.5f * sw; };
        auto SY = [&](float y) { return y / 768.0f * sh; };
        constexpr float T = 1.0f / 1024.0f;
        if (altpillar.tex)
            draw_screen_image(g, cview, dview, true, altpillar, -1.0f, 1.0f, AX(0), -1.0f, 0.96f, 0.0f, 0.0f, 0.166f, 0.749f);
        if (altpillar.tex)   // right goddess strip anchored into the right margin (inner edge at AX(1024))
            draw_screen_image(g, cview, dview, false, altpillar, AX(1024), 1.0f, 1.0f, -1.0f, 0.96f, 0.833f, 0.0f, 1.0f, 0.749f);
        if (frame.tex)
            draw_screen_image(g, cview, dview, altpillar.tex ? false : true, frame,
                              AX(0), AY(0), AX(1024), AY(768), 0.95f, 0, 0, 1, 0.749f);
        const SpriteTex& plaque = (save && savePlaque.tex) ? savePlaque : loadPlaque;
        if (plaque.tex)   // active plaque anchor X=128 (same as the other book pages)
            draw_screen_image(g, cview, dview, false, plaque, AX(128), AY(8), AX(281), AY(68),
                              0.4f, 0.0f, 0.0f, 0.598f, 0.492f);
        // File-tab row 1..8 across the top (reference/pause_load.png shows this row + slots numbered
        // No.(fileTab*8+i+1) -> tab 6 = No.41..48; the MN9 "remove the tabs" note is refuted by the
        // grab). Selected tab = bright sprite (v0.708..0.759), others dim (v0.774..0.810), stride 79px.
        for (int i = 0; i < 8; ++i) {
            if (!wsvld.tex) break;
            const float tw = 72.0f, tx = 344.0f + i * tw, ty = 100.0f;
            const float u0 = (i * 79 + 4) * T, u1 = (i * 79 + 74) * T;
            const float v0 = (i == fileTab ? 725 : 793) * T, v1 = (i == fileTab ? 777 : 829) * T;
            draw_screen_image(g, cview, dview, false, wsvld, AX(tx), AY(ty), AX(tx + tw - 4), AY(ty + 44),
                              0.35f, u0, v0, u1, v1);
        }
        // 8 slot bars -- individual W_SVLD arrow plates (the reference draws raised bars, NOT the flat
        // container grooves). Bar top Y = 130 + 72*slot (SVLD_LAYOUT.md), x[152.4,895], solid arrow
        // sprite u[0,0.714] v[0.587,0.643]; the focused slot is brightened. CleriaCore has no save
        // system, so every slot renders "No Data" (the occupied preview card is a runtime-state gap).
        const float barX0 = 152.4f, barX1 = 895.0f, barTop0 = 130.0f, barPitch = 72.0f, barH = 58.0f;
        const float tsz = 26.0f * A;
        for (int i = 0; i < 8; ++i) {
            const float yt = barTop0 + i * barPitch, cy = yt + barH * 0.5f;
            if (wsvld.tex) {
                const float wt = (i == selSlot) ? 0.28f : 0.0f;   // brighten the focused slot
                draw_screen_image(g, cview, dview, false, wsvld, AX(barX0), AY(yt), AX(barX1), AY(yt + barH),
                                  0.45f, 0.0f, 0.587f, 0.714f, 0.643f, 0.02f, 0.02f, 0.05f, wt);
            }
            char no[16]; std::snprintf(no, sizeof no, "No.%02d", fileTab * 8 + i + 1);
            mf.text(no, SX(barX0 + 26), SY(cy - 13), tsz, sw, sh, 236, 236, 236, 255);
            const char* nd = "-------------- No Data --------------";
            float w = mf.measure(nd, tsz);
            mf.text(nd, SX(560) - w * 0.5f, SY(cy - 13), tsz, sw, sh, 210, 214, 220, 255);
        }
        mf.text(save ? "Save the current gameplay session." : "Load a previously saved gameplay session.",
                SX(48), SY(707), tsz, sw, sh, 236, 236, 236, 255);
        mf.draw(g, cview);
    }
};

// Locate the game's Felghana font (Release/yso_ins04.dat) for MenuFont. It is loose in the game's
// release dir, not in the extracted asset pack, so check CLERIA_FONT_FILE and a few conventional
// spots; empty -> MenuFont falls back to a system serif.
static fs::path find_felghana(const fs::path& asset_root) {
    if (const char* e = std::getenv("CLERIA_FONT_FILE")) {
        fs::path p = e; std::error_code ec; if (fs::exists(p, ec)) return p;
    }
    const fs::path cands[] = {
        asset_root / "yso_ins04.dat",
        asset_root / "FONT" / "yso_ins04.dat",
        asset_root.parent_path() / "release" / "yso_ins04.dat",
        asset_root.parent_path() / "Release" / "yso_ins04.dat",
    };
    for (const auto& p : cands) { std::error_code ec; if (fs::exists(p, ec)) return p; }
    return {};
}

// Headless title render (--title out.ppm), to verify the composition off-window.
// mode: 0 main menu, 1 character-select, 2 bonus sub-list.
static void options_load(int slider[3], bool chk[4]);   // yso_win.ini loader (defined below render_window)
static int render_title(Gpu& g, const fs::path& asset_root, const fs::path& out,
                        std::uint32_t W, std::uint32_t H, int mode = 0) {
    TitleFx title;
    if (!title.init(g, asset_root)) { std::fprintf(stderr, "title: TITLE_000.DDS load failed\n"); return 1; }
    g_fog.on = false;
    write_mvp(g, Mat4::identity());   // screen space: RoomVertex positions ARE clip space
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

    MenuFont mf;
    // Rasterise at the native device-pixel em size, GGO_GRAY4 (FONT_RASTER.md): the engine's LOGFONT
    // height is screenH*32/768 and it rasterises per-size, so match that exactly.
    // The game renders its 2D overlay at the native 1280x960 internal backbuffer, then upscales to
    // the display. So rasterise the glyph atlas at the 960-height font size (32*960/768 = 40) and let
    // the draw bilinear-upscale it to screen -- this reproduces the game's characteristic softness
    // (a full-res crisp atlas over-sharpens vs the reference). build/menu_re/title_pixeldiff.py.
    int atlasPx = int(std::lround(32.0 * 960.0 / 768.0));
    if (const char* e = std::getenv("CLERIA_ATLAS_PX")) { int v = std::atoi(e); if (v >= 8 && v <= 400) atlasPx = v; }
    mf.init(g, WGPUTextureFormat_RGBA8Unorm, find_felghana(asset_root), atlasPx);
    if (mode == 1) {
        int cs = 0;   // CLERIA_CS_SEL=0|1|2 previews Yunica/Hugo/Toal off-window
        if (const char* e = std::getenv("CLERIA_CS_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 2) cs = v; }
        float pulse = 0.72f;   // CLERIA_CS_PULSE phases the selection-bar glow (matches the ref frame)
        if (const char* e = std::getenv("CLERIA_CS_PULSE")) pulse = float(std::atof(e));
        title.draw_charselect(g, mf, cview, dview, W, H, cs, pulse, std::getenv("CLERIA_CS_CHIBI") ? std::atoi(std::getenv("CLERIA_CS_CHIBI")) : 0);
        // CLERIA_POPUP=type|diff|sp overlays a New-Game popup for off-window verification.
        if (const char* e = std::getenv("CLERIA_POPUP")) {
            static const char* ti[3] = {"Normal", "Extra", "Cancel"};
            static const char* di[6] = {"Very Easy", "Easy", "Normal", "Hard", "Nightmare", "Cancel"};
            static const char* si[2] = {"[Enable]", "[Disable]"};
            if (std::string(e) == "type") title.draw_popup(g, mf, cview, dview, W, H, "Type Select", ti, 3, 0);
            else if (std::string(e) == "diff") title.draw_popup(g, mf, cview, dview, W, H, "Difficulty Level", di, 6, 2);
            else if (std::string(e) == "sp") title.draw_popup(g, mf, cview, dview, W, H, "SP Bonus Acquisition", si, 2, 0);
        }
    } else if (mode == 2) {
        title.draw_bonus(g, mf, cview, dview, W, H, /*sel=*/0);        // Time Attack lit
    } else if (mode == 3) {
        int osel = 0;   // CLERIA_OPT_SEL = focused row 0..9
        if (const char* e = std::getenv("CLERIA_OPT_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 9) osel = v; }
        int sliders[3] = {512, 512, 512};                     // reflects yso_win.ini if present
        bool checks[4] = {false, true, false, false};
        options_load(sliders, checks);
        title.draw_options(g, mf, cview, dview, W, H, osel, sliders, checks);
    } else if (mode == 4) {
        // Pause-book Equip page preview. Stats from GAMEINFO curves; equipped items stubbed
        // (CleriaCore has no runtime equip state yet). CLERIA_CS_SEL = character, CLERIA_LEVEL = level.
        const GameInfo ginfo = GameInfo::load(asset_root / "MISC" / "GAMEINFO.DAT");
        const ItemTable items = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
        int ch = 1; if (const char* e = std::getenv("CLERIA_CS_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 2) ch = v + 1; }
        int lvl = 52; if (const char* e = std::getenv("CLERIA_LEVEL")) { int v = std::atoi(e); if (v >= 1 && v <= 60) lvl = v; }
        const int tier = 4;   // stub weapon tier for the STR figure
        TitleFx::EquipView ev;
        ev.character = ch; ev.level = lvl;
        if (ginfo.ok()) {
            ev.maxhp = ev.hp = int(std::lround(ginfo.curves[kPlayerMaxHp].at(lvl)));
            ev.str = int(std::lround(ginfo.player_atk(ch, tier, lvl)));
            ev.def = int(std::lround(ginfo.curves[kPlayerDef].at(lvl)));
            ev.exp = long(std::llround(ginfo.exp_to_level(lvl)));
            ev.next = long(std::llround(ginfo.exp_to_level(lvl + 1) - ginfo.exp_to_level(lvl)));
        }
        ev.weaponId = 1; ev.armorId = 8; ev.selId = 1;   // stub equipped set (weapon shown in the detail)
        title.draw_equip(g, mf, cview, dview, W, H, ev, items, asset_root);
    } else if (mode == 5) {
        // Pause-book Item/Inventory page preview. Stats as Equip; owned items stubbed.
        const GameInfo ginfo = GameInfo::load(asset_root / "MISC" / "GAMEINFO.DAT");
        const ItemTable items = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
        int ch = 1; if (const char* e = std::getenv("CLERIA_CS_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 2) ch = v + 1; }
        int lvl = 52; if (const char* e = std::getenv("CLERIA_LEVEL")) { int v = std::atoi(e); if (v >= 1 && v <= 60) lvl = v; }
        TitleFx::EquipView ev; ev.character = ch; ev.level = lvl; ev.caption = "Examine or use an item.";
        if (ginfo.ok()) {
            ev.maxhp = ev.hp = int(std::lround(ginfo.curves[kPlayerMaxHp].at(lvl)));
            ev.str = int(std::lround(ginfo.player_atk(ch, 4, lvl)));
            ev.def = int(std::lround(ginfo.curves[kPlayerDef].at(lvl)));
            ev.exp = long(std::llround(ginfo.exp_to_level(lvl)));
            ev.next = long(std::llround(ginfo.exp_to_level(lvl + 1) - ginfo.exp_to_level(lvl)));
        }
        const int owned[] = {1, 48, 8, 78, 79, 116};   // stub: weapon, seed, armor, 2 medallions, bracelet
        title.draw_item(g, mf, cview, dview, W, H, ev, owned, 6, items, asset_root);
    } else if (mode == 6) {
        int sel = 0; if (const char* e = std::getenv("CLERIA_TOPIC_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 2) sel = v; }
        title.draw_topic(g, mf, cview, dview, W, H, sel);
    } else if (mode == 7) {
        // Bonus Shop preview (BONUSSHOP.md item list). Unlock state stubbed.
        static const char* shopItems[] = {
            "[5,000] Arena Stage 2", "[Unlocked] Adol (Felghana Version)",
            "[Unlocked] Yunica (Extra Version)", "[Unlocked] Hugo (Extra Version)",
            "[Unlocked] Toal (Extra Version)", "[Unlocked] Adol (Napishtim Version)", "Cancel"};
        int sel = 0; if (const char* e = std::getenv("CLERIA_SHOP_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 6) sel = v; }
        title.draw_shop(g, mf, cview, dview, W, H, shopItems, 7, sel, 2811);
    } else if (mode == 8 || mode == 9) {
        title.draw_svld(g, mf, cview, dview, W, H, /*save=*/mode == 9, /*fileTab=*/5, /*selSlot=*/0);
    } else {
        int ts = 1;   // CLERIA_TITLE_SEL overrides which item is lit (0 New Game ... 4 Exit)
        if (const char* e = std::getenv("CLERIA_TITLE_SEL")) { int v = std::atoi(e); if (v >= 0 && v <= 4) ts = v; }
        title.draw(g, cview, dview, W, H);
        title.draw_menu(g, mf, cview, dview, W, H, ts);
    }
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
    const auto* px = static_cast<const std::uint8_t*>(wgpuBufferGetConstMappedRange(readback, 0, rbd.size));
    std::ofstream f(out, std::ios::binary);
    f << "P6\n" << W << " " << H << "\n255\n";
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
            f.write(reinterpret_cast<const char*>(px + std::size_t(y) * bpr + x * 4), 3);
    wgpuBufferUnmap(readback);
    std::printf("wrote %s (%ux%u)\n", out.string().c_str(), W, H);
    return 0;
}

// ---- Option-screen persistence: yso_win.ini (build/menu_re/OPTIONS.md §3) -------------------
// The game stores config in yso_win.ini via FUN_0040b8e0 (load) / FUN_0040d7e0 (save): sections
// [sound] BgmVolume/EffectVolume (0..1024 decimal strings), [graphics] GammaValue ("%0.8f" float),
// [input] PadAnalog/DashControl/AtkBtnMagic/AlwaysDashOK (0/1). We read-modify-write so the file's
// other keys (PlayerName, TitleGameDifficulty, ...) survive. Slider<->gamma per OPTIONS.md §2.
static float slider_to_gamma(int s) {   // 0..1024 -> 0.5..2.0, neutral 1.0 @512
    return s <= 512 ? float(s) / 512.0f * 0.5f + 0.5f : float(s - 512) / 512.0f + 1.0f;
}
static int gamma_to_slider(float g) {
    float s = g <= 1.0f ? (g - 0.5f) * 1024.0f : (g - 1.0f) * 512.0f + 512.0f;
    return std::clamp(int(std::lround(s)), 0, 1024);
}
static fs::path options_ini_path() {
    if (const char* e = std::getenv("CLERIA_INI")) return e;
    std::error_code ec; return fs::current_path(ec) / "yso_win.ini";
}
// A line-preserving INI editor (keeps comments / unknown keys / section order).
struct IniFile {
    std::vector<std::string> lines;
    static std::string trim(const std::string& s) {
        std::size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        std::size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }
    void load(const fs::path& p) {
        std::ifstream f(p); std::string l;
        while (std::getline(f, l)) { if (!l.empty() && l.back() == '\r') l.pop_back(); lines.push_back(l); }
    }
    void save(const fs::path& p) const {
        std::ofstream f(p, std::ios::binary);
        for (const auto& l : lines) f << l << "\r\n";
    }
    bool get(const std::string& sec, const std::string& key, std::string& out) const {
        std::string cur;
        for (const auto& raw : lines) {
            std::string t = trim(raw);
            if (t.size() >= 2 && t.front() == '[' && t.back() == ']') { cur = t.substr(1, t.size() - 2); continue; }
            if (cur != sec) continue;
            std::size_t eq = t.find('=');
            if (eq != std::string::npos && trim(t.substr(0, eq)) == key) { out = trim(t.substr(eq + 1)); return true; }
        }
        return false;
    }
    void set(const std::string& sec, const std::string& key, const std::string& val) {
        int hdr = -1, bodyEnd = int(lines.size());
        for (int i = 0; i < int(lines.size()); ++i) {
            std::string t = trim(lines[i]);
            if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
                if (hdr < 0 && t.substr(1, t.size() - 2) == sec) hdr = i;
                else if (hdr >= 0) { bodyEnd = i; break; }
            }
        }
        if (hdr >= 0) {
            for (int i = hdr + 1; i < bodyEnd; ++i) {
                std::string t = trim(lines[i]);
                std::size_t eq = t.find('=');
                if (eq != std::string::npos && trim(t.substr(0, eq)) == key) { lines[i] = key + "=" + val; return; }
            }
            lines.insert(lines.begin() + bodyEnd, key + "=" + val);
        } else {
            if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
            lines.push_back("[" + sec + "]");
            lines.push_back(key + "=" + val);
        }
    }
};
// slider[0..2] = BGM / SFX / Brightness (0..1024); chk[0..3] = rows 3..6 (Analog/Dash/HoldAtk/AlwaysDash).
static void options_load(int slider[3], bool chk[4]) {
    IniFile f; f.load(options_ini_path()); std::string v;
    auto geti = [&](const char* s, const char* k, int def) { return f.get(s, k, v) ? std::atoi(v.c_str()) : def; };
    slider[0] = std::clamp(geti("sound", "BgmVolume", 512), 0, 1024);
    slider[1] = std::clamp(geti("sound", "EffectVolume", 512), 0, 1024);
    slider[2] = f.get("graphics", "GammaValue", v) ? gamma_to_slider(float(std::atof(v.c_str()))) : 512;
    chk[0] = geti("input", "PadAnalog", 0) != 0;
    chk[1] = geti("input", "DashControl", 1) != 0;
    chk[2] = geti("input", "AtkBtnMagic", 0) != 0;
    chk[3] = geti("input", "AlwaysDashOK", 0) != 0;
}
static void options_save(const int slider[3], const bool chk[4]) {
    const fs::path p = options_ini_path();
    IniFile f; f.load(p);   // preserve existing content
    f.set("sound", "BgmVolume", std::to_string(slider[0]));
    f.set("sound", "EffectVolume", std::to_string(slider[1]));
    char g[32]; std::snprintf(g, sizeof g, "%0.8f", slider_to_gamma(slider[2]));
    f.set("graphics", "GammaValue", g);
    f.set("input", "PadAnalog", chk[0] ? "1" : "0");
    f.set("input", "DashControl", chk[1] ? "1" : "0");
    f.set("input", "AtkBtnMagic", chk[2] ? "1" : "0");
    f.set("input", "AlwaysDashOK", chk[3] ? "1" : "0");
    f.save(p);
}

// ---- interactive walkthrough ------------------------------------------------
static int render_window(Gpu& g, const fs::path& asset_root, const fs::path& first_sob,
                         bool show_title = false) {
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
    // Prefer the LINEAR (non-sRGB) surface format. Our textures are already sRGB and the shaders
    // pass them straight through; if the surface is Rgba8UnormSrgb, presenting sRGB-encodes a
    // SECOND time and the window washes out. The offscreen renders use linear RGBA8Unorm and look
    // correct -- match that here so all pipelines + the surface stay consistent.
    WGPUTextureFormat fmt = caps.formats[0];
    for (std::size_t i = 0; i < caps.formatCount; ++i) {
        const WGPUTextureFormat f = caps.formats[i];
        if (f == WGPUTextureFormat_RGBA8Unorm || f == WGPUTextureFormat_BGRA8Unorm) { fmt = f; break; }
    }
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
    ShadowFx shadows;                    // floor shadow decals under characters
    if (!shadows.init(g, asset_root))
        std::fprintf(stderr, "shadows: SHADOW0.DDS failed to load\n");

    // The starting character (g_flags[150]: 1 Yunica / 2 Hugo / 3 Toal). CLERIA_CHAR=1|2|3
    // forces it (Tab still cycles in-game); on a normal launch the title's New Game -> the
    // character-select screen sets it. Declared here, before the title phase, so that screen
    // can write it. Toal (3, C_104) is the only shipping player sprite with AIA second layers,
    // so he is the one that exercises the composite path.
    int character = 1;
    if (const char* cs = std::getenv("CLERIA_CHAR")) {
        const int c = std::atoi(cs);
        if (c >= 1 && c <= 3) character = c;
    }
    // The rest of the New-Game choices (set by the title's popups; see the flow below):
    int skin = 0;          // 0 Normal, 1 Extra (alternate appearance -- swap pending skin RE)
    int difficulty = 2;    // 0 VeryEasy,1 Easy,2 Normal,3 Hard,4 Nightmare (persisted TitleGameDifficulty)
    bool sp_bonus = false; // "SP Bonus Acquisition": boosts this save's SP gains
    // Option settings (persisted in yso_win.ini). Function-scope so the title menu edits them and
    // the game loop (audio) can read them; loaded once up front.
    int oslider[3] = {512, 512, 512};    // BGM / SFX / Brightness (0..1024)
    bool ocheck[4] = {false, true, false, false};   // Analog / Dash / HoldAtk / AlwaysDash (rows 3..6)
    options_load(oslider, ocheck);
    (void)skin; (void)difficulty; (void)sp_bonus;   // stored now; applied to combat as RE lands

    // Audio comes up before the title screen: the title is a scene in its own
    // right (SCENELIST record "title\", BGM id 1), and `transition` far below
    // captures this to switch tracks on room entry.
    Audio audio;
    audio.init(asset_root);  // silent (and harmless) if the device or EFFECTDEF is missing
    audio.set_master_volume(oslider[1] / 1024.0f);  // Option "SFX Volume"
    audio.set_bgm_volume(oslider[0] / 1024.0f);     // Option "BGM Volume"

    // Per-room music: SCENELIST record byte +3 is a bgmdef id. The exe (re)starts
    // the track only when that id CHANGES, so walking between rooms that share one
    // plays straight through the doorway; id 0 means STOP, not "inherit".
    // See build/audio_re/BGM.md §2-3.
    SceneList scenes;
    BgmDefs bgmdefs;
    if (!scenes.load(asset_root))
        std::printf("audio: no MAP/SCENELIST.SL — no per-room BGM\n");
    else if (!bgmdefs.load(asset_root))
        std::printf("audio: no music folder found — set CLERIA_MUSIC_DIR to the game's "
                    "release\\music to hear BGM\n");
    else
        std::printf("audio: %zu scenes, music from %s\n", scenes.size(),
                    bgmdefs.dir().string().c_str());
    int cur_bgm = -1;
    auto apply_room_bgm = [&](const std::string& room_name) {
        const SceneRec* s = scenes.find_room(room_name);
        const int id = s ? int(s->bgm) : 0;
        if (id == cur_bgm) return;
        cur_bgm = id;
        if (id == 0)
            audio.stop_bgm();
        else
            audio.play_bgm(bgmdefs.track(id));
    };

    // Title screen: shown on a bare launch, before the game. New Game opens character-select
    // (pick Yunica/Hugo/Toal, then fall through to the game loop below); Esc/close quits.
    // Assets + flow from build/menu_re/MENUS.md.
    if (show_title) {
        apply_room_bgm("title");   // "title\" is a scene record like any room (BGM id 1)
        TitleFx title; title.init(g, asset_root);
        MenuFont mf;   // Felghana atlas rasterised at the 960-internal font size (see render_title),
        // bilinear-upscaled to the window. Format must match the window surface (fmt), not a
        // hardcoded RGBA8Unorm, or the pipeline is incompatible with an sRGB surface pass.
        mf.init(g, fmt, find_felghana(asset_root), int(std::lround(32.0 * 960.0 / 768.0)));
        WGPUTextureView tdepth = make_depth(g.device, cw, ch);
        g_fog.on = false; write_mvp(g, Mat4::identity());   // screen-space draw
        // The title is a state machine mirroring the engine (MENUS.md §1.4): the MAIN list, the
        // BONUS sub-list (DAT_0074e259), and the New-Game chain CHARSELECT -> TYPESELECT (Normal/
        // Extra skin) -> DIFFICULTY (Easy..Nightmare) -> SPBONUS (SP-gain boost) -> start.
        enum TState { MAIN, BONUS, CHARSELECT, TYPESELECT, DIFFICULTY, SPBONUS, OPTIONS };
        static const char* kTypeItems[3] = {"Normal", "Extra", "Cancel"};
        static const char* kDiffItems[6] = {"Very Easy", "Easy", "Normal", "Hard", "Nightmare", "Cancel"};
        static const char* kSpItems[2]  = {"[Enable]", "[Disable]"};
        TState ts = MAIN;
        int sel = 0;      // main menu: 0 New Game, 1 Load, 2 Bonus Modes, 3 Options, 4 Exit
        int bsel = 0;     // bonus sub-list: 0 Time Attack, 1 Arena, 2 Bonus Shop, 3 Back
        int csel = 0;     // character-select: 0 Yunica, 1 Hugo, 2 Toal
        int tsel = 0, dsel = 2, spsel = 0;   // popup cursors (difficulty defaults Normal, SP-bonus Enable)
        int osel = 0;                        // Option screen: focused row 0..9 (oslider/ocheck are
                                             // in render_window scope, above -- persisted to yso_win.ini)
        auto teardown = [&] { wgpuTextureViewRelease(tdepth); SDL_DestroyWindow(win); SDL_Quit(); };
        for (bool started = false; !started;) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_EVENT_QUIT) { teardown(); return 0; }
                else if (e.type == SDL_EVENT_KEY_DOWN) {
                    const SDL_Keycode k = e.key.key;
                    const bool up = (k == SDLK_UP || k == SDLK_W);
                    const bool down = (k == SDLK_DOWN || k == SDLK_S);
                    const bool ok = (k == SDLK_RETURN || k == SDLK_SPACE || k == SDLK_E);
                    const bool esc = (k == SDLK_ESCAPE);
                    const bool left = (k == SDLK_LEFT || k == SDLK_A);
                    const bool right = (k == SDLK_RIGHT || k == SDLK_D);
                    auto wrap = [](int v, int n, bool u) { return (v + (u ? n - 1 : 1)) % n; };
                    // Menu SFX, hooked off the key classification rather than at each of the
                    // ten state branches below. up/down always moves a cursor; left/right only
                    // does anything on the Option page (sliders/checkboxes), so it only clicks
                    // there. The exe's exact call sites are being RE'd separately.
                    if (up || down || ((left || right) && ts == OPTIONS))
                        audio.play(kSfxCursor, kSfxMenuVol);
                    else if (ok)
                        audio.play(kSfxConfirm, kSfxMenuVol);
                    else if (esc)
                        audio.play(kSfxCancel, kSfxMenuVol);
                    if (ts == CHARSELECT) {
                        if (esc) ts = MAIN;                              // back to main menu
                        else if (up || down) csel = wrap(csel, TitleFx::kCharCount, up);
                        else if (ok) { character = csel + 1; ts = TYPESELECT; tsel = 0; }
                    } else if (ts == TYPESELECT) {
                        if (esc) ts = CHARSELECT;
                        else if (up || down) tsel = wrap(tsel, 3, up);
                        else if (ok) {
                            if (tsel == 2) ts = CHARSELECT;              // Cancel
                            else { skin = tsel; ts = DIFFICULTY; dsel = 2; }  // Normal/Extra -> Difficulty (Normal)
                        }
                    } else if (ts == DIFFICULTY) {
                        if (esc) ts = TYPESELECT;
                        else if (up || down) dsel = wrap(dsel, 6, up);
                        else if (ok) {
                            if (dsel == 5) ts = TYPESELECT;              // Cancel
                            else { difficulty = dsel; ts = SPBONUS; spsel = 0; }  // 0 VeryEasy..4 Nightmare
                        }
                    } else if (ts == SPBONUS) {
                        if (esc) ts = DIFFICULTY;
                        else if (up || down) spsel = wrap(spsel, 2, up);
                        else if (ok) { sp_bonus = (spsel == 0); started = true; }  // -> game
                    } else if (ts == BONUS) {
                        if (esc) ts = MAIN;
                        else if (up || down) bsel = wrap(bsel, TitleFx::kBonusCount, up);
                        else if (ok) {
                            if (bsel == 3) ts = MAIN;   // Back
                            // Time Attack / Arena Mode / Bonus Shop: not yet implemented.
                        }
                    } else if (ts == OPTIONS) {
                        // 10 rows: 0-2 sliders, 3-6 checkboxes, 7 Default, 8 Return to Title, 9 Exit Game.
                        // Settings persist to yso_win.ini on leaving the screen (game saves config on close).
                        if (esc) { options_save(oslider, ocheck); ts = MAIN; }
                        else if (up || down) osel = wrap(osel, 10, up);
                        else if ((left || right) && osel < 3)
                            oslider[osel] = std::clamp(oslider[osel] + (right ? 8 : -8), 0, 1024);
                        else if (ok) {
                            if (osel >= 3 && osel <= 6) ocheck[osel - 3] = !ocheck[osel - 3];   // toggle
                            else if (osel == 7) {                                                // Default
                                oslider[0] = oslider[1] = oslider[2] = 512;
                                ocheck[0] = ocheck[2] = ocheck[3] = false; ocheck[1] = true;
                            } else if (osel == 8) { options_save(oslider, ocheck); ts = MAIN; }  // Return to Title
                            else if (osel == 9) { options_save(oslider, ocheck); teardown(); return 0; }  // Exit
                        }
                    } else {   // MAIN
                        if (esc) { teardown(); return 0; }
                        else if (up || down) sel = wrap(sel, TitleFx::kItemCount, up);
                        else if (ok) {
                            if (sel == 4) { teardown(); return 0; }   // Exit
                            else if (sel == 0) { ts = CHARSELECT; csel = 0; }  // New Game
                            else if (sel == 2) { ts = BONUS; bsel = 0; }       // Bonus Modes
                            else if (sel == 3) { ts = OPTIONS; osel = 0; }     // Options
                            // Load Game: not yet implemented.
                        }
                    }
                    // Drive the mixer from the sliders live, so you hear the level you are
                    // setting (and so Default resets it too). Applying these once at startup
                    // is not enough: Audio is constructed BEFORE this screen so the title has
                    // its BGM, which is earlier than any edit made here.
                    audio.set_bgm_volume(oslider[0] / 1024.0f);
                    audio.set_master_volume(oslider[1] / 1024.0f);
                } else if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                    cw = std::uint32_t(e.window.data1); ch = std::uint32_t(e.window.data2);
                    if (cw && ch) {
                        configure(); wgpuTextureViewRelease(tdepth); tdepth = make_depth(g.device, cw, ch);
                        mf.build_atlas(g, int(std::lround(32.0 * 960.0 / 768.0)));  // 960-internal font, upscaled
                    }
                }
            }
            WGPUSurfaceTexture st = {};
            wgpuSurfaceGetCurrentTexture(surface, &st);
            if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
                st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) { configure(); continue; }
            WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);
            // Selection-bar pulse: cyan <-> near-white, ~1s period (the game animates the lit bar).
            const float pulse = 0.50f + 0.42f * std::sin(SDL_GetTicks() * 0.006f);
            if (ts == CHARSELECT) {
                title.draw_charselect(g, mf, view, tdepth, cw, ch, csel, pulse, int(SDL_GetTicks() / 110));
            } else if (ts == TYPESELECT || ts == DIFFICULTY) {
                title.draw_charselect(g, mf, view, tdepth, cw, ch, csel, pulse, int(SDL_GetTicks() / 110));   // popup backdrop
                if (ts == TYPESELECT)
                    title.draw_popup(g, mf, view, tdepth, cw, ch, "Type Select", kTypeItems, 3, tsel);
                else
                    title.draw_popup(g, mf, view, tdepth, cw, ch, "Difficulty Level", kDiffItems, 6, dsel);
            } else if (ts == SPBONUS) {
                // SP Bonus Acquisition shares the invariant popup origin over the dimmed Character
                // Select (the on-black grab was an artifact; CHARSELECT_LAYOUT.md §2.3).
                title.draw_charselect(g, mf, view, tdepth, cw, ch, csel, pulse, int(SDL_GetTicks() / 110));
                title.draw_popup(g, mf, view, tdepth, cw, ch, "SP Bonus Acquisition", kSpItems, 2, spsel);
            } else if (ts == BONUS) {
                title.draw_bonus(g, mf, view, tdepth, cw, ch, bsel);
            } else if (ts == OPTIONS) {
                title.draw_options(g, mf, view, tdepth, cw, ch, osel, oslider, ocheck);
            } else {
                title.draw(g, view, tdepth, cw, ch);
                title.draw_menu(g, mf, view, tdepth, cw, ch, sel);
            }
            wgpuSurfacePresent(surface);
            wgpuTextureViewRelease(view); wgpuTextureRelease(st.texture);
        }
        wgpuTextureViewRelease(tdepth);   // the game loop rebuilds fog + its own MVP
    }
    float fx_time = 0;
    EnemyFx efx;                         // enemy sprite sets, per room
    // The game's own tables. Loaded once: the enemy chain is SCT name -> CASTINFO ->
    // ENEMYINFO -> GAMEINFO curve.
    const CastTable casts = CastTable::load(asset_root / "MISC" / "CASTINFO.DAT");
    const EnemyTable etab = EnemyTable::load(asset_root / "MISC" / "ENEMYINFO.DAT");
    const GameInfo ginfo = GameInfo::load(asset_root / "MISC" / "GAMEINFO.DAT");
    const EexTable eex = EexTable::load(asset_root / "MISC" / "EEX.DAT");  // boss stats
    // The live actors are real Actors in an ActorWorld now, not a bespoke list — one
    // spawn path, the deferred-sweep lifecycle, and the shared damage model. `render`
    // pairs each with its sprite for drawing.
    ActorWorld world({&casts, &etab, &ginfo, &eex});
    std::vector<EnemyRender> render;      // live enemies + their sprite identity
    // Our own RNG. The engine seeds its xorshift from timeGetTime(), so retail damage
    // was never replay-stable and there is no stream to match — only the algorithm's
    // shape matters, and an explicit seed buys us the determinism it lacked.
    std::mt19937 rng(20260717u);
    std::uniform_real_distribution<float> rng_pct(0.0f, 100.0f);
    // (No attack cooldown any more: the script's cancel window is the rate limit.)
    // The player's melee swing, from the ASO GenerateCollision hitbox (build/attack_re).
    // A swing arms a sphere for `atk_window` ticks; each tick it hits enemies inside it,
    // once per swing (the engine's hit-dedup). Yunica's basic swing (verified in
    // cleria-asotest): radius 0.6, 30-tick window, power 8.0, box 0.6 ahead of the actor.
    int atk_window = 0;                  // ticks left in the active hitbox
    // Recovery-hold tracking. Many attack sections end by looping on their final
    // frame with Movable pinned to 0, and several (Yunica 257/258/261, and the
    // like for Hugo/Toal) never open a cancel window at all — so `cancelable()`
    // alone would leave the player rooted there for good. The engine returns to
    // neutral via an action state machine we have not RE'd; stand in for it by
    // releasing control once the swing stops animating.
    long atk_frame_key = -1;
    int atk_stall = 0;
    // Projectiles. The effect registries resolve a bullet's eftlist id to its real
    // EF2 and texture; without them bullets simply do not spawn (rather than draw
    // something invented).
    EftList efl;
    EftTex eft;
    if (!efl.load(asset_root) || !eft.load(asset_root))
        std::printf("effects: EFTLIST/EFTTEX missing — projectiles disabled\n");
    std::vector<Projectile> shots;
    // The live hit-volume, copied from the script when GenerateCollision arms it.
    // Defaults are Yunica's combo #1 so a script that never arms behaves as before.
    AsoVm::Hitbox atk_hb{0.6f, 0.0f, 30, 8.0f};
    Vec3 atk_dir{0, 0, 1};               // the swing's facing (captured at swing start)
    std::set<int> atk_hit;               // instance ids already hit this swing (dedup)
    Vec3 face_dir{0, 0, 1};              // world dir the player faces (from movement)
    WGPUTextureView depth = make_depth(g.device, cw, ch);

    // `character` was chosen above (CLERIA_CHAR override or the title's character-select).
    XsoVm game;                        // persistent game state (survives rooms)
    game.g_flags[150] = character;
    // ** Yunica's weapon: flag 149 = 1 is her AXE (her normal walking-around look); the
    // uninitialised 0 is the Crimson Lotusblade (red sword), shown only with her fire
    // skill (aso.hpp). ** Her attack animations sit behind Flag_IsEqualInt(149,1), so
    // without this the swing renders the red sword. A global's initial value is not its
    // normal value -- set the normal one.
    game.g_flags[149] = 1;
    Player player;                     // stat block (HP/SP/level/EXP)
    // A fresh level-1 player: HP from the GAMEINFO player curve at level 1. There is no save
    // model, so a new run always starts at level 1 with 0 EXP -- the faithful new-game state.
    // CLERIA_LEVEL forces a starting level: the RE'd damage formula is steep in the target's
    // DEF (raw = ATK^0.66 * (ATK - 0.98*DEF + 10 + power)/22.92), so a lv1 player genuinely
    // cannot dent a high-DEF enemy (Normous etc.) -- that is progression, not a bug, and this
    // lets it be seen at a realistic level.
    if (const char* e = std::getenv("CLERIA_LEVEL")) {
        int v = std::atoi(e);
        if (v >= 1 && v <= 60) player.level = v;
    }
    if (ginfo.ok())
        player.max_hp = player.hp = int(std::lround(ginfo.curves[kPlayerMaxHp].at(player.level)));
    int player_iframes = 0;            // ticks of invulnerability after a hit (+0x734)

    // Award EXP for a kill and process level-ups (build/progress_re/LEVELING.md): the engine
    // credits the defeated enemy's EXP at the killing blow (FUN_004fbb40), then loops
    // level-up while total >= threshold(level+1) (= gameinfo exp_to_level, cumulative pair 10),
    // capped at 60. Each level-up recomputes maxHP from the curve and heals curHP by the
    // maxHP DELTA -- not a full refill. (Difficulty EXP bonus / rate mult are 1.0 here.)
    auto award_exp = [&](float enemy_exp) {
        if (!ginfo.ok()) return;
        player.exp += std::max(1.0f, std::round(enemy_exp));   // always at least 1
        while (player.level < 60 &&
               player.exp >= float(ginfo.exp_to_level(player.level + 1))) {
            const int old_max = player.max_hp;
            ++player.level;
            player.max_hp = int(std::lround(ginfo.curves[kPlayerMaxHp].at(player.level)));
            player.hp += (player.max_hp - old_max);
            hud.message("Level up! Lv " + std::to_string(player.level));
        }
    };
    Room room = load_room(g, asset_root, first_sob, game.g_flags, character);
    Camera cam;
    bool fly = false;
    Vec3 feet{}; float vy = 0; bool grounded = false; float speed_mult = 1.0f;

    // Play mode. The camera comes from the room's SCM regions (see update_camera), and
    // the player's SHAPE and VERTICAL MOTION now come from the game too — CASTINFO
    // record 13 (CAST_ADOL, the player's own cast) plus the engine's integrator. See
    // cleria/physics_player.hpp; the model is not `v += g*dt` and never was.
    CastMotion pm;
    {
        // CAST_ADOL is record 13 — the same 13 the damage path tests for. It is the
        // only record in all 186 with bespoke movement tuning.
        const CastTable ct = CastTable::load(asset_root / "MISC" / "CASTINFO.DAT");
        pm = cast_motion(ct, 13);
    }
    // ** kCharH is NOT pm.height(), and that is a deliberate refusal. **
    //
    // The disassembly says the engine derives a player height of `+0x38 + 0.075` =
    // 1.075 and a radius of `+0x34 * 0.9` = 0.675. Both were verified against the file.
    // But using 1.075 as the character's WORLD height contradicts two independent
    // measurements from the game's own data:
    //
    //   * S_1000's authored SCM cameras sit 18..32 units back. At 1.075 tall the
    //     camera is 17..30x the character's height away — no 2.5D action game frames
    //     itself like that. At 2.0 it is 9..16x, which is what Ys Origin looks like.
    //   * The engine's own jump model peaks at 2.46 units. That is 1.2x a 2.0-tall
    //     character (right) but 2.3x a 1.075-tall one (far too high). Note our
    //     hand-tuned kJumpH was 2.3 — it agrees with the engine's apex to 7%, which is
    //     what says the vertical model below IS in these units.
    //
    // So one of three things is true and we have not established which: the derivation
    // is wrong; +0x38 is a scale rather than a length; or the collision capsule is
    // simply not the sprite's size and this code conflates them. Until that is settled,
    // the sprite keeps the number that matches the game's framing, marked INVENTED —
    // shipping a "validated" 1.075 that visibly renders a doll would be trading a
    // working guess for a sourced mistake.
    const float kCharH = 2.0f;               // INVENTED — see above
    const float kCharRadius = 0.45f;         // INVENTED — pairs with kCharH, not pm.radius()
    const float kJumpV0 = pm.jump_v0;        // 0.285 — CASTINFO +0x50 [C-DIS]
    const float kStepUp = 0.6f;              // INVENTED — no source found
    // INVENTED: walk speed. The engine does `actor+0x30c * scale * X` and X resolves
    // to +0xdc on an object we have not identified, so the real value is unknown. It
    // is deliberately NOT back-derived from the 20/13 footstep cadence: that would
    // look right, match the audio, and be unfalsifiable.
    const float kWalkSpeed = 10.0f;   // units/s
    // INVENTED: the game paces footsteps from ASO animation events, which we do not
    // run yet. One step per this much ground travel is a stand-in for that; the
    // surface lookup and variant cycling it feeds are the game's own.
    // Footstep cadence is the game's: a countdown in ticks reloaded from the surface
    // record (walk 20, run 13 — see surface.hpp). No invented distance any more.
    int step_timer = 0;               // actor+0x4bf
    unsigned step_n = 0;              // actor+0x4c0, the step counter (variant = n & mask)
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
    (void)kTagWalk; (void)kTagRun; (void)kTagIdle; (void)kTagJump;

    // The character is driven by the game's OWN animation script (ASO), not by a
    // state machine of ours. The players' scripts are C_010/C_020/C_030 — the only
    // three ASOs with no same-named AIA, because they *choose* their sprite:
    // anime_File(id) indexes the exe's AIA name table, and Flag_IsEqualInt(161,1)
    // picks the cutscene set (c_102) over the gameplay set (c_111).
    //
    // Section ids double as the action: 0 walk, 1 run/dash, 2 idle (the VM's initial
    // state), 3 jump. The script supplies (file, tag, frame); `dir` is NOT in it —
    // that comes from the actor's facing, which is what closes the AIA formula.
    auto char_aso = [](int c) -> const char* {
        return c == 2 ? "C_020.ASO" : c == 3 ? "C_030.ASO" : "C_010.ASO";
    };
    Aso aso = Aso::load(asset_root / "ANI" / char_aso(character));
    AsoVm avm;
    avm.bind(&aso);
    avm.set_flags(game.g_flags, 0x200);

    CharSprite chr;
    std::string cur_aia;
    // Load whatever sprite the script asks for. Falls back to our old hardcoded
    // choice only if the script gives nothing (e.g. a missing ASO).
    auto sync_aia = [&]() -> bool {  // true if it swapped the AIA this call
        const char* want = aso_aia_name(avm.file_id());
        const std::string stem = (want && *want) ? std::string(want)
                                                 : std::string(char_aia(character)).substr(0, 5);
        if (stem == cur_aia) return false;
        if (chr.load(g, asset_root / "ANI" / (stem + ".AIA"), 0)) {
            cur_aia = stem;
            std::printf("  [aso] anime_File(%d) -> %s.AIA\n", avm.file_id(), stem.c_str());
            return true;
        }
        return false;
    };
    avm.tick();          // run to the first Pattern so file/tag/frame are populated
    sync_aia();
    int facing = 2;              // dir2 = back to camera (facing north, into the room)
    // Edge-trigger state: the SEN trigger index the player is currently inside (-1 = none).
    // A warp fires only on ENTERING a volume (the engine arms on entry), so arriving at a
    // door-mouth coordinate -- which sits inside the return door's own volume -- does not
    // instantly warp back. inside_trigger is primed to that door at spawn.
    int inside_trigger = -1;
    std::uint32_t curAnim = std::uint32_t(std::max(0, chr.find_tag(std::uint32_t(avm.anim_tag()))));
    int animFrame = 0, shownFrame = -1;
    chr.set_anim(g, curAnim, 0);

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
        const float fl = floor_at(phys_mesh(room), c.x, c.z, room.mesh.bmax.y + room_diag(room));
        feet = {c.x, (fl > kNoFloor ? fl : c.y), c.z};
        vy = 0; grounded = false;
        std::printf("spawn: %s(%.1f,%.1f,%.1f) floor=%s\n",
                    sp ? "CAST_ADOL" : "room centre", c.x, c.y, c.z,
                    fl > kNoFloor ? std::to_string(fl).c_str() : "none");
        if (fl <= kNoFloor && !sp) fly = true;  // no floor and no spawn -> free-fly
    };
    spawn_player();

    // Persistent per-kind enemy scripts + their attack section. The AsoVm in each
    // EnemyRender binds to a script here, so these must outlive the render list (which
    // is rebuilt on every spawn). Loaded and probed once per stem.
    std::unordered_map<std::string, Aso> enemy_scripts;
    std::unordered_map<std::string, int> enemy_atk_section;
    auto enemy_script = [&](const std::string& stem) -> Aso* {
        if (stem.empty()) return nullptr;
        auto it = enemy_scripts.find(stem);
        if (it == enemy_scripts.end()) {
            Aso a = Aso::load(asset_root / "ANI" / (stem + ".ASO"));
            it = enemy_scripts.emplace(stem, std::move(a)).first;
            // The attack section: the highest-priority one that arms a hit-volume. Basic
            // enemies have exactly one (M_100 -> section 7, a wind-up then a strike).
            int sec = -1, best_type = -1;
            if (it->second.ok())
                for (const Aso::Section& s : it->second.sections()) {
                    AsoVm probe;
                    probe.bind(&it->second);
                    if (!probe.set_section(s.id, /*force*/true)) continue;
                    bool arms = false;
                    for (int t = 0; t < 150 && !arms; ++t) {
                        probe.tick();
                        if (probe.armed_hitbox()) arms = true;
                    }
                    if (arms && s.type > best_type) { best_type = s.type; sec = s.id; }
                }
            enemy_atk_section[stem] = sec;
        }
        return it->second.ok() ? &it->second : nullptr;
    };
    // Bind each freshly-built enemy to its script + attack section, and start it in idle.
    auto bind_enemy_aso = [&] {
        for (EnemyRender& e : render) {
            Aso* a = enemy_script(e.stem);
            if (!a) continue;
            e.avm.bind(a);
            // Resting section: 2 (idle for most), else 0, else whatever exists first —
            // set_section is what makes AsoVm::ok() true, so it must land on a real one.
            int idle = a->find(2) ? 2 : (a->find(0) ? 0 : a->sections().front().id);
            e.idle_section = idle;
            e.avm.set_section(idle, /*force*/true);
            e.atk_section = enemy_atk_section[e.stem];
        }
    };

    // Barrier fight (kekkai). A room's KEKKAIZAKO_*.XSO polls Chr_IsSeedLiving(1) and,
    // when the last group-1 seed enemy dies, grants the progress flag its chest guard
    // tests (EVENTVM.md §6: S_1001 -> flag 301, which S_BOX01 requires before it opens).
    std::vector<Xso> barrier_scripts;        // this room's KEKKAIZAKO_*.XSO (the poll + grant)
    std::vector<Xso> barrier_break_scripts;  // KEKKAIBREAK_*.XSO (Eft_Kill the ward + camera)
    bool barrier_seeded = false;        // a group-1 seed spawned this visit
    bool barrier_cleared = false;       // its flag has already been granted this visit
    bool barrier_break_pending = false; // grant done; the main loop must play KEKKAIBREAK
    bool room_events_dirty = true;      // (re)run the room's event scripts on the manager

    auto spawn_enemies = [&] {
        // Populate the world the engine's way: run S_xxxx.XSO (op 0xa0 -> SCT bulk,
        // op 0x12c -> scripted), seeded with the persistent game flags. Then load a
        // sprite for every spawned cast (incl. 0x12c hub NPCs the SCT never named) and
        // pair them. Stats come from ONE path (ActorWorld::spawn): SCT name / cast_type
        // -> CASTINFO -> enemy_index -> ENEMYINFO -> GAMEINFO. Real names = CASTINFO+0x10.
        populate_room(world, room, game.g_flags);
        efx.kinds.clear();
        efx.init_from_world(g, asset_root, world, casts);
        render = build_enemy_render(world, casts, efx);
        bind_enemy_aso();
        // Detect this room's barrier fight: load its KEKKAIZAKO_*.XSO poll scripts and
        // note whether S_xxxx.XSO's 0x12c actually spawned a group-1 seed (it is gated
        // behind the very flag the barrier grants, so a re-visit spawns nothing and we
        // must not re-fire). The poll itself runs in clear_barrier, below.
        barrier_scripts.clear();
        barrier_break_scripts.clear();
        barrier_cleared = false;
        barrier_seeded = world.any_seed_alive(1);
        {
            std::error_code bec;
            for (const auto& de : fs::directory_iterator(room.dir, bec)) {
                std::string fn = de.path().filename().string();
                for (char& c : fn) c = char(std::toupper((unsigned char)c));
                const bool xso = fn.size() >= 4 && fn.compare(fn.size() - 4, 4, ".XSO") == 0;
                try {
                    if (xso && fn.rfind("KEKKAIZAKO", 0) == 0)
                        barrier_scripts.push_back(Xso::parse(read_bytes(de.path())));
                    else if (xso && fn.rfind("KEKKAIBREAK", 0) == 0)
                        barrier_break_scripts.push_back(Xso::parse(read_bytes(de.path())));
                } catch (const std::exception&) {}
            }
        }
        if (!render.empty()) {
            int statted = 0;
            for (const EnemyRender& e : render) statted += (e.actor->max_hp > 0);
            std::printf("  %zu enemies spawned (%d with game stats)\n", render.size(),
                        statted);
            if (std::getenv("CLERIA_ENEMY"))
                for (const EnemyRender& e : render)
                    std::printf("    %-14s (%-6s) lv%-3d HP %6.0f  ATK %5.0f  DEF %5.0f\n",
                                e.display.c_str(), e.stem.c_str(), e.actor->level,
                                e.actor->max_hp, e.actor->atk, e.actor->def);
        }
    };

    spawn_enemies();

    apply_room_bgm(room.name);

    float cooldown = 1.5f;
    auto transition = [&](const std::string& dest, const SenTrigger* via = nullptr) {
        auto it = g.sob_index.find(dest);
        if (it == g.sob_index.end()) { std::printf("  (no SOB for %s)\n", dest.c_str()); return; }
        std::printf(">> entering %s\n", dest.c_str());
        // ** Copy everything we need off `via` NOW. ** It points into the CURRENT room's
        // sen.triggers, which `room = std::move(next)` below frees -- reading via after
        // that is a use-after-free (it silently read as "no arrival", dropping the player
        // at the SCT default = the wrong entrance on every return). Copy by value first.
        const bool has_arr = via && via->has_arrival();
        const Vec3 arr = via ? via->arrival_pos : Vec3{};
        const bool has_fac = via && via->has_facing();
        const int fac_deg = via ? via->arrival_facing_deg : 0;
        Room next = load_room(g, asset_root, it->second, game.g_flags, character);
        free_room(room, g.white_bind);
        room = std::move(next);
        room_events_dirty = true;   // the new room's event scripts start next frame
        apply_room_bgm(room.name);
        if (fly) {
            spawn_in(cam, room);
        } else {
            spawn_player();
            // The exit's SEN arrival (+0x22c) overrides the SCT default (FUN_00434020)
            // whenever the exit's label is set -- verbatim, with NO zero-test: a (0,0,0)
            // arrival is a LITERAL door-mouth coordinate, not "unset" (build/warp_re/
            // DOORRETURN.md: S_1004->S_1005's (0,0,0) is exactly S_1005's door mouth). The
            // per-door correctness comes from applying the CROSSED trigger's own arrival,
            // not a by-destination lookup -- rooms joined by 2-3 doors each carry a
            // different arrival, and picking the wrong one lands you at the wrong door.
            if (has_arr) {
                const float fl = floor_at(phys_mesh(room), arr.x, arr.z,
                                          room.mesh.bmax.y + room_diag(room));
                feet = {arr.x, (fl > kNoFloor ? fl : arr.y), arr.z};
                vy = 0; grounded = false;
                std::printf("  arrival @ (%.1f,%.1f,%.1f) [SEN]\n", feet.x, feet.y, feet.z);
            }
            // Place the camera for the new room NOW: the real warp is hidden by a fade so
            // it cuts rather than swooping, and it gives the correct screen basis for the
            // arrival facing below.
            update_camera(room, feet, 0.0f, /*snap=*/true);
            if (g_cam.on) apply_cam_state(cam, feet);
            // Arrival facing (SEN +0x238, degrees) -> world heading -> screen sprite dir.
            // The world forward is (sin deg, cos deg), i.e. our yaw = deg directly, NO flip.
            // Provenance: build/warp_re/FACING{,_APPLY}.md trace the field to actor+0x314
            // via FUN_00443a80 = deg2rad(90-deg), consumed screen-relatively. That 90 is
            // the engine's own heading basis (0 -> +X); OUR Actor.yaw is 0 -> +Z (vx=sin,
            // vz=cos, world.cpp), a 90 offset, so the flip cancels and the degree maps to
            // our yaw as-is. Confirmed against geometry, not just the decode: across 219
            // shipped door pairs this forward points INTO the destination room (mean
            // alignment 0.81), where the un-cancelled (cos deg, sin deg) scores -0.03 and
            // aims at the wall you arrived through. heading_to_facing then maps it exactly
            // as walking that way would, so the warped-in player faces the right way.
            if (has_fac) {
                const float yaw = float(fac_deg) * 3.14159265358979323846f / 180.0f;
                face_dir = {std::sin(yaw), 0.0f, std::cos(yaw)};
                facing = heading_to_facing(face_dir.x, face_dir.z, cam.yaw);
                std::printf("  arrival facing %d deg -> dir %d\n", fac_deg, facing);
            }
        }
        // Prime the edge-trigger: mark whatever door volume the player spawned inside, so
        // a door-return that lands them in the doorway doesn't immediately warp back.
        inside_trigger = -1;
        if (!fly) {
            const Vec3 pr = {feet.x, feet.y + kCharH * 0.5f, feet.z};
            for (std::size_t i = 0; i < room.sen.triggers.size(); ++i)
                if (room.sen.triggers[i].contains(pr)) { inside_trigger = int(i); break; }
        }
        spawn_enemies();
        cooldown = 1.5f;
        g_tick_reset = true;  // don't catch up on the load stall (the exe's DAT_0075aed5)
    };
    // Reload the current room (re-gates chests for the new character / state).
    auto reload_room = [&] {
        auto it = g.sob_index.find(room.name);
        if (it == g.sob_index.end()) return;
        Room next = load_room(g, asset_root, it->second, game.g_flags, character);
        free_room(room, g.white_bind);
        room = std::move(next);
        room_events_dirty = true;   // re-run the room's event scripts for the re-gated room
    };

    // The barrier fight, now wired. A KEKKAIZAKO_*.XSO polls its group-1 seeds:
    //
    //     0: 0x131(1)       <- acc = any group-1 seed still alive?   (now implemented)
    //     3: jz -> 8        <- all dead -> grant
    //     5: 0xf0 / jmp 0   <- else wait a frame and loop
    //     8: 0x64(301, 1)   <- set the progress flag (S_1001 -> 301)
    //
    // 0x131 is Chr_IsSeedLiving (FUN_00570640, EVENTVM.md §6): ActorWorld answers it
    // from the live registry. We poll each frame; the instant the last seed dies we run
    // the script to completion on the PERSISTENT VM (with the seed gone, 0x131 reads
    // false, so it falls through to its grant) and re-gate the room, so the chest guard
    // S_BOX01 — which requires flag 301 before it will open — now unlocks. This is a
    // real fight now, not the old "running it grants the flag for free": no seed spawns
    // (the 0x12c is gated behind 301) or none die => no grant.
    auto clear_barrier = [&] {
        if (barrier_scripts.empty() || barrier_cleared || !barrier_seeded) return;
        if (world.any_seed_alive(1)) return;   // seeds still alive: keep fighting
        XsoHost* saved = game.host;
        game.host = &world;
        for (const Xso& ks : barrier_scripts) { game.reset_run(); game.run(ks); }
        game.host = saved;
        barrier_cleared = true;
        // The break sequence (KEKKAIBREAK: Fix -> Scroll_MoveTo the ward -> Eft_Kill it ->
        // Scroll_Return -> Unfix) is a real camera CUTSCENE. Play it through the script
        // manager (the main loop starts it when no other cutscene is up) so the pan is
        // visible and the ward vanishes mid-pan, then re-gate the chest when it ends. With
        // no break script, re-gate immediately (old behaviour).
        if (barrier_break_scripts.empty()) { reload_room(); hud.message("The barrier fades."); }
        else barrier_break_pending = true;
        std::printf("  [barrier] all seeds down -> ran %zu KEKKAIZAKO script(s); flag 301 = %d; "
                    "break %s\n", barrier_scripts.size(), game.g_flags[301],
                    barrier_break_scripts.empty() ? "none" : "queued (cutscene)");
    };

    // ## In-engine cutscene runner (CUTSCENE_PLAN.md B3/B9) — MVP: one debug-triggered
    // script driven by the resumable XSO VM (CUTSCENE_COROUTINE.md). The host receives the
    // dialogue ops and gates Wait_Prompt on "text fully revealed + advance pressed"; the
    // fixed-step loop ticks the coroutine and FREEZES the world while it runs (op 0xb6 Fix).
    struct CutsceneHost : XsoHost {
        std::string name, text;
        int speaker = 0;
        bool box_active = false;
        int reveal = 0;           // typewriter char count (advanced by the loop)
        bool advance = false;     // set by input; consumed to advance one line
        ScriptManager* mgr = nullptr;   // for Wait_Script (is the named child still live?)
        void window_msg(int sp, int, const std::string& nm, const std::string& tx) override {
            speaker = sp; name = nm; text = tx; reveal = 0; box_active = true;
        }
        void window_close() override { box_active = false; name.clear(); text.clear(); }
        std::string caption_name; int caption_timer = 0;   // 0xe2 area-name card (frames left)
        void caption(const std::string& nm) override { caption_name = nm; caption_timer = 210; }
        bool wait_prompt() override {
            if (reveal < int(text.size())) return false;    // line still typing out
            if (advance) { advance = false; return true; }  // one press advances one line
            return false;
        }
        // 0xf4 Wait_Script(name): a parent controller blocks until its spawned talk child
        // is gone. Answer from the live set (case-insensitive; script names vary in case).
        bool wait_script_done(const std::string& n) override {
            if (!mgr) return true;
            for (const auto& i : mgr->live) {
                if (i.finished || i.name.size() != n.size()) continue;
                bool same = true;
                for (std::size_t k = 0; k < n.size(); ++k)
                    if (std::tolower((unsigned char)i.name[k]) != std::tolower((unsigned char)n[k])) { same = false; break; }
                if (same) return false;   // still running -> keep waiting
            }
            return true;
        }
        void scroll_moveto(float x, float y, float z, int frames) override { ::scroll_moveto(x, y, z, frames); }
        void scroll_return(int frames) override { ::scroll_return(frames); }
        void shake(float ax, float ay, float az, int frames) override { ::shake(ax, ay, az, frames); }
        // Effect ops forward to the world so a cutscene's Eft_Kill / Eft_LocatePosAddStr
        // really adds/removes the ward from world.effects() (the barrier break-pan).
        XsoHost* worldHost = nullptr;
        void kill_effect(const std::string& n) override { if (worldHost) worldHost->kill_effect(n); }
        void locate_effect(int id, float x, float y, float z, float a, float b,
                           const std::string& n) override {
            if (worldHost) worldHost->locate_effect(id, x, y, z, a, b, n);
        }
        void spawn(int, int, float, float, float, float, int) override {}
        bool is_alive(int) const override { return false; }
        bool any_seed_alive(int) const override { return false; }
        // op 0x13d: is the player inside the named .SEV box? Only the player (13) is
        // positioned here; other ids resolve to no actor -> false (the engine's null path).
        const Room* roomPtr = nullptr;
        const Vec3* playerPos = nullptr;
        bool chr_in_event_box(int entityId, const std::string& name) const override {
            if (entityId != 13 || !roomPtr || !playerPos) return false;
            const SceneBox* b = find_box(roomPtr->event_boxes, name);
            return b && b->contains(*playerPos);
        }
    } csHost;
    // The cutscene runtime is the concurrent GoScript manager (CUTSCENE_COROUTINE.md §2/§5):
    // a triggered script can StartScript sub-talks / captions that tick alongside it. Its
    // loader resolves child names from the CURRENT room's script dir (&room.dir stays valid
    // across transitions — `room` is reassigned in place). Flags are copied in at start and
    // back out at end, so a cutscene reads/writes the live game state.
    struct ViewLoader : ScriptLoader {
        const fs::path* dir = nullptr;
        std::shared_ptr<const Xso> load_script(const std::string& name) override {
            std::string up = name;
            for (char& c : up) c = char(std::toupper((unsigned char)c));
            const fs::path p = *dir / (up + ".XSO");
            std::error_code ec;
            if (!fs::exists(p, ec)) return nullptr;    // engine would throw; we skip
            try { return std::make_shared<Xso>(Xso::parse(read_bytes(p))); }
            catch (const std::exception&) { return nullptr; }
        }
    } csLoader;
    csLoader.dir = &room.dir;
    ScriptManager csMgr;
    csMgr.vm.host = &csHost;
    csMgr.loader = &csLoader;
    csHost.mgr = &csMgr;
    csHost.worldHost = &world;
    csHost.roomPtr = &room;         // op 0x13d tests the player vs the current room's .SEV boxes
    csHost.playerPos = &feet;
    bool prev_frozen = false;
    std::function<void()> after_cutscene;   // ran once when the world thaws (e.g. re-gate the room)
    SpriteTex captionTex; std::string captionLoaded;   // cached area-name card (op 0xe2)

    // (Re)start the room's event scripts: run the room root S_xxxx.XSO on the PERSISTENT
    // manager. The root StartScripts the region-talk controllers (Stop*_Roi_*), the save
    // point, ambient effects, etc., which then poll each frame (op 0x13d) and fire talks when
    // the player enters a .SEV box. Seeded from the live game flags; csMgr owns its flag copy
    // for the room's lifetime. Spawn/placement ops no-op on csHost (the world is populated
    // separately by spawn_enemies), so this only drives flags + child scripts + camera/box.
    // OPT-IN (CLERIA_EVENTS): running the full room root live needs a correct STORY-FLAG state,
    // which CleriaCore doesn't model yet. With fresh/zero flags the root misroutes — e.g. S_1000
    // fires BattleStart1, which Fixes and locks the hub (cleria-cotest test 11 pins this). So it
    // is off by default (the debug-T talk + barrier break-pan still work, they're started
    // explicitly); set CLERIA_EVENTS=1 to run live region-talk triggers experimentally.
    const bool events_enabled = std::getenv("CLERIA_EVENTS") != nullptr;
    auto start_room_events = [&] {
        csMgr.live.clear(); csMgr.spawn_queue.clear(); csMgr.peak_live = 0; csMgr.fix_count = 0;
        csHost.box_active = false; csHost.reveal = 0; csHost.advance = false;
        after_cutscene = nullptr; prev_frozen = false;
        if (!events_enabled) return;                 // safe default: no live room root
        std::copy(game.g_flags, game.g_flags + 0x200, csMgr.vm.g_flags);
        csMgr.vm.g_flags[150] = character;
        const fs::path xso = room.dir / (room.name + ".XSO");
        if (!fs::exists(xso)) return;
        try { csMgr.start_root(std::make_shared<Xso>(Xso::parse(read_bytes(xso))), room.name); }
        catch (const std::exception&) {}
        std::printf("  [events] room %s -> %zu live script(s)%s\n", room.name.c_str(),
                    csMgr.live.size(), csMgr.world_frozen() ? " (frozen)" : "");
    };

    // Start an EXTRA cutscene script (debug T, barrier break) alongside the room's live
    // scripts; its Fix (0xb6) freezes the world and it reaps itself on end. Does NOT clear the
    // persistent controllers. `debug235` sets the S_1000 test-route flag for the debug T key.
    auto start_cutscene_xso = [&](std::shared_ptr<Xso> s, const std::string& name, bool debug235) {
        if (debug235) csMgr.vm.g_flags[235] = 1;
        csMgr.start_root(std::move(s), name);
        std::printf("  [cutscene] started %s (live=%zu, frozen=%d)\n", name.c_str(),
                    csMgr.live.size(), int(csMgr.world_frozen()));
    };
    auto start_cutscene = [&](const fs::path& p, bool debug235) {
        std::shared_ptr<Xso> s;
        try { s = std::make_shared<Xso>(Xso::parse(read_bytes(p))); }
        catch (const std::exception&) { hud.message("cutscene: load failed"); return; }
        start_cutscene_xso(std::move(s), p.stem().string(), debug235);
    };

    // One fixed tick of the room's event scripts. Returns whether the world is FROZEN (a Fix
    // is held) — the caller skips the player/enemy sim for that tick but the camera still runs.
    auto tick_events = [&] {
        csMgr.tick_all();
        if (csHost.box_active && csHost.reveal < int(csHost.text.size())) csHost.reveal += 2;
        scroll_advance();
        shake_advance();
        const bool frozen = csMgr.world_frozen();
        if (!frozen && prev_frozen && after_cutscene) {    // world just thawed -> queued action
            auto f = after_cutscene; after_cutscene = nullptr; f();
        }
        prev_frozen = frozen;
        if (frozen && g_cam.on) apply_cam_state(cam, feet);
        return frozen;
    };

    // Melee swing. Damage and reach are OUR numbers: the game has no per-enemy HP
    // column (stats come from a level curve we have not wired), and the player's
    // attack animation tags in c_111 are not identified yet.
    // Start a swing. The hit-volume is armed for `atk_window` ticks and processed in the
    // fixed-step loop (see the swing block there). This is the engine's shape now, from
    // build/attack_re/ATTACK.md, not the old instant reach-2.6 circle: a SPHERE of radius
    // 0.6 sitting 0.6 units AHEAD of the player in the facing direction (arg[2] is both
    // the radius and the forward reach; the "+0x10c angle" is a placement offset, 0 for a
    // straight swing, NOT a sector arc), live for a 30-tick window, dealing `power` 8.0 as
    // the damage bonus, and hitting each enemy ONCE per swing (the engine's hit-dedup).
    // The numbers are Yunica's basic swing, verified in cleria-asotest against C_010.
    // Start or chain a swing. The ASO owns everything about it — its length, the
    // hit-volume (GenerateCollision arms it), and when you may act again — which is what
    // makes the three characters feel different instead of sharing one hardcoded window.
    // Measured from the scripts: Yunica's combo #1 roots her (Movable 0) and opens its
    // cancel window at tick 21, Hugo's at tick 9 while he keeps full movement, Toal's at
    // tick 10 with an 8-frame box and a slow drift. See build/attack_re/ATTACK.md.
    // A character's real combo sections: the ids in [0x100,0x110) that actually arm a
    // hitbox or fire a bullet, ascending. The dud sections in between (Hugo's 257/258/…
    // do neither) must be SKIPPED — chaining to the literal next id lands on one and
    // stalls him after a single shot. Found by running each section briefly, cached per
    // character; the combo just walks this list, wrapping.
    auto combo_sections = [&](int chr) -> const std::vector<int>& {
        static std::unordered_map<int, std::vector<int>> cache;
        auto it = cache.find(chr);
        if (it != cache.end()) return it->second;
        std::vector<int>& out = cache[chr];
        for (const Aso::Section& s : aso.sections()) {
            if (s.id < 0x100 || s.id >= 0x110) continue;
            AsoVm probe;
            probe.bind(&aso);
            if (!probe.set_section(s.id, /*force*/true)) continue;
            bool real = false;
            for (int t = 0; t < 150 && !real; ++t) {
                probe.tick();
                if (probe.armed_hitbox() || !probe.bullets().empty()) real = true;
            }
            if (real) out.push_back(s.id);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    auto try_attack = [&] {
        const bool swinging = avm.section() >= 0x100;
        if (swinging && !avm.cancelable()) return;   // committed to the current swing
        const std::vector<int>& combo = combo_sections(character);
        if (combo.empty()) return;
        std::int32_t next = combo.front();
        if (swinging) {
            // Advance to the next real section after the current one, wrapping. For Hugo
            // that is 0x100->0x103->0x106->…, a bullet on each; for Yunica/Toal the melee
            // combo. This is NOT the game's authored combo order (which lives in the
            // script's own AttackReserve branches, not run here) — but each step is a
            // genuine attack, and the recovery-stall release keeps it from ever sticking.
            auto p = std::upper_bound(combo.begin(), combo.end(), int(avm.section()));
            next = (p != combo.end()) ? *p : combo.front();
        }
        if (!avm.set_section(next, /*force*/true)) return;
        atk_dir = face_dir;         // the swing points where you're facing
        atk_hit.clear();
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
    // Fixed-step logic. The game runs its logic at a FIXED 60 Hz with catch-up,
    // decoupled from rendering: it accumulates REAL elapsed milliseconds scaled by
    // a timeScale, then runs 0..N ticks per Present. On a 144 Hz display the ASO VM
    // still ticks 60x/sec. `anime_Wait(n)` is therefore n/60 s of SCALED time — so
    // the accumulator is the thing to reproduce, not a per-frame dt scale.
    float accum = 0;

    // In-game pause menu (Escape opens it, Escape/B closes; the window X still quits). Draws
    // the pause-book Equip page over a frozen game. The menu renderer + font + item table are
    // loaded here since the walk loop doesn't share the title-screen instances.
    TitleFx pauseUi;
    const bool pauseOk = pauseUi.init(g, asset_root);
    MenuFont pmf;
    pmf.init(g, fmt, find_felghana(asset_root), int(std::lround(32.0 * 960.0 / 768.0)));
    const ItemTable pitems = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
    bool paused = false;
    int pausePage = 0;  // pause-book tab: 0 Equip, 1 Item, 2 Topic, 3 Save/Load (Q/E cycle)
    std::fprintf(stderr, "[pause menu] Esc opens the pause book; assets %s\n",
                 pauseOk ? "loaded (ready)" : "MISSING TITLE_000.DDS -> book won't draw");

    while (running) {
        const Uint64 now = SDL_GetTicks();
        const float real_dt = float(now - last) / 1000.0f; last = now;
        accum += real_dt * g_time_scale;
        // The exe's own hitch protection (DAT_0075aed5): after a load it forces a
        // single tick with a zero delta rather than catching up on the stall. Room
        // transitions set this; without it a slow load bursts hundreds of ticks.
        if (g_tick_reset) { accum = kTickDt; g_tick_reset = false; }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            else if (e.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode k = e.key.key;
                // While the world is frozen by a cutscene, a confirm key advances the dialogue
                // and other gameplay input is swallowed (the sim is skipped anyway).
                if (csMgr.world_frozen()) {
                    if (k == SDLK_SPACE || k == SDLK_RETURN || k == SDLK_E || k == SDLK_Z)
                        csHost.advance = true;
                }
                // T = trigger a test cutscene (S_1000's Roy/Ramona talk) — debug.
                else if (k == SDLK_T) start_cutscene(asset_root / "MAP" / "S_10" / "S_1000" / "TALKC210.XSO", /*debug235*/true);
                else if (k == SDLK_ESCAPE) paused = !paused;  // toggle pause; NEVER quit (X quits)
                else if (k == SDLK_F) {
                    fly = !fly;
                    if (!fly) {  // dropping into walk: keep XZ, snap to floor
                        feet = {cam.pos.x, cam.pos.y, cam.pos.z};
                        const float fl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + room_diag(room));
                        if (fl > kNoFloor) feet.y = fl;
                        vy = 0;
                    }
                    std::printf("mode: %s\n", fly ? "fly" : "walk");
                }
                else if (k == SDLK_R) { if (fly) spawn_in(cam, room); else spawn_player(); }
                // Debug HP for testing the red damage-lag trail (feat: HUD_HP_BARS.md H3):
                // K = take 60 damage (watch the red ghost linger + drain), L = heal to full.
                else if (k == SDLK_K) { player.hp = std::max(1, player.hp - 60); hud.message("(debug) -60 HP"); }
                else if (k == SDLK_L) { player.hp = player.max_hp; hud.message("(debug) full heal"); }
                // Pause-book tab cycling (only while the menu is open): E = next, Q = prev page.
                else if (paused && k == SDLK_E) pausePage = (pausePage + 1) % 4;
                else if (paused && k == SDLK_Q) pausePage = (pausePage + 3) % 4;
                // Debug: B fills the EX/Boost gauge in ~4 presses (to eyeball the boost arc).
                else if (k == SDLK_B) { player.boost = std::min(30000.f, player.boost + 7500.f); hud.message("(debug) +Boost"); }
                else if (k == SDLK_TAB) {
                    character = character % 3 + 1;
                    game.g_flags[150] = character;
                    hud.message(std::string("Character: ") + kCharNames[character]);
                    // Swap the SCRIPT, not the sprite: the script picks the sprite.
                    aso = Aso::load(asset_root / "ANI" / char_aso(character));
                    avm = AsoVm{};
                    avm.bind(&aso);
                    avm.set_flags(game.g_flags, 0x200);
                    avm.tick();
                    sync_aia();
                    curAnim = std::uint32_t(std::max(0, chr.find_tag(std::uint32_t(avm.anim_tag()))));
                    animFrame = 0; shownFrame = -1;
                    reload_room();  // re-gate this room's chests for the new character
                }
                else if (k == SDLK_E && !fly) try_interact();
                else if (k >= SDLK_1 && k <= SDLK_9) {
                    std::size_t idx = std::size_t(k - SDLK_1);
                    if (idx < room.sen.triggers.size())
                        transition(room.sen.triggers[idx].dest_scene, &room.sen.triggers[idx]);
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
        // A newly (re)built room (re)starts its event scripts on the persistent manager.
        if (room_events_dirty) { room_events_dirty = false; start_room_events(); }
        // The barrier just broke: play KEKKAIBREAK as a camera cutscene (once the world is not
        // already frozen). Its host forwards Eft_Kill to the world so the ward vanishes mid-pan;
        // when it ends (thaw), after_cutscene re-gates the room so the chest unlocks.
        if (barrier_break_pending && !csMgr.world_frozen() && !barrier_break_scripts.empty()) {
            barrier_break_pending = false;
            after_cutscene = [&] { reload_room(); hud.message("The barrier fades."); };
            start_cutscene_xso(std::make_shared<Xso>(barrier_break_scripts.front()),
                               "KEKKAIBREAK", /*debug235*/false);
        }
        // ---- fixed 60 Hz logic, 0..N ticks per rendered frame --------------
        // The catch-up loop in the exe has NO cap; a long stall really does emit a
        // burst of ticks. We reproduce that, relying on the load-time reset above
        // rather than on clamping, so timing stays exact under normal play.
        if (paused) accum = 0;  // freeze the game while the pause menu is open (no unpause burst)
        int ticks = 0;
        while (accum >= kTickDt) {
        accum -= kTickDt;
        ++ticks;
        // Tick the room's event scripts every frame (controllers poll here). If a Fix is held
        // (a talk / cutscene) the world is frozen: skip the player/enemy sim for this tick (the
        // camera still runs inside tick_events).
        if (tick_events()) continue;
        const float dt = kTickDt;
        if (cooldown > 0) cooldown -= dt;

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
            const float eye = kCharH, radius = kCharRadius;
            // anime_Movable is a movement-speed SCALE, not a boolean, and it is a large
            // part of why the three feel different: Yunica's swing drops it to 0 (rooted
            // through the commit), Hugo keeps a full 1.0, Toal goes 0.80 -> 0.05 so he
            // keeps drifting through his. anime_Turnable locks facing over the same span.
            const float walk = kWalkSpeed * speed_mult * avm.movable(), stepUp = kStepUp;
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
            if (moving) {
                feet.x += wish.x / wl * walk * dt; feet.z += wish.z / wl * walk * dt;
                if (avm.turnable())
                    face_dir = {wish.x / wl, 0, wish.z / wl};  // the swing faces where you walk
            }
            { Vec3 body = {feet.x, feet.y + stepUp, feet.z};
              body = resolve_walls(phys_mesh(room), body, radius, std::max(eye - stepUp, 0.5f));
              feet.x = body.x; feet.z = body.z; }
            // The engine's vertical model, not ours. `vy` is NOT a velocity: it is the
            // signed TIME at actor+0x330, decremented ~1/60 per tick, and the Y delta
            // is closed-form. See cleria/physics_player.hpp — reproduce it literally,
            // it is non-physical but tuned.
            float dy = 0.5f * kGravity * vy * std::fabs(vy);  // v*|v| — the sign carries
            dy = std::fmax(dy, kTerminalFall);
            feet.y += dy;
            vy -= kTimerStep;
            if (vy < kTimerFloor) vy = kTimerFloor;
            int floor_tri = -1;
            const float fl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + stepUp, &floor_tri);
            if (fl > kNoFloor && dy <= 0.0f && fl >= feet.y - stepUp) {
                feet.y = fl; vy = 0; grounded = true;
            } else grounded = false;

            // Tick the enemies. The engine's enemies chase the player ACTOR; we sync it
            // to the view's player each tick, step the world (each enemy turns toward it
            // and advances at its ENEMYINFO speed -- world.hpp update_actor), then apply
            // COLLISION here, since the world is geometry-free and the view owns the mesh.
            // The engine integrates enemy motion with a wall-slide sweep inside
            // FUN_0045a510; we reuse the player's resolve_walls (push-out) against the
            // same collision mesh so enemies stop at walls/fences instead of clipping
            // through, then floor-snap. Enemy radius uses the player's for now -- a
            // per-cast collision radius (CASTINFO+0x34) is available but not yet wired.
            if (Actor* wp = world.player()) wp->pos = feet;
            // Enemy brains, BEFORE the chase mover so move_scale gates this frame's step.
            // Each enemy runs its own ASO: chase until in range, then commit to its attack
            // section (the one that arms a hit-volume), plant for the swing, and return to
            // idle when the animation stalls into recovery. This replaces the old
            // contact-damage stand-in with the real animated attack + scripted hitbox.
            constexpr float kEnemyAtkReach = 1.4f;   // wind-up trigger distance (+ radius)
            constexpr int kEnemyAtkCooldown = 40;    // ticks between an enemy's swings
            for (EnemyRender& e : render) {
                Actor* en = e.actor;
                if (!en || !en->alive() || !e.avm.ok()) continue;
                // Hitstun: a landed player hit interrupts the enemy. Drop out of the attack
                // section back to idle, clear the swing state, and hold (no chase/attack)
                // until the stun ends — so the swing visibly STOPS instead of playing on.
                if (en->hitstun > 0) {
                    --en->hitstun;
                    if (e.avm.section() == e.atk_section) {
                        e.avm.set_section(e.idle_section, /*force*/true);
                        e.atk_win = 0; e.atk_armed = false; e.atk_age = 0;
                        e.atk_cd = kEnemyAtkCooldown;   // no instant retaliation
                    }
                    en->move_scale = 0.0f;              // planted (knockback still slides it)
                    e.avm.set_is_land(true);
                    e.avm.tick();
                    continue;
                }
                if (e.atk_cd > 0) --e.atk_cd;
                const float dx = feet.x - en->pos.x, dz = feet.z - en->pos.z;
                const float dist2 = dx * dx + dz * dz;
                const bool attacking = e.atk_section >= 0 && e.avm.section() == e.atk_section;
                if (!attacking) {
                    const float reach = kCharRadius + kEnemyAtkReach;
                    if (e.atk_section >= 0 && e.atk_cd <= 0 && dist2 < reach * reach &&
                        e.avm.set_section(e.atk_section, /*force*/true)) {
                        const float dl = std::sqrt(dist2);
                        e.atk_dx = dl > 1e-4f ? dx / dl : std::sin(en->yaw);
                        e.atk_dz = dl > 1e-4f ? dz / dl : std::cos(en->yaw);
                        e.atk_hit = false;
                        e.atk_stall = 0;
                        e.atk_fkey = -1;
                        e.atk_armed = false;
                        e.atk_age = 0;
                        en->move_scale = 0.0f;
                    } else {
                        en->move_scale = 1.0f;   // chase
                    }
                } else {
                    en->move_scale = 0.0f;       // planted through the swing
                }
                e.avm.set_is_land(true);
                e.avm.tick();
                // Its GenerateCollision arms the enemy's hit-volume, with the section's own
                // radius/lifetime/power (M_100: frame 26, radius 1.0).
                if (e.avm.armed_hitbox()) {
                    e.hb = e.avm.hitbox();
                    e.atk_win = e.hb.lifetime > 0 ? e.hb.lifetime : 1;
                    e.atk_hit = false;
                    e.atk_armed = true;
                }
                // Release the swing. A wind-up can hold a single frame for many ticks
                // BEFORE the hit fires (M_100 holds frame 2 for 12 ticks, then arms at
                // tick 26), so a bare stall would end the attack before it ever hit —
                // only release once the hit-volume has fired and its window is spent and
                // the animation has stalled into recovery. The age cap is a safety net
                // for a section that somehow never arms.
                if (e.avm.section() == e.atk_section) {
                    ++e.atk_age;
                    const long fk = (long(e.avm.anim_tag()) << 20) ^ e.avm.frame();
                    e.atk_stall = (fk == e.atk_fkey) ? e.atk_stall + 1 : 0;
                    e.atk_fkey = fk;
                    const bool recovered = e.atk_armed && e.atk_win <= 0 && e.atk_stall >= 8;
                    if (recovered || e.atk_age >= 180) {
                        e.avm.set_section(e.idle_section, /*force*/true);
                        e.atk_cd = kEnemyAtkCooldown;
                        en->move_scale = 1.0f;
                    }
                }
            }

            world.step();
            for (Actor& a : world.actors()) {
                if (a.is_player() || !a.alive()) continue;
                if (a.hit_flash > 0) --a.hit_flash;
                // Knockback: add the decaying impulse, then let wall-resolve stop it at
                // walls like any other motion.
                a.pos.x += a.knockback.x; a.pos.z += a.knockback.z;
                a.knockback.x *= 0.75f; a.knockback.z *= 0.75f;
                Vec3 body = {a.pos.x, a.pos.y + stepUp, a.pos.z};
                body = resolve_walls(phys_mesh(room), body, radius, std::max(eye - stepUp, 0.5f));
                a.pos.x = body.x; a.pos.z = body.z;
                const float efl = floor_at(phys_mesh(room), a.pos.x, a.pos.z, a.pos.y + kCharH);
                if (efl > kNoFloor) a.pos.y = efl;
            }

            // The player's melee swing (build/attack_re). While the hit-volume is live,
            // hit every enemy inside the sphere once. The box sphere sits 0.6 ahead in
            // atk_dir with radius 0.6; an enemy is hit if its centre is within
            // (0.6 + enemy radius). Damage uses the game's formula with the ASO's power
            // as the +0xfc bonus (8.0), and marks each enemy hit so a 30-tick window
            // deals one hit per swing, not 30.
            if (atk_window > 0) {
                --atk_window;
                // Radius and power now come from the script's own GenerateCollision
                // rather than Yunica's values hardcoded for everyone.
                const Vec3 c = {feet.x + atk_dir.x * atk_hb.radius, feet.y + kCharH * 0.5f,
                                feet.z + atk_dir.z * atk_hb.radius};
                const float rr = atk_hb.radius + kCharRadius;  // box radius + enemy radius
                int killed = 0;
                for (Actor& en : world.actors()) {
                    if (en.is_player() || !en.alive() || atk_hit.count(en.instance_id)) continue;
                    const float dx = en.pos.x - c.x, dz = en.pos.z - c.z;
                    const float ddy = (en.pos.y + kCharH * 0.5f) - c.y;
                    if (dx * dx + ddy * ddy + dz * dz > rr * rr) continue;
                    atk_hit.insert(en.instance_id);   // the engine's per-attack hit-dedup
                    AttackStats a;
                    a.atk = ginfo.player_atk(character, /*tier*/ 0, player.level);
                    a.bonus = atk_hb.power;          // per-attack power, from this section's script
                    const int dmg = damage_int(a, en.def,
                        [&](float pct) { return float(rng_pct(rng)) < pct; });
                    const HitResult r = apply_damage(en.hp, en.flags, dmg);
                    // Boost builds on hits DEALT too (BOOST.md hook). Amount [H] (see hurt_player).
                    player.boost = std::min(30000.f, player.boost + 1500.f);
                    // Knockback away from the player + a white hit-flash (the engine's
                    // +0x338/+0x340 impulse and +0x4e8 tint). Decayed in the enemy tick.
                    const float kdx = en.pos.x - feet.x, kdz = en.pos.z - feet.z;
                    const float kd = std::sqrt(kdx * kdx + kdz * kdz);
                    if (kd > 1e-3f) en.knockback = {kdx / kd * 0.30f, 0, kdz / kd * 0.30f};
                    en.hit_flash = 6;
                    if (!r.died) en.hitstun = 14;   // interrupt its attack (duration [TUNED])
                    const std::string nm = casts.get(en.cast_id) && !casts.get(en.cast_id)->display.empty()
                                           ? casts.get(en.cast_id)->display : ("cast " + std::to_string(en.cast_id));
                    hud.message(r.died ? (nm + " defeated!")
                                       : (nm + "  -" + std::to_string(dmg) + " (" +
                                          std::to_string(int(en.hp)) + "/" +
                                          std::to_string(int(en.max_hp)) + ")"));
                    if (std::getenv("CLERIA_DMG"))
                        std::printf("  hit %s (lv%d DEF %.0f): %d dmg, %.0f/%.0f left\n",
                                    nm.c_str(), en.level, en.def, r.damage, en.hp, en.max_hp);
                    if (r.died) {
                        ++killed;
                        award_exp(en.exp_reward);
                        // Death entry plays p_hit0 from C for any actor, and the enemy's
                        // own death animation adds its z_dead* pair (SFX_TRIGGERS.md §4.3).
                        audio.play(kSfxDeathEntry, kSfxAnimVol);
                        for (int id : enemy_death_sounds(asset_root, casts.sprite_stem(en.cast_id)))
                            audio.play(id, kSfxAnimVol);
                    }
                }
                if (killed) {
                    int left = 0;
                    for (const Actor& en : world.actors())
                        if (!en.is_player() && en.alive()) ++left;
                    std::printf("  defeated %d (%d left)\n", killed, left);
                    if (left == 0) clear_barrier();
                }
            }

            // Enemies hitting the player. Damage is the REAL player-incoming formula
            // (combat.hpp player_incoming_damage = FUN_004fa740), gated by the player's
            // i-frames exactly as the engine does (one hit per window, however many
            // enemies are on you). Damage now comes from the enemy's own ASO attack — its
            // GenerateCollision box, armed on the swing frame in the brain above — not from
            // touching it. player_def is the lv1 curve; no equipment model.
            if (player_iframes > 0) --player_iframes;
            const float pdef = ginfo.curves[kPlayerDef].at(player.level);
            auto hurt_player = [&](float atk) {
                const int dmg = player_incoming_damage(AttackStats{atk, 0, 0}, pdef,
                    [&](float p) { return float(rng_pct(rng)) < p; });
                player.hp -= dmg;
                // Boost builds on hits TAKEN (BOOST.md hook FUN_0046cc10, missing-HP-scaled) —
                // the comeback mechanic. Amount is [H] (tuned for visibility; base const is 25).
                player.boost = std::min(30000.f, player.boost + 2500.f);
                // p_damage is the player's own hurt-reaction sound; death adds p_hit0.
                audio.play(kSfxPlayerHurt, kSfxAnimVol);
                if (player.hp <= 0) audio.play(kSfxDeathEntry, kSfxAnimVol);
                // Player i-frame window (~0.75s) is TUNED, not RE'd; the engine's generic
                // 16-tick grant lets a real-level enemy shred the fresh lv1 player.
                player_iframes = 45;
                hud.message("-" + std::to_string(dmg) + " HP");
                if (player.hp <= 0) {  // down: respawn at full with a grace window
                    player.hp = player.max_hp;
                    spawn_player();
                    player_iframes = 90;
                    hud.message("Down! respawned");
                }
            };
            for (EnemyRender& e : render) {
                Actor* en = e.actor;
                if (e.atk_win <= 0) {
                    // Scriptless enemies (no ASO) keep the old contact check so they are
                    // not harmless; scripted ones deal damage only through their swing.
                    if (!e.avm.ok() && en && en->alive() && en->atk > 0 && player_iframes == 0) {
                        const float mreach = kCharRadius + 0.6f;
                        const float dx = en->pos.x - feet.x, dz = en->pos.z - feet.z;
                        if (dx * dx + dz * dz <= mreach * mreach) hurt_player(en->atk);
                    }
                    continue;
                }
                --e.atk_win;
                if (!en || !en->alive() || en->atk <= 0 || e.atk_hit || player_iframes > 0)
                    continue;
                // The swing's box sits hb.radius ahead of the enemy in its latched heading.
                const float cx = en->pos.x + e.atk_dx * e.hb.radius;
                const float cz = en->pos.z + e.atk_dz * e.hb.radius;
                const float rr = e.hb.radius + kCharRadius;
                const float bx = feet.x - cx, bz = feet.z - cz;
                if (bx * bx + bz * bz > rr * rr) continue;
                e.atk_hit = true;                 // one hit per swing
                hurt_player(en->atk);
            }

            // Footsteps — the game's rule, not ours. It is a COUNTDOWN IN TICKS
            // reloaded from the surface record, gated on the motion state:
            //
            //   FUN_0045c490, once per tick:
            //     if (!enable) skip
            //     if (timer != 0) { timer--; skip }        // 0045cee2
            //     if (!moving_state) { step_counter = 0; skip }
            //     if (motion_state in {0,1}) TryFootstep()  // 0045cf26
            //   FUN_0045d600 (TryFootstep):
            //     if (!IsLand) return
            //     rec = SURFACE[latched_surface]
            //     if (rec.se_id == 0) return               // silent: NO reload
            //     play(rec.se_id + (step_counter++ & rec.mask))
            //     timer = (state == 1) ? rec.run_ticks : rec.walk_ticks
            //     if (rec.jitter) timer += rng % rec.jitter
            //
            // `motion_state` is actor+0x994 — 0 = walk, 1 = run — which is the ASO
            // SECTION ID, so our VM supplies it directly. Note the cadence is
            // deliberately NOT animation-synced: 20 ticks/step against C_010's
            // 48-tick walk cycle, so steps drift against footplants by design.
            if (step_timer > 0) {
                --step_timer;
            } else if (grounded && (avm.section() == 0 || avm.section() == 1)) {
                const bool is_running = avm.section() == 1;
                // The exe's jitter uses xorshift128 (FUN_00439120) — the same
                // generator XsoVm implements, though the exe keeps it in a separate
                // state block (DAT_0074622c) from the script RNG.
                step_timer = play_footstep(audio, phys_mesh(room), floor_tri, step_n++,
                                           is_running, game.rng_next());
            } else if (!moving) {
                step_n = 0;   // the exe resets the counter when not in a step state
            }
            // The jump sets the TIMER, not a velocity — apex then lands at ~v0
            // seconds (0.285 -> tick 17), which is what makes it a time and not a
            // speed. No sqrt(2gh): the engine never solves for an apex.
            if (grounded && ks[SDL_SCANCODE_SPACE]) { vy = kJumpV0; grounded = false; }
            if (feet.y < room.mesh.bmin.y - D) spawn_player();

            // Facing: map the world movement heading to one of the 8 sprite dirs
            // (screen-relative -- see heading_to_facing). SEN arrival facing uses the
            // same helper, so a warped-in player faces the same way it would if it had
            // walked in that direction.
            if (moving) facing = heading_to_facing(wish.x, wish.z, cam.yaw);
            // Animation comes from the game's OWN script now. We only choose the
            // ACTION (a section id) and feed the VM what it polls; the script decides
            // the sprite file, the tag, the frame and how long to hold it. That last
            // part is why this matters: the game holds each anime_Pattern for its own
            // anime_Wait(n), and attacks run 1-4 ticks/frame against idle's 10 — a 10x
            // spread that no single rate constant can reproduce.
            //
            // Section ids: 0 walk, 1 run/dash, 2 idle, 3 jump. (Jump is the id!=tag
            // case — Yunica's section 3 loops on animTag 10, which is exactly why
            // resolving by tag rather than index matters.)
            // While a swing is active, play the attack section (0x100 = the basic
            // attack, present in C_010/020/030) so the sprite actually swings instead of
            // sliding in the walk pose. Otherwise: jump / walk / idle.
            const std::int32_t want_sec = !grounded ? 3 : (moving ? 0 : 2);
            // Count how long the current animation frame has been held. Active swing
            // frames advance every 1-6 ticks; a recovery hold sits on one frame for
            // 40+, so a stall well past the former marks "the swing is over".
            if (avm.section() >= 0x100) {
                const long fk = (long(avm.anim_tag()) << 20) ^ avm.frame();
                atk_stall = (fk == atk_frame_key) ? atk_stall + 1 : 0;
                atk_frame_key = fk;
            } else {
                atk_stall = 0;
                atk_frame_key = -1;
            }
            // Attack sections hold on their last frame rather than falling off the end,
            // so locomotion takes back over on the script's cancel window OR, for the
            // sections that never open one, once the animation has clearly stalled into
            // its recovery. Below 0x100 the priority rule inside set_section refuses.
            if (avm.section() < 0x100)
                avm.set_section(want_sec);
            else if (avm.cancelable() || atk_stall >= 10)
                avm.set_section(want_sec, /*force*/true);
            avm.set_is_land(grounded);
            avm.tick();
            // Projectile flight. The motion here IS reverse-engineered (speed += accel,
            // gravity applied to the velocity, heading re-derived from it, and the bullet
            // living exactly as long as its effect). The DAMAGE is not: PROJECTILE.md
            // marks the generic bullet->victim path [UNKNOWN] — the only damage calls in
            // the update loop sit behind a branch for five hardcoded parryable ids. The
            // sphere test and formula below are the melee ones reused so Hugo can fight
            // at all. Stand-in, not engine behaviour; revisit if that path is found.
            for (Projectile& p : shots) {
                step_projectile(p);
                if (!p.from_player) continue;
                for (Actor& en : world.actors()) {
                    if (en.is_player() || !en.alive() || p.hit.count(en.instance_id)) continue;
                    const float bx = en.pos.x - p.pos.x, bz = en.pos.z - p.pos.z;
                    const float by = (en.pos.y + kCharH * 0.5f) - p.pos.y;
                    const float rr = kCharRadius + 0.3f;
                    if (bx * bx + by * by + bz * bz > rr * rr) continue;
                    p.hit.insert(en.instance_id);
                    AttackStats a;
                    a.atk = ginfo.player_atk(character, /*tier*/ 0, player.level);
                    const int dmg = damage_int(a, en.def,
                        [&](float pct) { return float(rng_pct(rng)) < pct; });
                    const HitResult r = apply_damage(en.hp, en.flags, dmg);
                    en.hit_flash = 6;
                    if (r.died) {
                        award_exp(en.exp_reward);
                        audio.play(kSfxDeathEntry, kSfxAnimVol);
                        for (int id : enemy_death_sounds(asset_root, casts.sprite_stem(en.cast_id)))
                            audio.play(id, kSfxAnimVol);
                    }
                    p.age = p.life;   // die on hit — pierce-vs-not is [UNKNOWN] too
                    break;
                }
            }
            shots.erase(std::remove_if(shots.begin(), shots.end(),
                                       [](const Projectile& p) { return !p.alive(); }),
                        shots.end());

            // anime_FireBullet fired. Arg meanings from build/projectile_re/PROJECTILE.md:
            // [2] forward reach, [3] vertical offset, [4] heading offset in DEGREES added
            // to facing, [5..7] an explicit direction used iff non-zero, [8] speed,
            // [9] acceleration, [10] gravity, [12] scale (negative = sentinel for 1.0).
            for (const AsoVm::BulletEvent& b : avm.bullets()) {
                Projectile p;
                p.effect_id = b.id;
                const float reach = b.f[0], voff = b.f[1], head_deg = b.f[2];
                const Vec3 d{b.f[3], b.f[4], b.f[5]};
                const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                if (dl > 1e-6f) {
                    p.dir = {d.x / dl, d.y / dl, d.z / dl};
                } else {
                    // Spawn geometry is the engine's x=sin / z=cos, same as GenerateCollision.
                    const float yaw = std::atan2(face_dir.x, face_dir.z) +
                                      head_deg * 3.14159265358979f / 180.0f;
                    p.dir = {std::sin(yaw), 0, std::cos(yaw)};
                }
                p.speed = b.f[6];
                p.accel = b.f[7];
                p.gravity = b.f[8];
                // The emitter's OWN scale is the world size. arg[12] (=0.15 for Hugo) was
                // read as a scale multiplier, but applying it shrank the shot to a
                // near-invisible sliver, so its role is not that (it may be an anim rate).
                // Default to the emitter scale as-authored; CLERIA_BULLET_SCALE tunes.
                p.scale = 1.0f;
                p.pos = {feet.x + p.dir.x * reach, feet.y + kCharH * 0.5f + voff,
                         feet.z + p.dir.z * reach};
                p.life = bullet_art(g, asset_root, efl, eft, p.effect_id).life;
                // Speed 0 means a stationary attached effect rather than a projectile —
                // that is all of Toal's 8 calls (smoke), so they must not become bullets.
                // How the script's scale arg combines with the emitter's own scale is
                // NOT pinned down by the RE, so it is left tunable rather than silently
                // fudged: CLERIA_BULLET_SCALE multiplies the result.
                if (const char* bs = std::getenv("CLERIA_BULLET_SCALE"))
                    p.scale *= float(std::atof(bs));
                if (std::getenv("CLERIA_BULLET")) {
                    const BulletArt& art = bullet_art(g, asset_root, efl, eft, p.effect_id);
                    std::printf("  [bullet] effect %d (%s) %zu layer(s) life=%d speed=%.3f "
                                "scale=%.3f dir=(%.2f,%.2f,%.2f)\n",
                                p.effect_id, efl.name(p.effect_id).c_str(), art.layers.size(),
                                p.life, p.speed, p.scale, p.dir.x, p.dir.y, p.dir.z);
                    for (const BulletArt::Layer& L : art.layers)
                        std::printf("           layer %ux%u  %zu frames  world size %.3f x %.3f\n",
                                    L.tex.w, L.tex.h, L.frames.size(), L.w * p.scale,
                                    L.h * p.scale);
                }
                if (p.speed > 1e-4f) shots.push_back(std::move(p));
            }
            // GenerateCollision fired: adopt this section's own box for its own lifetime.
            if (avm.armed_hitbox()) {
                atk_hb = avm.hitbox();
                atk_window = atk_hb.lifetime > 0 ? atk_hb.lifetime : 1;
                atk_dir = face_dir;
                atk_hit.clear();
            }
            // anime_SePlay3D events (ASO funcid 0x2a): the animation names its own
            // sounds, so the swing lands on the right frame and is per-character for
            // free -- Yunica's attack section issues 139 = player\p1_sw_slash0, and
            // Hugo/Toal issue their own. Only drained here, on the logic tick: the
            // other avm.tick() calls are init/character-switch and must stay silent.
            for (const AsoVm::SoundEvent& se : avm.sounds()) audio.play(se.id, kSfxAnimVol);
            // The script may switch sprite files mid-action (e.g. the attack section
            // swaps C_111 -> C_114). When it does, the image INDEX for a tag can differ
            // between the old and new AIA, so force a re-decode -- otherwise the sprite
            // keeps a stale/mismatched frame and renders half-missing after a swing.
            const bool aia_changed = sync_aia();

            const int wantImg = chr.find_tag(std::uint32_t(avm.anim_tag()));
            if (wantImg >= 0 && (aia_changed || std::uint32_t(wantImg) != curAnim)) {
                curAnim = std::uint32_t(wantImg);
                shownFrame = -1;
            }
            const int nf = int(std::max(1u, chr.frames_of(curAnim)));
            animFrame = nf ? (avm.frame() % nf) : 0;
            if (animFrame != shownFrame) { chr.set_anim(g, curAnim, animFrame); shownFrame = animFrame; }

            // The room's authored camera (SCM): region-selected, follow + orbit, blended.
            update_camera(room, feet, dt);
            if (g_cam.on) apply_cam_state(cam, feet);
        }

        // SEN trigger: move into a doorway volume to warp. Test the PLAYER only, exactly
        // as the engine does (FUN_0042cf00 tests actor+0x50) -- the old code also tested
        // the camera, and the follow camera sits ~24u behind and above the player, so it
        // entered the door volume well before the player did and warped too early. In fly
        // mode there is no player, so the probe is the camera itself. EDGE-triggered: the
        // warp fires only on ENTERING a volume (the engine arms on entry), which is also
        // what lets a door-return spawn the player standing in the doorway without an
        // instant re-warp -- inside_trigger is primed to that door at spawn.
        {
            const Vec3 probe = fly ? cam.pos : Vec3{feet.x, feet.y + kCharH * 0.5f, feet.z};
            int now_in = -1;
            for (std::size_t i = 0; i < room.sen.triggers.size(); ++i)
                if (room.sen.triggers[i].contains(probe)) { now_in = int(i); break; }
            if (now_in >= 0 && now_in != inside_trigger && cooldown <= 0) {
                const SenTrigger& t = room.sen.triggers[std::size_t(now_in)];
                transition(t.dest_scene, &t);   // re-primes inside_trigger for the new room
            } else {
                inside_trigger = now_in;
            }
        }
        }  // ---- end fixed-step logic ----------------------------------------
        // Barrier fight: if the last group-1 seed died during this frame's ticks, grant
        // the barrier's flag and re-gate the room so its chest unlocks (no-op otherwise).
        clear_barrier();
        // UV scroll — the water. `phase += velocity` ONCE PER TICK, unscaled: the
        // disassembly is unambiguous (FUN_0044cda0 @0x44e165 — the neighbouring
        // fields ARE MULSS-scaled by a delta, the scroll block has no MULSS at all).
        // Applying `ticks * velocity` once here is exactly equivalent, since the
        // accumulation is linear. The engine uploads c1.xy = baseTexcoordOffset +
        // phase; we bake the phase into the batch's UVs, which needs no per-draw
        // uniform and keeps the batching intact.
        if (ticks && !room.scrolls.empty() && !std::getenv("CLERIA_NOSCROLL"))
            advance_scroll(g, room, ticks);
        // Presentation advances by the time we actually SIMULATED this frame, not by
        // wall time — so fog blends, flame flipbooks and HUD bars stay locked to the
        // tick and slow down with timeScale, instead of drifting off it.
        const float dt = float(ticks) * kTickDt;
        // The exe keeps separate FPS and TPS counters (0x767c34 / 0x767c3c, both
        // refreshed every 1000 ms) — proof that ticks are not frames. CLERIA_TPS=1
        // prints ours, which is how we check the decoupling actually holds.
        if (std::getenv("CLERIA_TPS")) {
            static int tps = 0, fps = 0; static Uint64 t0 = SDL_GetTicks();
            tps += ticks; ++fps;
            if (now - t0 >= 1000) {
                std::printf("  %d fps / %d tps  (timeScale %.3f)\n", fps, tps, g_time_scale);
                tps = fps = 0; t0 = now;
            }
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
        // Character billboard (play mode only; fly is a free debug camera). Draw the floor
        // shadow first, on the actual floor under the player (so it stays put during a jump).
        if (!fly && facing >= 0 && facing < 8 && chr.dirs[facing].tex) {
            const float sfl = floor_at(phys_mesh(room), feet.x, feet.z, feet.y + 0.5f);
            draw_shadow(g, view, depth, shadows.tex, feet,
                        (sfl > kNoFloor ? sfl : feet.y), 0.6f);
            draw_character(g, view, depth, feet, kCharH, chr.dirs[facing], cam.forward(), cam.pos);
        }
        draw_enemies(g, view, depth, efx, render, cam.forward(), cam.pos, fx_time, shadows.tex);
        fx_time += dt;
        draw_flames(g, room, view, depth, flames, cam.forward(), fx_time);
        draw_projectiles(g, shots, asset_root, efl, eft, view, depth, cam.forward());
        // Script-placed persistent effects (the barrier ward). Killed by op 0x119 when
        // the barrier breaks, so it renders only while the fight is unresolved.
        draw_located_effects(g, world.effects(), asset_root, efl, eft, view, depth,
                             cam.forward(), fx_time);
        player.character = character;
        // EXP bar: progress from this level's threshold to the next (100% at the cap).
        if (ginfo.ok()) {
            const double e0 = ginfo.exp_to_level(player.level);
            const double e1 = ginfo.exp_to_level(player.level + 1);
            player.exp_pct = (player.level >= 60) ? 100.0f
                : float(std::clamp((double(player.exp) - e0) / std::max(1.0, e1 - e0), 0.0, 1.0) * 100.0);
        }
        hud.render(g, view, cw, ch, room.name + "  (" + kCharNames[character] + ")", dt, &player);
        // Cutscene dialogue box over the frozen world (FACEWIN0 bottom bar + name + text).
        if (csHost.box_active && pauseOk) {
            write_mvp(g, Mat4::identity());   // the box is screen-space
            WGPUTextureView ddepth = make_depth(g.device, cw, ch);
            pauseUi.draw_dialogue(g, pmf, view, ddepth, cw, ch, csHost.name, csHost.text, csHost.reveal);
            wgpuTextureViewRelease(ddepth);
        }
        // Area-name caption card (op 0xe2 = pre-rendered IMAGE/<name>.dds), fading out.
        if (csHost.caption_timer > 0) {
            if (csHost.caption_name != captionLoaded) {
                std::string nm = csHost.caption_name;
                for (char& c : nm) c = char(std::toupper((unsigned char)c));
                Dds cd = load_dds(asset_root / "IMAGE" / (nm + ".DDS"));
                if (cd.ok()) { captionTex = upload_rgba(g, cd.width, cd.height, cd.data.data()); }
                captionLoaded = csHost.caption_name;
            }
            if (captionTex.tex) {
                write_mvp(g, Mat4::identity());
                WGPUTextureView cdepth = make_depth(g.device, cw, ch);
                const float sw = float(cw), sh = float(ch);
                const float cw2 = sw * 0.6f, chh = cw2 * float(captionTex.h) / float(captionTex.w);
                const float cx = (sw - cw2) * 0.5f, cy = sh * 0.16f;
                const float a = std::min(1.0f, csHost.caption_timer / 45.0f);   // fade over the last 45f
                auto NX = [&](float x) { return 2.0f * x / sw - 1.0f; };
                auto NY = [&](float y) { return 1.0f - 2.0f * y / sh; };
                draw_screen_image(g, view, cdepth, false, captionTex, NX(cx), NY(cy), NX(cx + cw2), NY(cy + chh),
                                  0.1f, 0, 0, 1, 1, 0.02f, 0.02f, 0.05f, 0.0f, false, a, a, a);
                wgpuTextureViewRelease(cdepth);
            }
            --csHost.caption_timer;
        }
        // Pause menu overlay: the pause-book Equip page over the frozen game (Esc to close).
        if (paused && pauseOk) {
            TitleFx::EquipView ev;
            ev.character = character;
            ev.level = player.level;
            ev.hp = player.hp;
            ev.maxhp = player.max_hp;
            if (ginfo.ok()) {
                ev.str = int(std::lround(ginfo.player_atk(character, 0, player.level)));
                ev.def = int(std::lround(ginfo.curves[kPlayerDef].at(player.level)));
                ev.exp = long(std::llround(ginfo.exp_to_level(player.level)));
                ev.next = long(std::llround(ginfo.exp_to_level(player.level + 1)
                                            - ginfo.exp_to_level(player.level)));
            }
            write_mvp(g, Mat4::identity());  // the pause-book pages are screen-space
            // Fresh depth (like the title-menu path's tdepth) so the world's z-buffer can't
            // occlude the book; only allocated while the pause menu is actually open.
            WGPUTextureView pdepth = make_depth(g.device, cw, ch);
            switch (pausePage) {
                case 1: {  // Item / Inventory
                    static const int owned[] = {1, 48, 8, 78, 79, 116};  // stub inventory
                    ev.caption = "Examine or use an item.";
                    pauseUi.draw_item(g, pmf, view, pdepth, cw, ch, ev, owned, 6, pitems, asset_root);
                    break;
                }
                case 2:  // Topic (Character / Monster / Manual)
                    pauseUi.draw_topic(g, pmf, view, pdepth, cw, ch, 0);
                    break;
                case 3:  // Save / Load
                    pauseUi.draw_svld(g, pmf, view, pdepth, cw, ch, /*save=*/false, /*fileTab=*/5, /*sel=*/0);
                    break;
                default:  // Equip
                    ev.caption = "Your equipped weapon.";
                    pauseUi.draw_equip(g, pmf, view, pdepth, cw, ch, ev, pitems, asset_root);
                    break;
            }
            wgpuTextureViewRelease(pdepth);
        }
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // unbuffered: breadcrumbs survive an assert/crash
    init_cull_env();
    // No args -> free-fly the tower base (S_1000) with default assets.
    const fs::path asset_root = (argc >= 2) ? fs::path(argv[1]) : default_assets(argv[0]);

    // Headless Option-persistence check: load yso_win.ini (CLERIA_INI), print, mutate, save, dump.
    if (argc >= 3 && std::strcmp(argv[2], "--config-test") == 0) {
        int sl[3]; bool ck[4];
        options_load(sl, ck);
        std::printf("LOADED bgm=%d sfx=%d bright=%d | analog=%d dash=%d holdatk=%d always=%d\n",
                    sl[0], sl[1], sl[2], ck[0], ck[1], ck[2], ck[3]);
        sl[0] = 333; ck[1] = !ck[1];   // mutate BGM + toggle Double-tap-to-Dash
        options_save(sl, ck);
        std::ifstream f(options_ini_path()); std::string line;
        std::printf("--- yso_win.ini after save ---\n");
        while (std::getline(f, line)) std::printf("%s\n", line.c_str());
        return 0;
    }

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
    if (argc >= 4 && std::strcmp(argv[3], "--dialogue") == 0) {
        // cleria-view <root> <sob> --dialogue out.ppm [W H] — preview the REAL cutscene box
        // (TitleFx::draw_dialogue: FACEWIN0 frame + Felghana name/text) over a room, headless.
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        Room room = load_room(g, asset_root, sob_path);
        std::uint32_t W = argc >= 6 ? std::uint32_t(std::atoi(argv[5])) : 1280;
        std::uint32_t H = argc >= 7 ? std::uint32_t(std::atoi(argv[6])) : 720;
        static TitleFx tfx; tfx.init(g, asset_root);
        static MenuFont mf;
        mf.init(g, WGPUTextureFormat_RGBA8Unorm, find_felghana(asset_root), int(std::lround(32.0 * 960.0 / 768.0)));
        g_play_overlay = [W, H](Gpu& gg, WGPUTextureView cv, WGPUTextureView dv, std::uint32_t, std::uint32_t) {
            write_mvp(gg, Mat4::identity());
            tfx.draw_dialogue(gg, mf, cv, dv, W, H, "Iris",
                              "Roy and Ramona just left.\\nThe Commander and I will be following shortly.", -1);
        };
        return render_play(g, room, argv[4], W, H, asset_root, 1.5708f, -0.72f, 28.8f, 2.0f, 2);
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
        // INVENTED, and kept that way on purpose — CASTINFO's derived 1.075 renders a
        // doll against this room's own authored camera distances. See the long note on
        // kCharH in play mode.
        float hgt   = argc >= 11 ? float(std::atof(argv[10])) : 2.0f;
        std::uint32_t dir = argc >= 12 ? std::uint32_t(std::atoi(argv[11])) : 2;
        if (argc >= 13) g_fov = float(std::atof(argv[12]));
        return render_play(g, room, argv[4], W, H, asset_root, yaw, pitch, dist, hgt, dir);
    }
    if (argc >= 4 && (std::strcmp(argv[3], "--title") == 0 ||
                      std::strcmp(argv[3], "--charselect") == 0 ||
                      std::strcmp(argv[3], "--bonus") == 0 ||
                      std::strcmp(argv[3], "--options") == 0 ||
                      std::strcmp(argv[3], "--equip") == 0 ||
                      std::strcmp(argv[3], "--item") == 0 ||
                      std::strcmp(argv[3], "--topic") == 0 ||
                      std::strcmp(argv[3], "--shop") == 0 ||
                      std::strcmp(argv[3], "--load") == 0 ||
                      std::strcmp(argv[3], "--save") == 0)) {
        // cleria-view <root> <ignored> --title|--charselect|--bonus|--options|--equip|--item|--topic|--shop|--load|--save out.ppm [W H]
        const int mode = std::strcmp(argv[3], "--charselect") == 0 ? 1
                       : std::strcmp(argv[3], "--bonus") == 0      ? 2
                       : std::strcmp(argv[3], "--options") == 0    ? 3
                       : std::strcmp(argv[3], "--equip") == 0      ? 4
                       : std::strcmp(argv[3], "--item") == 0       ? 5
                       : std::strcmp(argv[3], "--topic") == 0      ? 6
                       : std::strcmp(argv[3], "--shop") == 0       ? 7
                       : std::strcmp(argv[3], "--load") == 0       ? 8
                       : std::strcmp(argv[3], "--save") == 0       ? 9 : 0;
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        std::uint32_t W = argc >= 6 ? std::uint32_t(std::atoi(argv[5])) : 1280;
        std::uint32_t H = argc >= 7 ? std::uint32_t(std::atoi(argv[6])) : 800;
        return render_title(g, asset_root, argv[4], W, H, mode);
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
    // Show the title screen only on a bare launch (no room SOB given); a room argument
    // (the dev/test path) goes straight into that room, unchanged.
    return render_window(g, asset_root, sob_path, /*show_title=*/argc < 3);
}
