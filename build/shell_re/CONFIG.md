# Retail settings: storage, config tool, game readers, input system

Scope: everything the user can configure in retail Ys Origin (Steam build `yso_win.exe`, 2024-01-08
"smallfix"), as groundwork for the CleriaCore settings shell. Read-only RE; nothing in the install
was modified.

Evidence tags: **[BYTES]** read from the exe with `tools/exe_peek.py` / pefile, **[EXPORT]** Ghidra
export `D:\ghidra-work\export` (decompile / globals / strings), **[DATA]** files on disk,
**[RES]** PE resources (dialog templates), **[H]** inference. Function names are `FUN_xxxxxxxx` from
the export. Globals are absolute VAs.

Scripts and dumps for this doc: `%TEMP%\claude\...\scratchpad\agent_config\` (`pe_info.py`,
`cfgdis.py`, `dlg.py` dialog parser, `keys.py` / `defs.py` table dumps, `dlg_*.txt`).

---

## 0. Summary

- **`config.exe` is a 3 KB stub.** It sets the working directory to its own folder, then runs
  `"yso_win_dbg.exe" -c` (if that file exists) or `"yso_win.exe" -c`, and exits. It contains no UI and no
  settings code: its imports are only `CreateFileA`, `CreateProcessA` and `wsprintfA`, and its strings
  are `0x402108..0x402124`. [BYTES config.exe `0x401010..0x401126`, table `0x4021b8` = {`yso_win_dbg.exe`,
  `yso_win.exe`}]
- **The configuration dialog lives inside `yso_win.exe`.** The `-c` switch → `FUN_00415d70` (Win32
  dialogs 200–204, class `ClassYsOConfig`, mutex `MutexYsOCfg`). If the user presses **Play**, the same
  process goes on to boot the game. OK, Cancel and Apply exit instead. [EXPORT `FUN_0040af50`,
  `FUN_00415d70`]
- **All settings live in one INI**, `yso_win.ini`, in the save folder. Saved Games is the default
  location. Sections are the root (`IniVersion`), `[kernel]`, `[game]`, `[graphics]`, `[sound]`,
  `[input]` and `[debug]`. Two side files exist: `config.ini` holds only the dialog's own page and
  position, and `common.ini` next to the exe chooses the save-folder location. **No setting is stored
  in the registry.** [EXPORT, DATA]
- **The game reads the INI once at boot** (`FUN_0040d1d0` → `FUN_0040bf50` + `FUN_0040b8e0`). It writes
  back a **subset** only at clean shutdown (`FUN_0040dcc0` → `FUN_0040db00` + `FUN_0040d7e0`) and on
  game clear (`FUN_0058f210`). The in-game Options screen changes globals **live**, but they persist
  only through those writes. [EXPORT]
- **The input system** is DirectInput8: keyboard, mouse and one joystick. There are 34 binding slots.
  The per-frame mapper `FUN_005d2ba0` turns them into the held/pressed bitmasks that the player copies
  to `+0x510` / `+0x50c`. The bit layout is fully decoded below (§3.4). [EXPORT, BYTES]

---

## 1. Storage

### 1.1 Files

| File | Location | Written by | Read by | Content |
|---|---|---|---|---|
| `common.ini` | exe folder | game, only when `UserProfileHere` is missing (it also writes comments). Not written by the config tool. | game `FUN_0040d1d0`, config tool `FUN_004155e0` | `BaseDir="..."` (chdir target, `0x7493d0`), `UserProfileHere` (0 = profile folder, 1 = exe folder; `0x7493c8` / `0x7494d5`) [EXPORT, DATA] |
| `yso_win.ini` | **save dir** (below) | game (exit, game clear), config tool (OK/Apply/Play) | game boot, config tool | all settings (§2) [EXPORT, DATA] |
| `config.ini` | save dir | config tool `FUN_00415d70` | config tool `FUN_004155e0` | root keys `CurrentPage` (`0x73005c`), `EnvPosX` / `EnvPosY` (`0x730064` / `68`). Dialog state only; the game never reads it. [EXPORT] |
| `key_NN.ky`, `*.ky` | save dir (Save dialog starts there) | config tool `FUN_00411820` ("Save Key Config") | config tool (preset combo 2001) | binding preset file: 16-byte header (`"Kya\0"`, dword at +0xC = `0x22` = 34), then 34 × u32 key codes (0x88 bytes) [EXPORT `FUN_00411820`] |

**Save dir** (`FUN_005de1d0`, `FUN_005ce950`) [EXPORT, BYTES]:
- `UserProfileHere=0`: `SHGetKnownFolderPath(FOLDERID_SavedGames {4C5C32FF-BB9D-43B0-B5B4-2D72E54EAAA4})`
  + `\FALCOM\` + `yso_win\`. The GUID is at `0x68fe04`, the suffixes at `0x6b95b8` / `0x6b72d4`. On
  this machine the result is `C:\Users\kores\Saved Games\FALCOM\yso_win\`. [DATA] Pre-Vista and Win9x
  fallbacks exist (`FUN_00600c10`, an exe-relative `\falcom` walk).
- `UserProfileHere=1`: `user\<GetUserNameA>\`, relative to `BaseDir` or the exe folder.

**Steam/GOG cloud** [EXPORT `FUN_0040d1d0`, H for the vtable names]: at boot, if the Steam
remote-storage interface (`0x7493b8`, vtable `+0x34` FileExists) or the Galaxy one (`0x7493b4`) reports
a cloud copy of `config.ini` or `yso_win.ini`, the game deletes it (`+0x18` FileDelete / `+0x0c`). The
INI files are therefore kept local. The config tool's footer text describes where saves and config go
(`0x6974f8..`). The `-R<n>` switch toggles Steam cloud use (vtable `+0x54` with `n>1`).

### 1.2 Registry: no settings [EXPORT]

`ADVAPI32 Reg*` is used for three things only:
- `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\DXDIAG.EXE`, read by the "DirectX
  Diagnostic Tool" button (`FUN_0041b270`).
- `Software\Microsoft\Direct3D\LoadDebugRuntime`, read in `FUN_005d3fd0` via `FUN_005e7dd0` /
  `FUN_005e7eb0`.
- `HKCU\SOFTWARE\Falcom\YSO_WIN "Serial key"`, written by `FUN_005a37e0`, which **has no callers**.

`build/arch_re/IMPORTS.md` §3 claims "config lives partly in the registry". **That claim is wrong.**
On this machine `HKCU\Software\Falcom` is visible to `reg.exe` but has no values or subkeys. [DATA]

### 1.3 INI dialect (Falcom ini library `0x5ce9d0..0x5d0f70`) [EXPORT]

| Function | Role |
|---|---|
| `FUN_005cefd0(path)` + `FUN_005cf090()` | open + parse |
| `FUN_005cfe70(section)` | select section; `""` (`0x694c4c`) = the root, before the first `[...]` |
| `FUN_005d07f0(key,&int)` | read int with `strtol`: `0x`/`0X` → hex, `0b`/`0B` → binary, else decimal. Returns `0x80ff0008` (<0) if the key is absent, and **every caller keeps its default** then. |
| `FUN_005cff30(key)` | raw node; used for floats (`atof`) and for `GamePadID` / `DoubleClickFrame` / `Language` (strtol) |
| `FUN_005d0590(key,buf,n)` | read string: strips the trailing `"`, MBCS (Shift-JIS) aware |
| `FUN_005d06f0(key,val,radix)` | write int (`radix 16` → `0x` prefix: `IniVersion=0x100`) |
| `FUN_005d04d0(key,str)` | write a `"quoted"` string; bytes < 0x20 are dropped |
| floats | written as `"%0.8f"` (`GammaValue`, `DeadZone`, `RunWalkThreshold`, `MouseSensitivity`) |
| `FUN_005cf580()` | save the whole tree |

