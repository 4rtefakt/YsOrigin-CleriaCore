"""Annotate capstone disassembly (tools/exe_peek.py output) with the float value of every
[0x6bxxxx]-style .rdata constant it reads.

    python tools/exe_peek.py 45a510 8100 | python tools/annotate_consts.py [first last]

Optional first/last are 1-based line numbers to keep. f64 is used for *sd instructions.
"""
import os
import re
import struct
import sys

import pefile

EXE = os.environ.get("YSO_EXE", r"D:\SteamLibrary\steamapps\common\Ys Origin\yso_win.exe")
pe = pefile.PE(EXE, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase


def read(va, n):
    rva = va - base
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            off = s.PointerToRawData + rva - s.VirtualAddress
            return pe.__data__[off:off + n]
    return None


lines = sys.stdin.read().splitlines()
if len(sys.argv) == 3:
    lines = lines[int(sys.argv[1]) - 1:int(sys.argv[2])]
for l in lines:
    m = re.search(r"\[0x(6[89ab][0-9a-f]{4})\]", l)
    if m:
        va = int(m.group(1), 16)
        dbl = re.search(r"\b\w+sd\b", l) is not None
        b = read(va, 8 if dbl else 4)
        if b:
            v = struct.unpack("<d" if dbl else "<f", b)[0]
            l += "   ; %.6g" % v
    print(l)
