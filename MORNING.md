# Morning checklist — 2026-07-24 overnight

## 🎉 THE BIG ONE: the S_1000 intro actually plays now

I stopped guessing flags and read `BATTLESTART1.XSO` — the intro fires only when **flag 141 == 1**
(the opening movie sets it; we skip the movie, so it was never set). Set that + implemented the
**Chr movement and camera ops** the cutscene needs, and **self-tested it via remote control.** It
works — I watched it:

1. ✅ "The Tower Entrance" caption (upper-left, correct)
2. ✅ Cutscene camera down the entrance walkway
3. ✅ Yunica **walks to the central platform** (this is the `Chr_MoveTo` op — never worked before)
4. ✅ 3 intro mobs spawn around her
5. ✅ Correct dialogue: *"Apprentice knight Yunica Tovah, at your service!"*

**Please double-check (events-ON):**
```powershell
$env:CLERIA_EVENTS=1; & "C:\Users\kores\repos\YsOrigin-CleriaCore\build\Release\cleria-view.exe" "C:\Users\kores\repos\YsOrigin-CleriaCore\assets"
```
New Game → Yunica → the intro should play (entrance → walk to platform → mobs → dialogue).
Keep pressing **Enter/E** to advance the dialogue and see how far the intro gets.

## Context you gave me (recorded)
Real new-game flow = **video movie → in-engine Roda-tree cutscene (per character, a room you never
revisit) → tower entrance**. We skip the movie + Roda-tree and jump to the tower (seeding flag 141).
The Roda-tree scene is a whole cutscene room to add later; the tower-entrance intro is what's playing.

## What to look for / tell me

- **Does the intro COMPLETE?** After the battle/dialogue it should reach the cleanse tutorial and
  give control back. My read says it won't *stall* (the ops it still lacks are visual-only, not
  blockers), but I couldn't sit through the whole thing by remote. If it gets stuck somewhere,
  tell me **the last line/thing you saw** — that pins the blocking op.
