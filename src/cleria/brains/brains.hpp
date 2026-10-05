// Ported enemy brains, one per file. Each is a line-by-line port of its decompiled brain
// function against EnemyAi's raw +0x6f0..+0x8b0 block; the spec it was ported from is
// build/ai_spec/M<cast>.md. Registered by AI index in registry.cpp.
#pragma once
#include <cstring>

#include "cleria/enemy_ai.hpp"

namespace cleria::brains {

// Shared by many brains: the difficulty d used for odds and cooldowns
// (3 if g_flags[201], -2 very easy, -1 easy, else the 0/1/2 index).
inline int difficulty(const AiEnv& e) { return e.difficulty; }

// unit(w) = bitcast<float>((w & 0x7fffff) | 0x3f800000) - 1.0f: the WHOLE word, in [0, 1).
inline float unit(std::uint32_t w) {
    const std::uint32_t bits = (w & 0x7fffffu) | 0x3f800000u;
    float f;
    std::memcpy(&f, &bits, 4);
    return f - 1.0f;
}

// The swarm speed M460 seeds into a bat's +0x818 (brain B halves it for the final dock).
inline float swarm_speed(int d, bool hugo) {
    const float df = float(d), dc = float((d > 1 ? d : 1) - 1);
    return hugo ? (dc * 0.15f + 0.5f) * df * 0.0015f + 0.073f
                : (dc * 0.25f + 0.5f) * df * 0.0025f + 0.058f;
}

AiOut m100(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0047adf0 Riffligan
AiOut m130(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0047bae0 Selnarg
AiOut m150(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0047c5d0 Normous (turret)
AiOut m910(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004ec790 Nooze (splits)
AiOut m180(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0047cfd0 Wilewarer (guards)
AiOut m390(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0047f6a0 Baocar (warp mage)
AiOut m490(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00481940 Sopus-Baocar (bomb rings)
AiOut m970(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004ca1a0 Vagully (lone bat)
AiOut m460(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004c7020 the bat-swarm boss
void m460_dead(Actor&, AiWorld&);                              // its handler's dead path (#5)
AiOut m480(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00480f50 Nustarl (turret)

}  // namespace cleria::brains
