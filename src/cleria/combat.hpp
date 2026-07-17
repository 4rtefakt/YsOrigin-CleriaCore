// The damage formula — disassembled from `FUN_004fd350`, the central applier
// (7 call sites). See build/combat_re/damage.json and build/dmgpath_re/dmgpath.json.
//
// ############################################################################
// ## THE PATH FORKS. EVERYTHING BELOW IS THE **ENEMY-TARGET** FORMULA ONLY. ##
// ############################################################################
//
// At 0x4fd3e1 the applier does:
//
//     if (target->cast == 13)  { CALL FUN_004fa740; JMP 0x4fd54f; }
//
// — i.e. when the target is the PLAYER it bypasses this entire formula *and* the
// difficulty post-scaler. `FUN_004fa740` is a different function with different rules:
// DEF comes from the global 0x76a740 (== the stat record's DEF field), there are two
// soft FLOORS at 3% and 1.5% of the attacker's ATK, a hardcoded 0.5 minimum instead of
// min_dmg, and the return is max(1.0, ...).
//
// ** Routing player-incoming damage through `damage_real` is WRONG. ** It is not a
// close approximation; it is the other branch. `player_incoming_damage` below is a
// deliberate compile-time stop rather than a guess.
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
    // from XMM3, a caller-supplied float.
    //
    // ** The "[UNKNOWN] per-move table" is CLOSED (build/attack_re/ATTACK.md): the power
    // is arg[11] of the ASO anime_GenerateCollision op ** that arms each attack — the
    // same op that sets the reach and the active window. It is per-attack DATA in the ASO
    // section, not a code table. AsoVm::hitbox().power surfaces it; verified against
    // C_010 (Yunica's basic swing = 8.0). So the melee is fully sourced: reach 0.6
    // (arg[2]), a 30-frame active window (arg[6]), power 8.0 (arg[11]) -- all from the
    // animation script, which is why no code table exists.
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
// ENEMY-TARGET ONLY. See the fork note at the top of this file.
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
// The generator is canonical Marsaglia xorshift128 at 0x74622c.
//
// ** The seeds in .data are DECOYS. ** FUN_005d6700, called once from 0x40e257, does
// `x = timeGetTime()` (005d6759 CALL ESI; 005d6760 MOV [0x0074622c],EAX) and merely
// re-stores y/z/w with the constants they already hold. So 123456789/362436069/
// 521288629 — which this comment used to quote as "the seeding" — is what the image
// ships, not what the game runs.
//
// ** Damage is therefore NOT replay-stable in the retail game. ** That is liberating
// rather than blocking: there is no canonical stream to match, so CleriaCore should
// seed from an explicit recorded seed and get the determinism the original never had.
// Matching the ALGORITHM and the ADVANCE COUNT buys fidelity; matching the seed is
// impossible.
//
// The tell for the re-seed is worth keeping: y/z/w's constants each have exactly one
// code hit, and x's 0x075bcd15 has NONE. That asymmetry IS the srand signature — the
// one seed that never appears in code is the one being overwritten.
//
// [UNKNOWN] Advance census: FIVE per call (0x4fd3dc, 0x4fd4ff, 0x4fd5f4, 0x4fdd94,
// 0x4fddc5), not the two implied above. Only the second feeds the rounding roll;
// #3/#4/#5 are untraced. Stream parity needs all five.
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
// The factor is now SOURCED [C-DIS]. FUN_00431260 is a bare table lookup:
//
//   004313f7  MOV EAX,[0x0076bb5c]                       ; the difficulty index
//   00431414  MOVSS XMM0,[EAX*0x4 + 0x68a268]            ; table[difficulty]
//   00431422  MOVSS [0x0076c190],XMM0
//
// and DAT_0068a268 is EXACTLY THREE floats: { 1.0, 1.5, 2.0 } (verified by reading raw
// 4-byte words out of the PE — entry [3] is 0x070805dc, an int that reads as the
// denormal 1.02e-34, which is how you know the table ends). Index 0 is never used: the
// gate is `> 0`. Two sibling tables share the shape — 0x689f80 (enemy HP {—, 1.2, 1.5},
// whose [0] is the int 300) and 0x68b26c (EXP {1.5, 1.65, 1.8}).
//
// ** Difficulty is NOT a single 0..3 global, and there are FIVE settings, not four. **
// It is a 3-valued index (0x76bb5c, domain {0,1,2}) PLUS two booleans — 0x76bb58
// (easy) and 0x76bc48 (very easy). The easing half of the range is not in 0x76bb5c at
// all, which is why it looked like a 0..3 enum. Effects, strictly monotone:
//
//   very easy : dmg x0.5,  enemy HP x0.75, EXP x2.0
//   easy      : dmg x0.5,  EXP x1.1
//   normal    : baseline (index 0 — the post-scaler branch is not taken)
//   hard      : enemy HP x1.2, dmg x1.5 + 1
//   nightmare : enemy HP x1.5, dmg x2.0 + 1
//
// The effects are [C-DIS]; the NAMES are [H-high] — the menu strings were never found.
//
// [H-medium], flagged rather than hidden: the factor appears to be applied on BOTH
// sides (0x4fa8fd player-incoming, 0x4fd53c enemy-incoming), so on Nightmare the
// player would also deal 2x+1, netting ~0.75x enemy time-to-kill — a glass-cannon
// curve. That branch was re-read three times without breaking it, but ONE in-game
// damage number on Nightmare vs Normal would settle it.
inline constexpr float kDifficultyFactor[3] = {1.0f, 1.5f, 2.0f};  // DAT_0068a268

