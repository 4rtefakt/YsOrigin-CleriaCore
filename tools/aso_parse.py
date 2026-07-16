#!/usr/bin/env python3
"""
aso_parse.py -- parser/disassembler for Ys Origin `.ASO` animation-script bytecode.

Container magic is "XSR\0" (shared with the .XSO scene scripts).

Layout (corpus-validated: for every .ASO in assets/ANI/, the section table ends
exactly where the code blob begins, and the last section ends exactly at EOF):

    0x00  char  magic[4]   = "XSR\0"
    0x04  u16   version    = 0x0010
    0x06  u16   unk        = 0x7c95   (constant across the whole corpus)
    0x08  u32   nSections
    0x0c  u32   unk2       (varies; 13 for player sets)
    0x10  Section[nSections]           stride 0x14
    ...   code blob                    (base = 0x10 + nSections*0x14)

    Section (0x14):
        +0x00 i32 id          animation id (sparse! e.g. 0,1,2,3,300,301)
        +0x04 i32 type        2 or 4
        +0x08 i32 codeOff     BYTE offset into the code blob
        +0x0c i32 codeLen     length in DWORDS
        +0x10 i32 unk         0 across the corpus

Instruction encoding -- 32-bit little-endian words:

    opcode    = w >> 24
    funcid    = (w >> 12) & 0xfff     (opcode 2 only)
    argOffset = (w >> 8)  & 0xf       (opcode 2 only)
    argCount  = w & 0xff              (opcode 2 only)

    For opcode 2: args start at pc+1+argOffset, and the next pc is
    argsStart+argCount.  The argOffset words between the opcode word and the
    args hold the nested-expression code.

Operand typing comes from assets/MISC/FUNCLIST.DEF, which the game ships: it is
the compiler's signature list, `name=sig`, where LINE INDEX == FUNCID and each
signature letter says how to reinterpret one 32-bit operand slot
(i=int32, f=float32, s=string, ...=varargs).
"""

import argparse
import glob
import os
import struct
import sys
from collections import Counter

HDR_SIZE = 0x10
SEC_SIZE = 0x14

# Top-level opcodes (from FUN_004472e0 @ 0x4472e0 disassembly).
OPCODES = {
    0x00: "nop", 0x01: "end", 0x02: "call", 0x03: "jmp", 0x04: "op04",
    0x05: "jl", 0x06: "jz", 0x07: "jle", 0x08: "jge", 0x09: "jnz",
    0x0a: "jge0", 0x0b: "jmp2", 0x0c: "clr", 0x0d: "clr2", 0x0e: "set",
    0x0f: "add", 0x10: "sub", 0x11: "and", 0x12: "or", 0x13: "xor",
}
# Opcodes taking one immediate/target word at pc+1.
OPCODES_1IMM = {0x03, 0x0b, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
                0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x04}


def load_funclist(path):
    """FUNCLIST.DEF -> {funcid: (name, sig)}.  Line index == funcid; blank
    lines are reserved slots and MUST be counted."""
    with open(path, "rb") as f:
        text = f.read().decode("cp932")
    table = {}
    for i, line in enumerate(text.split("\r\n")):
        line = line.strip()
        if not line or "=" not in line:
            continue
        name, _, sig = line.partition("=")
        table[i] = (name, sig)
    return table


class Section:
    __slots__ = ("id", "type", "off", "nwords", "unk", "words")

    def __repr__(self):
        return "<Section id=%d type=%d words=%d>" % (self.id, self.type, self.nwords)


class Aso:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        self.path = path
        d = self.data
        self.magic = d[:4]
        if self.magic != b"XSR\0":
            raise ValueError("%s: bad magic %r" % (path, self.magic))
        self.version, self.unk06 = struct.unpack("<HH", d[4:8])
        self.nsec, self.unk0c = struct.unpack("<II", d[8:16])
        self.code_base = HDR_SIZE + self.nsec * SEC_SIZE
        self.sections = []
        for i in range(self.nsec):
            o = HDR_SIZE + i * SEC_SIZE
            s = Section()
            s.id, s.type, s.off, s.nwords, s.unk = struct.unpack("<5i", d[o:o + SEC_SIZE])
            start = self.code_base + s.off
            s.words = list(struct.unpack("<%dI" % s.nwords,
                                         d[start:start + s.nwords * 4]))
            self.sections.append(s)

    def check(self):
        """Structural self-consistency; returns list of complaints."""
        bad = []
        if not self.sections:
            return ["no sections"]
        end = max(s.off + s.nwords * 4 for s in self.sections)
        if self.code_base + end != len(self.data):
            bad.append("code end %#x != EOF %#x" % (self.code_base + end, len(self.data)))
        # sections should tile the blob contiguously
        cur = 0
        for s in sorted(self.sections, key=lambda s: s.off):
            if s.off != cur:
                bad.append("section id=%d off %#x != expected %#x" % (s.id, s.off, cur))
            cur = s.off + s.nwords * 4
        return bad


