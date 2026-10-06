# Retail mouse: the cursor sprite, click-to-move, menus

Scope: everything the mouse does in retail Ys Origin (Steam `yso_win.exe`, 2024-01-08 "smallfix"),
for a faithful CleriaCore port. Read-only RE, written 2026-10-06. Builds on `CONFIG.md` §3 (input
system), corrects it where noted (§9).

Evidence tags: **[BYTES addr]** disassembly or constants read with `tools/exe_peek.py`,
**[EXPORT func]** Ghidra export `D:\ghidra-work\export` (decompile / globals.tsv / strings.tsv),
**[DATA file]** game files or reference grabs, **[H]** inference. Globals are absolute VAs.
`FUN_0066fd70` = cos, `FUN_00670d30` = sin, `FUN_0066f80a` = x87 `atan2(ST1, ST0)` (the code
pushes y then x), `FUN_004217f0` / `FUN_00671000` = floor.

Scratch scripts (template matching, DDS dumps): `%TEMP%\claude\...\scratchpad\agent_mouse\`.

---

## 0. Summary

- **The cursor is the Windows cursor position, not DirectInput deltas.** The mapper calls
  `GetCursorPos` every frame and maps it into a slightly over-scanned 1024×768 space (x gain
  1.09375 around the centre, y gain 1.125 from the top). `lX/lY/lZ` from DirectInput are never read;
  the wheel comes from `WM_MOUSEWHEEL`. (§1)
- **The OS cursor is hidden over the client area**: `WM_SETCURSOR` → `SetCursor(NULL)` and
  `IDirect3DDevice9::ShowCursor(FALSE)`. The game draws its own sprite, `data\image\cursor.dds`,
  a 36×36-texel crop of a 64×64 A1R5G5B5 texture. It is drawn **transposed** (u runs down the
  screen), as a 23×23-unit quad with the pointer position 15 units in from the top-left corner. It
  is made square on widescreen. It is white-modulated, alpha-blended, with a 0.1/frame fade-in and
  0.08/frame fade-out. (§2)
- **Two looks.** In a menu, or with no player, the sprite is upright: an arrowhead pointing up-left.
  In the field it is **rotated so the arrowhead points from the player toward the cursor**. (§2.4)
- **Visibility** is a small state machine in `FUN_004263c0` (§3):
  - menus show the cursor as soon as the mouse moves;
  - the field shows it only once the mouse moves while the move button (MOUSE_DIR, L-button in
    Preset 1) is held;
  - it fades out about 62 logic ticks after keyboard or pad input, events hide it, and the
    attract-demo window hides it.
- **Click-to-move** (Preset 1: hold L):
  - The cursor manager overwrites the frame's input record before the player ticks: angle =
    `atan2` from the player's projected screen position to the cursor, magnitude = 1.0 (always run
    unless Walk is held), and 8-way direction bits computed from the **screen centre (512, 360)**.
  - Movement continues for 2 ticks after release and stops within 0.5 px of the player.
  - A **double-click** arms a 12-tick window (`+0x70c`). An attack in that window plays the lunge
    section `0x112` toward the cursor and suppresses jump. (§4)
- **Menus** (§5):
  - Hover-select runs on cursor motion. The hit point is (x−16, y−16), the arrow tip.
  - Click (bit 0x10000) confirms only when the pointer is inside the selected item.
  - The right button is Cancel (0x4000) when `MouseButtonAlwaysOkCancel` = 1.
  - The Options sliders can be dragged.
  - Several windows warp the OS cursor (`FUN_005d33f0`) onto the selected item: the bottom-right
    corner, so the tip lands inside.
  - The wheel steps the Save/Load rows, the teleport map, the char-select bars and the shop
    quantity. In the field it switches skills; this needs |delta| ≥ 120. (§4.3)
- **CleriaCore today** passes the mouse buttons to the mapper but has no cursor position, no
  sprite, no click-to-move and no hover. It also confirms windows on any click, where retail
  requires a hover. (§7)

---

## 1. Cursor position

### 1.1 The mapper's cursor (input record `+0x00` / `+0x04`) [BYTES 0x5d2be8–0x5d2c55, EXPORT `FUN_005d2ba0`]

Every mapper call:
1. `GetCursorPos(&rec.x)`, in screen pixels.
2. The x and y are mapped with these globals:
   - `W`, `H` = `0x7035c4` / `0x7035c8`. These are copies of `[graphics] BackBufferWidth/Height`
     (graphics struct `+0x10/+0x14`, `FUN_005d68d0`); 1024×768 is the .data default.
   - `cx` = `0x764b9c` = `PP.BackBufferWidth >> 1`.
   - `ox, oy` = `0x764ba4/0x764ba8` = 0.
   - Windowed only (`0x764882` = PP.Windowed): `FUN_005d62b0` runs `ClientToScreen` on both
     `(ox, oy)` and `(cx, ·)`, so `ox` = client origin and `cx` = origin + W/2. It runs on WM_MOVE,
     device reset and init. [EXPORT `FUN_005d62b0`, `FUN_0040efb0` case 3]

All arithmetic is int32, and C division truncates toward zero:
```
x = ( ((sx - ox - cx) * 560) / 512 + cx ) * 1024 / W      // 0x230, sar 9 with the sign fix
y = ( ((sy - oy) * 432) / 384 ) * 768 / H                 // 0x1b0, *0x2aaaaaab >> 38
```

With fullscreen and the client the same size as the back buffer (`ox` = `oy` = 0, `cx` = W/2):
- **x = 512 + (sx − W/2)·1.09375·1024/W**, so a screen-edge cursor maps to **−48 .. 1072**.
- **y = sy·1.125·768/H**, which maps to **0 .. 864**.

The gains are fixed. `MouseSensitivity` is not used (CONFIG.md §2.5).

- **Windowed quirk [BYTES]:** because `cx` already contains the origin, x is offset by
  `−0.09375·origin_x` back-buffer px. A window at x = 64 reads 6 px left. y has no such term.
- `rec.wheel (+0x08)` = `0x76479c`, the **raw** `WM_MOUSEWHEEL` delta: `(short)HIWORD(wParam)`, ±120
  per notch, last message wins, not accumulated. The mapper zeroes `0x76479c` after copying it.
  [BYTES 0x5d2c58, 0x5d2c86; EXPORT `FUN_0040efb0` msg 0x20a]
- Buttons: DIMOUSESTATE L/R/M (`+0x10c..+0x10e`), swapped by `SM_SWAPBUTTON` (CONFIG.md §3.1).
  `lX`/`lY` (`0x764988/0x76498c`, `0x764ae8/0x764aec`) are only **written**, zeroed by the warp;
  nothing reads them. [EXPORT globals.tsv]

### 1.2 The cursor manager's copy [EXPORT `FUN_00426e30`, `FUN_00426c70`; BYTES 0x73263c..]

`FUN_004263c0` (§3) converts the record's ints to floats `0x73261c/0x732620` and calls
`FUN_00426e30`:
```
s_x = BackBufferWidth / clientRect.right      // 0x732614, FUN_00426c70 (startup FUN_0040d1d0, reset FUN_00410cd0)
s_y = BackBufferHeight / clientRect.bottom    // 0x732618
fx  = clamp(fx, -48/s_x, (−48+1120)/s_x)      // 0x73263c..48 = {-48, -48, 1120, 864} in .data
fy  = clamp(fy, -48/s_y, (−48+864)/s_y)
X = fx * s_x;  Y = fy * s_y                   // 0x732624 / 0x732628: the drawn cursor, 1024x768 units
Xi = floor(X);  Yi = floor(Y)                 // 0x73262c / 0x732630: the draw anchor
```

- With client = back buffer, `s` = 1. The **drawn** cursor is clamped to x ∈ [−48, 1072],
  y ∈ [−48, 816], so the bottom 48 units of the mapped range pin at 816.
- **Menus read the unclamped record ints** (§5), not this copy.
- [H] The −48 margin lets the 23-unit sprite leave the screen at the left edge, since its tip is
  at the anchor −15.

### 1.3 Warping the cursor: `FUN_005d33f0(ecx = X, edx = Y, [esp+4] = mode)` [EXPORT, BYTES]

The function:
- writes `(X, Y)` into **both** input records (`0x764730`, `0x764764`);
- zeroes DInput `lX/lY` in both state blocks;
- then calls
  ```
  px = (mode == 0 || A == 1.0) ? (W*X)/1024 : FUN_005d3be0(axis 0, mode, X, 0)   // pillarbox remap
  SetCursorPos( ((px - cx) * 512) / 560 + ox + cx,  ((H*Y)/768 * 384) / 432 + oy )
  ```

This is the exact inverse of §1.1. `A` = `0x765178` = W/(4H/3). Callers also write
`0x73261c/0x732620` so the drawn sprite jumps the same tick. The callers are listed in §5 (the
"Warps" table).

---

## 2. The cursor sprite

### 2.1 Texture [DATA `assets/IMAGE/CURSOR.DDS`; BYTES 0x6d65e0, 0x427206]

- `data\image\cursor.dds` is entry 3 of the static image table: path objects at `0x6d6398 + 0xc4·k`,
  handles at `0x749d4c + 4·k`, loaded by `FUN_00457410`. So handle = **`0x749d58`**. The draw
  reads it at 0x427206.
- The file is 64×64, no mips, 16-bit **A1R5G5B5** (masks 7c00/03e0/001f/8000). Alpha is binary.
- Content: a gold arrowhead with a blue gem. The tip is at texel (0,0), pointing up-left, and three
  feather strokes run toward the bottom-right. The opaque bbox is (0,0)–(39,39).
- **Only u, v ∈ [0, 0.5625]** (36×36 texels) are sampled. This clips the feather tips at 36–38.
- `CURSOR2.DDS` (a gold arrowhead with a white "X") and the `_C_` files are **not referenced by
  the exe**: the string search for `cursor2` is empty, in ASCII and UTF-16. `MENUCURSOR.DDS` is the
  menu bar, already ported.

### 2.2 Geometry [BYTES 0x426f20–0x427297]

- One 4-vertex triangle strip, stride 0x1c: x, y, z = 0, rhw = 1, colour, u, v.
- `FVF` via `FUN_005e4100`; drawn with `DrawPrimitive(TRIANGLESTRIP=5, start, 2)` through the
  dynamic VB `FUN_005eda00`.
- Anchor `(Xi, Yi)` from §1.2, in 1024×768 units. Offsets come from 0x6bd964 = 15.0 and
  0x6bd8e8 = 8.0.

**Upright** (menu, or no player):

| v | position | (u, v) |
|---|---|---|
| 0 | (Xi−15, Yi−15) | (0, 0) |
| 1 | (Xi−15, Yi+8) | (0.5625, 0) |
| 2 | (Xi+8, Yi−15) | (0, 0.5625) |
| 3 | (Xi+8, Yi+8) | (0.5625, 0.5625) |

- The UV is **transposed**: u increases down the screen, v increases to the right. The image is
  mirrored about its diagonal, and the tip stays at the top-left.
- The pointer position is **15 units right/down of the tip**, inside the gem. The quad is 23×23
  units.
- The menus hit-test `(x − 16, y − 16)`, which is effectively the tip (§5). The field steers from
  the anchor itself (§4.1).

Then:
1. `FUN_005e4100` maps every vertex 1024×768 → back buffer (PAUSEBOOK_DRAW §0.4 step 1:
   `floor(v·s) − 0.5`, s = W/1024 or H/768). This is the full-width stretch, not the pillarbox.
2. **If `A ≠ 1.0`** (double compare 0x765178 vs 0x6bd540) [BYTES 0x427187–0x4271ed], the quad is
   made square around its centre:
   ```
   w = v2.x - v0.x;  h = v1.y - v0.y;  d = (w - h) * 0.5
   v0.x += d;  v1.x += d;  v2.x = v0.x + h;  v3.x = v1.x + h
   ```
   This is applied to the **rotated** quad too, using these same vertex slots. The result is a
   slightly sheared arrow on widescreen; it is a retail quirk.
3. Render state `0x41` via `FUN_005eb5b0`: alpha blend SRCALPHA/INVSRCALPHA, no alpha test
   (hud_window.md C7). `SetTexture(0, 0x749d58)` is skipped if it is already bound (`0x764c64`).
   Sampling uses the global filter (`[graphics] TextureFilter`).

**Verified on the reference grabs [DATA]:**
- The cursor is visible in `typeselect_adol.png`, `charselect*.png`, `options.png`, `bonusshop.png`,
  `title_bonus.png`, `pause_topic*.png`, `pause_inventory.png` (2559×1439), and
  `realgame/statue_choice.png` (a field Window_Menu, 1914×1074).
- A masked template match of the **transposed** 36-texel crop fits best at **43×43 px** at 1440p
  (23·1440/768 = 43.1, squared), upright. Squared error: 19.9 transposed vs 8433 plain on
  `typeselect_adol.png`. This confirms the transpose, the size and the squaring.
- No field gameplay grab shows the cursor.

### 2.3 Colour and fade [BYTES 0x4265b5–0x426673]

Vertex colour = `0x7325f0`, the same on all four vertices; nothing is drawn when it is 0.
```
visible_now = 0x7325dc && (menuMode || !eventMode)          // §3
if visible_now: f = 0x7325f8; if f < 1: f = min(f + 0.1, 1) ...    // 0x6bd19c = 0.1
else:           if f > 0:  f = max(f - 0.08, 0) ...                // 0x6bd180 = 0.08
colour = visible_now && f >= 1 ? 0xFFFFFFFF
       : (f changed this tick)  ? 0x00FFFFFF | (bits(f + 1.0) << 9 & 0xFF000000) | (bits >> 30 ? 0xFF000000 : 0)
       : visible_now ? 0xFFFFFFFF : 0
