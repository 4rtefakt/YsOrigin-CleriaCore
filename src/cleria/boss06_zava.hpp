// Zava herself (the floor-6 boss's actor, cast 0xad CAST_M870, AI index 80) — the link between her enemy brain
// (src/cleria/brains/m870.cpp) and the two heads (src/cleria/boss06.hpp). Pure and headless.
//
// Spec: build/boss_re/ZAVA_AB.md (slices A and B: handler FUN_0053af10, brain FUN_0053ab20, death FUN_0053a760,
// ride FUN_0053aa90, the helpers, phase 1 FUN_00537160) and ZAVA_CG.md slice C (phase 2 FUN_00538820, the spline
// FUN_00536170 / FUN_00536580). Section references in the code are "AB §n" / "CG §n".
//
// Why an EnemyBrain and not a Boss05-style class: her handler sits in the enemy handler table and runs the shared
// action player (FUN_00479b00: steer +0x708, turn +0x814, the hover, act +0x710, the counters +0x87c / +0x8a0 /
// +0x700) and the actor mover / integrator between its own pieces, on the raw AI block every other brain uses. So
// she is `brains::m870`, driven by enemy_ai_tick like M360; what AiEnv cannot carry (the heads, the frame's
// Boss06Input, her statics, the outputs AiOut has no field for) rides on a Boss06Link the host hangs on
// AiEnv::boss06. She reads and writes the heads only through Boss06's public interface.
//
// A frame, in the handler's order (AB §A.1):
//   1. enemy_ai_tick(Z.ai, params, env, brains::m870)   handler head (HP floor, dying, death, the 131 hold) +
//                                                       brain FUN_0053ab20; then the shared action player
//   2. brains::m870_post(Z, env, out)                   0x53b459..0x53b572: facing (+0x39a / +0x360), pose
//   3. the host's mover, integrator, body push, hit scan
//   4. brains::m870_tail(Z, env)                        0x53b5a7..: ride, the floor ring 270, the z / y clamp
// and, after the heads' own tick, brains::m870_heads_events(Z, env, heads_out, out) for the fusion break's throw
// and the swap's re-seat. The host does not call 1..4 while the game is paused (byte [0x74ab8b] != 0 or byte
// [0x74c0f0] == 0, 0x53af64) [H].
#pragma once
#include <array>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

#include "cleria/boss06.hpp"

