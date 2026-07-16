# XSO — Ys Origin event-script bytecode + VM

The event system: 2,225 scripts driving chests, doors, item grants, cutscenes,
and scene flow. Reverse-engineered against the game's interpreter and validated
bit-exact against the sibling project's proven static extractor.

Interpreters: **`FUN_004472e0`** (effect/entity consumer) and **`FUN_00566350`**
(the event VM — flag grants, item gives, dialog; grant store at `0x00567D17`).
The bytecode *format* and class-level control flow are shared; the two differ
only in which class-2 sub-ops they implement.

## Container

```
+0x00  "XSR\0"
+0x1C  u32  code_words          (code length in 32-bit words)
+0x24  code[code_words]         (int32 words)
       label table follows (not needed to execute)
```

## Instruction encoding

`class = word >> 24`. Per-class length in **words**:

| class | len | meaning |
|---|---|---|
| `0` | 1 | nop |
| `1` | 1 | **end / return** |
| `2` | *computed* | **function call** (sub-op dispatch) — see below |
| `3`, `0xB` | 2 | unconditional jump; target = `code[pc+1]` |
| `4` | 2 | (reg op; treated as 2-word nop by the event VM) |
| `5`–`0xA` | 2 | **conditional jump** on the accumulator |
| `0xC`, `0xD` | 1 | `acc = 0` |
| `0xE` | 2 | `acc = imm` |
| `0xF`–`0x13` | 2 | `acc {+,-,&,|,^}= imm` |

**Class 2 (call):** `sub = (word>>12) & 0xFFF`, operands start at
`pc + 1 + ((word>>8)&0xF)`, operand count `= word & 0xFF`, total length
`= 1 + ((word>>8)&0xF) + (word&0xFF)`. Operands are int32 immediates.

### Conditional jumps (exact, from `FUN_004472e0`)

The accumulator is a signed int (`ctx[0x2f]` in the effect VM, `ctx+0x1ec` in the
event VM). Target = `code[pc+1]`; when the branch is *not* taken, `pc += 2`.

| class | jumps when |
|---|---|
| `5` | `acc < 0` |
| `6` | `acc == 0` |
| `7` | `acc <= 0` |
| `8` | `acc > 0` |
| `9` | `acc != 0` |
| `0xA` | `acc >= 0` |

## The two VMs have DISJOINT sub-op namespaces

There are two interpreters and **the same sub-op number means different things in
each**. Both dispatch through a bounded two-level jump table:

| VM | class table | sub-op table | bound | default |
|---|---|---|---|---|
| **Event** `FUN_00566350` | `0x56e060` | `0x56e0b0`, index = `sub` | **0..0x18f** | `0x56deb4` |
| **Effect/entity** `FUN_004472e0` | `0x449144` | `0x449194`, index = `sub-1` | **1..0x8e** | `0x4473e6` |

Every sub-op used below `0x18f` in the room scripts exceeds the effect VM's `0x8e`
bound, so **the script corpus runs on the event VM** — ignore the effect VM when
reading room scripts. The event VM implements **350 of 400** sub-ops; the
exhaustive list of the 82 that fall through to the default is in
`build/xso_ops/op_handler_map.txt`.

## Class-2 sub-ops (the ones needed for game logic)

Index `< 0x200` ⇒ `g_flags[idx]`; `>= 0x200` ⇒ script-local var. From the event
VM `FUN_00566350`:

| sub | op | effect |
|---|---|---|
| `0x5F` | test | `acc = (g_flags[op0] == op1) ? 1 : 0` |
| `0x60` | test2 | `acc = (g_flags[op0] == g_flags[op1])` — implemented, but **zero call sites in all 2,225 scripts**; dead in practice |
| `0x5c` | test-and | `acc = (g_flags[op0]==op1) && (g_flags[op2]==op3)` — two fused equality tests (always 4 operands, 32/32) |
| `0x5d` | test-or | `acc = (g_flags[op0]==op1) \|\| (g_flags[op2]==op3)` (always 4 operands, 30/30) |
| `0x61` | **compare** | `acc = cmp(g_flags[op0], sel=op1, op2)` |
| `0x62` | compare2 | `acc = cmp(g_flags[op0], sel=op1, g_flags[op2])` |
| `0x63` | compare-acc | `acc = cmp(g_flags[op0], sel=op1, acc)` — **RHS is the OLD accumulator; evaluate it before assigning `acc`** |
| `0x90` | bit test | `acc = ((g_flags[op0] & op1) != 0)`; op1 is always a power of two |
| `0x81` | **percent roll** | `acc = (xorshift128() >> 17) % 100 < op0` — RNG `FUN_00439120`, state at `0x74622c` |
| `0x5e` | — | **genuinely unimplemented in the real VM** (falls through to the default) |

