// The damage formula — disassembled from `FUN_004fd350`, the central applier
// (7 call sites). See build/combat_re/damage.json.
//
// The real signature is `__fastcall(GoActor* attacker /*ECX*/, GoActor* target /*EDX*/,
// float* knockback /*[esp+4]*/)`. Ghidra renders it `FUN_004fd350(undefined4*)` — it
// drops EDX *and* every XMM argument. That is this codebase's recurring trap and it
// was live here again: read the disassembly, not the decompiler's signature.
//
// ** The two magic constants are NOT what Ghidra's data annotations say. ** It reports
// `0x006bd414` as 0x48 and `0x006bd9b0` as 0x29 — those are the LOW BYTES of the
// floats 0x3f7ae148 (0.98) and 0x41b75c29 (22.92). Trusting the annotation yields 72
// and 41 and a formula that produces nonsense. Float constants must be read as raw
// 4-byte words.
#pragma once
#include <cmath>
#include <cstdint>

namespace cleria {

// The attacker's contribution. Fields are named by what the damage path actually
// reads: +0xa4 is loaded from the ATTACKER, +0xa8 from the TARGET and subtracted.
// (+0xac and +0xb0 are NOT read by this path and stay unnamed — see ENTITY.md.)
struct AttackStats {
    float atk = 0;      // actor +0xa4
    float bonus = 0;    // actor +0xfc  — additive, inside the linear term
    float min_dmg = 0;  // actor +0x100 — a FLOOR, applied after the clamp to 0
};

// `raw = ATK^0.66 * (ATK - 0.98*DEF + 10 + bonus) / 22.92`, then floored at min_dmg.
// Note the clamp order: max(min_dmg, max(0, raw)) — a min_dmg above 0 wins even when
// the linear term has gone negative, so a heavily-outclassed attacker still chips.
inline float damage_real(const AttackStats& a, float def) {
    const float raw =
        std::pow(a.atk, 0.66f) * (a.atk - 0.98f * def + 10.0f + a.bonus) / 22.92f;
    return std::fmax(a.min_dmg, std::fmax(0.0f, raw));
}

// The engine does NOT round or truncate: it rounds PROBABILISTICALLY. The fractional
// part is a percent chance to round up, so 3.25 damage is 3 damage 75% of the time
// and 4 the other 25% — the average is exact over many hits while every individual
// hit stays an integer. `roll` must return true with the given percent probability.
template <typename RollPercent>
inline int damage_int(const AttackStats& a, float def, RollPercent&& roll) {
    const float d = damage_real(a, def);
    const float n = std::floor(d);
    const bool up = (d - n) != 0.0f && roll((d - n) * 100.0f);
    return int(n) + (up ? 1 : 0);
}

// Applying it. Two things here are easy to get subtly wrong, and both are load-bearing:
//
//  1. The death test is `hp < 1.0f`, NOT `hp <= 0.0f`. Sub-1 HP is death.
//  2. Death is a DEFERRED FLAG (`actor +0x3bc |= 0x10`), not a call. Nothing is
//     destroyed here. That is precisely why our earlier pass could not link a
//     `curHP <= 0` test to the vtable-dispatched destructor: there is no such test
//     and no such call. Enemies additionally play animation state 5 in the same
//     frame; for the player, a later pass can XOR the bit back OFF (auto-revive), so
//     treating this flag as terminal at the point of damage would break revival.
enum : std::uint32_t { kActorDead = 0x10 };  // actor +0x3bc

struct HitResult {
    int damage = 0;
    bool died = false;
};

inline HitResult apply_damage(float& hp, std::uint32_t& flags, int dmg) {
    HitResult r;
    r.damage = dmg;
    hp -= float(dmg);
    if (hp < 1.0f) {  // NOT <= 0
        flags |= kActorDead;
        hp = 0.0f;
        r.died = true;
    }
    return r;
}

// Hit severity, which selects the reaction. Thresholds are fractions of the target's
// MAX HP, so the same blow staggers a weak enemy and barely nudges a boss.
enum class HitReaction { Small, Medium, Big };

inline HitReaction hit_reaction(int dmg, float max_hp) {
    if (max_hp > 0 && float(dmg) >= max_hp * 0.5f) return HitReaction::Big;  // + knockback
    if (max_hp > 0 && float(dmg) >= max_hp * 0.2f) return HitReaction::Medium;
    return HitReaction::Small;
}

// Invulnerability frames. `actor +0x734`, set to 8/12/16 by hit severity.
//
// ** This gates damage FOR THE PLAYER ONLY ** — the engine's test is
// `cast != 13 || actor->i_frames == 0`, i.e. every non-player cast is always hittable.
// Do not generalise it to enemies; enemy hit-spam is vanilla behaviour.
//
// [UNKNOWN] The DECREMENT site was never found. That this counts down once per tick
// is an assumption — a reasonable one, but it has not been read out of the exe.
inline int iframes_for(HitReaction r) {
    switch (r) {
        case HitReaction::Big: return 16;
        case HitReaction::Medium: return 12;
        default: return 8;
    }
}

}  // namespace cleria
