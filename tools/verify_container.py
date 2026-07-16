"""Validate the hypothesised Falcom sidecar container across every file.

Hypothesis (from S_0000.SOB / S_0001.SOB / S_0000.SEN):
    +0x00  char[4]  magic, e.g. "SOB\0"
    +0x04  u32      zero
    +0x08  u32      record_size   (stride, constant per format)
    +0x0C  u32      count
    +0x10  record[count]

Predicts exactly:  filesize == 16 + record_size * count
"""
import struct
import sys
from collections import defaultdict
from pathlib import Path

root = Path(sys.argv[1])
by_ext = defaultdict(lambda: {"ok": 0, "bad": [], "strides": set(),
                              "f4": set(), "counts": []})

for p in sorted({q.resolve() for q in root.rglob("*.*")}):
    b = p.read_bytes()
    if len(b) < 16:
        continue
    magic, f4, rec, cnt = struct.unpack_from("<4sIII", b, 0)
    if not (magic[3:4] == b"\0" and magic[:3].isalpha()):
        continue
    e = magic[:3].decode()
    d = by_ext[e]
    d["strides"].add(rec)
    d["f4"].add(f4)
    d["counts"].append(cnt)
    if len(b) == 16 + rec * cnt:
        d["ok"] += 1
    else:
        d["bad"].append((p.name, len(b), rec, cnt, 16 + rec * cnt))

print("%-6s %6s %6s  %-12s %-8s %s" %
      ("MAGIC", "OK", "BAD", "STRIDE(S)", "+0x04", "counts"))
print("-" * 78)
for e in sorted(by_ext):
    d = by_ext[e]
    n = d["ok"] + len(d["bad"])
    strides = ",".join(hex(s) for s in sorted(d["strides"])[:3])
    if len(d["strides"]) > 3:
        strides += f",+{len(d['strides'])-3}"
    cs = d["counts"]
    print("%-6s %6d %6d  %-12s %-8s min=%d max=%d" %
          (e, d["ok"], len(d["bad"]), strides,
           ",".join(hex(x) for x in sorted(d["f4"]))[:8],
           min(cs), max(cs)))
    for nm, got, rec, cnt, want in d["bad"][:3]:
        print(f"         ! {nm}: size {got}, predicted {want} "
              f"(stride {rec:#x} x {cnt})")

tot_ok = sum(d["ok"] for d in by_ext.values())
tot_bad = sum(len(d["bad"]) for d in by_ext.values())
print("-" * 78)
print(f"container model holds for {tot_ok}/{tot_ok+tot_bad} files "
      f"across {len(by_ext)} formats")
