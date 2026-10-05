// AI index (actor +0x120 = CASTINFO +0x64, the slot in the dispatch table 0x69b8c0)
// -> ported brain. tools/ai_roster.py lists every index with its handler and brain.
#include "cleria/brains/brains.hpp"

namespace cleria {

EnemyBrain enemy_brain_for(int ai_index) {
    switch (ai_index) {
        case 3: return &brains::m100;    // M100 Riffligan
        case 6: return &brains::m130;    // M130 Selnarg
        case 8: return &brains::m150;    // M150 Normous
        case 11: return &brains::m180;   // M180 Wilewarer
        case 32: return &brains::m390;   // M390 Baocar
        case 39: return &brains::m460;   // M460 the bat-swarm familiar
        case 41: return &brains::m480;   // M480 Nustarl
        case 42: return &brains::m490;   // M490 Sopus-Baocar
        case 84: return &brains::m910;   // M910 Nooze
        case 90: return &brains::m970;   // M970 Vagully
        default: return nullptr;
    }
}

EnemyDeathHook enemy_death_hook_for(int ai_index) {
    switch (ai_index) {
        case 39: return &brains::m460_dead;
        default: return nullptr;
    }
}

EnemyBrain enemy_brain_or_standin(int ai_index) {
    const EnemyBrain b = enemy_brain_for(ai_index);
    return b ? b : &brains::m100;
}

AiOut enemy_ai_tick_m100(EnemyAi& a, const EnemyAiParams& p, const AiEnv& env) {
    return enemy_ai_tick(a, p, env, &brains::m100);
}

}  // namespace cleria