```

- The float trick puts the top 8 mantissa bits of `1 + f` into the alpha byte. So **A = floor(f·256)**
  for f < 1, and 255 at f = 1. RGB is always white.
- Fading out from f = 0.08 lands at f = 0 with A = 0. That is drawn (colour 0x00FFFFFF ≠ 0) for
  one tick, then the colour becomes 0.
- The fade-in from 0 takes 10 ticks; the fade-out from 1 takes 13 ticks.
- A second "arrow" channel (`0x7325e0/0x7325f4/0x7325ec`, set while MOUSE_DIR is held in the
  field, held 37 ticks after release) has the same fade code. **Its colour is only used to decide
  whether the draw is queued.** The draw reads `0x7325f0` only, so the channel is vestigial.
  [BYTES 0x426b5b, 0x426f31]

### 2.4 The field look: rotated toward the cursor [BYTES 0x427071–0x427115, 0x4267a2–0x426818]

When `menuMode == 0` (struct `+0x1c`) and a player exists (`0x74c09c ≠ 0`):
```
a  = 0x732600 − π/4                     // 0x6bd3a0
c = cos(a), s = sin(a)
for corner (fx, fy) in {(-15,-15), (-15,8), (8,-15), (8,8)}   // table 0x6981e4, v0..v3, same UVs as upright
    X = fx*s - fy*c + Xi
    Y = fx*c + fy*s + Yi