Every writer loads the file, edits the in-memory tree and saves it. Keys a writer doesn't touch
therefore survive [EXPORT; H that unknown *sections* also survive, since the tree is generic].

**Version gate:** root `IniVersion` must equal `0x100`. If it doesn't:
- the game skips the early `[kernel] HighResoText` / `[graphics] Windowed` reads and the whole
  `FUN_0040b8e0` pass ([game] section, the 4 Option flags, ShowFPS, PlayBgm/PlayEffect/Reverb, the second
  Language read) [EXPORT `FUN_0040d1d0` `if (DVar2 == 0x100)`];
- the config tool treats the file as absent and rewrites **every** key with defaults (`FUN_004126e0` →
  `FUN_004133e0`).

**A CleriaCore-written ini must carry `IniVersion=0x100`.**

### 1.4 This machine's file [DATA]

`C:\Users\kores\Saved Games\FALCOM\yso_win\yso_win.ini` (2026-08-02) holds the complete key set listed
in §2: `IniVersion=0x100`, 2560×1440 windowed, MSAA 8, Preset-2 (WASD) bindings written in the
config-tool spelling (`"Button1"`, `"L-Shift"`), `BgmVolume=22`, `EffectVolume=129`. There is no
`GameFirstPlay` line, which means it has never been set. `config.ini` holds `CurrentPage=0`,
`EnvPosX=718`, `EnvPosY=423`.

---

## 2. Master key table

Columns:
- **Game read**: the reader function; the global the value lands in.
- **When**: B = boot only; R = re-applied at runtime from another source.
- **W**: who writes the key. g = game at exit or clear; c = config tool.
- **UI**: config-dialog control ID (dialogs 201–204), or the in-game Options row.

Missing-key defaults:
- **Game default** comes from the `FUN_0040bf50` locals, or from the `.data` initial value of the
  global [BYTES `.data` raw ends at `0x748200`; beyond that is zero-filled].
- **Tool default** comes from `FUN_004155e0`.

### 2.1 Root, `[kernel]`, `[debug]`

| Key | Type / range | Game default | Game read → global | When | Consumer | W | UI |
|---|---|---|---|---|---|---|---|
| `IniVersion` | hex int, must be `0x100` | — | `FUN_0040d1d0` (local) | B | gate (§1.3) | g,c | — |
| `WindowPosX` / `WindowPosY` | int px | 64 / 64 | `FUN_0040b600` (window create); `FUN_0040bf50` → param[3]/[4] → `0x703d40` / `0x703d44` | B | windowed window origin; updated by `FUN_005d4ab0` [H move/reset] | g only (`FUN_0040db00`) | — (tool neither reads nor writes it) |
| `EnglishRoonic` | 0/1 | 0 | `FUN_0040bf50` → gfx+0x65 → `0x76519d` | B | `FUN_005efb50`, `FUN_005f03f0` (text/glyph path) [H: Roonic-script text in Latin letters] | g,c | none found |
| `ClampInternalFrameRateMin` | int | 5 | `FUN_0040bf50` → param[11] (clamped ≤ max) | B | **no consumer** (confirmed 2026-10-11, CONFIG_PARITY.md §16) | c | combo **1025** "Minimum Frame Rate": {12, 10, 5, 1} (`0x689ed4`) |
| `ClampInternalFrameRateMax` | int | 10000 | `FUN_0040bf50` → param[12], clamped to [60, 10000] | B | not found | c | — |
| `AccessArchiveViaMemoryMappedFile` | 0/1 | 1 | `FUN_0040bf50` → param[10] | B | archive open (`FUN_005de7f0`) | c | — |
| `HighResoText` | 0/1 | `0x6d606c` init = **1** | `FUN_0040d1d0` → `0x6d606c` (only if IniVersion is OK) | B | text: `FUN_00442260`, `FUN_005781f0`, `FUN_00578710`, `FUN_0057a050`, `FUN_005a3850` | c | checkbox **1115** "Increase Text Resolution" (JP: pixel-exact text) |
| `[debug] NoInitializeGameCtrl` | 0/1 | 0 | `FUN_0040bf50`: clears bit 2 of the DInput init flags (`0xf`) → no joystick | B | `FUN_005d5d60` | — | JP-only debug dialog 206, id 5000 [RES] |

### 2.2 `[game]` (section string `0x6942a4`)

| Key | Type | Game default | Game read → global | When | Consumer | W | UI |
|---|---|---|---|---|---|---|---|
| `Language` | 0 JP, 1 EN, 2 ES, 3 FR, 4 DE, 5 IT | 1 | `FUN_0040bf50` and `FUN_0040b8e0` → `0x6d5d9c`, then `setlocale(jpn/enu/esp/fra/deu/ita)` (`0x6943d4..e8`) | B (restart) | `FUN_005d68d0` mounts overlay archive `release\data_{us,sp,fr,de,it}` (failure → JP and return `0x23d`); BGM table (`bgmdef2` for JP); movie paths; about 70 text sites | g (always), c (only without Steam) | combo **1045**: English, French, German, Spanish, Italian → {1,3,4,2,5} (`0x689e3a`). Entries are disabled when `release\data_xx.na` is missing; the whole combo is disabled under Steam. |
| `BloodyEffectLevel` | 0..2 | 2 (`.data`) | `FUN_0040b8e0` → `0x6d5d92` | B | enemy/death FX (`FUN_00478510`, `FUN_004faf20`, `FUN_005130c0`, …) | g,c | trackbar **1170** "Blood" (0..2) |
| `SaveFileNo` | int | 0 | → `0x74ac3e` | B | last save slot | g | — |
| `TitleCursorIndex` | int | 0 | → `0x74ac3d` | B | title menu cursor memory | g | — |
| `TitleGameDifficulty` | int | 0 | → `0x74ac3c` | B | last difficulty | g | — |
| `TitleTimeAttackIndex` | int | 0 | → `0x74ab9f` (−1 saved as 0) | B | Time Attack menu memory | g | — |
| `GameFirstPlay` | 0/1 | 0 | → `0x74ab9b` | B | movie-skip gate (`FUN_00577750`, MOVIES.md) | g (**only if ≠ 0**; set by the ending `FUN_0058f210`) | — |
| `GameSD` | 0/1 | 0 | → `0x74ab97` | B | crash-backup prompt (`FUN_005b5100`, `FUN_00431840`) | g (always `1`) | — |
| `PlayerName` | string ≤ 0x32 | `"Adol Christin"` | → `0x6d6044`; the surname is split at the separator into `0x6d602c` | B | name display | g | — |
| `AlternateNameSeparator` | string ≤ 10 | `" "` | → `0x6d6040` | B | as above | g | — |

