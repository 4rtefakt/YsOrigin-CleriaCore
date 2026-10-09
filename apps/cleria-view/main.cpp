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
#include <map>
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
#include "cleria/bullet.hpp"
#include "cleria/inventory.hpp"
#include "cleria/pausebook.hpp"
#include "cleria/pickup.hpp"
#include "cleria/popup.hpp"
#include "cleria/meshobj.hpp"
#include "cleria/ef2_sim.hpp"
#include "cleria/dds.hpp"
#include "cleria/mathx.hpp"
#include "cleria/cast.hpp"
#include "cleria/gameinfo.hpp"
#include "cleria/eex.hpp"
#include "cleria/enemies.hpp"
#include "cleria/combat.hpp"
#include "cleria/ailments.hpp"
#include "cleria/hit.hpp"
#include "cleria/hit_feedback.hpp"
#include "cleria/hudstate.hpp"
#include "cleria/body_push.hpp"
#include "cleria/arena_player.hpp"
#include "cleria/enemy_ai.hpp"
#include "cleria/brains/revive.hpp"
#include "cleria/sand.hpp"
#include "cleria/ice.hpp"
#include "cleria/brains/brains.hpp"
#include "cleria/physics.hpp"
#include "cleria/physics_player.hpp"
#include "cleria/surface.hpp"
#include "cleria/audio.hpp"
#include "cleria/bgmdef.hpp"
#include "cleria/gamedata.hpp"
#include "cleria/movie.hpp"
#include "cleria/scenelist.hpp"
#include "cleria/items.hpp"
#include "cleria/ledge.hpp"
#include "cleria/rider.hpp"
#include "cleria/scene.hpp"
#include "cleria/sev.hpp"
#include "cleria/scm.hpp"
#include "cleria/sef.hpp"
#include "cleria/sct.hpp"
#include "cleria/sen.hpp"
#include "cleria/sfo.hpp"
#include "cleria/minifont.hpp"
#include "cleria/save.hpp"
#include "cleria/world.hpp"
#include "cleria/xso.hpp"
#include "cleria/wndtext.hpp"
#include "cleria/extfont.hpp"
#include "cleria/wnddialog.hpp"
#include "cleria/titlescreen.hpp"
#include "cleria/eventsprites.hpp"
#include "cleria/boss01.hpp"
#include "cleria/boss02.hpp"
#include "cleria/boss03.hpp"
#include "cleria/boss04.hpp"
#include "cleria/boss05.hpp"
#include "cleria/boss06.hpp"
#include "cleria/boss06_zava.hpp"
#include "cleria/boss07.hpp"
#include "cleria/boss08.hpp"
#include "cleria/emotion.hpp"
#include "cleria/camrig.hpp"
#include "cleria/skillmp.hpp"
#include "cleria/hugo_eyes.hpp"
#include "cleria/exp_award.hpp"
#include "cleria/oxygen.hpp"
#include "cleria/hazard.hpp"
#include "cleria/player_actions.hpp"
#include "cleria/locus.hpp"
#include "cleria/ymo.hpp"
#include "cleria/input_map.hpp"
#include "cleria/mouse.hpp"
#include "cleria/settings.hpp"
#include "cleria/vfs.hpp"
#include "cleria/duel.hpp"
#include "cleria/weather.hpp"
#include "cleria/screenfx.hpp"
#include <ctime>
#include <optional>
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
    std::ifstream f(vfs::resolve(p), std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

static fs::path exe_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) return fs::path(buf).parent_path();
#else
    if (const char* b = SDL_GetBasePath()) return fs::path(b);   // the exe's folder, cached by SDL
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
    // The magic/skill resource (engine DAT_0076a738 cur / DAT_0076a734 max = 3000.0, mirrored
    // to actor+0xa0/+0x9c). NOT `sp` above, which is the shop currency. Regenerates while the
    // regen mode bit is live; a skill spends its per-character cost (ABILITY.md §1a/§2a).
    float sp_cur = 3000.0f, sp_max = 3000.0f;
    float sp_lag = 3000.0f;
    // The oxygen bar (build/physics_re/WATER.md §3): O2 0x76c1b8, its alpha 0x76c1bc, and the
    // player's projected origin in window pixels (the bar's anchor).
    float o2 = 6000.0f; int o2_alpha = 0; float o2_px = 0, o2_py = 0;   // DAT_0076c194: regen waits for it to drain after a spend (skillmp::Mp)
    int character = 1;  // portrait: 1 Yunica / 2 Hugo / 3 Toal
    int magic = -1;     // DAT_0076a630, the equipped bracelet 0x74..0x76 (the HUD's skill badge)
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
    // Boost MODE (engine DAT_0076bb50, mirrored into flag 141 for the player's ASO, and the
    // mode bit DAT_0076c198 & 0x20). Entering requires a FULL gauge (== 30000 exactly) and
    // grants 16 i-frames; the gauge then drains ~30/tick (~1000 frames on Normal).
    bool boosting = false;
    // build/audit/PLAYTEST_1_HUD.md (src/cleria/hudstate.hpp): builder B's ready test, its pulse
    // counter (0x76c184, or 0x76c188 while charged: c138 == 2), the buff values / timers
    // (0x76b904 / b908 / b900, 0x76c19c / 1a0 / 1a4) and their full length, the combo meter
    // (0x76c1a8 / 0x76c1b0) and the Fix gate (0x75ad90 + 0x75ad94) of the bars and lines.
    bool skill_ready = false;
    int charge_level = 0;
    std::int32_t pulse = 0;
    float buff_val[3] = {0, 0, 0};
    std::int32_t buff_timer[3] = {0, 0, 0};
    float buff_full = 1800.0f;
    int combo = 0;
    std::int32_t combo_timer = 0;
    bool hud_fix = false;
    std::uint32_t ail_mask = 0;        // the status mask 0x76c15c (ailments.hpp): the HUD's four lines
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
  @location(3) opacity : f32,   // characters only: 1 - nrm.x (sprites pass nx = 1 - opacity)
  @location(4) spec : vec3<f32>,  // characters only: the D3D vertex SPECULAR, added (see fs_char)
};