def decode_section(sec, funclist):
    """Linear-sweep decode. Returns (list_of_insns, list_of_errors).
    insn = (pc, word, opcode, funcid, args_pc, argcount, nextpc)"""
    insns, errs = [], []
    pc = 0
    words = sec.words
    n = len(words)
    seen = set()
    while pc < n:
        if pc in seen:
            break
        seen.add(pc)
        w = words[pc]
        op = w >> 24
        if op not in OPCODES:
            errs.append("pc=%d undefined opcode %#x (word %#010x)" % (pc, op, w))
            break
        if op == 0x02:
            funcid = (w >> 12) & 0xfff
            argoff = (w >> 8) & 0xf
            argc = w & 0xff
            args_pc = pc + 1 + argoff
            nextpc = args_pc + argc
            insns.append((pc, w, op, funcid, args_pc, argc, nextpc))
            pc = nextpc
        elif op in OPCODES_1IMM:
            insns.append((pc, w, op, None, pc + 1, 1, pc + 2))
            pc += 2
        else:
            insns.append((pc, w, op, None, None, 0, pc + 1))
            pc += 1
    return insns, errs


def fmt_operand(raw, kind):
    if kind == "f":
        return "%g" % struct.unpack("<f", struct.pack("<I", raw))[0]
    if raw > 0x7fffffff:
        return str(raw - 0x100000000)
    return str(raw)


def disasm_section(aso, sec, funclist, out):
    insns, errs = decode_section(sec, funclist)
    out.append("  --- section id=%d type=%d words=%d ---" % (sec.id, sec.type, sec.nwords))
    for (pc, w, op, funcid, args_pc, argc, nextpc) in insns:
        if op == 0x02:
            name, sig = funclist.get(funcid, ("FUNC_%d" % funcid, "?"))
            args = sec.words[args_pc:args_pc + argc]
            parts = []
            for i, a in enumerate(args):
                kind = sig[i] if sig and i < len(sig) and sig != "..." else "i"
                parts.append(fmt_operand(a, kind))
            extra = ""
            if args_pc > pc + 1:
                extra = "  [expr:%d]" % (args_pc - pc - 1)
            out.append("  %4d: %08x  %s(%s)%s" % (pc, w, name, ", ".join(parts), extra))
        else:
            imm = ""
            if op in OPCODES_1IMM and args_pc is not None and args_pc < len(sec.words):
                imm = " %d" % sec.words[args_pc]
            out.append("  %4d: %08x  %s%s" % (pc, w, OPCODES.get(op, "?%#x" % op), imm))
    for e in errs:
        out.append("  !! %s" % e)
    return len(insns), len(errs)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*")
    ap.add_argument("--funclist", default="assets/MISC/FUNCLIST.DEF")
    ap.add_argument("--disasm", action="store_true")
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--section", type=int, default=None)
    args = ap.parse_args()

    funclist = load_funclist(args.funclist)
    files = args.files or sorted(glob.glob("assets/ANI/*.ASO"))

    tot_insn = tot_err = tot_sec = 0
    op_hist, fn_hist = Counter(), Counter()
    bad_files = []
    out = []
    for path in files:
        try:
            aso = Aso(path)
        except Exception as e:
            bad_files.append((path, str(e)))
            continue
        complaints = aso.check()
        if complaints:
            bad_files.append((path, "; ".join(complaints)))
        if args.disasm:
            out.append("===== %s  nsec=%d =====" % (os.path.basename(path), aso.nsec))
        for sec in aso.sections:
            if args.section is not None and sec.id != args.section:
                continue
            tot_sec += 1
            if args.disasm:
                i, e = disasm_section(aso, sec, funclist, out)
                tot_insn += i
                tot_err += e
            else:
                insns, errs = decode_section(sec, funclist)
                tot_insn += len(insns)
                tot_err += len(errs)
                for (pc, w, op, funcid, a, c, np) in insns:
                    op_hist[op] += 1
                    if op == 0x02:
                        fn_hist[funcid] += 1
    print("\n".join(out))
    if args.stats or not args.disasm:
        print("files=%d sections=%d instructions=%d decode_errors=%d structurally_bad=%d"
              % (len(files), tot_sec, tot_insn, tot_err, len(bad_files)))
        for p, why in bad_files:
            print("  BAD %s: %s" % (os.path.basename(p), why))
        print("\n-- opcode histogram --")
        for op, c in op_hist.most_common():
            print("  %#04x %-6s %6d" % (op, OPCODES.get(op, "?"), c))
        print("\n-- funcid histogram --")
        for fid, c in fn_hist.most_common(40):
            nm = funclist.get(fid, ("FUNC_%d" % fid, "?"))[0]
            print("  %#05x %-32s %6d" % (fid, nm, c))


if __name__ == "__main__":
    main()
