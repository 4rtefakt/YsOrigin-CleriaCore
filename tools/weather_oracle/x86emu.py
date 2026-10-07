# A tiny x86-32 interpreter for straight-line game-logic functions of yso_win.exe: the integer ALU,
# SSE scalar float, a few SSE2 double ops. Enough to execute the weather emitters' update bodies
# (FUN_0058e7c0, FUN_0058daa0) on the REAL bytes with stubbed calls, as an oracle for the port.
import struct, pefile, capstone
from capstone import x86 as X
import numpy as np

EXE = r"D:\SteamLibrary\steamapps\common\Ys Origin\yso_win.exe"
_pe = pefile.PE(EXE, fast_load=True)
_base = _pe.OPTIONAL_HEADER.ImageBase

def image_read(va, n):
    rva = va - _base
    for s in _pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            off = rva - s.VirtualAddress
            raw = _pe.__data__[s.PointerToRawData + off: s.PointerToRawData + min(off + n, s.SizeOfRawData)]
            return bytes(raw) + b"\0" * (n - len(raw))
    return None

f32 = lambda b: struct.unpack("<f", struct.pack("<I", b & 0xffffffff))[0]
def bits32(x): return struct.unpack("<I", struct.pack("<f", float(np.float32(x))))[0]
def bits64(x): return struct.unpack("<Q", struct.pack("<d", float(x)))[0]
def d64(b): return struct.unpack("<d", struct.pack("<Q", b & 0xffffffffffffffff))[0]
M32 = 0xffffffff

