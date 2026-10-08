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

// FUN_004773f0, the brains' jump primitive (identical to anime_Jump's FUN_00449460): when
// grounded, +0x330 = v (the cast's +0x408 when v == 0), y += v, airborne, own-jump byte set.
// Defined in m250.cpp.
void jump(Actor& a, float v, float cast_jump_v0, AiOut& out);

// The launch each jump-capable brain inlines at section 0x13a's Marker 1 (its own copy, with
// its own K / push / fallback: build/ai_spec/JUMPHELPER.md §7), fed by the jump helper's
// +0x7dc / +0x7e0. launch_gate is the "std" gate; launch writes +0x330 = v, +0x54 += v,
// airborne and the own-jump byte (no +0x420); push1 adds sp along `heading` to channel 1.
// [not modelled] the x1.6 on surface bytes 0x0d / 0x11 (+0x4b9) some tails apply.
inline bool launch_gate(const AiEnv& e) { return e.section == 0x13a && e.grounded && e.aso_marker == 1; }
void launch(Actor& a, float v, AiOut& out);       // defined in m250.cpp
void push1(Actor& a, float heading, float sp);    // defined in m250.cpp

// The swarm speed M460 seeds into a bat's +0x818 (brain B halves it for the final dock).
inline float swarm_speed(int d, bool hugo) {
    const float df = float(d), dc = float((d > 1 ? d : 1) - 1);
    return hugo ? (dc * 0.15f + 0.5f) * df * 0.0015f + 0.073f
                : (dc * 0.25f + 0.5f) * df * 0.0025f + 0.058f;
}

AiOut m100(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0047adf0 Riffligan
AiOut m110(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00489970 Genoid
AiOut m120(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00483200 Jestona (swordsman)
AiOut m130(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0047bae0 Selnarg
AiOut m150(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0047c5d0 Normous (turret)
AiOut m920(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004ed3c0 Nooze (Blue)
AiOut m910(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004ec790 Nooze (splits)
AiOut m930(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004ee000 Nooze (Red)
AiOut m940(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004eec20 Nooze (Yellow)
AiOut m190(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0048f6d0 Irebolg (charge, leap)
AiOut m240(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00490530 Bordisch (smash, slam)
AiOut m760(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00499f90 Raldisch (press, charge)
AiOut m300(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00491060 Hubamobre (fire fan)
AiOut m770(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0049b060 Hubamobrun (fire fans)
AiOut m340(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00493240 Rezalior (breeds Perrie)
AiOut m380(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004953e0 Chelasken (breeds Celny)
AiOut m370(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00494430 Celny
AiOut m310(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00491c00 Jiamobre (hops, shuriken)
AiOut m540(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00498830 / FUN_00497bb0 Perrie (fuse)
AiOut m530(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004965d0 Sepast (beam top)
AiOut m580(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0049db20 Sevnam (M500's teleporter, bubbles)
AiOut m140(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0049f370 Twarth (gauntlet, charge)
AiOut m270(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004a1940 Moz (running contact)
AiOut m550(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0049c130 Arte-Baocar (warp caster, healer)
AiOut m350(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004a56c0 Diphtheloyd (flying breath, swoop)
AiOut m590(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004a7030 Sepaswait (lasers, the ramp dash)
AiOut m600(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004a8ea0 Titamonus (M110 + the 312 dive)
AiOut m360(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004d2620 Jenocres (the floor-4 boss)
void m360_dead(Actor&, AiWorld&);                              // FUN_004d3f30's dead path (crystals, flags)
AiOut m980(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004d4370 Jenocres' crystals
AiOut m560(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004a0260 Karon (buried, leaping spin; revives)
AiOut m780(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004aa220 Zelkaron (Karon + combos; revives)
AiOut m570(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004f3730 Zarue (hover summoner; revives)
AiOut m410(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004f4bb0 Zarue's hand
AiOut m430(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004b27e0 Zeruena (M570's template + the barrier; revives)
AiOut m440(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004b3e70 Zeruena's hand (M410 minus DEF 999 / the clamp)
AiOut m950(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004ef840 Nooze (Green)
AiOut m610(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004ac940 Gunghogan (M190's charge + leap)
AiOut m400(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004ae8c0 Tolpi (the pollen spray)
AiOut m650(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004b4890 Tenat-Baocar (drifting caster, homing shots)
AiOut m280(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004a34b0 Chups (the latch)
AiOut m290(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004a2120 Bicmobre (kind-0xf bombs)
// FUN_004a2f90, M290's handler death helper: one dead tick (bb5c = DAT_0076bb5c); the bombs it throws.
std::vector<AiBullet> m290_dead(Actor& a, int bb5c, const std::function<std::uint32_t()>& rng);
AiOut m170(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0048a6c0 Vilvan (orbits)
AiOut m180(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0047cfd0 Wilewarer (guards)
AiOut m390(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0047f6a0 Baocar (warp mage)
AiOut m490(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00481940 Sopus-Baocar (bomb rings)
AiOut m970(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004ca1a0 Vagully (lone bat)
AiOut m460(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004c7020 the bat-swarm boss
void m460_dead(Actor&, AiWorld&);                              // its handler's dead path (#5)
AiOut m850(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004cc3d0 Epona (the duel)
void m850_dead(Actor&, AiWorld&);                              // FUN_004d1c90's dead path (156 / 130)
AiOut m810(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004da070 / FUN_004d7510 Hugo (duel)
void m810_dead(Actor&, AiWorld&);                              // its handler's defeat (0x4da5fe)
AiOut m900(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004731d0 / FUN_004dab30 Eyes of Fact (M810's)
AiOut m830(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004e9870 / FUN_004e8050 Galleon (duel)
void m830_dead(Actor&, AiWorld&);                              // its handler's defeat (0x4e98bd)
AiOut m820(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004e2bd0 / FUN_004df8c0 Toal (duel)
void m820_dead(Actor&, AiWorld&);                              // its handler's defeat (0x4e2c1d)
AiOut m840(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004df410 / FUN_004dce30 Shion (duel)
void m840_dead(Actor&, AiWorld&);                              // its handler's defeat (0x4df45d)
AiOut m860(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004e7af0 / FUN_004e3070 Kishgal (duel)
void m860_dead(Actor&, AiWorld&);                              // its handler's defeat (0x4e7bac)
AiOut m500(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00485050 Oggnome (teleporter)
AiOut m890(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004f6520 Velagunder Spawn (boss01)
AiOut m200(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0047db90 Venueba
AiOut m250(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0048e190 Nipper (hops)
AiOut m510(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00488a90 Mobre (disc sweep)
AiOut m520(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_0048b460 Menoak (hop, spin)
AiOut m210(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_004865b0 Kelemare (spit, sting)
AiOut m480(EnemyAi&, const EnemyAiParams&, const AiEnv&);   // FUN_00480f50 Nustarl (turret)

}  // namespace cleria::brains
