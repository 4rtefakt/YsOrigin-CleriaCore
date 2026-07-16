# Map sidecar formats

Per-room metadata living beside each map. Nine formats, one shared container,
~197 files each (197 rooms). All tiny — the whole family is well under 1 MB.

Reversed 2026-07-16. Loader module lives at `0x432000..0x435000` in
`yso_win.exe` v1.1.1.0 — the scene subsystem (`FUN_00434970` in there is "the
warp" from the Archipelago RE notes).

## The shared container ✅ 1562/1562

```c
struct Sidecar {
    char magic[4];      // "SOB\0", "SEN\0", ...  (note: .SBE files carry "SBZ\0")
    u32  zero;          // always 0
    u32  record_size;   // stride, constant per format
    u32  count;
    u8   records[count][record_size];
};
```

Predicts `filesize == 16 + record_size * count`. Tested against every file:

| Ext | Magic | Files | Stride | Count range | Loader | Meaning |
|---|---|---|---|---|---|---|
| `.SOB` | `SOB\0` | 197/197 | `0x2A8` (680) | 1..86 | `FUN_00433110` | **object placement** ✅ solved |
| `.SEN` | `SEN\0` | 197/197 | `0x25C` (604) | 0..9 | `FUN_00433a10` | **scene exits / room graph** ✅ solved |
| `.SEV` | `SEV\0` | 197/197 | `0x25C` (604) | 0..5 | `FUN_00433db0` | **event trigger volumes** ✅ solved |
| `.SCM` | `SCM\0` | 197/197 | `0x410` (1040) | 1..14 | `FUN_004338c0` | **per-room camera regions** ✅ solved |
| `.SCT` | `SCT\0` | 192/192 | `0x2B0` (688) | 1..58 | `FUN_004341a0` | **actor placement — enemies + player spawn** ✅ solved |
| `.SEF` | `SEF\0` | 197/197 | `0x5C` (92) | 0..36 | `FUN_00433360` | **scene effects (torch flames, …)** ✅ solved |
| `.SFO` | `SFO\0` | 188/188 | `0x40` (64) | 1..5 | `FUN_00433530` | **scene fog regions** ✅ solved |
| `.SBE` | `SBZ\0` | 197/197 | `0x1B0` (432) | **0..0** | — | **vestigial — every file is empty** |
| `.SNF` | `SNF\0` | 76 | `0x3C` (60) | 0 | `FUN_00434560` | scene lighting? — **the last open sidecar** |

Two notes:
- **`.ASO` (115 files) is not a sidecar** — it carries `XSR\0` magic, i.e. it's
  event-script bytecode. The existing XSO disassembler reads it as-is.
- **All 197 `.SBE`/`SBZ` files have `count == 0`.** Skip the format entirely.

The generic reader is **`FUN_00432cb0(filename, magic, ?, stride, ?)`** — every
sidecar goes through it. Files are fetched from the NNI archive as
`data\map\%s%s%s.z` (`.z` = zlib, per `ni_unpack.py`).

## SOB — object placement ✅ solved

**This is the room.** A list of models with a transform. 197 files, **1,129
records**, **388 distinct models**, and **1129/1129 records carry a `.ymo` path**.

```c
struct SobRecord {                 // 0x2A8 = 680 bytes
    char  path[0x104];   // +0x000  ymo path, MAX_PATH — 1129/1129 end in ".ymo"
    float position[3];   // +0x104  metres          (|max| 178.25)
    float rotation[3];   // +0x110  radians, Euler  (99.9% within +-2pi; 78.7% zero)
    float scale[3];      // +0x11C  98.6% exactly (1,1,1)
    char  name[0x20];    // +0x128  object name, ASCII  ("box_01", "save_01")
    u32   _[9];          // +0x148..0x16C  (loader reads 0x14C,0x150,0x154,0x158,
                         //                 0x15C,0x160,0x164,0x168,0x16C)
    u32   _[?];          // +0x188  passed as a pointer
    char  label[0x20];   // +0x1A8  display text, CP932 Japanese
    u32   _[3];          // +0x1C8, 0x1CC, 0x1D0
    char  name3[0x20];   // +0x1D4
    u32   _[2];          // +0x1F4, 0x1F8
    u32   _;             // +0x2A4
};
```

Field identification came from `FUN_00433110`'s access pattern (`puVar3` walks
from `record+0x160` in `0xAA`-dword = 680-byte steps) plus corpus statistics:

