// The story duels' engine-side rules (build/event_re/DUELS.md).
#pragma once

#include <cstdint>

namespace cleria::duel {

// FUN_0046f810, the player's dead step, its first branch [EXPORT 0046f810; BYTES 0x46f810..]:
// with DAT_0076bb30 (g_flags[133], "a losable duel") set, the player's death ends the duel
// instead of the game:
//   156 (0x76bb8c, the boss fight) = 0, 130 (0x76bb24, the duel AI live) = 0, the boss gauge
//   hidden (0x6d5fd8 = 0), HP (+0x98 and 0x76a730) = 1.0, the stat refresh (FUN_004fa0e0, the
//   0x76b914 |= 0x75 dirty bits, FUN_00420c40), the motion fields zeroed (+0x32c/+0x334/+0xa3c/
//   +0xa44/+0x5d0..+0x5dc), i-frames +0x734 = 9999 and 157 / 158 (0x76bb90 / 0x76bb94) = 0.
// 174 is left alone: each duel opponent's own defeat path sets it, so 0 is the scripts' "lost"
// branch (S_3080 `Battle` -> `Battle_Lose`). Returns false (nothing written) without 133: the
// caller runs the ordinary death and the game over.
// The host does the gauge, the stat refresh and the motion fields; this writes the flags, HP
// and the i-frames.
inline bool losable_death(std::int32_t* g_flags, int& hp, int& iframes) {
    if (g_flags[133] == 0) return false;
    g_flags[156] = 0;
    g_flags[130] = 0;
    g_flags[157] = 0;
    g_flags[158] = 0;
    hp = 1;
    iframes = 9999;
    return true;
}

}  // namespace cleria::duel