**Language under Steam** [EXPORT `FUN_0040ccf0`]. When `steam_api.dll` is present and `SteamAPI_Init`
succeeds (`0x7493be && !0x7493bd`), the language comes from the Steam game language:
- `japanese` → 0, `french` → 3, `spanish` → 2, `german` → 4, `italian` → 5;
- anything else → 1;
- then: missing archive → EN; EN missing too → JP.

In that case the INI `Language` is ignored (`FUN_0040bf50` / `FUN_0040b8e0` keep `0x6d5d9c`).

### 2.3 `[graphics]` → `FUN_0040bf50` locals → copied 0x88 bytes to **`0x765138`** by `FUN_005d3fd0` [EXPORT, BYTES]

Struct offsets were verified by name-offset consistency:
- `+0x2c` → `0x765164` = GammaValue (matches OPTIONS.md);
- `+0x3b` → `0x765173` = DisableMovies (matches MOVIES.md);
- `+0x65` → `0x76519d` = EnglishRoonic (matches the `FUN_0040db00` write);
- the struct ends at `0x7651c0` = the device pointer.

`FUN_005d43d0` builds `D3DPRESENT_PARAMETERS` at `0x765100` from the struct.

| Key | Range | Default (game / tool) | Global | Consumer / effect | UI control |
|---|---|---|---|---|---|
| `Device` | int | −1 / −1 | `0x765140` | `CreateDevice(adapter, DevType = Device+1, …)` (HAL = 0) [EXPORT `0x5d3fd0`] | — |
| `Adapter` | int | 0 | `0x765144` | adapter index | combo **1031** "Adapter" (`"%S (Display %d)"`) |
| `BackBufferWidth` / `Height` | px | 800×600 | `0x765148` / `0x76514c`, plus `0x7035c4` / `0x7035c8` (plain copies: the cursor mapping's divisors and the warp's, MOUSE.md §1) | PP `BackBufferWidth/Height`; window size | combo **1035** "Resolution": `"%u x %u"` from the adapter mode list |
| `StretchAspect` | 0/1 | 0 | `0x765150` | viewport / letterbox: `FUN_005acf90`, `FUN_005d3ab0`, `FUN_005e2670` | checkbox **1531** "Stretch Widescreen" |
| `RefreshRate` | Hz, 0 = default | 0 | `0x765154` | `FullScreen_RefreshRateInHz`, validated against the mode list | combo **1037** (beside the "Refresh Rate" label): "Default", `"%u Hz"` |
| `MultiSampleType` | 0, 2..16 | 0 | `0x765158` | PP MultiSampleType (`0x765110`) | combo **1039** (beside the "FSAA Type" label): "None", `"%u samples"` |
| `TextureFilter` | 0 Point, 1 Bilinear, 2 Trilinear | 1 | `0x76515c` | sampler states `FUN_005d4f00` | combo **1041** (Trilinear only if caps allow) |
| `Anisotropy` | 1, 2, 4, 8, 16 | 1 | `0x765160` | `FUN_005d4f00` | combo **1043**: "None", `"%d samples"` up to the caps max |
| `GammaValue` | float 0.5..2.0 | 1.0 | **`0x765164`** | gamma ramp `FUN_005d4de0` (`SetGammaRamp`), **only if `GammaEnable`** | trackbar **1022** (0..10000 → `g = t/1e4 + 0.5`, and if g > 1 then `g = 2(g−1)+1`), button **1023** "Normal" = 5000. **In-game Options row 2.** |
| `BackBufferFormat32` | 0/1 | 0 | `0x765168` | fullscreen: R5G6B5 (`0x17`) vs X8R8G8B8. Windowed always follows the desktop format and overwrites the flag. | checkbox **1102** "32-bit Textures" |
| `Windowed` | 0/1 | **0** (bss) | `0x765169`; also `0x7493ca` read early by `FUN_0040d1d0` | PP Windowed, window style. **Alt+Enter toggles `0x7493ca` at runtime** (`FUN_0040efb0` WM_SYSKEYDOWN VK_RETURN); this is not saved. | checkbox **1100** "Full Screen" (shown as `!Windowed`) |
| `WaitVSync` | 0/1 | 0 | `0x76516a` | fullscreen `PresentationInterval` ONE vs IMMEDIATE; windowed keeps DEFAULT; also read by the main loop `FUN_00410440` | checkbox **1103** "Enable Vertical Sync" |
| `ForceSoftwareVP` | 0/1 | 0 | `0x76516b` | behaviour flags `|= D3DCREATE_SOFTWARE_VERTEXPROCESSING` | checkbox **1101** "Hardware Vertex Shader" (inverted) |
| `DisablePixelShader` | 0/1 | 0 | `0x76516c` | `0x764c2f` (PS path usable) | checkbox **1105** "Pixel Shader" (inverted) |
| `TripleBuffer` | 0/1 | 0 | `0x76516d` | `BackBufferCount = 1 + TripleBuffer` | **no control**; the presets set it to 0 |
| `CompressedTexture` | 0/1 | 0 | `0x76516e` | texture loader `FUN_005fba80` | checkbox **1110** |
| `LowResoTexture` | 0/1 | 0 | `0x76516f` | `FUN_00444a50`, `FUN_005fba80` | checkbox **1111** |
| `GammaEnable` | 0/1 | 0 | `0x765170` | gates `FUN_005d4de0`. The tool forces 0 when windowed or when caps are missing. | checkbox **1021** "Active" [RES; handler not located] |
| `OtherLightWeightMode` | 0/1 | 0 | `0x765171` | FX: `FUN_0044fdd0`, `FUN_00501a30`, `FUN_0055ddc0` | checkbox **1528** "Other Simplification" |
| `Mipmap` | 0/1 | 1 | `0x765172` | texture load: `FUN_00454b30`, `FUN_005fba80`, `FUN_005dfea0` | checkbox **1116** "Mipmapping" |
| `DisableMovies` | 0/1 | 0 | **`0x765173`** | `FUN_005773e0`: skip the movie straight to finish | checkbox **1533** "Disable all movie playback" |
| `ShadowEffectLevel` | 0..2 | 2 | `0x765198` | shadows: `FUN_00457a90`, `FUN_0045c490`, `FUN_005042e0`, … | trackbar **1180** "Shadows" |
| `WaterEffectLevel` | 0..2 | 2 | `0x765199` | water: `FUN_00562990`, `FUN_005f7e20`, … (forced 0 without the caps) | trackbar **1190** "Reflections" |
| `GlareEffectLevel` | 0..2 | 2 | `0x76519a` | glare/bloom: `FUN_005f4420`, `FUN_005fd750`, …. The game forces 0 when `Windowed && !BackBufferFormat32`; the tool caps it at 1 without PS 1.1. | trackbar **1171** "Glare" |
| `MaskOfEyes` | 0/1 (1 = effect **off**) | 0 | `0x76519b` | post effect `FUN_005fed30` | checkbox **1117** "Disable Mask of Eyes Effect" |
| `WaterCaustics` | 0/1 (1 = caustics **off**) | 0 | `0x76519c` | `FUN_00427c90`, `FUN_00474290`, `FUN_004745a0` | checkbox **1118** "Disable Water Caustics" |
| `DeviceIdentifier` | `"<%08X-%04X-%04X-%04X-%04X%08X>"` | — | parsed by `FUN_005d00f0` → `0x7651b0` | read in `FUN_005d43d0` [H: adapter identity check] | written only by the tool |
| `DisableQPC` | 0/1 | 0 | `0x7493bf` | `FUN_005ccab0(!DisableQPC)` timer source [H QPC vs timeGetTime] | checkbox **1530** |
| `ShowFPS` | 0..2 | 0 | `FUN_0040b8e0` → `0x6d6069` | FPS overlay; **Shift+F1 cycles 0→1→2** at runtime (`FUN_0040e940`) | checkbox **1104** "FPS Display" |

