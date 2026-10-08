"""Locate IDirect3DDevice9::SetVertexShaderConstantF call sites in yso_win.exe
and recover the StartRegister immediate where it is pushed as a literal.

IDirect3DDevice9 vtable index 94 -> offset 94*4 = 0x178.
Call forms scanned: FF /2 with disp32 0x178 (call dword ptr [reg+0x178]).

stdcall arg order pushed right-to-left:
    push Vector4fCount ; push pConstantData ; push StartRegister ; push this
    call [reg+0x178]
so StartRegister is the 3rd-from-last push before the call. We only report it
when it is a `push imm8` (6A xx) or `push imm32` (68 xx xx xx xx), which is the
common case for a hard-coded register index.
"""
import os
import struct, sys, re
from collections import Counter

VTBL_OFF = 0x178


def load(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 24 + 28)[0]
    secs, off = [], pe + 24 + optsz
    for _ in range(nsec):
        name = d[off:off + 8].rstrip(b"\0").decode()
        vs, va, rs, ra = struct.unpack_from("<IIII", d, off + 8)
        secs.append((name, va, vs, ra, rs))
        off += 40
    return d, base, secs


def pushes_before(d, o, n=40):
    """Walk backwards collecting push-imm / generic pushes in order."""
    out = []
    i = o - n
    while i < o:
        b = d[i]
        if b == 0x6A:
            out.append(("imm", d[i + 1] if d[i + 1] < 0x80 else d[i + 1] - 256)); i += 2
        elif b == 0x68:
            out.append(("imm32", struct.unpack_from("<I", d, i + 1)[0])); i += 5
        elif 0x50 <= b <= 0x57:
            out.append(("reg", ["eax","ecx","edx","ebx","esp","ebp","esi","edi"][b - 0x50])); i += 1
        elif b == 0xFF and i + 1 < o and (d[i + 1] & 0x38) == 0x30:
            out.append(("mem", None)); i += 2
        else:
            i += 1
    return out


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else \
        os.environ.get("YSO_EXE", "D:/SteamLibrary/steamapps/common/Ys Origin/yso_win.exe")
    d, base, secs = load(path)
    tname, tva, tvs, tra, trs = [s for s in secs if s[0] == ".text"][0]
    text = d[tra:tra + trs]
    # FF /2 [reg+disp32] : modrm 0x90|reg  (reg != esp/ebp special cases ignored)
    pat = re.compile(rb"\xFF[\x90-\x97]" + re.escape(struct.pack("<I", VTBL_OFF)))
    hits = []
    for m in pat.finditer(text):
        o = tra + m.start()
        va = base + tva + m.start()
        ps = pushes_before(d, o)
        # last push is `this`; the one before it is StartRegister
        start = None
        if len(ps) >= 2 and ps[-2][0] in ("imm", "imm32"):
            start = ps[-2][1]
        hits.append((va, start, ps[-4:]))
    print("SetVertexShaderConstantF (vtbl+0x%X) call sites: %d\n" % (VTBL_OFF, len(hits)))
    print("VA        StartReg  (last 4 pushes before call)")
    for va, s, ps in hits:
        print("%08X  %-8s  %s" % (va, s if s is not None else "?", ps))
    known = Counter(s for _, s, _ in hits if s is not None)
    print("\nliteral StartRegister histogram: %s" % dict(sorted(known.items())))


main()