@vertex
fn vs(@location(0) pos : vec3<f32>, @location(1) nrm : vec3<f32>,
      @location(2) uv : vec2<f32>, @location(3) color : vec4<f32>) -> VsOut {
  var o : VsOut;
  o.clip = u.mvp * vec4<f32>(pos, 1.0);
  o.uv = uv;
  o.color = color;              // baked per-vertex lighting (D3D diffuse)
  o.depth = o.clip.w;           // LH perspective: w == view-space z
  o.opacity = 1.0 - nrm.x;      // read by fs_char only; every non-sprite user passes nx = 0
  // A sprite's specular RGB (0xRRGGBB) rides in nrm.y as -(1 + rgb), exact in a float; a real
  // normal (|y| <= 1) or the 0 / 1 dummies other users pass decode to no specular.
  o.spec = vec3<f32>(0.0, 0.0, 0.0);
  if (nrm.y < -1.5) {
    let s = u32(-nrm.y - 1.0);
    o.spec = vec3<f32>(f32((s >> 16u) & 255u), f32((s >> 8u) & 255u), f32(s & 255u)) / 255.0;
  }
  return o;
}

@fragment
fn fs(in : VsOut) -> @location(0) vec4<f32> {
  let tc = textureSample(tex, smp, in.uv);
  if (tc.a < 0.5) { discard; }
  // Fixed-function MODULATE: texture * baked vertex colour, + the specular oD1 (0 but for a model's
  // additive colour c17: the boss hit flash, Room::mat_add), saturated.
  var c = min(tc.rgb * in.color.rgb + in.spec, vec3<f32>(1.0, 1.0, 1.0));
  // D3D9 linear fog, driven by the room's real SFO region data:
  //   f = (far - d) / (far - near), clamped;  colour = lerp(fog, lit, f)
  if (u.fogParams.z > 0.5) {
    let f = clamp((u.fogParams.y - in.depth) / max(u.fogParams.y - u.fogParams.x, 0.001),
                  0.0, 1.0);
    c = mix(u.fogColor.rgb, c, f);
  }
  return vec4<f32>(c, 1.0);
}

