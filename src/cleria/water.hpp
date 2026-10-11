#pragma once
// The generic actor water test, FUN_0045c490 [EXPORT 0045c490.c:121-196] (build/mech_re/ENEMY_WATER.md §1.2,
// build/physics_re/WATER.md §1): the three bytes +0x389 wading, +0x38a submerged, +0x38b entered-this-tick and
// the surface +0x320, for any actor. It runs in the handler tail, after the brain, the mover and the integrator:
// a brain reads last tick's bytes. The caller casts the ray (up 50 from `probe_y` against collision set 1) and
// owns the presentation (splash, bubbles, ripples); this is the state only.
// [not modelled here] the cooldown bytes +0x4c3..+0x4c6, the ripple counter +0x39b, +0x394.
#include "cleria/physics_player.hpp"

namespace cleria {

struct WaterBytes {
    bool wading = false;        // +0x389: 0.35 < depth < 2.0
    bool submerged = false;     // +0x38a: depth > 1.1
    bool entered = false;       // +0x38b: +0x38a went 0 -> 1 on this tick
    float surface = -1.0e30f;   // +0x320: the set-1 surface above the probe
};

inline constexpr float kWaterSubmerged = 1.1f;    // 0x6bd464
inline constexpr float kWaterWadeLo = 0.35f;      // 0x6bd2b0
inline constexpr float kWaterWadeHi = 2.0f;       // 0x6bd580
inline constexpr float kWaterProbeDrop = 0.25f;   // 0x6bd244: the probe starts this far under the feet
inline constexpr float kWaterProbeSand = 0.7f;    // 0x6bd36c: ... and this much more when +0x4ba == 0x12

// One tick. `mode` = the room's [0x76a544] (0 none, 1 a water set, 2 the whole room under water); `skip` = the
// actor's +0x12c & 0x8000 (M590, M910..M960, M270: no mode-1 probe, the bytes keep their placement value);
// `hit` / `surface_y` = the ray's result; `probe_y` = its start. Returns true when a splash is due: a wading
// change or a surfacing (mode 1 only; the caller applies the 15-tick cooldown).
inline bool water_test(WaterBytes& w, VerticalState& v, int mode, bool skip, bool hit, float surface_y, float probe_y) {
    w.entered = false;
    if (mode == 0) { w.wading = w.submerged = false; return false; }
    if (mode == 2) { w.wading = false; w.submerged = true; return false; }
    if (skip) return false;
    if (!hit) { w.wading = w.submerged = false; return false; }
    w.surface = surface_y;
    const float depth = surface_y - probe_y;
    const bool sub = depth > kWaterSubmerged;
    bool surfaced = false;
    if (sub != w.submerged) {
        w.entered = sub;
        water_transition(v);                // +0x330 capped at 0.1, on entry and on exit
        w.submerged = sub;
        surfaced = !sub;
    }
    const bool wade = depth > kWaterWadeLo && depth < kWaterWadeHi;
    const bool splash = wade != w.wading || surfaced;
    w.wading = wade;
    return splash;
}

}  // namespace cleria