class Emu:
    def __init__(self):
        self.mem = {}           # byte address -> value (writes / explicit sets)
        self.reads = set()      # globals read that came from the image (diagnostic)
        self.r = {k: 0 for k in ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp")}
        self.xmm = [[0, 0, 0, 0] for _ in range(8)]   # 4 x u32 lanes
        self.zf = self.cf = self.sf = self.of = False
        self.fs0 = 0x00300000   # fs:[0] lives here
        self.calls = {}         # va -> fn(emu) -> None (sets eax), returns stack bytes to pop
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self.cache = {}
        self.trace = []

    # ---- memory ----
    def rb(self, a):
        a &= M32
        if a in self.mem: return self.mem[a]
        b = image_read(a, 1)
        if b is None: raise RuntimeError("read of unmapped %08x" % a)
        self.reads.add(a)
        return b[0]
    def rd(self, a, n=4):
        v = 0
        for i in range(n): v |= self.rb(a + i) << (8 * i)
        return v
    def wd(self, a, v, n=4):
        for i in range(n): self.mem[(a + i) & M32] = (v >> (8 * i)) & 0xff
    def setf(self, a, x): self.wd(a, bits32(x))
    def getf(self, a): return f32(self.rd(a))

    # ---- operands ----
    def ea(self, op):
        m = op.mem
        a = m.disp
        if m.base: a += self.r[self.reg(m.base)]
        if m.index: a += self.r[self.reg(m.index)] * m.scale
        if m.segment == X.X86_REG_FS: a += self.fs0
        return a & M32
    def reg(self, rid): return self.md.reg_name(rid)
    def isx(self, op): return op.type == X.X86_OP_REG and self.reg(op.reg).startswith("xmm")
    def xi(self, op): return int(self.reg(op.reg)[3:])
    def get(self, op, size=4):
        if op.type == X.X86_OP_REG:
            n = self.reg(op.reg)
            if n in self.r: return self.r[n]
            if n in ("cl", "al", "dl", "bl"): return self.r["e" + n[0] + "x"] & 0xff
            if n in ("ch", "ah", "dh", "bh"): return (self.r["e" + n[0] + "x"] >> 8) & 0xff
            raise RuntimeError("reg " + n)
        if op.type == X.X86_OP_IMM: return op.imm & M32
        return self.rd(self.ea(op), op.size)
    def put(self, op, v):
        v &= (1 << (8 * op.size)) - 1
        if op.type == X.X86_OP_REG:
            n = self.reg(op.reg)
            if n in self.r: self.r[n] = v & M32; return
            if n in ("cl", "al", "dl", "bl"):
                k = "e" + n[0] + "x"; self.r[k] = (self.r[k] & 0xffffff00) | v; return
            raise RuntimeError("put reg " + n)
        self.wd(self.ea(op), v, op.size)

    def flags_logic(self, v, size=4):
        v &= (1 << (8 * size)) - 1
        self.zf = v == 0; self.sf = bool(v >> (8 * size - 1)); self.cf = self.of = False
        self.pf = bin(v & 0xff).count("1") % 2 == 0      # even parity of the low byte
    def push(self, v): self.r["esp"] = (self.r["esp"] - 4) & M32; self.wd(self.r["esp"], v)
    def pop(self):
        v = self.rd(self.r["esp"]); self.r["esp"] = (self.r["esp"] + 4) & M32; return v

    # xmm helpers
    def xload_f(self, op):      # scalar float operand as np.float32
        if self.isx(op): return np.float32(f32(self.xmm[self.xi(op)][0]))
        return np.float32(f32(self.rd(self.ea(op))))
    def xset_f(self, i, x): self.xmm[i][0] = bits32(x)

    def run(self, entry, ret_sentinel=0xdeadbeef, max_steps=200000):
        self.push(ret_sentinel)
        pc = entry
        for _ in range(max_steps):
            if pc == ret_sentinel: return
            ins = self.cache.get(pc)
            if ins is None:
                code = image_read(pc, 16)
                ins = next(self.md.disasm(code, pc))
                self.cache[pc] = ins
            pc = self.step(ins)
        raise RuntimeError("step limit")

    def step(self, i):
        m, ops = i.mnemonic, i.operands
        nxt = i.address + i.size
        if m == "mov":
            self.put(ops[0], self.get(ops[1]))
        elif m == "lea":
            self.r[self.reg(ops[0].reg)] = self.ea(ops[1])
        elif m in ("add", "sub", "cmp"):
            a, b = self.get(ops[0]), self.get(ops[1])
            if ops[1].type == X.X86_OP_IMM and ops[1].size < ops[0].size: b = ops[1].imm & M32
            r = (a + b) if m == "add" else (a - b)
            sz = ops[0].size
            mask = (1 << (8 * sz)) - 1
            self.cf = (r > mask) if m == "add" else (a < b)
            self.zf = (r & mask) == 0; self.sf = bool((r >> (8 * sz - 1)) & 1)
            sb = 1 << (8 * sz - 1)
            self.of = bool(((a ^ r) & (b ^ r) if m == "add" else (a ^ b) & (a ^ r)) & sb)
            if m != "cmp": self.put(ops[0], r)
        elif m in ("xor", "and", "or", "test"):
            a, b = self.get(ops[0]), self.get(ops[1])
            r = a ^ b if m == "xor" else (a & b if m in ("and", "test") else a | b)
            self.flags_logic(r, ops[0].size)
            if m != "test": self.put(ops[0], r)
        elif m in ("shl", "shr"):
            a, c = self.get(ops[0]), self.get(ops[1]) & 31
            r = (a << c) & M32 if m == "shl" else a >> c
            self.flags_logic(r); self.put(ops[0], r)
        elif m == "inc":
            r = (self.get(ops[0]) + 1) & M32; self.zf = r == 0; self.put(ops[0], r)
        elif m == "neg":
            a = self.get(ops[0]); r = (-a) & M32; self.cf = a != 0; self.zf = r == 0; self.put(ops[0], r)
        elif m == "sbb":
            a, b = self.get(ops[0]), self.get(ops[1]); r = (a - b - int(self.cf)) & M32
            self.cf = a < b + int(self.cf); self.zf = r == 0; self.put(ops[0], r)
        elif m == "imul":
            if len(ops) == 3:
                a = self.get(ops[1]); b = ops[2].imm
                self.put(ops[0], (np.int32(np.uint32(a)).item() * b) & M32)
            else:
                raise RuntimeError("imul form")
        elif m == "div":
            d = self.get(ops[0]); n = (self.r["edx"] << 32) | self.r["eax"]
            self.r["eax"] = (n // d) & M32; self.r["edx"] = n % d
        elif m == "cmove":
            if self.zf: self.put(ops[0], self.get(ops[1]))
        elif m == "push": self.push(self.get(ops[0]))
        elif m == "pop": self.put(ops[0], self.pop())
        elif m == "nop": pass
        elif m == "int3": raise RuntimeError("int3 at %x" % i.address)
        elif m in ("je", "jne", "jb", "jmp"):
            t = ops[0].imm
            take = {"je": self.zf, "jne": not self.zf, "jb": self.cf, "jmp": True}[m]
            return t if take else nxt
        elif m == "call":
            t = ops[0].imm
            fn = self.calls.get(t)
            if fn is None: raise RuntimeError("unstubbed call %x at %x" % (t, i.address))
            argbytes = fn(self)
            self.r["esp"] = (self.r["esp"] + (argbytes or 0)) & M32
        elif m == "ret":
            v = self.pop()
            if ops: self.r["esp"] = (self.r["esp"] + ops[0].imm) & M32
            return v
        # ---- SSE ----
        elif m == "movss":
            if self.isx(ops[0]):
                j = self.xi(ops[0])
                if self.isx(ops[1]): self.xmm[j][0] = self.xmm[self.xi(ops[1])][0]
                else: self.xmm[j] = [self.rd(self.ea(ops[1])), 0, 0, 0]
            else:
                self.wd(self.ea(ops[0]), self.xmm[self.xi(ops[1])][0])
        elif m in ("addss", "subss", "mulss"):
            j = self.xi(ops[0]); a = np.float32(f32(self.xmm[j][0])); b = self.xload_f(ops[1])
            with np.errstate(all="ignore"):
                r = a + b if m == "addss" else (a - b if m == "subss" else a * b)
            self.xset_f(j, r)
        elif m in ("xorps",):
            j = self.xi(ops[0])
            src = self.xmm[self.xi(ops[1])] if self.isx(ops[1]) else [self.rd(self.ea(ops[1]) + 4 * k) for k in range(4)]
            self.xmm[j] = [a ^ b for a, b in zip(self.xmm[j], src)]
        elif m in ("movq",):
            if self.isx(ops[0]):
                j = self.xi(ops[0])
                if self.isx(ops[1]): s = self.xmm[self.xi(ops[1])]; self.xmm[j] = [s[0], s[1], 0, 0]
                else: a = self.ea(ops[1]); self.xmm[j] = [self.rd(a), self.rd(a + 4), 0, 0]
            else:
                a = self.ea(ops[0]); s = self.xmm[self.xi(ops[1])]; self.wd(a, s[0]); self.wd(a + 4, s[1])
        elif m in ("movups", "movaps"):
            if self.isx(ops[0]):
                j = self.xi(ops[0])
                if self.isx(ops[1]): self.xmm[j] = list(self.xmm[self.xi(ops[1])])
                else: a = self.ea(ops[1]); self.xmm[j] = [self.rd(a + 4 * k) for k in range(4)]
            else:
                a = self.ea(ops[0]); s = self.xmm[self.xi(ops[1])]
                for k in range(4): self.wd(a + 4 * k, s[k])
        elif m == "movd":
            if self.isx(ops[0]): self.xmm[self.xi(ops[0])] = [self.get(ops[1]), 0, 0, 0]
            else: self.put(ops[0], self.xmm[self.xi(ops[1])][0])
        elif m == "cvtdq2pd":
            j = self.xi(ops[0]); s = self.xmm[self.xi(ops[1])]
            lo = bits64(float(np.int32(np.uint32(s[0])))); hi = bits64(float(np.int32(np.uint32(s[1]))))
            self.xmm[j] = [lo & M32, lo >> 32, hi & M32, hi >> 32]
        elif m == "addsd":
            j = self.xi(ops[0]); a = d64(self.xmm[j][0] | (self.xmm[j][1] << 32))
            if self.isx(ops[1]): s = self.xmm[self.xi(ops[1])]; b = d64(s[0] | (s[1] << 32))
            else: b = d64(self.rd(self.ea(ops[1]), 8))
            r = bits64(a + b); self.xmm[j][0] = r & M32; self.xmm[j][1] = r >> 32
        elif m == "cvtpd2ps":
            j = self.xi(ops[0]); s = self.xmm[self.xi(ops[1])]
            a = d64(s[0] | (s[1] << 32)); b = d64(s[2] | (s[3] << 32))
            self.xmm[j] = [bits32(np.float32(a)), bits32(np.float32(b)), 0, 0]
        elif m == "shufps":
            j = self.xi(ops[0]); a = list(self.xmm[j]); b = list(self.xmm[self.xi(ops[1])]); imm = ops[2].imm
            self.xmm[j] = [a[imm & 3], a[(imm >> 2) & 3], b[(imm >> 4) & 3], b[(imm >> 6) & 3]]
        elif m == "unpcklps":
            j = self.xi(ops[0]); a = self.xmm[j]; b = self.xmm[self.xi(ops[1])] if self.isx(ops[1]) else [self.rd(self.ea(ops[1]) + 4 * k) for k in range(4)]
            self.xmm[j] = [a[0], b[0], a[1], b[1]]
        # ---- added for FireBlur's grid (gen_blur.py: FUN_005e44c0, FUN_00427c90) ----
        elif m == "divss":
            j = self.xi(ops[0]); a = np.float32(f32(self.xmm[j][0])); b = self.xload_f(ops[1])
            with np.errstate(all="ignore"): self.xset_f(j, a / b)
        elif m in ("comiss", "ucomiss"):
            a = np.float32(f32(self.xmm[self.xi(ops[0])][0])); b = self.xload_f(ops[1])
            un = bool(np.isnan(a) or np.isnan(b))
            self.zf = un or a == b; self.cf = un or a < b; self.pf = un
            self.sf = self.of = False
        elif m == "lahf":        # AH = SF ZF 0 AF 0 PF 1 CF
            ah = (int(self.sf) << 7) | (int(self.zf) << 6) | (int(getattr(self, "pf", False)) << 2) | 2 | int(self.cf)
            self.r["eax"] = (self.r["eax"] & 0xffff00ff) | (ah << 8)
        elif m in ("jl", "jbe", "ja", "jae", "jnp", "jp", "jg", "jge", "jle", "js", "jns"):
            pf = getattr(self, "pf", False)
            take = {"jl": self.sf != self.of, "jge": self.sf == self.of, "jg": not self.zf and self.sf == self.of,
                    "jle": self.zf or self.sf != self.of, "jbe": self.cf or self.zf, "ja": not self.cf and not self.zf,
                    "jae": not self.cf, "jnp": not pf, "jp": pf, "js": self.sf, "jns": not self.sf}[m]
            return ops[0].imm if take else nxt
        elif m == "cvtdq2ps":
            j = self.xi(ops[0])
            s = self.xmm[self.xi(ops[1])] if self.isx(ops[1]) else [self.rd(self.ea(ops[1])), 0, 0, 0]
            self.xmm[j] = [bits32(np.float32(np.int32(np.uint32(x)))) for x in s]
        elif m == "cmovne":
            if not self.zf: self.put(ops[0], self.get(ops[1]))
        elif m == "cvtsi2ss":
            self.xset_f(self.xi(ops[0]), np.float32(np.int32(np.uint32(self.get(ops[1])))))
        elif m == "dec":
            r = (self.get(ops[0]) - 1) & M32; self.zf = r == 0; self.sf = bool(r >> 31); self.put(ops[0], r)
        else:
            raise RuntimeError("unimplemented %s %s at %x" % (m, i.op_str, i.address))
        return nxt
