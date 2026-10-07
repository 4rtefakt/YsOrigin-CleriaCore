# The Video tab

The shell's **Video** page (F1 > Video, and Settings in the launcher): retail's graphics settings that
CleriaCore's renderer can honour, CleriaCore's own extras, and an honest list of the retail switches it
cannot honour yet. Code: `src/cleria/video.{hpp,cpp}` (the pure part: `[cleria.video]`, retail's gamma /
sampler / aspect / FPS-text rules, the render-scale, frame-limiter, frame-statistics and display-mode
helpers), `apps/cleria-view/view_video.inl` (the page, the GPU passes, the overlay, the window, the drive
commands), and one-line hooks in `view_game.inl`, `view_gpu_room.inl` (the world sampler) and
`view_shell.inl` (Shift+F1). Tests: `cleria-videotest`, `cleria-settingstest`, `tools/drive_regress.py`
cases `video_passes` / `video_fpscap`.

Tags: **[BYTES]** read from `yso_win.exe` with `tools/exe_peek.py`, **[EXPORT]** the Ghidra export
(`D:\ghidra-work\export`), **[DOC]** an existing RE doc, **[H]** hand-chosen, **[not modelled]**.

Rule: retail's look is the default. **At the defaults every pass this page adds is skipped**, so a default
frame is the frame without it, pixel for pixel (§6).

## 1. Where each setting lives

