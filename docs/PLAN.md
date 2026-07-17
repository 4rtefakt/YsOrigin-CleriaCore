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

1. **DONE — `src/cleria/actor.hpp`, the `Actor` struct.** Our own layout, every field
   citing the offset it was derived from; unvalidated fields absent.
2. **DONE — `ActorWorld`, the pool** (`world.hpp`/`.cpp`). Keyed on the per-instance id
   (+0x128), not the cast id. A deque, not a vector, so the Actor* the registry hands
   out survive growth. Iteration order left explicitly undefined until RE answers it.
3. **DONE — `XsoVm` drives the world.** Ops `0x12c` (spawn) and `0x13f` (is-alive)
   wired through `XsoHost`, a primitive-typed interface that keeps xso.cpp free of the
   world/stat headers. Null host => inert, so every prior XSO test runs unchanged.
4. **DONE — the validated chain reaches spawn.** An enemy spawn runs
   `enemy_index_for_cast()` → ENEMYINFO → GAMEINFO and lands real hp/atk/def/exp on the
   actor.
5. **DONE — `cleria-view` spawns through `ActorWorld`.** Enemies are real Actors now,
   not a bespoke `EnemyInst`, and the SCT->stat chain that was copied THREE times (view
   interactive, view headless, world.cpp) is collapsed onto `ActorWorld::spawn`. SCT is
   a faithful path, not a stopgap — its consumer `FUN_004341a0` is a real creation site.
   The `.XSO` script spawn path is additive and layers on once the room-load RE lands.
   Verified GPU-free by a worldtest check spawning a real room's SCT (`--play` can't run
   headlessly; the crash is pre-existing, confirmed against HEAD).
6. **DONE — `cleria-worldtest`.** 36 checks: the spawn sequence, the id scheme, the
   player fork, damage on a live actor (a lv1 attacker kills a 60-HP enemy in ~7 hits,
   as combat.hpp predicted, and deals a lv60 enemy 0 — the min_dmg floor), and END TO
   END: `EVNT_3002_THOR.XSO` run dynamically spawns five cast-35 actors as ids
   167..171, an exact match to ENTITY.md's static `xso_spawns.py` scan. First test that
   can catch the *combat* code being wrong, not just the tables.

### Was blocked on `build/actor_re/` — now RE'd (ACTOR.md), and mostly wired

The actor-lifecycle pass came back and was verified against two independent prior
artifacts (`combat_re/damage.json` reproduced the death negative; `combat_re/
ai_dispatch_table.txt` confirmed the 94-entry AI table). What that unblocked:

7. **DONE — update order.** Player first, then the live list in spawn (index) order,
   count cached so mid-step spawns wait a step, order-preserving removal. `ActorWorld::
   step()`. It is a *correctness* invariant (hit processing runs inside the victim's
   update), not a detail — so it is now defined, where before it was deliberately left
   undefined.
8. **DONE — fixed 60 Hz, NOT delta-scaled.** ACTOR.md settled it: the sim drains whole
   1000/60-ms quanta; per-step dt is constant; the leftover fraction is
   presentation-only interpolation. `kSimHz`/`kSimDt` in world.hpp. This is the fact the
   decoupled-framerate goal hangs on: render decoupled, simulate fixed.
9. **DONE (model) — death is TWO flags, and bit 0x10 is not the one.** `+0x3bc` bit 0x10
   is a written-but-unread "died marker"; removal is driven by `+0x11` bit 2
   (kDestroyRequested), set after the AI's death state, consumed by a **frame-boundary**
   sweep (`sweep_dead()`). combat.hpp/actor.hpp corrected; the old "deferred death flag"
   framing is retired. The per-actor AI body (incl. the death state machine and motion
   state 5) is still [UNKNOWN] — `update_actor()` is an explicit no-op seam.
10. **RE'd — room load is entirely XSO opcodes (build/roomload_re/ROOMLOAD.md).** And
    it CORRECTED ACTOR.md's [H-medium] guess: `FUN_004358b0` is a name→record lookup,
    `FUN_00442260` is boot-time init — neither is the loader. The real chain is
    `FUN_00442dd0 → FUN_00431260 → FUN_00431840 → StartScript → the XSO VM`, and the
    asset loads + actor spawns are VM OPCODES: `0x9b`–`0xa1` load SOB/SEF/SFO/SCM/
    SEN+SEV/SNF, `0xa0` does the bulk SCT spawn, `0x12c` does scripted spawns. All
    VERIFIED against our data (our disassembler shows every room script opening with
    exactly those load ops in the claimed order; stride 0x2b0 and 1290 SCT records
    match).

    ** The population question is RESOLVED: SCT and 0x12c are two additive sources, not
    two views of one set. ** Per room the script runs `0xa0` once (bulk SCT) then
    `0x12c` 0..N times (scripted). S_3004 is pure-SCT (23, 0 scripted); S_1000 is a hub
    (SCT=just ADOL, 146 scripted). ** The `0xa0` area filter (record+0x7c vs sub-area)
    is DEAD in the shipped game — 1290/1290 records are area 0 ** (same as the
    group-overlap quirk: real code, never exercised).

11. **DONE — the faithful populate.** The view now runs `S_xxxx.XSO` from entry 0 through
    a world-connected VM: op `0xa0` bulk-spawns the SCT (`ActorWorld::spawn_placements`),
    op `0x12c` adds scripted actors, asset ops are inert. `CastTable::sprite_stem` gives
    the cast_id→`M_100` mapping so `0x12c` actors (casts the SCT never named) load their
    sprites too. `populate_room` + `EnemyFx::init_from_world` + `build_enemy_render`
    replace the SCT-only path. Proven GPU-free by worldtest: S_3004 (pure-SCT) 23 from
    `0xa0`; S_1000 (hub) 1 from SCT alone, 11 with the script — op `0x12c` supplying the
    hub NPCs the SCT-only path missed (count is story-flag-gated, so a fresh game reaches
    a subset, not the 146 static ops). 52 checks.

### Runtime room change — RE complete, partly wired

teardown [C-DIS] (ACTOR.md), load [C-DIS] (ROOMLOAD.md), populate WIRED, warp trigger +
fade [C-DIS] (build/warp_re/WARP.md). The exit ARRIVAL position is verified and wired
(the view spawns you at the door). What is NOT wired: the fade state machine (0.05/step,
0.333 s each way — verified timing, cosmetic) and moving the room-change logic out of the
view's ad-hoc `transition` into a proper engine module. Both optional; the room already
changes on trigger.

### The next BIG milestone: the enemy AI tick (build/aitick_re/AITICK.md)

RE'd [C-DIS], NOT wired — `update_actor()` is still a no-op, so enemies spawn but do not
move. The tick shape is known (activation-gate on distance to the player, cast brain,
walkable clamp, anim-integrate, push-out, attack scan). The blocker is that **locomotion
is ANIMATION-DRIVEN**: walk speed is a clip's ROOT MOTION × a movement-mode constant, not
a speed field. So a moving enemy needs **animation root-motion extraction** (diff a clip's
bone matrix frame-to-frame) which we do not have — that extraction is the real work, and
it is a milestone, not a seam.

Two findings from this pass already folded in: the superseded `+0x30c·X` speed note is
corrected (`+0xdc` is the attack-box centre, movement is root-motion), and a
**[DISCREPANCY]** is flagged in physics_player.hpp — the player's ½gt² vertical model vs
the generic-actor `pos+=vel` model AITICK found; likely different systems, unresolved,
do not reconcile blindly.

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
