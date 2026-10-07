#pragma once
// The Video page's pure part (build/shell_re/VIDEO.md): CleriaCore's own [cleria.video] switches, and
// the math behind retail's graphics keys that CleriaCore's renderer honours -- the gamma ramp
// (FUN_005d4de0), the sampler filter table (FUN_005d4f00), the projection aspect (FUN_005e2670), the
// FPS display text (FUN_00429530) -- plus the extras' helpers (render scale, frame limiter, the frame
// statistics, the display-mode lists). The view (apps/cleria-view/view_video.inl) owns the GPU passes,
// the page and the window. Every value chosen here rather than traced is tagged [H].
#include <cstdint>
#include <string>
#include <vector>

namespace cleria {
class Ini;

namespace video {

// ---- [cleria.video] -------------------------------------------------------------------------------
enum WindowMode : int { Windowed = 0, Borderless = 1, Exclusive = 2 };

struct Settings {
    // WindowMode. Absent from an older cleria.ini: taken from its BorderlessFullscreen key (1 ->
    // Borderless, else Windowed), so a CleriaCore user's window does not change on update. Retail's own
    // [graphics] Windowed key is only mirrored (written), never read for this: see VIDEO.md §3.
    int window_mode = Windowed;
    float render_scale = 1.0f;   // RenderScale: the 3D world's resolution x the window's (1 = native)
    int fps_cap = 0;             // FpsCap: frames per second, 0 = unlimited (presentation only)
    bool perf_graph = false;     // PerfGraph: the frame-time graph overlay
};

inline constexpr float kScaleMin = 0.5f, kScaleMax = 2.0f, kScaleStep = 0.25f;   // [H]
inline constexpr int kFpsCapMin = 15, kFpsCapMax = 1000;                         // [H]

void read(const Ini& ini, Settings& s);    // clamps to the ranges above
void write(Ini& ini, const Settings& s);

// Retail's [graphics] Windowed for a CleriaCore window mode: borderless has no retail equivalent, and
// its closest one is retail's fullscreen (Windowed=0).
inline int retail_windowed(int mode) { return mode == Windowed ? 1 : 0; }

// ---- gamma: FUN_005d4de0 [BYTES 0x5d4e0b..0x5d4e87] ------------------------------------------------
// g = GammaValue clamped to [0.25, 5.0] (comiss 0x6bd244 = 0.25, minss 0x6bd868 = 5.0); the ramp is
// ramp[i] = (u16)(pow(i * (1/255), 1/g) * 65535) for each channel (0x6bd038 = 1/255, 0x6bdf74 = 65535).
float gamma_exponent(float gamma_value);                 // 1 / clamp(g)
std::uint16_t gamma_ramp(float gamma_value, int i);      // retail's 16-bit ramp entry, i = 0..255
bool gamma_is_identity(float gamma_value);               // 1.0: no pass at all

// ---- the texture sampler: FUN_005d4f00 + table 0x6b754c [BYTES] ------------------------------------
// TextureFilter 0 -> {min, mag, mip} = {POINT, POINT, POINT}, 1 -> {LINEAR, LINEAR, POINT},
// 2 -> {LINEAR, LINEAR, LINEAR}. Anisotropy > 1 sets MAXANISOTROPY and turns a LINEAR min filter into
// ANISOTROPIC; a POINT filter keeps no anisotropy. wgpu needs every filter linear for anisotropy > 1,
// so the mip filter goes linear then too (CleriaCore uploads one mip level: no visible difference).
struct SamplerSpec {
    bool linear = true;          // min + mag
    bool mip_linear = false;
    std::uint16_t anisotropy = 1;
};
SamplerSpec sampler_spec(int texture_filter, int anisotropy);

// ---- the 3D projection's aspect: FUN_005e2670 [EXPORT] -----------------------------------------------
// DAT_007b38f8 = 4/3 when StretchAspect ("Stretch Widescreen") is set, else backbuffer W / H.
float projection_aspect(std::uint32_t w, std::uint32_t h, bool stretch_aspect);

// ---- render scale -----------------------------------------------------------------------------------
struct Extent { std::uint32_t w = 0, h = 0; };
// The world target for a w x h window at `scale`, each side rounded and clamped to [1, max_dim].
Extent scaled_extent(std::uint32_t w, std::uint32_t h, float scale, std::uint32_t max_dim);
float clamp_scale(float s);      // to [kScaleMin, kScaleMax] in kScaleStep steps
bool scale_is_native(float s);

// ---- the frame limiter (presentation only: the logic stays on its fixed 60 Hz accumulator) ----------
struct FrameLimiter {
    std::uint64_t next_ns = 0;
    // The time to wait (ns) before the frame that ends at `now_ns` may be presented under `cap` fps;
    // 0 = no cap / already late. A deadline schedule: a late frame re-bases instead of bursting.
    std::uint64_t wait(std::uint64_t now_ns, int cap);
};

// ---- the frame statistics (the FPS display and the frame-time graph) ---------------------------------
// Retail keeps an FPS and a TPS counter refreshed every 1000 ms (0x767c34 / 0x767c3c).
struct FrameStats {
    static constexpr int kHistory = 120;    // [H] frame times kept for the graph (2 s at 60 fps)
    float ms[kHistory] = {};
    int head = 0, count = 0;
    std::uint64_t window_start_ns = 0;
    int frames = 0, ticks = 0;              // this window
    int fps = 0, tps = 0;                   // the last full window
    void frame(std::uint64_t now_ns, float frame_ms, int ticks_this_frame);
    float avg_ms() const;
    float max_ms() const;
    float at(int k) const { return ms[(head - count + k + kHistory * 2) % kHistory]; }   // oldest first
};

// The ShowFPS text (FUN_00429530): mode 1 "FPS: %d", mode 2 "FPS: %d/%d %s" (fps, tps, then
// FUN_005d4820's back-buffer / depth formats, e.g. "32(X888)(D24X8)"); "" for mode 0.
std::string fps_text(int show_fps, int fps, int tps, const std::string& formats);

// ---- display modes ----------------------------------------------------------------------------------
struct Mode { int w = 0, h = 0; float hz = 0.0f; };
// Distinct resolutions, largest first (by width, then height).
std::vector<Mode> resolutions(const std::vector<Mode>& modes);
// The refresh rates (rounded Hz, ascending, distinct) the modes offer at w x h.
std::vector<int> refresh_rates(const std::vector<Mode>& modes, int w, int h);
// The index of the resolution closest to w x h (exact first, else by pixel-count distance); -1 if empty.
int nearest_resolution(const std::vector<Mode>& res, int w, int h);
// A windowed size that fits the usable desktop area [H]: each side clamped, at least 320 x 240.
Extent fit_window(int w, int h, int usable_w, int usable_h);

}  // namespace video
}  // namespace cleria