### 2.4 `[sound]` → `FUN_0040bf50` sound struct → copied 0x128 bytes to `0x764608` (`FUN_005d68d0`) [EXPORT, BYTES]

| Key | Range | Default | Global / path | Effect | UI |
|---|---|---|---|---|---|
| `Device` | int | 0 | struct +0x0c → `0x764614` | DirectSound device | combo **4001** "Device" |
| `BgmVolume` | 0..1024 | 512 | +0x04 → `FUN_005ce7f0(ecx)` [BYTES `0x5d6daa`] → **`0x7035bc`** | BGM gain = `clamp(3322·log10(vol·voiceVol/2^20), −10000, 0)` cB, silent ≤ 0.002, full ≥ 0.998 [BYTES consts `0x6bcfa4`=2⁻²⁰, `0x6bda04`=33.22, `0x6bdafc`=100] | trackbar **4011** (0..10000; `t = v·10000>>10`, `v = (t<<10)/10000`). **Options row 0.** |
| `EffectVolume` | 0..1024 | 512 | +0x08 → `FUN_005ce820(ecx)` [BYTES `0x5d6db2`] → **`0x7035cc`** | same curve, per SE voice | trackbar **4013**. **Options row 1.** |
| `PlayBgm` | 0/1 | 1 | `FUN_0040b8e0` → `0x6d6066` | gates BGM playback (`FUN_00432ee0` …). If both PlayBgm and PlayEffect are 0, struct+0x21 is set and the DSound and AudioMgr init is skipped. Without DSound, `0x6d6066` is zeroed (word). | checkbox **4010** "Enable BGMs" |
| `PlayEffect` | 0/1 | 1 | → `0x6d6067` | read at about 70 SE call sites | checkbox **4012** "Enable SFX" |
| `Reverb` | 0/1 | 0 | struct+0x20 and `0x6d6068` [BYTES `0x40be74`] | EAX environment switching: the player mover picks env 0x16 / 0x19 (`FUN_00465170`), plus `FUN_00431840` and `FUN_00439ee0`. Forced 0 when ForceSoftwareSoundBuffer is set. | checkbox **4020** "EAX 2.0" |
| `ForceSoftwareSoundBuffer` | 0/1 | 0 | struct+0x22 | buffers in system memory | checkbox **4021** |

