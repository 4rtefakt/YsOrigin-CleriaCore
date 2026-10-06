# Accessibility assists and cheats

CleriaCore's own layer (not the faithful game): the shell's **Gameplay**, **Cheats** and **Interface**
pages. Code: `src/cleria/access.{hpp,cpp}` (settings, multiplier math, the skip gate, the flash limiter),
`apps/cleria-view/view_access.inl` (pages, the prompt, the per-tick application, drive commands), and
one-line hooks at the game-code sites listed below. Tests: `cleria-accesstest`, `cleria-settingstest`,
`tools/drive_regress.py` cases `access_skip_double` / `access_cheats`.

Tags: **[EXPORT]** the Ghidra export (`D:\ghidra-work\export`), **[DOC]** an existing RE doc, **[H]**
hand-chosen by CleriaCore, **[not modelled]**.

Rule: every option defaults to retail behaviour (1x / off), and every gameplay-changing option says
"Not a retail option." in its description. The one exception is the cutscene skip (§1), whose default
keeps CleriaCore's existing single-press skip.

## 1. Cutscene skip (Gameplay > Cutscenes)

### 1.1 What retail does

**Retail has no cutscene skip.** [EXPORT `004106e0.c` L47-57] The step function `FUN_004106e0` reads
the menu bit `0x10` (Esc / Camp Menu / the pad's menu button) from the pressed-edge record
(`0x764754 + pad*0x34`). It acts on it only when **all** of these hold:

- `0x76bc20 == 0`;
- `0x76bb8c == 0` (flag 156, a boss battle);
- `0x75ad90 + 0x75ad94 == 0`: the two **Fix counters**. `Fix(slot)` (op `0xb6` -> `FUN_0056e8d0`)
  increments `(&0x75ad90)[slot]` [EXPORT `0056e8d0.c` L14; DOC EVENTVM_HANDLERS_5.md:112];
- `0x769f1c == 0.0` (no fade).

If they hold, it opens the book `FUN_00443150`, or, when a window is up (`0x730294` / `0x730270` /
`0x7302b8` non-zero), turns the bit into Cancel `0x4000`. **While a cutscene holds a Fix, the menu
press does nothing.**

What retail offers instead is the message box's fast-forward (WNDDIALOG_DRAW.md §2.4):
- Confirm held (after a release) types at 5 glyphs/tick.
- Cancel **auto-repeat** (`P+0x28 & 0x4000`) types fast **and** accepts a revealed, armed message.
  Holding Cancel therefore races through a cutscene's dialogue. It does not shorten the script's
  `Wait_Time` pauses.

Movies are a separate thing: retail makes the prologue / opening movies skippable only after one clear.
CleriaCore's existing Prelaunch toggle "Unlock movie skipping" covers that and is not changed here.

### 1.2 What CleriaCore did before (unchanged as the default)

Since 2026-07-23 (PLAN.md, user-requested), Esc during a frozen cutscene **skips the whole cutscene**.
`view_game.inl` frozen branch sets `skip_cutscene`, and `tick_events` drains every live script with its
waits collapsed. **This differs from retail** (§1.1); the code comment already marks it `[CleriaCore]`.
It stays the default, as **Single press**.

A second, smaller divergence was found but not changed. Over a script menu, CleriaCore treats Esc as the
menu's cancel (`menu && (kCancel | kMenu)`). In retail the menu-to-Cancel conversion only runs while no
Fix is held, so inside a cutscene only the Cancel binding cancels. This was traced statically and not
checked in the live game.

### 1.3 The option

`[cleria.access] CutsceneSkip`:

| Value | Name | Behaviour |
|---|---|---|
| 0 (default) | Single press | The menu press edge skips at once (the pre-existing behaviour). |
| 1 | Double press | The first press shows the prompt "Press again to skip" for **120 ticks (2 s)** [H] with a draining bar. A second press inside the window skips. |
| 2 | Hold | Hold the button for **60 ticks (1 s)** [H]. A bar fills while held. A hold must **start** inside the cutscene, so a button already held when the cutscene began never skips. A release resets the fill and keeps the hint up for 120 ticks [H]. |
| 3 | Off | Never skips (retail). |

- **Hook** (`view_game.inl`, the `csMgr.world_frozen()` branch of the input step):
  `access_skip_step(pressed & kMenu, held & kMenu)`. It is not stepped over a script menu (the menu
  keeps its cancel). The gate is reset on every tick without a Fix, and on the death screen.
- **Input:** the mapper record's menu bit, so keyboard, mouse and gamepad bindings all work.
- **Timing:** counted in logic ticks. The window therefore pauses while the F1 panel is open, and
  stretches with Game speed.
- **Prompt:** a `Shell::hud["access"]` layer. A top-right card (the message box owns the bottom): a
  small "SKIP CUTSCENE" head, the line, and a gold bar. It fades over the last 20 ticks of the window.
  Its look is [H] (the shell kit's palette).

## 2. Cheats (Cheats page, `[cleria.cheats]`)

Every cheat persists across launches (in cleria.ini). The page has a Status row ("Original game" /
"Cheats active") and a **Reset all cheats** button at the top.

| Option | Key | Values | Hook site | Retail behaviour it alters |
|---|---|---|---|---|
| Infinite HP / MP / Boost / oxygen | `InfiniteHP` / `InfiniteMP` / `InfiniteBoost` / `InfiniteOxygen` | off/on | unchanged pre-existing refills before the death check (`view_game.inl`, "The shell's Cheats") | refills each tick; now persisted |
| One-hit kills | `OneHitKill` | off/on | unchanged (`player_hit_enemy`, after the guard) | regular actors only; boss parts are not covered (pre-existing) |
| Damage dealt | `DamageDealt` | 0.25x-10x | `access::damage_dealt` on the result of `enemy_incoming_damage` (FUN_004fa550 / FUN_004faf20). Four sites: `player_hit_enemy` (after the guard, before one-hit kill), the Velagunder parts, Nygtilger's `b2_damage`, Gelaldy's parts | `dmg * x`, rounded. A guarded or immune 0 stays 0; a damaging hit stays at least 1 [H, same spirit as the engine's min-1 chip] |
| Damage taken | `DamageTaken` | None (0x) to 4x | `access::damage_taken` on `player_damage_after_boost(raw)` in `hurt_player` (FUN_004fd350's player branch: FUN_004fa740, then the Boost halving) | every enemy, boss-part, bullet and Nygtilger hit. 0 = no damage (the hit reaction still runs with dmg 0). **Not** lava, the S_3009 noise, drowning or the Evil Ring (use Infinite HP) |
| EXP gained | `ExpRate` | 0.25x-10x | `award_exp` -> `access::exp_gain(enemy_exp, rate)` = `max(1, round(exp * rate))` | stands where retail's EXP rate `0x76a5fc` (1.0 in every save [DOC audit/combat.md #30]) multiplies the kill EXP [DOC LEVELING.md:124]. It covers every award_exp caller: kills and the three bosses |
| SP gained | `SpRate` | 0.25x-10x | `EffectEnv::sp_rate` in `pickup()` (`src/cleria/inventory.cpp`), set in the view's pickup touch | is retail's own SP-rate factor `0x76a5f8` in FUN_00585ff0's `mag * SPrate * s` [DOC PICKUPS.md §gold]. The popup shows the scaled number |
| Drop rate | `DropRate` | None (0x) to 10x | `DropContext::rate` in `roll_enemy_drops()` (`src/cleria/pickup.cpp`) | multiplies each ENEMYINFO slot's chance in FUN_00478510. The RNG draws are unchanged (determinism), only the threshold moves; 10x on a 10%+ slot always drops. The Strength / Defense / MP buff scatter is untouched |

Multiplier steps (the rows step through these; a stored value snaps to the nearest) [H]:
- EXP / SP: 0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4, 5, 10.
- Drops: 0, then the same steps.
- Damage dealt: 0.25 ... 10.
- Damage taken: 0 ... 4.

The reader clamps to `kRateMin..kRateMax` (0..10), `kDealtMin..kDealtMax` (0.25..10),
`kTakenMin..kTakenMax` (0..4).

## 3. Speed (Gameplay > Speed)

`[cleria.access] GameSpeed`, 25 %-200 % in 5 % steps [H]. Hook: the frame accumulator
`accum += real_dt * g_time_scale * GameSpeed` (`view_game.inl`). The fixed 60 Hz tick (`kTickDt`) is
untouched, so the simulation stays deterministic; only the number of ticks per real second changes.
Measured with `CLERIA_TPS=1`: 60 tps at 100 %, 30 at 50 %, 120 at 200 %. The audio and movies are
not slowed.

This replaces the old Gameplay slider, which wrote `g_time_scale` itself. That variable is the
engine's own slow-motion (`0x6d6078`; Gelaldy's brain writes 0.875 / 0.8 through it). The two now
multiply instead of clobbering each other.

`CLERIA_DRIVE_LOCKSTEP` (one tick per frame) ignores the setting by design.

## 4. Accessibility (Interface > Accessibility, `[cleria.access]`)

| Option | Key | Hook | What changes |
|---|---|---|---|
| Reduce flashes | `ReduceFlashes` | `access_set_fade` wraps `hud.set_fade(fade_rgb, fade_cur)` (the script fader, ops 0xc8..0xca, drawn by `view_hud.inl`) | A **bright** fade (Rec. 709 luma >= 0.35 [H]) is dimmed to luma <= 0.55 [H], and its alpha rises at most 0.12 per frame [H], so a one-frame white flash becomes a ~9-frame grey swell. Falls are immediate. Fades to black are untouched. The fader's state (and every script wait on it) is unchanged: presentation only. |
| Screen shake | `ScreenShake` 0-100 % | `CamRig::shake_scale` multiplies the shake offset where FUN_0042a080 applies it (`src/cleria/camrig.cpp`). Set each tick by `access_tick()` | The XSO `Shake` op 0xcf, boss01's tilt and Gelaldy's shakes all go through the rig. The three xorshift draws per shake step still happen, so the game RNG stays in step. |
| Text speed | `TextSpeed` 0/1/2 | `WndText::text_speed` / `WndDialog::text_speed` (`src/cleria/wnddialog.cpp` reveal step), set before the windows' update in `tick_events` | **Original:** retail (1.125 glyphs/tick, 5 while Confirm / Cancel is held; dialogs 0.4375). **Fast:** always retail's own fast rate (0x50000 / 0x30000). **Instant:** the whole message at once. Advancing still takes Confirm (or retail's Cancel auto-repeat). The 14-frame box fade-in is kept. |

## 5. CLERIA_DRIVE commands (`view_access.inl`, `view_game.inl`)

- `opt=<Key>=<value>`: set one `[cleria.access]` / `[cleria.cheats]` key by its cleria.ini name (not
  saved).
- `esctap`: Esc for one tick, through the real mapper.
- `eschold=<n>`: Esc held for n ticks.
- `stats`: prints `exp / lv / hp / sp`.
- Log lines: `[access] skip prompt: press again|hold`, `[access] cutscene skip (<mode>)`,
  `[access] opt ...`.

## 6. Not hooked (follow-ups)

- **Effect flashes:** bright additive EF2 layers (boss attacks, the cleanse pillar). They are not
  covered by Reduce flashes. That would need a per-effect brightness cap in the effect renderer.
  Hugo's shield "white flash" and the death screen flash are [not modelled] in the game yet.
- **SP on kill** (FUN_004fbb40 `0x4fcd36`: `SP += floor(g * SPrate * m)`) is not implemented in
  CleriaCore. When it lands, apply `sp_rate` there too (the same SPrate factor).
- **Faster cutscenes:** a "scale Wait_Time" option, which would shorten pauses without skipping.
- **One-hit kills on boss parts:** pre-existing scope; Damage dealt covers bosses.
- The prompt names no button glyph ("Press again to skip").
- No on-screen "cheats active" badge in game.
- Larger UI text, colour filters and rumble (ForceFeedback is [not modelled]) are not hookable yet.

## 7. Settings fields (merge note)

`Settings::access` (`cleria::access::Settings`) is read and written by `read_cleria` / `write_cleria`
(one call each). Its sections are:
- `[cleria.access]`: `CutsceneSkip`, `TextSpeed`, `ReduceFlashes`, `ScreenShake`, `GameSpeed`.
- `[cleria.cheats]`: `InfiniteHP`, `InfiniteMP`, `InfiniteBoost`, `InfiniteOxygen`, `OneHitKill`,
  `ExpRate`, `SpRate`, `DropRate`, `DamageDealt`, `DamageTaken`.
