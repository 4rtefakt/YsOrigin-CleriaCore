// The player's locomotion and vertical motion, as the engine does them.
//
// Re-derived 2026-10-05 instruction by instruction (build/audit/core_gameplay.md, then
// the integrator FUN_0045a510 and the player mover FUN_00465170 re-read with
// tools/exe_peek.py + tools/annotate_consts.py). Pure functions — tested by
// apps/cleria-combattest's sibling apps/cleria-movetest.
//
// ## The origin is NOT the feet
//
// A grounded actor's origin rests at `floor + height` (+0x324 = CASTINFO+0x38 + 0.075 =
// 1.075 for the player), not on the floor. The integrator never clamps pos.y to the floor
// on its full path; it tests `pos.y - height < floor + margin` and then LERPS pos.y toward
// `floor + height` by 0.65 per tick. The collision sphere is centred at origin + +0x41c
// (CASTINFO+0x54, -0.25 for the player), so its bottom hovers ~0.4 above the floor — the
// same 0.4 as the falling landing margin. Callers that still think in feet use
// `origin - height`.
//
// ## +0x330 is a signed TIMER, not a velocity
//
// Airborne, the per-tick Y displacement is closed-form `0.5*9.81*t*|t|` (s = ½gt² with the
// sign carried); grounded it is `t` itself. Either way clamped at -0.65/tick. The timer
// drops 0.0168 per tick (0.0118 in water), floors at -0.5, and is reset to 0 on every
// grounded tick. Jumping sets t = CASTINFO+0x50 (0.285) while still grounded, so the
// launch tick moves 0.285 linearly before the quadratic takes over.
#pragma once
#include <cmath>

namespace cleria {

// Per-cast shape and motion, from CASTINFO (see cast.hpp). The derivations are the
// factory's own (FUN_00459410).
struct CastMotion {
    float radius_base = 0;    // CASTINFO +0x34
    float height_base = 0;    // CASTINFO +0x38
    float speed_base = 0;     // CASTINFO +0x48 -> +0x30c, per-tick walk speed (0.056)
    float run_mult = 0;       // CASTINFO +0x4c -> +0x410 (2.2)
    float jump_v0 = 0;        // CASTINFO +0x50 -> +0x408 (0.285)
    float center_offset = 0;  // CASTINFO +0x54 -> +0x41c (-0.25)

