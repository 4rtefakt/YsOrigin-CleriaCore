// The enemy mover and integrator — how an AI actor's position actually advances each tick.
// Spec: build/ai_spec/MOVER.md (FUN_00478080 -> FUN_0045f4d0 -> FUN_0045a510), verified in
// the disassembly. The order per tick is: brain -> action player -> mover_pre (locomotion
// section) -> the ASO step -> mover_post (velocity, knockback, facing) -> integrate.
//
// Not modelled: water (+0x38a) clamps and sand/lava surface multipliers for enemies, ice
// (+0x4b9 == 0xb), platform carry, conveyors, the ceiling probe, the ledge recovery after a
// floor-ray miss, the > 3 fall section request, and +0x400 (a player-only counter).
#pragma once
#include <cmath>
#include <cstdint>

#include "cleria/mathx.hpp"
#include "cleria/physics_player.hpp"

namespace cleria {

// The per-actor state the mover and integrator own.
struct MoverState {
    float vx = 0, vz = 0;    // +0x32c / +0x334: the stored velocity
    Vec3 k1{}, k2{};         // knock channel 1 (+0x338..; hits, Dash) and 2 (+0x344..; pushes, brains)
    float dx = 0, dz = 0;    // +0xa3c / +0xa44: this tick's displacement
    int loco = 0;            // +0x3a0: the locomotion index (0 idle, 1 walk, 2 run, 3 air)
};

// The locomotion section table 0x69ba48 {idle 2, walk 0, run 1, air 3}.
inline int loco_section(int idx) {
    static constexpr int kTable[4] = {2, 0, 1, 3};
    return kTable[idx & 3];
}

// FUN_0045f4d0 0x45f822..0x45f938: the locomotion index and, when it changes while the
// pre-step Movable is non-zero and the air section is not current, the GATED request.
// Returns the section to request, or -1. `idx_pre` is the moving index (0 stopped).
inline int mover_pre(MoverState& m, int mode, bool grounded, bool own_jump, bool movable_pre,
                     int section, int* idx_pre_out) {
    const int idx_pre = mode == 2 ? 2 : mode == 1 ? 1 : 0;
    int idx = idx_pre;
    if (!grounded && !own_jump) idx = 3;
    if (idx_pre_out) *idx_pre_out = idx_pre;
    if (movable_pre && m.loco != idx) {
        const bool was_air = section == 3;
        m.loco = idx;
        if (!was_air) return loco_section(idx);
    }
    return -1;
}

// FUN_005eee60(rate, cur, tgt): the mover's facing turn. Unlike rotate_toward (FUN_005eecc0)
// it compares in 1/16384-turn steps, and a stepped result is wrapped AS DISASSEMBLED
// (> pi: -2pi; else < pi: +2pi), so a mid-turn facing is stored in (pi, 3pi] — harmless,
// the next call re-wraps it and the render basis is periodic.
inline float turn_facing(float rate, float cur, float tgt) {
    constexpr float kPi = 3.14159265f, k2Pi = 6.2831853f;
    auto wrap = [&](float x) {                     // FUN_00444660: into [-pi, pi)
        float r = std::fmod(x + kPi, k2Pi);
        if (r < 0) r += k2Pi;
        return r - kPi;
    };
    const float c = wrap(cur), t = wrap(tgt);
    if (rate == 0.0f) return c;
    const int a = int((c + kPi) * 2607.5945f) & 0x3fff;
    const int b = int((t + kPi) * 2607.5945f) & 0x3fff;
    const int d = (a - b) & 0x3fff;
    const int R = int(rate * 2607.5945f);
    float r;
    if (a == 0 && b == 0x2000) r = c + rate;
    else if (0x4000 - d < d) { if (R > 0x4000 - d) return t; r = c + rate; }
    else { if (R > d) return t; r = c - rate; }
    if (r > kPi) r -= k2Pi;
    else if (r < kPi) r += k2Pi;
    return r;
}

// FUN_0045f4d0 0x45fa68..0x46002f, after the ASO step: velocity toward heading x speed x
// Movable (snapped when grounded, blended 0.12 / 0.05 in the air), then the knockback of
// both channels (capped 0.36/tick, damped 1 - 0.95f / 1 - 5f, f = 0.135, x0.75 airborne).
// `movable_pre` is +0x9cc before the step, `movable` after it.
inline void mover_post(MoverState& m, int idx_pre, float speed, float heading, bool grounded,
                       bool flyer, bool movable_pre, float movable, float hp) {
    m.dx = m.dz = 0;
    float f = 0.135f;
    if (!grounded) f *= 0.75f;
    if (!(grounded && !flyer && !movable_pre)) {   // THE GATE: no velocity write
        float k = 1.0f, tx, tz;
        if (idx_pre != 0) {
            const float s = speed * movable;
            tx = std::cos(heading) * s; tz = std::sin(heading) * s;
            if (!grounded) k = 0.12f;
        } else {
            tx = tz = 0;
            if (!grounded) k = 0.05f;
        }
        float vx, vz;
        if (k == 1.0f) { m.vx = tx; m.vz = tz; vx = tx; vz = tz; }
        else {
            vx = m.vx * (1 - k) + tx * k; vz = m.vz * (1 - k) + tz * k;
            m.vx = vx; m.vz = vz;
            if (std::fabs(vx) < 0.005f && std::fabs(vz) < 0.005f) { m.vx = m.vz = 0; vx = vz = 0; }
        }
        m.dx = vx; m.dz = vz;
    }
    // Knockback: enemies (+0x12c & 0x40 clear) sum both channels, y included.
    const Vec3 K{m.k1.x + m.k2.x, m.k1.y + m.k2.y, m.k1.z + m.k2.z};
    const float m2 = K.x * K.x + K.y * K.y + K.z * K.z;
    Vec3 disp = K;
    if (m2 > 0.1296f) {
        const float s = (1.0f / std::sqrt(m2)) * 0.36f;
        disp = {K.x * s, K.y * s, K.z * s};
    }
    const bool kill = m2 < 0.000225f || hp < 1.0f;
    const float d1 = 1 - 0.95f * f, d2 = 1 - 5 * f;
    m.k1 = {m.k1.x * d1, m.k1.y * d1, m.k1.z * d1};
    m.k2 = {m.k2.x * d2, m.k2.y * d2, m.k2.z * d2};
    m.dx += disp.x; m.dz += disp.z;                // the y part is discarded by the integrator
    if (kill) { m.k1 = {}; m.k2 = {}; }
}

// anime_Jump (FUN_00449460), applied by the host when the VM reports it: t = v (the cast's
// +0x408 when v == 0), origin y += t, airborne.
inline void apply_jump(VerticalState& vs, float& origin_y, float v, float cast_jump_v0) {
    const float t = v == 0.0f ? cast_jump_v0 : v;
    vs.t = t;
    origin_y += t;
    vs.grounded = false;
}

// anime_Dash (0x447665): knock channel 1 = (cos, sin)(heading) * d (overwrites x/z).
inline void apply_dash(MoverState& m, float heading, float d) {
    m.k1.x = std::cos(heading) * d;
    m.k1.z = std::sin(heading) * d;
}

}  // namespace cleria
