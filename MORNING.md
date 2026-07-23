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

## Still known-broken (not regressions — never worked)

- **Statue cleansing** mechanic (Yunica/Hugo must cleanse; Toal skips) — not implemented.
- **Inventory** — the 2 intro items / the Flabellum skill can't be seen (no inventory model).
- **S_1001 chest / S_1004 altar** "opened" sprites — they vanish instead of showing an emptied
  sprite (object-state swap not done).
- **Ward effect** around the chest/altar — not appearing.
- **Group-enemy red glow** — the ward enemies should glow red; likely the wrong (non-group) enemy
  set is spawning under the current flag state. Tied to the same flag-state work as the intro.

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