    float radius() const { return radius_base * 0.9f; }     // -> +0x328
    float height() const { return height_base + 0.075f; }   // -> +0x324
};

struct CastTable;
// Read a cast's motion fields. cast_id 13 is CAST_ADOL — the player.
CastMotion cast_motion(const CastTable& ct, int cast_id);

// ---------------------------------------------------------------------------
// HORIZONTAL — FUN_00465170 0x468ff5..0x4698be.
//
// speed (+0x310) = walk(+0x30c) * stick * movable(+0x9cc = the ASO's anime_Movable).
// Running (+0x4ca: stick magnitude >= a runtime config threshold, with no walk modifier)
// multiplies by run_mult(2.2) and then:
//   airborne (and not in a skill section 0x109/0x10c..0x111): x0.6
//   grounded: a per-character constant (0x4691cb..0x469260), with/without blessing bit
//   0x76bc80 & 0x2000 — Yunica 1.125/1.225, Hugo 1.0/1.1 (1.075/1.175 while +0x7bc, the
//   Force Shield's life, is non-zero: ABILITIES_VERIFIED.md §3.2), Toal 1.275, Adol 1.175.
// [not modelled] the special table entered when FUN_00460c10()+0x44 is set in sections
// 0x10a/3 (1.35..1.55), and the 10.0 rate of the lock-on branch.
inline float run_char_const(int character, bool blessing_2000, bool hugo_7bc = false) {
    switch (character) {
        case 1: return blessing_2000 ? 1.225f : 1.125f;
        case 2: return hugo_7bc ? (blessing_2000 ? 1.175f : 1.075f) : (blessing_2000 ? 1.1f : 1.0f);
        case 3: return 1.275f;
        default: return 1.175f;
    }
}

inline float player_speed(const CastMotion& m, int character, float stick, float movable,
                          bool running, bool grounded, bool skill_section,
                          bool blessing_2000 = false, bool hugo_7bc = false) {
    float s = m.speed_base * stick * movable;
    if (!running) return s;
    s *= m.run_mult;
    if (!grounded && !skill_section) return s * 0.6f;
    return s * run_char_const(character, blessing_2000, hugo_7bc);
}

// Velocity (+0x32c/+0x334), per tick. `angle` is the world move direction in the
// engine's heading basis: target = (cos, sin) * speed, i.e. angle = atan2(dz, dx).
//   grounded with input: SNAP to the target (no acceleration ramp)
//   airborne with input: lerp toward it at 0.12
//   no input: grounded -> stop; airborne -> decay toward 0 at 0.05
// The lerp paths zero both components once each is under 0.005.
struct PlanarVelocity { float x = 0, z = 0; };

inline void step_planar(PlanarVelocity& v, bool has_input, float angle, float speed,
                        bool grounded) {
    const float tx = has_input ? std::cos(angle) * speed : 0.0f;
    const float tz = has_input ? std::sin(angle) * speed : 0.0f;
    if (grounded) { v.x = tx; v.z = tz; return; }
    const float k = has_input ? 0.12f : 0.05f;
    v.x = v.x * (1.0f - k) + tx * k;
    v.z = v.z * (1.0f - k) + tz * k;
    if (std::fabs(v.x) < 0.005f && std::fabs(v.z) < 0.005f) v = {};
}

// Ground-surface speed multipliers — FUN_0045a510 0x45a6fa..0x45a91b, keyed on the
// latched surface id (+0x4b9, surface.hpp order). Base case (no blessing 0x76bc80&2, no
// accessory 0x76a624 bits): sand2 (0x12) x0.25, lava/blood (0x0d/0x11) x0.6 — both also
// scale a RISING dy — and submerged (+0x38a) x0.8, x0.9 with blessing 0x76bc80 & 2, none
// with the foot-gear bit 0x76a624 & 0x1000000 (build/physics_re/WATER.md §2a [BYTES
// 0x45a81a..0x45a875]). Everything else 1.0.
struct SurfaceScale { float xz = 1.0f, up = 1.0f; };
inline SurfaceScale surface_scale(int surface, bool in_water, bool blessing_2 = false,
                                  bool foot_gear = false) {
    if (surface == 0x12) return {0.25f, 0.25f};
    if (surface == 0x0d || surface == 0x11) return {0.6f, 0.6f};
    if (in_water && !foot_gear) return {blessing_2 ? 0.9f : 0.8f, 1.0f};   // 0x6bd3e0 / 0x6bd3a8
    return {};
}

// ---------------------------------------------------------------------------
// VERTICAL — FUN_0045a510 0x45a5a8..0x45a6f4 (displacement) and 0x45be61..0x45c1ef
// (timer, landing, snap). Not modelled: the +0x398 float flag, the sand2 lift while
// +0x3b8, and the +0x4d0 launch-state x0.985.
struct VerticalState {
    float t = 0;            // +0x330, signed timer (seconds)
    bool grounded = true;   // +0x388
    int coyote = 0;         // +0x3b0, ticks spent off-ground while still counted grounded
    bool jumping = false;   // +0x3a0 == 3
};

inline constexpr float kGravity = 9.81f;        // 0x006bd924
inline constexpr float kTerminalFall = -0.65f;  // 0x006be020, per TICK
inline constexpr float kTimerStep = 0.0168f;    // 0x006bd0d8 (0.0118 in water, 0x006bd098)
inline constexpr float kTimerFloor = -0.5f;     // 0x006be014

// This tick's Y displacement (computed BEFORE the move sweep). Submerged (+0x38a) it is
// clamped to [-0.05, +0.10] — [-0.015, +0.03] on sand2 — instead of the -0.65 terminal:
// the only buoyancy there is (WATER.md §2a [BYTES 0x45a5a8..0x45a6f4]).
inline float vertical_dy(const VerticalState& s, bool submerged = false, bool sand2 = false) {
    const float dy = s.grounded ? s.t : 0.5f * kGravity * s.t * std::fabs(s.t);
    if (submerged) {
        const float lo = sand2 ? -0.015f : -0.05f, hi = sand2 ? 0.03f : 0.10f;   // 0x6bdfb4/0x6bd100, 0x6bdfc8/0x6bd19c
        return std::fmin(std::fmax(dy, lo), hi);
    }
    return std::fmax(dy, kTerminalFall);
}

inline void begin_jump(VerticalState& s, const CastMotion& m) { s.t = m.jump_v0; s.jumping = true; }

// The mover's jump gate while already jumping or airborne (FUN_00465170 0x465388..0x46544a):
// submerged, any press once the timer is <= 0.05 (no count, no cooldown). [not modelled]
// the land double jump (flag 163, t <= 0.15).
inline bool can_rejump(const VerticalState& s, bool submerged) {
    return submerged && (s.jumping || !s.grounded) && s.t <= 0.05f;   // 0x6bd13c
}
// A submerged re-jump (0x468ba7..0x468dc8): the timer restarts at v0 and the origin is
// kicked up by it at once (FUN_0040b2a0), outside the dy clamp. [H] the land jump's own
// kick (WATER.md G4) is not applied here: the grounded launch tick already moves t.
inline void begin_rejump(VerticalState& s, float& y, const CastMotion& m) {
    s.t = m.jump_v0;
    s.jumping = true;
    y += s.t;
}
// FUN_0045c490's transition rule: when +0x38a changes (in or out), t is capped at 0.1.
inline void water_transition(VerticalState& s) { if (s.t > 0.1f) s.t = 0.1f; }   // 0x6bd19c

// After the sweep: advance the timer, then land, keep a coyote tick, or fall.
// `y` is the ORIGIN height, `floor_y` the last floor the down-ray found (+0x318 — it is
// not cleared when the ray misses). Returns true on the tick the actor touches down.
inline bool settle_vertical(VerticalState& s, float& y, float floor_y, float height,
                            bool in_water = false) {
    const float margin = (!in_water && s.t <= 0.0f) ? 0.4f : 0.01f;   // 0x6bd2d4 / 0x6bd084
    s.t -= in_water ? 0.0118f : kTimerStep;
    if (s.t < kTimerFloor) s.t = kTimerFloor;
    if (y - height < floor_y + margin) {
        const bool landed = !s.grounded;
        s.grounded = true;
        s.coyote = 0;
        s.jumping = false;
        s.t = 0.0f;
        y += (floor_y + height - y) * 0.65f;                     // 0x6bd34c — a lerp, not a snap
        return landed;
    }
    if (!s.jumping && s.coyote < 4) { ++s.coyote; return false; }   // 4 ticks of grace
    s.coyote = 0;
    s.grounded = false;
    return false;
}

}  // namespace cleria
