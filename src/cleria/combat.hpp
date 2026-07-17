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
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cleria {

// The attacker's contribution. Fields are named by what the damage path actually
// reads: +0xa4 is loaded from the ATTACKER, +0xa8 from the TARGET and subtracted.
// (+0xac and +0xb0 are NOT read by this path and stay unnamed — see ENTITY.md.)
struct AttackStats {
    // actor +0xa4. ** This is the level curve PLUS equipment, not the curve alone. **
    // FUN_004208f0 adds the weapon (GAMEINFO's 4x11 [char][tier] table) and armour
    // (INVINFO +0x24) into the stat record before it is mirrored here. A level-1 char0
    // is 25 + 23 = 48, not 25 — and feeding the bare curve makes the formula go
    // NEGATIVE against a level-1 enemy, which is a puzzle this project chased for a
    // while before noticing the curve was only the base.
    float atk = 0;
    // actor +0xfc — additive, inside the linear term. ** This is PER-ATTACK-MOVE power,
    // NOT an equipment bonus. ** FUN_00477f00 (the attack-descriptor setup) writes it
    // from XMM3, a caller-supplied float, and all 66 writers live in actor TUs
    // (go_actor_player.cpp, go_mesh_boss01..07.cpp) — not one in main/game/ where the
    // equipment code lives. Values are per-move constants (4.0, 14.0, 20.0, ...). The
    // per-move table itself is [UNKNOWN].
    float bonus = 0;
    float min_dmg = 0;  // actor +0x100 — a FLOOR, applied after the clamp to 0
};

// `raw = ATK^0.66 * (ATK - 0.98*DEF + 10 + bonus) / 22.92`, then floored at min_dmg.
// Note the clamp order: max(min_dmg, max(0, raw)) — a min_dmg above 0 wins even when
// the linear term has gone negative, so a heavily-outclassed attacker still chips.
//
// Sanity, with the REAL level-1 numbers (base curve + starting gear): ATK 48 vs a
// level-1 enemy's DEF 40 gives 10.56, i.e. 5.7 hits to kill a 60 HP enemy. At level 60
// the same check gives 8.3 — a monotone curve at both ends, which is decent
// independent corroboration that nothing upstream is misread.
//
// ** The player-as-target branch (FUN_004fa740, cast 13) differs: ** DEF comes from
// the stat record's DEF field (DAT_0076a740 == 0x76a72c + 0x14, settled), and the
// floor is a hardcoded 0.5 rather than min_dmg. It also applies a two-stage soft
// FLOOR at 3% / 1.5% of the ATTACKER's ATK — a floor, not the cap it was first
// described as.
inline float damage_real(const AttackStats& a, float def) {
    const float raw =
        std::pow(a.atk, 0.66f) * (a.atk - 0.98f * def + 10.0f + a.bonus) / 22.92f;
    return std::fmax(a.min_dmg, std::fmax(0.0f, raw));
}

// The rounding is PROBABILISTIC [C-DIS]. The fractional part is a percent chance to
// round up, so 3.25 damage is 3 seventy-five percent of the time and 4 the other
// twenty-five — every hit is an integer, and E[damage] == d, unbiased. The roll is at
// 0x004fd521:
//
//   004fd4bd  e8 2e 43 f2 ff   CALL 0x004217f0      ; floorf
//   004fd4c2  8b 45 c0         MOV EAX,[EBP-0x40]   ; <- inlined xorshift128 starts
//   004fd504  c1 e8 11         SHR EAX,0x11         ;    w >> 17
//   004fd507  6b c8 64         IMUL ECX,EAX,0x64    ;    * 100
//   004fd521  3b f0            CMP ESI,EAX          ;    P vs R   <- THE ROLL
//   004fd523  7e 0e            JLE 0x004fd533       ;    P <= R -> no +1
//   004fd525  f3 0f 58 ca      ADDSS XMM1,XMM2      ;    n += 1.0
//
// P(round up) == P/100 to five decimals: the `0x80010003 / SAR 14` sequence is a
// division by exactly 32767 (verified bit-exactly across the whole reachable domain —
// note 32765/32766/32768 all match on a coarse sample and only fail on a full sweep).
// The generator is canonical Marsaglia xorshift128 at 0x74622c, seeded
// 123456789/362436069/521288629.
//
// This was disputed by two passes and the resolution is worth keeping, because both
// were right about what they actually looked at. One read the CALLER and found an
// RNG; the other disassembled the CALLEE and found none. Both true: FUN_004217f0 is
// `floorf` — not `chance()` (it touches no RNG) and not `roundf` (FUN_00671000 is
// floor(double), proven by `006710d6: FLDZ; RET` for 0<x<1). The xorshift is INLINED
// into the damage function five bytes past the call, so reading the callee tells you
// nothing about the roll.
//
// ** The trap, which is general to this binary: MSVC inlines this global RNG at 799+
// sites and there is no rand() symbol to xref. Inlined RNG immediately after a CALL
// is trivially mis-attributed to the callee. **
template <typename RollPercent>
inline int damage_int(const AttackStats& a, float def, RollPercent&& roll) {
    const float d = damage_real(a, def);
    const float n = std::floor(d);
    const bool up = (d - n) != 0.0f && roll((d - n) * 100.0f);
    return int(n) + (up ? 1 : 0);
}

// A POST-SCALER sits on the damage path immediately after the rounding, at 0x4fd533
// [C-DIS] — structure only:
//
//     if (*(int*)0x76bb5c > 0)  dmg = dmg * (*(float*)0x76c190) + 1.0f;
//
// 0x76bb5c is the DIFFICULTY global (identified independently, from the level-up path
// asserting on a *different* global for player character — the two were briefly
// conflated, and this is what separates them). So difficulty scales damage
// multiplicatively and then adds a flat 1, and only when difficulty > 0 — meaning the
// lowest setting takes this branch not at all rather than taking it with scale 1.0.
//
// ** Both globals are runtime-set and their values are [UNKNOWN]. ** We know the shape
// of this step, not its magnitude, so it is NOT applied: `damage_int` returns the
// unscaled roll. Wiring it with a guessed multiplier would be worse than omitting it,
// because it would look correct. The values live in whatever writes 0x76c190 — that
// is the next thing to disassemble before damage parity is claimable.
struct DifficultyScale {
    int difficulty = 0;   // *(int*)0x76bb5c
    float factor = 1.0f;  // *(float*)0x76c190 — [UNKNOWN] at every setting
};

inline int apply_difficulty(int dmg, const DifficultyScale& s) {
    if (s.difficulty <= 0) return dmg;
    return int(float(dmg) * s.factor + 1.0f);
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

// Invulnerability frames. `actor +0x734`, a DWORD (not a byte — the footstep counter
// at +0x4bf is a byte, and they are not the same idiom).
//
// ** This gates damage FOR THE PLAYER ONLY ** — the engine's test is
// `cast != 13 || actor->i_frames == 0`, i.e. every non-player cast is always hittable.
// Do not generalise it to enemies; enemy hit-spam is vanilla behaviour.
//
// ** GRANTING IS A max(), NOT A STORE — so this function's return value must not be
// assigned. ** The sites we have read do:
//
//     actor->i_frames = max(actor->i_frames, N);
//
// i.e. "grant AT LEAST N ticks, never shortening an existing longer invulnerability".
// Two such sites are visible with N = 16 and N = 3, and a plain store of 9999 exists
// at 0x46fc90 (~166 s at 60 Hz — effectively "off"; what state that is, is [UNKNOWN]).
// Note 3 and 16 do not both fall in the {8,12,16} set below, so there are MORE grant
// sites than the hit-severity ones.
//
// [UNKNOWN] — and these two are why nothing here is applied automatically:
//   * The 8/12/16 values came from a pass that described them as stores. Given the
//     sites above are maxes, whether the severity grants are stores or maxes is NOT
//     established. Use `grant_iframes`, which encodes the max() we HAVE seen.
//   * The DECREMENT site has never been found. It must exist — +0x734 is raised by
//     max() and gated on == 0, so without a decrement the player would be permanently
//     invulnerable after one hit — but "once per tick" is an inference from that
//     argument, not something read out of the exe.
inline int iframes_for(HitReaction r) {
    switch (r) {
        case HitReaction::Big: return 16;
        case HitReaction::Medium: return 12;
        default: return 8;
    }
}

// The grant, as observed: raise, never lower.
inline void grant_iframes(int& i_frames, int n) { i_frames = std::max(i_frames, n); }

}  // namespace cleria
