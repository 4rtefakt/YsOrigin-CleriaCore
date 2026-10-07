# Speedrun mode

CleriaCore's built-in Speedrun mode: a run timer (RTA and IGT), splits with auto-split events taken
from the live game, PB and best-segment comparisons kept in a splits file, the list of every
non-retail option in use shown on screen (so a run can be verified), a LiveSplit Server mirror, and a
signature-tagged state block for LiveSplit ASL autosplitters.

It is part of the shell, not of the faithful game. It never changes how the game plays. It is off by
default. Tags: **[EXPORT]** the Ghidra export, **[BYTES]** read from `yso_win.exe`, **[H]** hand-chosen,
**[not modelled]**.

Code: `src/cleria/speedrun.{hpp,cpp}` (timer, splits, observer, session, ASL block),
`src/cleria/livesplit.{hpp,cpp}` (LiveSplit Server client), `apps/cleria-view/view_speedrun.inl`
(the tab, the overlay, the hooks). Tests: `apps/cleria-speedruntest`, `apps/cleria-settingstest`
(`[cleria.speedrun]`), and the `speedrun_autosplit` case in `tools/drive_regress.py`.

## 1. Research: Dusklight's speedrun mode

Dusklight is the Twilight Princess PC port (TwilitRealm/dusklight). Its speedrun mode lives in
`src/dusk/speedrun.{h,cpp}`, `src/dusk/livesplit.cpp` and `src/dusk/ui/overlay.cpp`:

- **A game mode** (`vanilla_speedrun`). Turning it on **forces** every cheat and QoL setting to its
  vanilla value through per-setting "speedrun overrides": damage multiplier, infinite items, moon jump,
  fast transitions, turbo, debug fly-cam and the rest (`resetForSpeedrunMode`). Turning it off clears
  the overrides.
- **Timer:** RTA, and IGT. IGT is the game clock minus the time spent in the overlap (load) manager:
  `m_totalLoadTime` grows while `fopOvlpM_IsDoingReq()` is set. Shown as `RTA` / `IGT` lines. The run
  starts when a save is loaded (`mOnSaveLoadedFunction`). It stops by itself at the final Ganondorf
  event (`d_a_alink_demo.inc`). Pad combos: L+R+A+Start resets, L+R+A+Y stops.
- **LiveSplit:** a TCP client to `127.0.0.1:16834`. On start it sends `initgametime`, `reset`,
  `starttimer`. Every frame it sends `setgametime H:MM:SS.mmm` from a frame count (30 fps, loads
  excluded) and `pausegametime` / `unpausegametime` around loads. It reconnects every 30 frames.
  **Dusklight does not split.** Splits are left to LiveSplit's own hotkeys or autosplitter.
- **Mods on screen:** since 2.0.2 the overlay lists the active mods ("Enabled Mods: • name") while
  speedrun mode is on.

Sources: https://github.com/TwilitRealm/dusklight (`src/dusk/speedrun.cpp`, `src/dusk/livesplit.cpp`,
`src/dusk/ui/overlay.cpp`), https://newreleases.io/project/github/TwilitRealm/dusklight/release/v1.0.0,
https://www.dsogaming.com/?p=197242, https://mynintendonews.com/2026/05/10/zelda-twilight-princess-unofficially-ported-to-pc-with-lots-of-extra-features-quality-of-life/

**What CleriaCore does differently.** It shows the non-retail options instead of forcing them off.
Cheats belong to other shell features, so they can't be overridden from here. The list is also
**sticky**: anything switched on at any point during the run stays listed, marked
"(used this run)". Switching a cheat off before the timer screenshot does not hide it. CleriaCore also
has what Dusklight leaves to LiveSplit: auto-splits on game events, a split list, PB and gold
comparisons, and an autosplitter memory block.

## 2. Research: the Ys Origin speedrun community

**speedrun.com/yso** (`https://www.speedrun.com/yso`, read through the API
`https://www.speedrun.com/api/v1/games/v1ppe718?embed=categories.variables` because the site
answers 403 to fetches):

- Ruleset: `run-times: ["realtime"]`, default `realtime`, video required, no milliseconds shown,
  emulators not allowed. **Runs are timed in real time (RTA) only.** No IGT or load-removed column is
  used. The top Any% (Yunica) runs have `realtime` set and `ingame` / `realtime_noloads` empty
  (`/api/v1/leaderboards/v1ppe718/category/q25j9vdo`).
