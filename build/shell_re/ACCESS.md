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
is Double press (the user's choice, 2026-10-07): retail has no skip at all.

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

### 1.2 What CleriaCore did before

Since 2026-07-23 (PLAN.md, user-requested), Esc during a frozen cutscene **skips the whole cutscene**.
`view_game.inl` frozen branch sets `skip_cutscene`, and `tick_events` drains every live script with its
waits collapsed. **This differs from retail** (§1.1); the code comment already marks it `[CleriaCore]`.
It survives as **Single press**; since 2026-10-07 the default is **Double press**, against accidental skips.

A second, smaller divergence was found but not changed. Over a script menu, CleriaCore treats Esc as the
menu's cancel (`menu && (kCancel | kMenu)`). In retail the menu-to-Cancel conversion only runs while no
Fix is held, so inside a cutscene only the Cancel binding cancels. This was traced statically and not
checked in the live game.

### 1.3 The option

`[cleria.access] CutsceneSkip`:

| Value | Name | Behaviour |
|---|---|---|
| 0 | Single press | The menu press edge skips at once (the pre-existing behaviour). |
| 1 (default) | Double press | The first press shows the prompt "Press again to skip" for **120 ticks (2 s)** [H] with a draining bar. A second press inside the window skips. |
| 2 | Hold | Hold the button for **60 ticks (1 s)** [H]. A bar fills while held. Any hold counts, including one carried in from before the cutscene or across a chain of scenes, so keeping the button held skips each scene one second in. Until 2026-10-07 a hold had to start inside the cutscene, which made a held button skip only the first scene of the opening chain (build/audit/PLAYTEST_1_STORY.md §3). A release resets the fill and keeps the hint up for 120 ticks [H]. |
| 3 | Off | Never skips (retail). |
| 4 | Always | [H] CleriaCore's own (2026-10-11), not retail: no press. The gate answers "skip" on every tick it is stepped, so a cutscene is skipped on the tick its Fix is taken and a movie on its first tick, through the pressed skip's own path (flags, grants, scene changes and the mods' events as there). Three rules a press does not need (2026-10-12, after a playthrough): a Fix **the player started with the action button** (a talk, a statue, a shop, a chest, a sign: `try_interact`) is not skipped by itself — it plays as the original plays it, and one press of the menu button skips it, as under Single press; a skip that ends with the Fix still held and no menu up is **not tried again under that Fix** (no 20000-step drain on every tick); a story scene's own menu waits for its answer (§1.5) and the skip goes on after it. Before, every answer set the skip off again: the boxes and pauses between two choices of a shop went by unread and the next choice was up at once, under the press meant for a box. Drive: `access_skip_always`, `access_skip_always_talk`, `access_skip_always_menu`, `access_always_menu_held`, `opening_skip_always`. |

- **Hook** (`view_game.inl`, the `csMgr.world_frozen()` branch of the input step):
  `access_skip_step(pressed & kMenu, held & kMenu)`. It is not stepped over a script menu (the menu
  keeps its cancel). The gate is reset on every tick without a Fix, and on the death screen.
- **Input:** the mapper record's menu bit, so keyboard, mouse and gamepad bindings all work.
- **Timing:** counted in logic ticks. The window therefore pauses while the F1 panel is open, and
  stretches with Game speed.
- **Prompt:** a `Shell::hud["access"]` layer. A top-right card (the message box owns the bottom): a
  small "SKIP CUTSCENE" head, the line, and a gold bar. It fades over the last 20 ticks of the window.
  Its look is [H] (the shell kit's palette).

### 1.4 Movies

Since 2026-10-07 the option also ends a movie (`play_demo`, view_game.inl): the menu button, in the same mode (its own gate per movie; no prompt is drawn over a movie), finishes it exactly as retail's skip does (`FUN_005758f0`). This works whether or not the movie has been seen. Off leaves only retail's skip: Attack / MOUSE_DIR held after a release, and only once the movie has been seen, or with the Prelaunch "Unlock movie skipping" setting.

### 1.5 What a skip does (the drain, `tick_events`)

Retail has no skip, so nothing here is retail's rule; the aim is **the state the same scene leaves when it is
played to its end**. One drained step = every live script's `Wait_Time` zeroed, one `tick_all`, then what the
scripts wait on is landed. Checked 2026-10-12 by playing and skipping (at the start, the middle, 40 ticks from
the end, and under Always) the entry scene of every room and every `EVT_* / EV_*` script that runs from
fresh flags, the eight boss intros and the nine duel intros, and diffing the camera rig, the scripts, the
flags, the gauge, the boss objects and the effects after the Fix is released.

| What | The rule | Why (what a skip left behind before) |
|---|---|---|
| `Wait_Script` | honest: the child drains with its parent. Given up on after 2000 drained steps [H]. | Waved through, Zava's intro (`Evt_6082_0`) let go of its Fix while `Summon` — the heads' rise — was at pc 280: the heads stayed under the floor (y -2 for 6.1) and `Summon` played on in real time over the fight. |
| Scripted map objects (`MeshWorld::finish_scripted`) | per drained step: a `Mesh_MoveTo` runs to its stop, an alpha reaches its target, a counted `Mesh_Rotate` runs out, a fall rests, a pending clip switches, a chain / stop clip runs to where it ends. | `Wait_MeshMoving` / `Wait_MeshMotionFinish` are waved through, and nothing stepped the objects: Zava's heads stopped mid-rise; Dalles began his fight in his intro's clip 17. |
| The camera rig | one `CamRig::step` per drained step. At the end the scroll ramp and the blend land (`finish_ramps`, `hard_cut`) and the shake stops. Nothing is reset: no `Scroll_Return`, no `Camera_Return` of the skip's own. | The camera ops act on the rig's **current** slot and only a step moves the selection onto a forced slot. With no step, a skipped scene's `Camera_Change` rewrote the slot the player stood in (for the rest of the room's life: S_1000's opening left slot 2 at dist 21 / pitch 0.7156 / fov 0.5411 and never entered the sticky slot 3 the played scene ends in). The skip's own `Camera_Return` drops a camera a scene means to keep: S_2099, S_3002, S_4013 and S_7080 end with a slot forced (by the code — `ret` turns the forced slot into -2, then -1; no run of the old build was made for these). |
| A script menu (`Window_Menu`) | every skip stops at it (until 2026-10-12 only Always did); it is answered by the player, with the window's own fade-in, release and fresh press (`WndDialog::update`). | A pressed skip waved it through with no row chosen: S_3015's `EV_3015_HUGO_2` then took neither branch and ended with slot 1 forced and the scroll out. |
| The scene's children that outlive its Fix (`ScriptManager::skip_tail_step`) | after the drain, the scripts born since the fix count last left 0 are run on with their `Wait_Time` collapsed (no other wait is waved), each until it ends, takes a Fix, parks where it has parked before (a loop), stands 600 steps [H] on one wait, or counts a `Wait_Time` over 600 [H] (a timer). At most 2000 steps [H]. | S_3080, Hugo: `Move_Epona01` walks Epona off and deletes her while the scene plays on. Left to real time after a skip at the start or the middle, she stood there when `Battle` set flag 130, and her own handler's defeat path ended Toal's duel five ticks in (156 = 0, 174 = 0: `Battle_Lose`, the duel lost unfought). The same in the build before this pass. |
| Effects | at the end, the effects the scripts placed (the named ones) are stepped 600 times [H]. | A boss intro's one-shot effects, all spawned in one tick, ran over the first seconds of the fight (Jenocres' warp loops, Zava's lights). Loops no script killed stay, as when played. |

Not done, known differences from a played scene: the idle clip of a boss is at another frame; Dalles is in the loop clip METEOR his intro leaves him in and his brain takes about 200 ticks to leave it (METEOR_FROM_WAIT, N_WAIT) before his first action, where a played intro has him there 39 ticks before its Fix ends; a played
scene's last scripts can still be running a few ticks after the Fix (Zava's `Yogrcs` / `Omrgan`, over within
60 ticks) where a skipped one has ended them; actors' ASO animations, bullets and the weather do not run in the
drain; under a Fix that holds slot 0 only, monsters act through a played scene (FIX_SLOTS S5c) and not
through a skipped one.

## 2. Cheats (Cheats page, `[cleria.cheats]`)

Every cheat persists across launches (in cleria.ini). The page has a Status row ("Original game" /
"Cheats active") and a **Reset all cheats** button at the top.

| Option | Key | Values | Hook site | Retail behaviour it alters |
|---|---|---|---|---|
| Infinite HP / MP / Boost / oxygen | `InfiniteHP` / `InfiniteMP` / `InfiniteBoost` / `InfiniteOxygen` | off/on | unchanged pre-existing refills before the death check (`view_game.inl`, "The shell's Cheats") | refills each tick; now persisted |
| One-hit kills | `OneHitKill` | off/on | unchanged (`player_hit_enemy`, after the guard) | regular actors only; boss parts are not covered (pre-existing) |
| Damage dealt | `DamageDealt` | 0.25x-10x | `access::damage_dealt` on the result of `enemy_incoming_damage` (FUN_004fa550 / FUN_004faf20). Four sites: `player_hit_enemy` (after the guard, before one-hit kill), the Velagunder parts, Nygtilger's `b2_damage`, Gelaldy's parts | `dmg * x`, rounded. A guarded or immune 0 stays 0; a damaging hit stays at least 1 [H, same spirit as the engine's min-1 chip] |
| Damage taken | `DamageTaken` | None (0x) to 4x | `access::damage_taken` on `player_damage_after_boost(raw)` in `hurt_player` (FUN_004fd350's player branch: FUN_004fa740, then the Boost halving) | every enemy, boss-part, bullet and Nygtilger hit. 0 = no damage (the hit reaction still runs with dmg 0). **Not** lava, the S_3009 noise, drowning or the Evil Ring (use Infinite HP) |
| EXP gained | `ExpRate` | 0.25x-10x | `award_exp` -> `expaward::kill_award` / `boss_award` (`src/cleria/exp_award`): `ExpRate` (x the Lua mods' `exp_factor`) is passed as the `rate` | is retail's EXP rate `0x76a5fc` (1.0 in every save [DOC audit/combat.md #30]): `max(1, floor(level_scale(EXP) * rate * diffMult))`, so it multiplies before the floor and before the kill's chain bonus [BYTES 0x4fcb9c / 0x4fa4fd]. It covers every award_exp caller: kills and the three bosses |
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