struct DifficultyScale {
    int difficulty = 0;   // *(int*)0x76bb5c — domain {0,1,2}
    float factor = 1.0f;  // *(float*)0x76c190 == kDifficultyFactor[difficulty]
};

inline int apply_difficulty(int dmg, const DifficultyScale& s) {
    if (s.difficulty <= 0) return dmg;  // the gate is > 0, so index 0 never applies
    return int(float(dmg) * s.factor + 1.0f);
}

// Applying it. Three things here are easy to get subtly wrong, all load-bearing:
//
//  1. The death test is `hp < 1.0f`, NOT `hp <= 0.0f`. Sub-1 HP is death.
//  2. `actor +0x3bc |= 0x10` is a "DIED event marker", and NOTHING READS IT. Two
//     independent RE passes (build/combat_re/damage.json and build/actor_re/ACTOR.md)
//     each ran a full-program operand scan and found no `TEST [x+0x3bc],0x10`. So this
//     bit does NOT drive removal, and an earlier note here calling it "the deferred
//     death flag" was wrong about mechanism (right that no destructor call happens).
//  3. The REAL deferral: the damage code zeroes HP and enters MOTION STATE 5 (the
//     dying anim) via FUN_00446550(actor+0x8f0, 5, 0); the AI plays state 5 out, then
//     calls GoActor::Kill to set +0x11 bit 2 (kDestroyRequested), which the
//     once-per-render-frame sweep consumes to actually remove the actor. For the
//     player, a survival gate XORs bit 0x10 back OFF (auto-revive) — which is why this
//     bit was never terminal even as a marker. See actor.hpp's death note and
//     world.hpp's sweep. We still SET bit 0x10 here because the engine does; we just
//     don't pretend it removes anything.
enum : std::uint32_t { kActorDead = 0x10 };  // actor +0x3bc — the marker, not the trigger

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
// ** GRANTING IS A max(), NOT A STORE. ** The sites read do:
//
//     actor->i_frames = max(actor->i_frames, N);
//
// i.e. "grant AT LEAST N ticks, never shortening an existing longer invulnerability".
// Observed N: 16, 10 (0x45e0e8) and 3 — plus a plain STORE of 9999 at 0x46fc90 (~166 s
// at 60 Hz, effectively "off"; what state that is, is [UNKNOWN]).
//
// ** The 8/12/16 this file used to publish is UNSOURCED, and is gone. ** A program-wide
// scan of all 284 exact `[reg+0x734]` sites found the constants 3, 10, 16 and 9999 —
// no 8 and no 12 anywhere. Those numbers came from a pass that also described the
// grants as stores, and both halves are now in doubt. Rather than keep a plausible
// triple nothing supports, use `grant_iframes` with a value you can cite.
// FUN_004fd350's 24 `+0x734` accesses are where the severity grants would be, if they
// exist at all.
//
// The DECREMENT is FOUND [C-DIS] — 0x00466ce3, inside FUN_00465170
// (go_actor_player.cpp):
//
//   00466ce3  8b 87 34 07 00 00   MOV EAX,[EDI + 0x734]
//   00466ce9  85 c0               TEST EAX,EAX
//   00466ceb  74 07               JZ 0x00466cf4
//   00466ced  48                  DEC EAX                    ; <- ONE byte
//   00466cee  89 87 34 07 00 00   MOV [EDI + 0x734],EAX
//
// So "once per tick" was right — but NOT unconditional: it is skipped when
// FUN_0040b230() returns 4, 9 or 0xb.
//
// ** Why every previous pass missed it, and it generalises: `DEC EAX` is one byte
// (0x48) and contains no trace of 0x734. An operand scan is structurally incapable of
// finding it. To locate a read-modify-write, scan for the LOAD and walk forward. **
// (The lead pushed for this — FUN_0045c490, the per-tick update that counts the
// footstep byte down — was wrong: it has no +0x734 access at all, which makes sense
// because this field is player-only.)

// The grant, as observed: raise, never lower.
inline void grant_iframes(int& i_frames, int n) { i_frames = std::max(i_frames, n); }

// ---------------------------------------------------------------------------
// PLAYER-INCOMING DAMAGE — deliberately not implemented.
//
// FUN_004fa740 is a genuinely different function, not a variation on the one above,
// and the applier jumps to it before reaching any of this file's math. What is known:
//
//   - DEF comes from the global 0x76a740 (the stat record's DEF field), not target+0xa8
//   - two soft FLOORS at 3% and 1.5% of the ATTACKER's ATK (floors, not caps)
//   - a hardcoded 0.5 minimum instead of min_dmg
//   - the return is max(1.0, ...)
//   - the difficulty factor is applied here too, at 0x4fa8fd
//
// That is enough to know `damage_real` is the wrong function for it and not enough to
// write the right one. This declaration exists so that a caller reaching for it gets a
// LINK ERROR rather than silently getting the enemy formula — the failure mode this
// codebase keeps hitting is plausible-looking wrongness, not obvious absence.
int player_incoming_damage(const AttackStats& attacker, float player_def);
// ---------------------------------------------------------------------------

}  // namespace cleria
