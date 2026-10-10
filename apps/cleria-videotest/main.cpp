// cleria-videotest — the Video page's pure part (src/cleria/video.hpp, build/shell_re/VIDEO.md): retail's
// gamma ramp (FUN_005d4de0), sampler table (FUN_005d4f00 / 0x6b754c), projection aspect (FUN_005e2670)
// and FPS text (FUN_00429530); cleria.ini [cleria.video] (defaults, the BorderlessFullscreen migration,
// clamping, round trip); the render-scale extent, the frame limiter, the frame statistics and the
// display-mode lists. Usage: cleria-videotest [ignored]
#include <cmath>
#include <cstdio>
#include <vector>

#include "cleria/settings.hpp"
#include "cleria/video.hpp"

using namespace cleria;

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("[video] %-80s %s\n", what, ok ? "OK" : "FAIL");
    fails += !ok;
}

int main() {
    // ---- gamma (FUN_005d4de0) ----
    {
        bool ident = true;
        for (int i = 0; i < 256; ++i) ident &= video::gamma_ramp(1.0f, i) == std::uint16_t(int(float(i) / 255.0f * 65535.0f));
        check(ident && video::gamma_ramp(1.0f, 255) == 65535 && video::gamma_ramp(1.0f, 0) == 0,
              "gamma 1.0: the identity ramp (i/255 x 65535)");
        check(video::gamma_is_identity(1.0f) && !video::gamma_is_identity(1.2f) && !video::gamma_is_identity(0.5f),
              "gamma 1.0 is the identity (no pass); 0.5 / 1.2 are not");
        check(video::gamma_ramp(2.0f, 128) > video::gamma_ramp(1.0f, 128) &&
              video::gamma_ramp(0.5f, 128) < video::gamma_ramp(1.0f, 128),
              "gamma > 1 brightens the mid-tones, < 1 darkens them (out = in^(1/g))");
        // 128/255 ^ (1/2) = 0.708484 -> 46430 (truncated)
        check(video::gamma_ramp(2.0f, 128) == std::uint16_t(int(float(std::pow(double(128 * 0.003921568859368563f), 0.5)) * 65535.0f)),
              "gamma 2.0, i = 128: pow(i * 0x6bd038, 1/g) x 65535, truncated");
        check(video::gamma_exponent(0.1f) == 1.0f / 0.25f && video::gamma_exponent(9.0f) == 1.0f / 5.0f &&
              video::gamma_exponent(1.5f) == 1.0f / 1.5f,
              "the clamp: g < 0.25 -> 0.25, g > 5 -> 5 (comiss 0x6bd244 / minss 0x6bd868)");
    }
    // ---- sampler (FUN_005d4f00, table 0x6b754c) ----
    {
        const video::SamplerSpec p = video::sampler_spec(0, 1), b = video::sampler_spec(1, 1), t = video::sampler_spec(2, 1);
        check(!p.linear && !p.mip_linear && b.linear && !b.mip_linear && t.linear && t.mip_linear,
              "TextureFilter 0/1/2: point / bilinear (mip point) / trilinear");
        check(b.anisotropy == 1 && video::sampler_spec(1, 8).anisotropy == 8 && video::sampler_spec(1, 8).mip_linear,
              "Anisotropy 8 on bilinear: 8x, every filter linear (wgpu)");
        check(video::sampler_spec(0, 16).anisotropy == 1 && !video::sampler_spec(0, 16).linear,
              "Anisotropy on point filtering: none (ANISOTROPIC replaces only a LINEAR min filter)");
        check(video::sampler_spec(2, 6).anisotropy == 4 && video::sampler_spec(2, 99).anisotropy == 16 &&
              video::sampler_spec(1, 0).anisotropy == 1,
              "Anisotropy: the power of two at or below, capped at 16; 0 reads as 1");
        const video::SamplerSpec d = video::sampler_spec(Settings{}.graphics.texture_filter, Settings{}.graphics.anisotropy);
        check(d.linear && !d.mip_linear && d.anisotropy == 1,
              "the defaults (TextureFilter 1, Anisotropy 1) = CleriaCore's existing bilinear sampler");
    }
    // ---- anti-aliasing ([graphics] MultiSampleType) ----
    check(video::msaa_samples(Settings{}.graphics.multi_sample_type) == 1 && video::msaa_samples(0) == 1 &&
          video::msaa_samples(1) == 1 && video::msaa_samples(2) == 4 && video::msaa_samples(4) == 4 &&
          video::msaa_samples(16) == 4,
          "MultiSampleType: the default and 0 / 1 draw single-sampled; every count >= 2 draws with 4 samples");
    // ---- aspect (FUN_005e2670) ----
    check(std::fabs(video::projection_aspect(1280, 800, false) - 1.6f) < 1e-6f &&
          video::projection_aspect(1280, 800, true) == 1.3333334f && video::projection_aspect(3440, 1440, true) == 1.3333334f,
          "projection aspect: W/H (Hor+), or 4:3 under StretchAspect");
    // ---- FPS text (FUN_00429530) ----
    check(video::fps_text(0, 60, 60, "x").empty() && video::fps_text(1, 59, 60, "x") == "FPS: 59" &&
          video::fps_text(2, 59, 60, "32(X888)(D24X8)") == "FPS: 59/60 32(X888)(D24X8)",
          "ShowFPS 0 / 1 / 2: none, \"FPS: %d\", \"FPS: %d/%d %s\"");
    // ---- [cleria.video] ----
    {
        Settings s;
        check(s.video.window_mode == video::Windowed && s.video.render_scale == 1.0f && s.video.fps_cap == 0 &&
              !s.video.perf_graph,
              "defaults: windowed, native render scale, no cap, no graph");
        Ini a; a.parse("[cleria.video]\r\nBorderlessFullscreen=1\r\n");
        Settings t; read_cleria(a, t);
        check(t.video.window_mode == video::Borderless, "an older cleria.ini: BorderlessFullscreen=1 -> borderless");
        Ini b; b.parse("[cleria.video]\r\nBorderlessFullscreen=1\r\nWindowMode=2\r\n");
        Settings u; read_cleria(b, u);
        check(u.video.window_mode == video::Exclusive, "WindowMode wins over the old key");
        Ini c; c.parse("[cleria.video]\r\nWindowMode=7\r\nRenderScale=9\r\nFpsCap=3\r\n");
        Settings v; read_cleria(c, v);
        check(v.video.window_mode == 2 && v.video.render_scale == video::kScaleMax && v.video.fps_cap == video::kFpsCapMin,
              "out-of-range values clamp (mode 2, scale 2.0, cap 15)");
        Ini d; d.parse("[cleria.video]\r\nRenderScale=0.6\r\nFpsCap=-5\r\n");
        Settings w; read_cleria(d, w);
        check(w.video.render_scale == 0.5f && w.video.fps_cap == 0, "scale snaps to 0.25 steps; a negative cap = unlimited");
        Settings x;
        x.video.window_mode = video::Exclusive; x.video.render_scale = 1.5f; x.video.fps_cap = 144; x.video.perf_graph = true;
        Ini out; write_cleria(out, x);
        Settings y; read_cleria(out, y);
        check(y.video.window_mode == 2 && y.video.render_scale == 1.5f && y.video.fps_cap == 144 && y.video.perf_graph &&
              out.has("cleria.video", "WindowMode") && out.has("cleria.video", "RenderScale") &&
              out.has("cleria.video", "FpsCap") && out.has("cleria.video", "PerfGraph"),
              "round trip: WindowMode / RenderScale / FpsCap / PerfGraph");
        {   // the looks switches (CleriaCore's own): off by default, off in an ini without them, round trip
            Settings z; z.video.smooth_textures = z.video.smooth_edges = z.video.crisp_sprites = true;
            Ini o2; write_cleria(o2, z);
            Settings z2; read_cleria(o2, z2);
            check(!s.video.smooth_textures && !s.video.smooth_edges && !s.video.crisp_sprites &&
                  !t.video.smooth_textures && !t.video.smooth_edges && !t.video.crisp_sprites &&
                  !y.video.smooth_textures && z2.video.smooth_textures && z2.video.smooth_edges && z2.video.crisp_sprites,
                  "SmoothTextures / SmoothEdges / CrispSprites: default off, round trip");
        }
        check(video::retail_windowed(video::Windowed) == 1 && video::retail_windowed(video::Borderless) == 0 &&
              video::retail_windowed(video::Exclusive) == 0,
              "retail Windowed mirror: windowed 1, borderless / exclusive 0");
    }
    // ---- render scale ----
    {
        const video::Extent e = video::scaled_extent(1280, 800, 0.5f, 8192);
        const video::Extent f = video::scaled_extent(1280, 800, 2.0f, 8192);
        const video::Extent g = video::scaled_extent(5000, 3000, 2.0f, 8192);
        check(e.w == 640 && e.h == 400 && f.w == 2560 && f.h == 1600 && g.w == 8192 && g.h == 6000,
              "scaled extent: 0.5x / 2x, clamped to the device's max texture side");
        check(video::clamp_scale(0.1f) == 0.5f && video::clamp_scale(1.1f) == 1.0f && video::clamp_scale(1.4f) == 1.5f &&
              video::scale_is_native(1.0f) && !video::scale_is_native(1.25f),
              "scale clamp / steps / native test");
    }
    // ---- frame limiter ----
    {
        video::FrameLimiter L;
        const std::uint64_t ms = 1000000ull;
        bool ok = L.wait(0, 0) == 0;                            // no cap
        ok &= L.wait(100 * ms, 50) == 0;                        // first frame: re-base, due at 120 ms
        ok &= L.wait(105 * ms, 50) == 15 * ms;                  // early: wait to 120
        ok &= L.wait(121 * ms, 50) == 19 * ms;                  // next due 140
        ok &= L.wait(400 * ms, 50) == 0 && L.next_ns == 420 * ms;   // far behind: re-base, no burst
        check(ok, "frame limiter: deadline schedule at the cap, re-based when late");
        int frames = 0;                                         // a 1 ms frame loop under a 60 fps cap
        std::uint64_t t = 0;
        video::FrameLimiter M;
        while (t < 1000 * ms) { t += ms; t += M.wait(t, 60); ++frames; }
        check(frames >= 59 && frames <= 61, "frame limiter: a fast loop presents ~60 frames a second at cap 60");
    }
    // ---- frame statistics ----
    {
        video::FrameStats st;
        const std::uint64_t ms = 1000000ull;
        std::uint64_t t = 5 * ms;
        st.frame(t, 40.0f, 2);                                  // opens the window
        for (int i = 0; i < 25; ++i) { t += 40 * ms; st.frame(t, 40.0f, 2); }
        check(st.fps == 25 && st.tps == 50, "FPS / TPS counters: 25 frames of 2 ticks in the first 1000 ms window");
        st.frame(t + 50 * ms, 50.0f, 3);
        check(std::fabs(st.max_ms() - 50.0f) < 1e-4f && st.avg_ms() > 40.0f && st.avg_ms() < 41.0f &&
              st.at(st.count - 1) == 50.0f,
              "frame times: max / average / newest last");
    }
    // ---- display modes ----
    {
        const std::vector<video::Mode> modes = {{1920, 1080, 60.0f}, {1920, 1080, 143.998f}, {2560, 1440, 59.95f},
                                                {1280, 720, 60.0f}, {1920, 1080, 59.94f}, {1280, 1024, 75.0f}};
        const auto res = video::resolutions(modes);
        check(res.size() == 4 && res[0].w == 2560 && res[1].w == 1920 && res[2].w == 1280 && res[2].h == 1024 &&
              res[3].h == 720,
              "resolutions: distinct, largest first");
        const auto hz = video::refresh_rates(modes, 1920, 1080);
        check(hz.size() == 2 && hz[0] == 60 && hz[1] == 144, "refresh rates at 1920x1080: 60, 144 (59.94 rounds to 60)");
        check(video::nearest_resolution(res, 1920, 1080) == 1 && video::nearest_resolution(res, 800, 600) == 3 &&
              video::nearest_resolution({}, 800, 600) == -1,
              "nearest resolution: exact, else the closest pixel count");
        const video::Extent w = video::fit_window(2560, 1440, 2560, 1400);
        const video::Extent s = video::fit_window(100, 100, 1920, 1040);
        check(w.w == 2560 && w.h == 1400 && s.w == 320 && s.h == 240, "fit window: inside the usable area, at least 320x240");
    }
    std::printf("%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
