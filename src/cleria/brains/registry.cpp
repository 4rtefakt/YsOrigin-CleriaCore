// AI index (actor +0x120 = CASTINFO +0x64, the slot in the dispatch table 0x69b8c0)
// -> ported brain. tools/ai_roster.py lists every index with its handler and brain.
#include "cleria/brains/brains.hpp"

namespace cleria {

EnemyBrain enemy_brain_for(int ai_index) {
    switch (ai_index) {
        case 3: return &brains::m100;    // M100 Riffligan
        case 4: return &brains::m110;    // M110 Genoid
        case 5: return &brains::m120;    // M120 Jestona
        case 6: return &brains::m130;    // M130 Selnarg
        case 8: return &brains::m150;    // M150 Normous
        case 10: return &brains::m170;   // M170 Vilvan
        case 11: return &brains::m180;   // M180 Wilewarer
        case 12: return &brains::m190;   // M190 Irebolg
        case 13: return &brains::m200;   // M200 Venueba
        case 14: return &brains::m210;   // M210 Kelemare
        case 17: return &brains::m240;   // M240 Bordisch
        case 18: return &brains::m250;   // M250 Nipper
        case 23: return &brains::m300;   // M300 Hubamobre
        case 24: return &brains::m310;   // M310 Jiamobre
        case 27: return &brains::m340;   // M340 Rezalior
        case 30: return &brains::m370;   // M370 Celny
        case 31: return &brains::m380;   // M380 Chelasken
        case 32: return &brains::m390;   // M390 Baocar
        case 39: return &brains::m460;   // M460 the bat-swarm familiar
        case 41: return &brains::m480;   // M480 Nustarl
        case 42: return &brains::m490;   // M490 Sopus-Baocar
        case 43: return &brains::m500;   // M500 Oggnome
        case 44: return &brains::m510;   // M510 Mobre
        case 45: return &brains::m520;   // M520 Menoak
        case 46: return &brains::m530;   // M530 Sepast
        case 47: return &brains::m540;   // M540 Perrie
        case 69: return &brains::m760;   // M760 Raldisch
        case 70: return &brains::m770;   // M770 Hubamobrun
        case 84: return &brains::m910;   // M910 Nooze
        case 85: return &brains::m920;   // M920 Nooze (Blue)
        case 86: return &brains::m930;   // M930 Nooze (Red)
        case 90: return &brains::m970;   // M970 Vagully
        default: return nullptr;
    }
}

bool enemy_handler_clamps(int ai_index) {
    return ai_index == 3 || ai_index == 24;    // FUN_0047b7f0 (M100), FUN_00492f50 (M310)
}

void enemy_handler_clamp(int ai_index, Vec3& origin, int arena_mode, bool boss_fight, const float rect[4]) {
    if (enemy_handler_clamps(ai_index)) arena_clamp(origin, arena_mode, rect);
    if (ai_index == 46 && boss_fight && -2.75f > origin.z) origin.z = -2.75f;   // M530 Sepast
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
