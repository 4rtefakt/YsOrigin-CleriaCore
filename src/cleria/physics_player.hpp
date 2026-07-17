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
// NOT HERE: walk and run speed. And the old `+0x30c * scale * X` note was WRONG.
//
// build/aitick_re/AITICK.md [C-DIS] supersedes the earlier hypothesis. ** Horizontal
// locomotion is ANIMATION-DRIVEN, not a speed field. ** The mover (FUN_0045a510, the
// anim update, run first in every handler) builds each frame's displacement from the
// current clip's ROOT MOTION — the clip's bone matrix diffed frame-to-frame into
// +0x5b0..+0x5b8 — times one of a small set of movement-mode constants
// {0.25, 0.6, 0.65, 0.7, 0.8, 0.9, 1.6} chosen by the mode byte +0x4b9, plus a scripted
// push (+0x5d0) and a persisted delta (+0xa3c). There is no scalar walk-speed on the
// actor. So a faithful mover needs animation ROOT-MOTION EXTRACTION, which we do not yet
// have — cleria-view keeps its hand-tuned speed until that is built.
//
// ** The old `X at +0xdc` was an offset-collision misread: +0xdc/+0xe0/+0xe4 is the
// world-space centre of the actor's ATTACK hit-volume ** (written by FUN_004fed50), not
// a speed factor. That whole formula is [H-low], superseded — recorded so it is not
// re-derived.
//
// ## [DISCREPANCY, unresolved] this vertical model vs the generic-actor one
//
// The model ABOVE (dy = 0.5·9.81·t·|t|, +0x330 as a signed TIMER) is the PLAYER's, and
// it is well-evidenced (apex timing, the 9.81's two xrefs). But AITICK.md decoded
// FUN_0045a510 — the anim update EVERY actor runs — and found +0x330 integrated as a
// VELOCITY: `pos.y += (+0x330); +0x330 -= 0.0168 (or 0.0118 grounded); clamp -0.5`, a
// LINEAR integration, no 9.81. Both agree on the +0x330 decrement (0.0168) and the -0.5
// clamp; they DISAGREE on the displacement (our ½gt² vs its pos+=v).
//
// Most likely the player and generic actors use DIFFERENT vertical integration (our
// header is player-specific; the enemy tick goes through FUN_0045a510). But it is NOT
// resolved which function moves the PLAYER vertically, so this is a real open question,
// not a reconciled one. Do not change the validated player model on the strength of the
// cross-function read; do not assume enemies share it either. Resolve by finding the
// player's own vertical writer before wiring enemy gravity.
// ---------------------------------------------------------------------------

}  // namespace cleria
