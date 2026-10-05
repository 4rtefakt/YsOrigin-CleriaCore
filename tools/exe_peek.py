# usage: python tools/exe_peek.py <hexva> [nbytes=256]   -- capstone disassembly of yso_win.exe at a VA
#        python tools/exe_peek.py --f32 <hexva> [n] / --f64 <hexva> [n] / --u32 <hexva> [n]  -- read constants
import sys, struct, pefile, capstone
EXE = r"D:\SteamLibrary\steamapps\common\Ys Origin\yso_win.exe"
pe = pefile.PE(EXE, fast_load=True); base = pe.OPTIONAL_HEADER.ImageBase
def read(va, n):
    rva = va - base
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            off = s.PointerToRawData + rva - s.VirtualAddress
            return pe.__data__[off:off+n]
    raise SystemExit("VA not in image")
a = sys.argv[1:]
if a[0] in ("--f32", "--f64", "--u32"):
    va = int(a[1], 16); n = int(a[2]) if len(a) > 2 else 1
    fmt, sz = {"--f32": ("<f", 4), "--f64": ("<d", 8), "--u32": ("<I", 4)}[a[0]]
    b = read(va, sz * n)
    for i in range(n): print(hex(va + i*sz), struct.unpack_from(fmt, b, i*sz)[0])
else:
    va = int(a[0], 16); n = int(a[1]) if len(a) > 1 else 256
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for i in md.disasm(read(va, n), va): print(f"{i.address:08x}  {i.mnemonic} {i.op_str}")