- **NPC poses** will be wrong (they'll idle-cycle, not do their scripted gestures) — that's the
  `Chr_Animate` op, not done yet. Expected.
- **Player's own portrait** in dialogue is blank (Yunica lines) — her bust source isn't found yet.
  NPC busts (Roy etc.) DO show. Expected.
- **The battle**: in my test the mobs were still there during dialogue — the intro may play the
  battle as scripted rather than "you kill them." Tell me if you actually get to fight them.

## ⭐ Late-night addendum (continued after the intro win) — I READ the S_1001 + S_1000 roots

Instead of guessing, I disassembled the actual room root scripts. Two important corrections:

**1. "No NPCs in S_1000" is CORRECT for the first visit — not a bug.**
I decoded `S_1000.XSO`. On the first visit (`flag 141 == 1`) the root only starts the intro/battle
scripts and hides one object. The townsfolk (Kadena=92, Roy=87, Galeon, Riko, entities 91/94/199/200…)
are each spawned under **later story flags** (225 / 235 / 236 / 237 / 242) — i.e. they only appear
when you come back to the entrance later in the story. So an empty entrance on the intro run is faithful.
→ The "6 enemies ALWAYS there" are actually **battle_0's own first wave** (6 mobs). S_1000.SCT has
**zero** placement enemies (only the player), so nothing is "statically" there — the 6 come from the
live battle script and only look permanent if the fight never resolves in the view. See the updated
`build/event_re/S1000_FIRSTVISIT.md` for the corrected analysis and the one thing to check live.

**2. The S_1001 barrier-chest mechanic — fully decoded from `S_BOX01.XSO` / `KEKKAIZAKO_01.XSO` / the root:**
- **flag 301 = "barrier mob killed / barrier down"**, **flag 302 = "chest already opened"**.
- Killing the red-glow barrier mob (`KEKKAIZAKO`) sets **flag 301 = 1**.
- The chest (`S_BOX01`) *refuses to open* while `flag 301 == 0` (barrier up) — so opening it before the
  kill is correctly blocked. After the kill it opens, gives item 89, sets flag 302 = 1.
- **The chest is meant to switch to an OPENED model via op `0x175`, NOT be deleted.** The root even
  re-applies `0x175` on re-entry when `flag 302 == 1`, so a looted chest *stays* as an opened chest.
  → Confirmed the vanishing chest is exactly a missing `0x175` object-model swap. A background RE pass
  is pinning down how the closed/opened chest models are stored before I wire it (so I don't guess).

## Still known-broken (not regressions — never worked)

- **Enemy spawn-gating** — battle mobs appear statically instead of being spawned/gated by the room
  root's story flags (the "6 always-there enemies"). Root cause identified; fix pending the RE pass.
- **Statue cleansing** mechanic (Yunica/Hugo must cleanse; Toal skips) — not implemented.
- **Inventory** — the 2 intro items / the Flabellum skill can't be seen (no inventory model).
- **S_1001 chest / S_1004 altar** "opened" model — vanishes instead of showing the emptied model
  (the `0x175` object-model swap; mechanic now fully decoded, see addendum above).
- **Ward effect** around the chest/altar — not appearing.
- **Group-enemy red glow** — the ward enemies should glow red; likely the wrong (non-group) enemy
  set is spawning under the current flag state. Tied to the same spawn-gating work.

## Ops added in the late-night pass
- **Chr_DirChr / Chr_LookAtChr (0x134/0x136)** and **Chr_LookAtPos (0x137)** — cutscene actors now
  turn to face each other / the player / a point during dialogue.
- The world's **player actor is now synced to the live position every frame**, including mid-cutscene,
  so facing + in-sphere/box tests resolve the player correctly.
- All still green: build both trees, cotest (full script sweep) passes exit 0.

## ✅ Chest "vanishing" is FIXED (opened chests now render OPEN)
I RE'd how chests work (a background agent confirmed it from the model files): a chest is **one model
with a built-in `OPEN` animation clip** that swings its hinged lid (`futa`) up — there is **no separate
"opened" model and the chest is never hidden/deleted**. The game re-poses an already-looted chest to
its OPEN pose the moment you re-enter the room (op `0x175(box,OPEN,0,-1)`).

I wired exactly that: on room load, any chest whose opened-flag is set (S_1001: flag 302/314/478) is
built posed to the final frame of its `OPEN` clip. **Fresh save = all flags 0 = every chest closed**, so
nothing changes until you actually loot one — proven by **all 7 golden screenshots still matching**.

**To see it:** open a chest (needs its barrier down first — see below), leave the room, come back — the
chest is now sitting open. *Pending polish:* the lid doesn't yet swing open in real-time the instant you
open it (that needs a per-object re-pose; a full room reload would cause an unfaithful stall, so I didn't
fake it). It shows open on re-entry, which is the persistent-state-correct behavior.

## ⭐⭐ NEW: I PROVED the barrier + intro-battle runtime logic is CORRECT (strict headless tests)
You said "strict and robust" — so instead of guessing, I drove the **real** room scripts through the
**real** event runtime (ScriptManager) against a **real** game world (ActorWorld) in headless tests
that tick frame-by-frame, and asserted the actual behavior. Two big results (both now permanent
regression tests in `cleria-worldtest`):

**Barrier (S_1001) — the logic is correct, the ward breaks ONLY on the kill:**
- The barrier mob spawns as a live group-1 seed on entry ✔
- The ward does **NOT** break on entry — flag 301 stays 0 for 60 frames while the mob lives ✔
- Killing the mob sets flag 301 → 1 and starts the KekkaiBreak cutscene ✔
So the "barrier fires on entry regardless of the kill" is **not** a runtime-logic bug. If you still
see it events-on, it's the *view wiring* (below), and this test is the yardstick to fix against.

**Intro battle (S_1000 battle_0) — the whole multi-wave fight works end-to-end:**
- Waves spawn (the scripted mobs, counted) ✔
- The kill-gate is **real**: if you never kill anything, the script stays stuck at the gate ✔
- If you kill each wave, the battle **runs to completion** (battle_0 finishes, sets flag 165) in
  ~4510 frames — i.e. "mobs spawn → you kill them → the cutscene resumes" genuinely works ✔
This proves the mechanic you narrated is faithful at the runtime level; what remains is purely the
view deciding *which* enemies exist when (see below).

## The barrier "fires on entry regardless of the kill" — root cause found (not yet wired)
The background RE proved the barrier mob **is** correctly gated (`if flag 301 != 1` in the room root),
and CleriaCore honours that gate. The bug: the little **watcher script** that sets `flag 301 = 1` when
you kill the barrier mob (`KekkaiZako_01`, which polls the mob's death and then plays the break cutscene)
is launched with `StartScript`, and `StartScript` is **inert in the room-populate path** — so flag 301
never flips and the barrier keeps coming back. This is a live-runtime wiring job (the events runtime that's
still opt-in), and it needs you playing to verify, so I documented it precisely rather than guess-wiring
it blind. Full trace: `build/event_re/S1001_BARRIER_CHEST_RE.md`.

## New RE docs written tonight (all code-traced, evidence-tagged)
- `build/event_re/S1001_BARRIER_CHEST_RE.md` — barrier mob, chest model/flags, the whole trigger chain.
- `build/event_re/S1004_ALTAR_RE.md` — the Flabellum altar (flag 304), stepping-stone puzzle, `0x175` states.
- `build/event_re/STATUE_CLEANSE_RE.md` — goddess-statue cleanse (flag 300), sealed vs cleansed models.
  The cleanse even uses the player-LookAtPos op I added tonight. Fully decoded, ready to implement with you.

## Status at end of session
- **All 7 test suites pass** (347s incl. the 186-room sweep). No regressions.
- **Default (events-OFF) build unchanged** — your normal game is exactly as before.
- Everything committed. Latest: the verified S_1000 intro.

## The intro's script chain (for reference, from the string pool)
S_1000 root (flag 141=1) → **BattleStart1** (walk-in cutscene) → **battle_0** (Unfixes for the
interactive fight; spawns 6 mobs; waits for their deaths) → **TalkC270** (resume dialogue) →
**SwordTutorial1** (the cleanse tutorial). If it stalls, it's most likely at the battle_0 kill-wait
(needs the mobs killable + the death-gate to fire) — that's the thing to watch and report.

## Ops implemented today (the cutscene runtime that was missing)
Chr_Pos, Chr_MoveTo, Chr_Dir, Wait_ChrMoving, Camera_Set/Return, the screen fader (Fader_*),
Chr_Delete, event NPC spawn→world, + the depth-buffer fix that made ALL overlays render.

## Rule I'm following now
No more flag-roulette. Read the actual script, find the actual trigger/op, implement it, verify.
The intro win came from doing exactly that. The remaining items are the same kind of work.

## Everything is committed; default (events-OFF) build is clean and unchanged.
