# Retail Probe — a "go look at the real game" panel for retail Ys Origin

A small mod for the **retail** game (`yso_win.exe` v1.1.1.0, Steam). It adds a panel
with one button per open question. Clicking a button loads the right room and sets
the character state for that test (level, weapon, skills, flags, boss ready to
fight…), tells you what to do and what to watch, and has answer buttons that write
your answer to a text file.

It is built like the Archipelago mod: a `dinput8.dll` placed next to `yso_win.exe`.

**It has never been run inside the game.** See "What is not tested" at the end before
you trust it. Nothing here touches your save files; read "Your saves" anyway.

## Install

1. Close the game.
2. In PowerShell, from this folder:

   ```powershell
   .\install.ps1
   ```

   The game folder is assumed to be `D:\SteamLibrary\steamapps\common\Ys Origin`
   (`-GameDir '...'` for another one).

The probe and the Archipelago mod are both `dinput8.dll`, so **they cannot be loaded
together**: the script keeps the Archipelago DLL as `dinput8.dll.before-retail-probe`
and `uninstall.ps1` puts it back. (`.\install.ps1 -KeepAP` keeps the Archipelago mod
loaded behind the probe instead. That path was never tried — use the plain swap.)

Files the install adds to the game folder: `dinput8.dll`, `retail_probe_tests.ini`,
`retail_probe.installed`, the backup. While you play the mod writes
`retail_answers.txt` and `retail_probe.log` there. No game file is changed.

## Use

1. Start Ys Origin from Steam and **load a save** (any story save; a late one is not
   needed — the tests set their own level and gear).
2. The panel is on screen from the start. **HOME** hides / shows it. Use the mouse.
   (Keys the game or the Archipelago mod use were avoided: F1–F4, F11, F12, Insert,
   F5–F9. Change the key with `toggle_vk` in the ini.)
3. **Tests** page: click a question. The screen fades, the room loads, you stand at
   the test's spot. The lower half shows what to do and what to watch.
4. Click an **answer** button (type a note first if you want one saved with it).
   Every answer is one line in `retail_answers.txt`, with the time, the test id, the
   room, your position and the config levels.
5. "Go again" reloads the test. Buttons next to it are the test's helpers
   (`Boss HP -> 1`, `HP lock`, `Invincible`…) and, for the look tests, config values
   (`shadows: 0 1 2`) that reload the room with that value.

The top lines always show the room, your x / y / z, level, HP, Boost and the config
levels — read distances off the x / z values.

A test for another character is dimmed: load a save of that character. (Helpers page:
"Experimental: let a test switch the character" does it without a save. Never tried.)

**Helpers** page: invincible, HP lock, heal, fill MP / Boost, boss HP → 1, monsters
HP → 1, save / restore position, go to any room (`S_1004`), read / set a flag,
reload the test list.

## Your saves

- The mod never writes a save.
- Once a test (or any helper) has changed the game's state, **the game's own Save is
  blocked**: choosing Save at a statue does nothing to the slot, and the panel shows
  an orange line with the number of blocked attempts. Do not count on the game's
  "saved" message while that line is there.
- Loading a save from the game's Load menu makes the state the save's own again and
  unblocks saving.
- A test changes level, weapon, flags, sometimes the difficulty. To get back to your
  real state, **load your save again**.
- "Allow saving" on the Helpers page turns the block off. Do not save over a real
  slot with a test state loaded.
- Not blocked: Steam achievements and the game's clear data. Killing a boss in a
  test can unlock its achievement for real; finishing the Darm fight would run the
  ending. The affected tests say so.

## The test list is a text file

`retail_probe_tests.ini` (in the game folder once installed; the copy here is the
source). One `[test <id>]` section per button; the keys are explained at the top of
the file. Edit it, press "Reload retail_probe_tests.ini" on the Helpers page — no
rebuild. `build.ps1` checks the file with the same parser and checks every room name
against the game's scene list.

60 tests are staged, in five groups: A the questions the port is waiting on, B the
look tests (glare / reflections / shadows / caustics / Other Simplification), C the
floor 1–2 checklist, D the bosses, E the duels.

**Dropped** (settled for certain by the code, RETAIL_ANSWERS.md): SP on kill, stun
length, S_5013 falling steps, jump on landing, the HUD Lv / Max badges, gem pickup
sound, Nightmare SP, the soft-chest test P11.

**Not staged** (and why):