The fixed format is 44100 Hz, 16-bit, 2 channels, with `0x385` voices (struct +0x10..+0x1c). The music
path is `<cwd>\release\music\` (+0x23). [EXPORT]

### 2.5 `[input]`

| Key | Range | Default (game) | Read → global | Consumer | UI |
|---|---|---|---|---|---|
| `MouseButtonAlwaysOkCancel` | 0/1 | **1** | `FUN_0040bf50` struct+0x9c → `0x764880` | mapper (§3.4): L/R mouse button = OK/Cancel | checkbox **2020** |
| `PadAnalog` | 0/1 | 0 | struct+0x9d → **`0x75aed6`** (mapper); `FUN_0040b8e0` → `0x6d606d` | stick magnitude forced to 1.0 ("Digitize Analog Input") | checkbox **2021**; **Options row 3** (also mirrors to `0x75aed6`) |
| `DashControl` | 0/1 | **1** (dword `0x6d606d = 0x100`) | `FUN_0040b8e0` → `0x6d606e` | `FUN_00465170`: double-tap dash FSM (`+0x720/+0x724/+0x728/+0xa00`), only when `0x76bbf0 > 0` | checkbox **2022**; **Options row 4** |
| `AtkBtnMagic` | 0/1 | 0 | → **`0x6d606f`** | `FUN_00465170` @ skill charge (`(+0x510 & 4) || 0x6d606f`), `FUN_0046ea00` (the "auto-charge" of ABILITIES_VERIFIED) | checkbox **2023** "Hold Attack for Skills"; **Options row 5** |
| `AlwaysDashOK` | 0/1 | 0 | → **`0x6d6070`** | `FUN_00465170`: in state 0xd, with dash owned, forces `+0x720 = 4`, `+0xa00 = 1` | checkbox **2024**; **Options row 6** |
| `ForceFeedback` | 0/1 | 0 | → `0x6d6064` → `0x6d5d93` | rumble `FUN_005d3500` | checkbox **3002** (forced 0 without a pad) |
| `GamePad` | 0/1 | 1 | → `0x6d6065` → **`0x6d602b`** | mapper: pad buttons and stick enabled; rumble | checkbox **3001** "Use Game Pad" |
| `GamePadID` | int (dec/0x) | caller EDX | → `0x764b4c` | joystick enumeration pick, 1-based (`FUN_005d5b20`) | combo **1531** on page 203: `sel + 1` |
| `FileIndex` | int | — | game: only checks presence | game: if present, an empty `Assign{}` = **unbound** (−1); if absent, empty = keep the default. Config tool: the selected preset index (`0x72ffa8`; 0 = Preset 1, 1 = Preset 2, ≥ 2 = a .ky file) [H] | combo **2001** |
| `Assign{SLOT}` ×34 | key name (§3.2) | per-language table (§3.3) | → **`0x7647f8[34]`** (struct+4) | mapper | buttons **2106–2125** + statics **2126–2159** |
| `DeadZone` | float 0..1 | 0.35 | → `0x7647a4` (clamped 0..1); effective `clamp(dz, 0.05, 0.95)·1024` → int `0x7647a8` | mapper stick threshold (squared compare) | trackbar **3004** (0..10000), **3005** "Reset" = 3500 |
| `RunWalkThreshold` | float | 0.5 | → `0x764b48` | mover: `0x764b48 <= magnitude` → run (`FUN_00465170` l.432) | — |
| `MouseSensitivity` | float | 1.0 | struct+0x98, **not copied** by `FUN_005d68d0` [BYTES `0x5d6af2..0x5d6b8a`] | **unused** [H] | — |
| `DoubleClickFrame` | int | 12 | → `0x7647a0` | mapper: MOUSE_DIR double click within N frames → pressed bit 0x800 | — |

**Not in any file**: `0x7035c0` (8 = 8-way stick, 4 = 4-way). The pause book (`GoWndMain2` ctor
`FUN_005a4ab0`) temporarily sets deadzone 0.8 (`0x7647a8 = 0x333`) and 4-way; `FUN_005a4b60` /
`FUN_005a4de0` restore the saved deadzone and 8-way. [EXPORT]

---

## 3. Input system

### 3.1 DirectInput setup and poll [EXPORT `FUN_005d5d60`, `FUN_005d6170`]

`DirectInput8Create(…, 0x800, …)`. Init flags `0xf`: bit0 keyboard, bit1 mouse, bit2 joystick (cleared
by `NoInitializeGameCtrl`). Cooperative level is NONEXCLUSIVE|FOREGROUND.

Each frame, `FUN_005d6170` fills one of two 0x160-byte state blocks at `0x764888 + k·0x160`:

| Offset | Content |
|---|---|
| `+0x000` | keyboard[256], DIK-indexed |
| `+0x100` | `DIMOUSESTATE`: lX, lY, lZ are written but **never read** (the warp zeroes lX/lY); the cursor is `GetCursorPos` and the wheel is `WM_MOUSEWHEEL` (MOUSE.md §1.1) [EXPORT globals.tsv, `FUN_0040efb0`]. Buttons at `+0x10c` = `0x764994` (L), `0x764995` (R), `0x764996` (M). L/R are swapped when `SM_SWAPBUTTON` is set (`0x75aed7`). |
| `+0x110` | `DIJOYSTATE` (0x50 bytes): lX `0x764998`, lY `0x76499c`, lRx `0x7649a4`, lRy `0x7649a8`, POV[4] `0x7649b8`, buttons `0x7649c8` |

### 3.2 Key codes and names [BYTES table `0x68edf8`, 131 × {u32 code, char* name}]

- `0x01..0xdd`: **DirectInput DIK scan codes**, named e.g. `Esc`, `1`, `Q`, `L-Shift`, `Numpad 8`,
  `Up`, `Home`, `L-Win`, `Apps`. They are the JP-keyboard names:
  - `0x2b` `_`, `0x7d` `\`, `0x90` `^`, `0x91` `@`, `0x92` `:`;
  - a Falcom bug: `0xc9` (DIK_PRIOR) is named `Page Down` and `0xd1` (DIK_NEXT) is named `Page Up`.
- `0x100` `L-Button`, `0x101` `R-Button`, `0x102` `M-Button`.
- `0x103..0x106` `---`. These are placeholder codes that read as never-pressed (the mapper only reads
  mouse buttons < 0x103), so `---` = unbound.
- `0x107..0x116` `Button 1`..`Button 16` = `DIJOYSTATE.rgbButtons[0..15]`.
- `0xffffffff` = unbound (an empty string when `FileIndex` is present).

**Parsing** (`FUN_0040bf50`, `FUN_004126e0`): both sides pass through `FUN_005cc390`, which strips
spaces and tabs, then compare with `_stricmp`. So `"Button1"` == `"Button 1"` and matching is
case-insensitive. **Writing** (`FUN_004133e0`) emits the table name with spaces stripped (`"Button1"`,
`"Numpad8"`); an unknown code is written as `""`.

### 3.3 Slots and defaults [BYTES `0x68f210` names; tables `0x7035d0`(JP) `0x7036e0`(IT) `0x7037f0`(ES) `0x703900`(DE) `0x703a10`(FR) `0x703b20`(default/EN = Preset 1) `0x703c30`(Preset 2); 34 × {u32 code, wchar* label}]

The six per-language tables carry **identical codes**; only the labels differ. Preset 2 is
`FUN_005d51d0`. Preset names are "Preset Type 1 [Arrows/ZXCV]" and "Preset Type 2 [WASD/Mouse]"
(`FUN_00417dc0`).

| # | Slot | Label (EN) | Preset 1 (default) | Preset 2 | Mask bit |
|---|---|---|---|---|---|
| 0 | KEY_ACTION | Attack/OK | Z | . | 0x1 |
| 1 | KEY_JUMP | Jump/Cancel | X | / | 0x2 |
| 2 | KEY_SHOT | Use Magic | C | _ | 0x4 |
| 3 | KEY_USE | Use Boost | V | Space | 0x8 |
| 4 | KEY_MENU | Camp Menu | Space | Tab | 0x10 |
| 5 | KEY_WALK | Walk/Run | L-Shift | L-Shift | 0x20 |
| 6 | KEY_SWORD_REVD | Previous Skill | S | [ | 0x40 |
| 7 | KEY_SWORD_REVU | Next Skill | D | ] | 0x80 |
| 8–10 | KEY_SWORD0..2 | Skill 1–3 | 1 / 2 / 3 | 1 / 2 / 3 | 0x100 / 0x200 / 0x400 |
| 11–18 | KEY_UP, DOWN, LEFT, RIGHT, UPLEFT, UPRIGHT, DOWNLEFT, DOWNRIGHT | Move … | Numpad 8, 2, 4, 6, 7, 9, 1, 3 | W, S, A, D, Q, E, Z, X | direction (§3.4) |
| 19 | MOUSE_DIR | Mouse Movement | L-Button | --- | held 0x1000 / dbl 0x800 |
| 20–27 | PAD_ACTION, JUMP, SHOT, USE, MENU, WALK, SWORD_REVD, SWORD_REVU | (as 0–7) | Button 1, 2, 3, 4, 10, 9, 5, 8 | same | 0x1..0x80 |
| 28–33 | MOUSE_ACTION, JUMP, SHOT, USE, MENU, WALK | (as 0–5) | ---, R-Button, ---, M-Button, ---, --- | L-Button, R-Button, M-Button, ---, ---, --- | 0x1..0x20 |

Mapper groups [BYTES `0x6b7344` = {11, 8, 6}, `0x6b7348` = {0, 20, 28}]:
- keyboard slots 0–10;
- pad slots 20–27 (no Skill 1–3 on the pad);
- mouse slots 28–33.

**Bit n of the mask = slot index within its group.** The three groups are OR-ed together.

### 3.4 Mapper `FUN_005d2ba0` (called from `FUN_00410440`) [EXPORT]

Output per frame: two 0x34-byte blocks at `0x764730 + DAT_00764798·0x34`.

| Offset | Content |
|---|---|
| `+0x00`, `+0x04` | cursor x / y in an **over-scanned** 1024×768 space: x ∈ [−48, 1072), y ∈ [0, 864) at fullscreen (gains 1.09375 / 1.125); the drawn copy is clamped to [−48, 1072] × [−48, 816] (MOUSE.md §1) |
| `+0x08` | wheel |
| `+0x0c`, `+0x10` | stick x / y |
| `+0x14` | **direction bits** |
| `+0x18` | angle |
| `+0x1c` | magnitude 0..1 |
| `+0x20` | **held mask** (`0x764750`) |
| `+0x24` | **pressed mask** (`0x764754`) |
| `+0x28` | repeat mask |
| `+0x2c`, `+0x30` | menu repeat dx / dy |

The player (`FUN_0045f250`) copies these each tick:
- `+0x50c` = pressed (`0x764754`);
- `+0x510` = held (`0x764750`);
- `+0x514` = direction bits;
- `+0x508` = magnitude.

All are zeroed while `0x75ad90 != 0`. `+0x510 |= 0x20` when actor flag `0x40000` is set.

**Button bits** (held = level, pressed = rising edge vs the previous block):

| Bit | Meaning | Source |
|---|---|---|
| `0x1` | Attack / OK-action | slot 0 / 20 / 28 |
| `0x2` | Jump / Cancel-action | slot 1 / 21 / 29 |
| `0x4` | Use Magic (skill) | slot 2 / 22 / 30 |
| `0x8` | Use Boost | slot 3 / 23 / 31 |
| `0x10` | Camp Menu | slot 4 / 24 / 32, **plus Esc (DIK 1) fixed** |
| `0x20` | Walk/Run (held = walk) | slot 5 / 25 / 33 |
| `0x40` / `0x80` | Previous / Next Skill | slot 6 / 26, 7 / 27 |
| `0x100` / `0x200` / `0x400` | Skill 1 / 2 / 3 | keyboard slots 8–10 |
| `0x800` | (pressed) MOUSE_DIR double-click within `DoubleClickFrame` | slot 19 |
| `0x1000` | MOUSE_DIR held (move toward the cursor) | slot 19 (codes < 0x107 only) |
| `0x2000` | **menu OK**: any non-mouse binding of slot 0 (pressed = edge of bit 1) | derived |
| `0x4000` | **menu Cancel**: any binding of slot 1 that is not an AlwaysOkCancel mouse button; plus **R-button** when `MouseButtonAlwaysOkCancel` | derived |
| `0x8000` | (pressed) mouse-bound Attack edge, when AlwaysOkCancel = 0 | derived |
| `0x10000` | **mouse click**: a mouse-bound Attack (AlwaysOkCancel = 0), or the **L-button** (AlwaysOkCancel = 1) | derived |

**Direction bits** (`+0x14`): `1` = down (+y), `2` = left (−x), `4` = right (+x), `8` = up (−y).
- The diagonal slots set 2 bits: UPLEFT = 10, UPRIGHT = 12, DOWNLEFT = 3, DOWNRIGHT = 5.
- The **arrow keys are always on** (DIK `0xc8/0xd0/0xcb/0xcd`).
- Opposite pairs resolve to the most recent press (latch `0x7647f4`).
- Angle table `0x6b734c` [BYTES]: right 0, up +π/2, left π, down −π/2.

**Stick** (only when `GamePad` is on and a joystick exists):
1. Use (lX, lY) if outside the deadzone, else (lRx, lRy); else the first POV hat that is not −1
   (converted to an angle).
2. Magnitude = |v|/1024. The decompile shows a sqrt of squared terms; the exact form is [H].
3. Quantise to 8 directions (`0x6b7384`) or 4 directions (`0x6b7380`) per `0x7035c0`.
4. `PadAnalog` (`0x75aed6`) and hat input force magnitude to 1.0.
5. Digital keys take precedence over the stick.

**Menu auto-repeat**: counter `0x764884` advances while anything is held. Repeat fires at frames 1 and
28, then every 5 frames. With buttons held, repeat = the held mask; otherwise direction → dx/dy ±1.

**Fixed function keys** (dialog 202 group 2030):
- "ESC Key – Menu" (bit 0x10);
- "Cursor Keys – Movement";
- "Mouse Wheel – Switch Skills" (wheel → `+0x08`).

Window-level hotkeys (`FUN_0040e940`, `FUN_0040efb0`), which are not settings:
- **Alt+Enter**: windowed toggle;
- **Shift+F1**: ShowFPS cycle;
- F1: `0x6d6072` toggle in a mode;
- F2–F4: post-clear unlocks `0x6d6073..75`;
- F11 / F12: scene functions.

---

## 4. Configuration dialog (`yso_win.exe -c`) [RES dialogs 200–206, EXPORT `0x410000..0x41f000`]

Languages: 7 DE, 9 EN, 10 ES, 12 FR, 16 IT, 1041 JP. The UI language follows `0x6d5d9c`, via
`SetThreadUILanguage` / `SetThreadLocale` (`FUN_004155e0`).

**Frame** (dialog 200 "Ys Origin – Configuration"):

| ID | Button | Action |
|---|---|---|
| 503 | **Play** | save, then boot the game in-process: `FUN_00415d70` returns `(0x6d5d91 == 0)` and `FUN_0040af50` continues into game init |
| 501 | OK | save, exit |
| 502 | Cancel | exit, no save |
| 500 | Apply | save; enabled when anything is dirty |

The page dialogs are embedded and the current one is remembered in `config.ini CurrentPage`.

- **Page 201 Graphics**: preset buttons, then every control listed in §2.3 / §2.2:
  - 1011 **Normal** (`FUN_00411f10` → `FUN_005d3640`: adapter desktop mode, Mipmap 1, gamma 1.0,
    shadow/water/glare 2, VSync 1, 32-bit / compressed / gamma per caps);
  - 1012 **High-Definition** (`FUN_00411f50`: Normal plus a 1024×768 mode, best MSAA, Trilinear);
  - 1014 **Light** (`FUN_005d3900`: 640×480, 32-bit, Bilinear, AF 1, no MSAA, shadow/water 1, glare 0);
  - 1013 **Lightest** (`0x3f5`: 640×480, 16-bit, LowRes 1, OtherLightWeight 1, Mipmap 0,
    shadow/water/glare 0);
  - all four presets then set GammaEnable = (fullscreen && caps); Light and Lightest (only) also set
    HighResoText = 0 (corrected 2026-10-11, CONFIG_PARITY.md §5);
  - other buttons: 601 DxDiag, 603 EU-localisation credits;
  - static 604: caps warnings (e.g. "Cannot find Hardware Vertex Shader (v1.1)").
- **Page 202 Input Assignments**:
  - a binding grid: 11 action buttons (2106–2116, 2125) and 8 movement buttons (2117–2124), with
    keyboard / pad / mouse name statics 2126–2159. Clicking a button captures the next input [H];
  - checkboxes 2020–2024 (§2.5);
  - preset combo 2001 (Preset 1, Preset 2, then user `.ky` files);
  - 2002 Save (`.ky`); 2003 is enabled only when `FileIndex > 1` [H: delete/cancel the file preset].
- **Page 203 Input Settings**: 3001 Use Game Pad, 3002 Force Feedback, 3006 Vibration Test, deadzone
  trackbar 3004 + 3005 Reset, joystick combo 1531, and a live stick preview (`FUN_0041c150`).
- **Page 204 Sound / Other**:
  - 4001 device, 4010/4012 enables, 4011/4013 volumes, 4020 EAX 2.0, 4021 force software buffer;
  - 4501 "Clear Time Attack Records", 4502 "Clear Game Progress";
  - the checkbox→key wiring is inferred from labels plus the load order (handler not located) [H].
- **Dialog 206 (JP only, debug)**: 5000 "don't initialise joystick" (= `[debug] NoInitializeGameCtrl`),
  5001 anime-test keys, 5002 "K" cubemap dump.

**Validation and constraints** (`FUN_00419b60`, the refresh run after every change):
- adapter, device and mode indices are clamped to the enumerated lists;
- Trilinear is offered only with mip caps (`0x72ff71`);
- the anisotropy list is capped at the caps max (`0x72ff68`);
- WaterEffectLevel is forced 0 without the caps;
- GlareEffectLevel is capped at 1 without PS 1.1 or when PS is disabled, and forced 0 without 32-bit;
- GammaEnable is forced 0 when windowed;
- Language entries are disabled when their archive is missing, and the whole combo is disabled under
  Steam;
- Anisotropy 0 is read back as 1.

**Working bank** `0x72ff00..0x730090` (the same 0x88-byte layout as the game's graphics struct at
`0x72ff00`, followed by the tool-only fields):

| Address | Field |
|---|---|
| `0x72ff8c` | sound device |
| `0x72ff90..93` | sound flags |
| `0x72ff98` / `0x72ff9c` | volumes (0..10000) |
| `0x72ffa0..a6` | input flags |
| `0x72ffa8` | FileIndex |
| `0x72ffac..0x730033` | bindings |
| `0x730034` | deadzone (×10000) |
| `0x730038` / `0x73003c` | RunWalk / MouseSens |
| `0x730040` | DoubleClickFrame |
| `0x730044..54` | combo indices |
| `0x730058` | gamma trackbar |
| `0x73005c` | CurrentPage |
| `0x73006c` | Blood |
| `0x730070` / `0x730074` | frame-rate clamp min / max |
| `0x730078` | ShowFPS |
| `0x730079` / `0x73007a` | MMF / HighResoText |
| `0x730088` | Language |

**Corrections to `build/gameflow_re/GAMEFLOW.md` §1.4–1.6:**
- The game reads **`yso_win.ini`**, not `config.ini`.
- `0x3f3` / `0x3f4` / `0x3f5` / `0x3f6` are the **Normal / High-Definition / Lightest / Light presets**,
  not OK / reset / defaults.
- `0x3ff` is the gamma **Normal** button (slider = 5000), not Cancel.
- The "sim-rate clamp the 60 Hz loop honours" claim for ClampInternalFrameRate is unverified (no
  consumer found).

---

## 5. In-game Options screen (title → Options, and the pause book tab) [EXPORT; layout in `build/menu_re/OPTIONS.md`]

This is the same page in both places: book page 3, sub-ids 0xb (pause) and 0x11 (title), logic in
`FUN_005ba2b0`. It exposes **only 7 settings**:

| Row | Setting | Global | Applies |
|---|---|---|---|
| 0 | BGM Volume (0..1024, step 8) | `0x7035bc` | **live**: `FUN_005ce7f0` |
| 1 | SFX Volume | `0x7035cc` | **live**: `FUN_005ce820` |
| 2 | Screen Brightness (gamma 0.5..2.0) | `0x765164` | **live**: `FUN_005d4de0`. No visible effect unless GammaEnable (so none when windowed). |
| 3 | Use Analog as Digital | `0x6d606d` + mirror `0x75aed6` | live (next mapper frame) |
| 4 | Double-tap to Dash | `0x6d606e` | live (the mover reads it every tick); toggling clears the dash FSM |
| 5 | Hold Attack to Use Skill | `0x6d606f` | live |
| 6 | Always Dash Once Able | `0x6d6070` | live |

Row 7 Default resets 512/512/512 and `0x6d606d = 0x100`. Rows 8 and 9 are Return to Title / Exit.

**Persistence**: the page itself writes nothing. `FUN_0040d7e0` writes:
- `[game]` (all keys above), `[graphics] ShowFPS`, `GammaValue`;
- `[sound] BgmVolume`, `EffectVolume`;
- `[input]` PadAnalog, DashControl, AtkBtnMagic, AlwaysDashOK.

It runs at **clean shutdown** (`FUN_0040dcc0`, gated by `0x7493c9`, which is set after the main loop
returns) and at **game clear** (`FUN_0058f210`). The shutdown path also writes `IniVersion`,
`[kernel] WindowPosX/Y`, `EnglishRoonic` and `[game] Language` (`FUN_0040db00`). A crash loses Options
changes.

This resolves OPTIONS.md §7 "string-value LOAD of BgmVolume / EffectVolume / GammaValue". They are
read at boot by `FUN_0040bf50`:
- BgmVolume / EffectVolume as ints into the sound struct → `0x7035bc` / `0x7035cc`;
- GammaValue via `atof` into gfx+0x2c → `0x765164`.

The section is `[game]` and the file is `yso_win.ini` (both confirmed).

---

## 6. What CleriaCore does today

Survey of the working tree on 2026-10-06. `vg` = `apps/cleria-view/view_game.inl`,
`vm` = `apps/cleria-view/view_menus.inl`, `main` = `apps/cleria-view/main.cpp`.

### INI handling

- **Path**: `CLERIA_INI`, else `cwd/yso_win.ini` (vm:1557-1560). It is **not** the retail Saved Games
  path.
- **Parser**: `IniFile` (vm:1561-1611), a line-preserving read-modify-write.
- **Mismatches with the retail dialect**:
  - ints are read with `atoi`, so the `0x` prefix is unsupported (`IniVersion=0x100` reads as 0);
  - quotes are not stripped;
  - `IniVersion` is never written, so a fresh CleriaCore-created file would be **ignored by retail**
    (§1.3).
- **Keys handled**: only `[sound]` BgmVolume / EffectVolume, `[graphics]` GammaValue, the 4 `[input]`
  flags, and `[game] GameFirstPlay` (read only) (vm:1613-1643).
- **Save timing**: every Options change saves immediately (vg:448-452, vg:3969-3973). This differs
  from retail (exit only) and is harmless.

### Per-setting status

| Setting | Status | Where |
|---|---|---|
| Resolution / window | hard-coded 1280×800 resizable, windowed only | vg:10, vg:33; swapchain follows resize vg:3891 |
| VSync | hard-coded Fifo | vg:51 |
| Refresh / MSAA / AF / filter / mips | hard-coded: no MSAA, Linear, AF 1, 1 mip level | view_gpu_room.inl:718, :726, :180 |
| Gamma / brightness | persisted, **not applied** (no shader term) | vm:1550-1556, vm:1618 |
| StretchAspect | automatic 4:3 pillarbox for 2D, window aspect for 3D | view_book.inl:347; vg:5957 |
| ShowFPS | env `CLERIA_TPS` only | vg:5934 |
| Frame-rate clamp | `kTickHz = 60`, uncapped catch-up | main:101-106 |
| Shadow / water / glare / caustics / MaskOfEyes / Blood levels | not settings: shadows always on (vg:64), no water/caustics/glare pass, Blood missing | — |
| DisableMovies | env `CLERIA_MOVIE=0` | vg:168 |
| HighResoText | (corrected 2026-10-11: not the `H\` folders — the text raster resolution, CONFIG_PARITY.md §15) | — |
| Language | English only (asset tree = `data.ni` + `data_us.ni` overlay, README:118) | bgmdef.cpp:19 |
| BGM / SFX volume | read, persisted and applied, retail 0..1024 scale and the same log taper | vm:1616-1617; vg:449-450; audio.hpp:97-102 |
| PlayBgm / PlayEffect / Reverb / sound device | missing; `CLERIA_MUTE` = no device | vg:99 |
| Keyboard bindings | hard-coded: WASD, Space jump, LMB attack, C skill, X boost, E interact, Esc book | vg:3801-3894, vg:4262-4320 |
| Pressed/held masks | no mask; individual bools annotated with the bits | player_actions.hpp:35-38; skillmp.hpp:67-69 |
| Walk / prev-next skill / Skill 1–3 bits | not mapped | vg:4290 |
| Gamepad / rumble / deadzone / run-walk | **missing**: `SDL_Init(SDL_INIT_VIDEO)` only; keyboard is always a full stick | vg:9; vg:4288 |
| PadAnalog / DashControl / AtkBtnMagic | persisted, **not wired**: the dash FSM runs unconditionally, `auto_charge` is never set | vm:1619-1621; player_actions.cpp:47-63; skillmp.hpp:69 |
| AlwaysDashOK | persisted and wired | vg:4331, player_actions.cpp:67 |
| GameFirstPlay | read only | vm:1640, vg:171 |
| SaveFileNo / Title* / PlayerName / GameSD | hard-coded or session-only; preserved only by the read-modify-write | vg:374, vg:86, titlescreen.hpp:43 |
| Save dir | `CLERIA_SAVE_DIR`, else `./saves` (not Saved Games) | vg:137-144 |
| Retail install / music / movies / font | `asset_root/../release` plus `CLERIA_MUSIC_DIR` / `CLERIA_MOVIE_DIR` / `CLERIA_FONT_FILE`; no Steam lookup | bgmdef.cpp:14, movie.cpp:52, vm:1398 |

---

## 7. Recommendations for the shell's data model

1. **Keep retail keys in retail form, in a retail-compatible `yso_win.ini`.**
   - Use the same section and key names, ints in decimal, `IniVersion=0x100` (hex) at the root,
     floats as `%0.8f`, strings `"quoted"`, binding names in table spelling with spaces stripped, CRLF,
     and ANSI/CP932.
   - The parser should accept `0x` / `0b` ints, strip quotes, match key names case-insensitively, and
     compare binding names with whitespace stripped.
   - Keep the line-preserving read-modify-write so retail-only keys (DeviceIdentifier, Device/Adapter,
     ForceSoftwareVP, …) round-trip untouched.
   - Default location: resolve exactly like retail (`common.ini` UserProfileHere/BaseDir, then
     FOLDERID_SavedGames`\FALCOM\yso_win\`), with `CLERIA_INI` as an override. Retail and CleriaCore
     then share one file.
2. **Put CleriaCore-only and QoL keys in a separate file** (e.g. `cleria.ini` next to it, sections
   `[cleria.video]`, `[cleria.input]`, `[cleria.qol]`, `[cleria.mods]`). Retail's writer probably keeps
   unknown sections (§1.3 [H]), but the config tool's IniVersion-reset path rewrites everything, so a
   separate file is the safe choice. Candidates:
   - true fullscreen-borderless;
   - an arbitrary internal resolution or scale;
   - an FPS cap;
   - SDL gamepad mappings by name (A/B/X/Y, triggers, sticks) and multiple binds per action;
   - remappable "fixed" keys;
   - a gamma that also works windowed;
   - an unlocked movie skip;
   - a language override that ignores Steam.
3. **Bindings model**: store `code` values using the retail code space:
   - DIK = PC set-1 scancodes, which map 1:1 to `SDL_Scancode` through a fixed table;
   - `0x100..0x102` mouse, `0x107+` pad button index, −1 unbound.

   Export to retail `Assign{}` only what retail can express. The pad: retail is DInput button numbers.
   For an XInput pad under DInput, Button 1–4 = A/B/X/Y, 5/6 = LB/RB, 7/8 = Back/Start, 9/10 = stick
   clicks [H, standard XInput→DInput order]. Note that retail's default PAD_MENU = Button 10 and
   PAD_WALK = Button 9 then mean stick clicks; worth a CleriaCore remap. Build the per-frame
   held/pressed masks **exactly** per §3.4 (bits, OK/Cancel derivation, fixed Esc/arrows, repeat
   1/28/+5) so menus and the mover can consume `+0x50c` / `+0x510` unchanged.
4. **Apply classes** (for the shell UI's "applies when" hint):

   | Class | Settings |
   |---|---|
   | **Live, per frame** | volumes, PlayBgm/PlayEffect, gamma (shader uniform), ShowFPS, all input (bindings, deadzone, RunWalk, DoubleClick, MouseOkCancel, PadAnalog, DashControl, AtkBtnMagic, AlwaysDashOK, GamePad, rumble), Shadow/Water/Glare/Caustics/MaskOfEyes/OtherLightWeight/Blood (render-path switches), StretchAspect (viewport), DisableMovies, the frame-rate cap |
   | **Sampler recreate** (cheap, live) | TextureFilter, Anisotropy |
   | **Swapchain reconfigure** | VSync (present mode), windowed/fullscreen, resolution, refresh rate, triple buffering |
   | **Attachments + pipelines rebuild** | MSAA sample count |
   | **Texture / font reload** | Mipmap, CompressedTexture, LowResoTexture, HighResoText |
   | **Return to title / reload data** | Language (retail needs a restart) |
   | **Round-trip only** (D3D9/DSound/Win32 specific; keep the values, no CleriaCore meaning) | Device, Adapter (could map to display index), DeviceIdentifier, ForceSoftwareVP, DisablePixelShader, BackBufferFormat32, DisableQPC, AccessArchiveViaMemoryMappedFile, sound Device, Reverb/EAX, ForceSoftwareSoundBuffer, ClampInternalFrameRateMin/Max (consumer unknown), EnglishRoonic (until its text path is ported), MouseSensitivity (unused by retail) |

5. **Persistence timing**: keep CleriaCore's immediate save for shell edits. Also write the `[game]`
   bookkeeping keys (SaveFileNo, Title*, GameFirstPlay-if-set, GameSD=1, WindowPos) when retail does,
   so retail sees consistent state.

---

## 8. Open questions

1. **ClampInternalFrameRateMin/Max**: closed 2026-10-11 — no consumer exists (CONFIG_PARITY.md §16).
2. **MouseSensitivity**: struct+0x98 is never copied (verified bytes). Confirm that no other reader
   exists.
3. **Sound page (204) and gamma "Active" (1021)** WM_COMMAND handlers were not located. Their key
   wiring is inferred from the labels.
4. Whether retail's INI writer preserves **unknown sections/keys** (generic tree, so likely yes). A
   5-minute test with a scratch copy would settle it.
5. **DeviceIdentifier** semantics in `FUN_005d43d0` (adapter re-identification?), and what
   `0x764b98 < 11` gates (glare/HD preset; RAM or CPU count?).
6. Exact stick-magnitude formula in `FUN_005d2ba0` (the decompile is garbled), and the joystick
   DIPROP_RANGE (assumed ±1024).
7. **FileIndex** semantics inside the config tool (preset index vs .ky slot), and button 2003's role.
8. `GamePadID` default (`FUN_0040bf50` takes it from EDX of the caller).