- Categories: Any% (Yunica), Any% (Toal), Any% (Hugo), Any% (Yunica EX), Any% (Toal EX),
  Any% (Hugo EX), and "Patch 1.0.0.4" (with a Character variable). Sub-category variables: Console
  (PC / PS4 / Switch), Difficulty (Very Easy, Easy, Normal, Hard, Nightmare), Version (English /
  Japanese).
- **Start:** "Time starts on difficulty select (or Enable/disable SP gain if that is unlocked)."
- **End:** "ends when Dalles's HP meter depletes on final phase". Toal's category says Darm. The EX
  categories all say Dalles.

**Autosplitters.** LiveSplit's official list (`LiveSplit.AutoSplitters.xml`) has **no Ys Origin
entry** (it has Ys I Chronicles, VIII, IX, X). The community scripts are in
**na2na-p/FalcomSpeedrunASL** (`https://github.com/na2na-p/FalcomSpeedrunASL`, `Ys/Origin/...`):
JP 1.0.0.3 (`Toal.asl`, `ToalVE.asl`, `YunicaVE.asl`, `cut.asl`, `forVerificationOnly.asl`), JP 1.0.0.4,
and the Steam build of 2018-04-12 (`ToalEasy.asl`). They read memory from `yso_win.exe`:

| ASL value | Meaning | Used for |
|---|---|---|
| `roomID` (byte) | the scene ordinal | start `19 -> 21` (JP) / `19 -> 173` (Steam "Bonus SP Select"); room-exit splits |
| `FloorBossHP`, `SPBossHP`, `SBHP99999`, `VelagunderHP` (floats) | boss HP | split when `> 0 -> <= 0` in a given room |
| `NumofDulessHP` | Dalles' phase | the final split: `FloorBossHP == 0 && NumofDulessHP == 2` in room 175 |
| `weaponLv`, `FireSkill`, `DBoneKey`, `boostflag` | item / ability bytes | "got X in room N" splits |
| `is_displayHUD` (`cut.asl`) | HUD shown | start / split on cutscene edges |

Read through our `MAP/SCENELIST.SL` (the ordinal is the record index, `SceneRec::index`), their room
numbers name the expected rooms: 45 = `s_10\s_1099` "5F Velagunder", 67 = `s_20\s_2080`,
69 = `s_20\s_2099`, 93 = `s_30\s_3099`, 122/123 = `s_40\s_4097` / `s_40\s_4099`, 146/147 =
`s_50\s_5097` / `s_50\s_5099`, 161/166 = `s_60\s_6050` / `s_60\s_6082` "24F Yog & Om",
175 = `s_70\s_7080` "Dalles Sky", 176 = `s_70\s_7099` "Other Dimension Darm", 19 = `s_00\s_0000`
(the new-game router). This is why CleriaCore's split events are room names and boss-gauge
depletions: they are the community's split vocabulary.

**LiveSplit Server** (the README section "The LiveSplit Server", and
`src/LiveSplit.Core/Server/CommandServer.cs` and `Connection.cs` at `https://github.com/LiveSplit/LiveSplit`):
- The TCP server must be started by hand each session: right-click > Control > Start TCP Server. The
  default port is **16834**. A named pipe `\\.\pipe\LiveSplit` and a WebSocket server also exist.
- One command per line. `Connection.ReadLine()` accepts `\n` or `\r\n`. Commands are case
  sensitive. There are no replies for the control commands.
- Times are parsed as `[-][[[d.]hh:]mm:]ss[.fffffff]`. Hours may pass 23 and fields need no padding.
- Commands used here: `starttimer`, `split`, `unsplit`, `skipsplit`, `reset`, `setgametime TIME`,
  `pausegametime`. `initgametime`, which Dusklight still sends, is **not** in the current
  `CommandServer` switch (it logs "Invalid command"), so CleriaCore does not send it.
- `setgametime` sets `LoadingTimes = RealTime - gameTime`. While game time is paused it also pins
  `GameTimePauseTime`. So **pause first, then set every step** and LiveSplit's game time is exactly
  ours (`LiveSplitState.SetGameTime`).

## 3. What the timer measures

**RTA**: CleriaCore's real-time clock (`SDL_GetTicksNS`) from the start of the run to now (or to the
last split). This is the time speedrun.com ranks. It runs through menus, the shell panel, loads and
cutscenes, like a stopwatch.

