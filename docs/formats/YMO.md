# YMO — Ys Origin model format

**Status: fully reverse-engineered.** A complete parser consumes all **1,006**
files byte-exactly — every byte accounted for, nothing left over, nothing
overrun. See [Validation](#validation).

Falcom model container. 1,006 files in `data.na`, 61 MB packed / 354 MB raw.
Totals across the corpus: **6,119,287 vertices, 2,280,027 triangles**.

Reversed 2026-07-16 from two independent sources that agree everywhere:

- **The loader** — `FUN_005f31d0` in `yso_win.exe` v1.1.1.0, found by scanning
  610,640 instructions for `CMP dword ptr [EDI],0x4f4d59` @ `0x005f322e`.
  Ghidra project `D:\ghidra-work\YsOproj`.
- **Corpus statistics** over all 1,006 files. Every ✅ below was tested against
  the whole corpus, not a sample.

Origin is **version 17** of the format Ys VI / Oath in Felghana use (Noesis
treats them as one versioned handler). Expect a shared parser across the 3D
trilogy.

## What YMO actually is

**YMO is a serialized DirectX `.X` mesh hierarchy.** Everything in it maps 1:1
onto the D3DX data model, which is fully documented by Microsoft:

| YMO section | D3DX equivalent |
|---|---|
| node | **`D3DXFRAME`** — `Name`, `TransformationMatrix`, `pFrameFirstChild`, `pFrameSibling` |
| mesh | **`D3DXMESHCONTAINER`** — FVF vertex buffer + `D3DFMT_INDEX16` index buffer |
| animation clip | **`ID3DXKeyframedAnimationSet`** — per-node scale/rotation/translation key arrays |

The node names give it away: `Frame_SCENE_ROOT`, `Frame1_hit_efe01_Layer1`,
`Mesh_hit_efe01`, `AnimationSet_hito_efe` — verbatim
`D3DXLoadMeshHierarchyFromX` conventions, out of a 3ds Max exporter.

Falcom built models as `.X` files and serialized the loaded D3DX hierarchy to
disk. The pointer graph became name references (resolved on load by `strcmp`),
and the D3DX structs became fixed-stride records — with **one deviation**: their
keyframes put the *value first and time last*, where `D3DXKEY_VECTOR3` /
`D3DXKEY_QUATERNION` put `Time` first. Everything else is D3DX.

### The three consequences that matter

1. **There is no custom vertex format.** Raw D3D9 vertex buffers + a standard
   **FVF** code; index buffers as `D3DFMT_INDEX16`. The loader hands both to D3D9
   untransformed. The layout is Microsoft's FVF documentation — nothing to reverse.
2. **The FVF path is rigid; the vertex-declaration path is GPU-skinned.** No FVF
   in the corpus carries `XYZB*` blend weights — `0x1d2`/`0x2d2`/`0x1c2` all mask
   to `D3DFVF_XYZ` — so FVF geometry binds **rigidly** to nodes: walk the
   hierarchy, compute world matrices, draw. But that is a statement about the FVF
   path only. **Ys Origin does skin**, via D3DX indexed-palette blending, and the
   weights *are* in the YMO — they sit in a second vertex buffer that the FVF path
   never uses. See [Skinning](#skinning) below.
3. **The remaining unclassified fields are almost certainly D3DX struct members.**
   When in doubt, check the D3DX headers before reaching for Ghidra.

## On-disk layout

Sections appear in the order the loader consumes them, which is **not** header
field order:

| # | Section | Count field | Record size |
|---|---|---|---|
| 0 | header | — | `0x28C` (652 B) |
| 1 | materials | `+0x10` | `0x2EC` (748 B) |
| 2 | nodes | `+0x1C` | `0x2A0` (672 B) |
| 3 | meshes | `+0x18` | variable |
| 4 | animation clips | `+0x14` | variable |

### Header — 652 bytes (`0xA3` dwords)

The loader copies `0xA3` dwords wholesale before parsing, which fixes the size
exactly: `for (i = 0xa3; i != 0; i--) *dst++ = *src++;`

| Offset | Type | Field | Evidence |
|---|---|---|---|
| `0x00` | char[4] | magic `"YMO\0"` | loader `== 0x4f4d59` ✅ 1006/1006 |
| `0x04` | u32 | **version = `0x11`** | loader `== 0x11` ✅ 1006/1006 |
| `0x08` | u32 | flags | 17 distinct, `0..2049` |
| `0x0C` | u32 | flags2 | only `0`, `1`, `16` |
| `0x10` | u32 | **material_count** | `1..260` |
| `0x14` | u32 | **anim_clip_count** | `0..39` — zero in 666/1006 (static geometry) |
| `0x18` | u32 | **mesh_count** | `1..121` |
| `0x1C` | u32 | **node_count** | `2..305` |
| `0x20` | u32 | zero | ✅ 1006/1006 |
| `0x24` | float[3] | `aabb_min` | ✅ 1006/1006 |
| `0x30` | float[3] | `aabb_max` | ✅ 1006/1006 |
| `0x3C` | float[3] | `center` == `(min+max)/2` | ✅ **derived**, 1006/1006 |
| `0x48` | float | `radius` == `\|max-center\|` | ✅ **derived**, 1006/1006 |

`center`/`radius` are derived quantities, so 1006/1006 is proof, not curve-fit.
The loader independently copies dwords `[9]..[0x12]` (= `0x24`..`0x48`, exactly
these 10 floats) to runtime `[0xb2]..[0xbb]`.

Units are metres — `AEF00_02.YMO` spans min `(-4.38,-0.40,-4.38)` → max
`(4.38,0.20,4.38)`: a floor panel ~8.8 m across, 0.6 m thick.

### 1. Materials — `0x2EC` = 748 bytes (`0xBB` dwords), runtime stride `0x2b0`

- `+0x00` u32 flags — bit `0x200000` sets header flags bit `0x200`
- `+0x4C` NUL-terminated **material name**
- `+0xAC` **3 × texture name**, stride `0x30` (48 B) — plain `.DDS`, no RE needed
- runtime `+0x3C` initialised to `1.0f` (alpha)

### 2. Nodes — `0x2A0` = 672 bytes, runtime stride `0x250` (+ `0x170` pose), 16-B aligned

Sub-parser **`FUN_005f3c70`**. This is the **scene graph**, serialized by name.

This is `D3DXFRAME`, with the pointer graph serialized as names.

| Offset | Type | Field | Confirmed |
|---|---|---|---|
| `0x000` | char[0x60] | `name[0]` — **own identity** | — |
| `0x060` | char[0x60] | `name[1]` — **first child** → node* @ rt `+0x54` | ✅ 3663/3663 |
| `0x0C0` | char[0x60] | `name[2]` — **next sibling** → node* @ rt `+0x58` | ✅ 2347/2347 |
| `0x120` | char[0x60] | `name[3]` — **parent** → node* @ rt `+0x5c` | ✅ 5670/5670 |
| `0x180` | char[0x60] | `name[4]` — **mesh name** → mesh* @ rt `+0x60` | — |
| `0x1E0` | float[16] | matrix A — the **LOCAL transform** (rest pose): `== compose(S,R,T)` below in **5832/7016** nodes. **Walk the hierarchy with this one**; animation replaces it per node per frame |
| `0x220` | float[16] | matrix B — the **bone offset / inverse-bind matrix**. Used *only* by the skinning palette (`B_i @ world_i`). **Identity in ~98.5% of room nodes**, which is why ignoring it costs rooms nothing |
| `0x260` | float[3] | **scale** |
| `0x26C` | float[3] | **translation** |
| `0x278` | float[4] | rotation quaternion |
| `0x288` | u32 | *(unclassified)* |
| `0x28C` | u32 | *(unclassified)* |
| `0x290` | u32 | *(unclassified)* |
| `0x294` | — | 12 bytes unread (padding) |

The TRS triple at `0x260` is copied **twice** into the pose struct — rest pose +
current pose. Runtime defaults: identity 4×4 at pose `[0..0xf]` and `[0x20..0x2f]`
(`1.0f` on the diagonals), and **`parent_index = 0xFFFFFFFF`** at `[0x42]`.

> **The scale/translation labels above were swapped in earlier revisions of this
> doc, and the fix is not cosmetic — it silently swaps a node's offset with its
> size.** The discriminating test is to compose the triple and compare it against
> the node's own **matrix A** (`0x1E0`) and compare. Over all 1006 files / 7016
> unit-quaternion nodes:
>
> | labelling | reproduces matrix A (`0x1E0`) | matrix B (`0x220`) |
> |---|---|---|
> | `0x260 = translation, 0x26C = scale` (old doc) | **0** (0.00%) | **0** (0.00%) |
> | `0x260 = scale, 0x26C = translation` | **5832** (83.12%) | 2490 (35.49%) |
>
> Zero versus 5832 settles the labels, and it identifies **matrix A as the
> TRS-composed rest pose**. Two independent passes agree (the other scored
> 5834/7016; the two-node gap is quaternion edge cases). The order is D3DX's own
> `RegisterAnimationSRTKeys` — **S, R, T**.
>
> **A is the local transform; B is the bone offset. They are unrelated, and no
> algebraic identity between them should be expected** — B is baked from the skin
> cluster's *bind* pose, which is not the node's *rest* pose.
>
> **⚠️ Two statistics in an earlier revision of this note were artifacts. Don't
> repeat them:**
> - "`A == B` in 34.9%" is real but meaningless: it holds **only when both are
>   identity**. In `MAP/`, `A == B` = 1912 and `A == identity` = 1912 — the same
>   nodes. Among nodes with a non-identity A, `A == B` holds for 1 of 4565.
> - "`B == inv(A)` in just 27 (0.4%)" was a **bug in the measuring script**, which
>   computed the inverse test only in an `elif` after the equality test, so every
>   `A == B` node was silently excluded. Measured properly it is **68.3%** in `MAP/`
>   — again the identity set. It is also the wrong test: A is *local*, so `inv(A)`
>   is the bone offset only for **root** nodes.
>
> **`B` is identity in ~98.5% of room nodes** (100% of purely static files). So a
> renderer that "walks `0x220` as the local matrix" through a room is applying
> identity to every node — it looks correct because room vertices are already baked
> to world space, not because `0x220` is the local transform. That test would pass
> with any constant matrix, which is exactly why it misled us.

**Name resolution** happens in a post-pass via **`FUN_005f4e10(this, name)`**,
which is a **node-lookup-by-name**, not a texture resolver:

```c
// count at this+0x1c (= header node_count), array at this+0x290, stride 0x250
if (strcmp(name, node_array[i] + 0x64) == 0)      // node.name[0]
    return i * 0x250 + node_array;
```

`name[4]` is instead `strcmp`'d against **mesh** names (`mesh+0x68`, stride
`0xcc`); on match the loader writes `node+0x60 = mesh*` **and** `mesh+0x2c =
node_index` — a back-pointer proving each mesh has exactly **one** node. That is
rigid binding, confirmed structurally.

### 3. Meshes — header `0xB4` = 180 bytes, runtime stride `0xCC`

Sub-parser **`FUN_005f4420`**. Allocated via `_eh_vector_constructor_iterator_`
(ctor `FUN_005f2490`) — a real C++ class with a dword count prefix.

| Offset | Field |
|---|---|
| `0x00` | NUL-terminated **mesh name** (runtime `+0x68`) |
| `0x60`..`0x84` | 10 floats (bounds, same shape as the model header) |
| `0x88` | submesh count |
| `0x8C` | **vertex_buffer_count** |
| `0x90` | **index_buffer_count** |
| `0x94` | per-submesh index-list length (dwords) |
| `0x98` | *(unclassified)* |
| `0x9C` | **node-binding count** — `0x60`-byte name records, each → node* via `FUN_005f4e10` |
| `0xA0` | copied into the parallel `0x18`-stride array |

Consumed in order: header → `n9C × 0x60` node-binding names → `n88 ×
(0x20 + n94*4)` submesh descs + index lists → VB descs + payloads → IB descs +
payloads.

#### Vertex buffers — `0x14` = 20-byte descriptors, payload follows the array

```c
count = desc[0x08]; stride = desc[0x0C]; fvf = desc[0x10];
device->CreateVertexBuffer(stride*count, usage, fvf, pool, &desc[0x00], 0); // vtbl +0x68
vb->Lock(0, size, &p, 0); memcpy(p, payload, size); vb->Unlock();           // vtbl +0x2c/+0x30
```

| FVF | Stride | Layout | Descs | Verts |
|---|---|---|---|---|
| `0x1D2` | 40 | `POSITION@0 NORMAL@12 DIFFUSE@24 SPECULAR@28 TEX0@32` | 1595 | props |
| `0x2D2` | 48 | …plus `TEX1@40` (2nd UV, **unused — all zero**) | 265 | map geometry |
| `0x0` | 16/20/28 | **vertex declaration** (shader path — see [Skinning](#skinning)) | 132 | — |

`0x1C2` (stride 28, no normal) is handled by the loader but appears **zero**
times in Origin — a Felghana/Ys VI leftover. Support defensively.

> **Baked lighting lives in the DIFFUSE vertex colour (`@24`), not a lightmap.**
> The `0x2D2` second UV set is entirely zero across the corpus — there is no
> lightmap texture. Lighting is per-vertex D3DCOLOR (`0xAARRGGBB`), applied
> fixed-function MODULATE (`final = texture × diffuse`). Verified: S_3004's
> lava-adjacent verts carry `f5b547` (orange), S_1000's carry cool greys. This
> is what makes rooms read as *lit*; a flat-shaded render looks wrong.

#### Skinning

The `FVF == 0` row above is **not** a rigid path. Shader-bytecode analysis (see
`build/render_re/`) shows Ys Origin has a real GPU skinning path, on a
**`c24` bone-matrix palette**, distinct from the fixed-function FVF path:

- **40 `S_*` shaders declare skinning inputs.** All 40 `dcl_blendindices v1` and
  all 40 index the palette with `m4x3 r4.xyz, v4, c24[a0.x]`. 30 of the 40 also
  `dcl_blendweight v0` — exactly the 2W/3W/4W variants; the 10 `*_1W` shaders omit
  it, since a single bone needs no weight. [C-DIS] 40/40, 30/40 over
  `build/render_re/listings/`
- **Bone index → palette address:** `mul r1, v1.zyxw, c2.w` / `mov a0.x, r1.x`.
  The `.zyxw` un-swizzles a `D3DCOLOR`-declared BGRA index; `c2.w` is the
  stride scale. [C-DIS] · exact `c2.w` value is [UNKNOWN] from the assets — it
  depends on the `BLENDINDICES` element type, which lives in the exe.
- **3 registers per bone** (`m4x3`), so the palette is `c24 + 3*bone`. On the
  unskinned `B_*` shaders the same `c24` block is instead the plain `WORLD*VIEW`
  matrix. [H-high] — see `build/render_re/constant_map.txt` for the full register
  map, incl. why `c24` is `WORLD*VIEW` and not `WORLD`.
- **Weights:** 2W/3W/4W each add one weighted bone (`mad r4, r0.xyzz, <weight>,
  r4.xyzz`), with the **last weight implied as `1 - sum(previous)`** — the shader
  computes it as `dp3 r1.w, v0.xyzz, c0.zxxx` then `add r1.w, -r1.w, c0.z`.
  Normals are skinned in parallel via `m3x3` and renormalised. [C-DIS]
- **Bone count is the vertex-declaration id.** The exe's shader-name table
  (`0x746250`..`0x7465D3`, dumped to `build/render_re/shader_name_table.txt`)
  tags each shader with a `kind`; for the `s_*` entries **`kind == 3 + bone
  count`** (1W→4 … 4W→7), and the `b_*` entries take kinds 0..3. [C-DIS]

**Resolved — and the multi-stream deduction above was right.** [C-DIS] The
declarations are **not in the YMO**: they are **8 static `D3DVERTEXELEMENT9`
arrays in the exe**, created by the loop at `0x5ee1d0` (`CreateVertexDeclaration`,
`esi < 0x20` step 4 ⇒ exactly 8) and selected through a 12-byte, 76-slot table at
`0x746240` — `{ IDirect3DVertexShader9*; char* name; u32 decl_index }`. All 36
`s_*_Nw` entries obey **`decl = 3 + N`**; `decl[0..3]` mirror the legacy FVFs
(strides 16 / 28 / 40 = `0x1D2` / 48 = `0x2D2`). Dumps in `build/ymo_re/`.

`decl[7]` — the one BOSS01 uses — is **two-stream**, and `VB[i]` binds to stream
*i*:

| Stream | Offset | Type | Usage |
|---|---|---|---|
| **0** (stride 28) | 0 | FLOAT3 | BLENDWEIGHT0 |
| 0 | 12 | D3DCOLOR | BLENDINDICES0 |
| 0 | 16 | D3DCOLOR | COLOR0 |
| 0 | 20 | FLOAT2 | TEXCOORD0 |
| **1** (stride 60) | 0 | FLOAT3 | POSITION0 |
| 1 | 12 | FLOAT3 | NORMAL0 |
| 1 | 24/36/48 | FLOAT3 | TANGENT0/1/2 |

> **Two traps.** (1) **Stream 0 has no position.** Reading its first 12 bytes as
> POSITION yields the blend *weights* — they span `(0.311,0,0)..(1.0,0.5,0.282)`,
> which looks exactly like a tiny model at the origin and is why BOSS01 rendered
> as a sub-unit blob. Its real bounds are ±15.1 in X; nothing needs rescaling.
> UV and COLOR0 live in stream 0, which is why UVs fell back to `(0,0)`.
> (2) **`FVF == 0` + `stride 28` does not imply skinned** — `decl[1]` is also
> stride 28 and unskinned. The discriminator is **two VBs with stream 1 at stride
> 60**, or equivalently `mesh + 0x98` (influence count) being nonzero.

Skinning is textbook D3DX **indexed palette** blending: the 32-byte submesh record
is a serialized `D3DXBONECOMBINATION` `{ AttribId, FaceStart, FaceCount,
VertexStart, VertexCount, DWORD* BoneId, 0, 0 }` (field 5 is a leftover heap
pointer — nonzero 43/43 on BOSS01, NULL 0/23 on room `S_1000`). `mesh + 0x94` is
the palette size (max **24**, exactly vs_1_1's constant budget); `mesh + 0x98` the
influence count 1..4, which maps 1:1 to stream 0's stride {16,20,24,28} in **66/66**
skinned meshes and is 0 in all 1860 unskinned ones; `mesh + 0x9C` the bone-name
list. **BLENDINDICES are palette slots, not bone ids** — resolve via `BoneId[slot]`.
The inverse-bind matrix **is** node matrix B (`0x220`), stored rather than derived.
Ignore TANGENT0/1/2 — **0 of 40 skinning shaders read them**.

> **This was briefly contested and is now settled: A (`0x1E0`) = local, B (`0x220`)
> = inverse-bind.** The objection was that `0x220` maps to node runtime `[0..0xf]`,
> which by position in a `D3DXFRAME` is `TransformationMatrix` — so it "couldn't"
> also be the offset matrix. That reasoning was wrong twice over: the statistics
> behind it were artifacts (see the node table above), and **`B` is identity in
> ~98.5% of room nodes**, so a room renderer walking `0x220` is applying identity
> and cannot distinguish the two readings at all.
>
> The decisive evidence is `build/ymo_re/corpus_validate.{py,log}`, which encodes
> exactly this model — `OFF = mB`, `restA = mA`, `W[i] = loc[i] @ W[parent]`,
> `F = OFF @ W` — and reproduces **303 stored clip AABBs across 59/64 skinned files
> to ≤1e-5**. That is only possible if B is the inverse-bind and A is the local
> transform. It also agrees with `A == compose(S,R,T)` at 83.12%: composing to a TRS
> is the signature of a local transform.
>
> **Consequence — a real latent bug:** a renderer that walks `0x220` gets every
> *skinned* model wrong. Rooms are unaffected only because `B ≡ I` there.
> (Outliers, separate `[UNKNOWN]`: `BOSS01_`/`BOSS01_SHADOW_` Δ11.4, `BOSS06A/B`
> Δ1.3–1.6, `PIC_TOGE` Δ0.93 — likely a second animation path, since 59/64 are
> exact.)

Corpus proof: `v' = Σ wᵢ · (v · B[bᵢ] · World[bᵢ])` with `w₃ = 1 − w₀ − w₁ − w₂`
reproduces the stored clip AABBs. Two independent implementations agree — a Python
pass (`build/ymo_re/corpus_validate.py`) and CleriaCore's own
(`cleria-animtest assets`, which poses and skins through `Model::pose` /
`Model::skin_point`):

    skinned files: 64   clips checked: 303   reproduced (≤1e-3): 270

The 33 misses are confined to **5 files**, the same 5 in both implementations and at
the same magnitudes: `BOSS06A`/`BOSS06B` (15 clips each, Δ1.32–1.58), `BOSS01_` and
`BOSS01_SHADOW_` (Δ11.4084 — Python: 11.408385147045454), `PIC_TOGE` (Δ0.93). Agreeing
to five significant figures on the *outliers* is what makes them a property of those
files rather than of either implementation. They likely use the `type != 0` track
path, which is [UNKNOWN].

Note the tag vocabulary here (`[C-DIS]` disassembled, `[C-COR]` corpus-validated,
`[H-*]` hypothesis, `[UNKNOWN]`) is `constant_map.txt`'s; the ✅ *n*/*n* marks
elsewhere in this doc are the corpus-statistics equivalent.

#### Index buffers — `0x14` = 20-byte descriptors

```c
device->CreateIndexBuffer(stride*count, usage, 0x65, pool, &desc[0x00], 0);  // vtbl +0x6c
```

`0x65` = 101 = **`D3DFMT_INDEX16`**, hardcoded. ✅ **All 1,926 index buffers in
the corpus have stride 2.** Topology is **triangle list** — the centroid pass
walks `index_count/3` triangles reading indices as `ushort*` in steps of 3 and
looks up position at vertex offset 0, independently confirming 16-bit indices,
triangle lists, and `POSITION@0`.

### 4. Animation clips — `+0x14`, variable

Clip header is `0x28` dwords = **160 bytes**, with `track_count` at dword `0x27`
(byte `0x9C`) and a name + ~14 floats. Each track (sub-parser **`FUN_005f4110`**,
runtime stride `0x84`):

| Offset | Field |
|---|---|
| `0x00` | char[0x60] **node name** → node* via `FUN_005f4e10` |
| `0x60` | `countA` |
| `0x64` | `countB` |
| `0x68` | `countC` |
| `0x6C` | `type` |
| `0x70` | end of track header |

Then, **if `type == 0`**, three keyframe arrays follow, in this on-disk order:

| Array | Count | Record | Layout | Confirmed |
|---|---|---|---|---|
| **A** | `+0x60` | `0x10` | `{ float3 scale; u32 frame; }` | ✅ first key == node `0x260` in **93.7%** (vs 0.2% for the other field) |
| **B** | `+0x64` | `0x14` | `{ float4 quaternion; u32 frame; }` | ✅ **100.00% unit length** — 865,462/865,462 |
| **C** | `+0x68` | `0x10` | `{ float3 translation; u32 frame; }` | ✅ first key == node `0x26C` in **79.5%** (vs 0% for the other field) |

This is **S, R, T** — the same order as D3DX's `RegisterAnimationSRTKeys`.

> **Two corrections from earlier revisions.** (1) A and C were labelled
> *translation* and *scale* respectively. The percentages above were always right;
> only the names were wrong, because they were stated against the node fields at
> `0x260`/`0x26C`, which were themselves swapped (see the node table above). A's
> first key matches `0x260`, and `0x260` is **scale**.
> (2) **The trailing field is a `u32` frame number, not a `float` time.** Read as a
> float it comes out as a denormal — frame 220 decodes to `3.08e-43` — which is why
> the "monotonically non-decreasing" test below passed anyway: denormal ordering
> happens to agree with integer ordering, so the test could not tell the two apart.
> Clip headers bound it: `frame_start` @`0x88` and `frame_end` @`0x8C` are `u32`
> (BOSS01's `WAIT` is frames 220–339). At 60 ticks/sec, frame *n* is *n*/60 s of
> scaled time.

**Value first, frame last** — this is the one place Falcom deviates from D3DX
(`D3DXKEY_VECTOR3`/`D3DXKEY_QUATERNION` put `Time` first). The field is
monotonically non-decreasing in **28157/28157** and **28158/28158** tracks
respectively; assuming it comes first only reaches 95.3% / 91.6%, and leaves the
quaternion only 44.67% unit. Do not port the D3DX field order.

Sub-100% match rates on A and C are expected and not a problem: the first
keyframe need not equal the bind pose. The quaternion's 100.00% unit-length
across 865k keys is the load-bearing proof for B.

**If `type != 0`**: `type × 0x50` bytes, 16-byte aligned (80 B = a 4×4 matrix +
4 floats — presumably baked matrix keys). **`type` is 0 for all 28,161 tracks in
the corpus**, so this branch is unreachable in Ys Origin. Ignore it unless
Felghana/Ys VI need it.

Each track targets a node by name — clips animate the scene graph, and meshes
ride their node rigidly. `anim_clip_count == 0` for **666 of 1,006** models,
exactly the static map geometry. Characters and enemies carry clips.
Corpus totals: **28,161 tracks**, 1,075,970 translation / 1,078,939 rotation /
1,078,643 scale keys.

## Notes for the CleriaCore renderer

- **Do not port the specular halving.** When `DAT_0076519a >= 2 &&
  DAT_00764c2f`, the loader rewrites specular in-place as `(c >> 1) & 0x7f7f7f`
  at offset `0x10` (FVF `0x1c2`) or `0x1c` (`0x1d2`/`0x2d2`). It compensates for
  fixed-function hardware you will not have.
- **Per-triangle centroids** are precomputed as `(v0+v1+v2)/3` when device flags
  `0x2fc & 0xe == 0xe`, for transparency depth-sorting. We can do better.
- **Draw order** is already solved: submeshes are `qsort`ed by comparator
  `FUN_005f4390` into 4 buckets keyed on material flags `0x4000`, `0x8040`,
  `0x10` and alpha `< 1.0` — i.e. opaque → alpha-test → alpha-blend.
- Node links are `D3DXFRAME`'s: `name[1]` first child, `name[2]` next sibling,
  `name[3]` parent — all ✅ 100% across the corpus. Traverse child/sibling from a
  root; `name[3]` is redundant but load-bearing for the loader's own resolution.

## Validation

`tools/ymo_parse.py` implements every stride above and parses the corpus:

```
corpus            : 1006 files
parsed byte-exact : 1006/1006  (100.00%)
failures          : 0
totals            : 6,119,287 vertices, 2,280,027 triangles
FVF               : {0x1d2: 1595, 0x2d2: 265, 0x0: 132}
index strides     : {2: 1926}
```

666 of those have `anim_clip_count == 0` and land precisely on EOF after
header + materials + nodes + meshes — the strongest single check, since nothing
downstream can absorb drift.

Independently: an FVF decode done from the D3D9 spec alone reproduces the
loader's three hardcoded specular offsets (`0x10`, `0x1c`, `0x1c`) exactly —
see `tools/ymo_fvf.py`.

## Remaining unknowns

1. Node `+0x288` (int, always 6) / `+0x28C` (**float**, ~0.2–0.6) / `+0x290`; mesh
   `+0xA0`. Check the D3DX headers first — these are likely `D3DXFRAME` /
   `D3DXMESHCONTAINER` members. *(`mesh +0x94` and `+0x98` are no longer unknown:
   they are the palette size and the influence count — see [Skinning](#skinning).)*
2. Which of the three node links the loader actually walks at runtime (all three
   are present and consistent; child+sibling alone is sufficient to rebuild).
3. The 5 clip-AABB outliers (`BOSS01_`, `BOSS01_SHADOW_`, `BOSS06A/B`, `PIC_TOGE`) —
   likely a second animation path rather than matrix semantics, since 59/64 skinned
   files reproduce exactly. *(The "which matrix is the bone offset" question that sat
   here is resolved: A = local, B = inverse-bind. See [Skinning](#skinning).)*
4. `S_SHDW_1W`..`S_SHDW_4W` exist as `.VSO` files but are **absent from the exe's
   76-entry shader table**, so they are never assigned a declaration id there.
   Something else registers them; that mechanism is unfound. (This is also why the
   shader counts differ legitimately: **40** `S_*.VSO` files on disk vs **36** `s_*`
   entries in the name table — the four `SHDW` variants are the difference.)
5. Which shader *family* (`base` / `cvr` / `cube` / `sphr` / `mmbrn` / `aniso` /
   `dahmex`) a given material selects.
6. `c2.w` = 765 (`255*3`, the D3DCOLOR expansion) is **[H-high] by derivation** — no
   such literal exists in the exe; it is computed.

> **Resolved since this list was first written**, kept here because the old entries
> were cited elsewhere: the vertex declarations for the `FVF == 0` buffers are **not
> in the YMO** — they are 8 static `D3DVERTEXELEMENT9` arrays in the exe, selected by
> a 76-entry table, with `decl = 3 + N` for the `_Nw` shaders. The per-stream layout
> is in [Skinning](#skinning). The old framing ("needs the VS_DX9 shaders") was right
> that the shaders were necessary, but the answer turned out to live in `.data`, not
> in the bytecode.

## Tools

| Tool | Does |
|---|---|
| `tools/ymo_parse.py` | full parser + the 1006/1006 byte-exact validation |
| `tools/ymo_fvf.py` | independent FVF decode; reproduces the loader's specular offsets |
| `tools/ymo_anim.py` | keyframe layout hypothesis tests (unit-quaternion, monotonic time) |
| `tools/ymo_links.py` | node link semantics + keyframe identity vs rest TRS |
| `tools/FindYmoLoader.java` | Ghidra: locate loaders by Falcom magic |
| `tools/DumpYmoMesh.java` | Ghidra: decompile mesh/node sub-parsers |
| `tools/DumpYmoS5.java` | Ghidra: decompile animation + name-resolver |

Ghidra 12 dropped Jython — headless scripts must be **Java**, not Python:

```sh
analyzeHeadless D:/ghidra-work YsOproj -process yso_win.exe -noanalysis \
  -scriptPath D:/ghidra-work/scripts2 -postScript FindYmoLoader.java
```
