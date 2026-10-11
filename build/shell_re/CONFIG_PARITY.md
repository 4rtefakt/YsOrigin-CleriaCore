# config.exe parity (issue #85)

Every control of the original configuration tool, the key it writes, what the game does with it, and
where CleriaCore's shell stands. The retail side is specified in `CONFIG.md` (same folder: the ini
dialect, the key table §2, the dialog §4); this file is the control-by-control checklist.

`config.exe` is a 3 KB stub that runs `yso_win.exe -c`; the dialogs (resources 200–204) and all their
code are in `yso_win.exe` (CONFIG.md §0). Every setting is a key of `yso_win.ini`
(`%USERPROFILE%\Saved Games\FALCOM\yso_win\`); the game reads it once at boot, so **in retail every
option needs a restart** (only the 7 in-game Options rows are live).

Tags: **[BYTES]** read from `yso_win.exe` (`tools/exe_peek.py`), **[EXPORT]** the Ghidra export
(`D:\ghidra-work\export`), **[RES]** dialog resources, **[DATA]** files on disk, **[H]** hand-chosen,
**[approx]** an approximation of a traced behaviour, **[not modelled]**.

Columns: **Before** = the state at commit `e64033c`: (a) in the shell and effective, (b) in the shell but
without effect / listed under "Not available yet", (c) missing. **Now** = after the issue #85 work.
**Live** = what a change made in the shell (the F1 panel in game, Settings in the launcher) does without a
restart: **yes** = at once; **next area** = for what is loaded from then on; **restart**; — = nothing to
apply. Each "yes" / "next area" was checked in a running session (§9).

## 1. Graphics tab (dialog 201)

| Control (id) | ini key | Before | Now (where in the shell) | Live |
|---|---|---|---|---|
| Lightest / Light / Normal / High-Definition (1013 / 1014 / 1011 / 1012) | a group of `[graphics]` keys (§5) | (c) | **done**: Video > Presets (`graphics_preset`); the window mode and the resolution are left alone [H] | yes (filter: next area) |
| Gamma Correction (1022) + Normal (1023) | `GammaValue` | (a) / (c) | already there: Video > Brightness (every window mode); **done**: Reset brightness | yes |
| Full Screen (1100) | `Windowed` (inverted) | (a) | already there: Video > Window mode | yes |
| 32-bit Textures (1102) | `BackBufferFormat32` | n/a | **not applicable**: the D3D9 fullscreen back-buffer format (R5G6B5 vs X8R8G8B8); wgpu's surface is always 32-bit. The key round-trips. | — |
| FPS Display (1104) | `ShowFPS` | (a) | already there | yes |
| Enable Vertical Sync (1103) | `WaitVSync` | (a) | already there: Video > VSync (`[cleria.video] VSync`); **done**: `WaitVSync` now follows it | yes |
| Disable all movie playback (1533) | `DisableMovies` | (a) | already there | yes (the next movie) |
| Minimum Frame Rate (1025) | `[kernel] ClampInternalFrameRateMin` | (c) | **not applicable**: the game reads the value and never uses it (§16). The key round-trips; the page says so | — |
| Shadows (1180) | `ShadowEffectLevel` | (b) | **no switch**: none of the three things the level controls is drawn yet (§6, issue #86) | — |
| Reflections (1190) | `WaterEffectLevel` | (b) | **no switch**: no reflection / refraction pass exists (§12, issues #9, #12) | — |
| Blood (1170) | `[game] BloodyEffectLevel` | (b) | **done**: Video > Blood (Off / Low / High). The enemy hit / death ports already read the key (`> 0` the blood effects and red debris, `> 1` the extra ones); only the row was missing | yes |
| Glare (1171) | `GlareEffectLevel` | (b) | **done** as far as the level-1 mask rules go: Video > Glare (Off / Low / High), each level with its own blur table (SCREEN_EFFECTS.md §2.5). [H] level 2's specular highlights and [not modelled] the actors' glow are open (issue #82) | yes |
| Hardware Vertex Shader (1101) | `ForceSoftwareVP` | n/a | **not applicable**: a D3D9 device-creation flag | — |
| Pixel Shader (1105) | `DisablePixelShader` | n/a | **not applicable**: D3D9's fixed-function fallback; CleriaCore's renderer is shaders only | — |
| Compressed Textures (1110) | `CompressedTexture` | (b) | **not applicable**: a D3D9 loader variant (convert to DXT at load to save video memory); CleriaCore uploads each DDS as stored | — |
| Low-Resolution Textures (1111) | `LowResoTexture` | (b) | **done** for map / object textures: Video > Low-resolution textures. `FUN_005fba80` looks in the file's `l\` folder first [EXPORT 005fba80.c:91-140]; the texture index is rebuilt with the L folders first (`video_low_res`). **done** for the sprite sheets too: `FUN_00444a50` opens `data\ani\l\<name>.aia` when it exists [EXPORT 00444a50.c:60-69] (`aia_low_res`; 125 of the 147 sheets have an L copy, half the size with twice the header scale) | next area (the sprites loaded from then on) |
| Other Simplification (1528) | `OtherLightWeightMode` | (b) | **no switch**: its three readers are traced (§14) and none of what they change is in CleriaCore (issue #88) | — |
| Mipmapping (1116) | `Mipmap` | (b) | **done**: Video > Mipmapping (§17). On (retail's default) the map / object textures are uploaded with their files' mip chains and sampled as `FUN_005d4f00` sets the filter; off, level 0 only. **The game's default picture changes** (it drew level 0 only before) | next area |
| Increase Text Resolution (1115) | `[kernel] HighResoText` | (b) | **no switch**: it is the resolution GDI text is rasterised at, not an art folder (§15, issue #89); CleriaCore's text is a mix of the two | — |
| Disable Mask of Eyes Effect (1117) | `MaskOfEyes` | (a) | already there | yes |
| Disable Water Caustics (1118) | `WaterCaustics` | (b) | **no switch**: the caustics are specified (§13) and not drawn yet (issue #87) | — |
| Stretch Widescreen (1531) | `StretchAspect` | (a) | already there (3D; the 2D layers keep 4:3 [not modelled]) | yes |
| Disable High-Resolution Performance Counters (1530) | `DisableQPC` | n/a | **not applicable**: picks timeGetTime over QueryPerformanceCounter; CleriaCore's clock is SDL's | — |
| Adapter (1031) | `Adapter` | (a) | already there: Video > Display (shown with 2+ displays) | yes |
| Resolution (1035) | `BackBufferWidth` / `Height` | (a) | already there | yes |
| Refresh Rate (1037) | `RefreshRate` | (a) | already there (exclusive fullscreen) | yes |
| FSAA Type (1039) | `MultiSampleType` | (b) | **done**: Video > Anti-aliasing (MSAA), Off / 4x. The 3D world draws into a 4-sample target (VIDEO.md §2.7). [approx] WebGPU offers 4 samples only, so every saved count >= 2 draws with 4 | yes |
| Texture Filter (1041) | `TextureFilter` | (a) restart | **done**: the world samplers are rebuilt at once (`video_resample`) | next area |
| Anisotropic Filter (1043) | `Anisotropy` | (a) restart | **done**, as above | next area |
| Language (1045) | `[game] Language` | (c) | **done** as far as the data goes (§7): Prelaunch > Language | restart |
| European Localization Credits (603) | — | (c) | **done**: Prelaunch > European localization credits, the text read from the install's `yso_win.exe` (`gamedata::localization_credits`) | yes |
| DirectX Diagnostic Tool (601) | — | — | skipped (the user's choice) | — |

## 2. Input Assignments tab (dialog 202)

| Control (id) | ini key | Before | Now | Live |
|---|---|---|---|---|
| Keyboard / Game Pad / Mouse binding per action (2106–2125), the 8 movement keys, Mouse Movement | the 34 `Assign{}` | (a) | already there: Input > Configure Inputs | yes |
| Always use the left and right mouse buttons for confirm and cancel (2020) | `MouseButtonAlwaysOkCancel` | effective, (c) in the shell | **done**: Input > Mouse buttons confirm and cancel | yes |
| Digitize Analog Input (2021) | `PadAnalog` | (a) | already there: Analog as digital | yes |
| Double-Tap to Dash (2022) | `DashControl` | (b) | **done** (§8): Input > Double-tap to dash; the mover's dash machine is ported | yes |
| Hold Attack for Skills (2023) | `AtkBtnMagic` | (b) | **done**: Input > Hold attack for skills; sets `skillmp::SkillIn::auto_charge` [EXPORT 00465170.c:906] | yes |
| Always Dash Once Able (2024) | `AlwaysDashOK` | (a) | already there | yes |
| Presets combo (2001) + Save (2002) + Delete (2003) | `FileIndex`, `Assign{}`; `key_NN.ky` | (a) the two presets, (c) `.ky` | Preset 1 / Preset 2 already there; **done**: Save as a preset file, and one row per `.ky` found (§18). Delete is not offered (remove the file by hand). `FileIndex` (the tool's remembered combo index) round-trips | yes |
| Fixed keys (group 2030) | — | effective, not stated | **done**: Input > Fixed keys (Esc, arrow keys, mouse wheel) | — |

## 3. Input Settings tab (dialog 203)

| Control (id) | ini key | Before | Now | Live |
|---|---|---|---|---|
| Stick position preview | — | (a) | already there: Input > Controller | yes |
| Vibration Test (3006) | — | (a) | already there | yes |
| Deadzone (3004) + Reset (3005) | `DeadZone` | (a) / (c) | slider already there; **done**: Reset deadzone (0.35) | yes |
| Use Game Pad (3001) | `GamePad` | (a) | already there | yes |
| Use Force Feedback (3002) | `ForceFeedback` | (a) | already there: Vibration | yes |
| Device combo (1531) | `GamePadID` | (a) | already there: Use controller | yes (the pad is reopened) |

## 4. Sound / Other tab (dialog 204)

The page's command handler is at `0x41cd80` (not a function in the export) [BYTES]: 4001 (0xfa1) the
device combo → `0x72ff8c`; 4010 (0xfaa) / 4012 (0xfac) / 4020 (0xfb4) / 4021 (0xfb5) toggle the bytes
`0x72ff90..93` = PlayBgm / PlayEffect / Reverb / ForceSoftwareSoundBuffer. This settles CONFIG.md §8.3.

| Control (id) | ini key | Before | Now | Live |
|---|---|---|---|---|
| Device (4001) | `[sound] Device` | (c) | **done**: Audio > Output device. 0 = the system default, n = SDL's n-th playback device [H: retail's index is DirectSound's]; the stream is reopened on it (`Audio::set_device`) | yes |
| BGM Volume (4011) / SFX Volume (4013) | `BgmVolume` / `EffectVolume` | (a) | already there | yes |
| Enable BGMs (4010) | `PlayBgm` | (c) | **done**: Audio > Music. [approx] retail does not start the stream at all; here it runs on and is mixed silent, so it comes back at once | yes |
| Enable SFX (4012) | `PlayEffect` | (c) | **done**: Audio > Sound effects. [approx] retail skips the play calls; here the voices are mixed silent (their clock is unchanged) | yes |
| EAX 2.0 (4020) | `Reverb` | n/a | **not applicable**: EAX environment reverb on a DirectSound hardware buffer (greyed in the user's own config.exe). The key round-trips | — |
| Force Sound Buffer Into System Memory (4021) | `ForceSoftwareSoundBuffer` | n/a | **not applicable**: a DirectSound buffer flag; CleriaCore mixes in software | — |
| Clear Time Attack Records (4501) | files | (c) | **done**: Audio > Other > Clear Time Attack records. Retail [BYTES `0x41d230..0x41d577`]: "Deleting Time Attack data. Continue?" (Yes / No), then `record.ta`, `.tae`, `.ar`, `.are` deleted from the save folder and the cloud, then "Deleted.". Here the same question as two rows (No / Yes, delete), the same four files from the save folder | yes |
| Clear Game Progress (4502) | files | (c) | **done**: Audio > Other > Clear game progress. Retail [BYTES `0x41cf17..0x41d178`]: `record.ta2`, `.ar2`, `.tp`. Here the same three files, and the clear data in memory is reset, so the title's gates (Bonus Modes, Toal, the difficulties) close at once | yes |

## 5. The four presets [EXPORT]

All four start from `FUN_005d3640`, which **zeroes the whole graphics struct** (so: fullscreen, no
stretch, movies on, Mask of Eyes and caustics on, no low-resolution textures, no simplification) and
then sets: the adapter's current display mode, refresh default, texture filter 1 (2 = trilinear with
the mip caps), anisotropy min(caps, 2), mipmap 1, gamma 1.0, MSAA 0, VSync 1, shadows / reflections /
glare 2 (glare 0 below Windows-version code 11), 32-bit and compressed textures per caps.

| Preset | On top of that |
|---|---|
| Normal (1011, `FUN_00411f10`) | nothing |
| High-Definition (1012, `FUN_00411f50`) | anisotropy = min(caps maximum, 4) (`FUN_005d50f0` is `min`, called with the caps value and the literal 4 [BYTES 0x411f65..0x411f89]), trilinear, the 1024×768 mode if listed, MSAA = the first available count ≥ 2, compressed textures off |
| Light (1014, `FUN_005d3900`) | 640×480, 32-bit, bilinear, anisotropy 1, no MSAA, no low-resolution textures, shadows 1, reflections 1, glare 1 with caps else 0 |
| Lightest (1013, inline in `FUN_004192c0`) | 640×480, 16-bit, bilinear, anisotropy 1, no MSAA, low-resolution textures 1, other simplification 1, mipmap 0, shadows / reflections / glare 0 |

Light and Lightest also clear `HighResoText` (the shared tail at 0x4193bb [EXPORT 004192c0.c:105-112]); Normal
and High-Definition do not touch it. All four then set GammaEnable = fullscreen && caps. No preset touches a key
outside the graphics struct other than that one (not ShowFPS, Blood, Language, the sound or input keys).

What the memset clears, field by field [EXPORT 005d3640.c:17-74]: +0x18 StretchAspect, +0x1c RefreshRate,
+0x20 MultiSampleType, +0x31 Windowed, +0x33 ForceSoftwareVP, +0x34 DisablePixelShader, +0x35 TripleBuffer,
+0x37 LowResoTexture, +0x39 OtherLightWeightMode, +0x3b DisableMovies, +0x63 MaskOfEyes, +0x64 WaterCaustics.

CleriaCore's `graphics_preset` sets all of these as retail does, **except** the window mode, the resolution and
the display, which it leaves alone (a deliberate deviation: a click should not throw the window into another
mode), and it takes the best case for every caps-dependent pick.

## 6. Shadows: what the levels do [EXPORT] (issue #86)

`ShadowEffectLevel` (`0x765198`) drives three unrelated things. No level removes a shadow.

**(a) The actors' round shadow: a flat quad, or the same texture laid on the floor's triangles.**
`FUN_00474cd0` [EXPORT 00474cd0.c:17-37] sets the mode byte `actor+0x2fc` (1 to start) to 0 only when
`(+0x12c & 2) == 0`, the level is ≠ 0 and (the level is > 1 or the actor is the player `DAT_0074ab90`),
`+0x2b8 != 0`, and the actor is within 15 units of the player (d² < 225, `0x6bdbe4`) or 36 of the camera target
(d² < 1296, `0x6bded0`); back to 1 when the linked object `+0x554` is a boss part or has no collision mesh.
The draw `FUN_00474e10` [EXPORT 00474e10.c:131-133]: mode 0 -> `FUN_00456740` clips the collision triangles of
every map object near the actor into the decal's box (`FUN_005e9f30`, callback `FUN_004558c0`), so the shadow
follows steps and slopes; when that fails (a full vertex buffer) or in mode 1 -> `FUN_00456da0`, one quad tilted
by the floor normal, alpha `a/255 − d²·0.025`, half-size `size − d²·0.025`. So: level 0 every actor a flat quad;
level 1 the player's follows the floor; level 2 everyone's in range.

**(b) Boss model shadows** (GoMesh types 5, 6, 9, 0xa, 0xb, 0xc). `FUN_005042e0` creates the boss's render target
[EXPORT 005042e0.c:17-41]: 256² (64² for type 6) divided by 4 / 2 / 1 for level 0 / 1 / 2, clamped to pool
surface 2's size. `FUN_005043e0` renders into it each frame through `FUN_00457a90`: a camera above the boss
looking down (`PerspectiveFovLH(π/4, 1, 0.1, h + 15)`), the model drawn (type 6: the model `+0x270` below
level 2, `+0x268` at 2 [EXPORT 005043e0.c:104-112]; the attached object only at level ≥ 2
[EXPORT 00457a90.c:173]), then a blur through pool surface 2. Receivers: map materials with flag `0x10000000`
(`FUN_005f92b0` variant 0xc, the `_trn_shadowed` shaders) [EXPORT 005f92b0.c:235, 361-441].

**(c) Toal's afterimage trail length.** The table `0x68b0ec` = {12, 18, 24} [BYTES] is the number of positions
kept (`FUN_00459410` [EXPORT 00459410.c:419-427], `FUN_0045c490`) and drawn as (N − 1) × 6 additive copies of
the sprite quad (`FUN_00474870`).

CleriaCore: `draw_shadow` draws a flat quad of `SHADOW0.DDS` (level 0's shape, without the tilt and the d²
terms); no floor-following decal, no boss shadow, no trail. The slider has nothing to scale, so there is no row.
[UNKNOWN] what sets `+0x2b8`.

## 7. Language

Retail [EXPORT `FUN_005d68d0`, `FUN_0040ccf0`, `FUN_00419b60`]:
- `Language` selects one overlay archive mounted over `release\data`: `data_us` (1), `data_sp` (2),
  `data_fr` (3), `data_de` (4), `data_it` (5) [strings `0x6b8978..0x6b8998`]; Japanese (0) mounts none.
  A missing archive falls back to English, then Japanese. The combo's entries are disabled when
  `release\data_xx.na` is missing and the whole combo is disabled under Steam (Steam's game language
  decides, which is why it is greyed in the user's config.exe).
- The exe also carries per-language strings and tables (HUD words such as "Level Up" / "Niveau sup.",
  menu labels, the key-name tables, the BGM table `bgmdef2` for Japanese, movie paths; about 70 text sites).

CleriaCore:
- The data folder is `data` + `data_us` merged (`gamedata`), so **English needs nothing more**.
- Another language's archive is unpacked on its own into `<data folder>/lang/<archive>/` (the same
  relative names): `gamedata::install` does it for every `release\data_sp / _fr / _de / _it` the install
  has (`unpack_archive`). `$CLERIA_LANG_DIR` replaces `<data folder>/lang` for the lookup.
- At start `language_startup` lays the chosen language's folder over the asset root
  (`vfs::set_language`: under the mods, over the base files). A language whose folder is missing runs in
  English, as retail falls back.
- Prelaunch > Language lists only the languages that can run: English, plus each one whose folder exists
  (`gamedata::languages`). The launcher's status line names the running language.
- **Applies after a restart**: the assets a language replaces (script text, item tables, menu and title
  images) are loaded once at start and held by many owners; there is no reload point short of a restart.
  The page says so and shows "Restart CleriaCore to change the language." while the choice differs.

**With this machine's install** [DATA]: `release\` holds `data.ni/.na/.nu` and `data_us.ni/.na/.nu` only
([H] Steam delivers only the selected language's archive). **English is the only language that works here**, and
the only one listed.

What the other languages need:
- Spanish / French / German / Italian: that language's `data_xx.ni` + `.na` in the install's `release\`
  folder (set the game's language in Steam so it downloads the archive), then unpack the game data again
  (Prelaunch > Locate Ys Origin / Find automatically). The files in the archive are then used.
  **[not modelled]** the words built into the exe stay English: the HUD's words, the pause book's
  built-in labels, the per-language boss names (`FUN_005bff40`), and whatever layout the other languages'
  text needs (the `0x6d5d9c` tests at the ~70 text sites). **[H]** untested with a real archive: the
  mechanism is tested with stand-in data (§9).
- Japanese: the base archive alone. Not offered: the merged data folder has the English files in place
  of the 813 Japanese ones, and Japanese text needs the Shift-JIS glyph path, which CleriaCore's text
  code does not have.

## 8. Double-Tap to Dash: the dash machine [BYTES / EXPORT `FUN_00465170`]

The dash flag is `player+0xa00` (`FUN_00460c10` returns `this+0xcc` of the sub-object at `+0x8f0`
[BYTES `0x460c10`, `0x4665a3`]); with it set, the run section 1 becomes `0x10a` [BYTES `0x468cb4`].
All of it needs the dash owned (`DAT_0076bbf0 > 0`, game flag 181).

- Held attack or skill sets it [BYTES `0x466afb`]; a release clears it and the latch `DAT_0076c1c4`
  unless the machine state `+0x720` is 4 [BYTES `0x466596..0x4665b5`]. (Already modelled.)
- Mover state 0xd: a latched dash that was cleared, or `AlwaysDashOK`, sets state 4 and the flag again
  [EXPORT 00465170.c:203-208].
- `DashControl` (`0x6d606e`) [BYTES `0x4654a0..0x465641`]: state 0 → 1 at neutral (no direction bits, or
  magnitude < 0.25); 1 → 2 at a direction, its input angle kept in `+0x724`; 2 → 3 at neutral, or back
  to 0 after 12 ticks; in 3, a direction within 2.243995 rad (`0x6bd5cc`) of the first within 12 ticks →
  state 4, the flag and the latch set. While the flag is set: at neutral the machine restarts, and with
  neither attack nor skill held the flag and the latch clear.
- The in-game Options rows that toggle DashControl, turn AlwaysDashOK off, or reset to Default clear
  `+0xa00`, `+0x728`, `+0x720` and the latch [EXPORT 005ba2b0.c:225-269].

CleriaCore: `PlayerActions` (`dash_state`, `dash_t`, `dash_angle`, `dash_latch`, `dash_reset`),
`In::dash_control` from the Options copy. **A behaviour change at the defaults**: `DashControl`'s default
is 1 (retail's), so with the dash owned a double tap of a direction now dashes; before, only a held
attack / skill or Always dash did. Every drive case and golden still passes unchanged. [approx] the mover-state test
(`+0x124 == 0xd`) is taken as always true, as the AlwaysDashOK path already did; the Default row's clear
happens only when it changes one of the two options.

## 9. Testing

- `cleria-settingstest`: the four presets (what each sets, what the memset clears); the keys behind the new rows round-trip and default to the game's.
- `cleria-inputtest`: the `.ky` layout, its round trip and the loader's refusals. `cleria-videotest`: the sample count.
- `drive_regress.py` `config_ky_preset`: Save writes `key_00.ky` then `key_01.ky` into the settings folder; a saved file loads.
- By hand (2026-10-11): `LowResoTexture=1` in S_1002 — the player's sprite is the L sheet, the same size on screen,
  blurrier (`scratchpad/batchF/lowres_on/`); Mipmap 0 / 1 / trilinear in S_1009 (`scratchpad/batchF/mip1/sheet.png`).
- `cleria-actiontest`: the dash machine (the double tap, the 12-tick and angle limits, the hold, the release, AlwaysDashOK).
- `cleria-audiotest --voices`: Enable SFX off = silence with the voice still tracked, on again = heard; no device = `set_device` refuses.
- `cleria-modstest`: the vfs language overlay (under the mods, over the base). `cleria-gamedatatest`: `unpack_archive`
  on the real `data_us` as a stand-in overlay (813 files), `languages`, the credits read from the real exe.
- `drive_regress.py` (each changes the option in mid-session and reads the effect):
  `config_live_video` (TextureFilter, a preset, Blood, LowResoTexture, MultiSampleType, Mipmap), `caustics_live`
  (S_2008: the room-wide flag, the frame and the scroll; WaterCaustics on / off), `config_live_input` (Double-tap to dash on / off,
  Hold attack for skills off / on), `config_live_audio` (Enable BGMs / SFX both ways), `config_clear_progress`
  (record.ta2 read at boot, cleared, the title's gates closed in the same run).
- Manual runs (2026-10-10): the output device switched in mid-session on a real device with music and effects off
  (`vopt=SoundDevice=1 / 2 / 99 / 0` → device 1, 2, 0 (out of range → default), 0); the launcher backdrop switched while the
  launcher is up (`CLERIA_LAUNCH_SWITCH=S_2007`, `S_7000`: the capture shows the new room); a stand-in language overlay through
  `CLERIA_LANG_DIR` with `Language=2` (6 textures replaced, 13 467 pixels of the launcher frame differ, the status line says
  Spanish); the low-resolution textures (the same frame, visibly blurrier).
- Drive commands added: `vopt=` keys TextureFilter, Anisotropy, Preset, LowResoTexture, BloodyEffectLevel, PlayBgm,
  PlayEffect, SoundDevice, DashControl, AtkBtnMagic, AlwaysDashOK, MouseButtonAlwaysOkCancel; `atk+` / `atk-` (the attack
  button held); `dstat` (the dash machine and the skill charge); `cleardata=ta|progress`; `shell=Prelaunch:credits`.

## 10. Hand-chosen and approximate

- The presets leave the window mode, the resolution and the display alone (a deliberate deviation, §5) and take
  the best case for every caps-dependent pick [H].
- [approx] MSAA draws with 4 samples whatever count is saved, and multisamples the blended draws too (VIDEO.md §2.7).
- [approx] Mipmapping: an uncompressed texture whose file has no mip chain gets a box-filtered one (§17).
- [H] `.ky` files are saved in CleriaCore's settings folder, not the original game's; with Xbox-style button names
  on, the PAD_* codes saved are the DirectInput-style ones of yso_win.ini, not the named binds.
- [H] The sound device index means SDL's device list, not DirectSound's.
- [approx] Enable BGMs / SFX mix silent instead of not playing (above).
- [approx] The dash machine's mover-state gate (§8).
- [H] The two "Clear ..." buttons ask with two rows instead of a message box; the cloud copies are [not modelled].
- [H] The credits are shown as rows of the Prelaunch page instead of two message boxes; en / em dashes are drawn as "-".
- [H] `<data folder>/lang/<archive>/` and `CLERIA_LANG_DIR` (CleriaCore's own layout for the overlays).

## 11. Open (the checklist on issue #85)

- Language: the exe-embedded strings / tables per language; Japanese; a check with a real `data_sp/fr/de/it`.
- Options whose feature CleriaCore does not have yet (no switch until it does): Shadows (#86), Reflections
  (#9, #12), Other Simplification (#88), Increase Text Resolution off (#89).
- Stretch Widescreen for the 2D layers (VIDEO.md §2.3).

Provenance of §6 and §12..§18: read from the export and the bytes on 2026-10-11 by two research passes. Re-read
by hand since: the `.ky` layout, the L-folder sprite path, High-Definition's anisotropy, the preset memset, the
`Mipmap` level force, the tick accumulator of §16, the MSAA render state. The other citations are the passes' own:
re-read them before building on them.

## 12. Reflections: what `WaterEffectLevel` (`0x765199`) changes [EXPORT]

| Reader | What the level changes |
|---|---|
| `FUN_005e2e70` | `c = (level != 2) + 1`: pool surface 1 (the frame grab) = full / c, surface 5 (the reflection) = full / 2c [EXPORT 005e2e70.c:509] (SCREEN_EFFECTS.md §1.1) |
| `FUN_005dfea0` | the reflection pass runs when `0x7b2e3c != 0 && level != 0` [EXPORT 005dfea0.c:46] |
| `FUN_005f92b0` | material flags `0x200000` -> variant 8 and `0x400000` -> variant 9 only when the level is ≠ 0 [EXPORT 005f92b0.c:189-195]; at 0 a plain textured draw |
| `FUN_005f7e20` | the water material's stages (below) |
| `FUN_00562990` | an object with `+0x298 & 0x100000` queues `FUN_00562650` when the level is ≠ 0 [EXPORT 00562990.c:243-245] |

`FUN_00562650` draws the object again through `D3DXMatrixReflect` about the plane through its origin, normal +Y:
the mirrored chests, boxes and save statues of `s_6014` / `s_6082` (`takara_00`, `box_00..03`, `save_01..03`
[EXPORT 00501a30.c:268-286]). Missing from SCREEN_EFFECTS.md §5.2.

The water material (`FUN_005f7e20`) is a four-stage fixed-function setup: stage 0 `data\image\water_dudv_256.dds`
as `BUMPENVMAP` (matrix `0x7b2e58..64`); stage 1 pool texture 1 (the frame grabbed before layer 0xb: a
refraction), `BLENDFACTORALPHA` with the material colour as TFACTOR, projected; flag `0x400000` (water2) stops
there; stage 2 the dudv map again; stage 3 pool texture 5 (the reflection), `BLENDDIFFUSEALPHA`, projected. Bump
matrix per frame [EXPORT 005dfea0.c:37-45]: θ = phase `0x7b2e54` × 0.2, M00 = M11 = 0.01 f(θ), M10 = −M01 =
0.01 g(θ) ([H] which of f / g is sin). Vertex constants [EXPORT 005f92b0.c:346-355]: c23.x = the phase (+0.02 an
update), c23.z = 0.075, c23.w = 0.08, c22.w = 0.85.

**Not a complete spec** (so nothing is built): the oblique projection column of the reflected camera, the
sin / cos order of the bump matrix, which materials carry `0x200000` / `0x400000`, the boss mirror branch at
`0x5634da`. CleriaCore has none of the passes.

## 13. Water caustics [EXPORT / BYTES] (issue #87)

- **Texture**: 96 frames `data\image\caustics\01\c_%03d.bmp`, 1..96 (128², 8-bit grey; all in the data)
  [EXPORT 004548d0.c:35-72], one R5G6B5 texture re-filled on a frame change (`FUN_00454b30`, `FUN_00454c30`):
  frame = (counter / 2) % 96, the counter stepping once an update while `0x74c0f0` [EXPORT 00454c30.c:28-74].
  An update is a live sim step (`0x74c0f0`). The file's rows are turned top row first (`FUN_005fb7f0`
  [EXPORT 005fb7f0.c:61-69]); a pixel is its 8-bit index as a grey (the palette is the identity ramp [DATA]).
- **Scroll**: (0, 0) at room load [BYTES 0x431576], + (0.0005, 0.00015) a live step outside the menu pause
  [BYTES 0x42807e..0x4280c7]; c3 = (scroll.x, scroll.y, 0.2, 0.4): `FUN_005df530` copies `0x6be6e0..ec` to
  `0x7b2e24..30`, the frame overwrites x, y and z (`0x76a5c8` = 0.2 [BYTES 0x431520]).
- **Coordinates** (the `*_cvr` vertex shaders, listings `B_NDST1_TRN_CVR.VSO`, `B_DST1_CVR.VSO`): `r0 = viewPos x c8`,
  `uv1 = (r0.x, r0.z + c3.w r0.y) c3.z + c3.xy`. c8 = the transposed inverse view (`D3DXMatrixInverse` of
  `0x795e70` into `0x795ef0`, `D3DXMatrixTranspose` into `0x795f70` [BYTES 0x457c68..0x457cc3], copied to c8 by
  `FUN_005ed000`), so r0 is the world position: `uv1 = (X, Z + 0.4 Y) x 0.2 + scroll`.
- **Room mode** `0x76a544` [EXPORT 00431840.c:98-108]: 2 for `s_2008`, `s_2009`, `s_2010`, `s_2014`, `s_2015`
  (table `0x68a20c`), 1 when a map object has a water collision mesh, else 0. `FUN_00427c90`: the room-wide flag
  `0x7b2e34 = (mode == 2)`, **forced 0 when `WaterCaustics` = 1** [EXPORT 00427c90.c:137-141].
- **Map materials** (`FUN_005f92b0` [EXPORT 005f92b0.c:166-200, 266-293]), after the tests for flags
  `0x20000000`, `0x100`, `0x200`: variant 3 for material flag `0x2000` or the pass flag `0x400` (every GoMesh of
  types 0 and `FUN_00562990`'s while `0x7b2e34` is set [EXPORT 00561f40.c:51-53, 00562990.c:212-214]) **unless the
  material has flag `0x40`** (the water surfaces); else variant 2 for material flag `0x4000`.
  Variant 3 (`FUN_005f7230(1)` [BYTES 0x5f9f88] [EXPORT 005f7230.c:55-125]): after the material's stage 0
  (texture x diffuse), stage 1 = `ADD(caustic, current)`, alpha `MODULATE(texture, current)`; stage 2, no texture,
  = `MODULATE(diffuse, current)`. So the pixel is `(tex x diffuse + caustic) x diffuse`: a flooded room's own
  colours are multiplied by the vertex colour twice. Variant 2 (`FUN_005f7230(0)` [BYTES 0x5f9f2d]): stage 0 =
  the caustic texture in place of the material's, `MODULATE` with the diffuse, alpha = the diffuse alpha, and the
  blend mode forced to 2 (additive) [EXPORT 005f92b0.c:274-284]. The option removes only the room-wide part.
  [DATA] flag `0x2000`: the pool floors `20yuka72 / 82 / 92` of S_2001, S_2002, S_2016, S_2017, S_2100, S_2101,
  and S_9008 (with `z_upwater`); flag `0x4000`: S_9008 and S_TEST4 only.
- **Actor sprites** (`FUN_00474290`, `FUN_004745a0` [EXPORT 00474290.c:108-123]): when `+0x38a != 0 && +0x389 == 0
  && +0x4ba != 0x12 && WaterCaustics == 0` — i.e. every actor in the five rooms, and an actor 2.0 or more deep
  elsewhere. `FUN_005e0630` [EXPORT 005e0630.c:21-33]: stage 1 = the caustic texture, the preset's combiner 2
  (`ADD(caustic, current)`, no third stage [EXPORT 005eb5b0.c:58-70, 120-130]), shader `b_dst1_cvr`. Not for
  additive presets (bit 0x80).

CleriaCore (2026-10-11): all of the above. `g_caustic` / `caustic_stage` / `caustic_upload`
(`view_gpu_room.inl`), the world shader's `u.cvr` and `cvr_stages` (`main.cpp`); a draw that takes the stage binds
group 0 with the combiner's number. The Video page's "Disable water caustics" is `WaterCaustics`, read every
frame. Measured in S_2008 (1280 x 800): 2.78 ms a frame without, 2.80 ms with.
[approx] the frame is sampled at level 0 (retail asks for generated mips when it can); every batch of the room
mesh takes the room-wide flag (GoMesh type 2, the camera-following model, is not told apart); an enemy's wading
byte is not kept (`in_water` stands for submerged); the frame counter stops with the scroll under a pause;
[H] the stage's filter is linear.

## 14. Other Simplification: the three readers of `OtherLightWeightMode` (`0x765171`) [EXPORT] (issue #88)

1. `FUN_00501a30` (the scene-object constructor) [EXPORT 00501a30.c:125-143]: only in `s_70\s_7099\` (Darm).
   The object `s_7099` (the room) loads `data\map\skybox\b_7099\b_7099.ymo` instead, at scale × 0.5
   [BYTES 0x6bd304], with the Mesh_Rotate block set to 0.0005 rad a step about Y for ever (`+0x40c`, frames −1);
   `s7099kumo` loads `data\map\mapobj\s7099box\s7099box.ymo`. Both files are in the data.
2. `FUN_0044fdd0` (an EF2 emitter's spawn) [EXPORT 0044fdd0.c:56-81]: an emitter with no child emitters
   (`B2+0x194..0x19b` all 0xff), `(u16 B2[0] & 0x718)` = 0x100 or 8, and `B2+0x894`, `B2+0x16`, `B2+2` all 0,
   drops its whole emission with probability 1/2 (one xorshift draw, taken either way) — unless the effect has
   bit 0x100 of `+0x1dc`, which `FUN_00450410` always sets [EXPORT 00450410.c:125]. [H] so in practice never.
3. `FUN_0055ddc0` (Darm's part update) [BYTES 0x55f7c8..0x55f7e1]: bit 0x20 of `0x765553`, read by
   `FUN_005f92b0` (flag-0x10 materials keep their depth write [EXPORT 005f92b0.c:555-561]) and `FUN_005fb0e0`
   (their 0xbf pre-pass is skipped [EXPORT 005fb0e0.c:176-182]), for models whose flags have bit 0.

None of it is in CleriaCore.

## 15. Increase Text Resolution: `HighResoText` (`0x6d606c`, default 1) [EXPORT] (issue #89)

Not an art folder (CONFIG.md said the `H\` folders; those belong to the texture loader). It is the resolution
GDI text is rasterised at:

- `FUN_00442260` [EXPORT 00442260.c:270-273, 334-335]: the font height is 0x20 when off,
  `BackBufferHeight × 32 / 768` when on.
- `FUN_005ef460(font, w, h, flag)` [EXPORT 005ef460.c:42-83], the text surface: when on, `h = H × h / 768`,
  `w = (W × w >> 10) × [0x765178]` (the 2D aspect factor), both rounded up to a power of two; the icon font is
  `data\image\extfont{20,25,32,36,40}.bmp` — off: always 32; on, by the height: < 600 -> 20, 600..767 -> 25,
  768..863 -> 32, 864..959 -> 36, ≥ 960 -> 40.
- The layout (`FUN_005efb50`, `FUN_005f03f0`): when on, tag advances are scaled (`x × W >> 10`, `y × H / 768`)
  and measured sizes converted back to 1024 × 768 units.

CleriaCore: the menu font is rasterised at `renderH × 32 / 768` (the On raster); the window text's font at 32 px
and the icon font always `EXTFONT32.BMP` (retail's Off, and On only at heights 768..863). A real switch needs
the scaled layout ported. [UNKNOWN] the filter used when the 1024 × 768 text is stretched (Off).

## 16. Minimum Frame Rate: no consumer [EXPORT / BYTES]

`FUN_0040bf50` reads `ClampInternalFrameRateMin` / `Max` (defaults 5 / 10000; max clamped to [60, 10000], min to
≤ max) into the init struct's slots [11] / [12] (+0x2c / +0x30) [EXPORT 0040bf50.c:299-322]. The struct's only
receiver, `FUN_005d68d0`, reads +0x00..+0x28 and nothing after, and does not pass the pointer on
[BYTES 0x5d6918..0x5d6e88]. The tool's own copies `0x730070` / `0x730074` are read only by the tool (load, save,
defaults, the combo, the refresh) [EXPORT globals.tsv]. The timing code has no clamp: `FUN_005ccc30` adds
`speed × dt_ms` to an accumulator and counts whole 16.6667 ms ticks [EXPORT 005ccc30.c:68-72]; `FUN_00410440`
runs that many updates (or one). So the option does nothing in retail; CleriaCore keeps the two values in the
file and has nothing to port. This closes CONFIG.md §8.1.

## 17. Mipmapping: `Mipmap` (`0x765172`, default 1) [EXPORT / BYTES]

- `FUN_005fba80` (the DDS loader): `Mipmap` = 0 forces the level count to 1 [EXPORT 005fba80.c:71-80]; else the
  descriptor's count. The game's own reader then creates the texture with the **file's own** mip count and
  copies each level from the file [EXPORT 00602370.c:41-52].
- Which textures ask for mips (the level argument at each call of `0x5fc4b0` [BYTES]): the model and map
  material textures (`FUN_005f2d90`), the boss images, the `FUN_00457410` list (debris). Not: the menu / HUD
  images, the sprite sheets (`CreateTexture` in `0x444f70`, `0x445bd0`, `0x44aae0`), the font, the render
  targets. In the data 5,538 of 5,638 `MAP` DDS files carry their own chain.
- The sampler (`FUN_005d4f00`, table `0x6b754c`, VIDEO.md §2.2): MIPFILTER is POINT at Point / Bilinear and
  LINEAR at Trilinear, never NONE; no LOD bias is ever set. **At retail's defaults a map texture is sampled from
  its nearest mip level.**
- Also under the key: the caustics texture and the reflection target get auto-generated mips (not in CleriaCore).

CleriaCore (`g_world_mips`, `video_mips`): with `Mipmap` = 1 a room's and its objects' textures are uploaded
with the file's chain (`load_dds(.., true)`) and bound with the world sampler, which now reaches every level
(`lodMaxClamp`); the mip filter is the table's (nearest, or linear at Trilinear / with anisotropy). With 0, level
0 only — the picture CleriaCore drew before 2026-10-11. "Smooth distant textures" (CleriaCore's own) stays as an
extra on top: trilinear and at least 8x anisotropy whatever the two retail rows say.

- **A change at the defaults**: the live game now draws with mips, as retail does: distant floors are slightly
  softer and shimmer less (S_1009, 1280 x 800: 29% of the pixels differ, mean difference 2.9 / 255). The
  headless renders (the goldens) have no settings and still draw level 0; every drive log is unchanged.
- [approx] an uncompressed texture without its own chain gets a box-filtered one (`load_dds`); a block-compressed
  one only the levels its file holds. [not modelled] the debris and boss-image textures' mips.
- Measured (`CLERIA_PROF`, 8 windows of 60 frames, two runs): S_1009 1.26-1.36 ms a frame off, 1.27-1.33 ms on;
  S_7099 1.67-1.72 off, 1.69-1.76 on (no measurable cost).

## 18. Key preset files (`.ky`) [EXPORT / BYTES]

- **File** (0x98 bytes, little-endian) [EXPORT 00411820.c:209-241]: u32 `0x0061794b` ("Kya\0"), u32 0, u32 4,
  u32 0x22, then the 34 codes in `Assign{}` slot order and code space. Saved through a Save dialog in the
  settings folder; the suggested name is the first `key_%02u.ky` (0..99) that does not exist [EXPORT 00411820.c:89-140].
- **Loader** `FUN_004122d0` [EXPORT 004122d0.c:79-131]: the extension `.ky`, dword 0 = the magic, dword 1 = 0,
  then 0x88 bytes of codes.
- **The list** (`FUN_004125e0`): Preset 1, Preset 2, then every valid file found under the settings folder, by
  its stem. Picking one copies its 34 codes into the bindings; picking a built-in one also resets the five
  option bytes (1, 0, 1, 0, 0) [BYTES 0x4181d9..0x418253]. Control 2003 is Delete ("Delete %S?")
  [BYTES 0x41828f..0x41834f]. `FileIndex` is the remembered index; the bindings always come from `Assign{}`.

CleriaCore (`input::ky_encode` / `ky_decode`, `ky_list` / `ky_save` / `ky_load` in view_input.inl): Input >
Configure Inputs > Presets lists every `.ky` of CleriaCore's settings folder and (read only) of the original
game's, and "Save as a preset file" writes the next `key_NN.ky` into CleriaCore's folder. A file made by either
program loads in the other. [not modelled] the built-in presets' reset of the five option switches; Delete.