**IGT**: the retail game's own Play Time counter. Each 60 Hz logic step adds one, and IGT =
steps × 1000 / 60 ms, an integer like retail's readout. Shown in retail's format `H:MM:SS:mmm`.
- Retail: `DAT_0076bc38` (g_flags[199], saved at header +0x64) is incremented in the master step
  `FUN_004281c0` at 0x428274 only when `DAT_0074ab8f == 0` (not paused: `DAT_0074ac38`) and
  `DAT_0074c0f0 != 0` (a real simulation step, not the idle call) **[EXPORT]**. The frame timer is
  re-anchored after a load (`FUN_005ccb90`, ACTOR.md §5), so **load stalls add no steps**. The pause
  book stops it. Cutscenes and dialogue count.
- CleriaCore: `speedrun_tick` runs beside `++game.g_flags[199]` in the game loop's logic tick (the
  same steps: not while the pause book or the F1 panel is open, one step after a load via
  `g_tick_reset`). The run counts its own steps from its start, so loading a save during a run does
  not jump IGT.
- The Gameplay "Game speed" slider changes the steps per second, so IGT runs faster or slower with it.
  That slider is listed as a modification.

**Start (auto):** the New Game difficulty pick. The title book's `ev.new_game` in `view_game.inl` is
the moment the last popup is confirmed, which is speedrun.com's "difficulty select". Starting a new
game resets a run in progress first. **[not modelled]** The "Enable/disable SP gain" alternative only
exists once a clear unlocks it (clear data is not modelled, and the GOHOUBIMENU.XSO script menu runs
after the start). "Start run now" (the tab, or `speedrun=start`) starts by hand, for a run from a
loaded save. It uses the category of the character being played (g_flags[150]).

**End:** the route's last split. The default routes end on `boss:S_7080` (Dalles) and `boss:S_7099`
(Darm for Toal): the boss gauge emptying, which is the community rule. **[not modelled]** Dalles and
Darm are not implemented yet. Dalles' "final phase" may need `boss:S_7080#n` (the n-th depletion in
the room) once his phases exist.

## 4. Auto-split events (`Observer`, every logic step)

| Event | Rule |
|---|---|
| Room | the room folder name changed between two steps |
| Boss | the boss HP gauge (Hud `boss_frac >= 0`, `boss_cur`, widget 3 / `DAT_006d5fd8`) was up with HP > 0, and now shows HP <= 0 **or was withdrawn** in the same room. Withdrawal covers Velagunder (`Boss01::tail_always` hides it at HP < 1), world-actor bosses (hidden when the gauge owner dies), Nygtilger and Gelaldy (hidden at the defeat). |
| Flag | any g_flags change. Inventory ids < 128 live in g_flags[id] (`pickup`, FUN_00585ff0: -1 never had, then counts), so `item:ID` is a flag going from < 1 to >= 1. |

Guards **[H]**:
- A gauge withdrawn after a player death is not a defeat. A retry re-inits the fight; the latch opens
  once the gauge has gone with the player alive.
- A lost duel is not a defeat (`FUN_0046f810` with g_flags[133]: 156 is cleared and the gauge hidden
  in the same step).
- A step that changes more than 16 flags (a save loaded, a new game) reports no flag events.
- Leaving the room with the gauge up is a Room event only.

The `speedrun_autosplit` drive case kills Velagunder at Lv 60 and checks that the default route's
first split fires on the real gauge.