- `+0x11C` is **scale** — 98.6% exactly `(1,1,1)`, never all-zero.
- `+0x110` is **rotation in radians** — 78.7% all-zero, `|max| = 6.94 ≈ 2π`, and
  observed values land on exact angles (`-0.524` rad = **−30.0°**).
- `+0x104` is **position in metres** — `|max| = 178.25`, only 35.9% within ±2π,
  and consistent with YMO's AABBs (also metres).
- `+0x1A8` is **display text**, e.g. `save_01` → ` 1F・捜索隊拠点`
  ("1F · Search Party Base"), `save_02` → `塔・最下層` ("Tower · Lowest Level").
  Save-point names — CP932, decode accordingly.

### Worked example — `S_0000`, the first room in the game

```
data\map\s_00\s_0000\s_0000.ymo    pos( 0.00, 0.00, 0.00)  rotY=0deg  sc=1.00
data\map\mapobj\box_01\box_01.ymo  pos(-3.90, 0.00, 2.70)  rotY=0deg  sc=1.00  "box_01"
```

Two records: the room geometry at origin, and a treasure chest. The room's own
mesh is just another SOB entry — **there is no special-cased "level geometry"**,
which is convenient: one code path renders everything.

Rooms reference shared props out of `data\map\mapobj\...` (chests, save points,
switches), so `mapobj` is an instanced prop library — 388 distinct models across
1,129 placements.

## Rendering a room — the complete recipe

Everything below is now specified:

1. Read `<room>.SOB` → `count` records of `(path, position, rotation, scale)`.
2. For each, load the YMO from the NNI archive (`ni_unpack.py` cipher) →
   nodes + meshes + materials. See [YMO.md](YMO.md).
3. Compose `T * R * S` per record, multiply through the node hierarchy
   (`name[1]` first child / `name[2]` sibling / `name[3]` parent).
4. Upload each mesh's vertex buffer using its FVF (`0x1D2` = 40 B, `0x2D2` = 48 B
   with lightmap UVs) and its `D3DFMT_INDEX16` triangle-list index buffer.
5. Bind the material's 3 texture slots — plain `.DDS`, no conversion.
6. Draw opaque → alpha-test → alpha-blend (the bucket order the original's
   `qsort` comparator `FUN_005f4390` produces).

## SEN — scene exits / warp triggers ✅ solved

**This is the room graph.** Each record is a **trigger volume** that warps the
player to another scene when entered. 197 files, **367 records**, **348 directed
edges** across 176 rooms. Loader `FUN_00433a10` — the debug string names the
source file `game\g_scene.cpp`, and it builds a `RegionEntry` (vftable
`RegionEntry::vftable`) per record.

```c
struct SenRecord {                 // 0x25C = 604 bytes
    char  name[0x104];   // +0x000  trigger/region name (non-empty, no extension)
    float position[3];   // +0x104  trigger centre, world metres   (|max| 182)
    float aabb_max[3];   // +0x110  box max corner, local          (90.7% nonneg)
    float aabb_min[3];   // +0x11C  box min corner, local          (0% all-nonneg)
    char  dest[0x60+];   // +0x128  DESTINATION scene path, e.g. "S_10\\S_1000\\"
    u64   _;             // +0x22C
    u32   _;             // +0x234
    i32   angle;         // +0x23C  spawn facing, DEGREES (0,45,30,135,150,-10...)
    u32   _;             // +0x240
    i32   flag;          // +0x244  rare (set in 5/367 records)
};
```