namespace cleria {

struct Actor;

// What the handler reads that neither AiEnv nor Boss06Input carries.
struct Boss06ZavaIn {
    // FindFx(136 / 147 / 148) (FUN_00452270): the position of the first live effect of that id (Hugo's PC2_S_C,
    // PC2_S_C2, PC2_S_C2_tame), or nothing. AB §0, §A.7, §B.3.
    std::array<std::optional<Vec3>, 3> hugo_fx{};
    // FUN_0042dc90(0x76a2b4, &p): p is inside the keep-out box (AB §A.1 KeepOut). Null = never inside.
    std::function<bool(const Vec3&)> keep_out;
    // FUN_00443b80(5, &Z+0x94): EEX record 5 + the loader's scaling, applied to the actor at 0x53ad69 (before the
    // HP / STR / DEF overrides). Null = skipped.
    std::function<void(Actor&)> eex5;
};

// What the host must do that AiOut / Boss06Output have no field for. brains::m870 resets it when it runs; on a
// frame enemy_ai_tick returns before the brain (the flag-156 puppet gate, its own cull test) it is stale: the host
// clears it (and hands a fresh Boss06Link::out) before every frame.
struct Boss06ZavaOut {
    std::vector<int> boss_cam;                 // FUN_00545000(cl m), in call order (CG §G.1 is the camera's)
    bool plain_tick = false;                   // FUN_0045f250(Z): the brain's script-puppet tick (flag130 == 0)
    bool frozen = false;                       // FUN_0045f470(Z): the flag-131 hold / the culled path
    std::optional<float> facing;               // byte Z+0x39a = 3 and Z+0x360 = this (engine basis) [0x53b470]
    int player_iframes = 0;                    // P+0x734 = max(P+0x734, this) (9999, the dying trigger)
    bool player_unlock = false;                // P+0x12c &= 0xffefefbe, P+0x60 |= 0x80, byte P+0x9d0 = 1 (death)
    std::optional<float> player_heading;       // P+0x308 = this (death step 0)
    // FUN_004fd350(ecx Z, edx P, &zero16) with Z+0xa4 (STR) = this for the call, restored after (CG §C.6 step 4).
    std::optional<float> strike_atk;
    // ChangeFog(index, speed), only if fogCount [0x76a270] > index: fogPrev = fogCur, fogSpeed = speed, fogT = 0,
    // fogCur = index, byte [fogRecs + index * 0x54 + 5] = 1 (CG §C.6 steps 2 and 6).
    std::optional<std::pair<int, float>> fog;
    std::optional<std::pair<float, float>> radial;   // f32 [0x76a548] / [0x76a54c], dword [0x7b2488] = 1 (death)
    bool gauge_zero = false;                   // FUN_0043a460(..., max Z+0x94, lenScale 0.0; HP 0.0) (death step 0)
    bool gauge_hide = false;                   // dword [0x6d5fd8] = 0 (also AiWorld::hide_boss_gauge)
    bool f6d6078 = false;                      // f32 [0x6d6078] = 0.8 (death step 0) [UNKNOWN meaning, AB G5]
    bool byte_76a888 = false;                  // byte [0x76a888] = 1 (death rewards) [UNKNOWN meaning, AB G5]
    bool kill_record = false;                  // FUN_00403cd0()
    bool award_exp = false;                    // FUN_004fa4a0(ecx Z+0x94)
};

// Hung on AiEnv::boss06 by the host for the Zava actor. `heads` null = the meshes are missing (the fight does not
// start). `out` receives her world requests with head = -1 (effects, sounds, kills, shake, zoom, rumble, flags,
// the suction's player_vel) and whatever her AbortMove / StartMove calls make the heads emit (clips, ...).
struct Boss06Link {
    Boss06* heads = nullptr;
    const Boss06Input* in = nullptr;           // this frame's (the player snapshot; abort_move runs a move tick)
    Boss06Zava* z = nullptr;                   // the shared record: Z+0x7a8 lives in z->hits
    Boss06Output* out = nullptr;
    Boss06ZavaIn zin;
    Boss06ZavaOut zout;

    // --- the engine's statics and the handler's per-frame bookkeeping (module-owned) ---
    std::array<Vec3, 4> sa{}, sb{}, sc{}, sd{};   // the spline 0x7ddd1c / 0x7ddd7c / 0x7ddddc / 0x7dde3c
    int sn = 0;                                // word [0x7ddd18]
    int fx533 = 0, fx388 = 0;                  // [0x7dde9c] / [0x7ddea0]: effect handles
    int next_handle = 0x1000;                  // her effect handles (the heads' count up from 1)
    int section = 0;                           // Z+0x994 as her requests of this frame leave it
    bool requested = false;                    // a section request was made this frame
    bool ai_ran = false;                       // the AI path ran (m870_post consumes it)
    bool f156_cleared = false;                 // flag156 was zeroed this frame (m870_tail consumes it)
};

namespace brains {
// The handler's facing and pose enforcement, after the action player [0x53b459..0x53b572].
void m870_post(Actor& Z, const AiEnv& e, AiOut& out);
// The handler's tail, after the integrator and the hit scan [0x53b5a7..0x53b75d].
void m870_tail(Actor& Z, const AiEnv& e);
// The heads' events she applies: move 8's throw (ZAVA_E2 §2, the actor side) and FUN_0053aa90 after a swap.
void m870_heads_events(Actor& Z, const AiEnv& e, const Boss06Output& heads_out, AiOut& out);
}  // namespace brains

}  // namespace cleria