```

- This is a rotation by θ = 3π/4 − `0x732600` in y-down screen space. The tip, which starts at the
  y-down angle −3π/4, ends at −`0x732600`. So **the arrowhead points along `0x732600` measured
  y-up** (right = 0, up = +π/2).
- At `0x732600` = 3π/4 the field sprite equals the upright one.
- The anchor stays the cursor position. The sprite turns about the gem, not the tip.

`0x732600` is updated each logic tick while the cursor alpha ≠ 0 and a player exists:
```
dx = (X − Px) * 1024 / 512            // 0x6bde70 / 0x6bd008
dy = (Y − Py) * 1024 / 376.32         // 0x6bdc7c; 1024/376.32 = 2.7210884
if dx² + dy² > 0:  0x732600 = atan2(−dy, dx)
```

- `(Px, Py)` = `0x7325c8/0x7325cc` is the player's screen position. `FUN_00473540` (the actor
  post-tick) writes it when `actor == 0x74c09c` [BYTES 0x473907–0x473962]:
  ```
  y1 = a.y − a[+0x324]
  yy = a[+0x318] (ground)
  if (yy > y1) or (y1 − 2.75 > yy): yy = (that y1 or y1 − 2.75)
  D3DXVec3TransformCoord(&0x7325c8, (a.x, yy, a.z), &0x796170)
  ```
- `0x796170` is the world → back-buffer-pixel matrix [H-high: `FUN_005e0590` clamps the same
  transform to `[0, PP.BackBufferWidth/Height]`].
- **So the cursor is in 1024-units and the player in back-buffer pixels.** They agree only at
  1024×768. At other resolutions the arrow's angle is biased. The 1024/512 vs 1024/376.32 weights
  stretch y by 1.36 [H: a ground-plane foreshortening fudge]. This is a retail quirk; port it as-is.

### 2.5 Layer [BYTES 0x426b5b–0x426ba8, 0x5df950]

The cursor queues `{key 0x02000000, 0, FUN_00426f20, &0x7325c8, 0}` into the 2D overlay pass 15
(`0x703ec0`, cap `0x703eb8`) when:
- render sub-step `0x74c0f1` is set;
- `0x7325ec | 0x7325f0 ≠ 0`.

Pass 15 sorts **descending** on `+0`, then `+4` (comparator `0x5df950`). All the pass-15 keys,
from every writer of `0x703ec4` [BYTES]:

| key | what | relative to the cursor |
|---|---|---|
| 0x0E000000 | title (`FUN_005765c0`) | under |
| 0x0A000000+prio | busts | under |
| 0x08000000 | text box (`FUN_0057a0e0`) | under |
| 0x07000001 | HUD (`FUN_004295d0`) | under |
| 0x06800000 | caption | under |
| 0x04000000 | TA/Arena list (`FUN_0057bb20`) | under |
| 0x03000000+z | GoWndDialog windows (`FUN_005789a0`, key in edi) | under |
| **0x02000000** | **cursor** | — |
| 0x01000000 | FPS overlay (`FUN_00429530`, only with `ShowFPS`) | over |
| 0 | the `FUN_004ffdb0` effect overlay and `FUN_0058aeb0` | over |

- [H] The key-0 entries are screen flashes and fades.
- The pause book shows under the cursor in the grabs [DATA], so pass 15 is drawn after the book's
  pass. [H]

---

## 3. Visibility and mode state machine: `FUN_004263c0` [BYTES 0x4263c0–0x426bb6, EXPORT]

Called once per frame from `FUN_004281c0`. It runs **before** the object ticks (the vtable `+8`
calls) [EXPORT `FUN_004281c0` l.141 vs l.157+]. `FUN_004106e0` saves the input record around
`FUN_004281c0` and restores it afterwards. So the writes of §4.1 are seen by the player tick and
are then undone for everything else (menus, later sub-steps). [EXPORT `FUN_004106e0`]

### 3.1 State at `0x7325c8`

| addr | field | meaning |
|---|---|---|
| 0x7325c8/cc | Px, Py | the player's projected screen position (§2.4) |
| 0x7325d4 | mouseMode | 1 = the mouse is driving. Read by the menus and the player tick |
| 0x7325d8 | moved | 1 = the cursor moved ≥ 2 record units this tick. The menus' hover gate |
| 0x7325dc | visible | the target visibility |
| 0x7325e0 / ec / f4 | arrow channel | vestigial, see §2.3 |
| 0x7325e4 | menuMode | `0x7301bc ∥ 0x730174 ∥ 0x730270 ∥ 0x730294` (window pools: GoWndZzz, GoWndMain/GoWndDialog family, GoWndShop2, the pause book) |
| 0x7325e8 | eventMode | `0x75ad90 + 0x75ad94 ≠ 0` (script fix/lock) `∥ 0x730198` (GoWndText) |
| 0x7325f0 | colour | §2.3 |
| 0x7325f8 | alpha | §2.3 |
| 0x7325fc | moveAngle | player → cursor, y-up, back-buffer px (§4.1). Also the double-click lunge direction |
| 0x732600 | drawAngle | the rotation of the field sprite (§2.4) |
| 0x732604 | — | `+= 0.1` each tick, then overwritten with the player angle; no reader [EXPORT globals] |
| 0x732608 | holdTicks | 3 while MOUSE_DIR is held, counts down after release |
| 0x73260c | idle | the hide timer |
| 0x732610 | arrow timer | vestigial |
| 0x73265c/60 | prev x/y | the last record ints |

The window-pool meanings come from the window registry docs (`GoWndText`, `GoWndShop2`, …); see
`build/arch_re` and `build/menu_re`.

### 3.2 Per tick (only when the logic step `0x74c0f0` is set; otherwise only the draw is queued)

```
menuMode  = any of the four window pools alive
eventMode = script lock || GoWndText alive
(fx, fy) = record ints → §1.2 clamp/scale
moved = |rec.x − prevX| ≥ 2 || |rec.y − prevY| ≥ 2          // "cmp 1; jg"
if !moved:
    if rec.dir != 0: mouseMode = 0                            // a keyboard/pad direction takes over
    movedFlag = 0
else:
    if menuMode:            visible = 1; mouseMode = 1
    elif rec.held & 0x1000: mouseMode = 1                     // the field: only a drag with MOUSE_DIR
    movedFlag = 1
prev = rec
// the idle hide (0x73260c)
if idle == 0:
    if rec.dir || rec.magnitude != 0 || (rec.held & ~0x10000): idle = 1
