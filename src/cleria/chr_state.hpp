// The cutscene actor ("Chr_") state the XSO ops write and the actor tick consumes: the facing
// model (look modes and the rate-limited turn), the sprite pattern pin and the tremble.
// Spec: build/event_re/CHR_OPS.md §4, §6, §7. Pure; the world (NPCs) and the view (the player)
// both run it.
//
// ## Two heading bases
// The engine's heading (+0x308 / +0x314 / +0x360) is `atan2(dz, dx)`: 0 = +X. CleriaCore's
// `Actor::yaw` is `atan2(dx, dz)`: 0 = +Z. They are mirror images, `yaw = pi/2 - heading`
// (the same identity view_game.inl uses for enemies). Everything here works in the ENGINE
// basis so FUN_005eee60 is reproduced bit-for-bit, including its tie-break direction.
#pragma once
#include <cmath>
#include <cstdint>

#include "cleria/enemy_mover.hpp"   // turn_facing = FUN_005eee60

namespace cleria {

inline float heading_of_yaw(float yaw) { return 1.5707963f - yaw; }
inline float yaw_of_heading(float h) { return 1.5707963f - h; }

// FUN_00443a80(int deg) -> engine heading in radians [BYTES 0x443a80..0x443b45]. The Chr ops
// truncate their float operand first (cvttss2si at 0x5708e2 Chr_Dir / 0x570892 Chr_LookAtDir).
//   v = 90 - deg, wrapped into [0, 360) (a negative v: 360 - (-v % 360)); v >= 180 -> v - 360;
//   result = v * 0.017453292 (0x6bd0dc).
// deg == -1 or 360 is the "random" path: one xorshift128 draw (the game RNG 0x74622c..38
// advances), then 45 * (((r >> 17) >> 16) % 7) degrees -- but the two shifts leave no bits,
// so the result is ALWAYS 0 radians (heading +X) [BYTES 0x443b2a..0x443b32]. `rng` may be null
// (no draw: headless tools).
inline float falcom_heading(int deg, std::uint32_t* rng = nullptr) {
    if (deg == -1 || deg == 360) {
        if (rng) {
            const std::uint32_t t = rng[0] ^ (rng[0] << 11);
            rng[0] = rng[1]; rng[1] = rng[2]; rng[2] = rng[3];
            rng[3] = rng[3] ^ (rng[3] >> 19) ^ t ^ (t >> 8);
        }
        return 0.0f;
    }
    int v = 90 - deg;
    if (v < 0) v = 360 - ((-v) % 360);
    else v = v % 360;
    if (v >= 180) v -= 360;
    return float(v) * 0.01745329238f;
}

// The look block of a GoActor [EXPORT FUN_005708c0 / 00570960 / 005707b0 / 00570820 /
// 00570870 / 005714d0; BYTES mover 0x46002f..0x46011a].
struct LookState {
    float cmd = 0.0f;        // +0x308: the commanded heading (look mode 0)
    std::uint8_t mode = 0;   // +0x39a: 0 cmd, 1 track actor +0x370, 2 track point, 3 fixed +0x360
    int target = 0;          // +0x370, held as an instance id (0 = none)
    float px = 0.0f, pz = 0.0f;   // +0x350 / +0x354: the tracked point (mode 1 copies the target's)
    float dir = 0.0f;        // +0x360: the fixed heading (mode 3)
    float mult = 1.0f;       // +0x36c: the turn-rate multiplier (factory 1.0, 0x4596d6)
};

// The ops' writes. `target_xz` is the resolved target's position (null = not found).
inline void look_dir_cmd(LookState& s, float heading) { s.cmd = heading; s.mode = 0; }   // 0x133 / 0x134
inline void look_at_actor(LookState& s, int target_id, bool found) {                     // 0x136
    if (target_id == 0) { s.mode = 0; s.target = 0; return; }   // the release
    if (found) { s.mode = 1; s.target = target_id; }            // a missing target changes nothing
}
inline void look_at_point(LookState& s, float x, float z) {                               // 0x137
    s.mode = 2; s.target = 0; s.px = x; s.pz = z;
}
inline void look_fixed_dir(LookState& s, float heading) {                                 // 0x138
    s.mode = 3; s.target = 0; s.dir = heading;
}

// The heading the facing turns toward this tick (0x46002f..0x4600dc). Mode 1 first copies the
// target's position into the point (+0x350/+0x354); a target that is gone keeps the last copy
// [H: the engine keeps a stale pointer; the port keeps the last position]. Engine atan2 order
// (dz, dx) through the x87 CRT _CIatan2, rounded to float.
// [not modelled] mode 0's camera-yaw subtraction for the player outside a Fix and for actors
// with GoBase +0x60 & kind bit (0x4600bd..0x4600d4; CHR_OPS.md §13 item 4).
inline float look_goal(LookState& s, float x, float z, const float* target_xz) {
    if (s.mode == 0) return s.cmd;
    if (s.mode == 3) return s.dir;
    if (s.mode == 1 && target_xz) { s.px = target_xz[0]; s.pz = target_xz[1]; }
    return float(std::atan2(double(s.pz) - double(z), double(s.px) - double(x)));
}

// The turn rate. NPC mover 0x4600ed..0x46010e: +0x36c x (running ? 0.35 : 0.18), running =
// moving (+0x514) with the walk bit (+0x510 & 0x20) clear. The player (0x46a144..0x46a15f):
// 0.35 while +0x4ca (the run stage) else 0.18, and NO +0x36c factor.
inline float npc_turn_rate(const LookState& s, bool running) { return s.mult * (running ? 0.35f : 0.18f); }
inline float player_turn_rate(bool running) { return running ? 0.35f : 0.18f; }

// One tick of the facing: `if (+0x314 != goal) +0x314 = FUN_005eee60(rate, +0x314, goal)`.
inline float look_turn(float cur, float goal, float rate) {
    return cur == goal ? cur : turn_facing(rate, cur, goal);
}

// Chr_Tremble's block: AniSprite +0x138..+0x148 = actor +0xa28..+0xa38 [EXPORT FUN_00571ce0;
// BYTES 0x446500 (the ASO op's twin), 0x447306..0x447344 (tick), 0x4463be..0x4463d2 (draw)].
struct Tremble {
    int ticks = 0;        // +0x138: countdown, -1 = forever
    int cnt = 0;          // +0x13c: the square-wave counter
    int period = 0;       // +0x140: the half-period (0 = off)
    float dx = 0.0f;      // +0x144: AIA pixels, right-positive
    float dy = 0.0f;      // +0x148: AIA pixels, added to the quad's TOP (up-positive)
};
inline void tremble_set(Tremble& t, int ticks, int period, float dx, float dy) {
    t.ticks = ticks;
    t.period = ticks ? period : 0;
    t.dx = ticks ? dx : 0.0f;
    t.dy = ticks ? dy : 0.0f;
    t.cnt = t.period * 2;
}
// The anim tick's head, which runs even under a Chr_Pattern pin.
inline void tremble_step(Tremble& t) {
    if (t.ticks != 0) {
        if (t.ticks != -1) --t.ticks;
        if (t.ticks == 0) { t.dx = 0.0f; t.dy = 0.0f; }
    }
    if (t.period != 0) {
        const int c = t.cnt == 0 ? 2 * t.period : t.cnt;
        t.cnt = c - 1;
    }
}
// The draw offset is applied while `period != 0 && cnt >= period` (an unsigned compare).
inline bool tremble_active(const Tremble& t) {
    return t.period != 0 && std::uint32_t(t.cnt) >= std::uint32_t(t.period);
}

// Chr_Pattern's pin: AniSprite +0x12c/+0x130 = actor +0xa1c/+0xa20 [EXPORT FUN_005712e0].
// tag -1 = no pin. While pinned the body quad shows (tag, frame) (FUN_00446340 0x446365) and
// the ASO VM does not run (FUN_004472e0 0x44735f); Chr_Animate (FUN_00571000) releases it.
struct PatternPin {
    int tag = -1;
    int frame = 0;
    bool pinned() const { return tag != -1; }
};

}  // namespace cleria
