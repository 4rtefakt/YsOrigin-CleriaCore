#!/usr/bin/env python3
"""Decode Ys Origin save slots (yso_NN.bin, 0x1FC8 bytes).

Validated against 26 real Steam-cloud saves. Reverses the two-key rolling cipher
(build/menu_re/SAVELOAD.md §4.1) and maps the payload back onto the live game-state
block at 0x0076a2d0 -- so any global RE'd in build/*_re can be read from a save.

Cipher: per-span seed s; s = (s*0x3d09)&0xffffffff; byte -= (s>>16)&0xff  (load).
  - key A @ +0x10 descrambles the +0x30..+0xd0 header span.
  - key B @ +0xc0 (INSIDE the key-A span, so descramble that span first) descrambles
    the +0xd0..+0x1fc8 payload.
Record +0xd0 == game-state global 0x0076a2d0 + 0xd0; i.e. payload byte at record
offset O == global 0x0076a2d0 + O.

Usage: python tools/save_decode.py <yso_NN.bin | dir>   (dir = summarise every slot)
"""
import glob
import os
import struct
import sys

BASE = 0x0076A2D0
CHR = {0: "Adol", 1: "Yunica", 2: "Hugo", 3: "Toal"}
DIF = {0: "VeryEasy", 1: "Easy", 2: "Normal", 3: "Hard", 4: "Nightmare"}


def _descramble(buf, off, length, key):
    b = bytearray(buf)
    s = key & 0xFFFFFFFF
    for i in range(off, off + length):
        s = (s * 0x3D09) & 0xFFFFFFFF
        b[i] = (b[i] - ((s >> 16) & 0xFF)) & 0xFF
    return bytes(b)


def decode(path):
    """Return the fully-descrambled 0x1FC8-byte record (header + game-state payload)."""
    raw = open(path, "rb").read()
    if len(raw) != 0x1FC8 or raw[:3] != b"YsO":
        raise ValueError(f"{path}: not a 0x1FC8 YsO save")
    key_a = struct.unpack_from("<I", raw, 0x10)[0]
    d = _descramble(raw, 0x30, 0xA0, key_a)          # header span
    key_b = struct.unpack_from("<I", d, 0xC0)[0]     # key B was inside that span
    return _descramble(d, 0xD0, 0x1FC8 - 0xD0, key_b)  # payload


def u32(rec, va):
    return struct.unpack_from("<I", rec, va - BASE)[0]


def f32(rec, va):
    return struct.unpack_from("<f", rec, va - BASE)[0]


def cstr(rec, off, n):
    x = rec[off:off + n]
    z = x.find(b"\0")
    return (x[:z] if z >= 0 else x).decode("latin1", "replace")


def summarise(path):
    d = decode(path)
    return {
        "file": os.path.basename(path),
        "char": CHR.get(u32(d, 0x76BB74), "?"),
        "difficulty": DIF.get(u32(d, 0x76BB5C), "?"),
        "location": cstr(d, 0x90, 0x1F).strip(),   # header +0x90 area line
        "date": cstr(d, 0x7C, 10),
        "sp": u32(d, 0x76A75C),   # UI "SP" currency (uncapped); NOT the code's SP gauge
        "hp": round(f32(d, 0x76A72C)),
        "o2": round(f32(d, 0x76C1B8)),
        "weapon_id": u32(d, 0x76BB78),
        "armor_id": u32(d, 0x76BB7C),
        "equipped_skill": u32(d, 0x76A630),
        "story_bits": u32(d, 0x76BC80),
    }


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    arg = sys.argv[1]
    files = sorted(glob.glob(os.path.join(arg, "yso_*.bin"))) if os.path.isdir(arg) else [arg]
    hdr = f"{'file':12} {'char':7} {'diff':9} {'location':26} {'SP':>9} {'HP':>4} {'wpn':>3} {'date'}"
    print(hdr)
    for f in files:
        try:
            s = summarise(f)
        except Exception as e:
            print(f"{os.path.basename(f):12} ERROR {e}")
            continue
        print(f"{s['file']:12} {s['char']:7} {s['difficulty']:9} {s['location'][:26]:26} "
              f"{s['sp']:>9} {s['hp']:>4} {s['weapon_id']:>3} {s['date']}")


if __name__ == "__main__":
    main()
