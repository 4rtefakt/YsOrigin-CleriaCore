// What a landed player hit LOOKS and SOUNDS like on a monster: the sounds and the EF2 effects
// the engine spawns at the victim. Spec: build/combat_re/HIT_FEEDBACK.md (every value read from
// the disassembly of yso_win.exe; tags there).
//
// Three engine sites, in the order they run:
//   1. FUN_004fbb40 (the per-victim hit, in the attacker's tick):
//        dmg < 1 (a guard or a zero roll) -> SE 93 player\p_parry (545 zako\z_180_guard for casts
//        0x16 / 0x49) at the victim, then FUN_00589130's 4 spark streaks (the guard itself threw 3
//        more, earlier in the function); the host makes them (Weather::sparks).
//        dmg >= 1 -> the elemental hit effect of the damage class +0x109 (skill_hit_effect).
//   2. FUN_00479b00 (the monster's common tick, first thing): +0x3bc & 2 (it took >= 1 damage)
//        -> FUN_00478510 alive path (the character's hit spark) then SE 92 player\p_hit0.
//   3. FUN_004780e0 (the death entry, +0x3fc 0 -> 1): FUN_00478510 death path (the death puff
//        and the character's hit spark) then SE 92 (unless +0x130 & 0x1000).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

#include "cleria/cos_table.hpp"
#include "cleria/mathx.hpp"

