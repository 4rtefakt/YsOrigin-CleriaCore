# CleriaCore — the spine

Status: proposed 2026-07-17. This is the first written plan; the previous one was
implicit ("RE each format, then assemble") and has finished its useful phase.

## Why now

48 modules in, **the bottleneck stopped being knowledge and became structure.**

The stat chain is closed and externally validated — our numbers reproduce the game's
own in-game monster journal 67/68, which is the only check this project has that is
not our own code against our own code. The damage formula, the curves, the equipment
table, the enemy index fold, the drop tables, i-frames, the player's vertical model:
all read, all validated, **all with nowhere to live.**

`combat.hpp` deliberately declares `player_incoming_damage()` and never defines it, so
callers link-error rather than silently take the wrong branch. That is good discipline,
and it is also a fair description of the whole project: we have organs and no skeleton.
Nothing tests the damage formula against a running enemy because there is no running
enemy.

The goal (per devilutionX / Dusklight) is a reimplementation that reads original Falcom
data. That means the next milestone is the **spine**, and it should *pull* RE rather
than have more RE pushed at it.

## What actually exists — corrected

Two claims made while scoping this were wrong, and both were wrong the same way: a
grep that returned nothing was believed. Recorded because the correction is the plan.

* **"The XSO VM only parses."** False. `XsoVm::run` (xso.cpp:116) is a working VM:
  control flow, conditional jumps, fused compares, flag stores, the xorshift RNG
  (FUN_00439120), item grants (0x116), the acquire box (0xD5), object visibility
  (0x169 / 0x175 / 0x16B / 0x16C). The regex was `^[A-Za-z].*Xso::`; the class is
  `XsoVm`, so it never matched. **Trap #7 — a negative from a branchy script is not a
  negative — produced by our own instrument, an hour after writing that trap down.**
* **"There is no spine at all."** Overstated. The director runs. It has no cast.

The real gap is exact: **sub-ops `0x12c` (spawn) and `0x13f` (is-alive) have zero
occurrences in src/.** Unimplemented sub-ops are skipped by their encoded length, so
every shipped script already runs to its end op — spawning nothing. The XSO VM is a
stage with working lights and no actors.

## The architectural decision: we do NOT mirror the game's struct

`build/entity_re/ENTITY.md` gives real offsets — `entity+0x94` max HP, `+0x98` current
HP, `+0x130` group bits, `+0x2b4` a marker colour. **We do not reproduce that layout.**

This is a clean-room reimplementation, not a decompilation. A mirrored struct would
import the engine's accidents (a ~0x9xx-byte actor, padding, dead fields, the
overlapping "group" bits that are really a 3-bit value at bits 8–10) into code that has
no reason to carry them. `Actor` is **our** struct, with fields named for meaning, and
each one cites the offset it was derived from. The offsets are provenance, not layout.

The one thing we DO reproduce exactly is **observable behaviour**, including quirks
that are load-bearing. The group overlap is the test case: a group-3 entity answers YES
to a query for group 1 or 2, because the setter writes `n<<8` and the reader masks with
`n<<8`. In the shipped game only group 1 is ever used, so it never bites — which makes
it a quirk to **document and not emulate**, unless and until a room needs it.

## Sequencing

The tick's *shape* is under RE right now (update order, fixed-timestep vs
delta-scaled, the death-flag consumer, room teardown). That work is deliberately
parallel and read-only, writing `build/actor_re/`. So this plan is split by what that
answer blocks.

### Buildable now — nothing here waits on the tick

1. **`src/cleria/actor.hpp` — the `Actor` struct.** Our own layout, from validated
   facts only: hp/max_hp, atk, def, exp reward, cast id, level, the AI handler index,
   the death flag, i-frames, and the vertical state `physics_player.hpp` already
   models. Every field cites its provenance. Fields we have NOT validated do not
   appear — an absent field is honest, a guessed one is not.
2. **`ActorWorld` — the pool.** Spawn, iterate, despawn. **Iteration order is left
   explicitly undefined and documented as such** until RE answers it, because a stable
   order we invented would look correct and be untestable. This is where inventing glue
   is most tempting and least detectable.
3. **Teach `XsoVm` the actor ops.** `0x12c` (spawn; 7th operand = group, `group<<8`,
   only 1..4 honoured, gated on the enemy class bit) and `0x13f` (is-alive,
   `hp >= 1.0`). This needs a **host interface** — `XsoVm` must spawn without `xso.cpp`
   taking a dependency on the whole world. An abstract sink keeps the VM testable
   headlessly, which is how every other module here got validated.
4. **Wire the validated chain into spawn.** `cast_id` → `enemy_index_for_cast()` →
   ENEMYINFO → the GAMEINFO curve → the actor's real hp/atk/def/exp. This is where the
   67/68-validated numbers finally reach a live object.
5. **Migrate `cleria-view` off `.SCT`.** It spawns from the placement list because that
   is what it had; **the room's `.XSO` is what the game uses.** Keep `.SCT` as the
   fallback for rooms whose scripts we cannot yet run, and say so at the call site.
6. **`cleria-worldtest`** — headless, in the house style: run a room's XSO, assert the
   spawned roster and their stats, apply the damage formula to a real actor, kill it,
   assert the death flag and the EXP award. This is the first test that can catch the
   *combat* code being wrong, as opposed to the *tables*.

### Blocked on `build/actor_re/`

7. The tick driver and **update order** (stable, or allocation-dependent?).
8. **Fixed-timestep vs delta-scaled** — and this is not a detail to assume. Both exist
   in this engine already: the UV scroll is unscaled per 60Hz tick, the vertical model
   runs off a signed timer, and neighbouring material fields *are* MULSS-scaled by a
   delta. Decoupled framerate is a stated project goal, so this decision is load-bearing
   and must be read, not chosen.
9. What consumes the deferred death flag, and where the actor is actually removed.
10. Room transition / actor-list teardown.

## The standing risk on this milestone

The spine will tempt us to invent glue — an update order, a collision step, a state
machine — that the game *has* and we have not read. That is exactly how `kCharH = 2.0f`
happened, and glue is worse than a wrong constant: a wrong constant is one number, and
invented control flow is a shape everything downstream is built to fit.

The rule stays what it has been, and it is the rule that produced the 67/68:

* Invented glue is **marked invented, at the call site**, in the same voice as
  `kWalkSpeed` and `reach` in cleria-view.
* Structural claims trace to a `FUN_` before they harden into `src/`.
* **A plausible answer is not a validated one.** The decoys here score *well* —
  `CASTINFO+0x64` matched 78/80 by being `enemy_index + 3`. A near-miss that tracks the
  truth is worse than a random guess.
* And per the drift work: when a fact lands, **fix the place someone will read**, not
  just the place it was found. Seven propagation failures so far; the count is the
  argument.