**Split conditions** (a split's `Event=`; rooms compare case-blind, `*` = any):

| Spec | Fires on |
|---|---|
| `enter:S_1005` | any transition into S_1005 |
| `leave:S_1099` | any transition out of S_1099 |
| `room:S_4099>S_4097` | exactly that transition (the ASLs' `old.roomID == a && current.roomID == b`) |
| `boss:S_1099` / `boss:S_7080#2` | a boss gauge empties in that room (the 2nd time during the visit) |
| `item:58` | inventory id 58 obtained |
| `flag:310=1` | g_flags[310] becomes 1 |
| `manual` | never (the Split button, or LiveSplit's own hotkey) |

**Default routes [H]**, from the ASL split points above: Velagunder `boss:S_1099`, Hugo `boss:S_2080`
(not for Toal), Nygtilger `boss:S_2099`, Gelaldy `boss:S_3099`, Khonsclard `room:S_4099>S_4097`,
Pictimos `room:S_5099>S_5097`, Yog & Om `room:S_6082>S_6050`, then Dalles `boss:S_7080` or Darm
`boss:S_7099`. The names and the selection are hand-chosen. A runner edits the file.

## 5. Splits file, comparisons

`<settings folder>/speedrun/<key>.splits`: the folder of `yso_win.ini`, i.e. `CLERIA_INI`'s folder in
tests. Keys are `yunica`, `hugo`, `toal`, `yunica_ex`, ... One file per category. It is written with
the default route the first time, then after every start (the attempt count), every split (golds
survive a crash), the finish (PB) and a reset. A memory-only run (`CLERIA_DRIVE` without
`CLERIA_INI`) writes nothing. The format is the ini dialect; quotes are optional when hand-editing:

```
[run]
Category="Any% (Yunica)"
Attempts=12
Completed=3
[split.0]
Name="Velagunder"
Event="boss:S_1099"
PbRta=512345      ; ms, cumulative, of the PB run (-1 none)
PbIgt=498000
GoldRta=512345    ; ms, the best this segment was ever done in
GoldIgt=498000
```

- A run compares against the PB and golds **as they were when it started** (LiveSplit's behaviour).
  A run never shows a delta against the PB it just set.
- **Timing** (RTA by default, the speedrun.com rule; or IGT) decides the PB and the deltas. Both times
  are always recorded.
- **Compare against:** personal best, or best segments (the sum of golds up to the split).
- A gold needs the previous split's time: a skipped split leaves the next segment unknown.
- Undo after the finish takes the PB back. The last split cannot be skipped.

## 6. The autosplitter block (for ASL authors)

A fixed struct in `cleria-view.exe`'s `.data`, constant-initialised so its 16 magic bytes appear once
in the image (checked: 1 hit, in `.data`). The layout is `static_assert`ed in `speedrun.cpp`:

| Offset | Type | Field |
|---|---|---|
| 0 | char[16] | `CleriaSpeedrun01` (no terminator) |
| 16 | u32 | version = 1 |
| 20 | u32 | phase: 0 idle, 1 running, 2 finished |
| 24 | i32 | the split being run (0-based) |
| 28 | u32 | split count |
| 32 | u64 | IGT in retail steps (60 Hz) since the start |
| 40 | double | IGT ms |
| 48 | double | RTA ms (CleriaCore's clock) |
| 56 | u32 | sequence: +1 on every start / split / skip / undo / reset |
| 60 | u32 | number of non-retail options used during the run |
| 64 | char[16] | the current room folder, NUL-terminated |

It is updated every rendered frame. A minimal ASL:

```
state("cleria-view") {}
init {
    var m = modules.First();
    var scan = new SignatureScanner(game, m.BaseAddress, m.ModuleMemorySize);
    IntPtr b = scan.Scan(new SigScanTarget(0, "43 6C 65 72 69 61 53 70 65 65 64 72 75 6E 30 31"));
    if (b == IntPtr.Zero) throw new Exception("speedrun block not found");
    vars.Phase = new MemoryWatcher<uint>(b + 20);
    vars.Index = new MemoryWatcher<int>(b + 24);
    vars.Igt = new MemoryWatcher<double>(b + 40);
    vars.W = new MemoryWatcherList { vars.Phase, vars.Index, vars.Igt };
}
update { vars.W.UpdateAll(game); }
start { return vars.Phase.Old == 0 && vars.Phase.Current == 1; }
split { return vars.Index.Current > vars.Index.Old; }
reset { return vars.Phase.Old != 0 && vars.Phase.Current == 0; }
isLoading { return true; }
gameTime { return TimeSpan.FromMilliseconds(vars.Igt.Current); }
```

The LiveSplit Server mirror (§7) does the same job without a script.

## 7. LiveSplit Server mirror

Option "Connect to LiveSplit" (`[cleria.speedrun] LiveSplit=1`, `LiveSplitHost`, `LiveSplitPort`).
It uses plain winsock, non-blocking (`ws2_32`, no new dependency). It retries every 2 s **[H]** while
LiveSplit's TCP server is not running.

| CleriaCore | Lines sent |
|---|---|
| run start | `reset`, `starttimer`, `pausegametime`, `setgametime 0:00:00.000` |
| every rendered frame where IGT moved | `setgametime H:MM:SS.mmm` |
| split | `setgametime <the split's IGT>`, `split` |
| skip / undo / reset | `skipsplit` / `unsplit` / `reset` |

LiveSplit's own RTA times the run. Its Game Time column equals CleriaCore's IGT. The runner's
LiveSplit layout needs the same number of splits as the route. Connecting in the middle of a run
does not start LiveSplit's timer.

Verified with a stand-in server (`scratchpad/speedrun/fake_livesplit.py`) during a live drive run:
it received `reset`, `starttimer`, `pausegametime`, `setgametime 0:00:00.000`, then one
`setgametime` per step, `split` at the real door crossing, `split` / `unsplit` from the drive, and
`reset`. The loopback test in `cleria-speedruntest` checks the exact bytes.

## 8. The modifications list (the extension point)

`cleria::speedrun::set_provider(key, fn)`. The function appends one line per non-retail option that
is on right now. The overlay shows the union of all providers. The run also records every line seen
at any step (`Run::mods_used`) and keeps showing them, dimmed, "(used this run)".
`note_modification(text)` records a one-off use. Registered now:

| Key | Lines |
|---|---|
| `cheats` (`view_game.inl`, next to the Cheats page) | Infinite HP / MP / Boost / oxygen, One-hit kills. **A new cheat adds its line here.** |
| `gameplay` | `Game speed N.NNx` when `g_time_scale` is not 1.0, 0.875 or 0.8. Those three are the game's own writes; boss03's 0x6d6078 slow-mo is retail. The slider steps by 0.25. |
| `qol` | "Movie skipping unlocked" (`UnlockMovieSkip`: it saves time) |
| notes | CleriaCore's debug keys (T, F, R, K, L, B, Ctrl+Tab, Ctrl+1-9), an activation on the Debug page (the page is wrapped from `view_speedrun.inl`), and `CLERIA_DRIVE` commands that move the player or write state (`at=`, `flag=`, `hp=`, `sp=`, `boss*`, ...) |

Other features (mods, randomizer, assists) call `set_provider("<feature>", ...)` from their own code.
With nothing in use, the overlay reads "RETAIL RULES - NO MODIFICATIONS" (green). Otherwise it reads
"MODIFIED" with the list, and the box edge turns amber.

## 9. The shell

**Speedrun tab** (F1): the mode toggle; Run (category and attempts, status, sum of best, Start run
now / Split / Undo / Skip / Reset); Timer (show RTA / IGT / splits, position in four corners, size
50-200 %, timing RTA / IGT, compare against PB / best segments); Auto-splitter (start on New Game,
auto-split, the splits file path, reload); LiveSplit (connect, server, connection state); the
modifications in use.

**Overlay** (`Shell::hud["speedrun"]`, drawn over the game, the title and the launcher): category
and attempt; up to 7 split rows [H] (the current one highlighted, the last pinned); deltas green /
red, golds in gold; the big RTA (green or red against the comparison while running, gold for a PB
finish); IGT in retail format; the last split's notice for 4 s [H]; the modifications; the LiveSplit
status.

**Settings** `[cleria.speedrun]` in `cleria.ini`: `Enabled=0`, `ShowRTA=1`, `ShowIGT=1`,
`ShowSplits=1`, `Corner=1` (top-right; the area-name caption is top-left), `Scale=1.0`,
`AutoStart=1`, `AutoSplit=1`, `Timing=0` (RTA), `Compare=0` (PB), `LiveSplit=0`,
`LiveSplitHost="127.0.0.1"`, `LiveSplitPort=16834`.

**CLERIA_DRIVE:** `speedrun=on|off|start|split|skip|undo|reset|status|page|page_end`. Each prints
`[speedrun] phase P split i/n rta .. igt .. (steps) mods [..]`; an auto-split prints
`[speedrun] auto-split i 'name' on <event> (IGT step n)`.

## 10. Open / not done

- [not modelled] The SP-gain start variant, and the Dalles / Darm final phases (see §3).
- No in-game hotkeys for split / reset. Use LiveSplit's global hotkeys with the mirror, or the tab.
  Dusklight's pad combos were not copied: every pad button is a retail binding.
- A loaded save's Extra type is not known, so "Start run now" picks the plain category [H].
- Practice tools were skipped. The Debug tab already has a room warp. A segment-restart tool would
  need save states, which do not exist yet.
- The default route names and the overlay's look are [H].
