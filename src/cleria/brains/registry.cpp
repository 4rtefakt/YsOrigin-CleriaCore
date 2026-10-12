// AI index (actor +0x120 = CASTINFO +0x64, the slot in the dispatch table 0x69b8c0)
// -> ported brain. tools/ai_roster.py lists every index with its handler and brain.
#include "cleria/brains/brains.hpp"

namespace cleria {

namespace brains {
AiOut none(EnemyAi&, const EnemyAiParams&, const AiEnv&) { return {}; }   // brains.hpp: the empty brain
}

EnemyBrain enemy_brain_for(int ai_index) {
    switch (ai_index) {
        case 3: return &brains::m100;    // M100 Riffligan
        case 4: return &brains::m110;    // M110 Genoid
        case 5: return &brains::m120;    // M120 Jestona
        case 6: return &brains::m130;    // M130 Selnarg
        case 7: return &brains::m140;    // M140 Twarth
        case 8: return &brains::m150;    // M150 Normous
        case 10: return &brains::m170;   // M170 Vilvan
        case 11: return &brains::m180;   // M180 Wilewarer
        case 12: return &brains::m190;   // M190 Irebolg
        case 13: return &brains::m200;   // M200 Venueba
        case 14: return &brains::m210;   // M210 Kelemare
        case 17: return &brains::m240;   // M240 Bordisch
        case 18: return &brains::m250;   // M250 Nipper
        case 20: return &brains::m270;   // M270 Moz
        case 23: return &brains::m300;   // M300 Hubamobre
        case 24: return &brains::m310;   // M310 Jiamobre
        case 25: return &brains::m320;   // M320 Pikkarton
        case 26: return &brains::m330;   // M330 Pikkardon
        case 27: return &brains::m340;   // M340 Rezalior
        case 28: return &brains::m350;   // M350 Diphtheloyd
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
        case 48: return &brains::m550;   // M550 Arte-Baocar
        case 51: return &brains::m580;   // M580 Sevnam
        case 52: return &brains::m590;   // M590 Sepaswait
        case 53: return &brains::m600;   // M600 Titamonus
        case 29: return &brains::m360;   // M360 Jenocres (the floor-4 boss)
        case 91: return &brains::m980;   // M980 Jenocres' crystals
        case 49: return &brains::m560;   // M560 Karon (handler: brains/revive.cpp)
        case 71: return &brains::m780;   // M780 Zelkaron (handler: brains/revive.cpp)
        case 50: return &brains::m570;   // M570 Zarue (handler: brains/revive.cpp)
        case 34: return &brains::m410;   // M410 Zarue's hand
        case 36: return &brains::m430;   // M430 Zeruena (handler: brains/revive.cpp)
        case 19: return &brains::m260;   // M260 Hand (Bazeraena's)
        case 60: return &brains::m670;   // M670 Bazeraena (handler: brains/revive.cpp)
        case 67: return &brains::m740;   // M740 Drefflum (handler far freeze)
        case 68: return &brains::m750;   // M750 Liliborad (its dead path in the view)
        case 16: return &brains::m230;   // M230 Euchreas (handler far freeze)
        case 35: return &brains::m420;   // M420 Sinn-Piache (handler far freeze; the +0x64 alpha bit in the view)
        case 38: return &brains::m450;   // M450 Maka-Piache (the same, and the Mask of Eyes tint)
        case 40: return &brains::m470;   // M470 Levenueba (the lifesteal and the hold in the view)
        case 62: return &brains::m690;   // M690 Glaregrave (handler far freeze)
        case 65: return &brains::m720;   // M720 Heketie (handler far freeze)
        case 63: return &brains::m700;   // M700 Fliogan (its dead path in the view)
        case 64: return &brains::m710;   // M710 Astyle (handler far freeze; the dead-path cull in the view)
        case 61: return &brains::m680;   // M680 Hekat-Baocar (handler far freeze: enemy_handler_far_freeze)
        case 37: return &brains::m440;   // M440 Zeruena's hand
        case 88: return &brains::m950;   // M950 Nooze (Green)
        case 89: return &brains::m960;   // M960 Nooze (Purple)
        case 54: return &brains::m610;   // M610 Gunghogan
        case 33: return &brains::m400;   // M400 Tolpi
        case 58: return &brains::m650;   // M650 Tenat-Baocar
        case 57: return &brains::m640;   // M640 Samachryce
        case 9: return &brains::m160;    // M160 Xomack
        case 15: return &brains::m220;   // M220 Chryolos (the normal brain)
        case 55: return &brains::m620;   // M620 Ogacha
        case 56: return &brains::m630;   // M630 Kelmarel
        case 59: return &brains::m660;   // M660 Urobass
        case 21: return &brains::m280;   // M280 Chups (lifesteal: the host's hit site)
        case 22: return &brains::m290;   // M290 Bicmobre (death bombs: view_revive.inl)
        case 69: return &brains::m760;   // M760 Raldisch
        case 70: return &brains::m770;   // M770 Hubamobrun
        case 78: return &brains::m850;   // M850 Epona (the duel)
        case 74: return &brains::m810;   // M810 Hugo (class-4 duel)
        case 76: return &brains::m830;   // M830 Galleon (class-4 duel)
        case 75: return &brains::m820;   // M820 Toal (class-4 duel, Hugo's S_3080)
        case 77: return &brains::m840;   // M840 Shion (class-4 duel, Toal's S_3080)
        case 79: return &brains::m860;   // M860 Kishgal (class-4 duel, Yunica's S_3080)
        case 82: return &brains::m890;   // M890 Velagunder Spawn (boss01's run drops it)
        case 83: return &brains::m900;   // M900 the Eyes of Fact (M810's)
        case 84: return &brains::m910;   // M910 Nooze
        case 85: return &brains::m920;   // M920 Nooze (Blue)
        case 86: return &brains::m930;   // M930 Nooze (Red)
        case 87: return &brains::m940;   // M940 Nooze (Yellow)
        case 90: return &brains::m970;   // M970 Vagully
        case 80: return &brains::m870;   // M870 Zava (the floor-6 boss actor; boss06_zava.hpp)
        // The handlers with no brain call (STANDINS.md §4): they used to run Riffligan's brain as the stand-in.
        // [not modelled] idx 73's re-type of the puppet while g_flags[130] is set (+0x60 = 0x88, +0x64 = 0x2f,
        // +0x4be = 0, +0x12c |= 0x10000 [BYTES 0x4f5c18..0x4f5c3f]; build/ai_spec/M800.md).
        case 66: case 72: case 92: return &brains::none;   // M730 / M790 / M990
        case 73: case 81: return &brains::none;            // M800 Yunica / M880 Dalles (cutscene puppets); 81 has no cast
        default: return nullptr;
    }
}

bool enemy_handler_far_freeze(int ai_index) {
    return ai_index == 61 || ai_index == 67    // M680 FUN_004c4df0, M740 FUN_004c59d0 (625.0 at 0x6bdd7c)
        || ai_index == 64                      // M710 FUN_004c3d50 0x4c3d79
        || ai_index == 62                      // M690 FUN_004c6cd0 0x4c6cf8
        || ai_index == 65                      // M720 FUN_004be0a0 0x4be0c8
        || ai_index == 35 || ai_index == 38    // M420 FUN_004bfdd0 0x4bfdf8, M450 FUN_004c0ee0 0x4c0f08
        || ai_index == 16;                     // M230 FUN_004c2fb0 0x4c2fd8
}

bool enemy_handler_clamps(int ai_index) {
    return ai_index == 3 || ai_index == 24;    // FUN_0047b7f0 (M100), FUN_00492f50 (M310)
}

void enemy_handler_clamp(int ai_index, Vec3& origin, int arena_mode, bool boss_fight, const float rect[4],
                         int flag131, int phase) {
    if (enemy_handler_clamps(ai_index)) arena_clamp(origin, arena_mode, rect);
    // M970 Vagully (FUN_004cc070 0x4cc375..0x4cc3bf, M970.md H4): a swarm bat (phase 4) is held in
    // the rect's box reading whatever g_flags[190] is, x then z, each "min, else max". Without it
    // a bat that strays over S_1080's railing never docks, and M460's recall waits forever (D1).
    if (ai_index == 90 && phase == 4) arena_clamp(origin, 1, rect);
    if (ai_index == 46 && boss_fight && -2.75f > origin.z) origin.z = -2.75f;   // M530 Sepast
    // The class-4 duel handlers clamp on their live path only (g_flags[130] != 0; boss_fight
    // stands in for it [H]: the duel scripts set and the defeat clears 156 and 130 together).
    if (ai_index == 76 && boss_fight) arena_clamp(origin, arena_mode, rect);   // M830: FUN_0047abf0
    // M820 (0x4e305c), M840 (0x4df8ab), M860 (0x4e803d): FUN_0047abf0 on the same live path.
    if ((ai_index == 75 || ai_index == 77 || ai_index == 79) && boss_fight) arena_clamp(origin, arena_mode, rect);
    if (ai_index == 74 && boss_fight) {                // M810 (0x4daa88): mode-blind, a 0.01 inset
        constexpr float kIn = 0.009999997913837433f;   // DAT_006bd080
        float x = origin.x, z = origin.z;
        bool moved = false;
        if (rect[0] > x) { x = rect[0] + kIn; moved = true; }
        if (x > rect[2]) { x = rect[2] - kIn; moved = true; }   // tests the updated x
        if (rect[1] > z) { z = rect[1] + kIn; moved = true; }
        if (z > rect[3]) { z = rect[3] - kIn; moved = true; }
        if (moved) { origin.x = x; origin.z = z; }
    }
    // M850 Epona (FUN_004d1c90 0x4d25aa..0x4d25fd): the box clamp, mode 1 only, gated by 131.
    if (ai_index == 78 && arena_mode == 1 && flag131 == 0) arena_clamp(origin, 1, rect);
}

// M850 FUN_004d1c90: `cmp [0x76bb24], 0` first (0x4d1c99), `cmp [0x76bb28], 0` after the
// culled test (0x4d2146). build/ai_spec/M850.md §1.
bool enemy_handler_puppet(int ai_index, const AiEnv& env) {
    // M870 Zava FUN_0053af10: `cmp [0x76bb8c], 0` first (0x53af3f, g_flags[156]). build/boss_re/ZAVA_AB.md §A.1.
    if (ai_index == 80) return env.world && env.world->flag(156, 1) == 0;
    return ai_index == 78 && env.world && env.world->flag(130, 1) == 0;
}
bool enemy_handler_held(int ai_index, const AiEnv& env) {
    // Every handler 3..93 but Hugo's eyes (83, FUN_004731d0) tests g_flags[131] (0x76bb28) between its cull branch
    // and its brain: 90 of the 91 functions that read it, the 91st being the scene reset FUN_00431260 (FIX_SLOTS.md
    // S5a, REACQUIRE.md D2) [EXPORT 0047b7f0.c:73-117 and the same branch in each handler of table 0x69b8c0].
    // Not here: the handlers whose ported brain takes that branch itself (it keeps their gauge / gates in their
    // order): M810 / M820 / M830 / M840 / M860 (74..77, 79) and Zava (80).
    if (ai_index < 3 || ai_index > 93 || ai_index == 83) return false;
    if ((ai_index >= 74 && ai_index <= 77) || ai_index == 79 || ai_index == 80) return false;
    return env.world && env.world->flag(131, 0) != 0;
}

EnemyDeathHook enemy_death_hook_for(int ai_index) {
    switch (ai_index) {
        case 39: return &brains::m460_dead;
        case 29: return &brains::m360_dead;
        case 78: return &brains::m850_dead;
        case 74: return &brains::m810_dead;
        case 76: return &brains::m830_dead;
        case 75: return &brains::m820_dead;
        case 77: return &brains::m840_dead;
        case 79: return &brains::m860_dead;
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
