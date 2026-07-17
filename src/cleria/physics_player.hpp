// The player's vertical motion, as the engine actually does it.
//
// ** There is no gravity constant to port. ** CleriaCore used `kGravity = 30.0f` with
// `v += g*dt`, and that model has no counterpart in the engine at all. The engine
// never integrates acceleration into a velocity. It keeps a signed scalar, decrements
// it by a fixed amount per tick, and computes the Y displacement in CLOSED FORM:
//
//     dy = 0.5f * 9.81f * v * fabsf(v);   // note v*|v|, not v*v — the sign carries
//     dy = fmaxf(dy, -0.65f);             // terminal fall, clamp at 0x006be020
//     pos.y += dy;
//     pos.y = fmaxf(pos.y, actor->f32[0x318]);   // floor
//     v -= 0.0168f;
//     if (v < -0.5f) v = -0.5f;
//
// The 9.81 is at 0x006bd924, raw bytes `c3 f5 1c 41`, and has exactly TWO xrefs in the
// whole 2.6 MB .text — one of which is the actor tick. It is not a coincidence-shaped
// number that happened to match; it is the gravity.
//
// ## +0x330 is a TIME, not a velocity [H-high]
//
// This is the part that makes the model make sense. `0.5*g*v*|v|` would be
// dimensionally wrong for a velocity, and right for a time: it is s = ½gt², with the
// sign of t carried through so the same expression covers rise and fall. Supporting
// evidence, all consistent:
//
//   - the per-tick decrement 0.0168 ≈ 1/60 — i.e. one tick of SECONDS at 60 Hz
//   - the player's jump v0 is 0.285, and the apex lands at tick 17 ≈ 0.283 s —
//     apex at exactly v0 seconds, which is what a signed timer predicts
//   - the -0.5 clamp is reached after exactly 0.5 s
//
// ** The engine's math is non-physical but tuned. Reproduce it literally; do not
// "fix" it into a real integrator. ** A simulated player jump gives an apex of 2.46
// units at tick 17 and an airtime of 35 ticks (0.583 s), which is sane — a misread
// model would have produced nonsense, so this doubles as a transcription check.
//
// ## The player SNAPS to speed — there is no acceleration ramp [C-DIS]
//
// actor+0x310 is a stateless per-tick product; there is no friction model to write.
// The `x = x*0.95 + 0.015` exponential filters in this area are a trap: they act on
// the VERTICAL field and are per-character AIR abilities (glide/float), gated on the
// character id at 0x76bb74. They are not walk-speed smoothing.
#pragma once
#include <cmath>

namespace cleria {

// Per-cast shape and motion, from CASTINFO (see cast.hpp). The two derivations are
// the engine's own — it does not store the final radius/height.
struct CastMotion {
    float radius_base = 0;  // CASTINFO +0x34
    float height_base = 0;  // CASTINFO +0x38
    float speed_base = 0;   // CASTINFO +0x48
    float jump_v0 = 0;      // CASTINFO +0x50 -> actor+0x408

    float radius() const { return radius_base * 0.9f; }
    float height() const { return height_base + 0.075f; }
};

struct CastTable;
// Read a cast's motion fields. cast_id 13 is CAST_ADOL — the player.
CastMotion cast_motion(const CastTable& ct, int cast_id);

// The vertical state. `t` is the signed timer at actor+0x330 — seconds, not m/s.
struct VerticalState {
    float t = 0;
};

inline constexpr float kGravity = 9.81f;        // 0x006bd924
inline constexpr float kTerminalFall = -0.65f;  // 0x006be020, per TICK
inline constexpr float kTimerStep = 0.0168f;    // ~1/60 — one tick of seconds
inline constexpr float kTimerFloor = -0.5f;

// One tick of vertical motion. `floor_y` is actor+0x318. Returns the new y.
inline float step_vertical(VerticalState& s, float y, float floor_y) {
    float dy = 0.5f * kGravity * s.t * std::fabs(s.t);  // v*|v| — sign carries
    dy = std::fmax(dy, kTerminalFall);
    y += dy;
    y = std::fmax(y, floor_y);
    s.t -= kTimerStep;
    if (s.t < kTimerFloor) s.t = kTimerFloor;
    return y;
}

inline void begin_jump(VerticalState& s, const CastMotion& m) { s.t = m.jump_v0; }

// ---------------------------------------------------------------------------
// NOT HERE: walk and run speed. But it is now SOURCED (build/player_re/PLAYER.md).
//
// The player's horizontal speed is a "stateless per-tick product" (confirmed):
//   +0x310 = CASTINFO+0x48 · stickFactor · rate,  then × CASTINFO+0x4c · modeConst
// CASTINFO+0x48 = 0.056 (the player cast's base, our cast.hpp reads it into speed_base;
// loads to actor+0x30c). `rate` = 10.0 (0x6bd928) in lock-on/strafe, else the walk
// clip's play-rate. `modeConst ∈ {0.6, 0.7, 0.8, … 1.6}` by control-mode + run flag.
// Velocity +0x32c/+0x334 = sin/cos(facing)·+0x310, integrated by FUN_0045a510. Grounded
// input SNAPS to speed (no ramp — confirms the note below); airborne is a 0.12 air-
// control lerp; no input decays at 0.05 to a 0.005 deadzone. Movement is camera-relative
// (a single world-direction angle is passed to the mover, camera-yaw folded upstream).
//
// The two CASTINFO scalars ARE now read from data: +0x48 = 0.056 and +0x4c = 2.2 for
// the player cast. ** Still [H-medium] for the ABSOLUTE magnitude, though: ** the exact
// units/tick multiplies those by `rate` (10.0 in strafe, or the walk clip's play-rate in
// free locomotion) and the run/mode const (0.6..1.6) — neither pinned to a concrete
// gameplay state. So cleria-view keeps its hand-tuned kWalkSpeed rather than ship a
// product of two unpinned factors; the sourced pieces are recorded here for when they
// close. (Jump v0 IS clean: CASTINFO+0x50 = 0.285, and cast_motion already uses it.)
//
// ** The old `X at +0xdc` was an offset-collision misread: +0xdc/+0xe0/+0xe4 is the
// world-space centre of the actor's ATTACK hit-volume ** (FUN_004fed50), not a speed
// factor. [H-low], superseded — recorded so it is not re-derived.
//
// ## [RESOLVED] the vertical-model discrepancy — this model is CORRECT for all actors
//
// A prior pass (AITICK.md) decoded FUN_0045a510 and read +0x330 as a VELOCITY
// (pos.y += v; v -= 0.0168; clamp -0.5), contradicting the ½gt² timer model above.
// build/player_re/PLAYER.md RESOLVES it in THIS model's favour [C-DIS]: there is ONE
// vertical path (FUN_0045a510, called by the player handler at 0x0046fd56), and it
// contains BOTH the quadratic displacement `0.5·9.81·t·|t|` (gated on airborne
// +0x388==0 && +0x398==0) AND the timer update `+0x330 -= 0.0168/0.0118, clamp -0.5`.
// AITICK saw only the decrement+clamp and INFERRED the linear form — it never traced the
// displacement term, which is quadratic. So the model above is confirmed, AITICK's
// linear claim is retracted, and ENEMIES share it (same FUN_0045a510). Authoritative
// constants (read from memory, not Ghidra annotations): 9.81 @0x6bd924, 0.5 @0x6bd304,
// 0.0118 @0x6bd098, 0.0168 @0x6bd0d8, -0.5 @0x6be014.
// ---------------------------------------------------------------------------

}  // namespace cleria