namespace cleria::hitfx {

inline constexpr int kSeHit = 92;            // player\p_hit0  — 0x479b5c (ecx 0x5c), 0x47819c
inline constexpr int kSeNoDamage = 93;       // player\p_parry — 0x4fd1c0 (ecx 0x5d)
inline constexpr int kSeNoDamageGuard = 545; // zako\z_180_guard — 0x4fd1ca (ecx 0x221)
inline constexpr int kSeDeathPuff = 862;     // event\e_l7_45 — 0x478828 (ecx 0x35e)

// FUN_004fbb40 0x4fd1b5..0x4fd1d2: the no-damage hit's SE by the victim's cast (+0x124).
inline int no_damage_se(int cast) {
    return (cast == 0x49 || cast == 0x16) ? kSeNoDamageGuard : kSeNoDamage;
}

// FUN_004fbb40 0x4fce60..0x4fd059: the damage class +0x109 picks the effect (jump table
// 0x4fd328): 0/4 -> 0x18 adol_kaiten_hit, 1/5 -> 0x1f adol_fire_hit, 2/6 -> 0x0d
// adol_dash_hit, 3 (melee) and anything else -> none. It is spawned only for character
// g_flags[150] == 0, or 1 / 2 with a class other than 1 / 5 (0x4fcf6c..0x4fcf93).
// Returns -1 for none.
inline int skill_hit_effect(int cls, int character) {
    int id = -1;
    switch (cls) {
        case 0: case 4: id = 0x18; break;
        case 1: case 5: id = 0x1f; break;
        case 2: case 6: id = 0x0d; break;
        default: return -1;
    }
    if (character == 0) return id;
    if ((character == 1 || character == 2) && cls != 1 && cls != 5) return id;
    return -1;
}
// Its placement (0x4fd020..0x4fd059 -> FUN_00450410): at the victim's origin, yaw
// -pi/2 - victim facing (+0x314; 0x6be078 = -pi/2), scale 1, side 0, power 0, knock 1,
// rtype 0, status 0, cls = the class.
inline float skill_hit_yaw(float victim_facing) { return -1.5707964f - victim_facing; }

// FUN_00478510's character hit spark (EFTLIST ids). g_flags[150] (0x76bb74) = character,
// g_flags[149] (0x76bb70) = Yunica's weapon set:
//   1: 0x7e PC1_ken_hit (sword) / 0x7f PC1_ono_hit (axe); 3: 0xb6 PC3_hit; else 0x41 z_yarare01.
// The alive path skips Toal's spark while the player is in section 0x13d (0x479590).
inline int hit_spark_effect(int character, int weapon149, bool alive_path, int player_section) {
    if (character == 1) return weapon149 == 0 ? 0x7e : 0x7f;
    if (character == 3) return (alive_path && player_section == 0x13d) ? -1 : 0xb6;
    return 0x41;
}

// FUN_00478510's draw (0x4785cc..0x478674): ONE game-RNG step, its top 23 bits as a float in
// [1, 2), scale = (hi - lo) * (f - 1) + lo. The alive path (FUN_00479b00) passes lo 1.5 /
// hi 2.0 (0x6bd4d4 / 0x6bd580), the death entry lo 1.0 / hi 1.5 (0x6bd438 / 0x6bd4d4).
inline float spark_scale(std::uint32_t draw, float lo, float hi) {
    const std::uint32_t bits = (draw & 0x7fffffu) | 0x3f800000u;
    float f;
    std::memcpy(&f, &bits, 4);
    return (hi - lo) * (f - 1.0f) + lo;
}
inline constexpr float kAliveLo = 1.5f, kAliveHi = 2.0f;
inline constexpr float kDeathLo = 1.0f, kDeathHi = 1.5f;

// FUN_00478510's placement: the victim's origin (+0x50) minus the global vector 0x795f50 —
// the camera's forward vector (GOXX_EFFECTS.md), so the spark sits one unit toward the
// viewer — and yaw -pi/2 - atan2(+0x73c, +0x734), the victim's last vector to the player
// (FUN_00427410 = atan2(xmm0, xmm1)).
struct Spark {
    Vec3 pos{};
    Vec3 rot{};          // (pitch, yaw, roll) as FUN_00451a40 reads it
    float scale = 1.0f;  // uniform
};
inline Spark place_spark(const Vec3& origin, const Vec3& cam_fwd, float to_player_dx, float to_player_dz,
                         float scale) {
    Spark s;
    s.pos = {origin.x - cam_fwd.x, origin.y - cam_fwd.y, origin.z - cam_fwd.z};
    s.rot = {0.0f, -1.5707964f - std::atan2(to_player_dz, to_player_dx), 0.0f};
    s.scale = scale;
    return s;
}

// The hurt tint. FUN_00478510 writes +0x238 = 1.65 (0x3fd33333) first thing, on both paths and
// before the cast-0x1a return; FUN_0045a3a0 (the integrator FUN_0045a510's helper, every tick)
// steps a non-zero +0x238 by -0.075 (0x6bd174) clamped at 0 [C-DIS, AITICK.md §8]; and while it is
// not 0.0, FUN_00473c60 forces the specular RGB to (0x6b, 0x37, 0x13) before the marker glow is
// added [BYTES 0x473ea8..0x473ec5]. The specular is the vertex colour D3D adds after the texture
// modulate (SPECULARENABLE is on), so a hit monster brightens dim orange for 22 ticks; there is
// no white flash, and a guarded / zero hit sets nothing (it never reaches FUN_00478510).
inline constexpr float kHurtTimer = 1.65f;
inline constexpr float kHurtStep = 0.075f;
inline constexpr std::uint32_t kHurtSpecular = 0x6b3713;
inline float step_hurt(float t) {
    if (t == 0.0f) return 0.0f;
    t -= kHurtStep;
    return t < 0.0f ? 0.0f : t;
}
inline std::uint32_t hurt_specular(float t) { return t != 0.0f ? kHurtSpecular : 0u; }

// The charged skill's glow on the player's sprite: FUN_00473c60 [BYTES 0x473df1..0x473ea5; data 0x69bd60 / 0x69bda0].
// With no Fix held ([0x75ad90] + [0x75ad94] == 0), on the player actor, while the charge level 0x76c138 is 2, the
// specular takes row[slot] x (0.5 x sin + 0.5) per channel (truncated, each clamped to 255): slot = FUN_0046d560
// (the bracelet: Ventus 0, Terra 1, Ignis 2, none 3), the sine from the cosine table at byte +0x2a4, which
// FUN_0045c490 steps by 4 a tick (a pulse of 64 ticks). Character 0 reads his own table: the same three rows in
// another order. `spec` = the specular so far (0xRRGGBB, as kHurtSpecular's bytes).
inline std::uint32_t charged_glow(std::uint32_t spec, int character, int slot, std::uint8_t phase) {
    static const float kOthers[4][3] = {{0, 128, 80}, {80, 80, 0}, {112, 0, 16}, {160, 160, 160}};   // 0x69bd60
    static const float kAdol[4][3] = {{112, 0, 16}, {0, 128, 80}, {80, 80, 0}, {160, 160, 160}};     // 0x69bda0
    const float* row = (character == 0 ? kAdol : kOthers)[slot & 3];
    const float f = cos_table((int(phase) - 0x40) & 0xff) * 0.5f + 0.5f;                             // 0x6bd304
    std::uint32_t out = 0;
    for (int k = 0; k < 3; ++k) {
        const int c = int((spec >> (16 - 8 * k)) & 0xffu) + int(row[k] * f);
        out |= std::uint32_t(c > 0xff ? 0xff : c) << (16 - 8 * k);
    }
    return out;
}

// The actor tint struct T (0x18 bytes; build/render_re/SCREEN_EFFECTS.md §3.1). Two per actor: +0x260 is where
// FUN_00473c60 starts the SPECULAR from, +0x278 the diffuse. FUN_00473b70 [EXPORT 00473b70.c] gives the colour,
// per channel and truncated: step != 0 ? B + (A - B) * t : base. FUN_0045a3a0 [EXPORT 0045a3a0.c:14-45] steps
// it every tick: t += step; on reaching 1.0 it stays there, then count -1 holds, count != 0 counts down, and
// count == 0 switches the struct off (t = step = 0). The hurt timer +0x238 REPLACES the specular while it runs.
struct Tint {
    std::uint32_t base = 0, a = 0, b = 0;   // T+0x00 / +0x04 / +0x08, 0xRRGGBB (bytes b, g, r)
    float t = 0, step = 0;                  // T+0x0c / +0x10
    int count = 0;                          // T+0x14
    void tick() {
        if (step != 0.0f) {
            t += step;
            if (t < 1.0f) return;
            t = 1.0f;
            if (count == -1) return;
            if (count != 0) { --count; return; }
        }
        t = 0.0f; step = 0.0f;
    }
    std::uint32_t colour() const {
        if (step == 0.0f) return base & 0xffffffu;
        auto ch = [&](int sh) {
            const float fa = float((a >> sh) & 0xffu), fb = float((b >> sh) & 0xffu);
            return std::uint32_t(int((fa - fb) * t + fb)) & 0xffu;
        };
        return (ch(16) << 16) | (ch(8) << 8) | ch(0);
    }
};
// The white flashes: the specular struct with A = 0, B = 0xffffff, t = 0, count = 1 — full white, back to 0
// linearly in 1 / step ticks, one more tick at 0. The step: 0.05 (0x3d4ccccd) on the three GUARD branches of
// FUN_004fd350 [EXPORT 004fd350.c:155-159, 197-201, 263-267], 0.075 (0x3d99999a) when the Boost gauge fills
// (FUN_0046cc10 [EXPORT 0046cc10.c:120-124]). The base word is not written.
inline constexpr float kGuardFlashStep = 0.05f;
inline constexpr float kBoostReadyFlashStep = 0.075f;
inline void white_flash(Tint& s, float step) { s.a = 0; s.b = 0xffffffu; s.t = 0.0f; s.step = step; s.count = 1; }

// FUN_00478510's alive path also returns at once, its draw taken, for cast 0x1a while
// g_flags[156] (0x76bb8c, a boss fight) is set (0x4793f9..0x479405).
inline bool alive_spark_suppressed(int cast, bool boss_fight) { return boss_fight && cast == 0x1a; }

// The death path's puff (0x47869d..0x478832), gated by the victim's +0x130 factory word:
//   & 0x8: no puff and no spark (straight to the drops);
//   & 0x2000: 0x43 z_yarare03 + SE 862 at the spark position; else 0x56 m_zakoyarare.
struct DeathPuff {
    int effect = -1;
    int se = -1;
    bool spark = false;  // the character spark follows
};
inline DeathPuff death_puff(std::uint32_t f130) {
    DeathPuff d;
    if (f130 & 0x8u) return d;
    d.spark = true;
    if (f130 & 0x2000u) { d.effect = 0x43; d.se = kSeDeathPuff; }
    else d.effect = 0x56;
    return d;
}

}  // namespace cleria::hitfx