| Item | Why |
|---|---|
| P05 shield sparks | no doc names a room with a shielded monster |
| Q01 statue cleanse | the "not yet cleansed" flag of a statue was not looked up |
| Q02 regen in an `S_x1xx` wing | needs the accessory + its blessing; ids not looked up |
| Q03 ailments, Q09 floor-5 blood water | no room named in the docs |
| Q13 Hugo after the S_2080 duel | a cutscene; the mod refuses to act during one |
| M01–M14, M16 bonus modes and menus | title-screen menus; the mod stays out of Time Attack / Arena |
| Part 4 (T01–T17) | debugger / frame-capture work, not for the eye |

**Staging that is a guess** (marked "uncertain staging" in the panel): the duels
(flag 205 = 1 is read as "start the fight", the way the boss rooms' scripts use it)
and the S_1080 pit (flag 230 = 1 read as "duel done"). If a duel does not start, or a
cutscene plays, answer "Duel did not start" — that is useful too.

## Config levels (Shadows / Reflections / Glare / Other Simplification)

None of them is read purely "live" by the engine: some readers run every frame, some
when a room's objects spawn, some once when a render target is created. So the mod
writes the value **with the room load**, never while a room is running, and never
**above** the level the game started with (buffers for a higher level were not
created — those buttons are greyed and say "needs a restart").
For a comparison you can fully trust, set the level in `config.exe`, restart, and
press the test again. Start the game with every level at its maximum so the buttons
can go down from there.

## Remove

```powershell
.\uninstall.ps1
```

It copies `retail_answers.txt` and `retail_probe.log` next to the script, removes the
probe's files and puts the Archipelago `dinput8.dll` back.

## How it works

- `dinput8.dll` forwards to the system DLL, hooks Direct3D 9 `EndScene` (panel drawn
  with Dear ImGui) and subclasses the game window for input — the Archipelago mod's
  mechanism.
- A click stages a room change exactly as walking into an exit does: it writes the
  cells the door trigger `FUN_0042cf00` writes (fade to black, pending scene, the
  arrival descriptor). The engine's own fade machine commits it and calls its scene
  loader. No coordinates are poked into a running room.
- A hook in front of the scene loader `FUN_00431260` writes the test's state at the
  moment nothing of the old or new room exists — the boundary at which a save is
  loaded. Level, HP, MP and Boost are set about 20 frames after the room is up
  (the level through the game's own function, as the Archipelago mod does).
- Every game write is gated the way the door trigger is: a player exists, no
  cutscene, no menu freeze, no game over, no pending room change, story mode. The
  exe's code bytes are checked at start; on any other build the panel is read-only.
  An access violation in the mod's game code is caught, logged, and turns all game
  writes off for the session.
- Every address is listed, with where it was verified, in `src/game.h`.

## Build

```powershell
.\build.ps1
```

Needs Visual Studio 2022 (x86 toolchain) and CMake. Builds only this DLL (plus three
tiny check programs) into `%LOCALAPPDATA%\retail_probe_build`, copies the DLL and the
ini to `dist\`, then runs:

- `probe_selftest` — parses the ini with the DLL's parser, checks rooms against
  `SCENELIST.SL`.
- `probe_simtest` — loads the real DLL against a fake memory image (the first bytes
  of the real functions, a fake scene list, player and save block) and presses
  buttons: 53 checks of what gets written where, the gates, the save block, the
  helpers, the fault guard.
- `probe_uitest` (not run by the script) — a bare Direct3D 9 window that loads the
  DLL and saves a screenshot of the panel.

## What is not tested

Checked without the game: the DLL builds clean for x86; it loads, hooks DirectInput
and Direct3D 9 and draws a working panel in a bare D3D9 window; the test list parses
and all rooms exist; the game-side logic passes the simulation above; every address
was read in the Ghidra export or the exe bytes (`src/game.h`).

**Never run in the game** — so all of this is unproven until you try it:

1. That the staged door really loads the room (the fade commits, the player spawns
   at the asked position). First thing to try: Helpers → Go to a room → `S_1000`.
2. That writing flags / items / weapon cells at the load boundary gives a sound
   state, and that the level call behaves 20 frames after a load.
3. That flag 205 = 1 with the defeat flag at 0 starts each boss fight (read from the
   room scripts, proven for the port, not for retail through this path).
4. The save block (the game's save menu after a blocked save may look odd).
5. The config levels written at a room load.
6. The character switch, `-KeepAP`, the boss / monster HP helpers, restoring a
   position (the facing may be wrong).
7. Whether the panel shows correctly over the game in exclusive fullscreen, and
   whether the game's own mouse handling minds the clicks.

If the game does not start or crashes at start: run `uninstall.ps1`. If a test
misbehaves, `retail_probe.log` in the game folder has one line per action.
