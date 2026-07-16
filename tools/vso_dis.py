"""Direct3D 9 shader bytecode disassembler (.VSO / .PSO).

Handles the full D3D9 token stream:
  * version token          0xFFFE_MMmm (vs) / 0xFFFF_MMmm (ps)
  * D3DSIO_COMMENT  0xFFFE, DWORD length in bits 16..30 -> skipped,
                    and if the payload starts with 'CTAB' the
                    D3DXSHADER_CONSTANTTABLE is decoded (creator string,
                    constant names, register set/index/count).
  * D3DSIO_END      0x0000FFFF
  * D3DSIO_DCL / D3DSIO_DEF / D3DSIO_DEFI / D3DSIO_DEFB special forms
  * co-issue (+), instruction predication, dest write mask / shift / saturate,
    source swizzle and source modifiers.

Usage:
    python tools/vso_dis.py <file.VSO|file.PSO> [...]
    python tools/vso_dis.py --json <file>     # machine-readable summary
"""
import struct, sys, os, json

# ---------------------------------------------------------------- opcode table
# name, #source operands (dest counted separately; -1 = special form)
OPS = {
    0x00: ("nop", 0, 0), 0x01: ("mov", 1, 1), 0x02: ("add", 1, 2),
    0x03: ("sub", 1, 2), 0x04: ("mad", 1, 3), 0x05: ("mul", 1, 2),
    0x06: ("rcp", 1, 1), 0x07: ("rsq", 1, 1), 0x08: ("dp3", 1, 2),
    0x09: ("dp4", 1, 2), 0x0A: ("min", 1, 2), 0x0B: ("max", 1, 2),
    0x0C: ("slt", 1, 2), 0x0D: ("sge", 1, 2), 0x0E: ("exp", 1, 1),
    0x0F: ("log", 1, 1), 0x10: ("lit", 1, 1), 0x11: ("dst", 1, 2),
    0x12: ("lrp", 1, 3), 0x13: ("frc", 1, 1),
    0x14: ("m4x4", 1, 2), 0x15: ("m4x3", 1, 2), 0x16: ("m3x4", 1, 2),
    0x17: ("m3x3", 1, 2), 0x18: ("m3x2", 1, 2),
    0x19: ("call", 0, 1), 0x1A: ("callnz", 0, 2), 0x1B: ("loop", 0, 2),
    0x1C: ("ret", 0, 0), 0x1D: ("endloop", 0, 0), 0x1E: ("label", 0, 1),
    0x1F: ("dcl", -1, 0),
    0x20: ("pow", 1, 2), 0x21: ("crs", 1, 2), 0x22: ("sgn", 1, 3),
    0x23: ("abs", 1, 1), 0x24: ("nrm", 1, 1), 0x25: ("sincos", 1, 3),
    0x26: ("rep", 0, 1), 0x27: ("endrep", 0, 0), 0x28: ("if", 0, 1),
    0x29: ("ifc", 0, 2), 0x2A: ("else", 0, 0), 0x2B: ("endif", 0, 0),
    0x2C: ("break", 0, 0), 0x2D: ("breakc", 0, 2), 0x2E: ("mova", 1, 1),
    0x2F: ("defb", -1, 0), 0x30: ("defi", -1, 0),
    0x40: ("texcoord", 1, 0), 0x41: ("texkill", 1, 0), 0x42: ("tex", 1, 0),
    0x43: ("texbem", 1, 1), 0x44: ("texbeml", 1, 1), 0x45: ("texreg2ar", 1, 1),
    0x46: ("texreg2gb", 1, 1), 0x47: ("texm3x2pad", 1, 1),
    0x48: ("texm3x2tex", 1, 1), 0x49: ("texm3x3pad", 1, 1),
    0x4A: ("texm3x3tex", 1, 1), 0x4B: ("reserved0", 0, 0),
    0x4C: ("texm3x3spec", 1, 2), 0x4D: ("texm3x3vspec", 1, 1),
    0x4E: ("expp", 1, 1), 0x4F: ("logp", 1, 1), 0x50: ("cnd", 1, 3),
    0x51: ("def", -1, 0), 0x52: ("texreg2rgb", 1, 1),
    0x53: ("texdp3tex", 1, 1), 0x54: ("texm3x2depth", 1, 1),
    0x55: ("texdp3", 1, 1), 0x56: ("texm3x3", 1, 1), 0x57: ("texdepth", 1, 0),
    0x58: ("cmp", 1, 3), 0x59: ("bem", 1, 2), 0x5A: ("dp2add", 1, 3),
    0x5B: ("dsx", 1, 1), 0x5C: ("dsy", 1, 1), 0x5D: ("texldd", 1, 4),
    0x5E: ("setp", 1, 2), 0x5F: ("texldl", 1, 2), 0x60: ("breakp", 0, 1),
}

