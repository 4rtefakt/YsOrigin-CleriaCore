#!/usr/bin/env python3
"""Full-state dissection of a Ys Origin save, item ids resolved against INVINFO.

Builds on tools/save_decode.py (the validated two-key decoder). Prints identity,
vitals, every equipment slot with its English item name, and the 7-float player
stat mirror at 0x76a72c (HP cur/max, SP gauge cur/max, final ATK, final DEF,
play-time) — see build/menu_re/SAVELOAD.md §4.5.

Usage: python tools/save_dissect.py <yso_NN.bin> [INVINFO.DAT]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from save_decode import decode, u32, f32, cstr, BASE, CHR, DIF  # noqa: E402

# English (data_us) INVINFO preferred; assets/MISC is the Japanese build.
_REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_INV_CANDIDATES = [
    os.path.join(_REPO, "build", "enemy_re", "misc_us", "MISC", "INVINFO.DAT"),
    os.path.join(_REPO, "assets", "MISC", "INVINFO.DAT"),
]


def i32(rec, va):
    return struct.unpack_from("<i", rec, va - BASE)[0]


def load_items(path=None):
    for p in ([path] if path else []) + _INV_CANDIDATES:
        if p and os.path.exists(p):
            raw = open(p, "rb").read()
            rec_size, count = struct.unpack_from("<II", raw, 8)  # 16-byte header
            names = []
            for k in range(count):
                nm = raw[16 + k * rec_size:16 + k * rec_size + 0x20]
                z = nm.find(b"\0")
                names.append((nm[:z] if z >= 0 else nm).decode("latin1", "replace"))
            return names
    return []


def nm(names, i):
    if i is None or i >= len(names) or i < 0 or i == 0xFFFFFFFF:
        return "(none)"
    return f"{names[i]!r} [id {i}]"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    path = sys.argv[1]
    names = load_items(sys.argv[2] if len(sys.argv) > 2 else None)
    d = decode(path)
    ch = u32(d, 0x76BB74)
    print(f"===== {os.path.basename(path)} =====")
    print(f"Character : {CHR.get(ch, '?')}  (index {ch})")
    print(f"Difficulty: {DIF.get(u32(d, 0x76BB5C), '?')}")
    print(f"Save name : {cstr(d, 0x90, 0x1f).strip()!r}")
    print(f"Date/time : {cstr(d, 0x7c, 10)}  {cstr(d, 0x87, 9)}")
    # UI name -> engine address. NB the game's "SP" is a CURRENCY (0x76a75c), NOT the
    # code's 3000-capped "SP gauge" (0x76a738); "EX" is the Boost gauge (0x76c1b4).
    print(f"\n---- vitals ----")
    print(f"HP      : {f32(d, 0x76A72C):.0f} / {f32(d, 0x76A730):.0f}")
    print(f"ATK/DEF : {f32(d, 0x76A73C):.1f} / {f32(d, 0x76A740):.1f}")
    print(f"SP (cur): {u32(d, 0x76A75C):,}      # currency, no cap")
    print(f"EXP     : {f32(d, 0x76A748):,.0f}   # toward next level")
    print(f"EX/Boost: {f32(d, 0x76C1B4):.0f} / 30000  ({f32(d, 0x76C1B4)/300:.0f}%)")
    print(f"Air     : {f32(d, 0x76C1B8):.0f} / 6000  (underwater oxygen)")
    print(f"skillgge: {f32(d, 0x76A738):.0f} / {f32(d, 0x76A734):.0f}  (code-'SP', regen skill gauge)")
    print(f"\n---- equipment ----")
    print(f"Weapon   : {nm(names, u32(d, 0x76BB78))}  tier {i32(d, 0x76A634 + ch*8)}  "
          f"cachedATK {f32(d, 0x76A638 + ch*8):.0f}")
    print(f"Armor    : {nm(names, u32(d, 0x76BB7C))}")
    print(f"Footwear : {nm(names, u32(d, 0x76BB80))}")
    print(f"Accessory: {nm(names, u32(d, 0x76A628))}")
    print(f"Tool     : {nm(names, u32(d, 0x76A62C))}")
    print(f"Magic    : {nm(names, u32(d, 0x76A630))}")
    print(f"Abil flags: 0x{u32(d, 0x76A624):08x}")


if __name__ == "__main__":
    main()
