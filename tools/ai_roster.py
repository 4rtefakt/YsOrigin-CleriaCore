"""Which enemy brain runs where — the porting order for the enemy AI.

    python tools/ai_roster.py            # per-area table, play order
    python tools/ai_roster.py --brains   # one row per brain, first area it appears in

Chain (all read from the shipped data + the exe, nothing hand-entered):
  room SCT placements + XSO op 0x12c spawns   -> cast id
  CASTINFO +0x64                              -> AI index (actor +0x120)
  dispatch table 0x69b8c0[idx]                -> handler (FUN_0045a2f0 jmp)
  the call right before `call 0x479b00` in it -> the cast's brain
Area order is the MAP/S_xx folder order (S_10 = the tower's first section, ...).
"""
import os
import struct
import sys
from collections import OrderedDict, defaultdict

import capstone
import pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sct_enemies as se       # noqa: E402
import xso_spawns as xs        # noqa: E402

EXE = r"D:\SteamLibrary\steamapps\common\Ys Origin\yso_win.exe"
ROOT = se.ROOT
pe = pefile.PE(EXE, fast_load=True)
BASE = pe.OPTIONAL_HEADER.ImageBase
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def read(va, n):
    return pe.get_data(va - BASE, n)


def handler_of(ai_idx):
    if not (0 <= ai_idx < 94):
        return None
    return struct.unpack("<I", read(0x69B8C0 + ai_idx * 4, 4))[0]


def brain_of(handler):
    """The last direct call before `call 0x479b00` (the action player)."""
    prev = None
    for ins in md.disasm(read(handler, 0x900), handler):
        if ins.mnemonic == "call" and ins.op_str.startswith("0x"):
            tgt = int(ins.op_str, 16)
            if tgt == 0x479B00:
                return prev
            prev = tgt
        if ins.mnemonic == "ret":
            break
    return None


def main():
    cast = se.Cast()
    rows = OrderedDict()   # area -> {cast_id: set(rooms)}
    mapdir = os.path.join(ROOT, "MAP")
    for area in sorted(d for d in os.listdir(mapdir) if d.startswith("S_")):
        adir = os.path.join(mapdir, area)
        for room in sorted(os.listdir(adir)):
            rdir = os.path.join(adir, room)
            if not os.path.isdir(rdir):
                continue
            ids = set()
            sct = os.path.join(rdir, room + ".SCT")
            if os.path.exists(sct):
                d = open(sct, "rb").read()
                _, _, stride, count = struct.unpack_from("<IIII", d, 0)
                for i in range(count):
                    o = 0x10 + i * stride
                    name = d[o:o + 0x10].split(b"\0")[0].decode("ascii", "replace")
                    if name in cast.by_name:
                        ids.add(cast.by_name[name])
            for f in os.listdir(rdir):
                if f.upper().endswith(".XSO"):
                    code = xs.parse(os.path.join(rdir, f))
                    if code:
                        for _pc, ops in xs.spawns(code):
                            ids.add(ops[1])          # 0x12c(entity, cast_type, ...)
            for cid in ids:
                if 0 <= cid < cast.count and cast.actor_class(cid) == 0x10:
                    rows.setdefault(area, defaultdict(set))[cid].add(room)

    seen_brain = OrderedDict()
    for area, casts in rows.items():
        if "--brains" not in sys.argv:
            print(f"== {area}")
        for cid in sorted(casts):
            idx = cast.i32(cid, 0x64)
            h = handler_of(idx)
            b = brain_of(h) if h else None
            key = b or h
            if key not in seen_brain:
                seen_brain[key] = (area, cid, idx, h, b)
            if "--brains" not in sys.argv:
                print(f"  {cast.ident(cid):10s} {cast.display(cid):22s} idx {idx:3d} "
                      f"handler {h:#010x} brain {b and hex(b)} rooms {len(casts[cid])}")
    if "--brains" in sys.argv:
        for key, (area, cid, idx, h, b) in seen_brain.items():
            print(f"{area}  {cast.ident(cid):10s} {cast.display(cid):22s} idx {idx:3d} "
                  f"handler {h:#010x} brain {b and hex(b)}")


if __name__ == "__main__":
    main()
