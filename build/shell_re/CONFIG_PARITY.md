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

State before this work (commit `e64033c`): **(a)** in the shell and effective, **(b)** in the shell but
without effect / listed under "Not available yet", **(c)** missing.

## 1. Graphics tab (dialog 201)

| Control (id) | ini key | What the game does | Before |
|---|---|---|---|
| Lightest / Light / Normal / High-Definition (1013 / 1014 / 1011 / 1012) | a group of `[graphics]` keys (§5) | set the keys below in one click [EXPORT `FUN_004192c0`, `FUN_005d3640`, `FUN_005d3900`, `FUN_00411f50`] | (c) |
| Gamma Correction slider (1022) + Normal (1023) | `GammaValue` 0.5..2.0 | gamma ramp `FUN_005d4de0`, fullscreen only | (a) slider (Video > Brightness, every window mode); (c) the Normal button |
| Full Screen (1100) | `Windowed` (inverted) | present parameters | (a) Video > Window mode |
| 32-bit Textures (1102) | `BackBufferFormat32` | fullscreen back buffer R5G6B5 vs X8R8G8B8 | n/a |
| FPS Display (1104) | `ShowFPS` 0..2 | `FUN_00429530` | (a) |
| Enable Vertical Sync (1103) | `WaitVSync` | fullscreen presentation interval | (a) Video > VSync, stored as `[cleria.video] VSync` only |
| Disable all movie playback (1533) | `DisableMovies` | `FUN_005773e0` finishes at once | (a) |
| Minimum Frame Rate (1025) | `[kernel] ClampInternalFrameRateMin` {12, 10, 5, 1} | read into init slot [11]; consumer not found (CONFIG.md §8.1) | (c) |
| Shadows (1180) | `ShadowEffectLevel` 0..2 | §6 | (b) |
| Reflections (1190) | `WaterEffectLevel` 0..2 | water reflection passes `FUN_00562990`, `FUN_005f7e20`, … | (b) |
| Blood (1170) | `[game] BloodyEffectLevel` 0..2 | enemy hit / death effects | (b) on the page, although the engine ports already read it |
| Glare (1171) | `GlareEffectLevel` 0..2 | bloom `FUN_005f4420`, … | (b), issue #82 |
| Hardware Vertex Shader (1101) | `ForceSoftwareVP` (inverted) | D3D create flag | n/a |
| Pixel Shader (1105) | `DisablePixelShader` (inverted) | D3D path switch | n/a |
| Compressed Textures (1110) | `CompressedTexture` | loader `FUN_005fba80` | (b) |
| Low-Resolution Textures (1111) | `LowResoTexture` | `FUN_00444a50`, `FUN_005fba80` | (b) |
| Other Simplification (1528) | `OtherLightWeightMode` | `FUN_0044fdd0`, `FUN_00501a30`, `FUN_0055ddc0` | (b) |
| Mipmapping (1116) | `Mipmap` | texture load | (b) (CleriaCore's own "Smooth distant textures" exists) |
| Increase Text Resolution (1115) | `[kernel] HighResoText` | text art variant | (b) always on |
| Disable Mask of Eyes Effect (1117) | `MaskOfEyes` | post effect `FUN_005fed30` | (a) |
| Disable Water Caustics (1118) | `WaterCaustics` | `FUN_00427c90`, `FUN_00474290`, `FUN_004745a0` | (b) |
| Stretch Widescreen (1531) | `StretchAspect` | projection aspect `FUN_005e2670` | (a) (3D only) |
| Disable High-Resolution Performance Counters (1530) | `DisableQPC` | timer source | n/a |
| Adapter (1031) | `Adapter` | D3D adapter | (a) Video > Display (with 2+ displays) |
| Resolution (1035) | `BackBufferWidth` / `Height` | back buffer | (a) |
| Refresh Rate (1037) | `RefreshRate` | fullscreen refresh | (a) |
| FSAA Type (1039) | `MultiSampleType` | MSAA | (b) |
| Texture Filter (1041) | `TextureFilter` | sampler `FUN_005d4f00` | (a) on restart |
| Anisotropic Filter (1043) | `Anisotropy` | sampler | (a) on restart |
| Language (1045) | `[game] Language` 0 JP, 1 EN, 2 ES, 3 FR, 4 DE, 5 IT | §7 | (c) |
| European Localization Credits (603) | — | two message boxes, the text in the exe [EXPORT `FUN_004192c0`, strings `0x695888..0x696072`] | (c) |
| DirectX Diagnostic Tool (601) | — | runs DXDIAG.EXE | skipped (the user's choice) |

## 2. Input Assignments tab (dialog 202)

| Control (id) | ini key | What the game does | Before |
|---|---|---|---|
| Keyboard / Game Pad / Mouse binding per action (2106–2125) | `Assign{KEY_*}`, `Assign{PAD_*}`, `Assign{MOUSE_*}`, `Assign{MOUSE_DIR}` (34 slots) | mapper `FUN_005d2ba0` | (a) Input > Configure Inputs: all 34 slots, the 4 diagonals and Mouse Movement included |
| Always use the left and right mouse buttons for confirm and cancel (2020) | `MouseButtonAlwaysOkCancel` | mapper bits 0x4000 / 0x10000 | effective (the mapper reads it) but (c) in the shell |
| Digitize Analog Input (2021) | `PadAnalog` | stick magnitude forced to 1.0 | (a) |
| Double-Tap to Dash (2022) | `DashControl` | the double-tap dash machine in the mover `FUN_00465170` (§8) | (b): a row exists, nothing reads it |
| Hold Attack for Skills (2023) | `AtkBtnMagic` | the skill charge runs while Attack is held [EXPORT `00465170.c:906`] | (b): a row exists, `skillmp::SkillIn::auto_charge` is never set |
| Always Dash Once Able (2024) | `AlwaysDashOK` | mover state 0xd forces the dash | (a) |
| Presets combo (2001): Preset 1 [Arrows/ZXCV], Preset 2 [WASD/Mouse], user `.ky` files; Save (2002) | `FileIndex`, the 34 `Assign{}`; `key_NN.ky` | tables `0x703b20` / `0x703c30`; `.ky` = "Kya\0" header + 34 u32 codes [EXPORT `FUN_00411820`] | (a) the two presets; (c) `.ky` files |
| Fixed keys (group 2030): ESC = Menu, cursor keys = movement, mouse wheel = switch | — | fixed in the mapper | effective; (c) not stated in the shell |

## 3. Input Settings tab (dialog 203)

| Control (id) | ini key | Before |
|---|---|---|
| Stick position preview | — | (a) Input > Controller (the live pad picture) |
| Vibration Test (3006) | — | (a) |
| Deadzone (3004) + Reset (3005 = 0.35) | `DeadZone` | (a) slider; (c) Reset |
| Use Game Pad (3001) | `GamePad` | (a) |
| Use Force Feedback (3002) | `ForceFeedback` | (a) |
| Device combo (1531) | `GamePadID` | (a) |

## 4. Sound / Other tab (dialog 204)

The page's command handler is at `0x41cd80` (not a function in the export) [BYTES]: 4001 (0xfa1) the
device combo → `0x72ff8c`; 4010 (0xfaa) / 4012 (0xfac) / 4020 (0xfb4) / 4021 (0xfb5) toggle the bytes
`0x72ff90..93` = PlayBgm / PlayEffect / Reverb / ForceSoftwareSoundBuffer. This settles CONFIG.md §8.3.

| Control (id) | ini key | What the game does | Before |
|---|---|---|---|
| Device (4001) | `[sound] Device` | DirectSound device index | (c) |
| BGM Volume (4011) / SFX Volume (4013) | `BgmVolume` / `EffectVolume` 0..1024 | centibel taper | (a) |
| Enable BGMs (4010) | `PlayBgm` | gates BGM playback | (c) |
| Enable SFX (4012) | `PlayEffect` | read at ~70 SE call sites | (c) |
| EAX 2.0 (4020) | `Reverb` | EAX environment switching | n/a |
| Force Sound Buffer Into System Memory (4021) | `ForceSoftwareSoundBuffer` | DirectSound buffer flag | n/a |
| Clear Time Attack Records (4501) | files | asks "Deleting Time Attack data. Continue?" (Yes / No), then deletes `record.ta`, `record.tae`, `record.ar`, `record.are` from the save folder and the Steam / Galaxy cloud, then says "Deleted." [BYTES `0x41d230..0x41d577`] | (c) |
| Clear Game Progress (4502) | files | asks "Deleting game completion data. Continue?", then deletes `record.ta2`, `record.ar2`, `record.tp` the same way [BYTES `0x41cf17..0x41d178`] | (c) |

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
  `data_fr` (3), `data_de` (4), `data_it` (5); Japanese (0) mounts none. A missing archive falls back
  to English, then Japanese. The combo's entries are disabled when `release\data_xx.na` is missing and
  the whole combo is disabled under Steam (Steam's game language decides).
- The exe also carries per-language strings and tables (HUD words, menu labels, the key-name tables,
  the BGM table `bgmdef2` for Japanese, movie paths; about 70 text sites).

This machine's install [DATA]: `release\` holds `data.ni/.na/.nu` and `data_us.ni/.na/.nu` only. So the
only complete language here is **English**.