Field evidence, from `FUN_00433a10` + corpus stats:

- `+0x128` is the **destination** — the loader copies it as a string, resolves it
  via `FUN_004358b0`, and throws a `g_scene.cpp` exception (`0x530`) if unknown.
  Values are scene directory paths (`S_10\S_1000\`); the leaf (`S_1000`) is the
  target room.
- `+0x104/+0x110/+0x11C` are the ctor args (`record-0x24/-0x18/-0xc` off the
  `+0x128` cursor). `+0x104` matches world scale (`|max| 182`, like SOB position);
  `+0x110` is 90.7% non-negative (box **max** corner) and `+0x11C` is 0%
  all-non-negative (box **min** corner) — a local AABB around the trigger.
- `+0x23C` is a **spawn facing angle in whole degrees** — the loader converts it
  to a float and range-reduces by `DAT_006bdc74` when `>= DAT_006bdba0` (a
  360/180 wrap). 340/367 are 0; the rest are exact angles (45, 30, 135…).

### The graph

`tools/sen_graph.py` builds and checks it:

```
rooms with a .SEN          : 197
rooms with >=1 exit        : 176
total directed edges       : 348
reciprocal (A->B & B->A)   : 302/348 (86.8%)
reachable from S_0000      : 126
```

86.8% reciprocity with sensible one-way exceptions (`S_0000 -> S_1000` is a
one-way drop — the intro cliff into the tower). 126 rooms reachable from the
prologue `S_0000` fits one protagonist's route. Only 3 destinations
(`S_6057/6060/6061`) lack their own SEN — dead-end cutscene/boss arenas.

This is the same adjacency backbone hand-authored in the Archipelago repo's
`room_graph_notes.md`, now available directly as data — with exact trigger
geometry the hand notes never had.

## The rest, solved

All seven loaders were found by walking callers of the generic reader
`FUN_00432cb0(name, magic, _, stride, flag)` — each call site pushes its magic and
stride as immediates, so the mapping is unambiguous.

**The engine has ONE "region" concept** — a named, optionally Z-rotated AABB —
shared by fog and cameras. Its RTTI names survive in `.rdata`: `RegionBase`,
`RegionCamera`, `RegionFog`, `RegionEntry`. `FUN_0042e040` is the shared
containment test: each frame it tests the **player** against every region, first
hit wins, else record 0, and changes **blend** rather than cut.

### SEF — scene effects ✅ (`FUN_00433360`)
```c
struct SefRecord {        // 0x5C
    u32   effect_id;      // +0x00 -> eftlist
    float position[3];    // +0x04 world
    float rotation[3];    // +0x10 DEGREES (the loader multiplies by pi/180)
    u8    dead[0x40];     // +0x1C never read (0/1603 non-zero corpus-wide)
};
```
`effect_id` resolves via `data\effect\eft\eftlist{,2,3}.dat.z`. **id 200 =
`map\syokudai_honou.eft`** (燭台の炎, the torch flame; 471 placements). Note the
extension differs on disk: eftlist says `.eft`, the archive holds `.EF2.Z`.
See `build/ef2_re/EF2_FORMAT.md`.

### SFO — scene fog ✅ (`FUN_00433530`, consumer `FUN_0042e040`)
```c
struct SfoRecord {        // 0x40
    float origin[3];      // +0x00
    float aabb_a[3];      // +0x0C  RELATIVE to origin (the loader sorts a/b -> min/max)
    float aabb_b[3];      // +0x18
    float fog_near;       // +0x24
    float fog_far;        // +0x28
    u32   fog_colour;     // +0x2C  memory bytes are B,G,R,0xFF — NOT RGBA
    u32   sticky;         // +0x30
};
```
Record 0 is the room default; 1..15 are sub-regions. Region changes cross-fade at
**0.02/frame** (~0.83 s). Corpus (223 records): min≤max 100%, near<far 100%.

### SCM — camera regions ✅ (`FUN_004338c0` → `FUN_0042b4f0`)
Each 1040-byte record is **two `0x1D4` `CameraPoint`s** — **A = the look-at target,
B = the eye** — then the region header. `2 × 0x1D4 = 0x3A8`, exactly where the
header starts; nothing is left over.

| CameraPoint | field |
|---|---|
| `+0x34` | **mode**: 0 = fixed, 1 = follow player, 3 = orbit |
| `+0x38` / `+0x3c` / `+0x40` | **yaw / pitch / distance** (radians, radians, units) |
| `+0x44` | auto-yaw flags |
| `+0x54` | fixed position (mode 0; all-zero corpus-wide) |

| record | field |
|---|---|
| `+0x3B8` | **FOV**, vertical, radians |
| `+0x3BC` | blend speed |
| `+0x3C0` | name, **CP932** |
| `+0x3E0`/`+0x3EC`/`+0x3F8`/`+0x408` | aabb A / aabb B / origin / region yaw |

Orbit math (`FUN_0042b6e0`): `a = -π/2 - yaw`, then
`eye = target + (-cos(a)·d·cos(pitch), sin(pitch)·d, sin(a)·d·cos(pitch))`.
**The modal room camera is FOV 30°, pitch 30°, distance 30** — 99%+ of authored
values are exact whole numbers. Region names are self-documenting: `基本カメラ`
("basic camera"), `イベント用アップ` ("close-up"), `イベント用遠景` ("distant view").

### SCT — actor placement ✅ (`FUN_004341a0`)
An **instance** list, not a cast list: one record per placed actor, so a room
repeats `CAST_M220` once per monster.
```c
struct SctRecord {        // 0x2B0
    char  name[0x104];    // +0x000  read to the FIRST NUL (see below)
    float yaw_degrees;    // +0x108  360 or -1 = random facing
    float position[3];    // +0x10C
};
```
The consumer walks records from `base + 0x108`, which is why the useful fields sit
late. **`CAST_ADOL` is the player's spawn point.** Chain:
`SCT --name--> MISC/CASTINFO.DAT (186 actor types) --cast_id--> MISC/ENEMYINFO.DAT
(90 enemy tunings)`. Sprites resolve by convention (`CAST_M100 → ANI/M_100.AIA`,
89/90). Corpus: 1290 records, **1106 enemy placements across 85/192 rooms**.

> **US localisation bug:** English names are `memcpy`'d over longer Shift-JIS
> strings without clearing the tail, so `CAST_M220`'s display name is literally
> `"Chryolos\0X"`. Always read to the first NUL.

### SEV — event trigger volumes ✅
Byte-identical layout to SEN, but `+0x128` holds an **XSO script name** where SEN
holds a destination scene: *"enter this volume → run this script"*
(`s_stop01` → `S_STOP1000_01.XSO`). 62 records corpus-wide.

## Still open

- **SNF** (`SNF\0`, stride `0x3C`, 76 files, `FUN_00434560`) — believed to be scene
  lighting; being reverse-engineered.
- SOB's remaining scalars at `0x148`..`0x16C`, `0x188`, `0x1C8`..`0x1D0`,
  `0x1D4`, `0x1F4`, `0x1F8`, `0x2A4`.
- SCT beyond name/yaw/position; `+0x104` is provably never read yet holds 165
  non-zero, meaningful-looking values.
- The ~400 remaining bytes of each `CameraPoint`, and SCM `+0x3AC`/`+0x3B0`/`+0x3B4`.

## Tools

| Tool | Does |
|---|---|
| `tools/verify_container.py` | validates the container across all 1562 files |
| `tools/sob_probe.py` | decodes SOB placement records |
| `tools/sen_probe.py` | decodes SEN warp-trigger records |
| `tools/sen_graph.py` | builds + validates the room adjacency graph |
| `tools/DumpSidecars.java` | Ghidra: decompile SOB/SEN/SCM loaders + module map |
