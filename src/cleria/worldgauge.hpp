// The three world-anchored mini gauges 0x76c1cc / 0x76c1e4 / 0x76c1fc (build/mech_re/MINI_GAUGE.md): ONE
// store of three identical records {timer, value, max, anchor} (g_flags words 556..573). Four boss drivers
// write it — Gelaldy (boss03: gauge 0), Khonsclard (boss04: 0 and 1), Dalles (boss07) and Darm (boss08) —
// and the HUD widget tick FUN_0043a540 counts the timers down. Builder A FUN_0043c230 draws each as the
// oxygen bar's atlas cell in red. Pure; the host (apps/cleria-view/view_hud.inl) owns the store and draws.
#pragma once
#include <cstdint>

#include "cleria/mathx.hpp"

namespace cleria {

struct WorldGauge { int timer = 0; float value = 0, max = 0; Vec3 anchor{}; };   // 0x18 bytes each [EXPORT 0043a540.c:22-39]

// FUN_0043a540, widget 0 only, once per sim step while the HUD runs [EXPORT 0043a540.c:15-39]: each live timer
// counts down by one; at 0 the whole record is zeroed.
inline void tick_world_gauges(WorldGauge* g) {
    for (int i = 0; i < 3; ++i)
        if (g[i].timer != 0 && --g[i].timer == 0) g[i] = WorldGauge{};
}

// One gauge's quad in the AUTHORED 1024x768 frame and its STATUS_512 cell (px), FUN_0043c230 0x43cb13..
// [BYTES; constants 0x6bda34 = 40, 0x6bdafc = 100, 0x6bdad0 = 84, 0x6bd948 = 12; uv 0.5625 / 0.046875 /
// 0.1640625 r / 0.0703125 of 512]: `ax, ay` = the anchor projected to the back buffer and taken into the
// authored frame (FUN_005e3fc0: x 1024 / W, y 768 / H). The ratio is NOT clamped. The colour is the HUD
// tint's red only, alpha = (A_hud x (timer & 0xff)) >> 8 [BYTES 0x43cb46..0x43cb81]. False = nothing drawn.
struct WorldGaugeQuad { float x0, y0, x1, y1, sx, sy, sw, sh; std::uint8_t a; };
inline bool world_gauge_quad(const WorldGauge& g, float ax, float ay, std::uint8_t hud_a, WorldGaugeQuad* q) {
    if (g.timer == 0 || g.max == 0.0f) return false;
    const float r = g.value / g.max;
    q->x0 = ax - 40.0f; q->y0 = ay - 100.0f;
    q->x1 = q->x0 + r * 84.0f; q->y1 = q->y0 + 12.0f;
    q->sx = 288.0f; q->sy = 24.0f; q->sw = 84.0f * r; q->sh = 12.0f;
    q->a = std::uint8_t((unsigned(hud_a) * unsigned(g.timer & 0xff)) >> 8);
    return true;
}

}  // namespace cleria