// Blended YMO materials (modes 1-3, material_blend_mode). Stage 0 is MODULATE in colour
// AND alpha, TEXTURE x DIFFUSE (FUN_005f6e50: COLOROP 4, ALPHAOP 4, ARG1 TEXTURE, ARG2
// DIFFUSE), and DIFFUSE = oD0 = c16 * v2 — the material alpha is baked into v2.a at load.
// ALPHATESTENABLE with ALPHAREF 1 drops only fully transparent texels. Mode 1 fogs to the
// room's fog colour; modes 2/3 set FOGCOLOR 0 (FUN_005f92b0 local_60), so they fog to black.
fn mat_lit(in : VsOut) -> vec4<f32> {
  let tc = textureSample(tex, smp, in.uv);
  return vec4<f32>(min(tc.rgb * in.color.rgb + in.spec, vec3<f32>(1.0, 1.0, 1.0)), tc.a * in.color.a);
}
fn fog_f(depth : f32) -> f32 {
  return clamp((u.fogParams.y - depth) / max(u.fogParams.y - u.fogParams.x, 0.001), 0.0, 1.0);
}
@fragment
fn fs_mat(in : VsOut) -> @location(0) vec4<f32> {
  var c = mat_lit(in);
  if (c.a * 255.0 < 0.5) { discard; }
  if (u.fogParams.z > 0.5) { c = vec4<f32>(mix(u.fogColor.rgb, c.rgb, fog_f(in.depth)), c.a); }
  return c;
}
// FUN_005f8de0's depth pre-pass (material_zprepass): ALPHAOP SELECTARG1(TEXTURE), ALPHAREF
// 0xbf, ALPHAFUNC GREATEREQUAL — the texture's own alpha, no vertex alpha. Depth only.
@fragment
fn fs_zpre(in : VsOut) -> @location(0) vec4<f32> {
  let a = textureSample(tex, smp, in.uv).a;
  if (round(a * 255.0) < 191.0) { discard; }
  return vec4<f32>(0.0, 0.0, 0.0, a);
}
@fragment
fn fs_mat_add(in : VsOut) -> @location(0) vec4<f32> {
  var c = mat_lit(in);
  if (c.a * 255.0 < 0.5) { discard; }
  if (u.fogParams.z > 0.5) { c = vec4<f32>(c.rgb * fog_f(in.depth), c.a); }
  return c;
}