| Row | Key (file) | Applies |
|---|---|---|
| Window mode | `[cleria.video] WindowMode` 0 windowed / 1 borderless / 2 fullscreen (cleria.ini); mirrors retail `[graphics] Windowed` | live |
| Resolution | `[graphics] BackBufferWidth` / `BackBufferHeight` (yso_win.ini) | live (windowed: the window size; fullscreen: the display mode) and at start |
| Refresh rate | `[graphics] RefreshRate` (0 = default) | live, exclusive fullscreen only |
| Display | `[graphics] Adapter` (an index into SDL's display list; only shown with 2+ displays) | live |
| VSync | `[cleria.video] VSync` (CleriaCore's own since before this page; retail `WaitVSync` round-trips) | live |
| Frame rate limit | `[cleria.video] FpsCap` (0 = off) | live |
| Brightness | `[graphics] GammaValue` (also the in-game Options' Screen Brightness) | live |
| Render scale | `[cleria.video] RenderScale` 0.5 .. 2.0 in 0.25 steps | live |
| Texture filtering | `[graphics] TextureFilter` 0 point / 1 bilinear / 2 trilinear | on restart |
| Anisotropic filtering | `[graphics] Anisotropy` 1 / 2 / 4 / 8 / 16 | on restart |
| Stretch widescreen | `[graphics] StretchAspect` | live |
| Disable movies | `[graphics] DisableMovies` | live |
| FPS display | `[graphics] ShowFPS` 0 / 1 / 2 (Shift+F1 cycles) | live |
| Frame-time graph | `[cleria.video] PerfGraph` | live |

Retail keys stay in `yso_win.ini` in retail's spelling, so the original game sees the same choices;
CleriaCore's extras are in `cleria.ini [cleria.video]` only (checked by `cleria-settingstest`).

## 2. Retail's settings CleriaCore honours

### 2.1 Brightness — `GammaValue`, `FUN_005d4de0` [BYTES 0x5d4de0..0x5d4ef5]

Retail builds a hardware gamma ramp (`IDirect3DDevice9::SetGammaRamp`, vtable +0x54):

- `g = GammaValue`, clamped: `g < 0.25` (comiss with `0x6bd244` = 0.25) -> 0.25, else `min(g, 5.0)` (`0x6bd868`);
- `ramp[i] = (u16)(pow(i * (1/255), 1/g) * 65535)` for i = 0..255, the same for R, G and B
  (`0x6bd038` = 1/255, `0x6bdf74` = 65535; `FUN_0041b790` = CRT `pow` in double);
- the ramp is set only when `GammaEnable` (`0x765170`) and a device exist; the tool forces GammaEnable 0
  when windowed (CONFIG.md §4), so **retail's brightness does nothing in a window**.

CleriaCore applies the same curve, `out = in^(1/g)`, as a shader pass over the finished frame (after the
cursor, before the shell): `video_post`. It copies the frame (the surface needs CopySrc; when the desktop
surface lacks it the pass is skipped) and redraws it through `fs_gamma`. **Deviation: CleriaCore applies it
in every window mode** (the description says so). [H] The shader computes `pow` in float and the 8-bit
target rounds; the 16-bit ramp's own truncation is not re-modelled. Measured: a GammaValue 1.6 / 0.6 frame
equals `round(255 * (in/255)^(1/g))` of the default frame within 1 level on every pixel.

### 2.2 Texture filtering — `TextureFilter` / `Anisotropy`, `FUN_005d4f00` + table `0x6b754c` [BYTES]

| TextureFilter | MINFILTER | MAGFILTER | MIPFILTER |
|---|---|---|---|
| 0 Point | POINT | POINT | POINT |
| 1 Bilinear (default) | LINEAR | LINEAR | POINT |
| 2 Trilinear | LINEAR | LINEAR | LINEAR |

`Anisotropy > 1` sets `MAXANISOTROPY` and turns a LINEAR min filter into ANISOTROPIC (given the caps bit
0x400); a POINT filter gets no anisotropy. CleriaCore: `video::sampler_spec` -> the world sampler `g.sampler`
(`build_pipeline`), which every room / sprite / effect bind group bakes in. wgpu needs every filter linear
for anisotropy > 1, so the mip filter goes linear then too; CleriaCore uploads one mip level, so that and
Trilinear look like bilinear (§4). The bind groups bake the sampler at load, so a change **applies on
restart** (the page says so and shows a "Restart" note while the running sampler differs). The 2D layers
(HUD, book, windows) keep their own linear samplers [not modelled: which of retail's 2D draws go through
`FUN_005d4f00`].

### 2.3 Stretch widescreen — `StretchAspect`, `FUN_005e2670` / `FUN_005d3ab0` [EXPORT]

`FUN_005e2670` (renderer init) sets the projection aspect `DAT_007b38f8 = 4/3`, then, unless
`StretchAspect` (`0x765150`), `BackBufferWidth / BackBufferHeight`. With the camera's fixed vertical FOV
(CAMERA.md §5) the default is Hor+; StretchAspect renders the 4:3 view stretched to the window.
CleriaCore: `video_aspect()` at the play loop's `view_proj`. `FUN_005d3ab0` also sets the 2D aspect
factors `0x765178` / `0x765180` to 1.0 under StretchAspect (the menus and HUD stretch too, 47 users) —
**[not modelled]**: CleriaCore's HUD / book keep their 4:3 layout, and the description says so.

### 2.4 Disable movies — `DisableMovies`

`FUN_005773e0` step 2 (MOVIES.md §1.1): `0x765173` set -> `FUN_005758f0` (finish) at once. CleriaCore:
`play_demo` treats the movie as absent (the existing open-failure path: finished at once, the fade, the
scene's BGM). Covers the logo, prologue, opening, interludes and ending. Verified: `S_0000` with
`CLERIA_MOVIE=1` logs `demo1: en_pro.avi disabled -> finished at once`.

### 2.5 FPS display — `ShowFPS`, `FUN_00429530` / `FUN_0040e940` [EXPORT]

- 1: `"FPS: %d"` (the FPS counter `0x767c34`); 2: `"FPS: %d/%d %s"` (FPS, TPS `0x767c3c`, and
  `FUN_005d4820`'s back-buffer + depth format names such as `"32(X888)(D24X8)"`); both counters refresh
  every 1000 ms.
- Shift+F1 cycles 0 -> 1 -> 2 -> 0 (`FUN_0040e940`, `GetAsyncKeyState(VK_SHIFT)` on F1).

CleriaCore: the `video` Shell::hud layer draws the same text; the counters (`video::FrameStats`) count
presented frames and logic ticks per 1000 ms. [H] position (top-left), font (the shell's) and the format
suffix `32(8888)(D24)` (Depth24Plus has no D3D name). [not modelled] `ShowFPS == 1` also changes a HUD
readout in `FUN_0043fe00` (a timer from `0x76bc38`); the HUD is not this page's.

### 2.6 Resolution, refresh rate, display, fullscreen

Retail: `BackBufferWidth/Height` are the window's client size (windowed) or the display mode
(fullscreen), `RefreshRate` the fullscreen refresh (0 = default), `Adapter` the D3D adapter.
CleriaCore (`video_apply_window`):

- **Windowed**: the window is resized to BackBufferWidth x Height, [H] clamped to the display's usable
  area and at least 320 x 240, centred on the chosen display. The window stays resizable; a drag does not
  change the setting.
- **Borderless fullscreen** (not a retail option): SDL's desktop fullscreen; the resolution row is
  disabled.
- **Fullscreen**: SDL exclusive fullscreen at `SDL_GetClosestFullscreenDisplayMode(BackBufferWidth,
  BackBufferHeight, RefreshRate)`.
- At start (`video_boot_window`): the saved mode and, for a file-backed session, the saved size. A
  memory-defaults session (no settings file) keeps the 1280 x 800 window; a CLERIA_DRIVE run's hidden
  offscreen window is never touched.

## 3. Window mode and the retail `Windowed` key

Retail's default is `Windowed=0` (bss) = fullscreen. CleriaCore's window used to come from
`[cleria.video] BorderlessFullscreen` alone (windowed unless set), and a CleriaCore-created ini carries
`Windowed=0` from the defaults. Reading `Windowed` now would flip such users into exclusive fullscreen.
So the source of truth is `[cleria.video] WindowMode`; when absent it is migrated from
`BorderlessFullscreen` (1 -> borderless, else windowed). A change on the page writes `WindowMode`,
`BorderlessFullscreen` (= borderless) and retail's `Windowed` (1 only for windowed: borderless has no
retail twin, fullscreen is its closest).

## 4. Retail switches CleriaCore cannot honour yet

Shown greyed under "Not available yet": the value is what CleriaCore does, the description names the
original option and the value saved for the original game.

| Retail key | Why not |
|---|---|
| `MultiSampleType` (MSAA) | Every world pipeline (room, characters, shadows, materials, effects) is built with 1 sample and each draw function opens its own pass on the frame; MSAA needs multisampled twins of all of them plus resolves. Render scale > 100% (supersampling) is the anti-aliasing offered instead. |
| `Mipmap` | `upload_texture` / `upload_rgba` upload level 0 only. Mipmapping would also make Trilinear real. |
| `ShadowEffectLevel` | The level's users (`FUN_00457a90`, `FUN_00459410`, `FUN_0045c490`, `FUN_00474870`, `FUN_00474cd0`, `FUN_005042e0`, `FUN_005043e0`) are not traced to what they drop. |
| `WaterEffectLevel`, `GlareEffectLevel`, `WaterCaustics` | No reflection, glare or caustics pass exists yet. (`MaskOfEyes` is a live toggle since 2026-10-07: the grey pass is `view_mask.inl`, build/audit/PLAYTEST_1_STORY.md §1.) |
| `[game] BloodyEffectLevel` | Its users (`FUN_00478510`, `FUN_004faf20`, `FUN_005130c0`) are not traced. |
| `OtherLightWeightMode` | Users (`FUN_0044fdd0`, `FUN_00501a30`, `FUN_0055ddc0`) not traced. |
| `CompressedTexture`, `LowResoTexture` | Loader variants, not ported (CleriaCore loads the full DDS). |
| `HighResoText` | CleriaCore always uses the high-resolution (`H\`) text art. |
| `TripleBuffer`, `BackBufferFormat32`, `ForceSoftwareVP`, `DisablePixelShader`, `DisableQPC`, `Device`, `DeviceIdentifier` | Direct3D 9 / timer specifics with no meaning for wgpu; they round-trip in yso_win.ini. (`TripleBuffer` = a second D3D back buffer under FIFO; wgpu's FIFO swap chain already queues frames.) |

## 5. CleriaCore's extras ("Not a retail option.")

- **Render scale** 50 .. 200%: the 3D world (room, characters, pickups, damage popups, enemies, flames,
  effects, particles, emotion bubbles) draws into its own colour + depth target at `RenderScale x` the
  frame (`video_world_begin`, sides rounded, clamped to the device's max texture side), then one bilinear
  fullscreen pass puts it on the frame (`video_world_end`); the HUD, windows, book, cursor and shell draw
  over it at native resolution. 200% = 2x2 supersampling; 50% = a quarter of the pixels.
- **Frame rate limit**: after each present, `video::FrameLimiter` waits (`SDL_DelayPrecise`) until the
  next 1/cap deadline; a frame more than one period late re-bases the schedule instead of bursting.
  Presentation only: the logic's 60 Hz accumulator is untouched, so a 30 fps cap runs 2 ticks a frame.
  Measured: cap 30 -> 30-31 fps / 60 tps, cap 144 -> 144-145 fps / 60 tps, off -> ~1050 fps / 60 tps.
- **Frame-time graph**: the last 120 frame times as bars, with 60 / 30 fps guides, average and max.
- **Borderless fullscreen** (pre-existing), **Brightness in a window** (§2.1).
- Not added: a field-of-view option (the FOV is per camera region from the SCM data, CAMERA.md §5; scaling
  it changes what the player can see of a room, so it stays retail) — Hor+ already shows more on ultrawide
  and Stretch widescreen gives retail's 4:3 framing.

## 6. Defaults are pixel-identical

At the defaults: `RenderScale` 1 -> `video_world_begin` returns before touching anything; `GammaValue` 1
-> `video_post` returns; `StretchAspect` 0 -> `projection_aspect` = `float(w)/float(h)` as before;
`TextureFilter` 1 / `Anisotropy` 1 -> the sampler descriptor CleriaCore always built; FpsCap 0 -> no
wait. Evidence: `tools/goldens.py compare` all 7 match; a live CLERIA_DRIVE frame (S_1000, fixed camera,
lockstep ticks 300 and 400) from this build is byte-identical to the same frames from a build of the parent commit.

## 7. Testing

- `cleria-videotest`: the gamma ramp vs retail's formula, the sampler table, the aspect, the FPS text,
  `[cleria.video]` (defaults, the BorderlessFullscreen migration, clamps, round trip), the render-scale
  extent, the frame limiter, the frame statistics, the display-mode lists.
- `cleria-settingstest`: the Video keys persist through the store, retail keys in yso_win.ini only, the
  extras in cleria.ini only.
- Drive commands (view_video.inl): `vopt=Key=value` (RenderScale, FpsCap, PerfGraph, GammaValue, ShowFPS,
  StretchAspect, DisableMovies) and `vstat` (`[video] world WxH scale .. gamma .. post on|off aspect ..
  fps .. tps .. cap .. held|over|uncapped`). `CLERIA_VIDEO_SCROLL=<px>` scrolls the page for headless
  captures of its lower rows.
- `drive_regress.py` `video_passes` (the passes switch on and off) and `video_fpscap` (cap 30 holds).
- Not testable headlessly (no visible window allowed): the window mode / resolution / display switches
  themselves. Their pure parts (mode lists, nearest mode, fitting, the Windowed mirror) are unit-tested.
