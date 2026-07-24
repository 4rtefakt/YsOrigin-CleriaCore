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
→ The thing you actually saw wrong ("6 enemies ALWAYS there") is the **enemy spawn-gating** bug: the
battle mobs should be spawned by the intro's `battle_0` script gated on flag 141, not sitting there
statically from placement. That's the one real fault, and it's being worked (see below).

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
