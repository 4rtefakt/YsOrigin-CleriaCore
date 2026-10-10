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
| Minimum Frame Rate (1025) | `[kernel] ClampInternalFrameRateMin` | (c) | **open**: the value is read into an init slot and no consumer was found (CONFIG.md §8.1), so the bytes do not say what it does. No switch added. | — |
| Shadows (1180) | `ShadowEffectLevel` | (b) | **open** (§6): the levels scale retail's projected shadows; CleriaCore draws blob decals | — |
| Reflections (1190) | `WaterEffectLevel` | (b) | **open**: no water reflection pass exists | — |
| Blood (1170) | `[game] BloodyEffectLevel` | (b) | **done**: Video > Blood (Off / Low / High). The enemy hit / death ports already read the key (`> 0` the blood effects and red debris, `> 1` the extra ones); only the row was missing | yes |
| Glare (1171) | `GlareEffectLevel` | (b) | stays "Not available yet" (issue #82) | — |
| Hardware Vertex Shader (1101) | `ForceSoftwareVP` | n/a | **not applicable**: a D3D9 device-creation flag | — |
| Pixel Shader (1105) | `DisablePixelShader` | n/a | **not applicable**: D3D9's fixed-function fallback; CleriaCore's renderer is shaders only | — |
| Compressed Textures (1110) | `CompressedTexture` | (b) | **not applicable**: a D3D9 loader variant (convert to DXT at load to save video memory); CleriaCore uploads each DDS as stored | — |
| Low-Resolution Textures (1111) | `LowResoTexture` | (b) | **done** for map / object textures: Video > Low-resolution textures. `FUN_005fba80` looks in the file's `l\` folder first [EXPORT 005fba80.c:91-140]; the texture index is rebuilt with the L folders first (`video_low_res`). [not modelled] the sprites' `data\ani\l\*.aia` (`FUN_00444a50`) | next area |
| Other Simplification (1528) | `OtherLightWeightMode` | (b) | **open**: its three users (`FUN_0044fdd0`, `FUN_00501a30`, `FUN_0055ddc0`) are not traced | — |
| Mipmapping (1116) | `Mipmap` | (b) | **open**: honouring the key (default 1) would change the default picture; CleriaCore's own "Smooth distant textures" is the switch that loads mip chains. One of the two should go once the mip path is a port | — |
| Increase Text Resolution (1115) | `[kernel] HighResoText` | (b) | **open**: CleriaCore always uses the high-resolution text art; the low-resolution path is not ported | — |
| Disable Mask of Eyes Effect (1117) | `MaskOfEyes` | (a) | already there | yes |
| Disable Water Caustics (1118) | `WaterCaustics` | (b) | **open**: no caustics pass exists | — |
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
| Presets combo (2001) + Save (2002) | `FileIndex`, `Assign{}`; `key_NN.ky` | (a) the two presets, (c) `.ky` | Preset 1 / Preset 2 already there; **open**: saving / loading `.ky` preset files ("Kya\0", 34 u32 codes [EXPORT `FUN_00411820`]) | yes |
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
| High-Definition (1012, `FUN_00411f50`) | anisotropy = the caps maximum, trilinear, the 1024×768 mode if listed, MSAA = the first available count ≥ 2, compressed textures off |
| Light (1014, `FUN_005d3900`) | 640×480, 32-bit, bilinear, anisotropy 1, no MSAA, no low-resolution textures, shadows 1, reflections 1, glare 1 with caps else 0 |
| Lightest (1013, inline in `FUN_004192c0`) | 640×480, 16-bit, bilinear, anisotropy 1, no MSAA, low-resolution textures 1, other simplification 1, mipmap 0, shadows / reflections / glare 0 |

Light and Lightest also clear `HighResoText`. All four then set GammaEnable = fullscreen && caps.

## 6. Shadows: what the levels do [EXPORT]

- `FUN_005042e0`: the shadow render target is created at ¼ (level 0), ½ (level 1) or the full
  requested size (level 2).
- `FUN_00474cd0`: an actor asks for a projected shadow only when the level is ≠ 0, and at level 1
  only the player (`param_1 == DAT_0074ab90`).
- `FUN_00459410`, `FUN_0045c490`, `FUN_00474870`: a per-level count from the table `0x68b0ec` =
  {12, 18, 24} [BYTES].

CleriaCore draws blob decals (`SHADOW0.DDS`) under characters, not retail's projected shadows, so the
levels have nothing to scale yet.

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

- `cleria-settingstest`: the four presets; the keys behind the new rows round-trip and default to the game's.
- `cleria-actiontest`: the dash machine (the double tap, the 12-tick and angle limits, the hold, the release, AlwaysDashOK).
- `cleria-audiotest --voices`: Enable SFX off = silence with the voice still tracked, on again = heard; no device = `set_device` refuses.
- `cleria-modstest`: the vfs language overlay (under the mods, over the base). `cleria-gamedatatest`: `unpack_archive`
  on the real `data_us` as a stand-in overlay (813 files), `languages`, the credits read from the real exe.
- `drive_regress.py` (each changes the option in mid-session and reads the effect):
  `config_live_video` (TextureFilter, a preset, Blood, LowResoTexture), `config_live_input` (Double-tap to dash on / off,
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

- [H] The presets leave the window mode, the resolution, Stretch widescreen, Disable movies, Mask of Eyes and
  caustics alone (retail's reset them all, §5), and take the best case for every caps-dependent pick.
  High-Definition's anisotropy 16: retail takes a minimum of two values not read from the bytes (`FUN_005d50f0`).
- [H] The sound device index means SDL's device list, not DirectSound's.
- [approx] Enable BGMs / SFX mix silent instead of not playing (above).
- [approx] The dash machine's mover-state gate (§8).
- [H] The two "Clear ..." buttons ask with two rows instead of a message box; the cloud copies are [not modelled].
- [H] The credits are shown as rows of the Prelaunch page instead of two message boxes; en / em dashes are drawn as "-".
- [H] `<data folder>/lang/<archive>/` and `CLERIA_LANG_DIR` (CleriaCore's own layout for the overlays).

## 11. Open (the checklist on issue #85)

- Language: the exe-embedded strings / tables per language; Japanese; a check with a real `data_sp/fr/de/it`.
- Shadows, Reflections, Water caustics, Glare (#82), FSAA (MSAA), Mipmapping (the retail key), Other
  Simplification, Increase Text Resolution off, the low-resolution sprite sheets (`ani\l`).
- Minimum Frame Rate (no consumer found in the exe).
- Key preset files (`.ky`): save / load.
- Stretch Widescreen for the 2D layers (VIDEO.md §2.3).
