// The Video page's pure part: [cleria.video], retail's gamma / sampler / aspect / FPS-text rules, the
// render-scale, frame-limiter, frame-statistics and display-mode helpers. See video.hpp and
// build/shell_re/VIDEO.md.
#include "cleria/video.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "cleria/settings.hpp"

namespace cleria::video {

namespace {
constexpr const char* kSec = "cleria.video";
}

void read(const Ini& ini, Settings& s) {
    if (auto v = ini.get_int(kSec, "WindowMode")) s.window_mode = std::clamp(int(*v), 0, 2);
    else if (auto b = ini.get_int(kSec, "BorderlessFullscreen")) s.window_mode = *b ? Borderless : Windowed;
    if (auto v = ini.get_float(kSec, "RenderScale")) s.render_scale = clamp_scale(*v);
    if (auto v = ini.get_int(kSec, "FpsCap")) s.fps_cap = *v <= 0 ? 0 : std::clamp(int(*v), kFpsCapMin, kFpsCapMax);
    if (auto v = ini.get_int(kSec, "PerfGraph")) s.perf_graph = *v != 0;
    if (auto v = ini.get_int(kSec, "SmoothTextures")) s.smooth_textures = *v != 0;
    if (auto v = ini.get_int(kSec, "SmoothEdges")) s.smooth_edges = *v != 0;
    if (auto v = ini.get_int(kSec, "CrispSprites")) s.crisp_sprites = *v != 0;
}

void write(Ini& ini, const Settings& s) {
    ini.set_int(kSec, "WindowMode", s.window_mode);
    ini.set_float(kSec, "RenderScale", s.render_scale);
    ini.set_int(kSec, "FpsCap", s.fps_cap);
    ini.set_int(kSec, "PerfGraph", s.perf_graph ? 1 : 0);
    ini.set_int(kSec, "SmoothTextures", s.smooth_textures ? 1 : 0);
    ini.set_int(kSec, "SmoothEdges", s.smooth_edges ? 1 : 0);
    ini.set_int(kSec, "CrispSprites", s.crisp_sprites ? 1 : 0);
}

// ---- gamma ----------------------------------------------------------------------------------------
float gamma_exponent(float g) {
    float c = g;
    if (!(0.25f <= c)) c = 0.25f;          // comiss 0.25 > g (NaN too) -> 0.25
    else c = std::min(5.0f, c);            // minss 5.0
    return 1.0f / c;
}

std::uint16_t gamma_ramp(float g, int i) {
    const float x = float(i) * 0.003921568859368563f;   // 0x6bd038
    const float y = float(std::pow(double(x), double(gamma_exponent(g))));   // FUN_0041b790 -> pow (double)
    return std::uint16_t(int(y * 65535.0f));             // cvttss2si, movzx ax
}

bool gamma_is_identity(float g) { return std::fabs(gamma_exponent(g) - 1.0f) < 1e-5f; }

// ---- sampler --------------------------------------------------------------------------------------
SamplerSpec sampler_spec(int filter, int aniso) {
    SamplerSpec s;
    const int f = std::clamp(filter, 0, 2);
    s.linear = f >= 1;                     // table 0x6b754c: min / mag
    s.mip_linear = f == 2;                 // mip
    if (s.linear && aniso > 1) {           // ANISOTROPIC only replaces a LINEAR min filter
        int a = 1;
        while (a * 2 <= std::min(aniso, 16)) a *= 2;   // the dialog offers 2, 4, 8, 16 [RES combo 1043]
        s.anisotropy = std::uint16_t(a);
        s.mip_linear = true;               // wgpu: anisotropy needs every filter linear
    }
    return s;
}

// ---- aspect ---------------------------------------------------------------------------------------
float projection_aspect(std::uint32_t w, std::uint32_t h, bool stretch) {
    if (stretch || h == 0) return 1.3333334f;
    return float(w) / float(h);
}

// ---- render scale ---------------------------------------------------------------------------------
float clamp_scale(float s) {
    if (!(s > 0.0f)) return 1.0f;
    const float c = std::clamp(s, kScaleMin, kScaleMax);
    return std::round(c / kScaleStep) * kScaleStep;
}

bool scale_is_native(float s) { return std::fabs(s - 1.0f) < 1e-4f; }

Extent scaled_extent(std::uint32_t w, std::uint32_t h, float scale, std::uint32_t max_dim) {
    auto side = [&](std::uint32_t v) {
        const double r = std::round(double(v) * double(scale));
        return std::uint32_t(std::clamp(r, 1.0, double(std::max<std::uint32_t>(max_dim, 1))));
    };
    return {side(w), side(h)};
}

// ---- frame limiter ----------------------------------------------------------------------------------
std::uint64_t FrameLimiter::wait(std::uint64_t now, int cap) {
    if (cap <= 0) { next_ns = 0; return 0; }
    const std::uint64_t period = 1000000000ull / std::uint64_t(cap);
    if (next_ns == 0 || now >= next_ns + period) {   // first frame, or more than a frame late: re-base
        next_ns = now + period;
        return 0;
    }
    const std::uint64_t due = next_ns;
    next_ns += period;
    return due > now ? due - now : 0;
}

// ---- frame statistics -------------------------------------------------------------------------------
void FrameStats::frame(std::uint64_t now, float frame_ms, int t) {
    ms[head] = frame_ms;
    head = (head + 1) % kHistory;
    count = std::min(count + 1, kHistory);
    if (window_start_ns == 0) { window_start_ns = now; return; }   // the first frame only opens the window
    ++frames;
    ticks += t;
    if (now - window_start_ns >= 1000000000ull) {        // retail refreshes both every 1000 ms
        fps = frames; tps = ticks;
        frames = 0; ticks = 0;
        window_start_ns = now;
    }
}

float FrameStats::avg_ms() const {
    if (count == 0) return 0.0f;
    double s = 0;
    for (int k = 0; k < count; ++k) s += at(k);
    return float(s / count);
}

float FrameStats::max_ms() const {
    float m = 0.0f;
    for (int k = 0; k < count; ++k) m = std::max(m, at(k));
    return m;
}

std::string fps_text(int mode, int fps, int tps, const std::string& formats) {
    char buf[160];
    if (mode == 1) std::snprintf(buf, sizeof buf, "FPS: %d", fps);
    else if (mode == 2) std::snprintf(buf, sizeof buf, "FPS: %d/%d %s", fps, tps, formats.c_str());
    else return {};
    return buf;
}

// ---- display modes ----------------------------------------------------------------------------------
std::vector<Mode> resolutions(const std::vector<Mode>& modes) {
    std::vector<Mode> out;
    for (const Mode& m : modes) {
        if (m.w <= 0 || m.h <= 0) continue;
        bool have = false;
        for (const Mode& o : out) have |= o.w == m.w && o.h == m.h;
        if (!have) out.push_back({m.w, m.h, 0.0f});
    }
    std::sort(out.begin(), out.end(), [](const Mode& a, const Mode& b) {
        return a.w != b.w ? a.w > b.w : a.h > b.h;
    });
    return out;
}

std::vector<int> refresh_rates(const std::vector<Mode>& modes, int w, int h) {
    std::vector<int> out;
    for (const Mode& m : modes) {
        if (m.w != w || m.h != h || m.hz <= 0.0f) continue;
        const int hz = int(std::lround(m.hz));
        if (std::find(out.begin(), out.end(), hz) == out.end()) out.push_back(hz);
    }
    std::sort(out.begin(), out.end());
    return out;
}

int nearest_resolution(const std::vector<Mode>& res, int w, int h) {
    int best = -1;
    long long best_d = 0;
    for (int i = 0; i < int(res.size()); ++i) {
        if (res[std::size_t(i)].w == w && res[std::size_t(i)].h == h) return i;
        const long long d = std::llabs(1ll * res[std::size_t(i)].w * res[std::size_t(i)].h - 1ll * w * h) +
                            std::llabs(1ll * res[std::size_t(i)].w - w) + std::llabs(1ll * res[std::size_t(i)].h - h);
        if (best < 0 || d < best_d) { best = i; best_d = d; }
    }
    return best;
}

Extent fit_window(int w, int h, int uw, int uh) {
    const int cw = std::max(320, uw > 0 ? std::min(w, uw) : w);
    const int chh = std::max(240, uh > 0 ? std::min(h, uh) : h);
    return {std::uint32_t(cw), std::uint32_t(chh)};
}

}  // namespace cleria::video