elif idle > 61: visible = 0                                   // stays at 62
else: idle++
if mouseMode && !visible: idle = 0; visible = 1
if GoWndDemo (*0x7302b4) && demo[+0x280] == 1: visible = 0    // the attract/demo state
```

Consequences [H, from the above]:
- **In menus** any cursor motion shows the cursor at once.
- **In the field** plain motion does nothing. The first move with MOUSE_DIR held enters mouse mode
  and shows the cursor.
- While mouse mode is on, the cursor never hides.
- Mouse mode ends on the first tick that has a keyboard or pad direction and no cursor motion. The
  cursor then fades out once the idle timer passes 61. The timer started at the first non-click
  input after the last reset, so the delay is ≤ 62 ticks.
- `held & ~0x10000` includes 0x1000 and 0x4000, so holding the mouse buttons also counts as input
  for the timer.
- **Events and cutscenes** fade the cursor out (the `!menuMode && eventMode` branch), unless a
  menu window is up.
- Windowed and fullscreen behave the same: there is no `Windowed` test here.

### 3.3 The OS cursor [EXPORT `FUN_0040b600`, `FUN_0040efb0`, `FUN_0040dcc0`]

- **Fullscreen window creation:** `do ShowCursor(FALSE) while (count ≥ 0)`.
- **`WM_SETCURSOR` (0x20):** when the app is active (`0x7493cb`, set by WM_ACTIVATEAPP) and
  (fullscreen `0x7493ca == 0` or hit-test == HTCLIENT), it calls `SetCursor(NULL)` and
  `device->ShowCursor(FALSE)` (vtable +0x30) and returns 0. Otherwise it calls DefWindowProc.
- **`WM_NCMOUSEMOVE` (0xa0):** `do ShowCursor(TRUE) while (count < 0)`. So the arrow is shown over
  the title bar and borders.
- **Shutdown:** `ShowCursor(TRUE)` until the count is ≥ 0.
- The class cursor is resource 0x7f00 (an arrow). `LoadCursorW(IDC_WAIT 0x7f89)` appears only in
  the config tool's dialogs.

---

## 4. Mouse in gameplay

### 4.1 Click-to-move [BYTES 0x42681e–0x426b53]

This runs only while the cursor alpha `0x7325f8 ≠ 0` and the controlled player `0x749d44` exists:
```
holdTicks = (rec.held & 0x1000) ? 3 : max(holdTicks − 1, 0)
P = (a.x, y', a.z),  base = a[+0x230] + a[+0x324],  d = a.y − base
y' = d > 5 ? base + d − 5 : d < −a[+0x324] ? base + d + a[+0x324] : base;  y' −= 2.0
(px, py, pz, w) = D3DXVec3Transform(P, 0x796170);  px /= w; py /= w      (NaN → skip the rest)
dx = px − BBW·X/1024,  dy = py − BBH·Y/768                     // both in back-buffer px
if dx²+dy² != 0:
    ang = atan2(py − cY, cX − px)                              // player → cursor, y-up
    0x732604 = ang;  if colour != 0: 0x7325fc = ang
else ang = 0x7325fc
if dx²+dy² < 0.25:  holdTicks = 0                              // within 0.5 px: stop
elif holdTicks:
    c = atan2(−(Y − 360)·2.7210884, (X − 512)·2)               // from the SCREEN CENTRE
    dirBits = 8-way: table 0x6b7384[floor((c + π + π/8)·4/π) & 7] = {2,3,1,5,4,12,8,10}
              (4-way when 0x7035c0 == 4: 0x6b7380[floor((c + π + π/4)·2/π) & 3] = {2,1,4,8})
    rec.angle (+0x18) = ang;  rec.magnitude (+0x1c) = 1.0;  rec.dir (+0x14) = dirBits
```

- `(cX, cY)` = (BBW·X/1024, BBH·Y/768).

**What this means:**
- **Direction** is the stick-space angle from the player's on-screen feet to the cursor. The
  player tick `FUN_0046f980` passes XMM1 = `rec.angle` (+0x18), **but in mouse mode
  (`0x7325d4 == 1`) it passes `0x7325fc` instead**, plus `param_3 = 1` [BYTES 0x46f9ca–0x46fa12].
  The mover subtracts the camera yaw like a stick (MOVEMENT_GAPS.md gap 5a). So **move toward the
  cursor**, re-aimed every tick.
  - Side effect [BYTES]: while mouse mode lasts, keyboard or pad movement with the mouse also
    moving is steered toward the cursor too. Mouse mode only ends on a tick where a direction is
    held and the cursor did not move.
- **Speed** is always magnitude 1.0, which is ≥ `RunWalkThreshold`, so it **runs**. Walk/Run (0x20)
  held still walks. There is no distance-to-speed law and **no deadzone** beyond 0.5 px.
- **The direction bits** come from a different origin: the screen centre (512, 360), with y
  weighted 1.36. They only make `+0x514` non-zero and feed the bit-based consumers (the
  `+0x710` double-tap FSM's "fresh press" test, the dash FSM). [H: the player is usually near the
  screen centre]
- Movement continues **2 ticks after release** (3 → 2 → 1 → 0).
- **Facing** follows the move angle through the normal mover turn (rate 0.35 / 0.18).
- Preset 2 leaves MOUSE_DIR unbound (`---`), so click-to-move is a **Preset 1** feature
  (L-button). With Preset 2 the field cursor never appears: mouse mode needs 0x1000.

### 4.2 Double-click: the directional lunge [BYTES 0x465643–0x4656d6, EXPORT `FUN_0046ea00` l.270]

In `FUN_00465170`, each tick with `param_4 == 0`, `+0x70c = 0`, then:
- **Mouse mode (`0x7325d4 == 1`):**
  - if `+0x710 == 4`: `+0x718++`, `+0x70c = 1`; past 12, `+0x710 = 0`. Otherwise `+0x718 = 0`.
  - if `pressed & 0x800` (double-click, CONFIG.md §3.4): `+0x714 = 0x7325fc` (the player → cursor
    angle), `+0x70c = 1`, `+0x710 = 4`, `+0x718 = 0`.
- **Otherwise:** the keyboard version. A fresh direction press within ±2.244 rad of the facing
  arms state 1. A release within 11 ticks moves to state 2. State 2 holds `+0x70c = 1` for up to
  12 ticks, or until a direction is pressed again.

`+0x70c ≠ 0` has two effects:
1. **Jump is suppressed** [EXPORT l.1898].
2. In `FUN_0046ea00` (the attack start), on the ground and not already in section 0x112:
   `+0x308 = +0x714`, then `FUN_00446550(0x112)`. For `0x76bb74` ∈ {0, 1, 3} it adds a
   `0.1·(cos, sin)(angle − camYaw)` impulse to `+0x338/+0x340` and sets `+0x3b8` = 60.0.

So: **double-click, then attack within 12 ticks = a lunge attack toward the cursor.** [H: what
section 0x112 looks like per character; ABILITY.md's "Hugo magic variant" reading is
unverified.]

### 4.3 Buttons, wheel, other

- **Attack, jump, magic, boost** from the mouse are ordinary mapper bits (slots 28–33), CONFIG.md
  §3.3. Attacking does **not** face the cursor, except through §4.2. The attack uses the current
  facing.
  - Preset 1: R = Jump, M = Boost.
  - Preset 2: L = Attack, R = Jump, M = Magic.
  - With `MouseButtonAlwaysOkCancel` = 1, a mouse-bound Attack does not make 0x2000/0x10000, but
    the L-button gives 0x10000 and the R-button gives 0x4000 (Cancel) anyway.
- **Wheel = skill switch** [BYTES 0x465a56–0x465b31, 0x46cb00–0x46cc0c]. `FUN_0046f980` passes
  `rec.wheel` as the mover's `param_2`; it is 0 when not the player or during a script lock.
  1. If the cooldown `player+0x764 ≠ 0`, the wheel is forced to 0 and the cooldown counts down.
  2. If attack is pressed and `FUN_00460470()` succeeds, the switch is skipped this tick.
  3. Otherwise, `(pressed & 0x7c0) || wheel` → `FUN_0046cb00(wheel)`:
     - `q = trunc(wheel / 120)`. q ≥ 1 → +1, q ≤ −1 → −1; then `+0x738 = dir` and
       **`+0x764 = 15`** (15-tick cooldown).
     - **|wheel| < 120 does nothing.** Wheel away (+120) equals Previous Skill (bit 0x40, +1);
       toward you equals Next Skill (0x80).
     - Skills cycle 0x74..0x76, skipping unowned ones (`0x76b91c[id] < 1`).
- **No camera control.** Nothing mouse-related writes the camera. The cursor manager only reads
  the matrix. [EXPORT: the readers of 0x764730/0x732624 are the cursor manager, the menus and
  the mover]
- **Player spawn** (`FUN_00434020` → `FUN_00426bc0(xmm1 = spawn yaw θ)`) [BYTES 0x426bc0–0x426c64]:
  `0x7325fc = 0x732600 = θ`, and the cursor is warped (mode 0) to
  `(trunc(512 + 96·cos(−θ)), trunc(360 + 96·sin(−θ)))`, i.e. 96 units from the screen centre along
  the facing (y-down screen).

---

## 5. Mouse in menus

Common rules [EXPORT/BYTES, the per-window sections below]:
- **Hover** is a point-in-rect test on the **raw record ints** (`rec.x`, `rec.y`, unclamped,
  full-width-stretched 1024 space), minus 16 on both axes. That point is the drawn arrow's tip
  (anchor −15, §2.2). The TA/Arena list is the exception: it uses the cursor without the −16.
  Most windows test hover only when `0x7325d8` (moved) is set, so a still cursor never fights the
  keyboard. A few test it every tick in mouse mode.
- **Click** = pressed `0x10000`, normally accepted only when the pointer is on an item. Keyboard OK
  = `0x2000`. **Right-click** = `0x4000` (Cancel) when AlwaysOkCancel = 1. **`0x8000` is not used
  by any menu.**
- **Pillarboxed menus** (the authored 4:3 field, csprite x-anchor 2) convert the cursor back with
  `ax = trunc((mx − p)/(1024 − 2p)·1024)`, where `p = 512(A−1)/A` (`FUN_0057b3c0`, the csprite test
  `FUN_005a4760`). They warp with mode 2.
- The sound on a hover change is SE 3 (`FUN_0043a040(3)`), the same as a keyboard step.
- **Wheel** is read only by:
  - the skill switch (§4.3);
  - the teleport map and the Save/Load list (§5.3);
  - char-select (§5.4);
  - the shop quantity and toast (§5.4).

  The book and the shop debounce it to one event per 4 ticks.
- **Warps (`FUN_005d33f0`)** [all BYTES, in §4.3 / §5.3 / §5.4]:

  | what | mode | gate |
  |---|---|---|
  | Player spawn → 96 units along the facing | 0 | — |
  | Book / shop close → back to the open-time point | 2 / 0 | — |
  | Book and shop transition highlight → its bottom-right, every tick of the 8-tick tween | 2 | mouse mode only |
  | Teleport map / Save-Load wheel step → the new item's bottom-right | 2 | — |
  | Char-select key or wheel step → the bar's bottom-right −32 | 2 | **ungated** |
  | Char-select builder → bar 0 | 2 | — |

  Every warp also writes `0x73261c/0x732620` so the sprite jumps the same tick.

### 5.1 Generic list windows: GoWndDialog `FUN_005789a0` (title menu, Bonus list, Type/Difficulty/SP popups, script `Window_Menu` / `Menu_*` choices, item-get box, Bonus-shop list)

Fields: `cx +0x1fe0`, `top +0x1fe4`, `w +0x1fe8`, `h +0x1fec`, `sel +0x1ffc`, rows `N +0x1fb0`
(row 0 = header), `hover +0x1fd7`.

**Hit test** [BYTES 0x578a26–0x578ad2, 0x578d9b–0x578e71]:
```
dy = trunc(float(my) − top) − 16;   require 0 ≤ dy < trunc(h)
dx = trunc(float(mx) − cx) − 16;    require 2·|dx| < trunc(X(w))      // X = FUN_005d3be0(0, 2, v, 0)
row = 1 − trunc((dy + 6.4) / −38.4) // 0x6bd8a0, 0x6be10c;  require 1 ≤ row < N
```

**Path A, the snap on the first interactive tick** [BYTES 0x578a0e–0x578af1]:
- Runs when the window just became interactive (`+0x2008 ≠ 1`), `0x7325d4 == 1` and `mx ≥ 0`.
- On a hit: SE 3 if the row changed, `sel = row`, `hover = 1`.
- This path is missing from TITLE_DRAW.md and WNDDIALOG_DRAW.md.

**Path B, every tick once the header is revealed** [BYTES 0x578d1d–0x578e90]:
1. The keyboard step `sel += rec.dy` (wrapping, SE 3).
2. Then, **only if `0x7325d8`**: `hover = 0`, then the hit test, with an extra left check
   `double(mx + 16) ≥ X(0.0)`. This path does **not** require mouse mode. On a hit: SE 3 if
   changed, `sel = row`, `hover = 1`.

The item-get box (`+0x1fac ≠ 0`) forces `hover = 1`, so a click anywhere confirms.

**Confirm / cancel**, gated on interactive and released (none of `0x12000`, or `0x16000` with a
cancel record, held) [BYTES 0x578b30–0x578c22]:
- Confirm on pressed `0x2000`, or `0x10000 && hover`: SE 0, `FUN_00578960(sel)`.
- Cancel on `0x4000` with a cancel record (`+0x2004 ≠ −1`): SE 1.

Other details:
- `hover` is cleared only by cursor motion. After a hover and a keyboard move, a click without
  moving the mouse confirms the keyboard row. [H]
- No wheel, no warp.
- It zeroes `rec +0x14..+0x30` after use: the window consumes the input.
- **[H] Widescreen quirk:** `X()` returns back-buffer px but is compared with 1024-unit
  distances. Exact at 4:3; at 1920×1080 the half-width band is 735/2 instead of 352/2, and the
  left gate is `mx ≥ 224`. Port as-is behind the same formula, and confirm in game.

### 5.2 Time Attack / Arena stage select `FUN_0057bb20` (key 0x04000000)

- **Hover runs every tick in mouse mode** (`0x7325d4 == 1`). The keyboard step is skipped while in
  mouse mode.
- `ax = FUN_0057b3c0(mx)` (the inverse pillarbox), `fx = ax − 96`, `fy = my − top` (top = 144 TA
  `0x6bdb60`, 176 Arena `0x6bdb94`).
- Hit when `0 ≤ fx < 640` and `0 ≤ fy < rows·32`; then `cur = trunc(fy/32)`, else `cur = −1`.
  The hit rows sit 3.2 below the drawn rows (140.8 / 172.8).
- The highlight is not drawn while `cur == −1`. SE 3 on change (Arena sub-state 2: no sound).
- **Click on pressed `0x12000`:** on a row, it acts on that row. **Off the list it acts as Back**:
  - sub-state 0 leaves the screen;
  - sub-states 1 and 2 go back one level.
- Cancel `0x4000` takes the same paths. No wheel, no warp.
- [BYTES 0x57c0b8–0x57c13b, 0x57ca85–0x57cdb8, 0x57bea8–0x57ca23]

### 5.3 The pause book (GoWndMain2) and the statue save list

Re-verified against the bytes. Where they overlap, the hover and click summaries in
PAUSEBOOK_DRAW.md §2.3/§3.4/§6.4 and PAUSEBOOK_DRAW_SVLD_OPT.md §1.4/§2/§3.6 are right. New here:
the exact rects, the input blocking, the warps and the wheel.

**Shared mechanics** [BYTES 0x5a51ff–0x5a5291, 0x5a4760–0x5a484b]:
- Each page update receives the record **by value**: +0x00/+0x04 cursor, +0x08 wheel, +0x24
  pressed, +0x2c/+0x30 steps, and `&subId`. A page zeroes its copy unless `subId` is its own.
- **The dispatcher `FUN_005a51a0`:**
  - zeroes the record for the first 2 frames after opening (`this+0x134 < 2`);
  - **wheel debounce `0x7d9be0`**: a non-zero wheel sets it to 3, and the next 3 ticks' wheels are
    zeroed;
  - **zeroes the whole record while `0x74418c == 0`**, i.e. while the page-transition highlight
    tweens. There is no hover, click or wheel during a page change.
- **Hit point** `p = (mx − 16, my − 16)`. This is the visible arrow **tip** (the sprite's tip is at
  anchor −15, §2.2), so menus hit-test the tip while the field steers from the anchor. Every warp
  target is a rect's bottom-right corner, which puts `p` just inside the rect.
- **Rect** = the csprite's (+0 x, +4 y, +8 w, +0xc h) in authored 1024×768 floats, half-open
  `x ≤ p.x < x + w`.
  - If the csprite x-anchor (+0xa4) is 2, `p.x` is first unprojected:
    `off = 512(A−1)/A`, `p.x = trunc((p.x − off)/(1024 − 2·off)·1024)`.
  - The y-anchor path (`+0xa8 == 2` → `FUN_005d3be0(axis 1)` on the rect) is dead for book
    sprites. [H]
  - Shared test: `FUN_005a4760(ecx = csprite, px, py)`.
- **The common pattern:**
  ```
  sel = keyboard_nav(sel)
  if (0x7325d8) for i: if hit(rect[i], p) { sel = i; break }   // a miss leaves sel unchanged
  if sel changed: SE 3; the page cursor snaps to the slot
  confirm  if pressed & 0x2000 || (pressed & 0x10000 && hit(rect[sel], p))
  cancel   if pressed & 0x4000
  ```
  - **No book page reads mouse mode `0x7325d4`.** Hover runs on motion only.
  - A click must land inside the **selected** rect, which after a hover is the hovered one.
  - A click outside every rect does nothing; there is no "click outside = back".

| page | function | hover rects | wheel | warp |
|---|---|---|---|---|
| Opener | `FUN_005a4fd0` | — saves `rec.x/y` to `this+0x128/+0x12c` | — | — |
| Close | dtors `FUN_005a4b60/5a4de0` | — | — | back to the saved point, **mode 2** [BYTES 0x5a4bdf–0x5a4c2d]. [H] At A ≠ 1 this pillarbox-remaps a full-width point, so the pointer drifts toward the centre; a retail quirk |
| Tab plaques (subId 0) | `FUN_005a68f0` | plaques `0x732e64 + i·0xAC`. The loop scans 9; only i < 5 are taken. One click selects and opens (`subId = {1,2,6,11,18}[i]`) | — | — |
| Equip (1) | `FUN_005a8030` | 22 tiles `0x733730 + i·0xAC` = (X0+4.8, Y0+4.8, 54.4, 54.4) | — | — |
| Item (2) | `FUN_005aa370` | 30 tiles `0x735f90 + i·0xAC`, with the inline unprojection | — | — |
| "Use this item?" (3) | `FUN_005abd10` st 1 | Yes (bx+96, by+32, 88, 26), No (bx+96, by+67, 88, 26), x-anchor 2 | — | — |
| Teleport map (3, st 3) | `FUN_005abd10` | 22 markers `0x739168 + i·0xAC` = (X, Y, 240, 40). Hover is skipped on a wheel tick | `idx = (sign + 22 + idx) % 22`, so wheel-up = +1 | after a wheel step: `(trunc(X+240), trunc(Y+40))`, mode 2 [BYTES 0x5ac94e–0x5ac9df] |
| Save/Load list (4/6/8) | `FUN_005b5100` | 8 plates `0x73c2ec + i·0xAC` = (152.4, 130+72i, 731, 57). **Tabs: click only**, not motion-gated: 8 tabs (210.4+80i, 64, 80, 52) set the page | `row = (row − sign + 8) & 7`, so wheel-up = row − 1 | after a wheel step: `(883, 187 + 72·row)`, mode 2 (the plate's bottom-right) [BYTES 0x5b5841–0x5b589c] |
| Load confirm (7/9) | `FUN_005b6eb0` | 3 buttons `0x73cc70 + i·0xAC` (Yes/No/Delete) | — | — |
| Save overwrite (5) | `FUN_005b7cc0` | 3 buttons `0x73cfe0 + i·0xAC` | — | — |
| Delete (10) | `FUN_005b6220` | 2 buttons `0x73d350 + i·0xAC` | — | — |
| Options Yes/No box (0xf / 0x10) | `FUN_005b8d40` (from `FUN_005ba2b0`) | Yes `this+0x16c`, No `+0x218` | — | — |

Notes:
- The confirm-dialog buttons are at `Ox+531/658, Oy+128 or Oy+12`, 89×25.6 (SVLD §2).
- The item-action state 2 (`0x738b40 = 2`, dismissed by any of 0x16000 anywhere) has no writer,
  so it is unreachable. [EXPORT globals]

**The page-transition highlight `FUN_005bb1b0`** [BYTES 0x5bb4c9–0x5bb52b]:
- When `subId` changes, the highlight `0x7440e0` tweens from the old page's cursor rect to the
  new one's, with `0x74418c = 0`.
- **In mouse mode (`0x7325d4`), every tick of the tween warps the pointer** to the highlight's
  current bottom-right corner (`FUN_005d33f0(trunc(x+w), trunc(y+h), mode 2)`, plus
  `0x73261c/20`). So the pointer rides the highlight onto the new selection.
- `FUN_005bb540` is an uncalled twin.
- The per-subId rect table (25 entries) is not fully verified; subIds 5 and 7 look swapped in the
  decompile.

Sounds go through `FUN_005ce2a0`: 0 confirm, 1 cancel, 2 buzzer, 3 move, 9 save, 15 equip.

### 5.4 Options, sliders, data cards, shop, skill wheel

All of these use the §5.3 pattern: hit point `(mx−16, my−16)`, `FUN_005a4760` or a byte-identical
inline copy, hover gated on `0x7325d8` only, click inside the current item.

**Options page `FUN_005ba2b0`** (10 rows `0x73d564 + i·0xAC`, cursor `0x73d4b4`; state 0xb/0x11):
- Hover over rows 0–9. On a change: SE 3, and the highlight `0x73dc1c` gets the row rect (row 7
  "Default" is forced to w = 179.2).
- Click or OK:
  - rows 0/1/2 enter slider mode (state 0xc/0xd/0xe);
  - rows 8/9 open the Yes/No box (0xf/0x10, §5.3 `FUN_005b8d40`);
  - rows 3–6 toggle;
  - row 7 resets the defaults.
- No wheel, no warp. [BYTES 0x5ba53f–0x5ba8b8, jump table 0x5bacd0]

**A slider `FUN_005b9480`** (BGM / SE / Brightness at `0x73e0d4/0x73e248/0x73e3bc`, id +0x10 =
0xc/0xd/0xe; active only while the state is its id) [BYTES 0x5b948c–0x5b96e7]:
- **Grab:** every tick while `held & 0x10000`, hit-test the knob csprite (+0x1c, x-mode 2). On a
  hit: `+0x14 = 1` (dragging), `+0x18 = knob.x − rawX`. A release clears `+0x14`.
- **Drag:** if held, dragging and `0x7325d8`:
  `value = trunc((rawX + off − base) · max / 188.8)` (0x6bdbb0).
  - It uses the **raw, un-unprojected x**, so at 16:9 the knob moves 1:1 in 4:3 units, faster than
    the pointer. This is a retail quirk.
- **Keys:** ±max/128 per tick on dir bits 2/4, **only when not in mouse mode** and 0x1000 is not
  held.
- **Leaving:** only Cancel (0x4000) leaves the slider (→ 0xb). OK does nothing.
- Then clamp, reposition the knob (`base − trunc(value·−188.8/max)`, 16×51.2) and apply.

**Topic pages** (book page 4; there are no wheel steps and no warps):

| fn | state | items | click / OK |
|---|---|---|---|
| `FUN_005bbae0` | 0x12 category | 3 bars `0x73eac4` | Character / Monster → 0x13, Manual → 0x16 |
| `FUN_005bc6a0` | 0x13 datacard nav | 3 hotspots `0x73ed80` (prev / open / next) | page −1 / → 0x14 / page +1 |
| `FUN_005bd480` | **0x14** entry grid | ≤ 32 cells `0x73f258`; the hover loop covers only the live count | → 0x15 |
| `FUN_005bf890` | **0x15** spread | 2 buttons `0x740a9c` (prev / next) | `FUN_005bdfd0(∓)` |
| `FUN_005c17a0` | 0x16 Manual | 13 tabs `0x79468c` | show that tab |

TOPIC_LAYOUT.md has 0x14 and 0x15 swapped: the bytes say 0x14 = the grid, 0x15 = the spread.

**Char-select / New Game / Arena `FUN_005c3790`** (state 0x17, case 1; bars `0x7432dc + i·0xAC`,
n = 3, or 4 in the Arena) [BYTES 0x5c3a6d–0x5c3e80]:
- **Wheel:** `idx = (cur − sign(wheel) + n) % n`, so wheel-up moves to the upper bar. Hover is
  skipped that tick.
- **Hover:** on `0x7325d8` over the bars.
- **Warp when the change did not come from hover** (keys or wheel), **with no mouse-mode gate**:
  `FUN_005d33f0(trunc(bar.x + 896 − 32), trunc(bar.y + 128 − 32), mode 2)`, i.e. the bar's
  bottom-right inset by 32.
  - The same warp runs on returning from the popups.
  - The builder `FUN_005c2110` warps unconditionally to bar 0.
- **Click:** inside the current bar, or OK → commit `0x76bb74`, SE 0. Cancel → −1.

**The book's page-transition highlight** `FUN_005bb1b0` is described in §5.3. It tweens in exactly
8 ticks (speed |Δ|·0.125). Its 25 slots, by sub-state:
- 0 = tab;
- 11/17 = the Options row;
- 12–14 = the knobs (+10);
- 15/16 = the box buttons;
- 18–22 = the Topic pages;
- 23 = the char-select bar.

`FUN_005bb540` is dead: the byte scan finds no call or pointer to it. WINDOW_UI.md's
"`FUN_005d33f0` = cursor render-state" is wrong; it is the warp.

**HUD:** no HUD code reads the cursor [EXPORT globals: the readers of 0x764730/34 are the mapper,
the cursor manager, the menus, the shop and the book].

**GoWndShop2 (Bonus Shop) shell** [BYTES `FUN_005c5800`, `FUN_005c5b20`, `FUN_005c5490/5690`]:
- **Open:** the constructor saves `rec.x/y` to `+0x128/+0x12c`.
- **Each tick:**
  - **Wheel debounce:** a non-zero wheel sets `0x7d9be8 = 3`, and the next 3 ticks' wheels are
    zeroed. That is at most one wheel event per 4 ticks.
  - While the focus highlight `0x7440e0` is still tweening (`0x74418c == 0`), **the whole input
    record is zeroed** before it reaches the sub-handlers.
  - **Warp:** in mouse mode, when the focus region changes or the tween runs, it calls
    `FUN_005d33f0(trunc(x + w), trunc(y + h), mode 2)` on the highlight's rect, i.e. its
    bottom-right corner.
- **Close:** both destructors warp back to the saved point (mode 0).
- **Trade / quantity popup `FUN_005caf40`:**
  - Hover over the 2 buttons (`0x745b30 + i·0xac`) via `FUN_005a4760(mx − 16, my − 16)`, only on
    `0x7325d8`.
  - In mouse mode, a click on csprite `0x7459d8` / `0x745a84` gives quantity +1 / −1.
  - The wheel gives `qty += sign(wheel)` **and toggles the button selection `0x74587c`**. The
    toggle happens after the change check, so there is no SE and no highlight move. [H: likely a
    retail bug, verify in game]
  - The focus highlight `0x74592c` = (btn.x − (112 − w)/2, btn.y − (35.2 − h)/2, 112, 35.2).
  - Buy plays SE 4.
- **Arena bonus-shop list:** it is a GoWndDialog, so §5.1 applies.
  - Confirm on `0x2000`, or a click inside the selected button. Cancel on `0x4000`.
- **`FUN_005c9d20`:** the toast (sub-state 3) is dismissed by `0x16000`, the wheel, `rec.dy`, or any
  cursor motion.

---

## 6. Config interplay

| Setting | Mouse effect |
|---|---|
| Preset 1 (default) | L = MOUSE_DIR (click-to-move, double-click lunge, field cursor), R = Jump, M = Boost |
| Preset 2 [WASD/Mouse] | L = Attack, R = Jump, M = Magic; **MOUSE_DIR unbound**, so no click-to-move and no field cursor |
| `MouseButtonAlwaysOkCancel` (default 1) | L → 0x10000 (click), R → 0x4000 (Cancel) in every context, on top of their slot bits. With 0, only mouse-bound slot-0/1 bindings make 0x10000/0x8000 and 0x4000 |
| `DoubleClickFrame` (12) | the 0x800 window (CONFIG.md §3.4) |
| `MouseSensitivity` | unused; the cursor gain is the fixed 1.09375 / 1.125 |
| `[graphics] BackBufferWidth/Height`, `Windowed` | the §1.1 mapping and the windowed x quirk |
| `StretchAspect` | `A` = 1, so no sprite squaring and no menu pillarbox remap |
| 4-way (`0x7035c0`, the pause book) | the click-to-move direction bits quantise to 4 |

---

## 7. What CleriaCore did before the port (2026-10-06 working tree; superseded by §11)

- `apps/cleria-view/view_input.inl` `SdlInput`:
  - feeds **only the mouse buttons** (`SDL_GetMouseState` button mask, plus sub-tick taps) and the
    **wheel** (`SDL_EVENT_MOUSE_WHEEL`, summed `e.wheel.y` notches, **not ×120**) into
    `input::Snapshot`;
  - **no cursor position.**
- `src/cleria/input_map.*` implements the mapper's bits (0x800/0x1000/0x2000/0x4000/0x8000/0x10000,
  with AlwaysOkCancel). `Frame` has no `+0x00/+0x04` cursor.
- Consumers:
  - Windows confirm on `kOk | kClick` held/pressed (`view_game.inl` ~2227, ~4240, ~4468), **with
    no hover test**. Any click confirms the keyboard row. Retail needs `hover` (§5.1).
  - Movie skip on `held & 0x1001`.
  - **The left button does not attack in Preset 1** (it is MOUSE_DIR, which is consumed nowhere
    else). That is correct per retail.
- The OS cursor stays visible over the window: there is no `SDL_HideCursor`. No cursor sprite.
- Free-fly debug mode: mouse drag to look, wheel = speed (`view_game.inl` ~4122). This is debug
  only, not retail.
- The shell overlay (`view_ui.inl`, ImGui) uses the mouse for its own widgets. It suppresses game
  input while focused.
- `titlescreen.hpp` lists "the mouse" as [not modelled]. The pause book, wnddialog and options
  ports have no hit tests.

---

## 8. Port plan

### 8.1 Pure pieces (new `src/cleria/mouse.hpp/.cpp`, unit-testable, no SDL)

1. **`cursor_from_client(sx, sy, W, H, windowed_origin)`**: the §1.1 integer formula, plus the
   inverse `client_from_cursor(X, Y, mode, A)` (§1.3) for warps. Tests use known pairs, including
   the edges (−48, 1072, 864) and round trips.
2. **`CursorState` + `step(const input::Frame&, ctx)`**: §3.2 verbatim.
   - ctx = `{menuMode, eventMode, demoHide, logicStep}`.
   - Output: `mouseMode`, `moved`, `visible`, alpha, colour, holdTicks.
   - Keep the vestigial arrow channel only as the queue gate.
3. **`steer(CursorState&, playerScreen(px, py), cursor X/Y, BB size, frame&)`**: §4.1. It rewrites
   `frame.angle / magnitude / dir`, with the centre-based 8/4-way table and the 0.25 stop. It
   returns the player angle for `0x7325fc`. The caller must apply it to a **copy** of the frame
   that only the player tick sees (§3: retail restores the record afterwards).
4. **`cursor_quad(Xi, Yi, upright, drawAngle, A, W, H)`**: §2.2 / §2.4. It returns 4 vertices and
   UVs: transposed, rotation, `FUN_005e4100` snap, widescreen squaring. Golden-test it against
   `reference/typeselect_adol.png` (43×43 at (1782, 1010), a 1440p grab).
5. **`draw_angle(X, Y, Px, Py)`**: §2.4, keeping the 2.0 / 2.72109 weights and the unit mismatch.
6. **Menu hit helpers**:
   - `wnddialog_row(mx, my, cx, top, w, h, N, A, W, H)` (§5.1, including `X()`);
   - `pillarbox_inv(mx, A)` (`FUN_0057b3c0`);
   - `csprite_hit(sprite, mx − 16, my − 16)` (`FUN_005a4760`);
   - and the per-page tests from §5.3 / §5.4.

   Also: `slider_drag` (§5.4), and the 8-tick transition highlight with its warp, which already
   exists as the book's csprite tweener and needs only the warp hook.
7. **The double-click lunge** belongs in `physics_player` / the attack start: `+0x70c/+0x710/+0x714/
   +0x718` (§4.2). It needs `mouseMode` and `moveAngle` from the cursor state.

### 8.2 Depends on the input mapper (the other session's `input_map.*`)

- `Snapshot` gains `cursor_x/cursor_y`: client pixels in back-buffer space (the view maps SDL
  window coords to the swapchain size). `Frame` gains `x/y` (+0x00/+0x04) from
  `cursor_from_client`.
- `wheel` **must be the raw ±120-per-notch delta**: `FUN_0046cb00` needs |wheel| ≥ 120.
  CleriaCore's `SdlInput` sums `e.wheel.y` notches (±1), so the skill wheel would never fire. Use
  `int(e.wheel.y * 120)`, or the last event of the tick like retail (§1.1). The menus test only
  the sign.
- The masks it already produces (`held/pressed` with 0x800/0x1000/0x10000/0x4000) are exactly what
  §3–§5 consume. Nothing else is needed from it.

### 8.3 View wiring (`apps/cleria-view`)

- `SDL_HideCursor()` while the window is focused and the pointer is over the client (retail's
  WM_SETCURSOR). Show it over the shell overlay, which draws its own ImGui cursor.
- Warps: `SDL_WarpMouseInWindow` with `client_from_cursor`.
- Each logic tick:
  1. `input_tick()` → `CursorState::step`.
  2. If in the field and the player exists, project the player (`FUN_00473540`'s ground-anchored
     point) with the view's world → back-buffer matrix, then `steer` on the player's private frame
     copy.
- Draw the quad last in the 2D overlay: after the windows, book, HUD and title; before the FPS
  text and the flash/fade overlays. Use `CURSOR.DDS`, alpha blend, colour white with A =
  floor(f·256).
- Menus: add the §5 hover and click tests to `wnddialog`, `titlescreen`, `pausebook`, the options
  and the shop ports. Fix the click-confirm gate to `0x2000 || (0x10000 && hover)`.

---

## 9. Corrections to CONFIG.md §3

**Applied 2026-10-06** with the port (§11): CONFIG.md §2.3 / §3.1 / §3.4, TOPIC_LAYOUT.md §4.1,
WINDOW_UI.md (four places) and MOVEMENT_GAPS.md 5a now say what is below.

- §3.1 lists `lX, lY, lZ` as the mouse state. **The cursor is `GetCursorPos`**, and `lX/lY/lZ`
  are never read. The wheel is `WM_MOUSEWHEEL`, not `lZ`. [EXPORT globals.tsv, `FUN_0040efb0`]
- §3.4 "cursor x / y in 1024×768 space": it is an **over-scanned** 1024×768 space,
  x ∈ [−48, 1072), y ∈ [0, 864) at fullscreen. The drawn copy is clamped to
  [−48, 1072] × [−48, 816] (§1).
- §2.3 "`0x7035c4/0x7035c8` (mouse scale)": these are plain copies of
  BackBufferWidth/Height, used as the divisor of the cursor mapping and in the warp.

Other docs:
- `build/menu_re/TOPIC_LAYOUT.md`: sub-state 0x14 is the grid (`FUN_005bd480`) and 0x15 the spread
  (`FUN_005bf890`), not the reverse.
- `build/window_re/WINDOW_UI.md`: `FUN_005d33f0` is the cursor warp, not a "cursor render-state".
- `physics_re/MOVEMENT_GAPS.md` 5a: in mouse mode the mover's angle is `0x7325fc`, not
  `rec+0x18` (§4.1).

---

## 10. Open questions

1. **Section 0x112** (§4.2): what the double-click lunge looks like for each character, and why
   `0x76bb74 == 2` gets no impulse.
2. **GoWndDialog at 16:9** (§5.1): confirm in game that the hover band really uses the
   px-vs-1024 mix. A grab with the cursor at a row edge would settle it.
3. **`0x796170`**: identify its writer, to pin it as view·proj·viewport (BB px). The arrow
   rotation bias at non-1024×768 resolutions depends on it.
4. **`a[+0x230]`** in the click-to-move projection (§4.1): its meaning (ground height?) is not
   traced.
5. **The demo hide** `*0x7302b4 → +0x280 == 1`: which GoWndDemo state this is (attract demo vs
   title).
6. **Logic tick vs frame**: every counter (fade 0.1/0.08, idle 62, hold 3, lunge 12, the mapper's
   DoubleClickFrame) runs per logic step (`0x74c0f0`). The mapper runs per frame. [H: at retail's
   fixed 60 Hz they coincide]
7. **The shop quantity popup's wheel** also toggles the button selection (§5.4); verify in game.
8. **The book's close warp uses mode 2** on a full-width point (§5.3). At 16:9 the pointer should
   drift toward the centre on close; verify.
9. **Char-select warps on keyboard steps without a mouse-mode gate** (§5.4). Confirm that the OS
   pointer really jumps for keyboard players.
10. **`FUN_005bb1b0`'s 25-slot rect table**: subIds 5 and 7 look swapped in the decompile. Check the
    disassembly (0x5bb1f9..) before porting the transition warp.
11. **The `+0x515` angle gate in `FUN_0046f980`** compares `rec.angle`, not the cursor angle, with
    `+0x308`. Its effect on mouse steering after a turn-in-place is untested.

Answered while porting (§11.2): 4 (`+0x230` is the smoothed floor + h), 10 is still open (CleriaCore has
no transition highlight), the spawn yaw's source (§4.3) is the placed heading.

---

## 11. The CleriaCore port (2026-10-06)

### 11.1 What is implemented

| Piece | Where | Provenance |
|---|---|---|
| The cursor transform and the warp (§1.1, §1.3), the windowed x quirk, `A`, `FUN_005d3be0(0, 2, ·)`, `FUN_0057b3c0` | `src/cleria/mouse.cpp` `cursor_from_client` / `client_from_cursor` / `aspect` / `pillar_x` / `pillar_inv` | [BYTES 0x5d2be8..0x5d2c55, 0x5d3be0..0x5d3e20; EXPORT FUN_005d33f0, FUN_005d62b0, FUN_005d3ab0, FUN_0057b3c0] |
| `Frame.x/y` (+0x00/+0x04), the raw ±120 wheel (last event wins) | `input_map` (`Snapshot.cursor_x/y`, `Snapshot.display`); `view_input.inl` `SdlInput` | [BYTES 0x5d2c58] |
| The cursor manager (§3): motion, mouse mode, the idle hide, the demo hide, the arrow channel, the fades and the colour trick, the clamp/scale copy (`FUN_00426e30`) | `CursorState::step` / `place`, `fade_colour` | [BYTES 0x4263c0..0x426bb6, 0x426e30..0x426f12] |
| The field arrow's angle with the 2 / 2.721 weights (§2.4) and the FUN_00473540 point | `draw_angle`, `shadow_y` | [BYTES 0x4267ab..0x426818, 0x473907..0x473962] |
| Click-to-move (§4.1): the anchor height, the angle, the 0.25 stop, the 2-tick carry, the 8 / 4-way bits from the screen centre | `CursorState::step`, `steer_anchor_y`, `steer_bits` | [BYTES 0x42681e..0x426b53] |
| The sprite (§2.2): CURSOR.DDS 36-texel crop, transposed, 23×23 at −15..+8, rotated in the field, squared at A ≠ 1 | `cursor_quad`; drawn by `BookRenderer::cursor` / `draw_cursor` (view_book.inl) | [BYTES 0x426f20..0x427297, table 0x6981e4] |
| The spawn warp (§4.3) | `spawn_cursor`; `transition` in view_game.inl | [BYTES 0x426bc0..0x426c64, 0x4340df..0x4340f2] |
| The skill switch: the wheel (/120, 15-tick cooldown), Previous / Next Skill, Skill 1–3, and the apply | `SkillSwitch` (FUN_0046cb00); the apply in the view's mover | [BYTES 0x465a56..0x465b31, 0x46cb00..0x46cc0c, 0x4652ca..0x465377] |
| The double-click lunge (§4.2): state 4, the 12-tick window, 0x112 toward the cursor, jump suppressed | `PlayerActions` (`In::mouse_mode / double_click / mouse_angle`) | [BYTES 0x465643..0x4656d6; EXPORT FUN_00465170 l.333-355] |
| The mouse-mode mover angle (`0x7325fc` instead of `+0x18`) | view_game.inl, the mover's wish | [BYTES 0x46f9fb..0x46fa12] |
| GoWndDialog (§5.1): path A (the first interactive tick in mouse mode), path B (motion, the left gate), the `X()` band, confirm = OK or click with hover, the item-get box's forced hover | `wnddialog` (`WndInput` mouse fields), `mouse::wnddialog_row`; the title list, script menus, the game-over menu, the Character-Select popups | [BYTES 0x578a0e..0x578af1, 0x578b30..0x578c22, 0x578d1d..0x578e90] |
| The pause book (§5.3/§5.4): the shared hit `FUN_005a4760` at the tip; the dispatcher's wheel debounce; hover + click-inside-selected for the tab plaques (0..4), Equip (22 tiles), Item (30 tiles), the Use popup, the Save / Load plates (+ the tabs on a click, the wheel and its warp), the Load / Save / Delete buttons, the Option rows, the Option message boxes, the sliders' drag (raw x) and their key gate, the warp map (+ the wheel and its warp), Character Select (+ the wheel and the ungated warp) | `pausebook.cpp` (`BookInput` mouse fields, `BookEvents::warp`) | [EXPORT FUN_005a68f0, 5a8030, 5aa370, 5abd10, 5b5100, 5b6eb0, 5b7cc0, 5b6220, 5ba2b0, 5b8d40, 5b9480, 5c3790; BYTES 0x5a524f..0x5a5291, 0x5a7060..0x5a7075, 0x5b5841..0x5b589c, 0x5b948c..0x5b96e7] |
| The book's open/close warp (the saved point, mode 2) | view_game.inl (`book_pt`, `tbook_pt`) | [BYTES 0x5a4bdf..0x5a4c2d] |
| The OS pointer hidden over the client while the game owns the input; shown by the shell (ImGui) | `sync_os_cursor` (view_input.inl) | [EXPORT FUN_0040efb0 WM_SETCURSOR] |

**View wiring (apps/cleria-view):** each loop (the title, the book, the game tick) steps the cursor manager
right after `input_tick`. The game tick passes a copy of the record (`pin`) that only the player's mover
reads, as FUN_004106e0 restores the record around the object ticks. The sprite is drawn after the
game-over menu and before the shell; the title draws it after its fader. Warps go through
`SDL_WarpMouseInWindow`, only while the window has the input focus [H]. `CLERIA_DRIVE` runs never read the
real mouse: a virtual pointer and buttons, driven by `mouse=X,Y` (record units), `click`, `rclick`,
`mhold` / `mrelease`, `wheel=n`, and printed by `cursor`. Tests: `cleria-mousetest` (and `cleria-inputtest`,
`cleria-wndtest`, `cleria-booktest`, `cleria-actiontest` still pass).

**Hand-chosen [H] and not modelled:**
- [H] The first cursor step takes the pointer as it is (retail's `0x73265c` starts at 0, so its first frame
  reads as motion).
- [H] `+0x230` (the 0.15-smoothed floor + h) is taken unsmoothed: the floor under the player + h.
- [H] The world → back-buffer matrix `0x796170` is the view's last frame's view-projection.
- [H] The spawn warp's θ is CleriaCore's facing heading `atan2(face_dir.z, face_dir.x)`.
- [H] The sprite is drawn after the CrossFade capture (no ghost cursor in a dissolve).
- [H] `FUN_005e4100`: the port takes `floor(v·s)` and drops D3D9's −0.5 half-pixel shift, as the book
  renderer does; its two other modes (§11.2) are not modelled.
- [H] Character Select's hover skips the bars past CleriaCore's own row limit (Toal still locked).
- [not modelled] the TA/Arena list (§5.2), the Bonus Shop (§5.4; not ported), the Topic pages' mouse, the
  page-transition highlight `0x7440e0` (its input block and its warp; CleriaCore has none), the
  `0x76b914 |= 0x20` bit and the `+0x6fc / +0x708 / 0x76c134` resets of the bracelet apply,
  `param_3 = 1` of the mouse-mode mover call, `StretchAspect`, the `ShowFPS` layer above the cursor.

### 11.2 RE findings made while porting (re-read from the bytes)

1. **`0x7325fc` first takes the centre angle.** Each tick with alpha ≠ 0, before the player angle,
   `0x7325fc = atan2(−(Y − 360), X − 512)` with **no weights** [BYTES 0x42670a..0x42679c]. The player angle
   overwrites it only when there is a controlled player, a finite projection, d² ≠ 0 and colour ≠ 0. So in a
   menu, or with no player, the double-click lunge would aim from the screen centre.
2. **The steer writes the previous `0x7325fc`** when d² = 0 or colour = 0 [BYTES 0x426a40..0x426a56]; the
   4-way table is taken whenever `0x7035c0 ≠ 8` (`cmp [0x7035c0], 8`) [BYTES 0x426ac6].
3. **`+0x230` is the 0.15-smoothed `floor + h`** (engine_arch C15, MOVER.md), so `base` is the camera's look
   point and the click-to-move anchor is **`floor + 2h − 2.0` ≈ the feet + 0.15** when grounded (§10 Q4).
4. **The spawn yaw** passed to `FUN_00426bc0` is `xmm1 = [ebp−0x10]`, the placed heading: the continue
   snapshot's `0x76a3ac` (+0x314) or the placement's own [BYTES 0x4340b6..0x4340f2].
5. **`FUN_0046cb00` (the skill request), fully:** with no bracelet (`0x76a630 == −1`) it requests 0x74 when
   owned. Otherwise the wheel's `trunc(wheel/120)` sign, or the pressed bits 0x40 (+1) / 0x80 (−1), step
   0x74..0x76 with wrap (0xff → 0x74 going up, 0x76 going down). An unowned step re-enters the loop with
   ±1, which truncates to 0 and falls to the **bits**: the keyboard keeps stepping and so skips unowned
   bracelets, but **a wheel step cannot** (it lands on the Skill-1 path: 0x74, or none). Skill 1/2/3
   (0x100 / 0x200 / 0x400) request 0x74 / 0x75 / 0x76, or −1 (= no request) if unowned. A request equal to
   the current bracelet is not made [BYTES 0x46cb00..0x46cc0c]. §4.3's "skipping unowned ones" holds for
   the keys only.
6. **The bracelet apply** [BYTES 0x4652ca..0x465377]: a request `+0x518 ≠ −1` lands when `+0x9d0` (Actable),
   or `+0x9d0 && !+0x39a && !busy`, or (`+0x6f4` a buffered attack and `+0x9d2` Cancelable); 0x74..0x76 / 0x77
   (→ 0xff) are written to `0x76a630`, the combo stage `+0x6f0` and `+0x6f4/+0x6f5` reset, SE **0xa**,
   `0x76b914 |= 0x20`; the request clears either way. It runs near the top of FUN_00465170, so a request
   lands on a later tick.
7. **`FUN_005e4100` has three modes**: with `0x765552` clear, `floor(v) − 0.5` (no scale); with it set and the
   scale `0x745fdc` > 1, `v·s` unfloored; else `floor(v·s) − 0.5` [EXPORT FUN_005e4100].
8. **The book's slider grab** tests the held 0x10000 (`+0x20`), and the value uses the RAW x with the grab
   offset `knob.x − rawX`; the keys move ±max/128 only when `!(held & 0x1000) && !mouse mode` [BYTES
   0x5b948c..0x5b96e7] (§5.4 confirmed).
