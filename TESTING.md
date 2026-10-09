# How to test (ADHD-friendly)

Quick checklists. One action per line. Each says what you should see.
Launch = the **CleriaCore** Start-menu shortcut. Skip any section you don't feel like.

---

## ⭐ Test the cutscene fixes (needs events ON)

Events are OFF by default right now. To turn them ON for a test, open **PowerShell**
(the blue terminal) and paste this ONE line:

```powershell
$env:CLERIA_EVENTS=1; & "C:\Users\kores\repos\YsOrigin-CleriaCore\build\Release\cleria-view.exe" "C:\Users\kores\repos\YsOrigin-CleriaCore\assets"
```

⚠️ It stays ON for that terminal window. To go back to normal: close the terminal,
or run `$env:CLERIA_EVENTS=0`. (Or just double-click the shortcut — that's always events-OFF.)

**With events ON, check these 3 things:**

1. **NPCs show up in cutscenes**
   - New Game → Yunica → into the tower (S_1000).
   - A cutscene plays. ➡️ You should SEE the NPC character(s), not just a floating text box.
   - ❌ Before: text talked but nobody was there.

2. **Camera is NOT in the floor**
   - Same intro cutscene.
   - ➡️ The camera should look at the room from a normal angle, not be buried at floor level.
   - ❌ Before: view was stuck at ground level staring sideways.

3. **Barrier needs a real kill (S_1001)**
   - Walk to the next room (a barrier fight room).
   - ➡️ An enemy should be there, and the barrier should stay UP until you kill it.
   - ❌ Before: the barrier "cleared" the instant you walked in, no enemy.

**Also handy:** press **ESC** (or **Backspace**) during any cutscene to skip it.
➡️ It should jump to the end and you can move normally after.

---

## Save / Load (works now, events OFF is fine)

1. Play a bit, walk to a **goddess statue**, press **E**.
2. Pick **Save** → pick a slot → **Enter**. ➡️ "Saved to slot N".
3. Quit. Relaunch. Title → **Load Game** → your slot → **Enter**.
4. ➡️ You're back where you saved, same level/HP/character.

Blessings (**E** at statue → "Accept divine blessing") just say "not implemented" for now — that's expected.

---

## Automated tests (CI)

Every push builds on Windows, Linux (gcc + clang) and macOS (GitHub Actions,
`.github/workflows/build.yml`) and runs `ctest -LE assets`: the tests that need no game data. The
ones labelled `assets` (cotest_0 .. cotest_9, worldtest, enemytest, asotest, animtest, ef2simtest, bullettest,
invtest, booktest, bosstest, locustest, xso13test, dueltest, weathertest) run only here, on a PC
with the game unpacked: `ctest --test-dir build_diff -C Release -j8`. Game data never goes to CI.

## Local verification (the fast commands, 2026-10-09)

From the repo root, on the `build_diff` tree. In a worktree (no `assets/`), set `CLERIA_ASSETS=<the unpacked game>`.

**Per commit** (about 2 minutes after the build):

```
python tools/verify.py --smoke
```

= the Release build, `ctest --test-dir build_diff -C Release -j8 -E cotest` (every test but the script
sweep: seconds), `python tools/goldens.py compare`, and `python tools/drive_regress.py --smoke` (30 short
drive cases, one or two per subsystem: the `SMOKE` list in the tool).

**Before a push** (about 20 minutes after the build):

```
python tools/verify.py
```

= the Release build, `ctest --test-dir build_diff -C Release -j8` (all 59 tests), the goldens, and
`python tools/drive_regress.py` (all 219 drive cases, 8 at a time by default; `--jobs N` to change it,
`--jobs 1` = one after the other, about 35 minutes).

- **cotest runs as 10 shards** (`cotest_0` .. `cotest_9` = `cleria-cotest <assets> --shard i/10`, every room's
  root script in exactly one). `ctest -R cotest` runs all of them; one process (`cleria-cotest <assets>`)
  is still the whole sweep, about 17 minutes. Without `-j` the shards run one after the other: always pass `-j8`
  (or `-j4`).
- **drive_regress prints each case's time** and lists the failed cases again at the end. Every case has its own
  temp folder (saves, shots, settings, mods) and an Archipelago case's mock server takes a free port, so no
  case has to run alone. A case is about twice as slow with 8 running as alone: a new case that checks
  wall-clock time would not be safe (none does; the Archipelago cases wait on their socket by design).
- Do not rebuild a test exe while ctest or drive_regress runs.

## If something looks wrong

Tell me: **which room**, **what you did**, **what you saw** (a screenshot is perfect).
That's all I need — I can read the game's code to find the rest.

---

## For me (not you): the controls cheat-sheet
Retail bindings from `Saved Games\CleriaCore\yso_win.ini` (imported from retail; Preset 2 = WASD move ·
**.**/LMB attack + talk/statue + OK · **/**/RMB jump + Cancel · Space boost · Tab book) · **Esc** book **or**
skip cutscene · pad works · debug (off until F1 > Debug > Debug keys): Ctrl+Tab swap character ·
Ctrl+1-9 take exit N · T debug-talk · F free-fly cam · F1 settings overlay

---

## 🆕 Note for the next events-ON test (2026-07-24)

When you launch with `CLERIA_EVENTS=1` now, a new game seeds the **FLAG_MAIN_1** story
state (the first main-story checkpoint) so cutscenes route to the *just-arrived* scenes
instead of the wrong floor-6 scene you saw before.

**The key question to answer:** when you enter S_1000 (new game, Yunica), does the
cutscene now show the **correct first-visit scene** — the right NPCs (search party), in
roughly the right spots? If yes, we adopt this and turn events on for real. If it's still
the wrong scene, tell me what you see and I'll pick a different checkpoint.