# D3DSHADER_PARAM_REGISTER_TYPE
REGTYPE = {
    0: "r", 1: "v", 2: "c", 3: "t", 4: "oRAST", 5: "oD", 6: "oT",
    7: "i", 8: "oC", 9: "oDepth", 10: "s", 11: "c", 12: "c", 13: "c",
    14: "b", 15: "aL", 16: "rh", 17: "misc", 18: "label", 19: "p",
}
RASTOUT = {0: "oPos", 1: "oFog", 2: "oPts"}
SRCMOD = {
    0: "%s", 1: "-%s", 2: "%s_bias", 3: "-%s_bias", 4: "%s_bx2",
    5: "-%s_bx2", 6: "1-%s", 7: "%s_x2", 8: "-%s_x2", 9: "%s_dz",
    10: "%s_dw", 11: "abs(%s)", 12: "-abs(%s)", 13: "!%s",
}
DECLUSAGE = {
    0: "position", 1: "blendweight", 2: "blendindices", 3: "normal",
    4: "psize", 5: "texcoord", 6: "tangent", 7: "binormal", 8: "tessfactor",
    9: "positiont", 10: "color", 11: "fog", 12: "depth", 13: "sample",
}
SW = "xyzw"


def regtype(tok):
    return ((tok & 0x70000000) >> 28) | ((tok & 0x00001800) >> 8)


def regname(tok, is_vs):
    t = regtype(tok)
    num = tok & 0x7FF
    if t == 4:
        return RASTOUT.get(num, "oRAST%d" % num)
    if t == 3 and is_vs:
        return "a%d" % num
    base = REGTYPE.get(t, "?%d" % t)
    if t in (11, 12, 13):          # CONST2/3/4 banks are c-space continuations
        num += {11: 2048, 12: 4096, 13: 6144}[t]
    return "%s%d" % (base, num)


def rel_str(tok, toks, i, is_vs):
    """Relative addressing: bit 13 set -> a0.x (vs_1_1 implicit)."""
    return "[a0.x]" if (tok & 0x00002000) else ""


def src_str(tok, is_vs):
    s = regname(tok, is_vs) + ("[a0.x]" if (tok & 0x2000) else "")
    swz = (tok >> 16) & 0xFF
    comps = "".join(SW[(swz >> (2 * i)) & 3] for i in range(4))
    if comps != "xyzw":
        s += "." + (comps[0] if len(set(comps)) == 1 else comps)
    return SRCMOD.get((tok >> 24) & 0xF, "%s?") % s


def dst_str(tok, is_vs):
    s = regname(tok, is_vs)
    mask = (tok >> 16) & 0xF
    if mask != 0xF:
        s += "." + "".join(SW[i] for i in range(4) if mask & (1 << i))
    mod = (tok >> 20) & 0xF
    pre = ""
    if mod & 0x1: pre += "_sat"
    if mod & 0x2: pre += "_pp"
    if mod & 0x4: pre += "_centroid"
    shift = (tok >> 24) & 0xF
    if shift:
        shift = shift - 16 if shift > 7 else shift
        pre += "_x%d" % (2 ** shift) if shift > 0 else "_d%d" % (2 ** -shift)
    return pre, s


def decode_ctab(payload):
    """Decode a D3DXSHADER_CONSTANTTABLE blob (payload starts with 'CTAB')."""
    def sz(off):
        e = payload.index(b"\0", off)
        return payload[off:e].decode("ascii", "replace")
    size, creator, ver, nconst, coff, flags, target = struct.unpack_from("<7I", payload, 4)
    out = {"creator": sz(creator + 4), "target": sz(target + 4) if target else "",
           "version": "%d_%d" % ((ver >> 8) & 0xFF, ver & 0xFF), "constants": []}
    RS = {0: "b", 1: "i", 2: "c", 3: "s"}
    for k in range(nconst):
        name, regset, regidx, regcnt, tinfo, dflt = struct.unpack_from(
            "<H H H H I I".replace(" ", ""), payload, coff + 4 + k * 20)
        # actual layout: Name(u32) RegisterSet(u16) RegisterIndex(u16)
        #                RegisterCount(u16) Reserved(u16) TypeInfo(u32) DefaultValue(u32)
        name, regset, regidx, regcnt, _res, tinfo, dflt = struct.unpack_from(
            "<IHHHHII", payload, coff + 4 + k * 20)
        out["constants"].append({"name": sz(name + 4), "set": RS.get(regset, "?"),
                                 "reg": regidx, "count": regcnt})
    return out