### The comparison selector (`FUN_00570240`)

`0x61`/`0x62`/`0x63` share one dispatcher, and its operator encoding is exactly
**(jump class − 4)** — the same ordering as the class `5..0xA` conditional jumps:

| sel | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7+ |
|---|---|---|---|---|---|---|---|---|
| | `0` | `a < b` | `a == b` | `a <= b` | `a > b` | `a != b` | `a >= b` | `1` |

Corpus check: `0x61`'s selector only ever takes values in `{1,2,3,6}` — all inside
the valid `0..7` range.

### Entity queries (they read live game state, not flags)

| sub | effect |
|---|---|
| `0x131` | `acc = 1` if **any live entity** has `(entity[0x130] & mask) != 0`, where op0 maps `1→0x100, 2→0x200, 3→0x300, 4→0x400`. I.e. *"is any entity tagged group-N still alive?"* — **not** "all enemies dead". Registry: ptr array `DAT_00730098`, count `DAT_0073009c`. |
| `0x13d` | `acc = ` is entity `op0` inside the named region `op1`? (op1 is a **string**-pool index; AABB + optional Z-rotation) |
| `0x13f` | `acc = entity(op0) != NULL && entity->float@0x98 >= 1.0` (`+0x98`'s meaning is unknown) |
| `0x9a` | `acc = ` bit N of the DWORD at `0x0076bc80` (meaning unknown) |

Entity ids: **`13` = player**, `998` = self, `999` = a cached singleton; any other
id scans the registry for `entity[0x128] == id`.

> **⚠️ Unknown sub-ops leave `acc` STALE.** Skipping an unimplemented sub-op by
> length is safe for the *static* grant scan but **not at runtime**: a following
> conditional jump then branches on a value we never computed. Run
> `python tools/xso_audit.py` for the live count of such sites (475 before the
> comparison family was implemented; 300 after). This is a correctness metric —
> it should only ever go down.
| `0x64` | **set** | `g_flags[op0] = op1` ← the grant |
| `0x65` | copy | `g_flags[op0] = g_flags[op1]` |
| `0x66` | set_reg | `g_flags[op0] = acc` |
| `0x67` | add | `g_flags[op0] += op1` |
| `0x69` | sub | `g_flags[op0] -= op1` |
| `0x6B` | mul | `g_flags[op0] *= op1` |
| `0x71`/`0x73`/`0x75` | and/or/xor | bitwise |
| `0x77` | zero | `g_flags[op0] = 0` |
| `0x116` | give-item | grant item `op0` |
| `0xD5` | acquire box | "Acquired X" UI |
| `0xF2` | frame-wait | cutscene delay (op0 frames) |
| `0xF3` | dialog advance | — |
| `0x12C` | skill spawn | — |

The remaining ~180 sub-ops are effects / camera / entity / dialog. The VM skips
any unimplemented sub-op **by its encoded length**, so a script always runs
cleanly to its `end` — unknown ops never desync it.

### Worked example — a treasure chest (`S_20/S_2003/S_BOX01.XSO`)

```
0:   0x5F (327, 1)   test "already opened" flag 0x147
4:   class 6         jump-if-acc==0  (not opened → run open sequence)
6:   class 1         end             (opened → stop)
7…:  guarded branches: give-item + set its flag, gated on a progress flag (150)
184: 0x64 (327, 1)   mark chest opened
193: class 1         end
```

Running the VM on fresh flags evaluates the `0x5F` guard, branches, gives the
item, and sets the opened flag — a faithful chest open.

## String pool (object/state names)

After the code comes a string table the object ops index into:

```
header +0x20 : u32 string_count
code_end     : string_count x u32  offset table (relative to pool base)
pool_base    : NUL-terminated ASCII string pool   (= code_end + count*4)
string[i]    : pool_base + offset_table[i]
```

Object-op operands are string-pool indices, resolved to names (the game matches
objects by name via `stricmp` on `obj+0x318`, not by numeric index). E.g. in
`S_2005.XSO` the pool is `s_2005, WaterReflection, box_02, box_03, box_01, OPEN`,
so `0x169(2)` enables `box_02`.

## Character gating

The current character is **`g_flags[150]`** (1=Yunica, 2=Hugo, 3=Toal, 0=default;
= AP option +1). Recovered from scripts (Hugo model IDs 197/167 in the `150==2`
branch), address `0x76BB74` cross-checked vs the known weapon-level flag.

**Chests are gated in the scene-main `S_XXXX.XSO`** (runs on room entry), not the
box scripts. It branches on `g_flags[150]` and calls object-visibility ops:

| sub | op | effect (via the 0x572xxx object manager) |
|---|---|---|
| `0x169` | enable/show object (name = string op0) | `FUN_00572a30`: clears hide bits |
| `0x175` | set object to opened/gone state (name op0) | `FUN_00572f70` |
| `0x176` | (object state) | `FUN_00572fd0` |

CleriaCore runs the scene-main on a throwaway VM (seeded with current g_flags +
character), captures the enabled/opened box names, and whitelists chests — hiding
both the model and the interactable for the wrong character. Verified: S_2005 →
Yunica/Hugo see box_02+box_03, Toal sees box_01. The object ops themselves drive a
runtime object/actor manager (real entity infra) — that's M3; capturing their
*intent* (which names) is all chest-gating needs.

## VM state

- `g_flags[0x200]` — the unified game-state array (items, key items, skills,
  location flags). Same array the live game keeps at `0x0076B91C`.
- script-local vars (index ≥ 0x200), accumulator, PC.

## Validation

`cleria-xso --grants` over all 2,225 scripts extracts **7,496 grants**, a
**bit-exact multiset match** (1,096 distinct `(index, op, value)` tuples) to the
sibling project's `tools/xso_dis.py` — which itself is validated against live
play. Op breakdown: 7,265 `set`, 93 `sub`, 79 `or`, 59 `add`. Zero unreadable.

## Status & next

- ✅ Parser, decoder, static grant extraction.
- ✅ Dynamic executor: control flow, flag store/test, give-item, the comparison
  family, object show/hide. Chests branch and execute correctly.
- ✅ The complete op→handler map (`build/xso_ops/op_handler_map.txt`) — we know
  exactly which 82 sub-ops the real VM leaves unimplemented.
- ⬜ **300 runtime mis-branch sites remain** (`tools/xso_audit.py`). The biggest
  are entity/state queries we can't answer yet: `0x13d`/`0x13f` (BATTLE.XSO),
  `0x9a` (GROW\*.XSO, level growth), `0x139`, `0x131`.
- ⬜ **Barrier fights are blocked on the entity system, not on script RE.**
  `KEKKAIZAKO_01.XSO` is a *polling loop* around `0x131`, so a constant answer
  cannot work: returning 0 grants the flag instantly, returning 1 hangs forever.
  It needs a real "is that entity alive?" answer.

> **Grant-count note.** The scan reports **7,505** grants over the corpus. An
> earlier figure of 7,496 was measured before the archives were fully extracted,
> when `assets/` was a **JP/US mix**; `data_us.ni` overwrites **525 XSO scripts**
> with their US versions. The delta is **+9, −0**, entirely in
> `CLEAR_EVENT_1/2.XSO` — the US build simply grants nine more flags. Any
> validation number predating the full extraction refers to a different tree.

## Tools

| Tool | Does |
|---|---|
| `cleria-xso <file>` | disassemble |
| `cleria-xso --run <file>` | execute; print grants + items |
| `cleria-xso --grants <dir> <csv>` | static grant scan (validation) |
| `src/cleria/xso.{hpp,cpp}` | the VM |