// Character/enemy sprites: ALPHA-BLEND, not the room's hard 0.5 alpha-test. The AIA
// sprites carry genuine partial alpha (anti-aliased edges, translucent detail); cutting
// it at 0.5 punched holes in the player and read as "half transparent". Discard only
// the fully-transparent texels and hand the real alpha to the blender.
@fragment
fn fs_char(in : VsOut) -> @location(0) vec4<f32> {
  let tc = textureSample(tex, smp, in.uv);
  if (tc.a < 0.004) { discard; }
  // D3D fixed function with SPECULARENABLE on (FUN_005f1870 sets render state 0x1d = 1 at device
  // init): texture x diffuse + the vertex specular, saturated, before the fog. The actor colour
  // builder FUN_00473c60 writes the specular word (+0x14c): the hurt tint (0x6b3713), the pickup
  // glint (grey), the marker glow.
  var c = min(tc.rgb * in.color.rgb + in.spec, vec3<f32>(1.0, 1.0, 1.0));
  if (u.fogParams.z > 0.5) {
    let f = clamp((u.fogParams.y - in.depth) / max(u.fogParams.y - u.fogParams.x, 0.001),
                  0.0, 1.0);
    c = mix(u.fogColor.rgb, c, f);
  }
  // Hit-flash: the vertex-colour alpha carries a white-tint amount (0 normally). The
  // room shader (fs) never reads color.a, so this channel is free for characters.
  c = mix(c, vec3<f32>(1.0, 1.0, 1.0), in.color.a);
  return vec4<f32>(c, tc.a * clamp(in.opacity, 0.0, 1.0));
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


// ---- the engine, in fixed include order (see PLAN.md Phase 0) ----------------
// Each part is a textual slice of the former 6,381-line main.cpp: one TU, split
// for editability. Order matters; nothing here is standalone.
#include "view_platform.inl"             // the window surface + OS calls per platform (issue #46)
#include "view_gpu_room.inl"
#include "view_camera.inl"
#include "view_sprites.inl"
#include "view_world_draw.inl"
#include "view_hud.inl"
#include "view_input.inl"
#include "view_shell.inl"
#include "view_mods.inl"
#include "view_modfile.inl"
#include "view_content.inl"
#include "view_access.inl"
#include "view_pad.inl"
#include "view_speedrun.inl"
#include "view_video.inl"
#include "view_mask.inl"                 // the Mask of Eyes (PLAYTEST_1_STORY.md §1)
#include "view_screenfx.inl"            // ZoomBlur (SCREEN_EFFECTS.md §7)
#include "view_lua.inl"
#include "view_headless.inl"
#include "view_menus.inl"
#include "view_book.inl"
#include "view_game.inl"

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

// Locate the extracted-asset tree when not given on the command line: $CLERIA_ASSETS, then
// CleriaCore's data folder once an unpack there is complete (src/cleria/gamedata: $CLERIA_DATA,
// else %LOCALAPPDATA%\CleriaCore\data), then the legacy <exe>/../../assets, <exe>/assets and
// the repo path.
static fs::path default_assets(const char* argv0) {
    std::error_code ec;
    if (const char* e = std::getenv("CLERIA_ASSETS"))
        if (fs::exists(fs::path(e) / "MAP", ec)) return e;
    if (fs::path d = gamedata::ready_data_dir(); !d.empty()) return d;
    const fs::path exedir = fs::absolute(fs::path(argv0), ec).parent_path();
    for (const fs::path& c : {exedir / ".." / ".." / "assets", exedir / "assets",
                              fs::path("C:/Users/kores/repos/YsOrigin-CleriaCore/assets")})
        if (fs::exists(c / "MAP", ec)) return fs::weakly_canonical(c, ec);
    return "C:/Users/kores/repos/YsOrigin-CleriaCore/assets";
}

#ifdef _WIN32
// cleria-view links as a Windows (GUI) program, so Explorer, the Start menu and a .cleriamod
// double-click open no console window. A parent that hands us pipes (tests, drive runs, a
// capture) keeps them as stdout / stderr; started from a console we attach to it; and
// CLERIA_CONSOLE=1 opens a console of our own for the logs.
static void attach_console() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS) && !(std::getenv("CLERIA_CONSOLE") && AllocConsole())) return;
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
}
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    attach_console();
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // unbuffered: breadcrumbs survive an assert/crash
    init_cull_env();
    modfile_take_args(argc, argv);   // <file>.cleriamod / --install-mod <file> (view_modfile.inl)
    // No args -> free-fly the tower base (S_1000) with default assets.
    const fs::path asset_root = (argc >= 2) ? fs::path(argv[1]) : default_assets(argv[0]);

    // Headless Option-persistence check: load yso_win.ini (CLERIA_INI), print, mutate, save, dump.
    if (argc >= 3 && std::strcmp(argv[2], "--config-test") == 0) {
        if (!std::getenv("CLERIA_INI")) { std::fprintf(stderr, "--config-test needs CLERIA_INI=<file>\n"); return 1; }
        int sl[3]; bool ck[4];
        options_load(sl, ck);
        std::printf("LOADED bgm=%d sfx=%d bright=%d | analog=%d dash=%d holdatk=%d always=%d\n",
                    sl[0], sl[1], sl[2], ck[0], ck[1], ck[2], ck[3]);
        sl[0] = 333; ck[1] = !ck[1];   // mutate BGM + toggle Double-tap-to-Dash
        options_save(sl, ck);
        std::ifstream f(settings().path()); std::string line;
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

    // CLERIA_FLAGS="300=1,150=2" — seed the game flags the headless room load sees. The
    // room-load scripts branch on them (S_SAVEOBJECTCHANGE picks the statue variant on
    // flag 300 and the character on 150), so without this every headless shot renders the
    // fresh-game state and a cleansed/altered room can never be looked at.
    static std::int32_t hl_flags[0x200] = {};
    const std::int32_t* hl_flagp = nullptr;
    int hl_char = 1;
    if (const char* fs_env = std::getenv("CLERIA_FLAGS")) {
        std::string s(fs_env);
        std::size_t i = 0;
        while (i < s.size()) {
            std::size_t c = s.find(',', i);
            if (c == std::string::npos) c = s.size();
            const std::string tok = s.substr(i, c - i);
            const std::size_t eq = tok.find('=');
            if (eq != std::string::npos) {
                const int idx = std::atoi(tok.substr(0, eq).c_str());
                const int val = std::atoi(tok.substr(eq + 1).c_str());
                if (idx >= 0 && idx < 0x200) hl_flags[idx] = val;
            }
            i = c + 1;
        }
        hl_flagp = hl_flags;
        if (hl_flags[150] >= 1 && hl_flags[150] <= 3) hl_char = hl_flags[150];
        std::printf("flags:  CLERIA_FLAGS applied (character %d)\n", hl_char);
    }

    if (shot) {
        // shot mode builds its own pipeline (RGBA target) then loads one room
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        Room room = load_room(g, asset_root, sob_path, hl_flagp, hl_char);
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
        Room room = load_room(g, asset_root, sob_path, hl_flagp, hl_char);
        std::uint32_t W = argc >= 6 ? std::uint32_t(std::atoi(argv[5])) : 1280;
        std::uint32_t H = argc >= 7 ? std::uint32_t(std::atoi(argv[6])) : 720;
        float yaw = argc >= 8 ? float(std::atof(argv[7])) : 1.0f;
        return render_fps(g, room, argv[4], W, H, yaw, asset_root);
    }
    if (argc >= 4 && std::strcmp(argv[3], "--dialogue") == 0) {
        // cleria-view <root> <sob> --dialogue out.ppm [W H] — preview the REAL cutscene box
        // (TitleFx::draw_dialogue: FACEWIN0 frame + Felghana name/text) over a room, headless.
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        Room room = load_room(g, asset_root, sob_path, hl_flagp, hl_char);
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
    if (argc >= 4 && std::strcmp(argv[3], "--itemget") == 0) {
        // cleria-view <root> <sob> --itemget out.ppm [W H itemId] — preview the REAL
        // Window_Treasure box (op 0xd5 -> TitleFx::draw_item_get) over a room, headless.
        // Without this the item-get box was only reachable mid-cutscene, so its frame
        // sizing/icon column could never be checked against the RE or a reference grab.
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        Room room = load_room(g, asset_root, sob_path, hl_flagp, hl_char);
        std::uint32_t W = argc >= 6 ? std::uint32_t(std::atoi(argv[5])) : 1280;
        std::uint32_t H = argc >= 7 ? std::uint32_t(std::atoi(argv[6])) : 720;
        const int itemId = argc >= 8 ? std::atoi(argv[7]) : 89;   // 89 = a chest drop
        static TitleFx tfx; tfx.init(g, asset_root);
        static MenuFont mf;
        mf.init(g, WGPUTextureFormat_RGBA8Unorm, find_felghana(asset_root), int(std::lround(32.0 * 960.0 / 768.0)));
        static ItemTable itbl = ItemTable::load(asset_root / "MISC" / "INVINFO.DAT");
        static fs::path aroot = asset_root;
        g_play_overlay = [W, H, itemId](Gpu& gg, WGPUTextureView cv, WGPUTextureView dv,
                                        std::uint32_t, std::uint32_t) {
            write_mvp(gg, Mat4::identity());
            const SpriteTex* ic = tfx.icon(gg, itbl, aroot, itemId);
            // 512,122 is the op's default authored origin (x=512 => horizontally centred).
            tfx.draw_item_get(gg, mf, cv, dv, W, H, 512.0f, 122.0f, itbl.name(itemId), ic);
        };
        return render_play(g, room, argv[4], W, H, asset_root, 1.5708f, -0.72f, 28.8f, 2.0f, 2);
    }
    if (play) {
        // cleria-view <root> <sob> --play out.ppm [W H yaw pitch distFrac heightFrac dir]
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        Room room = load_room(g, asset_root, sob_path, hl_flagp, hl_char);
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
    if (argc >= 5 && std::strcmp(argv[3], "--wnd") == 0) {
        // cleria-view <root> <ignored> --wnd out.ppm [W H]: the event windows (view_book.inl).
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        const std::uint32_t W = argc >= 6 ? std::uint32_t(std::atoi(argv[5])) : 1280;
        const std::uint32_t H = argc >= 7 ? std::uint32_t(std::atoi(argv[6])) : 720;
        return render_wnd(g, asset_root, argv[4], W, H);
    }
    if (argc >= 5 && std::strcmp(argv[3], "--book") == 0) {
        // cleria-view <root> <ignored> --book out.ppm [W H]: the rebuilt pause book (view_book.inl).
        build_pipeline(g, WGPUTextureFormat_RGBA8Unorm);
        const std::uint32_t W = argc >= 6 ? std::uint32_t(std::atoi(argv[5])) : 1280;
        const std::uint32_t H = argc >= 7 ? std::uint32_t(std::atoi(argv[6])) : 800;
        return render_book(g, asset_root, argv[4], W, H);
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
        Room room = load_room(g, asset_root, sob_path, hl_flagp, hl_char);
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