def disasm(path):
    d = open(path, "rb").read()
    toks = list(struct.unpack("<%dI" % (len(d) // 4), d[:len(d) // 4 * 4]))
    ver = toks[0]
    kind = "vs" if (ver >> 16) == 0xFFFE else ("ps" if (ver >> 16) == 0xFFFF else "??")
    is_vs = kind == "vs"
    r = {"file": os.path.basename(path), "bytes": len(d), "kind": kind,
         "version": "%s_%d_%d" % (kind, (ver >> 8) & 0xFF, ver & 0xFF),
         "lines": [], "dcl": [], "consts": set(), "outputs": set(),
         "temps": set(), "samplers": set(), "defs": {}, "icount": 0, "ctab": None}
    i = 1
    while i < len(toks):
        t = toks[i]
        if t == 0x0000FFFF:
            r["lines"].append("end")
            break
        if (t & 0xFFFF) == 0xFFFE:                       # D3DSIO_COMMENT
            n = (t >> 16) & 0x7FFF
            blob = d[(i + 1) * 4:(i + 1 + n) * 4]
            if blob[:4] == b"CTAB":
                try:
                    r["ctab"] = decode_ctab(blob)
                except Exception as e:
                    r["ctab"] = {"error": repr(e)}
            r["lines"].append("// comment chunk, %d dwords%s" %
                              (n, " (CTAB)" if blob[:4] == b"CTAB" else ""))
            i += 1 + n
            continue
        op = t & 0xFFFF
        name, ndst, nsrc = OPS.get(op, ("unknown_%#x" % op, 0, 0))
        coissue = "+" if (t & 0x40000000) else " "

        if op == 0x1F:                                   # dcl
            usage_tok, dst = toks[i + 1], toks[i + 2]
            _, ds = dst_str(dst, is_vs)
            if regtype(dst) == 10:                       # sampler dcl (ps_2+)
                r["lines"].append("    dcl %s" % ds)
                r["samplers"].add(ds)
            else:
                u = usage_tok & 0x1F
                ui = (usage_tok >> 16) & 0xF
                un = "%s%s" % (DECLUSAGE.get(u, "usage%d" % u), ui if ui else "")
                r["lines"].append("    dcl_%-14s %s" % (un, ds))
                r["dcl"].append((un, ds))
            i += 3
            continue

        if op == 0x51:                                   # def cN, f,f,f,f
            dst = toks[i + 1]
            f = struct.unpack("<4f", struct.pack("<4I", *toks[i + 2:i + 6]))
            n_ = regname(dst, is_vs)
            r["defs"][n_] = f
            r["lines"].append("    def %s, %g, %g, %g, %g" % (n_, *f))
            i += 6
            continue
        if op == 0x30:                                   # defi
            dst = toks[i + 1]
            v = struct.unpack("<4i", struct.pack("<4I", *toks[i + 2:i + 6]))
            r["lines"].append("    defi %s, %d, %d, %d, %d" % (regname(dst, is_vs), *v))
            i += 6
            continue
        if op == 0x2F:                                   # defb
            r["lines"].append("    defb %s, %s" % (regname(toks[i + 1], is_vs),
                                                   bool(toks[i + 2])))
            i += 3
            continue

        args = toks[i + 1:i + 1 + ndst + nsrc]
        parts, pre = [], ""
        if ndst:
            pre, s = dst_str(args[0], is_vs)
            parts.append(s)
            dt = regtype(args[0])
            if dt == 0: r["temps"].add(args[0] & 0x7FF)
            if dt in (4, 5, 6, 8, 9): r["outputs"].add(s.split(".")[0])
        for a in args[ndst:]:
            parts.append(src_str(a, is_vs))
            if regtype(a) in (2, 11, 12, 13):
                r["consts"].add(regname(a, is_vs))
            if regtype(a) == 10:
                r["samplers"].add(regname(a, is_vs))
        r["icount"] += 1
        r["lines"].append("  %s %-7s %s" % (coissue, name + pre,
                                            ", ".join(parts)) if parts
                          else "  %s %s" % (coissue, name + pre))
        i += 1 + ndst + nsrc
    r["consts"] = sorted(r["consts"], key=lambda s: int(s[1:]))
    r["outputs"] = sorted(r["outputs"])
    r["temps"] = sorted(r["temps"])
    r["samplers"] = sorted(r["samplers"])
    return r


def render(r):
    o = ["// %s  %s  (%d bytes, %d instructions)" %
         (r["file"], r["version"], r["bytes"], r["icount"])]
    if r["ctab"]:
        o.append("// CTAB creator=%r target=%r" % (r["ctab"].get("creator"),
                                                   r["ctab"].get("target")))
        for c in r["ctab"].get("constants", []):
            o.append("//   %-24s %s%d  count=%d" % (c["name"], c["set"], c["reg"], c["count"]))
    else:
        o.append("// (no CTAB / no comment chunks -- constant names stripped)")
    o += r["lines"]
    o.append("//")
    o.append("// const regs read : %s" % (", ".join(r["consts"]) or "-"))
    o.append("// outputs written : %s" % (", ".join(r["outputs"]) or "-"))
    o.append("// temps used      : %s" % (", ".join("r%d" % t for t in r["temps"]) or "-"))
    return "\n".join(o)


if __name__ == "__main__":
    a = sys.argv[1:]
    as_json = "--json" in a
    files = [x for x in a if x != "--json"]
    if as_json:
        out = []
        for f in files:
            r = disasm(f)
            r["dcl"] = ["%s:%s" % (u, x) for u, x in r["dcl"]]
            r["defs"] = {k: list(v) for k, v in r["defs"].items()}
            out.append(r)
        print(json.dumps(out, indent=1))
    else:
        for f in files:
            print(render(disasm(f)))
            print()
