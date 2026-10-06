# PAD — the gamepad in the shell (Input > Controller) and retail's stick / force feedback

CleriaCore's controller page and the pad-side runtime it adds. The game side stays the faithful
port of FUN_005d2ba0 (CONFIG.md §3.4); every option that changes behaviour defaults to retail.

Code: `src/cleria/pad.{hpp,cpp}` (pure logic, `[cleria.pad]`), `apps/cleria-view/view_pad.inl` (the
page, the picture, the hot-plug notice, `pad_rumble`), small hooks in `input_map.cpp` (mapper),
`settings.cpp` (ini + mapper config), `view_input.inl` (trigger threshold), `view_ui.inl` (pad
navigation), `view_shell.inl` (OS pointer under hud layers), `view_game.inl` (page entry, rumble
call sites). Tests: `apps/cleria-padtest`, `apps/cleria-settingstest` (`[cleria.pad]` block).

Provenance tags: [BYTES] read from yso_win.exe, [EXPORT] the Ghidra export (D:\ghidra-work\export),
[H] hand-chosen, [not modelled].

---

## 1. What retail does with a pad

### 1.1 The stick (mapper FUN_005d2ba0, CONFIG.md §3.4) [BYTES 0x5d2df5..0x5d300d, EXPORT]

| Step | Retail |
|---|---|
| Range | DInput `DIPROP_RANGE` ±1024 on every axis [BYTES 0x5d5ad7..0x5d5aff]; no saturation property is set (DInput's default: none) |
| Inner deadzone | **radial**: `lX² + lY² >= dz²` on the squared int length, `dz = (int)(clamp([input] DeadZone, 0.05, 0.95) · 1024)` (FUN_005d68d0). Default `DeadZone = 0.35` → **358**. The vector is used raw past it (no rescale). |
| Outer deadzone | none |
| Fallback | left stick inside its deadzone → the **right stick** (lRx, lRy) with the same test → else the first POV hat (the d-pad), converted to a unit vector |
| Direction | 8-way bits from `atan2(-y, x)` (4-way inside the pause book, which also raises the deadzone to 0x333 = 0.8) |
| Angle | analog (the stick's own `atan2`) unless `[input] PadAnalog` ("Use Analog as Digital": magnitude 1.0, the 8-way table angle) |
| Magnitude | `sqrt(x⁴ + y⁴) / 1024` — ≥ ~125 past any deadzone, so against `RunWalkThreshold` (0.5) **the stick always runs**; only the Walk button walks. The POV hat is 1.0. |
| Keys vs stick | any digital direction key wins over the stick |

So retail has exactly one stick setting, the inner deadzone, plus the PadAnalog switch. There is no
camera stick (the right stick is a second movement stick) and no sensitivity; magnitude only decides
walk vs run.

### 1.2 The config tool's page 203 "Input Settings" [EXPORT FUN_0041bf10, CONFIG.md §4]

3001 Use Game Pad (`GamePad`), 3002 Force Feedback (`ForceFeedback`, forced 0 without a pad),
3004 deadzone trackbar 0..10000 with **3005 Reset = 3500** (`0xdac`), 3006 **Vibration Test** (plays
effect 5, below), combo 1531 = the joystick to use (`GamePadID = sel + 1`; picking one re-creates the
device and its effects), and a live stick preview (`FUN_0041c150`, [not decoded]).

### 1.3 Force feedback [EXPORT FUN_005d5b20 / FUN_005d3500, BYTES 0x6b8df8 / 0x67c87c]

`FUN_005d5b20` (device setup) creates **six effects** with `CreateEffect(GUID_Sine
{13541C23-8E33-11D0-9AD0-00A0C9A06E35})`, `DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS`, gain 10000, no
trigger button, on the X / Y axes, `DIPERIODIC {magnitude, 0, 0, period 50000 us}`. Magnitude and
duration come from the table at **0x6b8df8** (stride 8, stored per effect at `0x795c28 + n·0x74`):

| Effect | Magnitude | Duration |
|---|---|---|
| 0 | 5000 (0.5) | 200 ms |
| 1 | 10000 (1.0) | 200 ms |
| 2 | 5000 | 400 ms |
| 3 | 10000 | 400 ms |
| 4 | 5000 | 1000 ms |
| 5 | 10000 | 1000 ms |

`FUN_005d3500(n)` (`__fastcall`, n in ECX): stop the last started effect (`0x746f54`), and when
`GamePad` (0x6d602b) and `ForceFeedback` (0x6d5d93) are set start effect n once; remember n.
`ForceFeedback` defaults to **0** (CONFIG.md §2.5) — retail ships with rumble off.

Call sites (61, found by scanning `.text` for `call 0x5d3500` and reading ECX; tool:
`scratch/ffcalls.py`-style capstone scan). The player's own:

| Site | Function | Effect | Event | CleriaCore hook |
|---|---|---|---|---|
| 0x465dc0 | FUN_00465170 (mover) | 5 | the Boost super (second Boost press, section 0x139/300/0x13d) | `try_boost` after the section is set |
| 0x46f19b | FUN_0046ea00 (skill cast) | 2 | charged cast (`c138 == 2`) | `skill_tick` before SetSection |
| 0x46f27a | FUN_0046ea00 | 0 | normal cast | `skill_tick` |
| 0x4fdf58 | FUN_004fd350 (hit on the player) | 0 | small hit (section 4, i-frames 8) | `hurt_player` via `pad::hit_effect` |
| 0x4fdf02 | FUN_004fd350 | 0 | rtype 10 flinch (section 4) | same |
| 0x4fdf7d | FUN_004fd350 | 1 (`esi 0xb − 0xa`) | medium hit (section 0xb) | same |
| 0x4fdfb5 | FUN_004fd350 | 3 (`esi 9 − 6`) | knockdown (section 9) | same |
| 0x4fde93 | FUN_004fd350 | 3 | lethal hit without a revive item (section 5) | same |

No rumble on a guarded hit (Hugo's shield path), a durable victim, or when the player lands a hit
(FUN_004fbb40 never calls it). The bosses: `src/cleria/boss01/02/03` already emit their
`FUN_005d3500(n)` ids in `Output::rumble`; `view_game.inl` now plays them. **[not modelled]** the
other callers (later bosses and set pieces: 0x4e3070, 0x4fe1a0, 0x5093e0, 0x509df0, 0x50f280,
0x510730, 0x515250 (boss02's dust, via the module), 0x516740, 0x518ae0, 0x5215e0, 0x524240,
0x524f10, 0x525300, 0x525940, 0x526510, 0x5295a0, 0x52b6e0, 0x52c2c0, 0x52c680, 0x52c8f0, 0x52e2c0,
0x538820, 0x53a760, 0x53c860, 0x53d700, 0x5423b0, 0x542cd0, 0x5472e0, 0x54a890, 0x54fb20, 0x552ef0,
0x55d3d0, 0x568a00) — they come with their owners as those are ported.

---

## 2. The settings

Retail keys the page edits, where retail keeps them:

| Page row | Key | Default | Note |
|---|---|---|---|
| Use controller | `[input] GamePadID` | 1 | 1-based into SDL's gamepad list, clamped (FUN_005d5b20); changing it reopens the pad |
| Use gamepad | `[input] GamePad` | 1 | |
| Deadzone | `[input] DeadZone` | 0.35 | 5..95 % in 5 % steps (retail's effective clamp) |
| Run threshold | `[input] RunWalkThreshold` | 0.5 | only matters with Analog walk (§1.1) |
| Vibration | `[input] ForceFeedback` | 0 | |
| Analog walk | `[cleria.input] LinearStickMagnitude` | 0 | existing QoL key: magnitude `|v|/1024` |
| Bind by button name | `[cleria.input] NativePadBinds` (+ `PadBind{}`) | 0 | existing key |

CleriaCore's own `[cleria.pad]` (all defaults = retail behaviour):

| Key | Values | Default | Effect |
|---|---|---|---|
| `Glyphs` | "auto" / "xbox" / "playstation" / "nintendo" / "generic" | auto | the picture and button names; auto = `SDL_GetGamepadType` of the open pad, generic without one |
| `OuterDeadZone` | 0.5..1.0 | 1.0 = none | analog walk only: `|v|` at which the magnitude saturates [H] |
| `ResponseCurve` | "linear" / "smooth" | linear | analog walk only (§3) [H] |
| `TriggerThreshold` | 0.05..0.95 | 0.25 | LT / RT read as Button 11 / 12 past this pull (CleriaCore's virtual buttons; DInput reports the triggers as the Z axis, which retail never reads) [H, the previous fixed 8192/32767] |
| `SwapConfirmCancel` | 0/1 | 0 | the pad's menu OK / Cancel swapped (§4). Not a retail option. |
| `VibrationIntensity` | 0..1 | 1.0 | scales the retail effects' magnitude [H] |

Values are clamped on read. No movement "sensitivity" is offered: the stick only picks a direction
and walk-vs-run (§1.1), so the run threshold already is the sensitivity; there is no camera stick.

## 3. Analog walk shaping (`pad::shaped_magnitude`, mapper hook)

Only on the `linear_magnitude` path (retail's `x⁴` magnitude is untouched). With `r = |v|/1024` and
`dz` the mapper's deadzone / 1024:

- linear: `min(r / outer, 1)` — with outer 1.0 bit-identical to the old LinearStickMagnitude;
- smooth: `u = clamp((r − dz) / (outer − dz), 0, 1)`, magnitude `u²` (0 at the deadzone edge, 1 at the
  outer ring, finer walking near the edge).

## 4. Swap confirm / cancel (mapper hook)

FUN_005d2ba0 derives menu OK (0x2000) from a slot-0 binding and Cancel (0x4000) from slot 1, in every
group. With `SwapConfirmCancel` the **pad group only** (slots 20..27) derives OK from PAD_JUMP and
Cancel from PAD_ACTION. The Attack / Jump bits (0x1 / 0x2) are unchanged, so play is the same and
only menus, windows and dialogue confirm with the Jump button (Nintendo's A sits where Xbox's B
is). Keyboard and mouse are untouched. The shell's own pad navigation (`ui::read_nav`) swaps its
face-button OK / Back the same way. Off = byte-identical behaviour (padtest checks both).

## 5. The bindings on the picture

The eight PAD_* slots are shown as SDL buttons (positional: SOUTH is the bottom face button on every
pad). Both storage modes reach the mapper as "Button n" codes: retail `Assign{PAD_*}` = `0x107 + n`
in the XInput→DInput order A, B, X, Y, LB, RB, Back, Start, LS, RS (+ LT, RT, Guide, Misc1, paddles
as Button 11..16) [H, input_map.hpp]; NativePadBinds stores the SDL names. `pad::sdl_from_dinput`
inverts the order, so the retail Preset 1 pad slots read on an Xbox pad as A, B, X, Y, **RS (press)**,
**LS (press)**, LB, **Menu** — Falcom's defaults assumed a generic DInput pad; the native defaults
(A, B, X, Y, Start, LT, LB, RB) are CleriaCore's [H].

Rebinding (`pad::bind_swap`): select an action row and press the pad button (Shell::Capture), or
click a button on the picture. If another action had that button the two swap (the other takes the
old button, or becomes unbound). D-pad directions are refused: the POV hat always moves (retail).
Unbound = `"---"` (0x103, retail's own spelling) / `""` with NativePadBinds. "Reset buttons to
default" restores the Preset 1 pad slots, or the native defaults.

## 6. The page

`Input > Controller` (a button under Configure Inputs). Left: status (pad name / "No controller
connected" / simulated), which controller, Use gamepad, glyphs; the eight actions; swap / bind by
name / reset; sticks; vibration (+ Test vibration = effect 5, retail's 3006). Right, under the
description: the controller drawn with ImDrawList (body, triggers with their pull and threshold
line, bumpers, sticks moving with the deflection, d-pad, face buttons coloured / shaped per glyph
set, back / start / guide), callouts naming each bound action, the selected action's button ringed
in gold, the hovered one in white with its name above the picture; under it the two stick monitors
(the deadzone disc, the outer ring with analog walk, the 8 sectors with the one the game would move
in lit — computed by a private `input::Mapper` on the live pad, so it is exactly what the game
reads — and "Run NE" / "Walk SW 43%" / "In the deadzone"), and the LT / RT bars with the threshold.

While the page is drawn the pad is free for testing (`ui::pad_test_frame`): only the d-pad moves the
selection, **Start** activates, **Back / View** leaves the page; face buttons, bumpers, triggers and
sticks just light up. Keyboard and mouse work as on every page.

Hot-plug: an always-on `Shell::hud["pad"]` layer opens a pad that appears while the launcher or panel
is up (those loops do not feed SDL events to the input layer) and shows "<name> connected" /
"Controller disconnected" for 3.5 s (not in CLERIA_DRIVE runs, so drives stay deterministic).
`view_shell.inl` now sets `ImGuiConfigFlags_NoMouseCursorChange` while only hud layers draw, or
ImGui's backend would show the OS pointer over the game every frame.

## 7. Headless

- `CLERIA_SHELL_PAGE=Input:pad` (with `CLERIA_LAUNCHER=1` and a `CLERIA_DRIVE`): the panel opens on
  the Controller page; `launcher.ppm` is shot on frame 6.
- `CLERIA_PAD_FAKE="type=playstation;lx=0.6;ly=-0.3;rx=0;ry=0;lt=0.7;rt=0;buttons=a,leftshoulder,dpup;row=16"`
  replaces the live pad on the page (type xbox / playstation / nintendo / generic; buttons by SDL name;
  `row` = the row to select and scroll to). It never reaches the game's mapper.

## 8. Open / [H] / [not modelled]

- [H] the rumble mapping: retail's sine magnitude → both SDL motors at the same speed; the 50 ms sine
  period has no SDL equivalent.
- [H] the native default layout, the trigger threshold, OuterDeadZone / ResponseCurve / intensity.
- [not modelled] the boss / set-piece rumble sites not yet ported (§1.3); the config tool's stick
  preview FUN_0041c150; retail's effect restart semantics beyond "replace" (SDL replaces too).
- The hot-plug notice was not exercised with a real plug event (no pad on the test machine); the
  page's live input was verified with `CLERIA_PAD_FAKE`.
