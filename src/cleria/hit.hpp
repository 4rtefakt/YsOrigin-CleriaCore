// Hit resolution — what one landed hit does to its victim, both directions.
//
// Re-derived 2026-10-05 from the disassembly (build/audit/combat.md, then re-read
// instruction by instruction; the audit's own "x(1+0.1*g_flags[182..184])" term turned
// out to be a dead store, see enemy_incoming_damage). Every function here is pure: the
// caller owns the actors and the RNG, so the whole module is unit-testable
// (apps/cleria-combattest).
//
// The two paths are different functions in the engine, not one formula with a flag:
//   player -> enemy : FUN_004fed50(ecx=0) -> FUN_004fbb40 -> FUN_004fa550
//   enemy -> player : FUN_004fed50(ecx=1) -> FUN_004fe9e0 -> FUN_004fd350 -> FUN_004fa740
// combat.hpp's damage_real is FUN_004fd350's NON-player branch, reached only from boss
// code — do not use it for the player's hits.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "cleria/combat.hpp"

namespace cleria {

// ---------------------------------------------------------------------------
// The probabilistic round shared by every damage path: P(round up) = frac*100 %.
// Engine: n = floorf(d); if (d-n != 0) { R = ((xorshift>>17)*100)/0x7fff;
// if ((int)((d-n)*100) > R) n += 1; }. `roll(pct)` must return true with P = pct/100.
template <typename RollPercent>
inline float round_probabilistic(float d, RollPercent&& roll) {
    const float n = std::floor(d);
    const float frac = d - n;
    return (frac != 0.0f && roll(float(int(frac * 100.0f)))) ? n + 1.0f : n;
}

// ---------------------------------------------------------------------------
// PLAYER -> ENEMY damage. FUN_004fa550 (0x4fa550..0x4fa737), called from FUN_004fbb40
// at 0x4fbbb4 with ECX=attacker, EDX=victim, XMM2=attacker+0xfc (the swing's
// GenerateCollision arg[11]), XMM3=attacker+0x100, then (class, yunica_bonus).
struct EnemyDefense {
    float def = 0;                     // victim +0xa8
    float resist[4] = {0, 0, 0, 0};    // victim +0xb4..+0xc0 <- ENEMYINFO +0x3c..+0x48 (FUN_004202e0)
    bool no_floor = false;             // victim +0xce: disables the floor and the min-1 rule
};

inline constexpr int kDamageClassNone = 0xff;  // attacker +0x109 for non-player hitboxes
inline constexpr int kDamageClassMelee = 3;    // FUN_0046d5e0 default (0046ea00.c:55)

// FUN_0046d5e0: the player's damage class from its live section (+0x994) — the magic
// casts carry an element, everything else is melee. Stamped into the hitbox (+0x109)
// when GenerateCollision arms it.
inline int player_damage_class(int section) {
    switch (section) {
        case 0x10d: return 0;  case 0x10e: return 1;  case 0x10c: return 2;   // normal casts
        case 0x110: return 4;  case 0x111: return 5;  case 0x10f: return 6;   // charged casts
        default:    return kDamageClassMelee;
    }
}

struct PlayerDamageMods {
    bool atk_debuff = false;    // DAT_0076c15c & 0x20: ATK' = max(0, ATK-5)
    bool yunica_bonus = false;  // FUN_004fbb40: g_flags[150]==1 (Yunica) && g_flags[149]==0 -> x1.1
    bool double_damage = false; // g_flags[210] (0x76bc64) -> x2; 0 in every observed save
};

template <typename RollPercent>
inline int enemy_incoming_damage(const AttackStats& a, int dmg_class, const EnemyDefense& v,
                                 const PlayerDamageMods& m, RollPercent&& roll) {
    float atk = a.atk;
    if (m.atk_debuff) atk = std::max(0.0f, atk - 5.0f);                       // 0x4fa582
    float raw = std::pow(atk, 0.66f) * (atk - 0.98f * v.def + 10.0f + a.bonus) / 22.92f;
    raw = std::max(0.0f, raw);                                                 // 0x4fa5e7
    // Elemental resist is a MULTIPLIER (1 - r), not a subtraction (0x4fa601). Classes
    // 4..7 (the charged casts) fold onto slots 0..3.
    float res = 0.0f;
    if (dmg_class != kDamageClassNone) {
        const int idx = dmg_class < 4 ? dmg_class : dmg_class - 4;
        res = (idx >= 0 && idx < 4) ? v.resist[idx] : 0.0f;
    }
    float d = (dmg_class != kDamageClassNone ? 1.0f - res : 1.0f) * raw;      // 0x4fa610
    if (a.min_dmg > d && !v.no_floor) d = a.min_dmg;                           // 0x4fa614
    float n = round_probabilistic(d, roll);                                    // 0x4fa631..6d6
    // Min-1 rule (0x4fa6e4): a hit on a non-invulnerable (DEF < 999), non-immune
    // (resist < 1) target always deals at least 1. [UNKNOWN] for class 0xff the engine
    // indexes resist slot 0xff (victim +0x4b0, an unmapped field); player hitboxes always
    // carry a class, so that case does not arise on this path.
    if (v.def < 999.0f && res < 1.0f && n < 1.0f && !v.no_floor) n = 1.0f;
    if (m.yunica_bonus) n *= 1.1f;                                             // 0x4fa717
    if (m.double_damage) n += n;                                               // 0x4fa728
    return int(n);                                                             // cvttss2si
    // FUN_004fbb40 then: guard/deflect (victim +0x399 or state 6, facing test) zeroes it;
    // the special launch states (+0x4d0/+0x398/+0x724) scale it by DAT_0068b218[diff];
    // HP -= dmg at 0x4fc805. The x(1+0.1*g_flags[0xb6..0xb8]) at 0x4fd064 runs AFTER the
    // subtraction and its result (0x4fd0a0) is never read again — a dead store, not a term.
}

// ---------------------------------------------------------------------------
// ENEMY -> PLAYER damage after FUN_004fa740 (combat.hpp player_incoming_damage):
// Boost halves it, truncating (0x4fd57f: test [0x76c198],0x20; mulss 0.5; cvttss2si).
// [not modelled yet] the 1/30 accessory block (0x76a624 & 0x200) and the per-character
// skill guards (Yunica/Toal spend 500 MP to block in their guard states, else x0.75;
// Hugo's shield charges +0x7c0/+0x7e0) — 0x4fd5a2..0x4fdab3.
// The guard/deflect test of FUN_004fbb40 (0x4fbf77..0x4fc028): a victim that is guarding
// (byte +0x399, or ASO section 6) takes 0 damage from a hit whose reaction byte (+0x108,
// GenerateCollision arg[9]) is <= 1 when its visible facing (+0x314) lies within
// arc/2 of the direction back toward the attacker: |wrap(norm(facing) - norm(angle + pi))|
// < arc * 0.5 * pi/180, arc 150 (0x6bdb6c). Casts 0x49, 0xa7, 0xac, 0xb8 (and 0xad while
// Durable) block every reaction type, all round (arc 360). The hit still lands
// (+0x3bc |= 4) — only bit 2 (damage) is missing, which brains read as "blocked".
// `hit_angle` is the record's +4 float; that it is the ATTACKER'S FACING (the XMM3 =
// +0x314 FUN_004fed50 receives) is [INFERRED], not traced to the write.
inline bool guard_blocks(int victim_cast, bool guarding, bool victim_durable, int rtype,
                         float victim_facing, float hit_angle) {
    if (!guarding) return false;
    const bool all_round = victim_cast == 0x49 || victim_cast == 0xa7 || victim_cast == 0xac ||
                           victim_cast == 0xb8 || (victim_cast == 0xad && victim_durable);
    if (std::uint8_t(rtype) > 1 && !all_round) return false;
    constexpr float k2Pi = 6.2831855f, kPi = 3.1415927f;
    auto norm = [&](float x) {                                 // FUN_005ef010
        if (x > k2Pi) return float(std::fmod(double(x), double(k2Pi)));
        if (x < 0) return float(std::fmod(double(x), double(k2Pi))) + k2Pi;
        return x;
    };
    float d = norm(victim_facing) - norm(hit_angle + kPi);
    if (d < 0) { if (d < -kPi) d += k2Pi; }
    else if (d > kPi) d -= k2Pi;
    const float arc = all_round ? 360.0f : 150.0f;
    return arc * 0.5f * 0.017453292f > std::fabs(d);
}

inline int player_damage_after_boost(int dmg, bool boosting) {
    return boosting ? int(float(dmg) * 0.5f) : dmg;
}

// ---------------------------------------------------------------------------
// The PLAYER's reaction to a landed hit — FUN_004fd350 0x4fde25..0x4fe0ce.
// `rtype` is the attacker's +0x108 = GenerateCollision arg[9]: 2 forces BIG, 1 forces
// MEDIUM, 10 = flinch only (no new i-frames); otherwise severity is by integer
// fractions of max HP.
struct PlayerReaction {
    int motion_state = -1;   // section to enter (4 small / 0xb medium / 9 big / 5 death), -1 none
    int iframes = 0;         // grant, applied as max(cur, n)
    float knockback = 0;     // impulse magnitude along the hit angle (+0x338/+0x340 +=)
    bool face_attacker = false;  // BIG: yaw = hit angle + pi (0x4fdfc2)
    float boost_gain = 0;    // FUN_0046cc10 amount (dmg * 100, 0x4fded0)
};

inline PlayerReaction player_reaction(int dmg, float max_hp, int rtype, bool died,
                                      bool durable, int cur_state) {
    PlayerReaction r;
    // Knockback base: 0.1 (DAT_006bd19c), or 0 while already knocked down (state 9)
    // (0x4fd36b..0x4fd391). Applied in the epilogue even when dmg <= 0.
    r.knockback = cur_state == 9 ? 0.0f : 0.1f;
    if (dmg <= 0 || cur_state == 5) return r;
    if (died) { r.motion_state = 5; return r; }  // (revive item check FUN_00460dc0 not modelled)
    if (durable) { r.iframes = 16; return r; }   // victim +0x9d8 (anime_Durable): no flinch
    r.boost_gain = float(dmg) * 100.0f;
    const int mhp = int(max_hp);                 // cvttss2si [edi+0x94]
    if (rtype == 10) { r.motion_state = 4; return r; }       // i-frames: max(cur, 0) = keep
    if (rtype == 2 || dmg >= mhp / 2) {                      // 0x4fdf0c..0x4fdf1c
        r.motion_state = 9; r.iframes = 16;                  // 0x4fdf9f
        r.knockback += 1.2f;                                 // DAT_006bd484
        r.face_attacker = true;
        return r;
    }
    if (rtype == 1 || dmg >= mhp / 5) {                      // 0x4fdf1e..0x4fdf3e
        r.motion_state = 0xb; r.iframes = 12;                // 0x4fdf65
        return r;
    }
    r.motion_state = 4; r.iframes = 8;                       // 0x4fdf46
    return r;
}

// The i-frame countdown (0x466cc1..0x466cee, FUN_00465170): one per tick, but frozen
// while the player plays a hurt section (4, 9 or 0xb) — the grant starts counting only
// after the hurt clip ends.
template <typename Int>
inline void tick_iframes(Int& iframes, int motion_state) {
    if (iframes != 0 && motion_state != 4 && motion_state != 9 && motion_state != 0xb) --iframes;
}

// ---------------------------------------------------------------------------
// The ENEMY's reaction to the player's hit — FUN_004fbb40 0x4fd0cc..0x4fd109. The flinch
// needs a real hit (dmg >= 1; a 0 hit plays the clank SE 10 instead), and is skipped
// for rtype 3 or a durable victim (+0x9d8). Returns the section to enter, or -1.
inline int enemy_flinch_state(int dmg, bool died, int rtype, bool durable, int cur_state) {
    if (dmg < 1 || cur_state == 5) return -1;
    if (died) return 5;
    if (rtype == 3 || durable) return -1;
    return 4;
}

// ---------------------------------------------------------------------------
// Knockback. Both paths ADD an impulse to the victim's +0x338 (x) / +0x340 (z):
//   +0x338 += cos(angle) * K,  +0x340 += sin(angle) * K   (cos = FUN_0066fd70)
// with `angle` in the engine's heading basis (0 -> +X), i.e. atan2(dz, dx).
//
// Player -> enemy: K = L * attacker+0x104 (GenerateCollision arg[12], per-swing data).
// L = 0.1 for a non-player attacker; for the player it is overwritten at 0x4fbe2d to
// CASTINFO+0x48 (the per-tick walk speed, +0x30c) * 1.7, times the run multiplier
// +0x410 while running (+0x4ca).
inline float player_knock_scale(float walk_speed, bool running, float run_mult) {
    const float l = walk_speed * 1.7f;
    return running ? l * run_mult : l;
}

// The player's knock ANGLE is not player->victim. FUN_004fbb40 0x4fbcda..0x4fbd86
// projects a point 65535*dist BEHIND the player along its facing (via the 256-entry
// cos table at 0x68f350, index = (int)(yaw*40.7437) & 0xff) and takes atan2 from that
// point to the victim — so enemies are driven along the player's facing, quantised to
// 1/256 turn. `yaw` is in the engine basis: atan2(facing.z, facing.x).
// [not modelled] player state +0x404 == 2 uses the plain radial angle instead.
inline float player_knock_angle(float yaw, float px, float pz, float py,
                                float vx, float vz, float vy) {
    constexpr float kTwoPi = 6.28318530718f;
    const int idx = int(yaw * 40.7436638f) & 0xff;
    const float c = std::cos(float(idx) * kTwoPi / 256.0f);
    const float s = std::cos(float((idx - 64) & 0xff) * kTwoPi / 256.0f);
    const float dx = vx - px, dy = vy - py, dz = vz - pz;
    const float D = std::sqrt(dx * dx + dy * dy + dz * dz) * 65535.0f;
    return std::atan2(vz - (pz - s * D), vx - (px - c * D));
}

struct Impulse { float x = 0, z = 0; };
inline Impulse knock_impulse(float angle, float k) {
    return {std::cos(angle) * k, std::sin(angle) * k};
}

// How a mover spends the impulse. Both movers cap THIS TICK's displacement at 0.36
// (|v|^2 > 0.1296 -> scaled; the stored velocity is not capped), damp the velocity with
// friction f = 0.135 (DAT_006bd1cc), and zero it below |v|^2 0.000225 or on a dead actor.
// The player's step timer +0x400 (the 0x112 tap-step, 13 ticks) halves f to 0.0675
// (DAT_006bd164) and skips the low-speed zeroing (0x468e84 / 00465170.c L2002, L2406).
// They differ in the damping law:
//   2-D sprite mover FUN_0045f4d0 (enemies)  0x45fdd0..: v *= 1 - f*0.95, f x0.75 airborne
//   player mover     FUN_00465170            0x468e84, 0x469cd4..: v *= 1 - f, f x0.3 airborne
//     (an extra x0.95 when airborne with +0x4d4 set — [UNKNOWN] flag, not modelled)
// Returns the displacement to apply this tick.
enum class Mover { Sprite2D, Player };
inline Impulse step_knockback(Impulse& v, bool grounded, bool dead, Mover m = Mover::Sprite2D,
                              bool step = false) {
    Impulse disp = v;
    const float m2 = v.x * v.x + v.z * v.z;
    if (m2 > 0.1296f) {
        const float s = 0.36f / std::sqrt(m2);
        disp.x *= s; disp.z *= s;
    }
    const bool player = m == Mover::Player;
    const float f = (step ? 0.0675f : 0.135f) * (grounded ? 1.0f : (player ? 0.3f : 0.75f));
    const float damp = player ? 1.0f - f : 1.0f - f * 0.95f;
    v.x *= damp; v.z *= damp;
    if ((!step && m2 < 0.000225f) || dead) v = {};
    return disp;
}

}  // namespace cleria
