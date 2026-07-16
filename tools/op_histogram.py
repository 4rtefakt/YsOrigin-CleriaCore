"""Histogram XSO class-2 sub-ops across every extracted event script.

Sizes the native op surface a reimplementation must provide: each distinct
sub-op is one host function CleriaCore has to implement.

Format per tools/xso_dis.py: 0x24-byte header, +0x1C = code length in words,
code at +0x24. class = w>>24. Class 2 = function call:
    sub-op = (w>>12)&0xfff, len = 1 + ((w>>8)&0xf) + (w&0xff)
"""

import struct
import sys
from collections import Counter
from pathlib import Path

FIXED_LEN = {
    0: 1, 1: 1, 3: 2, 4: 2, 5: 2, 6: 2, 7: 2, 8: 2, 9: 2, 0xA: 2,
    0xB: 2, 0xC: 1, 0xD: 1, 0xE: 2, 0xF: 2, 0x10: 2, 0x11: 2, 0x12: 2, 0x13: 2,
}

ops = Counter()          # sub-op -> total call sites
files_with = Counter()   # sub-op -> distinct scripts using it
classes = Counter()
bad = []
n_words = 0

root = Path(sys.argv[1])
# NB: Windows rglob is case-insensitive, so globbing both cases double-counts.
files = sorted({p.resolve() for p in root.rglob("*.[Xx][Ss][Oo]")})

for f in files:
    try:
        b = f.read_bytes()
        if b[:4] != b"XSR\0":
            bad.append((f.name, "magic"))
            continue
        code_words = struct.unpack_from("<I", b, 0x1C)[0]
        code = b[0x24:0x24 + code_words * 4]
        if len(code) < code_words * 4:
            bad.append((f.name, "truncated"))
            continue
        seen = set()
        i = 0
        while i < code_words:
            w = struct.unpack_from("<I", code, i * 4)[0]
            cls = w >> 24
            if cls == 2:
                sub = (w >> 12) & 0xFFF
                ln = 1 + ((w >> 8) & 0xF) + (w & 0xFF)
                ops[sub] += 1
                seen.add(sub)
            else:
                ln = FIXED_LEN.get(cls)
                if ln is None:
                    bad.append((f.name, f"class {cls:#x} @ word {i}"))
                    break
            classes[cls] += 1
            n_words += 1
            i += ln
        for s in seen:
            files_with[s] += 1
    except Exception as e:
        bad.append((f.name, repr(e)))

print(f"scripts parsed : {len(files)}")
print(f"instructions   : {n_words}")
print(f"parse failures : {len(bad)}")
if bad:
    for n, r in bad[:10]:
        print(f"   ! {n}: {r}")
print()
print(f"DISTINCT class-2 sub-ops (the native op surface) : {len(ops)}")
print(f"total class-2 call sites                         : {sum(ops.values())}")
print()

# Coverage curve: how many ops to cover X% of all call sites?
total = sum(ops.values())
run = 0
marks = {}
for rank, (op, c) in enumerate(ops.most_common(), 1):
    run += c
    for pct in (50, 80, 90, 95, 99, 100):
        if pct not in marks and run * 100 / total >= pct:
            marks[pct] = rank
print("coverage curve (ops needed to cover N% of all call sites):")
for pct in (50, 80, 90, 95, 99, 100):
    if pct in marks:
        print(f"   {pct:3d}%  ->  {marks[pct]:3d} ops")
print()
print("%-8s %8s %8s   %s" % ("SUB-OP", "CALLS", "SCRIPTS", "known"))
KNOWN = {
    0x64: "g_flags[a]=b GRANT", 0x65: "copy flag", 0x66: "flag=acc",
    0x67: "add", 0x69: "sub", 0x6B: "mul", 0x71: "and", 0x73: "or",
    0x75: "xor", 0x77: "zero", 0x5F: "test flag (guard)",
    0x116: "give-item", 0xD5: "acquire box", 0x7F: "weapon level",
    0xF2: "frame-wait", 0xF3: "dialog advance", 0x12C: "skill spawn",
}
for op, c in ops.most_common(45):
    print("%-8s %8d %8d   %s" % (hex(op), c, files_with[op], KNOWN.get(op, "")))
